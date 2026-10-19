"""W6 wave-start, warp and game-over drawing: game86 1000:055c..05cf (the new wave's
background), 0640..0649 (the warp banner) and 08ee..0910 (the game-over picture) against
wave_draw_background / wave_draw_warp_banner / game_over_draw (src/game/draw_play.c) through
llamatron_game_testlib, with the real texts, font, sprite bank and g3 shapes.

The original loads f.cmp and g3.cmp at start-up, so the test writes the port's extracted font
(at test_text's glyph area) and g3 shapes (test_play_draw.g3_image) into the image first.
Compared: all three pages, the DGROUP state (the text pen) and the dirty-recording switch.
"""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases

from adapters.cport import bind
from adapters.gamestate import DgroupMap
from test_anim import Port, compare_pages, setup_pages
from test_play_draw import CS_G3_SEG, G3_SEG, g3_image  # noqa: F401  (fixture)
from test_sprites import CODE0, CS_DIRTY_ENABLE, DRAW_PAGE, PAGES, SCROLL_MODE
from test_text import CS_CLEAR, CS_COLOUR, CS_OPAQUE, FONT, GLYPH_AREA, Font, GfxFont

WAVE_TYPE, LEVEL, DEMO_MODE = 0x21B0, 0x21AA, 0x06C4
LEVELS = 100
LEVEL_TITLES = 0x2340


class GameFont(Font):
    """The port's extracted font, for the original's font table (test_text.Font layout)."""

    def __init__(self, font):
        self.glyphs = []
        for i in range(font.glyph_count):
            g = font.glyphs[i]
            self.glyphs.append((g.advance, [g.bits[r] for r in range(g.rows)] or [0]))
        self.size = sum(6 + 2 * len(bits) for _, bits in self.glyphs)


@pytest.fixture(scope="module")
def dg(cgame):
    return DgroupMap(cgame)


@pytest.fixture(scope="module")
def port(cgame):
    p = Port(cgame)
    bind(cgame, "text_use_game_assets", None)()
    p.font = GameFont(bind(cgame, "asset_font_gfx", ctypes.POINTER(GfxFont))().contents)
    p.background = bind(cgame, "wave_draw_background", None, ctypes.c_void_p)
    p.warp = bind(cgame, "wave_draw_warp_banner", None, ctypes.c_void_p)
    p.game_over = bind(cgame, "game_over_draw", None)
    p.recording = bind(cgame, "draw_dirty_recording", ctypes.c_bool)
    return p


def run(machine, dg, port, rnd, start, stop, call, edit=None):
    page, back = setup_pages(machine, port, rnd)
    img = bytearray(machine.read_dgroup(0, 0x10000))
    struct.pack_into("<HH", img, PAGES, page, back)
    struct.pack_into("<H", img, DRAW_PAGE, page)
    img[SCROLL_MODE] = 0
    img[0x39D9] = rnd.randrange(256)
    struct.pack_into("<3h", img, 0x0B74, rnd.randrange(-50, 300), rnd.randrange(-50, 250),
                     rnd.randrange(0, 300))
    if edit:
        edit(img)
    image = dg.canonical(bytes(img))
    recording = rnd.random() < 0.5

    machine.write_dgroup(0, image)
    port.font.load_original(machine)
    machine.write(G3_SEG, 0, port.g3)
    machine.write_u16(CODE0, CS_G3_SEG, G3_SEG)
    # The pen's colour lives in CS (unmapped): both sides start at the image's colour 0.
    machine.write(CODE0, CS_COLOUR, b"\0")
    machine.write(CODE0, CS_CLEAR, b"\0")
    machine.write(CODE0, CS_OPAQUE, b"\0")
    machine.write_u16(CODE0, CS_DIRTY_ENABLE, 1 if recording else 0)
    machine.run_until(start, stop, max_insns=20_000_000)
    want = bytearray(machine.read_dgroup(0, 0x10000))
    # the test's font table and glyphs are not game state
    want[FONT:FONT + 2 + 256] = image[FONT:FONT + 2 + 256]
    want[GLYPH_AREA:GLYPH_AREA + port.font.size] = image[GLYPH_AREA:GLYPH_AREA + port.font.size]

    state = dg.from_dgroup(image)
    port.set_dirty(recording)
    call(state)
    compare_pages(machine, port, start)
    bad = dg.mismatches(bytes(want), dg.to_dgroup(state, image))
    assert not bad, f"DGROUP differs at {[hex(o) for o in bad[:16]]}"
    want_recording = machine.read_u16(CODE0, CS_DIRTY_ENABLE) != 0
    assert port.recording() == want_recording, "dirty recording switch"
    port.set_dirty(False)


@pytest.fixture
def ready(port, g3_image, cgame):
    port.g3 = g3_image
    return port


@pytest.mark.parametrize("case", fuzz_cases(24))
def test_wave_background(machine, dg, ready, case):
    rnd = case_rng("wave_background", case)

    def edit(img):
        struct.pack_into("<h", img, WAVE_TYPE, rnd.choice([0, 0, 1, rnd.randrange(-5, 6)]))
        struct.pack_into("<h", img, DEMO_MODE, rnd.choice([0, 0, 3, 1]))
        titled = []
        for off in range(LEVEL_TITLES, LEVEL_TITLES + 6 * LEVELS, 6):   # {level, ?, text}, -1 ends
            level = struct.unpack_from("<h", img, off)[0]
            if level == -1:
                break
            titled.append(level)
        assert titled, "no level titles in the image"
        struct.pack_into("<h", img, LEVEL, rnd.choice([rnd.choice(titled), rnd.randrange(LEVELS)]))

    run(machine, dg, ready, rnd, "1000:055c", "1000:05d2", ready.background, edit)


@pytest.mark.parametrize("case", fuzz_cases(8))
def test_warp_banner(machine, dg, ready, case):
    rnd = case_rng("warp_banner", case)
    run(machine, dg, ready, rnd, "1000:0640", "1000:064c", ready.warp)


@pytest.mark.parametrize("case", fuzz_cases(8))
def test_game_over(machine, dg, ready, case):
    rnd = case_rng("game_over", case)
    run(machine, dg, ready, rnd, "1000:08ee", "1000:0913", lambda state: ready.game_over())
