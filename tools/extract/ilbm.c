#include "ilbm.h"

#include <stdlib.h>
#include <string.h>

enum { MASKING_NONE = 0, COMPRESSION_BYTERUN1 = 1 };

static uint32_t read_u32be(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint16_t read_u16be(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
} ByteRunStream;

/* Decodes exactly `count` bytes of ByteRun1 into `out`; out == NULL discards them (mask rows).
 * A run that crosses the end of the row is cut there, as fs.exe does. fs 1000:231f */
static bool byterun1_row(ByteRunStream *s, uint8_t *out, size_t count)
{
    size_t done = 0;
    while (done < count) {
        if (s->pos >= s->size)
            return fail("ILBM BODY ends early");
        int8_t n = (int8_t)s->data[s->pos++];
        if (n == -128)
            continue;
        if (n >= 0) {
            size_t literal = (size_t)n + 1;
            for (size_t i = 0; i < literal && done < count; i++) {
                if (s->pos >= s->size)
                    return fail("ILBM BODY ends early");
                uint8_t byte = s->data[s->pos++];
                if (out)
                    out[done] = byte;
                done++;
            }
        } else {
            if (s->pos >= s->size)
                return fail("ILBM BODY ends early");
            uint8_t byte = s->data[s->pos++];
            for (int i = 0; i < 1 - n && done < count; i++, done++)
                if (out)
                    out[done] = byte;
        }
    }
    return true;
}

/* BODY: rows of (plane 0 .. plane n-1 [, mask]) bit rows, MSB = leftmost pixel.
 * fs 1000:2211 decode, 1000:04cc planar -> chunky */
static bool decode_body(const uint8_t *body, size_t size, IlbmImage *image)
{
    size_t row_bytes = ((size_t)image->width + 15) / 16 * 2;
    uint8_t *plane_row = malloc(row_bytes);
    image->pixels = calloc((size_t)image->width * image->height, 1);
    if (!plane_row || !image->pixels) {
        free(plane_row);
        return fail("out of memory (ILBM)");
    }
    ByteRunStream stream = {body, size, 0};
    bool ok = true;
    for (uint16_t y = 0; ok && y < image->height; y++) {
        uint8_t *row = image->pixels + (size_t)y * image->width;
        for (uint8_t plane = 0; ok && plane < image->plane_count; plane++) {
            ok = byterun1_row(&stream, plane_row, row_bytes);
            for (uint16_t x = 0; ok && x < image->width; x++)
                if (plane_row[x >> 3] & (0x80 >> (x & 7)))
                    row[x] |= (uint8_t)(1u << plane);
        }
        /* fs.exe skips one extra row for any masking value, not only 1 (mask plane). fs 1000:2362 */
        if (ok && image->masking != MASKING_NONE)
            ok = byterun1_row(&stream, NULL, row_bytes);
    }
    free(plane_row);
    return ok;
}

bool ilbm_decode(const uint8_t *data, size_t size, IlbmImage *out)
{
    *out = (IlbmImage){0};
    if (size < 12 || memcmp(data, "FORM", 4) != 0 || memcmp(data + 8, "ILBM", 4) != 0)
        return fail("not an IFF ILBM file");
    size_t end = 8 + (size_t)read_u32be(data + 4);
    if (end > size)
        return fail("ILBM FORM size exceeds the file");

    bool have_header = false;
    for (size_t pos = 12; pos + 8 <= end;) {
        const uint8_t *id = data + pos;
        size_t length = read_u32be(data + pos + 4);
        const uint8_t *chunk = data + pos + 8;
        if (pos + 8 + length > end)
            return fail("ILBM chunk %.4s runs past the end of the file", (const char *)id);

        if (memcmp(id, "BMHD", 4) == 0) {
            if (length < 20)
                return fail("ILBM BMHD too short");
            out->width = read_u16be(chunk + 0);
            out->height = read_u16be(chunk + 2);
            out->plane_count = chunk[8];
            out->masking = chunk[9];
            out->compression = chunk[10];
            if (out->compression != COMPRESSION_BYTERUN1)
                return fail("ILBM compression %u not supported (fs.exe needs ByteRun1)", out->compression);
            if (out->plane_count == 0 || out->plane_count > 8)
                return fail("ILBM with %u planes not supported", out->plane_count);
            have_header = true;
        } else if (memcmp(id, "CMAP", 4) == 0) {
            size_t colors = length / 3;
            out->color_count = (uint16_t)(colors > ILBM_MAX_COLORS ? ILBM_MAX_COLORS : colors);
            memcpy(out->cmap, chunk, (size_t)out->color_count * 3);
        } else if (memcmp(id, "BODY", 4) == 0) {
            if (!have_header)
                return fail("ILBM BODY before BMHD");
            return decode_body(chunk, length, out);
        }
        pos += 8 + length + (length & 1); /* chunks are padded to even length */
    }
    return fail("ILBM has no BODY chunk");
}

void ilbm_free(IlbmImage *image)
{
    free(image->pixels);
    image->pixels = NULL;
}
