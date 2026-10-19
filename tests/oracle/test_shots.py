"""W5a: the players' shots (src/game/bullets.c) against game86: shots_clear (1000:07b7),
shots_update (1000:0c28, with the real hit handlers of the entities it touches) and
shots_draw (1000:1499, pixels and dirty lists as in test_anim).

Shots carry a (direction, vx, vy) triple of the control tables (what player_fire gives them)
or one no nibble matches with direction 0 or >= 0x100: a bouncing shot with any other
unmatched triple sends the original's search on past the tables (docs/DEVIATIONS.md)."""

import ctypes
import struct

import pytest
from generators import PLAYFIELD_X, PLAYFIELD_Y, case_rng, fuzz_cases

from adapters.cport import bind
from llamatron_harness import Machine
from adapters.entities import ENTITY_COUNT
from test_anim import Port, compare_pages, restore_and_compare, setup_pages
from test_hit import DEBRIS_SEARCHES, KILL_ACTION_COUNT, SCORED_THINKS, debris_walker
from test_sprites import CODE0, CS_DIRTY_ENABLE, DRAW_PAGE, PAGE_IDS, PAGES, SCROLL_MODE
from think_oracle import SMART_BOMB, TICKS, w16
from w5a_oracle import (HIT_COUNT, HIT_X, HIT_Y, LEVEL_FLAGS, SHOT_COUNT, SHOT_SIZE, SHOTS, SHOTS_BOUNCE,
                        W5A_SCRATCH, W5aOracle, slot_address, u16)

SPRITE_SEG, REFS_AT = 0x218C, 0x001C
SPEEDUP_TIMER, SPEEDUP_PERIOD = 0x2336, 0x2338
SHOT_VX, SHOT_VY, SHOT_DIR = 0x0743, 0x0753, 0x0763
SHOT_SPRITES_DEFAULT = 0x9E
SOUND_LOG_SIZE = 64            # src/game/sound.c
X_LIMITS, Y_LIMITS = (8, 0x138), (8, 0xC0)
# The original keeps the shot's new y in [0x417] (and x in [0x413]) as scratch.
SHOTS_SCRATCH = W5A_SCRATCH | {HIT_Y, HIT_Y + 1}


@pytest.fixture(scope="module")
def oracle(cgame):
    o = W5aOracle(cgame)
    o.shots_update = bind(cgame, "shots_update", None, ctypes.c_void_p)
    o.shots_clear = bind(cgame, "shots_clear", None, ctypes.c_void_p)
    return o


@pytest.fixture(scope="module")
def tables(game86_path):
    """The (direction, vx, vy) triples player_fire gives shots, and the sprite numbers whose
    table entry is a frame."""
    machine = Machine.from_game86(game86_path)
    raw = machine.read_dgroup(SHOT_VX, 0x30)
    sb = lambda b: b - 256 if b > 127 else b
    triples = [(raw[0x20 + n], sb(raw[n]), sb(raw[0x10 + n])) for n in range(16) if raw[0x20 + n]]
    frames = set()
    for n in range(0x300):
        ptr = machine.read_u16(SPRITE_SEG, REFS_AT + 2 * n)
        if ptr == 0xFFFF:
            break
        if ptr >= 0x0400:
            frames.add(n)
    return triples, frames


def edge_biased(rnd, limits):
    lo, hi = limits
    return rnd.choice([rnd.randrange(lo - 3, lo + 5), rnd.randrange(hi - 4, hi + 4),
                       rnd.randrange(lo + 1, hi)])


def shot_record(rnd, triples, near=None):
    if rnd.random() < 0.25:
        return struct.pack("<8h", -1, *(rnd.randrange(-0x8000, 0x8000) for _ in range(7)))
    if near and rnd.random() < 0.6:
        x, y = near[0] + rnd.randrange(-16, 17), near[1] + rnd.randrange(-24, 25)
    else:
        x, y = edge_biased(rnd, X_LIMITS), edge_biased(rnd, Y_LIMITS)
    if rnd.random() < 0.85:
        direction, vx, vy = rnd.choice(triples)
        if rnd.random() < 0.2:          # only the low bytes are matched
            vx = (vx & 0xFF) - 256 if vx < 0 else vx
    else:
        direction = rnd.choice([0, rnd.randrange(0x100, 0x10000)])
        vx, vy = rnd.randrange(-4, 5), rnd.randrange(-4, 5)
    life = rnd.choice([0, 1, 2, rnd.randrange(0x40), rnd.randrange(-0x8000, 0x8000)])
    return struct.pack("<8h", x, y, vx, vy, direction - 0x10000 if direction > 0x7FFF else direction,
                       rnd.randrange(-0x8000, 0x8000), life, rnd.randrange(-0x8000, 0x8000))


def update_case(machine, oracle, rnd, triples, frames):
    case = oracle.random_image(machine, rnd, rnd.randrange(SCORED_THINKS))
    image = bytearray(case.image)
    live = []
    for slot in range(ENTITY_COUNT):
        base = slot_address(slot)
        if struct.unpack_from("<h", image, base)[0] < 0:
            continue
        live.append(slot)
        # Think 0x2c / 0x2d records are never hit (no kill / never solid): keep to the
        # thinks the score table covers, as test_hit does.
        image[base] = rnd.randrange(SCORED_THINKS)
        image[base + 0x1E:base + 0x20] = w16(rnd.randrange(KILL_ACTION_COUNT))
        image[base + 0x3E:base + 0x40] = w16(rnd.randrange(HIT_COUNT))
        image[base + 0x0C:base + 0x0E] = w16(rnd.choice(sorted(frames)))
        if rnd.random() < 0.3:
            image[base + 0x10:base + 0x12] = w16(0)     # not solid
    near = []
    for slot in live:
        base = slot_address(slot)
        near.append(struct.unpack_from("<2h", image, base + 2))
    for i in range(SHOT_COUNT):
        image[SHOTS + SHOT_SIZE * i:SHOTS + SHOT_SIZE * (i + 1)] = shot_record(
            rnd, triples, rnd.choice(near) if near else None)
    words = {SMART_BOMB: rnd.choice([0, 0, 0, 1]),
             SHOTS_BOUNCE: rnd.choice([0, 1]),
             LEVEL_FLAGS: rnd.choice([0, 0x32, 0xFFFF, 0x8000]),
             TICKS: rnd.randrange(0x10000),
             SPEEDUP_TIMER: rnd.randrange(0x10000),
             SPEEDUP_PERIOD: rnd.randrange(0x200)}
    for off, v in words.items():
        image[off:off + 2] = w16(v)
    case.image = oracle.dg.canonical(bytes(image))
    case.regs = {r: rnd.randrange(0x10000) for r in ("ax", "bx", "cx", "dx", "si", "di", "bp")}
    return case


def debris_ignore(image):
    """A debris kill action's pickup drop past the entity table (test_hit.kill_action_ignore)
    for every entity a shot might hit. A smart bomb's earlier kills change the slots the
    search walks, so then every debris kill's drop is left out."""
    out = set(SHOTS_SCRATCH)
    smart_bomb = u16(image, SMART_BOMB) != 0
    for slot in range(ENTITY_COUNT):
        base = slot_address(slot)
        if struct.unpack_from("<h", image, base)[0] < 0:
            continue
        action = u16(image, base + 0x1E)
        if u16(image, base + 0x3E) == 3 and action in DEBRIS_SEARCHES and \
                (smart_bomb or debris_walker(image, *DEBRIS_SEARCHES[action]) >= ENTITY_COUNT):
            out |= {base + 0x14, base + 0x15}
    return frozenset(out)


@pytest.mark.parametrize("case", fuzz_cases(64))
def test_shots_update(machine, oracle, tables, case):
    rnd = case_rng("shots_update", case)
    c = update_case(machine, oracle, rnd, *tables)
    want, want_sounds = oracle.run_original(machine, c, "1000:0c28")
    state = oracle.dg.from_dgroup(c.image)
    oracle.sound_clear()
    oracle.shots_update(ctypes.addressof(state))
    got = oracle.dg.to_dgroup(state, c.image)
    # A smart bomb can make more sounds than the port's log keeps (it counts them all).
    got_sounds = oracle.port_sounds()
    assert len(got_sounds) == len(want_sounds)
    oracle.compare(c, want, got, want_sounds[:SOUND_LOG_SIZE], got_sounds[:SOUND_LOG_SIZE],
                   ignore=debris_ignore(c.image))


@pytest.mark.parametrize("case", fuzz_cases(8))
def test_shots_clear(machine, oracle, tables, case):
    rnd = case_rng("shots_clear", case)
    c = update_case(machine, oracle, rnd, *tables)
    want, _ = oracle.run_original(machine, c, "1000:07b7")
    state = oracle.dg.from_dgroup(c.image)
    oracle.shots_clear(ctypes.addressof(state))
    oracle.compare(c, want, oracle.dg.to_dgroup(state, c.image), [], [])


@pytest.fixture(scope="module")
def port(cgame):
    p = Port(cgame)
    p.shots_draw = bind(cgame, "shots_draw", None, ctypes.c_void_p, ctypes.c_void_p)
    return p


@pytest.mark.parametrize("case", fuzz_cases(24))
def test_shots_draw(machine, oracle, port, tables, case):
    rnd = case_rng("shots_draw", case)
    _, frames = tables
    page, back = setup_pages(machine, port, rnd)
    img = bytearray(machine.read_dgroup(0, 0x10000))
    flags = rnd.choice([0, 0x32, 0xFFFF, 0x8000])
    base = 0 if flags & 0x8000 else SHOT_SPRITES_DEFAULT
    directions = [n - base for n in frames if 0 <= n - base < 0x100]
    for i in range(SHOT_COUNT):
        if rnd.random() < 0.3:
            rec = struct.pack("<8h", -1, *(rnd.randrange(-0x8000, 0x8000) for _ in range(7)))
        else:
            rec = struct.pack("<8h", rnd.randrange(*X_LIMITS), rnd.randrange(*Y_LIMITS),
                              rnd.randrange(-4, 5), rnd.randrange(-4, 5), rnd.choice(directions),
                              0, 0, 0)
        img[SHOTS + SHOT_SIZE * i:SHOTS + SHOT_SIZE * (i + 1)] = rec
    img[LEVEL_FLAGS:LEVEL_FLAGS + 2] = w16(flags)
    struct.pack_into("<HH", img, PAGES, page, back)
    struct.pack_into("<H", img, DRAW_PAGE, page)
    img[SCROLL_MODE] = 0
    img[0x39D9] = rnd.randrange(256)       # shots_draw sets the plain draw itself
    image = oracle.dg.canonical(bytes(img))

    machine.write_dgroup(0, image)
    machine.write_u16(CODE0, CS_DIRTY_ENABLE, 1)
    machine.call("1000:1499", max_insns=5_000_000)

    state = oracle.dg.from_dgroup(image)
    target = port.target(PAGE_IDS[page])
    port.set_dirty(True)
    port.shots_draw(ctypes.addressof(state), ctypes.byref(target))
    port.set_dirty(False)
    compare_pages(machine, port, "shots")
    restore_and_compare(machine, port, page, back)
