#include "common.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define make_one_dir(path) _mkdir(path)
#else
#define make_one_dir(path) mkdir(path, 0777)
#endif

static char error_message[1024];

bool(fail)(const char *fmt, ...)
{
    char text[sizeof error_message]; /* the arguments may include last_error() */
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    memcpy(error_message, text, sizeof error_message);
    return false;
}

const char *last_error(void) { return error_message; }

void bytebuf_free(ByteBuf *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->size = buf->capacity = 0;
}

bool bytebuf_reserve(ByteBuf *buf, size_t capacity)
{
    if (capacity <= buf->capacity)
        return true;
    size_t grown = buf->capacity ? buf->capacity : 256;
    while (grown < capacity)
        grown *= 2;
    uint8_t *data = realloc(buf->data, grown);
    if (!data)
        return fail("out of memory (%zu bytes)", grown);
    buf->data = data;
    buf->capacity = grown;
    return true;
}

bool bytebuf_push(ByteBuf *buf, uint8_t byte)
{
    if (!bytebuf_reserve(buf, buf->size + 1))
        return false;
    buf->data[buf->size++] = byte;
    return true;
}

bool bytebuf_append(ByteBuf *buf, const uint8_t *bytes, size_t count)
{
    if (!bytebuf_reserve(buf, buf->size + count))
        return false;
    if (count)
        memcpy(buf->data + buf->size, bytes, count);
    buf->size += count;
    return true;
}

bool bytebuf_put_u16(ByteBuf *buf, uint16_t value)
{
    uint8_t bytes[2];
    write_u16le(bytes, value);
    return bytebuf_append(buf, bytes, 2);
}

bool bytebuf_put_u32(ByteBuf *buf, uint32_t value)
{
    return bytebuf_put_u16(buf, (uint16_t)(value & 0xFFFF)) && bytebuf_put_u16(buf, (uint16_t)(value >> 16));
}

bool read_file(const char *path, ByteBuf *out)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return fail("cannot open %s: %s", path, strerror(errno));
    out->size = 0;
    uint8_t chunk[65536];
    for (;;) {
        size_t got = fread(chunk, 1, sizeof chunk, f);
        if (got > 0 && !bytebuf_append(out, chunk, got)) {
            fclose(f);
            return false;
        }
        if (got < sizeof chunk) /* end of file or an error, told apart by ferror below */
            break;
    }
    bool ok = !ferror(f);
    fclose(f);
    return ok ? true : fail("error reading %s", path);
}

bool write_file(const char *path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return fail("cannot create %s: %s", path, strerror(errno));
    bool ok = fwrite(data, 1, size, f) == size;
    ok = (fclose(f) == 0) && ok;
    return ok ? true : fail("error writing %s", path);
}

bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG;
}

bool make_dirs(const char *path)
{
    char partial[4096];
    size_t len = strlen(path);
    if (len >= sizeof partial)
        return fail("path too long: %s", path);
    memcpy(partial, path, len + 1);
    for (size_t i = 1; i <= len; i++) {
        if (partial[i] != '/' && partial[i] != '\0')
            continue;
        char saved = partial[i];
        partial[i] = '\0';
        if (make_one_dir(partial) != 0 && errno != EEXIST)
            return fail("cannot create directory %s: %s", partial, strerror(errno));
        partial[i] = saved;
    }
    return true;
}

bool path_join(char *out, size_t out_size, const char *dir, const char *name)
{
    size_t dir_len = strlen(dir);
    while (dir_len > 1 && dir[dir_len - 1] == '/')
        dir_len--;
    int written = snprintf(out, out_size, "%.*s/%s", (int)dir_len, dir, name);
    if (written < 0 || (size_t)written >= out_size)
        return fail("path too long: %s/%s", dir, name);
    return true;
}
