/*
 * draw_play.c - what play draws on top of the background each frame (game86 play_draw_hook
 * 1000:1a3b): the shots, the entities, the pause picture and the ships; and the pictures of
 * a wave's start, the warp and game over.
 *
 * The original tells its blitter how to draw (plain or scaled, the scale, the mirror) through
 * globals set before the call; here each draw call says which.
 */
#include "draw_play.h"

#include "bullets.h"
#include "entity_dispatch.h"
#include "game_data.h"
#include "hud.h"
#include "text.h"

static const Point16 PAUSE_PICTURE_POS = { 0xa0, 0x46 };
static const Point16 WAVE_HEADING_POS = { 0x9f, 0x03 };
static const Point16 GAME_OVER_POS = { 0xa0, 0x5a };

enum {
    PLAYFIELD_FRAME_COLOR = 2,
    SHIELD_RISE = 0x0f,          /* the shield is centred 15 px above the ship */
    SHIP_FRAMES = 4,             /* ship animation: (x or y) / 2 % 4 */
    /* the companion llama's walk: sprite refs 0..3 (218C:0000..0006), one per 2 ticks */
    SPRITE_REF_COMPANION_FIRST = 0,
    COMPANION_FRAMES = 4,
};

static void draw_plain(const DrawTarget *target, const GfxSprite *sprite, int x, int y)
{
    if (sprite != NULL)
        draw_sprite(target, sprite, x, y);
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
    unsigned frame = (game->tick_count >> 1) % COMPANION_FRAMES;
    draw_plain(target, asset_sprite_ref_gfx(SPRITE_REF_COMPANION_FIRST + frame), at.x, at.y);
    draw_rescue_marker(target, at, game->rescue_marker_y[1]);
}

/* One play frame, back to front: the control labels while the wave is about to start, the
 * shots, the entities, the pause picture, P1's ship, then P2's ship (a controller each) or
 * the companion llama (one controller). game86 1000:1a3b */
void play_draw_hook(GameState *game, const DrawTarget *target)
{
    game->frame_drawn = true;
    if (game->wave_start_delay != 0)
        hud_draw_control_labels(target, game->options, asset_title_shape);
    shots_draw(game, target);
    entities_animate_all(game, target);
    if (game->pause_message != 0) {      /* 1, 2, ...: the pause pictures in order */
        SpriteId picture = (SpriteId)(SPRITE_ID_PAUSE_FIRST + game->pause_message - 1);
        draw_plain(target, asset_sprite_id_gfx(picture), PAUSE_PICTURE_POS.x,
                   PAUSE_PICTURE_POS.y);
    }

    draw_ship(game, target, 0, game->player_label_offset);
    if (!(game->options & OPT_TWO_PLAYERS))
        return;
    if (game->options & OPT_DOUBLE_CONTROLLER)
        draw_ship(game, target, 1, game->player2_label_offset);
    else
        draw_companion(game, target);
}

/* ---- Wave start, warp, game over ------------------------------------------------------ */

/* The dotted frame around the playfield. */
static const struct { int16_t x0, y0, x1, y1; } PLAYFIELD_FRAME = { 0x00, 0x08, 0x13e, 0xc6 };

static void print_text(const DrawTarget *target, GameState *game, const GameText *text)
{
    if (text != NULL)
        text_print_home(target, &game->text, text->raw);
}

/* A wave's background page: the dotted playfield frame, the wave heading, the herd-wave and
 * demo banners when they apply, and the level's title. game86 1000:055c..05cf */
void wave_draw_background(GameState *game)
{
    gfx_clear(GFX_PAGE_BACKGROUND, 0);
    draw_rect_outline(GFX_PAGE_BACKGROUND, PLAYFIELD_FRAME.x0, PLAYFIELD_FRAME.y0,
                      PLAYFIELD_FRAME.x1, PLAYFIELD_FRAME.y1, PLAYFIELD_FRAME_COLOR);
    DrawTarget background = draw_target_page(GFX_PAGE_BACKGROUND);
    draw_sprite_unrecorded(&background, asset_title_shape(TITLE_SHAPE_WAVE_HEADING),
                           WAVE_HEADING_POS.x, WAVE_HEADING_POS.y);
    if (game->player.wave_type != 0)
        print_text(&background, game, asset_text(TEXT_ID_HERD_WAVE_BANNER));
    if (game->demo_mode != 0)
        print_text(&background, game, asset_text(TEXT_ID_DEMO_BANNER));
    print_text(&background, game, asset_level_title(game->player.level));
}

/* The warp banner, printed straight onto the shown screen. game86 1000:0640..0649 */
void wave_draw_warp_banner(GameState *game)
{
    DrawTarget screen = draw_target_page(gfx_display_page_id());
    print_text(&screen, game, asset_text(TEXT_ID_WARP_BANNER));
}

/* "Game over", drawn straight onto the shown screen. game86 1000:08ee..0910 */
void game_over_draw(void)
{
    DrawTarget screen = draw_target_page(gfx_display_page_id());
    draw_sprite_unrecorded(&screen, asset_title_shape(TITLE_SHAPE_GAME_OVER), GAME_OVER_POS.x,
                           GAME_OVER_POS.y);
}
