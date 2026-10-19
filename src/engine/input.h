/*
 * input.h - the game's view of keyboard and joysticks (PLAN.md §2.6).
 *
 * Reproduces three pieces of the original:
 *   - the INT 09 keyboard handler (game86 1000:e567): keeps "keys held" bits and the last
 *     scan code / make code;
 *   - the per-frame input read (game86 1e7b:0000): turns held keys or the two joysticks into
 *     one control byte per controller, with left beating right and up beating down;
 *   - the fire refresh at the start of every timer tick (game86 1000:3e12..3e4c).
 * Joysticks are host gamepads; their analogue position is turned into the port 201h "count"
 * the original would have timed, and compared with calibration thresholds computed exactly as
 * game86 1000:6014 does, so dead zones stay asymmetric like on the real hardware.
 */
#ifndef LLAMATRON_INPUT_H
#define LLAMATRON_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bits of a control byte (game86 [0x7fc]/[0x7fb]) and of the keys-held byte ([0x588]). */
enum InputControlBits {
    INPUT_UP = 0x01,
    INPUT_DOWN = 0x02,
    INPUT_LEFT = 0x04,
    INPUT_RIGHT = 0x08,
    INPUT_DIRECTIONS = 0x0f,
    INPUT_FIRE = 0x80,
};

/* PC scan code set 1 make codes the game looks at. Break code = make | PC_KEY_BREAK. */
enum PcKey {
    PC_KEY_ESCAPE = 0x01,
    PC_KEY_Q = 0x10,        /* quit (checked by the frame loop, game86 1e7b:0147) */
    PC_KEY_P = 0x19,        /* pause */
    PC_KEY_X = 0x2d,        /* abort wave */
    PC_KEY_SPACE = 0x39,    /* fire */
    PC_KEY_UP = 0x48,
    PC_KEY_LEFT = 0x4b,
    PC_KEY_RIGHT = 0x4d,
    PC_KEY_DOWN = 0x50,
    PC_KEY_BREAK = 0x80,
};

typedef enum InputDevice {
    INPUT_DEVICE_KEYBOARD,  /* llama.inf 'K' (game86 [0x58d] != 0) */
    INPUT_DEVICE_JOYSTICK,  /* llama.inf 'J' */
} InputDevice;

enum { INPUT_CONTROLLERS = 2, INPUT_STICKS = 2 };

/* Thresholds in port-201h count units (game86 [0x58e..0x59c]). A count <= low means
 * left/up, >= high means right/down. */
typedef struct JoystickCalibration {
    uint16_t x_low, x_high;
    uint16_t y_low, y_high;
} JoystickCalibration;

typedef struct JoystickReading {
    uint16_t count_x, count_y;  /* timed resistance, centre ~= calibrated centre */
    bool button_a, button_b;
} JoystickReading;

typedef struct InputState {
    InputDevice device;
    bool single_stick;              /* game86 [0x58c]: stick 2 did not answer at calibration */

    /* Keyboard handler state (game86 1000:e567). */
    uint8_t keys_held;              /* [0x588]: INPUT_* bits of keys currently down */
    uint8_t last_scan_code;         /* [0x587]: last byte, make or break */
    uint8_t last_make_code;         /* [0x586]: last make code; the game clears it after use */
    uint8_t last_make_code_copy;    /* [0xb72]: second copy, consumed independently */
    uint8_t unseen_by_frame;        /* port only: pressed keys input_frame_read has not read yet */
    uint8_t unseen_by_tick;         /* port only: INPUT_FIRE pressed, input_tick_fire not run yet */
    uint8_t release_pending;        /* port only: released before being seen; cleared once seen */

    /* Per-controller control bytes: [0] = controller 1 ([0x7fc]), [1] = controller 2 ([0x7fb]). */
    uint8_t controls[INPUT_CONTROLLERS];
    uint8_t joystick_buttons;       /* [0x7fd]: port 201h inverted (bits 4..7 = buttons, 0..3 set) */

    JoystickCalibration calibration[INPUT_STICKS];
    JoystickReading sticks[INPUT_STICKS];
} InputState;

/* Count value used for a centred host stick; calibration is derived from it. */
enum { INPUT_JOYSTICK_CENTRE_COUNT = 100 };

void input_init(InputDevice device, bool single_stick);
/* The game reads and clears fields directly (e.g. last_make_code). */
InputState *input_state(void);

/* Thresholds from the centre counts measured at start-up (game86 1000:6014). */
JoystickCalibration input_calibrate(uint16_t centre_x, uint16_t centre_y);

/* game86 1000:e567: one byte from the keyboard (make code, or make | 0x80 for a release).
 * A key released before it was sampled stays held until every reader of keys_held has seen it:
 * input_frame_read for all keys and, for fire, input_tick_fire too (game86 reads fire in both,
 * e.g. the title starts on the frame's copy, the shots on the tick's), so taps shorter than one
 * host frame are not lost. last_make_code is set on the press as in the original and survives
 * the release. */
void input_keyboard_byte(uint8_t scan_code);
/* Convenience for platform key events. */
void input_key_event(uint8_t make_code, bool pressed);

/* Feed a host gamepad as stick `stick`: x, y in -1..+1 (+ = right/down). */
void input_set_stick(int stick, float x, float y, bool button_a, bool button_b);

/* game86 1e7b:0000: per-frame read into controls[]. Stick 2 is read unless only one stick was
 * found at start-up; `force_both_sticks` (game86 [0x9d3] != 0, set by the title code) reads it
 * regardless. An unplugged host gamepad reads as centred. */
void input_frame_read(bool force_both_sticks);

/* game86 1000:3e12..3e4c: start of every timer tick, re-read the fire bits. */
void input_tick_fire(void);

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_INPUT_H */
