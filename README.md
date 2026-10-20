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

```sh
cmake --preset macos-native -DLLAMATRON_ORIGINAL_DIR=/path/to/llamatron
cmake --build --preset macos-native
ctest --preset macos-native
```

Web: `emcmake cmake --preset web-emscripten && cmake --build build-web`.

### The macOS app, in one command

```sh
cmake --workflow --preset macos-app        # -> build-app/Llamatron.app
```

This configures `build-app/` (Release, `LLAMATRON_MACOS_BUNDLE=ON`), builds a self-contained
`Llamatron.app` and runs its checks: SDL is bundled, so the app runs without Homebrew; the
signature verifies; and the icon is drawn from the title picture. The workflow reads the game
files from `original/`. For another folder, run the steps by hand:

```sh
cmake --preset macos-app -DLLAMATRON_ORIGINAL_DIR=/path/to/llamatron
cmake --build --preset macos-app
ctest --preset macos-app
```

Building needs Xcode's command line tools, Ninja, Python 3 and Homebrew's `sdl2`; running the
app does not. The app is ad-hoc signed for this Mac only (no Apple account, no notarisation).
Like every build it contains the original's assets: personal use only (above).
