# llamatron-extract

Pure C11 build-time extractor (no third-party dependencies, always built for the host).
CMake runs it via `cmake/Extract.cmake`; it can also be used by hand:

```
llamatron-extract --verify --original DIR --manifest tools/manifest/original-files.json
llamatron-extract --all    --original DIR --manifest ... --out build/generated
llamatron-extract --unpack | --unlzexe | --unexepack | --load-image IN OUT
llamatron-extract --sha256 FILE...
```

`--all` verifies first (fatal on mismatch unless `--allow-unverified`), then writes into OUT:

| path | content |
|------|---------|
| `exe/<n>.exe` | fully unpacked MZ (LZEXE 0.91, then EXEPACK removed); byte-identical to `tools/unlzexe.py` + `tools/unexepack.py` |
| `exe/<n>.unpacked` | flat load module (no header), relocations applied for load segment 0x1000: Ghidra `seg:off` == file offset `(seg-0x1000)*16+off`. BSS beyond it is not included (allocate `min_alloc_paras`) |
| `exe/<n>.layout.json` | load segment, image/header size, min/max alloc, reloc count, raw and loaded entry CS:IP / SS:SP |
| `data/title_vga.idx8` | 320x400, 1 byte/pixel: rows 0-199 newtit.lbm, 200-399 newtit2.lbm (fs.exe VGA screen) |
| `data/title_vga.pal` | 256 x 3 bytes, 6-bit DAC levels = newtit.lbm CMAP >> 2 (newtit2's CMAP ignored, like fs.exe) |
| `data/title_ega.idx4` | 640x350, 1 byte/pixel, values 0..15 (tronega.lbm) |
| `data/title_ega.pal` | 17 EGA palette register values (rgbRGB), entry 16 = overscan = 0, built like fs.exe |
| `extract.stamp` | SHA-256 of every output, fixed order |

`<n>` = `game86`, `fs`, `llama`, `install`. Original file names are matched as given or in upper case (DOS copies).
