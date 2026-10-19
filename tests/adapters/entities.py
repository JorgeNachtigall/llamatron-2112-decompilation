"""Entity table <-> game86 DGROUP layout (docs/re-notes-game86.md §4).

70 records x 64 bytes at DS:48C1 (the same layout is used by g_companion at 4881).
Fields follow W8's map; unnamed bytes stay opaque (handler-specific). Extend FIELDS as
the layout is understood, and keep this file in sync with tests/adapters/dgroup_map.c.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

ENTITY_TABLE = 0x48C1
ENTITY_COUNT = 70
ENTITY_SIZE = 0x40

# name: (offset in record, struct format) -- docs/re-notes-game86.md §8.3
FIELDS = {
    "type": (0x00, "<h"),          # < 0 free; low byte think index, high byte hatch type
    "x": (0x02, "<h"),             # sprite centre, screen pixels
    "y": (0x04, "<h"),
    "state": (0x06, "<H"),         # animate/draw handler (tbl_anim_handlers), 0x12 = idle
    "vx": (0x08, "<h"),
    "vy": (0x0A, "<h"),
    "sprite": (0x0C, "<h"),        # sprite bank index; negative = use sprite_ptr
    "scale": (0x0E, "<H"),         # draw scale / hatch countdown
    "solid": (0x10, "<H"),         # shots and players collide only when non-zero
    "timer": (0x14, "<H"),
    "hp": (0x18, "<h"),
    "kill_action": (0x1E, "<H"),   # tbl_hit_kill_actions
    "sub_mode": (0x20, "<H"),
    "anim_toggle": (0x28, "<H"),
    "sprite_ptr": (0x2C, "<H"),
    "extra_sprite": (0x32, "<H"),  # drawn after the anim handler, 0 = none
    "frame": (0x36, "<H"),
    "kind": (0x3A, "<H"),          # index into g_type_counts
    "death_sound": (0x3C, "<H"),   # low byte sample, high byte rate index
    "hit_handler": (0x3E, "<H"),   # tbl_hit_handlers (CS:0C04)
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
