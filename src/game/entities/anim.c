/*
 * anim.c - the per-frame animate/draw handlers of the entities (game86 tbl_anim_handlers
 * DS:202E, indexed by Entity.anim; called by entities_animate_all 1000:4374).
 *
 * Every handler draws into the page being drawn: the loop's target, the original's
 * [0x39d7] (which anim 04 / 09 set to [0x39d3] again, the same page during play). The
 * original tells its blitter how to draw through globals ([0x39d9] plain or scaled, [0x39da]
 * scale, [0x39dc] mirror); here each draw call says it.
 */
#include "entity_dispatch.h"

#include "game_data.h"

enum {
    /* anim 02 / 03: the frame at 218C:7AA2 (no sprite number refers to it), centred 10 px
     * above the entity */
    BEAM_HEAD_BANK_OFFSET = 0x7aa2,
    BEAM_HEAD_RISE = 10,
    /* anim 10: sprite 0xc5 (the frame at 218C:92A4) 10 px above, while player.powerup >= 0 */
    POWERUP_MARK_SPRITE = 0xc5,
    POWERUP_MARK_RISE = 10,
    /* anim 0e: DS:0911 lists sprites 0xaf..0xb2 */
    SATELLITE_FIRST_SPRITE = 0xaf,
    SATELLITE_COUNT = 4,
    /* anim 01: refs[0x1b1 + 2 * wave + (anim_toggle & 1)] (the table at 218C:0362), as
     * enemy_init 01 uses for sprite_ref */
    SPRITE_REF_LLAMA_BY_WAVE = 0x1b1,
};

/* The entity's own frame: sprite number `sprite`, or the sprites.bin frame in sprite_ref
 * when sprite < 0. game86 1000:44ab entity_sprite_setup */
static const GfxSprite *entity_sprite(const Entity *e)
{
    if (e->sprite >= 0)
        return asset_sprite_number_gfx((unsigned)e->sprite);
    return asset_sprite_gfx(SPRITE_BANK_GAME, (uint16_t)e->sprite_ref);
}

static void draw_at(const DrawTarget *target, const GfxSprite *sprite, int x, int y)
{
    if (sprite != NULL)
        draw_sprite(target, sprite, x, y);
}

/* An original slip: anim 04 and 09 load the draw page's VRAM offset ([0x39d3]) into AX, the blitter's x,
 * just before their plain draw, so it lands at x = 0 in frames drawn into page 0 and at
 * x = 0x2000 in the others, which the clipping of a recorded draw rejects (dirty recording is
 * always on during play). game86 1000:4664, 4681 */
static void draw_at_page_offset_x(const DrawTarget *target, const GfxSprite *sprite, int y)
{
    if (target->page == GFX_PAGE_0)
        draw_at(target, sprite, 0, y);
}

/* The original's scaled draw with g_draw_scale = e->scale: rows e->scale pixels apart,
 * slanted by the same step towards the sign of `mirror` (0: upright). game86 1000:e135 */
static void draw_entity_scaled(const DrawTarget *target, const Entity *e, int16_t mirror)
{
    const GfxSprite *sprite = entity_sprite(e);
    if (sprite == NULL)
        return;
    int shear = mirror == 0 ? 0 : mirror > 0 ? e->scale : -e->scale;
    draw_sprite_sheared(target, sprite, e->x, e->y, e->scale, shear);
}

/* The entity's frame at its position. game86 1000:4802 */
static void anim_plain(GameState *game, Entity *e, const DrawTarget *target)
{
    (void)game;
    draw_at(target, entity_sprite(e), e->x, e->y);
}

/* Nothing drawn. game86 1000:44d6, 480d, 3fde */
static void anim_none(GameState *game, Entity *e, const DrawTarget *target)
{
    (void)game, (void)e, (void)target;
}

/* Scaled, upright. game86 1000:4465 */
static void anim_scaled(GameState *game, Entity *e, const DrawTarget *target)
{
    (void)game;
    draw_entity_scaled(target, e, 0);
}

/* Scaled and slanted towards the sign of the timer. game86 1000:4499 */
static void anim_scaled_slanted(GameState *game, Entity *e, const DrawTarget *target)
{
    (void)game;
    draw_entity_scaled(target, e, e->timer);
}

/* Drawn plainly (at the wrong x, see draw_at_page_offset_x) and then scaled and slanted,
 * moving on to anim 05. The original also "draws" into the background and the displayed
 * page first, through anim_draw_current 1000:4675, which is an empty routine. On page-0
 * frames its scaled draw reads a stray record (docs/DEVIATIONS.md #16); this draws the
 * entity, as on page-1 frames. game86 1000:4646 */
static void anim_plain_then_slanted(GameState *game, Entity *e, const DrawTarget *target)
{
    e->anim++;
    draw_at_page_offset_x(target, entity_sprite(e), e->y);
    anim_scaled_slanted(game, e, target);
}

/* Drawn plainly (at the wrong x, see draw_at_page_offset_x), moving on to the next anim
 * (09 -> 0a -> 0b). game86 1000:4676 */
static void anim_plain_advance(GameState *game, Entity *e, const DrawTarget *target)
{
    (void)game;
    e->anim++;
    draw_at_page_offset_x(target, entity_sprite(e), e->y);
}

/* The power-up mark above the entity while player.powerup >= 0, then the entity.
 * game86 1000:468a */
static void anim_powerup_mark(GameState *game, Entity *e, const DrawTarget *target)
{
    if (game->player.powerup >= 0)
        draw_at(target, asset_sprite_number_gfx(POWERUP_MARK_SPRITE), e->x,
                e->y - POWERUP_MARK_RISE);
    anim_plain(game, e, target);
}

static void draw_beam_head(const DrawTarget *target, const Entity *e)
{
    int frame = asset_sprite_index_at(SPRITE_BANK_GAME, BEAM_HEAD_BANK_OFFSET);
    if (frame >= 0)
        draw_at(target, asset_sprite_gfx(SPRITE_BANK_GAME, (unsigned)frame), e->x,
                e->y - BEAM_HEAD_RISE);
}

/* The beam is on while the high byte of aux24 (+25) is non-zero. */
static bool beam_on(const Entity *e)
{
    return ((uint16_t)e->aux24 >> 8) != 0;
}

/* Beam head, a horizontal beam along the entity's row, the entity. game86 1000:4779 */
static void anim_beam_horizontal(GameState *game, Entity *e, const DrawTarget *target)
{
    draw_beam_head(target, e);
    if (beam_on(e))
        draw_beam_horizontal(target, e->y);
    anim_plain(game, e, target);
}

/* Beam head, a vertical beam down the entity's column, the entity. game86 1000:46b0 */
static void anim_beam_vertical(GameState *game, Entity *e, const DrawTarget *target)
{
    draw_beam_head(target, e);
    if (beam_on(e))
        draw_beam_vertical(target, e->x);
    anim_plain(game, e, target);
}

/* Satellite position i, stored from +16 as four (x, y) words: (+16, +18), (+1a, +1c),
 * (+1e, +20), (+22, +24). */
static Point16 satellite_at(const Entity *e, int i)
{
    switch (i) {
    case 0: return (Point16){ e->aux16, e->hp };
    case 1: return (Point16){ e->aux1a, e->aux1c };
    case 2: return (Point16){ e->kill_action, e->sub_mode };
    default: return (Point16){ (int16_t)(e->reload | e->countdown << 8), e->aux24 };
    }
}

/* Four satellite sprites, sprite k at position (timer / 4 + k) % 4, then the entity.
 * game86 1000:474a */
static void anim_satellites(GameState *game, Entity *e, const DrawTarget *target)
{
    int first = (e->timer & 0x0c) >> 2;
    for (int k = 0; k < SATELLITE_COUNT; k++) {
        Point16 at = satellite_at(e, (first + k) % SATELLITE_COUNT);
        draw_at(target, asset_sprite_number_gfx(SATELLITE_FIRST_SPRITE + (unsigned)k), at.x, at.y);
    }
    anim_plain(game, e, target);
}

/* The wave's llama frame, two per wave, picked by anim_toggle. game86 1000:4bab */
static void anim_wave_llama(GameState *game, Entity *e, const DrawTarget *target)
{
    unsigned ref = SPRITE_REF_LLAMA_BY_WAVE +
                   (uint16_t)(game->player.wave * 2 + (e->anim_toggle & 1));
    draw_at(target, asset_sprite_ref_gfx(ref), e->x, e->y);
}

const AnimFn entity_anim_handlers[ANIM_HANDLER_COUNT] = {
    [0x00] = anim_scaled,               /* 1000:4465 */
    [0x01] = anim_wave_llama,           /* 1000:4bab */
    [0x02] = anim_beam_horizontal,      /* 1000:4779 */
    [0x03] = anim_beam_vertical,        /* 1000:46b0 */
    [0x04] = anim_plain_then_slanted,   /* 1000:4646 */
    [0x05] = anim_scaled_slanted,       /* 1000:4499 */
    [0x06] = anim_scaled_slanted,
    [0x07] = anim_scaled_slanted,
    [0x08] = anim_none,                 /* 1000:44d6 */
    [0x09] = anim_plain_advance,        /* 1000:4676 */
    [0x0a] = anim_plain_advance,
    [0x0b] = anim_wave_llama,
    [0x0c] = anim_scaled,
    [0x0d] = anim_plain,                /* 1000:4802 */
    [0x0e] = anim_satellites,           /* 1000:474a */
    [0x0f] = anim_scaled,
    [0x10] = anim_powerup_mark,         /* 1000:468a */
    [0x11] = anim_none,                 /* 1000:480d */
    [ANIM_IDLE] = anim_none,            /* 1000:3fde */
};
