/*
 * input.c - keyboard handler, per-frame control read and tick fire refresh (see input.h).
 */
#include "input.h"

#include <string.h>

/* Port 201h button bits after inversion (game86 1e7b:0047 / 1000:3e28). */
enum {
    JOY_BUTTONS_STICK1 = 0x30,
    JOY_BUTTONS_STICK2 = 0xc0,
    JOY_BUTTON_1A = 0x10,
    JOY_BUTTON_1B = 0x20,
    JOY_BUTTON_2A = 0x40,
    JOY_BUTTON_2B = 0x80,
    JOY_AXES_IDLE = 0x0f,   /* axis one-shots have run out before the read: inverted to 1s */
};

static InputState g_input;

void input_init(InputDevice device, bool single_stick)
{
    memset(&g_input, 0, sizeof g_input);
    g_input.device = device;
    g_input.single_stick = single_stick;
    for (int i = 0; i < INPUT_STICKS; i++) {
        g_input.calibration[i] = input_calibrate(INPUT_JOYSTICK_CENTRE_COUNT, INPUT_JOYSTICK_CENTRE_COUNT);
        g_input.sticks[i].count_x = INPUT_JOYSTICK_CENTRE_COUNT;
        g_input.sticks[i].count_y = INPUT_JOYSTICK_CENTRE_COUNT;
    }
}

InputState *input_state(void)
{
    return &g_input;
}

/* game86 1000:6014: high = c + c/2, low = c/2 + (c/2)/2 (all integer halvings). */
JoystickCalibration input_calibrate(uint16_t centre_x, uint16_t centre_y)
{
    JoystickCalibration c;
    c.x_high = (uint16_t)(centre_x + centre_x / 2);
    c.x_low = (uint16_t)(centre_x / 2 + centre_x / 2 / 2);
    c.y_high = (uint16_t)(centre_y + centre_y / 2);
    c.y_low = (uint16_t)(centre_y / 2 + centre_y / 2 / 2);
    return c;
}

/* game86 1000:e567 */
void input_keyboard_byte(uint8_t scan_code)
{
    g_input.last_scan_code = scan_code;
    if (scan_code < PC_KEY_BREAK) {
        g_input.last_make_code = scan_code;
        g_input.last_make_code_copy = scan_code;
    }

    bool released = (scan_code & PC_KEY_BREAK) != 0;
    uint8_t bit;
    switch (scan_code & 0x7f) {
    case PC_KEY_SPACE: bit = INPUT_FIRE; break;
    case PC_KEY_LEFT: bit = INPUT_LEFT; break;
    case PC_KEY_RIGHT: bit = INPUT_RIGHT; break;
    case PC_KEY_UP: bit = INPUT_UP; break;
    case PC_KEY_DOWN: bit = INPUT_DOWN; break;
    default: return;
    }
    if (released) {
        /* Port deviation (documented): the original's INT 09 runs asynchronously, so a real
         * key press always overlaps at least one timer tick. Here press and release can arrive
         * closer together; keep the key held until the next tick (1000:3e12 reads fire there).
         * Longer presses behave exactly as before. */
        if (g_input.unsampled & bit)
            g_input.release_pending |= bit;
        else
            g_input.keys_held &= (uint8_t)~bit;
    } else {
        g_input.keys_held |= bit;
        g_input.unsampled |= bit;
        g_input.release_pending &= (uint8_t)~bit;
    }
}

/* A timer tick has run: finish the releases that were waiting for it. */
static void keys_sampled(void)
{
    g_input.unsampled = 0;
    uint8_t done = g_input.release_pending;
    g_input.keys_held &= (uint8_t)~done;
    g_input.release_pending &= (uint8_t)~done;
}

void input_key_event(uint8_t make_code, bool pressed)
{
    input_keyboard_byte(pressed ? make_code : (uint8_t)(make_code | PC_KEY_BREAK));
}

/* Host stick position -> the count the original's timing loop would have measured:
 * count = centre * (1 + axis), so full left/up = 0 and full right/down = 2 * centre. */
static uint16_t axis_to_count(float axis)
{
    if (axis < -1.0f) axis = -1.0f;
    if (axis > 1.0f) axis = 1.0f;
    return (uint16_t)((float)INPUT_JOYSTICK_CENTRE_COUNT * (1.0f + axis) + 0.5f);
}

void input_set_stick(int stick, float x, float y, bool button_a, bool button_b)
{
    if (stick < 0 || stick >= INPUT_STICKS)
        return;
    JoystickReading *r = &g_input.sticks[stick];
    r->count_x = axis_to_count(x);
    r->count_y = axis_to_count(y);
    r->button_a = button_a;
    r->button_b = button_b;
}

/* Port 201h read before the axis trigger, inverted (game86 1e7b:0046, 1000:3e27): 1 = button
 * pressed, low nibble always set. */
static uint8_t joystick_button_bits(void)
{
    uint8_t bits = JOY_AXES_IDLE;
    if (g_input.sticks[0].button_a) bits |= JOY_BUTTON_1A;
    if (g_input.sticks[0].button_b) bits |= JOY_BUTTON_1B;
    if (g_input.sticks[1].button_a) bits |= JOY_BUTTON_2A;
    if (g_input.sticks[1].button_b) bits |= JOY_BUTTON_2B;
    return bits;
}

/* Left is tested before right and up before down; only one of each pair can be set. */
static uint8_t stick_directions(const JoystickReading *r, const JoystickCalibration *c)
{
    uint8_t bits = 0;
    if (r->count_x <= c->x_low)
        bits |= INPUT_LEFT;
    else if (r->count_x >= c->x_high)
        bits |= INPUT_RIGHT;
    if (r->count_y <= c->y_low)
        bits |= INPUT_UP;
    else if (r->count_y >= c->y_high)
        bits |= INPUT_DOWN;
    return bits;
}

/* game86 1e7b:0000 */
void input_frame_read(bool force_both_sticks)
{
    if (g_input.device == INPUT_DEVICE_KEYBOARD) {
        /* 1e7b:0007..0037: controller 1 from held keys; controller 2 untouched. */
        uint8_t held = g_input.keys_held;
        uint8_t c = held & INPUT_FIRE;
        if (held & INPUT_LEFT)
            c |= INPUT_LEFT;
        else if (held & INPUT_RIGHT)
            c |= INPUT_RIGHT;
        if (held & INPUT_UP)
            c |= INPUT_UP;
        else if (held & INPUT_DOWN)
            c |= INPUT_DOWN;
        g_input.controls[0] = c;
        return;
    }

    /* 1e7b:0038..0139 */
    g_input.controls[0] = 0;
    g_input.controls[1] = 0;
    g_input.joystick_buttons = joystick_button_bits();
    if (g_input.joystick_buttons & JOY_BUTTONS_STICK1)
        g_input.controls[0] |= INPUT_FIRE;
    if (g_input.joystick_buttons & JOY_BUTTONS_STICK2)
        g_input.controls[1] |= INPUT_FIRE;

    g_input.controls[0] |= stick_directions(&g_input.sticks[0], &g_input.calibration[0]);
    if (!g_input.single_stick || force_both_sticks)
        g_input.controls[1] |= stick_directions(&g_input.sticks[1], &g_input.calibration[1]);
}

/* game86 1000:3e12..3e4c */
void input_tick_fire(void)
{
    g_input.controls[0] &= (uint8_t)~INPUT_FIRE;
    g_input.controls[1] &= (uint8_t)~INPUT_FIRE;
    if (g_input.device == INPUT_DEVICE_JOYSTICK) {
        g_input.joystick_buttons = joystick_button_bits();
        if (g_input.joystick_buttons & JOY_BUTTONS_STICK1)
            g_input.controls[0] |= INPUT_FIRE;
        if (g_input.joystick_buttons & JOY_BUTTONS_STICK2)
            g_input.controls[1] |= INPUT_FIRE;
    } else if (g_input.keys_held & INPUT_FIRE) {
        g_input.controls[0] |= INPUT_FIRE;
    }
    keys_sampled();
}
