/* score.c - score awards (see score.h). */
#include "score.h"

#include <stdbool.h>

#include "sound.h"

enum {
    EXTRA_LIFE_PLACE = 4,           /* a carry out of this digit (thousands) earns a life */
    EXTRA_LIFE_SAMPLE = 0x19,
    EXTRA_LIFE_RATE = 1,
    EXTRA_LIFE_PRIORITY = 6,
};

static uint32_t place_value(int place)
{
    uint32_t value = 1;
    for (int d = place; d < SCORE_DIGITS - 1; d++)
        value *= 10u;
    return value;
}

/* How often a DEC/JNS loop with a 16-bit counter starting at `counter` runs its body: once
 * per value down to 0, and once for a negative start, except -32768, which wraps to +32767. */
static long countdown_passes(int16_t counter)
{
    if (counter >= 0)
        return (long)counter + 1;
    return counter == INT16_MIN ? 0x8001L : 1;
}

/* game86 1000:0335 */
void score_add(GameState *game, int place, int increments)
{
    if (game->demo_mode)
        return;

    int player = (game->options & OPT_CURRENT_IS_P2) ? 1 : 0;
    uint32_t unit = place_value(place);
    uint32_t carry_limit = place_value(EXTRA_LIFE_PLACE - 1);    /* 10,000 */
    long repeats = countdown_passes(game->player.score_scale);
    long steps = countdown_passes((int16_t)(increments - 1));    /* the original's AX */

    for (long r = 0; r < repeats; r++) {
        for (long s = 0; s < steps; s++) {
            /* A carry out of the top digit restarts the same step (1000:037d jumps back
             * without counting it), so 99,999,999 + 1 ends at 1. */
            bool carried_out;
            do {
                uint32_t before = game->score[player];
                if (place >= EXTRA_LIFE_PLACE && before % carry_limit + unit >= carry_limit) {
                    game->player.pickup_pending = 3;
                    game_sound_play(EXTRA_LIFE_SAMPLE, EXTRA_LIFE_RATE, EXTRA_LIFE_PRIORITY);
                }
                uint32_t after = before + unit;
                carried_out = after >= SCORE_MODULUS;
                game->score[player] = after % SCORE_MODULUS;
            } while (carried_out);
        }
    }
    game->hud_dirty = true;
}
