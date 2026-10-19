"""W5b simple think handlers (src/game/entities/think_basic.c, movement.c) against game86:
think_00 hatch (43dc, also 04/07/0a), think_01 touch-only (43f1) with the player-touch tail
43f7 and entity_hits_player 4b49, think_02 fade (43c9), think_03 swarm (4ac9) with
entity_move_bounce 4e7f, think_09 timer (4ec8), think_0d straight shot (504f)."""

import pytest
from generators import case_rng, fuzz_cases
from think_oracle import ThinkOracle


@pytest.fixture(scope="module")
def oracle(cgame):
    return ThinkOracle(cgame)


def hatch_entity(rnd, rec):
    rec.set("scale", rnd.choice([0, 1, 2, 3, rnd.randrange(0x10000), 0x8000, 0x8001]))


def timer_entity(rnd, rec):
    rec.set("timer", rnd.choice([0, 1, 0xFFFF, 0x8000, rnd.randrange(0x40)]))
    rec.set("scale", rnd.choice([13, 14, 15, 0x7FFF, rnd.randrange(0x10000)]))


def swarm_entity(rnd, rec):
    rec.set("timer", rnd.choice([0, 0, 1, rnd.randrange(40)]))
    rec.set("vx", rnd.randrange(-8, 9))
    rec.set("vy", rnd.randrange(-8, 9))
    if rnd.random() < 0.3:      # at the edges, to exercise the bounce and the y pin
        rec.set("x", rnd.choice([8, 9, 0x137, 0x138]))
        rec.set("y", rnd.choice([0x10, 0x11, 0xbb, 0xbc]))


def shot_entity(rnd, rec):
    rec.set("vx", rnd.randrange(-6, 7))
    rec.set("vy", rnd.randrange(-6, 7))
    if rnd.random() < 0.4:
        rec.set("x", rnd.choice([9, 10, 0x136, 0x137]))
        rec.set("y", rnd.choice([0x11, 0x12, 0xbe, 0xbf]))


CASES = {
    0x00: hatch_entity, 0x04: hatch_entity, 0x07: hatch_entity, 0x0a: hatch_entity,
    0x01: None,
    0x02: timer_entity,
    0x03: swarm_entity,
    0x09: timer_entity,
    0x0d: shot_entity,
}


@pytest.mark.parametrize("think", sorted(CASES), ids=lambda t: f"think_{t:02x}")
@pytest.mark.parametrize("case", fuzz_cases(48))
def test_think(machine, oracle, think, case):
    rnd = case_rng(f"think_{think:02x}", case)
    c = oracle.make_case(machine, rnd, think, CASES[think])
    oracle.check(machine, c, think)
