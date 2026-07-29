# Lambda Benchmark Results: Result18

- **Date:** 2026-07-29
- **Platform:** Darwin arm64
- **Lambda commit:** `e406aa9b87ef26ea179f8933c650c76a9b0f8742`
- **Lambda build:** clean release build (`make release`)
- **Instrumentation check:** passed
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 120s per run; suites run in order `r7rs -> awfy -> beng -> kostya -> larceny -> jetstream` with a 10s idle gap between suites
- **Engines in this report:** MIR (untyped), MIR (typed), LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v18.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 9 | 10 | 1.74x | 1.17x | 5.41x | 6.10x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 2.23x | 1.67x | 18.8x | 5.16x |
| BENG | 10 | 10 | 10 | 10 | 7 | 10 | 1.19x | 0.95x | 7.54x | 4.11x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 4.78x | 4.12x | 15.8x | 11.9x |
| LARCENY | 12 | 12 | 12 | 12 | 12 | 12 | 5.11x | 2.82x | 14.0x | 13.7x |
| JetStream | 9 | 9 | 9 | 9 | 7 | 9 | 7.44x | 5.61x | 67.9x | 14.8x |
| **Overall dedup** | **56** | **56** | **56** | **56** | **50** | **56** | **2.55x** | **1.87x** | **13.6x** | **7.44x** |
| Overall raw | 62 | 62 | 62 | 62 | 56 | 62 | 2.95x | 2.10x | 14.8x | 8.04x |

> **Overall dedup** is the default headline metric: duplicate benchmark names across suites are counted once, using the best timed value per engine. **Overall raw** keeps the row-weighted value for auditability.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Notable Results

- Missing timings: **6** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- Deduplicated benchmark names: mandelbrot (awfy/beng), nbody (awfy/beng/jetstream), richards (awfy/jetstream), deltablue (awfy/jetstream), primes (kostya/larceny)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 49.34s | 117.9 | 418x |
| jetstream/crypto_sha1 | 2.49s | 10.0 | 248x |
| awfy/cd | 8.10s | 40.0 | 202x |
| jetstream/hashmap | 3.02s | 16.7 | 180x |
| jetstream/richards | 1.46s | 8.59 | 170x |
| beng/spectralnorm | 299.3 | 2.53 | 118x |
| jetstream/deltablue | 810.3 | 10.3 | 78.3x |
| larceny/triangl | 4.36s | 68.0 | 64.1x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.320 | 2.18 | 0.15x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 7.77 | 7.79 | 24.3 | 21.0 | 2.15 | 3.61x | 3.62x | 11.3x | 9.78x |
| fibfp | recursive | 8.00 | 8.21 | 23.6 | 20.7 | 2.01 | 3.98x | 4.09x | 11.7x | 10.3x |
| tak | recursive | 0.635 | 0.636 | 1.96 | 3.06 | 0.879 | 0.72x | 0.72x | 2.23x | 3.48x |
| cpstak | closure | 1.23 | 1.24 | 3.96 | 6.19 | 1.20 | 1.02x | 1.03x | 3.29x | 5.14x |
| sum | iterative | 4.39 | 4.39 | 13.1 | 33.6 | 1.37 | 3.21x | 3.21x | 9.55x | 24.6x |
| sumfp | iterative | 0.352 | 0.352 | 1.29 | 3.95 | 1.01 | 0.35x | 0.35x | 1.28x | 3.90x |
| nqueens | backtrack | 2.38 | 1.60 | 19.0 | 8.65 | 2.01 | 1.19x | 0.79x | 9.45x | 4.31x |
| fft | numeric | 2.97 | 1.02 | 12.4 | 3.00 | 1.77 | 1.68x | 0.58x | 7.00x | 1.70x |
| mbrot | numeric | 12.0 | 0.961 | 9.87 | 18.2 | 1.96 | 6.10x | 0.49x | 5.03x | 9.29x |
| ack | recursive | 24.4 | 24.5 | 76.6 | --- | 13.9 | 1.75x | 1.76x | 5.49x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.518 | 0.518 | 0.490 | 0.623 | 0.389 | 1.33x | 1.33x | 1.26x | 1.60x |
| permute | micro | 0.769 | 0.491 | 6.79 | 1.56 | 0.839 | 0.92x | 0.59x | 8.10x | 1.86x |
| queens | micro | 0.665 | 0.517 | 4.02 | 1.06 | 0.644 | 1.03x | 0.80x | 6.24x | 1.64x |
| towers | micro | 1.10 | 0.763 | 15.5 | 2.27 | 1.12 | 0.98x | 0.68x | 13.8x | 2.02x |
| bounce | micro | 0.312 | 0.820 | 3.34 | 0.888 | 0.557 | 0.56x | 1.47x | 6.01x | 1.60x |
| list | micro | 0.537 | 0.476 | 3.59 | 0.939 | 0.498 | 1.08x | 0.96x | 7.21x | 1.89x |
| storage | micro | 0.799 | 0.590 | 12.3 | 2.22 | 0.637 | 1.25x | 0.93x | 19.3x | 3.48x |
| mandelbrot | compute | 53.7 | 53.5 | 333.6 | 892.0 | 31.6 | 1.70x | 1.69x | 10.6x | 28.2x |
| nbody | compute | 168.9 | 82.1 | 271.5 | 162.4 | 5.45 | 31.0x | 15.1x | 49.8x | 29.8x |
| richards | macro | 1.45s | 144.7 | 1.20s | 193.8 | 48.3 | 30.1x | 3.00x | 24.9x | 4.02x |
| json | macro | 5.23 | 4.72 | 35.5 | 11.2 | 2.68 | 1.95x | 1.76x | 13.3x | 4.19x |
| deltablue | macro | 56.5 | 57.0 | 763.6 | 101.6 | 12.3 | 4.60x | 4.64x | 62.1x | 8.26x |
| havlak | macro | 40.4 | 40.4 | 49.34s | 3.62s | 117.9 | 0.34x | 0.34x | 418x | 30.7x |
| cd | macro | 679.4 | 426.0 | 8.10s | 1.03s | 40.0 | 17.0x | 10.6x | 202x | 25.7x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.67 | 6.40 | 35.6 | 23.9 | 4.13 | 2.34x | 1.55x | 8.62x | 5.78x |
| fannkuch | permutation | 0.648 | 0.650 | 12.9 | 7.30 | 4.28 | 0.15x | 0.15x | 3.02x | 1.70x |
| fasta | generation | 1.67 | 1.46 | 19.7 | 8.79 | 6.06 | 0.28x | 0.24x | 3.25x | 1.45x |
| knucleotide | hashing | 5.33 | 4.71 | 152.2 | --- | 5.00 | 1.07x | 0.94x | 30.4x | --- |
| mandelbrot | numeric | 137.6 | 139.1 | 61.4 | 693.1 | 15.4 | 8.95x | 9.05x | 4.00x | 45.1x |
| nbody | numeric | 81.9 | 42.9 | 375.6 | 150.8 | 7.57 | 10.8x | 5.67x | 49.6x | 19.9x |
| pidigits | bignum | 0.294 | 0.293 | 0.320 | 0.131 | 2.18 | 0.13x | 0.13x | 0.15x | 0.06x |
| regexredux | regex | 1.30 | 1.30 | 15.1 | --- | 2.49 | 0.52x | 0.52x | 6.05x | --- |
| revcomp | string | 1.42 | 1.44 | 38.0 | --- | 3.42 | 0.42x | 0.42x | 11.1x | --- |
| spectralnorm | numeric | 47.6 | 18.5 | 299.3 | 64.7 | 2.53 | 18.8x | 7.32x | 118x | 25.6x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 324.6 | 315.7 | 1.00s | 896.6 | 34.1 | 9.51x | 9.25x | 29.4x | 26.3x |
| matmul | numeric | 43.2 | 42.3 | 950.9 | 549.0 | 15.7 | 2.74x | 2.69x | 60.4x | 34.9x |
| primes | numeric | 57.6 | 57.0 | 102.8 | 96.4 | 4.59 | 12.5x | 12.4x | 22.4x | 21.0x |
| base64 | string | 77.3 | 51.3 | 658.2 | 161.5 | 17.7 | 4.38x | 2.91x | 37.3x | 9.15x |
| levenshtein | string | 47.7 | 34.4 | 85.0 | 55.1 | 4.06 | 11.7x | 8.47x | 20.9x | 13.6x |
| json_gen | data | 18.0 | 17.7 | 34.6 | 20.3 | 6.31 | 2.84x | 2.81x | 5.48x | 3.21x |
| collatz | numeric | 1.72s | 1.35s | 2.13s | 6.41s | 1.44s | 1.20x | 0.94x | 1.48x | 4.45x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 345.8 | 342.0 | 4.36s | 2.22s | 68.0 | 5.09x | 5.03x | 64.1x | 32.7x |
| array1 | array | 4.36 | 4.34 | 29.7 | 36.6 | 1.94 | 2.24x | 2.23x | 15.3x | 18.8x |
| deriv | symbolic | 26.1 | 23.5 | 77.8 | 60.3 | 3.72 | 7.01x | 6.31x | 20.9x | 16.2x |
| diviter | iterative | 1.22s | 1.22s | 8.84s | 27.17s | 478.9 | 2.55x | 2.54x | 18.4x | 56.7x |
| divrec | recursive | 20.8 | 3.67 | 28.0 | 36.1 | 7.79 | 2.67x | 0.47x | 3.60x | 4.64x |
| gcbench | allocation | 228.0 | 297.7 | 849.2 | 575.3 | 24.0 | 9.48x | 12.4x | 35.3x | 23.9x |
| paraffins | combinat | 2.17 | 1.85 | 2.59 | 2.54 | 1.01 | 2.15x | 1.84x | 2.57x | 2.53x |
| pnpoly | numeric | 110.0 | 26.7 | 126.1 | 205.2 | 6.12 | 18.0x | 4.36x | 20.6x | 33.5x |
| primes | iterative | 57.0 | 56.7 | 103.8 | 99.6 | 4.70 | 12.1x | 12.1x | 22.1x | 21.2x |
| puzzle | search | 19.9 | 15.0 | 27.5 | 32.0 | 3.68 | 5.39x | 4.08x | 7.46x | 8.68x |
| quicksort | sorting | 13.9 | 2.15 | 72.8 | 21.1 | 1.81 | 7.67x | 1.19x | 40.3x | 11.7x |
| ray | numeric | 12.4 | 1.97 | 12.9 | 14.8 | 3.90 | 3.19x | 0.51x | 3.32x | 3.80x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 80.5 | 42.8 | 239.7 | 132.8 | 5.51 | 14.6x | 7.76x | 43.5x | 24.1x |
| cube3d | 3d | 14.0 | 12.8 | 626.2 | --- | 17.8 | 0.79x | 0.72x | 35.2x | --- |
| navier_stokes | numeric | 952.5 | 957.7 | 605.7 | 99.9 | 14.2 | 66.9x | 67.3x | 42.5x | 7.02x |
| richards | macro | 169.2 | 96.5 | 1.46s | 162.4 | 8.59 | 19.7x | 11.2x | 170x | 18.9x |
| splay | data | 151.0 | 122.8 | 304.5 | 147.3 | 20.4 | 7.41x | 6.03x | 14.9x | 7.23x |
| deltablue | macro | 11.1 | 9.73 | 810.3 | 114.2 | 10.3 | 1.07x | 0.94x | 78.3x | 11.0x |
| hashmap | data | 76.1 | 61.6 | 3.02s | 341.4 | 16.7 | 4.55x | 3.69x | 180x | 20.4x |
| crypto_sha1 | crypto | 148.5 | 147.3 | 2.49s | 299.6 | 10.0 | 14.8x | 14.7x | 248x | 29.8x |
| raytrace3d | 3d | 177.5 | 88.3 | 1.10s | --- | 20.8 | 8.55x | 4.25x | 52.8x | --- |
