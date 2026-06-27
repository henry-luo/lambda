# Lambda Benchmark Results: Round 9

- **Date:** 2026-06-27
- **Platform:** Darwin arm64
- **Lambda commit:** `a18102ceaf4ad445e76cefa10aa846d9e3c1ffa7`
- **Lambda build:** clean release build (`make release`)
- **Instrumentation check:** passed
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v9.json`

JetStream JavaScript-engine wrappers are standardized to an explicit x8 loop over the detected benchmark function. They do not use per-file `Benchmark.runIteration()` counts, because those counts drift across JetStream files.

---

## Summary

| Suite | Total | Timed MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 9 | 10 | 2.03x | 5.21x | 6.66x |
| AWFY | 14 | 14 | 12 | 14 | 14 | 3.60x | 36.6x | 5.23x |
| BENG | 10 | 10 | 10 | 7 | 10 | 1.57x | 6.18x | 4.40x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 9.86x | 14.6x | 11.7x |
| LARCENY | 12 | 12 | 12 | 12 | 12 | 6.19x | 8.24x | 14.1x |
| JetStream | 9 | 9 | 8 | 7 | 9 | 21.2x | 148x | 13.1x |
| **Overall dedup** | **56** | **56** | **53** | **50** | **56** | **4.31x** | **13.1x** | **7.58x** |
| Overall raw | 62 | 62 | 59 | 56 | 62 | 4.62x | 15.6x | 8.16x |

> **Overall dedup** is the default headline metric: duplicate benchmark names across suites are counted once, using the best timed value per engine. **Overall raw** keeps the row-weighted value for auditability.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Notable Results

- Missing timings: **9** cells
- QuickJS missing: r7rs/ack (not_recorded), beng/knucleotide (not_recorded), beng/regexredux (not_recorded), beng/revcomp (not_recorded), jetstream/cube3d (not_recorded), jetstream/raytrace3d (not_recorded)
- LambdaJS missing: awfy/havlak (not_recorded), awfy/cd (not_recorded), jetstream/hashmap (not_recorded)
- Deduplicated benchmark names: mandelbrot (awfy/beng), nbody (awfy/beng/jetstream), richards (awfy/jetstream), deltablue (awfy/jetstream), primes (kostya/larceny)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| jetstream/splay | 1.62s | 4.12 | 392x |
| awfy/deltablue | 4.17s | 13.7 | 304x |
| jetstream/deltablue | 1.88s | 7.06 | 266x |
| jetstream/cube3d | 3.72s | 18.8 | 198x |
| kostya/matmul | 2.87s | 16.3 | 176x |
| jetstream/nbody | 929.2 | 5.68 | 163x |
| jetstream/navier_stokes | 6.23s | 40.2 | 155x |
| beng/nbody | 1.17s | 7.78 | 150x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.318 | 2.02 | 0.16x |
| beng/fannkuch | 2.06 | 4.19 | 0.49x |

---

## R7RS

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 56.9 | 17.2 | 19.0 | 1.81 | 31.5x | 9.53x | 10.5x |
| fibfp | recursive | 60.3 | 22.4 | 18.8 | 1.81 | 33.4x | 12.4x | 10.4x |
| tak | recursive | 3.26 | 1.26 | 2.88 | 0.826 | 3.94x | 1.53x | 3.49x |
| cpstak | closure | 6.41 | 2.52 | 5.67 | 0.978 | 6.55x | 2.57x | 5.80x |
| sum | iterative | 0.282 | 22.9 | 32.9 | 1.18 | 0.24x | 19.4x | 27.9x |
| sumfp | iterative | 0.069 | 1.67 | 3.82 | 0.905 | 0.08x | 1.84x | 4.23x |
| nqueens | backtrack | 2.57 | 3.81 | 9.88 | 1.75 | 1.47x | 2.17x | 5.64x |
| fft | numeric | 0.447 | 6.75 | 2.84 | 1.67 | 0.27x | 4.04x | 1.70x |
| mbrot | numeric | 0.867 | 29.0 | 18.4 | 1.80 | 0.48x | 16.1x | 10.2x |
| ack | recursive | 172.0 | 86.4 | --- | 13.8 | 12.5x | 6.27x | --- |

## AWFY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.415 | 0.491 | 0.633 | 0.405 | 1.02x | 1.21x | 1.56x |
| permute | micro | 2.65 | 38.7 | 1.58 | 0.825 | 3.21x | 46.9x | 1.92x |
| queens | micro | 1.95 | 25.1 | 1.14 | 0.922 | 2.11x | 27.2x | 1.24x |
| towers | micro | 4.95 | 70.8 | 2.33 | 1.14 | 4.36x | 62.3x | 2.05x |
| bounce | micro | 0.704 | 10.2 | 0.926 | 0.558 | 1.26x | 18.2x | 1.66x |
| list | micro | 0.200 | 11.9 | 0.942 | 0.538 | 0.37x | 22.1x | 1.75x |
| storage | micro | 1.72 | 30.2 | 2.71 | 0.649 | 2.65x | 46.5x | 4.16x |
| mandelbrot | compute | 32.8 | 1.52s | 905.8 | 32.3 | 1.01x | 46.9x | 28.0x |
| nbody | compute | 79.9 | 430.1 | 167.6 | 5.87 | 13.6x | 73.2x | 28.5x |
| richards | macro | 636.3 | 5.54s | 201.0 | 49.5 | 12.8x | 112x | 4.06x |
| json | macro | 20.7 | 75.6 | 12.0 | 2.76 | 7.52x | 27.4x | 4.36x |
| deltablue | macro | 228.2 | 4.17s | 116.0 | 13.7 | 16.6x | 304x | 8.45x |
| havlak | macro | 220.9 | --- | 4.37s | 115.5 | 1.91x | --- | 37.9x |
| cd | macro | 1.53s | --- | 1.11s | 40.0 | 38.1x | --- | 27.8x |

## BENG

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 44.4 | 52.1 | 29.1 | 4.54 | 9.79x | 11.5x | 6.42x |
| fannkuch | permutation | 2.00 | 2.06 | 7.45 | 4.19 | 0.48x | 0.49x | 1.78x |
| fasta | generation | 1.73 | 7.60 | 10.3 | 6.06 | 0.28x | 1.25x | 1.70x |
| knucleotide | hashing | 5.20 | 122.2 | --- | 5.60 | 0.93x | 21.8x | --- |
| mandelbrot | numeric | 192.3 | 184.1 | 712.3 | 15.9 | 12.1x | 11.6x | 44.9x |
| nbody | numeric | 81.1 | 1.17s | 157.5 | 7.78 | 10.4x | 150x | 20.2x |
| pidigits | bignum | 0.325 | 0.318 | 0.156 | 2.02 | 0.16x | 0.16x | 0.08x |
| regexredux | regex | 1.25 | 15.0 | --- | 3.09 | 0.40x | 4.86x | --- |
| revcomp | string | 1.15 | 46.5 | --- | 3.63 | 0.32x | 12.8x | --- |
| spectralnorm | numeric | 79.7 | 87.8 | 65.9 | 2.83 | 28.2x | 31.0x | 23.3x |

## KOSTYA

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 453.1 | 726.8 | 956.4 | 47.9 | 9.46x | 15.2x | 20.0x |
| matmul | numeric | 34.6 | 2.87s | 563.6 | 16.3 | 2.12x | 176x | 34.5x |
| primes | numeric | 11.9 | 16.5 | 99.9 | 4.79 | 2.49x | 3.45x | 20.9x |
| base64 | string | 15.52s | 806.0 | 187.2 | 18.6 | 836x | 43.4x | 10.1x |
| levenshtein | string | 155.8 | 58.9 | 56.7 | 4.07 | 38.3x | 14.5x | 14.0x |
| json_gen | data | 150.6 | 33.0 | 21.9 | 6.37 | 23.6x | 5.19x | 3.43x |
| collatz | numeric | 354.6 | 6.94s | 6.52s | 1.48s | 0.24x | 4.67x | 4.39x |

## LARCENY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 397.9 | 2.32s | 2.28s | 70.9 | 5.61x | 32.7x | 32.2x |
| array1 | array | 1.99 | 3.40 | 39.2 | 1.99 | 1.00x | 1.71x | 19.7x |
| deriv | symbolic | 72.9 | 107.2 | 71.3 | 3.91 | 18.6x | 27.4x | 18.2x |
| diviter | iterative | 15.63s | 2.66s | 28.64s | 494.0 | 31.6x | 5.39x | 58.0x |
| divrec | recursive | 19.6 | 28.1 | 38.9 | 8.02 | 2.45x | 3.50x | 4.85x |
| gcbench | allocation | 1.37s | 1.22s | 678.9 | 25.3 | 54.0x | 48.4x | 26.8x |
| paraffins | combinat | 1.03 | 2.14 | 2.70 | 1.15 | 0.89x | 1.86x | 2.34x |
| pnpoly | numeric | 121.8 | 138.4 | 212.4 | 6.09 | 20.0x | 22.7x | 34.8x |
| primes | iterative | 11.8 | 15.9 | 99.2 | 4.80 | 2.46x | 3.31x | 20.6x |
| puzzle | search | 17.8 | 22.7 | 30.9 | 3.43 | 5.18x | 6.62x | 9.00x |
| quicksort | sorting | 11.2 | 28.8 | 20.0 | 1.74 | 6.43x | 16.5x | 11.5x |
| ray | numeric | 18.1 | 16.6 | 14.4 | 3.62 | 5.00x | 4.58x | 3.97x |

## JetStream

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 80.4 | 929.2 | 137.7 | 5.68 | 14.1x | 163x | 24.2x |
| cube3d | 3d | 46.7 | 3.72s | --- | 18.8 | 2.49x | 198x | --- |
| navier_stokes | numeric | 1.29s | 6.23s | 819.6 | 40.2 | 32.0x | 155x | 20.4x |
| richards | macro | 539.1 | 628.2 | 26.9 | 5.29 | 102x | 119x | 5.08x |
| splay | data | 3.17s | 1.62s | 31.1 | 4.12 | 769x | 392x | 7.56x |
| deltablue | macro | 40.0 | 1.88s | 49.2 | 7.06 | 5.66x | 266x | 6.97x |
| hashmap | data | 245.6 | --- | 2.67s | 54.8 | 4.48x | --- | 48.7x |
| crypto_sha1 | crypto | 116.9 | 233.8 | 76.0 | 7.42 | 15.7x | 31.5x | 10.2x |
| raytrace3d | 3d | 471.2 | 2.26s | --- | 19.6 | 24.1x | 115x | --- |
