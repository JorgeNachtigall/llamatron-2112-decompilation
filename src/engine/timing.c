/*
 * timing.c - deterministic 50.16 Hz game-tick scheduler (see timing.h for the PIT math).
 */
#include "timing.h"

#include "snd.h"

const DisplayTiming DISPLAY_VGA_MODE_0D = { 25175000u, 800u * 449u };
/* Dot clock 315,000,000/22 Hz is not an integer, so scale both terms by 22. */
const DisplayTiming DISPLAY_EGA_MODE_0D = { 315000000u, 912u * 262u * 22u };

/* Realtime mode never catches up more than this many ticks in one frame (~0.2 s). */
enum { REALTIME_MAX_TICKS_PER_ADVANCE = 10 };

static uint64_t gcd_u64(uint64_t a, uint64_t b)
{
    while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

void timing_init(TickScheduler *s, uint64_t input_hz, uint32_t max_ticks_per_advance)
{
    /* One input unit = PIT_CRYSTAL_NUM / (PIT_CRYSTAL_DEN * input_hz) PIT cycles. Measure in
     * 1/(PIT_CRYSTAL_DEN * input_hz) PIT cycles: an input unit is then PIT_CRYSTAL_NUM units and
     * a tick is PIT_GAME_DIVISOR * PIT_CRYSTAL_DEN * input_hz units. Divide out the common factor. */
    uint64_t unit_scale = PIT_CRYSTAL_NUM;
    uint64_t tick_cost = (uint64_t)PIT_GAME_DIVISOR * PIT_CRYSTAL_DEN * input_hz;
    uint64_t g = gcd_u64(unit_scale, tick_cost);

    *s = (TickScheduler){ 0 };
    s->input_hz = input_hz;
    s->unit_scale = unit_scale / g;
    s->tick_cost = tick_cost / g;
    s->max_ticks_per_advance = max_ticks_per_advance;
}

void timing_init_realtime(TickScheduler *s)
{
    timing_init(s, 1000000u, REALTIME_MAX_TICKS_PER_ADVANCE);
}

void timing_init_display(TickScheduler *s, const DisplayTiming *display)
{
    timing_init(s, display->dot_clock_hz, 0);
}

uint32_t timing_advance(TickScheduler *s, uint64_t elapsed)
{
    /* Bound the input so the multiplication cannot overflow even with no cap (a full second of
     * input units is always far below the limit). */
    uint64_t limit = s->input_hz;
    if (s->max_ticks_per_advance == 0 && elapsed > limit)
        elapsed = limit;
    if (s->max_ticks_per_advance != 0) {
        uint64_t cap_units = (uint64_t)(s->max_ticks_per_advance + 1) * s->tick_cost / s->unit_scale + 1;
        if (elapsed > cap_units)
            elapsed = cap_units;
    }
    s->pending += elapsed * s->unit_scale;

    uint64_t due = s->pending / s->tick_cost;
    if (s->max_ticks_per_advance != 0 && due > s->max_ticks_per_advance) {
        uint64_t excess = due - s->max_ticks_per_advance;
        s->pending -= excess * s->tick_cost;
        s->dropped_ticks += excess;
        due = s->max_ticks_per_advance;
    }
    return (uint32_t)due;
}

uint32_t timing_advance_frame(TickScheduler *s, const DisplayTiming *display)
{
    return timing_advance(s, display->dots_per_frame);
}

/* game86 1000:3de4 (tick count) and 1000:3ec2 (BIOS chaining) */
bool timing_take_tick(TickScheduler *s)
{
    if (s->pending < s->tick_cost)
        return false;
    s->pending -= s->tick_cost;
    s->ticks++;
    uint16_t before = s->bios_accumulator;
    s->bios_accumulator = (uint16_t)(before + PIT_GAME_DIVISOR);
    if (s->bios_accumulator < before)
        s->bios_ticks++;
    return true;
}

/* game86 1000:3de4 */
uint32_t timing_run_timer_ticks(TickScheduler *s, TimerTickFn timer, void *user)
{
    uint32_t n = 0;
    while (timing_take_tick(s)) {
        snd_tick();                 /* 1000:3e02: driver AH=2 */
        if (timer)
            timer(user);            /* 1000:3e08..: counter, fire, flash, game hook */
        n++;
    }
    return n;
}

uint16_t timing_tick_phase(const TickScheduler *s)
{
    uint64_t within = s->pending % s->tick_cost;
    return (uint16_t)(within * 65536u / s->tick_cost);
}

double timing_tick_hz(void)
{
    return (double)PIT_CRYSTAL_NUM / PIT_CRYSTAL_DEN / PIT_GAME_DIVISOR;
}

double timing_display_hz(const DisplayTiming *display)
{
    return (double)display->dot_clock_hz / (double)display->dots_per_frame;
}
