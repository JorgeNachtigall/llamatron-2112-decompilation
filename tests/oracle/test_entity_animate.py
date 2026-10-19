"""W4 entities_animate_all (src/game/frame.c) against game86 1000:4374: which entities run,
and the overlay sprite's countdown (+38) and switch-off (+32 = 0). Every live entity uses
the idle animate state 0x12 (hook_nop in the original, an empty entry in the port), so only
the loop itself is compared; the pixels of the overlay draw are W6's blitter tests."""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases

from adapters.cport import bind
from adapters.gamestate import DgroupMap

ENTITY_TABLE, ENTITY_SIZE, ENTITY_COUNT = 0x48C1, 0x40, 70
ANIM_IDLE = 0x12
SPRITE_REF_BASE = 0x0E
CS_DIRTY_ENABLE = 0x672F       # dirty recording off: nothing to compare there
DRAW_PAGE = 0x39D7


class DrawTarget(ctypes.Structure):
    _fields_ = [("pixels", ctypes.c_void_p), ("cell_count", ctypes.c_int32),
                ("wraps", ctypes.c_bool), ("page", ctypes.c_int)]


@pytest.fixture(scope="module")
def dg(cgame):
    return DgroupMap(cgame)


def frame_sprites(machine):
    """Overlay sprite numbers whose table entry is a frame (not a link or the end mark)."""
    refs_at = 0x001C
    out = []
    for i in range(400):
        ptr = machine.read_u16(0x218C, refs_at + 2 * i)
        if ptr == 0xFFFF:
            break
        if ptr >= 0x0400:          # points at a frame header, not back into the table
            out.append(i)
    return out


def entity_table(rnd, sprites):
    out = bytearray()
    for _ in range(ENTITY_COUNT):
        rec = bytearray(rnd.randbytes(ENTITY_SIZE))
        if rnd.random() < 0.3:
            rec[0:2] = b"\xff\xff"
        else:
            rec[0:2] = bytes([rnd.randrange(0x2e), rnd.randrange(0x40)])
        struct.pack_into("<H", rec, 0x06, ANIM_IDLE)
        overlay_x = 0 if rnd.random() < 0.3 else rnd.randrange(16, 304)
        frames = rnd.choice([0, 1, 1, 2, rnd.randrange(1, 50), 0x8000])
        struct.pack_into("<hhhH", rec, 0x32, overlay_x, rnd.randrange(16, 184),
                         rnd.choice(sprites), frames)
        out += rec
    return bytes(out)


@pytest.mark.parametrize("case", fuzz_cases(32))
def test_entities_animate_all(machine, dg, cgame, case):
    rnd = case_rng("entities_animate_all", case)
    img = bytearray(machine.read_dgroup(0, 0x10000))
    img[ENTITY_TABLE:ENTITY_TABLE + ENTITY_COUNT * ENTITY_SIZE] = entity_table(rnd, frame_sprites(machine))
    struct.pack_into("<H", img, DRAW_PAGE, 0x2000)
    img = dg.canonical(bytes(img))
    machine.write_dgroup(0, img)
    machine.write(0x1000, CS_DIRTY_ENABLE, b"\x00")
    state = dg.from_dgroup(img)

    machine.call("1000:4374")

    bind(cgame, "draw_set_dirty_recording", None, ctypes.c_bool)(False)
    target = bind(cgame, "draw_target_page", DrawTarget, ctypes.c_int)(1)
    bind(cgame, "entities_animate_all", None, ctypes.c_void_p, ctypes.POINTER(DrawTarget))(
        state, ctypes.byref(target))

    assert dg.mismatches(machine.read_dgroup(0, 0x10000), dg.to_dgroup(state, img)) == []
