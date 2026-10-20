"""Compare the port's replay (tests/replay/replay_driver.c, LRPL file) with a DOSBox-X
reference run of the same input script (tools/harness/dosbox, PROBE.SNP).

Both are lists of 64 KB DGROUP images keyed by timer tick (the probe's IRQ number = the
port's tick number, both from 1). Only the bytes the adapter maps are compared (the mask in
the LRPL file); the reference images need no canonicalisation because the original only
stores the canonical encodings (flags 0/1, digits 0..9).

    python tests/replay/compare.py PORT.lrpl PROBE.SNP [--ignore 39e6]

prints, per common tick, the number of differing bytes and the first fields, and exits 1
if any tick differs.
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

DGROUP_SIZE = 0x10000
LRPL_HEADER = "<4sHHII"

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from adapters.gamestate import (ATTRACT_LLAMAS, CHAIN_TAIL, chain_tail_past_rings,  # noqa: E402
                                follower_trail_equal)

# The reference run's phase, from its tick hook (g_tick_hook, not part of GameState).
TICK_HOOK = 0x39E4
TITLE_TUNE_HOOK = 0x084E       # title_tune_tick: the only code that counts tune.countdown
TITLE_HOOKS = {0x3F6F, TITLE_TUNE_HOOK}  # title_tick_hook, title_tune_tick
PLAY_HOOK = 0x1562              # play_tick_hook
DRAW_HOOK = 0x39E0
TICK_COUNT = range(0x39E6, 0x39E8)  # counts every IRQ whatever runs: not part of a shift
TUNE_PERIOD, TUNE_COUNTDOWN = 0x0627, 0x0629
WAVE_INTRO_TIMER = 0x08FC      # counts down; reloaded when a wave (re)starts (e.g. after a death)

# Entity slots: a free slot (type word < 0) keeps stale bytes from its last user in the
# original (e.g. the title llamas) and whatever the port leaves there; only its type word
# is state. Compared again as soon as either side allocates the slot.
ENTITIES, ENTITY_COUNT, ENTITY_SIZE = 0x48C1, 70, 0x40
# overlay.frames (+38) counts display frames (entities_animate_all 1000:4374), not ticks: at a
# tick snapshot it depends on where the frames fell between the IRQs, which differs between
# DOSBox-X and the replay (Prism; the unicorn lockstep, with one frame schedule on both sides,
# matches it through level 99). Timing when the reference's value lies between the port's at
# the neighbouring ticks.
OVERLAY_FRAMES = 0x38

# A follower's trail (+1E) as the original's DS address and the port's player index is the same
# value: adapters.gamestate.follower_trail_equal.

# The loaded image has P2's trail head [2CF9] = 2 (the listing's g_p2_anim), which wave_start
# zeroes at 1000:04ef before anything reads it (DEVIATIONS #10, withdrawn); the port starts at
# 0. Skipped while the reference still holds the loaded 2.
P2_TRAIL_HEAD = 0x2CF9
P2_TRAIL_HEAD_LOADED = 2

# Named DGROUP regions for reports (docs/re-notes-game86.md §8).
_SCALARS = {  # DS offset: (size, name), src/game/state.h
    0x0429: (1, "frame_drawn"), 0x0453: (8, "rng.mixer"), 0x0589: (1, "flash_mode"),
    0x058A: (1, "vga"), 0x058B: (1, "joystick"), 0x058D: (1, "keyboard"),
    0x05A1: (2, "hud_dirty"), 0x0615: (2, "smart_bomb"), 0x061F: (2, "continues"),
    0x062F: (2, "enemies_alive"), 0x0634: (2, "respawn_timer[1]"), 0x0636: (4, "speed_accum"),
    0x063A: (2, "kills_toward_bonus"), 0x0650: (2, "shots_bounce"), 0x0652: (2, "shot_power_kept"),
    0x0688: (1, "sound_on"), 0x0689: (1, "sound_present"), 0x06C4: (2, "demo_mode"),
    0x06C7: (2, "flash_list"), 0x07F3: (2, "respawn_timer[0]"), 0x0802: (2, "spawning_done"),
    0x0804: (2, "eggs_spawned"), 0x0806: (2, "spawn_sprite"), 0x0810: (2, "wave_result"),
    0x0812: (2, "tuning.init01_mode"), 0x0814: (2, "spawn_phase"),
    0x0816: (2, "tuning.init0d_hp"), 0x0818: (2, "tuning.segment_family"),
    0x081A: (4, "orbit_angle"), 0x081E: (2, "orbit_next_phase"),
    0x0820: (2, "tuning.orbit_phase_step"), 0x0824: (2, "segment_sprite"),
    0x0826: (2, "tuning.growth_step"), 0x0828: (4, "tuning.accum_start"), 0x082C: (2, "tuning.runner_fire"),
    0x082E: (2, "tuning.runner_hits"), 0x0830: (2, "tuning.dropper_creep_mask"), 0x086E: (2, "tuning.init19_vx"),
    0x06BA: (4, "follower_lag"), 0x0870: (2, "snake_head"), 0x091F: (2, "rescue_marker_pending"),
    0x0921: (4, "rescue_marker_y"), 0x2CF7: (4, "trail_head"),
    0x069C: (4, "ship_control[0].shot_velocity"), 0x06A0: (4, "ship_control[1].shot_velocity"),
    0x06A4: (4, "chain_tail"), 0x06A8: (4, "demo_wander"), 0x06AC: (2, "ship_control[0].shot_direction"),
    0x06AE: (2, "ship_control[1].shot_direction"), 0x06B0: (4, "ship_control[1].move"),
    0x06B4: (2, "ship_control[1].trail_flag"), 0x06B6: (2, "ship_control[0].aim"),
    0x06B8: (2, "ship_control[1].aim"), 0x06BE: (4, "ship_control[0].move"),
    0x06C2: (2, "ship_control[0].facing"), 0x07F9: (2, "ship_control[1].facing"),
    0x0631: (1, "fire_delay"), 0x0632: (2, "fire_turn"), 0x09D3: (2, "started_with_stick_b"),
    0x0682: (2, "droid_aim_timer"), 0x409C: (4, "droid_aim_votes"), 0x0919: (2, "droid_quarry"),
    0x0625: (2, "tune.fire_pressed"), 0x0627: (2, "tune.period"), 0x0629: (2, "tune.countdown"),
    0x062B: (2, "tune.position"), 0x062D: (2, "tune.restart"), 0x059E: (1, "options_default"),
    0x0621: (2, "continue_seconds"), 0x0623: (2, "continue_ticks"),
    0x0686: (2, "sound_key_latch"), 0x09EC: (2, "pause_message"),
    0x39E0: (2, "draw_hook"), 0x39E4: (2, "tick_hook"), 0x2336: (2, "speedup_timer"),
    0x2338: (2, "speedup_period"),
    0x061D: (2, "game_started"), 0x068A: (2, "wave_start_delay"),
    0x07F5: (2, "player_label_offset"), 0x07FE: (2, "spawn_delay"), 0x0617: (2, "play_ticked"),
    0x059F: (2, "level_default"), 0x0902: (2, "pulse_enable"), 0x0680: (2, "pulse_index"),
    0x044C: (1, "flash_frame_timer"), 0x07F7: (2, "player2_label_offset"),
    0x0941: (2, "level_variant"), 0x0B74: (6, "text"),
    0x2104: (20, "title_objects"), 0x0904: (2, "title_timer"), 0x0906: (1, "title_key_latch"),
    0x0872: (2, "death_sound_timer"), 0x0874: (2, "rude_name"), 0x0876: (2, "debris_alt"),
    0x08F8: (2, "unhatched_left"), 0x08FA: (2, "spawn_remaining"),
    0x08FC: (2, "wave_intro_timer"), 0x0900: (2, "spawn_type"), 0x0907: (2, "shield_timer"),
    0x091B: (2, "unhatched"), 0x091D: (2, "swarm_count"), 0x0925: (2, "bounced"),
    0x0927: (2, "free_slots"), 0x09DE: (4, "hiscore_mark"), 0x233C: (2, "loop_count"),
    0x233E: (2, "level_flags"), 0x2CF6: (1, "options"), 0x39E6: (2, "tick_count"),
    0x39E9: (2, "rng.position"), 0x39EB: (2, "spawn cursor"),
}

# (start, size, name, record size or 0)
REGIONS = [(off, size, name, 0) for off, (size, name) in _SCALARS.items()] + [
    (0x0413, 0x10, "scratch", 0),
    (0x21A6, 0xC8, "player", 0),
    (0x226E, 0xC8, "player_other", 0),
    (0x2CFB, 8, "ship", 0),
    (0x2D03, 4, "target", 0),
    (0x40A0, 0xA0, "hiscores", 16),
    (0x4441, 0x10, "scores", 8),
    (0x4481, 0x200, "snake_trail", 8),
    (0x4881, 0x40, "companion", 0),
    (0x48C1, 70 * 0x40, "entities", 0x40),
    (0x68C9, 15 * 0x10, "shots", 0x10),
    (0x6AC9, 0x200, "rng.table", 0),
    (0x6CC9, 0x400, "trail[0]", 8),
    (0x70C9, 0x400, "trail[1]", 8),
]


def describe(off: int) -> str:
    for start, size, name, rec in REGIONS:
        if start <= off < start + size:
            rel = off - start
            if rec:
                return f"{name}[{rel // rec}]+{rel % rec:02x}"
            return f"{name}+{rel:02x}" if size > 2 else name
    return f"DS:{off:04x}"


@dataclass
class PortRun:
    every: int
    mask: bytes = field(repr=False)
    snapshots: dict[int, bytes] = field(repr=False)


def read_port(path: Path) -> PortRun:
    data = Path(path).read_bytes()
    magic, version, _, every, count = struct.unpack_from(LRPL_HEADER, data, 0)
    if magic != b"LRPL" or version != 1:
        raise ValueError(f"{path}: not an LRPL v1 file")
    pos = struct.calcsize(LRPL_HEADER)
    mask = data[pos:pos + DGROUP_SIZE]
    pos += DGROUP_SIZE
    snaps = {}
    for _ in range(count):
        tick = struct.unpack_from("<I", data, pos)[0]
        snaps[tick] = data[pos + 4:pos + 4 + DGROUP_SIZE]
        pos += 4 + DGROUP_SIZE
    return PortRun(every, mask, snaps)


@dataclass
class PortFrames:
    mask: bytes
    frames: list[tuple[int, int, bytes]]    # (tick, frame, dgroup) after each dumped frame

    def at_tick(self, tick: int) -> list[tuple[int, bytes]]:
        """(frame, dgroup) of the frames run while the tick count was `tick`."""
        return [(f, img) for t, f, img in self.frames if t == tick]


def read_frames(path: Path) -> PortFrames:
    """llamatron_replay --frames output (LRPF)."""
    data = Path(path).read_bytes()
    magic, version, _, count = struct.unpack_from("<4sHHI", data, 0)
    if magic != b"LRPF" or version != 1:
        raise ValueError(f"{path}: not an LRPF v1 file")
    pos = 12
    mask = data[pos:pos + DGROUP_SIZE]
    pos += DGROUP_SIZE
    frames = []
    for _ in range(count):
        tick, frame = struct.unpack_from("<II", data, pos)
        frames.append((tick, frame, data[pos + 8:pos + 8 + DGROUP_SIZE]))
        pos += 8 + DGROUP_SIZE
    return PortFrames(mask, frames)


@dataclass
class TickDiff:
    tick: int
    offsets: list[int]
    ignored: list[tuple[range, str]] = field(default_factory=list)
    # +1 / -1: the port one tick behind / ahead (its tick t+shift matches the reference's
    # tick t), at a transition (hook switch or tick counter jump) or carried on from one;
    # `timing` holds the bytes that shift explains (not in `offsets`, which stay real)
    shift: int = 0
    timing: list[int] = field(default_factory=list)
    transition: bool = False    # a transition since the previous snapshot (see compare)

    def fields(self, limit: int = 12) -> list[str]:
        seen: list[str] = []
        for off in self.offsets:
            name = describe(off)
            base = name.split("+")[0]
            if not seen or not seen[-1].startswith(base):
                seen.append(name)
            if len(seen) >= limit:
                break
        return seen


def compare(port: PortRun, reference: dict[int, bytes], ignore=()) -> list[TickDiff]:
    """Per common tick (ascending), the mapped offsets that differ.

    From the first reference snapshot with a title hook until the first one with the play
    hook, the title llamas (ATTRACT_LLAMAS) are skipped and listed as ignored: the original
    keeps them in the entity table, the port in attract.c. hook_nop transitions in between
    stay in the attract phase, because the bytes survive until entities_clear. P2's trail
    head is skipped while the reference still holds its loaded value (DEVIATIONS #10), and
    chain_tail while the reference read it past the trail rings (DEVIATIONS #14).
    Entity slots free on both sides are compared by their type word only. In play the title
    llamas' leftovers in slots 0..8 are compared: the port keeps them as the original does
    (attract_end at new_game, 7b4ed95), and some are read (DEVIATIONS #26).

    Timing: a differing byte that equals the port's byte one tick later (or earlier) is a
    one-tick shift, provided a transition happens there or the same shift was already
    established at the previous snapshot. A transition is a tick or draw hook switch (in either
    run, or between them) or a jump of the tick counter, which otherwise counts one per tick
    (the original sets it at state changes, e.g. name entry's end 1000:5ab3 and the hi-score
    table 1000:5d41, after CPU-heavy work the port does in zero time: DEVIATIONS #20), or a
    reload of wave_intro_timer (a wave restart, e.g. after a death, between snapshots). While
    such a shift holds, the whole state is compared with the port's tick t + shift: the bytes
    it explains go to `timing`, and every byte that differs from the shifted state is real
    (`offsets`), even one that matches the port's own tick t (e.g. input, which both runs get
    at the same IRQ). A shift is taken only if it leaves fewer differing bytes (an established one
    also on a tie); it ends when the unshifted state is closer again. The tick counter is not compared while shifted. The
    port's neighbouring ticks come from llamatron_replay --around 1.

    tune.countdown only counts while title_tune_tick is the tick hook (084e: -1 per tick,
    reloaded from tune.period at 0) and is frozen after that hook is replaced, so a tune hook
    that ran one tick longer or shorter leaves a lasting one-step difference. With the tune
    hook not running in the reference, a countdown one step (mod the period) from the port's
    is timing too. So is a live entity's overlay.frames between the port's values at the
    neighbouring ticks (OVERLAY_FRAMES: it counts frames)."""
    ignore = set(ignore)
    out = []
    attract = False
    prev_ref_hooks = None
    prev_tick, prev_counts, prev_intro = None, None, None
    carried = 0
    for tick in sorted(set(port.snapshots) & set(reference)):
        a, b = reference[tick], port.snapshots[tick]
        hook = struct.unpack_from("<H", a, TICK_HOOK)[0]
        if hook in TITLE_HOOKS:
            attract = True
        elif hook == PLAY_HOOK:
            attract = False
        counts = (_tick_count(a), _tick_count(b))
        jump = prev_counts is not None and any(not _counted(p, c, tick - prev_tick)
                                               for p, c in zip(prev_counts, counts))
        intro = (_word(a, WAVE_INTRO_TIMER), _word(b, WAVE_INTRO_TIMER))
        jump = jump or (prev_intro is not None and any(i > p for p, i in zip(prev_intro, intro)))
        prev = prev_ref_hooks if prev_ref_hooks is not None else _hooks(a)
        # the port's state at tick + s for s = 0, -1, +1, as compared with the reference
        candidates = {}
        for s in (0, -1, +1):
            other = port.snapshots.get(tick + s)
            if other is None:
                continue
            switch = (jump or _hooks(a) != prev or _hooks(a) != _hooks(b)
                      or _hooks(b) != _hooks(other)
                      or not _counted(_tick_count(b), _tick_count(other), s))
            if s and not (switch or s == carried):
                continue
            offs_s, skipped_s = _differences(a, other, port.mask, ignore, attract)
            candidates[s] = (offs_s, skipped_s)
        offs, skipped = candidates[0]
        state = [o for o in offs if o not in TICK_COUNT]
        shift, timing = 0, []
        for s in sorted((s for s in candidates if s), key=lambda s: s != carried):
            shifted = [o for o in candidates[s][0] if o not in TICK_COUNT]
            closer = len(shifted) < len(state) or (s == carried and len(shifted) == len(state) > 0)
            if closer and (not shift or len(shifted) < len(offs)):
                shift, offs = s, shifted
        if shift:
            # one tick apart: the rest is compared with the port's tick + shift, and every
            # byte that differs there is real
            timing = [o for o in state if o not in offs]
            skipped = candidates[shift][1]
        if jump and TICK_COUNT.start in offs and abs(counts[0] - counts[1]) == 1:
            # the counter was set one tick apart (e.g. 1000:5d41 after a draw the port does
            # in zero time); it counts on in step from there
            timing = timing + [o for o in offs if o in TICK_COUNT]
            offs = [o for o in offs if o not in TICK_COUNT]
        if hook != TITLE_TUNE_HOOK and TUNE_COUNTDOWN in offs and _tune_step(a, port.snapshots[tick + shift]):
            frozen = range(TUNE_COUNTDOWN, TUNE_COUNTDOWN + 2)
            timing = timing + [o for o in offs if o in frozen]
            offs = [o for o in offs if o not in frozen]
        frames = _frame_counted(a, port.snapshots, tick + shift, offs)
        if frames:
            timing = timing + frames
            offs = [o for o in offs if o not in frames]
        carried = shift
        prev_ref_hooks = _hooks(a)
        prev_tick, prev_counts, prev_intro = tick, counts, intro
        out.append(TickDiff(tick, offs, skipped, shift, timing,
                            transition=jump or _hooks(a) != prev))
    return out


def _hooks(image: bytes) -> tuple[int, int]:
    return (struct.unpack_from("<H", image, TICK_HOOK)[0],
            struct.unpack_from("<H", image, DRAW_HOOK)[0])


def _word(image: bytes, off: int) -> int:
    return struct.unpack_from("<H", image, off)[0]


def _tick_count(image: bytes) -> int:
    return struct.unpack_from("<H", image, TICK_COUNT.start)[0]


def _tune_step(a: bytes, b: bytes) -> bool:
    """The reference's tune.countdown is one tick (either way) from the port's: it cycles
    period, ..., 1 (attract.c tune_tick, 1000:084e)."""
    period = struct.unpack_from("<h", a, TUNE_PERIOD)[0]
    ref, mine = (struct.unpack_from("<h", x, TUNE_COUNTDOWN)[0] for x in (a, b))
    if period <= 1 or not (1 <= ref <= period and 1 <= mine <= period):
        return False
    return (mine - ref) % period in (1, period - 1)


def _frame_counted(a: bytes, snapshots: dict[int, bytes], tick: int, offs) -> list[int]:
    """Bytes of live entities' overlay.frames whose reference value lies between the port's at
    tick - 1 and tick + 1 (frame phase, not a different count)."""
    before, after = snapshots.get(tick - 1), snapshots.get(tick + 1)
    if before is None or after is None:
        return []
    out = []
    for i in range(ENTITY_COUNT):
        at = ENTITIES + ENTITY_SIZE * i + OVERLAY_FRAMES
        if (at not in offs and at + 1 not in offs) or _type(a, i) < 0:
            continue
        ref, lo, hi = (struct.unpack_from("<h", x, at)[0] for x in (a, before, after))
        if min(lo, hi) <= ref <= max(lo, hi):
            out += [o for o in (at, at + 1) if o in offs]
    return out


def _counted(before: int, after: int, ticks: int) -> bool:
    """The tick counter went from `before` to `after` by just counting `ticks` ticks."""
    return (before + ticks) & 0xFFFF == after


def _differences(a: bytes, b: bytes, mask: bytes, ignore: set, attract: bool):
    """Compared offsets where reference `a` and port `b` differ, and what was skipped."""
    skipped = [(ATTRACT_LLAMAS, "attract llamas")] if attract else []
    if struct.unpack_from("<H", a, P2_TRAIL_HEAD)[0] == P2_TRAIL_HEAD_LOADED:
        skipped.append((range(P2_TRAIL_HEAD, P2_TRAIL_HEAD + 2),
                        "trail_head[1] = 2 (DEVIATIONS #10)"))
    free = [i for i in range(ENTITY_COUNT) if _type(a, i) < 0 and _type(b, i) < 0]
    if free:
        skipped.append((None, f"stale bytes of {len(free)} free entity slots"))
    skip = set().union(*(r for r, _ in skipped if r is not None))
    for i in free:
        base = ENTITIES + ENTITY_SIZE * i
        skip.update(range(base + 2, base + ENTITY_SIZE))
    skip |= follower_trail_equal(a, b)
    if chain_tail_past_rings(a):
        skipped.append((CHAIN_TAIL, "chain_tail read past the trail rings (DEVIATIONS #14)"))
        skip.update(CHAIN_TAIL)
    offs = [i for i in range(DGROUP_SIZE)
            if mask[i] and a[i] != b[i] and i not in ignore and i not in skip]
    return offs, skipped


def _type(image: bytes, slot: int) -> int:
    return struct.unpack_from("<h", image, ENTITIES + ENTITY_SIZE * slot)[0]


def input_shift_check(diffs: list[TickDiff], segment) -> str | None:
    """Why the port's keys from tick `start` on may come `ticks` later (test_reference_golden
    port_script): a timing shift of the same size holding at the last snapshot at or before
    `start`, or set up by a transition between that snapshot and the next one. None when it
    is explained, else the reason."""
    start, ticks = segment[:2]
    at = [d for d in diffs if d.tick <= start]
    if at and at[-1].shift == ticks:
        return None
    after = next((d for d in diffs if d.tick >= start), None)
    if after is not None and after.shift == ticks and after.transition:
        return None             # the switch fell between the snapshots around `start`
    return (f"no timing shift of {ticks:+d} at the snapshot at or before tick {start}, nor "
            f"one from a transition at the next")


def first_divergence(diffs: list[TickDiff], input_shift=()) -> int | None:
    """First tick with a real (non-timing) difference; an unexplained input shift is real
    from its start."""
    ticks = [d.tick for d in diffs if d.offsets]
    ticks += [seg[0] for seg in input_shift if input_shift_check(diffs, seg)]
    return min(ticks, default=None)


def report(diffs: list[TickDiff], input_shift=()) -> str:
    lines = []
    for seg in input_shift:
        start, ticks, end = (*seg, None)[:3]
        why = input_shift_check(diffs, seg)
        span = f"from tick {start}" + (f" to {end - 1}" if end is not None else " on")
        lines.append(f"input shift: the port's keys {span} come {ticks:+d} tick later, "
                     + (f"NOT explained ({why}): real" if why else
                        "explained by the timing shift there"))
    for d in diffs:
        state = "ok" if not d.offsets else f"{len(d.offsets)} bytes: " + ", ".join(d.fields())
        if d.shift:
            state += (f"  [timing: port one tick {'behind' if d.shift > 0 else 'ahead'}, "
                      f"{len(d.timing)} bytes match its tick {d.tick + d.shift}]")
        elif d.timing:
            notes = []
            if any(o in TICK_COUNT for o in d.timing):
                notes.append("tick counter set one tick apart")
            if TUNE_COUNTDOWN in d.timing:
                notes.append("tune.countdown frozen one tick apart")
            if any((o - ENTITIES) % ENTITY_SIZE in (OVERLAY_FRAMES, OVERLAY_FRAMES + 1)
                   and ENTITIES <= o < ENTITIES + ENTITY_COUNT * ENTITY_SIZE for o in d.timing):
                notes.append("overlay.frames at another frame phase")
            state += f"  [timing: {', '.join(notes)}]"
        skipped = "".join(f"  [ignored {name} {r.start:04X}..{r.stop - 1:04X}]" if r else
                          f"  [ignored {name}]" for r, name in d.ignored)
        lines.append(f"tick {d.tick:6d}  {state}{skipped}")
    timing = [d.tick for d in diffs if d.shift or d.timing]
    if timing:
        lines.append(f"timing shifts (one tick, at hook switches / tick counter jumps): ticks "
                     + ", ".join(map(str, timing)))
    first = first_divergence(diffs, input_shift)
    lines.append("identical on every common tick (apart from timing)" if first is None
                 else f"first divergence at tick {first}")
    return "\n".join(lines)


def main(argv=None) -> int:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "harness" / "dosbox"))
    import refrun

    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("port", type=Path)
    ap.add_argument("probe_snp", type=Path)
    ap.add_argument("--ignore", nargs="*", default=[], help="hex DS offsets to skip")
    args = ap.parse_args(argv)
    port = read_port(args.port)
    ref = {s.irq: s.dgroup for s in refrun.read_snapshots(args.probe_snp)}
    diffs = compare(port, ref, [int(x, 16) for x in args.ignore])
    print(report(diffs))
    return 1 if first_divergence(diffs) is not None else 0


if __name__ == "__main__":
    sys.exit(main())
