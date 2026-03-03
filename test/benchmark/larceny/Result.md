# Larceny/Gambit Benchmark Results — Lambda Script

**Date**: 2026-03-03
**Platform**: macOS, Apple Silicon (M-series)
**Build**: Release (LTO, stripped, optimized)
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

| Benchmark | Category | Median Time | Status |
|-----------|----------|-------------|--------|
| deriv | alloc | 64.4 ms | PASS |
| primes | array | 7.0 ms | PASS |
| pnpoly | numeric | 66.9 ms | PASS |
| diviter | iterative | 5.731 s | PASS |
| divrec | recursive | 16.2 ms | PASS |
| array1 | array | 17.7 ms | PASS |
| gcbench | gc | 2.675 s | PASS |
| quicksort | sort | 13.3 ms | PASS |
| triangl | backtrack | 1.798 s | PASS |
| puzzle | backtrack | 21.5 ms | PASS |
| ray | numeric | 17.2 ms | PASS |
| paraffins | recursive | 15.2 ms | PASS |

**Total time**: 10.443 s
**Geometric mean**: 71.5 ms

## Origin

These benchmarks are adapted from:
- **Larceny Benchmark Suite**: https://www.larcenists.org/benchmarks.html
- **Gambit Scheme Benchmarks**: Classic Gabriel benchmarks and extensions
- Various well-known algorithmic benchmarks used in programming language evaluation
