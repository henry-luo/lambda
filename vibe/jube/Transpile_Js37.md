# Transpile_Js37: Structural Engine Fixes & ES2020 Completion

## Current State

**Baseline**: 27,172 entries (26,107 in run set) | **Run set**: 34,094 | **Failing**: ~7,985 (76.6% pass rate)

### ES2020 Feature Status

| Feature | Run Set | Passing | Failing | Pass Rate | Status |
|---------|---------|---------|---------|-----------|--------|
| `optional-chaining` | 50 | 44 | 6 | 88% | Near-complete (blocked by private brand check) |
| `coalesce-expression` | 23 | 23 | 0 | 100% | ✅ Done |
| `dynamic-import` | 194 | 156 | 38 | 80% | Near-complete (syntax edge cases) |
| `for-in-order` | 9 | 2 | 7 | 22% | Property order non-compliant |
| `String.prototype.matchAll` | 15 | 9 | 6 | 60% | Missing toString coercion, prototype fallback |
| `globalThis` | 60 | 1 | 59 | 2% | Mostly eval-code/Iterator failures, not globalThis itself |
| `import.meta` | 5 | 1 | 4 | 20% | Syntax tests (SyntaxError in non-module context) |
| `Promise.allSettled` | 45 | 11 | 34 | 24% | Promise infrastructure gaps |
| `BigInt` | 1,202 | 239 | 963 | 20% | Mostly TypedArray+BigInt interactions |

**Key insight**: ES2020 failures are mostly caused by cross-cutting engine gaps (eval wrapping, property order, private brand checks, TypedArray BigInt) rather than missing ES2020 feature implementations. The features themselves are largely implemented.

---

## Remaining Failure Breakdown (Top Areas)

From the ~7,985 failing tests in the run set:

| Area | Failing | Notes |
|------|---------|-------|
| **intl402** | ~300 | Intl not implemented (out of scope) |
| **TypedArray + BigInt** | ~800+ | BigInt typed array constructors/methods |
| **eval-code** | ~480 | eval wraps as expression → parse error |
| **RegExp property escapes** | ~423 | Unicode property tables needed (HIGH effort) |
| **class/elements** | ~368 | $DONE async, private brand, propertyHelper |
| **Array.prototype ES5** | ~322 | hasOwnProperty not checked during iteration |
| **Promise** | ~163 | Infrastructure gaps (then chaining, constructor) |
| **RegExp.prototype methods** | ~147 | Symbol.replace/split/match compliance |
| **Atomics** | ~160 | BigInt atomics, wait/notify |
| **class/subclass** | ~68 | Non-Array builtin subclasses |
| **Function.prototype.toString** | ~73 | Source text reconstruction |
| **dynamic-import syntax** | ~35 | import-source/import-defer syntax (ES2024+) |

---

## Proposal: 6 Structural Fixes (~1,200 new passes)

### Part 1 — eval() Program Parsing (~350 tests)

#### Problem

`eval()` wraps code as an `expression_statement`, causing Tree-sitter parse errors. 87% of `eval-code/direct` failures (426 tests) show `[depth 0] node 'expression_statement' has error`.

```js
eval("var x = 1; function f() {}");
// Currently: parsed as expression(var x = 1; function f() {}) → parse error
// Expected: parsed as program(var_declaration, function_declaration)
```

Direct eval (`eval(code)`) and indirect eval (`(0,eval)(code)`) both exhibit this — 426 + 188 = 614 total eval-code failures.

#### Root Cause

In the transpiler, `eval()` calls feed the string to the parser wrapped in an expression context. The parser expects a single expression but receives statements (variable declarations, function declarations, if/for blocks).

#### Fix

Compile eval'd code as a **program** (script) context:
1. Parse the eval string as a full program (top-level statements)
2. Execute in the appropriate scope (direct eval: caller scope, indirect eval: global scope)
3. Return the completion value of the last statement

#### Expected Impact

~350 new passes (eval-code/direct: ~300, eval-code/indirect: ~50). Some tests will still fail due to scope resolution edge cases.

---

### Part 2 — Array Method hasOwnProperty (~250 tests)

#### Problem

ES5.1 Array methods (`forEach`, `map`, `filter`, `reduce`, `reduceRight`, `indexOf`, `lastIndexOf`, `some`, `every`, `find`, `findIndex`) don't check `hasOwnProperty` when iterating. Tests like `15.4.4.14-9-b-i-8.js` verify that array holes are skipped.

```js
var arr = [, , ,];
Array.prototype[1] = "proto";
// arr.indexOf("proto") should return -1 (hole, not own property)
// Currently returns 1 (reads from prototype)
```

322 tests under `built-ins/Array/prototype/*/15.4.4.*` fail because array iteration visits prototype-inherited values for holes.

#### Root Cause

Array iteration in `js_globals.cpp` uses direct index access without checking `hasOwnProperty`. Per spec, array methods should skip indices where `HasProperty(O, k)` returns false (holes).

#### Fix

In each Array.prototype method implementation, add `hasOwnProperty` check before processing each element:
```
for (k = 0; k < len; k++) {
    if (!js_has_own_property(arr, k)) continue;  // skip holes
    val = js_get_property(arr, k);
    ...
}
```

This is a pattern change across ~12 Array methods in `js_globals.cpp`.

#### Expected Impact

~250 new passes from `15.4.4.*` tests. Some additional passes from non-ES5 tests that also depend on hole behavior.

---

### Part 3 — Property Enumeration Order (~50 tests)

#### Problem

`for-in`, `Object.keys()`, `Object.values()`, `Object.entries()`, and `JSON.stringify()` don't follow the spec-mandated property order after deletions and re-additions.

```js
var o = { p1: 1, p2: 2, p3: 3 };
o.p4 = 4;
delete o.p1; delete o.p3;
o.p1 = 1;  // re-add
Object.keys(o);
// Expected: ['p2', 'p4', 'p1']  (insertion order, p1 moved to end)
// Actual:   ['p1', 'p2', 'p4']  (original position preserved)
```

7 `for-in-order` tests + ~40 related `Object.keys/values/entries` and `JSON.stringify` order tests.

#### Root Cause

The map/object property storage preserves original key positions even after delete+re-add. The spec requires that `delete` removes a key from the enumeration order and re-adding inserts it at the end.

#### Fix

In the map/object property storage (`Shape`/`ShapeEntry` system):
- When a key is deleted and re-added, it should get a new insertion-order position
- Integer-indexed properties always enumerate before string properties (ascending numeric order)
- String properties enumerate in insertion order

#### Expected Impact

~50 new passes across `for-in-order`, `Object.keys/values/entries`, `JSON.parse/stringify` order tests.

---

### Part 4 — Private Member Brand Check (~100 tests)

#### Problem

Private field/method access on the wrong object doesn't throw TypeError:
```js
class C { #x = 1; static check(obj) { return obj.#x; } }
C.check({});  // Should throw TypeError, currently returns undefined or errors differently
```

Tests expect `TypeError` with a "private" brand check, but the engine returns `"accessed private field from an ordinary object"` or no error at all. This affects optional-chaining private tests too.

#### Root Cause

Private field/method access in the transpiler doesn't verify that the receiver object has the correct class brand (was constructed by the class that defined the private member).

#### Fix

1. During class construction, stamp each instance with a brand identifier (e.g., `__brand_ClassName__`)
2. Before each private field/method access, check `js_has_brand(receiver, class_brand)`
3. If brand check fails, throw `TypeError: Cannot read private member from an object whose class did not declare it`

#### Expected Impact

~100 new passes from `class/elements` private-* tests + 4 `optional-chaining` tests that depend on brand checking.

---

### Part 5 — Subclass Builtin Objects (~40 tests)

#### Problem

`class Sub extends Map/Set/RegExp/Date/Promise { ... }` doesn't create proper builtin instances. Js36 fixed `extends Array` but other builtins still fail.

```js
class MyMap extends Map { constructor() { super(); } }
var m = new MyMap();
m.set("a", 1);  // TypeError or wrong behavior — no Map internals
```

46 `class/subclass/builtin-objects` + 20 `class/subclass-builtins` tests.

#### Root Cause

`js_constructor_create_object` only has special handling for Array extends (added in Js36). Other builtin constructors (Map, Set, RegExp, Date) aren't detected.

#### Fix

Extend the `super()` dispatch table from Js36:
```
if (superclass == Map) → this = js_map_new()
if (superclass == Set) → this = js_set_new()
if (superclass == RegExp) → this = js_regexp_create()
if (superclass == Date) → this = js_date_new()
if (superclass == Promise) → this = js_promise_create()
```

Then set `this.__proto__` to `SubClass.prototype`.

#### Expected Impact

~40 new passes from subclass-builtins tests.

---

### Part 6 — String.prototype.matchAll (~6 tests)

#### Problem

6 failing `matchAll` tests with specific issues:
- `toString-this-val.js`: doesn't call `ToString(this)` before using the value
- `regexp-is-undefined.js` / `regexp-is-null.js`: `assert` not defined (harness issue?)
- `regexp-matchAll-is-undefined-or-null.js`: wrong iterator toString tag
- `regexp-prototype-has-no-matchAll.js`: should throw TypeError when `Symbol.matchAll` absent
- `custom-matcher-emulates-undefined.js`: `Object.defineProperty` on non-object

#### Fix

1. Add `ToString(this)` coercion at start of `matchAll`
2. When regexp argument has no `Symbol.matchAll`, throw `TypeError`
3. Fix iterator `@@toStringTag` to return `"RegExp String Iterator"`

#### Expected Impact

~6 new passes (all remaining `matchAll` failures).

---

## Implementation Order

| Phase | Work | Est. Passes | Effort | Risk |
|---|---|---|---|---|
| **1. eval() program parsing** | Parser context change | ~350 | Hard | Medium (scope edge cases) |
| **2. Array hasOwnProperty** | Pattern in ~12 methods | ~250 | Medium | Low |
| **3. Property enum order** | Shape/property storage | ~50 | Medium | Medium (perf impact) |
| **4. Private brand check** | Brand stamp + check | ~100 | Medium | Low |
| **5. Subclass builtins** | Extend Js36 dispatch | ~40 | Easy | Low |
| **6. matchAll fixes** | 3 small fixes | ~6 | Easy | Low |

**Recommended order**: 2 → 5 → 6 → 4 → 3 → 1 (easiest/safest first, hardest last)

---

## Out of Scope (Future)

| Area | Failing | Why Deferred |
|------|---------|-------------|
| **TypedArray BigInt** | ~800 | BigInt64Array/BigUint64Array need full BigInt arithmetic integration |
| **RegExp property escapes** | ~423 | Unicode property table integration into regex engine |
| **intl402** | ~300 | Intl not implemented |
| **Promise infrastructure** | ~163 | Async microtask queue, proper then-chaining |
| **Atomics BigInt** | ~160 | BigInt atomics support |
| **Function.prototype.toString** | ~73 | Source text reconstruction from MIR |

---

## Verification

```bash
# Quick regression check
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only

# Full run to find improvements
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only

# Update baseline when regressions=0
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --update-baseline
```

**Target**: ~800 new passes → baseline from 27,172 to ~28,000 (run set pass rate: 76.6% → ~79%)
