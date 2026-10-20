/*
 * kill_actions.c - what a HIT_LOSE_HP entity does when its hit points run out
 * (tbl_hit_kill_actions, CS:1047, index Entity.kill_action = KILL_*).
 *
 * Most actions leave something behind and then run the common kill; the rocks
 * (KILL_BREAK_INTO_ROCKS) and the humming herder's respawn (KILL_RESPAWN_HUMMER) replace it.
 * "Without power shots" means level_flags < 0, the normal state.
 */
#include "entities.h"

#include <stddef.h>

#include "game_data.h"
#include "rng.h"

enum { KILL_ACTION_COUNT = 12 };

/* How many occupied slots the debris search may pass before it gives up (1000:3c0f). The
 * alternative debris (KILL_DEBRIS_ALT) enters the search after that limit is set, so it gets
 * the 0x60 left over from shots_update's slot loop (1000:0c55), the only way the hit handlers
 * are reached. */
enum { DEBRIS_SEARCH_LIMIT = 0x3b, DEBRIS_SEARCH_LIMIT_INHERITED = 0x60 };
/* Sprites of the rocks and crawlers; a bounced-back fragment's sprite is this + the shot
 * direction. */
enum { SPRITE_ROCK = 0xb7, SPRITE_CRAWLER = 0xbf, SPRITE_FRAGMENT_BY_DIRECTION = 0xa3 };
/* A seeker piece's turn counter (split_into_seeker), as one word: period 4, left 0x20. */
enum { SEEKER_TURN_PIECE = 0x2004 };
/* The level script parameter that scales the debris speed (a left shift). */
enum { DEBRIS_SHIFT_PARAM = 2 };
enum { ROCKS_PER_BREAK = 10, ROCK_HP = 3 };

/* Think handlers named here (think_table.c). */
enum {
    THINK_FRAGMENT = 0x0f,              /* bounces, dies at the first wall */
    THINK_ROCK = 0x1a,
    THINK_CRAWLER = 0x1b,               /* deadly bouncer */
};

/* v << shift for a level-script shift count: counts are taken mod 32 (as on the 386 and
 * later processors the oracle emulates), and 16 or more clears the word. */
static int16_t shifted(int16_t v, int16_t shift)
{
    unsigned n = (unsigned)shift & 31;
    return n >= 16 ? 0 : (int16_t)(uint16_t)((uint16_t)v << n);
}

static Entity *first_free_slot(GameState *game)
{
    for (int i = 0; i < ENTITY_COUNT; i++)
        if (entity_is_free(&game->entities[i]))
            return &game->entities[i];
    return NULL;
}

/* game86 1000:3c09..3c81: `count` fragments from the debris templates first..first+count-1,
 * taken from the last one down, flying at the template velocity << the level's debris shift.
 * Returns the slot after the last one the search looked at (see kill_after_debris). */
static int spawn_debris_group(GameState *game, const Entity *e, int first, int count,
                              int search_limit)
{
    const EnemyTables *tables = asset_enemy_tables();
    int16_t shift = game->player.script_param[DEBRIS_SHIFT_PARAM];
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
        d->anim = ANIM_PLAIN;
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
        d->hit_handler = HIT_KILL_FRAGMENT;
        entity_set_type(d, THINK_FRAGMENT, 0);
        if (--next < 0)
            return i + 1;
    }
    return ENTITY_COUNT;
}

/* game86 1000:3c81: the common kill after the debris. The original walks the debris search
 * with the same pointer that held the shot, so the kill afterwards takes its "shot
 * direction" from the vx of the slot after the last one searched. Past the table (only
 * possible with KILL_DEBRIS_ALT's longer search) the port keeps the shot direction. */
static void kill_after_debris(GameState *game, Entity *e, const EntityHit *hit, int walker)
{
    Shot shot = *hit->shot;
    EntityHit after = *hit;
    if (walker < ENTITY_COUNT)
        shot.direction = game->entities[walker].vx;
    after.shot = &shot;
    entity_kill(game, e, &after);
}

static void debris_then_kill(GameState *game, Entity *e, const EntityHit *hit, int first,
                             int count, int search_limit)
{
    kill_after_debris(game, e, hit, spawn_debris_group(game, e, first, count, search_limit));
}

/* game86 1000:3bf6 */
static void kill_action_01(GameState *game, Entity *e, const EntityHit *hit)
{
    if (game->level_flags < 0) {
        game->debris_alt = 0;
        debris_then_kill(game, e, hit, DEBRIS_GROUP_CORNERS, 4, DEBRIS_SEARCH_LIMIT);
        return;
    }
    entity_kill(game, e, hit);
}

/* game86 1000:3be1 */
static void kill_action_02(GameState *game, Entity *e, const EntityHit *hit)
{
    if (game->level_flags < 0) {
        game->debris_alt = 0;
        debris_then_kill(game, e, hit, DEBRIS_GROUP_SIDEWAYS, 4, DEBRIS_SEARCH_LIMIT);
        return;
    }
    entity_kill(game, e, hit);
}

/* game86 1000:212d: breaks into up to ten small rocks (think 1a); the record itself just
 * vanishes, without a kill or score. The rocks carry KILL_DEBRIS_ALT, but HIT_DEFLECT kills
 * them without running a kill action. */
static void kill_action_03(GameState *game, Entity *e, const EntityHit *hit)
{
    (void)hit;
    int rocks = ROCKS_PER_BREAK;
    for (int i = 0; i < ENTITY_COUNT && rocks > 0; i++) {
        Entity *r = &game->entities[i];
        if (!entity_is_free(r))
            continue;
        r->x = e->x;
        r->y = e->y;
        r->anim = ANIM_PLAIN;
        int16_t vx;                         /* -1, 1 or 2 */
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
        r->kill_action = KILL_DEBRIS_ALT;
        r->hp = ROCK_HP;
        r->hit_handler = HIT_DEFLECT;
        entity_set_type(r, THINK_ROCK, THINK_ROCK);
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
    debris_then_kill(game, e, hit, DEBRIS_GROUP_SIDEWAYS, 4, DEBRIS_SEARCH_LIMIT_INHERITED);
}

/* game86 1000:1416: without power shots, leaves a crawler (think 1b) moving one pixel per
 * tick along a random axis. */
static void kill_action_05(GameState *game, Entity *e, const EntityHit *hit)
{
    Entity *c = game->level_flags < 0 ? first_free_slot(game) : NULL;
    if (c) {
        c->x = e->x;
        c->y = e->y;
        c->anim = ANIM_PLAIN;
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
        entity_set_type(c, THINK_CRAWLER, THINK_CRAWLER);
    }
    entity_kill(game, e, hit);
}

/* game86 1000:13a6: without power shots, a fragment (think 0f) bounces back against the
 * shot at half its speed, its sprite chosen by the shot's direction. */
static void kill_action_06(GameState *game, Entity *e, const EntityHit *hit)
{
    Entity *f = game->level_flags < 0 ? first_free_slot(game) : NULL;
    if (f) {
        f->x = e->x;
        f->y = e->y;
        f->vx = (int16_t)((int16_t)(uint16_t)-hit->shot->vx >> 1);
        f->vy = (int16_t)((int16_t)(uint16_t)-hit->shot->vy >> 1);
        f->sprite = (int16_t)(hit->shot->direction + SPRITE_FRAGMENT_BY_DIRECTION);
        f->anim = ANIM_PLAIN;
        f->scale = 0;
        f->solid = 0;
        entity_set_type(f, THINK_FRAGMENT, THINK_FRAGMENT);
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

/* game86 1000:12c6: comes back as a fresh humming herder (enemy_init_0c) where it died, not
 * counted again. */
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
    debris_then_kill(game, e, hit, DEBRIS_GROUP_STAR, 8, DEBRIS_SEARCH_LIMIT);
}

/* game86 1000:3d71 / 3dac: meant to fill slots with seekers at phases first, first + step,
 * ... below 8, but entity_alloc_next (1000:3d4e) continues from a slot count the first
 * allocation already used up, so it reports a full table at once and only the first seeker
 * appears. */
static void split_into_seeker(GameState *game, Entity *e, const EntityHit *hit, int16_t phase)
{
    Entity *s = entity_alloc(game);
    if (s) {
        s->x = e->x;
        s->y = e->y;
        s->kind = -1;
        enemy_setup_seeker(game, s, phase, SEEKER_TURN_PIECE);
    }
    entity_kill(game, e, hit);
}

/* game86 1000:3d71 */
static void kill_action_0a(GameState *game, Entity *e, const EntityHit *hit)
{
    split_into_seeker(game, e, hit, 1);
}

/* game86 1000:3dac */
static void kill_action_0b(GameState *game, Entity *e, const EntityHit *hit)
{
    split_into_seeker(game, e, hit, 0);
}

/* CS:1047 */
static const HitFn kill_actions[KILL_ACTION_COUNT] = {
    [KILL_PLAIN] = entity_kill,
    [KILL_DEBRIS_CORNERS] = kill_action_01,
    [KILL_DEBRIS_SIDEWAYS] = kill_action_02,
    [KILL_BREAK_INTO_ROCKS] = kill_action_03,
    [KILL_DEBRIS_ALT] = kill_action_04,
    [KILL_LEAVE_CRAWLER] = kill_action_05,
    [KILL_LEAVE_FRAGMENT] = kill_action_06,
    [KILL_BURST_FOUR] = kill_action_07,
    [KILL_RESPAWN_HUMMER] = kill_action_08,
    [KILL_DEBRIS_STAR] = kill_action_09,
    [KILL_SPLIT_SEEKER_1] = kill_action_0a,
    [KILL_SPLIT_SEEKER_0] = kill_action_0b,
};

/* game86 1000:106a */
void hit_kill_action(GameState *game, Entity *e, const EntityHit *hit)
{
    /* Indices outside the table jump to whatever code follows it in the original; no level
     * sets one. */
    if ((uint16_t)e->kill_action < KILL_ACTION_COUNT)
        kill_actions[e->kill_action](game, e, hit);
}
