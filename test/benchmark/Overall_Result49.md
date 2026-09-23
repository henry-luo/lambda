# Lambda Benchmark Results: Result49

- **Date:** 2026-09-24
- **Platform:** Darwin arm64
- **Lambda commit:** `537a867839e57be1ca0ee20e7ee4c929ec694db9`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v49-537a867839` (18,719,656 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 58.80s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 58.7s (batched 57.7s: sync 40.1s, async 17.7s; non-batched 1.0s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v49.json`
- **Separately measured:** LambdaJS, LambdaJS (auto) measured on 2026-09-24, 3 run(s) from `test/benchmark/benchmark_results_v49_ljs.json` on Lambda commit `537a867839`. LambdaJS re-measured on the archived v49 binary: Part 1 pinned JS_EXECUTION_BACKEND=mir, Part 2 unpinned AUTO (D8.1.3v19); the original v49 LambdaJS cells ran unpinned AUTO for both parts.
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, the MIR columns pin `LAMBDA_TIER=jit`, and LambdaJS pins `JS_EXECUTION_BACKEND=mir`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 0.39x | 0.31x | 0.21x | 1.07x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 1.09x | 0.47x | 0.09x | 12.2x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 0.40x | 0.24x | 0.10x | 4.34x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 1.48x | 0.74x | 0.23x | 11.7x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 1.10x | 0.64x | 0.33x | 7.94x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 4.02x | 2.20x | 0.29x | 25.8x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 4.75x | 2.46x | 0.47x | 24.9x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 1.13x | 0.63x | 0.20x | 7.80x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 59 | 0.57x | 1.03x | 3.14x | 59 |
| complete current suite | 63 | 0.63x | 1.13x | 3.16x | 63 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 3.16x over 63 of 63 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| text/microdiff | 58.4 | 2.62 | 22.3x |
| jetstream/splay | 307.4 | 18.9 | 16.3x |
| awfy/havlak | 27.4 | 1.86 | 14.7x |
| jetstream/cube3d | 7.18 | 0.532 | 13.5x |
| text/prettier_ast | 536.8 | 41.5 | 12.9x |
| awfy/deltablue | 14.4 | 1.16 | 12.4x |
| awfy/richards | 337.5 | 30.0 | 11.3x |
| larceny/puzzle | 13.9 | 1.28 | 10.9x |
| awfy/cd | 159.5 | 15.0 | 10.6x |
| kostya/base64 | 5.71 | 0.567 | 10.1x |
| jetstream/hashmap | 27.8 | 2.83 | 9.82x |
| jetstream/crypto_sha1 | 25.8 | 2.67 | 9.64x |

---

### Notable Results

- Missing timings: **0** cells

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 13.26s | 94.8 | 140x |
| awfy/cd | 3.95s | 36.1 | 109x |
| awfy/nbody | 590.1 | 5.49 | 107x |
| larceny/triangl | 6.00s | 68.4 | 87.8x |
| jetstream/hashmap | 1.24s | 15.2 | 81.0x |
| kostya/brainfuck | 1.81s | 33.6 | 54.0x |
| text/fast_diff | 2.04s | 39.1 | 52.1x |
| text/microdiff | 810.1 | 16.2 | 49.9x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| r7rs/sumfp | 0.067 | 0.888 | 0.08x |
| beng/pidigits | 0.534 | 1.82 | 0.29x |
| r7rs/tak | 0.327 | 0.805 | 0.41x |
| r7rs/sum | 0.624 | 1.21 | 0.52x |
| awfy/sieve | 0.213 | 0.379 | 0.56x |
| r7rs/cpstak | 0.699 | 0.982 | 0.71x |
| r7rs/ack | 12.7 | 13.3 | 0.96x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.79 [1.79–1.83] | 1.73 [1.70–1.77] | 1.14 [1.03–1.19] | 1.91 [1.84–1.98] | 1.77 [1.76–1.78] | 1.01x | 0.98x | 0.64x | 1.08x |
| fibfp | recursive | 2.51 [2.48–2.51] | 1.47 [1.46–1.49] | 1.12 [1.12–1.24] | 1.89 [1.83–1.99] | 1.81 [1.76–1.87] | 1.38x | 0.81x | 0.62x | 1.04x |
| tak | recursive | 0.170 [0.169–0.175] | 0.161 [0.161–0.165] | 0.118 [0.111–0.120] | 0.327 [0.322–0.344] | 0.805 [0.777–0.809] | 0.21x | 0.20x | 0.15x | 0.41x |
| cpstak | closure | 0.350 [0.341–0.351] | 0.324 [0.323–0.336] | 0.224 [0.222–0.230] | 0.699 [0.652–0.746] | 0.982 [0.981–0.999] | 0.36x | 0.33x | 0.23x | 0.71x |
| sum | iterative | 0.273 [0.266–0.277] | 0.266 [0.266–0.269] | 0.269 [0.269–0.342] | 0.624 [0.622–0.672] | 1.21 [1.20–1.28] | 0.23x | 0.22x | 0.22x | 0.52x |
| sumfp | iterative | 0.071 [0.069–0.077] | 0.068 [0.068–0.069] | 0.084 [0.079–0.085] | 0.067 [0.065–0.067] | 0.888 [0.876–0.899] | 0.08x | 0.08x | 0.09x | 0.08x |
| nqueens | backtrack | 1.00 [0.993–1.01] | 1.31 [1.28–1.38] | 0.367 [0.358–0.379] | 29.0 [28.7–29.1] | 1.78 [1.72–1.93] | 0.57x | 0.74x | 0.21x | 16.3x |
| fft | numeric | 0.247 [0.247–0.256] | 0.089 [0.089–0.090] | 0.025 [0.024–0.027] | 2.65 [2.64–2.83] | 1.56 [1.55–1.61] | 0.16x | 0.06x | 0.02x | 1.70x |
| mbrot | numeric | 0.728 [0.728–0.739] | 0.577 [0.569–0.591] | 0.442 [0.441–0.458] | 10.9 [10.9–11.0] | 1.91 [1.89–1.92] | 0.38x | 0.30x | 0.23x | 5.73x |
| ack | recursive | 15.1 [14.7–15.8] | 10.8 [10.7–11.3] | 11.8 [11.7–11.8] | 12.7 [12.6–13.2] | 13.3 [13.3–13.3] | 1.13x | 0.81x | 0.89x | 0.96x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.026 [0.025–0.026] | 0.027 [0.027–0.027] | 0.016 [0.015–0.018] | 0.213 [0.213–0.247] | 0.379 [0.378–0.399] | 0.07x | 0.07x | 0.04x | 0.56x |
| permute | micro | 0.291 [0.289–0.291] | 0.083 [0.082–0.086] | 0.026 [0.025–0.026] | 5.83 [5.77–5.92] | 0.806 [0.805–0.810] | 0.36x | 0.10x | 0.03x | 7.24x |
| queens | micro | 0.177 [0.173–0.190] | 0.067 [0.065–0.070] | 0.017 [0.017–0.021] | 3.34 [3.33–3.36] | 0.638 [0.635–0.647] | 0.28x | 0.11x | 0.03x | 5.24x |
| towers | micro | 0.942 [0.933–0.946] | 0.236 [0.232–0.246] | 0.027 [0.027–0.027] | 9.32 [9.27–9.40] | 1.12 [1.10–1.16] | 0.84x | 0.21x | 0.02x | 8.30x |
| bounce | micro | 0.065 [0.064–0.069] | 0.098 [0.095–0.102] | 0.026 [0.024–0.027] | 4.00 [3.98–4.04] | 0.538 [0.530–0.566] | 0.12x | 0.18x | 0.05x | 7.43x |
| list | micro | 0.617 [0.612–0.640] | 0.130 [0.129–0.130] | 0.023 [0.021–0.023] | 2.19 [2.17–2.22] | 0.485 [0.478–0.492] | 1.27x | 0.27x | 0.05x | 4.51x |
| storage | micro | 0.599 [0.588–0.610] | 0.348 [0.347–0.363] | 0.095 [0.090–0.099] | 4.17 [4.05–4.19] | 0.634 [0.631–0.644] | 0.94x | 0.55x | 0.15x | 6.58x |
| mandelbrot | compute | 39.6 [39.5–39.6] | 39.5 [39.5–39.6] | 30.6 [30.5–30.6] | 79.8 [79.1–79.9] | 31.2 [31.1–31.2] | 1.27x | 1.27x | 0.98x | 2.56x |
| nbody | compute | 6.90 [6.87–6.95] | 3.48 [3.47–3.50] | 1.51 [1.48–1.51] | 590.1 [577.8–594.5] | 5.49 [5.38–6.62] | 1.26x | 0.63x | 0.27x | 107x |
| richards | macro | 368.8 [368.4–373.3] | 337.5 [336.5–421.0] | 30.0 [29.1–30.5] | 894.5 [890.9–904.3] | 46.5 [46.4–47.9] | 7.92x | 7.25x | 0.64x | 19.2x |
| json | macro | 6.41 [6.37–6.46] | 1.86 [1.85–1.95] | 0.258 [0.257–0.279] | 41.6 [41.4–41.9] | 2.65 [2.65–2.72] | 2.41x | 0.70x | 0.10x | 15.7x |
| deltablue | macro | 126.1 [125.9–126.7] | 14.4 [14.3–14.4] | 1.16 [1.15–1.18] | 384.4 [382.7–385.2] | 11.9 [11.6–12.0] | 10.6x | 1.21x | 0.10x | 32.4x |
| havlak | macro | 73.1 [73.0–73.1] | 27.4 [27.4–27.8] | 1.86 [1.84–1.87] | 13.26s [13.24s–13.33s] | 94.8 [94.0–95.4] | 0.77x | 0.29x | 0.02x | 140x |
| cd | macro | 572.8 [566.2–573.5] | 159.5 [158.7–161.0] | 15.0 [14.8–15.1] | 3.95s [3.94s–4.01s] | 36.1 [35.8–36.2] | 15.9x | 4.42x | 0.42x | 109x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 8.63 [8.56–8.70] | 2.28 [2.27–2.31] | 2.97 [2.96–3.03] | 20.1 [19.7–20.1] | 3.89 [3.83–4.14] | 2.22x | 0.59x | 0.76x | 5.15x |
| fannkuch | permutation | 0.325 [0.324–0.336] | 0.370 [0.367–0.373] | 0.149 [0.148–0.152] | 20.3 [20.3–20.6] | 3.85 [3.83–4.30] | 0.08x | 0.10x | 0.04x | 5.29x |
| fasta | generation | 0.749 [0.736–0.775] | 0.901 [0.886–0.962] | 0.243 [0.239–0.246] | 26.7 [26.6–69.7] | 6.13 [6.05–6.28] | 0.12x | 0.15x | 0.04x | 4.35x |
| knucleotide | hashing | 5.00 [4.98–5.01] | 0.578 [0.576–0.607] | 0.279 [0.277–0.547] | 21.0 [20.7–21.1] | 4.77 [4.71–4.82] | 1.05x | 0.12x | 0.06x | 4.39x |
| pidigits | bignum | 0.347 [0.345–0.354] | 0.347 [0.347–0.361] | 0.045 [0.044–0.045] | 0.534 [0.508–0.585] | 1.82 [1.77–1.83] | 0.19x | 0.19x | 0.02x | 0.29x |
| regexredux | regex | 1.29 [1.27–1.36] | 1.27 [1.26–1.27] | 1.14 [1.14–1.14] | 8.86 [8.84–9.33] | 2.33 [2.25–2.37] | 0.55x | 0.55x | 0.49x | 3.81x |
| revcomp | string | 1.18 [1.18–1.19] | 1.13 [1.11–1.13] | 0.375 [0.373–0.388] | 22.3 [22.2–22.7] | 3.30 [3.27–3.40] | 0.36x | 0.34x | 0.11x | 6.76x |
| spectralnorm | numeric | 1.85 [1.84–1.85] | 0.785 [0.783–0.807] | 0.351 [0.351–0.352] | 82.3 [81.6–111.9] | 2.59 [2.57–2.73] | 0.72x | 0.30x | 0.14x | 31.8x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 191.5 [191.5–191.9] | 193.6 [193.0–193.7] | 28.6 [27.8–29.1] | 1.81s [1.80s–1.87s] | 33.6 [33.5–33.6] | 5.70x | 5.76x | 0.85x | 54.0x |
| matmul | numeric | 5.13 [5.12–5.18] | 5.09 [5.07–5.11] | 6.04 [6.02–6.04] | 248.3 [247.9–248.8] | 15.4 [15.3–15.7] | 0.33x | 0.33x | 0.39x | 16.1x |
| primes | numeric | 17.5 [17.4–17.6] | 2.21 [2.20–2.30] | 1.59 [1.59–1.59] | 56.5 [56.3–139.5] | 4.58 [4.33–4.64] | 3.82x | 0.48x | 0.35x | 12.3x |
| base64 | string | 15.9 [15.8–16.0] | 5.71 [5.70–5.74] | 0.567 [0.559–0.591] | 478.5 [477.4–479.3] | 17.5 [17.4–17.6] | 0.91x | 0.33x | 0.03x | 27.4x |
| levenshtein | string | 11.8 [11.7–11.9] | 5.05 [5.00–5.06] | 0.902 [0.899–0.908] | 83.5 [83.5–83.8] | 3.96 [3.95–4.16] | 2.98x | 1.28x | 0.23x | 21.1x |
| json_gen | data | 26.1 [25.8–26.2] | 9.81 [9.79–9.87] | 1.50 [1.49–1.53] | 27.1 [26.6–27.3] | 6.22 [6.12–6.22] | 4.20x | 1.58x | 0.24x | 4.36x |
| collatz | numeric | 275.3 [275.1–277.9] | 276.2 [275.7–277.7] | 225.5 [225.2–225.6] | 1.59s [1.59s–1.59s] | 1.43s [1.42s–1.55s] | 0.19x | 0.19x | 0.16x | 1.11x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 244.5 [243.6–246.6] | 166.8 [166.3–167.3] | 60.2 [60.1–60.3] | 6.00s [6.00s–6.17s] | 68.4 [67.9–68.4] | 3.57x | 2.44x | 0.88x | 87.8x |
| array1 | array | 0.805 [0.805–0.806] | 0.807 [0.807–0.807] | 0.316 [0.315–0.319] | 17.2 [17.1–17.2] | 1.81 [1.80–1.81] | 0.45x | 0.45x | 0.17x | 9.50x |
| deriv | symbolic | 30.3 [30.3–30.5] | 6.19 [6.16–6.59] | 2.84 [2.82–2.86] | 79.9 [79.3–82.5] | 3.74 [3.70–3.79] | 8.10x | 1.65x | 0.76x | 21.3x |
| diviter | iterative | 265.1 [265.1–335.8] | 251.8 [251.5–254.5] | 253.3 [252.4–256.8] | 612.3 [612.0–612.3] | 468.7 [468.7–469.2] | 0.57x | 0.54x | 0.54x | 1.31x |
| divrec | recursive | 6.09 [6.05–6.12] | 1.21 [1.21–1.29] | 4.88 [4.83–4.90] | 16.0 [15.7–37.3] | 7.54 [7.52–7.63] | 0.81x | 0.16x | 0.65x | 2.12x |
| gcbench | allocation | 199.2 [198.0–199.6] | 74.6 [74.5–103.5] | 70.7 [70.4–70.7] | 504.7 [498.8–506.7] | 22.9 [22.8–23.1] | 8.69x | 3.26x | 3.08x | 22.0x |
| paraffins | combinat | 0.194 [0.192–0.196] | 0.157 [0.154–0.158] | 0.048 [0.047–0.050] | 2.24 [2.24–2.27] | 0.963 [0.960–0.982] | 0.20x | 0.16x | 0.05x | 2.33x |
| pnpoly | numeric | 11.8 [11.7–11.8] | 4.25 [4.22–4.28] | 1.93 [1.92–1.98] | 110.1 [108.7–110.5] | 5.78 [5.75–5.87] | 2.03x | 0.74x | 0.33x | 19.0x |
| puzzle | search | 13.0 [12.9–13.0] | 13.9 [13.7–14.1] | 1.28 [1.27–1.28] | 30.9 [30.9–31.4] | 3.28 [3.28–3.34] | 3.96x | 4.25x | 0.39x | 9.42x |
| quicksort | sorting | 0.865 [0.826–0.897] | 0.667 [0.664–0.688] | 0.197 [0.195–0.197] | 18.9 [18.8–19.2] | 1.66 [1.64–1.66] | 0.52x | 0.40x | 0.12x | 11.4x |
| ray | numeric | 0.241 [0.240–0.247] | 0.251 [0.241–0.252] | 0.169 [0.167–0.174] | 5.35 [5.34–5.40] | 3.51 [3.51–3.54] | 0.07x | 0.07x | 0.05x | 1.53x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 8.54 [8.44–8.58] | 7.18 [7.16–7.23] | 0.532 [0.523–0.541] | 319.8 [319.2–321.6] | 17.8 [17.8–19.0] | 0.48x | 0.40x | 0.03x | 18.0x |
| navier_stokes | numeric | 82.3 [82.2–82.5] | 69.8 [69.7–70.0] | 47.0 [46.9–47.0] | 191.8 [191.2–192.2] | 14.7 [14.2–106.1] | 5.59x | 4.74x | 3.19x | 13.0x |
| splay | data | 323.4 [320.4–324.2] | 307.4 [307.2–308.1] | 18.9 [18.8–19.0] | 335.4 [334.4–336.9] | 19.3 [17.9–19.4] | 16.7x | 15.9x | 0.98x | 17.4x |
| hashmap | data | 79.0 [78.8–79.2] | 27.8 [27.8–28.3] | 2.83 [2.71–2.84] | 1.24s [1.23s–1.24s] | 15.2 [15.2–15.4] | 5.18x | 1.82x | 0.19x | 81.0x |
| crypto_sha1 | crypto | 44.0 [43.9–44.7] | 25.8 [25.7–25.8] | 2.67 [2.63–2.74] | 273.9 [273.8–274.6] | 8.89 [8.67–8.89] | 4.95x | 2.90x | 0.30x | 30.8x |
| raytrace3d | 3d | 67.5 [67.3–67.7] | 12.9 [12.8–21.4] | 2.18 [2.18–2.18] | 539.3 [539.1–541.5] | 18.4 [18.3–18.4] | 3.66x | 0.70x | 0.12x | 29.3x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 244.5 [243.8–244.7] | 54.3 [54.2–57.1] | 13.0 [12.9–13.0] | 2.04s [2.03s–2.13s] | 39.1 [38.8–39.1] | 6.25x | 1.39x | 0.33x | 52.1x |
| microdiff | data-diff | 51.5 [51.3–51.7] | 58.4 [58.3–136.8] | 2.62 [2.62–2.64] | 810.1 [808.7–811.1] | 16.2 [16.0–16.5] | 3.18x | 3.60x | 0.16x | 49.9x |
| hyphen | hyphenation | 67.5 [67.5–67.9] | 12.2 [12.2–12.5] | 1.49 [1.48–1.49] | 95.3 [95.2–96.8] | 6.62 [6.59–6.78] | 10.2x | 1.85x | 0.22x | 14.4x |
| prettier_ast | formatting | 954.9 [946.7–957.7] | 536.8 [535.7–542.7] | 41.5 [41.0–41.7] | 2.50s [2.49s–2.51s] | 98.6 [98.6–99.4] | 9.68x | 5.44x | 0.42x | 25.3x |
| text_search | search | 1.48s [1.48s–1.49s] | 1.76s [1.74s–1.82s] | 535.8 [534.8–542.5] | 35.36s [35.32s–35.38s] | 780.6 [778.6–793.3] | 1.89x | 2.25x | 0.69x | 45.3x |
| three_way_merge | merge | 2.94s [2.93s–2.96s] | 2.20s [2.20s–2.32s] | 2.14s [2.14s–2.15s] | 9.65s [9.65s–9.66s] | 972.5 [967.7–980.9] | 3.02x | 2.27x | 2.20x | 9.92x |
| log_pipeline | log-processing | 4.60s [4.56s–4.65s] | 2.03s [2.02s–2.04s] | 622.8 [622.1–624.0] | 13.20s [13.07s–13.28s] | 942.4 [939.3–954.5] | 4.88x | 2.15x | 0.66x | 14.0x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR and LambdaJS columns use the shipped auto tier -- no `LAMBDA_TIER` or `JS_EXECUTION_BACKEND` override -- which is what `lambda.exe run script.ls` and `lambda.exe js script.js` actually do.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. The MIR and LambdaJS columns are re-run, because part 1 pins the compiled lane and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS (auto) | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS (auto)/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 0.50x | 0.52x | 1.01x | 2.88x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 1.52x | 1.56x | 0.91x | 7.38x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 0.62x | 0.73x | 1.02x | 1.91x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 2.99x | 3.59x | 0.72x | 25.3x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 1.37x | 0.94x | 0.99x | 9.69x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 3.75x | 3.40x | 0.88x | 32.0x |
| Text | 7 | 7 | 7 | 7 | 6 | 7 | 3.35x | 2.57x | 0.92x | 37.1x |
| **Overall** | 63 | 63 | 63 | 63 | 62 | 63 | 1.43x | 1.36x | 0.93x | 8.66x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 18.8 [17.6–19.4] | 18.0 [17.8–18.6] | 47.8 [47.3–574.3] | 302.1 [285.5–374.8] | 45.9 [45.3–47.0] | 0.41x | 0.39x | 1.04x | 6.59x |
| fibfp | recursive | 18.3 [18.3–18.8] | 17.5 [17.3–17.6] | 46.7 [46.6–47.0] | 288.7 [286.1–289.6] | 45.7 [45.4–46.7] | 0.40x | 0.38x | 1.02x | 6.31x |
| tak | recursive | 17.1 [16.9–17.1] | 16.7 [16.3–17.0] | 45.6 [45.3–45.9] | 64.1 [63.9–64.8] | 44.1 [44.0–44.6] | 0.39x | 0.38x | 1.03x | 1.45x |
| cpstak | closure | 17.2 [17.2–17.4] | 16.5 [16.5–16.6] | 45.5 [45.4–46.2] | 104.0 [102.8–104.0] | 46.7 [46.2–46.8] | 0.37x | 0.35x | 0.97x | 2.22x |
| sum | iterative | 19.9 [19.8–20.2] | 18.4 [18.3–18.7] | 46.9 [46.1–47.0] | 33.4 [33.2–34.3] | 46.5 [45.8–46.8] | 0.43x | 0.39x | 1.01x | 0.72x |
| sumfp | iterative | 33.9 [33.6–34.0] | 49.0 [48.6–49.7] | 46.0 [45.0–47.0] | 50.8 [50.4–50.8] | 44.6 [44.6–44.8] | 0.76x | 1.10x | 1.03x | 1.14x |
| nqueens | backtrack | 28.1 [27.5–28.6] | 31.1 [30.8–31.5] | 46.7 [46.6–47.0] | 162.8 [159.7–165.4] | 45.8 [45.6–46.4] | 0.61x | 0.68x | 1.02x | 3.55x |
| fft | numeric | 34.8 [34.6–35.3] | 42.3 [41.7–42.4] | 45.5 [45.4–45.9] | 109.1 [109.0–109.6] | 45.9 [45.4–46.1] | 0.76x | 0.92x | 0.99x | 2.38x |
| mbrot | numeric | 24.3 [24.2–25.0] | 26.4 [26.3–26.4] | 45.6 [45.5–45.8] | 38.2 [37.5–38.3] | 46.7 [46.5–60.1] | 0.52x | 0.56x | 0.98x | 0.82x |
| ack | recursive | 32.7 [32.5–122.1] | 27.1 [27.0–27.1] | 57.0 [56.9–57.0] | 2.96s [2.95s–3.04s] | 57.8 [57.2–57.9] | 0.57x | 0.47x | 0.99x | 51.2x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 17.9 [17.8–17.9] | 20.2 [20.0–20.2] | 45.7 [45.3–54.1] | 26.7 [26.5–60.8] | 44.7 [44.7–46.1] | 0.40x | 0.45x | 1.02x | 0.60x |
| permute | micro | 20.1 [19.7–20.2] | 20.5 [20.4–20.6] | 45.5 [45.4–45.8] | 50.7 [49.8–51.6] | 45.2 [45.1–45.3] | 0.45x | 0.45x | 1.01x | 1.12x |
| queens | micro | 21.9 [21.6–22.2] | 28.6 [28.6–28.9] | 45.4 [45.3–45.5] | 37.9 [37.9–38.6] | 45.2 [45.2–45.3] | 0.48x | 0.63x | 1.00x | 0.84x |
| towers | micro | 23.2 [22.6–23.4] | 30.9 [30.6–30.9] | 45.5 [45.4–45.7] | 67.6 [67.1–68.2] | 45.7 [45.5–45.9] | 0.51x | 0.68x | 1.00x | 1.48x |
| bounce | micro | 21.3 [21.2–21.3] | 25.8 [25.7–26.4] | 46.0 [45.6–46.3] | 36.9 [36.6–37.0] | 45.8 [45.7–46.4] | 0.46x | 0.56x | 1.00x | 0.81x |
| list | micro | 20.1 [19.9–20.1] | 20.0 [19.8–20.4] | 45.4 [45.1–45.5] | 35.3 [35.0–35.4] | 44.8 [44.7–45.5] | 0.45x | 0.45x | 1.01x | 0.79x |
| storage | micro | 19.3 [19.2–19.5] | 19.8 [19.8–20.1] | 45.8 [45.2–46.0] | 42.2 [41.8–42.5] | 45.5 [45.4–46.0] | 0.42x | 0.43x | 1.01x | 0.93x |
| mandelbrot | compute | 4.61s [4.58s–4.67s] | 7.47s [7.43s–7.60s] | 76.4 [75.4–76.9] | 9.67s [9.55s–9.74s] | 76.0 [75.4–76.2] | 60.6x | 98.2x | 1.00x | 127x |
| nbody | compute | 66.3 [65.9–66.7] | 79.2 [79.0–79.4] | 47.9 [47.6–48.4] | 3.76s [3.75s–3.78s] | 53.2 [50.6–62.9] | 1.25x | 1.49x | 0.90x | 70.7x |
| richards | macro | 593.8 [592.0–597.0] | 611.2 [609.4–611.5] | 78.3 [77.4–78.6] | 4.26s [4.23s–4.32s] | 92.0 [91.2–92.5] | 6.46x | 6.64x | 0.85x | 46.3x |
| json | macro | 71.3 [71.1–71.3] | 74.3 [73.8–74.8] | 52.3 [52.1–53.3] | 178.4 [177.8–178.9] | 47.7 [47.6–48.3] | 1.49x | 1.56x | 1.10x | 3.74x |
| deltablue | macro | 347.0 [342.3–436.3] | 252.7 [252.5–268.2] | 54.0 [53.6–54.2] | 1.60s [1.60s–1.60s] | 57.5 [57.4–57.8] | 6.04x | 4.40x | 0.94x | 27.8x |
| havlak | macro | 285.6 [266.0–286.1] | 170.1 [168.5–179.9] | 54.2 [54.0–54.9] | 54.27s [54.17s–54.53s] | 143.3 [142.5–144.3] | 1.99x | 1.19x | 0.38x | 379x |
| cd | macro | 846.5 [837.6–850.8] | 558.6 [551.0–642.7] | 67.9 [67.9–68.7] | 14.44s [14.43s–14.45s] | 81.7 [81.6–81.8] | 10.4x | 6.84x | 0.83x | 177x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 35.1 [35.0–35.1] | 27.9 [27.6–28.1] | 48.6 [48.4–48.8] | 194.6 [194.5–197.5] | 46.0 [45.4–46.2] | 0.76x | 0.61x | 1.06x | 4.23x |
| fannkuch | permutation | 62.5 [62.3–62.7] | 107.5 [96.2–170.9] | 45.5 [45.5–45.7] | 165.6 [165.4–167.3] | 46.4 [45.9–47.1] | 1.34x | 2.31x | 0.98x | 3.57x |
| fasta | generation | 27.2 [27.0–27.4] | 31.7 [31.6–32.0] | 45.9 [45.6–46.1] | 70.8 [70.7–156.3] | 48.7 [48.5–49.3] | 0.56x | 0.65x | 0.94x | 1.45x |
| knucleotide | hashing | 30.9 [30.6–30.9] | 55.1 [54.9–55.5] | 46.7 [46.2–47.2] | 81.5 [81.4–86.0] | 47.1 [46.8–47.2] | 0.66x | 1.17x | 0.99x | 1.73x |
| pidigits | bignum | 15.7 [15.6–15.8] | 19.6 [15.6–20.1] | 47.5 [47.1–47.6] | 21.5 [20.9–21.5] | 43.8 [43.5–44.0] | 0.36x | 0.45x | 1.08x | 0.49x |
| regexredux | regex | 16.0 [15.8–16.4] | 15.8 [15.6–15.9] | 48.2 [48.2–48.4] | 29.8 [29.6–30.7] | 44.4 [43.9–44.7] | 0.36x | 0.36x | 1.09x | 0.67x |
| revcomp | string | 19.8 [19.7–19.9] | 21.7 [21.6–21.7] | 46.4 [45.9–46.5] | 53.5 [53.4–54.1] | 45.5 [45.4–45.9] | 0.43x | 0.48x | 1.02x | 1.17x |
| spectralnorm | numeric | 48.0 [48.0–48.1] | 47.8 [47.7–48.3] | 45.8 [45.8–46.1] | 558.9 [556.1–564.6] | 46.6 [46.5–46.9] | 1.03x | 1.03x | 0.98x | 12.0x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 218.1 [217.7–288.9] | 222.4 [222.3–223.5] | 74.0 [72.8–74.9] | 10.89s [10.87s–10.89s] | 78.2 [77.7–78.3] | 2.79x | 2.84x | 0.95x | 139x |
| matmul | numeric | 2.69s [2.66s–2.78s] | 4.02s [4.02s–4.02s] | 51.5 [51.2–51.8] | 13.13s [13.09s–13.15s] | 59.4 [59.3–59.7] | 45.3x | 67.8x | 0.87x | 221x |
| primes | numeric | 530.9 [530.1–534.6] | 941.2 [941.1–948.6] | 46.9 [46.5–47.6] | 3.41s [3.40s–3.42s] | 48.5 [47.8–48.6] | 10.9x | 19.4x | 0.97x | 70.2x |
| base64 | string | 43.7 [43.6–43.7] | 43.6 [43.3–43.8] | 45.8 [45.5–46.2] | 2.14s [2.14s–2.14s] | 61.9 [61.9–62.1] | 0.71x | 0.70x | 0.74x | 34.6x |
| levenshtein | string | 223.2 [222.4–224.6] | 277.9 [277.0–278.7] | 46.7 [46.4–46.8] | 1.32s [1.31s–1.32s] | 47.7 [47.7–47.9] | 4.68x | 5.82x | 0.98x | 27.6x |
| json_gen | data | 58.9 [58.6–59.5] | 53.0 [52.8–53.1] | 46.9 [46.5–47.1] | 119.1 [118.5–120.0] | 50.1 [50.1–50.6] | 1.18x | 1.06x | 0.94x | 2.38x |
| collatz | numeric | 585.0 [584.7–684.5] | 700.1 [700.0–705.7] | 271.2 [270.4–271.9] | 2.02s [2.02s–2.04s] | 1.47s [1.46s–1.59s] | 0.40x | 0.48x | 0.18x | 1.37x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 269.0 [268.3–269.3] | 194.8 [194.8–195.3] | 105.8 [105.8–106.9] | 51.16s [51.07s–51.28s] | 111.6 [110.7–111.8] | 2.41x | 1.75x | 0.95x | 459x |
| array1 | array | 198.3 [198.1–199.5] | 363.2 [362.1–364.6] | 44.3 [44.1–44.9] | 697.2 [692.6–700.1] | 44.6 [44.5–44.8] | 4.45x | 8.15x | 0.99x | 15.6x |
| deriv | symbolic | 58.1 [57.9–58.2] | 32.3 [32.3–32.7] | 48.2 [47.9–48.4] | 413.5 [412.7–414.2] | 47.2 [47.2–47.3] | 1.23x | 0.69x | 1.02x | 8.76x |
| diviter | iterative | 420.9 [419.2–422.4] | 16.6 [16.5–17.8] | 298.2 [297.3–301.2] | 1.33s [1.32s–1.35s] | 512.8 [512.0–513.1] | 0.82x | 0.03x | 0.58x | 2.59x |
| divrec | recursive | 25.6 [25.1–25.7] | 20.5 [20.4–20.7] | 49.1 [48.9–49.4] | 715.6 [705.5–724.5] | 50.5 [50.4–50.5] | 0.51x | 0.41x | 0.97x | 14.2x |
| gcbench | allocation | 317.5 [317.3–318.4] | 120.3 [119.2–131.1] | 115.9 [115.0–116.4] | 4.38s [4.37s–4.40s] | 65.2 [65.1–65.3] | 4.87x | 1.85x | 1.78x | 67.2x |
| paraffins | combinat | 52.9 [52.6–53.6] | 52.8 [52.7–53.5] | 45.4 [45.2–45.9] | 77.3 [77.1–78.0] | 44.1 [43.7–44.1] | 1.20x | 1.20x | 1.03x | 1.75x |
| pnpoly | numeric | 80.8 [79.5–81.5] | 102.3 [101.9–104.0] | 46.6 [46.5–47.4] | 164.5 [163.8–164.6] | 48.9 [48.8–49.0] | 1.65x | 2.09x | 0.95x | 3.36x |
| puzzle | search | 35.0 [34.9–35.4] | 38.0 [37.1–38.0] | 45.9 [45.5–46.4] | 492.4 [491.0–496.4] | 46.1 [46.0–46.1] | 0.76x | 0.82x | 1.00x | 10.7x |
| quicksort | sorting | 25.6 [25.5–25.7] | 28.4 [28.3–28.5] | 45.5 [44.8–45.9] | 73.3 [73.2–73.9] | 45.3 [45.2–45.5] | 0.56x | 0.63x | 1.00x | 1.62x |
| ray | numeric | 65.0 [64.7–65.7] | 72.4 [72.3–72.6] | 44.9 [44.8–45.0] | 206.0 [205.9–206.6] | 46.2 [46.2–46.3] | 1.41x | 1.57x | 0.97x | 4.46x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 99.0 [98.6–101.0] | 121.4 [121.2–122.7] | 49.9 [49.9–50.4] | 2.23s [2.22s–2.33s] | 66.1 [64.3–77.3] | 1.50x | 1.84x | 0.75x | 33.7x |
| navier_stokes | numeric | 421.1 [419.8–423.3] | 412.9 [411.3–414.5] | 94.9 [94.8–95.5] | 2.05s [2.05s–2.12s] | 71.1 [60.7–156.5] | 5.93x | 5.81x | 1.34x | 28.9x |
| splay | data | 480.4 [479.6–486.4] | 384.0 [379.3–386.2] | 66.5 [66.5–66.7] | 2.10s [2.10s–2.11s] | 92.4 [91.1–92.6] | 5.20x | 4.16x | 0.72x | 22.8x |
| hashmap | data | 405.1 [399.7–592.3] | 381.3 [378.8–383.0] | 48.3 [48.2–48.7] | 4.92s [4.91s–4.93s] | 59.6 [59.4–60.1] | 6.80x | 6.40x | 0.81x | 82.6x |
| crypto_sha1 | crypto | 183.0 [179.1–265.8] | 173.6 [172.6–173.9] | 49.2 [49.1–49.3] | 1.68s [1.67s–1.68s] | 53.0 [52.9–53.0] | 3.45x | 3.28x | 0.93x | 31.6x |
| raytrace3d | 3d | 162.9 [162.9–164.7] | 104.9 [104.9–105.6] | 52.8 [52.7–53.2] | 1.18s [1.18s–1.19s] | 63.2 [63.1–63.5] | 2.58x | 1.66x | 0.84x | 18.7x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (auto) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS (auto)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 276.6 [275.8–278.9] | 90.3 [89.9–90.3] | 58.3 [58.2–58.5] | 7.38s [7.36s–7.43s] | 83.1 [82.6–83.4] | 3.33x | 1.09x | 0.70x | 88.8x |
| microdiff | data-diff | 101.7 [101.5–102.7] | 112.4 [112.3–113.2] | 50.8 [50.6–51.6] | 1.49s [1.49s–1.49s] | 60.2 [59.4–60.4] | 1.69x | 1.87x | 0.84x | 24.7x |
| hyphen | hyphenation | 144.0 [143.1–144.9] | 94.0 [81.4–94.2] | 75.7 [75.0–76.5] | 447.4 [446.9–447.7] | 57.3 [57.2–57.5] | 2.51x | 1.64x | 1.32x | 7.81x |
| prettier_ast | formatting | 1.43s [1.42s–1.44s] | 1.63s [1.60s–1.66s] | 103.0 [102.9–104.4] | 11.70s [11.68s–11.72s] | 143.3 [143.2–143.9] | 9.98x | 11.4x | 0.72x | 81.7x |
| text_search | search | 1.52s [1.51s–1.54s] | 1.79s [1.77s–1.81s] | 583.4 [581.2–588.9] | --- [180.00s–180.00s] | 825.8 [823.9–839.3] | 1.84x | 2.16x | 0.71x | --- |
| three_way_merge | merge | 3.32s [3.31s–3.33s] | 2.94s [2.93s–2.99s] | 2.19s [2.19s–2.20s] | 31.27s [31.26s–31.30s] | 1.02s [1.01s–1.03s] | 3.26x | 2.89x | 2.15x | 30.7x |
| log_pipeline | log-processing | 5.61s [5.57s–5.63s] | 3.09s [3.09s–3.10s] | 672.4 [671.9–673.1] | 60.44s [60.34s–60.77s] | 995.6 [992.5–1.01s] | 5.63x | 3.11x | 0.68x | 60.7x |

