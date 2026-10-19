/*
 * hiscore.c - the hi-score table and name entry (see hiscore.h).
 */
#include "hiscore.h"

#include <string.h>

enum {
    LAST_ROW = HISCORE_COUNT - 1,
    /* table layout, relative to the pen (1000:5c6a..5d07) */
    LOGO_Y = 0xa0,
    LOGO_LEFT_X = 0x30,
    LOGO_RIGHT_X = 0x115,
    ROWS_TOP = 0x1e,
    ROWS_HEIGHT = 0xb4,
    ROW_STEP = 0x0f,
    MARK_X = 0xa0,
    MARK_TOP = 0x23,
    /* one row (1000:5d5d) */
    ROW_SCORE_X = 0x50,
    ROW_NAME_X = 0xb4,
    /* name entry (1000:59fa..5c3b) */
    ENTRY_LOGO_LEFT_X = 0x2d,
    ENTRY_LETTER_COLOR = 1,
    GRID_LEFT = 0x37,
    GRID_TOP = 0x53,
    GRID_COLUMN_STEP = 20,
    GRID_ROW_STEP = 32,
    CURSOR_DX = 3,
    CURSOR_DY = 4,
    NAME_LINE_CELL = 0x960,               /* rows 60..75 cleared before the name is drawn */
    NAME_LINE_CELLS = 0x140 * 2,
    PROMPT_MAX = 64,
    MOVE_TICK_MASK = 7,
};

/* Sound requests of the name entry (1000:5b39, 5ba8, 5bd7, 5bea) */
static const NameEntrySound SOUND_MOVE = { 0x07, 4, 2 };
static const NameEntrySound SOUND_LETTER = { 0x18, 1, 10 };
static const NameEntrySound SOUND_DELETE = { 0x0c, 1, 10 };
static const NameEntrySound SOUND_END = { 0x1a, 0, 10 };
static const NameEntrySound SOUND_NONE = { 0, 0, 0 };

static int current_player(const GameState *game)
{
    return (game->options & OPT_CURRENT_IS_P2) ? 1 : 0;
}

/* ---- The table ------------------------------------------------------------------------ */

/* game86 1000:5da2..5dd8 */
int hiscore_find_rank(const HiscoreEntry table[HISCORE_COUNT], const uint8_t score[SCORE_DIGITS])
{
    for (int rank = 0; rank < HISCORE_COUNT; rank++) {
        for (int d = 0; d < SCORE_DIGITS; d++) {
            int8_t held = (int8_t)(table[rank].digits[d] - '0');
            int8_t mine = (int8_t)score[d];
            if (held < mine)
                return rank;
            if (held > mine)
                break;
        }
    }
    return HISCORE_NO_RANK;
}

/* game86 1000:5dda..5e44 */
void hiscore_insert(HiscoreEntry table[HISCORE_COUNT], int16_t marks[PLAYER_COUNT], int player,
                    int rank, const uint8_t score[SCORE_DIGITS])
{
    int16_t *other = &marks[1 - player];
    marks[player] = (int16_t)rank;
    if (rank <= *other) {                 /* the other player's row moves down */
        (*other)++;
        if (*other > LAST_ROW)
            *other = HISCORE_NO_RANK;
    }
    if (rank != LAST_ROW)
        memmove(&table[rank + 1], &table[rank], (size_t)(LAST_ROW - rank) * sizeof table[0]);
    for (int d = 0; d < SCORE_DIGITS; d++)
        table[rank].digits[d] = (char)(score[d] + '0');
}

int hiscore_check_insert(GameState *game)
{
    int player = current_player(game);
    int rank = hiscore_find_rank(game->hiscores, game->score[player]);
    if (rank != HISCORE_NO_RANK)
        hiscore_insert(game->hiscores, game->hiscore_mark, player, rank, game->score[player]);
    return rank;
}

/* game86 1000:5e4b..5e85. The original also compares past the 8 characters, where the bytes
 * after the name buffer (a NUL and the \e of the prompt) never match. */
void hiscore_set_name(HiscoreEntry *entry, const char name[HISCORE_NAME_LENGTH],
                      const char rude_word[HISCORE_RUDE_WORD_SIZE], bool *rude)
{
    for (int i = 0; i + HISCORE_RUDE_WORD_SIZE <= HISCORE_NAME_LENGTH; i++)
        if (memcmp(&name[i], rude_word, HISCORE_RUDE_WORD_SIZE) == 0)
            *rude = true;
    for (int i = 0; i < HISCORE_NAME_LENGTH; i++)
        entry->name[i] = name[i] == NAME_ENTRY_BLANK ? ' ' : name[i];
}

/* text_draw_char 1000:54f6 at an explicit position: returns x after the glyph. */
static int draw_char_at(const DrawTarget *target, const TextState *text, unsigned char ch,
                        int x, int y)
{
    text_draw_glyph(target, text, ch, x, y);
    return (int16_t)(x + text_glyph_advance(ch));
}

/* game86 1000:5d5d */
void hiscore_draw_row(const DrawTarget *target, TextState *text, const HiscoreEntry *entry,
                      uint8_t color, int y)
{
    text->color = color & 0x0f;
    text->clear_planes = (uint8_t)(~color & 0x0f);   /* rows are drawn opaque */
    int first = 0;
    while (first < SCORE_DIGITS && entry->digits[first] == '0')
        first++;
    int x = ROW_SCORE_X;
    for (int d = first; d < SCORE_DIGITS; d++)
        x = draw_char_at(target, text, (unsigned char)entry->digits[d], x, y);
    x = ROW_NAME_X;
    for (int i = 0; i < HISCORE_NAME_LENGTH; i++)
        x = draw_char_at(target, text, (unsigned char)entry->name[i], x, y);
}

/* game86 1000:5c3c..5d13 */
void hiscore_table_draw(const Scroller *scroller, TextState *text,
                        const HiscoreEntry table[HISCORE_COUNT],
                        const int16_t marks[PLAYER_COUNT], const HiscoreArt *art)
{
    DrawTarget canvas = scroller_canvas();
    scroller_clear_below(scroller);
    draw_set_dirty_recording(false);
    int16_t logo_y = (int16_t)(LOGO_Y + text->y);
    if (art->logo_left)
        draw_sprite(&canvas, art->logo_left, LOGO_LEFT_X, logo_y);
    if (art->logo_right)
        draw_sprite(&canvas, art->logo_right, LOGO_RIGHT_X, logo_y);
    text_print(&canvas, text, art->table_heading);

    int16_t top = text->y;
    int row = 0;
    for (int16_t y = (int16_t)(top + ROWS_TOP);
         y < (int16_t)(top + ROWS_HEIGHT) && row < HISCORE_COUNT; y = (int16_t)(y + ROW_STEP), row++)
        hiscore_draw_row(&canvas, text, &table[row], art->data->row_colours[row], y);

    for (int p = 0; p < PLAYER_COUNT; p++)
        if (marks[p] >= 0 && art->marker[p])
            draw_sprite(&canvas, art->marker[p], MARK_X, top + marks[p] * ROW_STEP + MARK_TOP);
}

/* game86 1000:5d19..5d39 */
bool hiscore_table_scroll_frame(Scroller *scroller, int *rows_left, bool skip)
{
    scroller_scroll_row(scroller);
    if (skip)
        return true;
    return --*rows_left <= 0;
}

/* ---- Name entry ------------------------------------------------------------------------ */

/* game86 1000:5bfe */
void name_entry_cursor_pos(int index, int *x, int *y)
{
    *x = index % NAME_ENTRY_GRID_COLUMNS * GRID_COLUMN_STEP + GRID_LEFT;
    *y = index / NAME_ENTRY_GRID_COLUMNS * GRID_ROW_STEP + GRID_TOP;
}

/* game86 1000:59fa..5a85 */
void name_entry_begin(NameEntry *entry, TextState *text, uint8_t options, const HiscoreArt *art)
{
    memset(entry->name, NAME_ENTRY_BLANK, sizeof entry->name);
    entry->length = 0;
    entry->cursor = 0;

    gfx_clear(GFX_PAGE_BACKGROUND, 0);
    DrawTarget background = draw_target_page(GFX_PAGE_BACKGROUND);
    draw_set_dirty_recording(false);
    hud_draw_control_labels(&background, options, art->control_labels);
    if (art->logo_left)
        draw_sprite(&background, art->logo_left, ENTRY_LOGO_LEFT_X, LOGO_Y);
    if (art->logo_right)
        draw_sprite(&background, art->logo_right, LOGO_RIGHT_X, LOGO_Y);
    text->opaque = true;
    text_print_home(&background, text, art->entry_heading);
    text->opaque = false;

    text->color = ENTRY_LETTER_COLOR;
    text->clear_planes = (uint8_t)(~ENTRY_LETTER_COLOR & 0x0f);
    for (int i = 0; i < HISCORE_ALPHABET_SIZE; i++) {
        int x, y;
        name_entry_cursor_pos(i, &x, &y);
        draw_char_at(&background, text, (unsigned char)art->data->alphabet[i], x & 0xff, y);
    }
}

/* The prompt with the name in place of its \q text (the original prints the name buffer,
 * which lies inside the prompt string). */
static void build_prompt(char out[PROMPT_MAX], const char *prompt, const char name[HISCORE_NAME_LENGTH])
{
    int n = 0;
    while (n + 2 < PROMPT_MAX && !(prompt[n] == TEXT_ESCAPE && prompt[n + 1] == 'e'))
        n++;
    memcpy(out, prompt, (size_t)n);
    out[n] = TEXT_ESCAPE;
    out[n + 1] = 'e';
    for (int i = 0; i + 1 < n; i++) {
        if (prompt[i] == TEXT_ESCAPE && prompt[i + 1] == 'q') {
            int room = n - (i + 2);
            memcpy(&out[i + 2], name, (size_t)(room < HISCORE_NAME_LENGTH ? room : HISCORE_NAME_LENGTH));
            break;
        }
    }
}

/* game86 1000:5ac4 */
void name_entry_draw(const NameEntry *entry, const DrawTarget *target, TextState *text,
                     const HiscoreArt *art)
{
    int x, y;
    name_entry_cursor_pos(entry->cursor, &x, &y);
    if (art->cursor)
        draw_sprite(target, art->cursor, x + CURSOR_DX, y + CURSOR_DY);
    for (int32_t i = 0; i < NAME_LINE_CELLS; i++)
        draw_cell_paint(target, NAME_LINE_CELL + i, 0xff, 0x0f, 0);

    char prompt[PROMPT_MAX];
    build_prompt(prompt, art->entry_prompt, entry->name);
    text->opaque = true;
    text_print_home(target, text, prompt);
    text->opaque = false;
}

static NameEntrySound move_cursor(NameEntry *entry, int step, uint16_t *tick_count)
{
    int cursor = entry->cursor + step;
    if (cursor < 0)
        cursor += HISCORE_ALPHABET_SIZE;
    else if (cursor >= HISCORE_ALPHABET_SIZE)
        cursor -= HISCORE_ALPHABET_SIZE;
    entry->cursor = (int16_t)cursor;
    *tick_count = 0;
    return SOUND_MOVE;
}

/* game86 1000:5b06 */
NameEntrySound name_entry_tick(NameEntry *entry, uint8_t input, uint16_t *tick_count,
                               const HiscoreArt *art)
{
    enum { UP = 0x01, DOWN = 0x02, LEFT = 0x04, RIGHT = 0x08, FIRE = 0x80 };
    if ((*tick_count & MOVE_TICK_MASK) == 0) {
        if (input & (UP | DOWN))
            return move_cursor(entry, (input & DOWN) ? NAME_ENTRY_GRID_COLUMNS
                                                     : -NAME_ENTRY_GRID_COLUMNS, tick_count);
        if (input & (LEFT | RIGHT))
            return move_cursor(entry, (input & RIGHT) ? 1 : -1, tick_count);
    }
    if (!(input & FIRE)) {
        entry->fire_held = false;
        return SOUND_NONE;
    }
    if (entry->fire_held)
        return SOUND_NONE;
    entry->fire_held = true;
    *tick_count = 0;

    char letter = art->data->alphabet[entry->cursor];
    int length = entry->length;
    if (letter == NAME_ENTRY_END) {
        *tick_count = NAME_ENTRY_TIMEOUT_TICKS;
        return SOUND_END;
    }
    if (letter == NAME_ENTRY_DELETE) {
        if (length == 0)
            return SOUND_NONE;
        /* On the last character a set one is blanked first, staying there. */
        if (length == HISCORE_NAME_LENGTH - 1 && entry->name[length] != NAME_ENTRY_BLANK) {
            entry->name[length] = NAME_ENTRY_BLANK;
        } else {
            length--;
            entry->name[length] = NAME_ENTRY_BLANK;
        }
        entry->length = (int16_t)length;
        return SOUND_DELETE;
    }
    entry->name[length] = letter;
    if (++length == HISCORE_NAME_LENGTH)
        length = HISCORE_NAME_LENGTH - 1;
    entry->length = (int16_t)length;
    return SOUND_LETTER;
}
