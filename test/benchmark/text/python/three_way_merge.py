#!/usr/bin/env python3
"""Line and word three-way merge workload from three_way_merge.js."""

import time

ROUNDS = 11000
LINE_COUNT = 768
MODULUS = 1000000007


def make_variant(base, side):
    lines = []
    for index, line in enumerate(base):
        if index % 17 == 0:
            line += f" {side} edit {index % 31} keeps the paragraph useful"
        elif side == "left" and index % 23 == 0:
            line += " left-only annotation"
        elif side == "right" and index % 29 == 0:
            line += " right-only annotation"
        lines.append(line)
    return lines


def word_at(words, index):
    return words[index] if index < len(words) else ""


def merge_words(base_line, left_line, right_line):
    if left_line == right_line:
        return left_line
    if left_line == base_line:
        return right_line
    if right_line == base_line:
        return left_line
    base_words = base_line.split(" ")
    left_words = left_line.split(" ")
    right_words = right_line.split(" ")
    words = []
    for index in range(max(len(base_words), len(left_words), len(right_words))):
        base_word = word_at(base_words, index)
        left_word = word_at(left_words, index)
        right_word = word_at(right_words, index)
        if left_word == right_word:
            words.append(left_word)
        elif left_word == base_word:
            words.append(right_word)
        elif right_word == base_word:
            words.append(left_word)
        else:
            words.extend(("<<<<<<< LEFT", left_word, "=======", right_word, ">>>>>>> RIGHT"))
    return " ".join(words)


def merge_lines(base, left, right):
    merged = []
    for base_line, left_line, right_line in zip(base, left, right):
        if left_line == right_line:
            merged.append(left_line)
        elif left_line == base_line:
            merged.append(right_line)
        elif right_line == base_line:
            merged.append(left_line)
        else:
            merged.append(merge_words(base_line, left_line, right_line))
    return "\n".join(merged)


def main():
    base = [f"section {index} records the base document with stable words for merging and review"
            for index in range(LINE_COUNT)]
    left = make_variant(base, "left")
    right = make_variant(base, "right")
    checksum = 0
    started = time.perf_counter_ns()
    for round_index in range(ROUNDS):
        merged = merge_lines(base, left, right)
        checksum = (checksum + len(merged) * 31
                    + ord(merged[(round_index * 37) % len(merged)])) % MODULUS
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000
    if checksum != 342313356:
        raise RuntimeError(f"unexpected three_way_merge checksum: {checksum}")
    print(f"three_way_merge: CHECKSUM:{checksum}")
    print(f"__TIMING__:{elapsed_ms:.3f}")


if __name__ == "__main__":
    main()
