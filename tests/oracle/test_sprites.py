"""W6 sprite blitters: game86 1000:8cbb (one column aligned/shifted, edge paths 8dc9 / aa29,
fast tables 8e71 / abd1), 63d2 / 6566 (several columns) and the sheared draw e135, against
src/game/draw_sprite.c through llamatron_game_testlib.

Sprites are random, in the bank's layout (header {3, columns-1, rows-1, nominal width, 0, 0},
then per row and column: mask byte pair, then planes 0..3 as byte pairs) at a scratch
segment, and as a GfxSprite on the C side. Plane bits under transparent pixels are random,
so the tests also prove they are ignored. After each draw the dirty rectangles recorded by
both sides are restored from a random background (the original's restore pass
1e7b:0213..0299 vs gfx_restore_dirty) and the pages compared again.
"""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases, indexed_page

from adapters.cport import bind
from llamatron_harness.ega import HEIGHT, WIDTH

PAGE_IDS = {0x0000: 0, 0x2000: 1, 0x4000: 2}
SPRITE_SEG = 0x8000            # scratch segment for the test sprite (free memory)
CODE0 = 0x1000
CS_SAVED_DS = 0x0255
CS_DIRTY_ENABLE = 0x672F
CS_DIRTY_TMP = 0x6731          # write position in the current dirty list
CS_DIRTY_PTR_B = 0x6735        # list restored by the frame loop
CS_CODE0_SEG = (0x1E7B, 0x013D)
DIRTY_LIST = 0x6737
LIST_END = 0xFE0C
PAGES = 0x39D3                 # front, back
DRAW_PAGE = 0x39D7
DRAW_FLAGS, DRAW_SCALE, DRAW_MIRROR = 0x39D9, 0x39DA, 0x39DC
SCROLL_MODE = 0x0442
TRANSPARENT = 0xFF


class GfxSprite(ctypes.Structure):
    _fields_ = [("width", ctypes.c_int16), ("height", ctypes.c_int16),
                ("hotspot_x", ctypes.c_int16), ("hotspot_y", ctypes.c_int16),
                ("pixels", ctypes.c_void_p), ("mask", ctypes.c_void_p)]


class DrawTarget(ctypes.Structure):
    _fields_ = [("pixels", ctypes.c_void_p), ("cell_count", ctypes.c_int32),
                ("wraps", ctypes.c_bool), ("page", ctypes.c_int)]


class Sprite:
    def __init__(self, rnd, columns=None, rows=None):
        self.columns = columns or rnd.choice([1, 1, 1, 2, 3])
        # One-column draws dispatch on the row count through 25-entry tables (1000:8e71,
        # abd1), so the original handles at most 24 rows there (real frames: <= 23).
        tallest = 24 if self.columns == 1 else 40
        self.rows = rows or rnd.choice([rnd.randrange(1, tallest + 1), rnd.randrange(1, 8)])
        self.width = 16 * self.columns
        self.nominal = rnd.choice([self.width, rnd.randrange(1, self.width + 1)])
        density = rnd.choice([0.3, 0.7, 1.0])
        self.pixels = bytearray()
        for _ in range(self.rows * self.width):
            self.pixels.append(rnd.randrange(16) if rnd.random() < density else TRANSPARENT)
        self.garbage = [rnd.randrange(16) for _ in range(len(self.pixels))]

    def original_bytes(self):
        out = bytearray(struct.pack("<6H", 3, self.columns - 1, self.rows - 1, self.nominal, 0, 0))
        for r in range(self.rows):
            for c in range(self.columns):
                cell = bytearray(10)
                for half in range(2):
                    for i in range(8):
                        k = r * self.width + c * 16 + half * 8 + i
                        p = self.pixels[k]
                        colour = self.garbage[k] if p == TRANSPARENT else p
                        if p != TRANSPARENT:
                            cell[half] |= 0x80 >> i
                        for plane in range(4):
                            if colour >> plane & 1:
                                cell[2 + plane * 2 + half] |= 0x80 >> i
                out += cell
        return bytes(out)

    def to_c(self):
        self._buf = ctypes.create_string_buffer(bytes(self.pixels), len(self.pixels))
        return GfxSprite(self.width, self.rows, self.nominal >> 1, (self.rows - 1) >> 1,
                         ctypes.cast(self._buf, ctypes.c_void_p), None)


class C:
    def __init__(self, lib):
        self.lib = lib
        bind(lib, "gfx_init", None)()
        self.page = bind(lib, "gfx_page", ctypes.c_void_p, ctypes.c_int)
        self.flip = bind(lib, "gfx_flip", None)
        self.display = bind(lib, "gfx_display_page_id", ctypes.c_int)
        self.target = bind(lib, "draw_target_page", DrawTarget, ctypes.c_int)
        self.set_dirty = bind(lib, "draw_set_dirty_recording", None, ctypes.c_bool)
        self.draw = bind(lib, "draw_sprite", None, ctypes.POINTER(DrawTarget),
                         ctypes.POINTER(GfxSprite), ctypes.c_int, ctypes.c_int)
        self.draw_sheared = bind(lib, "draw_sprite_sheared", None, ctypes.POINTER(DrawTarget),
                                 ctypes.POINTER(GfxSprite), ctypes.c_int, ctypes.c_int,
                                 ctypes.c_int, ctypes.c_int)
        self.restore_dirty = bind(lib, "gfx_restore_dirty", None, ctypes.c_int)

    def load(self, pid, pixels):
        ctypes.memmove(self.page(pid), pixels, len(pixels))

    def read(self, pid):
        return ctypes.string_at(self.page(pid), WIDTH * HEIGHT)


@pytest.fixture
def c(cgame):
    return C(cgame)


def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return f"first difference at x={i % WIDTH} y={i // WIDTH}: C {x} vs original {y}"
    return "lengths differ"


def run_case(machine, c, rnd, sprite, x, y, dirty, sheared=None):
    """sheared = (row_step, mirror) or None for the normal draw."""
    page = rnd.choice([0x0000, 0x2000])
    back = 0x2000 - page                    # the drawn page is the front (draw) page
    pages = {off: indexed_page(rnd, rnd.choice(["noise", "blocks"])) for off in PAGE_IDS}
    for off, px in pages.items():
        machine.ega.load_indexed(px, page=off)
        c.load(PAGE_IDS[off], px)
    if c.display() != PAGE_IDS[back]:
        c.flip()

    machine.write(SPRITE_SEG, 0, sprite.original_bytes())
    machine.write_u16(CODE0, CS_SAVED_DS, machine.dgroup)
    machine.write_u16(CODE0, CS_DIRTY_ENABLE, 1 if dirty else 0)
    machine.write_u16(CODE0, CS_DIRTY_TMP, DIRTY_LIST)
    machine.write_u16(CODE0, DIRTY_LIST, LIST_END)
    machine.write_dgroup(DRAW_PAGE, struct.pack("<H", page))
    machine.write_dgroup(SCROLL_MODE, b"\0")
    if sheared:
        step, mirror = sheared
        machine.write_dgroup(DRAW_FLAGS, b"\x00")
        machine.write_dgroup(DRAW_SCALE, struct.pack("<H", step & 0xFFFF))
        machine.write_dgroup(DRAW_MIRROR, struct.pack("<H", mirror & 0xFFFF))
    else:
        machine.write_dgroup(DRAW_FLAGS, b"\xff")

    machine.call("1000:8cbb", regs={"ax": x & 0xFFFF, "bx": y & 0xFFFF, "si": 0,
                                    "es": SPRITE_SEG}, max_insns=2_000_000)

    gs = sprite.to_c()
    target = c.target(PAGE_IDS[page])
    c.set_dirty(dirty)
    if sheared:
        step, mirror = sheared
        shear = 0 if mirror == 0 else (step if mirror > 0 else -step)
        c.draw_sheared(ctypes.byref(target), ctypes.byref(gs), x, y, step, shear)
    else:
        c.draw(ctypes.byref(target), ctypes.byref(gs), x, y)
    c.set_dirty(False)

    want = machine.ega.rasterise(page)
    got = c.read(PAGE_IDS[page])
    assert got == want, "draw: " + first_diff(got, want)

    # Erase with the recorded dirty rectangles.
    machine.write_u16(CODE0, CS_DIRTY_PTR_B, DIRTY_LIST)
    machine.write_u16(*CS_CODE0_SEG, CODE0)
    machine.write_dgroup(PAGES, struct.pack("<2H", back, page))
    machine.run_until("1e7b:0213", "1e7b:029a", max_insns=2_000_000)
    c.restore_dirty(PAGE_IDS[page])
    want = machine.ega.rasterise(page)
    got = c.read(PAGE_IDS[page])
    assert got == want, "restore: " + first_diff(got, want)


def position(rnd, sprite):
    return (rnd.choice([rnd.randrange(-60, 380), rnd.randrange(0, 320), rnd.randrange(-24, 24),
                        rnd.randrange(296, 344)]),
            rnd.choice([rnd.randrange(-40, 240), rnd.randrange(0, 200), rnd.randrange(-20, 10),
                        rnd.randrange(190, 220)]))


@pytest.mark.parametrize("case", fuzz_cases(48))
def test_draw_sprite_recording(machine, c, case):
    """Dirty recording on: clipped to the screen, rectangles recorded."""
    rnd = case_rng("sprite_recording", case)
    sprite = Sprite(rnd)
    x, y = position(rnd, sprite)
    run_case(machine, c, rnd, sprite, x, y, dirty=True)


@pytest.mark.parametrize("case", fuzz_cases(24))
def test_draw_sprite_edges(machine, c, case):
    """Every x around both edges, including the aligned x = -16 quirk."""
    rnd = case_rng("sprite_edges", case)
    sprite = Sprite(rnd)
    left = rnd.random() < 0.5
    hx = sprite.nominal >> 1
    x = (rnd.randrange(-sprite.width - 2, 2) if left else rnd.randrange(300, 322)) + hx
    y = rnd.randrange(0, 200)
    run_case(machine, c, rnd, sprite, x, y, dirty=True)


@pytest.mark.parametrize("case", fuzz_cases(24))
def test_draw_sprite_not_recording(machine, c, case):
    """Dirty recording off: no clipping in y, nothing recorded (rows kept on the page)."""
    rnd = case_rng("sprite_not_recording", case)
    sprite = Sprite(rnd)
    hy = (sprite.rows - 1) >> 1
    # Left of x = -16 an unaligned one-column draw without recording corrupts the
    # original's stack (1000:ab6d pops its saved DI); recording always rejects it first.
    x = rnd.randrange(-16, 360) + (sprite.nominal >> 1)
    y = rnd.randrange(0, 200 - sprite.rows + 1) + hy
    run_case(machine, c, rnd, sprite, x, y, dirty=False)


@pytest.mark.parametrize("case", fuzz_cases(36))
def test_draw_sprite_sheared(machine, c, case):
    rnd = case_rng("sprite_sheared", case)
    sprite = Sprite(rnd, columns=rnd.choice([1, 1, 1, 2]))
    step = rnd.choice([0, 1, 1, 2, 3, rnd.randrange(0, 6)])
    mirror = rnd.choice([0, 1, -1, 0x1234, -0x1234])
    x, y = position(rnd, sprite)
    dirty = rnd.random() < 0.7
    if not dirty:
        # Keep the whole draw on the page (rows above it would index before the row table)
        # and right of x = -16 (see test_draw_sprite_not_recording).
        half = (sprite.rows - 1) >> 1
        y = half * step + rnd.randrange(0, 200 - sprite.rows + 1)
        x = rnd.randrange(-16, 330) + (sprite.nominal >> 1) + abs(half * (step if mirror else 0))
        if sprite.columns == 1 or y - half * step + sprite.rows > 200:
            y = min(y, 120)
    run_case(machine, c, rnd, sprite, x, y, dirty=dirty, sheared=(step, mirror))


@pytest.mark.parametrize("left", range(-18, 2))
@pytest.mark.parametrize("dirty", [True, False])
def test_narrow_left_edge(machine, c, left, dirty):
    """Every left edge from -18 to 1 for a one-column sprite; at -16 the original still
    draws the right half into the row's first 8 pixels (1000:8dc9 only tests the right
    edge for it)."""
    rnd = case_rng(f"narrow_left_edge:{int(dirty)}", left)
    sprite = Sprite(rnd, columns=1, rows=rnd.randrange(1, 25))
    sprite.nominal = 16
    if not dirty and left < -16:
        pytest.skip("unaligned left of -16 without recording breaks the original (see above)")
    run_case(machine, c, rnd, sprite, left + 8, 50 + ((sprite.rows - 1) >> 1), dirty=dirty)
