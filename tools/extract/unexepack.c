/* Microsoft EXEPACK decoder (as in tools/unexepack.py).
 *
 * The packed image ends at the EXEPACK segment (header CS). That segment starts with a header
 * (real IP, CS, SP, SS, unpacked size in paragraphs, ...) followed by the signature "RB", the
 * unpacker code, the "Packed file is corrupt" message and the relocation table. The compressed
 * data is a list of run-length commands read from the end backwards; output is written backwards
 * into the same buffer. */
#include "unpack.h"

#include <string.h>

static const char corrupt_message[] = "Packed file is corrupt";

enum {
    EXEPACK_CMD_FILL = 0xB0, /* repeat one byte */
    EXEPACK_CMD_COPY = 0xB2, /* copy literal bytes */
    EXEPACK_CMD_LAST = 0x01, /* low bit: final command */
};

/* Header at the start of the EXEPACK segment. */
typedef struct {
    uint16_t real_ip, real_cs;
    uint16_t exepack_size; /* bytes from the segment start to the end of the reloc table */
    uint16_t real_sp, real_ss;
    uint16_t dest_paras;   /* unpacked image size in paragraphs */
    uint16_t skip_paras;   /* only in the 18-byte variant; otherwise 1 */
    size_t signature_offset;
} ExepackHeader;

static bool read_exepack_header(const uint8_t *image, size_t image_size, uint16_t entry_ip,
                                size_t segment, ExepackHeader *out)
{
    size_t words = entry_ip == 0x10 ? 7 : entry_ip == 0x12 ? 8 : 0;
    if (words == 0)
        return fail("unexpected EXEPACK entry IP 0x%x", entry_ip);
    if (segment + words * 2 + 2 > image_size)
        return fail("EXEPACK header past end of file");
    const uint8_t *h = image + segment;
    *out = (ExepackHeader){
        .real_ip = read_u16le(h + 0),
        .real_cs = read_u16le(h + 2),
        .exepack_size = read_u16le(h + 6),
        .real_sp = read_u16le(h + 8),
        .real_ss = read_u16le(h + 10),
        .dest_paras = read_u16le(h + 12),
        .skip_paras = words == 8 ? read_u16le(h + 14) : 1,
        .signature_offset = segment + (words == 8 ? 16 : 14),
    };
    if (memcmp(image + out->signature_offset, "RB", 2) != 0)
        return fail("not an EXEPACK executable");
    return true;
}

bool exepack_detect(const uint8_t *data, size_t size)
{
    MzHeader header;
    if (!mz_parse_header(data, size, &header))
        return false;
    size_t header_size = (size_t)header.header_paras << 4;
    if (header_size > size)
        return false;
    ExepackHeader exepack;
    return read_exepack_header(data + header_size, size - header_size, header.ip,
                               (size_t)header.cs << 4, &exepack);
}

/* Runs the backwards command list in `buf`, whose first `src` bytes hold the packed data. */
static bool expand(ByteBuf *buf, size_t src, size_t dest_size)
{
    for (int i = 0; i < 15 && src > 0 && buf->data[src - 1] == 0xFF; i++)
        src--; /* up to 15 bytes of 0xFF padding precede the first command */
    size_t dst = dest_size;

    for (;;) {
        if (src < 3)
            return fail("EXEPACK command list runs before the start of the image");
        uint8_t command = buf->data[src - 1];
        size_t length = read_u16le(buf->data + src - 3);
        src -= 3;
        switch (command & 0xFE) {
        case EXEPACK_CMD_FILL:
            if (src < 1 || dst < length)
                return fail("EXEPACK fill out of range");
            {
                uint8_t fill = buf->data[--src];
                dst -= length;
                memset(buf->data + dst, fill, length);
            }
            break;
        case EXEPACK_CMD_COPY:
            if (src < length || dst < length)
                return fail("EXEPACK copy out of range");
            for (size_t i = 0; i < length; i++) /* backwards: source and target overlap */
                buf->data[--dst] = buf->data[--src];
            break;
        default:
            return fail("bad EXEPACK command 0x%02x at 0x%zx", command, src);
        }
        if (command & EXEPACK_CMD_LAST)
            break;
    }
    if (dst < src)
        return fail("EXEPACK output overran its input");
    return true;
}

static const uint8_t *find_bytes(const uint8_t *haystack, size_t size, const char *needle)
{
    size_t len = strlen(needle);
    for (size_t i = 0; i + len <= size; i++)
        if (memcmp(haystack + i, needle, len) == 0)
            return haystack + i;
    return NULL;
}

/* After the error message: for each of the 16 64 KB segments, a count and that many offsets. */
static bool read_relocations(const uint8_t *image, size_t image_size, size_t segment,
                             size_t table_end, MzRelocList *relocs)
{
    const uint8_t *message =
        find_bytes(image + segment, image_size - segment, corrupt_message);
    if (!message)
        return fail("EXEPACK relocation table not found");
    size_t pos = (size_t)(message - image) + strlen(corrupt_message);
    for (uint16_t group = 0; group < 16; group++) {
        if (pos + 2 > image_size)
            return fail("EXEPACK relocation table runs past end of file");
        uint16_t count = read_u16le(image + pos);
        pos += 2;
        if (pos + (size_t)count * 2 > image_size)
            return fail("EXEPACK relocation table runs past end of file");
        for (uint16_t i = 0; i < count; i++, pos += 2)
            if (!mz_reloc_push(relocs, read_u16le(image + pos), (uint16_t)(group * 0x1000)))
                return false;
    }
    if (pos != table_end)
        return fail("EXEPACK relocation table size mismatch");
    return true;
}

bool exepack_unpack(const uint8_t *data, size_t size, MzProgram *out)
{
    MzHeader header;
    if (!mz_parse_header(data, size, &header))
        return false;
    size_t header_size = (size_t)header.header_paras << 4;
    if (header_size > size)
        return fail("MZ header larger than the file");
    const uint8_t *image = data + header_size;
    size_t image_size = size - header_size;
    size_t segment = (size_t)header.cs << 4;

    ExepackHeader exepack;
    if (!read_exepack_header(image, image_size, header.ip, segment, &exepack))
        return false;

    size_t skip = ((size_t)exepack.skip_paras - 1) << 4;
    if (exepack.skip_paras == 0 || skip > segment)
        return fail("bad EXEPACK skip length");
    size_t src = segment - skip;
    size_t dest_size = (size_t)exepack.dest_paras << 4;

    *out = (MzProgram){
        .ip = exepack.real_ip, .cs = exepack.real_cs,
        .sp = exepack.real_sp, .ss = exepack.real_ss,
    };
    if (!bytebuf_reserve(&out->image, dest_size > src ? dest_size : src))
        return false;
    memset(out->image.data, 0, out->image.capacity);
    memcpy(out->image.data, image, src);
    if (!expand(&out->image, src, dest_size))
        return false;
    out->image.size = dest_size;

    if (!read_relocations(image, image_size, segment, segment + exepack.exepack_size,
                          &out->relocs))
        return false;

    /* Keep the packed program's total memory footprint. */
    long packed_load = (long)((image_size + 15) >> 4);
    long min_alloc = packed_load + header.min_alloc_paras - exepack.dest_paras;
    if (min_alloc < 0)
        min_alloc = 0;
    if (min_alloc > 0xFFFF)
        return fail("EXEPACK minimum allocation does not fit in 16 bits");
    out->min_alloc_paras = (uint16_t)min_alloc;
    out->max_alloc_paras = header.max_alloc_paras;
    if (header.max_alloc_paras != 0xFFFF && out->min_alloc_paras > header.max_alloc_paras)
        out->max_alloc_paras = out->min_alloc_paras;
    return true;
}

bool unpack_all(const uint8_t *data, size_t size, ByteBuf *out, int *layers)
{
    ByteBuf current = {0};
    if (!bytebuf_append(&current, data, size))
        return false;
    int removed = 0;
    for (;;) {
        bool is_lzexe = lzexe_detect(current.data, current.size);
        if (!is_lzexe && !exepack_detect(current.data, current.size))
            break;
        if (removed == 8) {
            bytebuf_free(&current);
            return fail("too many packer layers");
        }
        MzProgram program = {0};
        bool ok = is_lzexe ? lzexe_unpack(current.data, current.size, &program)
                           : exepack_unpack(current.data, current.size, &program);
        ok = ok && mz_write(&program, &current);
        mz_program_free(&program);
        if (!ok) {
            bytebuf_free(&current);
            return false;
        }
        removed++;
    }
    bytebuf_free(out);
    *out = current;
    if (layers)
        *layers = removed;
    return true;
}
