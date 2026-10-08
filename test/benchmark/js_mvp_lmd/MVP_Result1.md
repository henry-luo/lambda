# JS MVP Benchmark Results: MVP_Result1

**Scalar and integer tuning** — [MVP §9](../../../vibe/jube/JS_MVP_Lmd.md).

[Series index and comparison](README.md) · [Raw JSON](MVP_Result1.json) · [Next](MVP_Result2.md)

**Phase:** Scalar and integer tuning.

## Measurement

- Captured: 2026-10-07T08:57:46.187459+08:00.
- Platform: `macOS-26.3-arm64-arm-64bit`; Node `v24.7.0`.
- Runs: 5 measured processes per lane/workload, with one discarded process per lane.
- Release binaries, pinned native MIR and self-reported execution milliseconds. Startup, initial compilation and teardown are excluded; Node tiering during execution is included.
- Each control is the binary recorded within this snapshot, which need not be the preceding numbered result.
- All times below are recorded medians. Paired gains are copied only where the snapshot records them; missing comparisons are left blank.

## Results

| Workload | Control ms | MVP ms | LJS ms | Untyped Lambda ms | Node ms | Recorded paired gain |
|---|---:|---:|---:|---:|---:|---:|
| r7rs/fib | 1.289 | 1.291 | 1.782 | 1.277 | 1.283 | — |
| r7rs/fibfp | 1.275 | 1.282 | 1.672 | 1.888 | 1.356 | — |
| r7rs/sum | 0.677 | 0.252 | 0.656 | 0.255 | 0.737 | — |
| r7rs/tak | 0.117 | 0.117 | 0.341 | 0.122 | 0.312 | — |
| larceny/diviter | 600.719 | 253.543 | 648.633 | 252.250 | 383.018 | — |
| larceny/divrec | 0.640 | 0.638 | 15.053 | 1.167 | 7.615 | — |
| kostya/collatz | 480.511 | 481.496 | 1335.798 | 280.328 | 1180.324 | — |
| js_mvp_lmd/integer_dense | 5.672 | 3.520 | 19.256 | 13.741 | 3.810 | — |
| js_mvp_lmd/integer_indirect | 2.083 | 1.482 | 6.498 | 4.929 | 1.168 | — |

## Interpretation and recorded validation

- The selected scalar milestone retains the preceding optimized release as its measured control.
- Recorded gates: MVP 20/20 normally and with forced GC/poisoning; Lambda/input 6,284 passed; Test262 40,261 passed with zero regressions.
- The JSON also retains the MIR audit, implementation cost, source hashes and release GC smoke evidence.

## Provenance

- Measured release: `/Users/henryluo/Projects/lambda/temp/js-mvp-lmd/lambda.exe`; SHA-256 `53a05ffb4b1b32a80ca9ad838cec3710f0d03711e6426adaadb606283a98c2b4`.
- Control release: `/Users/henryluo/Projects/lambda/temp/js-mvp-lmd/temp/mvp_lmd_integer/before.exe`; SHA-256 `01962fb935e2dc8e23a21a494878279672d5eb5452e04b5aa01b38deb9ccb325`.
- Raw snapshot SHA-256: `615765b072faddef844956c7c63db1b0932d4db65df7085e5e6dc258f7c07a34`.
- Original samples, timing boundaries, source identities, control/noise evidence and gate qualifications remain in the unchanged JSON.
- JS and untyped Lambda retain their own numeric/container semantics (**S1.11**, **D2.2.5**); these are admitted-workload comparisons, not whole-language equivalence.
