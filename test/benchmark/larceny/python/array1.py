#!/usr/bin/env python3
# Larceny Benchmark: array1 (Python)
# Array creation, fill, and summation
import time
import array


def main():
    size = 10000
    arr = array.array('i', range(size))

    t0 = time.perf_counter_ns()
    total = 0
    for _ in range(100):
        s = 0
        for i in range(size):
            s += arr[i]
        total = s
    t1 = time.perf_counter_ns()

    if total == 49995000:
        print("array1: PASS")
    else:
        print(f"array1: FAIL result={total}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
