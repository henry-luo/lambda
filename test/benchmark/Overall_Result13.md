# Result13 — Tune6 exit (L1+L2+L3 landed; Track J dropped)

- **Date:** 2026-07-26
- **Platform:** Darwin arm64
- **Lambda commit:** `22eefe3f1c4d11f3b100314124f006fdb1ea429b`
- **Lambda build:** clean release build (`make release`)
- **Instrumentation check:** passed
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v13.json`

JetStream JavaScript-engine wrappers are standardized to an explicit x8 loop over the detected benchmark function. They do not use per-file `Benchmark.runIteration()` counts, because those counts drift across JetStream files.

---

## Summary

| Suite | Total | Timed MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 9 | 10 | 9 | 10 | 1.03x | 7.08x | 6.51x |
| AWFY | 14 | 13 | 14 | 14 | 14 | 1.87x | 21.3x | 5.22x |
| BENG | 10 | 9 | 10 | 7 | 10 | 1.87x | 8.33x | 4.13x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7.39x | 16.2x | 11.9x |
| LARCENY | 12 | 12 | 12 | 12 | 12 | 6.15x | 14.8x | 13.8x |
| JetStream | 9 | 9 | 9 | 7 | 9 | 8.78x | 71.2x | 13.1x |
| **Overall dedup** | **56** | **53** | **56** | **50** | **56** | **2.94x** | **15.4x** | **7.45x** |
| Overall raw | 62 | 59 | 62 | 56 | 62 | 3.24x | 16.5x | 8.04x |

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
| jetstream/hashmap | 63.67s | 56.8 | 1121x |
| awfy/havlak | 89.23s | 97.0 | 920x |
| awfy/cd | 9.35s | 36.5 | 256x |
| jetstream/navier_stokes | 5.45s | 37.9 | 144x |
| beng/spectralnorm | 287.7 | 2.68 | 107x |
| jetstream/crypto_sha1 | 509.2 | 6.86 | 74.2x |
| awfy/deltablue | 803.7 | 11.8 | 68.2x |
| larceny/triangl | 3.97s | 67.1 | 59.1x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.316 | 1.92 | 0.16x |

---

## R7RS

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 7.66 | 36.3 | 18.6 | 1.76 | 4.36x | 20.7x | 10.6x |
| fibfp | recursive | 5.38 | 36.1 | 18.8 | 1.77 | 3.04x | 20.4x | 10.6x |
| tak | recursive | 0.502 | 2.95 | 2.79 | 0.779 | 0.64x | 3.79x | 3.58x |
| cpstak | closure | 1.00 | 5.99 | 5.59 | 0.976 | 1.03x | 6.13x | 5.73x |
| sum | iterative | 3.98 | 11.9 | 31.2 | 1.18 | 3.36x | 10.0x | 26.4x |
| sumfp | iterative | 0.066 | 1.18 | 3.84 | 0.861 | 0.08x | 1.37x | 4.46x |
| nqueens | backtrack | 1.37 | 17.3 | 7.94 | 1.73 | 0.79x | 10.00x | 4.59x |
| fft | numeric | --- | 11.0 | 2.78 | 1.64 | --- | 6.71x | 1.70x |
| mbrot | numeric | 0.788 | 8.23 | 17.9 | 1.81 | 0.43x | 4.54x | 9.84x |
| ack | recursive | 22.2 | 104.3 | --- | 13.5 | 1.64x | 7.71x | --- |

## AWFY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.509 | 0.509 | 0.608 | 0.386 | 1.32x | 1.32x | 1.57x |
| permute | micro | 0.587 | 7.66 | 1.54 | 0.825 | 0.71x | 9.28x | 1.86x |
| queens | micro | 0.486 | 4.53 | 1.05 | 0.638 | 0.76x | 7.09x | 1.64x |
| towers | micro | 0.848 | 16.7 | 2.33 | 1.11 | 0.77x | 15.0x | 2.10x |
| bounce | micro | 0.809 | 3.49 | 0.873 | 0.553 | 1.46x | 6.31x | 1.58x |
| list | micro | --- | 3.43 | 0.906 | 0.485 | --- | 7.07x | 1.87x |
| storage | micro | 0.628 | 13.5 | 2.18 | 0.641 | 0.98x | 21.0x | 3.40x |
| mandelbrot | compute | 45.9 | 351.1 | 873.3 | 31.3 | 1.47x | 11.2x | 27.9x |
| nbody | compute | 80.0 | 274.8 | 160.3 | 5.33 | 15.0x | 51.5x | 30.1x |
| richards | macro | 183.7 | 1.20s | 191.9 | 46.9 | 3.92x | 25.5x | 4.09x |
| json | macro | 5.31 | 37.2 | 11.1 | 2.69 | 1.97x | 13.8x | 4.14x |
| deltablue | macro | 60.2 | 803.7 | 100.0 | 11.8 | 5.12x | 68.2x | 8.49x |
| havlak | macro | 47.9 | 89.23s | 3.31s | 97.0 | 0.49x | 920x | 34.1x |
| cd | macro | 372.0 | 9.35s | 968.0 | 36.5 | 10.2x | 256x | 26.5x |

## BENG

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 10.3 | 39.2 | 23.9 | 4.04 | 2.56x | 9.71x | 5.93x |
| fannkuch | permutation | 0.619 | 12.5 | 7.19 | 4.23 | 0.15x | 2.96x | 1.70x |
| fasta | generation | 6.92 | 37.3 | 8.86 | 6.41 | 1.08x | 5.82x | 1.38x |
| knucleotide | hashing | 11.2 | 149.2 | --- | 4.92 | 2.28x | 30.3x | --- |
| mandelbrot | numeric | 129.6 | 70.4 | 679.7 | 15.1 | 8.57x | 4.66x | 44.9x |
| nbody | numeric | 79.9 | 365.7 | 149.4 | 7.41 | 10.8x | 49.4x | 20.2x |
| pidigits | bignum | --- | 0.316 | 0.130 | 1.92 | --- | 0.16x | 0.07x |
| regexredux | regex | 1.28 | 15.4 | --- | 2.40 | 0.53x | 6.40x | --- |
| revcomp | string | 1.14 | 40.2 | --- | 3.29 | 0.35x | 12.2x | --- |
| spectralnorm | numeric | 47.0 | 287.7 | 64.1 | 2.68 | 17.5x | 107x | 23.9x |

## KOSTYA

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 458.7 | 969.6 | 880.7 | 34.2 | 13.4x | 28.4x | 25.8x |
| matmul | numeric | 41.5 | 862.2 | 542.3 | 15.6 | 2.67x | 55.4x | 34.8x |
| primes | numeric | 59.2 | 104.2 | 95.1 | 4.44 | 13.3x | 23.5x | 21.4x |
| base64 | string | 285.2 | 743.6 | 158.8 | 17.4 | 16.4x | 42.7x | 9.13x |
| levenshtein | string | 45.9 | 82.3 | 54.6 | 4.12 | 11.2x | 20.0x | 13.3x |
| json_gen | data | 70.8 | 36.2 | 20.1 | 6.16 | 11.5x | 5.87x | 3.26x |
| collatz | numeric | 1.71s | 2.25s | 6.27s | 1.43s | 1.20x | 1.57x | 4.39x |

## LARCENY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 642.5 | 3.97s | 2.20s | 67.1 | 9.57x | 59.1x | 32.8x |
| array1 | array | 4.30 | 27.1 | 36.0 | 1.92 | 2.24x | 14.1x | 18.7x |
| deriv | symbolic | 25.4 | 78.8 | 59.2 | 3.70 | 6.87x | 21.3x | 16.0x |
| diviter | iterative | 3.08s | 10.05s | 26.90s | 470.7 | 6.55x | 21.3x | 57.1x |
| divrec | recursive | 21.1 | 39.0 | 36.3 | 7.90 | 2.67x | 4.93x | 4.60x |
| gcbench | allocation | 383.7 | 913.3 | 549.9 | 23.7 | 16.2x | 38.6x | 23.2x |
| paraffins | combinat | 2.10 | 2.32 | 2.68 | 0.987 | 2.12x | 2.35x | 2.72x |
| pnpoly | numeric | 105.1 | 142.1 | 202.8 | 6.01 | 17.5x | 23.7x | 33.8x |
| primes | iterative | 59.1 | 104.1 | 94.9 | 4.45 | 13.3x | 23.4x | 21.3x |
| puzzle | search | 18.7 | 27.6 | 29.4 | 3.29 | 5.69x | 8.38x | 8.94x |
| quicksort | sorting | 12.8 | 61.1 | 19.4 | 1.64 | 7.76x | 37.2x | 11.8x |
| ray | numeric | 11.4 | 13.7 | 13.9 | 3.51 | 3.23x | 3.90x | 3.94x |

## JetStream

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 80.3 | 217.7 | 131.9 | 5.46 | 14.7x | 39.8x | 24.1x |
| cube3d | 3d | 13.0 | 776.3 | --- | 18.1 | 0.72x | 43.0x | --- |
| navier_stokes | numeric | 944.0 | 5.45s | 790.0 | 37.9 | 24.9x | 144x | 20.8x |
| richards | macro | 203.6 | 218.8 | 25.8 | 4.64 | 43.9x | 47.2x | 5.56x |
| splay | data | 157.2 | 49.2 | 23.9 | 3.33 | 47.2x | 14.7x | 7.16x |
| deltablue | macro | 11.9 | 370.0 | 45.4 | 6.33 | 1.88x | 58.5x | 7.17x |
| hashmap | data | 70.4 | 63.67s | 2.51s | 56.8 | 1.24x | 1121x | 44.2x |
| crypto_sha1 | crypto | 204.7 | 509.2 | 70.5 | 6.86 | 29.8x | 74.2x | 10.3x |
| raytrace3d | 3d | 151.6 | 1.05s | --- | 18.6 | 8.15x | 56.5x | --- |
