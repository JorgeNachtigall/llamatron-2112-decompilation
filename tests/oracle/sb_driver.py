"""The original Sound Blaster driver (sblaster.bin, with llatron.spl behind it) running in a
harness Machine, with just enough of a Sound Blaster around it to play samples in time.

Test-oracle code only (docs/PLAN.md §0.1): the driver and the sample bank are read from the
user's original folder at run time ($LLAMATRON_ORIGINAL_DIR, else original/), never stored.

Layout as llama.exe makes it (docs/re-notes-sound-data.md §1): the driver at seg:0000 and the
bank at seg:0767h. game86 calls it with CALLF (entry 0000: CALL 0076h; RETF). llama.exe's
AH=0 (init, 01A2h) detects the card; game86 never calls it, so `init_state` sets what a
successful init leaves (port table 0201h..0232h for base 220h, IRQ 7, functions up to 6
enabled at 024Dh) instead of emulating the detection.

The card model: DSP commands are bytes written to 22Ch (03DEh; the write-status read always
says ready). 40h sets the time constant, D0h halts the transfer (no IRQ follows), 14h plus a
16-bit count starts an 8-bit DMA transfer of count + 1 bytes, which ends with an IRQ after
(count + 1) * (256 - tc) microseconds. The IRQ runs the driver's handler (04FAh): it starts
the second half of a sample split at a 64 KB DMA page (0511h..0545h) or marks the sample
finished (0553h). Of the DMA controller only channel 1's address (02h, low then high byte after a
flip-flop clear on 0Ch) and page (83h) are kept, so `played` knows the bytes each transfer
sends to the DAC (`card_output`); PIC writes are ignored.

Time is counted in units of 1/315 us, like src/engine/snd.c: a game tick (PIT divisor 5CECh)
is 0x5CEC * 264 units.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

from unicorn import UC_HOOK_INSN
from unicorn import x86_const as X

from fs_oracle import original_dir
from llamatron_harness.runner import linear

UNITS_PER_US = 315
UNITS_PER_SECOND = UNITS_PER_US * 1_000_000
SILENCE = 0x80                      # unsigned 8-bit centre: the DAC between transfers
DMA_CLEAR_FLIP_FLOP, DMA1_ADDRESS, DMA1_PAGE = 0x0C, 0x02, 0x83
TICK_UNITS = 0x5CEC * 264
BANK_OFFSET = 0x767                 # llatron.spl right after the driver (llama.exe 1000:03c8)
DSP_WRITE = 0x22C
IRQ = 7
# Driver variables (re-notes-sound-data.md §1, "Driver variables").
PENDING_SAMPLE, PENDING_STOP, CURRENT_SAMPLE = 0xB2, 0xB3, 0xB8
TIME_CONSTANT, CURRENT_PRIORITY, REQUESTED_PRIORITY = 0x2AE, 0x2AF, 0x2B0
MAX_FUNCTION, IRQ_NUMBER, DMA_BUSY = 0xA1, 0x2A6, 0x2A9
PORT_TABLE = 0x2BD
IRQ_HANDLER = 0x4FA
STATE_BYTES = (PENDING_SAMPLE, PENDING_STOP, CURRENT_SAMPLE, TIME_CONSTANT, CURRENT_PRIORITY,
               REQUESTED_PRIORITY)


def driver_files() -> tuple[bytes, bytes] | None:
    """(sblaster.bin, llatron.spl) from the user's original folder, or None."""
    folder = original_dir()
    drv, spl = folder / "sblaster.bin", folder / "llatron.spl"
    if not drv.exists() or not spl.exists():
        return None
    return drv.read_bytes(), spl.read_bytes()


@dataclass
class Transfer:
    end: int            # units
    bytes: int


@dataclass
class Played:
    """One DSP 14h transfer: `bytes` bytes from linear `address`, each held (256 - tc) us from
    `start`, cut at `end` by a halt (D0h)."""
    start: int          # units
    bytes: int
    tc: int
    address: int
    end: int

    def byte_units(self) -> int:
        return (256 - self.tc) * UNITS_PER_US


class SoundBlasterDriver:
    """sblaster.bin at `seg` in `machine` (a harness Machine), plus the card model."""

    def __init__(self, machine, seg: int, files: tuple[bytes, bytes],
                 stack: tuple[int, int] = (0x0050, 0x0F00), sentinel: tuple[int, int] = (0x0040, 0x00F0)):
        self.m = machine
        self.seg = seg
        self.stack = stack
        self.sentinel = sentinel
        drv, spl = files
        self.m.write(seg, 0, drv)
        # The bank is longer than a segment: write it linearly.
        self.m.uc.mem_write(linear(seg, 0) + BANK_OFFSET, spl)
        self.now = 0                    # units: the time of what runs next
        self.transfer: Transfer | None = None
        self.started: list[tuple[int, int, int, bool]] = []  # DSP 14h: time, bytes, tc, from the IRQ
        self.played: list[Played] = []
        self._dma_address = self._dma_page = 0
        self._dma_high = False
        self._in_irq = False
        self._dsp: list[int] = []
        self._tc = 0
        self.m.uc.hook_add(UC_HOOK_INSN, self._on_out, None, 1, 0, X.UC_X86_INS_OUT)
        reads = self.m.io_read

        def io_read(port: int, size: int) -> int:
            if 0x220 <= port <= 0x22F:
                return 0                # DSP ready (03E3h), nothing to read
            if port == 0x21:
                return 0
            return reads(port, size)
        self.m.io_read = io_read

    # ---- state ------------------------------------------------------------------------------

    def init_state(self) -> None:
        """What AH=0 leaves after a successful detection at 220h (01A2h..024Dh)."""
        ports = [0x220, 0x221, 0x222, 0x223, 0x224, 0x225, 0x226, 0x22C, 0x22A, 0x22E]
        for i, port in enumerate(ports):        # 020Ah..0232h: [2CB] = 22C, [2CD] = 22A
            self.m.write_u16(self.seg, PORT_TABLE + 2 * i, port)
        self.m.write(self.seg, IRQ_NUMBER, bytes([IRQ]))
        self.m.write(self.seg, MAX_FUNCTION, bytes([6]))

    def byte(self, off: int) -> int:
        return self.m.read(self.seg, off, 1)[0]

    def state(self) -> tuple[int, ...]:
        return tuple(self.byte(off) for off in STATE_BYTES)

    # ---- the card --------------------------------------------------------------------------

    def _on_out(self, uc, port: int, size: int, value: int, _user) -> None:
        if port == DMA_CLEAR_FLIP_FLOP:
            self._dma_high = False
        elif port == DMA1_ADDRESS:
            shift = 8 if self._dma_high else 0
            self._dma_address = self._dma_address & ~(0xFF << shift) | (value & 0xFF) << shift
            self._dma_high = not self._dma_high
        elif port == DMA1_PAGE:
            self._dma_page = value & 0xFF
        if port != DSP_WRITE:
            return
        self._dsp.append(value & 0xFF)
        cmd = self._dsp[0]
        if cmd == 0x40 and len(self._dsp) == 2:
            self._tc = self._dsp[1]
        elif cmd == 0xD0:
            self.transfer = None        # halted: no IRQ
            if self.played:
                self.played[-1].end = min(self.played[-1].end, self.now)
        elif cmd == 0x14 and len(self._dsp) == 3:
            count = self._dsp[1] | self._dsp[2] << 8
            n = count + 1
            self.transfer = Transfer(self.now + n * (256 - self._tc) * UNITS_PER_US, n)
            self.started.append((self.now, n, self._tc, self._in_irq))
            address = self._dma_page << 16 | self._dma_address
            self.played.append(Played(self.now, n, self._tc, address, self.transfer.end))
        elif cmd in (0x40, 0x14):
            return                      # wait for the operands
        self._dsp = []

    def irqs_until(self, t: int) -> None:
        """Raise the IRQs of every transfer that ends at or before time t. The handler runs on
        its own stack and the registers are put back, so a program stopped in the same machine
        (tests/oracle/whole_game.py) goes on as after the handler's IRET."""
        while self.transfer is not None and self.transfer.end <= t:
            self.now = self.transfer.end
            self.transfer = None
            self._in_irq = True
            regs = self.m.get_regs()
            try:
                self._run((self.seg, IRQ_HANDLER), interrupt=True)
            finally:
                self._in_irq = False
                self.m.set_regs(regs)
        self.now = max(self.now, t)

    def card_output(self, rate: int, start: int, count: int) -> bytes:
        """What the DAC plays at output samples start .. start + count - 1 of `rate` per
        second (sample k at time k * UNITS_PER_SECOND // rate, as src/engine/snd.c samples)."""
        out = bytearray([SILENCE]) * count
        for p in self.played:
            data = bytes(self.m.uc.mem_read(p.address, p.bytes))
            per = p.byte_units()
            first = max(start, -(-p.start * rate // UNITS_PER_SECOND))
            for k in range(first, start + count):
                t = k * UNITS_PER_SECOND // rate
                if t >= p.end:
                    break
                if t >= p.start:
                    out[k - start] = data[(t - p.start) // per]
        return bytes(out)

    # ---- calls -----------------------------------------------------------------------------

    def _run(self, entry: tuple[int, int], ax: int = 0, cx: int = 0, interrupt: bool = False) -> int:
        uc = self.m.uc
        ss, sp = self.stack
        frame = struct.pack("<HHH", self.sentinel[1], self.sentinel[0], 0x0202) if interrupt else \
            struct.pack("<HH", self.sentinel[1], self.sentinel[0])
        sp -= len(frame)
        self.m.write(ss, sp, frame)
        self.m.set_regs({"cs": entry[0], "ds": 0, "es": 0, "ss": ss, "sp": sp, "ax": ax, "cx": cx,
                         "bx": 0, "dx": 0, "si": 0, "di": 0, "flags": 0x0002, "ip": entry[1]})
        uc.emu_start(linear(*entry), linear(*self.sentinel), count=200_000)
        if linear(uc.reg_read(X.UC_X86_REG_CS), uc.reg_read(X.UC_X86_REG_IP)) != linear(*self.sentinel):
            raise RuntimeError(f"sblaster.bin call {entry} did not return")
        return uc.reg_read(X.UC_X86_REG_AX) & 0xFFFF

    def call(self, t: int, ah: int, al: int = 0, cx: int = 0) -> int:
        """CALLF [0400] at time t (after the IRQs due by then)."""
        self.irqs_until(t)
        return self._run((self.seg, 0), ax=ah << 8 | al, cx=cx)

    def tick(self, t: int) -> int:
        """The game's timer tick at time t: AH=2 (game86 1000:3e02)."""
        return self.call(t, 2)
