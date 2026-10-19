/*
 * replay_driver.c - runs the port headless on a PROBE.KEY input script and dumps its state
 * as DGROUP images, for comparison with the DOSBox-X reference runs (tools/harness/dosbox,
 * PROBE.SNP). Test-only (tests/, PLAN §3.3); the game itself never sees the DGROUP layout.
 *
 *   llamatron_replay --ticks N [--every K] [--ega|--vga] [--seed BIOS_TICKS]
 *                    [--keys PROBE.KEY] [--out FILE] [--data DIR]
 *
 * Sequencing is the shell's replay mode (src/platform/apps/llamatron_main.c): game_init, then
 * per frame of the fixed 70.086 Hz display clock the due timer ticks (each: the script bytes
 * due at that tick into the keyboard handler, then game_tick) and one game_frame. Ticks count
 * from 1 after game_init, like the probe's IRQs. After every K-th tick (as the probe, after
 * the game's handler) the GameState goes through gamestate_to_dgroup into the output.
 *
 * Output (little endian): "LRPL", u16 version 1, u16 0, u32 every, u32 count,
 * u8 mask[65536] (dgroup_map_mask: which bytes are state), then count x
 * { u32 tick, u8 dgroup[65536] } with unmapped bytes 0. Read by tests/replay/compare.py.
 */
#include "dgroup_map.h"
#include "game.h"
#include "gfx.h"
#include "input.h"
#include "input_script.h"
#include "assets.h"
#include "timing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Replay {
    GameState game;
    InputScript keys;
    bool have_keys;
    uint32_t ticks;             /* run so far */
    uint32_t max_ticks;
    uint32_t every;
    uint32_t dumped;
    FILE *out;
    uint8_t image[DGROUP_SIZE];
} Replay;

static Replay g_replay;

static void put_u32(FILE *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    fwrite(b, 1, sizeof b, f);
}

static void write_header(Replay *r)
{
    fseek(r->out, 0, SEEK_SET);
    fwrite("LRPL\1\0\0\0", 1, 8, r->out);
    put_u32(r->out, r->every);
    put_u32(r->out, r->dumped);
    dgroup_map_mask(r->image);
    fwrite(r->image, 1, sizeof r->image, r->out);
    fseek(r->out, 0, SEEK_END);
}

static void tick_cb(void *user)
{
    Replay *r = user;
    if (r->ticks >= r->max_ticks)
        return;
    r->ticks++;
    const InputScriptEvent *e;
    while (r->have_keys && (e = input_script_take_due(&r->keys, r->ticks)) != NULL)
        input_keyboard_byte(e->scan_code);
    game_tick(&r->game);
    if (r->out && r->every && r->ticks % r->every == 0) {
        memset(r->image, 0, sizeof r->image);
        gamestate_to_dgroup(&r->game, r->image);
        put_u32(r->out, r->ticks);
        fwrite(r->image, 1, sizeof r->image, r->out);
        r->dumped++;
    }
}

static int usage(void)
{
    fprintf(stderr, "usage: llamatron_replay --ticks N [--every K] [--ega|--vga] [--seed T] "
                    "[--keys PROBE.KEY] [--out FILE] [--data DIR]\n");
    return 2;
}

int main(int argc, char **argv)
{
    Replay *r = &g_replay;
    GameLaunch launch = { .vga = true, .joystick = false, .sound = false, .flash_mode = 0 };
    const char *out_path = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (strcmp(a, "--vga") == 0)
            launch.vga = true;
        else if (strcmp(a, "--ega") == 0)
            launch.vga = false;
        else if (strcmp(a, "--ticks") == 0 && v)
            r->max_ticks = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(a, "--every") == 0 && v)
            r->every = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(a, "--seed") == 0 && v)
            launch.bios_ticks = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(a, "--keys") == 0 && v) {
            if (!input_script_load(&r->keys, argv[++i]))
                return 1;
            r->have_keys = true;
        } else if (strcmp(a, "--out") == 0 && v)
            out_path = argv[++i];
        else if (strcmp(a, "--data") == 0 && v)
            assets_set_directory(argv[++i]);
        else
            return usage();
    }
    if (r->max_ticks == 0)
        return usage();
    if (out_path && !(r->out = fopen(out_path, "wb"))) {
        perror(out_path);
        return 1;
    }
    if (r->out)
        write_header(r);

    input_init(INPUT_DEVICE_KEYBOARD, true);
    gfx_init();
    gfx_set_adapter(launch.vga ? GFX_ADAPTER_VGA : GFX_ADAPTER_EGA, NULL);
    if (!game_init(&r->game, &launch)) {
        fprintf(stderr, "llamatron_replay: the game could not start\n");
        return 1;
    }

    TickScheduler sched;
    timing_init_display(&sched, &DISPLAY_VGA_MODE_0D);
    GameHostInput host = { 0, true };
    unsigned long frames = 0;
    while (r->ticks < r->max_ticks && !game_wants_quit(&r->game)) {
        timing_advance_frame(&sched, &DISPLAY_VGA_MODE_0D);
        timing_run_timer_ticks(&sched, tick_cb, r);
        game_frame(&r->game, &host);
        frames++;
    }

    if (r->out) {
        write_header(r);
        fclose(r->out);
    }
    printf("llamatron_replay ticks=%u frames=%lu snapshots=%u quit=%d\n", r->ticks, frames,
           r->dumped, game_wants_quit(&r->game) ? 1 : 0);
    if (r->have_keys)
        input_script_free(&r->keys);
    return 0;
}
