# Lambda Benchmark Results: Result16

- **Date:** 2026-07-28
- **Platform:** Darwin arm64
- **Lambda commit:** `2b8c6037ebefef47802d22007aff955985992d90`
- **Lambda build:** clean release build (`make release`)
- **Instrumentation check:** passed
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v16.json`

JetStream JavaScript-engine wrappers are standardized to an explicit x8 loop over the detected benchmark function. They do not use per-file `Benchmark.runIteration()` counts, because those counts drift across JetStream files.

---

## Summary

| Suite | Total | Timed MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 9 | 10 | 1.17x | 5.75x | 6.24x |
| AWFY | 14 | 13 | 14 | 14 | 14 | 1.80x | 19.3x | 5.10x |
| BENG | 10 | 10 | 10 | 7 | 10 | 1.48x | 7.75x | 4.26x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 5.10x | 15.9x | 12.0x |
| LARCENY | 12 | 12 | 12 | 12 | 12 | 5.11x | 13.7x | 13.8x |
| JetStream | 9 | 9 | 9 | 7 | 9 | 8.07x | 58.0x | 12.7x |
| **Overall dedup** | **56** | **55** | **56** | **50** | **56** | **2.53x** | **13.6x** | **7.36x** |
| Overall raw | 62 | 61 | 62 | 56 | 62 | 2.80x | 14.7x | 7.95x |

> **Overall dedup** is the default headline metric: duplicate benchmark names across suites are counted once, using the best timed value per engine. **Overall raw** keeps the row-weighted value for auditability.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Notable Results

- Missing timings: **7** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- MIR missing: awfy/list (wrong_output)
- Deduplicated benchmark names: mandelbrot (awfy/beng), nbody (awfy/beng/jetstream), richards (awfy/jetstream), deltablue (awfy/jetstream), primes (kostya/larceny)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 42.88s | 95.6 | 448x |
| jetstream/hashmap | 23.25s | 56.9 | 409x |
| awfy/cd | 7.45s | 36.0 | 207x |
| jetstream/navier_stokes | 4.70s | 37.6 | 125x |
| beng/spectralnorm | 292.2 | 2.43 | 120x |
| jetstream/crypto_sha1 | 484.4 | 7.16 | 67.7x |
| awfy/deltablue | 762.3 | 11.5 | 66.3x |
| larceny/triangl | 4.17s | 66.5 | 62.7x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.319 | 1.91 | 0.17x |

---

## R7RS

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 6.64 | 20.3 | 17.4 | 1.65 | 4.02x | 12.3x | 10.5x |
| fibfp | recursive | 5.77 | 20.3 | 17.4 | 1.69 | 3.42x | 12.1x | 10.3x |
| tak | recursive | 0.537 | 1.77 | 2.59 | 0.734 | 0.73x | 2.42x | 3.54x |
| cpstak | closure | 1.07 | 3.54 | 5.14 | 0.925 | 1.16x | 3.83x | 5.56x |
| sum | iterative | 4.04 | 12.1 | 29.0 | 1.16 | 3.48x | 10.4x | 25.0x |
| sumfp | iterative | 0.234 | 1.14 | 3.40 | 0.873 | 0.27x | 1.30x | 3.89x |
| nqueens | backtrack | 1.42 | 17.7 | 7.31 | 1.67 | 0.85x | 10.6x | 4.37x |
| fft | numeric | 0.940 | 11.4 | 2.62 | 1.60 | 0.59x | 7.12x | 1.63x |
| mbrot | numeric | 0.822 | 8.94 | 16.5 | 1.72 | 0.48x | 5.21x | 9.64x |
| ack | recursive | 22.6 | 68.0 | --- | 12.5 | 1.80x | 5.42x | --- |

## AWFY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.512 | 0.490 | 0.580 | 0.382 | 1.34x | 1.28x | 1.52x |
| permute | micro | 0.472 | 6.46 | 1.44 | 0.831 | 0.57x | 7.77x | 1.73x |
| queens | micro | 0.510 | 4.03 | 0.972 | 0.629 | 0.81x | 6.40x | 1.55x |
| towers | micro | 0.732 | 15.4 | 2.12 | 1.06 | 0.69x | 14.5x | 1.99x |
| bounce | micro | 0.816 | 3.36 | 0.823 | 0.511 | 1.60x | 6.57x | 1.61x |
| list | micro | --- | 3.61 | 0.878 | 0.551 | --- | 6.55x | 1.59x |
| storage | micro | 0.586 | 12.7 | 2.10 | 0.596 | 0.98x | 21.3x | 3.53x |
| mandelbrot | compute | 50.8 | 322.2 | 814.5 | 29.2 | 1.74x | 11.1x | 27.9x |
| nbody | compute | 81.4 | 266.1 | 154.8 | 5.21 | 15.6x | 51.0x | 29.7x |
| richards | macro | 138.8 | 1.19s | 186.9 | 46.5 | 2.98x | 25.6x | 4.02x |
| json | macro | 4.38 | 34.9 | 10.9 | 2.59 | 1.69x | 13.5x | 4.22x |
| deltablue | macro | 54.2 | 762.3 | 98.2 | 11.5 | 4.71x | 66.3x | 8.55x |
| havlak | macro | 47.2 | 42.88s | 3.27s | 95.6 | 0.49x | 448x | 34.2x |
| cd | macro | 362.5 | 7.45s | 970.0 | 36.0 | 10.1x | 207x | 26.9x |

## BENG

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.26 | 34.0 | 23.2 | 3.95 | 2.34x | 8.61x | 5.88x |
| fannkuch | permutation | 0.637 | 12.5 | 7.11 | 3.96 | 0.16x | 3.17x | 1.80x |
| fasta | generation | 6.77 | 19.8 | 8.67 | 6.13 | 1.10x | 3.23x | 1.42x |
| knucleotide | hashing | 11.4 | 150.7 | --- | 4.88 | 2.33x | 30.9x | --- |
| mandelbrot | numeric | 126.0 | 60.2 | 660.8 | 14.7 | 8.58x | 4.10x | 45.0x |
| nbody | numeric | 80.6 | 368.5 | 150.6 | 7.37 | 10.9x | 50.0x | 20.4x |
| pidigits | bignum | 0.303 | 0.319 | 0.136 | 1.91 | 0.16x | 0.17x | 0.07x |
| regexredux | regex | 1.29 | 14.8 | --- | 2.41 | 0.54x | 6.15x | --- |
| revcomp | string | 1.09 | 37.4 | --- | 3.29 | 0.33x | 11.4x | --- |
| spectralnorm | numeric | 46.4 | 292.2 | 63.4 | 2.43 | 19.0x | 120x | 26.0x |

## KOSTYA

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 503.0 | 977.5 | 874.7 | 32.7 | 15.4x | 29.9x | 26.7x |
| matmul | numeric | 41.4 | 893.5 | 533.2 | 15.3 | 2.71x | 58.6x | 34.9x |
| primes | numeric | 56.0 | 97.7 | 94.7 | 4.42 | 12.7x | 22.1x | 21.5x |
| base64 | string | 76.4 | 649.1 | 158.8 | 17.4 | 4.40x | 37.4x | 9.14x |
| levenshtein | string | 44.8 | 82.7 | 53.7 | 3.95 | 11.4x | 20.9x | 13.6x |
| json_gen | data | 17.0 | 34.0 | 20.4 | 6.22 | 2.74x | 5.46x | 3.28x |
| collatz | numeric | 1.72s | 2.12s | 6.10s | 1.38s | 1.25x | 1.54x | 4.43x |

## LARCENY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 335.6 | 4.17s | 2.16s | 66.5 | 5.05x | 62.7x | 32.5x |
| array1 | array | 4.33 | 27.5 | 36.2 | 1.91 | 2.27x | 14.4x | 18.9x |
| deriv | symbolic | 24.7 | 75.6 | 59.3 | 3.66 | 6.75x | 20.7x | 16.2x |
| diviter | iterative | 1.20s | 8.35s | 26.16s | 454.3 | 2.65x | 18.4x | 57.6x |
| divrec | recursive | 20.1 | 25.9 | 36.6 | 7.51 | 2.68x | 3.44x | 4.87x |
| gcbench | allocation | 219.4 | 800.1 | 552.5 | 23.6 | 9.31x | 33.9x | 23.4x |
| paraffins | combinat | 2.16 | 2.48 | 2.52 | 0.984 | 2.19x | 2.53x | 2.57x |
| pnpoly | numeric | 105.2 | 123.5 | 202.1 | 5.80 | 18.1x | 21.3x | 34.9x |
| primes | iterative | 55.4 | 97.8 | 94.2 | 4.42 | 12.5x | 22.1x | 21.3x |
| puzzle | search | 18.2 | 24.5 | 29.1 | 3.31 | 5.50x | 7.38x | 8.77x |
| quicksort | sorting | 12.5 | 64.7 | 19.2 | 1.69 | 7.39x | 38.2x | 11.4x |
| ray | numeric | 11.4 | 11.6 | 13.9 | 3.72 | 3.07x | 3.11x | 3.72x |

## JetStream

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 80.3 | 234.9 | 132.1 | 5.69 | 14.1x | 41.3x | 23.2x |
| cube3d | 3d | 13.7 | 625.3 | --- | 17.8 | 0.77x | 35.1x | --- |
| navier_stokes | numeric | 929.1 | 4.70s | 788.2 | 37.6 | 24.7x | 125x | 21.0x |
| richards | macro | 162.5 | 215.5 | 26.0 | 4.78 | 34.0x | 45.1x | 5.44x |
| splay | data | 142.2 | 46.8 | 23.8 | 3.69 | 38.6x | 12.7x | 6.47x |
| deltablue | macro | 10.8 | 307.8 | 45.2 | 6.47 | 1.67x | 47.5x | 6.98x |
| hashmap | data | 68.3 | 23.25s | 2.51s | 56.9 | 1.20x | 409x | 44.1x |
| crypto_sha1 | crypto | 180.4 | 484.4 | 70.5 | 7.16 | 25.2x | 67.7x | 9.84x |
| raytrace3d | 3d | 150.2 | 1.00s | --- | 18.5 | 8.12x | 54.2x | --- |
