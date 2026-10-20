/*
 * think_spawners.c - the three-stage family: a wandering mother (think_0b) that drops
 * shooters (think_0c), which fire aimed shots (think_0d, think_basic.c).
 *
 * Both stages keep a period/countdown byte pair in one 16-bit field (low byte = period,
 * high byte = countdown), like the original's byte accesses to it.
 */
#include "entities.h"
#include "sound.h"

enum {
    SHOOTER_SPRITE = 0x40,
    SHOOTER_FIRE_PERIOD = 0x3f,
    SHOT_SPRITE = 0x41,
    SHOT_AIM_MAX_X = 2,             /* [0x41b]/[0x41d] at 1000:5008 and 5039 */
    SHOT_AIM_MAX_Y = 1,
    THINK_SHOOTER = 0x0c,
    THINK_SHOT = 0x0d,
};

static const SoundRequest SOUND_DROP_SHOOTER = { SAMPLE_ID_BEAM, 1, 3 };   /* 1000:4f2e */

/* Counts the high byte down; when it passes zero it restarts at the low byte (the period)
 * and the call returns true. */
static bool pair_count_down(int16_t *pair)
{
    uint8_t period = (uint8_t)*pair;
    int8_t countdown = (int8_t)((uint16_t)*pair >> 8);
    bool elapsed = --countdown < 0;
    if (elapsed)
        countdown = (int8_t)period;
    *pair = (int16_t)(uint16_t)((uint16_t)(uint8_t)countdown << 8 | period);
    return elapsed;
}

/* A random step of -1, 1 or 2 (rng & 3, minus 1, zero redrawn). */
static int16_t random_step(GameState *game)
{
    int16_t step;
    do
        step = (int16_t)((rng_next_byte(&game->rng) & 3) - 1);
    while (step == 0);
    return step;
}

static int16_t wrap16(int value)
{
    return (int16_t)(uint16_t)value;
}

/* Mother: wanders in random directions without bouncing (a step that would leave the
 * playfield is not taken). After a start delay (aux16, counting down while >= 0) it drops
 * aux1a shooters, one every period of its hp byte pair. game86 1000:4ed7 */
void think_0b(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;
    e->sprite = (int16_t)(((game->tick_count >> 1) & 3) + e->aux1c);
    if (game->wave_intro_timer != 0)
        return;

    if (e->aux16 >= 0) {
        e->aux16--;
    } else {
        if (pair_count_down(&e->hp) && e->aux1a != 0) {
            e->aux1a--;
            Entity *shooter = entity_spawn_debris(game, e);
            if (shooter) {
                shooter->sprite = SHOOTER_SPRITE;
                shooter->timer = 0;
                shooter->aux16 = SHOOTER_FIRE_PERIOD;   /* countdown 0: fires next tick */
                entity_set_type(shooter, THINK_SHOOTER, 0);
                game->enemies_alive++;
                game_sound_play(SOUND_DROP_SHOOTER.sample, SOUND_DROP_SHOOTER.rate_index,
                                SOUND_DROP_SHOOTER.priority);
            }
        }
    }

    if (--e->timer < 0) {
        e->vx = random_step(game);
        e->vy = random_step(game);
        e->timer = (int16_t)((rng_next_byte(&game->rng) & 0x1f) + 0x1f);
    }
    int16_t x = wrap16(e->x + e->vx);
    if (x > BOUNCE_X_MIN && x < BOUNCE_X_MAX)
        e->x = x;
    int16_t y = wrap16(e->y + e->vy);
    if (y > BOUNCE_Y_MIN && y < 0xc0)
        e->y = y;
}

/* Shooter: bounces around, fires an aimed shot every period of its aux16 byte pair, and
 * every 0x20..0x9f ticks turns towards its target ship. game86 1000:4feb */
void think_0c(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;

    if (pair_count_down(&e->aux16)) {
        Entity *shot = entity_spawn_debris(game, e);
        if (shot) {
            shot->sprite = SHOT_SPRITE;
            Point16 v = entity_aim_at_target(game, e, SHOT_AIM_MAX_X, SHOT_AIM_MAX_Y);
            shot->vx = v.x;
            shot->vy = v.y;
            entity_set_type(shot, THINK_SHOT, 0);
            game->enemies_alive++;
        }
    }

    if (--e->timer >= 0) {
        entity_move_bounce(game, e);
        return;
    }
    e->timer = (int16_t)((rng_next_byte(&game->rng) & 0x7f) + 0x20);
    Point16 v = entity_aim_at_target(game, e, SHOT_AIM_MAX_X, SHOT_AIM_MAX_Y);
    e->vx = v.x;
    e->vy = v.y;
}
