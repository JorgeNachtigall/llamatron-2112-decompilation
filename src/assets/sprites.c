/* sprites.c - sprites.bin / g3.bin (LSPR), sprite_refs.bin (LSRF), named_sprites.dat (LNSP). */
#include "formats.h"
#include "reader.h"

#include <stdlib.h>
#include <string.h>

enum { FRAME_RECORD_SIZE = 20 };

bool sprite_bank_parse(const uint8_t *data, size_t size, SpriteBank *out)
{
    *out = (SpriteBank){0};
    Reader r = reader_open(data, size);
    if (!reader_header(&r, "LSPR", FORMAT_LSPR_VERSION))
        return false;
    uint16_t count = reader_u16(&r);
    const uint8_t *records = reader_bytes(&r, (size_t)count * FRAME_RECORD_SIZE);
    if (!records)
        return format_fail("LSPR: truncated frame table");
    const uint8_t *pixels = data + r.pos;
    size_t pixels_size = size - r.pos;

    SpriteFrame *frames = calloc(count ? count : 1, sizeof *frames);
    GfxSprite *sprites = calloc(count ? count : 1, sizeof *sprites);
    if (!frames || !sprites) {
        free(frames);
        free(sprites);
        return format_fail("out of memory");
    }
    Reader table = reader_open(records, (size_t)count * FRAME_RECORD_SIZE);
    for (uint16_t i = 0; i < count; i++) {
        SpriteFrame *f = &frames[i];
        f->bank_offset = reader_u16(&table);
        f->kind = reader_u16(&table);
        f->width = reader_u16(&table);
        f->height = reader_u16(&table);
        f->nominal_width = reader_u16(&table);
        f->hotspot_x = (int16_t)reader_u16(&table);
        f->hotspot_y = (int16_t)reader_u16(&table);
        reader_u16(&table); /* reserved */
        uint32_t offset = reader_u32(&table);
        size_t area = (size_t)f->width * f->height;
        if (offset > pixels_size || area > pixels_size - offset) {
            free(frames);
            free(sprites);
            return format_fail("LSPR: frame %u pixels outside the file", (unsigned)i);
        }
        f->pixels = pixels + offset;
        sprites[i] = (GfxSprite){
            .width = (int16_t)f->width,
            .height = (int16_t)f->height,
            .hotspot_x = f->hotspot_x,
            .hotspot_y = f->hotspot_y,
            .pixels = f->pixels,
            .mask = NULL,
        };
    }
    *out = (SpriteBank){count, frames, sprites};
    return true;
}

void sprite_bank_free(SpriteBank *bank)
{
    free(bank->frames);
    free(bank->sprites);
    *bank = (SpriteBank){0};
}

int sprite_bank_find(const SpriteBank *bank, uint16_t bank_offset)
{
    for (int i = 0; i < bank->frame_count; i++)
        if (bank->frames[i].bank_offset == bank_offset)
            return i;
    return -1;
}

bool sprite_refs_parse(const uint8_t *data, size_t size, SpriteRefTable *out)
{
    *out = (SpriteRefTable){0};
    Reader r = reader_open(data, size);
    if (!reader_header(&r, "LSRF", FORMAT_LSRF_VERSION))
        return false;
    uint16_t count = reader_u16(&r);
    uint16_t *refs = calloc(count ? count : 1, sizeof *refs);
    if (!refs)
        return format_fail("out of memory");
    for (uint16_t i = 0; i < count; i++) {
        refs[i] = reader_u16(&r);
        if (refs[i] != SPRITE_REF_END && (refs[i] & SPRITE_REF_LINK) && (refs[i] & ~SPRITE_REF_LINK) >= count) {
            free(refs);
            return format_fail("LSRF: entry %u links outside the table", (unsigned)i);
        }
    }
    if (!r.ok) {
        free(refs);
        return format_fail("LSRF: truncated");
    }
    *out = (SpriteRefTable){count, refs};
    return true;
}

void sprite_refs_free(SpriteRefTable *table)
{
    free(table->refs);
    *table = (SpriteRefTable){0};
}

bool named_sprites_parse(const uint8_t *data, size_t size, uint16_t frame_count, NamedSprites *out)
{
    memset(out, 0, sizeof *out);
    Reader r = reader_open(data, size);
    if (!reader_header(&r, "LNSP", FORMAT_LNSP_VERSION))
        return false;
    uint16_t count = reader_u16(&r);
    if (r.ok && count != SPRITE_ID_COUNT)
        return format_fail("LNSP: %u names, expected %u", (unsigned)count, (unsigned)SPRITE_ID_COUNT);
    for (int i = 0; i < SPRITE_ID_COUNT; i++) {
        out->bank_offset[i] = reader_u16(&r);
        out->frame[i] = reader_u16(&r);
        if (r.ok && out->frame[i] >= frame_count)
            return format_fail("LNSP: name %d is frame %u of %u", i, (unsigned)out->frame[i], (unsigned)frame_count);
    }
    if (!r.ok || r.pos != size)
        return format_fail("LNSP: %s", r.ok ? "trailing bytes" : "truncated");
    return true;
}
