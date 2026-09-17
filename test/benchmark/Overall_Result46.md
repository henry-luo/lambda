# Lambda Benchmark Results: Result46

- **Date:** 2026-09-17
- **Platform:** Darwin arm64
- **Lambda commit:** `9697f433753f3fb2e7a3efc6fc705115c9acdb3b`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v46-9697f43375` (18,406,024 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 82.80s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 82.7s (batched 81.3s: sync 60.1s, async 21.2s; non-batched 1.4s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.1s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v46.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.32x | 0.30x | 0.17x | 1.06x | 6.08x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.30x | 0.65x | 0.08x | 13.2x | 4.77x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.35x | 0.30x | 0.09x | 4.77x | 1.66x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 1.36x | 0.76x | 0.20x | 17.7x | 11.0x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 1.34x | 0.69x | 0.29x | 14.8x | 12.4x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 4.42x | 2.54x | 0.29x | 28.5x | 11.8x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 6.74x | 4.03x | 0.44x | 40.9x | 12.5x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 63 | 1.20x | 0.75x | 0.18x | 10.00x | 6.83x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 59 | 0.67x | 1.05x | 4.24x | 59 |
| complete current suite | 63 | 0.75x | 1.20x | 4.26x | 63 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 4.26x over 63 of 63 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| text/hyphen | 94.8 | 1.73 | 54.9x |
| awfy/cd | 652.5 | 17.5 | 37.2x |
| awfy/deltablue | 41.2 | 1.30 | 31.7x |
| awfy/havlak | 74.9 | 2.80 | 26.7x |
| text/microdiff | 74.8 | 3.11 | 24.1x |
| beng/knucleotide | 5.70 | 0.331 | 17.2x |
| jetstream/cube3d | 8.65 | 0.517 | 16.7x |
| kostya/base64 | 10.6 | 0.644 | 16.5x |
| text/prettier_ast | 688.9 | 42.0 | 16.4x |
| jetstream/splay | 305.9 | 19.4 | 15.8x |
| r7rs/nqueens | 2.07 | 0.141 | 14.7x |
| awfy/richards | 483.3 | 33.6 | 14.4x |

---

### Notable Results

- Missing timings: **0** cells

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 22.28s | 149.3 | 149x |
| larceny/triangl | 10.31s | 85.6 | 120x |
| awfy/cd | 4.95s | 49.5 | 99.9x |
| awfy/nbody | 667.3 | 7.09 | 94.1x |
| jetstream/hashmap | 1.44s | 15.5 | 92.7x |
| text/hyphen | 843.2 | 9.14 | 92.2x |
| larceny/quicksort | 179.7 | 2.10 | 85.4x |
| text/text_search | 57.56s | 778.1 | 74.0x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| r7rs/sumfp | 0.075 | 1.48 | 0.05x |
| beng/pidigits | 0.632 | 2.43 | 0.26x |
| r7rs/tak | 0.343 | 0.844 | 0.41x |
| r7rs/sum | 0.739 | 1.53 | 0.48x |
| awfy/sieve | 0.262 | 0.477 | 0.55x |
| r7rs/cpstak | 0.712 | 1.27 | 0.56x |
| r7rs/fib | 1.79 | 2.20 | 0.81x |
| r7rs/ack | 13.7 | 15.2 | 0.90x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.57 [1.34–1.65] | 1.53 [1.42–1.58] | 1.39 [1.26–1.52] | 1.79 [1.75–2.04] | 23.6 [22.8–23.8] | 2.20 [1.99–10.4] | 0.71x | 0.70x | 0.63x | 0.81x | 10.7x |
| fibfp | recursive | 2.55 [2.31–2.57] | 1.37 [1.33–1.77] | 1.26 [1.20–1.39] | 1.85 [1.74–1.86] | 21.2 [21.2–23.0] | 2.02 [1.98–2.09] | 1.26x | 0.67x | 0.62x | 0.91x | 10.5x |
| tak | recursive | 0.129 [0.128–0.130] | 0.136 [0.132–0.136] | 0.129 [0.127–0.177] | 0.343 [0.333–0.375] | 3.15 [3.11–3.47] | 0.844 [0.827–0.964] | 0.15x | 0.16x | 0.15x | 0.41x | 3.73x |
| cpstak | closure | 0.263 [0.249–11.5] | 0.268 [0.268–0.268] | 0.252 [0.250–0.258] | 0.712 [0.664–0.719] | 6.90 [6.55–7.02] | 1.27 [1.23–1.34] | 0.21x | 0.21x | 0.20x | 0.56x | 5.44x |
| sum | iterative | 0.294 [0.287–0.294] | 0.301 [0.294–0.569] | 0.300 [0.297–0.301] | 0.739 [0.725–0.817] | 35.2 [34.9–37.2] | 1.53 [1.43–1.87] | 0.19x | 0.20x | 0.20x | 0.48x | 23.0x |
| sumfp | iterative | 0.074 [0.073–0.076] | 0.073 [0.073–0.076] | 0.086 [0.086–0.122] | 0.075 [0.075–0.075] | 4.13 [4.01–5.07] | 1.48 [1.23–2.30] | 0.05x | 0.05x | 0.06x | 0.05x | 2.79x |
| nqueens | backtrack | 1.66 [1.56–1.91] | 2.07 [2.02–2.25] | 0.141 [0.139–0.182] | 51.9 [45.3–54.0] | 8.95 [8.79–9.80] | 2.12 [2.04–2.79] | 0.78x | 0.98x | 0.07x | 24.5x | 4.22x |
| fft | numeric | 0.332 [0.318–0.335] | 0.188 [0.185–0.214] | 0.028 [0.028–0.029] | 6.20 [6.18–6.23] | 3.07 [2.98–3.26] | 2.00 [1.90–2.77] | 0.17x | 0.09x | 0.01x | 3.10x | 1.53x |
| mbrot | numeric | 0.775 [0.772–0.780] | 1.06 [1.02–1.08] | 0.496 [0.483–0.504] | 13.6 [13.4–14.6] | 19.9 [19.9–20.8] | 2.13 [2.09–2.34] | 0.36x | 0.50x | 0.23x | 6.39x | 9.35x |
| ack | recursive | 12.1 [11.9–13.5] | 11.3 [11.2–11.6] | 11.5 [11.3–12.5] | 13.7 [13.6–16.4] | 117.4 [115.5–159.4] | 15.2 [15.1–15.4] | 0.79x | 0.75x | 0.76x | 0.90x | 7.73x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.032 [0.032–0.033] | 0.037 [0.037–0.064] | 0.018 [0.017–0.018] | 0.262 [0.238–0.277] | 0.698 [0.674–0.702] | 0.477 [0.455–0.614] | 0.07x | 0.08x | 0.04x | 0.55x | 1.46x |
| permute | micro | 0.696 [0.685–0.707] | 0.105 [0.105–0.107] | 0.029 [0.028–0.029] | 7.16 [7.02–7.23] | 1.77 [1.76–1.88] | 1.10 [0.964–1.15] | 0.63x | 0.10x | 0.03x | 6.51x | 1.61x |
| queens | micro | 0.365 [0.364–0.369] | 0.132 [0.123–0.142] | 0.022 [0.021–0.026] | 4.48 [4.15–5.08] | 1.30 [1.26–1.55] | 0.803 [0.722–0.996] | 0.45x | 0.16x | 0.03x | 5.58x | 1.62x |
| towers | micro | 1.45 [1.41–1.48] | 0.361 [0.359–0.732] | 0.031 [0.030–0.031] | 16.8 [15.7–17.0] | 2.53 [2.47–2.55] | 1.32 [1.30–1.53] | 1.10x | 0.27x | 0.02x | 12.8x | 1.92x |
| bounce | micro | 0.082 [0.082–0.093] | 0.123 [0.114–0.156] | 0.027 [0.027–0.027] | 4.39 [3.95–4.43] | 1.00 [0.951–1.12] | 0.712 [0.679–0.887] | 0.12x | 0.17x | 0.04x | 6.16x | 1.40x |
| list | micro | 0.642 [0.630–0.768] | 0.147 [0.140–0.242] | 0.025 [0.025–0.026] | 2.60 [2.27–2.62] | 1.02 [0.972–1.23] | 0.588 [0.584–0.864] | 1.09x | 0.25x | 0.04x | 4.41x | 1.73x |
| storage | micro | 0.845 [0.840–0.927] | 0.414 [0.384–0.549] | 0.099 [0.096–0.102] | 5.53 [5.50–6.63] | 2.26 [2.24–2.26] | 0.739 [0.691–0.749] | 1.14x | 0.56x | 0.13x | 7.48x | 3.05x |
| mandelbrot | compute | 45.5 [44.7–86.2] | 45.9 [45.2–46.3] | 35.9 [35.1–37.1] | 220.4 [218.7–265.7] | 1.07s [1.03s–1.09s] | 36.4 [35.5–39.4] | 1.25x | 1.26x | 0.99x | 6.06x | 29.3x |
| nbody | compute | 61.3 [60.5–62.9] | 13.0 [12.9–13.8] | 1.70 [1.64–1.75] | 667.3 [665.5–682.9] | 199.1 [184.8–199.5] | 7.09 [6.62–7.50] | 8.65x | 1.84x | 0.24x | 94.1x | 28.1x |
| richards | macro | 467.9 [426.9–474.6] | 483.3 [474.1–541.3] | 33.6 [33.6–33.8] | 1.35s [1.35s–1.39s] | 232.7 [231.8–278.1] | 57.5 [56.1–57.6] | 8.14x | 8.41x | 0.58x | 23.5x | 4.05x |
| json | macro | 7.61 [7.30–12.3] | 3.45 [3.06–3.87] | 0.316 [0.301–0.481] | 48.4 [47.3–49.1] | 13.1 [12.7–13.1] | 3.84 [3.48–4.17] | 1.98x | 0.90x | 0.08x | 12.6x | 3.40x |
| deltablue | macro | 143.7 [142.5–196.3] | 41.2 [39.9–52.3] | 1.30 [1.25–1.31] | 624.0 [619.9–625.2] | 117.6 [117.3–119.7] | 17.0 [15.9–17.3] | 8.43x | 2.42x | 0.08x | 36.6x | 6.90x |
| havlak | macro | 86.0 [81.2–86.0] | 74.9 [72.3–131.9] | 2.80 [2.74–3.11] | 22.28s [22.19s–22.40s] | 4.45s [4.42s–4.52s] | 149.3 [144.8–210.6] | 0.58x | 0.50x | 0.02x | 149x | 29.8x |
| cd | macro | 773.2 [765.6–781.1] | 652.5 [603.5–653.2] | 17.5 [17.4–18.1] | 4.95s [4.93s–4.95s] | 1.24s [1.23s–1.26s] | 49.5 [47.3–51.3] | 15.6x | 13.2x | 0.35x | 99.9x | 25.1x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.64 [9.50–10.7] | 2.50 [2.44–2.87] | 3.30 [3.27–3.46] | 37.6 [37.5–37.9] | 27.3 [26.9–27.8] | 5.64 [5.30–6.74] | 1.71x | 0.44x | 0.59x | 6.67x | 4.84x |
| fannkuch | permutation | 0.356 [0.348–0.367] | 0.375 [0.373–0.390] | 0.187 [0.160–0.299] | 43.2 [42.5–46.1] | 16.1 [8.22–29.4] | 4.92 [4.60–5.49] | 0.07x | 0.08x | 0.04x | 8.78x | 3.27x |
| fasta | generation | 0.839 [0.809–1.24] | 1.03 [0.980–1.04] | 0.261 [0.260–0.277] | 35.0 [34.0–35.0] | 9.69 [9.62–9.79] | 7.74 [7.32–8.10] | 0.11x | 0.13x | 0.03x | 4.52x | 1.25x |
| knucleotide | hashing | 5.82 [5.07–6.53] | 5.70 [5.36–6.02] | 0.331 [0.316–0.365] | 26.2 [25.5–26.9] | 8.89 [8.66–9.33] | 6.26 [5.68–7.27] | 0.93x | 0.91x | 0.05x | 4.19x | 1.42x |
| pidigits | bignum | 0.423 [0.399–0.443] | 0.459 [0.406–0.520] | 0.053 [0.050–0.053] | 0.632 [0.604–0.864] | 0.140 [0.139–0.163] | 2.43 [2.18–2.88] | 0.17x | 0.19x | 0.02x | 0.26x | 0.06x |
| regexredux | regex | 1.49 [1.45–2.34] | 1.47 [1.45–1.99] | 1.36 [1.27–1.55] | 10.6 [10.4–11.2] | 6.79 [6.08–14.5] | 2.93 [2.84–3.56] | 0.51x | 0.50x | 0.46x | 3.60x | 2.32x |
| revcomp | string | 1.33 [1.30–1.33] | 1.28 [1.25–1.60] | 0.436 [0.435–0.439] | 30.9 [30.5–47.7] | 3.02 [2.83–3.08] | 4.60 [4.22–4.71] | 0.29x | 0.28x | 0.09x | 6.72x | 0.66x |
| spectralnorm | numeric | 1.98 [1.96–2.76] | 2.01 [2.01–2.20] | 0.381 [0.381–0.391] | 120.8 [120.6–179.4] | 73.7 [72.4–75.6] | 3.13 [2.80–3.58] | 0.63x | 0.64x | 0.12x | 38.7x | 23.6x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 242.0 [223.7–283.8] | 226.3 [223.8–292.3] | 32.5 [32.1–33.0] | 2.54s [2.48s–2.58s] | 1.10s [1.05s–1.29s] | 52.3 [49.6–67.7] | 4.63x | 4.33x | 0.62x | 48.6x | 21.1x |
| matmul | numeric | 7.91 [7.41–8.54] | 6.89 [6.75–7.59] | 6.88 [6.86–7.23] | 636.1 [584.0–641.8] | 696.2 [686.2–697.2] | 19.0 [18.9–19.1] | 0.42x | 0.36x | 0.36x | 33.6x | 36.7x |
| primes | numeric | 16.7 [16.3–17.1] | 2.95 [2.86–3.42] | 1.83 [1.70–2.27] | 119.2 [115.2–119.3] | 109.6 [109.1–112.2] | 5.14 [5.10–19.3] | 3.24x | 0.57x | 0.36x | 23.2x | 21.3x |
| base64 | string | 19.5 [19.4–19.8] | 10.6 [9.88–11.0] | 0.644 [0.622–0.710] | 684.7 [618.7–713.4] | 194.3 [194.0–205.6] | 21.5 [20.9–21.8] | 0.91x | 0.49x | 0.03x | 31.9x | 9.05x |
| levenshtein | string | 14.1 [12.6–26.6] | 7.39 [6.81–8.00] | 1.01 [0.973–1.74] | 260.1 [253.9–323.8] | 63.4 [63.1–65.1] | 5.28 [4.63–17.3] | 2.67x | 1.40x | 0.19x | 49.3x | 12.0x |
| json_gen | data | 31.5 [31.2–32.9] | 12.6 [11.7–13.0] | 1.71 [1.66–1.72] | 47.4 [46.1–48.0] | 23.7 [23.5–23.9] | 9.24 [7.35–9.83] | 3.41x | 1.36x | 0.18x | 5.13x | 2.56x |
| collatz | numeric | 320.2 [319.2–377.0] | 314.9 [308.8–372.2] | 257.2 [256.4–316.9] | 3.42s [3.34s–3.44s] | 7.93s [7.86s–8.10s] | 1.89s [1.82s–2.05s] | 0.17x | 0.17x | 0.14x | 1.81x | 4.19x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 304.4 [301.0–379.9] | 211.1 [208.9–287.5] | 75.7 [75.2–79.3] | 10.31s [10.13s–10.36s] | 2.81s [2.74s–2.86s] | 85.6 [84.9–87.5] | 3.56x | 2.47x | 0.88x | 120x | 32.8x |
| array1 | array | 0.900 [0.895–0.909] | 0.955 [0.908–1.15] | 0.362 [0.352–0.376] | 55.2 [51.6–69.0] | 42.1 [41.8–43.5] | 2.33 [2.21–2.47] | 0.39x | 0.41x | 0.16x | 23.7x | 18.1x |
| deriv | symbolic | 34.5 [34.1–35.2] | 8.49 [8.25–9.14] | 3.33 [3.31–3.41] | 95.9 [94.4–96.6] | 72.3 [68.9–72.6] | 5.38 [4.61–21.5] | 6.41x | 1.58x | 0.62x | 17.8x | 13.4x |
| diviter | iterative | 308.0 [306.8–308.7] | 311.2 [307.5–380.7] | 305.9 [305.8–306.5] | 16.26s [16.03s–16.26s] | 33.09s [32.99s–33.10s] | 547.5 [545.9–642.2] | 0.56x | 0.57x | 0.56x | 29.7x | 60.4x |
| divrec | recursive | 6.60 [6.44–7.12] | 1.37 [1.30–1.70] | 5.54 [5.51–5.81] | 16.7 [16.6–17.6] | 43.2 [40.9–43.3] | 8.86 [8.43–9.57] | 0.74x | 0.15x | 0.63x | 1.88x | 4.87x |
| gcbench | allocation | 231.7 [231.3–323.2] | 103.9 [99.8–227.1] | 85.0 [82.3–86.0] | 1.07s [1.06s–1.09s] | 679.9 [665.2–772.1] | 31.1 [30.9–135.1] | 7.45x | 3.34x | 2.73x | 34.5x | 21.9x |
| paraffins | combinat | 0.279 [0.265–0.281] | 0.251 [0.249–0.272] | 0.055 [0.053–0.057] | 4.35 [4.13–4.62] | 3.09 [2.86–3.20] | 1.27 [1.18–1.91] | 0.22x | 0.20x | 0.04x | 3.42x | 2.43x |
| pnpoly | numeric | 14.7 [14.5–90.3] | 15.4 [13.4–15.7] | 2.17 [2.02–2.18] | 165.6 [163.6–189.6] | 238.3 [236.7–325.7] | 7.19 [7.09–7.34] | 2.04x | 2.14x | 0.30x | 23.0x | 33.1x |
| puzzle | search | 23.0 [21.6–23.6] | 17.3 [15.7–18.3] | 1.45 [1.45–1.45] | 30.7 [30.1–31.0] | 35.1 [33.3–35.4] | 4.71 [4.13–4.94] | 4.88x | 3.67x | 0.31x | 6.51x | 7.45x |
| quicksort | sorting | 14.3 [13.5–14.6] | 0.861 [0.853–0.886] | 0.220 [0.219–0.226] | 179.7 [162.3–254.9] | 23.0 [22.3–24.1] | 2.10 [2.01–2.11] | 6.82x | 0.41x | 0.10x | 85.4x | 10.9x |
| ray | numeric | 0.306 [0.303–0.340] | 0.302 [0.301–0.314] | 0.190 [0.186–0.208] | 8.77 [8.59–9.37] | 16.3 [15.6–16.4] | 5.11 [4.83–6.60] | 0.06x | 0.06x | 0.04x | 1.72x | 3.19x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 10.0 [9.79–10.0] | 8.65 [8.62–8.73] | 0.517 [0.516–0.518] | 326.8 [326.3–329.4] | 216.7 [215.9–217.2] | 17.9 [17.7–18.1] | 0.56x | 0.48x | 0.03x | 18.3x | 12.1x |
| navier_stokes | numeric | 90.6 [90.5–90.6] | 90.5 [90.4–90.5] | 46.9 [46.9–46.9] | 213.9 [213.6–216.1] | 99.1 [99.1–99.1] | 14.2 [14.1–15.5] | 6.38x | 6.37x | 3.30x | 15.1x | 6.98x |
| splay | data | 317.9 [315.8–318.6] | 305.9 [305.2–309.9] | 19.4 [19.3–19.4] | 324.1 [321.5–326.7] | 146.2 [145.0–147.5] | 20.0 [18.2–20.3] | 15.9x | 15.3x | 0.97x | 16.2x | 7.29x |
| hashmap | data | 87.4 [86.6–176.6] | 39.6 [39.4–39.7] | 2.87 [2.86–2.89] | 1.44s [1.43s–1.44s] | 315.2 [312.8–319.4] | 15.5 [15.1–15.5] | 5.65x | 2.56x | 0.19x | 92.7x | 20.4x |
| crypto_sha1 | crypto | 53.0 [53.0–53.5] | 21.3 [21.3–24.1] | 2.62 [2.54–2.65] | 379.6 [378.5–380.0] | 218.9 [218.7–219.4] | 8.79 [8.77–8.83] | 6.03x | 2.43x | 0.30x | 43.2x | 24.9x |
| raytrace3d | 3d | 70.5 [70.2–93.7] | 16.7 [16.6–16.8] | 2.18 [2.17–2.24] | 546.1 [544.8–551.4] | 161.6 [161.2–161.9] | 18.2 [18.2–18.3] | 3.86x | 0.92x | 0.12x | 29.9x | 8.86x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 285.3 [285.1–390.3] | 164.6 [163.0–264.5] | 15.7 [15.2–16.1] | 3.03s [3.01s–3.11s] | 755.6 [741.7–838.3] | 56.4 [51.6–56.5] | 5.06x | 2.92x | 0.28x | 53.7x | 13.4x |
| microdiff | data-diff | 71.6 [68.4–79.3] | 74.8 [74.3–77.5] | 3.11 [3.01–3.38] | 1.35s [1.29s–1.35s] | 163.4 [134.5–225.9] | 22.3 [20.0–22.4] | 3.21x | 3.35x | 0.14x | 60.2x | 7.31x |
| hyphen | hyphenation | 96.1 [90.6–96.2] | 94.8 [94.1–209.0] | 1.73 [1.72–2.09] | 843.2 [739.9–847.6] | 101.6 [100.0–197.0] | 9.14 [8.54–9.67] | 10.5x | 10.4x | 0.19x | 92.2x | 11.1x |
| prettier_ast | formatting | 1.37s [1.17s–1.37s] | 688.9 [687.0–689.6] | 42.0 [41.9–42.3] | 4.79s [4.78s–4.93s] | 1.41s [1.41s–1.42s] | 100.8 [100.8–109.9] | 13.5x | 6.83x | 0.42x | 47.5x | 14.0x |
| text_search | search | 15.12s [15.11s–15.13s] | 1.74s [1.74s–1.74s] | 538.9 [538.3–539.6] | 57.56s [57.12s–57.92s] | 38.95s [38.73s–38.99s] | 778.1 [777.4–779.4] | 19.4x | 2.24x | 0.69x | 74.0x | 50.1x |
| three_way_merge | merge | 2.87s [2.83s–2.87s] | 2.33s [2.31s–2.35s] | 2.13s [2.13s–2.13s] | 12.26s [12.24s–12.30s] | 6.15s [6.15s–6.22s] | 967.4 [966.1–967.6] | 2.97x | 2.41x | 2.21x | 12.7x | 6.36x |
| log_pipeline | log-processing | 4.53s [4.50s–4.55s] | 4.42s [4.41s–4.43s] | 624.2 [623.0–626.2] | 13.62s [13.57s–13.71s] | 9.54s [9.48s–9.54s] | 948.2 [942.9–955.3] | 4.77x | 4.66x | 0.66x | 14.4x | 10.1x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.47x | 0.46x | 1.06x | 0.74x | 0.42x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.01x | 1.00x | 0.87x | 4.46x | 0.82x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.55x | 0.59x | 1.02x | 1.59x | 0.38x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 0.73x | 0.62x | 0.69x | 4.84x | 2.72x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 0.90x | 0.71x | 1.03x | 3.78x | 1.90x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 2.32x | 1.79x | 0.91x | 11.4x | 3.64x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 3.99x | 2.56x | 0.91x | 27.7x | 6.75x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 63 | 0.99x | 0.87x | 0.93x | 3.86x | 1.29x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 23.8 [23.1–25.2] | 24.3 [23.9–25.5] | 109.3 [66.7–532.5] | 34.0 [32.9–34.1] | 33.8 [33.7–35.1] | 65.4 [60.7–147.9] | 0.36x | 0.37x | 1.67x | 0.52x | 0.52x |
| fibfp | recursive | 26.6 [25.8–27.3] | 29.2 [25.8–83.6] | 64.9 [58.4–67.8] | 31.9 [31.4–33.1] | 30.3 [29.5–31.4] | 57.0 [54.5–57.5] | 0.47x | 0.51x | 1.14x | 0.56x | 0.53x |
| tak | recursive | 32.3 [23.0–54.9] | 21.6 [21.0–21.8] | 55.5 [54.5–57.4] | 27.0 [26.8–28.2] | 11.5 [11.3–11.7] | 53.8 [53.3–53.9] | 0.60x | 0.40x | 1.03x | 0.50x | 0.21x |
| cpstak | closure | 21.1 [20.9–39.8] | 21.2 [20.7–21.7] | 56.1 [55.4–58.4] | 30.5 [27.8–30.7] | 15.5 [14.5–15.6] | 56.0 [55.9–58.6] | 0.38x | 0.38x | 1.00x | 0.55x | 0.28x |
| sum | iterative | 23.3 [22.8–24.8] | 25.1 [23.6–25.1] | 61.2 [55.6–62.8] | 31.9 [30.5–35.5] | 44.6 [43.3–48.4] | 59.5 [58.6–111.5] | 0.39x | 0.42x | 1.03x | 0.54x | 0.75x |
| sumfp | iterative | 26.6 [23.5–29.0] | 23.2 [21.0–23.4] | 58.8 [55.0–59.1] | 30.3 [29.4–33.9] | 15.4 [14.6–15.4] | 60.4 [56.6–112.3] | 0.44x | 0.38x | 0.97x | 0.50x | 0.25x |
| nqueens | backtrack | 32.9 [32.5–33.8] | 38.5 [37.1–40.8] | 57.2 [56.8–57.6] | 164.7 [149.6–197.4] | 18.2 [18.1–18.2] | 58.8 [54.7–60.1] | 0.56x | 0.65x | 0.97x | 2.80x | 0.31x |
| fft | numeric | 29.6 [29.5–30.6] | 35.9 [34.5–37.3] | 56.0 [55.9–59.8] | 91.9 [91.6–144.4] | 12.1 [10.8–13.0] | 60.9 [60.4–64.8] | 0.49x | 0.59x | 0.92x | 1.51x | 0.20x |
| mbrot | numeric | 32.3 [30.4–32.8] | 33.0 [27.7–33.1] | 60.9 [55.0–113.1] | 55.5 [54.1–63.8] | 28.4 [28.1–29.5] | 56.7 [56.1–59.7] | 0.57x | 0.58x | 1.07x | 0.98x | 0.50x |
| ack | recursive | 35.1 [34.6–36.9] | 31.8 [30.7–34.1] | 66.5 [65.9–67.4] | 40.7 [39.9–43.8] | 126.4 [123.8–167.1] | 71.9 [69.0–77.4] | 0.49x | 0.44x | 0.92x | 0.57x | 1.76x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 22.2 [21.8–23.0] | 23.5 [22.7–23.7] | 56.4 [53.8–56.9] | 39.5 [38.5–40.3] | 9.39 [8.84–9.47] | 57.0 [54.6–61.0] | 0.39x | 0.41x | 0.99x | 0.69x | 0.16x |
| permute | micro | 24.2 [24.0–25.4] | 27.3 [25.5–27.3] | 58.3 [57.6–60.7] | 53.6 [51.7–55.4] | 12.4 [11.9–13.7] | 62.9 [61.3–109.7] | 0.38x | 0.43x | 0.93x | 0.85x | 0.20x |
| queens | micro | 33.1 [29.2–33.8] | 39.2 [37.3–41.4] | 62.2 [60.4–66.1] | 63.2 [59.8–109.5] | 10.5 [10.4–11.0] | 57.9 [57.9–58.6] | 0.57x | 0.68x | 1.07x | 1.09x | 0.18x |
| towers | micro | 31.1 [29.4–31.8] | 32.7 [30.5–33.4] | 58.7 [55.8–103.4] | 64.4 [63.7–67.8] | 11.8 [11.6–12.6] | 59.3 [58.8–60.4] | 0.53x | 0.55x | 0.99x | 1.09x | 0.20x |
| bounce | micro | 31.8 [30.9–33.8] | 35.9 [34.5–35.9] | 57.5 [56.4–61.5] | 198.4 [195.9–252.6] | 10.6 [10.1–13.2] | 59.9 [59.0–60.5] | 0.53x | 0.60x | 0.96x | 3.31x | 0.18x |
| list | micro | 29.7 [27.9–33.6] | 27.9 [27.8–29.4] | 58.2 [56.2–61.2] | 48.1 [48.0–49.0] | 10.4 [9.43–10.5] | 56.2 [54.4–57.1] | 0.53x | 0.50x | 1.04x | 0.85x | 0.19x |
| storage | micro | 25.7 [22.7–73.4] | 24.8 [23.7–26.4] | 56.7 [54.9–59.0] | 189.8 [187.1–240.7] | 10.8 [10.6–11.5] | 56.4 [55.1–56.8] | 0.46x | 0.44x | 1.01x | 3.37x | 0.19x |
| mandelbrot | compute | 69.0 [68.3–72.1] | 72.7 [69.5–115.5] | 93.5 [92.9–94.2] | 261.9 [260.1–306.7] | 1.08s [1.05s–1.09s] | 101.1 [91.5–110.1] | 0.68x | 0.72x | 0.92x | 2.59x | 10.6x |
| nbody | compute | 107.6 [104.5–107.7] | 86.0 [84.8–86.8] | 61.5 [59.0–67.7] | 759.9 [757.4–823.1] | 208.5 [192.4–236.4] | 65.4 [65.3–67.0] | 1.64x | 1.32x | 0.94x | 11.6x | 3.19x |
| richards | macro | 566.8 [547.7–567.5] | 609.7 [584.7–610.4] | 100.8 [95.7–149.2] | 1.51s [1.51s–1.56s] | 241.8 [241.2–289.2] | 114.0 [112.6–115.6] | 4.97x | 5.35x | 0.88x | 13.3x | 2.12x |
| json | macro | 70.8 [65.6–81.8] | 71.8 [69.3–72.4] | 72.4 [70.2–114.0] | 429.8 [412.4–461.6] | 23.3 [23.3–24.0] | 61.7 [60.5–112.9] | 1.15x | 1.16x | 1.17x | 6.97x | 0.38x |
| deltablue | macro | 336.4 [329.9–423.5] | 233.2 [229.0–276.0] | 65.1 [64.7–68.7] | 1.13s [1.11s–1.13s] | 128.4 [128.3–130.6] | 74.5 [70.2–75.1] | 4.51x | 3.13x | 0.87x | 15.2x | 1.72x |
| havlak | macro | 227.2 [227.1–279.0] | 220.3 [218.6–299.6] | 69.9 [67.4–70.5] | 22.95s [22.87s–23.09s] | 4.46s [4.43s–4.54s] | 271.8 [206.1–323.7] | 0.84x | 0.81x | 0.26x | 84.4x | 16.4x |
| cd | macro | 882.8 [877.6–895.6] | 781.0 [775.6–782.9] | 83.1 [82.5–92.6] | 5.48s [5.43s–5.51s] | 1.27s [1.25s–1.29s] | 107.3 [102.5–108.7] | 8.23x | 7.28x | 0.77x | 51.1x | 11.8x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 36.1 [33.2–85.7] | 32.9 [29.2–37.7] | 60.7 [59.9–63.0] | 76.6 [75.6–76.6] | 36.6 [35.5–37.1] | 58.2 [57.5–60.4] | 0.62x | 0.57x | 1.04x | 1.32x | 0.63x |
| fannkuch | permutation | 28.3 [27.9–29.7] | 32.8 [32.7–34.1] | 57.8 [55.5–59.0] | 131.7 [130.0–136.9] | 37.5 [17.1–54.5] | 57.6 [56.6–62.8] | 0.49x | 0.57x | 1.00x | 2.29x | 0.65x |
| fasta | generation | 34.3 [33.7–34.5] | 37.9 [37.0–41.1] | 56.4 [56.2–57.1] | 92.4 [88.3–152.4] | 18.3 [17.9–23.6] | 58.2 [58.0–62.5] | 0.59x | 0.65x | 0.97x | 1.59x | 0.32x |
| knucleotide | hashing | 39.5 [39.0–41.6] | 56.8 [55.5–125.2] | 59.3 [56.1–59.4] | 128.4 [77.7–527.9] | 16.8 [16.5–17.4] | 58.2 [56.7–61.0] | 0.68x | 0.98x | 1.02x | 2.21x | 0.29x |
| pidigits | bignum | 31.7 [30.7–38.1] | 31.3 [31.1–40.8] | 63.3 [58.7–67.4] | 45.9 [44.6–112.8] | 8.91 [8.18–8.98] | 56.8 [54.0–57.8] | 0.56x | 0.55x | 1.12x | 0.81x | 0.16x |
| regexredux | regex | 21.7 [20.3–22.4] | 21.1 [20.5–22.4] | 60.2 [58.9–63.2] | 66.2 [64.0–70.2] | 15.5 [13.8–66.4] | 58.1 [54.7–70.3] | 0.37x | 0.36x | 1.04x | 1.14x | 0.27x |
| revcomp | string | 32.1 [30.9–33.9] | 30.6 [30.5–31.6] | 57.7 [57.7–58.1] | 78.6 [76.8–150.5] | 11.7 [11.6–13.0] | 58.8 [56.2–59.7] | 0.55x | 0.52x | 0.98x | 1.34x | 0.20x |
| spectralnorm | numeric | 34.7 [34.2–35.5] | 40.7 [37.0–40.9] | 57.3 [55.1–59.7] | 181.6 [178.6–238.9] | 82.2 [81.2–84.6] | 58.5 [54.9–58.7] | 0.59x | 0.70x | 0.98x | 3.10x | 1.40x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 275.7 [253.4–320.8] | 265.1 [264.5–331.9] | 88.2 [88.0–88.3] | 2.59s [2.53s–2.62s] | 1.11s [1.06s–1.30s] | 129.1 [123.6–194.5] | 2.14x | 2.05x | 0.68x | 20.0x | 8.60x |
| matmul | numeric | 43.8 [41.8–46.0] | 42.0 [39.7–45.2] | 66.2 [63.3–68.6] | 677.4 [677.0–682.8] | 704.2 [694.5–706.8] | 72.4 [72.2–72.5] | 0.60x | 0.58x | 0.91x | 9.35x | 9.72x |
| primes | numeric | 41.3 [39.8–42.5] | 30.1 [28.3–30.7] | 61.7 [58.2–131.4] | 156.0 [150.5–156.2] | 119.5 [116.9–121.5] | 60.5 [59.7–128.5] | 0.68x | 0.50x | 1.02x | 2.58x | 1.97x |
| base64 | string | 48.9 [47.6–54.0] | 49.0 [48.5–49.6] | 57.9 [57.4–58.0] | 737.3 [731.6–759.0] | 204.1 [203.5–214.7] | 76.3 [75.9–77.8] | 0.64x | 0.64x | 0.76x | 9.66x | 2.68x |
| levenshtein | string | 53.0 [53.0–53.1] | 42.6 [40.9–43.9] | 57.9 [56.8–60.9] | 306.5 [302.1–374.5] | 71.3 [71.0–73.5] | 58.7 [58.0–119.7] | 0.90x | 0.73x | 0.99x | 5.22x | 1.21x |
| json_gen | data | 66.7 [64.5–67.6] | 46.8 [44.0–48.8] | 59.0 [57.1–124.9] | 89.9 [89.5–90.0] | 31.8 [31.8–33.0] | 62.3 [61.2–63.8] | 1.07x | 0.75x | 0.95x | 1.44x | 0.51x |
| collatz | numeric | 399.8 [335.6–406.6] | 343.0 [336.4–405.3] | 311.3 [309.9–370.4] | 3.45s [3.37s–3.47s] | 7.94s [7.86s–8.13s] | 1.95s [1.89s–2.11s] | 0.20x | 0.18x | 0.16x | 1.77x | 4.07x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 336.2 [333.3–421.4] | 288.1 [249.6–306.0] | 136.1 [134.9–140.8] | 10.38s [10.20s–10.42s] | 2.82s [2.75s–2.87s] | 140.0 [138.9–144.6] | 2.40x | 2.06x | 0.97x | 74.1x | 20.1x |
| array1 | array | 24.5 [23.1–26.5] | 24.7 [24.2–25.6] | 59.1 [56.9–59.3] | 89.5 [87.8–163.5] | 52.0 [50.9–53.5] | 59.0 [58.0–59.2] | 0.42x | 0.42x | 1.00x | 1.52x | 0.88x |
| deriv | symbolic | 71.0 [69.8–74.4] | 39.6 [37.8–115.0] | 64.1 [62.0–64.9] | 134.4 [131.4–136.2] | 80.8 [78.0–81.7] | 61.8 [60.7–143.1] | 1.15x | 0.64x | 1.04x | 2.17x | 1.31x |
| diviter | iterative | 358.1 [337.5–409.3] | 331.2 [330.2–403.2] | 365.0 [361.7–438.1] | 16.29s [16.06s–16.31s] | 33.10s [33.00s–33.10s] | 603.4 [599.9–696.3] | 0.59x | 0.55x | 0.60x | 27.0x | 54.9x |
| divrec | recursive | 50.5 [29.9–105.4] | 25.4 [24.6–26.9] | 62.1 [61.2–62.9] | 49.4 [48.8–50.8] | 53.3 [49.8–53.5] | 63.8 [63.1–63.9] | 0.79x | 0.40x | 0.97x | 0.77x | 0.84x |
| gcbench | allocation | 264.7 [263.7–271.9] | 161.5 [160.5–168.1] | 143.6 [142.3–146.4] | 1.11s [1.11s–1.13s] | 696.0 [681.4–788.6] | 89.2 [86.0–190.3] | 2.97x | 1.81x | 1.61x | 12.5x | 7.80x |
| paraffins | combinat | 38.8 [37.3–43.6] | 39.4 [38.7–42.1] | 59.6 [55.0–59.7] | 106.8 [104.6–107.2] | 11.9 [11.0–12.5] | 57.8 [57.1–59.5] | 0.67x | 0.68x | 1.03x | 1.85x | 0.21x |
| pnpoly | numeric | 43.6 [42.0–62.9] | 42.2 [41.8–43.5] | 62.1 [61.2–69.2] | 217.6 [213.3–242.7] | 247.5 [245.9–334.7] | 61.0 [58.4–62.8] | 0.71x | 0.69x | 1.02x | 3.56x | 4.05x |
| puzzle | search | 52.0 [51.8–57.0] | 49.3 [48.7–50.8] | 85.9 [59.2–127.3] | 83.1 [82.2–84.0] | 44.2 [43.7–44.6] | 59.1 [56.3–64.1] | 0.88x | 0.83x | 1.45x | 1.41x | 0.75x |
| quicksort | sorting | 44.0 [43.1–45.0] | 32.1 [31.1–32.8] | 57.9 [57.4–58.9] | 231.2 [215.4–306.5] | 32.7 [32.3–34.8] | 59.2 [59.1–63.4] | 0.74x | 0.54x | 0.98x | 3.91x | 0.55x |
| ray | numeric | 40.6 [38.7–43.7] | 32.6 [30.7–33.6] | 58.0 [57.4–104.6] | 61.9 [61.2–120.0] | 25.3 [24.6–26.8] | 62.6 [59.2–65.4] | 0.65x | 0.52x | 0.93x | 0.99x | 0.40x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 106.1 [106.1–106.7] | 111.2 [110.5–111.3] | 50.0 [49.2–50.5] | 728.6 [723.3–807.4] | 223.8 [222.8–224.0] | 64.5 [63.5–64.9] | 1.64x | 1.72x | 0.77x | 11.3x | 3.47x |
| navier_stokes | numeric | 171.2 [170.6–172.0] | 170.7 [170.5–171.4] | 95.1 [94.7–95.4] | 402.7 [402.2–406.0] | 110.2 [110.1–110.5] | 61.1 [60.5–61.4] | 2.80x | 2.79x | 1.56x | 6.59x | 1.80x |
| splay | data | 384.9 [384.2–385.6] | 372.1 [369.1–375.9] | 67.0 [67.0–67.1] | 975.6 [969.7–980.0] | 559.7 [556.2–564.6] | 93.8 [91.9–94.1] | 4.10x | 3.97x | 0.71x | 10.4x | 5.97x |
| hashmap | data | 126.7 [126.2–127.9] | 75.5 [75.1–75.6] | 48.6 [48.2–48.6] | 1.54s [1.54s–1.54s] | 322.5 [320.0–326.7] | 59.5 [59.3–59.6] | 2.13x | 1.27x | 0.82x | 25.9x | 5.42x |
| crypto_sha1 | crypto | 85.4 [85.4–85.8] | 53.5 [53.4–53.6] | 49.1 [48.9–49.6] | 448.2 [447.4–449.8] | 225.8 [225.4–226.1] | 52.8 [52.7–53.2] | 1.62x | 1.01x | 0.93x | 8.49x | 4.28x |
| raytrace3d | 3d | 148.4 [147.0–148.4] | 84.3 [84.2–85.0] | 52.6 [52.5–52.8] | 794.1 [784.4–859.5] | 168.5 [168.3–168.6] | 62.3 [61.8–62.4] | 2.38x | 1.35x | 0.84x | 12.7x | 2.70x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 322.4 [318.0–324.0] | 199.0 [198.3–199.8] | 73.4 [71.1–73.6] | 3.55s [3.42s–3.55s] | 766.0 [751.4–848.0] | 113.8 [109.6–114.7] | 2.83x | 1.75x | 0.65x | 31.2x | 6.73x |
| microdiff | data-diff | 120.8 [116.2–123.9] | 128.3 [124.7–196.6] | 66.9 [65.7–108.3] | 1.44s [1.37s–1.44s] | 172.9 [144.2–234.4] | 79.1 [76.6–80.1] | 1.53x | 1.62x | 0.85x | 18.2x | 2.19x |
| hyphen | hyphenation | 163.5 [163.1–167.7] | 155.7 [155.7–166.3] | 98.2 [98.0–102.1] | 4.37s [4.29s–4.48s] | 129.1 [127.3–223.4] | 79.5 [75.8–80.4] | 2.06x | 1.96x | 1.23x | 54.9x | 1.62x |
| prettier_ast | formatting | 1.09s [1.09s–1.31s] | 837.4 [813.5–885.7] | 105.0 [104.6–105.3] | 5.00s [4.99s–5.14s] | 1.42s [1.42s–1.43s] | 146.2 [146.1–263.9] | 7.48x | 5.73x | 0.72x | 34.2x | 9.75x |
| text_search | search | 15.13s [15.12s–15.23s] | 1.77s [1.77s–1.79s] | 585.5 [585.5–587.1] | 57.64s [57.19s–57.99s] | 38.96s [38.74s–39.00s] | 822.7 [822.2–827.1] | 18.4x | 2.15x | 0.71x | 70.1x | 47.4x |
| three_way_merge | merge | 2.92s [2.87s–2.92s] | 2.39s [2.36s–2.41s] | 2.18s [2.18s–2.19s] | 12.30s [12.28s–12.34s] | 6.16s [6.16s–6.23s] | 1.01s [1.01s–1.01s] | 2.88x | 2.37x | 2.16x | 12.1x | 6.08x |
| log_pipeline | log-processing | 4.57s [4.57s–4.58s] | 4.47s [4.46s–4.49s] | 673.3 [672.4–675.2] | 13.72s [13.67s–13.81s] | 9.57s [9.51s–9.57s] | 1.00s [998.1–1.01s] | 4.56x | 4.46x | 0.67x | 13.7x | 9.55x |

