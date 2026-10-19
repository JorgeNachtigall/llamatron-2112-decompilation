#!/usr/bin/env python3
"""Native / web parity: replay one reference scenario (tests/golden/reference_runs.json: its
keys and BIOS tick seed) in the native llamatron and in the web build, and check that the web
build prints the same page hashes ("pages tick=... fnv=... shown=...") at the same ticks.

  web_parity.py --native BIN --web-dir DIR --golden JSON [--run walk_vga] [--every 50]

The web run goes through web_smoke.py --report (headless Chrome, output posted back by the
page). Exit 77 (skipped) when the native binary or Chrome is missing.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

FRAMES_PER_TICK = 70.086 / 50.159
SKIP = 77


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--native", required=True)
    ap.add_argument("--web-dir", required=True)
    ap.add_argument("--golden", required=True)
    ap.add_argument("--run", default="walk_vga")
    ap.add_argument("--every", type=int, default=50)
    args = ap.parse_args()
    if not Path(args.native).exists():
        print(f"parity: skipped (no native build at {args.native})")
        return SKIP

    cfg = json.loads(Path(args.golden).read_text())[args.run]["config"]
    last = cfg.get("quit_after", 600)
    ticks = ",".join(str(t) for t in range(args.every, last + 1, args.every))
    keys = ",".join(f"{t}:{sc}" for t, sc in cfg["keys"])
    frames = int(last * FRAMES_PER_TICK) + 8
    video = "vga" if cfg.get("machine", "vga") == "vga" else "ega"
    flash = cfg.get("flash", "N")
    phase, delay = cfg.get("tick_phase", 0), cfg.get("start_delay", 0)

    with tempfile.TemporaryDirectory() as prefs:
        env = dict(os.environ, LLAMATRON_PREF_DIR=prefs)
        native = subprocess.run(
            [args.native, f"--{video}", "--keyboard", "--mute", "--flash", flash, "--seed", str(cfg["ticks"]),
             "--keys", keys, "--page-hashes", ticks, "--frames", str(frames),
             "--tick-phase", str(phase), "--start-delay", str(delay)],
            capture_output=True, text=True, timeout=600, env=env)
    if native.returncode != 0:
        print(f"parity: native run failed\n{native.stderr}")
        return 1
    lines = [l for l in native.stdout.splitlines() if l.startswith("pages tick=")]
    if not lines:
        print("parity: the native run printed no page hashes")
        return 1
    print(f"parity: native gave {len(lines)} page hashes, ticks {args.every}..{last}")

    query = (f"frames={frames}&{video}&keyboard&mute&flash={flash}&seed={cfg['ticks']}"
             f"&keys={keys}&hashes={ticks}&phase={phase}&delay={delay}")
    smoke = Path(__file__).with_name("web_smoke.py")
    cmd = [sys.executable, str(smoke), "--dir", args.web_dir, "--page", "llamatron.html", "--report",
           "--timeout-s", "120", "--query", query]
    for line in lines:
        cmd += ["--expect", line]
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
