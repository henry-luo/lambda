#!/usr/bin/env python3
# R7RS Benchmark: nqueens (Python)
# Count all solutions to N-Queens problem - nqueens(8) = 92
import time


def safe(placed, row, col):
    for r, c in enumerate(placed):
        if c == col or abs(c - col) == row - r:
            return False
    return True


def solve(n, row, placed):
    if row == n:
        return 1
    count = 0
    for col in range(n):
        if safe(placed, row, col):
            count += solve(n, row + 1, placed + [col])
    return count


def nqueens(n):
    return solve(n, 0, [])


def main():
    import sys
    sys.setrecursionlimit(100000)

    t0 = time.perf_counter_ns()
    result = nqueens(8)
    t1 = time.perf_counter_ns()

    if result == 92:
        print("nqueens: PASS")
    else:
        print(f"nqueens: FAIL result={result}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
