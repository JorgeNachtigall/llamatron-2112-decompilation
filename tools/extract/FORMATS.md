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
s8  sine8[256]    amplitude 126, used by game86 (1000:29b3, 29c3, 44fc)
s16 sine16[256]   amplitude 0x3FFF, no reader found in game86
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
