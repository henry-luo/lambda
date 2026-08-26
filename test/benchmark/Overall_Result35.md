# Result35

- **Date:** 2026-08-26
- **Platform:** Darwin arm64
- **Lambda commit:** `fe6c8e14c1`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v35-fe6c8e14c1` (19,817,976 bytes)
- **Instrumentation check:** not_recorded
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run; suites run in order `r7rs -> awfy -> beng -> kostya -> larceny -> jetstream -> text` with a 10s idle gap between suites
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v35.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 9 | 10 | 0.49x | 0.38x | 0.19x | 11.9x | 6.49x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.51x | 0.81x | 0.10x | 43.0x | 5.23x |
| BENG | 8 | 8 | 8 | 8 | 8 | 5 | 8 | 0.52x | 0.36x | 0.10x | 12.1x | 1.89x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 2.05x | 1.27x | 0.23x | 54.9x | 12.0x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 2.24x | 0.83x | 0.33x | 32.3x | 13.2x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 4 | 6 | 5.40x | 3.12x | 0.29x | 76.1x | 13.0x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0.29x | 0.22x | 0.02x | 66.7x | 9.40x |
| **Overall** | 59 | 59 | 59 | 59 | 59 | 53 | 59 | 1.26x | 0.73x | 0.16x | 30.8x | 7.37x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 4.67x over 59 of 59 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/deltablue | 69.4 | 1.15 | 60.6x |
| jetstream/cube3d | 11.4 | 0.516 | 22.1x |
| text/hyphen | 1.94 | 0.091 | 21.3x |
| jetstream/raytrace3d | 46.6 | 2.20 | 21.2x |
| awfy/havlak | 38.3 | 1.83 | 20.9x |
| kostya/base64 | 11.4 | 0.558 | 20.5x |
| awfy/towers | 0.464 | 0.027 | 17.1x |
| beng/knucleotide | 4.76 | 0.289 | 16.5x |
| jetstream/hashmap | 43.4 | 2.72 | 16.0x |
| awfy/queens | 0.314 | 0.022 | 14.3x |
| kostya/json_gen | 21.7 | 1.53 | 14.2x |
| awfy/nbody | 20.2 | 1.50 | 13.5x |

---

### Notable Results

- Missing timings: **6** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 106.97s | 97.6 | 1096x |
| kostya/primes | 3.84s | 4.40 | 872x |
| awfy/cd | 28.11s | 36.0 | 780x |
| jetstream/hashmap | 4.97s | 15.3 | 324x |
| larceny/quicksort | 402.6 | 1.64 | 245x |
| awfy/nbody | 998.5 | 5.25 | 190x |
| awfy/deltablue | 2.00s | 11.7 | 172x |
| larceny/gcbench | 3.95s | 23.5 | 168x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.418 | 1.96 | 0.21x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.30 | 1.27 | 1.14 | 40.3 | 18.7 | 1.74 | 0.75x | 0.73x | 0.66x | 23.1x | 10.7x |
| fibfp | recursive | 2.05 | 1.19 | 1.12 | 40.9 | 18.4 | 1.78 | 1.15x | 0.67x | 0.63x | 23.0x | 10.4x |
| tak | recursive | 0.126 | 0.158 | 0.109 | 2.56 | 2.79 | 0.790 | 0.16x | 0.20x | 0.14x | 3.24x | 3.54x |
| cpstak | closure | 0.243 | 0.327 | 0.232 | 5.10 | 5.55 | 0.963 | 0.25x | 0.34x | 0.24x | 5.30x | 5.76x |
| sum | iterative | 0.824 | 0.824 | 0.271 | 23.9 | 31.0 | 1.17 | 0.70x | 0.70x | 0.23x | 20.4x | 26.5x |
| sumfp | iterative | 0.069 | 0.068 | 0.082 | 2.35 | 3.67 | 0.847 | 0.08x | 0.08x | 0.10x | 2.77x | 4.33x |
| nqueens | backtrack | 1.50 | 1.14 | 0.128 | 39.6 | 7.91 | 1.70 | 0.88x | 0.67x | 0.08x | 23.2x | 4.65x |
| fft | numeric | 0.123 | 0.243 | 0.024 | 54.0 | 2.72 | 1.58 | 0.08x | 0.15x | 0.02x | 34.1x | 1.72x |
| mbrot | numeric | 10.6 | 0.535 | 0.445 | 15.3 | 17.7 | 1.80 | 5.87x | 0.30x | 0.25x | 8.48x | 9.86x |
| ack | recursive | 12.6 | 14.3 | 11.8 | 218.2 | --- | 13.4 | 0.94x | 1.07x | 0.88x | 16.3x | --- |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.036 | 0.035 | 0.016 | 10.2 | 0.604 | 0.378 | 0.10x | 0.09x | 0.04x | 27.0x | 1.60x |
| permute | micro | 0.559 | 0.130 | 0.026 | 11.3 | 1.53 | 0.820 | 0.68x | 0.16x | 0.03x | 13.7x | 1.87x |
| queens | micro | 0.376 | 0.314 | 0.022 | 7.03 | 1.05 | 0.633 | 0.59x | 0.50x | 0.03x | 11.1x | 1.65x |
| towers | micro | 0.984 | 0.464 | 0.027 | 31.1 | 2.27 | 1.10 | 0.89x | 0.42x | 0.02x | 28.1x | 2.05x |
| bounce | micro | 0.069 | 0.106 | 0.025 | 7.41 | 0.867 | 0.539 | 0.13x | 0.20x | 0.05x | 13.7x | 1.61x |
| list | micro | 0.765 | 0.224 | 0.023 | 3.33 | 0.906 | 0.501 | 1.53x | 0.45x | 0.05x | 6.63x | 1.81x |
| storage | micro | 0.733 | 0.537 | 0.096 | 7.32 | 2.14 | 0.627 | 1.17x | 0.86x | 0.15x | 11.7x | 3.42x |
| mandelbrot | compute | 38.2 | 38.2 | 30.6 | 442.1 | 873.0 | 31.1 | 1.23x | 1.23x | 0.98x | 14.2x | 28.1x |
| nbody | compute | 32.2 | 20.2 | 1.50 | 998.5 | 159.0 | 5.25 | 6.14x | 3.85x | 0.29x | 190x | 30.3x |
| richards | macro | 2.07s | 208.3 | 29.2 | 1.84s | 190.6 | 46.4 | 44.7x | 4.49x | 0.63x | 39.6x | 4.11x |
| json | macro | 6.64 | 2.30 | 0.262 | 98.6 | 10.9 | 2.60 | 2.55x | 0.88x | 0.10x | 37.9x | 4.20x |
| deltablue | macro | 73.1 | 69.4 | 1.15 | 2.00s | 100.2 | 11.7 | 6.25x | 5.94x | 0.10x | 172x | 8.58x |
| havlak | macro | 37.6 | 38.3 | 1.83 | 106.97s | 3.28s | 97.6 | 0.39x | 0.39x | 0.02x | 1096x | 33.6x |
| cd | macro | 743.5 | 197.4 | 15.0 | 28.11s | 960.3 | 36.0 | 20.6x | 5.48x | 0.42x | 780x | 26.7x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.05 | 4.40 | 3.00 | 161.3 | 23.5 | 4.14 | 2.18x | 1.06x | 0.73x | 38.9x | 5.68x |
| fannkuch | permutation | 0.329 | 0.406 | 0.150 | 61.1 | 7.16 | 3.99 | 0.08x | 0.10x | 0.04x | 15.3x | 1.79x |
| fasta | generation | 0.778 | 0.880 | 0.244 | 26.5 | 8.88 | 6.18 | 0.13x | 0.14x | 0.04x | 4.29x | 1.44x |
| knucleotide | hashing | 4.25 | 4.76 | 0.289 | 162.6 | --- | 4.92 | 0.86x | 0.97x | 0.06x | 33.0x | --- |
| pidigits | bignum | 0.315 | 0.300 | 0.046 | 0.418 | 0.133 | 1.96 | 0.16x | 0.15x | 0.02x | 0.21x | 0.07x |
| regexredux | regex | 1.28 | 1.27 | 1.14 | 51.9 | --- | 2.41 | 0.53x | 0.53x | 0.48x | 21.6x | --- |
| revcomp | string | 1.25 | 1.19 | 0.373 | 32.0 | --- | 3.41 | 0.37x | 0.35x | 0.11x | 9.37x | --- |
| spectralnorm | numeric | 21.6 | 1.59 | 0.356 | 320.8 | 63.5 | 2.61 | 8.27x | 0.61x | 0.14x | 123x | 24.3x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 309.1 | 326.1 | 28.5 | 3.72s | 884.5 | 33.3 | 9.28x | 9.79x | 0.86x | 112x | 26.6x |
| matmul | numeric | 11.8 | 11.4 | 6.05 | 1.28s | 539.5 | 15.4 | 0.77x | 0.74x | 0.39x | 83.0x | 35.0x |
| primes | numeric | 18.7 | 3.44 | 1.61 | 3.84s | 94.9 | 4.40 | 4.26x | 0.78x | 0.36x | 872x | 21.6x |
| base64 | string | 11.3 | 11.4 | 0.558 | 867.8 | 157.7 | 17.2 | 0.65x | 0.66x | 0.03x | 50.3x | 9.15x |
| levenshtein | string | 35.8 | 6.81 | 0.905 | 430.8 | 54.3 | 3.97 | 9.02x | 1.71x | 0.23x | 108x | 13.7x |
| json_gen | data | 20.7 | 21.7 | 1.53 | 53.0 | 20.0 | 6.16 | 3.36x | 3.52x | 0.25x | 8.60x | 3.24x |
| collatz | numeric | 359.5 | 343.8 | 225.6 | 5.61s | 6.24s | 1.42s | 0.25x | 0.24x | 0.16x | 3.95x | 4.39x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 490.4 | 210.7 | 60.4 | 11.12s | 2.18s | 66.8 | 7.34x | 3.15x | 0.90x | 166x | 32.7x |
| array1 | array | 0.819 | 0.819 | 0.319 | 79.2 | 36.1 | 1.90 | 0.43x | 0.43x | 0.17x | 41.8x | 19.0x |
| deriv | symbolic | 29.2 | 9.97 | 2.82 | 340.3 | 59.5 | 3.83 | 7.62x | 2.60x | 0.74x | 88.9x | 15.5x |
| diviter | iterative | 260.7 | 266.9 | 264.6 | 9.60s | 26.73s | 469.3 | 0.56x | 0.57x | 0.56x | 20.5x | 56.9x |
| divrec | recursive | 15.3 | 2.00 | 4.97 | 45.4 | 36.2 | 7.72 | 1.99x | 0.26x | 0.64x | 5.88x | 4.69x |
| gcbench | allocation | 211.9 | 135.6 | 70.8 | 3.95s | 547.0 | 23.5 | 9.02x | 5.77x | 3.01x | 168x | 23.3x |
| paraffins | combinat | 0.322 | 0.274 | 0.052 | 4.13 | 2.50 | 0.990 | 0.33x | 0.28x | 0.05x | 4.17x | 2.53x |
| pnpoly | numeric | 13.3 | 15.1 | 1.95 | 110.8 | 202.1 | 5.81 | 2.29x | 2.60x | 0.34x | 19.1x | 34.8x |
| puzzle | search | 8.98 | 4.02 | 1.27 | 109.7 | 29.3 | 3.41 | 2.64x | 1.18x | 0.37x | 32.2x | 8.60x |
| quicksort | sorting | 10.1 | 1.01 | 0.199 | 402.6 | 19.1 | 1.64 | 6.13x | 0.61x | 0.12x | 245x | 11.7x |
| ray | numeric | 8.93 | 0.304 | 0.171 | 18.2 | 13.9 | 3.55 | 2.51x | 0.09x | 0.05x | 5.11x | 3.91x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 11.0 | 11.4 | 0.516 | 763.6 | --- | 17.7 | 0.62x | 0.65x | 0.03x | 43.2x | --- |
| navier_stokes | numeric | 142.5 | 117.3 | 47.0 | 1.04s | 98.6 | 14.0 | 10.1x | 8.35x | 3.35x | 74.1x | 7.02x |
| splay | data | 134.6 | 144.3 | 18.9 | 1.12s | 146.5 | 19.0 | 7.07x | 7.58x | 0.99x | 59.1x | 7.70x |
| hashmap | data | 130.9 | 43.4 | 2.72 | 4.97s | 315.1 | 15.3 | 8.53x | 2.83x | 0.18x | 324x | 20.5x |
| crypto_sha1 | crypto | 47.9 | 26.8 | 2.66 | 627.2 | 219.2 | 8.59 | 5.57x | 3.12x | 0.31x | 73.0x | 25.5x |
| raytrace3d | 3d | 215.9 | 46.6 | 2.20 | 800.2 | --- | 18.4 | 11.8x | 2.54x | 0.12x | 43.6x | --- |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 256.1 | 150.6 | 13.2 | 2.61s | 614.5 | 39.7 | 6.45x | 3.79x | 0.33x | 65.7x | 15.5x |
| microdiff | data-diff | 0.155 | 0.155 | 0.017 | 1.30s | 110.5 | 16.3 | 0.010x | 0.010x | 0.001x | 79.5x | 6.77x |
| hyphen | hyphenation | 2.54 | 1.94 | 0.091 | 375.2 | 52.3 | 6.61 | 0.38x | 0.29x | 0.01x | 56.8x | 7.91x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

⚠ **Two part-2 cells are missing rather than timed**, both reported by the
runner's self-verification check rather than assumed:

| Row | cell | why |
|---|---|---|
| awfy/cd | `timeout` | exceeded the 180s per-run timeout on the auto tier |
| jetstream/hashmap | `wrong_output` | typed hashmap2 prints `hash-map: FAIL result=-450000` on auto AND on explicit `LAMBDA_TIER=interp`, while the JIT passes and the untyped variant passes on auto — a typed-script tier mismatch, filed separately. Timing a failure path would have recorded it as a fast row. |

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 9 | 10 | 2.36x | 0.85x | 1.03x | 1.61x | 0.35x |
| AWFY | 14 | 14 | 13 | 14 | 14 | 14 | 14 | 3.37x | 2.68x | 0.92x | 8.00x | 0.78x |
| BENG | 8 | 8 | 8 | 8 | 8 | 5 | 8 | 1.04x | 0.95x | 1.01x | 1.91x | 0.75x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 11.8x | 8.79x | 0.75x | 9.20x | 2.44x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 7.52x | 2.96x | 1.01x | 6.18x | 2.00x |
| JetStream | 6 | 6 | 5 | 6 | 6 | 4 | 6 | 24.1x | 26.5x | 0.91x | 21.9x | 3.88x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 4.29x | 2.81x | 0.78x | 27.4x | 2.58x |
| **Overall** | 59 | 59 | 57 | 59 | 59 | 53 | 59 | 4.50x | 2.74x | 0.94x | 5.74x | 1.16x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 222.6 | 17.7 | 50.2 | 75.6 | 24.5 | 44.2 | 5.03x | 0.40x | 1.13x | 1.71x | 0.55x |
| fibfp | recursive | 205.8 | 14.7 | 45.9 | 78.0 | 24.3 | 44.4 | 4.63x | 0.33x | 1.03x | 1.75x | 0.55x |
| tak | recursive | 52.1 | 58.6 | 45.1 | 40.8 | 8.29 | 44.0 | 1.18x | 1.33x | 1.03x | 0.93x | 0.19x |
| cpstak | closure | 91.3 | 104.8 | 45.3 | 43.7 | 11.1 | 43.7 | 2.09x | 2.40x | 1.04x | 1.00x | 0.25x |
| sum | iterative | 161.5 | 23.6 | 45.0 | 58.7 | 36.8 | 43.7 | 3.70x | 0.54x | 1.03x | 1.34x | 0.84x |
| sumfp | iterative | 25.0 | 28.4 | 44.5 | 37.6 | 9.10 | 43.6 | 0.57x | 0.65x | 1.02x | 0.86x | 0.21x |
| nqueens | backtrack | 51.4 | 59.3 | 44.9 | 130.9 | 13.8 | 44.5 | 1.16x | 1.33x | 1.01x | 2.94x | 0.31x |
| fft | numeric | 28.4 | 30.7 | 45.3 | 98.0 | 8.10 | 44.5 | 0.64x | 0.69x | 1.02x | 2.20x | 0.18x |
| mbrot | numeric | 111.9 | 130.0 | 45.1 | 54.4 | 23.6 | 44.2 | 2.53x | 2.94x | 1.02x | 1.23x | 0.53x |
| ack | recursive | 1.34s | 28.7 | 56.4 | 252.6 | --- | 56.2 | 23.8x | 0.51x | 1.00x | 4.50x | --- |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 15.4 | 15.1 | 44.2 | 51.4 | 6.09 | 43.5 | 0.35x | 0.35x | 1.02x | 1.18x | 0.14x |
| permute | micro | 28.1 | 33.3 | 44.7 | 55.3 | 7.13 | 44.7 | 0.63x | 0.75x | 1.00x | 1.24x | 0.16x |
| queens | micro | 18.6 | 27.7 | 44.8 | 55.7 | 6.73 | 43.8 | 0.42x | 0.63x | 1.02x | 1.27x | 0.15x |
| towers | micro | 19.5 | 55.6 | 45.3 | 79.1 | 7.99 | 44.5 | 0.44x | 1.25x | 1.02x | 1.78x | 0.18x |
| bounce | micro | 17.0 | 17.6 | 45.0 | 133.4 | 6.78 | 44.5 | 0.38x | 0.40x | 1.01x | 3.00x | 0.15x |
| list | micro | 21.5 | 23.7 | 45.1 | 49.6 | 6.61 | 44.3 | 0.49x | 0.54x | 1.02x | 1.12x | 0.15x |
| storage | micro | 21.7 | 24.0 | 44.8 | 125.4 | 8.10 | 45.0 | 0.48x | 0.53x | 1.00x | 2.79x | 0.18x |
| mandelbrot | compute | 3.99s | 4.86s | 75.3 | 488.7 | 879.5 | 75.1 | 53.1x | 64.7x | 1.00x | 6.51x | 11.7x |
| nbody | compute | 854.8 | 976.4 | 47.5 | 1.07s | 165.3 | 48.7 | 17.6x | 20.1x | 0.98x | 22.0x | 3.40x |
| richards | macro | 28.95s | 2.98s | 76.9 | 1.96s | 197.1 | 91.0 | 318x | 32.7x | 0.85x | 21.5x | 2.17x |
| json | macro | 88.6 | 140.9 | 51.9 | 309.3 | 17.6 | 46.8 | 1.89x | 3.01x | 1.11x | 6.61x | 0.38x |
| deltablue | macro | 1.10s | 1.15s | 53.1 | 2.26s | 107.3 | 56.1 | 19.6x | 20.5x | 0.95x | 40.2x | 1.91x |
| havlak | macro | 857.8 | 869.1 | 53.4 | 109.07s | 3.30s | 144.4 | 5.94x | 6.02x | 0.37x | 755x | 22.8x |
| cd | macro | 12.45s | --- | 117.0 | 28.79s | 1.01s | 125.7 | 99.1x | --- | 0.93x | 229x | 8.05x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 160.3 | 118.4 | 92.0 | 249.3 | 73.9 | 89.7 | 1.79x | 1.32x | 1.03x | 2.78x | 0.82x |
| fannkuch | permutation | 96.3 | 106.8 | 89.2 | 145.9 | 57.0 | 89.7 | 1.07x | 1.19x | 0.99x | 1.63x | 0.64x |
| fasta | generation | 66.7 | 68.4 | 89.9 | 125.2 | 59.0 | 91.7 | 0.73x | 0.75x | 0.98x | 1.37x | 0.64x |
| knucleotide | hashing | 77.6 | 79.2 | 90.8 | 252.1 | --- | 90.4 | 0.86x | 0.88x | 1.00x | 2.79x | --- |
| pidigits | bignum | 57.4 | 58.8 | 91.5 | 87.4 | 49.9 | 86.8 | 0.66x | 0.68x | 1.05x | 1.01x | 0.58x |
| regexredux | regex | 58.3 | 58.7 | 92.0 | 141.1 | --- | 87.8 | 0.66x | 0.67x | 1.05x | 1.61x | --- |
| revcomp | string | 60.8 | 61.5 | 89.9 | 123.9 | --- | 88.7 | 0.69x | 0.69x | 1.01x | 1.40x | --- |
| spectralnorm | numeric | 337.8 | 178.7 | 89.9 | 408.7 | 113.4 | 90.5 | 3.73x | 1.98x | 0.99x | 4.52x | 1.25x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 6.03s | 6.48s | 117.2 | 3.81s | 935.3 | 120.9 | 49.9x | 53.6x | 0.97x | 31.6x | 7.74x |
| matmul | numeric | 2.32s | 2.63s | 95.4 | 1.37s | 590.1 | 103.0 | 22.5x | 25.5x | 0.93x | 13.3x | 5.73x |
| primes | numeric | 486.7 | 621.9 | 90.7 | 3.94s | 145.4 | 91.5 | 5.32x | 6.79x | 0.99x | 43.0x | 1.59x |
| base64 | string | 765.5 | 778.2 | 90.7 | 961.9 | 208.8 | 104.5 | 7.32x | 7.45x | 0.87x | 9.20x | 2.00x |
| levenshtein | string | 817.2 | 397.9 | 90.3 | 524.3 | 104.9 | 91.0 | 8.98x | 4.37x | 0.99x | 5.76x | 1.15x |
| json_gen | data | 230.7 | 181.8 | 91.1 | 141.6 | 70.2 | 93.5 | 2.47x | 1.94x | 0.97x | 1.51x | 0.75x |
| collatz | numeric | 47.62s | 10.10s | 274.4 | 5.65s | 6.25s | 1.47s | 32.4x | 6.88x | 0.19x | 3.85x | 4.25x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 516.1 | 239.0 | 108.9 | 11.22s | 2.19s | 113.3 | 4.56x | 2.11x | 0.96x | 99.0x | 19.3x |
| array1 | array | 173.7 | 219.3 | 48.3 | 119.2 | 45.2 | 47.8 | 3.64x | 4.59x | 1.01x | 2.50x | 0.95x |
| deriv | symbolic | 230.3 | 526.2 | 52.1 | 391.4 | 68.8 | 51.1 | 4.50x | 10.3x | 1.02x | 7.65x | 1.35x |
| diviter | iterative | 145.75s | 17.4 | 313.1 | 9.64s | 26.74s | 516.0 | 282x | 0.03x | 0.61x | 18.7x | 51.8x |
| divrec | recursive | 465.1 | 21.3 | 59.0 | 86.6 | 45.2 | 53.9 | 8.64x | 0.40x | 1.10x | 1.61x | 0.84x |
| gcbench | allocation | 2.55s | 6.34s | 119.2 | 4.13s | 562.1 | 68.6 | 37.1x | 92.3x | 1.74x | 60.2x | 8.19x |
| paraffins | combinat | 35.9 | 38.2 | 49.3 | 58.5 | 12.3 | 47.1 | 0.76x | 0.81x | 1.05x | 1.24x | 0.26x |
| pnpoly | numeric | 1.39s | 1.60s | 50.1 | 155.3 | 211.9 | 52.1 | 26.6x | 30.7x | 0.96x | 2.98x | 4.07x |
| puzzle | search | 209.5 | 247.6 | 49.2 | 154.9 | 38.5 | 49.3 | 4.25x | 5.02x | 1.00x | 3.14x | 0.78x |
| quicksort | sorting | 126.5 | 152.0 | 48.8 | 445.7 | 28.5 | 47.8 | 2.65x | 3.18x | 1.02x | 9.33x | 0.60x |
| ray | numeric | 140.3 | 158.2 | 48.2 | 66.9 | 23.6 | 49.8 | 2.82x | 3.17x | 0.97x | 1.34x | 0.47x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 344.0 | 367.1 | 52.8 | 884.1 | --- | 67.8 | 5.07x | 5.41x | 0.78x | 13.0x | --- |
| navier_stokes | numeric | 9.42s | 10.25s | 98.0 | 1.35s | 112.7 | 63.1 | 149x | 162x | 1.55x | 21.3x | 1.78x |
| splay | data | 1.01s | 1.23s | 69.4 | 2.72s | 564.0 | 95.4 | 10.6x | 12.9x | 0.73x | 28.5x | 5.91x |
| hashmap | data | 1.54s | --- | 51.5 | 5.13s | 325.5 | 62.6 | 24.6x | --- | 0.82x | 81.9x | 5.20x |
| crypto_sha1 | crypto | 2.04s | 2.32s | 52.1 | 686.2 | 229.4 | 55.6 | 36.6x | 41.7x | 0.94x | 12.4x | 4.13x |
| raytrace3d | 3d | 1.77s | 1.81s | 55.6 | 913.1 | --- | 65.9 | 26.9x | 27.5x | 0.84x | 13.9x | --- |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 6.32s | 6.32s | 62.1 | 2.79s | 625.1 | 86.5 | 73.1x | 73.0x | 0.72x | 32.3x | 7.23x |
| microdiff | data-diff | 27.7 | 28.7 | 48.5 | 1.38s | 120.0 | 63.3 | 0.44x | 0.45x | 0.77x | 21.8x | 1.90x |
| hyphen | hyphenation | 139.2 | 37.8 | 49.0 | 1.65s | 70.5 | 56.3 | 2.47x | 0.67x | 0.87x | 29.3x | 1.25x |

