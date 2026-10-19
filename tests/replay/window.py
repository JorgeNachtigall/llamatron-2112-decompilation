"""One-tick window of a golden reference scenario against the port (dev loop, test-only).

    tools/harness/.venv/bin/python tests/replay/window.py walk_vga 600 [--count 60]
        [--driver build/tests/llamatron_replay] [--workdir DIR]

runs the scenario from tests/dosbox/test_reference_golden.py (same seed, keys and pokes)
under DOSBox-X with a DGROUP snapshot after every IRQ from FIRST (at most 60: the XMS slots),
replays the port over the same ticks (llamatron_replay --every 1 --first FIRST) and prints
the per-tick report, then every differing field at the first divergent tick with the
reference and port bytes. Snapshots stay in the workdir (gitignored build-dosbox/ by
default); never commit them.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "harness" / "dosbox"))
sys.path.insert(0, str(ROOT / "tests" / "dosbox"))
sys.path.insert(0, str(ROOT / "tests" / "replay"))

import compare  # noqa: E402
import refrun  # noqa: E402
from test_reference_golden import ORIGINAL, RUNS, probe_args  # noqa: E402

MAX_WINDOW = 60


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("scenario", choices=sorted(RUNS))
    ap.add_argument("first", type=int, help="first tick (timer IRQ) of the window")
    ap.add_argument("--count", type=int, default=MAX_WINDOW)
    ap.add_argument("--driver", default=os.environ.get("LLAMATRON_REPLAY"),
                    help="llamatron_replay (default $LLAMATRON_REPLAY)")
    ap.add_argument("--workdir", type=Path, default=None)
    args = ap.parse_args(argv)
    if not args.driver or not Path(args.driver).exists():
        ap.error("llamatron_replay not found: pass --driver or set LLAMATRON_REPLAY")
    count = max(1, min(args.count, MAX_WINDOW))
    last = args.first + count - 1

    run = RUNS[args.scenario]
    cfg = probe_args(run)
    cfg.update(interval=1, max_snapshots=count, first_snapshot=args.first,
               quit_after=max(cfg.get("quit_after", 0), last + 10))
    workdir = args.workdir or (Path(os.environ.get("LLAMATRON_DOSBOX_WORKDIR")
                                    or ROOT / "build-dosbox")
                               / f"window-{args.scenario}-{args.first}")
    result = refrun.probe(original=ORIGINAL, workdir=workdir,
                          time_limit=run.get("time_limit", 90) + last // 50, **cfg)
    reference = {s.irq: s.dgroup for s in result.snapshots}
    if not reference:
        print("the reference run took no snapshots", file=sys.stderr)
        return 1

    drive = workdir / "c"
    script = ["--keys", str(drive / "PROBE.KEY")]
    if (drive / "PROBE.POK").exists():
        script += ["--pokes", str(drive / "PROBE.POK")]
    out = workdir / "port.lrpl"
    machine = "--vga" if cfg["machine"] == "vga" else "--ega"
    proc = subprocess.run([args.driver, "--ticks", str(last), "--every", "1",
                           "--first", str(args.first), machine, "--seed", str(cfg["ticks"]),
                           *script, "--out", str(out)], capture_output=True, text=True)
    if proc.returncode != 0:
        print(proc.stderr, file=sys.stderr)
        return 1
    port = compare.read_port(out)
    diffs = compare.compare(port, reference)
    print(f"{args.scenario} ticks {args.first}..{last}: {run['summary']}")
    print(compare.report(diffs))
    first = next((d for d in diffs if d.offsets), None)
    if first:
        a, b = reference[first.tick], port.snapshots[first.tick]
        print(f"\ntick {first.tick}, reference -> port:")
        for off in first.offsets:
            print(f"  DS:{off:04X} {compare.describe(off):32s} {a[off]:02X} -> {b[off]:02X}")
    print(f"(snapshots in {workdir})")
    return 1 if first else 0


if __name__ == "__main__":
    sys.exit(main())
