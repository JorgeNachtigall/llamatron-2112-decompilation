/*
 * testpattern.c - platform/engine bring-up app. Uses no original data.
 *
 * Shows a generated test pattern through the real engine path: background page, page flip,
 * dirty-rectangle restore, sprite drawing, palette and the 50.16 Hz tick scheduler.
 *
 *   arrows / gamepad : move the cursor sprite (moved in game ticks, drawn per frame)
 *   space / pad A    : each press rotates the palette of colours 1..15
 *   T                : cycle display size 320x200 -> 320x400 -> 640x350 (title screen formats)
 *   E                : toggle EGA / VGA palette path
 *   F11 / Alt-Enter  : fullscreen;  Esc / Q : quit
 *
 * Command line: --frames N  run N frames with a fixed 70.086 Hz display, print the tick count
 *               and a hash of the last displayed page, then exit (smoke test).
 */
#include "gfx.h"
#include "input.h"
#include "platform.h"
#include "timing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

enum { SPRITE_SIZE = 16, BIG_W = 640, BIG_H = 400 };

typedef enum DisplayFormat {
    FORMAT_GAME_320X200,
    FORMAT_TITLE_320X400,
    FORMAT_TITLE_640X350,
    FORMAT_COUNT
} DisplayFormat;

typedef struct App {
    TickScheduler scheduler;
    uint64_t last_us;
    bool fixed_step;
    long frames_left;
    uint64_t frames;

    int cursor_x, cursor_y;
    uint8_t palette_phase;
    bool fire_was_down;
    DisplayFormat format;

    uint8_t sprite_pixels[SPRITE_SIZE * SPRITE_SIZE];
    uint8_t sprite_mask[SPRITE_SIZE * SPRITE_SIZE];
    GfxSprite sprite;

    uint8_t big_image[BIG_W * BIG_H];
    GfxColor big_palette[GFX_PALETTE_SIZE];
} App;

/* A made-up 16-colour palette (ST-style words) and a linear nibble->DAC table. */
static const uint16_t TEST_ST_WORDS[GFX_GAME_COLORS] = {
    0x000, 0xf00, 0x0f0, 0x00f, 0xff0, 0x0ff, 0xf0f, 0xfff,
    0x444, 0x800, 0x080, 0x008, 0x880, 0x088, 0x808, 0xaaa,
};
static const uint8_t TEST_EGA_REGS[GFX_GAME_COLORS + 1] = {
    0x00, 0x04, 0x02, 0x01, 0x06, 0x03, 0x05, 0x07,
    0x38, 0x3c, 0x3a, 0x39, 0x3e, 0x3b, 0x3d, 0x3f, 0x00,
};

static GfxGamePalette g_test_palette;
static uint8_t g_nibble_to_dac[16];

static void build_palette(App *app)
{
    for (int i = 0; i < GFX_GAME_COLORS; i++) {
        int src = i == 0 ? 0 : 1 + (i - 1 + app->palette_phase) % 15;
        g_test_palette.st_words[i] = TEST_ST_WORDS[src];
        g_test_palette.ega_registers[i] = TEST_EGA_REGS[src];
    }
    gfx_set_game_palette(&g_test_palette);

    /* 16..255: a 240-step colour ramp so the 256-colour formats have something to show. */
    for (int i = GFX_GAME_COLORS; i < GFX_PALETTE_SIZE; i++) {
        int t = i - GFX_GAME_COLORS;
        GfxColor c = { (uint8_t)(t * 255 / 239), (uint8_t)((t * 3) & 0xff), (uint8_t)(255 - t * 255 / 239) };
        gfx_set_palette(i, 1, &c);
    }
}

static void build_background(void)
{
    GfxPage *bg = gfx_page(GFX_PAGE_BACKGROUND);
    for (int y = 0; y < GFX_HEIGHT; y++)
        for (int x = 0; x < GFX_WIDTH; x++)
            bg->pixels[y][x] = (uint8_t)(((x / 20) ^ (y / 25)) & 1 ? 8 : 0);

    for (int i = 0; i < GFX_GAME_COLORS; i++)          /* colour bars along the top */
        gfx_fill_rect(GFX_PAGE_BACKGROUND, (GfxRect){ (int16_t)(i * 20), 0, 20, 24 }, (uint8_t)i);
    gfx_fill_rect(GFX_PAGE_BACKGROUND, (GfxRect){ 0, 0, GFX_WIDTH, 1 }, 7);   /* frame */
    gfx_fill_rect(GFX_PAGE_BACKGROUND, (GfxRect){ 0, GFX_HEIGHT - 1, GFX_WIDTH, 1 }, 7);
    gfx_fill_rect(GFX_PAGE_BACKGROUND, (GfxRect){ 0, 0, 1, GFX_HEIGHT }, 7);
    gfx_fill_rect(GFX_PAGE_BACKGROUND, (GfxRect){ GFX_WIDTH - 1, 0, 1, GFX_HEIGHT }, 7);
    for (int x = 0; x < GFX_WIDTH; x += 2)               /* 1-pixel checker: scaling check */
        for (int y = 180; y < 190; y++)
            bg->pixels[y][x + (y & 1)] = 15;

    gfx_copy_page(GFX_PAGE_0, GFX_PAGE_BACKGROUND);
    gfx_copy_page(GFX_PAGE_1, GFX_PAGE_BACKGROUND);
}

static void build_sprite(App *app)
{
    for (int y = 0; y < SPRITE_SIZE; y++) {
        for (int x = 0; x < SPRITE_SIZE; x++) {
            int dx = 2 * x - (SPRITE_SIZE - 1), dy = 2 * y - (SPRITE_SIZE - 1);
            int d2 = dx * dx + dy * dy;
            bool inside = d2 <= (SPRITE_SIZE - 1) * (SPRITE_SIZE - 1);
            app->sprite_mask[y * SPRITE_SIZE + x] = inside;
            app->sprite_pixels[y * SPRITE_SIZE + x] = (uint8_t)(d2 < 40 ? 7 : (x < SPRITE_SIZE / 2 ? 4 : 1));
        }
    }
    app->sprite = (GfxSprite){ SPRITE_SIZE, SPRITE_SIZE, SPRITE_SIZE / 2, SPRITE_SIZE / 2,
                               app->sprite_pixels, app->sprite_mask };
}

static void build_big_image(App *app, int w, int h)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            app->big_image[y * w + x] = (uint8_t)(GFX_GAME_COLORS + (x * 240 / w + y) % 240);
    for (int x = 0; x < w; x++) {                       /* white border */
        app->big_image[x] = 7;
        app->big_image[(h - 1) * w + x] = 7;
    }
    for (int y = 0; y < h; y++) {
        app->big_image[y * w] = 7;
        app->big_image[y * w + w - 1] = 7;
    }
}

static void on_key(uint8_t code, bool pressed, void *user)
{
    App *app = user;
    input_key_event(code, pressed);
    if (!pressed)
        return;
    if (code == 0x14) {                                 /* T */
        app->format = (DisplayFormat)((app->format + 1) % FORMAT_COUNT);
        if (app->format == FORMAT_TITLE_320X400)
            build_big_image(app, 320, 400);
        else if (app->format == FORMAT_TITLE_640X350)
            build_big_image(app, 640, 350);
    } else if (code == 0x12) {                          /* E */
        gfx_set_adapter(gfx_adapter() == GFX_ADAPTER_VGA ? GFX_ADAPTER_EGA : GFX_ADAPTER_VGA, g_nibble_to_dac);
        build_palette(app);
    }
}

/* One game tick (what the original does in INT 08). */
static void game_tick(App *app)
{
    input_tick_fire();
    const InputState *in = input_state();
    uint8_t c = in->controls[0];
    if (c & INPUT_LEFT) app->cursor_x -= 2;
    if (c & INPUT_RIGHT) app->cursor_x += 2;
    if (c & INPUT_UP) app->cursor_y -= 2;
    if (c & INPUT_DOWN) app->cursor_y += 2;
    if (app->cursor_x < 0) app->cursor_x = 0;
    if (app->cursor_x > GFX_WIDTH - 1) app->cursor_x = GFX_WIDTH - 1;
    if (app->cursor_y < 0) app->cursor_y = 0;
    if (app->cursor_y > GFX_HEIGHT - 1) app->cursor_y = GFX_HEIGHT - 1;
    bool fire_pressed = (c & INPUT_FIRE) && !app->fire_was_down;
    app->fire_was_down = (c & INPUT_FIRE) != 0;
    if (fire_pressed) {
        app->palette_phase = (uint8_t)((app->palette_phase + 1) % 15);
        build_palette(app);
    }
}

static void game_tick_cb(void *user)
{
    game_tick(user);
}

/* One display frame (what the original's frame loop does). */
static void game_frame(App *app)
{
    GfxPageId page = gfx_draw_page_id();
    gfx_restore_dirty(page);

    gfx_mark_dirty(page, gfx_draw_sprite(page, &app->sprite, app->cursor_x, app->cursor_y, GFX_DRAW_NORMAL));

    /* Tick indicator: a dot that walks one pixel per game tick along row 30. */
    int tx = (int)(app->scheduler.ticks % GFX_WIDTH);
    GfxRect dot = { (int16_t)tx, 30, 1, 4 };
    gfx_fill_rect(page, dot, 15);
    gfx_mark_dirty(page, dot);

    gfx_flip();
}

static uint32_t fnv1a(const uint8_t *data, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++)
        h = (h ^ data[i]) * 16777619u;
    return h;
}

static void present(App *app)
{
    const GfxColor *pal = gfx_palette();
    PlatColor plat_pal[GFX_PALETTE_SIZE];
    for (int i = 0; i < GFX_PALETTE_SIZE; i++)
        plat_pal[i] = (PlatColor){ pal[i].r, pal[i].g, pal[i].b };
    switch (app->format) {
    case FORMAT_GAME_320X200:
        plat_present(gfx_display_pixels(), GFX_WIDTH, GFX_HEIGHT, GFX_WIDTH, plat_pal);
        break;
    case FORMAT_TITLE_320X400:
        plat_present(app->big_image, 320, 400, 320, plat_pal);
        break;
    case FORMAT_TITLE_640X350:
        plat_present(app->big_image, 640, 350, 640, plat_pal);
        break;
    default:
        break;
    }
}

static bool frame(void *user)
{
    App *app = user;
    bool running = plat_poll_events(on_key, app);
    const InputState *in = input_state();
    if (in->last_make_code == PC_KEY_ESCAPE || in->last_make_code == PC_KEY_Q)
        running = false;

    PlatGamepad pad;
    plat_gamepad(0, &pad);
    if (pad.connected && input_state()->device == INPUT_DEVICE_JOYSTICK)
        input_set_stick(0, pad.x, pad.y, pad.buttons & PLAT_PAD_BUTTON_A, pad.buttons & PLAT_PAD_BUTTON_B);
    input_frame_read(false);

    if (app->fixed_step) {
        timing_advance_frame(&app->scheduler, &DISPLAY_VGA_MODE_0D);
    } else {
        uint64_t now = plat_time_us();
        timing_advance(&app->scheduler, now - app->last_us);
        app->last_us = now;
    }
    timing_run_timer_ticks(&app->scheduler, game_tick_cb, app);
    game_frame(app);
    present(app);
    app->frames++;

    if (app->frames_left > 0 && --app->frames_left == 0) {
        printf("frames=%llu ticks=%llu display_hash=%08x palette_phase=%u\n", (unsigned long long)app->frames,
               (unsigned long long)app->scheduler.ticks, fnv1a(gfx_display_pixels(), GFX_WIDTH * GFX_HEIGHT),
               app->palette_phase);
        running = false;
    }
    if (!running)
        plat_shutdown();   /* under Emscripten nothing after plat_run_main_loop runs */
    return running;
}

static App g_app;

int main(int argc, char **argv)
{
    App *app = &g_app;
    bool joystick = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            app->frames_left = strtol(argv[++i], NULL, 10);
            app->fixed_step = true;
        } else if (strcmp(argv[i], "--joystick") == 0) {
            joystick = true;
        }
    }

#ifdef __EMSCRIPTEN__
    /* The page URL stands in for the command line: ?frames=N */
    const char *frames = strstr(emscripten_run_script_string("location.search"), "frames=");
    if (frames) {
        app->frames_left = strtol(frames + 7, NULL, 10);
        app->fixed_step = true;
    }
#endif

    PlatConfig config = { "Llamatron platform test pattern", 3, false, true, 0 };
    if (app->frames_left > 0)
        config.web_timer_fps = 70;   /* deterministic frame pacing for test runs */
    if (!plat_init(&config))
        return 1;

    gfx_init();
    input_init(joystick ? INPUT_DEVICE_JOYSTICK : INPUT_DEVICE_KEYBOARD, true);
    for (int i = 0; i < 16; i++)
        g_nibble_to_dac[i] = (uint8_t)(i * 63 / 15);
    gfx_set_adapter(GFX_ADAPTER_VGA, g_nibble_to_dac);
    build_palette(app);
    build_background();
    build_sprite(app);
    app->cursor_x = GFX_WIDTH / 2;
    app->cursor_y = GFX_HEIGHT / 2;

    if (app->fixed_step)
        timing_init_display(&app->scheduler, &DISPLAY_VGA_MODE_0D);
    else
        timing_init_realtime(&app->scheduler);
    app->last_us = plat_time_us();

    printf("tick %.4f Hz, VGA %.3f Hz, EGA %.3f Hz\n", timing_tick_hz(),
           timing_display_hz(&DISPLAY_VGA_MODE_0D), timing_display_hz(&DISPLAY_EGA_MODE_0D));
    plat_run_main_loop(frame, app);
    return 0;
}
