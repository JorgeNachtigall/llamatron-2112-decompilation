/* reader.h - bounds-checked little-endian reader for the data-file parsers (internal). */
#ifndef LLAMATRON_ASSETS_READER_H
#define LLAMATRON_ASSETS_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    const uint8_t *data;
    size_t size, pos;
    bool ok;            /* false after any read past the end; later reads return 0 */
} Reader;

static inline Reader reader_open(const uint8_t *data, size_t size)
{
    return (Reader){data, size, 0, data != NULL};
}

/* Pointer to the next `count` bytes (advancing past them), or NULL past the end. */
static inline const uint8_t *reader_bytes(Reader *r, size_t count)
{
    if (!r->ok || count > r->size - r->pos) {
        r->ok = false;
        return NULL;
    }
    const uint8_t *p = r->data + r->pos;
    r->pos += count;
    return p;
}

static inline uint8_t reader_u8(Reader *r)
{
    const uint8_t *p = reader_bytes(r, 1);
    return p ? p[0] : 0;
}

static inline uint16_t reader_u16(Reader *r)
{
    const uint8_t *p = reader_bytes(r, 2);
    return p ? (uint16_t)(p[0] | p[1] << 8) : 0;
}

static inline uint32_t reader_u32(Reader *r)
{
    const uint8_t *p = reader_bytes(r, 4);
    return p ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 : 0;
}

/* Checks the 4-byte magic and the u16 version; sets the format error otherwise. */
bool reader_header(Reader *r, const char magic[4], uint16_t version);

/* Sets the message returned by format_error(); always returns false. */
bool format_fail(const char *fmt, ...);

#endif /* LLAMATRON_ASSETS_READER_H */
