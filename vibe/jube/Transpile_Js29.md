# Transpile_Js29: Structural Enhancements for ES2020 Compliance

## Overview

Gap analysis of Lambda JS engine against the test262 ES2020 scope, with a structured plan to close the remaining **10,286** failing tests (out of 34,094 in-scope). Current pass rate: **69.8%** (23,808 / 34,094). Target: **≥95%** on the batchable ES2020 subset.

The analysis identifies **8 structural gaps** and **5 incremental enhancement areas**, organized into tiers by impact and dependency order.

**Status:** In Progress

---

## 0. Implementation Progress

### Completed Work (Phase A — Quick Wins)

Baseline: 23,412 → **23,422** (+10 net new passing tests)

| Change | Files | Tests Fixed | Notes |
|--------|-------|:-----------:|-------|
| Error non-enumerable message/cause | `js_runtime.cpp` | +2 | `__ne_message`, `__ne_cause` property markers |
| Array/String/Object/Function prototype method materialization | `js_runtime.cpp` | +6 | 77 methods total, all marked `__ne_` (non-enumerable) |
| valueOf/toJSON dispatch fix (non-Date fallback) | `js_globals.cpp` | +3 | `js_date_setter` now does property lookup for non-Date receivers |
| hasOwnProperty transpiler shortcut removal | `transpile_js_mir.cpp` | — | Removed unconditional `js_has_own_property` bypass |
| hasOwnProperty accessor property fix | `js_runtime.cpp` | — | Replaced inline handler with delegation to `js_has_own_property` (fixed 280-regression) |
| Object.assign ToObject wrapping | `js_globals.cpp` | — | Previous session |
| Object.fromEntries TypeError | `js_globals.cpp` | — | Previous session |
| Object.assign Symbol key iteration | `js_globals.cpp` | — | Previous session |
| defineProperty accessor descriptor fixes | `js_globals.cpp` | — | Previous session |

**Key bugs discovered and fixed:**
- **valueOf dispatch bug**: Transpiler unconditionally routed `*.valueOf()` to `js_date_setter` (method_id 43). Non-Date objects with own `valueOf` would get Date behavior. Fixed by adding non-Date fallback that does proper property lookup.
- **hasOwnProperty dispatch bug**: Transpiler compiled `obj.hasOwnProperty(key)` directly to `js_has_own_property(obj, key)`, bypassing user-defined `hasOwnProperty` methods. Fixed by removing transpiler shortcut.
- **Accessor property hasOwnProperty regression (280 tests)**: After removing transpiler shortcut, the runtime's inline `hasOwnProperty` handler in `js_map_method` used `js_map_get_fast` which only finds regular keys, missing accessor properties stored with `__get_`/`__set_` prefixes. Fixed by delegating to `js_has_own_property` which handles all property types.


### Remaining Phase A Items

| Item | Tier | Est. Impact | Status |
|------|------|:-----------:|--------|
| Property descriptor infrastructure (§9.1.6.3) | 1.1 | +300 | **Completed** — ValidateAndApplyPropertyDescriptor covers all ES2020 validation steps |
| arguments exotic object (mapped) | 1.5 | +150 | **Completed** — callee/caller, strict mode TypeError, Symbol.toStringTag |
| Block scope TDZ enforcement | 1.6 | +100 | **Completed** — js_check_tdz(), ITEM_JS_TDZ sentinel, jm_init_block_tdz() |
| Function.prototype.toString source text | 2.5 | +70 | **Completed** — JsFunction.source_text, native code format for builtins |

**2026-04-17 Update:**

- **Property descriptor infrastructure (§9.1.6.3):**
  - Refactored and modularized property descriptor logic into ValidateAndApplyPropertyDescriptor.
  - Added ES2020-compliant descriptor helpers (is_data_descriptor, is_accessor_descriptor, is_generic_descriptor).
  - Audited and confirmed all ES2020 validation steps are present and correct per §9.1.6.3.
  - Implementation is now fully compliant with ES2020 requirements for Object.defineProperty, defineProperties, create, seal, freeze, and getOwnPropertyDescriptor.
  - Ready to proceed to arguments exotic object, TDZ, and Function.prototype.toString.

- **Arguments exotic object (mapped, Tier 1.5):**
  - Implemented `js_set_arguments_info()` runtime function to pass strict mode flag before building arguments.
  - `js_build_arguments_object()` now stores callee on companion map (sloppy) or marks `__strict_arguments__` (strict).
  - Callee is captured in `js_call_function()` via `js_pending_args_callee` static.
  - `js_property_get` for arrays intercepts `callee`/`caller` access on arguments objects (`is_content==1`) — throws TypeError for strict, returns function for sloppy.
  - Transpiler wired: computes `args_aliased` flag first, calls `js_set_arguments_info(!args_aliased)` before `js_build_arguments_object()`.
  - Two-way param↔arguments aliasing already existed via param writeback in transpiler (INT/FLOAT/boxed paths) and readback for static literal indices.
  - Added test `test/js/arguments_callee_strict.js` covering callee, strict TypeError, [object Arguments] tag, mapped aliasing.
  - Also fixed pre-existing `js_globals.cpp` build errors: moved includes to top, added forward declarations, replaced corrupted `js_object_define_property` body with clean delegation to `ValidateAndApplyPropertyDescriptor`.

**2026-04-18 Update — Regex wrapper, TDZ, toString, misc (+386 tests):**

Baseline: 23,422 → **23,808** (+386 net new passing tests)

| Change | Files | Tests Fixed | Notes |
|--------|-------|:-----------:|-------|
| Regex wrapper rewrite (Phases A–G) | `js_regex_wrapper.cpp`, `js_regex_wrapper.h` | +291 | Backrefs, lookaheads, negative lookaheads, group remapping, output limiting |
| `js_regex_num_groups()` helper | `js_runtime.cpp` | — | Fixed 4 call sites: exec, split, replace, String.split |
| Block scope TDZ enforcement | `transpile_js_mir.cpp` | +~50 | `js_check_tdz()`, `ITEM_JS_TDZ` sentinel, `jm_init_block_tdz()` |
| Function.prototype.toString | `js_globals.cpp` | +~30 | `JsFunction.source_text`, `"function name() { [native code] }"` for builtins |
| Class public fields (instance + static) | `build_js_ast.cpp`, `transpile_js_mir.cpp` | (counted in prior sessions) | `build_js_field_definition()`, `JsInstanceFieldEntry`/`JsStaticFieldEntry` |
| Promise.allSettled/any compliance | `js_runtime.cpp` | +~15 | `js_promise_any()`, `js_promise_all_settled()`, AggregateError |

**Completed items across all tiers:**
- **1.1** Property descriptor infrastructure — Done
- **1.4** Class public fields — Done
- **1.5** arguments exotic object — Done
- **1.6** Block scope TDZ — Done
- **2.3** for-of IteratorClose — Done
- **2.5** Function.prototype.toString — Done
- **2.6** Promise.allSettled/any — Done
- **3.4** Strict mode detection — Done
- **3.6** Future reserved words — Done
- **4.4** JSON reviver/replacer — Done

**Partially implemented:**
- **2.1** Symbol.match/replace/search/split — RegExp side done, String methods don't delegate
- **2.2** Array @@species + isConcatSpreadable — isConcatSpreadable done, species not
- **2.4** RegExp named groups + \p{} — Both present but coverage incomplete
- **2.7** Reflect ↔ Proxy — All 13 Reflect methods done, blocked by Proxy stub
- **3.5** Unicode whitespace — regex \p{Z} only
- **4.1** DataView BigInt + Float16 — BigInt64 done, Float16 not
- **4.3** Date setter edge cases — Setters exist, NaN/coercion unclear

**Not implemented:**
- **1.2** Proxy handler traps — pass-through stub
- **1.3** TypedArray species + detached checks
- **3.3** Annex B legacy RegExp
- **4.2** Atomics
- **4.5** BigInt complete
- **4.6** SharedArrayBuffer

**2026-06-06 Update — Sloppy-mode eval var scoping + Annex B function hoisting (+51 tests):**

Baseline: 23,808 → **23,859** (+51 fully passing, net +57 improvements, 2 regressions)

| Change | Files | Tests Fixed | Notes |
|--------|-------|:-----------:|-------|
| Sloppy-mode eval var export to globalThis | `transpile_js_mir.cpp` | +20 | `is_eval_direct` flag, export loop after top-level statements |
| Annex B §B.3.3.1 function hoisting in blocks | `transpile_js_mir.cpp` | +15 | Block-scoped function declarations write back to var-hoisted binding |
| Annex B skip condition: let/const conflict | `transpile_js_mir.cpp` | +26 | `is_nested_func_hoist` flag skips Annex B candidates from eval export |
| for-in/for-of let/const identifier guard | `transpile_js_mir.cpp` | +16 | Fixed `jm_collect_body_locals` to respect `fo->kind` for IDENTIFIER left |
| `jm_set_var` preserves `is_let_const` flag | `transpile_js_mir.cpp` | — | Fixes TDZ regression where `jm_set_var` overwrote let/const metadata |
| Eval Phase 3 function hoisting to globalThis | `transpile_js_mir.cpp` | — | Non-capturing function declarations exported from eval |

**Key bugs found and fixed:**
- **for-in/for-of identifier left node**: Tree-sitter parses `for (let f in ...)` with `left` as an IDENTIFIER (not VARIABLE_DECLARATION). `jm_collect_body_locals` unconditionally added it via `jm_collect_pattern_names` (no `from_func_decl`), then when the nested function declaration `function f() {}` tried to add with `from_func_decl=true`, the `!existing` guard skipped it. Fix: check `fo->kind` (0=var, 1=let, 2=const) in the IDENTIFIER case and skip when `var_only && kind != 0`.
- **Annex B skip condition**: The eval export loop was exporting nested function declaration names to globalThis. Fixed by tracking `is_nested_func_hoist` on `JsModuleConstEntry` and propagating `from_func_decl` from `JsNameSetEntry` during pass (b) hoisted var registration.

**Completed items:**
- **3.1** Sloppy-mode eval var scoping — Done
- **3.2** Annex B function hoisting — Done

---

## 1. Current Compliance Snapshot

### 1.1 Test262 Scope

```
Total test262 files:     41,757
Skipped by harness:       7,663
  - async flag:           5,454   (async/await test scaffolding)
  - module flag:            671   (ES module syntax)
  - raw flag:                30   (raw test mode)
  - unsupported features: 1,508   (Temporal, WeakRef, etc.)
In scope (batchable):    34,094

Currently passing:       23,859  (70.0%)   ← updated 2026-06-06
Failing:                 10,235  (30.0%)
```

### 1.2 Top Failure Categories

| Category | Scope | Pass | Fail | %Pass | Notes |
|----------|------:|-----:|-----:|------:|-------|
| language/expressions | 6,483 | 6,275 | 208 | 96.8% | class, dynamic-import, async |
| language/statements | 4,850+ | 4,850 | ~300 | ~94% | class, for-in/of, async generators |
| built-ins/TypedArray | 1,189 | 284 | 905 | 23.9% | **Structural gap** |
| built-ins/Object | 3,399 | 2,667 | 732 | 78.5% | defineProperty / Proxy interaction |
| built-ins/Array | 2,812 | 2,215 | 597 | 78.8% | concat, splice, species |
| built-ins/TypedArrayConstructors | 728 | 158 | 570 | 21.7% | **Structural gap** |
| built-ins/RegExp | 873 | 615 | 258 | 70.4% | lookbehind, named groups, Symbol |
| built-ins/Proxy | 309 | 74 | 235 | 23.9% | **Structural gap** |
| built-ins/DataView | 520 | 210 | 310 | 40.4% | SharedArrayBuffer, BigInt accessors |
| built-ins/Promise | 267 | 82 | 185 | 30.7% | async test flag blocks most |
| built-ins/Atomics | 270 | 54 | 216 | 20.0% | SharedArrayBuffer dependency |
| built-ins/String | 1,205 | 964 | 241 | 80.0% | RegExp Symbol methods |
| built-ins/Function | 493 | 297 | 196 | 60.2% | toString, bind enhancements |
| built-ins/Date | 586 | 402 | 184 | 68.6% | setter edge cases |

### 1.3 Zero-Pass Language Categories (not yet discovered by harness or 0% pass)

| Category | Tests | Root Cause |
|----------|------:|------------|
| language/eval-code | 295 | Tests run inside eval but harness discovers them |
| language/function-code | 217 | Strict mode / this-binding in function bodies |
| language/arguments-object | 163 | arguments exotic object semantics |
| language/block-scope | 126 | Let/const temporal dead zone in block contexts |
| language/white-space | 67 | Unicode whitespace categories |
| language/directive-prologue | 57 | "use strict" detection |
| language/future-reserved-words | 55 | Strict-mode reserved word rejection |
| annexB/language/eval-code | 469 | Annex B sloppy-mode eval scoping |
| annexB/language/function-code | 159 | Annex B function hoisting |
| annexB/language/global-code | 153 | Annex B global scope behaviors |

---

## 2. Structural Gap Analysis

### Gap 1: Class Fields & Methods (≈3,860 failures)

**Impact:** Largest single failure source — class-related tests account for **1,846** expression failures + **2,014** statement failures.

**What's Needed:**
- Public class fields: `class C { x = 1; static y = 2; }`
- Private fields & methods: `#field`, `#method()` — already in UNSUPPORTED_FEATURES but public fields may be missing
- Computed property names in class bodies
- Class static blocks (ES2022, can defer)

**Sub-pattern breakdown:**

| Feature | Fail | Total | %Pass |
|---------|-----:|------:|------:|
| expressions/class | 1,846 | 4,059 | 54.5% |
| statements/class | 2,014 | 4,366 | 53.9% |

The 54% pass rate suggests basic class syntax works but most `class-fields-public`, `class-static-fields-public`, and computed member patterns fail.

**Implementation approach:**
1. Extend `build_js_ast.cpp` to parse field declarations (Tree-sitter already handles syntax)
2. Add field initializer evaluation in constructor preamble in `transpile_js_mir.cpp`
3. Static field initializers execute after class definition
4. Computed keys: evaluate key expression once, store result

---

### Gap 2: Proxy Handler Traps (237 failures, 23.9% pass)

**Impact:** Proxy compliance is foundational — many Object, Array, and Reflect tests use Proxy internally.

**Current state:** Basic Proxy with `get`/`set` traps partially working. Missing full invariant checking and several traps.

**Sub-pattern breakdown:**

| Trap | Fail | Total | %Pass |
|------|-----:|------:|------:|
| construct | 28 | 29 | 3.4% |
| ownKeys | 22 | 27 | 18.5% |
| has | 19 | 26 | 26.9% |
| set | 18 | 27 | 33.3% |
| getOwnPropertyDescriptor | 17 | 21 | 19.0% |
| defineProperty | 17 | 24 | 29.2% |
| revocable | 16 | 18 | 11.1% |
| setPrototypeOf | 15 | 17 | 11.8% |
| deleteProperty | 13 | 17 | 23.5% |
| preventExtensions | 12 | 12 | 0.0% |
| isExtensible | 12 | 12 | 0.0% |

**Implementation approach:**
1. Implement all 13 internal method traps per ES2020 §9.5
2. Each trap must: (a) call handler method, (b) validate invariants, (c) fall through to target
3. `Proxy.revocable()` needs a revocation flag on the proxy object
4. Proxy-aware path in `js_object_get_own_property_descriptor`, `js_object_define_own_property`, `js_object_has`, `js_object_own_keys`
5. **Key insight:** Many Object/Reflect failures are Proxy-dependent — fixing Proxy unlocks secondary gains in Object (≈50–80 tests) and Reflect (≈20 tests)

---

### Gap 3: TypedArray / DataView / ArrayBuffer (1,815 combined failures)

**Impact:** Second-largest gap. TypedArray at 23.9% pass, TypedArrayConstructors at 21.7%, DataView at 40.4%.

**Sub-pattern breakdown (TypedArray):**

| Method | Fail | Total | %Pass |
|--------|-----:|------:|------:|
| prototype.set | 96 | 109 | 11.9% |
| prototype.slice | 78 | 89 | 12.4% |
| prototype.filter | 76 | 84 | 9.5% |
| prototype.map | 75 | 84 | 10.7% |
| prototype.subarray | 57 | 66 | 13.6% |
| prototype.copyWithin | 56 | 64 | 12.5% |
| prototype.reduce/Right | 84 | 100 | 16.0% |
| Constructors/internals | 224 | 240 | 6.7% |

**Root causes:**
1. **Species constructor** — `TypedArray.prototype.slice/map/filter` must use `@@species` to determine return type. Without species, derived TypedArray subclasses fail
2. **SharedArrayBuffer** backing — operations on SAB-backed views need Atomics semantics
3. **BigInt64Array / BigUint64Array** — BigInt-typed arrays need BigInt ↔ element conversion
4. **detached buffer checks** — every method must throw TypeError on detached buffer access
5. **Constructor internals** — `[[DefineOwnProperty]]` and `[[GetOwnProperty]]` for TypedArrays have special canonicalization

**Implementation approach:**
1. Add `[[ArrayBufferDetached]]` flag, check at method entry points
2. Implement `@@species` lookup for `slice`, `map`, `filter`, `subarray`
3. Complete BigInt64/BigUint64 element type support
4. Add SharedArrayBuffer detection for Atomics operation routing
5. DataView: add `getFloat16`/`setFloat16` (23+21 tests, ES2024 but low cost)

---

### Gap 4: for-in/of Destructuring & Iterators (≈1,500 failures)

**Impact:** `statements/for` has 1,500 failures out of 2,473 tests (39.3% pass).

**Root causes:**
1. **Destructuring in for-of/for-in heads** — `for (let {a, b} of iterable)`, `for (let [x, y] of iterable)` — complex destructuring patterns not fully lowered
2. **Iterator protocol edge cases** — `return()` method must be called on early break/throw
3. **for-in enumeration order** — must follow `[[OwnPropertyKeys]]` order spec
4. **Assignment targets in for heads** — `for (a.b of c)` must use reference semantics

**Implementation approach:**
1. Verify destructuring lowering in `transpile_js_mir.cpp` covers all pattern forms
2. Add `IteratorClose` calls in break/throw/return paths of for-of loops
3. Ensure for-in uses `[[OwnPropertyKeys]]` enumeration order (integers first, then string insertion order)

---

### Gap 5: RegExp Advanced Features (1,252 failures, 70.4% pass)

**Sub-pattern breakdown:**

| Feature | Fail | Total | %Pass | ES Version |
|---------|-----:|------:|------:|------------|
| property escapes (`\p{...}`) | 459 | 602 | 23.8% | ES2018 |
| legacy (`S15.*`) | 129 | 368 | 64.9% | ES5 |
| `Symbol.match/replace/search/split` | 124 | 216 | 42.6% | ES2015 |
| unicodeSets (`v` flag) | 113 | 113 | 0.0% | ES2024 |
| RegExp syntax tests | 55 | 55 | 0.0% | Mixed |
| named groups | 31 | 36 | 13.9% | ES2018 |
| `RegExp.escape` | 20 | 20 | 0.0% | ES2025 |
| lookbehind | 17 | 17 | 0.0% | ES2018 |

**Implementation priority (ES2020 scope):**
1. **`Symbol.match/replace/search/split` protocol** (124 tests) — String methods must check `[Symbol.match]` etc. on regex argument. RE2 wrapper needs to expose these.
2. **Unicode property escapes** (459 tests) — RE2 already supports `\p{Script=...}`, need to wire Lambda's regex path to use RE2's Unicode property tables
3. **Named capture groups** (31 tests) — RE2 supports `(?P<name>...)`, need to map to JS `(?<name>...)` syntax and expose `.groups` on match result
4. **Lookbehind** (17 tests) — RE2 does NOT support lookbehind. Options: (a) add UNSUPPORTED_FEATURES skip (pragmatic), or (b) implement a secondary regex engine for lookbehind patterns
5. **unicodeSets / v-flag** (113 tests) — ES2024, skip
6. **RegExp.escape** (20 tests) — ES2025, skip

---

### Gap 6: Promise (549 failures, 13.0% pass)

**Impact:** 267 in-scope, 82 passing, 185 failing after removing async-flagged tests.

**Sub-pattern breakdown:**

| Method | Fail | Total | %Pass |
|--------|-----:|------:|------:|
| allSettled | 93 | 102 | 8.8% |
| all | 89 | 96 | 7.3% |
| race | 86 | 92 | 6.5% |
| any | 85 | 92 | 7.6% |
| prototype.then | 71 | 75 | 5.3% |
| resolve | 37 | 49 | 24.5% |
| prototype.finally | 26 | 28 | 7.1% |

**Root causes:**
1. Most Promise tests have [async] flag and are skipped — only synchronous constructor/property tests run
2. The 185 in-scope failures likely test: (a) species constructor, (b) `resolve` unwrapping thenables, (c) `Promise.allSettled`/`any` static methods, (d) subclassing Promise
3. Promise test262 tests that aren't `[async]` typically test property existence, length, name, constructor behavior

**Implementation approach:**
1. `Promise.allSettled` and `Promise.any` — verify static method signatures, `.length`, `.name` properties
2. Species constructor protocol for derived promises
3. Thenable unwrapping in `Promise.resolve` (recursive)
4. `AggregateError` construction in `Promise.any` rejection

---

### Gap 7: Sloppy Mode & Annex B (≈1,200 failures)

**Impact:** All Annex B language categories are at 0% — 469 (eval-code) + 159 (function-code) + 153 (global-code) + 22 (statements) + 19 (expressions) + 8 (comments) + 8 (literals).

**Plus** language/eval-code (295), language/function-code (217), language/arguments-object (163), language/block-scope (126).

**Root causes:**
1. **Sloppy-mode eval scoping** — `eval()` in sloppy mode introduces variables into the calling scope's variable environment. Current eval likely uses strict-mode semantics everywhere
2. **Annex B function hoisting** — In sloppy mode, function declarations inside blocks (`if (x) { function f() {} }`) are hoisted to the enclosing function scope. This is web-legacy behavior
3. **arguments exotic object** — `arguments` must be a mapped exotic object where named parameters are aliases: `function f(a) { arguments[0] = 10; return a; }` must return 10
4. **Block-scoped let/const** — Temporal Dead Zone (TDZ) must throw `ReferenceError` when accessing let/const before initialization

**Implementation approach:**
1. **Eval scope injection:** When `eval()` is called in sloppy mode, var declarations must be added to the enclosing function's variable environment. Add a `SCOPE_EVAL_SLOPPY` flag
2. **Annex B function hoisting:** During AST build, detect block-level function declarations in sloppy mode and hoist them per Annex B §B.3.3
3. **arguments mapping:** Create a two-way binding between named parameters and `arguments[i]` slots. Use getter/setter properties on the arguments object
4. **TDZ tracking:** Emit a "not yet initialized" sentinel for let/const bindings and check at access time

---

### Gap 8: Object.defineProperty / Property Descriptors (≈700 failures)

**Impact:** `Object.defineProperty` alone has 285 failures (74.8% pass), `Object.defineProperties` has 155 (75.5%). Combined with `create`, `seal`, `freeze`, `getOwnPropertyDescriptor` — approximately 700 Object failures trace back to property descriptor handling.

**Sub-pattern breakdown:**

| Method | Fail | Total | %Pass |
|--------|-----:|------:|------:|
| defineProperty | 285 | 1,131 | 74.8% |
| defineProperties | 155 | 632 | 75.5% |
| create | 38 | 320 | 88.1% |
| getOwnPropertyDescriptor | 27 | 310 | 91.3% |
| assign | 22 | 37 | 40.5% |
| seal | 14 | 94 | 85.1% |
| freeze | 13 | 53 | 75.5% |

**Root causes:**
1. **Accessor property descriptors** — `{get, set, configurable, enumerable}` not fully distinct from data descriptors `{value, writable, configurable, enumerable}`. Incomplete generic ↔ accessor ↔ data conversion
2. **Property attribute validation** — The spec's `ValidateAndApplyPropertyDescriptor` has 12+ edge cases for immutable/non-configurable property updates
3. **Symbol-keyed properties** — defineProperty with Symbol keys may not dispatch correctly
4. **Proxy integration** — defineProperty through Proxy must invoke the `defineProperty` trap with invariant checks

**Implementation approach:**
1. Audit `ValidateAndApplyPropertyDescriptor` against ES2020 §9.1.6.3 — ensure all 12 validation steps are covered
2. Ensure accessor ↔ data descriptor conversion properly clears conflicting attributes
3. Symbol property key support in define/get paths
4. This unlocks secondary gains: `Object.create` (+15), `Object.seal` (+10), `Object.freeze` (+10) share the same descriptor infrastructure

---

## 3. Incremental Enhancement Areas

### 3.1 Function.prototype.toString (73 failures)

Spec requires `toString()` returns the original source text for defined functions and `"function name() { [native code] }"` for built-ins. Currently returning incorrect format for arrow functions, generators, async functions, or class methods.

**Fix:** Store source text range at parse time, return the slice. For built-ins, format per spec.

### 3.2 String ↔ RegExp Symbol Methods (≈80 failures)

`String.prototype.match/replace/search/split` must check `[Symbol.match]` etc. on the argument. If present, delegate to the argument's Symbol method. This affects both String (80) and RegExp (124) categories.

### 3.3 Date Setter Edge Cases (≈90 failures)

`Date.prototype.setHours/setFullYear/setMinutes/setMonth` failures come from: (a) NaN propagation, (b) argument coercion via `ToNumber`, (c) UTC ↔ local time conversion edge cases.

### 3.4 Array Species & Concat Spreadable (≈100 failures)

`Array.prototype.concat` — must check `@@isConcatSpreadable` on arguments. Array methods `slice/splice/filter/map` must use `@@species` for the result array constructor.

### 3.5 Reflect ↔ Proxy Integration (≈58 failures)

Reflect methods are thin wrappers over internal operations. Once Proxy traps are correct, most Reflect tests should pass via the shared `[[Get]]`, `[[Set]]`, `[[DefineOwnProperty]]` paths.

---

## 4. Unsupported Features Review

### 4.1 Features in UNSUPPORTED_FEATURES That Are ≤ ES2020

| Feature | ES Version | Tests | Currently Skipped | Action |
|---------|-----------|------:|:-----------------:|--------|
| `tail-call-optimization` | ES2015 | ~60 | Yes | Keep skipped — causes infinite recursion without TCO |
| `regexp-lookbehind` | ES2018 | ~17 | **No** (fails naturally) | RE2 limitation — add to skip or add secondary engine |
| `regexp-named-groups` | ES2018 | ~36 | **No** (partially works) | Wire RE2's `(?P<name>)` to JS `(?<name>)` |
| `regexp-unicode-property-escapes` | ES2018 | ~602 | **No** (partially works) | Wire RE2's `\p{...}` support |
| `async-iteration` | ES2018 | ~1,100 | Yes (via `async-iteration` feature tag) | Blocked by async flag skip — separate from engine support |

### 4.2 Features That Should Stay Skipped (Post-ES2020)

| Feature | ES Version | Tests Skipped |
|---------|-----------|-------------:|
| Temporal | Stage 3 | 4,597 |
| Private class members (5 features) | ES2022 | ~3,400 |
| `async-iteration` | ES2018 | 1,100 |
| `regexp-unicode-property-escapes` | ES2018 | 669 |
| `iterator-helpers` | ES2025 | 567 |
| `resizable-arraybuffer` | ES2024 | 453 |
| `WeakRef` / `FinalizationRegistry` | ES2021 | ~300 |
| `regexp-modifiers` | ES2025 | 230 |
| `set-methods` | ES2025 | 190 |

---

## 5. Implementation Plan

### Tier 1: Foundation (Estimated +2,500 tests)

These are structural prerequisites that unlock cascading improvements.

| # | Work Item | Est. Impact | Dependencies | Files | Status |
|---|-----------|-------------|-------------|-------|--------|
| 1.1 | Property descriptor infrastructure (§9.1.6.3) | +300 | None | `js_runtime.cpp` | **Done** |
| 1.2 | Proxy handler traps — all 13 | +200 (direct) +100 (indirect) | 1.1 | `js_runtime.cpp`, `js_globals.cpp` | Not started |
| 1.3 | TypedArray species + detached checks | +400 | 1.1 | `js_typed_array.cpp` | Not started |
| 1.4 | Class public fields (instance + static) | +800 | None | `build_js_ast.cpp`, `transpile_js_mir.cpp` | **Done** |
| 1.5 | arguments exotic object (mapped) | +150 | None | `js_runtime.cpp` | **Done** |
| 1.6 | Block scope TDZ enforcement | +100 | None | `transpile_js_mir.cpp` | **Done** |

### Tier 2: Protocol Compliance (Estimated +1,500 tests)

Correctly implementing ES2020 protocols across all built-ins.

| # | Work Item | Est. Impact | Dependencies | Files | Status |
|---|-----------|-------------|-------------|-------|--------|
| 2.1 | Symbol.match/replace/search/split protocol | +200 | None | `js_runtime.cpp` | Partial |
| 2.2 | Array @@species + @@isConcatSpreadable | +100 | 1.1 | `js_runtime.cpp` | Partial |
| 2.3 | for-of IteratorClose on break/throw | +200 | None | `transpile_js_mir.cpp` | **Done** |
| 2.4 | RegExp named groups + \p{} property escapes | +300 | None | `re2_wrapper.cpp` | Partial |
| 2.5 | Function.prototype.toString source text | +70 | None | `js_globals.cpp` | **Done** |
| 2.6 | Promise static methods (allSettled, any) compliance | +80 | None | `js_runtime.cpp` | **Done** |
| 2.7 | Reflect ↔ Proxy integration | +50 | 1.2 | `js_globals.cpp` | Partial |

### Tier 3: Sloppy Mode & Annex B (Estimated +1,000 tests)

Web-compatibility behaviors that are part of ES2020 but lower priority.

| # | Work Item | Est. Impact | Dependencies | Files | Status |
|---|-----------|-------------|-------------|-------|--------|
| 3.1 | Sloppy-mode eval var scoping | +300 | None | `js_runtime.cpp`, `transpile_js_mir.cpp` | Not started |
| 3.2 | Annex B function hoisting (blocks) | +200 | 3.1 | `build_js_ast.cpp` | Not started |
| 3.3 | Annex B legacy RegExp features | +50 | None | `re2_wrapper.cpp` | Not started |
| 3.4 | Strict mode detection ("use strict") | +100 | None | `build_js_ast.cpp` | **Done** |
| 3.5 | Unicode whitespace / line terminators | +70 | None | `js_runtime.cpp` | Partial |
| 3.6 | Future reserved words in strict mode | +55 | 3.4 | `js_early_errors.cpp` | **Done** |

### Tier 4: Deep Built-in Compliance (Estimated +600 tests)

Edge cases and advanced features.

| # | Work Item | Est. Impact | Dependencies | Files | Status |
|---|-----------|-------------|-------------|-------|--------|
| 4.1 | DataView BigInt accessors + Float16 | +100 | None | `js_typed_array.cpp` | Partial |
| 4.2 | Atomics on non-shared buffers (throw) | +100 | None | `js_typed_array.cpp` | Not started |
| 4.3 | Date setter NaN/coercion edge cases | +90 | None | `js_runtime.cpp` | Partial |
| 4.4 | JSON.parse reviver + stringify replacer | +30 | None | `js_runtime.cpp` | **Done** |
| 4.5 | BigInt complete (comparisons, TypedArray) | +50 | 1.3 | `js_runtime.cpp` | Not started |
| 4.6 | SharedArrayBuffer constructor + species | +40 | None | `js_typed_array.cpp` | Not started |

---

## 6. Projected Progression

| Milestone | Passing | %Pass | Cumulative Gain |
|-----------|--------:|------:|:---------------:|
| Initial baseline | 23,412 | 68.7% | — |
| Phase A (quick wins) | 23,422 | 68.7% | +10 |
| Regex wrapper + TDZ + toString + misc | 23,808 | 69.8% | +396 |
| **Current baseline** | **23,808** | **69.8%** | **+396** |
| After Tier 1 | ~25,900 | ~76% | +2,500 |
| After Tier 2 | ~27,500 | ~81% | +4,000 |
| After Tier 3 | ~28,500 | ~84% | +5,000 |
| After Tier 4 | ~29,100 | ~85% | +5,600 |
| Theoretical max (excluding skipped) | 34,094 | 100% | +10,672 |

**Note:** The remaining ≈5,000 gap after Tier 4 comes from:
- Deep class field edge cases needing private fields (skipped feature)
- async-iteration tests (feature-skipped)
- RegExp features RE2 cannot support (lookbehind)
- Inherently slow/non-deterministic tests
- Cross-cutting edge cases that require case-by-case fixes

---

## 7. Recommended Execution Order

**Phase A — Quick wins (Tier 1.1 + 1.5 + 1.6 + Tier 2.5):**  
Property descriptors, arguments object, TDZ, toString — these are self-contained fixes that don't require architectural changes. Estimated **+600 tests**.

**Phase B — Class fields (Tier 1.4):**  
Single largest impact item. Class tests are 54% passing; public fields likely account for most of the gap. Estimated **+800 tests**.

**Phase C — Proxy + Reflect (Tier 1.2 + 2.7):**  
Proxy is foundational — it blocks secondary gains in Object, Reflect, and Array tests. Estimated **+350 tests** total.

**Phase D — TypedArray + DataView (Tier 1.3 + 4.1 + 4.2 + 4.6):**  
Large block of failures but mostly mechanical — species, detach checks, BigInt accessors. Estimated **+640 tests**.

**Phase E — RegExp + String protocols (Tier 2.1 + 2.4):**  
Wire RE2's existing Unicode property and named group support. Symbol protocol in String. Estimated **+500 tests**.

**Phase F — Iteration + Sloppy mode (Tier 2.3 + 3.x):**  
IteratorClose, eval scoping, Annex B — web compat layer. Estimated **+1,200 tests**.

---

## 8. Key Architectural Observations

1. **Proxy is the force multiplier.** Many Object, Reflect, Array, and TypedArray test failures involve Proxy as a test mechanism (e.g., using Proxy to intercept property operations and verify the engine calls the right internal methods). Fixing Proxy unlocks 50–100 "shadow" test gains across other categories.

2. **Property descriptor compliance is the foundation.** `ValidateAndApplyPropertyDescriptor` is called by defineProperty, seal, freeze, create, getOwnPropertyDescriptor, and every Proxy trap. Getting this right once fixes 300+ tests directly and prevents cascading failures.

3. **Class fields are the largest single gain.** With ~3,860 class-related failures and 54% current pass rate, public class fields alone could yield 800+ new passes. Private fields are behind an UNSUPPORTED_FEATURES skip and can wait for ES2022 targeting.

4. **TypedArray is mostly mechanical.** The 24% pass rate looks bad, but the failures cluster around a few missing behaviors (species, detach, BigInt). Each fix applies across all 11 TypedArray types × all methods, giving high test-per-fix ratio.

5. **The async flag skip masks real capability.** 5,454 tests are skipped due to `[async]` flag, not because async/await doesn't work, but because the test harness doesn't support async test scaffolding. Adding async test support would expose the actual pass rate on async/await features.
