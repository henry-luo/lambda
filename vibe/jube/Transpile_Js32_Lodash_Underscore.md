# Gap Analysis: Lodash & Underscore Support in Lambda JS

## Summary

| Library | Version | Status | Tests Passed | Key Blocker |
|---------|---------|--------|-------------|-------------|
| **Underscore.js** | 1.13.7 | ✅ **Full** | **114/114** (100%) | All resolved |
| **Lodash** | 4.17.21 | ✅ **Near-Full** | **34/34** basic tests | `_.omit` undefined (scope), float display cosmetic |

## Bugs Found

### Bug 6: Ternary inside `typeof` Guard Returns Raw Pointer — ✅ FIXED

**Status:** Fixed in `transpile_js_mir.cpp` — the MIR transpiler's `typeof` type-narrowing pipe optimization was emitting unboxed values that leaked through ternary assignments to different variables. Fix ensures ternary results are always re-boxed.

**Impact resolved:** 10 underscore failures (contains, without, uniq, union, intersection, difference, zip, unzip, omit, isSymbol).

---

### Bug 7: `with` Statement Not Implemented — ✅ FIXED

**Status:** Fully implemented. Changes across 4 files:
- `js_ast.hpp` — Added `JsWithStatementNode` struct with `object` + `body` fields
- `build_js_ast.cpp` — Parse `with_statement` CST node, extract object expression (unwrap parenthesized_expression) and body
- `js_globals.cpp` — Added `js_with_push()`/`js_with_pop()` with a 16-deep scope stack; `js_get_global_property()` and `js_get_global_property_strict()` check with-scope before global object
- `transpile_js_mir.cpp` — Emit `js_with_push(obj)` before body, `js_with_pop()` after; added `WITH_STATEMENT` to all 7 analysis passes
- `sys_func_registry.c` — Registered `js_with_push`/`js_with_pop` in JIT runtime imports

**Impact resolved:** 3 underscore template failures (template-basic, template-escape, template-evaluate).

---

### Bug 8: `[object Symbol]` toString Tag Returns `[object Number]` — ✅ FIXED

**Status:** Fixed in `js_runtime.cpp` — added Symbol type detection in `JS_BUILTIN_OBJ_TO_STRING` before the Number path.

**Impact resolved:** 1 underscore failure (isSymbol).

---

### Bug 9: Transpiler `.bind()` Shortcut Intercepts User-Defined `.bind()` — ✅ FIXED

The MIR transpiler unconditionally intercepted ALL `.bind()` method calls and compiled them as `js_func_bind()`, bypassing runtime property lookup. This meant `_.bind(fn, ctx)` was treated as `Function.prototype.bind` instead of calling underscore's own `bind` method stored as an own property on the `_` function object.

**Fix:** Removed the transpiler shortcut in `transpile_js_mir.cpp`. The runtime's `js_map_method` cascade already handles `Function.prototype.bind` correctly for actual function objects, and own properties are checked first via `properties_map`.

**Impact resolved:** 1 underscore failure (bind).

---

### Bug 10: Call Expression Name Collision in Scope Lookup — ✅ FIXED

**Status:** Fixed in `transpile_js_mir.cpp` — the transpiler's `js_scope_lookup` resolved a call expression `t(...)` to a wrong outer-scope function instead of a local parameter `t`. When lodash's minified code called `t(e, o, r(o), n)` inside a higher-order function, the transpiler matched `t` to an unrelated top-level function definition rather than the `t` parameter passed to the closure.

**Fix:** Ensured call expression name resolution checks the local scope (parameters, let/var bindings) before searching outer scopes, so local parameter `t` correctly shadows any outer function named `t`.

**Impact resolved:** Lodash `runInContext` factory function now executes correctly. Previously caused cascade failures in array/collection operations.

---

### Bug 11: `Object()` Indirect Call Returns Undefined — ✅ FIXED

**Status:** Fixed in `js_globals.cpp` — the `Object` constructor placeholder (`js_ctor_placeholder`) was a no-op that returned undefined when called as a function via indirect invocation `Object(value)`. Lodash uses this pattern extensively for type coercion.

**Fix:** Replaced `js_ctor_placeholder` with `js_ctor_object_fn` which properly wraps primitive values into objects or returns object arguments unchanged, matching the ECMAScript `Object()` specification.

**Impact resolved:** Lodash now loads and initializes successfully. Previously failed with `Uncaught ReferenceError` cascade.

---

### Bug 12: `Array()` Indirect Call Returns Null — ✅ FIXED

**Status:** Fixed in `js_globals.cpp` — when `Array` is called through a variable alias (e.g., `var il = Array; il(3)`), the generic function call path uses the constructor's `func_ptr`. The `JS_CTOR_ARRAY` case fell through to `js_ctor_placeholder()` which returned `ItemNull`.

**Fix:** Added `js_ctor_array_fn(Item arg)` which delegates to `js_array_new(0)` for zero args or `js_array_new_from_item(arg)` for one arg. Wired into `js_create_constructor()` for `JS_CTOR_ARRAY`.

**Impact resolved:** Lodash uses `var il = Array; il(n)` extensively in its internal `baseSlice`, `baseRange`, and array creation functions. This single fix resolved 15+ lodash operations: `_.drop`, `_.take`, `_.chunk`, `_.range`, `_.uniq`, `_.union`, `_.difference`, `_.intersection`, `_.without`, `_.pick`, `_.assign`, `_.defaults`, `_.sortBy`, `_.zip`, `_.flattenDeep`.

---

### Bug 13: `var` Hoisting Register Mismatch in Loops — ✅ FIXED

**Status:** Fixed in `transpile_js_mir.cpp` — when `var computed = current;` appeared inside an `if` block inside a `while` loop, `jm_new_reg()` created a NEW unique MIR register (with counter suffix). The prologue hoisting had already created a different register for `computed` initialized to undefined. Code compiled BEFORE the var declaration (like `computed === undefined` in the while condition) referenced the OLD hoisted register, which was never updated.

**Fix:** In `jm_transpile_var_decl()`, for `var` declarations where the variable already exists from hoisting (checked via `from_hoist` flag on `JsMirVarEntry`), reuse the existing register and emit a MOV instead of creating a new register. The `from_hoist` flag distinguishes hoisted vars from function parameters — parameter registers must NOT be reused this way (attempting to do so produces corrupted values due to MIR parameter register semantics).

**Files changed:**
- `transpile_js_mir.cpp` — Added `from_hoist` field to `JsMirVarEntry`, set in all 4 hoisting locations (regular, native, generator, async functions), added early-exit var reuse path in `jm_transpile_var_decl()`

**Impact resolved:** `_.min` and `_.max` now return correct results. Previously `_.min([1,2,3,4])` returned `4` because lodash's internal `baseReduce` uses `var computed` inside a while-loop with a conditional initialization pattern.

---

### RE2 Negative Lookahead Limitation (Lodash-specific)

RE2 does not support `(?!...)` negative lookahead or `(?=...)` lookahead, which lodash uses for property path parsing:

```
// Failed regex compilations:
/\.|\[(?:[^[\]]*|(["'])(?:(?!\1)[^\\]|\\.)*?\1)\]/
/[^.[\]]+|\[(?:(-?\d+(?:\.\d+)?)|(["'])((?:(?!\2)[^\\]|\\.)*?)\2)\]|(?=(?:\.|\[\])(?:\.|\[\]|$))/g
```

**Observed impact on lodash (less severe than expected):**
- `_.get({a: {b: 42}}, 'a.b')` → `42` ✅ (works — lodash has fallback path parsing)
- `_.has({a: {b: 1}}, 'a.b')` → `true` ✅ (works)
- `_.get({a: 1}, 'b.c', 'default')` → `'default'` ✅ (works)
- `_.toPath("a.b.c")` — may be affected for complex paths with brackets/quotes

**Note:** This is a known RE2 limitation. Basic dotted-path access works because lodash's `isKey()` function has a simple-path fast path (`/^\w*$/` test) that bypasses the complex regex. Only bracket-notation paths like `a[0].b["c"]` would fail. Workarounds: implement a custom path parser, or add a PCRE2 fallback for specific patterns.

---

### Captured Variable Scope Resolution (Lodash-specific) — Mostly Resolved

**Current status:** Lodash now **loads and runs** with most operations working (Bug 10-13 fixes). A few captured variables from lodash's deeply nested `runInContext` scope still trigger warnings but have minimal observable impact:

```
js-mir: captured variable '_js_Et' not found in scope (in function '_js_anon98_60524')
js-mir: captured variable '_js_Mt' not found in scope (in function '_js_anon98_60524')
```

These correspond to:
| Minified | Purpose | Observable Impact |
|----------|---------|-------------------|
| `Et` | Regex for property path parsing | Bracket-notation paths may fail |
| `Mt` | Regex `/\\(\\)?/g` for backslash unescaping | Template string escaping |

**Previously blocking (now resolved by Bug 12):** The `sn`/`an`/`ln` bit flags and `En`/`Rn` clone constants were listed as blockers, but the actual root cause of `_.drop`/`_.take`/`_.chunk` returning null was Bug 12 (Array indirect call), not captured var scope. With Bug 12 fixed, these operations all work correctly.

**Remaining impact:** `_.omit` is `undefined` at runtime (not registered on the lodash object). This may be related to remaining captured variable scope issues in the `omit` registration path. Low priority — all other tested operations work.

---

## Underscore.js Detailed Test Results

### ✅ All 114/114 Tests Passing

- **Type checking**: isArray, isObject, isFunction, isString, isNumber, isBoolean, isDate, isRegExp, isNull, isUndefined, isNaN, isFinite, isEmpty, isElement, isError, isMap, isSet, isWeakMap, isWeakSet, isTypedArray, isArrayBuffer, **isSymbol** ✅
- **Collection**: each, map, reduce, reduceRight, find, filter, reject, every, some, pluck, max, min, sortBy, groupBy, countBy, size, partition, invoke, **contains** ✅
- **Array**: first, last, rest, initial, compact, flatten, flatten-shallow, indexOf, lastIndexOf, sortedIndex, findIndex, range, chunk, **without, uniq, union, intersection, difference, zip, unzip** ✅
- **Object**: keys, allKeys, values, pairs, invert, extend, extendOwn, defaults, clone, has, property, propertyOf, matcher, isEqual, isEqual-deep, isMatch, pick, **omit**, mapObject, get-nested ✅
- **Function**: **bind**, partial, memoize, once, negate, compose, after, before ✅
- **Utility**: identity, constant, noop, times, random, uniqueId, escape, unescape, result ✅
- **Template**: **template-basic, template-escape, template-evaluate** ✅
- **Chaining**: chain (map+filter) ✅
- **Other**: where, findWhere, sample, shuffle, toArray ✅

### Previously Failing (14 → 0)

| Test | Bug | Fix |
|------|-----|-----|
| `isSymbol` | Bug 8 | Symbol toString tag detection |
| `contains`, `without`, `uniq`, `union`, `intersection`, `difference`, `zip`, `unzip`, `omit` | Bug 6 | Ternary+typeof re-boxing |
| `bind` | Bug 9 | Removed `.bind()` transpiler shortcut |
| `template-basic`, `template-escape`, `template-evaluate` | Bug 7 | `with` statement implementation |

## Lodash 4.17.21 Detailed Test Results

### Test File: `test/js/lib_lodash.js` (303 lines, 34 test sections, 75 output lines)

GTest: **PASS** (126/126 JS tests including lodash) — batch timeout increased to 60s for large libraries (~35s in debug build, 702 MIR functions, 272K instructions).

### ✅ Working Operations (33/34 test sections correct)

- **Library**: Loads successfully, version `4.17.21`, all 13 core API types verified (`map`, `filter`, `reduce`, `forEach`, `find`, `sortBy`, `groupBy`, `clone`, `cloneDeep`, `debounce`, `throttle`)
- **Array**: compact ✅, flatten (shallow) ✅, **drop** ✅, **take** ✅, head ✅, last ✅, reverse ✅, fill ✅, indexOf ✅
- **Collection**: map ✅, filter ✅, reduce ✅, find ✅, some ✅, every ✅, forEach ✅, size ✅, includes ✅, map with property shorthand ✅
- **Object**: keys ✅, values ✅, has (dotted path) ✅, get (dotted path) ✅, get with default ✅, clone ✅, cloneDeep ✅, isEmpty ✅
- **String**: camelCase ✅, kebabCase ✅, snakeCase ✅, capitalize ✅, trim ✅, repeat ✅, escape ✅, unescape ✅, pad ✅, padStart ✅, padEnd ✅, startsWith ✅, endsWith ✅
- **Utility**: identity ✅
- **Lang**: isArray ✅, isObject ✅, isString ✅, isNumber ✅, isFunction ✅, isNil ✅
- **Function**: once ✅, negate ✅
- **Math**: clamp ✅, sum ✅, max ✅, **min** ✅
- **Lang conversions**: toNumber ✅, toInteger ✅, toString ✅

### ⚠️ Known Incorrect Results (1/34 test sections)

| Operation | Expected | Actual | Root Cause |
|-----------|----------|--------|------------|
| `_.flatten([1,[2,[3,[4]],5]])` | `[1,2,3,4,5]` (deep) | `[1,2,[3,[4]],5]` (shallow only) | `_.flatten` is shallow by default in lodash 4.x (correct behavior — not a bug) |

### ✅ Operations Verified Working (beyond GTest — probed with debug scripts)

| Operation | Result | Status |
|-----------|--------|--------|
| `_.chunk([1,2,3,4,5], 2)` | `[[1,2],[3,4],[5]]` | ✅ Fixed (Bug 12) |
| `_.range(5)` | `[0,1,2,3,4]` | ✅ Fixed (Bug 12) |
| `_.uniq([2,1,2])` | `[2,1]` | ✅ Fixed (Bug 12) |
| `_.union([2],[1,2])` | `[2,1]` | ✅ Fixed (Bug 12) |
| `_.difference([2,1],[2,3])` | `[1]` | ✅ Fixed (Bug 12) |
| `_.intersection([2,1],[2,3])` | `[2]` | ✅ Fixed (Bug 12) |
| `_.without([2,1,2,3],1,2)` | `[3]` | ✅ Fixed (Bug 12) |
| `_.sortBy(users, 'age')` | Correct ordering | ✅ Fixed (Bug 12) |
| `_.pick(obj, ['a'])` | `{"a":1}` | ✅ Fixed (Bug 12) |
| `_.assign({}, {a:1})` | `{"a":1}` | ✅ Fixed (Bug 12) |
| `_.defaults({a:1}, {a:3,b:3})` | `{"a":1,"b":3}` | ✅ Fixed (Bug 12) |
| `_.zip([1,2],[3,4])` | `[[1,3],[2,4]]` | ✅ Fixed (Bug 12) |
| `_.flattenDeep([1,[2,[3,[4]],5]])` | `[1,2,3,4,5]` | ✅ Works |
| `_.groupBy([4.2,6.1,6.2], Math.floor)` | Correct grouping | ✅ Works |
| `_.min([1,2,3,4])` | `1` | ✅ Fixed (Bug 13) |
| `_.max([1,2,3,4])` | `4` | ✅ Works |

### ❌ Known Remaining Issues

| Operation | Result | Root Cause |
|-----------|--------|------------|
| `_.omit(obj, ['a'])` | `undefined` (not a function) | `_.omit` not registered on lodash object — captured var scope issue in registration path |

## Priority Ranking (Remaining — Lodash)

| Priority | Bug | Effort | Impact |
|----------|-----|--------|--------|
| **P1** | `_.omit` not registered (captured var scope) | Medium | `_.omit` returns `undefined` |
| **P2** | RE2 lookahead | Hard | Bracket-notation paths only (dotted paths work) |
| ~~P1~~ | ~~Captured var scope (array ops)~~ | ~~Medium~~ | ✅ Fixed (Bug 12 — Array indirect call) |
| ~~P2~~ | ~~Set/cache operations~~ | ~~Medium~~ | ✅ Fixed (Bug 12) |
| ~~P3~~ | ~~`_.min` comparison bug~~ | ~~Easy~~ | ✅ Fixed (Bug 13 — var hoisting) |
| ~~P4~~ | ~~Object spread ops~~ | ~~Medium~~ | ✅ Fixed (Bug 12) |
| ~~P0~~ | ~~Bug 6: Ternary+typeof~~ | ~~Medium~~ | ✅ Fixed |
| ~~P3~~ | ~~Bug 8: Symbol tag~~ | ~~Easy~~ | ✅ Fixed |
| ~~P4~~ | ~~Bug 7: `with` stmt~~ | ~~Medium~~ | ✅ Fixed |
| ~~—~~ | ~~Bug 9: `.bind()` shortcut~~ | ~~Easy~~ | ✅ Fixed |
| ~~—~~ | ~~Bug 10: Call name collision~~ | ~~Medium~~ | ✅ Fixed |
| ~~—~~ | ~~Bug 11: `Object()` indirect call~~ | ~~Easy~~ | ✅ Fixed |
| ~~—~~ | ~~Bug 12: `Array()` indirect call~~ | ~~Easy~~ | ✅ Fixed |
| ~~—~~ | ~~Bug 13: `var` hoisting register~~ | ~~Medium~~ | ✅ Fixed |

## Comparison with Previous Libraries

| Library | Status | Tests | Blocker Category |
|---------|--------|-------|------------------|
| **jQuery 3.7.1** | ✅ Works | Full DOM support | (DOM shims needed) |
| **highlight.js 11.9** | ✅ Works (after Bug 1-5) | Full | Transpiler scope bugs |
| **Underscore 1.13.7** | ✅ **Full** | **114/114** (100%) | All bugs fixed (Bug 6-9) |
| **Lodash 4.17.21** | ✅ **Near-Full** | **34/34** basic tests + 16 probed ops | `_.omit` undefined (Bug 12-13 resolved rest) |

## Reproduction

```bash
# Underscore test (114/114 pass)
./lambda.exe js test/js/underscore_lib.js --no-log

# Lodash test (34/34 basic tests pass, ~35s debug build)
./lambda.exe js test/js/lib_lodash.js --no-log

# GTest (126/126 JS tests including lodash)
./test/test_js_gtest.exe --gtest_filter='*lib_lodash*'
./test/test_js_gtest.exe   # full suite
```

## Test Files

### Registered Tests (in GTest suite)
- `test/js/lib_lodash.js` + `test/js/lib_lodash.txt` — Lodash 4.17.21 unit test (303 lines, 34 sections, 75 output lines)
- `test/js/underscore_lib.js` + `test/js/underscore_lib.txt` — Underscore 1.13.7 unit test (415 lines, 114 sections)
- `test/js/array_ctor_indirect.js` + `.txt` — Array indirect call regression test (Bug 12)

### Temporary / Development Files
- `temp/underscore.min.js` — Underscore 1.13.7 minified
- `temp/lodash.min.js` — Lodash 4.17.21 minified
- `temp/underscore_combined.js` — Underscore + 114 test cases
- `temp/lodash_combined.js` — Lodash + 170+ test cases
- `temp/lodash_load_only.js` — Minimal lodash load test
- `temp/ternary_bug3.js` — Bug 6 minimal reproduction
- `temp/debug_core.js` — Core JS feature tests
