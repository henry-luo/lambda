"""Scalar Julia-inspired workloads; see ../SUITE.md for the shared contract."""
import math
import os
import time

MOD = 1000000007


def decimal_text(value):
    negative = value < 0
    value = abs(value)
    divisor = 1
    while value // divisor >= 10:
        divisor *= 10
    text = "-" if negative else ""
    while divisor > 0:
        text += chr(48 + value // divisor)
        value %= divisor
        divisor //= 10
    return text


def decimal_value(text):
    negative = text[0] == "-"
    index = 1 if negative else 0
    value = 0
    while index < len(text):
        value = value * 10 + ord(text[index]) - 48
        index += 1
    return -value if negative else value


def parse_integers():
    seed, checksum, size, errors = 42, 0, 0, 0
    for index in range(100000):
        seed = seed * 16807 % 2147483647
        value = 0 if index % 8 == 0 else (-seed if index % 8 == 1 else seed)
        text = decimal_text(value)
        parsed = decimal_value(text)
        errors += parsed != value
        size += len(text)
        checksum = (checksum * 31 + parsed + 2147483647) % MOD
    return [checksum, size, seed, errors]


def gram(matrix, rows, columns):
    result = [0.0] * (columns * columns)
    for i in range(columns):
        for j in range(columns):
            total = 0.0
            for k in range(rows):
                total += matrix[k * columns + i] * matrix[k * columns + j]
            result[i * columns + j] = total
    return result


def square(matrix, n):
    result = [0.0] * (n * n)
    for i in range(n):
        for j in range(n):
            total = 0.0
            for k in range(n):
                total += matrix[i * n + k] * matrix[k * n + j]
            result[i * n + j] = total
    return result


def trace_fourth(matrix, rows, columns):
    fourth = square(square(gram(matrix, rows, columns), columns), columns)
    total = 0.0
    for i in range(columns):
        total += fourth[i * columns + i]
    return total


def variation(values):
    total = 0.0
    for value in values:
        total += value
    mean = total / len(values)
    total = 0.0
    for value in values:
        delta = value - mean
        total += delta * delta
    return math.sqrt(total / (len(values) - 1)) / mean


def matrix_statistics():
    seed, digest = 42, 0
    v, w = [0.0] * 1000, [0.0] * 1000
    for iteration in range(1000):
        blocks, p, q = [0.0] * 100, [0.0] * 100, [0.0] * 100
        for i in range(100):
            seed = seed * 16807 % 2147483647
            blocks[i] = seed / 2147483647.0 * 2.0 - 1.0
        for block in range(4):
            for row in range(5):
                for column in range(5):
                    value = blocks[block * 25 + row * 5 + column]
                    p[row * 20 + block * 5 + column] = value
                    q[(block // 2 * 5 + row) * 10 + block % 2 * 5 + column] = value
        v[iteration] = trace_fourth(p, 5, 20)
        w[iteration] = trace_fourth(q, 10, 10)
        digest = (digest * 31 + math.floor(v[iteration] * 1000)) % MOD
        digest = (digest * 31 + math.floor(w[iteration] * 1000)) % MOD
    return [math.floor(variation(v) * 1e9), math.floor(variation(w) * 1e9), digest, seed]


def iteration_pi_sum():
    values = [0.0] * 500
    for iteration in range(500):
        total = 0.0
        for k in range(1, 10001 + iteration):
            total += 1.0 / (float(k) * float(k))
        values[iteration] = total
    digest = 0.0
    for i in range(500):
        digest += values[i] * (i + 1)
    return [math.floor(values[0] * 1e12), math.floor(values[-1] * 1e12), math.floor(digest * 1e6), 5124750]


def formatted_output():
    size, digest, writes, buffer = 0, 0, 0, ""
    for i in range(1, 100001):
        line = decimal_text(i) + " " + decimal_text(i + 1) + "\n"
        for char in line:
            digest = (digest * 31 + ord(char)) % MOD
        size += len(line)
        buffer += line
        if i % 256 == 0 or i == 100000:
            # Every port opens, writes and closes the null sink once per batch.
            with open(os.devnull, "w", encoding="ascii", newline="") as sink:
                assert sink.write(buffer) == len(buffer)
            writes += 1
            buffer = ""
    return [size, digest, writes, 100000]


EXPECTED = {'parse_integers': [592470661, 854479, 1966931148, 0], 'iteration_pi_sum': [1644834071848, 1644838824217, 206015869118, 5124750], 'formatted_output': [1177795, 584298900, 391, 100000], 'matrix_statistics': [464726438, 486656926, 47509838, 1966931148]}


def run_benchmark(name):
    workload = globals()[name]
    assert workload() == EXPECTED[name], "warmup: FAIL"
    started = time.perf_counter_ns()
    result = workload()
    elapsed = (time.perf_counter_ns() - started) / 1e6
    assert result == EXPECTED[name], f"{name}: FAIL {result}"
    print(name + ": PASS " + " ".join(map(str, result)))
    print("__TIMING__:" + str(elapsed))
