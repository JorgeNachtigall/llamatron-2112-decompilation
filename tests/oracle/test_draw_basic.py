"""W6 rendering: whole-page copies, dotted rectangle outline and the dirty-rectangle restore
pass, original game86 code (EGA plane model) vs the port's C (llamatron_game_testlib).

C side: src/game/draw_page.c, src/game/draw_rect.c, gfx_restore_rect (src/engine/gfx.c).
"""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases, indexed_page, screen_rect

from adapters.cport import bind
from llamatron_harness.ega import HEIGHT, WIDTH

PAGE_IDS = {0x0000: 0, 0x2000: 1, 0x4000: 2}   # VRAM offset -> GfxPageId
PAGE_BYTES = WIDTH * HEIGHT


class GfxRect(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int16), ("y", ctypes.c_int16),
                ("w", ctypes.c_int16), ("h", ctypes.c_int16)]


class CGfx:
    """The port's pages through ctypes."""

    def __init__(self, lib):
        self.lib = lib
        bind(lib, "gfx_init", None)()
        self._page = bind(lib, "gfx_page", ctypes.c_void_p, ctypes.c_int)
        self.flip = bind(lib, "gfx_flip", None)
        self.draw_page_id = bind(lib, "gfx_draw_page_id", ctypes.c_int)
        self.display_page_id = bind(lib, "gfx_display_page_id", ctypes.c_int)

    def load(self, page_id: int, pixels: bytes) -> None:
        ctypes.memmove(self._page(page_id), pixels, PAGE_BYTES)

    def read(self, page_id: int) -> bytes:
        return ctypes.string_at(self._page(page_id), PAGE_BYTES)

    def set_pages(self, front: int, back: int) -> None:
        """Original [0x39d3] front (drawn next) / [0x39d5] back (on screen)."""
        if self.display_page_id() != PAGE_IDS[back]:
            self.flip()
        assert (self.draw_page_id(), self.display_page_id()) == (PAGE_IDS[front], PAGE_IDS[back])


@pytest.fixture
def cgfx(cgame):
    return CGfx(cgame)


def load_both(machine, cgfx, offset, pixels):
    machine.ega.load_indexed(pixels, page=offset)
    cgfx.load(PAGE_IDS[offset], pixels)


def assert_pages_equal(machine, cgfx):
    for off, pid in PAGE_IDS.items():
        assert cgfx.read(pid) == machine.ega.rasterise(off), f"page {off:#06x}"


# ---- 1000:62e8 / 62f6 / 6304 ----------------------------------------------------------

PAGES = 0x39D3        # u16 front, u16 back (VRAM offsets)


@pytest.mark.parametrize("entry,c_name", [
    ("1000:62e8", "draw_save_screen_as_background"),
    ("1000:62f6", "draw_show_background"),
    ("1000:6304", "draw_copy_screen_to_draw_page"),
])
@pytest.mark.parametrize("swap", [False, True])
def test_page_copy(machine, cgfx, entry, c_name, swap):
    front, back = (0x2000, 0x0000) if swap else (0x0000, 0x2000)
    rnd = case_rng(f"draw_page_copy:{entry}", int(swap))
    for off in PAGE_IDS:
        load_both(machine, cgfx, off, indexed_page(rnd, rnd.choice(["noise", "blocks"])))
    machine.write_dgroup(PAGES, struct.pack("<2H", front, back))
    cgfx.set_pages(front, back)

    machine.call(entry, max_insns=200_000)
    bind(cgfx.lib, c_name, None)()

    assert_pages_equal(machine, cgfx)


# ---- 1000:6112 dotted outline ----------------------------------------------------------

RECT = 0x444          # x0, y0, x1, y1 (u16 each)
DRAW_PAGE = 0x39D7


@pytest.mark.parametrize("case", fuzz_cases(24))
def test_rect_outline(machine, cgfx, case):
    rnd = case_rng("draw_rect_outline", case)
    if case % 4 == 3:
        # Degenerate and reversed rectangles: every edge loop still draws once.
        x0, x1 = rnd.randrange(WIDTH - 2), rnd.randrange(WIDTH - 2)
        y0, y1 = rnd.randrange(HEIGHT - 2), rnd.randrange(HEIGHT - 2)
    else:
        x0, y0, x1, y1 = screen_rect(rnd, margin_x=2, margin_y=2)
    colour = rnd.randrange(16)
    page = rnd.choice([0x0000, 0x2000, 0x4000])
    for off in PAGE_IDS:
        load_both(machine, cgfx, off, indexed_page(rnd, rnd.choice(["noise", "blocks", "blank"])))
    machine.write_dgroup(RECT, struct.pack("<4H", x0, y0, x1, y1))
    machine.write_dgroup(DRAW_PAGE, struct.pack("<H", page))

    machine.call("1000:6112", regs={"bp": colour})
    bind(cgfx.lib, "draw_rect_outline", None, ctypes.c_int, ctypes.c_int, ctypes.c_int,
         ctypes.c_int, ctypes.c_int, ctypes.c_uint8)(PAGE_IDS[page], x0, y0, x1, y1, colour)

    assert_pages_equal(machine, cgfx)


# ---- 1e7b:0213..0299 dirty-list restore (tbl_restore_by_size 1e7b:031c) ---------------

CODE0 = 0x1000
CS_CODE0_SEG = (0x1E7B, 0x013D)   # frame_run's copy of the code-0 segment (set at start-up)
DIRTY_PTR_B = 0x6735              # CS:[6735] list restored this frame
DIRTY_LIST = 0x6737               # start of the first list in the image
LIST_END = 0xFE0C


def dirty_entries(rnd, n):
    """Entries as the sprite drawers record them: low byte = 16-px words, high byte != 0
    adds one word (unaligned x), rows, page-relative byte offset. Sizes cover the unrolled
    routines (1..3 words, 1..31 rows) and the generic loop (more words or rows); offsets
    include byte column 39 (span continues on the next row) and the last rows."""
    out = []
    for _ in range(n):
        words = rnd.choice([1, 1, 2, 2, 3, 4, 5])
        flag = rnd.choice([0, 0, 1])
        rows = rnd.choice([rnd.randrange(1, 32), rnd.randrange(1, 32), rnd.randrange(32, 60)])
        total_words = words + (1 if flag else 0)
        col = rnd.choice([0, 39, 40 - 2 * total_words, rnd.randrange(40)])
        col = max(0, min(39, col))
        row = rnd.choice([0, HEIGHT - rows, rnd.randrange(HEIGHT - rows + 1)])
        out.append((words | flag << 8, rows, row * 40 + col))
    return out


def c_restore(cgfx, page_id, entries):
    restore = bind(cgfx.lib, "gfx_restore_rect", None, ctypes.c_int, GfxRect)
    for count, rows, offset in entries:
        words = (count & 0xFF) + (1 if count >> 8 else 0)
        restore(page_id, GfxRect(offset % 40 * 8, offset // 40, words * 16, rows))


@pytest.mark.parametrize("case", fuzz_cases(24))
def test_restore_dirty_list(machine, cgfx, case):
    rnd = case_rng("draw_restore", case)
    back = rnd.choice([0x0000, 0x2000])
    front = 0x2000 - back
    for off in PAGE_IDS:
        load_both(machine, cgfx, off, indexed_page(rnd, "noise"))
    entries = dirty_entries(rnd, rnd.randrange(1, 12))
    blob = b"".join(struct.pack("<3H", *e) for e in entries) + struct.pack("<H", LIST_END)
    machine.write(CODE0, DIRTY_LIST, blob)
    machine.write_u16(CODE0, DIRTY_PTR_B, DIRTY_LIST)
    machine.write_u16(*CS_CODE0_SEG, CODE0)
    machine.write_dgroup(PAGES, struct.pack("<2H", front, back))

    before = machine.ega.rasterise(back)
    machine.run_until("1e7b:0213", "1e7b:029a", max_insns=2_000_000)
    c_restore(cgfx, PAGE_IDS[back], entries)

    assert machine.ega.rasterise(back) != before      # the case really restored something
    assert_pages_equal(machine, cgfx)
