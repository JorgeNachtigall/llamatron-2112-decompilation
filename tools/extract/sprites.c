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
 * Outputs (formats in FORMATS.md): data/sprites.bin, data/sprite_refs.bin, data/g3.bin,
 * data/named_sprites.dat. */
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
    CODE_SEGMENT = 0x1000,
    DGROUP_SEGMENT = 0x3559,
    MOV_SI_IMM16 = 0xBE,
    PAUSE_PICTURES = 0x09E2, /* tbl_pause_pictures, -1 terminated */
    PAUSE_PICTURE_COUNT = 4, /* 1000:15a5 AND BX,3 */
};

/* Frames the code draws by a fixed bank pointer (MOV SI,imm16 with ES = CS:[025F]), in the
 * order of named_sprites.dat; every site of one name must hold the same pointer. */
static const struct {
    const char *name;
    uint16_t sites[2];      /* code offsets of the MOV SI, 0 = unused */
} code_sprites[] = {
    {"beam head", {0x46B6, 0x477F}},      /* anim 02/03 (beams) */
    {"power-up mark", {0x469F, 0}},       /* anim 10 */
    {"shield", {0x1A81, 0x1B4B}},         /* while the shield timer DS:0907 runs, P1/P2 */
    {"rescue marker", {0x1AFE, 0x1BD2}},  /* at DS:0921/0923, P1/P2 */
    {"hiscore mark 1", {0x5CDE, 0}},      /* next to the new score at row DS:09DE */
    {"hiscore mark 2", {0x5D04, 0}},      /* row DS:09E0 */
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
    bool ok = bytebuf_append(&file, (const uint8_t *)"LSPR", 4) && bytebuf_put_u16(&file, FMT_LSPR_VERSION) &&
              bytebuf_put_u16(&file, (uint16_t)count);
    for (size_t i = 0; ok && i < count; i++) {
        const FrameHeader *f = &frames[i];
        uint32_t pixel_offset = (uint32_t)pixels.size;
        ok = bytebuf_put_u16(&file, f->offset) && bytebuf_put_u16(&file, f->kind) &&
             bytebuf_put_u16(&file, (uint16_t)(f->columns * 16)) && bytebuf_put_u16(&file, f->rows) &&
             bytebuf_put_u16(&file, f->nominal_width) &&
             bytebuf_put_u16(&file, (uint16_t)(f->nominal_width >> 1)) &&
             bytebuf_put_u16(&file, (uint16_t)((f->rows - 1) >> 1)) && bytebuf_put_u16(&file, 0) &&
             bytebuf_put_u32(&file, pixel_offset) && append_pixels(bank, f, &pixels);
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

/* "LNSP" file: {bank offset, frame index} of each code_sprites entry, then the pause pictures
 * (DS:09E2, picked by 1000:15a2..15ae, drawn by 1000:1a5c). */
static bool write_named_sprites(ExtractContext *ctx, const FrameHeader *frames, size_t count)
{
    size_t code_count = sizeof code_sprites / sizeof *code_sprites;
    uint16_t offsets[sizeof code_sprites / sizeof *code_sprites + PAUSE_PICTURE_COUNT];
    for (size_t i = 0; i < code_count; i++) {
        for (int k = 0; k < 2 && code_sprites[i].sites[k]; k++) {
            const uint8_t *op = game86_at(ctx, CODE_SEGMENT, code_sprites[i].sites[k], 3);
            if (!op)
                return false;
            if (op[0] != MOV_SI_IMM16)
                return fail("%s: 1000:%04x is not MOV SI,imm16", code_sprites[i].name, code_sprites[i].sites[k]);
            if (k > 0 && read_u16le(op + 1) != offsets[i])
                return fail("%s: 1000:%04x and 1000:%04x differ", code_sprites[i].name, code_sprites[i].sites[0],
                            code_sprites[i].sites[k]);
            offsets[i] = read_u16le(op + 1);
        }
    }
    const uint8_t *pause = game86_at(ctx, DGROUP_SEGMENT, PAUSE_PICTURES, (PAUSE_PICTURE_COUNT + 1) * 2);
    if (!pause)
        return false;
    if (read_u16le(pause + PAUSE_PICTURE_COUNT * 2) != REF_END)
        return fail("pause pictures at DS:%04x: no -1 after %d entries", PAUSE_PICTURES, PAUSE_PICTURE_COUNT);
    for (int i = 0; i < PAUSE_PICTURE_COUNT; i++)
        offsets[code_count + (size_t)i] = read_u16le(pause + i * 2);

    size_t total = code_count + PAUSE_PICTURE_COUNT;
    ByteBuf out = {0};
    bool ok = bytebuf_append(&out, (const uint8_t *)"LNSP", 4) && bytebuf_put_u16(&out, FMT_LNSP_VERSION) &&
              bytebuf_put_u16(&out, (uint16_t)total);
    for (size_t i = 0; ok && i < total; i++) {
        long frame = frame_index_at(frames, count, offsets[i]);
        if (frame < 0)
            ok = fail("named sprite %zu: 218C:%04x is not a frame", i, offsets[i]);
        else
            ok = bytebuf_put_u16(&out, offsets[i]) && bytebuf_put_u16(&out, (uint16_t)frame);
    }
    ok = ok && output_write(ctx->outputs, "data/named_sprites.dat", out.data, out.size) &&
         index_define(ctx, "NAMED_SPRITE_COUNT", total) && index_define(ctx, "PAUSE_PICTURE_COUNT", PAUSE_PICTURE_COUNT);
    bytebuf_free(&out);
    return ok;
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
    bool ok = bytebuf_append(&refs, (const uint8_t *)"LSRF", 4) && bytebuf_put_u16(&refs, FMT_LSRF_VERSION) &&
              bytebuf_put_u16(&refs, (uint16_t)(table_end / 2));
    for (size_t pos = 0; ok && pos < table_end; pos += 2) {
        uint16_t word = read_u16le(bank + pos);
        long frame = frame_index_at(frames, count, word);
        if (word == REF_END)
            ok = bytebuf_put_u16(&refs, REF_END);
        else if (frame >= 0)
            ok = bytebuf_put_u16(&refs, (uint16_t)frame);
        else if (word < table_end && (word & 1) == 0)
            ok = bytebuf_put_u16(&refs, (uint16_t)(REF_TABLE_LINK | word / 2));
        else
            ok = fail("sprite table entry 0x%x = 0x%04x is neither a frame nor a table link", (unsigned)pos, word);
    }
    ok = ok && output_write(ctx->outputs, "data/sprite_refs.bin", refs.data, refs.size) &&
         write_frames(ctx->outputs, "data/sprites.bin", bank, frames, count) &&
         write_named_sprites(ctx, frames, count);
    bytebuf_free(&refs);
    ok = ok && index_define(ctx, "SPRITE_FRAME_COUNT", count) &&
         index_define(ctx, "SPRITE_REF_COUNT", table_end / 2);
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
    ok = ok && write_frames(ctx->outputs, "data/g3.bin", raw.data, frames, G3_SHAPE_COUNT) &&
         index_define(ctx, "G3_SHAPE_COUNT", G3_SHAPE_COUNT);
    bytebuf_free(&raw);
    return ok;
}

bool extract_sprites(ExtractContext *ctx)
{
    return extract_sprite_bank(ctx) && extract_title_shapes(ctx);
}
