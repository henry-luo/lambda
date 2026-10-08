# JS MVP Benchmark Results: MVP_Result7

**Basic classes and class workload tuning** — [MVP §21](../../../vibe/jube/JS_MVP_Lmd.md#21-classes-and-inheritance-aligned-with-lambda).

[Series index and comparison](README.md) · [Raw JSON](MVP_Result7.json) · [Previous phase](MVP_Result6.md)

## Summary

- **54 workloads pass** their benchmark output checks: 37 standard benchmarks and 17 microbenchmarks.
- Sieve, permute, queens, towers and list improve **6.45×, 3.96×, 3.55×, 2.66× and 2.03×** against the fresh pre-tuning release.
- The 48 prior workloads have **3.23% lower geometric-mean execution time** against that same control.
- Separate 60-pair comparisons against the exact Result6 release show **49.53% lower deletion time** and **36.44% lower Map lookup time**. JSON generation is neutral.
- These results were recorded from the accepted captures; publication did not rerun benchmarks or tests.

## Measurement

- Captured **2026-10-08 18:03:55–18:05:34 UTC+08**; confirmations finished at **18:07:53**.
- Platform: `macOS-26.5.2-arm64-arm-64bit-Mach-O`; release binaries, pinned native MIR.
- Each headline cell is the median of **15 fresh processes**, alternating lane order, with one discarded process per lane and an identical-control peer. Peer samples and MIR evidence are retained in JSON.
- Times are **self-reported workload milliseconds**; startup and initial parsing/compilation are excluded. `Control / MVP` divides the two medians; a larger value means faster MVP. Per-round paired gains and bootstrap intervals are also retained in JSON.
- The main control already includes class support. It is a fresh pre-tuning build, **not the Result6 binary**. The Result6 binary is used only in the separately labeled regression confirmation.
- Only the six new AWFY rows have fresh untyped Lambda references. Node and full LambdaJS were not measured in this round; earlier cross-engine comparisons remain in [Result6](MVP_Result6.md).

```text
JS_EXEC_BACKEND=mir
JS_EXECUTION_BACKEND=mir
JS_MIR_INTERP=0
LAMBDA_JS_LARGE_INTERP=0
LAMBDA_EXEC_BACKEND=jit
LAMBDA_TIER=jit
```

## Six newly enabled class workloads

| Workload | Control ms | MVP ms | Control / MVP | Untyped Lambda ms | MVP / Lambda |
|---|---:|---:|---:|---:|---:|
| awfy/sieve | 0.600 | 0.093 | 6.45× | 0.027 | 3.44× |
| awfy/permute | 3.214 | 0.812 | 3.96× | 0.281 | 2.89× |
| awfy/queens | 1.563 | 0.440 | 3.55× | 0.178 | 2.47× |
| awfy/towers | 4.510 | 1.697 | 2.66× | 0.494 | 3.44× |
| awfy/list | 0.816 | 0.402 | 2.03× | 0.546 | 0.74× |
| awfy/mandelbrot | 32.359 | 32.237 | 1.00× | 37.846 | 0.85× |

MVP includes class initialization and setup; Lambda clocks the existing
`benchmark()` interval. The ports retain their respective language semantics
(**S1.11**). These compare the existing language ports:

- **Towers:** both perform 8,191 moves. JS uses linked disk instances and disk-order checks, initializing 14 objects and moving 13 disks; Lambda uses array stacks containing 13 integer disks.
- **Queens:** both solve eight queens ten times; Lambda additionally checks the completed board.
- Other sizes are unchanged: sieve 5,000; permute size 6 with count 8,660; list lengths 15/10/6 with result 10; Mandelbrot 500² with 50 iterations and result 191.

List takes 26.4% less time than untyped Lambda; Mandelbrot takes 14.8% less.
Sieve, permute, queens and towers remain 2.47–3.44× slower than their Lambda ports.

## Prior standard workloads (31)

| Workload | Control ms | MVP ms | Control / MVP |
|---|---:|---:|---:|
| beng/binarytrees | 3.805 | 3.826 | 0.99× |
| beng/fannkuch | 0.262 | 0.262 | 1.00× |
| beng/spectralnorm | 1.073 | 1.174 | 0.91× |
| kostya/base64 | 11.931 | 11.879 | 1.00× |
| kostya/brainfuck | 42.165 | 41.607 | 1.01× |
| kostya/collatz | 199.516 | 199.548 | 1.00× |
| kostya/json_gen | 5.634 | 5.697 | 0.99× |
| kostya/levenshtein | 1.539 | 1.561 | 0.99× |
| kostya/matmul | 5.892 | 5.891 | 1.00× |
| kostya/primes | 2.063 | 2.044 | 1.01× |
| larceny/array1 | 0.310 | 0.310 | 1.00× |
| larceny/deriv | 8.409 | 8.518 | 0.99× |
| larceny/diviter | 265.694 | 265.662 | 1.00× |
| larceny/divrec | 0.619 | 0.618 | 1.00× |
| larceny/gcbench | 89.885 | 89.915 | 1.00× |
| larceny/paraffins | 0.102 | 0.102 | 1.00× |
| larceny/pnpoly | 7.363 | 7.394 | 1.00× |
| larceny/puzzle | 2.486 | 2.482 | 1.00× |
| larceny/quicksort | 1.298 | 1.299 | 1.00× |
| larceny/ray | 0.241 | 0.239 | 1.01× |
| larceny/triangl | 154.659 | 154.755 | 1.00× |
| r7rs/ack | 8.896 | 8.906 | 1.00× |
| r7rs/cpstak | 0.245 | 0.247 | 0.99× |
| r7rs/fft | 0.165 | 0.164 | 1.01× |
| r7rs/fib | 1.474 | 1.482 | 0.99× |
| r7rs/fibfp | 1.489 | 1.505 | 0.99× |
| r7rs/mbrot | 0.858 | 0.482 | 1.78× |
| r7rs/nqueens | 0.929 | 0.930 | 1.00× |
| r7rs/sum | 0.270 | 0.270 | 1.00× |
| r7rs/sumfp | 0.027 | 0.027 | 1.00× |
| r7rs/tak | 0.123 | 0.123 | 1.00× |

## Microbenchmarks (17)

| Workload | Control ms | MVP ms | Control / MVP |
|---|---:|---:|---:|
| js_micro/args_ctl | 2.695 | 2.704 | 1.00× |
| js_micro/args_fp | 2.703 | 2.700 | 1.00× |
| js_micro/lit | 0.133 | 0.133 | 1.00× |
| js_micro/named | 3.947 | 3.944 | 1.00× |
| js_mvp_lmd/calls | 5.076 | 5.012 | 1.01× |
| js_mvp_lmd/dense_array | 57.347 | 57.570 | 1.00× |
| js_mvp_lmd/integer_dense | 2.704 | 2.708 | 1.00× |
| js_mvp_lmd/integer_indirect | 1.575 | 1.575 | 1.00× |
| js_mvp_lmd/map_iteration | 1.458 | 1.454 | 1.00× |
| js_mvp_lmd/map_lookup | 13.055 | 8.386 | 1.56× |
| js_mvp_lmd/numeric | 70.716 | 70.688 | 1.00× |
| js_mvp_lmd/object_delete | 2.299 | 1.188 | 1.94× |
| js_mvp_lmd/object_fields | 0.376 | 0.376 | 1.00× |
| js_mvp_lmd/object_growth | 2.907 | 2.891 | 1.01× |
| js_mvp_lmd/object_retype | 0.296 | 0.296 | 1.00× |
| js_mvp_lmd/object_retype_escaped | 5.776 | 5.869 | 0.98× |
| js_mvp_lmd/strings | 0.105 | 0.103 | 1.02× |

## Separate regression confirmations

Each row below uses **60 alternating pairs plus an identical-control peer**.
Intervals are two-sided 95% paired-bootstrap intervals for the ratio of medians
(MVP / control). These captures do not replace any 15-round headline cells above.

### Against the original pre-class Result6 release

| Workload | Result6 control ms | MVP ms | Time change | MVP / control 95% interval |
|---|---:|---:|---:|---:|
| js_mvp_lmd/object_retype_escaped | 6.7980 | 5.8015 | -14.66% | 0.8462–0.8590 |
| js_mvp_lmd/object_delete | 2.3560 | 1.1890 | -49.53% | 0.4853–0.5192 |
| js_mvp_lmd/map_lookup | 13.0985 | 8.3255 | -36.44% | 0.6300–0.6385 |
| kostya/json_gen | 5.7110 | 5.6990 | -0.21% | 0.9927–1.0092 |

### Against the fresh pre-tuning control

| Workload | Control ms | MVP ms | MVP / control 95% interval |
|---|---:|---:|---:|
| larceny/gcbench | 91.8405 | 91.8535 | 0.9966–1.0040 |
| js_micro/named | 3.9435 | 3.9425 | 0.9982–1.0008 |
| js_mvp_lmd/object_retype_escaped | 5.7950 | 5.8165 | 0.9931–1.0107 |
| js_mvp_lmd/map_iteration | 1.4570 | 1.4550 | 0.9973–1.0000 |
| beng/binarytrees | 3.8020 | 3.7995 | 0.9911–1.0034 |
| kostya/json_gen | 5.7420 | 5.7090 | 0.9887–0.9977 |
| r7rs/fibfp | 1.4895 | 1.4780 | 0.9814–1.0060 |
| larceny/deriv | 8.6175 | 8.5805 | 0.9877–1.0034 |
| beng/spectralnorm | 1.1145 | 1.0760 | 0.9123–1.0805 |
| kostya/levenshtein | 1.5855 | 1.5830 | 0.9950–1.0096 |

No fresh-control confirmation interval lies wholly above one. This does not
establish exact parity: small effects remain unresolved, and spectralnorm is
noisy even in the identical-control lane (candidate interval 0.912–1.081).

## Validation and provenance

Across the headline and confirmation captures, **5,040 measured and 210 discarded
outputs matched**. The release build succeeded with zero errors and 1,532
warnings. Unit, forced-GC, Lambda baseline and Test262 suites were **not run in
this tuning round**; earlier Result6 gates do not validate this binary.

| Binary | SHA-256 |
|---|---|
| Tuned MVP | `0b6688bc8b5a6742207b23d3b19e6ec42dedbc4ad4fb45227f2ba940efafb037` |
| Fresh pre-tuning control | `eee3d7c85c67bdb79674cb5df281aa8148dd1187b66bf0eaddddb9f8843de128` |
| Original pre-class Result6 control | `da89d81a9fa893516ede62345771b82a3709fd45faed23c0c2bc7dc37f7220ca` |

Fresh control source revision: `670733b89b6f5dc1067184bbb6415dc46b71f09b`. The tuned binary applies
the source patch embedded in this result's JSON. The artifact filename
`final6.exe` denotes the sixth tuning candidate; its published snapshot is
**MVP_Result7**.

The [raw JSON](MVP_Result7.json) preserves all five accepted captures, normalized
54-row results, samples, source hashes, commands, MIR hashes, exact binary
identities, and the tuning source patch. Original evidence remains under
`temp/mvp_class_tuning_20261008/`:

| Capture | Workloads | Rounds | Measured / discarded outputs |
|---|---:|---:|---:|
| `final6-48/comparison.json` | 48 | 15 | 2,160 / 144 |
| `final6-classes/comparison.json` | 6 | 15 | 360 / 24 |
| `confirm6-old/comparison.json` | 4 | 60 | 720 / 12 |
| `confirm6-fresh/comparison.json` | 6 | 60 | 1,080 / 18 |
| `confirm6-noise/comparison.json` | 4 | 60 | 720 / 12 |

`final-provenance.json`, `final-source.patch`, and `final6-build.log` retain the
original publication evidence. Intermediate candidates and the build-overlapped
`allocation-ablation` experiment are excluded from this result.

Implementation and remaining validation details:
[class tuning record](../../../vibe/impl/JS_MVP_Lmd_Classes.md#class-and-regression-tuning-2026-10-08).
