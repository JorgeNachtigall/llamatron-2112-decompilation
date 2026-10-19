"""W6 sprite draw wrappers: game86 1000:42bc / 42cb (draw with recording off, left off),
42da / 42f0 (register-saving draws), 4284 (g3 shape into all three pages) and 4bd3 (into the
background page, then the draw page), and the page clear 63c4, against src/game/draw_sprite.c
and gfx_clear through llamatron_game_testlib.

The sprite is a random one in the bank's layout (test_sprites.Sprite) at a scratch segment
that both CS:[0x25F] (sprite bank) and CS:[0x261] (g3 shapes) point at. Compared: all three
pages, the dirty-recording switch afterwards and, where something was recorded, the pages
after both sides' dirty lists are restored from a random background.
"""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases, indexed_page

from adapters.cport import bind
from test_sprites import (CODE0, CS_CODE0_SEG, CS_DIRTY_ENABLE, CS_DIRTY_PTR_B, CS_DIRTY_TMP,
                          CS_SAVED_DS, DIRTY_LIST, DRAW_FLAGS, DRAW_PAGE, LIST_END, PAGES,
                          PAGE_IDS, SCROLL_MODE, SPRITE_SEG, C, DrawTarget, GfxSprite, Sprite,
                          first_diff, position)

CS_SPRITE_SEG, CS_G3_SEG = 0x025F, 0x0261


class Wrappers(C):
    def __init__(self, lib):
        super().__init__(lib)
        sprite_args = (ctypes.POINTER(GfxSprite), ctypes.c_int, ctypes.c_int)
        self.unrecorded = bind(lib, "draw_sprite_unrecorded", None, ctypes.POINTER(DrawTarget),
                               *sprite_args)
        self.all_pages = bind(lib, "draw_sprite_all_pages", None, *sprite_args)
        self.with_background = bind(lib, "draw_sprite_with_background", None,
                                    ctypes.POINTER(DrawTarget), *sprite_args)
        self.recording = bind(lib, "draw_dirty_recording", ctypes.c_bool)
        self.clear = bind(lib, "gfx_clear", None, ctypes.c_int, ctypes.c_uint8)


@pytest.fixture
def w(cgame):
    return Wrappers(cgame)


def setup(machine, w, rnd, sprite, draw_page, dirty):
    """Random pages on both sides, the sprite at SPRITE_SEG, an empty dirty list. The page
    drawn next ([0x39d3]) is draw_page; the other of pages 0 / 1 is on screen."""
    back = 0x2000 - draw_page
    for off in PAGE_IDS:
        px = indexed_page(rnd, rnd.choice(["noise", "blocks"]))
        machine.ega.load_indexed(px, page=off)
        w.load(PAGE_IDS[off], px)
    if w.display() != PAGE_IDS[back]:
        w.flip()
    machine.write(SPRITE_SEG, 0, sprite.original_bytes())
    machine.write_u16(CODE0, CS_SPRITE_SEG, SPRITE_SEG)
    machine.write_u16(CODE0, CS_G3_SEG, SPRITE_SEG)
    machine.write_u16(CODE0, CS_SAVED_DS, machine.dgroup)
    machine.write_u16(CODE0, CS_DIRTY_ENABLE, 1 if dirty else 0)
    machine.write_u16(CODE0, CS_DIRTY_TMP, DIRTY_LIST)
    machine.write_u16(CODE0, DIRTY_LIST, LIST_END)
    machine.write_dgroup(PAGES, struct.pack("<2H", draw_page, back))
    machine.write_dgroup(DRAW_PAGE, struct.pack("<H", draw_page))
    machine.write_dgroup(SCROLL_MODE, b"\0")
    machine.write_dgroup(DRAW_FLAGS, b"\xff")
    w.set_dirty(dirty)


def compare_pages(machine, w, what):
    for off, pid in PAGE_IDS.items():
        want = machine.ega.rasterise(off)
        got = w.read(pid)
        assert got == want, f"{what}, page {off:#06x}: " + first_diff(got, want)


def compare_recording(machine, w):
    want = machine.read_u16(CODE0, CS_DIRTY_ENABLE) != 0
    assert w.recording() == want, "dirty recording switch afterwards"


def restore_and_compare(machine, w, draw_page):
    back = 0x2000 - draw_page
    machine.write_u16(CODE0, CS_DIRTY_PTR_B, DIRTY_LIST)
    machine.write_u16(*CS_CODE0_SEG, CODE0)
    machine.write_dgroup(PAGES, struct.pack("<2H", back, draw_page))
    machine.run_until("1e7b:0213", "1e7b:029a", max_insns=2_000_000)
    w.restore_dirty(PAGE_IDS[draw_page])
    compare_pages(machine, w, "after the dirty-list restore")


def unrecorded_position(rnd, sprite):
    """Where an unrecorded draw stays on its page, as in test_sprites (no vertical clip; left
    of x = -16 an unaligned one-column unrecorded draw corrupts the original's stack)."""
    x = rnd.randrange(-16, 360) + (sprite.nominal >> 1)
    y = rnd.randrange(0, 200 - sprite.rows + 1) + ((sprite.rows - 1) >> 1)
    return x, y


def call(machine, entry, x, y, es=None):
    regs = {"ax": x & 0xFFFF, "bx": y & 0xFFFF, "si": 0}
    if es is not None:
        regs["es"] = es
    machine.call(entry, regs=regs, max_insns=6_000_000)


@pytest.mark.parametrize("entry", ["1000:42bc", "1000:42cb"])
@pytest.mark.parametrize("case", fuzz_cases(12))
def test_unrecorded(machine, w, entry, case):
    """42bc / 42cb switch recording off for good and draw into [0x39d7], unclipped."""
    rnd = case_rng("draw_unrecorded" + entry, case)
    sprite = Sprite(rnd)
    page = rnd.choice(list(PAGE_IDS))
    setup(machine, w, rnd, sprite, rnd.choice([0x0000, 0x2000]), rnd.random() < 0.7)
    machine.write_dgroup(DRAW_PAGE, struct.pack("<H", page))
    x, y = unrecorded_position(rnd, sprite)
    call(machine, entry, x, y)

    target = w.target(PAGE_IDS[page])
    w.unrecorded(ctypes.byref(target), ctypes.byref(sprite.to_c()), x, y)
    compare_pages(machine, w, "draw")
    compare_recording(machine, w)


@pytest.mark.parametrize("entry", ["1000:42da", "1000:42f0"])
@pytest.mark.parametrize("case", fuzz_cases(12))
def test_draw(machine, w, entry, case):
    """42da (ES from the caller) / 42f0 (the sprite bank's segment): draw_sprite."""
    rnd = case_rng("draw_wrapped" + entry, case)
    sprite = Sprite(rnd)
    page = rnd.choice([0x0000, 0x2000])
    dirty = rnd.random() < 0.7
    setup(machine, w, rnd, sprite, page, dirty)
    x, y = position(rnd, sprite) if dirty else unrecorded_position(rnd, sprite)
    call(machine, entry, x, y, es=SPRITE_SEG if entry == "1000:42da" else 0)

    target = w.target(PAGE_IDS[page])
    w.draw(ctypes.byref(target), ctypes.byref(sprite.to_c()), x, y)
    compare_pages(machine, w, "draw")
    compare_recording(machine, w)
    if dirty:
        w.set_dirty(False)
        restore_and_compare(machine, w, page)


@pytest.mark.parametrize("case", fuzz_cases(16))
def test_all_pages(machine, w, case):
    """4284: into pages 0, 1 and the background, unrecorded; recording restored. The draw
    flags start random (4284 sets them to a plain draw)."""
    rnd = case_rng("draw_all_pages", case)
    sprite = Sprite(rnd)
    page = rnd.choice([0x0000, 0x2000])
    setup(machine, w, rnd, sprite, page, rnd.random() < 0.7)
    machine.write_dgroup(DRAW_FLAGS, bytes([rnd.randrange(256)]))
    machine.write_dgroup(DRAW_PAGE, struct.pack("<H", rnd.choice(list(PAGE_IDS))))
    x, y = unrecorded_position(rnd, sprite)
    call(machine, "1000:4284", x, y, es=SPRITE_SEG)

    w.all_pages(ctypes.byref(sprite.to_c()), x, y)
    compare_pages(machine, w, "draw")
    compare_recording(machine, w)
    assert machine.read_dgroup(DRAW_PAGE, 2) == struct.pack("<H", page), \
        "4284 leaves [0x39d7] = the page drawn next"


def run_with_background(machine, x, y):
    """4bd3 with the caller's x: the original loads the page offsets 0x4000 and [0x39d3]
    into AX (its x) before each draw (docs/DEVIATIONS.md #18), so its own instructions run
    in two parts with AX = x put back: 4bd9..4bed (the background draw, after 4bd3..4bd6's
    [0x39d7] = 0x4000) and 4bf3.. (the draw page, after 4bed..4bf0's [0x39d7] = [0x39d3])."""
    machine.write_dgroup(DRAW_PAGE, struct.pack("<H", 0x4000))
    machine.run_until("1000:4bd9", "1000:4bed", regs={"ax": x & 0xFFFF, "bx": y & 0xFFFF,
                                                       "si": 0}, max_insns=6_000_000)
    machine.write_dgroup(DRAW_PAGE, machine.read_dgroup(PAGES, 2))
    call(machine, "1000:4bf3", x, y)


@pytest.mark.parametrize("case", fuzz_cases(24))
def test_with_background(machine, w, case):
    """4bd3: into the background page unrecorded, then into the page drawn next as usual."""
    rnd = case_rng("draw_with_background", case)
    sprite = Sprite(rnd)
    page = rnd.choice([0x0000, 0x2000])
    dirty = rnd.random() < 0.7
    setup(machine, w, rnd, sprite, page, dirty)
    x, y = unrecorded_position(rnd, sprite)
    run_with_background(machine, x, y)

    target = w.target(PAGE_IDS[page])
    w.with_background(ctypes.byref(target), ctypes.byref(sprite.to_c()), x, y)
    compare_pages(machine, w, "draw")
    compare_recording(machine, w)
    if dirty:
        w.set_dirty(False)
        restore_and_compare(machine, w, page)


@pytest.mark.parametrize("case", fuzz_cases(4))
def test_with_background_ignores_x(game86_path, case):
    """The slip itself: the whole 4bd3 draws something, the same whatever x it is given."""
    from llamatron_harness.runner import Machine
    rnd = case_rng("draw_with_background_x", case)
    sprite = Sprite(rnd)
    y = unrecorded_position(rnd, sprite)[1]
    pages = [indexed_page(rnd, "noise") for _ in PAGE_IDS]
    results = []
    for x in (rnd.randrange(0, 320), rnd.randrange(0, 320)):
        machine = Machine.from_game86(game86_path)
        for off, px in zip(PAGE_IDS, pages):
            machine.ega.load_indexed(px, page=off)
        machine.write(SPRITE_SEG, 0, sprite.original_bytes())
        machine.write_u16(CODE0, CS_SPRITE_SEG, SPRITE_SEG)
        machine.write_u16(CODE0, CS_SAVED_DS, machine.dgroup)
        machine.write_u16(CODE0, CS_DIRTY_ENABLE, 0)
        machine.write_dgroup(PAGES, struct.pack("<2H", 0x0000, 0x2000))
        machine.write_dgroup(SCROLL_MODE, b"\0")
        machine.write_dgroup(DRAW_FLAGS, b"\xff")
        before = bytes(b for p in machine.ega.planes for b in p)
        call(machine, "1000:4bd3", x, y)
        results.append(bytes(b for p in machine.ega.planes for b in p))
        assert results[-1] != before, "4bd3 drew nothing"
    assert results[0] == results[1]


@pytest.mark.parametrize("page", list(PAGE_IDS))
def test_clear_page(machine, w, page):
    """63c4 with DI = a page's offset (all its callers): gfx_clear(page, 0); the other pages
    are untouched."""
    rnd = case_rng("clear_page", page)
    setup(machine, w, rnd, Sprite(rnd), 0x0000, False)
    machine.call("1000:63c4", regs={"di": page}, max_insns=200_000)
    w.clear(PAGE_IDS[page], 0)
    compare_pages(machine, w, "clear")
