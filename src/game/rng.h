/*
 * rng.h - the game's random numbers.
 *
 * game86 keeps gameplay randomness in a 512-byte table holding two copies of 0..255, read
 * back one byte at a time (game86 1000:e4de). At start-up the table is shuffled with 4000
 * random swaps (game86 1000:007b..00ab); the swap positions come from a small mixer
 * (game86 1000:e616) that is fed the BIOS tick count on every call.
 *
 * The BIOS tick is an input of the game, not something this module reads: the shuffle asks a
 * caller-provided RngTickSource for the tick before each of its 8000 mixer calls. On real
 * hardware the tick can advance during the shuffle, so the table depends on that whole
 * schedule, not on one start value (docs/PLAN.md §8 risk 8, tests/README.md).
 */
#ifndef LLAMATRON_GAME_RNG_H
#define LLAMATRON_GAME_RNG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    RNG_TABLE_SIZE = 512,   /* two copies of 0..255 */
    RNG_SHUFFLE_SWAPS = 4000,
};

/* Start-up mixer state. The last three values are a history of BIOS ticks (oldest first;
 * the first two start as the seed words), `salt` perturbs a tick that repeats. */
typedef struct RngMixer {
    uint16_t history[3];
    uint16_t salt;
} RngMixer;

typedef struct Rng {
    uint8_t table[RNG_TABLE_SIZE];
    uint16_t position;      /* next table index; always 0..511 (the original reads it unmasked) */
    RngMixer mixer;
} Rng;

/* Returns the low 16 bits of the BIOS tick count (0040:006C) seen by mixer call `call`
 * (0..2*RNG_SHUFFLE_SWAPS-1). */
typedef uint16_t (*RngTickFn)(void *ctx, unsigned call);

typedef struct RngTickSource {
    RngTickFn read;
    void *ctx;
} RngTickSource;

/* Seeds the mixer from the 32-bit BIOS tick count at start-up. game86 1000:5f6b */
void rng_seed(RngMixer *mixer, uint32_t bios_ticks);

/* One mixer step; `bios_tick` is the BIOS tick low word at the moment of the call.
 * game86 1000:e616 */
uint16_t rng_mix(RngMixer *mixer, uint16_t bios_tick);

/* Fills the table with two copies of 0..255 and shuffles it with the (already seeded)
 * mixer. Does not touch `position`. game86 1000:007b..00ab */
void rng_shuffle(Rng *rng, RngTickSource ticks);

/* Seeds and shuffles with a BIOS tick that stays at `bios_ticks` for the whole shuffle (a
 * fast machine; what the oracle tests and the pinned DOSBox-X runs use). */
void rng_init_constant(Rng *rng, uint32_t bios_ticks);

/* Next random byte from the table. game86 1000:e4de */
uint8_t rng_next_byte(Rng *rng);

#ifdef __cplusplus
}
#endif

#endif
