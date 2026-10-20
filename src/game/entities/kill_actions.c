/*
 * kill_actions.c - what an ent_hit_03 entity does when its hit points run out
 * (tbl_hit_kill_actions, CS:1047, index Entity.kill_action).
 *
 * Most actions leave something behind and then run the common kill; 03 and 08 replace it.
 */
#include "entities.h"

#include <stddef.h>

#include "game_data.h"
#include "rng.h"

enum { KILL_ACTION_COUNT = 12 };

/* Slots the debris search may look at: it gives up after this many occupied ones
 * (1000:3c0f). Kill action 04 jumps past that load and inherits DX = 0x60 from
 * shots_update (1000:0c55), the only way the hit handlers are reached. */
enum { DEBRIS_SEARCH_LIMIT = 0x3b, DEBRIS_SEARCH_LIMIT_INHERITED = 0x60 };
/* Sprites of kill action 03's rocks and 04's crawlers. */
enum { SPRITE_ROCK = 0xb7, SPRITE_CRAWLER = 0xbf };
/* A seeker piece's turn counter (split_into_type24), as one word: period 4, left 0x20. */
enum { SEEKER_TURN_PIECE = 0x2004 };

static int16_t shifted(int16_t v, int16_t shift)
{
    unsigned n = (unsigned)shift & 31;      /* SHL r16,CL as the 386+ oracle masks it */
    return n >= 16 ? 0 : (int16_t)(uint16_t)((uint16_t)v << n);
}

/* game86 1000:3c09..3c81: `count` fragments from the debris templates first..first+count-1,
 * taken from the last one down, flying at the template velocity << script_param[2].
 * Returns the slot after the last one the search looked at (see kill_after_debris). */
static int spawn_debris_group(GameState *game, const Entity *e, int first, int count,
                              int search_limit)
{
    const EnemyTables *tables = asset_enemy_tables();
    int16_t shift = game->player.script_param[2];
    int left = search_limit;
    int next = count - 1;
    /* The original keeps searching past the table if `left` allows it; the port stops at
     * the end of the table (no record there is ever processed). */
    for (int i = 0; i < ENTITY_COUNT; i++) {
        Entity *d = &game->entities[i];
        if (!entity_is_free(d)) {
            if (--left < 0)
                return i + 1;
            continue;
        }
        d->x = e->x;
        d->y = e->y;
        left--;
        game->enemies_alive++;
        game->free_slots--;
        d->anim = 0x0d;
        if (tables) {
            const DebrisTemplate *t = &tables->debris[first + next];
            d->sprite = (int16_t)t->sprite;
            d->vx = shifted(t->vx, shift);
            d->vy = shifted(t->vy, shift);
        }
        d->scale = 0;
        d->solid = 1;
        d->kind = -1;
        d->death_sample = SAMPLE_ID_WARP;
        d->death_rate = 4;
        d->hit_handler = 0x10;
        entity_set_type(d, 0x0f, 0);
        if (--next < 0)
            return i + 1;
    }
    return ENTITY_COUNT;
}

/* game86 1000:3c81: the common kill after the debris. The search walked the slots with the
 * register that held the shot, so ent_hit_00 picks the pickup drop from the vx of the slot
 * after the last one searched instead of the shot direction. Past the table (only possible
 * with kill action 04's longer search) the port keeps the shot direction. */
static void kill_after_debris(GameState *game, Entity *e, const EntityHit *hit, int walker)
{
    Shot shot = *hit->shot;
    EntityHit after = *hit;
    if (walker < ENTITY_COUNT)
        shot.direction = game->entities[walker].vx;
    after.shot = &shot;
    entity_kill(game, e, &after);
}

/* game86 1000:3bf6 */
static void kill_action_01(GameState *game, Entity *e, const EntityHit *hit)
{
    if (game->level_flags < 0) {
        game->debris_alt = 0;
        kill_after_debris(game, e, hit,
                          spawn_debris_group(game, e, DEBRIS_GROUP_CORNERS, 4, DEBRIS_SEARCH_LIMIT));
        return;
    }
    entity_kill(game, e, hit);
}

/* game86 1000:3be1 */
static void kill_action_02(GameState *game, Entity *e, const EntityHit *hit)
{
    if (game->level_flags < 0) {
        game->debris_alt = 0;
        kill_after_debris(game, e, hit,
                          spawn_debris_group(game, e, DEBRIS_GROUP_SIDEWAYS, 4, DEBRIS_SEARCH_LIMIT));
        return;
    }
    entity_kill(game, e, hit);
}

/* game86 1000:212d: breaks into up to ten small rocks (type 0x1a, hit handler 07); the
 * record itself just vanishes, without a kill or score. */
static void kill_action_03(GameState *game, Entity *e, const EntityHit *hit)
{
    (void)hit;
    int rocks = 10;
    for (int i = 0; i < ENTITY_COUNT && rocks > 0; i++) {
        Entity *r = &game->entities[i];
        if (!entity_is_free(r))
            continue;
        r->x = e->x;
        r->y = e->y;
        r->anim = 0x0d;
        int16_t vx;
        do {
            vx = (int16_t)((rng_next_byte(&game->rng) & 3) - 1);
        } while (vx == 0);
        r->vx = vx;
        r->sprite = SPRITE_ROCK;
        r->vy = (int16_t)-(rng_next_byte(&game->rng) & 3);
        r->death_sample = SAMPLE_ID_RELEASE;
        r->death_rate = 0;
        r->scale = 1;
        r->solid = 1;
        r->kind = -1;
        r->kill_action = 4;
        r->hp = 3;
        r->hit_handler = 7;
        entity_set_type(r, 0x1a, 0x1a);
        game->enemies_alive++;
        rocks--;
    }
    entity_vanish(e);
    game->enemies_alive--;
}

/* game86 1000:3bc5: unconditional, and flags the alternative debris. */
static void kill_action_04(GameState *game, Entity *e, const EntityHit *hit)
{
    game->debris_alt = 1;
    kill_after_debris(game, e, hit,
                      spawn_debris_group(game, e, DEBRIS_GROUP_SIDEWAYS, 4,
                                         DEBRIS_SEARCH_LIMIT_INHERITED));
}

/* game86 1000:1416: with level_flags < 0, leaves a crawler (type 0x1b) moving one pixel per
 * tick along a random axis. */
static void kill_action_05(GameState *game, Entity *e, const EntityHit *hit)
{
    Entity *c = NULL;
    if (game->level_flags < 0)
        for (int i = 0; i < ENTITY_COUNT && !c; i++)
            if (entity_is_free(&game->entities[i]))
                c = &game->entities[i];
    if (c) {
        c->x = e->x;
        c->y = e->y;
        c->anim = 0x0d;
        uint8_t roll;
        do {
            roll = rng_next_byte(&game->rng);
        } while ((roll & 1) == 0);
        int16_t step = (roll & 2) ? -1 : 1;
        c->vx = (roll & 4) ? step : 0;
        c->vy = (roll & 4) ? 0 : step;
        c->sprite = SPRITE_CRAWLER;
        c->scale = 0;
        c->solid = 0;
        c->kind = 1;
        entity_set_type(c, 0x1b, 0x1b);
    }
    entity_kill(game, e, hit);
}

/* game86 1000:13a6: with level_flags < 0, a fragment (type 0x0f) bounces back against the
 * shot, its sprite chosen by the shot's direction. */
static void kill_action_06(GameState *game, Entity *e, const EntityHit *hit)
{
    Entity *f = NULL;
    if (game->level_flags < 0)
        for (int i = 0; i < ENTITY_COUNT && !f; i++)
            if (entity_is_free(&game->entities[i]))
                f = &game->entities[i];
    if (f) {
        f->x = e->x;
        f->y = e->y;
        f->vx = (int16_t)((int16_t)(uint16_t)-hit->shot->vx >> 1);
        f->vy = (int16_t)((int16_t)(uint16_t)-hit->shot->vy >> 1);
        f->sprite = (int16_t)(hit->shot->direction + 0xa3);
        f->anim = 0x0d;
        f->scale = 0;
        f->solid = 0;
        entity_set_type(f, 0x0f, 0x0f);
        f->kind = -1;
        game->enemies_alive++;
    }
    entity_kill(game, e, hit);
}

/* game86 1000:3b92 */
static void kill_action_07(GameState *game, Entity *e, const EntityHit *hit)
{
    entity_burst_four(game, e);
    entity_kill(game, e, hit);
}

/* game86 1000:12c6: comes back as a fresh enemy_init_0c record where it died, not counted
 * again. */
static void kill_action_08(GameState *game, Entity *e, const EntityHit *hit)
{
    (void)hit;
    enemy_setup_0c(game, e);
    game->enemies_alive--;
}

/* game86 1000:3bd6: eight fragments, unconditional. */
static void kill_action_09(GameState *game, Entity *e, const EntityHit *hit)
{
    game->debris_alt = 0;
    kill_after_debris(game, e, hit,
                      spawn_debris_group(game, e, DEBRIS_GROUP_STAR, 8, DEBRIS_SEARCH_LIMIT));
}

/* game86 1000:3d71 / 3dac: meant to fill slots with type 0x24 records at phases first,
 * first + step, ... below 8, but entity_alloc_next (1000:3d4e) is entered with BP = -1 from
 * the successful alloc and reports a full table at once, so only the first one appears. */
static void split_into_type24(GameState *game, Entity *e, const EntityHit *hit, int16_t phase)
{
    Entity *s = entity_alloc(game);
    if (s) {
        s->x = e->x;
        s->y = e->y;
        s->kind = -1;
        enemy_setup_type24(game, s, phase, SEEKER_TURN_PIECE);
    }
    entity_kill(game, e, hit);
}

/* game86 1000:3d71 */
static void kill_action_0a(GameState *game, Entity *e, const EntityHit *hit)
{
    split_into_type24(game, e, hit, 1);
}

/* game86 1000:3dac */
static void kill_action_0b(GameState *game, Entity *e, const EntityHit *hit)
{
    split_into_type24(game, e, hit, 0);
}

/* CS:1047. Index 00 is the plain kill. */
static const HitFn kill_actions[KILL_ACTION_COUNT] = {
    entity_kill, kill_action_01, kill_action_02, kill_action_03, kill_action_04, kill_action_05,
    kill_action_06, kill_action_07, kill_action_08, kill_action_09, kill_action_0a, kill_action_0b,
};

/* game86 1000:106a */
void hit_kill_action(GameState *game, Entity *e, const EntityHit *hit)
{
    /* Indices outside the table jump to whatever code follows it in the original; no level
     * sets one. */
    if ((uint16_t)e->kill_action < KILL_ACTION_COUNT)
        kill_actions[e->kill_action](game, e, hit);
}
