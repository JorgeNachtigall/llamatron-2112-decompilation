"""W2 M5: the game's sound calls in the whole-game lockstep (tests/oracle/whole_game.py), game86
with the original Sound Blaster driver (tests/oracle/sb_driver.py) against the port with
snd.c.

game86 gets the driver as llama.exe passes it for llama.inf's S (PSP:89h = 0Ch, PSP:8Ah..8Dh
its entry), so every CALLF [0400] (requests AH=3, stops AH=4/5, status AH=6, the tick's AH=2
from the INT 08 handler 1000:3e02) runs the original driver; the card's IRQ comes at its
exact time, before the tick or frame it falls ahead of. The port runs snd_tick() before each
game_tick (timing.c) and snd_set_phase(the tick phase) before each game_frame, as the shell
does. After every tick and frame, on top of the DGROUP comparison:
  * the AH=3 calls the original made (AL sample, CL rate, CH priority) equal the port's
    game_sound_play log since the last check;
  * the driver's [B2] [B3] [B8] [2AE] [2AF] [2B0] equal snd_driver_state() ([2AE] once a
    request set it; it starts at 9Ch).
Both busy waits for the driver (065d warp, 0924 game over) poll after a wait_vsync, so each
poll is a frame on both sides (main_sm.c warp_sound, game_over_sound).

The bank on the port side is the driver's own table (test_snd_driver.PortDriver), so both play
the same lengths. Skips without game86, the testlib, sblaster.bin or llatron.spl.
"""

from __future__ import annotations

import ctypes

import pytest
import unicorn
import unicorn.x86_const as X

from adapters.cport import bind
from llamatron_harness.runner import linear
from sb_driver import TICK_UNITS, SoundBlasterDriver, driver_files
from test_snd_driver import PortDriver
from whole_game import Result, describe, load_reference_run, run_lockstep

DRIVER_SEG = 0x7000                 # free in game86's 1 MB (image 1000..47E0, VRAM A000)
AH_REQUEST = 3
# The runs: requests in play, the death / continue screens, the warp's busy wait (065d), game
# over's (0924) and the hi-score entry, two players.
RUNS = ["walk_vga", "death_continue", "warp", "game_over_hiscore", "two_player"]


class SoundRequest(ctypes.Structure):
    _fields_ = [("sample", ctypes.c_uint8), ("rate_index", ctypes.c_uint8),
                ("priority", ctypes.c_uint8)]


class SoundLockstep:
    """whole_game.run_lockstep's `sound`."""

    def __init__(self, lib, files):
        self.files = files
        self.port = PortDriver(lib, files)
        self.log_count = bind(lib, "game_sound_log_count", ctypes.c_uint)
        self.log_at = bind(lib, "game_sound_log_at", SoundRequest, ctypes.c_uint)
        self.log_clear = bind(lib, "game_sound_log_clear", None)
        self.drv: SoundBlasterDriver | None = None
        self.calls: list[tuple[int, int, int]] = []     # original AH=3: AL, CL, CH
        self.requested = False
        self.requests = 0
        self.started = 0

    def load(self, machine):
        self.drv = SoundBlasterDriver(machine, DRIVER_SEG, self.files)
        self.drv.init_state()
        machine.uc.hook_add(unicorn.UC_HOOK_CODE, self._on_call, None,
                            linear(DRIVER_SEG, 0), linear(DRIVER_SEG, 0))
        self.port.reset()
        self.log_clear()
        return DRIVER_SEG, 0

    def _on_call(self, uc, _address, _size, _user):
        ax, cx = uc.reg_read(X.UC_X86_REG_AX), uc.reg_read(X.UC_X86_REG_CX)
        if ax >> 8 == AH_REQUEST:
            self.calls.append((ax & 0xFF, cx & 0xFF, cx >> 8))

    @staticmethod
    def tick_time(tick: int) -> int:
        """Driver time units of tick n (1, 2, ...); the first tick is at 0, as snd.c counts."""
        return (tick - 1) * TICK_UNITS

    def before_tick(self, orig, port, tick):
        self.drv.irqs_until(self.tick_time(tick))
        self.port.tick()                # timing.c: snd_tick() before game_tick (3e02 AH=2)

    def before_frame(self, orig, port, tick, phase):
        if tick:
            self.drv.irqs_until(self.tick_time(tick) + phase * TICK_UNITS // 65536)
        self.port.set_phase(phase)

    def check(self, orig, port, tick, frame, where):
        calls = [(r.sample, r.rate_index, r.priority)
                 for r in (self.log_at(i) for i in range(self.log_count()))]
        self.log_clear()
        original, self.calls = self.calls, []
        if calls != original:
            return Result(False, tick, frame, "sound",
                          detail=f"after {where}: AH=3 calls original {original}, port {calls}")
        self.requests += len(calls)
        self.requested = self.requested or bool(calls)
        want, got = self.drv.state(), self.port.driver_state()
        if not self.requested:
            want, got = want[:3] + want[4:], got[:3] + got[4:]
        if want != got:
            return Result(False, tick, frame, "sound",
                          detail=f"after {where}: driver [B2 B3 B8 2AE 2AF 2B0] original "
                                 f"{[f'{v:02x}' for v in want]}, port {[f'{v:02x}' for v in got]}")
        self.started = self.port.stats().started
        return None


@pytest.fixture(scope="module")
def files():
    found = driver_files()
    if found is None:
        pytest.skip("sblaster.bin / llatron.spl not in the original folder")
    return found


@pytest.mark.parametrize("run", RUNS)
def test_whole_game_sound(cgame, game86_path, files, run):
    script, ticks, cfg = load_reference_run(run)
    sound = SoundLockstep(cgame, files)
    res = run_lockstep(cgame, script, ticks, cfg["ticks"], cfg.get("machine", "vga"),
                       game86=game86_path, sound=sound)
    if res.where == "sound":
        pytest.fail(f"{run}: sound differs at tick {res.tick} frame {res.frame}: {res.detail}")
    assert res.ok, f"{run}: {describe(res)}"
    assert sound.requests > 0 and sound.started > 0, f"{run}: no sound was played"
