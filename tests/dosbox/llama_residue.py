"""Do the title llamas' leftover bytes matter to the original's play? (compare.py's skip)

The title screen's llamas live in entity slots 0..8 (DS:48C1..4B00). entities_clear
(1000:4307) frees the slots at the attract -> play switch by their type word only, so the
wave's entities later take those slots over with the llama bytes still in every field their
type's init and think never write. The port starts play from zeroed slots. compare.py may
skip such a byte only if the original never reads it before writing it.

The proof is an oracle run: the golden scenario again in DOSBox-X, with every non-type word
of slots 0..8 poked (PROBE.POK) while the slots are free between entities_clear and the
first entity_alloc. Applying the pokes costs the probe's IRQ handler time, which moves
game86's frames (page bookkeeping DS:39D4..39D8, VRAM), so the baseline is a control run with
the same pokes writing the leftover itself. Two poisons: 0 (what the port holds) and the
bitwise inverse of the leftover. A field passes if neither poison changes anything but the
poisoned bytes themselves (still holding the poison, the control still holding its leftover)
in any DGROUP snapshot (game stack and page bookkeeping aside), and leaves the screen as it
was (the settled parts the golden test checks).

Result (2026-10-19, at the scenarios' full lengths): every field +02..+3e passes with both
poisons in warp, death_continue and two_player. compare.py's RESIDUE_FIELDS relies on it.

    python tests/dosbox/llama_residue.py [SCENARIO ...] [--workdir DIR] [--jobs N]
        [--fields 02 04 ...]

Runs every scenario that goes from the title to play (SWITCH) by default, all fields in
two halves (PROBE.POK holds at most 256 words), and narrows a failing half to single fields.
Prints one line per field: pass, or the first snapshot and byte it changed.
"""

from __future__ import annotations

import argparse
import struct
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "tools" / "harness" / "dosbox"))
import refrun  # noqa: E402
from test_reference_golden import (GAME_STACK, ORIGINAL, RUNS, probe_args,  # noqa: E402
                                   settled, shown_page, snapshot_summary)

ENTITIES, ENTITY_SIZE, LLAMA_SLOTS = 0x48C1, 0x40, 9
PLAY_HOOK, TICK_HOOK = 0x1562, 0x39E4
# g_page_front/back, g_draw_page (re-notes §8): where game86's frames fall between IRQs. Under
# heavy host load (8 runs at once) they moved run to run in two_player from IRQ 1300, poison
# or not; a rerun of the same pair agreed. Not game state: skipped, and the screen is only
# compared while they agree.
FRAME_PHASE = range(0x39D3, 0x39D9)

# IRQ to poke at per scenario: the slots are free (type word -1) from entities_clear to the
# first entity_alloc, IRQs 156..158 after fire at 150 and 1201..1203 after fire at 1200
# (per-IRQ windows of the reference); the middle one leaves a tick either way.
SWITCH = {"warp": 157, "death_continue": 157, "two_player": 1202}

# Word fields of an entity record after the type word (src/game/state.h Entity).
FIELDS = {
    0x02: "x", 0x04: "y", 0x06: "anim", 0x08: "vx", 0x0A: "vy", 0x0C: "sprite",
    0x0E: "scale", 0x10: "solid", 0x12: "aux12", 0x14: "timer", 0x16: "aux16", 0x18: "hp",
    0x1A: "aux1a", 0x1C: "aux1c", 0x1E: "kill_action", 0x20: "sub_mode",
    0x22: "reload/countdown", 0x24: "aux24", 0x26: "aux26", 0x28: "anim_toggle",
    0x2A: "aux2a", 0x2C: "sprite_ref", 0x2E: "aux2e", 0x30: "aux30", 0x32: "overlay.x",
    0x34: "overlay.y", 0x36: "overlay.sprite", 0x38: "overlay.frames", 0x3A: "kind",
    0x3C: "death_sample/rate", 0x3E: "hit_handler",
}
POISONS = ("zero", "inverse")       # against "control"


def same_screen(a, b) -> bool:
    """The settled parts of two VRAM captures agree (test_reference_golden: the background
    page, and the shown page unless the capture landed on a flip)."""
    a, b = snapshot_summary(a), snapshot_summary(b)
    if {**settled(a), "dgroup": None} != {**settled(b), "dgroup": None}:
        return False
    return shown_page(a) is None or shown_page(a)[0] != shown_page(b)[0] or \
        shown_page(a) == shown_page(b)


def run(name: str, workdir: Path, extra_pokes=()):
    cfg = probe_args(RUNS[name])
    cfg["pokes"] = list(cfg.get("pokes", [])) + list(extra_pokes)
    result = refrun.probe(original=ORIGINAL, workdir=workdir,
                          time_limit=RUNS[name].get("time_limit", 90), **cfg)
    if result.exit_type or result.exit_code or result.pokes_applied != len(cfg["pokes"]):
        raise RuntimeError(f"{name} in {workdir}: {result}")
    return result


def poison(clean, name: str, fields, kind: str) -> list[tuple[int, int, int]]:
    """Pokes for `fields` of slots 0..8 at the scenario's switch. The inverse poison flips the
    clean run's leftover, read at its first play snapshot (the leftover is unchanged from the
    switch on; bytes the wave writes before then are not leftover and don't matter)."""
    first = next(s.dgroup for s in clean.snapshots
                 if struct.unpack_from("<H", s.dgroup, TICK_HOOK)[0] == PLAY_HOOK)
    pokes = []
    for slot in range(LLAMA_SLOTS):
        for f in fields:
            off = ENTITIES + ENTITY_SIZE * slot + f
            word = struct.unpack_from("<H", first, off)[0]
            word = {"control": word, "zero": 0, "inverse": word ^ 0xFFFF}[kind]
            pokes.append((SWITCH[name], off, word))
    return pokes


def changes(control, poisoned, pokes):
    """(irq, what) of the first change the poison made, or None."""
    value = {}
    for _, off, word in pokes:
        value[off], value[off + 1] = word & 0xFF, word >> 8
    leftover = {}
    for a, b in zip(control.snapshots, poisoned.snapshots):
        assert a.irq == b.irq
        for off in range(len(a.dgroup)):
            if a.dgroup[off] == b.dgroup[off] or off in GAME_STACK or off in FRAME_PHASE:
                continue
            if off in value and b.dgroup[off] == value[off] and \
                    leftover.setdefault(off, a.dgroup[off]) == a.dgroup[off]:
                continue                     # the poison itself, both sides unchanged
            slot, rel = divmod(off - ENTITIES, ENTITY_SIZE)
            where = (f"entities[{slot}]+{rel:02x}" if 0 <= slot < 70 else f"DS:{off:04X}")
            return a.irq, f"{where} {a.dgroup[off]:02X} -> {b.dgroup[off]:02X}"
        phase = slice(FRAME_PHASE.start, FRAME_PHASE.stop)
        if a.dgroup[phase] == b.dgroup[phase] and not same_screen(a, b):
            return a.irq, "shown page / page 4000"
    if len(control.snapshots) != len(poisoned.snapshots):
        return None, "snapshot count"
    return None


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("scenarios", nargs="*", default=sorted(SWITCH))
    ap.add_argument("--workdir", type=Path, default=HERE.parents[1] / "build-dosbox" / "residue")
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--fields", nargs="*", help="hex entity offsets (default: all)")
    args = ap.parse_args(argv)
    fields = [int(f, 16) for f in args.fields] if args.fields else sorted(FIELDS)
    pool = ThreadPoolExecutor(args.jobs)        # DOSBox-X runs
    checks = ThreadPoolExecutor(16)              # waiting on them
    clean = {n: pool.submit(run, n, args.workdir / f"{n}-clean") for n in args.scenarios}
    clean = {n: f.result() for n, f in clean.items()}

    def trial(name, group, kind):
        pokes = poison(clean[name], name, group, kind)
        tag = "-".join(f"{f:02x}" for f in group[:1] + group[-1:])
        return run(name, args.workdir / f"{name}-{kind}-{tag}", pokes), pokes

    def check(name, group):
        """Per field: None (pass) or (poison, irq, what)."""
        jobs = {k: pool.submit(trial, name, group, k) for k in ("control", *POISONS)}
        control = jobs["control"].result()[0]
        failed = {k: changes(control, *jobs[k].result()) for k in POISONS}
        failed = {k: v for k, v in failed.items() if v}
        if not failed:
            return {f: None for f in group}
        if len(group) == 1:
            k, (irq, what) = next(iter(failed.items()))
            return {group[0]: (k, irq, what)}
        mid = len(group) // 2
        out = {}
        for half in (group[:mid], group[mid:]):
            out.update(check(name, half))
        if all(v is None for v in out.values()):    # only together: fail them all
            k, (irq, what) = next(iter(failed.items()))
            out = {f: (k, irq, f"{what}, only with +{group[0]:02x}..+{group[-1]:02x} together")
                   for f in group}
        return out

    worst = 0
    for name in args.scenarios:
        halves = [fields[:len(fields) // 2], fields[len(fields) // 2:]] if len(fields) > 8 else [fields]
        verdicts = {}
        for v in [checks.submit(check, name, h) for h in halves if h]:
            verdicts.update(v.result())
        print(f"== {name} (poisoned at IRQ {SWITCH[name]}, {len(clean[name].snapshots)} snapshots "
              f"to IRQ {clean[name].snapshots[-1].irq})")
        for f in sorted(verdicts):
            v = verdicts[f]
            print(f"  +{f:02x} {FIELDS.get(f, '?'):18s} " +
                  ("pass" if v is None else f"FAIL ({v[0]} poison: IRQ {v[1]}, {v[2]})"))
            worst |= v is not None
    return worst


if __name__ == "__main__":
    sys.exit(main())
