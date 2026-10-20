"""Whole-game lockstep with the joystick (tests/oracle/whole_game.py run_lockstep(joystick=)):
the original reads a model of the game port, the port its sticks through input_set_stick,
and the same stick script drives both.

Port 201h as the original sees it (game86 1e7b:0046, 1000:3e27; tests/oracle/
test_engine_input.py GamePort): an OUT arms the four axis one-shots; each IN then reads
bit i = 1 while one-shot i has reads left (and uses one up), and bits 4..7 = the buttons,
0 = pressed. A stick's axis count is centre * (1 + axis), the port's axis_to_count
(src/engine/input.c), so both sides measure the same position.

The script is level_sweep's (the warp, the lives, a walk in a new direction every 30 ticks
with fire in bursts) with the stick in place of the arrow keys and button A in place of
Space, the title's keys included (with the joystick selected the title menu reads the
stick, not the keyboard). With --two-controllers the title menu is stepped to a controller
each (OPT_DOUBLE_CONTROLLER) and stick 1 walks P2's ship on a pattern of its own. Each run
also checks that the original really plays a game once the script's play starts: no demo
waves left, and with --two-controllers the flag in its options.

Usage (from tests/):
  LLAMATRON_GAME_TESTLIB=<build>/tests/libllamatron_game_testlib.dylib \\
      ../tools/harness/.venv/bin/python oracle/whole_game_joystick.py [--levels 0,10,25]
      [--ticks N] [-j 6] [--two-controllers] [--json FILE] [--dump DIR]
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))
sys.path.insert(0, str(TESTS / "oracle"))

from level_sweep import (BREAK, DOWN, LEFT, LIVES, ONE_PLAYER,  # noqa: E402
                         OTHER_PLAYER_LIVES, RIGHT, SPACE, UP, StartPlan, lockstep_script,
                         parse_levels)

STICKS = 2
CENTRE_COUNT = 100                  # INPUT_JOYSTICK_CENTRE_COUNT
SAMPLE_LEVELS = "0,1,5,10,25,50,75,99"
DEFAULT_TICKS = 1500
TWO_CONTROLLER_TICKS = 2600
OPTIONS = 0x2CF6                    # DGROUP byte (tests/adapters/dgroup_map.c DS_OPTIONS)
DEMO_MODE = 0x06C4                  # DGROUP word: demo waves left (DS_DEMO_MODE)
OPT_DOUBLE_CONTROLLER = 0x02        # src/game/state.h
P2_PATTERN_SHIFT = 4                # P2 walks level_sweep's pattern of level + 4

# The title menu's Right twice: two players -> a controller each (level_sweep.TWO_PLAYERS'
# timing), then fire; both players' lives are topped up.
TWO_CONTROLLERS = StartPlan(((1150, RIGHT), (1156, RIGHT | BREAK), (1170, RIGHT),
                             (1176, RIGHT | BREAK), (1200, SPACE), (1210, SPACE | BREAK)),
                            1350, 1370, (LIVES, OTHER_PLAYER_LIVES))

# Scan code -> (x, y) of the stick for it.
DIRECTION = {UP: (0, -1), DOWN: (0, 1), LEFT: (-1, 0), RIGHT: (1, 0)}


def axis_count(axis: float) -> int:
    return int(CENTRE_COUNT * (1.0 + max(-1.0, min(1.0, axis))) + 0.5)


class JoystickPort:
    """The game port model: set() from the script, arm() on OUT 201h, read() on IN 201h."""

    def __init__(self):
        self.counts = [axis_count(0)] * (2 * STICKS)    # x0, y0, x1, y1
        self.buttons = [False] * (2 * STICKS)           # 1A, 1B, 2A, 2B
        self.left = [0] * (2 * STICKS)

    def set(self, stick: int, x: float, y: float, button_a: bool, button_b: bool) -> None:
        self.counts[2 * stick:2 * stick + 2] = [axis_count(x), axis_count(y)]
        self.buttons[2 * stick:2 * stick + 2] = [button_a, button_b]

    def arm(self) -> None:
        self.left = list(self.counts)

    def read(self) -> int:
        value = 0
        for i in range(2 * STICKS):
            if not self.buttons[i]:
                value |= 0x10 << i
            if self.left[i] > 0:
                value |= 1 << i
                self.left[i] -= 1
        return value


def stick_script(level: int, ticks: int, start: StartPlan = ONE_PLAYER, stick: int = 0):
    """whole_game.Script(keys, pokes, sticks) parts: level_sweep.lockstep_script with all its
    keys turned into the stick's position (the arrows held) and button A (Space held)."""
    keys, pokes = lockstep_script(level, ticks, start)
    sticks: dict[int, list[tuple[int, float, float, bool, bool]]] = {}
    held: set[int] = set()
    for t in sorted(keys):
        for code in keys[t]:
            if code & BREAK:
                held.discard(code & ~BREAK)
            else:
                held.add(code)
        x = sum(DIRECTION[c][0] for c in held if c in DIRECTION)
        y = sum(DIRECTION[c][1] for c in held if c in DIRECTION)
        sticks[t] = [(stick, float(x), float(y), SPACE in held, False)]
    return {}, pokes, sticks


def two_controller_script(level: int, ticks: int):
    """stick_script for TWO_CONTROLLERS, with stick 1 walking its own pattern."""
    keys, pokes, sticks = stick_script(level, ticks, TWO_CONTROLLERS)
    _, _, p2 = stick_script(level + P2_PATTERN_SHIFT, ticks, TWO_CONTROLLERS, stick=1)
    for t, moves in p2.items():
        sticks.setdefault(t, []).extend(moves)
    return keys, pokes, sticks


def run_one(level: int, ticks: int, dump: Path | None, two_controllers: bool = False,
            machine: str = "vga") -> dict:
    import whole_game as W
    from adapters.cport import load_testlib
    from whole_game_pages import PageComparer
    lib = load_testlib()
    config = "joystick_two_controllers" if two_controllers else "joystick"
    start = TWO_CONTROLLERS if two_controllers else ONE_PLAYER
    keys, pokes, sticks = (two_controller_script if two_controllers else stick_script)(
        level, ticks)
    pages = PageComparer(lib, dump_dir=dump / f"{config}_level{level:02d}" if dump else None)
    at_play = {}

    def on_frame(orig, port, tick, frame):
        if tick >= start.play_from and not at_play:
            dg = orig.dgroup()
            at_play.update(options=dg[OPTIONS],
                           demo=int.from_bytes(dg[DEMO_MODE:DEMO_MODE + 2], "little", signed=True))
        return pages.hook(orig, port, tick, frame)
    res = W.run_lockstep(lib, W.Script(keys, pokes, sticks), ticks, W.SWEEP_SEED, machine,
                         on_frame=on_frame, joystick=JoystickPort())
    out = {"config": config, "level": level, "ok": res.ok, "tick": res.tick,
           "frame": res.frame, "where": res.where,
           "report": res.detail if res.where == "pages" else W.describe(res)}
    out.update(at_play)
    if res.ok and (at_play.get("demo", 1) != 0
                   or two_controllers and not at_play["options"] & OPT_DOUBLE_CONTROLLER):
        out["ok"] = False
        out["report"] = f"matched, but the original does not play the scripted game at tick " \
                        f"{start.play_from}: {at_play}"
    if res.offsets:
        out["fields"] = sorted({W.field_of(o) for o in res.offsets[:64]})
        out["report"] += " -- " + ", ".join(out["fields"][:6])
    return out


def run_child(level: int, ticks: int, dump: Path | None, two_controllers: bool = False,
              timeout: float = 7200) -> dict:
    cmd = [sys.executable, __file__, "--one", str(level), "--ticks", str(ticks)]
    if two_controllers:
        cmd.append("--two-controllers")
    if dump:
        cmd += ["--dump", str(dump)]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=os.environ)
    except subprocess.TimeoutExpired:
        return {"level": level, "ok": False, "report": f"hung (> {timeout:.0f} s)"}
    for line in reversed(out.stdout.splitlines()):
        if line.startswith("{"):
            return json.loads(line)
    tail = (out.stderr.strip().splitlines() or ["no output"])[-1]
    return {"level": level, "ok": False, "report": f"crashed (exit {out.returncode}): {tail}"}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--levels", default=SAMPLE_LEVELS)
    ap.add_argument("--ticks", type=int)
    ap.add_argument("--two-controllers", action="store_true",
                    help="the title's double controller: a stick per player")
    ap.add_argument("-j", type=int, default=1)
    ap.add_argument("--one", type=int, help="run this level here and print its result as JSON")
    ap.add_argument("--json", type=Path, help="also write the results here")
    ap.add_argument("--dump", type=Path, help="PNGs of the first page difference per level")
    args = ap.parse_args()
    ticks = args.ticks or (TWO_CONTROLLER_TICKS if args.two_controllers else DEFAULT_TICKS)
    if args.one is not None:
        print(json.dumps(run_one(args.one, ticks, args.dump, args.two_controllers)), flush=True)
        return 0
    results = []
    with ThreadPoolExecutor(max_workers=max(1, args.j)) as pool:
        for r in pool.map(lambda lv: run_child(lv, ticks, args.dump, args.two_controllers),
                          parse_levels(args.levels)):
            results.append(r)
            print(f"{r.get('config', 'joystick')} level {r['level']}: {r['report']}", flush=True)
    if args.json:
        args.json.write_text(json.dumps(results, indent=1))
    bad = [r for r in results if not r["ok"]]
    print(f"{'joystick_two_controllers' if args.two_controllers else 'joystick'}: "
          f"{len(results) - len(bad)}/{len(results)} levels match")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
