/*
 * bullets.c - the players' shots: cleared at the start of a wave, moved and checked against
 * the entities every play tick, drawn every frame (see bullets.h).
 *
 * A shot keeps its velocity, and the sprite of its direction, from the control tables of
 * the input nibble it was fired with (player_fire); a bouncing shot is reflected by looking
 * that nibble up again and taking its mirror image across the edge it reached.
 */
#include "bullets.h"

#include "entity_dispatch.h"
#include "game_data.h"

enum {
    SHOT_X_MIN = 8,             /* a shot dies (or bounces) at x <= 8 or x >= 0x138 */
    SHOT_X_END = 0x138,
    SHOT_Y_MIN = 8,             /* ... and at y <= 8 or y >= 0xc0 */
    SHOT_Y_END = 0xc0,
    SHOT_REACH_X = 0x0c,        /* hit when |entity x - shot x| <= 12 ... */
    SHOT_REACH_Y_DEFAULT = 8,   /* ... and |entity y - shot y| <= the sprite's rows - 1, or 8
                                   for an entity drawn through sprite_ref */
    THINK_NO_SPEEDUP_RELOAD = 0x13,
    SHOT_SPRITES_DEFAULT = 0x9e,    /* sprite number of direction d: d + 0x9e, or d while
                                       level_flags < 0 */
};

typedef enum { EDGE_LEFT_RIGHT, EDGE_TOP_BOTTOM } ShotEdge;

static void shot_free(Shot *s)
{
    s->x = -1;
}

static bool shot_is_free(const Shot *s)
{
    return s->x < 0;
}

/* |a - b| as the original computes it with SUB / NEG: -32768 stays negative, and so counts
 * as near. */
static int16_t distance16(int16_t a, int16_t b)
{
    int16_t d = (int16_t)(uint16_t)(a - b);
    return d < 0 ? (int16_t)(uint16_t)-d : d;
}

/* game86 1000:07b7 */
void shots_clear(GameState *game)
{
    for (int i = 0; i < SHOT_COUNT; i++)
        shot_free(&game->shots[i]);
}

/* The input nibble the shot was fired with: the first whose direction (never 0) and
 * velocity match; -1 if none. The original compares the low bytes of the velocity and
 * the whole direction word. game86 1000:0c8f..0cac */
static int shot_nibble(const ControlTables *t, const Shot *s)
{
    for (int n = 0; n < INPUT_NIBBLES; n++) {
        if (t->shot_direction[n] != 0 && t->shot_direction[n] == s->direction &&
            (uint8_t)t->shot_vx[n] == (uint8_t)s->vx && (uint8_t)t->shot_vy[n] == (uint8_t)s->vy)
            return n;
    }
    return -1;
}

/* At an edge: without the bounce power-up the shot dies; with it, it takes the velocity
 * and direction of its nibble's mirror image. It does not move this tick. A shot that
 * matches no nibble dies (docs/DEVIATIONS.md: the original searches on past the tables).
 * game86 1000:0c77..0ccc */
static void shot_reach_edge(GameState *game, Shot *s, ShotEdge edge)
{
    const ControlTables *t = asset_control_tables();
    int n = (game->shots_bounce && t) ? shot_nibble(t, s) : -1;
    if (n < 0) {
        shot_free(s);
        return;
    }
    int m = (edge == EDGE_LEFT_RIGHT ? t->shot_bounce_x : t->shot_bounce_y)[n] &
            (INPUT_NIBBLES - 1);
    s->vx = t->shot_vx[m];
    s->vy = t->shot_vy[m];
    s->direction = t->shot_direction[m];
}

/* One step; false when the shot reached an edge instead (and died or bounced). */
static bool shot_move(GameState *game, Shot *s, Point16 *at)
{
    int16_t x = (int16_t)(s->x + s->vx);
    at->x = x;
    if (x <= SHOT_X_MIN || x >= SHOT_X_END) {
        shot_reach_edge(game, s, EDGE_LEFT_RIGHT);
        return false;
    }
    s->x = x;
    int16_t y = (int16_t)(s->y + s->vy);
    if (y <= SHOT_Y_MIN || y >= SHOT_Y_END) {
        shot_reach_edge(game, s, EDGE_TOP_BOTTOM);
        return false;
    }
    s->y = y;
    at->y = y;
    return true;
}

/* How far in y a shot reaches the entity: the rows of its sprite minus one (the original
 * reads the frame header word that holds them). */
static int16_t entity_reach_y(const Entity *e)
{
    if (e->sprite < 0)
        return SHOT_REACH_Y_DEFAULT;
    const SpriteRefTable *refs = asset_sprite_refs();
    unsigned index = SPRITE_NUMBER_BASE + (unsigned)e->sprite;
    if (!refs || index >= refs->count || (refs->refs[index] & SPRITE_REF_LINK))
        return SHOT_REACH_Y_DEFAULT;
    const SpriteFrame *frame = asset_sprite_frame(SPRITE_BANK_GAME, refs->refs[index]);
    return frame ? (int16_t)(frame->height - 1) : SHOT_REACH_Y_DEFAULT;
}

static bool shot_touches(const Entity *e, Point16 at)
{
    return distance16(e->x, at.x) <= SHOT_REACH_X && distance16(e->y, at.y) <= entity_reach_y(e);
}

static bool entity_is_target(const Entity *e)
{
    return !entity_is_free(e) && e->solid != 0;
}

static void entity_hit(GameState *game, Entity *e, const EntityHit *hit)
{
    if ((unsigned)e->hit_handler < HIT_HANDLER_COUNT && entity_hit_handlers[e->hit_handler])
        entity_hit_handlers[e->hit_handler](game, e, hit);
}

/* The smart bomb: the shot hits every solid entity from the first one on, once, and the
 * flag is spent. game86 1000:0cfe..0d27 */
static void shot_smart_bomb(GameState *game, int first, const EntityHit *hit)
{
    for (int i = first; i < ENTITY_COUNT; i++) {
        Entity *e = &game->entities[i];
        if (entity_is_target(e))
            entity_hit(game, e, hit);
    }
    game->smart_bomb = false;
}

/* The first solid entity the shot touches is hit. With level_flags < 0 the shot dies on the
 * hit; any hit but on a think 0x13 entity restarts the speed-up countdown.
 * game86 1000:0ce0..0da7 */
static void shot_collide(GameState *game, Shot *s, Point16 at)
{
    EntityHit hit = { .shot = s, .smart_bomb = false, .at = at };
    for (int i = 0; i < ENTITY_COUNT; i++) {
        Entity *e = &game->entities[i];
        if (!entity_is_target(e))
            continue;
        if (game->smart_bomb) {
            hit.smart_bomb = true;
            shot_smart_bomb(game, i, &hit);
            return;
        }
        if (!shot_touches(e, at))
            continue;
        if (game->level_flags < 0)
            shot_free(s);
        if (e->think != THINK_NO_SPEEDUP_RELOAD)
            game->speedup_timer = game->speedup_period;
        entity_hit(game, e, &hit);
        return;
    }
}

/* game86 1000:0c28 */
void shots_update(GameState *game)
{
    for (int i = 0; i < SHOT_COUNT; i++) {
        Shot *s = &game->shots[i];
        if (shot_is_free(s))
            continue;
        /* bouncing shots live for bounce_life ticks */
        if (game->shots_bounce && --s->bounce_life < 0) {
            shot_free(s);
            continue;
        }
        Point16 at;
        if (!shot_move(game, s, &at))
            continue;
        /* even and odd slots take turns at hitting (1000:0cd1) */
        if ((i & 1) != (game->tick_count & 1))
            continue;
        shot_collide(game, s, at);
    }
}

/* game86 1000:1499 */
void shots_draw(const GameState *game, const DrawTarget *target)
{
    int base = game->level_flags < 0 ? 0 : SHOT_SPRITES_DEFAULT;
    for (int i = 0; i < SHOT_COUNT; i++) {
        const Shot *s = &game->shots[i];
        if (shot_is_free(s))
            continue;
        const GfxSprite *sprite = asset_sprite_number_gfx((unsigned)(s->direction + base));
        if (sprite)
            draw_sprite(target, sprite, s->x, s->y);
    }
}
