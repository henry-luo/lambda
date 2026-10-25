"""Regenerate the demo's original PCM cue sounds with Python's standard library."""
from pathlib import Path
import math
import random
import struct
import wave

ROOT = Path(__file__).resolve().parent
RATE = 22050
for name, duration in [("chime", 0.6), ("click", 0.08), ("whoosh", 0.4)]:
    rng = random.Random(0)
    previous = 0.0
    frames = []
    for index in range(int(RATE * duration)):
        elapsed = index / RATE
        envelope = (1 - elapsed / duration) ** 2 * min(1, elapsed / 0.005)
        if name == "chime":
            sample = (math.sin(2 * math.pi * 880 * elapsed) +
                      0.45 * math.sin(2 * math.pi * 1320 * elapsed)) * envelope * 0.25
        elif name == "click":
            sample = math.sin(2 * math.pi * (1800 - 1200 * elapsed / duration) * elapsed) * envelope * 0.4
        else:
            previous = 0.85 * previous + 0.15 * rng.uniform(-1, 1)
            sample = previous * math.sin(math.pi * elapsed / duration) * 0.65
        frames.append(struct.pack("<h", int(sample * 32767)))
    with wave.open(str(ROOT / (name + ".wav")), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(b"".join(frames))
