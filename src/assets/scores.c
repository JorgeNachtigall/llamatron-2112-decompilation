/* scores.c - default_scores.dat / user_scores.dat (LSCO), config.dat (LCFG), hud_glyphs.dat (LHUD),
 * hiscore.dat (LHSC), enemies.dat (LENM). */
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

bool hud_glyphs_parse(const uint8_t *data, size_t size, HudGlyphs *out)
{
    Reader r = reader_open(data, size);
    if (!reader_header(&r, "LHUD", FORMAT_LHUD_VERSION))
        return false;
    const uint8_t *digits = reader_bytes(&r, sizeof out->digits);
    const uint8_t *labels = reader_bytes(&r, sizeof out->player_label);
    if (!r.ok || r.pos != size)
        return format_fail("LHUD: %s", r.ok ? "trailing bytes" : "truncated");
    memcpy(out->digits, digits, sizeof out->digits);
    memcpy(out->player_label, labels, sizeof out->player_label);
    return true;
}

bool scores_format_yak(const HighScoreTable *table, uint8_t out[SCORES_YAK_SIZE])
{
    if (table->count != HIGH_SCORE_ENTRIES)
        return format_fail("scores.yak needs %d entries, have %u", HIGH_SCORE_ENTRIES, (unsigned)table->count);
    for (int i = 0; i < HIGH_SCORE_ENTRIES; i++) {
        uint32_t score = table->entries[i].score;
        if (score > 99999999u)
            return format_fail("score %u does not fit 8 digits", (unsigned)score);
        uint8_t *entry = out + i * (8 + HIGH_SCORE_NAME_SIZE);
        for (int d = 7; d >= 0; d--, score /= 10)
            entry[d] = (uint8_t)('0' + score % 10);
        memcpy(entry + 8, table->entries[i].name, HIGH_SCORE_NAME_SIZE);
    }
    return true;
}

bool hiscore_data_parse(const uint8_t *data, size_t size, HiscoreData *out)
{
    memset(out, 0, sizeof *out);
    Reader r = reader_open(data, size);
    if (!reader_header(&r, "LHSC", FORMAT_LHSC_VERSION))
        return false;
    const uint8_t *alphabet = reader_bytes(&r, HISCORE_ALPHABET_SIZE);
    const uint8_t *colours = reader_bytes(&r, HISCORE_ROW_COUNT);
    const uint8_t *word = reader_bytes(&r, HISCORE_RUDE_WORD_SIZE);
    if (!r.ok || r.pos != size)
        return format_fail("LHSC: %s", r.ok ? "trailing bytes" : "truncated");
    memcpy(out->alphabet, alphabet, HISCORE_ALPHABET_SIZE);
    memcpy(out->row_colours, colours, HISCORE_ROW_COUNT);
    memcpy(out->rude_word, word, HISCORE_RUDE_WORD_SIZE);
    return true;
}

bool enemy_tables_parse(const uint8_t *data, size_t size, EnemyTables *out)
{
    memset(out, 0, sizeof *out);
    Reader r = reader_open(data, size);
    if (!reader_header(&r, "LENM", FORMAT_LENM_VERSION))
        return false;
    out->score_award_count = reader_u16(&r);
    if (out->score_award_count > SCORE_AWARDS_MAX)
        return format_fail("LENM: %u score awards", (unsigned)out->score_award_count);
    for (uint16_t i = 0; i < out->score_award_count; i++) {
        out->score_awards[i].count_minus_1 = reader_u16(&r);
        out->score_awards[i].digit = reader_u16(&r);
    }
    for (int i = 0; i < DROP_DIRECTIONS; i++)
        out->drop_by_direction[i] = (int8_t)reader_u8(&r);
    for (int i = 0; i < PICKUP_KINDS; i++)
        out->pickup_sprites[i] = reader_u16(&r);
    if (reader_u16(&r) != DEBRIS_TEMPLATE_COUNT && r.ok)
        return format_fail("LENM: debris count is not %d", DEBRIS_TEMPLATE_COUNT);
    for (int i = 0; i < DEBRIS_TEMPLATE_COUNT; i++) {
        out->debris[i].sprite = reader_u16(&r);
        out->debris[i].vx = (int16_t)reader_u16(&r);
        out->debris[i].vy = (int16_t)reader_u16(&r);
    }
    if (!r.ok || r.pos != size)
        return format_fail("LENM: %s", r.ok ? "trailing bytes" : "truncated");
    return true;
}
