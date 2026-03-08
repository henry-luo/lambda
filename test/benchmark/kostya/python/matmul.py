#!/usr/bin/env python3
# Kostya Benchmark: matmul (Python)
# Matrix multiplication — multiply two NxN matrices
import time
import array

N = 200


def next_rand(seed):
    return (seed * 1664525 + 1013904223) % 1000000


def matmul(a, b, c, n):
    for i in range(n):
        for j in range(n):
            s = 0.0
            row = i * n
            for k in range(n):
                s += a[row + k] * b[k * n + j]
            c[i * n + j] = s


def main():
    size = N * N
    a = array.array('d', [0.0] * size)
    b = array.array('d', [0.0] * size)
    c = array.array('d', [0.0] * size)

    seed = 42
    for i in range(size):
        seed = next_rand(seed)
        a[i] = (seed % 2000) / 1000.0 - 1.0
        seed = next_rand(seed)
        b[i] = (seed % 2000) / 1000.0 - 1.0

    t0 = time.perf_counter_ns()
    matmul(a, b, c, N)
    t1 = time.perf_counter_ns()

    total = sum(c)
    int_total = int(total)
    print(f"matmul: sum={int_total}")
    print("matmul: DONE")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
