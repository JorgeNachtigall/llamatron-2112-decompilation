# Deliberate deviations from the original

Every place where the port knowingly differs from the 1992 original. Each entry says why and who owns it.

| # | Area | Original behaviour | Port behaviour | Why | Owner |
|---|------|--------------------|----------------|-----|-------|
| 1 | llatron.spl sample 30 | Sample runs 96 bytes past the end of the file (the driver plays whatever follows in memory) | Padded with silence | The bytes past EOF are undefined | W1 |
| 2 | f.cmp last glyph (chars 7D-7F) | Glyph is truncated; its missing rows come from DGROUP 409C+ (the hi-score table) | Missing rows padded blank | The original shows hi-score bytes as pixels; that is a bug, not data | W1 |
| 3 | Text on a page target (W6, text.c) | Glyph cells past the page end are written into the gap after the page and, beyond ~4.8 rows, into the next page | Cells past the page end are dropped | No caller is known to print there; callers' y ranges get checked while porting HUD/hiscore | W6 |
| 4 | Keyboard taps (W2, engine/input.c) | INT 09 runs asynchronously, so any real key press overlaps at least one frame read / timer tick | A key released before its consumer sampled it (directions: frame read 1e7b:0000, fire: tick 1000:3e12) stays held until that sample | Host events arrive in batches; press + release in one batch would otherwise be lost. Presses that span a sample behave exactly as the original | W2 |
| 5 | Sound DMA page split (W2, engine/snd.c) | A sample crossing a 64 KB DMA page is played in two transfers; the second is started from the IRQ handler (sblaster.bin 0x511), leaving a gap of microseconds | Played as one continuous block; end time = length / rate | The gap is far below one tick and inaudible; status timing is unaffected at tick resolution | W2 |
| 6 | Title sound on the web (W2, apps/title_main.c) | fs.exe requests sample 1Bh right after showing the picture | On the web the request waits until the browser's AudioContext runs (first click); natively and in test runs it is immediate | Browsers keep audio suspended until a user gesture and SDL silently drains the queue meanwhile, so the sound would otherwise never be heard | W2 |
