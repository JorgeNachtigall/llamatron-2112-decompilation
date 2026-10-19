"""W5b player.c against game86: players_tick (1000:16c1..1a01, incl. enemy_aim_update 176c and
companion_update 480e), player_fire (0981), companion_update alone, player_swap_state (5317)
and player_copy_state (5336). The controller bytes (07FC/07FB) live in the engine's InputState
on the port side and are set through input_state()."""

import ctypes
import struct

import pytest
from generators import case_rng, fuzz_cases
from think_oracle import ThinkOracle

from adapters.cport import bind
from adapters.entities import ENTITY_COUNT, ENTITY_SIZE

CONTROL_A, CONTROL_B = 0x07FC, 0x07FB       # bytes: P1 / keyboard, second stick
SHIP_CONTROL_P1 = {"shot": 0x069C, "dir": 0x06AC, "aim": 0x06B6, "move": 0x06BE, "facing": 0x06C2}
SHIP_CONTROL_P2 = {"shot": 0x06A0, "dir": 0x06AE, "aim": 0x06B8, "move": 0x06B0, "facing": 0x07F9}
P2_TRAIL_FLAG = 0x06B4
FIRE_DELAY, FIRE_TURN = 0x0631, 0x0632
STICK_B = 0x09D3
DROID_AIM_TIMER, DROID_VOTES, DROID_QUARRY = 0x0682, 0x409C, 0x0919
CHAIN_TAIL, DEMO_WANDER = 0x06A4, 0x06A8
FOLLOWER_LAG, TRAIL_HEAD = 0x06BA, 0x2CF7
TRAIL_P1, TRAIL_P2, TRAIL_BYTES = 0x6CC9, 0x70C9, 0x400
SHOTS, SHOT_COUNT, SHOT_SIZE = 0x68C9, 15, 16
SPAWNING_DONE = 0x0802
SCORE_SCALE = 0x21B4
DEMO_MODE, INTRO_TIMER, RESPAWN_P1, RESPAWN_P2 = 0x06C4, 0x08FC, 0x07F3, 0x0634
SHIPS = 0x2CFB
PLAYER_A, PLAYER_B, PLAYER_WORDS = 0x21A6, 0x226E, 0x64
OPTIONS = 0x2CF6
CS_SAVED_DS = 0x0255          # 1000:0255, player_copy_state's ES
THINK_LLAMA = 0x05


class InputPrefix(ctypes.Structure):
    """The head of the engine's InputState (src/engine/input.h) up to controls[]."""
    _fields_ = [("device", ctypes.c_int), ("single_stick", ctypes.c_bool),
                ("keys", ctypes.c_uint8 * 6), ("controls", ctypes.c_uint8 * 2)]


@pytest.fixture(scope="module")
def oracle(cgame):
    return ThinkOracle(cgame)


@pytest.fixture(scope="module")
def port(cgame):
    fns = {name: bind(cgame, name, None, ctypes.c_void_p)
           for name in ("players_tick", "player_fire", "companion_update", "player_swap_state",
                        "player_copy_state")}
    controls = ctypes.cast(bind(cgame, "input_state", ctypes.c_void_p)(),
                           ctypes.POINTER(InputPrefix)).contents.controls
    return fns, controls


def control_byte(rnd):
    return rnd.choice([0, 0x80]) | rnd.choice([0, rnd.randrange(16)])


def player_words(rnd, records, slot):
    words = {}
    for c in (SHIP_CONTROL_P1, SHIP_CONTROL_P2):
        words[c["shot"]] = rnd.randrange(-4, 5) & 0xFFFF
        words[c["shot"] + 2] = rnd.randrange(-4, 5) & 0xFFFF
        words[c["dir"]] = rnd.randrange(0x30)
        words[c["aim"]] = rnd.randrange(16)
        words[c["move"]] = rnd.randrange(-3, 4) & 0xFFFF
        words[c["move"] + 2] = rnd.randrange(-3, 4) & 0xFFFF
        words[c["facing"]] = rnd.randrange(8)
    words[P2_TRAIL_FLAG] = rnd.randrange(8)
    words[STICK_B] = rnd.choice([0, 0, 0, 1])
    words[FIRE_TURN] = rnd.randrange(2)
    words[DROID_AIM_TIMER] = rnd.choice([0, 0, 1, rnd.randrange(0x15)])
    words[DROID_VOTES] = rnd.randrange(0x10000)
    words[DROID_VOTES + 2] = rnd.randrange(0x10000)
    words[DROID_QUARRY] = rnd.choice([0xFFFF, 0xFFFE, rnd.randrange(0x45)])
    for off in (CHAIN_TAIL, DEMO_WANDER):
        words[off] = rnd.randrange(0x10, 0x130)
        words[off + 2] = rnd.randrange(0x10, 0xb0)
    words[INTRO_TIMER] = rnd.choice([0, 0, 0, 1])
    words[DEMO_MODE] = rnd.choice([0, 0, 0, 1, 2])
    # trails: heads on entries, head + lag inside the two rings
    for p in range(2):
        words[TRAIL_HEAD + 2 * p] = 8 * rnd.randrange(0x80)
        words[FOLLOWER_LAG + 2 * p] = 8 * rnd.randrange(0x40)
    for base in (TRAIL_P1, TRAIL_P2):
        for off in range(0, TRAIL_BYTES, 8):
            words[base + off] = rnd.randrange(0x10, 0x130)
            words[base + off + 2] = rnd.randrange(0x10, 0xb0)
            words[base + off + 4] = rnd.randrange(8)
    if rnd.random() < 0.3:      # no solid enemy: the droid aims at P1's side
        for r in records:
            r.raw[0x10:0x12] = b"\0\0"
    # some llamas for the droid; sometimes only at the end of its 69-slot search
    for r in records:
        if not r.free and r.get("type") & 0xFF == THINK_LLAMA:
            r.set("type", r.get("type") ^ 1)
    llamas = rnd.sample(range(ENTITY_COUNT), rnd.randrange(0, 4))
    if rnd.random() < 0.2:
        llamas = [rnd.choice([0x44, 0x45])]
    for i in llamas:
        r = records[i]
        if r.free:
            r.raw[:] = bytes(rnd.randrange(256) for _ in range(ENTITY_SIZE))
        r.set("type", THINK_LLAMA | rnd.randrange(46) << 8)
    return words


def fire_words(rnd, records, slot):
    words = player_words(rnd, records, slot)
    words[RESPAWN_P1] = rnd.choice([0, 0, 0, 0, 0xFFFE])
    words[RESPAWN_P2] = rnd.choice([0, 0, 0, 0, 0x64])
    words[SPAWNING_DONE] = rnd.choice([1, 1, 1, 0])
    words[SCORE_SCALE] = rnd.choice([0, 1, 1, 2])
    for i in range(SHOT_COUNT):
        base = SHOTS + SHOT_SIZE * i
        busy = rnd.random() < 0.5
        words[base] = rnd.randrange(0x10, 0x130) if busy else 0xFFFF
    return words


def make_case(oracle, machine, rnd, table):
    c = oracle.make_case(machine, rnd, 0x01, table=table)
    image = bytearray(c.image)
    image[CONTROL_A] = control_byte(rnd)
    image[CONTROL_B] = control_byte(rnd)
    image[FIRE_DELAY] = rnd.choice([0, 0, 1, rnd.randrange(9)])
    if rnd.random() < 0.3:      # ship standing on its newest trail point
        for p, base in enumerate((TRAIL_P1, TRAIL_P2)):
            head = struct.unpack_from("<H", image, TRAIL_HEAD + 2 * p)[0]
            image[base + head:base + head + 4] = image[SHIPS + 4 * p:SHIPS + 4 * p + 4]
    if rnd.random() < 0.3:      # ships at the playfield edges
        struct.pack_into("<4h", image, SHIPS, rnd.choice([0x0f, 0x10, 0x12e, 0x12f]),
                         rnd.choice([0x12, 0x13, 0xba, 0xbb]),
                         rnd.choice([0x0f, 0x10, 0x12e, 0x12f]), rnd.choice([0x12, 0x13, 0xba, 0xbb]))
    return type(c)(oracle.dg.canonical(bytes(image)), c.slot, c.status)


def port_call(port, case, name):
    fns, controls = port
    def call(state):
        controls[0] = case.image[CONTROL_A]
        controls[1] = case.image[CONTROL_B]
        fns[name](ctypes.addressof(state))
    return call


@pytest.mark.parametrize("case", fuzz_cases(256))
def test_players_tick(machine, oracle, port, case):
    rnd = case_rng("players_tick", case)
    c = make_case(oracle, machine, rnd, player_words)
    oracle.check(machine, c, None, entry="1000:16c1", stop="1000:1a01",
                 port_call=port_call(port, c, "players_tick"))


@pytest.mark.parametrize("case", fuzz_cases(192))
def test_player_fire(machine, oracle, port, case):
    rnd = case_rng("player_fire", case)
    c = make_case(oracle, machine, rnd, fire_words)
    image = bytearray(c.image)
    image[FIRE_DELAY] = rnd.choice([0, 0, 0, 1])
    if rnd.random() < 0.6:
        image[OPTIONS] |= 0x01  # two players: the ships take turns with the spread
    c = type(c)(oracle.dg.canonical(bytes(image)), c.slot, c.status)
    oracle.check(machine, c, None, entry="1000:0981", port_call=port_call(port, c, "player_fire"))


@pytest.mark.parametrize("case", fuzz_cases(128))
def test_companion_update(machine, oracle, port, case):
    rnd = case_rng("companion_update", case)
    c = make_case(oracle, machine, rnd, player_words)
    oracle.check(machine, c, None, entry="1000:480e",
                 port_call=port_call(port, c, "companion_update"))


@pytest.mark.parametrize("name, entry", [("player_swap_state", "1000:5317"),
                                         ("player_copy_state", "1000:5336")])
@pytest.mark.parametrize("case", fuzz_cases(16))
def test_player_state(machine, oracle, port, name, entry, case):
    rnd = case_rng(name, case)
    c = make_case(oracle, machine, rnd, player_words)
    image = bytearray(c.image)
    for off in (PLAYER_A, PLAYER_B):
        image[off:off + 2 * PLAYER_WORDS] = bytes(rnd.randrange(256) for _ in range(2 * PLAYER_WORDS))
    c = type(c)(oracle.dg.canonical(bytes(image)), c.slot, c.status)
    machine.write_u16(0x1000, CS_SAVED_DS, machine.dgroup)     # set by the start-up code
    oracle.check(machine, c, None, entry=entry, port_call=port_call(port, c, name))
