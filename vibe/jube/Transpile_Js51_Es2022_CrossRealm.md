# Transpile_Js51_Es2022_CrossRealm - Proposal For Remaining ES2022 Scope

Date: 2026-05-30

Status: proposed

Js51 targets the remaining clean ES2022-era gaps after the Js50 ES2023
baseline update. The goal is deliberately narrow: cover the 11 ES2022
cross-realm tests that are currently excluded by the intentional
`cross-realm` exception, plus 1 ES2022 async test that is not yet admitted.

This is not a proposal for full browser-like realm support, ShadowRealm, or
iframe/subframe behavior. It is a test262 host-runtime phase for the specific
`$262.createRealm()` semantics needed by the current ES2022 skipped set.

## 1. Current Baseline

Current checked-in release baseline:

```text
# Scope: ES2023 (skip ES2024+ features)
# Total passing: 39246
# Total tests: 42295  Skipped: 3049  Batched: 39246  Passed: 39246  Failed: 0
# Runtime: 148.2s total (prep 0.0s + exec 148.1s)
# Batch size: batched 50 tests/process; async 50 tests/process
```

ES2021 and ES2022 status against this baseline:

| Era | Tagged discovered | In baseline | Outside baseline | Clean product gap |
|---|---:|---:|---:|---:|
| ES2021 | 510 | 503 | 7 | 0 |
| ES2022 | 5,109 | 5,083 | 26 | 12 |

The ES2021 outside-baseline tests are still intentional host/browser
exceptions (`cross-realm` and `IsHTMLDDA`). Js51 only targets the ES2022
cross-realm subset plus the one async-admission case.

## 2. Target Test Set

### Cross-Realm Tests

These 11 tests are ES2022-tagged and currently outside the baseline only
because they require `cross-realm` behavior:

```text
language_expressions_class_private_setter_brand_check_multiple_evaluations_of_class_realm_js
language_expressions_class_private_static_getter_multiple_evaluations_of_class_realm_js
language_expressions_class_private_getter_brand_check_multiple_evaluations_of_class_realm_function_ctor_js
language_expressions_class_private_setter_brand_check_multiple_evaluations_of_class_realm_function_ctor_js
language_expressions_class_private_static_method_brand_check_multiple_evaluations_of_class_realm_js
language_expressions_class_private_method_brand_check_multiple_evaluations_of_class_realm_function_ctor_js
language_expressions_class_private_static_setter_multiple_evaluations_of_class_realm_js
language_expressions_class_private_static_field_multiple_evaluations_of_class_realm_js
language_expressions_class_private_getter_brand_check_multiple_evaluations_of_class_realm_js
language_expressions_class_private_method_brand_check_multiple_evaluations_of_class_realm_js
built_ins_RegExp_prototype_hasIndices_cross_realm_js
```

Feature distribution:

| Feature | Tests |
|---|---:|
| `class-methods-private` | 6 |
| `class-static-methods-private` | 4 |
| `class-static-fields-private` | 1 |
| `regexp-match-indices` | 1 |

The feature counts overlap by test where metadata has multiple tags.

### Async Admission Test

This ES2022-tagged test is outside the baseline because it is async-flagged,
not because of a later feature dependency:

```text
language_expressions_class_elements_after_same_line_method_rs_static_async_method_privatename_identifier_alt_js
```

Metadata:

```text
features: class-static-methods-private; class; class-fields-public
flags: async, generated
includes: propertyHelper.js
```

## 3. Existing Runtime Situation

`$262.createRealm()` already exists, but it is shallow. It currently creates a
new object for `realm.global`, copies constructor functions from the current
runtime into that object, and installs `globalThis`, `self`, and `window`
self-references.

That shape is enough for tests to access `realm.global`, but it is not enough
for cross-realm semantics:

- `realm.global.eval` does not currently evaluate against that realm's own
  global object and intrinsics.
- Constructors and prototypes are shared, not realm-specific.
- Error constructors such as `realm.global.TypeError` are not distinct
  intrinsics.
- `GetFunctionRealm(newTarget)` behavior is incomplete for constructor paths
  that need default prototypes from the new target's realm.
- Built-in prototype identity checks, such as `%RegExpPrototype%`, cannot yet
  distinguish current-realm from other-realm prototypes.

## 4. Scope Decision

Include in Js51:

1. A limited real realm model for `$262.createRealm()`.
2. Realm-specific global objects with the intrinsic constructors/prototypes
   needed by the 11 target tests.
3. `realm.global.eval` that evaluates source in that realm.
4. Realm-aware error construction for the target private-brand and RegExp
   cases.
5. Realm-aware default prototype lookup for constructor/newTarget cases that
   show up in the target tests.
6. Admission of the one ES2022 async test through the normal async allowlist
   path, only if it passes normal batch execution.

Explicitly exclude from Js51:

- Full browser realm behavior, iframes, window/document separation, or WPT
  cross-realm/subframe coverage.
- ShadowRealm.
- ES2024+ and proposal overlaps such as `import-defer`,
  `regexp-duplicate-named-groups`, `regexp-v-flag`,
  `explicit-resource-management`, `nonextensible-applies-to-private`, and
  `resizable-arraybuffer`.
- Removing the global `cross-realm` exception for all test262 tests. Js51 may
  allowlist these 11 tests first; broad removal should wait for a separate
  audit.

## 5. Phase Plan

### P1 - Manifest And Baseline Guard

Goal: create repeatable focused manifests before touching runtime behavior.

Work:

- Write the cross-realm target list to `temp/js51_es2022_cross_realm.txt`.
- Write the async target to `temp/js51_es2022_async_admission.txt`.
- Run the current release baseline with async enabled.
- Run the focused manifests and capture the current failure modes.

Acceptance:

- Current baseline remains 0 failures, 0 regressions, and 0
  non-fully-passing tests.
- Focused failure logs clearly distinguish missing realm behavior from unrelated
  regressions.

Commands:

```bash
make release
./test/test_js_test262_gtest.exe --batch-only --baseline-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --write-failures=temp/js51_p1_baseline_guard.tsv \
  --gtest_brief=1
```

### P2 - Realm Object And Realm Eval

Goal: make `$262.createRealm().global.eval` evaluate source against the new
realm's global object.

Expected implementation areas:

- Add a small `JSRealm` runtime structure or equivalent internal association.
- Associate each realm with its global object.
- Add a realm-bound eval function installed as `realm.global.eval`.
- Ensure eval-created classes/functions retain enough realm identity for error
  construction and private-brand tests.
- Keep normal global eval behavior unchanged for the primary realm.

Risk controls:

- Do not change module evaluation in this phase.
- Do not let realm eval leak variables into the primary `globalThis`.
- Preserve the existing js262 baseline before admitting new tests.

### P3 - Realm-Specific Intrinsics Needed By Target Tests

Goal: provide distinct intrinsics for the target realm checks without building
an unrestricted browser-style realm system.

Expected implementation areas:

- Realm-specific `TypeError` and other error constructors used by
  `assert.throws(realm.global.TypeError, ...)`.
- Realm-specific `Function` sufficient for the target `Function` constructor
  eval cases.
- Realm-specific `RegExp.prototype` identity for the `hasIndices` cross-realm
  getter test.
- Realm-aware default prototype selection for constructor paths that use
  `newTarget` from another realm.

Risk controls:

- Start with only constructors/prototypes exercised by the target tests.
- Avoid replacing global constructor caches globally unless they become
  explicitly realm-indexed.
- Add targeted LambdaJS unit tests for realm identity before updating js262.

### P4 - Focused Cross-Realm Admission

Goal: pass the 11 target cross-realm tests in normal batch execution.

Work:

- Temporarily allowlist the 11 target paths despite the `cross-realm` tag, or
  split the intentional exception so these exact tests are admitted.
- Run the focused manifest in debug and release.
- Confirm failures are 0 and no target test is partial, slow, or retry-only.

Smoke command:

```bash
./test/test_js_test262_gtest.exe --batch-only \
  --batch-file=temp/js51_es2022_cross_realm.txt \
  --write-failures=temp/js51_p4_cross_realm.tsv \
  --gtest_brief=1
```

Acceptance:

- 11 / 11 target tests pass in batch mode.
- `cross-realm` remains skipped for non-target tests until a later full audit.

### P5 - Async Admission

Goal: admit the one ES2022 async test if it passes normal async batch
execution.

Work:

- Add the test to the async allowlist path used for baseline updates, or rely on
  `--batch-file` as the focused async allowlist.
- Run it with `--run-async`.

Smoke command:

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --batch-file=temp/js51_es2022_async_admission.txt \
  --write-failures=temp/js51_p5_async.tsv \
  --gtest_brief=1
```

Acceptance:

- 1 / 1 target async test passes in batch mode.
- No change to async behavior for unrelated skipped async tests.

### P6 - Release Baseline Update

Goal: update the baseline only after the focused scope and the existing
baseline are both clean.

Commands:

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --write-failures=temp/js51_p6_release_guard.tsv \
  --gtest_brief=1

./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --update-baseline \
  --write-failures=temp/js51_p6_update_baseline.tsv \
  --gtest_brief=1
```

Expected result if all Js51 targets land:

```text
Fully passed: 39258 / 39258
Improvements: 12
Regressions: 0
Skipped: 3037
```

The exact runtime may shift, but the update gate must still report 0 failures,
0 regressions, and 0 non-fully-passing tests.

## 6. Completion Criteria

Js51 is complete when:

- The 11 ES2022 cross-realm tests pass in normal batch execution.
- The 1 ES2022 async-admission test passes in normal async batch execution.
- The release baseline grows from 39,246 to 39,258 fully passing tests.
- `t262_partial.txt` remains empty after the final update.
- `cross-realm` is still treated as an intentional exception for tests outside
  the Js51 allowlist.
