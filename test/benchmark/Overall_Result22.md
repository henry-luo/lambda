# Lambda Benchmark Results: Result22

- **Date:** 2026-08-06
- **Platform:** Darwin arm64
- **Lambda commit:** `4babb408a283108a644d1de4fe298bc23fee38f7`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v22-4babb408a2` (21,215,160 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 137.00s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 136.9s (batched 136.1s: sync 97.2s, async 38.8s; non-batched 0.9s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run; suites run in order `r7rs -> awfy -> beng -> kostya -> larceny -> jetstream` with a 10s idle gap between suites
- **Engines in this report:** MIR (untyped), MIR (typed), LambdaJS, QuickJS, Node.js, C2MIR
- **Results source:** `test/benchmark/benchmark_results_v22.json`
- **Separately measured:** C2MIR measured on 2026-08-06, 3 run(s) from `test/benchmark/benchmark_results_v22_c2mir.json`. Toolchain: lambda/mir/c2m, MIR 99c65079038f3ba9242ef646f308c266cfd7a8e5; SHA-256 25bad0d7eeeae440559d1fab44ac55a642a6919cdb8c1fb36fbf6eb74b71a4fb; AC power with low-power mode off.
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed LambdaJS | Timed QuickJS | Timed Node.js | Timed C2MIR | MIR (untyped)/Node geo | MIR (typed)/Node geo | LambdaJS/Node geo | QuickJS/Node geo | C2MIR/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 9 | 10 | 10 | 1.58x | 1.46x | 6.35x | 6.37x | 0.19x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 9 | 2.93x | 2.40x | 24.0x | 5.19x | 0.08x |
| BENG | 8 | 8 | 8 | 8 | 5 | 8 | 7 | 0.66x | 0.62x | 7.22x | 1.91x | 0.12x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 4.47x | 4.00x | 15.8x | 11.8x | 0.22x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 10 | 3.63x | 2.59x | 14.4x | 13.3x | 0.30x |
| JetStream | 6 | 6 | 6 | 6 | 4 | 6 | 1 | 13.4x | 9.51x | 69.3x | 12.8x | 0.18x |
| **Overall** | 56 | 56 | 56 | 56 | 50 | 56 | 44 | 2.75x | 2.27x | 15.3x | 7.23x | 0.17x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 11.2x over 44 of 56 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| larceny/quicksort | 40.8 | 0.200 | 204x |
| r7rs/fft | 5.12 | 0.028 | 182x |
| awfy/permute | 3.09 | 0.026 | 119x |
| awfy/bounce | 2.97 | 0.025 | 119x |
| kostya/base64 | 62.2 | 0.558 | 111x |
| awfy/towers | 2.62 | 0.030 | 87.2x |
| kostya/levenshtein | 46.8 | 0.900 | 52.0x |
| beng/spectralnorm | 17.6 | 0.355 | 49.7x |
| r7rs/nqueens | 5.90 | 0.131 | 45.0x |
| larceny/paraffins | 1.95 | 0.048 | 40.4x |
| awfy/list | 1.02 | 0.026 | 39.5x |
| larceny/pnpoly | 61.1 | 1.93 | 31.6x |

---

## Notable Results

- Missing timings: **18** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- C2MIR missing: awfy/richards (missing_port), awfy/json (missing_port), awfy/deltablue (missing_port), awfy/havlak (missing_port), awfy/cd (missing_port), beng/pidigits (missing_port), larceny/deriv (missing_port), jetstream/cube3d (missing_port), +4 more

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 43.69s | 95.3 | 458x |
| awfy/cd | 9.58s | 36.0 | 266x |
| jetstream/hashmap | 3.17s | 15.2 | 209x |
| jetstream/crypto_sha1 | 1.72s | 8.70 | 198x |
| beng/spectralnorm | 295.3 | 2.55 | 116x |
| awfy/nbody | 583.2 | 5.53 | 105x |
| awfy/deltablue | 966.8 | 12.1 | 80.2x |
| larceny/triangl | 4.19s | 66.2 | 63.3x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.310 | 1.92 | 0.16x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | C2MIR (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 6.13 | 6.12 | 26.5 | 18.9 | 1.78 | 1.10 | 3.45x | 3.44x | 14.9x | 10.6x | 0.62x |
| fibfp | recursive | 8.79 | 7.77 | 26.6 | 18.8 | 1.78 | 1.12 | 4.94x | 4.37x | 15.0x | 10.6x | 0.63x |
| tak | recursive | 0.590 | 1.01 | 2.23 | 2.82 | 0.820 | 0.121 | 0.72x | 1.23x | 2.72x | 3.44x | 0.15x |
| cpstak | closure | 1.18 | 2.01 | 4.44 | 5.67 | 0.987 | 0.234 | 1.20x | 2.03x | 4.50x | 5.74x | 0.24x |
| sum | iterative | 0.838 | 0.838 | 13.2 | 31.5 | 1.20 | 0.270 | 0.70x | 0.70x | 11.0x | 26.1x | 0.22x |
| sumfp | iterative | 0.324 | 0.324 | 1.30 | 3.79 | 0.882 | 0.080 | 0.37x | 0.37x | 1.47x | 4.30x | 0.09x |
| nqueens | backtrack | 1.93 | 5.90 | 21.9 | 7.90 | 1.93 | 0.131 | 1.00x | 3.05x | 11.3x | 4.08x | 0.07x |
| fft | numeric | 2.67 | 5.12 | 11.6 | 2.88 | 1.65 | 0.028 | 1.61x | 3.10x | 7.03x | 1.74x | 0.02x |
| mbrot | numeric | 11.7 | 0.718 | 9.41 | 17.9 | 1.86 | 0.445 | 6.31x | 0.39x | 5.06x | 9.64x | 0.24x |
| ack | recursive | 35.1 | 16.9 | 81.0 | --- | 13.6 | 11.7 | 2.58x | 1.24x | 5.96x | --- | 0.86x |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | C2MIR (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.569 | 0.151 | 0.516 | 0.616 | 0.409 | 0.017 | 1.39x | 0.37x | 1.26x | 1.51x | 0.04x |
| permute | micro | 1.02 | 3.09 | 8.84 | 1.55 | 0.821 | 0.026 | 1.25x | 3.76x | 10.8x | 1.89x | 0.03x |
| queens | micro | 0.668 | 0.641 | 5.84 | 1.06 | 0.639 | 0.021 | 1.04x | 1.00x | 9.14x | 1.67x | 0.03x |
| towers | micro | 1.43 | 2.62 | 24.1 | 2.38 | 1.11 | 0.030 | 1.29x | 2.36x | 21.7x | 2.15x | 0.03x |
| bounce | micro | 0.314 | 2.97 | 4.70 | 0.901 | 0.549 | 0.025 | 0.57x | 5.41x | 8.57x | 1.64x | 0.05x |
| list | micro | 1.00 | 1.02 | 3.20 | 0.930 | 0.516 | 0.026 | 1.94x | 1.98x | 6.20x | 1.80x | 0.05x |
| storage | micro | 0.919 | 1.50 | 14.4 | 2.20 | 0.648 | 0.101 | 1.42x | 2.32x | 22.2x | 3.39x | 0.16x |
| mandelbrot | compute | 50.7 | 50.7 | 340.0 | 883.3 | 31.5 | 31.8 | 1.61x | 1.61x | 10.8x | 28.0x | 1.01x |
| nbody | compute | 173.0 | 20.3 | 583.2 | 161.4 | 5.53 | 1.54 | 31.3x | 3.66x | 105x | 29.2x | 0.28x |
| richards | macro | 2.59s | 256.2 | 1.72s | 193.2 | 47.3 | --- | 54.7x | 5.41x | 36.3x | 4.08x | --- |
| json | macro | 7.77 | 2.60 | 45.4 | 11.1 | 2.84 | --- | 2.73x | 0.91x | 16.0x | 3.91x | --- |
| deltablue | macro | 97.3 | 100.3 | 966.8 | 101.5 | 12.1 | --- | 8.08x | 8.32x | 80.2x | 8.42x | --- |
| havlak | macro | 58.8 | 58.0 | 43.69s | 3.30s | 95.3 | --- | 0.62x | 0.61x | 458x | 34.6x | --- |
| cd | macro | 902.2 | 637.0 | 9.58s | 962.2 | 36.0 | --- | 25.1x | 17.7x | 266x | 26.8x | --- |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | C2MIR (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 10.9 | 10.2 | 42.3 | 23.4 | 4.13 | 2.99 | 2.64x | 2.48x | 10.2x | 5.66x | 0.72x |
| fannkuch | permutation | 0.358 | 0.366 | 12.8 | 7.22 | 4.05 | 0.151 | 0.09x | 0.09x | 3.15x | 1.78x | 0.04x |
| fasta | generation | 1.76 | 2.44 | 27.0 | 8.79 | 6.33 | 0.242 | 0.28x | 0.39x | 4.26x | 1.39x | 0.04x |
| knucleotide | hashing | 4.78 | 5.28 | 150.2 | --- | 5.09 | 0.286 | 0.94x | 1.04x | 29.5x | --- | 0.06x |
| pidigits | bignum | 0.295 | 0.301 | 0.310 | 0.136 | 1.92 | --- | 0.15x | 0.16x | 0.16x | 0.07x | --- |
| regexredux | regex | 1.29 | 1.28 | 17.2 | --- | 2.46 | 1.13 | 0.52x | 0.52x | 6.99x | --- | 0.46x |
| revcomp | string | 1.39 | 1.46 | 46.4 | --- | 3.32 | 0.381 | 0.42x | 0.44x | 14.0x | --- | 0.11x |
| spectralnorm | numeric | 47.7 | 17.6 | 295.3 | 64.7 | 2.55 | 0.355 | 18.7x | 6.91x | 116x | 25.4x | 0.14x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | C2MIR (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 380.2 | 485.4 | 980.4 | 883.1 | 33.8 | 27.6 | 11.3x | 14.4x | 29.0x | 26.1x | 0.82x |
| matmul | numeric | 18.0 | 71.3 | 900.8 | 539.0 | 15.5 | 5.87 | 1.16x | 4.59x | 58.0x | 34.7x | 0.38x |
| primes | numeric | 58.8 | 10.9 | 99.9 | 94.7 | 4.49 | 1.59 | 13.1x | 2.42x | 22.2x | 21.1x | 0.35x |
| base64 | string | 81.5 | 62.2 | 674.5 | 158.1 | 17.2 | 0.558 | 4.73x | 3.61x | 39.1x | 9.17x | 0.03x |
| levenshtein | string | 48.3 | 46.8 | 83.5 | 54.1 | 4.10 | 0.900 | 11.8x | 11.4x | 20.4x | 13.2x | 0.22x |
| json_gen | data | 21.9 | 24.0 | 36.1 | 20.1 | 6.38 | 1.50 | 3.44x | 3.77x | 5.67x | 3.15x | 0.24x |
| collatz | numeric | 1.55s | 936.8 | 2.09s | 6.23s | 1.42s | 210.5 | 1.09x | 0.66x | 1.47x | 4.38x | 0.15x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | C2MIR (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 214.1 | 213.6 | 4.19s | 2.17s | 66.2 | 57.6 | 3.23x | 3.23x | 63.3x | 32.7x | 0.87x |
| array1 | array | 1.16 | 1.17 | 28.8 | 36.2 | 1.89 | 0.316 | 0.61x | 0.62x | 15.2x | 19.1x | 0.17x |
| deriv | symbolic | 32.8 | 16.1 | 94.8 | 59.9 | 3.65 | --- | 9.00x | 4.42x | 26.0x | 16.4x | --- |
| diviter | iterative | 397.3 | 397.9 | 8.25s | 26.80s | 471.5 | 248.5 | 0.84x | 0.84x | 17.5x | 56.8x | 0.53x |
| divrec | recursive | 19.3 | 2.01 | 30.0 | 36.2 | 7.54 | 4.72 | 2.56x | 0.27x | 3.98x | 4.80x | 0.63x |
| gcbench | allocation | 246.1 | 251.9 | 1.36s | 550.1 | 23.3 | 69.9 | 10.6x | 10.8x | 58.4x | 23.6x | 3.00x |
| paraffins | combinat | 2.22 | 1.95 | 2.55 | 2.62 | 0.989 | 0.048 | 2.25x | 1.97x | 2.58x | 2.65x | 0.05x |
| pnpoly | numeric | 110.6 | 61.1 | 123.4 | 202.1 | 5.82 | 1.93 | 19.0x | 10.5x | 21.2x | 34.8x | 0.33x |
| puzzle | search | 14.6 | 14.5 | 26.2 | 29.3 | 3.33 | 1.26 | 4.37x | 4.34x | 7.86x | 8.77x | 0.38x |
| quicksort | sorting | 10.2 | 40.8 | 63.8 | 19.1 | 1.63 | 0.200 | 6.23x | 24.9x | 39.0x | 11.7x | 0.12x |
| ray | numeric | 10.9 | 2.62 | 12.0 | 13.7 | 3.60 | 0.171 | 3.01x | 0.73x | 3.32x | 3.81x | 0.05x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | C2MIR (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 12.4 | 11.4 | 665.5 | --- | 17.7 | --- | 0.70x | 0.65x | 37.7x | --- | --- |
| navier_stokes | numeric | 2.33s | 1.58s | 713.5 | 98.5 | 14.2 | --- | 164x | 111x | 50.2x | 6.93x | --- |
| splay | data | 181.1 | 223.2 | 469.8 | 145.5 | 19.5 | --- | 9.27x | 11.4x | 24.1x | 7.45x | --- |
| hashmap | data | 156.0 | 46.5 | 3.17s | 315.3 | 15.2 | 2.73 | 10.3x | 3.07x | 209x | 20.8x | 0.18x |
| crypto_sha1 | crypto | 271.9 | 291.3 | 1.72s | 218.9 | 8.70 | --- | 31.3x | 33.5x | 198x | 25.2x | --- |
| raytrace3d | 3d | 307.4 | 159.2 | 1.07s | --- | 18.2 | --- | 16.9x | 8.73x | 58.5x | --- | --- |
