"""W5b line_walk (src/game/entities/movement.c) against game86 1000:32fe, called directly
(AX,BX from, CX,DX to, BP steps -> SI,DI point). The other movement helpers are covered
through the handlers in test_think_basic.py."""

import ctypes

import pytest
from generators import case_rng, fuzz_cases

from adapters.cport import bind


class Point16(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int16), ("y", ctypes.c_int16)]


@pytest.fixture(scope="module")
def line_walk(cgame):
    return bind(cgame, "line_walk", Point16, Point16, Point16, ctypes.c_int16)


def s16(v):
    return (v & 0xFFFF) - 0x10000 if v & 0x8000 else v & 0xFFFF


@pytest.mark.parametrize("case", fuzz_cases(400))
def test_line_walk(machine, line_walk, case):
    rnd = case_rng("line_walk", case)
    x0, y0 = rnd.randrange(0, 320), rnd.randrange(0, 200)
    if rnd.random() < 0.15:     # same point, same row/column, exact diagonals
        x1, y1 = rnd.choice([(x0, y0), (x0, rnd.randrange(200)), (rnd.randrange(320), y0),
                             (x0 + 7, y0 + 7), (x0 - 5, y0 + 5)])
    else:
        x1, y1 = rnd.randrange(0, 320), rnd.randrange(0, 200)
    steps = rnd.choice([0, 0, 1, rnd.randrange(64), rnd.randrange(400), 0xFFFF, 0x8000])
    res = machine.call("1000:32fe", regs={"ax": x0, "bx": y0, "cx": x1 & 0xFFFF,
                                          "dx": y1 & 0xFFFF, "bp": steps})
    got = line_walk(Point16(x0, y0), Point16(x1, y1), s16(steps))
    assert (got.x, got.y) == (s16(res.regs["si"]), s16(res.regs["di"]))
