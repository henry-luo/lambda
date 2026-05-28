# Transpile_Js49_Es2022 - Proposal To Reach Full ES2022 JS262 Scope

Date: 2026-05-28

Status: proposal

This proposal defines the next milestone after Js48: expand LambdaJS from the
current ES2021 js262 scope to a credible ES2022 scope, while preserving the
existing release baseline and the explicit Js48 compatibility exceptions.

The rule is the same as Js48: do not grow the baseline by weakening gates.
Every newly admitted test must pass in normal batch execution, not only in
retry, and release runtime must remain stable.

## 1. Current Baseline

Current checked-in release baseline:

```text
# Scope: ES2021 (skip ES2022+ features)
# Total passing: 38939
# Total tests: 42295  Skipped: 3356  Batched: 38941  Passed: 38939  Failed: 0
# Runtime: 181.3s total (prep 0.1s + exec 180.1s)
# Batch size: batched 50 tests/process; async 50 test/process
```

Js48 also made the following explicit non-ES2021-scope decisions:

- Proper tail calls: `intentional PTC exception`
- Cross-realm host support: `intentional cross-realm host exception`
- Browser `document.all` behavior: `intentional browser IsHTMLDDA exception`
- Annex B `Function.prototype.caller` web-compat behavior:
  `intentional Annex B caller exception`

Those exceptions remain out of scope for Js49 unless a separate product decision
changes them.

## 2. Outstanding ES2022 Inventory

Measured from `test/js262/test262_metadata.tsv` against the current
`test/js262/test262_baseline.txt`, using the runner's discovered test set:

| Bucket | Count | Meaning |
|---|---:|---|
| Missing ES2022 feature support | 35 | Real ES2022 implementation work |
| Async not admitted yet | 53 | ES2022-tagged async tests outside the baseline |
| Future/other unsupported overlap | 14 | ES2022-tagged, but also ES2023+/proposal/other unsupported |
| Intentional host/PTC exceptions | 11 | Mostly `cross-realm` overlap |
| Module flag not admitted yet | 1 | ES2022 module/static-initializer case |

By ES2022 feature tag outside the current baseline:

| Feature | Count |
|---|---:|
| `class-fields-public` | 58 |
| `class-static-methods-private` | 55 |
| `regexp-match-indices` | 31 |
| `top-level-await` | 13 |
| `class-methods-private` | 6 |
| `class-static-fields-public` | 5 |
| `class-fields-private` | 3 |
| `class-static-block` | 2 |
| `TypedArray.prototype.at` | 2 |
| `class-static-fields-private` | 1 |
| `class-fields-private-in` | 1 |

Important interpretation:

- The runner already marks most ES2022 library/class features as supported.
  Many of those remaining tests are outside the baseline because they are async
  or module-admission cases, not because the syntax/runtime is entirely absent.
- The only ES2022 feature tags still listed in `UNSUPPORTED_FEATURES` are:
  `top-level-await` and `regexp-match-indices`.

## 3. Scope Decision

For Js49 "full ES2022 in current js262 scope", include:

1. RegExp match indices (`/d`, `.indices`, `.hasIndices`).
2. Top-level await in modules.
3. Admission and verification of ES2022-tagged async tests that do not require
   future/proposal features or Js48 host exceptions.
4. Admission of the remaining ES2022 module/static-initializer test if it passes
   normal module execution.

Explicitly exclude from Js49:

- ES2023+ and proposal features that overlap with ES2022 tags:
  `import-defer`, `regexp-v-flag`, `regexp-duplicate-named-groups`,
  `explicit-resource-management`, `nonextensible-applies-to-private`, and
  similar future tags.
- Js48 intentional exceptions: PTC, cross-realm, `IsHTMLDDA`, and `caller`.
- Generic harness-only features such as `host-gc-required`.

## 4. Phase Plan

### P1 - Refresh ES2021 Baseline And ES2022 Inventory

Goal: start Js49 from a clean, repeatable baseline and regenerate the ES2022
candidate lists from local metadata.

Work:

- Build release.
- Run the current ES2021 baseline with async chunk size 50.
- Write candidate manifests under `temp/`, not `/tmp`:
  - `temp/js49_es2022_regexp_indices.txt`
  - `temp/js49_es2022_tla.txt`
  - `temp/js49_es2022_async_admission.txt`
  - `temp/js49_es2022_module_admission.txt`
- Keep future-overlap and intentional-exception candidates in separate audit
  files for traceability, but do not run them as Js49 candidates.

Acceptance:

- Existing ES2021 baseline remains 0 failures, 0 regressions, and 0
  non-fully-passing tests.
- Candidate counts match the current inventory or are explained by metadata
  changes.

Commands:

```bash
make release
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --write-failures=temp/js49_p1_release.tsv \
  --gtest_brief=1
```

### P2 - RegExp Match Indices

Goal: implement ES2022 RegExp match indices and admit the actionable
`regexp-match-indices` tests.

Current actionable set:

- `31` total `regexp-match-indices` tests outside baseline.
- `26` actionable after excluding future/host overlaps.

Representative tests:

- `built-ins/RegExp/duplicate-flags.js`
- `built-ins/RegExp/match-indices/indices-array.js`
- `built-ins/RegExp/match-indices/indices-groups-object.js`
- `built-ins/RegExp/prototype/flags/return-order.js`
- `built-ins/RegExp/prototype/hasIndices/this-val-regexp.js`

Expected implementation areas:

- Parser/runtime acceptance for the `d` RegExp flag.
- Store `hasIndices` on RegExp instances.
- Add `RegExp.prototype.hasIndices`.
- Include `d` in `RegExp.prototype.flags` in spec order.
- Extend match result construction so `exec`, `match`, and related paths expose
  `.indices`.
- Populate `.indices.groups` for named capture groups.
- Preserve behavior for unmatched captures as `undefined` and for non-global,
  global, sticky, unicode, and named-group combinations.

Risk controls:

- Keep this phase independent from `regexp-v-flag` and duplicate named groups.
- Do not treat future `regexp-duplicate-named-groups` failures as Js49 blockers.
- Add targeted tests before broadening to all `/d` candidates.

Acceptance:

- Actionable `regexp-match-indices` tests pass in batch mode.
- Existing RegExp baseline has 0 regressions.
- No measurable slowdown on ordinary RegExp tests.

Smoke command:

```bash
./test/test_js_test262_gtest.exe --batch-only \
  --batch-file=temp/js49_es2022_regexp_indices.txt \
  --write-failures=temp/js49_p2_regexp_indices.tsv \
  --gtest_brief=1
```

### P3 - Top-Level Await Module Evaluation

Goal: implement enough ES2022 top-level await to admit actionable TLA tests.

Current actionable set:

- `13` total `top-level-await` tests in the runner's discovered set outside
  baseline.
- `9` actionable after excluding future/proposal overlaps such as
  `import-defer`.

Representative tests:

- `language/expressions/class/cpn-class-expr-accessors-computed-property-name-from-await-expression.js`
- `language/expressions/object/cpn-obj-lit-computed-property-name-from-await-expression.js`
- `language/statements/class/cpn-class-decl-fields-computed-property-name-from-await-expression.js`

Expected implementation areas:

- Parse `await` at module top level under module grammar goals.
- Mark modules that require async evaluation.
- Evaluate module dependency graphs with Promise completion semantics.
- Preserve module namespace identity and caching while async evaluation is
  pending.
- Propagate TLA rejection through dynamic import and module execution.
- Integrate module jobs with the existing Promise/microtask drain used by async
  test262 tests.

Risk controls:

- Keep TLA module tests in `js-module` batches.
- Avoid mixing TLA modules with future `import-defer`, import attributes, JSON
  modules, or source-phase imports.
- Prefer small smoke lists before full module admission.

Acceptance:

- Actionable `top-level-await` tests pass in normal batch execution.
- Dynamic import tests that already pass do not regress.
- Module graph evaluation remains deterministic across repeated runs.

Smoke command:

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --batch-file=temp/js49_es2022_tla.txt \
  --write-failures=temp/js49_p3_tla.tsv \
  --gtest_brief=1
```

### P4 - ES2022 Async Admission

Goal: admit ES2022-tagged async tests that are currently skipped only because
they are not in the async allowlist/baseline path.

Current set:

- `53` ES2022-tagged async tests outside the baseline.
- Dominated by async class/static-private grammar tests, especially
  `class-fields-public` plus `class-static-methods-private`.

Representative examples:

- `language/expressions/class/elements/after-same-line-method-rs-static-async-method-privatename-identifier.js`
- `language/expressions/class/elements/after-same-line-static-method-rs-static-async-method-privatename-identifier.js`

Expected implementation areas:

- Verify these tests pass with existing async batching.
- If they fail, distinguish parser/runtime gaps from allowlist-only skips.
- Add only passing, batch-stable tests to the baseline.

Risk controls:

- Use `--run-async` and an explicit candidate file.
- Keep async chunk size at 50 unless evidence shows these tests need a different
  batch shape.
- Do not admit tests with future/proposal or host-exception feature overlaps.

Acceptance:

- Candidate async tests pass in `js-async` batches.
- No retry-only recoveries.
- No increase in non-fully-passing count.

Smoke command:

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --batch-file=temp/js49_es2022_async_admission.txt \
  --async-chunk-size=50 \
  --write-failures=temp/js49_p4_async.tsv \
  --gtest_brief=1
```

### P5 - ES2022 Module Admission

Goal: admit the remaining ES2022 module/static-initializer case if it passes the
normal module path.

Current set:

- `1` module-flagged ES2022 candidate:
  `language/expressions/class/elements/class-name-static-initializer-default-export.js`

Expected implementation areas:

- Verify class static initialization behavior in module default export context.
- Confirm strict module semantics and export evaluation order.

Acceptance:

- The module candidate passes in `js-module` batch execution.
- No module or dynamic-import regressions.

Command:

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --batch-file=temp/js49_es2022_module_admission.txt \
  --write-failures=temp/js49_p5_module.tsv \
  --gtest_brief=1
```

### P6 - Full ES2022 Release Verification And Baseline Update

Goal: update the baseline only after all Js49 candidates are clean in release.

Work:

- Remove `top-level-await` and `regexp-match-indices` from
  `UNSUPPORTED_FEATURES` only after their actionable tests pass.
- Run the full suite in release with async enabled.
- Update the baseline header scope to ES2022.
- Keep excluded future/host/PTC cases explicit.

Acceptance:

- Existing ES2021 tests: 0 regressions.
- Js49 candidate tests: fully passing in batch mode.
- Failed: 0.
- Non-fully-passing: 0, except any explicitly documented slow-test exception.
- Release runtime stays within 5% of the current baseline unless explained.

Commands:

```bash
make release
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --write-failures=temp/js49_release.tsv \
  --gtest_brief=1

./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --update-baseline \
  --write-failures=temp/js49_update.tsv \
  --gtest_brief=1
```

## 5. Candidate Generation Notes

Candidate generation should use the same discovered test set as
`test/test_js_test262_gtest.cpp`, not every row in `test262_metadata.tsv`.
The metadata file also contains fixture and staging rows that are not part of
the runner's normal 42,295 discovered tests.

Classification rules:

1. Include tests tagged with ES2022 features.
2. Exclude tests already in `test262_baseline.txt`.
3. Exclude tests with future/proposal unsupported features.
4. Exclude tests with Js48 intentional exceptions:
   `tail-call-optimization`, `cross-realm`, `IsHTMLDDA`, `caller`.
5. Split remaining tests by implementation area:
   `regexp-match-indices`, `top-level-await`, async admission, module admission.

All generated manifests must live under `temp/`.

## 6. Regression And Performance Discipline

Every implementation phase should follow this sequence:

1. Build debug and the gtest runner.

```bash
make build
make -C build/premake config=debug_native test_js_test262_gtest -j4
```

2. Run targeted smoke tests.

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --batch-file=temp/js49_<phase>.txt \
  --write-failures=temp/js49_<phase>.tsv \
  --gtest_brief=1
```

3. Run the full debug baseline.

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --write-failures=temp/js49_debug.tsv \
  --gtest_brief=1
```

4. Build release and run full verification.

```bash
make release
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --write-failures=temp/js49_release.tsv \
  --gtest_brief=1
```

5. Update baseline only after release is clean.

Acceptance for every phase:

- 0 failures in the existing baseline.
- 0 regressions versus the previous baseline.
- No new retry-only or slow tests unless explicitly documented.
- No release runtime regression above 5% without explanation.
- No broadening of ES2023+/proposal scope by accident.

## 7. Js49 Milestone Definition

Js49 can claim "full ES2022 support in current js262 scope" when:

- Actionable `regexp-match-indices` tests pass and are admitted.
- Actionable `top-level-await` tests pass and are admitted.
- ES2022-tagged async admission tests pass and are admitted.
- The remaining ES2022 module/static-initializer candidate is admitted or
  explicitly justified.
- `UNSUPPORTED_FEATURES` no longer contains ES2022 features.
- Future/proposal overlaps and Js48 host/PTC exceptions remain explicit.
- The release baseline is updated to ES2022 and remains fully passing.
