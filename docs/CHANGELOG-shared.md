# Shared header changelog

Shared headers (PLAN.md §7) are owned by the integration lead (W0). Every change gets one line here.

| Date | Header | Change | By |
|------|--------|--------|----|
| 2026-10-18 | src/engine/gfx.h | Initial API (pages, flip, rects, dirty lists, sprite/print signatures, game palette). Sprite/print semantics are stubs until W6 diff-tests them. | W2 Lantern, adopted by W0 |
| 2026-10-18 | src/platform/asset_blob.h | New W1/W2 contract: LlamaBlobEntry {name,data,size}, llama_blob_entries/llama_blob_count; Forge generates assets_blob.c against it. | W2 Lantern + W1 Forge, adopted by W0 |
| 2026-10-18 | src/engine/gfx.h | (B) gfx_restore_rect copies one linear span of w pixels per row in page memory (runs on into the next row, clamped at page end), like 1e7b:0247 / 1e7b:031c; no x clipping. | W2 Lantern, per W6 Prism review, approved by W0 |
| 2026-10-18 | src/engine/gfx.h | (C) GFX_MAX_DIRTY 256 -> 800 (original lists hold 799 entries, CS:673f..79f9). | W2 Lantern, per W6 Prism review, approved by W0 |
| 2026-10-18 | src/engine/gfx.h | (D) gfx_fill_rect no longer cites 1000:6112 (that is a dotted outline, ported by W6 as draw_rect_outline). | W2 Lantern, per W6 Prism review, approved by W0 |
| 2026-10-18 | src/engine/gfx.h | (E) GFX_TRANSPARENT = 0xFF: with mask == NULL, sprite pixels equal to it are not drawn (sprites.bin convention). | W2 Lantern, per W6 Prism review, approved by W0 |
| 2026-10-18 | src/engine/gfx.h | (A) gfx_draw_sprite / gfx_draw_sprite_scaled / gfx_print documented as generic helpers without game86 citations; the game's exact routines live in src/game/draw_*.c and text.c (W6). | W2 Lantern, per W6 Prism review, approved by W0 |
