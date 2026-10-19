/* texts.c - strings.dat (LTXT): texts as ops and raw bytes, level titles, DOS messages. */
#include "formats.h"
#include "reader.h"

#include <stdlib.h>

bool texts_parse(const uint8_t *data, size_t size, TextSet *out)
{
    *out = (TextSet){0};
    Reader r = reader_open(data, size);
    if (!reader_header(&r, "LTXT", FORMAT_LTXT_VERSION))
        return false;
    uint16_t text_count = reader_u16(&r);
    const uint8_t *text_records = reader_bytes(&r, (size_t)text_count * 6);
    uint16_t title_count = reader_u16(&r);
    const uint8_t *title_records = reader_bytes(&r, (size_t)title_count * 4);
    uint16_t message_count = reader_u16(&r);
    out->texts = calloc(text_count ? text_count : 1, sizeof *out->texts);
    out->level_titles = calloc(title_count ? title_count : 1, sizeof *out->level_titles);
    out->messages = calloc(message_count ? message_count : 1, sizeof *out->messages);
    if (!out->texts || !out->level_titles || !out->messages) {
        texts_free(out);
        return format_fail("out of memory");
    }
    out->text_count = text_count;
    out->level_title_count = title_count;
    out->message_count = message_count;

    bool ok = true;
    for (uint16_t i = 0; ok && r.ok && i < message_count; i++) {
        DosMessage *m = &out->messages[i];
        m->ds_offset = reader_u16(&r);
        m->length = reader_u16(&r);
        const uint8_t *text = reader_bytes(&r, m->length);
        m->text = text ? malloc((size_t)m->length + 1) : NULL;
        if (text && !m->text)
            ok = format_fail("out of memory");
        else if (text) {
            memcpy(m->text, text, m->length);
            m->text[m->length] = '\0';
        }
    }
    uint16_t op_size = reader_u16(&r);
    const uint8_t *ops = reader_bytes(&r, op_size);
    const uint8_t *raw_records = reader_bytes(&r, (size_t)text_count * 4);
    uint16_t raw_size = reader_u16(&r);
    const uint8_t *raw = reader_bytes(&r, raw_size);
    if (ok && (!r.ok || r.pos != size))
        ok = format_fail("LTXT: %s", r.ok ? "trailing bytes" : "truncated");

    for (uint16_t i = 0; ok && i < text_count; i++) {
        Reader t = reader_open(text_records + i * 6, 6), q = reader_open(raw_records + i * 4, 4);
        GameText *text = &out->texts[i];
        text->ds_offset = reader_u16(&t);
        uint16_t first = reader_u16(&t);
        text->op_size = reader_u16(&t);
        uint16_t raw_offset = reader_u16(&q);
        text->raw_length = reader_u16(&q);
        if (first > op_size || text->op_size > op_size - first)
            ok = format_fail("LTXT: text %u ops outside the op area", (unsigned)i);
        else if (raw_offset >= raw_size || text->raw_length >= raw_size - raw_offset ||
                 raw[raw_offset + text->raw_length] != 0)
            ok = format_fail("LTXT: text %u raw bytes outside the raw area", (unsigned)i);
        text->ops = ops + first;
        text->raw = (const char *)raw + raw_offset;
    }
    for (uint16_t i = 0; ok && i < title_count; i++) {
        Reader t = reader_open(title_records + i * 4, 4);
        out->level_titles[i].level = (int16_t)reader_u16(&t);
        out->level_titles[i].text_index = reader_u16(&t);
        if (out->level_titles[i].text_index >= text_count)
            ok = format_fail("LTXT: level title %u has no text", (unsigned)i);
    }
    if (!ok)
        texts_free(out);
    return ok;
}

void texts_free(TextSet *set)
{
    for (uint16_t i = 0; set->messages && i < set->message_count; i++)
        free(set->messages[i].text);
    free(set->texts);
    free(set->level_titles);
    free(set->messages);
    *set = (TextSet){0};
}

const GameText *texts_find(const TextSet *set, uint16_t ds_offset)
{
    for (uint16_t i = 0; i < set->text_count; i++)
        if (set->texts[i].ds_offset == ds_offset)
            return &set->texts[i];
    return NULL;
}

bool text_op_next(const uint8_t **cursor, const uint8_t *end, TextOp *op)
{
    if (*cursor >= end)
        return false;
    Reader r = reader_open(*cursor, (size_t)(end - *cursor));
    *op = (TextOp){.code = (TextOpCode)reader_u8(&r)};
    switch (op->code) {
    case TEXT_OP_GLYPHS:
    case TEXT_OP_CENTRED:
        op->length = reader_u16(&r);
        op->bytes = reader_bytes(&r, op->length);
        break;
    case TEXT_OP_SPRITE:
        op->sprite = reader_u16(&r);
        break;
    case TEXT_OP_GOTO:
        op->x = reader_u8(&r);
        op->dy = (int8_t)reader_u8(&r);
        break;
    case TEXT_OP_COLOUR:
    case TEXT_OP_IGNORED:
        op->value = reader_u8(&r);
        break;
    case TEXT_OP_END:
    case TEXT_OP_NEWLINE:
    case TEXT_OP_UP:
    case TEXT_OP_DOWN:
    case TEXT_OP_LEFT:
    case TEXT_OP_RIGHT:
        break;
    default:
        return false;
    }
    if (!r.ok)
        return false;
    *cursor += r.pos;
    return true;
}
