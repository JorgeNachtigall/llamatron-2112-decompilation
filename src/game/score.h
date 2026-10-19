/*
 * score.h - scores as numbers modulo 10^8, with the decimal digits the original stores
 * (one per byte, most significant first, DS:4441/4449) only where text is drawn.
 */
#ifndef LLAMATRON_GAME_SCORE_H
#define LLAMATRON_GAME_SCORE_H

#include <stdint.h>

#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCORE_MODULUS 100000000u    /* 8 digits; a carry out of the top digit is lost */

/* digits[0] is the most significant. */
static inline void score_to_digits(uint32_t score, uint8_t digits[SCORE_DIGITS])
{
    for (int d = SCORE_DIGITS - 1; d >= 0; d--) {
        digits[d] = (uint8_t)(score % 10u);
        score /= 10u;
    }
}

/* Awards `increments` steps of one unit at digit `place` (0 = ten millions .. 7 = units),
 * the whole award repeated score_scale+1 times, to the current player; nothing in demo mode.
 * An extra life (pickup_pending = 3 and sample 0x19) comes with every carry from the
 * thousands into the ten-thousands digit, so only awards below 10,000 points give one.
 * game86 1000:0335 (there AX = increments-1, BX = place). */
void score_add(GameState *game, int place, int increments);

#ifdef __cplusplus
}
#endif

#endif
