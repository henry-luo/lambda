# JS MVP Benchmark Results: MVP_Result5

**Slow kernels, especially triangl** — [MVP §17](../../../vibe/jube/JS_MVP_Lmd.md).

[Series index and comparison](README.md) · [Raw JSON](MVP_Result5.json) · [Previous](MVP_Result4.md)

The five target kernels improve **1.771×**
in geometric mean paired speedup against the preceding numeric-tuning release.
All 42 workloads improve **1.093×**;
the other 37 improve **1.024×**.

Across 25 standard kernels, current MVP speedups are
**1.217× versus untyped Lambda**
and **2.376× versus Node**.
Above one means MVP is faster. Across all 42 workloads, MVP is
**1.639× versus Node**.

## Target kernels

| Workload | Before ms | MVP ms | Paired gain | Untyped Lambda ms | Node ms |
|---|---:|---:|---:|---:|---:|
| larceny/triangl | 619.608 | 158.933 | 3.90× | 182.761 | 68.185 |
| larceny/deriv | 18.997 | 8.653 | 2.20× | 21.451 | 3.845 |
| larceny/gcbench | 99.980 | 94.913 | 1.05× | 116.919 | 22.018 |
| larceny/pnpoly | 13.678 | 7.436 | 1.83× | 12.454 | 5.819 |
| beng/binarytrees | 4.099 | 3.887 | 1.05× | 4.928 | 2.080 |

`triangl` reduces the gap to Node from about 9× to **2.33×**; the remaining
gaps are **2.25×** for `deriv`, **4.31×** for `gcbench`, **1.28×** for `pnpoly`,
and **1.87×** for `binarytrees`. All five now beat their untyped Lambda ports.
Each target's 95% paired ratio interval excludes no change, with its
identical-control peer within 0.4% of parity.

The retained tradeoff is `quicksort`: **3.3% slower**, with a 95% ratio interval
of **2.3–4.0% slower**. The final candidate avoids a redundant numeric-consumer
conversion; the earlier screen showed roughly 5% regression. Optional local
snapshots still cross integer and double carriers. No workload has a paired
median slowdown above 5%. Other small changes should be read with their
control intervals.

## Measurement and provenance

- Frozen release binaries; pinned native MIR in both Lambda profiles;
  **15 alternating pairs**, an identical-control peer, and one discarded
  process per lane. Node **v22.13.0**. All **3,330 measured**
  and **222 discarded** outputs match their oracles.
- Control SHA-256: `a9aea060e1452656cf791d65f37ccf4ccabc5c19d412f9e331010d7d9c2e4b1f`.
- Candidate SHA-256: `172cf89c2a781b8dc15b291c335cfb8518fda54a8110c2e857a1e10ee409f1c5`.
- Compiler source SHA-256: `7d573679dfb84057cb65e7f507c1cb78b7a72f18bdbe4b6da3832f7aca47f7b1`.
- The binary, compiler source, benchmark sources, runner and Node identities
  were checked. Frozen binaries, sources, raw outputs and MIR are under
  `temp/mvp_lmd_slow_tuning_20261008/final/`. [JSON evidence](MVP_Result5.json)
  includes all commands, samples, hashes and paired bootstrap intervals.
- Times are medians of self-reported execution milliseconds. Gains are
  medians of paired ratios. Startup, initial parsing/JIT and teardown are
  excluded. Node tiering during the workload is included; these fresh-process
  measurements do not represent fully warmed V8 throughput.
- The [port/timing audit](MVP_Result4.md#port-and-timing-audit)
  still applies. Algorithms, inputs, iterations and oracles match; untyped
  Lambda retains native semantics (**S1.11**) and the corrected output `var`
  parameters (**S9.1.3**). FFT's zero-input oracle remains weak numeric evidence.
- Identical-control speedups: **0.982–1.058×**. Untyped Lambda
  control/candidate speedups: **0.994–1.014×**.
  One-minute load: **6.3 → 3.1**
  on 8 logical CPUs. Small differences need their intervals
  and controls; a short screen alone is not acceptance evidence.

## Remaining standard kernels

| Workload | Before ms | MVP ms | Paired gain | Untyped Lambda ms | Node ms |
|---|---:|---:|---:|---:|---:|
| r7rs/fib | 1.488 | 1.485 | 1.00× | 1.581 | 1.824 |
| r7rs/fibfp | 1.483 | 1.490 | 1.00× | 2.353 | 1.819 |
| r7rs/tak | 0.125 | 0.124 | 1.00× | 0.139 | 0.727 |
| r7rs/cpstak | 0.248 | 0.245 | 1.00× | 0.276 | 0.928 |
| r7rs/sum | 0.270 | 0.270 | 1.00× | 0.270 | 1.226 |
| r7rs/sumfp | 0.027 | 0.027 | 1.00× | 0.070 | 0.901 |
| r7rs/ack | 8.930 | 8.913 | 1.00× | 13.767 | 13.511 |
| larceny/diviter | 266.791 | 266.712 | 1.00× | 266.853 | 470.800 |
| larceny/divrec | 0.628 | 0.629 | 1.00× | 1.224 | 7.764 |
| kostya/collatz | 521.424 | 520.372 | 1.00× | 298.825 | 1429.530 |
| r7rs/nqueens | 0.941 | 0.947 | 0.99× | 1.088 | 1.786 |
| r7rs/fft | 0.173 | 0.170 | 1.02× | 0.331 | 1.594 |
| larceny/array1 | 0.662 | 0.663 | 1.00× | 0.817 | 1.810 |
| larceny/paraffins | 0.144 | 0.104 | 1.38× | 0.199 | 1.032 |
| larceny/quicksort | 1.660 | 1.715 | 0.97× | 1.089 | 1.827 |
| larceny/ray | 0.346 | 0.236 | 1.45× | 0.222 | 3.634 |
| kostya/primes | 3.406 | 3.440 | 0.99× | 2.303 | 4.333 |
| kostya/matmul | 5.942 | 5.910 | 1.00× | 5.711 | 15.610 |
| beng/fannkuch | 0.455 | 0.461 | 0.99× | 0.284 | 2.204 |
| beng/spectralnorm | 1.083 | 1.072 | 1.00× | 1.691 | 2.503 |

## Microbenchmarks

| Workload | Before ms | MVP ms | Paired gain | Untyped Lambda ms | Node ms |
|---|---:|---:|---:|---:|---:|
| js_mvp_lmd/integer_dense | 2.744 | 2.739 | 1.00× | 15.370 | 2.988 |
| js_mvp_lmd/integer_indirect | 1.569 | 1.580 | 0.99× | 5.426 | 1.136 |
| js_micro/lit | 0.135 | 0.135 | 1.00× | — | 3.685 |
| js_micro/named | 3.940 | 3.918 | 1.00× | — | 6.180 |
| js_micro/args_ctl | 3.513 | 3.206 | 1.09× | — | 2.232 |
| js_micro/args_fp | 3.523 | 3.253 | 1.08× | — | 2.250 |
| js_mvp_lmd/object_fields | 0.376 | 0.376 | 1.00× | — | 1.536 |
| js_mvp_lmd/object_growth | 3.529 | 3.514 | 1.01× | — | 3.807 |
| js_mvp_lmd/object_retype | 0.296 | 0.296 | 1.00× | — | 1.299 |
| js_mvp_lmd/object_retype_escaped | 6.745 | 6.594 | 1.02× | — | 1.291 |
| js_mvp_lmd/object_delete | 2.583 | 2.455 | 1.01× | — | 1.125 |
| js_mvp_lmd/map_lookup | 13.310 | 12.961 | 1.02× | — | 4.029 |
| js_mvp_lmd/map_iteration | 1.454 | 1.455 | 1.00× | — | 3.551 |
| js_mvp_lmd/numeric | 70.893 | 70.801 | 1.00× | — | 70.904 |
| js_mvp_lmd/dense_array | 57.959 | 57.604 | 1.01× | — | 11.500 |
| js_mvp_lmd/calls | 5.112 | 5.139 | 0.99× | — | 4.609 |
| js_mvp_lmd/strings | 2.418 | 2.434 | 1.00× | — | 0.257 |

## Compilation and process cost

| Workload | MIR instructions before → after | Process wall ms before → after |
|---|---:|---:|
| larceny/triangl | 5,006 → 4,309 | 656.803 → 190.393 |
| larceny/deriv | 6,453 → 6,045 | 54.247 → 42.294 |
| larceny/gcbench | 4,197 → 3,990 | 135.031 → 128.641 |
| larceny/pnpoly | 2,743 → 1,889 | 33.201 → 22.781 |
| beng/binarytrees | 4,247 → 4,040 | 29.063 → 27.888 |

MIR instruction counts are static compiler output, not native code size.
Process wall time includes startup, compilation, execution and teardown; it is
reported separately from the self-reported kernel times above. This run does
not isolate parsing/JIT latency or fully warmed throughput.

## Implementation and validation

Integer element/presence carriers, direct condition branches, immutable
container facts, local factory calls, and guarded nullable object stores reuse
Lambda's existing emitters and shape transitions (**D2.2.5**, **D2.4.3**,
**D3.4.3**, **D5.3.4**, **D8.2.6**). Two disclosed compiler helpers were added;
there are no new runtime helpers or imports. No benchmark source was tuned.

See the [implementation and validation record](../../../vibe/impl/JS_MVP_Lmd_Slow_Tuning.md)
for the exact gates and remaining limits. MVP tests pass **37/37** normally
and with forced GC/poisoning; Lambda/input passes **6,362/6,362**; full-JS
Test262 passes **40,261/40,261**, with zero retries, unstable cases or
regressions. Test262 is a shared-host gate, not MVP feature coverage.
Recursive allocation, more complete control-flow bounds and optional integer
local snapshots remain follow-up work.

```sh
python3 temp/mvp_lmd_slow_tuning_20261008/paired.py \
  --candidate temp/mvp_lmd_slow_tuning_20261008/final/candidate.exe \
  --control temp/mvp_lmd_slow_tuning_20261008/final/control.exe \
  --output temp/mvp_lmd_slow_tuning_20261008/replay \
  --runs 15 --references --baseline-references
```
