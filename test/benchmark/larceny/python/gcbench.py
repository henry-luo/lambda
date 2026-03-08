#!/usr/bin/env python3
# Larceny Benchmark: gcbench (Python)
# GC stress test — binary tree allocation and traversal
import time


def make_tree(depth):
    if depth == 0:
        return (None, None)
    return (make_tree(depth - 1), make_tree(depth - 1))


def check_tree(node):
    if node[0] is None:
        return 1
    return 1 + check_tree(node[0]) + check_tree(node[1])


def main():
    import sys
    sys.setrecursionlimit(100000)

    t0 = time.perf_counter_ns()
    min_depth = 4
    max_depth = 14
    stretch_depth = max_depth + 1

    stretch = make_tree(stretch_depth)
    print(f"stretch tree of depth {stretch_depth} check: {check_tree(stretch)}")

    long_lived = make_tree(max_depth)

    depth = min_depth
    while depth <= max_depth:
        iterations = 1 << (max_depth - depth + min_depth)
        total_check = 0
        for _ in range(iterations):
            total_check += check_tree(make_tree(depth))
        print(f"{iterations} trees of depth {depth} check: {total_check}")
        depth += 2
    t1 = time.perf_counter_ns()

    print(f"long lived tree of depth {max_depth} check: {check_tree(long_lived)}")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
