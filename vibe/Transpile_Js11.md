# Transpile_Js11: Language Completeness & Performance — Phase 2

## 1. Executive Summary

After v10, LambdaJS passes **12/13 JetStream JS** files, **62/62** across 5 benchmark suites, and **all 633 baseline tests**. The engine handles ES6 classes, prototype OOP, multi-level closures, typed arrays, getter properties, try/catch/finally, object destructuring, and 90+ standard library methods.

**v11 goal:** Two parallel tracks:
- **Track A — Performance:** Implement the remaining P10 optimizations (property hash table, regular array fast path, float boxing elimination, constructor optimization, string interning) to close the geometric mean gap from 8.8× to a target of ~3–5× vs Node.js.
- **Track B — Language:** Add the most impactful missing language features — optional chaining, `Map`/`Set` collections, `Function.bind`, regex `.test()`/`.exec()`, error subclasses, Date instance methods, and sequence expressions — to support general-purpose JS programs beyond benchmarks.

### v11 Implementation Status

| Item | Track | Status | Notes |
|------|:-----:|:------:|-------|
| A1 Property hash table | Perf | ❌ NOT STARTED | Carry-forward from P10a |
| A2 Regular array fast path | Perf | ❌ NOT STARTED | Carry-forward from P10b |
| A3 Float boxing elimination | Perf | ❌ NOT STARTED | Carry-forward from P10c |
| A4 Integer index fast path (transpiler) | Perf | ❌ NOT STARTED | Complete P10e transpiler integration |
| A5 Constructor shape pre-alloc | Perf | ❌ NOT STARTED | Carry-forward from P10g |
| A6 Property name interning | Perf | ❌ NOT STARTED | Carry-forward from P10h |
| B1 Optional chaining (`?.`) | Lang | ❌ NOT STARTED | |
| B2 `Map` / `Set` collections | Lang | ❌ NOT STARTED | |
| B3 `Function.prototype.bind` | Lang | ❌ NOT STARTED | |
| B4 Regex `.test()` / `.exec()` | Lang | ❌ NOT STARTED | |
| B5 Error subclasses | Lang | ❌ NOT STARTED | |
| B6 Date instance methods | Lang | ❌ NOT STARTED | |
| B7 Sequence expressions | Lang | ❌ NOT STARTED | |
| B8 Labeled statements | Lang | ❌ NOT STARTED | |
| B9 Nullish coalescing assignment (`??=`) | Lang | ❌ NOT STARTED | |
| B10 `Object.fromEntries` / `Object.is` | Lang | ❌ NOT STARTED | |

---

## 2. Track A — Performance Optimizations

All items in this track target reducing the geometric mean ratio from **8.8×** toward **3–5×** vs Node.js. Each optimization addresses a specific bottleneck identified in the v10 benchmark analysis.

### A1: Property Access Hash Table (Impact: HIGH)

**Carry-forward from P10a.** This is the single highest-impact optimization remaining.

**Problem:** Every property access (`obj.prop`) walks the `ShapeEntry` linked list with `strncmp` at every node. Objects with `k` properties cost O(k) per read. In OOP-heavy benchmarks (richards, deltablue, havlak, cd), hundreds of millions of property reads dominate runtime.

**Approach:** Add inline hash table to `TypeMap` for O(1) property lookup by name.

```
TypeMap {
    ShapeEntry* shape;           // existing: iteration order
    ShapeEntry* field_index[N];  // new: hash buckets (N = 8 or 16)
    uint8_t     field_count;     // new: number of fields
}
```

For objects with ≤16 properties (covers >99% of JS objects), use a small fixed-size open-addressing table indexed by `FNV1a(name) & (N-1)`. Lookup: one hash, one or two pointer comparisons.

**Implementation plan:**
1. Add `field_index[16]` and `field_count` to `TypeMap` (or a new `TypeMapJS` variant)
2. In `shape_builder.cpp` / `map_put`: populate hash index when adding `ShapeEntry`
3. New runtime function `js_map_get_hashed(Map* m, const char* key, int len)`:
   - Compute `hash = fnv1a(key, len) & (field_count - 1)` (when field_count is power of 2)
   - Probe `field_index[hash]` — on name match, return `_map_read_field` directly
   - Linear probe on collision (max 2–3 probes for small tables)
   - Fall back to `js_map_get_fast` linked-list walk if table overflows
4. Wire `js_property_get` to use `js_map_get_hashed` for Map objects

**Code locations:**
- `lambda/shape_builder.cpp` — shape creation and field addition
- `lambda/shape_pool.hpp` — `TypeMap` / `ShapeEntry` definitions
- `lambda/js/js_runtime.cpp` — `js_property_get`, `js_property_set`
- `lambda/lambda-data-runtime.cpp` — `_map_get`, `_map_read_field`

**Estimated improvement:** 2–10× for object-heavy benchmarks (richards, deltablue, hash-map, havlak, cd, splay).

**Estimated effort:** ~100–150 LOC across shape_builder, shape_pool, js_runtime.

---

### A2: Regular Array Inline Fast Path (Impact: HIGH)

**Carry-forward from P10b.**

**Problem:** Regular JS array element access goes through: `js_property_get` → string key compare with "length" → `js_get_number(idx)` → type dispatch (6 branches) → double → int → bounds check → `items[idx]`. This is ~20 instructions for what should be a single indexed load.

Typed arrays already have inline MIR emission (direct `data[idx * elem_size]` from v5/P9). Regular arrays need the same treatment.

**Approach:** When the transpiler knows a variable holds a regular array (from `new Array(n)`, array literal `[...]`, or `Array.from()`), emit inline MIR for element access.

**Transpiler changes (`transpile_js_mir.cpp`):**

1. Add `is_js_array` flag to `JsMirVarEntry` (similar to `typed_array_type`)
2. Mark variables assigned from array constructors/literals
3. In `jm_transpile_member_expression`, when object is a known array and index is a computed expression:

```
// Inline MIR pseudo-code:
Container* c = (Container*)arr_reg;
int64_t count = c->count;             // load count field
int64_t idx = unbox_int(idx_reg);     // if idx known INT
branch (idx >= 0 && idx < count) -> fast_path, slow_path

fast_path:
    result = c->items[idx];           // single indexed load
    jump done

slow_path:
    result = call js_array_get(arr, idx);  // full runtime fallback

done:
    // result in target register
```

4. Same pattern for array set with `js_array_set` fallback

**Integrates with A4:** The `js_array_get_int` / `js_array_set_int` runtime functions from P10e serve as the fallback path. When full inlining isn't possible (unknown array type), the transpiler can still emit `js_array_get_int` for known-INT indices.

**Code locations:**
- `lambda/js/transpile_js_mir.cpp` — `jm_transpile_member_expression`, `jm_transpile_assignment`
- `lambda/js/js_transpiler.hpp` — `JsMirVarEntry` struct
- `lambda/js/js_runtime.cpp` — `js_array_get_int`, `js_array_set_int` (already exist from P10e)

**Estimated improvement:** 3–10× for array-heavy numeric benchmarks (navier-stokes, spectral-norm, fft, splay).

**Estimated effort:** ~150–200 LOC in transpile_js_mir.cpp.

---

### A3: Float Boxing Elimination (Impact: HIGH)

**Carry-forward from P10c.**

**Problem:** Every intermediate float result allocates on the GC heap via `js_make_number`:
```cpp
double *p = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
*p = d;
return (Item){(uint64_t)p};
```

In tight numeric loops (navier-stokes inner loop: ~70M allocations), this dominates runtime and triggers GC pressure. V8 avoids this entirely via NaN boxing with 64-bit tagged values.

**Approach:** Expression-level float register tracking — keep intermediate results in native `MIR_T_D` registers through compound expressions, only boxing at assignment to array/object/return.

**Implementation:**

1. **Type-tracked expressions:** Extend `jm_transpile_expression` to return a `{MIR_reg_t, NativeType}` pair where `NativeType ∈ {BOXED, NATIVE_INT, NATIVE_FLOAT}`. Binary ops on two `NATIVE_FLOAT` inputs produce `NATIVE_FLOAT` output — no boxing.

2. **Deferred boxing:** At assignment boundaries (`arr[i] = expr`, `this.prop = expr`, function call arguments), box native values. Compound expressions like `(a + b * c) * d` produce zero intermediate allocations.

3. **Loop variable widening:** The existing `jm_prescan_float_widening` handles `x += delta` patterns. Extend to cover `x = expr` when `expr` is known-float and `x` is a loop-local variable.

4. **Native comparison fast path:** `a < b` with two native floats emits MIR `DBLT` directly — no runtime call.

**Example transformation (navier-stokes inner loop):**
```javascript
// Before: 6 float allocations per iteration
lastX = x[currentRow] = (x0[currentRow] + a*(lastX+x[++currentRow]+x[++lastRow]+x[++nextRow])) * invC;

// After: 0 intermediate allocations
// lastX, a, invC kept as MIR_T_D registers
// x0[currentRow] → js_array_get_int → js_get_number → native double
// All arithmetic in native double registers
// Only box once at x[currentRow] = ... assignment
```

**Code locations:**
- `lambda/js/transpile_js_mir.cpp` — binary op emission, `jm_transpile_binary_expression`, `jm_ensure_native_number`, `jm_prescan_float_widening`
- `lambda/js/js_runtime.cpp` — `js_make_number` (unchanged, just called less often)

**Estimated improvement:** 3–8× for float-heavy benchmarks (navier-stokes, nbody, mandelbrot, fft).

**Estimated effort:** ~120–180 LOC in transpile_js_mir.cpp.

---

### A4: Integer Index Fast Path — Transpiler Integration (Impact: MEDIUM)

**Complete P10e.** The runtime functions `js_array_get_int` and `js_array_set_int` exist but are not yet emitted by the transpiler.

**Approach:** In `jm_transpile_member_expression`, when the computed member index expression has a known INT effective type (from `jm_get_effective_type`), emit a call to `js_array_get_int` with the index as a native `int64_t` instead of a boxed `Item`.

```cpp
// In jm_transpile_member_expression:
if (is_computed && jm_get_effective_type(mt, index_node) == LMD_TYPE_INT) {
    MIR_reg_t idx_native = jm_transpile_as_native(mt, index_node, LMD_TYPE_INT, LMD_TYPE_INT);
    result = jm_call_ext(mt, "js_array_get_int", MIR_T_I64, obj_reg, MIR_T_I64, idx_native, MIR_T_I64);
}
```

Similarly for assignment with `js_array_set_int`.

This is a prerequisite for A2 (the full inline fast path uses `js_array_get_int` as its fallback).

**Code locations:**
- `lambda/js/transpile_js_mir.cpp` — `jm_transpile_member_expression`, `jm_transpile_assignment`

**Estimated improvement:** 1.3–2× for array-heavy benchmarks (stacks with A2 for higher gain).

**Estimated effort:** ~50–80 LOC in transpile_js_mir.cpp.

---

### A5: Constructor Shape Pre-allocation (Impact: MEDIUM)

**Carry-forward from P10g.**

**Problem:** `new Entry(key, hash, value)` in hash-map performs: 1 empty object creation + N `js_property_set` calls that each extend the `ShapeEntry` linked list. For 90K constructor calls with 4 properties each, this means 360K linked-list extension operations.

**Approach:** Analyze constructor bodies for `this.prop = ...` patterns during the collection phase, pre-build a shape with all property slots, and allocate the object with the full shape at `new` time.

**Implementation:**

1. **Constructor analysis (transpile_js_mir.cpp, collection phase):**
   - During `jm_collect_functions`, when a function is marked as a constructor (used with `new`), scan its body for `this.property = expr` assignment patterns.
   - Record the property names in order as the constructor's "shape template."

2. **Shape template registration (shape_builder.cpp):**
   - `js_register_constructor_shape(const char** prop_names, int count)` → returns a `TypeMap*` with pre-allocated `ShapeEntry` nodes for all properties.
   - Store the template shape on the constructor function's metadata.

3. **Fast object creation (js_runtime.cpp):**
   - `js_new_object_from_shape(TypeMap* shape)` — allocates object with pre-sized data buffer and pre-linked `ShapeEntry` list. All property slots initialized to `ITEM_JS_UNDEFINED`.
   - Subsequent `js_property_set(this, "prop", val)` becomes a direct slot write (O(1) via hash index from A1) instead of shape extension.

4. **Prototype setup:** Attach `__proto__` to the pre-built shape so prototype chain is established at allocation time.

**Code locations:**
- `lambda/js/transpile_js_mir.cpp` — `jm_collect_functions`, `jm_create_func_or_closure`
- `lambda/js/js_runtime.cpp` — `js_new_object_for_construct`
- `lambda/shape_builder.cpp` — new `js_register_constructor_shape`

**Estimated improvement:** 2–5× for constructor-heavy code (hash-map, richards).

**Estimated effort:** ~150–200 LOC across transpile_js_mir.cpp, js_runtime.cpp, shape_builder.cpp.

---

### A6: Property Name Interning (Impact: LOW-MEDIUM)

**Carry-forward from P10h.**

**Problem:** Property access converts the property name to a runtime string then uses `strncmp`/`memcmp`. For literal property names known at compile time (`.length`, `.prototype`, `.key`, `.value`), this is redundant work repeated millions of times.

**Approach:** At module load time, intern all property name string literals used in the program. Use interned `Item` keys and pointer comparison in the hot path.

**Implementation:**

1. **Name table construction (transpile_js_mir.cpp):**
   - During the collection phase, gather all string literals used as property keys (member expressions, object literal keys).
   - Emit a module-init section that calls `heap_create_name(str, len)` once per unique property name and stores the result in a global `MIR_item_t` variable.

2. **Interned property access (js_runtime.cpp):**
   - New function `js_property_get_interned(Item obj, Item interned_key)` that checks `ShapeEntry->name` pointer equality first, falling back to string compare only on mismatch.
   - With A1's hash table, this becomes: hash probe → pointer compare → done (2 operations).

3. **Common globals pre-interned:**
   - Pre-intern the top ~20 property names at engine startup: `length`, `prototype`, `__proto__`, `constructor`, `toString`, `valueOf`, `hasOwnProperty`, `push`, `pop`, `map`, `filter`, `reduce`, `indexOf`, `slice`, `concat`, `keys`, `values`, `entries`, `call`, `apply`.
   - These cover the vast majority of property accesses in typical JS programs.

**Code locations:**
- `lambda/js/transpile_js_mir.cpp` — collection phase, module init emission
- `lambda/js/js_runtime.cpp` — `js_property_get_interned`
- `lambda/js/js_globals.cpp` — pre-interned name table

**Estimated improvement:** 1.3–2× for code with repeated property access patterns.

**Estimated effort:** ~100–150 LOC across transpile_js_mir.cpp, js_runtime.cpp, js_globals.cpp.

---

### Track A — Performance Priority Order

| Priority | Item | Impact | Effort | Benchmarks Affected |
|:--------:|------|:------:|:------:|---------------------|
| **1** | A1 Property hash table | HIGH | ~130 LOC | richards, deltablue, hash-map, havlak, cd, splay |
| **2** | A4 Integer index transpiler | MED | ~60 LOC | all array benchmarks (prerequisite for A2) |
| **3** | A2 Regular array fast path | HIGH | ~180 LOC | navier-stokes, spectral-norm, fft, splay |
| **4** | A3 Float boxing elimination | HIGH | ~150 LOC | navier-stokes, nbody, mandelbrot, fft |
| **5** | A5 Constructor shape pre-alloc | MED | ~180 LOC | hash-map, richards |
| **6** | A6 Property name interning | LOW-MED | ~130 LOC | all (cumulative) |

**Total Track A estimated effort:** ~830 LOC

**Expected outcome:** Geometric mean ratio drops from 8.8× to ~3–5×. The "Moderate" tier (5–50×) benchmarks should largely move into "Comparable" (1–5×). The "Severe" tier (50–200×) should move to "Moderate." The "Critical" (>200×) benchmarks (nbody, havlak, cd) will improve but likely remain in the 20–80× range due to deep OOP and GC patterns that require more fundamental architectural changes.

---

## 3. Track B — Language Completeness

These features target running **general-purpose JavaScript programs** — real-world libraries, utility code, and small applications. Each feature is common in everyday JS and blocking for non-benchmark code.

### B1: Optional Chaining (`?.`) (Impact: HIGH)

**Current state:** Not supported. Parser does not recognize `?.` tokens.

**Why it matters:** Optional chaining is pervasive in modern JS (ES2020). Any real-world code written after 2020 likely uses it. Without support, LambdaJS cannot run most contemporary JavaScript.

**Patterns to support:**
```javascript
obj?.prop              // property access
obj?.method()          // method call
obj?.[expr]            // computed member
arr?.[0]               // array optional access
a?.b?.c?.d             // chained
```

**Semantics:** If the left-hand side is `null` or `undefined`, short-circuit to `undefined` without evaluating the right side.

**Implementation:**

1. **Grammar/Parser (tree-sitter-javascript):**
   - Tree-sitter-javascript already supports optional chaining as `optional_chain_expression` (check existing grammar rules). If already parsed, the AST builder just needs to handle the node type.

2. **AST builder (`build_js_ast.cpp`):**
   - Add `JS_NODE_OPTIONAL_CHAIN` or a boolean `optional` flag on existing `JS_NODE_MEMBER_EXPRESSION` and `JS_NODE_CALL_EXPRESSION`.
   - When the Tree-sitter node has child `?.`, set the flag.

3. **Transpiler (`transpile_js_mir.cpp`):**
   - In `jm_transpile_member_expression` / `jm_transpile_call_expression`:
   - If `optional` flag is set, emit a null/undefined check on the object:
     ```
     check = (obj == ITEM_NULL || obj == ITEM_JS_UNDEFINED)
     branch check -> short_circuit, continue
     short_circuit:
         result = ITEM_JS_UNDEFINED
         jump done
     continue:
         // normal property access / method call
     done:
     ```
   - For chained `a?.b?.c`, each level independently checks.

**Code locations:**
- `lambda/js/build_js_ast.cpp` — member_expression / call_expression handling
- `lambda/js/js_ast.hpp` — add `optional` flag to `JsMemberExpression` and `JsCallExpression`
- `lambda/js/transpile_js_mir.cpp` — `jm_transpile_member_expression`, `jm_transpile_call_expression`

**Estimated effort:** ~80–120 LOC across build_js_ast.cpp, js_ast.hpp, transpile_js_mir.cpp.

---

### B2: `Map` and `Set` Collections (Impact: HIGH)

**Current state:** Not supported. `Map` and `Set` are undefined.

**Why it matters:** `Map` and `Set` are the most-used ES6 collections. Many algorithms, caches, deduplication patterns, and data-processing tasks rely on them. Their absence blocks a large class of real-world JS programs.

**Map API to implement:**
```javascript
const m = new Map();
m.set(key, value);       // returns the Map (for chaining)
m.get(key);              // returns value or undefined
m.has(key);              // boolean
m.delete(key);           // boolean
m.clear();
m.size;                  // property (getter)
m.forEach(callback);
m.keys();                // returns array (simplified iterator)
m.values();              // returns array
m.entries();             // returns array of [k,v] pairs
```

**Set API to implement:**
```javascript
const s = new Set();
s.add(value);            // returns the Set
s.has(value);            // boolean
s.delete(value);         // boolean
s.clear();
s.size;                  // property (getter)
s.forEach(callback);
s.values();              // returns array
```

**Implementation strategy — back with Lambda's `HashMap`:**

Rather than building a new hash table from scratch, leverage the existing Lambda `HashMap` from `lib/hashmap.h` for the underlying storage.

1. **Runtime representation (`js_runtime.cpp`):**
   - `Map`: A Lambda Map (TypeMap) with a special `__js_map__` flag and an embedded `HashMap` for O(1) key lookup. Keys are stored as `Item` values (not just strings), requiring an `Item`-based hash function.
   - `Set`: Same structure as `Map` but values are always `true`.

2. **Item hash function:**
   - String keys: `fnv1a(str, len)`
   - Integer keys: identity hash
   - Float keys: hash the bits
   - Object keys: hash the pointer value (reference identity)
   - Combined: `js_item_hash(Item key) → uint32_t`

3. **Constructor dispatch (`transpile_js_mir.cpp`):**
   - When transpiling `new Map()`, emit call to `js_map_collection_new()`.
   - When transpiling `m.set(k, v)`, recognize `Map` method calls via variable type tracking and dispatch to `js_map_collection_set(m, k, v)`.

4. **Method dispatch:**
   - Add `Map` and `Set` to the global constructor table in `js_globals.cpp`.
   - Method calls on Map/Set instances dispatch based on the `__js_map__` / `__js_set__` flag checked in `js_property_get` (similar to how typed arrays are dispatched).

**Code locations:**
- `lambda/js/js_runtime.cpp` — new `js_map_collection_*` and `js_set_collection_*` functions
- `lambda/js/js_runtime.h` — declarations
- `lambda/js/js_globals.cpp` — register `Map`/`Set` constructors
- `lambda/js/transpile_js_mir.cpp` — constructor recognition, method dispatch
- `lib/hashmap.h` — underlying hash table

**Estimated effort:** ~300–400 LOC across js_runtime.cpp, js_globals.cpp, transpile_js_mir.cpp.

---

### B3: `Function.prototype.bind` (Impact: MEDIUM)

**Current state:** `call` and `apply` are supported. `bind` is not.

**Why it matters:** `bind` is widely used for partial application, event handlers, and method extraction (`const fn = obj.method.bind(obj)`). Many libraries and frameworks depend on it.

**Semantics:**
```javascript
const bound = fn.bind(thisArg, arg1, arg2);
bound(arg3);  // equivalent to fn.call(thisArg, arg1, arg2, arg3)
```

**Implementation:**

1. **Bound function representation:**
   - A bound function is a Lambda Map object with special fields:
     - `__bound_target__`: the original function `Item`
     - `__bound_this__`: the `thisArg` value
     - `__bound_args__`: array of pre-applied arguments (may be empty)
     - `__js_bound__`: boolean flag for type detection

2. **`js_function_bind(Item fn, Item thisArg, Item* args, int argc)`** (`js_runtime.cpp`):
   - Creates the bound function Map object with the fields above.
   - Returns the Map as a callable (detected by `__js_bound__` flag during call dispatch).

3. **Call dispatch integration:**
   - In `js_call_function` (or wherever function calls are dispatched), check for `__js_bound__` flag.
   - If bound: extract target, this, and pre-args. Concatenate pre-args with call-site args. Call target with bound this.

4. **Transpiler:**
   - Recognize `.bind(` method call pattern. Emit call to `js_function_bind`.

**Code locations:**
- `lambda/js/js_runtime.cpp` — `js_function_bind`, call dispatch modification
- `lambda/js/transpile_js_mir.cpp` — `.bind()` method call recognition

**Estimated effort:** ~80–120 LOC in js_runtime.cpp, transpile_js_mir.cpp.

---

### B4: Regex `.test()` and `.exec()` (Impact: MEDIUM)

**Current state:** Regex literals are partially supported via RE2 backend. `String.search` and `String.replace` work with regex. But `regex.test(str)` and `regex.exec(str)` — the two most common regex instance methods — are not implemented.

**Why it matters:** Nearly all regex usage in JS goes through `.test()` or `.exec()`. Without them, any code that validates input, parses tokens, or extracts patterns fails.

**API to implement:**
```javascript
const re = /pattern/flags;
re.test(str);            // returns boolean
re.exec(str);            // returns [match, ...groups, index, input] or null
str.match(re);           // returns same as exec() for non-global; all matches for /g
```

**Implementation:**

1. **`js_regex_test(Item regex, Item str)`** (`js_runtime.cpp`):
   - Extract pattern string from the regex `Item`.
   - Call `re2_partial_match(pattern, str)` (already available via `re2_wrapper.hpp`).
   - Return `ITEM_TRUE` / `ITEM_FALSE`.

2. **`js_regex_exec(Item regex, Item str)`** (`js_runtime.cpp`):
   - Call `re2_match_with_groups(pattern, str, &groups, &count)`.
   - Build result array: `[full_match, group1, group2, ...]`.
   - Set `.index` property on the result array (index of match in string).
   - Set `.input` property (the original string).
   - Return the array, or `ITEM_NULL` if no match.

3. **`String.match(regex)`** (`js_runtime.cpp`):
   - Non-global: delegate to `js_regex_exec`.
   - Global (`/g`): collect all matches into an array.

4. **Regex flags tracking:**
   - Store flags (`g`, `i`, `m`, `s`) as a field on the regex Item.
   - Pass `i` flag to RE2 as `RE2::Options.set_case_sensitive(false)`.
   - Track `lastIndex` for global regexes.

**Code locations:**
- `lambda/js/js_runtime.cpp` — `js_regex_test`, `js_regex_exec`, `js_string_match`
- `lambda/re2_wrapper.cpp` / `re2_wrapper.hpp` — RE2 integration layer

**Estimated effort:** ~120–180 LOC in js_runtime.cpp.

---

### B5: Error Subclasses (Impact: MEDIUM)

**Current state:** Only generic `Error` constructor. `TypeError`, `RangeError`, `SyntaxError`, `ReferenceError` are undefined.

**Why it matters:** Many JS programs use `catch (e) { if (e instanceof TypeError) ... }` for error-specific handling. Libraries throw typed errors. Without subclasses, error type detection fails.

**Implementation:**

1. **Error object structure:**
   - Extend the existing `Error` creation with a `name` property:
     ```javascript
     new TypeError("msg")  → { message: "msg", name: "TypeError", stack: "..." }
     new RangeError("msg") → { message: "msg", name: "RangeError", stack: "..." }
     ```

2. **Constructor registration (`js_globals.cpp`):**
   - Register `TypeError`, `RangeError`, `SyntaxError`, `ReferenceError` as global constructors.
   - Each calls `js_new_error_with_name(name, message)` which creates an Error Map with the appropriate `name` field.

3. **`instanceof` support:**
   - Set `__proto__` on each error instance to point to the corresponding prototype (e.g., `TypeError.prototype`).
   - `instanceof TypeError` walks the prototype chain and matches.

4. **Transpiler:**
   - No transpiler changes needed — constructors resolve via global variable lookup, same as `Error`.

**Code locations:**
- `lambda/js/js_runtime.cpp` — `js_new_error_with_name`
- `lambda/js/js_globals.cpp` — register 4 error subclass constructors

**Estimated effort:** ~60–80 LOC in js_globals.cpp, js_runtime.cpp.

---

### B6: Date Instance Methods (Impact: MEDIUM)

**Current state:** `Date.now()` and `new Date()` are supported. No instance methods.

**Why it matters:** Date manipulation is common in data processing, logging, and formatted output. Without instance methods, Date objects are useless beyond timestamp creation.

**Methods to implement (priority order):**

| Method | Returns | Notes |
|--------|---------|-------|
| `getTime()` | ms since epoch | Core — everything else derives from this |
| `getFullYear()` | 4-digit year | |
| `getMonth()` | 0–11 | |
| `getDate()` | 1–31 | |
| `getDay()` | 0–6 (Sun=0) | |
| `getHours()` | 0–23 | |
| `getMinutes()` | 0–59 | |
| `getSeconds()` | 0–59 | |
| `getMilliseconds()` | 0–999 | |
| `toISOString()` | "YYYY-MM-DDTHH:MM:SS.sssZ" | |
| `toLocaleDateString()` | locale string | Simplified: same as toISOString date part |
| `toString()` | full string | |

**Implementation:**

1. **Date representation:**
   - Store Date as a Lambda datetime `Item` (already supported by the Lambda runtime).
   - `new Date()` → `make_datetime_item(current_ms)`.
   - `new Date(ms)` → `make_datetime_item(ms)`.
   - `new Date(year, month, day, ...)` → construct from components.

2. **Method dispatch (`js_runtime.cpp`):**
   - In `js_property_get`, when the object is a datetime `Item` and the property is a known Date method name, return a bound method or dispatch directly.
   - Helper: `js_date_get_component(Item date, int component)` uses C `gmtime_r` / `localtime_r` to decompose the timestamp.

3. **`toISOString()`:**
   - `strftime` with `"%Y-%m-%dT%H:%M:%S"` + milliseconds + `"Z"`.

**Code locations:**
- `lambda/js/js_runtime.cpp` — `js_date_*` methods
- `lambda/js/js_globals.cpp` — Date constructor variants

**Estimated effort:** ~120–160 LOC in js_runtime.cpp, js_globals.cpp.

---

### B7: Sequence Expressions (Impact: LOW)

**Current state:** Carry-forward from v9 A2. Parsed by Tree-sitter but not transpiled.

**Semantics:** `(expr1, expr2, expr3)` evaluates all expressions left-to-right, returns the last value. Common in minified code and `for` loop headers.

**Implementation:**

1. **AST builder (`build_js_ast.cpp`):**
   - `sequence_expression` → `JS_NODE_SEQUENCE_EXPRESSION` with list of child expressions.

2. **Transpiler (`transpile_js_mir.cpp`):**
   - Transpile each child expression in order. Discard results of all but the last. Return the last result.

```cpp
case JS_NODE_SEQUENCE_EXPRESSION: {
    JsNode* child = node->first_child;
    MIR_reg_t result = 0;
    while (child) {
        result = jm_transpile_expression(mt, child);
        child = child->next_sibling;
    }
    return result;
}
```

**Code locations:**
- `lambda/js/build_js_ast.cpp` — sequence_expression handling
- `lambda/js/js_ast.hpp` — `JS_NODE_SEQUENCE_EXPRESSION` enum entry
- `lambda/js/transpile_js_mir.cpp` — expression dispatch

**Estimated effort:** ~30–50 LOC.

---

### B8: Labeled Statements (Impact: LOW)

**Current state:** Not parsed, not transpiled.

**Semantics:**
```javascript
outer: for (let i = 0; i < n; i++) {
    inner: for (let j = 0; j < m; j++) {
        if (cond) break outer;     // break out of outer loop
        if (cond2) continue outer; // continue outer loop
    }
}
```

**Implementation:**

1. **AST builder:**
   - `labeled_statement` → `JS_NODE_LABELED_STATEMENT` with `label` name and `body` statement.
   - `break_statement` / `continue_statement` with optional label child.

2. **Transpiler:**
   - Track label → `JsLoopLabels` mapping in a label table.
   - When a `labeled_statement` wraps a loop, associate the label with the loop's break/continue MIR labels.
   - `break label` → jump to the labeled loop's break target.
   - `continue label` → jump to the labeled loop's continue target.

**Code locations:**
- `lambda/js/build_js_ast.cpp` — labeled_statement, break/continue label child
- `lambda/js/transpile_js_mir.cpp` — label table management, `jm_transpile_break/continue`

**Estimated effort:** ~60–80 LOC.

---

### B9: Nullish Coalescing Assignment (`??=`, `||=`, `&&=`) (Impact: LOW)

**Current state:** `??` is supported, but compound assignment variants are not.

**Semantics:**
```javascript
x ??= defaultValue;   // x = x ?? defaultValue (assign if null/undefined)
x ||= fallback;       // x = x || fallback (assign if falsy)
x &&= transform;      // x = x && transform (assign if truthy)
```

**Implementation:**

1. **AST builder:** Recognize `??=`, `||=`, `&&=` as assignment operators. Tree-sitter-javascript already parses these as `augmented_assignment_expression` with the operator as a child.

2. **Transpiler:** In `jm_transpile_assignment`:
   - `??=`: emit null/undefined check on LHS, conditional assign.
   - `||=`: emit truthiness check on LHS, conditional assign.
   - `&&=`: emit truthiness check on LHS, conditional assign of RHS.

Short-circuit semantics: RHS is only evaluated if the condition holds.

**Code locations:**
- `lambda/js/transpile_js_mir.cpp` — `jm_transpile_assignment`, compound assignment handling

**Estimated effort:** ~40–60 LOC.

---

### B10: `Object.fromEntries` / `Object.is` (Impact: LOW)

**Current state:** `Object.keys`, `Object.values`, `Object.entries`, `Object.assign`, `Object.create`, `Object.defineProperty`, `Object.freeze`, `Object.isFrozen`, `Object.getPrototypeOf`, `Object.setPrototypeOf`, `hasOwnProperty` are all implemented.

**`Object.fromEntries(iterable)`:**
```javascript
const obj = Object.fromEntries([['a', 1], ['b', 2]]);  // { a: 1, b: 2 }
const obj2 = Object.fromEntries(map.entries());
```
- Iterate array of `[key, value]` pairs, call `js_property_set` for each.
- ~30 LOC in `js_globals.cpp`.

**`Object.is(a, b)`:**
```javascript
Object.is(NaN, NaN);     // true (unlike ===)
Object.is(0, -0);        // false (unlike ===)
Object.is(1, 1);         // true
```
- Implements SameValue algorithm: `NaN === NaN` → true, `+0 !== -0`.
- ~20 LOC in `js_globals.cpp`.

**Code locations:**
- `lambda/js/js_globals.cpp` — register as static methods on `Object`

**Estimated effort:** ~50 LOC.

---

### Track B — Language Priority Order

| Priority | Item | Impact | Effort | Rationale |
|:--------:|------|:------:|:------:|-----------|
| **1** | B1 Optional chaining (`?.`) | HIGH | ~100 LOC | Most impactful modern syntax; blocks real-world code |
| **2** | B2 `Map` / `Set` collections | HIGH | ~350 LOC | Most-used ES6 data structures |
| **3** | B3 `Function.bind` | MED | ~100 LOC | Widely used in OOP and event-driven code |
| **4** | B4 Regex `.test()` / `.exec()` | MED | ~150 LOC | Enables pattern matching—core JS idiom |
| **5** | B5 Error subclasses | MED | ~70 LOC | Small effort, unblocks error-typed code |
| **6** | B6 Date instance methods | MED | ~140 LOC | Common in data processing, logging |
| **7** | B7 Sequence expressions | LOW | ~40 LOC | Required for minified code |
| **8** | B8 Labeled statements | LOW | ~70 LOC | Niche but used in algorithms |
| **9** | B9 Nullish assignment ops | LOW | ~50 LOC | Modern syntax, easy win |
| **10** | B10 `Object.fromEntries`/`is` | LOW | ~50 LOC | Small effort, completes Object API |

**Total Track B estimated effort:** ~1,120 LOC

---

## 4. Combined Implementation Plan

### Phase 1: Performance Foundation (A1, A4, A6)

Estimated ~340 LOC. Addresses the core property-access bottleneck.

1. **A1** — Property hash table: O(1) property lookup replaces O(n) linked-list walk.
2. **A4** — Integer index transpiler integration: wire existing `js_array_get_int`/`js_array_set_int` into the transpiler for known-INT indices.
3. **A6** — Property name interning: compile-time intern + pointer compare for hot property names.

**Validation:** Re-run full benchmark suite. Expected 1.5–3× improvement on object-heavy benchmarks.

### Phase 2: Array & Numeric Fast Path (A2, A3)

Estimated ~330 LOC. Targets the numeric benchmark tier.

4. **A2** — Regular array inline MIR: direct `items[idx]` load for known arrays.
5. **A3** — Float boxing elimination: keep intermediates in native double registers.

**Validation:** navier-stokes, nbody, spectral-norm, mandelbrot timings. Expected 2–5× improvement on numeric-heavy benchmarks.

### Phase 3: Modern Syntax (B1, B5, B7, B9)

Estimated ~260 LOC. Adds the most impactful modern JS syntax features.

6. **B1** — Optional chaining: null-check short-circuit for `?.` expressions.
7. **B5** — Error subclasses: `TypeError`, `RangeError`, `SyntaxError`, `ReferenceError`.
8. **B7** — Sequence expressions: `(a, b, c)` evaluates all, returns last.
9. **B9** — Nullish assignment operators: `??=`, `||=`, `&&=`.

**Validation:** New test scripts for each feature. Run baseline suite to confirm no regression.

### Phase 4: Collections & Standard Library (B2, B3, B4, B6, B10)

Estimated ~790 LOC. The largest phase, adding essential built-in types and methods.

10. **B2** — `Map` / `Set` collections backed by Lambda's `HashMap`.
11. **B3** — `Function.bind` with partial application.
12. **B4** — Regex `.test()` / `.exec()` / `String.match()`.
13. **B6** — Date instance methods (12 methods).
14. **B10** — `Object.fromEntries` / `Object.is`.

**Validation:** New test scripts for Map, Set, bind, regex, Date. Run full benchmark suite + baseline.

### Phase 5: Remaining Performance (A5) & Niche Syntax (B8)

Estimated ~250 LOC.

15. **A5** — Constructor shape pre-allocation.
16. **B8** — Labeled statements.

**Validation:** hash-map and richards benchmarks for A5. Labeled loop tests for B8.

---

## 5. Test Plan

### New Test Scripts

| Test File | Covers | Items |
|-----------|--------|-------|
| `test/js/v11_optional_chaining.js` | `?.` property, method, computed, chained | B1 |
| `test/js/v11_map_set.js` | `Map`/`Set` constructor, methods, iteration | B2 |
| `test/js/v11_function_bind.js` | `.bind()` with/without pre-args, `this` binding | B3 |
| `test/js/v11_regex_methods.js` | `.test()`, `.exec()`, `String.match()`, flags | B4 |
| `test/js/v11_error_subclasses.js` | `TypeError`, `RangeError`, `instanceof`, `.name` | B5 |
| `test/js/v11_date_methods.js` | All 12 Date instance methods, `toISOString` | B6 |
| `test/js/v11_sequence_label.js` | Sequence expressions, labeled break/continue | B7, B8 |
| `test/js/v11_nullish_assign.js` | `??=`, `\|\|=`, `&&=` | B9 |
| `test/js/v11_object_extras.js` | `Object.fromEntries`, `Object.is` | B10 |

Each `.js` test file must have a corresponding `.txt` expected-output file.

### Benchmark Regression Testing

After each phase:
1. Run all 633 baseline tests — must pass 100%.
2. Run JetStream suite — 12/13 must pass (13/13 after A2/A3).
3. Run AWFY + R7RS + Larceny + BENG + Kostya — 62/62 must pass.
4. Record per-benchmark timings (release build) for performance tracking.

### Performance Targets (Release Build)

| Benchmark | Current (ms) | Phase 1 Target | Phase 2 Target | Notes |
|-----------|-------------:|---------------:|---------------:|-------|
| navier-stokes | 1,320 | 1,200 | 400–600 | A2+A3 main impact |
| hash-map | 566 | 200–300 | 200–300 | A1 main impact |
| richards | 483 | 200–300 | 200–300 | A1+A5 main impact |
| deltablue | 48 | 20–30 | 20–30 | A1 main impact |
| raytrace3d | 709 | 400–500 | 200–400 | A1+A3 combined |
| nbody | 1,910 | 1,800 | 600–1000 | A3 main impact |
| splay | 48 | 30–40 | 20–30 | A1+A2 combined |

---

## 6. Updated Feature Coverage (Post-v11 Target)

### Language Syntax

| Feature | Pre-v11 | Post-v11 |
|---------|:-------:|:--------:|
| Optional chaining (`?.`) | ❌ | ✅ |
| Sequence expressions | ❌ | ✅ |
| Labeled statements | ❌ | ✅ |
| Nullish assignment (`??=`, `\|\|=`, `&&=`) | ❌ | ✅ |
| Generators / `yield` | ❌ | ❌ (future) |
| `async` / `await` | ❌ | ❌ (future) |
| `import` / `export` | ❌ | ❌ (future) |

### Built-in Objects

| Object | Pre-v11 Methods | Post-v11 Methods | Added |
|--------|:-----------:|:------------:|:-----:|
| String | 24 | 25 | `match` |
| Array | 29 | 29 | — |
| Object | 11 | 13 | `fromEntries`, `is` |
| Math | 27 | 27 | — |
| Number | 6 | 6 | — |
| Function | 2 (`call`, `apply`) | 3 | `bind` |
| Date | 2 (`now`, constructor) | 14 | 12 instance methods |
| Error | 1 (generic) | 5 | 4 subclasses |
| Regex | 0 instance methods | 2 | `test`, `exec` |
| Map | ❌ | ✅ (10 methods) | New |
| Set | ❌ | ✅ (7 methods) | New |
| JSON | 2 | 2 | — |

### Remaining Gaps (Post-v11, Future Work)

| Category | Items |
|----------|-------|
| **Async** | `Promise`, `async`/`await`, `setTimeout`/`setInterval` |
| **Generators** | `function*`, `yield`, `Symbol.iterator` |
| **Modules** | `import`/`export` (ES modules) |
| **Collections** | `WeakMap`, `WeakSet` |
| **Proxy/Reflect** | `Proxy`, `Reflect` |
| **Symbols** | Full Symbol API, well-known symbols |
| **Encoding** | `encodeURIComponent`, `decodeURIComponent`, `TextEncoder`/`TextDecoder` |
| **Timers** | `setTimeout`, `setInterval`, `clearTimeout` |

---

## 7. Summary

| Track | Items | LOC | Primary Outcome |
|-------|:-----:|:---:|-----------------|
| A (Performance) | 6 | ~830 | Geo. mean 8.8× → 3–5× vs Node.js |
| B (Language) | 10 | ~1,120 | General-purpose JS program support |
| **Total** | **16** | **~1,950** | Production-quality JS engine |

**Total estimated effort:** ~1,950 LOC across 5 phases.
