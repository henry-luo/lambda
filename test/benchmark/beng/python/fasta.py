#!/usr/bin/env python3
# BENG Benchmark: fasta (Python)
# Generate DNA sequences using LCG PRNG
import sys
import time

N = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
LINE_WIDTH = 60

ALU = "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAAGG"

IUB = [
    ('a', 0.27), ('c', 0.12), ('g', 0.12), ('t', 0.27),
    ('B', 0.02), ('D', 0.02), ('H', 0.02), ('K', 0.02),
    ('M', 0.02), ('N', 0.02), ('R', 0.02), ('S', 0.02),
    ('V', 0.02), ('W', 0.02), ('Y', 0.02)
]

HOMO = [
    ('a', 0.3029549426680), ('c', 0.1979883004921),
    ('g', 0.1975473066391), ('t', 0.3015094502008)
]


def make_cumulative(table):
    s = 0.0
    result = []
    for ch, prob in table:
        s += prob
        result.append((ch, s))
    return result


IM, IA, IC = 139968, 3877, 29573
seed = 42


def rand_next(max_val):
    global seed
    seed = (seed * IA + IC) % IM
    return max_val * seed / IM


def repeat_fasta(name, desc, src, n):
    print(f">{name} {desc}")
    src_len = len(src)
    k = 0
    buf = []
    for _ in range(n):
        buf.append(src[k])
        k = (k + 1) % src_len
        if len(buf) == LINE_WIDTH:
            print(''.join(buf))
            buf = []
    if buf:
        print(''.join(buf))


def random_fasta(name, desc, table, n):
    print(f">{name} {desc}")
    cum_table = make_cumulative(table)
    buf = []
    for _ in range(n):
        r = rand_next(1.0)
        ch = cum_table[-1][0]
        for c, prob in cum_table:
            if r < prob:
                ch = c
                break
        buf.append(ch)
        if len(buf) == LINE_WIDTH:
            print(''.join(buf))
            buf = []
    if buf:
        print(''.join(buf))


t0 = time.perf_counter_ns()
repeat_fasta("ONE", "Homo sapiens alu", ALU, N * 2)
random_fasta("TWO", "IUB ambiguity codes", IUB, N * 3)
random_fasta("THREE", "Homo sapiens frequency", HOMO, N * 5)
t1 = time.perf_counter_ns()
print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")
