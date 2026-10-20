#!/usr/bin/env python3
"""Makes ringtone.wav: a call coming in. A short bright phrase (G5 C6 E6 G6, then E6 G6) of
bell-like notes, and a pause; it loops while the phone rings."""
import math
import struct
import wave

RATE = 48000
PHRASE = [(0.00, 783.99), (0.14, 1046.5), (0.28, 1318.5), (0.42, 1568.0), (0.70, 1318.5), (0.84, 1568.0)]
LENGTH, DECAY = 3.0, 0.22
frames = []
for i in range(int(RATE * LENGTH)):
    t = i / RATE
    v = 0.0
    for start, f in PHRASE:
        u = t - start
        if 0 <= u < 1.2:
            env = min(1.0, u / 0.004) * math.exp(-u / DECAY)
            v += env * (math.sin(2 * math.pi * f * u) + 0.3 * math.sin(4 * math.pi * f * u)
                        + 0.12 * math.sin(6 * math.pi * f * u))
    frames.append(int(max(-1.0, min(1.0, 0.33 * v)) * 32767))
with wave.open("ringtone.wav", "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(b"".join(struct.pack("<h", f) for f in frames))
