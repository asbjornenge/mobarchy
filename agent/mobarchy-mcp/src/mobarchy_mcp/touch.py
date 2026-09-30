"""A virtual touchscreen through Linux uinput.

It sits below the compositor, so it works the same with any compositor and on
any device (simulator or phone). Needs write access to /dev/uinput.
"""

from __future__ import annotations

import ctypes
import fcntl
import os
import struct
import time

# linux/input-event-codes.h
EV_SYN, EV_KEY, EV_ABS = 0x00, 0x01, 0x03
SYN_REPORT = 0
BTN_TOUCH = 0x14A
ABS_X, ABS_Y = 0x00, 0x01
ABS_MT_SLOT, ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TRACKING_ID = 0x2F, 0x35, 0x36, 0x39
INPUT_PROP_DIRECT = 0x01
BUS_VIRTUAL = 0x06

# linux/uinput.h ioctls
UI_DEV_CREATE = 0x5501
UI_DEV_DESTROY = 0x5502
UI_SET_EVBIT = 0x40045564
UI_SET_KEYBIT = 0x40045565
UI_SET_ABSBIT = 0x40045567
UI_SET_PROPBIT = 0x4004556E

ABS_CNT = 64
AXIS_MAX = 32767
SLOTS = 10


class _InputId(ctypes.Structure):
    _fields_ = [("bustype", ctypes.c_uint16), ("vendor", ctypes.c_uint16),
                ("product", ctypes.c_uint16), ("version", ctypes.c_uint16)]


class _UinputUserDev(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char * 80), ("id", _InputId), ("ff_effects_max", ctypes.c_uint32),
                ("absmax", ctypes.c_int32 * ABS_CNT), ("absmin", ctypes.c_int32 * ABS_CNT),
                ("absfuzz", ctypes.c_int32 * ABS_CNT), ("absflat", ctypes.c_int32 * ABS_CNT)]


class Touchscreen:
    """Coordinates are fractions (0..1) of the output that the compositor maps the device to."""

    def __init__(self, name: str = "Mobarchy Virtual Touch"):
        self.fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
        fcntl.ioctl(self.fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_KEY)
        fcntl.ioctl(self.fd, UI_SET_KEYBIT, BTN_TOUCH)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_ABS)
        dev = _UinputUserDev()
        dev.name = name.encode()
        dev.id = _InputId(BUS_VIRTUAL, 0x1209, 0x4D42, 1)
        for axis, maximum in ((ABS_X, AXIS_MAX), (ABS_Y, AXIS_MAX), (ABS_MT_SLOT, SLOTS - 1),
                              (ABS_MT_POSITION_X, AXIS_MAX), (ABS_MT_POSITION_Y, AXIS_MAX),
                              (ABS_MT_TRACKING_ID, 65535)):
            fcntl.ioctl(self.fd, UI_SET_ABSBIT, axis)
            dev.absmax[axis] = maximum
        os.write(self.fd, bytes(dev))
        fcntl.ioctl(self.fd, UI_DEV_CREATE)
        self._next_id = 1
        time.sleep(0.5)  # give udev and the compositor time to pick up the device

    def close(self) -> None:
        fcntl.ioctl(self.fd, UI_DEV_DESTROY)
        os.close(self.fd)

    def _emit(self, events: list[tuple[int, int, int]]) -> None:
        now = time.time()
        sec, usec = int(now), int((now % 1) * 1_000_000)
        data = b"".join(struct.pack("llHHi", sec, usec, t, c, v) for t, c, v in events)
        data += struct.pack("llHHi", sec, usec, EV_SYN, SYN_REPORT, 0)
        os.write(self.fd, data)

    @staticmethod
    def _axis(f: float) -> int:
        return round(min(max(f, 0.0), 1.0) * AXIS_MAX)

    def down(self, fx: float, fy: float, slot: int = 0) -> None:
        tid, self._next_id = self._next_id, self._next_id % 65535 + 1
        x, y = self._axis(fx), self._axis(fy)
        self._emit([(EV_ABS, ABS_MT_SLOT, slot), (EV_ABS, ABS_MT_TRACKING_ID, tid),
                    (EV_ABS, ABS_MT_POSITION_X, x), (EV_ABS, ABS_MT_POSITION_Y, y),
                    (EV_KEY, BTN_TOUCH, 1), (EV_ABS, ABS_X, x), (EV_ABS, ABS_Y, y)])

    def move(self, fx: float, fy: float, slot: int = 0) -> None:
        x, y = self._axis(fx), self._axis(fy)
        self._emit([(EV_ABS, ABS_MT_SLOT, slot), (EV_ABS, ABS_MT_POSITION_X, x),
                    (EV_ABS, ABS_MT_POSITION_Y, y), (EV_ABS, ABS_X, x), (EV_ABS, ABS_Y, y)])

    def up(self, slot: int = 0) -> None:
        self._emit([(EV_ABS, ABS_MT_SLOT, slot), (EV_ABS, ABS_MT_TRACKING_ID, -1), (EV_KEY, BTN_TOUCH, 0)])

    def tap(self, fx: float, fy: float, hold: float = 0.05) -> None:
        self.down(fx, fy)
        time.sleep(hold)
        self.up()

    def swipe(self, fx1: float, fy1: float, fx2: float, fy2: float, duration: float = 0.3) -> None:
        steps = max(2, int(duration / 0.016))
        self.down(fx1, fy1)
        for i in range(1, steps + 1):
            t = i / steps
            time.sleep(duration / steps)
            self.move(fx1 + (fx2 - fx1) * t, fy1 + (fy2 - fy1) * t)
        self.up()
