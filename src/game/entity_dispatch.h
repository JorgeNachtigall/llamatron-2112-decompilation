/*
 * entity_dispatch.h - how the tick and frame loops (W4) call the per-entity handlers
 * (W5, src/game/entities/; drawing helpers from W6).
 *
 * DRAFT for agreement with W5 (Thorn, Moss) and W6 (Prism).
 *
 * The original passes the entity in DI (SI for spawns) and reaches everything else through
 * DGROUP. In the port every handler gets the GameState and the entity; extra inputs that the
 * original passed in registers or scratch variables are explicit parameters. The tables are
 * defined by W5 next to the handlers (src/game/entities/tables.c); W4 owns the loops that
 * index them:
 *   think   entities_think_all     game86 1000:432b   per tick, every live entity
 *   animate entities_animate_all   game86 1000:4374   per frame, every live entity
 *   hit     shots_update           game86 1000:0d15 / 0d8f
 *   spawn   wave_spawn_next        game86 1000:1d97..1dcb (jumps to the init handler)
 * The kill-action (CS:1047) and pickup-effect (CS:0F7E) tables are only used inside W5's
 * hit / think code and are W5's business.
 */
#ifndef LLAMATRON_GAME_ENTITY_DISPATCH_H
#define LLAMATRON_GAME_ENTITY_DISPATCH_H

#include "draw.h"
#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    THINK_HANDLER_COUNT = 46,   /* DS:1FD2 */
    ANIM_HANDLER_COUNT = 19,    /* DS:202E */
    HIT_HANDLER_COUNT = 18,     /* CS:0C04 */
    ENEMY_INIT_COUNT = 41,      /* DS:2CA4 */
};

/* Per tick. Before the call the loop sets game->target to the ship this slot aims at
 * (P2's in double-controller mode for odd slot counters, game86 1000:4342). */
typedef void (*ThinkFn)(GameState *game, Entity *e);

/* Per frame, into the page being drawn. The loop draws e->overlay afterwards. */
typedef void (*AnimFn)(GameState *game, Entity *e, const DrawTarget *target);

/* What hit the entity. game86 passes the shot in SI and the hit point in [0x413]/[0x417];
 * with the "hit everything" flag [0x615] (smart bomb) every solid entity is hit once and
 * there is no shot. */
typedef struct EntityHit {
    Shot *shot;                 /* NULL for the smart bomb */
    int player;                 /* [H] 0/1: whose shot; to be confirmed in shots_update */
    Point16 at;                 /* hit point */
} EntityHit;

typedef void (*HitFn)(GameState *game, Entity *e, const EntityHit *hit);

/* Wave spawn. The loop has already allocated `e`, cleared its overlay and set e->kind to
 * the script's enemy type; the handler places and initialises it. game86 1000:1dcd.. */
typedef void (*EnemyInitFn)(GameState *game, Entity *e);

extern const ThinkFn entity_think_handlers[THINK_HANDLER_COUNT];
extern const AnimFn entity_anim_handlers[ANIM_HANDLER_COUNT];
extern const HitFn entity_hit_handlers[HIT_HANDLER_COUNT];
extern const EnemyInitFn enemy_init_handlers[ENEMY_INIT_COUNT];

/* Services W4 provides to the handlers. */

/* First free slot, reset to the idle state like game86 1000:3d43 (overlay off, anim
 * ANIM_IDLE, scale 0); NULL when all 70 are in use. [H] The original then returns SI one
 * record past the table with the sign flag clear, and some callers (e.g. 1000:1db5) write
 * to it anyway; how the port reproduces that is decided per caller. */
Entity *entity_alloc(GameState *game);

#ifdef __cplusplus
}
#endif

#endif
