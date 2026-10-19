# Formats of build/generated/data

All multi-byte values are little-endian. Structured files start with a 4-byte magic and a
u16 version so that readers (`src/assets/`) can reject stale builds. These are our own formats
(PLAN §0.1: typed data, not memory images); the original layouts they come from are described in
`docs/re-notes-*.md`.

## Title pictures (fs.exe)
| file | layout |
|------|--------|
| `title_vga.idx8` | 320 x 400 bytes, colour index per pixel; rows 0-199 newtit.lbm, 200-399 newtit2.lbm |
| `title_vga.pal` | 256 x {r, g, b}, 6-bit DAC levels (newtit.lbm CMAP >> 2) |
| `title_ega.idx4` | 640 x 350 bytes, values 0..15 |
| `title_ega.pal` | 17 EGA palette register values (bits 5..0 = r g b R G B), [16] = overscan |

## `font.bin` (from f.cmp; game86 text drawer 1000:559f)
```
char  magic[4] = "LFNT"
u16   version = 1
u16   line_height          word 0 of f.cmp (14)
u16   char_count = 128     character codes 0..127
u16   glyph_count
u16   char_map[char_count] glyph index, 0xFFFF = no glyph (codes 0..31)
struct { u16 advance; u16 row_count; u16 first_row; } glyphs[glyph_count]
u16   rows[]               one row per u16, bit 15 = leftmost pixel, 16 px wide
```
Glyphs shared by several codes in the original (lower case -> upper case) are stored once.
The glyph's first row is the original's blank top row (the drawer draws rows-1+1 rows from +6).
**Deviation:** the last glyph (codes `]`, `^`, `_`, 0x7d-0x7f) claims 7 rows but f.cmp ends after
2; the original reads the rest from memory after the font (DGROUP 0x409c.., including the
high-score table). We store blank rows instead.

## `sine.bin` (s.cmp, verbatim; it is already two typed arrays)
```
s8  sine8[256]    sine, 256 steps per turn, amplitude 126, used by game86 (1000:29b3, 29c3, 44fc)
u8  rest[512]     no reader found in game86; read as big-endian s16 it is a sine of amplitude
                  0x3FFF in its positive half and 0x101 too high in its negative half
```

## Sprite frames: `sprites.bin` (game86 bank, segment 218C) and `g3.bin` (g3.cmp title shapes)
```
char  magic[4] = "LSPR"
u16   version = 1
u16   frame_count
struct {
    u16 bank_offset     offset of the frame in its original bank (218C:xxxx or g3 offset);
                        lets ported code map an original sprite pointer to a frame index
    u16 kind            original header word 0 (always 3)
    u16 width           stored width in pixels = 16 x columns
    u16 height          rows
    u16 nominal_width   original header word 3, used for centring
    s16 hotspot_x       nominal_width >> 1   (the draw position is the frame centre:
    s16 hotspot_y       (height - 1) >> 1     x -= hotspot_x, y -= hotspot_y; game86 1000:8cc6)
    u16 reserved = 0
    u32 pixel_offset    into the pixel area
} frames[frame_count]          20 bytes each
u8    pixels[]                 per frame width x height, row-major: colour 0..15, 0xFF = transparent
```
Pixels are opaque where the original mask bit is 1 (colour from the 4 plane bits, plane 0 = bit 0)
and transparent where it is 0 (game86 1000:8dc9, 63d2). `sprites.bin` lists frames in bank order;
`g3.bin` lists the 16 shapes in the order of g3.cmp's offset table (index = the game's `ES:[i*2] / 2`).

## `sprite_refs.bin` (the word table at 218C:0000, before the first frame)
```
char  magic[4] = "LSRF"
u16   version = 1
u16   count            table words (original offset = index * 2)
u16   refs[count]      < 0x8000: frame index into sprites.bin
                       0x8000 | i: link to refs[i] (the original pointed into the table itself)
                       0xFFFF: terminator, as in the original
```
The meaning of the individual lists (animations) is not decoded yet; the original code indexes
this table, and ported code uses the same indices.

## Samples: `samples.bin` + `samples.idx` (llatron.spl, directory in sblaster.bin 0x609)
`samples.bin`: 8-bit unsigned mono PCM (0x80 = silence) of samples 1..32 back to back. Ranges that
overlap in the original (23 contains 24-26, 31/32 lie inside 29) are copied, so every sample is
self-contained.
```
samples.idx
char  magic[4] = "LSMP"
u16   version = 1
u16   sample_count = 32       index i = the game's sample number i + 1 (AL = 1..32)
u16   rate_count = 5
u8    time_constants[5]       Sound Blaster DSP time constants for rate index (CL) 0..4,
                              from sblaster.bin 0x3d2; Hz = 1000000 / (256 - tc)
u8    pad = 0
struct { u32 offset; u32 length; } samples[sample_count]   into samples.bin
```
**Deviation:** sample 30's entry runs 96 bytes past the end of llatron.spl (the original DMA plays
whatever memory follows the bank); those bytes are silence here. Loop fields of the driver table
are dead in the driver and not exported.

## `palettes.dat` (DGROUP; palette code at game86 1000:1be6, 3e4e, 1e7b:0196, e4f5)
```
char  magic[4] = "LPAL"
u16   version = 1
u8    vga_attribute_registers[17]   DS:2122, INT 10h/1002h at video init on VGA (maps colour
                                    0..15 -> DAC 0..7, 16..23)
u8    nibble_to_dac[16]             DS:2134, ST colour nibble -> 6-bit DAC level (VGA only)
u8    pad = 0
u16   pulse_vga[16]                 DS:0658, ST words for colour 1, step = tick & 15
u8    pulse_ega[8]                  DS:0678, EGA register values for colour 1, one step per 4 ticks
u16   palette_count = 3
struct {                            = GfxGamePalette (src/engine/gfx.h) + its DS offset
    u16 ds_offset                   0x2144 game palette, 0x2164 EGA video init (EGA half only),
                                    0x2175 all black
    u16 st_words[16]                $0RGB, one nibble per channel
    u8  ega_registers[17]           at ds_offset + 0x20; [16] = overscan
    u8  pad = 0
} palettes[palette_count]           52 bytes each
u16   flash_list_count
struct { u16 ds_offset; u16 first_word; u16 word_count; } flash_lists[flash_list_count]
u16   flash_word_count
u16   flash_words[flash_word_count] ST words for colour 0, one per frame; the original's 0
                                    terminator is not stored (word_count instead). Only bits
                                    0..11 are colour (1e7b:01cd): the lists use 0x1000 for black
                                    because 0 would end the list
```
`ds_offset` is the value the original stores in `g_flash_list` (DS:06C7) or passes in DX.

## High scores: `default_scores.dat` (DS:0AD2) and `user_scores.dat` (the user's scores.yak)
```
char  magic[4] = "LSCO"
u16   version = 1
u16   count               10, or 0 in user_scores.dat when scores.yak is missing, not exactly
                          160 bytes (the game's own check) or has non-digit scores
struct { u32 score; u8 name[8]; } entries[count]   best first; name as stored (space padded)
```
scores.yak itself is 10 x {8 ASCII digits, 8 name bytes}; the port writes that format back.

## `config.dat` (the user's llama.inf, parsed like llama.exe 1000:0219)
```
char  magic[4] = "LCFG"
u16   version = 1
u8    present      0 = no valid llama.inf (the remaining fields are defaults)
u8    vga          1 = 'V', 0 = 'E'
u8    joystick     1 = 'J', 0 = 'K'
u8    sound        0x00 AdLib, 0x0C Sound Blaster, 0x18 Roland, 0xFF none
u8    cpu          inf[6] - '0' (0 = game86)
u8    flash_mode   'P' -> 1, 'Y' -> 2, else 0 (game86 [0x589])
```

## `strings.dat` (texts from DGROUP; text_print game86 1000:536f)
```
char  magic[4] = "LTXT"
u16   version = 2
u16   text_count
struct { u16 ds_offset; u16 first_op; u16 op_bytes; } texts[text_count]
                         ds_offset = where the original string starts (what the code passes in SI)
u16   level_title_count
struct { s16 level; u16 text_index; } level_titles[]     DS:2340 table, in order (level 99 first)
u16   message_count
struct { u16 ds_offset; u16 length; u8 text[length]; } messages[]   DOS '$' strings, '$' dropped
u16   op_area_size
u8    ops[op_area_size]
struct { u16 raw_offset; u16 raw_length; } raw_texts[text_count]   (version 2)
u16   raw_area_size
u8    raw[raw_area_size]
```
`raw` holds each text's original bytes as text_print reads them, escapes untouched, from
`ds_offset` up to and including the `\e`; `raw_length` counts those bytes, and one extra NUL
follows each text (not counted). The bytes can contain NULs (`\q` line ends, word-wrap padding),
so walk them to the `\e`, not to the first NUL.
Ops (one byte, then operands):
| op | operands | original | meaning |
|----|----------|----------|---------|
| 0x00 | — | `\e` | end of text |
| 0x01 | u16 n, n bytes | glyph bytes | glyphs; NUL bytes are kept (the drawer skips them, but they end the word-wrap look-ahead) |
| 0x02 | u16 n, n bytes | `\q`...NUL | one line centred on x = 160, no escapes inside |
| 0x03 | — | `\r` | newline: x = line start, y += 0x15 |
| 0x04 | u16 sprite | `\s` + BE u16 | draw sprite: refs[0x0E + sprite] of sprite_refs.bin (original table at 218C:001C) |
| 0x05..0x08 | — | `\u \d \l \i` | cursor up / down / left / right by 0x15 |
| 0x09 | u8 x, s8 dy | `\g` | x = line start = x, y += dy |
| 0x0A | u8 c | `\c` | glyph colour (low nibble) |
| 0x0B | u8 code | `\` + other | unknown escape; the original ignores it |

`ctest -R strings_roundtrip` rebuilds the original byte strings from these ops, compares them
with game86 and checks the raw bytes against them.

## `hud_glyphs.dat` (DS:09EE digits, DS:0A8E / 0A9E player-life labels; drawer game86 1000:519a)
```
char  magic[4] = "LHUD"
u16   version = 1
u8    digits[10][8]           digit 0..9, 8 rows each
u8    player_labels[2][8]     P1 (DS:0A8E), P2 (DS:0A9E)
```
A row is 8 pixels, bit 7 = leftmost, 1 = drawn. The original stores one word per row; the drawer
uses only the low (even) byte, so the high bytes are not exported.

## `hiscore.dat` (high-score screens, game86 1000:5a6e..5e6d)
```
char  magic[4] = "LHSC"
u16   version = 1
u8    alphabet[33]       DS:0994, the name-entry letter grid (11 x 3, row-major), including its
                         delete and end symbols; indexed by the cursor (1000:5b89)
u8    row_colours[10]    DS:09C9, glyph colour of each table row (low nibble, 1000:5d60)
u8    rude_word[4]       the immediates of the CMPs at 1000:5e52, 5e5c, 5e61, 5e67 that check the
                         entered name for this word (sets [0874] = 1)
```

## `levels.dat` (tbl_level_scripts DS:2BDA; interpreter game86 1000:020b)
The table has **100** entries (up to the 0 word at DS:2CA2), not 40. A 0 entry makes the game wrap
to level 0 and increment the loop count.
```
char  magic[4] = "LLVL"
u16   version = 1
u16   level_count = 100
struct {
    u16 ds_offset        where the original script starts
    u16 first_op, op_count
    u8  wave_type        the script's first non-opcode byte (g_wave_type)
    u8  spawn_count
    u16 first_spawn
} levels[level_count]                  10 bytes each
u16   op_count
struct { u8 handler; u8 operand_count; u8 operands[4]; } ops[]   6 bytes each, unused operands 0
u16   spawn_count
struct { u8 enemy_type; u8 count; } spawns[]   enemy_type indexes tbl_enemy_init (DS:2CA4, 41)
```
`handler` = -opcode - 1 (opcode byte 0xFF -> 0 ... 0xF8 -> 7), the CS:0245 table:
| handler | original | operands | effect |
|---------|----------|----------|--------|
| 0 | 1000:0421 | s8 x, s8 y | enemy speed x/y = value << loop count (21ac/21ae) |
| 1 | 1000:0438 | 4 x s8 | level parameters 21b6, 21b8, 21ba, 21bc |
| 2 | 1000:03f4 | 4 bytes | 21c0 = b0 * 0x101, 21be = b1 & 15, 21c2 = b2 * 0x101; b3 is skipped; requests sample 0x16 |
| 3 | 1000:03e1 | 2 bytes | 0826 = b0, 0818 = b1 |
| 4 | 1000:03cf | 2 bytes | 0816 = b0, 086e = b1 * 0x101 |
| 5 | 1000:03bc | 2 bytes | 0830 = b0, 0812 = b1 |
| 6 | 1000:039c | 3 bytes | 0828 = b0 << 8, 082a = 0, 082c = b1 * 0x101, 082e = b2 |
| 7 | 1000:0392 | 1 byte | 0820 = b0 |

The original ends every spawn list with `FF FF` (not stored). `ctest -R levels_roundtrip` rebuilds
the original scripts and compares them with game86.
