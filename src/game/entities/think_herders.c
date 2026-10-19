/*
 * think_herders.c - herders (think_0e and the variants 15, 16 built on it): they walk to an
 * unhatched egg (think 5), turn it into something according to their sub-mode, then return
 * to the companion and pick the next egg.
 *
 * Record use: timer = slot index of the egg being fetched, HERDER_SEEK = pick one,
 * HERDER_RETURN = walk back to the companion; aux1a = step period/countdown byte pair;
 * aux1c = sprite base (8 directions x 2 frames); sub_mode = what happens to the egg.
 * An egg is claimed by setting its hp to 1.
 */
#include "entities.h"
#include "sound.h"

enum {
    HERDER_SEEK = -1,
    HERDER_RETURN = -2,
    THINK_EGG = 0x05,
    ARRIVED_RANGE = 8,
    HERDER_STEP = 3,                    /* max pixels per axis per step */
    DROPPED_EGG_SPRITE = 0x33,
    DROPPED_EGG_TICKS = 0x14,
    THINK_TIMER = 0x09,
    PENDING_DEADLY_PICKUP = 9 + 1,      /* sub-mode 3: the next kill drops pickup kind 9 */
    PROXIMITY_RANGE = 0x1e,             /* think_15 */
    PROXIMITY_SAMPLE_PRIORITY = 0x0c,
    SPAWN_CHANCE_16 = 4,                /* out of 256 per period */
};

/* Egg handling by sub-mode (CS:3AFF). */
enum { EGG_HATCH_TIMER, EGG_BECOMES_HERDER, EGG_BURSTS, EGG_HATCH_ARM_PICKUP };

static const SoundRequest SOUND_EGG_DONE = { 0x00, 1, 4 };      /* 1000:3b80 */
static const SoundRequest SOUND_NEW_HERDER = { 0x1a, 0, 3 };    /* 1000:3b17 */
static const SoundRequest SOUND_PROXIMITY = { 0x1e, 2, 0x0c };  /* 1000:2484 */
static const SoundRequest SOUND_SPAWN_16 = { 0x07, 1, 4 };      /* 1000:23f5 */

static void play(SoundRequest s)
{
    game_sound_play(s.sample, s.rate_index, s.priority);
}

static uint8_t high_byte(int16_t w) { return (uint8_t)((uint16_t)w >> 8); }
static uint8_t low_byte(int16_t w) { return (uint8_t)w; }
static void set_high_byte(int16_t *w, uint8_t v)
{
    *w = (int16_t)(uint16_t)((uint16_t)v << 8 | low_byte(*w));
}

static int16_t magnitude16(int16_t v)
{
    return v < 0 ? (int16_t)(uint16_t)-v : v;
}

/* Claims the first unclaimed egg; the search gives up after g_unhatched eggs. The original
 * scans on past the table when there are fewer eggs than unhatched says; the port stops at
 * the table's end and returns as if all eggs were taken. Also think_08's search
 * (1000:498e). game86 1000:3a17 */
void herder_pick_egg(GameState *game, Entity *e)
{
    int16_t eggs_left = game->unhatched;
    if (eggs_left != 0) {
        for (int slot = 0; slot < ENTITY_COUNT; slot++) {
            Entity *egg = &game->entities[slot];
            if (egg->think != THINK_EGG)
                continue;
            if (egg->hp == 0) {
                egg->hp = 1;
                e->timer = (int16_t)slot;
                return;
            }
            if (--eggs_left == 0)
                break;
        }
    }
    e->timer = HERDER_RETURN;
}

/* The direction it walks in picks one of 8 sprites, two frames each by position. */
static int16_t herder_sprite(const Entity *e)
{
    int16_t frame;
    if (magnitude16(e->vy) > magnitude16(e->vx))
        frame = (int16_t)((e->vy >= 0 ? 4 : 2) + ((e->y >> 2) & 1));
    else
        frame = (int16_t)((e->vx >= 0 ? 6 : 0) + ((e->x >> 2) & 1));
    return (int16_t)(frame + e->aux1c);
}

/* Done with an egg (or back at the companion): show `marker` as a dropped egg for a while
 * and look for the next one. game86 1000:3b70 */
static void herder_drop(Entity *e, Entity *marker)
{
    e->timer = HERDER_SEEK;
    marker->sprite = DROPPED_EGG_SPRITE;
    marker->timer = DROPPED_EGG_TICKS;
    play(SOUND_EGG_DONE);
}

/* game86 1000:3b63 */
static void egg_hatch_timer(GameState *game, Entity *e, Entity *egg)
{
    entity_set_type(egg, THINK_TIMER, 0);
    game->unhatched--;
    egg->kind = -1;
    herder_drop(e, egg);
}

/* game86 1000:3aeb..3b62 */
static void herder_handle_egg(GameState *game, Entity *e, Entity *egg)
{
    switch (e->sub_mode) {
    case EGG_HATCH_TIMER:
        egg_hatch_timer(game, e, egg);
        break;
    case EGG_BECOMES_HERDER:            /* 3b0f; the herder keeps its target index */
        game->unhatched--;
        enemy_setup_0c(game, egg);
        play(SOUND_NEW_HERDER);
        break;
    case EGG_BURSTS:                    /* 3b21 */
        entity_burst_four(game, e);
        egg_hatch_timer(game, e, egg);
        break;
    case EGG_HATCH_ARM_PICKUP:          /* 3b07 */
        game->player.pickup_pending = PENDING_DEADLY_PICKUP;
        egg_hatch_timer(game, e, egg);
        break;
    default:                            /* the original jumps through garbage; never set */
        break;
    }
}

/* game86 1000:39e9 */
void think_0e(GameState *game, Entity *e)
{
    if (game->wave_intro_timer != 0)
        return;
    int8_t countdown = (int8_t)(high_byte(e->aux1a) - 1);
    set_high_byte(&e->aux1a, (uint8_t)countdown);
    if (countdown >= 0)
        return;
    set_high_byte(&e->aux1a, low_byte(e->aux1a));

    if (e->timer == HERDER_RETURN) {
        game->companion.x = game->target.x;
        game->companion.y = game->target.y;
    } else if (e->timer < 0) {
        herder_pick_egg(game, e);
        return;
    }

    e->sprite = herder_sprite(e);
    Entity *goal;
    if (e->timer == HERDER_RETURN) {
        goal = &game->companion;
    } else {
        goal = e->timer < ENTITY_COUNT ? &game->entities[e->timer] : NULL;
        if (!goal || goal->think != THINK_EGG) {
            herder_pick_egg(game, e);   /* the egg was shot or hatched meanwhile */
            return;
        }
    }

    if (entity_check_touch(game, e))
        return;

    int16_t dx = (int16_t)(uint16_t)(goal->x - e->x);
    int16_t dy = (int16_t)(uint16_t)(goal->y - e->y);
    if (magnitude16(dx) < ARRIVED_RANGE && magnitude16(dy) < ARRIVED_RANGE) {
        if (e->timer == HERDER_RETURN)
            herder_drop(e, goal);
        else
            herder_handle_egg(game, e, goal);
        return;
    }
    vector_shrink_to(&dx, &dy, HERDER_STEP, HERDER_STEP);
    e->vx = dx;
    e->vy = dy;
    e->x = (int16_t)(uint16_t)(e->x + dx);
    e->y = (int16_t)(uint16_t)(e->y + dy);
}

/* Herder that hums when it is close to its target ship, unless that sound (priority 0x0c)
 * is what plays or played last. game86 1000:2457 */
void think_15(GameState *game, Entity *e)
{
    int distance = magnitude16((int16_t)(uint16_t)(e->x - game->target.x))
                 + magnitude16((int16_t)(uint16_t)(e->y - game->target.y));
    if ((int16_t)(uint16_t)distance <= PROXIMITY_RANGE
        && game_sound_status().priority != PROXIMITY_SAMPLE_PRIORITY)
        play(SOUND_PROXIMITY);
    think_0e(game, e);
}

/* Herder that now and then (4/256 per reload period) leaves a think_17 chaser behind. The
 * slot is not taken from free_slots. game86 1000:23a7 */
void think_16(GameState *game, Entity *e)
{
    if ((int8_t)--e->countdown < 0) {
        e->countdown = e->reload;
        if (rng_next_byte(&game->rng) < SPAWN_CHANCE_16) {
            for (Entity *s = game->entities; s < game->entities + ENTITY_COUNT; s++) {
                if (!entity_is_free(s))
                    continue;
                s->x = e->x;
                s->y = e->y;
                s->anim = ANIM_IDLE;
                s->scale = 0;
                enemy_setup_0e(game, s);
                s->kind = -1;
                play(SOUND_SPAWN_16);
                break;
            }
        }
    }
    think_0e(game, e);
}
