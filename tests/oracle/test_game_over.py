"""W5b game over against game86 (Keel's main_sm, 375ee39): 1000:08ee from the game-over
picture through the sound and its wait (091f, 0924), the hi-score marks (0933), the current
player's hiscore_check_insert (093f) and, with a shared start, the swap and the other
player's (0942..094c), up to the table (094f) or to the name entry of a score that made the
table (5e47, after its insertion).

The original runs from 08ee with a far JMP patched over 5e47 to the 094f stop (a breakpoint
notes which way it went); the port is entered at MAIN_SM_TEST_GAME_OVER and stepped (main_sm_step)
until main_sm_state() says NAME_ENTRY or GAME_OVER_TABLE. Compared: which, the DGROUP state
and the sound requests. The drawing (08ee..0910) is test_wave_screens.py's.
"""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases
from test_hiscore import MARKS, OPTIONS, SCORES, TABLE, random_score, random_table
from test_play_draw import CS_G3_SEG, G3_SEG, g3_image, port  # noqa: F401  (fixtures)
from think_oracle import ThinkCase, ThinkOracle

from adapters.cport import bind

CODE0 = 0x1000
CS_SAVED_DS = 0x0255
NAME_ENTRY_CALL, TABLE_SHOW = 0x5E47, 0x094F
OPT_SHARED_START, OPT_CURRENT_IS_P2 = 0x04, 0x10
TEST_OTHER, TEST_GAME_OVER, TEST_NAME_ENTRY, TEST_GAME_OVER_TABLE = 0, 1, 2, 3
MAX_STEPS = 64


@pytest.fixture(scope="module")
def oracle(cgame):
    return ThinkOracle(cgame)


@pytest.fixture(scope="module")
def sm(cgame):
    bind(cgame, "gfx_init", None)()
    return {
        "enter": bind(cgame, "main_sm_enter", None, ctypes.c_void_p, ctypes.c_int),
        "state": bind(cgame, "main_sm_state", ctypes.c_int),
        "step": bind(cgame, "main_sm_step", ctypes.c_bool, ctypes.c_void_p),
    }


def make_case(oracle, machine, rnd):
    img = bytearray(oracle.make_case(machine, rnd, 0x01).image)
    rows = random_table(rnd)
    img[TABLE:TABLE + 160] = b"".join(rows)
    for p in range(2):
        score = random_score(rnd, rows)
        if rnd.random() < 0.25:
            score = rnd.choice([bytes(8), bytes([9] * 8)])
        img[SCORES[p]:SCORES[p] + 8] = score
    struct.pack_into("<2h", img, MARKS, rnd.randrange(-1, 10), rnd.randrange(-1, 10))
    img[OPTIONS] = (rnd.randrange(256) & ~(OPT_SHARED_START | OPT_CURRENT_IS_P2) & 0xFF) \
        | rnd.choice([0, OPT_SHARED_START]) | rnd.choice([0, OPT_CURRENT_IS_P2])
    # The sound driver is idle once the game-over sound is out (the 0924 wait ends).
    return ThinkCase(oracle.dg.canonical(bytes(img)), 0, (0, rnd.randrange(256)))


@pytest.mark.parametrize("case", fuzz_cases(96))
def test_game_over(machine, oracle, sm, g3_image, case):
    rnd = case_rng("game_over", case)
    case_ = make_case(oracle, machine, rnd)

    machine.write_u16(CODE0, CS_SAVED_DS, machine.dgroup)
    machine.write(G3_SEG, 0, g3_image)
    machine.write_u16(CODE0, CS_G3_SEG, G3_SEG)
    machine.write(CODE0, NAME_ENTRY_CALL, b"\xea" + struct.pack("<HH", TABLE_SHOW, CODE0))
    reached = []
    bp = machine.add_breakpoint((CODE0, NAME_ENTRY_CALL), lambda m: reached.append(True))
    try:
        want, want_sounds = oracle.run_original(machine, case_, None, "1000:08ee",
                                                f"1000:{TABLE_SHOW:04x}")
    finally:
        machine.remove_breakpoint(bp)
    want_state = TEST_NAME_ENTRY if reached else TEST_GAME_OVER_TABLE

    def run(state):
        sm["enter"](ctypes.addressof(state), TEST_GAME_OVER)
        for _ in range(MAX_STEPS):          # a step per state; waits only end a frame
            if sm["state"]() in (TEST_NAME_ENTRY, TEST_GAME_OVER_TABLE):
                break
            sm["step"](ctypes.addressof(state))
    got, got_sounds = oracle.run_port(case_, None, run)

    assert sm["state"]() == want_state
    bad = oracle.dg.mismatches(want, got)
    assert not bad, f"DGROUP differs at {[(hex(o), want[o], got[o]) for o in bad[:16]]}"
    assert got_sounds == want_sounds
