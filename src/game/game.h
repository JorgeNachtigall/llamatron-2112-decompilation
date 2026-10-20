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
#include <stddef.h>
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
    /* The high-score file the host keeps (scores.yak, SCORES_YAK_SIZE = 160 bytes: 10 x {8 ASCII
     * digits, 8 name bytes}), or NULL: game86 1000:60aa reads it over the built-in table. Without
     * one the game uses the user's original scores.yak if extracted (asset_user_scores). */
    const uint8_t *saved_scores;
    /* game86 1000:5e89: after a name entry, hand the table's SCORES_YAK_SIZE bytes (DS:40A0, as
     * scores_format_yak gives them) to the host to store. NULL = nothing is saved (replays,
     * tests). The host logs a failed write and carries on, as the original ignores DOS errors. */
    void (*save_scores)(const uint8_t *yak, size_t size, void *user);
    void *host_user;            /* passed to save_scores */
} GameLaunch;

enum GameHostRequest {
    GAME_HOST_QUIT = 1u << 0,   /* window closed / Cmd-Q / OS shutdown */
    GAME_HOST_PAUSE = 1u << 1,  /* focus lost, minimised, hidden tab */
};

typedef struct GameHostInput {
    unsigned requests;          /* GameHostRequest bits taken since the last frame */
    bool focused;
} GameHostInput;

/* Once, after the front screen. false = the game cannot start (missing data).
 * Call it once per process, never per game: GameState lives as long as the original's
 * DGROUP did, and state the original never resets (spawn_pairs_clobbered, the high scores,
 * the RNG) must carry over from one game to the next. */
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
