"""Deterministic whole-game reference runs (PLAN §3.3): pinned RNG tick + scripted
input applied at exact timer IRQs (PROBE.KEY) + periodic DGROUP/VRAM snapshots.

The same script must give identical snapshots every time. That is what makes the
runs usable as references for the port's replay of the same input.
"""

import os
import shutil
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "harness" / "dosbox"))

ORIGINAL = Path(os.environ.get("LLAMATRON_ORIGINAL_DIR") or ROOT / "original")
WORKDIR = Path(os.environ.get("LLAMATRON_DOSBOX_WORKDIR") or ROOT / "build-dosbox")

pytestmark = pytest.mark.skipif(
    not (shutil.which("dosbox-x") and shutil.which("nasm") and (ORIGINAL / "llama.exe").exists()),
    reason="needs dosbox-x, nasm and the original files")

SPACE, SPACE_UP, RIGHT, RIGHT_UP, UP, UP_UP = 0x39, 0xB9, 0x4D, 0xCD, 0x48, 0xC8
# Fire on the title starts a game, then P1 walks right and up.
SCRIPT = [(150, SPACE), (160, SPACE_UP), (400, RIGHT), (480, RIGHT_UP), (500, UP), (560, UP_UP)]
G_TICK_HOOK, G_LIVES, G_P1_X, G_P1_Y, G_TICKS = 0x39E4, 0x21A6, 0x2CFB, 0x2CFD, 0x39E6
PLAY_TICK_HOOK = 0x1562     # game86 1000:1562, the per-tick hook in play


def u16(dg, off):
    return struct.unpack_from("<H", dg, off)[0]


def test_scripted_run_is_deterministic():
    import refrun
    runs = [refrun.probe(0x0012ABCD, ORIGINAL, WORKDIR / f"replay-{i}", "vga", interval=50,
                         max_snapshots=14, keys=SCRIPT, quit_after=720, time_limit=90)
            for i in range(2)]
    for r in runs:
        assert r.exit_type == 0 and r.exit_code == 0, r
        assert r.snapshots_taken == 14 and r.keys_applied >= len(SCRIPT) + 1
    a, b = runs
    assert a.irq_count == b.irq_count
    for sa, sb in zip(a.snapshots, b.snapshots):
        assert (sa.irq, sa.vram_irq) == (sb.irq, sb.vram_irq)
        assert sa.dgroup == sb.dgroup, f"DGROUP differs at IRQ {sa.irq}"
        assert sa.vram == sb.vram, f"VRAM differs at IRQ {sa.vram_irq}"

    snaps = {s.irq: s.dgroup for s in a.snapshots}
    for irq in (50, 100):                               # title: one game tick per IRQ
        assert u16(snaps[irq], G_TICKS) == irq
    assert u16(snaps[250], G_TICKS) - u16(snaps[200], G_TICKS) == 50   # reset at game start
    assert u16(snaps[100], G_TICK_HOOK) != PLAY_TICK_HOOK
    assert u16(snaps[200], G_TICK_HOOK) == PLAY_TICK_HOOK   # fire at 150 started a game
    assert u16(snaps[200], G_LIVES) == 5
    assert u16(snaps[500], G_P1_X) > u16(snaps[400], G_P1_X)    # walked right
    assert u16(snaps[600], G_P1_Y) < u16(snaps[500], G_P1_Y)    # walked up
