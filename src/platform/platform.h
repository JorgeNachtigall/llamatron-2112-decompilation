/*
 * platform.h - host services for the Llamatron port (SDL2, native and Emscripten).
 *
 * This layer knows nothing about the game. It provides:
 *   - a window that shows an indexed (8-bit) image of any size, scaled to a 4:3 display
 *     like a 1992 monitor would (320x200, the title's 320x400 and 640x350 all fill 4:3);
 *   - keyboard events translated to IBM PC scan code set 1 (what the original read from
 *     port 60h), plus up to two gamepads as analogue sticks + buttons;
 *   - a monotonic clock and a callback-driven main loop that runs natively and under
 *     emscripten_set_main_loop (never blocks inside the callback);
 *   - a mono audio output stream (used by src/sound/).
 */
#ifndef LLAMATRON_PLATFORM_H
#define LLAMATRON_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PlatConfig {
    const char *window_title;
    int window_scale;          /* initial window = scale x 320x240 (4:3); 0 = default (3) */
    bool fullscreen;
    bool vsync;                /* native: present with vsync; ignored on the web (rAF) */
    int web_timer_fps;         /* web only: > 0 drives frames by a timer instead of
                                  requestAnimationFrame (headless tests: rAF barely runs there) */
    bool unpaced;              /* native only: run frames back to back, no vsync and no pacing,
                                  and draw only frames read back (plat_set_frame_capture), so
                                  test runs do not depend on the display or window server */
} PlatConfig;

/* One RGB colour, 8 bits per channel. */
typedef struct PlatColor {
    uint8_t r, g, b;
} PlatColor;

bool plat_init(const PlatConfig *config);
void plat_shutdown(void);

/* ---- Display ------------------------------------------------------------------------- */

/* Show an indexed image: `pixels` is width*height bytes (row stride `pitch`), each byte an
 * index into `palette` (256 entries), scaled for a 4:3 display as plat_scale_mode() says. */
void plat_present(const uint8_t *pixels, int width, int height, int pitch,
                  const PlatColor palette[256]);

/* How the image is scaled to the window (the drawable in pixels, so Retina / high-DPI
 * screens get their full resolution). F10 cycles through them. */
typedef enum PlatScaleMode {
    PLAT_SCALE_NEAREST,     /* largest 4:3 rectangle, nearest-neighbour (default) */
    PLAT_SCALE_INTEGER,     /* whole multiples of each pixel, as close to 4:3 as they get */
    PLAT_SCALE_SHARP,       /* largest 4:3 rectangle: nearest to a whole multiple, then smoothed */
    PLAT_SCALE_MODE_COUNT
} PlatScaleMode;

void plat_set_scale_mode(PlatScaleMode mode);
PlatScaleMode plat_scale_mode(void);

void plat_toggle_fullscreen(void);

/* Regression support: when enabled, every plat_present() reads the rendered frame back
 * (scaled, before presenting) and keeps its FNV-1a hash and size. Off by default (slow). */
void plat_set_frame_capture(bool enabled);
uint32_t plat_captured_frame_hash(int *width, int *height);

/* ---- Input --------------------------------------------------------------------------- */

/* Called for every key press/release with the PC set-1 make code (0x01..0x7f) of the key.
 * Extended keys (cursor block, keypad Enter, right Ctrl/Alt) report their base make code,
 * the way the original's INT 09 handler saw them after ignoring the 0xE0 prefix byte. */
typedef void (*PlatKeyFn)(uint8_t pc_make_code, bool pressed, void *user);

/* Pump host events. Returns false once the user closed the window (or pressed Cmd-Q). */
bool plat_poll_events(PlatKeyFn on_key, void *user);

/* Requests from the host for the game to act on (no game logic here; the game decides).
 * They accumulate across polls until taken. */
enum PlatRequest {
    PLAT_REQUEST_QUIT = 1u << 0,    /* window closed, Cmd-Q / Alt-F4, OS shutdown */
    PLAT_REQUEST_PAUSE = 1u << 1,   /* focus lost, minimised, hidden tab, app backgrounded */
};
unsigned plat_take_requests(void);
bool plat_has_focus(void);

enum { PLAT_MAX_GAMEPADS = 2 };

enum PlatGamepadButton {
    PLAT_PAD_BUTTON_A = 1u << 0,   /* fire 1 */
    PLAT_PAD_BUTTON_B = 1u << 1,   /* fire 2 */
    PLAT_PAD_BUTTON_START = 1u << 2,
    PLAT_PAD_BUTTON_X = 1u << 3,
    PLAT_PAD_BUTTON_Y = 1u << 4,
    PLAT_PAD_BUTTON_BACK = 1u << 5,
};

typedef struct PlatGamepad {
    bool connected;
    float x, y;                    /* left stick, -1 .. +1, +x = right, +y = down (d-pad gives +-1) */
    float rx, ry;                  /* right stick, same convention */
    unsigned buttons;              /* PlatGamepadButton bits */
} PlatGamepad;

void plat_gamepad(int index, PlatGamepad *out);

/* ---- Time and main loop -------------------------------------------------------------- */

/* Microseconds since plat_init(), monotonic. */
uint64_t plat_time_us(void);

/* One iteration of the program: poll input, advance time, draw, present.
 * Return false to end the main loop. */
typedef bool (*PlatFrameFn)(void *user);

/* Run `frame` once per display refresh until it returns false. Natively this loops with
 * vsync (or sleeps to ~60 Hz if vsync is unavailable) and returns. Under Emscripten it
 * registers the callback with requestAnimationFrame timing and never returns (code after
 * the call does not run), so do any cleanup inside `frame` before returning false. */
void plat_run_main_loop(PlatFrameFn frame, void *user);

/* ---- Audio --------------------------------------------------------------------------- */

/* Open a mono unsigned 8-bit output at `rate` Hz. On the web the device only starts
 * producing sound after the first user gesture. */
bool plat_audio_open(int rate);
/* True once the device really produces sound (on the web: after the first user gesture has
 * resumed the AudioContext; before that SDL silently drains the queue). */
bool plat_audio_running(void);
void plat_audio_close(void);
/* Append samples to the output queue; returns the number of bytes still queued. */
size_t plat_audio_queue(const uint8_t *samples, size_t count);
size_t plat_audio_queued(void);

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_PLATFORM_H */
