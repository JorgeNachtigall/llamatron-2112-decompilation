#!/usr/bin/env python3
"""Unpack MS-DOS executables compressed with Microsoft EXEPACK.

Usage:
    unexepack.py packed.exe unpacked.exe
"""
import struct
import sys

CORRUPT_MSG = b"Packed file is corrupt"


def unpack(data):
    head = struct.unpack_from("<14H", data, 0)
    hdr_len = head[4] << 4
    min_alloc, max_alloc = head[5], head[6]
    image = data[hdr_len:]
    cs_off = head[11] << 4

    # 16-byte header at CS:0 (18 bytes when a skip_len word is present).
    if head[10] == 0x10:
        real_ip, real_cs, _mem, exepack_size, real_sp, real_ss, dest_len = struct.unpack_from(
            "<7H", image, cs_off
        )
        skip_len = 1
        sig_off = cs_off + 14
    elif head[10] == 0x12:
        real_ip, real_cs, _mem, exepack_size, real_sp, real_ss, dest_len, skip_len = (
            struct.unpack_from("<8H", image, cs_off)
        )
        sig_off = cs_off + 16
    else:
        raise ValueError("unexpected EXEPACK entry IP")
    if image[sig_off:sig_off + 2] != b"RB":
        raise ValueError("not an EXEPACK executable")

    # Compressed data runs up to the EXEPACK segment, minus (skip_len - 1) paragraphs.
    src = cs_off - ((skip_len - 1) << 4)
    out_len = dest_len << 4
    buf = bytearray(max(out_len, src))
    buf[:src] = image[:src]

    # Trailing 0xFF padding (up to 15 bytes) precedes the first command.
    for _ in range(15):
        if src > 0 and buf[src - 1] == 0xFF:
            src -= 1
    dst = out_len

    # Commands are read backwards; output is written backwards in place.
    while True:
        cmd = buf[src - 1]
        length = buf[src - 3] | (buf[src - 2] << 8)
        src -= 3
        if cmd & 0xFE == 0xB0:
            fill = buf[src - 1]
            src -= 1
            dst -= length
            buf[dst:dst + length] = bytes([fill]) * length
        elif cmd & 0xFE == 0xB2:
            for _ in range(length):
                src -= 1
                dst -= 1
                buf[dst] = buf[src]
        else:
            raise ValueError(f"bad EXEPACK command {cmd:#04x} at {src:#x}")
        if cmd & 1:
            break
    if dst < src:
        raise ValueError("EXEPACK output overran its input")
    out = bytes(buf[:out_len])

    # Relocations follow the error message: 16 segment groups of (count, offsets...).
    pos = image.find(CORRUPT_MSG, cs_off)
    if pos < 0:
        raise ValueError("EXEPACK relocation table not found")
    pos += len(CORRUPT_MSG)
    relocs = []
    for seg in range(16):
        count = struct.unpack_from("<H", image, pos)[0]
        pos += 2
        for _ in range(count):
            relocs.append((struct.unpack_from("<H", image, pos)[0], seg * 0x1000))
            pos += 2
    if pos != cs_off + exepack_size:
        raise ValueError("EXEPACK relocation table size mismatch")

    reloc_bytes = b"".join(struct.pack("<HH", o, s) for o, s in relocs)
    header_len = (0x1C + len(reloc_bytes) + 0x1FF) & ~0x1FF
    total = header_len + len(out)
    pages, last = (total + 511) // 512, total % 512

    # Keep the total memory footprint of the packed program.
    packed_load = (len(image) + 15) >> 4
    new_min = max(0, packed_load + min_alloc - dest_len)
    new_max = max_alloc if max_alloc == 0xFFFF else max(new_min, max_alloc)

    header = bytearray(header_len)
    struct.pack_into(
        "<2s13H", header, 0, b"MZ", last, pages, len(relocs), header_len >> 4,
        new_min, new_max, real_ss, real_sp, 0, real_ip, real_cs, 0x1C, 0,
    )
    header[0x1C:0x1C + len(reloc_bytes)] = reloc_bytes
    return bytes(header) + out, len(relocs)


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
