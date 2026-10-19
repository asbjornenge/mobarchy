#!/usr/bin/env python3
"""Makes message.wav: the tone of a new message. Two short soft notes going up (E6, A6), each
a sine with a quick attack and a decay like a struck bell, so it is heard without startling."""
import math
import struct
import wave

RATE = 48000
NOTES = ((0.0, 1318.5), (0.12, 1760.0))     # (start in s, Hz)
LENGTH, ATTACK, DECAY = 0.7, 0.005, 0.12
frames = []
for i in range(int(RATE * LENGTH)):
    t = i / RATE
    v = 0.0
    for start, f in NOTES:
        u = t - start
        if u >= 0:
            env = min(1.0, u / ATTACK) * math.exp(-u / DECAY)
            v += env * (math.sin(2 * math.pi * f * u) + 0.25 * math.sin(4 * math.pi * f * u))
    frames.append(int(max(-1.0, min(1.0, 0.35 * v)) * 32767))
with wave.open("message.wav", "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(b"".join(struct.pack("<h", f) for f in frames))
