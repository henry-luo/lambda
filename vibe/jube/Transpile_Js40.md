# Transpile_Js40 - Structural test262 Enhancement Plan

Date: 2026-05-06

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

The important conclusion: the JS engine is not crashing or losing tests. The
remaining failures are spec machinery gaps plus 41 current baseline regressions.
Therefore Js40 should be a structural correctness program, not a crasher hunt.

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

Goal: make `TypedArray`/`DataView` behavior spec-driven instead of method-local.

Scope:

- Introduce internal-slot helpers:
  `js_is_typed_array`, `js_validate_typed_array`, `js_validate_dataview`,
  `js_is_detached_buffer`, `js_get_viewed_array_buffer`, and
  `js_typed_array_length_internal`.
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
  current regressions.

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
- Deduplicate and sort `test/js262/t262_partial.txt` when rewriting. The current
  file contains carried-forward duplicate-looking blocks after partial updates.
- Track memory regressions with a separate threshold. This run peaked at about
  1.1 GB RSS, with RegExp property escape tests causing the largest single-test
  growth.

## 7. Priority Order

Recommended order:

1. **J40-0 Regression recovery** - required before any baseline movement.
2. **J40-1 Descriptor/exotic kernel** - fixes regressions and unlocks broad
   Object/Array/String/Function behavior.
3. **J40-3 TypedArray/DataView internal slots** - high count and currently
   regression-linked.
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
