# Lambda Benchmark Results: Result23

- **Date:** 2026-08-06
- **Platform:** Darwin arm64
- **Lambda commit:** `91dbb15dd51bed138c6ae43b03488e10b1a0e711`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v23-91dbb15dd5` (21,215,544 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 134.90s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 134.8s (batched 133.9s: sync 95.2s, async 38.7s; non-batched 0.9s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run; suites run in order `r7rs -> awfy -> beng -> kostya -> larceny -> jetstream` with a 10s idle gap between suites
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v23.json`
- **Separately measured:** C2MIR measured on 2026-08-06, 3 run(s) from `test/benchmark/benchmark_results_v23_c2mir.json`. C2MIR measured separately with the same three-run, 180s, 10s-cooldown matrix.
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 9 | 10 | 1.61x | 1.05x | 0.19x | 6.44x | 6.43x |
| AWFY | 14 | 14 | 14 | 9 | 14 | 14 | 14 | 2.86x | 2.02x | 0.07x | 24.2x | 5.20x |
| BENG | 8 | 8 | 8 | 7 | 8 | 5 | 8 | 0.66x | 0.60x | 0.12x | 7.13x | 1.85x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 4.44x | 3.96x | 0.22x | 15.8x | 11.8x |
| LARCENY | 11 | 11 | 11 | 10 | 11 | 11 | 11 | 3.63x | 2.12x | 0.30x | 14.4x | 13.1x |
| JetStream | 6 | 6 | 6 | 1 | 6 | 4 | 6 | 12.0x | 7.39x | 0.18x | 69.0x | 12.8x |
| **Overall** | 56 | 56 | 56 | 44 | 56 | 50 | 56 | 2.70x | 1.91x | 0.16x | 15.4x | 7.20x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 9.56x over 44 of 56 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| kostya/base64 | 66.8 | 0.554 | 121x |
| awfy/towers | 1.91 | 0.028 | 68.1x |
| awfy/permute | 1.34 | 0.024 | 56.2x |
| kostya/levenshtein | 46.9 | 0.900 | 52.2x |
| beng/spectralnorm | 17.9 | 0.350 | 51.1x |
| awfy/list | 1.00 | 0.021 | 47.7x |
| larceny/paraffins | 1.82 | 0.048 | 37.8x |
| awfy/bounce | 0.815 | 0.023 | 35.5x |
| larceny/pnpoly | 63.2 | 1.92 | 33.0x |
| awfy/queens | 0.520 | 0.018 | 29.2x |
| larceny/quicksort | 5.31 | 0.201 | 26.4x |
| awfy/nbody | 30.4 | 1.50 | 20.4x |

---

## Notable Results

- Missing timings: **18** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- C2MIR missing: awfy/richards (missing_port), awfy/json (missing_port), awfy/deltablue (missing_port), awfy/havlak (missing_port), awfy/cd (missing_port), beng/pidigits (missing_port), larceny/deriv (missing_port), jetstream/cube3d (missing_port), +4 more

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 43.67s | 98.3 | 444x |
| awfy/cd | 9.61s | 35.9 | 268x |
| jetstream/hashmap | 3.20s | 15.4 | 207x |
| jetstream/crypto_sha1 | 1.71s | 8.64 | 198x |
| beng/spectralnorm | 298.1 | 2.57 | 116x |
| awfy/nbody | 577.5 | 5.44 | 106x |
| awfy/deltablue | 954.8 | 11.6 | 82.0x |
| larceny/triangl | 4.16s | 66.9 | 62.1x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.312 | 2.14 | 0.15x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 5.99 | 6.34 | 1.01 | 26.6 | 18.4 | 1.74 | 3.44x | 3.63x | 0.58x | 15.3x | 10.6x |
| fibfp | recursive | 8.83 | 7.70 | 1.12 | 26.8 | 18.6 | 1.75 | 5.03x | 4.39x | 0.64x | 15.3x | 10.6x |
| tak | recursive | 0.582 | 0.928 | 0.119 | 2.19 | 2.85 | 0.784 | 0.74x | 1.18x | 0.15x | 2.79x | 3.63x |
| cpstak | closure | 1.16 | 2.17 | 0.233 | 4.38 | 5.56 | 0.978 | 1.19x | 2.22x | 0.24x | 4.48x | 5.69x |
| sum | iterative | 0.825 | 0.824 | 0.269 | 12.8 | 31.0 | 1.19 | 0.69x | 0.69x | 0.23x | 10.8x | 26.1x |
| sumfp | iterative | 0.318 | 0.319 | 0.079 | 1.28 | 3.70 | 0.876 | 0.36x | 0.36x | 0.09x | 1.45x | 4.22x |
| nqueens | backtrack | 1.92 | 2.14 | 0.125 | 21.6 | 7.80 | 1.75 | 1.10x | 1.23x | 0.07x | 12.4x | 4.46x |
| fft | numeric | 2.64 | 0.406 | 0.024 | 11.4 | 2.71 | 1.61 | 1.64x | 0.25x | 0.01x | 7.09x | 1.69x |
| mbrot | numeric | 11.5 | 0.699 | 0.442 | 9.27 | 17.6 | 1.81 | 6.38x | 0.39x | 0.24x | 5.13x | 9.74x |
| ack | recursive | 34.5 | 17.4 | 11.7 | 80.2 | --- | 13.4 | 2.58x | 1.30x | 0.87x | 5.99x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.608 | 0.120 | 0.015 | 0.566 | 0.613 | 0.383 | 1.59x | 0.31x | 0.04x | 1.48x | 1.60x |
| permute | micro | 0.979 | 1.34 | 0.024 | 8.77 | 1.54 | 0.824 | 1.19x | 1.63x | 0.03x | 10.6x | 1.87x |
| queens | micro | 0.576 | 0.520 | 0.018 | 5.62 | 1.05 | 0.643 | 0.90x | 0.81x | 0.03x | 8.74x | 1.63x |
| towers | micro | 1.37 | 1.91 | 0.028 | 23.9 | 2.25 | 1.15 | 1.19x | 1.66x | 0.02x | 20.8x | 1.96x |
| bounce | micro | 0.276 | 0.815 | 0.023 | 4.65 | 0.873 | 0.546 | 0.51x | 1.49x | 0.04x | 8.52x | 1.60x |
| list | micro | 0.945 | 1.00 | 0.021 | 3.12 | 0.901 | 0.485 | 1.95x | 2.07x | 0.04x | 6.43x | 1.86x |
| storage | micro | 0.819 | 1.38 | 0.076 | 14.1 | 2.16 | 0.638 | 1.28x | 2.17x | 0.12x | 22.1x | 3.39x |
| mandelbrot | compute | 49.9 | 49.8 | 30.4 | 333.5 | 869.0 | 31.0 | 1.61x | 1.60x | 0.98x | 10.7x | 28.0x |
| nbody | compute | 169.6 | 30.4 | 1.50 | 577.5 | 159.0 | 5.44 | 31.2x | 5.59x | 0.27x | 106x | 29.2x |
| richards | macro | 2.58s | 255.7 | --- | 1.67s | 191.7 | 46.6 | 55.4x | 5.49x | --- | 35.9x | 4.12x |
| json | macro | 8.03 | 2.66 | --- | 44.9 | 11.0 | 2.64 | 3.04x | 1.00x | --- | 17.0x | 4.17x |
| deltablue | macro | 95.7 | 100.9 | --- | 954.8 | 100.2 | 11.6 | 8.22x | 8.67x | --- | 82.0x | 8.61x |
| havlak | macro | 53.3 | 53.6 | --- | 43.67s | 3.29s | 98.3 | 0.54x | 0.55x | --- | 444x | 33.5x |
| cd | macro | 916.8 | 641.4 | --- | 9.61s | 962.8 | 35.9 | 25.5x | 17.9x | --- | 268x | 26.8x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 11.2 | 10.2 | 3.03 | 42.7 | 23.2 | 4.19 | 2.67x | 2.44x | 0.72x | 10.2x | 5.55x |
| fannkuch | permutation | 0.361 | 0.361 | 0.149 | 12.8 | 7.17 | 4.06 | 0.09x | 0.09x | 0.04x | 3.15x | 1.77x |
| fasta | generation | 1.76 | 2.02 | 0.240 | 27.2 | 8.81 | 6.18 | 0.29x | 0.33x | 0.04x | 4.40x | 1.43x |
| knucleotide | hashing | 4.85 | 5.26 | 0.276 | 149.7 | --- | 5.40 | 0.90x | 0.97x | 0.05x | 27.7x | --- |
| pidigits | bignum | 0.295 | 0.302 | --- | 0.312 | 0.132 | 2.14 | 0.14x | 0.14x | --- | 0.15x | 0.06x |
| regexredux | regex | 1.30 | 1.33 | 1.13 | 17.9 | --- | 2.45 | 0.53x | 0.54x | 0.46x | 7.29x | --- |
| revcomp | string | 1.40 | 1.50 | 0.377 | 46.6 | --- | 3.38 | 0.41x | 0.44x | 0.11x | 13.8x | --- |
| spectralnorm | numeric | 47.6 | 17.9 | 0.350 | 298.1 | 64.7 | 2.57 | 18.5x | 6.96x | 0.14x | 116x | 25.2x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 376.3 | 487.1 | 27.3 | 990.2 | 887.9 | 33.9 | 11.1x | 14.4x | 0.81x | 29.2x | 26.2x |
| matmul | numeric | 18.2 | 69.7 | 6.02 | 900.6 | 541.8 | 15.4 | 1.18x | 4.52x | 0.39x | 58.4x | 35.1x |
| primes | numeric | 60.5 | 10.7 | 1.58 | 102.5 | 96.1 | 4.47 | 13.5x | 2.39x | 0.35x | 22.9x | 21.5x |
| base64 | string | 82.0 | 66.8 | 0.554 | 699.4 | 169.0 | 19.2 | 4.26x | 3.47x | 0.03x | 36.3x | 8.78x |
| levenshtein | string | 48.7 | 46.9 | 0.900 | 84.0 | 54.8 | 4.07 | 12.0x | 11.5x | 0.22x | 20.6x | 13.5x |
| json_gen | data | 22.0 | 24.0 | 1.51 | 36.8 | 20.1 | 6.32 | 3.48x | 3.80x | 0.24x | 5.82x | 3.17x |
| collatz | numeric | 1.54s | 922.1 | 211.5 | 2.08s | 6.28s | 1.42s | 1.09x | 0.65x | 0.15x | 1.47x | 4.43x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 214.2 | 214.6 | 59.8 | 4.16s | 2.19s | 66.9 | 3.20x | 3.21x | 0.89x | 62.1x | 32.7x |
| array1 | array | 1.16 | 1.16 | 0.316 | 29.3 | 35.8 | 1.91 | 0.60x | 0.61x | 0.17x | 15.3x | 18.7x |
| deriv | symbolic | 34.1 | 16.3 | --- | 95.8 | 58.8 | 3.71 | 9.20x | 4.40x | --- | 25.8x | 15.9x |
| diviter | iterative | 399.3 | 399.3 | 247.4 | 8.30s | 26.69s | 471.3 | 0.85x | 0.85x | 0.52x | 17.6x | 56.6x |
| divrec | recursive | 19.4 | 2.00 | 4.86 | 30.4 | 36.2 | 7.72 | 2.52x | 0.26x | 0.63x | 3.94x | 4.68x |
| gcbench | allocation | 252.1 | 255.2 | 70.7 | 1.37s | 547.0 | 23.4 | 10.8x | 10.9x | 3.02x | 58.6x | 23.3x |
| paraffins | combinat | 2.24 | 1.82 | 0.048 | 2.57 | 2.49 | 0.995 | 2.25x | 1.83x | 0.05x | 2.59x | 2.51x |
| pnpoly | numeric | 110.9 | 63.2 | 1.92 | 124.2 | 201.9 | 5.80 | 19.1x | 10.9x | 0.33x | 21.4x | 34.8x |
| puzzle | search | 14.7 | 14.3 | 1.27 | 26.5 | 29.1 | 3.36 | 4.37x | 4.26x | 0.38x | 7.89x | 8.67x |
| quicksort | sorting | 10.3 | 5.31 | 0.201 | 64.6 | 19.2 | 1.65 | 6.22x | 3.22x | 0.12x | 39.2x | 11.7x |
| ray | numeric | 10.8 | 2.55 | 0.169 | 11.9 | 13.8 | 3.62 | 2.99x | 0.70x | 0.05x | 3.28x | 3.82x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 14.3 | 13.4 | --- | 673.5 | --- | 17.8 | 0.80x | 0.76x | --- | 37.9x | --- |
| navier_stokes | numeric | 1.02s | 288.4 | --- | 709.8 | 98.7 | 14.1 | 72.1x | 20.5x | --- | 50.4x | 7.01x |
| splay | data | 186.7 | 229.9 | --- | 476.3 | 146.1 | 19.8 | 9.45x | 11.6x | --- | 24.1x | 7.39x |
| hashmap | data | 155.4 | 46.8 | 2.78 | 3.20s | 316.2 | 15.4 | 10.1x | 3.03x | 0.18x | 207x | 20.5x |
| crypto_sha1 | crypto | 274.0 | 297.2 | --- | 1.71s | 217.5 | 8.64 | 31.7x | 34.4x | --- | 198x | 25.2x |
| raytrace3d | 3d | 307.9 | 158.0 | --- | 1.04s | --- | 18.2 | 16.9x | 8.67x | --- | 57.0x | --- |
