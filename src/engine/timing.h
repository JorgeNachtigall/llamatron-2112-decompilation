/*
 * timing.h - deterministic 50.16 Hz game-tick scheduler.
 *
 * The original runs two unsynchronised clocks (docs/re-notes-game86.md §2):
 *   - PIT channel 0 reprogrammed to divisor 0x5CEC (game86 1000:e519). Its INT 08 handler
 *     (game86 1000:3de4) runs the whole simulation: one "game tick" per PIT period.
 *   - vertical retrace (game86 1000:e5f3), which paces the frame loop (game86 1e7b:013f):
 *     restore backgrounds, draw hook, page flip.
 *
 * PIT input clock: the PC's 14.31818 MHz crystal / 12 = 315/264 MHz = 1,193,181.8 Hz.
 * One game tick = 0x5CEC = 23788 PIT cycles = 23788 * 264 / 315,000,000 s
 *              = 19.9367 ms  ->  50.1590 Hz.
 * The handler also chains to the BIOS INT 08 whenever a 16-bit accumulator advanced by 0x5CEC
 * per tick wraps (game86 1000:3ec2), i.e. every 65536 PIT cycles = 18.2065 Hz, keeping the DOS
 * clock (BIOS tick count 0040:006C) correct.
 *
 * The scheduler measures time in exact integer units so it never drifts:
 *   time is fed in "input units" of 1/input_hz seconds (microseconds for wall-clock mode, or
 *   the dot clock of an emulated display for fixed-refresh mode), and
 *   1 input unit = PIT_CRYSTAL_NUM / (PIT_CRYSTAL_DEN * input_hz) PIT cycles.
 * Internally everything is multiplied by PIT_CRYSTAL_DEN * input_hz, so both "one input unit"
 * and "one tick" are integers.
 *
 * Ticks run only at frame boundaries (PLAN.md §2.4: the original's mid-frame pre-emption is a
 * documented deviation). Nothing here blocks, so it works inside emscripten_set_main_loop.
 */
#ifndef LLAMATRON_TIMING_H
#define LLAMATRON_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PIT input frequency as an exact fraction: 315,000,000 / 264 Hz (= 1,193,181.8 Hz). */
#define PIT_CRYSTAL_NUM 315000000u
#define PIT_CRYSTAL_DEN 264u
/* game86 1000:e519: PIT channel 0, mode 3, divisor 0x5CEC. */
#define PIT_GAME_DIVISOR 0x5CECu
/* BIOS INT 08 runs every 65536 PIT cycles (divisor 0). */
#define PIT_BIOS_DIVISOR 0x10000u

/* Display refresh of the emulated monitor, as frame period = dots_per_frame / dot_clock_hz. */
typedef struct DisplayTiming {
    uint64_t dot_clock_hz;
    uint64_t dots_per_frame;
} DisplayTiming;

/* VGA in mode 0Dh: 25.175 MHz dot clock, 800 x 449 total -> 70.086 Hz. */
extern const DisplayTiming DISPLAY_VGA_MODE_0D;
/* EGA in mode 0Dh on a 200-line monitor: 14.31818 MHz (=315 MHz/22), 912 x 262 dots -> 59.92 Hz.
 * [H] totals taken from the standard EGA BIOS parameter table, not measured. */
extern const DisplayTiming DISPLAY_EGA_MODE_0D;

typedef struct TickScheduler {
    uint64_t input_hz;          /* time units per second fed to timing_advance() */
    uint64_t tick_cost;         /* one game tick in internal units */
    uint64_t pending;           /* accumulated internal units not yet consumed by ticks */
    uint64_t unit_scale;        /* internal units per input unit */
    uint32_t max_ticks_per_advance;

    uint64_t ticks;             /* game ticks run so far (cf. g_tick_count [0x39e6], 16-bit there) */
    uint16_t bios_accumulator;  /* game86 [0x44f] */
    uint32_t bios_ticks;        /* BIOS INT 08 calls the original would have chained */
    uint64_t dropped_ticks;     /* ticks discarded by the catch-up cap */
} TickScheduler;

/* input_hz: resolution of the values given to timing_advance() (1,000,000 for microseconds).
 * max_ticks_per_advance: catch-up cap (avoid spiral of death after a stall); 0 = unlimited. */
void timing_init(TickScheduler *s, uint64_t input_hz, uint32_t max_ticks_per_advance);

/* Wall-clock mode: elapsed time in microseconds. */
void timing_init_realtime(TickScheduler *s);
/* Fixed-refresh mode (replays, tests): every timing_advance(s, display.dots_per_frame) is one
 * emulated retrace. Use timing_advance_frame(). Deterministic, no cap. */
void timing_init_display(TickScheduler *s, const DisplayTiming *display);

/* Add elapsed time; returns how many game ticks are now due. Consume them with
 * timing_take_tick(). */
uint32_t timing_advance(TickScheduler *s, uint64_t elapsed_input_units);
uint32_t timing_advance_frame(TickScheduler *s, const DisplayTiming *display);

/* Returns true and accounts one game tick (and BIOS chaining) if a tick is due. Typical use:
 *     timing_advance(&sched, elapsed_us);
 *     while (timing_take_tick(&sched)) game_tick();
 *     game_frame();
 */
bool timing_take_tick(TickScheduler *s);

/* The rest of the original timer handler after the sound driver tick: game86 1000:3e08..3ec0
 * (tick counter, fire refresh via input_tick_fire(), palette flash, per-tick game hook). */
typedef void (*TimerTickFn)(void *user);

/* Run every due tick the way the original INT 08 handler (game86 1000:3de4) does: first the
 * sound driver tick (AH=2 at 1000:3e02 -> snd_tick()), then `timer` (may be NULL, e.g. fs.exe
 * whose handler only ticks the driver). Returns the number of ticks run. Use this rather
 * than timing_take_tick() so sound and game stay in the original order. */
uint32_t timing_run_timer_ticks(TickScheduler *s, TimerTickFn timer, void *user);

/* Fraction (0..65535) of the next tick already elapsed, e.g. for audio scheduling. */
uint16_t timing_tick_phase(const TickScheduler *s);

/* Exact rates, for display/logging only. */
double timing_tick_hz(void);
double timing_display_hz(const DisplayTiming *display);

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_TIMING_H */
