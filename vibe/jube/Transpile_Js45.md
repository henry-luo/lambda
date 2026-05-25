# Proposal: Robustly Fixing the Remaining test262 Failures (Subclassable Built-ins, Private-Element Timing, Release-only Crash, Slow URI)

**Date**: 2026-05-24
**Status**: in progress; P0-P3 landed, release baseline updated
**Author context**: follow-up to `Transpile_Js262.md` and `Transpile_Js_Regex_Backtrack.md`.
After the regex backtracker landed (17 RegExp improvements, baseline at 34148),
the residual `built-ins/*` and `language/*` failures are no longer regex-shaped.
This proposal analyses every remaining failed and partial-passing test in the
js262 suite and lays out a robust, root-cause fix plan — prioritising the
release-only crash and the batch-collateral noise it creates.

Snapshot used for this analysis: `temp/js262/release_run_001/`
(release `lambda.exe`, commit `a854e5ff9`).

## 0. Progress update (2026-05-25)

P0-P3 have now landed and the release js262 baseline has been refreshed.

Latest full run (`make release`, then full `--batch-only --update-baseline`
using the release `lambda.exe`):

- **Fully passed:** 34157 / 34161
- **Failed:** 4
- **Skipped:** 8058
- **Regressions vs baseline:** 0
- **Improvements vs previous 34148 baseline:** 9
- **Updated baseline:** `test/js262/test262_baseline.txt` now contains 34157
  passing entries.

Newly graduated tests:

- `language_expressions_class_elements_prod_private_setter_before_super_return_in_field_initializer_js`
- `language_statements_class_elements_privatefieldset_typeerror_1_js`
- `language_statements_class_elements_prod_private_setter_before_super_return_in_field_initializer_js`
- `language_statements_class_subclass_builtin_objects_ArrayBuffer_regular_subclassing_js`
- `language_statements_class_subclass_builtin_objects_Array_contructor_calls_super_multiple_arguments_js`
- `language_statements_class_subclass_builtin_objects_Boolean_regular_subclassing_js`
- `language_statements_class_subclass_builtin_objects_TypedArray_super_must_be_called_js`
- `language_statements_class_subclass_builtins_js`
- `language_statements_let_syntax_let_closure_inside_next_expression_js`

Remaining release-suite work:

- **4 hard failures** remain from the original P4 bucket: lexical
  `super`/`new.target`, class-expression name binding, eval scope propagation,
  and eval early-error handling.
- **2 `functional-replace` named-group entries** remain in
  `t262_partial.txt` as `CRASH_139`; the P3 focused regression passed these
  cases, so this is now a partial-list/harness reconciliation item rather than a
  known correctness failure.
- **2 slow URI tests** remain as `SLOW_*` partials.
- The full run still reports the known batch-worker signal 11 after collecting
  all 50 results for that batch; it produced no promoted failures and no
  regressions.

Operational note: `test/js262` is a symlink to `../../lambda-test/js262`, so
baseline writes may appear to succeed while sandboxed writes are denied. The
2026-05-25 baseline update was verified by checking the file header and the
34157 non-comment entry count after an escalated write.

## 1. Goal

Drive the remaining **15 hard failures + 2 genuine correctness bugs + 1
release-only crash + 2 slow tests** to green, by fixing root causes (not the
symptoms), and remove the harness noise (stale `CRASH_139` collateral) that
makes the non-fully-passing count fluctuate run-to-run.

- **Correctness from the spec operations**, not per-test patching.
- **Kill the crash first** — it corrupts whole batches and is the single
  biggest source of flaky non-fully-passing collateral.
- **No regressions** on the refreshed 34157-test baseline; validate
  `--batch-only` before each baseline update.

## 2. Complete failure inventory

### 2.1 Hard failures in the release run (`failures.tsv`, 15)

| # | Test | Kind | Symptom |
|---|------|------|---------|
| 1 | `String/.../replaceAll/searchValue-replacer-RegExp-call` | **CRASH** | SIGSEGV (release only) |
| 2 | `String/.../replaceAll/searchValue-replacer-RegExp-call-fn` | **CRASH** | SIGSEGV (release only) |
| 3 | `class/subclass/builtin-objects/ArrayBuffer/regular-subclassing` | runtime | `ArrayBuffer species is not a constructor` |
| 4 | `class/subclass/builtin-objects/Boolean/regular-subclassing` | runtime | `Boolean.prototype.valueOf requires that 'this' be a Boolean` |
| 5 | `class/subclass/builtin-objects/Array/contructor-calls-super-multiple-arguments` | assert | `[undefined, …]` instead of `[42,'foo']` |
| 6 | `class/subclass/builtin-objects/TypedArray/super-must-be-called` | runtime | `Constructor is not defined` |
| 7 | `class/subclass/builtins` | assert | `SameValue` mismatch |
| 8 | `class/elements/prod-private-setter-before-super-return-in-field-initializer` (stmt) | assert | `private setters are not installed before super returns` |
| 9 | `class/elements/...private-setter-before-super-return...` (expr) | assert | same |
| 10 | `class/elements/privatefieldset-typeerror-1` | assert | `Expected a TypeError to be thrown` (brand check) |
| 11 | `arrow-function/.../lexical-super-call-from-within-constructor` | assert | `Expected a ReferenceError` |
| 12 | `class/syntax/class-expression-binding-identifier-opt-class-element-list` | assert | inner class-name binding `object` vs `undefined` |
| 13 | `let/syntax/let-closure-inside-next-expression` | assert | per-iteration `let` in for **increment** expr |
| 14 | `tagged-template/.../cache-eval-inner-function` | runtime | `ReferenceError: a is not defined` (eval scope) |
| 15 | `statements/break/S12.8_A7` | parse | early-error: `break` inside `eval` in iteration |

### 2.2 Partial / non-fully-passing list (`t262_partial.txt`, deduped)

| Tag | Count | Tests | Reality (verified standalone, debug build) |
|-----|------:|-------|---------|
| `CRASH_139` | 2 | `replaceAll/searchValue-replacer-RegExp-call(-fn)` | **genuine, release-only crash** |
| `CRASH_139` | 2 | `RegExp/named-groups/functional-replace-{global,non-global}` | **not a crash — assertion FAIL** (wrong functional-replace args) |
| `CRASH_139` | 9 | `class/dstr/async-gen-meth-*` (6), `class/elements/privatefieldset-typeerror-{6,8}` (2), `expressions/yield/star-array` (1) | **stale collateral — all PASS standalone** |
| `SLOW_69xx` | 2 | `decodeURI(Component)/S15.1.3.{1,2}_A2.5_T1` | genuine slow (~6.4–6.9 s) |

**Key empirical finding.** Of the 13 `CRASH_139` entries, only **2 actually
crash** (the subclassed-`RegExp` `@@replace` pair, and only in the **release**
build — they pass under the debug/ASAN build). Two more are plain assertion
failures mis-tagged as crashes, and the remaining **nine pass standalone** —
they were only ever *batch-collateral* of the real crash and got tagged
`CRASH_139` (= "exited 139") by a previous run. This is why the non-fully-passing
count swung 12 → 3 → 0 across runs: it tracks whether the one real crash landed
in a batch with these innocent neighbours.

## 3. Root-cause analysis (by cluster)

### Cluster A — Subclassing exotic built-ins via `super()` (tests 3–7, plus the crash 1–2)

`class Sub extends Boolean {}` then `new Sub(1).valueOf()` must yield `true`;
`class Sub extends Array { constructor(a,b){ super(a,b); } }` must yield an
exotic Array `[a,b]`; `extends ArrayBuffer`/`TypedArray`/`RegExp` similarly.

The spec model: a *derived* class constructor does **not** allocate `this`
itself — `super(...args)` calls `Construct(baseCtor, args, newTarget=Sub)`, and
the **base built-in's own [[Construct]]** allocates an object that (a) carries
the built-in's internal slot (`[[BooleanData]]`, `[[ArrayBufferData]]`,
`[[TypedArrayName]]`, exotic-Array length tracking, RegExp `[[RegExpMatcher]]`),
and (b) gets its `[[Prototype]]` from `newTarget.prototype` (the subclass's
prototype) via `OrdinaryCreateFromConstructor`/`GetPrototypeFromConstructor`.

Our engine instead constructs derived instances as ordinary objects and invokes
the built-in constructor like a plain function on that object, so:
- the internal slot is never installed → brand checks in `valueOf`/`@@species`/
  TypedArray accessors throw (`requires 'this' be a Boolean`, `species is not a
  constructor`, `Constructor is not defined`);
- exotic Array element/length behaviour is absent → `super(42,'foo')` produces
  empty slots;
- for `RegExp`, the `@@replace` path dereferences a partially-initialised
  subclass instance and **crashes in release** (the bad read is masked by the
  debug allocator's layout, hence release-only).

Source seams: derived-construct / `super()` lowering in
`lambda/js/js_mir_function_class_lowering.cpp` and the constructor dispatch +
built-in constructors in `lambda/js/js_runtime.cpp` / `lambda/js/js_globals.cpp`;
the RegExp `@@replace` path in `js_runtime.cpp`.

### Cluster B — Private class elements: install timing & brand checks (tests 8–10)

Per `InitializeInstanceElements`, private methods/accessors are installed on the
instance **when `super()` returns**, before the derived class's field
initializers run; a private set on an object lacking the brand throws
`TypeError`. We currently install private methods at the wrong point (so a base
field initializer that triggers a derived private setter sees it not-yet-installed
or already-installed at the wrong time) and skip the brand `TypeError` on
foreign receivers.

Source seams: class-element lowering in
`lambda/js/js_mir_function_class_lowering.cpp` and private-slot access in
`lambda/js/js_props.cpp` / `js_mir_expression_lowering.cpp`.

### Cluster C — Lexical `super`/`new.target` and per-iteration `let` (tests 11–13)

- **11** arrow functions must capture `super`/`new.target` lexically; a
  `super()` with `new.target === undefined` must throw `ReferenceError`.
- **12** a class *expression*'s optional `BindingIdentifier` must be visible
  (immutable) inside the class body and absent outside.
- **13** `for (let i …; <next> …)` must run `CreatePerIterationEnvironment`
  around each iteration; closures created in the **next-expression** must observe
  the post-increment value. Body-closures already work (verified `0,1,2`); only
  the increment-expression copy is wrong (`0,1,2` vs `1,2,3`).

### Cluster D — `eval` scoping & early errors (tests 14–15)

- **14** a binding introduced via `eval` (and the tagged-template call-site
  cache) must be visible to a sibling inner function — our eval var-injection
  doesn't reach the enclosing function scope in this shape.
- **15** `break;` with no enclosing iteration/switch *inside an `eval`* is an
  **early SyntaxError**; we accept it. This is a parse-time early-error rule.

Source seams: `lambda/js/build_js_ast.cpp` (early errors) and the eval scope
handling in the MIR lowering / `js_runtime.cpp`.

### Cluster E — `functional-replace` named-group argument (2 partial)

`String.prototype.replace(re, fn)` must append the `groups` object as the **last**
argument to `fn` when the pattern has named groups (spec @@replace step 14.k.iv).
We pass the wrong arity (`SameValue(0,2)`), i.e. the named-captures object is not
appended / captures are miscounted.

Source seam: `@@replace` functional path in `lambda/js/js_runtime.cpp`.

### Cluster F — Slow `decodeURI`/`decodeURIComponent` (2 partial)

`S15.1.3.{1,2}_A2.5_T1` run a 4-deep loop (~1.3 M iterations) each doing a
`decodeURIComponent` of a 4-byte percent sequence plus string building and
`decimalToHexString`. ~6.4–6.9 s in release — over the 3 s "fully-passing"
threshold. Pure throughput, no correctness issue.

## 4. Proposed fixes (priority order)

### P0 — Kill the release-only crash  ✅ DONE (2026-05-24)

This was first because a single crash corrupts a 50-test batch and produces the
fluctuating non-fully-passing collateral (§2.2).

**Actual root cause (found via a release+ASAN build, not what was hypothesised):**
it is **not** subclass-`RegExp`-specific. The minimal repro is a nested for-of
`const` array destructuring whose body allocates (`for (const [[a,b],c,d] of
samples) new RegExp(a,b)`). During destructuring/for-of, `IteratorClose` runs on
the built-in **fast Array/String iterators** (`MAP_KIND_ITERATOR`), whose Map
`type` is a **1-byte sentinel marker**, not a real `TypeMap`. `js_iterator_close`
→ `js_is_generator` → `js_property_get` then treats that sentinel as a `TypeMap*`
and reads `type->shape` out of bounds (ASAN: `global-buffer-overflow` on
`js_string_iter_marker`, `js_map_get_fast`/`js_find_shape_entry`). O0/debug masks
it via different layout; `-O3` faults.

**Fix (all in `js_runtime.cpp`):** treat synthetic `MAP_KIND_ITERATOR` maps as
having no shape/proto/own-properties at every entry point that walks a Map shape:
`js_map_get_fast` (own lookup), `js_get_prototype` (proto chain),
`js_is_generator` (the actual trigger), and `js_iterator_close` (built-in
iterators have no `return` method → spec no-op). Verified under release+ASAN:
both `replaceAll … RegExp-call(-fn)` tests now pass (exit 0, no ASAN error).

This is a general fix: the same fast-iterator-close path is exercised by all the
destructuring-heavy `CRASH_139` tests, so it is expected to clear most of that
collateral cluster too (to be confirmed in the final release run).

Tooling note: ASAN is normally only applied to test exes, and the release flags
are hardcoded in `utils/generate_premake.py` (with `-flto`, `-Wl,-x`). A
**release+ASAN, no-LTO, no-strip** build was needed to symbolise the fault — this
confirms the proposal's P6 recommendation to add a permanent release-ASAN lane.

### P1 — Subclassable built-ins: route `super()` through the base [[Construct]]  ✅ DONE (2026-05-24)

All 7 target tests pass; the full `class/subclass/builtin-objects` family (70
tests) passes with 0 regressions. Implemented across four seams:

- **`js_super_call_native`** (explicit `super()`): added `Boolean`/`String`/`Array`
  to the construct-via-base-[[Construct]] predicate; typed-array `super(args)` now
  constructs the backing with super's args (was reusing a pre-allocated empty
  `this`, dropping the length).
- **`js_new_from_class_object`**: the `Array`, typed-array and `ArrayBuffer`
  branches now apply the subclass prototype (`GetPrototypeFromConstructor`); a new
  branch runs the implicit `constructor(...args){ super(...args); }` via the base
  `[[Construct]]` for default-constructor subclasses.
- **Static class lowering** (`js_mir_statement_lowering.cpp`): stopped stamping
  `__class_name__` on built-in-wrapper instances — that property-add transitioned
  the Map shape and **dropped the `js_class` brand tag**, breaking
  `Boolean/String/Number.prototype.valueOf`.
- **`super()` lowering** (`js_mir_expression_lowering.cpp`): resolve the
  superclass from the class object's stored `__super_class__` (captured at
  definition time) instead of re-evaluating the identifier in the constructor
  scope — fixes `class T extends C` where `C` is a captured param/local
  (`testWithTypedArrayConstructors(Constructor => class extends Constructor)`).
- **`ArrayBuffer.prototype.slice` `@@species`** (`js_typed_array.cpp`): accept a
  subclass class-object (a constructable MAP with `__instance_proto__`) as the
  species constructor, not only a `FUNC`.

Original design notes follow.



Implement the derived-class allocation model properly:
- In the derived-constructor lowering, **do not pre-allocate `this`**. Make
  `super(args)` call the base constructor's real **[[Construct]]** with
  `newTarget` = the running subclass, so the base built-in allocates the exotic
  object (internal slot + `GetPrototypeFromConstructor(newTarget)` prototype),
  then bind that as `this`.
- Give the built-in constructors (`Boolean`, `Number`, `String`, `Array`,
  `ArrayBuffer`, `%TypedArray%`/typed arrays, `RegExp`, `Error`, …) a
  newTarget-aware construct entry that installs the internal slot and sets the
  prototype from `newTarget.prototype`.
- This single change fixes tests 3–7 and removes the brand-check failures; it is
  also the structural basis for the P0 crash fix.

### P2 — Private-element install timing & brand `TypeError` (Cluster B)  ✅ DONE (2026-05-24)

Target tests now pass: `prod-private-setter-before-super-return-in-field-initializer`
(stmt + expr) and `privatefieldset-typeerror-1`. The pre-existing
`class C { #a=10; b=this.#a*2 }` → `undefined` bug is also fixed (now `20`).

**Root cause:** the brand-check bypass flag `js_private_field_initializing` did
*double duty* — it was on for the whole field-init phase and was checked both to
(a) allow a field's own declaration to add itself, and (b) mark functions for the
field-initializer eval early-error context. Because it stayed on during
initializer-*expression* evaluation, a private set inside an initializer
(`this.#x = 1` before `#x` is declared) silently succeeded instead of throwing.

**Fix (the split):** introduce a second, narrowly-scoped flag
`js_private_define_active` that is on *only* while a field's declaration is being
added:
- New `js_private_field_define(obj, key, val)` sets the flag, writes the field,
  restores it; the field-init codegen uses it for the declaration set.
- The four private-write brand checks in `js_runtime.cpp` now bypass on
  `js_private_define_active` instead of `js_private_field_initializing`.
- `js_init_class_instance_fields` (the `__if_*` metadata path, which installs only
  literal/undefined declarations, no initializer expressions) sets the new flag
  for its whole walk.
- `js_private_field_init_begin/end` are kept around the field-init loops, so
  `js_private_field_initializing` still marks the eval early-error context.

Net effect: initializer expressions run brand-checked (referencing a
not-yet-installed private throws), while each field's own declaration still adds
itself. Validated: **6601/6601** `class/*` baseline tests pass, 0 regressions
(this also recovered 20 `class_elements_arrow_body_*eval*` early-error tests that
a first, naive attempt had broken).

Note: a separate **pre-existing** issue remains out of scope here — a *derived*
class with an explicit constructor (`class D extends B { #p; constructor(){
super(); this.#p = … } }`) installs `#p` only after the whole ctor body rather
than right after `super()` returns, so `this.#p = …` in the body throws. It
predates this work (fails with these changes stashed) and is its own fix.

### P3 — `functional-replace` named groups + per-iteration `let` increment (Clusters E, C-13)  ✅ DONE (2026-05-24)

Target tests now pass:
`built_ins_RegExp_named_groups_functional_replace_global_js`,
`built_ins_RegExp_named_groups_functional_replace_non_global_js`, and
`language_statements_let_syntax_let_closure_inside_next_expression_js`.

**Functional replace:** `String.prototype.replace` now routes regex-backed
receivers through the built-in `@@replace` path when there is no callable custom
`Symbol.replace`, so the existing named-captures plumbing appends the `groups`
object for function replacers. The string method call lowering also reads
callback-captured env changes back after synchronous string methods.

**Per-iteration for-`let` update:** closure creation in an update expression now
preserves the update-expression closure env long enough to reload the lexical
loop binding after the increment and patch the matching capture. This makes
closures created in the `next` expression observe the copied, post-increment
binding without broadening the general loop-let closure fallback.

Validated with a 7-test focused regression slice covering the two new
functional-replace cases, existing `RegExp.prototype[@@replace]` named-group
cases, and nearby `for (let ...)` closure cases: **7/7 passed**.

### P4 — Lexical super/new.target, class-expression name binding, eval scope, early errors (Clusters C-11/12, D)

Remaining correctness bucket after the 2026-05-25 baseline update: these are
the 4 hard failures still outside the 34157-test baseline.

- `ReferenceError` on `super()` with undefined `new.target`; lexical
  `super`/`new.target` in arrows (test 11).
- Bind a class-expression's optional name as an immutable binding inside the
  body (test 12).
- Propagate `eval`-introduced bindings to the enclosing function scope so
  sibling inner functions resolve them (test 14).
- Parse-time early SyntaxError for unlabeled `break`/`continue` with no valid
  target inside `eval` (test 15).

### P5 — `decodeURI`/`decodeURIComponent` throughput (Cluster F)

Profile the A2.5_T1 hot path; the likely wins are reducing per-iteration string
allocation in `decodeURIComponent` and the harness's `decimalToHexString`/string
concatenation. Target < 3 s in release so they graduate to fully-passing. If
they remain inherently slow, keep them as `SLOW_*` partials (they already pass
correctly) rather than forcing them.

### P6 — Harness hygiene (cross-cutting)

- **Partial list reconciliation remains.** After P3, the two
  `RegExp/named-groups/functional-replace-*` tests pass in the focused
  regression slice but are still present in `t262_partial.txt` as `CRASH_139`.
  They should be re-run through the partial path and removed from the partial
  file once confirmed in that mode.
- **Deduplicate slow entries.** The current `t262_partial.txt` preserves the two
  slow URI entries twice around the functional-replace crash entries. This is a
  harness hygiene issue, not a compliance regression.
- **Distinguish genuine crashers from collateral.** A test should only be
  written to `t262_partial.txt` as `CRASH_*` if it crashes **standalone**
  (Phase 2b/individual retry), not merely because it shared a batch with a
  crash-point. The 9 stale `CRASH_139` entries (§2.2) all pass standalone and
  should be removed once P0 lands.
- **Baseline/partial inconsistency fixed for the release-only crashers.** The 2
  `replaceAll` crashers now pass in release and are in the refreshed baseline.
- **Add a release-ASAN CI lane** to catch optimisation-sensitive memory UB (the
  exact class of bug that hid here) before it reaches the baseline.

## 5. Incremental plan (validate 0 regressions each step)

1. **P0-P3 complete.** The refreshed release baseline is 34157 passing tests
   with 0 regressions.
2. **P6 partial-list cleanup.** Confirm the two functional-replace partials in
   the partial path, remove them from `t262_partial.txt`, and dedupe the slow URI
   entries.
3. **P4** super/new.target, class-expr name, eval scope, early error (4 tests).
4. **P5** decodeURI perf (2 tests) — optional graduation.
5. Full `--batch-only` regression run → `--update-baseline` only if the gate
   stays at 0 regressions / 0 promoted non-fully-passing tests.

Each step: run the targeted tests with `sta.js assert.js compareArray.js
propertyHelper.js`, then a focused class/RegExp/String batch, then a full release
batch.

## 6. Risks

- **Subclass refactor breadth (P1).** Changing the derived-construct/`super()`
  path touches all `new`/`extends` flows. Mitigation: keep the ordinary-class
  path unchanged; gate the new base-[[Construct]] routing on "base is a built-in
  exotic constructor"; the 70 existing `subclass/builtin-objects` tests (most
  already pass) form a tight regression net.
- **Release-only bugs.** The crash proves `-O2` UB can pass debug+ASAN. The
  release-ASAN lane (P6) is the mitigation; without it, fixes can't be verified.
- **Private-element ordering.** Subtle interactions with field initializers and
  `super`; mitigate with the generated `class/elements/*` and `class/dstr/*`
  families as the regression net.
- **decodeURI perf.** May be interpreter-wide overhead, not local; treat as
  best-effort and keep as `SLOW_*` if needed rather than risk correctness.

## 7. Decisions (proposed)

1. **Crash first.** P0 + P6 land together; nothing else is reliable until the
   batch-collateral noise is gone.
2. **Root-cause the construct model.** Fix subclassing via the spec's
   base-[[Construct]] + `newTarget` model (P1) rather than per-built-in shims.
3. **Release-ASAN is mandatory tooling**, not optional — added as a CI lane.
4. **Slow tests are not failures.** Perf-tune `decodeURI` best-effort; do not
   distort correctness or the harness thresholds to force them green.
5. **Partial list reflects standalone reality** — only standalone crashers are
   tagged `CRASH_*`; collateral is never persisted.

## 8. Estimated size

- P0 crash fix: ~50–150 LOC + tooling (release-asan target) + 1 gtest.
- P1 subclassable built-ins: ~400–700 LOC across class lowering + built-in
  constructors (the bulk).
- P2 private elements: ~150–300 LOC.
- P3–P4: ~300–500 LOC combined.
- P5: profiling + targeted optimisation, size TBD.

No new third-party dependencies.
