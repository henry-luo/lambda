# Transpile JS Tune4 Proposal

Date: 2026-05-30
Status: proposed
Source run: `test/js262/results/release_run_006`

## Goal

Improve the slowest JS262 baseline cases from the latest release run without adding
test-specific shortcuts. The work should follow the successful pattern from Tune
and Tune3: find a broad runtime or compiler cost center, add a narrow generic
fast path, keep a simple fallback, and prove correctness with full baseline
verification.

This proposal intentionally avoids the Tune2 paths that were reverted or had no
measurable signal:

- Do not tune name-pool lookup for long identifier strings.
- Do not add an external scanner trie for Unicode identifiers.
- Do not lead with eval compile caching.
- Do not add pattern hacks for one or a few JS262 files.

## Latest Slow-Test Shape

The top 200 slow tests in `release_run_006/top_slow_tests.tsv` sum to about
65.50 seconds of per-test elapsed time. The whole release run reported about
516.89 seconds of summed per-test elapsed time, so the top 200 account for about
12.7% of measured test time.

| Cluster | Tests | Sum elapsed | Avg elapsed | Notes |
| --- | ---: | ---: | ---: | --- |
| URI codec | 27 | 17.06s | 0.632s | `encodeURI*`, `decodeURI*`, especially 4-byte escapes |
| Unicode identifiers | 21 | 22.74s | 1.083s | escaped and direct Unicode identifier suites |
| RegExp property escapes | 49 | 7.33s | 0.150s | still visible after Tune3 cursor win |
| RegExp misc/literal | 9 | 3.95s | 0.438s | mixed parse/execute costs |
| TypedArray | 59 | 9.48s | 0.161s | detach, constructor, property, set/copy, BigInt paths |
| Dynamic import/eval | 4 | 0.51s | 0.128s | not enough signal for Tune4 primary work |
| Class elements | 19 | 2.04s | 0.107s | broad but small total |
| Comments | 2 | 0.49s | 0.246s | parse-heavy, small total |
| Array bulk | 3 | 0.75s | 0.251s | possible future loop work |
| Map/Set bulk | 2 | 0.32s | 0.160s | too small for this pass |
| Other | 5 | 0.84s | 0.167s | scattered |

Top examples:

- `built_ins_decodeURIComponent_S15_1_3_2_A2_5_T1_js`: 5.876s
- `built_ins_decodeURI_S15_1_3_1_A2_5_T1_js`: 5.685s
- `language_identifiers_start_unicode_10_0_0_escaped_js`: 2.481s
- `language_identifiers_start_unicode_8_0_0_js`: 2.081s
- `built_ins_TypedArray_prototype_forEach_callbackfn_detachbuffer_js`: 0.588s
- `built_ins_TypedArrayConstructors_ctors_typedarray_arg_src_typedarray_big_throws_js`: 0.574s
- `built_ins_TypedArrayConstructors_internals_DefineOwnProperty_BigInt_detached_buffer_js`: 0.545s

## Prior Tuning Lessons

Tune and Tune3 produced useful wins when they targeted a repeated generic cost:

- Transient call-arg stack removed repeated dynamic-call argument allocation.
- Const-bound static dispatch reduced runtime lookup without changing semantics.
- Generator yield-spill fixes addressed a real runtime storage issue.
- RegExp property cursor improved repeated range lookup while preserving fallback.
- AST constant folding eliminated a broad shift-operator cluster.

Tune2 also showed what not to repeat:

- Name-pool length bypass and length hashing regressed or failed to help.
- External scanner ID trie was technically possible but had correctness and
  performance risk in the parser path.
- Eval compile cache had no useful signal for the measured baseline.

Tune4 should therefore favor:

- fast paths with a clear semantic guard,
- localized runtime/compiler changes,
- environment-gated A/B measurement during development,
- whole-baseline verification before accepting.

## Recommendation Summary

Proceed with two implementation candidates and one diagnostic candidate:

1. **T4-P1 URI codec scratch-buffer/table fast path**
   - Highest concentrated opportunity among runtime builtins.
   - Targets all URI encode/decode calls, not just A2.5.

2. **T4-P2 TypedArray raw view and bulk operations**
   - Broadest fresh cluster: 59 of the top 200 tests.
   - Targets repeated metadata lookup, boxing, and per-element dispatch.

3. **T4-P0 Unicode identifier instrumentation**
   - Largest total time, but prior direct parser/name-pool attempts failed.
   - Instrument parse/build/MIR/link/execute timing before proposing another
     implementation change.

RegExp property range-set work is a stretch candidate. Tune3 already improved
this area, and the remaining win is likely smaller and riskier than URI or
TypedArray.

## Implementation Approach

Each optimization must be implemented, measured, and accepted independently.
Do not combine URI, TypedArray, Unicode instrumentation, and RegExp work into
one mixed patch because that makes it impossible to attribute either wins or
regressions.

For each optimization:

1. Capture the current release baseline numbers before changing behavior.
2. Implement only one optimization behind a temporary development gate when
   practical.
3. Build the release version.
4. Run the release JS262 suite using the same flow that produced the baseline
   results.
5. Compare against the baseline:
   - pass/fail counts;
   - top slow tests;
   - affected cluster total;
   - total summed per-test elapsed;
   - wall-clock time where available.
6. Decide whether to keep, revise, or revert the optimization based on measured
   correctness and performance.

An optimization is worth keeping only if it preserves the passed JS262 baseline
and produces a material improvement in its target cluster. If the result is
neutral, noisy, or offset by regressions elsewhere, document the result and do
not keep the change by default.

## T4-P1: URI Codec Scratch-Buffer/Table Fast Path

### Current Shape

`lambda/js/js_globals.cpp` already contains useful URI fast paths:

- string inputs bypass `js_to_string`;
- no-percent decode returns the original string;
- single 4-byte percent escapes use `js_uri_try_decode_four_byte_escape`;
- the last 4-byte escape is cached by string item and heap epoch;
- URIError allocation is cached on failure.

`lambda/js/js_mir_expression_lowering.cpp` also recognizes some
`decodeURI*() === String.fromCharCode(...)` comparisons and routes them through
`js_uri_decode_equals_from_char_code`.

Despite that, the URI cluster is still 17.06s in the top 200. The top two tests
alone account for about 11.56s. This suggests the remaining cost is not just
one missing comparison fold. It is likely the general codec path: percent
scanning, temporary allocation, UTF-8 validation, and final heap string creation
repeated across many short strings.

### Proposed Optimization

Add a generic URI codec internal path that decodes or encodes into a reusable
scratch buffer and uses table-driven byte classification.

For decode:

- precompute a 256-entry hex nibble table with `-1` for non-hex bytes;
- scan once for `%` and malformed sequences;
- for short decoded results, decode into a fixed stack buffer;
- for longer decoded results, decode into a runtime scratch buffer owned by the
  JS context or a local arena-style scratch allocation;
- preserve the existing fallback for uncommon or risky cases;
- preserve `decodeURI` reserved-character behavior exactly.

For encode:

- scan once to detect all-unchanged ASCII strings and return the original item;
- use a precomputed percent-escape table for bytes that must be escaped;
- write into scratch storage rather than repeated small allocations;
- preserve surrogate handling and URIError behavior.

This should replace repeated generic allocation and conversion work, not add a
shortcut for the A2.5 files. The fast path should be selected by input shape
and URI rules only.

### Implementation Surface

Likely files:

- `lambda/js/js_globals.cpp`
- optional local helpers near existing URI helpers

Avoid adding broad API surface unless profiling shows multiple callers need it.

### Correctness Risks

URI behavior has many edge cases:

- malformed percent escapes;
- invalid UTF-8 continuation bytes;
- overlong UTF-8 encodings;
- surrogate boundaries;
- `decodeURI` reserved characters that must stay escaped;
- `encodeURI` versus `encodeURIComponent` allowed sets.

The fallback must remain easy to reason about. The fast path should reject and
fall back when it cannot prove it is in the supported shape.

### Verification

Use a focused URI manifest for iteration, then full baseline:

- all `built-ins/encodeURI*`
- all `built-ins/encodeURIComponent*`
- all `built-ins/decodeURI*`
- all `built-ins/decodeURIComponent*`

Acceptance:

- zero correctness regression in focused URI tests;
- no regression in JS262 baseline;
- URI cluster improves by at least 15% over release-run noise;
- no new hardcoded test names or input constants.

During development, gate with an environment flag such as:

```text
LAMBDA_JS_URI_FAST=0/1
```

Remove or default-enable the gate only after the full release baseline passes.

## T4-P2: TypedArray Raw View and Bulk Operations

### Current Shape

The TypedArray cluster is 59 of the top 200 tests, summing to 9.48s. The files
are not one narrow test family. They cover:

- constructors from typed-array sources;
- detached-buffer checks;
- BigInt typed arrays;
- property definition behavior;
- prototype methods such as `forEach`, `join`, and copying paths.

Code inspection shows repeated per-element overhead:

- `js_typed_array_get` recomputes current length and current data for each
  index, checks bounds, switches on element type, and boxes the result.
- `js_typed_array_set` repeats current length/data checks and type dispatch for
  each stored value.
- `js_typed_array_set_from` currently materializes `Item* values`, loops through
  `js_typed_array_get`, then loops through `js_typed_array_set`, even for many
  cases where a raw copy or hoisted conversion loop is sufficient.
- `js_typed_array_new_from_array` already has a same-type constructor memcpy
  path, which is a good pattern to extend carefully.

### Proposed Optimization

Introduce a small internal typed-array view helper for builtin loops:

```text
JsTypedArrayView:
  typed array pointer
  current data pointer
  current length
  byte offset / element size
  element type
  bigint-or-number category
```

The helper should validate detached/out-of-bounds state once when no user code
can execute between validation and element access. Builtins that call user
callbacks must still revalidate at the required spec points because callbacks
can detach buffers.

Then add generic raw loops:

- same-type bulk copy using `memmove` when source and target overlap;
- same-type constructor/set copy using `memcpy` when overlap is impossible;
- numeric cross-type conversion loops with the source and destination switches
  hoisted outside the element loop;
- BigInt typed-array copy/conversion loops separated from numeric arrays;
- raw read loops for no-callback methods such as `join`, `includes`, `indexOf`,
  `lastIndexOf`, `reverse`, `toReversed`, `slice`, `copyWithin`, and `fill`
  where semantics permit.

Do not skip detach checks around callback execution. For callback methods such
as `forEach`, `map`, `filter`, `reduce`, and `some`, the safe first step is only
to hoist stable metadata until the callback boundary, then revalidate afterward
when required.

### Implementation Surface

Likely files:

- `lambda/js/js_typed_array.cpp`
- `lambda/js/js_runtime.cpp`

Start with the lowest-risk operations:

1. `js_typed_array_set_from` same-type raw copy.
2. constructor-from-typed-array cross-type raw numeric conversion.
3. no-callback prototype methods that currently bounce through boxed `Item`
   access.

Defer callback-heavy methods unless the first phases leave the cluster
unchanged.

### Correctness Risks

TypedArray semantics are sensitive to:

- detached buffers;
- resizable buffers and out-of-bounds views;
- shared buffers;
- BigInt versus Number conversion;
- species constructors;
- overlapping source/target memory;
- property access and observable coercion order.

The optimization must only bypass boxing and repeated dispatch where no user
code can observe the intermediate steps.

### Verification

Use a focused TypedArray manifest for iteration:

- all `built-ins/TypedArray*`
- all `built-ins/%TypedArray%.prototype/*`
- BigInt typed-array constructor and conversion tests
- detached buffer tests

Acceptance:

- zero correctness regression in focused TypedArray tests;
- no regression in JS262 baseline;
- TypedArray top-200 cluster improves by at least 15%;
- callback detach tests continue to pass.

During development, gate with an environment flag such as:

```text
LAMBDA_JS_TA_RAW_FAST=0/1
```

## T4-P0: Unicode Identifier Instrumentation First

### Current Shape

Unicode identifier tests remain the largest top-200 cluster at 22.74s. Prior
Tune2 work found that direct name-pool and external-scanner approaches were not
successful enough to keep.

The remaining cost might be parse time, AST building, MIR generation, linker
state growth across a batch, or execution. The next step should determine which
phase dominates before changing code.

### Proposed Diagnostic

Add optional per-test phase timing for the JS262 runner:

- parse;
- AST/build or compile preparation;
- MIR generation;
- link/finalize;
- execute;
- cleanup/reset.

Use it on the Unicode identifier subset and compare:

- normal batch order;
- shuffled order;
- standalone execution of the same slow files;
- a smaller batch containing only Unicode identifier tests.

If the slowdown is caused by accumulated compile/linker state, revisit the
Tune2 soft-reset idea as an implementation candidate:

- preserve preloaded harness/preamble state;
- reset per-test generated symbols and transient compile artifacts;
- keep observable JS global behavior unchanged.

If the slowdown is parse-bound, avoid the previously failed external scanner
path and consider only grammar/runtime-neutral parser improvements with strong
evidence.

### Acceptance

This phase is complete when the Unicode cluster has an attributed dominant cost
and a measured standalone-versus-batch profile. It does not need to change
runtime behavior.

## Stretch: RegExp Property Range-Set Containment

Tune3 kept the cursor optimization for repeated RegExp property range lookup,
and the shift-operator cluster largely disappeared. Property escapes still
account for 49 of the top 200 tests and 7.33s, so there may be a second generic
win.

Potential follow-up:

- represent monotonic generated input as code-point ranges;
- compare those ranges against generated property ranges in bulk;
- use the existing cursor path as fallback for non-monotonic or short inputs.

This is lower priority than URI and TypedArray because the previous cursor work
already captured the obvious local win, and a range-set implementation has more
surface area.

## Measurement Plan

1. Capture the current release-run numbers as baseline:
   - top-200 total;
   - cluster totals;
   - focused URI and TypedArray subset totals.

2. Implement one phase at a time behind an environment gate:
   - `LAMBDA_JS_URI_FAST=0/1`
   - `LAMBDA_JS_TA_RAW_FAST=0/1`

3. Run focused subsets in release mode for fast iteration.

4. Run the complete JS262 baseline in release mode before accepting:

```bash
make release
make build-test
make test262-full
```

If `make test262-full` is not the active release-run path for this branch, use
the same command line that produced `test/js262/results/release_run_006` and
capture a new release results directory.

5. Compare:
   - pass/fail counts;
   - top slow tests;
   - cluster totals;
   - total summed per-test elapsed;
   - wall-clock time where available.

## Proposed Tune4 Order

1. Add Unicode phase instrumentation if cheap, so future parser/compiler work is
   evidence-led.
2. Implement T4-P1 URI scratch-buffer/table fast path.
3. Verify URI focused subset and full JS262 baseline.
4. Implement T4-P2 TypedArray raw view and same-type copy first.
5. Expand TypedArray raw loops only where the first step proves safe and useful.
6. Re-profile top 200 and decide whether RegExp range-set containment is worth a
   Tune5 proposal.

## Completion Criteria

Tune4 should be accepted only if:

- JS262 baseline still passes with no new skips;
- no hardcoded test-name or test-constant logic is added;
- at least one primary cluster improves materially:
  - URI cluster: target 15% or better;
  - TypedArray cluster: target 15% or better;
- changes are covered by focused subset runs and a full release baseline run;
- the proposal and resulting implementation notes record any reverted or
  rejected idea so the same path is not repeated later.
