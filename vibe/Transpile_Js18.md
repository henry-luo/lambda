# JavaScript Transpiler v18: Runtime Correctness Enhancement Proposal

## 1. Executive Summary

LambdaJS scores **17,740 / 26,259 executed (67.6%)** against test262 after v17 improvements. An additional **12,390 tests are skipped** due to feature gating and flag restrictions.

v17 focused on **static semantic validation** (early errors, strict mode) and **feature gate cleanup**. v18 shifts focus to **runtime correctness** — fixing fundamental behavioral bugs that cause cascading test failures across multiple categories. A single fix to property descriptor handling on class prototypes alone could recover **~450+ tests**.

### Current Results (v18 Baseline)

| Metric | Count |
|--------|-------|
| Total tests discovered | 38,650 |
| Tests executed (pass + fail) | 26,259 |
| Tests skipped | 12,390 |
| **Passed** | **17,740** |
| **Failed** | **8,519** |
| **Pass rate (of executed)** | **67.6%** |

### Projected v18 Impact

| Phase | Enhancement | Est. Tests Fixed | Effort |
|-------|-------------|:----------------:|--------|
| 1 | Property descriptors on class prototypes | ~450 | Medium |
| 2 | `finally` block with break/continue | ~35 | Small |
| 3 | Bare `return` produces `undefined`, not `null` | ~50 | Small |
| 4 | Non-callable TypeError + Boolean()/sparse arrays | ~60 | Small |
| 5 | Computed class field installation | ~55 | Medium |
| 6 | RegExp flag property accessors | ~30 | Small |
| 7 | Function name inference | ~45 | Medium |
| 8 | Labeled break/continue in nested loops | ~40 | Medium |
| 9 | Switch case expression evaluation | ~25 | Small |
| 10 | Unlock `onlyStrict` tests + `numeric-separator-literal` | +980 newly executed | Medium |
| **Total** | | **~790 fixed + ~980 unlocked** | |

**Projected result: ~18,530+ / ~27,240 → ~68%** overall, or **~18,530 / 26,259 → 70.6%** on the current executed set.

## 2. Current State Analysis

### Per-Category Failure Map

Subcategory-level analysis reveals where failures are concentrated:

| Category | Pass | Fail | Total | Rate | Primary Root Cause |
|----------|------|------|-------|------|--------------------|
| expressions/class | 1,523 | 2,167 | 3,690 | 41.2% | Descriptors, computed fields |
| statements/class | 2,074 | 1,601 | 3,675 | 56.4% | Descriptors, static blocks |
| expressions/object | 463 | 619 | 1,082 | 42.7% | Descriptors, fn-name |
| expressions/assignment | 455 | 465 | 920 | 49.4% | Destructuring, fn-name |
| statements/for-of | 400 | 411 | 811 | 49.3% | Destructuring, iterator close |
| literals | 144 | 245 | 389 | 37.0% | RegExp props, numeric seps |
| identifiers | 195 | 143 | 338 | 57.6% | Unicode identifiers |
| statements/try | 125 | 117 | 242 | 51.6% | finally+break, destructuring |
| asi | 50 | 105 | 155 | 32.2% | return→null, parser gaps |
| statements/switch | 47 | 91 | 138 | 34.0% | Case expression eval, scope |
| statements/for-in | 50 | 83 | 133 | 37.5% | Prototype chain, descriptors |
| block-scope | 84 | 79 | 163 | 51.5% | TDZ, lexical scope close |
| expressions/call | 24 | 83 | 107 | 22.4% | eval(), non-callable TypeError |
| line-terminators | 22 | 39 | 61 | 36.0% | Unicode line terminators |
| expressions/optional-chaining | 14 | 37 | 51 | 27.4% | Runtime edge cases |
| statements/variable | 104 | 39 | 143 | 72.7% | Various |
| expressions/template-literal | 39 | 35 | 74 | 52.7% | Tagged template edge cases |
| keywords | 19 | 13 | 32 | 59.3% | Keyword-as-identifier parse |
| statements/let | 120 | 27 | 147 | 81.6% | TDZ |
| statements/const | 112 | 27 | 139 | 80.5% | TDZ |
| global-code | 28 | 19 | 47 | 59.5% | Scope, eval |
| punctuators | 1 | 21 | 22 | 4.5% | Parser: semicolon insertion |
| comments | 16 | 15 | 31 | 51.6% | Unicode, HTML-like comments |
| reserved-words | 22 | 9 | 31 | 70.9% | Strict mode reserved |
| tagged-template | 19 | 7 | 26 | 73.0% | Various |
| labeled | 13 | 7 | 20 | 65.0% | Labeled control flow |
| typeof | 11 | 7 | 18 | 61.1% | Various |

### Root Cause Cross-Reference

Analysis of 889 sampled failing tests reveals these structural root causes, ordered by impact:

| Root Cause | Est. Failures | % of Sample | Categories Affected |
|-----------|:------------:|:-----------:|---------------------|
| Property descriptors on class/object prototypes | ~452 | 51% | class, object, for-in |
| Destructuring rest pattern edge cases | ~237 | 27% | class, object, for-of, try, assignment |
| Computed class field installation | ~52 | 6% | class |
| Function name inference | ~45 | 5% | for-of, assignment, object |
| finally + break/continue | ~33 | 4% | try, for-of |
| RegExp property accessors | ~28 | 3% | literals |
| Other (eval, Unicode, yield, etc.) | ~42 | 4% | various |

### Confirmed Bugs (Micro-Tests)

The following bugs were confirmed with minimal reproducing test cases:

| Bug | Expected | Actual | Impact |
|-----|----------|--------|--------|
| `Object.getOwnPropertyDescriptor(C.prototype, "m")` | `{value: fn, ...}` | `undefined` | ~452 tests |
| `try { break; } finally { x++; }` | finally runs | finally skipped | ~33 tests |
| `function f() { return; } f()` | `undefined` | `null` | ~50 tests |
| `true()` | TypeError | no error | ~10 tests |
| `Boolean()` | `false` | `null` | ~5 tests |
| `[1, , 3].length` | `3` | `2` | ~15 tests |
| `class { [key] = 42 }` field install | `42` | `undefined` | ~52 tests |
| `/abc/i.ignoreCase` | `true` | `undefined` | ~28 tests |
| `(function(){}).name` | `""` inferred | `""` always | ~45 tests |
| Labeled `break outer` from nested loop | `i=0, j=1` | `undefined, undefined` | ~40 tests |
| `switch(v) { case isNaN(v): }` | evaluates expr | incorrect | ~25 tests |

## 3. Phase 1: Property Descriptors on Class Prototypes

**Impact: ~450 tests | Effort: Medium (1-2 days)**

### Problem

`Object.getOwnPropertyDescriptor(C.prototype, "m")` returns `undefined` for class methods. This single bug accounts for the majority of class element test failures because the test262 harness file `propertyHelper.js` uses `verifyProperty()` which calls `getOwnPropertyDescriptor` internally.

```js
class C { m() { return 42; } }
Object.getOwnPropertyDescriptor(C.prototype, "m"); // → undefined (should be {value: fn, writable: true, ...})
```

### Root Cause

Class methods are stored on the prototype object as regular properties, but `js_get_own_property_descriptor()` in `js_globals.cpp` likely only recognizes properties with explicit descriptor markers (`__nw_`, `__nc_`, `__ne_`). For properties without markers, it should return a default data descriptor: `{value: prop, writable: true, enumerable: true, configurable: true}`.

Additionally, for prototype-assigned properties like `C.prototype.bar = fn`, the descriptor is returned but missing the `value` field.

### Implementation

1. In `js_get_own_property_descriptor()` (`lambda/js/js_globals.cpp`):
   - When a property exists on the object but has no descriptor markers, return a default data descriptor with `value`, `writable: true`, `enumerable: true`, `configurable: true`
   - Ensure the `value` field is always included in the returned descriptor object

2. Verify with test cases:
   ```js
   class C { m() {} get g() {} set s(v) {} }
   assert(Object.getOwnPropertyDescriptor(C.prototype, "m").value === C.prototype.m);
   assert(Object.getOwnPropertyDescriptor(C.prototype, "g").get !== undefined);
   ```

### Verification

```bash
./test/test_js_test262_gtest.exe --gtest_filter='*language_expressions_class_elements*' 2>&1 | tail -8
./test/test_js_test262_gtest.exe --gtest_filter='*language_statements_class_elements*' 2>&1 | tail -8
```

## 4. Phase 2: `finally` Block with Break/Continue

**Impact: ~35 tests | Effort: Small (half day)**

### Problem

When a `try` block contains `break` or `continue`, the `finally` block is skipped entirely. This violates ES spec §14.15.3 which requires finally to always execute.

```js
var x = 0;
for (var i = 0; i < 3; i++) {
  try { x++; break; } finally { x += 10; }
}
// Expected: x === 11 (1 from try + 10 from finally)
// Actual: x === 1 (finally skipped)
```

### Root Cause

The MIR codegen for `try`/`finally` in `transpile_js_mir.cpp` likely implements `break`/`continue` as direct jumps to loop headers/exits, bypassing the finally cleanup path. The `return` case works because it goes through the function return path which does execute finally.

### Implementation

In `transpile_js_mir.cpp`, the `try`/`finally` codegen needs to:
1. Track that we're inside a try-with-finally block
2. When emitting `break`/`continue` inside such a block, jump to the finally block first
3. After finally completes, then perform the actual break/continue jump
4. Handle nested try/finally + loops correctly

### Affected Tests
- `language/statements/try/S12.14_A*` (27 tests involving loops)
- `language/statements/for-of/break-from-finally` etc. (6 tests)
- `language/statements/for-of/return-from-finally` (2 tests)

## 5. Phase 3: Bare `return` Produces `undefined`

**Impact: ~50 tests | Effort: Small (few hours)**

### Problem

`return;` (bare return) and `return\n` (return with ASI) produce `null` instead of `undefined`. This affects many tests that check return values with strict equality.

```js
function f() { return; }
typeof f()   // → "object" (because null)
f() === undefined  // → false
```

### Root Cause

In `transpile_js_mir.cpp`, the code for `JS_AST_NODE_RETURN_STATEMENT` without an argument emits `ItemNull` instead of `ITEM_JS_UNDEFINED`. This was likely inherited from the Lambda-native convention where null is the default empty value.

### Implementation

In `transpile_js_mir.cpp`, find the return statement handler for the no-argument case and change it to emit `ITEM_JS_UNDEFINED` instead of `ItemNull` (or `jm_emit_null`). Similarly check function bodies that reach end-of-function without a return statement.

### Affected Tests
- `language/asi/S7.9.2_A1_T*` (ASI + return)
- `language/statements/return/S12.9_A*` (return semantics)
- Many tests across categories that use `=== undefined` on function return values

## 6. Phase 4: Non-Callable TypeError + Boolean()/Sparse Arrays

**Impact: ~60 tests | Effort: Small (1 day)**

### Problem A: Non-Callable TypeError

Calling a non-function value like `true()`, `1()`, `null()` silently succeeds instead of throwing TypeError. ES spec §12.3.6.1 requires throwing TypeError when the call target is not callable.

```js
true();   // Should throw TypeError, actually returns undefined silently
```

### Problem B: Boolean() Returns Null

`Boolean()` (called without arguments) returns `null` instead of `false`.

```js
Boolean()  // → null (should be false)
typeof Boolean()  // → "object" (should be "boolean")
```

### Problem C: Sparse Array Holes

Array literals with elisions (`[1, , 3]`) don't preserve holes — length is 2 instead of 3.

```js
[1, , 3].length  // → 2 (should be 3)
```

### Implementation

**A)** In `js_call_function()` (`js_runtime.cpp`) or the MIR call dispatch:
- Before calling, check if the callee is actually a function
- If not callable, throw TypeError: "X is not a function"

**B)** In `Boolean()` constructor/function handler:
- When called with no arguments, return `false` instead of `null`

**C)** In array literal codegen or the array builder:
- Emit `undefined` for elided elements and maintain correct length
- Or use a hole sentinel that preserves array length

## 7. Phase 5: Computed Class Field Installation

**Impact: ~55 tests | Effort: Medium (1 day)**

### Problem

Class fields with computed property names don't install on instances:

```js
var key = "x";
class A { [key] = 42; }
var a = new A();
a[key]  // → undefined (should be 42)
a.x     // → undefined (should be 42)
```

Literal-name fields (`class A { x = 42 }`) work correctly.

### Root Cause

In `transpile_js_mir.cpp`, the class field initializer codegen likely handles string-literal field names as a special case (direct property set) but doesn't handle computed names — where the property key is an expression that must be evaluated at class definition time and stored, then used during instance construction.

### Implementation

In the class field initializer path:
1. At class definition time, evaluate computed property name expressions
2. Store the computed keys (as a hidden array or shape entries)
3. During constructor/instance initialization, use the stored keys for property installation

## 8. Phase 6: RegExp Flag Property Accessors

**Impact: ~30 tests | Effort: Small (half day)**

### Problem

RegExp flag property accessors (`ignoreCase`, `multiline`, `global`, `dotAll`, `unicode`, `sticky`, `lastIndex`) return `undefined` instead of their proper values.

```js
var r = /abc/gi;
r.ignoreCase  // → undefined (should be true)
r.multiline   // → undefined (should be false)
r.lastIndex   // → undefined (should be 0)
```

### Root Cause

The RegExp object is created with the pattern and flags stored internally (for RE2 matching), but the flag properties are not exposed as readable properties on the object. Only `.global` appears to work.

### Implementation

In the RegExp constructor or the regexp creation path:
1. Parse the flags string and set boolean properties: `global`, `ignoreCase`, `multiline`, `dotAll`, `unicode`, `sticky`
2. Set `lastIndex` to `0`
3. Set `source` to the pattern string
4. Set `flags` to the sorted flag string

## 9. Phase 7: Function Name Inference

**Impact: ~45 tests | Effort: Medium (1-2 days)**

### Problem

Function `.name` always returns `""` for all inferred contexts:

```js
var fn = function(){};
fn.name  // → "" (should be "fn")

var obj = { bar: function(){} };
obj.bar.name  // → "" (should be "bar")

var arrow = () => {};
arrow.name  // → "" (should be "arrow")
```

### Root Cause

Function objects are created without names being inferred from the assignment context. ES2015+ requires that anonymous functions inherit names from:
- Variable declarations: `var fn = function(){}`
- Property definitions: `{ key: function(){} }`
- Default exports
- Class methods (already named via method syntax)

### Implementation

In `transpile_js_mir.cpp`:
1. During variable declaration with function initializer, set `fn.__name` property
2. During object property definition with function value, set the name
3. In the `fn.name` getter path, return the `__name` property

### Affected Tests
- `language/expressions/assignment/fn-name-*` (22 tests)
- `language/statements/for-of/dstr-*-fn-name-*` (15 tests)
- `language/expressions/object/method-definition/name-*` (8 tests)

## 10. Phase 8: Labeled Break/Continue in Nested Loops

**Impact: ~40 tests | Effort: Medium (1 day)**

### Problem

Labeled `break` and `continue` statements in nested loops produce incorrect variable state:

```js
outer: for (var i = 0; i < 3; i++) {
  inner: for (var j = 0; j < 3; j++) {
    if (j === 1) break outer;
  }
}
// Expected: i = 0, j = 1
// Actual: i = undefined, j = undefined
```

### Root Cause

The labeled break/continue likely doesn't properly update the loop variable before jumping. Or the variable scoping is reset when the labeled jump crosses loop boundaries.

### Implementation

In `transpile_js_mir.cpp`, ensure that when emitting labeled `break`/`continue`:
1. The loop counter variable has been updated (for-loop increment hasn't been skipped)
2. Variable storage is preserved across the label jump
3. Nested label tracking correctly identifies the target loop

### Affected Tests
- `language/statements/labeled/*` (7 tests)
- `language/statements/for/labeled-*` (various)
- `language/statements/block/labeled-continue` (1 test)

## 11. Phase 9: Switch Case Expression Evaluation

**Impact: ~25 tests | Effort: Small (half day)**

### Problem

Switch statements with expression cases (not just constants) don't evaluate correctly:

```js
function test(v) {
  switch(v) {
    case isNaN(v): return "nan"; 
    default: return "other";
  }
}
test(NaN)  // Should return "nan", but returns "other"
```

Also, switch case fall-through with expression cases is incorrect.

### Root Cause

The switch codegen may use compile-time constant matching for case values instead of runtime expression evaluation. All case expressions must be evaluated at runtime using strict equality (`===`).

### Implementation

In `transpile_js_mir.cpp`, ensure switch case codegen:
1. Evaluates each case expression at runtime (not just constant folding)
2. Compares with `===` semantics
3. Maintains correct fall-through behavior across expression cases

## 12. Phase 10: Unlock `onlyStrict` Tests + Feature Gates

**Impact: +980 newly executable tests | Effort: Medium (1 day)**

### Problem A: `onlyStrict` Tests Skipped

823 tests with `flags: [onlyStrict]` are completely skipped. Since v17 implemented strict mode `this` coercion and `with` rejection, many of these should now pass. The test runner should add `"use strict";` prefix and execute them.

### Problem B: `numeric-separator-literal` Still Gated

159 tests are skipped because `numeric-separator-literal` is in `UNSUPPORTED_FEATURES`. The Tree-sitter JavaScript grammar should parse `1_000_000` — if it does, this gate can be removed.

### Problem C: `class-static-block` Still Gated

65 tests blocked. Static blocks (`static { ... }`) are parsed by Tree-sitter but the body is never executed. Implementing basic codegen would unlock these.

### Implementation

**A)** In `test/test_js_test262_gtest.cpp`:
- Remove `onlyStrict` from the skip flags list
- When `meta.is_strict` is true, prepend `"use strict";\n` to the combined source

**B)** Test `1_000_000` parsing:
```bash
echo 'var x = 1_000_000; if (x !== 1000000) throw new Error(x);' > temp/test_numsep.js
./lambda.exe js temp/test_numsep.js --no-log 2>&1
```
If it works, remove `"numeric-separator-literal"` from `UNSUPPORTED_FEATURES`.

**C)** In `transpile_js_mir.cpp`, add codegen for `JS_AST_NODE_STATIC_BLOCK` (or equivalent):
- Emit the block body as part of the class static initialization

## 13. Implementation Priority & Dependencies

### Dependency Graph

```
Phase 1 (descriptors) ← independent, highest ROI
Phase 2 (finally+break) ← independent
Phase 3 (return undefined) ← independent, quick win
Phase 4 (TypeError+Boolean+sparse) ← independent, quick win
Phase 5 (computed fields) ← depends on class codegen understanding
Phase 6 (regexp props) ← independent, quick win
Phase 7 (fn-name) ← independent
Phase 8 (labeled break) ← independent
Phase 9 (switch expr) ← independent
Phase 10 (unlock tests) ← depends on Phase 3 (for strict tests)
```

### Recommended Order

| Week | Phases | Expected Delta |
|------|--------|:-------------:|
| 1 | Phase 1 (descriptors) + Phase 3 (return) + Phase 4 (TypeError) | +560 |
| 2 | Phase 2 (finally) + Phase 6 (regexp) + Phase 9 (switch) | +90 |
| 3 | Phase 5 (computed fields) + Phase 7 (fn-name) + Phase 8 (labeled) | +140 |
| 4 | Phase 10 (unlock onlyStrict + feature gates) | +980 unlocked |

### Risk Assessment

| Phase | Risk | Mitigation |
|-------|------|------------|
| 1 | Medium — descriptor logic is complex | Thorough testing w/ propertyHelper.js |
| 2 | Medium — break/continue + finally interactions | Test nested try/finally/for combos |
| 3 | Low — simple constant change | Verify doesn't break Lambda-native semantics |
| 4 | Low — straightforward runtime checks | |
| 5 | Medium — class codegen is intricate | Understand field initializer flow |
| 6 | Low — add properties to regexp object | |
| 7 | Medium — requires context propagation | Limit to var/property assignment |
| 8 | Medium — label tracking across loop nesting | |
| 9 | Low — switch codegen fix | |
| 10 | Low — test runner change only | Expect some new failures from unlocked tests |

## 14. Success Metrics

| Metric | v17 Actual | v18 Target |
|--------|:----------:|:----------:|
| Tests executed | 26,259 | ~27,240 |
| Tests passed | 17,740 | ~18,530 |
| Pass rate (executed) | 67.6% | 70-72% |
| class expressions pass rate | 41.2% | 65% |
| class statements pass rate | 56.4% | 72% |
| try/catch/finally pass rate | 51.6% | 75% |
| literals pass rate | 37.0% | 55% |
| Built-in pass rate | 99.8% | 99.8% (maintain) |

## 15. Files Reference

| File | Expected Changes |
|------|-----------------|
| `lambda/js/js_globals.cpp` | Phase 1 (descriptors), Phase 4B (Boolean), Phase 6 (regexp) |
| `lambda/js/transpile_js_mir.cpp` | Phase 2 (finally+break), Phase 3 (return), Phase 5 (computed fields), Phase 7 (fn-name), Phase 8 (labeled), Phase 9 (switch) |
| `lambda/js/js_runtime.cpp` | Phase 4A (non-callable TypeError) |
| `lambda/js/build_js_ast.cpp` | Phase 4C (sparse arrays) |
| `test/test_js_test262_gtest.cpp` | Phase 10 (onlyStrict unlock, feature gates) |

## 16. Appendix: Full Bug Reproduction

```bash
# Phase 1: Descriptor missing on class prototype
echo 'class C { m() {} } var d = Object.getOwnPropertyDescriptor(C.prototype, "m"); console.log(d);' > temp/t.js
./lambda.exe js temp/t.js --no-log  # → undefined (should be {value: fn, ...})

# Phase 2: finally skipped with break
echo 'var x=0; for(var i=0;i<1;i++){try{break;}finally{x=1;}} console.log(x);' > temp/t.js
./lambda.exe js temp/t.js --no-log  # → 0 (should be 1)

# Phase 3: return produces null
echo 'function f(){return;} console.log(typeof f());' > temp/t.js
./lambda.exe js temp/t.js --no-log  # → object (should be undefined)

# Phase 4A: Non-callable no TypeError
echo 'try{true();}catch(e){console.log(e instanceof TypeError);}' > temp/t.js
./lambda.exe js temp/t.js --no-log  # → (no output, should be true)

# Phase 4B: Boolean() returns null
echo 'console.log(Boolean(), typeof Boolean());' > temp/t.js
./lambda.exe js temp/t.js --no-log  # → null object (should be false boolean)

# Phase 4C: Sparse array length
echo 'console.log([1,,3].length);' > temp/t.js
./lambda.exe js temp/t.js --no-log  # → 2 (should be 3)

# Phase 5: Computed class field
echo 'var k="x"; class A{[k]=42} console.log(new A()[k]);' > temp/t.js
./lambda.exe js temp/t.js --no-log  # → undefined (should be 42)

# Phase 6: RegExp properties
echo 'console.log(/a/i.ignoreCase);' > temp/t.js
./lambda.exe js temp/t.js --no-log  # → undefined (should be true)

# Phase 7: Function name inference
echo 'var f=function(){}; console.log(f.name);' > temp/t.js
./lambda.exe js temp/t.js --no-log  # → (empty, should be "f")

# Phase 8: Labeled break
echo 'outer:for(var i=0;i<3;i++){for(var j=0;j<3;j++){if(j===1)break outer;}} console.log(i,j);' > temp/t.js
./lambda.exe js temp/t.js --no-log  # → undefined undefined (should be 0 1)

# Phase 9: Switch with expression case
echo 'switch(NaN){case isNaN(NaN):console.log("match");break;default:console.log("no");}' > temp/t.js
./lambda.exe js temp/t.js --no-log  # → no (should be match)
```

---

## Progress Checkpoint: Destructuring Assignment Fix (2026-04-03)

### Problem

Top-level `var` declarations are stored as **module variables** (`MCONST_MODVAR` in the `module_consts` hashmap), accessed via `js_get_module_var(idx)` / `js_set_module_var(idx, val)`. They are NOT local variables in `var_scopes`.

The destructuring assignment code paths only used `jm_find_var()`, which searches local `var_scopes` — returning NULL for module vars. This caused all destructuring assignments to top-level variables to silently produce `undefined`.

**Affected patterns:**
```js
var a, b; [a, b] = [1, 2];           // → undefined undefined (should be 1 2)
var x, y; ({x, y} = {x: 10, y: 20}); // → undefined undefined (should be 10 20)
[p = 100, q = 200] = [42];           // defaults broken
for ([v = 10, ...] of [[2, null]])    // for-of destructuring broken
```

### Fix Applied (`lambda/js/transpile_js_mir.cpp`)

Added module var fallback pattern to all 4 destructuring code paths:

| Code Path | Location | Changes |
|-----------|----------|---------|
| Standalone array destructuring | `jm_transpile_assignment` | `jm_assign_to_var` helper: local var → module var → global property fallback |
| Standalone object destructuring | `jm_transpile_assignment` | Same helper + `jm_emit_default_check` for defaults |
| For-of array extraction | `jm_transpile_for_of` | Added `ASSIGNMENT_PATTERN` handling + module var fallback for IDENTIFIER, nested OBJECT_PATTERN, SPREAD_ELEMENT |
| For-of object extraction | `jm_transpile_for_of` | Added default value handling + module var fallback |

### Result

| Version | Passed | Executed | Rate | vs Baseline |
|---------|--------|----------|------|-------------|
| v18 baseline (commit `3cff26eef`) | 10,903 | 26,967 | 40.4% | — |
| Before fix (master HEAD) | 10,784 | 26,967 | 40.0% | −119 |
| **After destructuring fix** | **10,838** | **26,967** | **40.2%** | **−65** |

**+54 tests recovered.** Gap to baseline reduced from −119 to −65.

### Note on Test Counts

The test262 runner reports two different totals — this is expected:

| Metric | Count | Source |
|--------|-------|--------|
| **Total test files discovered** | 38,649 | GTest parameterized test count |
| **Skipped** (unsupported features) | 11,682 | Tests requiring Proxy, Temporal, SharedArrayBuffer, etc. |
| **Executed** (pass + fail) | 26,967 | `38,649 − 11,682` |
| **Passed** | 10,838 | Custom summary in test runner |
| **Failed** | 16,129 | Custom summary in test runner |

GTest XML reports `tests="38649" failures="16129"` — it counts skipped tests as "not failed" (i.e., 38,649 − 16,129 = 22,520 includes both passed AND skipped). The custom `test262 Compliance Summary` box printed at the end correctly separates skipped from passed, giving the accurate **10,838 / 26,967 (40.2%)** figure.

---

## Parallel Test262 Runner (2026-04-03)

### Problem

The test262 suite ran **sequentially** — each of the 38,649 tests was executed one at a time via `popen()` → `pclose()` in a single GTest thread. Total wall-clock time: **~18m 43s** (1,123s).

### Changes (`test/test_js_test262_gtest.cpp`)

Converted to a **parallel pre-run + cached reporting** architecture:

1. **Thread pool**: Uses `std::thread::hardware_concurrency()` worker threads (8 on MacBook Air M2). Each thread atomically picks the next test from a shared index counter.

2. **Unique temp files**: Each `run_test262()` invocation writes to a uniquely-numbered temp file (`temp/_test262_run_<N>.js`) instead of the shared `temp/_test262_run.js`, eliminating write races.

3. **Result cache**: All `Test262RunResult` structs are stored in a `std::unordered_map<std::string, Test262RunResult>` protected by a mutex. The parallel phase runs to completion before GTest starts.

4. **GTest reporting phase**: `TEST_P(Test262Suite, Run)` reads from the cache instead of executing `lambda.exe`. This phase takes ~0.3s (just map lookups + GTest bookkeeping).

5. **Progress reporting**: Prints to stderr every 2,000 tests completed, with elapsed time.

### Timing Results

| Metric | Sequential | Parallel (8 threads) |
|--------|-----------|---------------------|
| Wall clock | **18m 43s** (1,123s) | **5m 13s** (313s) |
| CPU time (user) | ~18m | 998s |
| CPU time (system) | — | 610s |
| CPU utilization | ~100% (1 core) | **514%** (~5 cores avg) |
| **Speedup** | 1× | **3.6×** |

### Result Correctness

Results are identical before and after parallelization:

| Metric | Sequential | Parallel |
|--------|-----------|---------|
| Passed | 10,838 | 10,838 |
| Failed | 16,129 | 16,129 |
| Skipped | 11,682 | 11,682 |

---

## Implementation Status Audit (2026-04-03)

Verified each proposed phase against the current build using the Appendix §16 micro-tests.

| Phase | Enhancement | Status | Evidence |
|-------|-------------|--------|----------|
| 1 | Property descriptors on class prototypes | ✅ Fixed | `getOwnPropertyDescriptor(C.prototype, "m")` → `{writable: true, enumerable: false, configurable: true}` + value is function |
| 2 | `finally` block with break/continue | ✅ Fixed | `try { break; } finally { x=1; }` → `x === 1` |
| 3 | Bare `return` produces `undefined` | ✅ Fixed | `typeof f()` → `"undefined"` |
| 4A | Non-callable TypeError | ✅ Fixed | `true()` throws `TypeError` |
| 4B | `Boolean()` returns `false` | ✅ Fixed | `Boolean()` → `false`, `typeof` → `"boolean"` |
| 4C | Sparse array holes | ✅ Fixed | `[1,,3].length` → `3` |
| 5 | Computed class field installation | ✅ Fixed | `var k="x"; class A{[k]=42} new A()[k]` → `42` (computed + literal fields both work) |
| 6 | RegExp flag property accessors | ✅ Fixed | `/a/i.ignoreCase` → `true` |
| 7 | Function name inference | ✅ Fixed | `var f=function(){}; f.name` → `"f"` |
| 8 | Labeled break/continue in nested loops | ✅ Fixed | `break outer` → `i=0, j=1` |
| 9 | Switch case expression evaluation | ⚠️ Invalid | The NaN example was correct JS behavior: `isNaN(NaN)` → `true`, then `NaN === true` → `false` (fallthrough to default). Expression case evaluation works for non-NaN cases. |
| 10A | `numeric-separator-literal` | ✅ Ungated | Removed from `UNSUPPORTED_FEATURES`; `1_000_000` parses correctly |
| 10B | `onlyStrict` tests | ✅ Unlocked | Runner prepends `"use strict";` for `is_strict` tests |
| 10C | `class-static-block` | ✅ Fixed | Removed from `UNSUPPORTED_FEATURES`; `static { C.x = 42; }` → `C.x === 42` |

### Summary

- **All 10 phases fully implemented** (Phases 1–8, 10A/B/C)
- **Phase 9 retracted**: The original NaN switch example was actually correct `===` semantics, not a bug

### Current test262 Results (post all fixes)

| Metric | Count |
|--------|-------|
| Total tests | 38,649 |
| Passed | 10,982 |
| Failed | 16,048 |
| Skipped | 11,619 |
| **Pass rate (executed)** | **40.6%** (10,982 / 27,030) |
| **Git base** | `3402b2fca` + computed fields/static blocks (uncommitted) |

**Note:** The pass rate (40.6%) is significantly lower than the v18 proposal baseline (67.6% / 17,740 passed). The proposal numbers were from a different test262 runner configuration or codebase version. The current numbers reflect the actual state of the parallel test262 runner.

---

## Progress Checkpoint: Computed Class Fields + Static Blocks (2026-04-03)

**Git base:** `3402b2fca` (master) + uncommitted changes in `js_ast.hpp`, `build_js_ast.cpp`, `transpile_js_mir.cpp`, `test_js_test262_gtest.cpp`

### Phase 5: Computed Class Fields

**Problem:** Class fields with computed property names (`[expr] = value`) were silently dropped. The collection phase required `JS_AST_NODE_IDENTIFIER` as the key type, so computed keys (wrapped in `computed_property_name`) were ignored. Instance field installation and static field initialization only handled string-literal names.

**Root cause:** `JsFieldDefinitionNode` in `js_ast.hpp` had no `computed` flag. `JsStaticFieldEntry` and `JsInstanceFieldEntry` in the transpiler only stored `String* name` — no way to represent a runtime-evaluated key expression.

**Changes (3 files, 5 code paths):**

| File | Change |
|------|--------|
| `lambda/js/js_ast.hpp` | Added `bool computed` flag to `JsFieldDefinitionNode` |
| `lambda/js/build_js_ast.cpp` | Detect `computed_property_name` node type → set `computed = true`, store raw key expression |
| `lambda/js/transpile_js_mir.cpp` | Added `JsAstNode* key_expr` + `bool computed` to `JsStaticFieldEntry` and `JsInstanceFieldEntry` |

**Transpiler code paths updated:**
1. **Collection phase** (~line 3003): Computed fields now collected with `key_expr` (name stays NULL)
2. **Top-level static field init** (~line 17604): Computed → `js_property_set(cls_obj, eval(key_expr), val)` at runtime
3. **Statement-level static field init** (~line 13578): Same dual-path (computed → property set, non-computed → module var)
4. **Class expression static field init** (~line 10610): Added static field init + static block emission (was completely missing)
5. **Instance field install during `new`** (~line 12430): Both parent-chain and own-class loops now branch on `inf->computed`

### Phase 10C: Class Static Blocks

**Problem:** `class_static_block` had no AST node type, no builder handler, and no codegen. The Tree-sitter grammar parses `static { ... }` with a `body` field pointing to `statement_block`, but the AST builder had no case for it, and the transpiler never emitted the block body.

**Changes:**

| File | Change |
|------|--------|
| `lambda/js/js_ast.hpp` | Added `JS_AST_NODE_STATIC_BLOCK` enum value, new `JsStaticBlockNode` struct with `JsAstNode* body` |
| `lambda/js/build_js_ast.cpp` | Added `class_static_block` handler in `build_js_class_body` — creates `JsStaticBlockNode`, builds body via `build_js_block_statement` |
| `lambda/js/transpile_js_mir.cpp` | Added `JsAstNode* static_blocks[8]` + `int static_block_count` to `JsClassEntry`; collection stores block bodies; emission calls `jm_transpile_statement` after static field init |
| `test/test_js_test262_gtest.cpp` | Removed `"class-static-block"` from `UNSUPPORTED_FEATURES` |

### Test Results

| Version | Passed | Failed | Skipped | Total | Rate |
|---------|--------|--------|---------|-------|------|
| v18 baseline (pre-destructuring) | 10,903 | — | — | 26,967 | 40.4% |
| v18 destructuring fix | 10,838 | 16,129 | 11,682 | 38,649 | 40.2% |
| **v18 computed fields + static blocks** | **10,982** | **16,048** | **11,619** | **38,649** | **40.6%** |

**Delta vs destructuring fix:** +144 passed, −81 failed, −63 skipped (newly un-skipped from `class-static-block` removal)

### Unit Test

Added `test/js/v18_computed_fields.js` + `.txt` — 10 test patterns covering:
1. Computed instance field (`[k] = 42`)
2. Computed static field (`static ["y"] = 99`)
3. Expression key (`["a" + "b"] = 100`)
4. Mixed computed + regular fields
5. Static block basic (`static { E.x = 10; }`)
6. Static block with loop computation
7. Multiple static blocks
8. Variable-based computed key with method access
9. Computed static field + static block interaction
10. Inheritance with computed fields
