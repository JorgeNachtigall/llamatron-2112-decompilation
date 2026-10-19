/* game_data.c - loads every data file once through asset_get() and serves the accessors. */
#include "game_data.h"

#include "asset_index.h"
#include "assets.h"

#include <stdio.h>

/* A stale build/generated would fail at run time anyway; catch it at compile time. */
_Static_assert(LLAMA_FMT_LFNT_VERSION == FORMAT_LFNT_VERSION, "font.bin version");
_Static_assert(LLAMA_FMT_LSPR_VERSION == FORMAT_LSPR_VERSION, "sprites.bin version");
_Static_assert(LLAMA_FMT_LSRF_VERSION == FORMAT_LSRF_VERSION, "sprite_refs.bin version");
_Static_assert(LLAMA_FMT_LSMP_VERSION == FORMAT_LSMP_VERSION, "samples.idx version");
_Static_assert(LLAMA_FMT_LPAL_VERSION == FORMAT_LPAL_VERSION, "palettes.dat version");
_Static_assert(LLAMA_FMT_LSCO_VERSION == FORMAT_LSCO_VERSION, "scores version");
_Static_assert(LLAMA_FMT_LCFG_VERSION == FORMAT_LCFG_VERSION, "config.dat version");
_Static_assert(LLAMA_FMT_LTXT_VERSION == FORMAT_LTXT_VERSION, "strings.dat version");
_Static_assert(LLAMA_FMT_LLVL_VERSION == FORMAT_LLVL_VERSION, "levels.dat version");
_Static_assert(LLAMA_FMT_LHUD_VERSION == FORMAT_LHUD_VERSION, "hud_glyphs.dat version");
_Static_assert(LLAMA_FMT_LHSC_VERSION == FORMAT_LHSC_VERSION, "hiscore.dat version");
_Static_assert(LLAMA_FMT_LENM_VERSION == FORMAT_LENM_VERSION, "enemies.dat version");
_Static_assert(LLAMA_FMT_LCTL_VERSION == FORMAT_LCTL_VERSION, "controls.dat version");
_Static_assert(LLAMA_FMT_LTTL_VERSION == FORMAT_LTTL_VERSION, "title.dat version");
_Static_assert(LLAMA_FMT_LNSP_VERSION == FORMAT_LNSP_VERSION, "named_sprites.dat version");
_Static_assert(LLAMA_NAMED_SPRITE_COUNT == SPRITE_ID_COUNT, "named_sprites.dat names");
_Static_assert(LLAMA_PAUSE_PICTURE_COUNT == PAUSE_PICTURE_COUNT, "pause pictures");

static struct {
    bool loaded;
    SpriteBank banks[2];
    SpriteRefTable refs;
    AssetFont font;
    SineTables sine;
    PaletteSet palettes;
    TextSet texts;
    LevelSet levels;
    SampleSet samples;
    HighScoreTable default_scores, user_scores;
    GameConfig config;
    HudGlyphs hud_glyphs;
    HiscoreData hiscore;
    EnemyTables enemies;
    ControlTables controls;
    TitleTables title;
    NamedSprites named_sprites;
} data;

static bool report(const char *name)
{
    fprintf(stderr, "game data: %s: %s\n", name, format_error());
    return false;
}

bool game_data_load(void)
{
    if (data.loaded)
        return true;
    size_t size, pcm_size;
    const uint8_t *bytes, *pcm;
#define LOAD(name, parse_call) \
    ((bytes = asset_get(name, &size)) != NULL && ((parse_call) || report(name)))
    bool ok = LOAD(LLAMA_ASSET_SPRITES_BIN, sprite_bank_parse(bytes, size, &data.banks[SPRITE_BANK_GAME])) &&
              LOAD(LLAMA_ASSET_G3_BIN, sprite_bank_parse(bytes, size, &data.banks[SPRITE_BANK_TITLE])) &&
              LOAD(LLAMA_ASSET_SPRITE_REFS_BIN, sprite_refs_parse(bytes, size, &data.refs)) &&
              LOAD(LLAMA_ASSET_NAMED_SPRITES_DAT, named_sprites_parse(bytes, size, data.banks[SPRITE_BANK_GAME].frame_count,
                                                                      &data.named_sprites)) &&
              LOAD(LLAMA_ASSET_FONT_BIN, font_parse(bytes, size, &data.font)) &&
              LOAD(LLAMA_ASSET_SINE_BIN, sine_parse(bytes, size, &data.sine)) &&
              LOAD(LLAMA_ASSET_PALETTES_DAT, palettes_parse(bytes, size, &data.palettes)) &&
              LOAD(LLAMA_ASSET_STRINGS_DAT, texts_parse(bytes, size, &data.texts)) &&
              LOAD(LLAMA_ASSET_LEVELS_DAT, levels_parse(bytes, size, &data.levels)) &&
              LOAD(LLAMA_ASSET_DEFAULT_SCORES_DAT, scores_parse(bytes, size, &data.default_scores)) &&
              LOAD(LLAMA_ASSET_USER_SCORES_DAT, scores_parse(bytes, size, &data.user_scores)) &&
              LOAD(LLAMA_ASSET_CONFIG_DAT, config_parse(bytes, size, &data.config)) &&
              LOAD(LLAMA_ASSET_HUD_GLYPHS_DAT, hud_glyphs_parse(bytes, size, &data.hud_glyphs)) &&
              LOAD(LLAMA_ASSET_HISCORE_DAT, hiscore_data_parse(bytes, size, &data.hiscore)) &&
              LOAD(LLAMA_ASSET_ENEMIES_DAT, enemy_tables_parse(bytes, size, &data.enemies)) &&
              LOAD(LLAMA_ASSET_CONTROLS_DAT, control_tables_parse(bytes, size, &data.controls)) &&
              LOAD(LLAMA_ASSET_TITLE_DAT, title_tables_parse(bytes, size, &data.title)) &&
              (pcm = asset_get(LLAMA_ASSET_SAMPLES_BIN, &pcm_size)) != NULL &&
              LOAD(LLAMA_ASSET_SAMPLES_IDX, samples_parse(bytes, size, pcm, pcm_size, &data.samples));
#undef LOAD
    data.loaded = ok;
    return ok;
}

const SpriteBank *asset_sprite_bank(SpriteBankId bank)
{
    return data.loaded && (unsigned)bank < 2 ? &data.banks[bank] : NULL;
}

const SpriteFrame *asset_sprite_frame(SpriteBankId bank, unsigned index)
{
    const SpriteBank *b = asset_sprite_bank(bank);
    return b && index < b->frame_count ? &b->frames[index] : NULL;
}

const GfxSprite *asset_sprite_gfx(SpriteBankId bank, unsigned index)
{
    const SpriteBank *b = asset_sprite_bank(bank);
    return b && index < b->frame_count ? &b->sprites[index] : NULL;
}

int asset_sprite_index_at(SpriteBankId bank, uint16_t bank_offset)
{
    const SpriteBank *b = asset_sprite_bank(bank);
    return b ? sprite_bank_find(b, bank_offset) : -1;
}

const SpriteRefTable *asset_sprite_refs(void)
{
    return data.loaded ? &data.refs : NULL;
}

const GfxSprite *asset_sprite_ref_gfx(unsigned ref_index)
{
    if (!data.loaded || ref_index >= data.refs.count || (data.refs.refs[ref_index] & SPRITE_REF_LINK))
        return NULL;
    return asset_sprite_gfx(SPRITE_BANK_GAME, data.refs.refs[ref_index]);
}

const GfxSprite *asset_sprite_number_gfx(unsigned number)
{
    return asset_sprite_ref_gfx(SPRITE_NUMBER_BASE + number);
}

int asset_sprite_id_index(SpriteId id)
{
    return data.loaded && (unsigned)id < SPRITE_ID_COUNT ? data.named_sprites.frame[id] : -1;
}

const GfxSprite *asset_sprite_id_gfx(SpriteId id)
{
    int frame = asset_sprite_id_index(id);
    return frame >= 0 ? asset_sprite_gfx(SPRITE_BANK_GAME, (unsigned)frame) : NULL;
}

const AssetFont *asset_font(void)
{
    return data.loaded ? &data.font : NULL;
}

const GfxFont *asset_font_gfx(void)
{
    return data.loaded ? &data.font.gfx : NULL;
}

int asset_font_line_height(void)
{
    return data.loaded ? data.font.line_height : 0;
}

const SineTables *asset_sine(void)
{
    return data.loaded ? &data.sine : NULL;
}

const PaletteSet *asset_palettes(void)
{
    return data.loaded ? &data.palettes : NULL;
}

const GfxGamePalette *asset_palette_at(uint16_t ds_offset)
{
    const NamedPalette *p = data.loaded ? palettes_find(&data.palettes, ds_offset) : NULL;
    return p ? &p->palette : NULL;
}

const FlashList *asset_flash_list(FlashListId id)
{
    if (!data.loaded || (unsigned)id >= FLASH_LIST_COUNT || id >= data.palettes.flash_list_count)
        return NULL;
    return &data.palettes.flash_lists[id];
}

const FlashList *asset_flash_list_at(uint16_t ds_offset)
{
    return data.loaded ? palettes_find_flash_list(&data.palettes, ds_offset) : NULL;
}

const TextSet *asset_texts(void)
{
    return data.loaded ? &data.texts : NULL;
}

const GameText *asset_text_at(uint16_t ds_offset)
{
    return data.loaded ? texts_find(&data.texts, ds_offset) : NULL;
}

const char *asset_text_raw(uint16_t ds_offset)
{
    const GameText *text = asset_text_at(ds_offset);
    return text ? text->raw : NULL;
}

const GameText *asset_level_title(int level)
{
    for (uint16_t i = 0; data.loaded && i < data.texts.level_title_count; i++)
        if (data.texts.level_titles[i].level == level)
            return &data.texts.texts[data.texts.level_titles[i].text_index];
    return NULL;
}

const char *asset_dos_message_at(uint16_t ds_offset)
{
    for (uint16_t i = 0; data.loaded && i < data.texts.message_count; i++)
        if (data.texts.messages[i].ds_offset == ds_offset)
            return data.texts.messages[i].text;
    return NULL;
}

const LevelSet *asset_levels(void)
{
    return data.loaded ? &data.levels : NULL;
}

const LevelScript *asset_level(unsigned level)
{
    return data.loaded && level < data.levels.level_count ? &data.levels.levels[level] : NULL;
}

const SampleSet *asset_samples(void)
{
    return data.loaded ? &data.samples : NULL;
}

const Sample *asset_sample(unsigned number)
{
    if (!data.loaded || number == 0 || number > data.samples.sample_count)
        return NULL;
    return &data.samples.samples[number - 1];
}

unsigned asset_sample_rate_hz(unsigned rate_index)
{
    return data.loaded ? samples_rate_hz(&data.samples, rate_index) : 0;
}

const HighScoreTable *asset_default_scores(void)
{
    return data.loaded ? &data.default_scores : NULL;
}

const HighScoreTable *asset_user_scores(void)
{
    return data.loaded ? &data.user_scores : NULL;
}

const GameConfig *asset_config(void)
{
    return data.loaded ? &data.config : NULL;
}

const HudGlyphs *asset_hud_glyphs(void)
{
    return data.loaded ? &data.hud_glyphs : NULL;
}

const HiscoreData *asset_hiscore_data(void)
{
    return data.loaded ? &data.hiscore : NULL;
}

const EnemyTables *asset_enemy_tables(void)
{
    return data.loaded ? &data.enemies : NULL;
}

const ControlTables *asset_control_tables(void)
{
    return data.loaded ? &data.controls : NULL;
}

const TitleTables *asset_title_tables(void)
{
    return data.loaded ? &data.title : NULL;
}
