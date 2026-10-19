"""W5a: every one of the 41 enemy types live in the shot-hit code, as its own init leaves it.

test_hit, test_kill_action and test_shots run the handlers on random records with the handler
index forced. Here each case first runs the original enemy_init_NN (1000:2CA4 table, as
test_enemy_init enters it) and keeps the original's result, so the entities the init made
carry the think, hatch, sprite, hit handler, kill action and counters that type really has.
Then, on that image, both sides run
  - the type's own hit handler (+3E) on one of its entities (with hp run out in half the
    cases, so ent_hit_03 runs its real kill action), entered as test_hit enters it; and
  - shots_update (1000:0c28) with shots near that entity, as test_shots runs it.
Half the type's spawn markers are first hatched as think_00 does it (think = hatch_into, next
anim, solid) to cover the type after hatching; the hit test only takes solid entities, as
shots_update does."""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases

from adapters.entities import ENTITY_COUNT
from test_enemy_init import init_address, spawn_case
from test_hit import SCORED_THINKS, check_hit
from test_shots import SOUND_LOG_SIZE, debris_ignore, shot_record, tables  # noqa: F401
from think_oracle import SMART_BOMB, TICKS, w16
from w5a_oracle import (HIT_COUNT, HIT_X, HIT_Y, INIT_COUNT, KILL_ACTION_COUNT, LEVEL_FLAGS,
                        SCRIPT_PARAMS,
                        SHOT_COUNT, SHOT_SIZE, SHOTS, SHOTS_BOUNCE, SHOTS_UPDATE_DX, W5aOracle,
                        slot_address, u16)
from adapters.cport import bind

SPEEDUP_TIMER, SPEEDUP_PERIOD = 0x2336, 0x2338
HATCH_THINKS = (0x00, 0x04, 0x07, 0x0A)     # think_00 and its aliases
# Types whose entities are never solid, so never hit: 1c (the dropper, think_26) and 1d (the
# edge spawner, think_27); neither think writes +10.
NEVER_SOLID = {0x1C, 0x1D}


@pytest.fixture(scope="module")
def oracle(cgame):
    o = W5aOracle(cgame)
    sine = bind(cgame, "asset_sine", ctypes.c_void_p)()
    assert sine, "extracted data not loaded"
    o.sine8 = ctypes.string_at(sine, 256)      # spawn_case copies it into the image
    o.shots_update = bind(cgame, "shots_update", None, ctypes.c_void_p)
    return o


def is_live(image, slot):
    return struct.unpack_from("<h", image, slot_address(slot))[0] >= 0


def spawned(machine, oracle, rnd, index):
    """The original's image after enemy_init_`index`, and the slots it made live."""
    case = spawn_case(machine, oracle, rnd, index)
    # A level's script parameters 0 and 3 are the llamas' hit handler and kill action
    # (enemy_setup_01).
    image = bytearray(case.image)
    image[SCRIPT_PARAMS:SCRIPT_PARAMS + 2] = w16(rnd.randrange(HIT_COUNT))
    image[SCRIPT_PARAMS + 6:SCRIPT_PARAMS + 8] = w16(rnd.randrange(KILL_ACTION_COUNT))
    # Inits that do not set the hit handler (1c, 1d) leave it, and the kill action, as an
    # earlier entity in the slot left them: valid ones.
    for slot in range(ENTITY_COUNT):
        if not is_live(image, slot):
            base = slot_address(slot)
            image[base + 0x1E:base + 0x20] = w16(rnd.randrange(KILL_ACTION_COUNT))
            image[base + 0x3E:base + 0x40] = w16(rnd.randrange(HIT_COUNT))
    case.image = oracle.dg.canonical(bytes(image))
    after, _ = oracle.run_original(machine, case, f"1000:{init_address(machine, index):04x}")
    made = [s for s in range(ENTITY_COUNT)
            if is_live(after, s) and (s == case.slot or not is_live(case.image, s))]
    assert made, f"enemy_init_{index:02x} made no entity"
    return case, bytearray(after), made


def tame_others(rnd, image, made, frames):
    """The random records left from random_image get fields a real entity can have (as
    test_shots does); the type's own entities are left as the init made them."""
    for slot in range(ENTITY_COUNT):
        if slot in made or not is_live(image, slot):
            continue
        base = slot_address(slot)
        image[base] = rnd.randrange(SCORED_THINKS)
        image[base + 0x1E:base + 0x20] = w16(rnd.randrange(KILL_ACTION_COUNT))
        image[base + 0x3E:base + 0x40] = w16(rnd.randrange(HIT_COUNT))
        image[base + 0x0C:base + 0x0E] = w16(rnd.choice(frames))
        if rnd.random() < 0.3:
            image[base + 0x10:base + 0x12] = w16(0)


def is_solid(image, slot):
    return u16(image, slot_address(slot) + 0x10) != 0


def hatch(rnd, image, slot):
    """What think_00 (1000:43dc) does when the spawn marker's count runs out."""
    base = slot_address(slot)
    image[base] = image[base + 1]
    image[base + 0x06:base + 0x08] = w16(u16(image, base + 0x06) + 1)     # anim
    image[base + 0x0E:base + 0x10] = w16(rnd.choice([0xFFFF, 0xFFFE]))   # scale ran out
    image[base + 0x10:base + 0x12] = w16(1)


def type_case(machine, oracle, rnd, index, frames, need_solid=False):
    """The image after enemy_init_`index`, with one of its entities (case.slot) hatched in
    some cases, about to lose its last hit point in half of them. With `need_solid` the
    entity is one a shot can hit (solid, after hatching if need be); None if the type has
    none."""
    case, image, made = spawned(machine, oracle, rnd, index)
    tame_others(rnd, image, made, frames)
    for slot in made:
        if image[slot_address(slot)] in HATCH_THINKS and rnd.random() < 0.5:
            hatch(rnd, image, slot)
    if need_solid:
        for slot in made:
            if not is_solid(image, slot) and image[slot_address(slot)] in HATCH_THINKS:
                hatch(rnd, image, slot)
        made_solid = [s for s in made if is_solid(image, s)]
        if not made_solid:
            return None
        slot = rnd.choice(made_solid)
    else:
        slot = rnd.choice(made)
    base = slot_address(slot)
    if rnd.random() < 0.5:
        image[base + 0x18:base + 0x1A] = w16(0)         # hp runs out on this hit
    image[SMART_BOMB:SMART_BOMB + 2] = w16(rnd.choice([0, 0, 0, 1]))
    case.slot = slot
    return case, image, made


@pytest.mark.parametrize("index", range(INIT_COUNT), ids=lambda i: f"type_{i:02x}")
@pytest.mark.parametrize("case", fuzz_cases(16))
def test_type_hit(machine, oracle, tables, index, case):
    rnd = case_rng(f"type_hit_{index:02x}", case)
    _, frames = tables
    made = type_case(machine, oracle, rnd, index, sorted(frames), need_solid=True)
    if made is None:
        assert index in NEVER_SOLID, f"type {index:02x} has no solid entity"
        pytest.skip("never solid: no shot can hit it")
    c, image, _ = made
    base = slot_address(c.slot)
    handler = u16(image, base + 0x3E)
    assert handler < HIT_COUNT, f"type {index:02x}: hit handler {handler:#x}"
    shot = rnd.randrange(SHOT_COUNT)
    x, y = struct.unpack_from("<2h", image, base + 2)
    x, y = x + rnd.randrange(-12, 13), y + rnd.randrange(-8, 9)
    sb = SHOTS + SHOT_SIZE * shot
    image[sb:sb + SHOT_SIZE] = struct.pack(
        "<8h", x, y, rnd.randrange(-8, 9), rnd.randrange(-8, 9), rnd.randrange(0x30),
        rnd.randrange(-0x8000, 0x8000), rnd.randrange(0x100), rnd.randrange(-0x8000, 0x8000))
    image[HIT_X:HIT_X + 2] = w16(x)
    image[HIT_Y:HIT_Y + 2] = w16(y)
    c.image = oracle.dg.canonical(bytes(image))
    c.regs = {"di": base, "si": sb, "dx": SHOTS_UPDATE_DX, "cx": rnd.randrange(0x46),
              "bp": rnd.randrange(SHOT_COUNT)}
    c.shot = shot
    check_hit(machine, oracle, c, handler, debris_ignore(c.image))


@pytest.mark.parametrize("index", range(INIT_COUNT), ids=lambda i: f"type_{i:02x}")
@pytest.mark.parametrize("case", fuzz_cases(16))
def test_type_shots(machine, oracle, tables, index, case):
    rnd = case_rng(f"type_shots_{index:02x}", case)
    triples, frames = tables
    c, image, made = type_case(machine, oracle, rnd, index, sorted(frames))
    near = [struct.unpack_from("<2h", image, slot_address(s) + 2) for s in made]
    for i in range(SHOT_COUNT):
        image[SHOTS + SHOT_SIZE * i:SHOTS + SHOT_SIZE * (i + 1)] = shot_record(
            rnd, triples, rnd.choice(near))
    words = {SHOTS_BOUNCE: rnd.choice([0, 1]),
             LEVEL_FLAGS: rnd.choice([0, 0x32, 0xFFFF, 0x8000]),
             TICKS: rnd.randrange(0x10000),
             SPEEDUP_TIMER: rnd.randrange(0x10000),
             SPEEDUP_PERIOD: rnd.randrange(0x200)}
    for off, v in words.items():
        image[off:off + 2] = w16(v)
    c.image = oracle.dg.canonical(bytes(image))
    c.regs = {r: rnd.randrange(0x10000) for r in ("ax", "bx", "cx", "dx", "si", "di", "bp")}

    want, want_sounds = oracle.run_original(machine, c, "1000:0c28")
    state = oracle.dg.from_dgroup(c.image)
    oracle.sound_clear()
    oracle.shots_update(ctypes.addressof(state))
    got = oracle.dg.to_dgroup(state, c.image)
    got_sounds = oracle.port_sounds()
    assert len(got_sounds) == len(want_sounds)
    oracle.compare(c, want, got, want_sounds[:SOUND_LOG_SIZE], got_sounds[:SOUND_LOG_SIZE],
                   ignore=debris_ignore(c.image))
