"""Generators and adapters: determinism, invariants, DGROUP round trips."""

from adapters.entities import (ENTITY_COUNT, entities_from_dgroup, entities_to_dgroup)
from adapters.rng import rng_from_dgroup, rng_to_dgroup
from generators import (PLAYFIELD_X, PLAYFIELD_Y, case_rng, entity_table, indexed_page,
                        rng_state, screen_rect)


def test_deterministic():
    a, b = case_rng("x", 3), case_rng("x", 3)
    assert indexed_page(a, "blocks") == indexed_page(b, "blocks")
    assert case_rng("x", 3).random() != case_rng("x", 4).random()


def test_entity_table_invariants():
    rnd = case_rng("entities", 0)
    for _ in range(20):
        table = entity_table(rnd)
        assert len(table) == ENTITY_COUNT
        for rec in table:
            if rec.free:
                assert rec.get("type") == -1
            else:
                assert PLAYFIELD_X[0] <= rec.get("x") < PLAYFIELD_X[1]
                assert PLAYFIELD_Y[0] <= rec.get("y") < PLAYFIELD_Y[1]


def test_rect_inside_screen():
    rnd = case_rng("rect", 0)
    for _ in range(200):
        x0, y0, x1, y1 = screen_rect(rnd, 2, 2)
        assert 0 <= x0 < x1 < 318 and 0 <= y0 < y1 < 198


def test_adapter_round_trips(machine):
    rnd = case_rng("roundtrip", 0)
    table = entity_table(rnd, n_active=30)
    entities_to_dgroup(machine, table)
    assert [r.raw for r in entities_from_dgroup(machine)] == [r.raw for r in table]
    st = rng_state(rnd)
    rng_to_dgroup(machine, st)
    assert rng_from_dgroup(machine) == st
