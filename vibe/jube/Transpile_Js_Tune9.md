# Transpile JS Tune9 Proposal

Date: 2026-06-16
Status: implementation pass 4 complete
Source runs:

- Baseline capture: `test/js262/results/release_run_008`
- Post-Atomics timing source: historical `temp/_t262_timing_o0.tsv`
  snapshot, with values preserved below.
- Post-RegExp generated-property timing source: `temp/_t262_timing_o0.tsv`
- Dynamic Function instrumentation focus run:
  `temp/js_dynfunc_stats_pass7/*.tsv`

## Goal

Continue the js262 performance work after Tune8 and the Atomics waitAsync fix,
but skip URI codec tuning first. The remaining work should target repeated
runtime or compiler costs that show up across clusters, not one-off shortcuts
for individual test files.

Each candidate must be implemented and accepted independently:

1. capture or reuse a release baseline;
2. implement one change;
3. build release;
4. run a focused release probe;
5. run the full release js262 gate;
6. keep, revise, or revert based on measured result.

## Last Round Completed

### Release baseline captured first

Before tuning edits, a fresh release `--update-baseline` run was captured as
`test/js262/results/release_run_008`.

Baseline summary:

| Metric | Value |
| --- | ---: |
| Fully passed | 40,261 / 40,261 |
| Non-fully-passing | 0 |
| Failed | 0 |
| Regressions | 0 |
| Skipped | 2,628 |
| Total wall time | 120.5 s |
| Batched execute wall time | 119.2 s |
| Sync batched wall time | 98.9 s |
| Async batched wall time | 20.3 s |
| Per-test elapsed sum | 593.279 s |
| Max test | `built_ins_decodeURI_S15_1_3_1_A2_5_T1_js` at 3.438127 s |

The release_run_008 top slow set had two URI codec tests at about 3.4 s each
and then eighteen `Atomics.waitAsync` no-spurious-wakeup tests at about 1.0 s
each.

### Atomics waitAsync virtual-time tuning landed

Root cause:

- Test262 agent-slot `Atomics.waitAsync` used a real libuv timeout for short
  finite waits.
- Synchronous `Atomics.wait` already used the js262 virtual Atomics clock.
- The no-spurious-wakeup tests use small finite waits and only need the virtual
  elapsed time, not real wall sleep.

Runtime change:

- In `lambda/js/js_typed_array.cpp`, the agent-slot `Atomics.waitAsync` path now
  advances `js_atomics_virtual_now_ms` and resolves due waiters for short finite
  waits, matching the synchronous fast path.

Measured result:

| Probe | Before | After |
| --- | ---: | ---: |
| waitAsync no-spurious rows | about 1000 ms/test | about 17-32 ms/test |
| top-20 async batch | about 18.1 s | about 1.0 s |
| full release js262 | 40,261 / 40,261 | 40,261 / 40,261 |
| failures | 0 | 0 |
| regressions | 0 | 0 |
| full release wall time | 120.5 s | 108.5 s |
| async batched wall time | 20.3 s | 16.1 s |

The Atomics cluster is no longer in the top 50 after the patch.

## Current Top-50 Shape

Timing source: post-Atomics full release run in `temp/_t262_timing_o0.tsv`.

Top 50 total: 18.942964 s.

| Cluster | Count | Sum | Share | Notes |
| --- | ---: | ---: | ---: | --- |
| URI codec | 17 | 10.315537 s | 54.5% | explicitly deferred for Tune9 first pass |
| RegExp | 9 | 2.991096 s | 15.8% | regex literal/eval compile loops, property escapes, whitespace replacement |
| Unicode identifiers | 12 | 2.496670 s | 13.2% | generated direct and escaped identifier-source files |
| Function metadata / constructor | 7 | 2.083509 s | 11.0% | intrinsic descriptor and native-source validation walks |
| Parser stress | 2 | 0.492000 s | 2.6% | comment/destructuring parser stress |
| TypedArray / array bulk | 2 | 0.402122 s | 2.1% | spreadable typed-array concat and resizable TA set matrix |
| Array sort | 1 | 0.162030 s | 0.9% | 2048-object stable sort |

Non-URI top-50 total: 8.627427 s.

| Non-URI cluster | Count | Sum | Non-URI share |
| --- | ---: | ---: | ---: |
| RegExp | 9 | 2.991096 s | 34.7% |
| Unicode identifiers | 12 | 2.496670 s | 28.9% |
| Function metadata / constructor | 7 | 2.083509 s | 24.1% |
| Parser stress | 2 | 0.492000 s | 5.7% |
| TypedArray / array bulk | 2 | 0.402122 s | 4.7% |
| Array sort | 1 | 0.162030 s | 1.9% |

Top non-URI examples:

| Seconds | Test | Initial cause read |
| ---: | --- | --- |
| 0.442025 | `language_literals_regexp_S7_8_5_A2_4_T2_js` | 65K regex literal `eval` loop |
| 0.435274 | `annexB_built_ins_RegExp_RegExp_trailing_escape_BMP_js` | 65K regex literal construction loop |
| 0.427235 | `language_literals_regexp_S7_8_5_A1_4_T2_js` | regex syntax/eval sweep |
| 0.394456 | `language_literals_regexp_S7_8_5_A1_1_T2_js` | regex syntax/eval sweep |
| 0.381771 | `language_literals_regexp_S7_8_5_A2_1_T2_js` | regex syntax/eval sweep |
| 0.380375 | `annexB_built_ins_RegExp_RegExp_leading_escape_BMP_js` | regex syntax/eval sweep |
| 0.342218 | `built_ins_GeneratorFunction_is_a_constructor_js` | dynamic constructor metadata |
| 0.313511 | `built_ins_Function_prototype_toString_built_in_function_object_js` | intrinsic graph descriptor walk |
| 0.295890 | `language_expressions_async_arrow_function_prototype_js` | async function prototype setup |
| 0.293386 | `built_ins_AsyncFunction_is_a_constructor_js` | dynamic constructor metadata |
| 0.262859 | `language_identifiers_start_unicode_10_0_0_escaped_js` | huge escaped identifier source |
| 0.259350 | `language_destructuring_binding_syntax_recursive_array_and_object_patterns_js` | parser / AST / early-error stress |

## Tune9 Scope Decision

Skip URI codec work first.

Reason:

- URI is still the largest top-50 bucket, but it is already heavily tuned:
  direct runtime fast paths, 4-byte escape helpers, cached URIError objects,
  test262 percent-hex concatenation helpers, and MIR equality lowering.
- Tune8 showed that removing URI shortcuts causes hard test262 timeouts and
  batch instability.
- Further URI work is more likely to become test-shape-specific than broadly
  useful.

Tune9 should therefore start with non-URI clusters in this order:

1. RegExp simple-operation and compile-path fast paths.
2. Function metadata / descriptor caching.
3. TypedArray / array bulk operations.
4. Unicode identifier instrumentation and shared range helpers.

## P1: RegExp Simple-Operation Fast Paths

### Current shape

RegExp appears in the current top 50 in two forms:

- 65K-loop syntax/eval tests that repeatedly build regex literals from BMP
  code units.
- generated property-escape and whitespace tests that scan large strings or many
  single-character strings.

Existing runtime support is already useful:

- `js_create_regex` detects simple `^\p{...}+$` property repeats.
- generated Unicode property tables have sorted range lookup.
- generated property lookups have a monotonic cursor for near-sorted input.
- `js_regexp_test_property_all` avoids RE2 for simple property-all tests.

### Proposed work

P1-A: Cache simple property mode at compile time.

Today the test path can still rediscover mode from the regex object's `source`
property. Store the detected mode in `JsRegexData` when the regex is created.
Then `.test()` can read `rd->special_property_kind` directly without property
lookup or source reparsing.

P1-B: Broaden simple operation support beyond `.test()`.

The `character-class-escape-non-whitespace.js` row uses a repeated operation of
the form `str.replace(/\S+/g, "literal")` over many characters. Add or repair a
generic direct path for:

- global simple property runs;
- replacement strings with no `$` substitutions;
- no captures and no side-effectful replacement function.

This should be implemented as a string/regex fast path, not as a test262 file
shortcut.

P1-C: Add compile cache for eval-created regexes only when source+flags are
content-identical and the pattern has no observable compile side effect.

The 65K-loop tests repeatedly call `eval("/" + xx + "/")` where each source is
different, so a normal content cache will not help every row. The useful general
piece is to ensure existing regex frontend validation avoids repeated slow
Unicode/property setup for simple identity escapes and literal patterns.

### Acceptance

- Focused non-URI top-50 probe improves RegExp cluster sum.
- Full release js262 remains 40,261 / 40,261 with 0 regressions.
- No new parser or regex syntax acceptance changes are made without targeted
  syntax tests.

## P2: Function Metadata and Descriptor Caching

### Current shape

The representative slow test is
`built_ins_Function_prototype_toString_built_in_function_object_js`.
It recursively walks well-known intrinsics and repeatedly calls:

- `Object.getOwnPropertyDescriptors`;
- `Reflect.ownKeys`;
- `Function.prototype.toString`;
- native-source validation.

This is broad engine behavior, not a single test trick.

### Proposed work

P2-A: Cache canonical native-source strings.

Most built-in and bound/native functions intentionally return the short
canonical string `function () { [native code] }`. Reuse a per-epoch string item
instead of rebuilding it through `StrBuf` and `heap_create_name` for every call.

P2-B: Reuse descriptor key Items.

Descriptor construction repeatedly creates keys such as `value`, `writable`,
`enumerable`, `configurable`, `get`, and `set`. Use shared per-epoch name Items
or existing namepool helpers where safe. Descriptors themselves must remain fresh
objects because user code can mutate them.

P2-C: Fast path immutable built-in descriptor walks.

For ordinary built-in function objects with no user-added own properties and no
proxy involvement, `Object.getOwnPropertyDescriptors` can construct the standard
descriptor object directly from shape metadata without going through the full
generic ownKeys plus descriptor lookup path for every key.

### Acceptance

- Focused probe improves the Function metadata cluster.
- Tests that mutate built-ins or inspect descriptor identity still pass.
- Full release js262 remains 40,261 / 40,261 with 0 regressions.

## P3: TypedArray / Array Bulk Operations

### Current shape

Two current top-50 entries are bulk collection operations:

- `TypedArray.prototype.set` with resizable source buffers and many ctor pairs.
- `Array.prototype.concat` with typed arrays marked
  `Symbol.isConcatSpreadable = true`.

`TypedArray.prototype.set` already has same-type raw copy and raw conversion
paths. `Array.prototype.concat` still uses generic `HasProperty` and `Get` for
each spreadable index.

### Proposed work

P3-A: Spreadable typed-array concat direct path.

When an item is a typed array, has `Symbol.isConcatSpreadable === true`, is not
proxied, and has no observable indexed accessors, concat can append dense
elements by direct typed-array load instead of per-index generic property
queries.

Guard carefully for:

- user-defined `length` own property;
- out-of-bounds resizable views;
- detached buffers;
- BigInt typed arrays;
- inherited or own indexed accessors.

P3-B: Audit resizable-buffer `set` fallback frequency.

The set test matrix may already hit raw copy/conversion for many cases. Before
changing code, add focused logging or a local counter in a throwaway probe to
measure how often it falls back to the boxed `Item* values` path. Keep only if
the fallback is common enough.

### Acceptance

- Focused probe improves the two bulk rows without changing sparse or accessor
  semantics.
- Full release js262 remains 40,261 / 40,261 with 0 regressions.

## P4: Unicode Identifier Instrumentation and Shared Range Helpers

### Current shape

Unicode identifier rows are huge generated source files. The escaped variants
exercise escape normalization, parser/scanner acceptance, AST construction, and
early-error checks. Direct parser changes have a high regression surface.

### Proposed work

P4-A: Instrument first, then decide.

Measure parse, AST build, early-error, MIR build, and execute time for the
identifier rows before changing scanner logic.

P4-B: Share generated ID_Start / ID_Continue helpers.

The regex subsystem already has generated range tables for identifier start and
continue validation. A future cleanup should expose one shared helper layer so
regex named-group validation, early-error checks, and any scanner-facing code use
the same tables and lookup policy.

### Acceptance

- No manual edits to generated parser files.
- Any scanner or grammar change goes through the documented generation flow.
- Full release js262 remains 40,261 / 40,261 with 0 regressions.

## Measurement Plan

Create a non-URI top-50 focus list from the post-Atomics timing TSV, excluding
the URI codec rows:

```bash
awk 'BEGIN{FS="\t"} NR>1 {printf "%.6f\t%s\n", $3/1000000.0, $1}' temp/_t262_timing_o0.tsv \
  | sort -nr \
  | awk 'BEGIN{FS="\t"} $2 !~ /decodeURI|decodeURIComponent|encodeURI|encodeURIComponent/ {print $2}' \
  | head -50 > temp/js262_tune9_non_uri_top50.txt
```

Focused release probe:

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe \
  --batch-only \
  --batch-file=temp/js262_tune9_non_uri_top50.txt \
  --run-async \
  --async-list=temp/js262_tune9_non_uri_top50.txt \
  --batch-chunk-size=100 \
  --async-chunk-size=100 \
  --jobs=1 \
  --write-failures=temp/js262_tune9_focus_failures.tsv \
  --gtest_brief=1
```

Full release gate:

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe \
  --batch-only \
  --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --write-failures=temp/js262_tune9_full_failures.tsv \
  --feature-summary \
  --gtest_brief=1
```

Acceptance bar:

- 40,261 / 40,261 fully passing.
- 0 failures.
- 0 regressions.
- target cluster improves in the focused probe.
- full-suite wall time does not regress beyond normal run noise.

## Recommended Order

1. Implement P1-A first: compile-time simple property mode storage.
2. If P1-A is measurable, continue to P1-B for simple global property replace.
3. Implement P2-A and P2-B together only if they share the same helper for
   canonical name/string Items; otherwise stage separately.
4. Try P3-A only after RegExp and function metadata work, because the measured
   bulk cluster is smaller.
5. Keep P4 as instrumentation until there is hard evidence that parser-facing
   changes will beat their risk.

URI work remains deferred until the non-URI clusters are either improved or
shown not worth keeping.

## Implementation Pass 1 Results

### P1-A kept: use compiled simple property mode in `.test()`

Implemented in `lambda/js/js_runtime.cpp`.

Change:

- `js_regex_test` now reads `JsRegexData::special_property_kind` directly for
  non-global and non-sticky regexes before falling back to
  `js_regexp_property_all_mode(regex)`.
- This removes a repeated `source` property lookup and source-pattern reparse
  for regexes whose simple property-repeat mode was already detected by
  `js_create_regex`.

Verification:

| Run | Result |
| --- | --- |
| release build | passed |
| focused non-URI top-50 probe | 50 / 50 passed, 0 failures |
| full release js262 gate | 40,261 / 40,261 fully passed |
| regressions | 0 |
| full release wall time | 106.1 s |
| sync / async batched wall | 88.5 s / 16.3 s |

Decision: keep. The change is small, general, and semantically clean. The full
suite stayed green and was slightly faster than the post-Atomics reference run.

### P1-B attempted and reverted: `String.fromCharCode` plus `/\S+/g`

Attempted work:

- Add a MIR helper for the direct expression shape
  `String.fromCharCode(x).replace(/\S+/g, "literal")`.
- Reorder the existing `/\S+/g` helper to classify single-codepoint strings
  before consulting the `String.fromCharCode` side cache.

What the measurement showed:

| Probe | Result |
| --- | --- |
| focused non-URI top-50 probe | 50 / 50 passed, 0 failures |
| focused `character-class-escape-non-whitespace` row | about 188 ms to about 174 ms |
| full release js262 gate | 40,261 / 40,261 fully passed |
| regressions | 0 |
| full release wall time | 115.5 s |
| full-run `character-class-escape-non-whitespace` row | 0.231005 s |

Decision: revert. The direct-expression helper did not apply to the top test
because the source shape is:

```js
str = String.fromCharCode(j);
res = str.replace(/\S+/g, "test262");
```

The helper reorder gave only a small focused change and did not improve the
full run. The final code keeps P1-A only.

### P2 kept: canonical native-source fast path

Implemented in:

- `lambda/js/js_runtime.cpp`
- `lambda/js/js_globals.cpp`

Change:

- `Function.prototype.toString` now returns the already-chosen canonical native
  source literal directly for native/no-source functions instead of building the
  same string through a fresh `StrBuf`.
- `js_validate_native_function_source` now accepts the exact canonical
  `"function () { [native code] }"` string immediately.
- Descriptor and `ownKeys` caching were deliberately not implemented. The
  intrinsic walk touches mutable JavaScript objects, so caching descriptors
  without a mutation/version guard would be too broad for this pass.

Verification:

| Run | Result |
| --- | --- |
| release build | passed |
| focused function-metadata probe | 7 / 7 passed, 0 failures |
| full release js262 gate | 40,261 / 40,261 fully passed |
| regressions | 0 |
| full release wall time | 118.5 s |
| sync / async batched wall | 100.3 s / 16.8 s |

Timing comparison against `release_run_008`:

| Manifest | `release_run_008` | P2 full run |
| --- | ---: | ---: |
| function metadata 7-test manifest | 2.444405 s | 2.240419 s |
| non-URI top-50 manifest | 11.128023 s | 10.679616 s |

Decision: keep, with a cautious reading. The targeted manifests improved and
the full suite stayed at zero regressions. The full wall time was noisier than
the P1-A-only run, so future js262 tuning should continue to judge changes by
both targeted timings and a fresh full-suite gate.

## Implementation Pass 2 Results

### P1-D kept: generated Script property buckets and alias canonicalization

Implemented in:

- `lambda/js/js_regex_generated_property_tables.inc`
- `lambda/js/js_runtime.cpp`

Root cause:

- The generated `Script`, `sc`, `Script_Extensions`, and `scx` tests exercise
  four positive aliases and four negative aliases per generated script file.
- Those aliases point at identical generated range tables, but previously
  produced distinct generated property kind ids.
- `js_regexp_test_property_all` caches one all-string property result by
  mode/kind, so equivalent aliases could not share the cache.
- Name lookup also scanned the full generated property table even when the
  property name prefix already identified the Script or Script_Extensions band.

Change:

- Generated property lookup now discovers the contiguous Script/sc and
  Script_Extensions/scx table bands once, then scans only the matching band for
  those prefixes.
- Generated property kinds are canonicalized by shared `(ranges, count)` before
  they enter the runtime property-kind path, so equivalent aliases reuse the
  existing all-string property cache.

Rejected sub-attempt:

- A fixed-size generated property name cache was tried first and reverted. It
  did not improve the focused generated-script probes, because the dominant
  repeated work was alias-equivalent all-string testing rather than the one-time
  property-name scan alone.

Focused measurement:

| Probe | Before | After |
| --- | ---: | ---: |
| generated Script top-50 manifest | 50 / 50 passed | 50 / 50 passed |
| generated Script all-350 manifest | 350 / 350 passed | 350 / 350 passed |
| all-350 command wall | 11.7 s | 9.8 s |
| all-350 batch split | 3 s + 3 s + 3 s + 2 s | 3 s + 2 s + 2 s + 1 s |
| typical per-test rows | about 26-38 ms | about 16-19 ms |

Full release-gate measurement:

| Metric | `release_run_008` | After P1-D |
| --- | ---: | ---: |
| generated Script/Script_Extensions rows | 350 | 350 |
| generated Script/Script_Extensions sum | 13.281 s | 8.873 s |
| generated Script/Script_Extensions average | 37.946 ms | 25.351 ms |
| full release js262 | 40,261 / 40,261 | 40,261 / 40,261 |
| failures | 0 | 0 |
| regressions | 0 | 0 |
| full release wall time | 120.5 s | 109.0 s |
| sync / async batched wall | 98.9 s / 20.3 s | 91.3 s / 16.1 s |

Decision: keep. The optimization is general to generated Unicode property
aliases, uses immutable generated table identity as the canonicalization guard,
and leaves the observable RegExp semantics unchanged.

## Outstanding Work

The remaining non-URI weight is now mostly:

- URI tests, still explicitly deferred.
- Function metadata tests, split between true descriptor walking and repeated
  dynamic `Function("return ...")` compilation from the well-known-intrinsic
  harness.
- Unicode identifier generated source rows, which still need instrumentation
  before any parser/scanner-facing change.
- TypedArray and array bulk rows, which are smaller than the RegExp and
  descriptor clusters.

Do not add a broad descriptor cache just for the test262 intrinsic walker.

## Implementation Pass 3 Results

### P2 descriptor-walking attempt rejected

Focused manifest:

- `temp/js262_tune9_descriptor_phase_focus.txt`
- 123 tests covering the `Function.prototype.toString` rows,
  `is-a-constructor` rows, and async-arrow prototype row.

Root-cause split:

- `built_ins_Function_prototype_toString_built_in_function_object_js` is the
  true descriptor walker. It recursively visits well-known intrinsics and calls
  `Object.getOwnPropertyDescriptors` and `Reflect.ownKeys`.
- The `is-a-constructor` and async-arrow prototype rows are mostly the
  `wellKnownIntrinsicObjects.js` harness cost. It eagerly evaluates every
  source string with `new Function("return " + wkio.source)()` before each test
  asks for a single intrinsic.
- `isConstructor.js` itself is small; it uses `Reflect.construct(function(){},
  [], f)`.

Rejected implementation attempts:

- Added a `TypeMap` shape-version stamp and mutation bumps for future
  descriptor-cache invalidation.
- Tried a narrow `Reflect.ownKeys` fast path for plain string-key maps.
- Tried direct `Object.getOwnPropertyDescriptors` emission for function objects
  without live custom property maps and for plain data maps.

Focused measurements:

| Probe | Result |
| --- | --- |
| pre-descriptor focused baseline | 123 / 123 passed, about 4.1 s wall |
| version stamp + descriptor-result `CreateDataProperty` | 123 / 123 passed, about 4.1 s wall, hot rows slower |
| result insertion reverted, `Reflect.ownKeys` fast path kept | 123 / 123 passed, about 4.0 s wall, still not better than baseline |
| direct descriptor emission added | 123 / 123 passed, about 4.1 s wall, not a win |
| rollback of descriptor-phase code | 123 / 123 passed, about 4.1 s wall |

Decision: revert the descriptor-phase code. The mutation/version foundation is
not worth keeping until a descriptor cache actually uses it, because it adds
shape-mutation work to common object creation and did not improve the focused
cluster. The direct descriptor-emission path also did not beat the generic path
once fresh descriptor object creation remained mandatory.

### Next candidate: dynamic Function construction cost

The remaining function-metadata rows need a different approach than descriptor
walking alone. The broad cause is dynamic compilation of many tiny
`new Function("return ...")` bodies from the well-known-intrinsic harness.

Safe follow-up shape:

1. Instrument `js_new_function_from_string_kind` by source length/body shape and
   correlate with the focused manifest.
2. Consider a compiled-code cache only if it can return fresh function objects,
   fresh `.prototype` objects where required, correct `Function.prototype.toString`
   source, and guards for runtime/preamble context.
3. Avoid returning the same cached function object for `new Function`; that would
   violate observable identity.

Descriptor caching should stay deferred until the runtime has a measured
cache-entry design with mutation/version invalidation that improves focused and
full-suite timing.

## Implementation Pass 4 Results

### Dynamic Function instrumentation landed

Implemented in `lambda/js/js_mir_eval_lowering.cpp`.

Change:

- `js_new_function_from_string_kind` now has opt-in dynamic-Function telemetry
  gated by `LAMBDA_JS_DYNFUNC_STATS=1`.
- Stats are written per process under
  `LAMBDA_JS_DYNFUNC_STATS_DIR`, defaulting to `./temp/js_dynfunc_stats`.
- The instrumentation records:
  - dynamic function kind;
  - argument count;
  - generated source length and source hash;
  - whether the original body starts with `return`;
  - eval-preamble entry count, variable count, and a preamble fingerprint;
  - repeated calls under the safe source-plus-preamble fingerprint.
- The normal runtime path is unchanged unless the environment flag is enabled.
- While touching this path, the generated source length is carried through from
  `StrBuf` instead of recomputing it with `strlen()`.

Focused command:

```bash
LAMBDA_JS_DYNFUNC_STATS=1 \
LAMBDA_JS_DYNFUNC_STATS_DIR=./temp/js_dynfunc_stats_pass7 \
ASAN_OPTIONS=detect_container_overflow=0 \
./test/test_js_test262_gtest.exe \
  --batch-only \
  --batch-file=temp/js262_tune9_descriptor_phase_focus.txt \
  --run-async \
  --async-list=temp/js262_tune9_descriptor_phase_focus.txt \
  --batch-chunk-size=100 \
  --async-chunk-size=100 \
  --jobs=1 \
  --write-failures=temp/js262_tune9_dynfunc_stats_focus5_failures.tsv \
  --gtest_brief=1
```

Focused result:

| Metric | Value |
| --- | ---: |
| focused manifest | 123 / 123 passed |
| failures | 0 |
| wall time | about 4.0 s |
| stats files | `temp/js_dynfunc_stats_pass7/50701.tsv`, `temp/js_dynfunc_stats_pass7/50715.tsv` |

Aggregated stats:

| Metric | Value |
| --- | ---: |
| dynamic Function calls | 443 |
| unique safe keys, summed per process | 438 |
| repeated safe-key calls | 5 |
| overflow | 0 |
| return-body shaped calls | 435 |
| calls with eval preamble state | 443 |
| full-key repeat keys | 5 |
| full-key saved compiles if cached | 5 |

Key finding:

- If the preamble fingerprint is ignored, the focused slice appears highly
  cacheable because many sources are tiny `return ...` wrappers.
- With the preamble fingerprint included, the safe reuse opportunity collapses
  to five saved compiles in this focused slice.
- Every recorded dynamic Function call had eval-preamble state, so a cache that
  keys only on source text would be unsafe for this workload.

### Safe compiled-code cache design

Do not implement the broad compiled-code cache yet. The measured safe-hit
density is too low, and the unsafe source-only cache would conflate distinct
js262 harness preamble contexts.

If a future full-suite instrumentation run finds more safe hits, the cache
should use this shape:

1. Key by dynamic kind, argument count, full source length, full source hash,
   exact source bytes, async/generator flags, and a strong eval-preamble
   identity.
2. Treat hash matches as candidates only. Verify exact source bytes before a
   cache hit.
3. Store compiled factory code or an equivalent compiled module artifact, not
   the returned function object.
4. On each hit, execute the cached factory in the current runtime context and
   return a fresh `JsFunction` identity.
5. Preserve fresh `.prototype` objects for normal and generator functions where
   JavaScript requires them.
6. Preserve `.name`, `.length`, and `Function.prototype.toString` source text.
7. Ensure the dynamic factory path does not reuse the existing `js_new_function`
   function-pointer cache in a way that returns the same wrapper object.
8. Keep all memory that the compiled artifact depends on alive for the cache
   lifetime: MIR context, name pool, AST pool, and copied source text.
9. Use a real preamble generation or content identity for the cache guard. The
   current telemetry fingerprint is good enough to prove source-only caching is
   unsafe, but a production cache should use an explicit invalidation model.
10. Size-limit the cache and expose instrumentation counters before enabling it
   by default.

Decision: keep the gated instrumentation, but defer the compiled-code cache
until broader measurement shows enough safe reuse to justify the added runtime
complexity.

## Implementation Pass 5 Results

### Preamble-dependency instrumentation and safe cache landed

Implemented in:

- `lambda/js/js_mir_eval_lowering.cpp`
- `lambda/js/js_runtime_function.cpp`
- `lambda/js/js_runtime.h`
- `lambda/js/js_mir_module_batch_lowering.cpp`

Change:

- `js_new_function_from_string_kind` now scans the generated dynamic-function
  source before lookup/compile.
- The scanner marks a source as preamble-dependent when it sees a visible
  identifier matching the current eval/js262 preamble names.
- The scanner also marks escaped identifiers (`\u...`) as dependent. This is
  conservative, but avoids accidentally caching a source that references a
  preamble binding through an escaped identifier.
- Single-quoted strings, double-quoted strings, line comments, and block
  comments are skipped. Template-literal raw text is intentionally conservative
  rather than trying to implement the full JS lexer inside the cache guard.
- Only preamble-independent sources are eligible for compiled-code cache reuse.
- The cache key is dynamic-function kind, argument count, generated source
  length, source hash, and exact source bytes.
- A cache entry stores the compiled factory (`js_main`) and its MIR context, not
  the returned function object.
- On cache hit, the cached factory is executed again to produce a fresh
  `JsFunction`.
- The existing function-pointer wrapper cache is suppressed during cached
  factory execution, so repeated `new Function(...)` calls do not reuse the same
  function identity or `.prototype` object.
- Dynamic function metadata is applied on both cold compile and cache hit:
  `.name = "anonymous"` and the spec-shaped `Function.prototype.toString`
  source are preserved.
- The dynamic-function cache is reset when deferred MIR contexts are cleaned up,
  so entries do not outlive the compiled code they point at.

The runtime cache is on by default, while the telemetry remains opt-in behind
`LAMBDA_JS_DYNFUNC_STATS=1`.

### Focused cache measurement

Focused stats command:

```bash
LAMBDA_JS_DYNFUNC_STATS=1 \
LAMBDA_JS_DYNFUNC_STATS_DIR=./temp/js_dynfunc_stats_pass8 \
ASAN_OPTIONS=detect_container_overflow=0 \
./test/test_js_test262_gtest.exe \
  --batch-only \
  --batch-file=temp/js262_tune9_descriptor_phase_focus.txt \
  --run-async \
  --async-list=temp/js262_tune9_descriptor_phase_focus.txt \
  --batch-chunk-size=100 \
  --async-chunk-size=100 \
  --jobs=1 \
  --write-failures=temp/js262_tune9_dynfunc_cache_focus_failures.tsv \
  --gtest_brief=1
```

Focused result:

| Metric | Value |
| --- | ---: |
| focused manifest | 123 / 123 passed |
| failures | 0 |
| stats files | `temp/js_dynfunc_stats_pass8/68572.tsv`, `temp/js_dynfunc_stats_pass8/68592.tsv` |
| dynamic Function calls | 443 |
| unique stats rows | 438 |
| preamble-name references | 0 |
| escaped-identifier references | 0 |
| cacheable calls | 443 |
| cache hits | 263 |
| cache-hit rows | 260 |
| dependent rows | 0 |

Interpretation:

- The previous safe-key result looked poor because every call carried js262
  preamble state.
- The free-name guard shows that this focused slice does not actually reference
  those preamble bindings from the generated dynamic-function sources.
- That makes source-based compiled factory reuse safe for this slice, and it
  converts the same workload from five safe full-key repeats to 263 cache hits.

### Smoke verification

Two targeted runtime smoke scripts were kept under `./temp/`:

- `temp/dynfunc_cache_smoke.js`
- `temp/dynfunc_cache_dependent_smoke.js`

Results:

| Smoke | Result | What it checks |
| --- | --- | --- |
| preamble-independent repeated `Function("return 1;")` / `Function("return Array;")` | pass | cache hits occur, returned functions are fresh identities, `.prototype` objects are fresh, `toString()` source is preserved |
| dependent `Function("return dynSentinel;")` | pass | preamble-name reference is detected, calls are classified dependent, cache hits stay at 0 |

### Release js262 gate

Release build command:

```bash
PATH="/clang64/bin:$PATH" \
make -C build/premake config=release_native lambda -j8 CC="ccache gcc" CXX="ccache g++"
```

Full release gate command:

```bash
ASAN_OPTIONS=detect_container_overflow=0 \
./test/test_js_test262_gtest.exe \
  --batch-only \
  --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --write-failures=temp/js262_tune9_dynfunc_cache_full_rerun_failures.tsv \
  --feature-summary \
  --gtest_brief=1
```

Full release result:

| Metric | Value |
| --- | ---: |
| baseline passing | 40261 |
| fully passed | 40257 |
| non-fully-passing | 4 |
| failed | 0 |
| regressions | 0 |
| skipped | 2628 |
| total runtime | 118.4 s |
| failure manifest rows | 0 |

The four non-fully-passing rows were recovered by Phase 4 retry:

- `annexB_language_global_code_if_decl_no_else_global_existing_non_enumerable_global_init_js`
- `annexB_language_global_code_if_stmt_else_decl_global_existing_non_enumerable_global_init_js`
- `annexB_language_global_code_switch_case_global_existing_non_enumerable_global_init_js`
- `annexB_language_global_code_switch_dflt_global_existing_non_enumerable_global_init_js`

Focused follow-up command:

```bash
ASAN_OPTIONS=detect_container_overflow=0 \
./test/test_js_test262_gtest.exe \
  --batch-only \
  --batch-file=temp/js262_tune9_annexb_recovered4.txt \
  --run-async \
  --async-list=temp/js262_tune9_annexb_recovered4.txt \
  --batch-chunk-size=100 \
  --async-chunk-size=100 \
  --jobs=1 \
  --write-failures=temp/js262_tune9_annexb_recovered4_failures.tsv \
  --gtest_brief=1
```

Focused follow-up result: 4 / 4 passed, 0 failures.

### AnnexB retry cleanup

The four AnnexB rows were later traced to the dynamic-Function cache surviving
batch rewind through `js_batch_reset_to()`. The cache already reset in the full
`js_batch_reset()` path, but Phase 4 retry uses the rewind path and could reuse
compiled dynamic Function factories from the failed batch attempt.

Fix:

- reset the dynamic-Function cache from `js_batch_reset_to()`;
- keep the existing full-reset cache clear in `js_batch_reset()`.

Verification:

| Gate | Result |
| --- | ---: |
| exact 100-test AnnexB batch | 100 / 100 passed |
| 4-test AnnexB retry manifest | 4 / 4 passed |
| full release js262 | 40261 / 40261 fully passed |
| non-fully-passing | 0 |
| regressions | 0 |

### Decision

Keep the cache for preamble-independent dynamic Functions. The dependency
instrumentation gives a concrete safety guard, the focused workload shows real
reuse, the cache-hit path preserves observable function identity, and the full
release gate reports zero failed rows and zero regressions.

## P3 TypedArray / Array Bulk Pass

### Implementation

Implemented the P3-A spreadable typed-array concat path.

The generic `Array.prototype.concat` path still performs the observable
`@@isConcatSpreadable` read and the observable `length` read. After that, it can
append number typed-array elements directly when all guards pass:

- concat result is a plain extensible Array whose current length matches the
  concat write index;
- input is a real typed array, not a proxy;
- backing buffer is not detached or out-of-bounds;
- observed concat length equals the live typed-array length;
- typed array has no own `"length"` property and no own numeric/index-looking
  property in its upgraded property map;
- BigInt typed arrays stay on the generic path for this pass.

The element conversion itself was factored into
`js_typed_array_raw_get_item()` so the raw path reuses the same conversion logic
as `js_typed_array_get()`.

### Measurement

Previous full-run timing rows:

| Test | Previous full timing |
| --- | ---: |
| `Array.prototype.concat_large-typed-array` | 203784 us |
| `TypedArray.prototype.set typedarray-arg-src-backed-by-resizable-buffer` | 201089 us |

Focused pass after the concat change:

| Test | Focused timing |
| --- | ---: |
| `Array.prototype.concat_large-typed-array` | 125 ms |
| `TypedArray.prototype.set typedarray-arg-src-backed-by-resizable-buffer` | 116 ms |

Full release pass after the concat change:

| Metric | Value |
| --- | ---: |
| fully passed | 40261 / 40261 |
| non-fully-passing | 0 |
| regressions | 0 |
| total runtime | 112.1 s |
| failure manifest rows | 0 |
| `Array.prototype.concat_large-typed-array` | 193145 us |
| `TypedArray.prototype.set typedarray-arg-src-backed-by-resizable-buffer` | 230117 us |

P3-B audit using `LAMBDA_JS_TA_RAW_FAST=0` on the same focused manifest:

| Test | Raw-fast-off focused timing |
| --- | ---: |
| `Array.prototype.concat_large-typed-array` | 126 ms |
| `TypedArray.prototype.set typedarray-arg-src-backed-by-resizable-buffer` | 120 ms |

### Decision

Keep the guarded concat path. The full-suite per-test signal is modest but
positive, the focused run shows the intended direction, and the release gate is
clean.

Do not change `TypedArray.prototype.set` yet. The raw-fast-off audit does not
show the current raw-copy/convert switch as the dominant cost for the resizable
matrix; the next safe step is more detailed set-path instrumentation that counts
same-type raw copies, raw conversions, and generic fallback entries before
adding another write-path optimization.

Remaining follow-up:

1. Make the stats report distinguish lifetime cache inserts from currently live
   cache entries, because deferred MIR cleanup resets live entries before the
   process exits.
2. Add opt-in `TypedArray.prototype.set` path counters before changing the
   resizable-buffer set implementation.
3. Revisit descriptor walking only after a mutation/version invalidation model
   exists.

## P4 Unicode Identifier Instrumentation and Helper Pass

### Implementation

Implemented the low-risk shared helper portion of P4 without touching scanner
grammar or generated parser files.

Changes:

- exposed `js_unicode_id_is_start()` and `js_unicode_id_is_continue()` from
  `lambda/js/js_runtime.h`;
- backed those helpers with the existing generated Unicode `ID_Start` and
  `ID_Continue` range tables already used by RegExp property support;
- changed runtime named-group validation to call the shared helper names instead
  of regex-local `js_regex_cp_is_id_*` functions;
- changed `js_regexp_compile_frontend()` named capture/backreference scanning to
  decode UTF-8 and validate the full decoded name through the shared helpers,
  replacing the previous byte-level `>= 0x80` acceptance shortcut.
- added opt-in identifier counters for AST construction and early-error Unicode
  normalization, gated by `LAMBDA_JS_IDENTIFIER_STATS=1`;
- counter output is per worker under `./temp/js_identifier_stats` by default, or
  under `LAMBDA_JS_IDENTIFIER_STATS_DIR` when set.

This keeps RegExp named-group validation, frontend pre-scan behavior, and the
generated Unicode property tables on one identifier policy.

### Measurement

Focused Unicode identifier probe:

| Metric | Result |
| --- | ---: |
| manifest | `temp/js262_tune9_unicode_identifier_focus.txt` |
| tests | 16 / 16 passed |
| failure rows | 0 |
| phase timing file | `temp/_t262_phase_timing_o0.tsv` |

Phase timing totals across the focused identifier manifest:

| Phase | Total |
| --- | ---: |
| parse | 43.5 ms |
| AST build | 136.7 ms |
| early errors | 3.0 ms |
| MIR build | 93.7 ms |
| execute | 378.4 ms |
| phase total | 719.3 ms |

Identifier counter output with `LAMBDA_JS_IDENTIFIER_STATS=1`:

| Counter | Value |
| --- | ---: |
| stats file | `temp/js_identifier_stats_tune9_p4/9181.tsv` |
| AST identifiers | 27103 |
| AST escaped identifiers | 13300 |
| AST non-ASCII decoded identifiers | 26600 |
| AST source bytes | 179521 |
| AST decoded bytes | 111887 |
| early identifier checks | 26706 |
| early escape checks | 13300 |
| early Unicode normalizations | 13300 |
| early reserved hits | 0 |
| early contextual escape hits | 0 |

Focused RegExp named-group probe:

| Metric | Result |
| --- | ---: |
| manifest | `temp/js262_tune9_regexp_named_groups_focus.txt` |
| tests | 22 / 22 passed |
| failure rows | 0 |

Full release gate:

| Metric | Result |
| --- | ---: |
| fully passed | 40261 / 40261 |
| non-fully-passing | 0 |
| regressions | 0 |
| total runtime | 112.5 s |
| failure rows | 0 |

### Decision

Keep the shared helper cleanup. It removes a duplicate Unicode identifier policy
and makes the RegExp frontend fail invalid non-ASCII names through the same
generated tables as runtime validation.

Do not proceed to parser/scanner changes yet. The focused timing shows early
errors are tiny for these rows, while execution, AST build, and MIR build are
the larger buckets. A safe next performance phase should instrument or optimize
the generated identifier-start row execution/AST/MIR shape rather than changing
grammar acceptance first.

Remaining follow-up:

1. Move the generated `ID_Start` / `ID_Continue` table ownership to a neutral
   helper module only when another translation unit needs direct table access.
2. Use the new identifier counters with MIR-volume or execute-path probes on the
   large generated `language_identifiers_start_unicode_*` rows before changing
   parser logic.
3. Keep parser/scanner edits gated on generated-grammar workflow and a focused
   root-cause measurement.
