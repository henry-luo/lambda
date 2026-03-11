# JavaScript Transpiler v8: Remaining Benchmark Coverage & Performance

## 1. Executive Summary

Since v7, ES6 class support (`extends`/`super()`/`static`) was implemented, fixing all 6 AWFY class-based benchmarks and R7RS/mbrot. LambdaJS now passes **57 of 62 benchmarks** across 5 suites.

The remaining failures are:

| Benchmark | Suite | Root Cause |
|-----------|-------|------------|
| puzzle | LARCENY | Array mutation not visible in recursive calls (critical runtime bug) |
| cube3d | JetStream | No standalone `.js` file + prototype-based OOP not supported |
| richards | JetStream | No standalone `.js` file + `Function.prototype.method = fn` pattern |
| deltablue | JetStream | No standalone `.js` file + `inherits()` + `.call()` pattern |
| hashmap | JetStream | No standalone `.js` file + constructor function + prototype methods |
| raytrace3d | JetStream | No standalone `.js` file + prototype-based OOP |

Additionally, 3 JetStream benchmarks are excluded from the main comparison (crypto_aes, base64, crypto_md5) due to missing features (regex patterns, unsupported statement type).

### Status After v7

```
v1–v2: JS AST → C codegen → C2MIR → native              (removed)
v3:    Runtime alignment, GC, DOM, selectors              (done)
v4:    JS AST → direct MIR IR → native                    (done)
v5:    Language coverage + typed arrays + closures         (done)
v6:    Type inference + native code generation             (done)
v7:    ES6 class inheritance + AST fixes                   (done — 6 AWFY + mbrot fixed)
v8:    Prototype OOP + JetStream coverage + perf fixes     (this proposal)
         Phase A: Fix array mutation bug in recursion       1 benchmark (puzzle)
         Phase B: Prototype-based OOP (pre-ES6 patterns)   5 benchmarks (JetStream)
         Phase C: Create standalone JetStream JS files      9 benchmark files
         Phase D: Missing built-ins & methods               3 benchmarks (crypto_aes, base64, crypto_md5)
         Phase E: Performance optimization                  systemic improvement
```

### Target Outcome

| Metric | Current (Post-v7) | Target (v8) |
|--------|-------------------|-------------|
| Benchmarks runnable (5 suites) | 57/62 (92%) | **62/62 (100%)** |
| JetStream runnable on LambdaJS | 0/9 (0%) | **9/9 (100%)** |
| LARCENY/puzzle | ❌ failing | **✅ passing** |
| Additional JetStream benchmarks | 0/3 | **3/3** (crypto_aes, base64, crypto_md5) |

---

## 2. Failure Analysis

### 2.1 LARCENY/puzzle — Array Mutation Bug in Recursive Calls

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

**Observed behavior:** The inner `solve()` call does NOT see `cols[col] = true`. The `!cols[col]` check evaluates as `true` for ALL elements, meaning the pruning condition never fires. This causes the algorithm to explore all N^N combinations instead of N! permutations — for N=10, that's 10^10 iterations instead of ~724 solutions, effectively an infinite loop / timeout.

**Root cause hypothesis:** Arrays appear to be passed by value (copied) instead of by reference into recursive function calls. Specifically, when `jm_build_args_array()` marshals arguments for `js_call_function()`, the array `Item` is copied into the args slot. The `Item` is a tagged pointer — copying it should preserve the pointer to the same underlying `Array*` struct. BUT if the array is being defensively copied somewhere in the call path (e.g., in the capture analysis or closure creation for recursive functions), the mutation would be invisible to the parent frame.

**Investigation path:**
1. Check if `jm_build_args_array()` does anything special for recursive self-calls vs normal calls
2. Check if the native function generation (type inference) for `solve()` converts array arguments to a different representation
3. Check if `js_array_set()` correctly mutates in-place via `arr->items[idx] = value` (it does, confirmed in source)
4. Verify no defensive array copying in `js_invoke_fn()` or `js_call_function()`
5. Add `log_debug` tracing in `js_array_set` to confirm the array pointer identity across call frames

**Likely fix:** The bug is likely in native function compilation. When `solve()` is compiled as a native function (all-numeric params + numeric return), array arguments may be treated as numeric values and unboxed incorrectly, or the function may receive a stale copy of the array pointer due to register allocation issues. The fix is to ensure array-typed parameters are always passed as `Item` (pointer) and never unboxed.

### 2.2 JetStream — Two Blockers

#### Blocker 1: No Standalone JS Files

The JetStream benchmark runner (`run_all_benchmarks_v3.py`) hardcodes `lambdajs = None` for all 9 JetStream benchmarks because no standalone `.js` files exist. The original sources are in `ref/JetStream/` (not committed to repo). Even if the transpiler could handle them, the harness won't run them.

Fix: Create 9 standalone `.js` files in `test/benchmark/jetstream/`, each self-contained with timing output (Phase C).

#### Blocker 2: Prototype-Based OOP (Pre-ES6 Patterns)

The original JetStream sources (Octane, SunSpider) use pre-ES6 JavaScript patterns that LambdaJS does not support:

| Pattern | Example | Status |
|---------|---------|--------|
| `function Foo() { this.x = 1; }` | Constructor function | ✅ Works via `new Foo()` |
| `Foo.prototype.method = function() {...}` | Prototype method | ❌ Assignment to `.prototype` has no effect |
| `Foo.prototype.method.call(this, args)` | Parent delegation | ❌ `.call()` not implemented |
| `Foo.prototype.method.apply(this, [args])` | Argument spreading | ❌ `.apply()` not implemented |
| `Foo.call(this, args)` | Pre-ES6 `super()` | ❌ `.call()` not implemented |
| `Object.create(proto)` | Prototype chain setup | ❌ Not implemented |
| `SubClass.prototype = new ParentClass()` | Inheritance | ❌ Prototype assignment ignored |
| `SubClass.prototype.constructor = SubClass` | Constructor fixup | ❌ No effect |

These patterns are used extensively in:
- **richards** (Octane): `Scheduler.prototype.addIdleTask = function() {...}`, 6+ constructor functions
- **deltablue** (Octane): `inherits(child, parent)` helper, 10+ constructor functions with `.prototype` methods
- **hashmap** (simple): Constructor functions with prototype methods
- **cube3d** (SunSpider): 3D math constructor functions
- **raytrace3d** (SunSpider): Ray tracer with prototype-based OOP hierarchy

### 2.3 Additional JetStream Benchmarks (Not in Main 9)

Three additional JetStream benchmarks have Lambda Script (`.ls`) implementations but are not in the main comparison table:

| Benchmark | `.ls` Exists | JS Issue |
|-----------|:-----------:|----------|
| crypto_aes | ✅ | Unsupported regex pattern in original JS |
| base64 | ✅ | Unsupported JS statement type in AST |
| crypto_md5 | ✅ | Passes with wall-clock timing |

---

## 3. Phase A: Fix Array Mutation Bug in Recursive Calls

**Goal:** Fix puzzle (LARCENY) — currently timeouts, expected ~3ms.
**Impact:** +1 benchmark.
**Estimated effort:** ~50 LOC.

### A1. Diagnose the Root Cause

The bug manifests only with recursive functions passing arrays. Non-recursive functions with array mutation work correctly. This points to the recursive call path.

**Steps:**
1. Add `log_debug` in `js_array_set()` to print `(array_ptr, index, value)` triple
2. Run puzzle.js with logging and verify the array pointer is identical across recursive frames
3. If pointer differs → find where the copy happens (likely in native function arg marshaling)
4. If pointer is same but value doesn't persist → check for caching or register re-read issues

### A2. Fix the Bug

**Most likely scenario:** The native code generation (`jm_define_native_function`) treats array arguments as integers/floats and attempts to unbox them. Since arrays are `LMD_TYPE_ARRAY`, they should be left as `Item` pointers.

**Fix approach:**
1. In `jm_type_inference()`, ensure array-typed parameters are typed as `LMD_TYPE_ANY` (not numeric)
2. In the native function argument unboxing path, add a guard for container types (array, map, list)
3. Verify that `jm_transpile_box_item()` correctly returns the `Item` as-is for array values

**Alternative scenario:** The bug is in how `arr[i] = true` is compiled for regular arrays in native mode. The assignment may be emitting a typed-array fast path instead of `js_property_set()`, causing the write to go to a wrong memory location.

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

**Goal:** Support the constructor-function + `.prototype` pattern used by JetStream's Octane and SunSpider benchmarks.
**Impact:** Enables 5 JetStream benchmarks (richards, deltablue, hashmap, cube3d, raytrace3d).
**Estimated effort:** ~400 LOC across transpiler and runtime.

### B1. `Function.prototype` Property Access

Currently, constructor functions created via `function Foo() { ... }` do not have an accessible `.prototype` property. In V8/SpiderMonkey, every function automatically has a `.prototype` object.

**Implementation:**

In `js_new_function()` / `js_new_closure()` (js_runtime.cpp):
- When a function is created, also create an empty Map for its `.prototype` property
- Store the prototype Map as a property on the function's wrapper object
- Set `.prototype.constructor` back-reference to the function

In `jm_transpile_new_expr()` (transpile_js_mir.cpp):
- For `new Foo()` where `Foo` is a regular function (not a class):
  1. Create new object: `obj = js_new_object()`
  2. Set `obj.__proto__` to `Foo.prototype` (not flat-copy methods)
  3. Call `Foo` with `this = obj`
  4. If `Foo` returns an object, use that instead; otherwise use `obj`

**Runtime functions:**
```c
// Create a Function wrapper with auto-generated .prototype
extern "C" Item js_wrap_function_with_proto(Item func);

// Get the .prototype property of a function
extern "C" Item js_get_func_prototype(Item func);
```

### B2. `Foo.prototype.xxx = value` Assignment

Currently, `Foo.prototype.method = function() {...}` goes through `js_property_set()` on the function Item. Since functions are `JsFunction*` (not Maps), the property set fails silently.

**Fix:** Change the function representation to include a prototype Map:
- The `JsFunction` struct gets a new field: `Item prototype` — a Map object
- `js_property_get(func, "prototype")` returns this Map
- `js_property_set(func.prototype, "method", fn)` works as normal Map property set

**Alternative approach** (lower impact):
- Detect `identifier.prototype.name = expr` at the transpiler level
- Look up the function in `func_entries[]`
- Store the prototype property in a compile-time table
- At `new` time, copy from this table instead of the runtime prototype

The runtime approach is preferred for correctness with dynamic patterns.

### B3. `Function.prototype.call()` and `.apply()`

These are essential for pre-ES6 inheritance:

```javascript
// Pre-ES6 parent constructor invocation
function Animal(name) { this.name = name; }
function Dog(name, breed) {
    Animal.call(this, name);      // <-- super(name)
    this.breed = breed;
}
```

**Implementation in the transpiler** (`jm_transpile_call()`):

Detect `.call()` and `.apply()` member call patterns:

```
obj.method.call(thisArg, arg1, arg2, ...)
→ js_call_function(method, thisArg, [arg1, arg2, ...], argc)

obj.method.apply(thisArg, [arg1, arg2, ...])
→ js_call_function(method, thisArg, argsArray, argc)
```

**Transpiler pseudo-code:**
```cpp
// In jm_transpile_call(), after evaluating callee:
if (callee is MemberExpression && property_name == "call") {
    MIR_reg_t fn = jm_transpile_expr(mt, callee->object);  // the function
    MIR_reg_t this_arg = jm_transpile_expr(mt, first_arg);  // first arg = thisArg
    MIR_reg_t args = jm_build_args_array(mt, first_arg->next, argc - 1);  // remaining args
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
    // Convert array to Item* args and call js_call_function
    Array* arr = args_array.array;
    return js_call_function(func, this_val, arr->items, arr->length);
}
```

### B4. `Object.create(proto)`

Used in some JetStream benchmarks to set up prototype chains:

```javascript
Dog.prototype = Object.create(Animal.prototype);
Dog.prototype.constructor = Dog;
```

**Implementation:**
```c
extern "C" Item js_object_create(Item proto) {
    Item obj = js_new_object();
    if (proto.item != 0 && get_type_id(proto) == LMD_TYPE_MAP) {
        js_set_prototype(obj, proto);
    }
    return obj;
}
```

Register in the transpiler's `Object` built-in method dispatch (in `jm_transpile_call()` where `Object.keys()` is handled).

### B5. Prototype Chain for `new FunctionConstructor()`

Currently `jm_transpile_new_expr()` handles:
1. Built-in typed arrays → `js_typed_array_new()`
2. `new Array(n)` → `js_array_new()`
3. User-defined classes → flat method copy + constructor call

**Add a fallback for regular functions:**
```cpp
// In jm_transpile_new_expr(), after class lookup fails:
// Check if the name resolves to a function variable
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
    MIR_reg_t result = jm_call_4(mt, "js_call_function", MIR_T_I64, fn_reg, obj, args, call->arg_count);
    // 4. If function returns an object, use that; otherwise use obj
    // (standard JS `new` semantics)
    return jm_call_2(mt, "js_new_result_or_this", MIR_T_I64, result, obj);
}
```

### B6. `typeof function` Fix

Currently `js_typeof()` returns `"object"` for functions. Should return `"function"`.

```c
// In js_typeof():
if (type == LMD_TYPE_MAP) {
    // Check if it wraps a function
    if (js_is_function(value)) return make_string("function");
    // ...existing checks...
}
```

### B7. `inherits()` Helper Pattern (deltablue)

The deltablue benchmark uses a utility function:
```javascript
function inherits(childCtor, parentCtor) {
    childCtor.superClass_ = parentCtor.prototype;
    childCtor.prototype = Object.create(parentCtor.prototype);
    childCtor.prototype.constructor = childCtor;
}
```

This should work automatically if B1–B4 are implemented correctly. The `Object.create()` sets up the prototype chain, and prototype assignment stores the object on the function's prototype property.

**Validation:** Once B1–B5 are implemented, test with:
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
console.log(b instanceof B);  // true
console.log(b instanceof A);  // true
```

**Files:** `lambda/js/transpile_js_mir.cpp`, `lambda/js/js_runtime.cpp`, `lambda/js/js_globals.cpp`

---

## 5. Phase C: Create Standalone JetStream JS Files

**Goal:** Provide 9 self-contained `.js` files so `run_all_benchmarks_v3.py` can run them via LambdaJS and QuickJS.
**Impact:** Enables all JetStream benchmarks on LambdaJS (given Phase B is complete).
**Estimated effort:** ~200 LOC (scripting/porting).

### C1. Port Approach

Since the original JetStream JS sources (in `ref/JetStream/`) are third-party code, two approaches:

**Option A — Bundle originals:** Copy each `ref/JetStream/*.js` into `test/benchmark/jetstream/<name>.js`, append a timing harness, making each file self-contained. This requires obtaining the original files.

**Option B — Translate from Lambda `.ls` back to JS:** Since we have Lambda Script implementations for all 9 benchmarks (richards.ls, deltablue.ls, cube3d.ls, etc.), translate them to idiomatic JavaScript. This avoids copyright issues and ensures the JS files only use features LambdaJS supports.

**Recommended: Option B** for initial coverage. The `.ls` versions are already proven correct (they produce expected results). Translating them to JS ensures:
- No unsupported JS features (no pre-ES6 prototype patterns if we use ES6 classes)
- Equivalent algorithmic workload to the Lambda Script versions
- No external dependencies

If Option B is used, the file pattern is:
```javascript
// JetStream Benchmark: <name> (standalone JS)
// Translated from <name>.ls for LambdaJS / Node.js / QuickJS
'use strict';

// ... algorithm implementation using ES6 class syntax ...

function main() {
    const t0 = process.hrtime.bigint();
    // ... run benchmark ...
    const t1 = process.hrtime.bigint();
    console.log("__TIMING__:" + (Number(t1 - t0) / 1e6).toFixed(3));
}
main();
```

If Option A is used (original JetStream sources), Phase B must be complete first to handle the prototype-based patterns.

### C2. Files to Create

| File | Source | Lines (est) |
|------|--------|-------------|
| `test/benchmark/jetstream/nbody.js` | nbody.ls (143 lines) | ~160 |
| `test/benchmark/jetstream/cube3d.js` | cube3d.ls (443 lines) | ~460 |
| `test/benchmark/jetstream/navier_stokes.js` | navier_stokes.ls (196 lines) | ~210 |
| `test/benchmark/jetstream/richards.js` | richards.ls (317 lines) | ~330 |
| `test/benchmark/jetstream/splay.js` | splay.ls (213 lines) | ~230 |
| `test/benchmark/jetstream/deltablue.js` | deltablue.ls (400 lines) | ~420 |
| `test/benchmark/jetstream/hashmap.js` | hashmap.ls (213 lines) | ~230 |
| `test/benchmark/jetstream/crypto_sha1.js` | crypto_sha1.ls (245 lines) | ~260 |
| `test/benchmark/jetstream/raytrace3d.js` | raytrace3d.ls (348 lines) | ~370 |

### C3. Benchmark Runner Update

Update `run_all_benchmarks_v3.py` to include LambdaJS for JetStream:

```python
# Current (line ~481):
# --- LambdaJS (no JS source for jetstream) ---
lambdajs = None

# Updated:
jet_js = f"test/benchmark/jetstream/{bench_name}.js"
if os.path.exists(jet_js):
    cmd = f"{LAMBDA_EXE} js {jet_js}"
    w, e, ok = run_benchmark(cmd, NUM_RUNS)
    lambdajs = e if ok and e is not None else (w if ok else None)
else:
    lambdajs = None
```

Similarly update QuickJS to run the same `.js` files.

---

## 6. Phase D: Missing Built-ins & Methods

**Goal:** Fix 3 additional JetStream benchmarks (crypto_aes, base64, crypto_md5) and fill feature gaps.
**Estimated effort:** ~350 LOC.

### D1. Identify Unsupported Statement Type (base64)

The benchmark runner logs `"unsupported statement type %d"` for base64. Investigate:

1. Parse `base64.js` (or the original JetStream base64 source) and identify which AST node type is not handled
2. Most likely candidates: **labeled statement** (`label: for (...)`) or **comma expression** (`a, b, c`)
3. Implement the missing statement type in `jm_transpile_statement()`

**Labeled statements** implementation:
```cpp
case JS_AST_NODE_LABELED_STATEMENT:
    // Emit MIR label, transpile inner statement
    // Handle labeled break: `break label;` → MIR_JMP to label's exit
    // Handle labeled continue: `continue label;` → MIR_JMP to label's loop head
```

### D2. RegExp Support (crypto_aes)

The crypto_aes benchmark uses regex patterns. Current LambdaJS has no regex support in the JS engine, though Lambda itself uses RE2 via `re2_wrapper.hpp`.

**Minimal implementation:**
1. Parse regex literals `/pattern/flags` in `build_js_ast.cpp` (tree-sitter-javascript already parses them)
2. Create an AST node `JS_AST_NODE_REGEX_LITERAL`
3. In the transpiler, compile regex to a `re2_wrapper` call:
   - `str.match(/pattern/)` → `re2_find_all(str, pattern)`
   - `str.replace(/pattern/g, replacement)` → `re2_replace_all(str, pattern, replacement)`
   - `str.test(/pattern/)` → `re2_partial_match(str, pattern)`
   - `str.split(/pattern/)` → `re2_split(str, pattern)`
4. Store compiled regex as module-level `Item` (avoid recompilation)

**Scope:** Only need to support the subset of regex used by crypto_aes. Full regex support is future work.

### D3. `Date.now()` / `performance.now()`

JetStream benchmarks use `Date.now()` for self-reported timing. LambdaJS currently only supports `process.hrtime.bigint()`.

**Implementation:**
```c
// In jm_transpile_call(), add Date.now() dispatch:
extern "C" Item js_date_now() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return make_float((double)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000));
}

extern "C" Item js_performance_now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return make_float((double)(ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0));
}
```

### D4. Missing Math Functions

Several JetStream benchmarks need additional `Math` functions:

| Function | Needed by | Implementation |
|----------|-----------|----------------|
| `Math.atan2(y, x)` | cube3d, raytrace3d | `atan2()` from `<math.h>` |
| `Math.asin(x)` | raytrace3d | `asin()` |
| `Math.acos(x)` | cube3d, raytrace3d | `acos()` |
| `Math.atan(x)` | cube3d | `atan()` |
| `Math.log2(x)` | crypto_sha1 | `log2()` |
| `Math.imul(a, b)` | crypto_sha1 | `(int32_t)a * (int32_t)b` |
| `Math.clz32(x)` | crypto benchmarks | `__builtin_clz()` |

Add to `jm_transpile_call()` Math method dispatch (currently 20 functions, add ~7 more).

### D5. Missing Array Methods

| Method | Needed by | Implementation |
|--------|-----------|----------------|
| `Array.isArray(x)` | Several | `get_type_id(x) == LMD_TYPE_ARRAY` |
| `Array.from(iterable)` | deltablue, hashmap | Iterate and build array |
| `.splice(start, deleteCount, ...items)` | splay | In-place array modification |
| `.shift()` / `.unshift(item)` | richards | Remove/add at front |

The `splice` method is critical for the splay benchmark (splay tree operations).

### D6. Missing String Methods

| Method | Needed by | Implementation |
|--------|-----------|----------------|
| `.match(regex)` | crypto_aes | Via RE2 wrapper |
| `.padStart(len, str)` | base64, crypto | String padding |
| `.codePointAt(pos)` | crypto | Unicode code point |
| `.at(index)` | modern patterns | Negative indexing |

### D7. `JSON.stringify()` / `JSON.parse()`

Not needed for JetStream core 9, but would enable the AWFY/json benchmark to correctly self-test. Implementation can reuse Lambda's existing JSON formatter (`lambda/format/format-json.cpp`) and parser (`lambda/input/input-json.cpp`).

**Files:** `lambda/js/transpile_js_mir.cpp`, `lambda/js/js_runtime.cpp`, `lambda/js/js_globals.cpp`, `lambda/js/build_js_ast.cpp`

---

## 7. Phase E: Performance Optimization

**Goal:** Reduce LambdaJS/Node.js ratio from ~5× to ~3× on class-heavy benchmarks and fix known slow paths.
**Estimated effort:** ~300 LOC.

### E1. Array Sort — Replace O(n²) Insertion Sort

The current `js_array_method()` sort implementation uses **insertion sort** — O(n²) for random data. Several benchmarks sort arrays (quicksort, splay, deltablue).

**Fix:** Replace with **TimSort** or **introsort** (quicksort + heapsort fallback). Use `qsort_r()` (available on macOS and Linux) with a Lambda-compatible comparator:

```c
extern "C" Item js_array_sort(Item array, Item comparator) {
    Array* arr = array.array;
    if (comparator.item == 0) {
        // Default: string comparison sort
        qsort_r(arr->items, arr->length, sizeof(Item), /* ... */);
    } else {
        // User comparator function
        qsort_r(arr->items, arr->length, sizeof(Item), /* ... */);
    }
    return array;
}
```

### E2. Property Access Caching for Known Shapes

For objects with fixed shapes (e.g., `{x, y, z, vx, vy, vz, mass}` in nbody), the transpiler can resolve field offsets at compile time:

1. During Phase 1.8 (type inference), track object literal shapes
2. For known-shape property access like `body.x`, emit a direct memory load at the field's offset instead of calling `js_property_get()` with string lookup
3. Measurable impact on nbody (6 fields × millions of accesses), raytrace3d, cube3d

### E3. Compound Assignment Type Inference

Currently `jm_get_effective_type()` returns `LMD_TYPE_ANY` for compound assignments (`+=`, `-=`). This forces boxing on every iteration of numeric loops like:

```javascript
for (let i = 0; i < n; i++) sum += arr[i];
```

**Fix:** Infer the result type from both the LHS variable type and the operator:
```cpp
case JS_AST_NODE_ASSIGNMENT_EXPRESSION:
    if (asgn->op == JS_OP_ASSIGN) return jm_get_effective_type(mt, asgn->right);
    // Compound: result type is based on LHS type and operator
    TypeId lhs_type = jm_get_effective_type(mt, asgn->left);
    if (lhs_type == LMD_TYPE_INT || lhs_type == LMD_TYPE_FLOAT) return lhs_type;
    return LMD_TYPE_ANY;
```

### E4. TypedArray Pointer Hoisting

For loops that access typed arrays repeatedly, hoist the data pointer out of the loop:

```mir
// Before loop:
ta_ptr = LOAD ta_map.data        // JsTypedArray*
data_ptr = LOAD ta_ptr->data     // int32_t*
len = LOAD ta_ptr->length        // int
// Inside loop:
val = MEM[data_ptr + idx * 4]    // direct load, no Map indirection
```

This eliminates per-iteration `JsTypedArray*` extraction from the Map wrapper. Critical for fannkuch (10^9 accesses), spectralnorm, mandelbrot.

### E5. `charCodeAt()` / `String.fromCharCode()` Inlining

The brainfuck interpreter spends most time in `charCodeAt(i)` and `String.fromCharCode(n)`. Inline these:

```cpp
// In jm_transpile_call() for str.charCodeAt(i):
// Instead of calling js_string_charCodeAt():
MIR_reg_t str_ptr = /* load string data pointer */;
MIR_reg_t idx = /* evaluate argument */;
MIR_reg_t result = MIR_new_insn(mt->ctx, MIR_MOV,
    MIR_new_reg_op(mt->ctx, result),
    MIR_new_mem_op(mt->ctx, MIR_T_U8, 0, str_ptr, idx, 1));
// Return as unboxed int (skip heap allocation)
```

### E6. Prototype Lookup Fast Path

For objects created via `new ClassName()`, the prototype chain walk in `js_prototype_lookup()` traverses `__proto__` Maps linearly. Add a fast path:

1. For single-level inheritance (common case), cache the parent prototype pointer on the class entry
2. For `this.method()` inside a class body, resolve at compile time when possible (direct call)
3. For dynamic dispatch, add a one-entry inline cache per call site (monomorphic IC)

### E7. `js_invoke_fn` Max Args Limit

Currently limited to 8 arguments. Extend to 16 by adding more cases to the switch, or use a variadic calling convention:

```c
// Alternative: use function pointer cast with computed arg count
typedef Item (*JsFn16)(Item,Item,Item,Item,Item,Item,Item,Item,
                       Item,Item,Item,Item,Item,Item,Item,Item);
```

---

## 8. Implementation Priority & Dependencies

```
Phase A (Array mutation bug)     ──→ +1 benchmark    [P0, LOW effort]
  └── A1–A3: diagnose, fix, test

Phase B (Prototype-based OOP)    ──→ enables 5 JetStream [P0, HIGH effort]
  ├── B1 Function.prototype       (foundation)
  ├── B2 prototype property set   (depends on B1)
  ├── B3 .call() / .apply()       (depends on B1)
  ├── B4 Object.create()          (independent)
  ├── B5 new FunctionCtor()       (depends on B1)
  ├── B6 typeof function fix      (independent, quick)
  └── B7 inherits() validation    (depends on B1–B5)

Phase C (JetStream JS files)     ──→ +9 benchmarks   [P0, MEDIUM effort]
  ├── C1 Port .ls → .js           (independent of B if using ES6 classes)
  ├── C2 Create 9 files           (independent)
  └── C3 Update runner script     (depends on C2)

Phase D (Missing built-ins)      ──→ +3 benchmarks   [P1, MEDIUM effort]
  ├── D1 Labeled statements       (independent)
  ├── D2 RegExp basics            (independent)
  ├── D3 Date.now()               (independent, quick)
  ├── D4 Missing Math functions   (independent, quick)
  ├── D5 Array splice/shift       (independent)
  ├── D6 Missing String methods   (independent)
  └── D7 JSON stringify/parse     (independent, nice-to-have)

Phase E (Performance)            ──→ systemic perf    [P2, MEDIUM effort]
  ├── E1 Array sort O(n log n)    (independent, quick)
  ├── E2 Property access caching  (independent)
  ├── E3 Compound assign types    (independent, quick)
  ├── E4 TypedArray hoisting      (independent)
  ├── E5 charCodeAt inlining      (independent)
  ├── E6 Prototype lookup fast    (depends on B)
  └── E7 Max args extension       (independent)
```

**Recommended execution order:**

1. **Phase A** — Quick fix, unblocks puzzle (1 benchmark, hours of work)
2. **Phase C (Option B: ES6 ports)** — Create JS files using ES6 classes (avoids Phase B dependency)
3. **Phase B** — Prototype-based OOP (enables running original JetStream sources if desired)
4. **Phase D (D3, D4)** — Quick wins: `Date.now()`, missing Math functions
5. **Phase D (D1, D2, D5, D6)** — Remaining built-ins for crypto_aes, base64
6. **Phase E (E1, E3)** — Quick perf wins: sort algorithm, compound assign types
7. **Phase E (E2, E4, E5, E6)** — Deeper performance optimization

**Critical path for 62/62:** A → C → done (if using ES6 JS ports for JetStream).
**Critical path with original JetStream sources:** A → B → C → D → done.

---

## 9. Performance Projections

### After Phase A (puzzle fix)

| Benchmark | Status | Expected vs Node.js |
|-----------|--------|---------------------|
| puzzle | ❌ → ✅ | ~3–5x (backtracking with array mutation — close to Node.js) |

### After Phase C (JetStream JS files, ES6 ports)

| Benchmark | Status | Expected vs Node.js |
|-----------|--------|---------------------|
| nbody | --- → ✅ | ~3–5x |
| cube3d | --- → ✅ | ~5–10x |
| navier_stokes | --- → ✅ | ~3–8x (depends on TypedArray optimization) |
| richards | --- → ✅ | ~5–15x (method dispatch overhead) |
| splay | --- → ✅ | ~3–8x |
| deltablue | --- → ✅ | ~5–15x (constraint hierarchy) |
| hashmap | --- → ✅ | ~5–10x |
| crypto_sha1 | --- → ✅ | ~3–8x |
| raytrace3d | --- → ✅ | ~10–20x (object-heavy 3D math) |

### After Phase E (Performance optimization)

| Optimization | Expected Improvement |
|--------------|---------------------|
| O(n log n) sort | 10–100x on sorting benchmarks (splay, quicksort, deltablue) |
| Compound assign type inference | 1.5–3x on numeric loops |
| TypedArray pointer hoisting | 2–5x on typed array loops (spectralnorm, navier_stokes) |
| charCodeAt inlining | 2–5x on brainfuck |
| Property access caching | 1.5–3x on object-heavy benchmarks (nbody, raytrace3d) |

### Overall Target

| Metric | Current | Target |
|--------|---------|--------|
| Benchmarks runnable | 57/62 | **62/62** |
| JetStream LambdaJS/Node.js geo mean | N/A | ~5–8x |
| Overall LambdaJS/Node.js geo mean (excl. AWFY micro) | ~2.6x | ~2.0x |
| Worst-case ratio | brainfuck 11x | brainfuck ~3x |

---

## 10. Design Principles

1. **Fix correctness first, then optimize.** The array mutation bug (Phase A) is a fundamental correctness issue. All performance work comes after.

2. **Prefer ES6 class translations for JetStream JS files.** Translating `.ls` → ES6 JS avoids the need for full prototype-based OOP support (Phase B) as a prerequisite. Phase B remains valuable for running third-party JS code but is not on the critical path for benchmark coverage.

3. **Reuse existing Lambda infrastructure.**
   - RegExp → `re2_wrapper.hpp`
   - JSON → `format-json.cpp` / `input-json.cpp`
   - Timing → `clock_gettime()` already used by `process.hrtime.bigint()`
   - Sort → `qsort_r()` from libc

4. **No new external dependencies.** All features implemented using existing C/C++ standard library and Lambda's own infrastructure.

5. **Test after every phase.** Run `make test-lambda-baseline` and the full benchmark suite after each phase to prevent regressions.

6. **Release build for performance testing.** Never use debug build for benchmarking. Always `make release` first.

---

## 11. Test Plan

### Phase A — Correctness

```bash
# Minimal reproduction
echo 'function m(a,d){if(d===0)return a[0];a[0]=d;return m(a,d-1)}
const a=[99];console.log(m(a,3))' | ./lambda.exe js -
# Expected: 1 (deepest call sets a[0] = 1)

# Full puzzle benchmark
./lambda.exe js test/benchmark/larceny/puzzle.js
# Expected: __TIMING__:<3-10ms>, result=724

# Regression
make test-lambda-baseline
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

# instanceof
echo 'function A(){}
function B(){A.call(this)}
B.prototype=Object.create(A.prototype)
var b=new B();console.log(b instanceof B,b instanceof A)' | ./lambda.exe js -
# Expected: true true
```

### Phase C — JetStream Coverage

```bash
# Run each standalone JS file
for f in test/benchmark/jetstream/*.js; do
    echo "=== $f ==="
    timeout 60 ./lambda.exe js "$f"
done
# Verify all print __TIMING__:<number>

# Compare with Node.js
for f in test/benchmark/jetstream/*.js; do
    echo "=== $f ==="
    timeout 60 node "$f"
done
```

### Phase D — Built-ins

```bash
# Date.now()
echo 'console.log(Date.now())' | ./lambda.exe js -
# Expected: <millisecond timestamp>

# Math extensions
echo 'console.log(Math.atan2(1,1).toFixed(4))' | ./lambda.exe js -
# Expected: 0.7854

# Array.splice
echo 'var a=[1,2,3,4,5];a.splice(1,2);console.log(a.join(","))' | ./lambda.exe js -
# Expected: 1,4,5
```

### Full Benchmark Run

```bash
make release
cd test/benchmark && python3 run_all_benchmarks_v3.py
# Verify: all 62 benchmarks show timing for LambdaJS (no "---")
```

---

## 12. Summary

| Phase | Benchmarks Fixed | Key Deliverable | Effort | Priority |
|-------|-----------------|-----------------|--------|----------|
| **A: Array mutation bug** | +1 (puzzle) | Fix pass-by-reference for arrays in recursive calls | ~50 LOC | **P0** |
| **B: Prototype OOP** | enables JetStream originals | `Function.prototype`, `.call()`, `.apply()`, `Object.create()` | ~400 LOC | **P0** |
| **C: JetStream JS files** | +9 (JetStream suite) | 9 standalone `.js` benchmark files + runner update | ~2700 LOC | **P0** |
| **D: Missing built-ins** | +3 (crypto_aes, base64, crypto_md5) | Labeled statements, RegExp, Date.now(), Math/Array/String methods | ~350 LOC | **P1** |
| **E: Performance** | systemic ~5x → ~3x | O(n log n) sort, TypedArray hoisting, compound assign types | ~300 LOC | **P2** |
| **Total** | **62/62 runnable (100%)** | | **~3800 LOC** | |

**Minimum path to 62/62:** Phase A (50 LOC) + Phase C with ES6 ports (2700 LOC) = **2750 LOC**, no dependency on Phase B.

**Full path with original JetStream sources + extras:** A + B + C + D = **3500 LOC**, enables running arbitrary third-party JS benchmarks.
