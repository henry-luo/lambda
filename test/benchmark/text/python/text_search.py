#!/usr/bin/env python3
"""Naive, KMP, and Boyer–Moore search workload from text_search.js."""

import time

ROUNDS = 1536
MODULUS = 1000000007


def naive_search(text, pattern, start):
    if not pattern:
        return start
    for position in range(start, len(text) - len(pattern) + 1):
        offset = 0
        while offset < len(pattern) and text[position + offset] == pattern[offset]:
            offset += 1
        if offset == len(pattern):
            return position
    return -1


def prefix_table(pattern):
    table = [0] * len(pattern)
    length = 0
    index = 1
    while index < len(pattern):
        if pattern[index] == pattern[length]:
            length += 1
            table[index] = length
            index += 1
        elif length > 0:
            length = table[length - 1]
        else:
            index += 1
    return table


def kmp_search(text, pattern, start):
    if not pattern:
        return start
    table = prefix_table(pattern)
    text_index = start
    pattern_index = 0
    while text_index < len(text):
        if text[text_index] == pattern[pattern_index]:
            text_index += 1
            pattern_index += 1
            if pattern_index == len(pattern):
                return text_index - len(pattern)
        elif pattern_index > 0:
            pattern_index = table[pattern_index - 1]
        else:
            text_index += 1
    return -1


def boyer_moore_search(text, pattern, start):
    if not pattern:
        return start
    last = [-1] * 256
    for index, code in enumerate(pattern):
        last[code] = index
    position = start
    while position <= len(text) - len(pattern):
        offset = len(pattern) - 1
        while offset >= 0 and text[position + offset] == pattern[offset]:
            offset -= 1
        if offset < 0:
            return position
        position += max(1, offset - last[text[position + offset]])
    return -1


def main():
    corpus = "\n".join(
        f"record-{index} alpha aaaaaaaaaaaaaaaaaaaaaaaa token-{index % 23}"
        f" omega needle-{index % 11}"
        for index in range(512)
    )
    patterns = (
        "record-0 alpha", "record-2048 alpha", "token-22 omega",
        "needle-10", "omega needle-7",
        "alpha aaaaaaaaaaaaaaaaaaaaaaaa token-3",
        "missing-marker", "record-2047 omega",
    )
    corpus_codes = [ord(char) for char in corpus]
    pattern_codes = [[ord(char) for char in pattern] for pattern in patterns]

    checksum = 0
    started = time.perf_counter_ns()
    for round_index in range(ROUNDS):
        for index, pattern in enumerate(pattern_codes):
            start = (round_index * 17 + index * 13) % 97
            naive = naive_search(corpus_codes, pattern, start)
            kmp = kmp_search(corpus_codes, pattern, start)
            boyer_moore = boyer_moore_search(corpus_codes, pattern, start)
            if naive != kmp or kmp != boyer_moore:
                raise RuntimeError("search algorithms disagree")
            checksum = (checksum + (naive + 2) * (index + 3)
                        + (round_index + 1) * 7) % MODULUS
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000

    if checksum != 91395120:
        raise RuntimeError(f"unexpected text_search checksum: {checksum}")
    print(f"text_search: CHECKSUM:{checksum}")
    print(f"__TIMING__:{elapsed_ms:.3f}")


if __name__ == "__main__":
    main()
