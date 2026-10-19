/*
 * draw_play.c - the play frame (game86 play_draw_hook 1000:1a3b).
 *
 * The original picks plain or scaled draws through the blitter's globals ([0x39d9] flags,
 * [0x39da] scale, [0x39dc] mirror); here each draw call says which.
 */
#include "draw_play.h"

#include "bullets.h"
#include "entity_dispatch.h"
#include "game_data.h"
#include "hud.h"

static const Point16 PAUSE_PICTURE_POS = { 0xa0, 0x46 };

enum {
    SHIELD_RISE = 0x0f,          /* the shield is centred 15 px above the ship */
    SHIP_FRAMES = 4,             /* ship animation: (x or y) / 2 % 4 */
};

static void draw_plain(const DrawTarget *target, const GfxSprite *sprite, int x, int y)
{
    if (sprite != NULL)
        draw_sprite(target, sprite, x, y);
}

static const GfxSprite *title_shape(unsigned index)
{
    return asset_sprite_gfx(SPRITE_BANK_TITLE, index);
}

/* The ship's frame: sprite number first_sprite + |facing| + (coordinate / 2) % 4, where the
 * coordinate is x while facing >= 0 and y otherwise. game86 1000:1a93..1ab1, 1b5d..1b7b */
static const GfxSprite *ship_sprite(Point16 at, int16_t facing, int16_t first_sprite)
{
    int16_t along = facing >= 0 ? at.x : at.y;
    int16_t step = (int16_t)(facing >= 0 ? facing : -facing);
    int16_t number = (int16_t)(((along >> 1) & (SHIP_FRAMES - 1)) + step + first_sprite);
    return asset_sprite_number_gfx((uint16_t)number);
}

/* The rescue marker above the ship while its row is >= 0. */
static void draw_rescue_marker(const DrawTarget *target, Point16 ship, int16_t marker_y)
{
    if (marker_y >= 0)
        draw_plain(target, asset_sprite_id_gfx(SPRITE_ID_RESCUE_MARKER), ship.x, marker_y);
}

/* A ship (or the companion) with its shield, scaled while it respawns: three draws with
 * row step |respawn_timer|, slanted one way, the other way and upright; otherwise plain,
 * with the rescue marker. game86 1000:1a6e..1b0c (P1), 1b38..1be2 (P2) */
static void draw_ship(const GameState *game, const DrawTarget *target, int player,
                      int16_t first_sprite)
{
    Point16 at = game->ship[player];
    if (game->shield_timer != 0)
        draw_plain(target, asset_sprite_id_gfx(SPRITE_ID_SHIELD), at.x, at.y - SHIELD_RISE);
    const GfxSprite *sprite = ship_sprite(at, game->ship_control[player].facing, first_sprite);
    int16_t timer = game->respawn_timer[player];
    if (timer == 0) {
        draw_plain(target, sprite, at.x, at.y);
        draw_rescue_marker(target, at, game->rescue_marker_y[player]);
        return;
    }
    if (sprite == NULL)
        return;
    int step = (int16_t)(timer < 0 ? -timer : timer);
    draw_sprite_sheared(target, sprite, at.x, at.y, step, step);
    draw_sprite_sheared(target, sprite, at.x, at.y, step, -step);
    draw_sprite_sheared(target, sprite, at.x, at.y, step, 0);
}

/* The companion llama of a one-stick two-player game, in P2's place: sprite ref
 * (tick / 2) % 4, with P2's rescue marker. game86 1000:1b1e..1b36 */
static void draw_companion(const GameState *game, const DrawTarget *target)
{
    Point16 at = game->ship[1];
    draw_plain(target, asset_sprite_ref_gfx((game->tick_count >> 1) & 3), at.x, at.y);
    draw_rescue_marker(target, at, game->rescue_marker_y[1]);
}

/* game86 1000:1a3b */
void play_draw_hook(GameState *game, const DrawTarget *target)
{
    game->frame_drawn = true;
    if (game->wave_start_delay != 0)
        hud_draw_control_labels(target, game->options, title_shape);
    shots_draw(game, target);
    entities_animate_all(game, target);
    if (game->pause_message != 0)
        draw_plain(target, asset_sprite_id_gfx(SPRITE_ID_PAUSE_FIRST + game->pause_message - 1),
                   PAUSE_PICTURE_POS.x, PAUSE_PICTURE_POS.y);

    draw_ship(game, target, 0, game->player_label_offset);
    if (!(game->options & OPT_TWO_PLAYERS))
        return;
    if (game->options & OPT_DOUBLE_CONTROLLER)
        draw_ship(game, target, 1, game->player2_label_offset);
    else
        draw_companion(game, target);
}
