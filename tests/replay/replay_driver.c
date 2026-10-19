/*
 * replay_driver.c - runs the port headless on a PROBE.KEY input script and dumps its state
 * as DGROUP images, for comparison with the DOSBox-X reference runs (tools/harness/dosbox,
 * PROBE.SNP). Test-only (tests/, PLAN §3.3); the game itself never sees the DGROUP layout.
 *
 *   llamatron_replay --ticks N [--every K] [--ega|--vga] [--seed BIOS_TICKS]
 *                    [--keys PROBE.KEY] [--pokes PROBE.POK] [--out FILE] [--data DIR]
 *
 * Sequencing is the shell's replay mode (src/platform/apps/llamatron_main.c): game_init, then
 * per frame of the fixed display clock (VGA mode 0Dh 70.086 Hz, or the EGA monitor's
 * 59.92 Hz with --ega, as the reference run's machine) the due timer ticks (each: the script bytes
 * due at that tick into the keyboard handler, then game_tick) and one game_frame. Ticks count
 * from 1 after game_init, like the probe's IRQs. After every K-th tick (as the probe, after
 * the game's handler) the GameState goes through gamestate_to_dgroup into the output.
 *
 * PROBE.POK (the probe's scripted state changes, {u32 tick, u16 DS offset, u16 word}) is
 * applied before the keys of its tick, like the probe does: the GameState goes out through
 * gamestate_to_dgroup, the word is stored, and gamestate_from_dgroup reads it back. Only
 * offsets the adapter maps can be poked.
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

typedef struct Poke {
    uint32_t tick;
    uint16_t offset;
    uint16_t value;
} Poke;

typedef struct Replay {
    GameState game;
    InputScript keys;
    bool have_keys;
    Poke *pokes;
    size_t poke_count;
    size_t poke_next;
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

static bool load_pokes(Replay *r, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return false;
    }
    uint8_t rec[8];
    uint8_t *mask = r->image;
    dgroup_map_mask(mask);
    bool ok = true;
    while (ok && fread(rec, 1, sizeof rec, f) == sizeof rec) {
        Poke p = { (uint32_t)rec[0] | (uint32_t)rec[1] << 8 | (uint32_t)rec[2] << 16
                       | (uint32_t)rec[3] << 24,
                   (uint16_t)(rec[4] | rec[5] << 8), (uint16_t)(rec[6] | rec[7] << 8) };
        if (p.offset == 0xFFFF || mask[p.offset] == DGROUP_MASK_UNMAPPED
            || mask[p.offset + 1] == DGROUP_MASK_UNMAPPED) {
            fprintf(stderr, "%s: DS:%04x is not mapped by the adapter\n", path, p.offset);
            ok = false;
        } else if (r->poke_count && p.tick < r->pokes[r->poke_count - 1].tick) {
            fprintf(stderr, "%s: records not sorted by tick\n", path);
            ok = false;
        } else {
            Poke *grown = realloc(r->pokes, (r->poke_count + 1) * sizeof *grown);
            if (!grown) {
                ok = false;
                break;
            }
            r->pokes = grown;
            r->pokes[r->poke_count++] = p;
        }
    }
    fclose(f);
    return ok;
}

static void apply_pokes(Replay *r)
{
    while (r->poke_next < r->poke_count && r->pokes[r->poke_next].tick <= r->ticks) {
        const Poke *p = &r->pokes[r->poke_next++];
        memset(r->image, 0, sizeof r->image);
        gamestate_to_dgroup(&r->game, r->image);
        r->image[p->offset] = (uint8_t)p->value;
        r->image[p->offset + 1] = (uint8_t)(p->value >> 8);
        gamestate_from_dgroup(&r->game, r->image);
    }
}

static void tick_cb(void *user)
{
    Replay *r = user;
    if (r->ticks >= r->max_ticks)
        return;
    r->ticks++;
    apply_pokes(r);
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
                    "[--keys PROBE.KEY] [--pokes PROBE.POK] [--out FILE] [--data DIR]\n");
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
        } else if (strcmp(a, "--pokes") == 0 && v) {
            if (!load_pokes(r, argv[++i]))
                return 1;
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

    const DisplayTiming *display = launch.vga ? &DISPLAY_VGA_MODE_0D : &DISPLAY_EGA_MODE_0D;
    TickScheduler sched;
    timing_init_display(&sched, display);
    GameHostInput host = { 0, true };
    unsigned long frames = 0;
    while (r->ticks < r->max_ticks && !game_wants_quit(&r->game)) {
        timing_advance_frame(&sched, display);
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
    free(r->pokes);
    return 0;
}
