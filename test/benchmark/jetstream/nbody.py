#!/usr/bin/env python3
"""JetStream Benchmark: n-body (SunSpider) — Python version
Planetary orbit simulation — gravitational N-body problem
Original: The Great Computer Language Shootout
"""
import time
import math

PI = 3.141592653589793
SOLAR_MASS = 4.0 * PI * PI
DAYS_PER_YEAR = 365.24


def offset_momentum(bvx, bvy, bvz, bmass):
    px = py = pz = 0.0
    for i in range(5):
        px += bvx[i] * bmass[i]
        py += bvy[i] * bmass[i]
        pz += bvz[i] * bmass[i]
    bvx[0] = -px / SOLAR_MASS
    bvy[0] = -py / SOLAR_MASS
    bvz[0] = -pz / SOLAR_MASS


def advance(bx, by, bz, bvx, bvy, bvz, bmass, dt):
    for i in range(5):
        for j in range(i + 1, 5):
            dx = bx[i] - bx[j]
            dy = by[i] - by[j]
            dz = bz[i] - bz[j]
            d_squared = dx * dx + dy * dy + dz * dz
            distance = math.sqrt(d_squared)
            mag = dt / (d_squared * distance)
            bvx[i] -= dx * bmass[j] * mag
            bvy[i] -= dy * bmass[j] * mag
            bvz[i] -= dz * bmass[j] * mag
            bvx[j] += dx * bmass[i] * mag
            bvy[j] += dy * bmass[i] * mag
            bvz[j] += dz * bmass[i] * mag
    for i in range(5):
        bx[i] += dt * bvx[i]
        by[i] += dt * bvy[i]
        bz[i] += dt * bvz[i]


def energy(bx, by, bz, bvx, bvy, bvz, bmass):
    e = 0.0
    for i in range(5):
        e += 0.5 * bmass[i] * (bvx[i] * bvx[i] + bvy[i] * bvy[i] + bvz[i] * bvz[i])
        for j in range(i + 1, 5):
            dx = bx[i] - bx[j]
            dy = by[i] - by[j]
            dz = bz[i] - bz[j]
            distance = math.sqrt(dx * dx + dy * dy + dz * dz)
            e -= (bmass[i] * bmass[j]) / distance
    return e


def run_nbody_system(n_steps):
    bx = [0.0, 4.84143144246472090e+00, 8.34336671824457987e+00,
           1.28943695621391310e+01, 1.53796971148509165e+01]
    by = [0.0, -1.16032004402742839e+00, 4.12479856412430479e+00,
           -1.51111514016986312e+01, -2.59193146099879641e+01]
    bz = [0.0, -1.03622044471123109e-01, -4.03523417114321381e-01,
           -2.23307578892655734e-01, 1.79258772950371181e-01]
    bvx = [0.0, 1.66007664274403694e-03 * DAYS_PER_YEAR,
            -2.76742510726862411e-03 * DAYS_PER_YEAR,
             2.96460137564761618e-03 * DAYS_PER_YEAR,
             2.68067772490389322e-03 * DAYS_PER_YEAR]
    bvy = [0.0, 7.69901118419740425e-03 * DAYS_PER_YEAR,
            4.99852801234917238e-03 * DAYS_PER_YEAR,
            2.37847173959480950e-03 * DAYS_PER_YEAR,
            1.62824170038242295e-03 * DAYS_PER_YEAR]
    bvz = [0.0, -6.90460016972063023e-05 * DAYS_PER_YEAR,
             2.30417297573763929e-05 * DAYS_PER_YEAR,
            -2.96589568540237556e-05 * DAYS_PER_YEAR,
            -9.51592254519715870e-05 * DAYS_PER_YEAR]
    bmass = [SOLAR_MASS, 9.54791938424326609e-04 * SOLAR_MASS,
              2.85885980666130812e-04 * SOLAR_MASS,
              4.36624404335156298e-05 * SOLAR_MASS,
              5.15138902046611451e-05 * SOLAR_MASS]

    offset_momentum(bvx, bvy, bvz, bmass)
    ret = energy(bx, by, bz, bvx, bvy, bvz, bmass)
    for _ in range(n_steps):
        advance(bx, by, bz, bvx, bvy, bvz, bmass, 0.01)
    ret += energy(bx, by, bz, bvx, bvy, bvz, bmass)
    return ret


def run():
    ret = 0.0
    n = 3
    while n <= 24:
        ret += run_nbody_system(n * 100)
        n *= 2
    return ret


def main():
    t0 = time.perf_counter_ns()
    result = 0.0
    for _ in range(8):
        result = run()
    t1 = time.perf_counter_ns()

    expected = -1.3524862408537381
    diff = abs(result - expected)
    if diff < 1e-7:
        print("nbody: PASS")
    else:
        print(f"nbody: FAIL got={result} expected={expected}")
    print(f"__TIMING__:{(t1 - t0) / 1_000_000:.3f}")


if __name__ == "__main__":
    main()
