# JavaScript Transpiler v7: Full Benchmark Coverage & Regression Fixes

## 1. Executive Summary

Lambda's JS engine (LambdaJS) currently **fails 12 of 62 benchmark tests** across 6 suites (Round 3). The dominant blocker is **missing ES6 class inheritance** (`extends`/`super()`), which accounts for 10 of the 12 failures. Additionally, 3 benchmarks show **major performance regressions** from Round 2:

| Benchmark | R2 → R3 | Regression |
|-----------|---------|------------|
| nqueens (R7RS) | 0.013ms → 41ms | 3136× slower |
| quicksort (LARCENY) | 0.19ms → 9.4ms | 49.6× slower |
| sumfp (R7RS) | 1.7ms → 4.2ms | 2.5× slower |

This proposal targets **100% benchmark coverage** (all 62 benchmarks runnable on LambdaJS) and **root-cause analysis + fixes for all 3 regressions**.

### Architecture Position

```
v1–v2: JS AST → C codegen → C2MIR → native              (removed)
v3:    Runtime alignment, GC, DOM, selectors              (done)
v4:    JS AST → direct MIR IR → native                    (done)
v5:    Language coverage + typed arrays + closures         (done)
v6:    Type inference + native code generation             (done, 11–52x speedup)
v7:    Full benchmark coverage + regression fixes          (this proposal)
         Phase A: ES6 class inheritance (extends/super)    10 benchmarks
         Phase B: AST builder correctness fixes            3 benchmarks
         Phase C: Performance regression investigation     3 benchmarks
         Phase D: TypedArray loop performance              1 benchmark (fannkuch)
         Phase E: BigInt, Map, RegExp, require('fs')       4 benchmarks
         Phase F: Hot-loop deboxing                        systemic perf improvement
```

### Target Outcome

| Metric | Current (Round 3) | Target (v7) |
|--------|-------------------|-------------|
| Benchmarks runnable | 50/62 (80.6%) | **62/62 (100%)** |
| Failing benchmarks | 12 | **0** |
| Performance regressions investigated | 0/3 | **3/3** |
| Median self-time ratio vs V8 | ~5x | ~3x |

---

## 2. Failure Inventory — All 12 Failing LambdaJS Benchmarks

### By Root Cause

| Root Cause | Count | Benchmarks | Suites |
|-----------|------:|------------|--------|
| **Missing ES6 class inheritance** | 10 | bounce, storage, json, deltablue, havlak, cd (AWFY); cube3d, richards, deltablue, hashmap (JetStream) | AWFY, JetStream |
| **AST builder gaps** | 3 | binarytrees (BENG), nbody (BENG), levenshtein (KOSTYA) | BENG, KOSTYA |
| **Performance timeout** | 1 | fannkuch (BENG) | BENG |
| **Overlap** | -2 | raytrace3d timeout relates to class overhead; puzzle may work in release | |

**Note:** The R7RS/mbrot "---" in Round 3 is a separate issue (not class-related).

### Detailed Failure Map

| # | Benchmark | Suite | Status | Blocker | This Proposal |
|---|-----------|-------|--------|---------|---------------|
| 1 | bounce | AWFY | ❌ | `class Bounce extends Benchmark`, `super()` | Phase A |
| 2 | storage | AWFY | ❌ | `class Storage extends Benchmark`, `super()` | Phase A |
| 3 | json | AWFY | ❌ | `class JsonParser` with class hierarchy | Phase A |
| 4 | deltablue | AWFY | ❌ | 9 `extends`, `super()`, `static` fields | Phase A |
| 5 | havlak | AWFY | ❌ | `class HavlakLoopFinder`, 4 `extends` | Phase A |
| 6 | cd | AWFY | ❌ | `class RedBlackTree extends ...`, 4 `extends` | Phase A |
| 7 | cube3d | JetStream | ❌ | ES6 class support | Phase A |
| 8 | richards | JetStream | ❌ | ES6 class support | Phase A |
| 9 | deltablue | JetStream | ❌ | ES6 class support | Phase A |
| 10 | hashmap | JetStream | ❌ | ES6 class support | Phase A |
| 11 | binarytrees | BENG | ❌ | Template literal `\t` escape | Phase B |
| 12 | nbody | BENG | ❌ | Comments inside array expressions | Phase B |
| 13 | levenshtein | KOSTYA | ❌ | Destructuring assignment `[a,b]=[b,a]` | Phase B |
| 14 | fannkuch | BENG | ❌ | TypedArray tight-loop timeout | Phase D |

---

## 3. Performance Regression Analysis

### R1. nqueens: 0.013ms → 41ms (3136× slower)

**Diagnosis: Workload correction, NOT a code regression.**

The R2 result of 0.013ms is physically impossible for an N-Queens solver counting 92 solutions — a single function call on Apple Silicon takes ~0.001ms. At 0.013ms, the benchmark executed fewer than ~50 instructions total.

**Evidence:**
- The R3 JS file (`nqueens2.js`) correctly runs `nqueens(8)` which requires exploring ~2000 recursive `solve()` calls
- Each `solve()` allocates 3 `new Int32Array()` objects — the benchmark is inherently allocation-heavy
- Node.js V8 runs nqueens(8) in 1.8ms — LambdaJS at 41ms is 22× slower, consistent with the GC/allocation overhead pattern seen in similar benchmarks (gcbench at 24×, binarytrees at 5×)

**Root cause of slowness (not regression):** Heavy `Int32Array` allocation per recursive call. `solve()` creates 3 new typed arrays on each invocation. LambdaJS's `js_typed_array_new()` does a `pool_calloc` + data array allocation per call, while V8's generational GC handles short-lived allocations with near-zero cost via bump allocation in the young generation.

**Action items:**
- [ ] **C1a.** Verify: confirm R2 used a different (trivial) workload by checking git history for nqueens2.js changes
- [ ] **C1b.** Optimize typed array allocation: add a small-array fast path in `js_typed_array_new()` — for `Int32Array(n)` where `n ≤ 64`, use stack allocation or a thread-local free-list to avoid heap pressure
- [ ] **C1c.** Consider arena-bump allocation for typed arrays created inside recursive calls — allocate from a resettable arena that's freed when the function returns

### R2. quicksort: 0.19ms → 9.4ms (49.6× slower)

**Diagnosis: Workload correction, likely NOT a code regression.**

Sorting 5000 elements with Lomuto quicksort requires ~60,000 comparisons and ~30,000 swaps. At 0.19ms, that's ~3ns per comparison+swap — plausible only if the benchmark ran with a much smaller array or fewer iterations in R2.

**Evidence:**
- The R3 `quicksort.js` uses `const size = 5000` and sorts once
- Node.js runs it in 1.6ms — LambdaJS at 9.4ms is 5.9× slower, which is consistent with typed array access overhead
- The 0.19ms R2 time would require `size ≈ 100` to be realistic

**Root cause of current speed (not regression):** Typed array element access in `partition()` involves:
1. Extract `JsTypedArray*` from the Map wrapping object
2. Bounds check
3. Load `int32_t` from data pointer
4. Box to `Item` for the comparison
This 4-step sequence runs ~5000×log₂(5000) ≈ 60,000 times.

**Action items:**
- [ ] **C2a.** Verify workload change via git history
- [ ] **C2b.** Apply TypedArray pointer hoisting (Phase D.B1) — hoist the `JsTypedArray*` extraction out of `partition()`'s for-loop
- [ ] **C2c.** Apply bounds-check elimination (Phase D.B2) — the loop `for (let j = lo; j < hi; j++)` with `hi < arr.length` is provably safe

### R3. sumfp: 1.7ms → 4.2ms (2.5× slower)

**Diagnosis: Possible genuine code regression in float loop compilation.**

Unlike the other two, sumfp is a trivial `while` loop with float arithmetic — no allocation, no recursion, no typed arrays. The 2.5× slowdown is small but significant for such simple code.

**Source code:**
```javascript
function run(n) {
    let i = n;       // n = 100000.0
    let s = 0.0;
    while (i >= 0.0) {
        s = s + i;
        i = i - 1.0;
    }
    return s;
}
```

**Hypothesis 1: Float comparison fallback.**
The while-loop condition `i >= 0.0` should trigger the native float comparison fast path in `jm_transpile_while()`. If a v6 change altered how `0.0` is typed (e.g., treating it as INT instead of FLOAT), the compiler may emit a type mismatch and fall back to `js_is_truthy()` — a boxed runtime call per iteration.

**Hypothesis 2: TCO interference.**
The `run()` function has a single self-not-calling body (no recursion). But if `jm_has_tail_call()` scanning now traverses `while` bodies and finds `return s` inside (it shouldn't, but verify), it could incorrectly mark the function as TCO-eligible and wrap it in extra loop scaffolding.

**Hypothesis 3: Workload change.**
If R2 used `run(10000.0)` and R3 uses `run(100000.0)` (10× more iterations), the 2.5× change would actually indicate a 4× per-iteration speedup (10/2.5 = 4× improvement), not a regression.

**Action items:**
- [ ] **C3a.** Verify workload: check git history for sumfp2.js iteration count changes
- [ ] **C3b.** Debug float comparison path: add `log_debug` in `jm_transpile_while()` to confirm both operands are recognized as FLOAT, and the native `MIR_DBGE` is emitted (not `js_is_truthy`)
- [ ] **C3c.** If float comparison falls back to boxed path: trace through `jm_get_effective_type()` for the literal `0.0` — verify it returns `LMD_TYPE_FLOAT`, not `LMD_TYPE_INT`
- [ ] **C3d.** If confirmed workload change: mark as non-regression, update R2→R3 notes

---

## 4. Action Items

### Phase A: ES6 Class Inheritance (`extends` / `super()`)

**Goal:** Fix 10 failing benchmarks (6 AWFY + 4 JetStream). This is the single highest-impact change.  
**Estimated effort:** ~600 lines across transpiler and runtime.

#### Current State

The transpiler already supports:
- Class declaration parsing (AST: `JsClassNode` with `name`, `superclass`, `body`)
- Method collection into `JsClassEntry` with `JsClassMethodEntry` linked list
- Constructor detection by name matching `"constructor"`
- `new ClassName(args)` → create object, attach methods, call constructor
- `this.field` access via `js_get_this()`/`js_set_this()` runtime functions

**What's missing** (required by all 14 AWFY bundles):

| Feature | Usage Pattern | Implementation Gap |
|---------|--------------|-------------------|
| `extends` | `class Bounce extends Benchmark` | Superclass field parsed in AST but no prototype chain setup |
| `super()` | `super(); super(arg1, arg2)` | No parent constructor invocation |
| `super.method()` | `super.verifyResult(...)` | No parent method delegation |
| `static` methods | `Vector.with(elem)`, `Strength.of(n)` | `static_method` flag exists but ignored |
| `static` fields | `static strengthTable = [...]` | Not supported |
| Method override (virtual dispatch) | `this.benchmark()` calling subclass method | Methods flat-copied; need prototype chain lookup |

#### A1. Prototype Chain Model

Design: Use Lambda's existing Map (object) structure with a `__proto__` hidden property linking to the parent class's prototype object.

```
[Bounce instance]           [Bounce.prototype]        [Benchmark.prototype]
  x: 10                      benchmark: fn             benchmark: fn (default)
  y: 20                      verifyResult: fn          verifyResult: fn (default)
  __proto__: ──────→          __proto__: ──────────→    __proto__: null
```

**Implementation:**
1. For each class, create a **prototype object** at compile time containing all non-constructor methods
2. For classes with `extends`, set the prototype's `__proto__` to the parent prototype
3. On `new ClassName(args)`: create object, set `__proto__` to class prototype, call constructor
4. On `this.method()`: look up in object first, then walk `__proto__` chain

**Runtime functions:**
```c
extern "C" Item js_create_prototype(Item parent_proto); // create proto with __proto__ link
extern "C" Item js_proto_set(Item proto, Item key, Item val); // add method to prototype
extern "C" Item js_new_instance(Item proto);            // create {__proto__: proto}
extern "C" Item js_proto_lookup(Item obj, Item key);    // walk __proto__ chain
```

**Transpiler changes** in `jm_transpile_new_expression()`:
- Look up class entry, find its prototype register (created during pre-pass)
- Call `js_new_instance(proto)` instead of `js_new_object()`
- Call constructor with `this = instance`

**Files:** `lambda/js/transpile_js_mir.cpp`, `lambda/js/js_runtime.cpp`  
**Lines changed:** ~200

#### A2. `super()` Constructor Calls

When inside a constructor of a class that `extends` another:
- Detect `super(args...)` call expression in the AST (already parsed as `JS_AST_NODE_CALL_EXPRESSION` with callee `super`)
- Look up the parent class's constructor
- Emit: `js_call_function(parent_ctor, this, args)`
- The `this` object already has `__proto__` set to the correct prototype chain, so parent constructor field assignments work correctly

**Transpiler changes:**
- In `jm_transpile_call()`, add a `super` callee case
- Resolve parent class from current class context (`mt->current_class->node->superclass`)
- Find parent's constructor from `mt->class_entries`

**Lines changed:** ~50

#### A3. `super.method()` Calls

When `super.method(args)` appears:
- Resolve the parent class
- Look up `method` in the parent class's method list (compile-time resolution, not runtime)
- Emit a direct call to the parent's method MIR function with `this` as first arg

**Lines changed:** ~40

#### A4. Static Methods and Fields

**Static methods:**
- During class collection, detect `static` keyword on method definitions (the `static_method` flag already exists in `JsMethodDefinitionNode`)
- Store static methods on the **class object itself** (not on the prototype)
- Route `ClassName.staticMethod(args)` to a direct call

**Static fields:**
- During class body processing, detect field definitions (non-method members with initializers)
- Evaluate initializers at class declaration time
- Store as properties on the class object

**Lines changed:** ~80

#### A5. `this.method()` Virtual Dispatch

Currently, methods are flat-copied as properties on each instance. With the prototype chain model (A1), method lookup via `this.method()` must:
1. Check the instance's own properties first
2. Walk the `__proto__` chain if not found
3. Call the found function with `this = instance`

The existing `js_property_get` runtime function needs to be extended to walk `__proto__`:
```c
Item js_property_get(Item obj, Item key) {
    Item val = map_get(obj, key);
    if (val != ItemNull) return val;
    Item proto = map_get(obj, "__proto__");
    if (proto != ItemNull) return js_property_get(proto, key);  // recursive lookup
    return ItemNull;
}
```

**Optimization:** Since AWFY benchmarks call `this.method()` in tight loops, cache the `__proto__` chain walk result. If the object's shape hasn't changed, the method reference from the last lookup is still valid.

**Lines changed:** ~30

#### A6. `instanceof` with Prototype Chain

Update `js_instanceof()` to walk the `__proto__` chain:
```c
bool js_instanceof(Item obj, Item ctor) {
    Item proto = js_property_get(ctor, "prototype");
    Item p = js_property_get(obj, "__proto__");
    while (p != ItemNull) {
        if (p == proto) return true;
        p = js_property_get(p, "__proto__");
    }
    return false;
}
```

**Lines changed:** ~20

#### AWFY Bundle Compatibility Checklist

All 14 AWFY `*_bundle.js` files share a common base library (`Benchmark`, `Vector`, `Set`, `Dictionary`). Once the class system works for `bounce2_bundle.js`, the others should follow with minimal additional work.

| Bundle | Class Count | Extends Depth | Extra Features Needed |
|--------|:-----------:|:----:|--------------------------|
| sieve | 4 | 1 | — |
| permute | 4 | 1 | — |
| queens | 4 | 1 | — |
| towers | 5 | 1 | — |
| bounce | 13 | 2 | `Vector.with()` static |
| list | 5 | 1 | — |
| mandelbrot | 4 | 1 | — |
| nbody | 6 | 1 | — |
| storage | 12 | 2 | — |
| json | 21 | 2 | String methods |
| richards | 13 | 3 | `TaskState.createRunning()` static |
| havlak | 19 | 2 | `Set`, `Dictionary` |
| deltablue | 24 | 5 | 8 `static` fields/methods |
| cd | 24 | 2 | `RedBlackTree` |

**Recommended testing order:** sieve → permute → queens → towers → bounce → list → nbody → mandelbrot → storage → json → richards → havlak → cd → deltablue (ascending complexity).

---

### Phase B: AST Builder Correctness Fixes

**Goal:** Fix 3 failing benchmarks with localized code changes.  
**Estimated effort:** ~100 lines total across `build_js_ast.cpp` and `transpile_js_mir.cpp`.

#### B1. Template Literal Escape Sequences (binarytrees)

**Problem:** In `build_js_ast.cpp`, template element `cooked` value is set to `raw` without processing escape sequences:
```cpp
element->cooked = element->raw; // TODO: Process escape sequences
```
The `\t` in `` `${iterations}\t trees of depth ${depth}\t check: ${sum}` `` is output literally.

**Fix:**
- Add a `js_cook_template_string()` function that processes `\t` → tab, `\n` → newline, `\\` → backslash, `\r` → CR, `\0` → null, `\'`, `\"`, `` \` ``
- Use arena allocation for the cooked string
- Extract the escape processing from the existing string literal parser into a shared helper

**Files:** `lambda/js/build_js_ast.cpp`  
**Lines changed:** ~30

#### B2. Comments Inside Array Expressions (nbody)

**Problem:** `build_js_array_expression` iterates named children but does not skip `comment` nodes. `build_js_object_expression` already handles this.

**Fix:** Add `if (strcmp(child_type, "comment") == 0) continue;` guard. Audit other expression builders.

**Files:** `lambda/js/build_js_ast.cpp`  
**Lines changed:** ~10

#### B3. Destructuring Assignment (levenshtein)

**Problem:** `[prev, curr] = [curr, prev]` — the transpiler handles destructuring in declarations but `jm_transpile_assignment` falls through on `JS_AST_NODE_ARRAY_PATTERN` LHS.

**Fix:**
- Add `JS_AST_NODE_ARRAY_PATTERN` case in `jm_transpile_assignment`
- Evaluate RHS first into temp registers (handles circular swaps)
- Extract element-assignment loop from `jm_transpile_variable_declaration` into shared helper `jm_destructure_array()`

**Files:** `lambda/js/transpile_js_mir.cpp`  
**Lines changed:** ~60

---

### Phase C: Performance Regression Investigation & Fixes

**Goal:** Investigate all 3 regressions, fix genuine code issues, document workload changes.  
**Estimated effort:** ~100 lines + investigation time.

See **Section 3** for full analysis. Summary of action items:

#### C1. nqueens (3136× — workload correction)
- Verify R2 workload via git history
- Add small-array fast path in `js_typed_array_new()` for `n ≤ 64`
- Consider arena-bump allocation for recursive typed array creation

#### C2. quicksort (49.6× — workload correction)
- Verify R2 workload via git history
- Apply TypedArray pointer hoisting from Phase D
- Apply bounds-check elimination for `partition()` loop

#### C3. sumfp (2.5× — possible genuine regression)
- Verify workload (`run(10000.0)` vs `run(100000.0)`)
- Debug float comparison path in `jm_transpile_while()` — verify native `MIR_DBGE` is emitted
- Trace `jm_get_effective_type()` for literal `0.0` — must return `LMD_TYPE_FLOAT`
- If confirmed regression: fix the type inference for float literals in comparison context

**Files:** `lambda/js/transpile_js_mir.cpp`, `lambda/js/js_typed_array.cpp`  
**Lines changed:** ~100

---

### Phase D: TypedArray Loop Performance (fannkuch)

**Goal:** Make `fannkuch(12)` complete within 30s (currently times out). Target: ~2s.  
**Estimated effort:** ~150 lines in `transpile_js_mir.cpp`.

**Problem:** The `fannkuch` benchmark has 4 nested loops doing ~10^9 TypedArray bracket accesses. Each access involves:
1. Extract `JsTypedArray*` from `Map.data` (redundant per-iteration)
2. Bounds check (eliminable in counted loops)
3. Box/unbox integer results

#### D1. TypedArray Pointer Hoisting

Hoist `JsTypedArray*` and `data` pointer loads out of loops when the variable is not reassigned:
```mir
// Before loop:
ta_ptr = LOAD Map.data        // JsTypedArray*
data_ptr = LOAD ta_ptr->data  // int32_t*
// Inside loop:
val = MEM[data_ptr + idx * 4] // direct load, no Map indirection
```

#### D2. Bounds Check Elimination in Counted Loops

For `for (let i = 0; i < n; i++)` where `n ≤ arr.length`, eliminate the per-access bounds check. Detect:
- Loop variable starts at 0 or known non-negative
- Loop bound is `< arr.length` or `< n` where `n = arr.length`
- Loop step is +1

#### D3. Native Register Allocation for TypedArray Temporaries

Ensure the swap pattern `const tmp = perm[lo]; perm[lo] = perm[hi]; perm[hi] = tmp;` keeps `tmp` as native `int32_t` — no boxing to `Item`.

#### D4. Inlined TypedArray Copy Loops

Pattern `for (let i = 0; i < n; i++) perm[i] = perm1[i]` → lower to `memcpy()` when both arrays are same type.

**Files:** `lambda/js/transpile_js_mir.cpp`  
**Lines changed:** ~150

---

### Phase E: BigInt, Map, RegExp, require('fs')

**Goal:** Bring remaining out-of-scope benchmarks to passing.  
**Estimated effort:** ~900 lines total.

#### E1. BigInt via Lambda Decimal Library (pidigits) — ~300 LOC

Reuse `lambda-decimal.hpp` for arbitrary-precision integers:
- Parse `1n` BigInt literals → `LMD_TYPE_DECIMAL`
- Runtime: `js_bigint_add/sub/mul/div/mod/cmp` → wrap `decimal_*` functions
- Transpiler: route operators to BigInt functions when operands are BigInt-typed
- `typeof` returns `"bigint"` for decimal items

#### E2. JS `Map` Built-in (knucleotide) — ~250 LOC

Reuse `lib/hashmap.h`:
- `JsMap` struct wrapping `HashMap*`
- Runtime: `js_map_new/set/get/has/delete/size/entries/keys/values/forEach`
- Transpiler: detect `new Map()`, route `.set()/.get()` to runtime functions

#### E3. JS `RegExp` via Lambda RE2 (regexredux) — ~250 LOC

Reuse `re2_wrapper.hpp`:
- Parse `/pattern/flags` regex literals
- Map `test()` → `pattern_partial_match`, `replace()` → `pattern_replace_all`, `split()` → `pattern_split`, `match()` → `pattern_find_all`

#### E4. `require('fs')` Shim — ~80 LOC

Minimal shim for benchmarks reading stdin/files:
- Detect `require('fs')` → return built-in `fs` object
- Implement `fs.readFileSync(path, encoding)` via `read_text_file()`
- Alternatively: `--input <file>` CLI flag pre-loading content

**Files:** New files `lambda/js/js_map.cpp`, `lambda/js/js_regexp.cpp` + existing transpiler/runtime files

---

### Phase F: Hot-Loop Deboxing (Systemic Performance)

**Goal:** Reduce median self-time ratio from ~5x to ~3x across all benchmarks.  
**Estimated effort:** ~200 lines in `transpile_js_mir.cpp`.

#### F1. Compound Assignment Type Inference Fix

The `jm_get_effective_type` function returns `LMD_TYPE_ANY` for compound assignments (`+=`, `-=`):
```cpp
case JS_AST_NODE_ASSIGNMENT_EXPRESSION:
    if (asgn->op == JS_OP_ASSIGN) return jm_get_effective_type(mt, asgn->right);
    return LMD_TYPE_ANY;  // ← should infer numeric type from both sides
```

#### F2. Top-Level Function Body Native Emission

Enable selective deboxing within top-level code — track numeric locals as native even when the overall function can't be fully nativized.

#### F3. Native String Method Inlining

Inline `charCodeAt(i)` → direct byte load, `String.fromCharCode(n)` → single-char allocation. Critical for brainfuck's inner loop.

#### F4. Property Access Caching for Known Shapes

For objects with fixed literal shapes (nbody `{x, y, z, vx, vy, vz, mass}`), resolve field offsets at compile time and emit direct memory loads.

**Files:** `lambda/js/transpile_js_mir.cpp`, `lambda/js/js_runtime.cpp`  
**Lines changed:** ~200

---

## 5. Implementation Priority & Dependencies

```
Phase A (ES6 classes)         ──→ +10 benchmarks  [HIGH effort, HIGHEST value]
  ├── A1 prototype chain      (foundation)
  ├── A2 super() calls        (depends on A1)
  ├── A3 super.method()       (depends on A1)
  ├── A4 static methods       (independent of A2/A3)
  ├── A5 virtual dispatch     (depends on A1)
  └── A6 instanceof chain     (depends on A1)

Phase B (AST fixes)           ──→ +3 benchmarks   [LOW effort, HIGH value]
  ├── B1 template escapes     (independent)
  ├── B2 comment skipping     (independent)
  └── B3 destructuring asgn   (independent)

Phase C (Regression analysis) ──→ fix 3 regressions [LOW-MED effort, HIGH value]
  ├── C1 nqueens workload     (investigation)
  ├── C2 quicksort workload   (investigation)
  └── C3 sumfp float path     (investigation + possible fix)

Phase D (TypedArray perf)     ──→ +1 benchmark    [MEDIUM effort, HIGH value]
  ├── D1 pointer hoisting     (independent)
  ├── D2 bounds check elim    (depends on D1)
  ├── D3 native registers     (independent)
  └── D4 memcpy lowering      (independent, secondary)

Phase E (BigInt/Map/Regex/fs) ──→ +4 benchmarks   [HIGH effort, MEDIUM value]
  ├── E1 BigInt               (independent)
  ├── E2 Map                  (independent)
  ├── E3 RegExp               (independent)
  └── E4 require('fs')        (enables E2/E3 tests)

Phase F (Hot-loop deboxing)   ──→ systemic perf   [MEDIUM effort, HIGH value]
  ├── F1 compound assign type (independent)
  ├── F2 top-level native     (independent)
  ├── F3 string method inline (independent)
  └── F4 shape caching        (independent, larger)
```

**Recommended execution order:**
1. **Phase B** — Quick wins, 3 benchmarks fixed with ~100 LOC
2. **Phase C** — Investigate regressions, fix sumfp if genuine
3. **Phase A** — ES6 classes: biggest single impact (+10 benchmarks)
4. **Phase D** — TypedArray performance, makes fannkuch pass
5. **Phase F** — Systemic performance tuning
6. **Phase E** — BigInt/Map/RegExp/require for remaining 4

---

## 6. Performance Projection

### After Phase A (ES6 Classes)

| Benchmark | Status | Expected Perf vs Node.js |
|-----------|--------|--------------------------|
| sieve | ❌ → ✅ | ~0.03x (micro-benchmark, LambdaJS excels) |
| permute | ❌ → ✅ | ~0.01x |
| queens | ❌ → ✅ | ~0.01x |
| towers | ❌ → ✅ | ~0.01x |
| bounce | ❌ → ✅ | ~0.5–2x |
| storage | ❌ → ✅ | ~1–3x |
| json | ❌ → ✅ | ~2–5x |
| richards | ❌ → ✅ | ~3–8x (class-heavy, `this.method()` overhead) |
| havlak | ❌ → ✅ | ~5–10x |
| deltablue | ❌ → ✅ | ~5–10x (deep hierarchy, static fields) |
| cd | ❌ → ✅ | ~5–15x |
| cube3d (JS) | ❌ → ✅ | ~5–10x |
| hashmap (JS) | ❌ → ✅ | ~3–8x |

The class-heavy benchmarks (richards, deltablue, cd, havlak) will initially be slower than Node.js due to prototype chain walk overhead vs V8's hidden classes and inline caches. This is expected and can be improved in later optimization rounds.

### After Phase B + C (Correctness + Regressions)

| Benchmark | Status | Notes |
|-----------|--------|-------|
| binarytrees | ❌ → ✅ | Template escape fix |
| nbody (BENG) | ❌ → ✅ | Comment skip fix |
| levenshtein | ❌ → ✅ | Destructuring assignment fix |
| sumfp | 4.2ms → ~1.5ms | If float comparison regression confirmed and fixed |

### After Phase D (TypedArray Performance)

| Benchmark | Status | Expected |
|-----------|--------|----------|
| fannkuch | ❌ → ✅ | Timeout → ~2–5s |

### After All Phases

| Metric | Current | Target |
|--------|---------|--------|
| Benchmarks runnable | 50/62 | **62/62** |
| LambdaJS overall geo mean vs Node.js | 0.73x | ~0.6x |
| LambdaJS geo mean excl. AWFY micros | ~2.6x | ~2.0x |
| Self-time median ratio vs V8 | ~5x | ~3x |

---

## 7. Design Principles

1. **Reuse Lambda runtime infrastructure** — BigInt → `lambda-decimal.hpp`, RegExp → `re2_wrapper.hpp`, Map → `lib/hashmap.h`, file I/O → `lib/file_utils.h`, prototype chains → Lambda Map with hidden `__proto__` key. No new external dependencies.

2. **Same type system** — All JS values remain Lambda `Item` (64-bit tagged values). Classes use prototype chain via `__proto__` on Lambda Map objects. No new type tags (BigInt → `LMD_TYPE_DECIMAL`, Map → sentinel-marker wrapping, RegExp → sentinel-marker wrapping).

3. **Progressive optimization** — Each phase independently improves correctness or performance. No phase should create regressions in passing benchmarks. Always run `make test-lambda-baseline` after each phase.

4. **Prototype chain simplicity over V8 parity** — V8 uses hidden classes, inline caches, and speculative optimization. Lambda's JS engine uses static type inference and `__proto__` chain walks. This is simpler, deterministic, and sufficient for correctness. Performance optimization (F4: shape caching) can narrow the gap where it matters.

5. **Investigate before assuming regression** — The nqueens and quicksort "regressions" are most likely workload corrections (R2 had trivially small inputs). Always verify via git history before investing optimization effort. Only sumfp warrants immediate code-level investigation.

---

## 8. Test Plan

### Correctness Verification

After each phase, run against Node.js for output comparison:
```bash
# All benchmarks across all suites
cd test/benchmark && python3 run_js_benchmarks.py

# Individual AWFY class benchmarks (Phase A)
./lambda.exe js test/benchmark/awfy/sieve2_bundle.js
./lambda.exe js test/benchmark/awfy/bounce2_bundle.js
./lambda.exe js test/benchmark/awfy/deltablue2_bundle.js

# Regression benchmarks (Phase C)
./lambda.exe js test/benchmark/r7rs/sumfp2.js
./lambda.exe js test/benchmark/r7rs/nqueens2.js
./lambda.exe js test/benchmark/larceny/quicksort.js
```

### Regression Testing

```bash
make test-lambda-baseline    # Lambda core tests — must pass 100%
```

### Performance Benchmarking

**Release build only** (`make release`), median of 3 runs:
```bash
# Compare self-reported timing (__TIMING__ output)
./lambda.exe js test/benchmark/r7rs/sumfp2.js    # verify regression fix
node test/benchmark/r7rs/sumfp2.js

# AWFY class benchmarks — baseline perf after Phase A
./lambda.exe js test/benchmark/awfy/richards2_bundle.js
node test/benchmark/awfy/richards2_bundle.js
```

---

## 9. Summary

| Phase | Benchmarks Fixed | Key Deliverable | Effort | Priority |
|-------|-----------------|-----------------|--------|----------|
| **A: ES6 classes** | +10 (6 AWFY + 4 JetStream) | `extends`/`super()`/`static` | ~600 LOC | **P0** |
| **B: AST fixes** | +3 (binarytrees, nbody, levenshtein) | Template escapes, comments, destructuring | ~100 LOC | **P0** |
| **C: Regressions** | 3 investigated, sumfp fix | Workload verification + float path fix | ~100 LOC | **P0** |
| **D: TypedArray perf** | +1 (fannkuch) | Pointer hoisting, bounds elimination | ~150 LOC | **P1** |
| **E: BigInt/Map/Regex/fs** | +4 (pidigits, knucleotide, regexredux, revcomp) | Reuse Lambda infrastructure | ~900 LOC | **P2** |
| **F: Deboxing** | systemic perf ~5x→~3x | Compound assign, string inlining, shapes | ~200 LOC | **P2** |
| **Total** | **62/62 runnable (100%)** | | **~2050 LOC** | |
