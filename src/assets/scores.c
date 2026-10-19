/* scores.c - default_scores.dat / user_scores.dat (LSCO), config.dat (LCFG), hud_glyphs.dat (LHUD),
 * hiscore.dat (LHSC), enemies.dat (LENM), controls.dat (LCTL),
 * title.dat (LTTL). */
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

bool scores_parse_yak(const uint8_t *data, size_t size, HighScoreTable *out)
{
    *out = (HighScoreTable){0};
    if (size != SCORES_YAK_SIZE)
        return format_fail("scores.yak is %u bytes, not %d", (unsigned)size, SCORES_YAK_SIZE);
    for (int i = 0; i < HIGH_SCORE_ENTRIES; i++) {
        const uint8_t *entry = data + i * (8 + HIGH_SCORE_NAME_SIZE);
        uint32_t score = 0;
        for (int d = 0; d < 8; d++) {
            if (entry[d] < '0' || entry[d] > '9') {
                *out = (HighScoreTable){0};
                return format_fail("scores.yak entry %d has a non-digit score", i);
            }
            score = score * 10 + (uint32_t)(entry[d] - '0');
        }
        out->entries[i].score = score;
        memcpy(out->entries[i].name, entry + 8, HIGH_SCORE_NAME_SIZE);
    }
    out->count = HIGH_SCORE_ENTRIES;
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
    for (int i = 0; i < SEEK_QUADRANTS; i++)
        out->seek_dir_by_quadrant[i] = (int8_t)reader_u8(&r);
    int16_t *word_tables[] = {out->seek_vx, out->seek_vy, out->wander_vx, out->wander_vy, out->wander_sprite};
    for (size_t t = 0; t < sizeof word_tables / sizeof *word_tables; t++)
        for (int i = 0; i < ENEMY_DIRECTIONS; i++)
            word_tables[t][i] = (int16_t)reader_u16(&r);
    if (!r.ok || r.pos != size)
        return format_fail("LENM: %s", r.ok ? "trailing bytes" : "truncated");
    return true;
}

bool control_tables_parse(const uint8_t *data, size_t size, ControlTables *out)
{
    memset(out, 0, sizeof *out);
    Reader r = reader_open(data, size);
    if (!reader_header(&r, "LCTL", FORMAT_LCTL_VERSION))
        return false;
    void *nibble_tables[] = {out->shot_vx, out->shot_vy, out->shot_direction, out->spread_a, out->spread_b,
                             out->shot_bounce_x, out->shot_bounce_y,
                             out->move_vx, out->move_vy, out->ship_facing, out->trail_flag};
    void *quadrant_tables[] = {out->droid_shot_vx, out->droid_shot_vy, out->droid_shot_direction, out->droid_aim};
    for (size_t i = 0; i < sizeof nibble_tables / sizeof *nibble_tables; i++) {
        const uint8_t *bytes = reader_bytes(&r, INPUT_NIBBLES);
        if (bytes)
            memcpy(nibble_tables[i], bytes, INPUT_NIBBLES);
    }
    for (size_t i = 0; i < sizeof quadrant_tables / sizeof *quadrant_tables; i++) {
        const uint8_t *bytes = reader_bytes(&r, DROID_QUADRANTS);
        if (bytes)
            memcpy(quadrant_tables[i], bytes, DROID_QUADRANTS);
    }
    if (!r.ok || r.pos != size)
        return format_fail("LCTL: %s", r.ok ? "trailing bytes" : "truncated");
    return true;
}

bool title_tables_parse(const uint8_t *data, size_t size, TitleTables *out)
{
    memset(out, 0, sizeof *out);
    Reader r = reader_open(data, size);
    if (!reader_header(&r, "LTTL", FORMAT_LTTL_VERSION))
        return false;
    const uint8_t *letters = reader_bytes(&r, TITLE_LETTER_SPRITE_COUNT);
    if (letters)
        memcpy(out->letter_sprites, letters, TITLE_LETTER_SPRITE_COUNT);
    for (int i = 0; i < TITLE_LLAMA_SPRITE_COUNT; i++)
        out->llama_sprites[i] = reader_u16(&r);
    out->tune_length = reader_u16(&r);
    if (r.ok && (out->tune_length == 0 || out->tune_length > TITLE_TUNE_MAX || out->tune_length % 2 == 0))
        return format_fail("LTTL: tune length %u", (unsigned)out->tune_length);
    const uint8_t *tune = reader_bytes(&r, out->tune_length);
    if (!r.ok || r.pos != size)
        return format_fail("LTTL: %s", r.ok ? "trailing bytes" : "truncated");
    memcpy(out->tune, tune, out->tune_length);
    if (out->tune[out->tune_length - 1] != 0)
        return format_fail("LTTL: tune does not end with a 0 rate byte");
    return true;
}
