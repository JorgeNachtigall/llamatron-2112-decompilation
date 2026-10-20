# Playing Llamatron 2112 (the port)

The game plays like the 1992 PC version: same waves, same keys, same high-score table. This page
covers what you need at the keyboard. The builds contain data from your own copy of the game,
so they are for **personal use only**; never share the app or the web folder.

## Starting

- **Mac:** open `build-app/Llamatron.app`. It is built with `cmake --workflow --preset macos-app`
  (README.md, *Build*). It is ad-hoc signed, so the first time macOS may ask you to confirm
  with right-click → Open.
- **Web:** open the page the build serves (`llamatron.html`, or `index.html` in `dist/`).
  Browsers keep audio off until you interact, so click the page once to get sound.

The shareware title picture comes first. Press any key except Shift, Ctrl, Alt and the lock
keys (or a joystick button in joystick mode) to go on. On the game's own title screen:

| Key | What it does |
|-----|--------------|
| Space / fire | start |
| Left / Right | 1 player, 2 players with two joysticks, 2 players with one stick (player 2 is the droid) |
| Up / Down | shared start on / off |
| F2 | options screen (below) |

Left alone for 30 seconds, the title runs the attract mode: the story, a demo, the credits and
another demo.

## In the game

| Key | What it does |
|-----|--------------|
| Cursor keys (or keypad 8 2 4 6) | move |
| Space | fire |
| P | pause; while paused: G or O resume, S toggles the sound, other keys play a sample |
| X | abort the wave |
| Q | quit the game |

### Gamepad

Any SDL-supported pad (Xbox, PlayStation, Switch Pro, ...) works without setup:

| Pad | Keyboard control | Joystick control |
|-----|------------------|------------------|
| left stick / d-pad | move | joystick 1 |
| A, B | fire | joystick 1 fire |
| X, Y | fire | joystick 2 fire (single pad) |
| right stick | fire while pushed | joystick 2 (single pad: twin-stick aiming) |
| Start | pause; on the title: start (Enter) | the same |
| Back / Select | sound toggle while paused; on the title: options (F2) | the same |

A second pad drives joystick 2 in joystick mode.

## Options (F2 on the title)

These are the settings the original's install program wrote to `llama.inf`:

| Line | Choices |
|------|---------|
| Video | VGA 256 colours / EGA 16 colours |
| Control | Keyboard / Joystick |
| Sound | Sound Blaster, AdLib, Roland: all three play the game's sampled sound; None is silent |
| Processor | the original's CPU digit (kept for compatibility) |
| Flashes | flashes and pulsing / pulsing only / neither (for flash-sensitive players) |

Up/Down choose a line, Left/Right or Space change it, Enter saves, and Esc (or F2 again)
leaves without saving. Saved settings apply from the next game.

## Picture and window

| Key | Mac | Web |
|-----|-----|-----|
| Full screen | F11, Alt+Enter, Cmd+F or Ctrl+Cmd+F | F11, Alt+Enter or a double click |
| Next scale mode | F10 or Cmd+I | F10 |

On a Mac keyboard F10 and F11 need Fn, hence the Cmd shortcuts. The picture is always 4:3,
like on the 1992 monitor, with black borders. There are three scale modes:

- **nearest** (the default) fills the window;
- **integer** uses whole pixel multiples for the crispest picture;
- **sharp** scales up by whole multiples and then smooths down to the window size.

## Where settings and high scores are kept

|  | Mac | Web |
|--|-----|-----|
| Settings (`llama.inf`) | `~/Library/Application Support/llamatron-port/llamatron/llama.inf` | the browser's storage (IndexedDB) for the page's address |
| High scores (`scores.yak`) | the same folder, `scores.yak` | the same browser storage |

Both are written in the original's own file formats, so you can copy them to or from a DOS
installation. Until you save anything, the game starts from your original folder's `llama.inf`
and `scores.yak`, as extracted when the game was built, or else from the game's built-in
defaults. High scores are saved after you enter a name.

**Resetting:**
- **Mac:** quit the game and delete the files (or the whole folder):
  ```sh
  rm ~/Library/Application\ Support/llamatron-port/llamatron/llama.inf     # settings
  rm ~/Library/Application\ Support/llamatron-port/llamatron/scores.yak    # high scores
  ```
- **Web:** clear the site data for the page's address, e.g. in Chrome: the icon left of the
  address → *Site settings* → *Delete data*. That resets both. The web and Mac copies are
  separate, and each web address (host and port) has its own.

## Command line and page options

- **Mac:** start the app with options, e.g.
  `open build-app/Llamatron.app --args --ega --scale integer`.
- **Web:** add them to the page address, e.g. `llamatron.html?ega&scale=integer`.

| Mac | Web | What it does |
|-----|-----|--------------|
| `--vga` / `--ega` | `vga` / `ega` | video for this run |
| `--keyboard` / `--joystick` | `keyboard` / `joystick` | control for this run |
| `--sound S\|A\|R\|N` | `sound=S` ... | sound card for this run |
| `--mute` | `mute` | no audio at all |
| `--scale nearest\|integer\|sharp` | `scale=...` | the first scale mode |

These apply to one run only. The options screen changes the saved settings.
