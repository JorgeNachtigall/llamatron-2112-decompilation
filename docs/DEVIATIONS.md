# Deliberate deviations from the original

Every place where the port knowingly differs from the 1992 original. Each entry says why and who owns it.

| # | Area | Original behaviour | Port behaviour | Why | Owner |
|---|------|--------------------|----------------|-----|-------|
| 1 | llatron.spl sample 30 | Sample runs 96 bytes past the end of the file (the driver plays whatever follows in memory) | Padded with silence | The bytes past EOF are undefined | W1 |
| 2 | f.cmp last glyph (chars 7D-7F) | Glyph is truncated; its missing rows come from DGROUP 409C+ (the hi-score table) | Missing rows padded blank | The original shows hi-score bytes as pixels; that is a bug, not data | W1 |
| 3 | Text on a page target (W6, text.c) | Glyph cells past the page end are written into the gap after the page and, beyond ~4.8 rows, into the next page | Cells past the page end are dropped | No caller is known to print there; callers' y ranges get checked while porting HUD/hiscore | W6 |
