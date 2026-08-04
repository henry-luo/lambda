# Lambda Benchmark Results: Result19

- **Date:** 2026-08-05
- **Platform:** Darwin arm64
- **Lambda commit:** `ffcd4b0f0e950f99ed5b6cef217a01b464d7b6cf`
- **Lambda build:** clean release build (`make release`)
- **Instrumentation check:** not_recorded
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR (untyped), MIR (typed), LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v19.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 9 | 10 | 3.34x | 1.21x | 4.99x | 5.01x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 3.68x | 3.36x | 54.0x | 5.20x |
| BENG | 8 | 8 | 8 | 8 | 5 | 8 | 0.88x | 0.95x | 11.9x | 1.86x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 8.70x | 8.06x | 18.6x | 12.0x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 8.73x | 4.53x | 16.9x | 13.3x |
| JetStream | 6 | 6 | 6 | 6 | 4 | 6 | 14.0x | 12.7x | 156x | 12.9x |
| **Overall** | 56 | 56 | 56 | 56 | 50 | 56 | 4.49x | 3.19x | 22.2x | 6.92x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Notable Results

- Missing timings: **6** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 106.83s | 95.7 | 1117x |
| awfy/cd | 27.56s | 35.5 | 777x |
| jetstream/hashmap | 8.38s | 15.3 | 547x |
| jetstream/crypto_sha1 | 3.33s | 8.67 | 384x |
| awfy/deltablue | 3.06s | 12.6 | 243x |
| jetstream/raytrace3d | 3.12s | 18.1 | 172x |
| awfy/nbody | 1.02s | 6.00 | 171x |
| beng/spectralnorm | 423.5 | 2.59 | 163x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.343 | 2.02 | 0.17x |
| r7rs/sumfp | 1.35 | 2.36 | 0.57x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 22.7 | 7.79 | 29.3 | 21.4 | 2.29 | 9.92x | 3.41x | 12.8x | 9.36x |
| fibfp | recursive | 18.5 | 9.78 | 30.0 | 20.7 | 2.28 | 8.10x | 4.29x | 13.1x | 9.09x |
| tak | recursive | 1.73 | 1.31 | 2.33 | 3.07 | 0.908 | 1.91x | 1.44x | 2.57x | 3.38x |
| cpstak | closure | 3.52 | 2.83 | 4.85 | 7.84 | 1.42 | 2.49x | 1.99x | 3.42x | 5.53x |
| sum | iterative | 37.0 | 2.64 | 15.0 | 37.9 | 1.81 | 20.4x | 1.46x | 8.30x | 21.0x |
| sumfp | iterative | 2.77 | 0.954 | 1.35 | 5.41 | 2.36 | 1.17x | 0.40x | 0.57x | 2.29x |
| nqueens | backtrack | 3.74 | 6.33 | 82.4 | 11.4 | 3.62 | 1.03x | 1.75x | 22.8x | 3.15x |
| fft | numeric | 3.40 | 1.99 | 13.1 | 5.72 | 3.76 | 0.91x | 0.53x | 3.49x | 1.52x |
| mbrot | numeric | 12.7 | 0.845 | 11.0 | 22.1 | 4.08 | 3.10x | 0.21x | 2.70x | 5.42x |
| ack | recursive | 102.2 | 22.7 | 99.3 | --- | 15.8 | 6.47x | 1.44x | 6.29x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.627 | 0.305 | 0.597 | 1.00 | 0.481 | 1.30x | 0.63x | 1.24x | 2.08x |
| permute | micro | 1.13 | 3.52 | 24.3 | 1.59 | 0.910 | 1.24x | 3.87x | 26.7x | 1.75x |
| queens | micro | 0.664 | 0.562 | 14.3 | 1.08 | 0.679 | 0.98x | 0.83x | 21.0x | 1.59x |
| towers | micro | 1.64 | 4.17 | 70.8 | 2.34 | 1.16 | 1.42x | 3.59x | 61.1x | 2.01x |
| bounce | micro | 0.713 | 2.87 | 12.2 | 0.918 | 0.688 | 1.04x | 4.17x | 17.8x | 1.33x |
| list | micro | 0.901 | 1.32 | 16.6 | 1.09 | 0.537 | 1.68x | 2.46x | 31.0x | 2.03x |
| storage | micro | 1.03 | 1.89 | 34.5 | 2.26 | 0.658 | 1.57x | 2.88x | 52.4x | 3.43x |
| mandelbrot | compute | 59.1 | 58.9 | 350.6 | 930.6 | 32.8 | 1.80x | 1.80x | 10.7x | 28.4x |
| nbody | compute | 181.2 | 16.0 | 1.02s | 168.7 | 6.00 | 30.2x | 2.67x | 171x | 28.1x |
| richards | macro | 2.54s | 250.4 | 4.41s | 190.8 | 46.9 | 54.1x | 5.34x | 94.1x | 4.07x |
| json | macro | 107.3 | 104.2 | 110.1 | 11.2 | 2.76 | 38.8x | 37.7x | 39.8x | 4.05x |
| deltablue | macro | 94.1 | 97.2 | 3.06s | 102.0 | 12.6 | 7.49x | 7.73x | 243x | 8.11x |
| havlak | macro | 60.9 | 58.9 | 106.83s | 3.30s | 95.7 | 0.64x | 0.62x | 1117x | 34.5x |
| cd | macro | 893.2 | 842.5 | 27.56s | 954.0 | 35.5 | 25.2x | 23.7x | 777x | 26.9x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 12.2 | 44.0 | 76.7 | 23.6 | 4.26 | 2.87x | 10.3x | 18.0x | 5.55x |
| fannkuch | permutation | 3.00 | 2.44 | 15.8 | 7.22 | 4.22 | 0.71x | 0.58x | 3.74x | 1.71x |
| fasta | generation | 1.80 | 2.26 | 74.9 | 8.81 | 6.18 | 0.29x | 0.37x | 12.1x | 1.43x |
| knucleotide | hashing | 5.38 | 5.87 | 151.2 | --- | 5.21 | 1.03x | 1.13x | 29.0x | --- |
| pidigits | bignum | 0.308 | 0.309 | 0.343 | 0.136 | 2.02 | 0.15x | 0.15x | 0.17x | 0.07x |
| regexredux | regex | 1.29 | 1.29 | 40.4 | --- | 2.52 | 0.51x | 0.51x | 16.0x | --- |
| revcomp | string | 1.54 | 1.54 | 134.0 | --- | 3.61 | 0.43x | 0.43x | 37.1x | --- |
| spectralnorm | numeric | 47.3 | 20.4 | 423.5 | 63.6 | 2.59 | 18.2x | 7.85x | 163x | 24.5x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 549.1 | 416.6 | 1.05s | 881.9 | 33.5 | 16.4x | 12.4x | 31.2x | 26.3x |
| matmul | numeric | 178.4 | 1.29s | 898.5 | 536.8 | 15.3 | 11.7x | 84.5x | 58.7x | 35.1x |
| primes | numeric | 69.3 | 11.6 | 99.8 | 94.1 | 4.38 | 15.8x | 2.64x | 22.8x | 21.5x |
| base64 | string | 80.3 | 67.8 | 1.09s | 155.8 | 17.1 | 4.70x | 3.96x | 63.9x | 9.11x |
| levenshtein | string | 49.6 | 52.8 | 83.9 | 54.3 | 3.91 | 12.7x | 13.5x | 21.5x | 13.9x |
| json_gen | data | 23.1 | 25.4 | 55.5 | 19.9 | 6.16 | 3.75x | 4.12x | 9.00x | 3.22x |
| collatz | numeric | 7.86s | 5.08s | 2.08s | 6.20s | 1.41s | 5.56x | 3.59x | 1.47x | 4.38x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 1.28s | 414.9 | 4.11s | 2.16s | 66.2 | 19.4x | 6.27x | 62.2x | 32.7x |
| array1 | array | 10.6 | 20.1 | 27.9 | 35.8 | 1.89 | 5.62x | 10.6x | 14.7x | 18.9x |
| deriv | symbolic | 32.2 | 130.9 | 344.6 | 59.9 | 3.62 | 8.88x | 36.1x | 95.1x | 16.5x |
| diviter | iterative | 19.78s | 1.24s | 8.39s | 26.70s | 468.7 | 42.2x | 2.66x | 17.9x | 57.0x |
| divrec | recursive | 24.4 | 2.58 | 29.2 | 36.2 | 7.69 | 3.17x | 0.33x | 3.79x | 4.71x |
| gcbench | allocation | 287.8 | 1.08s | 1.78s | 548.7 | 23.4 | 12.3x | 46.1x | 76.0x | 23.4x |
| paraffins | combinat | 2.31 | 1.71 | 3.28 | 2.53 | 1.00 | 2.30x | 1.70x | 3.27x | 2.52x |
| pnpoly | numeric | 142.9 | 20.7 | 125.7 | 203.1 | 5.95 | 24.0x | 3.48x | 21.1x | 34.1x |
| puzzle | search | 25.0 | 14.4 | 25.6 | 29.3 | 3.28 | 7.62x | 4.38x | 7.80x | 8.93x |
| quicksort | sorting | 18.7 | 23.8 | 65.8 | 19.3 | 1.65 | 11.4x | 14.5x | 39.9x | 11.7x |
| ray | numeric | 10.6 | 1.60 | 11.8 | 13.9 | 3.58 | 2.95x | 0.45x | 3.29x | 3.87x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 32.0 | 30.8 | 1.41s | --- | 19.0 | 1.69x | 1.63x | 74.5x | --- |
| navier_stokes | numeric | 1.40s | 226.3 | 1.68s | 98.6 | 14.2 | 98.8x | 16.0x | 119x | 6.96x |
| splay | data | 175.4 | 196.3 | 861.2 | 146.9 | 19.2 | 9.13x | 10.2x | 44.8x | 7.65x |
| hashmap | data | 150.5 | 239.7 | 8.38s | 316.1 | 15.3 | 9.82x | 15.6x | 547x | 20.6x |
| crypto_sha1 | crypto | 281.0 | 285.7 | 3.33s | 218.5 | 8.67 | 32.4x | 33.0x | 384x | 25.2x |
| raytrace3d | 3d | 285.9 | 546.5 | 3.12s | --- | 18.1 | 15.8x | 30.1x | 172x | --- |
