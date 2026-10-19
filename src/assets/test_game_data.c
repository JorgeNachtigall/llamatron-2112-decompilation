/* test_game_data.c - loads the real extracted data through asset_get() and checks it against
 * asset_index.h and the original's invariants. Prints counts only, never data.
 *
 *   test_game_data [--data DIR]    (default: whatever the linked asset store provides) */
#include "game_data.h"

#include "asset_index.h"
#include "assets.h"

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

enum { ENEMY_TYPES = 41 }; /* tbl_enemy_init entries */

static void check_bank(SpriteBankId id, unsigned expected)
{
    const SpriteBank *bank = asset_sprite_bank(id);
    CHECK(bank && bank->frame_count == expected);
    for (unsigned i = 0; bank && i < bank->frame_count; i++) {
        const GfxSprite *s = asset_sprite_gfx(id, i);
        const SpriteFrame *f = asset_sprite_frame(id, i);
        CHECK(s && s->mask == NULL && s->width == (int16_t)f->width && s->width % 16 == 0);
        CHECK(f->kind == 3 && f->hotspot_y == (f->height - 1) >> 1);
        CHECK(asset_sprite_index_at(id, f->bank_offset) == (int)i);
        size_t bad_pixels = 0; /* colours are 0..15 or GFX_TRANSPARENT */
        for (size_t p = 0; p < (size_t)f->width * f->height; p++)
            bad_pixels += f->pixels[p] > 15 && f->pixels[p] != GFX_TRANSPARENT;
        CHECK(bad_pixels == 0);
    }
    CHECK(asset_sprite_gfx(id, expected) == NULL);
}

static void check_sprites(void)
{
    check_bank(SPRITE_BANK_GAME, LLAMA_SPRITE_FRAME_COUNT);
    check_bank(SPRITE_BANK_TITLE, LLAMA_G3_SHAPE_COUNT);
    const SpriteRefTable *refs = asset_sprite_refs();
    CHECK(refs && refs->count == LLAMA_SPRITE_REF_COUNT);
    unsigned frames = 0;
    for (unsigned i = 0; refs && i < refs->count; i++) {
        uint16_t ref = refs->refs[i];
        if (ref & SPRITE_REF_LINK)
            CHECK(asset_sprite_ref_gfx(i) == NULL);
        else {
            CHECK(asset_sprite_ref_gfx(i) == asset_sprite_gfx(SPRITE_BANK_GAME, ref));
            frames += asset_sprite_ref_gfx(i) != NULL;
        }
    }
    CHECK(frames > 0);
    CHECK(asset_sprite_number_gfx(0) == asset_sprite_ref_gfx(SPRITE_NUMBER_BASE));
    CHECK(asset_sprite_ref_gfx(LLAMA_SPRITE_REF_COUNT) == NULL);
}

static void check_font(void)
{
    const GfxFont *font = asset_font_gfx();
    CHECK(font && font->first_char == 0 && font->glyph_count == FONT_CHAR_COUNT);
    CHECK(asset_font_line_height() == 14);
    for (unsigned c = 0; font && c < FONT_CHAR_COUNT; c++) {
        const GfxGlyph *g = &font->glyphs[c];
        if (c < 32)
            CHECK(g->rows == 0 && g->bits == NULL);
        if (c >= 'A' && c <= 'Z')
            CHECK(g->rows > 0 && g->advance > 0 && g->bits != NULL && g->rows <= font->height);
    }
    const SineTables *sine = asset_sine();
    CHECK(sine && sine->sine8[0] == 0 && sine->sine8[64] == 126 && sine->sine8[192] == -126);
}

static void check_palettes(void)
{
    static const uint16_t palettes[] = {0x2144, 0x2164, 0x2175};
    static const uint16_t flash_lists[] = {0x6C9, 0x6CD, 0x6FD, 0x707, 0x713, 0x721, 0x729, 0x72D};
    for (size_t i = 0; i < sizeof palettes / sizeof *palettes; i++)
        CHECK(asset_palette_at(palettes[i]) != NULL);
    for (size_t i = 0; i < sizeof flash_lists / sizeof *flash_lists; i++) {
        const FlashList *list = asset_flash_list_at(flash_lists[i]);
        CHECK(list && list->word_count > 0);
        for (uint16_t w = 0; list && w < list->word_count; w++)
            CHECK(list->words[w] != 0 && (list->words[w] & ~0x1FFFu) == 0);
    }
    const GfxGamePalette *black = asset_palette_at(0x2175);
    for (int c = 0; black && c < GFX_GAME_COLORS; c++)
        CHECK(black->st_words[c] == 0);
    CHECK(asset_palette_at(0x2146) == NULL);
}

static void check_texts(void)
{
    const TextSet *set = asset_texts();
    CHECK(set && set->text_count == LLAMA_TEXT_COUNT && set->level_title_count == LLAMA_LEVEL_TITLE_COUNT);
    CHECK(set && set->message_count == LLAMA_MESSAGE_COUNT);
    for (uint16_t i = 0; set && i < set->text_count; i++) {
        const GameText *text = &set->texts[i];
        CHECK(asset_text_at(text->ds_offset) == text && asset_text_raw(text->ds_offset) == text->raw);
        CHECK(text->raw_length >= 2 && memcmp(text->raw + text->raw_length - 2, "\\e", 2) == 0);
        CHECK(text->raw[text->raw_length] == '\0');
        /* the ops end with exactly one END, at op_size */
        const uint8_t *cursor = text->ops, *end = text->ops + text->op_size;
        TextOp op = {0};
        size_t glyph_bytes = 0;
        while (text_op_next(&cursor, end, &op) && op.code != TEXT_OP_END)
            if (op.code == TEXT_OP_GLYPHS || op.code == TEXT_OP_CENTRED)
                glyph_bytes += op.length;
        CHECK(op.code == TEXT_OP_END && cursor == end && glyph_bytes < text->raw_length);
    }
    CHECK(asset_text_raw(0x05A5) != NULL && asset_text_raw(0x05A6) == NULL);
    CHECK(asset_level_title(0) != NULL && asset_level_title(1) == NULL);
    for (uint16_t i = 0; set && i < set->message_count; i++) {
        const char *message = asset_dos_message_at(set->messages[i].ds_offset);
        CHECK(message && strlen(message) == set->messages[i].length && !strchr(message, '$'));
    }
}

static void check_levels(void)
{
    static const uint8_t operand_counts[LEVEL_OP_HANDLERS] = {2, 4, 4, 2, 2, 2, 3, 1};
    const LevelSet *set = asset_levels();
    CHECK(set && set->level_count == LLAMA_LEVEL_COUNT);
    for (unsigned l = 0; set && l < set->level_count; l++) {
        const LevelScript *level = asset_level(l);
        CHECK(level->wave_type < 0x80 && level->spawn_count > 0);
        for (uint16_t i = 0; i < level->op_count; i++)
            CHECK(level->ops[i].operand_count == operand_counts[level->ops[i].handler]);
        for (uint16_t i = 0; i < level->spawn_count; i++)
            CHECK(level->spawns[i].enemy_type < ENEMY_TYPES);
    }
    CHECK(asset_level(LLAMA_LEVEL_COUNT) == NULL);
}

static void check_samples_scores_config(void)
{
    CHECK(asset_sample(0) == NULL && asset_sample(LLAMA_SAMPLE_COUNT + 1) == NULL);
    for (unsigned n = 1; n <= LLAMA_SAMPLE_COUNT; n++) {
        const Sample *s = asset_sample(n);
        CHECK(s && s->pcm && s->length > 0);
    }
    for (unsigned r = 0; r < LLAMA_SAMPLE_RATE_COUNT; r++)
        CHECK(asset_sample_rate_hz(r) >= 3000 && asset_sample_rate_hz(r) <= 45000);
    CHECK(asset_sample_rate_hz(LLAMA_SAMPLE_RATE_COUNT) == 0);

    const HighScoreTable *defaults = asset_default_scores(), *user = asset_user_scores();
    CHECK(defaults && defaults->count == HIGH_SCORE_ENTRIES);
    for (int i = 1; defaults && i < defaults->count; i++)
        CHECK(defaults->entries[i].score <= defaults->entries[i - 1].score);
    CHECK(user && (user->count == 0 || user->count == HIGH_SCORE_ENTRIES));
    CHECK(asset_config() != NULL);
    const HudGlyphs *hud = asset_hud_glyphs();
    CHECK(hud != NULL);
    for (int d = 0; hud && d < HUD_DIGIT_COUNT; d++) { /* every glyph draws something */
        unsigned bits = 0;
        for (int row = 0; row < HUD_GLYPH_ROWS; row++)
            bits |= hud->digits[d][row];
        CHECK(bits != 0);
    }
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc)
            assets_set_directory(argv[++i]);
    CHECK(asset_font_gfx() == NULL); /* nothing before game_data_load() */
    if (!game_data_load()) {
        fprintf(stderr, "game_data_load failed\n");
        return 1;
    }
    CHECK(game_data_load());
    check_sprites();
    check_font();
    check_palettes();
    check_texts();
    check_levels();
    check_samples_scores_config();
    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    else
        printf("game data (%s): all checks passed\n", assets_embedded() ? "embedded" : "files");
    return failures ? 1 : 0;
}
