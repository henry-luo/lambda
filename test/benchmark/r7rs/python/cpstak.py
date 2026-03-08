#!/usr/bin/env python3
# R7RS Benchmark: cpstak (Python)
# Double-run Takeuchi function - tak(18, 12, 6) = 7, run twice
import time
import sys

sys.setrecursionlimit(100000)


def tak(x, y, z):
    if y >= x:
        return z
    return tak(tak(x - 1, y, z),
               tak(y - 1, z, x),
               tak(z - 1, x, y))


def main():
    t0 = time.perf_counter_ns()
    result = tak(18, 12, 6)
    result = tak(18, 12, 6)
    t1 = time.perf_counter_ns()

    if result == 7:
        print("cpstak: PASS")
    else:
        print(f"cpstak: FAIL result={result}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
