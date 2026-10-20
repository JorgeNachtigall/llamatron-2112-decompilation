# Llamatron 2112 port

A C11 reimplementation of Llamatron 2112 (Llamasoft, 1992, MS-DOS), built natively (macOS, SDL2)
and for the browser (Emscripten). See `docs/PLAN.md` for goals, architecture and status.

## Personal use only

This repository contains no game data. Every build extracts the graphics, sounds and tables from
**your own copy** of the original game (`-DLLAMATRON_ORIGINAL_DIR=...`, hash-verified) and embeds
them in the binary, the `.app` bundle or the web `dist/` folder. Those outputs therefore contain
the original's assets: they are for **personal use only and must never be distributed,
uploaded or published**. Details: `src/platform/README.md`.

## Build

### What you need

- **Your copy of the original game**, as a folder holding these files (lower case):
  `llama.exe`, `game86.exe`, `fs.exe`, `install.exe`, `f.cmp`, `g3.cmp`, `s.cmp`,
  `llatron.spl`, `newtit.lbm`, `newtit2.lbm`, `tronega.lbm` and `sblaster.bin`. The build
  checks each one's hash (`tools/manifest/original-files.json`). `llama.inf` (settings) and
  `scores.yak` (high scores) are optional: they are read if present.
  Either put the folder's files in `<repo>/original/` (the default, gitignored), or name the
  folder with `-DLLAMATRON_ORIGINAL_DIR=/path/to/llamatron` on every configure. Instead of `-D`,
  you can set the environment variable once (`export LLAMATRON_ORIGINAL_DIR=/path/to/llamatron`);
  every configure and preset reads it.
- Xcode's command line tools, CMake, Ninja, Python 3 and Homebrew's `sdl2`.
- For the web build: Emscripten (Homebrew's `emscripten` or emsdk), and Google Chrome or
  Chromium for the web tests (they report *Skipped* without it).
- For the DOSBox-X reference runs: `dosbox-x` and `nasm`.

Versions known to work: DOSBox-X 2026.10.01, NASM 3.02, Emscripten 6.0.12 (Homebrew).

### Native

```sh
sh tools/harness/setup_venv.sh               # once: the Python venv for the oracle tests
cmake --preset macos-native -DLLAMATRON_ORIGINAL_DIR=/path/to/llamatron
cmake --build --preset macos-native          # -> build/src/platform/llamatron
ctest --preset macos-native                  # about 100 tests
```

Without the venv, configure prints "Oracle tests disabled" and ctest quietly runs only the C
tests (about 44). Run `setup_venv.sh` and configure again.

### Web

```sh
emcmake cmake --preset web-emscripten -DLLAMATRON_ORIGINAL_DIR=/path/to/llamatron
cmake --build build-web
cmake --build build-web --target web-dist    # -> build-web/dist/ (serve it on 127.0.0.1 only)
ctest --test-dir build-web -j1               # C tests under node, page tests in headless Chrome
```

Serving `dist/` and what it contains: `src/platform/README.md` (Web folder).

### DOSBox-X reference runs

The original game in DOSBox-X, replayed against the port. They are slow and opt-in. Configure
with `-DLLAMATRON_DOSBOX_TESTS=ON`, then run `ctest --preset macos-native -L dosbox -LE dosbox_long`.
Details are in `tests/README.md` (Run).

How to play: `docs/PLAYING.md`. What is verified and how: `docs/STATUS.md`.

### The macOS app, in one command

```sh
LLAMATRON_ORIGINAL_DIR=/path/to/llamatron cmake --workflow --preset macos-app   # -> build-app/Llamatron.app
```

This configures `build-app/` (Release, `LLAMATRON_MACOS_BUNDLE=ON`), builds a self-contained
`Llamatron.app` and runs its checks: SDL is bundled, so the app runs without Homebrew; the
signature verifies; and the icon is drawn from the title picture. A workflow preset takes no
`-D` options, so it finds the game files through the environment variable, or in
`<repo>/original/` when the variable is not set. The same three steps by hand:

```sh
cmake --preset macos-app -DLLAMATRON_ORIGINAL_DIR=/path/to/llamatron
cmake --build --preset macos-app
ctest --preset macos-app
```

Building needs the native tools above; running the app needs none of them. The app is ad-hoc signed for this Mac only (no Apple account, no notarisation).
Like every build it contains the original's assets: personal use only (above).
