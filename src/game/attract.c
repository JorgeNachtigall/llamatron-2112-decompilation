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
    LLAMA_ROW_CENTRE_X = 0xa0, LLAMA_ROW_SPACING = 4,
    LLAMA_HEIGHT_BASE = 0x12,
    LLAMA_SPEED_STEP = 0x1000,                      /* 16.16: llama n moves (n + 1) / 16 px */
    /* the frame and the two logo halves on the background page */
    BORDER_RIGHT = 0x13e, BORDER_BOTTOM = 0xc6, BORDER_COLOR = 0x0b,
    LOGO_LEFT_X = 0x41, LOGO_RIGHT_X = 0x101, LOGO_HALVES_Y = 0x50,
    /* the menu shown once the logo is complete */
    BOB_X = 0x9b, BOB_Y = 0x78,
    PLAYERS_X = 0x9c, PLAYERS_Y = 0x5f,
    START_DIGIT_X = 0x9b, START_DIGIT_Y = 0x4b,
    FOOTER_X = 0xa0,
};

static const SoundRequest SOUND_LETTER_LANDS = { SAMPLE_ID_LETTER_LANDS, 1, 2 };  /* 1000:3f8f */
static const SoundRequest SOUND_LETTERS_DONE = { SAMPLE_ID_WAVE_CLEARED, 0, 2 };  /* 1000:3fb1 */
static const SoundRequest SOUND_LOGO_DONE = { SAMPLE_ID_EXPLOSION, 1, 2 };     /* 1000:3fcd */

static AttractLlama llamas[ATTRACT_LLAMAS];

static void play(SoundRequest s)
{
    game_sound_play(s.sample, s.rate_index, s.priority);
}

const AttractArt *attract_art_default(void)
{
    static const AttractArt art = { asset_sprite_number_gfx, asset_title_shape };
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
    draw_rect_outline(GFX_PAGE_BACKGROUND, 0, 0, BORDER_RIGHT, BORDER_BOTTOM, BORDER_COLOR);
    DrawTarget background = draw_target_page(GFX_PAGE_BACKGROUND);
    draw_set_dirty_recording(false);
    draw_plain(&background, art->shape(TITLE_SHAPE_LOGO_LEFT), LOGO_LEFT_X, LOGO_HALVES_Y);
    draw_plain(&background, art->shape(TITLE_SHAPE_LOGO_RIGHT), LOGO_RIGHT_X, LOGO_HALVES_Y);
}

/* The bouncing llamas under the menu start in a row across the middle, 4 pixels apart, at
 * heights along a sine wave, each a little faster than the one before; every 8 share a sprite
 * set. game86 1000:44d7 */
void attract_llamas_init(void)
{
    const int8_t *sine = (const int8_t *)asset_sine()->sine8;
    const TitleTables *tables = asset_title_tables();
    for (int i = 0; i < ATTRACT_LLAMAS; i++) {
        int n = ATTRACT_LLAMAS - 1 - i;     /* the original counts down */
        AttractLlama *l = &llamas[i];
        l->x = (int32_t)(((n - 15) * LLAMA_ROW_SPACING + LLAMA_ROW_CENTRE_X) * 0x10000);
        l->height = (int32_t)(((sine[(n * 8) & 0xff] >> 3) + LLAMA_HEIGHT_BASE) * 0x10000);
        l->vx = (int32_t)((n + 1) * LLAMA_SPEED_STEP);
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
        /* Quirk kept: in the original the landing sound's call (1000:3f97) clobbers the
         * "still dropping" flag (set at 3f85, tested at 3fa1). The sound driver leaves it
         * set; with sound off the stub (1000:e785) clears it. So with sound off, the tick on
         * which the last letter lands already counts as "all landed" and the logo starts
         * one tick early. */
        bool dropping = false;
        for (int i = 0; i < TITLE_LETTERS; i++) {
            if (game->title_objects[i] <= 0)
                continue;
            dropping = true;
            game->title_objects[i] = (int16_t)(game->title_objects[i] - LETTER_STEP);
            if (game->title_objects[i] <= 0) {
                play(SOUND_LETTER_LANDS);
                dropping = game->sound_on;
            }
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
        draw_plain(frame, art->shape(TITLE_SHAPE_BOB_A), BOB_X, BOB_Y + bob(ticks, 0));
        draw_plain(frame, art->shape(TITLE_SHAPE_BOB_B), BOB_X, BOB_Y + bob(ticks, 8));
        TitleShapeId players = TITLE_SHAPE_ONE_PLAYER;
        if (game->options & OPT_TWO_PLAYERS)
            players = (game->options & OPT_DOUBLE_CONTROLLER) ? TITLE_SHAPE_TWO_PLAYERS_TEAM
                                                              : TITLE_SHAPE_TWO_PLAYERS_DROID;
        draw_plain(frame, art->shape(players), PLAYERS_X, PLAYERS_Y);
        draw_plain(frame, art->shape((game->options & OPT_SHARED_START) ? TITLE_SHAPE_DIGIT_2
                                                                        : TITLE_SHAPE_DIGIT_1),
                   START_DIGIT_X, START_DIGIT_Y);
        attract_llamas_draw(frame, art);
    } else if (timer == 0) {                /* the logo is complete: game86 1000:41c3 */
        draw_set_dirty_recording(false);
        static const GfxPageId pages[] = { GFX_PAGE_0, GFX_PAGE_1, GFX_PAGE_BACKGROUND };
        for (int p = 0; p < 3; p++) {
            DrawTarget page = draw_target_page(pages[p]);
            draw_logo_row(&page, art, 1, false);
        }
        static const struct { TitleShapeId shape; int y; } footer[] = {
            { TITLE_SHAPE_FOOTER_BEAST, 0x8c }, { TITLE_SHAPE_FOOTER_CREDIT, 0xa0 },
            { TITLE_SHAPE_FOOTER_SIGN, 0xb4 },
        };
        for (int f = 0; f < 3; f++) {
            for (int p = 0; p < 3; p++) {
                DrawTarget page = draw_target_page(pages[p]);
                draw_plain(&page, art->shape(footer[f].shape), FOOTER_X, footer[f].y);
            }
        }
        game->title_timer = LOGO_DONE;
        draw_set_dirty_recording(recording);
    } else if (timer != TITLE_TIMER_START) {    /* zooming in: game86 1000:40f6 */
        draw_logo_row(frame, art, timer, true);
    }
}

/* ---- residue in the entity table ---------------------------------------------------------- */

/* The original's title llamas are 32 records of 18 bytes at the start of the entity table
 * (written by attract_llamas_init 1000:44d7, the tick 4571 and read by the draw 4603, while
 * title_draw 3ee0..3f6b runs the title): x, height, vx, vy as 16.16 longs (low word first),
 * then sprite_base. 32 x 18 bytes are exactly the 9 x 64-byte records of slots 0..8, so the
 * records' 288 words fall on those slots' 288 words in order. */
enum { LLAMA_RECORD_WORDS = 9, ENTITY_RECORD_WORDS = 32, RESIDUE_SLOTS = 9 };
_Static_assert(ATTRACT_LLAMAS * LLAMA_RECORD_WORDS == RESIDUE_SLOTS * ENTITY_RECORD_WORDS,
               "the llama records cover slots 0..8 exactly");

static uint16_t low_word(int32_t v) { return (uint16_t)((uint32_t)v & 0xffff); }
static uint16_t high_word(int32_t v) { return (uint16_t)((uint32_t)v >> 16); }

/* Word `n` of a llama record. */
static uint16_t llama_record_word(const AttractLlama *l, unsigned n)
{
    switch (n) {
    case 0: return low_word(l->x);
    case 1: return high_word(l->x);
    case 2: return low_word(l->height);
    case 3: return high_word(l->height);
    case 4: return low_word(l->vx);
    case 5: return high_word(l->vx);
    case 6: return low_word(l->vy);
    case 7: return high_word(l->vy);
    default: return (uint16_t)l->sprite_base;
    }
}

void attract_end(GameState *game)
{
    for (unsigned slot = 0; slot < RESIDUE_SLOTS; slot++) {
        EntityWords record = { .entity = game->entities[slot] };
        for (unsigned w = 0; w < ENTITY_RECORD_WORDS; w++) {
            unsigned n = slot * ENTITY_RECORD_WORDS + w;   /* word n of the llama records */
            record.words[w] = llama_record_word(&llamas[n / LLAMA_RECORD_WORDS],
                                                n % LLAMA_RECORD_WORDS);
        }
        game->entities[slot] = record.entity;
    }
}
