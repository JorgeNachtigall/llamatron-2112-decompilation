/*
 * frame.c - the per-frame side of play (game86 1e7b:013f calls the draw hook 1000:1a3b).
 */
#include "frame.h"

#include <stddef.h>

#include "attract.h"
#include "continue.h"
#include "draw.h"
#include "draw_play.h"
#include "entity_dispatch.h"
#include "game_data.h"
#include "gfx.h"
#include "input.h"
#include "main_sm.h"

static void draw_hook_run(GameState *game, const DrawTarget *frame)  /* CALLF [39E0] */
{
    switch (game->draw_hook) {
    case DRAW_HOOK_TITLE:
        title_draw_hook(game, frame, attract_art_default());
        break;
    case DRAW_HOOK_PLAY:
        play_draw_hook(game, frame);
        break;
    case DRAW_HOOK_CONTINUE:
        continue_draw_hook(game, frame, continue_art_default());
        break;
    case DRAW_HOOK_NAME_ENTRY:
        main_sm_name_entry_draw(game, frame);
        break;
    case DRAW_HOOK_NONE:
        break;
    }
}

/* game86 1e7b:013f */
bool frame_run(GameState *game)
{
    InputState *input = input_state();
    input_frame_read(game->started_with_stick_b);   /* 0143 */
    if (input->last_make_code == PC_KEY_Q) {        /* 014b: game_exit */
        game->quit = true;
        return false;
    }

    DrawTarget frame = draw_target_page(gfx_draw_page_id());
    draw_set_dirty_recording(true);                 /* 0167 */
    draw_hook_run(game, &frame);
    input->last_make_code = 0;                      /* 0171 */

    /* 0176..0190: CRTC start and wait_vsync, then the flash step; the page swap and the
     * erase pass (0213..) leave the new draw page with only its background again. */
    bool palette_reload = game->flash_mode == 2 && game->flash_frame_timer == 1;
    draw_flash_step(game);
    gfx_flip();
    gfx_restore_dirty(gfx_draw_page_id());
    return palette_reload;
}

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
