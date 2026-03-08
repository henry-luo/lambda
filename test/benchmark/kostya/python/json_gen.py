#!/usr/bin/env python3
# Kostya Benchmark: json_gen (Python)
# JSON generation — build a large JSON structure and serialize
import time


def next_rand(seed):
    return (seed * 1664525 + 1013904223) % 1000000


def benchmark():
    num_objects = 1000
    seed = 42
    parts = ["["]
    for i in range(num_objects):
        if i > 0:
            parts.append(",")
        seed = next_rand(seed)
        obj_id = seed % 10000
        seed = next_rand(seed)
        x = int((seed % 20000 - 10000) / 100.0)
        seed = next_rand(seed)
        y = int((seed % 20000 - 10000) / 100.0)
        seed = next_rand(seed)
        score = seed % 100
        coord = '{"x":' + str(x) + ',"y":' + str(y) + '}'
        obj = '{"id":' + str(obj_id) + ',"score":' + str(score) + ',"coord":' + coord + ',"active":true}'
        parts.append(obj)
    parts.append("]")
    return len(''.join(parts))


def main():
    t0 = time.perf_counter_ns()
    result = 0
    for _ in range(10):
        result = benchmark()
    t1 = time.perf_counter_ns()

    print(f"json_gen: length={result}")
    if result > 0:
        print("json_gen: PASS")
    else:
        print("json_gen: FAIL")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
