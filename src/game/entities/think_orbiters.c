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
    BOLT_FRAME_TICKS = 3,
    STORM_CENTRE_X = 0xa0, STORM_CENTRE_Y = 0x64,  /* bolts land within +-128 / +-64 of it */
    CORE_SPRITE = 0xde, CORE_HIT_SPRITE = 0xdf,
    SATELLITE_SPRITE = 0xe0, SATELLITE_HIT_SPRITE = 0xe4,  /* + 4-frame animation */
    SATELLITE_SHOT_SPRITE = 0xe5,       /* + 1 when flying left */
    RUNNER_SPRITE = 0xe7,               /* + 1 horizontal, + 2 forwards; intro: 4-frame anim */
    ANIM_PLAIN = 0x0d,
    THINK_BOLT = 0x2d,
};

static const SoundRequest SOUND_THUNDER = { SAMPLE_ID_EXPLOSION, 3, 0x0a };    /* 1000:2748 */

/* Counts the counter down; true when it passes zero (and it restarts at the period). */
static bool counter_passed_zero(ByteCounter *c)
{
    int8_t left = (int8_t)(c->left - 1);
    bool passed = left < 0;
    c->left = passed ? c->period : (uint8_t)left;
    return passed;
}

static int16_t sine_half(uint8_t angle)
{
    return (int16_t)(asset_sine()->sine8[angle] >> 1);
}

/* orbit.orbit: the angle on the circle and the angular speed. game86 1000:2ab5 */
static void orbit_step(Entity *e)
{
    uint8_t angle = (uint8_t)(e->orbit.orbit.angle + e->orbit.orbit.speed);
    e->orbit.orbit.angle = angle;
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
 * speed) and launches a think_01 spawn every period of orbit.launch. game86 1000:2a75 */
void think_20(GameState *game, Entity *e)
{
    if (game->wave_intro_timer != 0)
        return;
    if (counter_passed_zero(&e->orbit.launch)) {
        Entity *spawn = entity_spawn_01_at(game, e);
        if (!spawn) /* with no free slot the original still marks the slot after the
                       search's end (61) as not counted */
            spawn = &game->entities[CORE_SEARCH_SLOTS];
        spawn->kind = -1;
    }
    game->orbit_angle += ORBIT_SPEED_STEP;
    int16_t speed = (int16_t)(uint16_t)(game->orbit_angle >> 16);
    game->player.enemy_speed_x = speed;
    game->player.enemy_speed_y = speed;
    int16_t sprite = CORE_SPRITE;
    if (e->orbit.hit_frames >= 0) {
        e->orbit.hit_frames--;
        sprite = CORE_HIT_SPRITE;
    }
    e->sprite = sprite;
    orbit_step(e);
}

/* Satellite: fires a child towards its target ship at one phase of orbit.fire.
 * game86 1000:2a21 */
void think_21(GameState *game, Entity *e)
{
    uint8_t phase = e->orbit.fire.left;
    int16_t sprite = (int16_t)(SATELLITE_SPRITE + ((phase >> 2) & 3));
    if (e->orbit.hit_frames >= 0) {
        sprite = SATELLITE_HIT_SPRITE;
        e->orbit.hit_frames--;
    }
    e->sprite = sprite;
    if (phase == SATELLITE_FIRE_PHASE) {
        int16_t shot_sprite = SATELLITE_SHOT_SPRITE;
        int16_t vx = 2;
        if ((int16_t)(uint16_t)(e->x - game->target.x) >= 0) {
            vx = -2;
            shot_sprite++;
        }
        int16_t vy = (int16_t)(uint16_t)(e->y - game->target.y) < 0 ? 1 : -1;
        entity_spawn_child_if(game, e, shot_sprite, vx, vy);
    }
    counter_passed_zero(&e->orbit.fire);
    orbit_step(e);
}

/* ---- think_22 ---------------------------------------------------------------------------- */

/* The edge runner (the edge_runner view) moves along one axis at a time with a decelerating
 * 16.16 speed; its 16.16 position's whole part is the coordinate. */

/* New random speed, deceleration and axis; the direction then points at the target ship.
 * game86 1000:292b */
static void runner_turn(GameState *game, Entity *e)
{
    EdgeRunnerView *r = &e->edge_runner;
    r->speed.whole = (int16_t)((rng_next_byte(&game->rng) & 1) + 1);
    r->speed.frac = (uint16_t)((uint16_t)rng_next_byte(&game->rng) << 8);
    r->decel = (int16_t)(uint16_t)((rng_next_byte(&game->rng) + 1) << 4);
    r->axis ^= 1;
    if (r->axis)
        r->backwards = (int16_t)(uint16_t)(e->y - game->target.y) < 0 ? 0 : 1;
    else
        r->backwards = (int16_t)(uint16_t)(e->x - game->target.x) < 0 ? 0 : 1;
}

/* Edge runner: after its intro animation it runs along x or y, slowing down, and turns
 * (new speed, the other axis, towards its target ship) when it stops or would leave the
 * playfield. game86 1000:2865 */
void think_22(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;
    if (game->wave_intro_timer != 0)
        return;
    EdgeRunnerView *r = &e->edge_runner;
    if (r->intro_frames >= 0) {
        e->sprite = (int16_t)(RUNNER_SPRITE + ((game->tick_count >> 1) & 3));
        r->intro_frames--;
        return;
    }

    int32_t speed = fixed16_get(r->speed);
    bool forward = r->backwards == 0;
    bool vertical = r->axis != 0;
    int16_t sprite = (int16_t)(RUNNER_SPRITE + (vertical ? 0 : 1) + (forward ? 2 : 0));
    Fixed16 *axis = vertical ? &r->position_y : &r->position_x;
    int32_t position = fixed16_get(*axis);
    position = (int32_t)(forward ? (uint32_t)position + (uint32_t)speed
                                 : (uint32_t)position - (uint32_t)speed);
    int16_t pixel = (int16_t)(uint16_t)((uint32_t)position >> 16);
    bool inside = forward ? pixel < (vertical ? RUNNER_Y_END : RUNNER_X_END)
                          : pixel >= RUNNER_MIN;
    if (!inside) {
        runner_turn(game, e);
        return;
    }
    fixed16_set(axis, position);
    if (vertical)
        e->y = pixel;
    else
        e->x = pixel;
    e->sprite = sprite;

    speed = (int32_t)((uint32_t)speed - (uint32_t)(int32_t)r->decel);
    fixed16_set(&r->speed, speed);
    if (speed < 0)
        runner_turn(game, e);
}

/* ---- think_2c / think_2d ---------------------------------------------------------------- */

/* Storm: every period of storm.strike the screen flashes, thunder sounds and a bolt
 * (think_2d) strikes somewhere around the centre; after storm.strikes_left strikes the storm
 * ends (its slot is not returned to free_slots). game86 1000:2736 */
void think_2c(GameState *game, Entity *e)
{
    if (!counter_passed_zero(&e->storm.strike))
        return;
    entity_start_flash(game, FLASH_LIST_STORM);
    game_sound_play(SOUND_THUNDER.sample, SOUND_THUNDER.rate_index, SOUND_THUNDER.priority);
    if (--e->storm.strikes_left < 0) {
        entity_vanish(e);
        game->enemies_alive--;
        return;
    }
    Entity *bolt = entity_alloc(game);
    if (!bolt)
        return;
    bolt->kind = -1;
    bolt->solid = 0;
    bolt->x = (int16_t)((int8_t)rng_next_byte(&game->rng) + STORM_CENTRE_X);
    bolt->y = (int16_t)(((int8_t)rng_next_byte(&game->rng) >> 1) + STORM_CENTRE_Y);
    bolt->anim = ANIM_PLAIN;
    bolt->sprite = BOLT_FIRST_SPRITE;
    bolt->storm.frame = (ByteCounter){ .period = BOLT_FRAME_TICKS, .left = BOLT_FRAME_TICKS };
    entity_set_type(bolt, THINK_BOLT, THINK_BOLT);
    game->enemies_alive++;
}

/* Bolt: steps through sprites 0x147..0x149, one every period of storm.frame, then
 * vanishes (without returning its slot to free_slots). game86 1000:27c4 */
void think_2d(GameState *game, Entity *e)
{
    if (!counter_passed_zero(&e->storm.frame))
        return;
    int16_t sprite = (int16_t)(e->sprite + 1);
    if (sprite == BOLT_LAST_SPRITE) {
        entity_vanish(e);
        game->enemies_alive--;
        return;
    }
    e->sprite = sprite;
}
