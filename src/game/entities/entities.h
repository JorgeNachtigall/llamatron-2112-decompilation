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

/* Think handlers (think_table.c puts them into entity_think_handlers). */
void think_00(GameState *game, Entity *e);
void think_01(GameState *game, Entity *e);
void think_02(GameState *game, Entity *e);
void think_03(GameState *game, Entity *e);
void think_09(GameState *game, Entity *e);
void think_0d(GameState *game, Entity *e);

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

#ifdef __cplusplus
}
#endif

#endif
