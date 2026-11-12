#!/usr/bin/env python3
"""Check the shared ASCII prose using Word's installed English vocabulary."""
import json
from pathlib import Path
import re
import time

ROOT = Path("test/benchmark/text")


def load_dictionary():
    words = {"Lambda", "Radiant"}
    encoded = Path("lmd/package/doc/spelling/data/en-US.dict").read_text(encoding="utf-8")
    for line in encoded.splitlines():
        previous = ""
        delta = False
        parts = re.split(r"([;|])", line)
        for index in range(0, len(parts), 2):
            token = parts[index]
            if delta:
                prefix, sep, suffix = token.partition(",")
                previous = previous[:int(prefix)] + suffix if sep else previous + token
            else:
                previous = token
            words.add(previous)
            delta = index + 1 < len(parts) and parts[index + 1] == ";"
    return words


def main():
    words = load_dictionary()
    text = (ROOT / "spelling_document.txt").read_text(encoding="utf-8")
    expected = json.loads((ROOT / "spelling_expected.json").read_text(encoding="utf-8"))
    started = time.perf_counter_ns()
    issues = []
    checked = 0
    for match in re.finditer(r"[A-Za-z]+(?:'[A-Za-z]+)*", text):
        word = match.group()
        checked += 1
        lower = word.lower()
        folded = word.isupper() or word == word[0].upper() + lower[1:]
        if word not in words and not (folded and lower in words):
            issues.append({"word": word, "start": match.start(), "end": match.end()})
    elapsed = (time.perf_counter_ns() - started) / 1_000_000
    if checked != 7110 or issues != expected:
        raise RuntimeError("spelling: FAIL result mismatch")
    print("spelling: CHECKED:7110 ISSUES:720")
    print(f"__TIMING__:{elapsed:.3f}")


if __name__ == "__main__":
    main()
