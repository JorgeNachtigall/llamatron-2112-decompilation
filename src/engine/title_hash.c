/*
 * title_hash.c - regression check for the front screen: loads the extracted title picture
 * through title_load() (no window) and compares the FNV-1a hash of pixels + palette with the
 * expected value; with --data it checks the extractor's files, otherwise whatever the linked
 * asset store provides. Hashes of derived data only; the pictures themselves are never committed.
 *
 *   title_hash [--data DIR] [--ega] [--expect HEX]
 */
#include "assets.h"
#include "title.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t fnv1a(const uint8_t *data, size_t n, uint32_t h)
{
    for (size_t i = 0; i < n; i++)
        h = (h ^ data[i]) * 16777619u;
    return h;
}

int main(int argc, char **argv)
{
    const char *expect = NULL;
    GfxAdapter adapter = GFX_ADAPTER_VGA;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc)
            assets_set_directory(argv[++i]);
        else if (strcmp(argv[i], "--ega") == 0)
            adapter = GFX_ADAPTER_EGA;
        else if (strcmp(argv[i], "--expect") == 0 && i + 1 < argc)
            expect = argv[++i];
    }

    TitleScreen title;
    if (!title_load(&title, adapter, INPUT_DEVICE_KEYBOARD))
        return 1;
    uint32_t h = fnv1a(title.pixels, (size_t)title.width * (size_t)title.height, 2166136261u);
    h = fnv1a((const uint8_t *)title.palette, sizeof title.palette, h);
    printf("title %dx%d hash=%08x\n", title.width, title.height, h);
    title_free(&title);

    if (expect && strtoul(expect, NULL, 16) != h) {
        fprintf(stderr, "title_hash: expected %s\n", expect);
        return 1;
    }
    return 0;
}
