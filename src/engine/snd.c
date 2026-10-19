/*
 * snd.c - Sound Blaster driver behaviour (see snd.h). Offsets "sblaster.bin 0x..." refer to
 * the driver as loaded by llama.exe (docs/re-notes-sound-data.md §1).
 */
#include "snd.h"

#include <string.h>

/* Time is counted in units of 1/315 microsecond: one game tick (23788 PIT cycles at
 * 315/264 MHz) is then exactly 23788 * 264 units and every sample duration is an integer. */
enum { UNITS_PER_US = 315 };
#define TICK_UNITS ((uint64_t)0x5CEC * 264u)
#define UNITS_PER_SECOND ((uint64_t)UNITS_PER_US * 1000000u)

enum {
    STOP_NONE = 0,
    STOP_ANY = 0xff,
    OUTPUT_BUFFER = 1 << 15,
};

static struct {
    const SoundBank *bank;
    bool enabled;
    int output_rate;
    uint64_t now;                   /* start of the current tick */

    /* Driver variables. */
    uint8_t pending_sample;         /* [0xB2] latest request (any value; 1..32 is valid) */
    uint8_t pending_stop;           /* [0xB3] 0 none, 0xFF any, else that sample */
    uint8_t current_sample;         /* [0xB8] 0 = idle */
    uint8_t time_constant;          /* [0x2AE] from the latest request */
    uint8_t current_priority;       /* [0x2AF] sticky */
    uint8_t requested_priority;     /* [0x2B0] from the latest request */

    /* The DMA voice. */
    bool voice_active;
    uint8_t voice_sample;
    uint64_t voice_start;           /* units */
    uint64_t voice_end;
    uint64_t units_per_source_sample;

    SoundStats stats;
    uint64_t output_emitted;        /* output samples rendered since snd_init */
    uint8_t output[OUTPUT_BUFFER];
    size_t output_count;
} g_snd;

void snd_init(const SoundBank *bank, int output_rate)
{
    memset(&g_snd, 0, sizeof g_snd);
    g_snd.bank = bank;
    g_snd.enabled = true;
    g_snd.output_rate = output_rate;
}

void snd_set_enabled(bool enabled)
{
    g_snd.enabled = enabled;
}

bool snd_enabled(void)
{
    return g_snd.enabled;
}

/* sblaster.bin 0x10F */
void snd_play(uint8_t sample, uint8_t rate_index, uint8_t priority)
{
    if (!g_snd.enabled)
        return;
    g_snd.stats.requests++;
    g_snd.pending_sample = sample;
    /* CL > 4 would read code bytes after the table; the game never does that. */
    g_snd.time_constant = g_snd.bank->time_constants[rate_index < SND_RATE_COUNT ? rate_index : 0];
    g_snd.requested_priority = priority;
    if (g_snd.current_sample != 0 && priority >= g_snd.current_priority)
        g_snd.pending_stop = g_snd.current_sample;
}

/* sblaster.bin 0x13D */
void snd_stop(uint8_t sample)
{
    if (g_snd.enabled)
        g_snd.pending_stop = sample;
}

/* sblaster.bin 0x135 */
void snd_stop_all(void)
{
    if (g_snd.enabled)
        g_snd.pending_stop = STOP_ANY;
}

/* sblaster.bin 0x143; the stub driver (game86 1000:e785) returns AX = 0. */
SoundStatus snd_status(void)
{
    SoundStatus s = { 0, 0 };
    if (g_snd.enabled) {
        s.playing = g_snd.current_sample;
        s.priority = g_snd.current_priority;
    }
    return s;
}

uint64_t snd_sample_duration_us(uint8_t sample, uint8_t rate_index)
{
    if (sample < 1 || sample > SND_SAMPLE_COUNT || rate_index >= SND_RATE_COUNT)
        return 0;
    return (uint64_t)g_snd.bank->samples[sample - 1].length * (256u - g_snd.bank->time_constants[rate_index]);
}

/* sblaster.bin 0x553: end-of-transfer IRQ clears busy and the current sample. */
static void finish_if_ended(void)
{
    if (g_snd.voice_active && g_snd.now >= g_snd.voice_end) {
        g_snd.voice_active = false;
        g_snd.current_sample = 0;
    }
}

/* sblaster.bin 0x171 */
static void apply_pending_stop(void)
{
    uint8_t stop = g_snd.pending_stop;
    if (stop == STOP_NONE || (stop != STOP_ANY && stop != g_snd.current_sample))
        return;     /* a stop for another sample stays armed */
    g_snd.pending_stop = STOP_NONE;
    g_snd.voice_active = false;     /* DSP 0xD0: halt DMA */
    g_snd.current_sample = 0;
}

/* sblaster.bin 0x14B (promote) and 0x38C (start DMA) */
static void start_pending_sample(void)
{
    uint8_t sample = g_snd.pending_sample;
    if (g_snd.current_sample != 0 || sample == 0 || sample > SND_SAMPLE_COUNT)
        return;
    g_snd.current_sample = sample;
    g_snd.pending_sample = 0;
    g_snd.current_priority = g_snd.requested_priority;
    g_snd.stats.started++;

    /* The 64 KB DMA page split (second half started from the IRQ) only adds microseconds; the
     * port plays the sample as one block. */
    g_snd.voice_active = true;
    g_snd.voice_sample = sample;
    g_snd.voice_start = g_snd.now;
    g_snd.units_per_source_sample = (uint64_t)(256u - g_snd.time_constant) * UNITS_PER_US;
    g_snd.voice_end = g_snd.now + (uint64_t)g_snd.bank->samples[sample - 1].length * g_snd.units_per_source_sample;
}

/* Hold each source sample for its duration over [now, now + one tick). */
static void render_tick(void)
{
    if (g_snd.output_rate <= 0)
        return;
    uint64_t tick_end = g_snd.now + TICK_UNITS;
    for (;;) {
        uint64_t t = g_snd.output_emitted * UNITS_PER_SECOND / (uint64_t)g_snd.output_rate;
        if (t >= tick_end)
            break;
        uint8_t value = SND_SILENCE;
        if (g_snd.voice_active && t >= g_snd.voice_start && t < g_snd.voice_end) {
            uint64_t index = (t - g_snd.voice_start) / g_snd.units_per_source_sample;
            value = g_snd.bank->pcm[g_snd.bank->samples[g_snd.voice_sample - 1].offset + index];
        }
        if (g_snd.output_count < OUTPUT_BUFFER)
            g_snd.output[g_snd.output_count++] = value;
        g_snd.output_emitted++;
    }
}

/* sblaster.bin 0x0C2 (AH=2). With the stub driver the tick does nothing, but a sample that
 * was already playing still runs to its end (its DMA and IRQ are independent of the tick). */
void snd_tick(void)
{
    finish_if_ended();
    if (g_snd.enabled) {
        apply_pending_stop();
        start_pending_sample();
    }
    render_tick();
    g_snd.now += TICK_UNITS;
}

size_t snd_take_output(uint8_t *out, size_t max)
{
    size_t n = g_snd.output_count < max ? g_snd.output_count : max;
    memcpy(out, g_snd.output, n);
    memmove(g_snd.output, g_snd.output + n, g_snd.output_count - n);
    g_snd.output_count -= n;
    return n;
}

SoundStats snd_stats(void)
{
    return g_snd.stats;
}

size_t snd_output_pending(void)
{
    return g_snd.output_count;
}
