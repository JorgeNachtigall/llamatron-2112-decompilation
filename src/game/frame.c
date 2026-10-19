/*
 * frame.c - the per-frame side of play (game86 1e7b:013f calls the draw hook 1000:1a3b).
 */
#include "entity_dispatch.h"

#include <stddef.h>

#include "game_data.h"

/* Sprite numbers count from the original's table 218C:001C = sprite_refs entry 0x0E. */
enum { SPRITE_REF_BASE = 0x0E };

/* game86 1000:4374 */
void entities_animate_all(GameState *game, const DrawTarget *target)
{
    for (int i = 0; i < ENTITY_COUNT; i++) {
        Entity *e = &game->entities[i];
        if (entity_is_free(e))
            continue;

        unsigned index = (uint8_t)e->anim;
        AnimFn animate = index < ANIM_HANDLER_COUNT ? entity_anim_handlers[index] : NULL;
        if (animate != NULL)
            animate(game, e, target);

        if (e->overlay.x == 0)
            continue;
        const GfxSprite *sprite = asset_sprite_ref_gfx(SPRITE_REF_BASE + (unsigned)e->overlay.sprite);
        if (sprite != NULL)
            draw_sprite(target, sprite, e->overlay.x, e->overlay.y);
        if (--e->overlay.frames == 0)
            e->overlay.x = 0;
    }
}
