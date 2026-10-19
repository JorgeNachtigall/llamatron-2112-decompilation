"""W5a: the 18 shot-hit handlers (tbl_hit_handlers, CS:0C04; src/game/entities/hit.c) with
the common kill (ent_hit_00), the pickup drop (0eb8) and, through ent_hit_03, the 12 kill
actions (tbl_hit_kill_actions, CS:1047; kill_actions.c), against game86.

The original is called as shots_update calls it (1000:0d15 / 0d8f): DI = the entity, SI = the
shot, [0x413]/[0x417] = the shot's new position, DX = 0x60. The port gets the same entity and
an EntityHit pointing at the same shot inside the GameState."""

import ctypes
import struct

import pytest
from generators import PLAYFIELD_X, PLAYFIELD_Y, case_rng, fuzz_cases

from w5a_oracle import (HIT_COUNT, HIT_FN, HIT_TABLE, HIT_X, HIT_Y, KILL_ACTION_COUNT, SHOT_COUNT,
                        SHOT_SIZE, SHOTS, SHOTS_UPDATE_DX, EntityHit, Point16, W5aOracle,
                        W5A_SCRATCH, slot_address)
from adapters.entities import ENTITY_COUNT
from think_oracle import SMART_BOMB, w16

# Kill actions that run the 3c12 debris search: fragment count, search limit (DX).
DEBRIS_SEARCHES = {0x01: (4, 0x3B), 0x02: (4, 0x3B), 0x04: (4, SHOTS_UPDATE_DX),
                   0x09: (8, 0x3B)}
SCORED_THINKS = 44          # tbl 2054 has 44 real records (+ the unused FFFF FFFF one)


@pytest.fixture(scope="module")
def oracle(cgame):
    return W5aOracle(cgame)


def hit_address(machine, index):
    return struct.unpack("<H", machine.read(0x1000, HIT_TABLE + 2 * index, 2))[0]


def hit_entity(index):
    def tighten(rnd, rec):
        rec.set("hit_handler", index)
        rec.set("kill_action", rnd.randrange(KILL_ACTION_COUNT))
        rec.set("hp", rnd.choice([0, 0, 1, 2, -1, rnd.randrange(-0x8000, 0x8000)]))
        # Counters the handlers decrement: favour the edges.
        for off in (0x14, 0x16, 0x24, 0x2E):
            rec.raw[off:off + 2] = w16(rnd.choice([0, 1, 0x100, 0x101, 0xFFFF,
                                                   rnd.randrange(0x10000)]))
    return tighten


def hit_case(machine, oracle, rnd, index):
    think = rnd.randrange(SCORED_THINKS)
    case = oracle.random_image(machine, rnd, think, hit_entity(index))
    image = bytearray(case.image)
    image[SMART_BOMB:SMART_BOMB + 2] = w16(rnd.choice([0, 0, 0, 1]))
    shot = rnd.randrange(SHOT_COUNT)
    base = SHOTS + SHOT_SIZE * shot
    x, y = rnd.randrange(*PLAYFIELD_X), rnd.randrange(*PLAYFIELD_Y)
    record = struct.pack("<8h", x, y, rnd.randrange(-8, 9), rnd.randrange(-8, 9),
                         rnd.randrange(0x30), rnd.randrange(-0x8000, 0x8000),
                         rnd.randrange(0x100), rnd.randrange(-0x8000, 0x8000))
    image[base:base + SHOT_SIZE] = record
    image[HIT_X:HIT_X + 2] = w16(x)
    image[HIT_Y:HIT_Y + 2] = w16(y)
    case.image = oracle.dg.canonical(bytes(image))
    case.regs = {"di": slot_address(case.slot), "si": base, "dx": SHOTS_UPDATE_DX,
                 "cx": rnd.randrange(0x46), "bp": rnd.randrange(SHOT_COUNT)}
    case.shot = shot
    return case


def kill_action_ignore(case):
    """ent_hit_00 takes the drop direction from the vx of the slot after the debris search
    (kill_after_debris). Past the table the original reads whatever follows it and the port
    keeps the shot direction (docs/DEVIATIONS.md): leave the drop (+14) out then."""
    base = slot_address(case.slot)
    action = struct.unpack_from("<H", case.image, base + 0x1E)[0]
    if action in DEBRIS_SEARCHES and \
            debris_walker(case.image, *DEBRIS_SEARCHES[action]) >= ENTITY_COUNT:
        return W5A_SCRATCH | {base + 0x14, base + 0x15}
    return W5A_SCRATCH


def check_hit(machine, oracle, case, index, ignore=W5A_SCRATCH):
    want, want_sounds = oracle.run_original(machine, case, f"1000:{hit_address(machine, index):04x}")
    state = oracle.dg.from_dgroup(case.image)
    oracle.sound_clear()
    smart = struct.unpack_from("<H", case.image, SMART_BOMB)[0] != 0
    at = Point16(*struct.unpack_from("<h", case.image, HIT_X), *struct.unpack_from("<h", case.image, HIT_Y))
    hit = EntityHit(oracle.shot_pointer(state, case.shot), smart, at)
    fn = oracle.hit_table[index]
    assert fn, f"ent_hit_{index:02x} missing"
    HIT_FN(fn)(ctypes.addressof(state), oracle.dg.entity(state, case.slot), ctypes.addressof(hit))
    got = oracle.dg.to_dgroup(state, case.image)
    oracle.compare(case, want, got, want_sounds, oracle.port_sounds(), ignore=ignore)


@pytest.mark.parametrize("index", range(HIT_COUNT), ids=lambda i: f"ent_hit_{i:02x}")
@pytest.mark.parametrize("case", fuzz_cases(32))
def test_hit(machine, oracle, index, case):
    rnd = case_rng(f"ent_hit_{index:02x}", case)
    case = hit_case(machine, oracle, rnd, index)
    check_hit(machine, oracle, case, index, kill_action_ignore(case) if index == 3 else W5A_SCRATCH)


@pytest.mark.parametrize("action", range(KILL_ACTION_COUNT), ids=lambda a: f"kill_action_{a:02x}")
@pytest.mark.parametrize("case", fuzz_cases(32))
def test_kill_action(machine, oracle, action, case):
    """ent_hit_03 with the hit points running out, so the kill action always runs."""
    rnd = case_rng(f"kill_action_{action:02x}", case)
    c = hit_case(machine, oracle, rnd, 3)
    image = bytearray(c.image)
    base = slot_address(c.slot)
    image[base + 0x18:base + 0x1A] = w16(0)         # hp 0 -> -1
    image[base + 0x1E:base + 0x20] = w16(action)
    c.image = oracle.dg.canonical(bytes(image))
    check_hit(machine, oracle, c, 3, kill_action_ignore(c))


def debris_walker(image, count, search_limit):
    """Slot after the last one the 3c12 debris search looked at (may be past the table)."""
    left, nxt, slot = search_limit, count - 1, 0
    while True:
        busy = struct.unpack_from("<h", image, slot_address(slot))[0] >= 0
        slot += 1
        left -= 1
        if busy and left < 0:
            return slot
        if not busy:
            nxt -= 1
            if nxt < 0:
                return slot
