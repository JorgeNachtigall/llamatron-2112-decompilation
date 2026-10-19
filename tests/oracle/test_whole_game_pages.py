"""tests/oracle/whole_game_pages.py on its own: its fast rasteriser against EgaPlanes, and
PageComparer finding planted differences (pixels, shown page, the scroller's canvas) between
an EGA plane model and the port's pages loaded with the same pictures."""

import ctypes
import random

import pytest

from llamatron_harness.ega import EgaPlanes
from whole_game_pages import PAGES, PIXELS, PageComparer, rasterise


def picture(rnd):
    return bytes(rnd.randrange(16) for _ in range(PIXELS))


@pytest.fixture
def setup(cgame):
    rnd = random.Random(1)
    ega = EgaPlanes()
    cmp = PageComparer(cgame)
    flip = cgame.gfx_flip
    pics = {}
    for offset, page_id in PAGES.items():
        pics[page_id] = picture(rnd)
        ega.load_indexed(pics[page_id], page=offset)
        ctypes.memmove(cmp._page(page_id), pics[page_id], PIXELS)
    if cmp._display_id() != 0:
        flip()
    ega.crtc[0x0C] = ega.crtc[0x0D] = 0
    return rnd, ega, cmp, cgame


@pytest.mark.parametrize("offset", [0x0000, 0x2000, 0x4000, 0x1450, 0xF000])
def test_rasterise(offset):
    rnd = random.Random(offset)
    ega = EgaPlanes()
    for p in range(4):
        ega.planes[p][:] = rnd.randbytes(0x10000)
    if offset + 8000 <= 0x10000:
        assert rasterise(ega.planes, offset) == ega.rasterise(offset)
    else:   # wraps: the original's CRTC reads on from plane offset 0
        got = rasterise(ega.planes, offset)
        for p in range(4):
            ega.planes[p][:] = ega.planes[p][offset:] + ega.planes[p][:offset]
        assert got == ega.rasterise(0)


def test_same(setup):
    _, ega, cmp, _ = setup
    assert cmp.compare(ega, 1, 1) is None


@pytest.mark.parametrize("page_id", [0, 1, 2])
def test_planted_pixels(setup, page_id):
    _, ega, cmp, _ = setup
    px = bytearray(cmp.port_page(page_id))
    for x, y in ((17, 30), (200, 150)):
        px[y * 320 + x] ^= 5
    ctypes.memmove(cmp._page(page_id), bytes(px), PIXELS)
    d = cmp.compare(ega, 7, 9)
    assert d is not None
    assert d.kind == ("shown pixels" if page_id == 0 else "page pixels")
    assert (d.bbox, d.count, d.first[:2]) == ((17, 30, 200, 150), 2, (17, 30))


def test_shown_page(setup):
    _, ega, cmp, lib = setup
    ega.crtc[0x0C] = 0x20                     # the original shows page 1, the port page 0
    d = cmp.compare(ega, 1, 1)
    assert d is not None and d.kind == "shown pixels"
    ctypes.memmove(cmp._page(0), cmp.port_page(1), PIXELS)
    d = cmp.compare(ega, 1, 1)                # same pixels, other page
    assert d is not None and d.kind == "shown page"


def test_canvas_start_compares_only_the_shown_picture(setup):
    rnd, ega, cmp, _ = setup
    ega.crtc[0x0C], ega.crtc[0x0D] = 0x14, 0x50
    ctypes.memmove(cmp._page(0), rasterise(ega.planes, 0x1450), PIXELS)
    ctypes.memmove(cmp._page(2), picture(rnd), PIXELS)   # not compared on a canvas frame
    assert cmp.compare(ega, 1, 1) is None
