/*
 * padmap.h - host gamepads as the original's controls.
 *
 * The game only knows the PC keyboard (scan codes through the INT 09 handler, game86
 * 1000:e567) and up to two analogue joysticks on port 201h. Gamepads are mapped onto those, so
 * the game sees no difference:
 *
 *   keyboard control (llama.inf 'K'): the pad types keys
 *     left stick / d-pad          -> cursor keys (48h up, 50h down, 4Bh left, 4Dh right)
 *     right stick, A, B, X or Y   -> Space (39h, fire)
 *   joystick control (llama.inf 'J'): the pads are the joysticks (input_set_stick)
 *     pad 1 left stick + A/B      -> joystick 1 position + buttons
 *     pad 2 left stick + A/B      -> joystick 2; without a second pad, pad 1's right stick
 *                                    (+ X/Y) is joystick 2 (the original's twin-stick aim)
 *   both:
 *     Start                       -> P (19h, pause)
 *     Back / Select               -> S (1Fh, sound on/off while paused, game86 1000:1500)
 *
 * Keys are sent as make codes when a control goes down and break codes when it comes up.
 */
#ifndef LLAMATRON_PADMAP_H
#define LLAMATRON_PADMAP_H

#include "platform.h"

#include <stdbool.h>
#include <stdint.h>

/* A stick counts as pushed beyond this deflection (keyboard mapping). */
#define PADMAP_THRESHOLD 0.5f

typedef struct PadMap {
    unsigned held;              /* PadKey bits currently sent as pressed */
} PadMap;

typedef void (*PadKeyFn)(uint8_t make_code, bool pressed, void *user);

/* Keyboard control: turn pad 1 into key presses / releases. */
void padmap_keys(PadMap *map, const PlatGamepad *pad, PadKeyFn on_key, void *user);
/* Joystick control: Start/Back as keys, sticks and fire buttons left to padmap_sticks(). */
void padmap_menu_keys(PadMap *map, const PlatGamepad *pad, PadKeyFn on_key, void *user);

/* Joystick control: the two original joysticks from the host pads. */
typedef struct PadStick {
    float x, y;
    bool button_a, button_b;
} PadStick;
void padmap_sticks(const PlatGamepad *pad1, const PlatGamepad *pad2, PadStick sticks[2]);

#endif /* LLAMATRON_PADMAP_H */
