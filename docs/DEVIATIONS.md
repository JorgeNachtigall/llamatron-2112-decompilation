# Deliberate deviations from the original

Every place where the port knowingly differs from the 1992 original. Each entry says why and who owns it.

| # | Area | Original behaviour | Port behaviour | Why | Owner |
|---|------|--------------------|----------------|-----|-------|
| 1 | llatron.spl sample 30 | Sample runs 96 bytes past the end of the file (the driver plays whatever follows in memory) | Padded with silence | The bytes past EOF are undefined | W1 |
| 2 | f.cmp last glyph (chars 7D-7F) | Glyph is truncated; its missing rows come from DGROUP 409C+ (the hi-score table) | Missing rows padded blank | The original shows hi-score bytes as pixels; that is a bug, not data | W1 |
