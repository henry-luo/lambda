# Transpile_Js48_Es2021 - Plan To Close Remaining ES2021 JS262 Gaps

Date: 2026-05-28

Status: proposal

This proposal defines an incremental path to complete the remaining ES2021
test262 support in LambdaJS while preserving the current passing js262 baseline
for correctness and performance.

The rule for this work is conservative: do not admit a larger test surface by
weakening correctness gates. Each phase must keep the existing baseline fully
passing, compare runtime against the current release baseline, and update the
baseline only after a clean release verification.

## 1. Current Baseline And Caveat

The current checked-in baseline header still reflects an older async execution
mode:

```text
# Total tests: 42295  Skipped: 3356  Batched: 38941  Passed: 38939  Failed: 0
# Runtime: 805.1s total (prep 0.1s + exec 804.2s)
# Phase timing: batch-execute-batched 127.1s
# Phase timing: batch-execute-non-batched 676.4s
```

That header is useful evidence for the previous performance problem, but it is
not the latest async-batching behavior. Js47 has since promoted async batching
to 50 tests per process. The latest full release verification with
`--async-chunk-size=50` produced:

| Metric | Latest release run |
|---|---:|
| Discovered tests | 42,295 |
| Skipped tests | 3,356 |
| Baseline passing tests | 38,939 |
| Failed | 0 |
| Regressions | 0 |
| Non-fully-passing | 0 |
| Batches | 780 |
| Total time | 177.1s |
| Batched execute time | 175.8s |
| Non-batched execute time | 0.0s |

Action: after Js48 work starts, regenerate the release baseline so the file
header documents the async-batch-50 timing rather than the older non-batched
async timing.

## 2. Outstanding Test Inventory

Against the runner's current ES2021 scope, 3,356 discovered tests are not in the
fully passing baseline. The exclusive skip/reject reasons are:

| Reason | Count | ES2021 relevance |
|---|---:|---|
| Unsupported future/proposal feature | 2,888 | Mostly not ES2021 |
| Async tests not yet admitted | 322 | Mostly dynamic import |
| Module flag | 86 | ES2015 modules and dynamic import coverage |
| Raw flag | 30 | Some ES2023 hashbang; some ES5/Annex B parser tests |
| Missing metadata cache | 25 | New Promise `allKeyed` proposal tests, not ES2021 |
| Manual skip list | 3 | Randomness / Unicode data-version exceptions |
| Partial slow pass | 2 | ES5 URI exhaustive tests, pass but slow |

The future/proposal feature bucket is dominated by non-ES2021 work:

| Feature | Count |
|---|---:|
| `resizable-arraybuffer` | 460 |
| `iterator-helpers` | 393 |
| `regexp-modifiers` | 230 |
| `source-phase-imports` | 218 |
| `explicit-resource-management` | 205 |
| `regexp-v-flag` | 187 |
| `cross-realm` | 183 |
| `import-defer` | 128 |
| `Atomics.waitAsync` | 101 |
| `Array.fromAsync` | 95 |

The practical ES2021 closure set is smaller:

| Area | Count | Why it matters |
|---|---:|---|
| Dynamic import async tests | 322 | ES2020 feature; major remaining in-scope gap |
| Module-flag tests without unsupported future features | 62 | ES2015 modules, import/export semantics, dynamic import dependencies |
| Raw parser-mode tests without unsupported future features | 30 | Includes 8 ES5/Annex B parser tests and 22 hashbang tests |
| Slow URI partial tests | 2 | Already pass, but exceed the fully-passing time gate |
| Tail-call optimization | 35 | ES2015 spec feature; optional policy decision in practice |
| Cross-realm harness support | 183 | Test harness/runtime capability, not a single JS syntax feature |
| Browser Annex B host quirks | about 58 | `IsHTMLDDA` and `caller`; likely not Lambda core unless explicitly targeted |

## 3. Scope Decision

For a credible "full ES2021" milestone, Js48 should include:

1. Dynamic import in script code.
2. Enough module loading to execute module-flagged ES2021 tests.
3. Raw mode support for ES5/Annex B parser tests that are not future features.
4. A decision and implementation path for the two slow URI tests.

Js48 should explicitly exclude from the ES2021 milestone:

- ES2022+ and proposal features such as resizable ArrayBuffer, iterator helpers,
  regexp modifiers, explicit resource management, source-phase imports, import
  defer, and Array.fromAsync.
- Browser-only host behavior unless the project decides LambdaJS should model a
  browser host.

Proper tail calls and cross-realm support need explicit product decisions:

- Proper tail calls are in ES2015, but many production JS engines do not support
  them. If LambdaJS wants strict ES2021 spec coverage, implement them. If not,
  document them as an intentional unsupported ES2015 feature.
- Cross-realm support is valuable for conformance but introduces runtime and
  memory-model surface. Treat it as its own phase after module/dynamic import.

## 4. Phase Plan

### P1 - Refresh Baseline Timing And Guards

Goal: start from a trustworthy timing baseline.

Work:

- Rebuild release.
- Run the full js262 suite with async batch size 50.
- Update the baseline so the header records the current batched async runtime.
- Keep the old 805.1s / 676.4s non-batched async number documented here as the
  pre-optimization reference, not as current performance.

Acceptance:

- 38,939 / 38,939 fully passing.
- 0 failures.
- 0 regressions.
- 0 non-fully-passing tests.
- Total release runtime should remain near the latest 177.1s result.

Commands:

```bash
make release
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --write-failures=temp/js48_p1_release.tsv \
  --gtest_brief=1
```

### P2 - Dynamic Import Support

Goal: admit the 322 currently skipped async dynamic-import tests that do not
require future features.

Current skipped examples:

- `language/expressions/dynamic-import/await-import-evaluation.js`
- `language/expressions/dynamic-import/custom-primitive.js`
- `language/expressions/dynamic-import/eval-rqstd-once.js`
- `language/expressions/dynamic-import/import-errored-module.js`
- `language/expressions/dynamic-import/reuse-namespace-object.js`

Expected implementation areas:

- Parser and AST path for `import(specifier)` expression if any edge cases are
  still missing.
- Runtime loader hook that returns a Promise.
- Module resolution for test262's relative module fixtures.
- Module namespace object creation and caching.
- Error propagation through Promise rejection.
- Interaction with the existing microtask drain and `$DONE` completion path.

Implementation constraints:

- Do not special-case test262 file names.
- Do not fake dynamic import with synchronous require semantics; the observable
  result must be Promise-based.
- Keep dynamic import disabled for unsupported future import forms such as
  import attributes, source-phase imports, import defer, JSON modules, import
  text, and import bytes.

Incremental test strategy:

1. Create a small `temp/js48_dynamic_import_smoke.txt` allowlist with 5 to 10
   basic dynamic-import tests.
2. Run the smoke list in debug with `--run-async`.
3. Expand to all dynamic-import tests that have no unsupported feature tags.
4. Run the full debug baseline.
5. Run full release before admitting new passing tests.

Acceptance:

- No existing baseline regressions.
- Dynamic import successes are fully passing in batch mode, not retry-only.
- Failure modes reject Promises with the expected error type.
- No measurable slowdown to the existing 38,939-test baseline.

### P3 - Module Runner Support

Goal: admit module-flagged ES2021 tests that do not require future feature tags.
There are 62 such tests in the current discovered set.

Current examples:

- `language/expressions/dynamic-import/eval-export-dflt-cls-anon.js`
- `language/expressions/dynamic-import/eval-export-dflt-expr-fn-named.js`
- `language/expressions/dynamic-import/eval-self-once-module.js`

Expected implementation areas:

- Parse module source under module grammar goals.
- Support import/export declarations needed by the tests.
- Create and link a module graph.
- Evaluate modules with correct namespace object identity and caching.
- Preserve strict-mode module semantics.
- Integrate module jobs with the same microtask drain used by async tests.

Risk controls:

- Keep script tests and module tests in separate batch groups at first.
- Add explicit `js-module` batch kind logging.
- Use a separate chunk-size knob if module jobs show different memory or timing
  behavior.
- Do not mix script dynamic import and module-flagged tests until both are clean
  separately.

Acceptance:

- 0 regressions to script baseline.
- Module tests pass in normal batch execution.
- Total runtime does not regress beyond an agreed threshold, suggested 5%.

### P4 - Raw Parser-Mode Tests

Goal: support the ES2021-relevant `raw` tests without accidentally admitting
future hashbang-only tests as ES2021 blockers.

Current raw skips:

- 22 hashbang tests under `language/comments/hashbang` are ES2023 and should
  remain outside the ES2021 milestone.
- 5 directive-prologue raw tests and 3 Annex B HTML comment raw tests are
  ES2021-relevant.

Expected implementation areas:

- Add a raw-source execution path that does not prepend the standard harness
  before parser-sensitive source text.
- Still provide assertion helpers through an allowed harness mode after parsing,
  or use a wrapper strategy that preserves parser-sensitive first-token
  behavior.
- Keep raw tests in a separate batch kind until stable.

Acceptance:

- The 8 ES2021 raw tests pass.
- Hashbang raw tests remain classified as ES2023 unless the project explicitly
  broadens scope.
- Existing non-raw parser tests do not regress.

### P5 - Slow URI Tests

Goal: convert the two partial slow passes into fully passing tests without
hiding real performance regressions.

Current partial list:

- `built_ins_decodeURI_S15_1_3_1_A2_5_T1_js`
- `built_ins_decodeURIComponent_S15_1_3_2_A2_5_T1_js`

These tests are exhaustive 4-byte UTF-8 sweeps and currently pass, but exceed
the slow-test gate at roughly 4.8s.

Options:

1. Optimize URI decoding.
   - Profile `decodeURI` and `decodeURIComponent` hot loops.
   - Add fast paths for percent-decoded UTF-8 sequences.
   - Avoid repeated allocation while building decoded strings.
2. Mark these as accepted slow conformance tests.
   - Keep them out of the normal baseline runtime.
   - Run them in a dedicated slow-conformance target.

Preferred path: first attempt optimization. Only create a slow-conformance
exception if profiling shows the tests are intentionally exhaustive rather than
engine-pathological.

Acceptance:

- Both tests pass below the fully-passing threshold, or are documented in a
  separate slow-conformance suite with no correctness failure.
- No slowdown in ordinary URI tests.

### P6 - Proper Tail Calls Decision

Goal: decide whether proper tail calls are part of LambdaJS's ES2021 claim.

Decision: for Js48, LambdaJS does not claim general ECMAScript proper tail call
support. The existing Lambda/JIT self-recursive TCO work is useful, but it is
not the same as spec-level PTC across arbitrary strict-mode tail-position calls.
The js262 runner therefore treats `tail-call-optimization` as an intentional
ES2021 compatibility exception with its own skip reason, not as a future-feature
or proposal skip.

Current skipped feature:

- `tail-call-optimization`: 35 tests.

Proposal:

- Treat PTC as a policy gate, not a hidden bug bucket.
- If in scope, design tail-position detection and stack-frame reuse in a
  dedicated proposal before implementation.
- If out of scope, document it as an explicit compatibility exception.

Acceptance:

- Project decision recorded. Done for Js48: PTC is an intentional compatibility
  exception.
- Skip reason updated to distinguish "intentional PTC exception" from
  "future/proposal unsupported feature".

### P7 - Cross-Realm And Annex B Host Decisions

Goal: decide whether to implement broader test262 host facilities that are not
required for ordinary Lambda script execution.

Decision: for Js48, LambdaJS does not claim cross-realm host support or
browser-specific Annex B host quirks. The js262 runner treats `cross-realm`,
`IsHTMLDDA`, and `caller` as intentional scope exceptions with dedicated skip
messages. `host-gc-required` remains a separate generic harness-capability skip.

Current relevant skipped features:

- `cross-realm`: 183 tests.
- `IsHTMLDDA`: 35 tests.
- `caller`: 23 tests.

Proposal:

- Cross-realm support can be valuable, but it should be designed as an isolated
  runtime capability with separate heap/global object boundaries.
- `IsHTMLDDA` is browser-specific `document.all` behavior and should probably
  remain out of scope for LambdaJS.
- Annex B `caller` behavior should be implemented only if LambdaJS wants broad
  web-compat semantics.

Acceptance:

- Explicit scope decision. Done for Js48: cross-realm, `IsHTMLDDA`, and
  `caller` are intentional host-scope exceptions.
- No silent conflation with ES2021 core support.

## 5. Async Performance Investigation

The checked-in baseline header reports:

```text
# Phase timing: batch-execute-batched 127.1s
# Phase timing: batch-execute-non-batched 676.4s
```

Root cause: async tests were admitted into the baseline but still executed as
one-test-per-process batches. That preserved isolation but paid process startup,
manifest transfer, harness compilation, and scheduler overhead thousands of
times.

Js47 changed the runner to support `--async-chunk-size=<n>` and verified async
chunk sizes 5, 10, 20, and 50. With async chunk size 50, async tests run with
the same process chunk size as sync JS tests:

| Async chunk | Total time | Failed | Regressions | Non-fully-passing |
|---:|---:|---:|---:|---:|
| 5 | 292.2s | 0 | 0 | 0 |
| 10 | 238.9s | 0 | 0 | 2 retry-only slow tests |
| 20 | 204.5s | 0 | 0 | 1 retry-only slow test |
| 50 | 177.1s | 0 | 0 | 0 |

Conclusion: the 676.4s non-batched async time is already solved in code, but the
baseline file needs to be refreshed to show it.

Further async performance work should be evidence-driven:

1. Keep async and sync tests in separate groups for diagnosability.
2. Do not mix async and sync tests by default unless a flagged experiment beats
   177.1s with 0 failures, 0 regressions, and 0 non-fully-passing tests.
3. Preserve the timing split:
   - `batch-execute-batched`
   - `batch-execute-non-batched`
4. Add a batch-count line to every baseline update:
   - sync chunk size
   - async chunk size
   - module chunk size if P3 adds one
5. Track worker-time as well as wall-time so improvements are not just CPU
   oversubscription artifacts.

Potential next optimizations:

- Reorder dispatch by estimated batch cost separately for async, module, and
  ordinary script groups.
- Cache module fixture parsing after P3, but only if module semantics remain
  correct.
- Investigate long-lived process memory growth if batch size above 50 is ever
  considered.

## 6. Regression And Performance Discipline

Every phase must follow this sequence:

1. Build debug.

```bash
make build
make -C build/premake config=debug_native test_js_test262_gtest -j4
```

2. Run targeted smoke tests for the new feature.

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --batch-file=temp/js48_<phase>_smoke.txt \
  --write-failures=temp/js48_<phase>_smoke_failures.tsv \
  --gtest_brief=1
```

3. Run the full debug baseline.

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --write-failures=temp/js48_<phase>_debug.tsv \
  --gtest_brief=1
```

4. Build release.

```bash
make release
```

5. Run full release verification.

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --write-failures=temp/js48_<phase>_release.tsv \
  --gtest_brief=1
```

6. Update baseline only after release is clean.

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --update-baseline \
  --write-failures=temp/js48_<phase>_update.tsv \
  --gtest_brief=1
```

Acceptance for every phase:

- 0 failures in the existing baseline.
- 0 regressions versus the previous baseline.
- No new retry-only or slow tests unless explicitly documented.
- Release runtime does not regress by more than 5% without explanation.
- No increase in peak memory that threatens batch stability.

## 7. Proposed Milestone Definition

Js48 can claim "full ES2021 support in current js262 scope" when:

- Dynamic import in script code is admitted.
- ES2021-relevant module tests are admitted or explicitly scoped out with a
  documented module limitation.
- ES2021-relevant raw parser-mode tests are admitted.
- The two URI slow-pass tests are either optimized into the full baseline or
  tracked in a dedicated slow-conformance suite.
- PTC, cross-realm, and browser Annex B host behavior have explicit product
  decisions instead of generic skip labels.
- The release baseline is updated and remains fully passing with async batch
  size 50.
