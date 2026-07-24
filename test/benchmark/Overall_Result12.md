# Round 12 (Tune4 + Tune5)

- **Date:** 2026-07-24
- **Platform:** Darwin arm64
- **Lambda commit:** `8eaaadca27b5e449845029ebf738bbdc8b26b99c`
- **Lambda build:** clean release build (`make release`)
- **Instrumentation check:** not_recorded
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v12.json`

JetStream JavaScript-engine wrappers are standardized to an explicit x8 loop over the detected benchmark function. They do not use per-file `Benchmark.runIteration()` counts, because those counts drift across JetStream files.

---

## Summary

| Suite | Total | Timed MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 9 | 10 | 9 | 10 | 0.74x | 6.44x | 4.66x |
| AWFY | 14 | 13 | 14 | 14 | 14 | 2.06x | 17.6x | 4.71x |
| BENG | 10 | 9 | 10 | 7 | 10 | 1.87x | 8.11x | 3.93x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7.02x | 17.0x | 12.1x |
| LARCENY | 12 | 12 | 12 | 12 | 12 | 6.14x | 15.4x | 13.7x |
| JetStream | 9 | 9 | 9 | 7 | 9 | 11.6x | 65.3x | 12.4x |
| **Overall dedup** | **56** | **53** | **56** | **50** | **56** | **3.01x** | **15.1x** | **7.00x** |
| Overall raw | 62 | 59 | 62 | 56 | 62 | 3.27x | 15.5x | 7.33x |

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
| jetstream/hashmap | 64.39s | 56.6 | 1138x |
| awfy/havlak | 92.49s | 159.7 | 579x |
| awfy/cd | 10.86s | 42.7 | 254x |
| jetstream/navier_stokes | 5.93s | 41.7 | 142x |
| beng/spectralnorm | 302.9 | 2.76 | 110x |
| jetstream/crypto_sha1 | 512.6 | 7.07 | 72.5x |
| larceny/triangl | 4.12s | 69.1 | 59.6x |
| kostya/matmul | 920.2 | 16.0 | 57.7x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.324 | 2.04 | 0.16x |

---

## R7RS

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 8.37 | 50.1 | 21.9 | 4.65 | 1.80x | 10.8x | 4.72x |
| fibfp | recursive | 7.14 | 66.0 | 23.8 | 3.20 | 2.23x | 20.6x | 7.45x |
| tak | recursive | 0.546 | 5.69 | 2.90 | 0.837 | 0.65x | 6.80x | 3.47x |
| cpstak | closure | 1.09 | 11.4 | 6.54 | 1.32 | 0.82x | 8.62x | 4.96x |
| sum | iterative | 8.70 | 16.9 | 43.4 | 4.69 | 1.85x | 3.61x | 9.25x |
| sumfp | iterative | 0.069 | 2.32 | 5.67 | 1.23 | 0.06x | 1.88x | 4.60x |
| nqueens | backtrack | 2.77 | 27.5 | 13.6 | 4.40 | 0.63x | 6.26x | 3.08x |
| fft | numeric | --- | 17.0 | 3.95 | 2.03 | --- | 8.39x | 1.94x |
| mbrot | numeric | 1.38 | 14.3 | 30.4 | 4.50 | 0.31x | 3.17x | 6.75x |
| ack | recursive | 36.4 | 190.9 | --- | 22.8 | 1.60x | 8.38x | --- |

## AWFY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.550 | 0.934 | 0.961 | 0.449 | 1.22x | 2.08x | 2.14x |
| permute | micro | 0.690 | 9.67 | 2.56 | 1.68 | 0.41x | 5.74x | 1.52x |
| queens | micro | 0.805 | 5.39 | 1.77 | 0.709 | 1.14x | 7.60x | 2.50x |
| towers | micro | 1.82 | 23.3 | 4.28 | 1.26 | 1.45x | 18.5x | 3.40x |
| bounce | micro | 1.16 | 6.85 | 0.967 | 0.662 | 1.75x | 10.3x | 1.46x |
| list | micro | --- | 4.39 | 1.03 | 0.560 | --- | 7.83x | 1.84x |
| storage | micro | 0.772 | 22.1 | 2.46 | 0.752 | 1.03x | 29.5x | 3.28x |
| mandelbrot | compute | 65.1 | 530.7 | 1.28s | 52.8 | 1.23x | 10.0x | 24.3x |
| nbody | compute | 134.6 | 437.7 | 246.6 | 25.9 | 5.19x | 16.9x | 9.51x |
| richards | macro | 515.8 | 1.28s | 214.0 | 64.4 | 8.01x | 19.9x | 3.32x |
| json | macro | 15.9 | 53.2 | 17.5 | 3.90 | 4.08x | 13.6x | 4.48x |
| deltablue | macro | 164.6 | 352.1 | 161.2 | 33.9 | 4.86x | 10.4x | 4.76x |
| havlak | macro | 95.8 | 92.49s | 3.98s | 159.7 | 0.60x | 579x | 24.9x |
| cd | macro | 568.9 | 10.86s | 1.12s | 42.7 | 13.3x | 254x | 26.2x |

## BENG

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 13.7 | 46.8 | 26.4 | 5.37 | 2.55x | 8.73x | 4.92x |
| fannkuch | permutation | 0.735 | 15.8 | 12.7 | 6.87 | 0.11x | 2.30x | 1.85x |
| fasta | generation | 8.68 | 50.9 | 9.64 | 8.39 | 1.03x | 6.06x | 1.15x |
| knucleotide | hashing | 14.7 | 193.4 | --- | 5.68 | 2.59x | 34.1x | --- |
| mandelbrot | numeric | 153.2 | 70.4 | 778.3 | 17.3 | 8.88x | 4.08x | 45.1x |
| nbody | numeric | 97.5 | 425.8 | 151.7 | 7.61 | 12.8x | 56.0x | 19.9x |
| pidigits | bignum | --- | 0.324 | 0.130 | 2.04 | --- | 0.16x | 0.06x |
| regexredux | regex | 1.50 | 15.9 | --- | 2.52 | 0.59x | 6.32x | --- |
| revcomp | string | 1.15 | 40.9 | --- | 3.46 | 0.33x | 11.8x | --- |
| spectralnorm | numeric | 47.3 | 302.9 | 66.1 | 2.76 | 17.1x | 110x | 24.0x |

## KOSTYA

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 468.4 | 1.11s | 930.0 | 35.0 | 13.4x | 31.7x | 26.6x |
| matmul | numeric | 45.5 | 920.2 | 556.7 | 16.0 | 2.85x | 57.7x | 34.9x |
| primes | numeric | 58.9 | 109.5 | 97.4 | 4.53 | 13.0x | 24.2x | 21.5x |
| base64 | string | 310.0 | 799.3 | 164.3 | 18.5 | 16.8x | 43.3x | 8.90x |
| levenshtein | string | 26.9 | 88.4 | 56.8 | 4.04 | 6.67x | 21.9x | 14.1x |
| json_gen | data | 74.4 | 38.6 | 20.8 | 6.19 | 12.0x | 6.23x | 3.36x |
| collatz | numeric | 1.83s | 2.33s | 6.51s | 1.46s | 1.25x | 1.59x | 4.45x |

## LARCENY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 354.1 | 4.12s | 2.30s | 69.1 | 5.12x | 59.6x | 33.3x |
| array1 | array | 4.90 | 31.3 | 37.4 | 2.06 | 2.38x | 15.2x | 18.2x |
| deriv | symbolic | 35.4 | 83.7 | 63.4 | 3.81 | 9.29x | 22.0x | 16.6x |
| diviter | iterative | 3.61s | 10.53s | 27.98s | 519.0 | 6.95x | 20.3x | 53.9x |
| divrec | recursive | 22.3 | 47.6 | 39.8 | 8.58 | 2.60x | 5.55x | 4.64x |
| gcbench | allocation | 486.4 | 1.12s | 662.2 | 26.8 | 18.2x | 41.7x | 24.8x |
| paraffins | combinat | 2.35 | 2.61 | 2.74 | 1.08 | 2.17x | 2.41x | 2.53x |
| pnpoly | numeric | 122.3 | 158.6 | 220.9 | 6.32 | 19.3x | 25.1x | 35.0x |
| primes | iterative | 62.4 | 121.0 | 103.0 | 4.86 | 12.8x | 24.9x | 21.2x |
| puzzle | search | 20.5 | 30.4 | 31.8 | 3.77 | 5.44x | 8.06x | 8.43x |
| quicksort | sorting | 14.2 | 69.7 | 20.4 | 1.80 | 7.88x | 38.7x | 11.3x |
| ray | numeric | 12.6 | 15.1 | 14.8 | 3.82 | 3.31x | 3.95x | 3.88x |

## JetStream

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 91.3 | 239.3 | 143.4 | 6.04 | 15.1x | 39.6x | 23.7x |
| cube3d | 3d | 14.8 | 845.0 | --- | 20.1 | 0.74x | 42.0x | --- |
| navier_stokes | numeric | 1.09s | 5.93s | 853.7 | 41.7 | 26.1x | 142x | 20.5x |
| richards | macro | 356.7 | 178.4 | 28.2 | 5.38 | 66.3x | 33.1x | 5.24x |
| splay | data | 201.8 | 54.8 | 26.8 | 4.73 | 42.7x | 11.6x | 5.66x |
| deltablue | macro | 20.3 | 378.4 | 51.0 | 7.47 | 2.72x | 50.6x | 6.82x |
| hashmap | data | 163.5 | 64.39s | 2.54s | 56.6 | 2.89x | 1138x | 44.9x |
| crypto_sha1 | crypto | 218.5 | 512.6 | 70.9 | 7.07 | 30.9x | 72.5x | 10.0x |
| raytrace3d | 3d | 348.1 | 1.05s | --- | 18.5 | 18.8x | 56.8x | --- |
