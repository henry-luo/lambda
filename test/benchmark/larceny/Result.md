# Larceny/Gambit Benchmark Results — Lambda Script

**Date**: (pending first run)
**Platform**: macOS, Apple Silicon
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
| paraffins | recursive | n=17 | Paraffin (alkane) isomer counting |

## Results

(Run `python3 test/benchmark/larceny/run_bench.py` to populate)

## Origin

These benchmarks are adapted from:
- **Larceny Benchmark Suite**: https://www.larcenists.org/benchmarks.html
- **Gambit Scheme Benchmarks**: Classic Gabriel benchmarks and extensions
- Various well-known algorithmic benchmarks used in programming language evaluation
