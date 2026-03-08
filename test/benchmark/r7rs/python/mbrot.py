#!/usr/bin/env python3
# R7RS Benchmark: mbrot (Python)
# Mandelbrot set generation - 75x75 grid, result at (0,0) = 5
import time


def count(r, i, step, x, y):
    max_count = 64
    radius2 = 16.0
    cr = r + x * step
    ci = i + y * step
    zr = cr
    zi = ci
    c = 0
    while c < max_count:
        zr2 = zr * zr
        zi2 = zi * zi
        if zr2 + zi2 > radius2:
            return c
        new_zr = zr2 - zi2 + cr
        zi = 2.0 * zr * zi + ci
        zr = new_zr
        c += 1
    return max_count


def mbrot(matrix, r, i, step, n):
    for y in range(n - 1, -1, -1):
        for x in range(n - 1, -1, -1):
            matrix[x][y] = count(r, i, step, x, y)


def test(n):
    matrix = [[0] * n for _ in range(n)]
    mbrot(matrix, -1.0, -0.5, 0.005, n)
    return matrix[0][0]


def main():
    t0 = time.perf_counter_ns()
    result = test(75)
    t1 = time.perf_counter_ns()

    if result == 5:
        print("mbrot: PASS")
    else:
        print(f"mbrot: FAIL result={result}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
