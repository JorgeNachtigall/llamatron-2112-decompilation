/* Shared helpers for the build-time extractor: byte buffers, little-endian access,
 * error reporting and file I/O. Pure C11, no third-party dependencies. */
#ifndef EXTRACT_COMMON_H
#define EXTRACT_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A growable byte buffer that owns its storage. */
typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} ByteBuf;

void bytebuf_free(ByteBuf *buf);
bool bytebuf_reserve(ByteBuf *buf, size_t capacity);
bool bytebuf_push(ByteBuf *buf, uint8_t byte);
bool bytebuf_append(ByteBuf *buf, const uint8_t *bytes, size_t count);
bool bytebuf_put_u16(ByteBuf *buf, uint16_t value); /* little-endian */
bool bytebuf_put_u32(ByteBuf *buf, uint32_t value); /* little-endian */

static inline uint16_t read_u16le(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static inline void write_u16le(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)(value >> 8);
}

/* Records a printf-style error message and returns false, so failures read as
 * `return fail("...")`. The last message is available through last_error(). */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
bool fail(const char *fmt, ...);
#ifdef __clang_analyzer__
/* The static analyzer does not look into variadic functions, so it would follow paths on
 * which fail() returned true. */
#define fail(...) ((void)fail(__VA_ARGS__), false)
#endif
const char *last_error(void);

bool read_file(const char *path, ByteBuf *out);
bool write_file(const char *path, const uint8_t *data, size_t size);
/* Creates every missing directory of `path` (like `mkdir -p`). */
bool make_dirs(const char *path);
bool file_exists(const char *path);
/* Joins `dir` and `name` with a single '/'. Returns false if it does not fit. */
bool path_join(char *out, size_t out_size, const char *dir, const char *name);

#endif
