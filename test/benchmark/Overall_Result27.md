# Lambda Benchmark Results: Result27

- **Date:** 2026-08-08
- **Platform:** Darwin arm64
- **Lambda commit:** `0f65f9d6df0572e8c80fd4914da331c283c58089`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v27-0f65f9d6df` (21,607,880 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 105.00s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 104.9s (batched 103.2s: sync 84.0s, async 19.2s; non-batched 1.7s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v27.json`
- **Separately measured:** C2MIR measured on 2026-08-19, 3 run(s) from `test/benchmark/run_c2mir_benchmarks.py`. Back-patched 12 rows whose native C ports did not exist during this session (awfy/cd, awfy/deltablue, awfy/havlak, awfy/json, awfy/richards, beng/pidigits, jetstream/crypto_sha1, jetstream/cube3d, jetstream/navier_stokes, jetstream/raytrace3d, jetstream/splay, larceny/deriv); all other C2MIR cells are this session's own. C2MIR measures native C ports through lambda/mir/c2m and does not depend on the Lambda binary; its cells are stable to within a few percent across the v18-v33 sessions.
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 9 | 10 | 0.86x | 0.49x | 0.19x | 5.48x | 6.35x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 3.62x | 2.03x | 0.09x | 25.3x | 5.13x |
| BENG | 8 | 8 | 8 | 8 | 8 | 5 | 8 | 0.66x | 0.49x | 0.10x | 7.35x | 1.89x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 3.04x | 2.25x | 0.23x | 15.6x | 11.7x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 2.75x | 1.35x | 0.32x | 13.6x | 13.0x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 4 | 6 | 8.58x | 4.18x | 0.29x | 68.3x | 12.5x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0.93x | 0.73x | 0.02x | 61.9x | 9.19x |
| **Overall** | 59 | 59 | 59 | 59 | 59 | 53 | 59 | 2.14x | 1.26x | 0.15x | 16.1x | 7.23x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 8.26x over 59 of 59 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/towers | 3.30 | 0.028 | 117x |
| awfy/deltablue | 103.0 | 1.17 | 88.2x |
| kostya/base64 | 48.9 | 0.569 | 85.9x |
| awfy/permute | 1.85 | 0.029 | 63.8x |
| text/microdiff | 0.963 | 0.018 | 53.3x |
| text/hyphen | 3.64 | 0.091 | 40.0x |
| text/fast_diff | 521.6 | 13.3 | 39.1x |
| awfy/list | 0.924 | 0.024 | 38.2x |
| larceny/quicksort | 7.45 | 0.200 | 37.3x |
| awfy/cd | 566.2 | 15.6 | 36.3x |
| awfy/bounce | 0.820 | 0.025 | 32.6x |
| awfy/havlak | 56.2 | 1.89 | 29.8x |

---

## Notable Results

- Missing timings: **6** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 47.17s | 103.3 | 456x |
| awfy/cd | 10.47s | 38.2 | 274x |
| jetstream/hashmap | 3.43s | 16.2 | 212x |
| jetstream/crypto_sha1 | 1.78s | 9.04 | 197x |
| beng/spectralnorm | 306.5 | 2.59 | 118x |
| awfy/nbody | 602.8 | 5.56 | 108x |
| text/microdiff | 1.53s | 17.0 | 90.0x |
| awfy/deltablue | 1.05s | 12.8 | 82.2x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.332 | 2.01 | 0.17x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.29 | 1.29 | 1.03 | 21.7 | 18.8 | 1.76 | 0.73x | 0.73x | 0.58x | 12.3x | 10.7x |
| fibfp | recursive | 4.08 | 2.95 | 1.14 | 21.6 | 19.2 | 1.87 | 2.19x | 1.58x | 0.61x | 11.6x | 10.3x |
| tak | recursive | 0.146 | 0.148 | 0.121 | 1.76 | 2.83 | 0.822 | 0.18x | 0.18x | 0.15x | 2.15x | 3.44x |
| cpstak | closure | 0.298 | 0.296 | 0.238 | 3.47 | 5.63 | 1.04 | 0.29x | 0.28x | 0.23x | 3.34x | 5.41x |
| sum | iterative | 0.884 | 0.840 | 0.273 | 10.6 | 32.0 | 1.22 | 0.72x | 0.69x | 0.22x | 8.65x | 26.2x |
| sumfp | iterative | 0.331 | 0.323 | 0.079 | 1.03 | 3.69 | 0.886 | 0.37x | 0.36x | 0.09x | 1.17x | 4.17x |
| nqueens | backtrack | 1.90 | 1.83 | 0.131 | 22.4 | 7.97 | 1.76 | 1.07x | 1.04x | 0.07x | 12.7x | 4.52x |
| fft | numeric | 2.56 | 0.250 | 0.026 | 11.6 | 2.77 | 1.66 | 1.54x | 0.15x | 0.02x | 6.99x | 1.67x |
| mbrot | numeric | 11.5 | 0.648 | 0.450 | 9.20 | 18.0 | 1.81 | 6.33x | 0.36x | 0.25x | 5.08x | 9.93x |
| ack | recursive | 13.5 | 12.4 | 12.0 | 70.7 | --- | 13.6 | 0.99x | 0.91x | 0.88x | 5.20x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.547 | 0.154 | 0.015 | 0.499 | 0.617 | 0.386 | 1.42x | 0.40x | 0.04x | 1.29x | 1.60x |
| permute | micro | 0.920 | 1.85 | 0.029 | 9.99 | 1.55 | 0.838 | 1.10x | 2.21x | 0.03x | 11.9x | 1.85x |
| queens | micro | 0.550 | 0.535 | 0.019 | 6.60 | 1.06 | 0.649 | 0.85x | 0.82x | 0.03x | 10.2x | 1.64x |
| towers | micro | 1.40 | 3.30 | 0.028 | 27.1 | 2.29 | 1.16 | 1.21x | 2.85x | 0.02x | 23.4x | 1.97x |
| bounce | micro | 0.278 | 0.820 | 0.025 | 5.32 | 0.878 | 0.560 | 0.50x | 1.46x | 0.04x | 9.49x | 1.57x |
| list | micro | 0.872 | 0.924 | 0.024 | 3.46 | 0.923 | 0.498 | 1.75x | 1.86x | 0.05x | 6.95x | 1.85x |
| storage | micro | 0.805 | 1.04 | 0.099 | 15.7 | 2.22 | 0.649 | 1.24x | 1.61x | 0.15x | 24.2x | 3.43x |
| mandelbrot | compute | 49.0 | 49.0 | 31.3 | 298.3 | 888.6 | 32.1 | 1.53x | 1.53x | 0.97x | 9.29x | 27.7x |
| nbody | compute | 34.5 | 22.3 | 1.55 | 602.8 | 162.0 | 5.56 | 6.21x | 4.02x | 0.28x | 108x | 29.1x |
| richards | macro | 2.60s | 262.4 | 31.2 | 1.87s | 194.0 | 48.0 | 54.1x | 5.47x | 0.65x | 39.0x | 4.04x |
| json | macro | 7.78 | 2.72 | 0.278 | 48.9 | 11.1 | 2.66 | 2.92x | 1.02x | 0.10x | 18.4x | 4.17x |
| deltablue | macro | 106.3 | 103.0 | 1.17 | 1.05s | 101.1 | 12.8 | 8.34x | 8.08x | 0.09x | 82.2x | 7.93x |
| havlak | macro | 12.76s | 56.2 | 1.89 | 47.17s | 3.40s | 103.3 | 123x | 0.54x | 0.02x | 456x | 32.9x |
| cd | macro | 970.1 | 566.2 | 15.6 | 10.47s | 980.1 | 38.2 | 25.4x | 14.8x | 0.41x | 274x | 25.6x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 10.6 | 5.30 | 3.10 | 41.3 | 23.6 | 4.26 | 2.48x | 1.25x | 0.73x | 9.69x | 5.54x |
| fannkuch | permutation | 0.909 | 2.29 | 0.152 | 13.3 | 7.29 | 4.14 | 0.22x | 0.55x | 0.04x | 3.22x | 1.76x |
| fasta | generation | 0.790 | 1.29 | 0.247 | 27.0 | 8.84 | 5.97 | 0.13x | 0.22x | 0.04x | 4.52x | 1.48x |
| knucleotide | hashing | 4.31 | 4.73 | 0.291 | 156.3 | --- | 5.04 | 0.86x | 0.94x | 0.06x | 31.0x | --- |
| pidigits | bignum | 0.303 | 0.308 | 0.047 | 0.332 | 0.132 | 2.01 | 0.15x | 0.15x | 0.02x | 0.17x | 0.07x |
| regexredux | regex | 1.36 | 1.34 | 1.16 | 18.1 | --- | 2.50 | 0.54x | 0.54x | 0.46x | 7.26x | --- |
| revcomp | string | 1.44 | 1.25 | 0.383 | 49.6 | --- | 3.62 | 0.40x | 0.34x | 0.11x | 13.7x | --- |
| spectralnorm | numeric | 45.7 | 2.06 | 0.360 | 306.5 | 66.2 | 2.59 | 17.6x | 0.79x | 0.14x | 118x | 25.5x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 327.8 | 427.2 | 28.9 | 971.1 | 899.3 | 34.7 | 9.45x | 12.3x | 0.83x | 28.0x | 25.9x |
| matmul | numeric | 13.0 | 11.7 | 6.21 | 949.0 | 549.1 | 16.2 | 0.80x | 0.72x | 0.38x | 58.7x | 33.9x |
| primes | numeric | 61.2 | 25.4 | 1.62 | 103.1 | 96.4 | 4.53 | 13.5x | 5.61x | 0.36x | 22.7x | 21.3x |
| base64 | string | 52.3 | 48.9 | 0.569 | 698.1 | 160.9 | 18.2 | 2.86x | 2.68x | 0.03x | 38.3x | 8.82x |
| levenshtein | string | 36.0 | 9.44 | 0.913 | 84.5 | 55.1 | 4.39 | 8.21x | 2.15x | 0.21x | 19.3x | 12.6x |
| json_gen | data | 20.9 | 22.0 | 1.58 | 36.6 | 20.3 | 6.32 | 3.30x | 3.48x | 0.25x | 5.78x | 3.21x |
| collatz | numeric | 432.9 | 420.1 | 229.5 | 2.01s | 6.34s | 1.44s | 0.30x | 0.29x | 0.16x | 1.39x | 4.39x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 244.1 | 207.8 | 62.2 | 4.43s | 2.23s | 68.2 | 3.58x | 3.05x | 0.91x | 65.0x | 32.7x |
| array1 | array | 1.13 | 1.14 | 0.324 | 28.0 | 36.8 | 1.94 | 0.58x | 0.59x | 0.17x | 14.4x | 18.9x |
| deriv | symbolic | 35.0 | 12.5 | 2.92 | 92.8 | 60.8 | 4.10 | 8.55x | 3.05x | 0.71x | 22.6x | 14.8x |
| diviter | iterative | 406.7 | 406.1 | 270.6 | 8.10s | 27.28s | 481.5 | 0.84x | 0.84x | 0.56x | 16.8x | 56.7x |
| divrec | recursive | 16.4 | 2.03 | 5.00 | 26.6 | 37.6 | 7.70 | 2.13x | 0.26x | 0.65x | 3.45x | 4.89x |
| gcbench | allocation | 246.1 | 164.9 | 72.6 | 1.34s | 566.4 | 25.2 | 9.75x | 6.54x | 2.88x | 53.2x | 22.5x |
| paraffins | combinat | 2.06 | 0.916 | 0.049 | 2.69 | 2.55 | 1.01 | 2.03x | 0.90x | 0.05x | 2.66x | 2.52x |
| pnpoly | numeric | 16.5 | 17.1 | 2.00 | 112.8 | 206.9 | 5.91 | 2.79x | 2.90x | 0.34x | 19.1x | 35.0x |
| puzzle | search | 8.54 | 4.83 | 1.29 | 25.5 | 29.9 | 3.36 | 2.54x | 1.43x | 0.38x | 7.58x | 8.88x |
| quicksort | sorting | 10.5 | 7.45 | 0.200 | 71.7 | 20.2 | 1.90 | 5.51x | 3.92x | 0.11x | 37.7x | 10.6x |
| ray | numeric | 10.9 | 0.909 | 0.174 | 11.9 | 14.7 | 3.97 | 2.74x | 0.23x | 0.04x | 2.99x | 3.72x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 12.4 | 12.1 | 0.523 | 680.7 | --- | 18.6 | 0.67x | 0.65x | 0.03x | 36.5x | --- |
| navier_stokes | numeric | 1.03s | 195.1 | 48.4 | 722.1 | 100.4 | 14.7 | 70.3x | 13.3x | 3.29x | 49.1x | 6.83x |
| splay | data | 147.2 | 267.5 | 20.4 | 487.1 | 150.1 | 20.8 | 7.08x | 12.9x | 0.98x | 23.4x | 7.22x |
| hashmap | data | 169.7 | 60.1 | 2.91 | 3.43s | 319.7 | 16.2 | 10.5x | 3.72x | 0.18x | 212x | 19.8x |
| crypto_sha1 | crypto | 67.8 | 35.5 | 2.70 | 1.78s | 224.6 | 9.04 | 7.51x | 3.93x | 0.30x | 197x | 24.8x |
| raytrace3d | 3d | 293.7 | 63.5 | 2.21 | 1.11s | --- | 19.3 | 15.2x | 3.30x | 0.11x | 57.8x | --- |

## Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 1.41s | 521.6 | 13.3 | 2.33s | 625.0 | 41.5 | 33.9x | 12.6x | 0.32x | 56.0x | 15.1x |
| microdiff | data-diff | 0.999 | 0.963 | 0.018 | 1.53s | 110.8 | 17.0 | 0.06x | 0.06x | 0.001x | 90.0x | 6.51x |
| hyphen | hyphenation | 2.69 | 3.64 | 0.091 | 313.9 | 53.0 | 6.68 | 0.40x | 0.54x | 0.01x | 47.0x | 7.93x |

---

## Analysis: Result27 vs Tune17 (2026-08-09)

Result27 is the measurement of the Tune17 implementation (`vibe/Lambda_Impl_Tune17.md`,
commit `0f65f9d6df`). The headline is flat, and the movement underneath is large but
net-zero. All row deltas below are stated as ratio-to-Node change, so host drift between
runs is factored out.

**Aggregates (v26 → v27):** untyped/Node geo 2.25x → **2.14x**; typed/Node geo
1.26x → **1.26x (unchanged)**; typed/C2MIR **6.70x → 7.13x (worse)**; LambdaJS
16.0x → 16.1x. Against the Tune17 §4 round targets: untyped ≤1.7x missed, typed ≤1.0x
missed, typed/C2MIR ≤5x missed and moved backwards. The only target met is the T3 sha1
evidence gate (≤8x → achieved 3.93x).

### Regression ledger vs Result26

**One catastrophic untyped regression:**

| Row | v26 | v27 | Change |
|---|---:|---:|---|
| awfy/havlak **untyped** | 62.9 ms (0.43x) | **12.76 s (123x)** | ~200x slower |

Typed havlak is fine (56.2 ms, faster than v26), so one lane diverged ~227x from the
other on the same program. havlak untyped was ~40–63 ms in R18, R26, and in the typed
column of every round — this is a v27-specific collapse of the inferred lane, the prime
T1 lane-unification fallout suspect (an inferred-edge path deopting to the boxed lane
inside the CFG loops). It single-handedly drags AWFY untyped geo from 2.43x to 3.62x.

**New typed regressions — annotated micro rows that were healthy in v26:**

| Row (typed) | v26 | v27 | Change |
|---|---:|---:|---|
| awfy/permute | 0.152 ms (0.18x) | 1.85 ms (2.21x) | **12x worse** |
| awfy/towers | 0.700 ms (0.62x) | 3.30 ms (2.84x) | **4.7x worse** — now the #1 widest C2MIR gap (117x) |
| awfy/storage | 0.473 ms (0.75x) | 1.04 ms (1.60x) | 2.2x worse |
| beng/fannkuch | 1.87 ms | 2.29 ms | +25% (untyped also +155%) |
| larceny/quicksort | 5.64 ms | 7.45 ms | +17% |
| jetstream/splay | 290.8 ms | 267.5 ms raw | +10% vs Node (untyped +22%) |

These are precisely the shapes Tune17 T1.2 rerouted ("declared contracts route through
the inference specializer"). In v26, permute and towers typed *beat* their untyped
columns; after the reroute they are 2–3.5x *worse* than untyped. The R3 inversion was
not closed — it moved: pnpoly/nbody left the inversion ledger, permute/towers/storage
entered it.

**Delivered by Tune17 (for fairness):** the R1 recursion family is fully recovered —
tak/cpstak/fib/ack untyped all −68% to −84%, back to parity with typed (T1.1). sha1
typed 235 → 35.5 ms (24.9x → 3.93x Node, the round's biggest win; T3). pnpoly typed
53.8 → 17.1, nbody typed 39.5 → 22.3 (typed finally beats untyped), gcbench −39%,
binarytrees −44%, deriv −37%, collatz untyped −64%, crypto_sha1 untyped −72%.

### Regression ledger vs Result18 (pre-enforcement)

Aggregate typed is well ahead of R18 (1.87x → 1.26x), but 13 typed rows remain >25%
above the R18 line, and the worst offenders **got worse this round**: towers **+318%**,
permute **+277%**, fannkuch +264%, quicksort +230%, splay +114%, list +94%,
richards +82%, deltablue +74%, storage +73%, cd +39%, brainfuck +33%, nqueens +31% —
plus the havlak untyped blowup, which is off any chart. towers, permute, storage, and
quicksort are regressions introduced *in v27 itself*: on these shapes the distance to
R18 is growing, not shrinking, three tuning rounds after the enforcement step.

### Why the typed column has stalled

The typed geo over the last three rounds is 1.32x → 1.26x → 1.26x, while typed/C2MIR
went 7.14x → 6.70x → 7.13x — a round trip. Four structural reasons, all visible in this
data:

1. **The rounds are zero-sum lane rebalancing, not cost deletion.** Each round's
   mechanism fixes one lane's rows and breaks the other lane's rows: Tune16's C0.C
   annotations created the R2 family (fannkuch/primes/sieve); Tune17's T1 fixed R1/R3
   and created permute/towers/storage. Gross movement in v27 is huge (sha1 −84%,
   pnpoly −68% against permute +1097%, towers +359%) and the geomean nets to exactly
   zero. The "same facts ⇒ same code" invariant (Tune17 §2, per D3.2.1, D2.4) is only
   enforced on the fixture shapes (tak and pnpoly carry emission-identity fixtures);
   every shape without a fixture is free to re-diverge, and did. Until emission identity
   is asserted as a broad ratchet rather than per-specimen, the pattern will repeat.

2. **The residual typed gap is representational, and check tuning cannot reach it.**
   The v27 widest-gap list is made of: towers/list/quicksort (record and list
   allocation + dynamic ShapeEntry lookup), base64 85.9x (no binary/byte lane),
   microdiff/hyphen/fast_diff 39–53x (per-char string building), gcbench 6.5x Node
   (allocator). Tune17's own T4 evidence gate concluded the dominant macro-row family
   is dynamic map/ShapeEntry lookup with no safe universal COW or fixed-shape
   invalidation proof — and correctly shipped nothing speculative. The honest reading
   of the Tune17 status note: **the safe admission/elision slices are exhausted.**
   What remains needs value-model design work — the S1.4–S1.6 binary lane, record
   lowering to fixed shapes, and the LambdaJS NameId path (D8.4.1 permits ICs there) —
   not another elision pass.

3. **Enforcement debt is still being amortized.** The R18→R20 type-enforcement round
   (bisected to `274625d56`: unconditional `emit_checked_boundary` + ANY downgrade)
   imposed a step cost exactly on annotated boundaries. Tune13–17 have mostly been
   buying that back — which is why "progress" looks like recovery toward the R18 line
   rather than advance past it, and why 13 rows still have not recovered.

4. **Micro-row structure makes annotations binary: free or fatal.** C2MIR runs towers
   in 28 µs and permute in 29 µs. At that scale a single boxed adapter, one boundary
   admission, or one root-frame setup per call *is* the entire 30–100x gap. There is no
   incremental tuning of these rows — either the annotated emission is
   instruction-identical to the best inferred emission, or the row is lost. That is a
   compiler-architecture property (T1.3's single decision point, evidently not yet
   actually singular), not a knob.

### Leads for the next round

In order: (a) root-cause **havlak untyped** — MIR-diff v26 vs v27; it is the largest
single regression in any round and the prime T1 fallout suspect; (b) MIR-diff
**permute/towers typed** v26 vs v27 to name what T1.2's reroute changed in the
declared-lane emission; (c) accept that reaching typed/C2MIR ≤5x now runs through the
gated design items — record fixed-shape representation and the binary lane — rather
than a Tune18 of the same shape as Tune15–17.
