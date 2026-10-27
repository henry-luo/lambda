"""Regenerate the Greetings demo's original, deterministic music-box miniatures.

Run from any directory: python3 test/demo/greetings/_generate_music.py
Only Python's standard library is needed. No samples or external recordings.
"""
import array
import math
from pathlib import Path
import sys
import wave

RATE = 16000
DEST = Path(__file__).parent / "assets" / "music"
# Original melodies, in MIDI note numbers; eight bars with a little breathing room.
TUNES = {
    "celebrate": (0.375, [72, 76, 79, 76, 81, 79, 76, 74, 72, 76, 79, 84, 83, 79, 76, 72,
                          74, 77, 81, 77, 79, 77, 74, 71, 72, 76, 79, 76, 74, 71, 72, 0]),
    "winter": (0.5, [79, 83, 86, 83, 81, 78, 74, 0, 76, 79, 83, 79, 78, 74, 71, 0,
                     72, 76, 79, 83, 81, 78, 74, 0, 71, 74, 79, 83, 81, 78, 79, 0]),
    "affection": (0.5, [69, 72, 76, 72, 74, 72, 69, 0, 67, 71, 74, 71, 72, 71, 67, 0,
                        65, 69, 72, 76, 74, 72, 69, 0, 67, 71, 74, 76, 72, 71, 72, 0]),
    "lullaby": (0.625, [72, 79, 76, 0, 74, 79, 77, 0, 72, 76, 79, 76, 74, 71, 67, 0,
                        69, 76, 72, 0, 67, 74, 71, 0, 65, 72, 69, 67, 64, 67, 72, 0]),
    "sunshine": (0.375, [76, 79, 81, 79, 76, 74, 72, 0, 74, 77, 79, 77, 74, 72, 71, 0,
                         72, 76, 79, 81, 79, 76, 74, 0, 72, 74, 76, 79, 76, 74, 72, 0]),
}


def add_note(samples, start, midi, duration, gain):
    frequency = 440 * 2 ** ((midi - 69) / 12)
    offset = round(start * RATE)
    for i in range(min(round(duration * RATE), len(samples) - offset)):
        t = i / RATE
        # A soft attack avoids clicks; decaying overtones give a small music-box voice.
        envelope = min(1, t / 0.012) * math.exp(-4.5 * t / duration)
        envelope *= min(1, (duration - t) / 0.06)
        phase = 2 * math.pi * frequency * t
        tone = math.sin(phase) + 0.24 * math.sin(2 * phase) + 0.08 * math.sin(3 * phase)
        samples[offset + i] += gain * envelope * tone


def generate():
    DEST.mkdir(parents=True, exist_ok=True)
    for name, (beat, melody) in TUNES.items():
        samples = [0.0] * round(32 * beat * RATE)
        for i, midi in enumerate(melody):
            if midi:
                add_note(samples, i * beat, midi, beat * 1.8, 0.42)
        # Quiet bass notes outline a I–V–vi–IV progression under the melody.
        root = 55 if name == "winter" else 48
        for bar, step in enumerate([0, 7, 9, 5, 0, 7, 5, 0]):
            add_note(samples, bar * beat * 4, root + step, beat * 3.5, 0.17)
        # The last rest and short edge fades make repeat playback click-free.
        edge = round(RATE * 0.04)
        peak = max(1, max(abs(value) for value in samples))
        pcm = array.array("h", (round(value / peak * 27000 * min(1, i / edge, (len(samples) - 1 - i) / edge))
                                for i, value in enumerate(samples)))
        if sys.byteorder != "little":
            pcm.byteswap()
        with wave.open(str(DEST / (name + ".wav")), "wb") as out:
            out.setnchannels(1)
            out.setsampwidth(2)
            out.setframerate(RATE)
            out.writeframes(pcm.tobytes())


if __name__ == "__main__":
    generate()
