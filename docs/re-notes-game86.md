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
- **`1000:5f6b` config** [C]: seeds RNG from BIOS tick count `0040:006C` (`[0x44d]`=0x40):
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
- `020b` LEVEL SCRIPT interpreter: `[0x21aa]` indexes **`DS:2BDA`** (40 pointers to level scripts);
  script bytes: negative byte = opcode -> `CS:0245` table (7 handlers `039c,03bc,03cf,03e1,03f4,0421,0438`
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
  (13 entries, `1000:4331..4373` loop) [C]. Player state: `[0x2cfb/0x2cfd]` P1 x,y,
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
  escape codes `\c<n>` colour, `\q` centre?, `\e` ?, `\r` newline, `\g`; glyphs from `f.cmp` data at
  `DS:39EE` via `54f6/559f`.
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

## 7. Coverage after headless passes (see `scratchpad/dis/Disasm.java`, targets files)
- CODE_0 (1000:0000-e7af): 15,783 instructions, 15,174 undefined bytes left (of which ~9.6 KB are
  the two CS-resident dirty-rect lists at `1000:673f` and `1000:79f9`, i.e. data). 269 functions.
- CODE_1 (1e7b): fully code except 211 bytes. CODE_2 (218C): sprite data (1080 "instructions" there
  are mis-disassembly from the first auto-analysis and should be cleared). CODE_3 (30EB): zero BSS.
  CODE_4 (3559): DGROUP data; holes are tables/strings (level scripts 2BDA.., enemy tables 2CA4..,
  palette, text, default scores, font/sine BSS at 39EE/4141, entity table 48C1 BSS).
- Remaining code holes worth a pass: `1000:0daa` (1160 B), `1000:12c6`, `1000:31d8`, `1000:37c2`
  (1351 B), `1000:495f` (1917 B): reached via tables not yet followed (check `[SI+0x3c]`-indexed
  tables and the text-engine escape dispatch).
