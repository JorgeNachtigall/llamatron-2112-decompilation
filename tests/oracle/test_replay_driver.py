"""The headless replay driver (tests/replay/replay_driver.c) and its comparator, without
DOSBox: deterministic output, the adapter's mask in the file, snapshot ticks, and compare()
on images that differ in known places."""

import os
import struct
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "replay"))
import compare  # noqa: E402

from adapters.gamestate import DgroupMap  # noqa: E402


@pytest.fixture(scope="module")
def driver():
    path = os.environ.get("LLAMATRON_REPLAY")
    if not path or not Path(path).exists():
        pytest.skip("llamatron_replay not built (LLAMATRON_REPLAY unset)")
    return path


def run(driver, tmp_path, name, *args):
    out = tmp_path / f"{name}.lrpl"
    proc = subprocess.run([driver, *args, "--out", str(out)], capture_output=True, text=True,
                          timeout=120)
    assert proc.returncode == 0, proc.stderr
    return compare.read_port(out)


def write_keys(path, events):
    path.write_bytes(b"".join(struct.pack("<IBB", t, sc, 0) for t, sc in events))
    return path


def test_deterministic_with_keys(driver, tmp_path):
    keys = write_keys(tmp_path / "PROBE.KEY", [(20, 0x39), (25, 0xB9), (60, 0x4D), (90, 0xCD)])
    args = ["--ticks", "200", "--every", "25", "--seed", "1223629", "--keys", str(keys)]
    a = run(driver, tmp_path, "a", *args)
    b = run(driver, tmp_path, "b", *args)
    assert sorted(a.snapshots) == list(range(25, 201, 25))
    assert a.snapshots == b.snapshots


def test_mask_is_the_adapters(driver, cgame, tmp_path):
    port = run(driver, tmp_path, "m", "--ticks", "10", "--every", "10")
    assert port.mask == DgroupMap(cgame).mask


def test_seed_changes_rng_table(driver, tmp_path):
    a = run(driver, tmp_path, "s1", "--ticks", "5", "--every", "5", "--seed", "1")
    b = run(driver, tmp_path, "s2", "--ticks", "5", "--every", "5", "--seed", "99999")
    table = slice(0x6AC9, 0x6AC9 + 0x200)
    assert a.snapshots[5][table] != b.snapshots[5][table]


def test_compare_names_fields():
    mask = bytearray(compare.DGROUP_SIZE)
    for off in (0x21A6, 0x21A7, 0x48C1 + 0x40 * 3 + 2, 0x4441):
        mask[off] = 1
    ref = bytearray(compare.DGROUP_SIZE)
    mine = bytearray(ref)
    mine[0x21A6] = 4                       # lives
    mine[0x48C1 + 0x40 * 3 + 2] = 9        # entity 3, x
    mine[0x0100] = 1                       # unmapped: ignored
    port = compare.PortRun(10, bytes(mask), {10: bytes(ref), 20: bytes(mine)})
    diffs = compare.compare(port, {10: bytes(ref), 20: bytes(ref), 30: bytes(ref)})
    assert [d.tick for d in diffs] == [10, 20]
    assert diffs[0].offsets == []
    assert diffs[1].fields() == ["player+00", "entities[3]+02"]
    assert "first divergence at tick 20" in compare.report(diffs)
