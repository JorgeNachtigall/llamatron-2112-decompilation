/*
 * continue.c - the continue prompt (see continue.h).
 */
#include "continue.h"

#include "game_data.h"
#include "input.h"

enum {
    CONTINUE_SECONDS = 9,
    TICKS_PER_SECOND = 0x32,
    CONTINUE_CHOSEN = -1,
    SHAPE_CONTINUE = 2,
    DIGIT_REF_ZERO = 4,             /* the digits' sprite refs, 218C:0008 + 2d */
    LABEL_X = 0x96, LABEL_Y = 0x3c,
    CONTINUES_X = 0xc8, CONTINUES_Y = 0x3c,
    SECONDS_X = 0xa0, SECONDS_Y = 0x5a,
};

static const GfxSprite *default_shape(unsigned index)
{
    return asset_sprite_gfx(SPRITE_BANK_TITLE, index);
}

const ContinueArt *continue_art_default(void)
{
    static const ContinueArt art = { asset_sprite_ref_gfx, default_shape };
    return &art;
}

static void draw_plain(const DrawTarget *target, const GfxSprite *sprite, int x, int y)
{
    if (sprite)
        draw_sprite(target, sprite, x, y);
}

void continue_begin(GameState *game)
{
    game->continue_seconds = CONTINUE_SECONDS;
    game->continue_ticks = TICKS_PER_SECOND;
}

void continue_tick_hook(GameState *game)
{
    if (--game->continue_ticks == 0) {
        game->continue_ticks = TICKS_PER_SECOND;
        game->continue_seconds--;
    } else if (input_state()->controls[0] & INPUT_FIRE) {
        game->continue_ticks = CONTINUE_CHOSEN;
        game->tick_hook = TICK_HOOK_NONE;
    }
}

void continue_draw_hook(GameState *game, const DrawTarget *frame, const ContinueArt *art)
{
    draw_plain(frame, art->shape(SHAPE_CONTINUE), LABEL_X, LABEL_Y);
    draw_plain(frame, art->sprite_ref((unsigned)(uint16_t)(DIGIT_REF_ZERO + game->continues)),
               CONTINUES_X, CONTINUES_Y);
    if (game->continue_seconds >= 0)
        draw_plain(frame, art->sprite_ref((unsigned)(DIGIT_REF_ZERO + game->continue_seconds)),
                   SECONDS_X, SECONDS_Y);
}
