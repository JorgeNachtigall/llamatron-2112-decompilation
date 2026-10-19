"""W5b orbiters, edge runner and storm (src/game/entities/think_orbiters.c) against game86:
think_20 core (2a75 with the spawn 366f), think_21 satellite (2a21, orbit step 2ab5),
think_22 edge runner (2865, turn 292b), think_2c storm (2736), think_2d bolt (27c4)."""

import struct

import pytest
from generators import case_rng, fuzz_cases
from think_oracle import ThinkOracle

ORBIT_ANGLE = 0x081A        # 32-bit
FREE_TAIL = range(0x3D, 70)


@pytest.fixture(scope="module")
def oracle(cgame):
    return ThinkOracle(cgame)


def orbit_entity(rnd, rec):
    rec.raw[0x14] = rnd.randrange(256)                      # angle
    rec.raw[0x15] = rnd.choice([1, 2, 0xfe, rnd.randrange(256)])
    rec.raw[0x18] = rnd.randrange(1, 0x10)                  # period
    rec.raw[0x19] = rnd.choice([0, 5, 6, rnd.randrange(0x10)])
    rec.raw[0x1a:0x1c] = rnd.choice([0, 3, 0xFFFF]).to_bytes(2, "little")


def runner_entity(rnd, rec):
    rec.set("timer", rnd.choice([0, 1]))
    rec.set("hp", rnd.choice([0, 1]))
    rec.raw[0x1a:0x1c] = rnd.choice([0xFFFF, 0xFFFF, 0, 2]).to_bytes(2, "little")
    x, y = rec.get("x"), rec.get("y")
    if rnd.random() < 0.3:
        x, y = rnd.choice([0x10, 0x11, 0x136, 0x137]), rnd.choice([0x10, 0x11, 0xbc, 0xbd])
        rec.set("x", x)
        rec.set("y", y)
    rec.raw[0x1c:0x20] = struct.pack("<HH", rnd.randrange(0x10000), x)
    rec.raw[0x20:0x24] = struct.pack("<HH", rnd.randrange(0x10000), y)
    rec.raw[0x24:0x28] = struct.pack("<i", rnd.choice([rnd.randrange(0x30000), rnd.randrange(0x200),
                                                       -1]))
    rec.raw[0x28:0x2a] = rnd.choice([0x10, 0x100, 0x1000, rnd.randrange(0x10000)]).to_bytes(2, "little")


def storm_entity(rnd, rec):
    rec.raw[0x16] = rnd.randrange(1, 0x20)
    rec.raw[0x17] = rnd.choice([0, 0, 1, rnd.randrange(0x20)])
    rec.set("timer", rnd.choice([0, 1, 5]))


def bolt_entity(rnd, rec):
    rec.raw[0x14] = rnd.randrange(1, 4)
    rec.raw[0x15] = rnd.choice([0, 0, 1, 3])
    rec.set("sprite", rnd.choice([0x147, 0x148, 0x149]))


def full_tail(rnd, records, slot):
    """Sometimes no free slot among the first 61 (366f's search window)."""
    if rnd.random() < 0.3:
        for i in range(0x3D):
            if i != slot and records[i].free:
                records[i].set("type", 0x0101)
    return {}


CASES = {
    0x20: (orbit_entity, full_tail),
    0x21: (orbit_entity, None),
    0x22: (runner_entity, None),
    0x2c: (storm_entity, None),
    0x2d: (bolt_entity, None),
}


@pytest.mark.parametrize("think", sorted(CASES), ids=lambda t: f"think_{t:02x}")
@pytest.mark.parametrize("case", fuzz_cases(96))
def test_think(machine, oracle, think, case):
    rnd = case_rng(f"think_{think:02x}", case)
    entity, table = CASES[think]
    angle = rnd.choice([0x20000, rnd.randrange(0x40000), 0xFFFFFFC0])
    c = oracle.make_case(machine, rnd, think, entity, table=table,
                         extra={ORBIT_ANGLE: angle & 0xFFFF, ORBIT_ANGLE + 2: angle >> 16})
    oracle.check(machine, c, think)
