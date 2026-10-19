"""EGA plane model (tools/harness/llamatron_harness/ega.py): register semantics, and
an original routine drawing through it (1000:6112, dotted rectangle outline)."""

import random
import struct

import pytest
from generators import case_rng, fuzz_cases, indexed_page, screen_rect

from llamatron_harness.ega import (GC_BIT_MASK, GC_ENABLE_SET_RESET, GC_MODE,
                                   GC_ROTATE, GC_SET_RESET, HEIGHT, WIDTH, EgaPlanes)


def gc(e, reg, value):
    e.port_out(0x3CE, reg | value << 8, 2)


def test_rasterise_roundtrip():
    rnd = random.Random(1)
    pixels = bytes(rnd.randrange(16) for _ in range(WIDTH * HEIGHT))
    e = EgaPlanes()
    e.load_indexed(pixels, page=0x2000)
    assert e.rasterise(0x2000) == pixels
    assert e.rasterise(0x0000) == bytes(WIDTH * HEIGHT)


def test_write_mode0_set_reset_with_bit_mask():
    e = EgaPlanes()
    e.load_indexed(bytes([5]) * 8, width=8, height=1)
    gc(e, GC_SET_RESET, 0x0A)
    gc(e, GC_ENABLE_SET_RESET, 0x0F)
    gc(e, GC_BIT_MASK, 0x0F)                 # right half of the byte only
    e.read(0)                                # load latches
    e.write(0, 0x00)                         # data ignored: all planes from set/reset
    assert e.rasterise(width=8, height=1) == bytes([5, 5, 5, 5, 10, 10, 10, 10])


def test_write_mode0_rotate_and_xor():
    e = EgaPlanes()
    e.load_indexed(bytes([0xF]) * 8, width=8, height=1)
    gc(e, GC_ROTATE, 1 | 3 << 3)             # rotate right 1, XOR with latch
    e.read(0)
    e.write(0, 0x01)                         # rotated -> 0x80: leftmost pixel flips
    assert e.rasterise(width=8, height=1) == bytes([0] + [0xF] * 7)


def test_write_mode1_copies_latches():
    e = EgaPlanes()
    e.load_indexed(bytes(range(8)), width=8, height=1, page=0x4000)
    gc(e, GC_MODE, 1)
    e.read(0x4000)
    e.write(0x0000, 0x00)
    assert e.rasterise(0x0000, width=8, height=1) == bytes(range(8))


def test_write_mode2_colour_from_data():
    e = EgaPlanes()
    gc(e, GC_MODE, 2)
    gc(e, GC_BIT_MASK, 0x81)
    e.read(0)
    e.write(0, 0x0C)
    assert e.rasterise(width=8, height=1) == bytes([12, 0, 0, 0, 0, 0, 0, 12])


def test_read_mode1_colour_compare():
    e = EgaPlanes()
    e.load_indexed(bytes([3, 7, 3, 3, 0, 3, 1, 3]), width=8, height=1)
    gc(e, GC_MODE, 0x08)
    gc(e, 2, 3)                              # colour compare = 3, don't care = all
    assert e.read(0) == 0b10110101


def test_map_mask_limits_planes():
    e = EgaPlanes()
    e.port_out(0x3C4, 0x02 | 0x05 << 8, 2)   # planes 0 and 2
    e.read(0)
    e.write(0, 0xFF)
    assert e.rasterise(width=8, height=1) == bytes([5] * 8)


# ---- original code through the model ---------------------------------------

RECT = 0x444          # x0, y0, x1, y1 (u16 each), DS-relative
PAGE_PTR = 0x39D7     # VRAM offset of the page being drawn


def ref_rect_outline(x0, y0, x1, y1, colour, page_pixels):
    """game86 1000:6112: 2x2-pixel dots every 2 px along the four edges
    (dot at x covers columns x&~1..x|1 and rows y..y+1), in set/reset colour."""
    out = bytearray(page_pixels)

    def dot(x, y):
        for yy in (y, y + 1):
            for xx in (x & ~1, x | 1):
                out[yy * WIDTH + xx] = colour

    for x in range(x0, x1 + 1, 2):
        dot(x, y0)
        dot(x, y1)
    for y in range(y0, y1 + 1, 2):
        dot(x0, y)
        dot(x1, y)
    return bytes(out)


@pytest.mark.parametrize("case", fuzz_cases(12))
def test_rect_outline_6112(machine, case):
    rnd = case_rng("rect_outline", case)
    x0, y0, x1, y1 = screen_rect(rnd, margin_x=2, margin_y=2)   # dots are 2x2
    colour = rnd.randrange(16)
    page = rnd.choice([0x0000, 0x2000])
    background = indexed_page(rnd, rnd.choice(["noise", "blocks", "blank"]))
    machine.ega.load_indexed(background, page=page)
    machine.write_dgroup(RECT, struct.pack("<4H", x0, y0, x1, y1))
    machine.write_dgroup(PAGE_PTR, struct.pack("<H", page))

    res = machine.call("1000:6112", regs={"bp": colour})

    assert machine.ega.rasterise(page) == ref_rect_outline(x0, y0, x1, y1, colour, background)
    other = 0x2000 if page == 0 else 0x0000
    assert machine.ega.rasterise(other) == bytes(WIDTH * HEIGHT)
    assert machine.ega.gc[GC_BIT_MASK] == 0xFF          # restored on exit
    assert machine.ega.gc[GC_ENABLE_SET_RESET] == 0x00
    assert not res.dgroup_diff                          # DI is the only other output


PAGES = 0x39D3        # u16 displayed page, u16 back page (VRAM offsets)
CLEAN = 0x4000        # clean background page


@pytest.mark.parametrize("entry,src,dst", [
    ("1000:62e8", "back", "clean"),      # save the back page as clean background
    ("1000:62f6", "clean", "back"),      # restore the back page from the background
    ("1000:6304", "back", "front"),      # copy back page onto the displayed page
])
@pytest.mark.parametrize("swap", [False, True])
def test_page_copy(machine, entry, src, dst, swap):
    """game86 1000:62e8 / 62f6 / 6304: whole-page copies, one plane at a time."""
    front, back = (0x2000, 0x0000) if swap else (0x0000, 0x2000)
    where = {"front": front, "back": back, "clean": CLEAN}
    rnd = case_rng(f"page_copy:{entry}", int(swap))
    content = {}
    for name, off in where.items():
        content[name] = indexed_page(rnd, "blocks")
        machine.ega.load_indexed(content[name], page=off)
    machine.write_dgroup(PAGES, struct.pack("<2H", front, back))

    machine.call(entry, max_insns=200_000)

    for name, off in where.items():
        want = content[src] if name == dst else content[name]
        assert machine.ega.rasterise(off) == want, name
