"""The whole-game lockstep sweep (tests/oracle/whole_game.py, original against port, with the
pages side of whole_game_pages.py) in the configurations its own 'sweep' does not cover:

  vga          VGA, one player (what 'whole_game.py sweep' runs)
  ega          EGA (command tail video E): the EGA palette path, pages and pulse
  two_players  the title menu's two players with one controller: the droid flies P2's ship
               (both ships, P2's trail ring)
  two_players_shared   the same with shared start (OPT_SHARED_START)
  ega_two_players

Each level is warped to and played as in tests/oracle/level_sweep.py (its StartPlan picks
the start), in a child process of its own, -j at a time.

Usage (from tests/):
  LLAMATRON_GAME_TESTLIB=<build>/tests/libllamatron_game_testlib.dylib \\
      ../tools/harness/.venv/bin/python oracle/lockstep_configs.py ega [--levels 0-99]
      [--ticks N] [-j 8] [--json FILE] [--dump DIR]
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

from level_sweep import (ONE_PLAYER, TWO_PLAYERS, TWO_PLAYERS_SHARED, lockstep_script,  # noqa: E402
                         parse_levels)

CONFIGS = {     # name: (machine, start plan, default ticks)
    "vga": ("vga", ONE_PLAYER, 1500),
    "ega": ("ega", ONE_PLAYER, 1500),
    "two_players": ("vga", TWO_PLAYERS, 2600),
    "two_players_shared": ("vga", TWO_PLAYERS_SHARED, 2600),
    "ega_two_players": ("ega", TWO_PLAYERS, 2600),
}


def run_one(config: str, level: int, ticks: int, dump: Path | None) -> dict:
    import whole_game as W
    from adapters.cport import load_testlib
    from whole_game_pages import PageComparer
    machine, start, _ = CONFIGS[config]
    lib = load_testlib()
    keys, pokes = lockstep_script(level, ticks, start)
    pages = PageComparer(lib, dump_dir=dump / f"{config}_level{level:02d}" if dump else None)
    res = W.run_lockstep(lib, W.Script(keys, pokes), ticks, W.SWEEP_SEED, machine,
                         on_frame=pages.hook)
    out = {"config": config, "level": level, "ok": res.ok, "tick": res.tick,
           "frame": res.frame, "where": res.where,
           "report": res.detail if res.where == "pages" else W.describe(res)}
    if res.offsets:
        out["fields"] = sorted({W.field_of(o) for o in res.offsets[:64]})
        out["report"] += " -- " + ", ".join(out["fields"][:6])
    return out


def run_child(config: str, level: int, ticks: int, dump: Path | None,
              timeout: float = 7200) -> dict:
    cmd = [sys.executable, __file__, config, "--one", str(level), "--ticks", str(ticks)]
    if dump:
        cmd += ["--dump", str(dump)]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=os.environ)
    except subprocess.TimeoutExpired:
        return {"config": config, "level": level, "ok": False, "report": f"hung (> {timeout:.0f} s)"}
    for line in reversed(out.stdout.splitlines()):
        if line.startswith("{"):
            return json.loads(line)
    tail = (out.stderr.strip().splitlines() or ["no output"])[-1]
    return {"config": config, "level": level, "ok": False,
            "report": f"crashed (exit {out.returncode}): {tail}"}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("config", choices=sorted(CONFIGS))
    ap.add_argument("--levels", default="0-99")
    ap.add_argument("--ticks", type=int)
    ap.add_argument("-j", type=int, default=1)
    ap.add_argument("--one", type=int, help="run this level here and print its result as JSON")
    ap.add_argument("--json", type=Path, help="also write the results here")
    ap.add_argument("--dump", type=Path, help="PNGs of the first page difference per level")
    args = ap.parse_args()
    ticks = args.ticks or CONFIGS[args.config][2]
    if args.one is not None:
        print(json.dumps(run_one(args.config, args.one, ticks, args.dump)), flush=True)
        return 0
    results = []
    with ThreadPoolExecutor(max_workers=max(1, args.j)) as pool:
        for r in pool.map(lambda lv: run_child(args.config, lv, ticks, args.dump),
                          parse_levels(args.levels)):
            results.append(r)
            print(f"{args.config} level {r['level']}: {r['report']}", flush=True)
    if args.json:
        args.json.write_text(json.dumps(results, indent=1))
    bad = [r for r in results if not r["ok"]]
    print(f"{args.config}: {len(results) - len(bad)}/{len(results)} levels match")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
