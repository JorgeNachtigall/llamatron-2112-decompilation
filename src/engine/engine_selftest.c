/*
 * engine_selftest.c - headless checks of the engine (timing math, input decoding, gfx basics).
 * Uses no original data. Run via ctest (engine_selftest).
 */
#include "gfx.h"
#include "input.h"
#include "timing.h"

#include <stdio.h>

static int g_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

static uint64_t run_ticks(TickScheduler *s)
{
    uint64_t n = 0;
    while (timing_take_tick(s))
        n++;
    return n;
}

static void test_timing(void)
{
    /* One hour of wall clock in 1 ms steps = 3600 * 315e6 / (264 * 23788) ticks. */
    TickScheduler s;
    timing_init(&s, 1000000u, 0);
    uint64_t ticks = 0;
    for (int i = 0; i < 3600 * 1000; i++) {
        timing_advance(&s, 1000);
        ticks += run_ticks(&s);
    }
    uint64_t expect = 3600ull * PIT_CRYSTAL_NUM / ((uint64_t)PIT_CRYSTAL_DEN * PIT_GAME_DIVISOR);
    CHECK(ticks == expect);
    CHECK(s.ticks == expect);
    /* BIOS INT 08 chained every 65536 PIT cycles: ticks * 0x5CEC / 0x10000. */
    CHECK(s.bios_ticks == ticks * PIT_GAME_DIVISOR / PIT_BIOS_DIVISOR);

    /* Fixed VGA refresh: 70.086 frames carry exactly 50.162 ticks per second, no drift. */
    TickScheduler v;
    timing_init_display(&v, &DISPLAY_VGA_MODE_0D);
    uint64_t frames = 70086ull * 10;  /* ~10000 s */
    uint64_t vticks = 0;
    for (uint64_t f = 0; f < frames; f++) {
        timing_advance_frame(&v, &DISPLAY_VGA_MODE_0D);
        vticks += run_ticks(&v);
    }
    /* ticks = floor(frames * frame_period / tick_period) */
    unsigned __int128 num = (unsigned __int128)frames * DISPLAY_VGA_MODE_0D.dots_per_frame * PIT_CRYSTAL_NUM;
    unsigned __int128 den = (unsigned __int128)DISPLAY_VGA_MODE_0D.dot_clock_hz * PIT_CRYSTAL_DEN * PIT_GAME_DIVISOR;
    CHECK(vticks == (uint64_t)(num / den));

    /* Per frame at 70 Hz: 0 or 1 ticks, never 2. */
    timing_init_display(&v, &DISPLAY_VGA_MODE_0D);
    for (int f = 0; f < 1000; f++)
        CHECK(timing_advance_frame(&v, &DISPLAY_VGA_MODE_0D) <= 1 && run_ticks(&v) <= 1);

    /* Realtime cap: a 5 s stall yields at most 10 ticks and records the rest as dropped. */
    TickScheduler r;
    timing_init_realtime(&r);
    CHECK(timing_advance(&r, 5000000u) == 10);
    CHECK(run_ticks(&r) == 10);
    CHECK(r.dropped_ticks > 0);

    CHECK(timing_tick_hz() > 50.158 && timing_tick_hz() < 50.160);

    /* Retrace clock: 10000 s of a 60 Hz host (16666/16667 us steps) give floor(10000 * 70.086)
     * VGA retraces, 1 or 2 per host frame; a 120 Hz host gets 0 or 1. */
    RetraceClock rc;
    timing_retrace_init(&rc, &DISPLAY_VGA_MODE_0D, 4);
    uint64_t retraces = 0, us = 0;
    for (int f = 0; f < 600000; f++) {
        uint64_t step = (f % 3 == 2) ? 16666u : 16667u;
        us += step;
        uint32_t due = timing_retraces_due(&rc, step);
        CHECK(due >= 1 && due <= 2);
        retraces += due;
    }
    uint64_t want = us * DISPLAY_VGA_MODE_0D.dot_clock_hz / (DISPLAY_VGA_MODE_0D.dots_per_frame * 1000000u);
    CHECK(retraces == want && rc.dropped_retraces == 0);
    timing_retrace_init(&rc, &DISPLAY_VGA_MODE_0D, 4);
    for (int f = 0; f < 1000; f++)
        CHECK(timing_retraces_due(&rc, 8333u) <= 1);
    /* A stall runs at most the cap and drops the rest. */
    timing_retrace_init(&rc, &DISPLAY_EGA_MODE_0D, 4);
    CHECK(timing_retraces_due(&rc, 3000000u) == 4 && rc.dropped_retraces > 0);
}

static void test_input(void)
{
    input_init(INPUT_DEVICE_KEYBOARD, true);
    InputState *in = input_state();
    input_key_event(PC_KEY_LEFT, true);
    input_key_event(PC_KEY_RIGHT, true);
    input_key_event(PC_KEY_UP, true);
    input_key_event(PC_KEY_DOWN, true);
    CHECK(in->keys_held == INPUT_DIRECTIONS);
    CHECK(in->last_make_code == PC_KEY_DOWN);
    input_frame_read(false);
    CHECK(in->controls[0] == (INPUT_LEFT | INPUT_UP));    /* left beats right, up beats down */
    input_key_event(PC_KEY_LEFT, false);
    CHECK(in->last_scan_code == (PC_KEY_LEFT | PC_KEY_BREAK));
    CHECK(in->last_make_code == PC_KEY_DOWN);              /* breaks do not change it */
    input_frame_read(false);
    CHECK(in->controls[0] == (INPUT_RIGHT | INPUT_UP));

    input_key_event(PC_KEY_SPACE, true);
    input_tick_fire();
    CHECK(in->controls[0] & INPUT_FIRE);
    input_key_event(PC_KEY_SPACE, false);
    input_tick_fire();
    CHECK(!(in->controls[0] & INPUT_FIRE));

    /* Taps inside one host frame (press + release delivered in the same event batch) must be
     * seen once by their consumer, then released. */
    input_init(INPUT_DEVICE_KEYBOARD, true);
    in = input_state();
    input_key_event(PC_KEY_SPACE, true);
    input_key_event(PC_KEY_SPACE, false);
    input_key_event(PC_KEY_LEFT, true);
    input_key_event(PC_KEY_LEFT, false);
    CHECK(in->last_make_code == PC_KEY_LEFT);
    input_frame_read(false);
    CHECK(in->controls[0] & INPUT_LEFT);
    input_tick_fire();
    CHECK(in->controls[0] & INPUT_FIRE);
    input_tick_fire();
    CHECK(!(in->controls[0] & INPUT_FIRE));
    input_frame_read(false);
    CHECK(!(in->controls[0] & INPUT_LEFT));
    CHECK(in->keys_held == 0);
    /* A held key released after it was sampled goes up immediately (original behaviour). */
    input_key_event(PC_KEY_UP, true);
    input_frame_read(false);
    input_key_event(PC_KEY_UP, false);
    CHECK(in->keys_held == 0);
    /* Press, release, press again before a sample: stays held. */
    input_key_event(PC_KEY_DOWN, true);
    input_key_event(PC_KEY_DOWN, false);
    input_key_event(PC_KEY_DOWN, true);
    input_frame_read(false);
    CHECK(in->keys_held == INPUT_DOWN && (in->controls[0] & INPUT_DOWN));

    /* Calibration halvings as in game86 1000:6014. */
    JoystickCalibration c = input_calibrate(101, 100);
    CHECK(c.x_high == 151 && c.x_low == 75);
    CHECK(c.y_high == 150 && c.y_low == 75);

    input_init(INPUT_DEVICE_JOYSTICK, false);
    in = input_state();
    input_set_stick(0, -0.3f, 0.0f, true, false);   /* count 70 <= 75 -> left */
    input_set_stick(1, 0.45f, 0.6f, false, true);   /* x 145 < 150: none; y 160 -> down */
    input_frame_read(false);
    CHECK(in->controls[0] == (INPUT_LEFT | INPUT_FIRE));
    CHECK(in->controls[1] == (INPUT_DOWN | INPUT_FIRE));

    input_init(INPUT_DEVICE_JOYSTICK, true);        /* only one stick: stick 2 ignored */
    in = input_state();
    input_set_stick(1, 1.0f, 1.0f, false, false);
    input_frame_read(false);
    CHECK(in->controls[1] == 0);
    input_frame_read(true);
    CHECK(in->controls[1] == (INPUT_RIGHT | INPUT_DOWN));
}

static void test_gfx(void)
{
    gfx_init();
    CHECK(gfx_draw_page_id() == GFX_PAGE_1 && gfx_display_page_id() == GFX_PAGE_0);
    gfx_flip();
    CHECK(gfx_draw_page_id() == GFX_PAGE_0 && gfx_display_page_id() == GFX_PAGE_1);

    gfx_clear(GFX_PAGE_BACKGROUND, 3);
    gfx_fill_rect(GFX_PAGE_0, (GfxRect){ -5, -5, 10, 10 }, 9);
    CHECK(gfx_page(GFX_PAGE_0)->pixels[4][4] == 9);
    CHECK(gfx_page(GFX_PAGE_0)->pixels[5][5] == 0);
    gfx_mark_dirty(GFX_PAGE_0, (GfxRect){ 0, 0, 5, 5 });
    gfx_restore_dirty(GFX_PAGE_0);
    CHECK(gfx_page(GFX_PAGE_0)->pixels[4][4] == 3);
    gfx_fill_rect(GFX_PAGE_0, (GfxRect){ 0, 0, 5, 5 }, 9);
    gfx_mark_dirty(GFX_PAGE_0, (GfxRect){ 0, 0, 5, 5 });
    gfx_discard_dirty(GFX_PAGE_0);
    gfx_restore_dirty(GFX_PAGE_0);                          /* nothing left to restore */
    CHECK(gfx_page(GFX_PAGE_0)->pixels[4][4] == 9);

    /* Restore spans are linear in page memory: wrap to the next row, clamp at the page end. */
    gfx_clear(GFX_PAGE_BACKGROUND, 3);
    gfx_clear(GFX_PAGE_0, 0);
    gfx_restore_rect(GFX_PAGE_0, (GfxRect){ GFX_WIDTH - 4, 10, 8, 2 });
    CHECK(gfx_page(GFX_PAGE_0)->pixels[10][GFX_WIDTH - 1] == 3);
    CHECK(gfx_page(GFX_PAGE_0)->pixels[11][3] == 3);       /* row 10's span ran on into row 11 */
    CHECK(gfx_page(GFX_PAGE_0)->pixels[11][4] == 0);
    CHECK(gfx_page(GFX_PAGE_0)->pixels[12][3] == 3);       /* row 11's span */
    CHECK(gfx_page(GFX_PAGE_0)->pixels[12][4] == 0);
    gfx_restore_rect(GFX_PAGE_0, (GfxRect){ GFX_WIDTH - 2, GFX_HEIGHT - 1, 16, 4 });  /* clamped */
    CHECK(gfx_page(GFX_PAGE_0)->pixels[GFX_HEIGHT - 1][GFX_WIDTH - 1] == 3);
    gfx_clear(GFX_PAGE_BACKGROUND, 3);

    /* Without a mask, GFX_TRANSPARENT pixels are skipped. */
    static const uint8_t keyed[2] = { 5, GFX_TRANSPARENT };
    GfxSprite k = { 2, 1, 0, 0, keyed, NULL };
    gfx_clear(GFX_PAGE_1, 9);
    gfx_draw_sprite(GFX_PAGE_1, &k, 10, 10, GFX_DRAW_NORMAL);
    CHECK(gfx_page(GFX_PAGE_1)->pixels[10][10] == 5 && gfx_page(GFX_PAGE_1)->pixels[10][11] == 9);

    static const uint8_t px[2 * 3] = { 1, 2, 3, 4, 5, 6 };
    static const uint8_t mask[2 * 3] = { 1, 0, 1, 1, 1, 1 };
    GfxSprite s = { 3, 2, 1, 1, px, mask };
    gfx_clear(GFX_PAGE_1, 0);
    GfxRect r = gfx_draw_sprite(GFX_PAGE_1, &s, 0, 0, GFX_DRAW_MIRROR_X);  /* clipped at top-left */
    CHECK(r.x == 0 && r.y == 0 && r.w == 2 && r.h == 1);
    /* Visible part = sprite row 1, columns 1..2, mirrored: 5 then 4. */
    CHECK(gfx_page(GFX_PAGE_1)->pixels[0][0] == 5);
    CHECK(gfx_page(GFX_PAGE_1)->pixels[0][1] == 4);

    uint8_t levels[16];
    for (int i = 0; i < 16; i++)
        levels[i] = (uint8_t)(i * 4);
    uint16_t words[16] = { 0xf80 };
    gfx_set_palette_st(words, levels);
    CHECK(gfx_palette()[0].r == ((60 << 2) | (60 >> 4)) && gfx_palette()[0].g == ((32 << 2) | (32 >> 4)));
    GfxColor brown = gfx_ega_color(0x14);  /* EGA 20 = brown (0xAA,0x55,0x00) */
    CHECK(brown.r == 0xaa && brown.g == 0x55 && brown.b == 0x00);
}

int main(void)
{
    test_timing();
    test_input();
    test_gfx();
    if (g_failures) {
        fprintf(stderr, "engine_selftest: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("engine_selftest: ok\n");
    return 0;
}
