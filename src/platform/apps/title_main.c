/*
 * title_main.c - M1 front screen app: shows the title picture like fs.exe and exits on a key
 * (or stick A button with --joystick).
 *
 *   llamatron_title [--ega] [--joystick] [--data DIR] [--frames N] [--dump FILE.ppm]
 *
 * The pictures are compiled into the binary (native default) or read from the data directory
 * (--data, web /data, LLAMATRON_EMBED_ASSETS=OFF builds); see assets.h.
 *
 * --frames N quits after N frames and prints a hash of the picture and of the rendered
 * window contents (read back before present); on the web use ?frames=N&ega. --dump writes the shown
 * picture (palette applied) as a binary PPM for local inspection. Never commit such dumps:
 * they contain the original artwork (docs/PLAN.md §9).
 */
#include "assets.h"
#include "input.h"
#include "platform.h"
#include "title.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

typedef struct App {
    TitleScreen title;
    long frames_left;
    const char *dump_path;
} App;

static App g_app;

static void on_key(uint8_t code, bool pressed, void *user)
{
    App *app = user;
    input_key_event(code, pressed);
    title_key(&app->title, code, pressed);
}

static uint32_t fnv1a(const uint8_t *data, size_t n, uint32_t h)
{
    for (size_t i = 0; i < n; i++)
        h = (h ^ data[i]) * 16777619u;
    return h;
}

static void dump_ppm(const TitleScreen *t, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", t->width, t->height);
    for (int i = 0; i < t->width * t->height; i++) {
        GfxColor c = t->palette[t->pixels[i]];
        fputc(c.r, f);
        fputc(c.g, f);
        fputc(c.b, f);
    }
    fclose(f);
}

static void finish(App *app)
{
    if (app->dump_path)
        dump_ppm(&app->title, app->dump_path);
    title_free(&app->title);
    plat_shutdown();
}

static bool frame(void *user)
{
    App *app = user;
    bool running = plat_poll_events(on_key, app);

    PlatGamepad pad;
    plat_gamepad(0, &pad);
    input_set_stick(0, pad.x, pad.y, pad.buttons & PLAT_PAD_BUTTON_A, pad.buttons & PLAT_PAD_BUTTON_B);
    input_frame_read(false);
    title_frame(&app->title, input_state()->joystick_buttons);

    PlatColor pal[GFX_PALETTE_SIZE];
    for (int i = 0; i < GFX_PALETTE_SIZE; i++)
        pal[i] = (PlatColor){ app->title.palette[i].r, app->title.palette[i].g, app->title.palette[i].b };
    plat_present(app->title.pixels, app->title.width, app->title.height, app->title.width, pal);

    if (title_finished(&app->title))
        running = false;
    if (app->frames_left > 0 && --app->frames_left == 0) {
        uint32_t h = fnv1a(app->title.pixels, (size_t)app->title.width * (size_t)app->title.height, 2166136261u);
        h = fnv1a((const uint8_t *)app->title.palette, sizeof app->title.palette, h);
        int cw = 0, ch = 0;
        uint32_t canvas = plat_captured_frame_hash(&cw, &ch);
        printf("title %dx%d hash=%08x canvas %dx%d hash=%08x\n", app->title.width, app->title.height, h,
               cw, ch, canvas);
        running = false;
    }
    if (!running)
        finish(app);   /* under Emscripten nothing after plat_run_main_loop runs */
    return running;
}

int main(int argc, char **argv)
{
    App *app = &g_app;
    GfxAdapter adapter = GFX_ADAPTER_VGA;
    InputDevice device = INPUT_DEVICE_KEYBOARD;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ega") == 0)
            adapter = GFX_ADAPTER_EGA;
        else if (strcmp(argv[i], "--joystick") == 0)
            device = INPUT_DEVICE_JOYSTICK;
        else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc)
            assets_set_directory(argv[++i]);
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            app->frames_left = strtol(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc)
            app->dump_path = argv[++i];
    }

#ifdef __EMSCRIPTEN__
    /* The page URL stands in for the command line: ?ega&joystick&frames=N */
    const char *query = emscripten_run_script_string("location.search");
    if (strstr(query, "ega"))
        adapter = GFX_ADAPTER_EGA;
    if (strstr(query, "joystick"))
        device = INPUT_DEVICE_JOYSTICK;
    const char *frames = strstr(query, "frames=");
    if (frames)
        app->frames_left = strtol(frames + 7, NULL, 10);
#endif

    if (!title_load(&app->title, adapter, device))
        return 1;
    PlatConfig config = { "Llamatron 2112", 3, false, true, 0 };
    if (app->frames_left > 0)
        config.web_timer_fps = 70;   /* deterministic frame pacing for test runs */
    if (!plat_init(&config))
        return 1;
    input_init(device, true);
    plat_set_frame_capture(app->frames_left > 0);
    /* TODO(W7): snd_request(TITLE_SAMPLE, TITLE_SAMPLE_RATE_INDEX, TITLE_SAMPLE_PRIORITY) */
    plat_run_main_loop(frame, app);
    return 0;
}
