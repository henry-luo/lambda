# Lambda Benchmark Results: Result55

- **Date:** 2026-11-10
- **Platform:** Darwin arm64
- **Lambda commit:** `dced2136789c8ed2e164875d3f339c37d4bd5eb6`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v55-dced213678` (30,204,008 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 62.20s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 62.1s (batched 57.6s: sync 50.2s, async 7.4s; non-batched 4.5s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **Julia microbenchmark suite:** every language uses one complete warmup, then one measured workload; formatted-output timing includes synchronous null-sink writes.
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run; suites run in order `r7rs -> awfy -> beng -> kostya -> larceny -> julia -> jetstream -> text` with a 10s idle gap between suites
- **Range notation:** repeated-run timing cells are `median [minimum–maximum]`; single-run cells show the one measured value; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v55.json`
- **Separately measured:** MIR (untyped), MIR (typed) measured on 2026-11-11, 3 run(s) from `test/benchmark/benchmark_results_v55_lambda_tuned.json` on Lambda commit `4e046a786b`. Part 1 JIT execution only: qualified candidate23 release test/benchmark/exe/lambda-v55-4e046a786b-tuned-c23 (SHA-256 f5376e67ccf8b6a67b082d812529ec3e9d89665d367f1be2d033efc25c4bc0e8), built from this base commit plus the recorded tuning patch. Three alternating release pairs against Result54 after one warmup, MIR cache disabled, with exact output checks. Post-run qualification passed Lambda 7,370/7,370 and Test262 40,261/40,261. AUTO end-to-end and other engines retain their original Result55 measurements. Typed r7rs/sumfp reports zero and is marked below timer resolution; positive geomeans exclude it.
- **Separately measured:** MIR (untyped), MIR (typed) measured on 2026-11-11, 7 run(s) from `temp/result55_c2mir_tuning/candidate3-result55-merge-source.json` on Lambda commit `d0ec7dcc0a`. Selected six-cell Lambda refresh from seven paired release runs versus pinned Result55 candidate23; candidate archive SHA-256 b92cd702c385f7b6d196b7d0f66fb25a4e5721daad408a461db8290ba35ceaf5. These cells supersede the candidate23 measurements; other engines and rows retain their original Result55 provenance.
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes 15, hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on the former single-frame Navier workload and hashmap. Navier now times all 15 frames and checks checksum 77; older snapshots that timed one JS frame require a fresh measurement for comparison.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with process startup and prior build steps excluded. Runtime compilation performed during the workload is included. This is the historical series, comparable back through Result18, the MIR columns pin `LAMBDA_EXEC_BACKEND=jit`, and LambdaJS pins `JS_EXEC_BACKEND=mir`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 9 | 10 | 10 | 10 | 0.14x | 0.19x | 0.21x | 0.26x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 0.82x | 0.38x | 0.12x | 2.59x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 0.17x | 0.14x | 0.10x | 1.49x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 0.54x | 0.36x | 0.22x | 0.51x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 0.50x | 0.38x | 0.32x | 0.74x |
| Julia microbenchmarks | 4 | 4 | 4 | 4 | 4 | 4 | 1.18x | 0.95x | 0.32x | 2.08x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 2.46x | 1.39x | 0.23x | 4.95x |
| Text | 11 | 11 | 11 | 11 | 11 | 11 | 1.09x | 0.63x | 0.47x | 12.4x |
| **Overall** | 71 | 71 | 70 | 71 | 71 | 71 | 0.56x | 0.39x | 0.22x | 1.64x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 67 | 0.36x | 0.50x | 1.76x | 67 |
| complete current suite | 71 | 0.39x | 0.56x | 1.78x | 71 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 1.78x over 71 of 71 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| kostya/base64 | 9.46 | 0.566 | 16.7x |
| awfy/deltablue | 18.4 | 1.20 | 15.4x |
| jetstream/splay | 281.6 | 19.0 | 14.8x |
| awfy/havlak | 465.9 | 38.6 | 12.1x |
| awfy/cd | 172.1 | 14.9 | 11.5x |
| text/microdiff | 28.1 | 2.63 | 10.7x |
| jetstream/crypto_sha1 | 27.8 | 2.62 | 10.6x |
| julia/parse_integers | 23.5 | 2.26 | 10.4x |
| text/prettier_ast | 411.1 | 41.0 | 10.0x |
| jetstream/cube3d | 4.10 | 0.462 | 8.88x |
| beng/pidigits | 0.351 | 0.045 | 7.77x |
| larceny/triangl | 395.8 | 57.2 | 6.92x |

---

### Notable Results

- Missing timings: **0** cells

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| text/jq_bf | 288.58s | 2.93s | 98.4x |
| beng/spectralnorm | 232.6 | 2.54 | 91.5x |
| text/jq_tree | 466.87s | 5.71s | 81.8x |
| text/jq_mix | 287.73s | 3.95s | 72.8x |
| text/jq_records | 68.28s | 2.01s | 33.9x |
| awfy/havlak | 2.60s | 93.1 | 27.9x |
| awfy/nbody | 128.5 | 5.47 | 23.5x |
| jetstream/hashmap | 339.0 | 15.2 | 22.3x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| r7rs/sumfp | 0.054 | 0.871 | 0.06x |
| larceny/ray | 0.227 | 3.53 | 0.06x |
| r7rs/fft | 0.150 | 1.53 | 0.10x |
| larceny/paraffins | 0.097 | 0.973 | 0.10x |
| r7rs/tak | 0.091 | 0.780 | 0.12x |
| beng/fasta | 0.871 | 6.11 | 0.14x |
| kostya/collatz | 228.1 | 1.42s | 0.16x |
| r7rs/cpstak | 0.178 | 0.976 | 0.18x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 0.708 [0.660–0.729] | 0.706 [0.672–0.712] | 1.14 [1.08–1.15] | 0.866 [0.861–0.881] | 1.82 [1.74–1.83] | 0.39x | 0.39x | 0.62x | 0.47x |
| fibfp | recursive | 0.603 [0.596–0.605] | 0.556 [0.553–0.562] | 1.12 [1.11–1.12] | 0.850 [0.849–0.855] | 1.74 [1.73–1.81] | 0.35x | 0.32x | 0.65x | 0.49x |
| tak | recursive | 0.083 [0.077–0.085] | 0.084 [0.082–0.087] | 0.109 [0.108–0.117] | 0.091 [0.088–0.091] | 0.780 [0.769–0.793] | 0.11x | 0.11x | 0.14x | 0.12x |
| cpstak | closure | 0.164 [0.152–0.164] | 0.163 [0.154–0.163] | 0.234 [0.214–0.235] | 0.178 [0.174–0.179] | 0.976 [0.972–1.02] | 0.17x | 0.17x | 0.24x | 0.18x |
| sum | iterative | 0.099 [0.098–0.099] | 0.098 [0.094–0.099] | 0.269 [0.267–0.282] | 0.545 [0.538–0.552] | 1.18 [1.18–1.21] | 0.08x | 0.08x | 0.23x | 0.46x |
| sumfp | iterative | 0.002 [0.002–0.002] | 0.000 [0.000–0.000] | 0.079 [0.076–0.079] | 0.054 [0.054–0.054] | 0.871 [0.864–0.885] | 0.002x | 0.000x | 0.09x | 0.06x |
| nqueens | backtrack | 0.799 [0.797–0.888] | 0.628 [0.618–0.631] | 0.361 [0.361–0.365] | 0.515 [0.510–0.519] | 1.71 [1.71–1.78] | 0.47x | 0.37x | 0.21x | 0.30x |
| fft | numeric | 0.216 [0.201–0.217] | 0.040 [0.040–0.042] | 0.024 [0.023–0.024] | 0.150 [0.150–0.154] | 1.53 [1.53–1.54] | 0.14x | 0.03x | 0.02x | 0.10x |
| mbrot | numeric | 0.491 [0.481–0.501] | 0.498 [0.487–0.504] | 0.470 [0.444–0.470] | 1.37 [1.35–1.38] | 1.83 [1.83–30.3] | 0.27x | 0.27x | 0.26x | 0.75x |
| ack | recursive | 5.14 [5.14–5.21] | 7.05 [5.24–20.1] | 11.5 [11.4–11.7] | 6.43 [6.42–6.44] | 13.3 [13.3–13.4] | 0.39x | 0.53x | 0.86x | 0.48x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.014 [0.014–0.014] | 0.014 [0.013–0.015] | 0.014 [0.014–0.019] | 0.083 [0.083–0.084] | 0.379 [0.375–0.381] | 0.04x | 0.04x | 0.04x | 0.22x |
| permute | micro | 0.152 [0.141–0.154] | 0.080 [0.080–0.080] | 0.025 [0.025–0.026] | 1.01 [0.990–1.02] | 0.811 [0.803–0.816] | 0.19x | 0.10x | 0.03x | 1.24x |
| queens | micro | 0.161 [0.159–0.167] | 0.022 [0.022–0.025] | 0.020 [0.019–0.020] | 0.474 [0.473–0.474] | 0.645 [0.643–0.645] | 0.25x | 0.03x | 0.03x | 0.73x |
| towers | micro | 0.321 [0.319–0.343] | 0.174 [0.173–0.184] | 0.027 [0.027–0.028] | 2.10 [2.09–2.10] | 1.11 [1.10–1.11] | 0.29x | 0.16x | 0.02x | 1.90x |
| bounce | micro | 0.055 [0.055–0.055] | 0.031 [0.029–0.031] | 0.025 [0.024–0.025] | 0.738 [0.708–0.751] | 0.535 [0.531–0.537] | 0.10x | 0.06x | 0.05x | 1.38x |
| list | micro | 0.202 [0.199–0.203] | 0.087 [0.086–0.089] | 0.021 [0.021–0.022] | 0.498 [0.488–0.525] | 0.485 [0.476–0.486] | 0.42x | 0.18x | 0.04x | 1.03x |
| storage | micro | 0.330 [0.326–0.332] | 0.306 [0.304–0.313] | 0.097 [0.092–0.097] | 0.612 [0.595–0.613] | 0.625 [0.621–0.634] | 0.53x | 0.49x | 0.16x | 0.98x |
| mandelbrot | compute | 28.8 [28.8–28.8] | 29.4 [28.8–56.9] | 30.5 [30.5–30.6] | 33.6 [33.4–34.2] | 31.0 [31.0–31.0] | 0.93x | 0.95x | 0.98x | 1.08x |
| nbody | compute | 3.04 [2.99–3.14] | 1.72 [1.70–1.75] | 1.49 [1.48–1.50] | 128.5 [127.0–128.8] | 5.47 [5.28–5.50] | 0.56x | 0.31x | 0.27x | 23.5x |
| richards | macro | 256.6 [255.2–272.2] | 98.3 [98.3–99.2] | 29.2 [29.0–29.2] | 230.1 [229.6–230.9] | 47.2 [47.1–47.9] | 5.43x | 2.08x | 0.62x | 4.87x |
| json | macro | 3.45 [3.36–3.46] | 1.42 [1.41–1.43] | 0.263 [0.252–0.266] | 10.0 [9.87–10.3] | 2.74 [2.65–2.90] | 1.26x | 0.52x | 0.10x | 3.65x |
| deltablue | macro | 38.8 [38.5–39.4] | 18.4 [18.2–18.6] | 1.20 [1.18–1.22] | 96.4 [94.7–128.3] | 11.8 [11.5–11.8] | 3.30x | 1.57x | 0.10x | 8.20x |
| havlak | macro | 4.78s [4.74s–4.94s] | 465.9 [453.2–499.2] | 38.6 [38.5–39.0] | 2.60s [2.48s–2.60s] | 93.1 [92.9–93.3] | 51.3x | 5.01x | 0.41x | 27.9x |
| cd | macro | 346.2 [345.9–346.5] | 172.1 [172.0–173.0] | 14.9 [14.8–15.0] | 404.5 [402.7–406.5] | 35.6 [35.4–36.0] | 9.71x | 4.83x | 0.42x | 11.4x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 3.45 [3.44–3.46] | 2.18 [2.18–2.20] | 3.00 [2.98–3.01] | 3.42 [3.38–3.52] | 3.91 [3.78–4.01] | 0.88x | 0.56x | 0.77x | 0.87x |
| fannkuch | permutation | 0.366 [0.365–0.372] | 0.283 [0.282–0.289] | 0.140 [0.139–0.147] | 42.7 [41.9–42.9] | 3.85 [3.84–3.87] | 0.09x | 0.07x | 0.04x | 11.1x |
| fasta | generation | 0.489 [0.486–0.497] | 0.398 [0.395–0.402] | 0.239 [0.239–0.241] | 0.871 [0.863–0.877] | 6.11 [5.98–6.32] | 0.08x | 0.07x | 0.04x | 0.14x |
| knucleotide | hashing | 0.416 [0.415–0.419] | 0.312 [0.305–0.313] | 0.282 [0.277–0.286] | 3.18 [3.15–3.19] | 4.78 [4.73–4.80] | 0.09x | 0.07x | 0.06x | 0.67x |
| pidigits | bignum | 0.354 [0.344–0.373] | 0.351 [0.340–0.351] | 0.045 [0.044–0.048] | 0.332 [0.330–0.333] | 1.80 [1.78–3.74] | 0.20x | 0.19x | 0.03x | 0.18x |
| regexredux | regex | 0.761 [0.730–0.770] | 0.734 [0.732–0.745] | 1.13 [1.13–1.13] | 3.14 [3.00–3.21] | 2.29 [2.28–2.35] | 0.33x | 0.32x | 0.49x | 1.37x |
| revcomp | string | 0.613 [0.601–0.646] | 0.457 [0.452–0.458] | 0.372 [0.371–0.377] | 3.80 [3.75–4.12] | 3.30 [3.22–3.32] | 0.19x | 0.14x | 0.11x | 1.15x |
| spectralnorm | numeric | 0.260 [0.259–0.262] | 0.235 [0.231–0.237] | 0.349 [0.348–0.349] | 232.6 [229.6–267.6] | 2.54 [2.50–2.57] | 0.10x | 0.09x | 0.14x | 91.5x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 84.6 [84.5–85.1] | 76.3 [75.8–76.4] | 28.1 [26.9–29.0] | 41.3 [41.1–42.1] | 33.4 [33.1–33.8] | 2.53x | 2.28x | 0.84x | 1.24x |
| matmul | numeric | 5.16 [5.16–5.17] | 5.12 [5.12–5.22] | 6.05 [6.03–6.07] | 7.10 [7.06–7.13] | 15.5 [15.4–16.3] | 0.33x | 0.33x | 0.39x | 0.46x |
| primes | numeric | 1.83 [1.82–1.85] | 0.909 [0.907–0.912] | 1.55 [1.53–1.56] | 2.77 [2.76–2.79] | 4.29 [4.26–4.43] | 0.43x | 0.21x | 0.36x | 0.65x |
| base64 | string | 7.31 [7.29–7.37] | 9.46 [9.39–9.49] | 0.566 [0.565–0.568] | 9.80 [9.80–9.81] | 17.3 [17.1–17.4] | 0.42x | 0.55x | 0.03x | 0.57x |
| levenshtein | string | 4.10 [4.10–4.17] | 1.21 [1.15–1.21] | 0.765 [0.763–0.794] | 1.57 [1.53–1.57] | 3.97 [3.92–3.97] | 1.03x | 0.31x | 0.19x | 0.40x |
| json_gen | data | 4.44 [4.44–4.45] | 1.56 [1.54–1.58] | 1.45 [1.45–1.46] | 4.00 [3.97–4.05] | 6.15 [6.10–6.16] | 0.72x | 0.25x | 0.24x | 0.65x |
| collatz | numeric | 156.6 [155.7–157.1] | 151.8 [151.2–153.1] | 222.5 [222.3–224.2] | 228.1 [226.3–228.4] | 1.42s [1.41s–1.46s] | 0.11x | 0.11x | 0.16x | 0.16x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 647.5 [645.4–712.8] | 395.8 [395.1–395.9] | 57.2 [57.2–57.2] | 127.7 [127.0–127.9] | 68.8 [68.7–69.5] | 9.41x | 5.75x | 0.83x | 1.86x |
| array1 | array | 0.118 [0.118–0.125] | 0.117 [0.116–0.117] | 0.301 [0.295–0.304] | 0.544 [0.544–0.546] | 1.80 [1.80–1.80] | 0.07x | 0.06x | 0.17x | 0.30x |
| deriv | symbolic | 16.5 [16.5–16.7] | 5.43 [5.36–5.45] | 2.86 [2.83–2.88] | 13.8 [13.8–14.0] | 3.74 [3.72–3.77] | 4.42x | 1.45x | 0.77x | 3.70x |
| diviter | iterative | 224.8 [221.3–259.4] | 221.0 [220.6–222.7] | 250.2 [248.2–250.8] | 506.1 [504.1–510.7] | 468.2 [466.2–479.8] | 0.48x | 0.47x | 0.53x | 1.08x |
| divrec | recursive | 0.949 [0.935–0.951] | 0.944 [0.934–0.972] | 4.90 [4.89–4.94] | 4.75 [4.74–4.76] | 7.54 [7.50–7.64] | 0.13x | 0.13x | 0.65x | 0.63x |
| gcbench | allocation | 96.3 [96.3–96.7] | 65.9 [65.8–66.1] | 70.7 [70.4–71.1] | 98.0 [97.8–98.3] | 23.2 [23.0–23.3] | 4.15x | 2.84x | 3.05x | 4.22x |
| paraffins | combinat | 0.240 [0.236–0.242] | 0.095 [0.094–0.096] | 0.047 [0.047–0.048] | 0.097 [0.097–0.097] | 0.973 [0.971–0.987] | 0.25x | 0.10x | 0.05x | 0.10x |
| pnpoly | numeric | 2.01 [2.01–2.04] | 1.96 [1.91–1.97] | 1.94 [1.91–2.00] | 10.6 [10.5–10.7] | 5.84 [5.78–5.99] | 0.34x | 0.34x | 0.33x | 1.82x |
| puzzle | search | 4.25 [4.22–4.25] | 4.21 [4.20–4.29] | 1.20 [1.15–1.21] | 2.33 [2.33–2.37] | 3.29 [3.28–3.31] | 1.29x | 1.28x | 0.36x | 0.71x |
| quicksort | sorting | 0.274 [0.273–0.276] | 0.265 [0.264–0.266] | 0.220 [0.219–0.222] | 1.23 [1.23–1.23] | 1.66 [1.65–1.66] | 0.17x | 0.16x | 0.13x | 0.74x |
| ray | numeric | 0.130 [0.127–0.136] | 0.130 [0.127–0.134] | 0.173 [0.168–0.180] | 0.227 [0.227–0.227] | 3.53 [3.50–3.62] | 0.04x | 0.04x | 0.05x | 0.06x |

### Julia microbenchmarks

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| parse_integers | conversion | 26.1 [26.1–26.6] | 23.5 [22.8–23.6] | 2.26 [2.25–2.31] | 45.4 [45.3–47.4] | 13.0 [13.0–13.1] | 2.01x | 1.81x | 0.17x | 3.50x |
| matrix_statistics | numeric | 17.5 [17.3–17.5] | 12.6 [12.6–12.7] | 8.98 [8.96–9.02] | 66.7 [66.6–66.7] | 21.2 [20.9–21.4] | 0.83x | 0.60x | 0.42x | 3.15x |
| iteration_pi_sum | numeric | 3.36 [3.35–3.37] | 3.47 [3.46–3.48] | 2.99 [2.99–2.99] | 3.22 [3.21–3.25] | 4.87 [4.80–4.91] | 0.69x | 0.71x | 0.61x | 0.66x |
| formatted_output | io | 61.5 [61.2–61.7] | 38.9 [38.7–39.7] | 8.02 [8.01–8.06] | 94.1 [93.6–94.1] | 36.2 [36.2–36.3] | 1.70x | 1.07x | 0.22x | 2.60x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 13.3 [13.0–13.3] | 4.10 [4.10–4.11] | 0.462 [0.461–0.464] | 67.6 [67.6–67.6] | 17.5 [17.5–17.6] | 0.76x | 0.23x | 0.03x | 3.86x |
| navier_stokes | numeric | 96.1 [95.2–96.2] | 129.8 [128.1–130.0] | 45.3 [45.3–45.3] | 420.6 [420.0–420.8] | 57.5 [57.3–57.6] | 1.67x | 2.26x | 0.79x | 7.32x |
| splay | data | 240.3 [239.8–241.4] | 281.6 [280.2–282.0] | 19.0 [18.9–19.1] | 53.2 [52.0–53.2] | 18.8 [18.0–19.0] | 12.8x | 14.9x | 1.01x | 2.82x |
| hashmap | data | 44.2 [44.1–44.3] | 12.4 [12.3–12.5] | 2.75 [2.69–2.79] | 339.0 [338.3–341.8] | 15.2 [15.2–15.3] | 2.91x | 0.81x | 0.18x | 22.3x |
| crypto_sha1 | crypto | 26.8 [26.6–27.2] | 27.8 [27.7–28.0] | 2.62 [2.57–2.79] | 10.6 [10.5–10.7] | 8.63 [8.56–8.66] | 3.11x | 3.23x | 0.30x | 1.23x |
| raytrace3d | 3d | 27.7 [27.6–27.7] | 6.34 [6.33–6.38] | 2.14 [2.13–2.14] | 121.6 [121.2–122.3] | 18.1 [18.0–18.4] | 1.53x | 0.35x | 0.12x | 6.73x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 43.5 [43.3–43.5] | 46.2 [46.1–46.5] | 12.7 [12.6–12.7] | 170.6 [170.5–171.3] | 40.8 [39.5–75.3] | 1.07x | 1.13x | 0.31x | 4.18x |
| microdiff | data-diff | 29.2 [29.0–29.4] | 28.1 [28.1–28.3] | 2.63 [2.58–2.67] | 115.0 [115.0–115.4] | 16.3 [16.2–16.3] | 1.80x | 1.73x | 0.16x | 7.08x |
| hyphen | hyphenation | 40.9 [40.0–41.0] | 7.66 [7.64–7.87] | 1.50 [1.46–1.56] | 54.0 [53.1–54.1] | 6.65 [6.63–6.74] | 6.16x | 1.15x | 0.23x | 8.12x |
| prettier_ast | formatting | 590.5 [586.3–592.5] | 411.1 [398.7–417.2] | 41.0 [40.9–41.2] | 538.0 [537.8–539.2] | 99.2 [98.8–99.8] | 5.95x | 4.15x | 0.41x | 5.42x |
| text_search | search | 4.57s [4.57s–4.58s] | 1.71s [1.70s–1.74s] | 510.0 [509.5–510.0] | 4.53s [4.52s–4.55s] | 777.7 [776.4–778.2] | 5.88x | 2.20x | 0.66x | 5.83x |
| three_way_merge | merge | 1.71s [1.68s–1.72s] | 1.46s [1.45s–1.46s] | 2.14s [2.13s–2.15s] | 1.44s [1.39s–1.48s] | 965.5 [962.3–965.7] | 1.77x | 1.51x | 2.21x | 1.49x |
| log_pipeline | log-processing | 3.45s [3.44s–3.55s] | 595.2 [594.7–597.2] | 625.5 [623.3–635.6] | 4.63s [4.62s–4.68s] | 940.8 [937.0–942.4] | 3.67x | 0.63x | 0.66x | 4.93x |
| jq_mix | jq-breadth | 2.01s [1.98s–2.06s] | 1.89s [1.88s–1.89s] | 986.1 [980.4–1.01s] / λ-VM --- | 287.73s [286.54s–289.27s] | 3.95s [3.91s–3.97s] | 0.51x | 0.48x | 0.25x | 72.8x |
| jq_records | jq-data | 312.5 [307.5–313.5] | 344.8 [340.4–345.4] | 907.2 [871.6–1.07s] / λ-VM --- | 68.28s [68.19s–68.34s] | 2.01s [2.00s–2.02s] | 0.16x | 0.17x | 0.45x | 33.9x |
| jq_bf | jq-interpreter | 140.7 [139.9–141.8] | 42.0 [41.9–42.5] | 1.84s [1.83s–1.85s] / λ-VM --- | 288.58s [288.36s–291.64s] | 2.93s [2.90s–2.95s] | 0.05x | 0.01x | 0.63x | 98.4x |
| jq_tree | jq-paths | 1.47s [1.47s–1.49s] | 1.55s [1.54s–1.55s] | 4.17s [4.07s–4.45s] / λ-VM --- | 466.87s [461.94s–471.69s] | 5.71s [5.70s–5.76s] | 0.26x | 0.27x | 0.73x | 81.8x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, including **startup and compilation performed inside that process**. Build steps completed before invocation are excluded. The MIR and LambdaJS columns use the shipped auto tier -- no `LAMBDA_EXEC_BACKEND` or `JS_EXEC_BACKEND` override -- which is what `lambda.exe run script.ls` and `lambda.exe js script.js` actually do.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Process timing includes each engine's recorded warmup policy, workload and verification; separately added measurements are described above.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. The MIR and LambdaJS columns are re-run, because part 1 pins the compiled lane and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS (default) | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS (default)/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 0.51x | 0.40x | 1.00x | 1.10x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 1.69x | 1.17x | 0.95x | 4.68x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 0.55x | 0.47x | 1.02x | 1.15x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 2.31x | 1.25x | 0.72x | 7.99x |
| LARCENY | 11 | 10 | 10 | 11 | 11 | 11 | 2.05x | 0.80x | 0.99x | 4.30x |
| Julia microbenchmarks | 4 | 4 | 4 | 4 | 4 | 4 | 2.93x | 1.36x | 1.25x | 11.8x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 3.49x | 4.22x | 0.83x | 30.3x |
| Text | 11 | 10 | 10 | 11 | 11 | 11 | 1.38x | 0.90x | 0.74x | 31.4x |
| **Overall** | 71 | 69 | 69 | 71 | 71 | 71 | 1.41x | 0.93x | 0.91x | 5.62x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 18.2 [18.1–18.3] | 16.2 [16.2–16.4] | 47.3 [46.7–456.1] | 69.5 [69.3–71.8] | 47.5 [46.0–48.2] | 0.38x | 0.34x | 1.00x | 1.46x |
| fibfp | recursive | 19.8 [19.8–20.0] | 16.6 [16.5–16.8] | 46.0 [45.6–46.0] | 68.2 [67.8–69.3] | 44.3 [44.3–44.8] | 0.45x | 0.37x | 1.04x | 1.54x |
| tak | recursive | 14.6 [14.4–14.8] | 13.6 [13.5–13.6] | 45.0 [44.9–46.9] | 25.9 [25.4–26.1] | 43.7 [43.6–44.1] | 0.33x | 0.31x | 1.03x | 0.59x |
| cpstak | closure | 15.3 [15.2–15.5] | 13.9 [13.9–14.0] | 46.1 [45.8–46.1] | 31.1 [30.9–31.1] | 44.2 [43.4–44.2] | 0.35x | 0.32x | 1.04x | 0.70x |
| sum | iterative | 21.3 [20.8–21.3] | 12.4 [12.4–12.8] | 44.9 [44.8–69.7] | 24.8 [24.6–24.9] | 44.5 [44.0–44.9] | 0.48x | 0.28x | 1.01x | 0.56x |
| sumfp | iterative | 15.9 [15.7–16.0] | 13.5 [13.2–13.8] | 44.3 [44.1–45.0] | 20.3 [19.7–20.5] | 44.0 [43.7–44.2] | 0.36x | 0.31x | 1.01x | 0.46x |
| nqueens | backtrack | 51.0 [50.8–51.8] | 28.7 [28.5–28.7] | 46.2 [46.1–47.1] | 81.4 [81.1–81.4] | 45.0 [44.9–45.5] | 1.13x | 0.64x | 1.02x | 1.81x |
| fft | numeric | 28.7 [28.7–28.7] | 33.0 [32.5–33.0] | 45.2 [44.8–45.5] | 80.8 [80.6–81.0] | 44.8 [44.6–44.9] | 0.64x | 0.74x | 1.01x | 1.81x |
| mbrot | numeric | 31.4 [31.4–31.5] | 15.9 [15.6–16.3] | 45.3 [45.2–45.4] | 33.2 [33.1–33.2] | 50.2 [44.9–75.8] | 0.63x | 0.32x | 0.90x | 0.66x |
| ack | recursive | 42.5 [41.7–43.0] | 38.9 [37.4–38.9] | 57.0 [56.9–57.2] | 274.3 [274.0–305.4] | 57.1 [56.8–90.1] | 0.74x | 0.68x | 1.00x | 4.80x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 14.2 [14.2–14.9] | 13.5 [13.5–13.7] | 44.6 [44.0–44.8] | 20.3 [20.3–20.4] | 44.3 [44.0–46.3] | 0.32x | 0.31x | 1.01x | 0.46x |
| permute | micro | 20.2 [20.0–20.5] | 15.9 [15.8–16.0] | 44.8 [44.5–45.0] | 35.9 [35.7–37.4] | 44.8 [44.4–45.2] | 0.45x | 0.36x | 1.00x | 0.80x |
| queens | micro | 22.9 [22.6–23.4] | 19.3 [19.2–19.4] | 45.2 [45.1–45.2] | 24.4 [24.0–24.8] | 44.4 [44.2–45.1] | 0.52x | 0.43x | 1.02x | 0.55x |
| towers | micro | 24.3 [24.2–27.2] | 23.0 [22.9–23.0] | 45.2 [45.2–45.4] | 49.6 [49.2–75.2] | 45.3 [45.0–45.9] | 0.54x | 0.51x | 1.00x | 1.10x |
| bounce | micro | 15.7 [15.4–16.4] | 15.8 [15.5–16.2] | 45.2 [45.2–45.3] | 26.9 [26.5–27.1] | 44.6 [44.0–45.6] | 0.35x | 0.35x | 1.01x | 0.60x |
| list | micro | 17.5 [17.2–17.6] | 16.0 [15.9–16.1] | 44.8 [44.7–45.4] | 27.6 [27.1–27.6] | 43.9 [43.8–44.7] | 0.40x | 0.36x | 1.02x | 0.63x |
| storage | micro | 17.7 [17.4–17.7] | 16.9 [16.7–17.0] | 44.9 [44.4–45.2] | 31.6 [31.5–31.7] | 44.8 [44.4–45.6] | 0.39x | 0.38x | 1.00x | 0.70x |
| mandelbrot | compute | 297.0 [296.7–325.0] | 52.9 [52.3–53.2] | 75.5 [75.2–75.8] | 7.55s [7.54s–7.72s] | 74.7 [74.6–76.1] | 3.97x | 0.71x | 1.01x | 101x |
| nbody | compute | 347.4 [342.1–353.4] | 134.1 [133.9–168.0] | 48.5 [48.5–49.4] | 1.75s [1.74s–1.76s] | 50.3 [50.2–51.0] | 6.90x | 2.66x | 0.96x | 34.7x |
| richards | macro | 535.6 [532.4–541.9] | 392.3 [391.7–393.3] | 77.6 [77.3–77.9] | 2.19s [2.19s–2.23s] | 92.5 [92.0–92.7] | 5.79x | 4.24x | 0.84x | 23.7x |
| json | macro | 48.4 [48.4–49.3] | 44.3 [43.4–44.6] | 52.3 [52.2–52.4] | 108.6 [101.5–133.7] | 49.4 [48.9–49.4] | 0.98x | 0.90x | 1.06x | 2.20x |
| deltablue | macro | 403.8 [400.4–452.5] | 328.5 [297.8–329.8] | 54.2 [53.4–54.3] | 897.3 [894.2–911.8] | 57.6 [57.5–58.2] | 7.01x | 5.70x | 0.94x | 15.6x |
| havlak | macro | 8.75s [8.74s–8.82s] | 3.32s [3.22s–3.36s] | 96.6 [90.6–100.4] | 25.51s [25.45s–25.59s] | 140.9 [139.9–141.3] | 62.1x | 23.6x | 0.69x | 181x |
| cd | macro | 860.5 [858.1–863.9] | 659.4 [652.1–701.9] | 67.7 [67.1–68.1] | 6.51s [6.50s–6.52s] | 80.9 [80.1–81.5] | 10.6x | 8.15x | 0.84x | 80.5x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 22.0 [22.0–22.5] | 19.4 [18.7–19.7] | 48.3 [48.2–49.1] | 62.2 [62.1–63.1] | 45.6 [45.0–45.7] | 0.48x | 0.43x | 1.06x | 1.37x |
| fannkuch | permutation | 64.0 [63.3–64.6] | 35.2 [34.9–35.4] | 45.5 [45.2–45.6] | 100.2 [99.8–100.3] | 45.3 [45.2–45.6] | 1.41x | 0.78x | 1.00x | 2.21x |
| fasta | generation | 22.0 [21.8–22.3] | 22.5 [22.4–22.5] | 45.1 [45.0–45.8] | 47.4 [47.0–47.6] | 48.5 [48.2–48.6] | 0.45x | 0.46x | 0.93x | 0.98x |
| knucleotide | hashing | 31.1 [30.9–31.1] | 23.7 [23.4–23.9] | 46.3 [46.3–46.4] | 47.1 [46.6–47.6] | 46.6 [46.3–46.7] | 0.67x | 0.51x | 0.99x | 1.01x |
| pidigits | bignum | 12.8 [12.7–13.1] | 12.0 [11.6–12.3] | 47.2 [47.2–77.1] | 19.6 [19.4–20.1] | 43.2 [43.1–45.3] | 0.30x | 0.28x | 1.09x | 0.45x |
| regexredux | regex | 11.8 [11.5–12.1] | 12.5 [12.2–12.6] | 47.8 [47.6–48.7] | 19.0 [18.9–19.4] | 43.9 [43.8–44.0] | 0.27x | 0.29x | 1.09x | 0.43x |
| revcomp | string | 15.8 [15.6–16.4] | 16.3 [16.1–16.6] | 45.7 [45.4–45.8] | 36.7 [36.0–37.0] | 44.7 [44.6–44.8] | 0.35x | 0.37x | 1.02x | 0.82x |
| spectralnorm | numeric | 67.4 [67.3–68.2] | 50.2 [50.2–50.7] | 46.2 [45.8–46.4] | 299.9 [298.0–324.8] | 45.7 [45.7–46.2] | 1.47x | 1.10x | 1.01x | 6.56x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 338.2 [337.4–339.2] | 274.6 [274.1–300.7] | 73.6 [73.6–74.8] | 2.24s [2.24s–2.27s] | 77.5 [76.8–77.5] | 4.37x | 3.55x | 0.95x | 28.9x |
| matmul | numeric | 148.2 [148.1–148.4] | 92.4 [92.1–92.8] | 51.1 [51.1–51.6] | 4.91s [4.89s–4.93s] | 59.1 [58.7–59.7] | 2.51x | 1.56x | 0.86x | 83.2x |
| primes | numeric | 132.8 [132.6–134.3] | 85.0 [85.0–119.6] | 46.4 [46.3–46.6] | 538.4 [529.3–540.5] | 47.0 [47.0–47.5] | 2.82x | 1.81x | 0.99x | 11.4x |
| base64 | string | 194.0 [193.5–194.3] | 59.7 [59.2–59.8] | 45.8 [45.7–45.8] | 624.2 [620.1–624.9] | 60.7 [60.2–61.3] | 3.20x | 0.98x | 0.76x | 10.3x |
| levenshtein | string | 140.1 [139.8–168.9] | 105.2 [105.1–106.1] | 46.4 [45.8–47.5] | 577.4 [577.3–579.7] | 47.4 [47.3–47.5] | 2.96x | 2.22x | 0.98x | 12.2x |
| json_gen | data | 78.5 [77.7–79.0] | 65.2 [65.0–65.2] | 46.2 [46.2–46.9] | 88.6 [88.1–88.8] | 49.7 [49.4–49.8] | 1.58x | 1.31x | 0.93x | 1.78x |
| collatz | numeric | 1.11s [1.11s–1.16s] | 236.2 [235.4–236.8] | 268.3 [268.1–269.9] | 491.3 [489.8–505.4] | 1.46s [1.46s–1.50s] | 0.76x | 0.16x | 0.18x | 0.34x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 703.6 [700.8–704.3] | 603.7 [603.1–603.7] | 102.6 [101.9–103.6] | 15.84s [15.82s–16.05s] | 112.3 [112.0–112.9] | 6.27x | 5.38x | 0.91x | 141x |
| array1 | array | 169.5 [168.5–170.7] | 162.0 [161.6–162.9] | 45.3 [45.3–45.4] | 317.0 [315.3–319.2] | 44.6 [44.5–45.1] | 3.80x | 3.63x | 1.02x | 7.10x |
| deriv | symbolic | 38.8 [38.5–38.8] | 28.5 [28.4–28.9] | 48.8 [48.4–49.0] | 151.4 [149.9–180.8] | 47.6 [47.6–47.8] | 0.81x | 0.60x | 1.03x | 3.18x |
| diviter | iterative | 3.87s [3.87s–3.88s] | 12.3 [12.1–12.7] | 295.1 [293.4–295.5] | 636.5 [635.5–636.8] | 512.8 [510.6–523.6] | 7.55x | 0.02x | 0.58x | 1.24x |
| divrec | recursive | 26.8 [26.4–26.8] | 24.1 [24.1–24.2] | 49.9 [49.8–49.9] | 128.0 [126.8–129.1] | 50.5 [50.3–50.6] | 0.53x | 0.48x | 0.99x | 2.54x |
| gcbench | allocation | 212.4 [211.5–245.6] | 122.3 [121.3–122.7] | 116.1 [115.6–116.8] | 886.7 [882.9–888.6] | 65.9 [65.6–66.0] | 3.22x | 1.86x | 1.76x | 13.5x |
| paraffins | combinat | 84.3 [82.7–85.0] | 39.1 [38.7–39.3] | 45.9 [45.4–46.6] | 86.5 [85.8–86.7] | 44.3 [44.1–44.4] | 1.90x | 0.88x | 1.04x | 1.95x |
| pnpoly | numeric | 106.3 [105.9–107.0] | 43.4 [42.9–43.5] | 46.8 [46.8–47.1] | 108.9 [107.9–109.8] | 49.1 [48.8–49.2] | 2.17x | 0.88x | 0.95x | 2.22x |
| puzzle | search | --- [46.1–46.5] | --- [22.8–23.7] | 46.4 [45.9–46.6] | 54.1 [53.6–54.2] | 46.5 [46.5–46.5] | --- | --- | 1.00x | 1.16x |
| quicksort | sorting | 45.0 [43.4–45.6] | 29.0 [28.6–29.1] | 45.9 [45.2–46.1] | 319.7 [316.2–344.1] | 45.1 [44.8–45.3] | 1.00x | 0.64x | 1.02x | 7.09x |
| ray | numeric | 59.8 [59.7–60.1] | 39.8 [39.8–40.1] | 45.3 [45.2–45.5] | 91.0 [90.9–92.2] | 46.8 [46.8–46.8] | 1.28x | 0.85x | 0.97x | 1.94x |

### Julia microbenchmarks

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| parse_integers | conversion | 202.5 [201.9–202.7] | 92.7 [91.7–92.8] | 94.4 [94.0–95.4] | 3.16s [3.16s–3.21s] | 72.1 [72.1–72.2] | 2.81x | 1.28x | 1.31x | 43.8x |
| matrix_statistics | numeric | 899.3 [897.8–935.6] | 358.3 [342.8–359.7] | 109.1 [108.8–111.3] | 1.30s [1.30s–1.32s] | 92.3 [92.1–93.5] | 9.74x | 3.88x | 1.18x | 14.1x |
| iteration_pi_sum | numeric | 62.7 [61.0–64.7] | 34.6 [34.5–34.8] | 96.4 [96.0–97.3] | 50.3 [50.2–50.7] | 53.6 [53.0–53.8] | 1.17x | 0.65x | 1.80x | 0.94x |
| formatted_output | io | 274.6 [274.4–276.2] | 127.2 [126.7–165.3] | 106.1 [105.7–106.4] | 4.04s [4.02s–4.14s] | 119.3 [118.5–119.4] | 2.30x | 1.07x | 0.89x | 33.9x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 104.4 [103.8–105.0] | 286.2 [285.6–288.2] | 48.6 [48.4–60.9] | 1.72s [1.72s–1.73s] | 62.2 [62.2–62.2] | 1.68x | 4.61x | 0.78x | 27.7x |
| navier_stokes | numeric | 327.7 [327.5–331.0] | 356.2 [355.2–356.4] | 92.5 [92.4–92.8] | 14.48s [14.41s–14.49s] | 102.7 [102.2–103.0] | 3.19x | 3.47x | 0.90x | 141x |
| splay | data | 383.9 [383.6–385.4] | 801.9 [790.6–806.9] | 65.7 [65.6–66.3] | 1.23s [1.23s–1.23s] | 91.0 [90.0–91.5] | 4.22x | 8.81x | 0.72x | 13.5x |
| hashmap | data | 195.2 [194.4–195.5] | 151.5 [151.0–152.4] | 47.4 [47.2–47.6] | 4.07s [4.06s–4.08s] | 58.6 [58.5–58.8] | 3.33x | 2.58x | 0.81x | 69.5x |
| crypto_sha1 | crypto | 286.0 [285.5–288.3] | 254.1 [253.9–255.1] | 47.7 [47.7–48.7] | 524.4 [516.3–525.7] | 51.6 [51.5–51.7] | 5.54x | 4.92x | 0.92x | 10.2x |
| raytrace3d | 3d | 266.2 [164.3–269.3] | 193.6 [192.5–194.9] | 51.4 [51.1–51.8] | 1.29s [1.29s–1.30s] | 61.6 [61.6–61.9] | 4.32x | 3.14x | 0.83x | 21.0x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 387.4 [383.3–416.0] | 139.3 [138.3–139.8] | 58.3 [57.9–58.7] | 1.77s [1.72s–1.77s] | 85.6 [85.2–121.1] | 4.53x | 1.63x | 0.68x | 20.7x |
| microdiff | data-diff | 79.3 [79.1–79.7] | 75.9 [75.6–77.2] | 51.8 [51.6–51.9] | 565.3 [563.0–596.7] | 59.8 [59.6–60.1] | 1.33x | 1.27x | 0.87x | 9.45x |
| hyphen | hyphenation | 109.0 [109.0–116.2] | 70.9 [70.5–71.0] | 76.1 [75.8–76.1] | 332.2 [331.5–346.5] | 57.5 [57.4–57.8] | 1.90x | 1.23x | 1.32x | 5.78x |
| prettier_ast | formatting | --- [559.7–567.9] | --- [87.4–141.6] | 102.7 [102.6–102.9] | 7.88s [7.86s–7.94s] | 143.9 [143.4–144.8] | --- | --- | 0.71x | 54.7x |
| text_search | search | 4.97s [4.97s–4.98s] | 2.57s [2.57s–2.58s] | 557.2 [556.6–557.6] | 38.02s [37.95s–38.15s] | 822.7 [821.5–823.0] | 6.04x | 3.12x | 0.68x | 46.2x |
| three_way_merge | merge | 4.01s [3.98s–4.14s] | 5.32s [5.27s–5.46s] | 2.18s [2.18s–2.20s] | 7.95s [7.86s–7.99s] | 1.01s [1.01s–1.01s] | 3.96x | 5.27x | 2.16x | 7.87x |
| log_pipeline | log-processing | 5.06s [5.04s–5.13s] | 1.13s [1.13s–1.15s] | 674.6 [672.5–685.0] | 15.87s [15.73s–16.00s] | 993.0 [989.4–995.4] | 5.10x | 1.14x | 0.68x | 16.0x |
| jq_mix | jq-breadth | 2.25s [2.20s–2.47s] | 2.10s [2.08s–2.20s] | 1.08s [1.07s–1.10s] | 418.48s [416.55s–422.87s] | 4.02s [3.96s–4.12s] | 0.56x | 0.52x | 0.27x | 104x |
| jq_records | jq-data | 2.58s [2.57s–2.60s] | 2.55s [2.55s–2.57s] | 1.00s [973.7–1.15s] | 116.39s [114.12s–117.08s] | 2.07s [2.05s–2.07s] | 1.25x | 1.23x | 0.49x | 56.3x |
| jq_bf | jq-interpreter | 266.8 [266.6–268.8] | 116.7 [115.6–116.7] | 1.92s [1.92s–1.97s] | 422.08s [421.97s–422.17s] | 3.00s [2.94s–3.06s] | 0.09x | 0.04x | 0.64x | 141x |
| jq_tree | jq-paths | 1.67s [1.66s–1.71s] | 1.73s [1.70s–1.74s] | 4.28s [4.16s–4.54s] | 569.68s [569.68s–573.92s] | 5.76s [5.75s–5.89s] | 0.29x | 0.30x | 0.74x | 98.9x |

