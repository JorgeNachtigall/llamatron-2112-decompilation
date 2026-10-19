#!/usr/bin/env python3
"""Visual check of the port against the DOSBox-X reference runs (tests/golden/reference_runs.json).

For one VGA reference scenario: write its input script (PROBE.KEY, the config's keys), replay
it in the llamatron executable with the same BIOS tick seed and llama.inf options, let the
port dump its pages right before every tick at which the reference captured VRAM
(`vram_irq`, inside game86's vsync wait), and compare sha256 hashes of the 320x200 indexed
pages with the reference's `pages`:
  * the shown page (the reference's `shown`, read from the CRTC start at capture time),
    which must also be the page the port shows;
  * the background page (4000h).
The page not shown is skipped: frame_run (game86 1e7b:013f) erases it only after the vsync
wait, the port at the end of its frame. Only hashes are compared; the dumps stay in --workdir.

Prints one line per capture and the first differing tick. Exit status 1 on a difference only
with --strict or LLAMATRON_VISUAL_STRICT=1 (the game is still being brought up), 2 on errors, 0 otherwise; prints
"visual: skipped" when the scenario has no page hashes.

  visual_check.py --llamatron BIN --golden JSON --run walk_vga --workdir DIR [--strict]
"""

import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
from pathlib import Path

PAGE_BYTES = 320 * 200
PAGE_OFFSETS = ["0000", "2000", "4000"]          # port page ids 0, 1, background
FRAMES_PER_TICK = 70.086 / 50.159                 # VGA retraces per timer tick


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--llamatron", required=True)
    ap.add_argument("--golden", required=True)
    ap.add_argument("--run", default="walk_vga")
    ap.add_argument("--workdir", required=True)
    ap.add_argument("--strict", action="store_true")
    args = ap.parse_args()

    ref = json.loads(Path(args.golden).read_text()).get(args.run)
    if ref is None:
        print(f"visual: no reference run {args.run}")
        return 2
    cfg = ref["config"]
    captures = sorted(((s["vram_irq"], s) for s in ref["snapshots"].values()
                       if "pages" in s and "vram_irq" in s), key=lambda c: c[0])
    if cfg.get("machine") != "vga" or not captures:
        print(f"visual: skipped ({args.run} has no page hashes)")
        return 0

    work = Path(args.workdir) / args.run
    work.mkdir(parents=True, exist_ok=True)
    for old in work.glob("pages-*.bin"):
        old.unlink()
    keys = work / "PROBE.KEY"
    keys.write_bytes(b"".join(struct.pack("<IBB", t, sc, 0) for t, sc in cfg["keys"]))
    last = captures[-1][0]
    cmd = [args.llamatron, "--replay", str(keys), "--seed", str(cfg["ticks"]), "--vga", "--keyboard",
           "--flash", cfg.get("flash", "N"), "--mute",
           "--frames", str(int(last * FRAMES_PER_TICK) + 8),
           "--dump-pages", str(work), "--dump-ticks", ",".join(str(t) for t, _ in captures)]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    if proc.returncode != 0:
        print(f"visual: llamatron failed ({proc.returncode})\n{proc.stderr}")
        return 2

    first = None
    compared = 0
    for tick, snap in captures:
        shown = snap.get("shown")
        if shown is not None:
            shown = f"{shown:04X}" if isinstance(shown, int) else shown.upper()
            if shown not in PAGE_OFFSETS[:2]:
                # The CRTC start is inside the 64 KB canvas of the text scroller (game86 1000:5844,
                # 40 bytes a frame): no page is on screen, the port's scroller keeps its own canvas.
                print(f"tick {tick}: scroller window at {shown}h, not compared")
                continue
        path = work / f"pages-{tick}.bin"
        if not path.exists():
            print(f"tick {tick}: no dump (the port did not reach this tick)")
            first = first or tick
            continue
        data = path.read_bytes()
        pages = [hashlib.sha256(data[i * PAGE_BYTES:(i + 1) * PAGE_BYTES]).hexdigest() for i in range(3)]
        port_shown = PAGE_OFFSETS[data[3 * PAGE_BYTES]]
        want = snap["pages"]
        shown = shown or port_shown
        compared += 1
        problems = []
        if port_shown != shown:
            problems.append(f"port shows page {port_shown}, reference {shown}")
        if pages[PAGE_OFFSETS.index(shown)] != want[shown]:
            problems.append(f"shown page {shown} differs")
        if pages[2] != want["4000"]:
            problems.append("background 4000 differs")
        print(f"tick {tick}: " + ("same" if not problems else "; ".join(problems)))
        if problems and first is None:
            first = tick
    if first is None:
        print(f"visual: {args.run}: all {compared} page captures match "
              f"({len(captures) - compared} scroller windows not compared)")
        return 0
    print(f"visual: {args.run}: first difference at tick {first} (dumps in {work})")
    return 1 if args.strict or os.environ.get("LLAMATRON_VISUAL_STRICT") == "1" else 0


if __name__ == "__main__":
    sys.exit(main())
