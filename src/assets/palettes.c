/* palettes.c - palettes.dat (LPAL). */
#include "formats.h"
#include "reader.h"

#include <stdlib.h>

static void read_block(Reader *r, uint8_t *out, size_t count)
{
    const uint8_t *p = reader_bytes(r, count);
    if (p)
        memcpy(out, p, count);
}

bool palettes_parse(const uint8_t *data, size_t size, PaletteSet *out)
{
    *out = (PaletteSet){0};
    Reader r = reader_open(data, size);
    if (!reader_header(&r, "LPAL", FORMAT_LPAL_VERSION))
        return false;
    read_block(&r, out->vga_attribute_registers, VGA_ATTRIBUTE_REGISTERS);
    read_block(&r, out->nibble_to_dac, sizeof out->nibble_to_dac);
    reader_u8(&r); /* pad */
    for (int i = 0; i < PULSE_VGA_STEPS; i++)
        out->pulse_vga[i] = reader_u16(&r);
    read_block(&r, out->pulse_ega, PULSE_EGA_STEPS);

    out->palette_count = reader_u16(&r);
    out->palettes = calloc(out->palette_count ? out->palette_count : 1, sizeof *out->palettes);
    if (!out->palettes)
        return format_fail("out of memory");
    for (uint16_t i = 0; i < out->palette_count; i++) {
        NamedPalette *p = &out->palettes[i];
        p->ds_offset = reader_u16(&r);
        for (int c = 0; c < GFX_GAME_COLORS; c++)
            p->palette.st_words[c] = reader_u16(&r);
        read_block(&r, p->palette.ega_registers, GFX_GAME_COLORS + 1);
        reader_u8(&r); /* pad */
    }

    out->flash_list_count = reader_u16(&r);
    out->flash_lists = calloc(out->flash_list_count ? out->flash_list_count : 1, sizeof *out->flash_lists);
    if (!out->flash_lists) {
        palettes_free(out);
        return format_fail("out of memory");
    }
    uint16_t *firsts = calloc(out->flash_list_count ? out->flash_list_count : 1, sizeof *firsts);
    for (uint16_t i = 0; firsts && i < out->flash_list_count; i++) {
        out->flash_lists[i].ds_offset = reader_u16(&r);
        firsts[i] = reader_u16(&r);
        out->flash_lists[i].word_count = reader_u16(&r);
    }
    uint16_t word_count = reader_u16(&r);
    out->flash_words = calloc(word_count ? word_count : 1, sizeof *out->flash_words);
    for (uint16_t i = 0; out->flash_words && i < word_count; i++)
        out->flash_words[i] = reader_u16(&r);

    bool ok = firsts && out->flash_words;
    if (!ok)
        format_fail("out of memory");
    else if (!r.ok || r.pos != size)
        ok = format_fail("LPAL: %s", r.ok ? "trailing bytes" : "truncated");
    for (uint16_t i = 0; ok && i < out->flash_list_count; i++) {
        FlashList *list = &out->flash_lists[i];
        if (firsts[i] > word_count || list->word_count > word_count - firsts[i])
            ok = format_fail("LPAL: flash list %u outside the word area", (unsigned)i);
        else
            list->words = out->flash_words + firsts[i];
    }
    free(firsts);
    if (!ok)
        palettes_free(out);
    return ok;
}

void palettes_free(PaletteSet *set)
{
    free(set->palettes);
    free(set->flash_lists);
    free(set->flash_words);
    *set = (PaletteSet){0};
}

const NamedPalette *palettes_find(const PaletteSet *set, uint16_t ds_offset)
{
    for (uint16_t i = 0; i < set->palette_count; i++)
        if (set->palettes[i].ds_offset == ds_offset)
            return &set->palettes[i];
    return NULL;
}

const FlashList *palettes_find_flash_list(const PaletteSet *set, uint16_t ds_offset)
{
    for (uint16_t i = 0; i < set->flash_list_count; i++)
        if (set->flash_lists[i].ds_offset == ds_offset)
            return &set->flash_lists[i];
    return NULL;
}
