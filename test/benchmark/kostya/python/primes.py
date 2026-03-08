#!/usr/bin/env python3
# Kostya Benchmark: primes (Python)
# Sieve of Eratosthenes counting primes up to 1,000,000
import time
import array


def sieve(limit):
    flags = array.array('b', [1] * (limit + 1))
    flags[0] = 0
    flags[1] = 0
    i = 2
    while i * i <= limit:
        if flags[i]:
            j = i * i
            while j <= limit:
                flags[j] = 0
                j += i
        i += 1
    return sum(flags[2:])


def main():
    t0 = time.perf_counter_ns()
    result = sieve(1000000)
    t1 = time.perf_counter_ns()

    if result == 78498:
        print(f"primes: PASS ({result})")
    else:
        print(f"primes: FAIL result={result}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
