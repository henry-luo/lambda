#!/usr/bin/env python3
# BENG Benchmark: reverse-complement (Python)
# Read FASTA, output reverse-complement of each sequence
import sys
import time

INPUT_PATH = sys.argv[1] if len(sys.argv) > 1 else "test/benchmark/beng/input/fasta_1000.txt"
LINE_WIDTH = 60

COMPLEMENT = str.maketrans(
    'ACTGMKRYVBHDWSNacgtmkryvbhdwsn',
    'TGACKMYRBVDHWSNTGACkMYRBVDHWSN'
)

with open(INPUT_PATH, 'r') as f:
    text = f.read()

lines = text.split('\n')

t0 = time.perf_counter_ns()
header = ''
seq = []
for line in lines:
    if line and line[0] == '>':
        if seq:
            combined = ''.join(seq).upper()
            rev_comp = combined.translate(COMPLEMENT)[::-1]
            print(header)
            for pos in range(0, len(rev_comp), LINE_WIDTH):
                print(rev_comp[pos:pos + LINE_WIDTH])
        header = line
        seq = []
    elif line:
        seq.append(line)

if seq:
    combined = ''.join(seq).upper()
    rev_comp = combined.translate(COMPLEMENT)[::-1]
    print(header)
    for pos in range(0, len(rev_comp), LINE_WIDTH):
        print(rev_comp[pos:pos + LINE_WIDTH])

t1 = time.perf_counter_ns()
print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")
