/* entity_loop.c - the loops that drive every entity's handlers (see entity_dispatch.h). */
#include "entity_dispatch.h"

#include <stddef.h>

static void entity_reset_slot(Entity *e)
{
    e->overlay.x = 0;
    e->anim = ANIM_IDLE;
    e->scale = 0;
}

/* game86 1000:4307. The original clears 100 records, 30 more than the table holds; nothing
 * is known to live in DS:5A41..61C0, so the port clears the 70 it has. */
void entities_clear(GameState *game)
{
    for (int i = 0; i < ENTITY_COUNT; i++) {
        Entity *e = &game->entities[i];
        e->think = ENTITY_FREE;
        e->hatch_into = ENTITY_FREE;
        e->anim = ANIM_IDLE;
        e->scale = 0;
        e->solid = 0;
        e->overlay.x = 0;
    }
}

/* game86 1000:3d43 */
Entity *entity_alloc(GameState *game)
{
    for (int i = 0; i < ENTITY_COUNT; i++) {
        Entity *e = &game->entities[i];
        if (entity_is_free(e)) {
            entity_reset_slot(e);
            return e;
        }
    }
    return NULL;
}

/* The slot counter the original's loops run down from 0x45 (BP); W5 handlers that read BP
 * use this too. */
int entity_slot_counter(const GameState *game, const Entity *e)
{
    return (ENTITY_COUNT - 1) - (int)(e - game->entities);
}

/* game86 1000:432b */
void entities_think_all(GameState *game)
{
    for (int i = 0; i < ENTITY_COUNT; i++) {
        Entity *e = &game->entities[i];
        if (entity_is_free(e))
            continue;

        /* In double-controller mode every other slot hunts the second ship. */
        game->target = game->ship[0];
        if ((game->options & OPT_DOUBLE_CONTROLLER) && (entity_slot_counter(game, e) & 1))
            game->target = game->ship[1];

        unsigned index = (uint8_t)e->think;
        ThinkFn think = index < THINK_HANDLER_COUNT ? entity_think_handlers[index] : NULL;
        if (think != NULL)      /* NULL: handler not ported yet */
            think(game, e);
    }
}
