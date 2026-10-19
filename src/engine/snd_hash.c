/*
 * snd_hash.c - regression check for the sound path on the real sample bank: plays the title
 * request (sample 1Bh, rate 1, priority 6) for a number of ticks at 22050 Hz output and prints
 * an FNV-1a hash of the rendered audio (same numbers llamatron_title --frames 140 prints).
 *
 *   snd_hash [--data DIR] [--ticks N] [--expect HEX]
 */
#include "assets.h"
#include "snd.h"
#include "title.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    long ticks = 100;
    const char *expect = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc)
            assets_set_directory(argv[++i]);
        else if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc)
            ticks = strtol(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--expect") == 0 && i + 1 < argc)
            expect = argv[++i];
    }

    static SoundBank bank;
    if (!snd_load_bank(&bank)) {
        fprintf(stderr, "snd_hash: cannot load the sample bank\n");
        return 1;
    }
    snd_init(&bank, 22050);
    snd_play(TITLE_SAMPLE, TITLE_SAMPLE_RATE_INDEX, TITLE_SAMPLE_PRIORITY);

    uint32_t hash = 2166136261u;
    unsigned long long samples = 0;
    uint8_t buffer[4096];
    for (long t = 0; t < ticks; t++) {
        snd_tick();
        size_t n;
        while ((n = snd_take_output(buffer, sizeof buffer)) > 0) {
            for (size_t i = 0; i < n; i++)
                hash = (hash ^ buffer[i]) * 16777619u;
            samples += n;
        }
    }
    printf("sound ticks=%ld samples=%llu hash=%08x\n", ticks, samples, hash);
    if (expect && strtoul(expect, NULL, 16) != hash) {
        fprintf(stderr, "snd_hash: expected %s\n", expect);
        return 1;
    }
    return 0;
}
