/*
 * platform_sdl.c - SDL2 implementation of platform.h (macOS/Linux native and Emscripten).
 */
#include "platform.h"
#include "keymap.h"
#include "scale.h"
#include "settings.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

enum {
    BASE_WINDOW_W = 320,
    BASE_WINDOW_H = 240,
    DEFAULT_WINDOW_SCALE = 3,
    MAX_IMAGE_W = 1024,
    MAX_IMAGE_H = 1024,
    AUDIO_BUFFER_SAMPLES = 2048,
};

static struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    int texture_w, texture_h;
    PlatScaleMode scale_mode;
    SDL_Texture *sharp;             /* PLAT_SCALE_SHARP: the image at whole multiples */
    int sharp_w, sharp_h;
    bool vsync;
    bool unpaced;
    uint64_t start_counter;
    uint64_t counter_hz;
    SDL_GameController *pads[PLAT_MAX_GAMEPADS];
    SDL_AudioDeviceID audio;
    uint32_t rgb_buffer[MAX_IMAGE_W * MAX_IMAGE_H];
    int web_timer_fps;
    unsigned requests;
    bool focused;
    bool capture;
    uint32_t capture_hash;
    int capture_w, capture_h;
} g_plat;

bool plat_init(const PlatConfig *config)
{
    PlatConfig defaults = { "Llamatron 2112", DEFAULT_WINDOW_SCALE, false, true, 0, false };
    if (!config)
        config = &defaults;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
#ifdef __EMSCRIPTEN__
    /* Let the browser keep keyboard focus on the canvas only, not the whole page. */
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "platform: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    int scale = config->window_scale > 0 ? config->window_scale : DEFAULT_WINDOW_SCALE;
    Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (config->fullscreen)
        window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    g_plat.window = SDL_CreateWindow(config->window_title ? config->window_title : defaults.window_title,
                                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     BASE_WINDOW_W * scale, BASE_WINDOW_H * scale, window_flags);
    if (!g_plat.window) {
        fprintf(stderr, "platform: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    g_plat.unpaced = config->unpaced;
    g_plat.vsync = config->vsync && !config->unpaced;
    g_plat.web_timer_fps = config->web_timer_fps;
    Uint32 renderer_flags = SDL_RENDERER_ACCELERATED | (g_plat.vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
    g_plat.renderer = SDL_CreateRenderer(g_plat.window, -1, renderer_flags);
    if (!g_plat.renderer)
        g_plat.renderer = SDL_CreateRenderer(g_plat.window, -1, SDL_RENDERER_SOFTWARE);
    if (!g_plat.renderer) {
        fprintf(stderr, "platform: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        plat_shutdown();
        return false;
    }
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(g_plat.renderer, &info) == 0)
        g_plat.vsync = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;

    g_plat.focused = true;
    g_plat.counter_hz = SDL_GetPerformanceFrequency();
    g_plat.start_counter = SDL_GetPerformanceCounter();
    return true;
}

void plat_shutdown(void)
{
    plat_audio_close();
    for (int i = 0; i < PLAT_MAX_GAMEPADS; i++) {
        if (g_plat.pads[i])
            SDL_GameControllerClose(g_plat.pads[i]);
        g_plat.pads[i] = NULL;
    }
    if (g_plat.texture)
        SDL_DestroyTexture(g_plat.texture);
    if (g_plat.sharp)
        SDL_DestroyTexture(g_plat.sharp);
    if (g_plat.renderer)
        SDL_DestroyRenderer(g_plat.renderer);
    if (g_plat.window)
        SDL_DestroyWindow(g_plat.window);
    g_plat.texture = NULL;
    g_plat.sharp = NULL;
    g_plat.renderer = NULL;
    g_plat.window = NULL;
    SDL_Quit();
}

/* ---- Display ------------------------------------------------------------------------- */

static bool ensure_texture(int width, int height)
{
    if (g_plat.texture && g_plat.texture_w == width && g_plat.texture_h == height)
        return true;
    if (g_plat.texture)
        SDL_DestroyTexture(g_plat.texture);
    g_plat.texture = SDL_CreateTexture(g_plat.renderer, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!g_plat.texture) {
        fprintf(stderr, "platform: SDL_CreateTexture(%dx%d) failed: %s\n", width, height, SDL_GetError());
        return false;
    }
    g_plat.texture_w = width;
    g_plat.texture_h = height;
    return true;
}

/* PLAT_SCALE_SHARP's render target: the image enlarged kx x ky with nearest filtering, then
 * drawn to the window smoothed. NULL where the renderer has no render targets. */
static SDL_Texture *sharp_target(int w, int h)
{
    if (g_plat.sharp && g_plat.sharp_w == w && g_plat.sharp_h == h)
        return g_plat.sharp;
    if (g_plat.sharp)
        SDL_DestroyTexture(g_plat.sharp);
    g_plat.sharp = NULL;
    if (!SDL_RenderTargetSupported(g_plat.renderer))
        return NULL;
    g_plat.sharp = SDL_CreateTexture(g_plat.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!g_plat.sharp)
        return NULL;
    SDL_SetTextureScaleMode(g_plat.sharp, SDL_ScaleModeLinear);
    g_plat.sharp_w = w;
    g_plat.sharp_h = h;
    return g_plat.sharp;
}

/* The image (in g_plat.texture) to the window, as the scale mode says. */
static void draw_image(int width, int height)
{
    int out_w = 0, out_h = 0;
    SDL_GetRendererOutputSize(g_plat.renderer, &out_w, &out_h);    /* pixels, also on Retina */
    ScaleRect r = scale_fit(g_plat.scale_mode, width, height, out_w, out_h);
    SDL_Rect dst = { r.x, r.y, r.w, r.h };
    SDL_SetRenderDrawColor(g_plat.renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_plat.renderer);
    if (g_plat.scale_mode == PLAT_SCALE_SHARP) {
        int kx, ky;
        scale_sharp_factors(width, height, r, &kx, &ky);
        SDL_Texture *big = sharp_target(width * kx, height * ky);
        if (big && SDL_SetRenderTarget(g_plat.renderer, big) == 0) {
            SDL_RenderCopy(g_plat.renderer, g_plat.texture, NULL, NULL);
            SDL_SetRenderTarget(g_plat.renderer, NULL);
            SDL_RenderCopy(g_plat.renderer, big, NULL, &dst);
            return;
        }
    }
    SDL_RenderCopy(g_plat.renderer, g_plat.texture, NULL, &dst);
}

/* Read back the frame being rendered (before present, so WebGL still has it) and hash the
 * RGB bytes of every pixel. */
static void capture_frame(void)
{
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(g_plat.renderer, &w, &h);
    uint32_t *frame = w > 0 && h > 0 ? SDL_malloc((size_t)w * (size_t)h * sizeof *frame) : NULL;
    if (!frame)
        return;
    if (SDL_RenderReadPixels(g_plat.renderer, NULL, SDL_PIXELFORMAT_ARGB8888, frame, w * (int)sizeof *frame) == 0) {
        uint32_t hash = 2166136261u;
        for (size_t i = 0; i < (size_t)w * (size_t)h; i++) {
            uint8_t rgb[3] = { (uint8_t)(frame[i] >> 16), (uint8_t)(frame[i] >> 8), (uint8_t)frame[i] };
            for (int c = 0; c < 3; c++)
                hash = (hash ^ rgb[c]) * 16777619u;
        }
        g_plat.capture_hash = hash;
        g_plat.capture_w = w;
        g_plat.capture_h = h;
    }
    SDL_free(frame);
}

void plat_present(const uint8_t *pixels, int width, int height, int pitch,
                  const PlatColor palette[256])
{
    if (width <= 0 || height <= 0 || width > MAX_IMAGE_W || height > MAX_IMAGE_H)
        return;
    /* Unpaced test runs draw only the frames they read back: a present waits on the window
     * server (a covered or background window is throttled), which made runs slow and their
     * timing depend on what else is on screen. */
    if (g_plat.unpaced && !g_plat.capture)
        return;
    if (!ensure_texture(width, height))
        return;

    uint32_t argb[256];
    for (int i = 0; i < 256; i++)
        argb[i] = 0xff000000u | ((uint32_t)palette[i].r << 16) | ((uint32_t)palette[i].g << 8) | palette[i].b;

    uint32_t *out = g_plat.rgb_buffer;
    for (int y = 0; y < height; y++) {
        const uint8_t *row = pixels + (size_t)y * (size_t)pitch;
        for (int x = 0; x < width; x++)
            *out++ = argb[row[x]];
    }
    SDL_UpdateTexture(g_plat.texture, NULL, g_plat.rgb_buffer, width * (int)sizeof(uint32_t));
    draw_image(width, height);
    if (g_plat.capture)
        capture_frame();
    SDL_RenderPresent(g_plat.renderer);
}

void plat_set_frame_capture(bool enabled)
{
    g_plat.capture = enabled;
}

uint32_t plat_captured_frame_hash(int *width, int *height)
{
    if (width)
        *width = g_plat.capture_w;
    if (height)
        *height = g_plat.capture_h;
    return g_plat.capture_hash;
}

void plat_set_scale_mode(PlatScaleMode mode)
{
    if (mode >= 0 && mode < PLAT_SCALE_MODE_COUNT)
        g_plat.scale_mode = mode;
}

PlatScaleMode plat_scale_mode(void)
{
    return g_plat.scale_mode;
}

void plat_toggle_fullscreen(void)
{
#ifdef __EMSCRIPTEN__
    /* The page goes full screen from inside the key or click handler (web/shell.html): browsers
     * allow it only there, and the canvas then fills the screen at 4:3. */
    emscripten_run_script("if (typeof llamatronToggleFullscreen === 'function') llamatronToggleFullscreen();");
#else
    Uint32 flags = SDL_GetWindowFlags(g_plat.window);
    SDL_SetWindowFullscreen(g_plat.window, (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
}

/* ---- Input --------------------------------------------------------------------------- */

static void open_gamepad(int device_index)
{
    if (!SDL_IsGameController(device_index))
        return;
    for (int i = 0; i < PLAT_MAX_GAMEPADS; i++) {
        if (!g_plat.pads[i]) {
            g_plat.pads[i] = SDL_GameControllerOpen(device_index);
            return;
        }
    }
}

static void close_gamepad(SDL_JoystickID instance)
{
    for (int i = 0; i < PLAT_MAX_GAMEPADS; i++) {
        SDL_GameController *pad = g_plat.pads[i];
        if (pad && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) == instance) {
            SDL_GameControllerClose(pad);
            g_plat.pads[i] = NULL;
        }
    }
}

unsigned plat_take_requests(void)
{
    unsigned r = g_plat.requests;
    g_plat.requests = 0;
    return r;
}

bool plat_has_focus(void)
{
    return g_plat.focused;
}

bool plat_poll_events(PlatKeyFn on_key, void *user)
{
    bool keep_running = true;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
        case SDL_APP_TERMINATING:
            g_plat.requests |= PLAT_REQUEST_QUIT;
            keep_running = false;
            break;
        case SDL_APP_WILLENTERBACKGROUND:
            g_plat.requests |= PLAT_REQUEST_PAUSE;
            break;
        case SDL_WINDOWEVENT:
            switch (ev.window.event) {
            case SDL_WINDOWEVENT_FOCUS_LOST:
            case SDL_WINDOWEVENT_MINIMIZED:
            case SDL_WINDOWEVENT_HIDDEN:
                g_plat.focused = false;
                g_plat.requests |= PLAT_REQUEST_PAUSE;
                break;
            case SDL_WINDOWEVENT_FOCUS_GAINED:
                g_plat.focused = true;
                break;
            default:
                break;
            }
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            HostKey host = keymap_host_key(ev.key.keysym.scancode, (SDL_Keymod)ev.key.keysym.mod);
            if (host != HOST_KEY_NONE) {
                bool act = ev.type == SDL_KEYDOWN && !ev.key.repeat;
#ifndef __EMSCRIPTEN__
                if (act && host == HOST_KEY_FULLSCREEN)
                    plat_toggle_fullscreen();     /* web: the page's own key handler does it */
#endif
                if (act && host == HOST_KEY_SCALE_MODE)
                    plat_set_scale_mode((PlatScaleMode)((g_plat.scale_mode + 1) % PLAT_SCALE_MODE_COUNT));
                break;
            }
            /* Auto-repeat is forwarded as further make codes, like the PC keyboard's typematic
             * repeat that the original INT 09 handler received. */
            uint8_t code = keymap_sdl_to_pc(ev.key.keysym.scancode);
            if (code && on_key)
                on_key(code, ev.type == SDL_KEYDOWN, user);
            break;
        }
        case SDL_CONTROLLERDEVICEADDED:
            open_gamepad(ev.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            close_gamepad(ev.cdevice.which);
            break;
        default:
            break;
        }
    }
    return keep_running;
}

static float axis_value(SDL_GameController *pad, SDL_GameControllerAxis axis)
{
    int v = SDL_GameControllerGetAxis(pad, axis);
    return v < 0 ? (float)v / 32768.0f : (float)v / 32767.0f;
}

void plat_gamepad(int index, PlatGamepad *out)
{
    memset(out, 0, sizeof *out);
    if (index < 0 || index >= PLAT_MAX_GAMEPADS || !g_plat.pads[index])
        return;
    SDL_GameController *pad = g_plat.pads[index];
    out->connected = true;
    out->x = axis_value(pad, SDL_CONTROLLER_AXIS_LEFTX);
    out->y = axis_value(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
        out->x = -1.0f;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
        out->x = 1.0f;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP))
        out->y = -1.0f;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
        out->y = 1.0f;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A))
        out->buttons |= PLAT_PAD_BUTTON_A;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B))
        out->buttons |= PLAT_PAD_BUTTON_B;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START))
        out->buttons |= PLAT_PAD_BUTTON_START;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X))
        out->buttons |= PLAT_PAD_BUTTON_X;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y))
        out->buttons |= PLAT_PAD_BUTTON_Y;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK))
        out->buttons |= PLAT_PAD_BUTTON_BACK;
    out->rx = axis_value(pad, SDL_CONTROLLER_AXIS_RIGHTX);
    out->ry = axis_value(pad, SDL_CONTROLLER_AXIS_RIGHTY);
}

/* ---- Time and main loop -------------------------------------------------------------- */

uint64_t plat_time_us(void)
{
    uint64_t elapsed = SDL_GetPerformanceCounter() - g_plat.start_counter;
    return elapsed / g_plat.counter_hz * 1000000u + elapsed % g_plat.counter_hz * 1000000u / g_plat.counter_hz;
}

#ifdef __EMSCRIPTEN__
static struct {
    PlatFrameFn frame;
    void *user;
} g_loop;

static void emscripten_frame(void *arg)
{
    (void)arg;
    if (!g_loop.frame(g_loop.user))
        emscripten_cancel_main_loop();
}
#endif

void plat_run_main_loop(PlatFrameFn frame, void *user)
{
#ifdef __EMSCRIPTEN__
    g_loop.frame = frame;
    g_loop.user = user;
    /* fps 0 = requestAnimationFrame; simulate_infinite_loop 1 = this call does not return. */
    emscripten_set_main_loop_arg(emscripten_frame, NULL, g_plat.web_timer_fps, 1);
#else
    /* Without vsync, pace at ~60 Hz so we do not spin a core. */
    const uint64_t fallback_period_us = 16667;
    uint64_t next_us = plat_time_us();
    while (frame(user)) {
        if (g_plat.vsync || g_plat.unpaced)
            continue;
        next_us += fallback_period_us;
        uint64_t now = plat_time_us();
        if (next_us > now)
            SDL_Delay((Uint32)((next_us - now) / 1000u));
        else
            next_us = now;
    }
#endif
}

/* ---- Settings storage ------------------------------------------------------------------ */

#ifdef __EMSCRIPTEN__
/* web/persist.js mounts IDBFS here and loads it before main() runs. */
#define SETTINGS_DIR "/persist/"
#endif
#define SETTINGS_FILE "llama.inf"
#define SCORES_FILE "scores.yak"

/* Where the saved file `name` lives: the user's preference directory. */
static bool data_path(const char *name, char *out, size_t size)
{
#ifdef __EMSCRIPTEN__
    snprintf(out, size, "%s%s", SETTINGS_DIR, name);
    return true;
#else
    /* Tests (and portable installs) redirect storage with LLAMATRON_PREF_DIR. */
    const char *override = SDL_getenv("LLAMATRON_PREF_DIR");
    if (override && *override) {
        snprintf(out, size, "%s/%s", override, name);
        return true;
    }
    char *dir = SDL_GetPrefPath("llamatron-port", "llamatron");
    if (!dir)
        return false;
    snprintf(out, size, "%s%s", dir, name);
    SDL_free(dir);
    return true;
#endif
}

/* Reads at most `size` bytes of the saved file `name`; -1 if there is none. */
static long data_read(const char *name, uint8_t *out, size_t size)
{
    char path[1024];
    if (!data_path(name, path, sizeof path))
        return -1;
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    size_t n = fread(out, 1, size, f);
    fclose(f);
    return (long)n;
}

/* Replaces the saved file `name` with `size` bytes. */
static bool data_write(const char *name, const uint8_t *data, size_t size)
{
    char path[1024];
    if (!data_path(name, path, sizeof path))
        return false;
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(data, 1, size, f) == size;
    ok = fclose(f) == 0 && ok;
#ifdef __EMSCRIPTEN__
    /* Push the in-memory IDBFS state to the browser's IndexedDB. */
    emscripten_run_script("if (typeof llamatronPersistSync === 'function') llamatronPersistSync();");
#endif
    return ok;
}

Settings plat_settings_load(void)
{
    Settings s = settings_defaults();
    uint8_t inf[SETTINGS_INF_SIZE];
    long n = data_read(SETTINGS_FILE, inf, sizeof inf);
    if (n >= 0)
        settings_parse_inf(inf, (size_t)n, &s);
    return s;
}

bool plat_settings_save(const Settings *settings)
{
    uint8_t inf[SETTINGS_INF_SIZE];
    settings_format_inf(settings, inf);
    return data_write(SETTINGS_FILE, inf, sizeof inf);
}

/* game86 1000:60aa: read up to 0xA1 bytes, keep them only if exactly 0xA0 came. */
bool plat_scores_load(uint8_t out[PLAT_SCORES_SIZE])
{
    uint8_t yak[PLAT_SCORES_SIZE + 1];
    if (data_read(SCORES_FILE, yak, sizeof yak) != PLAT_SCORES_SIZE)
        return false;
    memcpy(out, yak, PLAT_SCORES_SIZE);
    return true;
}

/* game86 1000:5e89 */
bool plat_scores_save(const uint8_t yak[PLAT_SCORES_SIZE])
{
    return data_write(SCORES_FILE, yak, PLAT_SCORES_SIZE);
}

/* ---- Audio --------------------------------------------------------------------------- */

bool plat_audio_open(int rate)
{
    if (g_plat.audio)
        return true;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = rate;
    want.format = AUDIO_U8;
    want.channels = 1;
    want.samples = AUDIO_BUFFER_SAMPLES;
    g_plat.audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_plat.audio) {
        fprintf(stderr, "platform: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(g_plat.audio, 0);
    return true;
}

bool plat_audio_running(void)
{
    if (!g_plat.audio)
        return false;
#ifdef __EMSCRIPTEN__
    /* SDL2's Emscripten audio keeps its state in Module.SDL2. */
    return emscripten_run_script_int(
               "(Module.SDL2 && Module.SDL2.audioContext && "
               "Module.SDL2.audioContext.state === 'running') ? 1 : 0") != 0;
#else
    return SDL_GetAudioDeviceStatus(g_plat.audio) == SDL_AUDIO_PLAYING;
#endif
}

void plat_audio_close(void)
{
    if (g_plat.audio)
        SDL_CloseAudioDevice(g_plat.audio);
    g_plat.audio = 0;
}

size_t plat_audio_queue(const uint8_t *samples, size_t count)
{
    if (!g_plat.audio)
        return 0;
    SDL_QueueAudio(g_plat.audio, samples, (Uint32)count);
    return SDL_GetQueuedAudioSize(g_plat.audio);
}

size_t plat_audio_queued(void)
{
    return g_plat.audio ? SDL_GetQueuedAudioSize(g_plat.audio) : 0;
}
