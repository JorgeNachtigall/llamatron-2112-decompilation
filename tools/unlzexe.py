#!/usr/bin/env python3
"""Unpack MS-DOS executables compressed with LZEXE 0.91.

Port of the classic unlzexe.c algorithm. Usage:
    unlzexe.py packed.exe unpacked.exe
"""
import struct
import sys


class BitReader:
    """LZEXE interleaves 16-bit control words with literal bytes in one stream."""

    def __init__(self, data, pos):
        self.data = data
        self.pos = pos
        self.buf = self.word()
        self.count = 16

    def byte(self):
        b = self.data[self.pos]
        self.pos += 1
        return b

    def word(self):
        w = struct.unpack_from("<H", self.data, self.pos)[0]
        self.pos += 2
        return w

    def bit(self):
        b = self.buf & 1
        self.count -= 1
        if self.count == 0:
            self.buf = self.word()
            self.count = 16
        else:
            self.buf >>= 1
        return b


def unpack(data):
    if data[:2] != b"MZ" or data[0x1C:0x20] != b"LZ91":
        raise ValueError("not an LZEXE 0.91 executable")
    head = struct.unpack_from("<14H", data, 0)
    hdr_paras, min_alloc, max_alloc, cs = head[4], head[5], head[6], head[11]

    # Info block at the start of the decompressor segment.
    stub = (hdr_paras + cs) << 4
    ip, ocs, sp, ss, packed_paras, _inc_paras, _stub_size, _checksum = struct.unpack_from(
        "<8H", data, stub
    )

    # Relocation table follows the 0x158-byte decompressor stub.
    relocs = []
    pos = stub + 0x158
    off = seg = 0
    while True:
        span = data[pos]
        pos += 1
        if span == 0:
            span = struct.unpack_from("<H", data, pos)[0]
            pos += 2
            if span == 0:
                seg += 0x0FFF
                continue
            if span == 1:
                break
        off += span
        seg += (off & ~0x0F) >> 4
        off &= 0x0F
        relocs.append((off, seg & 0xFFFF))

    # Compressed image sits just below the stub segment.
    bits = BitReader(data, (hdr_paras + cs - packed_paras) << 4)
    out = bytearray()
    while True:
        if bits.bit():
            out.append(bits.byte())
            continue
        if not bits.bit():
            length = ((bits.bit() << 1) | bits.bit()) + 2
            span = bits.byte() - 0x100
        else:
            lo = bits.byte()
            hi = bits.byte()
            span = (lo | ((hi & ~0x07) << 5) | 0xE000) - 0x10000
            length = (hi & 0x07) + 2
            if length == 2:
                length = bits.byte()
                if length == 0:
                    break
                if length == 1:
                    continue  # segment boundary marker
                length += 1
        start = len(out) + span
        for i in range(length):
            out.append(out[start + i])

    # Build a fresh MZ header with the restored relocation table.
    reloc_bytes = b"".join(struct.pack("<HH", o, s) for o, s in relocs)
    header_len = (0x1C + len(reloc_bytes) + 0x1FF) & ~0x1FF
    total = header_len + len(out)
    pages, last = (total + 511) // 512, total % 512

    # Keep the total memory footprint of the packed program.
    packed_load = (len(data) >> 4) - hdr_paras
    new_min = max(0, packed_load + min_alloc - ((len(out) + 15) >> 4))
    new_max = max_alloc if max_alloc == 0xFFFF else max(new_min, max_alloc)

    header = bytearray(header_len)
    struct.pack_into(
        "<2s13H", header, 0, b"MZ", last, pages, len(relocs), header_len >> 4,
        new_min, new_max, ss, sp, 0, ip, ocs, 0x1C, 0,
    )
    header[0x1C:0x1C + len(reloc_bytes)] = reloc_bytes
    return bytes(header) + bytes(out), len(relocs)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    result, nrel = unpack(data)
    with open(sys.argv[2], "wb") as f:
        f.write(result)
    print(f"{sys.argv[1]}: {len(data)} -> {len(result)} bytes, {nrel} relocations")


if __name__ == "__main__":
    main()
