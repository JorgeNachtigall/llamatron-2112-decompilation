/*
 * think_orbiters.c - enemies on a circle around the screen centre (think_20 core, think_21
 * satellites), the edge runner (think_22) and the lightning storm (think_2c strikes, think_2d
 * bolts).
 */
#include "entities.h"
#include "game_data.h"
#include "sound.h"

enum {
    ORBIT_CENTRE_X = 0xa0,
    ORBIT_CENTRE_Y = 0x6e,
    QUARTER_TURN = 0x40,
    ORBIT_SPEED_STEP = 0x40,            /* think_20: orbit_angle += per tick (16.16) */
    CORE_SEARCH_SLOTS = 0x3d,           /* 1000:366f only looks at the first 61 slots */
    SATELLITE_FIRE_PHASE = 5,
    RUNNER_X_END = 0x138, RUNNER_Y_END = 0xbe, RUNNER_MIN = 0x10,
    BOLT_FIRST_SPRITE = 0x147, BOLT_LAST_SPRITE = 0x14a,
};

static const SoundRequest SOUND_THUNDER = { SAMPLE_ID_EXPLOSION, 3, 0x0a };    /* 1000:2748 */

static uint8_t high_byte(int16_t w) { return (uint8_t)((uint16_t)w >> 8); }
static uint8_t low_byte(int16_t w) { return (uint8_t)w; }
static int16_t make_word(uint8_t high, uint8_t low)
{
    return (int16_t)(uint16_t)((uint16_t)high << 8 | low);
}

/* Counts the high byte of a period/countdown pair down; true when it passes zero (and it
 * restarts at the period). */
static bool pair_passed_zero(int16_t *pair)
{
    int8_t countdown = (int8_t)(high_byte(*pair) - 1);
    bool passed = countdown < 0;
    *pair = make_word(passed ? low_byte(*pair) : (uint8_t)countdown, low_byte(*pair));
    return passed;
}

static int16_t sine_half(uint8_t angle)
{
    return (int16_t)(asset_sine()->sine8[angle] >> 1);
}

/* timer: low byte = angle on the circle, high byte = angular speed. game86 1000:2ab5 */
static void orbit_step(Entity *e)
{
    uint8_t angle = (uint8_t)(low_byte(e->timer) + high_byte(e->timer));
    e->timer = make_word(high_byte(e->timer), angle);
    e->x = (int16_t)(sine_half(angle) + ORBIT_CENTRE_X);
    e->y = (int16_t)(sine_half((uint8_t)(angle + QUARTER_TURN)) + ORBIT_CENTRE_Y);
}

/* A think_01 spawn at `at` in the first free slot among the first 61, counted as roster
 * kind 1; NULL when none is free. Its slot is not taken from free_slots. game86 1000:366f */
Entity *entity_spawn_01_at(GameState *game, const Entity *at)
{
    for (int i = 0; i < CORE_SEARCH_SLOTS; i++) {
        Entity *spawn = &game->entities[i];
        if (!entity_is_free(spawn))
            continue;
        spawn->x = at->x;
        spawn->y = at->y;
        spawn->anim = ANIM_IDLE;
        spawn->scale = 0;
        enemy_setup_01(game, spawn);
        spawn->kind = 1;
        return spawn;
    }
    return NULL;
}

/* Core of the orbit: speeds the whole wave up (orbit_angle's integer part becomes the enemy
 * speed) and launches a think_01 spawn every period of its hp byte pair; aux1a = frames of
 * the "hit" sprite left. game86 1000:2a75 */
void think_20(GameState *game, Entity *e)
{
    if (game->wave_intro_timer != 0)
        return;
    if (pair_passed_zero(&e->hp)) {
        Entity *spawn = entity_spawn_01_at(game, e);
        if (!spawn) /* the search leaves SI on slot 61, whose kind the caller then overwrites */
            spawn = &game->entities[CORE_SEARCH_SLOTS];
        spawn->kind = -1;
    }
    game->orbit_angle += ORBIT_SPEED_STEP;
    int16_t speed = (int16_t)(uint16_t)(game->orbit_angle >> 16);
    game->player.enemy_speed_x = speed;
    game->player.enemy_speed_y = speed;
    int16_t sprite = 0xde;
    if (e->aux1a >= 0) {
        e->aux1a--;
        sprite = 0xdf;
    }
    e->sprite = sprite;
    orbit_step(e);
}

/* Satellite: fires a child towards its target ship at one phase of its hp byte pair.
 * game86 1000:2a21 */
void think_21(GameState *game, Entity *e)
{
    uint8_t phase = high_byte(e->hp);
    int16_t sprite = (int16_t)(0xe0 + ((phase >> 2) & 3));
    if (e->aux1a >= 0) {
        sprite = 0xe4;
        e->aux1a--;
    }
    e->sprite = sprite;
    if (phase == SATELLITE_FIRE_PHASE) {
        int16_t shot_sprite = 0xe5;
        int16_t vx = 2;
        if ((int16_t)(uint16_t)(e->x - game->target.x) >= 0) {
            vx = -2;
            shot_sprite++;
        }
        int16_t vy = (int16_t)(uint16_t)(e->y - game->target.y) < 0 ? 1 : -1;
        entity_spawn_child_if(game, e, shot_sprite, vx, vy);
    }
    pair_passed_zero(&e->hp);
    orbit_step(e);
}

/* ---- think_22 ---------------------------------------------------------------------------- */

/* The edge runner moves along one axis at a time with a decelerating 16.16 speed:
 *   timer = axis (0 = x, 1 = y), hp = direction (0 = increasing), aux1a = intro frames,
 *   x = aux1c/kill_action, y = sub_mode/(reload, countdown), speed = aux24/aux26,
 *   anim_toggle = deceleration per tick. */
static int32_t join32(int16_t low, int16_t high)
{
    return (int32_t)(uint32_t)((uint32_t)(uint16_t)high << 16 | (uint16_t)low);
}

static int16_t low16(int32_t v) { return (int16_t)(uint16_t)((uint32_t)v & 0xffff); }
static int16_t high16(int32_t v) { return (int16_t)(uint16_t)((uint32_t)v >> 16); }

/* New random speed, deceleration and axis; the direction then points at the target ship.
 * game86 1000:292b */
static void runner_turn(GameState *game, Entity *e)
{
    e->aux26 = (int16_t)((rng_next_byte(&game->rng) & 1) + 1);
    e->aux24 = (int16_t)(uint16_t)((uint16_t)rng_next_byte(&game->rng) << 8);
    e->anim_toggle = (int16_t)(uint16_t)((rng_next_byte(&game->rng) + 1) << 4);
    e->timer ^= 1;
    if (e->timer)
        e->hp = (int16_t)(uint16_t)(e->y - game->target.y) < 0 ? 0 : 1;
    else
        e->hp = (int16_t)(uint16_t)(e->x - game->target.x) < 0 ? 0 : 1;
}

/* game86 1000:2865 */
void think_22(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;
    if (game->wave_intro_timer != 0)
        return;
    if (e->aux1a >= 0) {
        e->sprite = (int16_t)(0xe7 + ((game->tick_count >> 1) & 3));
        e->aux1a--;
        return;
    }

    int32_t speed = join32(e->aux24, e->aux26);
    bool forward = e->hp == 0;
    bool vertical = e->timer != 0;
    int16_t sprite = (int16_t)((vertical ? 0xe7 : 0xe8) + (forward ? 2 : 0));
    int32_t position = vertical ? join32(e->sub_mode, make_word(e->countdown, e->reload))
                                : join32(e->aux1c, e->kill_action);
    position = (int32_t)(forward ? (uint32_t)position + (uint32_t)speed
                                 : (uint32_t)position - (uint32_t)speed);
    int16_t pixel = high16(position);
    bool inside = forward ? pixel < (vertical ? RUNNER_Y_END : RUNNER_X_END)
                          : pixel >= RUNNER_MIN;
    if (!inside) {
        runner_turn(game, e);
        return;
    }
    if (vertical) {
        e->y = pixel;
        e->sub_mode = low16(position);
        e->reload = (uint8_t)pixel;
        e->countdown = (uint8_t)((uint16_t)pixel >> 8);
    } else {
        e->x = pixel;
        e->aux1c = low16(position);
        e->kill_action = pixel;
    }
    e->sprite = sprite;

    speed = (int32_t)((uint32_t)speed - (uint32_t)(int32_t)e->anim_toggle);
    e->aux24 = low16(speed);
    e->aux26 = high16(speed);
    if (speed < 0)
        runner_turn(game, e);
}

/* ---- think_2c / think_2d ---------------------------------------------------------------- */

/* Storm: every period of its aux16 byte pair the screen flashes, thunder sounds and a bolt
 * (think_2d) strikes somewhere around the centre; after `timer` strikes the storm ends (its
 * slot is not returned to free_slots). game86 1000:2736 */
void think_2c(GameState *game, Entity *e)
{
    if (!pair_passed_zero(&e->aux16))
        return;
    entity_start_flash(game, FLASH_LIST_STORM);
    game_sound_play(SOUND_THUNDER.sample, SOUND_THUNDER.rate_index, SOUND_THUNDER.priority);
    if (--e->timer < 0) {
        entity_vanish(e);
        game->enemies_alive--;
        return;
    }
    Entity *bolt = entity_alloc(game);
    if (!bolt)
        return;
    bolt->kind = -1;
    bolt->solid = 0;
    bolt->x = (int16_t)((int8_t)rng_next_byte(&game->rng) + 0xa0);
    bolt->y = (int16_t)(((int8_t)rng_next_byte(&game->rng) >> 1) + 0x64);
    bolt->anim = 0x0d;
    bolt->sprite = BOLT_FIRST_SPRITE;
    bolt->timer = 0x303;
    entity_set_type(bolt, 0x2d, 0x2d);
    game->enemies_alive++;
}

/* Bolt: steps through sprites 0x147..0x149, one every period of its timer byte pair, then
 * vanishes (without returning its slot to free_slots). game86 1000:27c4 */
void think_2d(GameState *game, Entity *e)
{
    if (!pair_passed_zero(&e->timer))
        return;
    int16_t sprite = (int16_t)(e->sprite + 1);
    if (sprite == BOLT_LAST_SPRITE) {
        entity_vanish(e);
        game->enemies_alive--;
        return;
    }
    e->sprite = sprite;
}
