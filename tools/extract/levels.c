/* Level scripts (data/levels.dat, format in FORMATS.md).
 *
 * tbl_level_scripts (DS:2BDA) holds near pointers up to a 0 entry — 100 levels; a 0 entry makes
 * the game wrap to level 0 and bump the loop count (game86 1000:020b). Each script is:
 *
 *   op*        byte b >= 0x80: handler (-b - 1) of the CS:0245 table, with fixed operands
 *   wave type  byte < 0x80 (wave_run 1000:0455)
 *   spawns     {u8 enemy type (tbl_enemy_init DS:2CA4 index), u8 count} pairs; a pair whose
 *              count byte is 0xFF ends the list (wave_spawn_done 1000:3d09)
 */
#include "extract.h"

#include <stdio.h>

enum {
    DGROUP_SEGMENT = 0x3559,
    LEVEL_TABLE = 0x2BDA,
    MAX_LEVELS = 128,
    MAX_SPAWNS = 64,
    ENEMY_TYPES = 41,      /* tbl_enemy_init entries */
    SPAWN_END = 0xFF,
    OP_RECORD_SIZE = 6,
    MAX_OPERANDS = 4,
};

/* Operand bytes per handler index (= -opcode - 1), from the handlers listed in CS:0245. */
static const uint8_t operand_counts[] = {
    2, /* 0  1000:0421  enemy speed x/y (s8 << loop count) -> 21ac/21ae */
    4, /* 1  1000:0438  four s8 level parameters -> 21b6..21bc */
    4, /* 2  1000:03f4  three bytes -> 21c0/21be/21c2, fourth byte skipped; plays sample 0x16 */
    2, /* 3  1000:03e1  -> 0826, 0818 */
    2, /* 4  1000:03cf  -> 0816, 086e (high byte) */
    2, /* 5  1000:03bc  -> 0830, 0812 */
    3, /* 6  1000:039c  -> 0828 (high byte), 082c (both bytes), 082e */
    1, /* 7  1000:0392  -> 0820 */
};

static const uint8_t *dgroup(const ExtractContext *ctx, uint16_t offset, size_t size)
{
    return game86_at(ctx, DGROUP_SEGMENT, offset, size);
}

/* Parses the script at DS:start into `ops` / `spawns`; writes its level record into `levels`. */
static bool convert_level(const ExtractContext *ctx, int level, uint16_t start, ByteBuf *levels,
                          ByteBuf *ops, ByteBuf *spawns)
{
    uint16_t at = start;
    uint16_t first_op = (uint16_t)(ops->size / OP_RECORD_SIZE), op_count = 0;
    const uint8_t *byte;
    while ((byte = dgroup(ctx, at, 1)) && *byte >= 0x80) {
        unsigned handler = (unsigned)(0xFF - *byte); /* -(int8)b - 1 */
        if (handler >= sizeof operand_counts)
            return fail("level %d: unknown script opcode 0x%02x at DS:%04x", level, *byte, at);
        uint8_t count = operand_counts[handler];
        const uint8_t *operands = dgroup(ctx, (uint16_t)(at + 1), count);
        if (!operands)
            return false;
        uint8_t record[OP_RECORD_SIZE] = {(uint8_t)handler, count, 0, 0, 0, 0};
        for (uint8_t i = 0; i < count; i++)
            record[2 + i] = operands[i];
        if (!bytebuf_append(ops, record, sizeof record))
            return false;
        op_count++;
        at = (uint16_t)(at + 1 + count);
    }
    if (!byte)
        return false;
    uint8_t wave_type = *byte;
    at++;

    uint16_t first_spawn = (uint16_t)(spawns->size / 2), spawn_count = 0;
    for (;; at = (uint16_t)(at + 2)) {
        const uint8_t *pair = dgroup(ctx, at, 2);
        if (!pair)
            return false;
        if (pair[1] == SPAWN_END) {
            if (pair[0] != SPAWN_END)
                return fail("level %d: spawn terminator at DS:%04x is %02x %02x", level, at, pair[0], pair[1]);
            break;
        }
        if (pair[0] >= ENEMY_TYPES)
            return fail("level %d: enemy type %u out of range", level, pair[0]);
        if (++spawn_count > MAX_SPAWNS || !bytebuf_append(spawns, pair, 2))
            return spawn_count > MAX_SPAWNS ? fail("level %d: too many spawns", level) : false;
    }
    return bytebuf_put_u16(levels, start) && bytebuf_put_u16(levels, first_op) && bytebuf_put_u16(levels, op_count) &&
           bytebuf_push(levels, wave_type) && bytebuf_push(levels, (uint8_t)spawn_count) &&
           bytebuf_put_u16(levels, first_spawn);
}

bool extract_levels(ExtractContext *ctx)
{
    ByteBuf levels = {0}, ops = {0}, spawns = {0}, out = {0};
    bool ok = true;
    int count = 0;
    for (;; count++) {
        const uint8_t *entry = dgroup(ctx, (uint16_t)(LEVEL_TABLE + count * 2), 2);
        if (!entry || count == MAX_LEVELS) {
            ok = entry ? fail("level table at DS:%04x has no 0 terminator", LEVEL_TABLE) : false;
            break;
        }
        if (read_u16le(entry) == 0)
            break;
        if (!(ok = convert_level(ctx, count, read_u16le(entry), &levels, &ops, &spawns)))
            break;
    }
    ok = ok && bytebuf_append(&out, (const uint8_t *)"LLVL", 4) && bytebuf_put_u16(&out, FMT_LLVL_VERSION) &&
         bytebuf_put_u16(&out, (uint16_t)count) && bytebuf_append(&out, levels.data, levels.size) &&
         bytebuf_put_u16(&out, (uint16_t)(ops.size / OP_RECORD_SIZE)) && bytebuf_append(&out, ops.data, ops.size) &&
         bytebuf_put_u16(&out, (uint16_t)(spawns.size / 2)) && bytebuf_append(&out, spawns.data, spawns.size) &&
         output_write(ctx->outputs, "data/levels.dat", out.data, out.size);
    ok = ok && index_define(ctx, "LEVEL_COUNT", (unsigned long)count);
    if (ok)
        printf("  %d levels, %zu script ops, %zu spawn entries\n", count, ops.size / OP_RECORD_SIZE,
               spawns.size / 2);
    bytebuf_free(&levels);
    bytebuf_free(&ops);
    bytebuf_free(&spawns);
    bytebuf_free(&out);
    return ok;
}
