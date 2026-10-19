/*
 * palette_pulse.h - colour 1 pulsing with the timer (the glow of the playfield frame and the
 * other colour-1 pictures), when the flash option allows it.
 */
#ifndef LLAMATRON_GAME_PALETTE_PULSE_H
#define LLAMATRON_GAME_PALETTE_PULSE_H

#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Once per timer tick, after tick_count has counted it: with flash_mode != 0 and
 * pulse_enable, VGA sets colour 1 to the pulse table's entry for the tick (16 steps), EGA
 * steps pulse_index every 4th tick through its 8 register values. game86 1000:3e4e..3eb5 */
void palette_pulse_tick(GameState *game);

#ifdef __cplusplus
}
#endif

#endif
