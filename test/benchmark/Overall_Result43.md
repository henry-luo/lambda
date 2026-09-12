# Lambda Benchmark Results: Result43

- **Date:** 2026-09-13
- **Platform:** Darwin arm64
- **Lambda commit:** `fabc41214615b7dc45f56c87132d59d00d4f24de`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v43-fabc412146` (19,417,608 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,257 / 40,261 passed in 103.40s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 103.3s (batched 101.5s: sync 79.8s, async 21.7s; non-batched 1.8s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v43.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.32x | 0.29x | 0.17x | 6.97x | 5.81x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.49x | 0.82x | 0.09x | 20.4x | 5.02x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.38x | 0.33x | 0.10x | 9.46x | 1.68x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 1.78x | 1.04x | 0.23x | 42.2x | 11.9x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 1.53x | 0.80x | 0.33x | 21.7x | 13.4x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 5.48x | 3.31x | 0.29x | 34.7x | 12.0x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 7.42x | 5.00x | 0.47x | 51.0x | 12.9x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 63 | 1.36x | 0.88x | 0.19x | 19.9x | 7.04x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 59 | 0.79x | 1.20x | 4.55x | 59 |
| complete current suite | 63 | 0.88x | 1.36x | 4.64x | 63 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 4.64x over 63 of 63 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| text/hyphen | 73.1 | 1.48 | 49.4x |
| awfy/deltablue | 49.3 | 1.15 | 42.8x |
| awfy/cd | 537.3 | 15.1 | 35.5x |
| awfy/havlak | 62.3 | 1.83 | 34.1x |
| jetstream/cube3d | 14.4 | 0.515 | 28.0x |
| awfy/richards | 721.6 | 29.0 | 24.9x |
| text/prettier_ast | 1.01s | 42.1 | 23.9x |
| text/microdiff | 57.8 | 2.68 | 21.6x |
| jetstream/splay | 387.5 | 18.6 | 20.8x |
| beng/knucleotide | 5.27 | 0.286 | 18.4x |
| kostya/base64 | 9.72 | 0.556 | 17.5x |
| jetstream/hashmap | 40.9 | 2.71 | 15.1x |

---

### Notable Results

- Missing timings: **0** cells

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| kostya/primes | 1.81s | 4.42 | 410x |
| awfy/havlak | 18.32s | 96.0 | 191x |
| text/text_search | 117.85s | 782.5 | 151x |
| larceny/triangl | 7.62s | 66.6 | 114x |
| awfy/cd | 4.01s | 36.8 | 109x |
| jetstream/hashmap | 1.58s | 15.2 | 104x |
| awfy/nbody | 569.1 | 5.72 | 99.4x |
| beng/spectralnorm | 243.0 | 2.55 | 95.3x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.474 | 1.92 | 0.25x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.32 [1.32–1.39] | 1.45 [1.44–1.53] | 1.27 [1.15–1.28] | 18.3 [17.7–20.5] | 24.0 [23.8–28.3] | 2.49 [2.30–11.6] | 0.53x | 0.58x | 0.51x | 7.35x | 9.67x |
| fibfp | recursive | 2.92 [2.89–4.84] | 1.62 [1.60–1.71] | 1.45 [1.40–1.54] | 19.2 [18.6–19.5] | 23.0 [22.6–25.9] | 2.29 [2.22–2.82] | 1.28x | 0.71x | 0.63x | 8.39x | 10.1x |
| tak | recursive | 0.144 [0.143–0.145] | 0.145 [0.145–0.211] | 0.146 [0.134–0.152] | 1.61 [1.56–1.86] | 3.45 [3.33–3.46] | 0.966 [0.948–1.10] | 0.15x | 0.15x | 0.15x | 1.67x | 3.58x |
| cpstak | closure | 0.285 [0.284–0.292] | 0.312 [0.281–0.384] | 0.262 [0.255–0.286] | 3.26 [3.26–3.27] | 6.80 [6.63–6.81] | 1.28 [1.26–1.43] | 0.22x | 0.24x | 0.20x | 2.55x | 5.31x |
| sum | iterative | 0.319 [0.305–0.319] | 0.319 [0.305–0.320] | 0.368 [0.326–0.384] | 32.8 [31.5–35.4] | 43.9 [41.2–44.7] | 2.56 [1.76–2.62] | 0.12x | 0.12x | 0.14x | 12.8x | 17.1x |
| sumfp | iterative | 0.093 [0.092–0.098] | 0.093 [0.092–0.093] | 0.106 [0.105–0.108] | 3.45 [3.45–3.59] | 4.99 [4.97–5.04] | 1.43 [1.40–2.07] | 0.07x | 0.07x | 0.07x | 2.41x | 3.49x |
| nqueens | backtrack | 2.88 [2.61–2.96] | 2.57 [2.55–2.98] | 0.177 [0.173–0.225] | 97.4 [93.2–101.0] | 10.8 [10.8–10.9] | 2.79 [2.72–3.51] | 1.03x | 0.92x | 0.06x | 34.9x | 3.86x |
| fft | numeric | 0.411 [0.411–0.457] | 0.240 [0.223–0.334] | 0.038 [0.035–0.039] | 39.0 [39.0–39.6] | 3.67 [3.59–3.69] | 2.38 [2.19–2.72] | 0.17x | 0.10x | 0.02x | 16.4x | 1.54x |
| mbrot | numeric | 0.972 [0.959–1.07] | 1.37 [1.23–1.44] | 0.604 [0.600–0.681] | 27.7 [24.4–29.6] | 24.3 [23.5–25.5] | 2.93 [2.70–3.10] | 0.33x | 0.47x | 0.21x | 9.46x | 8.29x |
| ack | recursive | 18.5 [18.0–20.3] | 14.2 [13.6–14.5] | 16.5 [15.8–17.3] | 114.0 [112.3–119.1] | 149.5 [146.9–165.1] | 18.5 [17.9–20.2] | 1.00x | 0.77x | 0.89x | 6.15x | 8.07x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.041 [0.041–0.042] | 0.045 [0.045–0.045] | 0.024 [0.022–0.025] | 4.84 [4.57–5.97] | 0.829 [0.824–0.890] | 0.550 [0.509–0.559] | 0.07x | 0.08x | 0.04x | 8.79x | 1.51x |
| permute | micro | 0.816 [0.784–0.895] | 0.135 [0.125–0.546] | 0.038 [0.036–0.043] | 11.8 [11.1–12.2] | 2.25 [2.24–3.11] | 1.19 [1.11–1.27] | 0.69x | 0.11x | 0.03x | 9.91x | 1.89x |
| queens | micro | 0.563 [0.559–0.612] | 0.304 [0.303–0.322] | 0.029 [0.027–0.031] | 6.08 [6.07–6.74] | 1.42 [1.40–1.50] | 0.946 [0.930–0.966] | 0.60x | 0.32x | 0.03x | 6.43x | 1.50x |
| towers | micro | 1.69 [1.67–1.70] | 0.458 [0.428–0.598] | 0.041 [0.040–0.042] | 23.3 [21.7–24.6] | 3.08 [3.06–3.13] | 1.94 [1.68–2.00] | 0.87x | 0.24x | 0.02x | 12.0x | 1.59x |
| bounce | micro | 0.101 [0.100–0.166] | 0.090 [0.090–0.090] | 0.047 [0.039–0.064] | 5.78 [5.51–6.45] | 1.19 [1.17–2.15] | 0.767 [0.759–0.778] | 0.13x | 0.12x | 0.06x | 7.54x | 1.55x |
| list | micro | 0.974 [0.970–0.995] | 0.306 [0.282–0.310] | 0.032 [0.031–0.035] | 2.88 [2.81–7.00] | 1.25 [1.23–1.29] | 0.739 [0.663–1.04] | 1.32x | 0.41x | 0.04x | 3.90x | 1.69x |
| storage | micro | 0.983 [0.933–1.50] | 0.496 [0.484–0.538] | 0.126 [0.117–0.135] | 8.43 [8.08–9.06] | 2.76 [2.72–3.71] | 0.771 [0.756–0.805] | 1.27x | 0.64x | 0.16x | 10.9x | 3.57x |
| mandelbrot | compute | 46.9 [44.2–47.2] | 42.7 [42.6–43.0] | 30.8 [30.8–30.8] | 962.2 [957.5–962.8] | 871.8 [869.4–878.9] | 31.3 [31.2–31.7] | 1.50x | 1.36x | 0.98x | 30.7x | 27.8x |
| nbody | compute | 52.1 [52.0–52.3] | 16.6 [16.6–16.7] | 1.50 [1.50–1.50] | 569.1 [565.6–575.8] | 159.1 [159.1–160.0] | 5.72 [5.38–5.73] | 9.11x | 2.90x | 0.26x | 99.4x | 27.8x |
| richards | macro | 434.1 [431.7–458.8] | 721.6 [719.6–722.7] | 29.0 [28.8–29.6] | 1.11s [1.08s–1.12s] | 190.7 [190.4–192.4] | 47.1 [46.3–47.2] | 9.22x | 15.3x | 0.62x | 23.5x | 4.05x |
| json | macro | 6.10 [6.04–6.11] | 3.09 [3.05–3.20] | 0.259 [0.259–0.262] | 41.7 [41.6–41.9] | 10.7 [10.7–11.0] | 2.66 [2.60–2.67] | 2.29x | 1.16x | 0.10x | 15.7x | 4.03x |
| deltablue | macro | 135.9 [133.6–144.4] | 49.3 [49.1–50.1] | 1.15 [1.14–1.16] | 497.6 [494.1–497.8] | 100.0 [99.5–100.0] | 11.6 [11.4–11.7] | 11.7x | 4.24x | 0.10x | 42.8x | 8.60x |
| havlak | macro | 72.0 [69.4–73.2] | 62.3 [60.3–62.7] | 1.83 [1.82–1.99] | 18.32s [18.18s–18.44s] | 3.32s [3.29s–3.33s] | 96.0 [95.1–104.1] | 0.75x | 0.65x | 0.02x | 191x | 34.6x |
| cd | macro | 632.3 [629.1–633.8] | 537.3 [536.6–563.7] | 15.1 [14.9–15.2] | 4.01s [4.00s–4.03s] | 981.8 [974.3–982.6] | 36.8 [36.6–38.0] | 17.2x | 14.6x | 0.41x | 109x | 26.7x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.21 [9.10–9.36] | 2.18 [2.16–2.20] | 3.01 [2.96–3.03] | 32.3 [32.1–32.4] | 23.7 [23.5–23.8] | 4.12 [4.03–5.09] | 2.24x | 0.53x | 0.73x | 7.85x | 5.76x |
| fannkuch | permutation | 0.350 [0.348–0.352] | 0.359 [0.342–0.377] | 0.148 [0.148–0.150] | 41.9 [41.7–42.1] | 7.19 [7.18–7.25] | 4.12 [4.02–11.1] | 0.08x | 0.09x | 0.04x | 10.2x | 1.74x |
| fasta | generation | 0.769 [0.764–0.778] | 0.893 [0.885–0.908] | 0.239 [0.239–0.265] | 76.4 [75.4–76.5] | 8.85 [8.45–8.95] | 6.39 [6.08–6.58] | 0.12x | 0.14x | 0.04x | 12.0x | 1.39x |
| knucleotide | hashing | 4.49 [4.43–4.82] | 5.27 [5.27–5.29] | 0.286 [0.286–0.289] | 164.2 [163.7–164.8] | 7.69 [7.65–7.80] | 4.97 [4.85–6.00] | 0.90x | 1.06x | 0.06x | 33.0x | 1.55x |
| pidigits | bignum | 0.303 [0.301–0.361] | 0.299 [0.290–0.314] | 0.046 [0.045–0.046] | 0.474 [0.457–0.484] | 0.128 [0.126–0.132] | 1.92 [1.90–2.01] | 0.16x | 0.16x | 0.02x | 0.25x | 0.07x |
| regexredux | regex | 1.27 [1.26–1.29] | 1.28 [1.27–1.33] | 1.14 [1.13–1.16] | 26.0 [26.0–26.2] | 5.58 [5.53–5.69] | 2.42 [2.39–2.58] | 0.53x | 0.53x | 0.47x | 10.8x | 2.31x |
| revcomp | string | 1.24 [1.24–1.25] | 1.20 [1.18–1.24] | 0.382 [0.377–0.408] | 27.0 [26.9–27.0] | 2.55 [2.54–2.56] | 3.35 [3.32–3.39] | 0.37x | 0.36x | 0.11x | 8.06x | 0.76x |
| spectralnorm | numeric | 1.63 [1.62–1.69] | 1.69 [1.65–1.75] | 0.353 [0.351–0.354] | 243.0 [241.6–243.1] | 63.9 [63.7–64.4] | 2.55 [2.53–2.60] | 0.64x | 0.66x | 0.14x | 95.3x | 25.1x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 221.7 [221.5–222.0] | 193.1 [192.9–194.6] | 28.5 [28.4–28.7] | 3.10s [3.08s–3.14s] | 887.2 [886.2–887.9] | 33.3 [33.3–33.3] | 6.67x | 5.80x | 0.86x | 93.1x | 26.7x |
| matmul | numeric | 17.7 [17.7–17.7] | 17.7 [17.7–17.8] | 6.03 [6.01–6.04] | 1.39s [1.38s–1.41s] | 540.2 [540.2–551.1] | 15.5 [15.4–16.2] | 1.15x | 1.15x | 0.39x | 89.6x | 35.0x |
| primes | numeric | 14.6 [14.6–15.0] | 2.49 [2.45–2.51] | 1.61 [1.61–1.61] | 1.81s [1.80s–1.83s] | 94.6 [94.4–94.9] | 4.42 [4.40–4.53] | 3.31x | 0.56x | 0.37x | 410x | 21.4x |
| base64 | string | 16.5 [16.4–16.6] | 9.72 [9.64–9.80] | 0.556 [0.555–0.556] | 558.9 [544.5–576.9] | 158.6 [157.6–158.9] | 17.3 [17.3–18.3] | 0.95x | 0.56x | 0.03x | 32.2x | 9.15x |
| levenshtein | string | 9.97 [9.86–10.2] | 6.26 [6.12–6.31] | 0.910 [0.899–0.930] | 260.8 [260.7–260.9] | 54.3 [54.2–54.4] | 4.01 [3.92–4.05] | 2.49x | 1.56x | 0.23x | 65.1x | 13.5x |
| json_gen | data | 27.2 [26.7–27.5] | 11.8 [11.5–11.9] | 1.55 [1.51–1.55] | 42.8 [41.2–45.5] | 20.0 [20.0–20.2] | 6.49 [6.29–6.52] | 4.20x | 1.81x | 0.24x | 6.59x | 3.08x |
| collatz | numeric | 327.4 [325.7–330.1] | 324.4 [323.7–325.5] | 225.4 [225.2–225.5] | 7.31s [7.30s–7.37s] | 6.26s [6.23s–6.27s] | 1.45s [1.44s–1.49s] | 0.23x | 0.22x | 0.16x | 5.04x | 4.31x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 265.2 [264.9–265.8] | 187.2 [187.1–187.5] | 60.5 [60.3–85.5] | 7.62s [7.59s–8.04s] | 2.19s [2.18s–2.20s] | 66.6 [66.4–66.9] | 3.98x | 2.81x | 0.91x | 114x | 32.9x |
| array1 | array | 0.808 [0.808–0.808] | 0.810 [0.808–0.812] | 0.319 [0.319–0.322] | 85.7 [85.2–85.8] | 35.8 [35.7–35.9] | 1.90 [1.90–1.91] | 0.42x | 0.43x | 0.17x | 45.0x | 18.8x |
| deriv | symbolic | 30.2 [29.9–30.4] | 7.22 [7.06–7.54] | 2.85 [2.85–2.88] | 77.6 [77.4–77.7] | 59.0 [58.7–59.6] | 3.68 [3.67–3.71] | 8.21x | 1.96x | 0.77x | 21.1x | 16.0x |
| diviter | iterative | 266.2 [266.2–285.5] | 266.4 [266.2–266.5] | 266.2 [265.9–266.8] | 14.39s [14.38s–14.42s] | 26.89s [26.82s–26.98s] | 473.2 [472.8–474.6] | 0.56x | 0.56x | 0.56x | 30.4x | 56.8x |
| divrec | recursive | 5.53 [5.51–5.57] | 1.20 [1.20–1.21] | 4.85 [4.84–4.85] | 23.1 [22.9–23.2] | 36.2 [36.0–36.3] | 7.63 [7.54–7.87] | 0.73x | 0.16x | 0.64x | 3.03x | 4.75x |
| gcbench | allocation | 218.9 [215.9–219.8] | 84.7 [84.4–86.4] | 70.4 [70.3–70.7] | 788.9 [786.8–793.2] | 548.2 [547.8–573.9] | 23.7 [23.7–23.8] | 9.22x | 3.57x | 2.97x | 33.2x | 23.1x |
| paraffins | combinat | 0.263 [0.261–0.265] | 0.209 [0.208–0.210] | 0.048 [0.047–0.060] | 7.06 [7.05–7.07] | 2.50 [2.50–2.52] | 0.992 [0.990–0.997] | 0.27x | 0.21x | 0.05x | 7.11x | 2.52x |
| pnpoly | numeric | 12.7 [12.5–12.8] | 12.7 [12.7–12.7] | 1.95 [1.93–1.96] | 126.9 [125.9–127.3] | 202.5 [202.0–203.9] | 5.86 [5.84–6.00] | 2.16x | 2.16x | 0.33x | 21.6x | 34.5x |
| puzzle | search | 18.9 [18.6–19.0] | 16.1 [16.1–17.0] | 1.29 [1.26–1.30] | 58.7 [58.5–59.5] | 29.4 [29.2–29.6] | 3.31 [3.30–4.52] | 5.70x | 4.88x | 0.39x | 17.8x | 8.88x |
| quicksort | sorting | 12.1 [11.9–12.2] | 0.954 [0.938–0.963] | 0.205 [0.196–0.208] | 156.5 [151.1–181.3] | 19.5 [19.3–19.6] | 1.65 [1.64–1.66] | 7.31x | 0.58x | 0.12x | 94.8x | 11.8x |
| ray | numeric | 0.300 [0.297–0.321] | 0.302 [0.301–0.304] | 0.173 [0.172–0.174] | 20.9 [20.7–21.1] | 15.1 [14.0–17.2] | 3.52 [3.47–3.61] | 0.09x | 0.09x | 0.05x | 5.94x | 4.30x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 15.6 [15.6–15.7] | 14.4 [14.4–14.5] | 0.515 [0.512–0.516] | 382.1 [381.7–385.1] | 216.0 [215.9–217.7] | 17.6 [17.4–17.7] | 0.89x | 0.82x | 0.03x | 21.7x | 12.3x |
| navier_stokes | numeric | 124.3 [124.1–124.6] | 122.9 [122.9–123.4] | 46.9 [46.9–47.1] | 368.1 [368.0–368.6] | 98.4 [98.2–98.5] | 14.0 [13.9–14.1] | 8.87x | 8.78x | 3.35x | 26.3x | 7.03x |
| splay | data | 312.2 [310.9–314.1] | 387.5 [387.2–388.5] | 18.6 [18.6–18.9] | 406.8 [331.3–408.5] | 145.6 [144.1–170.9] | 19.6 [19.4–19.9] | 15.9x | 19.8x | 0.95x | 20.7x | 7.43x |
| hashmap | data | 125.3 [125.3–126.1] | 40.9 [40.2–41.1] | 2.71 [2.71–2.72] | 1.58s [1.57s–1.59s] | 314.8 [313.4–315.6] | 15.2 [15.1–15.5] | 8.24x | 2.68x | 0.18x | 104x | 20.7x |
| crypto_sha1 | crypto | 57.1 [56.3–57.2] | 26.7 [25.8–34.7] | 2.62 [2.61–2.68] | 380.8 [379.7–381.0] | 218.3 [218.2–218.3] | 8.68 [8.66–8.69] | 6.58x | 3.08x | 0.30x | 43.9x | 25.1x |
| raytrace3d | 3d | 72.8 [71.9–92.0] | 20.5 [20.5–20.6] | 2.19 [2.16–2.19] | 590.0 [588.5–611.0] | 163.1 [162.5–163.5] | 18.3 [18.2–18.3] | 3.99x | 1.13x | 0.12x | 32.3x | 8.93x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 245.0 [241.1–246.1] | 141.4 [141.2–143.3] | 13.1 [13.0–13.5] | 2.66s [2.61s–2.74s] | 613.6 [609.9–619.4] | 41.0 [39.3–126.9] | 5.98x | 3.45x | 0.32x | 65.1x | 15.0x |
| microdiff | data-diff | 54.4 [54.3–55.0] | 57.8 [57.8–58.2] | 2.68 [2.65–2.73] | 1.09s [1.08s–1.09s] | 108.5 [108.5–109.4] | 16.1 [16.0–16.9] | 3.38x | 3.59x | 0.17x | 67.5x | 6.74x |
| hyphen | hyphenation | 73.3 [72.8–74.3] | 73.1 [71.8–74.3] | 1.48 [1.48–1.53] | 534.8 [531.8–535.2] | 80.2 [79.3–80.9] | 6.75 [6.54–6.84] | 10.9x | 10.8x | 0.22x | 79.2x | 11.9x |
| prettier_ast | formatting | 1.12s [1.11s–1.12s] | 1.01s [987.3–1.01s] | 42.1 [41.7–42.5] | 6.54s [5.85s–6.76s] | 1.63s [1.62s–1.63s] | 105.4 [103.3–106.3] | 10.6x | 9.55x | 0.40x | 62.0x | 15.4x |
| text_search | search | 15.13s [15.09s–15.21s] | 1.83s [1.83s–1.84s] | 538.0 [537.8–550.0] | 117.85s [117.69s–118.02s] | 38.91s [38.81s–38.96s] | 782.5 [781.2–786.8] | 19.3x | 2.34x | 0.69x | 151x | 49.7x |
| three_way_merge | merge | 4.01s [4.00s–4.02s] | 3.71s [3.70s–3.72s] | 2.15s [2.13s–2.15s] | 14.72s [14.68s–14.85s] | 6.16s [6.14s–6.16s] | 968.9 [967.4–969.8] | 4.13x | 3.83x | 2.22x | 15.2x | 6.35x |
| log_pipeline | log-processing | 6.27s [6.11s–6.27s] | 6.42s [6.42s–6.43s] | 624.3 [622.1–662.7] | 17.24s [17.11s–17.25s] | 9.55s [9.43s–9.60s] | 943.0 [942.9–947.9] | 6.65x | 6.81x | 0.66x | 18.3x | 10.1x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.45x | 0.44x | 0.98x | 1.06x | 0.41x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.06x | 1.08x | 0.93x | 4.85x | 0.89x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.50x | 0.53x | 1.03x | 1.94x | 0.33x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 0.79x | 0.66x | 0.73x | 10.9x | 2.90x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 0.84x | 0.67x | 1.01x | 4.07x | 1.94x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 3.07x | 4.20x | 0.91x | 11.2x | 3.67x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 4.44x | 3.03x | 0.92x | 31.5x | 6.83x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 63 | 1.02x | 0.95x | 0.93x | 4.80x | 1.30x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 22.0 [21.5–22.8] | 27.4 [25.1–27.4] | 61.6 [60.3–500.8] | 60.3 [57.3–68.8] | 36.7 [33.8–37.7] | 70.5 [65.8–122.6] | 0.31x | 0.39x | 0.87x | 0.86x | 0.52x |
| fibfp | recursive | 32.9 [31.0–34.5] | 26.6 [25.9–28.4] | 65.8 [63.1–67.4] | 56.0 [55.0–57.8] | 33.0 [32.9–34.9] | 63.2 [62.9–63.2] | 0.52x | 0.42x | 1.04x | 0.89x | 0.52x |
| tak | recursive | 25.2 [24.6–26.1] | 25.5 [24.0–25.9] | 64.3 [60.5–65.1] | 40.0 [39.4–41.6] | 13.2 [11.7–14.3] | 61.2 [58.7–62.7] | 0.41x | 0.42x | 1.05x | 0.65x | 0.22x |
| cpstak | closure | 24.4 [23.8–27.3] | 25.7 [22.5–26.5] | 62.1 [59.6–63.1] | 43.8 [41.9–44.6] | 16.2 [15.3–17.0] | 62.4 [59.2–62.4] | 0.39x | 0.41x | 1.00x | 0.70x | 0.26x |
| sum | iterative | 24.3 [22.4–25.8] | 24.4 [23.7–26.7] | 62.1 [60.8–65.6] | 75.5 [70.5–79.1] | 55.1 [53.2–55.2] | 70.7 [69.8–73.0] | 0.34x | 0.34x | 0.88x | 1.07x | 0.78x |
| sumfp | iterative | 26.4 [25.9–28.1] | 27.3 [26.0–28.5] | 71.2 [66.9–72.8] | 48.4 [44.4–50.1] | 16.3 [15.4–16.9] | 70.7 [69.5–72.7] | 0.37x | 0.39x | 1.01x | 0.68x | 0.23x |
| nqueens | backtrack | 46.6 [42.8–47.2] | 47.6 [46.3–51.2] | 72.5 [67.6–73.3] | 209.3 [205.8–258.6] | 21.4 [20.8–23.0] | 73.2 [69.0–73.8] | 0.64x | 0.65x | 0.99x | 2.86x | 0.29x |
| fft | numeric | 46.0 [40.6–51.5] | 38.6 [38.4–40.2] | 73.0 [72.8–74.0] | 93.7 [93.0–95.8] | 15.2 [14.5–15.3] | 72.2 [66.8–76.0] | 0.64x | 0.53x | 1.01x | 1.30x | 0.21x |
| mbrot | numeric | 34.0 [32.2–35.1] | 34.9 [34.8–35.9] | 71.4 [67.2–74.5] | 75.0 [74.7–76.2] | 33.6 [33.1–36.1] | 71.9 [70.1–72.1] | 0.47x | 0.49x | 0.99x | 1.04x | 0.47x |
| ack | recursive | 49.0 [40.6–50.4] | 41.0 [40.7–43.4] | 87.5 [83.5–87.6] | 155.2 [154.5–162.6] | 161.6 [157.9–174.9] | 88.0 [85.3–91.7] | 0.56x | 0.47x | 0.99x | 1.76x | 1.84x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 32.6 [28.1–33.4] | 29.4 [28.4–35.4] | 71.5 [70.6–73.0] | 56.6 [53.4–58.5] | 11.3 [10.5–12.2] | 69.7 [67.2–75.2] | 0.47x | 0.42x | 1.03x | 0.81x | 0.16x |
| permute | micro | 29.1 [28.8–33.7] | 33.2 [30.9–36.0] | 78.5 [74.5–80.1] | 69.2 [67.7–69.3] | 27.5 [16.5–54.6] | 75.6 [74.3–77.2] | 0.38x | 0.44x | 1.04x | 0.92x | 0.36x |
| queens | micro | 38.5 [36.3–38.9] | 54.1 [40.9–64.5] | 76.9 [73.4–77.2] | 67.9 [67.6–68.4] | 12.5 [11.1–15.2] | 73.3 [69.1–75.5] | 0.53x | 0.74x | 1.05x | 0.93x | 0.17x |
| towers | micro | 39.6 [38.6–42.5] | 41.2 [35.6–41.2] | 73.1 [69.0–73.2] | 84.5 [81.4–90.7] | 14.2 [13.2–15.3] | 71.9 [69.3–74.5] | 0.55x | 0.57x | 1.02x | 1.17x | 0.20x |
| bounce | micro | 41.3 [41.3–46.7] | 44.3 [42.4–45.8] | 84.5 [75.2–93.6] | 190.1 [189.7–193.1] | 11.8 [11.7–15.3] | 71.1 [70.7–72.6] | 0.58x | 0.62x | 1.19x | 2.67x | 0.17x |
| list | micro | 33.5 [32.4–38.5] | 36.7 [35.7–40.1] | 72.6 [72.0–74.9] | 59.0 [56.5–60.3] | 12.3 [11.2–13.3] | 69.0 [68.6–98.3] | 0.49x | 0.53x | 1.05x | 0.86x | 0.18x |
| storage | micro | 33.1 [28.3–33.9] | 31.8 [31.1–32.5] | 74.4 [68.7–75.4] | 177.0 [172.3–215.9] | 14.0 [13.0–14.8] | 63.2 [57.3–63.7] | 0.52x | 0.50x | 1.18x | 2.80x | 0.22x |
| mandelbrot | compute | 66.0 [65.8–66.6] | 61.4 [61.3–62.0] | 77.1 [76.5–77.6] | 1.00s [995.9–1.00s] | 878.7 [876.6–885.9] | 76.4 [76.2–77.1] | 0.86x | 0.80x | 1.01x | 13.1x | 11.5x |
| nbody | compute | 82.9 [82.5–83.2] | 63.8 [63.8–63.9] | 48.2 [48.0–48.3] | 634.4 [631.5–640.3] | 166.0 [165.9–166.9] | 57.9 [51.0–82.3] | 1.43x | 1.10x | 0.83x | 11.0x | 2.87x |
| richards | macro | 483.4 [480.2–484.2] | 785.8 [785.1–796.8] | 77.3 [77.1–77.7] | 1.21s [1.18s–1.22s] | 197.8 [197.4–199.6] | 92.3 [92.1–94.0] | 5.24x | 8.52x | 0.84x | 13.1x | 2.14x |
| json | macro | 52.5 [52.4–54.4] | 50.6 [50.2–51.4] | 52.9 [52.8–53.8] | 230.7 [230.3–232.9] | 18.5 [18.2–19.2] | 48.4 [48.0–48.6] | 1.08x | 1.04x | 1.09x | 4.76x | 0.38x |
| deltablue | macro | 253.9 [251.1–283.3] | 197.6 [197.3–198.1] | 53.9 [53.8–54.4] | 727.7 [722.5–730.3] | 107.9 [107.6–108.1] | 57.8 [57.0–57.9] | 4.39x | 3.42x | 0.93x | 12.6x | 1.87x |
| havlak | macro | 168.7 [168.0–171.4] | 151.7 [150.9–152.3] | 54.7 [54.4–85.0] | 18.69s [18.54s–18.82s] | 3.34s [3.30s–3.34s] | 142.6 [142.6–227.9] | 1.18x | 1.06x | 0.38x | 131x | 23.4x |
| cd | macro | 712.9 [708.0–734.6] | 614.9 [613.1–632.9] | 67.8 [67.5–74.7] | 4.32s [4.31s–4.34s] | 989.9 [982.3–990.8] | 83.4 [82.7–84.8] | 8.55x | 7.37x | 0.81x | 51.8x | 11.9x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 27.4 [27.2–28.2] | 22.5 [22.5–22.5] | 48.4 [48.1–48.7] | 62.9 [62.8–63.8] | 29.2 [29.2–29.9] | 45.9 [45.0–46.4] | 0.60x | 0.49x | 1.05x | 1.37x | 0.64x |
| fannkuch | permutation | 21.4 [21.2–21.7] | 24.7 [24.6–26.5] | 45.5 [45.3–45.5] | 73.9 [73.8–74.0] | 12.7 [12.4–13.2] | 45.4 [44.5–73.2] | 0.47x | 0.54x | 1.00x | 1.63x | 0.28x |
| fasta | generation | 24.9 [24.7–25.0] | 27.4 [27.0–27.6] | 45.6 [45.1–45.8] | 113.2 [111.9–114.3] | 14.6 [14.5–14.6] | 48.3 [47.8–48.3] | 0.52x | 0.57x | 0.95x | 2.34x | 0.30x |
| knucleotide | hashing | 30.9 [30.3–32.1] | 37.7 [37.5–37.7] | 46.2 [46.0–46.3] | 198.0 [197.4–200.8] | 13.6 [13.5–14.2] | 46.8 [46.8–47.4] | 0.66x | 0.80x | 0.99x | 4.23x | 0.29x |
| pidigits | bignum | 19.6 [19.5–19.9] | 20.9 [20.1–20.9] | 47.6 [47.5–47.6] | 34.3 [34.0–35.2] | 5.42 [5.40–5.93] | 43.2 [41.9–43.8] | 0.45x | 0.48x | 1.10x | 0.79x | 0.13x |
| regexredux | regex | 14.9 [14.9–15.0] | 15.1 [14.9–15.2] | 48.1 [47.8–48.3] | 60.2 [59.5–60.6] | 11.3 [11.3–11.8] | 43.6 [43.1–44.2] | 0.34x | 0.35x | 1.10x | 1.38x | 0.26x |
| revcomp | string | 22.0 [21.9–22.4] | 22.6 [22.5–22.6] | 46.3 [45.8–47.3] | 61.9 [61.8–63.0] | 8.28 [7.91–8.57] | 44.7 [44.5–44.7] | 0.49x | 0.51x | 1.04x | 1.39x | 0.19x |
| spectralnorm | numeric | 24.8 [23.8–24.9] | 26.5 [26.4–26.6] | 45.7 [45.3–45.7] | 278.0 [277.1–278.2] | 70.1 [69.7–70.4] | 45.6 [45.0–46.0] | 0.54x | 0.58x | 1.00x | 6.09x | 1.54x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 245.7 [245.4–245.9] | 217.9 [217.7–244.5] | 74.0 [73.7–74.1] | 3.13s [3.12s–3.18s] | 894.2 [892.8–894.4] | 76.8 [76.3–76.8] | 3.20x | 2.84x | 0.96x | 40.8x | 11.6x |
| matmul | numeric | 40.3 [40.3–40.4] | 42.2 [42.0–42.4] | 51.3 [50.9–51.4] | 1.42s [1.41s–1.44s] | 546.9 [546.6–557.4] | 59.4 [58.6–60.4] | 0.68x | 0.71x | 0.86x | 23.9x | 9.21x |
| primes | numeric | 31.1 [30.7–31.6] | 20.1 [19.9–20.2] | 48.7 [47.9–49.7] | 1.84s [1.83s–1.87s] | 100.3 [100.3–101.1] | 47.2 [47.0–47.5] | 0.66x | 0.43x | 1.03x | 39.0x | 2.12x |
| base64 | string | 39.6 [38.8–39.9] | 35.7 [35.5–35.8] | 46.0 [46.0–46.5] | 594.3 [577.8–610.5] | 165.0 [163.8–165.0] | 61.4 [60.4–63.1] | 0.65x | 0.58x | 0.75x | 9.68x | 2.69x |
| levenshtein | string | 40.2 [40.0–40.5] | 32.0 [31.8–32.1] | 46.8 [46.3–47.0] | 295.9 [295.7–296.3] | 60.3 [60.1–60.4] | 46.8 [46.7–48.6] | 0.86x | 0.68x | 1.00x | 6.32x | 1.29x |
| json_gen | data | 51.3 [51.1–51.8] | 36.0 [35.7–36.1] | 46.5 [46.5–46.8] | 82.4 [75.9–85.7] | 26.4 [26.1–26.9] | 50.8 [50.7–51.1] | 1.01x | 0.71x | 0.91x | 1.62x | 0.52x |
| collatz | numeric | 344.9 [344.2–382.7] | 340.9 [339.8–341.7] | 271.0 [270.5–271.2] | 7.34s [7.33s–7.40s] | 6.27s [6.24s–6.28s] | 1.50s [1.49s–1.53s] | 0.23x | 0.23x | 0.18x | 4.90x | 4.19x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 291.8 [290.3–292.0] | 213.7 [212.7–214.1] | 106.3 [105.8–131.7] | 7.65s [7.62s–8.08s] | 2.20s [2.19s–2.20s] | 109.8 [109.7–110.6] | 2.66x | 1.95x | 0.97x | 69.7x | 20.0x |
| array1 | array | 16.6 [16.4–16.7] | 16.7 [16.6–16.8] | 45.0 [44.8–45.4] | 111.9 [111.8–112.5] | 41.6 [41.5–41.7] | 44.6 [44.5–44.6] | 0.37x | 0.37x | 1.01x | 2.51x | 0.93x |
| deriv | symbolic | 56.2 [56.1–56.9] | 27.2 [27.1–27.3] | 48.9 [48.5–49.5] | 108.8 [108.1–109.3] | 64.9 [64.4–65.6] | 47.3 [47.3–47.5] | 1.19x | 0.58x | 1.03x | 2.30x | 1.37x |
| diviter | iterative | 282.9 [282.8–283.4] | 283.6 [283.2–283.7] | 311.7 [311.5–312.6] | 14.42s [14.41s–14.46s] | 26.90s [26.83s–26.98s] | 517.7 [516.8–519.2] | 0.55x | 0.55x | 0.60x | 27.9x | 52.0x |
| divrec | recursive | 22.2 [21.9–22.3] | 17.9 [17.7–17.9] | 50.5 [50.2–51.1] | 50.2 [50.0–50.7] | 42.1 [41.9–42.2] | 51.5 [51.3–52.7] | 0.43x | 0.35x | 0.98x | 0.97x | 0.82x |
| gcbench | allocation | 237.5 [233.8–239.5] | 127.5 [124.5–130.2] | 116.1 [116.1–116.7] | 827.0 [823.9–831.3] | 559.8 [559.7–585.6] | 66.6 [65.9–66.8] | 3.57x | 1.91x | 1.74x | 12.4x | 8.40x |
| paraffins | combinat | 27.7 [27.6–28.3] | 27.6 [27.5–27.7] | 46.0 [45.7–46.5] | 52.1 [52.0–53.0] | 8.34 [7.89–8.48] | 44.1 [43.3–44.4] | 0.63x | 0.63x | 1.04x | 1.18x | 0.19x |
| pnpoly | numeric | 31.1 [30.9–31.1] | 32.1 [32.1–32.2] | 47.3 [46.9–49.5] | 156.3 [155.0–157.2] | 209.0 [208.1–210.2] | 48.9 [48.6–49.1] | 0.64x | 0.66x | 0.97x | 3.20x | 4.28x |
| puzzle | search | 39.5 [39.3–39.9] | 38.5 [38.4–38.8] | 46.5 [46.3–47.0] | 87.9 [87.1–88.6] | 35.5 [35.2–35.6] | 47.2 [45.9–126.5] | 0.84x | 0.82x | 0.99x | 1.86x | 0.75x |
| quicksort | sorting | 32.8 [32.3–33.4] | 23.5 [23.0–24.3] | 48.1 [45.7–50.1] | 189.6 [183.2–227.4] | 25.9 [25.6–26.1] | 44.8 [44.3–45.4] | 0.73x | 0.52x | 1.07x | 4.23x | 0.58x |
| ray | numeric | 29.1 [29.0–29.7] | 22.0 [21.9–22.2] | 45.9 [45.4–48.5] | 59.4 [57.1–79.7] | 21.3 [20.0–30.2] | 47.0 [46.9–58.7] | 0.62x | 0.47x | 0.98x | 1.26x | 0.45x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 223.5 [223.1–225.7] | 356.6 [354.2–371.5] | 49.8 [49.7–50.4] | 487.2 [486.9–489.6] | 222.9 [222.8–224.6] | 63.2 [62.6–64.8] | 3.53x | 5.64x | 0.79x | 7.71x | 3.52x |
| navier_stokes | numeric | 204.9 [204.5–205.4] | 205.3 [204.4–206.1] | 95.1 [94.4–95.4] | 541.0 [540.0–570.9] | 109.3 [109.2–109.3] | 60.0 [60.0–60.0] | 3.42x | 3.42x | 1.58x | 9.02x | 1.82x |
| splay | data | 373.5 [373.1–373.5] | 457.2 [456.9–460.2] | 66.1 [65.8–66.4] | 1.05s [981.7–1.05s] | 560.5 [557.6–602.1] | 92.7 [92.6–93.2] | 4.03x | 4.93x | 0.71x | 11.3x | 6.05x |
| hashmap | data | 155.6 [155.1–157.5] | 75.1 [75.0–75.3] | 48.1 [47.8–48.1] | 1.68s [1.67s–1.68s] | 322.0 [320.6–323.1] | 59.3 [59.1–59.5] | 2.63x | 1.27x | 0.81x | 28.3x | 5.43x |
| crypto_sha1 | crypto | 87.0 [86.9–89.1] | 740.7 [736.0–742.2] | 49.0 [49.0–49.9] | 425.8 [424.5–426.7] | 224.8 [224.6–224.9] | 52.6 [52.5–52.8] | 1.66x | 14.1x | 0.93x | 8.10x | 4.28x |
| raytrace3d | 3d | 249.5 [247.4–250.0] | 201.1 [200.8–203.4] | 52.3 [52.1–52.7] | 688.7 [686.4–709.8] | 170.0 [169.6–170.4] | 62.5 [62.0–63.5] | 3.99x | 3.22x | 0.84x | 11.0x | 2.72x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 266.6 [263.4–275.4] | 164.7 [164.1–166.0] | 59.6 [59.4–59.9] | 2.85s [2.78s–3.05s] | 621.3 [617.6–627.2] | 90.4 [83.8–171.8] | 2.95x | 1.82x | 0.66x | 31.5x | 6.87x |
| microdiff | data-diff | 89.9 [89.5–91.6] | 92.4 [91.8–93.4] | 51.5 [51.3–52.2] | 1.15s [1.14s–1.15s] | 115.0 [114.8–115.7] | 59.8 [59.7–60.4] | 1.50x | 1.55x | 0.86x | 19.2x | 1.92x |
| hyphen | hyphenation | 120.8 [120.2–144.8] | 120.0 [119.4–121.6] | 76.5 [75.8–77.1] | 1.79s [1.79s–1.80s] | 101.6 [100.5–102.2] | 57.2 [57.0–57.3] | 2.11x | 2.10x | 1.34x | 31.4x | 1.78x |
| prettier_ast | formatting | 1.20s [1.20s–1.21s] | 1.12s [1.12s–1.19s] | 104.7 [104.5–106.8] | 6.80s [6.06s–6.99s] | 1.64s [1.64s–1.65s] | 152.6 [151.7–153.1] | 7.88x | 7.35x | 0.69x | 44.6x | 10.7x |
| text_search | search | 15.15s [15.13s–15.20s] | 1.86s [1.86s–1.89s] | 585.0 [584.7–597.3] | 117.94s [117.75s–118.10s] | 38.92s [38.82s–38.97s] | 827.1 [826.0–831.1] | 18.3x | 2.25x | 0.71x | 143x | 47.1x |
| three_way_merge | merge | 4.02s [4.01s–4.02s] | 3.74s [3.72s–3.74s] | 2.20s [2.18s–2.20s] | 14.76s [14.72s–14.89s] | 6.16s [6.15s–6.17s] | 1.01s [1.01s–1.01s] | 3.96x | 3.69x | 2.17x | 14.6x | 6.08x |
| log_pipeline | log-processing | 6.36s [6.17s–6.48s] | 6.49s [6.48s–6.53s] | 673.8 [671.8–711.6] | 17.34s [17.21s–17.35s] | 9.58s [9.46s–9.63s] | 995.6 [994.8–999.7] | 6.39x | 6.52x | 0.68x | 17.4x | 9.62x |

