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

#ifdef __cplusplus
}
#endif

#endif
