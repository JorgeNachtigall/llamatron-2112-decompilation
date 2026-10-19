/* Game texts from DGROUP as typed op lists plus the raw bytes (data/strings.dat, format in
 * FORMATS.md).
 *
 * text_print (game86 1000:536f) reads a byte string: NUL bytes are skipped, other bytes are
 * glyphs (with word wrap at x 0x130), and '\' starts an escape:
 *   \e end            \r newline           \q<text>NUL line centred on x 160 (1000:5537)
 *   \s<u16 BE> sprite \u \d \l \i cursor +-0x15 up/down/left/right
 *   \g<u8 x><s8 dy>   \c<u8> colour (low nibble)      any other code: ignored
 * Texts are found from the code: every string passed to text_print/text_print_home/
 * text_scroller, and the level-title table at DS:2340 ({level, 0, text}, -1 terminated).
 * DOS messages (DS:0463..0585) are '$'-terminated strings for INT 21h/09h. */
#include "extract.h"

#include <stdio.h>
#include <string.h>

enum {
    DGROUP_SEGMENT = 0x3559,
    LEVEL_TITLE_TABLE = 0x2340,
    LEVEL_TITLE_RECORD = 6,
    MAX_TEXT_SIZE = 8192,
    MESSAGES_START = 0x0463,
    MESSAGES_END = 0x0586,

    OP_END = 0x00,
    OP_TEXT = 0x01,
    OP_CENTRED = 0x02,
    OP_NEWLINE = 0x03,
    OP_SPRITE = 0x04,
    OP_UP = 0x05,
    OP_DOWN = 0x06,
    OP_LEFT = 0x07,
    OP_RIGHT = 0x08,
    OP_GOTO = 0x09,
    OP_COLOUR = 0x0A,
    OP_IGNORED = 0x0B,
};

/* SI values at the text_print* / text_scroller call sites (1000:02d7, 02f9, 05a5, 05b2, 0646,
 * 5987, 58f1, 5c9f and the name-entry screens). */
static const uint16_t text_starts[] = {
    0x05A5, 0x05DB, 0x05F4, 0x094B, 0x09B5, 0x0AAE, 0x0B7A, 0x117A, 0x14E1, 0x1BC8,
};

static const uint8_t *dgroup(const ExtractContext *ctx, uint16_t offset, size_t size)
{
    return game86_at(ctx, DGROUP_SEGMENT, offset, size);
}

/* Flushes pending glyph bytes as one OP_TEXT. */
static bool flush_text(ByteBuf *ops, ByteBuf *pending)
{
    bool ok = true;
    if (pending->size)
        ok = bytebuf_push(ops, OP_TEXT) && bytebuf_put_u16(ops, (uint16_t)pending->size) &&
             bytebuf_append(ops, pending->data, pending->size);
    pending->size = 0;
    return ok;
}

/* Converts the text at DS:start into ops (ending with OP_END); *raw_length = original bytes up to
 * and including the \e. */
static bool convert_text(const ExtractContext *ctx, uint16_t start, ByteBuf *ops, size_t *raw_length)
{
    const uint8_t *text = dgroup(ctx, start, 1);
    if (!text)
        return false;
    size_t limit = ctx->game86_image.size - (size_t)(text - ctx->game86_image.data);
    if (limit > MAX_TEXT_SIZE)
        limit = MAX_TEXT_SIZE;

    ByteBuf pending = {0};
    bool ok = true;
    size_t pos = 0;
    for (;;) {
        if (pos + 3 > limit) {
            ok = fail("text at DS:%04x has no \\e terminator", start);
            break;
        }
        uint8_t byte = text[pos++];
        if (byte != '\\') {
            ok = bytebuf_push(&pending, byte); /* NULs kept: they end word-wrap look-ahead */
            if (!ok)
                break;
            continue;
        }
        if (!(ok = flush_text(ops, &pending)))
            break;
        uint8_t code = text[pos++];
        if (code == 'e') {
            ok = bytebuf_push(ops, OP_END);
            *raw_length = pos;
            break;
        }
        switch (code) {
        case 'q': {
            size_t end = pos;
            while (end < limit && text[end] != 0)
                end++;
            if (end == limit) {
                ok = fail("centred line at DS:%04x has no NUL", start);
                break;
            }
            ok = bytebuf_push(ops, OP_CENTRED) && bytebuf_put_u16(ops, (uint16_t)(end - pos)) &&
                 bytebuf_append(ops, text + pos, end - pos);
            pos = end + 1;
            break;
        }
        case 'r': ok = bytebuf_push(ops, OP_NEWLINE); break;
        case 'u': ok = bytebuf_push(ops, OP_UP); break;
        case 'd': ok = bytebuf_push(ops, OP_DOWN); break;
        case 'l': ok = bytebuf_push(ops, OP_LEFT); break;
        case 'i': ok = bytebuf_push(ops, OP_RIGHT); break;
        case 's': /* big-endian sprite number, 1000:5439 XCHG DL,DH */
            ok = bytebuf_push(ops, OP_SPRITE) && bytebuf_put_u16(ops, (uint16_t)(text[pos] << 8 | text[pos + 1]));
            pos += 2;
            break;
        case 'g':
            ok = bytebuf_push(ops, OP_GOTO) && bytebuf_append(ops, text + pos, 2);
            pos += 2;
            break;
        case 'c':
            ok = bytebuf_push(ops, OP_COLOUR) && bytebuf_push(ops, text[pos]);
            pos += 1;
            break;
        default:
            ok = bytebuf_push(ops, OP_IGNORED) && bytebuf_push(ops, code);
            break;
        }
        if (!ok)
            break;
    }
    bytebuf_free(&pending);
    return ok;
}

bool extract_texts(ExtractContext *ctx)
{
    uint16_t starts[64];
    size_t text_count = 0;
    for (size_t i = 0; i < sizeof text_starts / sizeof *text_starts; i++)
        starts[text_count++] = text_starts[i];

    /* level titles: {s16 level, u16 0, u16 text} until level == -1 (1000:05b8) */
    int16_t title_levels[16];
    uint16_t title_texts[16];
    size_t title_count = 0;
    for (uint16_t at = LEVEL_TITLE_TABLE;; at = (uint16_t)(at + LEVEL_TITLE_RECORD)) {
        const uint8_t *record = dgroup(ctx, at, LEVEL_TITLE_RECORD);
        if (!record)
            return false;
        if (read_u16le(record) == 0xFFFF)
            break;
        if (title_count == 16 || text_count == 64)
            return fail("level-title table at DS:%04x too long", LEVEL_TITLE_TABLE);
        title_levels[title_count] = (int16_t)read_u16le(record);
        title_texts[title_count++] = (uint16_t)text_count;
        starts[text_count++] = read_u16le(record + 4);
    }

    ByteBuf out = {0}, ops = {0}, messages = {0}, raw = {0}, raw_index = {0};
    bool ok = bytebuf_append(&out, (const uint8_t *)"LTXT", 4) && bytebuf_put_u16(&out, FMT_LTXT_VERSION) &&
              bytebuf_put_u16(&out, (uint16_t)text_count);
    for (size_t i = 0; ok && i < text_count; i++) {
        size_t first = ops.size, raw_length = 0;
        ok = convert_text(ctx, starts[i], &ops, &raw_length) && bytebuf_put_u16(&out, starts[i]) &&
             bytebuf_put_u16(&out, (uint16_t)first) && bytebuf_put_u16(&out, (uint16_t)(ops.size - first));
        /* the original bytes as text_print reads them, plus a NUL so they can be used as a C string
         * up to the first interior NUL (\q lines and word-wrap padding contain NULs) */
        ok = ok && bytebuf_put_u16(&raw_index, (uint16_t)raw.size) && bytebuf_put_u16(&raw_index, (uint16_t)raw_length) &&
             bytebuf_append(&raw, dgroup(ctx, starts[i], raw_length), raw_length) && bytebuf_push(&raw, 0);
    }
    ok = ok && bytebuf_put_u16(&out, (uint16_t)title_count);
    for (size_t i = 0; ok && i < title_count; i++)
        ok = bytebuf_put_u16(&out, (uint16_t)title_levels[i]) && bytebuf_put_u16(&out, title_texts[i]);

    /* DOS messages: split DS:0463..0585 at '$' */
    const uint8_t *area = dgroup(ctx, MESSAGES_START, MESSAGES_END - MESSAGES_START);
    uint16_t message_count = 0;
    ok = ok && area;
    for (size_t pos = 0, start = 0; ok && pos < MESSAGES_END - MESSAGES_START; pos++) {
        if (area[pos] != '$')
            continue;
        ok = bytebuf_put_u16(&messages, (uint16_t)(MESSAGES_START + start)) && bytebuf_put_u16(&messages, (uint16_t)(pos - start)) &&
             bytebuf_append(&messages, area + start, pos - start);
        message_count++;
        start = pos + 1;
    }
    ok = ok && bytebuf_put_u16(&out, message_count) && bytebuf_append(&out, messages.data, messages.size) &&
         bytebuf_put_u16(&out, (uint16_t)ops.size) && bytebuf_append(&out, ops.data, ops.size) &&
         bytebuf_append(&out, raw_index.data, raw_index.size) && bytebuf_put_u16(&out, (uint16_t)raw.size) &&
         bytebuf_append(&out, raw.data, raw.size) && output_write(ctx->outputs, "data/strings.dat", out.data, out.size);
    ok = ok && index_define(ctx, "TEXT_COUNT", text_count) &&
         index_define(ctx, "LEVEL_TITLE_COUNT", title_count) && index_define(ctx, "MESSAGE_COUNT", message_count);
    if (ok)
        printf("  %zu texts (%zu level titles), %u DOS messages\n", text_count, title_count, message_count);
    bytebuf_free(&out);
    bytebuf_free(&ops);
    bytebuf_free(&messages);
    bytebuf_free(&raw);
    bytebuf_free(&raw_index);
    return ok;
}
