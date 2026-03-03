# Kostya Benchmark Results — Lambda Script

**Date**: 2026-03-03
**Platform**: macOS, Apple Silicon (M-series)
**Lambda Build**: Release (LTO, stripped, optimized)
**Node.js**: v22.13.0
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

| Benchmark | Category | Lambda | Node.js | Ratio |
|-----------|----------|--------|---------|-------|
| brainfuck | interp | 356.7 ms | 45.7 ms | 7.8× |
| matmul | numeric | 360.9 ms | 16.5 ms | 21.9× |
| primes | array | 28.1 ms | 5.2 ms | 5.4× |
| base64 | string | 1.171 s | 19.1 ms | 61.3× |
| levenshtein | dp | 34.4 ms | 5.7 ms | 6.0× |
| json_gen | string | 88.6 ms | 21.2 ms | 4.2× |
| collatz | numeric | 2.576 s | 1.337 s | 1.9× |

| | Lambda | Node.js | Ratio |
|---|--------|---------|-------|
| **Total time** | 4.616 s | 1.451 s | 3.2× |
| **Geometric mean** | 229.2 ms | 27.6 ms | 8.3× |

## Analysis

Node.js (V8 JIT) is **8.3× faster** on geometric mean across this suite. The gap varies significantly by workload:

- **Closest** (2–6×): collatz (pure integer loops), json_gen (string concat), primes (sieve), levenshtein (DP arrays)
- **Widest** (20–60×): matmul (dense FP arithmetic), base64 (byte-level string building)

The base64 gap (61.3×) reflects Lambda's string concatenation overhead — each encoded character appends to a growing string. The matmul gap (21.9×) reflects V8's superior floating-point loop optimization with typed arrays.

## Origin

These benchmarks are adapted from:
- **Kostya Benchmarks**: https://github.com/kostya/benchmarks
- Various classic computer science problems used in cross-language benchmarking
