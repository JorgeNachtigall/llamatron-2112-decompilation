/*
 * enemy_init.c - the 41 wave-spawn init handlers (tbl_enemy_init, DS:2CA4) and the init bodies
 * other handlers reuse.
 *
 * The spawn loop (W4, game86 1000:1d97..1dcb) has already taken the slot, cleared its overlay,
 * set its roster kind and counted it off free_slots; an init handler places the record and
 * gives it its type (think handler), sprite, hit handler and the parameters of its view.
 * Fields an init does not write keep whatever the slot held before, as in the original.
 *
 * Handlers are named by table index; each section says which enemy it makes. Many start as
 * a spawning record (think_00 under the numbers 04, 07 or 0a, which only differ in score)
 * that grows in and then hatches into its real type.
 */
#include "entities.h"

#include <stddef.h>

#include "game_data.h"
#include "rng.h"

/* Llama sprites by wave: sprite ref table entry 0x1B1 + 2 * wave + walk phase
 * (218C:0362, game86 1000:1f45..1f54). */
enum { SPRITE_REF_LLAMA_BY_WAVE = 0x1b1 };

/* Each type's first sprite number (its thinks and anims step on from there). */
enum {
    SPRITE_HUNTER = 0x2b,
    SPRITE_MOTHER = 0x3c,
    SPRITE_BURSTING_HERDER = 0x4f,
    SPRITE_HERDER = 0x6f,
    SPRITE_SHOT_SLOW = 0x7b,            /* 05; also the speed-0x200 enemy shot */
    SPRITE_SHOT_MEDIUM = 0x7f,          /* 06; ent_hit_04's fire-back */
    SPRITE_SHOT_FAST = 0x83,            /* 07; ent_hit_05's fire-back */
    SPRITE_BEAM_HORIZONTAL = 0x87,
    SPRITE_BEAM_VERTICAL = 0x88,
    SPRITE_BOMBER = 0x89,               /* + its 8-frame cycle */
    SPRITE_FAST_BOUNCER = 0x95,
    SPRITE_HUMMING_HERDER = 0x99,
    SPRITE_LAYING_HERDER = 0xa3,
    SPRITE_CHASER = 0xaf,
    SPRITE_COLUMN = 0xb2,
    SPRITE_SNAKE_SEGMENT = 0xc6,
    SPRITE_SNAKE_HEAD = 0xcc,
    SPRITE_SEGMENT_11 = 0xce,           /* the segment families of inits 11..14 and 1f */
    SPRITE_SEGMENT_12 = 0xd2,
    SPRITE_SEGMENT_13 = 0xd6,
    SPRITE_SEGMENT_14 = 0xda,
    SPRITE_SEGMENT_1F = 0x118,
    SPRITE_ORBIT_CORE = 0xde,
    SPRITE_SATELLITE = 0xe0,
    SPRITE_RUNNER = 0xe7,
    SPRITE_WALKER = 0xfc,
    SPRITE_SLOW_SEEKER = 0x104,
    SPRITE_SEEKER_CHARGER = 0x10c,
    SPRITE_DROPPER = 0x10f,
    SPRITE_EDGE_SPAWNER = 0x110,
    SPRITE_PAIR_CHARGER = 0x113,
    SPRITE_PICKUP_CHARGER = 0x11c,
    SPRITE_MOTHER_21 = 0x122,
    SPRITE_EDGE_GUNNER_BOTTOM = 0x126,
    SPRITE_EDGE_GUNNER_RIGHT = 0x127,
    SPRITE_EDGE_GUNNER_TOP = 0x128,
    SPRITE_EDGE_GUNNER_LEFT = 0x129,
    SPRITE_ARMING_HERDER = 0x12b,
    SPRITE_SEEKER = 0x136,              /* + its direction 0..7 */
    SPRITE_STORM_CENTRE = 0x146,
};

/* Think handlers (think_table.c). */
enum {
    THINK_SWARM = 0x03,
    THINK_SPAWNING_EGG = 0x04,          /* think_00, hatches into THINK_LLAMA */
    THINK_LLAMA = 0x05,
    THINK_SPAWNING = 0x07,              /* think_00 */
    THINK_HUNTER = 0x08,
    THINK_SPAWNING_MOTHER = 0x0a,       /* think_00, hatches into THINK_MOTHER */
    THINK_MOTHER = 0x0b,
    THINK_HERDER = 0x0e,
    THINK_BEAM_HORIZONTAL = 0x11,
    THINK_BEAM_VERTICAL = 0x12,
    THINK_BOMBER = 0x13,
    THINK_FAST_BOUNCER = 0x14,
    THINK_HUMMING_HERDER = 0x15,
    THINK_LAYING_HERDER = 0x16,         /* leaves chasers behind */
    THINK_CHASER = 0x17,
    THINK_COLUMN = 0x18,
    THINK_SNAKE_HEAD = 0x1d,
    THINK_SNAKE_SEGMENT = 0x1e,
    THINK_SEGMENT = 0x1f,
    THINK_ORBIT_CORE = 0x20,
    THINK_SATELLITE = 0x21,
    THINK_RUNNER = 0x22,
    THINK_WALKER = 0x23,
    THINK_SEEKER = 0x24,
    THINK_CHARGER = 0x25,
    THINK_DROPPER = 0x26,
    THINK_EDGE_SPAWNER = 0x27,
    THINK_SEGMENT_ANIMATED = 0x28,
    THINK_CHARGER_ANIMATED = 0x29,
    THINK_EDGE_GUNNER = 0x2a,
    THINK_STORM_CENTRE = 0x2b,
};

/* Animation states (entity_anim_handlers, anim.c) besides ANIM_PLAIN. A spawning record's
 * think_00 steps its state on by one when it hatches: ANIM_SPAWN_SCALED into ANIM_PLAIN,
 * ANIM_EGG_SCALED into the llama's power-up mark. */
enum {
    ANIM_WAVE_LLAMA = 0x01,
    ANIM_BEAM_HORIZONTAL = 0x02,
    ANIM_BEAM_VERTICAL = 0x03,
    ANIM_SPAWN_SCALED = 0x0c,
    ANIM_EGG_SCALED = 0x0f,
    ANIM_HIDDEN = 0x11,
};

/* A spawning record's grow-in countdown (Entity.scale, think_00 counts it down by 2). */
enum { HATCH_QUICK = 2, HATCH_SLOW = 0x32 };

/* Fixed positions. */
enum {
    FIELD_CENTRE_X = 0xa0,
    TOP_START_Y = 0x1e,                 /* the column and the dropper start here */
    ORBIT_CENTRE_Y = 0x6e,
    STORM_CENTRE_Y = 0x64,
    EDGE_TOP_Y = 0x11, EDGE_BOTTOM_Y = 0xbe, EDGE_LEFT_X = 8, EDGE_RIGHT_X = 0x137,
};

/* Level script parameters (Player.script_param) the inits read. */
enum {
    PARAM_SWARM_HIT_HANDLER = 0,        /* 0 = HIT_LOSE_HP with KILL_PLAIN */
    PARAM_SWARM_HP = 1,                 /* plus the level loop count */
    PARAM_SWARM_KILL_ACTION = 3,
    PARAM_HERDER_SPAWN = 5,             /* herder.spawn counter, as one word */
    PARAM_HERDER_STEP = 6,              /* herder.step counter, as one word */
};

/* herder.target to start with, and what a herder does to the llama it reaches
 * (think_herders.c). */
enum { HERDER_SEEK = -1, HERDER_RETURN = -2 };
enum { HERDER_MULTIPLIES = 1, HERDER_BURSTS_LLAMA = 2, HERDER_ARMS_PICKUP = 3 };

/* The seeker's turn counter as enemy_init_27 starts it, as one word: period 4, left 1
 * (the pieces split_into_seeker makes start at SEEKER_TURN_PIECE, kill_actions.c). */
enum { SEEKER_TURN = 0x104 };

/* The radius-63 circle the orbiters start on: half the sine table's amplitude. */
enum { ORBIT_RADIUS_SHIFT = 1, SINE_QUARTER_TURN = 0x40 };

static void set_death_sound(Entity *e, uint8_t sample, uint8_t rate)
{
    e->death_sample = sample;
    e->death_rate = rate;
}

static ByteCounter byte_counter(uint8_t period, uint8_t left)
{
    return (ByteCounter){ .period = period, .left = left };
}

/* A counter given as one word, as the original stores it: period in the low byte. */
static ByteCounter counter_from_word(int16_t word)
{
    return byte_counter((uint8_t)word, (uint8_t)((uint16_t)word >> 8));
}

static void stand_still(Entity *e)
{
    e->vx = 0;
    e->vy = 0;
}

/* A random step of -1, 0, 1 or 2 (rng & 3, minus 1). */
static int16_t random_step(GameState *game)
{
    return (int16_t)((rng_next_byte(&game->rng) & 3) - 1);
}

/* The homing mover at rest where the entity is: no velocity, the given accelerations, its
 * 16.16 position at (x, y). */
static void homing_at_rest(Entity *e, int32_t accel_x, int32_t accel_y)
{
    HomingView *h = &e->homing;
    fixed16_set(&h->velocity_x, 0);
    fixed16_set(&h->velocity_y, 0);
    fixed16_set(&h->accel_x, accel_x);
    fixed16_set(&h->accel_y, accel_y);
    h->position_x = (Fixed16){ .frac = 0, .whole = e->x };
    h->position_y = (Fixed16){ .frac = 0, .whole = e->y };
}

/* ---- 00: an egg that hatches into a llama ---------------------------------------------- */

/* game86 1000:1dd0 */
void enemy_setup_00(GameState *game, Entity *e, bool hatch_fast)
{
    e->anim = ANIM_EGG_SCALED;
    stand_still(e);
    e->sprite = game->spawn_sprite;
    e->scale = hatch_fast ? HATCH_QUICK : HATCH_SLOW;
    e->solid = 0;
    e->llama.claimed = 0;
    e->llama.unused_1a_1c[0] = 0;
    e->llama.turn_timer = 0;
    e->llama.facing = 0;
    e->hit_handler = HIT_KILL;
    set_death_sound(e, SAMPLE_ID_ENEMY_DEATH, 0);
    e->llama.sprite_base = game->spawn_sprite;
    entity_set_type(e, THINK_SPAWNING_EGG, THINK_LLAMA);
    game->unhatched++;
    game->eggs_spawned++;
}

/* game86 1000:1dcd */
static void enemy_init_00(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    enemy_setup_00(game, e, game->player.wave_type != 0);
}

/* ---- 01: the swarm, in the wave's llama sprites ---------------------------------------- */

/* game86 1000:1edf. Hit handler, hit points and kill action come from the level script. */
void enemy_setup_01(GameState *game, Entity *e)
{
    e->anim = ANIM_WAVE_LLAMA;
    stand_still(e);
    e->sprite = -1;                         /* drawn from sprite_ref */
    e->scale = 0;
    e->solid = 1;
    game->spawn_phase++;
    e->timer = game->spawn_phase & 0x1f;
    e->hp = (int16_t)(game->player.script_param[PARAM_SWARM_HP] + game->player.level_loops);
    e->kill_action = game->player.script_param[PARAM_SWARM_KILL_ACTION];
    e->sub_mode = game->tuning.init01_mode;
    e->hit_handler = game->player.script_param[PARAM_SWARM_HIT_HANDLER];
    if (e->hit_handler == HIT_KILL) {
        e->hit_handler = HIT_LOSE_HP;
        e->kill_action = KILL_PLAIN;
    }
    entity_set_type(e, THINK_SWARM, THINK_SWARM);
    set_death_sound(e, SAMPLE_ID_ENEMY_DEATH, 3);

    const SpriteRefTable *refs = asset_sprite_refs();
    unsigned ref = SPRITE_REF_LLAMA_BY_WAVE + 2u * (unsigned)game->player.wave +
                   (unsigned)(e->anim_toggle & 1);
    if (refs && ref < refs->count && !(refs->refs[ref] & SPRITE_REF_LINK))
        asset_sprite_bank_ref(refs->refs[ref], &e->sprite_ref);
    game->enemies_alive++;
}

/* game86 1000:1edc */
static void enemy_init_01(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    enemy_setup_01(game, e);
}

/* ---- 02, 04: hunter and herder, hatching ----------------------------------------------- */

/* game86 1000:1f61: the hunter. Neither counted as alive nor given a death sound. */
static void enemy_init_02(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    e->anim = ANIM_SPAWN_SCALED;
    stand_still(e);
    e->sprite = SPRITE_HUNTER;
    e->scale = HATCH_QUICK;
    e->hunter.quarry = -1;                  /* pick a llama */
    e->hunter.hit_frames = 0;
    e->solid = 1;
    entity_set_type(e, THINK_SPAWNING, THINK_HUNTER);
    e->hit_handler = HIT_PUSH_BACK;
}

/* game86 1000:1f96: the herder, quicker after the first loop of the levels. It leaves
 * herder.mode as the slot had it (docs/DEVIATIONS.md #26). */
static void enemy_init_04(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    set_death_sound(e, SAMPLE_ID_KNOCK, 0);
    e->anim = ANIM_SPAWN_SCALED;
    stand_still(e);
    e->sprite = SPRITE_HERDER;
    e->scale = HATCH_QUICK;
    e->solid = 1;
    entity_set_type(e, THINK_SPAWNING, THINK_HERDER);
    e->herder.target = HERDER_RETURN;
    e->herder.unused_16 = 0;
    e->herder.hp = 1;
    e->herder.step = game->player.level_loops != 0 ? byte_counter(2, 2) : byte_counter(5, 5);
    e->herder.sprite_base = e->sprite;
    e->herder.kill_action = KILL_DEBRIS_CORNERS;
    e->hit_handler = HIT_LOSE_HP;
    game->enemies_alive++;
}

/* ---- 03, 21: mothers dropping shooters, hatching --------------------------------------- */

static void init_mother(GameState *game, Entity *e, int16_t sprite, int16_t start_delay,
                        uint8_t drop_period, int16_t drops)
{
    enemy_random_spawn_pos(game, e);
    e->anim = ANIM_SPAWN_SCALED;
    e->vx = random_step(game);
    e->vy = random_step(game);
    e->sprite = sprite;
    e->mother.sprite_base = sprite;
    e->scale = HATCH_SLOW;
    e->solid = 0;
    e->mother.turn_timer = 0x64;
    e->mother.start_delay = start_delay;
    e->mother.drop = byte_counter(drop_period, 0);
    e->mother.drops_left = drops;
    e->hit_handler = HIT_PICKUP_CHANCE;
    set_death_sound(e, SAMPLE_ID_LETTER_LANDS, 0);
    entity_set_type(e, THINK_SPAWNING_MOTHER, THINK_MOTHER);
    game->enemies_alive++;
}

/* game86 1000:1e2c */
static void enemy_init_03(GameState *game, Entity *e)
{
    init_mother(game, e, SPRITE_MOTHER, 0x12c, 0x60, 3);
}

/* game86 1000:1e84: starts dropping sooner, more often and more of them. */
static void enemy_init_21(GameState *game, Entity *e)
{
    init_mother(game, e, SPRITE_MOTHER_21, 0x1e, 0x30, 0x14);
}

/* ---- 05, 06, 07: projectiles placed at random ------------------------------------------ */

/* game86 1000:24d6: the record becomes a projectile fired from itself. */
static void init_as_projectile(GameState *game, Entity *e, int16_t sprite, int16_t speed,
                               int16_t hit_handler)
{
    enemy_random_spawn_pos(game, e);
    const EnemyShot shot = { .velocity_x = 0, .velocity_y = 0, .sprite = sprite, .speed = speed,
                             .hit_handler = hit_handler };
    stand_still(e);                         /* enemy_shot_init's caller-record clear */
    enemy_shot_init(game, e, e, &shot);
    e->homing.start_delay = 0;
}

/* game86 1000:24c1 */
static void enemy_init_05(GameState *game, Entity *e)
{
    init_as_projectile(game, e, SPRITE_SHOT_SLOW, 0x200, HIT_FIRE_BACK_PAIR);
}

/* game86 1000:2493 */
static void enemy_init_06(GameState *game, Entity *e)
{
    init_as_projectile(game, e, SPRITE_SHOT_MEDIUM, 0x400, HIT_FIRE_BACK_X);
}

/* game86 1000:24aa */
static void enemy_init_07(GameState *game, Entity *e)
{
    init_as_projectile(game, e, SPRITE_SHOT_FAST, 0x600, HIT_KILL);
}

/* ---- 08, 09: homing beams (think 11 / 12) ---------------------------------------------- */

static void init_beam(GameState *game, Entity *e, int16_t anim, int16_t sprite, int16_t speed)
{
    enemy_random_spawn_pos(game, e);
    set_death_sound(e, SAMPLE_ID_SHOT_DEATH, 0);
    e->anim = anim;
    stand_still(e);
    e->scale = 2;
    e->solid = 1;
    e->sprite = sprite;
    homing_at_rest(e, (uint16_t)speed, (uint16_t)speed);
    e->homing.beam = byte_counter(0x20, 0);
    e->homing.start_delay = 0;
}

/* game86 1000:2612: the horizontal beam, faster after the first loop. Not counted as
 * alive. */
static void enemy_init_08(GameState *game, Entity *e)
{
    init_beam(game, e, ANIM_BEAM_HORIZONTAL, SPRITE_BEAM_HORIZONTAL,
              game->player.level_loops != 0 ? 0x800 : 0x400);
    e->homing.pause = byte_counter(0x70, 0x70);
    e->hit_handler = HIT_KNOCK_AWAY;
    entity_set_type(e, THINK_BEAM_HORIZONTAL, THINK_BEAM_HORIZONTAL);
}

/* game86 1000:3736: the vertical beam. Not counted as alive; draws a random byte it never
 * uses (1000:37a4). */
static void enemy_init_09(GameState *game, Entity *e)
{
    init_beam(game, e, ANIM_BEAM_VERTICAL, SPRITE_BEAM_VERTICAL, 0x400);
    (void)rng_next_byte(&game->rng);
    e->homing.pause = byte_counter(0x70, 0x70);
    e->hit_handler = HIT_KNOCK_AWAY;
    entity_set_type(e, THINK_BEAM_VERTICAL, THINK_BEAM_VERTICAL);
}

/* ---- 0a .. 0e -------------------------------------------------------------------------- */

/* game86 1000:24ef: the bomber, hatching; it homes in at the wave's current speed and drops
 * seven bombs. */
static void enemy_init_0a(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    set_death_sound(e, SAMPLE_ID_NONE, 0);
    e->anim = ANIM_SPAWN_SCALED;
    stand_still(e);
    e->sprite = SPRITE_BOMBER;
    e->scale = 0x14;
    e->solid = 1;
    homing_at_rest(e, (int32_t)game->speed_accum, (int32_t)game->speed_accum);
    e->homing.drop = byte_counter(0x40, 0x40);
    e->homing.start_delay = 0x14;
    e->homing.drops_left = 7;
    e->hit_handler = HIT_KILL;
    entity_set_type(e, THINK_SPAWNING, THINK_BOMBER);
    game->enemies_alive++;
}

/* game86 1000:25ae: the fast bouncer, with 4 hit points. */
static void enemy_init_0b(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    set_death_sound(e, SAMPLE_ID_DEATH_0B, 0);
    e->anim = ANIM_PLAIN;
    e->vx = random_step(game);
    e->vy = random_step(game);
    e->sprite = SPRITE_FAST_BOUNCER;
    e->scale = 2;
    e->solid = 1;
    e->hp = 4;
    e->hit_handler = HIT_DEFLECT;
    entity_set_type(e, THINK_FAST_BOUNCER, THINK_FAST_BOUNCER);
    game->enemies_alive++;
}

/* game86 1000:2407: the humming herder, hatching (KILL_RESPAWN_HUMMER also brings a killed
 * record back with it). */
void enemy_setup_0c(GameState *game, Entity *e)
{
    set_death_sound(e, SAMPLE_ID_POP, 0);
    e->anim = ANIM_SPAWN_SCALED;
    stand_still(e);
    e->scale = 8;
    e->solid = 1;
    e->sprite = SPRITE_HUMMING_HERDER;
    entity_set_type(e, THINK_SPAWNING, THINK_HUMMING_HERDER);
    e->herder.target = HERDER_RETURN;
    e->herder.unused_16 = 0;
    e->herder.hp = 0;
    e->herder.step = byte_counter(1, 1);
    e->herder.sprite_base = e->sprite;
    e->herder.kill_action = KILL_DEBRIS_SIDEWAYS;
    e->hit_handler = HIT_LOSE_HP;
    game->enemies_alive++;
}

/* game86 1000:2404 */
static void enemy_init_0c(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    enemy_setup_0c(game, e);
}

/* game86 1000:2347: the chaser-laying herder, hatching; the llamas it reaches become herders
 * too. */
static void enemy_init_0d(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    set_death_sound(e, SAMPLE_ID_ENEMY_DEATH, 0);
    e->anim = ANIM_SPAWN_SCALED;
    stand_still(e);
    e->sprite = SPRITE_LAYING_HERDER;
    e->scale = 0x28;
    e->solid = 1;
    entity_set_type(e, THINK_SPAWNING, THINK_LAYING_HERDER);
    e->herder.target = HERDER_SEEK;
    e->herder.unused_16 = 0;
    e->herder.hp = game->tuning.init0d_hp;
    e->herder.step = counter_from_word(game->player.script_param[PARAM_HERDER_STEP]);
    e->herder.sprite_base = e->sprite;
    e->herder.kill_action = KILL_PLAIN;
    e->herder.mode = HERDER_MULTIPLIES;
    e->herder.spawn = counter_from_word(game->player.script_param[PARAM_HERDER_SPAWN]);
    e->hit_handler = HIT_LOSE_HP;
    game->enemies_alive++;
}

/* game86 1000:21af: the chaser; the spawn position fills its four-point trail. */
void enemy_setup_0e(GameState *game, Entity *e)
{
    set_death_sound(e, SAMPLE_ID_ENEMY_DEATH, 0);
    e->anim = ANIM_PLAIN;
    stand_still(e);
    e->sprite = SPRITE_CHASER;
    e->scale = 0;
    e->solid = 1;
    e->chaser.steer = (ChaserSteer){ .axis = -1, .keep_chance = 0x0a };
    e->chaser.newest = 0;
    for (int i = 0; i < 4; i++)
        e->chaser.trail[i] = (Point16){ e->x, e->y };
    entity_set_type(e, THINK_CHASER, THINK_CHASER);
    e->hit_handler = HIT_KILL;
    game->enemies_alive++;
}

/* game86 1000:21ac */
static void enemy_init_0e(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    enemy_setup_0e(game, e);
}

/* ---- 0f, 1c: column and dropper, at the top centre ------------------------------------- */

/* game86 1000:1ff5: the column, 100 hit points, breaking into rocks. */
static void enemy_init_0f(GameState *game, Entity *e)
{
    e->x = FIELD_CENTRE_X;
    e->y = TOP_START_Y;
    set_death_sound(e, SAMPLE_ID_ENEMY_DEATH, 0);
    e->anim = ANIM_PLAIN;
    e->vx = 0;
    e->vy = 1;
    e->sprite = SPRITE_COLUMN;
    e->scale = 0;
    e->solid = 1;
    e->timer = 1;
    e->hp = 0x64;
    entity_set_type(e, THINK_COLUMN, THINK_COLUMN);
    e->kill_action = KILL_BREAK_INTO_ROCKS;
    e->hit_handler = HIT_LOSE_HP;
    game->enemies_alive++;
}

/* game86 1000:2f0d: the dropper. Not solid and not counted as alive. */
static void enemy_init_1c(GameState *game, Entity *e)
{
    (void)game;
    e->x = FIELD_CENTRE_X;
    e->y = TOP_START_Y;
    e->sprite = SPRITE_DROPPER;
    e->anim = ANIM_PLAIN;
    stand_still(e);
    e->scale = 0;
    e->solid = 0;
    e->dropper.height = (Fixed16){ .frac = 0, .whole = TOP_START_Y };
    fixed16_set(&e->dropper.fall_speed, 0);
    e->dropper.state = 2;                   /* waiting at the top (think_w5a.c DROPPER_WAITING) */
    entity_set_type(e, THINK_DROPPER, THINK_DROPPER);
}

/* ---- 10: the snake --------------------------------------------------------------------- */

enum { SNAKE_SEGMENTS = 8 };

/* Next free slot at or after `from`, or NULL. */
static Entity *free_slot_from(GameState *game, Entity *from)
{
    for (Entity *s = from; s < game->entities + ENTITY_COUNT; s++)
        if (entity_is_free(s))
            return s;
    return NULL;
}

/* game86 1000:2def: eight segments, each four trail steps further behind, then the head.
 * The slot search starts at the spawn's own record, which is still free, so the first
 * segment takes it and the head goes into a later slot. The original searches on past the
 * table when it runs out of free slots; the port stops at the end of the table. */
static void enemy_init_10(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    Point16 at = { e->x, e->y };

    Entity *s = e;
    int16_t delay = 0x20;
    for (int i = 0; i < SNAKE_SEGMENTS; i++, delay = (int16_t)(delay - 4)) {
        s = free_slot_from(game, s);
        if (!s)
            return;
        s->x = at.x;
        s->y = at.y;
        s->anim = ANIM_PLAIN;
        s->sprite = SPRITE_SNAKE_SEGMENT;
        s->scale = 0;
        s->solid = 1;
        s->kind = -1;
        s->segment.steps_behind = delay;
        s->segment.unused_16 = -4;
        s->segment.flash_frames = -1;
        s->hit_handler = HIT_SNAKE_SEGMENT;
        entity_set_type(s, THINK_SNAKE_SEGMENT, THINK_SNAKE_SEGMENT);
        s++;
    }

    Entity *head = free_slot_from(game, s);
    if (!head)
        return;
    head->x = at.x;
    head->y = at.y;
    set_death_sound(head, SAMPLE_ID_POP, 0);
    head->anim = ANIM_PLAIN;
    head->sprite = SPRITE_SNAKE_HEAD;
    head->scale = 2;
    head->solid = 1;
    homing_at_rest(head, 0x800, 0x800);
    head->homing.egg_hits = byte_counter(4, 4);
    head->homing.start_delay = 0;
    head->homing.unused_2e = 0;
    head->snake_head.next_trail_slot = 0;
    head->snake_head.spawn = byte_counter(0x30, 0x30);
    head->hit_handler = HIT_SNAKE_HEAD;
    head->kind = 0x10;
    entity_set_type(head, THINK_SNAKE_HEAD, THINK_SNAKE_HEAD);
    game->enemies_alive++;

    for (int i = 0; i < SNAKE_TRAIL_LENGTH; i++) {
        game->snake_trail[i].x = head->x;
        game->snake_trail[i].y = head->y;
        game->snake_trail[i].sprite = head->sprite;
    }
}

/* ---- 11..14, 1f: segments (think 1f, accelerating homers) ------------------------------ */

/* game86 1000:2b2d. The sprite also becomes the wave's segment family, which HIT_LEAVE_SEGMENT
 * leaves behind. */
static void init_segment(GameState *game, Entity *e, int16_t sprite)
{
    game->segment_sprite = sprite;
    enemy_random_spawn_pos(game, e);
    set_death_sound(e, SAMPLE_ID_POP, 0);
    e->sprite = game->segment_sprite;
    e->scale = 0;
    e->solid = 1;
    homing_at_rest(e, 0, 0);
    e->homing.flash_frames = -1;
    e->homing.start_delay = 0;
    e->homing.segment_family = game->tuning.segment_family;
    e->hit_handler = HIT_SEGMENT;
    e->anim = ANIM_PLAIN;
    e->growth.unused_0a = 0;
    e->growth.sprite_base = e->sprite;
    entity_set_type(e, THINK_SEGMENT, THINK_SEGMENT);
    game->enemies_alive++;
}

/* game86 1000:2b27 */
static void enemy_init_11(GameState *game, Entity *e) { init_segment(game, e, SPRITE_SEGMENT_11); }
/* game86 1000:2b0f */
static void enemy_init_12(GameState *game, Entity *e) { init_segment(game, e, SPRITE_SEGMENT_12); }
/* game86 1000:2b17 */
static void enemy_init_13(GameState *game, Entity *e) { init_segment(game, e, SPRITE_SEGMENT_13); }
/* game86 1000:2b1f */
static void enemy_init_14(GameState *game, Entity *e) { init_segment(game, e, SPRITE_SEGMENT_14); }

/* game86 1000:2b01: with its own animation. */
static void enemy_init_1f(GameState *game, Entity *e)
{
    init_segment(game, e, SPRITE_SEGMENT_1F);
    entity_set_type(e, THINK_SEGMENT_ANIMATED, THINK_SEGMENT_ANIMATED);
}

/* ---- 15, 16: orbit core and satellites on a circle around the centre ------------------- */

/* game86 1000:29af: position on the circle of radius 63 around the field centre at sine
 * phase `phase`, which the orbit angle starts from, turning by `step` per tick. Each one
 * moves the next one's phase on by the level's tuning step. */
static void init_orbiter(GameState *game, Entity *e, int16_t phase, int16_t sprite,
                         int16_t hits, int16_t step)
{
    const SineTables *sine = asset_sine();
    uint8_t at = (uint8_t)phase;
    int8_t sin_x = sine ? sine->sine8[at] : 0;
    int8_t sin_y = sine ? sine->sine8[(uint8_t)(at + SINE_QUARTER_TURN)] : 0;
    e->x = (int16_t)((sin_x >> ORBIT_RADIUS_SHIFT) + FIELD_CENTRE_X);
    e->y = (int16_t)((sin_y >> ORBIT_RADIUS_SHIFT) + ORBIT_CENTRE_Y);
    e->anim = ANIM_PLAIN;
    stand_still(e);
    e->sprite = sprite;
    e->scale = 0;
    e->solid = 1;
    e->orbit.orbit = (OrbitAngle){ .angle = at, .speed = (uint8_t)step };
    e->orbit.hits_left = hits;
    e->orbit.launch = byte_counter(0x30, 0x30);
    e->orbit.hit_frames = -1;
    set_death_sound(e, SAMPLE_ID_BEAM, 0);
    e->hit_handler = HIT_COUNT_DOWN;
    game->orbit_next_phase = (int16_t)(game->orbit_next_phase + game->tuning.orbit_phase_step);
    game->enemies_alive++;
}

/* game86 1000:2974: the core; it resets orbit_angle and the enemy speed. */
static void enemy_init_15(GameState *game, Entity *e)
{
    int16_t phase = game->orbit_next_phase & 0xff;
    game->orbit_angle = 0x20000;
    game->player.enemy_speed_x = 1;
    game->player.enemy_speed_y = 1;
    init_orbiter(game, e, phase, SPRITE_ORBIT_CORE, 0x0a, 1);
    entity_set_type(e, THINK_ORBIT_CORE, THINK_ORBIT_CORE);
}

/* game86 1000:26a0: a satellite, firing at the ships. */
static void enemy_init_16(GameState *game, Entity *e)
{
    init_orbiter(game, e, 0, SPRITE_SATELLITE, 0x14, 1);
    e->orbit.fire = byte_counter(0x0f, 0x0f);
    entity_set_type(e, THINK_SATELLITE, THINK_SATELLITE);
}

/* ---- 17: the runner -------------------------------------------------------------------- */

/* game86 1000:27e7: starts along x at a random speed between 1 and 3 pixels per tick, with a
 * random deceleration. */
static void enemy_init_17(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    e->anim = ANIM_PLAIN;
    stand_still(e);
    e->scale = 0;
    e->solid = 1;
    e->sprite = SPRITE_RUNNER;
    EdgeRunnerView *r = &e->edge_runner;
    r->hits_left = 1;
    r->backwards = rng_next_byte(&game->rng) & 1;
    r->axis = 0;
    r->position_x = (Fixed16){ .frac = 0, .whole = e->x };
    r->position_y = (Fixed16){ .frac = 0, .whole = e->y };
    r->speed.whole = (int16_t)((rng_next_byte(&game->rng) & 1) + 1);
    r->speed.frac = (uint16_t)(rng_next_byte(&game->rng) << 8);
    r->decel = (int16_t)((rng_next_byte(&game->rng) + 1) << 4);
    e->hit_handler = HIT_COUNT_DOWN;
    set_death_sound(e, SAMPLE_ID_DROP, 4);
    entity_set_type(e, THINK_RUNNER, THINK_RUNNER);
    game->enemies_alive++;
}

/* ---- 18, 26: herders that do something else to the llamas ------------------------------ */

static void init_herder(GameState *game, Entity *e, uint8_t death_rate, int16_t sprite,
                        ByteCounter step, int16_t mode)
{
    enemy_random_spawn_pos(game, e);
    set_death_sound(e, SAMPLE_ID_ENEMY_DEATH, death_rate);
    e->anim = ANIM_PLAIN;
    e->solid = 1;
    entity_set_type(e, THINK_HERDER, THINK_HERDER);
    stand_still(e);
    e->sprite = sprite;
    e->scale = 0;
    e->herder.target = HERDER_SEEK;
    e->herder.unused_16 = 0;
    e->herder.hp = 4;
    e->herder.step = step;
    e->herder.kill_action = KILL_PLAIN;
    e->herder.mode = mode;
    e->herder.sprite_base = e->sprite;
    e->herder.spawn = counter_from_word(game->player.script_param[PARAM_HERDER_SPAWN]);
    e->hit_handler = HIT_LOSE_HP;
    game->enemies_alive++;
}

/* game86 1000:228b: the llamas it reaches burst into four. */
static void enemy_init_18(GameState *game, Entity *e)
{
    init_herder(game, e, 0, SPRITE_BURSTING_HERDER, byte_counter(2, 2), HERDER_BURSTS_LLAMA);
}

/* game86 1000:22e9: the llamas it reaches arm a deadly pickup. */
static void enemy_init_26(GameState *game, Entity *e)
{
    init_herder(game, e, 3, SPRITE_ARMING_HERDER, byte_counter(4, 4), HERDER_ARMS_PICKUP);
}

/* ---- 19: the walker -------------------------------------------------------------------- */

/* game86 1000:351e: firing (or laying seekers) every walker.fire period. */
static void enemy_init_19(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    set_death_sound(e, SAMPLE_ID_GAME_OVER, 0);
    e->anim = ANIM_PLAIN;
    stand_still(e);
    e->sprite = SPRITE_WALKER;
    e->scale = 0;
    e->solid = 1;
    int32_t accel_x = rng_next_byte(&game->rng) | 0x300;
    int32_t accel_y = rng_next_byte(&game->rng) | 0x600;
    homing_at_rest(e, accel_x, accel_y);
    e->homing.hits = byte_counter(0x18, 0x18);
    e->homing.start_delay = 0;
    e->homing.unused_2e = 0;
    e->walker.fire = counter_from_word(game->tuning.init19_vx);
    e->hit_handler = HIT_WALKER;
    entity_set_type(e, THINK_WALKER, THINK_WALKER);
    game->enemies_alive++;
}

/* ---- 1a, 27: seekers (think 24) -------------------------------------------------------- */

/* game86 1000:33e3: the slow seeker, turning every 0x20 ticks (also what the seeker
 * chargers spawn, SPAWN_AS_TYPE_1A). */
void enemy_setup_1a(GameState *game, Entity *e)
{
    e->sprite = SPRITE_SLOW_SEEKER;
    e->anim = ANIM_PLAIN;
    stand_still(e);
    e->scale = 0;
    e->solid = 1;
    e->seeker.unused_12_14[1] = 0;
    e->seeker.direction = 0;
    e->seeker.turn = byte_counter(0x20, 1);
    e->seeker.sprite_base = e->sprite;
    set_death_sound(e, SAMPLE_ID_ALERT, 3);
    e->hit_handler = HIT_KILL;
    entity_set_type(e, THINK_SEEKER, THINK_SEEKER);
    game->enemies_alive++;
}

/* game86 1000:33e0 */
static void enemy_init_1a(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    enemy_setup_1a(game, e);
}

/* game86 1000:3441 */
void enemy_setup_seeker(GameState *game, Entity *e, int16_t phase, int16_t turn)
{
    e->sprite = (int16_t)(SPRITE_SEEKER + phase);
    e->anim = ANIM_PLAIN;
    stand_still(e);
    e->scale = 0;
    e->solid = 1;
    e->seeker.unused_12_14[1] = 0;
    e->seeker.direction = phase;
    e->seeker.turn = counter_from_word(turn);
    e->seeker.sprite_base = SPRITE_SEEKER;
    set_death_sound(e, SAMPLE_ID_DEATH_24, 4);
    e->hit_handler = HIT_KILL;
    entity_set_type(e, THINK_SEEKER, THINK_SEEKER);
    game->enemies_alive++;
}

/* game86 1000:342c: a seeker facing a random direction. */
static void enemy_init_27(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    int16_t phase = rng_next_byte(&game->rng) & 7;
    enemy_setup_seeker(game, e, phase, SEEKER_TURN);
}

/* ---- 1b, 1e, 20: chargers (think 25), each spawning its own kind ----------------------- */

/* game86 1000:3199. `callback` (SPAWN_AS_*) is what its spawns become. */
static void init_charger(GameState *game, Entity *e, int16_t sprite, ByteCounter charge,
                         ByteCounter spawn, int16_t callback, ByteCounter move)
{
    enemy_random_spawn_pos(game, e);
    e->sprite = sprite;
    ChargerView *c = &e->charger;
    c->dash_reach = 0;
    c->charge = charge;
    c->spawn = spawn;
    c->callback = callback;
    c->move = move;
    e->anim = ANIM_PLAIN;
    stand_still(e);
    e->scale = 0;
    e->solid = 1;
    c->charging = 1;
    c->sprite_base = e->sprite;
    c->dash_frames = -1;
    set_death_sound(e, SAMPLE_ID_WARP, 3);
    e->hit_handler = HIT_CHARGER;
    entity_set_type(e, THINK_CHARGER, THINK_CHARGER);
    game->enemies_alive++;
}

/* game86 1000:3178: spawns slow seekers. */
static void enemy_init_1b(GameState *game, Entity *e)
{
    init_charger(game, e, SPRITE_SEEKER_CHARGER, byte_counter(0x10, 0x10), byte_counter(8, 8),
                 SPAWN_AS_TYPE_1A, byte_counter(0x70, 0x70));
}

/* game86 1000:3155: spawns pairs. */
static void enemy_init_1e(GameState *game, Entity *e)
{
    init_charger(game, e, SPRITE_PAIR_CHARGER, byte_counter(8, 8), byte_counter(7, 7),
                 SPAWN_AS_PAIR, byte_counter(0x80, 0x80));
}

/* game86 1000:30c3: spawns deadly pickups, with its own animation; a plain kill
 * (HIT_KILL), not only while charging. */
static void enemy_init_20(GameState *game, Entity *e)
{
    init_charger(game, e, SPRITE_PICKUP_CHARGER, byte_counter(2, 2), byte_counter(2, 2),
                 SPAWN_AS_DEADLY_PICKUP, byte_counter(0x30, 0x30));
    entity_set_type(e, THINK_CHARGER_ANIMATED, THINK_CHARGER_ANIMATED);
    e->hit_handler = HIT_KILL;
    set_death_sound(e, SAMPLE_ID_NONE, 2);
}

/* ---- 1d: the edge spawner -------------------------------------------------------------- */

/* game86 1000:2f53. Not solid. */
static void enemy_init_1d(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    e->sprite = SPRITE_EDGE_SPAWNER;
    e->anim = ANIM_PLAIN;
    e->vx = 1;
    e->vy = 2;
    e->scale = 0;
    e->solid = 0;
    e->edge_spawner.stopped = 0;
    e->edge_spawner.throw_child = byte_counter(0x10, 0x10);
    entity_set_type(e, THINK_EDGE_SPAWNER, THINK_EDGE_SPAWNER);
    game->enemies_alive++;
}

/* ---- 22..25: edge gunners (think 2a) --------------------------------------------------- */

typedef enum Edge { EDGE_TOP, EDGE_LEFT, EDGE_RIGHT, EDGE_BOTTOM } Edge;

/* game86 1000:2bb6 / 2c04 / 2c52 / 2c9f and their common tail 2cea. The gunner starts on one
 * edge moving inwards; it homes along that edge (across its run) at tuning.accum_start, with
 * random low bits, and not at all along its run. */
static void init_edge_gunner(GameState *game, Entity *e, Edge edge, int16_t sprite)
{
    enemy_random_spawn_pos(game, e);
    bool runs_vertically = edge == EDGE_TOP || edge == EDGE_BOTTOM;
    switch (edge) {
    case EDGE_TOP:    e->vy = 2;  e->vx = 0;  e->y = EDGE_TOP_Y;    break;
    case EDGE_LEFT:   e->vy = 0;  e->vx = 2;  e->x = EDGE_LEFT_X;   break;
    case EDGE_RIGHT:  e->vy = 0;  e->vx = -2; e->x = EDGE_RIGHT_X;  break;
    case EDGE_BOTTOM: e->vy = -2; e->vx = 0;  e->y = EDGE_BOTTOM_Y; break;
    }
    set_death_sound(e, SAMPLE_ID_POP, 3);
    e->anim = ANIM_PLAIN;
    e->sprite = sprite;
    int32_t accel_x = runs_vertically ? (int32_t)game->tuning.accum_start : 0;
    int32_t accel_y = runs_vertically ? 0 : (int32_t)game->tuning.accum_start;
    /* Random low bits for whichever fraction is not zero (accel_y's if both are). */
    uint8_t jitter = rng_next_byte(&game->rng);
    if ((uint16_t)accel_x != 0)
        accel_x |= jitter;
    else
        accel_y |= jitter;
    e->scale = 0;
    e->solid = 1;
    homing_at_rest(e, accel_x, accel_y);
    e->homing.gun_hits_left = game->tuning.runner_hits;
    e->homing.fire = counter_from_word(game->tuning.runner_fire);
    e->hit_handler = HIT_EDGE_GUNNER;
    entity_set_type(e, THINK_EDGE_GUNNER, THINK_EDGE_GUNNER);
    game->enemies_alive++;
}

/* game86 1000:2c9f */
static void enemy_init_22(GameState *game, Entity *e) { init_edge_gunner(game, e, EDGE_BOTTOM, SPRITE_EDGE_GUNNER_BOTTOM); }
/* game86 1000:2bb6 */
static void enemy_init_23(GameState *game, Entity *e) { init_edge_gunner(game, e, EDGE_TOP, SPRITE_EDGE_GUNNER_TOP); }
/* game86 1000:2c04 */
static void enemy_init_24(GameState *game, Entity *e) { init_edge_gunner(game, e, EDGE_LEFT, SPRITE_EDGE_GUNNER_LEFT); }
/* game86 1000:2c52 */
static void enemy_init_25(GameState *game, Entity *e) { init_edge_gunner(game, e, EDGE_RIGHT, SPRITE_EDGE_GUNNER_RIGHT); }

/* ---- 28: the storm centre -------------------------------------------------------------- */

/* game86 1000:26c5: in the middle of the field and not drawn; once its hits_left runs out
 * it turns into the storm (HIT_STORM_CENTRE). */
static void enemy_init_28(GameState *game, Entity *e)
{
    e->anim = ANIM_HIDDEN;
    stand_still(e);
    e->sprite = SPRITE_STORM_CENTRE;
    e->scale = 0;
    set_death_sound(e, SAMPLE_ID_GAME_OVER, 4);
    e->hit_handler = HIT_STORM_CENTRE;
    e->solid = 1;
    e->storm.unused_14 = 0x2828;
    e->storm.hits_left = 0x32;
    e->x = FIELD_CENTRE_X;
    e->y = STORM_CENTRE_Y;
    entity_set_type(e, THINK_STORM_CENTRE, THINK_STORM_CENTRE);
    game->enemies_alive++;
}

/* DS:2CA4 */
const EnemyInitFn enemy_init_handlers[ENEMY_INIT_COUNT] = {
    enemy_init_00, enemy_init_01, enemy_init_02, enemy_init_03, enemy_init_04, enemy_init_05,
    enemy_init_06, enemy_init_07, enemy_init_08, enemy_init_09, enemy_init_0a, enemy_init_0b,
    enemy_init_0c, enemy_init_0d, enemy_init_0e, enemy_init_0f, enemy_init_10, enemy_init_11,
    enemy_init_12, enemy_init_13, enemy_init_14, enemy_init_15, enemy_init_16, enemy_init_17,
    enemy_init_18, enemy_init_19, enemy_init_1a, enemy_init_1b, enemy_init_1c, enemy_init_1d,
    enemy_init_1e, enemy_init_1f, enemy_init_20, enemy_init_21, enemy_init_22, enemy_init_23,
    enemy_init_24, enemy_init_25, enemy_init_26, enemy_init_27, enemy_init_28,
};
