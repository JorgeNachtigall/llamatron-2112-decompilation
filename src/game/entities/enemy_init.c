/*
 * enemy_init.c - the 41 wave-spawn init handlers (tbl_enemy_init, DS:2CA4) and the init bodies
 * other handlers reuse.
 *
 * The spawn loop (W4, game86 1000:1d97..1dcb) has already taken the slot, cleared its overlay,
 * set its roster kind and counted it off free_slots; an init handler places the record and
 * gives it its type, sprite and handler-specific parameters. Fields an init does not write
 * keep whatever the slot held before, as in the original.
 *
 * Names are by table index until the enemies are identified on screen.
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
    SPRITE_TYPE_02 = 0x2b,
    SPRITE_TYPE_04 = 0x6f,
    SPRITE_SHOT_SLOW = 0x7b,            /* 05; also the speed-0x200 enemy shot */
    SPRITE_SHOT_MEDIUM = 0x7f,          /* 06; ent_hit_04's fire-back */
    SPRITE_SHOT_FAST = 0x83,            /* 07; ent_hit_05's fire-back */
    SPRITE_DRIFTER = 0x87,              /* 08 */
    SPRITE_DRIFTER_ALT = 0x88,          /* 09 */
    SPRITE_HOMING_DROPPER = 0x89,       /* 0a: think_13, + its 8-frame cycle */
    SPRITE_TYPE_0B = 0x95,
    SPRITE_TYPE_0C = 0x99,
    SPRITE_TYPE_0D = 0xa3,
    SPRITE_TYPE_0E = 0xaf,
    SPRITE_TYPE_0F = 0xb2,
    SPRITE_SNAKE_SEGMENT = 0xc6,        /* 10 */
    SPRITE_SNAKE_HEAD = 0xcc,
    SPRITE_SEGMENT_11 = 0xce,
    SPRITE_SEGMENT_12 = 0xd2,
    SPRITE_SEGMENT_13 = 0xd6,
    SPRITE_SEGMENT_14 = 0xda,
    SPRITE_SEGMENT_1F = 0x118,
    SPRITE_TYPE_17 = 0xe7,
    SPRITE_TYPE_18 = 0x4f,
    SPRITE_TYPE_26 = 0x12b,
    SPRITE_WALKER = 0xfc,               /* 19: think_23 */
    SPRITE_TYPE_1A = 0x104,
    SPRITE_SEEKER = 0x136,              /* type 0x24 (think_24), + its direction phase 0..7 */
    SPRITE_TYPE_1B = 0x10c,
    SPRITE_TYPE_1E = 0x113,
    SPRITE_TYPE_20 = 0x11c,
    SPRITE_DROPPER = 0x10f,             /* 1c: think_26 */
    SPRITE_EDGE_SPAWNER = 0x110,        /* 1d: think_27 */
    SPRITE_EDGE_RUNNER_BOTTOM = 0x126,  /* 22..25: think_2a */
    SPRITE_EDGE_RUNNER_RIGHT = 0x127,
    SPRITE_EDGE_RUNNER_TOP = 0x128,
    SPRITE_EDGE_RUNNER_LEFT = 0x129,
    SPRITE_TYPE_28 = 0x146,
};

/* Seeker hit points: two byte counters, as enemy_init_27 starts it (the pieces
 * split_into_type24 makes start at SEEKER_HP_PIECE, kill_actions.c). */
enum { SEEKER_HP = 0x104 };

static void set_death_sound(Entity *e, uint8_t sample, uint8_t rate)
{
    e->death_sample = sample;
    e->death_rate = rate;
}

static void stand_still(Entity *e)
{
    e->vx = 0;
    e->vy = 0;
}

/* The +22/+23 byte pair written as one word, as several inits do. */
static void set_reload_pair(Entity *e, int16_t value)
{
    e->reload = (uint8_t)(value & 0xff);
    e->countdown = (uint8_t)((uint16_t)value >> 8);
}

/* A random step of -1, 0, 1 or 2 (rng & 3, minus 1). */
static int16_t random_step(GameState *game)
{
    return (int16_t)((rng_next_byte(&game->rng) & 3) - 1);
}

/* Remembers the spawn position in +28 (x) and +2c (y) and clears +26 / +2a, the "home"
 * fields of the drifting types. */
static void remember_home(Entity *e)
{
    e->anim_toggle = e->x;
    e->aux26 = 0;
    e->sprite_ref = e->y;
    e->aux2a = 0;
}

/* ---- 00: egg that hatches into think 05 ------------------------------------------------ */

/* game86 1000:1dd0 */
void enemy_setup_00(GameState *game, Entity *e, bool hatch_fast)
{
    e->anim = 0x0f;
    stand_still(e);
    e->sprite = game->spawn_sprite;
    e->scale = hatch_fast ? 2 : 0x32;      /* hatch countdown */
    e->solid = 0;
    e->hp = 0;
    e->aux1a = 0;
    e->timer = 0;
    e->aux16 = 0;
    e->hit_handler = 0;
    set_death_sound(e, SAMPLE_ID_ENEMY_DEATH, 0);
    e->aux30 = game->spawn_sprite;
    entity_set_type(e, 0x04, 0x05);
    game->unhatched++;
    game->eggs_spawned++;
}

/* game86 1000:1dcd */
static void enemy_init_00(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    enemy_setup_00(game, e, game->player.wave_type != 0);
}

/* ---- 01: the llama herd (think 03) ----------------------------------------------------- */

/* game86 1000:1edf */
void enemy_setup_01(GameState *game, Entity *e)
{
    e->anim = 0x01;
    stand_still(e);
    e->sprite = -1;                         /* drawn from sprite_ref */
    e->scale = 0;
    e->solid = 1;
    game->spawn_phase++;
    e->timer = game->spawn_phase & 0x1f;
    e->hp = (int16_t)(game->player.script_param[1] + game->player.level_loops);
    e->kill_action = game->player.script_param[3];
    e->sub_mode = game->tuning.init01_mode;
    e->hit_handler = game->player.script_param[0];
    if (e->hit_handler == 0) {
        e->hit_handler = 3;
        e->kill_action = 0;
    }
    entity_set_type(e, 0x03, 0x03);
    set_death_sound(e, SAMPLE_ID_ENEMY_DEATH, 3);

    const SpriteRefTable *refs = asset_sprite_refs();
    unsigned ref = SPRITE_REF_LLAMA_BY_WAVE + 2u * (unsigned)game->player.wave +
                   (unsigned)(e->anim_toggle & 1);
    /* The original copies the table word, the frame's offset in the 218C bank. */
    if (refs && ref < refs->count && !(refs->refs[ref] & SPRITE_REF_LINK)) {
        const SpriteFrame *frame = asset_sprite_frame(SPRITE_BANK_GAME, refs->refs[ref]);
        if (frame)
            e->sprite_ref = (int16_t)frame->bank_offset;
    }
    game->enemies_alive++;
}

/* game86 1000:1edc */
static void enemy_init_01(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    enemy_setup_01(game, e);
}

/* ---- 02, 04: hatching spawns (think 07) ------------------------------------------------ */

/* game86 1000:1f61. Neither counted as alive nor given a death sound. */
static void enemy_init_02(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    e->anim = 0x0c;
    stand_still(e);
    e->sprite = SPRITE_TYPE_02;
    e->scale = 2;
    e->timer = -1;
    e->aux16 = 0;
    e->solid = 1;
    entity_set_type(e, 0x07, 0x08);
    e->hit_handler = 1;
}

/* game86 1000:1f96 */
static void enemy_init_04(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    set_death_sound(e, SAMPLE_ID_KNOCK, 0);
    e->anim = 0x0c;
    stand_still(e);
    e->sprite = SPRITE_TYPE_04;
    e->scale = 2;
    e->solid = 1;
    entity_set_type(e, 0x07, 0x0e);
    e->timer = -2;
    e->aux16 = 0;
    e->hp = 1;
    e->aux1a = game->player.level_loops != 0 ? 0x202 : 0x505;
    e->aux1c = e->sprite;
    e->kill_action = 1;
    e->hit_handler = 3;
    game->enemies_alive++;
}

/* ---- 03, 21: hatching spawns (think 0a) ------------------------------------------------ */

static void init_think0a_spawn(GameState *game, Entity *e, int16_t sprite, int16_t aux16,
                               int16_t hp, int16_t aux1a)
{
    enemy_random_spawn_pos(game, e);
    e->anim = 0x0c;
    e->vx = random_step(game);
    e->vy = random_step(game);
    e->sprite = sprite;
    e->aux1c = sprite;
    e->scale = 0x32;
    e->solid = 0;
    e->timer = 0x64;
    e->aux16 = aux16;
    e->hp = hp;
    e->aux1a = aux1a;
    e->hit_handler = 2;
    set_death_sound(e, SAMPLE_ID_LETTER_LANDS, 0);
    entity_set_type(e, 0x0a, 0x0b);
    game->enemies_alive++;
}

/* game86 1000:1e2c */
static void enemy_init_03(GameState *game, Entity *e)
{
    init_think0a_spawn(game, e, 0x3c, 0x12c, 0x60, 3);
}

/* game86 1000:1e84 */
static void enemy_init_21(GameState *game, Entity *e)
{
    init_think0a_spawn(game, e, 0x122, 0x1e, 0x30, 0x14);
}

/* ---- 05, 06, 07: projectiles placed at random ------------------------------------------ */

/* game86 1000:24d6: the record becomes a projectile fired from itself. */
static void init_as_projectile(GameState *game, Entity *e, int16_t sprite, int16_t speed,
                               int16_t hit_handler)
{
    enemy_random_spawn_pos(game, e);
    const EnemyShot shot = { .aim_y = 0, .aim_x = 0, .sprite = sprite, .speed = speed,
                             .hit_handler = hit_handler };
    stand_still(e);                         /* enemy_shot_init's write through SI (= the record) */
    enemy_shot_init(game, e, e, &shot);
    e->aux30 = 0;
}

/* game86 1000:24c1 */
static void enemy_init_05(GameState *game, Entity *e)
{
    init_as_projectile(game, e, SPRITE_SHOT_SLOW, 0x200, 4);
}

/* game86 1000:2493 */
static void enemy_init_06(GameState *game, Entity *e)
{
    init_as_projectile(game, e, SPRITE_SHOT_MEDIUM, 0x400, 5);
}

/* game86 1000:24aa */
static void enemy_init_07(GameState *game, Entity *e)
{
    init_as_projectile(game, e, SPRITE_SHOT_FAST, 0x600, 0);
}

/* ---- 08, 09: think 11 / 12 drifters ---------------------------------------------------- */

static void init_drifter(GameState *game, Entity *e, int16_t anim, int16_t sprite, int16_t speed)
{
    enemy_random_spawn_pos(game, e);
    set_death_sound(e, SAMPLE_ID_SHOT_DEATH, 0);
    e->anim = anim;
    stand_still(e);
    e->scale = 2;
    e->solid = 1;
    e->sprite = sprite;
    e->timer = 0;
    e->aux16 = 0;
    e->hp = 0;
    e->aux1a = 0;
    e->aux1c = speed;
    e->kill_action = 0;
    e->sub_mode = speed;
    set_reload_pair(e, 0);
    e->aux24 = 0x20;
    e->anim_toggle = e->x;
    e->aux26 = 0;
    e->aux30 = 0;
    e->sprite_ref = e->y;
    e->aux2a = 0;
}

/* game86 1000:2612. Not counted as alive. */
static void enemy_init_08(GameState *game, Entity *e)
{
    init_drifter(game, e, 0x02, SPRITE_DRIFTER, game->player.level_loops != 0 ? 0x800 : 0x400);
    e->aux2e = 0x7070;
    e->hit_handler = 6;
    entity_set_type(e, 0x11, 0x11);
}

/* game86 1000:3736. Not counted as alive; draws a random byte it never uses (1000:37a4). */
static void enemy_init_09(GameState *game, Entity *e)
{
    init_drifter(game, e, 0x03, SPRITE_DRIFTER_ALT, 0x400);
    (void)rng_next_byte(&game->rng);
    e->aux2e = 0x7070;
    e->hit_handler = 6;
    entity_set_type(e, 0x12, 0x12);
}

/* ---- 0a .. 0e -------------------------------------------------------------------------- */

/* game86 1000:24ef */
static void enemy_init_0a(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    set_death_sound(e, SAMPLE_ID_NONE, 0);
    e->anim = 0x0c;
    stand_still(e);
    e->sprite = SPRITE_HOMING_DROPPER;
    e->scale = 0x14;
    e->solid = 1;
    e->timer = 0;
    e->aux16 = 0;
    e->hp = 0;
    e->aux1a = 0;
    int16_t accum_low = (int16_t)(game->speed_accum & 0xffff);
    int16_t accum_high = (int16_t)(game->speed_accum >> 16);
    e->aux1c = accum_low;
    e->sub_mode = accum_low;
    e->kill_action = accum_high;
    set_reload_pair(e, accum_high);
    e->aux24 = 0x4040;
    e->anim_toggle = e->x;
    e->aux26 = 0;
    e->aux30 = 0x14;
    e->sprite_ref = e->y;
    e->aux2a = 0;
    e->aux2e = 7;
    e->hit_handler = 0;
    entity_set_type(e, 0x07, 0x13);
    game->enemies_alive++;
}

/* game86 1000:25ae */
static void enemy_init_0b(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    set_death_sound(e, SAMPLE_ID_DEATH_0B, 0);
    e->anim = 0x0d;
    e->vx = random_step(game);
    e->vy = random_step(game);
    e->sprite = SPRITE_TYPE_0B;
    e->scale = 2;
    e->solid = 1;
    e->hp = 4;
    e->hit_handler = 7;
    entity_set_type(e, 0x14, 0x14);
    game->enemies_alive++;
}

/* game86 1000:2407 */
void enemy_setup_0c(GameState *game, Entity *e)
{
    set_death_sound(e, SAMPLE_ID_POP, 0);
    e->anim = 0x0c;
    stand_still(e);
    e->scale = 8;
    e->solid = 1;
    e->sprite = SPRITE_TYPE_0C;
    entity_set_type(e, 0x07, 0x15);
    e->timer = -2;
    e->aux16 = 0;
    e->hp = 0;
    e->aux1a = 0x101;
    e->aux1c = e->sprite;
    e->kill_action = 2;
    e->hit_handler = 3;
    game->enemies_alive++;
}

/* game86 1000:2404 */
static void enemy_init_0c(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    enemy_setup_0c(game, e);
}

/* game86 1000:2347 */
static void enemy_init_0d(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    set_death_sound(e, SAMPLE_ID_ENEMY_DEATH, 0);
    e->anim = 0x0c;
    stand_still(e);
    e->sprite = SPRITE_TYPE_0D;
    e->scale = 0x28;
    e->solid = 1;
    entity_set_type(e, 0x07, 0x16);
    e->timer = -1;
    e->aux16 = 0;
    e->hp = game->tuning.init0d_hp;
    e->aux1a = game->player.script_param[6];
    e->aux1c = e->sprite;
    e->kill_action = 0;
    e->sub_mode = 1;
    set_reload_pair(e, game->player.script_param[5]);
    e->hit_handler = 3;
    game->enemies_alive++;
}

/* game86 1000:21af: the spawn position is copied into four position histories. */
void enemy_setup_0e(GameState *game, Entity *e)
{
    set_death_sound(e, SAMPLE_ID_ENEMY_DEATH, 0);
    e->anim = 0x0d;
    stand_still(e);
    e->sprite = SPRITE_TYPE_0E;
    e->scale = 0;
    e->solid = 1;
    e->aux12 = 0x0aff;
    e->timer = 0;
    e->aux16 = e->x;
    e->aux1a = e->x;
    e->kill_action = e->x;
    set_reload_pair(e, e->x);
    e->hp = e->y;
    e->aux1c = e->y;
    e->sub_mode = e->y;
    e->aux24 = e->y;
    entity_set_type(e, 0x17, 0x17);
    e->hit_handler = 0;
    game->enemies_alive++;
}

/* game86 1000:21ac */
static void enemy_init_0e(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    enemy_setup_0e(game, e);
}

/* ---- 0f, 1c: fixed start at the top centre --------------------------------------------- */

/* game86 1000:1ff5 */
static void enemy_init_0f(GameState *game, Entity *e)
{
    e->x = 0xa0;
    e->y = 0x1e;
    set_death_sound(e, SAMPLE_ID_ENEMY_DEATH, 0);
    e->anim = 0x0d;
    e->vx = 0;
    e->vy = 1;
    e->sprite = SPRITE_TYPE_0F;
    e->scale = 0;
    e->solid = 1;
    e->timer = 1;
    e->hp = 0x64;
    entity_set_type(e, 0x18, 0x18);
    e->kill_action = 3;
    e->hit_handler = 3;
    game->enemies_alive++;
}

/* game86 1000:2f0d. Not solid and not counted as alive. */
static void enemy_init_1c(GameState *game, Entity *e)
{
    (void)game;
    e->x = 0xa0;
    e->y = 0x1e;
    e->sprite = SPRITE_DROPPER;
    e->anim = 0x0d;
    stand_still(e);
    e->scale = 0;
    e->solid = 0;
    e->aux16 = 0x1e;
    e->timer = 0;
    e->hp = 0;
    e->aux1a = 0;
    e->aux1c = 2;
    entity_set_type(e, 0x26, 0x26);
}

/* ---- 10: segmented snake --------------------------------------------------------------- */

enum { SNAKE_SEGMENTS = 8 };

/* Next free slot at or after `from`, or NULL. */
static Entity *free_slot_from(GameState *game, Entity *from)
{
    for (Entity *s = from; s < game->entities + ENTITY_COUNT; s++)
        if (entity_is_free(s))
            return s;
    return NULL;
}

/* game86 1000:2def. The slot search starts at the spawn's own record, which is still free,
 * so the first segment takes it and the head goes into a later slot. The original searches
 * on past the table when it runs out of free slots; the port stops at the end of the table. */
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
        s->anim = 0x0d;
        s->sprite = SPRITE_SNAKE_SEGMENT;
        s->scale = 0;
        s->solid = 1;
        s->kind = -1;
        s->timer = delay;
        s->aux16 = -4;
        s->hp = -1;
        s->hit_handler = 9;
        entity_set_type(s, 0x1e, 0x1e);
        s++;
    }

    Entity *head = free_slot_from(game, s);
    if (!head)
        return;
    head->x = at.x;
    head->y = at.y;
    set_death_sound(head, SAMPLE_ID_POP, 0);
    head->anim = 0x0d;
    head->sprite = SPRITE_SNAKE_HEAD;
    head->scale = 2;
    head->solid = 1;
    head->timer = 0;
    head->aux16 = 0;
    head->hp = 0;
    head->aux1a = 0;
    head->aux1c = 0x800;
    head->kill_action = 0;
    head->sub_mode = 0x800;
    set_reload_pair(head, 0);
    head->aux24 = 0x404;
    head->anim_toggle = head->x;
    head->aux26 = 0;
    head->aux30 = 0;
    head->sprite_ref = head->y;
    head->aux2a = 0;
    head->aux2e = 0;
    head->vx = 0;
    head->vy = 0x3030;
    head->hit_handler = 8;
    head->kind = 0x10;
    entity_set_type(head, 0x1d, 0x1d);
    game->enemies_alive++;

    for (int i = 0; i < SNAKE_TRAIL_LENGTH; i++) {
        game->snake_trail[i].x = head->x;
        game->snake_trail[i].y = head->y;
        game->snake_trail[i].sprite = head->sprite;
    }
}

/* ---- 11..14, 1f: segment types (think 1f) ---------------------------------------------- */

/* game86 1000:2b2d */
static void init_segment(GameState *game, Entity *e, int16_t sprite)
{
    game->segment_sprite = sprite;
    enemy_random_spawn_pos(game, e);
    set_death_sound(e, SAMPLE_ID_POP, 0);
    e->sprite = game->segment_sprite;
    e->scale = 0;
    e->solid = 1;
    e->timer = 0;
    e->aux16 = 0;
    e->hp = 0;
    e->aux1a = 0;
    e->aux1c = 0;
    e->kill_action = 0;
    e->sub_mode = 0;
    set_reload_pair(e, 0);
    e->aux24 = -1;
    e->aux30 = 0;
    remember_home(e);
    e->aux2e = game->tuning.segment_family;
    e->hit_handler = 0x0a;
    e->anim = 0x0d;
    e->vy = 0;
    e->vx = e->sprite;
    entity_set_type(e, 0x1f, 0x1f);
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

/* game86 1000:2b01 */
static void enemy_init_1f(GameState *game, Entity *e)
{
    init_segment(game, e, SPRITE_SEGMENT_1F);
    entity_set_type(e, 0x28, 0x28);
}

/* ---- 15, 16: orbiters placed on a circle around the centre ----------------------------- */

/* game86 1000:29af: position on the circle of radius 63 around (0xa0, 0x6e) at sine phase
 * `phase`; the timer word holds the phase (low byte) and its step (high byte). */
static void init_orbiter(GameState *game, Entity *e, int16_t phase, int16_t sprite, int16_t aux16,
                         int16_t step)
{
    const SineTables *sine = asset_sine();
    uint8_t at = (uint8_t)phase;
    int8_t sin_x = sine ? sine->sine8[at] : 0;
    int8_t sin_y = sine ? sine->sine8[(uint8_t)(at + 0x40)] : 0;
    e->x = (int16_t)((sin_x >> 1) + 0xa0);
    e->y = (int16_t)((sin_y >> 1) + 0x6e);
    e->anim = 0x0d;
    stand_still(e);
    e->sprite = sprite;
    e->scale = 0;
    e->solid = 1;
    e->timer = (int16_t)((uint16_t)(step & 0xff) << 8 | at);
    e->aux16 = aux16;
    e->hp = 0x3030;
    e->aux1a = -1;
    set_death_sound(e, SAMPLE_ID_BEAM, 0);
    e->hit_handler = 0x0b;
    game->orbit_next_phase = (int16_t)(game->orbit_next_phase + game->tuning.orbit_phase_step);
    game->enemies_alive++;
}

/* game86 1000:2974 */
static void enemy_init_15(GameState *game, Entity *e)
{
    int16_t phase = game->orbit_next_phase & 0xff;
    game->orbit_angle = 0x20000;
    game->player.enemy_speed_x = 1;
    game->player.enemy_speed_y = 1;
    init_orbiter(game, e, phase, 0xde, 0x0a, 1);
    entity_set_type(e, 0x20, 0x20);
}

/* game86 1000:26a0 */
static void enemy_init_16(GameState *game, Entity *e)
{
    init_orbiter(game, e, 0, 0xe0, 0x14, 1);
    e->hp = 0x0f0f;
    entity_set_type(e, 0x21, 0x21);
}

/* ---- 17 -------------------------------------------------------------------------------- */

/* game86 1000:27e7 */
static void enemy_init_17(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    e->anim = 0x0d;
    stand_still(e);
    e->scale = 0;
    e->solid = 1;
    e->sprite = SPRITE_TYPE_17;
    e->aux16 = 1;
    e->hp = rng_next_byte(&game->rng) & 1;
    e->timer = 0;
    e->kill_action = e->x;                  /* x and y as 16.16 positions, fraction 0 */
    e->aux1c = 0;
    set_reload_pair(e, e->y);
    e->sub_mode = 0;
    e->aux26 = (int16_t)((rng_next_byte(&game->rng) & 1) + 1);
    e->aux24 = (int16_t)((uint16_t)rng_next_byte(&game->rng) << 8);
    e->anim_toggle = (int16_t)((rng_next_byte(&game->rng) + 1) << 4);
    e->hit_handler = 0x0b;
    set_death_sound(e, SAMPLE_ID_DROP, 4);
    entity_set_type(e, 0x22, 0x22);
    game->enemies_alive++;
}

/* ---- 18, 26: think 0e types ------------------------------------------------------------ */

static void init_think0e(GameState *game, Entity *e, uint8_t death_rate, int16_t sprite,
                         int16_t aux1a, int16_t mode)
{
    enemy_random_spawn_pos(game, e);
    set_death_sound(e, SAMPLE_ID_ENEMY_DEATH, death_rate);
    e->anim = 0x0d;
    e->solid = 1;
    entity_set_type(e, 0x0e, 0x0e);
    stand_still(e);
    e->sprite = sprite;
    e->scale = 0;
    e->timer = -1;
    e->aux16 = 0;
    e->hp = 4;
    e->aux1a = aux1a;
    e->kill_action = 0;
    e->sub_mode = mode;
    e->aux1c = e->sprite;
    set_reload_pair(e, game->player.script_param[5]);
    e->hit_handler = 3;
    game->enemies_alive++;
}

/* game86 1000:228b */
static void enemy_init_18(GameState *game, Entity *e)
{
    init_think0e(game, e, 0, SPRITE_TYPE_18, 0x202, 2);
}

/* game86 1000:22e9 */
static void enemy_init_26(GameState *game, Entity *e)
{
    init_think0e(game, e, 3, SPRITE_TYPE_26, 0x404, 3);
}

/* ---- 19 -------------------------------------------------------------------------------- */

/* game86 1000:351e */
static void enemy_init_19(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    set_death_sound(e, SAMPLE_ID_GAME_OVER, 0);
    e->anim = 0x0d;
    stand_still(e);
    e->sprite = SPRITE_WALKER;
    e->scale = 0;
    e->solid = 1;
    e->timer = 0;
    e->aux16 = 0;
    e->hp = 0;
    e->aux1a = 0;
    e->aux1c = (int16_t)(rng_next_byte(&game->rng) | 0x300);
    e->kill_action = 0;
    e->sub_mode = (int16_t)(rng_next_byte(&game->rng) | 0x600);
    set_reload_pair(e, 0);
    e->aux24 = 0x1818;
    e->aux30 = 0;
    remember_home(e);
    e->aux2e = 0;
    e->vx = game->tuning.init19_vx;
    e->hit_handler = 0x0c;
    entity_set_type(e, 0x23, 0x23);
    game->enemies_alive++;
}

/* ---- 1a, 27: type 0x24 ----------------------------------------------------------------- */

/* game86 1000:33e3 (also think_25's SPAWN_AS_TYPE_1A) */
void enemy_setup_1a(GameState *game, Entity *e)
{
    e->sprite = SPRITE_TYPE_1A;
    e->anim = 0x0d;
    stand_still(e);
    e->scale = 0;
    e->solid = 1;
    e->timer = 0;
    e->aux16 = 0;
    e->hp = 0x120;
    e->aux1a = e->sprite;
    set_death_sound(e, SAMPLE_ID_ALERT, 3);
    e->hit_handler = 0;
    entity_set_type(e, 0x24, 0x24);
    game->enemies_alive++;
}

/* game86 1000:33e0 */
static void enemy_init_1a(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    enemy_setup_1a(game, e);
}

/* game86 1000:3441 */
void enemy_setup_type24(GameState *game, Entity *e, int16_t phase, int16_t hp)
{
    e->sprite = (int16_t)(SPRITE_SEEKER + phase);
    e->anim = 0x0d;
    stand_still(e);
    e->scale = 0;
    e->solid = 1;
    e->timer = 0;
    e->aux16 = phase;
    e->hp = hp;
    e->aux1a = SPRITE_SEEKER;
    set_death_sound(e, SAMPLE_ID_DEATH_24, 4);
    e->hit_handler = 0;
    entity_set_type(e, 0x24, 0x24);
    game->enemies_alive++;
}

/* game86 1000:342c */
static void enemy_init_27(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    int16_t phase = rng_next_byte(&game->rng) & 7;
    enemy_setup_type24(game, e, phase, SEEKER_HP);
}

/* ---- 1b, 1e, 20: think 25 types with a callback ---------------------------------------- */

/* game86 1000:3199 */
static void init_think25(GameState *game, Entity *e, int16_t sprite, int16_t hp, int16_t aux1a,
                         int16_t callback, int16_t reload_pair)
{
    enemy_random_spawn_pos(game, e);
    e->sprite = sprite;
    e->aux16 = 0;
    e->hp = hp;
    e->aux1a = aux1a;
    e->aux1c = callback;
    set_reload_pair(e, reload_pair);
    e->anim = 0x0d;
    stand_still(e);
    e->scale = 0;
    e->solid = 1;
    e->timer = 1;
    e->sub_mode = e->sprite;
    e->aux24 = -1;
    set_death_sound(e, SAMPLE_ID_WARP, 3);
    e->hit_handler = 0x0d;
    entity_set_type(e, 0x25, 0x25);
    game->enemies_alive++;
}

/* game86 1000:3178 */
static void enemy_init_1b(GameState *game, Entity *e)
{
    init_think25(game, e, SPRITE_TYPE_1B, 0x1010, 0x808, SPAWN_AS_TYPE_1A, 0x7070);
}

/* game86 1000:3155 */
static void enemy_init_1e(GameState *game, Entity *e)
{
    init_think25(game, e, SPRITE_TYPE_1E, 0x808, 0x707, SPAWN_AS_PAIR, (int16_t)0x8080);
}

/* game86 1000:30c3 */
static void enemy_init_20(GameState *game, Entity *e)
{
    init_think25(game, e, SPRITE_TYPE_20, 0x202, 0x202, SPAWN_AS_DEADLY_PICKUP, 0x3030);
    entity_set_type(e, 0x29, 0x29);
    e->hit_handler = 0;
    set_death_sound(e, SAMPLE_ID_NONE, 2);
}

/* ---- 1d -------------------------------------------------------------------------------- */

/* game86 1000:2f53. Not solid. */
static void enemy_init_1d(GameState *game, Entity *e)
{
    enemy_random_spawn_pos(game, e);
    e->sprite = SPRITE_EDGE_SPAWNER;
    e->anim = 0x0d;
    e->vx = 1;
    e->vy = 2;
    e->scale = 0;
    e->solid = 0;
    e->timer = 0;
    e->aux16 = 0x1010;
    entity_set_type(e, 0x27, 0x27);
    game->enemies_alive++;
}

/* ---- 22..25: edge runners (think 2a) --------------------------------------------------- */

typedef enum Edge { EDGE_TOP, EDGE_LEFT, EDGE_RIGHT, EDGE_BOTTOM } Edge;

/* game86 1000:2bb6 / 2c04 / 2c52 / 2c9f and their common tail 2cea. The runner starts on one
 * edge moving inwards; its two 32-bit accumulators start at tuning.accum_start for the axis
 * across the run and 0 for the other, then the non-zero one gets random low bits. */
static void init_edge_runner(GameState *game, Entity *e, Edge edge, int16_t sprite)
{
    enemy_random_spawn_pos(game, e);
    int16_t accum_low = (int16_t)(game->tuning.accum_start & 0xffff);
    int16_t accum_high = (int16_t)(game->tuning.accum_start >> 16);
    bool runs_vertically = edge == EDGE_TOP || edge == EDGE_BOTTOM;
    switch (edge) {
    case EDGE_TOP:    e->vy = 2;  e->vx = 0;  e->y = 0x11;  break;
    case EDGE_LEFT:   e->vy = 0;  e->vx = 2;  e->x = 8;     break;
    case EDGE_RIGHT:  e->vy = 0;  e->vx = -2; e->x = 0x137; break;
    case EDGE_BOTTOM: e->vy = -2; e->vx = 0;  e->y = 0xbe;  break;
    }
    set_death_sound(e, SAMPLE_ID_POP, 3);
    e->anim = 0x0d;
    e->sprite = sprite;
    e->timer = 0;
    e->aux16 = 0;
    e->hp = 0;
    e->aux1a = 0;
    if (runs_vertically) {
        e->aux1c = accum_low;
        e->kill_action = accum_high;
        e->sub_mode = 0;
        set_reload_pair(e, 0);
    } else {
        e->sub_mode = accum_low;
        set_reload_pair(e, accum_high);
        e->aux1c = 0;
        e->kill_action = 0;
    }

    uint8_t jitter = rng_next_byte(&game->rng);
    if (e->aux1c != 0)
        e->aux1c = (int16_t)(e->aux1c | jitter);
    else
        e->sub_mode = (int16_t)(e->sub_mode | jitter);
    e->scale = 0;
    e->solid = 1;
    e->aux24 = game->tuning.runner_hits;
    remember_home(e);
    e->aux2e = game->tuning.runner_fire;
    e->hit_handler = 0x0f;
    entity_set_type(e, 0x2a, 0x2a);
    game->enemies_alive++;
}

/* game86 1000:2c9f */
static void enemy_init_22(GameState *game, Entity *e) { init_edge_runner(game, e, EDGE_BOTTOM, SPRITE_EDGE_RUNNER_BOTTOM); }
/* game86 1000:2bb6 */
static void enemy_init_23(GameState *game, Entity *e) { init_edge_runner(game, e, EDGE_TOP, SPRITE_EDGE_RUNNER_TOP); }
/* game86 1000:2c04 */
static void enemy_init_24(GameState *game, Entity *e) { init_edge_runner(game, e, EDGE_LEFT, SPRITE_EDGE_RUNNER_LEFT); }
/* game86 1000:2c52 */
static void enemy_init_25(GameState *game, Entity *e) { init_edge_runner(game, e, EDGE_RIGHT, SPRITE_EDGE_RUNNER_RIGHT); }

/* ---- 28 -------------------------------------------------------------------------------- */

/* game86 1000:26c5: fixed position in the middle of the field. */
static void enemy_init_28(GameState *game, Entity *e)
{
    e->anim = 0x11;
    stand_still(e);
    e->sprite = SPRITE_TYPE_28;
    e->scale = 0;
    set_death_sound(e, SAMPLE_ID_GAME_OVER, 4);
    e->hit_handler = 0x11;
    e->solid = 1;
    e->timer = 0x2828;
    e->aux16 = 0x32;
    e->x = 0xa0;
    e->y = 0x64;
    entity_set_type(e, 0x2b, 0x2b);
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
