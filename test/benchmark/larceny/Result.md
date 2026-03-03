# Larceny/Gambit Benchmark Results — Lambda Script

**Date**: 2026-03-03
**Platform**: macOS, Apple Silicon (M-series)
**Lambda Build**: Release (LTO, stripped, optimized)
**Node.js**: v22.13.0
**Runs per benchmark**: 3 (median reported)
**Timing**: Wall-clock (total) + in-script clock (exec). JIT = Total − Exec.

## Overview

This suite implements classic benchmarks from the Larceny/Gambit Scheme benchmark collection, adapted for Lambda Script. These benchmarks complement the R7RS suite with additional tests covering symbolic computation, array operations, GC stress, sorting, backtracking, and numeric computation.

## Benchmark Descriptions

| Benchmark | Category | Workload | Description |
|-----------|----------|----------|-------------|
| deriv | alloc | 5000 iterations | Symbolic differentiation of polynomial expression trees |
| primes | array | Sieve to 8000, 10× | Sieve of Eratosthenes prime counting |
| pnpoly | numeric | 100K point tests | Point-in-polygon ray casting classification |
| diviter | iterative | 1000 iterations | Integer division via repeated subtraction (iterative) |
| divrec | recursive | 1000 iterations | Integer division via repeated subtraction (recursive) |
| array1 | array | 10000 elements, 100× | Array creation, fill, and summation |
| gcbench | gc | Trees depth 4-14 | GC stress: binary tree allocation and traversal |
| quicksort | sort | 5000 elements, 10× | Quicksort with pseudo-random data |
| triangl | backtrack | 15 positions | Triangle solitaire board puzzle, count solutions |
| puzzle | backtrack | 10×10 board | N-Queens all solutions for n=10 |
| ray | numeric | 100×100, 10× | Ray tracer: rays against 4 spheres |
| paraffins | recursive | n=23, 10× | Paraffin (alkane) isomer counting |

## Results — Total Time (Wall-Clock)

| Benchmark | Category | Lambda | Node.js | Ratio |
|-----------|----------|--------|---------|-------|
| deriv | alloc | 62.9 ms | 5.5 ms | 11.4× |
| primes | array | 6.6 ms | 2.8 ms | 2.4× |
| pnpoly | numeric | 62.1 ms | 7.0 ms | 8.9× |
| diviter | iterative | 5.444 s | 470.5 ms | 11.6× |
| divrec | recursive | 14.4 ms | 8.9 ms | 1.6× |
| array1 | array | 15.5 ms | 2.7 ms | 5.7× |
| gcbench | gc | 2.528 s | 24.3 ms | 104.0× |
| quicksort | sort | 12.1 ms | 3.3 ms | 3.7× |
| triangl | backtrack | 1.677 s | 68.6 ms | 24.4× |
| puzzle | backtrack | 18.4 ms | 4.7 ms | 3.9× |
| ray | numeric | 14.2 ms | 4.5 ms | 3.2× |
| paraffins | recursive | 12.9 ms | 1.7 ms | 7.6× |

| | Lambda | Node.js | Ratio |
|---|--------|---------|-------|
| **Total** | 9.868 s | 604.5 ms | 16.3× |
| **Geometric mean** | 64.8 ms | 8.9 ms | 7.3× |

## Results — Exec Time Only (Excluding JIT/Startup Overhead)

| Benchmark | Lambda Exec | Node.js Exec | Ratio |
|-----------|-------------|--------------|-------|
| deriv | 55.5 ms | 3.7 ms | 15.0× |
| primes | 1.5 ms | 1.7 ms | 0.9× |
| pnpoly | 55.4 ms | 5.8 ms | 9.6× |
| diviter | 5.439 s | 469.3 ms | 11.6× |
| divrec | 9.6 ms | 7.8 ms | 1.2× |
| array1 | 11.1 ms | 1.9 ms | 5.8× |
| gcbench | 2.519 s | 23.9 ms | 105.4× |
| quicksort | 6.5 ms | 1.6 ms | 4.1× |
| triangl | 1.666 s | 67.1 ms | 24.8× |
| puzzle | 12.7 ms | 3.3 ms | 3.8× |
| ray | 6.9 ms | 3.5 ms | 2.0× |
| paraffins | 1.1 ms | 1.0 ms | 1.1× |

| | Lambda Exec | Node.js Exec | Ratio |
|---|-------------|--------------|-------|
| **Total** | 9.784 s | 590.6 ms | 16.6× |
| **Geometric mean** | 37.2 ms | 6.6 ms | 5.6× |

## JIT / Startup Overhead Breakdown

| Benchmark | Lambda Total | Lambda JIT | Lambda Exec | Node Total | Node JIT | Node Exec |
|-----------|-------------|------------|-------------|------------|----------|-----------|
| deriv | 62.9 ms | 7.4 ms | 55.5 ms | 5.5 ms | 1.8 ms | 3.7 ms |
| primes | 6.6 ms | 5.1 ms | 1.5 ms | 2.8 ms | 1.1 ms | 1.7 ms |
| pnpoly | 62.1 ms | 6.7 ms | 55.4 ms | 7.0 ms | 1.2 ms | 5.8 ms |
| diviter | 5.444 s | 5.1 ms | 5.439 s | 470.5 ms | 1.2 ms | 469.3 ms |
| divrec | 14.4 ms | 4.8 ms | 9.6 ms | 8.9 ms | 1.1 ms | 7.8 ms |
| array1 | 15.5 ms | 4.4 ms | 11.1 ms | 2.7 ms | 0.8 ms | 1.9 ms |
| gcbench | 2.528 s | 9.0 ms | 2.519 s | 24.3 ms | 0.4 ms | 23.9 ms |
| quicksort | 12.1 ms | 5.6 ms | 6.5 ms | 3.3 ms | 1.7 ms | 1.6 ms |
| triangl | 1.677 s | 11.4 ms | 1.666 s | 68.6 ms | 1.5 ms | 67.1 ms |
| puzzle | 18.4 ms | 5.7 ms | 12.7 ms | 4.7 ms | 1.4 ms | 3.3 ms |
| ray | 14.2 ms | 7.4 ms | 6.9 ms | 4.5 ms | 1.0 ms | 3.5 ms |
| paraffins | 12.9 ms | 11.8 ms | 1.1 ms | 1.7 ms | 0.8 ms | 1.0 ms |

**Lambda JIT overhead**: 4–12 ms (median ~6 ms). Includes parsing, AST build, MIR transpilation, and C2MIR JIT compilation.
**Node.js JIT overhead**: 0.4–1.8 ms (median ~1.2 ms). Includes V8 parsing, Ignition bytecode generation, and initial TurboFan compilation.

## Analysis

Node.js (V8 JIT) is **7.3× faster** on geometric mean (wall-clock) and **5.6× faster** on pure execution time across this suite. The exec-only gap is smaller because Lambda's JIT overhead (4–12 ms) is proportionally significant for fast benchmarks.

**Notable exec-time findings:**
- **primes** (0.9×): Lambda's pure execution is *faster* than Node.js — the sieve loop over a small array (8000) runs more efficiently in C2MIR compiled code.
- **paraffins** (1.1×): Essentially tied — recursive combinatorics with small data.
- **divrec** (1.2×): Near parity for simple recursive integer arithmetic.

**Workload categories:**
- **Closest** (1–4×): primes, paraffins, divrec, ray, puzzle, quicksort
- **Medium** (5–15×): array1, pnpoly, deriv, diviter
- **Widest** (25–105×): triangl (backtracking with array copies), gcbench (GC stress with deep tree allocation)

The gcbench gap (105×) is the largest outlier — this benchmark is a GC stress test that allocates millions of small binary tree nodes. V8's generational GC with fast nursery allocation dominates here. The triangl gap (25×) reflects Lambda's overhead in array mutation and backtracking state management.

## Origin

These benchmarks are adapted from:
- **Larceny Benchmark Suite**: https://www.larcenists.org/benchmarks.html Classic Scheme/Lisp benchmarks from the Larceny and Gambit Scheme implementations
- **Gambit Scheme Benchmarks**: Classic Gabriel benchmarks and extensions
- Various well-known algorithmic benchmarks used in programming language evaluation
