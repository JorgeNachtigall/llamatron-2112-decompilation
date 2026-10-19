/*
 * snd_bank.c - loads the sample bank from the asset store (kept apart from snd.c so code that
 * only ticks the driver does not depend on the asset store).
 */
#include "snd.h"

#include "assets.h"

#include <string.h>

enum { IDX_HEADER_SIZE = 16, IDX_ENTRY_SIZE = 8 };

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* samples.idx: "LSMP", u16 version 1, u16 sample_count, u16 rate_count, u8 tc[5], u8 pad,
 * then {u32 offset, u32 length} per sample (tools/extract/FORMATS.md). */
bool snd_load_bank(SoundBank *bank)
{
    size_t idx_size = 0, pcm_size = 0;
    const uint8_t *idx = asset_get("samples.idx", &idx_size);
    const uint8_t *pcm = asset_get("samples.bin", &pcm_size);
    if (!idx || !pcm || idx_size < IDX_HEADER_SIZE || memcmp(idx, "LSMP", 4) != 0 || read_u16(idx + 4) != 1)
        return false;
    unsigned count = read_u16(idx + 6), rates = read_u16(idx + 8);
    if (count != SND_SAMPLE_COUNT || rates != SND_RATE_COUNT ||
        idx_size < IDX_HEADER_SIZE + (size_t)count * IDX_ENTRY_SIZE)
        return false;

    memset(bank, 0, sizeof *bank);
    bank->pcm = pcm;
    bank->pcm_size = pcm_size;
    memcpy(bank->time_constants, idx + 10, SND_RATE_COUNT);
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *e = idx + IDX_HEADER_SIZE + i * IDX_ENTRY_SIZE;
        bank->samples[i].offset = read_u32(e);
        bank->samples[i].length = read_u32(e + 4);
        if ((uint64_t)bank->samples[i].offset + bank->samples[i].length > pcm_size)
            return false;
    }
    return true;
}
