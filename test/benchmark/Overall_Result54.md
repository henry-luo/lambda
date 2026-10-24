# Lambda Benchmark Results: Result54

- **Date:** 2026-10-24
- **Platform:** Darwin arm64
- **Lambda commit:** `aea2a23f46ca134cc86fb5f21b7c7d7586d13fc7`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v54-aea2a23f46` (26,055,592 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 210.00s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 209.9s (batched 209.0s: sync 81.9s, async 127.1s; non-batched 0.9s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **Julia microbenchmark suite:** every language uses one complete warmup, then one measured workload; formatted-output timing includes synchronous null-sink writes.
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run; suites run in order `r7rs -> awfy -> beng -> kostya -> larceny -> julia -> jetstream -> text` with a 10s idle gap between suites
- **Range notation:** repeated-run timing cells are `median [minimum–maximum]`; single-run cells show the one measured value; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v54.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes 15, hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on the former single-frame Navier workload and hashmap. Navier now times all 15 frames and checks checksum 77; older snapshots that timed one JS frame require a fresh measurement for comparison.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with process startup and prior build steps excluded. Runtime compilation performed during the workload is included. This is the historical series, comparable back through Result18, the MIR columns pin `LAMBDA_EXEC_BACKEND=jit`, and LambdaJS pins `JS_EXEC_BACKEND=mir`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 0.32x | 0.26x | 0.21x | 0.26x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 1.27x | 0.47x | 0.12x | 2.60x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 0.25x | 0.18x | 0.11x | 1.49x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 0.65x | 0.56x | 0.23x | 0.52x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 0.66x | 0.55x | 0.32x | 0.74x |
| Julia microbenchmarks | 4 | 4 | 4 | 4 | 4 | 4 | 1.27x | 1.17x | 0.32x | 2.10x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 2.68x | 1.61x | 0.22x | 4.93x |
| Text | 11 | 11 | 11 | 11 | 11 | 11 | 1.49x | 0.97x | 0.47x | 11.5x |
| **Overall** | 71 | 71 | 71 | 71 | 71 | 71 | 0.80x | 0.53x | 0.22x | 1.63x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 67 | 0.49x | 0.74x | 2.44x | 67 |
| complete current suite | 71 | 0.53x | 0.80x | 2.42x | 71 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 2.42x over 71 of 71 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| text/jq_mix | 44.62s | 989.8 | 45.1x |
| awfy/deltablue | 19.3 | 1.20 | 16.1x |
| awfy/havlak | 590.6 | 39.1 | 15.1x |
| julia/parse_integers | 33.0 | 2.26 | 14.6x |
| text/microdiff | 36.7 | 2.61 | 14.0x |
| jetstream/cube3d | 6.35 | 0.462 | 13.8x |
| jetstream/splay | 223.1 | 18.5 | 12.0x |
| awfy/cd | 170.3 | 15.0 | 11.4x |
| jetstream/crypto_sha1 | 26.3 | 2.63 | 10.0x |
| jetstream/hashmap | 28.1 | 2.84 | 9.89x |
| kostya/base64 | 5.38 | 0.567 | 9.49x |
| text/prettier_ast | 354.6 | 41.6 | 8.53x |

---

### Notable Results

- Missing timings: **0** cells

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/spectralnorm | 232.3 | 2.54 | 91.5x |
| text/jq_bf | 252.59s | 2.93s | 86.1x |
| text/jq_tree | 395.40s | 5.77s | 68.6x |
| text/jq_mix | 231.12s | 3.94s | 58.7x |
| awfy/havlak | 2.56s | 94.3 | 27.2x |
| text/jq_records | 53.33s | 2.01s | 26.5x |
| awfy/nbody | 128.5 | 5.34 | 24.1x |
| jetstream/hashmap | 360.9 | 15.9 | 22.7x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| r7rs/sumfp | 0.054 | 0.891 | 0.06x |
| larceny/ray | 0.226 | 3.57 | 0.06x |
| r7rs/fft | 0.149 | 1.55 | 0.10x |
| larceny/paraffins | 0.098 | 0.964 | 0.10x |
| r7rs/tak | 0.089 | 0.780 | 0.11x |
| beng/fasta | 0.881 | 5.93 | 0.15x |
| kostya/collatz | 240.1 | 1.42s | 0.17x |
| r7rs/cpstak | 0.183 | 0.978 | 0.19x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.30 [1.29–1.34] | 1.31 [1.29–1.31] | 1.12 [1.09–1.14] | 0.876 [0.862–0.890] | 1.84 [1.84–1.85] | 0.70x | 0.71x | 0.61x | 0.47x |
| fibfp | recursive | 1.35 [1.34–1.36] | 0.760 [0.757–0.777] | 1.09 [1.04–1.14] | 0.852 [0.848–0.865] | 1.80 [1.72–1.80] | 0.75x | 0.42x | 0.61x | 0.47x |
| tak | recursive | 0.142 [0.142–0.152] | 0.143 [0.136–0.146] | 0.119 [0.119–0.123] | 0.089 [0.088–0.089] | 0.780 [0.778–0.784] | 0.18x | 0.18x | 0.15x | 0.11x |
| cpstak | closure | 0.283 [0.278–0.304] | 0.290 [0.273–0.295] | 0.231 [0.216–0.235] | 0.183 [0.175–0.187] | 0.978 [0.959–0.983] | 0.29x | 0.30x | 0.24x | 0.19x |
| sum | iterative | 0.266 [0.265–0.270] | 0.266 [0.265–0.280] | 0.271 [0.270–0.275] | 0.540 [0.537–0.541] | 1.17 [1.17–1.19] | 0.23x | 0.23x | 0.23x | 0.46x |
| sumfp | iterative | 0.069 [0.068–0.069] | 0.069 [0.069–0.078] | 0.081 [0.079–0.082] | 0.054 [0.054–0.054] | 0.891 [0.870–0.900] | 0.08x | 0.08x | 0.09x | 0.06x |
| nqueens | backtrack | 0.785 [0.763–0.814] | 0.728 [0.721–0.734] | 0.359 [0.357–0.361] | 0.622 [0.620–0.622] | 1.72 [1.71–1.72] | 0.46x | 0.42x | 0.21x | 0.36x |
| fft | numeric | 0.202 [0.191–0.205] | 0.076 [0.074–0.076] | 0.025 [0.024–0.027] | 0.149 [0.149–0.158] | 1.55 [1.54–1.61] | 0.13x | 0.05x | 0.02x | 0.10x |
| mbrot | numeric | 0.714 [0.713–0.715] | 0.564 [0.564–0.588] | 0.449 [0.443–0.451] | 1.38 [1.36–1.42] | 1.85 [1.85–1.86] | 0.38x | 0.30x | 0.24x | 0.74x |
| ack | recursive | 13.4 [13.4–13.5] | 9.90 [9.89–10.0] | 11.7 [11.6–11.7] | 6.49 [6.46–7.52] | 13.3 [13.2–13.4] | 1.01x | 0.75x | 0.88x | 0.49x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.035 [0.032–0.050] | 0.026 [0.026–0.026] | 0.014 [0.014–0.017] | 0.083 [0.083–0.083] | 0.382 [0.378–0.386] | 0.09x | 0.07x | 0.04x | 0.22x |
| permute | micro | 0.264 [0.263–0.264] | 0.064 [0.063–0.064] | 0.031 [0.025–0.032] | 1.01 [1.00–1.08] | 0.801 [0.797–0.808] | 0.33x | 0.08x | 0.04x | 1.26x |
| queens | micro | 0.175 [0.175–0.176] | 0.057 [0.056–0.058] | 0.018 [0.017–0.021] | 0.492 [0.483–0.494] | 0.640 [0.640–0.644] | 0.27x | 0.09x | 0.03x | 0.77x |
| towers | micro | 0.470 [0.469–0.475] | 0.163 [0.162–0.164] | 0.027 [0.027–0.028] | 2.15 [2.13–2.16] | 1.10 [1.10–1.21] | 0.43x | 0.15x | 0.02x | 1.96x |
| bounce | micro | 0.065 [0.064–0.067] | 0.054 [0.053–0.055] | 0.024 [0.024–0.028] | 0.688 [0.676–0.746] | 0.534 [0.532–0.548] | 0.12x | 0.10x | 0.05x | 1.29x |
| list | micro | 0.838 [0.820–0.839] | 0.119 [0.116–0.120] | 0.025 [0.021–0.026] | 0.496 [0.492–0.522] | 0.488 [0.480–0.490] | 1.72x | 0.24x | 0.05x | 1.02x |
| storage | micro | 0.460 [0.451–0.469] | 0.252 [0.250–0.255] | 0.090 [0.086–0.096] | 0.628 [0.590–0.725] | 0.644 [0.624–0.653] | 0.71x | 0.39x | 0.14x | 0.98x |
| mandelbrot | compute | 37.5 [37.5–37.5] | 37.6 [37.5–37.9] | 30.6 [30.6–30.6] | 33.5 [33.2–33.5] | 31.0 [30.9–31.0] | 1.21x | 1.22x | 0.99x | 1.08x |
| nbody | compute | 7.08 [7.05–7.10] | 2.66 [2.58–2.66] | 1.49 [1.47–1.50] | 128.5 [127.3–129.8] | 5.34 [5.32–5.39] | 1.33x | 0.50x | 0.28x | 24.1x |
| richards | macro | 309.5 [309.0–310.8] | 76.5 [76.3–106.9] | 28.3 [28.0–28.5] | 226.0 [225.9–227.9] | 46.3 [46.2–46.9] | 6.68x | 1.65x | 0.61x | 4.88x |
| json | macro | 5.46 [5.43–5.50] | 1.89 [1.87–1.92] | 0.251 [0.249–0.259] | 9.68 [9.68–9.72] | 2.59 [2.58–2.59] | 2.11x | 0.73x | 0.10x | 3.74x |
| deltablue | macro | 80.9 [80.5–81.1] | 19.3 [19.2–19.4] | 1.20 [1.17–1.20] | 95.9 [95.1–96.9] | 11.5 [11.3–11.8] | 7.03x | 1.68x | 0.10x | 8.34x |
| havlak | macro | 1.68s [1.62s–1.71s] | 590.6 [586.7–629.1] | 39.1 [38.7–39.1] | 2.56s [2.53s–2.57s] | 94.3 [94.1–94.5] | 17.8x | 6.26x | 0.41x | 27.2x |
| cd | macro | 709.1 [703.4–714.2] | 170.3 [169.8–170.5] | 15.0 [14.9–15.0] | 405.5 [404.4–407.3] | 35.4 [35.4–35.7] | 20.0x | 4.80x | 0.42x | 11.4x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 5.53 [4.93–7.16] | 2.54 [2.52–2.55] | 3.05 [3.02–3.11] | 3.11 [3.08–3.13] | 3.93 [3.80–3.97] | 1.41x | 0.65x | 0.77x | 0.79x |
| fannkuch | permutation | 0.276 [0.275–0.279] | 0.291 [0.287–0.295] | 0.144 [0.143–0.146] | 43.7 [43.0–43.7] | 3.87 [3.81–3.93] | 0.07x | 0.08x | 0.04x | 11.3x |
| fasta | generation | 0.468 [0.451–0.485] | 0.404 [0.401–0.421] | 0.240 [0.240–0.243] | 0.881 [0.881–0.896] | 5.93 [5.93–5.94] | 0.08x | 0.07x | 0.04x | 0.15x |
| knucleotide | hashing | 0.854 [0.846–0.882] | 0.445 [0.439–0.449] | 0.288 [0.281–0.297] | 3.36 [3.30–3.39] | 4.69 [4.68–4.79] | 0.18x | 0.09x | 0.06x | 0.72x |
| pidigits | bignum | 0.361 [0.360–0.374] | 0.367 [0.360–0.369] | 0.047 [0.045–0.048] | 0.339 [0.335–0.351] | 1.80 [1.76–2.61] | 0.20x | 0.20x | 0.03x | 0.19x |
| regexredux | regex | 0.843 [0.820–0.871] | 0.827 [0.822–0.850] | 1.14 [1.13–1.14] | 2.92 [2.89–2.93] | 2.31 [2.24–9.07] | 0.37x | 0.36x | 0.49x | 1.27x |
| revcomp | string | 0.659 [0.631–0.680] | 0.549 [0.548–0.642] | 0.375 [0.374–0.377] | 3.69 [3.60–3.87] | 3.22 [3.18–3.47] | 0.20x | 0.17x | 0.12x | 1.15x |
| spectralnorm | numeric | 1.66 [1.66–1.66] | 0.789 [0.785–0.800] | 0.350 [0.348–0.350] | 232.3 [232.2–233.4] | 2.54 [2.49–2.58] | 0.65x | 0.31x | 0.14x | 91.5x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 96.8 [90.6–117.7] | 93.5 [93.2–93.6] | 28.0 [26.6–28.4] | 43.1 [42.9–43.7] | 33.4 [33.3–33.4] | 2.90x | 2.80x | 0.84x | 1.29x |
| matmul | numeric | 4.86 [4.85–4.86] | 4.77 [4.73–4.79] | 6.06 [6.06–6.08] | 7.09 [7.02–14.5] | 15.3 [15.3–16.2] | 0.32x | 0.31x | 0.40x | 0.46x |
| primes | numeric | 2.25 [2.24–2.27] | 2.19 [2.18–2.19] | 1.59 [1.58–1.60] | 2.73 [2.73–2.74] | 4.40 [4.34–4.50] | 0.51x | 0.50x | 0.36x | 0.62x |
| base64 | string | 12.6 [12.6–12.9] | 5.38 [5.13–5.42] | 0.567 [0.565–0.569] | 9.92 [9.84–9.96] | 17.3 [17.1–17.3] | 0.73x | 0.31x | 0.03x | 0.57x |
| levenshtein | string | 2.25 [2.13–2.25] | 2.16 [2.15–2.25] | 0.888 [0.887–0.905] | 1.55 [1.47–1.60] | 3.92 [3.90–3.92] | 0.57x | 0.55x | 0.23x | 0.40x |
| json_gen | data | 7.34 [7.33–7.45] | 6.94 [6.92–7.14] | 1.52 [1.52–1.59] | 4.42 [4.32–4.51] | 6.12 [6.09–6.23] | 1.20x | 1.13x | 0.25x | 0.72x |
| collatz | numeric | 290.2 [289.6–290.3] | 277.1 [275.2–278.4] | 229.8 [229.6–229.8] | 240.1 [228.6–255.7] | 1.42s [1.42s–1.43s] | 0.20x | 0.20x | 0.16x | 0.17x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 171.0 [168.8–173.9] | 151.7 [149.4–179.6] | 57.3 [57.3–58.1] | 140.7 [140.5–141.0] | 68.8 [68.4–68.8] | 2.49x | 2.21x | 0.83x | 2.05x |
| array1 | array | 0.805 [0.804–0.808] | 0.808 [0.805–0.808] | 0.294 [0.294–0.305] | 0.543 [0.543–0.552] | 1.81 [1.80–1.85] | 0.45x | 0.45x | 0.16x | 0.30x |
| deriv | symbolic | 18.0 [17.9–45.5] | 5.73 [5.72–5.80] | 2.83 [2.83–2.85] | 12.1 [12.0–12.1] | 3.84 [3.76–3.99] | 4.69x | 1.49x | 0.74x | 3.15x |
| diviter | iterative | 266.1 [265.8–266.2] | 265.9 [265.8–266.7] | 262.3 [259.2–264.6] | 537.4 [536.6–563.8] | 471.7 [470.9–484.8] | 0.56x | 0.56x | 0.56x | 1.14x |
| divrec | recursive | 1.20 [1.20–1.21] | 1.24 [1.21–1.32] | 4.86 [4.84–4.88] | 4.76 [4.75–4.76] | 7.55 [7.49–7.56] | 0.16x | 0.16x | 0.64x | 0.63x |
| gcbench | allocation | 118.1 [116.1–119.4] | 70.8 [70.6–71.2] | 70.7 [70.5–70.8] | 94.9 [94.6–95.4] | 23.1 [23.0–23.2] | 5.11x | 3.06x | 3.06x | 4.10x |
| paraffins | combinat | 0.192 [0.189–0.194] | 0.149 [0.149–0.152] | 0.049 [0.049–0.053] | 0.098 [0.097–0.099] | 0.964 [0.954–0.980] | 0.20x | 0.15x | 0.05x | 0.10x |
| pnpoly | numeric | 2.29 [2.27–2.33] | 3.68 [3.61–3.69] | 1.95 [1.95–1.98] | 10.6 [10.6–10.7] | 5.79 [5.78–5.81] | 0.39x | 0.63x | 0.34x | 1.84x |
| puzzle | search | 5.09 [5.03–5.12] | 5.04 [5.04–5.12] | 1.19 [1.19–1.21] | 2.39 [2.37–2.40] | 3.29 [3.27–3.32] | 1.55x | 1.53x | 0.36x | 0.72x |
| quicksort | sorting | 1.06 [1.01–1.09] | 0.587 [0.571–0.672] | 0.193 [0.192–0.195] | 1.22 [1.22–1.23] | 1.68 [1.66–1.68] | 0.63x | 0.35x | 0.12x | 0.73x |
| ray | numeric | 0.209 [0.208–0.219] | 0.218 [0.209–0.219] | 0.173 [0.169–0.181] | 0.226 [0.226–0.229] | 3.57 [3.55–3.66] | 0.06x | 0.06x | 0.05x | 0.06x |

### Julia microbenchmarks

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| parse_integers | conversion | 36.5 [36.2–36.6] | 33.0 [32.7–33.1] | 2.26 [2.25–2.26] | 46.1 [45.9–46.1] | 13.0 [12.9–13.2] | 2.80x | 2.54x | 0.17x | 3.54x |
| matrix_statistics | numeric | 16.6 [16.2–16.9] | 15.5 [15.5–15.6] | 8.98 [8.97–8.99] | 66.8 [66.7–67.0] | 21.2 [21.1–21.3] | 0.78x | 0.73x | 0.42x | 3.15x |
| iteration_pi_sum | numeric | 2.89 [2.88–3.11] | 2.89 [2.89–2.95] | 3.00 [2.98–3.02] | 3.21 [3.20–3.22] | 4.89 [4.86–4.94] | 0.59x | 0.59x | 0.61x | 0.66x |
| formatted_output | io | 74.7 [74.6–74.9] | 61.5 [61.5–63.2] | 8.29 [8.21–8.44] | 96.3 [96.2–96.9] | 36.6 [36.2–36.7] | 2.04x | 1.68x | 0.23x | 2.63x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 8.54 [8.47–8.71] | 6.35 [6.32–6.38] | 0.462 [0.461–0.482] | 68.6 [67.9–68.6] | 17.9 [17.8–30.2] | 0.48x | 0.36x | 0.03x | 3.83x |
| navier_stokes | numeric | 75.8 [75.0–75.9] | 67.6 [67.5–68.0] | 45.4 [45.4–45.5] | 422.4 [422.2–427.8] | 57.5 [57.5–57.6] | 1.32x | 1.18x | 0.79x | 7.34x |
| splay | data | 191.6 [190.2–193.4] | 223.1 [222.2–223.7] | 18.5 [18.5–18.7] | 51.1 [51.0–51.7] | 19.8 [19.1–20.3] | 9.66x | 11.2x | 0.93x | 2.58x |
| hashmap | data | 71.9 [70.0–72.8] | 28.1 [28.0–28.1] | 2.84 [2.69–8.87] | 360.9 [357.7–361.6] | 15.9 [15.0–16.7] | 4.52x | 1.77x | 0.18x | 22.7x |
| crypto_sha1 | crypto | 27.5 [27.2–27.5] | 26.3 [26.2–26.3] | 2.63 [2.59–2.65] | 11.0 [10.8–11.0] | 8.69 [8.67–8.77] | 3.16x | 3.02x | 0.30x | 1.26x |
| raytrace3d | 3d | 78.0 [77.9–78.2] | 12.7 [12.2–19.3] | 2.16 [2.14–2.19] | 124.9 [123.7–125.1] | 18.1 [18.0–18.3] | 4.31x | 0.70x | 0.12x | 6.90x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 44.6 [44.4–45.5] | 43.0 [42.8–43.2] | 12.7 [12.7–12.7] | 173.1 [172.8–175.2] | 39.7 [39.1–40.1] | 1.12x | 1.08x | 0.32x | 4.36x |
| microdiff | data-diff | 32.4 [32.4–32.6] | 36.7 [36.5–37.0] | 2.61 [2.61–2.63] | 114.3 [114.2–116.0] | 16.3 [16.1–16.4] | 1.99x | 2.25x | 0.16x | 7.01x |
| hyphen | hyphenation | 48.6 [48.5–49.7] | 9.26 [9.24–9.29] | 1.47 [1.47–1.64] | 49.1 [48.4–50.9] | 6.64 [6.45–6.74] | 7.32x | 1.39x | 0.22x | 7.39x |
| prettier_ast | formatting | 823.3 [819.9–826.9] | 354.6 [351.0–358.0] | 41.6 [41.0–41.6] | 536.2 [532.8–539.0] | 99.9 [99.3–100.9] | 8.24x | 3.55x | 0.42x | 5.37x |
| text_search | search | 1.45s [1.44s–1.46s] | 1.12s [1.12s–1.13s] | 510.6 [509.3–523.1] | 4.62s [4.58s–4.63s] | 776.8 [776.6–779.1] | 1.86x | 1.45x | 0.66x | 5.95x |
| three_way_merge | merge | 2.10s [2.07s–2.10s] | 1.40s [1.39s–1.41s] | 2.14s [2.13s–2.19s] | 1.42s [1.42s–1.45s] | 964.7 [964.3–967.9] | 2.17x | 1.45x | 2.22x | 1.47x |
| log_pipeline | log-processing | 4.20s [4.15s–4.21s] | 1.05s [1.03s–1.06s] | 623.4 [622.5–634.1] | 4.50s [4.46s–4.51s] | 941.5 [936.2–944.8] | 4.47x | 1.12x | 0.66x | 4.78x |
| jq_mix | jq-breadth | 44.80s [44.68s–44.89s] | 44.62s [44.58s–44.64s] | 989.8 [987.6–991.4] / λ-VM --- | 231.12s [228.25s–234.13s] | 3.94s [3.91s–3.96s] | 11.4x | 11.3x | 0.25x | 58.7x |
| jq_records | jq-data | 333.1 [331.0–388.8] | 390.4 [387.4–402.5] | 863.2 [859.6–866.0] / λ-VM --- | 53.33s [53.24s–53.71s] | 2.01s [1.98s–2.02s] | 0.17x | 0.19x | 0.43x | 26.5x |
| jq_bf | jq-interpreter | 164.2 [163.8–165.7] | 143.6 [142.0–143.7] | 1.85s [1.83s–1.89s] / λ-VM --- | 252.59s [252.08s–252.88s] | 2.93s [2.93s–2.94s] | 0.06x | 0.05x | 0.63x | 86.1x |
| jq_tree | jq-paths | 1.77s [1.76s–1.78s] | 1.38s [1.36s–1.40s] | 4.09s [4.02s–4.16s] / λ-VM --- | 395.40s [394.41s–395.92s] | 5.77s [5.75s–5.80s] | 0.31x | 0.24x | 0.71x | 68.6x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, including **startup and compilation performed inside that process**. Build steps completed before invocation are excluded. The MIR and LambdaJS columns use the shipped auto tier -- no `LAMBDA_EXEC_BACKEND` or `JS_EXEC_BACKEND` override -- which is what `lambda.exe run script.ls` and `lambda.exe js script.js` actually do.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Process timing includes each engine's recorded warmup policy, workload and verification; separately added measurements are described above.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. The MIR and LambdaJS columns are re-run, because part 1 pins the compiled lane and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS (default) | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS (default)/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 0.36x | 0.33x | 1.01x | 1.30x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 1.12x | 0.93x | 0.95x | 5.97x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 0.43x | 0.42x | 1.02x | 1.52x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 1.25x | 0.86x | 0.73x | 12.4x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 0.93x | 0.63x | 1.00x | 4.77x |
| Julia microbenchmarks | 4 | 4 | 4 | 4 | 4 | 4 | 1.35x | 0.98x | 1.25x | 42.2x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 2.55x | 2.62x | 0.84x | 33.6x |
| Text | 11 | 11 | 11 | 11 | 7 | 11 | 1.67x | 1.37x | 0.73x | 32.3x |
| **Overall** | 71 | 71 | 71 | 71 | 67 | 71 | 0.97x | 0.80x | 0.92x | 6.57x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 13.0 [12.7–13.3] | 13.4 [12.7–13.9] | 47.2 [47.1–476.9] | 71.2 [70.7–72.4] | 48.9 [48.6–50.0] | 0.27x | 0.27x | 0.97x | 1.45x |
| fibfp | recursive | 15.0 [13.4–15.6] | 12.5 [12.2–12.6] | 46.0 [45.9–46.0] | 69.7 [68.6–70.0] | 45.0 [44.7–45.1] | 0.33x | 0.28x | 1.02x | 1.55x |
| tak | recursive | 12.0 [12.0–12.3] | 11.7 [11.6–11.8] | 45.1 [44.9–45.2] | 28.8 [28.8–29.5] | 43.9 [43.7–44.5] | 0.27x | 0.27x | 1.03x | 0.66x |
| cpstak | closure | 12.3 [12.1–12.4] | 11.7 [11.7–12.0] | 45.9 [44.7–46.0] | 33.8 [33.0–60.2] | 44.0 [43.9–44.4] | 0.28x | 0.27x | 1.04x | 0.77x |
| sum | iterative | 14.5 [14.4–14.6] | 11.8 [11.7–11.9] | 44.8 [44.8–45.4] | 28.3 [28.0–28.4] | 44.0 [43.9–44.1] | 0.33x | 0.27x | 1.02x | 0.64x |
| sumfp | iterative | 14.4 [14.3–14.5] | 12.9 [12.9–13.1] | 44.7 [44.5–44.8] | 24.9 [24.6–25.0] | 43.7 [43.5–43.8] | 0.33x | 0.30x | 1.02x | 0.57x |
| nqueens | backtrack | 20.1 [19.8–20.2] | 21.8 [21.7–21.9] | 45.9 [45.9–46.3] | 131.2 [130.4–132.3] | 44.7 [44.6–44.9] | 0.45x | 0.49x | 1.03x | 2.93x |
| fft | numeric | 29.9 [29.6–30.4] | 24.8 [24.6–25.2] | 45.1 [44.8–45.1] | 129.3 [129.2–129.5] | 44.9 [44.9–45.4] | 0.67x | 0.55x | 1.00x | 2.88x |
| mbrot | numeric | 15.5 [15.4–15.6] | 15.5 [15.4–15.7] | 45.4 [45.3–45.5] | 35.9 [35.3–36.9] | 45.4 [45.1–74.9] | 0.34x | 0.34x | 1.00x | 0.79x |
| ack | recursive | 25.8 [25.7–25.9] | 21.2 [21.2–21.2] | 56.6 [56.1–57.0] | 271.9 [270.8–274.9] | 56.6 [56.0–56.6] | 0.46x | 0.37x | 1.00x | 4.81x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 16.0 [15.9–16.6] | 13.1 [13.0–13.2] | 44.5 [44.3–45.0] | 20.3 [19.9–20.6] | 44.3 [44.0–46.2] | 0.36x | 0.29x | 1.00x | 0.46x |
| permute | micro | 14.0 [14.0–14.1] | 14.6 [14.5–14.6] | 45.1 [44.7–45.4] | 43.6 [43.3–43.8] | 44.8 [44.5–73.6] | 0.31x | 0.33x | 1.01x | 0.97x |
| queens | micro | 15.9 [15.8–16.1] | 17.3 [17.2–17.3] | 44.7 [44.5–44.8] | 28.3 [28.2–28.9] | 44.7 [44.5–45.6] | 0.36x | 0.39x | 1.00x | 0.63x |
| towers | micro | 18.7 [18.5–19.0] | 20.3 [19.9–20.4] | 45.5 [45.2–45.8] | 61.1 [61.0–62.7] | 45.3 [44.5–45.5] | 0.41x | 0.45x | 1.00x | 1.35x |
| bounce | micro | 14.8 [14.4–14.9] | 14.7 [14.4–14.7] | 45.1 [44.7–45.1] | 30.3 [30.2–30.7] | 45.6 [44.9–71.6] | 0.32x | 0.32x | 0.99x | 0.66x |
| list | micro | 14.0 [13.9–14.1] | 14.5 [14.4–15.0] | 45.3 [44.9–45.5] | 28.5 [28.5–29.0] | 44.5 [44.2–45.4] | 0.31x | 0.33x | 1.02x | 0.64x |
| storage | micro | 13.8 [13.7–13.8] | 14.4 [14.1–14.5] | 45.2 [44.9–45.4] | 36.1 [36.1–37.3] | 45.2 [45.2–48.2] | 0.30x | 0.32x | 1.00x | 0.80x |
| mandelbrot | compute | 65.6 [64.9–65.8] | 61.9 [61.2–62.2] | 76.0 [75.6–76.0] | 7.59s [7.58s–7.60s] | 74.9 [74.7–76.2] | 0.88x | 0.83x | 1.01x | 101x |
| nbody | compute | 56.1 [55.8–56.1] | 56.1 [56.0–56.2] | 47.4 [47.3–47.5] | 2.79s [2.79s–2.80s] | 49.1 [48.8–50.7] | 1.14x | 1.14x | 0.97x | 56.9x |
| richards | macro | 572.6 [571.0–575.1] | 326.6 [322.8–327.7] | 75.9 [75.7–76.6] | 3.41s [3.39s–3.42s] | 90.5 [90.2–91.8] | 6.33x | 3.61x | 0.84x | 37.7x |
| json | macro | 41.0 [41.0–73.3] | 37.1 [37.0–37.1] | 52.3 [52.1–52.9] | 141.8 [141.1–143.7] | 47.5 [46.9–48.8] | 0.86x | 0.78x | 1.10x | 2.99x |
| deltablue | macro | 336.4 [320.0–343.0] | 230.8 [213.7–254.6] | 53.8 [53.4–54.0] | 1.26s [1.25s–1.28s] | 56.6 [56.3–58.1] | 5.95x | 4.08x | 0.95x | 22.3x |
| havlak | macro | 3.69s [3.63s–3.72s] | 1.20s [1.20s–1.22s] | 91.1 [91.0–91.2] | 41.37s [41.23s–41.37s] | 142.1 [141.7–142.3] | 25.9x | 8.44x | 0.64x | 291x |
| cd | macro | 941.9 [941.5–975.3] | 535.6 [533.8–535.7] | 67.3 [67.3–68.1] | 10.71s [10.68s–10.72s] | 80.4 [79.9–81.9] | 11.7x | 6.66x | 0.84x | 133x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 19.7 [19.6–19.9] | 18.6 [18.6–18.7] | 48.1 [48.0–48.2] | 65.2 [64.5–66.1] | 45.5 [45.3–45.6] | 0.43x | 0.41x | 1.06x | 1.43x |
| fannkuch | permutation | 29.2 [29.0–29.4] | 25.4 [25.3–25.7] | 45.2 [45.0–45.4] | 108.8 [103.1–127.2] | 45.3 [45.2–45.4] | 0.64x | 0.56x | 1.00x | 2.40x |
| fasta | generation | 20.6 [20.5–20.9] | 20.4 [20.2–20.7] | 45.4 [45.2–45.6] | 51.8 [51.7–52.4] | 47.8 [47.5–48.0] | 0.43x | 0.43x | 0.95x | 1.08x |
| knucleotide | hashing | 24.6 [24.1–24.6] | 22.8 [22.5–22.8] | 46.2 [46.2–46.4] | 86.2 [86.0–87.1] | 46.5 [46.5–46.8] | 0.53x | 0.49x | 0.99x | 1.85x |
| pidigits | bignum | 11.8 [11.3–12.1] | 11.2 [11.2–11.7] | 47.1 [47.1–47.3] | 22.3 [22.2–22.3] | 42.9 [42.8–43.5] | 0.28x | 0.26x | 1.10x | 0.52x |
| regexredux | regex | 11.2 [10.8–11.2] | 11.6 [11.5–11.6] | 47.7 [47.5–48.1] | 24.6 [24.6–24.8] | 44.0 [43.5–71.9] | 0.25x | 0.26x | 1.09x | 0.56x |
| revcomp | string | 14.4 [14.4–14.9] | 14.9 [14.6–15.3] | 45.8 [45.5–45.9] | 67.1 [67.0–67.6] | 44.7 [44.6–44.9] | 0.32x | 0.33x | 1.03x | 1.50x |
| spectralnorm | numeric | 35.3 [35.2–35.4] | 37.6 [37.4–37.8] | 45.4 [45.0–45.5] | 431.3 [427.1–434.1] | 46.2 [45.7–46.8] | 0.76x | 0.81x | 0.98x | 9.33x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 110.4 [110.1–111.0] | 114.7 [114.7–115.5] | 73.1 [71.9–74.7] | 5.17s [5.14s–5.20s] | 76.9 [76.9–77.0] | 1.44x | 1.49x | 0.95x | 67.2x |
| matmul | numeric | 113.4 [113.0–113.6] | 46.4 [46.2–46.7] | 51.1 [50.9–51.1] | 5.78s [5.68s–5.81s] | 58.3 [58.1–60.0] | 1.95x | 0.80x | 0.88x | 99.2x |
| primes | numeric | 155.3 [154.6–182.1] | 73.7 [73.3–73.7] | 46.9 [46.6–47.0] | 1.73s [1.72s–1.76s] | 47.8 [47.6–48.3] | 3.25x | 1.54x | 0.98x | 36.2x |
| base64 | string | 54.2 [54.1–54.7] | 43.0 [43.0–43.2] | 45.9 [45.8–46.0] | 1.13s [1.12s–1.17s] | 60.6 [60.6–61.0] | 0.89x | 0.71x | 0.76x | 18.6x |
| levenshtein | string | 99.0 [96.5–99.4] | 59.1 [59.0–59.5] | 46.3 [46.2–46.3] | 624.3 [622.9–639.9] | 47.1 [47.1–47.4] | 2.10x | 1.26x | 0.98x | 13.3x |
| json_gen | data | 61.8 [61.7–62.0] | 49.5 [48.8–49.7] | 47.2 [46.9–47.6] | 111.5 [110.6–138.9] | 49.6 [49.4–50.0] | 1.25x | 1.00x | 0.95x | 2.25x |
| collatz | numeric | 331.8 [330.0–336.1] | 317.7 [316.7–318.2] | 275.2 [275.0–275.4] | 495.8 [491.1–498.8] | 1.46s [1.46s–1.47s] | 0.23x | 0.22x | 0.19x | 0.34x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 191.0 [190.6–191.4] | 170.1 [169.9–171.2] | 103.5 [103.3–103.8] | 20.79s [20.70s–20.83s] | 112.2 [112.1–112.6] | 1.70x | 1.52x | 0.92x | 185x |
| array1 | array | 134.8 [134.0–136.1] | 128.3 [127.6–128.8] | 45.0 [44.6–45.0] | 365.9 [362.7–369.1] | 44.7 [44.5–44.8] | 3.02x | 2.87x | 1.01x | 8.19x |
| deriv | symbolic | 37.7 [37.3–37.9] | 24.1 [24.1–24.2] | 48.8 [48.7–48.9] | 172.4 [171.4–172.9] | 48.4 [48.1–48.6] | 0.78x | 0.50x | 1.01x | 3.56x |
| diviter | iterative | 369.2 [369.0–369.4] | 11.4 [11.3–12.6] | 307.8 [304.6–309.0] | 675.0 [675.0–675.7] | 516.6 [515.4–528.8] | 0.71x | 0.02x | 0.60x | 1.31x |
| divrec | recursive | 13.5 [13.5–13.5] | 14.2 [14.1–14.5] | 49.7 [49.5–49.8] | 129.7 [128.6–130.6] | 50.7 [50.6–50.9] | 0.27x | 0.28x | 0.98x | 2.56x |
| gcbench | allocation | 189.9 [189.7–215.4] | 104.9 [104.6–105.5] | 116.1 [115.8–116.3] | 836.8 [831.5–838.2] | 65.4 [65.3–65.7] | 2.90x | 1.60x | 1.77x | 12.8x |
| paraffins | combinat | 50.7 [33.4–50.8] | 50.0 [49.2–50.1] | 46.2 [45.8–46.8] | 93.5 [93.4–93.7] | 44.3 [44.0–45.3] | 1.14x | 1.13x | 1.04x | 2.11x |
| pnpoly | numeric | 30.5 [30.4–30.5] | 39.2 [39.2–39.3] | 47.4 [47.1–47.5] | 113.7 [113.4–114.6] | 48.7 [48.7–49.3] | 0.63x | 0.81x | 0.97x | 2.33x |
| puzzle | search | 31.1 [31.0–31.2] | 32.9 [32.6–33.3] | 46.1 [45.5–46.3] | 56.6 [56.5–56.6] | 46.4 [46.2–46.7] | 0.67x | 0.71x | 0.99x | 1.22x |
| quicksort | sorting | 21.8 [21.7–21.9] | 23.9 [23.7–24.1] | 45.5 [45.4–45.5] | 398.3 [396.9–420.9] | 45.2 [45.0–46.0] | 0.48x | 0.53x | 1.01x | 8.82x |
| ray | numeric | 40.9 [40.4–41.6] | 41.6 [41.5–42.0] | 45.6 [45.2–45.6] | 112.0 [111.7–112.2] | 46.6 [46.6–47.5] | 0.88x | 0.89x | 0.98x | 2.40x |

### Julia microbenchmarks

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| parse_integers | conversion | 119.8 [118.7–120.1] | 104.9 [99.6–115.1] | 95.2 [94.7–95.4] | 11.98s [11.85s–11.99s] | 72.6 [72.5–72.8] | 1.65x | 1.44x | 1.31x | 165x |
| matrix_statistics | numeric | 79.1 [78.2–79.6] | 75.2 [73.7–107.4] | 108.3 [107.8–108.4] | 3.84s [3.82s–3.84s] | 93.4 [92.6–93.5] | 0.85x | 0.81x | 1.16x | 41.1x |
| iteration_pi_sum | numeric | 65.4 [65.1–65.8] | 32.0 [32.0–32.2] | 95.9 [95.6–96.0] | 208.5 [207.7–209.7] | 53.1 [53.1–53.3] | 1.23x | 0.60x | 1.81x | 3.92x |
| formatted_output | io | 231.0 [230.3–231.2] | 157.0 [156.8–158.1] | 107.4 [106.0–107.5] | 14.26s [14.25s–14.60s] | 119.3 [119.3–119.4] | 1.94x | 1.32x | 0.90x | 120x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 93.2 [92.8–93.8] | 108.9 [108.0–108.9] | 49.0 [48.7–56.5] | 1.77s [1.77s–1.80s] | 63.4 [63.0–128.4] | 1.47x | 1.72x | 0.77x | 27.9x |
| navier_stokes | numeric | 245.6 [244.4–245.8] | 223.4 [221.6–225.4] | 92.8 [92.8–94.1] | 14.39s [14.06s–14.48s] | 103.1 [103.1–103.5] | 2.38x | 2.17x | 0.90x | 140x |
| splay | data | 352.1 [350.3–354.6] | 895.8 [892.6–925.2] | 65.3 [65.3–65.9] | 1.25s [1.25s–1.26s] | 92.3 [91.7–92.9] | 3.81x | 9.70x | 0.71x | 13.6x |
| hashmap | data | 165.2 [164.9–166.3] | 119.2 [118.4–120.6] | 55.2 [48.5–74.0] | 4.31s [4.28s–4.36s] | 60.3 [58.9–87.2] | 2.74x | 1.98x | 0.92x | 71.5x |
| crypto_sha1 | crypto | 156.0 [155.8–157.2] | 152.6 [151.9–153.1] | 48.3 [48.1–48.9] | 676.3 [673.3–686.4] | 52.3 [52.2–52.5] | 2.98x | 2.92x | 0.92x | 12.9x |
| raytrace3d | 3d | 154.6 [154.1–155.3] | 96.5 [96.4–111.0] | 51.7 [51.6–52.2] | 1.83s [1.83s–1.83s] | 61.8 [61.4–62.1] | 2.50x | 1.56x | 0.84x | 29.7x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 109.3 [108.7–138.8] | 70.9 [70.7–71.1] | 58.6 [58.4–58.6] | 3.22s [3.19s–3.23s] | 83.7 [82.9–84.3] | 1.31x | 0.85x | 0.70x | 38.4x |
| microdiff | data-diff | 64.9 [64.9–65.0] | 71.3 [70.5–71.6] | 51.0 [50.7–51.6] | 1.37s [1.37s–1.40s] | 59.7 [59.6–60.2] | 1.09x | 1.20x | 0.86x | 23.0x |
| hyphen | hyphenation | 98.1 [96.2–99.0] | 75.3 [73.8–75.4] | 75.9 [75.9–77.0] | 419.9 [416.9–420.0] | 57.6 [57.1–58.9] | 1.70x | 1.31x | 1.32x | 7.29x |
| prettier_ast | formatting | 1.34s [1.34s–1.35s] | 3.79s [3.76s–3.82s] | 103.5 [103.2–104.1] | 11.07s [10.98s–11.13s] | 144.7 [144.4–145.8] | 9.27x | 26.2x | 0.72x | 76.5x |
| text_search | search | 1.48s [1.47s–1.51s] | 1.16s [1.15s–1.16s] | 557.6 [556.4–569.6] | 112.12s [112.00s–112.37s] | 822.1 [821.9–824.2] | 1.80x | 1.41x | 0.68x | 136x |
| three_way_merge | merge | 3.17s [3.14s–3.18s] | 2.24s [2.23s–2.25s] | 2.19s [2.18s–2.24s] | 17.24s [17.17s–17.27s] | 1.01s [1.01s–1.01s] | 3.14x | 2.22x | 2.17x | 17.1x |
| log_pipeline | log-processing | 5.61s [5.56s–5.61s] | 1.29s [1.28s–1.30s] | 672.3 [671.5–682.9] | 31.82s [31.50s–31.85s] | 994.2 [988.9–998.4] | 5.64x | 1.29x | 0.68x | 32.0x |
| jq_mix | jq-breadth | 44.82s [44.77s–44.94s] | 44.73s [44.69s–44.95s] | 1.08s [1.08s–1.08s] | --- [600.00s–600.00s] | 4.00s [3.97s–4.10s] | 11.2x | 11.2x | 0.27x | --- |
| jq_records | jq-data | 2.74s [2.70s–2.78s] | 2.65s [2.61s–2.68s] | 951.7 [947.4–967.5] | --- [600.00s–600.00s] | 2.06s [2.04s–2.07s] | 1.33x | 1.28x | 0.46x | --- |
| jq_bf | jq-interpreter | 231.0 [230.4–231.6] | 190.7 [190.7–246.3] | 1.95s [1.92s–1.98s] | --- [600.00s–600.00s] | 2.98s [2.98s–2.99s] | 0.08x | 0.06x | 0.65x | --- |
| jq_tree | jq-paths | 2.07s [2.07s–2.07s] | 1.51s [1.49s–1.54s] | 4.18s [4.11s–4.25s] | --- [600.00s–600.00s] | 5.85s [5.81s–5.89s] | 0.35x | 0.26x | 0.71x | --- |

