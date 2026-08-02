# Inline-cache POC (historical baseline)

**Date:** 2026-08-02  
**Commit:** `a67ed09c6cc6deb5107a0fdad4cd0260f61550d9`  
**Protocol:** 56 canonical benchmarks across R7RS, AWFY, BENG, KOSTYA,
LARCENY, and JetStream; median of three execution-only runs; 120 s per run.

## Build switch

`LAMBDA_INLINE_CACHE` is an environment-overridable LambdaJS compile
definition with a default of `1`.

```sh
make release                         # LambdaJS IC-on release
LAMBDA_INLINE_CACHE=0 make release   # LambdaJS IC-off release
```

Use the clean `release` target when changing this definition: an incremental
Premake build does not reliably invalidate objects when a generated define
changes.

This matrix predates the permanent removal of the Lambda MIR member inline
cache. At capture time, `0` omitted both Lambda `fn_member_ic` and LambdaJS
named load/store and constructor-shape caches. Lambda MIR now always emits the
generic `fn_member` path; the flag affects LambdaJS cache lowering only.
Type-specialized direct slot access that does not use a cache remains enabled.

## Binaries and raw data

| Variant | Archive | SHA-256 | Raw results |
|---|---|---|---|
| IC on | `test/benchmark/exe/lambda-ic-current.exe` | `e633f80394ca0f6dda3678dc9019cc43fccb550a5d32ffdf2603b1d1d7121848` | [benchmark_results_ic_current.json](benchmark_results_ic_current.json) |
| IC off | `test/benchmark/exe/lambda-no-ic.exe` | `2b5e1a21da928082ba507e58d5035ce737092719d05b5015ffd7a53ed616da7f` | [benchmark_results_no_ic.json](benchmark_results_no_ic.json) |

`lambda.exe` was restored to the IC-on release after the POC.

## Same-tree IC-off / IC-on deltas

Each ratio is the geometric mean of `IC off / IC on`; values above 1 mean the
no-IC build is slower. Only rows with successful results for both variants are
included.

| Engine | Matched rows | Geometric mean | Median row |
|---|---:|---:|---:|
| Lambda MIR, untyped | 53 | 1.049x | 1.011x |
| Lambda MIR, typed | 50 | 1.027x | 1.002x |
| LambdaJS | 55 | 1.091x | 1.016x |

Largest measured regressions in the no-IC build:

| Workload | IC on | IC off | Slowdown |
|---|---:|---:|---:|
| LambdaJS AWFY/NBody | 938 ms | 5.69 s | 6.07x |
| LambdaJS AWFY/Bounce | 12.3 ms | 25.7 ms | 2.09x |
| LambdaJS AWFY/Permute | 22.3 ms | 37.2 ms | 1.67x |
| Lambda MIR AWFY/Richards | 1.28 s | 2.13 s | 1.67x |
| Lambda MIR JetStream/HashMap | 75.7 ms | 125.8 ms | 1.66x |
| Lambda MIR AWFY/DeltaBlue | 53.4 ms | 87.6 ms | 1.64x |

On the independent valid row populations, LambdaJS/QuickJS grows from 6.27x
with IC to 7.05x without it. LambdaJS/Node.js grows from 30.90x to 34.57x.

## Caveat

The matrices ran sequentially (IC off, then IC on), with the runner's normal
10-second inter-suite cooldown. Treat the small aggregate changes as POC
estimates; the multi-fold NBody regression is the stronger signal.

The IC-on LambdaJS AWFY/Havlak row timed out in all three 120-second samples,
while the no-IC build completed at 110.33 s. It is excluded from the paired
geometric mean and must not be interpreted as an IC speedup or slowdown. The
row needs a longer, cool-machine rerun before drawing a conclusion.
