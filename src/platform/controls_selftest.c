/*
 * controls_selftest.c - host controls give the original's scan codes (ctest controls_selftest):
 * the keys game86 reads (keymap.c) and the gamepad mapping (padmap.c).
 */
#include "keymap.h"
#include "padmap.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

static void test_keymap(void)
{
    /* game86: cursor keys and Space (1000:e567), Q quit (1e7b:0147), P pause (1000:1595),
     * X abort wave (1000:15b3); while paused G / O resume and S toggles sound (1000:14d0). */
    CHECK(keymap_sdl_to_pc(SDL_SCANCODE_UP) == 0x48 && keymap_sdl_to_pc(SDL_SCANCODE_KP_8) == 0x48);
    CHECK(keymap_sdl_to_pc(SDL_SCANCODE_DOWN) == 0x50 && keymap_sdl_to_pc(SDL_SCANCODE_KP_2) == 0x50);
    CHECK(keymap_sdl_to_pc(SDL_SCANCODE_LEFT) == 0x4b && keymap_sdl_to_pc(SDL_SCANCODE_KP_4) == 0x4b);
    CHECK(keymap_sdl_to_pc(SDL_SCANCODE_RIGHT) == 0x4d && keymap_sdl_to_pc(SDL_SCANCODE_KP_6) == 0x4d);
    CHECK(keymap_sdl_to_pc(SDL_SCANCODE_SPACE) == 0x39);
    CHECK(keymap_sdl_to_pc(SDL_SCANCODE_Q) == 0x10);
    CHECK(keymap_sdl_to_pc(SDL_SCANCODE_P) == 0x19);
    CHECK(keymap_sdl_to_pc(SDL_SCANCODE_X) == 0x2d);
    CHECK(keymap_sdl_to_pc(SDL_SCANCODE_G) == 0x22);
    CHECK(keymap_sdl_to_pc(SDL_SCANCODE_O) == 0x18);
    CHECK(keymap_sdl_to_pc(SDL_SCANCODE_S) == 0x1f);
}

typedef struct Log {
    char text[256];
} Log;

static void log_key(uint8_t code, bool pressed, void *user)
{
    Log *log = user;
    size_t n = strlen(log->text);
    snprintf(log->text + n, sizeof log->text - n, "%s%02x ", pressed ? "+" : "-", code);
}

static const char *keys(PadMap *map, PlatGamepad pad)
{
    static Log log;
    log.text[0] = 0;
    padmap_keys(map, &pad, log_key, &log);
    return log.text;
}

static void test_pad_keys(void)
{
    PadMap map = { 0 };
    PlatGamepad pad = { .connected = true };
    CHECK(strcmp(keys(&map, pad), "") == 0);
    pad.x = -0.8f;                                           /* left */
    CHECK(strcmp(keys(&map, pad), "+4b ") == 0);
    CHECK(strcmp(keys(&map, pad), "") == 0);                 /* held: no repeat */
    pad.x = 0.3f, pad.y = 0.9f;                              /* inside the dead zone, down */
    CHECK(strcmp(keys(&map, pad), "-4b +50 ") == 0);
    pad.y = 0, pad.buttons = PLAT_PAD_BUTTON_X;              /* face button fires */
    CHECK(strcmp(keys(&map, pad), "-50 +39 ") == 0);
    pad.buttons = 0, pad.rx = -0.4f, pad.ry = -0.4f;         /* right stick deflection fires */
    CHECK(strcmp(keys(&map, pad), "") == 0);
    pad.rx = 0.1f, pad.ry = 0.2f;
    CHECK(strcmp(keys(&map, pad), "-39 ") == 0);
    pad.buttons = PLAT_PAD_BUTTON_START | PLAT_PAD_BUTTON_BACK;
    CHECK(strcmp(keys(&map, pad), "+19 +1f ") == 0);
    pad.connected = false;                                   /* unplugged: everything released */
    CHECK(strcmp(keys(&map, pad), "-19 -1f ") == 0);

    /* Joystick control: only Start / Back are keys. */
    PadMap menu = { 0 };
    PlatGamepad stick = { .connected = true, .x = -1, .buttons = PLAT_PAD_BUTTON_A | PLAT_PAD_BUTTON_START };
    Log log = { "" };
    padmap_menu_keys(&menu, &stick, log_key, &log);
    CHECK(strcmp(log.text, "+19 ") == 0);
}

static void test_pad_sticks(void)
{
    PlatGamepad one = { .connected = true, .x = -1, .y = 0.5f, .rx = 1, .ry = -1,
                        .buttons = PLAT_PAD_BUTTON_A | PLAT_PAD_BUTTON_Y };
    PlatGamepad none = { 0 };
    PadStick s[2];
    padmap_sticks(&one, &none, s);                           /* right stick = joystick 2 */
    CHECK(s[0].x == -1 && s[0].y == 0.5f && s[0].button_a && !s[0].button_b);
    CHECK(s[1].x == 1 && s[1].y == -1 && !s[1].button_a && s[1].button_b);
    PlatGamepad two = { .connected = true, .x = 0.25f, .buttons = PLAT_PAD_BUTTON_B };
    padmap_sticks(&one, &two, s);                            /* second pad = joystick 2 */
    CHECK(s[1].x == 0.25f && s[1].y == 0 && !s[1].button_a && s[1].button_b);
    padmap_sticks(&none, &none, s);
    CHECK(s[0].x == 0 && !s[0].button_a && s[1].x == 0);
}

int main(void)
{
    test_keymap();
    test_pad_keys();
    test_pad_sticks();
    if (g_failures) {
        fprintf(stderr, "controls_selftest: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("controls_selftest: ok\n");
    return 0;
}
