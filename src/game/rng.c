/* rng.c - start-up shuffle and gameplay random bytes (see rng.h). */
#include "rng.h"

static uint16_t rotate_right(uint16_t value, unsigned bits)
{
    return (uint16_t)((value >> bits) | (value << (16u - bits)));
}

/* game86 1000:5f6b: both history slots of each pair get the same tick word. */
void rng_seed(RngMixer *mixer, uint32_t bios_ticks)
{
    uint16_t low = (uint16_t)bios_ticks;
    uint16_t high = (uint16_t)(bios_ticks >> 16);

    mixer->history[0] = low;
    mixer->history[1] = high;
    mixer->history[2] = low;
    mixer->salt = high;
}

/* game86 1000:e616 */
uint16_t rng_mix(RngMixer *mixer, uint16_t bios_tick)
{
    uint16_t sum = (uint16_t)(rotate_right(mixer->history[0], 3) + mixer->history[1] +
                              mixer->history[2]);
    uint16_t result = rotate_right(sum, 2);
    uint16_t previous_tick = mixer->history[2];

    /* A tick equal to the last one would add nothing new, so it is scrambled first. */
    uint16_t next = bios_tick;
    if (bios_tick == previous_tick) {
        mixer->salt = (uint16_t)(mixer->salt + bios_tick + 0x2F);
        next = bios_tick ^ mixer->salt;
    }
    mixer->history[0] = mixer->history[1];
    mixer->history[1] = previous_tick;
    mixer->history[2] = next;
    return result;
}

/* game86 1000:007b..00ab */
void rng_shuffle(Rng *rng, RngTickSource ticks)
{
    unsigned call = 0;

    for (unsigned i = 0; i < RNG_TABLE_SIZE; i++)
        rng->table[i] = (uint8_t)i;

    for (unsigned swap = 0; swap < RNG_SHUFFLE_SWAPS; swap++) {
        unsigned a = rng_mix(&rng->mixer, ticks.read(ticks.ctx, call++)) % RNG_TABLE_SIZE;
        unsigned b = rng_mix(&rng->mixer, ticks.read(ticks.ctx, call++)) % RNG_TABLE_SIZE;
        uint8_t held = rng->table[a];
        rng->table[a] = rng->table[b];
        rng->table[b] = held;
    }
}

static uint16_t constant_tick(void *ctx, unsigned call)
{
    (void)call;
    return *(const uint16_t *)ctx;
}

void rng_init_constant(Rng *rng, uint32_t bios_ticks)
{
    uint16_t tick = (uint16_t)bios_ticks;

    rng_seed(&rng->mixer, bios_ticks);
    rng_shuffle(rng, (RngTickSource){ constant_tick, &tick });
}

/* game86 1000:e4de */
uint8_t rng_next_byte(Rng *rng)
{
    uint8_t value = rng->table[rng->position % RNG_TABLE_SIZE];
    rng->position = (uint16_t)((rng->position + 1) % RNG_TABLE_SIZE);
    return value;
}
