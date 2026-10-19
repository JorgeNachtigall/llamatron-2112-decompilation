/*
 * game.h - entry points the host shell (src/platform/apps/llamatron_main.c) drives the game
 * through. The shell owns the platform, the main loop, the tick scheduler, sound and input
 * devices, the front screen (fs.exe) and presenting gfx_display_pixels(); everything game86
 * itself does lives behind these calls.
 *
 * The game does not depend on src/platform: the shell converts its Settings into GameLaunch
 * (which mirrors what game86 receives on its command tail, read by game86 1000:5f6b) and its
 * PLAT_REQUEST_* bits into GAME_HOST_*.
 */
#ifndef LLAMATRON_GAME_GAME_H
#define LLAMATRON_GAME_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "rng.h"
#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GameLaunch {
    bool vga;                   /* PSP:86h: VGA DAC palette path (else EGA) */
    bool joystick;              /* PSP:88h: calibrate and use the sticks (else keyboard) */
    bool sound;                 /* PSP:89h != 0xFF: a sound driver is present */
    uint8_t flash_mode;         /* PSP:85h: 0 none, 1 pulsing only, 2 pulsing and flashes */
    /* BIOS tick count (0040:006C) at start-up: the RNG seed. The shell derives it from the
     * time of day like the original; replays and tests pin it. */
    uint32_t bios_ticks;
    /* BIOS tick seen by each mixer call of the start-up shuffle; NULL read = constant
     * bios_ticks (a fast machine, and what the DOSBox-X reference runs pin). */
    RngTickSource shuffle_ticks;
} GameLaunch;

enum GameHostRequest {
    GAME_HOST_QUIT = 1u << 0,   /* window closed / Cmd-Q / OS shutdown */
    GAME_HOST_PAUSE = 1u << 1,  /* focus lost, minimised, hidden tab */
};

typedef struct GameHostInput {
    unsigned requests;          /* GameHostRequest bits taken since the last frame */
    bool focused;
} GameHostInput;

/* Once, after the front screen. false = the game cannot start (missing data). */
bool game_init(GameState *game, const GameLaunch *launch);

/* One 50.16 Hz timer tick, called from the shell's timing_run_timer_ticks callback, i.e.
 * after snd_tick(): the rest of the INT 08 handler, game86 1000:3e08..3ec0. */
void game_tick(GameState *game);

/* One display frame: input_frame_read, draw into the gfx pages, gfx_flip.
 * game86 1e7b:013f */
void game_frame(GameState *game, const GameHostInput *host);

/* The shell leaves its main loop once this is true. */
bool game_wants_quit(const GameState *game);

#ifdef __cplusplus
}
#endif

#endif
