#!/usr/bin/env python3
# BENG Benchmark: n-body (Python)
# Planetary motion simulation
import sys
import time
import math

N = int(sys.argv[1]) if len(sys.argv) > 1 else 1000

PI = math.pi
SOLAR_MASS = 4 * PI * PI
DAYS_PER_YEAR = 365.24

# [x, y, z, vx, vy, vz, mass]
bodies = [
    # Sun
    [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, SOLAR_MASS],
    # Jupiter
    [4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
     1.66007664274403694e-03 * DAYS_PER_YEAR, 7.69901118419740425e-03 * DAYS_PER_YEAR,
     -6.90460016972063023e-05 * DAYS_PER_YEAR, 9.54791938424326609e-04 * SOLAR_MASS],
    # Saturn
    [8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
     -2.76742510726862411e-03 * DAYS_PER_YEAR, 4.99852801234917238e-03 * DAYS_PER_YEAR,
     2.30417297573763929e-05 * DAYS_PER_YEAR, 2.85885980666130812e-04 * SOLAR_MASS],
    # Uranus
    [1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
     2.96460137564761618e-03 * DAYS_PER_YEAR, 2.37847173959480950e-03 * DAYS_PER_YEAR,
     -2.96589568540237556e-05 * DAYS_PER_YEAR, 4.36624404335156298e-05 * SOLAR_MASS],
    # Neptune
    [1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
     2.68067772490389322e-03 * DAYS_PER_YEAR, 1.62824170038242295e-03 * DAYS_PER_YEAR,
     -9.51592254519715870e-05 * DAYS_PER_YEAR, 5.15138902046611451e-05 * SOLAR_MASS],
]

X, Y, Z, VX, VY, VZ, MASS = 0, 1, 2, 3, 4, 5, 6


def offset_momentum():
    px = py = pz = 0.0
    for b in bodies:
        px += b[VX] * b[MASS]
        py += b[VY] * b[MASS]
        pz += b[VZ] * b[MASS]
    bodies[0][VX] = -px / SOLAR_MASS
    bodies[0][VY] = -py / SOLAR_MASS
    bodies[0][VZ] = -pz / SOLAR_MASS


def energy():
    e = 0.0
    n = len(bodies)
    for i in range(n):
        bi = bodies[i]
        e += 0.5 * bi[MASS] * (bi[VX] ** 2 + bi[VY] ** 2 + bi[VZ] ** 2)
        for j in range(i + 1, n):
            bj = bodies[j]
            dx = bi[X] - bj[X]
            dy = bi[Y] - bj[Y]
            dz = bi[Z] - bj[Z]
            e -= bi[MASS] * bj[MASS] / math.sqrt(dx * dx + dy * dy + dz * dz)
    return e


def advance(dt):
    n = len(bodies)
    for i in range(n):
        bi = bodies[i]
        for j in range(i + 1, n):
            bj = bodies[j]
            dx = bi[X] - bj[X]
            dy = bi[Y] - bj[Y]
            dz = bi[Z] - bj[Z]
            dist = math.sqrt(dx * dx + dy * dy + dz * dz)
            mag = dt / (dist * dist * dist)
            bi[VX] -= dx * bj[MASS] * mag
            bi[VY] -= dy * bj[MASS] * mag
            bi[VZ] -= dz * bj[MASS] * mag
            bj[VX] += dx * bi[MASS] * mag
            bj[VY] += dy * bi[MASS] * mag
            bj[VZ] += dz * bi[MASS] * mag
    for b in bodies:
        b[X] += dt * b[VX]
        b[Y] += dt * b[VY]
        b[Z] += dt * b[VZ]


offset_momentum()
print(f"{energy():.9f}")

t0 = time.perf_counter_ns()
for _ in range(N):
    advance(0.01)
t1 = time.perf_counter_ns()

print(f"{energy():.9f}")
print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")
