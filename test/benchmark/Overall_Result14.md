# Lambda Benchmark Results: Tune7 Result14

- **Date:** 2026-07-26
- **Platform:** Darwin arm64
- **Lambda commit:** `f0d36db76aa5f0a42a3011498e845d7a10328571`
- **Lambda build:** clean release build (`make release`)
- **Instrumentation check:** passed
- **Node.js:** v24.7.0
- **QuickJS:** 2026-06-04
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v14.json`

JetStream JavaScript-engine wrappers are standardized to an explicit x8 loop over the detected benchmark function. They do not use per-file `Benchmark.runIteration()` counts, because those counts drift across JetStream files.

---

## Summary

| Suite | Total | Timed MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 9 | 10 | 9 | 10 | 1.43x | 9.80x | 4.38x |
| AWFY | 14 | 13 | 14 | 14 | 14 | 2.43x | 29.1x | 3.95x |
| BENG | 10 | 9 | 10 | 7 | 10 | 2.08x | 9.23x | 2.30x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 8.17x | 18.1x | 6.50x |
| LARCENY | 12 | 12 | 12 | 12 | 12 | 7.02x | 17.4x | 7.86x |
| JetStream | 9 | 9 | 9 | 7 | 9 | 12.1x | 106x | 10.4x |
| **Overall dedup** | **56** | **53** | **56** | **50** | **56** | **3.72x** | **19.9x** | **4.93x** |
| Overall raw | 62 | 59 | 62 | 56 | 62 | 4.00x | 21.0x | 5.23x |

> **Overall dedup** is the default headline metric: duplicate benchmark names across suites are counted once, using the best timed value per engine. **Overall raw** keeps the row-weighted value for auditability.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Notable Results

- Missing timings: **9** cells
- MIR missing: r7rs/fft (exit_1), awfy/list (wrong_output), beng/pidigits (exit_1)
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- Deduplicated benchmark names: mandelbrot (awfy/beng), nbody (awfy/beng/jetstream), richards (awfy/jetstream), deltablue (awfy/jetstream), primes (kostya/larceny)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| jetstream/hashmap | 62.47s | 49.5 | 1262x |
| awfy/havlak | 91.65s | 73.4 | 1249x |
| awfy/cd | 9.31s | 32.1 | 290x |
| jetstream/navier_stokes | 5.37s | 29.0 | 185x |
| beng/spectralnorm | 276.4 | 1.93 | 143x |
| jetstream/crypto_sha1 | 503.4 | 4.24 | 119x |
| jetstream/deltablue | 361.4 | 3.83 | 94.4x |
| awfy/deltablue | 785.8 | 8.41 | 93.4x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.299 | 1.98 | 0.15x |

---

## R7RS

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 6.00 | 29.8 | 9.02 | 1.37 | 4.39x | 21.8x | 6.60x |
| fibfp | recursive | 4.73 | 32.0 | 9.18 | 1.48 | 3.18x | 21.6x | 6.18x |
| tak | recursive | 0.529 | 3.02 | 1.66 | 0.328 | 1.61x | 9.22x | 5.06x |
| cpstak | closure | 0.960 | 5.68 | 3.69 | 0.453 | 2.12x | 12.6x | 8.14x |
| sum | iterative | 4.36 | 11.9 | 10.5 | 0.972 | 4.49x | 12.2x | 10.8x |
| sumfp | iterative | 0.077 | 1.28 | 1.13 | 0.950 | 0.08x | 1.35x | 1.19x |
| nqueens | backtrack | 1.26 | 15.2 | 2.99 | 1.30 | 0.97x | 11.7x | 2.30x |
| fft | numeric | --- | 10.9 | 1.47 | 1.08 | --- | 10.0x | 1.36x |
| mbrot | numeric | 0.793 | 8.78 | 7.55 | 0.854 | 0.93x | 10.3x | 8.84x |
| ack | recursive | 22.4 | 105.8 | --- | 13.9 | 1.61x | 7.59x | --- |

## AWFY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.464 | 0.479 | 0.279 | 0.374 | 1.24x | 1.28x | 0.75x |
| permute | micro | 0.520 | 5.91 | 0.745 | 0.396 | 1.31x | 14.9x | 1.88x |
| queens | micro | 0.436 | 3.92 | 0.576 | 0.368 | 1.18x | 10.6x | 1.56x |
| towers | micro | 0.818 | 14.9 | 1.13 | 0.466 | 1.75x | 32.0x | 2.43x |
| bounce | micro | 0.829 | 3.47 | 0.421 | 0.486 | 1.71x | 7.15x | 0.87x |
| list | micro | --- | 3.39 | 0.458 | 0.214 | --- | 15.9x | 2.14x |
| storage | micro | 0.588 | 11.0 | 0.940 | 0.303 | 1.94x | 36.4x | 3.11x |
| mandelbrot | compute | 34.1 | 353.1 | 444.2 | 24.6 | 1.39x | 14.4x | 18.1x |
| nbody | compute | 79.2 | 251.3 | 71.3 | 7.41 | 10.7x | 33.9x | 9.63x |
| richards | macro | 166.5 | 1.13s | 126.0 | 41.3 | 4.03x | 27.4x | 3.05x |
| json | macro | 5.56 | 37.7 | 7.12 | 1.48 | 3.76x | 25.5x | 4.81x |
| deltablue | macro | 51.1 | 785.8 | 57.8 | 8.41 | 6.07x | 93.4x | 6.87x |
| havlak | macro | 46.0 | 91.65s | 1.90s | 73.4 | 0.63x | 1249x | 25.9x |
| cd | macro | 349.6 | 9.31s | 514.9 | 32.1 | 10.9x | 290x | 16.1x |

## BENG

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.98 | 37.7 | 10.1 | 3.36 | 2.97x | 11.2x | 3.02x |
| fannkuch | permutation | 0.574 | 11.5 | 3.32 | 2.87 | 0.20x | 3.99x | 1.15x |
| fasta | generation | 6.21 | 34.6 | 4.12 | 4.99 | 1.24x | 6.93x | 0.83x |
| knucleotide | hashing | 10.5 | 138.4 | --- | 4.59 | 2.29x | 30.1x | --- |
| mandelbrot | numeric | 116.0 | 68.5 | 262.3 | 11.4 | 10.2x | 6.00x | 23.0x |
| nbody | numeric | 81.4 | 350.7 | 72.4 | 6.51 | 12.5x | 53.9x | 11.1x |
| pidigits | bignum | --- | 0.299 | 0.058 | 1.98 | --- | 0.15x | 0.03x |
| regexredux | regex | 1.18 | 13.9 | --- | 2.15 | 0.55x | 6.45x | --- |
| revcomp | string | 1.04 | 37.1 | --- | 3.46 | 0.30x | 10.7x | --- |
| spectralnorm | numeric | 39.5 | 276.4 | 31.0 | 1.93 | 20.4x | 143x | 16.0x |

## KOSTYA

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 400.9 | 923.1 | 469.3 | 32.5 | 12.3x | 28.4x | 14.4x |
| matmul | numeric | 40.9 | 852.1 | 202.1 | 13.8 | 2.96x | 61.5x | 14.6x |
| primes | numeric | 54.8 | 109.9 | 41.8 | 4.60 | 11.9x | 23.9x | 9.08x |
| base64 | string | 277.7 | 652.3 | 70.6 | 14.8 | 18.7x | 44.0x | 4.76x |
| levenshtein | string | 43.8 | 86.1 | 28.3 | 2.73 | 16.1x | 31.6x | 10.4x |
| json_gen | data | 67.3 | 31.2 | 9.93 | 4.52 | 14.9x | 6.92x | 2.20x |
| collatz | numeric | 1.61s | 2.05s | 3.04s | 1.29s | 1.25x | 1.59x | 2.36x |

## LARCENY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 609.5 | 3.79s | 1.07s | 70.2 | 8.69x | 54.0x | 15.3x |
| array1 | array | 4.34 | 29.7 | 14.6 | 1.84 | 2.36x | 16.1x | 7.93x |
| deriv | symbolic | 22.9 | 70.4 | 22.8 | 2.01 | 11.4x | 35.0x | 11.3x |
| diviter | iterative | 2.54s | 9.76s | 10.03s | 403.1 | 6.31x | 24.2x | 24.9x |
| divrec | recursive | 19.6 | 40.7 | 26.5 | 8.33 | 2.35x | 4.88x | 3.18x |
| gcbench | allocation | 328.5 | 865.3 | 261.7 | 22.9 | 14.4x | 37.8x | 11.4x |
| paraffins | combinat | 1.93 | 2.16 | 1.17 | 0.628 | 3.08x | 3.44x | 1.86x |
| pnpoly | numeric | 95.4 | 131.9 | 94.9 | 4.59 | 20.8x | 28.8x | 20.7x |
| primes | iterative | 59.1 | 107.0 | 37.6 | 4.47 | 13.2x | 24.0x | 8.43x |
| puzzle | search | 16.5 | 24.5 | 12.7 | 2.63 | 6.26x | 9.29x | 4.83x |
| quicksort | sorting | 12.1 | 60.1 | 10.4 | 1.54 | 7.83x | 39.0x | 6.75x |
| ray | numeric | 10.9 | 10.3 | 6.69 | 1.58 | 6.89x | 6.52x | 4.23x |

## JetStream

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 78.4 | 192.3 | 70.3 | 3.91 | 20.1x | 49.2x | 18.0x |
| cube3d | 3d | 12.8 | 691.0 | --- | 15.1 | 0.85x | 45.7x | --- |
| navier_stokes | numeric | 866.8 | 5.37s | 288.6 | 29.0 | 29.9x | 185x | 9.96x |
| richards | macro | 170.6 | 212.5 | 17.7 | 2.38 | 71.6x | 89.3x | 7.44x |
| splay | data | 146.9 | 48.0 | 8.36 | 1.41 | 104x | 34.0x | 5.93x |
| deltablue | macro | 10.7 | 361.4 | 30.5 | 3.83 | 2.79x | 94.4x | 7.96x |
| hashmap | data | 60.3 | 62.47s | 1.30s | 49.5 | 1.22x | 1262x | 26.3x |
| crypto_sha1 | crypto | 160.4 | 503.4 | 34.5 | 4.24 | 37.8x | 119x | 8.14x |
| raytrace3d | 3d | 126.4 | 1.03s | --- | 11.4 | 11.1x | 90.4x | --- |
