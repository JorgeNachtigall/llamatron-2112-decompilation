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
    CHASER_SPRITE = 0xab,               /* + 4-frame animation */
    COLUMN_SPRITE = 0xb3,               /* + 1 in the second phase */
    BOUNCER_SPRITE = 0xb5,              /* think_19's first frame */
    ANIM_PLAIN = 0x0d,
    THINK_BOUNCER = 0x19,
    CHASER_SPEED_PARAM = 4,             /* the level script's parameter for the chaser speed */
};

static const SoundRequest SOUND_RELEASE = { SAMPLE_ID_RELEASE, 1, 4 };   /* 1000:20ea */

/* Chaser (the chaser view): keeps its last four positions in chaser.trail, chaser.newest is
 * the byte offset (0, 4, 8, 12) of the newest one; chaser.steer is the axis it moves along
 * (1 = x, 0 = y; negative = start with x) and how likely (out of 256) it keeps that axis
 * each tick. game86 1000:220b */
void think_17(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;
    e->sprite = (int16_t)(CHASER_SPRITE + ((game->tick_count >> 1) & 3));
    int16_t newest = (int16_t)((e->chaser.newest + sizeof(Point16))
                               & ((TRAIL_POINTS - 1) * sizeof(Point16)));
    e->chaser.trail[newest / sizeof(Point16)] = (Point16){ e->x, e->y };
    e->chaser.newest = newest;

    int8_t axis = e->chaser.steer.axis;
    if (axis < 0) {
        axis = 0;
    } else if (rng_next_byte(&game->rng) > e->chaser.steer.keep_chance) {
        entity_move_bounce(game, e);
        return;
    }
    axis ^= 1;
    e->chaser.steer.axis = axis;

    int16_t speed = game->player.script_param[CHASER_SPEED_PARAM];
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
    e->sprite = (int16_t)(e->timer + COLUMN_SPRITE);
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
        s->anim = ANIM_PLAIN;
        s->scale = 0;
        s->vx = random_step(game);
        s->sprite = BOUNCER_SPRITE;
        s->vy = (int16_t)((rng_next_byte(&game->rng) & 3) - 1);
        s->scale = 1;
        s->solid = 1;
        s->hit_handler = 0;
        s->death_sample = SAMPLE_ID_POP;
        s->death_rate = 0;
        s->kind = -1;
        entity_set_type(s, THINK_BOUNCER, THINK_BOUNCER);
        game->enemies_alive++;
        game_sound_play(SOUND_RELEASE.sample, SOUND_RELEASE.rate_index, SOUND_RELEASE.priority);
        return;
    }
}
