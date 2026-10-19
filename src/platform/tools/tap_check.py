#!/usr/bin/env python3
"""A quick key tap (press and release inside one host frame) must reach the game like a key
held for one frame. The original's INT 09 handler runs the moment a key moves, so even a fast
tap is a make code the game sees for a frame before the break replaces it. Here the host may
report both at once (keydown + keyup in one requestAnimationFrame). Without the shell holding
the break back, the start-up scroller's Space skip (game86 1000:58a4, last scan code) lost it.

Runs the native llamatron through the title into the version scroller and taps Space there:
  * same-frame tap (--tap F:57:0) == one-frame hold (F:57:1) == a 4-frame press (F:57:4,
    shorter than the ~5 ticks from the skip to the title), page hashes at every listed tick,
    and all differ from no tap (the scroller was skipped);
  * the tap only skips the scroller: it is not still down at the game86 title, whose start
    check (1000:0136..016a) would begin a game. A press held into the title does, so the tap's
    pages must differ from that one's (Prism's portal check of 5a14bfb);
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
FRAMES = 1000
TICKS = "250,300,400,500,600,700"
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
    for hold in (1, 4):
        held = native_hashes(args.native, f"{TAP_FRAME}:57:{hold}")
        if tap != held:
            print(f"tap: a same-frame tap differs from a {hold}-frame press:\n  " + "\n  ".join(
                f"{t}  vs  {h}" for t, h in zip(tap, held) if t != h))
            return 1
    if tap == none:
        print("tap: the same-frame tap had no effect (same pages as without a key)")
        return 1
    into_title = native_hashes(args.native, f"{TAP_FRAME}:57:150")
    if tap[-1] == into_title[-1]:
        print("tap: the tap ends like a press held into the title (it started a game)")
        return 1
    print(f"tap: native same-frame tap == 1- and 4-frame presses, only skips the scroller, ticks {TICKS}")
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
