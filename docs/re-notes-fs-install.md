# RE notes: FS.EXE (front screen) and INSTALL.EXE

Sources: `re/listings/fs.exe.lst`, `re/listings/install.exe.lst` (Ghidra, image base 1000h),
hexdumps of `unpacked/fs.exe` / `unpacked/install.exe` (MZ header 0x200 bytes; file offset =
0x200 + (seg-0x1000)*16 + off). "Confirmed" = read directly from code/data; "Hypothesis" = inferred.

Ghidra rendering quirk (both listings): `MOV Sreg,reg` is printed with operands swapped, e.g.
`1000:0013 MOV DX,DS` is really `MOV DS,DX` (8E DA), `fs 1000:02df MOV BP,DS` is `MOV DS,BP`.

Both EXEs share the same hand-written prologue: `1000:0010` loads DS with the data segment,
sets SS=DS and adjusts SP to keep the same linear address, then `JMP 0052`. The 41 "undefined"
bytes at `1000:0029` are a 40-character ASCII copyright notice (1992), then a NOP. The text is
not reproduced here (rule 2).

---

## 1. FS.EXE — title/"front screen" viewer

**What it is (confirmed):** a title-screen displayer run by LLAMA.EXE before GAME86.EXE. It
installs a 50 Hz timer hook that drives the resident sound driver, loads ILBM picture(s)
(EGA: TRONEGA.LBM 640x350x4; VGA: NEWTIT.LBM + NEWTIT2.LBM 320x200x8 shown stacked as one
320x400 screen), starts a tune via the driver, waits for a key or joystick button, then
restores timer/video and exits with code 0. It contains no game logic and no embedded graphics.

### 1.1 Layout / memory map (confirmed)

| Segment (Ghidra) | Size | Content |
|---|---|---|
| `1000:0000-247F` | 9.3 KB | code; vars at `1000:00BC-00DD` (see below); INT 08 handler at `1000:018E-01BE` (Ghidra left it undefined) |
| `1248:0000-F9FF` | 64000 | file load buffer (LBM is read here; zero-filled in EXE) |
| `21E8:0000-F9FF` | 64000 | decoded bitplane buffer (VGA: 8 planes x 8000 bytes; EGA: planes 0,1 at +0 / +6D60h) |
| `3188:0000-DABF` | 56000 | EGA planes 2,3 at +0 / +6D60h (also VGA plane-buffer overflow area) |
| `3F34:0000-014F` | 336 | DS data (strings, tables, DTA) |

The 194 KB unpacked size is entirely these zero-filled buffers; no graphics/tables are embedded.

CS variables (`1000:00BC..`): `BC` saved DS, `BE` PSP seg, `C0`=A000h (VRAM), `C2`=1248h, `C4`=21E8h,
`C6`=3188h (segment constants, relocated), `C8/CA` file size lo/hi (from findfirst DTA),
`CC/CE` FORM size lo/hi, `D0` bytes per row `(w+7)>>3`, `D2` height, `D4` nPlanes, `D5` masking,
`D6` compression, `D7` pageWidth, `D9` pageHeight, `DC` "palette already loaded" flag, `DD` VGA flag.

DS data (`3F34:`): `0000` far ptr to sound driver (copied from PSP), `0004-002E` findfirst DTA
(`001E` = file size), `002F` file handle, `0033 "NEWTIT.LBM"`, `003E "NEWTIT2.LBM"`,
`004A "TRONEGA.LBM"`, `0056` PIT accumulator, `0058` saved SP, `005A` old INT 08 vector,
`005E "\r\n\aFile Error$"` (**unreferenced**), `006C "\r\n\r\nTry running 'Install' again - Press
any key, about to exit...$"`, `00AE "\r\n\aNot enough memory found to run 'Llamatron'$"`,
`00DC "\r\n\a\r\nNo or Invalid Game Configuration found - Please run 'Install'$"` (**unreferenced**),
`0122/0126/012A` RGB->EGA tables (3 x 4 bytes, see 3.3), `012E` 17-byte EGA palette buffer,
`013F` 17 zero bytes (all-black palette used to blank the screen).

### 1.2 Main flow (confirmed unless noted)

1. `1000:0052` save DS/ES(PSP)/SP. `0065`: if PSP:81h != 'l' -> `0165`: if current video mode
   (INT 10h/0Fh) != 3 set mode 3, `INT 21h/4C00h`. (Run without the launcher it exits silently
   with code 0; the "No or Invalid Game Configuration" string is never printed.)
2. `0070` copy PSP:8Ah/8Ch (driver far pointer) to DS:0000; `007E` PSP:86h (VGA flag) -> CS:[DD].
3. `0086` `FUN_2458`: INT 21h/4Ah shrink memory block to (SS-PSP)+(SP>>4)+1 paragraphs. On failure
   prints DS:00AE "Not enough memory...", AX=0, jumps `0178`: prints DS:006C "Try running
   'Install' again - Press any key...", INT 21h/08h waits for a key, then `CMP AL,0FDh` — AL is 0
   here so it falls to `0165` and exits with code 0. **FS.EXE never returns exit code 0FDh**; the
   check at `0188` and the "File Error" string are dead code (hypothesis: shared boilerplate with
   GAME86.EXE, which is presumably where 0FDh comes from).
4. `0089` `FUN_00DE`: hook timer. Saves INT 08 vector (INT 21h/3508h) to DS:005A, programs PIT
   channel 0: `OUT 43h,36h`, divisor **5CECh = 23788** -> 1193182/23788 = **50.16 Hz**, sets INT 08
   to CS:018E (INT 21h/2508h with DS=CS).
   Handler `1000:018E` (hand-disassembled from the 49 undefined bytes):
   `CLI; PUSH DS; MOV DS,CS:[BC]; PUSH all; MOV AH,2; CALL FAR [DS:0000]` (sound driver tick, AH=2);
   `ADD [DS:0056],5CECh; JC chain; MOV AL,20h; OUT 20h,AL; JMP done; chain: PUSHF; CALL FAR [DS:005A]`
   (original BIOS INT 08 every 65536/23788 = 2.755 ticks -> 18.2 Hz preserved); `POP all; POP DS; STI; IRET`.
5. `008C` `FUN_01BF`: show the picture(s) — see 1.3.
6. `008F` `MOV AL,1Bh; MOV CL,1; MOV CH,6; MOV AH,3; CALLF [DS:0000]` — driver call AH=3
   (hypothesis: "start tune/sample #1Bh", CX=0601h parameters). Driver init (AH=0) was done by LLAMA.EXE.
7. `009B-00B9` wait for exit input, according to PSP:88h (joystick flag):
   * `== 0` (keyboard): `INT 21h/08h` — blocks until any key (consumed).
   * `!= 0` (joystick): loop `IN AL,201h; NOT AL; TEST AL,30h` — exits when **joystick A button 1 or 2**
     is pressed. Keyboard is *not* polled in this mode.
8. `0142` exit: `FUN_012B` resets SP to the saved value and flushes the keyboard buffer
   (INT 21h/0Bh + 07h loop, `FUN_011C`); two vsync waits (`FUN_010A`: wait for bit 3 of 3DAh to
   clear then set); CLI; restore INT 08 from DS:005A; PIT divisor 0 (65536 = 18.2 Hz); STI;
   `0165`: set text mode 3 if not already; **`INT 21h/4C00h` (exit code always 0)**.
   The driver is not stopped (hypothesis: the tune keeps playing into GAME86.EXE).

### 1.3 Picture display `FUN_01BF` (confirmed)

Video selection is purely on CS:[DD] = PSP:86h (VGA flag from llama.inf[0]=='V'):

**VGA path (`01C9`)**
1. `FUN_23FD`: INT 10h AX=0013h, then unchain into **320x400 planar ("Mode X" 400-line)**:
   SC reg 4: `(v & ~08h) | 04h` (chain-4 off, odd/even off); GC reg 5 `&= ~10h`; GC reg 6 `&= ~02h`;
   map mask 0Fh and clear all 64 KB of A000h; CRTC reg 9 `&= E0h` (max scan line 0 -> 400 rows);
   CRTC reg 14h `&= ~40h` (dword mode off); CRTC reg 17h `|= 40h` (byte mode). Pitch stays
   80 bytes/row/plane = 320 px.
2. `01CC-01DE` CRTC 0Ch/0Dh start address = 8000h (points at cleared VRAM -> screen black while loading).
3. `FUN_238B` clear buffer 1248h; load+decode `NEWTIT.LBM` (`FUN_0293`, SI=0033h);
   `FUN_04CC` planar->chunky into 1248h; `FUN_23A2` chunky -> VRAM offset 0 (rows 0-199).
4. Same for `NEWTIT2.LBM` (SI=003Eh) with `FUN_239D` -> VRAM offset 3E80h (rows 200-399).
5. Two vsyncs, start address = 0 -> both pictures appear at once as a 320x400 image
   (NEWTIT on top, NEWTIT2 below). Only the CMAP of the *first* file is loaded (flag CS:[DC]);
   NEWTIT2's CMAP (differs in entries 160-175) is ignored.

`FUN_04CC/0536` planar->chunky: for plane p (0..7) at 21E8h:p*1F40h, with weight DL=1<<p, treat the
8000-byte plane as a flat bitstream: 80 iterations x 100 bytes -> 800 pixels each (`ROL AL`, `OR ES:[DI+n],DL`
fully unrolled, `1000:0539-2210`), total 64000 chunky bytes. (Works because 320x200 rows are contiguous.)

`FUN_23A2/239D` chunky->Mode X: for plane p=0..3: `OUT 3C4h, (1<<p)<<8 | 2`; copy 16000 bytes taking every
4th source byte starting at p (`MOVSB; ADD SI,3`). I.e. pixel x -> plane x&3, byte x>>2.

**EGA path (`0216`)**
1. INT 10h AX=0010h (640x350x16). Clear 1248h. Load+decode `TRONEGA.LBM` (SI=004Ah).
2. `FUN_0229`: `FUN_04C1` INT 10h AX=1002h ES:DX=DS:013F (17 zero bytes) -> all palette regs black;
   GC 5=0 (write mode 0), GC 3=0, GC 8=FFh; then for map mask 1,2,4,8 `REP MOVSW` 36B0h words
   (28000 bytes = 80x350) from 21E8h:0000, 21E8h:6D60, 3188h:0000, 3188h:6D60 to A000h:0000;
   then INT 10h AX=1002h with DS:012E (palette built from CMAP, see 3.3).

### 1.4 File loading `FUN_0293` (confirmed)

Open (INT 21h/3D00h) -> handle DS:002F; set DTA=DS:0004 (1Ah); **findfirst 4Eh** on the same name
only to obtain the file size (DTA+1Ah -> CS:[C8]/[CA]). Size high word >1 -> error. If high word ==1:
read 8000h to 1248h:0000, 8000h to 1248h:8000, then the remaining low-word bytes to 2248h:0000
(this overlaps the plane buffer at 21E8h; works only because decoding is sequential — hypothesis,
never exercised by the shipped files, all < 64 KB). Otherwise one read of `size` bytes to 1248h:0000.
Close. Error exits are `JMP 0292` (a bare RET). **Latent bug:** the open-failure branch at `029D`
RETs with SI still pushed, i.e. jumps to CS:0033/003E/004A inside the copyright string. After
`02A9` errors return cleanly to the caller, which then displays the (zeroed) buffer -> black screen,
exit code still 0. So a missing LBM does *not* yield 0FDh.

### 1.5 Confirmed facts about ports/interrupts used

INT 10h: AH=00 AL=13h (VGA), AL=10h (EGA), AL=03h (exit), AH=0Fh; AX=1002h (EGA palette block,
17 bytes), AX=1012h (VGA DAC block, BX=0, CX=100h). INT 21h: 35/25 (INT 08), 4A, 3D, 1A, 4E, 3F,
3E, 0B, 07, 08, 09, 4C. Ports: 40h/43h PIT, 20h PIC EOI, 201h joystick, 3DAh vsync, 3C4h/3C5h
sequencer, 3CEh/3CFh graphics controller, 3D4h/3D5h CRTC.

---

## 2. INSTALL.EXE

Text-mode installer/configurator. Exit code always 0 (`1000:0184 MOV AX,4C00h`). Data segment 106Ah
(file offset 0x8A0+off).

### 2.1 Flow (confirmed)

1. `0052` mode 3; INT 10h AX=1001h BH=1 (blue border). Header line DS:0012 attr 1Fh row 0
   `"         PC Llamatron 1.0 Installation                Unregistered Game         "`; blank blue bars
   (DS:00B4 = 80 spaces) rows 1 and 23; footer DS:0063 row 24
   `"Use cursor keys to select option (↑↓)                 'Esc' key to return to DOS"`.
   `FUN_0398` fills rows 2-22 with alternating E8h/E9h chars, attr 13h.
2. `0092` menu @106A:016F "Select type of installation": 0 = Install game onto a hard disk drive,
   1 = Make changes to game options (**default 1**). Result stays in the struct's selected-index
   byte `106A:0177`.
3. `0098` menu @01EE "Select drive": A..E (default C). Drive letter written over the `@` placeholder
   of `0105 "@:..\LLAMA.BAT"`, `0114 "@:..\INSTALL.BAT"`, `0125 "@:LLAMA.INF"`, `0167 "@:LLAMA"`
   and `087B` (dest drive var). INT 21h/36h on that drive; AX=FFFFh -> box @02E1 "Cannot detect hard
   disk drive / Press 'Enter' to try again" and back to the drive menu.
4. `00C5` if [0177]==1 -> skip to options (step 9).
5. `00CF` free bytes = secs/cluster * bytes/sec * free clusters; needs DX>=2 (>= 128 KB) else box
   @0310 "Not enough free memory on disk / Delete files and run Install again" -> Esc -> exit.
6. `00DE` INT 21h/19h current drive -> `087A` (source drive letter). `00F0` confirm box @03BE
   "Install is about to create subdirectory '\LLAMA' on your destination drive (current directory)".
7. `00F6` mkdir `X:LLAMA` (39h; error other than AX=5 "already exists" -> box @0366 "Could not create
   directory on disk"); `010D` chdir `X:LLAMA` (3Bh).
8. `0117-0159` copy loop: `[0010]`=1 disk, `[0011]`=13 files on disk 1 (the 0Dh that precedes the header
   string). For each disk: draw "Copying file" box @047E, findfirst the disk's first file on the source
   drive and **retry forever until found** (disk-swap wait), then `FUN_047E` per file: open source
   `S:NAME`, create dest `D:NAME` (3Ch), copy in 8000h-byte chunks via buffer DS:0932, close. Error ->
   box @039A "Error copying file" -> Esc -> exit. File table at `106A:087C` (13 word ptrs):
   SBLASTER.BIN, F.CMP, G3.CMP, S.CMP, FS.EXE, GAME86.EXE, LLAMA.EXE, LLAMA.INF, NEWTIT.LBM,
   NEWTIT2.LBM, TRONEGA.LBM, LLATRON.SPL, INSTALL.EXE.
9. `015B` option menus: @04E7 video (6 items, default 2), @0640 control (2, default 0),
   @071E sound (2, default 0), @07D5 processor (1 selectable item; result never read).
10. `0173` `FUN_050F` writes LLAMA.INF (and the BATs when installing), shows box @04BA
    "Installation complete", `INT 21h/07h` wait key, `017A` clear screen, border 0, exit 0.
    Esc inside any menu goes straight to `017A`.

### 2.2 Menu structure (confirmed from `FUN_0318`, `01D2`, `0429`, `024B`, `0271`, `029D`, `02DB`)

`BP ->` +0 x, +1 y, +2 w, +3 h, +4 frame attr, +5 selected attr, +6 normal attr, +7 nSelectable,
+8 selected index (persists; read later as the result), +9 nDisplayLines, +0Ah title ptr,
+0Ch footer ptr, +0Eh.. word ptrs to lines. Each string = 1 attr byte + ASCIIZ text; the menu code
rewrites the attr byte (0Fh = selected). Keys: 0 then 48h/50h up/down, 0Dh Enter -> AL=index,
1Bh Esc -> exit program. Menus with 2-row spacing (`029D`) are used for choices; message boxes use
`02DB` 1-row spacing and wait for Esc (`03CE`) or Enter/Esc (`03F9`).

### 2.3 LLAMA.INF byte layout (confirmed, `FUN_050F` 0556-05C5)

Template at `106A:015D` = `"V J A 0 N\0"` (10 bytes incl. trailing NUL); 10 bytes written with INT 21h/40h.
Odd bytes 1,3,5,7 are spaces (20h), byte 9 is 00h.

| idx | value | set from | meaning |
|---|---|---|---|
| 0 | `'V'` if video idx < 3 else `'E'` | `[04EF]` | VGA 256-colour / EGA 16-colour |
| 2 | `'J'` if control idx == 0 else `'K'` | `[0648]` | Joystick (IBM game card, 1 or 2) / Keyboard (cursor keys, Space fire, P pause) |
| 4 | `'S'` if sound idx == 0 else `'N'` | `[0726]` | Soundblaster / No music or sound effects. (`'A'` AdLib / `'R'` Roland exist in the template & launcher but are "Registered Version" only; this installer never writes them) |
| 6 | always `'0'` | — | processor: 8088/8086/80286 -> GAME86.EXE. (`'1'`-ish/386 is registered-only; the processor menu result `[07DD]` is ignored) |
| 8 | `'Y'` if video idx 0 or 3; `'P'` if 1 or 4; `'N'` if 2 or 5 | `[04EF]` | Y = Normal (flashes + pulsing), P = No screen flashes (pulsing only), N = No flashes or pulsing |

Video menu items (idx): 0 "256 colour VGA - Normal", 1 "256 colour VGA - No screen flashes",
2 "256 colour VGA - No flashes or pulsing", 3 "16 colour EGA - Normal", 4 "16 colour EGA - No screen
flashes", 5 "16 colour EGA - No flashes or pulsing" (+ note "Flashing/pulsing causes erratic play speed
on some video cards"). Shipped `original/llama.inf` = `"V K N 0 N\0"` = VGA, keyboard, no sound, 8086, no flashes/pulsing.

### 2.4 Batch files (confirmed, written only in install mode, to the parent dir of \LLAMA, i.e. the drive root when installing from root)

* `X:..\LLAMA.BAT` (21 bytes, `106A:0131`): `CD LLAMA\r\nLLAMA\r\nCD..`
* `X:..\INSTALL.BAT` (23 bytes, `106A:0146`): `CD LLAMA\r\nINSTALL\r\nCD..`

(No trailing CR/LF after `CD..`.)

---

## 3. ILBM loading details for a port

### 3.1 Chunk parsing (`fs 1000:033A-0439`, confirmed)
* `FORM` at 0, big-endian size at 4; only the **low 16 bits** of the size are compared against
  `filesize-8` (must match exactly). `ILBM` id at 8 is *not* checked.
* `BMHD` must be the first chunk (id at 0Ch, size byte at 13h must be 14h). Fields read (all BE):
  w (+0) -> bytesPerRow=(w+7)>>3, h (+2), nPlanes (+8), masking (+9), compression (+0Ah) **must be 1**
  (ByteRun1) else abort, pageWidth (+10h), pageHeight (+12h). x/y origin, transparent colour, aspect ignored.
* `CMAP` must be the second chunk (id at 28h, data at 30h). Size from BE word at 2Eh; SI advances by
  the size *without* even padding (fine: 48 and 768 are even).
* Then chunks are skipped (`SI += 8 + size`, size rounded up to even) until `BODY`; DPPS/CRNG/TINY
  in the shipped files are skipped this way. Colour cycling (CRNG) is ignored.

### 3.2 BODY decode (`FUN_2211/231F/2354/2362`, confirmed)
Standard ILBM row-interleaved ByteRun1: per row, per plane, decode exactly bytesPerRow bytes
(`231F`: n = signed byte; n==-128 skip; n>=0 copy n+1 literal bytes; n<0 repeat next byte 1-n times;
decoding stops as soon as the row's byte count is reached). If masking != 0 one extra (mask) row is
decoded and discarded (`2362`). Source pointer wraps from 64 KB into the next segment (`2354`).
Plane destinations: VGA: 21E8h + plane*1F40h (8000 = 40x200), row stride **28h** (320 px assumed).
EGA: plane0 21E8h:0, plane1 21E8h:6D60h, plane2 3188h:0, plane3 3188h:6D60h (28000 = 80x350),
row stride **50h** (640 px assumed). Row count = BMHD height; nPlanes stops the per-row plane loop
early (1..8). Width is *not* used for the stride — images must be exactly 320 (VGA) / 640 (EGA) wide.

### 3.3 CMAP handling (`FUN_043A`, confirmed)
* **VGA (CS:[DD]!=0):** every CMAP byte is shifted right by 2 in place (8-bit -> 6-bit DAC), then
  `INT 10h AX=1012h, BX=0, CX=100h, ES:DX -> CMAP` — always 256 entries regardless of CMAP size
  — but only for the first picture loaded (flag CS:[DC]). The second picture (NEWTIT2.LBM) is shown with
  NEWTIT.LBM's palette; their CMAPs differ in indices 160-175.
* **EGA:** nColours = size/3, clamped to 16. For each colour the top 2 bits of each 8-bit component
  (`v >> 6`) index a 4-entry table and the three results are **added** into one EGA palette register value:
  red `3F34:0122` = {00h,20h,04h,24h}, green `0126` = {00h,10h,02h,12h}, blue `012A` = {00h,08h,01h,09h}
  (EGA 6-bit colour: bit5/4/3 = secondary r/g/b (1/3 intensity), bit2/1/0 = primary R/G/B (2/3)). So
  intensity 0->off, 1->secondary only, 2->primary only, 3->both. Result goes to the 17-byte table at
  `3F34:012E` (entries 0-15; entry 16 overscan stays 0 = black border) and is applied with
  `INT 10h AX=1002h` *after* the planes have been copied; the screen is blanked with the all-zero table
  at `3F34:013F` beforehand. CMAP bytes are not shifted in this path. TRONEGA.LBM has a 16-entry CMAP.

### 3.4 Shipped file facts (from the files, not the code)
* `tronega.lbm`: 640x350, 4 planes, CMAP 16, BODY 41923 bytes; `newtit.lbm`/`newtit2.lbm`: 320x200, 8 planes,
  CMAP 256, BODY 25112 / 24107 bytes. All compression=1, masking=0, chunk order BMHD, CMAP, DPPS, 16xCRNG, TINY, BODY.

### 3.5 What a port must reproduce
Timer: 50.16 Hz driver tick (AH=2) with BIOS 18.2 Hz chaining; title music call AH=3/AL=1Bh/CX=0601h;
VGA 320x400 composite of NEWTIT (top) + NEWTIT2 (bottom) with NEWTIT's palette >>2; EGA 640x350 with the
2-bit-per-channel palette mapping above; exit on any key (keyboard config) or joystick button 1/2
(joystick config); exit code 0 in every case.
