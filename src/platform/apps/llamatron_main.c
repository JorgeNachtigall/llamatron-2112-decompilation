/*
 * llamatron_main.c - the game executable: the title screen (fs.exe), then the game (game86)
 * through its entry API (src/game/game.h). No game logic here: the shell only sequences the two
 * and connects the platform services (window, audio, input, settings, host requests).
 *
 *   llamatron [--vga|--ega] [--keyboard|--joystick] [--flash N|P|Y] [--mute] [--data DIR]
 *             [--frames N] [--start-after K] [--seed BIOS_TICKS]
 *             [--record FILE | --replay FILE | --keys T:SC,...] [--page-hashes T1,T2,...]
 *             [--dump-pages DIR --dump-ticks T1,T2,...]
 *
 * Settings come from the saved llama.inf (settings.h); the options here override them for this
 * run only. The game gets them as game86 did, on its command tail (GameLaunch); the RNG seed is
 * the BIOS tick count of the local time of day, like the original (--seed pins it). As in the
 * original, the title always plays its sample and the game follows the llama.inf sound type
 * ('N' = stub driver, silent). --mute silences both.
 *
 * Input scripts (input_script.h, the DOSBox-X reference runs' PROBE.KEY format): --record
 * writes every key byte the game receives, keyed to the game tick it precedes; --replay starts
 * the game directly (no title, like the reference runs, which start game86 itself), ignores
 * the live keyboard, runs the fixed display clock and feeds each scripted byte to the keyboard
 * handler right before its tick. Ticks count from 1 after game_init.
 *
 * Test hooks: --frames N quits after N frames and prints the phase, tick count and a hash of
 * what is shown; --start-after K presses a key on frame K to leave the title. On the web the
 * page URL stands in for the command line:
 * ?ega&vga&joystick&keyboard&mute&frames=N&start=K&seed=T&flash=N|P|Y&keys=T:SC,...&hashes=T1,...
 * --keys is an input script given inline (tick:scan byte pairs, as in PROBE.KEY), so the web
 * build can replay too. --page-hashes prints, right before each listed tick, the FNV-1a hash of
 * the three pages and the shown page ("pages tick=..."), to compare native and web runs.
 * --dump-pages DIR with --dump-ticks writes, right before each listed game tick runs, the three
 * pages as the reference runs capture VRAM (inside the vsync wait that tick interrupts) to
 * DIR/pages-<tick>.bin: page 0, page 1, background (320x200 indexed pixels each), then one
 * byte, the shown page. For the visual check against those runs (tests/dosbox); the files hold
 * game graphics, so DIR must be a build directory.
 */
#include "assets.h"
#include "game.h"
#include "gfx.h"
#include "input.h"
#include "input_script.h"
#include "platform.h"
#include "settings.h"
#include "snd.h"
#include "timing.h"
#include "title.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

typedef enum AppPhase { PHASE_TITLE, PHASE_GAME, PHASE_DONE } AppPhase;

typedef struct App {
    AppPhase phase;
    Settings settings;
    GfxAdapter adapter;
    InputDevice device;
    TitleScreen title;
    GameState game;

    const DisplayTiming *display;   /* the emulated monitor: its retraces pace the frames */
    TickScheduler scheduler;        /* always in display mode: ticks per emulated retrace */
    RetraceClock retrace;           /* wall clock -> retraces (not in fixed-step runs) */
    uint64_t last_us;
    bool fixed_step;
    unsigned pending_requests;      /* host requests not yet handed to a game frame */
    SoundBank bank;
    bool sound;                     /* bank loaded and not muted */
    bool title_sample_requested;

    long frames_left;               /* --frames */
    long start_after;               /* --start-after */
    bool seed_pinned;               /* --seed */
    uint32_t seed;
    long frame_count;

    uint32_t game_ticks;            /* timer ticks since game_init (input script numbering) */
    const char *record_path;        /* --record */
    InputScript record;
    bool replaying;                 /* --replay */
    InputScript replay;
    const char *dump_dir;           /* --dump-pages */
    uint32_t dump_ticks[256];       /* --dump-ticks */
    size_t dump_tick_count;
    uint32_t hash_ticks[256];       /* --page-hashes */
    size_t hash_tick_count;
} App;

enum {
    AUDIO_RATE = 22050,
    AUDIO_MAX_QUEUED = AUDIO_RATE / 4,
};

static App g_app;

/* The game takes the host's requests as they are. */
_Static_assert((unsigned)GAME_HOST_QUIT == (unsigned)PLAT_REQUEST_QUIT &&
               (unsigned)GAME_HOST_PAUSE == (unsigned)PLAT_REQUEST_PAUSE,
               "GameHostRequest and PlatRequest bits must match");

/* BIOS tick count (0040:006C) for the local time of day: 1193182/65536 ticks per second. */
static uint32_t bios_ticks_now(void)
{
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    if (!local)
        return 0;
    uint64_t seconds = (uint64_t)local->tm_hour * 3600u + (uint64_t)local->tm_min * 60u + (uint64_t)local->tm_sec;
    return (uint32_t)(seconds * 1193182u / 65536u);
}

/* What llama.exe puts on game86's command tail (llama.exe 1000:0219, game86 1000:5f6b). */
static GameLaunch make_launch(const App *app)
{
    GameLaunch launch;
    memset(&launch, 0, sizeof launch);
    launch.vga = app->settings.vga;
    launch.joystick = app->settings.joystick;
    launch.sound = app->settings.sound != SETTINGS_SOUND_NONE;
    launch.flash_mode = (uint8_t)app->settings.flash;
    launch.bios_ticks = app->seed_pinned ? app->seed : bios_ticks_now();
    launch.shuffle_ticks = (RngTickSource){ NULL, NULL };
    return launch;
}

static uint32_t fnv1a(const uint8_t *data, size_t n, uint32_t h)
{
    for (size_t i = 0; i < n; i++)
        h = (h ^ data[i]) * 16777619u;
    return h;
}

static void on_key(uint8_t code, bool pressed, void *user)
{
    App *app = user;
    uint8_t scan_code = pressed ? code : (uint8_t)(code | PC_KEY_BREAK);
    if (app->phase == PHASE_TITLE) {
        input_keyboard_byte(scan_code);
        title_key(&app->title, code, pressed);
    } else if (!app->replaying) {
        /* Delivered before the frame's ticks run, i.e. before tick game_ticks + 1. */
        if (app->record_path && !input_script_add(&app->record, app->game_ticks + 1, scan_code))
            fprintf(stderr, "llamatron: recording failed\n");
        input_keyboard_byte(scan_code);
    }
}

/* What is on screen while game tick `tick` is about to run: the frames before it are done. */
static void dump_pages(const App *app, uint32_t tick)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/pages-%u.bin", app->dump_dir, (unsigned)tick);
    FILE *f = fopen(path, "wb");
    bool ok = f != NULL;
    for (int page = GFX_PAGE_0; ok && page <= GFX_PAGE_BACKGROUND; page++)
        ok = fwrite(gfx_page((GfxPageId)page)->pixels, 1, (size_t)GFX_WIDTH * GFX_HEIGHT, f) ==
             (size_t)GFX_WIDTH * GFX_HEIGHT;
    uint8_t shown = (uint8_t)gfx_display_page_id();
    ok = ok && fwrite(&shown, 1, 1, f) == 1;
    if (f && fclose(f) != 0)
        ok = false;
    if (!ok)
        fprintf(stderr, "llamatron: cannot write %s\n", path);
}

static void print_page_hashes(uint32_t tick)
{
    uint32_t h[3];
    for (int page = GFX_PAGE_0; page <= GFX_PAGE_BACKGROUND; page++)
        h[page] = fnv1a(&gfx_page((GfxPageId)page)->pixels[0][0], (size_t)GFX_WIDTH * GFX_HEIGHT, 2166136261u);
    printf("pages tick=%u fnv=%08x,%08x,%08x shown=%d\n", (unsigned)tick, h[0], h[1], h[2],
           (int)gfx_display_page_id());
}

static bool tick_listed(const uint32_t *ticks, size_t count, uint32_t tick)
{
    for (size_t i = 0; i < count; i++)
        if (ticks[i] == tick)
            return true;
    return false;
}

static void game_tick_cb(void *user)
{
    App *app = user;
    uint32_t next = app->game_ticks + 1;
    if (app->dump_dir && tick_listed(app->dump_ticks, app->dump_tick_count, next))
        dump_pages(app, next);
    if (tick_listed(app->hash_ticks, app->hash_tick_count, next))
        print_page_hashes(next);
    app->game_ticks++;
    /* The reference runs apply the bytes due at an IRQ before its handler; the driver tick
     * that already ran does not read the keyboard, so this is the same. */
    const InputScriptEvent *e;
    while (app->replaying && (e = input_script_take_due(&app->replay, app->game_ticks)) != NULL)
        input_keyboard_byte(e->scan_code);
    game_tick(&app->game);
}

/* Catch-up cap for wall-clock play: after a longer stall the game resumes, it does not race. */
enum { MAX_RETRACES_PER_HOST_FRAME = 4 };

/* How many emulated retraces this host frame holds: one in fixed-step runs (tests, replays),
 * otherwise as many as wall time allows (a 60 Hz host shows 70.086 Hz VGA as 1 or 2 per frame). */
static uint32_t retraces_due(App *app)
{
    if (app->fixed_step)
        return 1;
    uint64_t now = plat_time_us();
    uint32_t due = timing_retraces_due(&app->retrace, now - app->last_us);
    app->last_us = now;
    return due;
}

/* The timer ticks that fall before the next retrace, at 50.159 Hz: the sound driver tick, then
 * (in the game) the game's tick; fs.exe's handler (1000:018e) only ticks the driver. */
static void run_retrace_ticks(App *app)
{
    timing_advance_frame(&app->scheduler, app->display);
    if (app->phase == PHASE_GAME)
        timing_run_timer_ticks(&app->scheduler, game_tick_cb, app);
    else
        timing_run_timer_ticks(&app->scheduler, NULL, NULL);
}

static void feed_audio(App *app)
{
    uint8_t buffer[4096];
    size_t n;
    while (app->sound && (n = snd_take_output(buffer, sizeof buffer)) > 0) {
        if (plat_audio_queued() < AUDIO_MAX_QUEUED)
            plat_audio_queue(buffer, n);
    }
}

/* fs.exe 1000:008f; on the web it waits for the audio unlock (DEVIATIONS.md #6). */
static void request_title_sample(App *app)
{
    if (!app->sound || app->title_sample_requested)
        return;
#ifdef __EMSCRIPTEN__
    if (!app->fixed_step && !plat_audio_running())
        return;
#endif
    snd_play(TITLE_SAMPLE, TITLE_SAMPLE_RATE_INDEX, TITLE_SAMPLE_PRIORITY);
    app->title_sample_requested = true;
}

static void update_stick(void)
{
    PlatGamepad pad;
    plat_gamepad(0, &pad);
    input_set_stick(0, pad.x, pad.y, pad.buttons & PLAT_PAD_BUTTON_A, pad.buttons & PLAT_PAD_BUTTON_B);
}

/* fs.exe ends and llama.exe starts game86 with a freshly loaded sound driver and keyboard
 * handler; the game gets the llama.inf settings. */
static bool start_game(App *app)
{
    title_free(&app->title);
    if (app->sound) {
        snd_init(&app->bank, AUDIO_RATE);
        snd_set_enabled(app->settings.sound != SETTINGS_SOUND_NONE);
    }
    input_init(app->device, true);
    gfx_init();
    gfx_set_adapter(app->adapter, NULL);
    memset(&app->game, 0, sizeof app->game);
    GameLaunch launch = make_launch(app);
    if (!game_init(&app->game, &launch)) {
        fprintf(stderr, "llamatron: the game could not start\n");
        return false;
    }
    app->phase = PHASE_GAME;
    app->game_ticks = 0;
    return true;
}

static void present_title(const App *app)
{
    PlatColor pal[GFX_PALETTE_SIZE];
    for (int i = 0; i < GFX_PALETTE_SIZE; i++)
        pal[i] = (PlatColor){ app->title.palette[i].r, app->title.palette[i].g, app->title.palette[i].b };
    plat_present(app->title.pixels, app->title.width, app->title.height, app->title.width, pal);
}

static void present_game(void)
{
    const GfxColor *gp = gfx_palette();
    PlatColor pal[GFX_PALETTE_SIZE];
    for (int i = 0; i < GFX_PALETTE_SIZE; i++)
        pal[i] = (PlatColor){ gp[i].r, gp[i].g, gp[i].b };
    plat_present(gfx_display_pixels(), GFX_WIDTH, GFX_HEIGHT, GFX_WIDTH, pal);
}

/* --frames: what is shown at the end, for tests. */
static void print_test_report(const App *app)
{
    int cw = 0, ch = 0;
    uint32_t canvas = plat_captured_frame_hash(&cw, &ch);
    if (app->phase == PHASE_TITLE) {
        uint32_t h = fnv1a(app->title.pixels, (size_t)app->title.width * (size_t)app->title.height, 2166136261u);
        h = fnv1a((const uint8_t *)app->title.palette, sizeof app->title.palette, h);
        printf("title %dx%d hash=%08x canvas %dx%d hash=%08x\n", app->title.width, app->title.height,
               h, cw, ch, canvas);
    } else {
        uint32_t h = fnv1a(gfx_display_pixels(), (size_t)GFX_WIDTH * GFX_HEIGHT, 2166136261u);
        h = fnv1a((const uint8_t *)gfx_palette(), sizeof(GfxColor) * GFX_PALETTE_SIZE, h);
        printf("game display hash=%08x canvas %dx%d hash=%08x\n", h, cw, ch, canvas);
    }
    printf("llamatron phase=%s frames=%ld ticks=%llu\n", app->phase == PHASE_TITLE ? "title" : "game",
           app->frame_count, (unsigned long long)app->scheduler.ticks);
}

static void finish(App *app)
{
    if (app->phase == PHASE_TITLE)
        title_free(&app->title);
    if (app->record_path) {
        if (input_script_save(&app->record, app->record_path))
            printf("llamatron: recorded %zu key bytes to %s\n", app->record.count, app->record_path);
        input_script_free(&app->record);
    }
    if (app->replaying) {
        printf("llamatron: replayed %zu of %zu key bytes\n", app->replay.next, app->replay.count);
        input_script_free(&app->replay);
    }
    app->phase = PHASE_DONE;
    plat_shutdown();
}

/* One host frame: poll the host, run every emulated retrace that is due (each = its timer
 * ticks, then one title or game frame, the order of game86's frame loop 1e7b:013f with the
 * timer interrupt), then present the page on display once. */
static bool frame(void *user)
{
    App *app = user;
    bool window_open = plat_poll_events(on_key, app);
    app->pending_requests |= plat_take_requests();
    bool running = true;
    app->frame_count++;
    uint32_t due = retraces_due(app);

    if (app->phase == PHASE_TITLE) {
        if (!window_open)
            running = false;                    /* fs.exe has nothing to save */
        if (app->start_after > 0 && app->frame_count == app->start_after) {
            on_key(PC_KEY_SPACE, true, app);
            on_key(PC_KEY_SPACE, false, app);
        }
        request_title_sample(app);
        for (uint32_t i = 0; i < due && running && app->phase == PHASE_TITLE; i++) {
            run_retrace_ticks(app);
            update_stick();
            input_frame_read(false);
            title_frame(&app->title, input_state()->joystick_buttons);
            if (title_finished(&app->title) && !start_game(app))
                running = false;
        }
        app->pending_requests = 0;              /* nothing to pause or quit on the title */
    } else {
        /* The game decides what a quit or pause request means (window_open is ignored here:
         * a close arrives as PLAT_REQUEST_QUIT). */
        for (uint32_t i = 0; i < due && running; i++) {
            run_retrace_ticks(app);
            update_stick();
            GameHostInput host = { app->pending_requests, plat_has_focus() };
            app->pending_requests = 0;
            game_frame(&app->game, &host);
            if (game_wants_quit(&app->game))
                running = false;
        }
    }
    if (app->phase == PHASE_TITLE)
        present_title(app);
    else if (app->phase == PHASE_GAME)
        present_game();
    feed_audio(app);

    if (app->frames_left > 0 && --app->frames_left == 0) {
        print_test_report(app);
        running = false;
    }
    if (!running)
        finish(app);   /* under Emscripten nothing after plat_run_main_loop runs */
    return running;
}

enum { TICK_LIST_MAX = 256 };

/* "T1,T2,..." (stops at the first other character, e.g. '&' in a page URL). */
static size_t parse_ticks(const char *text, uint32_t *ticks, size_t max)
{
    size_t n = 0;
    char *end;
    while (n < max) {
        unsigned long t = strtoul(text, &end, 10);
        if (end == text)
            break;
        ticks[n++] = (uint32_t)t;
        if (*end != ',')
            break;
        text = end + 1;
    }
    return n;
}

/* "T:SC,T:SC,..." with decimal ticks and scan bytes, sorted by tick: an inline input script. */
static bool parse_keys(const char *text, InputScript *script)
{
    memset(script, 0, sizeof *script);
    char *end;
    for (;;) {
        unsigned long tick = strtoul(text, &end, 10);
        if (end == text || *end != ':')
            break;
        text = end + 1;
        unsigned long scan = strtoul(text, &end, 10);
        if (end == text || scan > 0xff || !input_script_add(script, (uint32_t)tick, (uint8_t)scan)) {
            fprintf(stderr, "llamatron: bad key list near \"%s\"\n", text);
            input_script_free(script);
            return false;
        }
        if (*end != ',')
            break;
        text = end + 1;
    }
    return true;
}

int main(int argc, char **argv)
{
    App *app = &g_app;
    app->settings = plat_settings_load();
    bool mute = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--vga") == 0)
            app->settings.vga = true;
        else if (strcmp(argv[i], "--ega") == 0)
            app->settings.vga = false;
        else if (strcmp(argv[i], "--joystick") == 0)
            app->settings.joystick = true;
        else if (strcmp(argv[i], "--keyboard") == 0)
            app->settings.joystick = false;
        else if (strcmp(argv[i], "--flash") == 0 && i + 1 < argc) {
            const char *f = argv[++i];
            app->settings.flash = f[0] == 'Y' ? SETTINGS_FLASH_ALL
                                : f[0] == 'P' ? SETTINGS_FLASH_PULSE_ONLY : SETTINGS_FLASH_NONE;
        } else if (strcmp(argv[i], "--mute") == 0)
            mute = true;
        else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc)
            assets_set_directory(argv[++i]);
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            app->frames_left = strtol(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--start-after") == 0 && i + 1 < argc)
            app->start_after = strtol(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            app->seed = (uint32_t)strtoul(argv[++i], NULL, 10);
            app->seed_pinned = true;
        } else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc)
            app->record_path = argv[++i];
        else if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            if (!input_script_load(&app->replay, argv[++i]))
                return 1;
            app->replaying = true;
        } else if (strcmp(argv[i], "--dump-pages") == 0 && i + 1 < argc)
            app->dump_dir = argv[++i];
        else if (strcmp(argv[i], "--dump-ticks") == 0 && i + 1 < argc)
            app->dump_tick_count = parse_ticks(argv[++i], app->dump_ticks, TICK_LIST_MAX);
        else if (strcmp(argv[i], "--page-hashes") == 0 && i + 1 < argc)
            app->hash_tick_count = parse_ticks(argv[++i], app->hash_ticks, TICK_LIST_MAX);
        else if (strcmp(argv[i], "--keys") == 0 && i + 1 < argc) {
            if (!parse_keys(argv[++i], &app->replay))
                return 1;
            app->replaying = true;
        }
    }
    if (app->replaying && app->record_path) {
        fprintf(stderr, "llamatron: --record and --replay exclude each other\n");
        return 1;
    }
#ifdef __EMSCRIPTEN__
    const char *query = emscripten_run_script_string("location.search");
    if (strstr(query, "ega"))
        app->settings.vga = false;
    if (strstr(query, "vga"))
        app->settings.vga = true;
    if (strstr(query, "joystick"))
        app->settings.joystick = true;
    if (strstr(query, "keyboard"))
        app->settings.joystick = false;
    if (strstr(query, "mute"))
        mute = true;
    const char *frames = strstr(query, "frames=");
    if (frames)
        app->frames_left = strtol(frames + 7, NULL, 10);
    const char *start = strstr(query, "start=");
    if (start)
        app->start_after = strtol(start + 6, NULL, 10);
    const char *seed = strstr(query, "seed=");
    if (seed) {
        app->seed = (uint32_t)strtoul(seed + 5, NULL, 10);
        app->seed_pinned = true;
    }
    const char *flash = strstr(query, "flash=");
    if (flash)
        app->settings.flash = flash[6] == 'Y' ? SETTINGS_FLASH_ALL
                            : flash[6] == 'P' ? SETTINGS_FLASH_PULSE_ONLY : SETTINGS_FLASH_NONE;
    const char *hashes = strstr(query, "hashes=");
    if (hashes)
        app->hash_tick_count = parse_ticks(hashes + 7, app->hash_ticks, TICK_LIST_MAX);
    const char *keys = strstr(query, "keys=");
    if (keys) {
        if (!parse_keys(keys + 5, &app->replay))
            return 1;
        app->replaying = true;
    }
#endif
    app->adapter = app->settings.vga ? GFX_ADAPTER_VGA : GFX_ADAPTER_EGA;
    app->device = app->settings.joystick ? INPUT_DEVICE_JOYSTICK : INPUT_DEVICE_KEYBOARD;
    uint8_t inf[SETTINGS_INF_SIZE];
    settings_format_inf(&app->settings, inf);
    printf("llamatron: settings %s\n", (const char *)inf);

    if (!app->replaying && !title_load(&app->title, app->adapter, app->device))
        return 1;
    PlatConfig config = { "Llamatron 2112", 3, false, true, 0 };
    if (app->frames_left > 0)
        config.web_timer_fps = 70;   /* deterministic frame pacing for test runs */
    if (!plat_init(&config))
        return 1;
    input_init(app->device, true);
    plat_set_frame_capture(app->frames_left > 0);

    /* Ticks are counted per emulated retrace of the adapter's monitor (VGA 70.086 Hz, EGA
     * 59.92 Hz). Test runs and replays take one retrace per host frame so they are exact;
     * normal play takes as many as wall time allows. */
    app->display = app->adapter == GFX_ADAPTER_VGA ? &DISPLAY_VGA_MODE_0D : &DISPLAY_EGA_MODE_0D;
    app->fixed_step = app->frames_left > 0 || app->replaying;
    timing_init_display(&app->scheduler, app->display);
    timing_retrace_init(&app->retrace, app->display, MAX_RETRACES_PER_HOST_FRAME);
    app->last_us = plat_time_us();

    if (!mute && snd_load_bank(&app->bank)) {
        snd_init(&app->bank, AUDIO_RATE);
        app->sound = true;
        if (!plat_audio_open(AUDIO_RATE))
            fprintf(stderr, "llamatron: no audio device, running silently\n");
    }
    app->phase = PHASE_TITLE;
    if (app->replaying) {
        if (!start_game(app))
            return 1;
    } else {
        request_title_sample(app);
    }
    plat_run_main_loop(frame, app);
    return 0;
}
