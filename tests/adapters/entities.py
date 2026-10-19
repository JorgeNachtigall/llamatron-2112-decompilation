"""Entity table <-> game86 DGROUP layout (docs/re-notes-game86.md §4).

70 records x 64 bytes at DS:48C1. Only fields the notes confirm are named; the
rest of each record stays opaque bytes until W8 (Quill) maps it. Extend FIELDS as
the layout is understood, and keep this file in sync with tests/adapters/dgroup_map.c.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

ENTITY_TABLE = 0x48C1
ENTITY_COUNT = 70
ENTITY_SIZE = 0x40

# name: (offset in record, struct format)
FIELDS = {
    "type": (0x00, "<h"),       # negative = free slot
    "x": (0x02, "<h"),
    "y": (0x04, "<h"),
    "state": (0x06, "<H"),      # -> animate/draw handler table DS:202E (19 entries)
    "sprite": (0x32, "<H"),     # sprite pointer
    "frame": (0x36, "<H"),
    "kind": (0x3A, "<H"),       # "counts" list DS:220A
    "hit_a": (0x3C, "<H"),      # collision / hit handler index -> CS:0C04 (18 entries)
    "hit_b": (0x3E, "<H"),
}


@dataclass
class EntityRecord:
    raw: bytearray = field(default_factory=lambda: bytearray(ENTITY_SIZE))

    def get(self, name: str) -> int:
        off, fmt = FIELDS[name]
        return struct.unpack_from(fmt, self.raw, off)[0]

    def set(self, name: str, value: int) -> None:
        off, fmt = FIELDS[name]
        struct.pack_into(fmt, self.raw, off, value)

    @property
    def free(self) -> bool:
        return self.get("type") < 0


def entities_to_dgroup(m, records: list[EntityRecord]) -> None:
    assert len(records) == ENTITY_COUNT
    m.write_dgroup(ENTITY_TABLE, b"".join(bytes(r.raw) for r in records))


def entities_from_dgroup(m) -> list[EntityRecord]:
    data = m.read_dgroup(ENTITY_TABLE, ENTITY_COUNT * ENTITY_SIZE)
    return [EntityRecord(bytearray(data[i * ENTITY_SIZE:(i + 1) * ENTITY_SIZE]))
            for i in range(ENTITY_COUNT)]
