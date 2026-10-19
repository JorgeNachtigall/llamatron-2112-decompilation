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
