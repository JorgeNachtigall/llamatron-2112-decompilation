#!/usr/bin/env python3
"""(Re)seed docs/funcmap.csv from the headless exports (addresses and names only).

Inputs (local, never committed): re/functions/<program>.exe.functions.csv (ExportFunctions.java)
and re/symbols.csv (func rows, including shared tails that Ghidra keeps as labels).
Rows that already exist in docs/funcmap.csv keep every column the workstreams own (size,
c_symbol, file, status, owner, range, notes, even when empty) and are written back byte for
byte unless the RE name changed; only new rows get the classify() defaults. Rows other
workstreams added by hand (e.g. labels) are kept, so the file can be edited by every
workstream and regenerated after each RE pass.

Columns: program,orig_addr,size,name,c_symbol,file,status,owner,range,notes
(docs/PLAN.md section 3.4 plus program and range).
"""
import csv
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "docs", "funcmap.csv")
COLS = ["program", "orig_addr", "size", "name", "c_symbol", "file", "status", "owner", "range", "notes"]
PROGRAMS = ["game86", "fs", "llama", "install"]

# Dispatch-table families owned by the enemy workstream regardless of address (PLAN section 7).
W5_PREFIXES = ("enemy_init_", "think_", "think0e_mode_", "anim_", "ent_hit_", "pickup_effect_",
               "hit_kill_action_", "entity_", "enemy_")


def game86_range(seg, off):
    """Annotation range letter of PLAN section 4 for a game86 address."""
    if seg == 0x1E7B:
        return "F"
    if off < 0x0981:
        return "A"
    if off < 0x1DCD:
        return "B"
    if off < 0x3DE4:
        return "C"
    if off < 0x4650:
        return "D"
    if off < 0x6566:
        return "E"
    if off < 0xE4DE:
        return "F"
    return "S"  # system: RNG, video mode, IRQ install, keyboard, file/LZSS, memory


# name prefix -> (proposed C symbol or None to keep the name, file, owner, note)
RULES = [
    ("blit_shifted_", "gfx_draw_sprite", "src/engine/gfx.c", "W6", "blitter family: one C routine"),
    ("blit_", "gfx_draw_sprite", "src/engine/gfx.c", "W6", "blitter family: one C routine"),
    ("restore_w", "gfx_restore_rect", "src/engine/gfx.c", "W6", "restore family: one C routine"),
    ("sprite_draw", "gfx_draw_sprite", "src/engine/gfx.c", "W6", ""),
    ("g3_draw", "gfx_draw_sprite", "src/engine/gfx.c", "W6", "g3 shapes use the sprite format"),
    ("vram_", None, "src/engine/gfx.c", "W6", "renderer API (W2) semantics"),
    ("glyph_", None, "src/game/text.c", "W6", ""),
    ("text_", None, "src/game/text.c", "W6", ""),
    ("hud_", None, "src/game/hud.c", "W6", ""),
    ("hiscore_", None, "src/game/hiscore.c", "W6", ""),
    ("name_entry_", None, "src/game/hiscore.c", "W6", ""),
    ("title_", None, "src/game/attract.c", "W4", ""),
    ("attract_", None, "src/game/attract.c", "W4", ""),
    ("script_", None, "src/game/script.c", "W4", ""),
    ("shots_", None, "src/game/bullets.c", "W4", ""),
    ("player_", None, "src/game/player.c", "W4", ""),
    ("score_", None, "src/game/player.c", "W4", ""),
    ("wave_", None, "src/game/main_sm.c", "W4", ""),
    ("continue_", None, "src/game/main_sm.c", "W4", ""),
    ("game_", None, "src/game/main_sm.c", "W4", ""),
    ("sm_", None, "src/game/main_sm.c", "W4", ""),
    ("main", "game_main", "src/game/main_sm.c", "W4", ""),
    ("play_tick_hook", "game_tick", "src/game/tick.c", "W4", ""),
    ("play_draw_hook", "game_draw_frame", "src/game/frame.c", "W4", ""),
    ("pause_", None, "src/game/tick.c", "W4", ""),
    ("companion_", None, "src/game/player.c", "W4", ""),
    ("level_", None, "src/game/script.c", "W4", ""),
    ("rng_mix", "rng_init", "src/game/rng.c", "W4", "seed = BIOS tick (injectable)"),
    ("rng_next", "rng_next", "src/game/rng.c", "W4", ""),
    ("irq08_timer", "timing_tick", "src/engine/timing.c", "W2", "50.16 Hz scheduler"),
    ("irq09_keyboard", "kbd_on_scancode", "src/engine/input.c", "W2", ""),
    ("input_poll", "input_state", "src/engine/input.c", "W2", ""),
    ("frame_run", "frame_run", "src/engine/timing.c", "W2", "frame callback"),
    ("page_swap", "gfx_flip", "src/engine/gfx.c", "W2", ""),
    ("palette_set", "gfx_set_palette", "src/engine/gfx.c", "W2", ""),
    ("crtc_", "gfx_flip", "src/engine/gfx.c", "W2", ""),
    ("display_page_reset", None, "src/engine/gfx.c", "W2", ""),
    ("screen_", None, "src/engine/gfx.c", "W2", ""),
    ("video_", None, "src/engine/gfx.c", "W2", ""),
    ("wait_vsync", None, "src/platform/", "W2", "platform: end of frame"),
    ("kbd_flush", None, "src/platform/", "W2", "platform"),
    ("sound_toggle", "snd_toggle", "src/sound/", "W7", ""),
    ("title_tune_", None, "src/sound/", "W7", ""),
    ("lzss_", "lzss_decode", "tools/extract/", "W1", "build-time extraction"),
    ("cmp_load", None, "tools/extract/", "W1", "build-time extraction"),
    ("data_load", "assets_load", "src/assets/", "W1", "assets come from build/generated"),
    ("config_init", "config_from_options", "src/game/init.c", "W4", "PSP tail -> port options"),
    ("irq_install", None, "src/platform/", "W2", "not ported: platform timer/input"),
    ("mem_shrink", None, "", "W0", "not ported: DOS memory"),
    ("game_exit", None, "src/game/main_sm.c", "W4", ""),
    ("hook_nop", None, "", "W4", "empty hook"),
]


def classify(program, seg, off, name):
    if program == "fs":
        return None, "src/sound/title.c", "W2", "fs", "front screen (ILBM decode in W1 extractor)"
    if program in ("llama", "install"):
        return None, "", "W0", program, "launcher/installer: llama.inf semantics become port options"
    rng = game86_range(seg, off)
    if name.startswith(W5_PREFIXES) and rng in ("B", "C", "D", "E") and not name.startswith("anim_draw"):
        return None, "src/game/entities/", "W5", rng, ""
    for prefix, csym, file, owner, note in RULES:
        if name == prefix or name.startswith(prefix):
            return csym, file, owner, rng, note
    owner = {"A": "W4", "B": "W4", "C": "W5", "D": "W4", "E": "W6", "F": "W6", "S": "W4"}[rng]
    return None, "", owner, rng, ""


def main():
    funcs = {}
    for prog in PROGRAMS:
        path = os.path.join(ROOT, "re", "functions", prog + ".exe.functions.csv")
        if not os.path.exists(path):
            sys.exit(f"missing {path}: run ExportFunctions.java first")
        with open(path, newline="") as f:
            for r in csv.DictReader(f):
                funcs[(prog, r["entry"])] = {"size": r["body_bytes"], "name": r["name"]}
    with open(os.path.join(ROOT, "re", "symbols.csv"), newline="") as f:
        for r in csv.DictReader(f):
            if r["type"] == "func":
                e = funcs.setdefault((r["program"], r["address"]), {"size": ""})
                e["name"] = r["name"]
    old, raw = {}, {}
    if os.path.exists(OUT):
        with open(OUT, newline="") as f:
            lines = f.read().split("\n")[1:]
        for line in lines:
            if not line:
                continue
            r = dict(zip(COLS, next(csv.reader([line]))))
            old[(r["program"], r["orig_addr"])] = r
            raw[(r["program"], r["orig_addr"])] = line
    rows = []
    for (prog, addr), e in funcs.items():
        seg, off = (int(x, 16) for x in addr.split(":"))
        name = e["name"]
        csym, file, owner, rng, note = classify(prog, seg, off, name)
        if csym is None and not name.startswith(("unk_", "FUN_")):
            csym = name
        row = {"program": prog, "orig_addr": addr, "size": e["size"], "name": name,
               "c_symbol": csym or "", "file": file, "status": "todo", "owner": owner,
               "range": rng, "notes": note}
        prev = old.pop((prog, addr), None)
        if prev:
            if name.startswith(("unk_", "FUN_")) and not prev["name"].startswith(("unk_", "FUN_")):
                name = prev["name"]
            row = dict(prev, name=name)
            if name == prev["name"]:
                row["_raw"] = raw[(prog, addr)]
        rows.append(row)
    # hand-added rows with no function or func symbol behind them
    rows.extend(dict(r, _raw=raw[k]) for k, r in old.items())
    order = {p: i for i, p in enumerate(PROGRAMS)}
    rows.sort(key=lambda r: (order[r["program"]], *(int(x, 16) for x in r["orig_addr"].split(":"))))
    with open(OUT, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=COLS, lineterminator="\n", extrasaction="ignore")
        w.writeheader()
        for r in rows:
            if "_raw" in r:
                f.write(r["_raw"] + "\n")
            else:
                w.writerow(r)
    print(f"{OUT}: {len(rows)} rows")


if __name__ == "__main__":
    main()
