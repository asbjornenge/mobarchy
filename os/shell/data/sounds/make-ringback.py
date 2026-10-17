#!/usr/bin/env python3
"""Makes ringback.wav: the tone a caller hears while the other phone rings (CEPT/ETSI, as in
Norway and most of Europe): 425 Hz, 1 s on, 4 s off; it loops. A pure sine with short ramps
(no clicks), at a modest level: it plays at the ear."""
import math
import struct
import wave

RATE, ON, OFF, RAMP = 16000, 1.0, 4.0, 0.01
on = int(RATE * ON)
frames = []
for i in range(int(RATE * (ON + OFF))):
    if i < on:
        t = i / RATE
        env = min(1.0, t / RAMP, (ON - t) / RAMP)
        frames.append(int(0.3 * env * math.sin(2 * math.pi * 425 * t) * 32767))
    else:
        frames.append(0)
with wave.open("ringback.wav", "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(b"".join(struct.pack("<h", f) for f in frames))
