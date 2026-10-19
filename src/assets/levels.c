/* levels.c - levels.dat (LLVL): the 100 level scripts of tbl_level_scripts (DS:2BDA). */
#include "formats.h"
#include "reader.h"

#include <stdlib.h>
#include <string.h>

enum { LEVEL_RECORD_SIZE = 10, OP_RECORD_SIZE = 6 };

bool levels_parse(const uint8_t *data, size_t size, LevelSet *out)
{
    *out = (LevelSet){0};
    Reader r = reader_open(data, size);
    if (!reader_header(&r, "LLVL", FORMAT_LLVL_VERSION))
        return false;
    uint16_t level_count = reader_u16(&r);
    const uint8_t *level_records = reader_bytes(&r, (size_t)level_count * LEVEL_RECORD_SIZE);
    uint16_t op_count = reader_u16(&r);
    const uint8_t *op_records = reader_bytes(&r, (size_t)op_count * OP_RECORD_SIZE);
    uint16_t spawn_count = reader_u16(&r);
    const uint8_t *spawn_records = reader_bytes(&r, (size_t)spawn_count * 2);
    LevelSpawnClobber clobber;
    clobber.level = reader_u16(&r);
    clobber.first_spawn = reader_u16(&r);
    clobber.spawn_count = reader_u16(&r);
    uint16_t spawn_sprites[LEVEL_VARIANTS], warp_sprites[LEVEL_VARIANTS];
    for (int i = 0; i < LEVEL_VARIANTS; i++)
        spawn_sprites[i] = reader_u16(&r);
    for (int i = 0; i < LEVEL_VARIANTS; i++)
        warp_sprites[i] = reader_u16(&r);
    const uint8_t *warp_box_colours = reader_bytes(&r, WARP_BOX_COLOUR_COUNT);
    if (!r.ok || r.pos != size)
        return format_fail("LLVL: %s", r.ok ? "trailing bytes" : "truncated");

    out->levels = calloc(level_count ? level_count : 1, sizeof *out->levels);
    out->ops = calloc(op_count ? op_count : 1, sizeof *out->ops);
    out->spawns = calloc(spawn_count ? spawn_count : 1, sizeof *out->spawns);
    if (!out->levels || !out->ops || !out->spawns) {
        levels_free(out);
        return format_fail("out of memory");
    }
    out->level_count = level_count;

    bool ok = true;
    for (uint16_t i = 0; ok && i < op_count; i++) {
        const uint8_t *record = op_records + i * OP_RECORD_SIZE;
        LevelOp *op = &out->ops[i];
        op->handler = record[0];
        op->operand_count = record[1];
        memcpy(op->operands, record + 2, LEVEL_OP_MAX_OPERANDS);
        if (op->handler >= LEVEL_OP_HANDLERS || op->operand_count > LEVEL_OP_MAX_OPERANDS)
            ok = format_fail("LLVL: op %u has handler %u with %u operands", (unsigned)i,
                             (unsigned)op->handler, (unsigned)op->operand_count);
    }
    for (uint16_t i = 0; i < spawn_count; i++)
        out->spawns[i] = (LevelSpawn){spawn_records[i * 2], spawn_records[i * 2 + 1]};
    for (uint16_t i = 0; ok && i < level_count; i++) {
        Reader l = reader_open(level_records + i * LEVEL_RECORD_SIZE, LEVEL_RECORD_SIZE);
        LevelScript *level = &out->levels[i];
        level->ds_offset = reader_u16(&l);
        uint16_t first_op = reader_u16(&l);
        level->op_count = reader_u16(&l);
        level->wave_type = reader_u8(&l);
        level->spawn_count = reader_u8(&l);
        uint16_t first_spawn = reader_u16(&l);
        if (first_op > op_count || level->op_count > op_count - first_op ||
            first_spawn > spawn_count || level->spawn_count > spawn_count - first_spawn)
            ok = format_fail("LLVL: level %u points outside the op or spawn area", (unsigned)i);
        level->ops = out->ops + first_op;
        level->spawns = out->spawns + first_spawn;
    }
    if (ok && clobber.level != LEVEL_NONE &&
        (clobber.level >= level_count || clobber.first_spawn > out->levels[clobber.level].spawn_count ||
         clobber.spawn_count > out->levels[clobber.level].spawn_count - clobber.first_spawn))
        ok = format_fail("LLVL: spawn clobber outside level %u's spawns", (unsigned)clobber.level);
    out->clobber_10b0 = clobber;
    memcpy(out->spawn_sprites, spawn_sprites, sizeof spawn_sprites);
    memcpy(out->warp_sprites, warp_sprites, sizeof warp_sprites);
    memcpy(out->warp_box_colours, warp_box_colours, WARP_BOX_COLOUR_COUNT);
    if (!ok)
        levels_free(out);
    return ok;
}

void levels_free(LevelSet *set)
{
    free(set->levels);
    free(set->ops);
    free(set->spawns);
    *set = (LevelSet){0};
}
