#!/usr/bin/env python3
# BENG Benchmark: spectral-norm (Python)
# Eigenvalue approximation using power method
import sys
import time
import math
import array

N = int(sys.argv[1]) if len(sys.argv) > 1 else 100


def a(i, j):
    return 1.0 / ((i + j) * (i + j + 1) / 2 + i + 1)


def multiply_av(n, v, av):
    for i in range(n):
        s = 0.0
        for j in range(n):
            s += a(i, j) * v[j]
        av[i] = s


def multiply_atv(n, v, atv):
    for i in range(n):
        s = 0.0
        for j in range(n):
            s += a(j, i) * v[j]
        atv[i] = s


def multiply_ata_v(n, v, atav):
    u = array.array('d', [0.0] * n)
    multiply_av(n, v, u)
    multiply_atv(n, u, atav)


t0 = time.perf_counter_ns()
u = array.array('d', [1.0] * N)
v = array.array('d', [0.0] * N)

for _ in range(10):
    multiply_ata_v(N, u, v)
    multiply_ata_v(N, v, u)

vbv = 0.0
vv = 0.0
for i in range(N):
    vbv += u[i] * v[i]
    vv += v[i] * v[i]

t1 = time.perf_counter_ns()
print(f"{math.sqrt(vbv / vv):.9f}")
print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")
