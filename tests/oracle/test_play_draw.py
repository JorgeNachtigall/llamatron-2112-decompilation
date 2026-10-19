"""W6 play frame: game86 play_draw_hook 1000:1a3b against src/game/draw_play.c through
llamatron_game_testlib, with the game's real sprite banks.

A random play state (ships, facings, ship sprite bases, respawn timers, shield, rescue
markers, pause picture, options, wave-start delay, tick) with no live shots or entities (their
draws have tests of their own) is drawn by both sides into the page being drawn with dirty
recording on, as in play. Compared: all three pages, the DGROUP state (frame_drawn 0429) and
the pages after both sides' dirty lists are restored from a random background.
"""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases

from adapters.cport import bind
from adapters.gamestate import DgroupMap
from test_anim import (ENTITY_COUNT, ENTITY_SIZE, ENTITY_TABLE, REFS_AT, SPRITE_SEG, Port,
                       compare_pages, restore_and_compare, setup_pages)
from test_sprites import (CODE0, CS_DIRTY_ENABLE, DRAW_PAGE, PAGES, PAGE_IDS, SCROLL_MODE,
                          DrawTarget, GfxSprite, Sprite)

SHOTS, SHOT_SIZE, SHOT_COUNT = 0x68C9, 0x10, 15
OPTIONS = 0x2CF6
SHIPS = 0x2CFB                       # P1 x, y, P2 x, y
SHIELD = 0x0907
FACING = (0x06C2, 0x07F9)
SHIP_BASE = (0x07F5, 0x07F7)
RESPAWN = (0x07F3, 0x0634)
RESCUE_Y = (0x0921, 0x0923)
WAVE_START_DELAY = 0x068A
PAUSE_MESSAGE, PAUSE_PICTURES = 0x09EC, 0x09E2
TICK = 0x39E6
FRAME_DRAWN = 0x0429
SHIP_BASES = (0x0C, 0x5E)            # what 0471 / the image leave in 07F5 / 07F7
CS_G3_SEG, G3_SEG = 0x0261, 0x8000    # g3.cmp is decoded at start-up: the image has none
G3_SHAPES, BANK_TITLE = 16, 1


@pytest.fixture(scope="module")
def dg(cgame):
    return DgroupMap(cgame)


@pytest.fixture(scope="module")
def port(cgame):
    p = Port(cgame)
    p.hook = bind(cgame, "play_draw_hook", None, ctypes.c_void_p, ctypes.POINTER(DrawTarget))
    return p


@pytest.fixture(scope="module")
def numbers(port):
    """Sprite numbers whose table entry is a frame of the bank."""
    from llamatron_harness import Machine, default_game86_path
    m = Machine.from_game86(default_game86_path())
    out = set()
    for n in range(0x300):
        ptr = m.read_u16(SPRITE_SEG, REFS_AT + 2 * n)
        if ptr == 0xFFFF:
            break
        if port.frame_of_offset(ptr) >= 0:
            out.add(n)
    return out


class BankSprite(Sprite):
    """A frame of the port's extracted bank, in the original's layout (Sprite.original_bytes)."""

    def __init__(self, gs):
        self.columns = gs.width // 16
        self.rows = gs.height
        self.width = gs.width
        self.nominal = 2 * gs.hotspot_x
        self.pixels = bytearray(ctypes.string_at(gs.pixels, gs.width * gs.height))
        self.garbage = [0] * len(self.pixels)


@pytest.fixture(scope="module")
def g3_image(cgame, port):
    """g3.cmp's 16 shapes as game86 holds them at segment cs_g3_seg: 16 offsets, then shapes."""
    shape = bind(cgame, "asset_sprite_gfx", ctypes.POINTER(GfxSprite), ctypes.c_int, ctypes.c_uint)
    table, blob = bytearray(2 * G3_SHAPES), bytearray()
    for i in range(G3_SHAPES):
        struct.pack_into("<H", table, 2 * i, len(table) + len(blob))
        blob += BankSprite(shape(BANK_TITLE, i).contents).original_bytes()
    return bytes(table + blob)


def facing(rnd, base, numbers):
    """A facing whose four ship frames (base + |facing| + 0..3) are all real frames."""
    choices = [f for f in range(-40, 41) if all(base + abs(f) + k in numbers for k in range(4))]
    edges = [f for f in (0, -1, 1) if f in choices]       # the sign test 1a97 / 1b61
    return rnd.choice(edges if edges and rnd.random() < 0.3 else choices)


def ship_position(rnd):
    # The scaled respawn draw always draws its first row: below the screen the original
    # writes into the next page, which the port's page target drops (DEVIATIONS #17).
    return (rnd.choice([rnd.randrange(0x0F, 0x130), rnd.randrange(-20, 340)]),
            rnd.choice([rnd.randrange(0x12, 0xBC), rnd.randrange(-10, 200)]))


def play_state(machine, rnd, numbers, page, back):
    img = bytearray(machine.read_dgroup(0, 0x10000))
    for slot in range(ENTITY_COUNT):
        base = ENTITY_TABLE + ENTITY_SIZE * slot
        img[base:base + 2] = b"\xff\xff"
    for slot in range(SHOT_COUNT):
        struct.pack_into("<h", img, SHOTS + SHOT_SIZE * slot, -1)
    img[OPTIONS] = rnd.choice([0, 1, 3, 5, 7]) | rnd.choice([0, 0x10])
    for p in range(2):
        struct.pack_into("<hh", img, SHIPS + 4 * p, *ship_position(rnd))
        base = rnd.choice(SHIP_BASES)
        struct.pack_into("<h", img, SHIP_BASE[p], base)
        struct.pack_into("<h", img, FACING[p], facing(rnd, base, numbers))
        struct.pack_into("<h", img, RESPAWN[p],
                         rnd.choice([0, 0, 0, 1, -1, 2, -2, rnd.randrange(-0x64, 0x65)]))
        struct.pack_into("<h", img, RESCUE_Y[p], rnd.choice([-1, -1, 0, rnd.randrange(0, 200)]))
    struct.pack_into("<h", img, SHIELD, rnd.choice([0, 0, rnd.randrange(1, 501)]))
    struct.pack_into("<h", img, WAVE_START_DELAY, rnd.choice([0, 0, rnd.randrange(1, 0x33)]))
    pause = rnd.choice([0, 0, 1, 2, 3, 4])
    struct.pack_into("<H", img, PAUSE_MESSAGE,
                     0 if pause == 0 else struct.unpack_from("<H", img, PAUSE_PICTURES + 2 * (pause - 1))[0])
    struct.pack_into("<H", img, TICK, rnd.randrange(0x10000))
    img[FRAME_DRAWN] = 0
    struct.pack_into("<HH", img, PAGES, page, back)
    struct.pack_into("<H", img, DRAW_PAGE, page)
    img[SCROLL_MODE] = 0
    img[0x39D9] = rnd.randrange(256)       # every draw sets its mode itself
    struct.pack_into("<hh", img, 0x39DA, rnd.randrange(-8, 9), rnd.randrange(-2, 3))
    return bytes(img)


@pytest.mark.parametrize("case", fuzz_cases(48))
def test_play_draw_hook(machine, dg, port, numbers, g3_image, case):
    rnd = case_rng("play_draw_hook", case)
    page, back = setup_pages(machine, port, rnd)
    image = dg.canonical(play_state(machine, rnd, numbers, page, back))

    machine.write_dgroup(0, image)
    machine.write(G3_SEG, 0, g3_image)
    machine.write_u16(CODE0, CS_G3_SEG, G3_SEG)
    machine.write_u16(CODE0, CS_DIRTY_ENABLE, 1)
    machine.call("1000:1a3b", far=True, max_insns=5_000_000)
    want_state = machine.read_dgroup(0, 0x10000)

    state = dg.from_dgroup(image)
    target = port.target(PAGE_IDS[page])
    port.set_dirty(True)
    port.hook(state, ctypes.byref(target))
    port.set_dirty(False)

    compare_pages(machine, port, "play frame")
    bad = dg.mismatches(want_state, dg.to_dgroup(state, image))
    assert not bad, f"DGROUP differs at {[hex(o) for o in bad[:16]]}"
    restore_and_compare(machine, port, page, back)
