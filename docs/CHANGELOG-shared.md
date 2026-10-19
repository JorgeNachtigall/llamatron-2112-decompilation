# Shared header changelog

Shared headers (PLAN.md §7) are owned by the integration lead (W0). Every change gets one line here.

| Date | Header | Change | By |
|------|--------|--------|----|
| 2026-10-18 | src/engine/gfx.h | Initial API (pages, flip, rects, dirty lists, sprite/print signatures, game palette). Sprite/print semantics are stubs until W6 diff-tests them. | W2 Lantern, adopted by W0 |
| 2026-10-18 | src/platform/asset_blob.h | New W1/W2 contract: LlamaBlobEntry {name,data,size}, llama_blob_entries/llama_blob_count; Forge generates assets_blob.c against it. | W2 Lantern + W1 Forge, adopted by W0 |
