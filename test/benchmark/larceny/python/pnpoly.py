#!/usr/bin/env python3
# Larceny Benchmark: pnpoly (Python)
# Point-in-polygon using ray casting
import time


def pnpoly(xs, ys, n, testx, testy):
    inside = False
    j = n - 1
    for i in range(n):
        yi, yj = ys[i], ys[j]
        if (yi > testy) != (yj > testy):
            xtest = (xs[j] - xs[i]) * (testy - yi) / (yj - yi) + xs[i]
            if testx < xtest:
                inside = not inside
        j = i
    return inside


def main():
    xs = [0.0, 1.0, 1.0, 0.0, 0.0,
          1.0, -0.5, -1.0, -1.0, -2.0,
          -2.5, -2.0, -1.5, -0.5, 0.5,
          1.0, 0.5, 0.0, -0.5, -1.0]
    ys = [0.0, 0.0, 1.0, 1.0, 2.0,
          3.0, 2.0, 3.0, 0.0, -0.5,
          0.5, 1.5, 2.0, 3.0, 3.0,
          2.0, 1.0, 0.5, -1.0, -1.0]
    n = 20

    t0 = time.perf_counter_ns()
    count = 0
    total = 0
    for ix in range(500):
        testx = -2.5 + ix * 0.008
        for iy in range(200):
            testy = -1.5 + iy * 0.025
            if pnpoly(xs, ys, n, testx, testy):
                count += 1
            total += 1
    t1 = time.perf_counter_ns()

    print(f"pnpoly: total={total} inside={count}")
    print("pnpoly: DONE")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
