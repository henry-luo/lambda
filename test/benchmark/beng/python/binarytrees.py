#!/usr/bin/env python3
# BENG Benchmark: binary-trees (Python)
# Allocate and deallocate binary trees to stress GC
import sys
import time

N = int(sys.argv[1]) if len(sys.argv) > 1 else 10


def make(depth):
    if depth == 0:
        return (None, None)
    return (make(depth - 1), make(depth - 1))


def check(node):
    if node[0] is None:
        return 1
    return 1 + check(node[0]) + check(node[1])


sys.setrecursionlimit(100000)

t0 = time.perf_counter_ns()
max_depth = max(6, N)
stretch_depth = max_depth + 1
print(f"stretch tree of depth {stretch_depth}\t check: {check(make(stretch_depth))}")

long_lived_tree = make(max_depth)

depth = 4
while depth <= max_depth:
    iterations = 1 << (max_depth - depth + 4)
    total = 0
    for _ in range(iterations):
        total += check(make(depth))
    print(f"{iterations}\t trees of depth {depth}\t check: {total}")
    depth += 2

print(f"long lived tree of depth {max_depth}\t check: {check(long_lived_tree)}")
t1 = time.perf_counter_ns()
print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")
