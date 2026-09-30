# Lambda Benchmark Results: Result50

- **Date:** 2026-09-30
- **Platform:** Darwin arm64
- **Lambda commit:** `a9489bc3297fe5b0f19cea6cd4dc676aa954acbe`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v50-a9489bc329` (19,394,456 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 57.10s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 57.0s (batched 56.0s: sync 38.9s, async 17.1s; non-batched 1.0s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js, Python, Go
- **Results source:** `test/benchmark/benchmark_results_v50.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, the MIR columns pin `LAMBDA_TIER=jit`, and LambdaJS pins `JS_EXECUTION_BACKEND=mir`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | Timed Python | Timed Go | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo | Python/Node geo | Go/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.36x | 0.29x | 0.21x | 0.99x | 6.51x | 6.16x | 0.33x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.01x | 0.41x | 0.09x | 11.6x | 5.18x | 3.96x | 0.22x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.31x | 0.17x | 0.10x | 4.31x | 1.74x | 0.57x | 0.21x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 1.47x | 0.64x | 0.23x | 11.1x | 11.9x | 9.55x | 0.40x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 1.08x | 0.64x | 0.33x | 7.26x | 13.3x | 10.6x | 0.50x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 4.21x | 2.17x | 0.29x | 26.7x | 12.0x | 9.28x | 0.22x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 4.39x | 1.73x | 0.47x | 23.4x | 12.8x | 7.00x | 0.52x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 63 | 63 | 63 | 1.05x | 0.54x | 0.20x | 7.43x | 7.24x | 5.02x | 0.31x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 59 | 0.50x | 0.96x | 2.78x | 59 |
| complete current suite | 63 | 0.54x | 1.05x | 2.74x | 63 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 2.74x over 63 of 63 rows
- **MIR (typed) / Go geomean:** 1.73x over 63 of 63 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | Go | MIR (typed)/C2MIR | MIR (typed)/Go |
|---|---:|---:|---:|---:|---:|
| jetstream/splay | 289.0 | 18.8 | 28.4 | 15.4x | 10.2x |
| awfy/havlak | 27.7 | 1.87 | 5.81 | 14.8x | 4.77x |
| text/microdiff | 37.5 | 2.64 | 11.3 | 14.2x | 3.33x |
| awfy/deltablue | 15.1 | 1.15 | 3.62 | 13.2x | 4.18x |
| jetstream/cube3d | 6.56 | 0.512 | 1.57 | 12.8x | 4.17x |
| larceny/puzzle | 14.6 | 1.27 | 2.09 | 11.4x | 6.98x |
| jetstream/crypto_sha1 | 28.8 | 2.60 | 0.391 | 11.1x | 73.7x |
| awfy/cd | 154.5 | 14.9 | 12.6 | 10.4x | 12.3x |
| kostya/base64 | 5.27 | 0.555 | 1.40 | 9.49x | 3.76x |
| jetstream/hashmap | 23.8 | 2.71 | 4.58 | 8.80x | 5.19x |
| awfy/towers | 0.230 | 0.028 | 0.062 | 8.19x | 3.71x |
| text/prettier_ast | 326.4 | 41.3 | 92.4 | 7.90x | 3.53x |

---

### Notable Results

- Missing timings: **0** cells

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 13.30s | 93.2 | 143x |
| awfy/nbody | 625.6 | 5.41 | 116x |
| awfy/cd | 3.93s | 35.6 | 110x |
| jetstream/hashmap | 1.20s | 15.1 | 79.9x |
| larceny/triangl | 5.38s | 69.0 | 78.0x |
| kostya/brainfuck | 1.73s | 33.1 | 52.4x |
| text/microdiff | 810.5 | 16.2 | 50.0x |
| text/fast_diff | 1.83s | 39.0 | 46.9x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| r7rs/sumfp | 0.069 | 0.891 | 0.08x |
| beng/pidigits | 0.525 | 1.91 | 0.27x |
| awfy/sieve | 0.168 | 0.392 | 0.43x |
| r7rs/tak | 0.356 | 0.788 | 0.45x |
| r7rs/sum | 0.676 | 1.19 | 0.57x |
| r7rs/cpstak | 0.702 | 1.02 | 0.69x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Python (ms) | Go (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Python/Node | Go/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.58 [1.55–1.59] | 1.55 [1.52–1.60] | 1.12 [1.02–1.12] | 1.98 [1.96–1.99] | 18.6 [18.6–18.6] | 1.76 [1.75–1.87] | 19.9 [19.9–20.1] | 0.600 [0.599–1.62] | 0.90x | 0.88x | 0.64x | 1.13x | 10.6x | 11.3x | 0.34x |
| fibfp | recursive | 2.38 [2.33–2.39] | 1.25 [1.25–1.28] | 1.13 [1.04–1.13] | 2.00 [1.94–2.36] | 18.9 [18.4–19.1] | 1.77 [1.75–1.84] | 23.7 [23.5–23.8] | 1.36 [1.05–1.81] | 1.34x | 0.71x | 0.64x | 1.13x | 10.7x | 13.4x | 0.77x |
| tak | recursive | 0.134 [0.134–0.138] | 0.134 [0.133–0.139] | 0.117 [0.109–0.120] | 0.356 [0.353–0.424] | 2.78 [2.77–2.94] | 0.788 [0.781–0.821] | 2.07 [2.03–2.10] | 0.160 [0.121–0.438] | 0.17x | 0.17x | 0.15x | 0.45x | 3.53x | 2.63x | 0.20x |
| cpstak | closure | 0.268 [0.266–0.271] | 0.285 [0.271–0.340] | 0.231 [0.214–0.233] | 0.702 [0.695–0.828] | 5.60 [5.53–5.63] | 1.02 [0.993–1.02] | 4.21 [4.10–4.22] | 0.278 [0.201–0.569] | 0.26x | 0.28x | 0.23x | 0.69x | 5.48x | 4.13x | 0.27x |
| sum | iterative | 0.266 [0.266–0.266] | 0.266 [0.265–0.266] | 0.269 [0.268–0.270] | 0.676 [0.668–0.804] | 31.0 [31.0–31.1] | 1.19 [1.18–1.23] | 31.5 [31.1–31.5] | 0.569 [0.485–0.743] | 0.22x | 0.22x | 0.23x | 0.57x | 26.0x | 26.4x | 0.48x |
| sumfp | iterative | 0.069 [0.068–0.069] | 0.069 [0.068–0.096] | 0.077 [0.077–0.079] | 0.069 [0.067–0.085] | 3.72 [3.64–3.75] | 0.891 [0.882–0.905] | 3.26 [3.24–3.35] | 0.357 [0.261–0.595] | 0.08x | 0.08x | 0.09x | 0.08x | 4.18x | 3.66x | 0.40x |
| nqueens | backtrack | 1.07 [1.04–1.07] | 1.26 [1.24–1.28] | 0.368 [0.361–0.369] | 29.0 [29.0–30.0] | 7.81 [7.78–7.91] | 1.75 [1.72–1.76] | 3.38 [3.34–3.54] | 0.323 [0.216–0.520] | 0.61x | 0.72x | 0.21x | 16.6x | 4.45x | 1.92x | 0.18x |
| fft | numeric | 0.295 [0.292–0.302] | 0.082 [0.081–0.091] | 0.025 [0.025–0.028] | 3.55 [3.53–3.59] | 2.82 [2.71–2.83] | 1.56 [1.55–1.67] | 4.53 [4.50–4.56] | 0.096 [0.073–0.285] | 0.19x | 0.05x | 0.02x | 2.28x | 1.81x | 2.90x | 0.06x |
| mbrot | numeric | 0.729 [0.727–0.732] | 0.564 [0.563–0.584] | 0.443 [0.442–0.447] | 2.20 [2.18–2.48] | 17.7 [17.7–17.8] | 1.88 [1.87–1.95] | 17.2 [17.2–17.3] | 0.916 [0.654–1.23] | 0.39x | 0.30x | 0.24x | 1.17x | 9.40x | 9.16x | 0.49x |
| ack | recursive | 12.7 [12.6–13.4] | 9.99 [9.84–9.99] | 11.8 [11.3–11.9] | 16.2 [14.1–16.3] | 101.7 [101.1–102.5] | 13.4 [13.3–13.5] | 129.7 [129.3–158.2] | 11.7 [9.41–16.7] | 0.95x | 0.75x | 0.88x | 1.21x | 7.61x | 9.71x | 0.87x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Python (ms) | Go (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Python/Node | Go/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.026 [0.026–0.026] | 0.026 [0.026–0.026] | 0.016 [0.015–0.016] | 0.168 [0.165–0.190] | 0.622 [0.621–0.624] | 0.392 [0.383–0.988] | 0.503 | 0.056 [0.051–0.324] | 0.07x | 0.07x | 0.04x | 0.43x | 1.59x | 1.28x | 0.14x |
| permute | micro | 0.291 [0.271–0.334] | 0.085 [0.084–0.089] | 0.025 [0.025–0.029] | 5.93 [5.68–6.41] | 1.56 [1.50–1.57] | 0.813 [0.807–0.829] | 1.35 | 0.082 [0.062–0.344] | 0.36x | 0.10x | 0.03x | 7.29x | 1.92x | 1.67x | 0.10x |
| queens | micro | 0.171 [0.170–0.172] | 0.068 [0.067–0.070] | 0.019 [0.018–0.020] | 3.40 [3.38–3.51] | 1.04 [1.04–1.06] | 0.640 [0.639–0.655] | 0.738 | 0.070 [0.051–0.315] | 0.27x | 0.11x | 0.03x | 5.31x | 1.63x | 1.15x | 0.11x |
| towers | micro | 0.884 [0.852–0.909] | 0.230 [0.228–0.231] | 0.028 [0.027–0.032] | 9.45 [9.25–9.81] | 2.29 [2.24–2.29] | 1.12 [1.12–1.12] | 1.82 | 0.062 [0.045–0.287] | 0.79x | 0.21x | 0.03x | 8.43x | 2.04x | 1.63x | 0.06x |
| bounce | micro | 0.065 [0.065–0.067] | 0.097 [0.097–0.098] | 0.024 [0.023–0.024] | 4.06 [4.00–4.30] | 0.873 [0.870–0.892] | 0.542 [0.538–0.548] | 0.835 | 0.086 [0.060–0.327] | 0.12x | 0.18x | 0.04x | 7.49x | 1.61x | 1.54x | 0.16x |
| list | micro | 0.592 [0.589–0.593] | 0.124 [0.123–0.124] | 0.023 [0.021–0.024] | 2.24 [2.18–2.45] | 0.908 [0.902–0.920] | 0.509 [0.494–0.516] | 0.613 | 0.059 [0.052–0.279] | 1.16x | 0.24x | 0.05x | 4.40x | 1.78x | 1.20x | 0.11x |
| storage | micro | 0.578 [0.571–0.581] | 0.356 [0.350–0.368] | 0.094 [0.093–0.100] | 4.17 [4.14–4.27] | 2.10 [2.06–2.12] | 0.646 [0.646–0.653] | 1.33 | 0.341 [0.269–0.586] | 0.90x | 0.55x | 0.15x | 6.46x | 3.26x | 2.06x | 0.53x |
| mandelbrot | compute | 39.6 [39.5–39.6] | 39.6 [39.6–39.7] | 30.6 [30.6–30.6] | 46.8 [46.7–46.9] | 872.6 [870.3–905.4] | 31.2 [31.0–31.2] | 871.8 | 34.5 [33.2–55.9] | 1.27x | 1.27x | 0.98x | 1.50x | 28.0x | 28.0x | 1.11x |
| nbody | compute | 5.58 [5.51–5.59] | 2.97 [2.97–2.97] | 1.48 [1.47–1.49] | 625.6 [620.8–628.7] | 159.4 [158.7–160.9] | 5.41 [5.29–5.96] | 156.5 | 2.70 [2.19–4.66] | 1.03x | 0.55x | 0.27x | 116x | 29.5x | 28.9x | 0.50x |
| richards | macro | 331.1 [330.7–333.1] | 75.4 [74.9–75.6] | 29.3 [29.3–29.7] | 897.3 [897.2–898.6] | 191.9 [190.7–192.0] | 46.9 [46.3–47.0] | 158.7 | 28.7 [26.4–46.6] | 7.06x | 1.61x | 0.63x | 19.1x | 4.09x | 3.38x | 0.61x |
| json | macro | 5.80 [5.77–5.84] | 1.81 [1.81–1.94] | 0.269 [0.265–0.271] | 42.9 [42.8–43.0] | 10.8 [10.8–10.8] | 2.76 [2.63–2.92] | 6.98 | 0.792 [0.572–1.33] | 2.10x | 0.66x | 0.10x | 15.5x | 3.91x | 2.53x | 0.29x |
| deltablue | macro | 87.5 [87.4–87.7] | 15.1 [15.1–15.2] | 1.15 [1.15–1.15] | 387.1 [385.7–387.8] | 99.7 [99.6–99.7] | 11.6 [11.5–11.7] | 66.8 | 3.62 [2.69–4.22] | 7.53x | 1.30x | 0.10x | 33.3x | 8.58x | 5.75x | 0.31x |
| havlak | macro | 69.2 [68.7–69.2] | 27.7 [27.5–27.8] | 1.87 [1.84–1.90] | 13.30s [13.25s–13.32s] | 3.24s [3.23s–3.37s] | 93.2 [86.1–94.7] | 2.00s | 5.81 [4.52–9.48] | 0.74x | 0.30x | 0.02x | 143x | 34.7x | 21.4x | 0.06x |
| cd | macro | 578.9 [578.1–582.5] | 154.5 [154.4–158.1] | 14.9 [14.8–15.0] | 3.93s [3.90s–3.94s] | 953.4 [952.6–958.1] | 35.6 [35.5–35.9] | 644.2 | 12.6 [10.2–19.7] | 16.3x | 4.34x | 0.42x | 110x | 26.8x | 18.1x | 0.35x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Python (ms) | Go (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Python/Node | Go/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 8.18 [8.15–8.27] | 2.47 [2.45–2.52] | 2.98 [2.96–2.98] | 21.4 [20.5–21.6] | 23.5 [23.3–23.8] | 3.85 [3.84–3.95] | 10.2 [10.1–10.3] | 3.55 [2.61–5.08] | 2.12x | 0.64x | 0.77x | 5.56x | 6.10x | 2.64x | 0.92x |
| fannkuch | permutation | 0.305 [0.303–0.309] | 0.347 [0.346–0.359] | 0.151 [0.148–0.151] | 20.1 [19.9–20.4] | 7.12 [7.12–7.28] | 3.83 [3.81–3.92] | 4.77 [4.77–4.78] | 0.222 [0.198–0.431] | 0.08x | 0.09x | 0.04x | 5.25x | 1.86x | 1.24x | 0.06x |
| fasta | generation | 0.773 [0.772–0.775] | 0.889 [0.865–0.894] | 0.241 [0.239–0.243] | 25.0 [25.0–25.2] | 8.79 [8.73–8.80] | 5.97 [5.93–6.04] | 1.85 [1.83–1.85] | 0.382 [0.314–1.64] | 0.13x | 0.15x | 0.04x | 4.19x | 1.47x | 0.31x | 0.06x |
| knucleotide | hashing | 4.86 [4.82–5.14] | 0.440 [0.423–0.462] | 0.278 [0.276–0.298] | 20.7 [20.1–39.5] | 7.69 [7.57–7.70] | 4.77 [4.75–4.83] | 3.57 [3.54–3.58] | 0.654 [0.498–2.03] | 1.02x | 0.09x | 0.06x | 4.34x | 1.61x | 0.75x | 0.14x |
| pidigits | bignum | 0.347 [0.346–0.367] | 0.347 [0.346–0.350] | 0.047 [0.045–0.049] | 0.525 [0.518–0.538] | 0.127 [0.127–0.127] | 1.91 [1.80–2.58] | 0.097 [0.096–0.098] | 0.203 [0.146–0.773] | 0.18x | 0.18x | 0.02x | 0.27x | 0.07x | 0.05x | 0.11x |
| regexredux | regex | 0.555 [0.551–0.571] | 0.556 [0.543–0.561] | 1.13 [1.12–1.13] | 8.40 [8.40–8.49] | 5.56 [5.53–5.56] | 2.28 [2.27–2.30] | 1.50 [1.50–1.51] | 4.28 [3.42–7.09] | 0.24x | 0.24x | 0.50x | 3.69x | 2.44x | 0.66x | 1.88x |
| revcomp | string | 0.438 [0.431–0.447] | 0.231 [0.225–0.233] | 0.373 [0.369–0.373] | 22.8 [22.4–23.5] | 2.55 [2.54–2.56] | 3.20 [3.19–3.21] | 0.081 [0.078–0.081] | 0.447 [0.363–1.96] | 0.14x | 0.07x | 0.12x | 7.13x | 0.80x | 0.03x | 0.14x |
| spectralnorm | numeric | 1.68 [1.67–1.69] | 0.785 [0.784–0.796] | 0.350 [0.350–0.354] | 81.7 [80.6–84.1] | 64.2 [64.1–64.5] | 2.61 [2.54–2.62] | 42.7 [42.5–42.8] | 0.628 [0.612–1.16] | 0.64x | 0.30x | 0.13x | 31.3x | 24.6x | 16.4x | 0.24x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Python (ms) | Go (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Python/Node | Go/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 169.1 [169.0–171.2] | 160.2 [158.8–161.1] | 28.7 [28.5–29.1] | 1.73s [1.73s–1.75s] | 847.9 [847.5–850.0] | 33.1 [33.1–33.4] | 697.8 [692.4–699.6] | 22.2 [18.4–34.0] | 5.11x | 4.84x | 0.87x | 52.4x | 25.6x | 21.1x | 0.67x |
| matmul | numeric | 5.14 [5.11–5.15] | 5.07 [5.06–5.08] | 6.03 [6.01–6.03] | 192.1 [191.7–193.7] | 536.7 [536.7–537.0] | 15.3 [15.2–16.1] | 505.1 [502.9–511.4] | 19.3 [16.1–32.1] | 0.33x | 0.33x | 0.39x | 12.5x | 35.0x | 32.9x | 1.26x |
| primes | numeric | 17.5 [17.4–17.5] | 2.18 [2.17–2.20] | 1.59 [1.58–1.62] | 51.6 [51.2–54.2] | 94.2 [94.0–94.4] | 4.35 [4.34–4.38] | 88.3 [86.6–90.7] | 3.14 [2.49–3.47] | 4.01x | 0.50x | 0.36x | 11.9x | 21.6x | 20.3x | 0.72x |
| base64 | string | 15.8 [15.8–16.0] | 5.27 [5.25–5.34] | 0.555 [0.555–0.559] | 467.2 [465.3–479.5] | 157.7 [156.0–158.6] | 17.2 [17.1–17.2] | 77.9 [77.0–81.2] | 1.40 [1.01–1.93] | 0.92x | 0.31x | 0.03x | 27.2x | 9.19x | 4.54x | 0.08x |
| levenshtein | string | 11.3 [11.2–11.4] | 2.83 [2.75–2.87] | 0.905 [0.902–0.922] | 76.6 [76.5–76.8] | 53.8 [53.7–54.0] | 3.99 [3.92–4.04] | 66.0 [65.7–66.7] | 2.47 [2.03–2.95] | 2.83x | 0.71x | 0.23x | 19.2x | 13.5x | 16.5x | 0.62x |
| json_gen | data | 23.7 [23.4–23.9] | 7.26 [7.04–7.28] | 1.57 [1.50–1.57] | 28.1 [27.8–28.1] | 19.8 [19.8–20.1] | 6.11 [6.07–6.12] | 7.97 [7.91–8.04] | 3.22 [2.55–4.22] | 3.88x | 1.19x | 0.26x | 4.59x | 3.24x | 1.30x | 0.53x |
| collatz | numeric | 298.8 [297.8–299.2] | 284.7 [277.9–285.0] | 224.4 [224.4–224.4] | 1.59s [1.59s–1.59s] | 6.21s [6.19s–6.23s] | 1.41s [1.41s–1.41s] | 7.43s [7.38s–7.46s] | 148.8 [147.1–173.0] | 0.21x | 0.20x | 0.16x | 1.12x | 4.40x | 5.26x | 0.11x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Python (ms) | Go (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Python/Node | Go/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 240.2 [239.1–240.7] | 166.0 [165.6–166.4] | 59.8 [59.7–59.9] | 5.38s [5.34s–5.39s] | 2.17s [2.16s–2.17s] | 69.0 [68.9–69.0] | 2.72s [2.69s–2.74s] | 48.0 [47.9–73.8] | 3.48x | 2.41x | 0.87x | 78.0x | 31.4x | 39.5x | 0.70x |
| array1 | array | 0.806 [0.805–0.811] | 0.808 [0.807–0.815] | 0.318 [0.315–0.321] | 13.2 [13.0–15.4] | 35.8 [35.7–36.1] | 1.83 [1.83–1.89] | 30.6 [30.0–30.7] | 0.777 [0.518–0.995] | 0.44x | 0.44x | 0.17x | 7.20x | 19.5x | 16.7x | 0.42x |
| deriv | symbolic | 24.5 [24.4–24.7] | 6.78 [6.77–6.85] | 2.86 [2.85–3.01] | 89.0 [84.5–91.2] | 59.3 [59.3–59.5] | 3.72 [3.67–3.73] | 23.8 [23.8–24.1] | 5.48 [4.42–8.20] | 6.59x | 1.82x | 0.77x | 23.9x | 15.9x | 6.40x | 1.47x |
| diviter | iterative | 265.0 [264.9–265.7] | 265.2 [265.1–265.2] | 265.1 [265.0–265.2] | 611.5 [611.4–611.7] | 26.58s [26.56s–26.58s] | 468.9 [467.0–475.0] | 23.16s [23.15s–23.16s] | 255.4 [247.6–275.2] | 0.57x | 0.57x | 0.57x | 1.30x | 56.7x | 49.4x | 0.54x |
| divrec | recursive | 5.89 [5.84–5.91] | 1.20 [1.20–1.21] | 4.85 [4.84–4.91] | 16.2 [16.0–16.5] | 35.6 [35.2–35.6] | 7.58 [7.51–7.66] | 40.7 [40.5–40.8] | 7.88 [6.47–10.8] | 0.78x | 0.16x | 0.64x | 2.14x | 4.70x | 5.37x | 1.04x |
| gcbench | allocation | 190.9 [190.3–192.9] | 82.6 [82.6–82.9] | 70.7 [70.5–71.3] | 510.0 [508.8–510.2] | 556.1 [551.9–558.0] | 23.0 [22.9–23.2] | 249.3 [248.8–251.2] | 39.7 [39.2–64.1] | 8.30x | 3.59x | 3.08x | 22.2x | 24.2x | 10.8x | 1.73x |
| paraffins | combinat | 0.195 [0.191–0.196] | 0.156 [0.155–0.158] | 0.047 [0.046–0.049] | 2.07 [2.07–2.09] | 2.51 [2.50–2.51] | 0.975 [0.971–0.982] | 2.58 [2.58–2.58] | 0.116 [0.099–0.403] | 0.20x | 0.16x | 0.05x | 2.12x | 2.58x | 2.65x | 0.12x |
| pnpoly | numeric | 11.7 [11.6–11.7] | 4.20 [4.18–4.21] | 1.96 [1.93–1.99] | 75.9 [75.0–76.3] | 200.4 [200.2–200.6] | 5.78 [5.75–5.86] | 111.5 [110.6–113.1] | 4.31 [3.42–4.37] | 2.02x | 0.73x | 0.34x | 13.1x | 34.6x | 19.3x | 0.75x |
| puzzle | search | 13.6 [13.5–13.6] | 14.6 [14.5–14.9] | 1.27 [1.26–1.27] | 28.4 [28.2–29.0] | 29.1 [29.1–29.3] | 3.29 [3.29–3.30] | 18.9 [18.8–19.1] | 2.09 [1.90–2.20] | 4.11x | 4.42x | 0.39x | 8.62x | 8.84x | 5.75x | 0.63x |
| quicksort | sorting | 1.08 [1.00–1.09] | 0.604 [0.567–0.609] | 0.196 [0.196–0.197] | 16.4 [16.2–17.4] | 19.1 [19.0–19.1] | 1.65 [1.64–1.67] | 22.9 [22.6–23.3] | 0.559 [0.410–0.776] | 0.65x | 0.37x | 0.12x | 9.93x | 11.6x | 13.9x | 0.34x |
| ray | numeric | 0.215 [0.212–0.224] | 0.215 [0.212–0.222] | 0.168 [0.167–0.172] | 5.34 [5.24–5.70] | 13.7 [13.6–13.8] | 3.55 [3.53–3.59] | 13.2 [13.0–13.3] | 0.202 [0.147–0.584] | 0.06x | 0.06x | 0.05x | 1.50x | 3.85x | 3.72x | 0.06x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Python (ms) | Go (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Python/Node | Go/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 8.29 [8.29–8.34] | 6.56 [6.53–6.62] | 0.512 [0.511–0.519] | 298.7 [296.8–299.1] | 215.5 [215.0–215.6] | 17.6 [17.5–17.9] | 49.2 [49.2–49.3] | 1.57 [1.33–4.24] | 0.47x | 0.37x | 0.03x | 17.0x | 12.3x | 2.80x | 0.09x |
| navier_stokes | numeric | 79.3 [79.3–79.3] | 68.9 [68.6–69.5] | 46.7 [46.6–46.7] | 208.6 [208.3–209.4] | 98.6 [98.4–98.7] | 14.0 [14.0–14.0] | 118.0 [117.3–118.7] | 3.26 [3.18–7.07] | 5.66x | 4.92x | 3.33x | 14.9x | 7.05x | 8.43x | 0.23x |
| splay | data | 319.7 [319.1–319.9] | 289.0 [288.3–289.5] | 18.8 [18.6–18.8] | 329.6 [329.3–329.9] | 143.8 [143.4–145.6] | 18.9 [18.1–19.1] | 297.7 [294.9–297.9] | 28.4 [27.0–44.9] | 16.9x | 15.3x | 0.99x | 17.4x | 7.61x | 15.8x | 1.50x |
| hashmap | data | 69.6 [68.2–72.3] | 23.8 [23.8–24.0] | 2.71 [2.70–2.72] | 1.20s [1.20s–1.21s] | 312.6 [311.4–316.1] | 15.1 [15.1–15.1] | 83.0 [82.0–83.1] | 4.58 [3.66–7.04] | 4.62x | 1.58x | 0.18x | 79.9x | 20.7x | 5.51x | 0.30x |
| crypto_sha1 | crypto | 49.1 [49.1–49.5] | 28.8 [28.4–28.8] | 2.60 [2.60–2.60] | 313.1 [305.8–313.3] | 217.3 [216.6–218.8] | 8.69 [8.63–8.82] | 319.1 [318.6–320.5] | 0.391 [0.185–0.595] | 5.65x | 3.31x | 0.30x | 36.0x | 25.0x | 36.7x | 0.04x |
| raytrace3d | 3d | 85.9 [85.9–86.0] | 12.9 [12.8–13.0] | 2.19 [2.18–2.21] | 522.8 [520.3–524.1] | 162.3 [161.0–162.7] | 18.2 [18.1–18.4] | 153.9 [152.3–154.0] | 4.29 [3.25–6.26] | 4.72x | 0.71x | 0.12x | 28.7x | 8.92x | 8.45x | 0.24x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Python (ms) | Go (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Python/Node | Go/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 246.8 [246.7–247.0] | 46.8 [46.8–46.9] | 13.0 [13.0–13.0] | 1.83s [1.82s–1.83s] | 608.2 [608.2–615.1] | 39.0 [38.9–39.5] | 333.6 [332.9–333.7] | 16.4 [13.8–28.0] | 6.33x | 1.20x | 0.33x | 46.9x | 15.6x | 8.55x | 0.42x |
| microdiff | data-diff | 36.5 [36.4–36.7] | 37.5 [37.4–38.0] | 2.64 [2.62–2.65] | 810.5 [806.1–810.7] | 107.9 [107.8–108.0] | 16.2 [16.2–16.2] | 44.9 [44.8–44.9] | 11.3 [9.28–19.2] | 2.25x | 2.31x | 0.16x | 50.0x | 6.65x | 2.77x | 0.69x |
| hyphen | hyphenation | 57.7 [57.6–57.8] | 9.21 [9.19–9.27] | 1.47 [1.46–1.47] | 95.9 [95.6–96.3] | 79.6 [79.4–80.2] | 6.65 [6.63–7.44] | 45.6 [45.2–46.0] | 1.97 [1.64–3.35] | 8.67x | 1.38x | 0.22x | 14.4x | 12.0x | 6.85x | 0.30x |
| prettier_ast | formatting | 945.2 [934.2–945.5] | 326.4 [325.4–327.7] | 41.3 [41.0–41.4] | 2.50s [2.49s–2.52s] | 1.40s [1.40s–1.41s] | 99.5 [99.2–100.7] | 1.01s [1.01s–1.01s] | 92.4 [92.2–115.3] | 9.50x | 3.28x | 0.42x | 25.1x | 14.1x | 10.1x | 0.93x |
| text_search | search | 1.48s [1.48s–1.53s] | 1.42s [1.42s–1.43s] | 535.4 [535.0–535.5] | 27.12s [27.09s–27.15s] | 38.61s [38.61s–38.61s] | 774.0 [772.6–775.2] | 35.75s [35.74s–35.82s] | 470.1 [470.1–494.2] | 1.92x | 1.84x | 0.69x | 35.0x | 49.9x | 46.2x | 0.61x |
| three_way_merge | merge | 2.82s [2.79s–2.84s] | 1.57s [1.55s–1.59s] | 2.12s [2.12s–2.14s] | 9.15s [9.13s–9.17s] | 6.09s [6.09s–6.13s] | 958.6 [953.1–961.9] | 2.49s [2.48s–2.50s] | 588.1 [584.0–609.8] | 2.94x | 1.64x | 2.22x | 9.55x | 6.36x | 2.60x | 0.61x |
| log_pipeline | log-processing | 4.43s [4.42s–4.44s] | 1.12s [1.12s–1.13s] | 620.2 [619.7–624.3] | 12.52s [12.50s–12.56s] | 9.39s [9.35s–9.47s] | 937.4 [935.6–940.7] | 3.93s [3.89s–3.94s] | 335.1 [334.4–349.5] | 4.72x | 1.20x | 0.66x | 13.4x | 10.0x | 4.19x | 0.36x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR and LambdaJS columns use the shipped auto tier -- no `LAMBDA_TIER` or `JS_EXECUTION_BACKEND` override -- which is what `lambda.exe run script.ls` and `lambda.exe js script.js` actually do.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. The MIR and LambdaJS columns are re-run, because part 1 pins the compiled lane and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS (auto) | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS (auto)/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.50x | 0.51x | 1.03x | 2.99x | 0.41x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.48x | 1.48x | 0.91x | 7.70x | 0.80x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.61x | 0.72x | 1.02x | 1.92x | 0.33x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 2.97x | 3.42x | 0.72x | 25.7x | 2.90x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 1.35x | 0.98x | 1.00x | 9.82x | 1.90x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 3.90x | 3.50x | 0.91x | 33.7x | 3.69x |
| Text | 7 | 7 | 7 | 7 | 6 | 7 | 7 | 3.20x | 2.16x | 0.93x | 36.7x | 6.78x |
| **Overall** | 63 | 63 | 63 | 63 | 62 | 63 | 63 | 1.41x | 1.32x | 0.93x | 8.86x | 1.26x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 17.4 [17.3–17.5] | 17.5 [17.4–17.9] | 49.2 [49.1–478.0] | 299.1 [295.4–300.9] | 24.8 [24.5–24.9] | 46.0 [44.9–46.5] | 0.38x | 0.38x | 1.07x | 6.50x | 0.54x |
| fibfp | recursive | 18.3 [17.9–18.7] | 17.0 [17.0–17.3] | 46.8 [46.4–47.0] | 301.6 [297.2–326.9] | 24.9 [24.5–25.0] | 45.2 [45.0–45.6] | 0.41x | 0.38x | 1.04x | 6.68x | 0.55x |
| tak | recursive | 17.0 [16.8–17.1] | 16.4 [16.1–16.5] | 45.9 [45.8–46.2] | 70.6 [68.0–70.9] | 8.64 [8.30–8.81] | 44.0 [43.7–45.0] | 0.39x | 0.37x | 1.04x | 1.60x | 0.20x |
| cpstak | closure | 17.0 [16.6–18.6] | 16.6 [16.5–16.6] | 46.0 [45.6–46.2] | 115.5 [108.0–140.7] | 11.2 [11.0–11.9] | 44.2 [44.2–46.4] | 0.39x | 0.37x | 1.04x | 2.61x | 0.25x |
| sum | iterative | 17.0 [16.9–17.1] | 16.7 [16.6–16.8] | 45.3 [45.1–45.5] | 36.3 [36.0–36.7] | 37.1 [36.6–37.7] | 44.9 [44.5–45.2] | 0.38x | 0.37x | 1.01x | 0.81x | 0.83x |
| sumfp | iterative | 32.0 [32.0–32.5] | 47.6 [46.5–47.6] | 45.3 [44.9–45.4] | 53.6 [52.9–54.6] | 9.25 [9.19–9.96] | 44.6 [44.3–45.0] | 0.72x | 1.07x | 1.02x | 1.20x | 0.21x |
| nqueens | backtrack | 33.9 [27.3–57.3] | 30.1 [29.8–30.4] | 46.8 [46.6–47.0] | 159.6 [159.3–165.0] | 13.5 [13.4–14.2] | 45.4 [45.1–45.8] | 0.75x | 0.66x | 1.03x | 3.52x | 0.30x |
| fft | numeric | 35.4 [34.9–36.3] | 42.3 [42.2–42.4] | 45.7 [45.3–46.3] | 113.6 [112.0–141.7] | 8.30 [8.19–8.97] | 45.5 [44.6–46.2] | 0.78x | 0.93x | 1.00x | 2.49x | 0.18x |
| mbrot | numeric | 23.5 [23.4–23.6] | 25.2 [24.8–25.3] | 46.1 [45.9–46.8] | 31.3 [31.3–31.7] | 23.4 [23.2–24.3] | 45.6 [45.1–45.8] | 0.52x | 0.55x | 1.01x | 0.69x | 0.51x |
| ack | recursive | 29.4 [29.3–30.4] | 26.3 [26.2–26.4] | 57.0 [56.8–57.9] | 3.07s [3.02s–3.09s] | 108.3 [107.1–108.9] | 57.2 [56.7–57.3] | 0.51x | 0.46x | 1.00x | 53.6x | 1.89x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 18.0 [17.8–18.1] | 19.9 [19.6–20.3] | 44.8 [44.8–45.4] | 37.2 [29.2–37.4] | 6.53 [6.38–7.24] | 45.2 [44.7–48.4] | 0.40x | 0.44x | 0.99x | 0.82x | 0.14x |
| permute | micro | 19.6 [19.4–19.6] | 20.7 [20.4–21.1] | 45.8 [45.4–45.8] | 52.0 [50.6–52.9] | 7.26 [6.91–7.77] | 45.5 [45.5–45.9] | 0.43x | 0.46x | 1.01x | 1.14x | 0.16x |
| queens | micro | 22.2 [21.9–22.5] | 27.9 [27.8–27.9] | 45.8 [45.8–46.3] | 41.4 [40.3–41.5] | 6.65 [6.36–7.56] | 45.6 [45.1–45.7] | 0.49x | 0.61x | 1.01x | 0.91x | 0.15x |
| towers | micro | 23.2 [22.6–24.5] | 29.9 [29.6–30.2] | 45.6 [45.5–46.0] | 69.1 [68.8–70.8] | 8.08 [7.94–8.71] | 45.8 [45.7–45.8] | 0.51x | 0.65x | 1.00x | 1.51x | 0.18x |
| bounce | micro | 21.3 [21.0–21.4] | 26.3 [26.0–26.5] | 45.8 [45.2–46.0] | 37.1 [36.8–37.4] | 7.32 [6.87–7.70] | 45.6 [45.5–47.3] | 0.47x | 0.58x | 1.00x | 0.81x | 0.16x |
| list | micro | 19.5 [19.4–19.6] | 19.6 [19.3–19.9] | 45.7 [45.6–47.0] | 36.6 [36.5–37.1] | 6.56 [6.40–7.27] | 44.9 [44.9–46.1] | 0.43x | 0.44x | 1.02x | 0.81x | 0.15x |
| storage | micro | 18.5 [18.4–19.2] | 19.8 [19.5–20.1] | 45.5 [45.5–45.5] | 41.1 [40.6–41.1] | 8.84 [8.47–9.03] | 45.8 [45.2–46.1] | 0.40x | 0.43x | 0.99x | 0.90x | 0.19x |
| mandelbrot | compute | 4.71s [4.69s–4.74s] | 7.45s [7.45s–7.47s] | 76.6 [75.6–76.8] | 9.82s [9.79s–9.88s] | 879.3 [877.2–912.2] | 76.2 [75.7–77.5] | 61.7x | 97.8x | 1.00x | 129x | 11.5x |
| nbody | compute | 60.1 [59.3–60.4] | 74.1 [73.7–74.5] | 47.8 [47.4–48.4] | 3.89s [3.85s–3.89s] | 166.1 [165.4–168.1] | 51.5 [50.7–51.5] | 1.17x | 1.44x | 0.93x | 75.5x | 3.23x |
| richards | macro | 554.3 [552.5–564.6] | 336.3 [335.8–382.8] | 77.6 [77.4–77.9] | 4.34s [4.29s–4.41s] | 198.9 [197.8–199.2] | 91.8 [90.8–94.1] | 6.04x | 3.66x | 0.85x | 47.2x | 2.17x |
| json | macro | 66.9 [66.5–67.1] | 70.3 [69.8–70.3] | 52.7 [52.6–53.6] | 180.2 [179.3–180.3] | 18.3 [18.1–18.7] | 48.2 [47.8–49.1] | 1.39x | 1.46x | 1.09x | 3.74x | 0.38x |
| deltablue | macro | 334.3 [323.9–334.7] | 245.6 [242.7–252.2] | 54.1 [53.8–54.3] | 1.61s [1.61s–1.63s] | 107.6 [107.4–107.8] | 57.4 [57.2–58.5] | 5.82x | 4.28x | 0.94x | 28.1x | 1.87x |
| havlak | macro | 261.3 [260.6–315.8] | 175.5 [165.8–177.8] | 54.3 [54.2–54.7] | 54.90s [54.69s–55.60s] | 3.25s [3.24s–3.38s] | 140.7 [133.2–143.8] | 1.86x | 1.25x | 0.39x | 390x | 23.1x |
| cd | macro | 849.8 [828.3–859.0] | 552.9 [550.8–559.3] | 67.5 [67.4–67.6] | 14.34s [14.31s–14.40s] | 961.5 [960.7–966.3] | 80.7 [80.5–80.9] | 10.5x | 6.85x | 0.84x | 178x | 11.9x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 33.2 [32.9–33.5] | 26.9 [26.9–26.9] | 47.8 [47.6–48.5] | 194.8 [194.0–196.0] | 29.3 [28.8–29.7] | 45.6 [44.8–45.8] | 0.73x | 0.59x | 1.05x | 4.28x | 0.64x |
| fannkuch | permutation | 62.6 [62.4–62.9] | 96.5 [96.4–97.6] | 44.9 [44.8–45.3] | 166.8 [166.3–167.8] | 12.9 [12.6–13.2] | 45.8 [45.3–45.9] | 1.37x | 2.11x | 0.98x | 3.64x | 0.28x |
| fasta | generation | 26.5 [26.4–26.6] | 31.2 [31.1–31.6] | 45.1 [45.0–45.5] | 65.5 [65.1–67.0] | 14.3 [14.1–14.9] | 47.5 [47.3–47.8] | 0.56x | 0.66x | 0.95x | 1.38x | 0.30x |
| knucleotide | hashing | 32.5 [30.1–32.6] | 59.1 [58.9–59.5] | 45.8 [45.6–46.1] | 80.4 [80.1–82.3] | 13.3 [13.3–13.9] | 46.5 [46.5–49.2] | 0.70x | 1.27x | 0.99x | 1.73x | 0.29x |
| pidigits | bignum | 15.1 [15.1–15.6] | 18.9 [15.3–19.6] | 46.9 [46.8–47.2] | 21.7 [21.4–21.9] | 5.41 [5.40–6.12] | 43.1 [42.8–43.6] | 0.35x | 0.44x | 1.09x | 0.50x | 0.13x |
| regexredux | regex | 14.4 [14.4–14.7] | 14.4 [14.0–14.8] | 47.5 [47.1–48.7] | 29.6 [28.5–30.4] | 11.1 [10.9–11.7] | 43.6 [43.6–44.2] | 0.33x | 0.33x | 1.09x | 0.68x | 0.26x |
| revcomp | string | 19.4 [19.3–19.6] | 20.0 [19.7–20.2] | 45.5 [45.2–45.6] | 52.1 [52.1–52.7] | 8.01 [7.95–8.61] | 44.7 [44.2–44.8] | 0.43x | 0.45x | 1.02x | 1.17x | 0.18x |
| spectralnorm | numeric | 44.7 [44.2–45.0] | 47.8 [47.6–47.8] | 45.3 [45.2–45.5] | 560.0 [560.0–566.5] | 70.3 [69.9–70.4] | 45.5 [45.1–45.6] | 0.98x | 1.05x | 1.00x | 12.3x | 1.54x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 197.6 [197.4–198.4] | 186.4 [185.7–186.7] | 73.5 [73.0–74.1] | 10.81s [10.79s–10.83s] | 854.8 [854.0–857.2] | 76.5 [76.2–77.6] | 2.58x | 2.44x | 0.96x | 141x | 11.2x |
| matmul | numeric | 2.69s [2.68s–2.69s] | 3.97s [3.94s–3.99s] | 50.6 [50.4–52.0] | 12.98s [12.93s–13.12s] | 543.0 [542.5–543.3] | 58.4 [58.3–59.3] | 46.0x | 67.9x | 0.87x | 222x | 9.30x |
| primes | numeric | 527.5 [527.1–527.9] | 942.6 [940.4–945.4] | 46.0 [45.9–46.8] | 3.36s [3.35s–3.36s] | 100.2 [99.4–100.7] | 47.0 [46.8–47.1] | 11.2x | 20.1x | 0.98x | 71.4x | 2.13x |
| base64 | string | 43.3 [43.2–43.5] | 35.4 [34.6–36.1] | 45.7 [45.1–46.1] | 2.11s [2.11s–2.12s] | 163.9 [161.7–164.8] | 60.1 [59.7–60.5] | 0.72x | 0.59x | 0.76x | 35.2x | 2.73x |
| levenshtein | string | 222.8 [222.5–223.2] | 275.7 [274.9–280.8] | 45.7 [45.7–46.5] | 1.32s [1.31s–1.33s] | 59.8 [59.6–59.9] | 46.7 [46.2–46.9] | 4.77x | 5.91x | 0.98x | 28.2x | 1.28x |
| json_gen | data | 55.6 [55.2–55.6] | 48.6 [48.4–48.9] | 46.1 [45.7–46.7] | 117.2 [117.2–118.9] | 25.8 [25.5–26.0] | 49.3 [48.9–49.9] | 1.13x | 0.99x | 0.94x | 2.38x | 0.52x |
| collatz | numeric | 577.8 [577.4–579.9] | 701.2 [688.9–705.1] | 269.5 [269.4–269.8] | 2.01s [2.00s–2.03s] | 6.22s [6.20s–6.23s] | 1.46s [1.45s–1.46s] | 0.40x | 0.48x | 0.19x | 1.38x | 4.27x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 262.5 [261.8–262.9] | 191.1 [191.1–191.8] | 104.6 [104.6–105.8] | 52.05s [52.04s–52.16s] | 2.17s [2.17s–2.18s] | 111.7 [111.6–112.6] | 2.35x | 1.71x | 0.94x | 466x | 19.5x |
| array1 | array | 204.3 [203.7–205.5] | 377.6 [375.3–378.6] | 44.5 [44.5–45.1] | 706.6 [706.1–712.5] | 41.6 [41.1–41.9] | 44.4 [44.0–44.7] | 4.60x | 8.51x | 1.00x | 15.9x | 0.94x |
| deriv | symbolic | 52.0 [51.9–52.2] | 32.2 [32.2–32.9] | 48.7 [48.6–49.1] | 417.7 [416.2–424.1] | 64.9 [64.8–65.5] | 47.2 [47.1–47.3] | 1.10x | 0.68x | 1.03x | 8.85x | 1.37x |
| diviter | iterative | 421.2 [420.6–421.8] | 27.0 [22.8–43.1] | 310.4 [309.8–310.4] | 1.38s [1.37s–1.38s] | 26.58s [26.56s–26.59s] | 513.0 [510.8–518.9] | 0.82x | 0.05x | 0.61x | 2.68x | 51.8x |
| divrec | recursive | 24.4 [24.3–24.5] | 20.3 [20.2–20.4] | 49.2 [49.1–49.5] | 731.0 [723.3–740.0] | 41.1 [40.8–41.6] | 50.4 [50.4–50.5] | 0.49x | 0.40x | 0.98x | 14.5x | 0.81x |
| gcbench | allocation | 310.4 [307.5–311.5] | 128.4 [127.9–128.9] | 116.2 [115.5–116.2] | 4.37s [4.36s–4.40s] | 567.5 [563.3–569.6] | 65.2 [65.1–65.2] | 4.76x | 1.97x | 1.78x | 67.0x | 8.70x |
| paraffins | combinat | 52.6 [52.5–52.8] | 51.6 [51.4–52.2] | 45.4 [45.4–46.1] | 80.8 [79.2–81.3] | 7.95 [7.90–8.71] | 44.2 [43.3–44.3] | 1.19x | 1.17x | 1.03x | 1.83x | 0.18x |
| pnpoly | numeric | 80.4 [79.9–80.5] | 102.8 [101.2–103.3] | 46.4 [46.3–47.2] | 164.5 [163.2–164.7] | 207.1 [206.3–207.2] | 49.0 [48.8–49.3] | 1.64x | 2.10x | 0.95x | 3.35x | 4.22x |
| puzzle | search | 37.1 [37.0–37.5] | 39.0 [38.2–39.2] | 46.1 [45.7–46.5] | 500.9 [496.4–504.2] | 34.7 [34.6–35.2] | 46.0 [45.5–46.4] | 0.81x | 0.85x | 1.00x | 10.9x | 0.75x |
| quicksort | sorting | 25.3 [24.9–25.4] | 27.9 [27.9–28.3] | 45.0 [44.7–46.0] | 71.2 [71.1–72.4] | 24.7 [24.7–25.3] | 44.8 [44.6–45.8] | 0.56x | 0.62x | 1.00x | 1.59x | 0.55x |
| ray | numeric | 65.1 [65.0–66.1] | 72.7 [72.4–72.7] | 45.6 [44.8–45.9] | 210.6 [210.3–212.8] | 19.1 [19.1–19.7] | 46.5 [46.2–46.5] | 1.40x | 1.56x | 0.98x | 4.53x | 0.41x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 100.0 [99.9–100.1] | 117.5 [117.3–117.8] | 49.0 [49.0–49.5] | 2.25s [2.24s–2.26s] | 222.4 [221.5–222.6] | 62.9 [62.7–63.7] | 1.59x | 1.87x | 0.78x | 35.7x | 3.53x |
| navier_stokes | numeric | 418.0 [415.8–418.0] | 409.8 [409.1–411.0] | 94.1 [94.0–95.0] | 2.07s [2.06s–2.09s] | 109.4 [108.9–109.4] | 59.4 [59.0–59.8] | 7.04x | 6.90x | 1.58x | 34.8x | 1.84x |
| splay | data | 458.4 [458.2–458.8] | 360.4 [360.3–360.9] | 65.8 [65.6–66.2] | 2.08s [2.07s–2.09s] | 553.4 [550.7–562.1] | 91.1 [90.8–91.8] | 5.03x | 3.96x | 0.72x | 22.9x | 6.08x |
| hashmap | data | 385.9 [379.7–395.5] | 380.8 [376.6–381.1] | 47.5 [47.3–48.1] | 5.05s [5.03s–5.07s] | 319.9 [318.8–323.6] | 59.0 [58.5–59.3] | 6.54x | 6.45x | 0.81x | 85.5x | 5.42x |
| crypto_sha1 | crypto | 182.4 [182.1–182.5] | 175.3 [174.5–177.0] | 48.8 [48.4–49.1] | 1.66s [1.66s–1.66s] | 224.1 [223.3–225.0] | 52.2 [51.9–52.4] | 3.50x | 3.36x | 0.93x | 31.8x | 4.30x |
| raytrace3d | 3d | 168.0 [167.5–168.1] | 101.9 [100.7–102.4] | 52.0 [51.8–52.6] | 1.16s [1.15s–1.16s] | 169.1 [167.9–169.5] | 61.7 [61.6–62.4] | 2.72x | 1.65x | 0.84x | 18.8x | 2.74x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 279.1 [278.8–279.6] | 79.6 [79.5–80.3] | 58.3 [58.2–58.9] | 7.34s [7.31s–7.38s] | 615.8 [615.8–622.8] | 83.0 [82.7–83.2] | 3.36x | 0.96x | 0.70x | 88.4x | 7.42x |
| microdiff | data-diff | 86.3 [85.8–87.4] | 91.8 [91.4–92.5] | 51.2 [51.0–51.2] | 1.49s [1.49s–1.49s] | 114.2 [113.5–114.2] | 59.5 [59.0–59.6] | 1.45x | 1.54x | 0.86x | 25.1x | 1.92x |
| hyphen | hyphenation | 128.6 [110.4–129.1] | 88.8 [88.8–88.8] | 75.9 [75.0–76.8] | 428.4 [428.4–429.5] | 100.3 [100.2–101.3] | 57.7 [57.3–57.7] | 2.23x | 1.54x | 1.32x | 7.42x | 1.74x |
| prettier_ast | formatting | 1.42s [1.41s–1.42s] | 1.50s [1.49s–1.52s] | 102.8 [102.4–103.8] | 11.51s [11.48s–11.54s] | 1.41s [1.41s–1.42s] | 144.5 [144.1–145.7] | 9.79x | 10.4x | 0.71x | 79.6x | 9.77x |
| text_search | search | 1.51s [1.51s–1.51s] | 1.45s [1.45s–1.46s] | 581.9 [581.5–582.5] | --- [180.00s–180.00s] | 38.62s [38.61s–38.62s] | 818.6 [817.1–820.6] | 1.85x | 1.78x | 0.71x | --- | 47.2x |
| three_way_merge | merge | 3.25s [3.25s–3.29s] | 2.31s [2.31s–2.34s] | 2.17s [2.17s–2.18s] | 30.76s [30.49s–30.83s] | 6.10s [6.10s–6.14s] | 1.00s [997.9–1.01s] | 3.24x | 2.30x | 2.17x | 30.7x | 6.08x |
| log_pipeline | log-processing | 5.34s [5.34s–5.36s] | 2.21s [2.21s–2.22s] | 669.9 [669.6–673.9] | 60.49s [60.43s–60.81s] | 9.42s [9.38s–9.50s] | 990.7 [987.9–993.3] | 5.39x | 2.23x | 0.68x | 61.1x | 9.51x |

