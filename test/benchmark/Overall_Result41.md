# Lambda Benchmark Results: Result41

- **Date:** 2026-09-10
- **Platform:** Darwin arm64
- **Lambda commit:** `cbd220d923b14a3bc5a4e98453565f3fcea3ea04`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v41-cbd220d923` (19,289,832 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 64.30s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 64.2s (batched 63.4s: sync 48.7s, async 14.7s; non-batched 0.7s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v41.json`
- **Separately measured:** MIR (untyped), MIR (untyped, auto), MIR (typed), MIR (typed, auto) measured on 2026-09-10, 3 run(s) from `temp/benchmark_v41_fixed_mir.json` on Lambda commit `086c8685a6`. Result41 recovery refresh for awfy/havlak and jetstream/navier_stokes; 3 runs on cached release test/benchmark/exe/lambda-v41-086c8685a6 (SHA-256 34e2779603fc695c8439902e05753561bc97a24d892354f753ab2c55fae0bd28) after Test262 40261/40261, 0 regressions.
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.45x | 0.49x | 0.19x | 11.8x | 6.71x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 2.00x | 1.60x | 0.09x | 18.2x | 5.23x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.40x | 0.38x | 0.10x | 9.19x | 1.71x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 2.10x | 2.25x | 0.23x | 23.4x | 11.9x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 1.68x | 1.05x | 0.33x | 24.2x | 13.2x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 9.16x | 6.60x | 0.29x | 44.6x | 12.0x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 2.28x | 3.01x | 0.14x | 48.9x | 12.1x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 63 | 1.47x | 1.31x | 0.17x | 20.4x | 7.22x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 59 | 1.10x | 1.30x | 7.09x | 59 |
| complete current suite | 63 | 1.31x | 1.47x | 7.65x | 63 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 7.65x over 63 of 63 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/havlak | 3.58s | 1.83 | 1960x |
| text/prettier_ast | 9.64s | 41.6 | 231x |
| awfy/richards | 2.01s | 29.2 | 68.8x |
| jetstream/cube3d | 27.7 | 0.514 | 53.9x |
| awfy/deltablue | 47.4 | 1.16 | 41.0x |
| jetstream/navier_stokes | 1.79s | 47.1 | 38.0x |
| awfy/cd | 533.5 | 15.0 | 35.6x |
| text/hyphen | 2.25 | 0.088 | 25.6x |
| jetstream/hashmap | 70.4 | 2.77 | 25.4x |
| kostya/base64 | 13.7 | 0.558 | 24.5x |
| text/three_way_merge | 31.69s | 1.45s | 21.9x |
| text/fast_diff | 281.0 | 13.1 | 21.5x |

---

### Notable Results

- Missing timings: **0** cells

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| larceny/gcbench | 18.72s | 23.2 | 806x |
| beng/binarytrees | 779.8 | 3.99 | 196x |
| awfy/havlak | 18.39s | 94.8 | 194x |
| text/text_search | 114.13s | 776.7 | 147x |
| larceny/triangl | 7.77s | 66.6 | 117x |
| awfy/cd | 4.10s | 35.9 | 114x |
| jetstream/hashmap | 1.71s | 15.4 | 111x |
| awfy/nbody | 564.6 | 5.38 | 105x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.377 | 1.90 | 0.20x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.53 [1.52–1.55] | 1.57 [1.52–1.62] | 1.14 [1.13–1.18] | 47.2 [47.2–47.4] | 18.9 [18.9–19.0] | 1.83 [1.78–7.97] | 0.83x | 0.86x | 0.62x | 25.8x | 10.3x |
| fibfp | recursive | 2.06 [2.04–2.11] | 1.21 [1.21–1.21] | 1.15 [1.09–1.22] | 48.0 [47.3–54.1] | 26.4 [20.8–26.9] | 1.85 [1.81–1.88] | 1.11x | 0.65x | 0.62x | 25.9x | 14.3x |
| tak | recursive | 0.152 [0.151–0.153] | 0.204 [0.198–0.209] | 0.120 [0.113–0.122] | 3.29 [3.29–3.35] | 2.82 [2.78–2.83] | 0.805 [0.791–0.835] | 0.19x | 0.25x | 0.15x | 4.09x | 3.50x |
| cpstak | closure | 0.306 [0.303–0.317] | 0.394 [0.394–0.395] | 0.245 [0.236–0.245] | 6.50 [6.50–6.58] | 5.50 [5.48–5.57] | 0.992 [0.970–1.01] | 0.31x | 0.40x | 0.25x | 6.56x | 5.55x |
| sum | iterative | 1.20 [1.20–1.20] | 1.20 [1.20–1.20] | 0.271 [0.270–0.276] | 26.0 [26.0–26.2] | 31.0 [31.0–31.1] | 1.22 [1.20–1.89] | 0.98x | 0.98x | 0.22x | 21.3x | 25.4x |
| sumfp | iterative | 0.068 [0.068–0.069] | 0.068 [0.068–0.069] | 0.081 [0.077–0.083] | 2.58 [2.58–2.66] | 3.73 [3.62–3.77] | 0.847 [0.842–1.73] | 0.08x | 0.08x | 0.10x | 3.04x | 4.40x |
| nqueens | backtrack | 1.76 [1.70–1.77] | 2.52 [2.52–2.58] | 0.130 [0.130–0.131] | 37.3 [37.1–96.8] | 7.80 [7.76–8.21] | 1.74 [1.73–2.75] | 1.01x | 1.45x | 0.07x | 21.5x | 4.49x |
| fft | numeric | 0.285 [0.280–0.314] | 0.348 [0.345–0.376] | 0.024 [0.024–0.026] | 22.7 [22.6–22.8] | 2.75 [2.73–2.75] | 1.58 [1.55–1.59] | 0.18x | 0.22x | 0.02x | 14.4x | 1.74x |
| mbrot | numeric | 0.711 [0.710–0.765] | 0.922 [0.911–0.979] | 0.446 [0.445–0.455] | 13.3 [13.2–13.4] | 17.6 [17.6–17.7] | 1.81 [1.75–1.84] | 0.39x | 0.51x | 0.25x | 7.36x | 9.73x |
| ack | recursive | 13.4 [13.1–14.6] | 15.5 [15.4–16.0] | 11.4 [11.2–11.7] | 253.2 [251.7–253.5] | 101.2 [100.6–101.3] | 13.3 [13.3–13.3] | 1.01x | 1.17x | 0.86x | 19.1x | 7.62x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.039 [0.038–0.039] | 0.135 [0.135–0.135] | 0.015 [0.015–0.016] | 1.26 [1.25–1.30] | 0.621 [0.604–0.623] | 0.381 [0.377–0.389] | 0.10x | 0.35x | 0.04x | 3.30x | 1.63x |
| permute | micro | 0.549 [0.545–0.559] | 0.174 [0.173–0.179] | 0.027 [0.025–0.027] | 7.89 [7.85–7.93] | 1.54 [1.53–1.56] | 0.808 [0.800–0.810] | 0.68x | 0.22x | 0.03x | 9.77x | 1.91x |
| queens | micro | 0.407 [0.406–0.411] | 0.263 [0.255–0.272] | 0.022 [0.017–0.022] | 4.38 [4.36–4.38] | 1.06 [1.06–1.08] | 0.640 [0.633–0.665] | 0.64x | 0.41x | 0.03x | 6.84x | 1.66x |
| towers | micro | 1.25 [1.24–1.26] | 0.447 [0.443–0.448] | 0.027 [0.025–0.029] | 15.7 [15.5–15.9] | 2.21 [2.19–2.26] | 1.12 [1.10–1.13] | 1.12x | 0.40x | 0.02x | 14.1x | 1.98x |
| bounce | micro | 0.073 [0.072–0.078] | 0.110 [0.109–0.113] | 0.025 [0.024–0.028] | 3.42 [3.36–3.45] | 0.900 [0.876–0.906] | 0.540 [0.540–0.552] | 0.14x | 0.20x | 0.05x | 6.32x | 1.67x |
| list | micro | 0.725 [0.720–0.730] | 0.213 [0.206–0.214] | 0.023 [0.021–0.023] | 2.14 [2.12–2.16] | 0.906 [0.903–0.911] | 0.485 [0.482–0.486] | 1.49x | 0.44x | 0.05x | 4.41x | 1.87x |
| storage | micro | 0.683 [0.663–0.683] | 0.356 [0.352–0.377] | 0.091 [0.090–0.093] | 5.75 [5.68–5.78] | 2.07 [2.05–2.08] | 0.632 [0.624–0.637] | 1.08x | 0.56x | 0.14x | 9.09x | 3.27x |
| mandelbrot | compute | 44.1 [44.1–44.1] | 44.1 [44.1–44.1] | 30.7 [30.6–30.7] | 448.0 [444.9–502.3] | 875.6 [869.8–902.2] | 31.6 [31.1–31.6] | 1.40x | 1.40x | 0.97x | 14.2x | 27.7x |
| nbody | compute | 52.1 [51.8–99.6] | 25.5 [25.4–25.5] | 1.49 [1.48–1.52] | 564.6 [562.0–566.0] | 159.4 [157.9–159.5] | 5.38 [5.37–5.79] | 9.69x | 4.73x | 0.28x | 105x | 29.6x |
| richards | macro | 427.4 [427.1–428.3] | 2.01s [1.98s–2.04s] | 29.2 [28.9–29.3] | 1.09s [1.09s–1.09s] | 193.3 [190.4–201.9] | 46.9 [46.9–47.1] | 9.11x | 42.9x | 0.62x | 23.2x | 4.12x |
| json | macro | 6.13 [6.12–6.15] | 4.62 [4.62–4.66] | 0.267 [0.257–0.275] | 44.1 [44.0–44.2] | 10.7 [10.7–11.1] | 2.62 [2.59–2.64] | 2.34x | 1.77x | 0.10x | 16.9x | 4.10x |
| deltablue | macro | 132.7 [132.4–133.3] | 47.4 [47.2–99.0] | 1.16 [1.15–1.16] | 501.2 [500.1–509.7] | 99.8 [99.5–100.5] | 11.7 [11.6–12.9] | 11.3x | 4.05x | 0.10x | 42.8x | 8.53x |
| havlak | macro | 2.49s [2.48s–2.56s] | 3.58s [3.57s–3.60s] | 1.83 [1.79–1.88] | 18.39s [18.29s–18.41s] | 3.33s [3.29s–3.36s] | 94.8 [93.8–95.4] | 26.3x | 37.8x | 0.02x | 194x | 35.1x |
| cd | macro | 635.8 [634.0–694.0] | 533.5 [531.5–534.6] | 15.0 [15.0–15.1] | 4.10s [4.08s–4.18s] | 963.0 [956.7–964.7] | 35.9 [35.8–36.1] | 17.7x | 14.9x | 0.42x | 114x | 26.8x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 8.91 [8.77–8.92] | 2.30 [2.28–2.32] | 3.01 [3.00–3.09] | 779.8 [777.6–791.4] | 23.4 [23.3–23.5] | 3.99 [3.98–5.11] | 2.23x | 0.58x | 0.76x | 196x | 5.86x |
| fannkuch | permutation | 0.442 [0.436–0.464] | 0.722 [0.721–0.746] | 0.153 [0.149–0.155] | 24.8 [24.8–25.0] | 7.24 [7.14–7.26] | 4.09 [3.94–4.17] | 0.11x | 0.18x | 0.04x | 6.07x | 1.77x |
| fasta | generation | 0.844 [0.818–0.849] | 0.917 [0.905–0.931] | 0.241 [0.241–0.244] | 30.3 [30.1–30.5] | 8.90 [8.90–8.99] | 6.18 [6.05–6.26] | 0.14x | 0.15x | 0.04x | 4.90x | 1.44x |
| knucleotide | hashing | 4.50 [4.50–4.75] | 5.32 [5.24–5.32] | 0.281 [0.279–0.283] | 164.2 [164.1–214.8] | 7.67 [7.63–7.68] | 4.91 [4.86–5.82] | 0.92x | 1.08x | 0.06x | 33.4x | 1.56x |
| pidigits | bignum | 0.308 [0.299–0.309] | 0.304 [0.302–0.310] | 0.049 [0.045–0.051] | 0.377 [0.365–0.392] | 0.129 [0.125–0.129] | 1.90 [1.90–1.94] | 0.16x | 0.16x | 0.03x | 0.20x | 0.07x |
| regexredux | regex | 1.29 [1.26–1.29] | 1.29 [1.29–1.33] | 1.14 [1.13–1.15] | 26.0 [26.0–26.2] | 5.60 [5.57–5.68] | 2.40 [2.38–2.51] | 0.54x | 0.54x | 0.47x | 10.8x | 2.33x |
| revcomp | string | 1.26 [1.26–1.27] | 1.21 [1.21–1.23] | 0.375 [0.373–0.386] | 26.4 [26.3–26.4] | 2.58 [2.53–2.62] | 3.31 [3.29–3.32] | 0.38x | 0.37x | 0.11x | 7.95x | 0.78x |
| spectralnorm | numeric | 1.77 [1.77–1.83] | 2.05 [2.03–2.06] | 0.355 [0.351–0.356] | 38.7 [38.6–38.8] | 64.3 [63.2–64.9] | 2.52 [2.51–2.60] | 0.70x | 0.81x | 0.14x | 15.4x | 25.5x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 223.9 [223.0–225.1] | 346.9 [343.9–386.7] | 28.3 [27.7–28.8] | 2.12s [2.11s–2.13s] | 884.8 [882.3–886.0] | 33.5 [33.5–33.6] | 6.68x | 10.3x | 0.84x | 63.3x | 26.4x |
| matmul | numeric | 33.7 [33.7–33.9] | 42.8 [42.6–42.9] | 6.06 [6.06–6.09] | 661.5 [660.4–725.2] | 538.1 [538.0–538.4] | 15.3 [15.3–15.5] | 2.20x | 2.79x | 0.39x | 43.1x | 35.1x |
| primes | numeric | 14.6 [14.5–14.6] | 23.2 [23.1–23.3] | 1.58 [1.58–1.64] | 135.5 [134.8–135.9] | 94.4 [94.4–94.5] | 4.51 [4.47–4.55] | 3.23x | 5.15x | 0.35x | 30.0x | 20.9x |
| base64 | string | 19.7 [19.6–19.8] | 13.7 [13.7–13.7] | 0.558 [0.558–0.565] | 850.7 [849.9–873.4] | 157.2 [156.1–157.3] | 17.4 [17.3–17.4] | 1.13x | 0.79x | 0.03x | 48.9x | 9.04x |
| levenshtein | string | 11.5 [11.4–11.6] | 8.06 [7.96–8.18] | 0.903 [0.898–0.950] | 123.2 [121.8–123.4] | 54.2 [54.0–108.2] | 3.96 [3.93–3.97] | 2.89x | 2.03x | 0.23x | 31.1x | 13.7x |
| json_gen | data | 24.7 [24.4–24.9] | 26.3 [26.0–26.5] | 1.52 [1.49–1.52] | 46.4 [46.4–46.6] | 20.0 [19.7–20.1] | 6.25 [6.17–6.32] | 3.94x | 4.21x | 0.24x | 7.42x | 3.19x |
| collatz | numeric | 411.4 [411.2–414.7] | 412.4 [410.9–414.6] | 226.0 [225.4–226.0] | 5.84s [5.84s–5.96s] | 6.22s [6.22s–6.29s] | 1.42s [1.42s–1.44s] | 0.29x | 0.29x | 0.16x | 4.12x | 4.38x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 284.6 [283.9–286.3] | 263.0 [262.6–263.0] | 60.4 [60.2–60.8] | 7.77s [7.77s–7.87s] | 2.19s [2.18s–2.19s] | 66.6 [66.6–66.8] | 4.27x | 3.95x | 0.91x | 117x | 32.9x |
| array1 | array | 1.08 [1.08–1.09] | 1.72 [1.69–1.74] | 0.318 [0.316–0.319] | 35.9 [35.9–86.4] | 35.8 [35.7–36.1] | 1.91 [1.89–1.92] | 0.57x | 0.90x | 0.17x | 18.8x | 18.8x |
| deriv | symbolic | 29.7 [29.7–29.9] | 7.93 [7.80–7.97] | 2.85 [2.82–2.85] | 83.1 [82.8–83.3] | 58.8 [58.7–59.2] | 3.79 [3.78–3.80] | 7.83x | 2.09x | 0.75x | 21.9x | 15.5x |
| diviter | iterative | 532.5 [532.3–532.6] | 532.9 [532.5–533.1] | 261.1 [259.4–261.2] | 11.65s [11.61s–13.03s] | 26.72s [26.70s–26.72s] | 469.5 [469.5–470.8] | 1.13x | 1.14x | 0.56x | 24.8x | 56.9x |
| divrec | recursive | 6.21 [6.01–6.25] | 2.55 [2.55–2.56] | 4.86 [4.85–4.87] | 50.8 [50.6–50.9] | 36.2 [35.7–36.3] | 7.61 [7.53–7.66] | 0.82x | 0.34x | 0.64x | 6.67x | 4.76x |
| gcbench | allocation | 204.6 [203.9–204.7] | 86.8 [86.7–87.5] | 70.8 [70.7–71.6] | 18.72s [18.61s–18.92s] | 546.4 [545.9–561.1] | 23.2 [23.2–23.4] | 8.81x | 3.74x | 3.05x | 806x | 23.5x |
| paraffins | combinat | 0.310 [0.308–0.311] | 0.375 [0.368–0.389] | 0.048 [0.048–0.052] | 2.43 [2.42–2.48] | 2.50 [2.49–2.50] | 0.999 [0.986–1.01] | 0.31x | 0.38x | 0.05x | 2.43x | 2.50x |
| pnpoly | numeric | 14.0 [13.8–14.0] | 18.6 [18.5–18.7] | 1.96 [1.95–1.97] | 107.5 [106.6–108.0] | 201.6 [201.4–202.4] | 5.83 [5.76–5.89] | 2.40x | 3.20x | 0.34x | 18.5x | 34.6x |
| puzzle | search | 15.0 [14.9–15.1] | 3.16 [3.15–3.17] | 1.28 [1.27–1.28] | 46.4 [45.7–46.7] | 29.4 [29.3–29.6] | 3.28 [3.27–3.30] | 4.57x | 0.96x | 0.39x | 14.1x | 8.98x |
| quicksort | sorting | 11.2 [11.2–11.3] | 1.86 [1.83–1.88] | 0.198 [0.198–0.199] | 146.0 [145.6–148.7] | 19.2 [19.2–19.3] | 1.63 [1.61–1.64] | 6.88x | 1.14x | 0.12x | 89.3x | 11.8x |
| ray | numeric | 0.310 [0.305–0.315] | 0.440 [0.435–0.461] | 0.173 [0.172–0.174] | 16.5 [16.3–16.6] | 13.8 [13.7–14.0] | 3.66 [3.52–3.69] | 0.08x | 0.12x | 0.05x | 4.51x | 3.78x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 25.1 [25.1–25.2] | 27.7 [27.6–28.0] | 0.514 [0.513–0.522] | 388.3 [387.8–390.8] | 217.0 [215.9–217.0] | 17.8 [17.7–17.9] | 1.41x | 1.56x | 0.03x | 21.8x | 12.2x |
| navier_stokes | numeric | 1.76s [1.74s–1.81s] | 1.79s [1.79s–1.84s] | 47.1 [47.0–48.4] | 349.7 [347.2–352.9] | 98.7 [98.5–98.9] | 14.0 [13.9–14.1] | 126x | 128x | 3.36x | 25.0x | 7.04x |
| splay | data | 317.4 [316.8–322.7] | 317.7 [316.5–317.9] | 19.2 [19.0–19.3] | 1.78s [1.76s–1.88s] | 146.9 [145.5–147.7] | 19.2 [18.5–19.4] | 16.5x | 16.5x | 1.00x | 92.7x | 7.64x |
| hashmap | data | 121.9 [121.4–122.9] | 70.4 [70.3–73.1] | 2.77 [2.72–2.83] | 1.71s [1.71s–1.74s] | 314.9 [309.3–316.6] | 15.4 [15.0–15.5] | 7.91x | 4.57x | 0.18x | 111x | 20.4x |
| crypto_sha1 | crypto | 53.9 [53.8–55.6] | 35.1 [34.7–35.7] | 2.65 [2.60–2.67] | 377.3 [374.8–378.8] | 218.8 [218.0–219.7] | 8.69 [8.30–8.82] | 6.20x | 4.04x | 0.31x | 43.4x | 25.2x |
| raytrace3d | 3d | 74.8 [74.3–75.0] | 24.8 [24.6–80.0] | 2.18 [2.16–2.18] | 590.7 [588.2–591.4] | 161.7 [161.5–162.7] | 18.2 [18.2–18.4] | 4.11x | 1.36x | 0.12x | 32.5x | 8.90x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 377.2 [376.0–379.6] | 281.0 [280.7–281.2] | 13.1 [13.0–13.1] | 1.95s [1.93s–1.95s] | 611.2 [610.1–624.7] | 39.5 [39.4–39.5] | 9.56x | 7.12x | 0.33x | 49.3x | 15.5x |
| microdiff | data-diff | 0.175 [0.174–0.186] | 0.181 [0.175–0.184] | 0.017 [0.016–0.018] | 1.18s [1.17s–1.19s] | 109.0 [108.6–109.2] | 16.3 [16.2–17.3] | 0.01x | 0.01x | 0.001x | 72.0x | 6.68x |
| hyphen | hyphenation | 3.45 [3.44–3.48] | 2.25 [2.19–3.21] | 0.088 [0.086–0.091] | 346.8 [344.5–362.9] | 51.0 [50.7–51.3] | 6.53 [6.36–7.33] | 0.53x | 0.35x | 0.01x | 53.1x | 7.81x |
| prettier_ast | formatting | 1.10s [1.08s–1.11s] | 9.64s [9.55s–9.76s] | 41.6 [41.6–41.7] | 5.75s [5.71s–5.77s] | 1.43s [1.41s–1.44s] | 99.8 [99.4–100.0] | 11.0x | 96.6x | 0.42x | 57.6x | 14.4x |
| text_search | search | 15.65s [15.62s–15.71s] | 2.81s [2.81s–2.82s] | 538.6 [538.5–538.6] | 114.13s [114.01s–114.59s] | 38.96s [38.88s–38.97s] | 776.7 [776.2–779.3] | 20.1x | 3.62x | 0.69x | 147x | 50.2x |
| three_way_merge | merge | 3.80s [3.80s–3.83s] | 31.69s [31.60s–31.70s] | 1.45s [1.44s–1.49s] | 20.22s [20.19s–20.44s] | 6.13s [6.12s–6.28s] | 966.6 [966.2–966.8] | 3.93x | 32.8x | 1.50x | 20.9x | 6.34x |
| log_pipeline | log-processing | 6.42s [6.42s–6.45s] | 6.74s [6.74s–6.87s] | 571.3 [558.0–571.6] | 19.05s [19.03s–19.17s] | 9.55s [9.48s–9.56s] | 945.2 [943.3–963.6] | 6.80x | 7.14x | 0.60x | 20.2x | 10.1x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.41x | 0.42x | 1.02x | 1.31x | 0.43x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.35x | 1.57x | 0.91x | 4.60x | 0.79x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.48x | 0.51x | 1.00x | 2.04x | 0.32x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 0.82x | 0.92x | 0.72x | 6.17x | 2.88x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 0.89x | 0.75x | 0.99x | 4.92x | 1.89x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 4.47x | 6.52x | 0.91x | 14.8x | 3.67x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 3.12x | 4.28x | 0.80x | 31.8x | 6.49x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 63 | 1.06x | 1.17x | 0.92x | 4.92x | 1.26x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 17.5 [17.3–17.7] | 17.3 [17.3–17.9] | 48.2 [47.9–487.8] | 73.8 [72.9–74.6] | 25.1 [24.9–27.2] | 45.6 [45.3–92.5] | 0.38x | 0.38x | 1.06x | 1.62x | 0.55x |
| fibfp | recursive | 17.3 [17.1–17.3] | 16.4 [16.2–16.5] | 47.0 [46.8–47.4] | 73.8 [73.2–81.4] | 35.4 [29.4–35.6] | 47.2 [46.7–49.8] | 0.37x | 0.35x | 1.00x | 1.56x | 0.75x |
| tak | recursive | 16.1 [16.0–16.3] | 15.8 [15.8–15.9] | 46.1 [45.7–46.2] | 31.8 [31.6–32.5] | 8.54 [8.36–9.19] | 44.5 [43.7–44.5] | 0.36x | 0.36x | 1.04x | 0.72x | 0.19x |
| cpstak | closure | 16.2 [16.0–16.3] | 15.7 [15.7–16.1] | 47.3 [46.1–47.7] | 34.8 [34.6–35.3] | 10.9 [10.8–11.4] | 44.2 [43.0–44.4] | 0.37x | 0.36x | 1.07x | 0.79x | 0.25x |
| sum | iterative | 16.2 [16.1–16.2] | 16.4 [16.1–16.5] | 45.1 [44.9–45.2] | 51.7 [50.6–52.3] | 36.9 [36.8–36.9] | 43.9 [43.9–44.7] | 0.37x | 0.37x | 1.03x | 1.18x | 0.84x |
| sumfp | iterative | 14.8 [14.7–14.8] | 14.5 [14.3–14.7] | 44.9 [44.7–44.9] | 27.4 [27.3–28.4] | 9.14 [9.08–9.56] | 43.7 [43.5–44.1] | 0.34x | 0.33x | 1.03x | 0.63x | 0.21x |
| nqueens | backtrack | 24.3 [24.3–24.4] | 28.1 [28.1–28.4] | 44.8 [44.7–45.0] | 107.9 [107.3–168.0] | 14.1 [13.4–14.7] | 45.0 [44.9–45.2] | 0.54x | 0.62x | 1.00x | 2.40x | 0.31x |
| fft | numeric | 20.6 [20.6–21.0] | 27.2 [27.1–27.2] | 45.0 [44.9–45.2] | 57.6 [57.4–58.2] | 8.10 [8.10–8.82] | 44.2 [44.0–45.1] | 0.47x | 0.61x | 1.02x | 1.30x | 0.18x |
| mbrot | numeric | 19.3 [18.9–19.3] | 18.7 [18.6–19.2] | 44.8 [44.7–44.9] | 42.0 [41.8–42.6] | 23.3 [23.0–23.5] | 44.4 [44.0–44.5] | 0.44x | 0.42x | 1.01x | 0.95x | 0.53x |
| ack | recursive | 29.6 [28.5–30.3] | 31.2 [31.0–32.4] | 56.5 [56.5–56.8] | 278.9 [277.1–279.4] | 107.4 [106.6–107.5] | 56.0 [55.8–56.1] | 0.53x | 0.56x | 1.01x | 4.98x | 1.92x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 15.5 [15.5–15.5] | 16.2 [16.0–16.3] | 44.6 [44.5–44.9] | 32.1 [31.6–32.3] | 5.92 [5.75–6.42] | 44.0 [43.9–44.5] | 0.35x | 0.37x | 1.02x | 0.73x | 0.13x |
| permute | micro | 16.9 [16.6–16.9] | 17.4 [17.4–17.6] | 44.8 [44.5–44.9] | 40.7 [40.6–41.2] | 6.94 [6.85–7.56] | 44.3 [44.3–44.4] | 0.38x | 0.39x | 1.01x | 0.92x | 0.16x |
| queens | micro | 21.1 [21.0–21.1] | 24.9 [24.8–25.0] | 45.1 [44.9–45.3] | 41.3 [41.3–42.2] | 6.43 [6.31–6.98] | 44.1 [43.8–93.9] | 0.48x | 0.57x | 1.02x | 0.94x | 0.15x |
| towers | micro | 21.4 [21.1–21.7] | 22.5 [22.3–22.6] | 45.6 [45.3–46.0] | 53.1 [52.9–53.3] | 7.78 [7.62–8.31] | 45.3 [44.9–45.3] | 0.47x | 0.50x | 1.01x | 1.17x | 0.17x |
| bounce | micro | 24.0 [23.9–24.1] | 44.0 [44.0–45.0] | 45.5 [45.0–45.6] | 117.8 [116.4–118.4] | 7.10 [6.93–7.70] | 44.5 [44.2–45.0] | 0.54x | 0.99x | 1.02x | 2.64x | 0.16x |
| list | micro | 19.2 [18.9–19.2] | 19.1 [18.9–19.3] | 45.0 [44.7–45.1] | 36.6 [36.3–37.3] | 6.44 [6.17–6.94] | 43.7 [43.7–43.8] | 0.44x | 0.44x | 1.03x | 0.84x | 0.15x |
| storage | micro | 17.0 [16.8–17.2] | 17.8 [17.7–17.9] | 45.0 [44.8–45.1] | 111.2 [111.2–112.9] | 8.15 [8.00–8.88] | 44.7 [44.5–44.9] | 0.38x | 0.40x | 1.01x | 2.49x | 0.18x |
| mandelbrot | compute | 61.1 [61.0–61.1] | 61.2 [61.1–61.3] | 75.7 [75.2–75.8] | 487.3 [483.5–541.1] | 882.3 [876.2–908.9] | 78.7 [78.3–78.8] | 0.78x | 0.78x | 0.96x | 6.19x | 11.2x |
| nbody | compute | 83.5 [83.0–84.1] | 91.1 [91.0–91.3] | 47.9 [47.5–48.3] | 630.6 [626.9–631.2] | 165.8 [164.3–166.1] | 49.2 [49.2–51.0] | 1.70x | 1.85x | 0.97x | 12.8x | 3.37x |
| richards | macro | 481.2 [479.2–482.3] | 2.05s [2.03s–2.16s] | 77.4 [76.8–77.5] | 1.19s [1.19s–1.20s] | 200.3 [197.3–209.0] | 93.4 [92.7–93.5] | 5.15x | 21.9x | 0.83x | 12.8x | 2.14x |
| json | macro | 51.7 [51.1–51.9] | 53.0 [52.5–53.8] | 52.7 [52.2–53.0] | 239.8 [238.0–239.9] | 18.2 [17.8–19.0] | 46.9 [46.9–47.8] | 1.10x | 1.13x | 1.12x | 5.11x | 0.39x |
| deltablue | macro | 244.7 [243.2–244.8] | 189.9 [189.4–190.3] | 53.7 [53.5–53.8] | 727.7 [726.6–740.1] | 107.5 [107.4–108.1] | 58.7 [57.1–59.4] | 4.17x | 3.23x | 0.92x | 12.4x | 1.83x |
| havlak | macro | 13.27s [13.26s–13.40s] | 12.91s [12.89s–13.01s] | 54.0 [53.7–54.4] | 18.72s [18.61s–18.72s] | 3.34s [3.31s–3.37s] | 142.5 [141.8–142.5] | 93.1x | 90.6x | 0.38x | 131x | 23.5x |
| cd | macro | 701.0 [700.7–701.0] | 595.1 [593.0–597.4] | 67.5 [67.4–68.2] | 4.42s [4.40s–4.50s] | 971.3 [964.8–972.7] | 80.7 [80.5–81.8] | 8.68x | 7.37x | 0.84x | 54.8x | 12.0x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 28.8 [28.8–38.8] | 23.0 [22.8–23.2] | 49.0 [48.5–49.0] | 812.9 [810.0–824.0] | 29.3 [29.0–29.7] | 46.7 [46.5–47.4] | 0.62x | 0.49x | 1.05x | 17.4x | 0.63x |
| fannkuch | permutation | 21.2 [21.0–21.7] | 27.2 [27.1–27.3] | 45.4 [45.2–45.5] | 56.4 [56.2–57.0] | 12.6 [12.5–13.2] | 47.1 [46.0–47.1] | 0.45x | 0.58x | 0.96x | 1.20x | 0.27x |
| fasta | generation | 23.6 [23.3–23.9] | 25.3 [25.3–25.3] | 45.5 [45.4–45.6] | 69.4 [68.4–69.8] | 14.6 [14.5–15.2] | 47.6 [47.3–47.9] | 0.50x | 0.53x | 0.96x | 1.46x | 0.31x |
| knucleotide | hashing | 29.8 [29.6–30.4] | 37.2 [36.8–37.4] | 46.2 [45.9–46.8] | 207.2 [199.1–250.0] | 13.5 [13.3–13.9] | 47.7 [47.1–47.9] | 0.62x | 0.78x | 0.97x | 4.35x | 0.28x |
| pidigits | bignum | 19.6 [19.5–19.6] | 19.8 [19.6–19.8] | 46.9 [46.7–47.1] | 35.6 [35.1–36.1] | 5.46 [5.37–5.97] | 44.2 [43.3–44.3] | 0.44x | 0.45x | 1.06x | 0.80x | 0.12x |
| regexredux | regex | 14.6 [14.6–14.9] | 14.7 [14.6–15.1] | 48.1 [47.8–48.2] | 61.0 [61.0–61.4] | 11.4 [11.0–11.8] | 45.0 [44.3–45.1] | 0.33x | 0.33x | 1.07x | 1.36x | 0.25x |
| revcomp | string | 20.9 [20.7–20.9] | 21.7 [21.4–22.1] | 45.7 [45.6–46.2] | 63.2 [63.1–63.5] | 8.16 [8.09–8.82] | 46.2 [45.5–46.4] | 0.45x | 0.47x | 0.99x | 1.37x | 0.18x |
| spectralnorm | numeric | 23.6 [23.5–23.7] | 25.9 [25.8–26.2] | 45.4 [45.2–46.1] | 70.4 [70.2–71.2] | 70.6 [69.4–71.0] | 46.6 [45.7–46.9] | 0.51x | 0.55x | 0.97x | 1.51x | 1.51x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 248.0 [247.3–248.9] | 370.9 [367.0–371.3] | 74.1 [73.0–74.2] | 2.16s [2.15s–2.16s] | 891.2 [888.8–892.6] | 78.0 [77.8–78.4] | 3.18x | 4.76x | 0.95x | 27.7x | 11.4x |
| matmul | numeric | 54.3 [53.8–54.3] | 64.4 [64.3–64.6] | 51.0 [50.8–51.1] | 694.6 [694.2–757.9] | 545.5 [545.4–545.9] | 58.2 [58.0–58.3] | 0.93x | 1.11x | 0.88x | 11.9x | 9.38x |
| primes | numeric | 30.6 [30.4–30.6] | 39.9 [39.6–40.1] | 46.6 [46.4–47.0] | 162.9 [161.9–163.5] | 100.8 [100.7–100.9] | 47.5 [47.3–47.6] | 0.64x | 0.84x | 0.98x | 3.43x | 2.12x |
| base64 | string | 41.5 [41.2–41.6] | 43.3 [43.0–44.0] | 45.6 [45.6–46.1] | 884.0 [883.2–907.4] | 163.5 [162.7–163.8] | 60.7 [60.6–61.1] | 0.68x | 0.71x | 0.75x | 14.6x | 2.69x |
| levenshtein | string | 35.0 [34.8–35.0] | 29.9 [29.9–30.0] | 46.4 [46.2–46.4] | 156.3 [155.4–157.2] | 60.6 [60.2–114.1] | 48.0 [48.0–48.1] | 0.73x | 0.62x | 0.97x | 3.25x | 1.26x |
| json_gen | data | 47.5 [46.6–62.5] | 49.1 [48.8–50.1] | 46.6 [46.2–46.9] | 79.5 [79.5–80.3] | 25.6 [25.4–26.2] | 50.5 [50.0–50.7] | 0.94x | 0.97x | 0.92x | 1.58x | 0.51x |
| collatz | numeric | 426.7 [423.8–426.9] | 428.4 [426.8–429.1] | 271.2 [270.8–271.5] | 5.87s [5.87s–5.99s] | 6.23s [6.23s–6.30s] | 1.46s [1.46s–1.48s] | 0.29x | 0.29x | 0.19x | 4.01x | 4.25x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 311.2 [310.5–311.5] | 293.4 [293.1–294.5] | 106.2 [105.7–106.3] | 7.81s [7.80s–7.91s] | 2.20s [2.19s–2.20s] | 111.0 [110.9–111.4] | 2.80x | 2.64x | 0.96x | 70.3x | 19.8x |
| array1 | array | 16.1 [16.1–16.2] | 17.2 [17.2–17.6] | 45.2 [45.0–45.2] | 63.0 [62.9–113.3] | 41.8 [41.4–41.9] | 45.8 [45.5–45.9] | 0.35x | 0.38x | 0.99x | 1.37x | 0.91x |
| deriv | symbolic | 54.0 [53.2–54.3] | 28.1 [28.0–28.4] | 48.6 [48.6–48.9] | 115.6 [115.2–115.7] | 65.5 [64.7–65.6] | 47.2 [47.2–47.5] | 1.14x | 0.59x | 1.03x | 2.45x | 1.39x |
| diviter | iterative | 549.1 [548.8–550.2] | 549.7 [549.7–549.9] | 306.0 [304.6–306.2] | 11.68s [11.64s–13.06s] | 26.73s [26.70s–26.73s] | 514.5 [514.3–516.1] | 1.07x | 1.07x | 0.59x | 22.7x | 52.0x |
| divrec | recursive | 22.3 [22.3–22.4] | 19.3 [19.1–19.7] | 49.9 [49.8–50.0] | 79.3 [79.1–132.7] | 42.2 [41.3–42.3] | 51.8 [51.6–52.0] | 0.43x | 0.37x | 0.96x | 1.53x | 0.82x |
| gcbench | allocation | 227.9 [227.0–229.2] | 130.1 [125.6–132.5] | 116.5 [116.2–117.1] | 18.76s [18.65s–18.96s] | 558.5 [557.5–573.1] | 66.7 [66.5–67.6] | 3.42x | 1.95x | 1.75x | 281x | 8.37x |
| paraffins | combinat | 41.6 [41.0–41.9] | 47.6 [47.1–48.1] | 45.9 [45.9–46.0] | 42.9 [42.5–43.0] | 8.01 [7.83–8.65] | 45.2 [44.5–45.6] | 0.92x | 1.05x | 1.02x | 0.95x | 0.18x |
| pnpoly | numeric | 32.0 [32.0–32.3] | 39.0 [38.7–39.2] | 47.4 [47.2–47.5] | 137.8 [137.7–138.6] | 208.0 [207.8–208.9] | 50.4 [50.1–51.0] | 0.64x | 0.77x | 0.94x | 2.73x | 4.13x |
| puzzle | search | 34.0 [33.2–84.5] | 23.2 [23.2–23.3] | 46.6 [46.1–46.9] | 76.6 [76.1–76.7] | 35.1 [34.9–35.4] | 47.1 [46.9–47.8] | 0.72x | 0.49x | 0.99x | 1.63x | 0.75x |
| quicksort | sorting | 30.8 [30.6–30.8] | 23.4 [23.3–23.6] | 45.8 [45.5–46.1] | 178.1 [176.9–180.4] | 25.4 [24.9–25.6] | 45.6 [45.3–45.7] | 0.68x | 0.51x | 1.01x | 3.91x | 0.56x |
| ray | numeric | 26.8 [26.8–27.0] | 20.3 [20.2–20.4] | 45.4 [45.2–45.6] | 50.1 [49.9–51.1] | 19.7 [19.4–19.8] | 47.1 [45.7–47.2] | 0.57x | 0.43x | 0.96x | 1.06x | 0.42x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 171.0 [170.8–171.1] | 386.1 [384.9–386.9] | 49.4 [49.3–50.1] | 495.4 [494.3–498.1] | 223.8 [222.8–223.9] | 63.5 [62.7–64.8] | 2.69x | 6.08x | 0.78x | 7.80x | 3.52x |
| navier_stokes | numeric | 2.38s [2.37s–2.39s] | 2.46s [2.43s–2.46s] | 95.5 [95.4–97.6] | 524.2 [521.6–529.9] | 109.8 [109.3–109.8] | 61.1 [60.3–61.2] | 38.9x | 40.2x | 1.56x | 8.58x | 1.80x |
| splay | data | 382.3 [379.7–382.3] | 377.0 [376.8–378.8] | 66.8 [66.6–67.0] | 5.37s [5.37s–5.38s] | 565.1 [561.8–569.3] | 92.4 [92.4–92.5] | 4.14x | 4.08x | 0.72x | 58.2x | 6.12x |
| hashmap | data | 153.1 [151.8–153.1] | 103.7 [103.5–105.3] | 48.6 [48.5–48.7] | 1.82s [1.82s–1.85s] | 322.3 [316.6–323.9] | 59.6 [58.9–60.1] | 2.57x | 1.74x | 0.82x | 30.6x | 5.41x |
| crypto_sha1 | crypto | 90.6 [88.9–146.2] | 684.4 [683.1–697.2] | 48.9 [48.4–49.4] | 423.0 [421.6–424.6] | 225.5 [224.7–226.2] | 52.1 [52.1–52.5] | 1.74x | 13.1x | 0.94x | 8.12x | 4.33x |
| raytrace3d | 3d | 258.2 [256.7–259.6] | 210.6 [209.6–211.1] | 52.5 [52.3–52.5] | 688.4 [686.7–689.1] | 168.4 [168.4–169.7] | 62.5 [61.7–62.5] | 4.13x | 3.37x | 0.84x | 11.0x | 2.69x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 411.6 [409.6–411.7] | 307.8 [303.2–367.4] | 59.1 [59.1–59.1] | 2.10s [2.08s–2.11s] | 619.0 [617.8–632.6] | 84.8 [84.5–84.9] | 4.85x | 3.63x | 0.70x | 24.7x | 7.30x |
| microdiff | data-diff | 17.8 [17.8–18.2] | 17.7 [17.6–18.1] | 45.1 [44.7–45.1] | 1.23s [1.23s–1.25s] | 115.2 [115.1–115.9] | 61.0 [60.9–61.5] | 0.29x | 0.29x | 0.74x | 20.2x | 1.89x |
| hyphen | hyphenation | 27.3 [27.2–27.3] | 25.0 [25.0–25.3] | 45.8 [45.7–46.0] | 1.58s [1.57s–1.58s] | 68.1 [67.6–68.1] | 53.0 [53.0–53.9] | 0.51x | 0.47x | 0.86x | 29.8x | 1.28x |
| prettier_ast | formatting | 1.18s [1.18s–1.19s] | 10.14s [10.11s–10.14s] | 104.2 [103.8–105.2] | 5.96s [5.92s–6.01s] | 1.44s [1.42s–1.45s] | 145.8 [145.2–146.0] | 8.11x | 69.5x | 0.71x | 40.9x | 9.89x |
| text_search | search | 15.65s [15.65s–15.73s] | 2.88s [2.85s–2.90s] | 586.0 [585.9–586.2] | 114.23s [114.11s–114.68s] | 38.97s [38.89s–38.98s] | 822.0 [821.6–824.6] | 19.0x | 3.51x | 0.71x | 139x | 47.4x |
| three_way_merge | merge | 3.97s [3.82s–4.02s] | 31.83s [31.65s–31.89s] | 1.49s [1.49s–1.54s] | 20.26s [20.23s–20.48s] | 6.14s [6.13s–6.29s] | 1.01s [1.01s–1.01s] | 3.92x | 31.5x | 1.47x | 20.0x | 6.06x |
| log_pipeline | log-processing | 6.51s [6.49s–6.53s] | 6.88s [6.81s–6.92s] | 619.8 [606.6–619.9] | 19.16s [19.13s–19.28s] | 9.59s [9.51s–9.59s] | 998.4 [996.8–1.02s] | 6.52x | 6.89x | 0.62x | 19.2x | 9.60x |

