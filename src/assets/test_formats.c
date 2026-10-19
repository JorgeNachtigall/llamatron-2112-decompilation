/* test_formats.c - parser unit tests on small hand-built files (no extracted data needed).
 * Every valid file is also cut short at every length and must then be rejected. */
#include "formats.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++;                                                   \
        }                                                                 \
    } while (0)

typedef struct {
    uint8_t bytes[1024];
    size_t size;
} Buf;

static void put8(Buf *b, unsigned v) { b->bytes[b->size++] = (uint8_t)v; }
static void put16(Buf *b, unsigned v) { put8(b, v & 0xFF); put8(b, v >> 8 & 0xFF); }
static void put32(Buf *b, uint32_t v) { put16(b, v & 0xFFFF); put16(b, v >> 16); }
static void put_bytes(Buf *b, const char *s, size_t n) { memcpy(b->bytes + b->size, s, n); b->size += n; }
static void header(Buf *b, const char *magic, unsigned version) { put_bytes(b, magic, 4); put16(b, version); }

/* Every proper prefix of the file must be rejected, and so must a wrong magic and version. */
typedef bool (*ParseFn)(const uint8_t *data, size_t size);
static void check_rejects_damage(const Buf *b, ParseFn parse, const char *what)
{
    for (size_t n = 0; n < b->size; n++)
        if (parse(b->bytes, n)) {
            fprintf(stderr, "%s: accepted a %zu-byte prefix of %zu\n", what, n, b->size);
            failures++;
            break;
        }
    Buf copy = *b;
    copy.bytes[0] ^= 0x20;
    CHECK(!parse(copy.bytes, copy.size));
    copy = *b;
    copy.bytes[4] ^= 0x7F;
    CHECK(!parse(copy.bytes, copy.size));
    CHECK(strstr(format_error(), "version") != NULL);
}

/* ---- adapters with the ParseFn signature; they free what they parse ---- */
static bool parse_bank(const uint8_t *d, size_t n) { SpriteBank b; bool ok = sprite_bank_parse(d, n, &b); if (ok) sprite_bank_free(&b); return ok; }
static bool parse_refs(const uint8_t *d, size_t n) { SpriteRefTable t; bool ok = sprite_refs_parse(d, n, &t); if (ok) sprite_refs_free(&t); return ok; }
static bool parse_font(const uint8_t *d, size_t n) { AssetFont f; bool ok = font_parse(d, n, &f); if (ok) font_free(&f); return ok; }
static bool parse_pal(const uint8_t *d, size_t n) { PaletteSet p; bool ok = palettes_parse(d, n, &p); if (ok) palettes_free(&p); return ok; }
static bool parse_texts(const uint8_t *d, size_t n) { TextSet t; bool ok = texts_parse(d, n, &t); if (ok) texts_free(&t); return ok; }
static bool parse_levels(const uint8_t *d, size_t n) { LevelSet l; bool ok = levels_parse(d, n, &l); if (ok) levels_free(&l); return ok; }
static const uint8_t test_pcm[8] = {0x80, 1, 2, 3, 4, 5, 6, 7};
static bool parse_samples(const uint8_t *d, size_t n) { SampleSet s; bool ok = samples_parse(d, n, test_pcm, sizeof test_pcm, &s); if (ok) samples_free(&s); return ok; }
static bool parse_scores(const uint8_t *d, size_t n) { HighScoreTable t; return scores_parse(d, n, &t); }
static bool parse_config(const uint8_t *d, size_t n) { GameConfig c; return config_parse(d, n, &c); }
static bool parse_hiscore(const uint8_t *d, size_t n) { HiscoreData h; return hiscore_data_parse(d, n, &h); }
static bool parse_enemies(const uint8_t *d, size_t n) { EnemyTables e; return enemy_tables_parse(d, n, &e); }
static bool parse_hud(const uint8_t *d, size_t n) { HudGlyphs h; return hud_glyphs_parse(d, n, &h); }

static void test_sprites(void)
{
    Buf b = {0};
    header(&b, "LSPR", FORMAT_LSPR_VERSION);
    put16(&b, 2);
    /* frame 0: 16 x 2 at bank offset 0x40; frame 1: 16 x 1 at 0x80 */
    put16(&b, 0x40); put16(&b, 3); put16(&b, 16); put16(&b, 2); put16(&b, 12); put16(&b, 6); put16(&b, 0); put16(&b, 0); put32(&b, 0);
    put16(&b, 0x80); put16(&b, 3); put16(&b, 16); put16(&b, 1); put16(&b, 16); put16(&b, 8); put16(&b, 0); put16(&b, 0); put32(&b, 32);
    for (int i = 0; i < 48; i++)
        put8(&b, i % 3 == 0 ? GFX_TRANSPARENT : (unsigned)(i & 15));
    SpriteBank bank;
    CHECK(sprite_bank_parse(b.bytes, b.size, &bank));
    CHECK(bank.frame_count == 2);
    CHECK(bank.frames[0].width == 16 && bank.frames[0].height == 2 && bank.frames[0].nominal_width == 12);
    CHECK(bank.sprites[0].hotspot_x == 6 && bank.sprites[0].hotspot_y == 0 && bank.sprites[0].mask == NULL);
    CHECK(bank.sprites[1].pixels == b.bytes + 6 + 2 + 40 + 32);
    CHECK(bank.sprites[1].pixels[1] == GFX_TRANSPARENT); /* byte 33 */
    CHECK(sprite_bank_find(&bank, 0x80) == 1 && sprite_bank_find(&bank, 0x42) == -1);
    sprite_bank_free(&bank);
    check_rejects_damage(&b, parse_bank, "LSPR");

    Buf r = {0};
    header(&r, "LSRF", FORMAT_LSRF_VERSION);
    put16(&r, 4); put16(&r, 1); put16(&r, SPRITE_REF_LINK | 3); put16(&r, 0); put16(&r, SPRITE_REF_END);
    SpriteRefTable refs;
    CHECK(sprite_refs_parse(r.bytes, r.size, &refs));
    CHECK(refs.count == 4 && refs.refs[1] == (SPRITE_REF_LINK | 3) && refs.refs[3] == SPRITE_REF_END);
    sprite_refs_free(&refs);
    check_rejects_damage(&r, parse_refs, "LSRF");
    r.bytes[10] = 9; /* link 0x8009 points past the table */
    CHECK(!parse_refs(r.bytes, r.size));
}

static void test_font(void)
{
    Buf b = {0};
    header(&b, "LFNT", FORMAT_LFNT_VERSION);
    put16(&b, 14); put16(&b, FONT_CHAR_COUNT); put16(&b, 2);
    for (unsigned c = 0; c < FONT_CHAR_COUNT; c++)
        put16(&b, c == 'A' || c == 'a' ? 0 : c == 'B' ? 1 : 0xFFFF);
    put16(&b, 9); put16(&b, 3); put16(&b, 0);  /* glyph 0: rows 0..2 */
    put16(&b, 7); put16(&b, 1); put16(&b, 3);  /* glyph 1: row 3 */
    put16(&b, 0); put16(&b, 0x8001); put16(&b, 0xF00F); put16(&b, 0x1234);
    AssetFont font;
    CHECK(font_parse(b.bytes, b.size, &font));
    CHECK(font.line_height == 14 && font.gfx.first_char == 0 && font.gfx.glyph_count == FONT_CHAR_COUNT);
    CHECK(font.gfx.height == 3 && font.gfx.glyphs == font.glyphs);
    CHECK(font.glyphs['A'].advance == 9 && font.glyphs['A'].rows == 3 && font.glyphs['A'].bits[1] == 0x8001);
    CHECK(font.glyphs['a'].bits == font.glyphs['A'].bits);
    CHECK(font.glyphs['B'].rows == 1 && font.glyphs['B'].bits[0] == 0x1234);
    CHECK(font.glyphs['C'].rows == 0 && font.glyphs['C'].bits == NULL);
    font_free(&font);
    check_rejects_damage(&b, parse_font, "LFNT");

    Buf s = {0};
    for (int i = 0; i < SINE_STEPS; i++)
        put8(&s, (unsigned)(i == 64 ? 126 : i == 192 ? 0x82 : 0));
    for (int i = 0; i < SINE_STEPS; i++)
        put16(&s, 0);
    SineTables sine;
    CHECK(sine_parse(s.bytes, s.size, &sine));
    CHECK(sine.sine8[64] == 126 && sine.sine8[192] == -126);
    CHECK(!sine_parse(s.bytes, s.size - 1, &sine));
}

static void test_palettes(void)
{
    Buf b = {0};
    header(&b, "LPAL", FORMAT_LPAL_VERSION);
    for (int i = 0; i < VGA_ATTRIBUTE_REGISTERS; i++) put8(&b, (unsigned)i);
    for (int i = 0; i < 16; i++) put8(&b, (unsigned)(i * 4));
    put8(&b, 0);
    for (int i = 0; i < PULSE_VGA_STEPS; i++) put16(&b, (unsigned)(0x100 + i));
    for (int i = 0; i < PULSE_EGA_STEPS; i++) put8(&b, (unsigned)(0x30 + i));
    put16(&b, 1);
    put16(&b, 0x2144);
    for (int i = 0; i < GFX_GAME_COLORS; i++) put16(&b, (unsigned)(i * 0x111));
    for (int i = 0; i <= GFX_GAME_COLORS; i++) put8(&b, (unsigned)i);
    put8(&b, 0);
    put16(&b, 2);
    put16(&b, 0x6C9); put16(&b, 0); put16(&b, 1);
    put16(&b, 0x6CD); put16(&b, 1); put16(&b, 2);
    put16(&b, 3); put16(&b, 0x777); put16(&b, 0x700); put16(&b, 0x070);
    PaletteSet set;
    CHECK(palettes_parse(b.bytes, b.size, &set));
    CHECK(set.nibble_to_dac[15] == 60 && set.pulse_vga[15] == 0x10F && set.pulse_ega[7] == 0x37);
    const NamedPalette *p = palettes_find(&set, 0x2144);
    CHECK(p && p->palette.st_words[15] == 0xFFF && p->palette.ega_registers[16] == 16);
    CHECK(palettes_find(&set, 0x2175) == NULL);
    const FlashList *flash = palettes_find_flash_list(&set, 0x6CD);
    CHECK(flash && flash->word_count == 2 && flash->words[0] == 0x700 && flash->words[1] == 0x070);
    palettes_free(&set);
    check_rejects_damage(&b, parse_pal, "LPAL");
    b.bytes[b.size - 12] = 3; /* second list's first word 3: runs past the word area */
    CHECK(!parse_pal(b.bytes, b.size));
}

static void test_texts(void)
{
    /* one text "HI\c\x05\s\x00\x02\q<OK>\0\e" at DS:05A5, titled for level 4 */
    static const char raw[] = "HI\\c\x05\\s\x00\x02\\qOK\0\\e";
    Buf ops = {0};
    put8(&ops, TEXT_OP_GLYPHS); put16(&ops, 2); put_bytes(&ops, "HI", 2);
    put8(&ops, TEXT_OP_COLOUR); put8(&ops, 5);
    put8(&ops, TEXT_OP_SPRITE); put16(&ops, 2);
    put8(&ops, TEXT_OP_CENTRED); put16(&ops, 2); put_bytes(&ops, "OK", 2);
    put8(&ops, TEXT_OP_END);

    Buf b = {0};
    header(&b, "LTXT", FORMAT_LTXT_VERSION);
    put16(&b, 1); put16(&b, 0x05A5); put16(&b, 0); put16(&b, (unsigned)ops.size);
    put16(&b, 1); put16(&b, 4); put16(&b, 0);
    put16(&b, 1); put16(&b, 0x0463); put16(&b, 3); put_bytes(&b, "Bye", 3);
    put16(&b, (unsigned)ops.size); put_bytes(&b, (const char *)ops.bytes, ops.size);
    size_t raw_length = sizeof raw - 1;
    put16(&b, 0); put16(&b, (unsigned)raw_length);
    put16(&b, (unsigned)raw_length + 1); put_bytes(&b, raw, raw_length + 1);

    TextSet set;
    CHECK(texts_parse(b.bytes, b.size, &set));
    const GameText *text = texts_find(&set, 0x05A5);
    CHECK(text && text->raw_length == raw_length && memcmp(text->raw, raw, raw_length) == 0);
    CHECK(text && text->raw[raw_length] == '\0');
    CHECK(set.level_title_count == 1 && set.level_titles[0].level == 4 && set.level_titles[0].text_index == 0);
    CHECK(set.message_count == 1 && strcmp(set.messages[0].text, "Bye") == 0);

    const uint8_t *cursor = text->ops, *end = text->ops + text->op_size;
    TextOp op;
    CHECK(text_op_next(&cursor, end, &op) && op.code == TEXT_OP_GLYPHS && op.length == 2 && op.bytes[1] == 'I');
    CHECK(text_op_next(&cursor, end, &op) && op.code == TEXT_OP_COLOUR && op.value == 5);
    CHECK(text_op_next(&cursor, end, &op) && op.code == TEXT_OP_SPRITE && op.sprite == 2);
    CHECK(text_op_next(&cursor, end, &op) && op.code == TEXT_OP_CENTRED && memcmp(op.bytes, "OK", 2) == 0);
    CHECK(text_op_next(&cursor, end, &op) && op.code == TEXT_OP_END);
    CHECK(!text_op_next(&cursor, end, &op) && cursor == end);
    const uint8_t *cut = text->ops;
    CHECK(!text_op_next(&cut, text->ops + 2, &op)); /* GLYPHS op cut inside its length */
    texts_free(&set);
    check_rejects_damage(&b, parse_texts, "LTXT");
    b.bytes[b.size - 1] = '!'; /* raw text no longer NUL-terminated */
    CHECK(!parse_texts(b.bytes, b.size));
}

static void test_levels(void)
{
    Buf b = {0};
    header(&b, "LLVL", FORMAT_LLVL_VERSION);
    put16(&b, 2);
    put16(&b, 0x2CA4); put16(&b, 0); put16(&b, 2); put8(&b, 1); put8(&b, 1); put16(&b, 0);
    put16(&b, 0x2CB0); put16(&b, 2); put16(&b, 0); put8(&b, 3); put8(&b, 2); put16(&b, 1);
    put16(&b, 2);
    put8(&b, 0); put8(&b, 2); put8(&b, 0xFE); put8(&b, 2); put8(&b, 0); put8(&b, 0);
    put8(&b, 7); put8(&b, 1); put8(&b, 9); put8(&b, 0); put8(&b, 0); put8(&b, 0);
    put16(&b, 3); put8(&b, 5); put8(&b, 10); put8(&b, 6); put8(&b, 1); put8(&b, 40); put8(&b, 2);
    LevelSet set;
    CHECK(levels_parse(b.bytes, b.size, &set));
    CHECK(set.level_count == 2 && set.levels[0].op_count == 2 && set.levels[0].ops[0].operands[0] == 0xFE);
    CHECK(set.levels[0].ops[1].handler == 7 && set.levels[0].spawns[0].enemy_type == 5);
    CHECK(set.levels[1].wave_type == 3 && set.levels[1].spawn_count == 2 && set.levels[1].spawns[1].enemy_type == 40);
    levels_free(&set);
    check_rejects_damage(&b, parse_levels, "LLVL");
    b.bytes[b.size - 6 - 2 - 12] = 8; /* handler 8 does not exist */
    CHECK(!parse_levels(b.bytes, b.size));
}

static void test_samples_scores_config(void)
{
    Buf b = {0};
    header(&b, "LSMP", FORMAT_LSMP_VERSION);
    put16(&b, 2); put16(&b, 5);
    put8(&b, 0x9C); put8(&b, 0xA6); put8(&b, 0xB0); put8(&b, 0xC0); put8(&b, 0xD3); put8(&b, 0);
    put32(&b, 0); put32(&b, 3); put32(&b, 2); put32(&b, 6);
    SampleSet set;
    CHECK(samples_parse(b.bytes, b.size, test_pcm, sizeof test_pcm, &set));
    CHECK(set.sample_count == 2 && set.samples[1].pcm == test_pcm + 2 && set.samples[1].length == 6);
    CHECK(samples_rate_hz(&set, 0) == 10000 && samples_rate_hz(&set, 5) == 0);
    samples_free(&set);
    check_rejects_damage(&b, parse_samples, "LSMP");
    CHECK(!samples_parse(b.bytes, b.size, test_pcm, 7, &set)); /* sample 2 past the PCM */

    Buf s = {0};
    header(&s, "LSCO", FORMAT_LSCO_VERSION);
    put16(&s, 2);
    put32(&s, 50000); put_bytes(&s, "JEFF    ", 8);
    put32(&s, 100); put_bytes(&s, "LLAMA   ", 8);
    HighScoreTable scores;
    CHECK(scores_parse(s.bytes, s.size, &scores));
    CHECK(scores.count == 2 && scores.entries[0].score == 50000 && strcmp(scores.entries[1].name, "LLAMA   ") == 0);
    check_rejects_damage(&s, parse_scores, "LSCO");
    uint8_t yak[SCORES_YAK_SIZE];
    CHECK(!scores_format_yak(&scores, yak)); /* only 2 entries */
    HighScoreTable full = {HIGH_SCORE_ENTRIES, {{0}}};
    full.entries[0] = scores.entries[0];
    full.entries[9].score = 7;
    CHECK(scores_format_yak(&full, yak) && memcmp(yak, "00050000JEFF    ", 16) == 0 && memcmp(yak + 144, "00000007", 8) == 0);
    full.entries[3].score = 100000000u;
    CHECK(!scores_format_yak(&full, yak));
    Buf empty = {0};
    header(&empty, "LSCO", FORMAT_LSCO_VERSION);
    put16(&empty, 0);
    CHECK(scores_parse(empty.bytes, empty.size, &scores) && scores.count == 0);

    Buf c = {0};
    header(&c, "LCFG", FORMAT_LCFG_VERSION);
    put8(&c, 1); put8(&c, 1); put8(&c, 0); put8(&c, SOUND_DEVICE_SBLASTER); put8(&c, 0); put8(&c, 2);
    GameConfig config;
    CHECK(config_parse(c.bytes, c.size, &config));
    CHECK(config.present && config.vga && !config.joystick && config.sound == SOUND_DEVICE_SBLASTER && config.flash_mode == 2);
    check_rejects_damage(&c, parse_config, "LCFG");
    c.bytes[9] = 0x42;
    CHECK(!config_parse(c.bytes, c.size, &config));

    Buf h = {0};
    header(&h, "LHUD", FORMAT_LHUD_VERSION);
    for (int i = 0; i < (HUD_DIGIT_COUNT + HUD_PLAYER_LABEL_COUNT) * HUD_GLYPH_ROWS; i++)
        put8(&h, (unsigned)i);
    HudGlyphs hud;
    CHECK(hud_glyphs_parse(h.bytes, h.size, &hud));
    CHECK(hud.digits[1][0] == 8 && hud.digits[9][7] == 79 && hud.player_label[1][7] == 95);
    check_rejects_damage(&h, parse_hud, "LHUD");
    put8(&h, 0);
    CHECK(!parse_hud(h.bytes, h.size));

    Buf x = {0};
    header(&x, "LHSC", FORMAT_LHSC_VERSION);
    for (int i = 0; i < HISCORE_ALPHABET_SIZE; i++)
        put8(&x, (unsigned)('A' + i % 26));
    for (int i = 0; i < HISCORE_ROW_COUNT; i++)
        put8(&x, (unsigned)i + 1);
    put_bytes(&x, "WXYZ", 4);
    HiscoreData hiscore;
    CHECK(hiscore_data_parse(x.bytes, x.size, &hiscore));
    CHECK(strlen(hiscore.alphabet) == HISCORE_ALPHABET_SIZE && hiscore.alphabet[26] == 'A');
    CHECK(hiscore.row_colours[9] == 10 && memcmp(hiscore.rude_word, "WXYZ", 4) == 0);
    check_rejects_damage(&x, parse_hiscore, "LHSC");

    Buf e = {0};
    header(&e, "LENM", FORMAT_LENM_VERSION);
    put16(&e, 2); put16(&e, 4); put16(&e, 6); put16(&e, 0xFFFF); put16(&e, 0xFFFF);
    put8(&e, 1); put8(&e, 0xFF); put8(&e, 2); put8(&e, 0xFE);
    for (int i = 0; i < PICKUP_KINDS; i++) put16(&e, (unsigned)(0x100 + i));
    put16(&e, DEBRIS_TEMPLATE_COUNT);
    for (int i = 0; i < DEBRIS_TEMPLATE_COUNT; i++) { put16(&e, (unsigned)i); put16(&e, 0xFFFE); put16(&e, 3); }
    put8(&e, 7); put8(&e, 1); put8(&e, 5); put8(&e, 3);
    for (int i = 0; i < 5 * ENEMY_DIRECTIONS; i++) put16(&e, (unsigned)(0xFFF0 + i));
    EnemyTables enemies;
    CHECK(enemy_tables_parse(e.bytes, e.size, &enemies));
    CHECK(enemies.score_award_count == 2 && enemies.score_awards[0].count_minus_1 == 4 && enemies.score_awards[0].digit == 6);
    CHECK(enemies.drop_by_direction[1] == -1 && enemies.drop_by_direction[3] == -2 && enemies.pickup_sprites[9] == 0x109);
    CHECK(enemies.debris[15].sprite == 15 && enemies.debris[15].vx == -2 && enemies.debris[15].vy == 3);
    CHECK(enemies.seek_dir_by_quadrant[0] == 7 && enemies.seek_dir_by_quadrant[3] == 3);
    CHECK(enemies.seek_vx[0] == -16 && enemies.seek_vy[0] == -8 && enemies.wander_sprite[7] == 0x17);
    check_rejects_damage(&e, parse_enemies, "LENM");
}

int main(void)
{
    test_sprites();
    test_font();
    test_palettes();
    test_texts();
    test_levels();
    test_samples_scores_config();
    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    else
        printf("formats: all checks passed\n");
    return failures ? 1 : 0;
}
