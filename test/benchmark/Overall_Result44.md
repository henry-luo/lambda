# Lambda Benchmark Results: Result44

- **Date:** 2026-09-14
- **Platform:** Darwin arm64
- **Lambda commit:** `acd1e88d1b99410199c467eb3cb47a1542acecb0`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v44-acd1e88d1b` (19,338,904 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,258 / 40,261 passed in 116.70s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 116.2s (batched 115.2s: sync 94.7s, async 20.5s; non-batched 1.0s); retry 0.3s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v44.json`
- **Separately measured:** LambdaJS measured on 2026-09-14, 3 run(s) from `temp/benchmark_v44_ljsfix.json` on Lambda commit `acd1e88d1b`. LambdaJS refresh after fixing the c7e285e51 regression (js_realm_intrinsic_slots_ensure_roots on every prototype lookup, 20x); measured with cached release test/benchmark/exe/lambda-v44-acd1e88d1b-ljsfix (sha256 3b43e87bb518f9fd6ed71aa78b93561f949a6b8fcc3ec09c02053cf28b98dbbe), the Result44 tree plus the two uncommitted fixes described in vibe/impl/Lambda_Impl_Tune27 (done).md §12
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.35x | 0.32x | 0.19x | 7.89x | 6.49x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.47x | 0.78x | 0.09x | 22.4x | 5.18x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.37x | 0.33x | 0.10x | 9.48x | 1.69x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 1.54x | 1.03x | 0.23x | 42.5x | 11.9x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 1.49x | 0.77x | 0.33x | 22.1x | 13.3x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 4.89x | 2.70x | 0.30x | 38.0x | 12.1x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 6.96x | 4.69x | 0.47x | 51.6x | 12.8x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 63 | 1.32x | 0.86x | 0.19x | 21.0x | 7.22x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 59 | 0.77x | 1.17x | 4.35x | 59 |
| complete current suite | 63 | 0.86x | 1.32x | 4.41x | 63 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 4.41x over 63 of 63 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| text/hyphen | 69.5 | 1.48 | 47.1x |
| awfy/deltablue | 41.2 | 1.14 | 36.0x |
| awfy/havlak | 62.9 | 1.80 | 35.0x |
| awfy/cd | 490.2 | 15.1 | 32.4x |
| text/microdiff | 57.1 | 2.61 | 21.9x |
| beng/knucleotide | 5.28 | 0.280 | 18.8x |
| jetstream/cube3d | 9.06 | 0.528 | 17.2x |
| text/prettier_ast | 666.9 | 41.4 | 16.1x |
| kostya/base64 | 8.77 | 0.555 | 15.8x |
| jetstream/splay | 295.6 | 18.9 | 15.6x |
| jetstream/hashmap | 42.9 | 2.80 | 15.3x |
| awfy/richards | 389.5 | 29.1 | 13.4x |

---

### Notable Results

- Missing timings: **0** cells

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| kostya/primes | 1.75s | 4.39 | 399x |
| awfy/havlak | 21.29s | 95.7 | 222x |
| text/text_search | 117.91s | 780.9 | 151x |
| awfy/cd | 5.36s | 36.0 | 149x |
| larceny/triangl | 7.61s | 66.5 | 114x |
| jetstream/hashmap | 1.72s | 15.2 | 113x |
| awfy/nbody | 574.4 | 5.53 | 104x |
| beng/spectralnorm | 245.4 | 2.55 | 96.4x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.449 | 1.92 | 0.23x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.29 [1.27–1.34] | 1.30 [1.29–1.32] | 1.12 [1.02–1.15] | 15.5 [15.4–15.6] | 18.7 [18.6–19.0] | 1.83 [1.76–7.89] | 0.70x | 0.71x | 0.61x | 8.45x | 10.2x |
| fibfp | recursive | 2.10 [2.08–2.12] | 1.19 [1.19–1.20] | 1.12 [1.08–1.13] | 15.6 [15.5–15.7] | 18.7 [18.5–35.3] | 1.77 [1.77–1.79] | 1.19x | 0.67x | 0.63x | 8.76x | 10.6x |
| tak | recursive | 0.122 [0.120–0.123] | 0.122 [0.121–0.127] | 0.118 [0.110–0.137] | 1.37 [1.37–1.38] | 2.77 [2.76–2.80] | 0.794 [0.784–0.804] | 0.15x | 0.15x | 0.15x | 1.73x | 3.49x |
| cpstak | closure | 0.243 [0.242–0.246] | 0.246 [0.241–0.253] | 0.234 [0.217–0.235] | 2.72 [2.72–2.97] | 5.55 [5.53–5.59] | 0.980 [0.964–1.01] | 0.25x | 0.25x | 0.24x | 2.77x | 5.67x |
| sum | iterative | 0.265 [0.265–0.266] | 0.266 [0.265–0.266] | 0.269 [0.268–0.271] | 28.4 [28.3–28.9] | 31.0 [31.0–31.1] | 1.18 [1.18–1.90] | 0.22x | 0.23x | 0.23x | 24.0x | 26.3x |
| sumfp | iterative | 0.069 [0.069–0.069] | 0.069 [0.068–0.069] | 0.082 [0.081–0.090] | 2.81 [2.78–2.91] | 3.70 [3.62–3.71] | 0.875 [0.849–1.74] | 0.08x | 0.08x | 0.09x | 3.22x | 4.23x |
| nqueens | backtrack | 1.56 [1.54–1.56] | 1.70 [1.68–1.75] | 0.128 [0.126–0.140] | 63.1 [56.5–113.1] | 7.87 [7.78–8.23] | 1.74 [1.71–2.90] | 0.89x | 0.98x | 0.07x | 36.2x | 4.51x |
| fft | numeric | 0.279 [0.274–0.283] | 0.156 [0.156–0.158] | 0.025 [0.024–0.027] | 24.0 [23.8–24.2] | 2.73 [2.73–2.74] | 1.62 [1.56–1.63] | 0.17x | 0.10x | 0.02x | 14.8x | 1.68x |
| mbrot | numeric | 0.706 [0.705–0.708] | 0.932 [0.910–0.936] | 0.449 [0.446–0.453] | 18.6 [18.5–18.6] | 17.7 [17.6–17.7] | 1.82 [1.80–1.83] | 0.39x | 0.51x | 0.25x | 10.2x | 9.68x |
| ack | recursive | 11.1 [11.1–11.2] | 9.59 [9.52–9.71] | 11.7 [11.7–11.7] | 84.4 [84.0–85.4] | 101.3 [101.1–101.4] | 13.4 [13.3–13.4] | 0.83x | 0.72x | 0.88x | 6.30x | 7.56x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.029 [0.029–0.030] | 0.030 [0.029–0.030] | 0.016 [0.014–0.019] | 3.30 [3.27–3.31] | 0.616 [0.606–0.630] | 0.378 [0.375–0.391] | 0.08x | 0.08x | 0.04x | 8.72x | 1.63x |
| permute | micro | 0.560 [0.560–0.590] | 0.093 [0.093–0.094] | 0.026 [0.024–0.027] | 8.38 [8.35–8.44] | 1.55 [1.52–1.63] | 0.828 [0.823–0.927] | 0.68x | 0.11x | 0.03x | 10.1x | 1.88x |
| queens | micro | 0.413 [0.411–0.414] | 0.206 [0.204–0.221] | 0.018 [0.018–0.018] | 4.69 [4.61–4.74] | 1.05 [1.04–1.06] | 0.657 [0.641–0.657] | 0.63x | 0.31x | 0.03x | 7.15x | 1.59x |
| towers | micro | 1.18 [1.16–1.23] | 0.326 [0.321–0.328] | 0.027 [0.027–0.028] | 19.7 [19.6–19.8] | 2.22 [2.22–2.26] | 1.10 [1.10–1.36] | 1.07x | 0.30x | 0.02x | 17.9x | 2.02x |
| bounce | micro | 0.066 [0.066–0.067] | 0.096 [0.096–0.097] | 0.024 [0.024–0.028] | 3.99 [3.96–3.99] | 0.877 [0.868–0.894] | 0.545 [0.540–0.550] | 0.12x | 0.18x | 0.04x | 7.32x | 1.61x |
| list | micro | 0.695 [0.691–0.736] | 0.147 [0.144–0.153] | 0.022 [0.020–0.026] | 2.15 [2.11–2.15] | 0.907 [0.900–0.910] | 0.493 [0.491–0.498] | 1.41x | 0.30x | 0.04x | 4.35x | 1.84x |
| storage | micro | 0.701 [0.685–0.706] | 0.334 [0.325–0.369] | 0.089 [0.088–0.090] | 6.41 [6.34–6.51] | 2.07 [2.07–2.08] | 0.626 [0.613–0.631] | 1.12x | 0.53x | 0.14x | 10.2x | 3.32x |
| mandelbrot | compute | 41.9 [41.9–42.1] | 41.9 [41.9–58.2] | 30.7 [30.6–30.9] | 1.04s [1.01s–1.04s] | 869.2 [868.5–873.7] | 31.1 [31.0–31.2] | 1.35x | 1.35x | 0.99x | 33.3x | 27.9x |
| nbody | compute | 52.4 [52.3–52.4] | 17.4 [17.3–17.5] | 1.49 [1.47–1.50] | 574.4 [570.3–576.8] | 159.5 [158.9–177.4] | 5.53 [5.45–5.98] | 9.47x | 3.14x | 0.27x | 104x | 28.8x |
| richards | macro | 412.9 [411.7–413.9] | 389.5 [388.3–399.4] | 29.1 [29.1–29.5] | 1.17s [1.16s–1.19s] | 190.8 [190.7–190.8] | 47.4 [46.9–47.8] | 8.72x | 8.22x | 0.61x | 24.6x | 4.03x |
| json | macro | 6.13 [6.12–11.1] | 3.00 [3.00–3.00] | 0.260 [0.253–0.266] | 45.1 [44.8–45.5] | 10.7 [10.7–11.1] | 2.63 [2.62–2.65] | 2.33x | 1.14x | 0.10x | 17.2x | 4.08x |
| deltablue | macro | 123.8 [123.2–124.7] | 41.2 [41.0–41.3] | 1.14 [1.14–1.15] | 536.2 [534.8–559.8] | 99.4 [98.9–100.2] | 11.6 [11.4–12.3] | 10.7x | 3.55x | 0.10x | 46.1x | 8.56x |
| havlak | macro | 72.9 [72.2–73.1] | 62.9 [62.8–63.0] | 1.80 [1.79–1.83] | 21.29s [21.28s–22.09s] | 3.33s [3.29s–3.35s] | 95.7 [95.6–96.2] | 0.76x | 0.66x | 0.02x | 222x | 34.7x |
| cd | macro | 577.0 [573.3–579.4] | 490.2 [485.3–491.7] | 15.1 [14.9–15.1] | 5.36s [5.33s–5.44s] | 959.3 [956.0–995.5] | 36.0 [36.0–36.2] | 16.0x | 13.6x | 0.42x | 149x | 26.6x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 8.44 [8.23–8.53] | 2.19 [2.16–2.23] | 3.08 [3.06–3.33] | 31.9 [31.9–32.3] | 23.2 [23.2–23.6] | 4.15 [3.98–5.10] | 2.03x | 0.53x | 0.74x | 7.68x | 5.58x |
| fannkuch | permutation | 0.328 [0.326–0.329] | 0.354 [0.351–0.356] | 0.152 [0.149–0.154] | 42.8 [42.5–42.9] | 7.23 [7.12–7.37] | 3.99 [3.92–4.14] | 0.08x | 0.09x | 0.04x | 10.7x | 1.81x |
| fasta | generation | 0.763 [0.755–0.792] | 0.861 [0.860–0.868] | 0.248 [0.245–0.255] | 46.6 [46.6–47.0] | 8.77 [8.74–8.82] | 6.26 [6.13–6.29] | 0.12x | 0.14x | 0.04x | 7.44x | 1.40x |
| knucleotide | hashing | 4.51 [4.50–4.57] | 5.28 [5.24–5.39] | 0.280 [0.278–0.291] | 164.0 [163.8–164.5] | 7.65 [7.60–7.68] | 4.90 [4.88–5.78] | 0.92x | 1.08x | 0.06x | 33.5x | 1.56x |
| pidigits | bignum | 0.296 [0.296–0.303] | 0.300 [0.297–0.303] | 0.047 [0.045–0.048] | 0.449 [0.446–0.463] | 0.130 [0.127–0.131] | 1.92 [1.90–1.93] | 0.15x | 0.16x | 0.02x | 0.23x | 0.07x |
| regexredux | regex | 1.27 [1.27–1.27] | 1.27 [1.27–1.27] | 1.14 [1.13–1.18] | 31.0 [30.8–31.0] | 5.59 [5.53–5.64] | 2.46 [2.43–2.59] | 0.52x | 0.52x | 0.46x | 12.6x | 2.27x |
| revcomp | string | 1.23 [1.23–1.25] | 1.18 [1.18–1.19] | 0.372 [0.371–0.375] | 37.4 [37.3–37.8] | 2.52 [2.51–2.56] | 3.34 [3.26–3.39] | 0.37x | 0.35x | 0.11x | 11.2x | 0.76x |
| spectralnorm | numeric | 1.75 [1.72–1.75] | 1.78 [1.78–1.79] | 0.366 [0.353–0.370] | 245.4 [244.8–245.7] | 65.1 [63.3–67.8] | 2.55 [2.51–2.58] | 0.69x | 0.70x | 0.14x | 96.4x | 25.6x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 219.5 [219.4–220.2] | 192.2 [191.5–192.6] | 28.2 [28.1–28.2] | 3.08s [3.07s–3.11s] | 887.7 [883.7–889.5] | 33.8 [33.6–33.8] | 6.50x | 5.69x | 0.83x | 91.1x | 26.3x |
| matmul | numeric | 5.87 [5.79–5.88] | 17.7 [17.7–17.8] | 6.11 [6.06–6.14] | 1.40s [1.40s–1.40s] | 539.5 [539.1–539.7] | 15.3 [15.3–15.4] | 0.38x | 1.16x | 0.40x | 91.3x | 35.2x |
| primes | numeric | 15.1 [15.1–15.2] | 2.44 [2.44–2.45] | 1.58 [1.58–1.61] | 1.75s [1.75s–1.76s] | 94.6 [94.4–94.6] | 4.39 [4.28–4.51] | 3.45x | 0.56x | 0.36x | 399x | 21.5x |
| base64 | string | 16.5 [16.4–16.5] | 8.77 [8.76–8.97] | 0.555 [0.555–0.556] | 561.7 [558.1–562.0] | 159.1 [157.6–159.4] | 17.4 [17.3–17.6] | 0.95x | 0.51x | 0.03x | 32.4x | 9.17x |
| levenshtein | string | 10.4 [10.4–10.5] | 6.25 [6.12–6.25] | 0.913 [0.902–0.933] | 264.1 [263.3–264.4] | 54.2 [54.1–54.4] | 4.03 [3.96–4.21] | 2.59x | 1.55x | 0.23x | 65.5x | 13.4x |
| json_gen | data | 27.4 [27.2–27.4] | 11.6 [11.6–11.6] | 1.55 [1.53–1.57] | 42.2 [42.1–43.0] | 20.0 [19.8–20.1] | 6.20 [6.08–6.29] | 4.41x | 1.88x | 0.25x | 6.80x | 3.22x |
| collatz | numeric | 316.6 [315.9–318.3] | 315.8 [314.5–318.7] | 225.8 [225.7–226.0] | 7.40s [7.40s–7.40s] | 6.24s [6.23s–6.26s] | 1.42s [1.42s–1.42s] | 0.22x | 0.22x | 0.16x | 5.22x | 4.40x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 277.0 [276.7–298.1] | 186.1 [185.8–186.2] | 60.5 [60.1–60.9] | 7.61s [7.60s–7.72s] | 2.18s [2.18s–2.21s] | 66.5 [66.3–66.8] | 4.17x | 2.80x | 0.91x | 114x | 32.8x |
| array1 | array | 0.821 [0.809–0.823] | 0.807 [0.807–0.807] | 0.317 [0.317–0.326] | 87.1 [86.5–87.5] | 36.2 [35.8–36.4] | 1.91 [1.90–1.96] | 0.43x | 0.42x | 0.17x | 45.7x | 19.0x |
| deriv | symbolic | 28.7 [28.4–28.7] | 7.60 [7.52–7.77] | 2.81 [2.80–2.86] | 76.9 [76.8–77.4] | 59.5 [58.7–59.5] | 3.67 [3.61–3.68] | 7.81x | 2.07x | 0.77x | 21.0x | 16.2x |
| diviter | iterative | 266.6 [266.5–266.6] | 266.7 [262.4–266.7] | 260.8 [258.2–264.0] | 15.03s [15.03s–15.03s] | 26.75s [26.58s–26.78s] | 467.9 [467.9–468.2] | 0.57x | 0.57x | 0.56x | 32.1x | 57.2x |
| divrec | recursive | 5.49 [5.48–5.50] | 1.20 [1.20–1.20] | 4.83 [4.83–4.84] | 23.8 [23.8–23.9] | 35.5 [35.1–36.1] | 7.47 [7.43–7.61] | 0.73x | 0.16x | 0.65x | 3.18x | 4.75x |
| gcbench | allocation | 194.3 [192.1–195.9] | 81.6 [81.4–82.0] | 70.5 [70.3–70.6] | 779.2 [776.2–780.3] | 545.3 [542.5–556.6] | 23.1 [23.0–23.2] | 8.40x | 3.53x | 3.05x | 33.7x | 23.6x |
| paraffins | combinat | 0.246 [0.245–0.251] | 0.214 [0.212–0.218] | 0.047 [0.046–0.048] | 6.85 [6.85–6.87] | 2.51 [2.49–2.51] | 0.982 [0.978–1.00] | 0.25x | 0.22x | 0.05x | 6.98x | 2.56x |
| pnpoly | numeric | 12.5 [12.5–12.5] | 12.5 [12.4–12.5] | 1.94 [1.92–1.94] | 129.3 [129.2–130.6] | 200.3 [200.0–200.4] | 5.76 [5.74–5.78] | 2.16x | 2.17x | 0.34x | 22.4x | 34.8x |
| puzzle | search | 19.0 [18.8–19.1] | 13.4 [13.3–13.6] | 1.28 [1.27–1.29] | 61.5 [61.1–61.8] | 29.1 [29.1–29.2] | 3.28 [3.27–3.28] | 5.78x | 4.08x | 0.39x | 18.8x | 8.88x |
| quicksort | sorting | 11.4 [11.4–11.4] | 0.797 [0.795–0.823] | 0.197 [0.197–0.197] | 151.9 [150.5–151.9] | 19.0 [19.0–19.0] | 1.64 [1.64–1.65] | 6.92x | 0.48x | 0.12x | 92.3x | 11.6x |
| ray | numeric | 0.265 [0.264–0.272] | 0.264 [0.263–0.264] | 0.172 [0.171–0.174] | 21.5 [21.3–21.5] | 13.7 [13.7–13.9] | 3.53 [3.50–3.57] | 0.08x | 0.07x | 0.05x | 6.09x | 3.89x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 9.89 [9.84–9.91] | 9.06 [8.98–9.06] | 0.528 [0.520–0.532] | 418.1 [415.5–419.1] | 216.2 [216.1–238.8] | 17.7 [17.6–17.7] | 0.56x | 0.51x | 0.03x | 23.6x | 12.2x |
| navier_stokes | numeric | 96.3 [96.3–96.3] | 95.7 [95.7–95.8] | 46.9 [46.8–46.9] | 386.7 [384.8–390.8] | 98.6 [98.0–99.2] | 14.0 [13.9–14.0] | 6.87x | 6.84x | 3.35x | 27.6x | 7.04x |
| splay | data | 313.4 [311.8–332.1] | 295.6 [294.6–296.5] | 18.9 [18.7–19.0] | 491.7 [490.1–492.3] | 143.6 [143.3–144.3] | 18.2 [18.0–19.4] | 17.2x | 16.2x | 1.04x | 27.0x | 7.88x |
| hashmap | data | 127.6 [127.2–130.6] | 42.9 [40.7–73.7] | 2.80 [2.72–2.84] | 1.72s [1.71s–1.73s] | 315.6 [314.4–315.9] | 15.2 [15.2–15.3] | 8.39x | 2.82x | 0.18x | 113x | 20.8x |
| crypto_sha1 | crypto | 59.1 [59.0–59.9] | 25.2 [25.2–25.4] | 2.64 [2.64–2.67] | 386.8 [386.3–387.1] | 218.6 [218.3–218.8] | 8.77 [8.58–8.86] | 6.74x | 2.87x | 0.30x | 44.1x | 24.9x |
| raytrace3d | 3d | 67.3 [66.4–67.3] | 15.2 [15.1–15.4] | 2.17 [2.17–2.18] | 624.5 [620.1–625.9] | 162.8 [162.1–164.8] | 18.3 [18.2–18.4] | 3.68x | 0.83x | 0.12x | 34.2x | 8.90x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 235.9 [235.5–236.9] | 137.8 [137.7–141.7] | 13.0 [12.9–13.0] | 2.86s [2.85s–2.89s] | 608.0 [605.0–608.4] | 38.9 [38.8–39.6] | 6.06x | 3.54x | 0.33x | 73.4x | 15.6x |
| microdiff | data-diff | 51.4 [51.2–51.4] | 57.1 [56.7–57.8] | 2.61 [2.61–2.61] | 1.26s [1.25s–1.26s] | 109.6 [107.8–110.5] | 16.0 [15.9–16.9] | 3.21x | 3.56x | 0.16x | 78.3x | 6.84x |
| hyphen | hyphenation | 69.9 [69.4–69.9] | 69.5 [69.4–69.7] | 1.48 [1.47–1.48] | 534.1 [533.6–534.2] | 80.3 [79.9–81.6] | 6.82 [6.80–7.49] | 10.3x | 10.2x | 0.22x | 78.4x | 11.8x |
| prettier_ast | formatting | 922.1 [915.2–925.4] | 666.9 [664.5–667.5] | 41.4 [41.3–42.1] | 5.02s [5.02s–5.05s] | 1.42s [1.41s–1.45s] | 99.8 [99.0–101.8] | 9.24x | 6.68x | 0.42x | 50.3x | 14.2x |
| text_search | search | 15.23s [15.19s–15.30s] | 1.79s [1.79s–1.82s] | 538.7 [538.5–568.5] | 117.91s [117.77s–118.34s] | 38.99s [38.84s–39.01s] | 780.9 [780.7–781.3] | 19.5x | 2.29x | 0.69x | 151x | 49.9x |
| three_way_merge | merge | 3.38s [3.38s–3.39s] | 3.76s [3.74s–3.77s] | 2.13s [2.13s–2.14s] | 15.02s [15.01s–15.02s] | 6.18s [6.17s–6.19s] | 966.3 [965.8–966.4] | 3.50x | 3.90x | 2.21x | 15.5x | 6.40x |
| log_pipeline | log-processing | 5.95s [5.93s–5.99s] | 6.12s [6.04s–6.14s] | 620.3 [616.9–622.2] | 17.22s [17.16s–17.23s] | 9.42s [9.39s–9.52s] | 943.9 [940.9–947.6] | 6.30x | 6.49x | 0.66x | 18.2x | 9.98x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.40x | 0.41x | 1.02x | 1.11x | 0.41x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 0.99x | 0.97x | 0.92x | 5.06x | 0.80x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.50x | 0.53x | 1.03x | 1.94x | 0.33x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 0.75x | 0.67x | 0.73x | 10.9x | 2.92x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 0.83x | 0.66x | 1.00x | 4.13x | 1.92x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 2.51x | 1.94x | 0.91x | 12.0x | 3.66x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 4.22x | 2.88x | 0.93x | 31.9x | 6.84x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 63 | 0.95x | 0.85x | 0.94x | 4.92x | 1.27x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 15.9 [15.9–17.1] | 17.0 [16.9–17.2] | 47.6 [47.4–685.0] | 41.8 [40.6–43.4] | 25.1 [24.2–26.9] | 44.7 [44.6–92.2] | 0.36x | 0.38x | 1.06x | 0.93x | 0.56x |
| fibfp | recursive | 16.9 [16.6–17.1] | 16.1 [15.6–16.2] | 46.2 [45.9–46.5] | 41.0 [40.9–41.3] | 25.0 [24.4–41.1] | 44.8 [44.4–45.0] | 0.38x | 0.36x | 1.03x | 0.92x | 0.56x |
| tak | recursive | 15.7 [15.6–16.0] | 15.3 [15.1–15.5] | 45.4 [45.1–45.5] | 28.2 [27.7–28.2] | 8.41 [8.22–8.95] | 43.8 [43.4–44.3] | 0.36x | 0.35x | 1.04x | 0.64x | 0.19x |
| cpstak | closure | 15.8 [15.8–16.0] | 15.3 [15.0–15.4] | 45.2 [45.2–45.4] | 29.9 [29.9–30.9] | 11.0 [10.8–11.5] | 44.6 [44.4–45.0] | 0.36x | 0.34x | 1.01x | 0.67x | 0.25x |
| sum | iterative | 15.3 [15.1–15.3] | 15.3 [15.3–15.4] | 45.3 [44.8–45.3] | 54.2 [53.9–54.7] | 36.7 [36.5–37.1] | 44.6 [44.0–44.7] | 0.34x | 0.34x | 1.01x | 1.21x | 0.82x |
| sumfp | iterative | 14.7 [14.6–14.7] | 15.4 [15.3–15.4] | 44.6 [44.5–44.7] | 28.5 [28.3–29.1] | 9.13 [9.04–9.73] | 44.1 [43.9–44.2] | 0.33x | 0.35x | 1.01x | 0.65x | 0.21x |
| nqueens | backtrack | 25.4 [25.2–25.5] | 27.7 [27.7–28.0] | 45.4 [44.9–46.1] | 160.1 [127.4–185.0] | 13.4 [13.3–14.6] | 45.1 [45.0–45.2] | 0.56x | 0.61x | 1.01x | 3.55x | 0.30x |
| fft | numeric | 23.3 [23.2–23.5] | 25.6 [25.6–26.2] | 45.4 [45.2–45.5] | 58.1 [57.5–58.7] | 9.01 [8.59–9.09] | 45.0 [44.5–45.3] | 0.52x | 0.57x | 1.01x | 1.29x | 0.20x |
| mbrot | numeric | 18.8 [18.7–19.0] | 19.1 [19.0–19.4] | 45.3 [45.1–45.5] | 48.3 [47.6–48.9] | 23.4 [23.1–23.9] | 44.6 [44.3–44.8] | 0.42x | 0.43x | 1.02x | 1.08x | 0.52x |
| ack | recursive | 26.7 [26.7–27.2] | 24.8 [24.5–25.0] | 56.9 [56.7–78.2] | 110.2 [109.3–110.9] | 107.4 [107.1–107.5] | 56.3 [56.1–56.3] | 0.48x | 0.44x | 1.01x | 1.96x | 1.91x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 15.9 [15.9–16.2] | 16.6 [16.5–16.7] | 45.1 [44.8–45.1] | 34.3 [34.2–34.3] | 6.21 [5.86–6.87] | 44.1 [43.6–45.9] | 0.36x | 0.38x | 1.02x | 0.78x | 0.14x |
| permute | micro | 16.7 [16.5–16.7] | 17.1 [17.0–17.3] | 45.1 [45.0–45.2] | 40.5 [40.5–41.4] | 7.01 [6.97–7.63] | 44.8 [44.6–68.7] | 0.37x | 0.38x | 1.01x | 0.90x | 0.16x |
| queens | micro | 21.3 [20.8–21.3] | 23.6 [23.6–23.6] | 45.4 [45.2–45.4] | 40.1 [39.7–40.2] | 6.51 [6.37–7.14] | 44.8 [44.5–44.9] | 0.47x | 0.53x | 1.01x | 0.89x | 0.15x |
| towers | micro | 21.3 [20.8–21.3] | 21.9 [21.7–22.1] | 45.3 [45.0–45.5] | 55.1 [55.1–55.3] | 8.00 [7.61–8.43] | 45.0 [44.9–45.1] | 0.47x | 0.49x | 1.01x | 1.23x | 0.18x |
| bounce | micro | 23.1 [23.0–23.2] | 25.1 [25.1–25.2] | 45.4 [45.4–45.7] | 114.5 [114.2–114.8] | 7.21 [7.14–7.77] | 44.5 [44.3–44.8] | 0.52x | 0.56x | 1.02x | 2.57x | 0.16x |
| list | micro | 19.3 [18.9–19.7] | 19.2 [19.1–19.2] | 45.0 [44.8–45.4] | 36.4 [36.3–36.6] | 6.45 [6.28–6.88] | 44.3 [44.2–44.3] | 0.44x | 0.43x | 1.02x | 0.82x | 0.15x |
| storage | micro | 17.4 [17.4–17.7] | 18.0 [17.6–18.0] | 45.0 [44.9–45.1] | 110.0 [108.7–110.2] | 8.33 [8.18–8.95] | 44.8 [44.7–45.1] | 0.39x | 0.40x | 1.00x | 2.46x | 0.19x |
| mandelbrot | compute | 60.9 [60.8–60.9] | 61.1 [60.9–61.3] | 76.2 [76.0–76.9] | 1.08s [1.05s–1.08s] | 876.2 [875.3–880.5] | 75.3 [75.3–76.2] | 0.81x | 0.81x | 1.01x | 14.3x | 11.6x |
| nbody | compute | 88.0 [87.8–88.7] | 81.1 [80.1–81.5] | 47.8 [47.7–48.5] | 639.4 [637.2–641.5] | 165.8 [165.6–184.1] | 50.0 [49.3–51.6] | 1.76x | 1.62x | 0.96x | 12.8x | 3.32x |
| richards | macro | 462.5 [462.2–465.5] | 442.2 [440.2–462.7] | 77.0 [76.9–77.7] | 1.29s [1.27s–1.29s] | 198.0 [197.9–198.0] | 91.6 [91.1–92.0] | 5.05x | 4.83x | 0.84x | 14.1x | 2.16x |
| json | macro | 51.6 [51.4–76.2] | 51.6 [51.2–51.8] | 52.6 [52.3–54.5] | 231.4 [229.3–232.7] | 18.3 [18.0–19.1] | 47.2 [47.0–48.1] | 1.09x | 1.09x | 1.12x | 4.91x | 0.39x |
| deltablue | macro | 242.5 [242.1–260.5] | 191.1 [190.6–193.0] | 53.6 [53.3–53.7] | 762.7 [760.9–786.7] | 107.3 [106.9–108.3] | 56.2 [55.9–57.6] | 4.31x | 3.40x | 0.95x | 13.6x | 1.91x |
| havlak | macro | 169.8 [166.5–169.9] | 158.2 [157.5–160.6] | 54.0 [53.8–54.1] | 21.61s [21.59s–22.44s] | 3.34s [3.31s–3.36s] | 142.8 [142.5–144.6] | 1.19x | 1.11x | 0.38x | 151x | 23.4x |
| cd | macro | 646.3 [644.2–650.6] | 559.4 [557.0–574.7] | 67.7 [67.4–68.0] | 5.66s [5.64s–5.75s] | 967.8 [964.6–1.00s] | 81.3 [81.2–82.2] | 7.95x | 6.88x | 0.83x | 69.6x | 11.9x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 26.8 [26.4–27.5] | 22.3 [21.9–22.3] | 48.6 [48.1–49.1] | 63.2 [62.3–63.6] | 29.3 [28.8–29.9] | 45.7 [45.6–46.2] | 0.59x | 0.49x | 1.06x | 1.39x | 0.64x |
| fannkuch | permutation | 21.1 [20.9–21.2] | 24.2 [24.1–25.1] | 45.3 [45.2–45.6] | 74.2 [74.2–74.6] | 13.1 [12.7–13.4] | 45.1 [44.7–45.3] | 0.47x | 0.54x | 1.00x | 1.64x | 0.29x |
| fasta | generation | 24.5 [24.5–24.7] | 27.6 [27.6–27.7] | 45.6 [45.6–67.1] | 83.1 [82.9–83.9] | 14.5 [14.3–15.4] | 47.9 [47.1–47.9] | 0.51x | 0.58x | 0.95x | 1.74x | 0.30x |
| knucleotide | hashing | 30.5 [30.1–31.0] | 37.9 [37.7–38.4] | 46.1 [45.8–46.4] | 198.5 [197.7–199.6] | 13.3 [13.1–13.9] | 46.6 [46.2–46.7] | 0.65x | 0.81x | 0.99x | 4.26x | 0.28x |
| pidigits | bignum | 19.8 [19.7–19.8] | 20.0 [19.8–20.0] | 47.2 [47.0–47.3] | 34.7 [34.4–34.8] | 5.59 [5.40–6.03] | 42.8 [42.2–43.2] | 0.46x | 0.47x | 1.10x | 0.81x | 0.13x |
| regexredux | regex | 15.2 [14.9–15.3] | 15.2 [14.8–15.2] | 47.8 [47.6–47.9] | 64.1 [64.0–64.4] | 11.5 [11.0–12.1] | 43.7 [43.4–43.8] | 0.35x | 0.35x | 1.10x | 1.47x | 0.26x |
| revcomp | string | 22.3 [22.3–22.7] | 22.6 [22.5–23.1] | 45.6 [45.3–45.7] | 71.9 [71.5–73.1] | 8.21 [8.02–9.09] | 44.7 [43.9–45.3] | 0.50x | 0.51x | 1.02x | 1.61x | 0.18x |
| spectralnorm | numeric | 24.5 [23.9–24.6] | 26.6 [26.5–26.8] | 45.5 [45.3–45.6] | 280.0 [279.4–280.4] | 70.9 [69.6–93.4] | 45.9 [45.4–46.0] | 0.53x | 0.58x | 0.99x | 6.10x | 1.54x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 245.6 [244.6–245.8] | 216.1 [214.3–216.4] | 73.8 [73.7–74.1] | 3.11s [3.11s–3.15s] | 894.9 [890.3–896.3] | 77.4 [77.0–77.8] | 3.17x | 2.79x | 0.95x | 40.2x | 11.6x |
| matmul | numeric | 28.3 [28.1–28.4] | 41.7 [41.6–41.9] | 51.2 [51.0–51.3] | 1.43s [1.43s–1.43s] | 545.9 [545.4–545.9] | 58.3 [58.2–58.5] | 0.48x | 0.72x | 0.88x | 24.6x | 9.36x |
| primes | numeric | 32.3 [32.3–54.6] | 19.6 [19.4–19.6] | 46.5 [46.1–46.5] | 1.79s [1.79s–1.80s] | 100.8 [100.7–100.9] | 47.1 [46.6–47.7] | 0.69x | 0.42x | 0.99x | 38.1x | 2.14x |
| base64 | string | 38.1 [38.1–38.2] | 38.5 [37.4–45.0] | 45.9 [45.7–46.5] | 594.3 [590.7–594.8] | 165.7 [163.9–165.8] | 60.8 [60.7–61.7] | 0.63x | 0.63x | 0.75x | 9.78x | 2.73x |
| levenshtein | string | 40.9 [40.6–41.4] | 31.9 [31.8–32.2] | 46.1 [46.1–46.7] | 299.0 [298.0–299.8] | 60.4 [60.2–60.5] | 47.8 [47.3–47.8] | 0.86x | 0.67x | 0.97x | 6.26x | 1.26x |
| json_gen | data | 51.6 [51.3–52.0] | 35.9 [35.9–36.0] | 47.0 [46.8–47.0] | 75.6 [75.5–76.4] | 25.9 [25.5–26.0] | 49.3 [49.2–72.1] | 1.05x | 0.73x | 0.95x | 1.53x | 0.52x |
| collatz | numeric | 328.9 [328.5–332.2] | 330.4 [330.2–330.5] | 271.2 [271.0–271.4] | 7.43s [7.42s–7.43s] | 6.25s [6.24s–6.26s] | 1.46s [1.46s–1.46s] | 0.22x | 0.23x | 0.19x | 5.08x | 4.27x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 303.3 [303.0–304.5] | 211.8 [211.6–212.2] | 105.9 [105.8–106.6] | 7.64s [7.63s–7.76s] | 2.19s [2.19s–2.21s] | 109.9 [109.6–110.7] | 2.76x | 1.93x | 0.96x | 69.6x | 19.9x |
| array1 | array | 15.8 [15.8–16.1] | 16.2 [16.1–16.3] | 45.2 [44.9–45.2] | 113.0 [112.9–113.3] | 41.9 [41.8–42.4] | 44.7 [44.7–44.8] | 0.35x | 0.36x | 1.01x | 2.53x | 0.94x |
| deriv | symbolic | 52.8 [52.7–53.0] | 27.5 [27.4–27.6] | 49.1 [48.7–49.4] | 107.7 [107.4–108.4] | 65.4 [64.5–65.6] | 47.6 [47.6–47.8] | 1.11x | 0.58x | 1.03x | 2.26x | 1.37x |
| diviter | iterative | 283.3 [283.0–283.7] | 278.4 [277.7–278.8] | 306.4 [303.9–308.8] | 15.05s [15.05s–15.06s] | 26.76s [26.59s–26.78s] | 511.5 [511.3–512.2] | 0.55x | 0.54x | 0.60x | 29.4x | 52.3x |
| divrec | recursive | 21.3 [21.0–21.5] | 16.5 [16.5–16.9] | 48.8 [48.7–48.9] | 50.3 [50.1–51.1] | 40.9 [40.4–42.0] | 49.3 [49.1–49.3] | 0.43x | 0.34x | 0.99x | 1.02x | 0.83x |
| gcbench | allocation | 215.3 [212.7–220.2] | 122.0 [121.1–123.0] | 115.4 [114.9–115.5] | 814.5 [812.5–817.0] | 556.3 [554.2–567.8] | 65.4 [65.2–65.5] | 3.29x | 1.86x | 1.76x | 12.5x | 8.50x |
| paraffins | combinat | 29.1 [28.9–29.4] | 27.8 [27.7–27.9] | 44.8 [44.6–45.4] | 51.6 [51.1–51.7] | 7.96 [7.92–8.49] | 43.3 [42.8–43.3] | 0.67x | 0.64x | 1.03x | 1.19x | 0.18x |
| pnpoly | numeric | 30.3 [30.1–30.5] | 31.3 [31.1–31.4] | 46.0 [45.9–46.3] | 158.3 [158.1–159.4] | 206.3 [205.9–206.4] | 48.0 [47.9–48.1] | 0.63x | 0.65x | 0.96x | 3.30x | 4.30x |
| puzzle | search | 38.6 [38.5–38.7] | 34.4 [34.3–34.4] | 45.4 [45.4–45.9] | 90.0 [89.4–90.5] | 34.5 [34.4–35.3] | 45.4 [45.3–45.5] | 0.85x | 0.76x | 1.00x | 1.98x | 0.76x |
| quicksort | sorting | 31.3 [31.0–31.4] | 22.4 [21.9–22.5] | 45.4 [45.1–45.8] | 182.6 [181.3–182.6] | 24.9 [24.8–25.3] | 44.2 [43.7–44.7] | 0.71x | 0.51x | 1.03x | 4.13x | 0.56x |
| ray | numeric | 28.4 [28.1–28.5] | 20.6 [20.3–20.7] | 44.7 [44.6–44.8] | 56.2 [55.9–56.3] | 19.1 [19.0–19.8] | 45.5 [45.4–46.0] | 0.62x | 0.45x | 0.98x | 1.23x | 0.42x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 134.9 [134.7–156.3] | 172.3 [171.7–173.8] | 49.8 [49.7–49.9] | 521.9 [520.5–523.8] | 223.2 [223.1–245.9] | 64.5 [62.9–65.1] | 2.09x | 2.67x | 0.77x | 8.09x | 3.46x |
| navier_stokes | numeric | 178.0 [177.5–178.7] | 177.9 [177.8–177.9] | 95.1 [94.6–95.2] | 561.8 [561.6–566.4] | 109.9 [109.2–110.6] | 60.3 [59.8–60.4] | 2.95x | 2.95x | 1.58x | 9.32x | 1.82x |
| splay | data | 375.5 [371.8–378.4] | 361.4 [360.9–363.1] | 66.4 [66.4–66.7] | 1.23s [1.22s–1.23s] | 552.0 [551.6–556.8] | 91.8 [91.3–92.6] | 4.09x | 3.94x | 0.72x | 13.4x | 6.02x |
| hashmap | data | 158.7 [158.1–160.2] | 76.9 [76.1–77.9] | 48.3 [48.2–48.3] | 1.82s [1.81s–1.82s] | 323.0 [321.8–323.5] | 59.3 [59.1–60.0] | 2.68x | 1.30x | 0.81x | 30.7x | 5.45x |
| crypto_sha1 | crypto | 89.3 [89.1–90.6] | 55.6 [55.3–55.9] | 49.2 [49.0–49.6] | 431.2 [431.1–431.6] | 225.4 [225.0–225.6] | 52.7 [52.4–52.7] | 1.70x | 1.06x | 0.93x | 8.19x | 4.28x |
| raytrace3d | 3d | 137.2 [136.3–138.1] | 78.7 [78.4–79.1] | 52.0 [52.0–53.0] | 722.0 [717.3–722.1] | 170.0 [169.3–182.0] | 62.3 [62.2–63.8] | 2.20x | 1.26x | 0.83x | 11.6x | 2.73x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 263.1 [262.5–264.5] | 161.9 [161.5–161.9] | 58.1 [57.9–58.1] | 3.03s [3.02s–3.06s] | 615.9 [612.3–616.1] | 82.3 [82.2–83.2] | 3.20x | 1.97x | 0.71x | 36.8x | 7.48x |
| microdiff | data-diff | 84.8 [84.3–84.8] | 90.2 [90.1–90.2] | 50.6 [50.3–50.9] | 1.31s [1.31s–1.32s] | 116.2 [113.8–116.5] | 59.0 [58.5–59.0] | 1.44x | 1.53x | 0.86x | 22.3x | 1.97x |
| hyphen | hyphenation | 115.1 [114.3–115.5] | 116.5 [116.3–117.9] | 74.9 [74.8–75.9] | 1.78s [1.78s–1.79s] | 101.5 [101.2–103.2] | 58.4 [57.3–58.4] | 1.97x | 1.99x | 1.28x | 30.5x | 1.74x |
| prettier_ast | formatting | 1.00s [1.00s–1.02s] | 778.6 [764.7–789.5] | 104.9 [104.0–105.7] | 5.22s [5.22s–5.25s] | 1.43s [1.42s–1.46s] | 144.2 [144.0–147.4] | 6.97x | 5.40x | 0.73x | 36.2x | 9.92x |
| text_search | search | 15.24s [15.23s–15.29s] | 1.82s [1.82s–1.83s] | 586.0 [585.5–615.4] | 117.98s [117.84s–118.41s] | 39.00s [38.85s–39.02s] | 825.6 [825.3–825.8] | 18.5x | 2.21x | 0.71x | 143x | 47.2x |
| three_way_merge | merge | 3.39s [3.38s–3.39s] | 3.77s [3.76s–3.77s] | 2.18s [2.18s–2.19s] | 15.06s [15.05s–15.06s] | 6.19s [6.18s–6.19s] | 1.01s [1.01s–1.01s] | 3.36x | 3.73x | 2.16x | 14.9x | 6.12x |
| log_pipeline | log-processing | 6.07s [6.00s–6.08s] | 6.12s [6.11s–6.13s] | 669.2 [665.3–670.4] | 17.31s [17.26s–17.33s] | 9.45s [9.42s–9.55s] | 996.2 [992.9–999.9] | 6.10x | 6.14x | 0.67x | 17.4x | 9.49x |

