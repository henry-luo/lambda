# JS MVP Benchmark Results: MVP_Result6

**Ordinary arrays, core string methods and subsequent tuning** — [MVP §§18–20](../../../vibe/jube/JS_MVP_Lmd.md#18-ordinary-arrays-and-core-string-methods).

[Series index and comparison](README.md) · [Raw JSON](MVP_Result6.json) · [Previous phase](MVP_Result5.md) · [Next phase](MVP_Result7.md)

Updated to the final **2026-10-08T14:26:48.615198+08:00** capture on `macOS-26.5.2-arm64-arm-64bit-Mach-O`. All **48 MVP workloads** use the corrected final release: **31 standard kernels and 17 microbenchmarks**. The initial Result6 snapshot and report remain embedded in the JSON as `previous_round` and `previous_round_report`.

## Latest tuning results

**15 alternating release pairs**, with an identical-control peer and native MIR pinned. Values are medians of self-reported execution milliseconds; startup, initial parsing/JIT and teardown are excluded. Paired gain is the median control/MVP ratio; above one means faster. The control is the accepted prior §20 binary, not Result5 or the initial Result6.

| Workload | Prior §20 control ms | MVP ms | Paired gain | Fresh untyped Lambda ms |
|---|---:|---:|---:|---:|
| `kostya/collatz` | 344.351 | 200.168 | 1.720× | 298.685 |
| `kostya/base64` | 13.414 | 11.869 | 1.128× | 12.069 |

Collatz takes **33.0% less time** than untyped Lambda; base64 is approximately level (**1.7% lower median**). The bounds correction also improves `array1` **0.662→0.309 ms**. Across all 48 rows, the geometric control-median/MVP-median gain is **1.032×**. No full-run candidate/control two-sided 95% interval lies entirely above one.

## Reference capture dates

**Cross-engine references are mixed-capture values.** Node v22.13.0 and full LambdaJS were not rerun in the tuning rounds. The ratios below are descriptive comparisons with their retained measurements, not fresh paired cross-engine results.

| Reference cells | Capture (UTC+08, 2026-10-08) |
|---|---|
| Untyped Lambda: collatz, base64 | Latest run, 14:25–14:26; marked ‡ |
| Untyped Lambda: primes, fannkuch | Prior numeric tuning, 13:08–13:10; marked † |
| Other untyped Lambda, all Node, six full LambdaJS cells | Original Result6, 11:36–11:41 |

Every reference cell has its source capture, lane, time and binary hash in `reference_provenance`. Samples remain in the corresponding raw capture. The JS workload text and oracles match the original Result6. Untyped Lambda retains native semantics (**S1.11**, **S9.1.3**); the [port/timing audit](MVP_Result4.md#port-and-timing-audit) still applies. Node tiering during fresh-process workloads remains included. FFT’s zero-input oracle remains weak numeric evidence.

## Six targets introduced in this phase

Initial and latest MVP columns are separate historical snapshots; their ratio does not isolate tuning. Reference/MVP ratios above one mean the latest MVP is faster.

| Workload | Initial Result6 MVP | Latest MVP | Untyped Lambda | Node | Full LambdaJS | Untyped / MVP | Node / MVP |
|---|---:|---:|---:|---:|---:|---:|---:|
| `r7rs/mbrot` | 0.852 | 0.855 | 0.730 | 1.883 | 2.205 | 0.854× | 2.202× |
| `larceny/puzzle` | 2.489 | 2.492 | 13.857 | 3.351 | 28.951 | 5.561× | 1.345× |
| `kostya/base64` | 81.083 | 11.869 | 12.069 ‡ | 17.385 | 478.189 | 1.017× | 1.465× |
| `kostya/json_gen` | 14.350 | 5.614 | 23.282 | 6.170 | 27.703 | 4.147× | 1.099× |
| `kostya/brainfuck` | 125.715 | 42.139 | 124.740 | 33.609 | 1778.156 | 2.960× | 0.798× |
| `kostya/levenshtein` | 121.696 | 1.563 | 5.144 | 4.043 | 76.939 | 3.291× | 2.587× |

## Full supported population

Using the mixed-capture references above, geometric reference/MVP ratios are **1.541× for untyped Lambda across 31 standard kernels**, **2.405× for Node across those 31**, and **1.873× for Node across all 48**. For the six additions, they are **2.408× / 1.464×** against untyped Lambda / Node. These aggregates are descriptive. Only two microbenchmarks have untyped ports; an all-48 untyped aggregate is unavailable.

## All 48 workload medians

### Standard kernels

| Workload | MVP ms | Prior §20 control ms | Paired gain | Untyped Lambda ms | Historical Node ms |
|---|---:|---:|---:|---:|---:|
| `beng/binarytrees` | 3.793 | 3.789 | 0.998× | 4.888 | 2.047 |
| `beng/fannkuch` | 0.264 | 0.264 | 1.000× | 0.281 † | 2.190 |
| `beng/spectralnorm` | 1.066 | 1.069 | 0.999× | 1.676 | 2.533 |
| `kostya/base64` | 11.869 | 13.414 | 1.128× | 12.069 ‡ | 17.385 |
| `kostya/brainfuck` | 42.139 | 42.223 | 1.002× | 124.740 | 33.609 |
| `kostya/collatz` | 200.168 | 344.351 | 1.720× | 298.685 ‡ | 1414.376 |
| `kostya/json_gen` | 5.614 | 5.700 | 1.016× | 23.282 | 6.170 |
| `kostya/levenshtein` | 1.563 | 1.551 | 0.994× | 5.144 | 4.043 |
| `kostya/matmul` | 5.903 | 5.889 | 0.998× | 5.709 | 15.544 |
| `kostya/primes` | 2.074 | 2.060 | 1.000× | 2.309 † | 4.369 |
| `larceny/array1` | 0.309 | 0.662 | 2.136× | 0.818 | 1.823 |
| `larceny/deriv` | 8.487 | 8.469 | 0.998× | 21.456 | 3.848 |
| `larceny/diviter` | 266.577 | 266.546 | 0.999× | 250.096 | 455.004 |
| `larceny/divrec` | 0.618 | 0.621 | 1.003× | 1.223 | 7.776 |
| `larceny/gcbench` | 89.510 | 89.665 | 1.002× | 116.288 | 21.987 |
| `larceny/paraffins` | 0.102 | 0.102 | 0.990× | 0.203 | 1.034 |
| `larceny/pnpoly` | 7.391 | 7.360 | 0.996× | 12.432 | 5.905 |
| `larceny/puzzle` | 2.492 | 2.487 | 0.999× | 13.857 | 3.351 |
| `larceny/quicksort` | 1.304 | 1.300 | 0.999× | 1.105 | 1.822 |
| `larceny/ray` | 0.238 | 0.242 | 1.017× | 0.219 | 3.622 |
| `larceny/triangl` | 155.729 | 155.415 | 1.000× | 182.911 | 68.041 |
| `r7rs/ack` | 8.947 | 8.937 | 1.000× | 13.898 | 13.473 |
| `r7rs/cpstak` | 0.243 | 0.245 | 1.008× | 0.273 | 0.939 |
| `r7rs/fft` | 0.165 | 0.164 | 1.000× | 0.327 | 1.602 |
| `r7rs/fib` | 1.480 | 1.474 | 0.999× | 1.549 | 1.809 |
| `r7rs/fibfp` | 1.501 | 1.485 | 1.004× | 2.371 | 1.820 |
| `r7rs/mbrot` | 0.855 | 0.858 | 1.000× | 0.730 | 1.883 |
| `r7rs/nqueens` | 0.925 | 0.926 | 0.998× | 1.110 | 1.838 |
| `r7rs/sum` | 0.270 | 0.270 | 1.000× | 0.270 | 1.231 |
| `r7rs/sumfp` | 0.027 | 0.027 | 1.000× | 0.070 | 0.902 |
| `r7rs/tak` | 0.123 | 0.125 | 1.016× | 0.137 | 0.728 |

### Microbenchmarks

| Workload | MVP ms | Prior §20 control ms | Paired gain | Historical untyped Lambda ms | Historical Node ms |
|---|---:|---:|---:|---:|---:|
| `js_micro/args_ctl` | 2.708 | 2.707 | 1.001× | — | 2.241 |
| `js_micro/args_fp` | 2.718 | 2.706 | 0.995× | — | 2.254 |
| `js_micro/lit` | 0.133 | 0.133 | 1.008× | — | 3.700 |
| `js_micro/named` | 3.953 | 3.960 | 0.998× | — | 6.195 |
| `js_mvp_lmd/calls` | 5.090 | 5.067 | 0.987× | — | 4.663 |
| `js_mvp_lmd/dense_array` | 57.855 | 57.773 | 0.996× | — | 11.523 |
| `js_mvp_lmd/integer_dense` | 2.715 | 2.711 | 0.997× | 15.570 | 2.981 |
| `js_mvp_lmd/integer_indirect` | 1.572 | 1.575 | 1.000× | 5.117 | 1.146 |
| `js_mvp_lmd/map_iteration` | 1.453 | 1.453 | 1.002× | — | 3.589 |
| `js_mvp_lmd/map_lookup` | 13.048 | 13.407 | 1.027× | — | 4.075 |
| `js_mvp_lmd/numeric` | 70.888 | 70.895 | 1.000× | — | 70.942 |
| `js_mvp_lmd/object_delete` | 2.424 | 2.520 | 1.086× | — | 1.117 |
| `js_mvp_lmd/object_fields` | 0.376 | 0.376 | 1.000× | — | 1.591 |
| `js_mvp_lmd/object_growth` | 2.985 | 3.002 | 1.001× | — | 3.809 |
| `js_mvp_lmd/object_retype` | 0.296 | 0.296 | 1.000× | — | 1.310 |
| `js_mvp_lmd/object_retype_escaped` | 6.818 | 6.840 | 1.000× | — | 1.317 |
| `js_mvp_lmd/strings` | 0.101 | 0.101 | 1.000× | — | 0.262 |

## Map/JSON regression diagnosis

The earlier ~2% slowdowns reproduced with the exact §19/§20 binaries. Their MIR operations and hot native helper bodies were unchanged apart from relocations; the helpers moved **5,280 bytes**. A diagnostic relink placed **31 runtime helpers at identical addresses and sizes**. Across 60 pairs, the Map/JSON candidate/control 95% intervals became **0.998–1.015 / 0.997–1.017**. This isolates native executable-layout sensitivity; the exact cache/predictor mechanism remains unmeasured. The ordering file is not part of the normal build.

A separate 60-pair comparison of the final normal build against §19 still finds **1.0% slower `map_lookup`** (12.9760→13.0995 ms, interval **1.004–1.015**). `json_gen` is statistically unchanged (5.6735→5.6825 ms, **0.997–1.006**). Another 60-pair check finds no regression in object growth or escaped retyping. These follow-ups retain separate samples and do not replace the 15-pair table cells.

## Capture and acceptance

- Latest run: **2,190 measured / 146 discarded matching outputs**; two normal-build confirmation runs: **720 / 12**. Sources and frozen binaries remained unchanged. The 60-pair placement experiment is diagnostic only.
- Release build: `make build-release-compile`, exit 0, **0 errors / 28 warnings**. MVP semantic tests: **44/44**, normally and with forced GC/poisoning. Lambda/input: **6,408/6,408**. Test262: **40,261/40,261**, zero retries, batch losses or regressions. Gates were rerun after the nested-loop bounds fix.
- Final candidate SHA-256: `da89d81a9fa893516ede62345771b82a3709fd45faed23c0c2bc7dc37f7220ca`.
- Prior §20 control SHA-256: `2039dbcb41b9e3ad0dae100730ddbcb8b79710c0f5da74e5e258935e32192730`.
- Frozen binaries, source snapshots, runner, outputs, MIR, hashes, build/test logs and replay command: `temp/mvp_followup_20261008/` and the [implementation record](../../../vibe/impl/JS_MVP_Lmd_Array_String.md#further-collatzbase64-tuning-and-regression-diagnosis).
- JSON embeds the accepted latest and prior numeric captures, separate confirmations, validation, original snapshot/report, and per-cell provenance. Earlier diagnostic candidates are excluded from current values.
- Validation is on macOS ARM64. The broader §15 feature-edge matrix and Linux/Windows acceptance remain pending.
