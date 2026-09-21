# Lambda Benchmark Results: Result48

- **Date:** 2026-09-22
- **Platform:** Darwin arm64
- **Lambda commit:** `0e6f8abd892734614123d70e079504a2c34f8016`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v48-0e6f8abd89` (18,752,840 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 60.00s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 59.9s (batched 58.9s: sync 40.4s, async 18.5s; non-batched 1.0s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v48.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.33x | 0.28x | 0.21x | 1.02x | 6.54x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.04x | 0.54x | 0.09x | 17.6x | 5.11x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.39x | 0.24x | 0.10x | 4.35x | 1.74x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 1.45x | 0.74x | 0.23x | 11.7x | 11.9x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 1.03x | 0.63x | 0.32x | 8.05x | 13.2x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 4.07x | 2.24x | 0.29x | 30.6x | 11.9x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 4.75x | 2.51x | 0.47x | 27.1x | 12.7x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 63 | 1.07x | 0.63x | 0.20x | 8.65x | 7.20x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 59 | 0.57x | 0.98x | 3.21x | 59 |
| complete current suite | 63 | 0.63x | 1.07x | 3.23x | 63 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 3.23x over 63 of 63 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/havlak | 58.6 | 1.82 | 32.3x |
| awfy/deltablue | 26.1 | 1.17 | 22.3x |
| text/microdiff | 58.9 | 2.67 | 22.1x |
| jetstream/splay | 328.2 | 19.1 | 17.2x |
| text/prettier_ast | 612.7 | 42.2 | 14.5x |
| jetstream/cube3d | 7.24 | 0.509 | 14.2x |
| awfy/cd | 180.1 | 15.0 | 12.0x |
| awfy/towers | 0.322 | 0.027 | 11.9x |
| awfy/richards | 337.2 | 29.7 | 11.4x |
| larceny/puzzle | 13.8 | 1.27 | 10.8x |
| kostya/base64 | 5.81 | 0.555 | 10.5x |
| jetstream/hashmap | 26.4 | 2.74 | 9.63x |

---

### Notable Results

- Missing timings: **0** cells

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/cd | 18.11s | 36.5 | 496x |
| awfy/havlak | 48.54s | 104.3 | 465x |
| jetstream/hashmap | 3.46s | 15.3 | 226x |
| awfy/nbody | 586.7 | 5.51 | 106x |
| larceny/triangl | 6.19s | 69.0 | 89.7x |
| awfy/deltablue | 890.3 | 11.6 | 76.7x |
| text/text_search | 55.23s | 776.4 | 71.1x |
| kostya/brainfuck | 1.79s | 35.0 | 51.1x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| r7rs/sumfp | 0.067 | 0.882 | 0.08x |
| beng/pidigits | 0.525 | 1.79 | 0.29x |
| r7rs/tak | 0.308 | 0.783 | 0.39x |
| awfy/sieve | 0.212 | 0.387 | 0.55x |
| r7rs/sum | 0.669 | 1.17 | 0.57x |
| r7rs/cpstak | 0.625 | 0.970 | 0.64x |
| r7rs/ack | 11.4 | 13.3 | 0.86x |
| r7rs/fibfp | 1.56 | 1.79 | 0.87x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.30 [1.28–1.30] | 1.31 [1.29–1.31] | 1.14 [1.03–1.17] | 1.57 [1.57–1.61] | 19.3 [19.3–19.3] | 1.80 [1.78–7.86] | 0.72x | 0.73x | 0.63x | 0.87x | 10.8x |
| fibfp | recursive | 2.10 [2.10–2.18] | 1.19 [1.19–1.19] | 1.13 [1.08–1.13] | 1.56 [1.52–1.56] | 18.6 [18.4–18.7] | 1.79 [1.75–1.81] | 1.17x | 0.67x | 0.63x | 0.87x | 10.4x |
| tak | recursive | 0.121 [0.120–0.122] | 0.124 [0.121–0.127] | 0.110 [0.109–0.134] | 0.308 [0.308–0.315] | 2.83 [2.77–2.83] | 0.783 [0.770–0.787] | 0.15x | 0.16x | 0.14x | 0.39x | 3.62x |
| cpstak | closure | 0.249 [0.239–0.254] | 0.242 [0.242–0.246] | 0.230 [0.216–0.231] | 0.625 [0.619–0.636] | 5.62 [5.53–5.68] | 0.970 [0.968–0.972] | 0.26x | 0.25x | 0.24x | 0.64x | 5.80x |
| sum | iterative | 0.266 [0.265–0.272] | 0.278 [0.276–0.284] | 0.268 [0.267–0.269] | 0.669 [0.669–0.671] | 31.1 [31.1–31.3] | 1.17 [1.17–1.57] | 0.23x | 0.24x | 0.23x | 0.57x | 26.6x |
| sumfp | iterative | 0.069 [0.068–0.070] | 0.069 [0.068–0.069] | 0.078 [0.077–0.079] | 0.067 [0.067–0.067] | 3.64 [3.63–3.73] | 0.882 [0.864–1.33] | 0.08x | 0.08x | 0.09x | 0.08x | 4.12x |
| nqueens | backtrack | 0.979 [0.975–1.00] | 1.30 [1.28–1.35] | 0.359 [0.356–0.366] | 29.2 [29.0–29.6] | 7.90 [7.75–7.92] | 1.78 [1.72–2.43] | 0.55x | 0.73x | 0.20x | 16.4x | 4.44x |
| fft | numeric | 0.250 [0.246–0.254] | 0.092 [0.092–0.093] | 0.025 [0.024–0.026] | 2.72 [2.70–2.76] | 2.74 [2.73–2.74] | 1.55 [1.54–1.56] | 0.16x | 0.06x | 0.02x | 1.75x | 1.77x |
| mbrot | numeric | 0.729 [0.727–0.777] | 0.545 [0.544–0.557] | 0.442 [0.442–0.442] | 11.0 [11.0–11.1] | 17.7 [17.5–17.8] | 1.90 [1.88–1.91] | 0.38x | 0.29x | 0.23x | 5.77x | 9.30x |
| ack | recursive | 10.8 [10.7–11.1] | 9.93 [9.87–10.1] | 11.7 [10.1–11.7] | 11.4 [11.4–14.3] | 101.1 [100.7–102.2] | 13.3 [13.2–13.4] | 0.81x | 0.74x | 0.88x | 0.86x | 7.57x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.026 [0.026–0.026] | 0.027 [0.027–0.028] | 0.016 [0.015–0.017] | 0.212 [0.211–0.216] | 0.603 [0.602–0.623] | 0.387 [0.382–0.388] | 0.07x | 0.07x | 0.04x | 0.55x | 1.56x |
| permute | micro | 0.272 [0.272–0.273] | 0.080 [0.078–0.081] | 0.025 [0.024–0.026] | 5.91 [5.83–5.95] | 1.55 [1.52–1.56] | 0.814 [0.805–0.946] | 0.33x | 0.10x | 0.03x | 7.27x | 1.91x |
| queens | micro | 0.153 [0.153–0.162] | 0.065 [0.064–0.067] | 0.019 [0.018–0.021] | 3.29 [3.26–3.45] | 1.05 [1.04–1.05] | 0.654 [0.646–0.656] | 0.23x | 0.10x | 0.03x | 5.02x | 1.60x |
| towers | micro | 0.874 [0.869–0.879] | 0.322 [0.320–0.342] | 0.027 [0.027–0.030] | 18.1 [18.1–18.2] | 2.24 [2.21–2.32] | 1.14 [1.10–1.14] | 0.77x | 0.28x | 0.02x | 15.9x | 1.96x |
| bounce | micro | 0.065 [0.064–0.065] | 0.097 [0.096–0.097] | 0.024 [0.023–0.024] | 6.10 [6.03–6.16] | 0.875 [0.873–0.884] | 0.551 [0.540–0.797] | 0.12x | 0.18x | 0.04x | 11.1x | 1.59x |
| list | micro | 0.605 [0.593–0.630] | 0.129 [0.129–0.140] | 0.022 [0.021–0.023] | 2.64 [2.58–2.65] | 0.908 [0.905–0.910] | 0.493 [0.485–0.498] | 1.23x | 0.26x | 0.04x | 5.36x | 1.84x |
| storage | micro | 0.586 [0.582–0.624] | 0.356 [0.341–0.357] | 0.091 [0.090–0.098] | 4.24 [4.19–4.34] | 2.09 [2.07–2.10] | 0.644 [0.634–0.661] | 0.91x | 0.55x | 0.14x | 6.58x | 3.24x |
| mandelbrot | compute | 39.6 [39.5–39.6] | 39.5 [39.5–39.5] | 30.6 [30.5–30.6] | 79.7 [79.6–79.7] | 871.3 [866.9–880.5] | 31.2 [31.0–31.2] | 1.27x | 1.27x | 0.98x | 2.56x | 27.9x |
| nbody | compute | 6.93 [6.87–7.00] | 3.42 [3.40–3.51] | 1.47 [1.46–1.47] | 586.7 [584.1–588.7] | 159.3 [158.8–159.9] | 5.51 [5.38–5.89] | 1.26x | 0.62x | 0.27x | 106x | 28.9x |
| richards | macro | 359.3 [358.5–360.7] | 337.2 [335.6–355.8] | 29.7 [29.5–30.2] | 969.9 [967.6–971.1] | 191.0 [190.8–191.1] | 46.3 [46.2–46.8] | 7.75x | 7.28x | 0.64x | 20.9x | 4.12x |
| json | macro | 6.08 [6.07–6.28] | 2.47 [2.45–2.50] | 0.262 [0.255–0.263] | 58.6 [58.5–59.0] | 10.7 [10.7–10.8] | 2.66 [2.64–2.80] | 2.29x | 0.93x | 0.10x | 22.1x | 4.05x |
| deltablue | macro | 123.5 [122.9–123.5] | 26.1 [26.0–26.1] | 1.17 [1.15–1.20] | 890.3 [883.5–901.9] | 98.8 [98.5–99.3] | 11.6 [11.5–12.2] | 10.6x | 2.24x | 0.10x | 76.7x | 8.51x |
| havlak | macro | 70.0 [69.9–71.2] | 58.6 [58.5–60.5] | 1.82 [1.82–1.85] | 48.54s [48.46s–48.61s] | 3.26s [3.26s–3.30s] | 104.3 [92.8–104.8] | 0.67x | 0.56x | 0.02x | 465x | 31.3x |
| cd | macro | 571.5 [570.7–614.1] | 180.1 [180.1–180.7] | 15.0 [14.9–15.0] | 18.11s [18.02s–18.13s] | 961.8 [957.0–994.7] | 36.5 [36.1–43.8] | 15.7x | 4.93x | 0.41x | 496x | 26.3x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 8.38 [8.36–8.71] | 2.06 [2.05–2.09] | 3.02 [3.00–3.06] | 19.7 [19.4–20.2] | 23.3 [23.3–23.5] | 4.01 [3.89–4.52] | 2.09x | 0.52x | 0.75x | 4.90x | 5.82x |
| fannkuch | permutation | 0.308 [0.304–0.312] | 0.365 [0.357–0.383] | 0.149 [0.148–0.149] | 21.3 [21.2–21.6] | 7.16 [7.09–7.22] | 3.88 [3.87–3.92] | 0.08x | 0.09x | 0.04x | 5.50x | 1.85x |
| fasta | generation | 0.766 [0.755–0.773] | 0.909 [0.887–0.936] | 0.240 [0.240–0.241] | 27.2 [27.0–27.5] | 8.88 [8.50–8.91] | 6.04 [5.82–6.17] | 0.13x | 0.15x | 0.04x | 4.51x | 1.47x |
| knucleotide | hashing | 4.74 [4.74–5.18] | 0.587 [0.572–0.610] | 0.280 [0.279–0.290] | 21.1 [21.1–37.2] | 7.67 [7.64–7.76] | 4.98 [4.82–5.28] | 0.95x | 0.12x | 0.06x | 4.24x | 1.54x |
| pidigits | bignum | 0.358 [0.358–0.363] | 0.346 [0.343–0.358] | 0.046 [0.045–0.049] | 0.525 [0.514–0.594] | 0.128 [0.125–0.130] | 1.79 [1.79–1.82] | 0.20x | 0.19x | 0.03x | 0.29x | 0.07x |
| regexredux | regex | 1.27 [1.26–1.38] | 1.27 [1.27–1.33] | 1.13 [1.13–1.13] | 8.87 [8.86–8.93] | 5.53 [5.52–5.62] | 2.31 [2.26–2.36] | 0.55x | 0.55x | 0.49x | 3.84x | 2.40x |
| revcomp | string | 1.20 [1.18–1.22] | 1.14 [1.11–1.17] | 0.384 [0.374–0.389] | 22.7 [22.6–42.7] | 2.62 [2.49–2.80] | 3.30 [3.18–3.42] | 0.37x | 0.35x | 0.12x | 6.89x | 0.79x |
| spectralnorm | numeric | 1.64 [1.63–1.70] | 0.786 [0.785–0.786] | 0.351 [0.350–0.352] | 83.6 [83.3–83.8] | 64.7 [63.8–64.9] | 2.59 [2.57–2.90] | 0.63x | 0.30x | 0.14x | 32.3x | 25.0x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 198.1 [193.8–198.3] | 198.9 [197.9–217.4] | 28.1 [27.9–28.9] | 1.79s [1.78s–1.82s] | 888.9 [886.7–894.4] | 35.0 [34.3–35.2] | 5.66x | 5.69x | 0.80x | 51.1x | 25.4x |
| matmul | numeric | 5.18 [5.18–5.19] | 5.08 [5.07–5.12] | 6.02 [6.01–6.02] | 249.7 [248.6–251.3] | 539.0 [539.0–550.7] | 15.6 [15.6–15.8] | 0.33x | 0.33x | 0.39x | 16.0x | 34.5x |
| primes | numeric | 14.8 [14.8–15.0] | 2.32 [2.30–2.38] | 1.64 [1.58–1.69] | 57.3 [57.2–57.4] | 94.9 [94.8–95.3] | 4.36 [4.27–4.41] | 3.39x | 0.53x | 0.38x | 13.1x | 21.8x |
| base64 | string | 17.6 [17.3–17.7] | 5.81 [5.73–5.82] | 0.555 [0.555–0.557] | 490.6 [490.0–498.3] | 159.5 [156.6–160.3] | 17.6 [17.5–18.6] | 1.00x | 0.33x | 0.03x | 27.9x | 9.06x |
| levenshtein | string | 10.1 [10.1–10.3] | 4.88 [4.83–4.92] | 0.902 [0.899–0.909] | 82.3 [82.0–83.3] | 54.1 [54.0–54.9] | 3.93 [3.91–3.96] | 2.57x | 1.24x | 0.23x | 21.0x | 13.8x |
| json_gen | data | 26.7 [26.2–27.2] | 9.69 [9.51–9.79] | 1.52 [1.51–1.56] | 27.1 [26.9–27.3] | 19.9 [19.6–19.9] | 6.23 [6.22–6.52] | 4.29x | 1.56x | 0.24x | 4.35x | 3.20x |
| collatz | numeric | 275.4 [275.0–278.5] | 274.3 [274.0–276.7] | 225.5 [225.4–246.3] | 1.60s [1.60s–1.61s] | 6.25s [6.22s–6.27s] | 1.43s [1.42s–1.45s] | 0.19x | 0.19x | 0.16x | 1.12x | 4.38x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 232.9 [231.7–233.2] | 166.6 [165.6–166.9] | 60.2 [60.1–60.4] | 6.19s [6.19s–6.25s] | 2.19s [2.18s–2.20s] | 69.0 [68.9–87.0] | 3.38x | 2.41x | 0.87x | 89.7x | 31.7x |
| array1 | array | 0.809 [0.806–0.813] | 0.809 [0.809–0.827] | 0.315 [0.315–0.315] | 21.9 [21.9–22.0] | 36.2 [35.8–36.3] | 1.89 [1.80–2.07] | 0.43x | 0.43x | 0.17x | 11.6x | 19.1x |
| deriv | symbolic | 29.5 [28.8–29.6] | 6.90 [6.90–6.96] | 2.88 [2.85–2.90] | 85.4 [85.4–86.2] | 59.4 [58.9–59.4] | 3.77 [3.72–5.13] | 7.82x | 1.83x | 0.76x | 22.6x | 15.7x |
| diviter | iterative | 266.4 [265.9–284.4] | 268.1 [266.3–284.6] | 266.2 [265.9–266.3] | 613.7 [612.9–613.7] | 26.65s [26.65s–26.74s] | 470.7 [470.0–486.7] | 0.57x | 0.57x | 0.57x | 1.30x | 56.6x |
| divrec | recursive | 5.53 [5.50–5.57] | 1.21 [1.20–1.28] | 4.90 [4.87–4.94] | 15.8 [15.0–15.8] | 37.1 [36.3–37.5] | 7.70 [7.64–7.86] | 0.72x | 0.16x | 0.64x | 2.05x | 4.82x |
| gcbench | allocation | 196.0 [194.7–196.1] | 75.9 [75.7–76.0] | 70.4 [70.4–70.4] | 504.6 [504.6–505.1] | 568.7 [558.4–576.8] | 23.1 [23.1–24.3] | 8.49x | 3.29x | 3.05x | 21.9x | 24.6x |
| paraffins | combinat | 0.191 [0.190–0.869] | 0.157 [0.156–0.157] | 0.050 [0.048–0.050] | 2.28 [2.28–2.82] | 2.54 [2.50–2.58] | 1.00 [0.953–1.09] | 0.19x | 0.16x | 0.05x | 2.28x | 2.54x |
| pnpoly | numeric | 11.7 [11.7–11.8] | 4.17 [4.16–4.20] | 1.93 [1.91–1.96] | 107.9 [107.1–108.5] | 202.7 [201.6–203.1] | 5.82 [5.80–6.49] | 2.02x | 0.72x | 0.33x | 18.5x | 34.8x |
| puzzle | search | 12.9 [12.7–13.1] | 13.8 [13.6–13.8] | 1.27 [1.27–1.27] | 30.5 [30.0–30.7] | 29.3 [29.2–29.4] | 3.35 [3.29–3.85] | 3.86x | 4.10x | 0.38x | 9.10x | 8.74x |
| quicksort | sorting | 0.712 [0.712–0.750] | 0.675 [0.672–0.711] | 0.197 [0.197–0.197] | 18.0 [17.9–18.0] | 19.1 [19.1–19.2] | 1.64 [1.63–2.20] | 0.44x | 0.41x | 0.12x | 11.0x | 11.7x |
| ray | numeric | 0.211 [0.209–0.213] | 0.209 [0.208–0.216] | 0.170 [0.168–0.171] | 5.80 [5.78–5.88] | 13.8 [13.7–13.8] | 3.65 [3.62–17.7] | 0.06x | 0.06x | 0.05x | 1.59x | 3.78x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 8.81 [8.65–9.85] | 7.24 [7.09–7.37] | 0.509 [0.508–0.515] | 315.3 [313.9–317.0] | 216.2 [214.9–216.4] | 17.8 [17.7–19.5] | 0.49x | 0.41x | 0.03x | 17.7x | 12.1x |
| navier_stokes | numeric | 86.7 [86.5–87.2] | 69.6 [69.5–69.6] | 46.8 [46.7–46.8] | 184.0 [183.6–184.3] | 98.7 [98.5–117.8] | 14.3 [13.9–14.5] | 6.07x | 4.88x | 3.27x | 12.9x | 6.91x |
| splay | data | 343.2 [342.3–345.8] | 328.2 [327.7–328.8] | 19.1 [19.1–19.2] | 356.4 [355.6–363.0] | 146.1 [145.0–146.5] | 20.1 [19.8–21.0] | 17.1x | 16.3x | 0.95x | 17.7x | 7.27x |
| hashmap | data | 72.9 [72.0–75.3] | 26.4 [26.4–26.9] | 2.74 [2.73–3.00] | 3.46s [3.45s–3.46s] | 314.8 [313.8–315.0] | 15.3 [15.2–15.6] | 4.77x | 1.73x | 0.18x | 226x | 20.6x |
| crypto_sha1 | crypto | 43.1 [42.6–43.4] | 23.8 [23.7–24.2] | 2.67 [2.64–2.70] | 265.1 [263.5–265.7] | 218.3 [218.3–218.5] | 8.78 [8.76–8.80] | 4.91x | 2.72x | 0.30x | 30.2x | 24.9x |
| raytrace3d | 3d | 69.4 [67.7–87.5] | 15.0 [14.8–15.0] | 2.16 [2.16–2.17] | 540.8 [539.5–544.0] | 162.9 [162.8–186.1] | 18.3 [18.1–18.7] | 3.79x | 0.82x | 0.12x | 29.6x | 8.91x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 246.0 [245.9–246.9] | 53.4 [53.1–53.4] | 13.0 [12.9–13.0] | 1.96s [1.94s–1.97s] | 613.2 [604.1–616.0] | 39.5 [39.3–42.4] | 6.23x | 1.35x | 0.33x | 49.8x | 15.5x |
| microdiff | data-diff | 53.0 [52.9–53.2] | 58.9 [58.8–59.4] | 2.67 [2.64–2.74] | 844.5 [827.9–855.5] | 108.9 [108.4–110.1] | 16.7 [16.2–16.8] | 3.18x | 3.53x | 0.16x | 50.6x | 6.53x |
| hyphen | hyphenation | 69.8 [69.5–70.2] | 13.4 [13.2–13.5] | 1.47 [1.46–1.52] | 96.1 [96.1–96.2] | 79.9 [79.8–80.4] | 6.84 [6.60–6.90] | 10.2x | 1.95x | 0.21x | 14.0x | 11.7x |
| prettier_ast | formatting | 968.5 [967.9–974.2] | 612.7 [611.1–612.8] | 42.2 [41.7–47.8] | 2.60s [2.58s–2.61s] | 1.42s [1.42s–1.43s] | 100.0 [99.5–108.2] | 9.68x | 6.12x | 0.42x | 26.0x | 14.2x |
| text_search | search | 1.50s [1.48s–1.50s] | 1.76s [1.75s–1.77s] | 536.9 [536.4–537.4] | 55.23s [55.19s–55.40s] | 38.92s [38.85s–38.93s] | 776.4 [776.4–791.7] | 1.93x | 2.27x | 0.69x | 71.1x | 50.1x |
| three_way_merge | merge | 2.94s [2.90s–2.97s] | 2.16s [2.15s–2.28s] | 2.15s [2.14s–2.15s] | 10.21s [10.21s–10.27s] | 6.12s [6.11s–6.17s] | 975.9 [963.8–988.3] | 3.02x | 2.21x | 2.20x | 10.5x | 6.27x |
| log_pipeline | log-processing | 4.53s [4.48s–4.55s] | 2.04s [2.04s–2.05s] | 621.2 [618.3–654.9] | 14.83s [14.22s–14.96s] | 9.43s [9.40s–9.46s] | 942.8 [941.2–948.6] | 4.80x | 2.17x | 0.66x | 15.7x | 10.0x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.43x | 0.45x | 1.02x | 0.78x | 0.41x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 0.95x | 0.91x | 0.88x | 5.50x | 0.77x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.52x | 0.58x | 1.01x | 1.44x | 0.33x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 0.73x | 0.62x | 0.72x | 3.46x | 2.86x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 0.82x | 0.69x | 1.02x | 2.26x | 1.91x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 2.37x | 1.86x | 0.90x | 12.6x | 3.63x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 2.98x | 1.94x | 0.95x | 17.7x | 6.76x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 63 | 0.91x | 0.82x | 0.93x | 3.41x | 1.25x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 16.9 [16.9–17.5] | 17.7 [17.6–17.9] | 49.5 [48.4–486.0] | 27.6 [26.2–27.9] | 26.0 [25.9–26.0] | 47.5 [46.0–93.4] | 0.36x | 0.37x | 1.04x | 0.58x | 0.55x |
| fibfp | recursive | 17.8 [17.8–18.7] | 17.1 [16.8–39.0] | 46.6 [46.5–46.9] | 25.4 [25.2–25.4] | 24.3 [24.2–24.7] | 44.9 [44.6–45.4] | 0.40x | 0.38x | 1.04x | 0.57x | 0.54x |
| tak | recursive | 16.6 [16.4–16.8] | 16.2 [16.2–16.2] | 45.6 [45.3–45.6] | 24.8 [24.8–25.3] | 8.28 [8.23–8.82] | 44.2 [43.7–44.3] | 0.38x | 0.37x | 1.03x | 0.56x | 0.19x |
| cpstak | closure | 16.9 [16.8–17.2] | 16.5 [16.4–16.5] | 45.4 [45.3–45.7] | 25.0 [24.9–25.6] | 11.3 [11.2–11.7] | 44.5 [44.0–45.1] | 0.38x | 0.37x | 1.02x | 0.56x | 0.25x |
| sum | iterative | 15.9 [15.8–16.0] | 16.1 [15.9–16.1] | 45.2 [45.1–45.6] | 25.2 [24.4–25.7] | 37.3 [37.1–37.4] | 44.7 [44.5–45.4] | 0.36x | 0.36x | 1.01x | 0.56x | 0.83x |
| sumfp | iterative | 15.6 [15.5–15.7] | 15.8 [15.4–15.9] | 44.8 [44.4–45.3] | 24.4 [24.1–27.1] | 9.19 [9.00–9.74] | 44.5 [44.4–45.1] | 0.35x | 0.36x | 1.01x | 0.55x | 0.21x |
| nqueens | backtrack | 24.7 [24.5–24.9] | 28.0 [27.8–28.2] | 46.8 [46.4–47.2] | 111.1 [110.5–115.3] | 13.6 [13.6–14.4] | 45.6 [45.5–46.0] | 0.54x | 0.61x | 1.03x | 2.44x | 0.30x |
| fft | numeric | 30.6 [30.3–30.8] | 39.8 [39.7–39.9] | 45.7 [45.4–45.8] | 75.6 [75.5–75.8] | 8.52 [8.30–9.42] | 45.3 [44.5–45.4] | 0.68x | 0.88x | 1.01x | 1.67x | 0.19x |
| mbrot | numeric | 19.9 [19.7–20.1] | 20.2 [20.2–20.3] | 45.6 [45.5–45.6] | 43.9 [43.7–45.3] | 23.4 [23.3–23.7] | 45.3 [44.9–45.7] | 0.44x | 0.45x | 1.01x | 0.97x | 0.52x |
| ack | recursive | 30.4 [27.6–30.6] | 31.1 [25.9–41.9] | 57.1 [55.1–57.4] | 36.5 [35.6–38.5] | 107.2 [106.7–108.5] | 57.3 [56.4–57.9] | 0.53x | 0.54x | 1.00x | 0.64x | 1.87x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 16.8 [16.7–17.1] | 18.2 [17.8–18.3] | 45.3 [45.2–45.3] | 34.8 [34.5–35.2] | 6.40 [6.28–6.89] | 45.1 [44.6–46.4] | 0.37x | 0.40x | 1.01x | 0.77x | 0.14x |
| permute | micro | 17.6 [17.6–18.1] | 20.2 [20.0–20.3] | 45.6 [45.3–45.7] | 42.2 [42.0–42.6] | 7.27 [6.96–7.90] | 45.6 [45.2–45.6] | 0.39x | 0.44x | 1.00x | 0.93x | 0.16x |
| queens | micro | 22.0 [21.7–22.0] | 29.0 [28.8–29.5] | 45.8 [45.4–45.9] | 45.9 [45.8–46.4] | 6.68 [6.67–7.29] | 45.5 [44.8–46.0] | 0.48x | 0.64x | 1.01x | 1.01x | 0.15x |
| towers | micro | 23.5 [23.5–23.9] | 25.8 [25.7–25.9] | 46.1 [45.6–69.3] | 57.9 [57.7–58.2] | 8.09 [7.70–8.49] | 45.4 [45.1–45.9] | 0.52x | 0.57x | 1.02x | 1.27x | 0.18x |
| bounce | micro | 21.8 [21.8–22.2] | 26.7 [26.7–27.1] | 45.8 [45.2–46.6] | 171.4 [171.3–172.0] | 7.20 [7.11–7.85] | 45.3 [44.6–45.3] | 0.48x | 0.59x | 1.01x | 3.79x | 0.16x |
| list | micro | 20.6 [20.6–21.8] | 20.2 [20.0–20.2] | 45.9 [45.7–46.3] | 40.8 [40.4–41.6] | 6.55 [6.52–7.41] | 44.9 [44.8–44.9] | 0.46x | 0.45x | 1.02x | 0.91x | 0.15x |
| storage | micro | 18.4 [18.3–18.5] | 18.6 [18.5–18.9] | 45.4 [45.4–46.0] | 164.5 [163.9–182.4] | 8.45 [8.39–9.10] | 45.4 [45.4–45.4] | 0.41x | 0.41x | 1.00x | 3.62x | 0.19x |
| mandelbrot | compute | 59.5 [59.3–59.9] | 59.5 [59.4–59.7] | 76.2 [75.5–76.4] | 117.5 [117.3–117.8] | 877.8 [874.4–887.5] | 75.6 [75.4–76.0] | 0.79x | 0.79x | 1.01x | 1.56x | 11.6x |
| nbody | compute | 54.1 [53.7–72.6] | 63.3 [62.7–63.4] | 47.5 [47.5–48.1] | 671.2 [669.4–673.8] | 166.1 [165.5–166.8] | 50.2 [49.9–51.6] | 1.08x | 1.26x | 0.95x | 13.4x | 3.31x |
| richards | macro | 436.8 [432.9–438.0] | 404.4 [401.2–406.3] | 78.7 [78.1–78.7] | 1.10s [1.10s–1.11s] | 198.2 [197.8–198.2] | 91.4 [91.0–91.7] | 4.78x | 4.42x | 0.86x | 12.0x | 2.17x |
| json | macro | 53.7 [53.6–53.9] | 52.1 [51.6–52.3] | 53.0 [52.7–71.2] | 377.4 [377.4–378.5] | 18.2 [18.1–18.8] | 48.8 [47.9–58.4] | 1.10x | 1.07x | 1.09x | 7.74x | 0.37x |
| deltablue | macro | 276.1 [275.3–276.4] | 156.7 [154.5–175.1] | 53.7 [53.7–54.7] | 1.27s [1.26s–1.28s] | 106.9 [106.0–107.3] | 57.1 [56.8–57.4] | 4.83x | 2.74x | 0.94x | 22.2x | 1.87x |
| havlak | macro | 198.4 [197.6–199.2] | 203.1 [174.4–204.1] | 54.3 [54.2–56.6] | 48.90s [48.83s–48.99s] | 3.28s [3.27s–3.31s] | 235.6 [140.9–256.3] | 0.84x | 0.86x | 0.23x | 208x | 13.9x |
| cd | macro | 671.8 [670.4–675.2] | 262.7 [260.8–262.9] | 68.6 [68.0–75.4] | 18.61s [18.52s–18.62s] | 973.0 [965.1–1.00s] | 82.2 [81.5–169.4] | 8.17x | 3.20x | 0.83x | 226x | 11.8x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 29.5 [29.3–29.9] | 23.8 [23.7–23.9] | 48.4 [48.2–59.9] | 52.9 [52.8–54.4] | 29.5 [29.1–29.6] | 46.1 [45.9–46.1] | 0.64x | 0.52x | 1.05x | 1.15x | 0.64x |
| fannkuch | permutation | 22.4 [21.9–22.8] | 27.2 [27.0–27.3] | 45.2 [45.1–45.6] | 69.4 [69.4–70.3] | 12.8 [12.7–13.3] | 45.5 [45.3–45.7] | 0.49x | 0.60x | 0.99x | 1.52x | 0.28x |
| fasta | generation | 25.7 [25.3–25.7] | 29.0 [28.7–29.2] | 45.8 [45.5–46.0] | 72.4 [72.2–72.5] | 14.4 [14.4–14.7] | 48.5 [48.0–63.3] | 0.53x | 0.60x | 0.94x | 1.49x | 0.30x |
| knucleotide | hashing | 31.7 [31.7–32.2] | 54.8 [54.7–54.9] | 46.2 [46.1–46.3] | 69.4 [66.0–562.5] | 13.6 [13.5–14.5] | 47.7 [47.5–48.9] | 0.66x | 1.15x | 0.97x | 1.46x | 0.29x |
| pidigits | bignum | 21.2 [21.1–21.3] | 21.4 [21.2–21.6] | 47.4 [47.3–48.0] | 53.3 [53.1–138.7] | 5.82 [5.47–6.17] | 43.9 [42.7–43.9] | 0.48x | 0.49x | 1.08x | 1.21x | 0.13x |
| regexredux | regex | 16.1 [15.7–16.2] | 15.5 [15.4–15.8] | 48.0 [47.5–48.2] | 49.5 [48.9–49.6] | 11.1 [11.1–11.7] | 44.4 [44.0–44.5] | 0.36x | 0.35x | 1.08x | 1.11x | 0.25x |
| revcomp | string | 23.6 [23.6–23.8] | 23.6 [23.3–24.0] | 45.9 [45.8–46.4] | 58.8 [58.4–79.3] | 8.16 [7.95–8.76] | 45.1 [44.6–45.4] | 0.52x | 0.52x | 1.02x | 1.30x | 0.18x |
| spectralnorm | numeric | 25.1 [25.1–25.4] | 29.8 [29.7–29.9] | 45.7 [45.4–46.0] | 130.0 [129.9–130.2] | 70.7 [69.8–70.9] | 46.5 [45.6–46.6] | 0.54x | 0.64x | 0.98x | 2.80x | 1.52x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 219.4 [218.6–220.2] | 220.4 [218.9–220.8] | 73.5 [72.8–74.7] | 1.83s [1.82s–1.85s] | 895.3 [894.9–901.1] | 79.1 [78.5–80.9] | 2.78x | 2.79x | 0.93x | 23.1x | 11.3x |
| matmul | numeric | 29.0 [28.5–29.1] | 30.4 [30.2–30.5] | 51.1 [50.9–52.5] | 284.8 [283.7–286.2] | 545.4 [545.3–557.1] | 60.2 [59.5–60.3] | 0.48x | 0.51x | 0.85x | 4.73x | 9.06x |
| primes | numeric | 32.7 [32.7–32.8] | 20.6 [20.2–21.0] | 47.6 [46.6–48.0] | 87.9 [87.6–88.3] | 101.0 [100.9–101.4] | 47.4 [47.3–47.8] | 0.69x | 0.43x | 1.00x | 1.85x | 2.13x |
| base64 | string | 42.3 [41.4–64.6] | 38.4 [37.8–38.5] | 46.1 [45.9–46.6] | 534.1 [531.8–539.1] | 165.5 [162.9–167.0] | 62.7 [62.5–63.2] | 0.67x | 0.61x | 0.74x | 8.52x | 2.64x |
| levenshtein | string | 40.5 [40.1–40.8] | 31.1 [30.8–31.3] | 46.8 [46.2–48.1] | 121.4 [120.6–122.1] | 60.2 [59.8–61.2] | 47.1 [47.1–47.4] | 0.86x | 0.66x | 0.99x | 2.58x | 1.28x |
| json_gen | data | 52.9 [52.9–76.2] | 38.5 [38.4–39.0] | 47.0 [46.7–47.6] | 63.4 [63.0–63.9] | 26.2 [25.7–26.4] | 51.6 [51.0–52.1] | 1.03x | 0.75x | 0.91x | 1.23x | 0.51x |
| collatz | numeric | 294.2 [291.0–295.0] | 292.0 [289.0–294.9] | 271.6 [270.7–302.2] | 1.63s [1.62s–1.64s] | 6.26s [6.22s–6.27s] | 1.49s [1.46s–1.57s] | 0.20x | 0.20x | 0.18x | 1.09x | 4.19x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 258.4 [258.2–260.7] | 194.8 [194.0–195.2] | 106.2 [105.8–118.4] | 6.24s [6.23s–6.29s] | 2.19s [2.18s–2.21s] | 113.0 [112.9–230.2] | 2.29x | 1.72x | 0.94x | 55.3x | 19.4x |
| array1 | array | 17.3 [17.1–18.3] | 17.2 [17.2–17.3] | 44.8 [44.7–50.7] | 47.2 [47.0–47.9] | 42.0 [41.7–42.4] | 45.0 [44.9–45.5] | 0.38x | 0.38x | 1.00x | 1.05x | 0.93x |
| deriv | symbolic | 55.0 [54.7–55.3] | 27.4 [27.0–27.5] | 49.9 [49.8–50.6] | 116.5 [116.0–117.6] | 65.4 [65.1–65.5] | 48.0 [48.0–49.0] | 1.14x | 0.57x | 1.04x | 2.42x | 1.36x |
| diviter | iterative | 283.8 [283.6–285.2] | 284.2 [283.4–300.0] | 311.9 [311.1–311.9] | 640.7 [639.3–641.3] | 26.66s [26.65s–26.75s] | 514.6 [514.1–644.1] | 0.55x | 0.55x | 0.61x | 1.25x | 51.8x |
| divrec | recursive | 22.8 [22.8–23.3] | 18.4 [18.2–18.4] | 62.6 [50.0–70.6] | 41.9 [41.4–50.9] | 43.0 [42.3–44.0] | 51.4 [50.7–51.7] | 0.44x | 0.36x | 1.22x | 0.82x | 0.84x |
| gcbench | allocation | 221.0 [220.3–221.7] | 117.4 [115.9–118.0] | 116.2 [116.0–116.8] | 542.5 [542.1–542.9] | 580.2 [570.7–588.3] | 66.4 [66.3–68.0] | 3.33x | 1.77x | 1.75x | 8.17x | 8.73x |
| paraffins | combinat | 44.1 [43.9–44.3] | 62.3 [61.6–62.6] | 47.1 [47.0–47.1] | 65.6 [65.1–67.2] | 8.16 [7.99–8.68] | 44.8 [44.4–45.2] | 0.98x | 1.39x | 1.05x | 1.47x | 0.18x |
| pnpoly | numeric | 30.4 [30.4–30.7] | 23.5 [23.3–23.5] | 47.2 [47.0–47.5] | 144.4 [143.1–165.3] | 209.1 [208.0–209.4] | 49.8 [49.4–50.3] | 0.61x | 0.47x | 0.95x | 2.90x | 4.20x |
| puzzle | search | 35.3 [35.2–35.7] | 36.8 [36.5–36.8] | 46.5 [46.4–46.5] | 66.7 [66.6–67.7] | 35.2 [34.8–35.6] | 46.5 [46.4–46.8] | 0.76x | 0.79x | 1.00x | 1.43x | 0.76x |
| quicksort | sorting | 21.1 [21.0–21.4] | 23.6 [23.5–23.7] | 45.5 [45.1–45.9] | 49.6 [49.2–49.6] | 25.2 [25.0–25.3] | 45.2 [45.2–45.3] | 0.47x | 0.52x | 1.01x | 1.10x | 0.56x |
| ray | numeric | 30.1 [30.0–30.1] | 24.0 [23.5–24.1] | 45.6 [45.4–45.7] | 46.9 [46.2–47.0] | 19.5 [19.4–19.8] | 47.5 [46.9–69.5] | 0.63x | 0.50x | 0.96x | 0.99x | 0.41x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 107.1 [105.9–107.2] | 116.9 [116.8–118.0] | 49.7 [49.4–62.2] | 766.0 [763.0–778.0] | 223.5 [221.9–223.9] | 64.5 [64.1–65.9] | 1.66x | 1.81x | 0.77x | 11.9x | 3.47x |
| navier_stokes | numeric | 222.2 [221.7–222.3] | 201.5 [201.3–201.8] | 95.4 [94.8–95.6] | 416.7 [415.7–417.6] | 109.9 [109.6–129.1] | 60.5 [60.4–60.8] | 3.67x | 3.33x | 1.58x | 6.89x | 1.82x |
| splay | data | 417.3 [415.0–417.7] | 401.1 [400.2–422.8] | 67.5 [67.1–68.5] | 896.0 [894.9–898.6] | 564.0 [563.4–578.3] | 95.6 [94.7–97.6] | 4.37x | 4.20x | 0.71x | 9.38x | 5.90x |
| hashmap | data | 114.2 [113.3–135.3] | 63.0 [62.9–63.5] | 48.6 [48.5–49.4] | 3.57s [3.57s–3.58s] | 322.3 [321.2–324.8] | 60.0 [59.5–106.9] | 1.90x | 1.05x | 0.81x | 59.5x | 5.37x |
| crypto_sha1 | crypto | 77.4 [77.0–78.1] | 60.0 [59.1–60.5] | 48.8 [48.8–50.7] | 360.4 [359.1–360.4] | 225.1 [225.0–225.2] | 52.8 [52.5–52.9] | 1.47x | 1.13x | 0.92x | 6.82x | 4.26x |
| raytrace3d | 3d | 149.2 [148.1–150.4] | 85.4 [85.3–85.6] | 52.5 [52.5–53.2] | 810.1 [809.0–813.8] | 170.1 [169.7–193.2] | 62.9 [62.7–63.0] | 2.37x | 1.36x | 0.84x | 12.9x | 2.71x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 274.4 [274.0–274.4] | 82.2 [82.0–82.6] | 58.5 [58.5–59.1] | 2.61s [2.58s–2.63s] | 622.7 [611.9–623.8] | 84.1 [83.4–92.0] | 3.26x | 0.98x | 0.70x | 31.0x | 7.41x |
| microdiff | data-diff | 90.4 [89.8–91.0] | 97.0 [96.5–98.5] | 53.2 [51.3–74.0] | 918.8 [902.6–929.6] | 115.5 [114.8–117.0] | 60.6 [60.1–60.7] | 1.49x | 1.60x | 0.88x | 15.2x | 1.91x |
| hyphen | hyphenation | 124.8 [123.5–125.2] | 76.3 [76.2–76.6] | 87.5 [76.4–88.7] | 353.0 [352.9–354.3] | 101.3 [101.1–101.7] | 58.1 [57.6–58.1] | 2.15x | 1.31x | 1.50x | 6.07x | 1.74x |
| prettier_ast | formatting | 1.11s [1.11s–1.15s] | 747.3 [739.0–792.8] | 110.6 [104.6–114.3] | 2.78s [2.76s–2.81s] | 1.43s [1.43s–1.45s] | 145.1 [144.8–231.7] | 7.65x | 5.15x | 0.76x | 19.1x | 9.87x |
| text_search | search | 1.64s [1.64s–1.64s] | 1.78s [1.78s–1.81s] | 584.2 [583.4–598.0] | 55.34s [55.28s–55.49s] | 38.93s [38.86s–38.95s] | 821.5 [821.5–949.7] | 2.00x | 2.17x | 0.71x | 67.4x | 47.4x |
| three_way_merge | merge | 2.97s [2.97s–2.98s] | 2.19s [2.18s–2.24s] | 2.20s [2.19s–2.21s] | 10.27s [10.26s–10.32s] | 6.13s [6.11s–6.18s] | 1.03s [1.01s–1.14s] | 2.88x | 2.12x | 2.12x | 9.94x | 5.93x |
| log_pipeline | log-processing | 4.50s [4.48s–4.50s] | 2.16s [2.15s–2.16s] | 669.8 [667.9–746.9] | 14.92s [14.32s–15.06s] | 9.46s [9.43s–9.49s] | 997.3 [994.4–1.10s] | 4.51x | 2.16x | 0.67x | 15.0x | 9.49x |

