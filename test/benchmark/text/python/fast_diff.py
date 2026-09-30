#!/usr/bin/env python3
"""Myers text diff and semantic cleanup workload from fast_diff.js.

The diff algorithm follows the checked-in fast-diff implementation (Apache 2.0,
derived from Neil Fraser's Diff Match and Patch). The benchmark input is ASCII,
so Python string indices and JavaScript UTF-16 indices coincide here.
"""

import json
import pathlib
import re
import time

DELETE, EQUAL, INSERT = -1, 0, 1
ROUNDS = 256
MODULUS = 1000000007
FIXTURE = pathlib.Path(__file__).resolve().parents[1] / "fast_diff_pairs.json"


def common_prefix(left, right):
    index = 0
    while index < min(len(left), len(right)) and left[index] == right[index]:
        index += 1
    return index


def common_suffix(left, right):
    index = 0
    while index < min(len(left), len(right)) and left[-index - 1] == right[-index - 1]:
        index += 1
    return index


def common_overlap(left, right):
    count = min(len(left), len(right))
    for length in range(count, 0, -1):
        if left.endswith(right[:length]):
            return length
    return 0


def half_match(left, right):
    long_text, short_text = (left, right) if len(left) > len(right) else (right, left)
    if len(long_text) < 4 or len(short_text) * 2 < len(long_text):
        return None

    def find_seed(start):
        seed = long_text[start:start + len(long_text) // 4]
        position = -1
        best = None
        while True:
            position = short_text.find(seed, position + 1)
            if position < 0:
                break
            prefix = common_prefix(long_text[start:], short_text[position:])
            suffix = common_suffix(long_text[:start], short_text[:position])
            middle = short_text[position - suffix:position + prefix]
            if best is None or len(middle) > len(best[4]):
                best = (long_text[:start - suffix], long_text[start + prefix:],
                        short_text[:position - suffix], short_text[position + prefix:], middle)
        return best if best and len(best[4]) * 2 >= len(long_text) else None

    first = find_seed((len(long_text) + 3) // 4)
    second = find_seed((len(long_text) + 1) // 2)
    match = first if second is None or (first and len(first[4]) > len(second[4])) else second
    if match is None:
        return None
    return match if len(left) > len(right) else (match[2], match[3], match[0], match[1], match[4])


def bisect(left, right):
    n, m = len(left), len(right)
    maximum = (n + m + 1) // 2
    offset = maximum
    length = 2 * maximum
    forward, reverse = [-1] * length, [-1] * length
    forward[offset + 1] = reverse[offset + 1] = 0
    delta = n - m
    front = delta % 2 != 0
    first_start = first_end = second_start = second_end = 0
    for distance in range(maximum):
        for diagonal in range(-distance + first_start, distance - first_end + 1, 2):
            position = offset + diagonal
            x = (forward[position + 1] if diagonal == -distance or
                 (diagonal != distance and forward[position - 1] < forward[position + 1])
                 else forward[position - 1] + 1)
            y = x - diagonal
            while x < n and y < m and left[x] == right[y]:
                x += 1
                y += 1
            forward[position] = x
            if x > n:
                first_end += 2
            elif y > m:
                first_start += 2
            elif front:
                reverse_pos = offset + delta - diagonal
                if 0 <= reverse_pos < length and reverse[reverse_pos] != -1 and x >= n - reverse[reverse_pos]:
                    return diff_main(left[:x], right[:y]) + diff_main(left[x:], right[y:])

        for diagonal in range(-distance + second_start, distance - second_end + 1, 2):
            position = offset + diagonal
            x = (reverse[position + 1] if diagonal == -distance or
                 (diagonal != distance and reverse[position - 1] < reverse[position + 1])
                 else reverse[position - 1] + 1)
            y = x - diagonal
            while x < n and y < m and left[n - x - 1] == right[m - y - 1]:
                x += 1
                y += 1
            reverse[position] = x
            if x > n:
                second_end += 2
            elif y > m:
                second_start += 2
            elif not front:
                forward_pos = offset + delta - diagonal
                if 0 <= forward_pos < length and forward[forward_pos] != -1:
                    split_x = forward[forward_pos]
                    split_y = offset + split_x - forward_pos
                    if split_x >= n - x:
                        return diff_main(left[:split_x], right[:split_y]) + diff_main(left[split_x:], right[split_y:])
    return [[DELETE, left], [INSERT, right]]


def diff_compute(left, right):
    if not left:
        return [[INSERT, right]]
    if not right:
        return [[DELETE, left]]
    long_text, short_text = (left, right) if len(left) > len(right) else (right, left)
    position = long_text.find(short_text)
    if position >= 0:
        kind = DELETE if len(left) > len(right) else INSERT
        return [[kind, long_text[:position]], [EQUAL, short_text],
                [kind, long_text[position + len(short_text):]]]
    if len(short_text) == 1:
        return [[DELETE, left], [INSERT, right]]
    match = half_match(left, right)
    if match:
        return diff_main(match[0], match[2]) + [[EQUAL, match[4]]] + diff_main(match[1], match[3])
    return bisect(left, right)


def cleanup_merge(diffs):
    diffs.append([EQUAL, ""])
    pointer = inserts = deletes = 0
    inserted = removed = ""
    while pointer < len(diffs):
        if pointer < len(diffs) - 1 and not diffs[pointer][1]:
            diffs.pop(pointer)
            continue
        kind, value = diffs[pointer]
        if kind == INSERT:
            inserts += 1
            inserted += value
            pointer += 1
        elif kind == DELETE:
            deletes += 1
            removed += value
            pointer += 1
        else:
            previous = pointer - inserts - deletes - 1
            if removed or inserted:
                if removed and inserted:
                    prefix = common_prefix(inserted, removed)
                    if prefix:
                        if previous >= 0:
                            diffs[previous][1] += inserted[:prefix]
                        else:
                            diffs.insert(0, [EQUAL, inserted[:prefix]])
                            pointer += 1
                        inserted, removed = inserted[prefix:], removed[prefix:]
                    suffix = common_suffix(inserted, removed)
                    if suffix:
                        diffs[pointer][1] = inserted[-suffix:] + diffs[pointer][1]
                        inserted, removed = inserted[:-suffix], removed[:-suffix]
                replacement = []
                if removed:
                    replacement.append([DELETE, removed])
                if inserted:
                    replacement.append([INSERT, inserted])
                count = inserts + deletes
                diffs[pointer - count:pointer] = replacement
                pointer = pointer - count + len(replacement)
            if pointer and diffs[pointer - 1][0] == EQUAL:
                diffs[pointer - 1][1] += diffs[pointer][1]
                diffs.pop(pointer)
            else:
                pointer += 1
            inserts = deletes = 0
            inserted = removed = ""
    if diffs and not diffs[-1][1]:
        diffs.pop()

    changed = False
    pointer = 1
    while pointer < len(diffs) - 1:
        if diffs[pointer - 1][0] == EQUAL and diffs[pointer + 1][0] == EQUAL:
            before, edit, after = (diffs[pointer - 1][1], diffs[pointer][1], diffs[pointer + 1][1])
            if edit.endswith(before):
                diffs[pointer][1] = before + edit[:-len(before)]
                diffs[pointer + 1][1] = before + after
                diffs.pop(pointer - 1)
                changed = True
            elif edit.startswith(after):
                diffs[pointer - 1][1] += after
                diffs[pointer][1] = edit[len(after):] + after
                diffs.pop(pointer + 1)
                changed = True
        pointer += 1
    if changed:
        cleanup_merge(diffs)


def semantic_score(left, right):
    if not left or not right:
        return 6
    first, second = left[-1], right[0]
    non_first, non_second = not first.isascii() or not first.isalnum(), not second.isascii() or not second.isalnum()
    white_first, white_second = non_first and first.isspace(), non_second and second.isspace()
    line_first, line_second = white_first and first in "\r\n", white_second and second in "\r\n"
    if (line_first and re.search(r"\n\r?\n$", left)) or (line_second and re.match(r"\r?\n\r?\n", right)):
        return 5
    if line_first or line_second:
        return 4
    if non_first and not white_first and white_second:
        return 3
    if white_first or white_second:
        return 2
    if non_first or non_second:
        return 1
    return 0


def cleanup_lossless(diffs):
    pointer = 1
    while pointer < len(diffs) - 1:
        if diffs[pointer - 1][0] == EQUAL and diffs[pointer + 1][0] == EQUAL:
            left, edit, right = diffs[pointer - 1][1], diffs[pointer][1], diffs[pointer + 1][1]
            suffix = common_suffix(left, edit)
            if suffix:
                common = edit[-suffix:]
                left, edit, right = left[:-suffix], common + edit[:-suffix], common + right
            best = (left, edit, right)
            best_score = semantic_score(left, edit) + semantic_score(edit, right)
            while edit and right and edit[0] == right[0]:
                left, edit, right = left + edit[0], edit[1:] + right[0], right[1:]
                score = semantic_score(left, edit) + semantic_score(edit, right)
                if score >= best_score:
                    best, best_score = (left, edit, right), score
            if diffs[pointer - 1][1] != best[0]:
                if best[0]:
                    diffs[pointer - 1][1] = best[0]
                else:
                    diffs.pop(pointer - 1)
                    pointer -= 1
                diffs[pointer][1] = best[1]
                if best[2]:
                    diffs[pointer + 1][1] = best[2]
                else:
                    diffs.pop(pointer + 1)
                    pointer -= 1
        pointer += 1


def cleanup_semantic(diffs):
    equalities = []
    last = None
    insertion_before = deletion_before = insertion_after = deletion_after = 0
    pointer = 0
    changed = False
    while pointer < len(diffs):
        kind, value = diffs[pointer]
        if kind == EQUAL:
            equalities.append(pointer)
            insertion_before, deletion_before = insertion_after, deletion_after
            insertion_after = deletion_after = 0
            last = value
        else:
            if kind == INSERT:
                insertion_after += len(value)
            else:
                deletion_after += len(value)
            if (last and len(last) <= max(insertion_before, deletion_before)
                    and len(last) <= max(insertion_after, deletion_after)):
                at = equalities[-1]
                diffs.insert(at, [DELETE, last])
                diffs[at + 1][0] = INSERT
                equalities.pop()
                if equalities:
                    equalities.pop()
                pointer = equalities[-1] if equalities else -1
                insertion_before = deletion_before = insertion_after = deletion_after = 0
                last = None
                changed = True
        pointer += 1
    if changed:
        cleanup_merge(diffs)
    cleanup_lossless(diffs)

    pointer = 1
    while pointer < len(diffs):
        if diffs[pointer - 1][0] == DELETE and diffs[pointer][0] == INSERT:
            removed, inserted = diffs[pointer - 1][1], diffs[pointer][1]
            forward, reverse = common_overlap(removed, inserted), common_overlap(inserted, removed)
            if forward >= reverse and (forward * 2 >= len(removed) or forward * 2 >= len(inserted)):
                diffs.insert(pointer, [EQUAL, inserted[:forward]])
                diffs[pointer - 1][1] = removed[:-forward]
                diffs[pointer + 1][1] = inserted[forward:]
                pointer += 1
            elif reverse > forward and (reverse * 2 >= len(removed) or reverse * 2 >= len(inserted)):
                diffs.insert(pointer, [EQUAL, removed[:reverse]])
                diffs[pointer - 1] = [INSERT, inserted[:-reverse]]
                diffs[pointer + 1] = [DELETE, removed[reverse:]]
                pointer += 1
            pointer += 1
        pointer += 1


def diff_main(left, right, cleanup=False):
    if left == right:
        return [[EQUAL, left]] if left else []
    prefix = common_prefix(left, right)
    leading = left[:prefix]
    left, right = left[prefix:], right[prefix:]
    suffix = common_suffix(left, right)
    trailing = left[-suffix:] if suffix else ""
    if suffix:
        left, right = left[:-suffix], right[:-suffix]
    diffs = diff_compute(left, right)
    if leading:
        diffs.insert(0, [EQUAL, leading])
    if trailing:
        diffs.append([EQUAL, trailing])
    cleanup_merge(diffs)
    if cleanup:
        cleanup_semantic(diffs)
    return diffs


def main():
    pairs = json.loads(FIXTURE.read_text())
    checksum = 0
    started = time.perf_counter_ns()
    for _ in range(ROUNDS):
        for left, right in pairs:
            parts = diff_main(left, right, True)
            checksum = (checksum + len(parts) * 17) % MODULUS
            for operation, value in parts:
                checksum = (checksum + operation * 31 + len(value)) % MODULUS
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000
    if checksum != 390912:
        raise RuntimeError(f"fast_diff checksum mismatch: {checksum}")
    print(f"CHECKSUM:{checksum}")
    print(f"__TIMING__:{elapsed_ms:.3f}")


if __name__ == "__main__":
    main()
