/*
 * think_chargers.c - enemies that alternate between bouncing around and standing still to
 * charge (think_25, think_29). While charging they release a spawn every so often (what it
 * is depends on the callback their init stored in charger.callback), then dash towards the
 * target ship. The fields are ChargerView's (entity_views.h).
 */
#include "entities.h"

enum {
    CHARGING_SPRITE_OFFSET = 2,
    DASH_SPRITE_OFFSET = 1,
    DEADLY_PICKUP_SPRITE = 0x120,
    PAIR_LEFT_SPRITE = 0x116, PAIR_RIGHT_SPRITE = 0x117,
    THINK29_SPRITE = 0x11c,         /* + 4-frame animation */
};

/* Counts down; when it reaches zero it restarts at the period and the call returns true. */
static bool counter_elapsed(ByteCounter *c)
{
    if (--c->left != 0)
        return false;
    c->left = c->period;
    return true;
}

/* ---- spawn callbacks (stored in charger.callback by enemy_init_1b / 1e / 20) ---------------------- */

/* A pickup (think_1c) of kind 9, the deadly one. Its slot is not taken from free_slots.
 * game86 1000:3119 */
static void spawn_deadly_pickup(GameState *game, Entity *spawn, const Entity *parent)
{
    (void)parent;
    spawn->pickup.pickup_kind = 9;
    spawn->pickup.life = 0x64;
    spawn->sprite = DEADLY_PICKUP_SPRITE;
    spawn->scale = 0;
    spawn->solid = 0;
    spawn->vx = 0;
    spawn->vy = 0;
    spawn->anim = 0x0d;
    spawn->kind = -1;
    entity_set_type(spawn, 0x1c, 0x1c);
    spawn->pickup.sprite_base = spawn->sprite;
    game->enemies_alive++;
}

/* Two children flying left and right; the second goes into the next free slot after the
 * first. game86 1000:32d7 */
static void spawn_pair(GameState *game, Entity *spawn, const Entity *parent)
{
    entity_init_child(game, spawn, parent, PAIR_LEFT_SPRITE, -2, 0);
    for (Entity *other = spawn; other < game->entities + ENTITY_COUNT; other++) {
        if (entity_is_free(other)) {
            entity_init_child(game, other, parent, PAIR_RIGHT_SPRITE, 1, 0);
            return;
        }
    }
}

/* Takes the first free slot (without counting it in free_slots), puts it idle at the
 * parent's position and lets the parent's callback make it into something.
 * game86 1000:329e */
static void charger_spawn(GameState *game, const Entity *parent)
{
    if (game->wave_intro_timer != 0)
        return;
    Entity *spawn = NULL;
    for (Entity *s = game->entities; s < game->entities + ENTITY_COUNT; s++) {
        if (entity_is_free(s)) {
            spawn = s;
            break;
        }
    }
    if (!spawn)
        return;
    spawn->anim = ANIM_IDLE;
    spawn->scale = 0;
    spawn->x = parent->x;
    spawn->y = parent->y;
    switch (parent->charger.callback) {
    case SPAWN_AS_DEADLY_PICKUP: spawn_deadly_pickup(game, spawn, parent); break;
    case SPAWN_AS_PAIR: spawn_pair(game, spawn, parent); break;
    case SPAWN_AS_TYPE_1A: enemy_setup_1a(game, spawn); break;
    default: break;     /* no other callback is ever stored */
    }
    spawn->kind = -1;
}

/* ---- the handlers ---------------------------------------------------------------------- */

/* game86 1000:3236 */
static void charger_update(GameState *game, Entity *e)
{
    ChargerView *c = &e->charger;
    if (c->charging == 0) {
        if (!counter_elapsed(&c->move)) {
            entity_move_bounce(game, e);
            return;
        }
        c->charging ^= 1;
        return;
    }

    if (counter_elapsed(&c->spawn))
        charger_spawn(game, e);
    if (!counter_elapsed(&c->charge))
        return;
    Point16 here = { e->x, e->y };
    Point16 aim = line_walk(here, game->target, c->dash_reach);
    e->vx = (int16_t)(uint16_t)(aim.x - here.x);
    e->vy = (int16_t)(uint16_t)(aim.y - here.y);
    c->charging ^= 1;
    c->dash_frames = -1;
}

/* game86 1000:320a */
void think_25(GameState *game, Entity *e)
{
    if (game->wave_intro_timer != 0)
        return;
    if (entity_check_touch(game, e))
        return;
    int16_t sprite = e->charger.sprite_base;
    if (e->charger.charging != 0) {
        sprite += CHARGING_SPRITE_OFFSET;
    } else if (e->charger.dash_frames >= 0) {
        e->charger.dash_frames--;
        sprite += DASH_SPRITE_OFFSET;
    }
    e->sprite = sprite;
    charger_update(game, e);
}

/* The same with a plain 4-frame animation. game86 1000:30f6 */
void think_29(GameState *game, Entity *e)
{
    if (game->wave_intro_timer != 0)
        return;
    if (entity_check_touch(game, e))
        return;
    e->sprite = (int16_t)(THINK29_SPRITE + ((game->tick_count >> 2) & 3));
    charger_update(game, e);
}
