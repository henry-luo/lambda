# Transpile_Js47 - Incremental Plan To Improve LambdaJS Async Support

Date: 2026-05-26

Status: proposal

This proposal defines an incremental path to improve async support in the
Lambda JavaScript runtime and test262 runner without destabilizing the current
green ES2021 baseline.

The current baseline is strong:

- Scope: ES2021, skipping ES2022+ features.
- Fully passing baseline: 34,245 / 34,245.
- `test/js262/t262_partial.txt`: empty after the latest release update.
- Release verification: 34,245 fully passing, 0 regressions.

Async is now one of the largest remaining test262 gates. The goal is not to
remove that gate in one large step. The goal is to admit async tests in small,
measured groups, prove each group under debug first, and finish with release
verification.

## 1. What The `async` Flag Means

In test262, `flags: [async]` means the test is not complete when top-level
script execution returns. The test completes by calling `$DONE()` or
`$DONE(error)`.

The current runner parses this metadata flag and skips the test before runtime
execution:

```text
test/test_js_test262_gtest.cpp:1781
if (meta.is_async) { skip "async flag"; continue; }
```

There is already a small `$DONE` assembly hook in `assemble_test_source`, but it
is not used by the normal baseline path because async tests are filtered out
before source assembly.

## 2. Current Async Inventory

Local evidence:

- `temp/test262_metadata.tsv`
- `test/js262/test262_baseline.txt`
- `test/js262/t262_partial.txt`
- `test/test_js_test262_gtest.cpp`
- discovered runner scope: 42,295 tests

Counts under the current runner categories:

| Metric | Count |
|---|---:|
| Discovered test files | 42,295 |
| Fully passing ES2021 baseline | 34,245 |
| Currently skipped by `async flag` | 5,378 |
| Async-flagged metadata rows in full cache | 5,558 |
| Async-flagged tests otherwise in ES2021/ES2022 scope | 5,018 |
| Async-flagged tests blocked by module flag | 47 |
| Async-flagged tests blocked by unsupported feature tags | 313 |

The biggest async-skipped areas are:

| Area | Async-skipped tests |
|---|---:|
| `language/statements` | 2,504 |
| `language/expressions` | 2,218 |
| `built-ins/Promise` | 358 |
| `built-ins/Array` | 90 |
| `built-ins/Atomics` | 65 |

The largest feature clusters among otherwise in-scope async tests are:

| Feature tag | Tests |
|---|---:|
| `async-iteration` | 3,842 |
| `destructuring-binding` | 1,132 |
| `class` | 995 |
| `Symbol.iterator` | 928 |
| `class-static-methods-private` | 580 |
| `Symbol.asyncIterator` | 532 |
| `generators` | 474 |
| `class-methods-private` | 464 |

This means the async flag is not just Promise basics. Most of the skipped
surface is async iteration, for-await-of, async generators, destructuring, and
class interactions.

## 3. Current Runtime Capabilities

LambdaJS already has several async building blocks:

- Promise runtime and `Promise.prototype.then`.
- Promise combinators such as `all`, `race`, `any`, and `allSettled`.
- Microtask and nextTick queues.
- `AsyncFunction` prototype wiring.
- Async function flags.
- Async/await state-machine runtime.
- Async generator related class/prototype scaffolding.

Important entry points:

| Area | File |
|---|---|
| Promise runtime | `lambda/js/js_runtime.cpp` |
| Microtask/event loop | `lambda/js/js_event_loop.cpp` |
| Async function marking | `lambda/js/js_runtime_function.cpp` |
| AsyncFunction prototype | `lambda/js/js_globals.cpp` |
| Await lowering | `lambda/js/js_mir_expression_lowering.cpp` |
| Async return lowering | `lambda/js/js_mir_statement_lowering.cpp` |
| Early errors for await | `lambda/js/js_early_errors.cpp` |

However, current support is not test262-complete:

- The normal runner does not execute or grade async tests.
- `$DONE` currently has no full per-test completion state machine in the batch
  path.
- Promise reaction storage has visible fixed-capacity behavior, such as the
  `then_count < 8` path.
- Top-level await remains tied to module execution, which is separately skipped.
- Async iteration and async generator semantics need focused verification.

## 4. Goals

1. Add reliable test262 async execution support without admitting all async
   tests at once.
2. Preserve the current 34,245-test ES2021 baseline at 100%.
3. Improve runtime async semantics only where focused tests expose root causes.
4. Keep changes incremental and reversible by feature group.
5. Validate every phase under debug first, then release.

## 5. Non-Goals

This proposal does not try to solve every future async-related proposal:

- `Array.fromAsync` remains ES2026+ and stays out of scope.
- `promise-try`, iterator helpers, import attributes, source phase imports,
  import defer, and other later features stay out of scope.
- Full module support and top-level await are not the first milestone.
- Performance testing must not use debug builds. Debug is for correctness only.

## 6. Risk Model

Async support has high regression risk because it touches:

- Promise settlement ordering;
- microtask flushing;
- exception propagation across async boundaries;
- test batching isolation;
- timers and event-loop draining;
- state-machine lowering for await and for-await-of;
- GC rooting of pending callbacks, promises, and async contexts.

The main failure modes to avoid are:

- tests that pass only because `$DONE` is ignored;
- tests that hang forever waiting for `$DONE`;
- async callbacks from one batched test mutating a later test;
- unbounded Promise reaction storage;
- microtasks flushed too early or too late;
- baseline updates that accidentally admit flaky async tests.

## 7. Implementation Strategy

### P0 - Build Async Test Inventory

Generate focused manifests before changing runtime behavior.

Deliverables:

- `temp/js47_async_all.tsv`
- `temp/js47_async_es2021_in_scope.tsv`
- `temp/js47_async_promise_basic.txt`
- `temp/js47_async_function_basic.txt`
- `temp/js47_async_iteration.txt`
- `temp/js47_async_generator.txt`

Each row should include:

- test name;
- canonical test path;
- flags;
- feature tags;
- includes;
- negative phase/type;
- whether it also carries unsupported feature tags.

Acceptance gate:

```bash
make build
./test/test_js_test262_gtest.exe --batch-only --baseline-only --write-failures=temp/js47_p0_baseline.tsv
```

Expected result:

- 34,245 / 34,245 fully passing under debug;
- 0 regressions;
- no baseline update yet.

### P1 - Add Async Runner Mode Behind A Narrow Gate

Add explicit async execution support to the runner without changing the default
baseline scope.

Proposed runner controls:

- `--run-async` to allow `flags: [async]` tests.
- `--async-list=<path>` to restrict async admission to a manifest.
- `--async-timeout=<seconds>` with a low default, clamped like `--js-timeout`.

Required semantics:

- Define `$DONE` per test, not globally for the whole batch.
- Track a per-test completion record:
  - pending at start;
  - pass on `$DONE()` with no argument;
  - fail on `$DONE(error)`;
  - fail on uncaught sync error;
  - fail on timeout if `$DONE` was never called.
- Drain microtasks and timers until completion or timeout.
- Reject late `$DONE` calls from a previous test once the test has ended.
- Keep async tests out of the ordinary batch path until isolation is proven.

Early implementation should run async tests individually or in a special batch
mode with hard isolation. Throughput is less important than correctness at this
stage.

Acceptance gates:

```bash
make build
./test/test_js_test262_gtest.exe --batch-only --baseline-only --write-failures=temp/js47_p1_baseline.tsv
./test/test_js_test262_gtest.exe --batch-only --run-async --async-list=temp/js47_async_smoke.txt --jobs=1 --write-failures=temp/js47_p1_async_smoke.tsv
```

Expected result:

- baseline remains 100%;
- async smoke tests are graded by `$DONE`, not by synchronous script exit;
- async failures produce clear TSV rows.

### P2 - Admit Promise And Microtask Basics

Start with the smallest async tests that validate Promise scheduling and `$DONE`
behavior without async functions or async iteration.

Candidate groups:

- `built-ins/Promise` async tests that do not require unsupported features;
- tests involving `Promise.resolve`, `Promise.reject`, `then`, `catch`,
  `finally`, `all`, `race`, `any`, and `allSettled`;
- microtask ordering tests that do not need modules or host hooks.

Runtime work likely needed:

- Replace fixed `then_count < 8` reaction storage with dynamically growable
  storage using project-local containers or GC-managed arrays.
- Audit Promise reaction job ordering against the existing microtask queue.
- Ensure thenable assimilation schedules jobs asynchronously where required.
- Ensure rejected promise handling does not leak exception state into the next
  test.

Acceptance gates:

```bash
make build
./test/test_js_test262_gtest.exe --batch-only --baseline-only --write-failures=temp/js47_p2_baseline.tsv
./test/test_js_test262_gtest.exe --batch-only --run-async --async-list=temp/js47_async_promise_basic.txt --jobs=1 --write-failures=temp/js47_p2_promise.tsv
```

Exit criteria:

- all admitted Promise async tests either fully pass or are moved to a focused
  failure manifest with root-cause categories;
- no broad baseline update until the admitted set is stable.

### P3 - Admit Async Function And Await Basics

Next, target async functions without async iteration.

Candidate groups:

- `async function` returns a Promise;
- return value adoption;
- thrown exception becomes rejected Promise;
- `await` non-Promise values;
- `await` fulfilled and rejected Promises;
- multiple awaits in one function;
- await inside `try`, `catch`, and `finally`;
- `AsyncFunction` constructor shape and prototype tests.

Runtime work likely needed:

- Audit `js_async_must_suspend` for thenables and pending Promises.
- Verify async context lifetime and GC rooting.
- Verify state-machine resume restores lexical locals correctly.
- Verify rejected await enters the nearest catch/finally path correctly.
- Verify `return await` and returning a Promise both use the Promise resolution
  procedure.

Acceptance gates:

```bash
make build
./test/test_js_test262_gtest.exe --batch-only --baseline-only --write-failures=temp/js47_p3_baseline.tsv
./test/test_js_test262_gtest.exe --batch-only --run-async --async-list=temp/js47_async_function_basic.txt --jobs=1 --write-failures=temp/js47_p3_async_fn.tsv
```

### P4 - Admit Async Generators And Async Iteration

This is the largest skipped surface and should come only after Promise and
async function basics are stable.

Candidate groups:

- `for await (...)`;
- async generator object shape;
- `AsyncGeneratorFunction`;
- `AsyncGeneratorPrototype`;
- `AsyncFromSyncIteratorPrototype`;
- `Symbol.asyncIterator`;
- iterator close on abrupt completion;
- destructuring in async iteration bindings.

Runtime work likely needed:

- Ensure async generator `next`, `return`, and `throw` methods return Promises.
- Ensure queued async generator requests are ordered correctly.
- Ensure `for-await-of` performs `AsyncIteratorClose` correctly.
- Ensure sync iterables are wrapped through AsyncFromSyncIterator semantics.
- Verify `yield`, `await`, and `yield await` ordering.

Acceptance gates:

```bash
make build
./test/test_js_test262_gtest.exe --batch-only --baseline-only --write-failures=temp/js47_p4_baseline.tsv
./test/test_js_test262_gtest.exe --batch-only --run-async --async-list=temp/js47_async_iteration.txt --jobs=1 --write-failures=temp/js47_p4_async_iteration.tsv
./test/test_js_test262_gtest.exe --batch-only --run-async --async-list=temp/js47_async_generator.txt --jobs=1 --write-failures=temp/js47_p4_async_generator.tsv
```

### P5 - Promote Stable Async Tests Into Baseline

Only after focused async groups are stable should the runner admit them into the
ordinary baseline update flow.

Promotion rules:

- A test must pass in debug focused mode.
- It must pass in debug full-suite mode with async enabled for its manifest.
- It must not be slow, batch-lost, crashy, or timing-sensitive.
- It must pass under release before becoming part of the permanent baseline.

Do not use one global "remove async skip" change. Promote by manifest:

- `Promise` basics;
- async function basics;
- async generators;
- async iteration;
- remaining language edge cases.

Acceptance gates:

```bash
make build
./test/test_js_test262_gtest.exe --batch-only --baseline-only --write-failures=temp/js47_p5_debug_baseline.tsv
./test/test_js_test262_gtest.exe --batch-only --run-async --async-list=temp/js47_promote_manifest.txt --update-baseline --write-failures=temp/js47_p5_debug_update.tsv
make release
./test/test_js_test262_gtest.exe --batch-only --baseline-only --write-failures=temp/js47_p5_release_verify.tsv
```

If `test/js262` is still a symlink to `../lambda-test/js262`, baseline updates
must be run with permission to write through that symlink.

## 8. Test And Build Discipline

For every implementation phase:

1. Build with debug:

```bash
make build
make build-test
```

2. Verify the current baseline:

```bash
./test/test_js_test262_gtest.exe --batch-only --baseline-only --write-failures=temp/js47_debug_baseline.tsv
```

3. Run the focused async manifest:

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async --async-list=<manifest> --jobs=1 --write-failures=temp/js47_async_focus.tsv
```

4. Only after debug is clean, perform release verification:

```bash
make release
./test/test_js_test262_gtest.exe --batch-only --baseline-only --write-failures=temp/js47_release_baseline.tsv
```

Debug builds are for correctness. Release builds are for final conformance and
performance-sensitive verification.

## 9. Success Metrics

Short-term success:

- Async smoke tests execute through `$DONE`.
- Failure artifacts clearly distinguish timeout, `$DONE(error)`, uncaught sync
  exception, and Promise rejection.
- Current ES2021 baseline remains 34,245 / 34,245.

Medium-term success:

- Promise async tests become baseline-eligible.
- Async function and await basics become baseline-eligible.
- No stale callbacks leak across tests.
- No partial list growth from async instability.

Long-term success:

- The 5,018 otherwise in-scope async tests are no longer skipped solely because
  of the async flag.
- Remaining async skips have specific non-async reasons, such as modules,
  future feature tags, host hooks, or known runtime gaps.
- Release verification remains 100% for the expanded baseline.

## 10. Recommended First Patch

The first code patch should not touch async lowering. It should only add runner
infrastructure:

1. Add `--run-async`.
2. Add `--async-list=<path>`.
3. Keep async tests skipped unless both controls admit them.
4. Add per-test `$DONE` completion state.
5. Add timeout and late-callback diagnostics.
6. Run a tiny manually curated smoke manifest.

This keeps the runtime unchanged while proving that the harness can tell the
difference between:

- synchronous pass;
- async pass after microtasks;
- async timeout;
- `$DONE(error)`;
- uncaught exception before `$DONE`.

Once the harness can grade async tests honestly, runtime fixes can proceed
feature by feature without risking the existing ES2021 baseline.
