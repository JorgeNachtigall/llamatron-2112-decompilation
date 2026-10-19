/*
 * sound.h - the game's sound requests.
 *
 * Every place where game86 calls the driver with AH=3 (CALLF [0x400]) calls
 * game_sound_play(); it forwards to the engine (snd_play, which also handles the stub driver
 * / sound off) and records the request, so tests and replays can compare the request
 * sequence with the original's driver-call log (PLAN §3.3).
 */
#ifndef LLAMATRON_GAME_SOUND_H
#define LLAMATRON_GAME_SOUND_H

#include <stdbool.h>
#include <stdint.h>

#include "snd.h"     /* SoundStatus */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SoundRequest {
    uint8_t sample;             /* AL, 1..32 */
    uint8_t rate_index;         /* CL, 0..4 */
    uint8_t priority;           /* CH */
} SoundRequest;

enum { SOUND_LOG_SIZE = 64 };

/* Driver AH=3. */
void game_sound_play(uint8_t sample, uint8_t rate_index, uint8_t priority);

/* The S key (pause screen) and game exit: flips sound_on and swaps the driver with the stub
 * driver. game86 1000:1514 */
struct GameState;
void game_sound_toggle(struct GameState *game);

/* Driver AH=6: the sample playing (0 = idle) and the sticky priority of the current or last
 * sample. game86 busy-waits on it and gates re-triggers with it (1000:065d, 1540, 2477). */
SoundStatus game_sound_status(void);

/* Tests and replays: while `on`, game_sound_status() answers `status` instead of asking the
 * engine (the oracle's stub driver answers a fixed AX the same way). */
void game_sound_override_status(bool on, SoundStatus status);

/* Requests since the last clear, oldest first; once more than SOUND_LOG_SIZE were made only
 * the count keeps growing. */
unsigned game_sound_log_count(void);
SoundRequest game_sound_log_at(unsigned i);
void game_sound_log_clear(void);

#ifdef __cplusplus
}
#endif

#endif
