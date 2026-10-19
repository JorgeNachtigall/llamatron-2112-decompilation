/* LZEXE 0.91 decoder, after the classic unlzexe.c algorithm (as in tools/unlzexe.py).
 *
 * Layout of a packed file: the compressed image sits right below the decompressor segment
 * (header CS); that segment starts with an info block (real IP, CS, SP, SS, size of the packed
 * image in paragraphs, ...) and the 0x158-byte stub is followed by the compressed relocation
 * table. The data stream interleaves 16-bit control words (read LSB first) with literal bytes. */
#include "unpack.h"

#include <string.h>

#define LZEXE_STUB_SIZE 0x158

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint16_t control;
    int bits_left;
    bool overrun;
} LzStream;

static uint8_t lz_byte(LzStream *s)
{
    if (s->pos >= s->size) {
        s->overrun = true;
        return 0;
    }
    return s->data[s->pos++];
}

static uint16_t lz_word(LzStream *s)
{
    uint8_t lo = lz_byte(s);
    return (uint16_t)(lo | (lz_byte(s) << 8));
}

/* The next control word is fetched as soon as the current one is used up, before the
 * literal bytes that follow — this ordering is part of the format. */
static int lz_bit(LzStream *s)
{
    int bit = s->control & 1;
    if (--s->bits_left == 0) {
        s->control = lz_word(s);
        s->bits_left = 16;
    } else {
        s->control >>= 1;
    }
    return bit;
}

bool lzexe_detect(const uint8_t *data, size_t size)
{
    return size >= 0x20 && data[0] == 'M' && data[1] == 'Z' && memcmp(data + 0x1C, "LZ91", 4) == 0;
}

static bool read_relocations(const uint8_t *data, size_t size, size_t pos, MzRelocList *relocs)
{
    uint32_t offset = 0, segment = 0;
    for (;;) {
        if (pos >= size)
            return fail("LZEXE relocation table runs past end of file");
        uint32_t span = data[pos++];
        if (span == 0) {
            if (pos + 2 > size)
                return fail("LZEXE relocation table runs past end of file");
            span = read_u16le(data + pos);
            pos += 2;
            if (span == 0) {
                segment += 0x0FFF;
                continue;
            }
            if (span == 1)
                return true;
        }
        offset += span;
        segment += (offset & ~0x0Fu) >> 4;
        offset &= 0x0F;
        if (!mz_reloc_push(relocs, (uint16_t)offset, (uint16_t)segment))
            return false;
    }
}

static bool decompress(const uint8_t *data, size_t size, size_t start, ByteBuf *out)
{
    LzStream s = {.data = data, .size = size, .pos = start};
    s.control = lz_word(&s);
    s.bits_left = 16;
    out->size = 0;

    for (;;) {
        if (s.overrun)
            return fail("LZEXE stream runs past end of file");
        if (lz_bit(&s)) {
            if (!bytebuf_push(out, lz_byte(&s)))
                return false;
            continue;
        }

        long distance; /* negative: how far back the match starts */
        size_t length;
        if (!lz_bit(&s)) {
            /* short match: 2-bit length, 8-bit distance */
            int high = lz_bit(&s);
            int low = lz_bit(&s);
            length = (size_t)((high << 1) | low) + 2;
            distance = (long)lz_byte(&s) - 0x100;
        } else {
            /* long match: 13-bit distance, 3-bit length (0 = length in an extra byte) */
            uint8_t lo = lz_byte(&s);
            uint8_t hi = lz_byte(&s);
            distance = (long)(lo | ((hi & 0xF8) << 5) | 0xE000) - 0x10000;
            length = (size_t)(hi & 0x07) + 2;
            if (length == 2) {
                uint8_t extra = lz_byte(&s);
                if (extra == 0)
                    return s.overrun ? fail("LZEXE stream runs past end of file") : true;
                if (extra == 1)
                    continue; /* segment boundary marker, no data */
                length = (size_t)extra + 1;
            }
        }
        if ((long)out->size + distance < 0)
            return fail("LZEXE match before start of output");
        size_t from = out->size + (size_t)distance; /* wraps to out->size - |distance| */
        if (!bytebuf_reserve(out, out->size + length))
            return false;
        for (size_t i = 0; i < length; i++) /* byte by byte: matches may overlap */
            out->data[out->size++] = out->data[from + i];
    }
}

bool lzexe_unpack(const uint8_t *data, size_t size, MzProgram *out)
{
    MzHeader header;
    if (!lzexe_detect(data, size) || !mz_parse_header(data, size, &header))
        return fail("not an LZEXE 0.91 executable");

    size_t stub = ((size_t)header.header_paras + header.cs) << 4;
    if (stub + LZEXE_STUB_SIZE > size)
        return fail("LZEXE decompressor stub past end of file");
    uint16_t real_ip = read_u16le(data + stub + 0);
    uint16_t real_cs = read_u16le(data + stub + 2);
    uint16_t real_sp = read_u16le(data + stub + 4);
    uint16_t real_ss = read_u16le(data + stub + 6);
    uint16_t packed_paras = read_u16le(data + stub + 8);

    *out = (MzProgram){.ip = real_ip, .cs = real_cs, .sp = real_sp, .ss = real_ss};
    if (!read_relocations(data, size, stub + LZEXE_STUB_SIZE, &out->relocs))
        return false;

    size_t packed_start = (size_t)header.header_paras + header.cs;
    if (packed_paras > packed_start)
        return fail("LZEXE packed image starts before the file");
    if (!decompress(data, size, (packed_start - packed_paras) << 4, &out->image))
        return false;

    /* Keep the packed program's total memory footprint. */
    long packed_load = (long)(size >> 4) - header.header_paras;
    long min_alloc = packed_load + header.min_alloc_paras - (long)((out->image.size + 15) >> 4);
    if (min_alloc < 0)
        min_alloc = 0;
    if (min_alloc > 0xFFFF)
        return fail("LZEXE minimum allocation does not fit in 16 bits");
    out->min_alloc_paras = (uint16_t)min_alloc;
    out->max_alloc_paras = header.max_alloc_paras;
    if (header.max_alloc_paras != 0xFFFF && out->min_alloc_paras > header.max_alloc_paras)
        out->max_alloc_paras = out->min_alloc_paras;
    return true;
}
