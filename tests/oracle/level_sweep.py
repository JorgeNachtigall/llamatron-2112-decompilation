"""Level sweep (M4: every level script playable): start each level through the warp poke and
play it for a while with scripted moving and firing, on the port alone (no oracle).

Per level, in a child process of its own (so a crash is reported, not fatal):
  game_init as llamatron_replay does (VGA, keyboard, no sound, seed 0), then the app's
  sequencing (50.16 Hz timer ticks between 70.086 Hz display frames); Space at tick 150
  starts a game; at WARP_TICK the level is poked to L - 5 and wave_result (DS:0810) to 4
  (warp, +5 levels: pickup_effect_01 1000:0ff7), as the golden `warp` run pokes it; from
  then on the ship walks and fires in a fixed per-level pattern, with its lives topped up so
  the game never ends.

Checked after every tick and frame:
  - the level that starts after the warp is L, and its wave runs (tick hook play_tick_hook);
  - every live entity's think and anim index is inside its table and the entry is ported
    (not NULL), and so is the hit handler of a solid one (and its kill action, for hit 03);
  - the spawn type (DS:0900) names a ported enemy init;
  - live entities stay near the screen (|x|, |y| < OFF_SCREEN);
  - no draw cell falls outside its page (draw_drop_stats: where the original writes into
    the gap after a page or into another page, docs/DEVIATIONS.md #3 and #17).

Usage (from tests/, with the testlib built):
  LLAMATRON_GAME_TESTLIB=../build-prism/tests/libllamatron_game_testlib.dylib \\
      ../tools/harness/.venv/bin/python oracle/level_sweep.py [--levels 0-99] [--ticks N]
prints one line per level: `level L: ok` or `level L: <problem> at tick T`. --one L runs a
single level in this process and prints its result as JSON (what the sweep's children run).
--replay BIN runs the same input (written as PROBE.KEY / PROBE.POK) through a llamatron_replay
instead, e.g. a -fsanitize=address,undefined build, and reports crashes and sanitizer errors
(the framework Python drops DYLD_INSERT_LIBRARIES, so the testlib cannot run under ASan).
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import struct
import subprocess
import sys
from fractions import Fraction
from pathlib import Path

TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from adapters.cport import bind, load_testlib  # noqa: E402
from adapters.gamestate import DgroupMap  # noqa: E402

LEVELS = 100
WARP_LEVELS = 5
DEFAULT_TICKS = 5000
START_TICK, WARP_TICK, PLAY_FROM = 150, 300, 320
LEVEL_START_WITHIN = 400            # ticks from the warp poke until the new wave runs
OFF_SCREEN = 1000

# DGROUP offsets (tests/adapters/dgroup_map.c)
LIVES, LEVEL = 0x21A6, 0x21AA
WAVE_RESULT, WAVE_WARP = 0x0810, 4
SPAWN_TYPE = 0x0900
TICK_HOOK, PLAY_TICK_HOOK = 0x39E4, 0x1562
ENTITY_TABLE, ENTITY_SIZE, ENTITY_COUNT = 0x48C1, 0x40, 70
HIT_KILL_ACTION, KILL_ACTION_COUNT = 3, 12

# PC scan codes (src/engine/input.h)
SPACE, UP, LEFT, RIGHT, DOWN, BREAK = 0x39, 0x48, 0x4B, 0x4D, 0x50, 0x80
MOVES = [(UP,), (UP, RIGHT), (RIGHT,), (DOWN, RIGHT), (DOWN,), (DOWN, LEFT), (LEFT,),
         (UP, LEFT)]

# One timer tick and one VGA mode 0Dh frame, in seconds (src/engine/timing.h).
TICK = Fraction(0x5CEC * 264, 315_000_000)
FRAME = Fraction(800 * 449, 25_175_000)

HANDLER_TABLES = {          # name: (symbol, entry count), src/game/entity_dispatch.h
    "think": ("entity_think_handlers", 46),
    "anim": ("entity_anim_handlers", 19),
    "hit": ("entity_hit_handlers", 18),
    "enemy init": ("enemy_init_handlers", 41),
}


class GameLaunch(ctypes.Structure):
    _fields_ = [("vga", ctypes.c_bool), ("joystick", ctypes.c_bool), ("sound", ctypes.c_bool),
                ("flash_mode", ctypes.c_uint8), ("bios_ticks", ctypes.c_uint32),
                ("shuffle_read", ctypes.c_void_p), ("shuffle_ctx", ctypes.c_void_p),
                ("saved_scores", ctypes.c_void_p), ("save_scores", ctypes.c_void_p),
                ("host_user", ctypes.c_void_p)]


class GameHostInput(ctypes.Structure):
    _fields_ = [("requests", ctypes.c_uint), ("focused", ctypes.c_bool)]


class DrawDropStats(ctypes.Structure):
    _fields_ = [("cells", ctypes.c_uint32), ("page", ctypes.c_int), ("cell", ctypes.c_int32)]


class Problem(Exception):
    pass


# key_script and poke_script are also the input of the warped-level DOSBox-X reference runs
# (tests/dosbox/test_reference_golden.py imports them): keep them pure, and expect any change
# to them to need those golden runs regenerated (Sentinel).

def key_script(level: int, ticks: int) -> dict[int, list[int]]:
    """Scan codes due per tick: Space to start, then walk in a direction for a while, fire
    in bursts (fire holds the facing, so the shots go the way the ship last walked)."""
    keys: dict[int, list[int]] = {START_TICK: [SPACE], START_TICK + 10: [SPACE | BREAK]}
    held: tuple[int, ...] = ()
    step = 0
    for t in range(PLAY_FROM, ticks + 1, 30):
        out = [k | BREAK for k in held]
        held = MOVES[(level * 3 + step) % len(MOVES)] if step % 4 != 3 else ()
        out += list(held)
        out.append(SPACE if step % 2 == 0 else SPACE | BREAK)
        keys.setdefault(t, []).extend(out)
        step += 1
    return keys


def poke_script(level: int, ticks: int) -> list[tuple[int, int, int]]:
    """(tick, DS offset, word), applied before the tick's keys: the warp, then lives."""
    pokes = [(WARP_TICK, LEVEL, level - WARP_LEVELS), (WARP_TICK, WAVE_RESULT, WAVE_WARP)]
    pokes += [(t, LIVES, 5) for t in range(WARP_TICK + 50 - WARP_TICK % 50, ticks + 1, 50)]
    return pokes


def write_scripts(level: int, ticks: int, keys_path: Path, pokes_path: Path) -> None:
    """The same input as PROBE.KEY / PROBE.POK (tools/harness/dosbox/refrun.py formats), for
    llamatron_replay or a DOSBox-X reference run."""
    keys = key_script(level, ticks)
    keys_path.write_bytes(b"".join(struct.pack("<IBB", t, code, 0)
                                   for t in sorted(keys) for code in keys[t]))
    pokes_path.write_bytes(b"".join(struct.pack("<IHH", t, off, value & 0xFFFF)
                                    for t, off, value in poke_script(level, ticks)))


class Game:
    def __init__(self, lib: ctypes.CDLL):
        self.lib = lib
        self.dg = DgroupMap(lib)
        self.state = self.dg.new_state()
        self.tick_fn = bind(lib, "game_tick", None, ctypes.c_void_p)
        self.frame_fn = bind(lib, "game_frame", None, ctypes.c_void_p,
                             ctypes.POINTER(GameHostInput))
        self.key = bind(lib, "input_keyboard_byte", None, ctypes.c_uint8)
        self.drops = bind(lib, "draw_drop_stats", DrawDropStats)
        self.tables = {}
        for name, (symbol, count) in HANDLER_TABLES.items():
            table = (ctypes.c_void_p * count).in_dll(lib, symbol)
            self.tables[name] = [table[i] is not None for i in range(count)]

        bind(lib, "input_init", None, ctypes.c_int, ctypes.c_bool)(0, True)
        bind(lib, "gfx_init", None)()
        if not bind(lib, "game_data_load", ctypes.c_bool)():
            raise Problem("the game data could not be loaded")
        palettes = bind(lib, "asset_palettes", ctypes.c_void_p)()
        bind(lib, "gfx_set_adapter", None, ctypes.c_int, ctypes.c_void_p)(1, palettes + 17)
        launch = GameLaunch(vga=True, joystick=False, sound=False, flash_mode=0, bios_ticks=0)
        if not bind(lib, "game_init", ctypes.c_bool, ctypes.c_void_p,
                    ctypes.POINTER(GameLaunch))(self.state, ctypes.byref(launch)):
            raise Problem("the game could not start")
        bind(lib, "draw_reset_drop_stats", None)()
        self.host = GameHostInput(0, True)

    def image(self) -> bytes:
        return self.dg.to_dgroup(self.state)

    def poke(self, words: dict[int, int]) -> None:
        img = bytearray(self.image())
        for off, value in words.items():
            struct.pack_into("<H", img, off, value & 0xFFFF)
        self.dg.from_dgroup(bytes(img), self.state)

    def frame(self) -> None:
        self.frame_fn(self.state, ctypes.byref(self.host))


def check_entities(game: Game, img: bytes) -> None:
    def ported(table: str, index: int) -> bool:
        entries = game.tables[table]
        return 0 <= index < len(entries) and entries[index]

    for slot in range(ENTITY_COUNT):
        base = ENTITY_TABLE + ENTITY_SIZE * slot
        think, hatch, x, y, anim = struct.unpack_from("<bbhhh", img, base)
        if hatch < 0:
            continue
        where = f"entity slot {slot}"
        if not ported("think", think & 0xFF):
            raise Problem(f"{where}: think {think & 0xFF:#04x} has no handler")
        if not ported("anim", anim & 0xFF):
            raise Problem(f"{where} (think {think:#04x}): anim {anim & 0xFF:#04x} has no handler")
        solid, = struct.unpack_from("<h", img, base + 0x10)
        kill_action, = struct.unpack_from("<h", img, base + 0x1E)
        hit, = struct.unpack_from("<h", img, base + 0x3E)
        if solid and not ported("hit", hit):
            raise Problem(f"{where} (think {think:#04x}): hit handler {hit} has no handler")
        if solid and hit == HIT_KILL_ACTION and not 0 <= kill_action < KILL_ACTION_COUNT:
            raise Problem(f"{where} (think {think:#04x}): kill action {kill_action} out of range")
        if abs(x) >= OFF_SCREEN or abs(y) >= OFF_SCREEN:
            raise Problem(f"{where} (think {think:#04x}): at ({x}, {y}), far off the screen")
    spawn, = struct.unpack_from("<h", img, SPAWN_TYPE)
    if spawn >= 0 and not ported("enemy init", spawn):
        raise Problem(f"spawn type {spawn} has no enemy init")


def run_level(lib: ctypes.CDLL, level: int, ticks: int) -> dict:
    """Plays one level; returns {"level", "ok", "problem", "tick", "reached"}."""
    game = Game(lib)
    keys = key_script(level, ticks)
    pokes: dict[int, dict[int, int]] = {}
    for t, off, value in poke_script(level, ticks):
        pokes.setdefault(t, {})[off] = value
    tick = frame = 0
    reached = None
    drops = 0
    try:
        while tick < ticks:
            frame += 1
            due = int(frame * FRAME / TICK)
            while tick < due and tick < ticks:
                tick += 1
                if tick in pokes:
                    game.poke(pokes[tick])
                for code in keys.get(tick, ()):
                    game.key(code)
                game.tick_fn(game.state)
                img = game.image()
                if tick > WARP_TICK and reached is None:
                    now, = struct.unpack_from("<h", img, LEVEL)
                    hook, = struct.unpack_from("<H", img, TICK_HOOK)
                    if now == level and hook == PLAY_TICK_HOOK:
                        reached = tick
                    elif tick > WARP_TICK + LEVEL_START_WITHIN:
                        raise Problem(f"level {level} never started (level {now}, "
                                      f"tick hook {hook:#06x})")
                check_entities(game, img)
            game.frame()
            stats = game.drops()
            if stats.cells != drops:
                raise Problem(f"{stats.cells - drops} draw cell(s) outside page {stats.page} "
                              f"(last at cell {stats.cell}, row {stats.cell // 40})")
    except Problem as p:
        return {"level": level, "ok": False, "problem": str(p), "tick": tick, "frame": frame,
                "reached": reached}
    return {"level": level, "ok": True, "problem": None, "tick": tick, "frame": frame,
            "reached": reached}


def run_child(level: int, ticks: int, timeout: float = 600) -> dict:
    """run_level in a process of its own: a crash or hang becomes the level's problem."""
    cmd = [sys.executable, __file__, "--one", str(level), "--ticks", str(ticks)]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=os.environ)
    except subprocess.TimeoutExpired:
        return {"level": level, "ok": False, "problem": f"hung (> {timeout:.0f} s)",
                "tick": None}
    for line in reversed(out.stdout.splitlines()):
        if line.startswith("{"):
            return json.loads(line)
    tail = (out.stderr.strip().splitlines() or ["no output"])[-1]
    how = f"signal {-out.returncode}" if out.returncode < 0 else f"exit {out.returncode}"
    return {"level": level, "ok": False, "problem": f"crashed ({how}): {tail}", "tick": None}


def run_replay(replay: Path, level: int, ticks: int, workdir: Path,
               timeout: float = 600) -> dict:
    """The same level in llamatron_replay (e.g. a sanitizer build): a crash, a sanitizer
    report or a hang is the level's problem."""
    keys, pokes = workdir / f"level{level:02d}.key", workdir / f"level{level:02d}.pok"
    write_scripts(level, ticks, keys, pokes)
    cmd = [str(replay), "--ticks", str(ticks), "--vga", "--seed", "0", "--keys", str(keys),
           "--pokes", str(pokes)]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return {"level": level, "ok": False, "problem": "replay hung", "tick": None}
    report = [line for line in out.stderr.splitlines()
              if "ERROR: AddressSanitizer" in line or "runtime error" in line]
    if out.returncode != 0 or report:
        how = f"signal {-out.returncode}" if out.returncode < 0 else f"exit {out.returncode}"
        tail = (report or out.stderr.strip().splitlines() or ["no output"])[0]
        return {"level": level, "ok": False, "problem": f"replay {how}: {tail}", "tick": None}
    return {"level": level, "ok": True, "problem": None, "tick": ticks, "reached": None}


def repro(level: int, ticks: int) -> str:
    return ("cd tests && LLAMATRON_GAME_TESTLIB=<build>/tests/libllamatron_game_testlib.dylib "
            f"../tools/harness/.venv/bin/python oracle/level_sweep.py --levels {level} "
            f"--ticks {ticks}")


def describe(r: dict) -> str:
    if r["ok"]:
        return f"level {r['level']}: ok (started at tick {r['reached']})"
    at = f" at tick {r['tick']}" if r.get("tick") is not None else ""
    return f"level {r['level']}: {r['problem']}{at}"


def parse_levels(text: str) -> list[int]:
    out: list[int] = []
    for part in text.split(","):
        lo, _, hi = part.partition("-")
        out += range(int(lo), int(hi or lo) + 1)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--levels", default=f"0-{LEVELS - 1}")
    ap.add_argument("--ticks", type=int, default=DEFAULT_TICKS)
    ap.add_argument("--one", type=int)
    ap.add_argument("--json", help="also write the results here")
    ap.add_argument("--replay", type=Path,
                    help="run llamatron_replay (e.g. an ASan build) on the same input instead")
    ap.add_argument("--workdir", type=Path, default=TESTS.parent / "build-pytest" / "level_sweep",
                    help="where --replay's PROBE.KEY / PROBE.POK files go")
    args = ap.parse_args()
    if args.replay:
        args.workdir.mkdir(parents=True, exist_ok=True)
        results = [run_replay(args.replay, lv, args.ticks, args.workdir)
                   for lv in parse_levels(args.levels)]
        for r in results:
            print(describe(r) if not r["ok"] else f"level {r['level']}: ok (replay)", flush=True)
        bad = [r for r in results if not r["ok"]]
        print(f"{len(results) - len(bad)}/{len(results)} levels ok (replay)")
        return 1 if bad else 0
    lib = load_testlib()
    if lib is None:
        print("LLAMATRON_GAME_TESTLIB is not set or not built", file=sys.stderr)
        return 2
    if args.one is not None:
        print(json.dumps(run_level(lib, args.one, args.ticks)), flush=True)
        return 0
    results = []
    for level in parse_levels(args.levels):
        r = run_child(level, args.ticks)
        results.append(r)
        print(describe(r), flush=True)
    if args.json:
        Path(args.json).write_text(json.dumps(results, indent=1))
    bad = [r for r in results if not r["ok"]]
    print(f"{len(results) - len(bad)}/{len(results)} levels ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
