/*
 * assets.c - asset lookup (see assets.h). Built twice: with LLAMATRON_EMBED_ASSETS (lookup in
 * the compiled-in blob) and without (files read from a directory and cached).
 */
#include "assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef LLAMATRON_EMBED_ASSETS
#include "asset_blob.h"
#endif

#ifndef LLAMATRON_DATA_DIR
#define LLAMATRON_DATA_DIR "data"
#endif

enum { MAX_CACHED_ASSETS = 64, MAX_PATH_LEN = 1024 };

static const char *g_directory = LLAMATRON_DATA_DIR;

void assets_set_directory(const char *dir)
{
    g_directory = dir;
}

#ifdef LLAMATRON_EMBED_ASSETS

bool assets_embedded(void)
{
    return true;
}

const uint8_t *asset_get(const char *name, size_t *size)
{
    for (unsigned i = 0; i < llama_blob_count; i++) {
        if (strcmp(llama_blob_entries[i].name, name) == 0) {
            if (size)
                *size = llama_blob_entries[i].size;
            return llama_blob_entries[i].data;
        }
    }
    fprintf(stderr, "assets: %s is not in the embedded data (rebuild with LLAMATRON_ORIGINAL_DIR set)\n", name);
    return NULL;
}

#else

static struct {
    char name[64];
    uint8_t *data;
    size_t size;
} g_cache[MAX_CACHED_ASSETS];
static int g_cached;

bool assets_embedded(void)
{
    return false;
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    uint8_t *data = NULL;
    long length = -1;
    if (fseek(f, 0, SEEK_END) == 0)
        length = ftell(f);
    if (length >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        data = malloc(length > 0 ? (size_t)length : 1);
        if (data && fread(data, 1, (size_t)length, f) != (size_t)length) {
            free(data);
            data = NULL;
        }
    }
    fclose(f);
    if (data)
        *size = (size_t)length;
    return data;
}

const uint8_t *asset_get(const char *name, size_t *size)
{
    for (int i = 0; i < g_cached; i++) {
        if (strcmp(g_cache[i].name, name) == 0) {
            if (size)
                *size = g_cache[i].size;
            return g_cache[i].data;
        }
    }
    if (g_cached == MAX_CACHED_ASSETS || strlen(name) >= sizeof g_cache[0].name) {
        fprintf(stderr, "assets: cannot cache %s\n", name);
        return NULL;
    }

    char path[MAX_PATH_LEN];
    snprintf(path, sizeof path, "%s/%s", g_directory, name);
    size_t length = 0;
    uint8_t *data = read_file(path, &length);
    if (!data) {
        fprintf(stderr, "assets: cannot read %s (run the build with LLAMATRON_ORIGINAL_DIR set)\n", path);
        return NULL;
    }
    snprintf(g_cache[g_cached].name, sizeof g_cache[g_cached].name, "%s", name);
    g_cache[g_cached].data = data;
    g_cache[g_cached].size = length;
    g_cached++;
    if (size)
        *size = length;
    return data;
}

#endif

const uint8_t *asset_get_exact(const char *name, size_t size)
{
    size_t actual = 0;
    const uint8_t *data = asset_get(name, &actual);
    if (data && actual != size) {
        fprintf(stderr, "assets: %s has %zu bytes, expected %zu\n", name, actual, size);
        return NULL;
    }
    return data;
}
