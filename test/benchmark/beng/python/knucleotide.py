#!/usr/bin/env python3
# BENG Benchmark: k-nucleotide (Python)
# Count nucleotide k-mer frequencies from FASTA >THREE section
import sys
import time

INPUT_PATH = sys.argv[1] if len(sys.argv) > 1 else "test/benchmark/beng/input/fasta_1000.txt"


def extract_three(text):
    lines = text.split('\n')
    seq = []
    in_three = False
    for line in lines:
        if line and line[0] == '>':
            if in_three:
                break
            if line.startswith('>THREE'):
                in_three = True
        elif in_three and line:
            seq.append(line.upper())
    return ''.join(seq)


def count_kmers(seq, k):
    counts = {}
    n = len(seq) - k + 1
    for i in range(n):
        kmer = seq[i:i + k]
        counts[kmer] = counts.get(kmer, 0) + 1
    return counts


def print_frequencies(seq, k):
    counts = count_kmers(seq, k)
    total = len(seq) - k + 1
    entries = sorted(counts.items(), key=lambda x: (-x[1], x[0]))
    for kmer, c in entries:
        freq = c * 100.0 / total
        print(f"{kmer} {freq:.3f}")
    print()


def print_count(seq, kmer):
    counts = count_kmers(seq, len(kmer))
    c = counts.get(kmer, 0)
    print(f"{c}\t{kmer}")


with open(INPUT_PATH, 'r') as f:
    text = f.read()
seq = extract_three(text)

t0 = time.perf_counter_ns()
print_frequencies(seq, 1)
print_frequencies(seq, 2)
print_count(seq, "GGT")
print_count(seq, "GGTA")
print_count(seq, "GGTATT")
print_count(seq, "GGTATTTTAATT")
print_count(seq, "GGTATTTTAATTTATAGT")
t1 = time.perf_counter_ns()
print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")
