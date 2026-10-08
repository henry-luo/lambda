# JS MVP Benchmark Results: MVP_Result2

**Object/Map and inlining tuning** — [MVP §§10–12](../../../vibe/jube/JS_MVP_Lmd.md).

[Series index and comparison](README.md) · [Raw JSON](MVP_Result2.json) · [Previous](MVP_Result1.md) · [Next](MVP_Result3.md)

**Phase:** Object/Map and inlining tuning.

## Measurement

- Captured: 2026-10-07T17:51:21.933018+08:00.
- Platform: `macOS-26.5.2-arm64-arm-64bit-Mach-O`; Node `v22.13.0`.
- Runs: 15 measured processes per lane/workload, with one discarded process per lane.
- Release binaries, pinned native MIR and self-reported execution milliseconds. Startup, initial compilation and teardown are excluded; Node tiering during execution is included.
- Each control is the binary recorded within this snapshot, which need not be the preceding numbered result.
- All times below are recorded medians. Paired gains are copied only where the snapshot records them; missing comparisons are left blank.

## Results

| Workload | Control ms | MVP ms | LJS ms | Untyped Lambda ms | Node ms | Recorded paired gain |
|---|---:|---:|---:|---:|---:|---:|
| larceny/deriv | 24.418 | 20.271 | — | — | 3.858 | 1.20× |
| larceny/gcbench | 163.431 | 107.006 | — | — | 22.630 | 1.53× |
| js_micro/lit | 0.138 | 0.137 | — | — | 3.756 | 1.00× |
| js_micro/named | 30.878 | 3.971 | — | — | 6.289 | 7.77× |
| js_micro/args_ctl | 10.949 | 4.643 | — | — | 2.238 | 2.35× |
| js_micro/args_fp | 10.928 | 4.724 | — | — | 2.244 | 2.31× |
| js_mvp_lmd/object_fields | 2.829 | 0.375 | — | — | 1.580 | 7.55× |
| js_mvp_lmd/object_growth | 3.571 | 3.556 | — | — | 3.816 | 1.00× |
| js_mvp_lmd/object_retype | 35.055 | 0.403 | — | — | 1.297 | 87.83× |
| js_mvp_lmd/object_retype_escaped | 42.282 | 7.697 | — | — | 1.625 | 5.17× |
| js_mvp_lmd/object_delete | 2.657 | 2.300 | — | — | 1.136 | 1.14× |
| js_mvp_lmd/map_lookup | 18.079 | 13.374 | — | — | 4.056 | 1.35× |
| js_mvp_lmd/map_iteration | 1.882 | 1.892 | — | — | 3.609 | 1.00× |
| js_mvp_lmd/numeric | 70.915 | 70.975 | — | — | 70.810 | 1.00× |
| js_mvp_lmd/dense_array | 76.996 | 76.830 | — | — | 11.961 | 1.00× |
| js_mvp_lmd/calls | 6.460 | 5.908 | — | — | 8.509 | 1.00× |
| js_mvp_lmd/strings | 2.639 | 2.786 | — | — | 0.284 | 1.00× |

## Interpretation and recorded validation

- Seventeen workloads cover object fields/transitions, Map operations, bounded inlining and scalar controls.
- This is the bundled paired comparison; its identical-control lane and bootstrap intervals qualify small effects. The escaped-retype diagnostic retains allocation.
- Recorded gates: MVP 30/30; forced-GC subset 10/10; Lambda/input 6,286/6,286; Test262 40,261 fully passed with zero retries.
- An earlier import-cache timing assertion failed; the unchanged isolated and aggregate reruns passed. This qualification remains in the JSON.

## Provenance

- control: `temp/mvp_lmd_tuning2/control.exe`; SHA-256 `d0ac0f6188f72c48f92afe5f3757583b5fa5e3b254e36dd0a2f3376174f96d46`.
- candidate: `temp/mvp_lmd_tuning2/candidate.exe`; SHA-256 `9af6d491cfe0306286f1572dca16559925dcbdffe2ce4e29eeba21df8660ff64`.
- Raw snapshot SHA-256: `a5fb84523743d4550dd2ae37719e8bcd14463526c5973f93b84141afa7bfb110`.
- Original samples, timing boundaries, source identities, control/noise evidence and gate qualifications remain in the unchanged JSON.
- JS and untyped Lambda retain their own numeric/container semantics (**S1.11**, **D2.2.5**); these are admitted-workload comparisons, not whole-language equivalence.
