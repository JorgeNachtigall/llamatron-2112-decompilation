/*
 * input_script_selftest.c - PROBE.KEY round trip and replay order (ctest input_script_selftest).
 *
 *   input_script_selftest OUT.key    writes the test script there for the llamatron replay test
 */
#include "input_script.h"

#include <stdio.h>

static int g_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "input_script_selftest.key";
    InputScript s = { 0 };
    /* Same shape as the reference run's script: fire on the title, then walk right. */
    CHECK(input_script_add(&s, 150, 0x39));
    CHECK(input_script_add(&s, 160, 0xb9));
    CHECK(input_script_add(&s, 160, 0x4d));              /* same tick: kept in order */
    CHECK(input_script_add(&s, 240, 0xcd));
    CHECK(!input_script_add(&s, 200, 0x48));             /* ticks must not go back */
    CHECK(input_script_save(&s, path));
    input_script_free(&s);

    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) {
        unsigned char rec[INPUT_SCRIPT_RECORD_SIZE];
        CHECK(fread(rec, 1, sizeof rec, f) == sizeof rec);
        /* {u32 LE irq, u8 scancode, u8 0}, as tools/harness/dosbox/refrun.py packs "<IBB". */
        CHECK(rec[0] == 150 && rec[1] == 0 && rec[2] == 0 && rec[3] == 0 && rec[4] == 0x39 && rec[5] == 0);
        fclose(f);
    }

    InputScript r;
    CHECK(input_script_load(&r, path));
    CHECK(r.count == 4);
    CHECK(input_script_take_due(&r, 149) == NULL);
    const InputScriptEvent *e = input_script_take_due(&r, 150);
    CHECK(e && e->scan_code == 0x39);
    CHECK(input_script_take_due(&r, 150) == NULL);
    e = input_script_take_due(&r, 170);                  /* both 160 bytes, late is fine */
    CHECK(e && e->scan_code == 0xb9);
    e = input_script_take_due(&r, 170);
    CHECK(e && e->scan_code == 0x4d);
    CHECK(input_script_take_due(&r, 239) == NULL);
    input_script_free(&r);

    if (g_failures) {
        fprintf(stderr, "input_script_selftest: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("input_script_selftest: ok (%s)\n", path);
    return 0;
}
