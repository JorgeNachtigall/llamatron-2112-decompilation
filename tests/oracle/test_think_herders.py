"""W5b herders and chasers (src/game/entities/think_herders.c, think_chasers.c) against
game86: think_0e (39e9 with the sub-modes at CS:3AFF: 3b63, 3b0f, 3b21, 3b07), think_15
(2457, driver status AH=6), think_16 (23a7), think_17 (220b), think_18 (203f)."""

import pytest
from generators import case_rng, fuzz_cases
from think_oracle import ThinkOracle

from adapters.entities import ENTITY_COUNT

UNHATCHED = 0x091B
COMPANION_X = 0x4883
SPEED_17 = 0x21BE
THINK_LLAMA = 0x05


@pytest.fixture(scope="module")
def oracle(cgame):
    return ThinkOracle(cgame)


def herder_entity(rnd, rec):
    rec.raw[0x1a] = rnd.randrange(0x10)                     # step period
    rec.raw[0x1b] = rnd.choice([0, 0, 1, rnd.randrange(0x10)])
    rec.set("vx", rnd.randrange(-3, 4))
    rec.set("vy", rnd.randrange(-3, 4))
    rec.raw[0x1c:0x1e] = rnd.randrange(0x90, 0xa0).to_bytes(2, "little")
    rec.set("sub_mode", rnd.randrange(4))
    rec.raw[0x22] = rnd.randrange(1, 0x40)                  # think_16 spawn period
    rec.raw[0x23] = rnd.choice([0, 1, rnd.randrange(0x40)])


def llamas(rnd, records, slot):
    """Some llamas (think 5, hp 0 = unclaimed / 1 = claimed), unhatched <= their number, and a
    herder target of seek / return / a llama / something else; the goal is often near."""
    herder = records[slot]
    llama_slots = [i for i in rnd.sample(range(ENTITY_COUNT), rnd.randrange(0, 8)) if i != slot]
    for i in llama_slots:
        r = records[i]
        if r.free:
            r.raw[:] = bytes(rnd.randrange(256) for _ in range(64))
        r.set("type", THINK_LLAMA | rnd.randrange(46) << 8)
        r.set("hp", rnd.choice([0, 0, 1]))
        if rnd.random() < 0.4:
            r.set("x", herder.get("x") + rnd.randrange(-9, 10))
            r.set("y", herder.get("y") + rnd.randrange(-9, 10))
    for i, r in enumerate(records):     # no stray llamas elsewhere
        if i not in llama_slots and i != slot and not r.free and r.get("type") & 0xFF == THINK_LLAMA:
            r.set("type", r.get("type") ^ 1)
    choice = rnd.random()
    if choice < 0.25:
        herder.set("timer", 0xFFFF)
    elif choice < 0.5:
        herder.set("timer", 0xFFFE)
    elif llama_slots and choice < 0.85:
        herder.set("timer", rnd.choice(llama_slots))
    else:
        herder.set("timer", rnd.randrange(ENTITY_COUNT))
    near = rnd.random() < 0.4
    cx = herder.get("x") + rnd.randrange(-9, 10) if near else rnd.randrange(0x10, 0x130)
    cy = herder.get("y") + rnd.randrange(-9, 10) if near else rnd.randrange(0x10, 0xb0)
    return {UNHATCHED: rnd.randrange(len(llama_slots) + 1), COMPANION_X: cx, COMPANION_X + 2: cy}


def chaser_entity(rnd, rec):
    rec.set("timer", rnd.choice([0, 4, 8, 12, rnd.randrange(0x10000)]))
    rec.raw[0x12] = rnd.choice([0, 1, 0xff, rnd.randrange(256)])
    rec.raw[0x13] = rnd.randrange(256)
    rec.set("vx", rnd.randrange(-3, 4))
    rec.set("vy", rnd.randrange(-3, 4))


def column_entity(rnd, rec):
    rec.set("timer", rnd.choice([0, 1]))
    rec.set("vy", rnd.choice([-2, -1, 1, 2]))
    if rnd.random() < 0.4:
        rec.set("y", rnd.choice([0x10, 0x11, 0xb6, 0xb7]))


CASES = {
    0x0e: (herder_entity, llamas),
    0x15: (herder_entity, llamas),
    0x16: (herder_entity, llamas),
    0x17: (chaser_entity, None),
    0x18: (column_entity, None),
}


@pytest.mark.parametrize("think", sorted(CASES), ids=lambda t: f"think_{t:02x}")
@pytest.mark.parametrize("case", fuzz_cases(96))
def test_think(machine, oracle, think, case):
    rnd = case_rng(f"think_{think:02x}", case)
    entity, table = CASES[think]
    c = oracle.make_case(machine, rnd, think, entity, table=table,
                         extra={SPEED_17: rnd.randrange(1, 4)})
    oracle.check(machine, c, think)
