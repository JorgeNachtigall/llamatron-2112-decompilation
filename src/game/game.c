/*
 * game.c - host entry points (game.h): start-up, the timer tick's hook dispatch and the
 * display frame, which runs the top-level flow (main_sm.c) up to its next retrace wait.
 */
#include "game.h"

#include <string.h>

#include "attract.h"
#include "continue.h"
#include "init.h"
#include "input.h"
#include "main_sm.h"
#include "palette_pulse.h"
#include "pause.h"
#include "tick.h"

static uint16_t constant_tick(void *ctx, unsigned call)
{
    (void)call;
    return (uint16_t)*(const uint32_t *)ctx;
}

/* game86 1000:0040..00ab (config part of 5f6b, RNG table build) */
bool game_init(GameState *game, const GameLaunch *launch)
{
    memset(game, 0, sizeof *game);
    init_image_values(game);
    game->vga = launch->vga;
    game->joystick = launch->joystick;
    game->sound_present = launch->sound;
    game->sound_on = launch->sound;
    game->flash_mode = launch->flash_mode;

    uint32_t start_tick = launch->bios_ticks;      /* constant_tick's, for the whole shuffle */
    RngTickSource ticks = launch->shuffle_ticks;
    if (ticks.read == NULL)
        ticks = (RngTickSource){ constant_tick, &start_tick };
    rng_seed(&game->rng.mixer, launch->bios_ticks);
    rng_shuffle(&game->rng, ticks);
    init_load_data(game, launch->saved_scores);
    title_tune_start(game);                         /* 00b6 */
    main_sm_start(game, launch);
    return true;
}

/* game86 1000:3e08..3ec0 (after the driver tick, which the shell runs first) */
void game_tick(GameState *game)
{
    game->tick_count++;
    input_tick_fire();
    palette_pulse_tick(game);                       /* 3e4e..3eb5 */
    switch (game->tick_hook) {                      /* 3eb6 CALL [39E4] */
    case TICK_HOOK_TITLE_TUNE:
        title_tune_tick(game);
        break;
    case TICK_HOOK_TITLE:
        title_tick_hook(game);
        break;
    case TICK_HOOK_PLAY:
        play_tick_hook(game);
        break;
    case TICK_HOOK_PAUSE:
        pause_tick_hook(game);
        break;
    case TICK_HOOK_CONTINUE:
        continue_tick_hook(game);
        break;
    case TICK_HOOK_NAME_ENTRY:
        main_sm_name_entry_tick(game);
        break;
    case TICK_HOOK_NONE:
        break;
    }
}

/* One retrace of the original: the flow runs to its next frame_run, wait_vsync or
 * palette_set. */
void game_frame(GameState *game, const GameHostInput *host)
{
    if (host->requests & GAME_HOST_QUIT)
        game->quit = true;
    if (!game->quit)
        main_sm_frame(game);
}

bool game_wants_quit(const GameState *game)
{
    return game->quit;
}
