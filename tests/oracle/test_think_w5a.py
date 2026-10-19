"""W5a think handlers (src/game/entities/think_w5a.c) against game86: think_23 walker (35b4),
think_24 seeker (3490), think_26 dropper (3014), think_27 edge spawner (2f87)."""

import pytest
from generators import case_rng, fuzz_cases
from think_oracle import ThinkOracle, w16

WAVE_INTRO_TIMER = 0x08FC
LEVEL = 0x21AA
TUNING_P0830 = 0x0830
RNG_TABLE, RNG_TABLE_SIZE, RNG_INDEX = 0x6AC9, 512, 0x39E9


@pytest.fixture(scope="module")
def oracle(cgame):
    return ThinkOracle(cgame)


def byte_pair(rnd, rec, off):
    """A reload period in the low byte and a counter near expiry in the high byte."""
    rec.raw[off] = rnd.choice([0, 1, rnd.randrange(0x40), rnd.randrange(256)])
    rec.raw[off + 1] = rnd.choice([0, 0, 1, 0xff, rnd.randrange(256)])


def walker(rnd, rec):
    byte_pair(rnd, rec, 0x08)
    rec.set("sprite", rnd.choice([0xfc, 0xfd]))


def seeker(rnd, rec):
    byte_pair(rnd, rec, 0x18)
    rec.raw[0x16:0x18] = w16(rnd.randrange(8))
    rec.raw[0x1a:0x1c] = w16(rnd.choice([0x136, rnd.randrange(0x150)]))


def dropper(rnd, rec):
    state = rnd.choice([0, 0, 1, 1, 1, 2, rnd.randrange(0x10000)])
    rec.raw[0x1c:0x1e] = w16(state)
    tops = [0x1e, 0x1f, 0x1f, 0x20] if state == 1 else []
    rec.raw[0x16:0x18] = w16(rnd.choice([1, 0x1e, 0x1f, 0xb6, 0xb7, 0xb8,
                                         rnd.randrange(0x10, 0xc0)] + tops))
    rec.raw[0x14:0x16] = w16(rnd.randrange(0x10000))
    rec.raw[0x18:0x1a] = w16(rnd.randrange(0x10000))
    rec.raw[0x1a:0x1c] = w16(rnd.choice([0, 1, 2, 4, rnd.randrange(0x10000)]))


def edge_spawner(rnd, rec):
    rec.raw[0x14:0x16] = w16(rnd.choice([0, 0, 0, 1, rnd.randrange(0x10000)]))
    byte_pair(rnd, rec, 0x16)
    # entity_move_bounce loops forever on a speed out of range both ways.
    rec.set("vx", rnd.randrange(-3, 4))
    rec.set("vy", rnd.randrange(-3, 4))


CASES = {0x23: walker, 0x24: seeker, 0x26: dropper, 0x27: edge_spawner}


@pytest.mark.parametrize("think", sorted(CASES), ids=lambda t: f"think_{t:02x}")
@pytest.mark.parametrize("case", fuzz_cases(96))
def test_think(machine, oracle, think, case):
    rnd = case_rng(f"think_{think:02x}", case)
    extra = {WAVE_INTRO_TIMER: rnd.choice([0, 0, 0, 1]),
             LEVEL: rnd.choice([0x31, 0x32, rnd.randrange(100)]),
             TUNING_P0830: rnd.choice([0, 1, 3, 7, rnd.randrange(0x10000)]),
             RNG_INDEX: rnd.randrange(RNG_TABLE_SIZE)}
    table = [i & 0xFF for i in range(RNG_TABLE_SIZE)]     # like the start-up shuffle
    rnd.shuffle(table)
    extra.update({RNG_TABLE + i: table[i] | table[i + 1] << 8
                  for i in range(0, RNG_TABLE_SIZE, 2)})
    c = oracle.make_case(machine, rnd, think, CASES[think], extra=extra)
    oracle.check(machine, c, think)
