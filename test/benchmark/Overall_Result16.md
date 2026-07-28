# Lambda Benchmark Results: Result16

- **Date:** 2026-07-28
- **Platform:** Darwin arm64
- **Lambda commit:** `634ca167698351dcce41d1ac3c965744653f448e`
- **Lambda build:** clean release build (`make release`)
- **Instrumentation check:** passed
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR (untyped), MIR (typed), LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v16.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers are standardized to an explicit x8 loop over the detected benchmark function. They do not use per-file `Benchmark.runIteration()` counts, because those counts drift across JetStream files.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 9 | 10 | 13.4x | 1.06x | 5.23x | 6.21x |
| AWFY | 14 | 14 | 13 | 14 | 14 | 14 | 11.9x | 1.73x | 18.6x | 5.23x |
| BENG | 10 | 10 | 10 | 10 | 7 | 10 | 1.43x | 1.43x | 7.57x | 4.21x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 4.96x | 4.96x | 15.7x | 11.9x |
| LARCENY | 12 | 12 | 12 | 12 | 12 | 12 | 5.07x | 5.13x | 13.6x | 13.8x |
| JetStream | 9 | 9 | 8 | 9 | 7 | 9 | 8.09x | 6.26x | 57.8x | 12.6x |
| **Overall dedup** | **56** | **56** | **54** | **56** | **50** | **56** | **6.16x** | **2.31x** | **13.2x** | **7.38x** |
| Overall raw | 62 | 62 | 60 | 62 | 56 | 62 | 6.26x | 2.58x | 14.3x | 7.96x |

> **Overall dedup** is the default headline metric: duplicate benchmark names across suites are counted once, using the best timed value per engine. **Overall raw** keeps the row-weighted value for auditability.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Notable Results

- Missing timings: **8** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- MIR (typed) missing: awfy/list (wrong_output), jetstream/raytrace3d (timeout)
- Deduplicated benchmark names: mandelbrot (awfy/beng), nbody (awfy/beng/jetstream), richards (awfy/jetstream), deltablue (awfy/jetstream), primes (kostya/larceny)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 43.30s | 98.2 | 441x |
| jetstream/hashmap | 23.21s | 55.9 | 415x |
| awfy/cd | 7.47s | 36.1 | 207x |
| jetstream/navier_stokes | 4.70s | 37.5 | 125x |
| beng/spectralnorm | 292.4 | 2.57 | 114x |
| jetstream/crypto_sha1 | 478.7 | 6.98 | 68.6x |
| awfy/deltablue | 760.8 | 12.2 | 62.6x |
| larceny/triangl | 4.18s | 67.1 | 62.4x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.318 | 1.94 | 0.16x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 23.4 | 6.61 | 20.1 | 18.7 | 1.85 | 12.6x | 3.57x | 10.8x | 10.1x |
| fibfp | recursive | 21.9 | 5.83 | 20.6 | 18.9 | 1.83 | 12.0x | 3.19x | 11.3x | 10.4x |
| tak | recursive | 17.8 | 0.536 | 1.76 | 2.83 | 0.816 | 21.9x | 0.66x | 2.16x | 3.46x |
| cpstak | closure | 18.0 | 1.07 | 3.55 | 5.60 | 0.990 | 18.2x | 1.09x | 3.59x | 5.66x |
| sum | iterative | 20.6 | 4.04 | 11.3 | 31.6 | 1.21 | 17.0x | 3.33x | 9.33x | 26.0x |
| sumfp | iterative | 16.7 | 0.220 | 1.17 | 3.78 | 0.883 | 18.9x | 0.25x | 1.32x | 4.28x |
| nqueens | backtrack | 26.5 | 1.40 | 17.6 | 8.02 | 1.77 | 15.0x | 0.79x | 9.93x | 4.53x |
| fft | numeric | 26.6 | 0.944 | 11.4 | 2.88 | 2.04 | 13.1x | 0.46x | 5.62x | 1.41x |
| mbrot | numeric | 31.5 | 0.855 | 8.98 | 18.2 | 1.96 | 16.1x | 0.44x | 4.59x | 9.32x |
| ack | recursive | 42.1 | 23.1 | 69.7 | --- | 13.7 | 3.08x | 1.69x | 5.10x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 17.2 | 0.518 | 0.484 | 0.625 | 0.395 | 43.5x | 1.31x | 1.23x | 1.58x |
| permute | micro | 17.4 | 0.475 | 6.43 | 1.56 | 0.824 | 21.1x | 0.58x | 7.81x | 1.89x |
| queens | micro | 19.3 | 0.515 | 4.05 | 1.08 | 0.642 | 30.1x | 0.80x | 6.31x | 1.68x |
| towers | micro | 19.9 | 0.737 | 15.5 | 2.29 | 1.11 | 17.9x | 0.66x | 14.0x | 2.06x |
| bounce | micro | 23.8 | 0.814 | 3.33 | 0.903 | 0.553 | 43.1x | 1.47x | 6.03x | 1.63x |
| list | micro | 18.7 | --- | 3.48 | 0.934 | 0.504 | 37.1x | --- | 6.90x | 1.85x |
| storage | micro | 18.1 | 0.582 | 12.4 | 2.25 | 0.643 | 28.1x | 0.90x | 19.2x | 3.51x |
| mandelbrot | compute | 68.2 | 49.9 | 314.0 | 885.3 | 31.8 | 2.15x | 1.57x | 9.88x | 27.9x |
| nbody | compute | 42.5 | 82.0 | 267.4 | 162.8 | 5.73 | 7.42x | 14.3x | 46.6x | 28.4x |
| richards | macro | 64.9 | 140.4 | 1.20s | 194.5 | 47.3 | 1.37x | 2.97x | 25.3x | 4.11x |
| json | macro | 47.1 | 4.31 | 35.6 | 11.2 | 2.65 | 17.8x | 1.62x | 13.4x | 4.24x |
| deltablue | macro | 71.0 | 54.2 | 760.8 | 102.6 | 12.2 | 5.84x | 4.46x | 62.6x | 8.44x |
| havlak | macro | 107.2 | 46.7 | 43.30s | 3.30s | 98.2 | 1.09x | 0.48x | 441x | 33.6x |
| cd | macro | 742.4 | 363.8 | 7.47s | 964.7 | 36.1 | 20.5x | 10.1x | 207x | 26.7x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.25 | 9.25* | 33.8 | 23.5 | 4.04 | 2.29x | 2.29x | 8.39x | 5.83x |
| fannkuch | permutation | 0.633 | 0.633* | 12.8 | 7.29 | 3.99 | 0.16x | 0.16x | 3.22x | 1.82x |
| fasta | generation | 6.71 | 6.71* | 19.5 | 8.73 | 6.10 | 1.10x | 1.10x | 3.19x | 1.43x |
| knucleotide | hashing | 11.4 | 11.4* | 149.2 | --- | 5.18 | 2.19x | 2.19x | 28.8x | --- |
| mandelbrot | numeric | 126.0 | 126.0* | 60.3 | 687.9 | 15.1 | 8.36x | 8.36x | 4.00x | 45.6x |
| nbody | numeric | 80.1 | 80.1* | 369.7 | 149.5 | 7.39 | 10.8x | 10.8x | 50.0x | 20.2x |
| pidigits | bignum | 0.296 | 0.296* | 0.318 | 0.129 | 1.94 | 0.15x | 0.15x | 0.16x | 0.07x |
| regexredux | regex | 1.28 | 1.28* | 14.8 | --- | 2.44 | 0.52x | 0.52x | 6.06x | --- |
| revcomp | string | 1.08 | 1.08* | 37.2 | --- | 3.38 | 0.32x | 0.32x | 11.0x | --- |
| spectralnorm | numeric | 45.6 | 45.6* | 292.4 | 64.2 | 2.57 | 17.7x | 17.7x | 114x | 25.0x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 449.5 | 449.5* | 975.8 | 886.1 | 33.8 | 13.3x | 13.3x | 28.9x | 26.2x |
| matmul | numeric | 41.4 | 41.4* | 893.9 | 541.5 | 15.5 | 2.66x | 2.66x | 57.5x | 34.8x |
| primes | numeric | 55.8 | 55.8* | 98.4 | 95.3 | 4.54 | 12.3x | 12.3x | 21.7x | 21.0x |
| base64 | string | 76.3 | 76.3* | 650.4 | 158.4 | 17.4 | 4.39x | 4.39x | 37.4x | 9.12x |
| levenshtein | string | 45.0 | 45.0* | 82.4 | 54.2 | 3.96 | 11.4x | 11.4x | 20.8x | 13.7x |
| json_gen | data | 17.1 | 17.1* | 34.5 | 20.2 | 6.15 | 2.78x | 2.78x | 5.62x | 3.29x |
| collatz | numeric | 1.72s | 1.72s* | 2.11s | 6.24s | 1.42s | 1.21x | 1.21x | 1.49x | 4.40x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 335.3 | 335.3* | 4.18s | 2.21s | 67.1 | 5.00x | 5.00x | 62.4x | 33.0x |
| array1 | array | 4.29 | 4.29* | 27.6 | 36.1 | 1.90 | 2.25x | 2.25x | 14.5x | 19.0x |
| deriv | symbolic | 25.3 | 22.5 | 74.6 | 59.2 | 3.67 | 6.88x | 6.12x | 20.3x | 16.1x |
| diviter | iterative | 1.20s | 1.20s* | 8.30s | 26.71s | 467.7 | 2.57x | 2.57x | 17.8x | 57.1x |
| divrec | recursive | 20.0 | 20.0* | 26.2 | 37.0 | 7.72 | 2.59x | 2.59x | 3.39x | 4.80x |
| gcbench | allocation | 220.3 | 285.0 | 799.6 | 554.0 | 23.4 | 9.41x | 12.2x | 34.2x | 23.7x |
| paraffins | combinat | 2.09 | 2.09* | 2.56 | 2.59 | 1.00 | 2.08x | 2.08x | 2.55x | 2.58x |
| pnpoly | numeric | 109.7 | 109.7* | 123.2 | 201.8 | 6.19 | 17.7x | 17.7x | 19.9x | 32.6x |
| primes | iterative | 55.4 | 55.4* | 98.2 | 94.8 | 4.51 | 12.3x | 12.3x | 21.8x | 21.0x |
| puzzle | search | 18.2 | 18.2* | 24.5 | 29.6 | 3.35 | 5.44x | 5.44x | 7.32x | 8.83x |
| quicksort | sorting | 12.5 | 12.5* | 64.2 | 19.2 | 1.65 | 7.59x | 7.59x | 39.0x | 11.7x |
| ray | numeric | 11.3 | 11.3* | 11.5 | 13.8 | 3.56 | 3.16x | 3.16x | 3.24x | 3.88x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 80.8 | 80.8* | 234.5 | 131.3 | 5.76 | 14.0x | 14.0x | 40.7x | 22.8x |
| cube3d | 3d | 13.8 | 13.8* | 627.6 | --- | 18.0 | 0.77x | 0.77x | 34.9x | --- |
| navier_stokes | numeric | 935.7 | 935.7* | 4.70s | 787.4 | 37.5 | 25.0x | 25.0x | 125x | 21.0x |
| richards | macro | 163.4 | 90.7 | 216.4 | 25.8 | 4.93 | 33.2x | 18.4x | 43.9x | 5.24x |
| splay | data | 143.1 | 48.5 | 47.0 | 24.0 | 3.66 | 39.1x | 13.3x | 12.8x | 6.57x |
| deltablue | macro | 10.7 | 9.22 | 307.9 | 45.0 | 6.52 | 1.64x | 1.41x | 47.2x | 6.91x |
| hashmap | data | 68.9 | 54.9 | 23.21s | 2.46s | 55.9 | 1.23x | 0.98x | 415x | 43.9x |
| crypto_sha1 | crypto | 181.1 | 181.1* | 478.7 | 69.8 | 6.98 | 26.0x | 26.0x | 68.6x | 10.0x |
| raytrace3d | 3d | 149.5 | --- | 974.8 | --- | 18.4 | 8.14x | --- | 53.1x | --- |
