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

#include <stdint.h>

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

/* Requests since the last clear, oldest first; once more than SOUND_LOG_SIZE were made only
 * the count keeps growing. */
unsigned game_sound_log_count(void);
SoundRequest game_sound_log_at(unsigned i);
void game_sound_log_clear(void);

#ifdef __cplusplus
}
#endif

#endif
