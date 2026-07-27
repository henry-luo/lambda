# Result15

- **Date:** 2026-07-27
- **Platform:** Darwin arm64
- **Lambda commit:** `7703c784c3dfe1fcbef538f2d3caaba6166761bd`
- **Lambda build:** clean release build (`make release`)
- **Instrumentation check:** not_recorded
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v15.json`

JetStream JavaScript-engine wrappers are standardized to an explicit x8 loop over the detected benchmark function. They do not use per-file `Benchmark.runIteration()` counts, because those counts drift across JetStream files.

> **Reading this snapshot.** Same machine and engine stack as Result14, so the comparison is apples-to-apples. LambdaJS/Node dedup geo improves 14.9x -> 14.4x while the untouched QuickJS control drifts +1.1% the wrong way. The per-row split is bimodal: recursive-call rows are 1.5-1.7x faster (r7rs/fib 34.3 -> 20.6 ms, fibfp, cpstak, tak, ack, larceny/divrec) from the per-callee call-entry work; five numeric rows regressed (beng/mandelbrot 1.21x, r7rs/sum 1.18x, sumfp 1.17x, larceny/diviter 1.08x, awfy/mandelbrot 1.05x) and were traced to **code/data placement, not semantics** - identical emitted MIR, instruction-identical helpers that merely moved (with their string literals and one hot global page), and no change under `JS_CALL_FORCE_GENERIC=1`. Why placement costs 20% is inferred, not measured; the executed code is provably identical on both sides. All other apparent movers in this snapshot (awfy/sieve, queens, larceny/paraffins, beng/pidigits, and the Node-side drifts) dissolve to ≤2% under interleaved A/B - machine noise; the unchanged Node control drifting +6-8.5% on fasta/havlak/fannkuch bounds this capture's noise floor. Full diagnosis + triage: `vibe/Lambda_Impl_Tune_JS_Dynamic_Call.md` sections 0.1/0.1.1.
>
> The binary is commit `7703c784c`, which bundles the dynamic-call entry work with an unrelated in-flight TDZ fix across four MIR-lowering files; the latter was ruled out as a cause of the numeric rows but means this commit is not a single-change delta.

---

## Summary

| Suite | Total | Timed MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 9 | 10 | 9 | 10 | 1.00x | 5.54x | 6.41x |
| AWFY | 14 | 13 | 14 | 14 | 14 | 1.87x | 21.5x | 5.22x |
| BENG | 10 | 9 | 10 | 7 | 10 | 1.87x | 8.29x | 4.12x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7.39x | 16.3x | 11.9x |
| LARCENY | 12 | 12 | 12 | 12 | 12 | 6.14x | 14.2x | 13.7x |
| JetStream | 9 | 9 | 9 | 7 | 9 | 8.46x | 66.8x | 12.4x |
| **Overall dedup** | **56** | **53** | **56** | **50** | **56** | **2.91x** | **14.4x** | **7.36x** |
| Overall raw | 62 | 59 | 62 | 56 | 62 | 3.20x | 15.6x | 7.95x |

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
| jetstream/hashmap | 64.13s | 56.7 | 1131x |
| awfy/havlak | 96.93s | 104.3 | 929x |
| awfy/cd | 9.77s | 37.4 | 261x |
| jetstream/navier_stokes | 5.12s | 38.4 | 133x |
| beng/spectralnorm | 292.4 | 2.63 | 111x |
| jetstream/crypto_sha1 | 519.0 | 7.11 | 73.0x |
| awfy/deltablue | 820.9 | 11.8 | 69.5x |
| larceny/triangl | 4.05s | 68.5 | 59.1x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.333 | 2.01 | 0.17x |

---

## R7RS

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 6.58 | 20.6 | 19.1 | 1.81 | 3.65x | 11.4x | 10.6x |
| fibfp | recursive | 5.30 | 20.8 | 19.1 | 1.80 | 2.95x | 11.6x | 10.6x |
| tak | recursive | 0.512 | 1.81 | 2.82 | 0.800 | 0.64x | 2.26x | 3.53x |
| cpstak | closure | 1.02 | 3.57 | 5.72 | 1.01 | 1.01x | 3.55x | 5.69x |
| sum | iterative | 4.04 | 14.2 | 32.0 | 1.22 | 3.32x | 11.7x | 26.3x |
| sumfp | iterative | 0.067 | 1.41 | 3.71 | 0.896 | 0.07x | 1.57x | 4.15x |
| nqueens | backtrack | 1.43 | 17.2 | 8.15 | 1.80 | 0.80x | 9.56x | 4.53x |
| fft | numeric | --- | 11.0 | 2.80 | 1.66 | --- | 6.60x | 1.68x |
| mbrot | numeric | 0.809 | 8.43 | 18.1 | 1.87 | 0.43x | 4.52x | 9.69x |
| ack | recursive | 22.5 | 67.6 | --- | 13.7 | 1.65x | 4.94x | --- |

## AWFY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.511 | 0.586 | 0.647 | 0.401 | 1.28x | 1.46x | 1.61x |
| permute | micro | 0.588 | 7.14 | 1.56 | 0.819 | 0.72x | 8.72x | 1.91x |
| queens | micro | 0.507 | 4.57 | 1.06 | 0.654 | 0.78x | 6.99x | 1.62x |
| towers | micro | 0.829 | 16.4 | 2.31 | 1.14 | 0.73x | 14.4x | 2.03x |
| bounce | micro | 0.828 | 3.54 | 0.884 | 0.555 | 1.49x | 6.38x | 1.59x |
| list | micro | --- | 3.41 | 0.915 | 0.506 | --- | 6.73x | 1.81x |
| storage | micro | 0.638 | 13.7 | 2.20 | 0.651 | 0.98x | 21.0x | 3.38x |
| mandelbrot | compute | 46.6 | 398.3 | 897.1 | 32.2 | 1.45x | 12.4x | 27.9x |
| nbody | compute | 83.5 | 284.2 | 164.6 | 5.41 | 15.4x | 52.5x | 30.4x |
| richards | macro | 185.6 | 1.19s | 196.9 | 48.6 | 3.82x | 24.5x | 4.05x |
| json | macro | 5.36 | 38.2 | 11.4 | 2.64 | 2.03x | 14.4x | 4.32x |
| deltablue | macro | 61.9 | 820.9 | 102.8 | 11.8 | 5.24x | 69.5x | 8.70x |
| havlak | macro | 49.9 | 96.93s | 3.41s | 104.3 | 0.48x | 929x | 32.7x |
| cd | macro | 385.6 | 9.77s | 989.7 | 37.4 | 10.3x | 261x | 26.4x |

## BENG

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 10.3 | 33.1 | 24.0 | 4.10 | 2.50x | 8.07x | 5.86x |
| fannkuch | permutation | 0.629 | 12.8 | 7.28 | 4.32 | 0.15x | 2.95x | 1.69x |
| fasta | generation | 7.07 | 37.9 | 9.05 | 6.55 | 1.08x | 5.78x | 1.38x |
| knucleotide | hashing | 11.5 | 152.0 | --- | 4.99 | 2.31x | 30.5x | --- |
| mandelbrot | numeric | 134.5 | 85.4 | 690.7 | 15.7 | 8.58x | 5.45x | 44.1x |
| nbody | numeric | 82.5 | 373.9 | 152.6 | 7.63 | 10.8x | 49.0x | 20.0x |
| pidigits | bignum | --- | 0.333 | 0.135 | 2.01 | --- | 0.17x | 0.07x |
| regexredux | regex | 1.34 | 15.7 | --- | 2.46 | 0.54x | 6.37x | --- |
| revcomp | string | 1.15 | 41.3 | --- | 3.55 | 0.32x | 11.7x | --- |
| spectralnorm | numeric | 48.4 | 292.4 | 65.7 | 2.63 | 18.4x | 111x | 25.0x |

## KOSTYA

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 460.1 | 987.2 | 904.6 | 34.9 | 13.2x | 28.3x | 25.9x |
| matmul | numeric | 42.2 | 885.0 | 551.3 | 16.0 | 2.64x | 55.4x | 34.5x |
| primes | numeric | 60.2 | 104.7 | 97.2 | 4.70 | 12.8x | 22.3x | 20.7x |
| base64 | string | 304.7 | 761.1 | 161.4 | 17.9 | 17.0x | 42.5x | 9.00x |
| levenshtein | string | 45.4 | 85.7 | 55.7 | 4.10 | 11.1x | 20.9x | 13.6x |
| json_gen | data | 74.6 | 37.0 | 20.5 | 6.21 | 12.0x | 5.95x | 3.31x |
| collatz | numeric | 1.73s | 2.38s | 6.37s | 1.45s | 1.19x | 1.64x | 4.40x |

## LARCENY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 652.5 | 4.05s | 2.23s | 68.5 | 9.53x | 59.1x | 32.6x |
| array1 | array | 4.34 | 28.1 | 36.5 | 1.96 | 2.21x | 14.3x | 18.6x |
| deriv | symbolic | 25.5 | 73.4 | 60.2 | 3.70 | 6.91x | 19.9x | 16.3x |
| diviter | iterative | 3.13s | 11.28s | 27.22s | 480.5 | 6.51x | 23.5x | 56.6x |
| divrec | recursive | 20.4 | 24.8 | 36.8 | 7.82 | 2.60x | 3.18x | 4.70x |
| gcbench | allocation | 396.4 | 783.1 | 565.3 | 24.3 | 16.3x | 32.2x | 23.3x |
| paraffins | combinat | 2.25 | 2.55 | 2.54 | 1.02 | 2.21x | 2.52x | 2.50x |
| pnpoly | numeric | 108.0 | 145.4 | 205.7 | 6.13 | 17.6x | 23.7x | 33.6x |
| primes | iterative | 60.0 | 104.3 | 96.9 | 4.53 | 13.2x | 23.0x | 21.4x |
| puzzle | search | 18.8 | 27.8 | 29.8 | 3.35 | 5.61x | 8.28x | 8.88x |
| quicksort | sorting | 12.7 | 61.8 | 19.6 | 1.68 | 7.54x | 36.7x | 11.6x |
| ray | numeric | 11.8 | 14.2 | 14.0 | 3.61 | 3.27x | 3.93x | 3.88x |

## JetStream

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 82.6 | 221.9 | 134.3 | 5.75 | 14.4x | 38.6x | 23.4x |
| cube3d | 3d | 13.5 | 787.6 | --- | 18.4 | 0.73x | 42.7x | --- |
| navier_stokes | numeric | 957.4 | 5.12s | 801.3 | 38.4 | 24.9x | 133x | 20.9x |
| richards | macro | 205.9 | 214.4 | 26.2 | 5.52 | 37.3x | 38.9x | 4.75x |
| splay | data | 160.7 | 49.6 | 24.4 | 4.00 | 40.2x | 12.4x | 6.09x |
| deltablue | macro | 12.2 | 355.7 | 45.9 | 6.62 | 1.84x | 53.8x | 6.94x |
| hashmap | data | 71.9 | 64.13s | 2.58s | 56.7 | 1.27x | 1131x | 45.5x |
| crypto_sha1 | crypto | 209.4 | 519.0 | 71.3 | 7.11 | 29.5x | 73.0x | 10.0x |
| raytrace3d | 3d | 157.0 | 1.07s | --- | 19.2 | 8.20x | 55.9x | --- |
