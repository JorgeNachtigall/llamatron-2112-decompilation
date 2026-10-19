/*
 * game.c - host entry points (game.h).
 *
 * Bring-up state: start-up builds the RNG table; the tick keeps the counter and the fire
 * input; frames are presented but nothing is drawn yet. The title/attract mode, the state
 * machine and play land in attract.c, main_sm.c, tick.c and frame.c.
 */
#include "game.h"

#include <string.h>

#include "gfx.h"
#include "input.h"

static uint16_t constant_tick(void *ctx, unsigned call)
{
    (void)call;
    return (uint16_t)*(const uint32_t *)ctx;
}

/* game86 1000:0040..00ab (config part of 5f6b, RNG table build) */
bool game_init(GameState *game, const GameLaunch *launch)
{
    memset(game, 0, sizeof *game);
    game->vga = launch->vga;
    game->joystick = launch->joystick;
    game->sound = launch->sound;
    game->flash_mode = launch->flash_mode;

    RngTickSource ticks = launch->shuffle_ticks;
    if (ticks.read == NULL)
        ticks = (RngTickSource){ constant_tick, (void *)&launch->bios_ticks };
    rng_seed(&game->rng.mixer, launch->bios_ticks);
    rng_shuffle(&game->rng, ticks);
    return true;
}

/* game86 1000:3e08..3ec0 (after the driver tick, which the shell runs first) */
void game_tick(GameState *game)
{
    game->tick_count++;
    input_tick_fire();
}

/* game86 1e7b:013f */
void game_frame(GameState *game, const GameHostInput *host)
{
    if (host->requests & GAME_HOST_QUIT)
        game->quit = true;
    input_frame_read(false);
    gfx_flip();
}

bool game_wants_quit(const GameState *game)
{
    return game->quit;
}
