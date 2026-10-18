# Llamatron 2112 (DOS, 1992) — sound driver, sample bank and .cmp data files

Scope: `sblaster.bin`, `llatron.spl`, `f.cmp`, `g3.cmp`, `s.cmp`, and the game86.exe code that
uses them. Addresses are Ghidra addresses (image base 0x1000, DGROUP = 0x3559, file offset of
`1000:xxxx` = 0x200 + xxxx). Driver offsets are offsets into `sblaster.bin` (= offsets in its segment).
"Confirmed" = read from the disassembly / verified against the bytes; "Hypothesis" = inferred.

Scratch tooling (not part of the project): capstone venv + scripts in
`/private/tmp/claude-501/-Users-jorgenachtigall-projects-llamatron-re/4426fa5f-e6fe-4b0e-8295-4b88be12627c/scratchpad/sound/`
(`d16.py` raw 16-bit disassembler, `d86.py` game86 disassembler, `lzss.py` .cmp decompressor, `spl.py` sample-table dump;
decompressed `*.cmp.dec` files are there too).

---

## 1. sblaster.bin — "Sound Images Generation 2" Sound Blaster driver (Tony Williams, 13.07.92)

1895 bytes = 0x767, position independent, all data in its own segment (DS=CS set in dispatcher).
Loaded by the launcher `llama.exe` into a fresh DOS block of 0x26C0 paragraphs (INT 21h/48h,
`llama.exe` 1000:05f0) at offset 0, immediately followed by `llatron.spl` at **byte offset 0x767**
of the same segment (`llama.exe` 1000:03c8 reads the .spl into DS:0x767 in three chunks 0xE899 + 0xF000 + 0x8BF4
= 0x2648D bytes = file size). So "seg:0767h" means offset 0x767 bytes, not paragraphs.

### Entry points (confirmed)

| offset | code | purpose |
|---|---|---|
| 0x000 | `call 0x76 ; retf` | far-call API entry (the game uses `CALLF [DS:0x400]`) |
| 0x004 | `call 0x76 ; iret` | same dispatcher, returns with IRET: for installing the driver on a software INT vector. **Not used** by llama.exe or game86 (neither hooks a vector to it; both use far calls). It is *not* the hardware IRQ handler. |
| 0x008 | word | IRQ counter: `add word [8],0x101` on every SB IRQ (both bytes +1). Returned by AH=2. |
| 0x00A | text | copyright / "SoundBlaster  LLamatron" |
| 0x076 | dispatcher | saves DS,BX,CX,DX,DI,SI; DS=CS; `if AH > [0xA1] -> AX=0xFFFF`; else `call [0xA2 + AH*2]` |
| 0x0A1 | byte | max function number. **0 until init succeeds**, then set to 6 (0x24D). |
| 0x0A2 | 7 words | jump table: 01A2, 00EC, 00C2, 010F, 013D, 0135, 0143 |
| 0x4FA | | real SB IRQ handler (installed via INT 21h/25h during init) |
| 0x4E3 | | temporary IRQ handler used only for IRQ autodetect |
| 0x5F6 | | unreferenced debug routine (VGA attribute reg 0x11 = border colour) |
| 0x609 | 34 × 10 bytes | **sample table** (see §2) |
| 0x75D | 8 bytes 0x80 | silence block (referenced by dummy table entries 0/1, effectively unused) |

### API (confirmed)

All functions preserve BX,CX,DX,SI,DI,DS; AX is the result.

| AH | name | inputs | outputs | behaviour |
|---|---|---|---|---|
| 0 | **Init / detect** | — | AL=0 ok, AL=1 "card not found" | (0x1A2) Base port fixed at **0x220** ([0xB6]=0x228 is the FM status port; no BLASTER env parsing). 1) AdLib/OPL2 timer-detect at 0x228/0x229 (regs 4,2,4; status &0xE0 must go 0x00→0xC0; uses PIT ch2 at ports 0x42/0x43/0x61 as a delay) — fail → AL=1. 2) Build port table [0x2BD..0x2CF] = 220..226, 22A (DSP read), 22C (DSP write), 22E (DSP status/IRQ ack). 3) DSP reset (0x226 ← 1, 0, wait for 0xAA on 0x22A, up to 1000 tries) — fail → AL=1. 4) IRQ autodetect: for IRQ in **7,5,2,3**: hook vector 8+IRQ to 0x4E3, unmask in PIC (0x21), DSP cmd **0xF2** (raise IRQ), spin ≤0xFFFF reading 0x22A until the temp handler sets [0x2A6]; restore. First IRQ that fires wins → hook its vector permanently to 0x4FA (old vector saved at [0x2B9]), unmask, [0x2A6]=IRQ. None → AL=1. 5) Enable functions 1..6 ([0xA1]=6). AL=0. Also stores linear address of DS:0x767 at [0xB9]/[0xBB] (unused). |
| 1 | **Shutdown** | — | AL=0 | (0x0EC) **Blocks** while a sample is playing ([0x2A9]!=0), restores the IRQ vector, masks the IRQ in the PIC. Called by game86 at exit (1000:5f51) after it has itself waited for silence. |
| 2 | **Tick / service** | — | AH = IRQ count (low byte of [8]), AL=0; AX=0xFFFE if re-entered | (0x0C2) Must be called periodically; game86 calls it from its INT 8 handler at **50.16 Hz** (1000:3e02; PIT divisor 0x5CEC). Does, in order: (a) 0x171 pending stop: if [0xB3]==0xFF or ==current sample → DSP **0xD0** (halt DMA), read 0x22E, idle; (b) 0x14B promote request: if idle ([0xB8]==0) and 1 ≤ [0xB2] ≤ 0x20 → current=[0xB2], priority [0x2AF]=[0x2B0], start index [0x2AD]=sample+1; (c) if [0x2AD]!=0 → 0x38C start DMA (see below). Return value is ignored by game86. |
| 3 | **Request sample** | AL = sample # (1..32; 0 or >32 ignored), CL = rate index 0..4, CH = priority | AL=0 | (0x10F) [0xB2]=AL; [0x2AE]=TC_table[CL] (xlat at 0x3D2 = `01 34 7E 98 CE`); [0x2B0]=CH. If something is playing and **CH ≥ current priority (unsigned)** → [0xB3]=current (stop it at next tick so the new one starts at that tick). Otherwise the request sits in the single pending slot ([0xB2], latest request overwrites it) and starts at the first tick after the current sample's IRQ. Nothing happens until the next AH=2 tick (≤20 ms latency). |
| 4 | **Stop sample** | AL = sample # | AL=0 | (0x13D) [0xB3]=AL; effective at next tick only if AL is the sample currently playing. |
| 5 | **Stop all** | — | AL=0 | (0x135) [0xB3]=0xFF → current sample halted at next tick. |
| 6 | **Status** | — | **AH = sample # now playing (0 = idle)**, AL = priority of current/last sample ([0x2AF], not cleared on finish) | (0x143) game86 busy-waits `until AH==0` at 1000:065d, 0924, 5eef, 5f18 and tests AL at 1000:247d. |
| ≥7 | — | | AX=0xFFFF | |

### Playback model (confirmed)

* **One voice, no mixing.** 8-bit unsigned mono (centre 0x80), SB 1.x single-cycle DMA
  (DSP **0x40** time constant, **0xD1** speaker on, **0x14** + (len-1) 8-bit DMA output; **0xD0** to halt).
  DMA channel 1: ports 0x0A (mask 5 / unmask 1), 0x0C (clear FF), 0x0B mode **0x49** (single, inc, no auto-init, mem→dev),
  address 0x02/0x02, page 0x83, count 0x03/0x03.
* Start (0x3ED): entry = table[AL+1]; linear = DS*16 + 0x767 + offset24; count = length-1. If the block crosses a 64 KB
  DMA page the transfer is split: first part to the page end, remainder ([0x2B3]) programmed from the IRQ handler
  (0x511-0x545) at page+1 offset 0. End of sample: IRQ handler (0x553) sends EOI, clears busy ([0x2A9]) and current ([0xB8]).
* **Loop fields are dead**: table words 3/4 are read into [0x2B5]/[0x2B7] and the code at 0x565-0x5F5 would
  restart DMA at the loop point, but nothing jumps to 0x565 (0x50F `je 0x553` goes straight to "finished"); all
  table entries have loop start/len = 0 anyway. Samples never loop.
* Speaker is never turned off (no 0xD3). Mixer never touched. PIT only used as a delay during AdLib detection.
* Rate index → DSP time constant → Hz (`rate = 1e6 / (256 - TC)`; TC values confirmed, Hz derived):

| CL | TC | Hz | used by game86 |
|---|---|---|---|
| 0 | 0x01 | ≈3922 | yes |
| 1 | 0x34 | ≈4902 | yes (most calls, title tune) |
| 2 | 0x7E | ≈7692 | yes |
| 3 | 0x98 | ≈9615 | yes |
| 4 | 0xCE | 20000 | yes (a few) |

(CL>4 would index past the table into code bytes — never happens.)

### Driver variables (for reference)

`[0xB2]` pending request, `[0xB3]` pending stop (0xFF=any), `[0xB8]` current sample, `[0xB6]` FM status port (0x228),
`[0x2A6]` IRQ#, `[0x2A7]` saved PIC mask, `[0x2A8]` IRQ under test, `[0x2A9]` DMA busy, `[0x2AA]`/`[0x2AB]` DMA page/addr,
`[0x2AD]` start index pending, `[0x2AE]` TC, `[0x2AF]` current priority, `[0x2B0]` requested priority,
`[0x2B1]` DMA count, `[0x2B3]` second-half count, `[0x2B5]`/`[0x2B7]` loop (dead), `[0x2B9]` old IRQ vector,
`[0x2BD..0x2CF]` port table, `[0xC0]` re-entrancy flag.

---

## 2. llatron.spl — raw sample bank (156813 = 0x2648D bytes)

**No header.** The file is pure 8-bit unsigned PCM; the directory lives in the driver at `sblaster.bin` 0x609:
34 entries × 10 bytes `{u16 offset_lo, u16 offset_hi, u16 length, u16 loop_start, u16 loop_len}`, offset relative to
the start of the .spl (= driver+0x767). Entry index = game sample number + 1 (entries 0 and 1 are dummies pointing at
8 bytes of silence; AL=0 is rejected by the driver). Confirmed by: last entry (sample 30) ends exactly at
0x2648D = EOF, and entries are contiguous for most of the bank.

| AL | offset | length | AL | offset | length |
|---|---|---|---|---|---|
| 1 | 0x00000 | 5301 | 17 | 0x0A14C | 5022 |
| 2 | 0x015CC | 558 | 18 | 0x0B4EA | 10741 |
| 3 | 0x017FA | 2790 | 19 | 0x0DEDF | 5161 |
| 4 | 0x022E0 | 1534 | 20 | 0x0F309 | 3906 |
| 5 | 0x02853 | 976 | 21 | 0x1024B | 10183 |
| 6 | 0x02C23 | 4045 | 22 | 0x12A12 | 6138 |
| 7 | 0x03BF1 | 6277 | 23 | 0x141F4 | 21529 (*) |
| 8 | 0x05476 | 4603 | 24 | 0x142F4 | 6329 |
| 9 | 0x06672 | 4464 | 25 | 0x15BCD | 9589 |
| 10 | 0x077E2 | 1255 | 26 | 0x1832E | 4510 |
| 11 | 0x07CC9 | 1953 | 27 | 0x194CC | 4788 |
| 12 | 0x0846A | 1813 | 28 | 0x1A75F | 7703 |
| 13 | 0x08B80 | 1116 | 29 | 0x1C558 | 10679 |
| 14 | 0x08FDC | 558 | 30 | 0x1EEAF | 30270 (to EOF) |
| 15 | 0x09295 | 558 | 31 | 0x1D5E9 | 6435 (*) |
| 16 | 0x094C3 | 3208 | 32 | 0x1C591 | 4155 (*) |

All loop fields are 0. (*) overlapping/aliased ranges: sample 23 covers samples 24–26 (+311 bytes of 27) as one long
sample; 31 and 32 are sub-ranges of 29; 28/29 and 29/30 overlap by 30/96 bytes. Gaps: 279 unused bytes between 1 and 2,
139 before 15. Durations = length / rate from the table above (e.g. sample 6 at CL=0: 4045/3922 ≈ 1.03 s; sample 30
at CL=1: 30270/4902 ≈ 6.2 s).

---

## 3. game86 ↔ driver

* Driver far pointer at DGROUP `[0x400]` (off) / `[0x402]` (seg). Set in 1000:6065-60a6 from the PSP
  (ES=CS:[0x257]): byte PSP:[0x89] = sound-card code (0xFF = none), word PSP:[0x8A] = offset, word PSP:[0x8C] = segment.
  These are written by the launcher into the command tail it passes to EXEC (`llama.exe` 1000:0289-02d6: card code from
  llama.inf letter A=0/S=0x0C/R=0x18/N=0xFF, offset 0, segment = allocated block). Confirmed one-byte shift: the EXEC
  parameter block at `llama.exe` 1000:0073 points the command tail at CS:0081, and DOS copies it to the child's PSP:0080,
  so launcher bytes 0x8A/0x8B/0x8D land at PSP 0x89/0x8A/0x8C (PSP:0x88 = the K/J flag from llama.inf; the tail text is
  "lm"). If code==0xFF the game installs the
  **dummy driver** `[0x404]/[0x406] = 1000:E785` = `xor ax,ax ; retf` (so AH=6 always reports idle). `[0x688]`/`[0x689]`
  = sound enabled flags; FUN_1000_1514 (sound toggle) swaps `[0x400]` with `[0x404]`.
* **AH=0 is never called by game86** — the launcher inits the driver (`llama.exe` 1000:041f `CALLF [0x8B]`, AL≠0 →
  "Selected Sound Card not found").
* AH=2 from the INT 8 handler (1000:3de4..3edf, hooked at 1000:e519; PIT ch0 divisor 0x5CEC → 50.159 Hz; original
  INT 8 chained every 65536/0x5CEC ticks via `[0x44F]` accumulator). game86 does not hook any vector to the driver.
* Exit (1000:5ed9): wait AH=6 until idle, AH=5, wait idle again, restore INT 8/9, PIT back to 0, AH=1, INT 21h/4Ch.

### Sample requests in game86 (all `MOV AL,n; MOV CL,r; MOV CH,p; MOV AH,3; CALLF [0x400]`, from a byte scan of the exe)

| addr | AL | CL | CH | context |
|---|---|---|---|---|
| 011a | 0x15 | 1 | 2 | game start |
| 01ef | 0x15 | 1 | 0x1E | new wave setup (reached from 0759/07ae too) |
| 0373 | 0x19 | 1 | 6 | score digit carry loop (every 10^4 points?) |
| 041a | 0x16 | 0 | 0x1E | |
| 0654 | 0x06 | 0 | 0x0A | then **busy-wait until done** (065d) |
| 088f | data | data | 2 | title tune player (see below) |
| 091b | 0x1E | 1 | 8 | then **busy-wait until done** (0924) |
| 0ad9, 0bff | 0x0E | 0 | 1 | |
| 0eb0 | obj[+0x3C] lo | obj[+0x3C] hi | 2 | per-enemy sound; +0x3C set to 0x0F, 7, 0x0D, 9, 2, 5, 0x309 (=9 @ rate 3), 0x41E (=0x1E @ rate 4), 0x40B (=0x0B @ rate 4), 0 (silent) at 1232,1324,1d74,1e14,1e76,1ece,1f3a,1f99,1fff,21af,228e,22ec,234a,2407,24f2,25b1,2615,26de,2857 |
| 0f0b | 0x1A | 3 | 4 | |
| 0fb0 | 0x19 | 2 | 0x0A | |
| 0fc9 | 0x10 | 1 | 0x0A | |
| 100a | 0x19 | 2 | 8 | |
| 101d | 0x1C | 2 | 8 | |
| 102a | 0x0D | 0 | 3 | |
| 10ab | 0x0D | 1 | 3 | |
| 116b | 0x17 | 0 | 4 | |
| 12e8 | 0x20 | 3 | 4 | |
| 155d | ([0xB72] & 0x1F), >0x1C → 0x1B | 1 | 2 | only if AH=6 says idle (1540) |
| 1591 | 0x1A | 1 | 0x0A | |
| 161c | 0x11 | 3 | 0x1E | |
| 16bc | 0x14 | 0 | 0x1E | |
| 20f2 | 0x0C | 1 | 4 | |
| 23fd | 0x07 | 1 | 4 | |
| 248c | 0x1E | 2 | 0x0C | only if AH=6 priority (AL) != 0x0C (2479) |
| 259f | 0x0B | 1 | 3 | |
| 2718 | 0x1E | 4 | 4 | |
| 2750 | 0x11 | 3 | 0x0A | |
| 2afc | 0x04 | 1 | 3 | |
| 2dea | 0x07 | 1 | 3 | |
| 30be | 0x11 | 3 | 5 | |
| 3205 | 0x07 | 3 | 4 | |
| 364f | 0x1E | 3 | 4 | |
| 366a | 0x1E | 1 | 6 | |
| 37ee | 0x1D | 3 | 0x1E | |
| 3884 | 0x08 | 0 | 7 | |
| 3b8c | 0 | 1 | 4 | no-op (sample 0 rejected by driver) |
| 3f97 | 0x02 | 1 | 2 | |
| 3fb9 | 0x14 | 0 | 2 | |
| 3fd5 | 0x11 | 1 | 2 | |
| 4443 | 0x1D | 3 | 0x1E | |
| 4450 | 0x11 | 0 | 0x1E | |
| 4a97 | 0x18 | 2 | 6 | |
| 4ceb | 0x1A | 1 | 6 | |
| 4dee | 0x17 | 1 | 3 | |
| 4f36 | 0x08 | 1 | 3 | |
| 5b41 | 0x07 | 4 | 2 | |
| 5bb0 | 0x18 | 1 | 0x0A | |
| 5bdf | 0x0C | 1 | 0x0A | |
| 5bf2 | 0x1A | 0 | 0x0A | |

Priorities used: 1..8, 0x0A, 0x0C, 0x1E (0x1E = "always interrupts"). Which sample is which sound is not identified
here (needs listening); samples 1, 3, 5, 0x0F, 0x12, 0x13, 0x1B(only via 155d/tune), 0x1F are not referenced by a literal.

### Title tune (confirmed)

FUN_1000_083b/084e: per-tick callback `[0x39E4]=0x84E` counts down `[0x629]` from `[0x627]` ticks; on zero reads the
next pair `{rate, sample}` from `[0x62B]` (byte 0 → restart at `[0x62D]`) and requests it with CH=2. Set up at
1000:5c42: sequence at DGROUP 0x9D5 = `01 1B 01 20 01 17 01 1A 00`, period 0x24 = 36 ticks ≈ 0.72 s per note:
samples 0x1B, 0x20, 0x17, 0x1A at rate 1 (≈4902 Hz), looping.

---

## 4. The .cmp files — LZSS-compressed data (confirmed)

Loader FUN_1000_60aa (called from 1000:0064 at startup) → FUN_1000_e65e: INT 21h/3Dh open, 3Fh read 0x400 bytes into
**DS:0000** (the DGROUP header area is used as a 1 KB input window, refilled by FUN_1000_e73d when SI wraps), then
FUN_1000_e6bb decompresses to ES:DI, 3Eh close. Any error → message at DGROUP 0x463 and exit (1000:5f5a).

Decompressor (1000:e6bb) = classic Okumura **LZSS**: 4096-byte ring buffer at **SS:0000-0FFF** (zero-filled, write
position starts at 0xFEE), flag byte read when the 8 flags are used up (bit 0 first); flag 1 → literal byte;
flag 0 → pair `b0,b1`: offset = `(b1>>4)<<8 | b0`, length = `(b1&0xF)+3`, copied from the ring. Special case pair
`00 00`: next byte 0 = end of stream; otherwise that byte is pushed back as the next input byte and a 3-byte copy from
ring[0..2] (zeros) is emitted. The "0xFF separators" seen in the raw files are simply flag bytes meaning "8 literals".
`lzss.py` reproduces this and consumes every file exactly; results:

| file | packed | unpacked | destination | content |
|---|---|---|---|---|
| g3.cmp | 6426 | **18144** | segment CS:[0x261] = 0x30EB (Ghidra block CODE_3 30eb:0000-46df, sized exactly 18144), offset 0 | 16 title-screen shapes |
| f.cmp | 705 | **1710** | DGROUP:0x39EE | proportional bitmap font |
| s.cmp | 840 | **768** | DGROUP:0x4141 | sine tables |
| scores.yak | 160 (raw) | — | DGROUP:0x40A0 (0xA1 requested, must read 0xA0) | high scores; defaults copied from DGROUP 0xAD2 if missing |

### s.cmp (768 bytes, confirmed layout)

* `0x000-0x0FF`: 256-entry **signed 8-bit sine**, amplitude 126 (`0,3,6,…,7E,…,0,FD,…,82,…`). Used as
  `[BX+0x4141]` at 1000:29b3/29c3 (BX and BX+0x40 → sin/cos for a circular formation) and 1000:44fc.
* `0x100-0x2FF`: 256-entry **signed 16-bit sine**, amplitude 0x3FFF (`0000,0192,…,3FFF,…,0000,FF6E,…,C101,…`).
  No reference to 0x4241.. found in the game86 listing — hypothesis: unused by game86 (maybe used by game386.exe).

### f.cmp (1710 bytes, format confirmed from 1000:5394/54f6/5537/559f)

* word @0: 0x000E (hypothesis: line height / font height 14; no reader found).
* `0x02 + ch*2`: u16 offset of glyph relative to the font base (0x39EE); game reads `[0x39F0 + ch*2]` then adds 0x39EE.
  Entries exist for 0x20..0x60 and lower-case a-z map to the upper-case glyphs (`b`..`z` share `B`..`Z`; `a` → glyph
  0x188).
* Glyph: `u16 w0` (always 4, unused by the drawer), `u16 advance` (added to X at 53a0/555a/552c), `u16 rows-1`
  (CX = rows+1 at 559f), `u16 pad`, then (rows) × u16 row bitmaps MSB-first, 1 word wide (e.g. `!` = `E0 E0 E0 F0 F0 F0 00 F0 F0`
  with advance 5, 9 rows). Drawn via the planar VGA path 559f.. (same blitter family as sprites). Space (0x20) is a
  zero-row glyph; 0x20/0x5C/0 terminate strings in 5394 (text-width routine).

### g3.cmp (18144 bytes; format partially confirmed)

* 16 × u16 at offset 0: shape offsets (0x0020, 0x01BC, 0x03EE, 0x0620, 0x0690, 0x06D8, 0x0720, 0x08F8, 0x0B70,
  0x107C, 0x1600, 0x16D4, 0x1870, 0x19A8, 0x1B12, 0x2F0A). Game picks shapes by `ES:[idx*2]` (e.g. ES:[4], [8],
  [0xE/0x10/0x12] = control-method text chosen by `[0x2CF6]` bits 0/1, [0x14/0x16] by bit 4) and draws with
  FUN_1000_42da → FUN_1000_8cbb (the general sprite blitter; in-game sprites come from CS:[0x25F] = segment 0x218C,
  static in the exe).
* Shape header (12 bytes, read at 8cbb): `u16 3` (constant, hypothesis: plane count-1 / type), `u16 cols-1`
  (16-pixel columns minus 1; 0 → fast 1-word path, else 1000:63d2), `u16 rows-1`, `u16 width_px`, `u16 0`, `u16 0`.
  AX/BX passed to 8cbb are the **centre** (x -= width/2, y -= rows/2). Data = rows × cols × **5 words**
  (10 bytes per row per column: confirmed by `MOV AX,0xA; MUL DI` row-skip for top clipping and by every shape's size:
  (size-12)/(rows) = 10*cols). Hypothesis: the 5 words are mask + 4 EGA/VGA planes (planar mode-X-ish blit through
  GC reg 5 at 0x3CE).
* Shapes: #0 19×17, #1/#2 10 rows×77 px (control option texts), #3 4×16, #4/#5 5×10, #6 22×17, #7 30×29, #8 31×53,
  #9 34×51, #10 19×12, #11 19×18, #12 14×19, #13 4×98, #14 84×88, #15 85×96 (rows×px).

---

## 5. Notes for a faithful C port

1. **Single voice, no mixer.** Emulate one DMA voice: 8-bit unsigned PCM, start/stop only, no looping, no volume.
   Resample from the five fixed rates (3922 / 4902 / 7692 / 9615 / 20000 Hz; TC bytes `01 34 7E 98 CE`).
2. **Tick-based latching.** Requests/stops take effect only inside AH=2, which the game calls at **50.16 Hz** (its
   INT 8). Reproduce: `tick()` = apply pending stop (if stop target == current or 0xFF) → if idle and pending sample valid
   (1..32) start it with the *latest* requested rate/priority → clear pending. A request with CH ≥ current priority
   (unsigned) schedules the stop of the current sample at the next tick so the new sample starts on that tick;
   lower-priority requests wait (single slot, latest overwrites) and start on the first tick after the current one ends.
3. **Status matters for game flow.** The game busy-waits on AH=6 (`AH == 0`) after sample 6 (1000:065d), after
   sample 0x1E (1000:0924) and at exit (1000:5eef/5f18); it also suppresses retriggers using AH=6 (1000:1540 idle test,
   1000:247d "priority already 0x0C" test). So sample *durations* (length/rate) and the "current priority" value (which
   is **not** cleared when a sample finishes) must be modelled, or those waits must be stubbed. With the dummy driver
   (sound off / no card) AH=6 returns AX=0 instantly.
4. AH=2's return value and the driver's IRQ counter are unused by the game. AH=1 blocks until silence (the game already
   waits first).
5. Sample table lives in the driver, not in the .spl: use the 32 entries above (index AL-1) and ignore entries 0/1 and
   the loop words. Note the overlapping entries (23 spans 24–26; 31/32 inside 29) — copy them as given.
6. The .cmp files must be LZSS-decoded (algorithm above) at load; f.cmp → font (proportional, 1-word-wide rows),
   s.cmp → 8-bit sine (first 256 bytes) [+ probably unused 16-bit sine], g3.cmp → 16 title shapes in the 5-words-per-
   column planar sprite format shared with the in-exe sprite bank.
7. Sound-card selection comes from the launcher via the PSP tail (byte 0x89: 0=AdLib `adlib.bin`, 0x0C=SB `sblaster.bin`,
   0x18=Roland `roland.bin`, 0xFF=none). Only sblaster.bin ships in this copy; `llama.inf` here is `V K N 0 N` (N = no sound).
