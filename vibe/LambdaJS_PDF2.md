# LambdaJS PDF.js Phase 2: Structural Enhancement Proposal

## 1. Executive Summary

**Goal:** Systematically fix LambdaJS runtime gaps to significantly increase the PDF.js test pass rate, targeting **90%+ overall**.

**Original Baseline (2025-07-27):**

| Category | Passed | Total | Rate |
|----------|--------|-------|------|
| 10 running specs | 2,734 | 3,062 | 89.3% |
| 12 crashing specs | 0 | ~573 | 0% |
| **Combined** | **2,734** | **~3,635** | **75.2%** |

**Current State (2025-07-28) — After Phase 1 Implementation:**

| Category | Passed | Total | Rate |
|----------|--------|-------|------|
| 15 running specs | 2,840 | 3,373 | 84.2% |
| 7 crashing specs | 0 | ~253 | 0% |
| **Combined** | **2,840** | **~3,626** | **78.3%** |

**Latest State (2026-03-29) — After Phase 5: Spread Calls & Symbol Static Methods:**

| Category | Passed | Total | Rate |
|----------|--------|-------|------|
| 18 running specs | 3,122 | 3,421 | 91.3% |
| 4 crashing specs | 0 | ~105 | 0% |
| **Combined** | **3,122** | **~3,526** | **88.5%** |

**Progress:** +388 passing tests total (+13% combined rate vs baseline). 18 of 22 specs produce results. 2 more specs unblocked this session (xfa_formcalc: +99, xml: +14). 4 specs now run perfectly (type1_parser, pdf_find_utils, bidi\*, encodings).

**Previous State (2025-03-28) — After Phase 2/3/4 Partial Implementation:**

| Category | Passed | Total | Rate |
|----------|--------|-------|------|
| 16 running specs | 2,844 | 3,375 | 84.3% |
| 6 crashing specs | 0 | ~251 | 0% |
| **Combined** | **2,844** | **~3,626** | **78.4%** |

**Four-phase roadmap (updated with actuals):**

| Phase | Focus | Est. New Passes | Actual | Status |
|-------|-------|----------------|--------|--------|
| Phase 1 | Transpiler crash fixes | +200–400 | **+106** | **Partial** (5/12 unblocked) |
| Phase 2 | String & number primitives | +40–55 | **+1** | **Done** (codePointAt already worked) |
| Phase 3 | Runtime semantics (undefined, instanceof, RegExp) | +90–120 | **+3** | **Partial** (instanceof+Error done, undefined remaining) |
| Phase 4 | Collection iteration (Map/Set) | +20–30 | **+0** | **Partial** (Map for...of+order done, URL remaining) |
| Phase 5 | Spread calls & Symbol static methods | +100–200 | **+278** | **Done** |
| **Total** | | **+350–600** | **+388 so far** | **In progress** |

---

## 2. Current Scoreboard

### 2.1 Running Specs (18) — Updated 2026-03-29

| Spec | Passed | Failed | Total | Change from Baseline | Primary Failure Causes |
|------|--------|--------|-------|---------------------|------------------------|
| encodings_spec | 1807 | 0 | 1807 | — | — (perfect) |
| core_utils_spec | 865 | 12 | 878 | **+44** | Remaining edge cases |
| primitives_spec | 118 | 7 | 122 | **+56** | Dict iteration, undefined vs null (7 remaining) |
| type1_parser_spec | 24 | 0 | 24 | **+22 PERFECT** | — |
| function_spec | 77 | 37 | 114 | **+69** | PostScript compiler/evaluator stack returns |
| pdf_find_utils_spec | 22 | 0 | 22 | **+12 PERFECT** | — |
| unicode_spec | 23 | 3 | 26 | **+15** | Remaining Unicode category edge cases |
| util_spec | 32 | 20 | 52 | **+21** | UTF-16 BOM decoding, URL constructor, BaseException |
| bidi_spec | 9 | 1 | 10 | **+4** | 1 remaining Unicode bidi case |
| murmurhash3_spec | 0 | 7 | 7 | — | toString(16) negative numbers, bitwise hash values |
| colorspace_spec | 10 | 13 | 23† | **−22†** | *(regression: `ColorSpace.parse()` returns null; fewer tests run)* |
| cff_parser_spec | 15 | 25 | 40† | **+11** | *(exits after 40 tests — likely hang mid-suite)* |
| xfa_parser_spec | 6 | 111 | 117 | **+4** | Namespace resolution, undefined vs null |
| xfa_tohtml_spec | 1 | 49 | 50 | 0 | XFA rendering features |
| xfa_serialize_data_spec | 0 | 1 | 1 | — | XFA serialization |
| stream_spec | 0 | 1 | 1 | — | Stream implementation |
| **xfa_formcalc_spec** | **99** | **11** | **110** | **NEW +99** | *(was CRASH — Math.max spread fix unblocked; FormCalc lexer strings, subscript exprs)* |
| **xml_spec** | **14** | **1** | **15** | **NEW +14** | *(was CRASH — spread fix unblocked; 1 remaining XML edge case)* |

†colorspace_spec and cff_parser_spec run fewer tests than their full suite suggests they hang/early-exit due to a regression introduced in this session. Investigation needed.

### 2.2 Crashing Specs (4) — Down from 12

| Spec | ~Assertions | Crash Error | Root Cause | Status |
|------|------------|-------------|------------|--------|
| autolinker_spec | ~13 | `assignment to undefined var '_js_scale'` | Destructuring in constructor params (deeper pattern) | Remaining |
| crypto_spec | ~44 | `captured variable '_js_k' not found in scope` | Closure capture in scope | Remaining |
| default_appearance_spec | ~23 | `Repeated item declaration` | Shares colorspace dependency | Remaining |
| parser_spec | ~44 | `captured variable '_js_width' not found for class method` | Closure capture in class | Remaining |

**Previously crashing, now running:** xfa_formcalc_spec (99/110), xml_spec (14/15)

---

## 3. Root Cause Analysis

### 3.1 Diagnostic Test Results

Created and ran diagnostic test files (`temp/test_diag{5-8}.js`) to isolate specific JS feature gaps. Results:

| Feature | Status | Diagnostic Result |
|---------|--------|-------------------|
| `Number.toString(radix)` | ✅ FIXED | `(255).toString(16)` → `"ff"` (was broken: radix ignored) |
| `String.prototype.codePointAt` | ✅ WORKS | `"A".codePointAt(0)` → `65` (was reported missing, but already implemented) |
| `new RegExp(pattern)` | ✅ FIXED | `new RegExp("^[a-z]+$").test("hello")` → `true` (was broken) |
| `String.replaceAll(regex, fn)` | ✅ FIXED | Callback now invoked correctly (was returning `[object Object]`) |
| `undefined` vs `null` | ❌ CONFLATED | Missing property returns `null`, `=== undefined` fails |
| `instanceof` (user classes) | ✅ FIXED | `e instanceof MyError` → `true` (prototype chain walking via `__class_name__`) |
| `Error.message` propagation | ✅ FIXED | `new MyError("test").message` → `"test"` (super() for builtin Error classes) |
| `charCodeAt` (non-ASCII) | ✅ FIXED | Now returns UTF-16 code units (was returning UTF-8 bytes) |
| `String.fromCharCode(0)` | ✅ FIXED | NUL character now handled (was producing empty string) |
| `Map` for...of | ✅ FIXED | `for (var [k,v] of map)` now yields entries (js_iterable_to_array handles Map/Set) |
| `Map.forEach` order | ✅ FIXED | Iterates in insertion order (doubly-linked list in JsCollectionData) |
| Getter/setter (class) | ✅ FIXED | No longer crashes with `Repeated item declaration` |
| Closure capture (class) | ⚠️ PARTIAL | 7 specs unblocked; 4 specs still crash (deeper capture patterns) |
| Object destructuring | ✅ FIXED | Constructor/function destructuring patterns work |
| `Array.from(iter, mapFn)` | ✅ FIXED | Mapper function applied via `js_array_from_with_mapper` |
| Super() constructor chain | ✅ FIXED | 3-level inheritance works (was broken by P3 optimization) |
| Static class properties | ✅ FIXED | `ClassName.PROP` works (was returning null from MCONST_CLASS) |
| Class method scope | ✅ FIXED | Method names no longer pollute enclosing scope |
| `Math.max(...arr)` | ✅ FIXED | `Math.max(...[4,1,7,2])` → `7`; `Math.min(...arr)` → `1` (was returning NaN) |
| `fn(...arr)` spread call | ✅ FIXED | `sum3(...[1,2,3])` → `6` (was passing wrong args) |
| `obj.method(...arr)` spread | ✅ FIXED | Method calls with spread args now route correctly |
| `fn(...args); args.length` rest+spread | ❌ BUG | `test2(...[1,2,3])` where `test2(...args)` → `args.length===0` (should be 3). Root cause: `js_apply_function` unpacks array to 3 individual args; rest-param compiled as `param_count=1` → `_js_args = args[0] = 1` (scalar, not array) |
| Symbol-keyed static methods | ✅ FIXED | `Foo[Symbol.for('ns')]` static methods now registered/callable |
| UTF-16 BOM decoding | ❌ REMAINING | `stringToPDFString` passes BOM bytes through unprocessed for UTF-16 BE/LE strings |
| `Number.toString(16)` negative | ❌ REMAINING | Negative hash values produce negative hex, not two's complement |
| PostScript stack eval | ❌ REMAINING | Compiled PostScript functions return `[]` instead of stack result |
| `URL` constructor | ❌ REMAINING | `new URL("...")` not fully implemented |
| Dict/Map `.keys()` iteration | ❌ REMAINING | Returns `[]` instead of keys array in some patterns |
| `Map.size` | ✅ | Works correctly |
| `Set.has` / `Set.add` | ✅ | Works correctly |
| `for...of` on Array | ✅ | Works correctly |
| `Object.keys` | ✅ | Works correctly |
| `Object.assign` | ✅ | Works correctly |
| Array spread `[...arr]` | ✅ | Works correctly |
| Template literals | ✅ | Works correctly |
| Optional chaining `?.` | ✅ | Works correctly |
| `parseInt(str, radix)` | ✅ | Works correctly |
| `try/catch/throw` | ✅ | Works correctly |
| Static class fields | ✅ | Works correctly |
| Prototype-based classes | ✅ | get/set/has/forEach all work |

### 3.2 Root Cause Categories

**Category A: Transpiler Crashes (4 specs still blocked, ~105 assertions remaining)**

| ID | Issue | Status | Affected Specs | ~Assertions |
|----|-------|--------|---------------|-------------|
| A1 | Closure capture: outer-scope variables not visible in class method bodies | ⚠️ PARTIAL | ~~cff_parser~~, ~~xfa_formcalc~~, ~~xml~~, crypto, parser | ~~285~~ → ~88 remaining |
| A2 | Getter/setter naming collision | ✅ FIXED | ~~colorspace~~, default_appearance (still shares dep) | ~~99~~ → ~23 remaining |
| A3 | AST builder: `Failed to build function body` | ✅ FIXED | ~~xfa_parser, xfa_tohtml, xfa_serialize_data~~ | ~~174~~ → 0 |
| A4 | Destructuring in constructor: `{ scale, rotation }` param pattern | ✅ FIXED (general), but autolinker still crashes | autolinker | ~13 |
| A5 | Collection runtime: `js_collection_create` type confusion | ⚠️ Partial (stream now runs but 0/1 pass) | stream | ~2 |
| A6 | Super() constructor P3 optimization incompatible with inheritance | ✅ FIXED | type1_parser (2/24 → 24/24) | — |
| A7 | MCONST_CLASS returned null — static class properties broken | ✅ FIXED | function_spec (TIMEOUT → 77/114) | — |
| A8 | Class method names polluting enclosing scope | ✅ FIXED | Multiple specs affected | — |
| A9 | Class variable alias (`var X = _X`) not detected | ✅ FIXED | function_spec, others using esbuild alias pattern | — |
| **A10** | **Math.max/min/etc. with spread args** — `Math.max(...arr)` returned NaN | ✅ **FIXED** | ~~xfa_formcalc~~ (CRASH→99/110), ~~xml~~ (CRASH→14/15) | ~~+113~~ |
| **A11** | **General function spread calls** — `fn(...arr)` passed wrong args | ✅ **FIXED** | function_spec, xfa, util (+56 function, others) | ~~+56+~~ |
| **A12** | **Symbol-keyed static class methods** — `Foo[Symbol.xxx]` methods broken | ✅ **FIXED** | xfa_parser (+4 via Namespace lookup) | ~ |
| **A13** | **colorspace regression** — `ColorSpace.parse()` returns null; only 23/74 tests run | ❌ **NEW BUG** | colorspace_spec (32→10 passing, 65→23 running) | ~42 |
| **A14** | **Rest params via spread call** — `f(...args)` where `f(...rest)` has rest param → `rest.length===0` | ❌ **BUG** | Various spread+rest patterns | ~unknown |

**Category B: Missing/Broken JS Built-in APIs**

| ID | Issue | Status | Affected Tests |
|----|-------|--------|---------------|
| B1 | `Number.toString(radix)` ignores radix | ✅ FIXED | ~~escapePDFName (5), stringToUTF16HexString (4), murmurhash3 (5)~~ |
| B2 | `String.prototype.codePointAt` — was reported missing but already works | ✅ Already worked | ~~encodeToXmlString (2), unicode tests~~ |
| B3 | `String.replaceAll(regex, fn)` — callback not invoked | ✅ FIXED | ~~escapeString (1), text processing~~ |
| B4 | `String.fromCharCode(0)` — NUL char produces empty string | ✅ FIXED | ~~bytesToString (3), binary patterns~~ |
| B5 | `charCodeAt` returns UTF-8 bytes not UTF-16 code units for non-ASCII | ✅ FIXED | ~~stringToPDFString (8), getModificationDate (2), stringToUTF16String (4)~~ |
| B6 | `Array.from(iterable, mapFn)` — mapper argument ignored | ✅ FIXED | ~~minor impact~~ |
| **B7** | **`Number.toString(16)` for negative numbers** — should produce two's-complement hex | ❌ Remaining | murmurhash3 (7), bitwise hash outputs |
| **B8** | **UTF-16 BOM decoding** — `stringToPDFString` passes BOM bytes `0xfe 0xff` through instead of consuming and decoding as UTF-16 BE/LE | ❌ Remaining | util stringToPDFString (6+), PDF text extraction |
| **B9** | **`bytesToString` type check** — should throw `InvalidArgumentException` for non-Uint8Array input | ❌ Remaining | util bytesToString (1) |

**Category C: Runtime Semantics**

| ID | Issue | Status | Affected Tests |
|----|-------|--------|---------------|
| C1 | `undefined` vs `null` conflation — missing property returns `null` not `undefined` | ❌ Remaining | primitives Dict (7 remaining), util, general |
| C2 | `instanceof` always false for user-defined classes | ✅ FIXED | ~~BaseException (2), isDict (1), type checks~~ |
| C3 | `Error` class: `super(msg)` doesn't set `.message`, inheritance broken | ✅ FIXED | ~~BaseException (5), toThrow patterns~~ |
| C4 | `new RegExp(pattern, flags)` — dynamic regex construction broken | ✅ FIXED | ~~validateCSSFont (10), recoverJsURL (7), isAscii (2), bidi (5), pdf_find_utils (8)~~ |
| C5 | `expect().toThrow()` — BaseException class inheritance chain for instanceof | ❌ Remaining | util BaseException (4+) |
| **C6** | **`Math.max(...spread)`** — spread args returned NaN from `jm_transpile_math_call` | ✅ **FIXED** | ~~xfa_formcalc (root cause of CRASH), xml~~ |
| **C7** | **`fn(...spread)`** — general function spread calls broken | ✅ **FIXED** | ~~function_spec, xfa, others~~ |
| **C8** | **`obj.method(...spread)`** — method spread calls added | ✅ **FIXED** | Various method calls with spread |
| **C9** | **Rest params via spread** — `function f(...rest); f(...arr)` → `rest.length===0` | ❌ **BUG** | Patterns where rest-param functions are called via spread dispatch. Root cause: `js_apply_function` unpacks spread array to individual args; rest-param function compiled with `param_count=1` only receives `args[0]` as scalar, not the full array. |
| **C10** | **Symbol-keyed static methods** — `Foo[Symbol.for('x')] = fn` and `Foo[computed] = fn` | ✅ **FIXED** | ~~xfa_parser namespace lookup (+4)~~ |
| **C11** | **PostScript stack returns** — `PostScriptCompiler`-compiled functions return `[]` instead of stack values | ❌ Remaining | function_spec PostScriptCompiler/Evaluator (37 failures) |

**Category D: Collection/Iterator Gaps**

| ID | Issue | Affected Tests |
|----|-------|---------------|
| D1 | `Map[Symbol.iterator]` / `for...of` on Map — no entries produced | ✅ FIXED — Dict forEach/iteration (5), RefSetCache (4) |
| D2 | `Map.forEach` iterates in LIFO order instead of insertion order | ✅ FIXED — parseXFAPath (1), ordering-sensitive tests |
| D3 | `URL` constructor not implemented (or incomplete) | ❌ Remaining — createValidAbsoluteUrl (8), util (8) |
| D4 | `ReadableStream` stub — `.getReader` returns object not function | ❌ — ReadableStream (1) |
| **D5** | **Dict/Map iteration for `keys()`/`values()`** — returns `[]` instead of entries | ❌ Remaining | primitives_spec Dict iteration (7), RefSet (2), RefSetCache (2) |
| **D6** | **xfa_parser namespace 111 failures** — complex class hierarchy, Symbol-keyed namespace lookup, `_nextNsId` still NaN-sensitive patterns | ❌ Remaining | xfa_parser (111), xfa_tohtml (49), xfa_serialize_data (1) |

---

## 3.3 Implementation Progress Log (2025-07-27 → 2025-07-28)

### Fixes Implemented — Prior Session (2025-07-27)

8 fixes from Phase 2/3 proposal items + additional discoveries:

| # | Fix | Category | Impact |
|---|-----|----------|--------|
| 1 | `Number.toString(radix)` — radix conversion for bases 2-36 | B1 | core_utils +5 |
| 2 | `new RegExp(pattern, flags)` — dynamic regex construction | C4 | pdf_find_utils +9, bidi +3, core_utils +12 |
| 3 | Getter/setter class declarations — disambiguated names | A2 | Unblocked colorspace_spec (32/74) |
| 4 | `charCodeAt` — UTF-8→UTF-16 code unit conversion | B5 | core_utils, util |
| 5 | `String.fromCharCode(0)` — NUL character support | B4 | util bytesToString |
| 6 | `String.replaceAll(regex, fn)` — callback invocation | B3 | core_utils escapeString |
| 7 | Escape sequences in string literals (`\xNN`, `\uNNNN`) | — | Multiple specs |
| 8 | 3 closure capture fixes (null fallback, parent index, destructuring locals) | A1 | Unblocked cff_parser_spec (4/69) |
| 9 | MIR type mismatch fixes (d2f, call double/int, prototype lookup registration) | — | Prevented crashes |
| 10 | Object destructuring support in function params | A4 | General |
| 11 | `jm_collect_body_refs` — missing node types | — | AST completeness |
| 12 | Scoped ancestor names fix for closure capture analysis | A1 | Closure correctness |
| 13 | LABELED_STATEMENT in `jm_collect_body_locals` | — | AST completeness |
| 14 | Static getter method transpilation | — | Getter methods |

### Fixes Implemented — This Session (2025-07-28)

| # | Fix | Files Modified | Impact |
|---|-----|---------------|--------|
| 1 | **Class method name scope pollution** — method names (parse, getToken, etc.) were registered in enclosing scope via `js_scope_define()`, shadowing module-level variables | `build_js_ast.cpp` | Prevented incorrect variable resolution |
| 2 | **P3 disable for inheritance hierarchies** — `js_set_shaped_slot` (slot-indexed writes) was breaking super() calls because child object shape didn't match parent constructor's expected slots | `transpile_js_mir.cpp` | type1_parser: 2/24 → 21/24 |
| 3 | **MCONST_CLASS returns runtime class object** — `MCONST_CLASS` was a null placeholder, meaning `ClassName.staticProp` always returned null. Now stores class object in module var with `module_var_index` | `transpile_js_mir.cpp` | function_spec: TIMEOUT → 21/114; unblocked all static property access |
| 4 | **IIFE body class writeback** — esbuild bundles wrap all code in `(() => { ... })()`. Class declarations inside IIFE bodies weren't being stored to module vars. Extended condition from `mt->in_main` to include `is_iife_body` | `transpile_js_mir.cpp` | Critical for all esbuild bundles |
| 5 | **Variable alias detection** — esbuild emits `var _X = class _X { }; var X = _X;`. The public name `X` wasn't recognized as a class, so `new X()` fell to generic constructor path | `transpile_js_mir.cpp` | Enabled class construction via alias names |

### Fixes Implemented — Session 3 (2025-03-28)

| # | Fix | Files Modified | Impact |
|---|-----|---------------|--------|
| 1 | **`Array.from` with mapper** — `Array.from(iter, mapFn)` now applies the mapper to each element via new `js_array_from_with_mapper` runtime function | `js_globals.cpp`, `transpile_js_mir.cpp`, `js_runtime.h`, `sys_func_registry.c` | Array.from mapper works |
| 2 | **`instanceof` for user-defined classes** — Prototype chain walking via `__class_name__` property. Each `new ClassName()` sets `__class_name__` on the instance and builds a `__proto__` chain with ancestor class names. `instanceof` either uses compile-time class name resolution (for known classes/aliases) or runtime `__proto__` chain walking | `js_globals.cpp`, `transpile_js_mir.cpp`, `js_runtime.h`, `sys_func_registry.c` | primitives_spec +2, util_spec +1 |
| 3 | **Error class inheritance** — `super()` in builtin Error subclasses (Error, TypeError, RangeError, SyntaxError, ReferenceError) now sets `this.message` and `this.name`. `js_new_error`/`js_new_error_with_name` set `__class_name__` for instanceof support | `transpile_js_mir.cpp`, `js_runtime.cpp` | Error.message/name propagation works |
| 4 | **Map for...of iteration** — `js_iterable_to_array` now detects Map/Set collections and calls `.entries()`/`.values()` to produce iterable arrays for `for...of` loops | `js_runtime.cpp` | Map for...of yields entries |
| 5 | **Map insertion order** — Added doubly-linked list (`JsCollectionOrderNode`) to `JsCollectionData` to track insertion order. `forEach`, `keys`, `values`, `entries` all iterate in FIFO order. `set` appends/updates, `delete` removes, `clear` resets | `js_runtime.cpp` | Map.forEach/iteration in correct order |
| 6 | **Import resolver registration** — Registered `js_array_from_with_mapper` and `js_instanceof_classname` in `jit_runtime_imports[]` so MIR JIT can resolve them at link time | `sys_func_registry.c` | Fixed "failed to resolve native fn/pn" errors |
| 7 | **`instanceof` compile-time class resolution** — `instanceof` with any known class name (including aliases like `var MyError = _MyError`) now resolves to `js_instanceof_classname` at compile time, avoiding null variable lookups for classes inside function bodies | `transpile_js_mir.cpp` | instanceof MyError works in IIFEs |
| 8 | **`codePointAt` verification** — Confirmed already working (was reported as missing in earlier diagnostics but implementation exists) | — | No code change needed |

### Fixes Implemented — Session 4 (2026-03-29) — Spread Calls & Symbol Static Methods

**Net impact: +278 passing tests. 2 more specs unblocked (xfa_formcalc: CRASH→99/110, xml: CRASH→14/15).**

| # | Fix | Files Modified | Root Cause | Impact |
|---|-----|---------------|------------|--------|
| 1 | **`Math.max(...spread)`** — `jm_transpile_math_call` was calling `jm_transpile_box_item(spread_element)` which returned the array as-is instead of expanding it. Added spread detection at top of math handler; builds spread array via `jm_build_spread_args_array` and dispatches to new `js_math_apply(name, array)` | `transpile_js_mir.cpp`, `js_runtime.cpp`, `js_runtime.h`, `sys_func_registry.c` | XFA Builder constructor used `Math.max(...Object.values(NamespaceIds).map(({id})=>id))` → returned NaN → `_nextNsId=NaN` → all namespace lookups broken | xfa_formcalc CRASH→99/110; xml CRASH→14/15; xfa_parser +4 |
| 2 | **`fn(...spread)` general function spread calls** — fallback (dynamic) call path now builds a spread-expanded JS array and uses `js_apply_function(callee, null_this, array)` instead of `jm_build_args_array + js_call_function` | `transpile_js_mir.cpp` | Stack-allocated args arrays can't accommodate dynamic-length spread | function_spec +56 |
| 3 | **`obj.method(...spread)` method spread calls** — method call path detects spread args; builds spread array; dispatches to new `js_method_call_apply(obj, name, array)` which type-routes to string/array/number/map method handlers | `transpile_js_mir.cpp`, `js_runtime.cpp`, `js_runtime.h`, `sys_func_registry.c` | Same as #2 | Method calls with spread now work |
| 4 | **`resolved_fn` + spread guard** — direct-call path (static dispatch by function name) nullifies `fc` when spread args detected, falls through to dynamic path | `transpile_js_mir.cpp` | Direct-call path built fixed-size arg arrays incompatible with spread | Prevents incorrect arg passing |
| 5 | **`jm_build_spread_args_array` helper** — new transpiler helper that builds a runtime-heap JS array from arg list, handling spread expansion via loop + `js_iterable_to_array` | `transpile_js_mir.cpp` | Shared by all three spread-call code paths | Foundation for all spread fixes |
| 6 | **Symbol-keyed static class methods** — `build_js_ast.cpp` `build_js_method_definition` now sets `method->computed = true` when key type is `computed_property_name`. Transpiler registers ALL static methods (including `Symbol`-keyed ones). `JsClassMethodEntry` struct gains `bool computed` + `JsAstNode* key_expr` fields | `build_js_ast.cpp`, `transpile_js_mir.cpp` | `method->computed` was never set → Symbol-keyed static methods like `[Symbol.iterator]` invisible to class dispatch | xfa_parser namespace Symbol lookups (+4) |

**Known regression introduced this session:**
- `colorspace_spec`: Dropped from ~32/65 → 10/23. `ColorSpace.parse()` now returns `null` for most tests. Likely caused by spread-call or method-call changes affecting an internal constructor/factory call in ColorSpace hierarchy. Needs investigation.

---

## 3.4 Architectural Findings — esbuild Bundle Patterns

Key discoveries about how esbuild-generated JavaScript bundles differ from hand-written JS, and how these patterns affect the LambdaJS transpiler:

### 3.4.1 IIFE Wrapping

All code in esbuild bundles is wrapped in an immediately-invoked function expression:
```js
(() => {
  // ALL module code lives here, not at top-level
  class MyClass { ... }
  var instance = new MyClass();
})();
```

**Implication:** Module-level variable storage (`js_set_module_var` / `js_get_module_var`) must work inside IIFE bodies, not just at the true top-level (`mt->in_main`). The transpiler needed a new `in_modvar_scope` flag that includes both `mt->in_main` and `is_iife_body`.

### 3.4.2 Class Name Aliasing

esbuild consistently emits a two-step class declaration pattern:
```js
var _PostScriptStack = class _PostScriptStack {
  constructor() { ... }
  static MAX_STACK_SIZE = 100;
};
var PostScriptStack = _PostScriptStack;  // alias
```

**Implication:** The transpiler must detect `var X = ClassName` assignments and register `X` as an alias for the internal class `_ClassName`. Without this, `new PostScriptStack()` doesn't find the constructor, and falls through to a generic object creation path that skips the constructor body entirely.

### 3.4.3 `__publicField` for Static Properties

esbuild compiles static class fields as:
```js
__publicField(ClassName, "MAX_STACK_SIZE", 100);
```

This calls at file scope (IIFE body scope). If `ClassName` resolves to null at runtime, the `__publicField` call is a silent no-op — no error, just the property never gets set. This was the root cause of the function_spec timeout: `_PostScriptStack.MAX_STACK_SIZE` was null → stack push overflow guard was broken → infinite loop.

### 3.4.4 MCONST_CLASS Must Be a Runtime Value

The original design treated `MCONST_CLASS` as a compile-time-only marker. But esbuild patterns require the class name to be a real runtime value (the class prototype object) because:
- Static property access: `ClassName.PROP`
- `__publicField(ClassName, ...)` at IIFE body scope
- `instanceof ClassName` checks
- `new ClassName()` construction

**Solution:** Each class gets a `module_var_index`. The class object is created during hoisting and stored in a module var. All three codegen paths for `MCONST_CLASS` (top-level, function body, call argument) now emit `js_get_module_var()` instead of null.

### 3.4.5 P3 Optimization vs Inheritance

The P3 optimization (`js_set_shaped_slot`) compiles constructor bodies to write properties by slot index rather than by name. This is fast but assumes the object's shape is known at compile time.

**Problem:** When class B extends A, and B's constructor calls `super()`, the parent A's constructor runs on B's `this` object. If A's constructor was compiled with P3 slot indices based on A's shape, but the actual object has B's shape (different slot layout), writes go to wrong slots or cause crashes.

**Solution:** Disable P3 for ALL constructors in any class that participates in an inheritance hierarchy (either as parent or child). The A5 optimization (pre-shaped object creation) still works because it shapes the object based on the leaf class.

---

## 4. Phased Implementation Plan

### Phase 1: Transpiler Crash Fixes — Unblock Remaining 7 Specs

**Status: PARTIALLY COMPLETE — 5 of 12 specs unblocked**

#### 1.1 Fix Closure Capture in Class Methods (A1) — PARTIALLY DONE

**Status:** 3 closure capture fixes implemented (null fallback, parent index, destructuring locals), plus scoped ancestor names fix. This unblocked cff_parser_spec (+4 passing), but 4 specs still crash with deeper capture patterns (crypto, parser, xfa_formcalc, xml).

**Remaining work:** The remaining crashes involve variables captured across multiple nesting levels or from non-ancestor scopes. Need to analyze the specific capture patterns in each failing bundle.

#### 1.2 Fix Getter/Setter Declaration Collision (A2) — DONE ✅

**Status:** Fixed. Getter/setter names are now disambiguated. colorspace_spec unblocked (32/74 passing). default_appearance_spec still crashes because it shares a colorspace dependency that has a different remaining issue.

#### 1.3 Fix AST Builder Failures (A3) — DONE ✅

**Status:** Fixed. All 3 AST builder failures resolved. xfa_parser_spec (2/117), xfa_tohtml_spec (1/50), xfa_serialize_data_spec (0/1) now run. Pass rates are low because these bundles have many other runtime issues (undefined vs null, Map iteration, etc.).

#### 1.4 Fix Object Destructuring in Constructor Params (A4) — DONE ✅ (general case)

**Status:** General object destructuring in function parameters implemented. However, autolinker_spec still crashes — may have a different remaining issue beyond basic destructuring.

#### 1.5 Fix Collection Runtime Bug (A5)

**Problem:** `js_collection_create` produces type confusion errors when certain Map/Set patterns are used.

**Location:** `lambda/js/js_runtime.cpp` — `js_collection_create`, `js_get_collection_data`

**Approach:**
1. Debug the `cd_val_tid=3` type confusion (expected collection descriptor, got wrong type)
2. Fix the tagging/untagging logic for collection items

**Affects:** stream (~2 assertions)

---

### Phase 2: String & Number Primitives — MOSTLY DONE

**Status: 5 of 6 items implemented.**

#### 2.1 Number.toString(radix) (B1) — DONE ✅

Radix conversion implemented for bases 2-36. `(255).toString(16)` → `"ff"`.

#### 2.2 String.prototype.codePointAt (B2) — ALREADY WORKS ✅

Was reported as returning null in earlier diagnostics, but re-testing confirms `"A".codePointAt(0)` → `65` works correctly. No code change needed.

#### 2.3 charCodeAt for Non-ASCII (B5) — DONE ✅

Now returns UTF-16 code units instead of UTF-8 byte values.

#### 2.4 String.fromCharCode(0) (B4) — DONE ✅

NUL character now produces a string of length 1.

#### 2.5 String.replaceAll with Callback (B3) — DONE ✅

Callback function is now invoked for each match.

---

### Phase 3: Runtime Semantics

**Goal:** Fix fundamental runtime behavior that many tests depend on.

#### 3.1 Dynamic RegExp Construction (C4) — DONE ✅

**Status:** Fixed. `new RegExp("pattern", "flags")` now works with `.test()`, `.exec()`, `.match()`. Contributed to pdf_find_utils (+9), bidi (+3), core_utils (+12) improvements.

#### 3.2 undefined vs null Distinction (C1)

**Problem:** LambdaJS uses `null` for both `null` and `undefined`. Missing object properties return `null` instead of `undefined`. This breaks `=== undefined` checks and `typeof x === "undefined"` patterns.

**Impact Estimate:** ~25 direct fixes across primitives (Dict tests), util.

**Fix Options:**
- **Option A (Minimal):** Introduce a separate `undefined` sentinel value. Property lookup returns `undefined` when key not found. `typeof undefined === "undefined"`. `null == undefined` remains true, `null === undefined` remains false.
- **Option B (Pragmatic):** Keep null internally but make the jasmine shim treat `null` as `undefined` for `toBe(undefined)` checks. This is a workaround, not a real fix.

**Recommendation:** Option A is the correct long-term fix. It requires changes in `lambda-data.hpp` (new sentinel), `js_runtime.cpp` (property access returns undefined), and `transpile_js_mir.cpp` (undefined literal).

#### 3.3 instanceof for User-Defined Classes (C2) — DONE ✅

**Status:** Fixed. Implemented `__class_name__`-based prototype chain walking. Each `new ClassName()` sets `__class_name__` on the instance and builds a `__proto__` chain with ancestor class names. At compile time, `instanceof` with known classes (including aliases like `var MyError = _MyError`) resolves directly to `js_instanceof_classname` for reliable name-based checking. Runtime fallback uses `js_instanceof` which extracts `__class_name__` from the constructor.

**Implementation:** `js_globals.cpp` (new `js_instanceof_classname`), `transpile_js_mir.cpp` (compile-time class resolution + `__proto__` chain setup), `js_runtime.h`, `sys_func_registry.c`.

#### 3.4 Error Class Inheritance (C3) — DONE ✅

**Status:** Fixed. `super()` in builtin Error subclasses (Error, TypeError, RangeError, SyntaxError, ReferenceError) now emits code to set `this.message = first_arg` and `this.name = error_type_name`. `js_new_error`/`js_new_error_with_name` also set `__class_name__` for instanceof support.

**Implementation:** `transpile_js_mir.cpp` (super() handling for builtin Error parents), `js_runtime.cpp` (`__class_name__` on error objects).

---

### Phase 4: Collection Iteration & Miscellaneous

#### 4.1 Map[Symbol.iterator] / for...of (D1) — DONE ✅

**Status:** Fixed. `js_iterable_to_array` now detects Map and Set collections by checking for `JsCollectionData` in the `__collection__` property. For Maps, it calls `.entries()` to produce `[key, value]` pair arrays. For Sets, it calls `.values()`.

#### 4.2 Map Insertion Order (D2) — DONE ✅

**Status:** Fixed. Added a doubly-linked list (`JsCollectionOrderNode` with key, value, next, prev) to `JsCollectionData`. `set` appends new entries or updates existing values in-place. `delete` removes from the list. `clear` resets head/tail. `forEach`, `keys`, `values`, `entries` all iterate the linked list in FIFO order.

**Impact:** ~2 direct fixes

#### 4.3 URL Constructor (D3)

**Problem:** `new URL("https://example.com")` doesn't produce a proper URL object with `.href`, `.protocol`, etc.

**Fix:** Implement a basic `URL` class that parses URL strings and exposes standard properties.

**Impact:** ~8 direct fixes (createValidAbsoluteUrl)

#### 4.4 Array.from Mapper (B6) — DONE ✅

**Status:** Fixed. New `js_array_from_with_mapper(Item iterable, Item mapFn)` function calls `js_array_from()` then applies `mapFn` to each element via `js_call_function()`. Transpiler passes the second argument when present.

---

## 5. Impact Projections — Updated with Actuals (2026-03-29)

### 5.1 Actual Gains Per Session

| Session | Date | Key Fixes | Net New Passes |
|---------|------|-----------|----------------|
| Session 1 (Phase 1+2 partial) | 2025-07-27 | RegExp, toString(radix), closure capture (3 fixes), getter/setter, charCodeAt, etc. | +106 |
| Session 2 (Phase 1 cont.) | 2025-07-28 | P3/super fix, MCONST_CLASS, IIFE class writeback, alias detection, method scope | +4 |
| Session 3 (Phase 3+4) | 2025-03-28 | instanceof, Error inheritance, Map for...of+order, Array.from mapper | +3 |
| Session 4 (Phase 5) | 2026-03-29 | Math.max spread, fn(...spread), method spread, Symbol-keyed static methods | **+278** |
| **Total** | | | **+388** |

The massive Phase 5 jump (+278) was driven by unblocking 2 completely crashing specs (xfa_formcalc +99, xml +14) plus cascading improvements to function_spec (+56), primitives_spec (+52), core_utils_spec (+31), util_spec (+18), unicode_spec (+8), type1_parser_spec (+3), pdf_find_utils_spec (+3).

### 5.2 Remaining Work Estimate

**Priority 1 — colorspace regression (A13): ~+22 recoverable**
- `ColorSpace.parse()` returning null for most test cases
- Was 32/65 before, now 10/23 (only 23 tests run before hang)
- Investigate: spread-call or method-call changes affecting ColorSpace factory/constructor

**Priority 2 — rest params via spread (C9): ~+unknown**
- `function f(...rest); f(...arr)` — `rest.length===0` instead of `arr.length`
- Fix: `js_invoke_fn` needs to detect rest-param functions (add `has_rest_param` flag to `JsFunction`) and collect `args[param_count-1:]` into array when `arg_count >= param_count`

**Priority 3 — PostScript stack eval (C11): ~+30**
- `PostScriptCompiler`-compiled functions and `PostScriptEvaluator` return `[]`
- 37 function_spec failures, possibly a stack architecture issue

**Priority 4 — UTF-16 BOM decoding (B8): ~+12**
- `stringToPDFString` passes `\xfe\xff` / `\xff\xfe` BOM bytes through unprocessed
- util_spec stringToPDFString (6), UTF-8 BOM version (3+)

**Priority 5 — Dict/Map keys iteration (D5): ~+7**
- primitives_spec: 7 remaining Dict/RefSet/RefSetCache iteration failures
- Returns `[]` for `.keys()`, `.forEach()` iteration in specific patterns

**Priority 6 — Closure capture remaining (A1): ~+88**
- 4 specs still crash: crypto (44), parser (44), autolinker (13), default_appearance (23)
- Deeper multi-level closure capture patterns not yet handled

**Priority 7 — BaseException/toThrow (C5): ~+6**
- util BaseException: `super()` chain with `__class_name__` — 4+ failures
- A known prototype chain check issue

**Priority 8 — URL constructor (D3): ~+8**
- util createValidAbsoluteUrl needs `new URL("...")` returning `.href`, `.protocol`, etc.

**Priority 9 — Number.toString(16) negative (B7): ~+7**
- murmurhash3: all 7 tests fail because hash values become negative signed integers before hex conversion

### 5.3 Revised Projected Final Scoreboard

| Spec | Baseline (7/27) | Phase 1–4 (3/28) | **Current (3/29)** | Projected Final | Delta from Baseline |
|------|-----------------|-------------------|--------------------|-----------------|---------------------|
| encodings_spec | 1807/1807 | 1807/1807 | **1807/1807** | 1807/1807 | — |
| core_utils_spec | 821/878 | 834/878 | **865/878** | 875/878 | +54 |
| primitives_spec | 62/122 | 66/122 | **118/122** | 120/122 | +58 |
| type1_parser_spec | 2/24 | 21/24 | **24/24 ✅** | 24/24 | +22 |
| function_spec | 8/114 | 21/114 | **77/114** | 90/114 | +82 |
| pdf_find_utils_spec | 10/22 | 19/22 | **22/22 ✅** | 22/22 | +12 |
| unicode_spec | 8/26 | 15/26 | **23/26** | 25/26 | +17 |
| util_spec | 11/52 | 14/52 | **32/52** | 45/52 | +34 |
| bidi_spec | 5/10 | 8/10 | **9/10** | 10/10 | +5 |
| murmurhash3_spec | 0/7 | 0/7 | **0/7** | 5/7 | +5 |
| colorspace_spec | CRASH | 32/65 | **10/23†** | 50/74 | — |
| cff_parser_spec | CRASH | 4/69 | **15/40†** | 35/69 | — |
| xfa_parser_spec | CRASH | 2/117 | **6/117** | 20/117 | — |
| xfa_tohtml_spec | CRASH | 1/50 | **1/50** | 10/50 | — |
| xfa_serialize_data_spec | CRASH | 0/1 | **0/1** | 1/1 | — |
| stream_spec | CRASH | 0/1 | **0/1** | 1/1 | — |
| **xfa_formcalc_spec** | CRASH | CRASH | **99/110** | 105/110 | — |
| **xml_spec** | CRASH | CRASH | **14/15** | 15/15 | — |
| autolinker_spec | CRASH | CRASH | **CRASH** | ~10/13 | — |
| crypto_spec | CRASH | CRASH | **CRASH** | ~35/44 | — |
| default_appearance_spec | CRASH | CRASH | **CRASH** | ~15/23 | — |
| parser_spec | CRASH | CRASH | **CRASH** | ~30/44 | — |
| **Total passing** | **~2,734** | **~2,844** | **3,122** | **~3,390** | **+656** |
| **Combined rate** | **75.2%** | **78.4%** | **88.5%** | **~93.5%** | |

†colorspace and cff_parser totals reduced from previous run — possible hang/regression from Session 4 changes. Needs investigation.

### 5.4 Top Remaining Opportunities (Prioritized)

| Priority | Fix | Est. Gain | Difficulty |
|----------|-----|-----------|------------|
| 1 | Fix colorspace regression (A13) | +22 | Low — revert/debug spread changes |
| 2 | Rest params via spread (C9) | +? | Medium — add `has_rest_param` to JsFunction |
| 3 | PostScript stack eval (C11) | +30 | High — stack architecture |
| 4 | UTF-16 BOM decoding (B8) | +12 | Medium — string parsing fix |
| 5 | Dict/Map keys iteration (D5) | +7 | Medium — collection iterator |
| 6 | Remaining closure crashes (A1) | +88 | High — multi-level capture |
| 7 | URL constructor (D3) | +8 | Medium — implement URL class |
| 8 | Number.toString(16) negative (B7) | +7 | Low — unsigned hex conversion |
| 9 | BaseException toThrow (C5) | +6 | Low — depends on C1/C3 |
| xfa_parser_spec | CRASH | 2/117 | 2/117 | 40/117 | +40 |
| xfa_tohtml_spec | CRASH | 1/50 | 1/50 | 15/50 | +15 |
| xfa_serialize_data_spec | CRASH | 0/1 | 0/1 | 1/1 | +1 |
| stream_spec | CRASH | CRASH | 0/1 | 1/1 | +1 |
| 6 remaining crashes | CRASH | CRASH | CRASH | ~80/~251 | +80 |
| **Total** | **2,734/~3,635** | **2,840/~3,626** | **2,844/~3,626** | **~3,166/~3,626** | **+432 (87%)** |

---

## 6. Implementation Priority Matrix (Updated 2026-03-29)

| Priority | Issue | Effort | Est. Gain | Status |
|----------|-------|--------|-----------|--------|
| 🔴 P0 | colorspace regression (A13) — `ColorSpace.parse()` returns null | LOW | ~+22 | ❌ New regression |
| 🔴 P0 | Rest params via spread (C9) — `f(...arr)` where `f(...rest)` → length=0 | MED | ~+? | ❌ Bug found |
| 🔴 P0 | PostScript stack eval (C11) — compiled PS functions return `[]` | HIGH | ~+30 | ❌ Not started |
| 🟡 P1 | UTF-16 BOM decoding (B8) — `stringToPDFString` BOM passthrough | MED | ~+12 | ❌ Remaining |
| 🟡 P1 | Dict/Map keys iteration (D5) — returns `[]` in some patterns | MED | ~+7 | ❌ Remaining |
| 🟡 P1 | Closure capture — 4 specs still crash (crypto, parser, autolinker, default_appearance) | HIGH | ~+88 | ⚠️ Partial |
| 🟡 P1 | URL constructor (D3) | MED | ~+8 | ❌ Not started |
| 🟢 P2 | Number.toString(16) negative (B7) — two's complement hex | LOW | ~+7 | ❌ Remaining |
| 🟢 P2 | BaseException toThrow (C5) | LOW | ~+6 | ❌ Remaining |
| 🟢 P2 | undefined vs null (C1) — pervasive but pre-empted by other fixes | HIGH | ~+0 remaining | ❌ Blocked |
| ✅ Done | Math.max/min spread, fn(...spread), method spread | — | **+278** | ✅ |
| ✅ Done | Symbol-keyed static methods | — | +4 | ✅ |
| ✅ Done | instanceof (C2) + Error (C3) + Map for...of/order (D1/D2) | — | +7 | ✅ |
| ✅ Done | P3/super() inheritance (A6), MCONST_CLASS (A7), alias detection (A9) | — | +23 | ✅ |
| ✅ Done | RegExp dynamic (C4), charCodeAt (B5), toString(radix) (B1) | — | +32 | ✅ |

---

## 7. Detailed Failure Catalog (Updated 2026-03-29)

### 7.1 core_utils_spec (12 failures — was 57)

| Test Group | # Fails | Root Cause(s) | Status |
|-----------|---------|---------------|--------|
| arrayBuffersToBytes | 2 | Deep equality comparison | Remaining |
| toRomanNumerals | 3 | `toThrow` (instanceof + Error) | Remaining |
| encodeToXmlString | 2 | codePointAt returns null? | Needs check |
| validateCSSFont | ~3 | Some weight/angle RegExp edge cases | Remaining |
| numberToString | 2 | Float precision / large number formatting | Remaining |

### 7.2 primitives_spec (7 failures — was 60)

| Test Group | # Fails | Root Cause(s) |
|-----------|---------|---------------|
| Dict iteration (keys/values/forEach) | 3–5 | `.keys()` returns `[]`; Map iteration in some patterns (D5) |
| Dict single object | 1 | `Dict.empty` — `===` identity check vs new instance |
| Dict set expected values | ~2 | Deep equality after set operations |

### 7.3 util_spec (20 failures — was 41)

| Test Group | # Fails | Root Cause(s) |
|-----------|---------|---------------|
| BaseException | 4 | `__class_name__` chain for `instanceof`; `toThrow` patterns (C5) |
| stringToPDFString | 6+ | UTF-16 BOM decoding (B8) — `0xfe 0xff` not consumed as BOM |
| URL / createValidAbsoluteUrl | ~8 | URL constructor not implemented (D3) |
| bytesToString type check | 1 | `toThrow InvalidArgument` for non-array input |
| getUuid | 2 | Possibly crypto.getRandomValues or Date-based |

### 7.4 function_spec (37 failures — was 93)

**Huge improvement:** 21 → 77/114 (+56) from spread call fixes (PostScript compiler now runs). Remaining 37:

| Test Group | # Fails | Root Cause(s) |
|-----------|---------|---------------|
| PostScriptCompiler check compiled | ~28 | Compiled PostScript math functions return `[]` instead of stack result (C11) |
| PostScriptEvaluator | ~5 | Stack-based evaluation returning wrong values |
| PostScriptParser | ~4 | `toThrow` patterns — `undefined` vs `null` for error messages |

**Root cause of PostScript stack issue (C11):** `PostScriptCompiler` transpiles PostScript programs to JS functions that are supposed to return a numeric result by operating on a virtual stack. The compiled functions appear to return empty array `[]`. This could be the `src_arr.map(fn)` result issue if the compiled function body uses `.map()` which returns an array.

### 7.5 xfa_formcalc_spec (11 failures — was CRASH)

**Now running at 99/110 after Math.max spread fix.** Remaining 11:

| Test Group | # Fails | Root Cause(s) |
|-----------|---------|---------------|
| FormCalc lexer — strings | 2 | String literal parsing edge cases |
| FormCalc lexer — comments | 2 | Comment skipping edge cases |
| FormCalc parser — subscripts | 3 | Subscript expression parsing |
| FormCalc parser — loop/if/func | 4 | `for`-step, `while`, `do`, `if` declaration parsing |

### 7.6 xfa_parser_spec (111 failures)

Despite Math.max spread fix (+4), 111 tests remain failing. Root causes include:
- Complex XFA element/attribute handling involving Symbol keys and namespace lookups
- Undefined vs null for missing attributes
- XFA tree construction patterns

### 7.7 murmurhash3_spec (7 failures)

All 7 tests fail. Root cause: hash values are treated as signed 32-bit integers; `toString(16)` on negative numbers produces `-2345678` hex notation instead of two's-complement unsigned hex (e.g., `ffdcba98`). Needs `(n >>> 0).toString(16)` semantics (B7).

---

## 8. Key Technical Challenges

### 8.1 Closure Capture Architecture

The transpiler compiles each class method as an independent MIR function. When a class is defined inside another function, the method bodies may reference variables from the enclosing function's scope. Currently, these outer variables are not declared in the method's capture/upvalue list.

**Proposed fix pattern:**
```
// Before: Method compiled as standalone function
_js_MyClass_parse(self, arg) {
  // Error: _js_pos not found
  return _js_input[_js_pos];
}

// After: Method captures outer scope variables
_js_MyClass_parse(self, arg, _env) {
  var _js_pos = env_load(_env, 0);
  var _js_input = env_load(_env, 1);
  return _js_input[_js_pos];
}
```

This requires:
1. Scanning class method bodies for free variables that come from enclosing scopes
2. Building a capture list during transpilation
3. Storing the closure environment in the class instance or method reference
4. Loading captured values at method entry

### 8.2 charCodeAt / String Encoding

Lambda strings are internally UTF-8, but JavaScript's `charCodeAt` must return UTF-16 code units. This requires a **logical indexing layer**:

- For ASCII-only strings: byte index = character index (fast path)
- For non-ASCII strings: must decode UTF-8 → Unicode code points → UTF-16 code units
- Characters > U+FFFF require surrogate pairs (two code units per character)

This is the same problem that `codePointAt`, `String.length`, and `charAt` must solve. Consider implementing a shared UTF-8→UTF-16 indexing utility.

### 8.3 undefined Sentinel

Introducing `undefined` as a distinct value requires:
1. A new tagged value (e.g., `ItemUndefined`) distinct from `ItemNull`
2. Property access returns `ItemUndefined` when key not found
3. `typeof ItemUndefined === "undefined"`
4. `ItemNull == ItemUndefined` is `true` (loose equality)
5. `ItemNull === ItemUndefined` is `false` (strict equality)
6. Function with no return statement returns `ItemUndefined`
7. Uninitialized `var` declarations are `ItemUndefined`

---

## 9. Testing Strategy

### 9.1 Regression Tests

After each fix, run:
```bash
make test-lambda-baseline        # Must remain 690/690
```

### 9.2 PDF.js Spec Verification

Run individual specs to measure improvement:
```bash
./lambda.exe js temp/pdfjs_bundles/<spec>_bundle.js 2>/dev/null | grep "^Results:"
```

### 9.3 Full Score Tracking

Track cumulative scores after each phase:
```bash
for f in temp/pdfjs_bundles/*_bundle.js; do
  name=$(basename "$f" _bundle.js)
  result=$(timeout 30 ./lambda.exe js "$f" 2>/dev/null | grep "^Results:")
  echo "$name: $result"
done
```

---

## 10. Summary

### Progress So Far (2025-07-27 → 2026-03-29)

**+388 passing tests** (2,734 → 3,122). **8 formerly-crashing specs now run** (12 → 4 crashes). **18 of 22 specs produce results.** Overall rate: **88.5%** (up from 75.2%).

Key wins by session:
- **Session 1 (2025-07-27):** type1_parser 2→21/24, function_spec TIMEOUT→21, colorspace CRASH→32, cff_parser CRASH→4, RegExp dynamic, charCodeAt, toString(16) (+106)
- **Session 2 (2025-07-28):** type1_parser 21→24/24, MCONST_CLASS, alias detection (+4)
- **Session 3 (2025-03-28):** instanceof, Error super(), Map for...of+order, Array.from mapper (+3)
- **Session 4 (2026-03-29):** Math.max spread, fn(...spread), method spread, Symbol static methods. xfa_formcalc CRASH→99/110, xml CRASH→14/15, function_spec 21→77/114, primitives 66→118/122 (+278)

### Session 4 Achievements (2026-03-29)

Implemented 6 transpiler/runtime fixes for spread calls and Symbol-keyed methods:

| Fix | Category | Files Changed | Impact |
|-----|----------|---------------|--------|
| `Math.max/min(...spread)` | C6, A10 | `transpile_js_mir.cpp`, `js_runtime.cpp` | xfa_formcalc CRASH→99, xml CRASH→14 |
| `fn(...spread)` fallback path | C7, A11 | `transpile_js_mir.cpp` | function_spec +56 |
| `obj.method(...spread)` | C8 | `transpile_js_mir.cpp`, `js_runtime.cpp` | Method spread calls work |
| `jm_build_spread_args_array` helper | — | `transpile_js_mir.cpp` | Shared foundation for all 3 spread fixes |
| Symbol-keyed static methods | A12, C10 | `build_js_ast.cpp`, `transpile_js_mir.cpp` | xfa_parser Namespace lookup +4 |
| `resolved_fn` + spread guard | — | `transpile_js_mir.cpp` | Prevents wrong arg passing on direct calls |

**Known regression:** colorspace_spec dropped from 32/65 → 10/23 (possible hang/regression from spread/method-call changes). Needs investigation.

### Remaining Path to 93%+

The path from **88.5% to 93%+** requires:

1. **Fix colorspace regression** — most impactful immediate fix (~+22 tests recoverable)
2. **Rest params via spread** (C9) — `f(...args)` where `f(...rest)` receives wrong args
3. **PostScript stack eval** (C11) — 37 function_spec failures, compiled PS functions return `[]`
4. **UTF-16 BOM decoding** (B8) — util stringToPDFString (+12 tests)
5. **Remaining closure capture** (A1) — 4 specs still crash (crypto +44, parser +44, autolinker +13, default_appearance +23)
6. **URL constructor** (D3) — util createValidAbsoluteUrl (+8)

### Completed Items (no longer blocking)
- ~~instanceof (C2) + Error (C3)~~ → Done
- ~~Map for...of (D1) + insertion order (D2)~~ → Done
- ~~Math.max/min(...spread), fn(...spread), obj.method(...spread)~~ → Done
- ~~Symbol-keyed static methods~~ → Done
- ~~P3/super() inheritance (A6)~~ → Done (type1_parser 2→24/24)
- ~~MCONST_CLASS null (A7)~~ → Done (function_spec TIMEOUT→77/114)
- ~~RegExp dynamic construction (C4), charCodeAt (B5), toString(radix) (B1)~~ → Done

### Architectural Lessons Learned

The biggest discovery was the **esbuild bundle pattern** — IIFE wrapping, class name aliasing (`var X = _X`), and `__publicField` calls — which required fundamental changes to how the transpiler handles class declarations, module vars, and scope resolution. The **MCONST_CLASS** design change (from null placeholder to real runtime value) was the single most impactful architectural fix.

The **Math.max spread root cause chain** was subtle: Builder constructor used `Math.max(...Object.values(NamespaceIds).map(({id})=>id))` → returned NaN because spread args in Math inline handler were processed as a single array item, not expanded → `_nextNsId = NaN` → all XFA namespace lookups broken → entire xfa_formcalc_spec crashed (111 assertions stuck). One transpiler fix unlocked 99 passing tests.

The **P3 optimization incompatibility with inheritance** was another key finding: shaped-slot writes assume static shapes, but `super()` calls require dynamic dispatch to parent constructors that may have different slot layouts.
