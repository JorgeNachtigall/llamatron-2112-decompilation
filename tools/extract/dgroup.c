/* Typed tables from game86's data segment (DGROUP = 3559, docs/re-notes-game86.md §8) and the
 * user's configuration files. Formats in FORMATS.md.
 *
 *   data/palettes.dat        VGA attribute registers, ST nibble -> DAC table, game palettes,
 *                            colour-1 pulse tables, screen-flash lists
 *   data/default_scores.dat  built-in high-score table (DS:0AD2)
 *   data/user_scores.dat     the user's scores.yak, if the game would accept it (else empty)
 *   data/config.dat          the user's llama.inf as llama.exe parses it (else "absent")
 *   data/hud_glyphs.dat      HUD digit glyphs (DS:09EE) and player-life labels (DS:0A8E/0A9E)
 *   data/hiscore.dat         name-entry alphabet, table row colours, the rejected word
 *   data/enemies.dat         score awards, drop values, pickup sprites, debris templates
 *   data/controls.dat        player shot / move tables by input nibble, droid aim by quadrant
 *   data/title.dat           title letter sprites, title llama sprites, title tune
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

    HUD_DIGITS = 0x09EE,         /* 10 glyphs */
    HUD_DIGIT_COUNT = 10,
    HUD_PLAYER_LABELS = 0x0A8E,  /* P1, then P2 at 0A9E */
    HUD_PLAYER_LABEL_COUNT = 2,
    HUD_GLYPH_STRIDE = 16,       /* 8 words per glyph */
    HUD_GLYPH_ROWS = 8,

    HISCORE_ALPHABET = 0x0994,       /* letter grid, indexed by the cursor (1000:5b89) */
    HISCORE_ALPHABET_SIZE = 33,      /* 11 x 3 */
    HISCORE_ROW_COLOURS = 0x09C9,    /* one colour per table row (DI at 1000:5cb5, read 5d60) */
    HISCORE_ROW_COUNT = 10,

    SCORE_AWARDS = 0x2054,           /* {u16 count - 1, u16 digit} per think index, 1000:0e38 */
    SCORE_AWARDS_MAX = 64,
    DROP_BY_DIRECTION = 0x0654,      /* 4 x s8, 1000:0e07 */
    DROP_DIRECTIONS = 4,
    PICKUP_SPRITES = 0x063C,         /* 10 x u16 sprite numbers, 1000:0ed5 */
    PICKUP_KINDS = 10,
    DEBRIS_TEMPLATES = 0x0878,       /* 16 x {u16 sprite, s16 vx, s16 vy, u16 0}, 1000:3c46 */
    DEBRIS_TEMPLATE_COUNT = 16,
    DEBRIS_TEMPLATE_SIZE = 8,

    SEEK_DIR_BY_QUADRANT = 0x084A,   /* 4 x s8, think_24 1000:34cc */
    SEEK_VX = 0x084E,                /* 8 x s16 by direction, 1000:350d */
    SEEK_VY = 0x085E,                /* 8 x s16 by direction, 1000:3514 */
    WANDER_VX = 0x1FA2,              /* 8 x s16 by random direction, think_05 1000:4e13 */
    WANDER_VY = 0x1FB2,              /* 1000:4e1a */
    WANDER_SPRITE = 0x1FBA,          /* 1000:4e21; overlaps WANDER_VY[4..7] in the original */
    DIRECTIONS = 8,

    INPUT_NIBBLES = 16,              /* bits 0/1 up/down, 2/3 left/right */
    DROID_QUADRANTS = 4,

    TITLE_LETTER_SPRITES = 0x2118,   /* 10 x u8 sprite numbers, title_draw_hook 1000:4036 */
    TITLE_LETTERS = 10,
    TITLE_LLAMA_SPRITES = 0x0909,    /* 4 x u16 sprite bases, attract_llamas_init 1000:4560 */
    TITLE_LLAMAS = 4,
    TITLE_TUNE = 0x09D5,             /* {rate index, sample} pairs, 0 rate = restart (1000:0870) */
    TITLE_TUNE_MAX = 256,
};

/* Player control tables, 16 bytes each by input nibble (player_fire 1000:0981.., players_tick
 * 1000:16c1..), in file order. DS:0793 and DS:07A3 have no reader and are not exported. */
static const uint16_t control_tables[] = {
    0x0743, /* shot vx      1000:0a2a, 16ef */
    0x0753, /* shot vy      1000:0a41, 16f7 */
    0x0763, /* shot dir     1000:0a58, 16e3 */
    0x0773, /* spread a     1000:0a0b */
    0x0783, /* spread b     1000:0a72 */
    0x07B3, /* move vx      1000:1703 */
    0x07C3, /* move vy      1000:1716 */
    0x07D3, /* ship facing  1000:1727 */
    0x07E3, /* trail flag   1000:1733 */
};
/* Droid aim, 4 bytes each by quadrant (1000:17f6..1810). */
static const uint16_t droid_tables[] = {0x068C, 0x0690, 0x0694, 0x0698};

/* The entered name is compared with a 4-letter word by CMP byte ptr [SI+d], imm8 instructions
 * (game86 1000:5e52, 5e5c, 5e61, 5e67); we take the immediates from the code and check the
 * instruction bytes in front of them. */
static const struct {
    uint16_t offset;
    uint8_t prefix[3];
    uint8_t prefix_size;
} rude_word_compares[] = {
    {0x5e52, {0x80, 0x7C, 0xFF}, 3}, /* CMP [SI-1], imm8 */
    {0x5e5c, {0x80, 0x3C}, 2},       /* CMP [SI], imm8 */
    {0x5e61, {0x80, 0x7C, 0x01}, 3}, /* CMP [SI+1], imm8 */
    {0x5e67, {0x80, 0x7C, 0x02}, 3}, /* CMP [SI+2], imm8 */
};

/* Every palette pointer passed to palette_set (1000:1be6): 2144 everywhere in play, 2175 at
 * 1000:61ff (black), 2164 at video init on EGA (1000:e510; only its EGA half is used). */
static const uint16_t game_palettes[] = {0x2144, 0x2164, 0x2175};

/* Values stored into g_flash_list (DS:06C7); each list is ST words up to a 0 word, played one
 * word per frame into colour 0 (1e7b:01b0). */
static const uint16_t flash_lists[] = {0x6C9, 0x6CD, 0x6FD, 0x707, 0x713, 0x721, 0x729, 0x72D};

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
        if (!bytebuf_put_u16(out, read_u16le(data + i * 2)))
            return false;
    return data != NULL;
}

static bool write_palettes(ExtractContext *ctx)
{
    ByteBuf out = {0}, flash_words = {0};
    bool ok = bytebuf_append(&out, (const uint8_t *)"LPAL", 4) && bytebuf_put_u16(&out, FMT_LPAL_VERSION) &&
              append_dgroup(ctx, &out, VGA_ATTRIBUTE_TABLE, ATTRIBUTE_REGISTERS) &&
              append_dgroup(ctx, &out, NIBBLE_TO_DAC_TABLE, PALETTE_COLORS) && bytebuf_push(&out, 0) &&
              append_words(ctx, &out, PULSE_VGA_TABLE, PULSE_VGA_STEPS) &&
              append_dgroup(ctx, &out, PULSE_EGA_TABLE, PULSE_EGA_STEPS);

    size_t palette_count = sizeof game_palettes / sizeof *game_palettes;
    ok = ok && bytebuf_put_u16(&out, (uint16_t)palette_count);
    for (size_t i = 0; ok && i < palette_count; i++)
        ok = bytebuf_put_u16(&out, game_palettes[i]) && append_words(ctx, &out, game_palettes[i], PALETTE_COLORS) &&
             append_dgroup(ctx, &out, (uint16_t)(game_palettes[i] + PALETTE_EGA_OFFSET), EGA_REGISTERS) &&
             bytebuf_push(&out, 0);

    size_t list_count = sizeof flash_lists / sizeof *flash_lists;
    ok = ok && bytebuf_put_u16(&out, (uint16_t)list_count);
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
            ok = bytebuf_put_u16(&flash_words, read_u16le(word));
        }
        ok = ok && bytebuf_put_u16(&out, flash_lists[i]) && bytebuf_put_u16(&out, first) && bytebuf_put_u16(&out, count);
    }
    ok = ok && bytebuf_put_u16(&out, (uint16_t)(flash_words.size / 2)) &&
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
        if (!bytebuf_put_u32(out, score) || !bytebuf_append(out, entry + SCORE_DIGITS, SCORE_NAME_SIZE))
            return false;
    }
    return true;
}

static bool write_scores(ExtractContext *ctx)
{
    ByteBuf out = {0}, user = {0};
    const uint8_t *defaults = dgroup(ctx, DEFAULT_SCORES, SCORE_TABLE_SIZE);
    bool ok = defaults && bytebuf_append(&out, (const uint8_t *)"LSCO", 4) && bytebuf_put_u16(&out, FMT_LSCO_VERSION) &&
              bytebuf_put_u16(&out, SCORE_ENTRIES);
    if (ok && !append_score_table(defaults, &out))
        ok = fail("default high-score table at DS:%04x is not in the expected format", DEFAULT_SCORES);
    ok = ok && output_write(ctx->outputs, "data/default_scores.dat", out.data, out.size);

    /* The game accepts scores.yak only if reading 0xA1 bytes returns exactly 0xA0
     * (game86 1000:60aa); then it uses the bytes as they are. */
    char path[4096];
    bool have_user = ok && find_original(ctx->original_dir, "scores.yak", path, sizeof path) &&
                     read_file(path, &user) && user.size == SCORE_TABLE_SIZE;
    out.size = 0;
    ok = ok && bytebuf_append(&out, (const uint8_t *)"LSCO", 4) && bytebuf_put_u16(&out, FMT_LSCO_VERSION);
    size_t count_at = out.size;
    ok = ok && bytebuf_put_u16(&out, have_user ? SCORE_ENTRIES : 0);
    if (ok && have_user && !append_score_table(user.data, &out)) {
        printf("  scores.yak has non-digit scores; not imported\n");
        out.size = count_at;
        ok = bytebuf_put_u16(&out, 0);
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

    uint8_t record[12] = {'L', 'C', 'F', 'G', FMT_LCFG_VERSION, 0, present, vga, joystick, sound, cpu, flash_mode};
    return output_write(ctx->outputs, "data/config.dat", record, sizeof record);
}

/* HUD glyphs: 8 rows, one word per row, of which the drawer (game86 1000:519a) uses only the low
 * (even) byte: 8 pixels, bit 7 = left. We keep those bytes. */
static bool append_hud_glyphs(const ExtractContext *ctx, ByteBuf *out, uint16_t offset, int count)
{
    const uint8_t *glyphs = dgroup(ctx, offset, (size_t)count * HUD_GLYPH_STRIDE);
    for (int i = 0; glyphs && i < count * HUD_GLYPH_ROWS; i++)
        if (!bytebuf_push(out, glyphs[i * 2]))
            return false;
    return glyphs != NULL;
}

static bool write_hud_glyphs(ExtractContext *ctx)
{
    ByteBuf out = {0};
    bool ok = bytebuf_append(&out, (const uint8_t *)"LHUD", 4) && bytebuf_put_u16(&out, FMT_LHUD_VERSION) &&
              append_hud_glyphs(ctx, &out, HUD_DIGITS, HUD_DIGIT_COUNT) &&
              append_hud_glyphs(ctx, &out, HUD_PLAYER_LABELS, HUD_PLAYER_LABEL_COUNT) &&
              output_write(ctx->outputs, "data/hud_glyphs.dat", out.data, out.size);
    bytebuf_free(&out);
    return ok;
}

static bool write_hiscore_data(ExtractContext *ctx)
{
    ByteBuf out = {0};
    bool ok = bytebuf_append(&out, (const uint8_t *)"LHSC", 4) && bytebuf_put_u16(&out, FMT_LHSC_VERSION) &&
              append_dgroup(ctx, &out, HISCORE_ALPHABET, HISCORE_ALPHABET_SIZE) &&
              append_dgroup(ctx, &out, HISCORE_ROW_COLOURS, HISCORE_ROW_COUNT);
    for (size_t i = 0; ok && i < sizeof rude_word_compares / sizeof *rude_word_compares; i++) {
        uint8_t size = rude_word_compares[i].prefix_size;
        const uint8_t *code = game86_at(ctx, LOAD_SEGMENT, rude_word_compares[i].offset, (size_t)size + 1);
        if (!code || memcmp(code, rude_word_compares[i].prefix, size) != 0)
            ok = code ? fail("game86 1000:%04x is not the expected CMP", rude_word_compares[i].offset) : false;
        else
            ok = bytebuf_push(&out, code[size]);
    }
    ok = ok && output_write(ctx->outputs, "data/hiscore.dat", out.data, out.size);
    bytebuf_free(&out);
    return ok;
}

/* Read-only tables of the enemy hit / kill handlers. The score table is indexed directly by
 * the think index; its FFFF FFFF record is kept (the code never treats it as an end). */
static bool write_enemy_tables(ExtractContext *ctx)
{
    ByteBuf out = {0};
    uint16_t awards = 0;
    for (;; awards++) {
        const uint8_t *award = dgroup(ctx, (uint16_t)(SCORE_AWARDS + awards * 4), 4);
        if (!award || awards == SCORE_AWARDS_MAX)
            return award ? fail("score table at DS:%04x has no FFFF FFFF record", SCORE_AWARDS) : false;
        if (read_u16le(award) == 0xFFFF && read_u16le(award + 2) == 0xFFFF)
            break;
    }
    awards++; /* include the FFFF FFFF record */
    bool ok = bytebuf_append(&out, (const uint8_t *)"LENM", 4) && bytebuf_put_u16(&out, FMT_LENM_VERSION) &&
              bytebuf_put_u16(&out, awards) && append_words(ctx, &out, SCORE_AWARDS, (size_t)awards * 2) &&
              append_dgroup(ctx, &out, DROP_BY_DIRECTION, DROP_DIRECTIONS) &&
              append_words(ctx, &out, PICKUP_SPRITES, PICKUP_KINDS) &&
              bytebuf_put_u16(&out, DEBRIS_TEMPLATE_COUNT);
    for (uint16_t i = 0; ok && i < DEBRIS_TEMPLATE_COUNT; i++) {
        const uint8_t *t = dgroup(ctx, (uint16_t)(DEBRIS_TEMPLATES + i * DEBRIS_TEMPLATE_SIZE), DEBRIS_TEMPLATE_SIZE);
        if (!t || read_u16le(t + 6) != 0)
            ok = t ? fail("debris template %u at DS:%04x: last word not 0", i, DEBRIS_TEMPLATES) : false;
        else
            ok = append_words(ctx, &out, (uint16_t)(DEBRIS_TEMPLATES + i * DEBRIS_TEMPLATE_SIZE), 3);
    }
    ok = ok && append_dgroup(ctx, &out, SEEK_DIR_BY_QUADRANT, 4) && append_words(ctx, &out, SEEK_VX, DIRECTIONS) &&
         append_words(ctx, &out, SEEK_VY, DIRECTIONS) && append_words(ctx, &out, WANDER_VX, DIRECTIONS) &&
         append_words(ctx, &out, WANDER_VY, DIRECTIONS) && append_words(ctx, &out, WANDER_SPRITE, DIRECTIONS);
    ok = ok && output_write(ctx->outputs, "data/enemies.dat", out.data, out.size) &&
         index_define(ctx, "SCORE_AWARD_COUNT", awards);
    bytebuf_free(&out);
    return ok;
}

static bool write_control_tables(ExtractContext *ctx)
{
    ByteBuf out = {0};
    bool ok = bytebuf_append(&out, (const uint8_t *)"LCTL", 4) && bytebuf_put_u16(&out, FMT_LCTL_VERSION);
    for (size_t i = 0; ok && i < sizeof control_tables / sizeof *control_tables; i++)
        ok = append_dgroup(ctx, &out, control_tables[i], INPUT_NIBBLES);
    for (size_t i = 0; ok && i < sizeof droid_tables / sizeof *droid_tables; i++)
        ok = append_dgroup(ctx, &out, droid_tables[i], DROID_QUADRANTS);
    ok = ok && output_write(ctx->outputs, "data/controls.dat", out.data, out.size);
    bytebuf_free(&out);
    return ok;
}

/* The tune is kept as bytes, in the original order, up to and including its 0 rate byte. */
static bool write_title_tables(ExtractContext *ctx)
{
    ByteBuf out = {0};
    bool ok = bytebuf_append(&out, (const uint8_t *)"LTTL", 4) && bytebuf_put_u16(&out, FMT_LTTL_VERSION) &&
              append_dgroup(ctx, &out, TITLE_LETTER_SPRITES, TITLE_LETTERS) &&
              append_words(ctx, &out, TITLE_LLAMA_SPRITES, TITLE_LLAMAS);
    uint16_t length = 0;
    for (const uint8_t *rate; ok; length += 2) {
        rate = dgroup(ctx, (uint16_t)(TITLE_TUNE + length), 2);
        if (!rate || length >= TITLE_TUNE_MAX) {
            ok = rate ? fail("title tune at DS:%04x has no 0 rate byte", TITLE_TUNE) : false;
            break;
        }
        if (rate[0] == 0) {
            length++;
            break;
        }
    }
    ok = ok && bytebuf_put_u16(&out, length) && append_dgroup(ctx, &out, TITLE_TUNE, length) &&
         output_write(ctx->outputs, "data/title.dat", out.data, out.size);
    bytebuf_free(&out);
    return ok;
}

bool extract_dgroup_tables(ExtractContext *ctx)
{
    if (!write_title_tables(ctx))
        return false;
    if (!write_control_tables(ctx))
        return false;
    if (!write_enemy_tables(ctx))
        return false;
    return write_palettes(ctx) && write_scores(ctx) && write_config(ctx) && write_hud_glyphs(ctx) &&
           write_hiscore_data(ctx);
}
