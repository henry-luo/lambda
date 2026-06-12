# Transpile_Js54_Es2024 - ES2024 Completion: Resizable Buffers, transfer Family, /v Flag

Date: 2026-06-12

Status: proposed

Js54 finishes the ES2024 baseline admission work that [Transpile_Js_53_Es2024.md](Transpile_Js_53_Es2024.md) deferred. Three gates remain open from Js53: **resizable-arraybuffer** (P4 in Js53 numbering, ≈463 tests), **arraybuffer-transfer** (P5, 59 tests), and **regexp-v-flag** (P6, 187 tests). Unlike Js53's mostly-runner/admission work, Js54 is **net-new engine implementation** across two subsystems (TypedArray/DataView OOB-aware semantics, regex `/v` flag features). The Js53 proposal §11 documented exactly what's missing; this proposal turns that diagnosis into a phased landing plan.

## 1. Starting Baseline

Current checked-in release baseline at Js54 start (post-Js53 P3-revisit):

```text
# Scope: ES2023 (skip ES2024+ features)
# Total passing: 39539
# Total tests: 42889  Skipped: 3310  Batched: 39579  Passed: 39539  Failed: 38
# Runtime: 130.0s total wall (prep 0.0s + exec 129.6s)
# Batch size: batched 50 tests/process; async 50 tests/process
```

The Js54 acceptance bar:

- Passing count stays `>= 39539` after every phase.
- Regressions count is `0` at every phase boundary.
- `t262_partial.txt` does not gain new entries; the existing 23 carry over unchanged.
- Total runtime stays within `+5%` of the P0-captured baseline (multi-run trend, not single-run hard fail).
- Final `# Scope:` line in [test/js262/test262_baseline.txt](../../test/js262/test262_baseline.txt) updated to `ES2024 (skip ES2025+ features)` once all three gates close.

## 2. What Js53 Deferred

Per [Transpile_Js_53_Es2024.md §11](Transpile_Js_53_Es2024.md):

| Gate | Js53 phase | Tests total | Already pass | To admit | Fix surface |
|---|---|---:|---:|---:|---|
| resizable-arraybuffer | P4 (D1) | 463 | 325 | ≈138 | TypedArray/DataView OOB + length-tracking + 1 SIGSEGV |
| arraybuffer-transfer | P5 (D2) | 59 | 0 | 59 | Net-new `ArrayBuffer.prototype.transfer{,ToFixedLength}` |
| regexp-v-flag | P6 (E) | 187 | 76 | ≈111 | Validator + rewriter + 7 string-property tables |
| **Total** | | **709** | **401** | **≈308** | |

The probe artifacts retained from Js53 in `temp/js53_repros/`:

- [temp/js53_repros/gate_d1_resizable.txt](../../temp/js53_repros/gate_d1_resizable.txt) — 463 entries
- [temp/js53_gate_d1_probe.tsv](../../temp/js53_gate_d1_probe.tsv) — 138 failure rows
- [temp/js53_gate_d1_probe_by_path.tsv](../../temp/js53_gate_d1_probe_by_path.tsv) — per-path breakdown
- [temp/js53_repros/gate_e_v_flag.txt](../../temp/js53_repros/gate_e_v_flag.txt) — 187 entries
- [temp/js53_gate_e_probe.tsv](../../temp/js53_gate_e_probe.tsv) — 111 failure rows
- [temp/js53_gate_e_probe_by_path.tsv](../../temp/js53_gate_e_probe_by_path.tsv) — per-path breakdown

P5 has no probe artifact because Js53 §11 P5 noted "the test outcomes depend on the P4 detach semantics being correct first." Js54 generates it after P4 work lands.

## 3. The Three Gates

### Gate A (Js54) — resizable-arraybuffer (Js53 P4 / D1)

463 tests; **138 failures across 7 path categories and 8 failure clusters**. Per the Js53 §11 P4 diagnosis:

| Failure cluster | Approx count | Root cause |
|---|---:|---|
| "Expected TypeError but got Test262Error" | ~38 | Engine fails to throw `TypeError` on OOB-TA / OOB-DataView access |
| "Expected TypeError to be thrown but no exception" | 9 | Same root cause, slightly different test wrapper |
| "Invalid typed array byteLength" | ~12 | RangeError on TA construction in `makePassthrough` scenarios |
| Array-content mismatch | ~25 | TA prototype methods memoize length at entry instead of re-reading |
| "Cannot convert a BigInt value to a number" | 8 | BigInt TA detach interaction in coercion path |
| Over-eager OOB throw (`Cannot perform iterator/slice/fill on OOB ArrayBuffer`) | ~10 | Spec for iteration says "treat OOB length as 0" — engine throws instead |
| TDZ ("Cannot access X before initialization") | ~6 | Secondary symptom of earlier-test-fixture errors |
| **SIGSEGV** | **1** | `built-ins/TypedArray/out-of-bounds-behaves-like-detached.js` — indexed access after `rab.resize(0)` dereferences a freed/moved pointer |

**The 1 SIGSEGV is the highest-priority sub-item.** It's a memory-safety bug that exists today; it's not new from this gate, but it surfaces on any test that resizes a buffer to zero and then reads through a now-OOB TA. Fix-first before any other admission work.

Fix surface (verbatim from Js53 §11 P4):

1. **`JsDataView` struct** ([lambda/js/js_typed_array.h:44–49](../../lambda/js/js_typed_array.h)) needs a `length_tracking` field (mirroring `JsTypedArray.length_tracking`). The DataView constructor at [lambda/js/js_typed_array.cpp:2427+](../../lambda/js/js_typed_array.cpp) needs to set it when called with no explicit `byteLength` argument.

2. **DataView accessor dispatch** ([lambda/js/js_runtime.cpp:11520–11553](../../lambda/js/js_runtime.cpp)) needs OOB-aware logic for `byteLength`, `byteOffset`, and every `getInt8`/`getUint8`/...`setBigUint64` accessor (~28 methods total via [js_runtime_builtin_registry.cpp:362–404](../../lambda/js/js_runtime_builtin_registry.cpp)).

3. **TypedArray `length` getter and indexed access**: every read needs to re-derive length from `ab->byte_length` when `ta->length_tracking` is true, and check OOB. The crash test proves the current indexed-access path doesn't.

4. **TypedArray prototype methods (~30 methods)**: each one needs to call into a shared "validate-and-get-current-length" helper at method entry (and again after each `valueOf`/`toString` callback that might trigger resize). Today the methods cache length at entry.

5. **Array.prototype methods that accept TA inputs**: the same length-re-derivation discipline must extend through `Array.from`, `Array.prototype.{at,copyWithin,fill,slice,splice}`, `Array.of`, etc. — ~50 tests fail here.

6. **ArrayBuffer detached-buffer return values**: `maxByteLength` must return 0 when `ab->detached` is true ([lambda/js/js_typed_array.cpp:1267–1272](../../lambda/js/js_typed_array.cpp) does not check); same for a handful of other getters. Small isolated fixes.

### Gate B (Js54) — arraybuffer-transfer (Js53 P5 / D2)

59 tests, **net-new code**. Spec §25.1.5.3 and §25.1.5.4:

- `ArrayBuffer.prototype.transfer(newLength)` — creates a new ArrayBuffer of `newLength` (default `byteLength`), copies bytes, detaches the original. Preserves `resizable` and `maxByteLength` from the source.
- `ArrayBuffer.prototype.transferToFixedLength(newLength)` — same, but produces a non-resizable buffer regardless of source.
- Both detach the source via the existing detach path (`IsDetachedBuffer` semantics already exist in the engine for `ArrayBuffer.prototype.slice` and TA constructors — reuse).

Work:

1. Add `JS_BUILTIN_AB_TRANSFER` and `JS_BUILTIN_AB_TRANSFER_TO_FIXED_LENGTH` enum values in [lambda/js/js_runtime_builtin_registry.cpp](../../lambda/js/js_runtime_builtin_registry.cpp) and dispatch in [lambda/js/js_runtime.cpp](../../lambda/js/js_runtime.cpp).
2. Add `extern "C" Item js_arraybuffer_transfer(...)` and `js_arraybuffer_transfer_to_fixed_length(...)` in [lambda/js/js_typed_array.cpp](../../lambda/js/js_typed_array.cpp).
3. Wire the prototype entries on `ArrayBuffer.prototype` at [js_runtime_builtin_registry.cpp:362+](../../lambda/js/js_runtime_builtin_registry.cpp) (same block as `byteLength`/`resizable`/`maxByteLength` getters).
4. Confirm the GC story: the source buffer's backing store is freed only after the new buffer takes ownership. Use the same handoff pattern already used by `ArrayBuffer.prototype.slice`.

P5 depends on P4 — many `arraybuffer-transfer` tests construct typed-array views over the transferred buffer and verify the views observe the detach. Without P4's correct detach/OOB semantics through the TA prototype, those tests can't pass even if `transfer` itself is correct.

### Gate C (Js54) — regexp-v-flag (Js53 P6)

187 tests; **111 failures across 5 sub-clusters**. Per the Js53 §11 P6-revisit diagnosis:

| Sub-cluster | Failures | Layers needed | Approx LOC | Data size |
|---|---:|---|---:|---:|
| `[A--B]` / `[A--Y]` set difference | 34 | 1 + 2 | ~250 | — |
| `[A&&B]` set intersection | 27 | 1 + 2 | ~50 (reuses Layer 2 ranges helper) | — |
| `\p{StringProperty}` outside class | ~14 | 1 + 2 + 3 | ~300 | + 5–10 KB |
| `\p{StringProperty}` inside class | ~7 | 1 + 2 + 3 | (subset of above) | (shared) |
| `[\q{ab|cd|...}]` quoted-string alt | ~10 | 1 + 2 | ~100 | — |
| Mixed / secondary | ~19 | combinations | — | — |

The three layers are dependent:

**Layer 1 — JS source validator** ([lambda/js/js_regex_wrapper.cpp:878–1106](../../lambda/js/js_regex_wrapper.cpp) `validate_unicode_strict`).

The validator's inside-class branch treats nested `[` as a literal character. Pattern `[[0-9]--_]` is parsed as `[`, `0-9` range, `]` (ending the inner class), then `-`, `-`, `_`, `]` — the final `]` is then standalone outside-class, which line 984 rejects with "standalone `]` illegal under `u`". The error then surfaces as `"Annex B legacy syntax not allowed under \`u\` flag"` (caller's error formatter).

Layer 1 fix: ~50–100 LOC of `/v`-aware nested-class tracking, `--`/`&&` operator parsing, and `\q{...}` recognition. **Must land first — no `/v` test admits without this.**

**Layer 2 — Pattern rewriter** ([lambda/js/js_regex_wrapper.cpp:410+](../../lambda/js/js_regex_wrapper.cpp) `rewrite_pattern`).

Even with validation fixed, RE2 will reject every `[A--B]` / `[A&&B]` / `[\q{...}]` because RE2 has no set-op or quoted-string-alt syntax. The rewriter needs to:

- Parse the operands A, B as code-point sets (each can be a single char, range `a-z`, char class escape `\d`/`\w`/etc., property escape `\p{...}`, nested set-op class, OR `\q{...}` string list).
- Compute the difference/intersection over code-point ranges.
- Emit a flat RE2-compatible character class for the result.
- For `\q{...}` (quoted string alternation): rewrite the entire enclosing class as `(?:lit1|lit2|...|[remaining-chars])` outer alternation.

Layer 2 fix: ~200–300 LOC of careful regex IR work, including a code-point range list helper (insert/union/diff/intersect/sort), test cases for every operand combination, and care to preserve flag interactions (case-insensitivity, Unicode-aware matching).

**Layer 3 — String-valued Unicode property tables** ([lambda/js/js_regex_generated_property_tables.inc](../../lambda/js/js_regex_generated_property_tables.inc)).

ES2024 introduces 7 string properties: `Basic_Emoji`, `Emoji_Keycap_Sequence`, `RGI_Emoji`, `RGI_Emoji_Flag_Sequence`, `RGI_Emoji_Modifier_Sequence`, `RGI_Emoji_Tag_Sequence`, `RGI_Emoji_ZWJ_Sequence`. Unlike code-point properties, these match **multi-codepoint sequences** (e.g. flag sequences are 2 code points; ZWJ sequences are 3+). They cannot be emitted as a character class; they must become alternation of the contained string sequences.

- Source data: Unicode CLDR / `emoji-test.txt` for the relevant Unicode version (pin to whatever the rest of `js_regex_generated_property_tables.inc` already uses).
- Estimated table size: 5–10 KB across the 7 properties (RGI_Emoji alone has ~3,000+ sequences).
- Layer 3 fix: data generator script + ~2 KLOC of generated tables + ~100 LOC of property-table lookup integration in the rewriter.

**Estimated total for full Gate C: ~400 LOC of regex-engine code + 5–10 KB Unicode data + test-vector validation across the 187 tests.**

## 4. Phase Plan

Phases ordered to minimize blast radius and dependency cycles. P4 and P6 work on disjoint subsystems and can branch in parallel; P5 depends on P4 and lands after.

### P0 — Baseline Capture and SIGSEGV Triage

Goal: pin the exact pre-Js54 numbers, and fix the highest-priority memory-safety bug **before** any other admission work.

Work:

1. Re-run the release js262 guard against the current `test262_baseline.txt`. Confirm `39539 / 39539`, 0 failures, 0 regressions. Capture runtime as the P0 number for the +5% ceiling.
2. **SIGSEGV root-cause and fix** — `built-ins/TypedArray/out-of-bounds-behaves-like-detached.js` crashes on indexed access after `rab.resize(0)`. Use the existing `temp/js53_repros/gate_d1_resizable.txt` probe as the work-list. ASAN run on the crash test → reduce to minimal repro → fix the dereference. Add a focused C++ unit test in [test/test_js_gtest.cpp](../../test/test_js_gtest.cpp) for the reduced repro.
3. Snapshot the current skip-list region ([test/test_js_test262_gtest.cpp:170–177](../../test/test_js_test262_gtest.cpp)) into this proposal as the diff anchor.

Acceptance:

- `temp/js54_p0_release_guard.tsv` exists with `Failed: 0`, `Passing: 39539`, `Runtime` recorded.
- The SIGSEGV reduced repro no longer crashes under ASAN.
- No engine change beyond the SIGSEGV fix.
- Release guard passes.

### P1 — Gate A (resizable-arraybuffer) sub-phase: Detached-buffer Returns

Risk class: smallest. Isolated getters with `if (ab->detached) return 0;` style fixes.

Work:

1. `ArrayBuffer.prototype.maxByteLength` ([lambda/js/js_typed_array.cpp:1267–1272](../../lambda/js/js_typed_array.cpp)) must return 0 when `ab->detached`.
2. Audit the other ArrayBuffer accessor getters in the same file for the same gap (`byteLength`, `resizable`, etc. — likely fewer than 5 sites).
3. Same audit for SharedArrayBuffer accessors (lines ≈1488+).
4. Test the `built-ins/ArrayBuffer/prototype/maxByteLength/detached-buffer.js` probe — expected to pass after this change.

Acceptance:

- The 3 explicit `built-ins/ArrayBuffer/prototype/{maxByteLength,byteLength,resizable}/detached-buffer.js` tests pass.
- Existing ArrayBuffer tests pass byte-for-byte.
- Release guard clean.
- Passing count rises by ~3–5.

### P2 — Gate A: DataView OOB-aware accessors

Risk class: small-to-medium. ~24 DataView tests affected; isolated to DataView prototype.

Work:

1. Add `bool length_tracking` to `JsDataView` struct at [lambda/js/js_typed_array.h:44–49](../../lambda/js/js_typed_array.h). Initialize to `true` when the DataView constructor at [lambda/js/js_typed_array.cpp:2427+](../../lambda/js/js_typed_array.cpp) is called with no explicit `byteLength` argument.
2. In the DataView accessor dispatch at [lambda/js/js_runtime.cpp:11520–11553](../../lambda/js/js_runtime.cpp), add OOB check before every read:
   - Compute `current_view_end = dv->byte_offset + (length_tracking ? dv->buffer->byte_length - dv->byte_offset : dv->byte_length)`.
   - If `current_view_end > dv->buffer->byte_length` OR `dv->buffer->detached` → throw `TypeError`.
   - For `byteLength` getter on length-tracking view: return `max(0, dv->buffer->byte_length - dv->byte_offset)` instead of `dv->byte_length`.
3. Wire OOB check into every `get*` / `set*` method (~28 entries via [js_runtime_builtin_registry.cpp:362–404](../../lambda/js/js_runtime_builtin_registry.cpp)). The check is identical at the method entry, so extract into a `js_dataview_validate_or_throw(dv)` helper.
4. Add a focused C++ unit test in [test/test_js_gtest.cpp](../../test/test_js_gtest.cpp) covering: fixed-length DV stays valid after grow, fixed-length DV throws on shrink-past-end, length-tracking DV `byteLength` updates on resize, length-tracking DV throws when offset > buffer length.

Acceptance:

- All `built-ins/DataView/prototype/{byteLength,byteOffset,get*,set*}/resizable-*.js` tests pass.
- Existing DataView tests pass byte-for-byte.
- Release guard clean.
- Passing count rises by ~24.

### P3 — Gate A: TypedArray length-tracking and indexed access

Risk class: medium. The TA length getter and indexed access are hot paths used by every typed-array operation.

Work:

1. TypedArray `length` getter (search for the field-read site in [lambda/js/js_runtime.cpp](../../lambda/js/js_runtime.cpp) or [js_typed_array.cpp](../../lambda/js/js_typed_array.cpp)): when `ta->length_tracking` is true, re-derive as `max(0, (ab->byte_length - ta->byte_offset) / sizeof(element))` and check OOB.
2. Indexed access via the integer-index Get/Set path (handles `ta[i]`). On every access:
   - Recompute current length if `length_tracking`.
   - If index out of bounds OR buffer detached → return `undefined` (get) or no-op (set), per spec.
   - Per the spec, the **crash test** in `built-ins/TypedArray/out-of-bounds-behaves-like-detached.js` expects this exact behavior. The P0 fix may have addressed the crash; this phase ensures the spec semantics are also correct.
3. Add C++ unit tests for: length-tracking TA `length` updates on grow/shrink, OOB-TA `length === 0`, OOB-TA index reads return `undefined`, OOB-TA `in` operator returns false.

Acceptance:

- The `out-of-bounds-behaves-like-detached.js` test passes (was the P0 crash test).
- TypedArray length-tracking and OOB-related tests pass.
- All previously-passing TypedArray tests pass byte-for-byte.
- Release guard clean.
- Passing count rises by ~30.

### P4 — Gate A: TypedArray prototype methods (~30 methods)

Risk class: medium-to-large. The prototype methods are the largest single fix surface.

Work:

1. Identify the prototype methods that take an integer length or iterate over `ta[i]` (the `built-ins/TypedArray/prototype/*` failures). Likely: `at`, `copyWithin`, `every`, `fill`, `filter`, `find`, `findIndex`, `findLast`, `findLastIndex`, `forEach`, `includes`, `indexOf`, `join`, `lastIndexOf`, `map`, `reduce`, `reduceRight`, `set`, `slice`, `some`, `sort`, `subarray`, `toLocaleString`, `toReversed`, `toSorted`, `toString`, `values`, `with`. ~28 methods.
2. Extract a shared helper `js_ta_get_current_length_or_throw(ta)` (or `_or_oob_indicator` depending on the spec for each method).
3. In each method's implementation: replace the cached-at-entry `length` read with the helper. Re-call the helper after each user-callback invocation that could trigger a resize (`valueOf`, `toString` on indexed iteration values).
4. Per-spec, **some methods throw on OOB and some treat OOB as length-0 silently**. Read each method's spec text in the failure-row metadata. The Js53 §11 P4 cluster "Over-eager OOB throw" identifies ~10 methods that currently throw but shouldn't.

Risk controls:

- Run each method's chapter in test262 as soon as it's edited (e.g. `--gtest_filter='*TypedArray*copyWithin*'`).
- Run the `language/expressions/array-literal/*` and `language/expressions/spread/*` chapters as well — spread of a TA into an array literal goes through TA iteration.

Acceptance:

- The ~58 `built_ins/TypedArray` failures from the Js53 §11 P4 by-path table reduce to 0.
- Existing TypedArray method tests pass byte-for-byte.
- Release guard clean.
- Passing count rises by ~50.

### P5 — Gate A: Array.prototype methods on TA inputs

Risk class: medium. Spillover of the same length-tracking discipline into `Array.from`, `Array.prototype.{at, copyWithin, fill, slice, splice}`, `Array.of`, etc.

Work:

1. Audit the Js53 §11 P4 by-path summary: `built_ins/Array` has 50 failures, all in the `resizable-arraybuffer` feature set. Most exercise `Array.from(typedArrayOverResizedBuffer)` or similar.
2. In each affected `Array.prototype.*` method that accepts an array-like input (which includes TAs): when the input is a TA, use the length-helper from P3 rather than the cached `length` field.
3. `Array.from` with a TA iterable: the iteration path goes through the TA's iterator, which P3 already handles. Confirm.
4. `Array.prototype.copyWithin` / `slice` / `splice` on a TA receiver: these are inherited from Array but executed on TA semantics — confirm the OOB checks fire correctly.

Acceptance:

- The ~50 `built_ins/Array` failures from the Js53 §11 P4 by-path table reduce to 0.
- Release guard clean.
- Passing count rises by ~50.

### P6 — Gate A: BigInt + Symbol.species clusters

Risk class: small. The remaining ~20 failures from the Js53 §11 P4 by-feature table: 11 BigInt, 12 Symbol.species, 1 Reflect.construct.

Work:

1. BigInt + detach interaction (8–11 tests): the BigInt TA detach path's coercion is failing with "Cannot convert a BigInt value to a number" on operations that should already have thrown earlier. Likely a missing detach-check in the BigInt-specific coercion helper.
2. Symbol.species clamping under OOB (12 tests): TA prototype methods that use `[[Species]]` for their result type must clamp the result length to the current buffer length when species-derive.
3. Reflect.construct (1 test): isolated; debug per-test.

Acceptance:

- The remaining Gate A failures reduce to 0.
- Release guard clean.
- Passing count rises by ~15.

### P7 — Gate A: probe + admit + admission predicate

Goal: with all 138 Gate A failures fixed, admit all 463 tests to baseline.

Work:

1. Re-probe `temp/js53_repros/gate_d1_resizable.txt` (now `temp/js54_gate_a_probe.tsv` after re-run). Confirm 463/463 pass.
2. Generate per-test list of admissions. Any async-flagged tests in the set need an admission predicate similar to Js53's `is_js53_waitasync_admission_test`. Add `is_js54_resizable_arraybuffer_admission_test` if needed.
3. Remove `"resizable-arraybuffer"` from the skip list at [test/test_js_test262_gtest.cpp:171](../../test/test_js_test262_gtest.cpp) (it currently has the `(deferred to Js54 — ...)` comment marker).
4. Run release baseline guard. Then `--update-baseline`.

Acceptance:

- Skip-list entry removed.
- Baseline header bumps by ~138 (the failures that turned to passes).
- 0 regressions.
- Release guard clean.

### P8 — Gate B (arraybuffer-transfer)

Goal: implement `ArrayBuffer.prototype.transfer` and `transferToFixedLength`, admit the 59 tests.

Work:

1. Generate the probe: `awk -F'\t' '/arraybuffer-transfer/ {print $1}' test/js262/test262_metadata.tsv | sanitize > temp/js54_gate_b_probe.txt`.
2. Add `JS_BUILTIN_AB_TRANSFER` and `JS_BUILTIN_AB_TRANSFER_TO_FIXED_LENGTH` enum entries to the `JsBuiltinId` enum (search [lambda/js/js_runtime.h](../../lambda/js/js_runtime.h) for the existing enum) and to the dispatch switch in [js_runtime.cpp](../../lambda/js/js_runtime.cpp).
3. Add `extern "C" Item js_arraybuffer_transfer(Item this_val, Item new_length)` and `js_arraybuffer_transfer_to_fixed_length(...)` in [js_typed_array.cpp](../../lambda/js/js_typed_array.cpp):
   - Validate `this_val` is an ArrayBuffer (not SharedArrayBuffer).
   - Coerce `new_length` to integer (default = `ab->byte_length`).
   - Allocate new ArrayBuffer with `new_length` bytes.
   - Copy `min(ab->byte_length, new_length)` bytes from source to new.
   - For `transfer`: preserve `resizable` and `maxByteLength`. For `transferToFixedLength`: force `resizable = false`.
   - Detach the source via the existing detach path (mirror what `ArrayBuffer.prototype.slice` does).
   - Return the new buffer.
4. Wire the prototype entries at [js_runtime_builtin_registry.cpp:362+](../../lambda/js/js_runtime_builtin_registry.cpp) — `{"transfer", 8, JS_BUILTIN_AB_TRANSFER, -1}` and `{"transferToFixedLength", 21, JS_BUILTIN_AB_TRANSFER_TO_FIXED_LENGTH, -1}`.
5. Add C++ unit tests covering: zero-arg `transfer()` keeps length, larger-arg expands, smaller-arg shrinks, source detached after transfer, double-transfer throws, transfer of resizable preserves `maxByteLength`, `transferToFixedLength` returns non-resizable result, transfer through TA view propagates detach.
6. Remove `"ArrayBuffer-transfer"` and `"arraybuffer-transfer"` from the skip list at [test/test_js_test262_gtest.cpp:172](../../test/test_js_test262_gtest.cpp).
7. Run probe → release guard → update-baseline.

Acceptance:

- All 59 `arraybuffer-transfer` tests pass in the probe.
- 0 regressions across the existing baseline.
- Passing count rises by ~59.

### P9 — Gate C (regexp-v-flag) Layer 1: validator nested-class parser

Risk class: small. Single function update; no semantic change to existing `/u` regex behavior.

Work:

1. In `validate_unicode_strict` at [js_regex_wrapper.cpp:878+](../../lambda/js/js_regex_wrapper.cpp), track class-nesting depth instead of a boolean `in_class`. When the flag is `v` (passed via a new parameter), `[` inside a class opens a nested class.
2. Add `--` and `&&` operator parsing inside classes under `/v`: when found between class elements, consume the two characters and continue. Do not allow these tokens under plain `/u`.
3. Add `\q{...}` recognition inside classes under `/v`: when `\q{` is seen, consume up to and including `}` (parsing the contents as a `|`-separated list of strings, each being a sequence of valid `/u` escapes). Reject `\q` outside of `/v`.
4. Pass the `v` flag through from the call site at [js_regexp_compile.cpp:230–237](../../lambda/js/js_regexp_compile.cpp) — wire `out->unicode_sets` as a parameter to `validate_unicode_strict`.

Risk controls:

- Diff parse trees of every existing `test/js/regex_*.js` test before/after the change. Treat any diff as a regression.
- Run the entire `built-ins/RegExp/property-escapes/generated/*` chapter on debug before release.

Acceptance:

- `[A--B]` patterns parse without "standalone `]`" error.
- `[A&&B]` patterns parse without error.
- `[\q{ab|cd}]` patterns parse without "Annex B legacy syntax" error.
- `/v` patterns are accepted at the validator layer; they still fail at RE2 (rejected by RE2's parser). That's expected — Layer 2 fixes it.
- Existing `/u` regex tests pass byte-for-byte.
- Passing count unchanged (Layer 2 is what unblocks tests).

### P10 — Gate C Layer 2: pattern rewriter (set ops + `\q{...}`)

Risk class: medium-to-large. Net-new regex IR code; touches the rewriter that every regex flows through.

Work:

1. Add a code-point range list data structure to [js_regex_wrapper.cpp](../../lambda/js/js_regex_wrapper.cpp): sorted, non-overlapping ranges of code points; ops `add_range`, `union`, `difference`, `intersection`, `negate`.
2. In `rewrite_pattern` (or a new `rewrite_v_flag_pattern`), when `unicode_sets` is true and the current position is inside a class:
   - Parse the class as a sequence of class elements (chars, ranges, escapes, property escapes, nested classes, `\q{...}`).
   - Build a range list for the class's matched-characters set.
   - If `\q{X|Y|Z}` is present: split the class into "string-list" and "char-list" components. Emit as `(?:X|Y|Z|[char-list])` outer alternation. Save outer-context to thread quantifier (e.g. `+`, `*`) through to the alternation.
   - If `--` / `&&` operators are present: compute the resulting range list per spec semantics and emit as a flat character class.
3. For `\p{...}` inside `/v` class: if it's a non-string property (code-point property), look up in the existing `js_regex_generated_property_tables.inc`. If it's a string property (handled in Layer 3), defer.
4. Test cases: every entry in `temp/js53_gate_e_probe2.tsv`'s set-difference and set-intersection categories should pass.

Risk controls:

- Pre-flight every `built-ins/RegExp/character-classes/*` and `built-ins/RegExp/unicodeSets/generated/*` test on debug before release.
- Diff captured `temp/js53_repros/gate_e_v_flag.txt` probe results: was 76 / 187 pass; after Layer 2 should rise to ~140 / 187 (Layer 3 covers the remaining string-property gap).

Acceptance:

- 34 set-difference tests pass.
- 27 set-intersection tests pass.
- ~10 `\q{...}` tests pass.
- Existing `/u` regex tests pass byte-for-byte.
- Release guard clean.
- Passing count rises by ~71.

### P11 — Gate C Layer 3: string-valued property tables

Risk class: medium. Net-new data file + table lookup; data-only changes are easy to review.

Work:

1. Generate property tables for the 7 ES2024 string properties from the Unicode CLDR / `emoji-test.txt` source (pinned Unicode version matches the existing `js_regex_generated_property_tables.inc` source data — check what version that is and stay aligned).
2. Output format: per-property C-array of UTF-8 string literals. Either inline into [js_regex_generated_property_tables.inc](../../lambda/js/js_regex_generated_property_tables.inc) or add a sibling file `js_regex_string_properties.inc`.
3. Wire property-table lookup into the rewriter from Layer 2: when `\p{StringProperty}` appears inside or outside a class under `/v`, expand it to alternation of the string-table entries.
4. Inside a class: `[A\p{Basic_Emoji}B]` → outer alternation `(?:emoji1|emoji2|...|[AB])` (Layer 2 already wires this for `\q{...}`; this is the same mechanism with property-derived sequences).
5. Outside a class: `\p{Basic_Emoji}+` → `(?:emoji1|emoji2|...)+`.
6. Add a unit test that loads each of the 7 properties and matches a representative sequence.

Risk controls:

- The generator script must be reproducible from a documented command, with the Unicode version pinned in this proposal.
- Regenerated tables must NOT change behavior of `/u`-only property escapes (the existing tables stay unchanged; new tables are additive).

Acceptance:

- `\p{Basic_Emoji}`, `\p{Emoji_Keycap_Sequence}`, `\p{RGI_Emoji}`, `\p{RGI_Emoji_Flag_Sequence}`, `\p{RGI_Emoji_Modifier_Sequence}`, `\p{RGI_Emoji_Tag_Sequence}`, `\p{RGI_Emoji_ZWJ_Sequence}` all match the expected sequences.
- All ~21 string-property tests in `built-ins/RegExp/property-escapes/generated/strings/*` pass.
- Existing `/u` property tests pass byte-for-byte.
- Release guard clean.
- Passing count rises by ~21.

### P12 — Gate C: probe + admit + admission predicate

Goal: with all 111 Gate C failures fixed, admit all 187 tests.

Work:

1. Re-probe `temp/js53_repros/gate_e_v_flag.txt`. Confirm 187/187 pass.
2. Add `is_js54_regexp_v_flag_admission_test` predicate if any of the tests are async-flagged.
3. Remove `"regexp-v-flag"` from the skip list at [test/test_js_test262_gtest.cpp:173](../../test/test_js_test262_gtest.cpp).
4. Run release baseline guard. Then `--update-baseline`.

Acceptance:

- Skip-list entry removed.
- Baseline header bumps by ~111.
- 0 regressions.
- Release guard clean.

### P13 — Scope-line flip

Goal: with all five ES2024 gates open (Gate A from Js53 P1 + Js54 P7, Gate B from Js53 P3 + Js54 P8, Gate C from Js54 P9–P12), flip the baseline scope label.

Work:

1. In [test/test_js_test262_gtest.cpp:2023](../../test/test_js_test262_gtest.cpp), change the literal `# Scope: ES2023 (skip ES2024+ features)` to `# Scope: ES2024 (skip ES2025+ features)`.
2. Run release with `--update-baseline`. The header line in [test/js262/test262_baseline.txt](../../test/js262/test262_baseline.txt) updates.
3. Final guard run confirms `Passing` matches the expected upper bound (Js53 39,539 + ~138 + ~59 + ~111 ≈ 39,847; the actual final number is what the run produces).

Acceptance:

- `test262_baseline.txt` header reads `# Scope: ES2024 (skip ES2025+ features)`.
- Final passing count recorded in §7.
- 0 regressions across the full guard.

## 5. Per-Phase Guard Commands

Run after every phase boundary (P1 through P13). The guard is the contract; if any clause fails, the phase is reverted before the next phase opens.

Pre-flight (debug build) — catches the obvious cases fast:

```bash
make build && make build-test
./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/typed_array_*'
./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/buffer_*'
./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/regex_*'
```

Release js262 guard — the binding acceptance bar:

```bash
make release
ASAN_OPTIONS=detect_container_overflow=0 \
  ./test/test_js_test262_gtest.exe --batch-only --baseline-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --write-failures=temp/js54_pN_release_guard.tsv \
  --gtest_brief=1
```

(Replace `pN` with the phase number.)

The guard tsv must report:

- `Failed: 0`
- `Regressions: 0`
- `Passing >= 39539 + sum of admissions from prior phases`
- `Skipped` decreases monotonically across phases
- Total runtime within `+5%` of the P0-captured runtime (multi-run trend)

Final update (only after P13 guard is clean):

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --update-baseline \
  --write-failures=temp/js54_update_baseline.tsv \
  --gtest_brief=1
```

## 6. Risk Register

| Risk | Impact | Mitigation |
|---|---|---|
| **P0 SIGSEGV fix introduces a regression** in non-resizable TA tests | high — silent wrong reads | Full `built-ins/TypedArray*` chapter as pre-flight before release guard; require byte-identical passing on the existing baseline subset. |
| **P2 DataView OOB-check helper extracted incorrectly** mis-fires on fixed-length, non-resizable buffers | high — silent throws on healthy reads | Add an `if (!dv->length_tracking && !dv->buffer->resizable && !dv->buffer->detached) skip` short-circuit. Diff the failure pattern of every existing DataView test before/after. |
| **P3 length-tracking re-derivation hits a perf cliff** in hot iteration loops | medium — runtime drift | Bench `built_ins/TypedArray/prototype/{forEach,map,reduce}/*` before/after; if total release guard runtime drifts > +5%, stop and add an opt-out fast-path for `!ta->length_tracking`. |
| **P4 prototype-method helper extraction breaks `length.spec` byte-snapshot tests** | low — false positive | These tests compare `Function.prototype.toString` output; helper extraction doesn't change observable signature. Pre-flight the chapter. |
| **P7 admission predicate misses async-flagged Gate A tests** | low | Probe with `--batch-file` first; any test that passes the probe but is then skipped during update needs the predicate. |
| **P8 `ArrayBuffer.transfer` GC handoff leaks the source backing store** | medium — heap growth | Run release guard with `ASAN_OPTIONS=detect_leaks=1` for the P8 boundary specifically. |
| **P9 validator change rejects existing `/u` patterns** | high — silent wrong tokens | Diff parse trees of every existing `test/js/regex_*.js` test before/after with a one-shot AST dump tool; treat any diff as a regression. |
| **P10 rewriter set-op compute produces wrong class** | high — silent wrong matches | Add a focused test set in `test/js/regex_v_flag.js` covering `[A--[B-Z]]`, `[A&&B&&C]`, `[[A]--[B]]`, `[\d--[3-7]]`, `[\p{ASCII_Hex_Digit}--A]` before opening the gate; treat any pre-existing regex test diff as a regression. |
| **P11 property-table regeneration introduces a property delta** affecting existing `/u` regex tests | high — silent wrong matches | Diff the regenerated `.inc` against the current one and confirm no `/u`-only properties change. The new entries must be additive. |
| **Cumulative drift hides a regression** | medium | The +5% runtime band is enforced *per phase*, not just at the end. Each guard is the previous-guard's contract. |
| **A gate proves larger than this proposal anticipates** | medium | Mirror Js53's §10 kill-switch precedent. Stop at the gate, document in a §11 addendum to this proposal, and ship the prior phases. |
| **Gate A and Gate C diverge into a deep refactor** that breaks the baseline acceptance bar | very high — overscope | Each phase ends with a release guard. If passing count cannot be held `>= 39539 + prior phases`, the phase is reverted before continuing. This is the inviolable contract. |

## 7. Completion Criteria

| Criterion | Target | Notes |
|---|---|---|
| ES2024 skip-list entries removed | 3 of 3 remaining | `resizable-arraybuffer` (P7), `ArrayBuffer-transfer`/`arraybuffer-transfer` (P8), `regexp-v-flag` (P12) |
| Baseline scope header reads `ES2024` | yes | edited at [test/test_js_test262_gtest.cpp:2023](../../test/test_js_test262_gtest.cpp) (P13) |
| Passing count rises by ~308 | ~39,847 | Js53 39,539 + Gate A ~138 + Gate B ~59 + Gate C ~111 (approx; final number recorded after P13) |
| Failures (in baseline) | 0 | unchanged contract |
| Regressions | 0 | unchanged contract per phase |
| `t262_partial.txt` | <= 23 (no growth) | unchanged contract — Js53 carryovers only |
| Runtime within +5% of P0 | yes | enforced per phase, multi-run trend |
| New regression tests landed | 5+ | `test/js/arraybuffer_transfer.js`, `regex_v_flag.js`, `dataview_oob.js`, `typed_array_resize.js`, `bigint_ta_detach.js` minimum |
| 1 SIGSEGV fixed | yes | P0 must close the `out-of-bounds-behaves-like-detached.js` crash |

## 8. Out Of Scope

- **ES2025+ features.** `iterator-helpers`, `set-methods` (already supported), `import-attributes`, `Float16Array`, `Promise.try`, `RegExp.escape`, `regexp-modifiers`, `regexp-duplicate-named-groups`, `json-modules`, `json-parse-with-source` all remain on the skip list and are deferred to a Js55 (or later) proposal.
- **Stage 3 proposals.** `Temporal`, `ShadowRealm`, `decorators`, `explicit-resource-management`, `Atomics.pause`, `import-defer`, etc. — unchanged.
- **Cross-realm.** Js51's `cross-realm` skip exception stands.
- **`language/module-code/` runner-discovery completeness.** Js53 P2-revisit admitted 213 module-code tests via the discovery one-liner. The remaining 35 failures in that subdirectory (TLA syntax + dynamic-import Promise bug) are documented for follow-up but not owned by Js54.
- **Performance work.** No regex engine performance changes beyond what Gate C correctness requires. No buffer-copy fast paths beyond what `transfer` needs.
- **The 3 `language/module-code/top-level-await/{fulfillment-order,rejection-order,unobservable-global-async-evaluation-count-reset}.js` tests.** These are blocked on a separate Promise-capability bug in the module + TLA + dynamic-import path documented in Js53 §11 P2-revised. Js54 does not own this; it lands on its own when the underlying bug is fixed.

## 9. Open Questions

1. **P11 Unicode data version**: which Unicode CLDR / `emoji-test.txt` version do the existing `js_regex_generated_property_tables.inc` tables target? The new string-property tables must use the same version to avoid mid-table version drift.
2. **P10 \q{...} containing escape sequences**: the spec allows `\q{\u{1F1E6}\u{1F1F8}|cd}`. The escape-sequence handling inside `\q{...}` may have edge cases (especially around `|` literal vs. separator). Read the spec carefully or use a known-good reference (V8's source is BSD-licensed and well-commented).
3. **P3 length-tracking on a sub-byte-aligned offset**: if a TA is constructed with `new Int32Array(rab, 7)` (offset not aligned to element size), does the spec allow this? Confirm during P3 design before lowering, to avoid emitting code that silently mis-aligns.
4. **P8 `SharedArrayBuffer.prototype.transfer`**: spec §25.2 does not define this; only `ArrayBuffer.prototype.transfer` exists. Confirm no test262 test invokes `transfer` on a shared buffer expecting success. Spot-check by `grep -l 'transfer' ref/test262/test/built-ins/SharedArrayBuffer/`.
5. **P9–P12 admission-predicate naming**: Js53 used `is_js53_waitasync_admission_test`. The naming convention for Js54 — one predicate per gate? Or one predicate per phase? Recommend per-gate (`is_js54_resizable_arraybuffer_admission_test`, `is_js54_arraybuffer_transfer_admission_test`, `is_js54_regexp_v_flag_admission_test`) for clarity.

## 10. Hand-off Notes

This proposal is **not** a one-shot landing. Each of the 13 phases is independently shippable. The order in §4 is the dependency-minimum order:

- P0 (SIGSEGV) is unconditional first.
- P1–P7 (Gate A) and P9–P12 (Gate C) work on disjoint subsystems — they can branch in parallel without merge conflict. Recommend: one engineer per gate.
- P8 (Gate B) depends on P7 (Gate A) and must land after.
- P13 (scope flip) is unconditional last.

If a gate proves larger than this proposal anticipates, **stop** at that gate and either split it to Js55 (the Js52 P3 / Js53 P4-P6 kill-switch precedent) or extend this proposal in-place with a §11 addendum. The Js54 acceptance bar is a stable baseline at every phase boundary, not a deadline.

Total estimated engineering effort: **~3–5 sessions** of focused engine work, dominated by Gate A P4 (TypedArray prototype methods, ~30 sites) and Gate C P10–P11 (regex rewriter + Unicode data). The other phases are mostly mechanical once the foundation lands.

## 11. Why Js53's Methodology Carries Over

Js53 ran 7 numbered phases. Three (P1, P2-revised, P3 + P3-revisit) landed cleanly with admissions; three (P4, P5, P6) were kill-switched because the work outgrew the proposal's scope. The methodology was:

1. **Probe-first.** Generate a per-feature batch file. Run it. See what passes today.
2. **Classify failures by root cause.** Cluster by failure-shape, then map to fix surface.
3. **Estimate fix-surface cost honestly.** Js53 P3-revisit succeeded because the bugs were narrow (1 line each). Js53 P4/P5/P6 deferred because the work was net-new implementation, not bug fixes.
4. **Land per-phase guards.** Each phase ends with a release guard that confirms `>= prior baseline` and `0 regressions`. The guard is the contract.
5. **Admission predicate when needed.** Async-flagged tests need explicit allowlist entry; per-gate predicates keep the admission surface visible.
6. **Document deferrals precisely.** When a gate is kill-switched, the §11 entry must capture the actual fix surface for the successor proposal. Js53's §11 P4 and P6 entries are the scoping document for this proposal's Gate A and Gate C.

Js54 applies the same methodology to deeper work. The phase plan is sized for "net-new feature implementation" rather than "narrow bug fix" — phases are longer, fix surfaces are broader, but the per-phase guard contract is unchanged.

## 12. Anticipated Final Numbers

| Metric | Js53 final | Js54 P13 target |
|---|---:|---:|
| Baseline fully passing | 39,539 | ≈ 39,847 |
| ES2024 admissions from Gate A | 28 (Js53 array-grouping only) | + ~138 (Gate A complete) |
| ES2024 admissions from Gate B | 6 (Js53 promise-with-resolvers partial) | + ~59 (Gate B complete) |
| ES2024 admissions from Gate C | 0 | + ~111 (Gate C complete) |
| Scope line | ES2023 | **ES2024 (skip ES2025+ features)** |
| Failures in baseline | 0 | 0 |
| Regressions | 0 | 0 |
| Total ES2024 tests admitted across Js53+Js54 | 264 (P1: 17, P2: 214, P3: 35, P3-revisit: 19, baseline pickups: 28+6) | ≈ 572 |

Js54's primary deliverable is the ES2024 scope line. Js53 admitted the easy-to-discover ES2024 features (string well-formed, waitAsync sync paths, module-code tests, async IIFE await fix). Js54 admits the implementation-heavy features (resizable buffers + OOB-TA, ArrayBuffer.transfer, `/v` flag). When Js54 P13 lands, Lambda's JS engine is **ES2024-conformant** for the test262 baseline scope.
