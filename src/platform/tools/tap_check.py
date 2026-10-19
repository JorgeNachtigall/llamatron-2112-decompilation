#!/usr/bin/env python3
"""A quick key tap (press and release inside one host frame) must reach the game like a key
held for one frame. The original's INT 09 handler runs the moment a key moves, so even a fast
tap is a make code the game sees for a frame before the break replaces it. Here the host may
report both at once (keydown + keyup in one requestAnimationFrame). Without the shell holding
the break back, the start-up scroller's Space skip (game86 1000:58a4, last scan code) lost it.

Runs the native llamatron through the title into the version scroller and taps Space there:
  * same-frame tap (--tap F:57:0) == one-frame hold (--tap F:57:1), page hashes at every
    listed tick, and both differ from no tap (the scroller was skipped);
  * with --web-dir, the web build with real browser keydown + keyup events dispatched back to
    back (?taps=F:57:0, handed over by SDL at the next frame's poll) prints the same hashes as
    the native tap one frame later.

  tap_check.py --native BIN [--web-dir DIR]

Exit 77 (skipped) when the native binary or, for the web part, Chrome is missing.
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77
TAP_FRAME = 240                       # inside the version scroller (game tick ~169)
FRAMES = 600
TICKS = "250,300,350,400"
BASE = ["--vga", "--keyboard", "--mute", "--flash", "N", "--seed", "0", "--frames", str(FRAMES),
        "--start-after", "3", "--page-hashes", TICKS]


def native_hashes(binary: str, tap: str | None) -> list[str]:
    with tempfile.TemporaryDirectory() as prefs:
        env = dict(os.environ, LLAMATRON_PREF_DIR=prefs)
        cmd = [binary, *BASE] + (["--tap", tap] if tap else [])
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=300, env=env)
    if out.returncode != 0:
        raise SystemExit(f"tap: llamatron failed ({out.returncode})\n{out.stderr}")
    lines = [l for l in out.stdout.splitlines() if l.startswith("pages tick=")]
    if len(lines) != len(TICKS.split(",")):
        raise SystemExit(f"tap: expected {TICKS} page hashes, got {lines}")
    return lines


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--native", required=True)
    ap.add_argument("--web-dir")
    args = ap.parse_args()
    if not Path(args.native).exists():
        print(f"tap: skipped (no native build at {args.native})")
        return SKIP

    none = native_hashes(args.native, None)
    tap = native_hashes(args.native, f"{TAP_FRAME}:57:0")
    held = native_hashes(args.native, f"{TAP_FRAME}:57:1")
    if tap != held:
        print("tap: a same-frame tap differs from a one-frame hold:\n  " + "\n  ".join(
            f"{t}  vs  {h}" for t, h in zip(tap, held) if t != h))
        return 1
    if tap == none:
        print("tap: the same-frame tap had no effect (same pages as without a key)")
        return 1
    print(f"tap: native same-frame tap == one-frame hold, ticks {TICKS}")
    if not args.web_dir:
        return 0

    # Web: the events dispatched in frame F reach on_key at frame F + 1's poll.
    web_ref = native_hashes(args.native, f"{TAP_FRAME + 1}:57:0")
    query = f"frames={FRAMES}&vga&keyboard&mute&flash=N&seed=0&start=3&hashes={TICKS}&taps={TAP_FRAME}:57:0"
    cmd = [sys.executable, str(Path(__file__).with_name("web_smoke.py")), "--dir", args.web_dir,
           "--page", "llamatron.html", "--report", "--timeout-s", "120", "--query", query]
    for line in web_ref:
        cmd += ["--expect", line]
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
