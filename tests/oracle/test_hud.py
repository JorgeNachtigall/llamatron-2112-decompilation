"""W6 HUD: game86 hud_refresh_if_dirty 1000:0309 (level 52b0, lives 51dc, score 52e7 through
the digit / label drawer 50dc / 51c6 and the cell clear 520f) and the control labels 59ab,
against src/game/hud.c through llamatron_game_testlib.

Glyphs are random and written to DGROUP (digits DS:09EE, labels DS:0A8E / 0A9E, 16 bytes
each with random odd bytes, which the drawer ignores). All three pages start random; the HUD
is drawn on all of them. The graphics controller starts with bit mask 0xff and zero latches
(see hud.h for why that is the state that matters).
"""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases, indexed_page

from adapters.cport import bind
from llamatron_harness.ega import HEIGHT, WIDTH
from test_sprites import DRAW_FLAGS, DRAW_PAGE, DrawTarget, Sprite, first_diff

PAGE_IDS = {0x0000: 0, 0x2000: 1, 0x4000: 2}
CODE0 = 0x1000
CS_SAVED_DS = 0x0255
CS_DIRTY_ENABLE = 0x672F
CS_G3_SEG = 0x0261
G3_SEG = 0x8000
DIGITS, LABEL_P1, LABEL_P2 = 0x09EE, 0x0A8E, 0x0A9E
OPTIONS = 0x2CF6
HUD_DIRTY = 0x05A1
LIVES, LEVEL = 0x21A6, 0x21AA
SCORE = (0x4441, 0x4449)
SCROLL_MODE = 0x0442
GC_BIT_MASK = 8


class HudGlyphs(ctypes.Structure):
    _fields_ = [("digits", (ctypes.c_uint8 * 8) * 10), ("player_label", (ctypes.c_uint8 * 8) * 2)]


SHAPE_LOOKUP = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_uint)   # const GfxSprite *


class C:
    def __init__(self, lib):
        bind(lib, "gfx_init", None)()
        self.page = bind(lib, "gfx_page", ctypes.c_void_p, ctypes.c_int)
        self.target = bind(lib, "draw_target_page", DrawTarget, ctypes.c_int)
        self.set_dirty = bind(lib, "draw_set_dirty_recording", None, ctypes.c_bool)
        self.set_glyphs = bind(lib, "hud_set_glyphs", None, ctypes.POINTER(HudGlyphs))
        self.level = bind(lib, "hud_draw_level", None, ctypes.c_int, ctypes.c_int)
        self.lives = bind(lib, "hud_draw_lives", None, ctypes.c_int, ctypes.c_int)
        self.score = bind(lib, "hud_draw_score", None, ctypes.POINTER(ctypes.c_uint8), ctypes.c_int)
        self.labels = bind(lib, "hud_draw_control_labels", None, ctypes.POINTER(DrawTarget),
                           ctypes.c_uint8, SHAPE_LOOKUP)

    def load(self, pid, pixels):
        ctypes.memmove(self.page(pid), pixels, len(pixels))

    def read(self, pid):
        return ctypes.string_at(self.page(pid), WIDTH * HEIGHT)


@pytest.fixture
def c(cgame):
    return C(cgame)


def random_glyph(rnd):
    return [rnd.choice([0, 0xFF, rnd.randrange(256)]) for _ in range(8)]


def setup_pages(machine, c, rnd):
    for off, pid in PAGE_IDS.items():
        px = indexed_page(rnd, rnd.choice(["noise", "blocks"]))
        machine.ega.load_indexed(px, page=off)
        c.load(pid, px)
    machine.write_u16(CODE0, CS_SAVED_DS, machine.dgroup)
    machine.write_dgroup(SCROLL_MODE, b"\0")
    machine.ega.gc[GC_BIT_MASK] = 0xFF
    machine.ega.latch = [0, 0, 0, 0]


def compare_pages(machine, c, what):
    for off, pid in PAGE_IDS.items():
        want = machine.ega.rasterise(off)
        got = c.read(pid)
        assert got == want, f"{what}, page {pid}: " + first_diff(got, want)


def random_score(rnd):
    lead = rnd.choice([0, 0, 1, 3, 6, 7, 8])
    return bytes([0] * lead + [rnd.randrange(10) for _ in range(8 - lead)])[:8]


@pytest.mark.parametrize("case", fuzz_cases(48))
def test_hud_refresh(machine, c, case):
    rnd = case_rng("hud_refresh", case)
    setup_pages(machine, c, rnd)
    glyphs = HudGlyphs()
    for d in range(10):
        rows = random_glyph(rnd)
        glyphs.digits[d][:] = rows
        machine.write_dgroup(DIGITS + 16 * d,
                             bytes(b for r in rows for b in (r, rnd.randrange(256))))
    for i, off in enumerate((LABEL_P1, LABEL_P2)):
        rows = random_glyph(rnd)
        glyphs.player_label[i][:] = rows
        machine.write_dgroup(off, bytes(b for r in rows for b in (r, rnd.randrange(256))))
    c.set_glyphs(ctypes.byref(glyphs))

    player = rnd.randrange(2)
    options = (rnd.randrange(256) & ~0x10) | (0x10 if player else 0)
    lives = rnd.choice([rnd.randrange(-2, 9), rnd.randrange(0, 0x8000)])
    level = rnd.choice([rnd.randrange(0, 40), rnd.randrange(90, 120), rnd.randrange(0, 1000)])
    scores = [random_score(rnd), random_score(rnd)]
    machine.write_dgroup(OPTIONS, bytes([options]))
    machine.write_dgroup(HUD_DIRTY, struct.pack("<H", 1))
    machine.write_dgroup(LIVES, struct.pack("<h", lives))
    machine.write_dgroup(LEVEL, struct.pack("<H", level))
    for p in range(2):
        machine.write_dgroup(SCORE[p], scores[p])

    machine.call("1000:0309", max_insns=2_000_000)
    assert machine.read_dgroup(HUD_DIRTY, 2) == b"\0\0"

    # hud_refresh(game) is these three calls with the GameState fields; test them directly.
    c.level(player, level)
    c.lives(player, lives)
    digits = (ctypes.c_uint8 * 8)(*scores[player])
    c.score(digits, 0x20 if player else 0)
    compare_pages(machine, c, f"lives {lives} level {level} score {scores[player].hex()}")


@pytest.mark.parametrize("case", fuzz_cases(16))
def test_control_labels(machine, c, case):
    rnd = case_rng("hud_labels", case)
    setup_pages(machine, c, rnd)
    shapes, blob, table = {}, bytearray(), {}
    base = 0x40
    for index in range(7, 12):
        # The turn labels (10, 11) are centred at y = 15 and drawn without clipping, so keep
        # them on the page (the real ones are 20 rows; one column allows <= 24 anyway).
        s = Sprite(rnd, rows=rnd.randrange(1, 25) if index >= 10 else None)
        data = s.original_bytes()
        table[index] = base + len(blob)
        blob += data
        shapes[index] = s
    machine.write(G3_SEG, base, bytes(blob))
    for index, off in table.items():
        machine.write_u16(G3_SEG, 2 * index, off)
    machine.write_u16(CODE0, CS_G3_SEG, G3_SEG)
    machine.write_u16(CODE0, CS_DIRTY_ENABLE, 0)
    page = rnd.choice([0x0000, 0x2000, 0x4000])
    machine.write_dgroup(DRAW_PAGE, struct.pack("<H", page))
    machine.write_dgroup(DRAW_FLAGS, b"\x00")     # 59ab sets 0xff itself
    options = rnd.randrange(256)
    machine.write_dgroup(OPTIONS, bytes([options]))
    machine.call("1000:59ab", max_insns=2_000_000)

    c_shapes = {i: s.to_c() for i, s in shapes.items()}

    @SHAPE_LOOKUP
    def lookup(index):
        return ctypes.addressof(c_shapes[index])

    target = c.target(PAGE_IDS[page])
    c.set_dirty(False)
    c.labels(ctypes.byref(target), options, lookup)
    compare_pages(machine, c, f"options {options:#x}")
