/* font.c - font.bin (LFNT) and sine.bin. */
#include "formats.h"
#include "reader.h"

#include <stdlib.h>

enum { NO_GLYPH = 0xFFFF };

bool font_parse(const uint8_t *data, size_t size, AssetFont *out)
{
    *out = (AssetFont){0};
    Reader r = reader_open(data, size);
    if (!reader_header(&r, "LFNT", FORMAT_LFNT_VERSION))
        return false;
    uint16_t line_height = reader_u16(&r);
    uint16_t char_count = reader_u16(&r);
    uint16_t glyph_count = reader_u16(&r);
    if (r.ok && char_count != FONT_CHAR_COUNT)
        return format_fail("LFNT: %u character codes, expected %u", (unsigned)char_count, FONT_CHAR_COUNT);
    const uint8_t *char_map = reader_bytes(&r, (size_t)char_count * 2);
    const uint8_t *glyph_records = reader_bytes(&r, (size_t)glyph_count * 6);
    if (!glyph_records)
        return format_fail("LFNT: truncated");
    size_t row_total = (size - r.pos) / 2;
    uint16_t *rows = malloc((row_total ? row_total : 1) * sizeof *rows);
    if (!rows)
        return format_fail("out of memory");
    for (size_t i = 0; i < row_total; i++)
        rows[i] = reader_u16(&r);

    out->line_height = line_height;
    out->rows = rows;
    uint8_t tallest = 0;
    for (unsigned c = 0; c < FONT_CHAR_COUNT; c++) {
        uint16_t glyph = (uint16_t)(char_map[c * 2] | char_map[c * 2 + 1] << 8);
        if (glyph == NO_GLYPH)
            continue;
        if (glyph >= glyph_count) {
            font_free(out);
            return format_fail("LFNT: character %u maps to glyph %u of %u", c, (unsigned)glyph,
                               (unsigned)glyph_count);
        }
        Reader record = reader_open(glyph_records + glyph * 6, 6);
        uint16_t advance = reader_u16(&record), row_count = reader_u16(&record), first = reader_u16(&record);
        if (advance > 0xFF || row_count > 0xFF || first > row_total || row_count > row_total - first) {
            font_free(out);
            return format_fail("LFNT: glyph %u out of range", (unsigned)glyph);
        }
        out->glyphs[c] = (GfxGlyph){(uint8_t)advance, (uint8_t)row_count, rows + first};
        if (row_count > tallest)
            tallest = (uint8_t)row_count;
    }
    out->gfx = (GfxFont){tallest, 0, FONT_CHAR_COUNT, out->glyphs};
    return true;
}

void font_free(AssetFont *font)
{
    free(font->rows);
    *font = (AssetFont){0};
}

bool sine_parse(const uint8_t *data, size_t size, SineTables *out)
{
    if (size != SINE_STEPS * 3)
        return format_fail("sine.bin: %zu bytes, expected %d", size, SINE_STEPS * 3);
    Reader r = reader_open(data, size);
    for (int i = 0; i < SINE_STEPS; i++)
        out->sine8[i] = (int8_t)reader_u8(&r);
    return true;
}
