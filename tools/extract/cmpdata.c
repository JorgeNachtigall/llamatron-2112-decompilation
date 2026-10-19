/* Data from the LZSS-packed .cmp files that game86 loads at start-up (game86 1000:60aa):
 *
 *   data/font.bin  typed proportional font from f.cmp (format in tools/extract/FORMATS.md)
 *   data/sine.bin  s.cmp: 256 x s8 sine (amplitude 126), then 512 bytes no game86 code reads (FORMATS.md)
 *
 * g3.cmp (title shapes) uses the sprite format and is converted by sprites.c. */
#include "extract.h"
#include "lzss.h"

#include <stdio.h>
#include <string.h>

enum {
    FONT_CHAR_COUNT = 128,       /* offset table covers character codes 0..127 */
    FONT_TABLE_OFFSET = 2,       /* u16 line height, then u16 offset per character */
    FONT_GLYPH_HEADER_SIZE = 6,  /* u16 (always 4), u16 advance, u16 rows - 1 */
    FONT_NO_GLYPH = 0xFFFF,
    F_CMP_SIZE = 1710,
    S_CMP_SIZE = 768,
    SINE_COUNT = 256,
};

bool load_cmp(const char *original_dir, const char *name, size_t expected_size, ByteBuf *out)
{
    char path[4096];
    ByteBuf packed = {0};
    if (!find_original(original_dir, name, path, sizeof path))
        return fail("%s not found", name);
    bool ok = read_file(path, &packed) && lzss_decode(packed.data, packed.size, out);
    bytebuf_free(&packed);
    if (ok && out->size != expected_size)
        ok = fail("%s decodes to %zu bytes, expected %zu", name, out->size, expected_size);
    if (!ok)
        return fail("%s: %s", name, last_error());
    return true;
}

/* Glyphs are drawn by game86 1000:559f: rows - 1 at +4, rows from +6, one u16 per row,
 * MSB = leftmost pixel; the pen advances by the word at +2 (1000:53a0, 552c, 555a). */
/* `truncated` receives the glyphs whose rows run past the end of f.cmp, `padded_rows` the rows
 * those are missing (DEVIATIONS #2; counted into asset_index.h for test_game_data). */
static bool convert_font(const ByteBuf *raw, ByteBuf *out, unsigned *truncated, unsigned *padded_rows)
{
    uint16_t glyph_offsets[FONT_CHAR_COUNT]; /* distinct original offsets, in first-use order */
    uint16_t char_map[FONT_CHAR_COUNT];
    size_t glyph_count = 0;

    for (int ch = 0; ch < FONT_CHAR_COUNT; ch++) {
        uint16_t offset = read_u16le(raw->data + FONT_TABLE_OFFSET + ch * 2);
        char_map[ch] = FONT_NO_GLYPH;
        if (offset == 0)
            continue;
        if (offset + FONT_GLYPH_HEADER_SIZE > raw->size)
            return fail("f.cmp: glyph for character 0x%02x outside the font", ch);
        size_t index = 0;
        while (index < glyph_count && glyph_offsets[index] != offset)
            index++;
        if (index == glyph_count)
            glyph_offsets[glyph_count++] = offset;
        char_map[ch] = (uint16_t)index;
    }

    /* header */
    out->size = 0;
    bool ok = bytebuf_append(out, (const uint8_t *)"LFNT", 4) && bytebuf_put_u16(out, FMT_LFNT_VERSION) &&
              bytebuf_put_u16(out, read_u16le(raw->data)) && bytebuf_put_u16(out, FONT_CHAR_COUNT) &&
              bytebuf_put_u16(out, (uint16_t)glyph_count);
    for (int ch = 0; ok && ch < FONT_CHAR_COUNT; ch++)
        ok = bytebuf_put_u16(out, char_map[ch]);

    /* glyph records, then all bitmap rows */
    uint16_t first_row = 0;
    for (size_t g = 0; ok && g < glyph_count; g++) {
        const uint8_t *glyph = raw->data + glyph_offsets[g];
        uint16_t rows = (uint16_t)(read_u16le(glyph + 4) + 1);
        ok = bytebuf_put_u16(out, read_u16le(glyph + 2)) && bytebuf_put_u16(out, rows) && bytebuf_put_u16(out, first_row);
        first_row = (uint16_t)(first_row + rows);
    }
    for (size_t g = 0; ok && g < glyph_count; g++) {
        const uint8_t *glyph = raw->data + glyph_offsets[g];
        size_t wanted = (size_t)(read_u16le(glyph + 4) + 1) * 2;
        size_t available = raw->size - glyph_offsets[g] - FONT_GLYPH_HEADER_SIZE;
        size_t present = wanted < available ? wanted : available;
        /* Rows are stored as two bytes in screen order (first byte = left 8 pixels; the
         * drawer loads them with MOV AX,[SI] and draws AL first). We store each row as a u16
         * with the MSB as the leftmost pixel. */
        for (size_t i = 0; ok && i + 1 < present; i += 2) {
            const uint8_t *row = glyph + FONT_GLYPH_HEADER_SIZE + i;
            ok = bytebuf_put_u16(out, (uint16_t)(row[0] << 8 | row[1]));
        }
        /* The last glyph (used for ] ^ _ and 0x7d-0x7f) claims 7 rows but f.cmp ends after 2.
         * The original drawer reads on past the font into DGROUP 0x409c.., i.e. into the
         * high-score table at 0x40a0, so those rows depend on the current scores. We use
         * blank rows instead (documented deviation, FORMATS.md). */
        for (size_t i = present; ok && i < wanted; i++)
            ok = bytebuf_push(out, 0);
        if (present < wanted) {
            ++*truncated;
            *padded_rows += (unsigned)((wanted - present) / 2);
        }
    }
    return ok;
}

bool extract_cmp_data(ExtractContext *ctx)
{
    const char *original_dir = ctx->original_dir;
    OutputSet *outputs = ctx->outputs;
    ByteBuf raw = {0}, font = {0};
    unsigned truncated = 0, padded_rows = 0;
    bool ok = load_cmp(original_dir, "f.cmp", F_CMP_SIZE, &raw) &&
              convert_font(&raw, &font, &truncated, &padded_rows) &&
              output_write(outputs, "data/font.bin", font.data, font.size) &&
              index_define(ctx, "FONT_TRUNCATED_GLYPHS", truncated) &&
              index_define(ctx, "FONT_PADDED_ROWS", padded_rows);
    /* s.cmp is already two plain little-endian arrays: s8[256] then s16[256]. */
    ok = ok && load_cmp(original_dir, "s.cmp", S_CMP_SIZE, &raw) &&
         output_write(outputs, "data/sine.bin", raw.data, SINE_COUNT * 3);
    bytebuf_free(&raw);
    bytebuf_free(&font);
    return ok;
}
