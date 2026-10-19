/* Stream layout: a flag byte announces the next 8 items, bit 0 first. Flag 1 = literal byte.
 * Flag 0 = two bytes b0 b1: ring position (b1 >> 4) << 8 | b0, length (b1 & 0x0F) + 3.
 * The ring starts zero-filled with the write position at 0xFEE. A pair 00 00 followed by a
 * zero byte ends the stream; followed by anything else it is an ordinary 3-byte copy from
 * ring position 0 and that byte stays in the input. game86 1000:e6bb */
#include "lzss.h"

#include <string.h>

enum {
    RING_SIZE = 4096,
    RING_START = 0xFEE,
    MIN_MATCH = 3,
};

bool lzss_decode(const uint8_t *data, size_t size, ByteBuf *out)
{
    uint8_t ring[RING_SIZE];
    memset(ring, 0, sizeof ring);
    size_t write_pos = RING_START;
    size_t pos = 0;
    unsigned flags = 0;
    int flags_left = 0;
    out->size = 0;

    for (;;) {
        if (flags_left == 0) {
            if (pos >= size)
                return fail("LZSS stream ends without a terminator");
            flags = data[pos++];
            flags_left = 8;
        }
        bool literal = flags & 1;
        flags >>= 1;
        flags_left--;

        if (literal) {
            if (pos >= size)
                return fail("LZSS stream ends inside a literal");
            uint8_t byte = data[pos++];
            ring[write_pos++ % RING_SIZE] = byte;
            if (!bytebuf_push(out, byte))
                return false;
            continue;
        }

        if (pos + 2 > size)
            return fail("LZSS stream ends inside a match");
        uint8_t b0 = data[pos++];
        uint8_t b1 = data[pos++];
        if (b0 == 0 && b1 == 0 && pos < size && data[pos] == 0) {
            if (pos + 1 != size)
                return fail("LZSS terminator %zu bytes before the end of the input", size - pos - 1);
            return true;
        }
        size_t from = (size_t)(b1 >> 4) << 8 | b0;
        size_t length = (size_t)(b1 & 0x0F) + MIN_MATCH;
        for (size_t i = 0; i < length; i++) {
            uint8_t byte = ring[(from + i) % RING_SIZE];
            ring[write_pos++ % RING_SIZE] = byte;
            if (!bytebuf_push(out, byte))
                return false;
        }
    }
}
