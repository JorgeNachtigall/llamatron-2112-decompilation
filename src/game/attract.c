/*
 * attract.c - the title screen and the title tune (see attract.h).
 *
 * Drawing modes: game86's sprite_draw_core either draws a sprite plainly or, "scaled", row by
 * row with a row step and a shear (draw_sprite_sheared). The letters drop in and the logo
 * zooms in that way: row step = their countdown, drawn once slanted each way.
 */
#include "attract.h"

#include "game_data.h"
#include "input.h"
#include "sound.h"

enum {
    LETTER_START = 0x80, LETTER_STAGGER = 0x10,     /* countdowns 0x80, 0x90, ... */
    LETTER_STEP = 2,
    LETTER_X0 = 0x32, LETTER_DX = 0x18, LETTER_Y = 0x14,
    TITLE_TIMER_START = 0x50,
    LOGO_DONE = -1,
    LOGO_SPRITE_FIRST = 8, LOGO_SPRITE_END = 13,    /* sprite numbers of the logo row */
    LOGO_X0 = 0x82, LOGO_DX = 0x10, LOGO_Y = 0x2c,
    TUNE_PERIOD = 0x24,
    TUNE_PRIORITY = 2,
    LLAMA_GROUND_Y = 0xc0,
    LLAMA_X_MIN = 0x10, LLAMA_X_MAX = 0x130,
    LLAMA_GRAVITY = 0xa00,                          /* 16.16 per tick */
    LLAMA_BOUNCE_SPEED = -0x20000,
    LLAMA_FACING_RIGHT = 6,
};

/* g3 title shapes */
enum {
    SHAPE_PLAYERS_1 = 7, SHAPE_PLAYERS_2_STICKS = 8, SHAPE_PLAYERS_2_ONE_STICK = 9,
    SHAPE_SEPARATE_START = 10, SHAPE_SHARED_START = 11,
    SHAPE_BOB_A = 4, SHAPE_BOB_B = 5,
    SHAPE_FOOTER_A = 12, SHAPE_FOOTER_B = 13, SHAPE_FOOTER_C = 6,
    SHAPE_LOGO_RIGHT = 14, SHAPE_LOGO_LEFT = 15,
};

static const SoundRequest SOUND_LETTER_LANDS = { 0x02, 1, 2 };  /* 1000:3f8f */
static const SoundRequest SOUND_LETTERS_DONE = { 0x14, 0, 2 };  /* 1000:3fb1 */
static const SoundRequest SOUND_LOGO_DONE = { 0x11, 1, 2 };     /* 1000:3fcd */

static AttractLlama llamas[ATTRACT_LLAMAS];

static void play(SoundRequest s)
{
    game_sound_play(s.sample, s.rate_index, s.priority);
}

static const GfxSprite *default_shape(unsigned index)
{
    return asset_sprite_gfx(SPRITE_BANK_TITLE, index);
}

const AttractArt *attract_art_default(void)
{
    static const AttractArt art = { asset_sprite_number_gfx, default_shape };
    return &art;
}

AttractLlama *attract_llamas(void)
{
    return llamas;
}

static void draw_plain(const DrawTarget *target, const GfxSprite *sprite, int x, int y)
{
    if (sprite)
        draw_sprite(target, sprite, x, y);
}

/* The original's scaled mode with row step `step`, drawn slanted by +step and by -step
 * (both upright when `slant` is false). */
static void draw_zoomed_pair(const DrawTarget *target, const GfxSprite *sprite, int x, int y,
                             int step, bool slant)
{
    if (!sprite)
        return;
    int shear = slant ? step : 0;
    draw_sprite_sheared(target, sprite, x, y, step, shear);
    draw_sprite_sheared(target, sprite, x, y, step, -shear);
}

/* ---- The title tune ------------------------------------------------------------------- */

void title_tune_start(GameState *game)
{
    game->tune.fire_pressed = false;
    game->tune.countdown = -1;
    game->tick_hook = TICK_HOOK_TITLE_TUNE;
}

void title_tune_begin(GameState *game)
{
    title_tune_start(game);
    game->tune.position = 0;
    game->tune.restart = 0;
    game->tune.period = TUNE_PERIOD;
    game->tune.countdown = TUNE_PERIOD;
}

static uint8_t tune_byte(const TitleTables *tables, uint16_t position)
{
    return position < tables->tune_length ? tables->tune[position] : 0;
}

void title_tune_tick(GameState *game)
{
    TitleTune *tune = &game->tune;
    if (input_state()->controls[0] & INPUT_FIRE)
        tune->fire_pressed = true;
    if (tune->countdown < 0)
        return;
    if (--tune->countdown != 0)
        return;
    tune->countdown = tune->period;

    const TitleTables *tables = asset_title_tables();
    uint16_t position = tune->position;
    uint8_t rate = tune_byte(tables, position++);
    if (rate == 0) {                /* end mark: start over (the note there is not checked) */
        position = tune->restart;
        rate = tune_byte(tables, position++);
    }
    uint8_t sample = tune_byte(tables, position++);
    tune->position = position;
    game_sound_play(sample, rate, TUNE_PRIORITY);
}

/* ---- The title screen ----------------------------------------------------------------- */

void title_draw(GameState *game, const AttractArt *art)
{
    for (int i = 0; i < TITLE_LETTERS; i++)
        game->title_objects[i] = (int16_t)(LETTER_START + LETTER_STAGGER * i);
    game->title_timer = TITLE_TIMER_START;

    gfx_clear(GFX_PAGE_BACKGROUND, 0);
    draw_rect_outline(GFX_PAGE_BACKGROUND, 0, 0, 0x13e, 0xc6, 0x0b);
    DrawTarget background = draw_target_page(GFX_PAGE_BACKGROUND);
    draw_set_dirty_recording(false);
    draw_plain(&background, art->shape(SHAPE_LOGO_LEFT), 0x41, 0x50);
    draw_plain(&background, art->shape(SHAPE_LOGO_RIGHT), 0x101, 0x50);
}

void attract_llamas_init(void)
{
    const int8_t *sine = (const int8_t *)asset_sine()->sine8;
    const TitleTables *tables = asset_title_tables();
    for (int i = 0; i < ATTRACT_LLAMAS; i++) {
        int n = ATTRACT_LLAMAS - 1 - i;     /* the original counts down */
        AttractLlama *l = &llamas[i];
        l->x = (int32_t)(((n - 15) * 4 + 0xa0) * 0x10000);
        l->height = (int32_t)(((sine[(n * 8) & 0xff] >> 3) + 0x12) * 0x10000);
        l->vx = (int32_t)((n + 1) << 12);
        l->vy = 0;
        l->sprite_base = (int16_t)tables->llama_sprites[(n >> 3) & 3];
    }
}

static int16_t integer_part(int32_t v) { return (int16_t)(uint16_t)((uint32_t)v >> 16); }
static int32_t add32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static int32_t neg32(int32_t v) { return (int32_t)(0u - (uint32_t)v); }

/* Gravity, the bounce off the ground and off the playfield's sides. game86 1000:4571 */
static void attract_llamas_tick(void)
{
    for (AttractLlama *l = llamas; l < llamas + ATTRACT_LLAMAS; l++) {
        int32_t height = add32(l->height, neg32(l->vy));
        if (height < 0) {
            l->vy = LLAMA_BOUNCE_SPEED;
            height = 0;
        }
        l->height = height;
        l->vy = add32(l->vy, LLAMA_GRAVITY);

        int32_t x = add32(l->x, l->vx);
        int32_t wall = 0;
        if (x > (int32_t)(LLAMA_X_MAX * 0x10000))
            wall = LLAMA_X_MAX;
        else if (integer_part(x) < LLAMA_X_MIN)
            wall = LLAMA_X_MIN;
        if (wall) {                         /* mirror the overshoot back inside */
            x = add32(wall * 0x20000, neg32(x));
            l->vx = neg32(l->vx);
        }
        l->x = x;
    }
}

/* game86 1000:4603 */
static void attract_llamas_draw(const DrawTarget *target, const AttractArt *art)
{
    for (const AttractLlama *l = llamas; l < llamas + ATTRACT_LLAMAS; l++) {
        int16_t x = integer_part(l->x);
        int number = ((x >> 2) & 1) + l->sprite_base + (l->vx >= 0 ? LLAMA_FACING_RIGHT : 0);
        draw_plain(target, art->sprite((unsigned)(uint16_t)number), x,
                   LLAMA_GROUND_Y - integer_part(l->height));
    }
}

void title_tick_hook(GameState *game)
{
    if (game->title_timer == TITLE_TIMER_START) {
        bool dropping = false;
        for (int i = 0; i < TITLE_LETTERS; i++) {
            if (game->title_objects[i] <= 0)
                continue;
            dropping = true;
            game->title_objects[i] = (int16_t)(game->title_objects[i] - LETTER_STEP);
            if (game->title_objects[i] <= 0)
                play(SOUND_LETTER_LANDS);
        }
        if (dropping)
            return;
        game->title_timer--;
        play(SOUND_LETTERS_DONE);
        return;
    }
    if (game->title_timer > 0) {
        if (--game->title_timer == 0)
            play(SOUND_LOGO_DONE);
        return;
    }
    attract_llamas_tick();
}

/* Up/down toggles the shared start; right/left step through 1 player, 2 players on two
 * sticks, 2 players on one stick (the droid). Other option bits are cleared. One step per
 * press. game86 1000:4081..40de */
static void title_menu(GameState *game)
{
    uint8_t direction = input_state()->controls[0] & INPUT_DIRECTIONS;
    if (direction == 0) {
        game->title_key_latch = 0;
    } else if (game->title_key_latch == 0) {
        game->title_key_latch = direction;
        if (direction & (INPUT_UP | INPUT_DOWN)) {
            game->options ^= OPT_SHARED_START;
        } else {
            uint8_t mode = game->options & (OPT_TWO_PLAYERS | OPT_DOUBLE_CONTROLLER);
            if (direction & INPUT_RIGHT) {
                mode = (mode + 1) & 3;
                if (mode == OPT_DOUBLE_CONTROLLER)      /* not a valid mode */
                    mode++;
            } else {
                mode = (mode - 1) & 3;
                if (mode == OPT_DOUBLE_CONTROLLER)
                    mode--;
            }
            game->options = (uint8_t)((game->options & OPT_SHARED_START) | mode);
        }
    }
    game->options_default = game->options;
}

/* The logo row: sprites 8..12 side by side. game86 1000:40fc */
static void draw_logo_row(const DrawTarget *target, const AttractArt *art, int step, bool slant)
{
    for (int n = LOGO_SPRITE_FIRST; n < LOGO_SPRITE_END; n++)
        draw_zoomed_pair(target, art->sprite((unsigned)n),
                         LOGO_X0 + LOGO_DX * (n - LOGO_SPRITE_FIRST), LOGO_Y, step, slant);
}

/* Bobs up and down by up to 8 pixels with the tick count. */
static int bob(uint16_t ticks, int phase)
{
    int v = (((ticks >> 1) + phase) & 0x0f) - 8;
    return v < 0 ? -v : v;
}

void title_draw_hook(GameState *game, const DrawTarget *frame, const AttractArt *art)
{
    bool recording = draw_dirty_recording();
    for (int i = 0; i < TITLE_LETTERS; i++) {
        int x = LETTER_X0 + LETTER_DX * i;
        int16_t countdown = game->title_objects[i];
        const GfxSprite *letter = art->sprite(asset_title_tables()->letter_sprites[i]);
        if (countdown == 0) {
            /* landed: into the picture on screen and the background too, unrecorded
             * (game86 1000:3fdf) */
            game->title_objects[i] = -1;
            DrawTarget shown = draw_target_page(gfx_display_page_id());
            DrawTarget background = draw_target_page(GFX_PAGE_BACKGROUND);
            draw_set_dirty_recording(false);
            if (letter) {
                draw_sprite_sheared(frame, letter, x, LETTER_Y, 1, 0);
                draw_sprite_sheared(&shown, letter, x, LETTER_Y, 1, 0);
                draw_sprite_sheared(&background, letter, x, LETTER_Y, 1, 0);
            }
            draw_set_dirty_recording(recording);
        } else if (countdown > 0 && countdown < LETTER_START) {
            draw_zoomed_pair(frame, letter, x, LETTER_Y, countdown, true);
        }
    }

    title_menu(game);

    int16_t timer = game->title_timer;
    if (timer < 0) {                        /* game86 1000:4130 */
        uint16_t ticks = game->tick_count;
        draw_plain(frame, art->shape(SHAPE_BOB_A), 0x9b, 0x78 + bob(ticks, 0));
        draw_plain(frame, art->shape(SHAPE_BOB_B), 0x9b, 0x78 + bob(ticks, 8));
        unsigned players = SHAPE_PLAYERS_1;
        if (game->options & OPT_TWO_PLAYERS)
            players = (game->options & OPT_DOUBLE_CONTROLLER) ? SHAPE_PLAYERS_2_STICKS
                                                              : SHAPE_PLAYERS_2_ONE_STICK;
        draw_plain(frame, art->shape(players), 0x9c, 0x5f);
        draw_plain(frame, art->shape((game->options & OPT_SHARED_START) ? SHAPE_SHARED_START
                                                                        : SHAPE_SEPARATE_START),
                   0x9b, 0x4b);
        attract_llamas_draw(frame, art);
    } else if (timer == 0) {                /* the logo is complete: game86 1000:41c3 */
        draw_set_dirty_recording(false);
        static const GfxPageId pages[] = { GFX_PAGE_0, GFX_PAGE_1, GFX_PAGE_BACKGROUND };
        for (int p = 0; p < 3; p++) {
            DrawTarget page = draw_target_page(pages[p]);
            draw_logo_row(&page, art, 1, false);
        }
        static const struct { unsigned shape; int y; } footer[] = {
            { SHAPE_FOOTER_A, 0x8c }, { SHAPE_FOOTER_B, 0xa0 }, { SHAPE_FOOTER_C, 0xb4 },
        };
        for (int f = 0; f < 3; f++) {
            for (int p = 0; p < 3; p++) {
                DrawTarget page = draw_target_page(pages[p]);
                draw_plain(&page, art->shape(footer[f].shape), 0xa0, footer[f].y);
            }
        }
        game->title_timer = LOGO_DONE;
        draw_set_dirty_recording(recording);
    } else if (timer != TITLE_TIMER_START) {    /* zooming in: game86 1000:40f6 */
        draw_logo_row(frame, art, timer, true);
    }
}
