"""Whole-game lockstep check: the original game86 against the port, frame by frame
(tests/oracle/whole_game.py), as a regression guard.

  --sample   10 levels spread over 0..99 (1500 ticks each, pages compared too) plus the
             two-player and EGA warps (reference runs level_40_two_player, level_25_ega);
             ctest oracle_long.lockstep_sample runs this.
  --all      every level 0..99 x 3000 ticks with pages: the release check (tests/README.md).

It first configures and builds its own testlib and game86 image from the current tree in
--build-dir (targets llamatron_generated, llamatron_game_testlib), so a library left over
from an older build can never produce the result. Each level or run is a child process of
its own (the port keeps static state), -j of them at a time. Exit 0 when everything
matches, 1 when something diverges (or the build fails), 77 (ctest: skipped) without the
original files or cmake.

Usage (from tests/):
  ../tools/harness/.venv/bin/python oracle/lockstep_check.py --sample -j 6
  ../tools/harness/.venv/bin/python oracle/lockstep_check.py --all -j 6 --json all.json
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

TESTS = Path(__file__).resolve().parents[1]
REPO = TESTS.parent
WHOLE_GAME = TESTS / "oracle" / "whole_game.py"

SAMPLE_LEVELS = (0, 11, 22, 33, 44, 55, 66, 77, 88, 99)
SAMPLE_TICKS = 1500
SAMPLE_RUNS = ("level_40_two_player", "level_25_ega")
ALL_TICKS = 3000
SKIPPED = 77


def build(build_dir: Path, original_dir: Path) -> tuple[Path, Path]:
    """Configure (first time) and build the testlib and game86 image from this tree."""
    if not (build_dir / "CMakeCache.txt").exists():
        cmd = ["cmake", "-S", str(REPO), "-B", str(build_dir),
               f"-DLLAMATRON_ORIGINAL_DIR={original_dir}",
               f"-DLLAMATRON_HARNESS_PYTHON={sys.executable}"]
        if shutil.which("ninja"):
            cmd += ["-G", "Ninja"]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["cmake", "--build", str(build_dir), "--target", "llamatron_generated",
                    "llamatron_game_testlib"], check=True, stdout=subprocess.DEVNULL)
    image = build_dir / "generated" / "exe" / "game86.unpacked"
    libs = sorted((build_dir / "tests").glob("libllamatron_game_testlib.*"))
    if not image.exists() or not libs:
        raise SystemExit(f"build in {build_dir} made no game86 image or testlib")
    return image, libs[0]


def tree_state() -> str:
    head = subprocess.run(["git", "-C", str(REPO), "rev-parse", "--short", "HEAD"],
                          capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "-C", str(REPO), "status", "--porcelain", "--untracked-files=no"],
                           capture_output=True, text=True).stdout.strip()
    return f"{head}{' + uncommitted changes' if dirty else ''}"


def run_one(name: str, cmd: list[str], env: dict, timeout: float) -> dict:
    """One level (sweep --one, a JSON line) or one reference run ('<run>: <report>')."""
    started = time.monotonic()
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=env,
                             cwd=TESTS)
    except subprocess.TimeoutExpired:
        return {"name": name, "ok": False, "report": f"hung (> {timeout:.0f} s)"}
    seconds = round(time.monotonic() - started)
    lines = out.stdout.strip().splitlines()
    for line in reversed(lines):
        if line.startswith("{"):
            r = json.loads(line)
            return {"name": name, "ok": r["ok"], "report": r["report"], "seconds": seconds}
        if line.startswith(f"{name}: "):
            return {"name": name, "ok": out.returncode == 0, "report": line[len(name) + 2:],
                    "seconds": seconds}
    tail = (out.stderr.strip().splitlines() or ["no output"])[-1]
    return {"name": name, "ok": False, "report": f"crashed (exit {out.returncode}): {tail}"}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    which = ap.add_mutually_exclusive_group(required=True)
    which.add_argument("--sample", action="store_true")
    which.add_argument("--all", action="store_true")
    ap.add_argument("-j", "--jobs", type=int, default=max(1, min(6, (os.cpu_count() or 2) - 2)))
    ap.add_argument("--ticks", type=int, help="override the ticks per level / run")
    ap.add_argument("--build-dir", type=Path, default=REPO / "build-lockstep",
                    help="where the check builds its own testlib and image")
    ap.add_argument("--json", type=Path, help="also write the results here")
    args = ap.parse_args()

    original_dir = Path(os.environ.get("LLAMATRON_ORIGINAL_DIR", REPO / "original")).resolve()
    if not (original_dir / "llama.exe").exists():
        print(f"no original files in {original_dir}: skipped")
        return SKIPPED
    if not shutil.which("cmake"):
        print("no cmake to build the testlib with: skipped")
        return SKIPPED
    image, lib = build(args.build_dir.resolve(), original_dir)
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1", LLAMATRON_ORIGINAL_DIR=str(original_dir),
               LLAMATRON_GAME86_IMAGE=str(image), LLAMATRON_GAME_TESTLIB=str(lib))

    ticks = args.ticks or (SAMPLE_TICKS if args.sample else ALL_TICKS)
    levels = SAMPLE_LEVELS if args.sample else range(100)
    py = [sys.executable, str(WHOLE_GAME)]
    jobs = [(f"level {lv}", py + ["sweep", "--one", str(lv), "--ticks", str(ticks), "--pages"])
            for lv in levels]
    if args.sample:
        jobs += [(run, py + [run, "--ticks", str(ticks)]) for run in SAMPLE_RUNS]
    print(f"lockstep {'sample' if args.sample else 'all levels'}: {len(jobs)} runs x {ticks} "
          f"ticks, -j {args.jobs}, tree {tree_state()}, testlib {lib}", flush=True)

    results = []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        runs = {pool.submit(run_one, name, cmd, env, timeout=4 * ticks): name
                for name, cmd in jobs}
        for run in as_completed(runs):
            r = run.result()
            results.append(r)
            print(f"  {r['name']}: {r['report']}", flush=True)
    order = {name: i for i, (name, _) in enumerate(jobs)}
    results.sort(key=lambda r: order[r["name"]])
    if args.json:
        args.json.write_text(json.dumps(results, indent=1))
    bad = [r for r in results if not r["ok"]]
    for r in bad:
        print(f"DIVERGES {r['name']}: {r['report']}")
    print(f"{len(results) - len(bad)}/{len(results)} match")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
