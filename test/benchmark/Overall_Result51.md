# Lambda Benchmark Results: Result51

- **Date:** 2026-10-06
- **Platform:** Darwin arm64
- **Lambda commit:** `3cc2a8a658b74f13dff7fcb7e2bdf56a85240295`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v51-3cc2a8a658` (21,295,224 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 49.70s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 49.6s (batched 48.8s: sync 33.3s, async 15.5s; non-batched 0.8s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **Julia:** julia version 1.13.1; one thread; 1 full warmup run(s) with fresh inputs. Execution excludes startup and warmup; the process column includes startup, compilation and warmup. Node's timer uses the checked-in script's own warmup policy.
- **Java:** openjdk version "26.0.1" 2026-04-21; 1 optional full warmup run(s). Implementation: direct native functions and data structures; no generated language adapter. Execution excludes startup and warmup; process time includes VM startup and warmup; native source compilation precedes both timers.
- **Erlang:** Erlang/OTP 29 [erts-17.1] [source] [64-bit] [smp:1:1] [ds:1:1:10] [async-threads:1] [jit] [dtrace]; 1 optional full warmup run(s). Implementation: direct native functions and data structures; no generated language adapter. Execution excludes startup and warmup; process time includes VM startup and warmup; native source compilation precedes both timers.
- **Julia microbenchmark suite:** every language uses one complete warmup, then one measured workload; formatted-output timing includes synchronous null-sink writes.
- **QuickJS:** 2025-09-13
- **Original measurements:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Added measurements:** Java and Erlang columns were refreshed on 2026-10-07 after rewriting all ports as native code. Each cell is the median of three sequential samples with one full warmup; all 77 registered entries were Node-verified. Historical generated-adapter results are retained in the superseded import metadata and adapter-baseline archive.
- **Added measurements:** Go and C2MIR Julia microbenchmark cells were refreshed on 2026-10-07 from the latest tuned ports: 20 samples per cell, median workload time; C2MIR process times were refreshed from the same launches. Exact samples, prior cells, source/binary hashes and evidence archives are retained with the merge provenance.
- **Range notation:** repeated-run timing cells are `median [minimum–maximum]`; single-run cells show the one measured value; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js, Python, Go, Julia, Java, Erlang
- **Results source:** `test/benchmark/benchmark_results_v51.json`
- **Separately measured:** Go measured on 2026-10-07, 3 run(s) from `temp/benchmark_v51/benchmark_results_v51_go.json` on Lambda commit `3cc2a8a658`. Go was measured in a separate same-commit run using GOCACHE=./temp/go-cache after the default ~/Library/Caches/go-build location returned operation not permitted.
- **Separately measured:** MIR (typed), MIR (typed, auto) measured on 2026-10-07, 3 run(s) from `temp/benchmark_v51/cd_refresh.json` on Lambda commit `3cc2a8a658`. Only awfy/cd MIR-T and its auto-tier cell were refreshed after fixing cd2.ls to propagate fill() errors (S7.6.3v2). The exact original release binary is unchanged; follow-up sources and hashes are retained in source_metadata.
- **Separately measured:** Julia, Julia (startup + warmup) measured on 2026-10-07, 3 run(s) from `temp/benchmark_v51/julia_jq_refresh.json` on Lambda commit `3cc2a8a658`. Only the four jq_* text rows were added to Julia using a native Julia jq VM over the shared filters and full workloads. Each process uses one complete fresh-state warmup before its timed evaluation. Source/runtime/fixture hashes and the source archive are retained in source_metadata.
- **Historical import (superseded):** Java, Java (startup + warmup), Erlang, Erlang (startup + warmup) measured, 1 run(s) from `test/benchmark/benchmark_results_v51_java_erlang.json`. Historical generated-adapter measurements, superseded by the native rewrite. Latest saved full-size measurements imported on 2026-10-07: one sample per row, with optional extra warmup disabled except the microbenchmark kernels. Includes recent full-size follow-ups and older saved runs from multiple adapter revisions; concurrent correctness runs were not a matched performance snapshot. Reduced jq diagnostic fixtures are excluded. Erlang text/jq_tree is failed/missing after user cancellation, with no timing recorded. Exact per-row sources, compiled artifacts and hashes are retained in source_metadata and its evidence archive.
- **Separately measured:** Java, Java (startup + warmup), Erlang, Erlang (startup + warmup) measured, 3 run(s) from `test/benchmark/benchmark_results_v51_java_erlang_native.json` on Lambda commit `f6e11aa334`. Native rewrite measured on 2026-10-07: three sequential fresh-process samples per canonical workload, one complete warmup, median workload and process times. All 77 registered entries pass complete-output comparison with Node. Erlang jq_tree now passes at full size. Previous adapter results remain archived as historical evidence.
- **Separately measured:** Go, C2MIR measured, 20 run(s) from `test/benchmark/benchmark_results_v51_go_c2mir_tuned.json` on Lambda commit `fb464641d5`. Only the four Julia microbenchmarks were refreshed on 2026-10-07 after tuning the native Go and C ports. Each workload cell is the median of 20 tuned samples from alternating original/tuned measurements, with one full warmup per fresh process. C2MIR process cells use the same launches. Previous cells, complete raw samples, exact sources and binaries are retained in source_metadata and its evidence archive.
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with process startup and prior build steps excluded. Runtime compilation performed during the workload is included. This is the historical series, comparable back through Result18, the MIR columns pin `LAMBDA_EXEC_BACKEND=jit`, and LambdaJS pins `JS_EXEC_BACKEND=mir`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | Timed Python | Timed Go | Timed Julia | Timed Java | Timed Erlang | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo | Python/Node geo | Go/Node geo | Julia/Node geo | Java/Node geo | Erlang/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.38x | 0.29x | 0.21x | 0.97x | 6.59x | 6.24x | 0.34x | 0.07x | 0.28x | 1.12x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 0.96x | 0.40x | 0.09x | 11.7x | 5.22x | 4.04x | 0.22x | 0.25x | 0.30x | 1.18x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.28x | 0.19x | 0.10x | 4.00x | 1.77x | 0.57x | 0.25x | 0.17x | 0.40x | 0.24x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 0.89x | 0.60x | 0.23x | 11.1x | 12.0x | 9.50x | 0.39x | 0.52x | 0.42x | 2.28x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 0.87x | 0.64x | 0.33x | 7.23x | 13.3x | 10.6x | 0.50x | 0.08x | 0.46x | 1.81x |
| Julia microbenchmarks | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 2.40x | 1.75x | 0.33x | 52.3x | 25.5x | 22.7x | 0.33x | 1.02x | 0.50x | 4.34x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 4.18x | 2.24x | 0.29x | 26.3x | 12.0x | 9.25x | 0.22x | 0.75x | 0.49x | 1.49x |
| Text | 11 | 11 | 11 | 11 | 8 | 10 | 11 | 11 | 11 | 11 | 11 | 11 | 1.62x | 1.06x | 0.47x | 29.3x | 10.9x | 5.96x | 0.52x | 4.43x | 0.69x | 3.04x |
| **Overall** | 71 | 71 | 71 | 71 | 68 | 70 | 71 | 71 | 71 | 71 | 71 | 71 | 0.93x | 0.58x | 0.21x | 8.60x | 7.84x | 5.46x | 0.33x | 0.34x | 0.41x | 1.42x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 67 | 0.54x | 0.85x | 2.72x | 67 |
| complete current suite | 71 | 0.58x | 0.93x | 2.69x | 71 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 2.69x over 71 of 71 rows
- **MIR (typed) / Go geomean:** 1.73x over 71 of 71 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | Go | MIR (typed)/C2MIR | MIR (typed)/Go |
|---|---:|---:|---:|---:|---:|
| text/jq_mix | 44.74s | 1.00s | 994.8 | 44.6x | 45.0x |
| julia/parse_integers | 48.1 | 2.27 | 2.31 | 21.2x | 20.9x |
| awfy/havlak | 28.0 | 1.81 | 6.05 | 15.5x | 4.63x |
| jetstream/splay | 287.8 | 18.7 | 27.7 | 15.4x | 10.4x |
| text/microdiff | 38.7 | 2.62 | 11.5 | 14.8x | 3.38x |
| jetstream/cube3d | 6.89 | 0.514 | 1.86 | 13.4x | 3.70x |
| awfy/deltablue | 15.7 | 1.17 | 3.55 | 13.4x | 4.43x |
| awfy/cd | 177.7 | 15.0 | 12.5 | 11.9x | 14.2x |
| larceny/puzzle | 14.6 | 1.27 | 2.10 | 11.4x | 6.95x |
| jetstream/crypto_sha1 | 28.4 | 2.67 | 0.393 | 10.6x | 72.2x |
| jetstream/hashmap | 27.6 | 2.72 | 4.71 | 10.2x | 5.86x |
| julia/formatted_output | 83.1 | 8.24 | 7.97 | 10.1x | 10.4x |

---

### Notable Results

- Missing timings: **4** cells
- LambdaJS missing: text/jq_mix (timeout), text/jq_bf (timeout), text/jq_tree (exit_1)
- QuickJS missing: text/jq_mix (exit_1)

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| text/jq_records | 321.07s | 1.99s | 161x |
| awfy/havlak | 13.25s | 94.9 | 140x |
| awfy/nbody | 619.4 | 5.30 | 117x |
| awfy/cd | 4.02s | 35.8 | 112x |
| julia/matrix_statistics | 2.00s | 21.3 | 93.9x |
| larceny/triangl | 5.49s | 68.9 | 79.7x |
| jetstream/hashmap | 1.19s | 15.2 | 78.3x |
| julia/parse_integers | 942.5 | 13.1 | 72.0x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| r7rs/sumfp | 0.067 | 0.865 | 0.08x |
| beng/pidigits | 0.498 | 1.75 | 0.28x |
| awfy/sieve | 0.168 | 0.381 | 0.44x |
| r7rs/tak | 0.355 | 0.771 | 0.46x |
| r7rs/sum | 0.668 | 1.18 | 0.57x |
| r7rs/cpstak | 0.707 | 0.961 | 0.74x |
| r7rs/ack | 12.8 | 13.3 | 0.97x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Python (ms) | Go (ms) | Julia (ms) | Java (ms) | Erlang (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Python/Node | Go/Node | Julia/Node | Java/Node | Erlang/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.56 [1.53–1.58] | 1.56 [1.55–1.58] | 1.02 [1.01–1.04] | 1.92 [1.91–1.93] | 18.5 [18.5–18.6] | 1.74 [1.73–1.77] | 19.7 [19.7–20.1] | 0.856 [0.770–1.03] | 0.656 [0.646–0.699] | 0.473 [0.461–0.488] | 3.21 [2.87–3.96] | 0.90x | 0.90x | 0.58x | 1.10x | 10.7x | 11.3x | 0.49x | 0.38x | 0.27x | 1.85x |
| fibfp | recursive | 2.31 [2.30–2.33] | 1.27 [1.23–1.39] | 1.11 [1.11–1.14] | 1.96 [1.95–1.97] | 18.7 [18.5–19.1] | 1.77 [1.75–1.83] | 23.5 [23.5–24.0] | 1.36 [1.03–1.76] | 1.42 [1.41–1.44] | 0.462 [0.444–0.497] | 5.83 [5.79–7.57] | 1.31x | 0.72x | 0.63x | 1.11x | 10.6x | 13.3x | 0.77x | 0.80x | 0.26x | 3.30x |
| tak | recursive | 0.140 [0.134–0.142] | 0.134 [0.133–0.182] | 0.112 [0.111–0.112] | 0.355 [0.355–0.379] | 2.76 [2.75–2.77] | 0.771 [0.766–0.771] | 2.04 [2.03–2.06] | 0.178 [0.118–0.420] | 0.063 [0.063–0.063] | 0.125 [0.124–0.139] | 0.271 [0.255–0.291] | 0.18x | 0.17x | 0.15x | 0.46x | 3.58x | 2.65x | 0.23x | 0.08x | 0.16x | 0.35x |
| cpstak | closure | 0.268 [0.267–0.278] | 0.268 [0.266–0.279] | 0.231 [0.216–0.240] | 0.707 [0.705–0.711] | 5.55 [5.53–5.59] | 0.961 [0.960–0.969] | 4.14 [4.10–4.22] | 0.287 [0.263–0.554] | 0.125 [0.125–0.126] | 0.262 [0.248–0.263] | 0.508 [0.500–0.515] | 0.28x | 0.28x | 0.24x | 0.74x | 5.77x | 4.31x | 0.30x | 0.13x | 0.27x | 0.53x |
| sum | iterative | 0.265 [0.265–0.268] | 0.268 [0.266–0.274] | 0.270 [0.268–0.272] | 0.668 [0.668–0.675] | 30.9 [30.9–30.9] | 1.18 [1.17–1.18] | 30.9 [30.7–31.4] | 0.673 [0.495–0.801] | 0.000 [0.000–0.000] | 0.556 [0.552–0.559] | 2.73 [2.69–2.85] | 0.23x | 0.23x | 0.23x | 0.57x | 26.3x | 26.3x | 0.57x | 0.000x | 0.47x | 2.32x |
| sumfp | iterative | 0.069 [0.068–0.069] | 0.068 [0.068–0.068] | 0.081 [0.076–0.082] | 0.067 [0.067–0.067] | 3.66 [3.63–3.67] | 0.865 [0.861–0.888] | 3.27 [3.23–3.34] | 0.361 [0.261–0.611] | 0.074 [0.074–0.074] | 0.198 [0.145–0.270] | 1.86 [1.84–1.97] | 0.08x | 0.08x | 0.09x | 0.08x | 4.23x | 3.79x | 0.42x | 0.09x | 0.23x | 2.15x |
| nqueens | backtrack | 1.07 [1.05–1.09] | 1.02 [1.01–1.03] | 0.367 [0.364–0.367] | 28.4 [28.1–28.9] | 7.88 [7.86–8.11] | 1.73 [1.70–1.75] | 3.38 [3.37–3.42] | 0.288 [0.211–0.515] | 0.211 [0.209–0.213] | 0.217 [0.213–0.241] | 0.183 [0.180–0.193] | 0.62x | 0.59x | 0.21x | 16.5x | 4.57x | 1.96x | 0.17x | 0.12x | 0.13x | 0.11x |
| fft | numeric | 0.328 [0.319–0.343] | 0.085 [0.085–0.085] | 0.026 [0.025–0.028] | 3.37 [3.33–3.41] | 2.74 [2.74–2.78] | 1.56 [1.55–1.74] | 4.53 [4.48–4.54] | 0.081 [0.069–0.260] | 0.023 [0.021–0.023] | 0.957 [0.874–1.03] | 4.51 [4.44–5.30] | 0.21x | 0.05x | 0.02x | 2.16x | 1.75x | 2.91x | 0.05x | 0.01x | 0.61x | 2.89x |
| mbrot | numeric | 0.717 [0.714–0.731] | 0.572 [0.567–0.612] | 0.443 [0.442–0.521] | 2.15 [2.15–2.17] | 17.7 [17.6–17.9] | 1.80 [1.75–1.91] | 17.2 [17.1–17.4] | 0.887 [0.651–1.08] | 0.408 [0.407–0.408] | 0.557 [0.551–0.568] | 2.98 [2.92–3.07] | 0.40x | 0.32x | 0.25x | 1.20x | 9.81x | 9.58x | 0.49x | 0.23x | 0.31x | 1.65x |
| ack | recursive | 14.6 [13.5–14.7] | 9.86 [9.84–9.89] | 11.5 [10.4–11.7] | 12.8 [12.8–16.8] | 100.5 [100.5–101.8] | 13.3 [13.2–13.3] | 127.7 [127.6–129.6] | 11.9 [9.66–15.2] | 8.43 [8.40–8.44] | 4.40 [4.16–9.66] | 14.3 [13.6–15.7] | 1.10x | 0.74x | 0.86x | 0.97x | 7.58x | 9.63x | 0.90x | 0.64x | 0.33x | 1.08x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Python (ms) | Go (ms) | Julia (ms) | Java (ms) | Erlang (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Python/Node | Go/Node | Julia/Node | Java/Node | Erlang/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.028 [0.027–0.028] | 0.025 [0.024–0.025] | 0.016 [0.015–0.016] | 0.168 [0.168–0.168] | 0.604 [0.603–0.621] | 0.381 [0.380–0.381] | 0.501 | 0.069 [0.047–0.293] | 0.010 [0.010–0.011] | 0.149 [0.145–0.149] | 1.21 [1.17–1.21] | 0.07x | 0.07x | 0.04x | 0.44x | 1.59x | 1.32x | 0.18x | 0.03x | 0.39x | 3.17x |
| permute | micro | 0.261 [0.260–0.269] | 0.069 [0.068–0.070] | 0.029 [0.026–0.029] | 5.83 [5.79–5.86] | 1.54 [1.53–1.55] | 0.810 [0.797–0.831] | 1.35 | 0.081 [0.036–0.340] | 0.015 [0.014–0.015] | 0.048 [0.047–0.051] | 0.349 [0.349–0.369] | 0.32x | 0.09x | 0.04x | 7.20x | 1.90x | 1.67x | 0.10x | 0.02x | 0.06x | 0.43x |
| queens | micro | 0.179 [0.178–0.182] | 0.058 [0.057–0.061] | 0.019 [0.018–0.019] | 3.27 [3.26–3.33] | 1.05 [1.04–1.06] | 0.637 [0.630–0.641] | 0.753 | 0.074 [0.038–0.296] | 0.011 [0.011–0.011] | 0.054 [0.052–0.064] | 0.067 [0.062–0.107] | 0.28x | 0.09x | 0.03x | 5.13x | 1.64x | 1.18x | 0.12x | 0.02x | 0.09x | 0.11x |
| towers | micro | 0.470 [0.469–0.471] | 0.239 [0.233–0.244] | 0.027 [0.026–0.028] | 9.35 [9.34–9.37] | 2.24 [2.23–2.29] | 1.12 [1.12–1.20] | 1.85 | 0.063 [0.044–0.315] | 0.032 [0.031–0.033] | 0.099 [0.095–0.102] | 0.287 [0.278–0.293] | 0.42x | 0.21x | 0.02x | 8.37x | 2.01x | 1.65x | 0.06x | 0.03x | 0.09x | 0.26x |
| bounce | micro | 0.068 [0.068–0.069] | 0.059 [0.058–0.061] | 0.025 [0.025–0.028] | 3.98 [3.90–4.00] | 0.867 [0.864–0.915] | 0.535 [0.529–0.536] | 0.845 | 0.072 [0.054–0.411] | 0.010 [0.010–0.011] | 0.121 [0.120–0.124] | 0.207 [0.197–0.220] | 0.13x | 0.11x | 0.05x | 7.45x | 1.62x | 1.58x | 0.13x | 0.02x | 0.23x | 0.39x |
| list | micro | 0.553 [0.541–0.561] | 0.114 [0.113–0.125] | 0.021 [0.020–0.024] | 2.17 [2.16–2.18] | 0.912 [0.901–0.914] | 0.483 [0.478–0.488] | 0.615 | 0.072 [0.056–0.319] | 0.017 [0.017–0.017] | 0.050 [0.048–0.055] | 0.116 [0.065–0.129] | 1.15x | 0.24x | 0.04x | 4.49x | 1.89x | 1.27x | 0.15x | 0.04x | 0.10x | 0.24x |
| storage | micro | 0.572 [0.559–0.573] | 0.367 [0.363–0.370] | 0.095 [0.089–0.101] | 4.08 [4.08–4.09] | 2.10 [2.09–2.13] | 0.626 [0.623–0.637] | 1.33 | 0.312 [0.192–0.708] | 0.093 [0.091–0.099] | 1.20 [1.19–1.21] | 0.358 [0.355–0.391] | 0.91x | 0.59x | 0.15x | 6.52x | 3.36x | 2.13x | 0.50x | 0.15x | 1.92x | 0.57x |
| mandelbrot | compute | 37.6 [37.6–57.5] | 37.6 [37.6–37.7] | 30.7 [30.6–30.7] | 47.5 [47.2–47.7] | 869.3 [869.0–881.4] | 31.0 [30.9–31.0] | 867.2 | 34.3 [30.8–57.0] | 28.5 [28.1–29.1] | 29.4 [29.3–30.1] | 228.9 [219.6–234.5] | 1.22x | 1.21x | 0.99x | 1.53x | 28.1x | 28.0x | 1.11x | 0.92x | 0.95x | 7.39x |
| nbody | compute | 7.37 [7.26–7.42] | 3.15 [3.15–3.81] | 1.49 [1.47–1.49] | 619.4 [616.5–625.6] | 158.4 [158.4–158.9] | 5.30 [5.24–5.39] | 155.9 | 2.64 [2.10–4.84] | 2.33 [2.32–2.33] | 1.80 [1.75–1.86] | 58.6 [57.9–61.3] | 1.39x | 0.59x | 0.28x | 117x | 29.9x | 29.4x | 0.50x | 0.44x | 0.34x | 11.1x |
| richards | macro | 311.1 [308.8–312.2] | 76.5 [75.2–76.6] | 29.5 [29.3–30.0] | 906.6 [895.5–912.8] | 189.4 [188.7–189.9] | 46.3 [46.3–46.5] | 157.8 | 29.7 [25.1–45.9] | 100.2 [98.1–101.8] | 22.8 [22.4–25.0] | 370.2 [367.9–409.0] | 6.71x | 1.65x | 0.64x | 19.6x | 4.09x | 3.41x | 0.64x | 2.16x | 0.49x | 7.99x |
| json | macro | 5.10 [5.07–5.18] | 1.99 [1.97–2.02] | 0.255 [0.253–0.259] | 43.1 [42.9–43.5] | 10.8 [10.8–10.9] | 2.62 [2.61–2.64] | 6.99 | 0.784 [0.570–1.18] | 1.24 [1.23–1.24] | 1.36 [1.26–1.56] | 0.189 [0.180–0.194] | 1.94x | 0.76x | 0.10x | 16.4x | 4.13x | 2.66x | 0.30x | 0.47x | 0.52x | 0.07x |
| deltablue | macro | 76.7 [76.6–96.1] | 15.7 [15.5–20.7] | 1.17 [1.15–1.21] | 383.9 [383.2–387.8] | 99.1 [98.8–99.3] | 11.7 [11.7–11.7] | 67.0 | 3.55 [2.72–4.02] | 49.6 [47.8–49.8] | 6.65 [6.38–6.72] | 117.6 [113.8–124.3] | 6.56x | 1.34x | 0.10x | 32.8x | 8.47x | 5.73x | 0.30x | 4.24x | 0.57x | 10.1x |
| havlak | macro | 64.3 [63.9–64.5] | 28.0 [27.8–28.1] | 1.81 [1.78–1.82] | 13.25s [13.20s–13.33s] | 3.25s [3.23s–3.35s] | 94.9 [93.0–95.7] | 2.02s | 6.05 [4.63–9.21] | 869.7 [844.1–907.4] | 35.7 [31.3–43.2] | 611.9 [580.8–632.4] | 0.68x | 0.30x | 0.02x | 140x | 34.2x | 21.3x | 0.06x | 9.17x | 0.38x | 6.45x |
| cd | macro | 587.4 [586.4–600.5] | 177.7 [176.4–177.9] | 15.0 [15.0–15.0] | 4.02s [4.01s–4.04s] | 958.7 [954.7–989.6] | 35.8 [35.6–35.8] | 652.6 | 12.5 [10.2–19.7] | 382.4 [381.2–395.3] | 11.2 [11.1–14.2] | 58.1 [57.9–60.1] | 16.4x | 4.96x | 0.42x | 112x | 26.8x | 18.2x | 0.35x | 10.7x | 0.31x | 1.62x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Python (ms) | Go (ms) | Julia (ms) | Java (ms) | Erlang (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Python/Node | Go/Node | Julia/Node | Java/Node | Erlang/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 4.93 [4.90–5.01] | 2.52 [2.48–2.54] | 3.00 [2.97–3.01] | 21.7 [21.5–21.9] | 23.6 [23.4–23.7] | 3.86 [3.80–3.96] | 10.3 [10.2–10.3] | 3.37 [2.63–4.91] | 1.03 [1.01–1.04] | 1.09 [1.05–1.19] | 1.36 [1.36–1.42] | 1.28x | 0.65x | 0.78x | 5.61x | 6.12x | 2.66x | 0.87x | 0.27x | 0.28x | 0.35x |
| fannkuch | permutation | 0.277 [0.275–0.287] | 0.300 [0.292–0.360] | 0.149 [0.149–0.150] | 14.0 [14.0–14.0] | 7.15 [7.11–7.24] | 3.88 [3.83–4.06] | 4.78 [4.72–4.83] | 0.406 [0.293–0.584] | 0.174 [0.173–0.182] | 0.304 [0.297–0.317] | 1.97 [1.76–2.11] | 0.07x | 0.08x | 0.04x | 3.60x | 1.84x | 1.23x | 0.10x | 0.04x | 0.08x | 0.51x |
| fasta | generation | 0.774 [0.743–0.833] | 0.732 [0.705–0.741] | 0.243 [0.241–0.256] | 25.9 [25.1–25.9] | 8.81 [8.74–8.89] | 6.05 [6.02–6.11] | 1.85 [1.82–2.03] | 0.450 [0.317–0.813] | 0.409 [0.407–0.440] | 0.996 [0.948–1.03] | 0.453 [0.423–0.510] | 0.13x | 0.12x | 0.04x | 4.28x | 1.46x | 0.31x | 0.07x | 0.07x | 0.16x | 0.07x |
| knucleotide | hashing | 1.43 [1.42–1.51] | 0.425 [0.422–0.427] | 0.281 [0.277–0.288] | 20.1 [20.1–38.2] | 7.72 [7.68–7.73] | 4.68 [4.65–4.76] | 3.52 [3.50–3.54] | 0.743 [0.507–1.11] | 1.20 [1.18–1.25] | 2.91 [2.78–3.02] | 1.90 [1.89–2.38] | 0.31x | 0.09x | 0.06x | 4.30x | 1.65x | 0.75x | 0.16x | 0.26x | 0.62x | 0.41x |
| pidigits | bignum | 0.360 [0.354–0.377] | 0.345 [0.345–0.383] | 0.045 [0.045–0.045] | 0.498 [0.490–0.502] | 0.128 [0.124–0.132] | 1.75 [1.75–2.58] | 0.096 [0.093–0.097] | 0.199 [0.089–0.647] | 0.101 [0.094–0.102] | 0.626 [0.606–0.659] | 0.021 [0.021–0.022] | 0.21x | 0.20x | 0.03x | 0.28x | 0.07x | 0.05x | 0.11x | 0.06x | 0.36x | 0.01x |
| regexredux | regex | 0.819 [0.811–0.844] | 0.850 [0.828–1.34] | 1.14 [1.13–1.23] | 8.30 [8.28–8.35] | 5.59 [5.55–5.63] | 2.25 [2.24–2.27] | 1.50 [1.50–1.52] | 4.45 [3.51–6.00] | 0.514 [0.496–0.522] | 10.1 [9.90–12.1] | 2.51 [2.51–2.58] | 0.36x | 0.38x | 0.50x | 3.68x | 2.48x | 0.67x | 1.97x | 0.23x | 4.48x | 1.12x |
| revcomp | string | 0.673 [0.649–0.701] | 0.502 [0.467–0.550] | 0.375 [0.374–0.380] | 20.3 [20.3–20.4] | 2.56 [2.54–2.57] | 3.25 [3.22–3.26] | 0.082 [0.079–0.083] | 0.596 [0.442–1.01] | 9.46 [9.36–9.59] | 0.815 [0.806–1.01] | 0.335 [0.331–0.443] | 0.21x | 0.15x | 0.12x | 6.24x | 0.79x | 0.03x | 0.18x | 2.91x | 0.25x | 0.10x |
| spectralnorm | numeric | 1.69 [1.67–1.80] | 0.793 [0.785–0.814] | 0.354 [0.354–0.357] | 71.0 [70.6–72.9] | 64.2 [63.6–64.4] | 2.63 [2.53–2.69] | 43.1 [42.9–43.2] | 0.831 [0.660–1.18] | 0.229 [0.228–0.237] | 1.94 [1.81–2.29] | 4.06 [4.06–4.16] | 0.64x | 0.30x | 0.13x | 27.0x | 24.4x | 16.4x | 0.32x | 0.09x | 0.74x | 1.54x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Python (ms) | Go (ms) | Julia (ms) | Java (ms) | Erlang (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Python/Node | Go/Node | Julia/Node | Java/Node | Erlang/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 123.5 [122.6–141.8] | 112.7 [112.6–113.4] | 28.1 [27.9–28.2] | 1.78s [1.78s–1.79s] | 853.3 [850.8–875.6] | 33.7 [33.6–34.0] | 688.7 [684.7–707.4] | 21.6 [17.9–34.2] | 428.8 [427.7–451.7] | 28.3 [28.0–30.3] | 244.9 [238.9–251.0] | 3.67x | 3.35x | 0.84x | 53.0x | 25.4x | 20.5x | 0.64x | 12.7x | 0.84x | 7.28x |
| matmul | numeric | 5.13 [5.11–5.17] | 5.17 [5.14–5.21] | 6.10 [6.07–8.64] | 192.2 [188.3–207.5] | 538.7 [538.3–540.9] | 15.4 [15.3–16.2] | 506.4 [506.1–509.5] | 19.0 [16.0–33.6] | 4.88 [4.87–4.89] | 8.01 [7.21–8.11] | 115.7 [111.3–116.9] | 0.33x | 0.34x | 0.40x | 12.5x | 35.0x | 32.9x | 1.24x | 0.32x | 0.52x | 7.52x |
| primes | numeric | 2.24 [2.20–2.29] | 2.18 [2.18–2.18] | 1.59 [1.58–1.59] | 51.7 [51.6–51.9] | 99.1 [95.0–159.1] | 4.49 [4.42–4.50] | 90.4 [90.1–100.5] | 3.36 [2.43–3.63] | 0.922 [0.917–0.934] | 2.14 [2.10–2.67] | 291.4 [282.0–299.9] | 0.50x | 0.48x | 0.35x | 11.5x | 22.1x | 20.1x | 0.75x | 0.21x | 0.48x | 64.9x |
| base64 | string | 12.0 [12.0–12.5] | 5.35 [5.34–5.42] | 0.567 [0.558–0.581] | 471.7 [470.6–473.2] | 158.5 [157.4–158.6] | 17.2 [17.2–17.9] | 77.7 [76.9–77.8] | 1.27 [1.05–1.82] | 29.4 [28.8–29.9] | 5.72 [5.63–6.38] | 12.6 [12.2–13.1] | 0.70x | 0.31x | 0.03x | 27.4x | 9.22x | 4.52x | 0.07x | 1.71x | 0.33x | 0.73x |
| levenshtein | string | 5.11 [5.09–5.18] | 2.89 [2.85–2.91] | 0.898 [0.897–0.911] | 76.9 [76.1–77.4] | 54.1 [54.0–54.2] | 3.94 [3.94–3.95] | 66.5 [65.5–66.8] | 2.46 [2.14–2.89] | 1.08 [1.08–1.12] | 2.88 [2.81–4.40] | 3.14 [3.03–3.66] | 1.30x | 0.73x | 0.23x | 19.5x | 13.7x | 16.9x | 0.62x | 0.27x | 0.73x | 0.80x |
| json_gen | data | 23.2 [23.2–23.4] | 7.18 [7.10–7.24] | 1.53 [1.53–1.54] | 27.7 [27.3–27.8] | 19.9 [19.7–20.0] | 6.19 [6.08–6.26] | 7.96 [7.86–8.15] | 3.07 [2.44–3.98] | 2.02 [2.02–2.03] | 2.38 [2.24–2.58] | 2.79 [2.76–2.91] | 3.75x | 1.16x | 0.25x | 4.47x | 3.22x | 1.29x | 0.50x | 0.33x | 0.39x | 0.45x |
| collatz | numeric | 297.5 [296.6–300.3] | 286.2 [283.0–286.9] | 225.1 [224.9–225.3] | 1.60s [1.59s–1.61s] | 6.23s [6.22s–6.26s] | 1.42s [1.42s–1.42s] | 7.42s [7.40s–7.49s] | 138.9 [138.8–167.0] | 108.6 [108.5–108.8] | 165.6 [165.3–170.1] | 488.5 [485.9–518.2] | 0.21x | 0.20x | 0.16x | 1.13x | 4.40x | 5.24x | 0.10x | 0.08x | 0.12x | 0.34x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Python (ms) | Go (ms) | Julia (ms) | Java (ms) | Erlang (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Python/Node | Go/Node | Julia/Node | Java/Node | Erlang/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 182.4 [181.9–183.5] | 166.0 [165.8–166.3] | 60.5 [60.3–60.5] | 5.49s [5.48s–5.52s] | 2.18s [2.18s–2.22s] | 68.9 [68.7–69.3] | 2.70s [2.70s–2.70s] | 47.3 [46.5–74.9] | 39.9 [39.9–40.2] | 48.9 [48.5–53.4] | 182.9 [181.9–184.3] | 2.65x | 2.41x | 0.88x | 79.7x | 31.6x | 39.1x | 0.69x | 0.58x | 0.71x | 2.65x |
| array1 | array | 0.804 [0.804–0.822] | 0.808 [0.804–0.808] | 0.315 [0.315–0.316] | 13.0 [12.8–13.4] | 35.8 [35.7–35.9] | 1.81 [1.80–1.81] | 30.8 [30.7–32.0] | 0.688 [0.397–0.985] | 0.035 [0.035–0.036] | 2.05 [1.63–2.27] | 2.54 [2.47–2.55] | 0.44x | 0.45x | 0.17x | 7.19x | 19.8x | 17.0x | 0.38x | 0.02x | 1.13x | 1.40x |
| deriv | symbolic | 21.4 [21.1–21.8] | 6.74 [6.74–6.99] | 2.82 [2.81–2.84] | 88.1 [87.1–89.0] | 59.3 [59.2–59.8] | 3.86 [3.69–3.87] | 24.2 [24.2–24.6] | 5.62 [4.39–7.98] | 5.24 [5.23–5.77] | 1.57 [1.49–1.65] | 2.49 [2.47–2.55] | 5.56x | 1.75x | 0.73x | 22.8x | 15.4x | 6.28x | 1.46x | 1.36x | 0.41x | 0.64x |
| diviter | iterative | 267.6 [264.1–267.6] | 263.6 [249.2–271.2] | 254.1 [249.1–255.2] | 613.3 [612.9–614.7] | 26.61s [26.56s–26.74s] | 463.8 [461.4–464.4] | 23.24s [23.24s–23.36s] | 247.5 [247.4–272.3] | 0.006 [0.006–0.006] | 405.5 [276.4–411.9] | 2.32s [2.27s–3.83s] | 0.58x | 0.57x | 0.55x | 1.32x | 57.4x | 50.1x | 0.53x | 0.000x | 0.87x | 4.99x |
| divrec | recursive | 1.21 [1.20–1.22] | 1.21 [1.20–1.21] | 4.85 [4.85–4.85] | 16.1 [15.7–16.2] | 36.0 [35.4–37.3] | 7.59 [7.53–7.73] | 40.9 [40.5–41.7] | 7.70 [6.28–11.2] | 4.77 [4.75–4.79] | 2.49 [2.46–2.50] | 5.30 [5.20–7.44] | 0.16x | 0.16x | 0.64x | 2.13x | 4.75x | 5.39x | 1.01x | 0.63x | 0.33x | 0.70x |
| gcbench | allocation | 116.8 [116.6–117.0] | 84.4 [83.5–84.8] | 71.1 [70.4–71.1] | 541.4 [540.3–555.9] | 556.6 [551.9–565.4] | 23.1 [23.0–23.2] | 251.6 [250.7–252.4] | 39.4 [38.5–63.7] | 21.8 [21.7–21.8] | 13.9 [13.7–14.2] | 33.0 [32.1–40.7] | 5.05x | 3.65x | 3.07x | 23.4x | 24.1x | 10.9x | 1.70x | 0.94x | 0.60x | 1.43x |
| paraffins | combinat | 0.200 [0.198–0.202] | 0.160 [0.153–0.168] | 0.050 [0.048–0.058] | 1.96 [1.93–1.96] | 2.53 [2.50–2.58] | 0.974 [0.970–0.981] | 2.56 [2.55–2.72] | 0.134 [0.097–0.387] | 0.035 [0.034–0.035] | 0.163 [0.161–0.165] | 1.65 [1.50–1.66] | 0.21x | 0.16x | 0.05x | 2.01x | 2.60x | 2.63x | 0.14x | 0.04x | 0.17x | 1.70x |
| pnpoly | numeric | 12.4 [12.3–12.4] | 4.23 [4.21–4.30] | 1.95 [1.95–2.00] | 76.8 [76.1–93.7] | 202.4 [202.4–202.7] | 5.80 [5.79–5.84] | 111.2 [111.0–116.1] | 4.29 [3.36–4.41] | 1.21 [1.19–1.21] | 2.66 [2.60–2.80] | 23.6 [23.5–68.6] | 2.13x | 0.73x | 0.34x | 13.2x | 34.9x | 19.2x | 0.74x | 0.21x | 0.46x | 4.06x |
| puzzle | search | 14.0 [13.9–14.1] | 14.6 [14.5–14.7] | 1.27 [1.27–1.28] | 29.0 [28.7–29.2] | 29.2 [29.2–29.3] | 3.33 [3.29–3.33] | 19.1 [19.1–19.1] | 2.10 [1.89–2.41] | 1.03 [1.01–1.06] | 1.60 [1.48–1.64] | 2.77 [2.75–3.06] | 4.20x | 4.38x | 0.38x | 8.71x | 8.79x | 5.74x | 0.63x | 0.31x | 0.48x | 0.83x |
| quicksort | sorting | 1.02 [1.00–1.07] | 0.630 [0.624–0.631] | 0.199 [0.198–0.201] | 16.2 [16.2–16.4] | 19.1 [19.1–19.4] | 1.64 [1.63–1.64] | 23.1 [22.6–23.2] | 0.558 [0.410–0.756] | 0.214 [0.212–0.215] | 0.609 [0.598–0.611] | 17.9 [17.8–21.1] | 0.62x | 0.38x | 0.12x | 9.87x | 11.6x | 14.1x | 0.34x | 0.13x | 0.37x | 10.9x |
| ray | numeric | 0.218 [0.212–0.218] | 0.214 [0.214–0.218] | 0.175 [0.170–0.175] | 5.10 [5.09–5.14] | 13.8 [13.7–14.0] | 3.54 [3.49–3.62] | 13.1 [13.1–13.3] | 0.204 [0.142–0.582] | 0.132 [0.129–0.132] | 0.942 [0.926–1.10] | 3.28 [3.23–3.67] | 0.06x | 0.06x | 0.05x | 1.44x | 3.88x | 3.71x | 0.06x | 0.04x | 0.27x | 0.93x |

### Julia microbenchmarks

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Python (ms) | Go (ms) | Julia (ms) | Java (ms) | Erlang (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Python/Node | Go/Node | Julia/Node | Java/Node | Erlang/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| parse_integers | conversion | 97.4 [97.1–98.9] | 48.1 [47.7–48.6] | 2.27 [2.25–2.33] | 942.5 [938.8–957.9] | 276.0 [275.1–279.1] | 13.1 [13.1–13.2] | 186.0 [185.6–187.1] | 2.31 [2.26–2.40] | 25.1 [24.6–25.7] | 5.21 [4.79–5.49] | 19.5 [19.0–24.7] | 7.45x | 3.68x | 0.17x | 72.0x | 21.1x | 14.2x | 0.18x | 1.92x | 0.40x | 1.49x |
| matrix_statistics | numeric | 17.3 [17.3–17.5] | 17.1 [16.8–17.2] | 9.78 [9.74–11.0] | 2.00s [1.99s–2.01s] | 1.28s [1.28s–1.29s] | 21.3 [21.0–21.3] | 1.20s [1.20s–1.21s] | 10.4 [9.79–11.6] | 14.1 [13.9–14.1] | 12.0 [11.9–13.2] | 338.0 [305.9–445.7] | 0.81x | 0.80x | 0.46x | 93.9x | 60.1x | 56.5x | 0.49x | 0.66x | 0.56x | 15.9x |
| iteration_pi_sum | numeric | 6.82 [6.81–6.83] | 6.81 [6.81–6.86] | 3.00 [2.98–23.0] | 168.1 [167.4–169.3] | 164.2 [163.9–164.5] | 4.86 [4.81–5.10] | 291.1 [290.2–292.2] | 2.91 [2.89–5.29] | 3.76 [3.76–3.77] | 3.46 [3.45–3.58] | 45.2 [44.8–118.3] | 1.40x | 1.40x | 0.62x | 34.5x | 33.8x | 59.8x | 0.60x | 0.77x | 0.71x | 9.30x |
| formatted_output | io | 142.5 [142.1–146.5] | 83.1 [82.5–101.1] | 8.24 [8.20–8.39] | 1.16s [1.16s–1.18s] | 358.2 [357.0–376.0] | 36.3 [36.2–36.3] | 199.2 [197.9–206.7] | 7.97 [7.81–8.82] | 40.3 [40.2–40.5] | 14.2 [14.1–14.9] | 58.4 [56.4–152.5] | 3.93x | 2.29x | 0.23x | 32.1x | 9.88x | 5.49x | 0.22x | 1.11x | 0.39x | 1.61x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Python (ms) | Go (ms) | Julia (ms) | Java (ms) | Erlang (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Python/Node | Go/Node | Julia/Node | Java/Node | Erlang/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 9.10 [8.99–9.29] | 6.89 [6.87–6.92] | 0.514 [0.512–0.519] | 293.4 [293.2–294.1] | 215.5 [214.4–217.7] | 17.4 [17.4–17.5] | 49.8 [49.1–50.2] | 1.86 [1.37–4.21] | 6.55 [6.48–6.59] | 7.21 [6.78–9.96] | 54.5 [51.8–64.5] | 0.52x | 0.40x | 0.03x | 16.8x | 12.4x | 2.86x | 0.11x | 0.38x | 0.41x | 3.13x |
| navier_stokes | numeric | 81.9 [81.4–82.3] | 70.7 [69.8–71.0] | 46.7 [46.6–46.7] | 208.8 [208.3–209.9] | 98.5 [97.8–98.9] | 14.0 [13.8–14.3] | 118.1 [116.7–120.1] | 3.27 [3.07–6.64] | 2.96 [2.96–2.97] | 5.38 [4.62–5.68] | 188.0 [186.1–197.6] | 5.84x | 5.04x | 3.33x | 14.9x | 7.02x | 8.41x | 0.23x | 0.21x | 0.38x | 13.4x |
| splay | data | 292.3 [291.1–292.6] | 287.8 [286.4–297.6] | 18.7 [18.4–19.1] | 322.3 [320.6–323.1] | 144.0 [143.1–146.7] | 19.3 [18.9–19.3] | 294.3 [293.3–294.3] | 27.7 [25.7–43.2] | 149.8 [149.5–149.9] | 11.9 [11.3–27.0] | 50.0 [45.2–50.4] | 15.2x | 14.9x | 0.97x | 16.7x | 7.47x | 15.3x | 1.44x | 7.77x | 0.62x | 2.60x |
| hashmap | data | 72.8 [72.4–72.9] | 27.6 [27.3–28.3] | 2.72 [2.70–2.76] | 1.19s [1.19s–1.20s] | 311.9 [311.7–312.4] | 15.2 [15.1–15.3] | 83.0 [82.9–84.3] | 4.71 [3.77–6.91] | 1.32 [1.31–1.34] | 11.0 [10.4–18.6] | 37.0 [36.6–37.2] | 4.78x | 1.82x | 0.18x | 78.3x | 20.5x | 5.45x | 0.31x | 0.09x | 0.72x | 2.43x |
| crypto_sha1 | crypto | 50.0 [49.8–50.0] | 28.4 [28.4–28.4] | 2.67 [2.63–2.70] | 311.9 [308.5–312.2] | 216.3 [216.1–216.8] | 8.66 [8.57–8.71] | 319.0 [318.8–320.1] | 0.393 [0.266–0.582] | 3.53 [3.51–3.53] | 3.30 [2.70–10.6] | 0.178 [0.177–0.188] | 5.78x | 3.28x | 0.31x | 36.0x | 25.0x | 36.9x | 0.05x | 0.41x | 0.38x | 0.02x |
| raytrace3d | 3d | 75.9 [75.1–77.0] | 13.1 [12.6–13.3] | 2.17 [2.17–2.17] | 515.8 [514.7–518.6] | 161.2 [160.7–162.8] | 18.3 [18.1–18.4] | 154.7 [153.7–155.3] | 4.05 [3.22–6.00] | 154.2 [148.7–154.7] | 9.50 [9.15–27.2] | 36.4 [34.9–36.5] | 4.16x | 0.72x | 0.12x | 28.3x | 8.83x | 8.47x | 0.22x | 8.45x | 0.52x | 1.99x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | Python (ms) | Go (ms) | Julia (ms) | Java (ms) | Erlang (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node | Python/Node | Go/Node | Julia/Node | Java/Node | Erlang/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 51.1 [50.9–51.2] | 44.8 [44.8–44.9] | 13.1 [13.1–13.2] | 1.81s [1.81s–1.84s] | 618.4 [616.5–652.6] | 39.7 [39.0–40.4] | 337.3 [337.0–338.1] | 16.6 [14.3–28.7] | 417.8 [405.5–422.1] | 53.9 [52.1–89.2] | 197.0 [186.5–296.1] | 1.29x | 1.13x | 0.33x | 45.7x | 15.6x | 8.51x | 0.42x | 10.5x | 1.36x | 4.97x |
| microdiff | data-diff | 35.6 [35.3–35.9] | 38.7 [38.3–39.8] | 2.62 [2.60–2.63] | 769.0 [763.0–800.5] | 109.7 [108.2–110.1] | 16.3 [16.0–16.6] | 45.8 [45.2–47.2] | 11.5 [9.24–18.6] | 56.4 [56.2–56.5] | 15.1 [11.9–15.3] | 7.21 [6.98–12.3] | 2.19x | 2.38x | 0.16x | 47.2x | 6.74x | 2.82x | 0.70x | 3.46x | 0.93x | 0.44x |
| hyphen | hyphenation | 48.8 [47.8–49.1] | 9.43 [9.35–9.62] | 1.46 [1.46–1.53] | 94.9 [94.7–95.2] | 81.6 [80.4–102.2] | 6.77 [6.68–7.30] | 46.2 [46.0–46.9] | 2.06 [1.68–3.00] | 79.0 [75.9–79.8] | 6.65 [5.08–6.66] | 11.3 [11.0–16.8] | 7.21x | 1.39x | 0.22x | 14.0x | 12.1x | 6.83x | 0.30x | 11.7x | 0.98x | 1.66x |
| prettier_ast | formatting | 820.4 [818.3–820.9] | 361.2 [358.2–361.5] | 41.3 [41.2–41.5] | 2.53s [2.52s–2.55s] | 1.44s [1.41s–1.45s] | 99.9 [99.8–100.8] | 1.01s [1.01s–1.02s] | 92.2 [91.1–115.5] | 585.2 [583.8–590.6] | 71.3 [64.6–75.4] | 158.6 [156.9–258.8] | 8.21x | 3.61x | 0.41x | 25.3x | 14.4x | 10.1x | 0.92x | 5.86x | 0.71x | 1.59x |
| text_search | search | 1.70s [1.70s–1.72s] | 1.48s [1.47s–1.48s] | 536.7 [536.2–537.3] | 26.17s [26.09s–26.29s] | 38.63s [38.58s–38.63s] | 773.1 [772.6–774.5] | 35.90s [35.83s–36.62s] | 452.3 [450.7–474.9] | 11.16s [11.07s–11.19s] | 599.8 [597.9–602.1] | 12.58s [10.82s–27.51s] | 2.20x | 1.91x | 0.69x | 33.9x | 50.0x | 46.4x | 0.58x | 14.4x | 0.78x | 16.3x |
| three_way_merge | merge | 2.03s [2.01s–2.03s] | 1.51s [1.50s–1.53s] | 2.12s [2.12s–2.12s] | 9.16s [9.14s–9.18s] | 6.09s [6.08s–6.19s] | 954.5 [954.1–961.9] | 2.49s [2.48s–2.49s] | 584.0 [581.7–599.3] | 2.67s [2.65s–2.69s] | 687.2 [677.4–692.0] | 1.98s [1.83s–2.05s] | 2.12x | 1.58x | 2.22x | 9.59x | 6.38x | 2.60x | 0.61x | 2.80x | 0.72x | 2.08x |
| log_pipeline | log-processing | 3.95s [3.94s–3.95s] | 1.05s [1.04s–1.05s] | 619.9 [618.3–621.4] | 12.53s [12.41s–12.77s] | 9.32s [9.31s–9.35s] | 938.6 [930.5–941.3] | 3.95s [3.90s–3.95s] | 332.9 [332.6–345.5] | 6.42s [6.40s–6.50s] | 678.2 [674.4–731.2] | 5.64s [5.64s–10.36s] | 4.21x | 1.12x | 0.66x | 13.3x | 9.93x | 4.21x | 0.35x | 6.84x | 0.72x | 6.01x |
| jq_mix | jq-breadth | 44.48s [44.41s–44.51s] | 44.74s [44.63s–44.80s] | 1.00s [991.1–1.01s] / λ-VM 36.75s | --- | --- | 3.94s [3.94s–3.98s] | 10.30s [10.26s–10.41s] | 994.8 [989.8–1.01s] | 3.67s [3.62s–3.71s] | 1.34s [1.28s–1.50s] | 8.09s [8.07s–8.21s] | 11.3x | 11.3x | 0.25x | --- | --- | 2.61x | 0.25x | 0.93x | 0.34x | 2.05x |
| jq_records | jq-data | 537.6 [534.5–544.0] | 601.3 [589.2–620.6] | 871.8 [871.4–886.2] / λ-VM 33.51s | 321.07s [319.05s–321.28s] | 10.77s [10.72s–10.77s] | 1.99s [1.98s–2.05s] | 6.35s [6.32s–6.36s] | 1.03s [1.03s–1.06s] | 4.06s [4.00s–4.13s] | 904.7 [868.6–918.4] | 5.96s [5.91s–6.25s] | 0.27x | 0.30x | 0.44x | 161x | 5.41x | 3.19x | 0.52x | 2.04x | 0.46x | 3.00x |
| jq_bf | jq-interpreter | 175.3 [174.9–176.1] | 136.0 [135.4–137.1] | 1.82s [1.82s–1.84s] / λ-VM 106.30s | --- | 27.15s [27.10s–27.22s] | 2.91s [2.86s–2.93s] | 22.62s [22.27s–22.68s] | 2.47s [2.46s–2.50s] | 12.01s [12.00s–12.36s] | 1.90s [1.90s–1.91s] | 22.17s [21.95s–22.25s] | 0.06x | 0.05x | 0.63x | --- | 9.34x | 7.78x | 0.85x | 4.13x | 0.65x | 7.62x |
| jq_tree | jq-paths | 1.94s [1.92s–1.97s] | 1.54s [1.53s–1.54s] | 3.96s [3.95s–4.02s] / λ-VM 142.46s | --- | 46.72s [46.70s–46.92s] | 5.70s [5.70s–5.71s] | 35.22s [35.02s–35.69s] | 3.97s [3.97s–4.02s] | 13.59s [13.44s–13.68s] | 2.51s [2.41s–3.98s] | 21.46s [20.61s–23.82s] | 0.34x | 0.27x | 0.70x | --- | 8.19x | 6.18x | 0.70x | 2.38x | 0.44x | 3.76x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, including **startup and compilation performed inside that process**. Build steps completed before invocation are excluded. The MIR and LambdaJS columns use the shipped auto tier -- no `LAMBDA_EXEC_BACKEND` or `JS_EXEC_BACKEND` override -- which is what `lambda.exe run script.ls` and `lambda.exe js script.js` actually do.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Process timing includes each engine's recorded warmup policy, workload and verification; separately added measurements are described above.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. The MIR and LambdaJS columns are re-run, because part 1 pins the compiled lane and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed Julia (startup + warmup) | Timed Java (startup + warmup) | Timed Erlang (startup + warmup) | Timed LambdaJS (auto) | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | Julia (startup + warmup)/Node geo | Java (startup + warmup)/Node geo | Erlang (startup + warmup)/Node geo | LambdaJS (auto)/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.42x | 0.40x | 1.03x | 9.66x | 0.77x | 3.49x | 0.68x | 0.41x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 0.98x | 0.86x | 0.92x | 10.7x | 0.91x | 5.04x | 5.68x | 0.79x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.49x | 0.47x | 1.03x | 24.7x | 0.89x | 3.37x | 1.31x | 0.33x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 1.47x | 0.92x | 0.72x | 6.44x | 0.78x | 4.18x | 6.27x | 2.90x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 1.07x | 0.68x | 1.00x | 7.75x | 0.85x | 3.89x | 2.42x | 1.92x |
| Julia microbenchmarks | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 2.42x | 1.14x | 1.23x | 3.64x | 0.73x | 3.83x | 19.8x | 9.51x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 3.06x | 2.60x | 0.92x | 8.34x | 1.57x | 6.91x | 17.3x | 3.72x |
| Text | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 7 | 10 | 11 | 1.79x | 1.42x | 0.74x | 11.1x | 1.40x | 6.99x | 20.5x | 6.97x |
| **Overall** | 71 | 71 | 71 | 71 | 71 | 71 | 71 | 67 | 70 | 71 | 1.08x | 0.84x | 0.92x | 9.72x | 0.95x | 4.59x | 4.15x | 1.53x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Julia (startup + warmup) (ms) | Java (startup + warmup) (ms) | Erlang (startup + warmup) (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | Julia (startup + warmup)/Node | Java (startup + warmup)/Node | Erlang (startup + warmup)/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 16.6 [16.4–16.9] | 16.4 [16.3–16.8] | 47.3 [47.0–481.2] | 437.0 [436.0–4.97s] | 37.0 [33.1–44.8] | 153.9 [150.6–186.3] | 20.9 [20.3–21.1] | 23.9 [23.8–24.6] | 44.6 [44.1–44.9] | 0.37x | 0.37x | 1.06x | 9.80x | 0.83x | 3.45x | 0.47x | 0.54x |
| fibfp | recursive | 15.7 [15.5–15.7] | 16.2 [15.7–17.4] | 46.0 [45.5–47.2] | 440.0 [438.3–441.2] | 32.2 [32.0–50.9] | 157.5 [155.4–265.6] | 20.4 [20.3–20.9] | 24.4 [24.0–24.5] | 45.3 [45.2–45.4] | 0.35x | 0.36x | 1.02x | 9.72x | 0.71x | 3.48x | 0.45x | 0.54x |
| tak | recursive | 14.5 [14.3–14.8] | 14.4 [14.1–14.5] | 45.4 [44.6–45.9] | 434.8 [434.5–435.4] | 32.1 [31.3–42.1] | 162.8 [142.2–199.6] | 19.6 [19.6–19.6] | 7.91 [7.87–8.53] | 43.3 [43.0–43.6] | 0.33x | 0.33x | 1.05x | 10.0x | 0.74x | 3.76x | 0.45x | 0.18x |
| cpstak | closure | 14.6 [14.4–14.9] | 14.0 [14.0–14.3] | 45.8 [45.6–45.8] | 437.1 [436.6–437.6] | 34.1 [31.7–41.6] | 148.9 [143.8–181.7] | 20.1 [19.8–20.2] | 10.8 [10.8–11.2] | 43.3 [43.2–43.5] | 0.34x | 0.32x | 1.06x | 10.1x | 0.79x | 3.44x | 0.47x | 0.25x |
| sum | iterative | 17.1 [17.0–17.6] | 14.2 [14.0–14.2] | 45.6 [45.1–46.3] | 434.9 [433.8–435.5] | 33.5 [32.4–34.4] | 152.5 [148.8–219.0] | 22.9 [22.7–23.0] | 36.3 [36.2–36.6] | 43.7 [43.6–44.1] | 0.39x | 0.32x | 1.05x | 9.96x | 0.77x | 3.49x | 0.52x | 0.83x |
| sumfp | iterative | 18.0 [16.6–18.3] | 15.5 [15.3–15.6] | 44.6 [44.5–45.0] | 438.0 [434.0–438.0] | 31.1 [30.4–39.6] | 149.3 [148.8–173.3] | 21.2 [21.1–21.3] | 8.95 [8.92–9.37] | 43.7 [43.2–43.7] | 0.41x | 0.35x | 1.02x | 10.0x | 0.71x | 3.42x | 0.49x | 0.21x |
| nqueens | backtrack | 22.8 [22.8–22.8] | 25.5 [25.3–44.6] | 46.0 [45.7–46.4] | 434.9 [434.7–435.9] | 33.8 [31.1–35.9] | 145.2 [144.4–173.2] | 95.0 [94.8–95.2] | 14.6 [14.2–15.0] | 44.7 [44.7–44.9] | 0.51x | 0.57x | 1.03x | 9.73x | 0.76x | 3.25x | 2.13x | 0.33x |
| fft | numeric | 29.0 [28.7–29.1] | 27.2 [27.1–28.5] | 45.3 [44.7–45.6] | 434.9 [434.3–435.9] | 35.7 [32.7–67.5] | 161.7 [156.4–218.7] | 112.2 [111.2–128.5] | 8.15 [7.95–8.96] | 45.7 [44.0–47.0] | 0.64x | 0.60x | 0.99x | 9.51x | 0.78x | 3.54x | 2.45x | 0.18x |
| mbrot | numeric | 18.2 [17.7–18.3] | 18.7 [18.6–18.8] | 47.0 [46.4–47.5] | 439.6 [437.6–471.2] | 33.4 [33.0–36.2] | 154.6 [150.6–186.3] | 25.9 [25.7–26.1] | 23.6 [23.5–23.7] | 44.5 [44.4–44.9] | 0.41x | 0.42x | 1.06x | 9.87x | 0.75x | 3.47x | 0.58x | 0.53x |
| ack | recursive | 28.0 [27.7–28.3] | 23.6 [23.5–23.8] | 56.7 [56.3–79.1] | 448.1 [446.8–451.5] | 47.4 [39.6–75.1] | 201.6 [173.4–270.9] | 32.4 [32.1–35.6] | 106.3 [106.0–107.7] | 56.1 [55.9–57.7] | 0.50x | 0.42x | 1.01x | 7.99x | 0.85x | 3.60x | 0.58x | 1.90x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Julia (startup + warmup) (ms) | Java (startup + warmup) (ms) | Erlang (startup + warmup) (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | Julia (startup + warmup)/Node | Java (startup + warmup)/Node | Erlang (startup + warmup)/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 15.0 [14.7–15.1] | 14.5 [14.2–14.7] | 44.7 [44.6–45.1] | 420.4 [420.4–421.8] | 35.7 [33.6–41.0] | 174.4 [149.7–211.9] | 22.5 [22.4–22.6] | 6.05 [5.85–6.42] | 44.0 [43.9–45.5] | 0.34x | 0.33x | 1.02x | 9.56x | 0.81x | 3.97x | 0.51x | 0.14x |
| permute | micro | 16.8 [16.8–17.2] | 17.8 [17.0–18.1] | 48.1 [45.2–51.7] | 421.0 [419.8–421.5] | 33.4 [33.2–37.5] | 177.5 [177.0–195.5] | 40.7 [40.4–40.7] | 6.90 [6.84–7.48] | 44.6 [44.5–44.7] | 0.38x | 0.40x | 1.08x | 9.43x | 0.75x | 3.98x | 0.91x | 0.15x |
| queens | micro | 18.5 [18.2–18.5] | 20.4 [19.9–20.5] | 45.2 [45.1–46.1] | 420.6 [419.5–422.4] | 34.8 [32.9–35.1] | 167.5 [163.8–240.1] | 29.9 [29.8–30.0] | 6.45 [6.39–7.02] | 44.2 [43.9–44.3] | 0.42x | 0.46x | 1.02x | 9.52x | 0.79x | 3.79x | 0.68x | 0.15x |
| towers | micro | 21.7 [21.0–21.9] | 23.6 [23.2–23.9] | 45.1 [44.2–46.4] | 423.4 [419.4–440.1] | 35.4 [33.1–37.1] | 192.1 [168.8–237.0] | 54.3 [54.2–54.6] | 7.88 [7.82–8.45] | 45.1 [44.3–45.1] | 0.48x | 0.52x | 1.00x | 9.39x | 0.79x | 4.26x | 1.20x | 0.17x |
| bounce | micro | 18.1 [17.8–18.4] | 17.1 [16.7–17.2] | 45.2 [45.1–46.3] | 419.7 [419.4–421.5] | 34.5 [32.9–34.9] | 181.7 [163.6–208.4] | 30.5 [30.3–30.9] | 7.01 [6.88–7.46] | 44.3 [43.9–44.7] | 0.41x | 0.39x | 1.02x | 9.48x | 0.78x | 4.10x | 0.69x | 0.16x |
| list | micro | 16.5 [16.5–16.8] | 16.7 [16.6–16.8] | 44.7 [44.6–45.8] | 423.4 [422.7–437.3] | 33.6 [32.4–34.6] | 209.0 [164.7–212.6] | 27.8 [27.7–27.9] | 6.53 [6.37–6.88] | 44.2 [44.1–44.2] | 0.37x | 0.38x | 1.01x | 9.59x | 0.76x | 4.73x | 0.63x | 0.15x |
| storage | micro | 16.2 [15.9–16.7] | 16.6 [16.5–16.8] | 44.8 [44.6–45.4] | 421.1 [419.8–421.7] | 39.2 [39.2–39.6] | 165.6 [161.7–327.9] | 33.6 [33.3–33.8] | 8.25 [7.91–8.67] | 44.5 [44.5–44.7] | 0.36x | 0.37x | 1.01x | 9.47x | 0.88x | 3.72x | 0.76x | 0.19x |
| mandelbrot | compute | 77.8 [77.8–78.0] | 62.6 [62.5–63.2] | 75.8 [75.8–76.3] | 486.3 [484.4–514.3] | 94.7 [94.2–95.5] | 643.8 [601.8–735.3] | 6.22s [6.21s–6.30s] | 876.0 [875.6–888.4] | 74.5 [74.4–76.3] | 1.04x | 0.84x | 1.02x | 6.53x | 1.27x | 8.64x | 83.5x | 11.8x |
| nbody | compute | 54.7 [54.6–55.0] | 65.0 [64.9–65.1] | 47.1 [46.8–48.4] | 424.7 [424.0–428.9] | 38.2 [38.1–39.5] | 274.3 [268.6–284.8] | 2.90s [2.89s–2.93s] | 164.9 [164.8–165.0] | 50.2 [49.8–50.3] | 1.09x | 1.30x | 0.94x | 8.47x | 0.76x | 5.47x | 57.9x | 3.29x |
| richards | macro | 576.5 [571.4–606.0] | 327.3 [318.0–334.4] | 77.8 [77.4–78.0] | 644.1 [639.5–648.3] | 94.2 [92.9–102.4] | 915.1 [890.4–949.9] | 3.11s [3.11s–3.14s] | 196.4 [195.7–196.9] | 91.1 [90.8–92.9] | 6.33x | 3.59x | 0.85x | 7.07x | 1.03x | 10.0x | 34.2x | 2.16x |
| json | macro | 43.3 [43.0–45.7] | 39.4 [37.6–39.4] | 52.6 [52.0–52.8] | 653.8 [653.2–674.9] | 40.2 [39.7–41.0] | 161.9 [146.2–181.8] | 131.7 [131.3–131.7] | 18.4 [18.3–18.7] | 46.8 [46.4–48.4] | 0.93x | 0.84x | 1.12x | 14.0x | 0.86x | 3.46x | 2.81x | 0.39x |
| deltablue | macro | 293.3 [291.9–296.6] | 229.6 [224.1–231.8] | 53.6 [53.4–55.5] | 1.16s [1.15s–1.18s] | 63.6 [59.7–65.3] | 389.8 [372.8–430.1] | 1.17s [1.16s–1.21s] | 106.7 [106.3–107.1] | 56.7 [56.5–56.7] | 5.17x | 4.05x | 0.95x | 20.6x | 1.12x | 6.88x | 20.7x | 1.88x |
| havlak | macro | 213.2 [207.9–219.4] | 115.4 [113.8–121.3] | 53.6 [53.5–55.0] | 2.68s [2.64s–2.71s] | 194.8 [179.2–199.7] | 1.43s [1.30s–1.51s] | 41.10s [41.06s–41.22s] | 3.26s [3.25s–3.36s] | 142.1 [140.7–142.5] | 1.50x | 0.81x | 0.38x | 18.8x | 1.37x | 10.1x | 289x | 22.9x |
| cd | macro | 827.0 [823.6–828.4] | 557.7 [545.7–567.0] | 67.3 [66.8–67.6] | 1.50s [1.50s–1.52s] | 76.8 [76.8–83.9] | 263.4 [261.9–275.8] | 8.54s [8.41s–8.97s] | 966.9 [963.0–998.1] | 80.5 [80.1–81.8] | 10.3x | 6.92x | 0.84x | 18.7x | 0.95x | 3.27x | 106x | 12.0x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Julia (startup + warmup) (ms) | Java (startup + warmup) (ms) | Erlang (startup + warmup) (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | Julia (startup + warmup)/Node | Java (startup + warmup)/Node | Erlang (startup + warmup)/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 24.1 [24.1–24.7] | 19.4 [19.3–19.7] | 48.1 [47.7–49.3] | 1.11s [1.11s–1.12s] | 35.4 [34.8–39.5] | 156.5 [146.2–163.7] | 60.8 [60.7–61.0] | 29.1 [28.9–29.8] | 45.2 [44.7–45.3] | 0.53x | 0.43x | 1.06x | 24.5x | 0.78x | 3.46x | 1.35x | 0.64x |
| fannkuch | permutation | 31.8 [30.4–33.2] | 30.1 [28.7–30.1] | 45.6 [45.4–45.7] | 1.10s [1.09s–1.15s] | 36.2 [35.8–38.2] | 158.7 [148.1–160.5] | 148.8 [148.8–150.5] | 12.9 [12.4–13.4] | 45.6 [44.7–46.0] | 0.70x | 0.66x | 1.00x | 24.1x | 0.79x | 3.48x | 3.27x | 0.28x |
| fasta | generation | 22.5 [22.1–22.5] | 22.5 [22.4–23.7] | 45.8 [45.3–45.8] | 1.07s [1.07s–1.08s] | 35.5 [35.0–36.0] | 155.6 [146.2–199.9] | 59.9 [59.4–60.7] | 14.6 [14.5–15.0] | 48.0 [47.9–49.0] | 0.47x | 0.47x | 0.95x | 22.3x | 0.74x | 3.24x | 1.25x | 0.31x |
| knucleotide | hashing | 26.3 [25.3–28.0] | 24.4 [24.2–24.9] | 45.9 [45.7–46.5] | 1.07s [1.07s–1.10s] | 49.1 [48.6–53.4] | 149.8 [147.0–160.8] | 55.3 [54.5–73.9] | 13.4 [13.2–13.9] | 46.0 [45.5–46.6] | 0.57x | 0.53x | 1.00x | 23.3x | 1.07x | 3.26x | 1.20x | 0.29x |
| pidigits | bignum | 13.8 [13.8–14.9] | 13.9 [13.8–14.1] | 46.9 [46.6–47.7] | 1.10s [1.08s–1.11s] | 35.2 [34.7–37.2] | 145.1 [143.0–155.5] | 19.4 [18.9–19.5] | 5.39 [5.26–6.11] | 43.1 [42.7–43.3] | 0.32x | 0.32x | 1.09x | 25.5x | 0.82x | 3.36x | 0.45x | 0.12x |
| regexredux | regex | 13.9 [13.7–16.0] | 14.6 [14.5–15.2] | 48.1 [47.5–48.6] | 1.15s [1.14s–1.18s] | 55.8 [55.0–88.7] | 154.6 [150.9–166.0] | 26.4 [26.2–26.5] | 10.9 [10.9–11.6] | 43.4 [42.9–43.8] | 0.32x | 0.34x | 1.11x | 26.4x | 1.28x | 3.56x | 0.61x | 0.25x |
| revcomp | string | 16.6 [16.4–16.7] | 16.8 [16.4–17.1] | 45.6 [45.1–46.0] | 1.26s [1.24s–1.28s] | 36.6 [36.3–37.6] | 146.5 [145.9–153.9] | 64.7 [64.0–64.8] | 8.18 [8.09–8.76] | 44.9 [44.9–45.4] | 0.37x | 0.37x | 1.02x | 28.1x | 0.82x | 3.26x | 1.44x | 0.18x |
| spectralnorm | numeric | 39.9 [39.6–41.0] | 41.3 [41.2–42.6] | 45.8 [45.6–46.4] | 1.09s [1.08s–1.09s] | 44.8 [44.5–47.4] | 153.4 [152.0–164.2] | 150.8 [149.8–151.3] | 70.2 [69.9–70.6] | 45.7 [45.1–45.8] | 0.87x | 0.90x | 1.00x | 23.8x | 0.98x | 3.36x | 3.30x | 1.54x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Julia (startup + warmup) (ms) | Java (startup + warmup) (ms) | Erlang (startup + warmup) (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | Julia (startup + warmup)/Node | Java (startup + warmup)/Node | Erlang (startup + warmup)/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 147.3 [146.9–147.7] | 139.4 [135.3–153.9] | 73.0 [73.0–74.1] | 1.30s [1.30s–1.40s] | 97.7 [96.4–100.9] | 634.6 [627.3–655.6] | 1.82s [1.82s–1.83s] | 860.3 [858.2–882.1] | 77.8 [76.8–77.9] | 1.89x | 1.79x | 0.94x | 16.8x | 1.26x | 8.16x | 23.4x | 11.1x |
| matmul | numeric | 224.1 [223.3–225.3] | 49.9 [49.7–50.0] | 52.6 [52.0–59.7] | 465.6 [463.5–477.8] | 48.9 [47.8–54.0] | 396.3 [372.5–398.0] | 295.9 [295.5–298.1] | 545.2 [544.7–547.3] | 58.9 [58.5–60.4] | 3.81x | 0.85x | 0.89x | 7.91x | 0.83x | 6.73x | 5.03x | 9.26x |
| primes | numeric | 162.4 [161.3–164.4] | 75.2 [74.8–75.5] | 46.5 [46.2–66.9] | 445.5 [443.9–449.5] | 42.2 [41.8–42.9] | 742.8 [712.2–771.6] | 442.1 [437.4–443.0] | 109.2 [101.4–169.8] | 49.4 [48.5–49.9] | 3.29x | 1.52x | 0.94x | 9.02x | 0.85x | 15.0x | 8.95x | 2.21x |
| base64 | string | 55.5 [53.7–57.0] | 48.7 [47.6–48.8] | 45.9 [45.8–46.7] | 553.8 [547.1–580.6] | 54.0 [50.3–84.7] | 171.3 [169.4–183.4] | 584.0 [580.5–611.4] | 164.5 [163.7–165.1] | 60.9 [60.8–61.9] | 0.91x | 0.80x | 0.75x | 9.09x | 0.89x | 2.81x | 9.58x | 2.70x |
| levenshtein | string | 106.2 [105.4–106.4] | 63.1 [62.2–64.1] | 46.0 [45.9–46.6] | 474.9 [473.7–476.1] | 48.3 [46.8–48.4] | 182.6 [162.5–184.8] | 1.01s [1.01s–1.05s] | 60.1 [60.0–61.6] | 46.6 [46.4–47.0] | 2.28x | 1.35x | 0.99x | 10.2x | 1.04x | 3.92x | 21.7x | 1.29x |
| json_gen | data | 63.7 [63.7–64.8] | 51.6 [51.2–52.2] | 46.5 [46.5–47.8] | 463.5 [452.5–470.9] | 38.4 [36.5–39.0] | 155.2 [150.7–166.6] | 76.4 [75.7–76.5] | 25.5 [25.5–26.3] | 50.0 [48.7–50.7] | 1.27x | 1.03x | 0.93x | 9.26x | 0.77x | 3.10x | 1.53x | 0.51x |
| collatz | numeric | 347.1 [344.8–362.6] | 317.4 [317.0–317.4] | 270.7 [269.9–270.8] | 656.3 [655.7–672.0] | 398.3 [363.7–409.3] | 1.15s [1.12s–1.36s] | 1.66s [1.66s–1.69s] | 6.23s [6.23s–6.26s] | 1.46s [1.46s–1.46s] | 0.24x | 0.22x | 0.19x | 0.45x | 0.27x | 0.79x | 1.14x | 4.27x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Julia (startup + warmup) (ms) | Java (startup + warmup) (ms) | Erlang (startup + warmup) (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | Julia (startup + warmup)/Node | Java (startup + warmup)/Node | Erlang (startup + warmup)/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 208.3 [206.5–223.8] | 190.0 [189.6–191.1] | 106.0 [105.8–106.8] | 576.3 [575.8–577.6] | 132.8 [126.6–133.4] | 507.6 [507.1–525.6] | 6.06s [6.06s–6.10s] | 2.18s [2.18s–2.22s] | 112.1 [111.9–113.2] | 1.86x | 1.69x | 0.95x | 5.14x | 1.18x | 4.53x | 54.0x | 19.5x |
| array1 | array | 135.8 [135.1–136.4] | 129.7 [126.9–131.7] | 44.4 [44.3–45.6] | 484.0 [477.1–508.4] | 37.2 [34.9–41.5] | 159.4 [150.5–182.0] | 44.5 [43.3–44.8] | 41.4 [41.4–41.7] | 44.5 [44.4–44.6] | 3.05x | 2.91x | 1.00x | 10.9x | 0.84x | 3.58x | 1.00x | 0.93x |
| deriv | symbolic | 44.6 [44.5–45.1] | 28.6 [28.2–28.9] | 48.8 [48.5–49.5] | 489.6 [487.0–491.8] | 35.2 [34.4–39.4] | 149.3 [148.9–156.3] | 127.8 [127.0–145.0] | 65.4 [65.4–66.1] | 47.9 [47.9–48.3] | 0.93x | 0.60x | 1.02x | 10.2x | 0.73x | 3.12x | 2.67x | 1.37x |
| diviter | iterative | 377.1 [373.0–385.1] | 13.9 [13.6–14.4] | 299.3 [294.2–300.4] | 480.1 [477.6–485.7] | 843.8 [589.3–859.8] | 4.76s [4.75s–6.56s] | 636.2 [635.1–636.9] | 26.61s [26.57s–26.75s] | 508.0 [505.3–509.1] | 0.74x | 0.03x | 0.59x | 0.95x | 1.66x | 9.37x | 1.25x | 52.4x |
| divrec | recursive | 15.9 [15.9–16.2] | 16.8 [16.5–18.5] | 49.7 [49.5–51.2] | 492.0 [489.1–492.9] | 35.6 [35.4–35.9] | 159.5 [153.9–219.3] | 36.8 [36.7–37.1] | 41.6 [41.0–43.2] | 50.5 [50.4–50.8] | 0.32x | 0.33x | 0.98x | 9.75x | 0.71x | 3.16x | 0.73x | 0.83x |
| gcbench | allocation | 227.9 [227.3–241.8] | 119.6 [119.2–119.6] | 116.2 [115.3–117.8] | 541.6 [541.4–576.3] | 59.3 [59.2–60.3] | 215.5 [210.6–283.9] | 1.06s [1.05s–1.08s] | 568.2 [563.7–577.0] | 65.3 [65.0–65.6] | 3.49x | 1.83x | 1.78x | 8.29x | 0.91x | 3.30x | 16.2x | 8.70x |
| paraffins | combinat | 55.4 [55.3–56.0] | 30.0 [30.0–57.1] | 46.1 [45.9–47.1] | 497.9 [496.2–508.2] | 30.9 [30.7–31.7] | 149.3 [148.7–202.5] | 68.4 [68.3–69.5] | 7.99 [7.97–8.68] | 44.3 [44.1–44.3] | 1.25x | 0.68x | 1.04x | 11.3x | 0.70x | 3.37x | 1.55x | 0.18x |
| pnpoly | numeric | 43.8 [43.3–46.3] | 43.6 [43.0–43.8] | 47.1 [46.7–47.5] | 500.1 [497.7–520.5] | 38.3 [37.7–39.1] | 199.5 [191.8–299.4] | 108.4 [106.3–108.6] | 208.8 [208.6–209.7] | 48.9 [48.7–49.4] | 0.90x | 0.89x | 0.96x | 10.2x | 0.78x | 4.08x | 2.22x | 4.27x |
| puzzle | search | 36.7 [35.4–56.7] | 37.8 [37.3–38.1] | 46.8 [46.2–47.1] | 481.3 [480.5–482.3] | 34.3 [33.5–80.3] | 152.0 [150.7–240.4] | 60.0 [59.8–60.3] | 35.2 [35.1–35.4] | 46.7 [46.3–47.7] | 0.79x | 0.81x | 1.00x | 10.3x | 0.73x | 3.25x | 1.28x | 0.75x |
| quicksort | sorting | 25.2 [25.0–25.4] | 27.2 [26.9–27.7] | 45.9 [45.6–46.7] | 481.7 [480.2–481.9] | 33.2 [31.4–34.0] | 187.8 [181.8–243.2] | 60.0 [59.6–77.0] | 24.8 [24.6–25.6] | 44.6 [44.0–44.8] | 0.56x | 0.61x | 1.03x | 10.8x | 0.74x | 4.21x | 1.35x | 0.56x |
| ray | numeric | 43.6 [43.0–43.8] | 43.2 [42.9–43.9] | 45.2 [45.2–45.7] | 498.6 [495.4–500.9] | 33.4 [33.0–35.4] | 154.2 [151.7–236.6] | 61.7 [61.6–62.1] | 19.6 [19.4–20.1] | 46.3 [46.0–46.6] | 0.94x | 0.93x | 0.98x | 10.8x | 0.72x | 3.33x | 1.33x | 0.42x |

### Julia microbenchmarks

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Julia (startup + warmup) (ms) | Java (startup + warmup) (ms) | Erlang (startup + warmup) (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | Julia (startup + warmup)/Node | Java (startup + warmup)/Node | Erlang (startup + warmup)/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| parse_integers | conversion | 243.8 [241.9–272.6] | 136.4 [135.7–139.2] | 92.6 [92.0–109.3] | 280.4 [276.2–288.7] | 50.4 [49.1–50.9] | 184.4 [182.6–240.3] | 2.00s [1.98s–2.00s] | 558.7 [557.1–563.7] | 72.5 [72.0–72.9] | 3.36x | 1.88x | 1.28x | 3.87x | 0.70x | 2.54x | 27.5x | 7.71x |
| matrix_statistics | numeric | 80.3 [79.6–88.2] | 78.4 [78.0–79.0] | 107.9 [107.0–180.0] | 306.5 [306.2–307.1] | 70.5 [67.7–70.6] | 790.2 [756.0–1.10s] | 4.05s [3.98s–4.07s] | 2.57s [2.57s–2.58s] | 92.1 [91.8–92.4] | 0.87x | 0.85x | 1.17x | 3.33x | 0.77x | 8.58x | 44.0x | 27.9x |
| iteration_pi_sum | numeric | 200.8 [200.6–201.4] | 34.4 [34.2–34.7] | 94.3 [93.4–153.5] | 223.8 [221.4–224.5] | 40.6 [39.6–40.8] | 238.7 [236.7–562.6] | 340.7 [338.1–351.1] | 335.7 [335.0–335.7] | 53.9 [53.1–67.7] | 3.72x | 0.64x | 1.75x | 4.15x | 0.75x | 4.43x | 6.32x | 6.22x |
| formatted_output | io | 374.8 [373.7–375.1] | 197.7 [195.7–200.5] | 104.5 [103.5–106.7] | 387.5 [386.7–401.9] | 82.2 [80.1–82.7] | 263.0 [256.3–721.5] | 2.39s [2.39s–2.40s] | 724.3 [723.3–740.6] | 118.6 [118.3–118.8] | 3.16x | 1.67x | 0.88x | 3.27x | 0.69x | 2.22x | 20.2x | 6.11x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Julia (startup + warmup) (ms) | Java (startup + warmup) (ms) | Erlang (startup + warmup) (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | Julia (startup + warmup)/Node | Java (startup + warmup)/Node | Erlang (startup + warmup)/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 98.6 [97.8–99.5] | 115.4 [114.2–115.6] | 49.0 [48.6–59.8] | 559.4 [559.2–833.0] | 50.0 [46.5–52.6] | 270.6 [251.1–345.8] | 1.44s [1.44s–1.46s] | 222.0 [220.7–224.5] | 61.9 [61.9–62.7] | 1.59x | 1.87x | 0.79x | 9.04x | 0.81x | 4.37x | 23.3x | 3.59x |
| navier_stokes | numeric | 252.0 [251.7–253.5] | 227.2 [225.6–227.5] | 93.5 [93.3–95.0] | 526.1 [524.1–526.8] | 248.7 [243.1–258.6] | 6.26s [5.77s–6.30s] | 985.3 [984.6–985.3] | 109.2 [108.4–109.7] | 59.0 [58.4–59.3] | 4.27x | 3.85x | 1.59x | 8.92x | 4.22x | 106x | 16.7x | 1.85x |
| splay | data | 428.9 [428.5–432.8] | 356.7 [356.6–380.5] | 67.8 [65.0–79.7] | 1.26s [1.26s–1.28s] | 192.3 [152.2–218.5] | 453.2 [432.4–490.2] | 833.3 [829.7–834.6] | 556.8 [554.3–566.5] | 90.7 [90.2–90.8] | 4.73x | 3.93x | 0.75x | 13.9x | 2.12x | 5.00x | 9.19x | 6.14x |
| hashmap | data | 173.5 [172.8–174.4] | 127.9 [126.8–128.4] | 47.0 [46.9–48.5] | 185.3 [185.0–186.2] | 105.7 [47.7–106.3] | 222.5 [221.1–228.7] | 3.43s [3.42s–3.44s] | 319.1 [319.0–320.0] | 58.4 [58.2–58.6] | 2.97x | 2.19x | 0.80x | 3.17x | 1.81x | 3.81x | 58.7x | 5.47x |
| crypto_sha1 | crypto | 175.8 [174.6–177.1] | 159.0 [157.6–162.9] | 48.6 [48.0–49.4] | 309.4 [309.2–310.1] | 59.1 [56.8–101.2] | 180.2 [164.6–190.5] | 561.4 [560.5–562.2] | 222.8 [222.2–223.0] | 51.5 [51.0–52.0] | 3.41x | 3.08x | 0.94x | 6.00x | 1.15x | 3.50x | 10.9x | 4.32x |
| raytrace3d | 3d | 155.1 [141.6–155.6] | 100.3 [100.2–100.3] | 51.5 [51.3–52.2] | 970.7 [962.9–976.2] | 62.2 [56.1–88.6] | 217.0 [212.5–226.4] | 714.6 [711.4–718.5] | 169.0 [167.5–169.6] | 61.4 [61.4–61.8] | 2.53x | 1.63x | 0.84x | 15.8x | 1.01x | 3.53x | 11.6x | 2.75x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | Julia (startup + warmup) (ms) | Java (startup + warmup) (ms) | Erlang (startup + warmup) (ms) | LambdaJS (auto) (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | Julia (startup + warmup)/Node | Java (startup + warmup)/Node | Erlang (startup + warmup)/Node | LambdaJS (auto)/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 96.9 [96.5–96.9] | 76.6 [75.8–76.9] | 58.6 [58.5–59.9] | 2.02s [2.02s–2.03s] | 176.4 [175.6–229.4] | 530.2 [523.9–957.5] | 2.52s [2.51s–2.54s] | 626.0 [624.2–660.3] | 84.7 [82.8–84.7] | 1.14x | 0.90x | 0.69x | 23.9x | 2.08x | 6.26x | 29.7x | 7.39x |
| microdiff | data-diff | 70.7 [70.2–72.9] | 77.5 [76.6–77.7] | 50.9 [50.8–51.2] | 1.00s [1.00s–1.04s] | 103.2 [94.8–106.2] | 169.6 [168.1–262.4] | 1.29s [1.29s–1.30s] | 116.5 [114.7–116.6] | 60.1 [59.5–61.4] | 1.18x | 1.29x | 0.85x | 16.7x | 1.72x | 2.82x | 21.5x | 1.94x |
| hyphen | hyphenation | 103.9 [96.9–104.8] | 86.8 [82.4–87.0] | 78.2 [75.5–93.7] | 1.27s [1.26s–1.28s] | 88.2 [82.1–91.3] | 177.4 [177.3–289.9] | 354.2 [351.4–367.3] | 103.3 [101.7–123.9] | 58.0 [57.1–58.1] | 1.79x | 1.50x | 1.35x | 21.9x | 1.52x | 3.06x | 6.10x | 1.78x |
| prettier_ast | formatting | 1.34s [1.32s–1.34s] | 1.54s [1.52s–1.55s] | 103.5 [103.4–104.4] | 2.81s [2.81s–2.83s] | 255.1 [225.1–257.1] | 504.6 [464.3–755.7] | 9.22s [9.19s–9.23s] | 1.45s [1.42s–1.46s] | 145.5 [145.3–146.2] | 9.20x | 10.6x | 0.71x | 19.3x | 1.75x | 3.47x | 63.4x | 9.94x |
| text_search | search | 1.73s [1.73s–1.76s] | 1.49s [1.49s–1.52s] | 583.8 [583.8–584.5] | 22.75s [22.63s–22.95s] | 1.26s [1.24s–1.38s] | 64.74s [21.89s–112.39s] | 26.59s [26.53s–26.59s] | 38.64s [38.59s–38.64s] | 817.8 [817.7–818.7] | 2.12x | 1.83x | 0.71x | 27.8x | 1.55x | 79.2x | 32.5x | 47.2x |
| three_way_merge | merge | 2.99s [2.98s–3.01s] | 2.29s [2.28s–2.30s] | 2.17s [2.17s–2.17s] | 5.79s [5.77s–5.83s] | 1.50s [1.49s–1.52s] | 4.14s [3.79s–4.37s] | 10.40s [10.34s–10.41s] | 6.09s [6.08s–6.19s] | 998.6 [998.4–1.01s] | 2.99x | 2.30x | 2.17x | 5.80x | 1.51x | 4.14x | 10.4x | 6.10x |
| log_pipeline | log-processing | 5.31s [5.29s–5.31s] | 1.29s [1.29s–1.29s] | 669.2 [667.2–670.4] | 13.48s [13.43s–13.57s] | 1.63s [1.52s–1.67s] | 11.95s [11.48s–38.83s] | 18.15s [18.09s–18.24s] | 9.36s [9.34s–9.38s] | 991.4 [982.8–992.9] | 5.35x | 1.30x | 0.67x | 13.6x | 1.65x | 12.1x | 18.3x | 9.44x |
| jq_mix | jq-breadth | 44.91s [44.55s–45.32s] | 44.55s [44.51s–44.56s] | 1.10s [1.08s–1.11s] | 10.99s [10.96s–11.09s] | 3.17s [2.99s–3.39s] | 16.50s [16.47s–16.65s] | --- [600.00s–600.00s] | --- [62.8–63.5] | 4.01s [4.00s–4.04s] | 11.2x | 11.1x | 0.27x | 2.74x | 0.79x | 4.12x | --- | --- |
| jq_records | jq-data | 4.67s [4.62s–4.71s] | 4.84s [4.78s–4.92s] | 961.9 [961.7–976.5] | 12.93s [12.92s–13.05s] | 2.22s [2.17s–2.40s] | 12.11s [12.05s–12.63s] | --- [600.00s–600.00s] | 10.80s [10.76s–10.81s] | 2.04s [2.03s–2.18s] | 2.29x | 2.37x | 0.47x | 6.33x | 1.09x | 5.93x | --- | 5.29x |
| jq_bf | jq-interpreter | 240.4 [239.4–242.0] | 185.9 [185.7–187.1] | 1.92s [1.91s–1.93s] | 27.58s [27.54s–27.90s] | 3.82s [3.74s–3.83s] | 43.40s [43.12s–43.62s] | --- [600.00s–600.00s] | 27.17s [27.12s–27.23s] | 2.96s [2.91s–3.05s] | 0.08x | 0.06x | 0.65x | 9.32x | 1.29x | 14.7x | --- | 9.18x |
| jq_tree | jq-paths | 2.19s [2.17s–2.20s] | 1.63s [1.62s–1.64s] | 4.05s [4.05s–4.11s] | 30.22s [30.11s–30.23s] | 5.71s [5.16s–6.72s] | 42.42s [40.27s–44.19s] | --- [600.00s–600.00s] | 46.73s [46.72s–46.93s] | 5.76s [5.75s–5.82s] | 0.38x | 0.28x | 0.70x | 5.25x | 0.99x | 7.36x | --- | 8.11x |

