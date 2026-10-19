/*
 * llamatron_main.c - the game executable: the title screen (fs.exe), then the game (game86)
 * through its entry API (src/game/game.h). No game logic here: the shell only sequences the two
 * and connects the platform services (window, audio, input, settings, host requests).
 *
 *   llamatron [--vga|--ega] [--keyboard|--joystick] [--mute] [--data DIR]
 *             [--frames N] [--start-after K] [--seed BIOS_TICKS]
 *
 * Settings come from the saved llama.inf (settings.h); the options here override them for this
 * run only. The game gets them as game86 did, on its command tail (GameLaunch); the RNG seed is
 * the BIOS tick count of the local time of day, like the original (--seed pins it). As in the original, the title always plays its sample and the game follows the
 * llama.inf sound type ('N' = stub driver, silent). --mute silences both.
 *
 * Test hooks: --frames N quits after N frames and prints the phase, tick count and a hash of
 * what is shown; --start-after K presses a key on frame K to leave the title. On the web the
 * page URL stands in for the command line: ?ega&vga&joystick&keyboard&mute&frames=N&start=K&seed=T.
 */
#include "assets.h"
#include "game.h"
#include "gfx.h"
#include "input.h"
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

    TickScheduler scheduler;
    uint64_t last_us;
    bool fixed_step;
    SoundBank bank;
    bool sound;                     /* bank loaded and not muted */
    bool title_sample_requested;

    long frames_left;               /* --frames */
    long start_after;               /* --start-after */
    bool seed_pinned;               /* --seed */
    uint32_t seed;
    long frame_count;
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
    input_key_event(code, pressed);
    if (app->phase == PHASE_TITLE)
        title_key(&app->title, code, pressed);
}

static void game_tick_cb(void *user)
{
    App *app = user;
    game_tick(&app->game);
}

/* Advance the 50.159 Hz scheduler: the sound driver tick, then (in the game) the game's tick;
 * fs.exe's handler (1000:018e) only ticks the driver. Then feed the audio device. */
static void run_ticks(App *app)
{
    if (app->fixed_step) {
        timing_advance_frame(&app->scheduler, &DISPLAY_VGA_MODE_0D);
    } else {
        uint64_t now = plat_time_us();
        timing_advance(&app->scheduler, now - app->last_us);
        app->last_us = now;
    }
    if (app->phase == PHASE_GAME)
        timing_run_timer_ticks(&app->scheduler, game_tick_cb, app);
    else
        timing_run_timer_ticks(&app->scheduler, NULL, NULL);

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
    app->phase = PHASE_DONE;
    plat_shutdown();
}

static bool frame(void *user)
{
    App *app = user;
    bool window_open = plat_poll_events(on_key, app);
    unsigned requests = plat_take_requests();
    bool running = true;
    app->frame_count++;

    if (app->phase == PHASE_TITLE) {
        if (!window_open)
            running = false;                    /* fs.exe has nothing to save */
        if (app->start_after > 0 && app->frame_count == app->start_after) {
            on_key(PC_KEY_SPACE, true, app);
            on_key(PC_KEY_SPACE, false, app);
        }
        request_title_sample(app);
        run_ticks(app);
        update_stick();
        input_frame_read(false);
        title_frame(&app->title, input_state()->joystick_buttons);
        present_title(app);
        if (running && title_finished(&app->title) && !start_game(app))
            running = false;
    } else {
        /* The game decides what a quit or pause request means (window_open is ignored here:
         * a close arrives as PLAT_REQUEST_QUIT). */
        run_ticks(app);
        update_stick();
        GameHostInput host = { requests, plat_has_focus() };
        game_frame(&app->game, &host);
        present_game();
        if (game_wants_quit(&app->game))
            running = false;
    }

    if (app->frames_left > 0 && --app->frames_left == 0) {
        print_test_report(app);
        running = false;
    }
    if (!running)
        finish(app);   /* under Emscripten nothing after plat_run_main_loop runs */
    return running;
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
        else if (strcmp(argv[i], "--mute") == 0)
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
        }
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
#endif
    app->adapter = app->settings.vga ? GFX_ADAPTER_VGA : GFX_ADAPTER_EGA;
    app->device = app->settings.joystick ? INPUT_DEVICE_JOYSTICK : INPUT_DEVICE_KEYBOARD;
    uint8_t inf[SETTINGS_INF_SIZE];
    settings_format_inf(&app->settings, inf);
    printf("llamatron: settings %s\n", (const char *)inf);

    if (!title_load(&app->title, app->adapter, app->device))
        return 1;
    PlatConfig config = { "Llamatron 2112", 3, false, true, 0 };
    if (app->frames_left > 0)
        config.web_timer_fps = 70;   /* deterministic frame pacing for test runs */
    if (!plat_init(&config))
        return 1;
    input_init(app->device, true);
    plat_set_frame_capture(app->frames_left > 0);

    /* Test runs use the fixed 70.086 Hz display clock so ticks are exact. */
    app->fixed_step = app->frames_left > 0;
    if (app->fixed_step)
        timing_init_display(&app->scheduler, &DISPLAY_VGA_MODE_0D);
    else
        timing_init_realtime(&app->scheduler);
    app->last_us = plat_time_us();

    if (!mute && snd_load_bank(&app->bank)) {
        snd_init(&app->bank, AUDIO_RATE);
        app->sound = true;
        if (!plat_audio_open(AUDIO_RATE))
            fprintf(stderr, "llamatron: no audio device, running silently\n");
        request_title_sample(app);
    }
    app->phase = PHASE_TITLE;
    plat_run_main_loop(frame, app);
    return 0;
}
