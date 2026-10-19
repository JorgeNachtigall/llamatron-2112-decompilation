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
from adapters.gamestate import ATTRACT_LLAMAS  # noqa: E402

# The reference run's phase, from its tick hook (g_tick_hook, not part of GameState).
TICK_HOOK = 0x39E4
TITLE_HOOKS = {0x3F6F, 0x084E}  # title_tick_hook, title_tune_tick
PLAY_HOOK = 0x1562              # play_tick_hook

# DEVIATIONS #10: the loaded image has trail_head[1] = 2 (not entry-aligned) until P2's
# first trail write; the port starts at entry 0. Skipped while the reference still holds 2.
P2_TRAIL_HEAD = 0x2CF9
P2_TRAIL_HEAD_LOADED = 2

# Named DGROUP regions for reports (docs/re-notes-game86.md §8).
_SCALARS = {  # DS offset: (size, name), src/game/state.h
    0x0429: (1, "frame_drawn"), 0x0453: (8, "rng.mixer"), 0x0589: (1, "flash_mode"),
    0x058A: (1, "vga"), 0x058B: (1, "joystick"), 0x058D: (1, "keyboard"),
    0x05A1: (2, "hud_dirty"), 0x0615: (2, "smart_bomb"), 0x061F: (2, "continues"),
    0x062F: (2, "enemies_alive"), 0x0634: (2, "respawn_timer[1]"), 0x0636: (4, "speed_accum"),
    0x063A: (2, "kills_toward_bonus"), 0x0650: (2, "shots_bounce"), 0x0652: (2, "powerup_0652"),
    0x0688: (1, "sound_on"), 0x0689: (1, "sound_present"), 0x06C4: (2, "demo_mode"),
    0x06C7: (2, "flash_list"), 0x07F3: (2, "respawn_timer[0]"), 0x0802: (2, "spawning_done"),
    0x0804: (2, "spawn_count_0804"), 0x0806: (2, "spawn_sprite"), 0x0810: (2, "wave_result"),
    0x0812: (2, "tuning.init01_mode"), 0x0814: (2, "spawn_phase"),
    0x0816: (2, "tuning.init0d_hp"), 0x0818: (2, "tuning.segment_family"),
    0x081A: (4, "orbit_angle"), 0x081E: (2, "orbit_next_phase"),
    0x0820: (2, "tuning.orbit_phase_step"), 0x0824: (2, "segment_sprite"),
    0x0826: (2, "tuning.p0826"), 0x0828: (4, "tuning.accum_start"), 0x082C: (2, "tuning.p082c"),
    0x082E: (2, "tuning.p082e"), 0x0830: (2, "tuning.p0830"), 0x086E: (2, "tuning.init19_vx"),
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
    0x07F5: (2, "player_label_offset"), 0x07FE: (2, "spawn_delay"), 0x0617: (2, "play_flag_0617"),
    0x059F: (2, "level_default"), 0x0902: (2, "pulse_enable"),
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
class TickDiff:
    tick: int
    offsets: list[int]
    ignored: list[tuple[range, str]] = field(default_factory=list)

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
    head is skipped while the reference still holds its loaded value (DEVIATIONS #10)."""
    ignore = set(ignore)
    out = []
    attract = False
    for tick in sorted(set(port.snapshots) & set(reference)):
        a, b = reference[tick], port.snapshots[tick]
        hook = struct.unpack_from("<H", a, TICK_HOOK)[0]
        if hook in TITLE_HOOKS:
            attract = True
        elif hook == PLAY_HOOK:
            attract = False
        skipped = [(ATTRACT_LLAMAS, "attract llamas")] if attract else []
        if struct.unpack_from("<H", a, P2_TRAIL_HEAD)[0] == P2_TRAIL_HEAD_LOADED:
            skipped.append((range(P2_TRAIL_HEAD, P2_TRAIL_HEAD + 2),
                            "trail_head[1] = 2 (DEVIATIONS #10)"))
        skip = set().union(*(r for r, _ in skipped))
        offs = [i for i in range(DGROUP_SIZE)
                if port.mask[i] and a[i] != b[i] and i not in ignore and i not in skip]
        out.append(TickDiff(tick, offs, skipped))
    return out


def report(diffs: list[TickDiff]) -> str:
    lines = []
    for d in diffs:
        state = "ok" if not d.offsets else f"{len(d.offsets)} bytes: " + ", ".join(d.fields())
        skipped = "".join(f"  [ignored {name} {r.start:04X}..{r.stop - 1:04X}]"
                          for r, name in d.ignored)
        lines.append(f"tick {d.tick:6d}  {state}{skipped}")
    first = next((d.tick for d in diffs if d.offsets), None)
    lines.append("identical on every common tick" if first is None
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
    return 1 if any(d.offsets for d in diffs) else 0


if __name__ == "__main__":
    sys.exit(main())
