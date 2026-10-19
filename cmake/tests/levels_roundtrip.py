"""Rebuilds every original level script from data/levels.dat and compares it with game86 DGROUP.

Usage: levels_roundtrip.py <generated>/data/levels.dat <generated>/exe/game86.unpacked
"""
import struct
import sys

DGROUP = (0x3559 - 0x1000) * 16
LEVEL_TABLE = 0x2BDA

data = open(sys.argv[1], "rb").read()
image = open(sys.argv[2], "rb").read()

assert data[:4] == b"LLVL"
version, count = struct.unpack_from("<HH", data, 4)
pos = 8
levels = [struct.unpack_from("<HHHBBH", data, pos + i * 10) for i in range(count)]
pos += count * 10
(op_count,) = struct.unpack_from("<H", data, pos)
pos += 2
ops = [data[pos + i * 6:pos + i * 6 + 6] for i in range(op_count)]
pos += op_count * 6
(spawn_count,) = struct.unpack_from("<H", data, pos)
pos += 2
spawns = [data[pos + i * 2:pos + i * 2 + 2] for i in range(spawn_count)]
pos += spawn_count * 2
clobber = struct.unpack_from("<3H", data, pos)
pos += 6
variants = data[pos:pos + 16]
assert version == 3 and pos + 16 == len(data), "trailing bytes"
assert variants == image[DGROUP + 0x0808:DGROUP + 0x0810] + image[DGROUP + 0x0939:DGROUP + 0x0941], "spawn/warp sprites"
# the clobbered pairs are the 4 bytes at think_13's code offset (DS:1FD2 entry 0x13) + 8
target = struct.unpack_from("<H", image, DGROUP + 0x1FD2 + 0x13 * 2)[0] + 8
if clobber[0] != 0xFFFF:
    lv = levels[clobber[0]]
    found = [i for i in range(lv[4]) if image[DGROUP + target:DGROUP + target + 2] == spawns[lv[5] + i]]
    assert clobber[1] in found and clobber[2] == 2, ("clobber", clobber, found)

bad = 0
for index, (ds_offset, first_op, n_ops, wave_type, n_spawns, first_spawn) in enumerate(levels):
    script = bytearray()
    for op in ops[first_op:first_op + n_ops]:
        script.append(0xFF - op[0])
        script += op[2:2 + op[1]]
    script.append(wave_type)
    for pair in spawns[first_spawn:first_spawn + n_spawns]:
        script += pair
    script += b"\xff\xff"
    pointer = struct.unpack_from("<H", image, DGROUP + LEVEL_TABLE + index * 2)[0]
    original = image[DGROUP + ds_offset:DGROUP + ds_offset + len(script)]
    if pointer != ds_offset or original != bytes(script):
        bad += 1
        print(f"level {index} at DS:{ds_offset:04x} differs")
print(f"{count} levels, {op_count} ops, {spawn_count} spawns; mismatches {bad}")
sys.exit(1 if bad else 0)
