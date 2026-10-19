/* samples.c - samples.idx (LSMP) over samples.bin. */
#include "formats.h"
#include "reader.h"

#include <stdlib.h>

bool samples_parse(const uint8_t *index, size_t index_size, const uint8_t *pcm, size_t pcm_size,
                   SampleSet *out)
{
    *out = (SampleSet){0};
    Reader r = reader_open(index, index_size);
    if (!reader_header(&r, "LSMP", FORMAT_LSMP_VERSION))
        return false;
    uint16_t sample_count = reader_u16(&r);
    uint16_t rate_count = reader_u16(&r);
    if (rate_count > sizeof out->time_constants)
        return format_fail("LSMP: %u rates", (unsigned)rate_count);
    const uint8_t *time_constants = reader_bytes(&r, rate_count);
    if (rate_count & 1)
        reader_u8(&r); /* pad to even */
    if (time_constants)
        memcpy(out->time_constants, time_constants, rate_count);
    Sample *samples = calloc(sample_count ? sample_count : 1, sizeof *samples);
    if (!samples)
        return format_fail("out of memory");
    for (uint16_t i = 0; i < sample_count; i++) {
        uint32_t offset = reader_u32(&r), length = reader_u32(&r);
        if (r.ok && (offset > pcm_size || length > pcm_size - offset)) {
            free(samples);
            return format_fail("LSMP: sample %u outside samples.bin", (unsigned)i + 1);
        }
        samples[i] = (Sample){pcm + offset, length};
    }
    if (!r.ok || r.pos != index_size) {
        free(samples);
        return format_fail("LSMP: %s", r.ok ? "trailing bytes" : "truncated");
    }
    out->sample_count = sample_count;
    out->samples = samples;
    out->rate_count = rate_count;
    return true;
}

void samples_free(SampleSet *set)
{
    free(set->samples);
    *set = (SampleSet){0};
}

unsigned samples_rate_hz(const SampleSet *set, unsigned rate)
{
    if (rate >= set->rate_count)
        return 0;
    return 1000000u / (256u - set->time_constants[rate]);
}
