# Lambda Benchmark Results — Tune22

- **Date:** 2026-09-09
- **Platform:** Darwin arm64
- **Lambda commit:** `440c6485d1d57dbf3d7ad061f5ace2bdc057908b`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v40-440c6485d1` (19,479,624 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 58.40s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 58.3s (batched 57.8s: sync 45.1s, async 12.7s; non-batched 0.6s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v24.7.0
- **QuickJS:** 2026-06-04
- **Methodology:** 15 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 240s per run; suites run in order `r7rs -> awfy -> beng -> kostya -> larceny -> jetstream -> text` with a 10s idle gap between suites
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, Node.js
- **Results source:** `test/benchmark/benchmark_results_v40.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 0.64x | 0.64x | 0.28x |
| AWFY | 14 | 14 | 14 | 14 | 13 | 1.67x | 1.29x | 0.14x |
| BENG | 8 | 8 | 8 | 8 | 8 | 0.46x | 0.42x | 0.12x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 2.03x | 1.88x | 0.26x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 1.42x | 1.22x | 0.41x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 7.54x | 5.18x | 0.41x |
| Text | 7 | 7 | 7 | 7 | 7 | 1.98x | 1.50x | 0.15x |
| **Overall** | 63 | 63 | 63 | 63 | 62 | 1.42x | 1.20x | 0.22x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 59 | 1.07x | 1.26x | 5.34x | 59 |
| complete current suite | 63 | 1.20x | 1.42x | 5.49x | 63 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 5.49x over 63 of 63 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/deltablue | 113.2 | 1.21 | 93.3x |
| awfy/havlak | 62.9 | 1.80 | 35.0x |
| awfy/cd | 466.0 | 13.8 | 33.7x |
| jetstream/cube3d | 13.6 | 0.502 | 27.0x |
| text/hyphen | 2.18 | 0.089 | 24.6x |
| kostya/base64 | 12.3 | 0.506 | 24.3x |
| jetstream/hashmap | 62.2 | 2.62 | 23.7x |
| text/prettier_ast | 912.3 | 41.2 | 22.1x |
| beng/knucleotide | 4.72 | 0.254 | 18.6x |
| kostya/json_gen | 24.5 | 1.41 | 17.4x |
| awfy/richards | 374.8 | 21.9 | 17.1x |
| jetstream/splay | 280.8 | 17.5 | 16.0x |

---

### Notable Results

- Missing timings: **1** cells
- Node.js missing: awfy/nbody (wrong_output)

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.50 [1.30–1.58] | 1.31 [1.28–1.38] | 1.09 [0.945–1.37] | 1.35 [1.25–4.78] | 1.11x | 0.97x | 0.81x |
| fibfp | recursive | 1.87 [1.84–2.09] | 1.11 [1.06–1.92] | 1.10 [1.02–1.41] | 1.34 [1.23–1.56] | 1.40x | 0.83x | 0.82x |
| tak | recursive | 0.141 [0.138–0.156] | 0.225 [0.207–0.262] | 0.113 [0.109–0.155] | 0.302 [0.295–0.341] | 0.47x | 0.75x | 0.37x |
| cpstak | closure | 0.337 [0.280–0.583] | 0.496 [0.373–0.708] | 0.227 [0.219–0.242] | 0.484 [0.417–0.518] | 0.70x | 1.02x | 0.47x |
| sum | iterative | 1.36 [1.20–1.54] | 1.14 [1.11–1.48] | 0.262 [0.251–0.294] | 0.856 [0.778–2.30] | 1.58x | 1.34x | 0.31x |
| sumfp | iterative | 0.081 [0.068–0.112] | 0.067 [0.066–0.071] | 0.077 [0.071–0.091] | 0.917 [0.848–1.41] | 0.09x | 0.07x | 0.08x |
| nqueens | backtrack | 1.08 [0.912–1.21] | 1.40 [1.32–2.04] | 0.135 [0.123–0.164] | 1.22 [1.20–1.53] | 0.89x | 1.15x | 0.11x |
| fft | numeric | 0.256 [0.234–0.287] | 0.241 [0.199–0.269] | 0.023 [0.021–0.033] | 1.03 [0.951–1.20] | 0.25x | 0.23x | 0.02x |
| mbrot | numeric | 0.762 [0.603–0.818] | 0.530 [0.519–0.548] | 0.414 [0.376–0.463] | 0.865 [0.798–1.25] | 0.88x | 0.61x | 0.48x |
| ack | recursive | 13.3 [12.9–17.9] | 18.2 [15.5–19.7] | 11.7 [10.9–14.5] | 16.0 [15.7–16.5] | 0.83x | 1.14x | 0.73x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.032 [0.028–0.054] | 0.037 [0.036–0.082] | 0.017 [0.015–0.021] | 0.388 [0.335–0.515] | 0.08x | 0.10x | 0.04x |
| permute | micro | 0.252 [0.241–0.276] | 0.124 [0.121–0.195] | 0.025 [0.023–0.030] | 0.239 [0.222–0.270] | 1.05x | 0.52x | 0.11x |
| queens | micro | 0.343 [0.329–0.455] | 0.291 [0.285–0.343] | 0.021 [0.017–0.025] | 0.357 [0.345–0.428] | 0.96x | 0.82x | 0.06x |
| towers | micro | 0.633 [0.622–0.680] | 0.426 [0.342–0.482] | 0.028 [0.026–0.031] | 0.384 [0.341–0.453] | 1.65x | 1.11x | 0.07x |
| bounce | micro | 0.077 [0.063–0.086] | 0.062 [0.057–0.068] | 0.026 [0.022–0.033] | 0.394 [0.343–0.485] | 0.20x | 0.16x | 0.07x |
| list | micro | 0.562 [0.517–0.742] | 0.184 [0.174–0.210] | 0.023 [0.021–0.026] | 0.234 [0.210–0.254] | 2.41x | 0.79x | 0.10x |
| storage | micro | 0.447 [0.432–0.600] | 0.292 [0.279–0.340] | 0.093 [0.084–0.108] | 0.325 [0.297–0.374] | 1.38x | 0.90x | 0.29x |
| mandelbrot | compute | 30.6 [30.3–41.2] | 35.6 [30.3–39.0] | 22.5 [22.0–25.3] | 19.4 [19.2–21.4] | 1.57x | 1.83x | 1.16x |
| nbody | compute | 44.4 [37.8–53.8] | 16.8 [13.9–20.2] | 1.51 [1.39–1.63] | --- | --- | --- | --- |
| richards | macro | 369.0 [335.4–400.3] | 374.8 [333.0–382.0] | 21.9 [20.4–25.4] | 41.2 [37.4–50.0] | 8.95x | 9.09x | 0.53x |
| json | macro | 4.65 [4.57–4.78] | 2.53 [2.41–3.16] | 0.239 [0.227–0.327] | 1.60 [1.42–2.01] | 2.91x | 1.59x | 0.15x |
| deltablue | macro | 112.6 [99.0–120.1] | 113.2 [96.7–117.1] | 1.21 [1.08–1.44] | 7.06 [6.72–15.3] | 15.9x | 16.0x | 0.17x |
| havlak | macro | 55.9 [51.2–65.0] | 62.9 [53.5–72.6] | 1.80 [1.64–1.98] | 78.1 [68.7–94.6] | 0.72x | 0.81x | 0.02x |
| cd | macro | 550.3 [494.9–590.0] | 466.0 [417.2–488.5] | 13.8 [12.2–15.6] | 29.8 [27.2–40.9] | 18.5x | 15.6x | 0.46x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 8.94 [7.08–9.18] | 4.27 [4.06–4.75] | 3.00 [2.79–3.46] | 3.41 [3.22–4.04] | 2.62x | 1.25x | 0.88x |
| fannkuch | permutation | 0.381 [0.370–0.505] | 0.513 [0.432–0.802] | 0.147 [0.139–0.249] | 2.99 [2.70–3.86] | 0.13x | 0.17x | 0.05x |
| fasta | generation | 0.822 [0.706–0.931] | 0.821 [0.791–0.999] | 0.260 [0.229–0.298] | 4.41 [4.17–5.30] | 0.19x | 0.19x | 0.06x |
| knucleotide | hashing | 4.18 [3.86–6.88] | 4.72 [4.53–5.85] | 0.254 [0.245–0.267] | 4.71 [4.28–5.74] | 0.89x | 1.00x | 0.05x |
| pidigits | bignum | 0.347 [0.263–0.455] | 0.266 [0.258–0.297] | 0.050 [0.042–0.061] | 1.99 [1.90–2.34] | 0.17x | 0.13x | 0.03x |
| regexredux | regex | 1.48 [1.18–1.59] | 1.15 [1.13–1.26] | 1.23 [1.06–2.09] | 2.56 [2.42–7.07] | 0.58x | 0.45x | 0.48x |
| revcomp | string | 1.21 [1.15–1.75] | 1.12 [1.09–1.59] | 0.414 [0.355–0.743] | 3.06 [2.87–3.96] | 0.39x | 0.37x | 0.14x |
| spectralnorm | numeric | 1.56 [1.52–3.10] | 2.03 [1.69–2.25] | 0.328 [0.322–0.380] | 1.81 [1.65–2.36] | 0.86x | 1.12x | 0.18x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 210.8 [190.9–236.4] | 321.2 [278.3–341.6] | 28.3 [24.5–31.0] | 31.9 [28.2–38.4] | 6.62x | 10.1x | 0.89x |
| matmul | numeric | 17.3 [16.5–21.0] | 33.4 [32.6–40.4] | 5.47 [5.43–7.01] | 14.5 [13.5–16.5] | 1.19x | 2.29x | 0.38x |
| primes | numeric | 17.9 [17.1–18.9] | 4.04 [3.92–4.36] | 1.66 [1.50–2.60] | 4.75 [4.37–5.41] | 3.77x | 0.85x | 0.35x |
| base64 | string | 10.2 [9.51–10.7] | 12.3 [10.4–13.2] | 0.506 [0.473–0.570] | 15.4 [14.7–24.3] | 0.66x | 0.80x | 0.03x |
| levenshtein | string | 10.3 [9.93–12.8] | 7.54 [6.59–8.07] | 0.902 [0.811–0.955] | 2.61 [2.33–5.16] | 3.93x | 2.89x | 0.35x |
| json_gen | data | 24.6 [20.7–25.2] | 24.5 [22.2–32.4] | 1.41 [1.31–1.71] | 4.31 [4.08–4.53] | 5.71x | 5.69x | 0.33x |
| collatz | numeric | 416.3 [371.4–433.2] | 415.9 [377.6–433.1] | 218.1 [198.5–232.5] | 1.28s [1.25s–1.33s] | 0.32x | 0.32x | 0.17x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 286.9 [252.2–296.3] | 204.0 [174.7–210.5] | 59.4 [55.8–65.2] | 69.8 [61.9–74.6] | 4.11x | 2.92x | 0.85x |
| array1 | array | 1.03 [1.00–1.20] | 1.24 [0.909–1.40] | 0.280 [0.259–0.386] | 1.62 [1.58–1.86] | 0.63x | 0.77x | 0.17x |
| deriv | symbolic | 29.0 [26.6–30.6] | 14.7 [12.5–15.5] | 2.47 [2.36–2.65] | 2.22 [2.07–2.50] | 13.1x | 6.64x | 1.11x |
| diviter | iterative | 542.5 [510.6–592.8] | 549.9 [498.7–581.6] | 278.6 [247.9–286.2] | 422.6 [375.0–435.2] | 1.28x | 1.30x | 0.66x |
| divrec | recursive | 7.35 [6.95–7.99] | 2.34 [2.24–2.42] | 5.07 [4.84–5.88] | 8.18 [8.00–8.56] | 0.90x | 0.29x | 0.62x |
| gcbench | allocation | 173.5 [165.2–206.4] | 170.2 [157.7–204.6] | 69.6 [64.7–71.4] | 20.9 [20.2–21.8] | 8.32x | 8.16x | 3.34x |
| paraffins | combinat | 0.279 [0.231–0.312] | 0.251 [0.245–0.270] | 0.045 [0.040–0.056] | 0.616 [0.590–0.832] | 0.45x | 0.41x | 0.07x |
| pnpoly | numeric | 11.9 [11.2–13.4] | 16.1 [13.1–17.8] | 1.98 [1.72–3.16] | 4.36 [4.07–5.11] | 2.73x | 3.69x | 0.45x |
| puzzle | search | 2.65 [2.38–3.01] | 2.35 [2.32–2.57] | 1.43 [1.26–1.49] | 2.22 [2.09–11.4] | 1.19x | 1.06x | 0.64x |
| quicksort | sorting | 0.992 [0.965–1.26] | 1.02 [0.988–1.22] | 0.203 [0.193–0.230] | 1.66 [1.12–1.80] | 0.60x | 0.61x | 0.12x |
| ray | numeric | 0.255 [0.252–0.299] | 0.310 [0.257–0.363] | 0.138 [0.133–0.182] | 1.53 [1.41–1.74] | 0.17x | 0.20x | 0.09x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 12.3 [11.4–25.6] | 13.6 [12.0–16.8] | 0.502 [0.440–0.625] | 13.3 [12.7–17.3] | 0.92x | 1.02x | 0.04x |
| navier_stokes | numeric | 154.7 [140.3–165.4] | 162.3 [139.4–168.8] | 34.1 [32.3–45.2] | 7.63 [7.31–9.27] | 20.3x | 21.3x | 4.47x |
| splay | data | 286.2 [252.2–310.1] | 280.8 [254.4–314.9] | 17.5 [16.5–21.2] | 8.07 [7.17–8.97] | 35.4x | 34.8x | 2.17x |
| hashmap | data | 107.6 [99.6–121.0] | 62.2 [51.8–81.1] | 2.62 [2.49–2.79] | 11.9 [11.5–13.9] | 9.03x | 5.22x | 0.22x |
| crypto_sha1 | crypto | 43.0 [41.2–51.2] | 25.5 [24.6–31.1] | 2.73 [2.43–2.97] | 7.52 [6.01–8.03] | 5.73x | 3.39x | 0.36x |
| raytrace3d | 3d | 65.5 [56.4–74.9] | 17.8 [16.7–21.6] | 1.88 [1.78–2.46] | 12.2 [11.6–21.3] | 5.37x | 1.46x | 0.15x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 252.1 [220.0–260.6] | 144.5 [127.9–154.2] | 13.7 [12.1–15.4] | 32.9 [30.3–44.4] | 7.67x | 4.40x | 0.42x |
| microdiff | data-diff | 0.131 [0.122–0.172] | 0.133 [0.132–0.176] | 0.018 [0.016–0.022] | 15.8 [15.6–54.7] | 0.008x | 0.008x | 0.001x |
| hyphen | hyphenation | 2.76 [2.50–3.19] | 2.18 [1.72–2.46] | 0.089 [0.081–0.177] | 5.70 [5.20–6.50] | 0.48x | 0.38x | 0.02x |
| prettier_ast | formatting | 911.5 [850.7–932.2] | 912.3 [844.2–959.4] | 41.2 [36.7–49.1] | 90.2 [80.7–95.5] | 10.1x | 10.1x | 0.46x |
| text_search | search | 14.97s [14.51s–15.40s] | 5.30s [4.96s–5.63s] | 556.2 [501.3–600.7] | 747.2 [692.9–801.4] | 20.0x | 7.09x | 0.74x |
| three_way_merge | merge | 3.45s [3.37s–3.51s] | 3.19s [3.12s–3.27s] | 1.44s [1.41s–1.56s] | 1.02s [945.2–1.11s] | 3.39x | 3.13x | 1.42x |
| log_pipeline | log-processing | 5.97s [5.89s–6.18s] | 5.63s [5.55s–5.78s] | 570.1 [508.2–621.7] | 1.04s [1.01s–1.08s] | 5.74x | 5.41x | 0.55x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 0.64x | 0.65x | 1.54x |
| AWFY | 14 | 14 | 14 | 14 | 13 | 1.37x | 1.44x | 1.33x |
| BENG | 8 | 8 | 8 | 8 | 8 | 0.74x | 0.82x | 1.55x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 1.01x | 1.09x | 0.92x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 1.19x | 1.11x | 1.43x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 4.06x | 5.48x | 1.23x |
| Text | 7 | 7 | 7 | 7 | 7 | 3.27x | 2.51x | 0.93x |
| **Overall** | 63 | 63 | 63 | 63 | 62 | 1.29x | 1.32x | 1.29x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 16.0 [15.3–17.3] | 15.8 [15.2–21.5] | 46.9 [40.9–468.0] | 26.6 [26.2–81.6] | 0.60x | 0.60x | 1.76x |
| fibfp | recursive | 17.8 [16.6–27.5] | 15.7 [15.3–16.7] | 45.8 [41.2–48.8] | 25.7 [25.2–27.5] | 0.69x | 0.61x | 1.78x |
| tak | recursive | 15.7 [14.6–16.7] | 15.8 [14.6–16.4] | 42.1 [38.9–56.4] | 25.9 [25.0–26.8] | 0.60x | 0.61x | 1.62x |
| cpstak | closure | 16.5 [15.5–22.1] | 19.1 [16.3–22.9] | 42.1 [39.4–52.1] | 27.9 [25.7–32.5] | 0.59x | 0.68x | 1.51x |
| sum | iterative | 18.4 [17.1–19.1] | 15.2 [14.8–16.6] | 43.0 [39.1–47.8] | 29.1 [28.0–50.1] | 0.63x | 0.52x | 1.48x |
| sumfp | iterative | 14.3 [13.4–15.9] | 13.6 [13.2–14.3] | 46.0 [40.4–52.3] | 31.0 [28.4–33.9] | 0.46x | 0.44x | 1.48x |
| nqueens | backtrack | 23.3 [22.3–24.2] | 27.1 [25.4–28.1] | 47.6 [41.0–51.8] | 26.4 [25.7–31.2] | 0.88x | 1.02x | 1.80x |
| fft | numeric | 23.2 [20.7–24.8] | 23.6 [22.8–30.1] | 42.9 [39.2–54.1] | 30.1 [28.2–32.5] | 0.77x | 0.78x | 1.42x |
| mbrot | numeric | 18.1 [17.3–21.1] | 17.9 [16.9–18.7] | 45.1 [40.5–50.5] | 30.2 [29.3–33.5] | 0.60x | 0.59x | 1.49x |
| ack | recursive | 28.0 [27.1–43.2] | 34.4 [28.7–36.5] | 52.8 [49.2–67.4] | 44.4 [43.6–46.8] | 0.63x | 0.77x | 1.19x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 14.7 [14.3–21.6] | 16.1 [15.1–17.1] | 44.2 [41.9–54.7] | 28.6 [25.4–31.6] | 0.52x | 0.56x | 1.54x |
| permute | micro | 15.4 [15.0–16.3] | 18.4 [16.3–23.1] | 43.7 [41.5–47.6] | 27.9 [27.5–30.1] | 0.55x | 0.66x | 1.57x |
| queens | micro | 19.4 [19.1–28.1] | 25.3 [23.5–25.9] | 44.4 [39.9–47.8] | 25.3 [24.6–27.3] | 0.77x | 1.00x | 1.76x |
| towers | micro | 20.5 [19.6–22.5] | 23.1 [19.7–24.1] | 40.1 [38.4–51.8] | 28.4 [25.8–31.6] | 0.72x | 0.81x | 1.41x |
| bounce | micro | 24.7 [21.5–35.0] | 27.0 [25.3–38.7] | 45.1 [43.7–63.6] | 29.0 [28.3–31.5] | 0.85x | 0.93x | 1.55x |
| list | micro | 17.4 [17.1–17.7] | 18.8 [18.4–20.9] | 44.6 [42.6–57.4] | 28.9 [28.4–31.7] | 0.60x | 0.65x | 1.54x |
| storage | micro | 15.8 [15.3–16.1] | 16.7 [16.2–18.0] | 44.0 [42.0–47.2] | 28.3 [27.9–30.8] | 0.56x | 0.59x | 1.55x |
| mandelbrot | compute | 49.4 [47.3–55.9] | 49.2 [46.1–61.3] | 66.6 [65.8–73.7] | 44.6 [44.1–50.5] | 1.11x | 1.10x | 1.50x |
| nbody | compute | 66.5 [64.0–77.6] | 49.4 [48.6–53.2] | 47.3 [45.2–51.9] | --- [31.6–33.7] | --- | --- | --- |
| richards | macro | 421.1 [386.8–440.9] | 432.0 [387.8–479.7] | 64.5 [61.4–73.7] | 70.6 [65.8–79.0] | 5.97x | 6.12x | 0.91x |
| json | macro | 53.7 [47.4–55.5] | 57.4 [49.3–70.9] | 45.7 [44.5–55.3] | 29.2 [26.5–33.8] | 1.84x | 1.97x | 1.57x |
| deltablue | macro | 230.8 [198.9–250.7] | 207.8 [197.6–247.0] | 53.6 [52.0–60.2] | 33.4 [32.6–43.0] | 6.91x | 6.23x | 1.61x |
| havlak | macro | 158.7 [132.7–160.5] | 149.9 [141.6–182.8] | 52.9 [48.2–59.0] | 108.9 [97.3–136.2] | 1.46x | 1.38x | 0.49x |
| cd | macro | 618.4 [568.3–641.2] | 526.4 [480.2–556.7] | 65.6 [58.2–70.2] | 58.3 [53.8–67.9] | 10.6x | 9.02x | 1.12x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 24.3 [23.9–33.7] | 27.0 [24.1–27.8] | 47.6 [46.7–50.3] | 26.4 [25.9–32.0] | 0.92x | 1.03x | 1.80x |
| fannkuch | permutation | 21.1 [20.5–23.1] | 27.2 [22.9–36.2] | 41.3 [39.4–73.9] | 29.1 [27.6–31.8] | 0.73x | 0.93x | 1.42x |
| fasta | generation | 23.8 [22.3–24.9] | 22.7 [21.9–24.0] | 45.6 [40.4–51.4] | 32.5 [31.5–36.2] | 0.73x | 0.70x | 1.40x |
| knucleotide | hashing | 27.1 [26.4–27.6] | 40.0 [38.7–41.5] | 40.7 [39.1–44.9] | 30.0 [28.5–34.0] | 0.90x | 1.33x | 1.36x |
| pidigits | bignum | 21.0 [20.0–28.9] | 18.1 [17.8–18.5] | 45.8 [42.1–50.9] | 27.9 [26.9–38.5] | 0.75x | 0.65x | 1.64x |
| regexredux | regex | 14.0 [13.4–15.6] | 13.8 [13.3–68.2] | 54.6 [43.4–97.4] | 29.8 [29.3–57.4] | 0.47x | 0.46x | 1.83x |
| revcomp | string | 19.3 [19.0–24.8] | 24.5 [22.4–26.7] | 48.3 [45.8–60.7] | 28.6 [26.0–39.2] | 0.67x | 0.86x | 1.69x |
| spectralnorm | numeric | 25.2 [23.9–27.8] | 28.3 [23.8–90.0] | 41.5 [39.9–44.6] | 30.8 [28.6–34.4] | 0.82x | 0.92x | 1.35x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 244.4 [216.0–262.4] | 331.7 [298.4–355.9] | 73.1 [66.9–78.7] | 65.5 [53.7–170.1] | 3.73x | 5.07x | 1.12x |
| matmul | numeric | 41.3 [38.7–42.8] | 61.2 [53.2–64.0] | 45.5 [44.2–53.0] | 42.5 [40.8–47.1] | 0.97x | 1.44x | 1.07x |
| primes | numeric | 29.8 [28.9–36.7] | 21.1 [19.7–22.9] | 46.7 [45.4–62.0] | 32.7 [28.7–36.3] | 0.91x | 0.65x | 1.43x |
| base64 | string | 31.7 [30.3–36.1] | 37.7 [37.3–44.3] | 42.0 [40.5–49.4] | 43.6 [43.2–62.5] | 0.73x | 0.86x | 0.96x |
| levenshtein | string | 32.0 [31.1–34.1] | 30.0 [26.2–31.0] | 41.4 [39.4–43.7] | 30.5 [27.7–34.5] | 1.05x | 0.98x | 1.36x |
| json_gen | data | 42.3 [41.6–44.9] | 43.5 [42.6–52.5] | 41.8 [40.3–49.2] | 32.3 [31.9–34.3] | 1.31x | 1.35x | 1.29x |
| collatz | numeric | 431.1 [386.1–462.7] | 440.2 [386.5–447.7] | 258.1 [238.2–279.4] | 1.31s [1.27s–1.36s] | 0.33x | 0.34x | 0.20x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 315.1 [274.8–336.9] | 222.2 [199.2–235.4] | 98.6 [94.5–113.5] | 97.9 [86.5–102.8] | 3.22x | 2.27x | 1.01x |
| array1 | array | 16.1 [15.5–25.5] | 17.0 [16.0–18.0] | 41.8 [37.9–49.9] | 27.9 [25.6–29.1] | 0.58x | 0.61x | 1.50x |
| deriv | symbolic | 46.6 [45.5–61.8] | 36.2 [30.9–38.0] | 42.9 [41.3–44.3] | 30.4 [28.3–33.2] | 1.53x | 1.19x | 1.41x |
| diviter | iterative | 569.0 [514.8–592.6] | 568.6 [515.5–597.4] | 322.6 [286.2–334.3] | 450.5 [399.6–462.4] | 1.26x | 1.26x | 0.72x |
| divrec | recursive | 24.4 [20.8–25.1] | 17.4 [17.1–19.0] | 48.9 [43.8–53.6] | 35.5 [34.4–38.4] | 0.69x | 0.49x | 1.38x |
| gcbench | allocation | 217.1 [186.2–219.1] | 222.3 [190.8–237.4] | 114.8 [106.2–131.2] | 44.7 [43.5–47.7] | 4.86x | 4.97x | 2.57x |
| paraffins | combinat | 43.1 [36.8–51.7] | 44.8 [39.4–47.1] | 44.8 [43.0–52.2] | 25.5 [24.6–26.5] | 1.69x | 1.76x | 1.76x |
| pnpoly | numeric | 32.5 [30.5–35.0] | 30.7 [29.7–32.5] | 47.8 [42.1–52.1] | 32.6 [31.4–35.8] | 1.00x | 0.94x | 1.46x |
| puzzle | search | 18.4 [18.0–18.7] | 20.0 [19.6–21.5] | 47.0 [44.9–54.3] | 30.1 [29.5–39.3] | 0.61x | 0.66x | 1.56x |
| quicksort | sorting | 18.3 [17.9–19.1] | 22.2 [20.0–22.7] | 44.2 [42.8–49.6] | 28.7 [26.2–31.2] | 0.64x | 0.77x | 1.54x |
| ray | numeric | 24.7 [23.6–25.9] | 19.9 [18.9–20.6] | 43.3 [38.4–47.4] | 27.4 [25.9–28.5] | 0.90x | 0.73x | 1.58x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 147.7 [130.8–159.6] | 211.1 [187.1–279.8] | 48.5 [45.8–54.9] | 42.5 [41.6–45.7] | 3.47x | 4.96x | 1.14x |
| navier_stokes | numeric | 219.1 [204.9–250.3] | 234.0 [202.4–244.7] | 77.8 [73.7–91.0] | 38.0 [36.9–42.4] | 5.77x | 6.16x | 2.05x |
| splay | data | 353.4 [311.6–363.0] | 354.2 [320.1–371.7] | 60.7 [57.4–72.8] | 67.4 [58.4–87.2] | 5.25x | 5.26x | 0.90x |
| hashmap | data | 132.8 [127.4–152.9] | 95.5 [80.5–99.6] | 42.6 [41.5–59.0] | 40.6 [39.9–52.3] | 3.27x | 2.35x | 1.05x |
| crypto_sha1 | crypto | 81.7 [70.0–94.4] | 571.6 [519.0–615.6] | 48.7 [47.6–55.2] | 33.4 [31.3–38.3] | 2.44x | 17.1x | 1.46x |
| raytrace3d | 3d | 230.6 [201.8–249.6] | 181.2 [161.4–211.5] | 47.0 [44.7–53.9] | 43.5 [41.4–61.2] | 5.30x | 4.17x | 1.08x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 276.7 [245.4–299.0] | 162.2 [150.1–180.5] | 58.5 [52.3–63.0] | 62.0 [56.9–89.7] | 4.46x | 2.62x | 0.94x |
| microdiff | data-diff | 16.0 [15.6–17.1] | 18.2 [17.3–19.0] | 45.2 [43.9–48.8] | 41.3 [40.5–84.8] | 0.39x | 0.44x | 1.10x |
| hyphen | hyphenation | 29.2 [27.4–30.7] | 22.4 [21.9–22.7] | 42.7 [39.4–55.5] | 38.3 [37.0–40.7] | 0.76x | 0.58x | 1.11x |
| prettier_ast | formatting | 1.00s [954.4–1.05s] | 1.02s [967.2–1.06s] | 102.7 [93.1–106.5] | 120.0 [107.2–124.0] | 8.34x | 8.47x | 0.86x |
| text_search | search | 14.97s [14.45s–15.29s] | 5.25s [4.94s–5.62s] | 596.9 [545.6–646.1] | 773.5 [718.5–926.1] | 19.4x | 6.79x | 0.77x |
| three_way_merge | merge | 3.49s [3.43s–3.61s] | 3.18s [3.13s–3.24s] | 1.48s [1.45s–1.60s] | 1.04s [973.7–1.14s] | 3.34x | 3.04x | 1.42x |
| log_pipeline | log-processing | 6.09s [5.97s–6.21s] | 5.71s [5.59s–5.94s] | 617.3 [555.6–667.1] | 1.07s [1.05s–1.12s] | 5.68x | 5.33x | 0.58x |

