#!/usr/bin/env python3
# R7RS Benchmark: ack (Python)
# Ackermann function - ack(3, 8) = 2045
import time
import sys

sys.setrecursionlimit(100000)


def ack(m, n):
    if m == 0:
        return n + 1
    if n == 0:
        return ack(m - 1, 1)
    return ack(m - 1, ack(m, n - 1))


def main():
    t0 = time.perf_counter_ns()
    result = ack(3, 8)
    t1 = time.perf_counter_ns()

    if result == 2045:
        print("ack: PASS")
    else:
        print(f"ack: FAIL result={result}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
