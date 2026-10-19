/* Typed tables from game86's data segment (DGROUP = 3559, docs/re-notes-game86.md §8) and the
 * user's configuration files. Formats in FORMATS.md.
 *
 *   data/palettes.dat        VGA attribute registers, ST nibble -> DAC table, game palettes,
 *                            colour-1 pulse tables, screen-flash lists
 *   data/default_scores.dat  built-in high-score table (DS:0AD2)
 *   data/user_scores.dat     the user's scores.yak, if the game would accept it (else empty)
 *   data/config.dat          the user's llama.inf as llama.exe parses it (else "absent")
 *
 * The only relocation inside DGROUP is DS:0406 (the stub driver's segment), so the relocated
 * image holds the original values everywhere these tables are read. */
#include "extract.h"

#include <stdio.h>
#include <string.h>

enum {
    DGROUP_SEGMENT = 0x3559,

    VGA_ATTRIBUTE_TABLE = 0x2122, /* video init on VGA, game86 1000:e502 */
    ATTRIBUTE_REGISTERS = 17,
    NIBBLE_TO_DAC_TABLE = 0x2134, /* palette_set 1000:1c01 */
    PALETTE_COLORS = 16,
    PALETTE_EGA_OFFSET = 0x20,    /* EGA registers follow the ST words, 1000:1c59 */
    EGA_REGISTERS = 17,
    PULSE_VGA_TABLE = 0x658,      /* 16 ST words, indexed by tick & 15, 1000:3e6c */
    PULSE_VGA_STEPS = 16,
    PULSE_EGA_TABLE = 0x678,      /* 8 EGA register values, every 4th tick, 1000:3eac */
    PULSE_EGA_STEPS = 8,
    FLASH_LIST_MAX_WORDS = 64,

    DEFAULT_SCORES = 0x0AD2,
    SCORE_ENTRIES = 10,
    SCORE_DIGITS = 8,
    SCORE_NAME_SIZE = 8,
    SCORE_TABLE_SIZE = SCORE_ENTRIES * (SCORE_DIGITS + SCORE_NAME_SIZE),
};

/* Every palette pointer passed to palette_set (1000:1be6): 2144 everywhere in play, 2175 at
 * 1000:61ff (black), 2164 at video init on EGA (1000:e510; only its EGA half is used). */
static const uint16_t game_palettes[] = {0x2144, 0x2164, 0x2175};

/* Values stored into g_flash_list (DS:06C7); each list is ST words up to a 0 word, played one
 * word per frame into colour 0 (1e7b:01b0). */
static const uint16_t flash_lists[] = {0x6C9, 0x6CD, 0x6FD, 0x707, 0x713, 0x721, 0x729, 0x72D};

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

static const uint8_t *dgroup(const ExtractContext *ctx, uint16_t offset, size_t size)
{
    return game86_at(ctx, DGROUP_SEGMENT, offset, size);
}

static bool append_dgroup(const ExtractContext *ctx, ByteBuf *out, uint16_t offset, size_t size)
{
    const uint8_t *data = dgroup(ctx, offset, size);
    return data && bytebuf_append(out, data, size);
}

static bool append_words(const ExtractContext *ctx, ByteBuf *out, uint16_t offset, size_t count)
{
    const uint8_t *data = dgroup(ctx, offset, count * 2);
    for (size_t i = 0; data && i < count; i++)
        if (!put_u16(out, read_u16le(data + i * 2)))
            return false;
    return data != NULL;
}

static bool write_palettes(ExtractContext *ctx)
{
    ByteBuf out = {0}, flash_words = {0};
    bool ok = bytebuf_append(&out, (const uint8_t *)"LPAL", 4) && put_u16(&out, 1) &&
              append_dgroup(ctx, &out, VGA_ATTRIBUTE_TABLE, ATTRIBUTE_REGISTERS) &&
              append_dgroup(ctx, &out, NIBBLE_TO_DAC_TABLE, PALETTE_COLORS) && bytebuf_push(&out, 0) &&
              append_words(ctx, &out, PULSE_VGA_TABLE, PULSE_VGA_STEPS) &&
              append_dgroup(ctx, &out, PULSE_EGA_TABLE, PULSE_EGA_STEPS);

    size_t palette_count = sizeof game_palettes / sizeof *game_palettes;
    ok = ok && put_u16(&out, (uint16_t)palette_count);
    for (size_t i = 0; ok && i < palette_count; i++)
        ok = put_u16(&out, game_palettes[i]) && append_words(ctx, &out, game_palettes[i], PALETTE_COLORS) &&
             append_dgroup(ctx, &out, (uint16_t)(game_palettes[i] + PALETTE_EGA_OFFSET), EGA_REGISTERS) &&
             bytebuf_push(&out, 0);

    size_t list_count = sizeof flash_lists / sizeof *flash_lists;
    ok = ok && put_u16(&out, (uint16_t)list_count);
    for (size_t i = 0; ok && i < list_count; i++) {
        uint16_t first = (uint16_t)(flash_words.size / 2), count = 0;
        for (;; count++) {
            const uint8_t *word = dgroup(ctx, (uint16_t)(flash_lists[i] + count * 2), 2);
            if (!word || count == FLASH_LIST_MAX_WORDS) {
                ok = word ? fail("flash list DS:%04x has no terminator", flash_lists[i]) : false;
                break;
            }
            if (read_u16le(word) == 0)
                break;
            ok = put_u16(&flash_words, read_u16le(word));
        }
        ok = ok && put_u16(&out, flash_lists[i]) && put_u16(&out, first) && put_u16(&out, count);
    }
    ok = ok && put_u16(&out, (uint16_t)(flash_words.size / 2)) &&
         bytebuf_append(&out, flash_words.data, flash_words.size) &&
         output_write(ctx->outputs, "data/palettes.dat", out.data, out.size);
    bytebuf_free(&out);
    bytebuf_free(&flash_words);
    return ok;
}

/* scores.yak / DS:0AD2 layout: 10 x {8 ASCII digits, 8 name bytes}. Returns false (without an
 * error) when a score field is not all digits. */
static bool append_score_table(const uint8_t *table, ByteBuf *out)
{
    for (int i = 0; i < SCORE_ENTRIES; i++) {
        const uint8_t *entry = table + i * (SCORE_DIGITS + SCORE_NAME_SIZE);
        uint32_t score = 0;
        for (int d = 0; d < SCORE_DIGITS; d++) {
            if (entry[d] < '0' || entry[d] > '9')
                return false;
            score = score * 10 + (uint32_t)(entry[d] - '0');
        }
        if (!put_u32(out, score) || !bytebuf_append(out, entry + SCORE_DIGITS, SCORE_NAME_SIZE))
            return false;
    }
    return true;
}

static bool write_scores(ExtractContext *ctx)
{
    ByteBuf out = {0}, user = {0};
    const uint8_t *defaults = dgroup(ctx, DEFAULT_SCORES, SCORE_TABLE_SIZE);
    bool ok = defaults && bytebuf_append(&out, (const uint8_t *)"LSCO", 4) && put_u16(&out, 1) &&
              put_u16(&out, SCORE_ENTRIES);
    if (ok && !append_score_table(defaults, &out))
        ok = fail("default high-score table at DS:%04x is not in the expected format", DEFAULT_SCORES);
    ok = ok && output_write(ctx->outputs, "data/default_scores.dat", out.data, out.size);

    /* The game accepts scores.yak only if reading 0xA1 bytes returns exactly 0xA0
     * (game86 1000:60aa); then it uses the bytes as they are. */
    char path[4096];
    bool have_user = ok && find_original(ctx->original_dir, "scores.yak", path, sizeof path) &&
                     read_file(path, &user) && user.size == SCORE_TABLE_SIZE;
    out.size = 0;
    ok = ok && bytebuf_append(&out, (const uint8_t *)"LSCO", 4) && put_u16(&out, 1);
    size_t count_at = out.size;
    ok = ok && put_u16(&out, have_user ? SCORE_ENTRIES : 0);
    if (ok && have_user && !append_score_table(user.data, &out)) {
        printf("  scores.yak has non-digit scores; not imported\n");
        out.size = count_at;
        ok = put_u16(&out, 0);
    }
    ok = ok && output_write(ctx->outputs, "data/user_scores.dat", out.data, out.size);
    bytebuf_free(&out);
    bytebuf_free(&user);
    return ok;
}

/* llama.inf as the launcher reads it (llama.exe 1000:0219..02c8; re-notes-game86 §1). */
static bool write_config(ExtractContext *ctx)
{
    enum { SOUND_ADLIB = 0x00, SOUND_SBLASTER = 0x0C, SOUND_ROLAND = 0x18, SOUND_NONE = 0xFF };
    ByteBuf inf = {0};
    char path[4096];
    bool present = find_original(ctx->original_dir, "llama.inf", path, sizeof path) &&
                   read_file(path, &inf) && inf.size >= 9 &&
                   (inf.data[0] == 'V' || inf.data[0] == 'E');
    uint8_t vga = 0, joystick = 0, sound = SOUND_NONE, cpu = 0, flash_mode = 0;
    if (present) {
        vga = inf.data[0] == 'V';
        joystick = inf.data[2] == 'J';
        switch (inf.data[4]) {
        case 'A': sound = SOUND_ADLIB; break;
        case 'S': sound = SOUND_SBLASTER; break;
        case 'R': sound = SOUND_ROLAND; break;
        default: sound = SOUND_NONE; break;
        }
        cpu = (uint8_t)(inf.data[6] - '0');
        flash_mode = inf.data[8] == 'P' ? 1 : inf.data[8] == 'Y' ? 2 : 0;
    }
    bytebuf_free(&inf);

    uint8_t record[12] = {'L', 'C', 'F', 'G', 1, 0, present, vga, joystick, sound, cpu, flash_mode};
    return output_write(ctx->outputs, "data/config.dat", record, sizeof record);
}

bool extract_dgroup_tables(ExtractContext *ctx)
{
    return write_palettes(ctx) && write_scores(ctx) && write_config(ctx);
}
