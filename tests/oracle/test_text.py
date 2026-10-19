"""W6 text engine: glyph blitter 1000:559f, text_print 1000:536f (word wrap, escapes),
centred text 1000:5537, against src/game/text.c through llamatron_game_testlib.

The font is random (generated per case) and loaded both into DGROUP in the original's
layout and into a C GfxFont, so the tests need no game data and cover any advance, glyph
height and bit pattern. Most cases draw into the scroller's canvas (all of the 64 KB plane,
g_draw_page = 0) and compare the whole plane, which covers glyphs running past the bottom
of a page and the cell drift of clipped rows.
"""

import ctypes
import random
import struct

import pytest
from generators import case_rng, fuzz_cases, indexed_page

from adapters.cport import bind

# ---- original layout ---------------------------------------------------------------
FONT = 0x39EE              # DGROUP: u16 line height, then u16 offsets[128] (from FONT)
FONT_TABLE = FONT + 2
GLYPH_AREA = 0x4900        # test-only home for the glyph records (entity table area)
STRING_AREA = 0x6200
TEXT_X, TEXT_Y, TEXT_MARGIN = 0x0B74, 0x0B76, 0x0B78
SCROLL_MODE = 0x0442
DRAW_PAGE = 0x39D7
CODE0 = 0x1000
CS_COLOUR = 0x559C         # map mask value of the colour pass
CS_CLEAR = 0x559E          # map mask of the clear pass (0 = none)
CS_OPAQUE = 0x5362

CELLS = 0x10000
CANVAS_ROWS = 1638          # rows in tbl_row_offset
TEXT_LINE = 21
CANVAS_PIXELS = CELLS * 8


# ---- C structs ----------------------------------------------------------------------
class GfxGlyph(ctypes.Structure):
    _fields_ = [("advance", ctypes.c_uint8), ("rows", ctypes.c_uint8),
                ("bits", ctypes.POINTER(ctypes.c_uint16))]


class GfxFont(ctypes.Structure):
    _fields_ = [("height", ctypes.c_uint8), ("first_char", ctypes.c_uint8),
                ("glyph_count", ctypes.c_uint16), ("glyphs", ctypes.POINTER(GfxGlyph))]


class TextState(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int16), ("y", ctypes.c_int16), ("margin", ctypes.c_int16),
                ("color", ctypes.c_uint8), ("clear_planes", ctypes.c_uint8),
                ("opaque", ctypes.c_bool)]


class DrawTarget(ctypes.Structure):
    _fields_ = [("pixels", ctypes.c_void_p), ("cell_count", ctypes.c_int32),
                ("wraps", ctypes.c_bool)]


# ---- planes <-> linear pixels ------------------------------------------------------
_SPREAD = [sum(((b >> (7 - i)) & 1) << (8 * i) for i in range(8)) for b in range(256)]


def planes_to_pixels(planes, start=0, cells=CELLS):
    p0, p1, p2, p3 = planes
    out = bytearray(cells * 8)
    sp = _SPREAD
    for c in range(cells):
        a = start + c
        v = sp[p0[a]] | sp[p1[a]] << 1 | sp[p2[a]] << 2 | sp[p3[a]] << 3
        out[c * 8:c * 8 + 8] = v.to_bytes(8, "little")
    return bytes(out)


def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return f"first difference at pixel {i} (cell {i // 8}, row {i // 320}, x {i % 320})"
    return "lengths differ"


# ---- random fonts and strings ------------------------------------------------------
class Font:
    def __init__(self, rnd):
        self.glyphs = []          # (advance, [row bits])
        for code in range(128):
            advance = rnd.choice([rnd.randrange(1, 17), rnd.randrange(4, 12), 0])
            rows = rnd.randrange(1, 15)
            density = rnd.choice([0.2, 0.5, 0.9])
            bits = [sum(1 << b for b in range(16) if rnd.random() < density) for _ in range(rows)]
            self.glyphs.append((advance, bits))

    def load_original(self, machine):
        table = []
        blob = bytearray()
        for advance, bits in self.glyphs:
            table.append((GLYPH_AREA + len(blob) - FONT) & 0xFFFF)
            blob += struct.pack("<3H", 4, advance, len(bits) - 1)
            blob += b"".join(struct.pack(">H", r) for r in bits)   # byte 0 = left 8 px
        machine.write_dgroup(FONT, struct.pack("<H", 14) + struct.pack("<128H", *table))
        machine.write_dgroup(GLYPH_AREA, bytes(blob))

    def to_c(self, lib):
        self._rows = [(ctypes.c_uint16 * len(bits))(*bits) for _, bits in self.glyphs]
        self._glyphs = (GfxGlyph * 128)()
        for i, (advance, bits) in enumerate(self.glyphs):
            self._glyphs[i] = GfxGlyph(advance, len(bits),
                                       ctypes.cast(self._rows[i], ctypes.POINTER(ctypes.c_uint16)))
        self._font = GfxFont(14, 0, 128, self._glyphs)
        bind(lib, "text_set_font", None, ctypes.POINTER(GfxFont))(ctypes.byref(self._font))


PRINTABLE = [c for c in range(33, 127) if c != 0x5C]


def random_text(rnd, allow_moves=True, tokens=None):
    out = bytearray()
    for _ in range(tokens or rnd.randrange(1, 30)):
        kind = rnd.random()
        if kind < 0.45:
            out += bytes(rnd.choice(PRINTABLE) for _ in range(rnd.randrange(1, 9)))
        elif kind < 0.65:
            out += b" " * rnd.randrange(1, 3)
        elif kind < 0.70:
            out += b"\0"
        elif kind < 0.78:
            out += b"\\c" + bytes([rnd.randrange(256)])
        elif kind < 0.84:
            out += b"\\r"
        elif kind < 0.88:
            out += b"\\q" + bytes(rnd.choice(PRINTABLE + [0x20]) for _ in range(rnd.randrange(0, 12))) + b"\0"
        elif kind < 0.92:
            out += b"\\g" + bytes([rnd.randrange(256), rnd.randrange(256)])
        elif allow_moves and kind < 0.98:
            out += b"\\" + bytes([rnd.choice(b"udli")])
        else:
            out += b"\\" + bytes([rnd.choice(b"zab")])          # unknown escapes: ignored
    return bytes(out) + b"\\e"


def max_line_breaks(text):
    """Upper bound of how far down (in lines) printing `text` can move the pen: two lines per
    byte (a word wider than the line wraps before a glyph and again after it), \\g adds up
    to 6 more."""
    return 2 * len(text) + 6 * text.count(b"\\g")


def fits_canvas(text):
    return CANVAS_ROWS - 16 - TEXT_LINE * max_line_breaks(text) > 0


def start_y(rnd, text):
    """A start row keeping every line inside the original's 1638-row table (DS:2D07): rows
    past it are not a screen position (the original reads unrelated words as row offsets)."""
    last = CANVAS_ROWS - 16 - TEXT_LINE * max_line_breaks(text)
    assert last > 0, "text too long for the canvas"
    return rnd.choice([rnd.randrange(-20, last), rnd.randrange(-20, min(200, last)), -5])


# ---- the two sides ------------------------------------------------------------------
def setup_original(machine, font, text, x, y, colour, opaque, draw_page, wrapped):
    font.load_original(machine)
    machine.write_dgroup(STRING_AREA, text)
    machine.write_dgroup(TEXT_X, struct.pack("<2h", x, y))
    machine.write_dgroup(SCROLL_MODE, bytes([2 if wrapped else 0]))
    machine.write_dgroup(DRAW_PAGE, struct.pack("<H", draw_page))
    machine.write(CODE0, CS_COLOUR, bytes([colour]))
    machine.write(CODE0, CS_CLEAR, bytes([(~colour & 15) if opaque else 0]))
    machine.write(CODE0, CS_OPAQUE, bytes([1 if opaque else 0]))


def c_state(x, y, colour, opaque):
    return TextState(x, y, 0, colour, (~colour & 15) if opaque else 0, opaque)


@pytest.fixture
def ctext(cgame):
    bind(cgame, "gfx_init", None)()
    return cgame


def run_canvas_case(machine, ctext, rnd, text, x, y, opaque, wrapped):
    font = Font(rnd)
    colour = rnd.randrange(16)
    for p in machine.ega.planes:
        p[:] = bytes(rnd.randrange(256) for _ in range(CELLS))
    canvas = ctypes.create_string_buffer(planes_to_pixels(machine.ega.planes), CANVAS_PIXELS)
    setup_original(machine, font, text, x, y, colour, opaque, 0, wrapped)

    res = machine.call("1000:536f", regs={"si": STRING_AREA}, max_insns=20_000_000)

    font.to_c(ctext)
    bind(ctext, "draw_set_canvas_wrapped", None, ctypes.c_bool)(wrapped)
    state = c_state(x, y, colour, opaque)
    target = DrawTarget(ctypes.cast(canvas, ctypes.c_void_p), CELLS, True)
    end = bind(ctext, "text_print", ctypes.c_void_p, ctypes.POINTER(DrawTarget),
               ctypes.POINTER(TextState), ctypes.c_char_p)
    text_buf = ctypes.create_string_buffer(text, len(text))
    ret = end(ctypes.byref(target), ctypes.byref(state), text_buf)
    bind(ctext, "draw_set_canvas_wrapped", None, ctypes.c_bool)(False)

    want = planes_to_pixels(machine.ega.planes)
    got = canvas.raw
    assert got == want, first_diff(got, want)
    tx, ty, margin = struct.unpack("<3h", machine.read_dgroup(TEXT_X, 6))
    assert (state.x, state.y, state.margin) == (tx, ty, margin)
    assert ret - ctypes.cast(text_buf, ctypes.c_void_p).value == res.regs["si"] - STRING_AREA
    assert state.color == machine.read(CODE0, CS_COLOUR, 1)[0]
    assert state.clear_planes == machine.read(CODE0, CS_CLEAR, 1)[0]


@pytest.mark.parametrize("case", fuzz_cases(24))
def test_text_print_canvas(machine, ctext, case):
    """Anywhere on the canvas, including near and past the screen edges."""
    rnd = case_rng("text_print_canvas", case)
    x = rnd.choice([rnd.randrange(-20, 330), rnd.randrange(0, 300), -16, -8, 312, 318])
    text = random_text(rnd, tokens=rnd.randrange(1, 8))
    while not fits_canvas(text):
        text = random_text(rnd, tokens=rnd.randrange(1, 8))
    y = start_y(rnd, text)
    run_canvas_case(machine, ctext, rnd, text, x, y,
                    opaque=rnd.random() < 0.3, wrapped=rnd.random() < 0.25)


@pytest.mark.parametrize("case", fuzz_cases(24))
def test_glyph_clipping(machine, ctext, case):
    """Single glyphs placed exactly around the left/right edges and the top."""
    rnd = case_rng("glyph_clip", case)
    x = rnd.choice(list(range(-17, 2)) + list(range(300, 321)))
    y = rnd.choice([rnd.randrange(-14, 3), rnd.randrange(0, 200)])
    ch = rnd.choice(PRINTABLE)
    run_canvas_case(machine, ctext, rnd, bytes([ch]) + b"\\e", x, y,
                    opaque=rnd.random() < 0.5, wrapped=False)


@pytest.mark.parametrize("case", fuzz_cases(12))
def test_text_print_page(machine, ctext, case):
    """Drawing into one of the three pages (text kept on the page)."""
    rnd = case_rng("text_print_page", case)
    page_off = rnd.choice([0x0000, 0x2000, 0x4000])
    page_id = {0x0000: 0, 0x2000: 1, 0x4000: 2}[page_off]
    font = Font(rnd)
    colour = rnd.randrange(16)
    opaque = rnd.random() < 0.3
    words = bytes(rnd.choice(PRINTABLE) for _ in range(rnd.randrange(1, 6)))
    text = b"\\c" + bytes([colour]) + words + b" " + words + b"\\e"
    x, y = rnd.randrange(0, 300), rnd.randrange(0, 160)
    background = indexed_page(rnd, "noise")
    machine.ega.load_indexed(background, page=page_off)
    setup_original(machine, font, text, x, y, colour, opaque, page_off, False)

    machine.call("1000:536f", regs={"si": STRING_AREA}, max_insns=5_000_000)

    font.to_c(ctext)
    page = bind(ctext, "gfx_page", ctypes.c_void_p, ctypes.c_int)(page_id)
    ctypes.memmove(page, background, len(background))
    target = bind(ctext, "draw_target_page", DrawTarget, ctypes.c_int)(page_id)
    state = c_state(x, y, colour, opaque)
    bind(ctext, "text_print", ctypes.c_void_p, ctypes.POINTER(DrawTarget),
         ctypes.POINTER(TextState), ctypes.c_char_p)(ctypes.byref(target), ctypes.byref(state), text)

    want = machine.ega.rasterise(page_off)
    got = ctypes.string_at(page, len(want))
    assert got == want, first_diff(got, want)
