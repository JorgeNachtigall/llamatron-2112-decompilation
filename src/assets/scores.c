/* scores.c - default_scores.dat / user_scores.dat (LSCO) and config.dat (LCFG). */
#include "formats.h"
#include "reader.h"

bool scores_parse(const uint8_t *data, size_t size, HighScoreTable *out)
{
    *out = (HighScoreTable){0};
    Reader r = reader_open(data, size);
    if (!reader_header(&r, "LSCO", FORMAT_LSCO_VERSION))
        return false;
    uint16_t count = reader_u16(&r);
    if (count > HIGH_SCORE_ENTRIES)
        return format_fail("LSCO: %u entries", (unsigned)count);
    for (uint16_t i = 0; i < count; i++) {
        out->entries[i].score = reader_u32(&r);
        const uint8_t *name = reader_bytes(&r, HIGH_SCORE_NAME_SIZE);
        if (name)
            memcpy(out->entries[i].name, name, HIGH_SCORE_NAME_SIZE);
    }
    if (!r.ok || r.pos != size)
        return format_fail("LSCO: %s", r.ok ? "trailing bytes" : "truncated");
    out->count = count;
    return true;
}

bool config_parse(const uint8_t *data, size_t size, GameConfig *out)
{
    *out = (GameConfig){0};
    Reader r = reader_open(data, size);
    if (!reader_header(&r, "LCFG", FORMAT_LCFG_VERSION))
        return false;
    out->present = reader_u8(&r) != 0;
    out->vga = reader_u8(&r) != 0;
    out->joystick = reader_u8(&r) != 0;
    uint8_t sound = reader_u8(&r);
    out->cpu = reader_u8(&r);
    out->flash_mode = reader_u8(&r);
    if (!r.ok || r.pos != size)
        return format_fail("LCFG: %s", r.ok ? "trailing bytes" : "truncated");
    switch (sound) {
    case SOUND_DEVICE_ADLIB:
    case SOUND_DEVICE_SBLASTER:
    case SOUND_DEVICE_ROLAND:
    case SOUND_DEVICE_NONE:
        out->sound = (SoundDevice)sound;
        return true;
    default:
        return format_fail("LCFG: unknown sound device 0x%02x", (unsigned)sound);
    }
}
