# Lambda Benchmark Results — Tune22

- **Date:** 2026-09-09
- **Platform:** Darwin arm64
- **Lambda commit:** `440c6485d1d57dbf3d7ad061f5ace2bdc057908b`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v39-440c6485d1` (19,479,624 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 55.70s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 55.6s (batched 55.1s: sync 42.8s, async 12.3s; non-batched 0.5s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v24.7.0
- **QuickJS:** 2026-06-04
- **Methodology:** 15 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 240s per run; suites run in order `r7rs -> awfy -> beng -> kostya -> larceny -> jetstream -> text` with a 10s idle gap between suites
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, Node.js
- **Results source:** `test/benchmark/benchmark_results_v39.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 0.62x | 0.58x | 0.28x |
| AWFY | 14 | 14 | 14 | 14 | 13 | 1.74x | 1.24x | 0.13x |
| BENG | 8 | 8 | 8 | 8 | 8 | 0.43x | 0.42x | 0.11x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 2.13x | 1.87x | 0.26x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 1.42x | 1.19x | 0.40x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 7.46x | 5.06x | 0.40x |
| Text | 7 | 7 | 7 | 7 | 7 | 2.05x | 1.46x | 0.15x |
| **Overall** | 63 | 63 | 63 | 63 | 62 | 1.42x | 1.16x | 0.22x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 59 | 1.04x | 1.27x | 5.21x | 59 |
| complete current suite | 63 | 1.16x | 1.42x | 5.36x | 63 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 5.36x over 63 of 63 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/deltablue | 106.5 | 1.09 | 97.8x |
| awfy/havlak | 63.9 | 1.62 | 39.3x |
| awfy/cd | 465.8 | 13.4 | 34.7x |
| jetstream/cube3d | 13.1 | 0.495 | 26.4x |
| text/prettier_ast | 900.3 | 40.4 | 22.3x |
| beng/knucleotide | 5.07 | 0.258 | 19.7x |
| jetstream/hashmap | 53.7 | 2.78 | 19.3x |
| kostya/base64 | 10.6 | 0.556 | 19.1x |
| text/hyphen | 1.76 | 0.094 | 18.8x |
| kostya/json_gen | 24.6 | 1.35 | 18.3x |
| awfy/richards | 375.5 | 21.3 | 17.7x |
| jetstream/splay | 284.2 | 17.3 | 16.4x |

---

### Notable Results

- Missing timings: **1** cells
- Node.js missing: awfy/nbody (wrong_output)

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.31 [1.26–1.38] | 1.33 [1.26–1.47] | 1.16 [1.04–1.37] | 1.46 [1.38–1.62] | 0.90x | 0.91x | 0.80x |
| fibfp | recursive | 1.82 [1.80–2.14] | 1.09 [1.05–1.33] | 1.16 [1.08–1.37] | 1.44 [1.32–1.51] | 1.27x | 0.76x | 0.81x |
| tak | recursive | 0.152 [0.141–0.191] | 0.182 [0.176–0.192] | 0.117 [0.111–0.148] | 0.338 [0.315–0.390] | 0.45x | 0.54x | 0.35x |
| cpstak | closure | 0.322 [0.289–0.384] | 0.366 [0.353–0.376] | 0.243 [0.217–0.302] | 0.474 [0.440–0.497] | 0.68x | 0.77x | 0.51x |
| sum | iterative | 1.29 [1.14–1.54] | 1.13 [1.11–1.45] | 0.260 [0.251–0.326] | 0.778 [0.722–1.14] | 1.66x | 1.45x | 0.33x |
| sumfp | iterative | 0.076 [0.068–0.089] | 0.078 [0.066–0.090] | 0.071 [0.069–0.097] | 0.869 [0.795–0.952] | 0.09x | 0.09x | 0.08x |
| nqueens | backtrack | 1.04 [0.988–1.27] | 1.34 [1.27–1.72] | 0.130 [0.122–0.161] | 1.33 [1.28–1.46] | 0.78x | 1.01x | 0.10x |
| fft | numeric | 0.256 [0.238–0.314] | 0.197 [0.194–0.208] | 0.024 [0.021–0.030] | 1.04 [0.920–1.15] | 0.25x | 0.19x | 0.02x |
| mbrot | numeric | 0.671 [0.610–0.823] | 0.529 [0.525–0.560] | 0.404 [0.366–0.438] | 0.845 [0.784–0.901] | 0.79x | 0.63x | 0.48x |
| ack | recursive | 14.9 [14.3–17.8] | 14.7 [14.6–14.9] | 12.9 [12.3–13.2] | 14.1 [13.7–16.1] | 1.06x | 1.05x | 0.92x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.035 [0.032–0.044] | 0.037 [0.036–0.043] | 0.018 [0.014–0.023] | 0.376 [0.343–0.572] | 0.09x | 0.10x | 0.05x |
| permute | micro | 0.289 [0.254–0.345] | 0.121 [0.119–0.144] | 0.024 [0.022–0.033] | 0.248 [0.233–0.281] | 1.17x | 0.49x | 0.10x |
| queens | micro | 0.397 [0.334–0.466] | 0.281 [0.276–0.300] | 0.021 [0.019–0.029] | 0.383 [0.364–0.414] | 1.04x | 0.73x | 0.05x |
| towers | micro | 0.724 [0.638–0.847] | 0.344 [0.324–0.362] | 0.032 [0.026–0.041] | 0.384 [0.359–0.446] | 1.89x | 0.90x | 0.08x |
| bounce | micro | 0.074 [0.064–0.091] | 0.063 [0.062–0.096] | 0.027 [0.023–0.030] | 0.388 [0.366–0.419] | 0.19x | 0.16x | 0.07x |
| list | micro | 0.594 [0.530–0.749] | 0.182 [0.178–0.228] | 0.023 [0.020–0.032] | 0.231 [0.207–0.239] | 2.58x | 0.79x | 0.10x |
| storage | micro | 0.518 [0.472–0.593] | 0.293 [0.278–0.313] | 0.099 [0.086–0.121] | 0.331 [0.293–0.371] | 1.57x | 0.89x | 0.30x |
| mandelbrot | compute | 34.3 [30.4–44.1] | 35.1 [33.9–39.7] | 22.3 [20.2–24.4] | 23.2 [22.1–23.5] | 1.48x | 1.52x | 0.96x |
| nbody | compute | 39.6 [37.6–48.0] | 13.9 [13.6–15.8] | 1.46 [1.33–2.58] | --- | --- | --- | --- |
| richards | macro | 376.7 [335.2–394.1] | 375.5 [332.2–384.3] | 21.3 [20.7–23.9] | 40.3 [39.5–42.2] | 9.36x | 9.33x | 0.53x |
| json | macro | 4.90 [4.56–6.13] | 2.93 [2.55–3.35] | 0.264 [0.231–0.281] | 1.55 [1.42–1.65] | 3.17x | 1.89x | 0.17x |
| deltablue | macro | 111.9 [98.2–123.2] | 106.5 [96.4–118.3] | 1.09 [1.06–2.46] | 7.54 [7.08–8.06] | 14.8x | 14.1x | 0.14x |
| havlak | macro | 58.0 [51.2–65.5] | 63.9 [54.0–68.4] | 1.62 [1.56–1.90] | 84.3 [78.5–92.0] | 0.69x | 0.76x | 0.02x |
| cd | macro | 556.9 [491.7–584.9] | 465.8 [410.4–483.7] | 13.4 [12.3–15.4] | 29.4 [29.0–30.8] | 18.9x | 15.8x | 0.46x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 8.11 [7.42–9.65] | 4.19 [4.07–4.57] | 2.93 [2.80–3.54] | 3.71 [3.45–3.84] | 2.19x | 1.13x | 0.79x |
| fannkuch | permutation | 0.377 [0.371–0.464] | 0.442 [0.426–0.526] | 0.163 [0.142–0.187] | 2.93 [2.83–3.03] | 0.13x | 0.15x | 0.06x |
| fasta | generation | 0.696 [0.660–0.778] | 0.845 [0.797–1.05] | 0.244 [0.232–0.282] | 4.24 [3.82–4.77] | 0.16x | 0.20x | 0.06x |
| knucleotide | hashing | 3.99 [3.91–4.37] | 5.07 [4.58–5.98] | 0.258 [0.247–0.297] | 4.69 [4.28–5.69] | 0.85x | 1.08x | 0.05x |
| pidigits | bignum | 0.292 [0.265–0.372] | 0.301 [0.276–0.341] | 0.044 [0.042–0.054] | 2.01 [1.89–2.33] | 0.15x | 0.15x | 0.02x |
| regexredux | regex | 1.29 [1.19–1.53] | 1.34 [1.19–1.53] | 1.05 [1.04–1.22] | 2.43 [2.16–2.68] | 0.53x | 0.55x | 0.43x |
| revcomp | string | 1.35 [1.18–1.49] | 1.25 [1.05–1.47] | 0.363 [0.330–0.471] | 3.28 [3.15–3.53] | 0.41x | 0.38x | 0.11x |
| spectralnorm | numeric | 1.74 [1.56–2.03] | 1.51 [1.49–1.69] | 0.357 [0.331–0.419] | 1.85 [1.71–2.01] | 0.94x | 0.82x | 0.19x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 218.4 [194.4–241.8] | 312.3 [280.0–336.3] | 25.2 [23.4–29.6] | 32.8 [30.4–35.8] | 6.65x | 9.51x | 0.77x |
| matmul | numeric | 19.3 [18.2–21.8] | 36.0 [32.2–50.0] | 5.54 [5.41–7.06] | 14.7 [14.1–15.1] | 1.32x | 2.45x | 0.38x |
| primes | numeric | 16.5 [15.5–18.9] | 3.98 [3.92–6.59] | 1.58 [1.49–1.69] | 4.87 [4.61–5.07] | 3.38x | 0.82x | 0.32x |
| base64 | string | 11.5 [10.6–13.1] | 10.6 [9.84–12.4] | 0.556 [0.507–0.589] | 13.8 [13.5–15.0] | 0.84x | 0.77x | 0.04x |
| levenshtein | string | 11.1 [10.2–13.6] | 7.25 [6.49–8.25] | 0.954 [0.855–1.07] | 2.56 [2.44–4.77] | 4.34x | 2.83x | 0.37x |
| json_gen | data | 24.3 [20.5–26.9] | 24.6 [23.7–26.8] | 1.35 [1.30–1.60] | 4.28 [4.02–4.66] | 5.66x | 5.76x | 0.31x |
| collatz | numeric | 421.4 [371.2–427.8] | 421.2 [371.8–441.4] | 214.1 [199.6–227.3] | 1.28s [1.24s–1.32s] | 0.33x | 0.33x | 0.17x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 283.1 [253.1–294.7] | 198.9 [174.8–208.0] | 61.9 [58.5–68.2] | 68.2 [62.2–75.1] | 4.15x | 2.92x | 0.91x |
| array1 | array | 1.12 [1.03–1.31] | 1.15 [1.04–1.32] | 0.285 [0.263–0.339] | 1.71 [1.58–1.86] | 0.65x | 0.67x | 0.17x |
| deriv | symbolic | 27.3 [25.6–29.5] | 12.9 [12.2–13.5] | 2.74 [2.50–3.20] | 2.27 [2.00–2.65] | 12.1x | 5.70x | 1.21x |
| diviter | iterative | 554.0 [505.9–601.8] | 543.8 [505.0–572.7] | 272.5 [251.4–287.1] | 416.0 [377.2–441.6] | 1.33x | 1.31x | 0.66x |
| divrec | recursive | 6.88 [6.48–7.86] | 2.38 [2.27–2.44] | 5.34 [4.83–5.67] | 8.47 [8.23–8.66] | 0.81x | 0.28x | 0.63x |
| gcbench | allocation | 188.4 [165.5–204.3] | 184.4 [159.3–203.3] | 68.7 [63.2–72.5] | 20.8 [20.5–24.6] | 9.06x | 8.87x | 3.31x |
| paraffins | combinat | 0.243 [0.231–0.303] | 0.252 [0.243–0.292] | 0.044 [0.040–0.054] | 0.643 [0.609–0.681] | 0.38x | 0.39x | 0.07x |
| pnpoly | numeric | 13.7 [11.9–15.2] | 14.0 [13.1–17.1] | 1.96 [1.72–2.38] | 3.89 [3.82–3.94] | 3.52x | 3.59x | 0.50x |
| puzzle | search | 2.36 [2.30–2.69] | 2.65 [2.43–3.14] | 1.24 [1.22–1.48] | 2.14 [2.06–2.47] | 1.10x | 1.24x | 0.58x |
| quicksort | sorting | 1.09 [1.03–1.17] | 1.18 [1.08–1.36] | 0.190 [0.185–0.228] | 1.75 [1.08–1.88] | 0.62x | 0.68x | 0.11x |
| ray | numeric | 0.285 [0.261–0.340] | 0.265 [0.252–0.330] | 0.141 [0.136–0.185] | 1.70 [1.63–1.82] | 0.17x | 0.16x | 0.08x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 12.8 [11.2–17.3] | 13.1 [11.8–13.7] | 0.495 [0.441–0.804] | 13.3 [12.7–24.2] | 0.96x | 0.98x | 0.04x |
| navier_stokes | numeric | 149.7 [140.6–164.2] | 155.3 [140.5–162.4] | 34.3 [32.2–39.4] | 7.56 [7.24–8.48] | 19.8x | 20.5x | 4.54x |
| splay | data | 283.4 [252.0–302.1] | 284.2 [252.3–291.9] | 17.3 [16.8–21.0] | 7.89 [7.51–11.9] | 35.9x | 36.0x | 2.20x |
| hashmap | data | 112.8 [100.0–132.7] | 53.7 [51.6–63.4] | 2.78 [2.59–3.24] | 12.0 [11.7–12.4] | 9.40x | 4.48x | 0.23x |
| crypto_sha1 | crypto | 43.1 [42.3–53.4] | 25.6 [25.1–27.1] | 2.49 [2.36–2.75] | 8.09 [6.19–15.0] | 5.33x | 3.16x | 0.31x |
| raytrace3d | 3d | 61.8 [56.4–70.7] | 20.1 [17.9–21.4] | 1.96 [1.84–2.22] | 12.3 [11.6–16.5] | 5.02x | 1.63x | 0.16x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 252.7 [225.0–258.9] | 147.5 [133.4–155.5] | 12.1 [12.0–14.0] | 32.7 [30.1–34.7] | 7.74x | 4.52x | 0.37x |
| microdiff | data-diff | 0.154 [0.137–0.183] | 0.141 [0.130–0.168] | 0.017 [0.015–0.027] | 16.9 [16.4–17.3] | 0.009x | 0.008x | 0.001x |
| hyphen | hyphenation | 3.06 [2.71–3.58] | 1.76 [1.71–1.85] | 0.094 [0.083–0.101] | 5.48 [5.15–5.88] | 0.56x | 0.32x | 0.02x |
| prettier_ast | formatting | 904.7 [853.6–931.6] | 900.3 [852.3–942.3] | 40.4 [36.6–41.3] | 89.2 [79.4–92.5] | 10.1x | 10.1x | 0.45x |
| text_search | search | 14.82s [14.40s–15.06s] | 5.21s [4.98s–5.36s] | 554.7 [501.0–582.1] | 758.2 [698.9–800.8] | 19.5x | 6.88x | 0.73x |
| three_way_merge | merge | 3.41s [3.35s–3.50s] | 3.12s [2.99s–3.16s] | 1.44s [1.40s–1.49s] | 998.9 [954.1–1.04s] | 3.42x | 3.12x | 1.44x |
| log_pipeline | log-processing | 5.96s [5.88s–6.04s] | 5.57s [5.47s–5.81s] | 552.4 [499.6–573.0] | 1.06s [983.6–1.08s] | 5.64x | 5.28x | 0.52x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 0.62x | 0.60x | 1.51x |
| AWFY | 14 | 14 | 14 | 14 | 13 | 1.31x | 1.33x | 1.28x |
| BENG | 8 | 8 | 8 | 8 | 8 | 0.75x | 0.79x | 1.48x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 1.04x | 1.13x | 0.95x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 1.24x | 1.11x | 1.43x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 4.06x | 5.39x | 1.25x |
| Text | 7 | 7 | 7 | 7 | 7 | 3.26x | 2.47x | 0.90x |
| **Overall** | 63 | 63 | 63 | 63 | 62 | 1.29x | 1.28x | 1.27x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 14.8 [14.6–15.3] | 16.4 [15.5–19.1] | 45.3 [44.4–55.4] | 29.6 [29.0–32.3] | 0.50x | 0.56x | 1.53x |
| fibfp | recursive | 15.4 [15.0–15.5] | 15.9 [15.2–23.4] | 45.9 [44.4–54.1] | 29.0 [27.8–29.9] | 0.53x | 0.55x | 1.59x |
| tak | recursive | 14.7 [14.1–16.9] | 14.1 [13.9–15.0] | 43.2 [39.9–46.9] | 28.1 [27.1–29.9] | 0.52x | 0.50x | 1.54x |
| cpstak | closure | 16.7 [15.1–18.4] | 14.4 [14.1–14.8] | 42.6 [40.1–50.0] | 27.8 [27.3–28.5] | 0.60x | 0.52x | 1.53x |
| sum | iterative | 16.9 [15.2–18.9] | 15.0 [14.7–15.2] | 41.9 [38.1–54.0] | 28.6 [27.8–30.2] | 0.59x | 0.52x | 1.47x |
| sumfp | iterative | 15.7 [14.5–17.0] | 13.7 [13.4–26.0] | 40.5 [37.8–45.6] | 28.4 [27.7–30.6] | 0.55x | 0.48x | 1.43x |
| nqueens | backtrack | 25.5 [23.2–27.6] | 23.3 [23.0–23.7] | 44.4 [40.3–48.6] | 29.3 [28.8–30.9] | 0.87x | 0.80x | 1.52x |
| fft | numeric | 21.7 [20.4–29.6] | 23.5 [22.8–24.9] | 44.7 [40.4–56.1] | 28.8 [27.0–31.1] | 0.75x | 0.82x | 1.55x |
| mbrot | numeric | 19.4 [17.5–21.4] | 17.1 [16.8–17.3] | 43.3 [40.0–55.4] | 28.4 [27.5–29.3] | 0.68x | 0.60x | 1.52x |
| ack | recursive | 29.8 [27.0–33.7] | 32.2 [29.6–37.0] | 58.2 [55.4–67.9] | 41.1 [38.1–44.6] | 0.73x | 0.78x | 1.42x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 14.3 [13.9–14.7] | 15.3 [14.2–16.0] | 43.1 [40.1–50.9] | 28.3 [27.8–29.0] | 0.50x | 0.54x | 1.52x |
| permute | micro | 17.5 [16.3–19.3] | 15.6 [15.1–16.0] | 43.4 [39.7–53.7] | 28.1 [27.5–28.8] | 0.62x | 0.56x | 1.54x |
| queens | micro | 22.7 [19.7–24.9] | 21.6 [20.8–22.9] | 44.9 [39.6–53.3] | 28.7 [27.7–29.2] | 0.79x | 0.75x | 1.57x |
| towers | micro | 19.4 [18.9–24.5] | 20.3 [19.1–34.4] | 43.8 [40.7–52.5] | 28.6 [27.6–29.1] | 0.68x | 0.71x | 1.53x |
| bounce | micro | 21.9 [21.3–26.9] | 29.4 [27.0–31.6] | 45.9 [42.6–55.2] | 28.8 [27.8–29.6] | 0.76x | 1.02x | 1.59x |
| list | micro | 17.7 [17.5–18.2] | 18.9 [18.3–21.5] | 45.0 [42.4–51.0] | 28.3 [27.8–28.9] | 0.62x | 0.67x | 1.59x |
| storage | micro | 16.7 [15.9–19.4] | 15.9 [15.5–17.6] | 43.5 [39.6–59.6] | 29.0 [28.3–30.0] | 0.57x | 0.55x | 1.50x |
| mandelbrot | compute | 46.4 [46.1–53.5] | 50.0 [46.2–56.0] | 65.3 [59.4–77.1] | 52.3 [50.9–53.1] | 0.89x | 0.96x | 1.25x |
| nbody | compute | 76.4 [73.3–82.3] | 57.3 [52.4–60.1] | 47.0 [41.2–59.6] | --- [28.9–30.0] | --- | --- | --- |
| richards | macro | 413.1 [386.2–435.7] | 433.4 [387.0–450.5] | 64.0 [61.3–71.9] | 68.8 [68.2–70.3] | 6.00x | 6.30x | 0.93x |
| json | macro | 47.5 [45.0–54.4] | 48.1 [47.2–59.3] | 52.3 [48.6–63.3] | 30.9 [30.5–31.5] | 1.53x | 1.56x | 1.69x |
| deltablue | macro | 229.7 [199.2–239.3] | 226.5 [197.0–235.2] | 49.8 [46.1–62.7] | 36.9 [35.2–37.8] | 6.23x | 6.14x | 1.35x |
| havlak | macro | 153.6 [134.1–169.2] | 160.6 [140.5–174.7] | 48.3 [46.1–51.7] | 116.0 [110.7–124.9] | 1.32x | 1.38x | 0.42x |
| cd | macro | 604.7 [571.7–642.7] | 523.0 [482.6–553.0] | 63.1 [58.4–77.9] | 58.9 [57.9–59.4] | 10.3x | 8.88x | 1.07x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 24.3 [23.9–28.1] | 25.9 [23.3–27.9] | 47.7 [46.9–58.7] | 29.8 [29.1–30.2] | 0.82x | 0.87x | 1.60x |
| fannkuch | permutation | 19.3 [19.1–19.7] | 25.0 [22.2–35.8] | 45.1 [43.3–61.1] | 29.1 [28.8–29.9] | 0.67x | 0.86x | 1.55x |
| fasta | generation | 21.2 [20.7–23.7] | 25.3 [24.0–27.1] | 44.2 [42.8–59.7] | 32.0 [28.8–35.4] | 0.66x | 0.79x | 1.38x |
| knucleotide | hashing | 30.3 [27.8–40.6] | 34.1 [32.0–36.2] | 39.7 [39.4–48.5] | 29.0 [27.7–34.6] | 1.04x | 1.17x | 1.37x |
| pidigits | bignum | 20.8 [19.2–22.7] | 20.2 [18.2–22.4] | 40.8 [39.7–52.2] | 27.5 [25.3–30.6] | 0.76x | 0.73x | 1.48x |
| regexredux | regex | 15.7 [14.7–16.4] | 15.4 [14.1–17.0] | 42.8 [40.8–52.6] | 28.1 [26.1–32.2] | 0.56x | 0.55x | 1.53x |
| revcomp | string | 22.7 [20.0–24.6] | 19.9 [19.5–27.3] | 41.8 [39.0–56.8] | 29.4 [28.6–30.0] | 0.77x | 0.68x | 1.42x |
| spectralnorm | numeric | 23.9 [22.6–26.0] | 23.0 [22.5–25.9] | 43.9 [40.5–54.5] | 29.5 [29.0–30.4] | 0.81x | 0.78x | 1.49x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 246.0 [216.1–259.5] | 340.6 [300.1–349.2] | 67.6 [62.4–75.5] | 61.6 [58.0–66.7] | 3.99x | 5.53x | 1.10x |
| matmul | numeric | 34.8 [34.4–42.3] | 59.9 [51.1–63.9] | 46.3 [43.8–58.5] | 42.3 [41.9–44.4] | 0.82x | 1.42x | 1.09x |
| primes | numeric | 32.5 [29.5–37.3] | 19.6 [18.6–21.0] | 45.0 [41.5–61.5] | 32.5 [31.9–33.1] | 1.00x | 0.60x | 1.38x |
| base64 | string | 29.9 [29.4–31.7] | 43.2 [40.4–55.1] | 44.8 [44.4–55.5] | 38.7 [38.4–43.3] | 0.77x | 1.12x | 1.16x |
| levenshtein | string | 35.9 [34.1–43.7] | 25.8 [25.1–34.4] | 42.9 [39.7–47.6] | 30.8 [30.3–32.7] | 1.17x | 0.84x | 1.39x |
| json_gen | data | 42.0 [41.2–44.4] | 52.0 [42.8–63.1] | 42.0 [39.2–51.0] | 31.8 [31.3–35.5] | 1.32x | 1.63x | 1.32x |
| collatz | numeric | 435.0 [386.0–444.1] | 432.3 [386.0–457.3] | 256.3 [238.2–271.6] | 1.31s [1.27s–1.35s] | 0.33x | 0.33x | 0.20x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 312.4 [276.8–323.4] | 224.2 [200.3–238.9] | 106.8 [100.3–115.6] | 94.5 [87.1–102.3] | 3.31x | 2.37x | 1.13x |
| array1 | array | 16.7 [15.9–25.2] | 16.8 [15.9–18.7] | 40.7 [38.9–50.1] | 26.5 [25.0–29.4] | 0.63x | 0.63x | 1.53x |
| deriv | symbolic | 45.7 [44.7–65.8] | 35.0 [33.3–37.5] | 48.5 [43.0–52.3] | 29.1 [26.6–34.2] | 1.57x | 1.20x | 1.67x |
| diviter | iterative | 563.8 [517.4–599.7] | 570.4 [518.3–593.2] | 318.0 [291.2–331.9] | 442.0 [401.9–470.6] | 1.28x | 1.29x | 0.72x |
| divrec | recursive | 24.2 [22.9–26.9] | 17.6 [17.2–18.4] | 49.5 [45.5–54.2] | 36.3 [35.7–36.7] | 0.67x | 0.49x | 1.36x |
| gcbench | allocation | 210.7 [186.4–217.8] | 210.9 [194.7–229.9] | 114.7 [105.2–125.1] | 44.9 [44.0–54.4] | 4.69x | 4.69x | 2.55x |
| paraffins | combinat | 44.4 [41.8–54.4] | 40.1 [38.3–47.8] | 46.5 [43.9–56.3] | 28.2 [27.7–29.2] | 1.58x | 1.42x | 1.65x |
| pnpoly | numeric | 28.5 [28.1–40.3] | 36.4 [34.0–39.3] | 46.2 [42.1–56.8] | 28.8 [28.5–29.8] | 0.99x | 1.26x | 1.60x |
| puzzle | search | 21.4 [19.7–22.5] | 23.0 [20.7–30.1] | 40.3 [39.2–49.3] | 28.1 [26.2–31.9] | 0.76x | 0.82x | 1.43x |
| quicksort | sorting | 21.9 [20.2–23.8] | 19.8 [19.2–24.8] | 40.6 [39.1–55.5] | 29.0 [27.9–32.0] | 0.76x | 0.68x | 1.40x |
| ray | numeric | 27.2 [26.8–29.6] | 17.6 [17.3–18.2] | 40.2 [38.7–48.0] | 29.3 [29.0–30.0] | 0.93x | 0.60x | 1.37x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 149.4 [131.5–167.8] | 217.1 [187.8–284.3] | 49.0 [45.5–60.4] | 41.9 [40.8–153.8] | 3.56x | 5.18x | 1.17x |
| navier_stokes | numeric | 232.9 [203.5–244.1] | 230.8 [204.2–318.7] | 79.8 [73.5–84.9] | 36.8 [36.0–39.7] | 6.34x | 6.28x | 2.17x |
| splay | data | 340.3 [308.3–366.8] | 352.3 [318.9–374.2] | 61.0 [58.1–69.5] | 65.8 [63.8–70.5] | 5.17x | 5.35x | 0.93x |
| hashmap | data | 143.6 [127.7–165.9] | 93.2 [80.6–109.9] | 45.8 [42.0–54.9] | 41.0 [40.2–42.8] | 3.50x | 2.27x | 1.12x |
| crypto_sha1 | crypto | 79.5 [70.2–82.8] | 573.3 [519.4–658.5] | 45.3 [43.4–54.2] | 36.0 [33.9–144.8] | 2.21x | 15.9x | 1.26x |
| raytrace3d | 3d | 221.2 [200.2–243.1] | 173.7 [163.4–194.8] | 50.9 [47.9–55.5] | 44.8 [42.3–51.4] | 4.94x | 3.88x | 1.14x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 272.9 [247.9–285.2] | 172.8 [154.8–187.1] | 52.2 [50.8–62.9] | 61.2 [59.8–66.4] | 4.46x | 2.82x | 0.85x |
| microdiff | data-diff | 18.7 [17.7–20.2] | 16.3 [15.9–16.7] | 40.8 [38.6–47.4] | 45.4 [44.2–46.0] | 0.41x | 0.36x | 0.90x |
| hyphen | hyphenation | 27.5 [24.3–30.6] | 23.3 [22.2–24.6] | 46.2 [42.6–57.8] | 37.6 [36.5–38.5] | 0.73x | 0.62x | 1.23x |
| prettier_ast | formatting | 979.9 [937.8–1.04s] | 992.1 [943.6–1.03s] | 102.3 [91.1–111.0] | 118.1 [106.0–120.9] | 8.30x | 8.40x | 0.87x |
| text_search | search | 14.83s [14.59s–15.22s] | 5.21s [5.01s–5.55s] | 597.6 [541.6–628.2] | 784.7 [727.6–829.1] | 18.9x | 6.63x | 0.76x |
| three_way_merge | merge | 3.46s [3.41s–3.62s] | 3.16s [3.09s–3.24s] | 1.48s [1.45s–1.53s] | 1.03s [982.3–1.06s] | 3.37x | 3.08x | 1.44x |
| log_pipeline | log-processing | 5.99s [5.94s–6.17s] | 5.63s [5.55s–6.08s] | 600.8 [543.4–620.2] | 1.10s [1.02s–1.20s] | 5.47x | 5.14x | 0.55x |

