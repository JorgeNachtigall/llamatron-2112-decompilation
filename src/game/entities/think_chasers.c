/*
 * think_chasers.c - think_17, a chaser that closes in on its target ship along one axis at a
 * time and leaves a short position trail, and think_18, which patrols the centre column and
 * releases think_19 bouncers.
 */
#include "entities.h"
#include "sound.h"

enum {
    TRAIL_POINTS = 4,
    COLUMN_X = 0xa0,                    /* think_18 */
    COLUMN_Y_MIN = 0x10, COLUMN_Y_END = 0xb8,
    FLIP_CHANCE = 5,                    /* out of 256 per tick */
    RELEASE_CHANCE = 0x14,
};

static const SoundRequest SOUND_RELEASE = { 0x0c, 1, 4 };   /* 1000:20ea */

/* think_17 keeps its last four positions in the words +16..+24 (pairs x, y), timer is the
 * byte offset (0, 4, 8, 12) of the newest one. */
static void trail_store(Entity *e, int point, int16_t x, int16_t y)
{
    switch (point) {
    case 0: e->aux16 = x; e->hp = y; break;                     /* +16/+18 */
    case 1: e->aux1a = x; e->aux1c = y; break;                  /* +1a/+1c */
    case 2: e->kill_action = x; e->sub_mode = y; break;         /* +1e/+20 */
    default:                                                    /* +22/+24 */
        e->reload = (uint8_t)x;
        e->countdown = (uint8_t)((uint16_t)x >> 8);
        e->aux24 = y;
        break;
    }
}

static uint8_t low_byte(int16_t w) { return (uint8_t)w; }
static uint8_t high_byte(int16_t w) { return (uint8_t)((uint16_t)w >> 8); }

/* aux12: low byte = axis it moves along (1 = x, 0 = y; negative = start with x), high byte =
 * how likely (out of 256) it keeps its axis each tick. game86 1000:220b */
void think_17(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;
    e->sprite = (int16_t)(0xab + ((game->tick_count >> 1) & 3));
    int16_t newest = (int16_t)((e->timer + 4) & 0xc);
    trail_store(e, newest / 4, e->x, e->y);
    e->timer = newest;

    int8_t axis = (int8_t)low_byte(e->aux12);
    if (axis < 0) {
        axis = 0;
    } else if (rng_next_byte(&game->rng) > high_byte(e->aux12)) {
        entity_move_bounce(game, e);
        return;
    }
    axis ^= 1;
    e->aux12 = (int16_t)(uint16_t)((uint16_t)high_byte(e->aux12) << 8 | (uint8_t)axis);

    int16_t speed = game->player.script_param[4];
    if (axis) {
        e->vy = 0;
        e->vx = (int16_t)(uint16_t)(game->target.x - e->x) < 0 ? (int16_t)(uint16_t)-speed : speed;
    } else {
        e->vx = 0;
        e->vy = (int16_t)(uint16_t)(game->target.y - e->y) < 0 ? (int16_t)(uint16_t)-speed : speed;
    }
    entity_move_bounce(game, e);
}

static int16_t random_step(GameState *game)
{
    int16_t step;
    do
        step = (int16_t)((rng_next_byte(&game->rng) & 3) - 1);
    while (step == 0);
    return step;
}

/* Moves up and down the centre column, blinking between two sprites (timer); in its first
 * phase it sometimes releases a think_19 bouncer. The slot is not taken from free_slots.
 * game86 1000:203f */
void think_18(GameState *game, Entity *e)
{
    e->x = COLUMN_X;
    e->sprite = (int16_t)(e->timer + 0xb3);
    if (rng_next_byte(&game->rng) < FLIP_CHANCE)
        e->timer ^= 1;

    int16_t y = (int16_t)(uint16_t)(e->y + e->vy);
    if (y < COLUMN_Y_MIN || y >= COLUMN_Y_END) {
        e->vy = (int16_t)(uint16_t)-e->vy;
        y = (int16_t)(uint16_t)(y + e->vy);
    }
    e->y = y;

    if (e->timer != 0 || rng_next_byte(&game->rng) >= RELEASE_CHANCE)
        return;
    for (Entity *s = game->entities; s < game->entities + ENTITY_COUNT; s++) {
        if (!entity_is_free(s))
            continue;
        s->x = e->x;
        s->y = e->y;
        s->anim = 0x0d;
        s->scale = 0;
        s->vx = random_step(game);
        s->sprite = 0xb5;
        s->vy = (int16_t)((rng_next_byte(&game->rng) & 3) - 1);
        s->scale = 1;
        s->solid = 1;
        s->hit_handler = 0;
        s->death_sample = 0x07;
        s->death_rate = 0;
        s->kind = -1;
        entity_set_type(s, 0x19, 0x19);
        game->enemies_alive++;
        game_sound_play(SOUND_RELEASE.sample, SOUND_RELEASE.rate_index, SOUND_RELEASE.priority);
        return;
    }
}
