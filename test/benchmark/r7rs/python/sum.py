#!/usr/bin/env python3
# R7RS Benchmark: sum (Python)
# Sum of integers from 0 to 10000, repeated 100 times
import time


def run(n):
    i = n
    s = 0
    while i >= 0:
        s += i
        i -= 1
    return s


def main():
    t0 = time.perf_counter_ns()
    result = 0
    for _ in range(100):
        result = run(10000)
    t1 = time.perf_counter_ns()

    if result == 50005000:
        print("sum: PASS")
    else:
        print(f"sum: FAIL result={result}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
