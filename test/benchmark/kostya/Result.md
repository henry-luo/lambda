# Kostya Benchmark Results — Lambda Script

**Date**: (pending first run)
**Platform**: macOS, Apple Silicon
**Runs per benchmark**: 3 (median reported)

## Overview

This suite implements common cross-language benchmarks adapted from [github.com/kostya/benchmarks](https://github.com/kostya/benchmarks). These benchmarks are widely used for comparing performance across programming languages with simple, self-contained programs.

## Benchmark Descriptions

| Benchmark | Category | Workload | Description |
|-----------|----------|----------|-------------|
| brainfuck | interp | 10000 iterations | Brainfuck interpreter for "Hello World" program |
| matmul | numeric | 200×200 | Dense matrix multiplication |
| primes | array | Sieve to 1M | Sieve of Eratosthenes counting primes to 1,000,000 |
| base64 | string | 10KB, 100× | Base64 encoding of 10,000-byte input |
| levenshtein | dp | 500 chars | Levenshtein edit distance via DP |
| json_gen | string | 1000 objects, 10× | JSON object generation and string building |
| collatz | numeric | N < 1M | Longest Collatz (3n+1) sequence under 1,000,000 |

## Results

(Run `python3 test/benchmark/kostya/run_bench.py` to populate)

## Origin

These benchmarks are adapted from:
- **Kostya Benchmarks**: https://github.com/kostya/benchmarks
- Various classic computer science problems used in cross-language benchmarking
