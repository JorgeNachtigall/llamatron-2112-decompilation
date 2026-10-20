"""W2 M5: snd.c's output samples against what the Sound Blaster plays under the original
driver (tests/oracle/sb_driver.py), on random call sequences.

The card plays 8-bit unsigned mono: each byte the DMA fetches is held (256 - time constant) us
from the transfer's start, until the transfer ends or a halt (D0h) cuts it; between transfers
the DAC sits at 80h. card_output reads those bytes from the DMA address and page the driver
programmed (so a 64 KB page split plays its second half from the next page), and samples the
result at the host rate. snd.c renders a tick's output at snd_tick (unsigned 8-bit, SDL's
AUDIO_U8 mono, no volume scaling); every byte must equal the card's, at the rate the shell uses
(22050 Hz) and at others.

The call sequences are test_snd_driver's (requests, stops, status calls at random sub-tick
times); the bank on the port side is the driver's table. Sample 30 runs past the end of
llatron.spl; both sides get the extractor's silence there (DEVIATIONS #1): the port's bank is
padded and so is the memory after the file.
"""

import ctypes

import pytest
from generators import case_rng, fuzz_cases

from adapters.cport import bind
from llamatron_harness.runner import linear
from sb_driver import (BANK_OFFSET, SILENCE, TICK_UNITS, UNITS_PER_SECOND, SoundBlasterDriver,
                       driver_files)
from test_snd_driver import DRIVER_SEG, PortDriver, random_calls

TICKS = 300
RATES = [22050, 44100, 48000, 11025, 8000]


@pytest.fixture(scope="module")
def files():
    found = driver_files()
    if found is None:
        pytest.skip("sblaster.bin / llatron.spl not in the original folder")
    return found


@pytest.fixture(scope="module")
def port(cgame, files):
    driver = PortDriver(cgame, files)
    spl = files[1]
    end = max(off + length for off, length in driver.bank.samples)
    driver.padding = bytes([SILENCE]) * max(0, end - len(spl))
    driver.pcm = ctypes.create_string_buffer(spl + driver.padding, len(spl) + len(driver.padding))
    driver.bank.pcm = ctypes.cast(driver.pcm, ctypes.c_void_p)
    driver.bank.pcm_size = len(spl) + len(driver.padding)
    driver.take = bind(cgame, "snd_take_output", ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t)
    driver.init_rate = bind(cgame, "snd_init", None, ctypes.c_void_p, ctypes.c_int)
    return driver


def card(machine, files, port) -> SoundBlasterDriver:
    machine.reset()
    drv = SoundBlasterDriver(machine, DRIVER_SEG, files)
    drv.init_state()
    machine.uc.mem_write(linear(DRIVER_SEG, BANK_OFFSET) + len(files[1]), port.padding)
    return drv


def take_all(port) -> bytes:
    buf = ctypes.create_string_buffer(1 << 15)
    out = bytearray()
    while n := port.take(buf, len(buf)):
        out += buf.raw[:n]
    return bytes(out)


@pytest.mark.parametrize("rate", RATES)
def test_output_matches_card(machine, files, port, rate):
    played = 0
    for case in fuzz_cases(10):
        rnd = case_rng("snd_mixer", case)
        drv = card(machine, files, port)
        port.init_rate(ctypes.byref(port.bank), rate)
        got = b""
        for tick in range(TICKS):
            t0 = tick * TICK_UNITS
            for phase, ah, al, cx in random_calls(rnd, port.status().playing):
                if tick == 0:
                    continue            # nothing has ticked yet (test_snd_driver)
                drv.call(t0 - TICK_UNITS + phase * TICK_UNITS // 65536, ah, al, cx)
                port.set_phase(phase)
                if ah == 3:
                    port.play(al, cx & 0xFF, cx >> 8)
                elif ah == 4:
                    port.stop(al)
                elif ah == 5:
                    port.stop_all()
            drv.irqs_until(t0)
            drv.tick(t0)
            port.set_phase(0)
            port.tick()
            got += take_all(port)
        drv.irqs_until(TICKS * TICK_UNITS)  # the IRQs of the last window (page splits)
        end = TICKS * TICK_UNITS
        assert len(got) == -(-end * rate // UNITS_PER_SECOND), f"rate {rate}: {len(got)} samples"
        want = drv.card_output(rate, 0, len(got))
        if got != want:
            k = next(i for i, (a, b) in enumerate(zip(got, want)) if a != b)
            tick = k * UNITS_PER_SECOND // rate // TICK_UNITS
            pytest.fail(f"rate {rate} case {case}: output sample {k} (tick {tick}) "
                        f"port {got[k]:02x}, card {want[k]:02x}")
        played += sum(b != SILENCE for b in got)
    assert played > 0, "no sample was audible"


def test_each_sample_whole(machine, files, port):
    """Every sample once, played to its end at each rate index: with the bank at
    DRIVER_SEG:0767h one of them crosses the DMA page at 80000h, so its second half comes
    from the IRQ handler (0511h)."""
    rate = 22050
    for rate_index in range(5):
        drv = card(machine, files, port)
        port.init_rate(ctypes.byref(port.bank), rate)
        got = b""
        tick = 0
        for sample in range(1, 33):
            first = True
            while first or port.status().playing:
                t0 = tick * TICK_UNITS
                if first and tick:
                    drv.call(t0 - TICK_UNITS, 3, sample, 1 << 8 | rate_index)
                    port.set_phase(0)
                    port.play(sample, rate_index, 1)
                    first = False
                drv.irqs_until(t0)
                drv.tick(t0)
                port.set_phase(0)
                port.tick()
                got += take_all(port)
                tick += 1
        drv.irqs_until(tick * TICK_UNITS)
        assert any(irq for *_x, irq in drv.started), "no transfer was split at a DMA page"
        want = drv.card_output(rate, 0, len(got))
        if got != want:
            k = next(i for i, (a, b) in enumerate(zip(got, want)) if a != b)
            pytest.fail(f"rate index {rate_index}: output sample {k} "
                        f"(tick {k * UNITS_PER_SECOND // rate // TICK_UNITS}) "
                        f"port {got[k]:02x}, card {want[k]:02x}")
