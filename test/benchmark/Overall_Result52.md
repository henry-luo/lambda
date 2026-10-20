# Lambda Benchmark Results: Result52

- **Date:** 2026-10-20
- **Platform:** Darwin arm64
- **Lambda commit:** `222fb3a494d31b08b0cbc0578a23fe2d1e8a91fa`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v52-222fb3a494` (25,078,824 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 222.30s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 222.2s (batched 221.1s: sync 82.8s, async 138.2s; non-batched 1.1s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **Julia microbenchmark suite:** every language uses one complete warmup, then one measured workload; formatted-output timing includes synchronous null-sink writes.
- **QuickJS:** 2025-09-13
- **Original measurements:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Added measurements:** During the full matrix, macOS mediaanalysisd was observed at 112-116% CPU (the process had been running for more than 18 hours); sequential timings may reflect concurrent host load.
- **Range notation:** repeated-run timing cells are `median [minimum–maximum]`; single-run cells show the one measured value; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js, Go
- **Results source:** `test/benchmark/benchmark_results_v52.json`
- **Separately measured:** Go measured on 2026-10-20, 3 run(s) from `temp/benchmark_v52/go_refresh.json` on Lambda commit `222fb3a494`. Go cells were measured separately with Go 1.26.5 and a workspace-local GOCACHE after the initial matrix could not write the default macOS Go build cache; the Go-only rerun completed 71/71 rows with three samples.
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with process startup and prior build steps excluded. Runtime compilation performed during the workload is included. This is the historical series, comparable back through Result18, the MIR columns pin `LAMBDA_EXEC_BACKEND=jit`, and LambdaJS pins `JS_EXEC_BACKEND=mir`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | Timed Go | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo | Go/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.37x | 0.29x | 0.21x | 5.77x | 6.49x | 0.34x |
| AWFY | 14 | 13 | 14 | 14 | 14 | 14 | 14 | 14 | 1.43x | 0.50x | 0.11x | 4.69x | 5.17x | 0.25x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.24x | 0.17x | 0.10x | 3.57x | 1.74x | 0.21x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 1.22x | 0.59x | 0.23x | 27.3x | 11.8x | 0.35x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 0.75x | 0.61x | 0.32x | 14.8x | 13.2x | 0.47x |
| Julia microbenchmarks | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 1.63x | 1.50x | 0.32x | 10.1x | 25.7x | 0.44x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 4.16x | 2.24x | 0.28x | 11.6x | 11.9x | 0.21x |
| Text | 11 | 11 | 10 | 11 | 10 | 10 | 11 | 11 | 2.39x | 1.39x | 0.46x | 23.1x | 10.9x | 0.53x |
| **Overall** | 71 | 70 | 70 | 71 | 70 | 70 | 71 | 71 | 1.03x | 0.60x | 0.22x | 9.49x | 7.77x | 0.33x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 67 | 0.56x | 0.93x | 2.81x | 66 |
| complete current suite | 71 | 0.60x | 1.03x | 2.78x | 70 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 2.78x over 70 of 71 rows
- **MIR (typed) / Go geomean:** 1.84x over 70 of 71 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | Go | MIR (typed)/C2MIR | MIR (typed)/Go |
|---|---:|---:|---:|---:|---:|
| text/jq_mix | 44.48s | 1.01s | 997.1 | 44.0x | 44.6x |
| awfy/deltablue | 20.2 | 1.18 | 3.44 | 17.1x | 5.85x |
| awfy/havlak | 615.0 | 38.9 | 71.5 | 15.8x | 8.59x |
| jetstream/cube3d | 7.27 | 0.464 | 1.58 | 15.7x | 4.61x |
| julia/parse_integers | 35.4 | 2.28 | 3.88 | 15.6x | 9.13x |
| text/microdiff | 39.6 | 2.61 | 11.5 | 15.2x | 3.45x |
| jetstream/splay | 284.4 | 19.0 | 27.3 | 15.0x | 10.4x |
| larceny/puzzle | 15.1 | 1.19 | 2.32 | 12.7x | 6.51x |
| awfy/cd | 179.8 | 15.0 | 12.7 | 12.0x | 14.2x |
| jetstream/crypto_sha1 | 28.7 | 2.71 | 0.372 | 10.6x | 77.0x |
| jetstream/hashmap | 27.5 | 2.74 | 4.42 | 10.0x | 6.22x |
| kostya/base64 | 5.18 | 0.574 | 0.895 | 9.02x | 5.78x |

---

### Notable Results

- Missing timings: **4** cells
- MIR (untyped) missing: awfy/queens (exit_-6)
- QuickJS missing: text/jq_mix (exit_1)
- MIR (typed) missing: text/jq_bf (exit_1)
- LambdaJS missing: text/jq_tree (timeout)

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| kostya/matmul | 4.91s | 15.3 | 320x |
| larceny/triangl | 16.79s | 68.7 | 244x |
| kostya/primes | 772.7 | 4.44 | 174x |
| larceny/quicksort | 265.9 | 1.68 | 158x |
| larceny/array1 | 281.0 | 1.81 | 155x |
| text/jq_mix | 521.47s | 3.94s | 132x |
| text/jq_bf | 369.66s | 2.93s | 126x |
| beng/spectralnorm | 284.0 | 2.58 | 110x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.375 | 1.78 | 0.21x |
| kostya/collatz | 403.8 | 1.42s | 0.29x |
| awfy/sieve | 0.118 | 0.389 | 0.30x |
| r7rs/sumfp | 0.485 | 0.893 | 0.54x |
| r7rs/sum | 0.693 | 1.20 | 0.58x |
| julia/iteration_pi_sum | 3.28 | 4.84 | 0.68x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Go (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Go/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.56 [1.54–1.58] | 1.63 [1.62–1.70] | 1.11 [1.03–1.11] | 43.2 [43.1–43.5] | 18.6 [18.4–18.7] | 1.80 [1.75–7.93] | 1.18 [0.924–1.53] | 0.87x | 0.90x | 0.62x | 23.9x | 10.3x | 0.65x |
| fibfp | recursive | 2.32 [2.27–2.38] | 1.26 [1.24–1.27] | 1.12 [1.12–1.12] | 37.8 [37.5–38.9] | 18.6 [18.6–18.7] | 1.81 [1.74–1.83] | 1.19 [0.891–1.64] | 1.28x | 0.69x | 0.62x | 20.8x | 10.3x | 0.66x |
| tak | recursive | 0.137 [0.136–0.143] | 0.141 [0.136–0.141] | 0.110 [0.108–0.118] | 4.47 [4.42–4.49] | 2.76 [2.75–2.77] | 0.796 [0.778–0.805] | 0.166 [0.120–0.450] | 0.17x | 0.18x | 0.14x | 5.62x | 3.47x | 0.21x |
| cpstak | closure | 0.281 [0.272–0.283] | 0.283 [0.272–0.283] | 0.232 [0.231–0.239] | 9.09 [9.00–9.12] | 5.57 [5.52–5.64] | 0.973 [0.969–0.991] | 0.276 [0.206–0.540] | 0.29x | 0.29x | 0.24x | 9.34x | 5.72x | 0.28x |
| sum | iterative | 0.266 [0.266–0.266] | 0.266 [0.266–0.270] | 0.269 [0.269–0.277] | 0.693 [0.692–0.693] | 30.9 [30.9–31.3] | 1.20 [1.19–1.52] | 0.573 [0.505–0.781] | 0.22x | 0.22x | 0.22x | 0.58x | 25.8x | 0.48x |
| sumfp | iterative | 0.069 [0.068–0.070] | 0.068 [0.068–0.068] | 0.079 [0.077–0.085] | 0.485 [0.478–0.489] | 3.69 [3.64–3.72] | 0.893 [0.867–1.21] | 0.357 [0.264–0.546] | 0.08x | 0.08x | 0.09x | 0.54x | 4.13x | 0.40x |
| nqueens | backtrack | 1.17 [1.15–1.21] | 1.16 [1.12–1.20] | 0.366 [0.359–0.366] | 19.1 [19.0–19.3] | 7.88 [7.77–7.89] | 1.73 [1.71–3.08] | 0.274 [0.215–0.527] | 0.67x | 0.67x | 0.21x | 11.1x | 4.55x | 0.16x |
| fft | numeric | 0.323 [0.322–0.334] | 0.078 [0.077–0.091] | 0.024 [0.024–0.024] | 35.7 [35.4–36.4] | 2.74 [2.73–2.78] | 1.55 [1.55–1.62] | 0.098 [0.072–0.343] | 0.21x | 0.05x | 0.02x | 23.0x | 1.77x | 0.06x |
| mbrot | numeric | 0.714 [0.710–0.715] | 0.573 [0.567–0.587] | 0.442 [0.441–0.442] | 2.63 [2.62–2.64] | 17.7 [17.7–17.9] | 1.83 [1.78–1.85] | 0.798 [0.583–1.09] | 0.39x | 0.31x | 0.24x | 1.44x | 9.68x | 0.44x |
| ack | recursive | 13.4 [13.4–13.6] | 9.86 [9.85–9.99] | 11.8 [11.3–11.8] | 179.5 [179.0–179.6] | 101.2 [101.0–101.5] | 13.2 [13.2–13.2] | 11.4 [9.30–15.6] | 1.02x | 0.75x | 0.89x | 13.6x | 7.66x | 0.86x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Go (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Go/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.025 [0.025–0.026] | 0.025 [0.025–0.026] | 0.015 [0.015–0.015] | 0.118 [0.117–0.123] | 0.602 [0.602–0.611] | 0.389 [0.382–0.394] | 0.059 [0.052–0.306] | 0.06x | 0.06x | 0.04x | 0.30x | 1.55x | 0.15x |
| permute | micro | 0.266 [0.260–0.336] | 0.066 [0.063–0.066] | 0.026 [0.025–0.026] | 1.45 [1.44–1.70] | 1.54 [1.53–1.56] | 0.808 [0.802–0.814] | 0.058 [0.054–0.388] | 0.33x | 0.08x | 0.03x | 1.79x | 1.91x | 0.07x |
| queens | micro | --- | 0.062 [0.061–0.065] | 0.018 [0.018–0.018] | 0.677 [0.674–0.739] | 1.04 [1.04–1.05] | 0.640 [0.636–0.642] | 0.065 [0.058–0.286] | --- | 0.10x | 0.03x | 1.06x | 1.62x | 0.10x |
| towers | micro | 0.489 [0.481–0.574] | 0.210 [0.205–0.215] | 0.028 [0.027–0.028] | 2.97 [2.92–3.00] | 2.27 [2.21–2.27] | 1.10 [1.10–1.14] | 0.062 [0.045–0.315] | 0.44x | 0.19x | 0.03x | 2.70x | 2.06x | 0.06x |
| bounce | micro | 0.065 [0.064–0.066] | 0.054 [0.052–0.058] | 0.023 [0.023–0.024] | 2.50 [2.49–2.56] | 0.877 [0.875–0.878] | 0.550 [0.545–0.786] | 0.072 [0.054–0.320] | 0.12x | 0.10x | 0.04x | 4.55x | 1.59x | 0.13x |
| list | micro | 0.800 [0.794–0.803] | 0.121 [0.116–0.130] | 0.021 [0.021–0.022] | 0.620 [0.618–0.625] | 0.910 [0.898–0.945] | 0.519 [0.496–0.525] | 0.063 [0.056–0.335] | 1.54x | 0.23x | 0.04x | 1.20x | 1.75x | 0.12x |
| storage | micro | 0.598 [0.584–0.598] | 0.397 [0.385–0.403] | 0.094 [0.091–0.094] | 2.15 [2.12–2.18] | 2.07 [2.04–2.08] | 0.636 [0.635–0.639] | 0.350 [0.285–0.771] | 0.94x | 0.62x | 0.15x | 3.37x | 3.26x | 0.55x |
| mandelbrot | compute | 37.5 [37.5–37.6] | 37.7 [37.6–38.6] | 30.5 [30.5–30.6] | 32.3 [32.1–32.3] | 876.9 [868.1–1.01s] | 31.0 [31.0–31.0] | 34.1 [33.0–54.9] | 1.21x | 1.21x | 0.98x | 1.04x | 28.3x | 1.10x |
| nbody | compute | 7.07 [7.02–7.08] | 2.60 [2.58–2.65] | 1.47 [1.47–1.49] | 237.9 [237.3–238.5] | 158.6 [158.2–159.3] | 5.45 [5.43–5.77] | 2.63 [2.04–4.77] | 1.30x | 0.48x | 0.27x | 43.7x | 29.1x | 0.48x |
| richards | macro | 313.7 [312.5–315.6] | 77.7 [76.1–78.6] | 28.5 [28.4–28.8] | 340.8 [338.4–341.2] | 190.6 [190.1–191.6] | 46.4 [46.3–49.0] | 28.2 [25.6–45.1] | 6.75x | 1.67x | 0.61x | 7.34x | 4.10x | 0.61x |
| json | macro | 5.85 [5.85–5.90] | 2.03 [2.00–2.04] | 0.256 [0.251–0.274] | 15.9 [15.8–15.9] | 10.8 [10.8–10.8] | 2.62 [2.61–2.85] | 0.683 [0.582–1.73] | 2.23x | 0.77x | 0.10x | 6.05x | 4.11x | 0.26x |
| deltablue | macro | 84.9 [84.5–85.0] | 20.2 [20.1–20.3] | 1.18 [1.18–1.19] | 166.2 [165.4–166.8] | 99.8 [98.7–100.7] | 11.6 [11.5–11.9] | 3.44 [2.75–3.94] | 7.34x | 1.74x | 0.10x | 14.4x | 8.63x | 0.30x |
| havlak | macro | 1.84s [1.80s–1.91s] | 615.0 [613.3–632.0] | 38.9 [38.8–38.9] | 5.87s [5.85s–5.89s] | 3.28s [3.26s–3.47s] | 95.9 [95.6–97.9] | 71.5 [70.7–94.6] | 19.1x | 6.42x | 0.41x | 61.3x | 34.2x | 0.75x |
| cd | macro | 732.3 [731.8–737.2] | 179.8 [179.8–180.6] | 15.0 [15.0–15.0] | 1.80s [1.80s–1.81s] | 957.5 [957.1–960.4] | 35.9 [35.8–36.2] | 12.7 [10.3–19.3] | 20.4x | 5.00x | 0.42x | 50.1x | 26.7x | 0.35x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Go (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Go/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 5.25 [5.22–5.34] | 2.52 [2.48–2.54] | 3.04 [2.97–3.07] | 33.1 [32.8–33.5] | 23.2 [23.2–23.4] | 4.04 [3.98–4.91] | 3.46 [2.60–4.94] | 1.30x | 0.62x | 0.75x | 8.21x | 5.76x | 0.86x |
| fannkuch | permutation | 0.275 [0.268–0.281] | 0.285 [0.281–0.290] | 0.143 [0.142–0.148] | 44.4 [44.1–44.7] | 7.13 [7.13–7.16] | 3.85 [3.85–3.98] | 0.178 [0.176–0.291] | 0.07x | 0.07x | 0.04x | 11.5x | 1.85x | 0.05x |
| fasta | generation | 0.407 [0.405–0.410] | 0.349 [0.348–0.352] | 0.249 [0.242–0.253] | 31.5 [31.5–31.7] | 8.87 [8.76–8.99] | 6.13 [6.06–6.19] | 0.394 [0.291–0.908] | 0.07x | 0.06x | 0.04x | 5.13x | 1.45x | 0.06x |
| knucleotide | hashing | 1.25 [1.08–1.31] | 0.424 [0.402–0.427] | 0.280 [0.279–0.288] | 5.54 [5.51–13.2] | 7.61 [7.61–7.70] | 4.79 [4.78–5.58] | 0.655 [0.506–2.21] | 0.26x | 0.09x | 0.06x | 1.16x | 1.59x | 0.14x |
| pidigits | bignum | 0.359 [0.358–0.363] | 0.360 [0.359–0.373] | 0.045 [0.043–0.046] | 0.375 [0.363–0.383] | 0.127 [0.123–0.127] | 1.78 [1.77–1.81] | 0.194 [0.167–0.559] | 0.20x | 0.20x | 0.03x | 0.21x | 0.07x | 0.11x |
| regexredux | regex | 0.802 [0.800–0.849] | 0.822 [0.816–0.841] | 1.14 [1.14–1.21] | 2.90 [2.88–2.96] | 5.56 [5.54–5.62] | 2.31 [2.31–2.36] | 4.59 [3.45–6.38] | 0.35x | 0.36x | 0.49x | 1.25x | 2.40x | 1.98x |
| revcomp | string | 0.543 [0.535–0.543] | 0.480 [0.475–0.483] | 0.380 [0.374–0.383] | 5.22 [5.09–5.55] | 2.57 [2.54–2.58] | 3.20 [3.17–3.34] | 0.506 [0.351–1.27] | 0.17x | 0.15x | 0.12x | 1.63x | 0.80x | 0.16x |
| spectralnorm | numeric | 1.64 [1.64–1.65] | 0.783 [0.783–0.827] | 0.360 [0.349–0.433] | 284.0 [282.2–286.0] | 63.6 [63.5–65.1] | 2.58 [2.56–2.83] | 0.798 [0.585–1.16] | 0.64x | 0.30x | 0.14x | 110x | 24.7x | 0.31x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Go (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Go/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 254.5 [254.3–255.0] | 110.7 [110.6–111.0] | 27.5 [27.5–29.0] | 2.41s [2.37s–2.47s] | 871.9 [869.2–880.0] | 33.8 [33.6–34.1] | 21.9 [19.2–25.2] | 7.53x | 3.28x | 0.81x | 71.2x | 25.8x | 0.65x |
| matmul | numeric | 4.73 [4.73–4.77] | 4.76 [4.74–4.80] | 6.05 [6.03–6.07] | 4.91s [4.90s–5.11s] | 537.6 [537.4–539.0] | 15.3 [15.3–15.4] | 20.5 [15.9–31.6] | 0.31x | 0.31x | 0.39x | 320x | 35.1x | 1.34x |
| primes | numeric | 2.28 [2.26–2.32] | 2.20 [2.17–2.22] | 1.60 [1.58–1.61] | 772.7 [772.4–790.7] | 94.8 [94.7–95.2] | 4.44 [4.41–4.83] | 2.88 [2.33–3.32] | 0.51x | 0.50x | 0.36x | 174x | 21.4x | 0.65x |
| base64 | string | 12.5 [12.2–18.8] | 5.18 [5.17–5.37] | 0.574 [0.565–0.574] | 465.7 [464.7–470.9] | 158.6 [158.2–159.7] | 17.7 [17.3–18.0] | 0.895 [0.812–1.16] | 0.71x | 0.29x | 0.03x | 26.3x | 8.96x | 0.05x |
| levenshtein | string | 22.6 [22.5–23.2] | 2.86 [2.83–2.94] | 0.899 [0.889–0.919] | 403.3 [402.4–407.4] | 54.0 [53.9–54.0] | 4.07 [3.96–4.08] | 1.77 [1.65–2.37] | 5.56x | 0.70x | 0.22x | 99.1x | 13.3x | 0.44x |
| json_gen | data | 24.5 [24.4–24.9] | 7.25 [7.20–7.26] | 1.51 [1.49–1.51] | 23.3 [23.2–23.4] | 19.8 [19.6–20.3] | 6.10 [6.10–6.10] | 3.13 [2.56–4.07] | 4.02x | 1.19x | 0.25x | 3.81x | 3.24x | 0.51x |
| collatz | numeric | 295.2 [293.9–296.5] | 280.1 [274.7–280.1] | 229.8 [229.7–231.1] | 403.8 [401.0–404.4] | 6.22s [6.21s–6.23s] | 1.42s [1.41s–1.42s] | 139.3 [139.3–168.7] | 0.21x | 0.20x | 0.16x | 0.29x | 4.39x | 0.10x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Go (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Go/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 169.3 [169.3–169.4] | 150.7 [150.3–151.2] | 57.5 [57.3–57.5] | 16.79s [16.76s–17.09s] | 2.18s [2.17s–2.19s] | 68.7 [68.2–68.8] | 49.6 [49.5–74.9] | 2.46x | 2.19x | 0.84x | 244x | 31.7x | 0.72x |
| array1 | array | 0.803 [0.803–0.820] | 0.805 [0.804–0.806] | 0.293 [0.293–0.295] | 281.0 [280.6–283.3] | 35.8 [35.7–36.2] | 1.81 [1.80–1.84] | 0.690 [0.506–0.956] | 0.44x | 0.45x | 0.16x | 155x | 19.8x | 0.38x |
| deriv | symbolic | 22.8 [22.7–23.0] | 5.93 [5.88–5.99] | 2.89 [2.84–2.90] | 49.0 [48.2–49.3] | 58.8 [58.4–58.9] | 3.78 [3.78–3.86] | 5.37 [4.25–8.13] | 6.04x | 1.57x | 0.76x | 13.0x | 15.6x | 1.42x |
| diviter | iterative | 265.6 [265.6–266.8] | 265.9 [265.5–265.9] | 265.6 [265.4–266.1] | 600.2 [599.5–600.6] | 26.63s [26.59s–26.82s] | 469.7 [469.0–470.8] | 279.9 [250.8–392.5] | 0.57x | 0.57x | 0.57x | 1.28x | 56.7x | 0.60x |
| divrec | recursive | 1.21 [1.20–1.21] | 1.21 [1.20–1.23] | 4.86 [4.83–4.87] | 76.9 [75.4–77.3] | 35.7 [35.4–36.1] | 7.55 [7.53–7.67] | 7.89 [5.71–10.8] | 0.16x | 0.16x | 0.64x | 10.2x | 4.73x | 1.04x |
| gcbench | allocation | 125.2 [122.7–127.6] | 78.9 [78.2–79.4] | 70.5 [70.4–71.7] | 723.6 [722.0–725.5] | 554.2 [552.0–558.7] | 23.4 [23.1–23.4] | 40.0 [39.8–64.7] | 5.35x | 3.37x | 3.02x | 31.0x | 23.7x | 1.71x |
| paraffins | combinat | 0.199 [0.195–0.202] | 0.154 [0.151–0.156] | 0.047 [0.046–0.048] | 7.30 [7.25–7.43] | 2.52 [2.50–2.52] | 1.00 [0.989–1.06] | 0.063 [0.062–0.186] | 0.20x | 0.15x | 0.05x | 7.28x | 2.51x | 0.06x |
| pnpoly | numeric | 2.32 [2.27–2.34] | 3.63 [3.62–3.65] | 1.95 [1.92–2.01] | 43.8 [43.5–43.9] | 201.4 [201.2–203.4] | 5.80 [5.79–5.84] | 4.37 [3.37–4.56] | 0.40x | 0.63x | 0.34x | 7.55x | 34.7x | 0.75x |
| puzzle | search | 14.4 [14.3–14.5] | 15.1 [15.1–15.2] | 1.19 [1.19–1.21] | 9.93 [9.89–9.93] | 29.2 [29.1–29.3] | 3.29 [3.28–3.36] | 2.32 [1.73–2.58] | 4.39x | 4.60x | 0.36x | 3.02x | 8.86x | 0.71x |
| quicksort | sorting | 1.03 [0.930–1.06] | 0.610 [0.572–0.639] | 0.192 [0.192–0.193] | 265.9 [265.0–267.7] | 19.2 [19.0–19.2] | 1.68 [1.66–1.73] | 0.578 [0.423–0.723] | 0.61x | 0.36x | 0.11x | 158x | 11.4x | 0.34x |
| ray | numeric | 0.222 [0.222–0.224] | 0.216 [0.216–0.220] | 0.170 [0.166–0.174] | 5.07 [5.02–5.13] | 13.7 [13.6–13.8] | 3.62 [3.54–3.85] | 0.201 [0.150–0.519] | 0.06x | 0.06x | 0.05x | 1.40x | 3.78x | 0.06x |

### Julia microbenchmarks

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Go (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Go/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| parse_integers | conversion | 39.0 [38.9–39.9] | 35.4 [34.7–35.5] | 2.28 [2.26–2.30] | 581.3 [573.7–597.5] | 276.2 [275.1–277.1] | 13.0 [12.9–13.0] | 3.88 [3.02–6.05] | 3.00x | 2.73x | 0.18x | 44.8x | 21.3x | 0.30x |
| matrix_statistics | numeric | 16.2 [16.1–16.6] | 16.2 [15.6–16.3] | 9.04 [9.00–9.06] | 350.2 [350.1–350.9] | 1.28s [1.28s–1.29s] | 20.9 [20.8–21.2] | 11.0 [9.64–15.5] | 0.78x | 0.78x | 0.43x | 16.8x | 61.1x | 0.53x |
| iteration_pi_sum | numeric | 6.80 [6.79–6.82] | 6.82 [6.78–6.84] | 2.99 [2.98–3.00] | 3.28 [3.28–3.31] | 164.2 [163.9–164.5] | 4.84 [4.80–5.02] | 4.43 [3.64–6.62] | 1.40x | 1.41x | 0.62x | 0.68x | 33.9x | 0.91x |
| formatted_output | io | 78.0 [77.6–78.9] | 62.6 [62.4–62.7] | 8.20 [8.20–8.22] | 755.0 [754.6–761.8] | 360.4 [356.0–364.7] | 36.5 [36.2–36.7] | 9.25 [7.90–13.9] | 2.14x | 1.72x | 0.22x | 20.7x | 9.88x | 0.25x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Go (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Go/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 9.86 [9.78–9.89] | 7.27 [7.17–7.51] | 0.464 [0.463–0.474] | 120.7 [120.0–132.2] | 217.7 [215.4–223.6] | 17.5 [17.4–17.7] | 1.58 [1.37–4.29] | 0.56x | 0.41x | 0.03x | 6.89x | 12.4x | 0.09x |
| navier_stokes | numeric | 76.3 [76.1–76.8] | 68.5 [68.5–68.6] | 45.4 [45.4–45.5] | 31.3 [30.9–31.4] | 98.7 [98.4–98.8] | 14.1 [13.9–14.2] | 3.27 [3.20–6.77] | 5.40x | 4.85x | 3.22x | 2.22x | 6.99x | 0.23x |
| splay | data | 290.6 [285.6–292.7] | 284.4 [279.2–288.6] | 19.0 [18.6–19.1] | 205.9 [202.7–207.8] | 145.8 [145.4–146.1] | 20.1 [19.5–20.4] | 27.3 [26.1–39.7] | 14.5x | 14.2x | 0.95x | 10.3x | 7.26x | 1.36x |
| hashmap | data | 73.2 [72.9–73.8] | 27.5 [27.4–27.5] | 2.74 [2.73–2.84] | 384.2 [381.7–386.2] | 316.4 [315.7–316.5] | 15.3 [15.2–15.6] | 4.42 [3.62–6.76] | 4.77x | 1.79x | 0.18x | 25.1x | 20.6x | 0.29x |
| crypto_sha1 | crypto | 49.4 [49.1–50.9] | 28.7 [28.5–29.5] | 2.71 [2.54–2.82] | 396.4 [396.1–397.1] | 218.4 [217.9–218.6] | 8.78 [8.68–8.79] | 0.372 [0.298–0.630] | 5.62x | 3.26x | 0.31x | 45.1x | 24.9x | 0.04x |
| raytrace3d | 3d | 80.3 [80.1–81.8] | 14.1 [14.1–14.2] | 2.15 [2.13–2.23] | 252.0 [251.9–255.4] | 162.7 [162.0–163.1] | 18.4 [18.3–19.4] | 4.07 [3.24–5.89] | 4.36x | 0.76x | 0.12x | 13.7x | 8.83x | 0.22x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Go (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Go/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 732.7 [726.8–734.5] | 42.8 [42.8–43.1] | 12.7 [12.6–12.7] | 739.1 [735.1–871.3] | 614.1 [610.4–626.4] | 39.1 [39.0–40.0] | 16.8 [14.1–28.9] | 18.7x | 1.09x | 0.32x | 18.9x | 15.7x | 0.43x |
| microdiff | data-diff | 37.6 [37.6–38.0] | 39.6 [39.1–40.2] | 2.61 [2.60–2.62] | 145.9 [145.8–146.4] | 108.5 [107.7–108.7] | 16.4 [16.3–17.0] | 11.5 [9.43–18.9] | 2.30x | 2.42x | 0.16x | 8.92x | 6.63x | 0.70x |
| hyphen | hyphenation | 60.7 [50.9–61.1] | 9.67 [9.58–10.7] | 1.48 [1.47–1.52] | 54.1 [54.0–54.9] | 79.9 [78.7–80.8] | 6.74 [6.72–6.90] | 2.03 [1.64–3.40] | 9.01x | 1.43x | 0.22x | 8.02x | 11.9x | 0.30x |
| prettier_ast | formatting | 860.1 [852.4–863.5] | 364.8 [363.5–367.6] | 41.7 [41.4–42.0] | 2.18s [2.16s–2.18s] | 1.41s [1.40s–1.42s] | 99.5 [99.4–115.9] | 94.7 [93.9–115.7] | 8.64x | 3.67x | 0.42x | 21.9x | 14.2x | 0.95x |
| text_search | search | 7.78s [7.75s–7.93s] | 1.37s [1.37s–1.38s] | 508.5 [508.4–508.8] | 17.40s [17.36s–17.40s] | 38.95s [38.66s–39.07s] | 775.5 [775.0–775.8] | 477.8 [473.7–480.2] | 10.0x | 1.77x | 0.66x | 22.4x | 50.2x | 0.62x |
| three_way_merge | merge | 2.22s [2.21s–2.37s] | 1.52s [1.52s–1.56s] | 2.13s [2.12s–2.14s] | 6.75s [6.72s–6.82s] | 6.20s [6.18s–6.25s] | 965.0 [963.7–973.3] | 601.6 [601.1–624.1] | 2.30x | 1.58x | 2.21x | 7.00x | 6.43x | 0.62x |
| log_pipeline | log-processing | 4.52s [4.44s–4.74s] | 1.04s [1.04s–1.04s] | 622.8 [620.7–623.8] | 9.91s [9.89s–10.08s] | 9.77s [9.61s–9.78s] | 940.6 [938.6–945.1] | 337.8 [337.4–349.7] | 4.81x | 1.11x | 0.66x | 10.5x | 10.4x | 0.36x |
| jq_mix | jq-breadth | 44.88s [44.87s–45.08s] | 44.48s [44.20s–44.84s] | 1.01s [998.3–1.03s] / λ-VM --- | 521.47s | --- | 3.94s [3.94s–3.99s] | 997.1 [996.4–1.02s] | 11.4x | 11.3x | 0.26x | 132x | --- | 0.25x |
| jq_records | jq-data | 346.4 [344.3–347.7] | 409.1 [399.2–413.2] | 872.3 [868.4–880.7] / λ-VM --- | 108.54s [107.47s–109.46s] | 10.86s [10.77s–10.99s] | 2.01s [1.98s–2.05s] | 1.06s [1.03s–1.12s] | 0.17x | 0.20x | 0.43x | 54.0x | 5.40x | 0.53x |
| jq_bf | jq-interpreter | 191.9 [191.6–197.7] | --- | 1.83s [1.82s–1.84s] / λ-VM --- | 369.66s [369.01s–371.20s] | 27.76s [27.68s–27.85s] | 2.93s [2.92s–2.95s] | 2.49s [2.49s–2.53s] | 0.07x | --- | 0.62x | 126x | 9.47x | 0.85x |
| jq_tree | jq-paths | 1.86s [1.76s–1.87s] | 1.59s [1.59s–1.62s] | 4.08s [4.06s–4.11s] / λ-VM --- | --- | 47.65s [47.51s–47.66s] | 6.04s [5.73s–6.14s] | 4.01s [4.00s–4.08s] | 0.31x | 0.26x | 0.68x | --- | 7.89x | 0.66x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, including **startup and compilation performed inside that process**. Build steps completed before invocation are excluded. The MIR and LambdaJS columns use the shipped auto tier -- no `LAMBDA_EXEC_BACKEND` or `JS_EXEC_BACKEND` override -- which is what `lambda.exe run script.ls` and `lambda.exe js script.js` actually do.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Process timing includes each engine's recorded warmup policy, workload and verification; separately added measurements are described above.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. The MIR and LambdaJS columns are re-run, because part 1 pins the compiled lane and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS (auto) | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS (auto)/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.36x | 0.35x | 1.02x | 1.65x | 0.42x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.17x | 0.94x | 0.95x | 9.03x | 0.83x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.43x | 0.42x | 1.05x | 2.59x | 0.34x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 1.61x | 0.89x | 0.72x | 8.62x | 2.87x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 0.93x | 0.65x | 0.99x | 4.95x | 1.91x |
| Julia microbenchmarks | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 1.85x | 0.99x | 1.28x | 11.5x | 9.48x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 2.97x | 2.93x | 0.90x | 17.1x | 3.65x |
| Text | 11 | 11 | 10 | 11 | 10 | 10 | 11 | 2.36x | 1.95x | 0.73x | 37.6x | 6.95x |
| **Overall** | 71 | 71 | 70 | 71 | 70 | 70 | 71 | 1.10x | 0.86x | 0.93x | 7.30x | 1.55x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 14.5 [14.2–14.5] | 14.2 [14.2–14.7] | 47.8 [47.8–755.4] | 79.6 [79.6–79.9] | 24.6 [24.5–25.3] | 45.4 [45.4–92.4] | 0.32x | 0.31x | 1.05x | 1.75x | 0.54x |
| fibfp | recursive | 13.7 [13.6–14.1] | 13.2 [13.0–13.2] | 46.7 [46.4–47.3] | 75.4 [75.3–75.5] | 24.9 [24.7–25.3] | 45.9 [45.8–48.3] | 0.30x | 0.29x | 1.02x | 1.64x | 0.54x |
| tak | recursive | 12.7 [12.4–12.7] | 12.8 [12.6–12.8] | 45.8 [45.7–45.9] | 46.2 [45.9–46.4] | 8.74 [8.73–9.04] | 43.9 [43.7–44.6] | 0.29x | 0.29x | 1.04x | 1.05x | 0.20x |
| cpstak | closure | 12.6 [12.5–13.0] | 12.7 [12.5–12.7] | 46.0 [45.9–46.1] | 51.3 [50.7–51.4] | 11.5 [11.4–12.2] | 44.8 [44.1–44.8] | 0.28x | 0.28x | 1.03x | 1.15x | 0.26x |
| sum | iterative | 15.4 [15.2–15.5] | 13.2 [12.8–14.5] | 46.0 [45.3–46.0] | 37.7 [37.4–38.2] | 37.1 [37.1–37.4] | 45.4 [45.0–46.2] | 0.34x | 0.29x | 1.01x | 0.83x | 0.82x |
| sumfp | iterative | 15.0 [14.6–15.0] | 13.6 [13.5–13.8] | 45.0 [45.0–45.7] | 38.9 [38.8–39.3] | 9.65 [9.42–10.4] | 44.9 [44.6–45.0] | 0.33x | 0.30x | 1.00x | 0.87x | 0.22x |
| nqueens | backtrack | 21.0 [21.0–21.4] | 22.9 [22.8–23.2] | 46.8 [46.6–46.8] | 139.7 [139.7–140.3] | 14.2 [13.5–14.9] | 46.1 [45.4–46.2] | 0.46x | 0.50x | 1.02x | 3.03x | 0.31x |
| fft | numeric | 28.3 [27.4–28.4] | 25.3 [25.1–25.7] | 46.1 [45.7–46.2] | 179.3 [178.2–181.1] | 8.74 [8.50–9.33] | 45.0 [44.8–45.6] | 0.63x | 0.56x | 1.03x | 3.98x | 0.19x |
| mbrot | numeric | 16.0 [15.3–16.9] | 16.0 [16.0–16.2] | 45.6 [45.6–45.7] | 57.6 [57.2–58.5] | 24.2 [23.9–24.4] | 45.5 [45.3–45.5] | 0.35x | 0.35x | 1.00x | 1.27x | 0.53x |
| ack | recursive | 26.3 [25.8–26.7] | 21.8 [21.6–22.1] | 57.8 [57.4–59.1] | 220.4 [219.1–221.1] | 107.8 [107.5–108.1] | 56.9 [56.7–58.9] | 0.46x | 0.38x | 1.02x | 3.87x | 1.89x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 15.6 [15.5–15.9] | 13.8 [13.6–13.8] | 45.5 [44.9–45.8] | 40.9 [40.9–41.4] | 6.77 [6.26–7.41] | 44.6 [44.4–47.0] | 0.35x | 0.31x | 1.02x | 0.92x | 0.15x |
| permute | micro | 14.9 [14.8–15.1] | 15.4 [15.3–15.5] | 45.7 [45.5–46.2] | 239.2 [239.0–387.0] | 7.76 [7.56–8.41] | 45.5 [45.1–45.6] | 0.33x | 0.34x | 1.00x | 5.25x | 0.17x |
| queens | micro | 17.0 [16.8–17.1] | 18.7 [18.4–19.1] | 47.8 [47.6–48.1] | 441.0 [433.0–442.1] | 7.08 [6.91–8.02] | 45.0 [44.9–45.4] | 0.38x | 0.41x | 1.06x | 9.79x | 0.16x |
| towers | micro | 19.7 [19.7–19.9] | 20.8 [20.7–21.4] | 45.9 [45.7–46.0] | 89.0 [88.2–89.2] | 8.42 [8.23–9.14] | 45.4 [45.3–45.9] | 0.43x | 0.46x | 1.01x | 1.96x | 0.19x |
| bounce | micro | 15.8 [15.3–17.4] | 15.3 [14.9–15.7] | 46.1 [45.6–46.4] | 712.0 [710.9–712.9] | 7.89 [7.66–8.48] | 45.4 [45.0–46.1] | 0.35x | 0.34x | 1.02x | 15.7x | 0.17x |
| list | micro | 14.7 [14.5–14.8] | 15.0 [15.0–15.4] | 46.5 [46.1–46.5] | 80.5 [80.1–81.3] | 7.36 [6.85–7.68] | 45.0 [44.9–46.0] | 0.33x | 0.33x | 1.03x | 1.79x | 0.16x |
| storage | micro | 14.8 [14.7–14.8] | 15.2 [15.0–15.3] | 45.5 [45.3–46.6] | 674.6 [668.6–676.4] | 9.04 [8.93–9.72] | 45.8 [45.7–45.9] | 0.32x | 0.33x | 0.99x | 14.7x | 0.20x |
| mandelbrot | compute | 76.3 [76.2–76.3] | 60.9 [60.8–61.0] | 76.5 [76.3–76.7] | 81.8 [81.1–81.9] | 884.5 [875.3–1.02s] | 76.2 [75.4–77.5] | 1.00x | 0.80x | 1.00x | 1.07x | 11.6x |
| nbody | compute | 57.3 [57.2–57.8] | 57.2 [56.5–57.4] | 47.8 [47.6–48.5] | 497.7 [496.9–498.0] | 165.9 [165.8–166.5] | 50.6 [50.0–51.8] | 1.13x | 1.13x | 0.94x | 9.83x | 3.28x |
| richards | macro | 580.7 [579.1–586.1] | 325.3 [320.3–328.5] | 77.0 [76.9–78.2] | 934.0 [803.6–938.4] | 198.5 [197.9–199.4] | 91.4 [91.4–97.2] | 6.35x | 3.56x | 0.84x | 10.2x | 2.17x |
| json | macro | 43.5 [43.2–43.5] | 39.2 [39.0–39.5] | 52.6 [52.3–53.9] | 2.19s [2.17s–2.23s] | 18.8 [18.5–19.6] | 47.4 [47.1–48.5] | 0.92x | 0.83x | 1.11x | 46.3x | 0.40x |
| deltablue | macro | 333.5 [330.6–335.2] | 215.8 [214.1–216.0] | 53.6 [53.5–54.9] | 2.09s [2.08s–2.12s] | 108.2 [107.3–109.4] | 57.8 [56.7–58.8] | 5.77x | 3.73x | 0.93x | 36.2x | 1.87x |
| havlak | macro | 4.44s [4.44s–4.53s] | 1.25s [1.21s–1.26s] | 91.5 [91.5–91.9] | 9.08s [9.04s–9.15s] | 3.29s [3.27s–3.48s] | 143.9 [143.7–147.3] | 30.9x | 8.70x | 0.64x | 63.1x | 22.9x |
| cd | macro | 962.4 [960.4–964.9] | 548.2 [547.9–555.3] | 67.7 [67.6–68.6] | 4.51s [4.50s–4.58s] | 966.4 [966.1–969.1] | 82.2 [81.7–82.4] | 11.7x | 6.67x | 0.82x | 54.9x | 11.8x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 24.0 [23.6–24.0] | 19.3 [19.2–19.7] | 49.1 [49.1–49.9] | 104.6 [103.9–241.8] | 29.9 [29.3–30.3] | 47.1 [46.5–47.3] | 0.51x | 0.41x | 1.04x | 2.22x | 0.63x |
| fannkuch | permutation | 27.2 [27.0–27.3] | 25.9 [25.9–26.0] | 46.3 [45.8–57.5] | 181.9 [179.2–183.0] | 13.3 [13.0–13.9] | 46.0 [45.2–46.9] | 0.59x | 0.56x | 1.01x | 3.95x | 0.29x |
| fasta | generation | 20.3 [19.8–20.5] | 20.5 [20.3–21.2] | 46.6 [46.4–47.7] | 106.0 [105.3–106.2] | 15.1 [15.0–16.0] | 48.9 [48.5–49.3] | 0.41x | 0.42x | 0.95x | 2.17x | 0.31x |
| knucleotide | hashing | 24.5 [24.3–25.6] | 22.9 [22.3–23.1] | 46.8 [46.6–47.2] | 101.8 [101.1–102.3] | 13.7 [13.7–14.7] | 47.4 [46.8–47.6] | 0.52x | 0.48x | 0.99x | 2.15x | 0.29x |
| pidigits | bignum | 12.0 [11.7–12.3] | 11.9 [11.8–12.2] | 47.6 [47.6–48.8] | 85.6 [85.4–85.8] | 6.31 [6.01–6.71] | 44.6 [43.6–45.8] | 0.27x | 0.27x | 1.07x | 1.92x | 0.14x |
| regexredux | regex | 12.1 [11.8–12.2] | 12.0 [11.9–12.3] | 49.1 [48.3–49.2] | 71.0 [70.2–71.4] | 12.0 [11.8–12.6] | 44.8 [43.8–44.9] | 0.27x | 0.27x | 1.10x | 1.59x | 0.27x |
| revcomp | string | 14.9 [14.1–14.9] | 15.5 [15.0–15.8] | 46.5 [46.2–47.1] | 89.6 [89.3–89.7] | 8.55 [8.41–9.07] | 45.4 [45.2–45.5] | 0.33x | 0.34x | 1.03x | 1.97x | 0.19x |
| spectralnorm | numeric | 38.3 [37.7–38.5] | 38.2 [38.0–38.6] | 59.9 [46.9–177.8] | 382.7 [382.2–383.6] | 70.8 [70.3–71.5] | 46.5 [46.5–46.5] | 0.82x | 0.82x | 1.29x | 8.23x | 1.52x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 276.2 [275.6–278.2] | 132.7 [132.1–132.9] | 73.9 [73.1–74.9] | 2.50s [2.50s–2.53s] | 879.1 [876.7–887.2] | 78.8 [77.9–79.3] | 3.50x | 1.68x | 0.94x | 31.8x | 11.2x |
| matmul | numeric | 222.7 [222.7–224.2] | 47.4 [47.2–48.0] | 51.5 [51.3–51.8] | 4.98s [4.97s–4.98s] | 544.8 [544.2–545.8] | 59.5 [59.4–59.8] | 3.74x | 0.80x | 0.87x | 83.6x | 9.15x |
| primes | numeric | 168.0 [167.3–168.6] | 75.7 [75.0–77.4] | 47.2 [47.2–47.6] | 835.3 [829.3–843.7] | 101.6 [101.2–101.8] | 48.0 [47.6–51.7] | 3.50x | 1.58x | 0.98x | 17.4x | 2.12x |
| base64 | string | 52.5 [52.0–52.8] | 45.7 [45.5–47.0] | 47.9 [47.6–58.4] | 641.0 [637.6–644.7] | 165.7 [165.3–166.7] | 63.4 [62.0–65.1] | 0.83x | 0.72x | 0.76x | 10.1x | 2.61x |
| levenshtein | string | 113.5 [108.1–117.0] | 60.6 [60.4–195.5] | 46.6 [46.4–47.1] | 550.6 [545.5–552.3] | 60.9 [60.4–61.1] | 48.0 [47.9–48.7] | 2.36x | 1.26x | 0.97x | 11.5x | 1.27x |
| json_gen | data | 66.3 [65.7–66.9] | 53.0 [52.5–53.2] | 47.8 [47.2–48.4] | 110.2 [110.0–110.6] | 26.8 [26.3–26.9] | 50.3 [49.2–52.4] | 1.32x | 1.05x | 0.95x | 2.19x | 0.53x |
| collatz | numeric | 349.4 [348.8–351.0] | 320.1 [316.6–321.3] | 276.7 [276.2–277.7] | 438.8 [437.9–442.6] | 6.22s [6.22s–6.23s] | 1.46s [1.46s–1.46s] | 0.24x | 0.22x | 0.19x | 0.30x | 4.26x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 191.8 [191.6–192.5] | 172.6 [172.4–174.0] | 103.7 [103.6–103.7] | 17.09s [16.98s–17.40s] | 2.18s [2.18s–2.20s] | 113.2 [112.6–113.4] | 1.69x | 1.52x | 0.92x | 151x | 19.3x |
| array1 | array | 133.9 [133.8–255.6] | 127.7 [126.7–128.7] | 45.3 [44.9–45.7] | 326.5 [325.6–327.2] | 42.2 [42.0–42.7] | 45.0 [44.9–45.5] | 2.98x | 2.84x | 1.01x | 7.26x | 0.94x |
| deriv | symbolic | 45.8 [44.5–46.6] | 26.8 [25.6–27.6] | 49.9 [49.7–50.1] | 127.0 [126.7–127.2] | 65.5 [65.3–65.5] | 48.7 [48.5–49.0] | 0.94x | 0.55x | 1.03x | 2.61x | 1.35x |
| diviter | iterative | 389.2 [389.1–389.9] | 12.3 [12.1–13.3] | 311.4 [311.3–312.0] | 645.3 [644.5–645.5] | 26.64s [26.60s–26.83s] | 514.7 [513.9–515.8] | 0.76x | 0.02x | 0.61x | 1.25x | 51.8x |
| divrec | recursive | 14.2 [14.0–14.3] | 14.5 [14.3–14.5] | 50.2 [50.2–50.3] | 125.3 [125.2–126.7] | 42.4 [41.9–42.7] | 51.5 [51.2–51.5] | 0.28x | 0.28x | 0.97x | 2.43x | 0.82x |
| gcbench | allocation | 237.7 [236.4–238.5] | 113.1 [112.5–113.9] | 117.0 [116.2–117.2] | 802.5 [795.4–806.0] | 567.9 [564.8–571.6] | 66.5 [66.4–67.2] | 3.58x | 1.70x | 1.76x | 12.1x | 8.54x |
| paraffins | combinat | 30.4 [30.2–48.7] | 49.0 [48.8–49.2] | 46.6 [46.4–46.9] | 235.1 [234.4–237.7] | 8.73 [8.69–9.58] | 44.7 [44.2–45.3] | 0.68x | 1.10x | 1.04x | 5.26x | 0.20x |
| pnpoly | numeric | 31.8 [31.7–32.4] | 41.0 [40.9–41.2] | 47.8 [47.3–47.8] | 110.9 [110.3–111.9] | 208.1 [208.0–209.9] | 50.1 [49.6–50.5] | 0.63x | 0.82x | 0.95x | 2.21x | 4.15x |
| puzzle | search | 34.8 [34.0–34.8] | 36.1 [35.3–36.4] | 46.7 [46.5–46.7] | 123.9 [122.7–126.6] | 35.2 [34.9–35.8] | 46.6 [46.5–46.8] | 0.75x | 0.78x | 1.00x | 2.66x | 0.76x |
| quicksort | sorting | 22.5 [22.1–22.6] | 25.5 [25.2–26.6] | 46.7 [46.3–47.5] | 394.4 [392.5–399.3] | 25.3 [25.3–26.0] | 45.7 [44.6–46.2] | 0.49x | 0.56x | 1.02x | 8.63x | 0.55x |
| ray | numeric | 41.3 [41.1–41.7] | 42.0 [41.4–42.1] | 46.3 [45.8–46.3] | 75.4 [75.0–75.4] | 19.8 [19.5–20.6] | 48.9 [46.5–191.4] | 0.85x | 0.86x | 0.95x | 1.54x | 0.41x |

### Julia microbenchmarks

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| parse_integers | conversion | 125.4 [125.0–126.1] | 104.0 [103.8–106.0] | 97.1 [96.7–97.2] | 1.46s [1.44s–1.46s] | 561.1 [558.5–562.5] | 72.8 [72.3–73.1] | 1.72x | 1.43x | 1.33x | 20.0x | 7.71x |
| matrix_statistics | numeric | 75.4 [75.3–76.1] | 74.3 [72.9–89.0] | 109.8 [109.4–112.0] | 995.8 [987.6–1.00s] | 2.56s [2.56s–2.57s] | 92.7 [91.9–92.8] | 0.81x | 0.80x | 1.18x | 10.7x | 27.6x |
| iteration_pi_sum | numeric | 215.5 [215.3–215.5] | 33.3 [33.1–33.5] | 102.7 [97.2–232.3] | 294.9 [294.7–296.0] | 336.2 [335.0–336.2] | 54.3 [53.9–70.1] | 3.97x | 0.61x | 1.89x | 5.44x | 6.20x |
| formatted_output | io | 252.5 [250.9–253.5] | 162.6 [161.8–164.7] | 107.9 [107.4–109.7] | 1.83s [1.83s–1.83s] | 736.9 [719.1–738.9] | 120.1 [119.7–120.5] | 2.10x | 1.35x | 0.90x | 15.2x | 6.14x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 93.2 [92.3–95.1] | 108.2 [106.5–109.3] | 48.9 [48.8–67.4] | 1.28s [1.26s–1.28s] | 224.9 [222.8–231.2] | 63.8 [62.6–64.3] | 1.46x | 1.69x | 0.77x | 20.1x | 3.52x |
| navier_stokes | numeric | 243.9 [243.1–245.3] | 223.1 [222.3–226.6] | 93.8 [93.0–94.5] | 1.27s [1.26s–1.40s] | 109.9 [109.3–110.1] | 60.0 [59.9–60.2] | 4.07x | 3.72x | 1.56x | 21.1x | 1.83x |
| splay | data | 452.1 [450.3–458.9] | 1.00s [999.7–1.00s] | 67.2 [66.4–77.1] | 849.2 [841.0–854.3] | 570.7 [566.5–580.2] | 95.3 [93.1–95.7] | 4.74x | 10.5x | 0.71x | 8.91x | 5.99x |
| hashmap | data | 177.5 [176.9–181.4] | 130.0 [128.9–130.6] | 48.4 [48.4–49.6] | 884.7 [838.3–978.0] | 323.9 [323.5–324.2] | 60.4 [59.8–60.5] | 2.94x | 2.15x | 0.80x | 14.7x | 5.37x |
| crypto_sha1 | crypto | 179.4 [174.3–181.1] | 154.1 [153.4–154.4] | 49.0 [48.7–50.1] | 756.3 [752.8–760.5] | 225.8 [225.2–226.0] | 53.2 [53.1–53.9] | 3.37x | 2.90x | 0.92x | 14.2x | 4.25x |
| raytrace3d | 3d | 157.3 [156.5–157.4] | 97.6 [97.3–99.2] | 52.6 [52.2–52.7] | 2.00s [1.98s–2.00s] | 170.2 [169.5–170.4] | 63.7 [61.9–65.4] | 2.47x | 1.53x | 0.83x | 31.4x | 2.67x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 769.0 [768.5–769.1] | 70.5 [70.2–71.2] | 59.2 [58.8–59.4] | 5.22s [5.21s–5.23s] | 622.6 [618.8–635.0] | 84.3 [84.0–85.1] | 9.13x | 0.84x | 0.70x | 61.9x | 7.39x |
| microdiff | data-diff | 72.2 [71.6–74.7] | 76.9 [76.4–77.3] | 52.1 [51.8–52.5] | 408.9 [407.5–411.1] | 115.5 [115.2–115.7] | 60.5 [60.2–61.4] | 1.19x | 1.27x | 0.86x | 6.76x | 1.91x |
| hyphen | hyphenation | 96.8 [96.1–104.1] | 76.1 [73.9–76.2] | 77.6 [77.5–79.3] | 17.17s [17.06s–17.44s] | 102.1 [100.3–102.5] | 58.2 [58.2–58.3] | 1.66x | 1.31x | 1.33x | 295x | 1.76x |
| prettier_ast | formatting | 1.39s [1.37s–1.59s] | 3.96s [3.93s–3.99s] | 105.7 [103.8–106.1] | 2.94s [2.91s–2.95s] | 1.42s [1.41s–1.44s] | 145.9 [144.8–160.9] | 9.51x | 27.2x | 0.72x | 20.1x | 9.74x |
| text_search | search | 7.79s [7.77s–7.86s] | 1.40s [1.40s–1.54s] | 557.2 [555.8–558.2] | 17.51s [17.45s–17.85s] | 38.96s [38.67s–39.08s] | 821.0 [820.5–821.7] | 9.49x | 1.71x | 0.68x | 21.3x | 47.5x |
| three_way_merge | merge | 3.29s [3.25s–3.52s] | 2.42s [2.38s–2.55s] | 2.18s [2.17s–2.19s] | 6.90s [6.85s–6.94s] | 6.21s [6.19s–6.26s] | 1.01s [1.01s–1.02s] | 3.26x | 2.40x | 2.16x | 6.83x | 6.15x |
| log_pipeline | log-processing | 5.77s [5.74s–5.85s] | 1.30s [1.29s–1.31s] | 672.6 [671.0–674.8] | 10.06s [10.05s–10.06s] | 9.81s [9.64s–9.81s] | 994.0 [992.2–998.8] | 5.80x | 1.30x | 0.68x | 10.1x | 9.87x |
| jq_mix | jq-breadth | 45.00s [44.92s–45.18s] | 44.32s [44.17s–44.61s] | 1.12s [1.10s–1.12s] | 598.16s [584.58s–600.00s] | --- [61.7–67.0] | 4.00s [4.00s–4.13s] | 11.2x | 11.1x | 0.28x | 149x | --- |
| jq_records | jq-data | 2.52s [2.50s–2.58s] | 2.56s [2.54s–2.58s] | 962.5 [956.9–981.9] | 151.47s [149.72s–152.63s] | 10.89s [10.81s–11.03s] | 2.06s [2.04s–2.18s] | 1.22x | 1.24x | 0.47x | 73.4x | 5.28x |
| jq_bf | jq-interpreter | 263.0 [261.1–264.9] | --- [10.6–11.5] | 1.92s [1.91s–1.94s] | 415.26s [413.86s–452.92s] | 27.78s [27.70s–27.87s] | 3.00s [2.97s–3.06s] | 0.09x | --- | 0.64x | 139x | 9.28x |
| jq_tree | jq-paths | 2.09s [2.08s–2.09s] | 1.75s [1.73s–1.84s] | 4.18s [4.15s–4.19s] | --- [600.00s–600.00s] | 47.67s [47.53s–47.68s] | 6.20s [5.79s–6.20s] | 0.34x | 0.28x | 0.68x | --- | 7.69x |

