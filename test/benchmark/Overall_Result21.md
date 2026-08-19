# Lambda Benchmark Results: Result21

- **Date:** 2026-08-05
- **Platform:** Darwin arm64
- **Lambda commit:** `6fcf2283fa6e09c8cab645d66abfc8b5d1e22989`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v21-6fcf2283fa` (20,520,136 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 156.00s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 155.9s (batched 155.0s: sync 111.2s, async 43.8s; non-batched 0.9s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run; suites run in order `r7rs -> awfy -> beng -> kostya -> larceny -> jetstream` with a 10s idle gap between suites
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v21.json`
- **Separately measured:** C2MIR measured on 2026-08-06, 3 run(s) from `test/benchmark/benchmark_results_v21_c2mir.json`. Toolchain: `lambda/mir/c2m`, MIR `99c65079038f3ba9242ef646f308c266cfd7a8e5`, SHA-256 `25bad0d7eeeae440559d1fab44ac55a642a6919cdb8c1fb36fbf6eb74b71a4fb`; AC power with low-power mode off.
- **Separately measured:** C2MIR measured on 2026-08-19, 3 run(s) from `test/benchmark/run_c2mir_benchmarks.py`. Back-patched 12 rows whose native C ports did not exist during this session (awfy/cd, awfy/deltablue, awfy/havlak, awfy/json, awfy/richards, beng/pidigits, jetstream/crypto_sha1, jetstream/cube3d, jetstream/navier_stokes, jetstream/raytrace3d, jetstream/splay, larceny/deriv); all other C2MIR cells are this session's own. C2MIR measures native C ports through lambda/mir/c2m and does not depend on the Lambda binary; its cells are stable to within a few percent across the v18-v33 sessions.
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 9 | 10 | 2.05x | 2.56x | 0.20x | 6.33x | 6.42x |
| AWFY | 14 | 14 | 13 | 14 | 14 | 14 | 14 | 2.98x | 2.46x | 0.10x | 24.2x | 5.23x |
| BENG | 8 | 8 | 8 | 8 | 8 | 5 | 8 | 0.73x | 0.72x | 0.10x | 7.28x | 1.90x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 5.59x | 5.83x | 0.23x | 16.1x | 11.9x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 4.69x | 3.69x | 0.33x | 14.6x | 13.3x |
| JetStream | 6 | 6 | 4 | 6 | 6 | 4 | 6 | 13.7x | 13.0x | 0.30x | 69.6x | 12.9x |
| **Overall** | 56 | 56 | 53 | 56 | 56 | 50 | 56 | 3.17x | 2.85x | 0.18x | 15.5x | 7.26x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 16.2x over 53 of 56 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| larceny/quicksort | 41.5 | 0.202 | 205x |
| r7rs/fft | 5.00 | 0.025 | 199x |
| awfy/towers | 5.39 | 0.028 | 192x |
| kostya/base64 | 82.1 | 0.564 | 146x |
| awfy/permute | 3.12 | 0.026 | 119x |
| jetstream/crypto_sha1 | 315.3 | 2.70 | 117x |
| awfy/bounce | 2.73 | 0.025 | 110x |
| kostya/levenshtein | 80.4 | 0.914 | 87.9x |
| awfy/deltablue | 100.4 | 1.17 | 85.9x |
| beng/spectralnorm | 25.2 | 0.356 | 70.7x |
| awfy/list | 1.21 | 0.022 | 55.2x |
| r7rs/nqueens | 7.03 | 0.129 | 54.5x |

---

## Notable Results

- Missing timings: **9** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- MIR (typed) missing: awfy/cd (timeout), jetstream/hashmap (exit_-11), jetstream/raytrace3d (exit_-11)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 43.94s | 96.8 | 454x |
| awfy/cd | 9.58s | 35.8 | 267x |
| jetstream/hashmap | 3.24s | 15.3 | 212x |
| jetstream/crypto_sha1 | 1.70s | 8.64 | 197x |
| beng/spectralnorm | 296.0 | 2.59 | 114x |
| awfy/nbody | 576.7 | 5.37 | 107x |
| awfy/deltablue | 954.1 | 11.6 | 82.3x |
| larceny/triangl | 4.17s | 66.5 | 62.7x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.313 | 1.95 | 0.16x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 9.83 | 16.2 | 1.18 | 25.1 | 18.5 | 1.76 | 5.59x | 9.21x | 0.67x | 14.3x | 10.5x |
| fibfp | recursive | 7.33 | 7.22 | 1.14 | 25.2 | 18.7 | 1.80 | 4.06x | 4.01x | 0.63x | 14.0x | 10.4x |
| tak | recursive | 0.903 | 3.05 | 0.122 | 2.15 | 2.80 | 0.795 | 1.14x | 3.83x | 0.15x | 2.70x | 3.52x |
| cpstak | closure | 1.81 | 6.17 | 0.235 | 4.63 | 5.68 | 0.984 | 1.84x | 6.27x | 0.24x | 4.70x | 5.77x |
| sum | iterative | 1.85 | 1.87 | 0.273 | 12.9 | 31.2 | 1.18 | 1.57x | 1.59x | 0.23x | 10.9x | 26.5x |
| sumfp | iterative | 0.318 | 0.318 | 0.081 | 1.29 | 3.69 | 0.878 | 0.36x | 0.36x | 0.09x | 1.47x | 4.20x |
| nqueens | backtrack | 2.14 | 7.03 | 0.129 | 21.6 | 7.98 | 1.77 | 1.21x | 3.98x | 0.07x | 12.2x | 4.52x |
| fft | numeric | 2.63 | 5.00 | 0.025 | 11.4 | 2.74 | 1.60 | 1.64x | 3.12x | 0.02x | 7.14x | 1.71x |
| mbrot | numeric | 11.4 | 0.688 | 0.449 | 9.19 | 17.7 | 1.83 | 6.24x | 0.38x | 0.25x | 5.02x | 9.64x |
| ack | recursive | 51.9 | 66.3 | 11.8 | 78.2 | --- | 13.3 | 3.90x | 4.98x | 0.88x | 5.87x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.627 | 0.341 | 0.017 | 0.528 | 0.615 | 0.385 | 1.63x | 0.89x | 0.04x | 1.37x | 1.60x |
| permute | micro | 0.953 | 3.12 | 0.026 | 8.82 | 1.59 | 0.803 | 1.19x | 3.88x | 0.03x | 11.0x | 1.99x |
| queens | micro | 0.655 | 0.621 | 0.019 | 5.58 | 1.04 | 0.644 | 1.02x | 0.96x | 0.03x | 8.67x | 1.61x |
| towers | micro | 1.40 | 5.39 | 0.028 | 24.2 | 2.29 | 1.13 | 1.25x | 4.79x | 0.02x | 21.5x | 2.04x |
| bounce | micro | 0.307 | 2.73 | 0.025 | 4.62 | 0.868 | 0.552 | 0.56x | 4.94x | 0.05x | 8.36x | 1.57x |
| list | micro | 0.903 | 1.21 | 0.022 | 3.13 | 0.908 | 0.482 | 1.87x | 2.51x | 0.05x | 6.48x | 1.88x |
| storage | micro | 0.945 | 2.10 | 0.096 | 14.3 | 2.15 | 0.644 | 1.47x | 3.26x | 0.15x | 22.2x | 3.33x |
| mandelbrot | compute | 51.2 | 51.4 | 31.1 | 333.9 | 868.9 | 31.1 | 1.65x | 1.65x | 1.00x | 10.7x | 27.9x |
| nbody | compute | 170.4 | 21.4 | 1.51 | 576.7 | 159.9 | 5.37 | 31.7x | 3.98x | 0.28x | 107x | 29.8x |
| richards | macro | 2.59s | 257.2 | 31.2 | 1.66s | 190.7 | 46.6 | 55.7x | 5.52x | 0.67x | 35.8x | 4.10x |
| json | macro | 7.87 | 2.57 | 0.278 | 44.5 | 11.1 | 2.67 | 2.95x | 0.96x | 0.10x | 16.7x | 4.17x |
| deltablue | macro | 95.0 | 100.4 | 1.17 | 954.1 | 100.3 | 11.6 | 8.19x | 8.65x | 0.10x | 82.3x | 8.64x |
| havlak | macro | 61.7 | 61.5 | 1.89 | 43.94s | 3.29s | 96.8 | 0.64x | 0.64x | 0.02x | 454x | 34.0x |
| cd | macro | 933.4 | --- | 15.6 | 9.58s | 963.6 | 35.8 | 26.0x | --- | 0.44x | 267x | 26.9x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 12.4 | 12.4 | 3.01 | 42.1 | 23.3 | 4.09 | 3.04x | 3.04x | 0.74x | 10.3x | 5.69x |
| fannkuch | permutation | 0.627 | 0.626 | 0.152 | 12.6 | 7.18 | 4.02 | 0.16x | 0.16x | 0.04x | 3.15x | 1.79x |
| fasta | generation | 1.77 | 2.42 | 0.247 | 27.1 | 8.87 | 6.13 | 0.29x | 0.40x | 0.04x | 4.41x | 1.45x |
| knucleotide | hashing | 4.68 | 5.17 | 0.289 | 149.0 | --- | 4.88 | 0.96x | 1.06x | 0.06x | 30.5x | --- |
| pidigits | bignum | 0.292 | 0.304 | 0.047 | 0.313 | 0.132 | 1.95 | 0.15x | 0.16x | 0.02x | 0.16x | 0.07x |
| regexredux | regex | 1.27 | 1.30 | 1.15 | 17.2 | --- | 2.44 | 0.52x | 0.53x | 0.47x | 7.06x | --- |
| revcomp | string | 1.39 | 1.47 | 0.385 | 46.5 | --- | 3.32 | 0.42x | 0.44x | 0.12x | 14.0x | --- |
| spectralnorm | numeric | 47.3 | 25.2 | 0.356 | 296.0 | 64.3 | 2.59 | 18.3x | 9.74x | 0.14x | 114x | 24.9x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 334.9 | 358.9 | 28.6 | 997.6 | 884.7 | 33.6 | 9.98x | 10.7x | 0.85x | 29.7x | 26.4x |
| matmul | numeric | 22.3 | 76.0 | 6.15 | 897.8 | 539.1 | 15.4 | 1.45x | 4.94x | 0.40x | 58.3x | 35.0x |
| primes | numeric | 60.7 | 13.0 | 1.62 | 99.9 | 94.9 | 4.43 | 13.7x | 2.93x | 0.37x | 22.5x | 21.4x |
| base64 | string | 81.8 | 82.1 | 0.564 | 676.0 | 157.0 | 17.3 | 4.73x | 4.75x | 0.03x | 39.1x | 9.08x |
| levenshtein | string | 49.1 | 80.4 | 0.914 | 83.5 | 54.0 | 3.92 | 12.5x | 20.5x | 0.23x | 21.3x | 13.8x |
| json_gen | data | 23.0 | 26.9 | 1.53 | 36.2 | 19.8 | 6.29 | 3.65x | 4.27x | 0.24x | 5.75x | 3.15x |
| collatz | numeric | 5.66s | 5.06s | 229.1 | 2.09s | 6.23s | 1.42s | 3.99x | 3.57x | 0.16x | 1.47x | 4.40x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 257.2 | 254.6 | 61.3 | 4.17s | 2.19s | 66.5 | 3.87x | 3.83x | 0.92x | 62.7x | 32.9x |
| array1 | array | 2.15 | 12.7 | 0.322 | 28.6 | 35.9 | 1.92 | 1.12x | 6.63x | 0.17x | 14.9x | 18.7x |
| deriv | symbolic | 33.6 | 16.2 | 2.92 | 94.4 | 59.0 | 3.66 | 9.18x | 4.44x | 0.80x | 25.8x | 16.1x |
| diviter | iterative | 1.07s | 1.12s | 270.9 | 9.29s | 26.68s | 466.3 | 2.29x | 2.40x | 0.58x | 19.9x | 57.2x |
| divrec | recursive | 29.9 | 2.06 | 4.96 | 29.8 | 37.0 | 7.59 | 3.94x | 0.27x | 0.65x | 3.92x | 4.87x |
| gcbench | allocation | 428.0 | 306.2 | 71.6 | 1.36s | 549.5 | 23.4 | 18.3x | 13.1x | 3.06x | 58.1x | 23.5x |
| paraffins | combinat | 2.21 | 1.93 | 0.050 | 2.61 | 2.53 | 0.995 | 2.23x | 1.94x | 0.05x | 2.62x | 2.55x |
| pnpoly | numeric | 113.0 | 63.7 | 1.99 | 128.9 | 201.2 | 5.90 | 19.1x | 10.8x | 0.34x | 21.8x | 34.1x |
| puzzle | search | 14.6 | 15.3 | 1.28 | 26.3 | 29.1 | 3.26 | 4.49x | 4.69x | 0.39x | 8.07x | 8.93x |
| quicksort | sorting | 10.5 | 41.5 | 0.202 | 64.8 | 19.2 | 1.64 | 6.43x | 25.3x | 0.12x | 39.6x | 11.7x |
| ray | numeric | 10.8 | 2.63 | 0.172 | 11.8 | 13.7 | 3.59 | 3.00x | 0.73x | 0.05x | 3.29x | 3.83x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 13.2 | 12.4 | 0.523 | 668.3 | --- | 17.6 | 0.75x | 0.71x | 0.03x | 38.0x | --- |
| navier_stokes | numeric | 1.86s | 1.21s | 48.4 | 716.2 | 98.4 | 14.1 | 132x | 86.1x | 3.44x | 51.0x | 7.00x |
| splay | data | 191.2 | 248.7 | 20.4 | 467.8 | 145.4 | 19.3 | 9.91x | 12.9x | 1.06x | 24.2x | 7.53x |
| hashmap | data | 157.0 | --- | 2.88 | 3.24s | 317.7 | 15.3 | 10.3x | --- | 0.19x | 212x | 20.8x |
| crypto_sha1 | crypto | 300.9 | 315.3 | 2.70 | 1.70s | 218.7 | 8.64 | 34.8x | 36.5x | 0.31x | 197x | 25.3x |
| raytrace3d | 3d | 337.4 | --- | 2.21 | 1.06s | --- | 18.3 | 18.4x | --- | 0.12x | 58.0x | --- |
