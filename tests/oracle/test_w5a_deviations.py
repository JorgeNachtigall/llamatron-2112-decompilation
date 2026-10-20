"""W5a: the edge cases behind docs/DEVIATIONS.md rows 8, 9 and 19, pinned against game86.

Row 8: slot searches stop at the end of the 70-record table. With fewer free slots than
the search needs, the original goes on into the records after the table; here those look
free (type word FFFF), the worst case. The port must leave slots 0..69 exactly as the
original does, and differ only where the row says: the records past the table (masked), the
counters the past-table records add to, the snake trail (enemy_init_10's head is past the
table), the pickup drop of a kill whose debris walker ended past the table, and the random
index after kill action 03's rocks past the table.
Row 9: enemy_shot_alloc with no free slot returns NULL; the original's callers write the
kind word of record 70 (masked), everything else must match.
Row 19: a bouncing shot's (direction, vx, vy) search never reaches past the 16 control
entries for any triple a shot can carry: the 16 table triples player_fire and the droid
give, each also with direction 0 (fire_back, ent_hit_04/05, clears it). Every such shot is
bounced at every edge, original against port."""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases

from adapters.cport import bind
from adapters.entities import ENTITY_COUNT, ENTITY_SIZE, ENTITY_TABLE
from test_enemy_init import init_address
from test_hit import SCORED_THINKS, check_hit, hit_address, hit_case
from test_shots import SOUND_LOG_SIZE, X_LIMITS, Y_LIMITS
from think_oracle import ENEMIES_ALIVE, FREE_SLOTS, TICKS, w16
from w5a_oracle import (INIT_FN, LEVEL_FLAGS, SHOT_COUNT, SHOT_SIZE, SHOTS, SHOTS_BOUNCE,
                        W5A_SCRATCH, W5aOracle, slot_address)

PAST_TABLE = ENTITY_TABLE + ENTITY_COUNT * ENTITY_SIZE
PAST_RECORDS = 16
PAST = frozenset(range(PAST_TABLE, PAST_TABLE + PAST_RECORDS * ENTITY_SIZE))
COUNTERS = frozenset({ENEMIES_ALIVE, ENEMIES_ALIVE + 1, FREE_SLOTS, FREE_SLOTS + 1})
SNAKE_TRAIL = frozenset(range(0x4481, 0x4481 + 64 * 8))
SINE8 = 0x4141
SNAKE_INIT = 0x10
HIT_KILL_ACTION, HIT_FIRE_BACK_2, HIT_FIRE_BACK_4 = 3, 4, 5
KILL_ACTION_ROCKS, KILL_ACTION_DEBRIS_INHERITED = 3, 4
SHOT_VX, SHOT_VY, SHOT_DIR = 0x0743, 0x0753, 0x0763
LAST_SLOT = ENTITY_COUNT - 1
RNG_INDEX = frozenset({0x39E9, 0x39EA})


@pytest.fixture(scope="module")
def oracle(cgame):
    o = W5aOracle(cgame)
    sine = bind(cgame, "asset_sine", ctypes.c_void_p)()
    assert sine, "extracted data not loaded"
    o.sine8 = ctypes.string_at(sine, 256)
    o.shots_update = bind(cgame, "shots_update", None, ctypes.c_void_p)
    o.shot_alloc = bind(cgame, "enemy_shot_alloc", ctypes.c_void_p, ctypes.c_void_p,
                        ctypes.c_void_p, ctypes.c_void_p)
    return o


def crowd(rnd, image: bytearray, free: set[int], keep=()) -> None:
    """Every slot busy except `free` (records the random image left free get a busy type
    word); the records after the table look free."""
    for slot in range(ENTITY_COUNT):
        base = slot_address(slot)
        if slot in keep:
            continue
        if slot in free:
            image[base:base + 2] = w16(0xFFFF)
        elif struct.unpack_from("<h", image, base)[0] < 0:
            image[base] = rnd.randrange(SCORED_THINKS)
            image[base + 1] = image[base]
    for j in range(PAST_RECORDS):
        base = PAST_TABLE + j * ENTITY_SIZE
        image[base:base + 2] = w16(0xFFFF)


def differing(oracle, want: bytes, got: bytes, ignore) -> list[int]:
    return oracle.dg.mismatches(want, got, ignore=frozenset(ignore))


# ---- row 8: enemy_init_10 ------------------------------------------------------------------

@pytest.mark.parametrize("free_count", range(1, 10))
@pytest.mark.parametrize("case", fuzz_cases(4))
def test_snake_init_full_table(machine, oracle, free_count, case):
    """enemy_init_10 (1000:2def) needs nine free slots from its own record on: with fewer the
    original puts the rest (and the head) past the table, the port stops."""
    rnd = case_rng(f"snake_full_{free_count}", case)
    c = oracle.random_image(machine, rnd)
    image = bytearray(c.image)
    image[SINE8:SINE8 + 256] = oracle.sine8
    free = set(rnd.sample(range(ENTITY_COUNT), free_count))
    crowd(rnd, image, free)
    slot = min(free)
    base = slot_address(slot)
    image[base + 0x32:base + 0x34] = w16(0)
    image[base + 0x06:base + 0x08] = w16(0x12)
    image[base + 0x0E:base + 0x10] = w16(0)
    image[base + 0x3A:base + 0x3C] = w16(SNAKE_INIT)
    image[FREE_SLOTS:FREE_SLOTS + 2] = w16(free_count - 1)
    c.image = oracle.dg.canonical(bytes(image))
    c.slot = slot
    c.regs = {"si": base, "di": base}

    want, _ = oracle.run_original(machine, c, f"1000:{init_address(machine, SNAKE_INIT):04x}")
    state = oracle.dg.from_dgroup(c.image)
    INIT_FN(oracle.init_table[SNAKE_INIT])(ctypes.addressof(state), oracle.dg.entity(state, slot))
    got = oracle.dg.to_dgroup(state, c.image)
    allowed = W5A_SCRATCH | PAST
    if free_count < 9:
        allowed |= COUNTERS | SNAKE_TRAIL
    # Slots 0..69 (never in `allowed`) are exactly what the original made of them.
    assert differing(oracle, want, got, allowed) == []
    assert any(want[o] != c.image[o] for o in PAST) == (free_count < 9)


# ---- row 8: the debris searches past the table ---------------------------------------------

@pytest.mark.parametrize("action", [KILL_ACTION_ROCKS, KILL_ACTION_DEBRIS_INHERITED],
                         ids=["kill_action_03", "kill_action_04"])
@pytest.mark.parametrize("free_count", [0, 1, 2, 3])
@pytest.mark.parametrize("case", fuzz_cases(4))
def test_kill_search_full_table(machine, oracle, action, free_count, case):
    """ent_hit_03's kill action with hp run out and too few free slots: kill action 04's
    debris search (DX = 0x60 from shots_update; the small rocks' kill action) goes past the
    table; kill action 03's ten rocks do only when the last slot is one of the free ones
    (half the cases)."""
    rnd = case_rng(f"kill_full_{action}_{free_count}", case)
    c = hit_case(machine, oracle, rnd, HIT_KILL_ACTION)
    image = bytearray(c.image)
    base = slot_address(c.slot)
    image[base + 0x18:base + 0x1A] = w16(0)
    image[base + 0x1E:base + 0x20] = w16(action)
    others = [s for s in range(ENTITY_COUNT) if s != c.slot]
    free = set(rnd.sample(others, free_count))
    if action == KILL_ACTION_ROCKS and case % 2:
        free = (free - {min(free)} if free else free) | {LAST_SLOT}
    crowd(rnd, image, free, keep={c.slot})
    c.image = oracle.dg.canonical(bytes(image))
    # Kill action 04's search (DX = 0x60) runs past the table. Kill action 03's rock loop
    # checks for the end only after a busy slot: when the last slot takes a rock, the loop
    # goes on past the table, and every rock there costs random numbers.
    last_free = struct.unpack_from("<h", c.image, slot_address(LAST_SLOT))[0] < 0
    past = action == KILL_ACTION_DEBRIS_INHERITED or last_free
    allowed = W5A_SCRATCH | PAST | COUNTERS
    if action == KILL_ACTION_DEBRIS_INHERITED:
        allowed |= {base + 0x14, base + 0x15}  # the drop: the slot after the walker
    else:
        allowed |= RNG_INDEX if past else set()
    check_hit(machine, oracle, c, HIT_KILL_ACTION, allowed)
    want, _ = oracle.run_original(machine, c, f"1000:{hit_address(machine, HIT_KILL_ACTION):04x}")
    assert any(want[o] != c.image[o] for o in PAST) == past


# ---- row 9: no free slot for an enemy shot -------------------------------------------------

@pytest.mark.parametrize("handler", [HIT_FIRE_BACK_2, HIT_FIRE_BACK_4],
                         ids=["ent_hit_04", "ent_hit_05"])
@pytest.mark.parametrize("case", fuzz_cases(8))
def test_fire_back_no_free_slot(machine, oracle, handler, case):
    """ent_hit_04/05 with level_flags < 0 fire back through enemy_shot_alloc (1000:1221);
    with the table full the original's callers (1131 etc.) write record 70's kind word."""
    rnd = case_rng(f"fire_back_full_{handler}", case)
    c = hit_case(machine, oracle, rnd, handler)
    image = bytearray(c.image)
    image[LEVEL_FLAGS:LEVEL_FLAGS + 2] = w16(0x8000)
    crowd(rnd, image, set(), keep={c.slot})
    c.image = oracle.dg.canonical(bytes(image))
    check_hit(machine, oracle, c, handler, W5A_SCRATCH | PAST)
    want, _ = oracle.run_original(machine, c, f"1000:{hit_address(machine, handler):04x}")
    kind = PAST_TABLE + 0x3A
    assert want[kind:kind + 2] != c.image[kind:kind + 2], "record 70's kind word not written"


@pytest.mark.parametrize("case", fuzz_cases(4))
def test_shot_alloc_no_free_slot(machine, oracle, case):
    rnd = case_rng("shot_alloc_full", case)
    c = oracle.random_image(machine, rnd)
    image = bytearray(c.image)
    crowd(rnd, image, set())
    image = oracle.dg.canonical(bytes(image))
    state = oracle.dg.from_dgroup(image)
    tmpl = (ctypes.c_int16 * 8)(*(rnd.randrange(-0x8000, 0x8000) for _ in range(8)))
    shot = oracle.shot_alloc(ctypes.addressof(state), oracle.dg.entity(state, 0),
                             ctypes.addressof(tmpl))
    assert shot is None
    assert oracle.dg.to_dgroup(state, image) == image


# ---- row 19: every reachable bouncing shot ---------------------------------------------------

def reachable_triples(machine):
    raw = machine.read_dgroup(SHOT_VX, 0x30)
    sb = lambda b: b - 256 if b > 127 else b
    out = []
    for n in range(16):
        direction = struct.unpack_from("<B", raw, 0x20 + n)[0]
        if direction:
            vx, vy = sb(raw[n]), sb(raw[0x10 + n])
            out += [(direction, vx, vy), (0, vx, vy)]
    return out


EDGES = {"left": lambda vx, vy: (X_LIMITS[0] + 1 - min(vx, 0), 100),
         "right": lambda vx, vy: (X_LIMITS[1] - 1 - max(vx, 0), 100),
         "top": lambda vx, vy: (160, Y_LIMITS[0] + 1 - min(vy, 0)),
         "bottom": lambda vx, vy: (160, Y_LIMITS[1] - 1 - max(vy, 0))}


@pytest.mark.parametrize("edge", sorted(EDGES))
def test_bounce_reachable_triples(machine, oracle, edge):
    """Each reachable triple one step from `edge`, moving into it when it can (a shot whose
    velocity points away only moves), bounce on; the port must match the original."""
    rnd = case_rng(f"bounce_{edge}", 0)
    for direction, vx, vy in reachable_triples(machine):
        c = oracle.random_image(machine, rnd)
        image = bytearray(c.image)
        for slot in range(ENTITY_COUNT):
            image[slot_address(slot):slot_address(slot) + 2] = w16(0xFFFF)
        x, y = EDGES[edge](vx, vy)
        x, y = x + vx, y + vy                      # one step back from the edge
        for i in range(SHOT_COUNT):
            rec = struct.pack("<8h", x, y, vx, vy, direction, 0, 0x40, 0) if i == 0 else \
                struct.pack("<8h", -1, 0, 0, 0, 0, 0, 0, 0)
            image[SHOTS + SHOT_SIZE * i:SHOTS + SHOT_SIZE * (i + 1)] = rec
        image[SHOTS_BOUNCE:SHOTS_BOUNCE + 2] = w16(1)
        image[TICKS:TICKS + 2] = w16(0)
        c.image = oracle.dg.canonical(bytes(image))
        c.regs = {}
        want, want_sounds = oracle.run_original(machine, c, "1000:0c28")
        state = oracle.dg.from_dgroup(c.image)
        oracle.sound_clear()
        oracle.shots_update(ctypes.addressof(state))
        got = oracle.dg.to_dgroup(state, c.image)
        bad = differing(oracle, want, got, W5A_SCRATCH | {0x417, 0x418})
        assert bad == [], ((direction, vx, vy), [(hex(o), want[o], got[o]) for o in bad[:8]])
        assert oracle.port_sounds()[:SOUND_LOG_SIZE] == want_sounds[:SOUND_LOG_SIZE]
