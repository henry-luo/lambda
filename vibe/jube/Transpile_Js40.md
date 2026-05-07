# Transpile_Js40 - Structural test262 Enhancement Plan

Date: 2026-05-06
Last updated: 2026-05-07

This proposal analyzes the failing run of:

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe
```

The command was reproduced and captured in `temp/js40_test262_bare.log`. This
is the bare GTest mode: it first runs the batch engine, then reports each
test262 case as an individual GTest case. For day-to-day gates the standard
command remains `./test/test_js_test262_gtest.exe --batch-only`, but the bare
run is useful here because it exposes the failed case names directly.

## 1. Run Result

Infrastructure health is good; semantic compliance is the failure surface.

### 2026-05-07 Progress Update

Js40 has moved from initial failure analysis into implementation. The baseline
is now clean and has been refreshed after the latest full `--run-partial`
update run. The current working tree has additional post-refresh improvements
from the focused TypedArray prototype and `TypedArrayConstructors/internals`
work.

| Metric | Initial Js40 run | Current after fixes |
|---|---:|---:|
| Total discovered tests | 42,219 | 42,219 |
| Batched / in-scope tests | 34,145 | 34,147 |
| Fully passing | 29,185 | 29,992 |
| Failed | 4,960 | 4,155 |
| Skipped | 8,074 | 8,072 |
| Pass rate, in-scope | 85.5% | 87.8% |
| Baseline passing | 29,149 | 29,802 |
| Improvements vs baseline | 77 | 181 incorporated into refreshed baseline; 190 current local improvements |
| Regressions vs baseline | 41 | 0 |

Latest successful update command:

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --run-partial --update-baseline --js-timeout=20
```

Latest verification command:

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
```

Latest baseline update result: `29,802 / 34,167` fully passing, `4,362`
failed, `8,052` skipped, `181` improvements, `0` regressions. The update run
also cleaned `test/js262/t262_partial.txt` from 52 to 44 entries.

Latest baseline verification result after the baseline refresh and follow-up
TypedArray `reverse`/`subarray` work: `29,802 / 29,802` fully passing, `0`
failures, and `0` regressions.

Latest non-updating full batch after the DataView BigInt, TypedArray
`@@toStringTag`, TypedArray accessor/RAB, `at`, `includes`, `indexOf`, and
`lastIndexOf`, `fill`, `find`/`findIndex`, callback-method, `copyWithin`,
`subarray`, `slice`, iterator, and `TypedArrayConstructors/internals` work:

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

Result before the baseline refresh: `29,804 / 34,151` fully passing, `4,347`
failed, `8,068` skipped, `180` improvements, `0` regressions. That run used
the then-current partial-test list with 52 non-fully-passing entries, so the
denominator differs from the later baseline-refresh run.

Latest non-updating full batch after the focused `reverse` work, under the
refreshed baseline and current partial-test accounting: `29,802 / 34,148` fully
passing, `4,346` failed, `8,071` skipped, `0` improvements, and `0`
regressions.

Latest non-updating full batch after the focused `slice` ToInteger/species/RAB,
`map`/`filter` RAB callback, and `entries`/`keys`/`values` live iterator work:
`29,885 / 34,147` fully passing, `4,262` failed, `8,072` skipped, `83`
improvements, and `0` regressions. The refreshed baseline gate remains clean at
`29,802 / 29,802`, `0` failures, and `0` regressions.

Latest non-updating full batch after the `TypedArrayConstructors/internals`
canonical numeric-index plus `DefineOwnProperty` / `OwnPropertyKeys` work:
`29,992 / 34,147` fully passing, `4,155` failed, `8,072` skipped, `190`
improvements, and `0` regressions. The refreshed baseline gate remains clean at
`29,802 / 29,802`, `0` failures, and `0` regressions.

The `--js-timeout=20` option was added to the Test262 runner because one
RegExp test (`built_ins_RegExp_character_class_escape_non_whitespace_js`) could
pass individually but repeatedly hit the child `js-test-batch --timeout=10`
limit during full `--run-partial` baseline-update runs. Raising only the child
JS timeout allowed the runner's normal regression gate to complete without
weakening the zero-regression requirement.

DataView was the main semantic progress area:

| DataView focused subset | Before | After |
|---|---:|---:|
| Constructor/prototype/extensibility subset | 7 / 21, then 11 / 21 after ToIndex fixes | 21 / 21 |
| Current DataView failure list | 29 / 187 before accessor/constructor work | 187 / 187 |

TypedArray progress since the baseline update:

| TypedArray focused subset | Before | After |
|---|---:|---:|
| `%TypedArray%.prototype[@@toStringTag]` | failing descriptor/receiver/name cases | 18 / 18 |
| `%TypedArray%.prototype.{buffer,byteLength,byteOffset,length}` plus RAB edge cases | 37 / 50 before RAB/accessor work | 50 / 50 |
| `%TypedArray%.prototype.at` | 9 / 15 before method/internal-slot work | 15 / 15 |
| `%TypedArray%.prototype.includes` | 24 / 45 before method/RAB/Buffer identity work | 45 / 45 |
| `%TypedArray%.prototype.indexOf` | 31 / 43 before method/RAB work | 43 / 43 |
| `%TypedArray%.prototype.lastIndexOf` | 36 / 42 before method/RAB/fill work | 42 / 42 |
| `%TypedArray%.prototype.fill` | 19 / 51 before coercion/RAB/integer conversion work | 51 / 51 |
| `%TypedArray%.prototype.{find,findIndex}` | 66 / 76 before RAB callback-loop work | 76 / 76 |
| `%TypedArray%.prototype.{every,some,findLast,findLastIndex}` | 144 / 164 before RAB callback-loop work | 164 / 164 |
| `%TypedArray%.prototype.copyWithin` | 37 / 64 before coercion/RAB/BigInt work | 64 / 64 |
| `%TypedArray%.prototype.reverse` | 18 / 21 before RAB/current-length work | 21 / 21 |
| `%TypedArray%.prototype.subarray` | 29 / 67 before RAB/current-view/species work | 67 / 67 |
| `%TypedArray%.prototype.slice` | 70 / 93 before ToInteger/species/RAB work | 92 / 93; remaining subclass species resize path |
| `%TypedArray%.prototype.map` / `filter` | 156 / 168 before RAB callback-value work | 166 / 168; remaining `map` subclass species resize paths |
| `%TypedArray%.prototype.entries` / `keys` / `values` | 43 / 59 before live iterator/RAB work | 59 / 59 |
| `TypedArrayConstructors/internals` canonical `Get` / `Set` / `HasProperty` / `Delete` | 108 / 240 full internals before canonical-key work | 170 / 240 after canonical-key work |
| `TypedArrayConstructors/internals` `DefineOwnProperty` / `OwnPropertyKeys` | 58 / 64 after first descriptor/key hook pass | 62 / 64; remaining cross-realm detached-buffer throw cases |
| Full `TypedArrayConstructors/internals` | 170 / 240 before descriptor/key work | 220 / 240 |

DataView BigInt getter/setter behavior has also landed. The focused list now
passes all `getBigInt64`, `getBigUint64`, `setBigInt64`, and `setBigUint64`
coverage that was previously blocking this part of J40-3.

Implemented since the initial proposal:

- Constructor/prototype snapshot and lazy TypedArray cache reset fixes brought
  the baseline regressions to zero.
- The Test262 runner race around `g_preamble_includes` was fixed with a mutex,
  making bare diagnostic mode reliable again.
- DataView constructor `ToIndex`, explicit `null` length, detached-buffer, and
  Symbol conversion behavior were aligned with the spec.
- DataView instances now preserve `.buffer` identity, link to
  `DataView.prototype`, expose `sample.constructor === DataView`, and remain
  extensible for ordinary own properties.
- DataView prototype accessors for `buffer`, `byteLength`, and `byteOffset` now
  have proper native accessor descriptors, receiver validation, getter metadata,
  and detached-buffer checks.
- DataView numeric getters/setters now share better offset/value coercion,
  `littleEndian` truthiness, Symbol TypeError behavior, setter detached-check
  ordering, and ordinary method fallback (`hasOwnProperty`, etc.).
- DataView BigInt getters/setters now perform 64-bit endian-aware buffer
  reads/writes, `ToBigInt` value conversion, `BigInt.asIntN(64)` /
  `BigInt.asUintN(64)` wrapping, and unsigned 64-bit BigInt materialization.
- `%TypedArray%.prototype[@@toStringTag]` is now installed as a native accessor;
  direct typed-array property access preserves the original receiver and returns
  the correct concrete typed-array name.
- `%TypedArray%.prototype` accessors for `buffer`, `byteLength`, `byteOffset`,
  and `length` now pass the focused 50-test list, including minimal resizable
  ArrayBuffer `maxByteLength`/`resize` support, length-tracking views,
  fixed-view out-of-bounds handling, TypedArray subclass construction over
  inherited typed-array constructors, and native Test262 `compareArray`
  support for ArrayBuffer identity checks.
- `%TypedArray%.prototype.at` now validates detached/out-of-bounds views,
  preserves pre-index-conversion length semantics for length-tracking RAB views,
  propagates Symbol/ToNumber abrupt completions, returns JS `undefined` for
  post-conversion out-of-range reads, recognizes BigInt typed-array
  `instanceof`, and snapshots regular-array constructor inputs before value
  conversion. The focused list improved from `9 / 15` to `15 / 15` after the
  float-widening pre-scan learned to treat `NaN` and `Infinity` assignments as
  float evidence for ordinary function locals.
- `%TypedArray%.prototype.includes` now captures initial TypedArray length
  before `fromIndex` coercion, uses `ToIntegerOrInfinity`, handles
  shrink/grow RAB reads as JS `undefined` when appropriate, compares with
  SameValueZero including `NaN`, and avoids routing ordinary `Uint8Array`
  instances through Node Buffer methods by marking real Buffer instances
  explicitly.
- `%TypedArray%.prototype.indexOf` and `lastIndexOf` now share the same
  receiver validation and initial-length-before-`fromIndex` discipline, while
  preserving strict-equality search semantics. The `lastIndexOf` slice also
  fixed length-tracking RAB `fill` setup by making `js_typed_array_fill` use
  dynamic view length and backing data after resize.
- `%TypedArray%.prototype.fill` now computes start/end from initial TypedArray
  length, propagates `ToIntegerOrInfinity` abrupt completions, converts the
  fill value exactly once, supports BigInt typed-array value conversion, and
  revalidates RAB state after coercion. Integer typed-array stores now use a
  shared JS ToInt/ToUint modulo conversion helper instead of C casts for
  `Infinity` and out-of-range doubles.
- `%TypedArray%.prototype.find` and `findIndex` now validate RAB-backed
  receivers before callback iteration, capture the current internal length
  rather than stale construction length, and pass `undefined` for elements that
  become out of bounds after a mid-iteration resize.
- `%TypedArray%.prototype.every`, `some`, `findLast`, and `findLastIndex` now
  follow the same RAB callback-loop discipline: entry validation, dynamic
  current-length capture, and `undefined` callback values for elements made
  out of bounds by resize during iteration.
- `%TypedArray%.prototype.copyWithin` now validates RAB-backed receivers before
  coercion, coerces `target`/`start`/`end` through `js_to_number` with Symbol
  abrupt-completion handling, revalidates after user coercion, computes the
  copy window from the pre-coercion length plus current RAB bounds, uses current
  backing data, and preserves BigInt typed-array 8-byte element copies.
- `%TypedArray%.prototype.reverse` now validates detached/out-of-bounds
  RAB-backed receivers before swapping and uses the current internal length
  instead of the construction-time cached length.
- `%TypedArray%.prototype.subarray` now captures the source length before
  argument coercion, creates shared-buffer results from current RAB state,
  preserves length-tracking results when `end` is omitted, throws RangeError
  for result views that would start or extend beyond the current buffer, and
  routes buffer/byteOffset/length creation through `TypedArraySpeciesCreate`.
  Custom `@@species` getters, custom constructors, custom returned typed
  arrays, inherited `constructor` accessors, non-constructable prototype
  methods, and detached-buffer argument/species ordering now pass. The MIR
  member-read path now reloads `.constructor` getter side effects so inherited
  accessor tests observe closure mutations without disturbing unrelated
  computed member access, and explicit accessor `return null` remains distinct
  from implicit `undefined`.
- `%TypedArray%.prototype.slice` now uses `ToIntegerOrInfinity` for `start` and
  `end`, propagates Symbol/number conversion abrupt completions, uses current
  RAB length where the spec observes post-coercion growth, preserves the
  original result count across post-coercion shrink, validates species result
  length against current typed-array length, and copies from current backing
  storage with zero-fill for bytes/elements that disappear after RAB resize.
  Same-buffer species copies now use forward byte-copy semantics instead of
  `memcpy`, matching the observable overlapping-copy behavior. The remaining
  focused failure is `speciesctor-resize`, which is rooted in loop-scoped
  dynamic typed-array subclass construction invoking an explicit subclass
  constructor.
- `%TypedArray%.prototype.map` and `filter` now validate the receiver's current
  RAB bounds before callback/species work, capture current length for
  length-tracking views, and pass JS `undefined` rather than internal `null` to
  callbacks when a shrink during iteration makes an element unavailable. The
  focused `filter` subset is complete, and the remaining focused `map` failures
  are the same loop-scoped subclass species resize class/super issue that still
  blocks the final `slice` case.
- `TypedArrayConstructors/internals` now routes canonical numeric-index strings
  through integer-indexed exotic `Get`, `Set`, `HasProperty`, `Delete`,
  `GetOwnProperty`, and `DefineOwnProperty` behavior while preserving ordinary
  property semantics for non-canonical keys. Typed-array own keys now synthesize
  current integer indices before ordinary string/symbol properties, numeric
  descriptors are synthesized from current element values, invalid and detached
  element reads expose JS `undefined` instead of the internal null sentinel, and
  `indexOf` / `lastIndexOf` stop searching when `fromIndex` conversion detaches
  the backing buffer.
- String receiver checks, RegExp string-iterator coercions, Array reduce
  prototype-key refresh, and for-of destructuring assignment writeback were
  also fixed while recovering the baseline.

The initial snapshot below is kept as historical context for why the phases were
chosen.

| Metric | Value |
|---|---:|
| Total discovered tests | 42,219 |
| Batched / in-scope tests | 34,145 |
| Fully passing | 29,185 |
| Failed | 4,960 |
| Skipped | 8,074 |
| Pass rate, in-scope | 85.5% |
| Baseline passing | 29,149 |
| Improvements vs baseline | 77 |
| Regressions vs baseline | 41 |
| Missing / batch-lost / crash-exit | 0 / 0 / 0 |
| Slow in fresh run | 5 slow at Phase 3, 9 slow observed before partial rewrite |
| Known partial entries loaded | 44 |

The important initial conclusion still holds: the JS engine is not primarily
failing because of crashes or lost tests. The remaining failures are spec
machinery gaps. The original 41 baseline regressions have now been recovered,
so Js40 can continue as a structural correctness program rather than a crasher
hunt.

## 2. Failure Landscape

Failed GTest names were extracted from the bare log and grouped by test262 path
encoded in the generated GTest names.

| Rank | Cluster | Failing | Primary missing machinery |
|---:|---|---:|---|
| 1 | `built-ins/RegExp` | 1,046 | ES RegExp semantics over RE2, named/lookbehind/sticky/unicode, matchAll iterator edge cases |
| 2 | `language/statements/class` | 714 | Class element initialization, private/static fields, super/new.target/receiver semantics |
| 3 | `built-ins/TypedArray` | 702 | Internal slots, detach checks, species, method descriptor behavior |
| 4 | `built-ins/Array` | 690 | Array exotic length, sparse/hole semantics, callback iterator close, property descriptors |
| 5 | `built-ins/TypedArrayConstructors` | 572 | Constructor/prototype descriptors, canonical numeric index, BigInt typed arrays |
| 6 | `language/expressions/class` | 492 | Class expressions, computed names, private fields, destructuring params |
| 7 | `built-ins/Object` | 464 | `defineProperty/defineProperties`, descriptor validation and application order |
| 8 | `built-ins/DataView` | 374 | Detached buffer checks, byte offset/length validation, BigInt accessors |
| 9 | `built-ins/Atomics` | 316 | Atomics/SAB in-scope ES2020 behavior mostly absent |
| 10 | `built-ins/Promise` | 312 | Microtask ordering, combinators, thenable assimilation/spec jobs |
| 11 | `language/statements/for` | 258 | Iterator close, destructuring in loop heads, yield/TDZ edge cases |
| 12 | `annexB/language/eval` | 242 | Annex B eval/global function declaration hoisting |
| 13 | `built-ins/String` | 226 | Generic brand checks, regex protocol hooks, primitive wrapper descriptors |
| 14 | `language/expressions/compound-assignment` | 202 | Reference model and ToPrimitive/ToNumeric ordering |
| 15 | `language/expressions/assignment` | 188 | PutValue semantics, destructuring target evaluation order, strict failures |
| 16 | `language/statements/with` | 158 | Object environment record and `Symbol.unscopables` |
| 17 | `annexB/language/global` | 152 | Sloppy global function declaration instantiation |
| 18 | `built-ins/Date` | 144 | Date coercion, prototype methods, descriptor details |
| 19 | `language/expressions/object` | 142 | Spread order, symbol keys, computed property key coercion |
| 20 | `built-ins/Set` | 134 | Iterator protocol, species, mutation during iteration |

One-level-deeper clusters show the same shape:

| Cluster | Failing | Note |
|---|---:|---|
| `built-ins/RegExp/property-*` | 856 | Unicode property escape expansion and memory growth are the biggest RegExp surface |
| `built-ins/TypedArray/prototype` | 696 | Almost all TypedArray failures are method/prototype internal-slot semantics |
| `built-ins/Array/prototype` | 604 | Array exotic methods, holes, non-writable length, callback semantics |
| `language/statements/class/elements` | 400 | Class field/static/private initialization order |
| `built-ins/DataView/prototype` | 328 | DataView method checks and numeric conversion |
| `language/expressions/class/elements` | 268 | Mirrors class statement failures |
| `built-ins/TypedArrayConstructors/internals` | 268 | Canonical numeric index and detached/internal slot behavior |
| `annexB/language/eval/code` | 242 | Eval-scope Annex B behavior |
| `language/statements/for/of` | 220 | Iterator close + destructuring |
| `language/expressions/compound/assignment` | 202 | Reference and coercion ordering |

## 3. Regressions First

Status: complete as of 2026-05-07. The latest baseline refresh now records
`29,802` passing tests; the follow-up `--baseline-only --js-timeout=20`
verification reports `0` failures and `0` regressions, with two slow/batch-kill
cases recovered in Phase 4 retry.

The run has 77 improvements, but 41 baseline regressions. Js40 should begin by
making the baseline clean again before attempting broad gains.

Regression themes:

| Theme | Representative regressions | Likely owner |
|---|---|---|
| Descriptor validation / accessor conversion | `Object.defineProperty_15_2_3_6_3_157` through `_162`, String/TypedArray failures with `Cannot redefine property: getter` | `js_props.cpp`, `js_property_attrs.cpp`, `js_globals.cpp` descriptor pipeline |
| Function identity / aliasing | `trimLeft` reference `trimStart`, `trimRight` reference `trimEnd`, several `Function.prototype.bind` property tests | builtin function object metadata and alias installation |
| Array exotic length | `Array.prototype.unshift` non-writable length, `Array.prototype.some` callback result | array exotic Set/DefineOwnProperty path |
| RegExp protocol | `RegExpStringIteratorPrototype.next` error paths, RegExp flag accessor on prototype | `js_regex_wrapper.cpp`, RegExp builtins in `js_runtime.cpp` |
| TypedArray internal slots | constructors prototype tests, detached-buffer HasProperty, prototype methods reading `length` | `js_typed_array.cpp`, typed-array branches in `js_runtime.cpp` |
| WeakMap/WeakSet | iterable and prototype set/delete regressions | weak collection constructor/prototype install and callable checks |
| For-of destructuring + yield | seven `language_statements_for_of_dstr_*_yield_expr` regressions | loop-head destructuring emitter and yield identifier treatment |

The descriptor/accessor and typed-array regressions are especially important
because they sit inside high-volume failure clusters. Fixing them should not be
handled as isolated tests; they should be used as acceptance cases for the
structural phases below.

## 4. Why Js40 Should Be Structural

The remaining failures repeatedly point to missing ECMAScript abstract
operations:

- `ValidateAndApplyPropertyDescriptor`, `OrdinaryDefineOwnProperty`, and array
  exotic `[[DefineOwnProperty]]` behavior.
- `GetIterator`, `IteratorStep`, `IteratorClose`, `IterableToList`, and
  destructuring target binding/assignment order.
- TypedArray/DataView internal slot validation and `IsDetachedBuffer`.
- Class definition evaluation: element definition, private brand, super
  receiver, class field initialization, `new.target` capture.
- Promise job queues and microtask draining.
- RegExp protocol operations (`RegExpExec`, `AdvanceStringIndex`, `lastIndex`,
  `@@matchAll`, named groups) layered over the RE2 wrapper.
- Object environment records for `with` and Annex B global/eval declaration
  instantiation.

The engine already has the right direction from Js38/J39: `js_props.{h,cpp}`
for property kernels, `js_coerce.{h,cpp}` for coercion, `js_class.h` for class
identity, and batch-test reset machinery. Js40 should continue that pattern:
one spec kernel, one narrow GTest suite, then route all call sites.

## 5. Proposal Phases

Each phase must be independently shippable:

- `make build`
- relevant focused GTest suite
- `ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only`
- full `--batch-only` when the phase changes broad semantics
- `regressions=0` before any baseline update

### Phase J40-0 - Restore Baseline Health

Status: complete. The 41 initial baseline regressions were reduced to zero, and
the baseline has now been refreshed to 29,802 passing tests after a full
`--run-partial --update-baseline` run.

Goal: reduce the 41 regressions to zero without deleting baseline entries.

Actions:

- Reproduce each regression as a small focused test in the closest suite:
  `Props.*`, a new `TypedArray.*`, `Regex.*`, `Collections.*`, or `Loop.*`.
- Fix descriptor/accessor regressions first, because they explain many
  secondary failures: invalid data/accessor descriptor transitions, getter
  redefinition, non-writable length, and alias function descriptor equality.
- Re-run bare command only after `--batch-only --baseline-only` is clean; the
  bare command is expensive and noisy.

Exit criteria:

- `Regressions: 0` against `test/js262/test262_baseline.txt`.
- No increase in `test/js262/t262_partial.txt` except known slow timing noise.

### Phase J40-1 - Descriptor and Exotic Object Kernel Completion

Goal: make property descriptors the single source of truth for ordinary objects,
arrays, functions, typed arrays, and primitive wrappers.

Scope:

- Finish routing through `js_props.{h,cpp}` and `js_property_attrs.{h,cpp}` for:
  `Object.defineProperty`, `Object.defineProperties`, function virtual
  properties, primitive wrapper properties, and array companion maps.
- Add an array-exotic define path for `length` and indexed properties:
  non-writable length truncation, blocked extension, and partial rollback.
- Ensure accessor-to-data and data-to-accessor transitions follow
  `ValidateAndApplyPropertyDescriptor` exactly, including validation order.
- Normalize builtin function aliases so alias identity and `toString` source
  text compare correctly (`trimLeft`/`trimStart`, `trimRight`/`trimEnd`).

Focused tests:

- `Props.DefinePropertyValidationOrder`
- `Props.AccessorDataTransition_NoGetterRedefineRegression`
- `ArrayExotic.NonWritableLength_SetAndUnshift`
- `FunctionBuiltin.AliasIdentityAndSourceText`

Expected impact:

- Clears descriptor regressions and improves `built-ins/Object`,
  `built-ins/Array`, `built-ins/String`, `built-ins/Function`, and parts of
  typed-array prototype behavior.

### Phase J40-2 - RegExp Protocol Layer over RE2

Goal: reduce the largest cluster (`built-ins/RegExp`, 1,046 failures) while
keeping RE2 as the safe matching engine.

Scope:

- Move all RegExp protocol behavior behind a small kernel set:
  `js_regexp_exec`, `js_regexp_advance_string_index`,
  `js_regexp_set_last_index`, and `js_regexp_string_iterator_next`.
- Complete named capture group result objects and replacement `$<name>`.
- Tighten sticky `y`, global `g`, unicode `u`, and `lastIndex` behavior.
- Extend the existing Unicode property alias/expansion table and address the
  memory-growth hotspots from the run (`Script_Extensions`, `Script`, generated
  property escape files). Expansion should be cached by canonical property key
  rather than rebuilt per test/pattern.
- Implement fixed-width lookbehind by wrapper metadata where possible; reject or
  clearly fail unsupported variable-width forms.
- Ensure RegExp flag accessors work generically on RegExp instances and reject
  invalid receivers correctly.

Focused tests:

- `Regex.LastIndexStrictStickyUnicode`
- `Regex.NamedGroupsExecReplace`
- `Regex.MatchAllIteratorAbruptPaths`
- `Regex.PropertyEscapeCacheMemory`

Expected impact:

- Largest single failure reduction; also reduces slow/partial pressure and peak
  RSS from generated Unicode property tests.

### Phase J40-3 - TypedArray, DataView, and ArrayBuffer Internal Slots

Status: in progress, with the focused DataView portion complete. The focused
DataView constructor/prototype/extensibility subset is now `21 / 21`, and the
current DataView failure list improved to `187 / 187`. The focused
`%TypedArray%.prototype[@@toStringTag]` subset is also `18 / 18`. The focused
`%TypedArray%.prototype` accessor/RAB subset is now `50 / 50`; the focused
`%TypedArray%.prototype.includes` subset is now `45 / 45`; and the focused
`indexOf`/`lastIndexOf` subsets are now `43 / 43` and `42 / 42`; the focused
`fill` subset is now `51 / 51`; the focused `find`/`findIndex` subset is now
`76 / 76`; and the focused `every`/`some`/`findLast`/`findLastIndex` callback
subset is now `164 / 164`; the focused `copyWithin` subset is now `64 / 64`;
the focused `reverse` subset is now `21 / 21`; and the focused `subarray`
subset has improved from `29 / 67` to `67 / 67`; the focused `slice` subset has
improved from `70 / 93` to `92 / 93`; and the focused `map`/`filter` subset has
improved from `156 / 168` to `166 / 168`; and the focused `entries`/`keys`/
`values` subset is now `59 / 59`. Broader TypedArray method, subclass species
construction, and canonical numeric-index work remains.

Goal: make `TypedArray`/`DataView` behavior spec-driven instead of method-local.

Scope:

- Introduce internal-slot helpers:
  `js_is_typed_array`, `js_validate_typed_array`, `js_validate_dataview`,
  `js_is_detached_buffer`, `js_get_viewed_array_buffer`, and
  `js_typed_array_length_internal`.
- Completed for DataView incrementally: `js_get_dataview_ptr`, stored
  `buffer_item`, prototype linking, class stamping, accessor receiver checks,
  shared `ToIndex`/numeric conversion helpers, and BigInt64/BigUint64
  endian-aware read/write paths.
- Completed for TypedArray incrementally: native `@@toStringTag` accessor
  descriptor/receiver behavior and direct typed-array `[[TypedArrayName]]`
  lookup; `buffer`/`byteLength`/`byteOffset`/`length` accessor receiver and
  descriptor behavior; minimal resizable ArrayBuffer construction and resize;
  length-tracking and fixed-view resize accounting; and TypedArray subclass
  construction over inherited concrete typed-array constructors.
- Every TypedArray/DataView method must call the same validation helpers before
  reading user-visible `length`, `byteLength`, or `byteOffset` properties.
- Centralize canonical numeric index parsing (`-0`, BigInt keys, floats,
  non-canonical strings) for get/set/has/delete.
- Implement `@@species` lookup for `slice`, `subarray`, `map`, `filter`, and
  `ArrayBuffer.prototype.slice`.
- Add BigInt typed array and DataView conversion helpers, even if the first
  increment only validates and skips unsupported math paths.

Focused tests:

- `TypedArray.InternalLengthIgnoresLengthProperty`
- `TypedArray.DetachedBufferAllEntrypoints`
- `TypedArray.CanonicalNumericIndexKeys`
- `DataView.DetachedAndOffsetValidation`

Expected impact:

- Targets roughly 1,650 failures across `TypedArray`,
  `TypedArrayConstructors`, `DataView`, and `ArrayBuffer`, and fixes several
  current regressions. The DataView portion has converted all 158 remaining
  cases in the 187-test focused list to passing status; the latest TypedArray
  accessor/RAB slice converted the focused accessor list from `37 / 50` to
  `50 / 50`; the later `at`, `includes`, `indexOf`, `lastIndexOf`, `fill`,
  `find`/`findIndex`, `every`/`some`/`findLast`/`findLastIndex`, `copyWithin`,
  `reverse`, `subarray`, `slice`, `map`/`filter`, `entries`/`keys`/`values`,
  `TypedArrayConstructors/internals`, Buffer identity, dynamic RAB backing
  updates, integer conversion, and float-widening work leave the latest
  non-updating full-batch improvement count at `190` after the baseline was
  refreshed to incorporate the earlier improvements. The callback-method and
  `reverse` slices improved their focused manifests but did not change the
  full-run improvement count because those tests are outside the current
  partial-list accounting; the earlier `copyWithin` slice moved the pre-refresh
  full-run improvement count from `158` to `180`, and the completed `subarray`
  plus partial `slice` species/RAB work started the post-refresh full-run
  improvements.
  The focused `map`/`filter` RAB callback-value slice improved its manifest but
  did not change the full-run improvement count under the current partial-list
  accounting; the focused live iterator slice completed `entries`/`keys`/`values`
  and moved the post-refresh full-run improvement count from `82` to `83`. The
  canonical numeric-index internals slice then moved it to `142`, and the
  `DefineOwnProperty` / `OwnPropertyKeys` internals slice moved it to `190`.

### Phase J40-4 - Iterator and Destructuring Runtime Kernels

Goal: remove duplicated iterator/destructuring behavior from expression,
assignment, call, array spread, and loop-head emitters.

Scope:

- Add runtime helpers for `GetIterator`, `IteratorStepValue`, `IteratorClose`,
  and `IterableToList` with abrupt-completion propagation.
- Route array spread, call spread, assignment destructuring, declaration
  destructuring, and `for-of` destructuring through these helpers.
- Implement target evaluation order for destructuring assignment exactly: source
  iterator acquisition, property-key coercion, target reference formation, then
  PutValue.
- Ensure elisions, default initializers, rest elements, and iterator `return`
  follow spec on normal and abrupt paths.

Focused tests:

- `Iterator.CloseOnDefaultInitializerThrow`
- `Iterator.CloseReturnNonObject`
- `Destructure.TargetEvaluationOrder`
- `ForOf.DestructureYieldIdentifierRegression`

Expected impact:

- Targets `language/statements/for` (258), destructuring-heavy assignment
  failures, array/call spread order failures, and Set/Map iterator collateral.

### Phase J40-5 - Class Evaluation and Lexical Receiver Model

Goal: complete class element semantics without further ad hoc codegen branches.

Scope:

- Represent class evaluation state explicitly in `transpile_js_mir.cpp`:
  current class, home object, super base, derived-constructor state,
  `new.target`, and lexical-arrow captures.
- Route class fields, methods, accessors, private fields, and static blocks
  through one class-element evaluation path.
- Add private brand checks and private static lookup as runtime helpers, not
  string-prefix property hacks.
- Fix `super` property get/set/call with the correct receiver and `this` TDZ in
  derived constructors.
- Reuse normal function parameter/destructuring binders for class methods and
  constructors.

Focused tests:

- `Class.PrivateBrandAndStatic`
- `Class.SuperReceiverGetSet`
- `Class.FieldInitializerThisTDZ`
- `Class.MethodDestructuringParams`
- `Arrow.LexicalSuperArgumentsNewTarget`

Expected impact:

- Targets 1,200+ class failures plus arrow lexical-super/new.target failures.

### Phase J40-6 - Scope, Annex B, Eval, and With Environment Records

Goal: model JavaScript environment records explicitly enough for the remaining
sloppy-mode and `with`/eval failures.

Scope:

- Introduce a small environment-record abstraction for runtime lookups that can
  represent declarative, object, global, and function environments.
- Use it for `with` object environments and `Symbol.unscopables` filtering.
- Implement Annex B B.3.3 function declaration instantiation for global/eval
  code, including suppression on lexical/catch collisions.
- Tighten direct vs indirect eval: strictness inheritance, var export, lexical
  collision early errors, and completion value handling.
- Keep fast direct MIR locals for ordinary code; only route through environment
  records when dynamic scope is present.

Focused tests:

- `With.UnscopablesAndNestedFunction`
- `AnnexB.GlobalFunctionDeclarationInstantiation`
- `Eval.StrictnessAndVarScope`
- `Scope.CatchVarAnnexBInteractions`

Expected impact:

- Targets `language/statements/with` (158), `annexB/language/eval` (242),
  `annexB/language/global` (152), and many scope/arrow/assignment edge cases.

### Phase J40-7 - Promise Jobs and Async Semantics

Goal: move async support from mostly synchronous compatibility to spec-visible
job queues.

Scope:

- Make Promise reactions enqueue microtasks and drain them FIFO at defined
  points in `js_event_loop.cpp` and batch execution.
- Implement thenable assimilation and Promise combinator shared kernels for
  `all`, `allSettled`, `any`, and `race`.
- Enforce `await` syntax errors in non-async contexts via early-error pass,
  and use PromiseResolve semantics for async functions.
- Prepare, but do not fully require, async generator protocol if it would
  balloon the phase.

Focused tests:

- `Promise.MicrotaskDrainOrder`
- `Promise.ThenableAssimilation`
- `Promise.CombinatorAbruptPaths`
- `Async.AwaitEarlyErrors`

Expected impact:

- Targets `built-ins/Promise` (312) and async collateral still entering the
  ES2020 in-scope set.

### Phase J40-8 - Atomics / SharedArrayBuffer Decision

Goal: decide whether Atomics/SAB are in-scope for this engine target or should
be explicitly feature-gated.

The current run has 316 `built-ins/Atomics` failures and 76
`SharedArrayBuffer` failures. ES2020 includes these, but implementing them
correctly requires shared memory semantics and wait/notify behavior that may not
fit LambdaJS's current single-threaded embedding.

Options:

- Implement the single-thread-compatible subset: integer typed-array validation,
  `Atomics.load/store/add/sub/and/or/xor/exchange/compareExchange`, and
  deterministic `notify` behavior. Keep `wait` limited or skipped when true
  blocking is unavailable.
- Or document Atomics/SAB as intentionally unsupported and add feature gating in
  the runner for transparent compliance accounting.

This decision should be made explicitly; leaving the tests as ordinary failures
obscures the pass-rate signal for features the engine intends to support.

## 6. Test Runner and Reporting Enhancements

The test runner itself is healthy, but Js40 would benefit from better analysis
artifacts:

- Add a `--report-failures=temp/js40_failures.tsv` option that writes:
  `name`, `path`, `category`, `result`, first error line, runtime, RSS delta,
  baseline status.
- Add a category summary directly to the runner output. The manual aggregation
  in this proposal should become automatic.
- Normalize the bare command guidance: docs should say bare mode is diagnostic;
  `--batch-only` is the compliance gate.
- Completed: add `--js-timeout=N` to pass a larger timeout through to child
  `lambda.exe js-test-batch` runs. This is useful for full `--run-partial`
  baseline updates when a known-slow passing test is near the default 10s child
  timeout.
- Deduplicate and sort `test/js262/t262_partial.txt` when rewriting. The current
  file contains carried-forward duplicate-looking blocks after partial updates.
- Track memory regressions with a separate threshold. This run peaked at about
  1.1 GB RSS, with RegExp property escape tests causing the largest single-test
  growth.

## 7. Priority Order

Recommended order:

1. **J40-0 Regression recovery** - complete; baseline is clean and updated.
2. **J40-1 Descriptor/exotic kernel** - fixes regressions and unlocks broad
   Object/Array/String/Function behavior.
3. **J40-3 TypedArray/DataView internal slots** - in progress; the focused
    DataView list is complete, while broader TypedArray work remains high
    priority.
4. **J40-2 RegExp protocol** - largest cluster and memory/slow pressure.
5. **J40-4 Iterator/destructuring kernels** - high leverage across language and
   built-ins.
6. **J40-5 Class evaluation** - large but compiler-heavy; do after kernels are
   less shaky.
7. **J40-6 Scope/AnnexB/with** - important for spec closure, lower ROI than
   descriptors/typed arrays/regex.
8. **J40-7 Promise jobs** - should be tackled when event loop semantics are the
   chosen focus.
9. **J40-8 Atomics/SAB decision** - implement or explicitly gate.

## 8. Expected Outcome

Conservative target for Js40 after phases 0-4:

| Area | Approx. current failures | Conservative reduction target |
|---|---:|---:|
| Regressions | 41 | 41 |
| Descriptor/Object/Array/String/Function | ~1,400 overlapping | 400-700 |
| TypedArray/DataView/ArrayBuffer | ~1,650 | 600-900 |
| RegExp | 1,046 | 400-700 |
| Iterator/destructuring/for-of | ~600 overlapping | 250-450 |

That would move the engine from 29,185 fully passing toward roughly
30,800-32,000 fully passing, depending on overlap, while keeping the baseline
clean. Phases 5-7 can then push class and async compliance without fighting the
lower-level property, iterator, and internal-slot gaps.

Progress against that target as of 2026-05-07: the engine has moved from
29,185 to 29,802 fully passing tests in the full `--run-partial` accounting,
with the refreshed baseline clean at 29,802 and `0` regressions. The most
recent baseline-refresh run was `29,802 / 34,167` fully passing with `181`
improvements incorporated into the baseline. The current non-updating working
tree result is `29,992 / 34,147` fully passing with `190` post-refresh
improvements and `0` regressions.
The next highest-leverage continuation is to broaden the same internal-slot
discipline across TypedArray methods, species paths, and canonical numeric-index
behavior.

Additional verification after the DataView BigInt work:

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_dataview_all.txt --js-timeout=20
```

Result: `187 / 187` passing in the focused DataView list, followed by a clean
baseline verification: `29,624 / 29,624`, `0` failures, `0` regressions.

Additional verification after the TypedArray `@@toStringTag` work:

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarray_tostringtag.txt --js-timeout=20
```

Result: `18 / 18` passing in the focused TypedArray `@@toStringTag` list, with
the baseline gate still clean at `29,624 / 29,624`, `0` regressions.

Additional verification after the TypedArray accessor/RAB work:

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarray_accessors.txt --js-timeout=20
```

Result: `50 / 50` passing in the focused TypedArray accessor list.

Follow-up baseline and full-batch verification:

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

Results: baseline gate `29,624 / 29,624`, `0` failures, `0` regressions; full
non-updating batch `29,686 / 34,153`, `4,467` failed, `8,066` skipped, `62`
improvements, `0` regressions.

Additional verification after the `%TypedArray%.prototype.at` work:

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarray_at.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_ctor_regression.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

Results: focused `at` list `15 / 15`, constructor snapshot regression `1 / 1`,
baseline gate `29,624 / 29,624`, `0` failures, `0` regressions. The full
non-updating batch is now `29,692 / 34,153`, `4,461` failed, `8,066` skipped,
`68` improvements, `0` regressions.

Additional verification after the `%TypedArray%.prototype.includes` work:

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarray_includes.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

Results: focused `includes` list `45 / 45`, baseline gate `29,624 / 29,624`,
`0` failures, `0` regressions. The full non-updating batch is now
`29,739 / 34,153`, `4,414` failed, `8,066` skipped, `115` improvements,
`0` regressions.

Additional verification after the `%TypedArray%.prototype.indexOf` and
`lastIndexOf` work:

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarray_indexof.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarray_lastindexof.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

Results: focused `indexOf` list `43 / 43`, focused `lastIndexOf` list
`42 / 42`, baseline gate `29,624 / 29,624`, `0` failures, `0` regressions.
The full non-updating batch is now `29,749 / 34,153`, `4,404` failed,
`8,066` skipped, `125` improvements, `0` regressions.

Additional verification after the `%TypedArray%.prototype.fill` work:

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarray_fill.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

Results: focused `fill` list `51 / 51`, baseline gate `29,624 / 29,624`,
`0` failures, `0` regressions. The full non-updating batch is now
`29,783 / 34,153`, `4,370` failed, `8,066` skipped, `159` improvements,
`0` regressions.

Additional verification after the `%TypedArray%.prototype.find` and
`findIndex` work:

```bash
make -j1 lambda
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarray_find_findindex.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

Results: focused `find`/`findIndex` list `76 / 76`, baseline gate
`29,624 / 29,624`, `0` failures, `0` regressions. The full non-updating batch
under the current 52-entry partial-test list is `29,782 / 34,151`, `4,369`
failed, `8,068` skipped, `158` improvements, `0` regressions.

Additional verification after the `%TypedArray%.prototype.every`, `some`,
`findLast`, and `findLastIndex` work:

```bash
make -j1 lambda
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarray_callback_methods.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

Results: focused callback-method list `164 / 164`, up from `144 / 164`;
baseline gate `29,624 / 29,624`, `0` failures, `0` regressions. The full
non-updating batch remains `29,782 / 34,151`, `4,369` failed, `8,068` skipped,
`158` improvements, `0` regressions, under the current 52-entry partial-test
list.

Additional verification after the `%TypedArray%.prototype.copyWithin` work:

```bash
make -j1 lambda
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarray_copywithin.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

Results: focused `copyWithin` list `64 / 64`, up from `37 / 64`; baseline gate
`29,624 / 29,624`, `0` failures, `0` regressions. The full non-updating batch
is now `29,804 / 34,151`, `4,347` failed, `8,068` skipped, `180`
improvements, `0` regressions, under the current 52-entry partial-test list.

Additional verification after the `%TypedArray%.prototype.reverse` work:

```bash
make -j1 lambda
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarray_reverse.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

Results: focused `reverse` list `21 / 21`, up from `18 / 21`; refreshed
baseline gate `29,802 / 29,802`, `0` failures, `0` regressions. The full
non-updating batch is `29,802 / 34,148`, `4,346` failed, `8,071` skipped, `0`
improvements, `0` regressions, under the current 44-entry partial-test list.

Additional verification after the `%TypedArray%.prototype.subarray` RAB,
current-view, and species-constructor work:

```bash
make -j1 lambda
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarray_subarray.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

Results: focused `subarray` list `67 / 67`, up from `29 / 67`; refreshed
baseline gate `29,802 / 29,802`, `0` failures, `0` regressions. The full
non-updating batch is `29,870 / 34,148`, `4,278` failed, `8,071` skipped, `68`
improvements, `0` regressions, under the current 48-entry partial-test list.
The completed slice covers inherited `constructor` accessors, detached-buffer
species ordering, default species creation, and the `isConstructor`/
non-constructable typed-array method path while preserving
`Reflect.construct.length === 2`.

Additional verification after the `%TypedArray%.prototype.slice` ToInteger,
RAB, and species-copy work:

```bash
make -j1 lambda
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarray_slice.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

Results: focused `slice` list `92 / 93`, up from `70 / 93`; refreshed
baseline gate `29,802 / 29,802`, `0` failures, `0` regressions. The full
non-updating batch is `29,884 / 34,147`, `4,263` failed, `8,072` skipped, `82`
improvements, `0` regressions, under the current 50-entry partial-test list.
The remaining focused `slice` failure is `speciesctor-resize`, where a
loop-scoped dynamic typed-array subclass species constructor with an explicit
constructor still reaches the superclass call with an invalid callee
(`is not a constructor`) before it can resize the source RAB. A prototype fix
that treated class MAPs as pending `new.target` and routed dynamic class
construction through the generic class-object constructor was rejected because
the baseline gate exposed regressions in accepted builtin subclassing tests
(`ArrayBuffer`, `Map`, `RegExp`, `Set`, `WeakMap`, `WeakSet`, and related
statement/expression subclass fixtures). The next attempt should solve this at
the class/super binding model boundary rather than broadening `new.target` MAP
acceptance globally.

Additional verification after the `%TypedArray%.prototype.map` / `filter` RAB
callback-value work:

```bash
make -j1 lambda
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarray_map_filter.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

Results: focused `map`/`filter` list `166 / 168`, up from `156 / 168`; refreshed
baseline gate `29,802 / 29,802`, `0` failures, `0` regressions. The full
non-updating batch remains `29,884 / 34,147`, `4,263` failed, `8,072` skipped,
`82` improvements, and `0` regressions under the current partial-test list. The
remaining focused `map` failures are `speciesctor-resizable-buffer-grow` and
`speciesctor-resizable-buffer-shrink`, both blocked on the same loop-scoped
typed-array subclass species constructor issue as the final `slice` failure.

Additional verification after the `%TypedArray%.prototype.entries` / `keys` /
`values` live RAB iterator work:

```bash
make -j1 lambda
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarray_iterators.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

Results: focused `entries`/`keys`/`values` list `59 / 59`, up from `43 / 59`;
refreshed baseline gate `29,802 / 29,802`, `0` failures, `0` regressions. The
full non-updating batch is `29,885 / 34,147`, `4,262` failed, `8,072` skipped,
`83` improvements, and `0` regressions under the current partial-test list. This
slice replaces the old eager snapshot with a live typed-array-backed iterator
for direct `.next()` and `for...of`, including RAB out-of-bounds throws,
current-length grow/shrink behavior, `entries` pair materialization, and sticky
exhaustion after a length-tracking view reaches done.

Additional verification after the `TypedArrayConstructors/internals` canonical
numeric-index `Get` / `Set` / `HasProperty` / `Delete` work:

```bash
make -j1 lambda
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_ta_canonical_groups.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarrayconstructors_internals.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

Results: affected `Get` / `Set` / `HasProperty` / `Delete` list `130 / 152`,
up from `121 / 152` after the first canonical-key patch and from a much broader
ad hoc numeric-key failure cluster. The full `TypedArrayConstructors/internals`
manifest is now `170 / 240`, up from `108 / 240`. The baseline gate is clean at
`29,802 / 29,802`, `0` failures, `0` regressions. The full non-updating batch is
now `29,944 / 34,147`, `4,203` failed, `8,072` skipped, `142` improvements, and
`0` regressions under the current partial-test list.

This slice adds canonical numeric-index string detection for typed-array
property hooks, including `-0`, `Infinity`, `NaN`, integer strings, and JS-style
decimal canonicalization down to `0.000001`; routes non-canonical keys through
ordinary own/prototype property behavior; makes valid integer-index deletes
non-configurable; preserves ordinary properties for keys like `"1.0"` and
`"+1"`; fixes `Reflect.set` receiver-sensitive valid-index behavior; and moves
typed-array element value conversion ahead of integer-index validity checks for
direct assignment so abrupt `ToNumber` / `ToBigInt` completions propagate.

Additional verification after the `TypedArrayConstructors/internals`
`DefineOwnProperty` / `OwnPropertyKeys` work:

```bash
make -j1 lambda
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_ta_defown_ownkeys.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js40_typedarrayconstructors_internals.txt --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

Results: focused `DefineOwnProperty` / `OwnPropertyKeys` list `62 / 64`, up
from `58 / 64`; all `OwnPropertyKeys` tests pass. The two remaining focused
failures are the normal and BigInt cross-realm detached-buffer throw cases. The
full `TypedArrayConstructors/internals` manifest is now `220 / 240`, up from
`170 / 240` after the canonical-key slice and `108 / 240` before the internals
work. The baseline gate is clean at `29,802 / 29,802`, `0` failures, `0`
regressions. The full non-updating batch is now `29,992 / 34,147`, `4,155`
failed, `8,072` skipped, `190` improvements, and `0` regressions under the
current partial-test list.

This slice adds typed-array integer-indexed `[[DefineOwnProperty]]` handling
for `Object.defineProperty` and `Reflect.defineProperty`, including the
throw-vs-boolean distinction, accessor and non-writable/non-enumerable/
non-configurable rejection, invalid-index false results, and value-conversion
ordering before detached/OOB no-op semantics. `Object.getOwnPropertyNames` /
`Reflect.ownKeys` now synthesize current typed-array integer indices before
ordinary keys, and `Object.getOwnPropertyDescriptor` now synthesizes numeric
element descriptors while returning `undefined` for invalid numeric indices.
The MIR and runtime typed-array element read paths now expose JS `undefined`
for invalid or detached reads, with `indexOf` / `lastIndexOf` guarding the
post-`fromIndex` detached-buffer case so existing prototype-method behavior does
not regress.

Remaining `TypedArrayConstructors/internals` failures are now concentrated in
cross-realm detached-buffer error identity, prototype-chain `Set`,
BigInt64/BigUint64 modulo storage edge cases, and two detached `Infinity`
`HasProperty` cases. Those should be treated as separate structural internals
passes rather than folded into the descriptor/key path.

## 9. Verification Checklist

Use this checklist after each phase:

```bash
make build
./test/test_js_props_gtest.exe
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only
```

For baseline updates, follow `test/js262/JS262_Test_Guide.md`: run at least two
stable `--batch-only` verification passes, require `regressions=0`, and only
then use `--update-baseline`.

When updating with partial tests included, use the runner timeout knob if a
known-slow passing test hits the child batch timeout:

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --run-partial --update-baseline --js-timeout=20
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
```
