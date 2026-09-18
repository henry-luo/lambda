# Lambda Benchmark Results: Result47

- **Date:** 2026-09-18
- **Platform:** Darwin arm64
- **Lambda commit:** `fc0755a79dca0c6726cd8152528481aa3aa73c13`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v47-fc0755a79d` (18,525,416 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 56.10s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 56.0s (batched 55.1s: sync 38.1s, async 17.0s; non-batched 0.8s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v47.json`
- **Separately measured:** MIR (untyped), MIR (untyped, auto), MIR (typed), MIR (typed, auto), LambdaJS measured on 2026-09-18, 3 run(s) from `temp/result47_repair_cells.json` on Lambda commit `fc0755a79d`. Result47 repaired crypto_sha1 MIR-U and pidigits LambdaJS failures; refreshed MIR and LambdaJS columns for both targeted rows with the patched release archive test/benchmark/exe/lambda-v47-fc0755a79d-repair (sha256 b3a562b685782ebcdc05b5509eb6f6ddb3086561194e8ba9a1b6105c5bf687fb).
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 0.35x | 0.28x | 0.21x | 1.10x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 1.43x | 0.54x | 0.09x | 12.6x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 0.39x | 0.31x | 0.10x | 5.14x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 1.45x | 0.80x | 0.23x | 16.3x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 1.39x | 0.70x | 0.33x | 15.4x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 3.96x | 2.34x | 0.29x | 33.7x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 6.63x | 4.03x | 0.47x | 27.0x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 1.26x | 0.71x | 0.20x | 9.73x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 59 | 0.64x | 1.12x | 3.55x | 59 |
| complete current suite | 63 | 0.71x | 1.26x | 3.59x | 63 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 3.59x over 63 of 63 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| text/hyphen | 69.0 | 1.48 | 46.7x |
| awfy/havlak | 60.0 | 1.84 | 32.7x |
| text/microdiff | 59.5 | 2.64 | 22.6x |
| awfy/deltablue | 25.4 | 1.16 | 21.9x |
| beng/knucleotide | 4.90 | 0.283 | 17.3x |
| jetstream/splay | 325.5 | 19.1 | 17.1x |
| kostya/base64 | 8.39 | 0.557 | 15.1x |
| text/prettier_ast | 607.2 | 41.7 | 14.6x |
| jetstream/hashmap | 37.6 | 2.84 | 13.2x |
| jetstream/cube3d | 7.18 | 0.543 | 13.2x |
| awfy/cd | 180.0 | 15.0 | 12.0x |
| awfy/towers | 0.322 | 0.028 | 11.5x |

---

### Notable Results

- Missing timings: **0** cells

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 14.34s | 95.6 | 150x |
| larceny/triangl | 7.64s | 69.2 | 110x |
| awfy/cd | 3.81s | 36.0 | 106x |
| jetstream/hashmap | 1.55s | 15.3 | 101x |
| awfy/nbody | 530.5 | 5.47 | 96.9x |
| larceny/quicksort | 133.2 | 1.68 | 79.3x |
| text/text_search | 53.63s | 779.7 | 68.8x |
| kostya/brainfuck | 1.97s | 33.6 | 58.5x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| r7rs/sumfp | 0.068 | 0.961 | 0.07x |
| beng/pidigits | 0.479 | 1.80 | 0.27x |
| r7rs/tak | 0.314 | 0.796 | 0.39x |
| awfy/sieve | 0.214 | 0.383 | 0.56x |
| r7rs/sum | 0.670 | 1.19 | 0.56x |
| r7rs/cpstak | 0.623 | 0.974 | 0.64x |
| r7rs/fib | 1.85 | 1.90 | 0.98x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.34 [1.33–1.44] | 1.31 [1.27–1.36] | 1.12 [1.12–1.12] | 1.85 [1.83–1.90] | 1.90 [1.81–3.21] | 0.71x | 0.69x | 0.59x | 0.98x |
| fibfp | recursive | 2.16 [2.10–2.23] | 1.19 [1.19–1.27] | 1.16 [1.12–1.19] | 1.80 [1.79–1.81] | 1.77 [1.75–1.84] | 1.22x | 0.67x | 0.65x | 1.02x |
| tak | recursive | 0.122 [0.120–0.123] | 0.123 [0.121–0.126] | 0.119 [0.118–0.121] | 0.314 [0.308–0.319] | 0.796 [0.779–0.873] | 0.15x | 0.15x | 0.15x | 0.39x |
| cpstak | closure | 0.241 [0.238–0.249] | 0.243 [0.242–0.300] | 0.231 [0.217–0.231] | 0.623 [0.617–0.626] | 0.974 [0.964–0.982] | 0.25x | 0.25x | 0.24x | 0.64x |
| sum | iterative | 0.266 [0.265–0.266] | 0.266 [0.265–0.266] | 0.271 [0.270–0.271] | 0.670 [0.670–0.671] | 1.19 [1.18–1.22] | 0.22x | 0.22x | 0.23x | 0.56x |
| sumfp | iterative | 0.068 [0.068–0.070] | 0.069 [0.068–0.069] | 0.080 [0.075–0.080] | 0.068 [0.067–0.068] | 0.961 [0.888–0.962] | 0.07x | 0.07x | 0.08x | 0.07x |
| nqueens | backtrack | 1.51 [1.43–1.75] | 1.25 [1.23–1.33] | 0.371 [0.370–0.372] | 34.7 [34.6–35.2] | 1.80 [1.75–1.87] | 0.84x | 0.70x | 0.21x | 19.3x |
| fft | numeric | 0.256 [0.243–0.262] | 0.093 [0.092–0.109] | 0.025 [0.025–0.025] | 3.80 [3.80–3.81] | 1.55 [1.54–1.56] | 0.17x | 0.06x | 0.02x | 2.45x |
| mbrot | numeric | 0.725 [0.724–0.725] | 0.550 [0.542–0.558] | 0.443 [0.442–0.450] | 9.41 [9.32–9.42] | 1.87 [1.86–1.89] | 0.39x | 0.29x | 0.24x | 5.02x |
| ack | recursive | 11.8 [10.8–12.8] | 9.92 [9.84–9.95] | 11.7 [11.1–11.8] | 14.3 [13.1–14.9] | 13.4 [13.2–13.4] | 0.88x | 0.74x | 0.87x | 1.07x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.027 [0.026–0.028] | 0.027 [0.026–0.028] | 0.015 [0.015–0.017] | 0.214 [0.213–0.214] | 0.383 [0.374–0.392] | 0.07x | 0.07x | 0.04x | 0.56x |
| permute | micro | 0.608 [0.606–0.621] | 0.085 [0.079–0.094] | 0.026 [0.025–0.026] | 6.63 [6.58–6.82] | 0.818 [0.804–0.823] | 0.74x | 0.10x | 0.03x | 8.11x |
| queens | micro | 0.325 [0.324–0.325] | 0.066 [0.063–0.067] | 0.023 [0.018–0.023] | 3.67 [3.65–3.77] | 0.639 [0.639–0.669] | 0.51x | 0.10x | 0.04x | 5.75x |
| towers | micro | 1.33 [1.31–1.35] | 0.322 [0.320–0.333] | 0.028 [0.027–0.031] | 14.4 [14.4–14.6] | 1.12 [1.12–1.15] | 1.18x | 0.29x | 0.03x | 12.9x |
| bounce | micro | 0.065 [0.064–0.069] | 0.096 [0.096–0.099] | 0.024 [0.024–0.025] | 3.87 [3.79–3.89] | 0.540 [0.528–0.554] | 0.12x | 0.18x | 0.04x | 7.17x |
| list | micro | 0.601 [0.600–0.610] | 0.124 [0.123–0.125] | 0.024 [0.021–0.024] | 2.20 [2.20–2.21] | 0.484 [0.484–0.487] | 1.24x | 0.26x | 0.05x | 4.55x |
| storage | micro | 0.753 [0.737–0.829] | 0.341 [0.336–0.350] | 0.092 [0.080–0.095] | 4.39 [4.33–4.41] | 0.635 [0.632–0.659] | 1.19x | 0.54x | 0.14x | 6.92x |
| mandelbrot | compute | 39.6 [39.5–39.6] | 39.6 [39.5–39.6] | 30.7 [30.7–30.7] | 50.3 [50.1–50.3] | 31.1 [31.1–31.2] | 1.27x | 1.27x | 0.99x | 1.61x |
| nbody | compute | 51.8 [51.7–51.9] | 3.57 [3.51–3.58] | 1.55 [1.53–1.55] | 530.5 [528.1–607.3] | 5.47 [5.42–5.53] | 9.47x | 0.65x | 0.28x | 96.9x |
| richards | macro | 364.2 [359.9–367.3] | 324.9 [323.8–325.2] | 29.3 [29.1–29.6] | 1.11s [1.10s–1.11s] | 46.7 [46.3–46.8] | 7.79x | 6.95x | 0.63x | 23.8x |
| json | macro | 6.17 [6.12–6.19] | 2.58 [2.58–2.67] | 0.266 [0.258–0.268] | 42.4 [42.1–42.6] | 2.72 [2.62–2.99] | 2.27x | 0.95x | 0.10x | 15.6x |
| deltablue | macro | 121.7 [121.4–122.1] | 25.4 [25.3–25.6] | 1.16 [1.14–1.16] | 432.5 [430.8–432.7] | 11.6 [11.6–12.1] | 10.5x | 2.18x | 0.10x | 37.2x |
| havlak | macro | 70.2 [70.2–70.3] | 60.0 [59.9–60.1] | 1.84 [1.80–2.00] | 14.34s [14.33s–14.43s] | 95.6 [90.2–130.9] | 0.73x | 0.63x | 0.02x | 150x |
| cd | macro | 571.9 [564.5–573.9] | 180.0 [179.4–183.4] | 15.0 [15.0–15.0] | 3.81s [3.80s–3.82s] | 36.0 [35.7–36.6] | 15.9x | 5.00x | 0.42x | 106x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 8.33 [8.26–8.87] | 2.16 [2.06–2.20] | 3.09 [3.01–3.18] | 37.6 [37.4–37.6] | 3.85 [3.83–4.51] | 2.17x | 0.56x | 0.80x | 9.77x |
| fannkuch | permutation | 0.311 [0.307–0.312] | 0.359 [0.358–0.367] | 0.153 [0.149–0.154] | 36.4 [36.3–36.7] | 3.85 [3.83–3.93] | 0.08x | 0.09x | 0.04x | 9.46x |
| fasta | generation | 0.766 [0.743–0.820] | 0.872 [0.870–0.883] | 0.245 [0.239–0.245] | 27.0 [26.9–27.3] | 6.08 [6.03–6.09] | 0.13x | 0.14x | 0.04x | 4.44x |
| knucleotide | hashing | 4.76 [4.73–5.01] | 4.90 [4.86–5.06] | 0.283 [0.282–0.286] | 21.6 [21.3–38.8] | 4.80 [4.80–4.81] | 0.99x | 1.02x | 0.06x | 4.50x |
| pidigits | bignum | 0.333 [0.330–0.335] | 0.326 [0.321–0.331] | 0.046 [0.044–0.046] | 0.479 [0.476–0.524] | 1.80 [1.79–2.04] | 0.19x | 0.18x | 0.03x | 0.27x |
| regexredux | regex | 1.27 [1.26–1.31] | 1.28 [1.28–1.40] | 1.14 [1.13–1.15] | 8.83 [8.79–8.83] | 2.31 [2.31–2.34] | 0.55x | 0.55x | 0.49x | 3.82x |
| revcomp | string | 1.20 [1.18–1.20] | 1.12 [1.11–1.15] | 0.378 [0.376–0.379] | 22.2 [21.9–22.2] | 3.26 [3.23–3.41] | 0.37x | 0.34x | 0.12x | 6.79x |
| spectralnorm | numeric | 1.64 [1.62–1.65] | 0.786 [0.785–0.795] | 0.354 [0.351–0.355] | 102.0 [101.8–103.0] | 2.69 [2.50–2.73] | 0.61x | 0.29x | 0.13x | 38.0x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 196.4 [196.0–283.6] | 196.6 [196.3–196.8] | 27.9 [27.8–28.0] | 1.97s [1.94s–1.97s] | 33.6 [33.5–33.7] | 5.84x | 5.85x | 0.83x | 58.5x |
| matmul | numeric | 5.89 [5.30–9.24] | 5.11 [5.08–5.17] | 6.03 [6.03–6.22] | 359.0 [358.0–360.2] | 15.5 [15.5–15.5] | 0.38x | 0.33x | 0.39x | 23.2x |
| primes | numeric | 14.7 [14.7–14.8] | 2.23 [2.20–2.27] | 1.59 [1.58–1.64] | 80.3 [80.2–80.5] | 4.44 [4.40–4.58] | 3.31x | 0.50x | 0.36x | 18.1x |
| base64 | string | 16.2 [16.2–16.3] | 8.39 [8.30–8.51] | 0.557 [0.556–0.598] | 508.3 [505.0–509.2] | 19.2 [18.6–19.7] | 0.84x | 0.44x | 0.03x | 26.4x |
| levenshtein | string | 10.2 [10.1–10.6] | 6.52 [6.47–6.53] | 0.912 [0.899–1.11] | 218.1 [214.0–225.0] | 4.09 [3.98–4.13] | 2.49x | 1.60x | 0.22x | 53.3x |
| json_gen | data | 26.9 [26.9–27.2] | 9.79 [9.67–9.99] | 1.50 [1.49–1.54] | 28.4 [28.1–29.3] | 6.06 [6.00–6.38] | 4.45x | 1.62x | 0.25x | 4.69x |
| collatz | numeric | 276.5 [276.3–276.6] | 279.8 [279.4–302.4] | 225.4 [225.0–225.9] | 2.66s [2.65s–2.70s] | 1.42s [1.42s–1.52s] | 0.19x | 0.20x | 0.16x | 1.87x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 231.7 [231.5–232.0] | 167.1 [167.0–167.3] | 60.4 [60.3–60.4] | 7.64s [7.61s–7.68s] | 69.2 [69.0–70.5] | 3.35x | 2.41x | 0.87x | 110x |
| array1 | array | 0.806 [0.806–0.807] | 0.808 [0.808–0.838] | 0.320 [0.317–0.325] | 43.0 [42.9–43.4] | 1.83 [1.82–1.90] | 0.44x | 0.44x | 0.18x | 23.5x |
| deriv | symbolic | 29.5 [29.3–29.7] | 7.11 [7.04–7.54] | 2.84 [2.83–2.88] | 88.3 [87.6–88.5] | 3.80 [3.79–3.89] | 7.76x | 1.87x | 0.75x | 23.2x |
| diviter | iterative | 261.6 [253.9–266.5] | 253.1 [250.4–350.6] | 260.6 [258.8–262.5] | 12.69s [12.67s–12.78s] | 472.5 [471.5–485.2] | 0.55x | 0.54x | 0.55x | 26.9x |
| divrec | recursive | 5.55 [5.50–5.61] | 1.22 [1.22–1.28] | 4.92 [4.87–5.09] | 14.8 [14.7–14.9] | 7.63 [7.59–7.68] | 0.73x | 0.16x | 0.64x | 1.94x |
| gcbench | allocation | 192.7 [192.0–193.9] | 80.0 [79.6–81.9] | 71.1 [71.0–73.4] | 979.2 [965.1–989.0] | 23.9 [23.7–23.9] | 8.06x | 3.35x | 2.97x | 41.0x |
| paraffins | combinat | 0.199 [0.186–0.202] | 0.155 [0.154–0.155] | 0.048 [0.047–0.048] | 3.55 [3.49–3.55] | 0.971 [0.967–1.01] | 0.20x | 0.16x | 0.05x | 3.65x |
| pnpoly | numeric | 11.8 [11.8–11.9] | 12.1 [12.0–12.1] | 1.94 [1.91–1.96] | 136.9 [136.0–137.1] | 5.84 [5.79–5.87] | 2.02x | 2.07x | 0.33x | 23.5x |
| puzzle | search | 19.3 [19.0–19.5] | 13.9 [13.6–14.0] | 1.27 [1.25–1.34] | 26.2 [26.1–26.5] | 3.32 [3.28–3.33] | 5.81x | 4.18x | 0.38x | 7.89x |
| quicksort | sorting | 11.9 [11.9–12.1] | 0.705 [0.693–0.736] | 0.198 [0.197–0.198] | 133.2 [131.5–134.8] | 1.68 [1.64–1.89] | 7.10x | 0.42x | 0.12x | 79.3x |
| ray | numeric | 0.212 [0.208–0.227] | 0.208 [0.206–0.210] | 0.173 [0.170–0.173] | 6.17 [6.05–6.19] | 3.62 [3.55–3.64] | 0.06x | 0.06x | 0.05x | 1.70x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 8.43 [8.34–8.68] | 7.18 [7.17–7.34] | 0.543 [0.526–0.557] | 319.7 [317.8–321.0] | 17.7 [17.6–17.8] | 0.48x | 0.41x | 0.03x | 18.1x |
| navier_stokes | numeric | 86.6 [86.5–86.7] | 69.8 [69.6–70.0] | 47.1 [47.0–47.1] | 565.4 [563.5–609.5] | 14.3 [14.2–14.3] | 6.05x | 4.88x | 3.30x | 39.5x |
| splay | data | 341.7 [340.4–344.5] | 325.5 [324.8–329.4] | 19.1 [19.0–19.2] | 374.5 [373.9–374.5] | 19.9 [19.7–20.1] | 17.2x | 16.3x | 0.96x | 18.8x |
| hashmap | data | 73.5 [72.1–73.8] | 37.6 [37.4–37.7] | 2.84 [2.77–2.88] | 1.55s [1.54s–1.56s] | 15.3 [15.3–15.5] | 4.80x | 2.46x | 0.19x | 101x |
| crypto_sha1 | crypto | 39.0 [38.8–39.1] | 22.5 [22.3–22.9] | 2.74 [2.65–2.78] | 308.7 [307.7–309.3] | 8.79 [8.65–8.85] | 4.44x | 2.56x | 0.31x | 35.1x |
| raytrace3d | 3d | 68.4 [67.8–68.6] | 15.1 [14.9–15.1] | 2.18 [2.16–2.19] | 569.7 [567.7–571.2] | 18.6 [18.3–18.6] | 3.68x | 0.81x | 0.12x | 30.7x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 240.6 [240.3–259.3] | 137.3 [137.0–137.4] | 13.1 [13.1–13.2] | 1.93s [1.93s–1.94s] | 39.6 [39.6–40.0] | 6.07x | 3.46x | 0.33x | 48.8x |
| microdiff | data-diff | 52.8 [52.4–53.2] | 59.5 [58.8–60.1] | 2.64 [2.63–2.73] | 825.1 [824.1–841.0] | 16.4 [16.3–16.5] | 3.22x | 3.63x | 0.16x | 50.3x |
| hyphen | hyphenation | 69.2 [69.1–70.0] | 69.0 [68.0–69.1] | 1.48 [1.47–1.48] | 100.6 [100.2–101.7] | 6.72 [6.68–8.08] | 10.3x | 10.3x | 0.22x | 15.0x |
| prettier_ast | formatting | 976.6 [965.7–984.0] | 607.2 [600.9–607.9] | 41.7 [41.4–42.2] | 2.69s [2.66s–2.70s] | 100.4 [99.9–100.8] | 9.73x | 6.05x | 0.42x | 26.8x |
| text_search | search | 15.29s [15.23s–15.30s] | 1.75s [1.75s–1.77s] | 541.0 [537.7–548.4] | 53.63s [53.57s–53.76s] | 779.7 [779.3–793.7] | 19.6x | 2.24x | 0.69x | 68.8x |
| three_way_merge | merge | 2.97s [2.95s–2.97s] | 2.26s [2.25s–2.27s] | 2.14s [2.13s–2.15s] | 9.91s [9.90s–9.96s] | 972.6 [968.2–984.6] | 3.05x | 2.33x | 2.20x | 10.2x |
| log_pipeline | log-processing | 4.56s [4.55s–4.56s] | 4.03s [4.03s–4.06s] | 625.4 [624.5–720.2] | 14.29s [14.20s–14.34s] | 946.9 [942.7–974.7] | 4.81x | 4.26x | 0.66x | 15.1x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 0.43x | 0.44x | 1.01x | 0.77x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 1.02x | 0.92x | 0.91x | 4.30x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 0.52x | 0.58x | 1.01x | 1.54x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 0.72x | 0.63x | 0.71x | 4.49x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 0.85x | 0.72x | 0.99x | 3.78x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 2.31x | 1.89x | 0.97x | 12.8x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 4.08x | 2.50x | 0.93x | 17.4x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 0.96x | 0.85x | 0.94x | 3.66x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 18.3 [17.8–19.0] | 19.1 [18.6–19.4] | 48.2 [48.0–483.5] | 27.7 [26.6–27.9] | 48.4 [47.9–86.6] | 0.38x | 0.39x | 1.00x | 0.57x |
| fibfp | recursive | 18.4 [18.4–18.6] | 17.8 [17.6–17.9] | 47.3 [47.1–47.4] | 25.9 [25.7–26.9] | 46.4 [45.9–46.7] | 0.40x | 0.38x | 1.02x | 0.56x |
| tak | recursive | 17.1 [16.9–17.3] | 16.9 [16.5–17.1] | 46.1 [45.9–46.8] | 25.2 [24.7–25.9] | 45.1 [45.0–45.4] | 0.38x | 0.37x | 1.02x | 0.56x |
| cpstak | closure | 17.1 [16.8–17.1] | 16.9 [16.7–16.9] | 46.2 [46.0–46.4] | 26.3 [25.5–26.7] | 45.2 [45.1–46.1] | 0.38x | 0.37x | 1.02x | 0.58x |
| sum | iterative | 16.4 [16.3–17.0] | 16.7 [16.6–17.1] | 46.1 [46.1–46.5] | 24.8 [24.5–26.7] | 45.6 [45.2–45.7] | 0.36x | 0.37x | 1.01x | 0.54x |
| sumfp | iterative | 15.9 [15.8–16.0] | 16.3 [16.2–16.8] | 46.0 [45.9–46.6] | 25.0 [24.9–26.3] | 46.0 [45.3–46.1] | 0.35x | 0.35x | 1.00x | 0.54x |
| nqueens | backtrack | 25.2 [24.8–25.7] | 28.7 [28.1–29.1] | 48.1 [47.4–48.2] | 124.8 [124.5–126.5] | 46.5 [46.0–46.6] | 0.54x | 0.62x | 1.04x | 2.69x |
| fft | numeric | 31.3 [30.7–31.4] | 40.1 [39.6–40.5] | 46.7 [46.1–46.9] | 67.8 [67.5–68.6] | 46.2 [46.1–46.2] | 0.68x | 0.87x | 1.01x | 1.47x |
| mbrot | numeric | 20.4 [20.0–20.4] | 20.6 [20.2–20.9] | 46.1 [46.0–46.7] | 44.0 [43.9–45.1] | 46.3 [45.8–46.5] | 0.44x | 0.44x | 1.00x | 0.95x |
| ack | recursive | 27.8 [27.7–29.2] | 26.5 [26.1–26.7] | 57.6 [57.1–57.6] | 38.8 [38.3–40.8] | 57.7 [57.7–57.8] | 0.48x | 0.46x | 1.00x | 0.67x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 17.3 [17.2–17.5] | 18.0 [17.8–19.1] | 46.4 [46.4–46.6] | 34.1 [33.8–34.9] | 45.9 [45.8–48.6] | 0.38x | 0.39x | 1.01x | 0.74x |
| permute | micro | 18.8 [18.3–19.5] | 20.8 [20.6–21.6] | 46.7 [46.3–46.9] | 44.2 [43.6–44.3] | 46.1 [45.9–46.6] | 0.41x | 0.45x | 1.01x | 0.96x |
| queens | micro | 22.9 [22.8–23.1] | 29.2 [29.2–29.7] | 46.2 [45.8–46.4] | 45.7 [45.5–47.0] | 45.8 [45.7–45.8] | 0.50x | 0.64x | 1.01x | 1.00x |
| towers | micro | 23.0 [22.8–23.0] | 25.4 [25.1–25.4] | 46.4 [46.2–46.7] | 54.0 [53.6–144.5] | 46.4 [45.6–47.1] | 0.50x | 0.55x | 1.00x | 1.16x |
| bounce | micro | 22.6 [22.0–22.7] | 26.6 [26.4–26.9] | 46.4 [46.4–46.4] | 157.2 [156.7–158.5] | 46.1 [46.1–46.7] | 0.49x | 0.58x | 1.01x | 3.41x |
| list | micro | 20.7 [20.7–21.1] | 20.0 [19.9–20.5] | 46.8 [46.1–46.8] | 40.6 [40.1–40.7] | 46.0 [45.7–46.5] | 0.45x | 0.44x | 1.02x | 0.88x |
| storage | micro | 18.9 [18.8–19.2] | 19.5 [19.0–20.0] | 46.2 [46.1–46.6] | 153.7 [153.4–155.6] | 46.2 [45.9–46.8] | 0.41x | 0.42x | 1.00x | 3.33x |
| mandelbrot | compute | 59.6 [59.3–60.1] | 59.8 [59.7–59.8] | 77.0 [76.9–77.2] | 85.5 [85.4–85.6] | 76.7 [76.0–76.7] | 0.78x | 0.78x | 1.00x | 1.11x |
| nbody | compute | 88.8 [88.6–89.1] | 63.1 [62.5–63.6] | 49.3 [48.9–49.6] | 621.0 [617.7–695.2] | 50.7 [50.5–52.1] | 1.75x | 1.24x | 0.97x | 12.2x |
| richards | macro | 432.3 [432.3–433.4] | 394.5 [390.7–473.3] | 78.2 [78.1–79.1] | 1.24s [1.23s–1.24s] | 92.3 [91.6–93.2] | 4.68x | 4.27x | 0.85x | 13.4x |
| json | macro | 54.1 [53.2–54.1] | 51.6 [51.4–52.6] | 53.4 [53.1–54.4] | 339.9 [339.7–340.5] | 49.0 [48.7–50.2] | 1.10x | 1.05x | 1.09x | 6.93x |
| deltablue | macro | 274.2 [272.4–274.5] | 153.0 [152.3–153.2] | 54.7 [54.6–54.9] | 789.4 [789.3–877.2] | 57.8 [57.5–59.3] | 4.75x | 2.65x | 0.95x | 13.7x |
| havlak | macro | 197.6 [197.2–198.5] | 177.4 [176.8–177.8] | 55.6 [55.6–55.9] | 14.69s [14.67s–14.78s] | 144.7 [138.7–180.5] | 1.37x | 1.23x | 0.38x | 101x |
| cd | macro | 664.7 [657.6–665.6] | 261.3 [260.9–261.4] | 68.6 [68.4–69.6] | 4.21s [4.21s–4.22s] | 82.8 [81.9–82.9] | 8.03x | 3.16x | 0.83x | 50.9x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 30.1 [30.0–30.4] | 32.9 [25.5–108.9] | 49.9 [49.7–50.0] | 70.6 [70.0–71.4] | 48.1 [46.8–48.3] | 0.63x | 0.68x | 1.04x | 1.47x |
| fannkuch | permutation | 22.7 [22.4–22.8] | 27.2 [27.0–27.3] | 46.7 [46.3–46.7] | 104.0 [103.4–104.7] | 47.0 [46.8–47.1] | 0.48x | 0.58x | 0.99x | 2.21x |
| fasta | generation | 26.3 [26.0–26.4] | 29.6 [29.5–29.8] | 46.7 [46.5–46.7] | 72.5 [72.1–72.9] | 49.3 [49.0–49.3] | 0.53x | 0.60x | 0.95x | 1.47x |
| knucleotide | hashing | 32.6 [32.3–32.7] | 45.7 [45.5–45.9] | 47.3 [46.9–47.6] | 68.7 [67.7–587.1] | 49.0 [48.6–49.9] | 0.66x | 0.93x | 0.96x | 1.40x |
| pidigits | bignum | 20.0 [20.0–20.4] | 20.1 [20.1–20.4] | 48.8 [48.3–48.8] | 49.8 [49.4–49.9] | 45.1 [45.0–45.3] | 0.44x | 0.44x | 1.08x | 1.10x |
| regexredux | regex | 17.0 [16.9–17.8] | 16.8 [16.6–17.5] | 49.2 [49.2–49.4] | 50.3 [50.2–50.5] | 45.4 [45.3–45.7] | 0.37x | 0.37x | 1.08x | 1.11x |
| revcomp | string | 24.8 [24.8–25.1] | 24.0 [23.8–24.3] | 47.0 [46.8–47.5] | 57.7 [57.4–57.8] | 46.1 [46.1–46.3] | 0.54x | 0.52x | 1.02x | 1.25x |
| spectralnorm | numeric | 25.2 [25.2–25.4] | 30.5 [30.1–30.7] | 46.7 [46.5–46.9] | 149.9 [149.4–151.3] | 47.6 [47.2–48.3] | 0.53x | 0.64x | 0.98x | 3.15x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 218.9 [218.2–219.0] | 222.2 [219.4–247.5] | 74.4 [74.1–74.4] | 2.00s [1.98s–2.01s] | 78.5 [78.1–79.1] | 2.79x | 2.83x | 0.95x | 25.6x |
| matmul | numeric | 30.4 [30.0–30.5] | 32.2 [32.1–32.3] | 52.9 [52.5–53.5] | 394.5 [394.1–396.9] | 60.2 [60.0–61.0] | 0.51x | 0.54x | 0.88x | 6.56x |
| primes | numeric | 32.8 [32.6–32.9] | 21.1 [20.9–21.1] | 47.6 [47.3–47.8] | 111.2 [110.8–111.4] | 49.0 [48.9–128.5] | 0.67x | 0.43x | 0.97x | 2.27x |
| base64 | string | 40.6 [40.5–40.6] | 39.9 [39.9–40.0] | 47.2 [46.5–47.3] | 562.1 [559.5–565.8] | 70.7 [69.9–71.0] | 0.57x | 0.56x | 0.67x | 7.95x |
| levenshtein | string | 41.9 [41.6–42.5] | 35.7 [34.6–36.0] | 48.6 [48.1–48.9] | 265.4 [256.4–388.9] | 49.7 [49.0–49.7] | 0.84x | 0.72x | 0.98x | 5.34x |
| json_gen | data | 54.6 [54.0–55.2] | 39.0 [38.9–39.4] | 48.6 [48.6–48.6] | 63.8 [63.8–66.4] | 51.3 [50.9–52.2] | 1.06x | 0.76x | 0.95x | 1.24x |
| collatz | numeric | 292.8 [291.9–293.1] | 299.4 [298.2–368.8] | 271.9 [271.5–292.2] | 2.69s [2.68s–2.73s] | 1.47s [1.46s–1.56s] | 0.20x | 0.20x | 0.19x | 1.83x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 258.8 [258.4–259.5] | 194.7 [194.4–195.8] | 106.6 [106.6–107.6] | 7.68s [7.65s–7.73s] | 115.3 [113.8–116.8] | 2.24x | 1.69x | 0.92x | 66.6x |
| array1 | array | 17.4 [17.0–17.5] | 18.2 [18.1–18.3] | 46.2 [46.1–46.6] | 70.4 [70.2–70.8] | 46.6 [46.2–46.6] | 0.37x | 0.39x | 0.99x | 1.51x |
| deriv | symbolic | 56.0 [55.9–56.7] | 28.5 [28.1–28.9] | 50.3 [50.1–50.4] | 119.7 [119.2–120.0] | 49.3 [49.0–49.7] | 1.14x | 0.58x | 1.02x | 2.43x |
| diviter | iterative | 272.0 [270.3–274.6] | 276.8 [275.8–288.6] | 307.1 [304.8–308.9] | 12.72s [12.69s–12.81s] | 517.6 [516.3–531.5] | 0.53x | 0.53x | 0.59x | 24.6x |
| divrec | recursive | 23.3 [23.3–24.1] | 19.9 [19.4–19.9] | 52.2 [52.2–52.5] | 42.3 [42.0–42.5] | 52.0 [51.9–53.1] | 0.45x | 0.38x | 1.01x | 0.81x |
| gcbench | allocation | 221.6 [221.4–305.8] | 124.1 [120.2–127.7] | 119.5 [117.6–121.8] | 1.02s [1.00s–1.03s] | 69.2 [68.8–69.4] | 3.20x | 1.79x | 1.73x | 14.7x |
| paraffins | combinat | 44.0 [43.2–44.0] | 61.5 [61.2–61.6] | 47.1 [46.6–47.4] | 81.4 [79.9–82.1] | 45.5 [45.3–45.9] | 0.97x | 1.35x | 1.04x | 1.79x |
| pnpoly | numeric | 31.0 [31.0–31.4] | 32.4 [31.9–32.5] | 48.2 [47.8–48.3] | 176.0 [175.3–204.6] | 50.9 [50.8–52.4] | 0.61x | 0.64x | 0.95x | 3.46x |
| puzzle | search | 41.8 [41.2–42.0] | 37.9 [37.8–38.2] | 47.9 [47.8–48.4] | 66.6 [66.4–68.0] | 47.7 [47.5–47.9] | 0.88x | 0.79x | 1.00x | 1.40x |
| quicksort | sorting | 33.7 [33.5–34.0] | 24.4 [24.3–24.8] | 47.7 [46.9–48.0] | 176.8 [175.5–179.9] | 47.3 [46.7–47.5] | 0.71x | 0.52x | 1.01x | 3.74x |
| ray | numeric | 31.8 [31.2–32.1] | 24.5 [24.5–24.5] | 46.3 [46.2–46.7] | 47.2 [46.6–48.0] | 48.0 [47.9–48.4] | 0.66x | 0.51x | 0.96x | 0.98x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 106.4 [105.7–106.5] | 115.6 [115.0–116.3] | 50.7 [50.7–51.0] | 758.4 [750.5–845.6] | 64.5 [64.4–64.8] | 1.65x | 1.79x | 0.79x | 11.8x |
| navier_stokes | numeric | 221.9 [221.5–247.3] | 201.7 [201.1–202.4] | 96.3 [96.3–97.0] | 869.6 [854.3–903.7] | 61.5 [61.3–62.4] | 3.61x | 3.28x | 1.57x | 14.1x |
| splay | data | 415.1 [414.3–419.8] | 417.6 [403.2–506.2] | 68.1 [67.5–68.5] | 937.6 [936.9–942.1] | 94.8 [93.6–95.3] | 4.38x | 4.41x | 0.72x | 9.89x |
| hashmap | data | 114.2 [113.9–114.2] | 75.6 [75.6–75.7] | 49.4 [48.7–50.1] | 1.67s [1.65s–1.67s] | 60.9 [60.5–60.9] | 1.88x | 1.24x | 0.81x | 27.4x |
| crypto_sha1 | crypto | 72.1 [71.4–72.2] | 56.5 [56.5–56.8] | 76.6 [53.2–137.3] | 396.1 [395.4–412.7] | 54.0 [53.4–54.8] | 1.34x | 1.05x | 1.42x | 7.34x |
| raytrace3d | 3d | 149.8 [149.5–149.8] | 86.1 [85.3–86.2] | 53.6 [52.9–54.0] | 852.5 [849.8–852.9] | 64.5 [63.5–64.6] | 2.32x | 1.33x | 0.83x | 13.2x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 270.7 [270.5–273.4] | 163.9 [163.4–164.5] | 60.2 [60.0–60.2] | 2.59s [2.59s–2.59s] | 85.1 [85.0–85.5] | 3.18x | 1.93x | 0.71x | 30.4x |
| microdiff | data-diff | 92.1 [91.0–177.4] | 97.6 [97.0–97.7] | 52.4 [52.2–52.7] | 901.1 [899.8–916.0] | 61.3 [61.0–61.8] | 1.50x | 1.59x | 0.85x | 14.7x |
| hyphen | hyphenation | 120.2 [120.1–120.2] | 119.2 [118.9–120.4] | 77.3 [77.0–78.0] | 361.9 [361.0–362.4] | 59.5 [58.9–61.1] | 2.02x | 2.00x | 1.30x | 6.08x |
| prettier_ast | formatting | 1.11s [1.11s–1.20s] | 730.1 [730.0–733.4] | 105.3 [105.0–106.9] | 2.85s [2.83s–2.87s] | 146.8 [146.8–147.8] | 7.58x | 4.97x | 0.72x | 19.4x |
| text_search | search | 15.34s [15.26s–15.49s] | 1.78s [1.78s–1.80s] | 589.9 [585.8–596.4] | 53.72s [53.65s–53.84s] | 826.1 [825.9–840.0] | 18.6x | 2.16x | 0.71x | 65.0x |
| three_way_merge | merge | 3.03s [3.00s–3.12s] | 2.29s [2.27s–2.31s] | 2.19s [2.19s–2.20s] | 9.95s [9.94s–10.02s] | 1.02s [1.01s–1.03s] | 2.97x | 2.24x | 2.15x | 9.77x |
| log_pipeline | log-processing | 4.69s [4.66s–4.72s] | 4.09s [4.08s–4.14s] | 675.3 [674.7–770.1] | 14.38s [14.28s–14.43s] | 1.00s [997.5–1.03s] | 4.68x | 4.08x | 0.67x | 14.4x |

