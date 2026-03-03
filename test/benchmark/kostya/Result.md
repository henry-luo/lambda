# Kostya Benchmark Results — Lambda Script

**Date**: 2026-03-03
**Platform**: macOS, Apple Silicon (M-series)
**Lambda Build**: Release (LTO, stripped, optimized)
**Node.js**: v22.13.0
**Runs per benchmark**: 3 (median reported)
**Timing**: Wall-clock (total) + in-script clock (exec). JIT = Total − Exec.

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

## Results — Total Time (Wall-Clock)

| Benchmark | Category | Lambda | Node.js | Ratio |
|-----------|----------|--------|---------|-------|
| brainfuck | interp | 334.8 ms | 49.2 ms | 6.8× |
| matmul | numeric | 343.0 ms | 18.9 ms | 18.1× |
| primes | array | 26.6 ms | 8.2 ms | 3.2× |
| base64 | string | 996.3 ms | 23.1 ms | 43.1× |
| levenshtein | dp | 31.6 ms | 8.0 ms | 4.0× |
| json_gen | string | 81.6 ms | 8.9 ms | 9.2× |
| collatz | numeric | 2.460 s | 1.433 s | 1.7× |

| | Lambda | Node.js | Ratio |
|---|--------|---------|-------|
| **Total** | 4.274 s | 1.549 s | 2.8× |
| **Geometric mean** | 212.2 ms | 29.2 ms | 7.3× |

## Results — Exec Time Only (Excluding JIT/Startup Overhead)

| Benchmark | Lambda Exec | Node.js Exec | Ratio |
|-----------|-------------|--------------|-------|
| brainfuck | 325.7 ms | 45.9 ms | 7.1× |
| matmul | 333.6 ms | 16.2 ms | 20.6× |
| primes | 21.4 ms | 4.6 ms | 4.7× |
| base64 | 902.6 ms | 19.0 ms | 47.5× |
| levenshtein | 23.6 ms | 4.2 ms | 5.6× |
| json_gen | 66.1 ms | 6.4 ms | 10.3× |
| collatz | 2.455 s | 1.432 s | 1.7× |

| | Lambda Exec | Node.js Exec | Ratio |
|---|-------------|--------------|-------|
| **Total** | 4.128 s | 1.528 s | 2.7× |
| **Geometric mean** | 187.1 ms | 22.0 ms | 8.5× |

## JIT / Startup Overhead Breakdown

| Benchmark | Lambda Total | Lambda JIT | Lambda Exec | Node Total | Node JIT | Node Exec |
|-----------|-------------|------------|-------------|------------|----------|-----------|
| brainfuck | 334.8 ms | 9.1 ms | 325.7 ms | 49.2 ms | 3.3 ms | 45.9 ms |
| matmul | 343.0 ms | 9.4 ms | 333.6 ms | 18.9 ms | 2.7 ms | 16.2 ms |
| primes | 26.6 ms | 5.2 ms | 21.4 ms | 8.2 ms | 3.6 ms | 4.6 ms |
| base64 | 996.3 ms | 93.7 ms | 902.6 ms | 23.1 ms | 4.1 ms | 19.0 ms |
| levenshtein | 31.6 ms | 8.0 ms | 23.6 ms | 8.0 ms | 3.8 ms | 4.2 ms |
| json_gen | 81.6 ms | 15.5 ms | 66.1 ms | 8.9 ms | 2.6 ms | 6.4 ms |
| collatz | 2.460 s | 5.4 ms | 2.455 s | 1.433 s | 1.0 ms | 1.432 s |

**Lambda JIT overhead**: 5–94 ms (median ~9 ms). Includes parsing, AST build, MIR transpilation, and C2MIR JIT compilation.
**Node.js JIT overhead**: 1–4 ms (median ~3 ms). Includes V8 parsing, Ignition bytecode generation, and initial TurboFan compilation.

## Analysis

Node.js (V8 JIT) is **7.3× faster** on geometric mean (wall-clock) and **8.5× faster** on pure execution time across this suite. The gap varies significantly by workload:

- **Closest** (2–6×): collatz (pure integer loops), primes (sieve), levenshtein (DP arrays), json_gen (string concat)
- **Widest** (18–47×): matmul (dense FP arithmetic), base64 (byte-level string building)

The base64 gap (47×) reflects Lambda's string concatenation overhead — each encoded character appends to a growing string. Lambda's JIT overhead for base64 (94ms) is unusually high, suggesting the large literal tables in this benchmark are expensive to compile. The matmul gap (21×) reflects V8's superior floating-point loop optimization with typed arrays.

Lambda's JIT overhead is consistent at ~5–15 ms for most benchmarks, which is negligible for longer-running workloads (collatz: 0.2% overhead) but significant for fast ones (primes: 20% overhead).

## Origin

These benchmarks are adapted from:
- **Kostya Benchmarks**: https://github.com/kostya/benchmarks
- Various classic computer science problems used in cross-language benchmarking
