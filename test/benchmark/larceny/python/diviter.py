#!/usr/bin/env python3
# Larceny Benchmark: diviter (Python)
# Iterative integer division via repeated subtraction
import time


def diviter_div(x, y):
    q = 0
    r = x
    while r >= y:
        r -= y
        q += 1
    return q


def diviter_mod(x, y):
    r = x
    while r >= y:
        r -= y
    return r


def main():
    t0 = time.perf_counter_ns()
    result = 0
    for _ in range(1000):
        result += diviter_div(1000000, 2)
        result -= diviter_mod(1000000, 2)
    t1 = time.perf_counter_ns()

    if result == 500000000:
        print("diviter: PASS")
    else:
        print(f"diviter: FAIL result={result}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
