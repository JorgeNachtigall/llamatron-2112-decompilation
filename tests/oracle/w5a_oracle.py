"""Shared setup for the W5a oracle tests (src/game/entities/: enemy_init.c, hit.c,
kill_actions.c, pickups.c, spawn.c).

A case is a random but valid DGROUP image (made by think_oracle.ThinkOracle.make_case and
tightened here). The original routine runs in unicorn with the registers its real caller
passes; the port runs the matching C function on the GameState that the C adapter builds from
the same image. Both images are compared over every DGROUP byte the adapter maps, and the
sound-driver requests are compared too.
"""

from __future__ import annotations

import ctypes
import struct
from dataclasses import dataclass

from adapters.cport import bind
from adapters.entities import ENTITY_COUNT, ENTITY_SIZE, ENTITY_TABLE
from adapters.gamestate import SCRATCH
from think_oracle import SND_DRIVER, STUB_SEG, ThinkOracle, w16

INIT_TABLE = 0x2CA4
INIT_COUNT = 41
HIT_TABLE = 0x0C04          # CS
HIT_COUNT = 18
PICKUP_TABLE = 0x0F7E       # CS
PICKUP_COUNT = 10
KILL_ACTION_COUNT = 12

SHOTS = 0x68C9
SHOT_SIZE = 16
SHOT_COUNT = 15
HIT_X, HIT_Y = 0x0413, 0x0417     # the shot's new position, set by shots_update
SHOTS_UPDATE_DX = 0x60            # DX when shots_update calls a hit handler (1000:0c55)

# DS offsets (docs/re-notes-game86.md §8; state.h)
LIVES = 0x21A6
WAVE = 0x21A8
LEVEL = 0x21AA
WAVE_TYPE = 0x21B0
SCORE_SCALE = 0x21B4
SCRIPT_PARAMS = 0x21B6      # 7 words
POWERUP = 0x21C4
PICKUP_PENDING = 0x21C6
BONUS_COUNTDOWN = 0x21C8
PARAM_21CA = 0x21CA
LEVEL_FLAGS = 0x233E
SHOTS_BOUNCE = 0x0650
POWERUP_0652 = 0x0652
KILLS_TOWARD_BONUS = 0x063A
SPEED_ACCUM = 0x0636        # 32-bit
SPAWN_COUNT_0804 = 0x0804
SPAWN_SPRITE = 0x0806
TUNING = {0x0812: None, 0x0816: None, 0x0818: None, 0x0820: None, 0x0826: None,
          0x0828: None, 0x082A: None, 0x082C: None, 0x082E: None, 0x0830: None,
          0x086E: None}
SPAWN_PHASE = 0x0814
ORBIT_ANGLE = 0x081A        # 32-bit
ORBIT_NEXT_PHASE = 0x081E
SEGMENT_SPRITE = 0x0824
UNHATCHED = 0x091B
DEBRIS_ALT = 0x0876
RNG_TABLE, RNG_TABLE_SIZE, RNG_INDEX = 0x6AC9, 512, 0x39E9
FLASH_LIST = 0x06C7         # pointer into the list being shown
FLASH_LISTS = (0x06C9, 0x06CD, 0x06FD, 0x0707, 0x0713, 0x0721, 0x0729, 0x072D)

# The enemy shot template is scratch for the port (EnemyShot parameters).
W5A_SCRATCH = SCRATCH | frozenset(range(0x0419, 0x0423))

HIT_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)
INIT_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p)


class Point16(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int16), ("y", ctypes.c_int16)]


class EntityHit(ctypes.Structure):
    _fields_ = [("shot", ctypes.c_void_p), ("smart_bomb", ctypes.c_bool), ("at", Point16)]


def flash_in_progress(rnd, image: bytes) -> int:
    """[0x6C7]: none, or a flash list part-way through (from its start to its 0 terminator),
    so a handler that starts a new flash must also restart the read position."""
    if rnd.random() < 0.4:
        return 0
    start = rnd.choice(FLASH_LISTS)
    length = 0
    while u16(image, start + 2 * length) != 0:
        length += 1
    return start + 2 * rnd.choice([0, length, rnd.randrange(length + 1)])


def u16(image: bytes, off: int) -> int:
    return struct.unpack_from("<H", image, off)[0]


def slot_address(slot: int) -> int:
    return ENTITY_TABLE + ENTITY_SIZE * slot


@dataclass
class Case:
    image: bytes
    slot: int                   # the entity the routine works on
    regs: dict


class W5aOracle(ThinkOracle):
    def __init__(self, cgame):
        super().__init__(cgame)
        self.lib = cgame
        self.init_table = (ctypes.c_void_p * INIT_COUNT).in_dll(cgame, "enemy_init_handlers")
        self.hit_table = (ctypes.c_void_p * HIT_COUNT).in_dll(cgame, "entity_hit_handlers")
        self.pickup_collect = bind(cgame, "pickup_collect", None, ctypes.c_void_p,
                                   ctypes.c_void_p, ctypes.c_uint)
        bank = bind(cgame, "asset_sprite_bank", ctypes.c_void_p, ctypes.c_int)(0)
        find = bind(cgame, "sprite_bank_find", ctypes.c_int, ctypes.c_void_p, ctypes.c_uint16)
        self.frame_of_offset = lambda off: find(bank, off)
        self._shot_offset = None

    # ---- state generation -------------------------------------------------------------

    def random_image(self, machine, rnd, think: int = 0, entity=None, **extra) -> Case:
        """ThinkOracle's random image, plus the wave state the W5a routines read."""
        base = self.make_case(machine, rnd, think, entity, near_ship=0.1)
        image = bytearray(base.image)
        words = {
            WAVE: rnd.randrange(3),             # 0..2: the waves levels.dat uses
            LEVEL: rnd.randrange(100),
            WAVE_TYPE: rnd.choice([0, 0, 1, rnd.randrange(0x10000)]),
            SCORE_SCALE: rnd.choice([0, 0, 1]),
            POWERUP: rnd.randrange(0x10000),
            PICKUP_PENDING: rnd.choice([0, 0, 0, rnd.randrange(1, 11)]),
            BONUS_COUNTDOWN: rnd.randrange(4),
            PARAM_21CA: rnd.choice([0, 0, 1]),
            LEVEL_FLAGS: rnd.choice([0, 0x32, 0xFFFF, 0x8000, rnd.randrange(0x10000)]),
            SHOTS_BOUNCE: rnd.randrange(2),
            KILLS_TOWARD_BONUS: rnd.choice([0, 198, 199, rnd.randrange(200)]),
            SPAWN_SPRITE: rnd.randrange(0x150),
            SPAWN_PHASE: rnd.randrange(0x10000),
            ORBIT_NEXT_PHASE: rnd.randrange(0x10000),
            SEGMENT_SPRITE: rnd.randrange(0x150),
            UNHATCHED: rnd.randrange(40),
            SPAWN_COUNT_0804: rnd.randrange(40),
            DEBRIS_ALT: rnd.randrange(2),
            SPEED_ACCUM: rnd.randrange(0x10000),
            SPEED_ACCUM + 2: rnd.randrange(4),
        }
        for i in range(7):
            words[SCRIPT_PARAMS + 2 * i] = rnd.choice([0, 1, 2, 3, rnd.randrange(0x10000)])
        for off in TUNING:
            words[off] = rnd.choice([0, 1, 0x40, rnd.randrange(0x10000)])
        words.update(extra)
        for off, v in words.items():
            image[off:off + 2] = w16(v)
        # A table like the start-up shuffle builds (two copies of 0..255, permuted): the
        # spawn-position loops retry until the RNG gives a usable value.
        table = [i & 0xFF for i in range(RNG_TABLE_SIZE)]
        rnd.shuffle(table)
        image[RNG_TABLE:RNG_TABLE + RNG_TABLE_SIZE] = bytes(table)
        image[RNG_INDEX:RNG_INDEX + 2] = w16(rnd.randrange(RNG_TABLE_SIZE))
        image[FLASH_LIST:FLASH_LIST + 2] = w16(flash_in_progress(rnd, image))
        return Case(self.dg.canonical(bytes(image)), base.slot, {})

    # ---- running ----------------------------------------------------------------------

    def run_original(self, machine, case: Case, entry: str):
        machine.write_dgroup(0, case.image)
        machine.write(STUB_SEG, 0, b"\xcb")
        machine.write_dgroup(SND_DRIVER, struct.pack("<2H", 0, STUB_SEG))
        heard = []
        bp = machine.add_breakpoint((STUB_SEG, 0), lambda m: heard.append(m.get_regs()))
        try:
            machine.call(entry, regs=case.regs)
        finally:
            machine.remove_breakpoint(bp)
        sounds = [(r["ax"] & 0xFF, r["cx"] & 0xFF, r["cx"] >> 8) for r in heard
                  if r["ax"] >> 8 == 3]
        return machine.read_dgroup(0, 0x10000), sounds

    def port_sounds(self):
        out = []
        for i in range(self.sound_count()):
            s = self.sound_at(i)
            out.append((s.sample, s.rate_index, s.priority))
        return out

    def shot_pointer(self, state, index: int) -> int:
        """Address of shots[index] inside the opaque GameState, found once by probing the
        adapter with marker values (the struct layout is the C side's business)."""
        if self._shot_offset is None:
            image = bytearray(self.dg.to_dgroup(self.dg.new_state()))
            marker = struct.pack("<4H", 0x7A31, 0x6B42, 0x5C53, 0x4D64)
            image[SHOTS:SHOTS + 8] = marker
            probe = self.dg.from_dgroup(bytes(image))
            raw = probe.raw
            assert raw.count(marker) == 1, "shot marker not unique in GameState"
            self._shot_offset = raw.index(marker)
        return ctypes.addressof(state) + self._shot_offset + index * SHOT_SIZE

    def compare(self, case: Case, want: bytes, got: bytes, want_sounds, got_sounds,
                ignore=W5A_SCRATCH):
        bad = self.dg.mismatches(want, got, ignore=ignore)
        detail = [(hex(o), want[o], got[o]) for o in bad[:16]]
        assert not bad, f"slot {case.slot}: DGROUP differs at {detail}"
        assert got_sounds == want_sounds
