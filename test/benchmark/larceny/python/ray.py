#!/usr/bin/env python3
# Larceny Benchmark: ray (Python)
# Simple ray tracer — cast rays against spheres
import time
import math


def sphere_intersect(ox, oy, oz, dx, dy, dz, cx, cy, cz, r):
    ex, ey, ez = ox - cx, oy - cy, oz - cz
    a = dx * dx + dy * dy + dz * dz
    b = 2.0 * (ex * dx + ey * dy + ez * dz)
    c = ex * ex + ey * ey + ez * ez - r * r
    disc = b * b - 4.0 * a * c
    if disc < 0.0:
        return -1.0
    t = (-b - math.sqrt(disc)) / (2.0 * a)
    if t > 0.001:
        return t
    t = (-b + math.sqrt(disc)) / (2.0 * a)
    if t > 0.001:
        return t
    return -1.0


def main():
    sx = [0.0, -2.0, 2.0, 0.0]
    sy = [0.0, 0.0, 0.0, 2.0]
    sz = [5.0, 5.0, 5.0, 5.0]
    sr = [1.0, 1.0, 1.0, 1.0]
    num_spheres = 4
    grid = 100

    t0 = time.perf_counter_ns()
    hits = 0
    for py in range(grid):
        for px in range(grid):
            dx = (px - 50.0) / 50.0
            dy = (py - 50.0) / 50.0
            dz = 1.0
            length = math.sqrt(dx * dx + dy * dy + dz * dz)
            dx /= length
            dy /= length
            dz /= length

            min_t = 999999.0
            for si in range(num_spheres):
                t = sphere_intersect(0, 0, 0, dx, dy, dz,
                                     sx[si], sy[si], sz[si], sr[si])
                if 0.0 < t < min_t:
                    min_t = t
            if min_t < 999999.0:
                hits += 1
    t1 = time.perf_counter_ns()

    print(f"ray: hits={hits}")
    if 0 < hits < 10000:
        print("ray: PASS")
    else:
        print("ray: FAIL")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
