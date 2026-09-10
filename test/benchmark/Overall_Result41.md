# Lambda Benchmark Results: Result41

- **Date:** 2026-09-10
- **Platform:** Darwin arm64
- **Lambda commit:** `ff81fc838842c54b5c2fe0cf7609bc3d917e800f`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v41-ff81fc8388` (19,289,240 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 72.00s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 71.9s (batched 71.1s: sync 55.3s, async 15.8s; non-batched 0.8s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v41.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.42x | 0.49x | 0.18x | 10.8x | 5.97x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.52x | 3.57x | 0.09x | 18.0x | 5.17x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.39x | 0.41x | 0.10x | 9.01x | 1.66x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 2.13x | 2.28x | 0.23x | 23.2x | 11.9x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 1.71x | 1.43x | 0.33x | 24.1x | 13.3x |
| JetStream | 6 | 5 | 5 | 6 | 6 | 6 | 6 | 5.82x | 4.49x | 0.29x | 44.2x | 12.0x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 2.33x | 3.67x | 0.14x | 48.7x | 12.1x |
| **Overall** | 63 | 62 | 62 | 63 | 63 | 63 | 63 | 1.29x | 1.62x | 0.17x | 20.0x | 7.05x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 59 | 1.35x | 1.13x | 9.35x | 58 |
| complete current suite | 63 | 1.62x | 1.29x | 10.1x | 62 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 10.1x over 62 of 63 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/deltablue | 33.78s | 1.15 | 29367x |
| text/prettier_ast | 9.71s | 41.7 | 233x |
| awfy/towers | 3.48 | 0.027 | 128x |
| awfy/queens | 2.04 | 0.018 | 115x |
| awfy/richards | 3.42s | 30.6 | 112x |
| jetstream/cube3d | 36.9 | 0.515 | 71.6x |
| awfy/havlak | 111.8 | 1.80 | 62.2x |
| awfy/permute | 1.08 | 0.026 | 41.8x |
| awfy/cd | 619.2 | 15.1 | 41.1x |
| text/log_pipeline | 20.47s | 552.8 | 37.0x |
| r7rs/nqueens | 4.42 | 0.130 | 34.0x |
| jetstream/hashmap | 78.8 | 2.73 | 28.8x |

---

### Notable Results

- Missing timings: **2** cells
- MIR (untyped) missing: jetstream/navier_stokes (wrong_output)
- MIR (typed) missing: jetstream/navier_stokes (wrong_output)

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| larceny/gcbench | 18.75s | 23.3 | 804x |
| awfy/havlak | 18.43s | 96.4 | 191x |
| beng/binarytrees | 791.1 | 4.21 | 188x |
| text/text_search | 114.21s | 783.3 | 146x |
| larceny/triangl | 7.80s | 66.5 | 117x |
| awfy/cd | 4.13s | 35.9 | 115x |
| jetstream/hashmap | 1.67s | 15.2 | 110x |
| awfy/nbody | 567.0 | 5.46 | 104x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.386 | 1.95 | 0.20x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.60 [1.56–1.66] | 1.54 [1.52–1.55] | 1.11 [1.10–1.12] | 47.1 [46.8–47.4] | 18.9 [18.8–19.8] | 1.82 [1.79–7.92] | 0.88x | 0.85x | 0.61x | 25.9x | 10.4x |
| fibfp | recursive | 2.08 [2.07–2.08] | 1.21 [1.20–1.22] | 1.14 [1.11–1.29] | 48.2 [47.5–48.7] | 19.5 [19.1–76.7] | 4.07 [1.92–4.54] | 0.51x | 0.30x | 0.28x | 11.9x | 4.79x |
| tak | recursive | 0.181 [0.161–0.186] | 0.210 [0.209–0.216] | 0.122 [0.113–0.128] | 3.28 [3.27–3.50] | 2.83 [2.81–2.83] | 0.806 [0.802–0.815] | 0.22x | 0.26x | 0.15x | 4.07x | 3.51x |
| cpstak | closure | 0.311 [0.311–0.356] | 0.413 [0.406–0.432] | 0.238 [0.223–0.256] | 6.74 [6.69–6.98] | 5.54 [5.51–5.61] | 0.996 [0.989–1.01] | 0.31x | 0.41x | 0.24x | 6.76x | 5.56x |
| sum | iterative | 1.21 [1.21–1.23] | 1.25 [1.22–1.26] | 0.273 [0.268–0.274] | 26.2 [25.6–26.4] | 31.1 [31.0–31.1] | 1.19 [1.19–1.98] | 1.02x | 1.05x | 0.23x | 22.0x | 26.1x |
| sumfp | iterative | 0.069 [0.069–0.069] | 0.069 [0.068–0.073] | 0.079 [0.078–0.080] | 2.59 [2.55–2.63] | 3.66 [3.66–3.72] | 0.898 [0.861–1.74] | 0.08x | 0.08x | 0.09x | 2.88x | 4.07x |
| nqueens | backtrack | 1.81 [1.70–1.81] | 4.42 [4.38–4.45] | 0.130 [0.130–0.130] | 37.7 [37.6–38.1] | 7.89 [7.79–8.03] | 1.76 [1.74–3.02] | 1.03x | 2.51x | 0.07x | 21.4x | 4.48x |
| fft | numeric | 0.299 [0.294–0.309] | 0.387 [0.387–0.402] | 0.025 [0.025–0.026] | 22.7 [22.6–22.9] | 2.79 [2.75–2.81] | 1.61 [1.60–1.62] | 0.19x | 0.24x | 0.02x | 14.1x | 1.74x |
| mbrot | numeric | 0.704 [0.704–0.707] | 0.913 [0.913–0.932] | 0.457 [0.454–0.480] | 13.1 [13.1–13.2] | 17.7 [17.6–17.7] | 1.85 [1.84–1.87] | 0.38x | 0.49x | 0.25x | 7.06x | 9.52x |
| ack | recursive | 13.2 [13.0–14.7] | 15.5 [15.4–16.1] | 11.7 [10.8–11.8] | 253.6 [250.4–254.2] | 101.3 [101.0–101.6] | 13.4 [13.3–13.4] | 0.99x | 1.16x | 0.88x | 19.0x | 7.58x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.039 [0.039–0.039] | 0.135 [0.134–0.145] | 0.016 [0.015–0.016] | 1.19 [1.16–1.30] | 0.616 [0.607–0.624] | 0.383 [0.377–0.397] | 0.10x | 0.35x | 0.04x | 3.11x | 1.61x |
| permute | micro | 0.559 [0.553–0.560] | 1.08 [1.07–1.15] | 0.026 [0.025–0.027] | 7.78 [7.74–7.92] | 1.54 [1.54–1.57] | 0.813 [0.812–0.822] | 0.69x | 1.33x | 0.03x | 9.56x | 1.90x |
| queens | micro | 0.420 [0.413–0.420] | 2.04 [1.99–2.05] | 0.018 [0.017–0.021] | 4.31 [4.31–4.38] | 1.04 [1.04–1.06] | 0.638 [0.636–0.641] | 0.66x | 3.20x | 0.03x | 6.76x | 1.63x |
| towers | micro | 1.24 [1.23–1.24] | 3.48 [3.42–3.54] | 0.027 [0.027–0.029] | 16.0 [15.8–16.0] | 2.26 [2.22–2.30] | 1.12 [1.12–1.12] | 1.10x | 3.11x | 0.02x | 14.3x | 2.02x |
| bounce | micro | 0.073 [0.073–0.074] | 0.143 [0.141–0.144] | 0.025 [0.024–0.026] | 3.41 [3.39–3.45] | 0.870 [0.867–0.883] | 0.539 [0.533–0.594] | 0.14x | 0.27x | 0.05x | 6.33x | 1.61x |
| list | micro | 0.720 [0.717–0.729] | 0.210 [0.209–0.216] | 0.022 [0.021–0.022] | 2.12 [2.09–2.13] | 0.910 [0.901–0.931] | 0.494 [0.490–0.510] | 1.46x | 0.42x | 0.04x | 4.29x | 1.84x |
| storage | micro | 0.696 [0.692–0.745] | 0.839 [0.836–0.848] | 0.092 [0.090–0.095] | 5.86 [5.74–5.92] | 2.07 [2.04–2.10] | 0.635 [0.627–0.641] | 1.10x | 1.32x | 0.14x | 9.22x | 3.26x |
| mandelbrot | compute | 44.2 [44.2–44.5] | 44.2 [44.1–44.2] | 30.6 [30.6–30.8] | 436.2 [435.7–489.7] | 868.8 [868.7–882.0] | 31.1 [31.0–31.1] | 1.42x | 1.42x | 0.98x | 14.0x | 28.0x |
| nbody | compute | 53.0 [52.9–53.0] | 39.1 [39.1–39.3] | 1.49 [1.49–1.49] | 567.0 [565.6–569.1] | 159.6 [159.2–159.7] | 5.46 [5.36–5.73] | 9.72x | 7.16x | 0.27x | 104x | 29.2x |
| richards | macro | 425.4 [424.7–425.8] | 3.42s [3.41s–3.42s] | 30.6 [30.4–35.7] | 1.16s [1.11s–1.20s] | 191.8 [191.6–191.9] | 47.4 [46.9–57.1] | 8.97x | 72.1x | 0.65x | 24.5x | 4.05x |
| json | macro | 6.01 [5.98–6.16] | 5.19 [5.07–5.19] | 0.264 [0.260–0.275] | 44.9 [44.3–44.9] | 10.7 [10.7–10.9] | 2.65 [2.57–2.88] | 2.27x | 1.95x | 0.10x | 16.9x | 4.04x |
| deltablue | macro | 126.3 [126.2–126.4] | 33.78s [33.72s–33.90s] | 1.15 [1.15–1.16] | 507.3 [503.2–508.7] | 99.9 [99.9–100.2] | 12.4 [11.4–20.7] | 10.2x | 2715x | 0.09x | 40.8x | 8.03x |
| havlak | macro | 66.2 [66.2–68.5] | 111.8 [111.8–112.5] | 1.80 [1.79–1.83] | 18.43s [18.42s–18.47s] | 3.31s [3.28s–3.32s] | 96.4 [96.2–97.6] | 0.69x | 1.16x | 0.02x | 191x | 34.3x |
| cd | macro | 593.9 [592.5–594.8] | 619.2 [617.7–624.8] | 15.1 [14.9–15.1] | 4.13s [4.10s–4.14s] | 965.7 [963.9–966.2] | 35.9 [35.7–36.5] | 16.5x | 17.2x | 0.42x | 115x | 26.9x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.06 [9.02–9.64] | 4.87 [4.84–4.96] | 3.09 [3.02–3.11] | 791.1 [786.2–886.6] | 24.3 [23.8–24.6] | 4.21 [4.15–5.39] | 2.15x | 1.16x | 0.73x | 188x | 5.76x |
| fannkuch | permutation | 0.434 [0.432–0.436] | 0.810 [0.810–0.819] | 0.149 [0.148–0.154] | 25.2 [25.2–25.3] | 7.21 [7.16–7.29] | 4.02 [3.99–4.05] | 0.11x | 0.20x | 0.04x | 6.28x | 1.79x |
| fasta | generation | 0.801 [0.798–0.829] | 0.939 [0.935–0.941] | 0.244 [0.243–0.244] | 30.6 [30.3–30.6] | 8.75 [8.71–8.94] | 6.26 [6.14–6.74] | 0.13x | 0.15x | 0.04x | 4.89x | 1.40x |
| knucleotide | hashing | 4.61 [4.53–4.81] | 5.31 [5.20–5.38] | 0.297 [0.285–0.304] | 164.9 [164.5–219.9] | 7.70 [7.65–7.72] | 5.03 [4.89–6.03] | 0.92x | 1.06x | 0.06x | 32.8x | 1.53x |
| pidigits | bignum | 0.303 [0.300–0.319] | 0.303 [0.302–0.310] | 0.046 [0.045–0.046] | 0.386 [0.386–0.414] | 0.127 [0.126–0.132] | 1.95 [1.92–2.13] | 0.16x | 0.16x | 0.02x | 0.20x | 0.06x |
| regexredux | regex | 1.28 [1.27–1.28] | 1.29 [1.29–1.30] | 1.14 [1.13–1.16] | 26.1 [25.9–26.2] | 5.64 [5.56–5.79] | 2.51 [2.42–2.60] | 0.51x | 0.52x | 0.46x | 10.4x | 2.25x |
| revcomp | string | 1.27 [1.26–1.29] | 1.21 [1.21–1.22] | 0.377 [0.375–0.385] | 26.9 [26.8–27.1] | 2.59 [2.57–2.63] | 3.48 [3.38–4.03] | 0.36x | 0.35x | 0.11x | 7.73x | 0.74x |
| spectralnorm | numeric | 1.79 [1.78–1.91] | 2.18 [2.18–2.21] | 0.355 [0.351–0.360] | 39.0 [38.8–39.0] | 65.1 [64.2–65.3] | 2.70 [2.42–2.73] | 0.66x | 0.81x | 0.13x | 14.4x | 24.1x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 228.1 [225.5–282.3] | 328.6 [327.7–329.1] | 28.3 [28.0–28.3] | 2.15s [2.11s–2.26s] | 889.4 [886.7–891.4] | 33.8 [33.6–34.0] | 6.75x | 9.72x | 0.84x | 63.5x | 26.3x |
| matmul | numeric | 38.3 [38.3–38.3] | 46.2 [46.1–46.2] | 6.11 [6.04–6.17] | 660.2 [658.3–667.3] | 539.2 [539.2–540.4] | 15.3 [15.3–15.4] | 2.50x | 3.01x | 0.40x | 43.0x | 35.1x |
| primes | numeric | 14.7 [14.6–14.7] | 23.4 [23.3–23.4] | 1.58 [1.58–1.65] | 133.0 [132.2–133.5] | 94.8 [94.6–95.1] | 4.42 [4.32–4.45] | 3.32x | 5.29x | 0.36x | 30.1x | 21.4x |
| base64 | string | 19.6 [19.3–19.7] | 14.0 [13.9–14.1] | 0.580 [0.558–0.580] | 852.7 [851.7–853.7] | 160.5 [157.7–164.6] | 17.4 [17.3–17.5] | 1.12x | 0.80x | 0.03x | 48.9x | 9.20x |
| levenshtein | string | 11.5 [11.4–11.6] | 8.43 [8.43–8.48] | 0.900 [0.898–0.915] | 121.9 [121.0–121.9] | 54.4 [54.3–54.4] | 4.10 [4.10–4.10] | 2.81x | 2.06x | 0.22x | 29.7x | 13.3x |
| json_gen | data | 24.2 [24.0–25.5] | 27.2 [26.1–61.2] | 1.54 [1.50–1.56] | 46.9 [46.7–47.2] | 19.9 [19.9–19.9] | 6.28 [6.22–6.33] | 3.86x | 4.33x | 0.25x | 7.47x | 3.17x |
| collatz | numeric | 412.2 [411.6–468.6] | 413.6 [412.0–416.1] | 225.4 [225.2–225.8] | 5.82s [5.81s–6.02s] | 6.24s [6.22s–6.25s] | 1.42s [1.42s–1.42s] | 0.29x | 0.29x | 0.16x | 4.11x | 4.41x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 284.5 [283.4–284.6] | 280.4 [279.5–280.7] | 60.3 [60.1–60.7] | 7.80s [7.79s–7.82s] | 2.19s [2.17s–2.19s] | 66.5 [66.4–67.1] | 4.28x | 4.22x | 0.91x | 117x | 32.9x |
| array1 | array | 1.08 [1.07–1.09] | 2.03 [2.03–2.04] | 0.320 [0.316–0.323] | 35.8 [35.6–36.2] | 35.9 [35.7–35.9] | 1.91 [1.90–1.95] | 0.57x | 1.07x | 0.17x | 18.8x | 18.8x |
| deriv | symbolic | 30.0 [29.7–30.1] | 11.1 [10.8–11.2] | 2.89 [2.88–2.91] | 82.8 [82.7–83.1] | 59.5 [59.2–59.9] | 3.77 [3.73–3.93] | 7.96x | 2.94x | 0.77x | 21.9x | 15.8x |
| diviter | iterative | 532.7 [532.4–532.9] | 532.8 [532.3–533.0] | 266.1 [264.8–266.8] | 11.34s [11.33s–11.39s] | 26.75s [26.73s–26.76s] | 471.2 [470.9–471.9] | 1.13x | 1.13x | 0.56x | 24.1x | 56.8x |
| divrec | recursive | 6.21 [6.20–6.31] | 2.54 [2.54–2.58] | 4.90 [4.85–4.90] | 50.3 [50.1–50.6] | 35.9 [35.8–36.5] | 7.55 [7.52–7.55] | 0.82x | 0.34x | 0.65x | 6.67x | 4.76x |
| gcbench | allocation | 206.8 [205.4–208.1] | 150.6 [142.4–163.2] | 71.6 [70.4–71.9] | 18.75s [18.72s–18.80s] | 549.6 [545.2–560.6] | 23.3 [23.3–24.0] | 8.87x | 6.46x | 3.07x | 804x | 23.6x |
| paraffins | combinat | 0.313 [0.309–0.319] | 0.485 [0.480–0.488] | 0.049 [0.048–0.050] | 2.41 [2.40–2.42] | 2.53 [2.48–2.56] | 1.000 [0.989–1.01] | 0.31x | 0.49x | 0.05x | 2.41x | 2.53x |
| pnpoly | numeric | 15.9 [15.6–15.9] | 29.5 [29.4–29.6] | 1.94 [1.94–1.96] | 107.1 [107.0–107.2] | 202.0 [201.4–202.3] | 5.81 [5.80–5.93] | 2.74x | 5.09x | 0.33x | 18.4x | 34.8x |
| puzzle | search | 14.7 [14.6–15.1] | 8.68 [8.53–8.75] | 1.28 [1.27–1.29] | 46.4 [46.1–46.8] | 29.4 [29.3–29.4] | 3.34 [3.30–3.36] | 4.40x | 2.60x | 0.38x | 13.9x | 8.80x |
| quicksort | sorting | 11.3 [11.3–11.3] | 2.84 [2.81–2.86] | 0.198 [0.195–0.200] | 147.3 [147.2–148.5] | 19.2 [19.2–19.3] | 1.64 [1.63–1.76] | 6.89x | 1.73x | 0.12x | 89.8x | 11.7x |
| ray | numeric | 0.305 [0.305–0.306] | 0.483 [0.482–0.484] | 0.171 [0.170–0.171] | 16.4 [16.3–16.6] | 13.9 [13.9–13.9] | 3.54 [3.45–3.57] | 0.09x | 0.14x | 0.05x | 4.65x | 3.92x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 29.5 [29.3–30.2] | 36.9 [36.7–37.2] | 0.515 [0.508–0.522] | 384.3 [382.3–386.5] | 215.0 [214.7–215.7] | 17.8 [17.5–17.9] | 1.66x | 2.07x | 0.03x | 21.6x | 12.1x |
| navier_stokes | numeric | --- | --- | 46.7 [46.7–46.9] | 339.0 [337.4–347.1] | 99.0 [98.4–99.0] | 14.0 [13.9–14.0] | --- | --- | 3.34x | 24.3x | 7.09x |
| splay | data | 309.9 [307.4–318.2] | 301.2 [300.3–301.4] | 18.9 [18.8–19.5] | 1.73s [1.72s–1.73s] | 144.0 [143.7–145.2] | 19.0 [18.9–19.5] | 16.3x | 15.9x | 1.00x | 91.1x | 7.60x |
| hashmap | data | 119.9 [118.9–120.8] | 78.8 [78.2–79.3] | 2.73 [2.71–2.80] | 1.67s [1.67s–1.70s] | 314.2 [309.8–316.2] | 15.2 [15.0–15.3] | 7.87x | 5.17x | 0.18x | 110x | 20.6x |
| crypto_sha1 | crypto | 52.4 [52.2–52.7] | 35.9 [35.4–36.1] | 2.71 [2.61–2.79] | 372.6 [371.1–372.7] | 216.6 [216.5–217.5] | 8.61 [8.53–8.79] | 6.08x | 4.17x | 0.32x | 43.3x | 25.2x |
| raytrace3d | 3d | 92.8 [92.7–93.4] | 46.1 [46.0–46.8] | 2.17 [2.16–2.20] | 589.1 [587.4–591.8] | 161.9 [161.4–162.0] | 18.0 [17.9–18.4] | 5.16x | 2.56x | 0.12x | 32.7x | 8.99x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 429.0 [429.0–485.3] | 330.3 [328.2–336.7] | 13.0 [13.0–13.1] | 1.94s [1.93s–1.95s] | 614.5 [612.1–620.2] | 39.0 [38.9–39.7] | 11.0x | 8.47x | 0.33x | 49.6x | 15.8x |
| microdiff | data-diff | 0.187 [0.186–0.194] | 0.186 [0.185–0.186] | 0.018 [0.018–0.076] | 1.19s [1.17s–1.20s] | 109.8 [108.0–109.9] | 16.3 [16.3–17.1] | 0.01x | 0.01x | 0.001x | 72.7x | 6.73x |
| hyphen | hyphenation | 3.33 [3.28–3.40] | 2.20 [2.13–2.24] | 0.088 [0.086–0.089] | 347.4 [345.8–356.0] | 51.2 [51.1–51.8] | 6.51 [6.25–7.32] | 0.51x | 0.34x | 0.01x | 53.3x | 7.87x |
| prettier_ast | formatting | 1.08s [1.08s–1.15s] | 9.71s [9.63s–9.77s] | 41.7 [41.5–42.3] | 5.68s [5.66s–5.79s] | 1.42s [1.41s–1.43s] | 100.6 [100.3–108.8] | 10.7x | 96.5x | 0.41x | 56.5x | 14.1x |
| text_search | search | 15.68s [15.65s–15.74s] | 3.06s [3.06s–3.07s] | 538.3 [537.8–538.5] | 114.21s [113.99s–114.46s] | 38.95s [38.95s–39.02s] | 783.3 [781.1–789.7] | 20.0x | 3.91x | 0.69x | 146x | 49.7x |
| three_way_merge | merge | 3.77s [3.74s–3.82s] | 32.22s [31.85s–32.41s] | 1.44s [1.44s–1.44s] | 19.80s [19.79s–19.95s] | 6.13s [6.07s–6.14s] | 962.1 [954.7–965.6] | 3.92x | 33.5x | 1.49x | 20.6x | 6.37x |
| log_pipeline | log-processing | 6.33s [6.33s–6.34s] | 20.47s [20.22s–21.21s] | 552.8 [551.0–553.5] | 18.57s [18.47s–18.61s] | 9.34s [9.33s–9.36s] | 932.8 [932.7–941.8] | 6.79x | 21.9x | 0.59x | 19.9x | 10.0x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.40x | 0.43x | 0.99x | 1.29x | 0.40x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 0.99x | 1.86x | 0.92x | 4.63x | 0.80x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.48x | 0.53x | 1.00x | 2.04x | 0.33x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 0.84x | 0.94x | 0.74x | 6.16x | 2.89x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 0.91x | 0.84x | 0.99x | 4.92x | 1.90x |
| JetStream | 6 | 5 | 5 | 6 | 6 | 6 | 6 | 2.88x | 4.93x | 0.90x | 14.7x | 3.67x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 3.15x | 5.21x | 0.81x | 31.5x | 6.47x |
| **Overall** | 63 | 62 | 62 | 63 | 63 | 63 | 63 | 0.94x | 1.22x | 0.92x | 4.91x | 1.25x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 16.6 [16.6–16.8] | 17.3 [16.8–18.0] | 46.2 [46.2–479.4] | 74.4 [73.9–75.5] | 26.3 [25.3–27.4] | 46.3 [46.2–94.4] | 0.36x | 0.37x | 1.00x | 1.61x | 0.57x |
| fibfp | recursive | 18.2 [18.1–18.6] | 17.4 [17.2–17.6] | 48.8 [47.7–49.4] | 75.0 [74.3–75.6] | 25.9 [25.7–83.4] | 65.6 [47.3–67.2] | 0.28x | 0.26x | 0.74x | 1.14x | 0.39x |
| tak | recursive | 17.9 [17.1–18.1] | 17.6 [17.1–17.9] | 48.6 [47.4–49.8] | 32.8 [32.8–34.4] | 8.69 [8.62–9.14] | 45.4 [44.1–45.4] | 0.39x | 0.39x | 1.07x | 0.72x | 0.19x |
| cpstak | closure | 16.9 [16.8–17.3] | 16.8 [16.8–16.8] | 49.0 [47.9–50.3] | 37.7 [37.6–37.9] | 11.5 [10.9–12.3] | 44.2 [43.7–44.6] | 0.38x | 0.38x | 1.11x | 0.85x | 0.26x |
| sum | iterative | 16.3 [16.3–16.7] | 16.8 [16.7–17.0] | 45.5 [45.1–45.6] | 52.1 [51.2–53.0] | 36.8 [36.6–37.1] | 44.6 [44.1–45.6] | 0.37x | 0.38x | 1.02x | 1.17x | 0.82x |
| sumfp | iterative | 14.8 [14.7–15.0] | 15.0 [15.0–15.3] | 44.9 [44.9–45.2] | 28.3 [27.9–29.4] | 9.41 [8.94–9.52] | 44.4 [44.4–44.5] | 0.33x | 0.34x | 1.01x | 0.64x | 0.21x |
| nqueens | backtrack | 25.1 [24.8–25.3] | 32.2 [32.2–32.4] | 45.3 [44.9–45.6] | 110.7 [110.4–110.8] | 14.0 [13.6–14.3] | 45.7 [45.0–46.5] | 0.55x | 0.70x | 0.99x | 2.42x | 0.31x |
| fft | numeric | 21.7 [21.6–22.2] | 29.9 [29.8–30.0] | 45.9 [45.7–45.9] | 58.9 [58.4–59.4] | 8.55 [8.38–9.08] | 45.4 [45.3–101.5] | 0.48x | 0.66x | 1.01x | 1.30x | 0.19x |
| mbrot | numeric | 19.1 [19.0–19.3] | 19.3 [19.3–19.5] | 45.9 [45.7–46.8] | 42.4 [42.3–43.0] | 23.4 [23.4–23.8] | 45.1 [44.7–45.3] | 0.42x | 0.43x | 1.02x | 0.94x | 0.52x |
| ack | recursive | 31.0 [30.8–33.8] | 31.5 [31.3–32.6] | 57.1 [55.7–57.1] | 280.4 [276.6–280.5] | 107.5 [107.2–107.9] | 56.5 [56.5–56.5] | 0.55x | 0.56x | 1.01x | 4.96x | 1.90x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 15.8 [15.7–16.2] | 16.7 [16.5–17.1] | 45.2 [45.1–45.5] | 33.0 [32.3–33.9] | 6.19 [6.12–6.85] | 44.6 [44.3–46.1] | 0.36x | 0.37x | 1.01x | 0.74x | 0.14x |
| permute | micro | 17.5 [17.3–17.8] | 19.1 [19.0–19.5] | 45.4 [45.2–45.7] | 41.8 [41.5–43.0] | 7.21 [7.09–7.87] | 44.9 [44.8–45.0] | 0.39x | 0.42x | 1.01x | 0.93x | 0.16x |
| queens | micro | 21.8 [21.7–22.0] | 27.6 [27.4–27.6] | 45.6 [45.5–46.1] | 41.6 [41.4–42.7] | 6.52 [6.52–7.22] | 44.9 [44.3–45.2] | 0.49x | 0.61x | 1.02x | 0.93x | 0.15x |
| towers | micro | 21.9 [21.8–22.0] | 26.0 [26.0–26.2] | 46.0 [45.6–104.8] | 53.0 [52.9–54.2] | 8.07 [7.63–8.43] | 45.1 [44.8–45.3] | 0.49x | 0.58x | 1.02x | 1.18x | 0.18x |
| bounce | micro | 24.4 [24.3–24.6] | 48.3 [48.3–48.3] | 45.5 [45.5–45.7] | 118.6 [117.6–118.6] | 7.24 [7.00–7.88] | 44.9 [44.6–45.4] | 0.54x | 1.08x | 1.01x | 2.64x | 0.16x |
| list | micro | 19.7 [19.6–19.9] | 19.4 [19.2–19.4] | 46.4 [45.3–46.5] | 37.7 [37.6–38.6] | 6.56 [6.36–7.03] | 44.8 [44.5–44.9] | 0.44x | 0.43x | 1.04x | 0.84x | 0.15x |
| storage | micro | 18.0 [18.0–18.1] | 18.9 [18.7–19.0] | 45.7 [45.6–46.1] | 114.3 [113.0–114.7] | 8.37 [8.35–9.05] | 44.8 [44.8–45.3] | 0.40x | 0.42x | 1.02x | 2.55x | 0.19x |
| mandelbrot | compute | 61.8 [61.6–61.8] | 61.8 [61.8–62.0] | 76.3 [75.7–76.7] | 475.6 [475.4–529.6] | 875.5 [875.5–889.0] | 76.4 [76.0–77.5] | 0.81x | 0.81x | 1.00x | 6.23x | 11.5x |
| nbody | compute | 84.0 [83.9–84.4] | 108.0 [107.4–108.5] | 48.0 [47.9–48.8] | 634.5 [632.3–638.3] | 166.3 [166.1–166.4] | 50.1 [49.2–51.3] | 1.68x | 2.16x | 0.96x | 12.7x | 3.32x |
| richards | macro | 482.9 [478.3–538.5] | 3.57s [3.57s–3.65s] | 85.5 [80.0–86.3] | 1.27s [1.22s–1.31s] | 199.0 [198.7–200.6] | 93.0 [92.6–177.4] | 5.19x | 38.4x | 0.92x | 13.6x | 2.14x |
| json | macro | 53.5 [52.2–60.9] | 55.5 [55.1–55.7] | 52.7 [52.7–53.1] | 243.3 [242.3–304.4] | 18.8 [18.2–18.9] | 47.9 [47.2–49.4] | 1.12x | 1.16x | 1.10x | 5.08x | 0.39x |
| deltablue | macro | 239.6 [238.8–240.1] | 34.51s [34.49s–34.52s] | 53.9 [53.5–58.9] | 739.0 [735.7–741.2] | 107.8 [107.7–109.6] | 58.3 [57.5–142.5] | 4.11x | 592x | 0.93x | 12.7x | 1.85x |
| havlak | macro | 158.0 [157.6–158.6] | 195.1 [194.9–195.8] | 54.2 [53.6–54.2] | 18.79s [18.79s–18.82s] | 3.32s [3.29s–3.33s] | 143.6 [143.0–145.9] | 1.10x | 1.36x | 0.38x | 131x | 23.1x |
| cd | macro | 656.6 [651.9–661.4] | 691.4 [690.9–697.2] | 67.5 [67.4–68.2] | 4.45s [4.43s–4.46s] | 973.7 [971.9–974.7] | 81.4 [80.6–81.7] | 8.07x | 8.50x | 0.83x | 54.6x | 12.0x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 27.7 [27.5–28.8] | 26.4 [25.9–26.7] | 48.5 [48.3–49.2] | 824.1 [819.2–933.0] | 30.8 [30.7–31.2] | 47.7 [46.8–49.0] | 0.58x | 0.55x | 1.02x | 17.3x | 0.65x |
| fannkuch | permutation | 21.9 [21.7–22.9] | 29.6 [29.4–29.6] | 46.1 [45.5–46.6] | 57.6 [57.2–60.3] | 13.0 [12.9–13.2] | 47.2 [45.8–47.4] | 0.46x | 0.63x | 0.98x | 1.22x | 0.28x |
| fasta | generation | 23.8 [23.6–23.9] | 25.4 [25.4–25.5] | 45.9 [45.8–46.3] | 69.9 [69.8–70.3] | 14.9 [14.4–15.0] | 48.1 [47.9–48.5] | 0.49x | 0.53x | 0.95x | 1.45x | 0.31x |
| knucleotide | hashing | 30.0 [29.8–30.5] | 37.8 [37.6–37.8] | 46.5 [46.0–47.6] | 209.2 [200.4–255.6] | 13.4 [13.3–13.9] | 47.7 [47.2–47.8] | 0.63x | 0.79x | 0.98x | 4.39x | 0.28x |
| pidigits | bignum | 19.9 [19.8–20.1] | 19.8 [19.8–19.9] | 46.9 [46.9–47.3] | 36.4 [35.5–36.7] | 5.38 [5.23–6.00] | 44.7 [42.8–44.9] | 0.44x | 0.44x | 1.05x | 0.81x | 0.12x |
| regexredux | regex | 14.8 [14.6–15.2] | 15.0 [15.0–15.3] | 48.3 [47.6–49.2] | 61.6 [61.5–62.0] | 11.6 [11.3–11.6] | 45.3 [44.9–45.4] | 0.33x | 0.33x | 1.07x | 1.36x | 0.26x |
| revcomp | string | 21.5 [21.2–21.6] | 22.4 [22.3–22.6] | 46.2 [46.2–48.6] | 64.3 [64.2–64.9] | 8.52 [8.03–8.80] | 47.3 [46.9–47.5] | 0.45x | 0.47x | 0.98x | 1.36x | 0.18x |
| spectralnorm | numeric | 24.7 [24.6–24.9] | 28.6 [27.9–28.8] | 46.7 [46.6–47.6] | 71.8 [71.7–72.6] | 71.2 [70.6–71.7] | 48.1 [47.7–48.4] | 0.51x | 0.59x | 0.97x | 1.49x | 1.48x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 250.7 [250.2–250.9] | 355.9 [355.0–361.7] | 74.0 [73.9–76.0] | 2.18s [2.15s–2.30s] | 896.0 [893.6–897.9] | 78.7 [78.2–78.9] | 3.19x | 4.52x | 0.94x | 27.7x | 11.4x |
| matmul | numeric | 59.4 [59.3–59.5] | 68.7 [68.5–69.5] | 51.6 [51.2–51.6] | 694.0 [691.5–700.3] | 545.7 [545.4–546.7] | 58.7 [58.3–58.8] | 1.01x | 1.17x | 0.88x | 11.8x | 9.30x |
| primes | numeric | 31.1 [31.0–31.1] | 40.8 [40.6–41.0] | 46.6 [46.4–47.1] | 160.5 [160.4–161.1] | 100.8 [100.7–101.2] | 47.1 [46.7–47.1] | 0.66x | 0.87x | 0.99x | 3.41x | 2.14x |
| base64 | string | 41.7 [41.6–41.8] | 45.3 [45.3–45.4] | 58.1 [46.1–98.1] | 887.2 [885.3–895.0] | 166.8 [164.0–170.9] | 60.7 [60.6–61.1] | 0.69x | 0.75x | 0.96x | 14.6x | 2.75x |
| levenshtein | string | 35.3 [35.0–35.5] | 31.1 [30.9–31.3] | 46.2 [45.8–46.6] | 156.4 [154.6–156.7] | 60.4 [60.2–60.5] | 48.1 [48.0–48.3] | 0.73x | 0.65x | 0.96x | 3.25x | 1.25x |
| json_gen | data | 48.4 [48.3–48.7] | 50.6 [49.5–50.7] | 46.6 [46.3–46.9] | 81.1 [80.4–81.5] | 25.8 [25.7–26.1] | 51.1 [49.9–51.5] | 0.95x | 0.99x | 0.91x | 1.59x | 0.51x |
| collatz | numeric | 427.2 [426.2–428.5] | 430.6 [429.5–430.7] | 270.5 [270.3–271.4] | 5.85s [5.84s–6.05s] | 6.25s [6.23s–6.26s] | 1.46s [1.46s–1.46s] | 0.29x | 0.29x | 0.18x | 4.00x | 4.27x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 311.9 [310.6–312.0] | 311.9 [311.0–356.8] | 105.7 [105.7–107.0] | 7.84s [7.83s–7.86s] | 2.20s [2.18s–2.20s] | 111.3 [111.2–111.4] | 2.80x | 2.80x | 0.95x | 70.4x | 19.7x |
| array1 | array | 16.9 [16.5–17.0] | 17.6 [17.4–17.6] | 45.0 [45.0–45.1] | 62.7 [62.3–122.0] | 41.7 [41.6–41.8] | 45.8 [45.6–46.3] | 0.37x | 0.38x | 0.98x | 1.37x | 0.91x |
| deriv | symbolic | 54.6 [54.5–54.9] | 32.0 [32.0–32.1] | 49.0 [49.0–49.2] | 115.3 [115.0–115.6] | 65.4 [65.2–66.1] | 47.7 [47.1–47.7] | 1.14x | 0.67x | 1.03x | 2.42x | 1.37x |
| diviter | iterative | 549.3 [549.3–549.7] | 549.7 [549.7–549.8] | 311.6 [310.4–312.3] | 11.39s [11.35s–11.41s] | 26.76s [26.74s–26.77s] | 516.4 [515.8–517.3] | 1.06x | 1.06x | 0.60x | 22.1x | 51.8x |
| divrec | recursive | 22.6 [22.2–22.9] | 19.3 [18.9–19.7] | 49.5 [49.4–49.7] | 79.2 [78.8–79.3] | 41.8 [41.8–42.8] | 51.8 [51.7–52.1] | 0.44x | 0.37x | 0.96x | 1.53x | 0.81x |
| gcbench | allocation | 228.2 [227.3–229.0] | 194.1 [190.4–195.4] | 117.2 [115.8–117.3] | 18.79s [18.76s–18.85s] | 561.6 [557.4–572.3] | 67.2 [67.1–68.1] | 3.40x | 2.89x | 1.74x | 280x | 8.36x |
| paraffins | combinat | 42.5 [40.9–42.5] | 48.4 [48.3–49.4] | 45.9 [45.7–101.4] | 43.0 [42.9–43.2] | 8.31 [7.94–8.61] | 45.1 [44.7–45.7] | 0.94x | 1.07x | 1.02x | 0.95x | 0.18x |
| pnpoly | numeric | 34.2 [34.0–34.3] | 50.8 [50.6–50.9] | 47.5 [47.4–47.5] | 137.6 [137.5–138.1] | 208.3 [207.9–208.5] | 50.3 [50.2–50.4] | 0.68x | 1.01x | 0.94x | 2.74x | 4.14x |
| puzzle | search | 33.9 [33.7–34.0] | 30.2 [30.0–30.6] | 46.7 [46.6–47.0] | 76.9 [76.7–78.0] | 35.2 [35.2–35.3] | 46.9 [46.9–47.2] | 0.72x | 0.64x | 1.00x | 1.64x | 0.75x |
| quicksort | sorting | 31.4 [31.1–31.4] | 25.0 [24.9–25.1] | 45.9 [45.7–46.1] | 179.9 [179.1–180.0] | 25.3 [25.0–25.5] | 45.5 [45.5–46.3] | 0.69x | 0.55x | 1.01x | 3.96x | 0.56x |
| ray | numeric | 26.6 [26.4–26.7] | 20.5 [20.3–20.6] | 45.3 [45.1–45.6] | 50.6 [50.0–51.5] | 19.5 [19.3–19.9] | 46.4 [45.6–46.6] | 0.57x | 0.44x | 0.98x | 1.09x | 0.42x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 173.8 [173.1–174.5] | 448.0 [445.5–464.4] | 48.6 [48.2–48.8] | 487.6 [486.2–490.0] | 221.4 [220.9–222.1] | 63.1 [63.1–63.9] | 2.75x | 7.10x | 0.77x | 7.73x | 3.51x |
| navier_stokes | numeric | --- [197.9–200.3] | --- [199.2–201.1] | 93.5 [93.4–94.3] | 510.2 [506.6–522.6] | 109.3 [108.6–109.7] | 61.1 [60.1–61.2] | --- | --- | 1.53x | 8.34x | 1.79x |
| splay | data | 365.2 [364.3–366.4] | 363.9 [361.7–364.4] | 65.3 [65.1–66.1] | 5.12s [5.12s–5.13s] | 549.1 [547.4–555.0] | 90.5 [90.2–90.9] | 4.03x | 4.02x | 0.72x | 56.6x | 6.07x |
| hashmap | data | 147.4 [146.3–150.5] | 119.8 [118.2–120.3] | 47.4 [47.4–47.9] | 1.79s [1.78s–1.81s] | 321.3 [316.9–323.6] | 58.6 [58.0–58.8] | 2.52x | 2.04x | 0.81x | 30.5x | 5.48x |
| crypto_sha1 | crypto | 82.1 [81.9–82.3] | 676.7 [676.0–683.0] | 48.0 [48.0–48.6] | 417.7 [416.8–418.1] | 222.9 [222.9–223.6] | 51.6 [51.1–51.9] | 1.59x | 13.1x | 0.93x | 8.09x | 4.32x |
| raytrace3d | 3d | 272.6 [272.4–274.4] | 234.4 [231.8–235.3] | 51.4 [51.1–51.7] | 684.9 [682.5–686.8] | 168.2 [168.1–168.7] | 61.5 [61.2–61.5] | 4.43x | 3.81x | 0.84x | 11.1x | 2.74x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 462.9 [462.5–468.9] | 358.7 [352.5–361.4] | 59.5 [59.3–59.5] | 2.09s [2.08s–2.10s] | 622.3 [619.9–628.3] | 84.5 [84.4–85.3] | 5.48x | 4.24x | 0.70x | 24.7x | 7.36x |
| microdiff | data-diff | 17.8 [17.8–17.9] | 17.8 [17.6–18.2] | 51.3 [44.8–91.5] | 1.25s [1.23s–1.25s] | 116.3 [114.5–116.5] | 61.0 [60.8–62.0] | 0.29x | 0.29x | 0.84x | 20.5x | 1.91x |
| hyphen | hyphenation | 27.6 [27.3–27.8] | 25.1 [24.7–25.2] | 46.3 [45.5–46.3] | 1.57s [1.57s–1.60s] | 68.1 [67.9–68.8] | 53.0 [53.0–54.5] | 0.52x | 0.47x | 0.87x | 29.6x | 1.28x |
| prettier_ast | formatting | 1.18s [1.18s–1.18s] | 10.31s [10.28s–10.33s] | 104.2 [104.0–111.3] | 5.93s [5.89s–6.01s] | 1.43s [1.42s–1.44s] | 146.7 [145.9–230.2] | 8.02x | 70.3x | 0.71x | 40.4x | 9.73x |
| text_search | search | 15.74s [15.71s–15.75s] | 3.11s [3.10s–3.16s] | 585.6 [584.7–585.7] | 114.31s [114.09s–114.54s] | 38.96s [38.96s–39.03s] | 830.3 [826.7–873.8] | 19.0x | 3.75x | 0.71x | 138x | 46.9x |
| three_way_merge | merge | 3.79s [3.78s–3.80s] | 31.84s [31.68s–32.32s] | 1.48s [1.48s–1.49s] | 19.84s [19.83s–19.99s] | 6.13s [6.08s–6.14s] | 1.01s [1.00s–1.01s] | 3.77x | 31.6x | 1.47x | 19.7x | 6.09x |
| log_pipeline | log-processing | 6.35s [6.34s–6.38s] | 21.15s [21.12s–21.31s] | 601.0 [599.3–608.0] | 18.69s [18.58s–18.73s] | 9.37s [9.36s–9.39s] | 994.7 [985.1–1.07s] | 6.38x | 21.3x | 0.60x | 18.8x | 9.42x |

