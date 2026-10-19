/*
 * input_script.c - PROBE.KEY-format input scripts (see input_script.h).
 */
#include "input_script.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool input_script_add(InputScript *script, uint32_t tick, uint8_t scan_code)
{
    if (script->count > 0 && tick < script->events[script->count - 1].tick)
        return false;
    if (script->count == script->capacity) {
        size_t capacity = script->capacity ? script->capacity * 2 : 64;
        InputScriptEvent *events = realloc(script->events, capacity * sizeof *events);
        if (!events)
            return false;
        script->events = events;
        script->capacity = capacity;
    }
    script->events[script->count++] = (InputScriptEvent){ tick, scan_code };
    return true;
}

bool input_script_load(InputScript *script, const char *path)
{
    memset(script, 0, sizeof *script);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "input script: cannot read %s\n", path);
        return false;
    }
    uint8_t rec[INPUT_SCRIPT_RECORD_SIZE];
    size_t n;
    bool ok = true;
    while ((n = fread(rec, 1, sizeof rec, f)) == sizeof rec) {
        uint32_t tick = (uint32_t)rec[0] | (uint32_t)rec[1] << 8 | (uint32_t)rec[2] << 16 | (uint32_t)rec[3] << 24;
        if (!input_script_add(script, tick, rec[4])) {
            fprintf(stderr, "input script: %s is not sorted by tick (record %zu)\n", path, script->count);
            ok = false;
            break;
        }
    }
    if (ok && n != 0) {
        fprintf(stderr, "input script: %s has a partial record at the end\n", path);
        ok = false;
    }
    fclose(f);
    if (!ok)
        input_script_free(script);
    return ok;
}

bool input_script_save(const InputScript *script, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "input script: cannot write %s\n", path);
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < script->count && ok; i++) {
        uint32_t t = script->events[i].tick;
        uint8_t rec[INPUT_SCRIPT_RECORD_SIZE] = {
            (uint8_t)t, (uint8_t)(t >> 8), (uint8_t)(t >> 16), (uint8_t)(t >> 24),
            script->events[i].scan_code, 0,
        };
        ok = fwrite(rec, 1, sizeof rec, f) == sizeof rec;
    }
    if (fclose(f) != 0)
        ok = false;
    if (!ok)
        fprintf(stderr, "input script: writing %s failed\n", path);
    return ok;
}

const InputScriptEvent *input_script_take_due(InputScript *script, uint32_t tick)
{
    if (script->next < script->count && script->events[script->next].tick <= tick)
        return &script->events[script->next++];
    return NULL;
}

void input_script_free(InputScript *script)
{
    free(script->events);
    memset(script, 0, sizeof *script);
}
