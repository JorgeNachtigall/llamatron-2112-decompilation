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

    const SpriteBank *bank = asset_sprite_bank(SPRITE_BANK_GAME);
    for (int id = 0; bank && id < SPRITE_ID_COUNT; id++) {
        int frame = asset_sprite_id_index((SpriteId)id);
        CHECK(frame >= 0 && asset_sprite_id_gfx((SpriteId)id) == asset_sprite_gfx(SPRITE_BANK_GAME, (unsigned)frame));
        for (int other = 0; other < id; other++) /* every name is a different frame */
            CHECK(asset_sprite_id_index((SpriteId)other) != frame);
    }
    /* anim 10 draws 218C:92A4 by pointer, the comment there says it is sprite number 0xc5 */
    CHECK(asset_sprite_id_gfx(SPRITE_ID_POWERUP_MARK) == asset_sprite_number_gfx(0xc5));
    CHECK(asset_sprite_id_index(SPRITE_ID_BEAM_HEAD) == asset_sprite_index_at(SPRITE_BANK_GAME, 0x7aa2)); /* 1000:46b6 */
    CHECK(asset_sprite_id_gfx(SPRITE_ID_COUNT) == NULL);
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
    /* FlashListId order = the extractor's order */
    CHECK(asset_palettes() && asset_palettes()->flash_list_count == FLASH_LIST_COUNT);
    for (int id = 0; id < FLASH_LIST_COUNT; id++)
        CHECK(asset_flash_list((FlashListId)id) == asset_flash_list_at(flash_lists[id]));
    CHECK(asset_flash_list(FLASH_LIST_COUNT) == NULL);
    const GfxGamePalette *black = asset_palette_at(0x2175);
    for (int c = 0; black && c < GFX_GAME_COLORS; c++)
        CHECK(black->st_words[c] == 0);
    CHECK(asset_palette_at(0x2146) == NULL);
    for (int id = 0; id < PALETTE_ID_COUNT; id++)
        CHECK(asset_palette((PaletteId)id) == asset_palette_at(palettes[id]));
    CHECK(asset_palette(PALETTE_ID_BLACK) == black && asset_palette(PALETTE_ID_COUNT) == NULL);
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
    /* named IDs: the call-site offsets listed in formats.h, every one a different entry */
    static const uint16_t text_ids[] = {0x05A5, 0x05DB, 0x05F4, 0x094B, 0x09B5, 0x0AAE, 0x0B7A, 0x117A, 0x14E1, 0x1BC8};
    _Static_assert(sizeof text_ids / sizeof *text_ids == TEXT_ID_COUNT, "text ids");
    for (int id = 0; id < TEXT_ID_COUNT; id++)
        CHECK(asset_text((TextId)id) != NULL && asset_text((TextId)id) == asset_text_at(text_ids[id]));
    CHECK(asset_text(TEXT_ID_COUNT) == NULL);
    static const uint16_t message_ids[] = {0x0463, 0x0471, 0x04B3, 0x04E1, 0x0520, 0x0563};
    _Static_assert(sizeof message_ids / sizeof *message_ids == MESSAGE_ID_COUNT, "message ids");
    for (int id = 0; id < MESSAGE_ID_COUNT; id++)
        CHECK(asset_dos_message((MessageId)id) != NULL && asset_dos_message((MessageId)id) == asset_dos_message_at(message_ids[id]));
    CHECK(MESSAGE_ID_COUNT == LLAMA_MESSAGE_COUNT && asset_dos_message(MESSAGE_ID_COUNT) == NULL);
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
    /* the current originals: think_13's clobber hits level 21, pairs 2..3 */
    CHECK(set && set->clobber_10b0.level == 21 && set->clobber_10b0.first_spawn == 2 &&
          set->clobber_10b0.spawn_count == 2);
    for (int i = 0; set && i < LEVEL_VARIANTS; i++) /* sprite numbers, entity +0C */
        CHECK(asset_sprite_number_gfx(set->spawn_sprites[i]) != NULL && asset_sprite_number_gfx(set->warp_sprites[i]) != NULL);
    for (int i = 0; set && i < WARP_BOX_COLOUR_COUNT; i++) /* colours 1..15 (0 would be invisible) */
        CHECK((set->warp_box_colours[i] & 0x0F) != 0);
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
    /* parse(format_yak(t)) == t for the real tables */
    for (int t = 0; t < 2; t++) {
        const HighScoreTable *table = t ? user : defaults;
        if (!table || table->count == 0)
            continue;
        uint8_t yak[SCORES_YAK_SIZE];
        CHECK(scores_format_yak(table, yak));
        for (int i = 0; i < HIGH_SCORE_ENTRIES; i++) {
            uint32_t score = 0;
            for (int d = 0; d < 8; d++)
                score = score * 10 + (uint32_t)(yak[i * 16 + d] - '0');
            CHECK(score == table->entries[i].score && memcmp(yak + i * 16 + 8, table->entries[i].name, 8) == 0);
        }
    }
    CHECK(asset_config() != NULL);
    const EnemyTables *enemies = asset_enemy_tables();
    CHECK(enemies && enemies->score_award_count == LLAMA_SCORE_AWARD_COUNT);
    for (uint16_t i = 0; enemies && i + 1u < enemies->score_award_count; i++)
        CHECK(enemies->score_awards[i].digit <= 7);
    CHECK(enemies && enemies->score_awards[enemies->score_award_count - 1].digit == 0xFFFF);
    for (int i = 0; enemies && i < PICKUP_KINDS; i++) /* sprite numbers resolve through the refs */
        CHECK(asset_sprite_number_gfx(enemies->pickup_sprites[i]) != NULL);
    for (int i = 0; enemies && i < SEEK_QUADRANTS; i++)
        CHECK(enemies->seek_dir_by_quadrant[i] >= 0 && enemies->seek_dir_by_quadrant[i] < ENEMY_DIRECTIONS);
    for (int i = 0; enemies && i < 4; i++) /* the original's overlap of DS:1FB2 and DS:1FBA */
        CHECK(enemies->wander_vy[4 + i] == enemies->wander_sprite[i]);
    const ControlTables *controls = asset_control_tables();
    CHECK(controls && controls->shot_direction[0] == 0 && controls->move_vx[0] == 0 && controls->move_vy[0] == 0);
    for (int i = 0; controls && i < INPUT_NIBBLES; i++) /* spread entries are input nibbles */
        CHECK(controls->spread_a[i] < INPUT_NIBBLES && controls->spread_b[i] < INPUT_NIBBLES);
    for (int i = 0; controls && i < DROID_QUADRANTS; i++)
        CHECK(controls->droid_aim[i] < INPUT_NIBBLES);
    for (int i = 0; controls && i < INPUT_NIBBLES; i++) { /* a bounce mirrors vx or vy, 0 = none */
        uint8_t x = controls->shot_bounce_x[i], y = controls->shot_bounce_y[i];
        CHECK(x < INPUT_NIBBLES && y < INPUT_NIBBLES);
        if (x && x < INPUT_NIBBLES)
            CHECK(controls->shot_vx[x] == -controls->shot_vx[i] && controls->shot_vy[x] == controls->shot_vy[i]);
        if (y && y < INPUT_NIBBLES)
            CHECK(controls->shot_vy[y] == -controls->shot_vy[i] && controls->shot_vx[y] == controls->shot_vx[i]);
    }
    const TitleTables *title = asset_title_tables();
    CHECK(title && title->tune_length >= 3 && title->tune[title->tune_length - 1] == 0);
    for (int i = 0; title && i + 1 < title->tune_length; i += 2) /* rates and sample numbers */
        CHECK(title->tune[i] < LLAMA_SAMPLE_RATE_COUNT && asset_sample(title->tune[i + 1]) != NULL);
    for (int i = 0; title && i < TITLE_LETTER_SPRITE_COUNT; i++)
        CHECK(asset_sprite_number_gfx(title->letter_sprites[i]) != NULL);
    const HiscoreData *hiscore = asset_hiscore_data();
    CHECK(hiscore && strlen(hiscore->alphabet) == HISCORE_ALPHABET_SIZE);
    for (int i = 0; hiscore && i < HISCORE_RUDE_WORD_SIZE; i++)
        CHECK(hiscore->rude_word[i] >= 'a' && hiscore->rude_word[i] <= 'z'); /* the grid is lower case */
    for (int i = 0; hiscore && i < HISCORE_ROW_COUNT; i++)
        CHECK(hiscore->row_colours[i] != 0);
    /* the high-score screens' texts (1000:5a6e.., 5c9f) */
    static const uint16_t hiscore_texts[] = {0x094B, 0x09B5, 0x0AAE};
    for (size_t i = 0; i < sizeof hiscore_texts / sizeof *hiscore_texts; i++)
        CHECK(asset_text_raw(hiscore_texts[i]) != NULL);
    const GameText *prompt = asset_text_at(0x09B5);
    CHECK(prompt && prompt->raw_length >= 3 && prompt->raw[prompt->raw_length - 3] == '\0');
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
