"""W6 entity animate/draw handlers: game86 tbl_anim_handlers (DS:202E; 1000:4465, 4499,
4646, 4676, 468a, 46b0, 474a, 4779, 4802, 4bab and the empty 44d6 / 480d / 3fde), against
src/game/entities/anim.c through llamatron_game_testlib.

One live entity (slot 0) with a random record and the anim under test runs through
entities_animate_all (1000:4374 vs the port's), drawn with dirty recording on as in play,
with the game's real sprite bank. Compared: all three pages, the DGROUP state (anim 04 / 09
/ 0a advance their anim) and the pages after both sides' dirty lists are restored from a
random background (1e7b:0213..0299 vs gfx_restore_dirty). The beams are also drawn on their
own with recording off.
"""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases, indexed_page

from adapters.cport import bind
from adapters.gamestate import DgroupMap
from llamatron_harness.ega import HEIGHT, WIDTH
from test_sprites import (CODE0, CS_CODE0_SEG, CS_DIRTY_ENABLE, CS_DIRTY_PTR_B, CS_DIRTY_TMP,
                          CS_SAVED_DS, DIRTY_LIST, DRAW_PAGE, LIST_END, PAGES, PAGE_IDS,
                          SCROLL_MODE, DrawTarget, first_diff)

ENTITY_TABLE, ENTITY_SIZE, ENTITY_COUNT = 0x48C1, 0x40, 70
SPRITE_SEG = 0x218C
REFS_AT = 0x001C               # sprite number n: 218C:[0x1C + 2n]
WAVE, POWERUP = 0x21A8, 0x21C4
ANIM_COUNT = 19
SCALED = {0x00, 0x04, 0x05, 0x06, 0x07, 0x0C, 0x0F}
GC_INDEX = 0x3CE


@pytest.fixture(scope="module")
def dg(cgame):
    return DgroupMap(cgame)


class Port:
    def __init__(self, lib):
        bind(lib, "gfx_init", None)()
        self.page = bind(lib, "gfx_page", ctypes.c_void_p, ctypes.c_int)
        self.flip = bind(lib, "gfx_flip", None)
        self.display = bind(lib, "gfx_display_page_id", ctypes.c_int)
        self.target = bind(lib, "draw_target_page", DrawTarget, ctypes.c_int)
        self.set_dirty = bind(lib, "draw_set_dirty_recording", None, ctypes.c_bool)
        self.restore_dirty = bind(lib, "gfx_restore_dirty", None, ctypes.c_int)
        self.discard_dirty = bind(lib, "gfx_discard_dirty", None, ctypes.c_int)
        self.animate = bind(lib, "entities_animate_all", None, ctypes.c_void_p,
                            ctypes.POINTER(DrawTarget))
        self.beam_v = bind(lib, "draw_beam_vertical", None, ctypes.POINTER(DrawTarget), ctypes.c_int)
        self.beam_h = bind(lib, "draw_beam_horizontal", None, ctypes.POINTER(DrawTarget),
                           ctypes.c_int)
        bind(lib, "game_data_load", ctypes.c_bool)()
        bank = bind(lib, "asset_sprite_bank", ctypes.c_void_p, ctypes.c_int)(0)
        assert bank, "extracted data not loaded"
        find = bind(lib, "sprite_bank_find", ctypes.c_int, ctypes.c_void_p, ctypes.c_uint16)
        self.frame_of_offset = lambda off: find(bank, off)

    def load(self, pid, pixels):
        ctypes.memmove(self.page(pid), pixels, len(pixels))

    def read(self, pid):
        return ctypes.string_at(self.page(pid), WIDTH * HEIGHT)


@pytest.fixture(scope="module")
def port(cgame):
    return Port(cgame)


@pytest.fixture(scope="module")
def bank(cgame):
    """(sprite numbers whose entry is a frame, frame offsets) of the real bank."""
    from llamatron_harness import Machine, default_game86_path
    m = Machine.from_game86(default_game86_path())
    p = Port(cgame)
    numbers, offsets = [], set()
    for n in range(0x300):
        ptr = m.read_u16(SPRITE_SEG, REFS_AT + 2 * n)
        if ptr == 0xFFFF:
            break
        if p.frame_of_offset(ptr) >= 0:
            numbers.append(n)
            offsets.add(ptr)
    return numbers, sorted(offsets)


def handler_address(machine, anim):
    return machine.read_u16(machine.dgroup, 0x202E + 2 * anim)


def position(rnd):
    return (rnd.choice([rnd.randrange(-60, 380), rnd.randrange(0, 320), rnd.randrange(-24, 24),
                        rnd.randrange(296, 344)]),
            rnd.choice([rnd.randrange(-40, 240), rnd.randrange(0, 200), rnd.randrange(-20, 10),
                        rnd.randrange(190, 220)]))


def entity_record(rnd, anim, numbers, offsets):
    rec = bytearray(rnd.randbytes(ENTITY_SIZE))
    rec[0:2] = bytes([rnd.randrange(0x2E), rnd.randrange(0x40)])
    x, y = position(rnd)
    if anim == 0x10 and rnd.random() < 0.7:     # the mark is drawn 10 px above: keep it visible
        x, y = rnd.randrange(0, 320), rnd.randrange(10, 200)
    if anim == 0x02:
        # The horizontal beam's row: below row 203 the original writes into the next page,
        # which the port's page target drops (draw.h).
        y = rnd.randrange(0, 204)
    if anim in SCALED and y > 199:
        # The scaled draw always draws its first row: below the screen the original writes
        # into the next page, which the port's page target drops (draw.h).
        y = rnd.randrange(0, 200)
    struct.pack_into("<hhH", rec, 0x02, x, y, anim)
    if rnd.random() < 0.6:
        struct.pack_into("<h", rec, 0x0C, rnd.choice(numbers))
    else:
        struct.pack_into("<hH", rec, 0x0C, rnd.choice([-1, -2, -0x8000]), 0)
        struct.pack_into("<H", rec, 0x2C, rnd.choice(offsets))
    struct.pack_into("<h", rec, 0x0E, rnd.choice([0, 1, 1, 2, 3, rnd.randrange(0, 12)]))
    struct.pack_into("<h", rec, 0x14, rnd.choice([0, 1, -1, rnd.randrange(-0x8000, 0x8000)]))
    if anim == 0x0E:
        for i in range(4):
            struct.pack_into("<hh", rec, 0x16 + 4 * i, *position(rnd))
    rec[0x25] = rnd.choice([0, 0, rnd.randrange(1, 256)])
    struct.pack_into("<hhhh", rec, 0x32, 0, 0, 0, 0)       # no overlay sprite
    return bytes(rec)


def setup_pages(machine, port, rnd, page=None):
    page = rnd.choice([0x0000, 0x2000]) if page is None else page
    back = 0x2000 - page
    for off in PAGE_IDS:
        px = indexed_page(rnd, rnd.choice(["noise", "blocks"]))
        machine.ega.load_indexed(px, page=off)
        port.load(PAGE_IDS[off], px)
    if port.display() != PAGE_IDS[back]:
        port.flip()
    for pid in PAGE_IDS.values():
        port.discard_dirty(pid)
    machine.write_u16(CODE0, CS_SAVED_DS, machine.dgroup)
    machine.write_u16(CODE0, CS_DIRTY_TMP, DIRTY_LIST)
    machine.write_u16(CODE0, DIRTY_LIST, LIST_END)
    return page, back


def compare_pages(machine, port, what):
    for off, pid in PAGE_IDS.items():
        want = machine.ega.rasterise(off)
        got = port.read(pid)
        assert got == want, f"{what}, page {off:#06x}: " + first_diff(got, want)


def restore_and_compare(machine, port, page, back):
    machine.write_u16(CODE0, CS_DIRTY_PTR_B, DIRTY_LIST)
    machine.write_u16(*CS_CODE0_SEG, CODE0)
    machine.write_dgroup(PAGES, struct.pack("<2H", back, page))
    machine.run_until("1e7b:0213", "1e7b:029a", max_insns=2_000_000)
    port.restore_dirty(PAGE_IDS[page])
    compare_pages(machine, port, "restore")


def run_anim(machine, dg, port, bank, rnd, anim):
    numbers, offsets = bank
    # anim 04 on page-0 frames draws a stray record (docs/DEVIATIONS.md #16)
    page, back = setup_pages(machine, port, rnd, 0x2000 if anim == 0x04 else None)
    img = bytearray(machine.read_dgroup(0, 0x10000))
    for slot in range(ENTITY_COUNT):
        base = ENTITY_TABLE + ENTITY_SIZE * slot
        img[base:base + 2] = b"\xff\xff"
    img[ENTITY_TABLE:ENTITY_TABLE + ENTITY_SIZE] = entity_record(rnd, anim, numbers, offsets)
    struct.pack_into("<H", img, WAVE, rnd.randrange(12))
    # anim 10 tests the sign of the power-up state: 0 and -1 are its edges
    struct.pack_into("<h", img, POWERUP, rnd.choice([0, 0, 1, -1, -1, rnd.randrange(-0x8000, 0x8000)]))
    struct.pack_into("<HH", img, PAGES, page, back)
    struct.pack_into("<H", img, DRAW_PAGE, page)
    img[SCROLL_MODE] = 0
    img[0x39D9] = rnd.randrange(256)       # the handlers set the draw mode themselves
    image = dg.canonical(bytes(img))

    machine.write_dgroup(0, image)
    machine.write_u16(CODE0, CS_DIRTY_ENABLE, 1)
    machine.call("1000:4374", max_insns=5_000_000)
    want_state = machine.read_dgroup(0, 0x10000)

    state = dg.from_dgroup(image)
    target = port.target(PAGE_IDS[page])
    port.set_dirty(True)
    port.animate(state, ctypes.byref(target))
    port.set_dirty(False)

    compare_pages(machine, port, f"anim {anim:#04x}")
    bad = dg.mismatches(want_state, dg.to_dgroup(state, image))
    assert not bad, f"DGROUP differs at {[hex(o) for o in bad[:16]]}"
    restore_and_compare(machine, port, page, back)


def test_table_matches_original(machine, cgame):
    """Entries sharing an original handler share a port function (the port also shares one
    between the original's empty routines 44d6, 480d and 3fde)."""
    table = (ctypes.c_void_p * ANIM_COUNT).in_dll(cgame, "entity_anim_handlers")
    original = [handler_address(machine, i) for i in range(ANIM_COUNT)]
    assert all(table), "an anim handler is missing"
    for i in range(ANIM_COUNT):
        for j in range(ANIM_COUNT):
            if original[i] == original[j]:
                assert table[i] == table[j], (i, j)


@pytest.mark.parametrize("anim", range(ANIM_COUNT))
@pytest.mark.parametrize("case", fuzz_cases(12))
def test_anim(machine, dg, port, bank, anim, case):
    run_anim(machine, dg, port, bank, case_rng(f"anim:{anim}", case), anim)


@pytest.mark.parametrize("case", fuzz_cases(24))
def test_beams_not_recording(machine, port, case):
    """The beam loops on their own (1000:46cd..4746, 4795..4800), recording off, from any x
    and any row that stays on the page."""
    rnd = case_rng("beams", case)
    page, back = setup_pages(machine, port, rnd)
    vertical = rnd.random() < 0.5
    pos = rnd.choice([rnd.randrange(-80, 400), rnd.randrange(0, 320)]) if vertical \
        else rnd.choice([rnd.randrange(0, 204), rnd.randrange(0, 200)])
    machine.write_u16(CODE0, CS_DIRTY_ENABLE, 0)
    machine.write_dgroup(DRAW_PAGE, struct.pack("<H", page))
    di = ENTITY_TABLE
    machine.write_dgroup(di + (0x02 if vertical else 0x04), struct.pack("<h", pos))
    if vertical:
        machine.run_until("1000:46cd", "1000:4747", regs={"di": di}, max_insns=200_000)
    else:
        machine.run_until("1000:4795", "1000:4802", regs={"di": di}, max_insns=200_000)

    target = port.target(PAGE_IDS[page])
    port.set_dirty(False)
    (port.beam_v if vertical else port.beam_h)(ctypes.byref(target), pos)
    compare_pages(machine, port, "beam")
