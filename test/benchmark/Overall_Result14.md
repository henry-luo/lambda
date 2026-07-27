# Lambda Benchmark Results: Tune7 Result14

- **Date:** 2026-07-26
- **Platform:** Darwin arm64
- **Lambda commit:** `de30aae9686a192284755ece08e27c547181429b`
- **Lambda build:** clean release build (`make release`)
- **Instrumentation check:** passed
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v14.json`

> This is a fresh local snapshot on the current Darwin arm64 machine. It supersedes the earlier Result14 capture from a different machine and hardware configuration; Result14-to-Result13 performance deltas are therefore not an Apple-to-Apple comparison.

JetStream JavaScript-engine wrappers are standardized to an explicit x8 loop over the detected benchmark function. They do not use per-file `Benchmark.runIteration()` counts, because those counts drift across JetStream files.

---

## Summary

| Suite | Total | Timed MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 9 | 10 | 9 | 10 | 1.00x | 6.79x | 6.41x |
| AWFY | 14 | 13 | 14 | 14 | 14 | 1.85x | 20.7x | 5.17x |
| BENG | 10 | 9 | 10 | 7 | 10 | 1.91x | 8.08x | 3.99x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7.38x | 16.2x | 11.8x |
| LARCENY | 12 | 12 | 12 | 12 | 12 | 6.15x | 14.8x | 13.7x |
| JetStream | 9 | 9 | 9 | 7 | 9 | 8.28x | 65.8x | 12.0x |
| **Overall dedup** | **56** | **53** | **56** | **50** | **56** | **2.90x** | **14.9x** | **7.28x** |
| Overall raw | 62 | 59 | 62 | 56 | 62 | 3.20x | 16.0x | 7.86x |

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
| jetstream/hashmap | 64.22s | 58.2 | 1104x |
| awfy/havlak | 90.70s | 97.4 | 932x |
| awfy/cd | 9.70s | 36.8 | 264x |
| jetstream/navier_stokes | 5.09s | 38.5 | 132x |
| beng/spectralnorm | 288.5 | 2.54 | 114x |
| jetstream/crypto_sha1 | 512.2 | 7.19 | 71.2x |
| awfy/deltablue | 811.9 | 12.0 | 67.9x |
| larceny/triangl | 4.02s | 67.6 | 59.4x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.315 | 2.72 | 0.12x |

---

## R7RS

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 6.61 | 34.1 | 19.1 | 1.79 | 3.70x | 19.1x | 10.7x |
| fibfp | recursive | 5.32 | 34.1 | 18.9 | 1.78 | 2.99x | 19.2x | 10.6x |
| tak | recursive | 0.508 | 2.81 | 2.83 | 0.817 | 0.62x | 3.45x | 3.46x |
| cpstak | closure | 1.03 | 5.59 | 5.67 | 0.998 | 1.03x | 5.60x | 5.68x |
| sum | iterative | 4.07 | 11.9 | 31.5 | 1.23 | 3.32x | 9.72x | 25.7x |
| sumfp | iterative | 0.067 | 1.20 | 3.70 | 0.887 | 0.08x | 1.35x | 4.17x |
| nqueens | backtrack | 1.39 | 17.0 | 7.98 | 1.74 | 0.80x | 9.77x | 4.58x |
| fft | numeric | --- | 10.9 | 2.78 | 1.60 | --- | 6.79x | 1.74x |
| mbrot | numeric | 0.813 | 8.47 | 17.9 | 1.86 | 0.44x | 4.54x | 9.58x |
| ack | recursive | 22.4 | 100.6 | --- | 13.6 | 1.65x | 7.41x | --- |

## AWFY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.507 | 0.520 | 0.617 | 0.402 | 1.26x | 1.29x | 1.53x |
| permute | micro | 0.564 | 7.02 | 1.57 | 0.825 | 0.68x | 8.51x | 1.91x |
| queens | micro | 0.500 | 4.33 | 1.05 | 0.656 | 0.76x | 6.60x | 1.61x |
| towers | micro | 0.845 | 16.1 | 2.27 | 1.11 | 0.76x | 14.4x | 2.04x |
| bounce | micro | 0.830 | 3.44 | 0.877 | 0.567 | 1.46x | 6.06x | 1.55x |
| list | micro | --- | 3.31 | 0.927 | 0.509 | --- | 6.51x | 1.82x |
| storage | micro | 0.640 | 13.4 | 2.21 | 0.640 | 1.00x | 20.9x | 3.44x |
| mandelbrot | compute | 46.3 | 371.0 | 899.9 | 31.7 | 1.46x | 11.7x | 28.4x |
| nbody | compute | 82.2 | 279.2 | 161.4 | 5.77 | 14.3x | 48.4x | 28.0x |
| richards | macro | 185.7 | 1.18s | 193.5 | 47.2 | 3.93x | 24.9x | 4.10x |
| json | macro | 5.18 | 36.8 | 11.0 | 2.74 | 1.89x | 13.4x | 4.02x |
| deltablue | macro | 61.1 | 811.9 | 101.0 | 12.0 | 5.11x | 67.9x | 8.45x |
| havlak | macro | 48.8 | 90.70s | 3.39s | 97.4 | 0.50x | 932x | 34.8x |
| cd | macro | 379.7 | 9.70s | 989.9 | 36.8 | 10.3x | 264x | 26.9x |

## BENG

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 10.3 | 38.1 | 23.6 | 4.15 | 2.48x | 9.18x | 5.68x |
| fannkuch | permutation | 0.631 | 12.6 | 7.26 | 4.08 | 0.15x | 3.08x | 1.78x |
| fasta | generation | 7.06 | 37.7 | 8.79 | 6.04 | 1.17x | 6.24x | 1.46x |
| knucleotide | hashing | 11.6 | 151.4 | --- | 5.01 | 2.31x | 30.2x | --- |
| mandelbrot | numeric | 134.9 | 71.2 | 689.5 | 15.3 | 8.81x | 4.65x | 45.1x |
| nbody | numeric | 82.1 | 363.1 | 150.4 | 7.60 | 10.8x | 47.8x | 19.8x |
| pidigits | bignum | --- | 0.315 | 0.131 | 2.72 | --- | 0.12x | 0.05x |
| regexredux | regex | 1.30 | 15.4 | --- | 2.43 | 0.54x | 6.35x | --- |
| revcomp | string | 1.15 | 40.8 | --- | 3.41 | 0.34x | 12.0x | --- |
| spectralnorm | numeric | 48.1 | 288.5 | 64.7 | 2.54 | 18.9x | 114x | 25.5x |

## KOSTYA

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 463.6 | 982.2 | 898.5 | 34.2 | 13.6x | 28.7x | 26.3x |
| matmul | numeric | 43.2 | 883.4 | 549.2 | 15.7 | 2.75x | 56.2x | 34.9x |
| primes | numeric | 59.8 | 104.4 | 96.4 | 4.71 | 12.7x | 22.2x | 20.5x |
| base64 | string | 296.8 | 756.1 | 160.5 | 17.6 | 16.9x | 43.1x | 9.14x |
| levenshtein | string | 45.8 | 87.7 | 55.1 | 4.04 | 11.3x | 21.7x | 13.6x |
| json_gen | data | 72.9 | 36.7 | 20.1 | 6.74 | 10.8x | 5.46x | 2.98x |
| collatz | numeric | 1.74s | 2.30s | 6.37s | 1.44s | 1.21x | 1.60x | 4.43x |

## LARCENY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 648.2 | 4.02s | 2.22s | 67.6 | 9.59x | 59.4x | 32.8x |
| array1 | array | 4.34 | 27.7 | 36.4 | 1.94 | 2.23x | 14.3x | 18.7x |
| deriv | symbolic | 25.2 | 78.3 | 59.6 | 3.75 | 6.73x | 20.9x | 15.9x |
| diviter | iterative | 3.12s | 10.18s | 27.14s | 478.7 | 6.52x | 21.3x | 56.7x |
| divrec | recursive | 21.3 | 38.6 | 36.7 | 7.76 | 2.74x | 4.97x | 4.73x |
| gcbench | allocation | 379.9 | 901.6 | 561.2 | 24.1 | 15.8x | 37.4x | 23.3x |
| paraffins | combinat | 2.13 | 2.38 | 2.54 | 1.01 | 2.11x | 2.36x | 2.52x |
| pnpoly | numeric | 106.8 | 143.8 | 204.3 | 5.90 | 18.1x | 24.4x | 34.6x |
| primes | iterative | 61.2 | 104.7 | 96.0 | 4.43 | 13.8x | 23.6x | 21.7x |
| puzzle | search | 18.9 | 27.8 | 29.7 | 3.38 | 5.59x | 8.22x | 8.78x |
| quicksort | sorting | 12.9 | 60.9 | 19.4 | 1.69 | 7.60x | 36.0x | 11.5x |
| ray | numeric | 11.7 | 14.2 | 14.0 | 3.62 | 3.23x | 3.93x | 3.87x |

## JetStream

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 82.4 | 217.8 | 133.5 | 5.60 | 14.7x | 38.9x | 23.8x |
| cube3d | 3d | 13.2 | 782.1 | --- | 18.0 | 0.74x | 43.5x | --- |
| navier_stokes | numeric | 955.9 | 5.09s | 799.7 | 38.5 | 24.8x | 132x | 20.8x |
| richards | macro | 202.1 | 218.9 | 26.0 | 5.51 | 36.7x | 39.7x | 4.72x |
| splay | data | 159.6 | 50.1 | 24.5 | 4.81 | 33.2x | 10.4x | 5.09x |
| deltablue | macro | 12.2 | 370.8 | 45.8 | 6.54 | 1.86x | 56.7x | 7.01x |
| hashmap | data | 72.2 | 64.22s | 2.55s | 58.2 | 1.24x | 1104x | 43.9x |
| crypto_sha1 | crypto | 211.6 | 512.2 | 71.0 | 7.19 | 29.4x | 71.2x | 9.87x |
| raytrace3d | 3d | 155.1 | 1.05s | --- | 18.9 | 8.21x | 55.6x | --- |
