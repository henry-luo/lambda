# Test262 batch harness optimization

Date: 2026-09-10. Source base: `cbd220d923b14a3bc5a4e98453565f3fcea3ea04`.
Scope: the first four optimization items from the batch throughput analysis:
realm lifecycle, retained canonical helper ASTs, selective C+ helpers, and
phase/lifecycle telemetry. Implementation follows **D8.1.3v10** (retained
Scripts in the common runtime), **D5.3.3** (precise native roots), and
**D6.2.2v2** (real callable dispatch). No formal ruling changes.

## Implementation

1. **Realm construction and cleanup.** `js_get_global_this` installs ordinary
   own lazy slots for catalog constructors and Math/JSON/Intl/Reflect/Atomics/
   console/CSS. Existing property kernels resolve values on observation and
   preserve presence, descriptors, identity, replacement, and deletion.
   `js_test262_hot_context_recycle` delegates the full reset to
   `runtime_reset_heap`, eliminating a duplicate `js_batch_reset`. Global
   binding removal after heap destruction now invalidates metadata without
   trying to publish roots into a dead realm.
2. **Retained includes.** Positive classic AST tests are grouped by ordered
   includes and strictness. The new `harness-includes` record prepares a
   second Script once per batch. Base and include Scripts reexecute in each
   fresh realm; test-created function objects and mutations never survive
   recycling. Strictness belongs to the include Script independently of the
   non-strict base harness. Negative tests keep the previous inclusion policy;
   modules keep includes in their own source, and MIR keeps its existing
   preamble path.
3. **Native checks.** Native `assert.throws` now compares the thrown object's
   constructor identity instead of accepting subclasses via `instanceof`.
   Native `verifyProperty` performs observable enumeration, writes with
   conditional restoration, deletion, descriptor-field validation, and
   abrupt-completion propagation. Its span entry distinguishes an omitted
   descriptor from explicit `undefined`. Twenty-two canonical/native probes
   cover both passing and failing outcomes. These exposed a runtime defect:
   `for-in` bypassed Proxy `ownKeys`; the shared reflection walk now honors
   the trap and retains visited strings across collection.

   Native admission remains selective. Tests including `propertyHelper.js`
   continue to execute the canonical helper. Native diagnostic text is not a
   complete reproduction of canonical messages; this work does not claim
   universal harness equivalence or replace all helpers with native code.
4. **Timing.** AST execution and total phases are populated, using monotonic
   timing and preserving the outer phase record across nested eval. Initial
   global construction is separate from AST execution and MIR imports.
   `LAMBDA_JS_PHASE_TIMING=1` adds `realm_us`, `harness_us`, `reset_us`, and
   `lifecycle_us` to the optional TSV through a separate post-cleanup
   `BATCH_LIFECYCLE` record. All three readers share numeric result parsing.
   Status, timeout thresholds, baseline membership, and classification are
   unchanged. A worker exiting before lifecycle emission leaves zero timing
   fields; it is still classified by the existing result/recovery rules.

See [JS_16 §1.1](../../doc/dev/js/JS_16_Testing.md#11-current-batch-throughput-implementation)
for timing scope and protocol details.

## Verification

Release build: `make release`, then incremental
`make -C build/premake config=release_native lambda test_js_script_gtest test_js_gtest test_js_test262_gtest -j8`.

- Full admitted Test262 baseline: **40,261/40,261**, zero regressions, failures,
  retries, or partial results. Command:
  `LAMBDA_JS_PHASE_TIMING=1 ./test/test_js_test262_gtest.exe --baseline-only --batch-only --run-async --async-list=test/js262/test262_baseline.txt --jobs=9`.
  The final release run reported **44.7 s** (44.6 s execution); the previous
  implementation pass was 44.8 s. These are runner phase totals, excluding
  startup before preparation. Final run telemetry has all 40,261 lifecycle
  records, each with nonnegative phase values.
- Script/AST/MIR ownership suite: **110/110**. Three new tests cover lazy
  descriptors and replacement, retained strict helpers and realm isolation,
  and canonical/native outcome parity. All three also pass with
  `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.
- Broader JS regression/fixture suite: **493/493** with local socket access.
  The restricted run's 12 socket failures reproduced as loopback bind
  `EPERM`; they disappear when the same suite has loopback access.
- Runner preflight: `./test/test_js_test262_gtest.exe --prelim` passes.
- Required `make test-lambda-baseline`: **5,241/5,241**, including input
  tests and the **118/118** MIR forced-GC stress cases. This correctness
  gate builds a debug host; the saved, measured release executable was
  restored afterward.
- Object census in release/debug source scopes reports five existing
  findings: four semantic `map_kind` rows and one `__json_own_proto__` marker.
  Each was verified unchanged in the source base. No census suppression or
  vendor changes were made.

Logs and raw data are under `temp/test262_harness_impl/`, including
`baseline-final.log`, `phases-final.tsv`, `phase-summary.json`,
`script-suite-final-2.log`, `focused-gc.log`, `js-suite-unrestricted.log`,
`prelim-final.log`, `lambda-baseline.log`, and `census-preexisting.txt`.

## Measurement method

`temp/test262_harness_impl/benchmark.py` runs saved before/after release
executables in alternating order. Each case uses 300 repeated sources per
worker, one warm-up per version, and five measured processes per version.
Process wall time includes setup and teardown; medians below are divided by
300. The ordinary probes use identical manifests. The cached-property case
compares the old inline helper to the new retained include Script. Each
source must report success; the new worker must emit 300 lifecycle records.
These isolate fixed worker costs and do not predict full-suite speedup.

| Release probe | Before ms/test | After ms/test | Reduction |
|---|---:|---:|---:|
| AST, bare body | 3.950 | 2.720 | 31.1% |
| AST, native assertions | 4.053 | 2.903 | 28.4% |
| AST, canonical assertions | 4.091 | 2.955 | 27.8% |
| AST, property helper still inline (realm change only) | 5.647 | 4.573 | 19.0% |
| AST, property helper inline → retained includes | 5.554 | 3.781 | 31.9% |
| MIR O0, canonical assertions | 5.209 | 3.786 | 27.3% |

All 21,600 synthetic executions, including warm-ups, succeeded. Retaining
the property helper reduces measured per-test parsing from 936 to 14 µs;
its top-level initialization still executes in every fresh realm. Native
and canonical base assertions remain close, supporting the decision to
prioritize realm work and helper parsing over a blanket C+ rewrite.

In the final nine-worker baseline, cumulative lifecycle time was 374.6 s,
including 127.0 s of initial realm construction, 60.6 s of post-result
cleanup, 14.0 s of test-source parsing, and 13.9 s of retained-harness
execution. These are summed worker durations, not serial wall time or CPU
time. Deferred constructors are charged to the code that first reads them,
so `realm_us` is not the entire eventual cost of all realm objects.

The pre-change full baseline collected in this session had 5,451 MIR failures,
so its 82.3 s total is not an equivalent successful-run performance baseline.
The saved September 9 successful 58.4 s log also belongs to an earlier binary.
Neither is used to claim a controlled full-suite percentage improvement.
