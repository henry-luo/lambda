#!/usr/bin/env python3
# BENG Benchmark: regex-redux (Python)
# Read FASTA, count regex matches, perform IUPAC substitutions
import sys
import re
import time

INPUT_PATH = sys.argv[1] if len(sys.argv) > 1 else "test/benchmark/beng/input/fasta_1000.txt"

with open(INPUT_PATH, 'r') as f:
    text = f.read()
original_len = len(text)

t0 = time.perf_counter_ns()
# remove FASTA headers and newlines
text = re.sub(r'>[^\n]*\n', '', text)
text = text.replace('\n', '')
clean_len = len(text)

patterns = [
    ('agggtaaa|tttaccct', re.compile(r'agggtaaa|tttaccct')),
    ('[cgt]gggtaaa|tttaccc[acg]', re.compile(r'[cgt]gggtaaa|tttaccc[acg]')),
    ('a[act]ggtaaa|tttacc[agt]t', re.compile(r'a[act]ggtaaa|tttacc[agt]t')),
    ('ag[act]gtaaa|tttac[agt]ct', re.compile(r'ag[act]gtaaa|tttac[agt]ct')),
    ('agg[act]taaa|ttta[agt]cct', re.compile(r'agg[act]taaa|ttta[agt]cct')),
    ('aggg[acg]aaa|ttt[cgt]ccct', re.compile(r'aggg[acg]aaa|ttt[cgt]ccct')),
    ('agggt[cgt]aa|tt[acg]taccct', re.compile(r'agggt[cgt]aa|tt[acg]taccct')),
    ('agggta[cgt]a|t[acg]ataccct', re.compile(r'agggta[cgt]a|t[acg]ataccct')),
    ('agggtaa[cgt]|[acg]aataccct', re.compile(r'agggtaa[cgt]|[acg]aataccct')),
]

for label, pat in patterns:
    count = len(pat.findall(text))
    print(f"{label} {count}")

substitutions = [
    (re.compile(r'B'), '(c|g|t)'),
    (re.compile(r'D'), '(a|g|t)'),
    (re.compile(r'H'), '(a|c|t)'),
    (re.compile(r'K'), '(g|t)'),
    (re.compile(r'M'), '(a|c)'),
    (re.compile(r'N'), '(a|c|g|t)'),
    (re.compile(r'R'), '(a|g)'),
    (re.compile(r'S'), '(c|g)'),
    (re.compile(r'V'), '(a|c|g)'),
    (re.compile(r'W'), '(a|t)'),
    (re.compile(r'Y'), '(c|t)'),
]

result = text
for pat, repl in substitutions:
    result = pat.sub(repl, result)

t1 = time.perf_counter_ns()
print()
print(original_len)
print(clean_len)
print(len(result))
print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")
