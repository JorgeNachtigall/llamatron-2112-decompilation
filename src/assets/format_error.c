/* format_error.c - error message and header check shared by the parsers. */
#include "formats.h"
#include "reader.h"

#include <stdarg.h>
#include <stdio.h>

static char error_text[256];

const char *format_error(void)
{
    return error_text;
}

bool format_fail(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(error_text, sizeof error_text, fmt, args);
    va_end(args);
    return false;
}

bool reader_header(Reader *r, const char magic[4], uint16_t version)
{
    const uint8_t *found = reader_bytes(r, 4);
    if (!found || memcmp(found, magic, 4) != 0)
        return format_fail("not a %.4s file", magic);
    uint16_t found_version = reader_u16(r);
    if (!r->ok || found_version != version)
        return format_fail("%.4s version %u, expected %u (rebuild the generated data)", magic,
                           (unsigned)found_version, (unsigned)version);
    return true;
}
