#!/usr/bin/env python3
# Larceny Benchmark: puzzle (Python)
# N-Queens n=10 — count all solutions
import time

BOARD_SIZE = 10


def solve(row, cols, diag1, diag2, n):
    if row == n:
        return 1
    count = 0
    for col in range(n):
        d1 = row + col
        d2 = row - col + n - 1
        if not cols[col] and not diag1[d1] and not diag2[d2]:
            cols[col] = True
            diag1[d1] = True
            diag2[d2] = True
            count += solve(row + 1, cols, diag1, diag2, n)
            cols[col] = False
            diag1[d1] = False
            diag2[d2] = False
    return count


def main():
    t0 = time.perf_counter_ns()
    cols = [False] * BOARD_SIZE
    diag1 = [False] * (BOARD_SIZE * 2)
    diag2 = [False] * (BOARD_SIZE * 2)
    result = solve(0, cols, diag1, diag2, BOARD_SIZE)
    t1 = time.perf_counter_ns()

    if result == 724:
        print("puzzle: PASS")
    else:
        print(f"puzzle: FAIL result={result}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
