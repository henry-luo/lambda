# Transpile_Js36: Class Improvements & Dynamic Import

## Summary

| Area | Current | Target | Est. New Passes |
|------|---------|--------|-----------------|
| **Class async-gen methods** | 20% (dstr: 6%) | ~80% | ~1,800 |
| **Dynamic import()** | 0% | ~80% | ~350 |
| **Subclass builtins** | 31% | ~90% | ~40 |
| **Class accessor names** | 57% | ~95% | ~20 |
| **Class async methods (elements)** | 10% | ~70% | ~60 |
| **Totals** | — | — | **~2,270** |

This proposal addresses the two largest failure areas in the test262 baseline: **class features** (~3,600 non-passing across expressions + statements) and **dynamic import()** (~785 non-passing). Together they account for roughly half of all non-passing test262 tests.

---

## Part 1 — Class Async Generator Methods (~1,800 tests)

### 1.1 Problem

Async generator methods in classes pass at 20% (dstr variants at 6%). The test262 `dstr` suite generates the **same** destructuring tests across 6 method types:

| Method type | Pass rate | Count |
|---|---|---|
| `meth-*` (regular) | 89% | 372 |
| `gen-meth-*` (generator) | 63% | 372 |
| `async-gen-meth-*` (async gen) | 6% | 372 |
| `private-meth-*` | 88% | 268 |
| `private-gen-meth-*` | 88% | 268 |
| `async-private-meth-*` | 9% | 268 |

The destructuring logic is shared across all method types — the failures are not in destructuring itself but in **async generator execution**.

### 1.2 Root Cause

In `transpile_js_mir.cpp`, async generators (`is_async=true && is_generator=true`) take the **generator path** (Phase v15, ~line 19569). The async state machine path (Phase 6, ~line 19960) is explicitly skipped: `if (fn->is_async && !fn->is_generator)`.

The generator state machine builds yield-point-based states via `jm_count_yields`. When the body contains `await` expressions inside an `async *method()`, they're processed as regular expressions rather than state-machine transitions. The `.next().then(() => ...)` pattern in tests requires:

1. `.next()` runs the generator body → encounters `yield` → returns `Promise<{value, done}>`
2. The promise resolves with the yielded value
3. `.then()` runs assertions

The generator SM correctly creates an async generator via `js_generator_create(..., is_async=1)`, but `await` inside the body doesn't properly suspend/resume the state machine. The 20% that pass are tests where the body runs synchronously.

### 1.3 Fix

Unify `await` handling into the generator state machine for async generators:

1. **Count await points as state transitions** — In `jm_count_yields`, also count `await` expressions when `is_async && is_generator`. Each `await` becomes a yield-like suspend point.
2. **Emit await-as-yield** — When transpiling an `await expr` inside an async generator, emit:
   - Evaluate `expr` → `val`
   - `js_await_resolve(val)` → unwrap thenable if needed
   - Yield the internal `__await__` sentinel to suspend
   - On `.next()` resume, the resolved value becomes the await result
3. **Runtime: `js_generator_next` for async generators** — When the yielded value is the `__await__` sentinel, wrap the value in a promise and auto-resume on resolution instead of returning to the caller.

**Key insight**: The existing generator state machine already handles multiple suspend/resume points. The fix is to treat `await` as another suspend point type, distinguished from `yield` by a flag that tells `js_generator_next` to auto-continue rather than return.

### 1.4 Scope

Fixing async generator execution will cascade across:
- `async-gen-meth-*` dstr tests: ~348 → ~300 new passes
- `async-gen-method` class elements: ~158 → ~130 new passes
- `async-gen-method-static` class elements: ~158 → ~130 new passes
- `async-private-meth-*` dstr tests: ~244 → ~200 new passes
- `private-static-async-gen` elements: ~30 → ~25 new passes
- `language/statements/async-generator`: ~267 → ~200 new passes
- `language/expressions/async-generator`: ~534 → ~400 new passes
- Other async-gen tests across built-ins: ~50+

**Estimated total: ~1,500–1,800 new passes from this single fix.**

---

## Part 2 — Dynamic Import (~350 tests)

### 2.1 Problem

`import()` expressions produce 0% pass rate in `catch/`, `usage/`, `namespace/` subcategories. The syntax subcategory partially passes (30%) for basic parsing but fails for all runtime behavior.

Of the 785 non-passing dynamic-import tests:
- ~295 are skippable (import-attributes, import-defer, import-source — ES2024+)
- **~490 are genuine in-scope failures**

### 2.2 Root Cause

Three layers are missing:

1. **AST**: `build_js_expression()` in `build_js_ast.cpp` has no handler for the `"import"` node type. The callee falls through to `log_error("Unsupported JavaScript expression type: import")` and the entire `import()` call is silently dropped.

2. **Transpiler**: Since no AST node is produced, `transpile_js_mir.cpp` never sees `import()` calls. There's no `import()` interception analogous to the existing `require()` interception (line ~9584).

3. **Runtime**: No `js_dynamic_import()` function exists. However, all the infrastructure is in place:
   - Module compilation: `transpile_js_module_to_mir()` (line ~25818)
   - Module cache: `js_module_get()` / `js_module_register()` (line ~20313/19603)
   - Namespace creation: `js_module_namespace_create()` (line ~20873)
   - Promise: `js_promise_resolve()`, `js_promise_reject()`

### 2.3 Implementation Plan

#### Phase A — Parse `import()` (syntax tests, ~34 genuine failures)

In `build_js_ast.cpp` → `build_js_expression()`:
```
case ts_js_import:
    // Create an identifier node with name "import" for call_expression handling
    return build_js_identifier_node(b, node, "import");
```

This alone makes `import()` survive as a `JsCallNode` with an `"import"` callee. Fixes syntax/valid tests that just check parsing.

#### Phase B — Transpile `import()` calls (~350 genuine failures)

In `transpile_js_mir.cpp` → `jm_transpile_call_expression()`:
```
// Intercept import() — similar to require() interception
if (callee is "import" identifier) {
    emit: jm_call_1(mt, "js_dynamic_import", specifier_arg)
    return
}
```

#### Phase C — Runtime `js_dynamic_import()` (~50-80 lines)

New function in `js_runtime.cpp`:
```c
Item js_dynamic_import(Item specifier) {
    // 1. ToString(specifier) — with error → reject
    // 2. Resolve path relative to current module
    // 3. Check module cache (js_module_get)
    // 4. If not cached: transpile_js_module_to_mir() → namespace
    // 5. js_module_register() to cache
    // 6. Return js_promise_resolve(namespace)
    // On any error: return js_promise_reject(error)
}
```

Register in `sys_func_registry.c`.

#### Phase D — Module error handling

- TypeError from module evaluation → catch and reject the promise
- Specifier coercion errors → reject
- Already-evaluated-with-error modules → reject with cached error

### 2.4 Test Breakdown After Fix

| Subcategory | Total | Skippable | Genuine | Expected Pass |
|---|---|---|---|---|
| syntax/valid | 192 | 126 | 34 | ~30 |
| syntax/invalid | 311 | 147 | 42 | ~35 |
| catch | 176 | 64 | 112 | ~90 |
| usage | 108 | 0 | 108 | ~85 |
| namespace | 67 | 0 | 67 | ~55 |
| (root) | 31 | — | ~30 | ~20 |
| assignment-expression | 28 | — | ~27 | ~20 |
| **Total** | **913** | **~337** | **~420** | **~335** |

---

## Part 3 — Subclass Builtins (~40 tests)

### 3.1 Problem

`class Sub extends Array { constructor() { super(); } }` creates a plain object that doesn't have Array internals (`.length` tracking, `Array.isArray`, etc.). Only `Error` subclasses have special handling (hardcoded at lines 9713-9743 in `js_runtime.cpp`).

### 3.2 Root Cause

`js_new_from_class_object` (line ~4484) always creates `js_new_object()` for class instances. When the superclass is a builtin constructor (Array, Map, Set, RegExp, Date, Promise, TypedArray), `super()` calls the parent but `this` has no native backing store.

### 3.3 Fix

In the `super()` call path (around line 9746), check if the resolved superclass constructor is a known builtin:

```
if (superclass == Array) → this = js_array_new()
if (superclass == Map) → this = js_map_new()
if (superclass == Set) → this = js_set_new()
if (superclass == RegExp) → this = js_regexp_create(...)
if (superclass == Date) → this = js_date_new(...)
if (superclass == Promise) → this = js_promise_create()
...
```

Then set `this.__proto__` to `SubClass.prototype` to maintain the inheritance chain.

### 3.4 Expected Impact

25 failing `subclass-builtins` tests × 2 (expr+stmt) = 50 tests. Expect ~40 new passes.

---

## Part 4 — Class Accessor Names (~20 tests)

### 4.1 Problem

Tests like `get 'character\tescape'()` fail because method names with **string escape sequences** aren't unescaped. The method name is stored as raw source text rather than the evaluated string value.

### 4.2 Fix

In `build_js_ast.cpp` class method key extraction: when the key is a string literal, apply the same unescape logic used for regular string literals (`js_string_unescape` or equivalent).

Also handle **numeric literal keys**: `get 0x10()` should normalize to key `"16"`.

### 4.3 Expected Impact

~18 failing accessor-name tests × 2 (inst+static) = ~36 tests, minus some already passing. Expect ~20 new passes.

---

## Part 5 — Class Async Method Elements (~60 tests)

### 5.1 Problem

`async` methods in class elements (not async generators) pass at only 10%. These use the async state machine (Phase 6), which should work since it explicitly handles `is_async && !is_generator`.

### 5.2 Likely Root Cause

Class element context (private async methods, static async methods) may have scope/capture issues in the async state machine. The state machine creates a closure for the async body, and private field access (`this.#field`) inside the closure may not resolve correctly.

### 5.3 Fix

Investigate by running a few failing tests individually and checking `log.txt`. Likely a capture/scope issue specific to async closures in class element context — similar to past fixes for generator scope resolution.

### 5.4 Expected Impact

~132 failing async-method/async-method-static element tests. Expect ~60 new passes.

---

## Implementation Order

| Phase | Work | Tests Fixed | Effort |
|---|---|---|---|
| **1. Dynamic import (A-C)** | AST + transpiler + runtime | ~335 | Medium (mostly wiring) |
| **2. Async generator SM** | Await-as-yield in gen SM | ~1,500-1,800 | Hard (state machine changes) |
| **3. Subclass builtins** | Builtin-aware super() | ~40 | Easy (dispatch table) |
| **4. Accessor names** | String unescape for keys | ~20 | Easy (one-line fix area) |
| **5. Async method elements** | Debug + scope fix | ~60 | Medium (diagnosis needed) |

### Phase 1 should start first because:
- Lowest risk — mostly new code, no existing behavior changes
- All infrastructure (modules, promises, MIR compilation) already exists
- Enables test262 dynamic-import tests to provide feedback on module system correctness

### Phase 2 is highest impact:
- Single state machine change cascades across ~1,800 tests
- But higher risk of regressions in existing generator tests
- Requires careful testing: run test262 baseline-only after each change

---

## Verification

After each phase:
```bash
# Quick regression check
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only

# Full run to find improvements
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only

# Update baseline when regressions=0
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --update-baseline
```

Target: **~2,270 new passes** → baseline from 25,272 to ~27,500 (80.7% in-scope pass rate).
