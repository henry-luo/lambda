# Gap Analysis: Lodash & Underscore Support in Lambda JS

## Summary

| Library | Version | Status | Tests Passed | Key Blocker |
|---------|---------|--------|-------------|-------------|
| **Underscore.js** | 1.13.7 | Partial | **100/114** (87.7%) | Bug 6 (ternary+typeof), `with` stmt |
| **Lodash** | 4.17.21 | **Fails to load** | 0/N | Captured var scope + regex + Bug 6 |

## Bugs Found

### Bug 6: Ternary inside `typeof` Guard Returns Raw Pointer (CRITICAL)

**Pattern that triggers the bug:**
```js
function test(idx) {
  var i = 0;
  if (typeof idx == 'number') {   // <-- typeof guard
    i = idx >= 0 ? idx : 0;       // <-- ternary assigns to DIFFERENT variable
  }
  return i;
}
test(0)  // => 5008080896 (should be 0) — returns raw MIR pointer!
test(2)  // => 5008080904 (should be 2)
```

**What works (contrast):**
```js
// Without typeof guard → OK
function test2(idx) { var i = 0; i = idx >= 0 ? idx : 0; return i; }
test2(0) // => 0 ✓

// With typeof guard + explicit if/else instead of ternary → OK
function test3(idx) {
  var i = 0;
  if (typeof idx == 'number') { if (idx >= 0) { i = idx; } else { i = 0; } }
  return i;
}
test3(0) // => 0 ✓

// Ternary assigning to SAME variable → OK
function test4(idx) {
  if (typeof idx == 'number') { idx = idx >= 0 ? idx : 0; }
  return idx;
}
test4(0) // => 0 ✓
```

**Root cause hypothesis:** The MIR transpiler's type specialization after a `typeof` check narrows the variable's type representation (e.g., unboxes to raw integer). When the ternary result is assigned to a *different* variable, the raw/unboxed value leaks through instead of being re-boxed to a JS value.

**Impact on underscore (8 failures):**
- `_.contains` / `_.includes` — uses `_.indexOf` with numeric `fromIndex`
- `_.without` — delegates to `_.difference` which uses `_.contains`
- `_.uniq` — uses internal comparison that depends on `_.contains`
- `_.union`, `_.intersection`, `_.difference` — all use `_.contains`
- `_.omit` — uses internal predicate with same pattern
- Potentially `_.bind` (underscore's executeBound has typeof checks)
- Potentially `_.max(arr, iteratee)` → returns `null`

**Impact on lodash:** Would affect many internal functions if lodash could load.

---

### Bug 7: `with` Statement Not Implemented

The `with` statement is parsed by Tree-sitter (AST node type `JS_AST_NODE_WITH_STATEMENT`) but the MIR transpiler logs `unsupported statement type 58` and skips it.

```js
var obj = {name: "world"};
var result;
with(obj) { result = name; }  // result is undefined, should be "world"
```

**Impact on underscore (3 failures):**
- `_.template("hello <%= name %>")` → compiled template uses `with(obj||{}){...}` internally
- All three template tests fail: `template-basic`, `template-escape`, `template-evaluate`

**Impact on lodash:** Same — `_.template()` would fail.

**Note:** `with` is deprecated in strict mode and many modern codebases avoid it. Lodash and underscore are the main users (for template functions). Low priority unless template support is needed.

---

### Bug 8: `[object Symbol]` toString Tag Returns `[object Number]`

```js
Object.prototype.toString.call(Symbol("x"))
// Actual:   "[object Number]"
// Expected: "[object Symbol]"
```

**Root cause:** In `js_runtime.cpp` `JS_BUILTIN_OBJ_TO_STRING`, Symbol primitives are not detected and fall through to the Number path (both are stored as tagged values with similar internal representations).

**Impact (1 failure each):**
- Underscore: `_.isSymbol(Symbol("x"))` → false
- Lodash: `_.isSymbol(Symbol("x"))` → false

---

### RE2 Negative Lookahead Limitation (Lodash-specific)

RE2 does not support `(?!...)` negative lookahead or `(?=...)` lookahead, which lodash uses for property path parsing:

```
// Failed regex compilations:
/\.|\[(?:[^[\]]*|(["'])(?:(?!\1)[^\\]|\\.)*?\1)\]/
/[^.[\]]+|\[(?:(-?\d+(?:\.\d+)?)|(["'])((?:(?!\2)[^\\]|\\.)*?)\2)\]|(?=(?:\.|\[\])(?:\.|\[\]|$))/g
```

**Impact on lodash:**
- `_.toPath("a.b.c")` — property path parsing broken
- `_.get(obj, "a.b.c")` — nested property access
- `_.set(obj, "a.b.c", value)` — nested property setting
- `_.has(obj, "a.b.c")` — nested has-check

**Note:** This is a known RE2 limitation. Workarounds: implement a custom path parser, or add a PCRE2 fallback for specific patterns.

---

### Captured Variable Scope Resolution (Lodash-specific)

Seven variables from lodash's outer scope cannot be found by inner closures during MIR compilation:

```
js-mir: captured variable '_js_Et' not found in scope (in function '_js_anon98_60524')
js-mir: captured variable '_js_Mt' not found in scope (in function '_js_anon98_60524')
js-mir: captured variable '_js_sn' not found in scope (in function '_js_anon151_64534')
js-mir: captured variable '_js_an' not found in scope (in function '_js_anon151_64534')
js-mir: captured variable '_js_ln' not found in scope (in function '_js_anon151_64534')
js-mir: captured variable '_js_En' not found in scope (in function '_js_anon177_70448')
js-mir: captured variable '_js_Rn' not found in scope (in function '_js_anon177_70448')
```

These correspond to:
| Minified | Purpose |
|----------|---------|
| `Et` | Regex for property path parsing |
| `Mt` | Regex `/\\(\\)?/g` for backslash unescaping |
| `sn`, `an`, `ln` | Bit flags: `WRAP_CURRY_FLAG=4`, `WRAP_BIND_FLAG=1`, `WRAP_BIND_KEY_FLAG=2` |
| `En`, `Rn` | Constants: `CLONE_DEEP_FLAG=3`, clone depth `=1` |

**Root cause:** Lodash defines these in a deeply nested scope (inside its `runInContext` factory function, ~58K chars into the IIFE). The transpiler fails to resolve the closure chain for inner functions that reference these outer variables. This is similar to the Bug 5 pattern fixed previously for highlight.js.

**Impact:** Lodash fails to load at runtime with `Uncaught ReferenceError: yl is not defined` (cascade failure).

---

## Underscore.js Detailed Test Results

### Passing (100/114)

All core functionality works:
- **Type checking**: isArray, isObject, isFunction, isString, isNumber, isBoolean, isDate, isRegExp, isNull, isUndefined, isNaN, isFinite, isEmpty, isElement, isError, isMap, isSet, isWeakMap, isWeakSet, isTypedArray, isArrayBuffer ✅
- **Collection**: each, map, reduce, reduceRight, find, filter, reject, every, some, pluck, max, min, sortBy, groupBy, countBy, size, partition, invoke ✅
- **Array**: first, last, rest, initial, compact, flatten, flatten-shallow, indexOf, lastIndexOf, sortedIndex, findIndex, range, chunk ✅
- **Object**: keys, allKeys, values, pairs, invert, extend, extendOwn, defaults, clone, has, property, propertyOf, matcher, isEqual, isEqual-deep, isMatch, pick, mapObject, get-nested ✅
- **Function**: partial, memoize, once, negate, compose, after, before ✅
- **Utility**: identity, constant, noop, times, random, uniqueId, escape, unescape, result ✅
- **Chaining**: chain (map+filter) ✅
- **Other**: where, findWhere, sample, shuffle, toArray ✅

### Failing (14/114)

| Test | Error | Root Cause |
|------|-------|------------|
| `isSymbol` | Returns false | Bug 8: `[object Symbol]` → `[object Number]` |
| `contains` | Returns false | Bug 6: ternary+typeof in `_.indexOf` |
| `without` | Returns original array | Bug 6: cascades from `_.contains` |
| `uniq` | Returns original array | Bug 6: cascades from `_.contains` |
| `union` | Returns wrong result | Bug 6: cascades from `_.contains` |
| `intersection` | Returns wrong result | Bug 6: cascades from `_.contains` |
| `difference` | Returns wrong result | Bug 6: cascades from `_.contains` |
| `zip` | Returns `[]` | Likely Bug 6 or related (`_.max` with iteratee) |
| `unzip` | Returns `[]` | Same as zip |
| `omit` | Returns original object | Bug 6: internal predicate comparison |
| `bind` | Returns `[object Object]` | Likely Bug 6 (typeof guard in executeBound) |
| `template-basic` | Returns `""` | Bug 7: `with` statement |
| `template-escape` | Returns `""` | Bug 7: `with` statement |
| `template-evaluate` | Returns `""` | Bug 7: `with` statement |

## Priority Ranking

| Priority | Bug | Effort | Impact |
|----------|-----|--------|--------|
| **P0** | Bug 6: Ternary+typeof | Medium | Fixes 8-11 underscore failures, unblocks lodash patterns |
| **P1** | Captured var scope | Medium | Required for lodash to load |
| **P2** | RE2 lookahead | Hard | Required for lodash `_.get`/`_.set`/`_.toPath` |
| **P3** | Bug 8: Symbol tag | Easy | 1 failure each in underscore + lodash |
| **P4** | Bug 7: `with` stmt | Medium | 3 template failures (can workaround with `_.templateSettings.variable`) |

## Comparison with Previous Libraries

| Library | Status | Tests | Blocker Category |
|---------|--------|-------|------------------|
| **jQuery 3.7.1** | ✅ Works | Full DOM support | (DOM shims needed) |
| **highlight.js 11.9** | ✅ Works (after Bug 1-5) | Full | Transpiler scope bugs |
| **Underscore 1.13.7** | ⚠️ Partial | 100/114 (87.7%) | Transpiler Bug 6, `with`, Symbol |
| **Lodash 4.17.21** | ❌ Fails to load | 0/N | Scope + regex + Bug 6 |

## Reproduction

```bash
# Underscore test (100/114 pass)
./lambda.exe js temp/underscore_combined.js --no-log

# Lodash test (fails to load)
./lambda.exe js temp/lodash_load_only.js --no-log

# Bug 6 minimal reproduction
./lambda.exe js temp/ternary_bug3.js --no-log
```

## Test Files

- `temp/underscore.min.js` — Underscore 1.13.7 minified
- `temp/lodash.min.js` — Lodash 4.17.21 minified
- `temp/underscore_combined.js` — Underscore + 114 test cases
- `temp/lodash_combined.js` — Lodash + 170+ test cases
- `temp/ternary_bug3.js` — Bug 6 minimal reproduction
- `temp/debug_core.js` — Core JS feature tests
