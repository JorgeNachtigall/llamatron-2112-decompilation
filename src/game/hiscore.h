/*
 * hiscore.h - the hi-score table: ranking a score, inserting it, the name-entry screen and
 * drawing the table (game86 1000:59fa..5ead).
 *
 * The table is GameState.hiscores (best first, 8 ASCII digits + 8 name characters per entry,
 * as game86 keeps it at DS:40A0; the host converts it from and to scores.yak). A new entry's
 * row is marked for each player (GameState.hiscore_mark) and drawn with a marker sprite.
 *
 * The original runs the name entry and the table's scroll as blocking loops; here they are
 * calls per tick / frame for the game's state machine, which also plays the tunes, reveals the
 * name-entry screen (screen_reveal_bg 1000:61ff) and saves the table.
 */
#ifndef LLAMATRON_GAME_HISCORE_H
#define LLAMATRON_GAME_HISCORE_H

#include <stdbool.h>
#include <stdint.h>

#include "draw.h"
#include "formats.h"
#include "hud.h"
#include "scroller.h"
#include "state.h"
#include "text.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    HISCORE_NO_RANK = -1,
    HISCORE_TABLE_SCROLL_ROWS = 200,      /* 1000:5d16 */
    NAME_ENTRY_TIMEOUT_TICKS = 6000,      /* 1000:5ab3: two minutes without a key */
    NAME_ENTRY_GRID_COLUMNS = 11,
    NAME_ENTRY_DELETE = '$',              /* alphabet symbols with a function (1000:5b8d) */
    NAME_ENTRY_END = '*',
    NAME_ENTRY_BLANK = '-',               /* unset name character, saved as ' ' */
};

/* Pictures and texts of the two screens, from the game data. */
typedef struct HiscoreArt {
    const GfxSprite *logo_left;           /* g3 shape 15 (30EB:001E) */
    const GfxSprite *logo_right;          /* g3 shape 14 (30EB:001C) */
    const GfxSprite *cursor;              /* g3 shape 0 (30EB:0000) */
    const GfxSprite *marker[PLAYER_COUNT];/* game bank 218C:245A / 218C:4268 */
    HudShapeLookup control_labels;        /* g3 shapes for hud_draw_control_labels */
    const char *table_heading;            /* escaped text DS:0AAE */
    const char *entry_heading;            /* escaped text DS:094B */
    const char *entry_prompt;             /* escaped text DS:09B5; its \q text is the name */
    const HiscoreData *data;              /* alphabet, row colours, the rude word */
} HiscoreArt;

/* ---- The table ------------------------------------------------------------------------ */

/* Row a score would take: the first entry whose score is lower (digit by digit; equal scores
 * go below), or HISCORE_NO_RANK. game86 1000:5da2..5dd8 */
int hiscore_find_rank(const HiscoreEntry table[HISCORE_COUNT], const uint8_t score[SCORE_DIGITS]);

/* Put the score of `player` at row `rank`: the rows below move down one (the last drops
 * out), the row keeps the moved-down entry's name until hiscore_set_name, and the marks
 * follow (`player`'s mark becomes rank; the other player's moves down with its row).
 * game86 1000:5dda..5e44 */
void hiscore_insert(HiscoreEntry table[HISCORE_COUNT], int16_t marks[PLAYER_COUNT], int player,
                    int rank, const uint8_t score[SCORE_DIGITS]);

/* Rank and insert the current player's score (OPT_CURRENT_IS_P2); HISCORE_NO_RANK if it
 * does not make the table. Then run the name entry and hiscore_set_name. */
int hiscore_check_insert(GameState *game);

/* Store the entered name in row `rank` (NAME_ENTRY_BLANK -> ' '); a name containing the rude
 * word sets *rude. game86 1000:5e4b..5e85 */
void hiscore_set_name(HiscoreEntry *entry, const char name[HISCORE_NAME_LENGTH],
                      const char rude_word[HISCORE_RUDE_WORD_SIZE], bool *rude);

/* Draw the table on the canvas below the scroller's window, from the text pen's row: the
 * blank area, the logos, the heading, ten rows (score without leading zeros, name) in their
 * colours, opaque, and the players' marks. Leaves the pen after the heading and the text
 * colour of the last row, as the original. game86 1000:5c3c..5d13 (5c3f..5c54: the tune,
 * left to the caller) */
void hiscore_table_draw(const Scroller *scroller, TextState *text,
                        const HiscoreEntry table[HISCORE_COUNT],
                        const int16_t marks[PLAYER_COUNT], const HiscoreArt *art);

/* One row: score digits from x 80 without leading zeros (none for a zero score), name from
 * x 180. game86 1000:5d5d */
void hiscore_draw_row(const DrawTarget *target, TextState *text, const HiscoreEntry *entry,
                      uint8_t color, int y);

/* One frame of the table's scroll: the window moves one row; returns true when it is done
 * (*rows_left frames, or `skip` seen after the move). Start with *rows_left =
 * HISCORE_TABLE_SCROLL_ROWS. game86 1000:5d19..5d39 */
bool hiscore_table_scroll_frame(Scroller *scroller, int *rows_left, bool skip);

/* ---- Name entry ------------------------------------------------------------------------ */

typedef struct NameEntry {
    char name[HISCORE_NAME_LENGTH];       /* DS:09BE, NAME_ENTRY_BLANK where unset */
    int16_t length;                       /* 0822: next character's index, 0..7 */
    int16_t cursor;                       /* 0947: alphabet index 0..32 */
    bool fire_held;                       /* 0949: fire must be released between letters */
} NameEntry;

typedef struct NameEntrySound {           /* a sound driver AH=3 request; sample 0 = none */
    uint8_t sample, rate_index, priority;
} NameEntrySound;

/* Screen position of alphabet index `index` (11 per row). game86 1000:5bfe */
void name_entry_cursor_pos(int index, int *x, int *y);

/* Build the name-entry screen in the background page (the caller then reveals it and sets
 * the tick count to 0). fire_held carries over from the last entry, as in the original.
 * game86 1000:59fa..5a85, 5a88, 5aa2 */
void name_entry_begin(NameEntry *entry, TextState *text, uint8_t options, const HiscoreArt *art);

/* The frame's drawing: cursor sprite, then the name line redrawn. game86 1000:5ac4 */
void name_entry_draw(const NameEntry *entry, const DrawTarget *target, TextState *text,
                     const HiscoreArt *art);

/* One timer tick (after tick_count was advanced): every 8th tick a direction moves the
 * cursor; otherwise a fresh fire press takes the letter (or deletes, or ends the entry by
 * setting the tick count to the timeout). Key activity restarts the timeout.
 * `input` is game86's g_input_a (07FC: bits 0/1 up/down, 2/3 left/right, 7 fire).
 * game86 1000:5b06 */
NameEntrySound name_entry_tick(NameEntry *entry, uint8_t input, uint16_t *tick_count,
                               const HiscoreArt *art);

static inline bool name_entry_done(uint16_t tick_count)
{
    return (int16_t)tick_count >= NAME_ENTRY_TIMEOUT_TICKS;   /* 1000:5ab3 signed compare */
}

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_GAME_HISCORE_H */
