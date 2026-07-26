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
| R7RS | 10 | 9 | 10 | 9 | 10 | 0.99x | 6.95x | 6.29x |
| AWFY | 14 | 13 | 14 | 14 | 14 | 1.86x | 21.0x | 5.23x |
| BENG | 10 | 9 | 10 | 7 | 10 | 1.87x | 8.33x | 4.14x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7.54x | 16.6x | 12.1x |
| LARCENY | 12 | 12 | 12 | 12 | 12 | 6.12x | 14.9x | 13.9x |
| JetStream | 9 | 9 | 9 | 7 | 9 | 8.70x | 70.7x | 12.9x |
| **Overall dedup** | **56** | **53** | **56** | **50** | **56** | **2.92x** | **15.3x** | **7.42x** |
| Overall raw | 62 | 59 | 62 | 56 | 62 | 3.22x | 16.5x | 8.02x |

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
| jetstream/hashmap | 62.81s | 54.4 | 1154x |
| awfy/havlak | 67.04s | 92.2 | 727x |
| awfy/cd | 9.42s | 36.3 | 260x |
| jetstream/navier_stokes | 5.52s | 37.3 | 148x |
| beng/spectralnorm | 290.6 | 2.76 | 105x |
| jetstream/crypto_sha1 | 503.8 | 6.93 | 72.7x |
| awfy/deltablue | 805.3 | 12.0 | 67.0x |
| larceny/triangl | 4.01s | 66.9 | 59.9x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.332 | 1.94 | 0.17x |

---

## R7RS

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 6.84 | 37.1 | 19.2 | 2.02 | 3.38x | 18.4x | 9.49x |
| fibfp | recursive | 5.23 | 37.1 | 19.5 | 1.84 | 2.84x | 20.2x | 10.6x |
| tak | recursive | 0.520 | 3.10 | 2.82 | 0.814 | 0.64x | 3.81x | 3.47x |
| cpstak | closure | 1.03 | 6.11 | 5.69 | 1.01 | 1.02x | 6.05x | 5.63x |
| sum | iterative | 4.05 | 12.2 | 32.2 | 1.22 | 3.33x | 9.99x | 26.5x |
| sumfp | iterative | 0.068 | 1.20 | 3.70 | 0.927 | 0.07x | 1.29x | 3.99x |
| nqueens | backtrack | 1.39 | 17.8 | 8.01 | 1.74 | 0.80x | 10.2x | 4.62x |
| fft | numeric | --- | 10.9 | 2.77 | 1.65 | --- | 6.59x | 1.68x |
| mbrot | numeric | 0.801 | 8.36 | 17.9 | 1.87 | 0.43x | 4.47x | 9.57x |
| ack | recursive | 22.6 | 107.3 | --- | 13.6 | 1.66x | 7.90x | --- |

## AWFY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.506 | 0.547 | 0.616 | 0.393 | 1.29x | 1.39x | 1.57x |
| permute | micro | 0.574 | 7.43 | 1.54 | 0.821 | 0.70x | 9.05x | 1.88x |
| queens | micro | 0.496 | 4.62 | 1.05 | 0.650 | 0.76x | 7.11x | 1.62x |
| towers | micro | 0.831 | 16.6 | 2.29 | 1.13 | 0.73x | 14.7x | 2.03x |
| bounce | micro | 0.835 | 3.56 | 0.888 | 0.569 | 1.47x | 6.26x | 1.56x |
| list | micro | --- | 3.46 | 0.926 | 0.502 | --- | 6.89x | 1.85x |
| storage | micro | 0.627 | 13.5 | 2.22 | 0.635 | 0.99x | 21.2x | 3.49x |
| mandelbrot | compute | 46.4 | 359.1 | 908.6 | 31.6 | 1.47x | 11.4x | 28.8x |
| nbody | compute | 80.0 | 273.6 | 161.2 | 5.38 | 14.9x | 50.8x | 30.0x |
| richards | macro | 183.8 | 1.20s | 193.7 | 46.7 | 3.94x | 25.8x | 4.15x |
| json | macro | 5.12 | 37.2 | 11.0 | 2.66 | 1.93x | 14.0x | 4.15x |
| deltablue | macro | 60.3 | 805.3 | 100.8 | 12.0 | 5.02x | 67.0x | 8.39x |
| havlak | macro | 48.4 | 67.04s | 3.25s | 92.2 | 0.53x | 727x | 35.3x |
| cd | macro | 368.5 | 9.42s | 970.7 | 36.3 | 10.2x | 260x | 26.8x |

## BENG

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 10.4 | 39.1 | 23.4 | 4.19 | 2.47x | 9.33x | 5.59x |
| fannkuch | permutation | 0.614 | 12.4 | 7.15 | 4.00 | 0.15x | 3.11x | 1.79x |
| fasta | generation | 7.00 | 36.9 | 8.77 | 6.15 | 1.14x | 6.00x | 1.43x |
| knucleotide | hashing | 11.5 | 149.4 | --- | 4.93 | 2.33x | 30.3x | --- |
| mandelbrot | numeric | 129.8 | 70.2 | 676.3 | 15.1 | 8.58x | 4.64x | 44.7x |
| nbody | numeric | 80.3 | 364.7 | 149.1 | 7.35 | 10.9x | 49.6x | 20.3x |
| pidigits | bignum | --- | 0.332 | 0.133 | 1.94 | --- | 0.17x | 0.07x |
| regexredux | regex | 1.27 | 15.4 | --- | 2.45 | 0.52x | 6.28x | --- |
| revcomp | string | 1.13 | 40.2 | --- | 3.41 | 0.33x | 11.8x | --- |
| spectralnorm | numeric | 46.9 | 290.6 | 64.8 | 2.76 | 17.0x | 105x | 23.4x |

## KOSTYA

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 456.2 | 977.9 | 885.0 | 33.6 | 13.6x | 29.1x | 26.3x |
| matmul | numeric | 41.6 | 915.6 | 587.1 | 15.7 | 2.65x | 58.4x | 37.4x |
| primes | numeric | 58.9 | 104.8 | 95.1 | 4.46 | 13.2x | 23.5x | 21.3x |
| base64 | string | 307.4 | 746.4 | 159.2 | 17.4 | 17.6x | 42.8x | 9.14x |
| levenshtein | string | 45.3 | 83.8 | 54.5 | 3.92 | 11.6x | 21.4x | 13.9x |
| json_gen | data | 72.0 | 36.2 | 20.2 | 6.16 | 11.7x | 5.88x | 3.27x |
| collatz | numeric | 1.74s | 2.27s | 6.26s | 1.42s | 1.22x | 1.60x | 4.40x |

## LARCENY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 641.6 | 4.01s | 2.21s | 66.9 | 9.59x | 59.9x | 33.0x |
| array1 | array | 4.27 | 27.0 | 36.0 | 1.91 | 2.24x | 14.2x | 18.9x |
| deriv | symbolic | 25.2 | 79.0 | 59.2 | 3.67 | 6.87x | 21.5x | 16.1x |
| diviter | iterative | 3.09s | 10.03s | 27.04s | 471.5 | 6.55x | 21.3x | 57.3x |
| divrec | recursive | 21.1 | 39.7 | 36.5 | 7.81 | 2.71x | 5.08x | 4.68x |
| gcbench | allocation | 381.5 | 920.5 | 555.8 | 23.3 | 16.3x | 39.4x | 23.8x |
| paraffins | combinat | 2.09 | 2.33 | 2.51 | 1.01 | 2.07x | 2.31x | 2.49x |
| pnpoly | numeric | 105.3 | 141.4 | 201.8 | 5.79 | 18.2x | 24.4x | 34.8x |
| primes | iterative | 59.1 | 114.6 | 109.0 | 4.84 | 12.2x | 23.7x | 22.5x |
| puzzle | search | 18.9 | 27.5 | 29.5 | 3.28 | 5.76x | 8.39x | 8.99x |
| quicksort | sorting | 12.7 | 61.2 | 19.2 | 1.65 | 7.74x | 37.1x | 11.7x |
| ray | numeric | 11.3 | 13.7 | 13.8 | 3.58 | 3.15x | 3.83x | 3.85x |

## JetStream

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 79.6 | 217.5 | 131.8 | 5.54 | 14.4x | 39.2x | 23.8x |
| cube3d | 3d | 12.9 | 776.1 | --- | 18.2 | 0.71x | 42.7x | --- |
| navier_stokes | numeric | 963.3 | 5.52s | 788.0 | 37.3 | 25.8x | 148x | 21.1x |
| richards | macro | 201.7 | 215.6 | 25.6 | 4.61 | 43.8x | 46.8x | 5.56x |
| splay | data | 155.9 | 48.2 | 23.5 | 3.54 | 44.1x | 13.6x | 6.65x |
| deltablue | macro | 11.6 | 365.0 | 44.8 | 6.30 | 1.85x | 57.9x | 7.11x |
| hashmap | data | 69.1 | 62.81s | 2.47s | 54.4 | 1.27x | 1154x | 45.5x |
| crypto_sha1 | crypto | 202.8 | 503.8 | 70.1 | 6.93 | 29.3x | 72.7x | 10.1x |
| raytrace3d | 3d | 149.5 | 1.04s | --- | 18.2 | 8.20x | 57.0x | --- |
