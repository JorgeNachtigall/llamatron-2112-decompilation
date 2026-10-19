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


def test_compare_skips_attract_llamas_while_title_hooks_run():
    mask = bytearray(compare.DGROUP_SIZE)
    llama = 0x48C1 + 5                     # entity 0, inside ATTRACT_LLAMAS
    mask[llama] = 1
    mine = bytearray(compare.DGROUP_SIZE)
    mine[llama] = 7

    def ref(hook):
        img = bytearray(compare.DGROUP_SIZE)
        struct.pack_into("<H", img, compare.TICK_HOOK, hook)
        return bytes(img)

    port = compare.PortRun(10, bytes(mask), {t: bytes(mine) for t in (10, 20, 30, 40)})
    refs = {10: ref(0x1234), 20: ref(0x3F6F), 30: ref(0x3FDE), 40: ref(compare.PLAY_HOOK)}
    diffs = {d.tick: d for d in compare.compare(port, refs)}
    assert diffs[10].offsets == [llama] and not diffs[10].ignored     # before attract
    assert diffs[20].offsets == [] and diffs[20].ignored              # title hook
    assert diffs[30].offsets == [] and diffs[30].ignored              # hook_nop: still attract
    assert diffs[40].offsets == [llama] and not diffs[40].ignored     # in play: compared again
    assert "[ignored attract llamas 48C1..4B00]" in compare.report(list(diffs.values()))


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


def test_compare_skips_p2_trail_head_until_p2_writes():
    mask = bytearray(compare.DGROUP_SIZE)
    mask[0x2CF9] = mask[0x2CFA] = 1
    mine = bytes(compare.DGROUP_SIZE)                    # the port starts at entry 0
    loaded, written = bytearray(mine), bytearray(mine)
    loaded[0x2CF9] = 2
    written[0x2CF9] = 8                                  # P2 wrote entry 1
    port = compare.PortRun(10, bytes(mask), {10: mine, 20: mine})
    diffs = compare.compare(port, {10: bytes(loaded), 20: bytes(written)})
    assert diffs[0].offsets == [] and diffs[0].ignored
    assert diffs[1].offsets == [0x2CF9] and not diffs[1].ignored
    assert "[ignored trail_head[1] = 2 (DEVIATIONS #10) 2CF9..2CFA]" in compare.report(diffs)
