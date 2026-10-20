/*
 * think_herders.c - herders (think_0e and the variants 15, 16 built on it): they walk to a
 * llama still waiting to be rescued (think_05, see think_llamas.c), turn it into something
 * according to their mode, then return to the companion and pick the next llama.
 *
 * The fields are HerderView's (entity_views.h). herder.target is the slot of the llama being
 * fetched, or HERDER_SEEK (pick one) / HERDER_RETURN (walk back to the companion). A claimed
 * llama (llama.claimed) is left to its herder; game->unhatched counts the llamas left.
 */
#include "entities.h"
#include "sound.h"

enum {
    HERDER_SEEK = -1,
    HERDER_RETURN = -2,
    THINK_LLAMA = 0x05,
    ARRIVED_RANGE = 8,
    HERDER_STEP = 3,                    /* max pixels per axis per step */
    CAUGHT_SPRITE = 0x33,
    CAUGHT_TICKS = 0x14,
    THINK_TIMER = 0x09,
    PENDING_DEADLY_PICKUP = 9 + 1,      /* sub-mode 3: the next kill drops pickup kind 9 */
    PROXIMITY_RANGE = 0x1e,             /* think_15 */
    PROXIMITY_SAMPLE_PRIORITY = 0x0c,
    SPAWN_CHANCE_16 = 4,                /* out of 256 per period */
};

/* What a herder does to the llama it reaches, by herder.mode (CS:3AFF). */
enum { LLAMA_TO_TIMER, LLAMA_TO_HERDER, LLAMA_BURSTS, LLAMA_TO_TIMER_ARM_PICKUP };

static const SoundRequest SOUND_CAUGHT = { SAMPLE_ID_NONE, 1, 4 };        /* 1000:3b80 */
static const SoundRequest SOUND_NEW_HERDER = { SAMPLE_ID_ALERT, 0, 3 };    /* 1000:3b17 */
static const SoundRequest SOUND_PROXIMITY = { SAMPLE_ID_GAME_OVER, 2, 0x0c };  /* 1000:2484 */
static const SoundRequest SOUND_SPAWN_16 = { SAMPLE_ID_POP, 1, 4 };            /* 1000:23f5 */

/* Sprite offsets of the four walking directions in a walker's sprite set. */
enum { FACE_LEFT = 0, FACE_UP = 2, FACE_DOWN = 4, FACE_RIGHT = 6 };

static void play(SoundRequest s)
{
    game_sound_play(s.sample, s.rate_index, s.priority);
}

static int16_t magnitude16(int16_t v)
{
    return v < 0 ? (int16_t)(uint16_t)-v : v;
}

/* Claims the first unclaimed llama; the search gives up after game->unhatched llamas. The original
 * scans on past the table when there are fewer llamas than unhatched says; the port stops at
 * the table's end and returns as if all llamas were taken. Also think_08's search
 * (1000:498e): herder.target is the same word as hunter.quarry. game86 1000:3a17 */
void herder_claim_llama(GameState *game, Entity *e)
{
    int16_t llamas_left = game->unhatched;
    if (llamas_left != 0) {
        for (int slot = 0; slot < ENTITY_COUNT; slot++) {
            Entity *llama = &game->entities[slot];
            if (llama->think != THINK_LLAMA)
                continue;
            if (llama->llama.claimed == 0) {
                llama->llama.claimed = 1;
                e->herder.target = (int16_t)slot;
                return;
            }
            if (--llamas_left == 0)
                break;
        }
    }
    e->herder.target = HERDER_RETURN;
}

/* The direction it mostly walks in picks the sprite pair; the frame alternates every 4
 * pixels along that axis. */
static int16_t herder_sprite(const Entity *e)
{
    int16_t frame;
    if (magnitude16(e->vy) > magnitude16(e->vx))
        frame = (int16_t)((e->vy >= 0 ? FACE_DOWN : FACE_UP) + ((e->y >> 2) & 1));
    else
        frame = (int16_t)((e->vx >= 0 ? FACE_RIGHT : FACE_LEFT) + ((e->x >> 2) & 1));
    return (int16_t)(frame + e->herder.sprite_base);
}

/* Done with a llama (or back at the companion): `marker` shows the "caught" sprite for a
 * while and the herder looks for the next one. game86 1000:3b70 */
static void herder_drop(Entity *e, Entity *marker)
{
    e->herder.target = HERDER_SEEK;
    marker->sprite = CAUGHT_SPRITE;
    marker->timer = CAUGHT_TICKS;
    play(SOUND_CAUGHT);
}

/* The llama is taken: it no longer counts as one and vanishes after the "caught" sprite.
 * game86 1000:3b63 */
static void llama_to_timer(GameState *game, Entity *e, Entity *llama)
{
    entity_set_type(llama, THINK_TIMER, 0);
    game->unhatched--;
    llama->kind = -1;
    herder_drop(e, llama);
}

/* game86 1000:3aeb..3b62 */
static void herder_handle_llama(GameState *game, Entity *e, Entity *llama)
{
    switch (e->herder.mode) {
    case LLAMA_TO_TIMER:
        llama_to_timer(game, e, llama);
        break;
    case LLAMA_TO_HERDER:               /* 3b0f: the llama becomes an enemy; the herder keeps
                                           the slot as target and, as it is no longer a llama,
                                           claims another on its next step */
        game->unhatched--;
        enemy_setup_0c(game, llama);
        play(SOUND_NEW_HERDER);
        break;
    case LLAMA_BURSTS:                  /* 3b21: four children fly apart */
        entity_burst_four(game, e);
        llama_to_timer(game, e, llama);
        break;
    case LLAMA_TO_TIMER_ARM_PICKUP:     /* 3b07 */
        game->player.pickup_pending = PENDING_DEADLY_PICKUP;
        llama_to_timer(game, e, llama);
        break;
    default:    /* the original jumps through the code after its 4-entry table (CS:3AFF);
                 * enemy_init_04 leaves herder.mode stale, see docs/DEVIATIONS.md #26 */
        break;
    }
}

/* Herder: once per herder.step period it walks (up to 3 pixels per axis) to its llama, or
 * back to the companion, which stands on its target ship meanwhile. game86 1000:39e9 */
void think_0e(GameState *game, Entity *e)
{
    if (game->wave_intro_timer != 0)
        return;
    if ((int8_t)--e->herder.step.left >= 0)
        return;
    e->herder.step.left = e->herder.step.period;

    if (e->herder.target == HERDER_RETURN) {
        game->companion.x = game->target.x;
        game->companion.y = game->target.y;
    } else if (e->herder.target < 0) {
        herder_claim_llama(game, e);
        return;
    }

    e->sprite = herder_sprite(e);
    Entity *goal;
    if (e->herder.target == HERDER_RETURN) {
        goal = &game->companion;
    } else {
        goal = e->herder.target < ENTITY_COUNT ? &game->entities[e->herder.target] : NULL;
        if (!goal || goal->think != THINK_LLAMA) {
            herder_claim_llama(game, e);   /* the llama was rescued, caught or shot meanwhile */
            return;
        }
    }

    if (entity_check_touch(game, e))
        return;

    int16_t dx = (int16_t)(uint16_t)(goal->x - e->x);
    int16_t dy = (int16_t)(uint16_t)(goal->y - e->y);
    if (magnitude16(dx) < ARRIVED_RANGE && magnitude16(dy) < ARRIVED_RANGE) {
        if (e->herder.target == HERDER_RETURN)
            herder_drop(e, goal);
        else
            herder_handle_llama(game, e, goal);
        return;
    }
    vector_shrink_to(&dx, &dy, HERDER_STEP, HERDER_STEP);
    e->vx = dx;
    e->vy = dy;
    e->x = (int16_t)(uint16_t)(e->x + dx);
    e->y = (int16_t)(uint16_t)(e->y + dy);
}

/* Herder that hums when it is within 30 pixels (x plus y) of its target ship, unless the hum
 * is already what plays (or played last). game86 1000:2457 */
void think_15(GameState *game, Entity *e)
{
    int distance = magnitude16((int16_t)(uint16_t)(e->x - game->target.x))
                 + magnitude16((int16_t)(uint16_t)(e->y - game->target.y));
    if ((int16_t)(uint16_t)distance <= PROXIMITY_RANGE
        && game_sound_status().priority != PROXIMITY_SAMPLE_PRIORITY)
        play(SOUND_PROXIMITY);
    think_0e(game, e);
}

/* Herder that now and then (4/256 per herder.spawn period) leaves a think_17 chaser behind.
 * The slot is not taken from free_slots. game86 1000:23a7 */
void think_16(GameState *game, Entity *e)
{
    if ((int8_t)--e->herder.spawn.left < 0) {
        e->herder.spawn.left = e->herder.spawn.period;
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
