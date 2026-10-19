/*
 * think_table.c - entity_think_handlers, game86 tbl_think_handlers (DS:1FD2), indexed by
 * Entity.think. Entries still NULL are not ported yet; the think loop skips them.
 */
#include "entities.h"

const ThinkFn entity_think_handlers[THINK_HANDLER_COUNT] = {
    [0x00] = think_00,      /* 1000:43dc */
    [0x01] = think_01,      /* 1000:43f1 */
    [0x02] = think_02,      /* 1000:43c9 */
    [0x03] = think_03,      /* 1000:4ac9 */
    [0x04] = think_00,
    [0x07] = think_00,
    [0x09] = think_09,      /* 1000:4ec8 */
    [0x0a] = think_00,
    [0x0b] = think_0b,      /* 1000:4ed7 */
    [0x0c] = think_0c,      /* 1000:4feb */
    [0x0d] = think_0d,      /* 1000:504f */
    [0x0f] = think_0f,      /* 1000:3cea */
    [0x10] = think_10,      /* 1000:388a */
    [0x11] = think_11,      /* 1000:382d */
    [0x12] = think_12,      /* 1000:37fe */
    [0x13] = think_13,      /* 1000:256e */
    [0x14] = think_14,      /* 1000:25f1 */
    [0x19] = think_19,      /* 1000:20f7 */
    [0x1a] = think_1a,      /* 1000:2112 */
    [0x1b] = think_1b,      /* 1000:148d */
    [0x1f] = think_1f,      /* 1000:2d6e */
    [0x28] = think_28,      /* 1000:2d5a */
    [0x2a] = think_2a,      /* 1000:2d35 */
    [0x2b] = think_2b,      /* 1000:27e6 */
};
