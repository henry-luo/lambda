#!/usr/bin/env python3
# Larceny Benchmark: quicksort (Python)
# Quicksort with Lomuto partition
import time
import array


def lcg_next(seed):
    return (seed * 1664525 + 1013904223) % 1000000


def partition(arr, lo, hi):
    pivot = arr[hi]
    i = lo
    for j in range(lo, hi):
        if arr[j] <= pivot:
            arr[i], arr[j] = arr[j], arr[i]
            i += 1
    arr[i], arr[hi] = arr[hi], arr[i]
    return i


def quicksort(arr, lo, hi):
    if lo >= hi:
        return
    p = partition(arr, lo, hi)
    quicksort(arr, lo, p - 1)
    quicksort(arr, p + 1, hi)


def is_sorted(arr, n):
    for i in range(1, n):
        if arr[i] < arr[i - 1]:
            return False
    return True


def main():
    import sys
    sys.setrecursionlimit(100000)

    size = 5000
    arr = array.array('i', [0] * size)
    seed = 42
    for i in range(size):
        seed = lcg_next(seed)
        arr[i] = seed

    t0 = time.perf_counter_ns()
    quicksort(arr, 0, size - 1)
    t1 = time.perf_counter_ns()

    if is_sorted(arr, size):
        print("quicksort: PASS")
    else:
        print("quicksort: FAIL")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
