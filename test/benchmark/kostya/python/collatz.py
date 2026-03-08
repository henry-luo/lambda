#!/usr/bin/env python3
# Kostya Benchmark: collatz (Python)
# Longest Collatz (3n+1) sequence under 1,000,000
import time


def collatz_len(n):
    steps = 1
    x = n
    while x != 1:
        if x % 2 == 0:
            x //= 2
        else:
            x = 3 * x + 1
        steps += 1
    return steps


def benchmark():
    limit = 1000000
    max_len = 0
    max_start = 0
    for i in range(1, limit):
        clen = collatz_len(i)
        if clen > max_len:
            max_len = clen
            max_start = i
    return max_start


def main():
    t0 = time.perf_counter_ns()
    result = benchmark()
    t1 = time.perf_counter_ns()

    if result == 837799:
        print(f"collatz: PASS (start={result})")
    else:
        print(f"collatz: FAIL result={result}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
