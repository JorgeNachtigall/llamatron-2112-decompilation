/*
 * controls_selftest.c - host controls give the original's scan codes (ctest controls_selftest):
 * the keys game86 reads (keymap.c) and the gamepad mapping (padmap.c); the host's own keys and
 * the display's scale modes (scale.c).
 */
#include "keymap.h"
#include "padmap.h"
#include "scale.h"

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
    pad.x = 0.3f;                                            /* inside the dead zone, down */
    pad.y = 0.9f;
    CHECK(strcmp(keys(&map, pad), "-4b +50 ") == 0);
    pad.y = 0;                                               /* face button fires */
    pad.buttons = PLAT_PAD_BUTTON_X;
    CHECK(strcmp(keys(&map, pad), "-50 +39 ") == 0);
    pad.buttons = 0;                                         /* right stick deflection fires */
    pad.rx = -0.4f;
    pad.ry = -0.4f;
    CHECK(strcmp(keys(&map, pad), "") == 0);
    pad.rx = 0.1f;
    pad.ry = 0.2f;
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

    /* On the title Start is Enter and Back is F2 (the options screen); a button held across
     * the change is released as its old key and pressed as its new one. */
    PadMap title = { .title = true };
    PlatGamepad back = { .connected = true, .buttons = PLAT_PAD_BUTTON_BACK };
    CHECK(strcmp(keys(&title, back), "+3c ") == 0);
    back.buttons = PLAT_PAD_BUTTON_START;
    CHECK(strcmp(keys(&title, back), "-3c +1c ") == 0);
    title.title = false;
    CHECK(strcmp(keys(&title, back), "-1c +19 ") == 0);
    PadMap menu_title = { .title = true };
    Log log2 = { "" };
    stick.buttons = PLAT_PAD_BUTTON_BACK;
    padmap_menu_keys(&menu_title, &stick, log_key, &log2);
    CHECK(strcmp(log2.text, "+3c ") == 0);
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

static void test_host_keys(void)
{
    CHECK(keymap_host_key(SDL_SCANCODE_F11, KMOD_NONE) == HOST_KEY_FULLSCREEN);
    CHECK(keymap_host_key(SDL_SCANCODE_RETURN, KMOD_LALT) == HOST_KEY_FULLSCREEN);
    CHECK(keymap_host_key(SDL_SCANCODE_RETURN, KMOD_NONE) == HOST_KEY_NONE);
    CHECK(keymap_host_key(SDL_SCANCODE_F10, KMOD_NONE) == HOST_KEY_SCALE_MODE);
    /* The game's own keys stay the game's, also with modifiers. */
    CHECK(keymap_host_key(SDL_SCANCODE_SPACE, KMOD_LALT) == HOST_KEY_NONE);
    CHECK(keymap_host_key(SDL_SCANCODE_P, KMOD_NONE) == HOST_KEY_NONE);
    CHECK(keymap_host_key(SDL_SCANCODE_F, KMOD_NONE) == HOST_KEY_NONE);
    CHECK(keymap_host_key(SDL_SCANCODE_I, KMOD_NONE) == HOST_KEY_NONE);
#ifdef __APPLE__
    CHECK(keymap_host_key(SDL_SCANCODE_F, KMOD_LGUI) == HOST_KEY_FULLSCREEN);
    CHECK(keymap_host_key(SDL_SCANCODE_F, KMOD_LGUI | KMOD_LCTRL) == HOST_KEY_FULLSCREEN);
    CHECK(keymap_host_key(SDL_SCANCODE_I, KMOD_RGUI) == HOST_KEY_SCALE_MODE);
#endif
}

static bool rect_is(ScaleRect r, int x, int y, int w, int h)
{
    if (r.x == x && r.y == y && r.w == w && r.h == h)
        return true;
    fprintf(stderr, "  got %d,%d %dx%d, want %d,%d %dx%d\n", r.x, r.y, r.w, r.h, x, y, w, h);
    return false;
}

static void test_scale(void)
{
    /* 4:3, centred: pillarbox, letterbox, exact. */
    CHECK(rect_is(scale_fit(PLAT_SCALE_NEAREST, 320, 200, 1920, 1080), 240, 0, 1440, 1080));
    CHECK(rect_is(scale_fit(PLAT_SCALE_NEAREST, 320, 200, 1000, 900), 0, 75, 1000, 750));
    CHECK(rect_is(scale_fit(PLAT_SCALE_SHARP, 640, 350, 960, 720), 0, 0, 960, 720));
    /* Integer: 320x200 at 5x6 is exactly 4:3 (1600x1200); a Retina 960x720 window (1920x1440
     * pixels) takes 6x7, the largest within the tolerance; a 1080p screen 4x5. */
    CHECK(rect_is(scale_fit(PLAT_SCALE_INTEGER, 320, 200, 1600, 1200), 0, 0, 1600, 1200));
    CHECK(rect_is(scale_fit(PLAT_SCALE_INTEGER, 320, 200, 1920, 1440), 0, 20, 1920, 1400));
    CHECK(rect_is(scale_fit(PLAT_SCALE_INTEGER, 320, 200, 1920, 1080), 320, 40, 1280, 1000));
    /* The VGA title (320x400, pixels half as tall) and the EGA title (640x350). */
    CHECK(rect_is(scale_fit(PLAT_SCALE_INTEGER, 320, 400, 1600, 1200), 0, 0, 1600, 1200));
    CHECK(rect_is(scale_fit(PLAT_SCALE_INTEGER, 640, 350, 1920, 1440), 0, 20, 1920, 1400));
    /* Too small for whole pixels near 4:3: the 4:3 fit. */
    CHECK(rect_is(scale_fit(PLAT_SCALE_INTEGER, 320, 200, 300, 225), 0, 0, 300, 225));
    CHECK(rect_is(scale_fit(PLAT_SCALE_INTEGER, 320, 200, 1000, 750), 0, 0, 1000, 750));
    CHECK(rect_is(scale_fit(PLAT_SCALE_NEAREST, 320, 200, 0, 0), 0, 0, 0, 0));
    /* Sharp: enlarge to whole multiples at least the size of the 4:3 rectangle. */
    int kx, ky;
    scale_sharp_factors(320, 200, (ScaleRect){ 0, 75, 1000, 750 }, &kx, &ky);
    CHECK(kx == 4 && ky == 4);
    scale_sharp_factors(320, 200, (ScaleRect){ 0, 0, 1600, 1200 }, &kx, &ky);
    CHECK(kx == 5 && ky == 6);
    scale_sharp_factors(320, 200, (ScaleRect){ 0, 0, 200, 150 }, &kx, &ky);
    CHECK(kx == 1 && ky == 1);
}

int main(void)
{
    test_host_keys();
    test_scale();
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
