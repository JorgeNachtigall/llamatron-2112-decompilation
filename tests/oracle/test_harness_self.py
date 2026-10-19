"""Self-tests of the harness: image loading and call mechanics."""

import pytest

from llamatron_harness import HarnessError, Machine, load_image
from llamatron_harness.mzimage import LoadedImage


def test_dgroup_and_stack(machine):
    assert machine.dgroup == 0x3559                 # PLAN.md §1: DGROUP 3559
    assert machine.image.load_seg == 0x1000
    assert (machine.image.entry_cs, machine.image.entry_ip) == (0x1000, 0x0010)


def test_flat_image_equals_mz(game86_path, tmp_path):
    """A flat, pre-relocated module (extractor's .unpacked) loads to the same bytes."""
    mz = load_image(game86_path)
    if game86_path.suffix != ".exe":
        pytest.skip("default image is already flat")
    flat = tmp_path / "game86.unpacked"
    flat.write_bytes(mz.data)
    img = load_image(flat)
    assert img.data == mz.data and img.load_seg == 0x1000
    assert Machine(img).dgroup == 0x3559


def test_extractor_flat_image_matches(game86_path):
    """If tools/extract has produced build/generated/exe/game86.unpacked, compare it."""
    from llamatron_harness.runner import REPO_ROOT
    flat = REPO_ROOT / "build/generated/exe/game86.unpacked"
    mz = REPO_ROOT / "unpacked/game86.exe"
    if not (flat.exists() and mz.exists()):
        pytest.skip("needs both build/generated/exe/game86.unpacked and unpacked/game86.exe")
    a, b = load_image(flat), load_image(mz)
    assert a.data == b.data
    assert (a.entry_cs, a.entry_ip, a.stack_ss, a.stack_sp) == \
           (b.entry_cs, b.entry_ip, b.stack_ss, b.stack_sp)
    assert a.min_extra_paras == b.min_extra_paras


def test_unhandled_interrupt_aborts(machine):
    # 1000:e4f5 starts with INT 10h (set mode 0Dh); without a handler the run must stop.
    with pytest.raises(HarnessError, match="unhandled INT 10h"):
        machine.call("1000:e4f5")


def test_interrupt_handler_is_called(machine):
    seen = []
    res = machine.call("1000:e4f5", interrupts={0x10: lambda m, n: seen.append(m.get_regs()["ax"])},
                       max_insns=200_000)
    assert seen and seen[0] == 0x000D               # AX=000Dh: mode 0Dh
    assert res.interrupts[0].number == 0x10
