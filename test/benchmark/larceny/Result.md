# Larceny/Gambit Benchmark Results — Lambda Script

**Date**: 2026-03-03
**Platform**: macOS, Apple Silicon (M-series)
**Lambda Build**: Release (LTO, stripped, optimized)
**Node.js**: v22.13.0
**Runs per benchmark**: 3 (median reported)

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

## Results

| Benchmark | Category | Lambda | Node.js | Ratio |
|-----------|----------|--------|---------|-------|
| deriv | alloc | 67.2 ms | 6.4 ms | 10.5× |
| primes | array | 7.4 ms | 3.3 ms | 2.2× |
| pnpoly | numeric | 66.9 ms | 7.2 ms | 9.3× |
| diviter | iterative | 5.762 s | 445.7 ms | 12.9× |
| divrec | recursive | 15.9 ms | 9.0 ms | 1.8× |
| array1 | array | 17.9 ms | 3.5 ms | 5.1× |
| gcbench | gc | 2.831 s | 24.5 ms | 115.6× |
| quicksort | sort | 13.6 ms | 4.2 ms | 3.2× |
| triangl | backtrack | 1.792 s | 68.5 ms | 26.2× |
| puzzle | backtrack | 20.4 ms | 5.3 ms | 3.8× |
| ray | numeric | 16.2 ms | 5.6 ms | 2.9× |
| paraffins | recursive | 14.7 ms | 3.6 ms | 4.1× |

| | Lambda | Node.js | Ratio |
|---|--------|---------|-------|
| **Total time** | 10.625 s | 586.6 ms | 18.1× |
| **Geometric mean** | 71.6 ms | 10.4 ms | 6.9× |

## Analysis

Node.js (V8 JIT) is **6.9× faster** on geometric mean across this suite. The gap varies dramatically by workload type:

- **Closest** (2–4×): divrec (simple recursion), primes (small sieve), ray (FP math), quicksort (array sort), paraffins (combinatorics)
- **Medium** (5–13×): array1, puzzle, deriv, pnpoly, diviter
- **Widest** (26–109×): triangl (backtracking with array copies), gcbench (GC stress with deep tree allocation)

The gcbench gap (116×) is the largest outlier — this benchmark is a GC stress test that allocates millions of small binary tree nodes. V8's generational GC with fast nursery allocation dominates here. The triangl gap (26×) reflects Lambda's overhead in array mutation and backtracking state management.

## Origin

These benchmarks are adapted from:
- **Larceny Benchmark Suite**: https://www.larcenists.org/benchmarks.html
- **Gambit Scheme Benchmarks**: Classic Gabriel benchmarks and extensions
- Various well-known algorithmic benchmarks used in programming language evaluation
