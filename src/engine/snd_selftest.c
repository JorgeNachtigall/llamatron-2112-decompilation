/*
 * snd_selftest.c - driver-rule tests for snd.c with a synthetic sample bank (no audio device,
 * no original data). Run via ctest (snd_selftest).
 */
#include "snd.h"

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

/* One game tick in microseconds: 23788 * 264 / 315. */
#define TICK_US (23788.0 * 264.0 / 315.0)

static uint8_t g_pcm[4096];
static SoundBank g_bank;

/* Sample n: (n * 100) bytes of value n, laid out back to back (n = 1..8 used). */
static void make_bank(void)
{
    memset(&g_bank, 0, sizeof g_bank);
    size_t at = 0;
    for (int n = 1; n <= SND_SAMPLE_COUNT; n++) {
        uint32_t len = n <= 8 ? (uint32_t)n * 100 : 10;
        g_bank.samples[n - 1].offset = (uint32_t)at;
        g_bank.samples[n - 1].length = len;
        memset(g_pcm + at, n, len);
        at += len;
    }
    g_bank.pcm = g_pcm;
    g_bank.pcm_size = at;
    static const uint8_t tc[SND_RATE_COUNT] = { 0x01, 0x34, 0x7e, 0x98, 0xce };
    memcpy(g_bank.time_constants, tc, sizeof tc);
}

static SoundStatus st(void) { return snd_status(); }

/* Ticks until the playing sample is idle (bounded). */
static int ticks_until_idle(void)
{
    int n = 0;
    while (st().playing && n < 10000) {
        snd_tick();
        n++;
    }
    return n;
}

static void test_latching_and_duration(void)
{
    snd_init(&g_bank, 0);
    snd_play(2, 1, 5);
    CHECK(st().playing == 0);                    /* nothing until the tick */
    snd_tick();
    CHECK(st().playing == 2 && st().priority == 5);
    /* 200 bytes at TC 0x34 = 204 us each = 40800 us; ends at the first tick >= that. */
    CHECK(snd_sample_duration_us(2, 1) == 40800);
    int expected = 0;
    while (expected * TICK_US < 40800.0)
        expected++;
    CHECK(ticks_until_idle() == expected);
    CHECK(st().priority == 5);                   /* sticky after the end */
}

static void test_priority_rules(void)
{
    snd_init(&g_bank, 0);
    snd_play(8, 0, 5);                           /* long: 800 * 255 us */
    snd_tick();
    snd_play(3, 1, 4);                           /* lower: waits */
    snd_tick();
    CHECK(st().playing == 8);
    snd_play(4, 1, 4);                           /* latest request overwrites the slot */
    /* The waiting sample starts at the same tick that sees the end of the current one. */
    int ticks = 1;
    while (st().playing == 8 && ticks < 10000) {
        snd_tick();
        ticks++;
    }
    int expected = 0;
    while (expected * TICK_US < (double)snd_sample_duration_us(8, 0))
        expected++;
    CHECK(ticks == expected);
    CHECK(st().playing == 4 && st().priority == 4);

    snd_init(&g_bank, 0);
    snd_play(8, 0, 5);
    snd_tick();
    snd_play(3, 1, 5);                           /* equal priority pre-empts at the next tick */
    snd_tick();
    CHECK(st().playing == 3 && st().priority == 5);

    snd_init(&g_bank, 0);
    snd_play(8, 0, 5);
    snd_tick();
    snd_play(3, 1, 2);
    snd_play(0, 1, 2);                           /* invalid request cancels the pending one */
    CHECK(ticks_until_idle() > 0);
    CHECK(st().playing == 0);
}

static void test_stops(void)
{
    snd_init(&g_bank, 0);
    snd_play(8, 0, 5);
    snd_tick();
    snd_stop(3);                                 /* not playing: stays armed */
    snd_tick();
    CHECK(st().playing == 8);
    snd_stop_all();
    snd_tick();
    CHECK(st().playing == 0);

    snd_init(&g_bank, 0);
    snd_stop(3);                                 /* armed while idle ... */
    snd_play(3, 1, 1);
    snd_tick();
    CHECK(st().playing == 3);                    /* ... starts (stop runs before the promote) */
    snd_tick();
    CHECK(st().playing == 0);                    /* ... and is stopped on the next tick */

    snd_init(&g_bank, 0);
    snd_play(8, 0, 5);
    snd_tick();
    snd_stop(8);
    snd_tick();
    CHECK(st().playing == 0);
}

static void test_disabled(void)
{
    snd_init(&g_bank, 0);
    snd_set_enabled(false);
    snd_play(2, 1, 5);
    snd_tick();
    CHECK(st().playing == 0 && st().priority == 0);
    snd_set_enabled(true);
    snd_tick();
    CHECK(st().playing == 0);                    /* the request never reached the driver */
}

static void test_output(void)
{
    enum { RATE = 22050 };
    snd_init(&g_bank, RATE);
    snd_play(1, 4, 1);                           /* 100 bytes at 20000 Hz = 5 ms */
    uint8_t out[RATE];
    size_t total = 0;
    for (int i = 0; i < 50; i++)
        snd_tick();
    total = snd_take_output(out, sizeof out);
    /* 50 ticks = 0.99682 s -> 21980 or 21981 output samples. */
    CHECK(total >= 21979 && total <= 21982);
    /* Silence in the first tick-latency, then 5 ms of value 1 (= 110 output samples), then silence. */
    size_t ones = 0, first = total;
    for (size_t i = 0; i < total; i++) {
        if (out[i] == 1) {
            ones++;
            if (first == total)
                first = i;
        } else {
            CHECK(out[i] == SND_SILENCE);
        }
    }
    CHECK(ones >= 109 && ones <= 111);
    CHECK(first == 0);                           /* started at the first tick (time 0) */
    CHECK(snd_output_pending() == 0);
}

int main(void)
{
    make_bank();
    test_latching_and_duration();
    test_priority_rules();
    test_stops();
    test_disabled();
    test_output();
    if (g_failures) {
        fprintf(stderr, "snd_selftest: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("snd_selftest: ok\n");
    return 0;
}
