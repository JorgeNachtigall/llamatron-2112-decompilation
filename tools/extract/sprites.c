/* Sprite frames: the bank embedded in game86 (segment 218C) and the title shapes in g3.cmp share
 * one format, drawn by game86 1000:8cbb (one 16-px column) and 1000:63d2 (several columns):
 *
 *   header  u16 kind (always 3), u16 columns - 1, u16 rows - 1, u16 nominal width, u16 0, u16 0
 *   cells   rows x columns x 10 bytes, row-major; a cell is one row of a 16-px column:
 *           2 mask bytes, then 2 bytes for each of planes 0..3 (first byte = left 8 pixels,
 *           MSB = leftmost). Mask 1 = the pixel takes the 4-bit colour from the planes,
 *           mask 0 = background stays (write mode 0 through the GC bit mask).
 *   origin  drawn with x -= nominal_width >> 1, y -= (rows - 1) >> 1 (centred, 8cc6..8cd4)
 *
 * Outputs (formats in FORMATS.md): data/sprites.bin, data/sprite_refs.bin, data/g3.bin. */
#include "extract.h"

#include <stdio.h>
#include <string.h>

enum {
    SPRITE_BANK_SEGMENT = 0x218C,
    SPRITE_BANK_END_SEGMENT = 0x30EB, /* next segment: g3 BSS */
    SPRITE_KIND = 3,
    SPRITE_HEADER_SIZE = 12,
    SPRITE_CELL_SIZE = 10,
    SPRITE_MAX_COLUMNS = 20, /* 320 px */
    SPRITE_MAX_ROWS = 200,
    TRANSPARENT = 0xFF,
    MAX_FRAMES = 1024,
    G3_CMP_SIZE = 18144,
    G3_SHAPE_COUNT = 16,
    REF_TABLE_LINK = 0x8000, /* sprite_refs entry pointing at another table position */
    REF_END = 0xFFFF,
    FRAME_RECORD_SIZE = 20,
};

typedef struct {
    uint16_t offset; /* within its bank */
    uint16_t kind, columns, rows, nominal_width;
} FrameHeader;

static bool parse_header(const uint8_t *bank, size_t bank_size, size_t offset, FrameHeader *out)
{
    if (offset + SPRITE_HEADER_SIZE > bank_size)
        return false;
    const uint8_t *h = bank + offset;
    FrameHeader frame = {
        .offset = (uint16_t)offset,
        .kind = read_u16le(h),
        .columns = (uint16_t)(read_u16le(h + 2) + 1),
        .rows = (uint16_t)(read_u16le(h + 4) + 1),
        .nominal_width = read_u16le(h + 6),
    };
    if (frame.kind != SPRITE_KIND || read_u16le(h + 8) != 0 || read_u16le(h + 10) != 0)
        return false;
    if (frame.columns > SPRITE_MAX_COLUMNS || frame.rows > SPRITE_MAX_ROWS)
        return false;
    if (frame.nominal_width == 0 || frame.nominal_width > frame.columns * 16u)
        return false;
    size_t end = offset + SPRITE_HEADER_SIZE + (size_t)frame.rows * frame.columns * SPRITE_CELL_SIZE;
    if (end > bank_size)
        return false;
    *out = frame;
    return true;
}

static size_t frame_end(const FrameHeader *frame)
{
    return frame->offset + SPRITE_HEADER_SIZE + (size_t)frame->rows * frame->columns * SPRITE_CELL_SIZE;
}

static bool put_u16(ByteBuf *buf, uint16_t value)
{
    uint8_t bytes[2];
    write_u16le(bytes, value);
    return bytebuf_append(buf, bytes, 2);
}

static bool put_u32(ByteBuf *buf, uint32_t value)
{
    return put_u16(buf, (uint16_t)(value & 0xFFFF)) && put_u16(buf, (uint16_t)(value >> 16));
}

/* Planar cells -> one byte per pixel (colour 0..15 or TRANSPARENT). */
static bool append_pixels(const uint8_t *bank, const FrameHeader *frame, ByteBuf *pixels)
{
    const uint8_t *cells = bank + frame->offset + SPRITE_HEADER_SIZE;
    for (uint16_t y = 0; y < frame->rows; y++) {
        for (uint16_t column = 0; column < frame->columns; column++) {
            const uint8_t *cell = cells + ((size_t)y * frame->columns + column) * SPRITE_CELL_SIZE;
            for (int x = 0; x < 16; x++) {
                int byte = x >> 3;
                uint8_t bit = (uint8_t)(0x80 >> (x & 7));
                uint8_t colour = TRANSPARENT;
                if (cell[byte] & bit) {
                    colour = 0;
                    for (int plane = 0; plane < 4; plane++)
                        if (cell[2 + plane * 2 + byte] & bit)
                            colour |= (uint8_t)(1 << plane);
                }
                if (!bytebuf_push(pixels, colour))
                    return false;
            }
        }
    }
    return true;
}

/* "LSPR" file: header, frame records, then all pixels. */
static bool write_frames(OutputSet *outputs, const char *relative, const uint8_t *bank,
                         const FrameHeader *frames, size_t count)
{
    ByteBuf file = {0}, pixels = {0};
    bool ok = bytebuf_append(&file, (const uint8_t *)"LSPR", 4) && put_u16(&file, 1) &&
              put_u16(&file, (uint16_t)count);
    for (size_t i = 0; ok && i < count; i++) {
        const FrameHeader *f = &frames[i];
        uint32_t pixel_offset = (uint32_t)pixels.size;
        ok = put_u16(&file, f->offset) && put_u16(&file, f->kind) &&
             put_u16(&file, (uint16_t)(f->columns * 16)) && put_u16(&file, f->rows) &&
             put_u16(&file, f->nominal_width) &&
             put_u16(&file, (uint16_t)(f->nominal_width >> 1)) &&
             put_u16(&file, (uint16_t)((f->rows - 1) >> 1)) && put_u16(&file, 0) &&
             put_u32(&file, pixel_offset) && append_pixels(bank, f, &pixels);
    }
    ok = ok && bytebuf_append(&file, pixels.data, pixels.size) &&
         output_write(outputs, relative, file.data, file.size);
    bytebuf_free(&file);
    bytebuf_free(&pixels);
    return ok;
}

static long frame_index_at(const FrameHeader *frames, size_t count, size_t offset)
{
    for (size_t i = 0; i < count; i++)
        if (frames[i].offset == offset)
            return (long)i;
    return -1;
}

/* The game86 bank: a table of words at 218C:0000 (frame pointers, links into the table itself and
 * 0xFFFF terminators; it ends where the first frame starts), then the frames, with a few small
 * stretches of other data between them. */
static bool extract_sprite_bank(ExtractContext *ctx)
{
    size_t bank_size = (size_t)(SPRITE_BANK_END_SEGMENT - SPRITE_BANK_SEGMENT) * 16;
    const uint8_t *bank = game86_at(ctx, SPRITE_BANK_SEGMENT, 0, bank_size);
    if (!bank)
        return false;

    size_t table_end = bank_size;
    FrameHeader probe;
    for (size_t pos = 0; pos < table_end; pos += 2) {
        uint16_t word = read_u16le(bank + pos);
        if (word < table_end && word > pos && parse_header(bank, bank_size, word, &probe))
            table_end = word;
    }

    static FrameHeader frames[MAX_FRAMES];
    size_t count = 0, skipped = 0;
    for (size_t pos = table_end; pos + SPRITE_HEADER_SIZE <= bank_size;) {
        if (!parse_header(bank, bank_size, pos, &frames[count])) {
            pos += 2;
            skipped += 2;
            continue;
        }
        pos = frame_end(&frames[count]);
        if (++count == MAX_FRAMES)
            return fail("sprite bank: too many frames");
    }

    ByteBuf refs = {0};
    bool ok = bytebuf_append(&refs, (const uint8_t *)"LSRF", 4) && put_u16(&refs, 1) &&
              put_u16(&refs, (uint16_t)(table_end / 2));
    for (size_t pos = 0; ok && pos < table_end; pos += 2) {
        uint16_t word = read_u16le(bank + pos);
        long frame = frame_index_at(frames, count, word);
        if (word == REF_END)
            ok = put_u16(&refs, REF_END);
        else if (frame >= 0)
            ok = put_u16(&refs, (uint16_t)frame);
        else if (word < table_end && (word & 1) == 0)
            ok = put_u16(&refs, (uint16_t)(REF_TABLE_LINK | word / 2));
        else
            ok = fail("sprite table entry 0x%x = 0x%04x is neither a frame nor a table link", (unsigned)pos, word);
    }
    ok = ok && output_write(ctx->outputs, "data/sprite_refs.bin", refs.data, refs.size) &&
         write_frames(ctx->outputs, "data/sprites.bin", bank, frames, count);
    bytebuf_free(&refs);
    if (ok)
        printf("  sprite bank: %zu frames, %zu table words, %zu bytes outside frames\n", count,
               table_end / 2, skipped);
    return ok;
}

/* g3.cmp: 16 u16 shape offsets, then the shapes (title logo, menu texts, ...). */
static bool extract_title_shapes(ExtractContext *ctx)
{
    ByteBuf raw = {0};
    if (!load_cmp(ctx->original_dir, "g3.cmp", G3_CMP_SIZE, &raw))
        return false;
    FrameHeader frames[G3_SHAPE_COUNT];
    bool ok = true;
    for (int i = 0; ok && i < G3_SHAPE_COUNT; i++) {
        uint16_t offset = read_u16le(raw.data + i * 2);
        if (!parse_header(raw.data, raw.size, offset, &frames[i]))
            ok = fail("g3.cmp shape %d at 0x%x has no valid header", i, offset);
    }
    ok = ok && write_frames(ctx->outputs, "data/g3.bin", raw.data, frames, G3_SHAPE_COUNT);
    bytebuf_free(&raw);
    return ok;
}

bool extract_sprites(ExtractContext *ctx)
{
    return extract_sprite_bank(ctx) && extract_title_shapes(ctx);
}
