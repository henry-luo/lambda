#!/usr/bin/env python3
# Kostya Benchmark: levenshtein (Python)
# Levenshtein edit distance using dynamic programming
import time
import array


def levenshtein(s1, s2):
    n, m = len(s1), len(s2)
    prev = array.array('i', range(m + 1))
    curr = array.array('i', [0] * (m + 1))
    for i in range(1, n + 1):
        curr[0] = i
        c1 = ord(s1[i - 1])
        for j in range(1, m + 1):
            cost = 0 if c1 == ord(s2[j - 1]) else 1
            d = prev[j] + 1
            ins = curr[j - 1] + 1
            sub = prev[j - 1] + cost
            curr[j] = min(d, ins, sub)
        prev, curr = curr, prev
    return prev[m]


def main():
    t0 = time.perf_counter_ns()
    d1 = levenshtein("kitten", "sitting")
    d2 = levenshtein("saturday", "sunday")
    d3 = levenshtein("a" * 500, "b" * 500)
    d4 = levenshtein("ab" * 200, "ba" * 200)
    t1 = time.perf_counter_ns()

    print(f"levenshtein: d(kitten,sitting)={d1}")
    print(f"levenshtein: d(saturday,sunday)={d2}")
    print(f"levenshtein: d(aaa...,bbb...)={d3}")
    print(f"levenshtein: d(ababab...,babab...)={d4}")
    if d1 == 3 and d2 == 3:
        print("levenshtein: PASS")
    else:
        print("levenshtein: FAIL")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
