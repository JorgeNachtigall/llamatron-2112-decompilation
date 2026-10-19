# Llamatron 2112 (PC, 1992) — reverse-engineering port plan

Shared reference for everyone (humans and agents) working on the port. Companion notes:
`docs/re-notes-game86.md` (game + launcher), `docs/re-notes-fs-install.md` (title viewer, installer,
llama.inf), `docs/re-notes-sound-data.md` (sound driver, llatron.spl, *.cmp formats).
Disassembly listings live in `re/listings/*.lst` (local only, never committed).

## 0. Goal and non-negotiables

1. **Readable C11 reimplementation of the original logic, exactly** — same behaviour, timing
   (50.16 Hz logic tick), RNG sequence, scoring, enemy patterns. Every reimplemented routine cites the
   original address: `/* game86 1000:1d40 */`.
2. **Repository contains zero proprietary content**: no graphics, samples, music, level/enemy tables,
   strings, palettes or copied byte blobs; no disassembly listings. Only our code, format/offset
   descriptions, and hashes of the original files.
3. **Build-time extraction**: `cmake -DLLAMATRON_ORIGINAL_DIR=/path/to/game` runs our extractor on the
   user's own copy (hash-verified), unpacking the EXEs and writing everything the game needs into
   `build/generated/` (data files and generated C). The resulting binary / web bundle is
   self-contained and does **not** need the original folder at runtime. Because it embeds the user's
   assets it is for **personal use only and must never be redistributed or published**.
4. **Two targets from one code base**: native (macOS arm64 first, SDL2) and **browser** (Emscripten,
   SDL2 port, canvas/WebGL, Web Audio via SDL audio). No JS rewrite, no three.js: the logic exists once.
5. Generating assets from scratch is explicitly not the goal.
6. **A real reimplementation, never an emulator wrapper** (top priority, set by the user). See §0.1.

### 0.1 What "real reimplementation" means (binding for every workstream)
- **No CPU emulation in the game.** The shipped binary (native and web) never executes original
  machine code: no unicorn, no x86 interpreter, no "hybrid" mode, no fallback to original routines.
  unicorn exists **only** in `tools/harness/` as a test oracle and is never linked into `src/`.
- **Idiomatic, editable C.** Game code is written as normal C: named structs (`Entity`, `Player`,
  `Bullet`, `Level`), enums (`EnemyType`, `GameState`), meaningful function names, local variables.
  Forbidden in `src/game/`: register variables (`ax`, `bx`, …), flag emulation, segment:offset
  arithmetic, raw `mem[0x48C1]`-style accesses, `goto`-translated asm, copies of self-modifying code.
  A routine is translated by understanding **what** it does, then writing that in C.
- **No hardware emulation as the game's API.** The game draws through a small renderer API
  (`gfx_draw_sprite`, `gfx_restore_rect`, `gfx_print`, `gfx_set_palette`, page flip) onto an indexed
  320x200 framebuffer — not by poking EGA registers. Same for sound (`snd_play(sample, rate,
  priority)`) and input (`input_state()`). Hardware semantics that affect *visible results* are
  reproduced inside those APIs, not exposed to game code.
- **Structured data, not memory images.** The extractor converts the original's tables into typed
  data (sprite frames with sizes/hotspots, level scripts as instruction lists, enemy parameter
  tables, palettes, strings, font glyphs). The game loads that, so new levels, enemies or sprites can
  be added later by editing data or code, without matching original byte layouts.
- **Exactness is proven by tests, not by mimicking the machine.** `tests/` contain adapters that
  translate between our structs and the original's byte layout (`state_to_dgroup()` /
  `state_from_dgroup()`); only the test harness knows original offsets. Game structs stay free to
  evolve; when a mod intentionally changes behaviour, the affected tests are marked as deviations.
- Each C function still cites its original address(es) in a comment, for traceability only.

## 1. What we know (investigation results)

Status tags: **[C]** confirmed by reading code, **[H]** hypothesis. Addresses are Ghidra addresses
(image base segment 1000; game86 DGROUP = 3559). Details and more addresses in the re-notes.

### 1.1 Program flow between the executables [C]
- `llama.exe` reads `llama.inf` (9 chars + NUL: `V J A 0 N` pattern: [0] V/E video, [2] J/K control,
  [4] S/A/R/N sound, [6] '0' = 8086 build (else game386.exe, not shipped), [8] Y/P/N flash mode),
  allocates one 158,720-byte block, loads the sound driver (`sblaster.bin`, 1895 B) at `seg:0000`
  and `llatron.spl` (156,813 B) immediately after it at `seg:0767`, calls the driver `AH=0` (init),
  then execs **`fs.exe`** (title picture) and afterwards **`game86.exe`** (`llama 1000:0103`).
- Child processes get their configuration through the **command tail**: child `PSP:81h='l'`,
  `82h='m'`, `84h`=cpu digit, `85h`=flash mode (P=1,Y=2,N=0), `86h`=VGA flag, `88h`=joystick,
  `89h`=sound type (0 AdLib, 0Ch SB, 18h Roland, FFh none), `8Ah..8Dh`=far pointer to the resident
  driver. game86 refuses to run (exit code 0FDh) without the `'l'` marker (`game86 1000:5f8e`).
- `fs.exe` = front screen only: VGA -> mode 13h unchained to 320x400 showing `newtit.lbm` over
  `newtit2.lbm`; EGA -> mode 10h with `tronega.lbm` (640x350x16). Plays driver sample 1Bh and waits
  for a key / joystick button; always exits 0. It never returns 0FDh.
- `install.exe` writes `llama.inf` and the two .BAT files, copies 13 files. The shipped
  `llama.inf` = "V K N 0 N" (VGA, keyboard, no sound, 8086, no flashes/pulsing).
- `game86.exe` talks to the sound driver via `CALLF [DS:0400]` with AH = function (driver API fully
  RE'd, see `re-notes-sound-data.md`): 0 init/detect (only llama.exe calls it; fixed base 220h, IRQ
  autodetect), 1 shutdown, **2 tick** (called from the game's INT 08 handler at 50.16 Hz: applies
  pending stop, promotes the pending request, starts the DMA transfer), **3 request** (AL = sample
  1..32, CL = rate index 0..4 -> DSP time constants 01/34/7E/98/CE = ~3922/4902/7692/9615/20000 Hz,
  CH = priority; pre-empts the current sample if CH >= its priority, else waits in a single pending
  slot), 4 stop sample AL, 5 stop all, 6 status (AH = sample playing or 0, AL = sticky priority of
  the current/last sample). **Single voice, 8-bit unsigned mono, no mixing, no looping**; start/stop
  are latched to the tick. The game busy-waits on AH=6 at `1000:065d`, `0924`, `5eef`, `5f18` and
  gates re-triggers at `1000:1540`/`247d`, so sample durations and the sticky priority are part of
  the game's timing. With sound type FFh the game points `[0400]` at an internal stub
  (`1000:e785`, always idle) and `1000:1514` swaps the two to toggle sound at runtime.
- `llatron.spl` is headerless 8-bit PCM; the **34-entry sample directory lives inside sblaster.bin at
  offset 0x609** (u32 offset, u16 length, u16 loop start/len — loops unused), entries overlap in
  places. 60 `CALLF [0400]` sites are tabulated in the sound notes; the title tune is a 4-note list at
  `DS:09D5` played every 0x24 ticks by `1000:084e`.

### 1.2 game86 runtime model [C]
- **Video**: INT 10h mode **0Dh (320x200x16, planar EGA)** on both EGA and VGA. VGA only changes the
  palette path (DAC via 1012h with 12-bit ST-style palette words converted by table `DS:2134`) vs EGA
  (1002h with a 17-byte table). Three VRAM pages: 0x0000/0x2000 flipped via CRTC start address,
  **0x4000 = clean background** used to erase sprites (dirty-rectangle lists kept in the code
  segment, 96 unrolled restore routines at `1e7b:031c`). Rendering uses GC set/reset, bit mask,
  read-map select, SEQ map mask (~147 `OUT DX,AX` sites). Sprites are centred, clipped, bit-mask
  blitted; shifted variants for x%8!=0 (`1000:aa29/ac03`); scaled/mirrored draws (`1000:63d2`).
- **Timing**: PIT ch0 reprogrammed to divisor **0x5CEC (50.1590 Hz = 1193181.8 Hz / 23788; "50.16 Hz" elsewhere is this value rounded)**; the INT 08 handler
  (`1000:3de4`) calls the driver tick, reads fire buttons, does palette pulsing, and calls the
  **per-tick game hook `[0x39e4]`** — in play this is `1000:1562`, i.e. the entire simulation runs in
  the timer interrupt at 50 Hz. The frame loop (`1e7b:013f`) restores backgrounds, calls the
  **per-frame draw hook `[0x39e0]`** (play: `1000:1a3b`), flips pages and waits for vertical retrace
  (60 Hz EGA / 70 Hz VGA). Chains to BIOS INT 08 at the correct 18.2 Hz rate.
- **Input**: own INT 09 handler (`1000:e567`) reads port 60h: cursor keys -> direction bits 0..3 of
  `[0x588]`, Space -> fire bit 7; last make code in `[0x586]`: 'P' (0x19) pause, 'Q' (0x10) quit, 'X' (0x2D)
  aborts the current wave (`[0x810]=3`). Joystick: port 201h polled with calibration thresholds (centre*3/4, centre*3/2) for two
  sticks; buttons in bits 4..7.
- **RNG**: init-time mixer `1000:e616` seeded from the BIOS tick count (0040:006C) shuffles a 512-byte
  permutation table at `DS:6AC9`; gameplay randomness is `1000:e4de` = next byte of that table.
  Exact reproduction therefore requires an injectable seed (BIOS tick value) in the port.
- **Data**: `g3.cmp` -> segment 30EB (pictures), `f.cmp` -> `DS:39EE` (font), `s.cmp` -> `DS:4141`
  (sine table); all three are **LZSS** (4 KB window, 12-bit offsets, 4-bit length, decoder at
  `1000:e6bb`). Sprite bank (60 KB) is **embedded in game86.exe at segment 218C**, level scripts
  (100 level scripts via table `DS:2BDA..2CA1`, byte-code with 8 opcodes 0xFF..0xF8 (W1 finding), enemy (type,count) pairs, 41 enemy init handlers via
  `DS:2CA4`), palettes, text and the default hi-score table are in DGROUP (3559). High scores:
  `scores.yak` = 10 x (8 ASCII digits + 8 chars), 160 bytes, defaults from `DS:0AD2`.
- **Entities**: 70 x 64-byte records at `DS:48C1`; think handlers by type (`DS:1FD2`), animation /
  state handlers (`DS:202E`), hit handlers (`CS:0C04`). Players at `[0x2cfb..0x2d01]`, two
  128-entry bullet rings. Everything is 16-bit integer arithmetic (no FPU).

### 1.3 Still open / hypotheses
- [H] `s.cmp`'s second table (256 x s16 sine at `DS:4241`) and the dead loop code in the driver appear
  unused by game86 — confirm by search before dropping them from the extractor.
- [H] A few code holes remain in game86 (`1000:0daa`, `12c6`, `31d8`, `37c2`, `495f`, ~5 KB total),
  reached through tables not yet followed. ~84 % of the code segment is disassembled; all hooks,
  handlers and dispatch tables listed in the notes are confirmed.
- [H] Whether the original's interrupt-vs-frame race ever changes visible behaviour (see §3.3).

## 2. Architecture of the port

### 2.1 Directory layout
```
CMakeLists.txt, CMakePresets.json       # presets: macos-native, web-emscripten, (linux-native)
cmake/Extract.cmake                     # configure/build step running the extractor
tools/
  extract/        C11, no deps: hash check, unlzexe, unexepack, LZSS, sprite/table/picture/sample
                  extraction, ILBM decode -> build/generated (also usable as a CLI for debugging)
  manifest/       original-files.json: name, size, SHA-256 of the supported release(s)
  ghidra/         headless scripts (Disasm.java, Export.java, ApplySymbols.java, DumpTable.java)
  harness/        differential-test harness (Python + unicorn; later C mini-emulator), DOSBox-X scripts
  unlzexe.py, unexepack.py (existing reference implementations)
src/
  platform/       SDL2 window/texture, audio device, input, file/scores storage, main-loop driver
                  (native loop vs emscripten_set_main_loop); no game knowledge
  engine/         game-facing services (no hardware registers exposed): gfx.c (indexed 320x200
                  framebuffer, 3 pages incl. clean background, sprite/rect/text drawing, palette,
                  page flip, EGA vs VGA palette conversion), timing.c (50.16 Hz tick scheduler),
                  input.c (keys/joystick -> the game's direction/fire/command state), snd.c
  assets/         loads the typed data produced by the extractor (sprites, levels, enemy tables,
                  font, palettes, strings, samples)
  game/           reimplemented game86 logic as idiomatic C, one file per subsystem
                  (state.h with GameState/Entity/Player, init.c, main_sm.c, script.c, tick.c,
                  frame.c, player.c, enemies/*.c, hud.c, hiscore.c, attract.c); each function
                  cites its original address in a comment
  sound/          reimplementation of the sblaster.bin driver behaviour (sample scheduler, priorities,
                  tick) + mixer to the SDL audio stream; title.c (front screen like fs.exe)
docs/             PLAN.md, re-notes-*.md, funcmap.csv (orig address -> C symbol -> status)
re/               LOCAL ONLY (gitignored): listings, symbols.csv, scratch RE output
tests/            unit + differential tests (ctest), golden hashes of VRAM frames (hashes only)
```

### 2.2 Build pipeline (build-time extraction)
1. `cmake -S . -B build --preset macos-native -DLLAMATRON_ORIGINAL_DIR=~/games/llamatron`
   (`LLAMATRON_ORIGINAL_DIR` is required; configure fails with a clear message if missing).
2. Configure step runs `tools/extract --verify`: computes SHA-256 of `game86.exe, fs.exe, llama.exe,
   install.exe, llatron.spl, sblaster.bin, f.cmp, g3.cmp, s.cmp, newtit.lbm, newtit2.lbm, tronega.lbm`
   and compares against `tools/manifest/original-files.json`. Mismatch => fatal error naming the file,
   expected/actual hash and size ("unsupported version; the port is derived from release X"). An
   `-DLLAMATRON_ALLOW_UNVERIFIED=ON` escape hatch exists for RE work only and prints a loud warning.
3. Build step (custom command, depends on the extractor sources and the manifest) produces
   `build/generated/`:
   - `exe/game86.unpacked`, `exe/fs.unpacked`, ... (LZEXE 0.91 then EXEPACK removed; relocations
     applied for load segment 0x1000 so Ghidra addresses == image offsets + 0x10000);
   - `data/sprites.bin` (segment 218C), `data/g3.bin` (16 title shapes, 12-byte headers),
     `data/font.bin` (proportional font: u16 height, glyph offset table, {4,advance,rows-1,pad,rows x u16}),
     `data/sine.bin` (256 x s8 amp 126, then 256 x s16) — all three LZSS-decoded,
     typed tables decoded from DGROUP (`levels.dat` as instruction lists, `enemies.dat` parameter
     tables, `palettes.dat`, `strings.dat`, `default_scores.dat`; formats defined by our own headers
     in `src/assets/`, so they can be extended later), `data/samples.bin` +
     `samples.idx` (from llatron.spl using the driver's table layout), `data/title_vga.idx8 + .pal`,
     `data/title_ega.idx4 + .pal` (decoded ILBMs);
   - `generated/asset_index.h` (sizes/offsets/hashes) and, for the embedded variant,
     `generated/assets_blob.c` (arrays) so the native binary needs no files next to it;
   - `generated/extract.stamp` with the SHA-256 of every output.
4. **Determinism**: fixed processing order, no timestamps or absolute paths in outputs, outputs
   byte-identical across hosts (checked in CI by hashing `extract.stamp` against a committed hash of
   *hashes* — hashes of derived data are not proprietary content). The extractor is pure C11 with no
   third-party dependency so native and Emscripten builds use the same tool (built for the host).
5. Web preset: `emcmake cmake --preset web-emscripten ...`; assets bundled with
   `--preload-file build/generated/data@/data` (or `--embed-file`), output `llamatron.html/.js/.wasm/.data`
   into `build-web/`. Nothing under `build*/` is ever committed or hosted publicly.

### 2.3 Video: high-level renderer on an indexed framebuffer (decided, §0.1)
The game draws through `src/engine/gfx.c`: three 320x200 8-bit indexed pages (two displayed
alternately + the clean background used to erase sprites, like the original's 0x0000/0x2000/0x4000),
operations `draw_sprite(frame, x, y, flags)` (clipping, mirroring, scaling as the original does),
`restore_rect`, `fill_rect`, `print_text`, `set_palette`, `flip`. Each operation is written from an
understanding of the original blitter's *result* (which pixels end up which colour), not by
replaying its register writes. Exactness: the harness runs the original blitter in unicorn with a
Python EGA-plane model, rasterises the resulting VRAM to indexed pixels and compares with our page
(tests/adapters convert our page <-> planes). EGA-specific effects that are visible (e.g. colour
combination via set/reset, palette pulsing) are reproduced in `gfx.c` semantics and covered by
those tests. This keeps the renderer easy to extend (higher resolutions, new sprites, overlays).

### 2.4 Timing: deterministic PIT-tick scheduler
- The original has two asynchronous clocks (50.16 Hz PIT interrupt running the simulation; vertical
  retrace 60/70 Hz running drawing). The port models time explicitly: `pit.c` keeps a 64-bit
  accumulator of 1,193,182 Hz PIT cycles; every frame callback advances by one display period
  (1/60 s for the EGA config, 1/70 s for VGA, selectable) and runs `tick_handler()` for each elapsed
  divisor period (0x5CEC), then runs the frame routine (background restore, draw hook, page flip).
  This is `emscripten_set_main_loop`-friendly (no blocking waits, no threads; vsync wait becomes
  "return from callback"). Native uses the same callback driven by SDL's present/vsync or a
  fixed-step timer.
- Deliberate, documented deviation: the original interrupt can pre-empt drawing mid-frame; the port
  runs ticks only at frame boundaries. Simulation results do not depend on the draw code, so logic
  is unaffected; only which frame shows which state can differ by one frame. §3 tests will confirm
  nothing else depends on it (e.g. the `[0x428]` guard and `[0x429]` "first frame drawn" flag).
- The BIOS tick count read at init (`0040:006C`) is an input of the game (RNG seed); the port
  exposes it as a startup parameter (default: derived from wall clock like the original, overridable
  for tests/replays).

### 2.5 Sound
- Reimplement the **behaviour** of `sblaster.bin` (Sound Images Generation 2 driver) as
  `src/sound/sidriver.c`: the AH-function API (§1.1), one voice, 8-bit unsigned mono, five fixed
  rates (TC table 01/34/7E/98/CE), the pending slot + priority pre-emption rule, and tick-latched
  start/stop (AH=2 from the 50.16 Hz tick). Sample durations are computed exactly as
  `length / rate`, so `AH=6` reports busy/idle at the same tick the real DMA transfer would have
  ended (the IRQ handler marks completion; the port completes at the tick containing the end time).
  The voice output is resampled into an SDL audio stream (`SDL_QueueAudio` from the frame callback;
  works identically under Emscripten). AdLib/Roland drivers are not shipped and are out of scope.
- Timing coupling to the game is reproduced by construction: busy-waits on AH=6 (`1000:065d`,
  `0924`, exit) and re-trigger gates (`1540`, `247d`) read the same emulated status/priority.
- The extractor takes the sample directory from `sblaster.bin` offset 0x609 (34 entries, index =
  sample#+1) and cuts `llatron.spl` into `samples.bin`/`samples.idx`; overlapping entries are kept
  as-is, loop fields ignored (dead in the driver).
- Sound is optional at runtime (sound type FFh path with the stub driver) — the shipped config is
  "no sound", so M2/M3 can ship before sound is done.

### 2.6 Input
- Keyboard: SDL scancodes -> PC set-1 make/break codes -> the reimplemented INT 09 logic
  (`kbd_on_scancode()` sets bits of `[0x588]`, `[0x586]`). Keeps the original quirks (e.g. the fire
  bit is cleared by the timer each tick and re-read; last-make-code semantics for P/Q/-).
- Joystick/gamepad: SDL GameController. To preserve the original asymmetric dead zones we synthesise
  the 201h "count" the original would have measured: `count = centre * (1 + axis)` and apply the
  calibrated thresholds (`<= centre*3/4` -> negative, `>= centre*3/2` -> positive) exactly as
  `1e7b:0082..0134`; buttons map to bits 4..7. Two controllers = two sticks (2-player and
  double-controller modes). Web: SDL's Gamepad API backend.
- Config: `llama.inf` semantics are kept but exposed as port options (video EGA/VGA palette path and
  refresh, flash mode Y/P/N, control J/K, sound on/off); the extractor also imports the user's
  `llama.inf` and `scores.yak` as defaults if present.

### 2.7 High scores and persistence
- Native: `scores.yak`-compatible 160-byte file in the platform's user-data dir (SDL_GetPrefPath),
  seeded from the user's original `scores.yak` at build time if present.
- Web: same bytes stored via IDBFS (mounted at `/persist`, synced after each write) with a
  localStorage fallback. Format stays identical so files can be moved between targets.

## 3. Methodology for "exact logic"

### 3.1 Game state vs. original layout (superseded by §0.1)
Game state is ordinary C (`GameState` with `Entity entities[70]`, `Player players[2]`, bullet rings,
level/script state, RNG table…), designed for readability, not byte layout. The original's memory
layout lives only in `tests/adapters/dgroup_map.c`, which converts `GameState` <-> DGROUP bytes
(and indexed framebuffer <-> planar VRAM) so the harness can compare against the original. There is
**no hybrid mode**: unported features are simply not available yet (§6), never emulated.

### 3.2 Differential testing against the original code (build first)
- `tools/harness/`: Python + **unicorn-engine** (pip in a venv; later optionally a small C 8086 core
  for CI speed) loads `build/generated/exe/game86.unpacked` at linear 0x10000, sets up DS/SS/ES,
  stubs INT 21h/10h and port I/O (port writes recorded into a log; VRAM at A000 as plain memory with
  a Python model of the GC/SEQ registers), and can call any routine with given registers.
- A test = { routine address, input state } -> the harness builds the original's memory (DGROUP
  bytes + VRAM + CS vars) from the same `GameState` via `tests/adapters`, runs the original routine
  in unicorn and our C function on the `GameState`, converts both results to a common form and
  compares game state and rendered pixels (registers only where they carry a return value). Generators produce random but valid states (e.g. random
  entity tables) so each ported function is fuzzed against its original.
- Order of attack: pure functions first (LZSS decoder, sprite blitters, text renderer, rect fill,
  restore routines, RNG, score compare), then handlers (enemy init/think/hit), then the tick and
  frame hooks, then the state machine.

### 3.3 Whole-game reference runs with DOSBox-X
- `brew install dosbox-x`; scripted runs with a fixed config (machine=ega and machine=vgaonly), a
  patched RNG seed (set the BIOS tick at 0040:006C before launching, or run via the debugger and poke
  memory), deterministic input via DOSBox-X's input recording / `autoexec` key injection, and the
  debugger's `MEMDUMPBIN` to dump DGROUP + VRAM every N frames (trigger on the page-flip write to
  3D4h/0Ch). The port replays the same input recording with the same seed and compares DGroup bytes
  and rasterised frames (hashes of frames are stored in tests/, never the frames themselves).
- Audio check: compare the sequence of driver calls (function, AL/CL/CH, tick number) logged by both.

### 3.4 Acceptance per function (tracked in `docs/funcmap.csv`)
`program, orig_addr, size, name, c_symbol, file, status(todo|ported|diff-tested|run-verified), owner, range, notes`.
A function is "done" only when diff-tested; a milestone is done when its run-verified set passes.

## 4. RE / decompilation workflow for hand-written asm
- **Headless Ghidra only** for automation (`ghidra/headless/LlamatronCLI.gpr`); the GUI project is the
  user's. Scripts (`tools/ghidra/`): `Disasm.java` (disassemble + create functions from an address
  list incl. `table seg:off count` entries — already used for 3 passes), `Export.java` (listing),
  `ApplySymbols.java` (reads `re/symbols.csv`: `program,address,name,type(func|data|label),comment`
  and applies labels/comments/function names), `DumpTable.java`. `re/listings/*.lst` are regenerated
  after each pass; a `make re-export` target wraps this.
- **Symbols file** `re/symbols.csv` is the single source of truth for names; agents append rows for
  their address range only; the GUI project imports it with the same script (run by the user), so
  names never conflict between headless and GUI.
- Naming: data `g_<meaning>` (e.g. `g_tick_count` for `[0x39e6]`), code `<subsystem>_<verb>` with the
  original address kept in the C comment, enemy types by their on-screen identity once known
  (`enemy_llama_init`), unknowns `unk_1000_1234`. Keep Ghidra's `FUN_1000_xxxx` as alias.
- Address-range ownership for annotation (so agents don't collide):
  A: `1000:0000-0980` (state machine, script), B: `1000:0981-1d40` (tick: player, bullets, collisions),
  C: `1000:1dcd-3d09` (41 enemy init handlers), D: `1000:3de4-4650` (IRQ, attract, think handlers),
  E: `1000:4650-6600` (entities/animation, hi-score, text, VRAM utils), F: `1000:6566-e4de` +
  `1e7b:*` (blitters, restore routines), G: DGROUP data layout (`3559:*`), H: sound driver.
- Coverage improvement recipe: find `CALL/JMP reg` and `CS:[BX+tbl]` sites, dump the table with the
  Python offset helper, extend the targets file, rerun `Disasm.java`; clear the mis-disassembled
  sprite bytes in 218C; mark 30EB as BSS; define DGROUP tables as data with the correct types.

## 5. Browser target specifics
- Emscripten via `brew install emscripten`; preset `web-emscripten` uses `-sUSE_SDL=2`, `-sASYNCIFY`
  **not** needed because the main loop is callback-based from day one (`emscripten_set_main_loop
  (frame_cb, 0, 1)` with `0` = requestAnimationFrame; the PIT scheduler converts real elapsed time
  into ticks with a cap to avoid spiral-of-death; a fixed 60/70 Hz step is available for replays).
- Audio: SDL2 audio device opened with 2048-sample buffer; on the web the device starts after the
  first user gesture (title screen "press fire" doubles as the gesture).
- Input: keyboard events + SDL_GameController (Gamepad API). Scores: IDBFS. Assets: `--preload-file`.
- A `web-smoke` CI job builds the wasm and runs a headless Chromium screenshot of the title screen
  (hash compared) so web regressions surface early.

## 6. Roadmap (each milestone runnable)
| M | Deliverable | Est. size |
|---|-------------|-----------|
| M0 | Repo skeleton, .gitignore, CMake presets (native + web), `tools/extract --verify` with manifest + C unlzexe/unexepack (validated byte-for-byte against the Python tools), funcmap.csv seeded from the listings, symbols.csv + ApplySymbols | ~1.5 kLOC C, 2–3 agent-days |
| M1 | Extractor completes (LZSS, sprites, DGROUP, pictures, font, sine, samples, ILBM); SDL window shows the decoded title LBMs (fs.exe behaviour, both VGA 320x400 and EGA 640x350 variants); **same page running in the browser** | ~1.5 kLOC, 2–3 days |
| M2 | `engine/gfx.c` renderer API + palette, tick scheduler, input mapping; unicorn harness (with its Python EGA-plane model, test-only); blitters/text/rect/restore routines ported and diff-tested; attract/title screen reproduced from reimplemented init + attract hooks (`3ee0`, `402c`, `3f6f`, `44d7`) | ~3 kLOC, 1 week |
| M3 | Native bring-up: state machine, level-script interpreter, tick/frame logic, player, bullets and the enemy types of the first waves ported (pure C); levels whose enemies are not yet ported are skipped; first playable waves | ~3 kLOC, 1–2 weeks |
| M4 | All 41 enemy init + think/animation/hit handlers ported and diff-tested; all 100 level scripts playable; game over, hi-score entry, scores persistence | ~4 kLOC, 2 weeks |
| M5 | Sound: driver behaviour + sample mixer; title tune/samples on native and web | ~1 kLOC, 1 week |
| M6 | DOSBox-X reference runs: recorded inputs, frame-hash and DGROUP comparisons for EGA and VGA configs, fixes for every divergence; web build parity (gamepad, IDBFS) | 1–2 weeks |
| M7 | Polish: options UI mapping llama.inf semantics, window scaling/aspect (320x200 -> 4:3), pause/quit, app bundle; documentation of every deliberate deviation | 1 week |

## 7. Parallel workstreams (separate Maestri terminals)
Each workstream owns a directory (or an address range) and a section of `docs/funcmap.csv`; nobody
edits another workstream's files; shared headers (`game/state.h`, `asset_index.h`, `engine/gfx.h`) change only
via the Integration lead with a note in `docs/CHANGELOG-shared.md`.

| WS | Owner area | Inputs | Outputs | Depends on |
|----|-----------|--------|---------|-----------|
| W1 Build & Extractor | `CMakeLists.txt`, `cmake/`, `tools/extract/`, `tools/manifest/` | formats in re-notes; existing Python unpackers | `build/generated/*`, `asset_index.h`, presets native/web | — (starts first) |
| W2 Platform & engine | `src/platform/`, `src/engine/` | §2.3–2.6 | `gfx.c/h` renderer API, `timing.c`, `input.c`, SDL + Emscripten main loop, title screen (M1) | W1 for assets (can stub) |
| W3 Test harness | `tools/harness/`, `tests/`, DOSBox-X scripts | unpacked EXE, listings | unicorn runner, state generators, ctest integration, frame-hash tooling | W1 (unpacked image) |
| W4 Game core (ranges A, B, D) | `src/game/{init,main_sm,script,tick,frame,player,bullets}.c` | listings, re-notes | ported + diff-tested functions | W2 API, W3 harness |
| W5 Enemies (range C + think/hit tables) | `src/game/entities/*.c`, one file per enemy type | `DS:2CA4/1FD2/202E`, `CS:0C04` tables | 41 init + handlers | W4's entity struct (agree layout first) |
| W6 Rendering & text (ranges E, F) | `src/game/draw_*.c`, `text.c`, `hiscore.c` | blitter listings | diff-tested blitters, restore routines, text engine | W2 ega API |
| W7 Sound | `src/sound/`, driver RE notes | sblaster.bin disassembly, llatron.spl | driver behaviour + mixer, sample index extractor piece (handed to W1) | W1 (samples) |
| W8 RE annotation | `re/symbols.csv`, headless scripts, re-notes | listings | names/comments per range, closing code holes, DGROUP layout map for `tests/adapters/dgroup_map.c`, meanings for `game/state.h` | — |
| W0 Integration lead | shared headers, funcmap, PLAN upkeep, reviews | all | merged tree, milestone sign-off | — |

Sequencing: W1, W2, W3, W8 start immediately in parallel. W6 starts once `engine/gfx.h` exists (day 2–3).
W4 starts after the harness can run a single routine. W5 starts when W4 has fixed the entity struct
(it is the first thing W4 does). W7 is independent after W1 provides `samples.bin`. M3 native
bring-up is sequential (W0 + W4). M6 reference runs are W3 + W0 with everyone fixing their ranges.

## 8. Risks and how to retire them early
1. **Interrupt/frame race** (§2.4): verify in M3 with DOSBox-X that the canonical ordering yields
   identical DGROUP traces; if not, add a mid-frame tick point matching the measured pattern.
2. **Driver RE incomplete** (sample rate, mixing, priorities): sound is isolated (W7) and the shipped
   config has no sound; log-level comparison of driver calls keeps logic exact regardless.
3. **Version drift**: only the release we have is supported; the manifest makes this explicit. If
   other releases exist (game386.exe, registered version), add manifests later; never guess offsets.
4. **Hidden code** in remaining holes: closed by W8 in week 1 using the table-following recipe; the
   M3 only needs the code paths of the first waves; remaining holes are closed before their features are ported.
5. **Hand-written asm idioms** (flag tricks, self-modifying code at `CS:0x582d`, `CS:0x5362`,
   `CS:0x559c/0x559e`, `CS:0x672f` variables): the diff harness catches mistranslations; document each
   idiom in `docs/asm-idioms.md`.
6. **Emscripten pitfalls** (audio unlock, IDBFS sync, performance): M1 already ships the web build;
   the PIT scheduler never blocks.
7. **IP leakage into the repo by accident** (a pasted table, a listing): pre-commit hook rejects
   files under `re/`, `build*/`, `original/`, `unpacked/`, binary blobs > 4 KB, and lines matching
   known original strings; code review checks that generated headers are only included from
   `build/generated`.
8. **RNG seed reads the live BIOS tick** (found by W3): the init shuffle (`1000:007b..00ab`) calls
   the mixer `1000:e616` 8000 times, and each call re-reads the BIOS tick word `0000:046C`, which can
   change during the shuffle on real hardware. The tick word becomes the mixer's new `s2` on every call
   (XORed with `s3` first when it equals the old `s2`), so the table depends on the whole schedule of
   8000 tick reads, not one number (details: tests/README.md). Oracle tests hold it constant; DOSBox-X reference runs must pin it the same way.

## 9. Legal / IP hygiene rules
- Committed: our C/CMake/Python/Java code, format descriptions (offsets, sizes, struct layouts),
  SHA-256 hashes of original and generated files, this documentation.
- Never committed: `original/`, `unpacked/`, `re/` (listings, symbols with copied strings are fine
  but keep them local anyway), `build*/`, `*.lst`, `*.gpr/*.rep`, any `.bin/.lbm/.spl/.cmp/.yak`,
  screenshots/frames of the game (hashes only), extracted strings or tables in source.
- `.gitignore` (initial): `original/ unpacked/ re/ build/ build-*/ *.lst *.gpr *.rep *.lock
  ghidra/ *.bin *.lbm *.spl *.cmp *.yak *.exe .venv/ .DS_Store`.
- Source citing addresses and describing behaviour is fine; copying text strings is not: strings the
  game prints are read from the extracted DGROUP image at their original offsets.
- README states plainly: the port is a clean-room-by-disassembly reimplementation for owners of the
  original; built artifacts contain the owner's assets and are for personal use; do not distribute
  or publish builds (including the web bundle).
