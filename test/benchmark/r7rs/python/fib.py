#!/usr/bin/env python3
# R7RS Benchmark: fib (Python)
# Naive recursive Fibonacci - fib(27) = 196418
import time


def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)


def main():
    import sys
    sys.setrecursionlimit(10000)
    t0 = time.perf_counter_ns()
    result = fib(27)
    t1 = time.perf_counter_ns()

    if result == 196418:
        print("fib: PASS")
    else:
        print(f"fib: FAIL result={result}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
