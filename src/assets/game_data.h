/*
 * game_data.h - the game's extracted data, loaded once through asset_get() (src/platform/assets.h)
 * and kept for the whole run. Record types and their meaning: formats.h.
 *
 * Call game_data_load() once at start-up; the accessors return NULL / 0 before that or for an
 * index out of range. Nothing returned here is ever freed.
 */
#ifndef LLAMATRON_GAME_DATA_H
#define LLAMATRON_GAME_DATA_H

#include "formats.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Loads and checks every data file; false (with a message on stderr) if one is missing, stale
 * or malformed. Calling it again after success does nothing. */
bool game_data_load(void);

/* ---- Sprites ---- */

typedef enum SpriteBankId {
    SPRITE_BANK_GAME,   /* sprites.bin: game86's bank at segment 218C */
    SPRITE_BANK_TITLE,  /* g3.bin: the 16 title shapes of g3.cmp */
} SpriteBankId;

const SpriteBank *asset_sprite_bank(SpriteBankId bank);
const SpriteFrame *asset_sprite_frame(SpriteBankId bank, unsigned index);
/* The frame as a GfxSprite (mask NULL, GFX_TRANSPARENT pixels, centred hotspot). */
const GfxSprite *asset_sprite_gfx(SpriteBankId bank, unsigned index);
/* Frame index of an original sprite pointer (offset within its bank), or -1. */
int asset_sprite_index_at(SpriteBankId bank, uint16_t bank_offset);

const SpriteRefTable *asset_sprite_refs(void);
/* sprite_refs.bin entry `ref_index` (original offset 218C:ref_index*2) as a frame of the game
 * bank; NULL when the entry is a link or a terminator. */
const GfxSprite *asset_sprite_ref_gfx(unsigned ref_index);
/* Sprite number n of \s and of an entity's sprite field: refs[SPRITE_NUMBER_BASE + n]. */
const GfxSprite *asset_sprite_number_gfx(unsigned number);

/* ---- Font, sine ---- */

const AssetFont *asset_font(void);
const GfxFont *asset_font_gfx(void);   /* first_char 0, 128 glyphs, missing ones rows 0 */
int asset_font_line_height(void);      /* 14 */
const SineTables *asset_sine(void);

/* ---- Palettes ---- */

const PaletteSet *asset_palettes(void);
/* Palette the original passes to palette_set as DS:ds_offset (0x2144, 0x2164, 0x2175). */
const GfxGamePalette *asset_palette_at(uint16_t ds_offset);
/* Flash list by name (store it in GameState.flash_list; NULL = none). */
const FlashList *asset_flash_list(FlashListId id);
/* Flash list the original stores in g_flash_list as DS:ds_offset (for adapters/tests). */
const FlashList *asset_flash_list_at(uint16_t ds_offset);

/* ---- Texts ---- */

const TextSet *asset_texts(void);
const GameText *asset_text_at(uint16_t ds_offset);
/* Raw bytes of the text at DS:ds_offset (see GameText.raw), or NULL. */
const char *asset_text_raw(uint16_t ds_offset);
/* Title text shown before `level` (DS:2340 table), or NULL. */
const GameText *asset_level_title(int level);
/* DOS message at DS:ds_offset, NUL-terminated without the '$', or NULL. */
const char *asset_dos_message_at(uint16_t ds_offset);

/* ---- Levels, samples, scores, config ---- */

const LevelSet *asset_levels(void);
const LevelScript *asset_level(unsigned level);
const SampleSet *asset_samples(void);
/* The game's sample number (1..32, AL of the sound request), or NULL. */
const Sample *asset_sample(unsigned number);
unsigned asset_sample_rate_hz(unsigned rate_index);
const HighScoreTable *asset_default_scores(void);
const HighScoreTable *asset_user_scores(void);   /* count 0: no usable scores.yak */
const GameConfig *asset_config(void);
const HudGlyphs *asset_hud_glyphs(void);
const HiscoreData *asset_hiscore_data(void);
const EnemyTables *asset_enemy_tables(void);

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_GAME_DATA_H */
