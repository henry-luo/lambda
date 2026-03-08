#!/usr/bin/env python3
# R7RS Benchmark: fibfp (Python)
# Fibonacci using floating-point arithmetic - fibfp(27.0) = 196418.0
import time


def fibfp(n):
    if n < 2.0:
        return n
    return fibfp(n - 1.0) + fibfp(n - 2.0)


def main():
    import sys
    sys.setrecursionlimit(10000)
    t0 = time.perf_counter_ns()
    result = fibfp(27.0)
    t1 = time.perf_counter_ns()

    if result == 196418.0:
        print("fibfp: PASS")
    else:
        print(f"fibfp: FAIL result={result}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
