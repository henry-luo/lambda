#!/usr/bin/env python3
"""Liang-pattern hyphenation workload from hyphen.js."""

import json
import time

TABLES_PATH = "test/benchmark/text/hyphen_tables.json"
CASES_PATH = "test/benchmark/text/hyphen_cases.json"
ROUNDS = 32
MODULUS = 1000000007


def ascii_letter(char):
    return "A" <= char <= "Z" or "a" <= char <= "z"


def word_char(char):
    return ascii_letter(char) or char == "'"


class Hyphenator:
    def __init__(self, tables):
        self.tables = tables
        self.word_cache = {}
        self.marker_cache = {}
        self.exceptions = {
            word: tables["exception_markers"][offset:offset + count]
            for word, offset, count in zip(
                tables["exception_words"], tables["exception_offsets"],
                tables["exception_counts"])
        }

    def trie_child(self, node, code):
        tables = self.tables
        first = tables["node_first"][node]
        for edge in range(first, first + tables["node_count"][node]):
            if tables["edge_code"][edge] == code:
                return tables["edge_child"][edge]
        return -1

    def markers_for_word(self, word):
        lowered = word.lower()
        if lowered in self.exceptions:
            return self.exceptions[lowered]
        if lowered in self.marker_cache:
            return self.marker_cache[lowered]
        tables = self.tables
        length = len(word)
        levels = [0] * (length + 1)
        extended = "." + lowered + "."
        for start in range(length):
            node = tables["root"]
            position = 0 if start == 0 else start - 1
            for cursor in range(start, length + 2):
                node = self.trie_child(node, ord(extended[cursor]))
                if node < 0:
                    break
                level_index = tables["node_level"][node]
                if level_index >= 0:
                    offset = tables["level_offsets"][level_index]
                    count = tables["level_lengths"][level_index]
                    for level_offset in range(count):
                        target = position + level_offset
                        value = tables["level_values"][offset + level_offset]
                        if 0 <= target <= length and value > levels[target]:
                            levels[target] = value
        levels[0] = levels[1] = levels[length - 1] = levels[length] = 0
        markers = [index for index, value in enumerate(levels) if value & 1]
        self.marker_cache[lowered] = markers
        return markers

    def hyphenate_word(self, word):
        if word in self.word_cache:
            return self.word_cache[word]
        if len(word) < 5 or "-" in word:
            result = word
        else:
            markers = self.markers_for_word(word)
            marker_index = 0
            chars = []
            for index, char in enumerate(word):
                if marker_index < len(markers) and markers[marker_index] == index:
                    chars.append("-")
                    marker_index += 1
                chars.append(char)
            chars.extend("-" for _ in markers[marker_index:])
            result = "".join(chars)
        self.word_cache[word] = result
        return result

    def hyphenate_text(self, source):
        result = []
        index = 0
        while index < len(source):
            if (source[index] == "<" and index + 1 < len(source)
                    and (ascii_letter(source[index + 1]) or source[index + 1] == "/")):
                end = source.find(">", index)
                if end < 0:
                    end = len(source) - 1
                result.append(source[index:end + 1])
                index = end + 1
            elif word_char(source[index]):
                start = index
                while index < len(source):
                    char = source[index]
                    if word_char(char):
                        index += 1
                    elif (char == "-" and index > start and index + 1 < len(source)
                          and ascii_letter(source[index + 1])):
                        index += 1
                    else:
                        break
                result.append(self.hyphenate_word(source[start:index]))
            else:
                result.append(source[index])
                index += 1
        return "".join(result)


def main():
    with open(TABLES_PATH, encoding="utf-8") as stream:
        tables = json.load(stream)
    with open(CASES_PATH, encoding="utf-8") as stream:
        cases = json.load(stream)
    verifier = Hyphenator(tables)
    for index, (source, expected) in enumerate(cases):
        if verifier.hyphenate_text(source) != expected:
            raise RuntimeError(f"hyphen fixture verification failed at case {index}")

    checksum = 0
    started = time.perf_counter_ns()
    for _ in range(ROUNDS):
        hyphenator = Hyphenator(tables)
        for index, (source, _) in enumerate(cases):
            result = hyphenator.hyphenate_text(source)
            checksum = (checksum + len(result) * 29) % MODULUS
            if result:
                checksum = (checksum + ord(result[index % len(result)])) % MODULUS
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000
    if checksum != 1183296:
        raise RuntimeError(f"hyphen checksum mismatch: {checksum}")
    print(f"CHECKSUM:{checksum}")
    print(f"__TIMING__:{elapsed_ms:.3f}")


if __name__ == "__main__":
    main()
