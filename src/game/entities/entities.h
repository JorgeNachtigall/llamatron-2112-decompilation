/*
 * entities.h - helpers shared by the entity handlers in src/game/entities/ (W5 Moss/Thorn,
 * W6 Prism). The handler types, counts and dispatch tables are in entity_dispatch.h.
 *
 * Each block below belongs to the file named in its heading; only that file's owner edits it.
 */
#ifndef LLAMATRON_GAME_ENTITIES_H
#define LLAMATRON_GAME_ENTITIES_H

#include <stdint.h>

#include "entity_dispatch.h"
#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- think_basic.c (Moss) ------------------------------------------------------------- */

/* Which ships an entity touches (entity_hits_player). */
enum {
    TOUCH_P1 = 0x01,
    TOUCH_P2 = 0x02,
};

/* Frees the slot: the whole type word becomes 0xFFFF and the free-slot count goes up
 * (e.g. game86 1000:43d3, 4ece, 508e). */
void entity_release(GameState *game, Entity *e);

/* TOUCH_* bits of the ships the entity overlaps (|dx| < 16, |dy| < 10). P1 only counts while
 * its respawn timer is 0, P2 only in double-controller mode with a timer >= 0.
 * game86 1000:4b49 */
unsigned entity_hits_player(const GameState *game, const Entity *e);

/* An enemy touched the ships in `touched` (TOUCH_* bits, non-zero): kills the player unless the
 * wave is cleared, a shield or smart bomb is active or a demo runs. Shared tail of most think
 * handlers. game86 1000:43f7 */
void entity_touch_player(GameState *game, unsigned touched);

/* entity_hits_player, then entity_touch_player if a ship was touched; true if one was. Most
 * think handlers end their tick then (they jump to 1000:43f7). */
bool entity_check_touch(GameState *game, const Entity *e);

/* The death itself, after the guards: the touched player (P2 when both) starts dying and the
 * death sound plays (with a delayed second one when few enemies are left). In single-
 * controller mode a P2-only touch does nothing. Also entered from pickup_effect_09 (37fa).
 * game86 1000:441d */
void player_kill(GameState *game, unsigned touched);

/* Think handlers (think_table.c puts them into entity_think_handlers). */
void think_00(GameState *game, Entity *e);
void think_01(GameState *game, Entity *e);
void think_02(GameState *game, Entity *e);
void think_03(GameState *game, Entity *e);
void think_09(GameState *game, Entity *e);
void think_0d(GameState *game, Entity *e);
/* think_spawners.c */
void think_0b(GameState *game, Entity *e);
void think_0c(GameState *game, Entity *e);
/* think_homing.c */
void think_10(GameState *game, Entity *e);
void think_11(GameState *game, Entity *e);
void think_12(GameState *game, Entity *e);
/* Steering towards game->target with inertia (16.16 fixed point in the record, see
 * think_homing.c). Tail of think_10..13, 1f, 28, 2a. game86 1000:38a2 */
void entity_homing_move(GameState *game, Entity *e);

/* ---- movement.c (Moss)---------------------------------------------------------------- */

/* Playfield an entity bounces in (entity_move_bounce, game86 1000:4e85..4ec4). */
enum {
    BOUNCE_X_MIN = 0x08, BOUNCE_X_MAX = 0x138,
    BOUNCE_Y_MIN = 0x10, BOUNCE_Y_MAX = 0xbc,
};

/* Adds the velocity; a coordinate that would leave the playfield reverses that velocity
 * component and sets game->bounced. game86 1000:4e7f */
void entity_move_bounce(GameState *game, Entity *e);

/* Halves (dx, dy) until |dx| <= max_x and |dy| <= max_y. The magnitudes are halved on their
 * own, like the original keeps them in separate registers (1000:4b1a, 50b8), so a negative
 * component rounds towards minus infinity while the test uses the rounded-down magnitude. */
void vector_shrink_to(int16_t *dx, int16_t *dy, int16_t max_x, int16_t max_y);

/* Velocity from the entity towards game->target, shrunk to the given limits.
 * game86 1000:509b (limits in [0x41b]/[0x41d]) */
Point16 entity_aim_at_target(const GameState *game, const Entity *e, int16_t max_x, int16_t max_y);

/* ---- spawn.c (Thorn) ------------------------------------------------------------------ */

/* Sets the type word: the think handler now, and the one a spawn hatches into (think_00). */
static inline void entity_set_type(Entity *e, int think, int hatch_into)
{
    e->think = (int8_t)think;
    e->hatch_into = (int8_t)hatch_into;
}

/* Frees a slot without touching free_slots (the original writes 0xFFFF to the type word and
 * leaves the count alone in the hit / kill code, e.g. game86 1000:1191, 2143, 3654). */
static inline void entity_vanish(Entity *e)
{
    entity_set_type(e, ENTITY_FREE, ENTITY_FREE);
}

/* Callback ids stored in aux1c by some inits and called by think_25. The values are the
 * original's code addresses, kept as ids so the test adapter stays a plain copy. */
enum {
    ENTITY_CALLBACK_3119 = 0x3119,  /* enemy_init_20 */
    ENTITY_CALLBACK_32D7 = 0x32d7,  /* enemy_init_1e */
    ENTITY_CALLBACK_33E3 = 0x33e3,  /* enemy_init_1b */
};

/* Random spawn position at least 0x20 away from the ships (see spawn.c for the y quirk).
 * game86 1000:1c67 */
void enemy_random_spawn_pos(GameState *game, Entity *e);

/* Enemy projectile template (game86's scratch words 0419..0421). */
typedef struct EnemyShot {
    int16_t aim_y;              /* 0419 -> +16 */
    int16_t aim_x;              /* 041B -> +1a */
    int16_t sprite;             /* 041D -> +0c (and +24) */
    int16_t speed;              /* 041F -> +1c and +20 */
    int16_t hit_handler;        /* 0421 -> +3e */
} EnemyShot;

/* Turns `shot` into a projectile of type 0x10 at `from`'s position. The original also writes
 * zero to [SI+8] and [SI+0Ah] of whatever record its caller had in SI; callers do that
 * themselves where it matters. game86 1000:1232 */
void enemy_shot_init(GameState *game, Entity *shot, const Entity *from, const EnemyShot *tmpl);
/* First free slot made into a projectile, or NULL when the table is full. game86 1000:1221 */
Entity *enemy_shot_alloc(GameState *game, const Entity *from, const EnemyShot *tmpl);
/* think_13's dropped projectile (sprite 0x91, idle state reset first). game86 1000:10b0 */
Entity *enemy_shot_drop(GameState *game, const Entity *from);

/* A plain child (type 0x0f) at `parent`'s position with the given sprite and velocity, or
 * NULL when the table is full. game86 1000:3c91 / 3ca3 */
Entity *entity_spawn_child(GameState *game, const Entity *parent, int16_t sprite, int16_t vx,
                           int16_t vy);
void entity_init_child(GameState *game, Entity *child, const Entity *parent, int16_t sprite,
                       int16_t vx, int16_t vy);
/* entity_spawn_child unless the wave intro is running or sprite is 0. game86 1000:3c84 */
Entity *entity_spawn_child_if(GameState *game, const Entity *parent, int16_t sprite,
                              int16_t vx, int16_t vy);
/* Four children flying apart (think0e_mode_02, kill action 07). game86 1000:3b26 */
void entity_burst_four(GameState *game, const Entity *parent);
/* Fragment slot at `at` for think_0b / think_0c (they set its type), or NULL when the table
 * is full. game86 1000:4f93 */
Entity *entity_spawn_debris(GameState *game, const Entity *at);

/* ---- enemy_init.c (Thorn) ------------------------------------------------------------- */

/* Init bodies after the random placement, also used by other handlers on an existing
 * record. enemy_setup_00's `hatch_fast` stands for wave_type != 0 (ent_hit_08 forces it). */
void enemy_setup_00(GameState *game, Entity *e, bool hatch_fast);   /* 1000:1dd0 */
void enemy_setup_01(GameState *game, Entity *e);                    /* 1000:1edf */
void enemy_setup_0c(GameState *game, Entity *e);                    /* 1000:2407 */
void enemy_setup_0e(GameState *game, Entity *e);                    /* 1000:21af */
/* Type 0x24 record with sprite 0x136 + phase. game86 1000:3441 */
void enemy_setup_type24(GameState *game, Entity *e, int16_t phase, int16_t hp);

/* ---- hit.c / pickups.c (Thorn) -------------------------------------------------------- */

/* The common kill: score, explosion, pickup drop. ent_hit_00, game86 1000:0dfe */
void entity_kill(GameState *game, Entity *e, const EntityHit *hit);

/* ent_hit_03 ran the record out of hit points: its kill action (tbl_hit_kill_actions,
 * CS:1047, index e->kill_action). game86 1000:106a */
void hit_kill_action(GameState *game, Entity *e, const EntityHit *hit);

/* think_1c touched a pickup (already freed by the caller): apply the effect of kind
 * e->timer. `touched` = the TOUCH_* bits from entity_hits_player. game86 1000:0f69 */
void pickup_collect(GameState *game, Entity *e, unsigned touched);

/* Pickup kind 9, also entered straight from think_11/12/1b: unless the wave is cleared, a
 * shield is up or it is the demo, flash, play the death sound and kill the player. `touched`
 * goes to player_kill unchanged (it tests only bit 1, so callers may pass any value).
 * game86 1000:37c2 */
void deadly_touch(GameState *game, unsigned touched);

#ifdef __cplusplus
}
#endif

#endif
