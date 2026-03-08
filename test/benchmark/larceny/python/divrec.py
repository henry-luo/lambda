#!/usr/bin/env python3
# Larceny Benchmark: divrec (Python)
# Recursive integer division via repeated subtraction
import sys
import time

sys.setrecursionlimit(100000)


def divrec_div(x, y):
    if x < y:
        return 0
    return 1 + divrec_div(x - y, y)


def divrec_mod(x, y):
    if x < y:
        return x
    return divrec_mod(x - y, y)


def main():
    t0 = time.perf_counter_ns()
    result = 0
    for _ in range(1000):
        result += divrec_div(1000, 2)
        result -= divrec_mod(1000, 2)
    t1 = time.perf_counter_ns()

    if result == 500000:
        print("divrec: PASS")
    else:
        print(f"divrec: FAIL result={result}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
