# Transpile_Js43 - Plan For The Remaining js262 Failure Clusters

Date: 2026-05-16

This proposal follows the Js42 work. Js42 materially changed the shape of the
problem: the runner can now emit machine-readable failure manifests, the
baseline has grown substantially, and the remaining failures are no longer a
single large structural wall. They are a smaller set of repeated semantic gaps.

Primary evidence for this proposal:

- `temp/js42_full_run_partial_after_fixes_failures.tsv`
- `temp/js42_full_run_partial_after_fixes_failures_by_path.tsv`
- `temp/js42_full_run_partial_after_fixes_failures_by_feature.tsv`
- `test/js262/test262_baseline.txt`
- `test/js262/t262_partial.txt`

The latest manifest contains 1,003 failed tests. The current baseline file has
32,857 passing non-comment entries. The remaining partial list has 46 tracked
slow or batch-kill entries. The practical next goal for Js43 should be to
retire the largest repeated failure families while preserving the Js42 baseline
gate.

## 1. Current Failure Shape

### 1.1 Failures by top-level path

| Area | Failures | Main theme |
|---|---:|---|
| `language/statements` | 224 | class semantics, `with`, try/finally completion, Annex B-adjacent declaration behavior |
| `built_ins/Array` | 190 | array prototype algorithms, mutation side effects, species/unscopables, length/index descriptors |
| `built_ins/TypedArray` | 92 | integer-indexed exotic `set`, `join`, `sort`, `with`, `toLocaleString` |
| `language/expressions` | 60 | class private brand/eval cases, generators, call/reference semantics |
| `annexB/language/function-code` | 58 | sloppy block function declaration instantiation |
| `annexB/language/global-code` | 57 | global sloppy block function declaration instantiation |
| `built_ins/RegExp` | 54 | named groups, lookbehind, regexp literal/prototype protocol edges |
| `built_ins/Object` | 46 | prototype helpers, `assign`, symbols, `fromEntries` |
| `built_ins/Promise` | 41 | iterator-close and combinator slow/failure cases |
| `built_ins/String` | 33 | string prototype protocol/coercion details |
| `built_ins/Proxy` | 20 | trap invariants and receiver semantics |
| `language/function-code` | 19 | declaration/eval binding details |
| `language/global-code` | 16 | global declaration instantiation |
| `built_ins/Function` | 14 | constructor/prototype restricted behavior |
| `language/literals` | 13 | regexp and string literal edge cases |

Everything else is single-digit or low-teens cleanup.

### 1.2 Larger path clusters

Grouping one level deeper shows where the repeated work is:

| Cluster | Failures | Notes |
|---|---:|---|
| `built-ins/Array/prototype` | 166 | mostly ordinary algorithm and side-effect order issues |
| `language/statements/class` | 91 | class elements, subclassing, static blocks |
| `built-ins/TypedArray/prototype` | 91 | typed-array exotic operations |
| `annexB/language/function-code` | 58 | same family of sloppy block function tests |
| `annexB/language/global-code` | 57 | same family at global scope |
| `language/statements/with` | 53 | `with` object environment record support |
| `language/expressions/class` | 43 | private names and multiple-evaluation/eval cases |
| `built-ins/String/prototype` | 23 | protocol dispatch and borrowed-method coercions |
| `language/statements/try` | 22 | completion/finally propagation |
| `built-ins/Object/prototype` | 21 | `__defineGetter__`, `__lookupGetter__`, descriptors |
| `built-ins/RegExp/named-groups` | 16 | named capture support |
| `built-ins/RegExp/lookBehind` | 16 | lookbehind support and errors |

### 1.3 Feature tags

The feature summary is less complete because 534 failures carry no feature tag,
but the tagged failures still point to the same roots:

| Feature | Failures |
|---|---:|
| `TypedArray` | 96 |
| `class` | 93 |
| `Proxy` | 64 |
| `BigInt` | 48 |
| `Symbol` | 33 |
| `generators` | 32 |
| `Reflect` | 26 |
| `change-array-by-copy` | 23 |
| `class-methods-private` | 33 |
| `class-fields-private` | 19 |
| `class-fields-public` | 18 |
| `async-iteration` | 17 |
| `regexp-lookbehind` | 17 |
| `regexp-named-groups` | 16 |
| `exponentiation` | 16 |

## 2. Diagnosis

### 2.1 Annex B block functions are the highest-leverage language fix

The most obvious repeated diagnostic is:

```text
An initialized binding is not created prior to evaluation Expected a ReferenceError to be thrown but no exception was thrown at all
```

This appears 88 times and is concentrated in:

- `annexB/language/function-code`
- `annexB/language/global-code`
- related `language/function-code` and `language/global-code` declaration tests

The current implementation is close enough to create some bindings, but it does
not model the split between lexical binding creation, var/global binding
instantiation, and Annex B.3.3 runtime replacement precisely. The failures are
not isolated syntax bugs; they point to declaration-instantiation order and
sloppy block function semantics.

### 2.2 `with` needs a real object environment record

`language/statements/with` still has 53 failures. These are unlikely to be
fixed by adding individual lookup cases. `with` changes identifier resolution
through an object environment record and must respect:

- property lookup through the object and prototype chain;
- `Symbol.unscopables`;
- assignment target resolution;
- deletion and ReferenceError behavior;
- interaction with `let`, `const`, `var`, function declarations, and eval.

This is a good Js43 target because it is relatively contained and it unlocks a
large language/statements cluster without touching every built-in.

### 2.3 Array and TypedArray share abstract-operation gaps

Array prototype failures are spread across many methods:

| Method | Failures |
|---|---:|
| `sort` | 21 |
| `concat` | 18 |
| `reduceRight` | 17 |
| `push` / `unshift` | 18 |
| `shift` / `pop` | 13 |
| `reduce` | 8 |
| change-by-copy methods (`toSpliced`, `with`, `toReversed`, `toSorted`) | 18 |
| callback methods (`some`, `map`, `forEach`, `filter`, `every`) | 19 |
| search/index methods | 7 |

TypedArray prototype failures are even more concentrated:

| Method | Failures |
|---|---:|
| `set` | 44 |
| `toLocaleString` | 22 |
| `join` | 11 |
| `sort` | 8 |
| `with` | 5 |

The common theme is algorithm conformance around property access order,
observable side effects, integer-indexed exotic behavior, detach checks,
species/constructor lookup, and abrupt completion. Js43 should not hand-fix
each method independently. It should add a small set of reusable operation
helpers and then port the high-count methods.

### 2.4 Class failures are now mostly private-name and subclass edges

Class failures fell far enough that the remaining set is crisp:

- `language/statements/class/elements`: 37
- `language/statements/class/subclass`: 30
- `language/statements/class/definition`: 13
- `language/expressions/class`: 43, mostly private static/private method brand
  checks across direct eval, indirect eval, factory, and function constructor
  multiple-evaluation cases

The repeated diagnostic:

```text
invalid access of c1 private method Expected a TypeError to be thrown but no exception was thrown at all
```

appears 15 times. That points to private name allocation/brand identity being
too broad or being reused across class evaluations.

### 2.5 RegExp is smaller but still structurally special

RegExp failures are no longer dominated by broad property-escape tables in the
current manifest. The remaining high-count sets are:

- named groups: 16
- lookbehind: 16
- regexp literals: 10
- regexp prototype protocol: 10

This is a good bounded phase if Js43 wants a built-in win without reopening the
entire RegExp subsystem. Named captures and lookbehind validation/execution
should remain behind the existing RegExp front-end path, not patched in string
methods.

### 2.6 Promise failures are split between correctness and speed

`built_ins/Promise` has 41 current failures, and the partial list still includes
many slow Promise combinator iterator-close tests:

- `Promise.all`
- `Promise.allSettled`
- `Promise.any`
- `Promise.race`

The slow entries are mostly iterator-close paths where resolve/then retrieval
throws or returns a bad value. The fix should make Promise combinators share one
iterator loop with early-close and job-queue behavior. A local speed patch is
unlikely to stay correct.

## 3. Js43 Target

The recommended Js43 target is:

```text
Current known failures: 1,003
Target remaining failures after Js43: 500-650
Baseline target: 33,200+ fully passing tests
Regression target: 0
Crash/batch-lost target: 0
Partial-list target: below 30 entries
```

The plan deliberately avoids trying to close every remaining failure. The best
return is to retire repeated families and leave the long tail for Js44.

## 4. Proposed Phases

### J43-1 - Annex B Declaration Instantiation

Goal: correctly implement sloppy block function declaration behavior in
function and global code.

Scope:

- `annexB/language/function-code`: 58 failures
- `annexB/language/global-code`: 57 failures
- collateral in `language/function-code` and `language/global-code`

Work:

- Add an explicit collection pass for Annex B.3.3 block function declarations.
- Separate lexical declaration creation from var/global object binding updates.
- For function code, implement the conditional runtime update from the block
  function binding to the var binding only when the Annex B rules allow it.
- For global code, route updates through global environment operations so
  property creation and descriptor checks are observable.
- Preserve early TDZ behavior for the `skip-early-err-*` tests.
- Add focused manifests for the two Annex B directories and the related
  non-Annex global/function declaration directories.

Gate:

```bash
./test/test_js_test262_gtest.exe --batch-file=temp/js43_annexb_decl_batch.txt --js-timeout=30 --write-failures=temp/js43_annexb_decl_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_baseline_after_annexb_failures.tsv
```

Expected impact:

- 100-150 passing tests.

Status 2026-05-16:

- Completed the first Annex B declaration-instantiation slice against the
  failure-derived batch `temp/js43_annex_from_failures.txt`.
- Fixed sloppy block function binding split for function/global code, including
  suppressed early-error cases, `arguments` binding collisions, nested block
  function replacement guards, and visible-scope capture filtering.
- Fixed adjacent Annex B stragglers surfaced by the focused batch:
  non-strict `for-in` initializers, catch-parameter `var` initializer scoping,
  `$262.evalScript` global lexical collisions, and arguments-object
  `toString`.
- Verification: `make build-test` passed, and
  `./test/test_js_test262_gtest.exe --batch-file=temp/js43_annex_from_failures.txt --js-timeout=30 --write-failures=temp/js43_annex_from_failures_failures.tsv`
  passed `118/118`.

### J43-2 - Object Environment Records For `with`

Goal: implement `with` identifier resolution through a proper object
environment record.

Scope:

- `language/statements/with`: 53 failures
- collateral in `Symbol.unscopables`, assignment/reference tests, and eval
  declaration tests

Work:

- Add a runtime environment-record variant for object environments.
- Implement `HasBinding` using `HasProperty` plus `@@unscopables` checks.
- Route identifier get/set/delete inside `with` through Reference Records.
- Ensure `var` declarations still target the variable environment, not the
  object environment.
- Verify nested `with`, prototype-chain lookup, accessor properties, deletion,
  strict/sloppy assignment, and eval interactions.

Gate:

```bash
find test/js262/test/language/statements/with -type f -name '*.js' | sed 's#^test/js262/test/##; s#/#_#g; s#[-.]#_#g' | sort > temp/js43_with_batch.txt
./test/test_js_test262_gtest.exe --batch-file=temp/js43_with_batch.txt --js-timeout=30 --write-failures=temp/js43_with_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_baseline_after_with_failures.tsv
```

Expected impact:

- 45-70 passing tests.

Status 2026-05-16:

- Completed the first `with` object-environment slice against
  `temp/js43_with_batch.txt`.
- Fixed `with(null)` / `with(undefined)` to throw TypeError before entering the
  body, and routed the exception through MIR propagation.
- Made global-ish identifier reads inside `with` consult the object
  environment before falling back to intrinsic/global values. This covers
  `undefined`, `NaN`, `Infinity`, `globalThis`, namespace objects, global
  builtin functions, and constructor/global-property reads while preserving the
  existing with-aware global lookup path to avoid duplicate proxy traps.
- Fixed legacy `var` initializer assignment inside `with`: the `var` binding
  is still hoisted to the variable environment, but the initializer writes to
  the active object environment when `HasBinding` succeeds.
- Fixed `with` statement completion for empty normal/abrupt bodies by resetting
  eval completion before evaluating the body.
- Enabled early-error checking for dynamic `Function(...)` sources so strict
  functions containing `with` throw SyntaxError.
- Verification: `make build-test` passed, and
  `./test/test_js_test262_gtest.exe --batch-file=temp/js43_with_batch.txt --js-timeout=30 --write-failures=temp/js43_with_after_cleanup.tsv`
  improved the focused batch from `128/181` to `157/181`.
- Follow-up slice: function objects now capture the active `with` object stack
  when created inside a `with` body, and compiled JS calls enter their callee
  lexical `with` stack before restoring the caller stack. This fixes escaped
  closure cases such as `12.10-0-3`.
- Fixed a crash-class regression by aligning the `JsCtor` and
  `JsFunctionLayout` mirrors in `js_globals.cpp` with the shared `JsFunction`
  layout after adding captured `with` fields. This also keeps builtin
  constructors safe when called through the new call-entry environment restore.
- Verification: `make build-test` passed, the local proxy repros under
  `temp/js43_proxy_repro*.js` no longer segfault, and
  `./test/test_js_test262_gtest.exe --batch-file=temp/js43_proxy_crash_batch.txt --js-timeout=30 --write-failures=temp/js43_proxy_after_layout_fix.tsv`
  now passes the two proxy call tests that previously crashed. The full focused
  batch remains `157/181` with zero crashes:
  `temp/js43_with_after_layout_fix.tsv`.
- Fixed the proxy compound-assignment duplicate-trap case by only caching the
  last resolved `with` binding after the final `[[Get]]` completes. This keeps
  proxy getter calls from invalidating the cache mid-lookup and forcing the
  subsequent write through the slower global fallback path.
- Split identifier global lookup from object-environment strictness for
  `GetBindingValue`. Bare identifier reads still throw for truly unresolvable
  names, but a binding deleted during `@@unscopables` lookup now returns
  `undefined` in sloppy code and throws ReferenceError in strict code.
- Verification: `make build-test` passed;
  `./test/test_js_test262_gtest.exe --batch-file=temp/js43_proxy_crash_batch.txt --js-timeout=30 --write-failures=temp/js43_proxy_after_cache_after_get.tsv`
  passed `3/3`; `./test/test_js_test262_gtest.exe --batch-file=temp/js43_with_deleted_unscopables_batch.txt --js-timeout=30 --write-failures=temp/js43_with_deleted_unscopables_after_registry.tsv`
  passed `2/2`; and
  `./test/test_js_test262_gtest.exe --batch-file=temp/js43_with_batch.txt --js-timeout=30 --write-failures=temp/js43_with_after_reference_lookup.tsv`
  improved the focused batch to `159/181` with zero crashes.
- Fixed captured-runtime-`with` identifier deletion by always routing
  identifier `delete` through `js_delete_identifier_with_binding`. This lets a
  function compiled with no lexical `with_depth`, but invoked through a
  captured object environment, delete object-environment bindings before
  declared/global fallback.
- Fixed special global fast-path reads (`parseInt`, `NaN`, `Infinity`, `eval`,
  etc.) to consult `js_get_with_binding_or_fallback` regardless of compile-time
  `with_depth`. The runtime helper returns the original fast-path value when no
  object environment is active.
- Fixed direct-call optimization across `with` boundaries. Calls whose call
  site, target function, or current function is lexically connected to a
  `with` statement now fall back to `js_call_function`, so the callee wrapper
  can install its captured `with` stack instead of leaking or dropping the
  caller environment.
- Fixed module/top-level binding reads inside escaped `with` functions by
  applying the same runtime object-environment fallback to module constants,
  functions, classes, and modvars. This covers the A3.12 `return value` /
  `throw value` cases where the object environment shadows a top-level
  binding after the `with` body has exited.
- Verification: `make build-test` passed, and
  `./test/test_js_test262_gtest.exe --batch-file=temp/js43_with_batch.txt --js-timeout=30 --write-failures=temp/js43_with_after_module_with_reads.tsv`
  passed `181/181`.
- Broad gate note: `./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_baseline_after_with_green.tsv`
  completed with `33271/34145` fully passing, `487` improvements, and `72`
  reported regressions versus the checked-in baseline. The reported regressions
  are outside `language/statements/with` and should be triaged separately before
  using the broad gate as the final Js43 acceptance signal.

### J43-3 - Array Algorithm Spine

Goal: make high-count Array prototype methods use common abstract-operation
helpers instead of local method-specific shortcuts.

Scope:

- `built-ins/Array/prototype`: 166 failures
- `Array` length and constructor edge cases: about 20 more failures

Work:

- Add or harden helpers for `LengthOfArrayLike`, `ToLength`, `HasProperty`,
  `Get`, `Set`, `DeletePropertyOrThrow`, `CreateDataPropertyOrThrow`, and
  `ArraySpeciesCreate`.
- Port the highest-count methods first:
  `sort`, `concat`, `reduceRight`, `push`, `unshift`, `shift`, `pop`,
  `reduce`.
- Then port the change-by-copy family:
  `toSpliced`, `with`, `toReversed`, `toSorted`.
- Verify callback method ordering and abrupt completion:
  `some`, `map`, `forEach`, `filter`, `every`.
- Treat sparse arrays, holes, prototype accessors, non-writable length, and
  self-mutating callbacks as first-class tests.

Gate:

```bash
./test/test_js_test262_gtest.exe --gtest_filter='*built_ins_Array_prototype*' --write-failures=temp/js43_array_proto_failures.tsv
./test/test_js_array_gtest.exe
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_baseline_after_array_failures.tsv
```

Expected impact:

- 100-170 passing tests.

### J43-4 - TypedArray Integer-Indexed Exotic Operations

Goal: make TypedArray prototype algorithms share detach, bounds, conversion,
and integer-indexed exotic semantics.

Scope:

- `built-ins/TypedArray/prototype`: 91 failures
- `TypedArray` feature-tagged failures: 96
- BigInt typed-array collateral: up to 48 tagged failures

Work:

- Centralize `ValidateTypedArray`, detach checks, length/bounds checks, and
  element conversion.
- Make `TypedArray.prototype.set` handle overlap, source coercion, abrupt
  completion, detached buffers, and BigInt/Number separation.
- Port `join`, `toLocaleString`, `sort`, and `with` to the shared helpers.
- Ensure descriptor behavior for integer-indexed properties matches exotic
  objects: impossible deletion, define failures, canonical numeric index
  strings, and receiver behavior.

Gate:

```bash
./test/test_js_test262_gtest.exe --gtest_filter='*built_ins_TypedArray_prototype*' --write-failures=temp/js43_typedarray_proto_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_baseline_after_typedarray_failures.tsv
```

Expected impact:

- 70-120 passing tests.

### J43-5 - Private Name Brand Identity And Class Evaluation

Goal: make every class evaluation allocate distinct private names and enforce
brand checks correctly.

Scope:

- `language/statements/class`: 91 failures
- `language/expressions/class`: 43 failures
- private field/method/static feature-tagged failures

Work:

- Audit private name storage so private names are records allocated per class
  evaluation, not reused by source name alone.
- Ensure direct eval, indirect eval, function constructor, and factory-created
  classes each allocate distinct private brand records.
- Fix private getter/setter/method static and instance brand checks to throw
  TypeError on wrong receiver.
- Recheck subclass constructor initialization order, `super`, static blocks,
  `new.target`, and computed property name evaluation order.

Gate:

```bash
./test/test_js_test262_gtest.exe --gtest_filter='*language_statements_class*:*language_expressions_class*' --write-failures=temp/js43_class_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_baseline_after_class_failures.tsv
```

Expected impact:

- 70-130 passing tests.

### J43-6 - RegExp Named Groups And Lookbehind

Goal: close the current RegExp cluster without broadening into unsupported
future RegExp features.

Scope:

- `built-ins/RegExp/named-groups`: 16 failures
- `built-ins/RegExp/lookBehind`: 16 failures
- `language/literals/regexp`: 10 failures
- `built-ins/RegExp/prototype`: 10 failures

Work:

- Keep all pattern validation in the RegExp compile/front-end path.
- Implement named capture group parsing, duplicate-name validation, named
  backreference lookup, and match result `groups` materialization.
- Implement lookbehind validation and backend execution strategy for fixed
  length cases, with correct SyntaxError paths for invalid patterns.
- Recheck `lastIndex`, sticky/global, `exec`, `test`, `@@match`,
  `@@replace`, `@@search`, and `@@split` protocol behavior for named groups.

Gate:

```bash
./test/test_js_test262_gtest.exe --gtest_filter='*built_ins_RegExp_named_groups*:*built_ins_RegExp_lookBehind*:*language_literals_regexp*' --write-failures=temp/js43_regexp_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_baseline_after_regexp_failures.tsv
```

Expected impact:

- 35-60 passing tests.

### J43-7 - Promise Combinator Iterator Close

Goal: make Promise combinators correct and fast on abrupt iterator paths.

Scope:

- `built-ins/Promise`: 41 failures
- many remaining `SLOW_*` partial-list entries

Work:

- Route `Promise.all`, `allSettled`, `any`, and `race` through one iterator
  consumption helper.
- Implement correct `IteratorClose` when resolving functions, `then` getters,
  or iterator methods throw.
- Cache only spec-safe operations; do not skip observable property reads.
- Use the job queue consistently for thenable assimilation and reaction jobs.
- Re-run the partial list after each optimization because many failures are
  time-threshold failures rather than direct assertions.

Gate:

```bash
./test/test_js_test262_gtest.exe --gtest_filter='*built_ins_Promise_all*:*built_ins_Promise_allSettled*:*built_ins_Promise_any*:*built_ins_Promise_race*' --run-partial --js-timeout=30 --write-failures=temp/js43_promise_failures.tsv
./test/test_js_test262_gtest.exe --run-partial --update-baseline --js-timeout=30 --write-failures=temp/js43_after_promise_failures.tsv
```

Expected impact:

- 25-50 passing tests.
- Reduce partial-list slow entries by 10-20 if performance remains below the
  slow threshold.

### J43-8 - Object/String/Proxy Long-Tail Batch

Goal: clean the smaller built-in clusters once the property/reference helpers
from earlier phases are stable.

Scope:

- `built-ins/Object`: 46 failures
- `built-ins/String`: 33 failures
- `built-ins/Proxy`: 20 failures
- `built-ins/Reflect`: 9 failures

Work:

- Object:
  `Object.prototype` getter/setter helpers, `assign`, `fromEntries`,
  `getOwnPropertySymbols`, descriptor edge cases.
- String:
  borrowed-method coercions, `Symbol.split`, `toStringTag`, regexp protocol
  handoff, surrogate/Unicode edge cases.
- Proxy/Reflect:
  `get`, `set`, `has`, `defineProperty`, `deleteProperty`, `construct`, and
  receiver/invariant enforcement.
- Keep Proxy/Reflect tests together because a passing Reflect path often
  exposes a Proxy invariant failure.

Gate:

```bash
./test/test_js_test262_gtest.exe --gtest_filter='*built_ins_Object*:*built_ins_String*:*built_ins_Proxy*:*built_ins_Reflect*' --write-failures=temp/js43_builtin_tail_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_baseline_after_builtin_tail_failures.tsv
```

Expected impact:

- 60-100 passing tests.

## 5. Verification Discipline

Every phase should follow the same loop:

1. Build the focused failure manifest from the current source of truth.
2. Add one or more LambdaJS unit tests for the root semantic issue.
3. Run the focused js262 batch.
4. Run the baseline gate.
5. Only update `test262_baseline.txt` when there are zero regressions, zero
   crash exits, and zero batch-lost tests.
6. Re-run `--run-partial` before touching `t262_partial.txt`.

Recommended end-of-Js43 verification:

```bash
make build-test
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_final_baseline_failures.tsv
./test/test_js_test262_gtest.exe --run-partial --update-baseline --js-timeout=30 --write-failures=temp/js43_final_partial_failures.tsv
make test262-baseline
```

## 6. Recommended Order

1. J43-1 Annex B declaration instantiation.
2. J43-2 `with` object environment records.
3. J43-3 Array algorithm spine.
4. J43-4 TypedArray integer-indexed operations.
5. J43-5 private names and class evaluation.
6. J43-7 Promise iterator close and partial-list cleanup.
7. J43-6 RegExp named groups/lookbehind.
8. J43-8 Object/String/Proxy/Reflect long tail.

This order puts the environment/reference model first, then high-count array
algorithms, then class identity, then the slower built-in/protocol work. It also
keeps the riskiest areas away from the final baseline update until the shared
semantics have settled.

## 7. Non-Goals

- Do not unlock broad skipped ES2021+ feature sets in Js43.
- Do not add new skips for currently in-scope failures.
- Do not patch individual test names without first identifying the shared
  operation being violated.
- Do not manually edit generated parser files or build Lua files.
- Do not optimize Promise or Array paths in a way that removes observable
  `Get`, `Set`, `HasProperty`, `then`, `constructor`, or `@@species` lookups.

## 8. Success Criteria

Js43 is successful if:

- known failures drop from 1,003 to roughly 500-650;
- the fully passing baseline grows past 33,200;
- `make test262-baseline` passes with 0 regressions;
- `t262_partial.txt` shrinks or at least does not grow;
- no new crash, timeout, or batch-kill family is introduced;
- the fix areas are backed by reusable semantic helpers, not one-off test
  branches.

## 9. Regression Fix Status

Status on 2026-05-16:

- Fixed the 72 reported broad-batch regressions.
- Root causes addressed:
  - closure capture analysis missed enclosing block, catch, and loop lexical
    scopes for nested functions;
  - object-literal methods inside `for (let/const ... of/in ...)` did not see
    the loop-head binding form stored as `left + kind`;
  - sloppy direct-eval Annex B globals were treated as non-configurable
    restricted globals during `$262.evalScript` lexical prechecks.
- Focused representative gate:

```bash
./test/test_js_test262_gtest.exe --batch-file=temp/js43_regression_representatives.txt --js-timeout=30 --write-failures=temp/js43_regression_representatives_after_forof_kind.tsv
```

Result: 5 / 5 passed.

- Broad gate:

```bash
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_after_regression_fixes.tsv
```

Result: 33,346 / 34,145 fully passing, 799 failing, 0 non-fully-passing,
489 improvements, and 0 regressions versus baseline.

## 10. Current Progress

Status on 2026-05-16 after the latest remaining-failure pass:

- Fixed `Array.prototype.flat` / `flatMap` generic receiver behavior:
  - proxy `HasProperty` / `Get` ordering is now observable;
  - `ArraySpeciesCreate` now constructs custom species with the proper
    constructor path;
  - `flatMap` preserves strict callback `thisArg` and bound-function callback
    semantics.
- Fixed `Object.assign`'s shared copy operation:
  - sources now use `ToObject` and `OwnPropertyKeys` instead of manually walking
    plain map shapes;
  - proxy `ownKeys` / `getOwnPropertyDescriptor` ordering and abrupt completion
    now flow through the normal path;
  - array sources and array targets now use their exotic property behavior;
  - target writes use strict `Set` behavior, including read-only String wrapper
    indices.
- Focused gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_flat_batch.txt --write-failures=temp/js43_array_flat_bound_rerun_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_object_assign_batch.txt --write-failures=temp/js43_object_assign_rerun2_failures.tsv
```

Results: flat batch 6 / 6 passed; Object.assign batch 8 / 8 passed.

- Broad gate:

```bash
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_after_object_assign_full_failures.tsv
```

Result: 33,541 / 34,165 fully passing, 608 failing, 16 non-fully-passing,
183 improvements, and 0 regressions versus baseline.

Current top remaining path clusters:

| Area | Failures |
|---|---:|
| `language/statements` | 160 |
| `built_ins/TypedArray` | 87 |
| `language/expressions` | 60 |
| `built_ins/RegExp` | 54 |
| `built_ins/Promise` | 41 |
| `built_ins/String` | 31 |
| `built_ins/Object` | 28 |
| `built_ins/Array` | 25 |

## 11. OwnPropertyKeys / Symbol Integrity Pass

Status on 2026-05-16 after the latest remaining-failure pass:

- Fixed `Object.getOwnPropertySymbols` as a real own-key operation:
  - proxy inputs now route through the existing `[[OwnPropertyKeys]]` trap and
    invariant checks before filtering symbol keys;
  - arrays now expose symbol keys stored in their companion property map;
  - function custom symbol properties are read from the function property map;
  - ordinary map symbol keys are converted from internal `__sym_N` storage back
    to public Symbol items.
- Fixed proxy integrity checks for `Object.isFrozen` / `Object.isSealed`:
  - proxies now call `IsExtensible`, `OwnPropertyKeys`, and
    `GetOwnPropertyDescriptor` in key order;
  - descriptor checks now honor proxy trap exceptions and order-observable
    `getOwnPropertyDescriptor` calls.
- Fixed an arguments-object metadata leak exposed by the broader symbol path:
  - internal `Symbol.toStringTag = "Arguments"` is now marked non-enumerable
    when the arguments companion map is built;
  - this prevents `Object.defineProperties` / `Object.create` from treating the
    internal tag value as a property descriptor.

Focused gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_ownkeys_symbols_integrity_batch.txt --write-failures=temp/js43_ownkeys_symbols_integrity_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_arguments_descriptor_batch.txt --write-failures=temp/js43_arguments_descriptor_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_object_assign_batch.txt --write-failures=temp/js43_object_assign_guard_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_flat_batch.txt --write-failures=temp/js43_array_flat_guard_failures.tsv
```

Results: ownKeys/symbol batch 8 / 8 passed; arguments descriptor batch 5 / 5
passed; Object.assign guard 8 / 8 passed; Array flat/flatMap guard 6 / 6
passed.

Broad gate before the final arguments metadata fix:

```bash
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_after_ownkeys_symbols_full_failures.tsv
```

Result: 33,509 / 34,165 fully passing, 584 failing, 72 non-fully-passing, 191
improvements, and 11 baseline regressions reported by the debug runner. Five
descriptor regressions were the arguments `Symbol.toStringTag` leak and are now
covered by the focused descriptor batch above; the remaining six reported
regressions are the existing 10s-per-test Array callback timeout family in this
debug runner.

## 12. Object.prototype.toString Tag Semantics Pass

Status on 2026-05-16 after the latest Object-cluster pass:

- Fixed `Object.prototype.toString` to read `@@toStringTag` through normal
  `Get` semantics instead of raw map-slot probing:
  - tag getters now run and abrupt completions propagate;
  - primitive receivers are boxed with `ToObject` before reading inherited
    `@@toStringTag`, so Boolean / Number / String / BigInt prototype overrides
    are visible;
  - proxy function targets now keep async/generator function brands.
- Fixed fallback behavior for built-ins whose tag in these tests comes from
  `@@toStringTag` rather than an intrinsic brand:
  - deleting or replacing `Symbol.prototype[Symbol.toStringTag]` with a
    non-string falls back to `[object Object]`;
  - `Math` and Promise instances now fall back to `[object Object]` when their
    tag property is missing or non-string.
- Left one generator-prototype failure open:
  - `Object.getPrototypeOf(gen) === genFn.prototype` is still false because the
    generator instance currently inherits from the shared generator prototype
    one level too high;
  - avoid solving this by sharing all generator-function `.prototype` objects,
    because that would violate per-function prototype identity.

Focused gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_object_tostring_batch.txt --write-failures=temp/js43_object_tostring_final_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_object_remaining_batch.txt --write-failures=temp/js43_object_remaining_after_tostring_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_ownkeys_symbols_integrity_batch.txt --write-failures=temp/js43_ownkeys_symbols_integrity_after_tostring_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_arguments_descriptor_batch.txt --write-failures=temp/js43_arguments_descriptor_after_tostring_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_object_assign_batch.txt --write-failures=temp/js43_object_assign_after_tostring_seq_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_flat_batch.txt --write-failures=temp/js43_array_flat_after_tostring_seq_failures.tsv
```

Results: Object toString batch 8 / 9 passed; Object remaining batch improved
from 5 / 26 to 13 / 26 passed; ownKeys/symbol guard 8 / 8 passed; arguments
descriptor guard 5 / 5 passed; Object.assign guard 8 / 8 passed; Array
flat/flatMap guard 6 / 6 passed. The first parallel Object.assign / Array
flat guard attempt produced runner-local "lost" rows; both passed when rerun
sequentially.

## 13. Object Prototype / Constructor Semantics Pass

Status on 2026-05-16 after continuing the remaining Object failures:

- Fixed object-prototype operations to accept every object representation used
  by the runtime, not just ordinary maps:
  - `Object.create` and proxy `getPrototypeOf` results now accept arrays,
    functions, elements, ordinary maps, or null;
  - `Object.getPrototypeOf(arguments)` now reports `Object.prototype` for the
    arguments exotic instead of leaking the array carrier prototype;
  - `Object.prototype.__lookupGetter__` / `__lookupSetter__` now walk the
    prototype chain through `[[GetPrototypeOf]]`, so proxy abrupt completions
    and traps are observable.
- Fixed prototype mutation invariants:
  - `Object.setPrototypeOf` now routes through the same semantic path as
    `Reflect.setPrototypeOf` and propagates failures;
  - `Object.prototype` is treated as an immutable-prototype exotic object;
  - assigning `__proto__` on ordinary objects now rejects non-extensible
    targets instead of silently changing the internal prototype.
- Fixed constructor/newTarget edges:
  - subclassing `Object` now creates the subclass instance and applies
    `newTarget.prototype` instead of returning the wrapped argument object;
  - `Reflect.construct(Object, args, NewTarget)` now accepts constructable
    class maps as `newTarget` and applies object-valued prototypes from maps,
    arrays, functions, or elements.
- Finished the toString generator open item from section 12:
  - no-argument generator direct calls now fall back to the runtime call setup
    path so the generator instance receives the public `genFn.prototype`;
  - the direct-call path still needs a fuller future cleanup to thread that
    prototype explicitly for every generator shape.
- Closed the follow-up regressions from the broad Object pass:
  - `Object.entries` / `Object.values` now skip symbol keys after switching to
    `Reflect.ownKeys`;
  - function own-property names no longer duplicate intrinsic `length`, `name`,
    or `prototype` entries when custom descriptors exist;
  - primitive BigInt `Object.prototype.toString` now preserves the BigInt brand
    when no `@@toStringTag` exists and falls back to Object for a present
    non-string tag.

Focused gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_object_remaining_batch.txt --write-failures=temp/js43_object_remaining_after_generator_call_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_object_tostring_batch.txt --write-failures=temp/js43_object_tostring_after_generator_call_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_ownkeys_symbols_integrity_batch.txt --write-failures=temp/js43_ownkeys_symbols_integrity_after_object_proto_ops_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_arguments_descriptor_batch.txt --write-failures=temp/js43_arguments_descriptor_after_object_proto_ops_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_object_assign_batch.txt --write-failures=temp/js43_object_assign_after_object_proto_ops_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_flat_batch.txt --write-failures=temp/js43_array_flat_after_object_proto_ops_failures.tsv
```

Results: Object remaining batch 26 / 26 passed; Object toString batch 9 / 9
passed; ownKeys/symbol guard 8 / 8 passed; arguments descriptor guard 5 / 5
passed; Object.assign guard 8 / 8 passed; Array flat/flatMap guard 6 / 6
passed.

Broad gate before the final regression follow-up:

```bash
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_after_object_proto_ops_full_failures.tsv
```

Result: 33,565 / 34,165 fully passing, 589 failing, 11 non-fully-passing, 215
improvements, and 7 reported regressions. Those seven were the two symbol-key
enumeration fallouts, one primitive BigInt toString-tag fallback, and four
generator direct-call cases; the focused regression follow-up below passes
after the fixes above.

Regression follow-up gate:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_regression_followup_batch.txt --write-failures=temp/js43_regression_followup_clean_failures.tsv
```

Result: 8 / 8 passed.

Final broad gate after the regression follow-up:

```bash
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_after_object_proto_ops_regression_fixed_full_failures.tsv
```

Result: 33,573 / 34,165 fully passing, 583 failing, 9 non-fully-passing, 215
improvements, and 0 regressions versus baseline. The nine non-fully-passing
rows were recovered in isolated retry and are the existing slow/batch-kill
family, mostly Array callback/index methods plus one RegExp whitespace case.

## 14. Async / Generator Constructor Shape Pass

Status on 2026-05-16 after the async/generator cluster pass:

- Fixed dynamic `Function`-family compilation so each constructor parses the
  correct source form instead of compiling an ordinary function and patching
  flags afterward:
  - `AsyncFunction` compiles `async function anonymous(...) { ... }`;
  - `GeneratorFunction` compiles `function* anonymous(...) { ... }`;
  - `AsyncGeneratorFunction` compiles `async function* anonymous(...) { ... }`;
  - generated dynamic functions now receive the spec name `anonymous`.
- Fixed async/generator function prototype shape:
  - `%AsyncFunction.prototype%`, `%GeneratorFunction.prototype%`, and
    `%AsyncGeneratorFunction.prototype%` inherit from `Function.prototype`;
  - their constructor objects inherit from `Function`;
  - `.constructor` and `@@toStringTag` descriptors are non-enumerable and
    non-writable as expected by Test262.
- Fixed async/generator constructability:
  - async functions are no longer constructable;
  - async non-generator functions no longer expose an own public `.prototype`;
  - typed-array species construction rejects async functions as constructors.
- Fixed async iterator / async generator prototype plumbing:
  - `%AsyncIteratorPrototype%` now exposes `[Symbol.asyncIterator]`;
  - async generator shared prototypes now expose `next`, `return`, and `throw`.
- Fixed dynamic generator early errors:
  - `yield` in generator formal parameters and `await` in async formal
    parameters now fail early instead of reaching MIR lowering with missing
    labels.

Focused gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_async_generator_shape_batch.txt --write-failures=temp/js43_async_generator_shape_after_early_error_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_object_remaining_batch.txt --write-failures=temp/js43_object_remaining_after_async_gen_seq_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_object_tostring_batch.txt --write-failures=temp/js43_object_tostring_after_async_gen_seq_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_regression_followup_batch.txt --write-failures=temp/js43_regression_followup_after_async_gen_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_ownkeys_symbols_integrity_batch.txt --write-failures=temp/js43_ownkeys_symbols_after_async_gen_seq_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_arguments_descriptor_batch.txt --write-failures=temp/js43_arguments_descriptor_after_async_gen_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_object_assign_batch.txt --write-failures=temp/js43_object_assign_after_async_gen_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_flat_batch.txt --write-failures=temp/js43_array_flat_after_async_gen_seq_failures.tsv
```

Results: async/generator shape batch 30 / 30 passed; Object remaining 26 / 26
passed; Object toString 9 / 9 passed; regression follow-up 8 / 8 passed;
ownKeys/symbol 8 / 8 passed; arguments descriptor 5 / 5 passed; Object.assign
8 / 8 passed; Array flat/flatMap 6 / 6 passed.

Broad gate before the sparse Array callback fix:

```bash
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_after_async_generator_dynamic_full_failures.tsv
```

Result: 33,601 / 34,165 fully passing, 537 failing, 27 non-fully-passing, 252
improvements, and 3 reported regressions. The three regressions were timeout
rows in the sparse Array callback family and are covered by section 15.

## 15. Sparse Array Callback Timeout Pass

Status on 2026-05-16 after fixing the remaining Array callback timeout family:

- Fixed `Array.prototype.map`, `filter`, `forEach`, `some`, and `every` on
  sparse arrays with large dense hole ranges:
  - the shared iterative callback helper now jumps to the next present own
    index when the prototype chain has no numeric properties;
  - dense carriers are scanned directly for non-hole slots instead of doing a
    companion-map lookup for every hole;
  - companion-map numeric entries are still considered, so accessor/data index
    properties installed by `defineProperty` remain visible;
  - after each callback, the prototype chain is rechecked and iteration falls
    back to the sequential `HasProperty` path if user code adds numeric
    prototype properties.
- The older method-name path uses the same next-own-index helper for
  `forEach`, `some`, and `every`, keeping both dispatch paths aligned.

Focused gate:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_sparse_array_iter_regressions_batch.txt --write-failures=temp/js43_sparse_array_iter5_after_generic_fix_failures.tsv
```

Result: 5 / 5 passed. The timeout cases now run in roughly 22-28ms each in the
debug Test262 runner instead of hitting the 10s per-test cap.

Guard gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_flat_batch.txt --write-failures=temp/js43_array_flat_after_sparse_iter_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_object_assign_batch.txt --write-failures=temp/js43_object_assign_after_sparse_iter_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_regression_followup_batch.txt --write-failures=temp/js43_regression_followup_after_sparse_iter_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_async_generator_shape_batch.txt --write-failures=temp/js43_async_generator_shape_after_sparse_iter_failures.tsv
```

Results: Array flat/flatMap 6 / 6 passed; Object.assign 8 / 8 passed;
regression follow-up 8 / 8 passed; async/generator shape 30 / 30 passed.

Next recommended gate: rerun the full Test262 batch and update the baseline if
the three sparse Array timeout rows are gone from the regression list.

## 16. Full Test262 Baseline Update

Status on 2026-05-16 after rerunning the full js262 batch and updating the
baseline through the gated updater:

```bash
./test/test_js_test262_gtest.exe --batch-only --update-baseline --write-failures=temp/js43_update_baseline_failures.tsv
```

Result: 33,580 / 34,165 fully passing, 513 failing, 72 non-fully-passing, 251
improvements, and 0 regressions. The baseline gate passed with
`batch-lost=0`, `crash=0`, and the stable minimum satisfied.

Baseline artifacts:

- `test/js262/test262_baseline.txt` now records 33,580 fully passing tests.
- `test/js262/t262_partial.txt` now records 38 slow entries from this run.
- `temp/js43_update_baseline_failures.tsv` records 547 manifest rows, with
  summaries in `temp/js43_update_baseline_failures_by_feature.tsv` and
  `temp/js43_update_baseline_failures_by_path.tsv`.

Note: `test/js262` is a symlink to `../lambda-test/js262`, so the baseline
update command needs permission to write through that symlink target. A
non-elevated run can report "Baseline updated" even when the silent `fopen`
inside the runner cannot write the target.

## 17. Function Constructor And Proxy Shape Pass

Status on 2026-05-16 after retiring the remaining `built-ins/Function` rows
from `temp/js43_update_baseline_failures.tsv` and a small adjacent Proxy shape
set:

- Dynamic `Function` compilation now throws `SyntaxError` when the generated
  function source fails to parse, instead of returning `null` and letting the
  test body continue.
- Early errors now track class-private names in the enclosing class stack and
  reject unbound private identifiers in dynamic function bodies, covering
  `new Function("o.#f")`.
- MIR parameter inference no longer treats `+` as numeric evidence for unknown
  parameters. Unknown `param + param` now stays boxed and routes through
  `js_add`, preserving runtime string concatenation such as `"1" + 2`.
- Constructor object creation now reads `new.target.prototype`, not always the
  target function prototype. If a proxy is revoked during that prototype lookup
  and the result is not an object, it raises the required TypeError.
- Proxy constructor shape now matches the spec more closely:
  - `Proxy` remains constructable but has no own `prototype` property;
  - Proxy revocation functions have no own `prototype`, are not constructors,
    and `new revoke()` throws `TypeError`;
  - the native Test262 `isConstructor` helper recognizes callable proxies.

Focused gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_function_constructor_batch.txt --write-failures=temp/js43_function_constructor_after_proxy_realm_fix.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_proxy_shape_batch.txt --write-failures=temp/js43_proxy_shape_after_new_revoke_failures.tsv
```

Results: Function constructor shard 8 / 8 passed; Proxy shape shard 3 / 3
passed.

Guard gate:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_function_guard_batch.txt --write-failures=temp/js43_function_guard_after_proxy_shape_failures.tsv
```

Result: 10 / 10 passed across Function constructor syntax/runtime behavior,
addition semantics, proxy construct `newTarget` behavior, and private-name
class checks.

## 18. Reflect, Proxy Construct, And Object ToString Pass

Status on 2026-05-17 after fixing the next stale rows from
`temp/js43_update_baseline_failures.tsv`:

- `Object.prototype.toString` now brands proxies from the target's builtin
  tag (`Array`, `Function`, or `Object`) before applying `@@toStringTag`, and
  preserves the required fallback when a proxy is revoked during the tag lookup.
- `Reflect.apply` and `Reflect.construct` now use `CreateListFromArrayLike`
  semantics for array-like argument lists instead of requiring actual Arrays.
- `Reflect.construct(target, args)` lowering now defaults `newTarget` to
  `target`, matching the JS spec and avoiding a spurious null newTarget.
- Proxy `[[Construct]]` with missing, null, or undefined `construct` trap now
  forwards through the target's construct path, preserving nested proxy and
  `newTarget.prototype` behavior.
- Class-map constructors now honor an explicit `newTarget.prototype` during
  construction, including proxy targets and subclassed Array allocation.
- `Function.prototype.bind` now supports class-map constructors on the
  construct path, so bound class targets inside proxy construct forwarding
  remain constructable.
- The ordinary user-function construction path now preserves proxy
  `newTarget`s through `OrdinaryCreateFromConstructor`; a proxy revoked while
  resolving `newTarget.prototype` therefore raises the required `TypeError`.

Focused gates:

```bash
./lambda.exe js-test-batch --timeout=10 < temp/js43_debug_bound_proxy.proto
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_reflect_arraylike_batch.txt --write-failures=temp/js43_reflect_arraylike_after_proxy_newtarget_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_object_tostring_proxy_batch.txt --write-failures=temp/js43_object_tostring_proxy_after_proxy_newtarget_failures.tsv
```

Results: bound proxy construct protocol passed; Reflect/proxy construct shard
9 / 9 passed; Object toString/proxy construct shard 4 / 4 passed.

Guard gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_function_guard_batch.txt --write-failures=temp/js43_function_guard_after_proxy_newtarget_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_proxy_shape_batch.txt --write-failures=temp/js43_proxy_shape_after_proxy_newtarget_failures.tsv
```

Results: Function/proxy/private-name guard 10 / 10 passed; Proxy shape guard
3 / 3 passed.

Next recommended gate: rerun the full Test262 batch without updating the
baseline first, then update the baseline only if the regression list stays at
zero.

## 19. Proxy / Reflect Forwarding Follow-Up

Status on 2026-05-17 while reducing the remaining
`temp/js43_update_baseline_failures.tsv` rows:

- Added a focused Proxy/Reflect forwarding shard in
  `temp/js43_proxy_reflect_forwarding_batch.txt` covering 15 nested proxy,
  Reflect, string exotic, and receiver-forwarding cases.
- Proxy `construct`, `deleteProperty`, `get`, `has`, and Reflect
  `defineProperty` / `get` / `set` forwarding now pass the shard.
- `Reflect.get(target, key, receiver)` lowering and runtime dispatch now
  preserve the explicit receiver through nested proxy forwarding.
- Native Test262 `compareArray` now recognizes proxies to Arrays as
  array-like while still reading `length` and indexed values through ordinary
  property access.
- String wrapper exotic `length` / index delete and define-property rejection
  paths now reject consistently through Reflect/Proxy forwarding.
- Array custom prototype storage now writes directly to the array companion
  map, matching the storage read by `js_array_get_custom_proto`.
- `ValidateAndApplyPropertyDescriptor` now rechecks own descriptors when the
  fast own-property probe misses synthetic own properties such as function
  `prototype`.
- Proxy `setPrototypeOf` invariant validation now propagates abrupt completion
  from the target's `[[GetPrototypeOf]]` instead of continuing with a null
  fallback.
- Ordinary `[[Set]]` now delegates array holes and out-of-range integer-index
  writes through a proxy prototype's `set` trap, preserving the original
  receiver and avoiding unrelated `getPrototypeOf` trap probes.
- `__proto__` assignment now lets an immediate proxy prototype handle
  `[[Set]]` before the Annex B prototype-setter shortcut mutates the receiver's
  internal prototype.
- The typed-array-prototype numeric set shortcut no longer probes
  `[[GetOwnProperty]]` on proxy receivers before proxy `[[Set]]` forwarding,
  so missing `set` traps perform exactly one receiver `getOwnPropertyDescriptor`
  and one receiver `defineProperty` per assignment.

Focused gate:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_proxy_reflect_forwarding_batch.txt --write-failures=temp/js43_proxy_reflect_forwarding_after_set_fix_failures.tsv
```

Result: 15 / 15 passed.

Guard gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_proxy_shape_batch.txt --write-failures=temp/js43_proxy_shape_after_set_fix_seq_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_reflect_arraylike_batch.txt --write-failures=temp/js43_reflect_arraylike_after_set_fix_seq_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_function_guard_batch.txt --write-failures=temp/js43_function_guard_after_set_fix_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_protocol_fixed_batch.txt --write-failures=temp/js43_array_protocol_fixed_after_set_fix_seq_failures.tsv
```

Results: Proxy shape 3 / 3 passed; Reflect/proxy construct array-like 9 / 9
passed; Function/proxy/private-name guard 10 / 10 passed; Array protocol guard
7 / 7 passed.

Note: run these Test262 runner processes sequentially. Parallel invocations can
share the runner's temporary batch-result channel and report spurious
`results=52/N` lost rows.

Next step: rerun the full Test262 batch without updating the baseline first,
then update the baseline if the regression list remains empty.

## 20. Regression And Promise Capability Follow-Up

Status on 2026-05-17 while continuing the remaining js262 failures:

- Closed the latest focused regression rows from the broad proxy/set run:
  - `Reflect.get(target, key, receiver)` now rejects primitive and Symbol
    targets before attempting ordinary property access.
  - Array `length` descriptor queries now report the synthetic ES attributes:
    non-enumerable, non-configurable, and writable unless the companion
    non-writable marker is present.
  - TypedArray integer-indexed `[[Set]]` with an altered receiver now defines
    the receiver property directly instead of recursing through proxy `set`
    forwarding.
  - `eval` hashbang sources now bypass the expression wrapper and return
    JavaScript `undefined` for empty/comment-only completions.
  - Proxy `defineProperty` traps now receive public Symbol keys instead of the
    runtime's internal `__sym_N` storage names.
- Fixed constructor/newTarget edges:
  - nested bound constructor calls now compute the effective bound
    `new.target` before creating the receiver object, so `new C()` for
    `C = A.bind().bind()` uses `A.prototype`;
  - dynamic `super()` resolution now falls back to the lexical superclass when
    a custom `Reflect.construct` newTarget would otherwise derive
    `Function.prototype` as the parent constructor.
- Fixed built-in `@@toStringTag` descriptor gaps for `Promise.prototype` and
  `Reflect`: both are now non-writable and non-enumerable while remaining
  configurable.
- Fixed the Promise capability constructor protocol:
  - class-map and proxy constructors are now accepted as valid native
    `new.target` values;
  - `super()` into native constructors that require construction now routes
    through the construct path instead of ordinary call dispatch;
  - Promise construction applies `newTarget.prototype` to the created promise
    object;
  - `NewPromiseCapability` capture cells now start as `undefined` and throw if
    the executor tries to capture resolve/reject twice;
  - `Promise.prototype.then` now uses the promise species constructor and
    forwards the native reaction promise into the returned capability.

Focused gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_construct_newtarget_batch.txt --write-failures=temp/js43_construct_newtarget_after_fix_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_tostringtag_descriptor_batch.txt --write-failures=temp/js43_tostringtag_descriptor_after_fix_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_promise_capability_batch.txt --write-failures=temp/js43_promise_capability_after_then_species_failures.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_turn_guard_batch.txt --write-failures=temp/js43_turn_guard_failures.tsv
```

Results: construct/newTarget 2 / 2 passed; toStringTag descriptor 2 / 2
passed; Promise capability 13 / 13 passed; turn guard 27 / 27 passed.

Next recommended gate: rerun the full Test262 batch without updating the
baseline. If the regression list is clean, update the baseline in a separate
run.

## 21. Promise Generic Protocol And Array Slice Follow-Up

Status on 2026-05-17 while reducing the remaining rows from
`temp/js43_current_full_failures.tsv`:

- Fixed the remaining Promise protocol cluster from the current manifest:
  - Promise resolving functions, capability executors, and `finally` wrapper
    closures now expose anonymous built-in `name` metadata and are
    non-constructable.
  - Promise executors are called with JavaScript `undefined` as `this`.
  - `Promise.prototype.catch` and `Promise.prototype.finally` now use ordinary
    `Invoke(this, "then", ...)` behavior, including primitive ToObject lookup
    and poisoned/non-callable `then` handling.
  - `Promise.prototype.then/finally` now use the species constructor path and
    propagate constructor getter/type errors.
  - Promise combinators fetch `constructor.resolve` once before the empty
    iterable fast path, and call the captured resolve method for elements.
  - `Promise.resolve` now honors a native promise's observable `constructor`
    property before returning the original promise.
  - `Promise.withResolvers` is registered as a real static builtin and dynamic
    construction of no-constructor subclasses of `Promise` now creates a
    Promise-backed instance with the subclass prototype.
  - Removed the MIR direct-call shortcuts for `Promise.resolve/reject/all/race`
    / `any/allSettled/withResolvers`; those shortcuts skipped observable
    property lookup and the constructor protocol.
- Fixed the Array `slice` high-index/proxy cluster:
  - `Array.prototype.slice` on generic object/proxy receivers now uses a
    spec-style `ToLength`, relative index, `HasProperty`, `Get`, and
    `CreateDataProperty` loop.
  - The previous array-like materialization and full-length pre-validation no
    longer run for `slice`, so small slices near `2^53 - 1` do not throw
    `RangeError` or drop high-index properties.
  - Proxy-wrapped arrays now preserve `ArraySpeciesCreate` behavior for
    `slice`.

Focused gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_promise_remaining_batch.txt --write-failures=temp/js43_promise_remaining_after_sequential.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_promise_generic_batch.txt --write-failures=temp/js43_promise_generic_after_sequential.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_slice_batch.txt --write-failures=temp/js43_array_slice_after_generic.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_failures_batch.txt --write-failures=temp/js43_array_failures_after_generic_slice.tsv
```

Results: Promise remaining 5 / 5 passed; Promise generic 20 / 20 passed; Array
slice 3 / 3 passed. The old 25-row Array manifest now has 3 passes and 22
remaining failures, all in `reverse` high-index proxy ordering or the
`sort` precise side-effect cluster.

Important runner note: do not run multiple `test_js_test262_gtest` processes in
parallel. Each runner process reuses `temp/_t262_worker_0.manifest`, so parallel
batch-file invocations can clobber each other and report impossible
`results=20/5` lost rows.

## 22. Array Reverse/Delete And Generic Sort Follow-Up

Status on 2026-05-17 while continuing from the remaining Array rows:

- Fixed the `Array.prototype.reverse` high-index proxy trap-order failure:
  - `DeletePropertyOrThrow` now performs the receiver's actual `[[Delete]]`
    operation and throws only if it returns false.
  - This removes the observable pre-delete `getOwnPropertyDescriptor` probe on
    proxies; proxy delete invariants remain handled by the proxy delete path.
- Replaced the direct-slot `Array.prototype.sort` implementation with a
  generic spec-shaped path:
  - collect elements through captured `length`, `HasProperty`, and `Get`;
  - sort the collected list without writing to the receiver first, so comparator
    throws leave the array in the collected-only side-effect state;
  - call user comparators with JavaScript `undefined` as `this`;
  - write sorted values back through strict `Set`, then delete the trailing
    captured-index range through `DeletePropertyOrThrow`;
  - route plain array-like receivers and primitive wrappers through the same
    generic sort path instead of materializing and writing back a dense temp
    array.
- The previous 25-row Array manifest now passes completely.

Focused gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_reverse_batch.txt --write-failures=temp/js43_array_reverse_after_delete_or_throw.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_failures_batch.txt --write-failures=temp/js43_array_failures_after_generic_sort.tsv
```

Results: reverse focused 1 / 1 passed; old Array failures 25 / 25 passed.

Fresh full gate:

```bash
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_full_after_array_sort.tsv
```

Result: 33,700 / 34,127 fully passed, 425 failed, 2 non-fully-passing
batch-unstable/slow rows, and 0 regressions versus the 33,580-row baseline.
The full run reported 121 improvements. One RegExp generated-property test
recovered in isolated retry after a batch worker signal 11.

Current largest remaining clusters from
`temp/js43_full_after_array_sort.tsv`:

- `language/statements`: 137 failures, dominated by class/private fields,
  switch/try lexical scoping, and try/destructuring completion semantics.
- `built-ins/TypedArray`: 87 failures, especially `TypedArray.prototype.set`
  and BigInt/toLocaleString/join/sort edges.
- `language/expressions`: 57 failures, mostly class elements and class/private
  method paths.
- `built-ins/RegExp`: 54 failures, including lookbehind, named groups,
  backreferences, and Unicode/dotAll behavior.
- `built-ins/String`: 30 failures, including string wrapper descriptors,
  locale/case conversion, and replace evaluation order.

## 23. TypedArray Iterator Closeout And String Wrapper Follow-Up

Status on 2026-05-17 while continuing the remaining js262 failures:

- Closed the remaining TypedArray cluster from the latest focused manifest:
  - `%ArrayIteratorPrototype%.next` mutations are now observable by array
    iterators returned from `keys`, `values`, `entries`, and default array
    iteration.
  - `TypedArray` constructors and `TypedArray.from` now route array inputs
    through the iterator protocol when `@@iterator` is present, so sparse array
    holes become observable `undefined` values instead of dense-slot skips.
  - Iterator prototype caches are reset by the Test262 batch reset path, so a
    test that mutates an intrinsic iterator prototype cannot leak into the next
    file in the same runner process.
- Started reducing the next `built-ins/String` cluster:
  - primitive string indexed access now rejects non-canonical array indices
    such as `NaN`, `Infinity`, and `2^32 - 1`;
  - `new String(value)` now creates a String exotic wrapper with a
    non-writable, non-enumerable, non-configurable own `length`;
  - dynamic `new String/Number/Boolean` constructor dispatch now creates the
    correct wrapper object instead of a plain object with only the prototype;
  - `String(array)` now observes the array's `toString` method before falling
    back to the legacy join-shaped conversion.

Focused gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_typedarray_last4_batch.txt --write-failures=temp/js43_typedarray_last4_after_cache_reset.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_typedarray_failures_batch.txt --write-failures=temp/js43_typedarray_after_iterator_cache_reset.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_string_failures_batch.txt --write-failures=temp/js43_string_after_tostring_index.tsv
```

Results: TypedArray last-four 4 / 4 passed; full TypedArray focused manifest
89 / 89 passed; String focused manifest improved to 8 / 30 passed.

Guard gates after the String `ToString(Array)` change:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_failures_batch.txt --write-failures=temp/js43_array_failures_after_string_tostring_guard_seq.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_typedarray_failures_batch.txt --write-failures=temp/js43_typedarray_after_string_tostring_guard_seq.tsv
```

Results: old Array failures 25 / 25 passed; TypedArray focused manifest
89 / 89 passed.

Runner note: the guard commands above must be run sequentially. A parallel
attempt produced impossible `results=289/N` rows because the Test262 runner
shares a temporary worker result channel.

## 24. String Indexed Access, Replace Ordering, And Locale Compare Follow-Up

Status on 2026-05-17 while continuing the remaining js262 failures:

- Fixed the lingering primitive string indexed-access row:
  - the unknown-object integer fast path now routes strings through
    `js_string_get_int` instead of Lambda `item_at`, so out-of-range string
    indexes produce JavaScript `undefined` rather than Lambda `null`.
- Fixed `String.prototype.charAt/charCodeAt` position coercion order:
  - empty receivers now still coerce `pos` before returning `""`/`NaN`, matching
    the spec path where `ToInteger(pos)` can throw.
- Fixed `String.prototype.replace` replacement coercion order:
  - non-callable `replaceValue` is converted with `ToString` before no-match and
    empty-string exits;
  - RegExp replacement objects with an own `toString` now use ordinary object
    conversion instead of the internal `/source/flags` shortcut.
- Added the missing `String.prototype.localeCompare` dispatcher body:
  - compares the two strings after NFC normalization, which closes the focused
    wrapper/coercion rows and canonical-equivalence row.

Focused gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_string_failures_batch.txt --write-failures=temp/js43_string_after_locale_compare.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_failures_batch.txt --write-failures=temp/js43_array_after_string_locale_guard.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_typedarray_failures_batch.txt --write-failures=temp/js43_typedarray_after_string_locale_guard.tsv
```

Results: String focused manifest improved from 8 / 30 at the previous checkpoint
to 17 / 30 passed. Old Array failures remain 25 / 25 passed, and TypedArray
focused manifest remains 89 / 89 passed.

Remaining String rows in this manifest:

- one RegExp backreference/substitution case:
  `built_ins_String_prototype_replace_S15_5_4_11_A5_T1_js`;
- twelve Unicode case-mapping rows covering SpecialCasing expansions,
  supplementary-plane case pairs, and final-sigma context. The current
  `fn_lower`/`fn_upper` path is still ASCII-oriented, so this should be handled
  as a Unicode case-mapping kernel rather than a narrow per-test patch.

## 25. Unicode String Case-Mapping Follow-Up

Status on 2026-05-17 while continuing the remaining js262 failures:

- Replaced the JS `String.prototype.toLowerCase/toUpperCase` dispatch from the
  Lambda ASCII-oriented `fn_lower`/`fn_upper` helpers with a JS-specific Unicode
  case-mapping path.
- Added UTF-8 code point traversal for string case conversion:
  - simple Unicode case pairs now use `utf8proc_tolower/toupper`;
  - supplementary-plane case pairs now survive the conversion path;
  - invalid UTF-8 bytes are copied through unchanged.
- Added full-case expansion support using utf8proc metadata and decomposition:
  - expansion rows such as `ß -> SS`, ligatures, Armenian ligatures, and Greek
    decomposed upper-case mappings now go through the JS string mapper;
  - lower-case `İ -> i + combining dot` is handled without normalizing the whole
    surrounding string.
- Added the language-insensitive final-sigma context rule:
  - Greek capital sigma now lowercases to final sigma only when preceded by a
    cased character and not followed by a cased character after case-ignorable
    code points.

Focused gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_string_failures_batch.txt --write-failures=temp/js43_string_after_upper_iota_split.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_failures_batch.txt --write-failures=temp/js43_array_after_unicode_case_guard.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_typedarray_failures_batch.txt --write-failures=temp/js43_typedarray_after_unicode_case_guard.tsv
```

Results at the first Unicode checkpoint: String focused manifest improved from
17 / 30 at the previous checkpoint to 27 / 30 passed. Old Array failures
remained 25 / 25 passed, and TypedArray focused manifest remained 89 / 89
passed.

Follow-up on the same cluster:

- Added the missing Unicode SpecialCasing uppercase expansions that utf8proc's
  simple mapper and decomposition path do not expose directly:
  - `ß -> SS`;
  - Greek ypogegrammeni/prosgegrammeni full uppercase mappings, including the
    U+1F80..U+1FAF ranges and the U+1FB2/U+1FB3/U+1FB4/U+1FB7,
    U+1FC2/U+1FC3/U+1FC4/U+1FC7, and
    U+1FF2/U+1FF3/U+1FF4/U+1FF7 singletons.
- This closed the remaining two Unicode uppercase rows and left only the RegExp
  backreference/substitution case in the 30-row String manifest.

Focused gate:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_string_failures_batch.txt --write-failures=temp/js43_string_after_greek_upper_table.tsv
```

Result: String focused manifest improved to 29 / 30 passed.

## 26. RegExp Backreference Capture Selection Follow-Up

Status on 2026-05-17 while continuing the remaining js262 failures:

- Fixed the remaining `String.prototype.replace` row by repairing numeric
  backreference execution in the RE2 wrapper.
- The previous two-pass rewrite accepted the first widened RE2 match for
  patterns like `^(a+)\1*,\1+$`. RE2 could choose a shorter referenced capture
  than JavaScript's greedy backtracking semantics, so `$1` became `"a"` instead
  of the expected `"aaaaa"`.
- For regexes whose numeric backrefs all target the same group, the wrapper now
  enumerates candidate referenced captures from longest to shortest, checks the
  candidate against the original capture atom, literalizes both the referenced
  capture and its synthetic backref groups, then reruns the refined pattern.
  This keeps the fix at the RegExp semantic layer instead of special-casing
  String replacement.

Focused gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_string_failures_batch.txt --write-failures=temp/js43_string_after_backref_retry.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_failures_batch.txt --write-failures=temp/js43_array_after_backref_retry_guard.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_typedarray_failures_batch.txt --write-failures=temp/js43_typedarray_after_backref_retry_guard.tsv
```

Results: String focused manifest now passes completely at 30 / 30. Old Array
failures remain 25 / 25 passed, and TypedArray focused manifest remains 89 / 89
passed.

## 27. RegExp Decimal Backrefs, Indices, And Dot Atoms

Status on 2026-05-17 while continuing the remaining js262 failures:

- Fixed multi-digit numeric backreference scanning in the RE2 wrapper:
  - `\10` is now consumed as capture 10 when that capture exists, instead of
    being rewritten as `\1` followed by literal `0`;
  - the focused Sputnik rows `S15.10.2.11_A1_T8` and
    `S15.10.2.11_A1_T9` now pass.
- Fixed `RegExp.prototype.exec` visible index and `lastIndex` conversion for
  non-ASCII subjects:
  - JavaScript reports UTF-16 code-unit offsets regardless of the `u` flag;
  - non-`u` regexes now convert `lastIndex`, match end, and `result.index`
    through the same UTF-16 index helpers used by Unicode-mode regexes;
  - this closes `S15.10.2.7_A2_T1`, where a UTF-8 byte offset `9` was reported
    instead of code-unit offset `5`.
- Fixed non-`u` dot atom matching for supplementary-plane characters:
  - non-Unicode regex dot patterns now use the existing UTF-16-expanded subject
    path, so a supplementary-plane code point is two code units and is not
    matched by a single `.`;
  - both `dotall/with-dotall.js` and `dotall/without-dotall.js` now pass.

Focused gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_regexp_legacy_core_batch.txt --write-failures=temp/js43_regexp_legacy_core_after_dot_and_indices.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_regexp_dotall_batch.txt --write-failures=temp/js43_regexp_dotall_after_dot_and_indices.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_string_failures_batch.txt --write-failures=temp/js43_string_after_regexp_guard.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_failures_batch.txt --write-failures=temp/js43_array_after_regexp_guard.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_typedarray_failures_batch.txt --write-failures=temp/js43_typedarray_after_regexp_guard.tsv
```

Results: RegExp legacy core moved from 0 / 7 in the older full manifest to
3 / 7 in the focused batch. RegExp dotAll is 2 / 2. String remains 30 / 30,
old Array remains 25 / 25, and TypedArray remains 89 / 89.

Remaining rows in the RegExp legacy-core focused batch:

- `S15.10.2.5_A1_T4`: quantified capture reset semantics differ from RE2;
- `S15.10.2.8_A1_T2`, `S15.10.2.8_A2_T1`,
  `S15.10.2.8_A3_T15`: lookahead semantics still need a proper zero-width
  assertion/capture implementation instead of the current consume-and-trim
  rewrite.

## 28. RegExp Capture Result Ceiling And Positive Lookahead Commit

Status on 2026-05-17 while continuing the remaining js262 failures:

- Lifted the RegExp capture extraction ceiling from the old static `$1..$9`
  era limit to `JS_REGEX_MAX_GROUPS = 256`.
  - The runtime `exec`, `test`, `replace`, `match`, and `split` paths now size
    their temporary capture arrays from the same shared cap.
  - This closes the legacy stress row `S15.10.2.8_A3_T15`, which expects a
    result array with 200 nested captures instead of truncating near 16.
- Fixed leading positive lookahead with backreferences.
  - The wrapper had been absorbing `(?=Y)` as `(Y)` and then allowing the
    backreference retry pass to choose a shorter capture than JavaScript's
    committed first lookahead match.
  - Leading positive lookaheads that participate in numeric backreferences now
    get a post-filter requiring the absorbed assertion to equal the assertion's
    first anchored match at that position. If an unanchored candidate is
    rejected by that post-filter, the wrapper advances and retries the next
    candidate.
  - This closes `S15.10.2.8_A1_T2` without regressing the older passing
    positive-lookahead rows.

Focused gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_regexp_legacy_core_batch.txt --write-failures=temp/js43_regexp_legacy_core_after_capture_and_pos_lookahead.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_regexp_lookahead_guard_batch.txt --write-failures=temp/js43_regexp_lookahead_guard_after_assert_tight.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_string_failures_batch.txt --write-failures=temp/js43_string_after_lookahead_guard_sequential.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_failures_batch.txt --write-failures=temp/js43_array_after_lookahead_guard_sequential.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_typedarray_failures_batch.txt --write-failures=temp/js43_typedarray_after_lookahead_guard.tsv
```

Results: RegExp legacy core is now 5 / 7. The lookahead guard is 7 / 8, with
only `S15.10.2.8_A2_T1` still failing. String remains 30 / 30, old Array
remains 25 / 25, and TypedArray remains 89 / 89.

Remaining rows in the RegExp legacy-core focused batch:

- `S15.10.2.5_A1_T4`: quantified capture reset semantics still need JS
  last-iteration capture handling instead of RE2's retained prior optional
  capture;
- `S15.10.2.8_A2_T1`: negative lookahead still needs proper capture-slot
  preservation and assertion-local backreference evaluation.

## 29. RegExp Negative Lookahead And Repeated Capture Close-Out

Status on 2026-05-17 while continuing the remaining js262 failures:

- Fixed quantified repeated-capture reset semantics for the direct RE2 path.
  - RE2 retains optional child captures from earlier iterations of a repeated
    parent group, while JavaScript exposes the captures from the final
    iteration.
  - After a direct RE2 match, the runtime now parses the pattern's capture
    parentage and clears child captures that fall outside the repeated parent's
    final captured span.
  - This closes `S15.10.2.5_A1_T4`.
- Fixed negative lookahead capture-slot and assertion-local backreference
  handling in the wrapper.
  - Captures inside a negative lookahead now still reserve their original JS
    capture numbers even though the assertion body is erased from the widened
    RE2 pattern.
  - Assertion-local numeric backrefs are normalized before compiling the
    rejection pattern, and rejection can use a nested wrapper when the assertion
    itself needs JS-only regex features.
  - When a rejected marker occurs after a non-greedy prefix, the wrapper now
    retries later marker boundaries at the same overall match start instead of
    advancing to the next input position. This matches JS backtracking for
    `S15.10.2.8_A2_T1`.
- Tightened the earlier numeric-backref capture-selection optimization.
  - The longest-candidate retry is now skipped when the referenced capture
    contains nested captures, preserving nested group participation for legacy
    rows such as `S15.10.2.9_A1_T2` and `S15.10.2.9_A1_T3`.
- Restored plain `String.prototype.replace` coercion order after the full-run
  regression check exposed two baseline rows:
  - string-search conversion now precedes non-callable replacement conversion;
  - the RegExp replace path still converts non-callable replacement before
    matching, as required by the RegExp replacement algorithm.

Focused gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_regexp_legacy_core_batch.txt --write-failures=temp/js43_regexp_legacy_core_after_regression_fix.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_regexp_lookahead_guard_batch.txt --write-failures=temp/js43_regexp_lookahead_guard_after_regression_fix.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_string_failures_batch.txt --write-failures=temp/js43_string_after_regression_fix.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_failures_batch.txt --write-failures=temp/js43_array_after_regression_fix.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_typedarray_failures_batch.txt --write-failures=temp/js43_typedarray_after_regression_fix.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_regression_guard_batch.txt --write-failures=temp/js43_regression_guard_after_fix.tsv
```

Results: RegExp legacy core is now 7 / 7. The lookahead guard is 8 / 8.
String remains 30 / 30, old Array remains 25 / 25, TypedArray remains 89 / 89,
and the four-row regression guard is 4 / 4.

Full js262 gate:

```bash
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_full_after_regression_fix.tsv
```

Result: 33834 / 34127 fully passed, 292 failed, 8092 skipped, and 1
batch-unstable row (`built_ins_TypedArray_prototype_copyWithin_coerced_values_end_detached_js`)
that passed in isolated retry. Baseline regression check reports 0 regressions
and 255 improvements versus the 33580-entry baseline.

Remaining failure areas after this checkpoint:

- RegExp: lookbehind, named groups, nullable quantifier, and several older exec
  edge cases;
- Language/class semantics: private fields/methods, static blocks, class
  expressions, destructuring/let/global-code strictness, and statement coverage;
- Smaller clusters: Proxy, Symbol, generators, async, `globalThis`, `new.target`,
  and one U+180E whitespace row.

## 30. RegExp Exec/Test LastIndex Protocol Tail

Status on 2026-05-17 while continuing the remaining js262 failures:

- Fixed `RegExpBuiltinExec` lastIndex observation for non-global,
  non-sticky regexes.
  - The runtime now performs `Get(R, "lastIndex")` and numeric coercion before
    deciding whether the value is used for the match start.
  - Non-`g`/`y` regexes still search from zero and do not write `lastIndex`,
    matching the accessor-count tests.
- Fixed `ToLength(lastIndex)` handling for global/sticky regex execution.
  - Negative numeric `lastIndex` values are clamped to zero instead of being
    treated as an out-of-range failed match.
  - The same alignment was applied to `RegExp.prototype.test`.
- Fixed omitted-argument dispatch for regex instance `.exec()` / `.test()`.
  - Both the builtin switch path and the regex-instance method fast path now
    pass JS `undefined` for omitted arguments, so `exec()` matches against the
    string `"undefined"` rather than the internal null sentinel.
- Kept the change in the RegExp execution layer; no test-specific branches or
  special casing were added.

Focused gate:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_regexp_exec_tail_batch.txt --write-failures=temp/js43_regexp_exec_tail_after_test_tolength.tsv
```

Result: the RegExp exec/test tail is now 6 / 6, closing:

- `built_ins_RegExp_prototype_exec_failure_lastindex_access_js`
- `built_ins_RegExp_prototype_exec_success_lastindex_access_js`
- `built_ins_RegExp_prototype_exec_S15_10_6_2_A12_js`
- `built_ins_RegExp_prototype_exec_S15_10_6_2_A1_T16_js`
- `built_ins_RegExp_prototype_exec_S15_10_6_2_A5_T3_js`
- `built_ins_RegExp_prototype_test_S15_10_6_3_A1_T22_js`

Regression gates:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_regexp_legacy_core_batch.txt --write-failures=temp/js43_regexp_legacy_core_after_exec_tail.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_regexp_lookahead_guard_batch.txt --write-failures=temp/js43_regexp_lookahead_guard_after_exec_tail.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_string_failures_batch.txt --write-failures=temp/js43_string_after_exec_tail.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_failures_batch.txt --write-failures=temp/js43_array_after_exec_tail.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_typedarray_failures_batch.txt --write-failures=temp/js43_typedarray_after_exec_tail.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_regression_guard_batch.txt --write-failures=temp/js43_regression_guard_after_exec_tail.tsv
```

Results: RegExp legacy core remains 7 / 7, lookahead guard remains 8 / 8,
String remains 30 / 30, old Array remains 25 / 25, TypedArray remains 89 / 89,
and the four-row regression guard remains 4 / 4.

Full js262 gate:

```bash
./test/test_js_test262_gtest.exe --batch-only --write-failures=temp/js43_full_after_exec_tail.tsv
```

Result: 33839 / 34127 fully passed, 285 failed, 8092 skipped, and 3
non-fully-passing rows. The regression check reports 0 regressions and 261
improvements versus the 33580-entry baseline. Two rows recovered in isolated
Phase 4 retry after batch kills:

- `built_ins_Object_defineProperties_15_2_3_7_6_a_202_js`
- `built_ins_TypedArray_prototype_copyWithin_coerced_values_end_detached_js`

Updated remaining failure areas:

| Area | Failures |
|---|---:|
| `language/statements` | 134 |
| `language/expressions` | 57 |
| `built_ins/RegExp` | 36 |
| `language/global_code` | 15 |
| `language/function_code` | 13 |
| `language/literals` | 13 |
| `language/arguments_object` | 5 |
| `language/statementList` | 5 |
| `language/directive_prologue` | 3 |
| `language/types` | 3 |
| `language/destructuring` | 1 |
| `language/white_space` | 1 |

## 31. js262 Baseline Refresh After RegExp Exec Tail Fixes

Status on 2026-05-17 after rerunning the full js262 suite with baseline update:

```bash
./test/test_js_test262_gtest.exe --batch-only --update-baseline --write-failures=temp/js43_update_baseline_after_exec_tail.tsv
```

Result: the official updater accepted the run and rewrote
`test/js262/test262_baseline.txt` with 33839 fully passing tests.

- Fully passed: 33839 / 34127
- Failed: 285
- Skipped: 8092
- Non-fully-passing: 3
- Regression check: 0 regressions, 261 improvements versus the prior
  33580-entry baseline
- Failure manifest: `temp/js43_update_baseline_after_exec_tail.tsv`
- Failure summaries:
  - `temp/js43_update_baseline_after_exec_tail_by_path.tsv`
  - `temp/js43_update_baseline_after_exec_tail_by_feature.tsv`

The updated baseline header records:

- Total tests: 42219
- Batched: 34165
- Runtime: 656.0s total
- Phase timing: 653.8s batch execution, 3.4s retry-regressions

Remaining failures by path:

| Area | Failures |
|---|---:|
| `language/statements` | 134 |
| `language/expressions` | 57 |
| `built_ins/RegExp` | 36 |
| `language/global_code` | 15 |
| `language/function_code` | 13 |
| `language/literals` | 13 |
| `language/arguments_object` | 5 |
| `language/statementList` | 5 |
| `language/directive_prologue` | 3 |
| `language/types` | 3 |
| `language/destructuring` | 1 |
| `language/white_space` | 1 |

Remaining failures by feature are still dominated by class/private-field
semantics and the general language statement/expression buckets:

| Feature | Failures |
|---|---:|
| `(none)` | 125 |
| `class` | 73 |
| `class-methods-private` | 33 |
| `class-fields-public` | 17 |
| `regexp-lookbehind` | 17 |
| `class-static-methods-private` | 16 |
| `regexp-named-groups` | 15 |
| `generators` | 13 |
| `destructuring-binding` | 12 |
| `Proxy` | 10 |
| `let` | 10 |

## 32. Non-Fully-Passing Cleanup: TypedArray Sort Stability

Status on 2026-05-17 after targeting the 3 reported non-fully-passing
entries.

Root cause fixed:

- `built_ins_TypedArray_prototype_sort_stability_js` was still using a
  quadratic insertion-sort path for typed arrays.  The stability test sorts a
  large typed array and was passing semantically but exceeding the js262 slow
  threshold, keeping it in `t262_partial.txt` as `SLOW_3191`.
- Replaced the typed-array sort implementation with a stable merge sort over a
  temporary `Item` buffer and scratch buffer.  The comparator path is unchanged:
  custom compare functions, BigInt typed arrays, `NaN`, and detached-buffer
  checks still flow through the existing typed-array comparison and setter
  helpers.

Focused verification:

```bash
make release
make build-test
./test/test_js_test262_gtest.exe --batch-file=temp/js43_nonfull_sort_single.txt --write-failures=temp/js43_nonfull_sort_single_after.tsv
./test/test_js_test262_gtest.exe --batch-file=temp/js43_nonfull_sort_neighborhood.txt --write-failures=temp/js43_nonfull_sort_neighborhood_after.tsv
./test/test_js_test262_gtest.exe --batch-file=temp/js43_nonfull_copy_neighborhood.txt --write-failures=temp/js43_nonfull_copy_neighborhood_after.tsv
./test/test_js_test262_gtest.exe --batch-file=temp/js43_nonfull_two.txt --write-failures=temp/js43_nonfull_two_after.tsv
```

Focused results:

- `built_ins_TypedArray_prototype_sort_stability_js`: passed in 115ms as a
  singleton and 92ms inside its 50-test neighborhood.
- `built_ins_TypedArray_prototype_copyWithin_coerced_values_end_detached_js`:
  passed in its 50-test neighborhood and in the combined non-full batch.
- Combined non-full typed-array batch: 2 / 2 passed, with no failure manifest
  rows.

Attempted full partial/baseline refresh:

```bash
./test/test_js_test262_gtest.exe --batch-only --run-partial --update-baseline --write-failures=temp/js43_nonfull_after.tsv
```

The run confirmed the two typed-array target improvements but did not update
the baseline because one existing release/O0 generator test now fails the
regression gate:

- Fully passed: 33881 / 34165
- Failed: 282
- Skipped: 8054
- Non-fully-passing: 2
- Improvements: 44
- Regressions: 1
- Blocking regression:
  `language_statements_for_of_dstr_array_rest_nested_obj_yield_expr_js`
  (`crashed with signal 11`)

Follow-up investigation for the blocker:

- The `for_of` test crashes as a singleton through `js-test-batch
  --opt-level=0`, but passes as a normal `lambda.exe js` file.
- The minimized batch case also passes with `--mir-interp` and with
  `--opt-level=3`, and it passes in a debug `lambda.exe` build.
- This points at a release JIT/codegen issue in the batch source-protocol path
  for a generator that resumes through a nested destructuring `yield` and then
  performs a function call.  It is separate from the typed-array sort fix, but
  it must be fixed before the baseline updater will accept the new typed-array
  improvements.

## 33. Generator Array-Rest Destructuring Yield Crash

Status on 2026-05-17: fixed the release/O0 crash in
`language_statements_for_of_dstr_array_rest_nested_obj_yield_expr_js`.

Root cause:

- The source test has one explicit `yield`, but the MIR lowering for array rest
  destructuring emits the rest target twice: once for the collected-rest branch
  and once for the already-exhausted empty-array branch.
- `jm_count_yields()` already doubled yield counts for regular array-pattern
  elements because those lower into value and exhausted-iterator branches.  It
  did not apply the same branch-aware count to rest elements.
- The generator state machine therefore allocated only two states (implicit
  parameter-binding state plus one explicit yield) while codegen emitted a
  third yield site.  In release/O0 native JIT this malformed state machine could
  crash after resuming and reading an incremented local.

Fix:

- Updated `lambda/js/js_mir_analysis.cpp` so every non-elision array pattern
  element, including rest/spread elements, counts yield-containing initializers
  once per emitted branch.
- Reverted the temporary spill-frame size experiment; the crash was a state
  count mismatch, not generator env spill overflow.

Focused verification:

```bash
make -C build/premake config=release_native lambda -j4 CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib"
./lambda.exe js temp/js43_forof_use_incremented_after_dstr_yield.js --opt-level=0 --no-log
./lambda.exe js temp/js43_forof_call_inside_only.js --opt-level=0 --no-log
./lambda.exe js temp/js43_forof_call_min.js --opt-level=0 --no-log
./lambda.exe js temp/js43_forof_yield_min.js --opt-level=0 --no-log
./test/test_js_test262_gtest.exe '--gtest_filter=*language_statements_for_of_dstr_array_rest_nested_obj_yield_expr_js*' --gtest_print_time=0
```

Results:

- All reduced release/O0 repros now pass.
- The targeted js262 test passes through the gtest runner.
- The gtest runner also completed its batch collection phase without promoting
  any non-fully-passing tests under the default partial-skipping mode:
  fully passed target: 1 / 1, non-fully-passing: 0, regressions: 0.

## 34. Low-Cardinality Language Cleanup: U+180E and Directive Prologues

Status on 2026-05-17: fixed four remaining language failures from the latest
manifest:

- `language_white_space_mongolian_vowel_separator_eval_js`
- `language_directive_prologue_14_1_16_s_js`
- `language_directive_prologue_14_1_4_s_js`
- `language_directive_prologue_14_1_5_s_js`

Root causes:

- Tree-sitter accepts raw U+180E (`E1 A0 8E`) in source positions where the
  current ECMAScript grammar should reject it.  Eval then executed
  `var\u180Efoo;` as an identifier expression and threw `ReferenceError`
  instead of the required `SyntaxError`.
- Eval parse failures returned `ItemNull` without installing a `SyntaxError`,
  so parser-level eval failures were not observable by `assert.throws`.
- Strict directive detection used the cooked string literal value after AST
  building and skipped empty statements.  That incorrectly treated
  `'use str\ict'`, `'use\u0020strict'`, and `; 'use strict'` as strict-mode
  directives.

Fix:

- Added a source scan in `lambda/js/js_scope.cpp` that rejects raw U+180E in
  executable source text while still allowing it inside comments, strings,
  templates, and regular-expression literals.
- Made eval convert direct-script parse failure into `SyntaxError`.
- Moved strict directive detection to raw Tree-sitter source ranges and stored
  the result on `JsProgramNode` / `JsFunctionNode`.  Runtime strictness and
  early-error strictness now use that raw directive-prologue flag.
- Preserved U+1680 (`E1 9A 80`) as valid Unicode whitespace; it was only
  temporarily confused with U+180E during investigation.

Focused verification:

```bash
make -C build/premake config=release_native lambda -j4 CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib"
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_u180e_language_batch.txt --write-failures=temp/js43_u180e_language_after.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_directive_prologue_batch.txt --write-failures=temp/js43_directive_prologue_after.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_directive_prologue_guard_batch.txt --write-failures=temp/js43_directive_prologue_guard_after.tsv
```

Results:

- U+180E language neighborhood: 11 / 11 passed, including comments, strings,
  regex literals, template literals, and eval forms.
- Directive-prologue failures: 3 / 3 passed.
- Directive guard batch: 9 / 9 passed, including valid strict-directive
  representatives.

## 35. Destructuring/StatementList/Arguments Cleanup

Status on 2026-05-17: fixed eleven focused js262 failures across three small
language clusters.

Root causes:

- Object destructuring under `with` evaluated the source property before probing
  the binding target.  The spec-visible `HasBinding` trap for the target name
  therefore happened too late.
- Statement-position source beginning with `{` could reach lowering as an object
  expression even though ECMAScript forbids `{` at the start of an
  `ExpressionStatement`.  This miscompiled block/labeled statement completion
  cases and regexp-literal-looking statement-list tests.
- Arguments objects were backed by arrays, so `"length"` followed Array length
  semantics instead of arguments-object data-property semantics.  Redefining a
  mapped arguments index as an accessor also failed to snapshot the parameter
  value at ParameterMap unlink time.
- Strict direct eval used the single-expression fast path before applying the
  strict-mode early error for assigning to `arguments`/`eval`.

Fix:

- Added `js_probe_with_binding()` and emitted a pre-`GetV` probe for object
  binding destructuring targets when a `with` environment is active.
- Reinterpreted statement-position leading object expressions as block/labeled
  statements in AST construction.
- Materialized `arguments.length` on the arguments companion map, routed
  assignment/defineProperty/descriptor reads through that map, and taught the
  `.length` fast path to preserve non-numeric arguments length values.
- Snapshotted mapped arguments values before accessor/non-writable
  `defineProperty` unmaps the index.
- Added a strict direct-eval source scan so assignments to `arguments` or `eval`
  throw `SyntaxError` before the expression-wrapper fast path.

Focused verification:

```bash
make -C build/premake config=release_native lambda -j4 CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib"
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_current_focused_batch.txt --js-timeout=30 --write-failures=temp/js43_current_focused_after.tsv
```

Results:

- Focused cleanup batch: 11 / 11 passed.

## 36. Catch Parameters And Sparse Array Slow-List Cleanup

Status on 2026-05-18: fixed the next catch-parameter failures and retired two
array slow-list entries.

Root causes:

- Function/class collection skipped `catch` parameter binding patterns.  Default
  initializers inside catch destructuring patterns were therefore not
  pre-collected, so function/class name inference and class metadata could be
  missing during lowering.
- Strict-mode catch parameters were walked as expressions only.  A simple
  `catch (arguments)` or `catch (eval)` in strict eval source therefore bypassed
  reserved binding-name validation.
- The active generic `Array.prototype.indexOf` / `lastIndexOf` path performed a
  sequential `HasProperty` walk over every hole in large sparse arrays, even
  when the prototype chain had no numeric keys.  The later legacy array branch
  already had some sparse awareness, but it was not the path used by normal
  method dispatch.

Fix:

- Recurse into `JsCatchNode::param` during function/class collection.
- Run reserved binding-pattern validation on catch parameters before the normal
  expression walk in early errors.
- Added reverse sparse-own-element lookup and routed generic `indexOf` /
  `lastIndexOf` through sparse own-element iteration when the array prototype
  chain has no numeric keys or proxy prototypes.  This preserves observable
  `HasProperty` behavior when prototype numeric keys exist, while avoiding the
  all-hole scan for ordinary sparse arrays.
- Locally cleaned the generated partial list from 39 loaded entries to 23 by
  removing measured-fast URI entries and the two fixed sparse array search
  entries.

Focused verification:

```bash
make -C build/premake config=release_native lambda -j4 CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib"
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_try_dstr_fn_name_batch.txt --js-timeout=30 --write-failures=temp/js43_try_dstr_fn_name_after2.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_catch_restriction_batch.txt --js-timeout=30 --write-failures=temp/js43_catch_restriction_after2.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_sparse_array_search_batch.txt --js-timeout=30 --write-failures=temp/js43_sparse_array_search_after2.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_uri_slow_batch.txt --js-timeout=30 --write-failures=temp/js43_uri_slow_before.tsv
```

Results:

- Catch destructuring default/name-inference batch: 12 / 12 passed.
- Strict catch parameter restriction batch: 2 / 2 passed.
- Sparse array search batch: 2 / 2 passed; timing improved to 12 ms for
  `indexOf` and 14 ms for `lastIndexOf` after the generic-path fix.
- URI slow batch: 14 / 14 passed, each under 1 second in the focused batch.

## 37. Regression Cleanup After Partial-List Sweep

Status on 2026-05-18: fixed the reported full-suite regressions from the
partial-list cleanup pass and preserved the sparse-array speed work.

Root causes:

- The sparse `Array.prototype.indexOf` / `lastIndexOf` shortcut skipped holes
  while assuming the prototype chain would stay free of numeric keys.  That is
  not valid when an own indexed accessor runs during the scan and installs a
  numeric property on `Array.prototype`.
- Array iterator `.next()` used the backing array length for arguments objects.
  `arguments.length` is an observable data property in this runtime, so
  expansion and truncation before iterator exhaustion were ignored.
- Array iterator `.next()` read raw dense slots for values/entries.  That missed
  arguments overflow entries and other property-observable numeric values.
- Generator array-rest destructuring did not spill the outer iterator state
  before binding a rest target that could suspend through a nested
  destructuring default `yield`.  After resume, the stale MIR registers could be
  used by the close/exception path and crash.
- Object destructuring similarly needed to preserve its source register across
  yield-bearing nested targets so later properties/rest can continue safely.
- The `Function.prototype.toString` intrinsic-object walk still passes but is
  slow enough to remain a partial-list item under normal js262 timeouts.

Fix:

- Disabled the sparse array search shortcut for arrays with own numeric
  accessor descriptors.  Ordinary sparse data arrays still use the fast
  own-element scan when the prototype chain has no numeric keys.
- Added live observable length lookup for arguments-backed array iterators.
- Switched ArrayIterator values/entries to normal property reads rather than
  raw dense-slot reads.
- Added generator spill/restore around yield-bearing array rest targets and
  object destructuring targets.
- Added a dense identity-search fast path for `Array.prototype.includes`; it is
  guarded to arrays with no companion map, full dense capacity, and object-like
  search values, falling back to the existing property-observable path for
  holes and primitive SameValueZero cases.
- Restored the local partial list to include the two identifier Unicode slow
  tests plus the slow `Function.prototype.toString` intrinsic walk.

Focused verification:

```bash
make -C build/premake config=release_native lambda -j4 CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib"
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_search_regression_batch.txt --js-timeout=30 --write-failures=temp/js43_array_search_regression_after.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_remaining_regression_batch.txt --js-timeout=30 --write-failures=temp/js43_remaining_regression_after4.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js262_dstr_yield_focus.txt --js-timeout=30 --write-failures=temp/js43_dstr_yield_focus_after.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_array_includes_guard_batch.txt --js-timeout=30 --write-failures=temp/js43_array_includes_guard_after.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_function_to_string_slow_batch.txt --js-timeout=30 --write-failures=temp/js43_function_to_string_slow_after.tsv
```

Results:

- Array search regression batch: 7 / 7 passed, including five accessor/prototype
  mutation regressions and two sparse search sentinels.
- Remaining regression batch: 6 / 6 passed.  The ArrayIterator arguments cases
  and `language_expressions_assignment_dstr_array_rest_nested_obj_yield_expr_js`
  crash are fixed.
- Destructuring-yield focus batch: 11 / 11 passed.
- Array includes guard batch: 5 / 5 passed.
- Function `toString` slow representative: passes with a 30-second test timeout
  but remains slow at roughly 18-22 seconds in focused runs.

## 38. Private Field Assignment Tightening

Status on 2026-05-18: fixed the remaining `privatefieldset` rows from the
current 591-test remaining manifest and reduced that manifest from 190 to 184
failures.

Root causes:

- Runtime private field installation and ordinary private assignment both used
  `js_property_set`.  During class field initialization the runtime suppresses
  missing-private checks so real field adds can succeed; ordinary assignments
  like `this.#x = 1` inside an earlier public initializer were accidentally
  using the same relaxed path.
- The MIR Reference record did not remember when a member target was a private
  reference, so `PutValue` could not distinguish private assignment from normal
  object property writes.
- `for-in` member left-hand sides were evaluated into the loop temporary but
  were not written back through the Reference path in the for-in branch.
- Destructuring pre-reference evaluation for `this.#field` before `super()`
  resolved to a later TypeError instead of the required ReferenceError before
  the source getter was invoked.

Fix:

- Added `js_private_property_set` / `js_private_property_set_strict` as the
  ordinary-private-assignment path.  It requires an existing private field slot
  or private brand before delegating to the normal property setter, while class
  field installation continues to use the existing add path.
- Marked MIR references with `is_private` and routed private reference writes
  through the new checked runtime helper.
- Added immediate ReferenceError emission when a member reference evaluates
  `this` before `super()` in a derived constructor.
- Made for-in member targets use `jm_emit_reference` / `jm_emit_put_value`,
  matching the for-of member target path.
- Registered the new runtime helpers in `sys_func_registry.c` for MIR imports.

Focused verification:

```bash
make -C build/premake config=release_native lambda -j4 CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib"
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_private_runtime_batch.txt --js-timeout=30 --write-failures=temp/js43_private_runtime_after_private_set.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_privatefieldset_batch.txt --js-timeout=30 --write-failures=temp/js43_privatefieldset_after_private_set.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_current_remaining_batch_after_private.txt --js-timeout=30 --write-failures=temp/js43_current_remaining_after_private_set.tsv
```

Results:

- Private runtime guard batch: 7 / 7 passed.
- Private field set focused batch: 3 / 3 passed.
- Current remaining batch: 407 / 591 passed, 184 failed.  Previous comparable
  run was 401 / 591 passed, 190 failed.

Next class work:

- The remaining public-field derived-constructor rows require moving public
  field initializer emission from `new` construction into the `super()` binding
  path, so initializers run immediately after the first successful `super()`
  and are not rerun on a second `super()` attempt.

## 39. Derived Public Fields And Private Method Reinitialization

Status on 2026-05-18: fixed the next class-element slice from the 591-test
remaining manifest.  The public-field/super rows now pass, and the private
method duplicate-initialization rows are restored.

Root causes:

- Derived constructor `this` was visible to arrow closures during field
  initializers before `super()` had bound it.  The runtime needed a lexical
  `this` binding that can hold TDZ, while internal `super()` still reads the
  pending superclass receiver.
- Public instance fields were installed through assignment, so inherited
  setters and proxy-observable internal writes could run.  Public fields must
  use `CreateDataPropertyOrThrow` semantics.
- Derived own fields were emitted in the outer `new` path instead of after the
  first successful `super()` binding.  That made double-`super()` and
  superclass-returned-object cases observable in the wrong order.
- Private method brands installed on superclass-returned objects did not check
  whether the same private name was already present.  Getter/setter pairs also
  needed emitter-side de-duplication so the first construction still installs
  one shared private accessor brand.

Fix:

- Added TDZ-aware lexical `this` capture/resolution helpers and updated
  derived constructor call setup so explicit `super()` uses the pending
  superclass receiver while ordinary `this` remains TDZ until bound.
- Updated derived public-field initialization to use `js_create_data_property`
  and to run on the object returned by `super()`.
- Added `js_set_internal_class_name` so class-name stamping bypasses proxy
  traps when a superclass constructor returns a proxy.
- Added duplicate-brand detection to `js_private_brand_add` and de-duplicated
  private instance method brand emission by private name.

Focused verification:

```bash
make -C build/premake config=release_native lambda -j4 CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib"
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_public_field_super_batch.txt --js-timeout=30 --write-failures=temp/js43_public_field_super_after_brand_guard.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_private_method_double_init_batch.txt --js-timeout=30 --jobs=1 --write-failures=temp/js43_private_method_double_init_after_brand_guard_seq.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_current_remaining_batch_after_private.txt --js-timeout=30 --write-failures=temp/js43_current_remaining_after_private_brand_guard.tsv
```

Results:

- Public field/super batch: 4 / 4 passed.
- Private method duplicate-initialization batch: 4 / 4 passed.
- Current remaining batch: 412 / 591 passed, 179 failed.  Previous comparable
  run was 407 / 591 passed, 184 failed.

Next class work:

- The two remaining `grammar-field-named-{get,set}-followed-by-generator-asi`
  rows are parser/ASI issues: the JavaScript grammar still refuses to insert a
  class-field semicolon before a following generator `*` method.

## 40. Class Field ASI Before Generator Methods

Status on 2026-05-18: fixed the two remaining class-element ASI rows.

Root causes:

- The JavaScript scanner refused automatic semicolon insertion when the next
  token was `*`, which prevented a class field named `get` or `set` from ending
  before a following generator method.
- Non-static `get` / `set` field definitions needed a small precedence lift so
  `get\n*a(){}` parses as a field plus generator method rather than one broken
  accessor parse.
- The existing `static get\n...` grammar alias is needed for line-broken static
  getters, but it over-consumes the exact `static get\n*a(){}` ASI shape.  The
  AST builder now recognizes only that generator-ASI parse shape and splits it
  into a static field named `get` plus an instance generator method.

Verification:

```bash
make -B -C build/premake config=release_native lambda -j4 CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib"
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_class_get_set_generator_asi_batch.txt --js-timeout=30 --jobs=1 --write-failures=temp/js43_class_get_set_generator_asi_after_ast_split.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_current_remaining_batch_after_private.txt --js-timeout=30 --write-failures=temp/js43_current_remaining_after_ast_split.tsv
```

Results:

- Class `get` / `set` generator ASI batch: 2 / 2 passed.
- Current remaining batch: 414 / 591 passed, 177 failed.  Previous comparable
  run was 412 / 591 passed, 179 failed.

## 41. Class Constructor Metadata Ordering

Status on 2026-05-18: fixed the static `length` precedence slice and improved
the current remaining manifest to 173 failures.

Root causes:

- Class constructor `.length` and `.name` were emitted after static methods and
  accessors.  That overwrote `static length()` and invoked `static set length`
  while defining the class.
- The class constructor `prototype` property was also created too late for
  Test262's required own-property order.  The constructor now creates
  `length`, `name`, and `prototype` before static class elements run.
- Method installation used assignment semantics.  Static methods that replace
  configurable constructor intrinsics need define-property semantics, so method
  installation now uses `js_create_data_property` for data methods while
  accessors continue through the accessor installer.

Focused verification:

```bash
make -B -C build/premake config=release_native lambda -j4 CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib"
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_class_length_static_batch.txt --js-timeout=30 --jobs=1 --write-failures=temp/js43_class_length_static_after_reorder.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_class_definition_remaining_batch.txt --js-timeout=30 --jobs=1 --write-failures=temp/js43_class_definition_after_reorder.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_current_remaining_batch_after_private.txt --js-timeout=30 --write-failures=temp/js43_current_remaining_after_class_metadata.tsv
```

Results:

- Static `length` precedence batch: 2 / 2 passed.
- Class-definition focused batch: 3 / 12 passed.  New passes are the two static
  `length` rows plus `side-effects-in-property-define`.
- Current remaining batch: 418 / 591 passed, 173 failed.  Previous comparable
  run was 414 / 591 passed, 177 failed.

Next work:

- The remaining class-definition `fn-name-*` rows point at computed Symbol
  property-key `SetFunctionName` formatting for class methods/accessors.
- The larger remaining clusters are still RegExp lookbehind/named groups,
  lexical TDZ/scope behavior, global binding attributes, and legacy function
  activation semantics.

## 42. Class Definition Completion And Derived Super TDZ

Status on 2026-05-18: completed the focused `class/definition` slice and
improved the current 591-test remaining manifest from 418 / 591 passing to
428 / 591 passing.

Root causes:

- Computed Symbol class method/accessor keys installed the function before
  applying `SetFunctionName`, so Test262 saw missing or incorrectly prefixed
  names for `[sym](){}`, `get [sym](){}`, and `set [sym](v){}`.
- The P4 `this.prop` shaped-slot fast path treated static class
  methods/accessors as instance methods.  Static accessors like
  `static get x() { return this._x; }` read constructor instance-layout slots
  instead of class-object properties.
- Class heritage validation checked the superclass constructor but skipped the
  required validation of `superclass.prototype`.  Bound functions and accessors
  returning non-object prototype values therefore failed to throw the required
  TypeError.
- `new C()` used the compile-time class-entry fast path even when multiple
  lexical classes had the same name.  Test262's repeated local `class C extends
  Base { ... }; new C();` forms could run the wrong class metadata instead of
  the runtime lexical binding.
- The no-op `super(...args)` path for parent classes without explicit
  constructors skipped argument evaluation, so TDZ reads like `super(this)` and
  `super(Object.getPrototypeOf(this))` did not execute.
- Some super-property and builtin-call fast paths continued after
  `js_get_this()` had raised a pending ReferenceError, allowing a later TypeError
  to overwrite the expected error type.

Fix:

- Centralized method/accessor installation through
  `js_set_function_name_from_property_key_if_anonymous` before defining the
  method or accessor.
- Added `is_class_static_method` to collected function metadata and excluded
  static class methods/accessors from the instance shaped-slot `this.prop`
  optimization.
- Added `js_check_class_prototype_parent` and called it after resolving
  `superclass.prototype` for class declarations, expressions, and module-batch
  lowering.
- Added a duplicate-class-name guard to `new ClassName(...)`; ambiguous class
  names now use runtime `js_new_from_class_object` dispatch so block/function
  lexical bindings select the actual class object.
- Evaluated `super()` arguments even when the parent class has no explicit
  constructor, and added exception propagation after TDZ-sensitive `this`
  resolution in `super.method()` and `Object.getPrototypeOf(...)` fast paths.
- Generalized argument-array builders to propagate pending exceptions after
  argument evaluation.

Verification:

```bash
make -C build/premake config=release_native lambda -j4 CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib"
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_class_definition_remaining_batch.txt --js-timeout=30 --jobs=1 --write-failures=temp/js43_class_definition_after_super_tdz.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_current_remaining_batch_after_private.txt --js-timeout=30 --write-failures=temp/js43_current_remaining_after_class_definition_tdz.tsv
```

Results:

- Class-definition focused batch: 12 / 12 passed.
- Current remaining batch: 428 / 591 passed, 163 failed.  Previous comparable
  run was 418 / 591 passed, 173 failed.

Next work:

- The largest remaining block is still RegExp lookbehind/named groups.
- The non-RegExp failures are now concentrated around lexical/global binding
  semantics, class static blocks/subclass builtins, and older activation-object
  function semantics.

## 43. Static Blocks And Block Lexical Closure Capture

Status on 2026-05-18: fixed the focused static-block slice and the two
ordinary block lexical closure rows from the current remaining manifest.  The
current 591-test remaining manifest now reports 440 / 591 passing, leaving
151 failures.

Root causes:

- Static class elements were not emitted in exact source order across static
  fields and static blocks.  That broke static-block sequencing, `arguments`
  behavior inside static blocks, `new.target`, and `var` scoping around static
  blocks.
- Static element execution needed to run with `this` set to the class object,
  `new.target` cleared, and the superclass prototype already installed so
  static `super` property reads observe the right class state.
- Function bodies for closures over a block `let` that shadows a top-level
  module binding still treated the name as a live module variable.  The
  closure creator copied the correct block value into the per-closure env, but
  the compiled prologue skipped loading it because `force_env_capture` was only
  set for lexical `for` head captures.

Fix:

- Added source-order emission for static fields and static blocks, with static
  block lowering isolated through a class-static-block helper.
- Preserved and restored static-element `this` and `new.target`, and installed
  class prototype linkage before static elements execute.
- Marked captures as forced env captures when an ancestor lexical/function
  local shadows a same-named `MCONST_MODVAR`, so block lexical closures read the
  per-closure env instead of falling through to the module table.

Verification:

```bash
make -C build/premake config=release_native lambda -j4 CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib"
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_class_static_block_batch.txt --js-timeout=30 --jobs=1 --write-failures=temp/js43_class_static_block_after_cleanup_serial.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_lexical_scope_batch.txt --js-timeout=30 --jobs=1 --write-failures=temp/js43_lexical_scope_after_cleanup_serial.tsv
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_current_remaining_batch_after_private.txt --js-timeout=30 --write-failures=temp/js43_current_remaining_after_lexical_shadow.tsv
```

Results:

- Static-block focused batch: 8 / 8 passed.
- Block lexical closure batch: 2 / 2 passed.
- Current remaining batch: 440 / 591 passed, 151 failed.  The earlier
  comparable checkpoint in this doc was 428 / 591 passed, 163 failed.

Current top remaining path clusters:

| Area | Failures |
|---|---:|
| `language/statements` | 56 |
| `built_ins/RegExp` | 36 |
| `language/global_code` | 15 |
| `language/expressions` | 14 |
| `language/function_code` | 13 |
| `language/literals` | 12 |

Next work:

- RegExp lookbehind/named-groups remains the largest single cluster.
- The language side is now mostly switch/try lexical environment handling,
  global declaration attributes, subclass builtin edges, and legacy
  activation-object function behavior.

## 44. Switch CaseBlock, Strict Annex B, And Global Const TDZ

Status on 2026-05-18: reduced the 151-test remaining slice to 134 failures.
The focused switch lexical batch, strict global switch declarations, and strict
function block/switch declaration cases now pass.  A direct global const TDZ
read before declaration also now throws correctly.

Root causes:

- `switch` reused `loop_depth` for break-label tracking.  Closure creation used
  the same value to decide whether let/const captures were inside an iteration
  loop, so closures inside switch CaseBlocks copied the TDZ value instead of
  sharing the CaseBlock lexical environment.
- Switch CaseBlock lexical collection omitted function declarations.  Function
  declarations in switch cases therefore leaked through Annex B/module hoist
  paths and did not participate in lexical collision suppression.
- Strict scripts/functions still treated nested block/switch function
  declarations as Annex B function-scope hoists.
- Top-level `const` literal folding turned lexical bindings into immediate
  `MCONST_*` constants, bypassing the module-var TDZ slot for reads before the
  declaration.

Fix:

- Added a separate `iteration_depth` and increment it only for real iteration
  statements; `switch` and labeled break scopes continue to use `loop_depth`.
- Added switch CaseBlock function declarations to lexical-name collection and
  initialized switch-local function bindings in the CaseBlock environment.
- Suppressed nested function hoist registration for strict scripts/modules and
  strict function bodies, while preserving direct function-body declarations.
- Kept top-level `const` declarations as live module-var bindings instead of
  folding them into immediate constants.

Verification:

```bash
make -C build/premake config=release_native lambda -j4 CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib"
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_switch_scope_lex_batch.txt --js-timeout=30 --write-failures=temp/js43_switch_scope_lex_after_nested_class.tsv --gtest_brief=1
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_strict_switch_decl_batch.txt --js-timeout=30 --write-failures=temp/js43_strict_switch_decl_after.tsv --gtest_brief=1
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_strict_func_block_decl_batch.txt --js-timeout=30 --write-failures=temp/js43_strict_func_block_decl_after.tsv --gtest_brief=1
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_global_tdz_closure_batch.txt --js-timeout=30 --write-failures=temp/js43_global_tdz_closure_after.tsv --gtest_brief=1
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_current_remaining_after_switch_input.txt --js-timeout=30 --write-failures=temp/js43_current_remaining_after_const_tdz_fix.tsv --gtest_brief=1
```

Results:

- Switch lexical focused batch: 8 / 8 passed.
- Strict global switch declaration batch: 2 / 2 passed.
- Strict function block/switch declaration batch: 4 / 4 passed.
- Global TDZ focused batch: 1 / 5 passed; direct prior-statement const TDZ is
  fixed, closure get/set TDZ still needs capture/env work.
- Original 151-test slice: 17 / 151 passed, leaving 134 failures.  Earlier in
  this section it was 15 / 151 after the strict-function fix, and 12 / 151
  after the switch/global strict fixes.

Current top remaining path clusters:

| Area | Failures |
|---|---:|
| `language/statements` | 47 |
| `built_ins/RegExp` | 36 |
| `language/expressions` | 14 |
| `language/literals` | 12 |
| `language/global_code` | 11 |
| `language/function_code` | 10 |

Next work:

- Finish global/block/function lexical TDZ through closure envs.
- Then return to the RegExp lookbehind/named-groups cluster, which remains the
  largest standalone area.
