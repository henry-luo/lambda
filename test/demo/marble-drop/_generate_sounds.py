"""Rebuild Marble Drop's original, deterministic glass-and-brass WAV effects."""

from array import array
import math
from pathlib import Path
import random
import sys
import wave

RATE = 24000
ROOT = Path(__file__).resolve().parent / "assets" / "sounds"


def strike(t, frequency, decay, modes, strength=1.0):
    if t < 0:
        return 0.0
    attack = 1.0 - math.exp(-t / 0.0008)
    return strength * attack * sum(
        weight * math.sin(math.tau * frequency * ratio * t)
        * math.exp(-t / (decay / math.sqrt(ratio)))
        for ratio, weight in modes
    )


GLASS = [(1, 1), (2.71, 0.36), (4.07, 0.18), (5.43, 0.07)]
BRASS = [(1, 1), (1.47, 0.5), (2.09, 0.28), (3.14, 0.1)]
WOOD = [(1, 1), (2.3, 0.22), (3.8, 0.09)]


def effect(kind, t, noise):
    if kind == "drop":
        return (strike(t, 1480, 0.035, GLASS, 0.65)
                + strike(t - 0.043, 1760, 0.025, BRASS, 0.24))
    if kind == "switch":
        return (strike(t, 840, 0.012, WOOD, 0.7)
                + strike(t - 0.018, 2100, 0.016, BRASS, 0.38)
                + noise * math.exp(-t / 0.006) * 0.2)
    if kind == "catch":
        return (strike(t, 390, 0.055, BRASS, 0.8)
                + strike(t - 0.052, 1200, 0.022, WOOD, 0.3))
    if kind == "pocket":
        return (strike(t, 1320, 0.12, GLASS, 0.65)
                + strike(t - 0.058, 1640, 0.075, GLASS, 0.3)
                + strike(t - 0.12, 1980, 0.06, GLASS, 0.14))
    if kind == "steel":
        return strike(t, 2240, 0.095, BRASS, 0.65)
    if kind == "miss":
        return (strike(t, 310, 0.055, WOOD, 0.7)
                + strike(t - 0.075, 230, 0.055, WOOD, 0.5))
    # A soft C-major glass arpeggio celebrates the completed machine.
    return sum(strike(t - start, hz, 0.17, GLASS, gain)
               for start, hz, gain in [(0, 1046.5, 0.52), (0.11, 1318.5, 0.45),
                                       (0.22, 1568, 0.4), (0.36, 2093, 0.5)])


def render(kind, duration):
    rng = random.Random(1997)
    count = round(duration * RATE)
    samples = []
    for i in range(count):
        t = i / RATE
        value = effect(kind, t, rng.uniform(-1, 1))
        # Smooth the playback boundaries to keep the transients free of digital clicks.
        fade = min(1, t / 0.001, (duration - t) / 0.012)
        samples.append(value * fade)
    gain = 0.7 / max(abs(x) for x in samples)
    pcm = array("h", (round(value * gain * 32767) for value in samples))
    if sys.byteorder != "little":
        pcm.byteswap()
    with wave.open(str(ROOT / (kind + ".wav")), "wb") as output:
        output.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        output.writeframes(pcm.tobytes())


if __name__ == "__main__":
    ROOT.mkdir(parents=True, exist_ok=True)
    for kind, duration in [("drop", 0.15), ("switch", 0.09), ("catch", 0.25),
                           ("pocket", 0.46), ("steel", 0.36), ("miss", 0.3), ("win", 1.1)]:
        render(kind, duration)
