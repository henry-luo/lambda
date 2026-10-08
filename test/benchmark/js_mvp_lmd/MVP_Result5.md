# JS MVP Benchmark Results: MVP_Result5

**Slow kernels; recursive allocation and optional integer locals** — [MVP §17](../../../vibe/jube/JS_MVP_Lmd.md#17-slow-kernel-tuning).

[Series index and comparison](README.md) · [Raw JSON](MVP_Result5.json) · [Previous phase](MVP_Result4.md)

Updated **2026-10-08T10:36:54.962973+08:00**. This follow-up compares the new release against the
exact preceding Result5 candidate. All 42 workloads improve **1.025×**
geometrically; the six tracked kernels improve **1.078×**.

Across 25 standard kernels, MVP is **1.247× faster than untyped Lambda**
and **2.444× faster than Node** geometrically.
Across all 42 workloads, its speedup over Node is **1.682×**.
Above one means MVP is faster.

## Target kernels

| Workload | Previous Result5 ms | MVP ms | Paired gain | Untyped Lambda ms | Node ms |
|---|---:|---:|---:|---:|---:|
| larceny/gcbench | 95.970 | 91.792 | 1.046× | 116.486 | 21.876 |
| beng/binarytrees | 3.981 | 3.753 | 1.061× | 4.845 | 2.081 |
| larceny/quicksort | 1.707 | 1.298 | 1.316× | 1.080 | 1.835 |
| larceny/triangl | 157.987 | 155.499 | 1.016× | 181.827 | 67.895 |
| larceny/deriv | 8.789 | 8.465 | 1.040× | 21.570 | 3.822 |
| larceny/pnpoly | 7.425 | 7.332 | 1.017× | 12.517 | 5.857 |

Quicksort's earlier 3.3% regression is reversed: it improves
**1.316×** against that exact binary.
The escaped object-retyping microbenchmark is **2.2% slower** in a separate
30-pair confirmation (90 measured and 3 discarded matching outputs). Its fresh
control peer is near parity; this is a retained small regression. The full
42-row table keeps the original 15-pair medians; follow-up samples are separate
in the JSON. No full-run row has a paired median slowdown above 5%.
All six tracked kernels' 95% candidate/control ratio intervals are below one.

Tree allocation gains remain smaller than the conversion gain. Object allocation,
collection and recursive call overhead still remain; no region allocator was added.

## Measurement and provenance

- **15 alternating release pairs**, identical-control peer, one discarded
  process per lane; pinned native MIR for MVP and Lambda. Node **v22.13.0**.
  All **3,330 measured and 222 discarded outputs** match their oracles.
- Values are medians of self-reported execution milliseconds; gains are medians
  of paired ratios. Startup, initial parsing/JIT and teardown are excluded.
  Node tiering within the workload remains included; these are fresh-process
  workload times, not fully warmed throughput.
- The [port/timing audit](MVP_Result4.md#port-and-timing-audit) still applies.
  Workloads and oracles are unchanged. Untyped Lambda retains native semantics
  (**S1.11**, **S9.1.3**); FFT's zero-input oracle remains weak numeric evidence.
- Control SHA-256: `172cf89c2a781b8dc15b291c335cfb8518fda54a8110c2e857a1e10ee409f1c5`.
- Candidate SHA-256: `190d5f602876b1023ba83e9d25eee603af5fd938ebf64c3aaaefe1ef8ff38cf7`.
- Compiler SHA-256: `88c5dfdc937f124f7555fababe55ae5df4bafe675854df9b733ceedd8319e832`.
- Frozen binaries, compiler sources, runner, raw outputs and MIR:
  `temp/mvp_lmd_recursive_20261008/final/`. All source, binary, runner and Node
  identities were checked. JSON includes samples and bootstrap intervals.
- Identical-control speedups: **0.987–1.028×**; untyped Lambda
  control/candidate speedups: **0.986–1.014×**.
  One-minute host load: **5.4 → 3.5** on 8 logical CPUs.
  Read small changes alongside their control intervals.

## Remaining standard kernels

| Workload | Previous Result5 ms | MVP ms | Paired gain | Untyped Lambda ms | Node ms |
|---|---:|---:|---:|---:|---:|
| r7rs/fib | 1.497 | 1.501 | 0.997× | 1.591 | 1.816 |
| r7rs/fibfp | 1.491 | 1.500 | 0.993× | 2.370 | 1.858 |
| r7rs/tak | 0.124 | 0.123 | 1.000× | 0.137 | 0.740 |
| r7rs/cpstak | 0.243 | 0.245 | 0.996× | 0.271 | 0.931 |
| r7rs/sum | 0.270 | 0.270 | 1.000× | 0.270 | 1.247 |
| r7rs/sumfp | 0.027 | 0.027 | 1.000× | 0.070 | 0.893 |
| r7rs/ack | 8.852 | 8.884 | 0.998× | 14.181 | 13.483 |
| larceny/diviter | 266.607 | 266.126 | 1.000× | 266.183 | 470.492 |
| larceny/divrec | 0.629 | 0.628 | 1.002× | 1.223 | 7.738 |
| kostya/collatz | 519.442 | 519.727 | 1.000× | 297.863 | 1413.390 |
| r7rs/nqueens | 0.967 | 0.925 | 1.047× | 1.111 | 1.815 |
| r7rs/fft | 0.169 | 0.168 | 1.006× | 0.324 | 1.593 |
| larceny/array1 | 0.663 | 0.663 | 0.998× | 0.817 | 1.815 |
| larceny/paraffins | 0.105 | 0.103 | 1.010× | 0.201 | 1.031 |
| larceny/ray | 0.237 | 0.237 | 1.000× | 0.221 | 3.660 |
| kostya/primes | 3.459 | 3.382 | 1.022× | 2.294 | 4.369 |
| kostya/matmul | 5.887 | 5.853 | 1.002× | 5.702 | 15.489 |
| beng/fannkuch | 0.465 | 0.380 | 1.210× | 0.277 | 2.172 |
| beng/spectralnorm | 1.078 | 1.080 | 1.002× | 1.692 | 2.551 |

## Microbenchmarks

| Workload | Previous Result5 ms | MVP ms | Paired gain | Untyped Lambda ms | Node ms |
|---|---:|---:|---:|---:|---:|
| js_mvp_lmd/integer_dense | 2.714 | 2.741 | 0.996× | 15.301 | 2.933 |
| js_mvp_lmd/integer_indirect | 1.565 | 1.570 | 0.997× | 5.268 | 1.148 |
| js_micro/lit | 0.134 | 0.134 | 1.000× | — | 3.675 |
| js_micro/named | 3.946 | 3.898 | 0.999× | — | 6.172 |
| js_micro/args_ctl | 3.235 | 2.699 | 1.199× | — | 2.239 |
| js_micro/args_fp | 3.240 | 2.692 | 1.200× | — | 2.248 |
| js_mvp_lmd/object_fields | 0.376 | 0.376 | 1.000× | — | 1.557 |
| js_mvp_lmd/object_growth | 3.531 | 3.540 | 0.996× | — | 3.748 |
| js_mvp_lmd/object_retype | 0.296 | 0.296 | 1.000× | — | 1.297 |
| js_mvp_lmd/object_retype_escaped | 6.576 | 6.699 | 0.977× | — | 1.300 |
| js_mvp_lmd/object_delete | 2.478 | 2.373 | 0.985× | — | 1.113 |
| js_mvp_lmd/map_lookup | 12.934 | 13.286 | 0.984× | — | 4.122 |
| js_mvp_lmd/map_iteration | 1.458 | 1.454 | 1.003× | — | 3.568 |
| js_mvp_lmd/numeric | 70.668 | 70.723 | 1.000× | — | 70.633 |
| js_mvp_lmd/dense_array | 57.899 | 57.225 | 1.011× | — | 11.500 |
| js_mvp_lmd/calls | 5.059 | 5.046 | 0.996× | — | 4.630 |
| js_mvp_lmd/strings | 2.414 | 2.416 | 1.001× | — | 0.258 |

## Compilation and process cost

| Workload | MIR instructions before → after | Process wall ms before → after |
|---|---:|---:|
| larceny/gcbench | 3,990 → 3,793 | 130.657 → 125.451 |
| beng/binarytrees | 4,040 → 3,864 | 28.100 → 27.562 |
| larceny/quicksort | 841 → 605 | 12.217 → 10.928 |
| larceny/triangl | 4,309 → 4,162 | 188.689 → 185.208 |
| larceny/deriv | 6,045 → 5,587 | 42.918 → 39.963 |
| larceny/pnpoly | 1,889 → 1,751 | 22.632 → 21.949 |

MIR counts describe static compiler output, not native code size. Process wall
time includes compilation and teardown and is separate from workload timing.
Quicksort's static I2D/D2I counts fall from
**18/19** to
**12/11**.

## Implementation and validation

Optional integer locals retain an i64 payload and a presence bit; present-value
ranges stay separate from ordinary arithmetic ranges. Integer comparisons and
stores preserve undefined/NaN behavior without round trips through double.
Closed nonnumeric factories use Lambda's existing plain-Item return ABI;
nonnumeric locals skip scalar adoption. Fixed-key literal values are evaluated
in order and precisely rooted before allocating their unobservable parent.
Existing shape guards and transition fallback remain (**D2.2.5**, **D3.4.3v5**,
**D5.2.1v3**, **D5.3.4**).

No new compiler/runtime helper or import was added. Only the MVP compiler
changed; shared runtime and benchmark sources are unchanged. Release build
passes with zero errors and 28 existing warnings. Benchmark output validation
is current; **unit, forced-GC and baseline suites were not rerun for this
follow-up**. The preceding round's 37/37 MVP, 6,362/6,362 Lambda/input and
40,261/40,261 Test262 results apply to its earlier binary only.

See the [implementation record](../../../vibe/impl/JS_MVP_Lmd_Slow_Tuning.md).

## Phase history retained

| Workload | Before phase 17 ms | Initial Result5 ms | Current Result5 ms |
|---|---:|---:|---:|
| larceny/gcbench | 99.980 | 94.913 | 91.792 |
| beng/binarytrees | 4.099 | 3.887 | 3.753 |
| larceny/quicksort | 1.660 | 1.715 | 1.298 |
| larceny/triangl | 619.608 | 158.933 | 155.499 |
| larceny/deriv | 18.997 | 8.653 | 8.465 |
| larceny/pnpoly | 13.678 | 7.436 | 7.332 |

These columns were captured sequentially; use the individual rounds' paired
controls for attribution. The complete initial Result5 JSON and report are
embedded as `previous_round` and `previous_round_report` in the current JSON.
Original JSON SHA-256: `b3db70ded08c5c857bfd7d133dd4273f71b3f198ad1b3bdeee7bd6b2f1b4d8b5`.
Its exact local backup remains `temp/mvp_lmd_recursive_20261008/original_Result5.json`.
The initial round's all-42 paired gain was **1.093×**; its five original target
kernels improved **1.771×** geometrically against the preceding numeric release.

```sh
python3 temp/mvp_lmd_recursive_20261008/paired.py \
  --candidate temp/mvp_lmd_recursive_20261008/final/candidate.exe \
  --control temp/mvp_lmd_recursive_20261008/final/control.exe \
  --output temp/mvp_lmd_recursive_20261008/replay \
  --runs 15 --references --baseline-references
```
