"""End-to-end proof DOSBox-X -> harness oracle (PLAN §3.3).

The original game runs in DOSBox-X under PROBE.COM with the BIOS tick pinned during
start-up (tests/README.md: the RNG table depends on every tick value the shuffle
reads). The 512-byte random table and the mixer state it ends with must equal what
the unicorn oracle computes for the same constant tick schedule, and, when
llamatron_game_testlib is built, what the port's rng_init_constant produces.
"""

import os
import shutil
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "harness" / "dosbox"))

from adapters.rng import (MIXER, PERM_INDEX, PERM_SIZE, PERM_TABLE, RngState,  # noqa: E402
                          rng_from_dgroup, rng_to_dgroup)

ORIGINAL = Path(os.environ.get("LLAMATRON_ORIGINAL_DIR") or ROOT / "original")
WORKDIR = Path(os.environ.get("LLAMATRON_DOSBOX_WORKDIR") or ROOT / "build-dosbox")

pytestmark = pytest.mark.skipif(
    not (shutil.which("dosbox-x") and shutil.which("nasm") and (ORIGINAL / "llama.exe").exists()),
    reason="needs dosbox-x, nasm and the original files")


@pytest.mark.parametrize("machine,ticks", [
    ("vga", 0x0012ABCD),
    ("ega", 0x00001234),
])
def test_probe_table_matches_oracle(machine, ticks):
    import refrun
    from llamatron_harness import Machine, default_game86_path

    res = refrun.probe(ticks, ORIGINAL, WORKDIR / f"probe-{machine}-{ticks:08x}", machine)
    assert res.exit_type == 0 and res.exit_code == 0, res
    assert res.snapshot_taken, res
    assert res.ticks_seen == ticks, "BIOS tick advanced during start-up: schedule not pinned"
    dg = res.dgroup

    oracle = Machine.from_game86(default_game86_path())
    lo, hi = ticks & 0xFFFF, ticks >> 16
    rng_to_dgroup(oracle, RngState(mixer=(lo, hi, lo, hi)))   # as 1000:5f6b seeds it
    oracle.set_bios_ticks(ticks)
    oracle.run_until("1000:007b", "1000:00ab")
    want = rng_from_dgroup(oracle)

    assert dg[PERM_TABLE:PERM_TABLE + PERM_SIZE] == want.table
    assert struct.unpack("<4H", dg[MIXER:MIXER + 8]) == want.mixer
    assert struct.unpack("<H", dg[PERM_INDEX:PERM_INDEX + 2])[0] == 0

    from adapters.cport import load_testlib
    lib = load_testlib()
    if lib is not None:                       # third link of the chain: the C port
        import ctypes
        from adapters.rng import CRngApi, rng_from_c, rng_to_c
        c = rng_to_c(RngState())
        CRngApi(lib).init_constant(ctypes.byref(c), ticks)
        got = rng_from_c(c)
        assert got.table == dg[PERM_TABLE:PERM_TABLE + PERM_SIZE]
        assert got.mixer == want.mixer
