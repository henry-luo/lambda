#!/usr/bin/env python3
# BENG Benchmark: mandelbrot (Python)
# XOR checksum approach
import sys
import time

N = int(sys.argv[1]) if len(sys.argv) > 1 else 200

t0 = time.perf_counter_ns()
checksum = 0
for y in range(N):
    bits = 0
    bit_num = 0
    for x in range(N):
        cr = 2.0 * x / N - 1.5
        ci = 2.0 * y / N - 1.0
        zr = 0.0
        zi = 0.0
        inside = 1
        for _ in range(50):
            tr = zr * zr - zi * zi + cr
            ti = 2.0 * zr * zi + ci
            zr, zi = tr, ti
            if zr * zr + zi * zi > 4.0:
                inside = 0
                break
        bits = (bits << 1) | inside
        bit_num += 1
        if bit_num == 8:
            checksum ^= bits
            bits = 0
            bit_num = 0
    if bit_num > 0:
        bits <<= (8 - bit_num)
        checksum ^= bits
t1 = time.perf_counter_ns()
print(checksum)
print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")
