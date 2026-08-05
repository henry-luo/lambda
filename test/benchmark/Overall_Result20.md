# Lambda Benchmark Results: Result20

- **Date:** 2026-08-05
- **Platform:** Darwin arm64
- **Lambda commit:** `6fcf2283fa6e09c8cab645d66abfc8b5d1e22989`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v20-6fcf2283fa` (20,486,504 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 197.30s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 197.2s (batched 195.2s: sync 144.0s, async 51.2s; non-batched 2.0s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run; suites run in order `r7rs -> awfy -> beng -> kostya -> larceny -> jetstream` with a 10s idle gap between suites
- **Engines in this report:** MIR (untyped), MIR (typed), LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v20.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 9 | 10 | 4.46x | 1.64x | 6.76x | 5.89x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 2.97x | 3.52x | 53.9x | 5.18x |
| BENG | 8 | 8 | 8 | 8 | 5 | 8 | 0.79x | 1.16x | 12.2x | 1.90x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 8.75x | 8.21x | 18.7x | 12.0x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 7.05x | 8.50x | 20.0x | 14.6x |
| JetStream | 6 | 6 | 6 | 6 | 4 | 6 | 15.0x | 15.8x | 150x | 12.8x |
| **Overall** | 56 | 56 | 56 | 56 | 50 | 56 | 4.26x | 4.07x | 24.2x | 7.28x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Notable Results

- Missing timings: **6** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 110.71s | 115.6 | 958x |
| awfy/cd | 27.48s | 36.5 | 753x |
| jetstream/hashmap | 7.88s | 15.4 | 511x |
| jetstream/crypto_sha1 | 3.35s | 8.79 | 381x |
| awfy/nbody | 1.91s | 5.53 | 345x |
| awfy/deltablue | 2.56s | 12.6 | 203x |
| beng/spectralnorm | 425.8 | 2.59 | 165x |
| jetstream/raytrace3d | 2.44s | 18.6 | 131x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.328 | 1.90 | 0.17x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 21.5 | 7.73 | 30.7 | 21.9 | 2.15 | 10.0x | 3.60x | 14.3x | 10.2x |
| fibfp | recursive | 18.6 | 10.2 | 32.0 | 22.5 | 2.49 | 7.48x | 4.09x | 12.9x | 9.04x |
| tak | recursive | 2.03 | 1.28 | 2.59 | 3.37 | 0.979 | 2.07x | 1.31x | 2.64x | 3.44x |
| cpstak | closure | 3.76 | 2.58 | 5.44 | 6.81 | 1.24 | 3.02x | 2.08x | 4.38x | 5.48x |
| sum | iterative | 35.2 | 2.77 | 16.0 | 37.8 | 2.01 | 17.6x | 1.38x | 7.97x | 18.9x |
| sumfp | iterative | 3.19 | 0.383 | 1.59 | 4.44 | 1.10 | 2.89x | 0.35x | 1.44x | 4.03x |
| nqueens | backtrack | 4.11 | 6.76 | 75.6 | 9.85 | 2.08 | 1.97x | 3.25x | 36.3x | 4.73x |
| fft | numeric | 3.52 | 5.95 | 14.4 | 3.12 | 1.91 | 1.84x | 3.11x | 7.51x | 1.63x |
| mbrot | numeric | 13.9 | 1.18 | 13.1 | 22.2 | 2.66 | 5.21x | 0.44x | 4.93x | 8.35x |
| ack | recursive | 95.3 | 23.1 | 84.4 | --- | 13.9 | 6.85x | 1.66x | 6.06x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.564 | 0.269 | 0.550 | 0.625 | 0.407 | 1.39x | 0.66x | 1.35x | 1.54x |
| permute | micro | 1.05 | 3.22 | 22.8 | 1.57 | 0.816 | 1.29x | 3.95x | 28.0x | 1.93x |
| queens | micro | 0.646 | 0.558 | 14.2 | 1.06 | 0.654 | 0.99x | 0.85x | 21.7x | 1.62x |
| towers | micro | 1.57 | 4.28 | 71.3 | 2.31 | 1.13 | 1.40x | 3.81x | 63.3x | 2.05x |
| bounce | micro | 0.328 | 2.55 | 12.3 | 0.889 | 0.553 | 0.59x | 4.61x | 22.2x | 1.61x |
| list | micro | 0.938 | 1.36 | 7.33 | 0.922 | 0.492 | 1.91x | 2.76x | 14.9x | 1.87x |
| storage | micro | 1.03 | 1.71 | 33.6 | 2.35 | 0.631 | 1.63x | 2.71x | 53.2x | 3.73x |
| mandelbrot | compute | 53.8 | 53.7 | 350.2 | 883.8 | 31.5 | 1.71x | 1.70x | 11.1x | 28.0x |
| nbody | compute | 169.4 | 21.7 | 1.91s | 161.8 | 5.53 | 30.6x | 3.93x | 345x | 29.3x |
| richards | macro | 2.62s | 260.3 | 4.21s | 195.7 | 47.7 | 55.0x | 5.46x | 88.3x | 4.11x |
| json | macro | 8.01 | 108.8 | 107.9 | 11.2 | 2.81 | 2.85x | 38.8x | 38.4x | 4.01x |
| deltablue | macro | 97.9 | 103.7 | 2.56s | 103.0 | 12.6 | 7.76x | 8.23x | 203x | 8.17x |
| havlak | macro | 61.4 | 62.5 | 110.71s | 3.60s | 115.6 | 0.53x | 0.54x | 958x | 31.2x |
| cd | macro | 970.8 | 876.8 | 27.48s | 969.5 | 36.5 | 26.6x | 24.0x | 753x | 26.6x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 13.5 | 376.8 | 78.1 | 23.1 | 4.05 | 3.34x | 93.1x | 19.3x | 5.72x |
| fannkuch | permutation | 0.839 | 0.844 | 15.8 | 7.23 | 4.00 | 0.21x | 0.21x | 3.94x | 1.81x |
| fasta | generation | 1.80 | 2.40 | 75.8 | 8.83 | 6.34 | 0.28x | 0.38x | 12.0x | 1.39x |
| knucleotide | hashing | 5.47 | 5.76 | 151.5 | --- | 4.95 | 1.11x | 1.16x | 30.6x | --- |
| pidigits | bignum | 0.303 | 0.298 | 0.328 | 0.133 | 1.90 | 0.16x | 0.16x | 0.17x | 0.07x |
| regexredux | regex | 1.27 | 1.28 | 40.0 | --- | 2.47 | 0.52x | 0.52x | 16.2x | --- |
| revcomp | string | 1.50 | 1.49 | 129.9 | --- | 3.38 | 0.44x | 0.44x | 38.4x | --- |
| spectralnorm | numeric | 47.1 | 26.6 | 425.8 | 63.5 | 2.59 | 18.2x | 10.3x | 165x | 24.5x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 520.5 | 377.7 | 1.07s | 885.0 | 34.3 | 15.2x | 11.0x | 31.2x | 25.8x |
| matmul | numeric | 184.2 | 1.32s | 921.7 | 543.3 | 15.3 | 12.0x | 86.5x | 60.2x | 35.5x |
| primes | numeric | 72.2 | 12.6 | 101.6 | 95.3 | 4.39 | 16.4x | 2.86x | 23.1x | 21.7x |
| base64 | string | 83.1 | 80.8 | 1.12s | 160.5 | 17.5 | 4.75x | 4.62x | 64.0x | 9.17x |
| levenshtein | string | 50.2 | 53.3 | 85.1 | 54.5 | 4.02 | 12.5x | 13.3x | 21.2x | 13.6x |
| json_gen | data | 23.9 | 25.1 | 56.5 | 19.9 | 6.12 | 3.91x | 4.10x | 9.23x | 3.25x |
| collatz | numeric | 8.05s | 5.21s | 2.10s | 6.32s | 1.42s | 5.66x | 3.66x | 1.48x | 4.44x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 284.0 | 284.0 | 4.21s | 2.21s | 66.7 | 4.26x | 4.26x | 63.1x | 33.1x |
| array1 | array | 2.54 | 11.8 | 29.0 | 35.9 | 1.92 | 1.32x | 6.15x | 15.1x | 18.7x |
| deriv | symbolic | 32.9 | 794.2 | 344.0 | 58.8 | 3.66 | 9.00x | 217x | 94.1x | 16.1x |
| diviter | iterative | 31.11s | 4.05s | 36.23s | 76.55s | 471.5 | 66.0x | 8.59x | 76.8x | 162x |
| divrec | recursive | 25.1 | 2.57 | 30.0 | 35.7 | 7.60 | 3.31x | 0.34x | 3.95x | 4.69x |
| gcbench | allocation | 305.0 | 12.20s | 2.22s | 550.2 | 23.4 | 13.0x | 521x | 94.8x | 23.5x |
| paraffins | combinat | 2.34 | 2.10 | 3.32 | 2.52 | 1.00 | 2.34x | 2.09x | 3.31x | 2.51x |
| pnpoly | numeric | 142.5 | 56.6 | 125.6 | 202.1 | 5.89 | 24.2x | 9.61x | 21.3x | 34.3x |
| puzzle | search | 25.1 | 15.0 | 27.1 | 29.4 | 3.30 | 7.60x | 4.55x | 8.23x | 8.92x |
| quicksort | sorting | 18.8 | 43.0 | 64.2 | 19.2 | 1.63 | 11.5x | 26.3x | 39.3x | 11.7x |
| ray | numeric | 10.7 | 2.86 | 12.3 | 13.8 | 3.53 | 3.03x | 0.81x | 3.49x | 3.89x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 30.6 | 29.8 | 1.37s | --- | 18.0 | 1.70x | 1.65x | 75.8x | --- |
| navier_stokes | numeric | 1.87s | 1.22s | 1.68s | 98.8 | 14.2 | 131x | 85.4x | 118x | 6.94x |
| splay | data | 182.9 | 66.8 | 961.3 | 145.6 | 19.4 | 9.44x | 3.45x | 49.6x | 7.52x |
| hashmap | data | 158.9 | 250.9 | 7.88s | 317.0 | 15.4 | 10.3x | 16.3x | 511x | 20.6x |
| crypto_sha1 | crypto | 280.0 | 292.2 | 3.35s | 219.1 | 8.79 | 31.8x | 33.2x | 381x | 24.9x |
| raytrace3d | 3d | 312.3 | 1.10s | 2.44s | --- | 18.6 | 16.8x | 59.2x | 131x | --- |
