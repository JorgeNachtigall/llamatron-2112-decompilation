/* Sound samples (docs/re-notes-sound-data.md §1-2). llatron.spl is headerless 8-bit unsigned
 * mono PCM; its directory lives in the Sound Blaster driver:
 *
 *   sblaster.bin 0x609  34 x {u16 offset_lo, u16 offset_hi, u16 length, u16 loop_start,
 *                       u16 loop_length}; entry = sample number + 1, entries 0/1 are dummies,
 *                       loop fields are never used by the driver
 *   sblaster.bin 0x3d2  5 DSP time constants for rate index 0..4 (rate = 1e6 / (256 - tc))
 *
 * Outputs: data/samples.bin (each sample's PCM, back to back; overlapping originals are copied
 * so every sample is independent) and data/samples.idx (format in FORMATS.md). */
#include "extract.h"

#include <stdio.h>

enum {
    SAMPLE_TABLE_OFFSET = 0x609,
    SAMPLE_TABLE_ENTRIES = 34,
    SAMPLE_ENTRY_SIZE = 10,
    FIRST_SAMPLE_ENTRY = 2, /* sample 1 */
    SAMPLE_COUNT = SAMPLE_TABLE_ENTRIES - FIRST_SAMPLE_ENTRY,
    RATE_TABLE_OFFSET = 0x3d2,
    RATE_COUNT = 5,
    PCM_SILENCE = 0x80, /* unsigned 8-bit centre */
};

static bool load_original(const char *original_dir, const char *name, ByteBuf *out)
{
    char path[4096];
    if (!find_original(original_dir, name, path, sizeof path))
        return fail("%s not found", name);
    return read_file(path, out);
}

static bool put_u16(ByteBuf *buf, uint16_t value)
{
    uint8_t bytes[2];
    write_u16le(bytes, value);
    return bytebuf_append(buf, bytes, 2);
}

static bool put_u32(ByteBuf *buf, uint32_t value)
{
    return put_u16(buf, (uint16_t)(value & 0xFFFF)) && put_u16(buf, (uint16_t)(value >> 16));
}

/* sblaster.bin 0x3ed (start), 0x10f (request) */
bool extract_samples(ExtractContext *ctx)
{
    ByteBuf driver = {0}, bank = {0}, pcm = {0}, index = {0};
    bool ok = load_original(ctx->original_dir, "sblaster.bin", &driver) &&
              load_original(ctx->original_dir, "llatron.spl", &bank);
    if (ok && driver.size < SAMPLE_TABLE_OFFSET + SAMPLE_TABLE_ENTRIES * SAMPLE_ENTRY_SIZE)
        ok = fail("sblaster.bin too short for the sample table");

    ok = ok && bytebuf_append(&index, (const uint8_t *)"LSMP", 4) && put_u16(&index, 1) &&
         put_u16(&index, SAMPLE_COUNT) && put_u16(&index, RATE_COUNT) &&
         bytebuf_append(&index, driver.data + RATE_TABLE_OFFSET, RATE_COUNT) &&
         bytebuf_push(&index, 0); /* pad to even */

    for (int sample = 1; ok && sample <= SAMPLE_COUNT; sample++) {
        const uint8_t *entry = driver.data + SAMPLE_TABLE_OFFSET + (sample + 1) * SAMPLE_ENTRY_SIZE;
        uint32_t offset = (uint32_t)read_u16le(entry) | (uint32_t)read_u16le(entry + 2) << 16;
        uint32_t length = read_u16le(entry + 4);
        if (offset > bank.size) {
            ok = fail("sample %d starts at 0x%x, outside llatron.spl", sample, offset);
            break;
        }
        /* Sample 30's entry runs 96 bytes past the end of llatron.spl; the driver's DMA then
         * plays whatever DOS memory follows the bank. We pad with silence (deviation,
         * FORMATS.md). */
        size_t present = bank.size - offset < length ? bank.size - offset : length;
        ok = put_u32(&index, (uint32_t)pcm.size) && put_u32(&index, length) &&
             bytebuf_append(&pcm, bank.data + offset, present);
        for (size_t i = present; ok && i < length; i++)
            ok = bytebuf_push(&pcm, PCM_SILENCE);
    }

    ok = ok && output_write(ctx->outputs, "data/samples.bin", pcm.data, pcm.size) &&
         output_write(ctx->outputs, "data/samples.idx", index.data, index.size);
    if (ok)
        printf("  %d samples, %zu bytes of PCM\n", SAMPLE_COUNT, pcm.size);
    bytebuf_free(&driver);
    bytebuf_free(&bank);
    bytebuf_free(&pcm);
    bytebuf_free(&index);
    return ok;
}
