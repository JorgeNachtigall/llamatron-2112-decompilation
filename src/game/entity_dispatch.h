/*
 * entity_dispatch.h - how the tick and frame loops (W4) call the per-entity handlers
 * (W5, src/game/entities/; drawing helpers from W6).
 *
 * DRAFT for agreement with W5 (Thorn, Moss) and W6 (Prism).
 *
 * The original passes the entity in DI (SI for spawns) and reaches everything else through
 * DGROUP. In the port every handler gets the GameState and the entity; extra inputs that the
 * original passed in registers or scratch variables are explicit parameters. Each table is
 * defined by its handlers' owner (W5 think_table.c / init_table.c / hit.c, W6 anim.c);
 * W4 owns the loops that index them:
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

/* What hit the entity: always a shot (game86 SI). The shot loop calls the hit handler after
 * the shot has moved (1000:0c40..0c6b) and passed the tick-parity test (0cd1); with the
 * smart-bomb flag [0x615] every solid entity is hit once by that same shot (0cf7..0d27).
 * Before a normal hit the loop also does 0d6d..0d85 (kills the shot when level_flags < 0,
 * copies 2338 -> 2336 unless the entity's think is 0x13). */
typedef struct EntityHit {
    Shot *shot;                 /* never NULL; handlers read its direction and velocity */
    bool smart_bomb;            /* hit through [0x615], not by overlap */
    Point16 at;                 /* the shot's new position ([0x413]/[0x417]) */
} EntityHit;

typedef void (*HitFn)(GameState *game, Entity *e, const EntityHit *hit);

/* Wave spawn. The loop has already allocated `e`, cleared its overlay and set e->kind to
 * the script's enemy type; the handler places and initialises it. game86 1000:1dcd.. */
typedef void (*EnemyInitFn)(GameState *game, Entity *e);

extern const ThinkFn entity_think_handlers[THINK_HANDLER_COUNT];
extern const AnimFn entity_anim_handlers[ANIM_HANDLER_COUNT];
extern const HitFn entity_hit_handlers[HIT_HANDLER_COUNT];
extern const EnemyInitFn enemy_init_handlers[ENEMY_INIT_COUNT];

/* Services W4 provides to the handlers (scores: score.h; sound: engine snd.h snd_play, which
 * is driver AH=3 including the stub-driver toggle). */

/* First free slot, reset to the idle state like game86 1000:3d43 (overlay off, anim
 * ANIM_IDLE, scale 0); NULL when all 70 are in use. [H] The original then returns SI one
 * record past the table with the sign flag clear, and some callers (e.g. 1000:1db5) write
 * to it anyway; how the port reproduces that is decided per caller. */
Entity *entity_alloc(GameState *game);

/* All 70 slots free and idle. game86 1000:4307 */
void entities_clear(GameState *game);

/* The original's slot counter for `e` (BP, running 0x45 down to 0 over the table). */
int entity_slot_counter(const GameState *game, const Entity *e);

/* Every live entity's think handler (NULL entries, i.e. not yet ported, are skipped).
 * game86 1000:432b */
void entities_think_all(GameState *game);

/* Every live entity's animate handler (NULL skipped), then its overlay sprite, which counts
 * down its frames. game86 1000:4374 */
void entities_animate_all(GameState *game, const DrawTarget *target);

#ifdef __cplusplus
}
#endif

#endif
