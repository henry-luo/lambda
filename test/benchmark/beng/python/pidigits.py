#!/usr/bin/env python3
# BENG Benchmark: pi-digits (Python)
# Unbounded spigot algorithm (Gibbons 2004)
import sys
import time

NUM_DIGITS = int(sys.argv[1]) if len(sys.argv) > 1 else 30

t0 = time.perf_counter_ns()
q, r, s, t, k = 1, 0, 0, 1, 0
i = 0
digits = ''

while i < NUM_DIGITS:
    k += 1
    k2 = k * 2 + 1

    new_q = q * k
    new_r = (2 * q + r) * k2
    new_s = s * k
    new_t = (2 * s + t) * k2
    q, r, s, t = new_q, new_r, new_s, new_t

    if q <= r:
        fd3 = (3 * q + r) // (3 * s + t)
        fd4 = (4 * q + r) // (4 * s + t)
        if fd3 == fd4:
            digits += str(fd3)
            i += 1

            if i % 10 == 0:
                print(f"{digits}\t:{i}")
                digits = ''

            r = (r - fd3 * t) * 10
            q = q * 10

if digits:
    pad = ' ' * (10 - len(digits))
    print(f"{digits}{pad}\t:{i}")

t1 = time.perf_counter_ns()
print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")
