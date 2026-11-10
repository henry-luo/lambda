# Slow Lambda test tuning

**Date:** 2026-11-10. **Tree:** `master`, base `32d495f9f`, with this task's uncommitted changes.
**Scope:** the eight requested `test_lambda_gtest` cases, below 10 seconds in the ordinary debug build. The user explicitly selected debug measurements.

## Completion

All eight requested cases finish below 10 seconds in debug across the focused qualification, full baseline, and final complete script-suite replay. The final 150-case batching configuration passes 2,276/2,276 cases in 3m12s; the initial 50-case configuration took 4m28s for the same cases. No required work remains.

## Causes and changes

1. **Eager MIR compilation dominated cold import closures.** The chart fixture imports 99 modules; its interpreter/AUTO runs took about 0.4 seconds each, while eager JIT took about 15.4 seconds. Sampling attributed most compilation cost to native generation and register allocation. The old optimization budget considered only the largest function. `lambda_mir_link_compiler_pass` now also caps aggregate eager-module optimization at O1 above 5,000 MIR instructions. Small modules and hot AUTO satellite compilation retain their existing policy. This tunes startup cost without changing the execution tiers or ABI (**D8.3.1–D8.3.4**). Long-running eager-module throughput was not benchmarked.
2. **LaTeX repeatedly rebuilt identical native glyph facts.** Sampling found repeated glyph-outline SVG serialization and MATH record construction in Lambda's `radiant_module.cpp`. Its font-query capsule now retains a bounded 2,048-entry LRU keyed by the retained resolved font handle, codepoint and exact scale. Each cached record has a precise GC root. Heap replacement and font-resource replacement clear the records; retained fonts cannot be recycled while used as keys. Shared records detach on mutation (**D6.3.1, D6.4.2–D6.4.3, S9.1.2**).
3. **The suite scheduler serialized nested tier fixtures.** Each case worker previously received one child-process slot, serializing 9–54 isolated CLI evaluations inside the requested cross-tier cases. `case_worker_slots` now reserves three slots per Lambda case worker and reduces the number of case workers to fit the suite budget. The normal heavy-load four-slot ceiling runs one three-slot case worker. Outer batches contain up to 150 cases, allowing ordinary golden execution to fill three existing 50-script CLI batches. Every fixture, tier and golden remains selected (**D8.3.1, D8.5.1**).

The original per-fixture CLI isolation remains. A per-tier batching experiment was removed after confirming that `test-batch` deliberately disables retained MIR imports across heap resets; it did not provide the intended reuse. No sibling or vendor sources changed, and no formal ruling changed.

## Timing evidence

All times below are seconds. The first column is the user's reported duration; the next columns are debug measurements from the focused normal runner, full baseline and final complete script-suite replay. LaTeX uses the harness's actual script elapsed time rather than its zero-duration golden assertion.

| Case | Reported | Focused | Full baseline | Final replay |
|---|---:|---:|---:|---:|
| Keyed noise | 37 | 8.652 | 8.568 | 8.637 |
| Plant architecture/development | 31 | 9.302 | 9.216 | 9.294 |
| Drawing expansion/admission | 28 | 7.428 | 7.472 | 7.499 |
| Surface bindings/coordinates | 26 | 7.418 | 7.398 | 7.389 |
| Animal specimens/surfaces | 20 | 4.851 | 4.821 | 4.811 |
| Chart paint fallback | 19 | 9.397 | 9.397 | 9.534 |
| LaTeX phase-3 corpus | 16 | 9.112 | 8.963 | 9.146 |
| Solar plants/wind | 16 | 4.578 | 4.717 | 4.605 |

## Validation

- `make test-lambda-baseline`: **7,355/7,355 pass**, debug host; 2,276/2,276 main script cases. This run used the initial 50-case outer batches; the final batch-size replay passes 2,276/2,276 in 3m12s with all eight targets below 10 seconds.
- `node test/test_test_run.js`: **85/85 pass**, including nested-slot ceiling contracts at budgets 1, 5 and 6; repeated after the final configuration change.
- `NativeFontTests.CachedMathFactsKeepIndependentValuesAcrossTiers`: all three tiers pass. Its paired `proc/math_metrics_cache.ls` golden covers nested mutation, scaling, face-byte replacement under the same family name, and restoration.
- That cache fixture also passes with every-allocation GC and freed-object poisoning on interp/AUTO/JIT, including three repeated batch evaluations per tier across heap replacement.
- `make generate-premake` and `git diff --check` pass. The executable retains the debug validator marker. Release results do not qualify these measurements.

Evidence lives under `temp/slow-lambda-tests/`: `chart-sample.txt`, `latex-sample.txt`, `runner-final/`, `full-baseline.log`, `runner-contracts-final.json`, and `final-runtime-authorized/`. The unprivileged final replay's four WebSocket bind failures were environmental: the same four cases pass with localhost-listener permission, as used by `make test-lambda-baseline`.
