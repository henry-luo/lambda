# Transpile_Js50_Es2023 - ES2023 JS262 Baseline Admission

Date: 2026-05-30

Status: implemented (2026-05-30)

Js50 continues the Js49 baseline policy: expand only by admitting tests that
pass normal release batch execution, while preserving the already-passing js262
baseline. No gate was weakened for this phase.

Implementation result: the checked-in release baseline is now ES2023-scoped
with 39,246 fully passing tests, 3,049 skipped tests, 0 failures, and 0
non-fully-passing tests in the final update pass.

## 1. Current Baseline

Current checked-in release baseline:

```text
# Scope: ES2023 (skip ES2024+ features)
# Total passing: 39246
# Total tests: 42295  Skipped: 3049  Batched: 39246  Passed: 39246  Failed: 0
# Runtime: 148.2s total (prep 0.0s + exec 148.1s)
# Batch size: batched 50 tests/process; async 50 tests/process
```

This reflects the final Js50 release update pass, captured on 2026-05-30.

## 2. ES2023 Inventory

Measured from `test/js262/test262_metadata.tsv` against the previous ES2022
baseline, using the runner's discovered test set:

| Bucket | Count | Meaning |
|---|---:|---|
| Outside previous baseline | 39 | Tests tagged with ES2023 features outside Js49 |
| Admitted in Js50 | 23 | Hashbang tests now passing in normal batch execution |
| Future overlap | 15 | ES2023-tagged, but also ES2024+ or proposal overlap |
| Not discovered by runner | 1 | `staging/sm/WeakMap/symbols.js` metadata row not in the discovered suite |
| Intentional exceptions | 0 | No new Js48-style host/PTC exception needed |

By ES2023 feature tag outside the previous baseline:

| Feature | Count |
|---|---:|
| `hashbang` | 23 |
| `symbols-as-weakmap-keys` | 9 |
| `array-find-from-last` | 4 |
| `change-array-by-copy` | 3 |

The 23 admitted tests are all `language/comments/hashbang/` cases, including
the module/raw case.

## 3. Implementation Notes

The runner already had parser/runtime support for hashbang source text. The
remaining issue was admission and source assembly:

- Raw hashbang tests are now admitted as ES2023 raw tests.
- `language/comments/hashbang/module.js` is admitted as the ES2023 module/raw
  case.
- Raw module source is executed without prepending harness text, preserving
  `#!` at the start of the source file.

The raw module handling is intentionally narrow. Non-raw module tests continue
to use the normal module source assembly path with harness prelude injection.

## 4. Verification

Focused debug admission:

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --batch-file=temp/js50_es2023_actionable.txt \
  --write-failures=temp/js50_es2023_hashbang_debug_async.tsv \
  --gtest_brief=1
```

Result: 23 passed, 0 failed.

Release baseline guard before updating:

```bash
./test/test_js_test262_gtest.exe --batch-only --baseline-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --write-failures=temp/js50_release_baseline_guard.tsv \
  --gtest_brief=1
```

Result: 39,223 / 39,223 fully passed, 0 regressions.

Final release update:

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --update-baseline \
  --write-failures=temp/js50_update_baseline.tsv \
  --gtest_brief=1
```

Result: 39,246 / 39,246 fully passed, 23 improvements, 0 regressions.
