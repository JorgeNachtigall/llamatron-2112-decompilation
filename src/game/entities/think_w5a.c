/*
 * think_w5a.c - the think handlers W5a took over from W5b: the eight-direction seeker that
 * the type-0x24 records run (think_24), the walker that lays them or fires sideways
 * (think_23), the dropper that falls, bounces and waits under the ship (think_26) and the
 * spawner that throws children in from the edges (think_27).
 */
#include "entities.h"

#include "game_data.h"
#include "rng.h"
#include "sound.h"

enum {
    SEEK_TURN_HALF = 4,             /* half of the eight directions (1000:34e3..34ed) */
    WALKER_SPRITE = 0xfc,           /* +1 while the walker is left of its target */
    WALKER_LAYS_FROM_LEVEL = 0x32,  /* from this level on, think_23 lays type-0x24 records */
    WALKER_CHILD_HP = 4,
    WALKER_CHILD_PHASE_RIGHT = 4,
    DROPPER_FLOOR = 0xb8,
    DROPPER_TOP = 0x1e,
    DROPPER_REACH = 0x10,
    DROPPER_GRAVITY = 0x2000,       /* 16.16 per tick */
    EDGE_SPAWNER_DEAD_SPRITE = 0x111,
    EDGE_SPAWNER_CHILD_SPRITE = 0x112,
};

/* think_26's fall: state in aux1c. */
enum { DROPPER_FALLING, DROPPER_RISING, DROPPER_WAITING };

static const SoundRequest SOUND_DROPPER_LANDS = { SAMPLE_ID_EXPLOSION, 3, 5 };     /* 1000:30b6 */

static uint8_t high_byte(int16_t w) { return (uint8_t)((uint16_t)w >> 8); }
static uint8_t low_byte(int16_t w) { return (uint8_t)w; }

static void set_high_byte(int16_t *w, uint8_t v)
{
    *w = (int16_t)(uint16_t)(((uint16_t)*w & 0x00ff) | (uint16_t)v << 8);
}

/* Counts the high byte of a byte pair down and reloads it from the low byte when it goes
 * negative; true on that tick. */
static bool byte_pair_expired(int16_t *pair)
{
    int8_t count = (int8_t)(high_byte(*pair) - 1);
    set_high_byte(pair, (uint8_t)count);
    if (count >= 0)
        return false;
    set_high_byte(pair, low_byte(*pair));
    return true;
}

static int32_t join32(int16_t low, int16_t high)
{
    return (int32_t)(uint32_t)((uint32_t)(uint16_t)high << 16 | (uint16_t)low);
}

static void split32(int32_t value, int16_t *low, int16_t *high)
{
    *low = (int16_t)(uint16_t)((uint32_t)value & 0xffff);
    *high = (int16_t)(uint16_t)((uint32_t)value >> 16);
}

static bool left_of(int16_t a, int16_t b) { return (int16_t)(a - b) < 0; }

/* ---- think_24 ----------------------------------------------------------------------------- */

/* One step round the eight directions towards `wanted`, the short way. Aligned already, it
 * still turns one step clockwise (1000:34f1 starts from +1). */
static int16_t turn_towards(int16_t dir, int wanted)
{
    int16_t diff = (int16_t)(wanted - dir);
    int16_t size = diff < 0 ? (int16_t)-diff : diff;
    if (diff >= 0 ? size >= SEEK_TURN_HALF : size > SEEK_TURN_HALF)
        diff = (int16_t)-diff;
    return (int16_t)((dir + (diff < 0 ? -1 : 1)) & (ENEMY_DIRECTIONS - 1));
}

/* Seeker: every reload period (low byte of hp, counted in the high byte) it turns one of
 * eight directions (aux16) towards the target's quadrant, then flies that way.
 * game86 1000:3490 */
void think_24(GameState *game, Entity *e)
{
    if (game->wave_intro_timer != 0)
        return;
    if (entity_check_touch(game, e))
        return;
    const EnemyTables *tables = asset_enemy_tables();
    if (byte_pair_expired(&e->hp) && tables) {
        unsigned quadrant = (left_of(e->x, game->target.x) ? 1u : 0u) |
                            (left_of(e->y, game->target.y) ? 2u : 0u);
        e->aux16 = turn_towards(e->aux16, (uint8_t)tables->seek_dir_by_quadrant[quadrant]);
    }
    /* aux16 is always a direction 0..7: enemy_setup_type24 and the turn above set it. */
    int dir = e->aux16 & (ENEMY_DIRECTIONS - 1);
    e->sprite = (int16_t)(e->aux16 + e->aux1a);
    if (tables) {
        e->vx = tables->seek_vx[dir];
        e->vy = tables->seek_vy[dir];
    }
    entity_move_bounce(game, e);
}

/* ---- think_23 ----------------------------------------------------------------------------- */

/* Walker on the homing mover, facing its target. Every period of the byte pair in vx it
 * fires a child sideways or, from level 0x32 on, lays a type-0x24 seeker. The tick it does,
 * it skips the move. game86 1000:35b4 */
void think_23(GameState *game, Entity *e)
{
    if (entity_check_touch(game, e))
        return;
    bool facing_left = !left_of(e->x, game->target.x);
    e->sprite = (int16_t)(WALKER_SPRITE + (facing_left ? 0 : 1));
    if (byte_pair_expired(&e->vx)) {
        if (game->wave_intro_timer != 0)
            return;
        if (game->player.level < WALKER_LAYS_FROM_LEVEL) {
            if (facing_left)
                entity_spawn_child_if(game, e, 0xff, -1, 0);
            else
                entity_spawn_child_if(game, e, 0xfe, 1, 0);
        } else {
            Entity *child = entity_alloc(game);
            if (child) {
                child->kind = -1;
                child->x = e->x;
                child->y = e->y;
                enemy_setup_type24(game, child, facing_left ? 0 : WALKER_CHILD_PHASE_RIGHT,
                                   WALKER_CHILD_HP);
            }
        }
    }
    entity_homing_move(game, e);
}

/* ---- think_26 ----------------------------------------------------------------------------- */

/* Dropper: falls with gravity (height 16.16 in timer/aux16, speed 16.16 in hp/aux1a) to the
 * floor, then rises one pixel per tick to the top and waits there; whenever the ship is
 * below it and within reach in x it falls again. Meanwhile it creeps towards the ship's x
 * every tick that tuning.p0830 masks out, except at height 1. game86 1000:3014 */
void think_26(GameState *game, Entity *e)
{
    if (game->wave_intro_timer != 0)
        return;
    if (entity_check_touch(game, e))
        return;
    if ((game->tick_count & (uint16_t)game->tuning.p0830) == 0 && e->aux16 != 1)
        e->x = (int16_t)(e->x + (left_of(game->target.x, e->x) ? -1 : 1));

    if (e->aux1c != DROPPER_FALLING) {
        int16_t dx = (int16_t)(game->target.x - e->x);
        if (dx < 0)
            dx = (int16_t)-dx;
        if (dx <= DROPPER_REACH && e->y <= game->target.y) {
            e->hp = 0;
            e->aux1a = 0;
            e->aux1c = DROPPER_FALLING;
        }
        if (e->aux1c == DROPPER_WAITING)
            return;
        if (e->aux1c != DROPPER_FALLING) {          /* rising (any other state counts) */
            e->aux16--;
            if (e->aux16 <= DROPPER_TOP)
                e->aux1c = DROPPER_WAITING;
            e->y = e->aux16;
            return;
        }
    }
    int32_t speed = join32(e->hp, e->aux1a);
    int32_t height = (int32_t)((uint32_t)join32(e->timer, e->aux16) + (uint32_t)speed);
    split32(height, &e->timer, &e->aux16);
    split32((int32_t)((uint32_t)speed + DROPPER_GRAVITY), &e->hp, &e->aux1a);
    if (e->aux16 < DROPPER_FLOOR) {
        e->y = e->aux16;
        return;
    }
    e->aux1c = DROPPER_RISING;
    game_sound_play(SOUND_DROPPER_LANDS.sample, SOUND_DROPPER_LANDS.rate_index,
                    SOUND_DROPPER_LANDS.priority);
}

/* ---- think_27 ----------------------------------------------------------------------------- */

/* Edge spawner: bounces around and every period of the byte pair in aux16 throws a child
 * (sprite 0x112, moving down-left) in from a random point of the right or top edge. Touching
 * a ship does not kill: it stops (timer set) and shows its dead sprite. game86 1000:2f87 */
void think_27(GameState *game, Entity *e)
{
    if (e->timer != 0)
        return;
    if (entity_hits_player(game, e)) {
        e->timer = 1;
        e->sprite = EDGE_SPAWNER_DEAD_SPRITE;
        game->enemies_alive--;
        return;
    }
    entity_move_bounce(game, e);
    if (!byte_pair_expired(&e->aux16))
        return;
    /* The child starts where the spawner is, so it is moved there for the call. */
    Point16 home = { e->x, e->y };
    if (rng_next_byte(&game->rng) & 1) {
        e->x = 0x12f;
        e->y = (int16_t)((int8_t)rng_next_byte(&game->rng) >> 1) + 0x64;
    } else {
        e->x = (int16_t)((int8_t)rng_next_byte(&game->rng) + 0xa0);
        e->y = 0x14;
    }
    entity_spawn_child_if(game, e, EDGE_SPAWNER_CHILD_SPRITE, -1, 1);
    e->x = home.x;
    e->y = home.y;
}
