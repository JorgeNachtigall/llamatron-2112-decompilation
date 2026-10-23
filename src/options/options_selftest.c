/*
 * options_selftest.c - the options screen's key handling (every value of every llama.inf field
 * is reachable and survives settings_format_inf / settings_parse_inf) and its picture (drawn
 * with the extracted font, inside the screen, every line visible).
 *
 *   options_selftest [--dump FILE]     FILE: the 320x200 picture as a binary PPM (for a look;
 *                                      it shows game graphics, so keep it in a build directory)
 */
#include "options_screen.h"

#include "game_data.h"
#include "input.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                                                \
        }                                                                              \
    } while (0)

enum { KEY_ENTER = 0x1c };

static void press(OptionsScreen *screen, uint8_t code)
{
    options_key(screen, code, true);
    options_key(screen, code, false);
}

static bool same(const Settings *a, const Settings *b)
{
    return a->vga == b->vga && a->joystick == b->joystick && a->sound == b->sound && a->cpu == b->cpu &&
           a->flash == b->flash;
}

static void check_keys(void)
{
    Settings start = settings_shipped_defaults();
    OptionsScreen screen;

    /* Esc and O leave without changes; Enter saves. */
    options_open(&screen, &start);
    press(&screen, PC_KEY_RIGHT);
    press(&screen, PC_KEY_ESCAPE);
    CHECK(screen.state == OPTIONS_CANCELLED);
    options_open(&screen, &start);
    press(&screen, OPTIONS_KEY);
    CHECK(screen.state == OPTIONS_CANCELLED);
    options_open(&screen, &start);
    press(&screen, KEY_ENTER);
    CHECK(screen.state == OPTIONS_SAVED && same(&screen.settings, &start));
    options_key(&screen, PC_KEY_RIGHT, true);   /* finished: keys are ignored */
    CHECK(same(&screen.settings, &start));

    /* Up wraps to the last line; releases do nothing. */
    options_open(&screen, &start);
    press(&screen, PC_KEY_UP);
    CHECK(screen.line == OPTIONS_LINE_FLASHES);
    options_key(&screen, PC_KEY_DOWN, false);
    CHECK(screen.line == OPTIONS_LINE_FLASHES);

    /* Every value of every line, round trip through llama.inf text. */
    static const int counts[OPTIONS_LINE_COUNT] = { 2, 2, 4, 10, 3 };
    for (int line = 0; line < OPTIONS_LINE_COUNT; line++) {
        options_open(&screen, &start);
        for (int i = 0; i < line; i++)
            press(&screen, PC_KEY_DOWN);
        CHECK((int)screen.line == line);
        char seen[16][32];
        for (int step = 0; step < counts[line]; step++) {
            snprintf(seen[step], sizeof seen[step], "%s", options_value_text(&screen, (OptionsLine)line));
            for (int j = 0; j < step; j++)
                CHECK(strcmp(seen[j], seen[step]) != 0);
            uint8_t inf[SETTINGS_INF_SIZE];
            settings_format_inf(&screen.settings, inf);
            Settings back = settings_shipped_defaults();
            CHECK(settings_parse_inf(inf, sizeof inf, &back));
            CHECK(same(&back, &screen.settings));
            press(&screen, PC_KEY_RIGHT);
        }
        CHECK(strcmp(seen[0], options_value_text(&screen, (OptionsLine)line)) == 0);   /* wrapped */
        press(&screen, PC_KEY_LEFT);
        CHECK(strcmp(seen[counts[line] - 1], options_value_text(&screen, (OptionsLine)line)) == 0);
    }
}

static void check_render(const char *dump)
{
    if (!game_data_load()) {
        fprintf(stderr, "options_selftest: no game data, picture not checked\n");
        failures++;
        return;
    }
    static uint8_t pixels[GFX_HEIGHT][GFX_WIDTH];
    GfxColor palette[GFX_PALETTE_SIZE];
    Settings start = settings_shipped_defaults();
    for (int vga = 0; vga < 2; vga++) {
        OptionsScreen screen;
        start.vga = vga != 0;
        options_open(&screen, &start);
        press(&screen, PC_KEY_DOWN);
        CHECK(options_render(&screen, pixels, palette));
        /* Text on every line band, nothing in the outer columns, and more than one colour. */
        int lit_rows = 0, colors = 0;
        bool used[16] = { false };
        for (int y = 0; y < GFX_HEIGHT; y++) {
            bool lit = false;
            for (int x = 0; x < GFX_WIDTH; x++) {
                if (pixels[y][x] == 0)
                    continue;
                lit = true;
                CHECK(x >= 4 && x < GFX_WIDTH - 4);
                CHECK(pixels[y][x] < 16);
                if (pixels[y][x] < 16 && !used[pixels[y][x]]) {
                    used[pixels[y][x]] = true;
                    colors++;
                }
            }
            lit_rows += lit;
        }
        CHECK(lit_rows >= 8 * 8);         /* title, five lines, two help lines */
        CHECK(colors >= 4);
        for (int c = 1; c < 16; c++)
            if (used[c])
                CHECK(palette[c].r || palette[c].g || palette[c].b);   /* visible on black */
    }
    if (dump) {
        FILE *f = fopen(dump, "wb");
        if (!f) {
            fprintf(stderr, "options_selftest: cannot write %s\n", dump);
            failures++;
            return;
        }
        fprintf(f, "P6\n%d %d\n255\n", GFX_WIDTH, GFX_HEIGHT);
        for (int y = 0; y < GFX_HEIGHT; y++)
            for (int x = 0; x < GFX_WIDTH; x++)
                fwrite(&palette[pixels[y][x]], 3, 1, f);
        fclose(f);
    }
}

int main(int argc, char **argv)
{
    const char *dump = argc == 3 && strcmp(argv[1], "--dump") == 0 ? argv[2] : NULL;
    check_keys();
    check_render(dump);
    if (failures) {
        fprintf(stderr, "options_selftest: %d failures\n", failures);
        return 1;
    }
    printf("options_selftest: all checks passed\n");
    return 0;
}
