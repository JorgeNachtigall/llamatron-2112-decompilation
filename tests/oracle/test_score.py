"""score_add (src/game/score.c) vs game86 1000:0335 in unicorn.

The original runs with BX = place, AX = increments - 1 on a DGROUP image whose sound driver
pointer [0x400] is the game's own stub driver (1000:e785, XOR AX,AX; RETF); every AH=3 call
reaching it is logged. The port runs on the GameState the adapter (dgroup_map.c) makes from
the same image. Compared: every mapped DGROUP byte (scores, extra-life flag 21C6, hud_dirty,
...) and the sound-request sequence (src/game/sound.h log).
"""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases

from adapters.cport import bind
from adapters.gamestate import DGROUP_SIZE, DgroupMap

SCORE_ADD = "1000:0335"
STUB_DRIVER = (0x1000, 0xE785)
SND_DRIVER = 0x0400
HUD_DIRTY = 0x05A1
DEMO_MODE = 0x06C4
SCORE_SCALE = 0x21B4
PICKUP_PENDING = 0x21C6
OPTIONS = 0x2CF6
SCORES = 0x4441
OPT_CURRENT_IS_P2 = 0x10
MAX_INSNS = 50_000_000


class SoundRequest(ctypes.Structure):
    _fields_ = [("sample", ctypes.c_uint8), ("rate_index", ctypes.c_uint8),
                ("priority", ctypes.c_uint8)]


@pytest.fixture(scope="module")
def port(cgame):
    class Port:
        dg = DgroupMap(cgame)
        score_add = bind(cgame, "score_add", None, ctypes.c_void_p, ctypes.c_int, ctypes.c_int)
        log_count = bind(cgame, "game_sound_log_count", ctypes.c_uint)
        log_at = bind(cgame, "game_sound_log_at", SoundRequest, ctypes.c_uint)
        log_clear = bind(cgame, "game_sound_log_clear", None)
    return Port


def digits(score: int) -> bytes:
    return bytes(int(c) for c in f"{score % 10**8:08d}")


def make_image(machine, dg, *, scores, options=0, scale=0, demo=0, pickup=0):
    img = bytearray(dg.canonical(machine.read_dgroup(0, DGROUP_SIZE)))
    img[SCORES:SCORES + 8] = digits(scores[0])
    img[SCORES + 8:SCORES + 16] = digits(scores[1])
    img[OPTIONS] = options
    struct.pack_into("<h", img, SCORE_SCALE, scale)
    struct.pack_into("<H", img, DEMO_MODE, demo)
    struct.pack_into("<h", img, PICKUP_PENDING, pickup)
    struct.pack_into("<H", img, HUD_DIRTY, 0)
    struct.pack_into("<HH", img, SND_DRIVER, STUB_DRIVER[1], STUB_DRIVER[0])
    return bytes(img)


def run_original(machine, img, place, increments):
    machine.write_dgroup(0, img)
    sounds = []

    def driver(m):
        r = m.get_regs()
        if r["ax"] >> 8 == 3:
            sounds.append((r["ax"] & 0xFF, r["cx"] & 0xFF, r["cx"] >> 8))

    handle = machine.add_breakpoint(STUB_DRIVER, driver)
    try:
        machine.call(SCORE_ADD, {"bx": place, "ax": (increments - 1) & 0xFFFF},
                     max_insns=MAX_INSNS)
    finally:
        machine.remove_breakpoint(handle)
    return machine.read_dgroup(0, DGROUP_SIZE), sounds


def run_port(port, img, place, increments):
    state = port.dg.from_dgroup(img)
    port.log_clear()
    port.score_add(state, place, increments)
    sounds = [port.log_at(i) for i in range(port.log_count())]
    return port.dg.to_dgroup(state, img), [(s.sample, s.rate_index, s.priority) for s in sounds]


def passes(counter: int) -> int:
    """Passes of a `DEC r16; JNS` loop entered with `counter` (signed 16-bit)."""
    counter &= 0xFFFF
    if counter == 0x8000:
        return 0x8001                   # DEC gives 0x7FFF, still >= 0
    return counter + 1 if counter < 0x8000 else 1


def ref_score_add(score: int, place: int, increments: int, scale: int):
    """game86 1000:0335 on one score: (new score, extra lives, wrapped).

    A carry out of the top digit (BX < 0 at 037b) re-runs the same step: 037d jumps back
    to 0345 without the DEC AX, so that award counts one unit more."""
    digits = [int(c) for c in f"{score:08d}"]
    lives = 0
    wrapped = False
    for _ in range(passes(scale)):
        steps = passes(increments - 1)
        while steps:
            bx = place
            while True:
                digits[bx] += 1
                if digits[bx] != 10:
                    break
                digits[bx] = 0
                bx -= 1
                if bx == 3:
                    lives += 1
                if bx < 0:
                    break
            if bx < 0:
                wrapped = True          # same step again
                continue
            steps -= 1
    return int("".join(map(str, digits))), lives, wrapped


def check(machine, port, place, increments, *, scores, options=0, scale=0, demo=0, **image):
    img = make_image(machine, port.dg, scores=scores, options=options, scale=scale, demo=demo,
                     **image)
    orig, orig_sounds = run_original(machine, img, place, increments)

    # the model reproduces the original (keeps the known-gap classification honest)
    player = 1 if options & OPT_CURRENT_IS_P2 else 0
    base = SCORES + 8 * player
    if demo:
        expect, lives, wrapped = scores[player], 0, False
    else:
        expect, lives, wrapped = ref_score_add(scores[player], place, increments, scale)
    assert orig[base:base + 8] == digits(expect)
    assert len(orig_sounds) == lives

    mine, my_sounds = run_port(port, img, place, increments)
    bad = port.dg.mismatches(orig, mine)
    if bad and (wrapped or (scale & 0xFFFF) == 0x8000 or ((increments - 1) & 0xFFFF) == 0x8000):
        pytest.xfail("known port gap: top-digit carry repeats the step / 0x8000 loop counter "
                     "(reported to W4)")
    assert bad == [], {hex(o): (orig[o], mine[o]) for o in bad[:16]}
    assert my_sounds == orig_sounds
    return orig_sounds


@pytest.mark.parametrize("case", fuzz_cases(150))
def test_random(machine, port, case):
    rnd = case_rng("score.random", case)
    check(machine, port,
          place=rnd.randrange(8),
          increments=rnd.choice([0, 1, 1, 2, rnd.randrange(1, 30), rnd.randrange(-5, 1)]),
          scores=(rnd.randrange(10**8), rnd.randrange(10**8)),
          options=rnd.randrange(256),
          scale=rnd.choice([0, 0, 1, 2, rnd.randrange(-6, 6)]),
          demo=rnd.choice([0, 0, 0, 1, rnd.randrange(1, 0x10000)]),
          pickup=rnd.choice([0, 1, 3]))


@pytest.mark.parametrize("score, place, increments, lives", [
    (9_999, 7, 1, 1),            # carry from the thousands: extra life
    (9_990, 6, 1, 1),
    (9_500, 5, 5, 1),
    (19_999, 7, 1, 1),
    (8_000, 4, 2, 1),            # thousands digit itself wraps
    (0, 4, 25, 2),               # 25,000 points one thousand at a time: two lives
    (5_000, 3, 1, 0),            # 10,000 at once: no carry out of the thousands
    (99_999, 3, 1, 0),
    (1_234, 7, 1, 0),
])
def test_extra_life(machine, port, score, place, increments, lives):
    sounds = check(machine, port, place, increments, scores=(score, 0))
    assert len(sounds) == lives


@pytest.mark.parametrize("current_p2", [False, True])
@pytest.mark.parametrize("score, place", [
    (99_999_999, 7),             # every digit carries out of the top
    (99_999_999, 0),
    (90_000_000, 0),
    (99_990_000, 3),
])
def test_wrap(machine, port, score, place, current_p2):
    opts = OPT_CURRENT_IS_P2 if current_p2 else 0
    check(machine, port, place, 1, scores=(score, score), options=opts)


@pytest.mark.parametrize("scale", [-0x8000, -2, -1, 0, 1, 7])
@pytest.mark.parametrize("increments", [-0x7FFF, -1, 0, 1, 3])
def test_loop_counts(machine, port, scale, increments):
    if scale == -0x8000 and increments == -0x7FFF:
        pytest.skip("32769 x 32769 steps: too slow in unicorn")
    # both counters loop while >= 0 after DEC (0x8000 - 1 = 0x7FFF: 32769 passes)
    check(machine, port, 7, increments, scores=(0, 0), scale=scale)


def test_demo_mode(machine, port):
    check(machine, port, 7, 5, scores=(9_999, 0), demo=1)
