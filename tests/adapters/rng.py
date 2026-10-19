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
