# Lambda Benchmark Results: Result24

- **Date:** 2026-08-07
- **Platform:** Darwin arm64
- **Lambda commit:** `83a7099e4edd01373a2987caaf1a732b4d8552a1`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v24-83a7099e4e` (21,214,968 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 123.20s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 123.1s (batched 122.3s: sync 88.8s, async 33.5s; non-batched 0.7s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run; suites run in order `r7rs -> awfy -> beng -> kostya -> larceny -> jetstream` with a 10s idle gap between suites
- **Engines in this report:** MIR (untyped), MIR (typed), LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v24.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 9 | 10 | 1.59x | 0.62x | 6.49x | 6.37x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 2.85x | 2.04x | 24.2x | 5.21x |
| BENG | 8 | 8 | 8 | 8 | 5 | 8 | 0.67x | 0.59x | 7.17x | 1.88x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 4.14x | 3.04x | 16.0x | 11.8x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 3.60x | 2.18x | 14.8x | 13.3x |
| JetStream | 6 | 6 | 6 | 6 | 4 | 6 | 11.2x | 6.98x | 68.8x | 12.7x |
| **Overall** | 56 | 56 | 56 | 56 | 50 | 56 | 2.65x | 1.68x | 15.5x | 7.22x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Notable Results

- Missing timings: **6** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 43.47s | 97.1 | 448x |
| awfy/cd | 9.61s | 35.9 | 267x |
| jetstream/hashmap | 3.21s | 15.3 | 210x |
| jetstream/crypto_sha1 | 1.71s | 8.71 | 197x |
| beng/spectralnorm | 299.4 | 2.59 | 116x |
| awfy/nbody | 575.7 | 5.46 | 105x |
| awfy/deltablue | 950.6 | 11.5 | 82.8x |
| larceny/triangl | 4.13s | 66.8 | 61.8x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.314 | 1.92 | 0.16x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 6.00 | 1.91 | 26.9 | 18.5 | 1.81 | 3.32x | 1.06x | 14.9x | 10.3x |
| fibfp | recursive | 8.74 | 3.58 | 26.5 | 18.7 | 1.73 | 5.06x | 2.07x | 15.4x | 10.8x |
| tak | recursive | 0.580 | 0.201 | 2.19 | 2.79 | 0.776 | 0.75x | 0.26x | 2.83x | 3.59x |
| cpstak | closure | 1.16 | 0.402 | 4.42 | 5.57 | 0.976 | 1.19x | 0.41x | 4.52x | 5.70x |
| sum | iterative | 0.854 | 0.833 | 13.3 | 30.9 | 1.23 | 0.69x | 0.68x | 10.8x | 25.1x |
| sumfp | iterative | 0.321 | 0.318 | 1.35 | 3.64 | 0.870 | 0.37x | 0.37x | 1.55x | 4.19x |
| nqueens | backtrack | 1.96 | 2.16 | 21.5 | 7.89 | 1.82 | 1.08x | 1.19x | 11.8x | 4.34x |
| fft | numeric | 2.57 | 0.422 | 11.1 | 2.75 | 1.63 | 1.58x | 0.26x | 6.79x | 1.68x |
| mbrot | numeric | 11.5 | 0.696 | 10.3 | 18.3 | 1.85 | 6.20x | 0.38x | 5.56x | 9.89x |
| ack | recursive | 35.0 | 16.6 | 80.3 | --- | 13.4 | 2.62x | 1.24x | 6.01x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.623 | 0.120 | 0.535 | 0.619 | 0.381 | 1.63x | 0.31x | 1.40x | 1.62x |
| permute | micro | 0.930 | 1.40 | 8.75 | 1.53 | 0.821 | 1.13x | 1.70x | 10.7x | 1.87x |
| queens | micro | 0.562 | 0.539 | 5.58 | 1.05 | 0.646 | 0.87x | 0.83x | 8.64x | 1.63x |
| towers | micro | 1.37 | 2.08 | 24.0 | 2.28 | 1.12 | 1.23x | 1.87x | 21.5x | 2.05x |
| bounce | micro | 0.282 | 0.762 | 4.65 | 0.865 | 0.538 | 0.52x | 1.42x | 8.64x | 1.61x |
| list | micro | 0.972 | 1.02 | 3.12 | 0.915 | 0.485 | 2.01x | 2.10x | 6.43x | 1.89x |
| storage | micro | 0.822 | 1.45 | 14.2 | 2.16 | 0.648 | 1.27x | 2.23x | 21.8x | 3.33x |
| mandelbrot | compute | 50.0 | 49.9 | 347.9 | 872.2 | 31.1 | 1.61x | 1.61x | 11.2x | 28.1x |
| nbody | compute | 168.4 | 30.3 | 575.7 | 159.3 | 5.46 | 30.9x | 5.55x | 105x | 29.2x |
| richards | macro | 2.56s | 253.6 | 1.67s | 191.4 | 47.1 | 54.4x | 5.38x | 35.5x | 4.06x |
| json | macro | 7.76 | 2.70 | 44.9 | 10.9 | 2.68 | 2.90x | 1.01x | 16.8x | 4.07x |
| deltablue | macro | 97.3 | 94.5 | 950.6 | 99.8 | 11.5 | 8.48x | 8.22x | 82.8x | 8.69x |
| havlak | macro | 52.1 | 52.2 | 43.47s | 3.29s | 97.1 | 0.54x | 0.54x | 448x | 33.9x |
| cd | macro | 894.4 | 669.3 | 9.61s | 966.1 | 35.9 | 24.9x | 18.6x | 267x | 26.9x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 11.1 | 10.4 | 42.1 | 23.3 | 4.09 | 2.71x | 2.53x | 10.3x | 5.71x |
| fannkuch | permutation | 0.374 | 0.370 | 12.8 | 7.14 | 4.32 | 0.09x | 0.09x | 2.97x | 1.65x |
| fasta | generation | 1.79 | 1.97 | 25.7 | 8.87 | 6.28 | 0.29x | 0.31x | 4.09x | 1.41x |
| knucleotide | hashing | 4.86 | 5.23 | 149.8 | --- | 4.95 | 0.98x | 1.06x | 30.3x | --- |
| pidigits | bignum | 0.296 | 0.295 | 0.314 | 0.134 | 1.92 | 0.15x | 0.15x | 0.16x | 0.07x |
| regexredux | regex | 1.27 | 1.31 | 17.2 | --- | 2.44 | 0.52x | 0.53x | 7.02x | --- |
| revcomp | string | 1.39 | 1.50 | 46.6 | --- | 3.37 | 0.41x | 0.44x | 13.8x | --- |
| spectralnorm | numeric | 46.8 | 15.4 | 299.4 | 64.6 | 2.59 | 18.1x | 5.95x | 116x | 25.0x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 374.1 | 527.8 | 1.01s | 880.1 | 34.3 | 10.9x | 15.4x | 29.4x | 25.7x |
| matmul | numeric | 18.8 | 18.6 | 901.9 | 539.4 | 15.3 | 1.22x | 1.21x | 58.8x | 35.2x |
| primes | numeric | 59.4 | 10.4 | 102.3 | 94.9 | 4.47 | 13.3x | 2.33x | 22.9x | 21.2x |
| base64 | string | 79.6 | 67.1 | 676.2 | 158.0 | 17.4 | 4.58x | 3.86x | 38.9x | 9.10x |
| levenshtein | string | 27.8 | 22.8 | 83.6 | 54.4 | 4.03 | 6.90x | 5.67x | 20.8x | 13.5x |
| json_gen | data | 21.7 | 24.1 | 36.1 | 19.8 | 6.30 | 3.45x | 3.82x | 5.74x | 3.15x |
| collatz | numeric | 1.54s | 929.9 | 2.08s | 6.26s | 1.42s | 1.08x | 0.66x | 1.47x | 4.41x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 213.4 | 213.5 | 4.13s | 2.20s | 66.8 | 3.19x | 3.20x | 61.8x | 32.9x |
| array1 | array | 1.16 | 1.16 | 28.8 | 36.0 | 1.90 | 0.61x | 0.61x | 15.1x | 19.0x |
| deriv | symbolic | 32.7 | 16.3 | 96.3 | 59.7 | 3.78 | 8.64x | 4.31x | 25.5x | 15.8x |
| diviter | iterative | 398.5 | 396.8 | 8.86s | 26.74s | 468.1 | 0.85x | 0.85x | 18.9x | 57.1x |
| divrec | recursive | 19.3 | 2.00 | 31.1 | 36.0 | 7.73 | 2.50x | 0.26x | 4.02x | 4.66x |
| gcbench | allocation | 247.3 | 253.6 | 1.36s | 550.5 | 23.6 | 10.5x | 10.7x | 57.6x | 23.3x |
| paraffins | combinat | 2.16 | 2.05 | 2.70 | 2.52 | 0.983 | 2.20x | 2.09x | 2.75x | 2.56x |
| pnpoly | numeric | 112.9 | 62.8 | 130.5 | 201.7 | 5.80 | 19.5x | 10.8x | 22.5x | 34.8x |
| puzzle | search | 14.1 | 14.3 | 27.4 | 29.3 | 3.32 | 4.23x | 4.32x | 8.24x | 8.80x |
| quicksort | sorting | 10.3 | 5.86 | 63.9 | 19.3 | 1.63 | 6.30x | 3.60x | 39.2x | 11.8x |
| ray | numeric | 10.8 | 2.69 | 12.4 | 13.8 | 3.54 | 3.06x | 0.76x | 3.50x | 3.90x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 13.9 | 13.2 | 683.4 | --- | 17.8 | 0.78x | 0.74x | 38.4x | --- |
| navier_stokes | numeric | 1.03s | 288.7 | 710.4 | 98.5 | 14.1 | 73.0x | 20.5x | 50.3x | 6.98x |
| splay | data | 147.4 | 205.7 | 462.3 | 144.7 | 20.2 | 7.30x | 10.2x | 22.9x | 7.16x |
| hashmap | data | 153.6 | 50.7 | 3.21s | 313.1 | 15.3 | 10.1x | 3.32x | 210x | 20.5x |
| crypto_sha1 | crypto | 246.7 | 226.8 | 1.71s | 218.4 | 8.71 | 28.3x | 26.0x | 197x | 25.1x |
| raytrace3d | 3d | 306.9 | 159.3 | 1.06s | --- | 18.3 | 16.7x | 8.69x | 57.9x | --- |
