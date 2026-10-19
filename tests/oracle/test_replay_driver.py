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


def test_ega_runs_on_the_ega_display_clock(driver, tmp_path):
    """Same ticks, fewer frames: EGA retraces at 59.92 Hz, VGA mode 0Dh at 70.086 Hz."""
    def frames(machine):
        proc = subprocess.run([driver, "--ticks", "500", machine], capture_output=True,
                              text=True, timeout=120)
        assert proc.returncode == 0, proc.stderr
        return int(proc.stdout.split("frames=")[1].split()[0])
    vga, ega = frames("--vga"), frames("--ega")
    assert abs(vga - 500 * 70.086 / 50.16) <= 2
    assert abs(ega - 500 * 59.92 / 50.16) <= 2


def test_clock_calibration(driver, tmp_path):
    """--start-delay N: N retraces of ordinary timer ticks with no game frame; --tick-phase F
    starts game_init F of a tick in, so the ticks come earlier (fewer retraces for as many)."""
    def frames(*args):
        proc = subprocess.run([driver, "--ticks", "500", *args], capture_output=True,
                              text=True, timeout=120)
        assert proc.returncode == 0, proc.stderr
        assert "ticks=500 " in proc.stdout
        return int(proc.stdout.split("frames=")[1].split()[0])
    base = frames()
    assert frames("--start-delay", "5") == base - 5
    assert frames("--tick-phase", "0.99") <= base
    assert frames("--tick-phase", "0") == base
    port = run(driver, tmp_path, "d", "--ticks", "20", "--every", "10", "--start-delay", "3")
    assert sorted(port.snapshots) == [10, 20]


def test_first_snapshot_tick(driver, tmp_path):
    port = run(driver, tmp_path, "f", "--ticks", "64", "--every", "1", "--first", "60")
    assert sorted(port.snapshots) == [60, 61, 62, 63, 64]


def test_compare_skips_stale_bytes_of_free_slots():
    mask = bytes([1]) * compare.DGROUP_SIZE
    ref, mine = bytearray(compare.DGROUP_SIZE), bytearray(compare.DGROUP_SIZE)
    for img in (ref, mine):                     # play hook, every slot allocated (type 0)
        struct.pack_into("<H", img, compare.TICK_HOOK, compare.PLAY_HOOK)
    slot = 0x48C1 + 0x40 * 20
    struct.pack_into("<hH", ref, slot, -1, 7)   # free in both, different stale x
    struct.pack_into("<hH", mine, slot, -1, 9)
    port = compare.PortRun(10, mask, {10: bytes(mine), 20: bytes(mine)})
    allocated = bytearray(ref)
    struct.pack_into("<h", allocated, slot, 3)  # the reference allocated it: compared again
    diffs = compare.compare(port, {10: bytes(ref), 20: bytes(allocated)})
    assert diffs[0].offsets == []
    assert "[ignored stale bytes of 1 free entity slots]" in compare.report(diffs[:1])
    assert diffs[1].offsets == [slot, slot + 1, slot + 2]


def _image(tick_hook, value, tick_count):
    img = bytearray(compare.DGROUP_SIZE)
    struct.pack_into("<H", img, compare.TICK_HOOK, tick_hook)
    struct.pack_into("<H", img, 0x39E6, tick_count)
    img[0x0927] = value                             # free_slots, some state
    return bytes(img)


def test_compare_one_tick_shift_at_a_hook_switch_is_timing():
    """Reference switches to play at 20, the port one tick later: the bytes its tick 21
    explains are timing (carried while the shift holds); other bytes stay real."""
    mask = bytearray(compare.DGROUP_SIZE)
    for off in (compare.TICK_HOOK, compare.TICK_HOOK + 1, 0x39E6, 0x39E7, 0x0927, 0x0810):
        mask[off] = 1
    nop, play = 0x3FDE, compare.PLAY_HOOK

    def img(hook, value, tick, other=0):
        image = bytearray(_image(hook, value, tick))
        image[0x0810] = other
        return bytes(image)

    ref = {10: img(nop, 0, 10), 20: img(play, 5, 20), 30: img(play, 6, 30),
           40: img(play, 7, 40, other=1)}
    port = {10: img(nop, 0, 10),
            19: img(nop, 0, 19), 20: img(nop, 0, 20), 21: img(play, 5, 21),
            29: img(play, 5, 29), 30: img(play, 5, 30), 31: img(play, 6, 31),
            39: img(play, 6, 39), 40: img(play, 6, 40), 41: img(play, 7, 41)}
    diffs = {d.tick: d for d in compare.compare(compare.PortRun(10, bytes(mask), port), ref)}
    assert diffs[10].offsets == [] and diffs[10].shift == 0
    assert diffs[20].shift == 1 and diffs[20].offsets == []        # at the switch
    assert diffs[30].shift == 1 and diffs[30].offsets == []        # carried, no switch
    assert diffs[40].shift == 1 and diffs[40].offsets == [0x0810]  # timing + a real byte
    text = compare.report(list(diffs.values()))
    assert "timing shifts (one tick, at hook switches): ticks 20, 30, 40" in text
    assert "first divergence at tick 40" in text


def test_compare_shift_without_a_hook_switch_is_a_divergence():
    mask = bytearray(compare.DGROUP_SIZE)
    mask[0x0927] = 1
    play = compare.PLAY_HOOK
    ref = {20: _image(play, 5, 20)}
    port = {20: _image(play, 4, 20), 21: _image(play, 5, 21)}
    diffs = compare.compare(compare.PortRun(10, bytes(mask), port), ref)
    assert diffs[0].shift == 0 and compare.first_divergence(diffs) == 20
