# JS MVP Benchmark Results: MVP_Result4

**Numeric libraries and array facts** — [MVP §§15–16](../../../vibe/jube/JS_MVP_Lmd.md).

[Series index and comparison](README.md) · [Raw JSON](MVP_Result4.json) · [Previous](MVP_Result3.md) · [Next](MVP_Result5.md)

The twelve newly supported kernels improve **2.396×**
in geometric mean paired speedup versus the frozen numeric-phase release.
Their current MVP speedups are **0.904× versus untyped Lambda**
and **2.117× versus Node**; above one means MVP is faster.
Across all 25 standard kernels, the corresponding comparisons are
**1.052×** and
**2.069×**.

## Measurement and provenance

- Frozen release binaries; pinned native MIR in both Lambda profiles;
  **15 alternating pairs**, an identical-control peer, and one discarded
  process per lane. Node is **v22.13.0**. All **3,330
  measured** and **222 discarded** output checks match.
- Control SHA-256: `e461ea033e59a12b236c1a2beadb8672589586b4e09a58531f912aa2bdf1524b`.
- Candidate SHA-256: `a9aea060e1452656cf791d65f37ccf4ccabc5c19d412f9e331010d7d9c2e4b1f`.
- Compiler source SHA-256: `13f48228d35f47f85f5c9d50683e2a942226607275c8d9eeddbdcb0e74f84c19`.
- Binary, compiler source, benchmark sources, runner and Node identity were
  unchanged during this final run. Raw outputs, frozen binaries, MIR and
  the replay runner are under `temp/mvp_lmd_numeric_tuning_20261008/final/`.
  [JSON evidence](MVP_Result4.json) includes commands, hashes, samples, paired
  bootstrap intervals and MIR instruction counts.
- Time columns are independent medians of **self-reported execution ms**;
  speedups use paired median ratios. Startup, initial parsing/JIT and teardown
  are excluded. Node tiering during the workload is included; fresh processes
  do not measure fully warmed V8 throughput. MVP times its generated entry;
  Node brackets the same adapted kernel.
- The [preceding port/timing audit](MVP_Result4.md#port-and-timing-audit)
  applies unchanged: algorithms, inputs, iterations and oracles match. Lambda
  retains its native semantics (**S1.11**); temporary `matmul`/`spectralnorm`
  ports use output `var` parameters (**S9.1.3**). FFT retains the canonical
  zero-input checksum, so this benchmark alone is a weak numeric oracle.
- Identical-control paired speedups span **0.985–1.021×**.
  The 27 untyped Lambda control/candidate comparisons span
  **0.982–1.022×**. One-minute load was
  **8.1 → 3.8** on 8 logical CPUs.
  Small differences remain noise-sensitive; row intervals are in the JSON.

## Twelve newly supported kernels

| Workload | Before ms | After ms | Paired speedup | Untyped Lambda ms | Node ms |
|---|---:|---:|---:|---:|---:|
| r7rs/nqueens | 1.149 | 0.990 | 1.15× | 1.127 | 1.839 |
| r7rs/fft | 0.535 | 0.176 | 3.03× | 0.332 | 1.614 |
| larceny/array1 | 3.531 | 0.666 | 5.31× | 0.817 | 1.821 |
| larceny/triangl | 872.795 | 620.267 | 1.41× | 183.635 | 68.553 |
| larceny/paraffins | 0.264 | 0.149 | 1.78× | 0.205 | 1.037 |
| larceny/quicksort | 3.258 | 1.685 | 1.94× | 1.095 | 1.858 |
| larceny/ray | 0.769 | 0.344 | 2.23× | 0.220 | 3.670 |
| kostya/primes | 9.451 | 3.400 | 2.78× | 2.300 | 4.435 |
| kostya/matmul | 45.321 | 5.938 | 7.64× | 5.726 | 15.566 |
| beng/fannkuch | 1.258 | 0.457 | 2.77× | 0.281 | 2.168 |
| beng/spectralnorm | 3.575 | 1.170 | 3.01× | 1.695 | 2.558 |
| beng/binarytrees | 4.211 | 4.212 | 1.00× | 4.966 | 2.123 |

## Earlier standard kernels

| Workload | Before ms | After ms | Paired speedup | Untyped Lambda ms | Node ms |
|---|---:|---:|---:|---:|---:|
| r7rs/fib | 1.473 | 1.485 | 0.98× | 1.584 | 1.838 |
| r7rs/fibfp | 1.493 | 1.497 | 1.00× | 2.342 | 1.847 |
| r7rs/tak | 0.125 | 0.125 | 1.00× | 0.136 | 0.739 |
| r7rs/cpstak | 0.244 | 0.246 | 1.00× | 0.282 | 0.943 |
| r7rs/sum | 0.270 | 0.276 | 1.00× | 0.270 | 1.253 |
| r7rs/sumfp | 0.027 | 0.027 | 1.00× | 0.070 | 0.930 |
| r7rs/ack | 8.951 | 8.909 | 1.01× | 14.857 | 13.652 |
| larceny/diviter | 266.655 | 266.540 | 1.00× | 266.996 | 469.885 |
| larceny/divrec | 0.628 | 0.629 | 1.00× | 1.225 | 7.856 |
| kostya/collatz | 522.710 | 523.517 | 1.00× | 299.281 | 1419.128 |
| larceny/pnpoly | 17.235 | 13.808 | 1.25× | 12.736 | 5.904 |
| larceny/deriv | 19.184 | 19.433 | 0.99× | 21.726 | 3.859 |
| larceny/gcbench | 100.949 | 100.224 | 1.00× | 117.384 | 22.188 |

## Microbenchmarks

| Workload | Before ms | After ms | Paired speedup | Untyped Lambda ms | Node ms |
|---|---:|---:|---:|---:|---:|
| js_mvp_lmd/integer_dense | 3.311 | 2.787 | 1.19× | 15.454 | 3.008 |
| js_mvp_lmd/integer_indirect | 1.574 | 1.577 | 1.00× | 5.430 | 1.137 |
| js_micro/lit | 0.135 | 0.138 | 0.99× | — | 3.691 |
| js_micro/named | 3.952 | 3.945 | 1.00× | — | 6.181 |
| js_micro/args_ctl | 3.532 | 3.519 | 1.00× | — | 2.247 |
| js_micro/args_fp | 3.522 | 3.519 | 1.00× | — | 2.251 |
| js_mvp_lmd/object_fields | 0.376 | 0.376 | 1.00× | — | 1.554 |
| js_mvp_lmd/object_growth | 3.551 | 3.608 | 0.99× | — | 3.832 |
| js_mvp_lmd/object_retype | 0.296 | 0.296 | 1.00× | — | 1.305 |
| js_mvp_lmd/object_retype_escaped | 6.757 | 6.781 | 0.99× | — | 1.325 |
| js_mvp_lmd/object_delete | 2.555 | 2.564 | 1.00× | — | 1.137 |
| js_mvp_lmd/map_lookup | 13.125 | 13.352 | 0.99× | — | 4.074 |
| js_mvp_lmd/map_iteration | 1.461 | 1.461 | 1.00× | — | 3.609 |
| js_mvp_lmd/numeric | 71.176 | 71.193 | 1.00× | — | 71.166 |
| js_mvp_lmd/dense_array | 58.519 | 58.281 | 1.01× | — | 11.644 |
| js_mvp_lmd/calls | 5.087 | 5.086 | 0.99× | — | 4.724 |
| js_mvp_lmd/strings | 2.439 | 2.420 | 1.00× | — | 0.264 |

The all-42 tuning geometric mean is **1.294×**;
the prior-13 standard control group is **1.016×**.
These populations are kept separate from the twelve-kernel headline.

## Implementation and limits

The compiler preserves typed lanes/extents, native element results and stores,
number-or-undefined snapshots, nested induction ranges, affine bounds, and
numeric contents of unchanged ordinary literals. It reuses Lambda's physical
storage and scalar-ownership emitters (**D2.2.5**, **D2.4.3**, **D5.3.4**,
**D8.2.6**) without runtime feedback (**D8.4.1v2**). Three disclosed compiler
utilities were added; no native runtime helper or full-JS dependency was added.
See the [implementation record](../../../vibe/impl/JS_MVP_Lmd_Numeric_Tuning.md).

Unproved bounds still use checks; no raw data pointer is retained across a
call or GC boundary. Allocation/recursive-call tuning and more complex
path-sensitive bounds remain follow-ups. Numeric edge/GC and complete
Lambda/Test262 gates remain pending; this turn added and ran no new tests or
baseline suites. Benchmark output checks are not full semantic acceptance.

```sh
python3 temp/mvp_lmd_numeric_tuning_20261008/paired.py \
  --candidate temp/mvp_lmd_numeric_tuning_20261008/final/candidate.exe \
  --control temp/mvp_lmd_numeric_tuning_20261008/final/control.exe \
  --output temp/mvp_lmd_numeric_tuning_20261008/replay \
  --runs 15 --references --baseline-references
```

## Port and timing audit

- Algorithms, inputs, seeds, sizes and repetitions are preserved. Host
  `performance`, `console`, `process` and CLI argument plumbing is adapted;
  BENG uses the canonical default sizes N=10/7/100. Result checks are retained
  or strengthened, including `ray` hits=1392, `triangl` solutions=29760,
  `matmul` checksum=-29562, and both `fannkuch` outputs.
- Untyped Lambda uses the unannotated `.ls` ports. The nominal untyped
  `matmul.ls` contains four typed parameters: the temporary comparison port
  removes them and declares the output `c` as a `var` parameter.
  `spectralnorm` similarly declares its three output parameters `var`.
  Plain parameters snapshot under **S9.1.3**; without these declarations,
  the ports produce checksum zero and NaN respectively. The corrected ports
  match the reference results. Initial failed preflight evidence is retained.
- Lambda retains its native numeric and container semantics (**S1.11**,
  **D2.2.5**): sieve flags are bool arrays versus JS Uint8Array; `paraffins`
  uses `div` versus JS truncation; `fft` halves with division versus JS shifts;
  `spectralnorm` accumulates into a local scalar versus JS element updates;
  Collatz halves with `shr` versus JS division. These are port comparisons.
- `gcbench` and `binarytrees` build matching report strings, including final
  long-lived traversals, and print after timing. `pnpoly` retains one timed
  Lambda report print. Lambda `fannkuch`/`spectralnorm` retain small timed
  formatting and output operations. Quicksort verification is included in
  each adapted kernel; FFT retains the canonical zero-input/result check.
- JS source bodies are identical between MVP and Node. Every adapted source,
  original source hash, frozen executable, MIR artifact and raw output is
  under `temp/mvp_lmd_threeway_20261008/measured/`; the runner and locked
  oracles are in its parent directory. This audit was recorded before numeric tuning; its source and timing-boundary qualifications also apply to this paired result.
- This run validates the measured outputs. Numeric edge tests, forced GC,
  and the complete Lambda/Test262 acceptance gates remain pending for §15
  of [JS_MVP_Lmd](../../../vibe/jube/JS_MVP_Lmd.md).
