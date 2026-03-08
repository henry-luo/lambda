#!/usr/bin/env python3
# R7RS Benchmark: sumfp (Python)
# Sum of floats from 0.0 to 100000.0
import time


def run(n):
    i = n
    s = 0.0
    while i >= 0.0:
        s += i
        i -= 1.0
    return s


def main():
    t0 = time.perf_counter_ns()
    result = run(100000.0)
    t1 = time.perf_counter_ns()

    expected = 5000050000.0
    if abs(result - expected) < 1.0:
        print("sumfp: PASS")
    else:
        print(f"sumfp: FAIL result={result}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
