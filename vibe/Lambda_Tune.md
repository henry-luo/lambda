# Lambda Tuning Notes

**Date**: June 26, 2026

This note records the Lambda tuning pass for slow package-heavy Lambda script tests, especially the math markup tests.

## 1. Bottleneck Identified From Profiling

The slowest focused tests were:

| Test | Before tuning |
|------|--------------:|
| `math_test_math_atom_enclose` | 13.421 s |
| `math_test_math_html_output` | 16.603 s |

The detailed profile for `test/lambda/math/test_math_html_output.ls` showed that runtime execution was not the primary cost. The wall time was dominated by MIR transpiling imported package scripts.

Pre-tuning direct profile:

| Run | Time |
|-----|-----:|
| `test_math_html_output.ls` direct run | 22.54 s real |

The biggest single hotspot was `lambda/package/math/metrics_data.ls`:

| Script | Parse | AST | Transpile | Total |
|--------|------:|----:|----------:|------:|
| `lambda/package/math/metrics_data.ls` | 57.045 ms | 268.604 ms | 15,780.468 ms | 16,175.588 ms |

The imported scripts were already compiled in parallel by import level. For level 0, the runner launched 15 jobs with 15 worker threads against an 8 CPU cap, but the elapsed time was still 16,185 ms because `metrics_data.ls` was one very large static-data module:

| Import level | Modules | Jobs | Threads | CPU cap | Elapsed |
|--------------|--------:|-----:|--------:|--------:|--------:|
| 0 | 15 | 15 | 15 | 8 | 16,185.370 ms |

Root cause: fully static collection literals, especially the large metric tables, were lowered into MIR as runtime construction code. This produced huge MIR generation work for data that never needed dynamic construction.

## 2. Tuning Change

The tuning changes made static collection data compile as data, not code.

### Static Container Flag

Added a new `Container.is_static` flag in `lambda/lambda.h`.

Purpose:

- marks const-pool/static data containers as read-only;
- keeps this lifecycle state separate from `map_kind`;
- avoids overloading existing `array_flags` or map-kind semantics.

### Const-Pool Materialization

In `lambda/transpile-mir.cpp`, fully static generic collection literals can now be built during AST-to-MIR lowering and appended to the script `const_list`.

The generated MIR then emits one const load for the whole collection instead of many instructions to allocate and populate the collection at runtime.

Static-folded collections are allocated from the script pool and marked `is_static`.

The first implementation is conservative:

| Case | Behavior |
|------|----------|
| generic fully static maps | materialized into const pool |
| generic fully static arrays | materialized into const pool |
| specialized `ArrayNum` literals | keep existing optimized typed-array lowering |
| proc bodies | skipped, preserving procedural mutable literal behavior |
| nested array literals | skipped, preserving existing nested-array and N-D promotion behavior |
| bool typed arrays | skipped, preserving bool mask `ArrayNum` behavior |

### Mutation Guards

Added mutation checks in `lambda/lambda-eval.cpp` so static containers cannot be changed through mutation helpers.

Covered mutation paths:

- `fn_array_set`
- `fn_map_set` for map/object/element containers

### Container Layout Fallout

The `Container.flags` layout changed at the same time, so raw-layout users were updated:

- `lib/gc/gc_heap.c`
- `lambda/transpile-mir.cpp`
- comments in `lambda/lambda.hpp` and `lambda/lambda-data-runtime.cpp`

The raw byte reads for `map_kind`, `array_flags`, `is_view`, `is_pinned`, and `elem_type` were adjusted to match the new layout.

## 3. Result

Focused gtest before/after:

| Test | Before | After | Improvement |
|------|-------:|------:|------------:|
| `math_test_math_atom_enclose` | 13.421 s | 0.614 s | 21.9x faster |
| `math_test_math_html_output` | 16.603 s | 0.890 s | 18.7x faster |

Post-tuning direct profile for `test_math_html_output.ls`:

| Run | Time |
|-----|-----:|
| `test_math_html_output.ls` direct run | 1.79 s real |

Post-tuning `metrics_data.ls` profile:

| Script | Parse | AST | Transpile | Total |
|--------|------:|----:|----------:|------:|
| `lambda/package/math/metrics_data.ls` | 12.647 ms | 40.362 ms | 152.167 ms | 206.927 ms |

The key hotspot improved from 15,780.468 ms transpile time to 152.167 ms:

| Metric | Before | After | Improvement |
|--------|-------:|------:|------------:|
| `metrics_data.ls` transpile | 15,780.468 ms | 152.167 ms | 103.7x faster |
| `metrics_data.ls` total | 16,175.588 ms | 206.927 ms | 78.2x faster |
| direct script wall time | 22.54 s | 1.79 s | 12.6x faster |

Verification completed:

| Check | Result |
|-------|--------|
| release `lambda` build | passed |
| release `test_lambda_gtest` build | passed |
| focused math gtests | passed |
| full `./test/test_lambda_gtest.exe` | 381/381 passed |
| `git diff --check` | passed |

## Follow-Up Ideas

Potential next steps:

- Extend static const materialization to `Element` literals once element construction semantics are audited.
- Add a small unit/integration test that proves static containers reject mutation.
- Add an optional profile report mode that preserves before/after gtest timing JSON under `temp/` with a timestamped filename.
