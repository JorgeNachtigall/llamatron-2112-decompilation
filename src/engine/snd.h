/*
 * snd.h - the game's sound service: a reimplementation of the behaviour of the Sound Blaster
 * driver sblaster.bin (docs/re-notes-sound-data.md §1, PLAN.md §2.5).
 *
 * One voice, 8-bit unsigned mono, no mixing, no looping. Requests and stops only take effect
 * at the driver tick, which the game calls from its 50.159 Hz timer (game86 1000:3e02):
 *   - snd_play() puts the sample into a single pending slot (the latest request overwrites it,
 *     including its rate and priority). If a sample is playing and the new priority is >= the
 *     current one, the current sample is stopped at the next tick so the new one starts there;
 *     otherwise the request waits until the current sample has ended.
 *   - snd_stop(n) arms a stop for sample n; it fires at a tick where n is the sample playing and
 *     stays armed until then (driver quirk). snd_stop_all() stops whatever plays at the next tick.
 *   - snd_status() reports the sample playing (0 = idle) and the priority of the current or last
 *     sample (not cleared when it ends). Sample length / rate decides when it ends; the end is
 *     seen at the first tick at or after it (the original's DMA IRQ came in between ticks).
 * With sound disabled (sound type FFh: game86's stub driver 1000:e785) every call returns 0
 * and nothing plays; game86 1000:1514 toggles between the two at run time.
 *
 * Output: snd_tick() also renders the audio of the tick it starts, at `output_rate` Hz, by
 * holding each source sample for its duration (like the SB DAC). Fetch it with
 * snd_take_output() and hand it to the platform's audio queue. Pure C, no SDL.
 */
#ifndef LLAMATRON_SND_H
#define LLAMATRON_SND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SND_SAMPLE_COUNT = 32,      /* sample numbers 1..32 */
    SND_RATE_COUNT = 5,         /* rate index (CL) 0..4 */
    SND_SILENCE = 0x80,         /* unsigned 8-bit centre */
};

/* The sample bank in our own format (extractor: samples.bin + samples.idx). */
typedef struct SoundBank {
    const uint8_t *pcm;
    size_t pcm_size;
    struct {
        uint32_t offset;
        uint32_t length;
    } samples[SND_SAMPLE_COUNT];                /* [n - 1] = sample n */
    uint8_t time_constants[SND_RATE_COUNT];     /* SB DSP TC per rate index; Hz = 1e6 / (256 - tc) */
} SoundBank;

typedef struct SoundStatus {
    uint8_t playing;            /* AH of driver function 6: sample number, 0 = idle */
    uint8_t priority;           /* AL: priority of the current or last sample */
} SoundStatus;

/* Load the bank from the asset store (samples.idx / samples.bin). */
bool snd_load_bank(SoundBank *bank);

/* Reset the driver state. `output_rate` = device sample rate for rendering, 0 = no output
 * (logic only, e.g. tests). The bank must stay valid. Sound starts enabled. */
void snd_init(const SoundBank *bank, int output_rate);

/* Real driver (true) or the stub that ignores everything (false). */
void snd_set_enabled(bool enabled);
bool snd_enabled(void);

void snd_play(uint8_t sample, uint8_t rate_index, uint8_t priority); /* driver AH=3 */
void snd_stop(uint8_t sample);                                       /* driver AH=4 */
void snd_stop_all(void);                                             /* driver AH=5 */
SoundStatus snd_status(void);                                        /* driver AH=6 */

/* Driver AH=2: call once per game tick (game86 1000:3e02, first thing in the timer handler). */
void snd_tick(void);

/* Move up to `max` rendered output samples into `out`; returns how many. */
size_t snd_take_output(uint8_t *out, size_t max);
size_t snd_output_pending(void);

/* Counters since snd_init, for tests: requests taken by the real driver (AH=3 while enabled)
 * and samples started at a tick. */
typedef struct SoundStats {
    uint32_t requests;
    uint32_t started;
} SoundStats;
SoundStats snd_stats(void);

/* Duration of sample n at a rate index in microseconds (length * (256 - tc)). */
uint64_t snd_sample_duration_us(uint8_t sample, uint8_t rate_index);

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_SND_H */
