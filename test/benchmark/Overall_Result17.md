# Lambda Benchmark Results: Result17

- **Date:** 2026-07-29
- **Platform:** Darwin arm64
- **Lambda commit:** `3ca5bcefea3f02b6e194692317238dddce25d442`
- **Lambda build:** verified release binary reused after machine restart (release, non-debug)
- **Instrumentation check:** passed
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run; suites ran in order `r7rs → awfy → beng → kostya → larceny → jetstream` with a 60s initial cooldown and 45s between suites
- **Engines in this report:** MIR (untyped), MIR (typed), LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v17.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers are standardized to an explicit x8 loop over the detected benchmark function. They do not use per-file `Benchmark.runIteration()` counts, because those counts drift across JetStream files.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 9 | 10 | 14.4x | 1.06x | 5.18x | 6.22x |
| AWFY | 14 | 14 | 13 | 14 | 14 | 14 | 14.0x | 1.74x | 18.9x | 5.25x |
| BENG | 10 | 10 | 10 | 10 | 7 | 10 | 1.45x | 1.45x | 7.39x | 4.14x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 5.12x | 5.12x | 15.8x | 11.9x |
| LARCENY | 12 | 12 | 12 | 12 | 12 | 12 | 5.05x | 5.11x | 13.4x | 13.5x |
| JetStream | 9 | 9 | 8 | 9 | 7 | 9 | 7.80x | 5.98x | 55.7x | 11.9x |
| **Overall dedup** | **56** | **56** | **54** | **56** | **50** | **56** | **6.46x** | **2.30x** | **13.0x** | **7.26x** |
| Overall raw | 62 | 62 | 60 | 62 | 56 | 62 | 6.57x | 2.58x | 14.1x | 7.87x |

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
| awfy/havlak | 43.22s | 100.0 | 432x |
| jetstream/hashmap | 23.24s | 56.3 | 413x |
| awfy/cd | 7.41s | 36.2 | 204x |
| jetstream/navier_stokes | 4.70s | 37.7 | 125x |
| beng/spectralnorm | 293.4 | 2.63 | 111x |
| jetstream/crypto_sha1 | 482.1 | 6.55 | 73.6x |
| larceny/triangl | 4.28s | 66.6 | 64.2x |
| awfy/deltablue | 749.2 | 11.7 | 64.0x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.304 | 1.92 | 0.16x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 24.5 | 6.21 | 20.2 | 21.1 | 2.51 | 9.77x | 2.48x | 8.06x | 8.40x |
| fibfp | recursive | 29.0 | 7.04 | 22.0 | 20.3 | 2.07 | 14.0x | 3.40x | 10.6x | 9.81x |
| tak | recursive | 20.8 | 0.550 | 1.83 | 3.04 | 0.803 | 25.9x | 0.68x | 2.28x | 3.78x |
| cpstak | closure | 20.1 | 1.06 | 3.53 | 5.65 | 0.996 | 20.2x | 1.06x | 3.55x | 5.67x |
| sum | iterative | 22.4 | 4.04 | 11.5 | 31.0 | 1.19 | 18.8x | 3.39x | 9.69x | 26.0x |
| sumfp | iterative | 17.3 | 0.212 | 1.12 | 3.65 | 0.881 | 19.6x | 0.24x | 1.27x | 4.15x |
| nqueens | backtrack | 28.2 | 1.40 | 17.2 | 8.03 | 1.71 | 16.5x | 0.82x | 10.1x | 4.70x |
| fft | numeric | 27.1 | 0.958 | 11.3 | 2.77 | 1.61 | 16.9x | 0.60x | 7.06x | 1.72x |
| mbrot | numeric | 31.5 | 0.844 | 9.17 | 18.3 | 2.03 | 15.5x | 0.42x | 4.51x | 9.02x |
| ack | recursive | 43.3 | 23.0 | 67.6 | --- | 13.3 | 3.24x | 1.72x | 5.07x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 51.3 | 0.546 | 0.563 | 0.650 | 0.408 | 126x | 1.34x | 1.38x | 1.59x |
| permute | micro | 23.2 | 0.479 | 6.64 | 1.79 | 0.835 | 27.7x | 0.57x | 7.95x | 2.14x |
| queens | micro | 32.4 | 0.520 | 4.31 | 1.04 | 0.646 | 50.2x | 0.81x | 6.67x | 1.61x |
| towers | micro | 22.0 | 0.725 | 15.2 | 2.21 | 1.10 | 20.0x | 0.66x | 13.9x | 2.02x |
| bounce | micro | 25.0 | 0.804 | 3.24 | 0.865 | 0.558 | 44.8x | 1.44x | 5.81x | 1.55x |
| list | micro | 18.8 | --- | 3.43 | 0.906 | 0.503 | 37.5x | --- | 6.82x | 1.80x |
| storage | micro | 19.1 | 0.578 | 12.1 | 2.14 | 0.603 | 31.6x | 0.96x | 20.1x | 3.54x |
| mandelbrot | compute | 68.2 | 48.8 | 308.1 | 859.5 | 29.4 | 2.32x | 1.66x | 10.5x | 29.2x |
| nbody | compute | 43.4 | 80.2 | 262.7 | 159.1 | 5.42 | 8.00x | 14.8x | 48.4x | 29.3x |
| richards | macro | 65.5 | 136.8 | 1.17s | 190.7 | 46.7 | 1.40x | 2.93x | 25.0x | 4.08x |
| json | macro | 47.9 | 4.22 | 34.3 | 10.9 | 2.59 | 18.5x | 1.63x | 13.2x | 4.21x |
| deltablue | macro | 68.5 | 53.2 | 749.2 | 100.1 | 11.7 | 5.85x | 4.55x | 64.0x | 8.55x |
| havlak | macro | 106.9 | 45.5 | 43.22s | 3.30s | 100.0 | 1.07x | 0.45x | 432x | 33.0x |
| cd | macro | 739.6 | 360.5 | 7.41s | 958.3 | 36.2 | 20.4x | 9.95x | 204x | 26.5x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 10.7 | 10.7* | 38.6 | 26.2 | 5.61 | 1.90x | 1.90x | 6.88x | 4.66x |
| fannkuch | permutation | 0.704 | 0.704* | 13.8 | 8.65 | 4.32 | 0.16x | 0.16x | 3.19x | 2.00x |
| fasta | generation | 8.55 | 8.55* | 19.7 | 8.88 | 6.11 | 1.40x | 1.40x | 3.22x | 1.45x |
| knucleotide | hashing | 11.3 | 11.3* | 148.4 | --- | 5.05 | 2.24x | 2.24x | 29.4x | --- |
| mandelbrot | numeric | 126.5 | 126.5* | 60.3 | 679.7 | 15.0 | 8.42x | 8.42x | 4.01x | 45.2x |
| nbody | numeric | 80.2 | 80.2* | 366.5 | 148.6 | 7.37 | 10.9x | 10.9x | 49.7x | 20.2x |
| pidigits | bignum | 0.302 | 0.302* | 0.304 | 0.129 | 1.92 | 0.16x | 0.16x | 0.16x | 0.07x |
| regexredux | regex | 1.28 | 1.28* | 14.6 | --- | 2.43 | 0.53x | 0.53x | 6.02x | --- |
| revcomp | string | 1.08 | 1.08* | 36.9 | --- | 3.37 | 0.32x | 0.32x | 10.9x | --- |
| spectralnorm | numeric | 46.1 | 46.1* | 293.4 | 66.3 | 2.63 | 17.5x | 17.5x | 111x | 25.2x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 535.6 | 535.6* | 978.1 | 882.8 | 33.8 | 15.8x | 15.8x | 28.9x | 26.1x |
| matmul | numeric | 42.0 | 42.0* | 907.1 | 538.9 | 15.3 | 2.75x | 2.75x | 59.4x | 35.3x |
| primes | numeric | 55.3 | 55.3* | 99.0 | 95.3 | 4.39 | 12.6x | 12.6x | 22.6x | 21.7x |
| base64 | string | 76.3 | 76.3* | 645.7 | 157.7 | 17.4 | 4.37x | 4.37x | 37.0x | 9.04x |
| levenshtein | string | 45.0 | 45.0* | 85.5 | 54.2 | 3.94 | 11.4x | 11.4x | 21.7x | 13.7x |
| json_gen | data | 17.4 | 17.4* | 34.0 | 19.8 | 6.25 | 2.78x | 2.78x | 5.45x | 3.17x |
| collatz | numeric | 1.71s | 1.71s* | 2.09s | 6.22s | 1.42s | 1.21x | 1.21x | 1.48x | 4.39x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 386.0 | 386.0* | 4.28s | 2.23s | 66.6 | 5.80x | 5.80x | 64.2x | 33.5x |
| array1 | array | 4.32 | 4.32* | 27.7 | 37.6 | 2.12 | 2.04x | 2.04x | 13.1x | 17.8x |
| deriv | symbolic | 25.1 | 22.6 | 75.8 | 60.9 | 4.16 | 6.03x | 5.43x | 18.2x | 14.7x |
| diviter | iterative | 1.21s | 1.21s* | 8.50s | 26.69s | 471.4 | 2.57x | 2.57x | 18.0x | 56.6x |
| divrec | recursive | 20.1 | 20.1* | 25.9 | 36.3 | 7.58 | 2.65x | 2.65x | 3.42x | 4.79x |
| gcbench | allocation | 219.7 | 284.2 | 809.2 | 564.8 | 25.8 | 8.50x | 11.0x | 31.3x | 21.9x |
| paraffins | combinat | 2.28 | 2.28* | 2.70 | 2.61 | 1.06 | 2.14x | 2.14x | 2.54x | 2.45x |
| pnpoly | numeric | 113.7 | 113.7* | 131.3 | 215.2 | 6.19 | 18.4x | 18.4x | 21.2x | 34.8x |
| primes | iterative | 58.4 | 58.4* | 97.7 | 94.6 | 4.35 | 13.4x | 13.4x | 22.4x | 21.7x |
| puzzle | search | 18.3 | 18.3* | 24.5 | 29.2 | 3.35 | 5.46x | 5.46x | 7.30x | 8.70x |
| quicksort | sorting | 12.6 | 12.6* | 64.2 | 19.2 | 1.68 | 7.49x | 7.49x | 38.2x | 11.4x |
| ray | numeric | 11.0 | 11.0* | 11.7 | 13.8 | 3.62 | 3.05x | 3.05x | 3.22x | 3.82x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 89.1 | 89.1* | 256.6 | 132.4 | 5.57 | 16.0x | 16.0x | 46.0x | 23.8x |
| cube3d | 3d | 13.7 | 13.7* | 623.8 | --- | 17.6 | 0.78x | 0.78x | 35.4x | --- |
| navier_stokes | numeric | 933.2 | 933.2* | 4.70s | 787.7 | 37.7 | 24.8x | 24.8x | 125x | 20.9x |
| richards | macro | 162.2 | 91.2 | 213.9 | 25.6 | 4.99 | 32.5x | 18.3x | 42.9x | 5.13x |
| splay | data | 144.2 | 47.6 | 47.2 | 24.1 | 6.34 | 22.8x | 7.51x | 7.44x | 3.80x |
| deltablue | macro | 10.7 | 9.22 | 306.9 | 44.7 | 6.36 | 1.68x | 1.45x | 48.3x | 7.03x |
| hashmap | data | 68.9 | 54.9 | 23.24s | 2.54s | 56.3 | 1.22x | 0.98x | 413x | 45.1x |
| crypto_sha1 | crypto | 179.2 | 179.2* | 482.1 | 70.3 | 6.55 | 27.4x | 27.4x | 73.6x | 10.7x |
| raytrace3d | 3d | 151.5 | --- | 990.3 | --- | 18.2 | 8.31x | --- | 54.3x | --- |
