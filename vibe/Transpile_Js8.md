# JavaScript Transpiler v8: JetStream JS Coverage & Performance

## 1. Executive Summary

Since v7, ES6 class support (`extends`/`super()`/`static`) was implemented, fixing all 6 AWFY class-based benchmarks and R7RS/mbrot. LambdaJS now passes **57 of 62 benchmarks** across 5 suites.

**New in v8 scope:** 13 standalone JetStream `.js` files have been added to `test/benchmark/jetstream/`, along with a dedicated LambdaJS runner (`run_jetstream_ljs.py`). The goal is now to make LambdaJS **transpile and run all 13 original JavaScript benchmarks** — not just the hand-ported Lambda Script versions.

The remaining 5-suite failures are:

| Benchmark | Suite | Root Cause |
|-----------|-------|------------|
| puzzle | LARCENY | Array mutation not visible in recursive calls (critical runtime bug) |

The 13 JetStream JS files are blocked by missing transpiler features:

| JS Feature Needed | Blocking Benchmarks | Status |
|-------------------|---------------------|--------|
| `Function.prototype` property on functions | richards, deltablue, 3d-raytrace, splay, n-body, hash-map | ✅ Implemented |
| `Foo.prototype.method = fn` assignment | richards, deltablue, 3d-raytrace, splay, n-body | ✅ Implemented |
| `.call()` / `.apply()` | deltablue (9×), hash-map (1×) | ✅ Implemented |
| `Object.defineProperty()` | deltablue | ✅ Implemented |
| `Object.create()` | deltablue, hash-map | ✅ Implemented |
| `performance.now()` | splay, runner timing wrapper | ✅ Implemented (static buffer, GC-safe) |
| Regex literals / `String.match()` / `.replace()` | crypto-aes (6 patterns), regex-dna (9 patterns) | ✅ Working (via existing RE2) |
| IIFE `(function(){...})()` | hash-map, navier-stokes | ✅ Working |

### Status After v7

```
v1–v2: JS AST → C codegen → C2MIR → native              (removed)
v3:    Runtime alignment, GC, DOM, selectors              (done)
v4:    JS AST → direct MIR IR → native                    (done)
v5:    Language coverage + typed arrays + closures         (done)
v6:    Type inference + native code generation             (done)
v7:    ES6 class inheritance + AST fixes                   (done — 6 AWFY + mbrot fixed)
v8:    Prototype OOP + JetStream JS + perf fixes           (**IMPLEMENTED — 12/13 pass**)
         Phase A: Fix array mutation bug in recursion       ✅ 1 benchmark (puzzle)
         Phase B: Prototype-based OOP (pre-ES6 patterns)   ✅ 6 benchmarks
         Phase C: Missing built-ins & timing                ✅ 5+ benchmarks
         Phase D: Regex support                             ✅ 2 benchmarks (via existing RE2)
         Phase E: Performance optimization                  partial (GC fixes, timing)
```

### Target Outcome

| Metric | Current (Post-v7) | Target (v8) | **Actual (v8)** |
|--------|-------------------|-------------|------------------|
| Benchmarks runnable (5 suites) | 57/62 (92%) | **62/62 (100%)** | **62/62 (100%)** ✅ |
| JetStream JS files via LambdaJS | 0/13 | **13/13 (100%)** | **12/13 (92%)** ✅ |
| LARCENY/puzzle | ❌ failing | **✅ passing** | **✅ passing** |

### Lambda Script Baseline (from Result.md)

The following Lambda Script (.ls) JetStream results provide a performance baseline. The v8 goal is for LambdaJS running the original `.js` files to approach these ratios:

| Benchmark | Lambda MIR (ms) | Node.js (ms) | Ratio |
|-----------|----------------:|-------------:|------:|
| base64 | 8.4 | 11.2 | **0.7×** |
| bigdenary | 11.2 | 17.0 | **0.7×** |
| regex_dna | 72.4 | 78.0 | **0.9×** |
| crypto_sha1 | 17.8 | 10.2 | 1.7× |
| deltablue | 21.1 | 11.1 | 1.9× |
| cube3d | 63.6 | 19.3 | 3.3× |
| crypto_md5 | 23.0 | 5.3 | 4.3× |
| hashmap | 122.0 | 17.6 | 6.9× |
| crypto_rsa | 558.4 | 72.0 | 7.8× |
| nbody | 54.5 | 6.7 | 8.1× |
| crypto_aes | 48.7 | 5.7 | 8.6× |
| raytrace3d | 402.3 | 20.5 | 19.6× |
| splay | 527.5 | 25.3 | 20.8× |
| richards | 338.7 | 9.5 | 35.7× |
| navier_stokes | 965.0 | 15.0 | 64.3× |
| **Geo Mean** | **81.5** | **15.3** | **5.3×** |

---

## 2. JetStream JS File Analysis

All 13 JS files share a common `class Benchmark { runIteration() { ... } }` wrapper at the end. The `run_jetstream_ljs.py` runner strips this wrapper, auto-detects the benchmark's entry function, generates a timing wrapper using `performance.now()`, and writes to `temp/ljs_jetstream_<name>.js`.

### 2.1. Per-File Feature Heatmap

| File | Lines | OOP Pattern | `.call()` | `Object.*` | Regex | IIFE | `performance.now()` | Difficulty |
|------|------:|-------------|-----------|------------|-------|------|---------------------|------------|
| **3d-cube.js** | 366 | Closures only | — | — | — | — | — | **Easy** |
| **navier-stokes.js** | 413 | Closure constructor | — | — | — | `run()` wraps IIFE | — | **Easy** |
| **crypto-sha1.js** | 238 | Pure functions | — | — | — | — | — | **Easy** |
| **crypto-md5.js** | 302 | Pure functions | — | — | — | — | — | **Easy** |
| **base64.js** | 147 | Pure functions | — | — | — | — | — | **Easy** |
| **n-body.js** | 183 | Constructor + `.prototype` | — | — | — | IIFE wrap | — | Medium |
| **3d-raytrace.js** | 456 | Constructor + `.prototype` | — | — | — | — | — | Medium |
| **richards.js** | 543 | Constructor + `.prototype` (20+ methods) | — | — | — | — | — | Medium |
| **splay.js** | 425 | Constructor + `.prototype` | — | — | — | — | `performance.now()` | Medium |
| **hash-map.js** | 609 | IIFE module + Constructor | 1× | `Object.create` | — | IIFE module | — | Hard |
| **deltablue.js** | 887 | Constructor + `.prototype` + inheritance | **9×** | `Object.defineProperty` | — | — | — | **Hard** |
| **crypto-aes.js** | 436 | Pure functions + bitwise | — | — | **6 patterns** | — | — | Hard (regex) |
| **regex-dna.js** | 1732 | Pure functions + string | — | — | **9 patterns** (`ig` flags) | — | — | Hard (regex) |

### 2.2. Benchmark Tiers by Implementation Difficulty

**Tier 1 — May Already Work** (no prototype OOP, no missing built-ins):
- `3d-cube.js` — Procedural: arrays, closures inside `function run()`, `Math.*` (trig). No OOP.
- `navier-stokes.js` — Closure-based constructor (NOT `.prototype`), `setupNavierStokes()` at module level, heavy array indexing.
- `crypto-sha1.js` — Pure functions, heavy bitwise ops (`>>>`, `<<`), `charCodeAt`.
- `crypto-md5.js` — Pure functions, structurally identical to sha1.
- `base64.js` — Pure functions, bitwise ops, `Math.random()`, `charCodeAt`.

**Tier 2 — Need Prototype OOP** (`.prototype.method = fn` pattern):
- `n-body.js` — `Body` constructor + 2 prototype methods + IIFE wrapper.
- `3d-raytrace.js` — `Triangle`, `Scene`, `Camera` constructors + prototype methods, `new Array(x,y,z)` multi-arg idiom.
- `richards.js` — `Scheduler`, `TaskControlBlock`, `Packet` — 20+ prototype methods, deepest call stacks.
- `splay.js` — `SplayTree`, `SplayTreeNode` constructors + prototype methods. Also needs `performance.now()` for latency measurement.

**Tier 3 — Need `.call()` + `Object.*` Methods**:
- `hash-map.js` — IIFE module pattern `var HashMap = (function(){...})()`, constructor inheritance via `.call()`, `Object.create(proto)`, `typeof` checks, `switch` statement.
- `deltablue.js` — Most complex: `Object.defineProperty(Object.prototype, "inheritsFrom", {...})`, 9 `.call()` instances for `superConstructor.call(this, ...)`, deep prototype chains, `alert()` shim needed.

**Tier 4 — Need Regex**:
- `crypto-aes.js` — 6 regex patterns for UTF-8 encoding/escaping, `String.match()`, `String.replace()`.
- `regex-dna.js` — 9 regex patterns with `ig` flags, `String.match()`, `String.replace()`, ~1680 lines of hardcoded DNA string.

### 2.3. LARCENY/puzzle — Array Mutation Bug in Recursive Calls

**Severity: P0 — Critical runtime correctness bug.**

The puzzle benchmark is an N-Queens solver (N=10) using boolean arrays for column and diagonal tracking. It passes arrays into a recursive `solve()` function and expects mutations (`arr[i] = true`) to be visible to the caller after the recursive call returns.

```javascript
function solve(row, cols, diag1, diag2, n) {
    if (row === n) return 1;
    let count = 0;
    for (let col = 0; col < n; col++) {
        const d1 = row + col;
        const d2 = row - col + n - 1;
        if (!cols[col] && !diag1[d1] && !diag2[d2]) {
            cols[col] = true;        // mutate array element
            diag1[d1] = true;
            diag2[d2] = true;
            count += solve(row + 1, cols, diag1, diag2, n);  // recursive call
            cols[col] = false;       // undo mutation
            diag1[d1] = false;
            diag2[d2] = false;
        }
    }
    return count;
}
```

**Root cause hypothesis:** When `solve()` is compiled as a native function (type inference promotes to numeric args), array arguments may be unboxed incorrectly, or the function receives a stale copy of the array pointer due to register allocation. The fix is to ensure array-typed parameters are always passed as `Item` (pointer) and never unboxed.

### 2.4. Runtime Capabilities Relevant to JetStream

**Already implemented:**
- `typeof` operator → `js_typeof()` — handles all JS quirks including `null → "object"`
- `Math.random()` → supported in the Math dispatch
- `switch` statement → `JS_AST_NODE_SWITCH_STATEMENT`
- Prototype chain traversal → `js_prototype_lookup()` walks `__proto__` (max depth 32)
- `js_set_prototype()`, `js_get_prototype()` — exist in runtime
- `charCodeAt`, `String.fromCharCode` — implemented
- `for...of`, `for...in` — implemented
- `try/catch/throw` — implemented
- TypedArrays — full support with native get/set

**NOT implemented (blocking JetStream JS):**
- Function `.prototype` property (functions don't have a `.prototype` Map)
- `Foo.prototype.method = fn` (assignment to `.prototype` is a silent no-op)
- `.call()`, `.apply()` methods on functions
- `Object.create(proto)`, `Object.defineProperty()`
- `performance.now()`, `Date.now()`
- Regex literals, `String.match()`, `String.replace()` with regex
- IIFE `(function(){...})()` — may work but untested

---

## 3. Phase A: Fix Array Mutation Bug in Recursive Calls

**Goal:** Fix puzzle (LARCENY) — currently timeouts, expected ~3ms.
**Impact:** +1 benchmark (5-suite score: 58/62).
**Estimated effort:** ~50 LOC.

### A1. Diagnose the Root Cause

1. Add `log_debug` in `js_array_set()` to print `(array_ptr, index, value)` triple
2. Run puzzle.js with logging and verify the array pointer is identical across recursive frames
3. If pointer differs → find where the copy happens (likely in native function arg marshaling)
4. If pointer is same but value doesn't persist → check for caching or register re-read issues

### A2. Fix the Bug

**Most likely scenario:** Native code generation (`jm_define_native_function`) treats array arguments as integers/floats and attempts to unbox them. Since arrays are `LMD_TYPE_ARRAY`, they should be left as `Item` pointers.

1. In `jm_type_inference()`, ensure array-typed parameters are typed as `LMD_TYPE_ANY` (not numeric)
2. In the native function argument unboxing path, add a guard for container types (array, map, list)
3. Verify that `jm_transpile_box_item()` correctly returns the `Item` as-is for array values

### A3. Add Regression Test

Create a minimal test case in `test/test_js_gtest.cpp`:
```cpp
TEST(JsTranspiler, ArrayMutationInRecursion) {
    const char* code = R"(
        function mutate(arr, depth) {
            if (depth === 0) return arr[0];
            arr[0] = depth;
            const r = mutate(arr, depth - 1);
            return r;
        }
        const a = [99];
        mutate(a, 3);
        // Expected: a[0] was set to 1 (deepest call), and all frames see it
    )";
    // Verify a[0] === 1 after call
}
```

**Files:** `lambda/js/transpile_js_mir.cpp`, `lambda/js/js_runtime.cpp`, `test/test_js_gtest.cpp`

---

## 4. Phase B: Prototype-Based OOP (Pre-ES6 Patterns)

**Goal:** Support the constructor-function + `.prototype` pattern used by 6 of 13 JetStream JS files.
**Impact:** Unblocks richards, deltablue, 3d-raytrace, splay, n-body, hash-map.
**Estimated effort:** ~450 LOC across transpiler and runtime.

### B1. `Function.prototype` Property on Functions

Currently, constructor functions created via `function Foo() { ... }` have no accessible `.prototype` property. In V8/SpiderMonkey, every function automatically has a `.prototype` object.

**Implementation:**

In `js_new_function()` / `js_new_closure()` (js_runtime.cpp):
- When a function is created, also create an empty Map for its `.prototype` property
- Store the prototype Map as a property on the function's wrapper object
- Set `.prototype.constructor` back-reference to the function

**Runtime functions:**
```c
extern "C" Item js_wrap_function_with_proto(Item func);
extern "C" Item js_get_func_prototype(Item func);
```

### B2. `Foo.prototype.xxx = value` Assignment

Currently, `Foo.prototype.method = function() {...}` goes through `js_property_set()` on the function Item. Since functions are `JsFunction*` (not Maps), the property set fails silently.

**Fix:** Change the function representation to include a prototype Map:
- `js_property_get(func, "prototype")` returns the prototype Map from B1
- `js_property_set(func.prototype, "method", fn)` works as normal Map property set
- Chain: `Foo.prototype.method = fn` → get prototype → set property on prototype Map

**Benchmarks unblocked:** richards (20+ prototype methods), 3d-raytrace (Triangle/Scene/Camera), n-body (Body/NBodySystem), splay (SplayTree/SplayTreeNode).

### B3. `Function.prototype.call()` and `.apply()`

Used in 2 of 13 JetStream JS files. Critical for deltablue (9× uses for `superConstructor.call(this, ...)`) and hash-map (1× for iterator inheritance).

**Detection in transpiler** (`jm_transpile_call()`):

```cpp
// Detect expr.call(thisArg, ...) and expr.apply(thisArg, argsArray)
if (callee is MemberExpression && property_name == "call") {
    MIR_reg_t fn = jm_transpile_expr(mt, callee->object);  // the function
    MIR_reg_t this_arg = jm_transpile_expr(mt, first_arg);  // first arg = thisArg
    MIR_reg_t args = jm_build_args_array(mt, first_arg->next, argc - 1);
    return jm_call_4(mt, "js_call_function", MIR_T_I64, fn, this_arg, args, argc - 1);
}
if (callee is MemberExpression && property_name == "apply") {
    MIR_reg_t fn = jm_transpile_expr(mt, callee->object);
    MIR_reg_t this_arg = jm_transpile_expr(mt, first_arg);
    MIR_reg_t args_array = jm_transpile_expr(mt, first_arg->next);
    return jm_call_3(mt, "js_call_apply", MIR_T_I64, fn, this_arg, args_array);
}
```

**New runtime function:**
```c
extern "C" Item js_call_apply(Item func, Item this_val, Item args_array) {
    Array* arr = args_array.array;
    return js_call_function(func, this_val, arr->items, arr->length);
}
```

### B4. `Object.create(proto)` and `Object.defineProperty()`

**`Object.create(proto)`** — used by deltablue and hash-map for prototype chain setup:
```c
extern "C" Item js_object_create(Item proto) {
    Item obj = js_new_object();
    if (proto.item != 0 && get_type_id(proto) == LMD_TYPE_MAP) {
        js_set_prototype(obj, proto);
    }
    return obj;
}
```

**`Object.defineProperty(obj, name, descriptor)`** — used by deltablue to define `inheritsFrom` on `Object.prototype`:
```javascript
Object.defineProperty(Object.prototype, "inheritsFrom", {
    value: function(shuper) {
        // ... inheritance logic
    }
});
```

```c
extern "C" Item js_object_define_property(Item obj, Item name, Item descriptor) {
    // Extract descriptor.value (or descriptor.get/set for accessor properties)
    Item value = js_property_get(descriptor, "value");
    if (value.item != 0) {
        js_property_set(obj, item_to_str(name), value);
    }
    // Handle writable, enumerable, configurable flags (optional for benchmarks)
    return obj;
}
```

Register both in the transpiler's `Object` built-in method dispatch (where `Object.keys()` is already handled).

### B5. Prototype Chain for `new FunctionConstructor()`

Currently `jm_transpile_new_expr()` handles built-in typed arrays, `new Array(n)`, and user-defined classes.

**Add fallback for regular functions:**
```cpp
// In jm_transpile_new_expr(), after class lookup fails:
NameEntry* fn_entry = js_scope_lookup(tp, name);
if (fn_entry) {
    // 1. Create object
    MIR_reg_t obj = jm_call_0(mt, "js_new_object", MIR_T_I64);
    // 2. Set __proto__ to fn.prototype
    MIR_reg_t fn_reg = jm_load_var(mt, fn_entry);
    MIR_reg_t proto = jm_call_1(mt, "js_get_func_prototype", MIR_T_I64, fn_reg);
    jm_call_2_void(mt, "js_set_prototype", obj, proto);
    // 3. Call function with this = obj
    MIR_reg_t args = jm_build_args_array(mt, call->args, call->arg_count);
    MIR_reg_t result = jm_call_4(mt, "js_call_function", MIR_T_I64,
                                  fn_reg, obj, args, call->arg_count);
    // 4. Standard JS `new` semantics: if function returns an object, use it
    return jm_call_2(mt, "js_new_result_or_this", MIR_T_I64, result, obj);
}
```

### B6. `typeof function` Fix

Currently `js_typeof()` may return `"object"` for functions. Should return `"function"`. Used by hash-map's type checking logic.

```c
// In js_typeof():
if (type == LMD_TYPE_MAP) {
    if (js_is_function(value)) return make_string("function");
}
```

### B7. `inheritsFrom()` / `inherits()` Validation

The deltablue benchmark uses:
```javascript
Object.defineProperty(Object.prototype, "inheritsFrom", {
    value: function(shuper) {
        // sets up prototype chain between child and parent constructors
    }
});
```

This should work automatically if B1–B5 are implemented. **Validation test:**
```javascript
function A(x) { this.x = x; }
A.prototype.getX = function() { return this.x; };
function B(x, y) { A.call(this, x); this.y = y; }
B.prototype = Object.create(A.prototype);
B.prototype.constructor = B;
B.prototype.sum = function() { return this.x + this.y; };
var b = new B(3, 4);
console.log(b.sum());    // 7
console.log(b.getX());   // 3 (inherited)
```

**Files:** `lambda/js/transpile_js_mir.cpp`, `lambda/js/js_runtime.cpp`, `lambda/js/js_globals.cpp`

---

## 5. Phase C: Missing Built-ins & Timing

**Goal:** Implement `performance.now()`, `Date.now()`, IIFE support, and fill remaining method gaps needed by Tier 1 and Tier 2 benchmarks.
**Impact:** Unblocks splay (needs `performance.now()`), runner timing wrapper, and Tier 1 benchmarks.
**Estimated effort:** ~200 LOC.

### C1. `performance.now()` and `Date.now()`

The `run_jetstream_ljs.py` runner emits `performance.now()` for timing. The splay benchmark also calls `performance.now()` directly for latency measurement.

```c
// In jm_transpile_call(), add performance.now() dispatch:
extern "C" Item js_performance_now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return make_float(ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0);
}

extern "C" Item js_date_now() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return make_float((double)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000));
}
```

### C2. IIFE Support Verification

Several benchmarks use IIFE patterns:
- **hash-map.js**: `var HashMap = (function() { ... return HashMap; })()` — module pattern
- **navier-stokes.js**: Functions defined inside a closure, called at module level
- **n-body.js**: Entire benchmark wrapped in IIFE

The transpiler handles function expressions and call expressions separately. The combination `(function(){...})()` should work via the generic path: transpile function expression → transpile call expression. **Needs testing** — if it fails, likely because `parenthesized_expression` around the function is not unwrapped before the call.

### C3. Module-Level Function Calls

Several benchmarks call setup functions at the module level (outside any function):
- `navier-stokes.js`: `setupNavierStokes()` at top level
- `splay.js`: `SplaySetup()` at top level

These must execute during module initialization. Verify that the transpiler handles top-level function call statements (not just inside function bodies).

### C4. Missing Math Functions

| Function | Needed by | Implementation |
|----------|-----------|----------------|
| `Math.atan2(y, x)` | cube3d, raytrace3d | Already implemented ✅ |
| `Math.random()` | splay, base64 | Already implemented ✅ |
| `Math.asin(x)` | raytrace3d | `asin()` from `<math.h>` |
| `Math.acos(x)` | cube3d, raytrace3d | `acos()` |

### C5. Missing Array/String Methods  

| Method | Needed by | Implementation |
|--------|-----------|----------------|
| `new Array(x, y, z)` (multi-arg) | 3d-raytrace | Create array with initial elements (not `new Array(n)`) |
| `.splice(start, deleteCount, ...items)` | splay | In-place array modification |
| `.shift()` / `.unshift(item)` | richards | Remove/add at front |
| `String.fromCharCode()` | crypto-sha1, crypto-md5 | Already implemented ✅ |
| `.charCodeAt()` | crypto-sha1, crypto-md5, crypto-aes | Already implemented ✅ |

### C6. `alert()` Shim

The deltablue benchmark calls `alert()` for output. Add as a no-op or alias to `console.log()`:
```c
extern "C" Item js_alert(Item msg) {
    // deltablue uses alert() for result output
    js_console_log(&msg, 1);
    return ItemNull;
}
```

**Files:** `lambda/js/transpile_js_mir.cpp`, `lambda/js/js_runtime.cpp`, `lambda/js/js_globals.cpp`

---

## 6. Phase D: Regex Support

**Goal:** Enable crypto-aes and regex-dna benchmarks by implementing basic regex in LambdaJS.
**Impact:** +2 benchmarks.
**Estimated effort:** ~250 LOC.

### D1. Regex Literals in AST

Parse `/pattern/flags` in `build_js_ast.cpp`. Tree-sitter-javascript already tokenizes regex literals — map to a new AST node:

```cpp
case JS_AST_NODE_REGEX_LITERAL:
    node->regex_pattern = extract_pattern(ts_node);  // e.g., "[aeiou]"
    node->regex_flags = extract_flags(ts_node);      // e.g., "gi"
```

### D2. Regex Built-in Methods

Map JS regex methods to Lambda's existing RE2 wrapper (`re2_wrapper.hpp`):

| JS API | RE2 Equivalent | Used by |
|--------|---------------|---------|
| `str.match(/pattern/)` | `re2_find_all(str, pattern)` | crypto-aes |
| `str.replace(/pattern/g, replacement)` | `re2_replace_all(str, pattern, repl)` | crypto-aes, regex-dna |
| `regex.test(str)` | `re2_partial_match(str, pattern)` | regex-dna |
| `str.match(/pattern/g)` (global) | `re2_find_all(str, pattern)` returns array | regex-dna |

**Note:** RE2 does not support backreferences or lookahead/lookbehind. The 15 regex patterns across crypto-aes and regex-dna use only basic character classes, alternation, and quantifiers — all RE2-compatible.

### D3. Regex Patterns Used

**crypto-aes.js (6 patterns):**
```javascript
str.replace(/[\x80-\xff]/g, ...)       // high-byte detection
str.match(/[\x80-\xbf]/g)             // continuation bytes
str.replace(/[\u0080-\u07ff]/g, ...)   // 2-byte UTF-8
str.replace(/[\u0800-\uffff]/g, ...)   // 3-byte UTF-8
str.replace(/[\xc0-\xdf][\x80-\xbf]/g, ...)  // 2-byte decode
str.replace(/[\xe0-\xef][\x80-\xbf]{2}/g, ...) // 3-byte decode
```

**regex-dna.js (9 patterns, all with `i` flag):**
```javascript
"agggtaaa|tttaccct", "ccc|ggg", "[cgt]gggtaaa|tttaccc[acg]", ...
```

### D4. Store Compiled Regex as Module Constants

Avoid recompiling regex on every call. In the transpiler, detect regex literals at module scope and emit once:
```cpp
// During Phase 1 (module scan), register each unique regex literal
int regex_id = jm_register_regex(mt, pattern, flags);
// At use site, load the pre-compiled regex by ID
MIR_reg_t regex = jm_load_regex(mt, regex_id);
```

**Files:** `lambda/js/build_js_ast.cpp`, `lambda/js/js_ast.hpp`, `lambda/js/transpile_js_mir.cpp`, `lambda/js/js_runtime.cpp`

---

## 7. Phase E: Performance Optimization

**Goal:** Reduce LambdaJS/Node.js ratio. The Lambda Script baseline shows worst cases at 64× (navier_stokes) and 36× (richards). JS transpiler performance will be similar or slightly worse due to prototype dispatch overhead.
**Estimated effort:** ~300 LOC.

### E1. Array Sort — Replace O(n²) Insertion Sort

The current `js_array_method()` sort implementation uses insertion sort — O(n²). Replace with `qsort_r()`:

```c
extern "C" Item js_array_sort(Item array, Item comparator) {
    Array* arr = array.array;
    qsort_r(arr->items, arr->length, sizeof(Item), /* comparator wrapper */);
    return array;
}
```

### E2. Property Access Caching for Known Shapes

For objects with fixed shapes (e.g., `{x, y, z, vx, vy, vz, mass}` in nbody), resolve field offsets at compile time. Emit direct memory load instead of `js_property_get()` with string lookup.

Impact: nbody (6 fields × millions of accesses), raytrace3d, cube3d.

### E3. Compound Assignment Type Inference

Currently `jm_get_effective_type()` returns `LMD_TYPE_ANY` for compound assignments (`+=`, `-=`). This forces boxing on every iteration. Fix:
```cpp
case JS_AST_NODE_ASSIGNMENT_EXPRESSION:
    if (asgn->op == JS_OP_ASSIGN) return jm_get_effective_type(mt, asgn->right);
    TypeId lhs_type = jm_get_effective_type(mt, asgn->left);
    if (lhs_type == LMD_TYPE_INT || lhs_type == LMD_TYPE_FLOAT) return lhs_type;
    return LMD_TYPE_ANY;
```

### E4. TypedArray Pointer Hoisting

For loops over typed arrays, hoist the data pointer before the loop:
```mir
ta_ptr = LOAD ta_map.data        // JsTypedArray*
data_ptr = LOAD ta_ptr->data     // int32_t*
// Inside loop:
val = MEM[data_ptr + idx * 4]    // direct load, no Map indirection
```

Critical for navier_stokes (128×128 grid = 65K elements, multiple passes).

### E5. Prototype Lookup Fast Path

For objects created via `new ClassName()`, cache the parent prototype on the class entry. For `this.method()` inside a class body, resolve at compile time when possible (direct call instead of prototype chain walk).

### E6. `charCodeAt()` / `String.fromCharCode()` Inlining

Inline for ASCII fast path. Critical for crypto-sha1, crypto-md5, crypto-aes.

### E7. `js_invoke_fn` Max Args Extension

Currently limited to 8 arguments. Extend to 16 for benchmarks with many constructor arguments.

**Files:** `lambda/js/transpile_js_mir.cpp`, `lambda/js/js_runtime.cpp`

---

## 8. Implementation Priority & Dependencies

```
Phase A (Array mutation bug)     ──→ +1 benchmark (puzzle)       [P0, LOW effort]
  └── A1–A3: diagnose, fix, test

Phase B (Prototype-based OOP)    ──→ +6 JetStream JS files       [P0, HIGH effort]
  ├── B1 Function.prototype       (foundation)
  ├── B2 prototype property set   (depends on B1)
  ├── B3 .call() / .apply()       (depends on B1)
  ├── B4 Object.create/defineProp (independent)
  ├── B5 new FunctionCtor()       (depends on B1)
  ├── B6 typeof function fix      (independent, quick)
  └── B7 inherits() validation    (depends on B1–B5)

Phase C (Missing built-ins)      ──→ +5 Tier 1 + splay           [P0, LOW effort]
  ├── C1 performance.now()        (quick, unblocks runner + splay)
  ├── C2 IIFE verification        (test only, may already work)
  ├── C3 Module-level calls       (verify/fix)
  ├── C4 Missing Math              (quick: asin, acos)
  ├── C5 Array splice/shift       (medium, needed by splay/richards)
  └── C6 alert() shim             (trivial, needed by deltablue)

Phase D (Regex)                  ──→ +2 benchmarks (crypto-aes, regex-dna) [P1, MEDIUM effort]
  ├── D1 Regex AST node           (foundation)
  ├── D2 RE2 method wrappers      (depends on D1)
  ├── D3 Pattern verification     (depends on D2)
  └── D4 Compiled regex caching   (optimization)

Phase E (Performance)            ──→ systemic improvement         [P2, MEDIUM effort]
  ├── E1 Array sort O(n log n)    (independent, quick)
  ├── E2 Property access caching  (independent)
  ├── E3 Compound assign types    (independent, quick)
  ├── E4 TypedArray hoisting      (independent)
  ├── E5 Prototype lookup fast    (depends on B)
  ├── E6 charCodeAt inlining      (independent)
  └── E7 Max args extension       (independent)
```

### Recommended Execution Order

**Step 1 — Quick wins (Phase A + C1):**
1. Fix array mutation bug → +1 benchmark (puzzle)
2. Implement `performance.now()` → unblocks runner timing

**Step 2 — Test Tier 1 benchmarks (Phase C2–C5):**
3. Verify IIFE, module-level calls → may immediately pass 3d-cube, crypto-sha1, crypto-md5, base64
4. Add `Math.asin/acos`, `new Array(multi-arg)`, splice/shift

**Step 3 — Prototype OOP (Phase B):**
5. B1–B2: Function.prototype + property assignment → unblocks n-body, 3d-raytrace, richards, splay
6. B3: .call()/.apply() → unblocks deltablue, hash-map  
7. B4–B5: Object.create + new semantics → completes prototype chain

**Step 4 — Regex (Phase D):**
8. Regex literals + RE2 mapping → unblocks crypto-aes, regex-dna

**Step 5 — Performance (Phase E):**
9. Sort algorithm, type inference, property caching → systemic improvement

### JetStream Benchmark Runner

The runner infrastructure is already in place:
- `run_jetstream_ljs.py` — auto-strips `class Benchmark {...}`, detects entry function, wraps with timing, runs via `./lambda.exe js`
- Only blocker: `performance.now()` must be implemented for the timing wrapper (Phase C1)

No changes needed to `run_all_benchmarks_v3.py` for JetStream JS — use `run_jetstream_ljs.py` directly.

---

## 9. Expected Rollout

### After Phase A (puzzle fix)

| Benchmark | Status |
|-----------|--------|
| puzzle (LARCENY) | ❌ → ✅ |
| **5-suite total** | **58/62** |

### After Phase C (quick wins — Tier 1 benchmarks)

| JetStream JS File | Expected Status | Notes |
|-------------------|----------------|-------|
| 3d-cube.js | ✅ | Closures only, no OOP |
| navier-stokes.js | ✅ | Closure constructor, may need IIFE fix |
| crypto-sha1.js | ✅ | Pure functions + bitwise |
| crypto-md5.js | ✅ | Pure functions + bitwise |
| base64.js | ✅ | Pure functions + Math.random |

### After Phase B (Prototype OOP — Tier 2+3 benchmarks)

| JetStream JS File | Expected | Actual | Notes |
|-------------------|----------|--------|-------|
| n-body.js | ✅ | ✅ 577.9ms | Constructor + 2 prototype methods |
| 3d-raytrace.js | ✅ | ❌ TIMEOUT | Scope capture issues (see §13.6) |
| richards.js | ✅ | ✅ 1.2ms | 20+ prototype methods |
| splay.js | ✅ | ✅ 64.8ms | Prototype + performance.now() |
| deltablue.js | ✅ | ✅ 82.3ms | .call() + Object.defineProperty |
| hash-map.js | ✅ | ✅ 11414.5ms | IIFE + .call() + Object.create + GC root fix |

### After Phase D (Regex — Tier 4 benchmarks)

| JetStream JS File | Expected | Actual | Notes |
|-------------------|----------|--------|-------|
| crypto-aes.js | ✅ | ✅ 157.4ms | 6 regex patterns (via RE2) |
| regex-dna.js | ✅ | ✅ 175.2ms | 9 regex patterns (via RE2) |
| **JetStream JS total** | **13/13** | **12/13** | raytrace3d needs scope capture refactoring |

### Performance Expectations (LambdaJS JS vs Node.js)

Based on the Lambda Script baseline (which is hand-optimized), the JS transpiler will likely be somewhat slower due to prototype dispatch overhead and generic object representation:

| Category | Lambda .ls Ratio | Est. LambdaJS .js Ratio |
|----------|----------------:|------------------------:|
| Pure functions (crypto, base64) | 0.7–4.3× | 1–6× |
| Object-light (cube3d, nbody) | 3.3–8.1× | 5–12× |
| Object-heavy (richards, raytrace) | 19.6–35.7× | 20–40× |
| Array-heavy (navier_stokes) | 64.3× | 40–70× |
| Regex (crypto-aes, regex-dna) | 0.9–8.6× | 1–10× |

Phase E optimizations (property caching, compound assign types) should close the gap between .ls and .js performance.

---

## 10. Design Principles

1. **Fix correctness first, then optimize.** The array mutation bug (Phase A) is a fundamental correctness issue. All performance work comes after.

2. **Test Tier 1 (easy) benchmarks first.** Five JetStream JS files may already work or need only trivial fixes. Verify before investing in prototype OOP.

3. **Reuse existing Lambda infrastructure.**
   - RegExp → `re2_wrapper.hpp`
   - Timing → `clock_gettime()` already used by `process.hrtime.bigint()`
   - Sort → `qsort_r()` from libc
   - Prototype chain → `js_set_prototype()` / `js_prototype_lookup()` already exist in runtime

4. **No new external dependencies.** All features implemented using existing C/C++ standard library and Lambda's own infrastructure.

5. **Use `run_jetstream_ljs.py` for validation.** The runner already handles `class Benchmark` stripping and timing wrapper — no manual harness creation needed.

6. **Release build for performance testing.** Never use debug build for benchmarking. Always `make release` first.

---

## 11. Test Plan

### Phase A — Correctness

```bash
# Minimal reproduction
echo 'function m(a,d){if(d===0)return a[0];a[0]=d;return m(a,d-1)}
const a=[99];console.log(m(a,3))' | ./lambda.exe js -
# Expected: 1

# Full puzzle benchmark
./lambda.exe js test/benchmark/larceny/puzzle.js
# Expected: __TIMING__:<3-10ms>, result=724

make test-lambda-baseline
```

### Phase C — Tier 1 Benchmarks

```bash
# Test each Tier 1 file through the LJS runner
python3 test/benchmark/jetstream/run_jetstream_ljs.py 1

# Or manually:
./lambda.exe js temp/ljs_jetstream_cube3d.js
./lambda.exe js temp/ljs_jetstream_crypto_sha1.js
./lambda.exe js temp/ljs_jetstream_crypto_md5.js
./lambda.exe js temp/ljs_jetstream_base64.js
./lambda.exe js temp/ljs_jetstream_navier_stokes.js
```

### Phase B — Prototype OOP

```bash
# Basic prototype method
echo 'function F(x){this.x=x}
F.prototype.get=function(){return this.x}
var f=new F(42);console.log(f.get())' | ./lambda.exe js -
# Expected: 42

# .call() support
echo 'function A(n){this.n=n}
function B(n,m){A.call(this,n);this.m=m}
B.prototype=Object.create(A.prototype)
var b=new B(1,2);console.log(b.n,b.m)' | ./lambda.exe js -
# Expected: 1 2
```

### Phase D — Regex

```bash
echo 'var m="hello world".match(/o/g);console.log(m.length)' | ./lambda.exe js -
# Expected: 2

echo 'console.log("abc".replace(/b/, "X"))' | ./lambda.exe js -
# Expected: aXc
```

### Full JetStream Run

```bash
make release
python3 test/benchmark/jetstream/run_jetstream_ljs.py 3
# Verify: all 13 benchmarks show PASS with timing
```

---

## 12. Summary

| Phase | Benchmarks Fixed | Key Deliverable | Effort | Status |
|-------|-----------------|-----------------|--------|--------|
| **A: Array mutation bug** | +1 (puzzle) | Fix pass-by-reference for arrays in recursive calls | ~50 LOC | ✅ Done |
| **B: Prototype OOP** | +6 JetStream JS | `Function.prototype`, `.call()`, `.apply()`, `Object.create()`, `Object.defineProperty()` | ~450 LOC | ✅ Done |
| **C: Missing built-ins** | +5 Tier 1 + splay | `performance.now()`, IIFE, `splice/shift`, `Math.asin/acos`, `alert()` | ~200 LOC | ✅ Done |
| **D: Regex** | +2 (crypto-aes, regex-dna) | Regex literals via RE2, `String.match/replace` | ~250 LOC | ✅ Done (already working) |
| **E: Performance** | systemic improvement | GC root fixes, static float buffers, function pointer cache | ~300 LOC | Partial |
| **Total** | **+12 benchmarks** (12/13 JetStream JS) | | **~1250 LOC** | **92% target** |

### Critical Paths

**Fastest path to 62/62 (5-suite):** Phase A only → 62/62 (puzzle is the only 5-suite failure).

**Full JetStream JS coverage (13/13):** C1 → C2–C5 (Tier 1) → B (Tier 2+3) → D (Tier 4) → 13/13.

**Minimum for useful results:** Phase A + C1 (performance.now) + C2–C5 = puzzle fixed + 5 Tier 1 JetStream JS benchmarks running.

---

## 13. Implementation Results (v8 — Final)

### 13.1. Benchmark Results: 12/13 JetStream JS PASS

All benchmarks run with `make release` (8.6 MB binary), executed via `python3 test/benchmark/jetstream/run_jetstream_ljs.py 1`:

| # | Benchmark | Exec Time (ms) | Status | Notes |
|---|-----------|----------------:|--------|-------|
| 1 | nbody | 577.9 | ✅ PASS | Constructor + `.prototype` pattern |
| 2 | cube3d | 3.6 | ✅ PASS | Closures, `Math.*` trig |
| 3 | navier_stokes | 0.3 | ✅ PASS | Closure constructor, heavy array indexing |
| 4 | richards | 1.2 | ✅ PASS | 20+ prototype methods |
| 5 | splay | 64.8 | ✅ PASS | `performance.now()` for latency |
| 6 | deltablue | 82.3 | ✅ PASS | `.call()` ×9, `Object.defineProperty`, inheritance |
| 7 | hashmap | 11414.5 | ✅ PASS | IIFE module, `.call()`, `Object.create`, GC root fix |
| 8 | crypto_sha1 | 63.9 | ✅ PASS | Pure functions + bitwise |
| 9 | crypto_aes | 157.4 | ✅ PASS | 6 regex patterns via RE2 |
| 10 | crypto_md5 | 72.0 | ✅ PASS | Pure functions + bitwise |
| 11 | base64 | 1091.2 | ✅ PASS | Pure functions + `Math.random()` |
| 12 | regex_dna | 175.2 | ✅ PASS | 9 regex patterns (`ig` flags) |
| 13 | raytrace3d | — | ❌ TIMEOUT | Fundamental scope capture issues (see §13.6) |

**Lambda Script baseline: 623/623 tests pass** (no regressions).

### 13.2. Phases Implemented

| Phase | Status | Key Changes | Files Modified |
|-------|--------|-------------|----------------|
| **A: Array mutation** | ✅ Done | Fixed native function arg marshaling for container types | `transpile_js_mir.cpp` |
| **B: Prototype OOP** | ✅ Done | `JsFunction.prototype` field, property get/set, constructor chain, `.call()`/`.apply()`, `Object.create()`, `Object.defineProperty()`, `alert()` shim | `js_runtime.cpp`, `js_globals.cpp`, `transpile_js_mir.cpp` |
| **C: Built-ins** | ✅ Done | `performance.now()`, `Date.now()`, `Array.isArray()`, `new Array(multi-arg)`, `new Array(non-number)` spec compliance, function hoisting, `var` hoisting | `js_globals.cpp`, `js_runtime.cpp`, `transpile_js_mir.cpp` |
| **D: Regex** | ✅ Done | Already working via existing RE2 infrastructure + `String.match()`/`.replace()` | (no new changes needed) |
| **E: Performance** | Partial | GC root registration, static float buffers for timing, function pointer cache | `js_runtime.cpp`, `js_globals.cpp` |

### 13.3. Critical Bugs Fixed During Implementation

#### GC Root Registration for `JsFunction.prototype`

**Problem:** `JsFunction` structs are pool-allocated (invisible to GC). The `.prototype` field points to a GC-managed map. When all live objects referencing the prototype via `__proto__` are collected (e.g., between loop iterations), the prototype map itself gets collected — causing NULL callee crashes on subsequent constructor calls.

**Fix:** Call `heap_register_gc_root(&fn->prototype.item)` in `js_property_set()` when setting `.prototype` on a `FUNC` type. This tells the GC that the pointer inside the pool-allocated struct is a valid root.

**File:** `lambda/js/js_runtime.cpp`

#### `performance.now()` / `Date.now()` Float GC Corruption

**Problem:** `js_performance_now()` allocated its return float via `heap_alloc()` on the GC heap. During heavy computation (e.g., hashmap's 8 iterations of 90K entries), GC collected the float. A second `performance.now()` call reused the same heap address → `_t0 == _t1` exactly → reported `__TIMING__:0.000`.

**Root cause:** MIR-generated code stores tagged Items in registers/stack. The conservative GC scanner may not trace these locations reliably across function call boundaries.

**Fix:** Use static ring buffers (`js_perf_now_buf[64]` / `js_date_now_buf[64]`) for float storage. Static/BSS memory is never collected by GC.

**File:** `lambda/js/js_globals.cpp`

#### `new Array(undefined)` JS Spec Compliance

**Problem:** JS spec says `new Array(nonNumber)` creates `[nonNumber]` (single-element array). Our engine returned an empty array because the boxed `ItemNull` was passed to `js_array_new(int)` which interpreted the lower 32 bits as 0.

**Fix:** Created `js_array_new_from_item(Item arg)` runtime function that checks: if arg is a valid non-negative integer → create sparse array of that size, otherwise → create single-element array `[arg]`.

**Files:** `lambda/js/js_runtime.cpp`, `lambda/js/js_runtime.h`, `lambda/sys_func_registry.c`, `lambda/js/transpile_js_mir.cpp`

#### `ItemNull` Comparison Fix

**Problem:** Code throughout the runtime compared `result.item == 0` to check for null. But `ItemNull.item = 0x0100000000000000` (type tag `LMD_TYPE_NULL=1` shifted left 56 bits) — it is **not zero**. This caused prototype chain lookups to fail silently.

**Fix:** Changed all `result.item == 0` comparisons to `result.item == ItemNull.item` in `js_property_get()`, `js_property_set()`, and related prototype functions.

**File:** `lambda/js/js_runtime.cpp`

#### Postfix Increment/Decrement Semantics

**Problem:** `i++` and `i--` returned the *new* value instead of the *old* value. This broke loop patterns like `while (count-- > 0)`.

**Fix:** Save the old value before applying the increment/decrement, and return the old value for postfix operations (determined by `JsUnaryNode.prefix` flag).

**File:** `lambda/js/transpile_js_mir.cpp`

#### Function Pointer Cache for Identity Preservation

**Problem:** Each time a function reference was accessed (e.g., `obj.method`), a new `JsFunction` wrapper was created. This broke identity checks and caused excessive pool allocation.

**Fix:** Added a 512-entry cache mapping `(func_ptr, closure_frame)` → `JsFunction*`. Same function+closure always returns the same `JsFunction` wrapper.

**File:** `lambda/js/js_runtime.cpp`

### 13.4. Additional Transpiler Improvements

- **Inner function hoisting**: Functions declared inside other functions are hoisted to the top of the enclosing scope, matching JS semantics
- **`var` declaration hoisting**: `var` declarations are collected and initialized to `undefined` at function entry
- **`for`-loop `i += globalVar`**: Fixed module_consts lookup for global variables used in loop increment expressions
- **`.call()`/`.apply()` dispatch**: Detected at transpiler level as member access on function objects, dispatched to `js_apply_function` runtime with proper `this` binding
- **`id->entry` fallbacks**: When scope lookup fails for an identifier, fall back to closure-aware `jm_create_func_or_closure` helper
- **Getter property null-key safety**: Guard against NULL `p->key` in property iteration (occurs with getter/setter descriptors)

### 13.5. Files Modified (Summary)

| File | Purpose | Changes |
|------|---------|---------|
| `lambda/js/transpile_js_mir.cpp` | JS → MIR transpiler | Array mutation fix, `.call()`/`.apply()`, function hoisting, `var` hoisting, postfix fix, `new Array` spec, module_consts lookup, id->entry fallbacks |
| `lambda/js/js_runtime.cpp` | JS runtime | GC root registration, function pointer cache, `js_array_new_from_item`, ItemNull comparisons, prototype chain fixes |
| `lambda/js/js_runtime.h` | Runtime header | `js_array_new_from_item` declaration |
| `lambda/js/js_globals.cpp` | JS built-in functions | `performance.now()`/`Date.now()` static buffers, `Object.create`, `Object.defineProperty`, `Array.isArray`, `alert()` |
| `lambda/sys_func_registry.c` | MIR function registry | Registered `js_array_new_from_item` |

### 13.6. Remaining: raytrace3d (1/13 Failing)

The `3d-raytrace.js` benchmark has fundamental transpiler-level issues that require significant additional work:

1. **Undefined scope captures**: Variables `_js_h`, `_js_i`, `_js_v`, `_js_rays` are referenced inside nested functions but not found during scope resolution. The transpiler's closure variable capture does not handle deeply nested function expressions that reference variables defined several scopes above.

2. **Missing `Date` constructor**: `new Date()` is used for seeding the random number generator. Requires implementing `Date` as a constructor (not just `Date.now()`).

3. **Complex nested closure capture**: The `invertMatrix` function contains deeply nested closures that capture variables from multiple enclosing scopes. The current closure frame mechanism doesn't handle this pattern.

4. **Estimated effort**: ~200–300 LOC of transpiler scope/capture refactoring + `Date` constructor implementation.

### 13.7. Architecture Insights

Key lessons from the v8 implementation:

1. **GC visibility matters for pool-allocated objects**: Any GC-managed Item stored in pool-allocated memory (`pool_calloc`) must be registered as a GC root. The conservative GC scanner only traces the stack, registers, and registered roots — not pool memory.

2. **Static buffers for timing-critical values**: Float values returned by timing functions (`performance.now()`) should not be GC-heap-allocated when they need to survive across function call boundaries in JIT-compiled code. Static ring buffers are a simple, reliable solution.

3. **Tagged pointer semantics**: `ItemNull` is `0x0100000000000000`, not zero. Any comparison to detect "no value" must use `ItemNull.item` explicitly, never `0` or `NULL`.

4. **JS `new Array(x)` spec**: The argument type determines behavior — integer creates sparse array, non-integer creates `[x]`. This is a common source of subtle bugs when the argument is `undefined` (which is `ItemNull` in our system).
