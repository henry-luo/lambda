#!/usr/bin/env python3
# BENG Benchmark: fannkuch-redux (Python)
import sys
import time

N = int(sys.argv[1]) if len(sys.argv) > 1 else 7


def fannkuch(n):
    perm = list(range(n))
    perm1 = list(range(n))
    count = [0] * n
    max_flips = 0
    checksum = 0
    perm_count = 0

    r = n
    while True:
        while r != 1:
            count[r - 1] = r
            r -= 1

        for i in range(n):
            perm[i] = perm1[i]

        flips = 0
        k = perm[0]
        while k != 0:
            lo, hi = 0, k
            while lo < hi:
                perm[lo], perm[hi] = perm[hi], perm[lo]
                lo += 1
                hi -= 1
            flips += 1
            k = perm[0]

        if flips > max_flips:
            max_flips = flips
        checksum += flips if perm_count % 2 == 0 else -flips
        perm_count += 1

        # next permutation
        found = False
        r = 1
        while r < n:
            perm0 = perm1[0]
            for i in range(r):
                perm1[i] = perm1[i + 1]
            perm1[r] = perm0
            count[r] -= 1
            if count[r] > 0:
                found = True
                break
            r += 1
        if not found:
            break

    return checksum, max_flips


t0 = time.perf_counter_ns()
checksum, max_flips = fannkuch(N)
t1 = time.perf_counter_ns()
print(checksum)
print(f"Pfannkuchen({N}) = {max_flips}")
print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")
