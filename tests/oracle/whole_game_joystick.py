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
Space; the title is started with the keyboard's Space as in the golden runs.

Usage (from tests/):
  LLAMATRON_GAME_TESTLIB=<build>/tests/libllamatron_game_testlib.dylib \\
      ../tools/harness/.venv/bin/python oracle/whole_game_joystick.py [--levels 0,10,25]
      [--ticks N] [-j 6] [--json FILE] [--dump DIR]
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

from level_sweep import (BREAK, DOWN, LEFT, ONE_PLAYER, RIGHT, SPACE, UP,  # noqa: E402
                         StartPlan, lockstep_script, parse_levels)

STICKS = 2
CENTRE_COUNT = 100                  # INPUT_JOYSTICK_CENTRE_COUNT
SAMPLE_LEVELS = "0,1,5,10,25,50,75,99"
DEFAULT_TICKS = 1500

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
    """whole_game.Script(keys, pokes, sticks) parts: level_sweep.lockstep_script with its play
    keys turned into stick 0 positions and button A (the start keys stay keys)."""
    keys, pokes = lockstep_script(level, ticks, start)
    start_keys: dict[int, list[int]] = {}
    for t, code in start.keys:
        start_keys.setdefault(t, []).append(code)
    sticks: dict[int, list[tuple[int, float, float, bool, bool]]] = {}
    fire = False
    for t in sorted(keys):
        if t < start.play_from:
            continue
        x = y = 0
        for code in keys[t]:
            if code == SPACE:
                fire = True
            elif code == SPACE | BREAK:
                fire = False
            elif code in DIRECTION:
                dx, dy = DIRECTION[code]
                x, y = x + dx, y + dy
        sticks[t] = [(stick, float(x), float(y), fire, False)]
    return start_keys, pokes, sticks


def run_one(level: int, ticks: int, dump: Path | None, machine: str = "vga") -> dict:
    import whole_game as W
    from adapters.cport import load_testlib
    from whole_game_pages import PageComparer
    lib = load_testlib()
    keys, pokes, sticks = stick_script(level, ticks)
    pages = PageComparer(lib, dump_dir=dump / f"joystick_level{level:02d}" if dump else None)
    res = W.run_lockstep(lib, W.Script(keys, pokes, sticks), ticks, W.SWEEP_SEED, machine,
                         on_frame=pages.hook, joystick=JoystickPort())
    out = {"config": "joystick", "level": level, "ok": res.ok, "tick": res.tick,
           "frame": res.frame, "where": res.where,
           "report": res.detail if res.where == "pages" else W.describe(res)}
    if res.offsets:
        out["fields"] = sorted({W.field_of(o) for o in res.offsets[:64]})
        out["report"] += " -- " + ", ".join(out["fields"][:6])
    return out


def run_child(level: int, ticks: int, dump: Path | None, timeout: float = 7200) -> dict:
    cmd = [sys.executable, __file__, "--one", str(level), "--ticks", str(ticks)]
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
    ap.add_argument("--ticks", type=int, default=DEFAULT_TICKS)
    ap.add_argument("-j", type=int, default=1)
    ap.add_argument("--one", type=int, help="run this level here and print its result as JSON")
    ap.add_argument("--json", type=Path, help="also write the results here")
    ap.add_argument("--dump", type=Path, help="PNGs of the first page difference per level")
    args = ap.parse_args()
    if args.one is not None:
        print(json.dumps(run_one(args.one, args.ticks, args.dump)), flush=True)
        return 0
    results = []
    with ThreadPoolExecutor(max_workers=max(1, args.j)) as pool:
        for r in pool.map(lambda lv: run_child(lv, args.ticks, args.dump),
                          parse_levels(args.levels)):
            results.append(r)
            print(f"joystick level {r['level']}: {r['report']}", flush=True)
    if args.json:
        args.json.write_text(json.dumps(results, indent=1))
    bad = [r for r in results if not r["ok"]]
    print(f"joystick: {len(results) - len(bad)}/{len(results)} levels match")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
