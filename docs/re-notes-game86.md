# RE notes: game86.exe (and llama.exe launcher)

Evidence base: `re/listings/game86.exe.lst` (regenerated 3x by headless Ghidra after
disassembling indirect-call targets; see "Coverage" below) and byte dumps of
`unpacked/game86.exe`. Ghidra loads the image at segment 1000; DGROUP (DS) = 3559.
File offset of Ghidra address `seg:off` = `0x200 + (seg-0x1000)*16 + off` (header = 32 paragraphs).

Legend: **[C]** confirmed from code, **[H]** hypothesis.

## 1. Launch chain and inter-process contract

### llama.exe (launcher) — `re/listings/llama.exe.lst`
1. `1000:05db` resize own block (INT 21h/4Ah), then `INT 21h/48h` allocate **0x26C0 paragraphs
   (158,720 bytes)** -> segment saved at `CS:[0x71]`. [C]
2. `1000:01e7` open `llama.inf`, read 9 bytes to `DS:0x185`. Parsing (`1000:0219..02c8`): [C]
   - `inf[0]` 'V' -> VGA flag=1, 'E' -> 0 (stored `CS:[0x87]`); anything else -> "No or Invalid Game Configuration".
   - `inf[2]` 'J' -> joystick flag=1 (`CS:[0x89]`), 'K' -> 0.
   - `inf[4]` sound: 'A'=0 (adlib.bin, 0xC1C bytes), 'S'=0x0C (sblaster.bin, 0x767 bytes), 'R'=0x18 (roland.bin, 0x14E7 bytes), 'N'=0xFF (no driver, nothing loaded) -> `CS:[0x8a]`.
   - `inf[6]` digit: `CS:[0x85]` = inf[6]-'0'; nonzero => exec **game386.exe** instead of game86.exe (`1000:015f`).
   - `inf[8]` 'P' -> `CS:[0x86]`=1, 'Y' -> 2, else 0 (flash/pulse option, see §6).
3. Sound driver load (`1000:030a..041f`): driver file read to `drvseg:0000`; for SB, `llatron.spl`
   is read straight after it at `drvseg:0767` in chunks 0xE899 + 0xF000 + 0x8BF4 (= 156,813 bytes, the
   exact file size) [C]. Then `CALLF [CS:0x8b]` (= drvseg:0000) with **AH=0 = init/detect**; AL!=0 ->
   "Selected Sound Card not found". Before loading, hardware detection runs: AdLib at port 388h/228h
   via PIT-timed OPL timer test (`1000:0449..04fe`), MPU-401 at 330h/331h (`1000:04ff..0563`).
4. `1000:0103` exec (INT 21h/4B00h) **fs.exe** first, then **game86.exe**, both with EPB at `CS:0x73`:
   env=0 (inherit), command tail pointer = `CS:0081`. The 128-byte tail copied to the child PSP:80h is
   the launcher's own bytes `CS:0081..0100`, i.e. child **PSP:81h='l', 82h='m', 83h=0Dh, 84h=inf[6]-'0',
   85h=P/Y flag, 86h=VGA flag, 87h=0, 88h=joystick flag, 89h=sound type, 8Ah..8Dh=far pointer to
   driver** [C: game86 reads exactly these offsets, see below].
5. Child return code (INT 21h/4D00h) 0xFD => print "File Error / Try running 'Install' again" and exit. [C]
6. After the children return: wait 2 vsyncs, restore text mode 3 if needed, exit 4C00h. [C]

### game86.exe start-up (`entry` 1000:0010)
- `1000:0040` saves DS (`CS:[0x255]`), PSP segment ES (`CS:[0x257]`), SP; stores CS into `1e7b:013d`
  and `DS:[0x39e2]` (segment half of the far frame-hook pointer). Then: `e788` (resize memory block),
  `5f6b` (config), `60aa` (data files), `e519` (hook IRQs + PIT).
- **`1000:5f6b` config** [C]: seeds RNG from the BIOS tick count: `MOV ES,[0x44d]` then
  `ES:[0x46c]/[0x46e]`; `[0x44d]` holds **0** in the image, so this reads absolute `0000:046C`
  (= `0040:006C`) [C, confirmed by W3 running 5f6b/e616 in the unicorn harness,
  `tests/oracle/test_rng.py`]; `rng_mix` (`e616`) leaves ES=0 on return.
  `[0x453]=[0x457]=low word, [0x455]=[0x459]=high word`. Checks `PSP:81h=='l'` else exits with
  `4CFDh` (`1000:5f66`). Reads `PSP:85h -> [0x589]` (flash option), `PSP:86h -> [0x58a]` (VGA flag),
  `PSP:88h` joystick flag -> `[0x58b]` (joystick) / `[0x58d]` (keyboard). If joystick: calibration loop
  on port 201h (`1000:5fce..6064`) producing thresholds `[0x58e..0x59c]` (x/y centre*3/4 and *3/2 for
  both sticks); if no stick responds prints "No joysticks found - using keyboard instead" and falls back.
  `PSP:89h` sound type: 0xFF -> use internal dummy driver pointer `[0x404]:[0x406]`; else
  `[0x400]:[0x402] = PSP:8Ah..8Dh` and `[0x688]=[0x689]=1` (sound on flags).
- **`1000:60aa` data files** [C]: loads via `e65e` (open, read 1 KB chunks into `DS:0000..03FF`,
  LZSS-decode with `e6bb`): `g3.cmp` -> `[CS:0x261]:0000` (= segment **30EB**, zero-filled BSS in the EXE,
  18,144 bytes), `f.cmp` -> `DS:39EE`, `s.cmp` -> `DS:4141`. Then `scores.yak`: read 0xA1 bytes to
  `DS:40A0`, must return exactly 0xA0 bytes, else the built-in default table at `DS:0AD2` (160 bytes,
  "00050000inspiral...") is copied in.
- **LZSS format of *.cmp** (`1000:e6bb`) [C]: Okumura-style: flag byte, bit=1 literal, bit=0 => 16-bit
  (offset low byte, then high nibble=offset high | low nibble=length-3); 4 KB ring buffer at `SS:0000`
  (yes, the stack segment) starting at 0xFEE; a pair of 0x0000 followed by a 0 byte terminates.
- **`1000:e519` interrupts** [C]: saves INT 09 -> `[0x45f]`, INT 08 -> `[0x45b]`; installs
  `CS:e567` as INT 09 and `CS:3de4` as INT 08; programs **PIT channel 0 mode 3 (0x36) divisor 0x5CEC
  = 23788 => 1,193,182/23788 = 50.16 Hz**. The handler re-dispatches to the BIOS INT 08 whenever a
  16-bit accumulator `[0x44f] += 0x5CEC` overflows (`1000:3ec2`), keeping the DOS clock right.
- `1000:5960`/`e4f5`: **INT 10h AX=000Dh => EGA/VGA mode 0Dh, 320x200x16, 4 planes** [C]. Palette:
  if VGA (`[0x58a]!=0`) via INT 10h/1012h (DAC block) after converting 12-bit `$0RGB` words (Atari-ST
  style palette at `DS:2144`, 16 words) through the 4->6 bit table `DS:2134`; else via INT 10h/1002h
  with the 17-byte EGA table at `DS:2164` (`1000:1be6`). Other palette tables: `DS:2122`, `DS:2175`.

## 2. Timing model [C]

Two independent clocks:
- **INT 08 at 50.16 Hz** (`1000:3de4`): guard `[0x428]`; calls sound driver **AH=2** (driver tick);
  `[0x39e6]++` (global tick counter, used for attract timeout 0x5DC=1500 ticks=30 s, hi-score entry
  timeout 0x1770=6000, flashing phase); clears fire bits in `[0x7fb]/[0x7fc]` then re-reads fire
  (joystick buttons from port 201h -> `[0x7fd]`, or keyboard space bit from `[0x588]`); optional
  palette flash of colour 1 (`[0x589]!=0 && [0x902]!=0`, tables `DS:0658` (VGA) / `DS:0678` (EGA));
  then **`CALL [0x39e4]`** = the per-tick game hook. In play `[0x39e4]=0x1562`: the whole game
  simulation (input decode, player move, bullets, enemy spawn/update, collisions) runs **inside the
  timer interrupt at 50 Hz**, independent of frame rate.
- **Frame loop** (`1e7b:013f`, called as `CALLF 1000:e8ef` from every mode's wait loop): read input
  (`1e7b:0000`), flush BIOS key buffer, if last scancode `[0x586]==0x10 ('Q')` -> quit (`1000:5ed9`);
  erase previous frame's sprites by copying from the clean background page at VRAM 0x4000 (dirty list
  in CS: `[0x6733]/[0x6735]` double lists, entries {len,yoff,src} terminated by 0xFE0C; 96 unrolled copy
  routines via table `1e7b:031c` indexed by (words-1)*32+height); **`CALLF [0x39e0]`** = per-frame draw
  hook (play: `0x1a3b`); set CRTC start address (3D4h reg 0Ch) to flip pages; wait vertical retrace
  (`e5f3`, port 3DAh bit 3); palette pulsing list `[0x6c7]` (skipped when `[0x589]==2`);
  swap page pointers `[0x39d3]/[0x39d5]` (0x0000 / 0x2000), `[0x39d7]` = page being drawn.
  Frame rate = vertical refresh (60 Hz EGA, 70 Hz VGA mode 0Dh) — so **logic speed is 50 Hz on any
  machine, draw rate differs**. The tick handler only runs after `[0x429]` is set by the first draw.
- Pseudo-random: `e616` (3-word LCG-ish mixer seeded from BIOS ticks and perturbed when the BIOS tick
  equals state) is used only at init to shuffle a **512-byte permutation table at `DS:6AC9`**
  (4000 swaps); gameplay randomness is `e4de`: `BX=table[[0x39e9]++ & 0x1FF]`. Reproducing the exact
  sequence needs the same shuffle, hence the same seed (BIOS tick at start) — the port must make the
  seed injectable.

## 3. Top-level state machine (`entry`, 1000:00db..0980) [C]
- `00db` ATTRACT/TITLE: `[0x39e4]=3fde` (nop), draws title via `3ee0` (pictures from g3 segment
  entries `ES:[0x1c]`,`ES:[0x1e]`), `44d7` (attract animation), sets `[0x39e4]=0x3f6f`,
  `[0x39e0]=0x402c` (title menu: options bits in `[0x2cf6]`: bit0 2-player, bit1 double-controller
  mode, bit2 ?, bit4 current player). Loop `0136..0168`: wait for fire (`[0x7fb]/[0x7fc]` bit 7) or
  tick count 0x5DC -> `0271` rotates through demo/credits cases (`switchdata 1000:0269`:
  case1 = pick random level `[0x21aa]`/wave `[0x21a8]` for DEMO, case0/case2 = scrolling text
  `5844` with text at `DS:14E1` / `DS:117A`).
- `018f` NEW GAME: lives `[0x21a6]=5`, `[0x21c8]=2`, start positions `[0x2cfb..0x2d01]`, play
  sample 0x15 (`AH=3` jingle), zero score, level `[0x21aa]`.
- `020b` LEVEL SCRIPT interpreter: `[0x21aa]` indexes **`DS:2BDA`** (**100** pointers to level
  scripts, `2BDA..2CA1`, terminated by a 0 word at `2CA2`; a 0 pointer wraps to level 0 and
  increments the loop count `[0x233c]`); the scripts themselves lie back to back at `244C..2AFD`
  [C, W1/Forge, re-checked]. Script bytes: negative byte = opcode, handler `CS:[0245 + 2*(-op-1)]`,
  i.e. **0xFF..0xF8 = 8 handlers** `0421,0438,03f4,03e1,03cf,03bc,039c,0392` (`script_op0..7`)
  set per-level parameters e.g. speeds `[0x21ac/0x21ae]` scaled by loop count `[0x233c]`, bonus
  flags, music `03f4`); positive byte = wave: `0455` sets wave type `[0x21b0]`, resets entities,
  then reads (type,count) pairs until 0xFF (`3d09`) and spawns through **`DS:2CA4`** (41 enemy-type
  init handlers in `1000:1dcd..3d09`).
- `0455..0980` WAVE RUN: draw "herd wave"/"warp"/"bonus" banners (text strings at `DS:05A5..`), run
  `0616` loop until `[0x810]` != 0 (1=player dead, 2=wave cleared, 3='-' key level skip/abort,
  4=warp), handle lives, game over `08ee` (hi-score check `5da2`, name entry `59fa` with on-screen
  alphabet `DS:0994`, save `scores.yak` `5e89`, write 0xA0 bytes from `DS:40A0`).
- Exit `5ed9`: toggles driver off, waits for sample queue (AH=6 -> AH!=0 busy), driver AH=5, AH=1,
  restores INT 08/09 and PIT divisor 0 (18.2 Hz), INT 21h/4Ch with the pushed exit code.

## 4. Gameplay data structures (partial) [C]
- Entity table `DS:48C1`, **0x46 (70) records x 0x40 bytes**; `[0]` type (word; negative = free),
  `[2],[4]` x,y, `[6]` state (-> draw/animate handler table `DS:202E`, 19 entries), `[0x3a]` kind for
  the "counts" list `DS:220A`, `[0x3c]`/`[0x3e]` collision/hit handler index (-> table `CS:0C04`,
  18 entries), `[0x32]` sprite pointer, `[0x36]` frame. Per-type think handlers via `DS:1FD2`
  (**46 entries**, `1FD2..202D`, indexed by the low byte of `[0]`; `1000:4331..4373` loop) [C]
  — the earlier "13 entries" was wrong; 33 of the handlers were undisassembled code holes. Full
  record layout in §8.3.
- **Stale-SI spawn-list clobber** [C, W4/Keel, re-checked in the listing]: `enemy_shot_init`
  (`1232`) zeroes `[SI+8]`/`[SI+0A]`, which the other callers use as the parent's velocity.
  `think_13` (`256e`) calls `think13_spawn_shot` (`10b0`) at `2594`. Neither routine sets SI,
  so SI still holds the code offset `0x256E` that the `432b` dispatch loaded
  (`4363 MOV SI,[BX+1FD2]` / `4369 CALL SI`). The store therefore zeroes `DS:2576..2579`, which
  are level 21's spawn pairs 2 and 3 (type 1 x16 and type 5 x4 become type 0 x1 each). The
  damage lasts for the whole process, because nothing reloads the scripts `244C..2AFD`. The
  roster `220A` is rebuilt from live entities at `1ced`, not from the list, so only the
  spawns change. The port reproduces this on purpose: `script_clobber_spawns_10b0` (W4,
  9a3bc40) and levels.dat `clobber_10b0` (W1, f86753d).
- Player state: `[0x2cfb/0x2cfd]` P1 x,y,
  `[0x2cff/0x2d01]` P2 x,y, clamps x in [0x0F,0x130), y in [0x12,0xBC). Direction tables
  `DS:0743/0753/0763/07B3/07C3/07D3/07E3` indexed by 4-bit joystick nibble. Bullets: ring
  buffers `DS:6CC9` (P1) and `DS:70C9` (P2), 128 x 8 bytes. Lives `[0x21a6]`, score digits
  BCD-ish bytes at `DS:4441` (P1) / `DS:4449` (P2), 0x6c4 = demo-mode flag.
- Keyboard (`e567`): raw port 60h scancodes: 0x48/0x50/0x4B/0x4D up/down/left/right -> bits 0..3
  of `[0x588]`, 0x39 space -> bit 7 (fire). Last make code in `[0x586]`/`[0xb72]`; 0x19 'P' pause
  (`1562`, picks a random pause picture from `DS:09E2`), 0x2D 'X' -> `[0x810]=3` abort wave, 0x10 'Q' quit.
- Joystick: two sticks (bits 0/1 = stick 1 axes, 2/3 = stick 2), buttons bits 4-7 of port 201h.

## 5. Video / sprite engine [C]
- VRAM A000: three 8000-byte pages: 0x0000 and 0x2000 displayed alternately, **0x4000 = clean
  background copy** used to erase sprites (`62e8/62f6/6304` copy whole pages plane by plane with
  read-map 3CEh/4 and map-mask 3C4h/2).
- Rect fill `6112` uses GC set/reset (3CEh regs 0,1,3,5,8), map mask 0x0F.
- Sprites: segment **218C** (`CS:[0x25f]`, 60 KB) is a sprite bank embedded in the EXE: pointer table
  at `218C:0000`, each sprite header `{+0 ?, +2 flags(0=masked), +4 height-1, +6 width px, ...}` then
  rows of 10 bytes = 2 mask bytes + 4 planes x 2 bytes (16 px wide cells). `8cbb` draws centred at
  (AX,BX) with bit-mask register writes (3CEh/8) per plane, clips to 320x200, records dirty entries;
  dispatch by height (`CS:8E71`, 25 entries) or by shifted position (`CS:ABD1`, 25 entries, used when
  x%8!=0 -> `aa29/ac03` shifted blitters). `42f0/42da/42cb` are wrappers (scale/mirror flags in
  `[0x39d9]/[0x39da]/[0x39dc]`, `63d2` = scaled/zoomed draw). Text: `536f` renders strings with
  escape codes `\c<n>` colour, `\q` centre?, `\e` ?, `\r` newline, `\g`; glyphs from `f.cmp` data (format: `re-notes-sound-data.md` §4) at
  `DS:39EE` via `54f6/559f`.
- Text details [C, W6/Prism, instructions re-checked]:
  - Colour: `\c` writes the map mask byte `CS:559c` (= colour). `CS:559e` (= `~colour & 15`, the
    planes to clear) is only set while `smc_text_flag` (`CS:5362`) is 1, which happens only in name
    entry (`5a53`/`5a5c`); otherwise it is 0, so text ORs its colour into the existing pixels and is
    opaque only in name entry. The image starts with colour 0 (nothing is drawn until a `\c`).
  - `glyph_blit` (`559f`) has no bottom clip, and its left/right clipping makes the cell cursor
    drift one byte per row (aligned x = -8 or x >= 312, any unaligned x < 0): clipped glyph rows
    land shifted left into the previous row. W6 reproduces this in `src/game/text.c`.
  - The `\s` escape sets `cs_dirty_enable` = 0 (`5458`) and does not restore it, so later sprite
    draws in the same frame are neither dirty-recorded nor clipped until `frame_run` sets it to 1
    again (`1e7b:0166`).
- `text_scroller` (`5844`) [C, W6/Prism]: uses all 64 KB of a plane as one 320 x 1638.4-row
  canvas. It clears from `0x1F40` up (page 0 stays as the first screen), prints at y >= 206, moves
  the CRTC start 40 bytes per frame (`g_scroll_vram_off`, 64 KB wrap) and clears the line that left
  the top (`638f`). Since 65536 mod 40 = 16, rows are 16 bytes off after the wrap; with
  `g_scroll_mode` >= 2 `vram_addr_from_xy` subtracts 0x10 (`e0fa..e101`). `g_scroll_mode` is reset
  at `02ee`.
- The g3 segment (30EB) holds decompressed pictures: pointer table at `30EB:0000` (`ES:[0]..[0x1e]`
  used: logo, "press fire", 1/2 player labels, "yak 1992", etc.).

## 6. Config byte semantics as used by game86
- `[0x58a]` (PSP:86h, llama.inf[0]=='V'): VGA DAC palette path vs EGA 1002h path.
- `[0x589]` (PSP:85h, from llama.inf[8]; install writes 'Y'=Normal, 'P'=No screen flashes,
  'N'=No flashes or pulsing; llama.exe maps 'P'->1, 'Y'->2, other->0):
  - `!= 0` (Y or P): timer-driven **pulsing** of palette colour 1 through tables `DS:0658` (VGA) /
    `DS:0678` (EGA) while `[0x902]!=0` (`1000:3e4e`);
  - `== 2` (Y only): per-frame **screen flashes** — `1e7b:0196` reloads palette `DS:2144` every 30
    frames and executes the pending flash list `[0x6c7]` (INT 10h/1000h BX=1700h = background to
    bright white on EGA, 1010h DAC entry on VGA).
  So 'N' => neither effect; this matches install.exe's menu (see `docs/re-notes-fs-install.md`).

## 7. Coverage and annotation (W8)
Headless project `ghidra/headless/LlamatronCLI.gpr`, scripts in `tools/ghidra/` (README there).
Names live in `re/symbols.csv` (local) and are applied with `ApplySymbols.java`; the listings in
`re/listings/` are regenerated with symbolic operands and comments. `docs/funcmap.csv` lists every
routine (address, name, proposed C symbol, owner, range, status).

- Passes 1-3 (earlier): direct call targets and the dispatch tables known at the time.
- Pass 4 (`re/targets/game86_pass4.txt`): `table 1000:1047 12` — kill-action table jumped to from
  `ent_hit_03` (`1000:106f`, index entity `+0x1e`); closed `12c6`, `13a6`, `1416`, `3b92..3dac`.
- Pass 5: `table 3559:1fd2 46 1000` — the think table really has 46 entries; 33 new handlers
  (`0f10`, `148d`, `203f`..`3cea`, `504f`), which were most of the big holes (`0daa`, `31d8`,
  `37c2`, `495f` areas).
- Pass 6: `table 1000:0f7e 10` (pickup effects, index `+0x14`, from `think_1c` `1000:0f69`),
  `table 1000:3aff 4` (`think_0e` sub-modes, index `+0x20`), the pause tick hook `14d0`
  (installed at `159c`), entity callbacks `3119`/`32d7` (stored in `+0x1c` at `30da`/`316c`) and
  six fall-through routines (`1538`, `424c`, `42bc`, `4bd3`, `5994`, `5eae`).
- **Result: no code holes left in CODE_0/CODE_1.** The remaining undefined bytes are data only:
  header/copyright (`0000`, `0017`), CS dispatch tables (`0245`, `0269`, `0c04`, `0f7e`, `1047`,
  `3aff`, `8e71`, `abd1`, `1e7b:031c`), CS variables (`6194`, `e783`), the two dirty-rect lists
  (`673f`, `79f9`) and `e68c` (47 bytes inside the LZSS loader, [H] data). 356 functions, plus 13
  shared tails kept as labels inside other bodies (e.g. `entity_move_bounce` inside `think_03`).
- Still to do: CODE_2 (218C sprite bank) still has ~1080 mis-disassembled "instructions" from the
  first auto-analysis (`SUB_218c_*` call targets in the listing are bogus); clear them and type the
  sprite headers. 9 game86 routines are still `unk_*` (see funcmap).

## 8. DGROUP (segment 3559, 0x74D0 bytes) layout map
For `tests/adapters/dgroup_map.c` and the meaning of `game/state.h` fields. Offsets are DS
offsets; names are the `re/symbols.csv` names. The image is fully stored in the EXE but everything
from `39D7` up is zero (BSS). Ranges marked [H] are inferred from usage, not fully read.

### 8.1 Overview
| Range | Size | Content |
|---|---|---|
| `0000-03FF` | 1024 | `g_io_window`: file read buffer / LZSS input window (BSS) |
| `0400-0462` | | driver far ptrs, filename, spawn scratch, file handle, IRQ guard, scroller and rect-fill args, BIOS/PIT/RNG/vector state (§8.2) |
| `0463-0585` | | DOS error message strings |
| `0586-05A1` | | input + config bytes from the PSP tail, joystick thresholds, attract defaults, HUD dirty |
| `05A5-0614` | | wave banner strings |
| `0615-06C7` | | game/wave flags, continue and tune state, fire state, pulse tables, sound flags, demo flag, flash-list pointer |
| `06C9-0742` | | screen-flash lists (targets of `g_flash_list`) |
| `0743-07F2` | 7 x 16 | direction tables indexed by the 4-bit input nibble |
| `07F3-0949` | | per-wave runtime: respawn timers, input bytes, spawn bookkeeping, `g_wave_result`, script parameters `0812..0876`, intro freeze, free-slot count, text cursor vars [H for many] |
| `0994-0AD1` | | name-entry alphabet, name buffer, title tune list, pause-picture list, HUD digit/label glyphs (`09EE`, 10 x 16 B; `0A8E`/`0A9E`) |
| `0AD2-0B71` | 160 | default hi-score table |
| `0B72-0B79` | | last make code, text cursor |
| `0B7A-1FD1` | | text: attract scroller texts (`117A`, `14E1`), instructions/credits [H], misc tables `1FA2/1FB2/1FBA` used by `think_05` |
| `1FD2-202D` | 92 | `tbl_think_handlers` (46 near ptrs) |
| `202E-2053` | 38 | `tbl_anim_handlers` (19 near ptrs) |
| `2054-2133` | | per-type tables [H] (`2054`/`2056` read by `ent_hit_02`), title objects `2104` (9 x word timers) |
| `2134-2173` | | `tbl_vga_4to6` (16 B), `tbl_palette_game` (16 words, 12-bit RGB), `tbl_palette_ega` (17 B), black palette `2175` |
| `21A6-226D` | 200 | **`g_player`: current player state** (§8.4) |
| `226E-2335` | 200 | `g_player_other`: the other player's saved copy (swapped by `player_swap_state`) |
| `2336-233F` | | `2336/2338/233a` [H], `g_loop_count` (`233c`), `g_level_flags` (`233e`) |
| `2340-244B` | | `tbl_level_titles`: 4 records {level, 0, text ptr} + `-1`, then their texts |
| `244C-2AFD` | 1714 | level scripts, back to back (byte code; pointers in `tbl_level_scripts`) |
| `2AFE-2BD9` | 220 | zero in the image, no direct references [H: spare] |
| `2BDA-2CA1` | 200 | `tbl_level_scripts` (100 near ptrs) |
| `2CA2-2CA3` | 2 | 0 terminator of `tbl_level_scripts` |
| `2CA4-2CF5` | 82 | `tbl_enemy_init` (41 near ptrs) |
| `2CF6-2D06` | | `g_options`, player animation counters, `g_p1_x/y`, `g_p2_x/y`, `g_target_x/y` |
| `2D07-39D2` | 3276 | `tbl_row_offset`: VRAM byte offset per row, y*40 for y = 0..1637, i.e. the whole 64 KB plane as one tall canvas (used by `text_scroller`) [C, W6/Prism, re-checked against the image] |
| `39D3-39ED` | | page offsets (`g_page_front/back`, `g_draw_page`), blitter flags/scale/mirror, `g_draw_hook` (far), `g_tick_hook`, `g_tick_count`, `g_rng_index`, `g_script_ptr` |
| `39EE-409B` | 1710 | `g_font`: decoded f.cmp. Its last glyph (chars 7D-7F) is truncated in the file: its last 5 rows (10 bytes) are read from `409C-40A5`, i.e. from the variables below and the first hi-score bytes, so that glyph's look depends on runtime data |
| `409C-409F` | | `enemy_aim_update` results [H] (also the tail of the 7D-7F glyph) |
| `40A0-413F` | 160 | `g_hiscores` (scores.yak image) |
| `4141-4440` | 768 | decoded s.cmp: `g_sine8` (256 x s8), `g_sine16` (256 x s16) |
| `4441-4450` | 16 | `g_score_p1`, `g_score_p2`: 8 decimal digits each, one digit per byte, most significant first |
| `4451-4480` | 48 | [H] palette conversion buffer used by `palette_set` (`4451`, `4469`) |
| `4481-4486` | 6 | [H] 3-word state used by `think_1d/1e` and `enemy_init_10` |
| `4881-48C0` | 64 | `g_companion`: an entity record just before the table (`companion_update`) |
| `48C1-5A40` | 4480 | `g_entities`: 70 x 64-byte records (§8.3). `entities_clear` initialises 100 records (to `61C0`) |
| `68C9-69B8` | 240 | `g_shots`: 15 player shots x 16 bytes |
| `6AC9-6CC8` | 512 | `g_rng_table`: byte permutation shuffled at init |
| `6CC9-70C8` | 1024 | `g_trail_p1`: 128 x 8-byte position history (x, y, flag, ?) |
| `70C9-74C8` | 1024 | `g_trail_p2` |

### 8.2 Scalar variables (selection; full list in `re/symbols.csv`)
| Off | Name | Type | Meaning |
|---|---|---|---|
| `0400` | `g_snd_driver` | far ptr | sound driver entry; `0404` `g_snd_stub` is the swap partner |
| `0413/0415` | `g_tmp_413/415` | word | scratch parameters of child spawns |
| `0419..0421` | `g_eshot_param0..4` | word | enemy projectile template (copied into the new entity) |
| `0428` | `g_irq08_busy` | byte | tick re-entrancy guard |
| `0429` | `g_frame_drawn` | byte | the tick hook idles until the first frame of a wave is drawn |
| `0444..044a` | `g_rect_x0,y0,x1,y1` | word | `vram_fill_rect` arguments |
| `044d` | `g_bios_seg` | word | **0** (see §1) |
| `044f` | `g_pit_accum` | word | BIOS INT 08 chaining accumulator |
| `0453..0459` | `g_rng_state` | 4 words | `rng_mix` state |
| `0586` | `g_last_scancode` | byte | last make code |
| `0588` | `g_kbd_bits` | byte | arrows bits 0..3, space bit 7 |
| `0589/058a` | `g_flash_mode`, `g_vga` | byte | from PSP:85h/86h |
| `058e` | `g_joy_thresholds` | 8 words | calibrated joystick thresholds |
| `061f` | `g_continues` | word | continues left (4 per game) |
| `062f` | `g_enemies_alive` | word | the wave ends when it reaches 0 |
| `0650` | `g_shots_bounce` | word | power-up: player shots reflect at the edges |
| `06c4` | `g_demo_mode` | word | demo running, `score_add` disabled |
| `06c7` | `g_flash_list` | word | pending screen-flash list (0 = none) |
| `07f3/0634` | `g_p1/p2_respawn_timer` | word | [H] 0x64 at wave start; P1 `0xfffe` = dying |
| `07fb/07fc` | `g_input_b/a` | byte | controller bytes (direction nibble, fire bits 4..7) |
| `0810` | `g_wave_result` | word | 0 running, 1 player dead, 2 cleared, 3 aborted (X), 4 warp (+5 levels) |
| `08fc` | `g_wave_intro_timer` | word | 0x32 ticks of freeze; most think handlers return while non-zero |
| `0925` | `g_bounced` | word | set by `entity_move_bounce` |
| `0927` | `g_free_slots` | word | free entity slots (0x45 at wave start) |
| `0b74/0b76` | `g_text_x/y` | word | text cursor |
| `2cf6` | `g_options` | byte | bit0 2 players, bit1 double controller, bit2 shared start, bit4 current player is P2 |
| `2cfb..2d01` | `g_p1_x,y,g_p2_x,y` | word | x 0x0f..0x130, y 0x12..0xbc |
| `2d03/2d05` | `g_target_x/y` | word | position of the player the current entity targets (alternates per slot in 2P) |
| `39d3/39d5/39d7` | `g_page_front/back`, `g_draw_page` | word | VRAM page offsets 0x0000/0x2000, 0x4000 = clean background |
| `39d9/39da/39dc` | `g_draw_flags/scale/mirror` | | blitter mode (0xff normal draw, 1 scaled) |
| `39e0` | `g_draw_hook` | far ptr | per-frame hook |
| `39e4` | `g_tick_hook` | word | per-tick hook |
| `39e6` | `g_tick_count` | word | 50 Hz counter |
| `39e9` | `g_rng_index` | word | `rng_next` index |
| `39eb` | `g_script_ptr` | word | level script position |

### 8.3 Entity record (64 bytes; `g_entities`, `g_companion`) [C unless marked]
| Off | Type | Meaning |
|---|---|---|
| `+00` | word | type; `< 0` = free slot. Low byte = think index (`tbl_think_handlers`), high byte = type it becomes when the spawn "hatches" (`think_00`) |
| `+02/+04` | word | x / y (screen pixels, sprite centre) |
| `+06` | word | animate/draw state (`tbl_anim_handlers`); `0x12` = idle (`hook_nop`) |
| `+08/+0a` | word | velocity x / y (`entity_move_bounce`) |
| `+0c` | word | sprite index into the sprite bank pointer table (`218C:001C + 2*i`); negative = use `+2c` |
| `+0e` | word | draw scale / hatch countdown |
| `+10` | word | solid: shots and players only collide when non-zero |
| `+14` | word | general timer; pickup kind for `think_1c` (`tbl_pickup_effects`) |
| `+16..+1a` | word | handler-specific (projectile parameters from `g_eshot_param*`) |
| `+18` | word | hit points (`ent_hit_03` decrements, < 0 -> kill action) |
| `+1c` | word | handler-specific: callback near ptr (`3119`, `32d7`) or 32-bit position accumulator with `+1e` |
| `+1e` | word | kill action index (`tbl_hit_kill_actions`) |
| `+20` | word | sub-mode (`think_0e`: `tbl_think0e_modes`) |
| `+22/+23` | byte | reload / countdown pair (`think_25`) |
| `+24/+26` | word | handler-specific accumulators |
| `+28` | word | animation toggle (`anim_01`) |
| `+2c` | word | explicit sprite pointer when `+0c < 0` |
| `+32` | word | **overlay sprite** x (0 = no overlay). After the anim handler, `entities_animate_all` (`1000:4392..43be`) draws sprite bank index `+36` at (`+32`, `+34`), then decrements `+38` and clears `+32` when it reaches 0 [C, W4/Keel] |
| `+34` | word | overlay sprite y |
| `+36` | word | overlay sprite index (`218C:001C + 2*i`) |
| `+38` | word | overlay frames left |
| `+3a` | word | kind for the kind roster `g_type_counts` (low byte; `< 0` = not counted) |
| `+3c` | word | death sound request: low byte sample, high byte rate index (`1000:0eb0`) |
| `+3e` | word | hit handler index (`tbl_hit_handlers`) |

Player shots (`g_shots`, 16 bytes): `+0` x (`< 0` free), `+2` y, `+4/+6` velocity, `+8` sprite /
direction index, `+0c` lifetime when bouncing [H].

### 8.4 Player state block (`g_player`, 200 bytes at `21A6`; same layout at `226E`)
Swapped wholesale on player change (`player_swap_state`) and copied for a shared start
(`player_copy_state`, `g_options` bit 2). Bytes `21AC..21C8` are initialized in the image (default
speeds/parameters); everything else starts at 0.
| Off | Name | Meaning |
|---|---|---|
| `21A6` | `g_lives` (block start) | lives (5 at new game) |
| `21A8` | `g_wave` | wave within the level |
| `21AA` | `g_level` | level 0..39 |
| `21AC/21AE` | `g_enemy_speed_x/y` | set by script op 0 |
| `21B0` | `g_wave_type` | first byte of the current wave record |
| `21B2..21C2` | | per-level script parameters (ops 1..2) [H] |
| `21C4` | `g_powerup` | [H] power-up state |
| `21C6` | `g_extra_life_flag` | [H] |
| `21C8/21CA` | | [H] |
| `21CC-2209` | | no references in the listing (the one `[0x2200]` hit is bogus 218C disassembly) [C, W4/Keel] |
| `220A-226D` | `g_type_counts` | kind roster: {byte kind, byte count} pairs, filled to `-1` (50 words) by `wave_spawn_enemies`; for each live entity with `+3a >= 0` the pair of that kind gets count+1, a new kind takes the first free pair with count 0 (so count = entities of that kind - 1). Only the first 31 pairs are searched (`1000:1d15..1d3e`) |

## 9. CS-resident data and dispatch tables (segment 1000 unless noted)
| Address | Name | Entries | Index |
|---|---|---|---|
| `0245` | `tbl_script_ops` | 8 | -op-1 for script bytes 0xFF..0xF8 |
| `0255..0265` | `cs_saved_ds`, `cs_psp_seg`, `cs_vram_seg`, `cs_code1_seg`, `cs_sprite_seg` (218C), `cs_g3_seg` (30EB), clip temps | | |
| `0269` | `tbl_attract_cases` | 3 | attract rotation |
| `0c04` | `tbl_hit_handlers` | 18 | entity `+3e` |
| `0f7e` | `tbl_pickup_effects` | 10 | entity `+14` |
| `1047` | `tbl_hit_kill_actions` | 12 | entity `+1e` |
| `3aff` | `tbl_think0e_modes` | 4 | entity `+20` |
| `582d`, `5362`, `559b..559e`, `e131/e133`, `e783/e784` | self-modified bytes / temps | | see asm-idioms |
| `672f` | `cs_dirty_enable` | | dirty-rect recording on/off |
| `6733/6735` | `cs_dirty_ptr_a/b` | | write pointers into the two dirty lists `673f`/`79f9` |
| `8e71` | `tbl_blit_by_height` | 25 | sprite rows: 0-5 and 23-24 generic loop, 6-11 unrolled, 12-22 two-call wrappers |
| `abd1` | `tbl_blit_shifted_by_height` | 25 | same scheme for x%8 != 0 |
| `1e7b:031c` | `tbl_restore_by_size` | 96 | (words-1)*32 + rows; same 6/6/11 scheme per width |
