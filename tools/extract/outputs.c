#include "outputs.h"

#include <stdio.h>
#include <string.h>

#define STAMP_FORMAT "llamatron-extract stamp v1"

bool output_write(OutputSet *set, const char *relative, const uint8_t *data, size_t size)
{
    if (set->count == MAX_OUTPUTS)
        return fail("too many outputs");
    char path[4096];
    if (!path_join(path, sizeof path, set->root, relative))
        return false;
    char *slash = strrchr(path, '/');
    if (slash) {
        *slash = '\0';
        bool made = make_dirs(path);
        *slash = '/';
        if (!made)
            return false;
    }
    if (!write_file(path, data, size))
        return false;
    OutputRecord *record = &set->records[set->count++];
    snprintf(record->path, sizeof record->path, "%s", relative);
    sha256_hex(data, size, record->sha256);
    return true;
}

bool output_write_text(OutputSet *set, const char *relative, const char *text)
{
    return output_write(set, relative, (const uint8_t *)text, strlen(text));
}

bool output_write_stamp(const OutputSet *set)
{
    ByteBuf text = {0};
    char line[512];
    snprintf(line, sizeof line, "%s\n", STAMP_FORMAT);
    bool ok = bytebuf_append(&text, (const uint8_t *)line, strlen(line));
    for (size_t i = 0; ok && i < set->count; i++) {
        snprintf(line, sizeof line, "%s  %s\n", set->records[i].sha256, set->records[i].path);
        ok = bytebuf_append(&text, (const uint8_t *)line, strlen(line));
    }
    char path[4096];
    ok = ok && path_join(path, sizeof path, set->root, "extract.stamp") &&
         write_file(path, text.data, text.size);
    bytebuf_free(&text);
    return ok;
}
