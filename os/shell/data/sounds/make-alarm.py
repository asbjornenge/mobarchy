#!/usr/bin/env python3
"""Makes alarm.wav: a soft three-note chime (A, C#, E), twice, 2 s; it loops. Synthesised
(sines with a quick attack and a decay), so there is no licence to carry."""
import math
import struct
import wave

RATE, SECONDS = 44100, 2.0
NOTES = [(0.00, 880.0), (0.18, 1108.73), (0.36, 1318.51), (0.80, 880.0), (0.98, 1108.73), (1.16, 1318.51)]

n = int(RATE * SECONDS)
buf = [0.0] * n
for start, f in NOTES:
    s0 = int(start * RATE)
    for i in range(int(0.6 * RATE)):
        if s0 + i >= n:
            break
        t = i / RATE
        env = min(1, t / 0.005) * math.exp(-t * 6)
        buf[s0 + i] += 0.28 * env * (math.sin(2 * math.pi * f * t) + 0.35 * math.sin(4 * math.pi * f * t)
                                     + 0.12 * math.sin(6 * math.pi * f * t))
scale = 0.85 / max(abs(x) for x in buf)
with wave.open("alarm.wav", "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(b"".join(struct.pack("<h", int(x * scale * 32767)) for x in buf))
