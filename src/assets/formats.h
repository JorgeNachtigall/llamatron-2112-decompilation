/*
 * formats.h - the extractor's data files (tools/extract/FORMATS.md, which stays authoritative)
 * decoded into C structs, and the parsers that build them from a file's bytes.
 *
 * Shared header (W1 Forge): changes are logged in docs/CHANGELOG-shared.md.
 *
 * Parsers never copy what can be used in place: pixel, PCM and raw text pointers point into the
 * buffer passed in, which must outlive the result (asset_get() memory lives forever). Multi-byte
 * arrays are converted into allocated memory; *_free() releases it. A parser returns false on a
 * wrong magic, a version other than the one below, or a truncated or inconsistent file, and
 * format_error() then says why.
 */
#ifndef LLAMATRON_FORMATS_H
#define LLAMATRON_FORMATS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Format versions these parsers read (asset_index.h's LLAMA_FMT_*_VERSION must match). */
enum {
    FORMAT_LFNT_VERSION = 1,
    FORMAT_LSPR_VERSION = 1,
    FORMAT_LSRF_VERSION = 1,
    FORMAT_LSMP_VERSION = 1,
    FORMAT_LPAL_VERSION = 1,
    FORMAT_LSCO_VERSION = 1,
    FORMAT_LCFG_VERSION = 1,
    FORMAT_LTXT_VERSION = 2,
    FORMAT_LLVL_VERSION = 4,
    FORMAT_LHUD_VERSION = 1,
    FORMAT_LHSC_VERSION = 1,
    FORMAT_LENM_VERSION = 2,
    FORMAT_LCTL_VERSION = 2,
    FORMAT_LTTL_VERSION = 1,
    FORMAT_LNSP_VERSION = 1,
};

const char *format_error(void);

/* ---- sprites.bin, g3.bin (LSPR) ------------------------------------------------------- */

typedef struct SpriteFrame {
    uint16_t bank_offset;    /* offset in the original bank (218C:xxxx or the g3.cmp offset) */
    uint16_t kind;           /* original header word 0 (3) */
    uint16_t width, height;  /* stored size; width = 16 x columns */
    uint16_t nominal_width;  /* original header word 3, used for centring */
    int16_t hotspot_x;       /* nominal_width >> 1 */
    int16_t hotspot_y;       /* (height - 1) >> 1: the draw position is the frame centre */
    const uint8_t *pixels;   /* width x height, colour 0..15 or GFX_TRANSPARENT */
} SpriteFrame;

typedef struct SpriteBank {
    uint16_t frame_count;
    SpriteFrame *frames;
    GfxSprite *sprites;      /* the same frames as gfx views: mask NULL, GFX_TRANSPARENT */
} SpriteBank;

bool sprite_bank_parse(const uint8_t *data, size_t size, SpriteBank *out);
void sprite_bank_free(SpriteBank *bank);
/* Frame index whose original offset is `bank_offset`, or -1. */
int sprite_bank_find(const SpriteBank *bank, uint16_t bank_offset);

/* ---- sprite_refs.bin (LSRF): the word table at 218C:0000 ------------------------------- */

enum {
    SPRITE_REF_LINK = 0x8000, /* 0x8000 | i: points at refs[i] (into the table itself) */
    SPRITE_REF_END = 0xFFFF,  /* list terminator */
    /* Sprite number n of the text engine's \s and of an entity's sprite field is refs[0x0E + n]
     * (the original indexes 218C:001C + 2n). */
    SPRITE_NUMBER_BASE = 0x0E,
};

typedef struct SpriteRefTable {
    uint16_t count;
    uint16_t *refs;          /* < 0x8000: frame index into sprites.bin; else LINK / END */
} SpriteRefTable;

bool sprite_refs_parse(const uint8_t *data, size_t size, SpriteRefTable *out);
void sprite_refs_free(SpriteRefTable *table);

/* ---- named_sprites.dat (LNSP): game-bank frames the code names directly --------------- */

/* In file order. The code draws these by a fixed 218C pointer (MOV SI,imm16), not by sprite
 * number; the pause pictures come from DS:09E2. */
typedef enum SpriteId {
    SPRITE_ID_BEAM_HEAD,        /* 1000:46b6 / 477f, anim 02/03 beams */
    SPRITE_ID_POWERUP_MARK,     /* 1000:469f, anim 10 (also sprite number 0xc5) */
    SPRITE_ID_SHIELD,           /* 1000:1a81 / 1b4b, while the shield timer DS:0907 runs */
    SPRITE_ID_RESCUE_MARKER,    /* 1000:1afe / 1bd2, at DS:0921 / 0923 */
    SPRITE_ID_HISCORE_MARK_1,   /* 1000:5cde, beside the new score at row DS:09DE */
    SPRITE_ID_HISCORE_MARK_2,   /* 1000:5d04, row DS:09E0 */
    SPRITE_ID_PAUSE_FIRST,      /* DS:09E2: 4 pause pictures, one picked at random (1000:15a2) */
    SPRITE_ID_COUNT = SPRITE_ID_PAUSE_FIRST + 4,
} SpriteId;

enum { PAUSE_PICTURE_COUNT = SPRITE_ID_COUNT - SPRITE_ID_PAUSE_FIRST };

typedef struct NamedSprites {
    uint16_t bank_offset[SPRITE_ID_COUNT];  /* the original pointer (218C:offset) */
    uint16_t frame[SPRITE_ID_COUNT];        /* index into sprites.bin */
} NamedSprites;

/* Fails unless the file names exactly SPRITE_ID_COUNT frames, all below frame_count. */
bool named_sprites_parse(const uint8_t *data, size_t size, uint16_t frame_count, NamedSprites *out);

/* ---- font.bin (LFNT) ------------------------------------------------------------------ */

enum { FONT_CHAR_COUNT = 128 };

typedef struct AssetFont {
    uint16_t line_height;               /* word 0 of f.cmp (14) */
    GfxGlyph glyphs[FONT_CHAR_COUNT];   /* by character code; missing: rows 0, bits NULL */
    GfxFont gfx;                        /* first_char 0, glyph_count 128, height = tallest glyph */
    uint16_t *rows;                     /* storage behind glyphs[].bits */
} AssetFont;

bool font_parse(const uint8_t *data, size_t size, AssetFont *out);
void font_free(AssetFont *font);

/* ---- sine.bin -------------------------------------------------------------------------- */

enum { SINE_STEPS = 256 };

/* sine.bin's second half has no reader in game86 and is not decoded. */
typedef struct SineTables {
    int8_t sine8[SINE_STEPS];    /* 256 steps per turn, amplitude 126 */
} SineTables;

bool sine_parse(const uint8_t *data, size_t size, SineTables *out);

/* ---- palettes.dat (LPAL) --------------------------------------------------------------- */

enum {
    VGA_ATTRIBUTE_REGISTERS = 17,
    PULSE_VGA_STEPS = 16,
    PULSE_EGA_STEPS = 8,
};

typedef struct NamedPalette {
    uint16_t ds_offset;         /* what the original passes to palette_set (1000:1be6) */
    GfxGamePalette palette;
} NamedPalette;

typedef struct FlashList {
    uint16_t ds_offset;         /* what the original stores in g_flash_list (DS:06C7) */
    uint16_t word_count;
    const uint16_t *words;      /* ST words for colour 0, one per frame (no 0 terminator); only
                                 * bits 0..11 are colour, 0x1000 = black */
} FlashList;

/* palettes.dat's flash lists in file order (the extractor writes them in this order; the DS
 * offset each stands for is in the comment). Meanings from W5a Thorn / W5b Moss. */
typedef enum FlashListId {
    FLASH_LIST_KILL,          /* DS:06C9 enemy kill (ent_hit_00) and player death */
    FLASH_LIST_DEADLY_TOUCH,  /* DS:06CD deadly pickup / deadly touch (1000:37c2) */
    FLASH_LIST_SMART_BOMB,    /* DS:06FD */
    FLASH_LIST_HIT,           /* DS:0707 non-lethal hit (ent_hit handlers) */
    FLASH_LIST_HIT_ALT,       /* DS:0713 non-lethal hit (ent_hit handlers) */
    FLASH_LIST_STORM,         /* DS:0721 lightning storm, each strike (think_2c 1000:2736) */
    FLASH_LIST_HIT_0C,        /* DS:0729 ent_hit_0c hit */
    FLASH_LIST_KILL_0C,       /* DS:072D ent_hit_0c kill */
    FLASH_LIST_COUNT,
} FlashListId;

/* Palettes the original passes to palette_set (1000:1be6), by name (asset_palette()). */
typedef enum PaletteId {
    PALETTE_ID_GAME,    /* DS:2144 the in-game palette, set everywhere in play */
    PALETTE_ID_EGA,     /* DS:2164 EGA register block, video init on EGA (1000:e510) */
    PALETTE_ID_BLACK,   /* DS:2175 all black, while a new background is revealed (1000:61ff) */
    PALETTE_ID_COUNT,
} PaletteId;

typedef struct PaletteSet {
    uint8_t vga_attribute_registers[VGA_ATTRIBUTE_REGISTERS]; /* DS:2122 */
    uint8_t nibble_to_dac[16];          /* DS:2134, for gfx_set_adapter() */
    uint16_t pulse_vga[PULSE_VGA_STEPS]; /* DS:0658, colour 1, step = tick & 15 */
    uint8_t pulse_ega[PULSE_EGA_STEPS];  /* DS:0678, colour 1, one step per 4 ticks */
    uint16_t palette_count;
    NamedPalette *palettes;
    uint16_t flash_list_count;
    FlashList *flash_lists;
    uint16_t *flash_words;              /* storage behind flash_lists[].words */
} PaletteSet;

bool palettes_parse(const uint8_t *data, size_t size, PaletteSet *out);
void palettes_free(PaletteSet *set);
const NamedPalette *palettes_find(const PaletteSet *set, uint16_t ds_offset);
const FlashList *palettes_find_flash_list(const PaletteSet *set, uint16_t ds_offset);

/* ---- strings.dat (LTXT) ---------------------------------------------------------------- */

typedef enum TextOpCode {
    TEXT_OP_END = 0x00,      /* \e */
    TEXT_OP_GLYPHS = 0x01,   /* bytes/length: glyphs; NULs kept (skipped when drawn, end word wrap) */
    TEXT_OP_CENTRED = 0x02,  /* bytes/length: \q line centred on x 160, no escapes */
    TEXT_OP_NEWLINE = 0x03,  /* \r */
    TEXT_OP_SPRITE = 0x04,   /* sprite: \s sprite number (SPRITE_NUMBER_BASE) */
    TEXT_OP_UP = 0x05,       /* \u \d \l \i: cursor by 0x15 */
    TEXT_OP_DOWN = 0x06,
    TEXT_OP_LEFT = 0x07,
    TEXT_OP_RIGHT = 0x08,
    TEXT_OP_GOTO = 0x09,     /* x, dy: \g, x = line start = x, y += dy */
    TEXT_OP_COLOUR = 0x0A,   /* value: \c colour (low nibble used) */
    TEXT_OP_IGNORED = 0x0B,  /* value: unknown escape code, ignored by the original */
} TextOpCode;

typedef struct TextOp {
    TextOpCode code;
    const uint8_t *bytes;    /* GLYPHS, CENTRED */
    uint16_t length;
    uint16_t sprite;         /* SPRITE */
    uint8_t x;               /* GOTO */
    int8_t dy;               /* GOTO */
    uint8_t value;           /* COLOUR, IGNORED */
} TextOp;

typedef struct GameText {
    uint16_t ds_offset;      /* where the original string starts (SI at the call site) */
    const uint8_t *ops;      /* op_size bytes, walk with text_op_next() */
    uint16_t op_size;
    /* The original bytes as text_print (1000:536f) reads them, escapes untouched, up to and
     * including the "\e"; a NUL follows. They contain NULs (\q line ends, word-wrap padding), so
     * walk them to the \e, not to the first NUL. */
    const char *raw;
    uint16_t raw_length;
} GameText;

typedef struct LevelTitle {
    int16_t level;
    uint16_t text_index;     /* into TextSet.texts */
} LevelTitle;

typedef struct DosMessage {
    uint16_t ds_offset;
    uint16_t length;
    char *text;              /* NUL-terminated copy, '$' dropped */
} DosMessage;

typedef struct TextSet {
    uint16_t text_count;
    GameText *texts;
    uint16_t level_title_count;
    LevelTitle *level_titles; /* DS:2340 order */
    uint16_t message_count;
    DosMessage *messages;
} TextSet;

/* Texts by name (asset_text()); the DS offset is SI at the call site. */
typedef enum TextId {
    TEXT_ID_WARP_BANNER,       /* DS:05A5 "warp five levels", g_wave_result == 4 (1000:0646) */
    TEXT_ID_HERD_WAVE_BANNER,  /* DS:05DB at wave start when g_wave_type != 0 (1000:05a5) */
    TEXT_ID_DEMO_BANNER,       /* DS:05F4 at wave start in demo mode (1000:05b2) */
    TEXT_ID_HISCORE_ENTRY,     /* DS:094B name entry heading (1000:5a50) */
    TEXT_ID_HISCORE_NAME,      /* DS:09B5 name entry line (1000:5af3) */
    TEXT_ID_HISCORE_TITLE,     /* DS:0AAE high-score table heading (1000:5c9f) */
    TEXT_ID_VERSION_SCROLL,    /* DS:0B7A scroller at start-up, video_init (1000:5987) */
    TEXT_ID_CREDITS_SCROLL,    /* DS:117A attract scroller, case 2 (1000:02f9) */
    TEXT_ID_STORY_SCROLL,      /* DS:14E1 attract scroller, case 0 (1000:02d7) */
    TEXT_ID_STORY_LATE,        /* DS:1BC8 the story scroller's late text, mode 2 (1000:58f1) */
    TEXT_ID_COUNT,
} TextId;

/* DOS messages ('$'-terminated, INT 21h/09h) by name (asset_dos_message()). */
typedef enum MessageId {
    MESSAGE_ID_FILE_ERROR,      /* DS:0463 */
    MESSAGE_ID_REINSTALL,       /* DS:0471 "try running Install again" */
    MESSAGE_ID_NO_MEMORY,       /* DS:04B3 */
    MESSAGE_ID_NO_JOYSTICK,     /* DS:04E1 falls back to the keyboard */
    MESSAGE_ID_NO_CONFIG,       /* DS:0520 no or invalid llama.inf */
    MESSAGE_ID_NO_SOUND_CARD,   /* DS:0563 */
    MESSAGE_ID_COUNT,
} MessageId;

bool texts_parse(const uint8_t *data, size_t size, TextSet *out);
void texts_free(TextSet *set);
const GameText *texts_find(const TextSet *set, uint16_t ds_offset);
/* Decodes the op at *cursor (< end) and advances; false at the end of the buffer or on a
 * truncated op. TEXT_OP_END is returned like any other op. */
bool text_op_next(const uint8_t **cursor, const uint8_t *end, TextOp *op);

/* ---- levels.dat (LLVL) ----------------------------------------------------------------- */

enum {
    LEVEL_OP_MAX_OPERANDS = 4,
    LEVEL_OP_HANDLERS = 8,   /* handler = -opcode - 1, the CS:0245 table (FORMATS.md) */
};

typedef struct LevelOp {
    uint8_t handler;
    uint8_t operand_count;
    uint8_t operands[LEVEL_OP_MAX_OPERANDS]; /* unused ones 0 */
} LevelOp;

typedef struct LevelSpawn {
    uint8_t enemy_type;      /* index into tbl_enemy_init (DS:2CA4) */
    uint8_t count;
} LevelSpawn;

typedef struct LevelScript {
    uint16_t ds_offset;      /* where the original script starts */
    uint8_t wave_type;       /* g_wave_type */
    uint16_t op_count;
    const LevelOp *ops;
    uint16_t spawn_count;
    const LevelSpawn *spawns;
} LevelScript;

/* Spawn pairs the original overwrites with zeros (type 0, count 0) when enemy_shot_init
 * (1000:123c/1241, via 1000:10b0 from think_13) writes [SI+8] and [SI+0A] with SI = think_13's
 * code offset taken as a DS address. Computed by the extractor; level = LEVEL_NONE: none. */
enum { LEVEL_NONE = 0xFFFF };
typedef struct LevelSpawnClobber {
    uint16_t level;
    uint16_t first_spawn;    /* index into that level's spawns */
    uint16_t spawn_count;
} LevelSpawnClobber;

enum { LEVEL_VARIANTS = 4, WARP_BOX_COLOUR_COUNT = 4 };

typedef struct LevelSet {
    LevelSpawnClobber clobber_10b0;
    /* Sprite number of each spawned enemy (entity +0C via spawn_sprite DS:0806), by level & 3,
     * or by rng_next & 3 when DS:21B0 is non-zero (wave_spawn_next 1000:1d97..1dae). DS:0808. */
    uint16_t spawn_sprites[LEVEL_VARIANTS];
    /* Warp-in sprite number of each spawn effect (1000:1d60, entity +0C), picked by level & 3 at
     * wave start into g_level_variant DS:0941 (level_select_variant 1000:5350). DS:0939. */
    uint16_t warp_sprites[LEVEL_VARIANTS];
    /* Colours the warp rectangle cycles through, SI = 0..3 (1000:06e9, [0619 + SI] & 0x0F);
     * stored as in the original, the & 0x0F is the reader's. DS:0619. */
    uint8_t warp_box_colours[WARP_BOX_COLOUR_COUNT];
    uint16_t level_count;
    LevelScript *levels;
    LevelOp *ops;            /* storage behind levels[].ops */
    LevelSpawn *spawns;      /* storage behind levels[].spawns */
} LevelSet;

bool levels_parse(const uint8_t *data, size_t size, LevelSet *out);
void levels_free(LevelSet *set);

/* ---- samples.idx (LSMP) + samples.bin -------------------------------------------------- */

/* The game's sample numbers (driver AH=3, AL; samples[n - 1]), named after their call sites
 * (game86 addresses; docs/re-notes-sound-data.md §3), not by listening. "death of XX" = enemy
 * type XX's death sound (played by the kill at 1000:0eb0). The pause screen plays the last scan
 * code & 0x1F (1000:155d, above 0x1C -> 0x1B): the only way the KEY_ samples are heard. */
typedef enum SampleId {
    SAMPLE_ID_KEY_01 = 0x01,           /* pause-screen keys only */
    SAMPLE_ID_LETTER_LANDS = 0x02,     /* title letter lands 3f97; death of 04 */
    SAMPLE_ID_KEY_03 = 0x03,           /* pause-screen keys only */
    SAMPLE_ID_HIT_0B = 0x04,           /* hit handler 0b 2afc */
    SAMPLE_ID_DEATH_0B = 0x05,         /* death of 0b */
    SAMPLE_ID_WARP = 0x06,             /* warp banner 0654 (waited for); death of 27, kill-action debris */
    SAMPLE_ID_POP = 0x07,              /* hits 0a/0d 2dea/3205, herder spawn 23fd, name-entry cursor 5b41;
                                          deaths of 0c, 10, segments, edge runners, dropped shots */
    SAMPLE_ID_BEAM = 0x08,             /* homing beam 3884, drop shooter 4f36; death of 1f */
    SAMPLE_ID_ENEMY_DEATH = 0x09,      /* deaths of 00, 01, 0d, 0e, 0f, 17 and debris */
    SAMPLE_ID_KEY_0A = 0x0A,           /* pause-screen keys only */
    SAMPLE_ID_DROP = 0x0B,             /* homing drop 259f; death of 17 (rate 4) */
    SAMPLE_ID_RELEASE = 0x0C,          /* chaser release 20f2, name-entry delete 5bdf; kill action 03 */
    SAMPLE_ID_KNOCK = 0x0D,            /* hits 07/01/06 102a/10ab, warp-in death; death of 04 */
    SAMPLE_ID_FIRE = 0x0E,             /* player shot 0ad9/0bff */
    SAMPLE_ID_SHOT_DEATH = 0x0F,       /* death of enemy shots (1232) and drifters */
    SAMPLE_ID_SMART_BOMB = 0x10,       /* smart-bomb pickup 0fc9 */
    SAMPLE_ID_EXPLOSION = 0x11,        /* player death 161c, thunder 2750, dropper lands 30be, title logo
                                          done 3fd5, enemy death 4450 */
    SAMPLE_ID_DEATH_24 = 0x12,         /* death of 24 */
    SAMPLE_ID_KEY_13 = 0x13,           /* pause-screen keys only */
    SAMPLE_ID_WAVE_CLEARED = 0x14,     /* wave cleared 16bc, title letters done 3fb9 */
    SAMPLE_ID_WAVE_START = 0x15,       /* game start 011a, next wave 01ef */
    SAMPLE_ID_LEVEL_SOUND_OP = 0x16,   /* level-script op 2 041a */
    SAMPLE_ID_RESCUE = 0x17,           /* llama rescued 4dee, snake head hit 116b; title tune */
    SAMPLE_ID_CAUGHT = 0x18,           /* llama caught 4a97, name-entry letter 5bb0 */
    SAMPLE_ID_EXTRA_LIFE = 0x19,       /* extra life 0373, extra-life pickup 100a, shield pickup 0fb0 */
    SAMPLE_ID_ALERT = 0x1A,            /* shield ending 1591, bonus pickup appears 0f0b, last llama 4ceb,
                                          new herder, name entry done 5bf2; death of 1a; title tune */
    SAMPLE_ID_TITLE = 0x1B,            /* fs.exe title picture, title tune, pause keys above 0x1C */
    SAMPLE_ID_PICKUP = 0x1C,           /* pickup 00 101d */
    SAMPLE_ID_DEADLY_TOUCH = 0x1D,     /* deadly touch 37ee, last enemies' death 4443 */
    SAMPLE_ID_GAME_OVER = 0x1E,        /* game over 091b (waited for), hits 0c/11 364f/366a/2718, herder
                                          proximity 248c; deaths of 19 and 28 */
    SAMPLE_ID_UNUSED_1F = 0x1F,        /* never requested (its PCM lies inside sample 0x1D's) */
    SAMPLE_ID_HIT_0F = 0x20,           /* hit handler 0f 12e8; title tune */
} SampleId;

typedef struct Sample {
    const uint8_t *pcm;      /* 8-bit unsigned mono, 0x80 = silence */
    uint32_t length;
} Sample;

typedef struct SampleSet {
    uint16_t sample_count;   /* samples[i] = the game's sample number i + 1 */
    Sample *samples;
    uint16_t rate_count;
    uint8_t time_constants[8]; /* DSP time constants by rate index; Hz = 1e6 / (256 - tc) */
} SampleSet;

bool samples_parse(const uint8_t *index, size_t index_size, const uint8_t *pcm, size_t pcm_size,
                   SampleSet *out);
void samples_free(SampleSet *set);
/* Playback rate in Hz of rate index `rate` (0 when out of range). */
unsigned samples_rate_hz(const SampleSet *set, unsigned rate);

/* ---- default_scores.dat, user_scores.dat (LSCO) ---------------------------------------- */

enum {
    HIGH_SCORE_ENTRIES = 10,
    HIGH_SCORE_NAME_SIZE = 8,
};

typedef struct HighScore {
    uint32_t score;
    char name[HIGH_SCORE_NAME_SIZE + 1]; /* as stored (space padded), NUL-terminated */
} HighScore;

typedef struct HighScoreTable {
    uint16_t count;          /* 10, or 0 when the user has no usable scores.yak */
    HighScore entries[HIGH_SCORE_ENTRIES]; /* best first */
} HighScoreTable;

bool scores_parse(const uint8_t *data, size_t size, HighScoreTable *out);

/* scores.yak as the game writes it (game86 1000:5da2): 10 x {8 ASCII digits, 8 name bytes}.
 * Scores are written zero-padded ("%08u"), so every table the game accepts round-trips exactly. */
enum { SCORES_YAK_SIZE = HIGH_SCORE_ENTRIES * (8 + HIGH_SCORE_NAME_SIZE) };
/* false if count != 10 or a score exceeds 99999999. File I/O is the platform's job. */
bool scores_format_yak(const HighScoreTable *table, uint8_t out[SCORES_YAK_SIZE]);
/* The reverse, for the scores.yak the host saved: the game takes the file only if it is exactly
 * SCORES_YAK_SIZE bytes (game86 1000:60aa); like the extractor, a score field that is not all
 * digits rejects it too. false = count 0. */
bool scores_parse_yak(const uint8_t *data, size_t size, HighScoreTable *out);

/* ---- hud_glyphs.dat (LHUD): DS:09EE digits, DS:0A8E / 0A9E player-life labels ----------- */

enum {
    HUD_DIGIT_COUNT = 10,
    HUD_PLAYER_LABEL_COUNT = 2,
    HUD_GLYPH_ROWS = 8,
};

/* One byte per row, 8 pixels, bit 7 = leftmost, 1 = drawn (game86 1000:519a). */
typedef struct HudGlyphs {
    uint8_t digits[HUD_DIGIT_COUNT][HUD_GLYPH_ROWS];
    uint8_t player_label[HUD_PLAYER_LABEL_COUNT][HUD_GLYPH_ROWS]; /* [0] P1, [1] P2 */
} HudGlyphs;

bool hud_glyphs_parse(const uint8_t *data, size_t size, HudGlyphs *out);

/* ---- hiscore.dat (LHSC): high-score screen tables -------------------------------------- */

enum {
    HISCORE_ALPHABET_SIZE = 33,  /* 11 x 3 */
    HISCORE_ROW_COUNT = 10,
    HISCORE_RUDE_WORD_SIZE = 4,
};

typedef struct HiscoreData {
    char alphabet[HISCORE_ALPHABET_SIZE + 1];  /* DS:0994 name-entry grid incl. delete and end
                                                * symbols; NUL-terminated */
    uint8_t row_colours[HISCORE_ROW_COUNT];    /* DS:09C9, colour of each table row (1000:5d60) */
    char rude_word[HISCORE_RUDE_WORD_SIZE];    /* immediates of 1000:5e52..5e67; not terminated */
} HiscoreData;

bool hiscore_data_parse(const uint8_t *data, size_t size, HiscoreData *out);

/* ---- enemies.dat (LENM): read-only tables of the enemy hit / kill handlers ------------- */

enum {
    SCORE_AWARDS_MAX = 64,
    DROP_DIRECTIONS = 4,
    PICKUP_KINDS = 10,
    DEBRIS_TEMPLATE_COUNT = 16,
    ENEMY_DIRECTIONS = 8,
    SEEK_QUADRANTS = 4,
    /* First template of each group the kill actions use (DS:0878 / 0898 / 08B8, 1000:3bcb..) */
    DEBRIS_GROUP_0878 = 0,
    DEBRIS_GROUP_0898 = 4,
    DEBRIS_GROUP_08B8 = 8,
};

/* score_add(AX = count_minus_1, BX = digit) (1000:0335): adds 1 to score digit `digit`
 * (7 = units) count_minus_1 + 1 times; digit 0 = no score (ent_hit_00 1000:0e40). */
typedef struct ScoreAward {
    uint16_t count_minus_1;
    uint16_t digit;
} ScoreAward;

typedef struct DebrisTemplate {
    uint16_t sprite;         /* sprite number */
    int16_t vx, vy;          /* shifted left by the caller (1000:3c50) */
} DebrisTemplate;

typedef struct EnemyTables {
    uint16_t score_award_count;          /* incl. the original's FFFF FFFF last record */
    ScoreAward score_awards[SCORE_AWARDS_MAX];   /* DS:2054, by think index (1000:0e38) */
    int8_t drop_by_direction[DROP_DIRECTIONS];   /* DS:0654, by (shot dir - 0x1d) & 3 */
    uint16_t pickup_sprites[PICKUP_KINDS];       /* DS:063C, sprite number per pickup kind */
    DebrisTemplate debris[DEBRIS_TEMPLATE_COUNT]; /* DS:0878 */
    /* think_24 (1000:34cc..3514): direction 0..7 by quadrant (bit 0 = x < target x,
     * bit 1 = y < target y), then speed by direction */
    int8_t seek_dir_by_quadrant[SEEK_QUADRANTS];  /* DS:084A */
    int16_t seek_vx[ENEMY_DIRECTIONS];            /* DS:084E */
    int16_t seek_vy[ENEMY_DIRECTIONS];            /* DS:085E */
    /* think_05 (1000:4e13..4e21), by a random direction 0..7 */
    int16_t wander_vx[ENEMY_DIRECTIONS];          /* DS:1FA2 */
    int16_t wander_vy[ENEMY_DIRECTIONS];          /* DS:1FB2 */
    int16_t wander_sprite[ENEMY_DIRECTIONS];      /* DS:1FBA sprite offset (shares 4 words with
                                                   * wander_vy in the original) */
} EnemyTables;

bool enemy_tables_parse(const uint8_t *data, size_t size, EnemyTables *out);

/* ---- controls.dat (LCTL): player and droid control tables ----------------------------- */

enum {
    INPUT_NIBBLES = 16,      /* bit 0 up, 1 down, 2 left, 3 right */
    DROID_QUADRANTS = 4,     /* bit 0 = P2 x < enemy x, bit 1 = P2 y < enemy y */
};

typedef struct ControlTables {
    /* by input nibble (player_fire 1000:0981.., players_tick 1000:16c1..) */
    int8_t shot_vx[INPUT_NIBBLES];          /* DS:0743 */
    int8_t shot_vy[INPUT_NIBBLES];          /* DS:0753 */
    uint8_t shot_direction[INPUT_NIBBLES];  /* DS:0763, 0 = no direction: keep the old aim */
    uint8_t spread_a[INPUT_NIBBLES];        /* DS:0773, nibble of a volley's 2nd shot, 0 = none */
    uint8_t spread_b[INPUT_NIBBLES];        /* DS:0783, 3rd shot */
    /* A bouncing shot leaving the screen: shots_update (1000:0c84..0ccc) finds the nibble whose
     * shot_direction/shot_vx/shot_vy match the shot and takes its new nibble from here. */
    uint8_t shot_bounce_x[INPUT_NIBBLES];   /* DS:0793, left/right edge (DX = 0x50) */
    uint8_t shot_bounce_y[INPUT_NIBBLES];   /* DS:07A3, top/bottom edge (DX = 0x60) */
    int8_t move_vx[INPUT_NIBBLES];          /* DS:07B3 */
    int8_t move_vy[INPUT_NIBBLES];          /* DS:07C3 */
    uint8_t ship_facing[INPUT_NIBBLES];     /* DS:07D3, 0 = keep */
    uint8_t trail_flag[INPUT_NIBBLES];      /* DS:07E3 */
    /* droid aim by quadrant (1000:17f6..1810) */
    int8_t droid_shot_vx[DROID_QUADRANTS];         /* DS:068C */
    int8_t droid_shot_vy[DROID_QUADRANTS];         /* DS:0690 */
    uint8_t droid_shot_direction[DROID_QUADRANTS]; /* DS:0694 */
    uint8_t droid_aim[DROID_QUADRANTS];            /* DS:0698, input nibble */
} ControlTables;

bool control_tables_parse(const uint8_t *data, size_t size, ControlTables *out);

/* ---- title.dat (LTTL): attract mode / title screen ------------------------------------- */

enum {
    TITLE_LETTER_SPRITE_COUNT = 10,
    TITLE_LLAMA_SPRITE_COUNT = 4,
    TITLE_TUNE_MAX = 256,
};

typedef struct TitleTables {
    uint8_t letter_sprites[TITLE_LETTER_SPRITE_COUNT];  /* DS:2118, sprite numbers (SPRITE_NUMBER_BASE:
                                                         * 1000:403f reads 218C:1C + 2n) */
    uint16_t llama_sprites[TITLE_LLAMA_SPRITE_COUNT];   /* DS:0909, sprite bases (1000:4560) */
    /* DS:09D5 verbatim, so byte positions match the original's tune pointer: {rate index,
     * sample} pairs, then a 0 rate byte = restart at byte 0 (title_tune_tick 1000:0870) */
    uint16_t tune_length;                   /* including the final 0 */
    uint8_t tune[TITLE_TUNE_MAX];
} TitleTables;

bool title_tables_parse(const uint8_t *data, size_t size, TitleTables *out);

/* ---- config.dat (LCFG): llama.inf as llama.exe parses it ------------------------------- */

typedef enum SoundDevice {
    SOUND_DEVICE_ADLIB = 0x00,
    SOUND_DEVICE_SBLASTER = 0x0C,
    SOUND_DEVICE_ROLAND = 0x18,
    SOUND_DEVICE_NONE = 0xFF,
} SoundDevice;

typedef struct GameConfig {
    bool present;            /* false: no valid llama.inf, the rest are defaults */
    bool vga;                /* 'V' (else 'E') */
    bool joystick;           /* 'J' (else 'K') */
    SoundDevice sound;
    uint8_t cpu;             /* inf[6] - '0' (0 = game86) */
    uint8_t flash_mode;      /* 'P' -> 1, 'Y' -> 2, else 0 (game86 [0x589]) */
} GameConfig;

bool config_parse(const uint8_t *data, size_t size, GameConfig *out);

#ifdef __cplusplus
}
#endif

#endif /* LLAMATRON_FORMATS_H */
