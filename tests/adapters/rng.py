"""RNG state <-> game86 DGROUP layout.  Offsets are DS-relative (DGROUP 3559)."""

from __future__ import annotations

import struct
from dataclasses import dataclass

PERM_TABLE = 0x6AC9      # 512-byte permutation table used by rng_next_byte
PERM_SIZE = 0x200
PERM_INDEX = 0x39E9      # u16 read position, wraps at 0x1FF
MIXER = 0x0453           # 4 x u16 mixer state (s0, s1, s2, s3) used by the init shuffle
# [0x044D] holds segment 0 (static in the image): the mixer reads the BIOS tick
# word as 0000:046C. Not part of the state, so not written here.


@dataclass
class RngState:
    table: bytes = bytes(PERM_SIZE)
    index: int = 0
    mixer: tuple[int, int, int, int] = (0, 0, 0, 0)


def rng_to_dgroup(m, st: RngState) -> None:
    m.write_dgroup(PERM_TABLE, st.table)
    m.write_dgroup(PERM_INDEX, struct.pack("<H", st.index))
    m.write_dgroup(MIXER, struct.pack("<4H", *st.mixer))


def rng_from_dgroup(m) -> RngState:
    return RngState(table=m.read_dgroup(PERM_TABLE, PERM_SIZE),
                    index=struct.unpack("<H", m.read_dgroup(PERM_INDEX, 2))[0],
                    mixer=struct.unpack("<4H", m.read_dgroup(MIXER, 8)))


# ---- the port's C structs (src/game/rng.h), via ctypes --------------------------------

import ctypes  # noqa: E402


class CRngMixer(ctypes.Structure):
    _fields_ = [("history", ctypes.c_uint16 * 3), ("salt", ctypes.c_uint16)]


class CRng(ctypes.Structure):
    _fields_ = [("table", ctypes.c_uint8 * PERM_SIZE), ("position", ctypes.c_uint16),
                ("mixer", CRngMixer)]


CRngTickFn = ctypes.CFUNCTYPE(ctypes.c_uint16, ctypes.c_void_p, ctypes.c_uint)


class CRngTickSource(ctypes.Structure):
    _fields_ = [("read", CRngTickFn), ("ctx", ctypes.c_void_p)]


def mixer_to_c(mixer: tuple[int, int, int, int]) -> CRngMixer:
    """DGROUP 0453/0455/0457/0459 = history[0..2], salt."""
    c = CRngMixer()
    c.history[0], c.history[1], c.history[2] = mixer[:3]
    c.salt = mixer[3]
    return c


def mixer_from_c(c: CRngMixer) -> tuple[int, int, int, int]:
    return (c.history[0], c.history[1], c.history[2], c.salt)


def rng_to_c(st: RngState) -> CRng:
    c = CRng()
    ctypes.memmove(c.table, st.table, PERM_SIZE)
    c.position = st.index
    c.mixer = mixer_to_c(st.mixer)
    return c


def rng_from_c(c: CRng) -> RngState:
    return RngState(table=bytes(c.table), index=c.position, mixer=mixer_from_c(c.mixer))


class CRngApi:
    """Signatures of the port's RNG functions in llamatron_game_testlib."""

    def __init__(self, lib):
        from adapters.cport import bind
        P = ctypes.POINTER
        self.next_byte = bind(lib, "rng_next_byte", ctypes.c_uint8, P(CRng))
        self.mix = bind(lib, "rng_mix", ctypes.c_uint16, P(CRngMixer), ctypes.c_uint16)
        self.seed = bind(lib, "rng_seed", None, P(CRngMixer), ctypes.c_uint32)
        self.shuffle = bind(lib, "rng_shuffle", None, P(CRng), CRngTickSource)
        self.init_constant = bind(lib, "rng_init_constant", None, P(CRng), ctypes.c_uint32)

    def shuffle_with(self, rng: CRng, schedule) -> None:
        """rng_shuffle with the tick schedule `schedule(call) -> tick low word`."""
        fn = CRngTickFn(lambda _ctx, call: schedule(call) & 0xFFFF)
        self.shuffle(ctypes.byref(rng), CRngTickSource(fn, None))
