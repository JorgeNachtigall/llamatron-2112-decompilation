"""W2 sound driver: src/engine/snd.c against the original sblaster.bin (tests/oracle/sb_driver.py)
on random call sequences.

Between game ticks (AH=2, 50.159 Hz) the game requests (AH=3), stops (AH=4, AH=5) and asks
for the status (AH=6) at any time; a sample ends at its exact time (the card's IRQ), between
ticks. Both sides get the same calls at the same times: the port is told the time inside the
tick (snd_set_phase) before each call, as the shell and the replay driver do before a game
frame. Compared:
  * every AH=6 answer (AH = sample playing, AL = priority) with snd_status();
  * after every tick the driver's variables [B2] pending request, [B3] pending stop, [B8]
    current sample, [2AE] time constant (once a request set it), [2AF] / [2B0] priorities,
    with snd_driver_state();
  * the transfers the driver started (DSP 14h: time, bytes, time constant) with the samples
    snd.c started, joined across a 64 KB page split.
"""

import ctypes

import pytest
from generators import case_rng, fuzz_cases

from adapters.cport import bind
from sb_driver import TICK_UNITS, SoundBlasterDriver, driver_files

DRIVER_SEG = 0x7000
TC_TABLE = 0x3D2                    # rate index -> DSP time constant (0112h XLAT)
SAMPLE_TABLE = 0x609                # 34 x {u32 offset, u16 length, u16 loop start, u16 loop len}
TICKS = 300


class SoundBank(ctypes.Structure):
    _fields_ = [("pcm", ctypes.c_void_p), ("pcm_size", ctypes.c_size_t),
                ("samples", (ctypes.c_uint32 * 2) * 32), ("time_constants", ctypes.c_uint8 * 5)]


class SoundStatus(ctypes.Structure):
    _fields_ = [("playing", ctypes.c_uint8), ("priority", ctypes.c_uint8)]


class SoundStats(ctypes.Structure):
    _fields_ = [("requests", ctypes.c_uint32), ("started", ctypes.c_uint32)]


class SoundDriverState(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint8) for name in ("pending_sample", "pending_stop", "current_sample",
                                                    "time_constant", "current_priority",
                                                    "requested_priority")]


class PortDriver:
    def __init__(self, lib, files):
        self.init = bind(lib, "snd_init", None, ctypes.POINTER(SoundBank), ctypes.c_int)
        self.play = bind(lib, "snd_play", None, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8)
        self.stop = bind(lib, "snd_stop", None, ctypes.c_uint8)
        self.stop_all = bind(lib, "snd_stop_all", None)
        self.status = bind(lib, "snd_status", SoundStatus)
        self.tick = bind(lib, "snd_tick", None)
        self.set_phase = bind(lib, "snd_set_phase", None, ctypes.c_uint16)
        self.state = bind(lib, "snd_driver_state", SoundDriverState)
        self.stats = bind(lib, "snd_stats", SoundStats)
        # The bank as the driver describes it (its table at 0609h, time constants at 03D2h), so
        # both sides play the same lengths; the extracted bank is the extractor's to test.
        drv, spl = files
        self.pcm = ctypes.create_string_buffer(spl, len(spl))
        self.bank = SoundBank(ctypes.cast(self.pcm, ctypes.c_void_p), len(spl))
        for n in range(1, 33):
            entry = drv[SAMPLE_TABLE + (n + 1) * 10:SAMPLE_TABLE + (n + 2) * 10]
            self.bank.samples[n - 1][0] = int.from_bytes(entry[0:4], "little")
            self.bank.samples[n - 1][1] = int.from_bytes(entry[4:6], "little")
        for i in range(5):
            self.bank.time_constants[i] = drv[TC_TABLE + i]

    def reset(self):
        self.init(ctypes.byref(self.bank), 0)

    def driver_state(self):
        s = self.state()
        return (s.pending_sample, s.pending_stop, s.current_sample, s.time_constant,
                s.current_priority, s.requested_priority)


@pytest.fixture(scope="module")
def files():
    found = driver_files()
    if found is None:
        pytest.skip("sblaster.bin / llatron.spl not in the original folder")
    return found


@pytest.fixture(scope="module")
def port(cgame, files):
    return PortDriver(cgame, files)


def random_calls(rnd, current: int):
    """The calls of one tick interval: (phase 0..65535, ah, al, cx), in time order."""
    calls = []
    for _ in range(rnd.choice([0, 0, 0, 1, 1, 2, 3])):
        kind = rnd.random()
        if kind < 0.55:
            sample = rnd.choice([rnd.randrange(1, 33)] * 8 + [0, 33, 0xFF, current or 1])
            rate = rnd.randrange(5)
            priority = rnd.choice([0, 1, 2, 3, 4, 5, 6, 8, 0x0A, 0x0C, 0x1E, rnd.randrange(256)])
            calls.append((rnd.randrange(65536), 3, sample, priority << 8 | rate))
        elif kind < 0.7:
            calls.append((rnd.randrange(65536), 4, rnd.choice([current, rnd.randrange(34), 0]), 0))
        elif kind < 0.75:
            calls.append((rnd.randrange(65536), 5, 0, 0))
        else:
            calls.append((rnd.randrange(65536), 6, 0, 0))
    return sorted(calls)


def test_driver_matches_original(machine, files, port):
    table = files[0][SAMPLE_TABLE:SAMPLE_TABLE + 34 * 10]
    for case in fuzz_cases(25):
        rnd = case_rng("snd_driver", case)
        machine.reset()
        drv = SoundBlasterDriver(machine, DRIVER_SEG, files)
        drv.init_state()
        port.reset()
        requested = False
        for tick in range(TICKS):
            t0 = tick * TICK_UNITS
            for phase, ah, al, cx in random_calls(rnd, port.status().playing):
                t = t0 - TICK_UNITS + phase * TICK_UNITS // 65536    # after the previous tick
                ax = drv.call(t, ah, al, cx) if tick else None
                if tick == 0:
                    continue            # nothing has ticked yet: both sides idle
                port.set_phase(phase)
                where = f"case {case} tick {tick} phase {phase} AH={ah:x} AL={al:02x} CX={cx:04x}"
                if ah == 3:
                    port.play(al, cx & 0xFF, cx >> 8)
                    requested = True
                elif ah == 4:
                    port.stop(al)
                elif ah == 5:
                    port.stop_all()
                elif ah == 6:
                    s = port.status()
                    assert (ax >> 8, ax & 0xFF) == (s.playing, s.priority), where
            started = port.stats().started
            drv.irqs_until(t0)
            transfers = len(drv.started)
            drv.tick(t0)
            port.set_phase(0)
            port.tick()
            want, got = drv.state(), port.driver_state()
            if not requested:
                want, got = want[:3] + want[4:], got[:3] + got[4:]     # [2AE] starts at 9Ch
            assert got == want, f"case {case} after tick {tick}: port {got}, original {want}"
            # A transfer started at this tick is a sample start (0x38C); its bytes are the
            # sample's length, or the first part of a page split (the rest follows at its IRQ).
            original_started = any(not irq for *_x, irq in drv.started[transfers:])
            assert (port.stats().started > started) == original_started, \
                f"case {case} tick {tick}: sample start port {not original_started}, original {original_started}"
            if original_started:
                sample = want[2]
                length = int.from_bytes(table[(sample + 1) * 10 + 4:(sample + 1) * 10 + 6], "little")
                n = drv.started[-1][1]
                assert n <= length, f"case {case}: sample {sample} started with {n} of {length} bytes"
