#include "manifest.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A small JSON reader: just enough for the manifest (objects, arrays, strings without
 * \u escapes, unsigned integers, booleans, null). */
typedef struct {
    const char *text;
    size_t pos;
    size_t size;
    const char *path;
} JsonReader;

static bool json_error(const JsonReader *r, const char *what)
{
    size_t line = 1;
    for (size_t i = 0; i < r->pos && i < r->size; i++)
        line += r->text[i] == '\n';
    return fail("%s:%zu: %s", r->path, line, what);
}

static void json_skip_space(JsonReader *r)
{
    while (r->pos < r->size && isspace((unsigned char)r->text[r->pos]))
        r->pos++;
}

static bool json_peek(JsonReader *r, char c)
{
    json_skip_space(r);
    return r->pos < r->size && r->text[r->pos] == c;
}

/* Consumes `c` when it comes next (after white space). */
static bool json_take(JsonReader *r, char c)
{
    if (!json_peek(r, c))
        return false;
    r->pos++;
    return true;
}

static bool json_expect(JsonReader *r, char c)
{
    if (!json_peek(r, c)) {
        char what[32];
        snprintf(what, sizeof what, "expected '%c'", c);
        return json_error(r, what);
    }
    r->pos++;
    return true;
}

static bool json_string(JsonReader *r, char *out, size_t out_size)
{
    if (!json_expect(r, '"'))
        return false;
    size_t len = 0;
    while (r->pos < r->size && r->text[r->pos] != '"') {
        char c = r->text[r->pos++];
        if (c == '\\') {
            if (r->pos >= r->size)
                break;
            c = r->text[r->pos++];
            c = c == 'n' ? '\n' : c == 't' ? '\t' : c;
        }
        if (len + 1 >= out_size)
            return json_error(r, "string too long");
        out[len++] = c;
    }
    if (r->pos >= r->size)
        return json_error(r, "unterminated string");
    r->pos++;
    out[len] = '\0';
    return true;
}

static bool json_number(JsonReader *r, uint64_t *out)
{
    json_skip_space(r);
    if (r->pos >= r->size || !isdigit((unsigned char)r->text[r->pos]))
        return json_error(r, "expected an unsigned integer");
    uint64_t value = 0;
    while (r->pos < r->size && isdigit((unsigned char)r->text[r->pos]))
        value = value * 10 + (uint64_t)(r->text[r->pos++] - '0');
    *out = value;
    return true;
}

static bool json_literal(JsonReader *r, const char *word)
{
    json_skip_space(r);
    size_t len = strlen(word);
    if (r->pos + len > r->size || memcmp(r->text + r->pos, word, len) != 0)
        return false;
    r->pos += len;
    return true;
}

static bool json_bool(JsonReader *r, bool *out)
{
    if (json_literal(r, "true"))
        *out = true;
    else if (json_literal(r, "false"))
        *out = false;
    else
        return json_error(r, "expected true or false");
    return true;
}

static bool json_skip_value(JsonReader *r, int depth);

/* Calls `on_member(r, key, ctx)` for every member of an object; the callback must consume
 * the value. */
typedef bool (*JsonMemberFn)(JsonReader *r, const char *key, void *ctx);

static bool json_object(JsonReader *r, JsonMemberFn on_member, void *ctx)
{
    if (!json_expect(r, '{'))
        return false;
    if (json_take(r, '}'))
        return true;
    for (;;) {
        char key[64];
        if (!json_string(r, key, sizeof key) || !json_expect(r, ':') || !on_member(r, key, ctx))
            return false;
        if (!json_take(r, ','))
            return json_expect(r, '}');
    }
}

static bool skip_member(JsonReader *r, const char *key, void *ctx)
{
    (void)key;
    return json_skip_value(r, *(int *)ctx);
}

static bool json_skip_value(JsonReader *r, int depth)
{
    if (depth > 32)
        return json_error(r, "nesting too deep");
    int inner = depth + 1;
    json_skip_space(r);
    if (json_peek(r, '{'))
        return json_object(r, skip_member, &inner);
    if (json_peek(r, '[')) {
        r->pos++;
        if (json_take(r, ']'))
            return true;
        do {
            if (!json_skip_value(r, inner))
                return false;
        } while (json_take(r, ','));
        return json_expect(r, ']');
    }
    if (json_peek(r, '"')) {
        char scratch[1024];
        return json_string(r, scratch, sizeof scratch);
    }
    if (json_literal(r, "true") || json_literal(r, "false") || json_literal(r, "null"))
        return true;
    uint64_t number;
    return json_number(r, &number);
}

static bool file_member(JsonReader *r, const char *key, void *ctx)
{
    ManifestFile *file = ctx;
    if (strcmp(key, "name") == 0)
        return json_string(r, file->name, sizeof file->name);
    if (strcmp(key, "size") == 0)
        return json_number(r, &file->size);
    if (strcmp(key, "sha256") == 0)
        return json_string(r, file->sha256, sizeof file->sha256);
    if (strcmp(key, "verify") == 0)
        return json_bool(r, &file->verify);
    return json_skip_value(r, 1);
}

static bool manifest_member(JsonReader *r, const char *key, void *ctx)
{
    Manifest *manifest = ctx;
    if (strcmp(key, "release") == 0)
        return json_string(r, manifest->release, sizeof manifest->release);
    if (strcmp(key, "files") != 0)
        return json_skip_value(r, 1);

    if (!json_expect(r, '['))
        return false;
    if (json_take(r, ']'))
        return true;
    do {
        if (manifest->file_count == MANIFEST_MAX_FILES)
            return json_error(r, "too many files");
        ManifestFile *file = &manifest->files[manifest->file_count];
        *file = (ManifestFile){.verify = true};
        if (!json_object(r, file_member, file))
            return false;
        if (!file->name[0] || strlen(file->sha256) != SHA256_HEX_SIZE - 1)
            return json_error(r, "file entry needs a name and a 64-digit sha256");
        manifest->file_count++;
    } while (json_take(r, ','));
    return json_expect(r, ']');
}

bool manifest_load(const char *path, Manifest *out)
{
    ByteBuf text = {0};
    if (!read_file(path, &text))
        return false;
    *out = (Manifest){0};
    JsonReader reader = {.text = (const char *)text.data, .size = text.size, .path = path};
    bool ok = json_object(&reader, manifest_member, out);
    bytebuf_free(&text);
    if (ok && out->file_count == 0)
        ok = fail("%s: no files listed", path);
    return ok;
}

const ManifestFile *manifest_find(const Manifest *manifest, const char *name)
{
    for (size_t i = 0; i < manifest->file_count; i++)
        if (strcmp(manifest->files[i].name, name) == 0)
            return &manifest->files[i];
    return NULL;
}

bool find_original(const char *dir, const char *name, char *path, size_t path_size)
{
    if (path_join(path, path_size, dir, name) && file_exists(path))
        return true;
    char upper[MANIFEST_NAME_SIZE];
    size_t len = strlen(name);
    if (len >= sizeof upper)
        return false;
    for (size_t i = 0; i <= len; i++)
        upper[i] = (char)toupper((unsigned char)name[i]);
    return path_join(path, path_size, dir, upper) && file_exists(path);
}

bool manifest_verify(const Manifest *manifest, const char *dir)
{
    bool all_ok = true;
    for (size_t i = 0; i < manifest->file_count; i++) {
        const ManifestFile *file = &manifest->files[i];
        const char *kind = file->verify ? "required" : "optional";
        char path[4096];
        if (!find_original(dir, file->name, path, sizeof path)) {
            printf("  %-12s MISSING (%s)\n", file->name, kind);
            all_ok &= !file->verify;
            continue;
        }
        ByteBuf data = {0};
        if (!read_file(path, &data)) {
            printf("  %-12s UNREADABLE: %s\n", file->name, last_error());
            all_ok &= !file->verify;
            continue;
        }
        char hash[SHA256_HEX_SIZE];
        sha256_hex(data.data, data.size, hash);
        bool match = data.size == file->size && strcmp(hash, file->sha256) == 0;
        if (match) {
            printf("  %-12s ok\n", file->name);
        } else {
            printf("  %-12s %s (%s)\n"
                   "      expected size %llu sha256 %s\n"
                   "      actual   size %zu sha256 %s\n",
                   file->name, file->verify ? "MISMATCH" : "differs", kind,
                   (unsigned long long)file->size, file->sha256, data.size, hash);
            all_ok &= !file->verify;
        }
        bytebuf_free(&data);
    }
    return all_ok;
}
