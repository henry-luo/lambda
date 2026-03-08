#!/usr/bin/env python3
# Larceny Benchmark: primes (Python)
# Sieve of Eratosthenes to 8000, repeated 10 times
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
    count = 0
    for i in range(2, limit + 1):
        if flags[i]:
            count += 1
    return count


def main():
    t0 = time.perf_counter_ns()
    result = 0
    for _ in range(10):
        result = sieve(8000)
    t1 = time.perf_counter_ns()

    if result == 1007:
        print("primes: PASS")
    else:
        print(f"primes: FAIL result={result}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
