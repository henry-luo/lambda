# Transpile_Js46 - Structured Plan To Finish ES2021 test262 Scope

Date: 2026-05-26

Status: proposal

This proposal defines an incremental path from the current stable ES2020
test262 scope to full ES2021 coverage, while preserving the current 100%
passing ES2020 baseline.

The key constraint is stability: the current ES2020 run is clean, so ES2021 work
must be done behind narrow gates, with each step proving that it did not regress
the already-green scope.

## 1. Current Baseline

Latest evidence used for this proposal:

- `test/js262/results/release_run_003/test262_baseline_at_run.txt`
- `test/js262/results/release_run_003/t262_partial_at_run.txt`
- `test/test_js_test262_gtest.cpp`
- local test262 metadata from `test/js262/test`

Release run 003 summary:

| Metric | Count |
|---|---:|
| Total discovered under current runner scope | 42,219 |
| Fully passing baseline | 34,165 |
| Skipped | 8,054 |
| Failed | 0 |
| Partial / non-fully-passing | 3 |

The runner still labels the scope as:

```text
Scope: ES2020 (skip ES2021+ features)
```

However, `test/test_js_test262_gtest.cpp` already supports several ES2021
feature tags. Under the current `UNSUPPORTED_FEATURES` gate, the only ES2021
features still skipped are:

- `WeakRef`
- `FinalizationRegistry`

All other ES2021 feature tags currently shown in the runner's ES2021 section
are already marked supported:

- `AggregateError`
- `logical-assignment-operators`
- `numeric-separator-literal`
- `String.prototype.replaceAll`
- `Promise.any`

## 2. ES2021 Test Inventory

Counting local test262 metadata by feature tag gives:

| ES2021 feature tag | Tests |
|---|---:|
| `numeric-separator-literal` | 159 |
| `logical-assignment-operators` | 108 |
| `Promise.any` | 92 |
| `AggregateError` | 56 |
| `FinalizationRegistry` | 49 |
| `String.prototype.replaceAll` | 41 |
| `WeakRef` | 37 |
| Unique ES2021-tagged tests | 535 |

Relative to `release_run_003`, 177 ES2021-tagged tests are not in the current
baseline. Of those, 82 are the currently unsupported ES2021 weak-reference
tests:

| Still-gated ES2021 feature | Tests | Not in baseline |
|---|---:|---:|
| `FinalizationRegistry` | 49 | 49 |
| `WeakRef` | 37 | 37 |
| Unique still-gated tests | 82 | 82 |

The count is 82 instead of 86 because a few tests carry both tags.

## 3. Goal

Finish ES2021 test262 scope in a way that is:

- root-cause driven, with no hard-coded test workarounds;
- incremental, so each change has a small behavioral surface;
- regression-safe for the 34,165-test ES2020 baseline;
- verified first with debug correctness runs, then with release as the final
  acceptance gate.

The immediate implementation target is the 82 unique `WeakRef` and
`FinalizationRegistry` tests. The broader milestone is to rename the runner
scope from ES2020 to ES2021 only after all ES2021-tagged tests are either
passing or explicitly excluded for a non-edition reason already accepted by the
runner, such as host GC requirements.

## 4. Non-Goals

This work should not attempt to solve post-ES2021 features:

- ES2022 class features already supported by the current engine may remain
  marked supported, but they are not the target of this proposal.
- `Temporal`, `ShadowRealm`, decorators, import attributes, iterator helpers,
  regexp `/v`, resizable ArrayBuffer, and other later features stay out of
  scope.
- Performance tuning is not the focus. Debug builds are used for correctness
  validation. Release builds are used only for final conformance and stability
  verification.

## 5. Risk Model

Weak references touch memory-management semantics, object identity, reachability,
and cleanup scheduling. This is riskier than most built-in method additions
because a wrong implementation can create nondeterminism or disturb GC behavior
outside the weak-reference tests.

The primary regression risks are:

- making ordinary objects unexpectedly reachable or unreachable;
- running cleanup callbacks synchronously when the spec expects job-queue
  scheduling;
- exposing host-GC behavior that the test harness cannot control reliably;
- adding global state that leaks between batched tests;
- changing object layout in ways that destabilize ES2020 object, Map, Set,
  Promise, or class tests.

The design should therefore bias toward narrow runtime hooks and explicit
reset points in the js262 batch harness.

## 6. Implementation Strategy

### P0 - Audit The Test Surface

Before changing runtime code, generate a focused manifest of the 82 tests.

Deliverables:

- `temp/js46_es2021_weakref_tests.tsv`
- feature split by path, negative phase, includes, flags, and host requirements
- list of tests requiring `$262.gc()` or `host-gc-required`

Questions to answer:

- Which tests only check constructor shape, descriptors, `.name`, `.length`,
  prototype wiring, and basic TypeErrors?
- Which tests require observable collection after GC?
- Which tests exercise cleanup callback ordering?
- Which tests are already skipped by `host-gc-required` independent of the
  `WeakRef` / `FinalizationRegistry` feature tags?

Acceptance gate:

```bash
make build
./test/test_js_test262_gtest.exe --batch-only --baseline-only
```

Expected result: 34,165 baseline tests remain fully passing under debug.

### P1 - Add Surface Objects Without GC Semantics

Implement the shape of the ES2021 constructors before making anything weak:

- global `WeakRef`
- `WeakRef.prototype`
- `WeakRef.prototype.deref`
- global `FinalizationRegistry`
- `FinalizationRegistry.prototype`
- `FinalizationRegistry.prototype.register`
- `FinalizationRegistry.prototype.unregister`
- `FinalizationRegistry.prototype.cleanupSome` only if required by the local
  test262 snapshot

Required semantics:

- constructor calls without `new` throw where required;
- prototype and property descriptors match test262 expectations;
- brand checks throw `TypeError` for incompatible receivers;
- `WeakRef` target must be an object or non-registered symbol if the local
  feature set expects symbols-as-weakmap-keys behavior;
- `FinalizationRegistry` cleanup callback must be callable;
- unregister token validation must match the spec.

This phase should not remove `WeakRef` or `FinalizationRegistry` from
`UNSUPPORTED_FEATURES` globally. Instead, run focused tests through the diagnose
or batch-file path until the surface-only subset is green.

Acceptance gates:

```bash
make build
./test/test_js_test262_gtest.exe --batch-only --baseline-only
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js46_es2021_weakref_surface.txt
```

Expected result:

- no ES2020 baseline regressions;
- constructor, descriptor, and brand-check tests pass;
- GC-observable tests remain skipped or failing in the focused manifest until
  P2.

### P2 - Add Weak Cell Storage And `deref`

Introduce an internal weak-cell representation that can be used by both
`WeakRef` and `FinalizationRegistry`.

Design requirements:

- do not use ordinary strong `Item` references for weak targets;
- integrate with the existing Lambda GC root and mark phases deliberately;
- make `WeakRef.prototype.deref()` return the target while it is still live;
- after collection, allow `deref()` to return `undefined`;
- avoid running cleanup callbacks inside the collector itself.

The implementation should be conservative at first: preserve ES2020 stability
even if GC-observable ES2021 tests still need a later cleanup-job phase.

Likely source areas:

- `lambda/lambda-mem.cpp`
- `lambda/lambda-data.hpp`
- `lambda/js/js_runtime.cpp`
- `lambda/js/js_globals.cpp`
- any existing JS job-queue or Promise job scheduling code used by the test
  harness

Acceptance gates:

```bash
make build
./test/test_js_test262_gtest.exe --batch-only --baseline-only
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js46_es2021_weakref_core.txt
```

Expected result:

- ES2020 baseline remains 100%;
- non-GC and basic weak-reference tests pass;
- no new partial-pass entries are introduced.

### P3 - FinalizationRegistry Registration Semantics

Implement registry state:

- held value storage;
- unregister token indexing;
- repeated registration behavior;
- `unregister(token)` return value;
- validation order and abrupt completion behavior.

Important correctness details:

- the held value must not keep the target alive;
- unregister tokens are normal object keys and must be strongly reachable
  through the registry as required;
- the target and held value cannot be the same object if the spec forbids it in
  the relevant edition;
- cleanup callback errors must propagate through the scheduled cleanup job
  behavior expected by the harness.

Acceptance gates:

```bash
make build
./test/test_js_test262_gtest.exe --batch-only --baseline-only
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js46_es2021_finalization_registry.txt
```

Expected result:

- all registration and unregister tests pass;
- tests requiring actual collection are isolated for P4;
- ES2020 baseline remains 100%.

### P4 - Cleanup Job Scheduling And Harness GC

Handle observable finalization carefully. This is the most nondeterministic
phase and should be kept separate from constructor and registry state work.

Tasks:

- identify how `$262.gc()` is currently handled in js262 runs;
- decide whether Lambda can expose a deterministic test-only GC hook for js262;
- enqueue cleanup jobs after collection, not during collection;
- flush cleanup jobs at the same safe points used for Promise jobs if that
  matches the test262 harness behavior;
- reset weak-reference and finalization state between batched tests.

If some tests require host GC behavior that cannot be made deterministic, keep
them skipped under `host-gc-required` rather than pretending ES2021 is fully
observable in a way the engine cannot guarantee.

Acceptance gates:

```bash
make build
./test/test_js_test262_gtest.exe --batch-only --baseline-only
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js46_es2021_gc_observable.txt
```

Expected result:

- deterministic GC-observable tests pass;
- nondeterministic host-GC tests are explicitly accounted for;
- no baseline regressions and no new flaky partial entries.

### P5 - Open The ES2021 Gate

Only after P1-P4 are stable:

- remove `WeakRef` and `FinalizationRegistry` from `UNSUPPORTED_FEATURES`;
- update the scope label from ES2020 to ES2021;
- run the full debug suite with the expanded scope;
- update the baseline only through the runner's `--update-baseline` gate.

Acceptance gates:

```bash
make build
./test/test_js_test262_gtest.exe --batch-only --update-baseline
./test/test_js_test262_gtest.exe --batch-only --baseline-only
```

Expected result:

- ES2021 expanded debug run has no regressions;
- newly passing ES2021 entries are admitted only through the normal update gate;
- `t262_partial.txt` does not grow except for explicitly justified
  nondeterministic host-GC tests.

### P6 - Final Release Verification

After the debug suite is green, rebuild and verify release.

Acceptance gates:

```bash
make release
./test/test_js_test262_gtest.exe --batch-only --baseline-only
./test/test_js_test262_gtest.exe --batch-only --update-baseline
```

Expected result:

- full release js262 run passes the expanded ES2021 baseline;
- failed count is 0;
- slow-test list is empty or unchanged for known non-ES2021 reasons;
- memory and timing artifacts are captured under `test/js262/results/`;
- release baseline count and scope header reflect ES2021.

## 7. Regression Discipline

Every implementation PR or commit should follow this order:

1. Add the smallest focused ES2021 test batch manifest for the feature slice.
2. Implement the minimum runtime surface needed for that slice.
3. Run the debug ES2020 baseline gate.
4. Run the focused ES2021 batch.
5. Run a full debug js262 batch before removing any unsupported-feature gate.
6. Only then run release verification.

No change should be accepted if it regresses the current 34,165 passing baseline,
even if it makes ES2021 tests pass.

If a failure appears in an unrelated ES2020 test, stop and find the root cause.
Do not compensate by adding skip-list entries, changing expected results, or
special-casing test names.

## 8. Batch And State Isolation Requirements

Weak-reference tests are especially sensitive to state leaking between tests.
The runner should be audited for:

- whether each batched test starts with clean JS global state;
- whether queued Promise or cleanup jobs are flushed between tests;
- whether FinalizationRegistry cells from one test can survive into another;
- whether forced GC hooks run at deterministic points;
- whether memory instrumentation changes object lifetime.

If needed, add explicit test-batch cleanup hooks in the JS runtime rather than
relying on process exit. The goal is to keep the existing batch runner fast while
preventing weak-reference state from becoming a new flake source.

## 9. Deliverables

Implementation is complete when these are true:

- `WeakRef` is implemented with correct constructor, prototype, brand, and
  `deref` behavior.
- `FinalizationRegistry` is implemented with correct constructor, prototype,
  registration, unregister, held-value, and cleanup scheduling behavior.
- `WeakRef` and `FinalizationRegistry` are removed from the ES2021 unsupported
  feature gate.
- The runner scope is renamed from ES2020 to ES2021.
- Full debug js262 expanded-scope run passes.
- Full release js262 expanded-scope run passes.
- No ES2020 baseline regressions are introduced.

## 10. Suggested First Patch

Start with a no-runtime-change audit patch:

- generate `temp/js46_es2021_weakref_tests.tsv`;
- split it into surface, core weak-ref, registry, and GC-observable manifests;
- add a short note to the next run report explaining the exact expected number
  of tests unlocked by removing each feature gate.

Then implement P1. That gives an early correctness win with minimal GC risk and
keeps the scary part, finalization after collection, isolated until the surface
area is already proven.
