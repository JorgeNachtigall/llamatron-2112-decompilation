"""GameState <-> game86 DGROUP, through the C adapter in the testlib (dgroup_map.h).

Typical oracle comparison of one routine:

    dg = DgroupMap(cgame)
    image = dg.canonical(machine.read_dgroup(0, 0x10000))  # or a generated state
    machine.write_dgroup(0, image)
    state = dg.from_dgroup(image)          # opaque GameState buffer for the C call
    res = machine.call(ENTRY, ...)         # the original
    c_function(state, ...)                 # the port, on a GameState*
    assert dg.mismatches(machine.read_dgroup(0, 0x10000), dg.to_dgroup(state, image)) == []

The GameState is passed to C as an opaque buffer (ctypes.byref works); tests that need a
field from Python read it back through to_dgroup and the DS offset.
"""

from __future__ import annotations

import ctypes

from adapters.cport import bind

DGROUP_SIZE = 0x10000
MASK_UNMAPPED, MASK_FIELD, MASK_DERIVED = 0, 1, 2

# Words that game86 uses as scratch inside routines (not state): ignored in comparisons.
# 0413/0415 (Thorn), 0419..0421 enemy-shot template (W5: scratch, not in GameState).
SCRATCH = frozenset({*range(0x0413, 0x0417), *range(0x0419, 0x0423)})

# 48C1..4B00 (entity slots 0..8) hold the 32 title llamas while the attract/title hooks run;
# the port keeps them inside attract.c, not in GameState.entities. Attract-mode comparisons
# pass this in `ignore`; entities_clear rebuilds the table when a game starts.
ATTRACT_LLAMAS = range(0x48C1, 0x4B01)


class DgroupMap:
    def __init__(self, lib: ctypes.CDLL):
        self.lib = lib
        self._size = bind(lib, "gamestate_size", ctypes.c_size_t)()
        self._from = bind(lib, "gamestate_from_dgroup", None, ctypes.c_void_p, ctypes.c_char_p)
        self._to = bind(lib, "gamestate_to_dgroup", None, ctypes.c_void_p, ctypes.c_void_p)
        self._mask = bind(lib, "dgroup_map_mask", ctypes.c_uint, ctypes.c_void_p)
        self.has_data = bool(bind(lib, "dgroup_map_has_data", ctypes.c_bool)())
        self.entity = bind(lib, "gamestate_entity", ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int)
        buf = ctypes.create_string_buffer(DGROUP_SIZE)
        self.overlaps = self._mask(buf)
        self.mask = buf.raw

    def new_state(self) -> ctypes.Array:
        """A zeroed GameState."""
        return ctypes.create_string_buffer(self._size)

    def from_dgroup(self, dgroup: bytes, state: ctypes.Array | None = None) -> ctypes.Array:
        assert len(dgroup) == DGROUP_SIZE
        state = state if state is not None else self.new_state()
        self._from(state, bytes(dgroup))
        return state

    def to_dgroup(self, state: ctypes.Array, base: bytes | None = None) -> bytes:
        """`base` (zeros by default) with every mapped byte replaced from `state`."""
        buf = ctypes.create_string_buffer(bytes(base) if base is not None else bytes(DGROUP_SIZE),
                                          DGROUP_SIZE)
        self._to(state, buf)
        return buf.raw

    def canonical(self, dgroup: bytes) -> bytes:
        """`dgroup` with its mapped bytes in the encoding the adapter writes back."""
        return self.to_dgroup(self.from_dgroup(dgroup), dgroup)

    def mismatches(self, original: bytes, port: bytes, ignore=SCRATCH) -> list[int]:
        """Mapped DS offsets where the two images differ."""
        return [i for i in range(DGROUP_SIZE)
                if self.mask[i] != MASK_UNMAPPED and original[i] != port[i] and i not in ignore]
