# JS MVP Benchmark Results: MVP_Result3

**Element coercion and scalar ownership** — [MVP §§13–14](../../../vibe/jube/JS_MVP_Lmd.md).

[Series index and comparison](README.md) · [Raw JSON](MVP_Result3.json) · [Previous](MVP_Result2.md) · [Next](MVP_Result4.md)

**Phase:** Element coercion and scalar ownership.

## Measurement

- Captured: 2026-10-07T21:42:44.412430+08:00.
- Platform: `macOS-26.5.2-arm64-arm-64bit-Mach-O`; Node `v22.13.0`.
- Runs: 15 measured processes per lane/workload, with one discarded process per lane.
- Release binaries, pinned native MIR and self-reported execution milliseconds. Startup, initial compilation and teardown are excluded; Node tiering during execution is included.
- Each control is the binary recorded within this snapshot, which need not be the preceding numbered result.
- All times below are recorded medians. Paired gains are copied only where the snapshot records them; missing comparisons are left blank.

## Results

| Workload | Control ms | MVP ms | LJS ms | Untyped Lambda ms | Node ms | Recorded paired gain |
|---|---:|---:|---:|---:|---:|---:|
| r7rs/fib † | 1.487 | 1.494 | 1.969 | 1.597 | 1.836 | 1.00× |
| r7rs/fibfp | 2.144 | 2.082 | 2.805 | 3.548 | 2.944 | 0.99× |
| r7rs/tak | 0.177 | 0.177 | 0.506 | 0.195 | 1.259 | 0.99× |
| r7rs/cpstak | 0.349 | 0.336 | 1.007 | 0.396 | 1.499 | 1.00× |
| r7rs/sum † | 0.270 | 0.270 | 0.679 | 0.270 | 1.246 | 1.00× |
| r7rs/sumfp | 0.042 | 0.037 | 0.123 | 0.122 | 2.147 | 1.03× |
| r7rs/ack | 11.804 | 11.154 | 20.148 | 18.821 | 16.984 | 1.01× |
| larceny/diviter | 291.066 | 294.748 | 680.134 | 318.402 | 558.098 | 1.00× |
| larceny/divrec | 0.731 | 0.744 | 18.717 | 1.423 | 9.160 | 1.00× |
| kostya/collatz | 1069.130 | 1083.473 | 3618.743 | 561.726 | 2864.396 | 1.02× |
| js_mvp_lmd/integer_dense | 5.155 | 3.905 | 24.377 | 19.386 | 4.140 | 1.32× |
| js_mvp_lmd/integer_indirect | 2.203 | 1.955 | 8.667 | 6.993 | 1.882 | 1.10× |
| larceny/pnpoly † | 28.710 | 17.069 | 76.073 | 12.476 | 5.878 | 1.68× |
| larceny/deriv | 24.971 | 23.940 | 109.933 | 26.988 | 4.879 | 1.02× |
| larceny/gcbench | 113.227 | 109.881 | 598.751 | 127.732 | 24.418 | 1.03× |
| js_micro/lit | 0.152 | 0.147 | 129.017 | — | 4.032 | 1.00× |
| js_micro/named | 4.307 | 4.302 | 160.706 | — | 6.689 | 1.00× |
| js_micro/args_ctl | 5.377 | 3.966 | 78.727 | — | 2.558 | 1.33× |
| js_micro/args_fp | 5.186 | 3.955 | 80.020 | — | 2.433 | 1.32× |
| js_mvp_lmd/object_fields | 0.444 | 0.444 | 11.394 | — | 1.788 | 1.00× |
| js_mvp_lmd/object_growth | 3.806 | 3.904 | 44.005 | — | 4.054 | 1.01× |
| js_mvp_lmd/object_retype | 0.397 | 0.296 | 59.061 | — | 1.333 | 1.36× |
| js_mvp_lmd/object_retype_escaped | 6.757 | 6.616 | 58.715 | — | 1.335 | 1.02× |
| js_mvp_lmd/object_delete † | 2.342 | 2.469 | 97.275 | — | 1.131 | 0.99× |
| js_mvp_lmd/map_lookup | 13.116 | 12.932 | 107.391 | — | 4.080 | 1.01× |
| js_mvp_lmd/map_iteration | 1.860 | 1.442 | 659.217 | — | 3.633 | 1.29× |
| js_mvp_lmd/numeric | 71.043 | 71.075 | 137.818 | — | 71.036 | 1.00× |
| js_mvp_lmd/dense_array † | 76.513 | 58.613 | 287.933 | — | 11.877 | 1.31× |
| js_mvp_lmd/calls † | 5.190 | 5.188 | 47.136 | — | 4.705 | 1.00× |
| js_mvp_lmd/strings | 2.648 | 2.644 | 0.285 | — | 0.281 | 1.00× |

## Interpretation and recorded validation

- Thirty workloads have 15 paired rounds; six have a 30-pair follow-up. Rows marked † use that follow-up in this report and the history table. Both rounds remain in the JSON.
- The follow-up confirms 1.68× for pnpoly and 1.31× for dense_array. diviter remains within noise; object deletion remains inconclusive.
- All 4,410 measured and 252 discarded output checks matched. Recorded gates: MVP 34/34 normally and under forced GC/poisoning; Lambda/input 6,292/6,292.
- Test262: 40,259 fully passing, two retry-only cases, zero regressions. Two initial Lambda child failures passed focused replays and an unchanged aggregate rerun; their initial cause remains unconfirmed.
- The JSON retains the platform and release-profile MIR-budget qualifications; this report does not turn recovered runs into a clean first-pass gate.

## Provenance

- control: `temp/mvp_lmd_element_tuning/confirm/control.exe`; SHA-256 `7c0325224a52793c13e1d576520bc3c3716007bc34caa090033d64a7a6bc00b7`.
- candidate: `temp/mvp_lmd_element_tuning/confirm/candidate.exe`; SHA-256 `3fbbefeb3521edcb95060e4bca0b2d164aca7d98d6c5ffa34b29c4135d15e6a8`.
- Raw snapshot SHA-256: `5afb735a8d2ed0a9cc060663f01ffeab391f89b21f7d6a540fc5081ec3dee29b`.
- Original samples, timing boundaries, source identities, control/noise evidence and gate qualifications remain in the unchanged JSON.
- JS and untyped Lambda retain their own numeric/container semantics (**S1.11**, **D2.2.5**); these are admitted-workload comparisons, not whole-language equivalence.
