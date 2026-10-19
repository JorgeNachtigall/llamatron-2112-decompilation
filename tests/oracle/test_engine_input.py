"""W2 input: game86 irq09_keyboard 1000:e567, input_poll 1e7b:0000 and the fire refresh at the
start of the timer tick (1000:3e12..3e4e), against src/engine/input.c through
llamatron_game_testlib.

Keyboard: random byte streams (the five game keys, their releases and other codes) go through
the INT 09 handler, each followed by a frame read and a tick refresh in random order, so the
port's tap-retention deviation (a release before the key was sampled, input.c) never applies.
Joystick: port 201h is a fake game port whose axis bits stay high for a given number of reads
after the trigger write, with random centres calibrated as 1000:6014 does, random positions and
buttons, one or two sticks, and the title's "read both sticks" word [9d3].
"""

import ctypes
import struct

from generators import case_rng, fuzz_cases

from adapters.cport import bind

KBD_LAST_MAKE, KBD_LAST_SCAN, KBD_HELD = 0x0586, 0x0587, 0x0588
KBD_LAST_MAKE_COPY = 0x0B72
SINGLE_STICK, KEYBOARD_MODE = 0x058C, 0x058D
THRESHOLDS = 0x058E                      # 8 words: per axis low, high (x1, y1, x2, y2)
INPUT_B, INPUT_A, JOY_BUTTONS = 0x07FB, 0x07FC, 0x07FD
BOTH_STICKS = 0x09D3
CODE0, CS_SAVED_DS = 0x1000, 0x0255    # the handler loads DS from here (set by game86 startup)
GAME_KEYS = [0x39, 0x4B, 0x4D, 0x48, 0x50]
DEVICE_KEYBOARD, DEVICE_JOYSTICK = 0, 1


class Calibration(ctypes.Structure):
    _fields_ = [("x_low", ctypes.c_uint16), ("x_high", ctypes.c_uint16),
                ("y_low", ctypes.c_uint16), ("y_high", ctypes.c_uint16)]


class Reading(ctypes.Structure):
    _fields_ = [("count_x", ctypes.c_uint16), ("count_y", ctypes.c_uint16),
                ("button_a", ctypes.c_bool), ("button_b", ctypes.c_bool)]


class InputState(ctypes.Structure):
    _fields_ = [("device", ctypes.c_int), ("single_stick", ctypes.c_bool),
                ("keys_held", ctypes.c_uint8), ("last_scan_code", ctypes.c_uint8),
                ("last_make_code", ctypes.c_uint8), ("last_make_code_copy", ctypes.c_uint8),
                ("unseen_by_frame", ctypes.c_uint8), ("unseen_by_tick", ctypes.c_uint8),
                ("release_pending", ctypes.c_uint8),
                ("controls", ctypes.c_uint8 * 2), ("joystick_buttons", ctypes.c_uint8),
                ("calibration", Calibration * 2), ("sticks", Reading * 2)]


class C:
    def __init__(self, lib):
        self.init = bind(lib, "input_init", None, ctypes.c_int, ctypes.c_bool)
        self.state = bind(lib, "input_state", ctypes.POINTER(InputState))
        self.calibrate = bind(lib, "input_calibrate", Calibration, ctypes.c_uint16, ctypes.c_uint16)
        self.byte = bind(lib, "input_keyboard_byte", None, ctypes.c_uint8)
        self.frame_read = bind(lib, "input_frame_read", None, ctypes.c_bool)
        self.tick_fire = bind(lib, "input_tick_fire", None)


class GamePort:
    """Port 201h: buttons active low in bits 4..7; after a write, axis bit i reads 1 for the
    next counts[i] reads (the one-shot timing the original counts in its loop)."""

    def __init__(self, machine):
        self.counts, self.buttons = [0, 0, 0, 0], 0
        self.left = [0, 0, 0, 0]
        ega_out = machine.ega.port_out

        def port_out(port, value, size):
            if port == 0x201:
                self.left = list(self.counts)
            ega_out(port, value, size)
        machine.ega.port_out = port_out
        machine.io_read = self.read

    def read(self, port, size):
        if port != 0x201:
            return 0xFF
        value = ~self.buttons & 0xF0
        for i in range(4):
            if self.left[i] > 0:
                value |= 1 << i
                self.left[i] -= 1
        return value


def keyboard_byte(machine, byte):
    machine.io_read = lambda port, size: byte if port == 0x60 else 0xFF
    machine.run_until("1000:e567", "1000:e5eb")          # up to the BIOS chain


def frame_read(machine):
    machine.call("1e7b:0000")


def tick_fire(machine):
    machine.run_until("1000:3e12", "1000:3e4e")


def check_keyboard(machine, st, where):
    m = machine.read_dgroup
    assert m(KBD_LAST_MAKE, 1)[0] == st.last_make_code, where
    assert m(KBD_LAST_SCAN, 1)[0] == st.last_scan_code, where
    assert m(KBD_HELD, 1)[0] == st.keys_held, where
    assert m(KBD_LAST_MAKE_COPY, 1)[0] == st.last_make_code_copy, where
    assert m(INPUT_A, 1)[0] == st.controls[0], where
    assert m(INPUT_B, 1)[0] == st.controls[1], where


def random_byte(rnd):
    kind = rnd.randrange(10)
    if kind < 4:
        return rnd.choice(GAME_KEYS)
    if kind < 8:
        return rnd.choice(GAME_KEYS) | 0x80
    return rnd.randrange(256)


def test_keyboard_stream(machine, cgame):
    c = C(cgame)
    for case in fuzz_cases(40):
        rnd = case_rng("engine_input_keyboard", case)
        c.init(DEVICE_KEYBOARD, False)
        st = c.state().contents
        machine.reset()
        machine.write_u16(CODE0, CS_SAVED_DS, machine.dgroup)
        machine.write_dgroup(KEYBOARD_MODE, b"\x01")
        start = [rnd.randrange(256) for _ in range(4)]
        machine.write_dgroup(KBD_LAST_MAKE, bytes(start[:2]))
        machine.write_dgroup(KBD_LAST_MAKE_COPY, bytes(start[2:3]))
        machine.write_dgroup(INPUT_B, bytes([start[3]]))
        st.last_make_code, st.last_scan_code = start[0], start[1]
        st.last_make_code_copy, st.controls[1] = start[2], start[3]
        for step in range(30):
            byte = random_byte(rnd)
            keyboard_byte(machine, byte)
            c.byte(byte)
            check_keyboard(machine, st, f"case {case} step {step} byte {byte:02x} (handler)")
            if rnd.randrange(2):
                frame_read(machine), c.frame_read(False)
                tick_fire(machine), c.tick_fire()
            else:
                tick_fire(machine), c.tick_fire()
                frame_read(machine), c.frame_read(False)
            assert st.unseen_by_frame == 0 and st.unseen_by_tick == 0 and st.release_pending == 0
            check_keyboard(machine, st, f"case {case} step {step} byte {byte:02x} (sampled)")


def axis_count(rnd, low, high):
    """A position near a threshold, at either extreme, or anywhere."""
    return max(0, rnd.choice([low - 1, low, low + 1, high - 1, high, high + 1,
                              0, rnd.randrange(2 * high + 2)]))


def test_calibration_matches(machine, cgame):
    c = C(cgame)
    for case in fuzz_cases(40):
        rnd = case_rng("engine_input_calibrate", case)
        centres = [rnd.choice([0, 1, 2, 3, rnd.randrange(0x1000), rnd.randrange(0x10000)])
                   for _ in range(4)]
        machine.reset()
        regs = dict(zip(("si", "di", "bx", "bp"), centres))
        machine.run_until("1000:6014", "1000:6064", regs=regs)
        got = struct.unpack("<8H", machine.read_dgroup(THRESHOLDS, 16))
        for stick in range(2):
            cal = c.calibrate(centres[2 * stick], centres[2 * stick + 1])
            assert got[4 * stick:4 * stick + 4] == (cal.x_low, cal.x_high, cal.y_low, cal.y_high), \
                f"case {case} centres {centres}"


def test_joystick_frame_and_tick(machine, cgame):
    c = C(cgame)
    port = GamePort(machine)
    for case in fuzz_cases(60):
        rnd = case_rng("engine_input_joystick", case)
        single = rnd.randrange(2) == 1
        both = rnd.randrange(3) == 0
        c.init(DEVICE_JOYSTICK, single)
        st = c.state().contents
        machine.reset()
        machine.write_dgroup(KEYBOARD_MODE, b"\x00")
        machine.write_dgroup(SINGLE_STICK, bytes([single]))
        machine.write_dgroup(BOTH_STICKS, struct.pack("<H", rnd.randrange(1, 0x10000) if both else 0))
        machine.write_dgroup(INPUT_A, bytes([rnd.randrange(256)]))
        machine.write_dgroup(INPUT_B, bytes([rnd.randrange(256)]))
        words = []
        for stick in range(2):
            cal = c.calibrate(rnd.randrange(4, 400), rnd.randrange(4, 400))
            st.calibration[stick] = cal
            words += [cal.x_low, cal.x_high, cal.y_low, cal.y_high]
        machine.write_dgroup(THRESHOLDS, struct.pack("<8H", *words))
        counts = []
        for stick in range(2):
            cal = st.calibration[stick]
            counts += [axis_count(rnd, cal.x_low, cal.x_high), axis_count(rnd, cal.y_low, cal.y_high)]
            st.sticks[stick].count_x, st.sticks[stick].count_y = counts[-2], counts[-1]
        buttons = rnd.randrange(16) << 4
        for stick in range(2):
            st.sticks[stick].button_a = bool(buttons & (0x10 << 2 * stick))
            st.sticks[stick].button_b = bool(buttons & (0x20 << 2 * stick))
        port.counts, port.buttons = counts, buttons
        machine.io_read = port.read

        where = f"case {case} single={single} both={both} counts={counts} buttons={buttons:02x}"
        frame_read(machine)
        c.frame_read(both)
        m = machine.read_dgroup
        assert m(INPUT_A, 1)[0] == st.controls[0], where
        assert m(INPUT_B, 1)[0] == st.controls[1], where
        assert m(JOY_BUTTONS, 1)[0] == st.joystick_buttons, where

        buttons = rnd.randrange(16) << 4                   # buttons change before the tick
        for stick in range(2):
            st.sticks[stick].button_a = bool(buttons & (0x10 << 2 * stick))
            st.sticks[stick].button_b = bool(buttons & (0x20 << 2 * stick))
        port.buttons = buttons
        port.left = [0, 0, 0, 0]                           # a tick later the one-shots ran out
        tick_fire(machine)
        c.tick_fire()
        assert m(INPUT_A, 1)[0] == st.controls[0], where + " (tick)"
        assert m(INPUT_B, 1)[0] == st.controls[1], where + " (tick)"
        assert m(JOY_BUTTONS, 1)[0] == st.joystick_buttons, where + " (tick)"
