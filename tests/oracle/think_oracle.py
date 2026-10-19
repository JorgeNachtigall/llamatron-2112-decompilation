"""Shared setup for the think-handler oracle tests (W5b, src/game/entities/).

One case = a random but valid DGROUP image (entity table, ships, wave state), one entity slot
and one think index. The original handler runs in unicorn with DI = the record and BP = the
loop's slot counter, exactly as entities_think_all (1000:432b) calls it; the port runs
entity_think_handlers[think] on the GameState made from the same image by the C adapter
(tests/adapters/dgroup_map.c). Both results are compared byte for byte over every DGROUP
byte the adapter maps, plus the sequence of sound-driver requests.
"""

from __future__ import annotations

import ctypes
import struct
from dataclasses import dataclass

from adapters.cport import bind
from adapters.entities import ENTITY_COUNT, ENTITY_SIZE, ENTITY_TABLE, EntityRecord
from adapters.gamestate import DgroupMap
from generators import PLAYFIELD_X, PLAYFIELD_Y, entity_table

THINK_TABLE = 0x1FD2
THINK_COUNT = 46

# DS offsets (docs/re-notes-game86.md §8)
SND_DRIVER = 0x0400
SMART_BOMB = 0x0615
ENEMIES_ALIVE = 0x062F
RESPAWN_P2 = 0x0634
DEMO_MODE = 0x06C4
FLASH_LIST = 0x06C7
FLASH_LISTS = (0x06C9, 0x06CD, 0x06FD, 0x0707, 0x0713, 0x0721, 0x0729, 0x072D)  # 0-terminated
RESPAWN_P1 = 0x07F3
WAVE_RESULT = 0x0810
DEATH_SOUND_TIMER = 0x0872
INTRO_TIMER = 0x08FC
SHIELD_TIMER = 0x0907
SWARM_COUNT = 0x091D
BOUNCED = 0x0925
FREE_SLOTS = 0x0927
SPEED_X, SPEED_Y = 0x21AC, 0x21AE
REAIM_PERIOD = 0x21B2
BONUS_COUNTDOWN = 0x21C8
OPTIONS = 0x2CF6
SHIPS = 0x2CFB            # P1 x, y, P2 x, y
TARGET = 0x2D03
TICKS = 0x39E6
RNG_TABLE, RNG_TABLE_SIZE, RNG_INDEX = 0x6AC9, 512, 0x39E9
SINE8 = 0x4141            # filled by the data loader at start-up, not in the EXE image

STUB_SEG = 0x9800
OPT_DOUBLE = 0x02

THINK_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p)


def w16(v: int) -> bytes:
    return struct.pack("<H", v & 0xFFFF)


def think_address(machine, think: int) -> int:
    return struct.unpack("<H", machine.read_dgroup(THINK_TABLE + 2 * think, 2))[0]


@dataclass
class ThinkCase:
    image: bytes
    slot: int
    status: tuple[int, int] = (0, 0)    # driver AH=6 answer: (sample playing, priority)

    @property
    def di(self) -> int:
        return ENTITY_TABLE + ENTITY_SIZE * self.slot

    @property
    def bp(self) -> int:
        return ENTITY_COUNT - 1 - self.slot


def flash_in_progress(rnd, image):
    """No flash, or a position inside (or at the end of) one of the flash lists, so a
    handler that starts a flash without rewinding it shows up (as in w5a_oracle)."""
    if rnd.random() < 0.4:
        return 0
    start = rnd.choice(FLASH_LISTS)
    length = 0
    while struct.unpack_from("<H", image, start + 2 * length)[0] != 0:
        length += 1
    return start + 2 * rnd.choice([0, length, rnd.randrange(length + 1)])


class ThinkOracle:
    def __init__(self, cgame):
        self.dg = DgroupMap(cgame)
        self.table = (ctypes.c_void_p * THINK_COUNT).in_dll(cgame, "entity_think_handlers")
        sine = bind(cgame, "asset_sine", ctypes.c_void_p)()
        assert sine, "extracted data not loaded"
        self.sine8 = ctypes.string_at(sine, 256)
        bank = bind(cgame, "asset_sprite_bank", ctypes.c_void_p, ctypes.c_int)(0)
        find = bind(cgame, "sprite_bank_find", ctypes.c_int, ctypes.c_void_p, ctypes.c_uint16)
        self.frame_of_offset = lambda off: find(bank, off)
        self.sound_count = bind(cgame, "game_sound_log_count", ctypes.c_uint)
        self.sound_clear = bind(cgame, "game_sound_log_clear", None)

        class SoundRequest(ctypes.Structure):
            _fields_ = [("sample", ctypes.c_uint8), ("rate_index", ctypes.c_uint8),
                        ("priority", ctypes.c_uint8)]
        self.sound_at = bind(cgame, "game_sound_log_at", SoundRequest, ctypes.c_uint)

        class SoundStatus(ctypes.Structure):
            _fields_ = [("playing", ctypes.c_uint8), ("priority", ctypes.c_uint8)]
        self.SoundStatus = SoundStatus
        self.override_status = bind(cgame, "game_sound_override_status", None, ctypes.c_bool,
                                    SoundStatus)

    # ---- state generation -------------------------------------------------------------

    def make_case(self, machine, rnd, think: int, entity=None, near_ship: float = 0.3,
                  extra: dict[int, int] | None = None, table=None) -> ThinkCase:
        """A random image with entity `slot` given think index `think`. `entity(rnd, rec)`
        tightens the record's handler-specific fields; `table(rnd, records, slot)` may edit
        the other records and returns extra {DS offset: word}; `extra` = {DS offset: word}."""
        image = bytearray(machine.read_dgroup(0, 0x10000))
        image[SINE8:SINE8 + 256] = self.sine8
        # The start-up shuffle fills the RNG table (zeros in the EXE image); without it every
        # random branch would always go the same way.
        rng_table = [i & 0xFF for i in range(RNG_TABLE_SIZE)]
        rnd.shuffle(rng_table)
        image[RNG_TABLE:RNG_TABLE + RNG_TABLE_SIZE] = bytes(rng_table)
        records = entity_table(rnd, n_think=THINK_COUNT)
        slot = rnd.randrange(ENTITY_COUNT)
        rec = records[slot]
        if rec.free:
            records[slot] = rec = EntityRecord(bytearray(rnd.randrange(256) for _ in range(ENTITY_SIZE)))
            rec.set("x", rnd.randrange(*PLAYFIELD_X))
            rec.set("y", rnd.randrange(*PLAYFIELD_Y))
        rec.set("type", rnd.randrange(46) << 8 | think)
        if entity:
            entity(rnd, rec)
        table_words = table(rnd, records, slot) if table else {}
        image[ENTITY_TABLE:ENTITY_TABLE + ENTITY_COUNT * ENTITY_SIZE] = b"".join(
            bytes(r.raw) for r in records)

        ships = []
        for _ in range(2):
            if rnd.random() < near_ship:
                ships += [rec.get("x") + rnd.randrange(-18, 19), rec.get("y") + rnd.randrange(-12, 13)]
            else:
                ships += [rnd.randrange(*PLAYFIELD_X), rnd.randrange(*PLAYFIELD_Y)]
        image[SHIPS:SHIPS + 8] = struct.pack("<4h", *ships)
        options = rnd.randrange(256) & 0x17
        image[OPTIONS] = options
        target = ships[2:4] if (options & OPT_DOUBLE) and (ENTITY_COUNT - 1 - slot) & 1 else ships[0:2]
        image[TARGET:TARGET + 4] = struct.pack("<2h", *target)

        words = {
            RESPAWN_P1: rnd.choice([0, 0, 0, 0xFFFE, 0x64, rnd.randrange(0x10000)]),
            RESPAWN_P2: rnd.choice([0, 0, 0, 0xFFFE, 0x64, rnd.randrange(0x10000)]),
            SHIELD_TIMER: rnd.choice([0, 0, 0, rnd.randrange(1, 500)]),
            SMART_BOMB: rnd.choice([0, 0, 0, 1]),
            DEMO_MODE: rnd.choice([0] * 7 + [1]),
            WAVE_RESULT: rnd.choice([0, 0, 0, 2, 1]),
            ENEMIES_ALIVE: rnd.randrange(0, 30),
            INTRO_TIMER: rnd.choice([0, 0, 0, rnd.randrange(1, 0x33)]),
            SPEED_X: rnd.randrange(1, 9),
            SPEED_Y: rnd.randrange(1, 9),
            REAIM_PERIOD: rnd.randrange(3, 40),
            TICKS: rnd.randrange(0x10000),
            RNG_INDEX: rnd.randrange(RNG_TABLE_SIZE),
            FREE_SLOTS: rnd.randrange(0x46),
            SWARM_COUNT: rnd.randrange(40),
            DEATH_SOUND_TIMER: rnd.randrange(0x15),
            BONUS_COUNTDOWN: rnd.randrange(3),
            FLASH_LIST: flash_in_progress(rnd, image),
            BOUNCED: rnd.randrange(2),
        }
        words.update(extra or {})
        words.update(table_words)
        for off, v in words.items():
            image[off:off + 2] = w16(v)
        status = (rnd.choice([0, rnd.randrange(1, 33)]), rnd.choice([0, 0x0c, rnd.randrange(256)]))
        return ThinkCase(self.dg.canonical(bytes(image)), slot, status)

    # ---- running both sides -----------------------------------------------------------

    def run_original(self, machine, case: ThinkCase, think: int | None, entry: str | None = None,
                     stop: str | None = None, driver: tuple[int, int] | None = None):
        """Runs think handler `think`, or the code at `entry` (until its RET, or until `stop`).
        `driver` (segment, offset) replaces the stub as [0400], e.g. game86's own sound-off
        stub 1000:e785 (XOR AX,AX; RETF); its requests are recorded the same way."""
        machine.write_dgroup(0, case.image)
        # Stub driver: AH=6 answers the status (AH = sample playing, AL = priority); every
        # other function returns AL = 0 with AH unchanged, as sblaster.bin's (e.g. AH=3 at
        # its 0x10F leaves AX = 0x0300; title_tick_hook 1000:3f8f depends on that).
        playing, priority = case.status
        machine.write(STUB_SEG, 0, bytes([0x80, 0xFC, 0x06,           # CMP AH,6
                                          0x75, 0x04,                 # JNE +4
                                          0xB8, priority, playing,    # MOV AX,status
                                          0xCB,                       # RETF
                                          0xB0, 0x00,                 # MOV AL,0
                                          0xCB]))                     # RETF
        seg, off = driver or (STUB_SEG, 0)
        machine.write_dgroup(SND_DRIVER, struct.pack("<2H", off, seg))
        heard = []
        bp = machine.add_breakpoint((seg, off), lambda m: heard.append(m.get_regs()))
        try:
            if entry is None:
                # entities_think_all calls through SI (1000:4363 MOV SI,[BX+1FD2]; CALL SI), and
                # some handlers' callees depend on it (enemy_shot_drop's 10b0 clobber).
                handler = think_address(machine, think)
                machine.call(f"1000:{handler:04x}",
                             regs={"di": case.di, "bp": case.bp, "si": handler})
            elif stop is None:
                machine.call(entry)
            else:
                machine.run_until(entry, stop, max_insns=2_000_000)
        finally:
            machine.remove_breakpoint(bp)
        sounds = [(r["ax"] & 0xFF, r["cx"] & 0xFF, r["cx"] >> 8) for r in heard
                  if r["ax"] >> 8 == 3]
        return machine.read_dgroup(0, 0x10000), sounds

    def run_port(self, case: ThinkCase, think: int | None, port_call=None):
        """Runs think handler `think`, or `port_call(state)` with the GameState from the image."""
        state = self.dg.from_dgroup(case.image)
        self.sound_clear()
        self.override_status(True, self.SoundStatus(*case.status))
        if port_call is not None:
            port_call(state)
        else:
            fn = self.table[think]
            assert fn, f"think_{think:02x} not in entity_think_handlers"
            THINK_FN(fn)(ctypes.addressof(state), self.dg.entity(state, case.slot))
        self.override_status(False, self.SoundStatus(0, 0))
        sounds = []
        for i in range(self.sound_count()):
            s = self.sound_at(i)
            sounds.append((s.sample, s.rate_index, s.priority))
        return self.dg.to_dgroup(state, case.image), sounds

    def check(self, machine, case: ThinkCase, think: int | None, fixup=None, entry=None,
              stop=None, port_call=None):
        """`fixup(image) -> image` maps the original's representation of a value to the
        port's where the two differ by design; it is applied to the port's input image and to
        the original's result."""
        want, want_sounds = self.run_original(machine, case, think, entry, stop)
        port_case = case
        if fixup:
            want = fixup(want)
            port_case = ThinkCase(fixup(case.image), case.slot, case.status)
        got, got_sounds = self.run_port(port_case, think, port_call)
        bad = self.dg.mismatches(want, got)
        detail = [(hex(o), want[o], got[o]) for o in bad[:16]]
        assert not bad, f"slot {case.slot}: DGROUP differs at {detail}"
        assert got_sounds == want_sounds
        return want
