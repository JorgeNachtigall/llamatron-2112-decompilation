/*
 * think_homing.c - enemies that steer towards their target ship with inertia (think_10) and
 * the others built on the same mover (beams think_11/12, dropper think_13, think_1f/28/2a).
 *
 * The mover keeps 16.16 fixed-point values in pairs of record words:
 *   velocity x  +14/+16 (timer, aux16)      velocity y  +18/+1a (hp, aux1a)
 *   accel x     +1c/+1e (aux1c, kill_action) accel y   +20/+22 (sub_mode, reload/countdown)
 *   position x  +26/+28 (aux26, anim_toggle) position y +2a/+2c (aux2a, sprite_ref)
 *   start delay +30 (aux30)
 * The accessors below are the only place that knows which fields form which value.
 */
#include "entities.h"
#include "sound.h"

enum {
    SPEED_LIMIT_SHIFT = 5,          /* |velocity| <= 32 * acceleration (1000:390a..3922) */
    HOMING_X_MIN = 0x08, HOMING_X_END = 0x130,
    HOMING_Y_MIN = 0x10, HOMING_Y_END = 0xb7,
    BEAM_RANGE = 10,
    THINK28_SPRITE = 0x118,         /* + 4-frame animation */
    THINK2A_CHILD_SPRITE = 0x12a,
};

static const SoundRequest SOUND_BEAM = { SAMPLE_ID_BEAM, 0, 7 };     /* 1000:387c */
static const SoundRequest SOUND_DROP = { SAMPLE_ID_DROP, 1, 3 };     /* 1000:2597 */

static int32_t join32(int16_t low, int16_t high)
{
    return (int32_t)(uint32_t)((uint32_t)(uint16_t)high << 16 | (uint16_t)low);
}

static void split32(int32_t value, int16_t *low, int16_t *high)
{
    *low = (int16_t)(uint16_t)((uint32_t)value & 0xffff);
    *high = (int16_t)(uint16_t)((uint32_t)value >> 16);
}

static int32_t neg32(int32_t v) { return (int32_t)(0u - (uint32_t)v); }
static int32_t add32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }

typedef struct HomingAxis {
    int32_t velocity, accel, position;
} HomingAxis;

static HomingAxis axis_x(const Entity *e)
{
    return (HomingAxis){ join32(e->timer, e->aux16), join32(e->aux1c, e->kill_action),
                         join32(e->aux26, e->anim_toggle) };
}

static HomingAxis axis_y(const Entity *e)
{
    int16_t accel_high = (int16_t)(uint16_t)((uint16_t)e->countdown << 8 | e->reload);
    return (HomingAxis){ join32(e->hp, e->aux1a), join32(e->sub_mode, accel_high),
                         join32(e->aux2a, e->sprite_ref) };
}

static void store_x(Entity *e, const HomingAxis *a)
{
    split32(a->velocity, &e->timer, &e->aux16);
    split32(a->position, &e->aux26, &e->anim_toggle);
}

static void store_y(Entity *e, const HomingAxis *a)
{
    split32(a->velocity, &e->hp, &e->aux1a);
    split32(a->position, &e->aux2a, &e->sprite_ref);
}

/* Accelerates towards the target: `offset` = own coordinate minus the target's. */
static void steer(HomingAxis *a, int16_t offset)
{
    a->velocity = add32(a->velocity, offset >= 0 ? neg32(a->accel) : a->accel);
}

/* Caps |velocity| at 32 * accel. The original compares the high words signed and, when the
 * high word is not greater, the low words unsigned on their own, so a smaller high word with
 * a larger low word also counts as too fast (1000:3924..392a). */
static void limit_speed(HomingAxis *a)
{
    uint32_t speed = (uint32_t)(a->velocity < 0 ? neg32(a->velocity) : a->velocity);
    uint32_t limit = (uint32_t)a->accel << SPEED_LIMIT_SHIFT;
    bool too_fast = (int16_t)(uint16_t)(speed >> 16) > (int16_t)(uint16_t)(limit >> 16)
                 || (uint16_t)speed > (uint16_t)limit;
    if (too_fast)
        a->velocity = a->velocity < 0 ? neg32((int32_t)limit) : (int32_t)limit;
}

/* Moves along one axis; a step that would leave (min, end) stops the axis instead.
 * Returns the new integer coordinate, or `current` when stopped. */
static int16_t advance(HomingAxis *a, int16_t current, int min, int end)
{
    int32_t position = add32(a->position, a->velocity);
    int16_t pixel = (int16_t)(uint16_t)((uint32_t)position >> 16);
    if (pixel > min && pixel < end) {
        a->position = position;
        return pixel;
    }
    a->velocity = 0;
    return current;
}

/* The mover, also the tail of think_13, 1f, 28 and 2a. game86 1000:38a2 */
void entity_homing_move(GameState *game, Entity *e)
{
    if (game->wave_intro_timer != 0)
        return;
    HomingAxis x = axis_x(e), y = axis_y(e);
    if (e->aux30 >= 0) {
        e->aux30--;
    } else {
        steer(&x, (int16_t)(uint16_t)(e->x - game->target.x));
        steer(&y, (int16_t)(uint16_t)(e->y - game->target.y));
    }
    limit_speed(&x);
    limit_speed(&y);
    e->x = advance(&x, e->x, HOMING_X_MIN, HOMING_X_END);
    store_x(e, &x);
    e->y = advance(&y, e->y, HOMING_Y_MIN, HOMING_Y_END);
    store_y(e, &y);
}

/* Homing enemy with a 4-frame animation from sprite base aux24. game86 1000:388a */
void think_10(GameState *game, Entity *e)
{
    e->sprite = (int16_t)(((game->tick_count >> 2) & 3) + e->aux24);
    if (entity_check_touch(game, e))
        return;
    entity_homing_move(game, e);
}

/* The beam emitters keep two byte pairs (low = period, high = countdown):
 *   aux24: beam duration / time left of the current beam (0 = off)
 *   aux2e: pause between beams / time left of the pause */
static uint8_t high_byte(int16_t w) { return (uint8_t)((uint16_t)w >> 8); }
static uint8_t low_byte(int16_t w) { return (uint8_t)w; }
static void set_high_byte(int16_t *w, uint8_t v)
{
    *w = (int16_t)(uint16_t)((uint16_t)v << 8 | low_byte(*w));
}

static bool beam_on(const Entity *e) { return high_byte(e->aux24) != 0; }

static void beam_cycle(GameState *game, Entity *e)
{
    /* game86 1000:3860 */
    if (beam_on(e)) {
        set_high_byte(&e->aux24, (uint8_t)(high_byte(e->aux24) - 1));
    } else {
        set_high_byte(&e->aux2e, (uint8_t)(high_byte(e->aux2e) - 1));
        if (high_byte(e->aux2e) == 0) {
            set_high_byte(&e->aux2e, low_byte(e->aux2e));
            set_high_byte(&e->aux24, low_byte(e->aux24));
            game_sound_play(SOUND_BEAM.sample, SOUND_BEAM.rate_index, SOUND_BEAM.priority);
        }
    }
    entity_homing_move(game, e);
}

static int16_t distance16(int16_t a, int16_t b)
{
    int16_t d = (int16_t)(uint16_t)(a - b);
    return d < 0 ? (int16_t)(uint16_t)-d : d;
}

/* While its beam is on, a ship in line with the emitter dies. The distance, not touch
 * bits, is what the death code then tests for "P2" (it is still in AX there). */
static bool beam_hits(GameState *game, int16_t p1_distance, int16_t p2_distance)
{
    if (p1_distance < BEAM_RANGE) {
        deadly_touch(game, (uint16_t)p1_distance);
        return true;
    }
    if ((game->options & OPT_DOUBLE_CONTROLLER) && p2_distance < BEAM_RANGE) {
        deadly_touch(game, (uint16_t)p2_distance);
        return true;
    }
    return false;
}

/* Vertical beam: kills ships at the same x. game86 1000:37fe */
void think_12(GameState *game, Entity *e)
{
    if (beam_on(e) && beam_hits(game, distance16(e->x, game->ship[0].x),
                                distance16(e->x, game->ship[1].x)))
        return;
    beam_cycle(game, e);
}

/* Horizontal beam: kills ships at the same y. game86 1000:382d */
void think_11(GameState *game, Entity *e)
{
    if (beam_on(e) && beam_hits(game, distance16(e->y, game->ship[0].y),
                                distance16(e->y, game->ship[1].y)))
        return;
    beam_cycle(game, e);
}

/* Homing enemy that drops a projectile every period of its aux24 byte pair, aux2e times,
 * once its start delay is over. Touching it kills like the deadly pickup.
 * game86 1000:256e */
void think_13(GameState *game, Entity *e)
{
    e->sprite = (int16_t)(0x89 + ((game->tick_count >> 2) & 7));
    if (e->aux30 < 0) {
        int8_t countdown = (int8_t)(high_byte(e->aux24) - 1);
        set_high_byte(&e->aux24, (uint8_t)countdown);
        if (countdown < 0 && --e->aux2e >= 0) {
            set_high_byte(&e->aux24, low_byte(e->aux24));
            enemy_shot_drop(game, e);
            game_sound_play(SOUND_DROP.sample, SOUND_DROP.rate_index, SOUND_DROP.priority);
        }
    }
    unsigned touched = entity_hits_player(game, e);
    if (touched) {
        deadly_touch(game, touched);
        return;
    }
    entity_homing_move(game, e);
}

/* Homing enemy whose acceleration keeps growing by the level's tuning value; its sprite
 * (base in vx) faces the target and shows a flash while aux24 counts down.
 * game86 1000:2d6e */
void think_1f(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;
    uint16_t growth = (uint16_t)game->tuning.p0826;
    HomingAxis x = axis_x(e), y = axis_y(e);
    x.accel = add32(x.accel, growth);
    y.accel = add32(y.accel, growth);
    split32(x.accel, &e->aux1c, &e->kill_action);
    int16_t accel_y_high;
    split32(y.accel, &e->sub_mode, &accel_y_high);
    e->reload = (uint8_t)accel_y_high;
    e->countdown = (uint8_t)((uint16_t)accel_y_high >> 8);

    int16_t sprite = e->vx;
    if ((int16_t)(uint16_t)(e->x - game->target.x) < 0)
        sprite++;
    if (e->aux24 >= 0) {
        sprite += 2;
        e->aux24--;
    }
    e->sprite = sprite;
    entity_homing_move(game, e);
}

/* think_1f with its own 4-frame animation. game86 1000:2d5a */
void think_28(GameState *game, Entity *e)
{
    think_1f(game, e);
    e->sprite = (int16_t)(THINK28_SPRITE + ((game->tick_count >> 2) & 3));
}

/* Homing enemy that fires a child (THINK2A_CHILD_SPRITE, its own vx/vy) every period of its aux2e
 * byte pair. game86 1000:2d35 */
void think_2a(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;
    int8_t countdown = (int8_t)(high_byte(e->aux2e) - 1);
    set_high_byte(&e->aux2e, (uint8_t)countdown);
    if (countdown < 0) {
        set_high_byte(&e->aux2e, low_byte(e->aux2e));
        entity_spawn_child_if(game, e, THINK2A_CHILD_SPRITE, e->vx, e->vy);
    }
    entity_homing_move(game, e);
}
