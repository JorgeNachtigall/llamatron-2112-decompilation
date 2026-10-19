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
    FORMAT_LLVL_VERSION = 1,
    FORMAT_LHUD_VERSION = 1,
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

typedef struct LevelSet {
    uint16_t level_count;
    LevelScript *levels;
    LevelOp *ops;            /* storage behind levels[].ops */
    LevelSpawn *spawns;      /* storage behind levels[].spawns */
} LevelSet;

bool levels_parse(const uint8_t *data, size_t size, LevelSet *out);
void levels_free(LevelSet *set);

/* ---- samples.idx (LSMP) + samples.bin -------------------------------------------------- */

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
