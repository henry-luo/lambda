# Lambda Benchmark Results — Result11 (Tune-COW)

- **Date:** 2026-07-23
- **Platform:** Darwin arm64
- **Lambda commit:** `d6b1c1c5797670edd39e4afe69f191811a08644e`
- **Lambda build:** clean release build (`make release`)
- **Instrumentation check:** passed
- **Node.js:** v24.7.0
- **QuickJS:** unavailable
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v11.json`

JetStream JavaScript-engine wrappers are standardized to an explicit x8 loop over the detected benchmark function. They do not use per-file `Benchmark.runIteration()` counts, because those counts drift across JetStream files.

---

## Summary

| Suite | Total | Timed MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 9 | 10 | 0 | 10 | 1.50x | 10.2x | --- |
| AWFY | 14 | 13 | 13 | 0 | 14 | 3.96x | 55.4x | --- |
| BENG | 10 | 9 | 10 | 0 | 10 | 2.38x | 9.15x | --- |
| KOSTYA | 7 | 7 | 7 | 0 | 7 | 19.0x | 18.9x | --- |
| LARCENY | 12 | 12 | 12 | 0 | 12 | 8.12x | 23.5x | --- |
| JetStream | 9 | 9 | 9 | 0 | 9 | 32.1x | 150x | --- |
| **Overall dedup** | **56** | **53** | **55** | **0** | **56** | **5.59x** | **26.7x** | **---** |
| Overall raw | 62 | 59 | 61 | 0 | 62 | 6.06x | 27.0x | --- |

> **Overall dedup** is the default headline metric: duplicate benchmark names across suites are counted once, using the best timed value per engine. **Overall raw** keeps the row-weighted value for auditability.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Notable Results

- Missing timings: **66** cells
- QuickJS missing: r7rs/fib (exit_127), r7rs/fibfp (exit_127), r7rs/tak (exit_127), r7rs/cpstak (exit_127), r7rs/sum (exit_127), r7rs/sumfp (exit_127), r7rs/nqueens (exit_127), r7rs/fft (exit_127), +54 more
- MIR missing: r7rs/fft (exit_1), awfy/list (wrong_output), beng/pidigits (exit_1)
- LambdaJS missing: awfy/havlak (timeout)
- Deduplicated benchmark names: mandelbrot (awfy/beng), nbody (awfy/beng/jetstream), richards (awfy/jetstream), deltablue (awfy/jetstream), primes (kostya/larceny)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/cd | 85.62s | 27.2 | 3150x |
| jetstream/hashmap | 113.59s | 50.5 | 2251x |
| jetstream/navier_stokes | 35.61s | 27.9 | 1278x |
| larceny/puzzle | 752.9 | 2.36 | 319x |
| jetstream/crypto_sha1 | 836.2 | 4.13 | 202x |
| awfy/permute | 64.0 | 0.341 | 187x |
| awfy/towers | 68.8 | 0.425 | 162x |
| beng/spectralnorm | 261.7 | 1.80 | 146x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.306 | 2.10 | 0.15x |

---

## R7RS

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 5.95 | 35.3 | --- | 1.47 | 4.05x | 24.0x | --- |
| fibfp | recursive | 5.23 | 37.1 | --- | 1.38 | 3.79x | 26.9x | --- |
| tak | recursive | 0.528 | 3.28 | --- | 0.312 | 1.69x | 10.5x | --- |
| cpstak | closure | 1.08 | 6.18 | --- | 0.472 | 2.29x | 13.1x | --- |
| sum | iterative | 3.66 | 12.6 | --- | 0.891 | 4.11x | 14.1x | --- |
| sumfp | iterative | 0.085 | 1.11 | --- | 0.827 | 0.10x | 1.35x | --- |
| nqueens | backtrack | 1.53 | 13.8 | --- | 1.32 | 1.16x | 10.5x | --- |
| fft | numeric | --- | 8.96 | --- | 1.01 | --- | 8.88x | --- |
| mbrot | numeric | 0.754 | 8.65 | --- | 0.847 | 0.89x | 10.2x | --- |
| ack | recursive | 23.3 | 116.9 | --- | 15.1 | 1.54x | 7.72x | --- |

## AWFY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.506 | 44.1 | --- | 0.345 | 1.47x | 128x | --- |
| permute | micro | 0.709 | 64.0 | --- | 0.341 | 2.08x | 187x | --- |
| queens | micro | 0.757 | 30.8 | --- | 0.363 | 2.09x | 84.9x | --- |
| towers | micro | 1.16 | 68.8 | --- | 0.425 | 2.74x | 162x | --- |
| bounce | micro | 0.845 | 3.55 | --- | 0.360 | 2.35x | 9.87x | --- |
| list | micro | --- | 2.37 | --- | 0.210 | --- | 11.3x | --- |
| storage | micro | 0.678 | 11.1 | --- | 0.321 | 2.11x | 34.5x | --- |
| mandelbrot | compute | 34.5 | 335.5 | --- | 22.1 | 1.56x | 15.2x | --- |
| nbody | compute | 147.2 | 255.7 | --- | 6.51 | 22.6x | 39.3x | --- |
| richards | macro | 312.4 | 1.14s | --- | 40.3 | 7.74x | 28.3x | --- |
| json | macro | 11.3 | 37.9 | --- | 1.47 | 7.67x | 25.8x | --- |
| deltablue | macro | 92.3 | 206.8 | --- | 7.78 | 11.9x | 26.6x | --- |
| havlak | macro | 125.0 | --- | --- | 79.2 | 1.58x | --- | --- |
| cd | macro | 469.2 | 85.62s | --- | 27.2 | 17.3x | 3150x | --- |

## BENG

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 10.3 | 36.5 | --- | 3.90 | 2.65x | 9.36x | --- |
| fannkuch | permutation | 0.745 | 11.9 | --- | 2.91 | 0.26x | 4.10x | --- |
| fasta | generation | 7.29 | 36.6 | --- | 5.01 | 1.45x | 7.30x | --- |
| knucleotide | hashing | 9.77 | 123.5 | --- | 4.39 | 2.23x | 28.1x | --- |
| mandelbrot | numeric | 127.1 | 61.8 | --- | 11.7 | 10.9x | 5.31x | --- |
| nbody | numeric | 163.2 | 322.6 | --- | 6.74 | 24.2x | 47.9x | --- |
| pidigits | bignum | --- | 0.306 | --- | 2.10 | --- | 0.15x | --- |
| regexredux | regex | 1.21 | 13.9 | --- | 2.11 | 0.57x | 6.58x | --- |
| revcomp | string | 1.03 | 43.2 | --- | 2.95 | 0.35x | 14.6x | --- |
| spectralnorm | numeric | 38.4 | 261.7 | --- | 1.80 | 21.4x | 146x | --- |

## KOSTYA

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 396.5 | 1.02s | --- | 33.6 | 11.8x | 30.4x | --- |
| matmul | numeric | 358.7 | 873.3 | --- | 14.4 | 24.9x | 60.7x | --- |
| primes | numeric | 55.3 | 109.3 | --- | 4.91 | 11.3x | 22.2x | --- |
| base64 | string | 10.88s | 727.4 | --- | 15.5 | 704x | 47.1x | --- |
| levenshtein | string | 27.6 | 89.8 | --- | 2.70 | 10.2x | 33.3x | --- |
| json_gen | data | 111.4 | 31.5 | --- | 3.79 | 29.4x | 8.30x | --- |
| collatz | numeric | 1.66s | 2.07s | --- | 1.28s | 1.29x | 1.62x | --- |

## LARCENY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 319.9 | 3.64s | --- | 72.0 | 4.44x | 50.6x | --- |
| array1 | array | 3.81 | 29.0 | --- | 1.79 | 2.13x | 16.2x | --- |
| deriv | symbolic | 30.9 | 66.2 | --- | 2.09 | 14.7x | 31.6x | --- |
| diviter | iterative | 17.51s | 9.87s | --- | 383.9 | 45.6x | 25.7x | --- |
| divrec | recursive | 20.5 | 49.3 | --- | 8.36 | 2.45x | 5.90x | --- |
| gcbench | allocation | 381.1 | 938.0 | --- | 22.8 | 16.7x | 41.1x | --- |
| paraffins | combinat | 2.08 | 2.39 | --- | 0.626 | 3.32x | 3.82x | --- |
| pnpoly | numeric | 94.0 | 154.5 | --- | 4.33 | 21.7x | 35.7x | --- |
| primes | iterative | 53.5 | 109.7 | --- | 4.78 | 11.2x | 22.9x | --- |
| puzzle | search | 19.0 | 752.9 | --- | 2.36 | 8.04x | 319x | --- |
| quicksort | sorting | 13.0 | 44.2 | --- | 1.65 | 7.87x | 26.8x | --- |
| ray | numeric | 9.95 | 10.5 | --- | 1.62 | 6.15x | 6.49x | --- |

## JetStream

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 164.1 | 217.3 | --- | 4.43 | 37.0x | 49.0x | --- |
| cube3d | 3d | 57.6 | 818.5 | --- | 13.0 | 4.43x | 62.9x | --- |
| navier_stokes | numeric | 1.60s | 35.61s | --- | 27.9 | 57.6x | 1278x | --- |
| richards | macro | 310.4 | 163.0 | --- | 1.79 | 173x | 91.0x | --- |
| splay | data | 2.22s | 45.3 | --- | 1.48 | 1502x | 30.6x | --- |
| deltablue | macro | 16.4 | 333.8 | --- | 3.99 | 4.11x | 83.6x | --- |
| hashmap | data | 137.3 | 113.59s | --- | 50.5 | 2.72x | 2251x | --- |
| crypto_sha1 | crypto | 176.0 | 836.2 | --- | 4.13 | 42.6x | 202x | --- |
| raytrace3d | 3d | 339.5 | 1.04s | --- | 11.0 | 30.9x | 94.6x | --- |
