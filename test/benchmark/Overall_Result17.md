# Lambda Benchmark Results: Result17

- **Date:** 2026-07-29
- **Platform:** Darwin arm64
- **Lambda commit:** `8a56450419ee0298393eeee73fdb07bdbe2118c8`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v17-3ca5bcefe` (19,971,912 bytes)
- **Instrumentation check:** not_recorded
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 120s per run
- **Engines in this report:** MIR (untyped), MIR (typed), LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v17.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 9 | 10 | 1.61x | 1.07x | 5.32x | 6.33x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 2.12x | 1.56x | 18.7x | 4.90x |
| BENG | 10 | 10 | 10 | 10 | 7 | 10 | 1.69x | 1.36x | 7.80x | 4.22x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 5.05x | 4.03x | 16.2x | 12.0x |
| LARCENY | 12 | 12 | 12 | 12 | 12 | 12 | 5.00x | 2.79x | 13.8x | 13.9x |
| JetStream | 9 | 9 | 9 | 9 | 7 | 9 | 7.45x | 5.64x | 64.2x | 14.0x |
| **Overall dedup** | **56** | **56** | **56** | **56** | **50** | **56** | **2.65x** | **1.91x** | **13.5x** | **7.37x** |
| Overall raw | 62 | 62 | 62 | 62 | 56 | 62 | 3.06x | 2.15x | 14.7x | 7.99x |

> **Overall dedup** is the default headline metric: duplicate benchmark names across suites are counted once, using the best timed value per engine. **Overall raw** keeps the row-weighted value for auditability.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Historical Comparison

### What changed in this revision of Result17

This report supersedes the first Result17. The earlier numbers were not comparable across engines; the same archived binary (`lambda-v17-3ca5bcefe`) was used for both, so every difference below is a methodology fix, not a build change.

**Self-timing.** 24 rows (all 10 R7RS and all 14 AWFY untyped Lambda scripts) had no `__TIMING__` line and were timed by wall clock, which charged them ~12 ms of process start, parse and JIT that no other cell paid. All 62 rows now self-report. This is why MIR (untyped) improves so sharply — the old column was mostly harness overhead on the short rows.

**Workload alignment.** The JetStream JS wrappers ran a blanket x8 loop over each benchmark's inner function, discarding the file's own `runIteration()` count: the JS engines were doing 8/50 of Lambda's work on richards and splay, 8/25 on crypto_sha1, 8/20 on deltablue, and 8x too much on navier_stokes and hashmap. Separately, three AWFY untyped scripts ran a fraction of the canonical workload (richards 1 iteration vs 50, deltablue 1 vs 20, nbody 1000 steps vs 36000), which is why untyped awfy/richards previously printed 1.40x against Node and now reports honestly.

**Typed coverage.** 31 benchmarks had no typed source and silently reused the untyped result (marked `*`). All 62 rows now have a real typed script; there are no `*` cells.

**Two typed scripts were broken and being timed anyway.** `jetstream/splay2.ls` had dropped the retained-header borrow that `splay.ls` uses, so its tree collapsed to 1 node instead of 8000 — the old typed figure of 47.6 ms measured almost no work. `jetstream/raytrace3d2.ls` did not finish in 120 s because declared map types on locals made every triangle read a deep COW copy. Both are fixed and pass.

**`beng/fasta` and `beng/revcomp` correctness — FIXED, and both rows re-measured on the same archived binary.** fasta: the JS and Python references carried a 289-character ALU instead of the canonical 287, so they diverged from the Lambda port after the first wrap; the constant is corrected and all engines now agree. Being content-only, its timings barely moved (MIR 7.28 -> 7.12 ms). revcomp: the Lambda scripts printed the complement WITHOUT reversing it, because `fn_reverse` returns text unchanged by design (strings are singular and not iterable; only `reverse(vec)` exists) — and `revcomp.txt` had been generated from that wrong output, so the row passed its own golden while computing something different from Node. Both scripts now reverse explicitly; all three of Lambda-untyped, Lambda-typed and Node match an independently computed reverse-complement, and the goldens were regenerated from that verified output rather than from program output. Doing the real work moved MIR from 1.155 ms to 4.596 ms and typed from 1.178 ms to 4.011 ms; the Node and LambdaJS figures were already correct and did not move.

Measured while an unrelated build was running on the same machine, so absolute values carry more noise than usual; the per-row ratios are the meaningful part.

**Suite cooldown.** `run_benchmarks.py` now idles 10 s between suites (`--cooldown`, 0 disables) and records `suite_cooldown_seconds`, so the methodology line above is generated from what the runner actually did. The timings in this report were taken BEFORE that change, with no gap between suites.

**Headline geometric means, previous revision vs this one (dedup / raw)**

| Engine vs Node.js | Previous Result17 | This revision | Timed rows then → now |
|---|---|---|---|
| MIR (untyped) | 6.46x / 6.57x | **2.59x / 2.99x** | 56 / 62 → 56 / 62 |
| MIR (typed) | 2.30x / 2.58x | **1.87x / 2.11x** | 54 / 60 → 56 / 62 |
| LambdaJS | 13.0x / 14.1x | 13.5x / 14.7x | 56 / 62 → 56 / 62 |
| QuickJS | 7.26x / 7.87x | 7.37x / 7.98x | 50 / 56 → 50 / 56 |

---

## Notable Results

- Missing timings: **6** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- Deduplicated benchmark names: mandelbrot (awfy/beng), nbody (awfy/beng/jetstream), richards (awfy/jetstream), deltablue (awfy/jetstream), primes (kostya/larceny)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 81.29s | 119.0 | 683x |
| awfy/cd | 8.12s | 40.7 | 200x |
| jetstream/hashmap | 3.20s | 17.8 | 180x |
| jetstream/richards | 1.71s | 9.62 | 178x |
| jetstream/crypto_sha1 | 1.78s | 10.2 | 175x |
| beng/spectralnorm | 324.1 | 2.68 | 121x |
| jetstream/deltablue | 910.3 | 12.6 | 72.5x |
| awfy/deltablue | 808.6 | 12.5 | 64.8x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.336 | 2.09 | 0.16x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 6.61 | 7.00 | 21.3 | 19.7 | 1.85 | 3.57x | 3.78x | 11.5x | 10.7x |
| fibfp | recursive | 5.99 | 5.94 | 21.2 | 19.6 | 1.86 | 3.22x | 3.19x | 11.4x | 10.5x |
| tak | recursive | 0.551 | 0.549 | 1.81 | 2.89 | 0.838 | 0.66x | 0.66x | 2.16x | 3.45x |
| cpstak | closure | 1.14 | 1.10 | 3.69 | 5.78 | 1.03 | 1.11x | 1.07x | 3.59x | 5.62x |
| sum | iterative | 4.13 | 4.13 | 12.0 | 32.7 | 1.37 | 3.02x | 3.02x | 8.77x | 23.9x |
| sumfp | iterative | 0.221 | 0.220 | 1.16 | 3.80 | 0.967 | 0.23x | 0.23x | 1.20x | 3.93x |
| nqueens | backtrack | 2.19 | 1.46 | 18.9 | 8.54 | 1.80 | 1.22x | 0.81x | 10.5x | 4.75x |
| fft | numeric | 2.73 | 0.969 | 11.6 | 2.86 | 1.64 | 1.66x | 0.59x | 7.04x | 1.74x |
| mbrot | numeric | 12.6 | 0.839 | 9.27 | 18.9 | 1.97 | 6.40x | 0.43x | 4.71x | 9.62x |
| ack | recursive | 24.1 | 24.1 | 74.4 | --- | 15.1 | 1.59x | 1.59x | 4.92x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.554 | 0.561 | 0.531 | 0.686 | 0.427 | 1.30x | 1.32x | 1.25x | 1.61x |
| permute | micro | 0.804 | 0.507 | 8.03 | 1.69 | 0.906 | 0.89x | 0.56x | 8.86x | 1.87x |
| queens | micro | 0.700 | 0.552 | 4.47 | 1.15 | 1.27 | 0.55x | 0.43x | 3.51x | 0.90x |
| towers | micro | 1.22 | 0.825 | 18.2 | 2.56 | 1.35 | 0.90x | 0.61x | 13.5x | 1.89x |
| bounce | micro | 0.353 | 0.891 | 3.79 | 0.896 | 0.580 | 0.61x | 1.54x | 6.54x | 1.55x |
| list | micro | 0.554 | 0.524 | 3.51 | 0.924 | 0.504 | 1.10x | 1.04x | 6.95x | 1.83x |
| storage | micro | 0.787 | 0.577 | 12.7 | 2.18 | 0.655 | 1.20x | 0.88x | 19.3x | 3.33x |
| mandelbrot | compute | 50.2 | 50.3 | 319.6 | 919.9 | 32.8 | 1.53x | 1.53x | 9.74x | 28.0x |
| nbody | compute | 171.7 | 82.8 | 276.7 | 166.9 | 5.94 | 28.9x | 13.9x | 46.6x | 28.1x |
| richards | macro | 1.52s | 143.7 | 1.24s | 198.9 | 49.0 | 31.0x | 2.93x | 25.2x | 4.06x |
| json | macro | 5.26 | 4.26 | 37.9 | 11.8 | 2.67 | 1.97x | 1.60x | 14.2x | 4.41x |
| deltablue | macro | 55.6 | 56.9 | 808.6 | 103.6 | 12.5 | 4.46x | 4.56x | 64.8x | 8.31x |
| havlak | macro | 48.4 | 48.0 | 81.29s | 3.71s | 119.0 | 0.41x | 0.40x | 683x | 31.1x |
| cd | macro | 674.6 | 398.2 | 8.12s | 1.06s | 40.7 | 16.6x | 9.79x | 200x | 26.0x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 10.1 | 6.66 | 37.0 | 25.5 | 4.21 | 2.40x | 1.58x | 8.78x | 6.06x |
| fannkuch | permutation | 0.675 | 0.679 | 13.6 | 7.78 | 4.22 | 0.16x | 0.16x | 3.24x | 1.84x |
| fasta | generation | 7.12 | 6.70 | 20.6 | 8.88 | 6.20 | 1.15x | 1.08x | 3.33x | 1.43x |
| knucleotide | hashing | 12.4 | 12.2 | 163.2 | --- | 5.43 | 2.29x | 2.24x | 30.1x | --- |
| mandelbrot | numeric | 137.9 | 137.4 | 66.6 | 710.4 | 15.5 | 8.90x | 8.87x | 4.30x | 45.8x |
| nbody | numeric | 86.1 | 46.5 | 402.3 | 160.8 | 8.15 | 10.6x | 5.70x | 49.4x | 19.7x |
| pidigits | bignum | 0.314 | 0.319 | 0.336 | 0.132 | 2.09 | 0.15x | 0.15x | 0.16x | 0.06x |
| regexredux | regex | 1.37 | 1.39 | 16.0 | --- | 2.59 | 0.53x | 0.54x | 6.18x | --- |
| revcomp | string | 4.60 | 4.01 | 39.5 | --- | 3.45 | 1.33x | 1.16x | 11.5x | --- |
| spectralnorm | numeric | 50.0 | 19.1 | 324.1 | 69.5 | 2.68 | 18.6x | 7.12x | 121x | 25.9x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 482.8 | 343.0 | 1.07s | 949.7 | 35.7 | 13.5x | 9.60x | 30.0x | 26.6x |
| matmul | numeric | 45.0 | 44.9 | 993.0 | 566.5 | 16.1 | 2.81x | 2.79x | 61.9x | 35.3x |
| primes | numeric | 57.7 | 59.4 | 108.1 | 102.4 | 4.64 | 12.4x | 12.8x | 23.3x | 22.0x |
| base64 | string | 83.4 | 55.0 | 710.6 | 171.7 | 18.8 | 4.43x | 2.92x | 37.8x | 9.13x |
| levenshtein | string | 48.9 | 35.9 | 89.9 | 58.2 | 4.28 | 11.4x | 8.39x | 21.0x | 13.6x |
| json_gen | data | 18.7 | 18.3 | 37.2 | 21.7 | 6.74 | 2.77x | 2.72x | 5.52x | 3.22x |
| collatz | numeric | 1.85s | 1.11s | 2.26s | 6.44s | 1.46s | 1.27x | 0.76x | 1.55x | 4.42x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 343.8 | 344.7 | 4.40s | 2.37s | 75.6 | 4.55x | 4.56x | 58.1x | 31.3x |
| array1 | array | 4.68 | 4.56 | 30.6 | 39.1 | 2.06 | 2.27x | 2.21x | 14.8x | 18.9x |
| deriv | symbolic | 27.4 | 24.7 | 82.1 | 65.9 | 3.87 | 7.08x | 6.39x | 21.2x | 17.0x |
| diviter | iterative | 1.30s | 1.30s | 10.02s | 31.04s | 521.3 | 2.50x | 2.50x | 19.2x | 59.5x |
| divrec | recursive | 22.3 | 4.32 | 29.3 | 40.7 | 8.50 | 2.63x | 0.51x | 3.44x | 4.79x |
| gcbench | allocation | 251.8 | 331.1 | 966.9 | 747.2 | 30.2 | 8.33x | 11.0x | 32.0x | 24.7x |
| paraffins | combinat | 2.38 | 2.04 | 2.86 | 2.76 | 1.09 | 2.19x | 1.88x | 2.63x | 2.54x |
| pnpoly | numeric | 123.9 | 29.0 | 141.9 | 262.7 | 8.11 | 15.3x | 3.58x | 17.5x | 32.4x |
| primes | iterative | 69.5 | 88.5 | 135.7 | 123.2 | 5.73 | 12.1x | 15.4x | 23.7x | 21.5x |
| puzzle | search | 24.0 | 17.7 | 34.1 | 34.2 | 3.88 | 6.17x | 4.56x | 8.78x | 8.81x |
| quicksort | sorting | 14.8 | 2.30 | 73.9 | 21.8 | 1.91 | 7.75x | 1.21x | 38.7x | 11.4x |
| ray | numeric | 12.2 | 1.67 | 13.2 | 15.5 | 3.91 | 3.13x | 0.43x | 3.37x | 3.97x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 89.8 | 47.5 | 267.2 | 148.9 | 5.92 | 15.2x | 8.03x | 45.2x | 25.2x |
| cube3d | 3d | 15.5 | 14.6 | 727.1 | --- | 21.4 | 0.72x | 0.68x | 33.9x | --- |
| navier_stokes | numeric | 1.14s | 1.05s | 682.3 | 110.0 | 16.0 | 71.2x | 65.5x | 42.6x | 6.87x |
| richards | macro | 186.5 | 102.6 | 1.71s | 179.5 | 9.62 | 19.4x | 10.7x | 178x | 18.7x |
| splay | data | 166.1 | 141.1 | 369.7 | 175.2 | 27.6 | 6.02x | 5.11x | 13.4x | 6.35x |
| deltablue | macro | 12.9 | 10.3 | 910.3 | 133.0 | 12.6 | 1.03x | 0.82x | 72.5x | 10.6x |
| hashmap | data | 84.8 | 68.2 | 3.20s | 360.1 | 17.8 | 4.76x | 3.83x | 180x | 20.2x |
| crypto_sha1 | crypto | 205.0 | 208.0 | 1.78s | 243.6 | 10.2 | 20.1x | 20.4x | 175x | 23.9x |
| raytrace3d | 3d | 171.2 | 100.8 | 1.14s | --- | 21.9 | 7.83x | 4.61x | 52.1x | --- |
