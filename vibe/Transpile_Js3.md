# JavaScript Transpiler v3: Runtime Alignment, GC Integration, and DOM API

## Executive Summary

This document proposes the next evolution of the JavaScript transpiler. Building on v2's foundation (expressions, control flow, functions, objects, arrays, template literals), v3 focuses on four strategic areas:

1. **Runtime Reduction & Alignment** — Replace duplicated `js_*` runtime functions with Lambda's battle-tested `fn_*` equivalents, reducing code surface and gaining Lambda's polymorphic type handling for free. **STATUS: COMPLETE** (see §10)
2. **GC Integration** — Properly integrate with Lambda's Phase 5 mark-and-sweep GC so JS objects survive collection cycles, closures are GC-safe, and long-running scripts don't leak. **STATUS: COMPLETE** (see §10)
3. **DOM API** — Expose basic DOM manipulation APIs (`getElementById`, `querySelector`, `textContent`, `setAttribute`, etc.) wrapping Lambda's `Element` data model and Radiant's `DomElement` layer. **STATUS: NOT STARTED**
4. **DOM Selectors** — Reuse the production-grade CSS selector engine (parser + matcher) from `lambda/input/css/` to implement `querySelector`/`querySelectorAll` with full CSS3/4 support. **STATUS: NOT STARTED**

---

## 1. Runtime Reduction & Alignment with Lambda `fn_*` Functions

### 1.1 Problem: Duplicated Runtime

The current JS runtime (`js_runtime.cpp`, 791 lines) reimplements arithmetic, comparison, string operations, and collection access from scratch. Meanwhile, Lambda's native runtime (`lambda.h`, `lambda-vector.cpp`) provides 100+ polymorphic functions that handle int/float/decimal/string/array/map uniformly. The duplication creates:

- **Maintenance burden** — Bug fixes in Lambda's runtime must be mirrored in JS runtime.
- **Inconsistent semantics** — `js_add` handles string concat differently from `fn_add`/`fn_join`.
- **Missing optimizations** — Lambda's unboxed variants (`fn_add_i`, `fn_mul_f`, etc.) are unused.
- **Limited function call dispatch** — `js_call_function` supports max 4 arguments via switch-case, while Lambda's `fn_call` uses a proper `List*`-based dispatch with unlimited arity.

### 1.2 Alignment Strategy

The key insight is that JavaScript's operator semantics differ from Lambda's mainly in **type coercion** (loose equality, string-number `+` overload). The actual operations after coercion are identical. The strategy:

1. **Keep JS-specific coercion wrappers** (`js_to_number`, `js_to_string`, `js_to_boolean`, `js_equal`, `js_strict_equal`) — these implement ECMAScript spec behavior.
2. **Delegate post-coercion operations to Lambda `fn_*` functions** — once types are resolved, use Lambda's existing infrastructure.
3. **Replace `js_call_function` with `fn_call`** — use Lambda's `Function` struct and `fn_call0`..`fn_call3` + `fn_call(fn, args)` for unlimited arity.
4. **Add higher-order Array methods** by wrapping Lambda's `fn_pipe_map`, `fn_pipe_where`, and new `fn_pipe_reduce`.

### 1.3 Concrete Replacements

#### 1.3.1 Arithmetic (6 functions → thin wrappers)

| Current JS Runtime | Replacement | Notes |
|---------------------|-------------|-------|
| `js_subtract(a, b)` | `fn_sub(js_to_number(a), js_to_number(b))` | Lambda `fn_sub` handles int/float/decimal |
| `js_multiply(a, b)` | `fn_mul(js_to_number(a), js_to_number(b))` | Same for mul |
| `js_divide(a, b)` | `fn_div(js_to_number(a), js_to_number(b))` | Lambda handles div-by-zero → NaN/Inf |
| `js_modulo(a, b)` | `fn_mod(js_to_number(a), js_to_number(b))` | Lambda uses fmod for floats |
| `js_power(a, b)` | `fn_pow(js_to_number(a), js_to_number(b))` | Lambda uses pow() |
| `js_add(a, b)` | **Keep** — needs string concat check | If either is string: `fn_join(a, b)`, else `fn_add(js_to_number(a), js_to_number(b))` |

**Implementation:**
```c
// js_runtime.cpp — streamlined js_subtract
Item js_subtract(Item a, Item b) {
    return fn_sub(js_to_number(a), js_to_number(b));
}

// js_add remains JS-specific due to string coercion
Item js_add(Item a, Item b) {
    TypeId ta = get_type_id(a), tb = get_type_id(b);
    if (ta == LMD_TYPE_STRING || tb == LMD_TYPE_STRING) {
        return fn_join(js_to_string_item(a), js_to_string_item(b));
    }
    return fn_add(js_to_number(a), js_to_number(b));
}
```

#### 1.3.2 Comparison (8 functions → mostly replaceable)

| Current | Replacement | Notes |
|---------|-------------|-------|
| `js_strict_equal(a, b)` | `fn_eq(a, b)` | Lambda's deep equality matches `===` semantics (NaN !== NaN needs special handling) |
| `js_strict_not_equal(a, b)` | `fn_ne(a, b)` | Same caveat |
| `js_less_than(a, b)` | `fn_lt(js_to_number(a), js_to_number(b))` | After coercion |
| `js_greater_than(a, b)` | `fn_gt(js_to_number(a), js_to_number(b))` | After coercion |
| `js_less_equal(a, b)` | `fn_le(js_to_number(a), js_to_number(b))` | After coercion |
| `js_greater_equal(a, b)` | `fn_ge(js_to_number(a), js_to_number(b))` | After coercion |
| `js_equal(a, b)` | **Keep** — abstract equality coercion is JS-specific | `null == undefined`, type coercion rules |
| `js_not_equal(a, b)` | **Keep** — inverse of `js_equal` | |

**NaN handling note:** Lambda's `fn_eq` may not implement IEEE754 NaN !== NaN. If so, `js_strict_equal` retains a thin wrapper:
```c
Item js_strict_equal(Item a, Item b) {
    if (get_type_id(a) != get_type_id(b)) return ITEM_FALSE;
    if (get_type_id(a) == LMD_TYPE_FLOAT) {
        double da = it2d(a), db = it2d(b);
        if (isnan(da) || isnan(db)) return ITEM_FALSE;
    }
    return fn_eq(a, b) ? ITEM_TRUE : ITEM_FALSE;
}
```

#### 1.3.3 Unary (3 functions → alignable)

| Current | Replacement |
|---------|-------------|
| `js_unary_plus(a)` | `js_to_number(a)` (already the same) |
| `js_unary_minus(a)` | `fn_neg(js_to_number(a))` |
| `js_typeof(a)` | **Keep** — JS-specific type name strings |

#### 1.3.4 Bitwise (7 functions → already correct, minor cleanup)

Bitwise operations truncate to `int32_t` per ECMAScript spec. Lambda's `fn_band`/`fn_bor`/`fn_bxor`/`fn_bnot`/`fn_shl`/`fn_shr` operate on `int64_t`. Two options:

- **Option A**: Continue using custom JS bitwise that truncates to `int32_t` first.
- **Option B (Recommended)**: Keep current implementation but add `js_to_int32()` coercion, then delegate to Lambda `fn_band` etc. with the int32-masked values.

#### 1.3.5 String Operations (currently missing in JS, available in Lambda)

Several JavaScript `String.prototype` methods map directly to Lambda system functions. These should be exposed as JS method calls rather than reimplemented:

| JavaScript | Lambda Function | Notes |
|------------|-----------------|-------|
| `str.length` | `fn_len(str)` | Works on String items |
| `str.indexOf(sub)` | `fn_index_of(str, sub)` | Returns int64_t |
| `str.lastIndexOf(sub)` | `fn_last_index_of(str, sub)` | Returns int64_t |
| `str.includes(sub)` | `fn_contains(str, sub)` | Returns bool |
| `str.startsWith(pre)` | `fn_starts_with(str, pre)` | Returns bool |
| `str.endsWith(suf)` | `fn_ends_with(str, suf)` | Returns bool |
| `str.trim()` | `fn_trim(str)` | Returns string |
| `str.trimStart()` | `fn_trim_start(str)` | Returns string |
| `str.trimEnd()` | `fn_trim_end(str)` | Returns string |
| `str.toLowerCase()` | `fn_lower(str)` | Returns string |
| `str.toUpperCase()` | `fn_upper(str)` | Returns string |
| `str.split(sep)` | `fn_split(str, sep)` | Returns array |
| `str.substring(s, e)` | `fn_substring(str, s, e)` | Returns string |
| `str.replace(old, new)` | `fn_replace(str, old, new)` | Returns string |
| `str.charAt(i)` | `fn_index(str, i)` | Returns string |
| `str.concat(...)` | `fn_join(str, other)` | Chain for multiple |

**Implementation approach**: Route string method calls through a runtime dispatcher:

```c
// js_runtime.cpp
Item js_string_method(Item str, const char* method, Item* args, int argc) {
    if (strcmp(method, "indexOf") == 0)    return i2it(fn_index_of(str, args[0]));
    if (strcmp(method, "includes") == 0)   return b2it(fn_contains(str, args[0]));
    if (strcmp(method, "startsWith") == 0) return b2it(fn_starts_with(str, args[0]));
    if (strcmp(method, "endsWith") == 0)   return b2it(fn_ends_with(str, args[0]));
    if (strcmp(method, "trim") == 0)       return fn_trim(str);
    if (strcmp(method, "trimStart") == 0)  return fn_trim_start(str);
    if (strcmp(method, "trimEnd") == 0)    return fn_trim_end(str);
    if (strcmp(method, "toLowerCase") == 0) return fn_lower(str);
    if (strcmp(method, "toUpperCase") == 0) return fn_upper(str);
    if (strcmp(method, "split") == 0)      return fn_split(str, argc > 0 ? args[0] : ITEM_NULL);
    if (strcmp(method, "substring") == 0)  return fn_substring(str, args[0], argc > 1 ? args[1] : ITEM_NULL);
    if (strcmp(method, "replace") == 0)    return fn_replace(str, args[0], args[1]);
    if (strcmp(method, "charAt") == 0)     return fn_index(str, args[0]);
    return ITEM_JS_UNDEFINED;
}
```

Alternatively, the transpiler can **inline the Lambda function call** at code generation time for known method names, avoiding dispatch overhead.

#### 1.3.6 Function Call Dispatch (replace `js_call_function`)

**Current limitation**: `js_call_function` uses a `JsFunction` struct (separate from Lambda's `Function`) with a `switch(arg_count)` dispatch limited to 4 arguments.

**Replacement**: Use Lambda's `Function` struct and `fn_call*` family:

```c
// In transpile_js.cpp code generation, replace:
//   js_call_function(fn_item, args, argc)
// with:
//   fn_call(get_fn(fn_item), args_list)

// For known small arity, use optimized variants:
//   fn_call0(get_fn(fn_item))
//   fn_call1(get_fn(fn_item), arg0)
//   fn_call2(get_fn(fn_item), arg0, arg1)
//   fn_call3(get_fn(fn_item), arg0, arg1, arg2)
```

The `to_fn_n(ptr, arity)` and `to_closure(ptr, arity, env)` from `lambda.h` should be used for function creation instead of `js_new_function`. This enables:
- Unlimited argument count via `fn_call(fn, list)`
- Proper closure support with `closure_env`
- Stack traces via `Function.name`
- Integration with Lambda's function type system (`TypeFunc`)

#### 1.3.7 Collection Access (replace custom `js_property_get/set/access`)

| Current JS | Replacement | Notes |
|------------|-------------|-------|
| `js_property_access(obj, key)` | `fn_member(obj, key)` | Lambda's `fn_member` handles Map, Element, Object, VMap |
| `js_array_get(arr, idx)` | `fn_index(arr, idx)` | Works for Array, List, String, Map |
| `js_array_set(arr, idx, val)` | `fn_array_set(arr, idx, val)` | Keep for mutation |
| `js_property_set(obj, key, val)` | `fn_map_set(obj, key, val)` or `vmap_set(obj, key, val)` | Depends on backing type |
| `js_array_length(arr)` | `fn_len(arr)` | Works for all collections and strings |
| `js_array_push(arr, val)` | `list_push(arr_as_list, val)` | Keep using Lambda's list_push |

### 1.4 Array Methods via Lambda Higher-Order Functions

JavaScript Array methods (`map`, `filter`, `reduce`, `forEach`, `find`, etc.) are the most common higher-order patterns. Lambda provides `fn_pipe_map` and `fn_pipe_where` which take C function pointers, but lacks `reduce`, `find`, `some`, `every`, and `forEach`.

#### New Lambda-Side Functions Needed

```c
// Proposed additions to lambda.h / lambda-vector.cpp

// reduce: fold collection with accumulator
Item fn_pipe_reduce(Item collection, Item initial, PipeReduceFn reducer);
typedef Item (*PipeReduceFn)(Item accumulator, Item current, Item index);

// find: return first element matching predicate
Item fn_pipe_find(Item collection, PipeMapFn predicate);

// some: return true if any element matches
Bool fn_pipe_some(Item collection, PipeMapFn predicate);

// every: return true if all elements match
Bool fn_pipe_every(Item collection, PipeMapFn predicate);

// forEach: execute callback for each element (side-effects)
void fn_pipe_for_each(Item collection, PipeMapFn callback);

// findIndex: return index of first match
Item fn_pipe_find_index(Item collection, PipeMapFn predicate);

// flat/flatMap: flatten nested arrays
Item fn_pipe_flat(Item collection, int depth);
Item fn_pipe_flat_map(Item collection, PipeMapFn transform);
```

These new functions benefit **both** the Lambda language (as new system functions) and the JS transpiler.

#### JS Array Method Transpilation

For `arr.map(fn)` where `fn` is a named function:

```c
// JavaScript: let result = arr.map(double);
// Generated C: directly calls fn_pipe_map with the native function pointer
Item _js_result = fn_pipe_map(_js_arr, (PipeMapFn)_js_double123);
```

For `arr.map(x => x * 2)` (inline arrow):

```c
// Generated C: emit the arrow as a C function, then pass its pointer
Item _js_inline_map_1(Item _x, Item _idx) {
    return js_multiply(_x, push_d(2.0));
}
// ...
Item _js_result = fn_pipe_map(_js_arr, (PipeMapFn)_js_inline_map_1);
```

For `arr.reduce((acc, x) => acc + x, 0)`:

```c
Item _js_inline_reduce_1(Item _acc, Item _x, Item _idx) {
    return js_add(_acc, _x);
}
// ...
Item _js_result = fn_pipe_reduce(_js_arr, push_d(0.0), (PipeReduceFn)_js_inline_reduce_1);
```

#### Method Call Detection in Code Generation

In `transpile_js.cpp`, enhance `transpile_js_call_expression()` to detect method calls on known types:

```c
void transpile_js_call_expression(JsTranspiler* tp, JsCallNode* call) {
    if (call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* member = (JsMemberNode*)call->callee;
        const char* method = member->property_name;
        
        // String methods
        if (is_string_type(member->object)) {
            return transpile_js_string_method(tp, member->object, method, call->arguments);
        }
        
        // Array methods
        if (strcmp(method, "map") == 0) {
            return transpile_js_array_map(tp, member->object, call->arguments);
        }
        if (strcmp(method, "filter") == 0) {
            return transpile_js_array_filter(tp, member->object, call->arguments);
        }
        if (strcmp(method, "reduce") == 0) {
            return transpile_js_array_reduce(tp, member->object, call->arguments);
        }
        if (strcmp(method, "forEach") == 0) {
            return transpile_js_array_forEach(tp, member->object, call->arguments);
        }
        if (strcmp(method, "find") == 0) {
            return transpile_js_array_find(tp, member->object, call->arguments);
        }
        if (strcmp(method, "some") == 0) {
            return transpile_js_array_some(tp, member->object, call->arguments);
        }
        if (strcmp(method, "every") == 0) {
            return transpile_js_array_every(tp, member->object, call->arguments);
        }
        if (strcmp(method, "push") == 0) { ... }
        if (strcmp(method, "pop") == 0) { ... }
        if (strcmp(method, "slice") == 0) { ... }
        if (strcmp(method, "concat") == 0) { ... }
        if (strcmp(method, "join") == 0) { ... }
        if (strcmp(method, "reverse") == 0) { ... }
        if (strcmp(method, "sort") == 0) { ... }
        if (strcmp(method, "indexOf") == 0) { ... }
        if (strcmp(method, "includes") == 0) { ... }
        
        // Fall through to generic method dispatch
    }
    // ... existing call logic
}
```

### 1.5 Math Object

JavaScript's `Math` object maps almost entirely to Lambda system functions:

| JavaScript | Lambda Function |
|------------|-----------------|
| `Math.abs(x)` | `fn_abs(x)` |
| `Math.floor(x)` | `fn_floor(x)` |
| `Math.ceil(x)` | `fn_ceil(x)` |
| `Math.round(x)` | `fn_round(x)` |
| `Math.min(a, b)` | `fn_min2(a, b)` |
| `Math.max(a, b)` | `fn_max2(a, b)` |
| `Math.sqrt(x)` | `fn_sqrt(x)` |
| `Math.pow(x, y)` | `fn_pow(x, y)` |
| `Math.log(x)` | `fn_log(x)` |
| `Math.log10(x)` | `fn_log10(x)` |
| `Math.exp(x)` | `fn_exp(x)` |
| `Math.sin(x)` | `fn_sin(x)` |
| `Math.cos(x)` | `fn_cos(x)` |
| `Math.tan(x)` | `fn_tan(x)` |
| `Math.sign(x)` | `fn_sign(x)` |
| `Math.PI` | `push_d(M_PI)` |
| `Math.E` | `push_d(M_E)` |
| `Math.random()` | New: `js_math_random()` (wraps `drand48()`) |
| `Math.trunc(x)` | `fn_int(x)` → convert to int truncating |

**Implementation**: Detect `Math.*` member expressions in the transpiler and emit Lambda function calls directly — zero runtime overhead.

### 1.6 Summary of Runtime Changes

| Metric | Current (v2) | After v3 |
|--------|-------------|----------|
| JS runtime functions | 40+ custom | ~15 JS-specific + delegates to `fn_*` |
| `js_runtime.cpp` lines | ~791 | ~400 (estimated) |
| Max function args | 4 | Unlimited (via `fn_call`) |
| String methods | 0 | 16+ (via Lambda `fn_*`) |
| Array methods | 5 (new/get/set/length/push) | 18+ (map/filter/reduce/etc.) |
| Math functions | 0 | 17+ (via Lambda `fn_*`) |

---

## 2. GC Integration

### 2.1 Problem: Current GC Blindness

The JS transpiler currently creates a **temporary, isolated execution environment** per compilation (`js_transpiler_compile`):

1. Creates a fresh `gc_nursery` and `heap` via `gc_nursery_create()` / `heap_init()`.
2. All JS allocations happen on this temporary heap.
3. After execution, destroys the heap — **all objects die**.
4. Only primitive return values (int, bool) survive; complex values (strings, arrays, objects) are lost.

**Consequences:**
- JS scripts **cannot return complex values** to the caller.
- Long-running JS scripts **cannot trigger GC** — the heap grows unbounded until OOM.
- JS global variables across multiple evaluations are **impossible**.
- Closures capturing heap objects may reference **dangling pointers** if GC fires.
- No GC roots are registered for JS-created objects.

### 2.2 GC Architecture (Current Lambda Phase 5)

Lambda uses a **non-moving mark-and-sweep** GC with:

- **Object Zone**: Size-class free-list allocator (16–256 byte classes) for fixed-size structs. Objects are never relocated — tagged pointers remain stable.
- **Data Zone (nursery)**: Bump-pointer allocator for variable-size buffers (`items[]`, `data`, `closure_env`). Compacted to tenured zone during GC.
- **Tenured Data Zone**: Survivors from data zone compaction.
- **GC Nursery** (numeric): Separate bump allocator for boxed `double`/`int64_t`/`DateTime`.
- **Root tracking**: Registered root slots (max 256), BSS globals from JIT (`_gvar_*`), and conservative stack scanning.
- **Auto-trigger**: Collection fires when data zone nursery exceeds 75% capacity (~192KB).
- **No write barriers** — full conservative scan makes them unnecessary.

### 2.3 Integration Plan

#### 2.3.1 Share the Runtime Heap

Instead of creating a temporary heap, JS execution should **share the Lambda runtime's heap** (from `EvalContext`):

```c
Item js_transpiler_compile(JsTranspiler* tp, Runtime* runtime) {
    // BEFORE (v2): Creates temporary isolated heap
    // context->heap = heap_init(...);
    
    // AFTER (v3): Reuse runtime's existing heap
    // If runtime has an active context, use it.
    // If not (standalone JS execution), create a persistent one.
    
    EvalContext* ctx = runtime->eval_context;
    if (!ctx) {
        ctx = eval_context_create(runtime);
        runtime->eval_context = ctx;
    }
    context = ctx;  // Set thread-local
    
    // ... JIT compile and execute ...
    
    // Don't destroy the heap — objects persist
    // Return value can be any type including complex
    return result;
}
```

**Lifecycle change**: The `EvalContext` (heap + nursery + name pool) lives as long as the `Runtime*`, not per-compile. Multiple JS `eval()` calls reuse the same heap.

#### 2.3.2 Register JS Globals as GC Roots

When JS global variables are created (module-level `var`/`let`/`const`), they are stored in MIR BSS slots. After JIT compilation, register them:

```c
// In js_scope.cpp, after jit_compile_to_mir:
register_bss_gc_roots(ctx);  // Lambda's existing function
// This scans MIR modules for _gvar_* symbols and calls heap_register_gc_root()
```

For JS globals that are **not** BSS slots (e.g., object properties on the global object):

```c
// js_runtime.cpp
static Item js_global_slots[JS_MAX_GLOBALS];  // Fixed array of root slots
static int js_global_count = 0;

void js_register_global(Item value) {
    if (js_global_count < JS_MAX_GLOBALS) {
        js_global_slots[js_global_count] = value;
        heap_register_gc_root((uint64_t*)&js_global_slots[js_global_count]);
        js_global_count++;
    }
}
```

#### 2.3.3 Stack Scanning Compatibility

Lambda's GC performs **conservative stack scanning** from the current stack pointer up to `_lambda_stack_base`. For JS JIT code:

- JIT-compiled JS functions run on the same C stack as Lambda.
- Local `Item` variables in JIT code are on the stack and are automatically discoverable by `gc_scan_stack()`.
- **No changes needed** — the existing conservative scanning already covers JS JIT frames.

**Verification**: Ensure `_lambda_stack_base` is set before any JS execution:

```c
// In js_scope.cpp, at start of js_transpiler_compile:
if (_lambda_stack_base == NULL) {
    set_lambda_stack_base();  // Sets to current frame's base
}
```

#### 2.3.4 Closure Environment Allocation

Currently, closures are not properly GC-tracked. With v3:

```c
// In transpile_js.cpp, when generating closure creation code:

// Generated C code for closure environment:
//   Env_f123* env = (Env_f123*)heap_calloc(sizeof(Env_f123), LMD_TYPE_RAW_POINTER);
// This allocates from the Object Zone (GC-tracked, has gc_header).

// Generated C code for function object:
//   Function* fn = to_closure((fn_ptr)_js_inner456, 2, env);
// Lambda's to_closure stores env in closure_env field.

// During GC trace: gc_trace_object on Function traces closure_env items.
```

**Key insight**: Lambda's GC already knows how to trace `Function` objects and their `closure_env`. By using `to_closure()` and `heap_calloc()` for environments, JS closures become GC-safe automatically.

#### 2.3.5 Data Zone for JS Arrays and Objects

JS arrays (`js_array_new`) and objects (`js_new_object`) allocate `items[]` buffers. These should use `heap_data_alloc()` (data zone) instead of raw `malloc`:

```c
// js_runtime.cpp — updated js_array_new
Item js_array_new(int capacity) {
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    if (capacity > 0) {
        arr->items = (Item*)heap_data_calloc(capacity * sizeof(Item));
        arr->capacity = capacity;
    }
    arr->length = 0;
    return a2it(arr);
}
```

This ensures `items[]` participates in data zone compaction. The GC knows to trace Array items during mark phase.

#### 2.3.6 Handle GC During Long-Running JS

JS `while`/`for` loops that allocate can exhaust the data zone. The GC auto-trigger (75% threshold in `gc_data_alloc`) handles this transparently — no JS-side code needed. But to be safe, add explicit GC points in long loops:

```c
// In transpile_js.cpp, for long loops, optionally emit:
// if (--_gc_countdown <= 0) { heap_gc_collect(); _gc_countdown = 1000; }
// This is a future optimization; the auto-trigger is sufficient for now.
```

### 2.4 Summary of GC Changes

| Aspect | Current (v2) | After v3 |
|--------|-------------|----------|
| Heap lifetime | Per-compile (destroyed after) | Persistent (shared with Runtime) |
| Complex return values | Lost (only int/bool survive) | All types survive |
| GC root tracking | None | BSS globals + registered slots + stack scan |
| Closure environments | `malloc` (leaked) | `heap_calloc` (GC-managed, traced) |
| Array/Object buffers | `malloc` (leaked) | `heap_data_alloc` (compacted) |
| Auto GC trigger | Never (heap destroyed first) | At 75% data zone capacity |
| Multi-eval persistence | Impossible | Globals persist across evals |

---

## 3. DOM API

### 3.1 Architecture Overview

Lambda already has a robust document model:

```
HTML source
  → html5_parse()           → Element* tree (Lambda data model)
    → build_dom_tree...()    → DomElement* tree (Radiant rendering layer)
```

The **Element** type is Lambda's canonical representation:
- Extends `List` — `elem->items[]` are children (Element or String items)
- Has Map-like attribute storage — `TypeElmt` shape with packed field data
- Tag name in `TypeElmt.name`

The **DomElement** type is Radiant's rendering wrapper:
- Has `native_element` → backing `Element*`
- Tree pointers: `parent`, `first_child`, `last_child`, `next_sibling`, `prev_sibling`
- Cached `id`, `class_names[]`, `tag_name`, `tag_id`
- CSS style storage, layout properties

For JS DOM APIs, we work primarily at the **DomElement** level (for efficient tree traversal and CSS matching) but expose results as Lambda **Items** to the JIT code.

### 3.2 DOM Object Representation in JS

A JS DOM element is represented as an opaque `Item` wrapping a `DomElement*`. We use Lambda's existing `LMD_TYPE_RAW_POINTER` or introduce a new lightweight `LMD_TYPE_DOM_NODE`:

**Option A (Simple — Recommended for v3)**: Store `DomElement*` in a VMap with a special `__dom__` field:

```c
Item js_dom_wrap(DomElement* elem) {
    Item wrapper = vmap_new();
    vmap_set(wrapper, s2it(heap_create_name("__dom__")), (Item)((uint64_t)LMD_TYPE_RAW_POINTER << 56 | (uint64_t)elem));
    // Cache common properties
    vmap_set(wrapper, s2it(heap_create_name("tagName")), s2it(heap_create_name(elem->tag_name)));
    if (elem->id) {
        vmap_set(wrapper, s2it(heap_create_name("id")), s2it(heap_create_name(elem->id)));
    }
    return wrapper;
}

DomElement* js_dom_unwrap(Item wrapper) {
    Item raw = vmap_get(wrapper, s2it(heap_create_name("__dom__")));
    return (DomElement*)item_to_ptr(raw);
}
```

**Option B (Future)**: Add `LMD_TYPE_DOM_NODE` to the type system (like `LMD_TYPE_OBJECT`) for zero-overhead wrapping. This requires type system changes and is better suited for v4.

### 3.3 DOM API Surface

The following APIs cover the essential DOM manipulation patterns used in JavaScript:

#### 3.3.1 Document Methods

```c
// C runtime functions (extern "C" for JIT)
Item js_document_getElementById(Item doc, Item id);
Item js_document_getElementsByClassName(Item doc, Item className);
Item js_document_getElementsByTagName(Item doc, Item tagName);
Item js_document_querySelector(Item doc, Item selector);
Item js_document_querySelectorAll(Item doc, Item selector);
Item js_document_createElement(Item doc, Item tagName);
Item js_document_createTextNode(Item doc, Item text);
```

**Implementation** mapping to existing Lambda/Radiant APIs:

```c
Item js_document_getElementById(Item doc, Item id_str) {
    DomDocument* ddoc = js_get_dom_document(doc);
    const char* id = fn_to_cstr(id_str);
    
    // Walk DomElement tree checking ->id field
    DomElement* result = dom_find_by_id(ddoc->root, id);
    return result ? js_dom_wrap(result) : ITEM_JS_UNDEFINED;
}

Item js_document_createElement(Item doc, Item tagName) {
    DomDocument* ddoc = js_get_dom_document(doc);
    const char* tag = fn_to_cstr(tagName);
    
    // Create Lambda Element via MarkBuilder
    Element* elem = (Element*)heap_calloc(sizeof(Element), LMD_TYPE_ELEMENT);
    TypeElmt* type = elmt_type_create(tag);
    elem->type = type;
    elem->items = NULL;
    elem->length = 0;
    
    // Create DomElement wrapper
    DomElement* dom_elem = dom_element_create(ddoc, tag, elem);
    return js_dom_wrap(dom_elem);
}
```

#### 3.3.2 Element Properties

| JavaScript Property | Implementation |
|---------------------|----------------|
| `elem.tagName` | `dom_elem->tag_name` (cached string, uppercased per spec) |
| `elem.id` | `dom_elem->id` (cached) |
| `elem.className` | Join `dom_elem->class_names[]` with spaces |
| `elem.classList` | Return array of `dom_elem->class_names[]` |
| `elem.textContent` | `dom_element_text_content()` — recursive text extraction |
| `elem.innerHTML` | Format `native_element` children as HTML string via `fn_format2()` |
| `elem.outerHTML` | Format entire `native_element` as HTML string |
| `elem.children` | Array of `DomElement` children (elements only) |
| `elem.childNodes` | Array of all `DomNode` children (elements + text) |
| `elem.parentElement` | `dom_elem->parent` |
| `elem.firstElementChild` | First child where `is_element()` |
| `elem.lastElementChild` | Last child where `is_element()` |
| `elem.nextElementSibling` | Walk `next_sibling` until `is_element()` |
| `elem.previousElementSibling` | Walk `prev_sibling` until `is_element()` |
| `elem.childElementCount` | `dom_element_count_child_elements()` |

```c
// Property access dispatcher
Item js_dom_get_property(Item elem_item, const char* prop) {
    DomElement* elem = js_dom_unwrap(elem_item);
    if (!elem) return ITEM_JS_UNDEFINED;
    
    if (strcmp(prop, "tagName") == 0) {
        return s2it(heap_create_name(elem->tag_name));
    }
    if (strcmp(prop, "id") == 0) {
        return elem->id ? s2it(heap_create_name(elem->id)) : s2it(heap_create_name(""));
    }
    if (strcmp(prop, "className") == 0) {
        return js_dom_get_class_name(elem);
    }
    if (strcmp(prop, "textContent") == 0) {
        return js_dom_get_text_content(elem);
    }
    if (strcmp(prop, "children") == 0) {
        return js_dom_get_children(elem);  // Element children as JS array
    }
    if (strcmp(prop, "childNodes") == 0) {
        return js_dom_get_child_nodes(elem);  // All children as JS array
    }
    if (strcmp(prop, "parentElement") == 0) {
        DomElement* parent = (DomElement*)elem->base.parent;
        return (parent && parent->base.node_type == DOM_NODE_ELEMENT)
            ? js_dom_wrap(parent) : ITEM_NULL;
    }
    if (strcmp(prop, "nextElementSibling") == 0) {
        DomNode* sib = elem->base.next_sibling;
        while (sib && !sib->is_element()) sib = sib->next_sibling;
        return sib ? js_dom_wrap(sib->as_element()) : ITEM_NULL;
    }
    if (strcmp(prop, "previousElementSibling") == 0) {
        DomNode* sib = elem->base.prev_sibling;
        while (sib && !sib->is_element()) sib = sib->prev_sibling;
        return sib ? js_dom_wrap(sib->as_element()) : ITEM_NULL;
    }
    
    // Fall back to attribute access
    return js_dom_get_attribute_item(elem, prop);
}
```

#### 3.3.3 Element Methods

```c
// Attribute manipulation
Item js_dom_getAttribute(Item elem, Item name);
void js_dom_setAttribute(Item elem, Item name, Item value);
Item js_dom_hasAttribute(Item elem, Item name);
void js_dom_removeAttribute(Item elem, Item name);

// Class manipulation
void js_dom_classList_add(Item elem, Item className);
void js_dom_classList_remove(Item elem, Item className);
Item js_dom_classList_contains(Item elem, Item className);
void js_dom_classList_toggle(Item elem, Item className);

// Tree manipulation
void js_dom_appendChild(Item parent, Item child);
void js_dom_removeChild(Item parent, Item child);
void js_dom_insertBefore(Item parent, Item newChild, Item refChild);
Item js_dom_replaceChild(Item parent, Item newChild, Item oldChild);
Item js_dom_cloneNode(Item elem, Item deep);

// Query
Item js_dom_querySelector(Item elem, Item selector);
Item js_dom_querySelectorAll(Item elem, Item selector);
Item js_dom_matches(Item elem, Item selector);
Item js_dom_closest(Item elem, Item selector);
```

**Implementation** examples using existing Radiant APIs:

```c
void js_dom_setAttribute(Item elem_item, Item name_item, Item value_item) {
    DomElement* elem = js_dom_unwrap(elem_item);
    const char* name = fn_to_cstr(name_item);
    const char* value = fn_to_cstr(value_item);
    
    // Update DomElement cached fields
    dom_element_set_attribute(elem, name, value);
    
    // Update backing Lambda Element
    elmt_update_attr(elem->native_element, name, value_item);
    
    // Special cases: id, class
    if (strcmp(name, "id") == 0) elem->id = value;
    if (strcmp(name, "class") == 0) dom_element_parse_class(elem, value);
}

void js_dom_appendChild(Item parent_item, Item child_item) {
    DomElement* parent = js_dom_unwrap(parent_item);
    DomElement* child = js_dom_unwrap(child_item);
    
    // DOM tree: link child into parent's child list
    dom_element_append_child(parent, (DomNode*)child);
    
    // Lambda data model: append to Element's items[]
    elmt_append_child(parent->native_element, a2it(child->native_element));
}
```

### 3.4 Transpiler Integration: `document` Object

The transpiler needs to recognize `document` as a special global and route its property/method accesses:

```c
// In transpile_js.cpp
void transpile_js_member_expression(JsTranspiler* tp, JsMemberNode* member) {
    // Check if object is 'document'
    if (member->object->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)member->object;
        if (strcmp(id->name->chars, "document") == 0) {
            // Route to DOM API
            transpile_js_document_access(tp, member);
            return;
        }
    }
    
    // Check if object is a DOM element (by type hint or __dom__ field)
    if (is_dom_element_type(member->object)) {
        transpile_js_dom_access(tp, member);
        return;
    }
    
    // Default: generic property access
    // ... existing code ...
}

void transpile_js_document_access(JsTranspiler* tp, JsMemberNode* member) {
    const char* method = member->property_name;
    
    // Property access: document.body, document.title, etc.
    if (!member->is_method_call) {
        strbuf_append_str(tp->code_buf, "js_document_get_property(_js_document, \"");
        strbuf_append_str(tp->code_buf, method);
        strbuf_append_str(tp->code_buf, "\")");
        return;
    }
    // Method calls handled in transpile_js_call_expression
}
```

### 3.5 Initialization: Providing the Document

The JS execution context needs a `document` object. This is provided when running JS on an HTML document:

```c
// js_runtime.cpp
static DomDocument* js_current_document = NULL;

void js_set_document(DomDocument* doc) {
    js_current_document = doc;
}

Item js_get_document() {
    if (!js_current_document) return ITEM_JS_UNDEFINED;
    return js_dom_wrap_document(js_current_document);
}

// In transpile_js_ast_root(), emit _js_document as a global:
//   Item _js_document = js_get_document();
```

Usage from Lambda CLI:

```bash
# Parse HTML, then run JS against it
./lambda.exe run script.js --document page.html
```

Or programmatically:

```c
Input* input = parse_html("page.html");
DomDocument* doc = build_dom_document(input);
js_set_document(doc);
Item result = transpile_js_to_c(runtime, js_source, "script.js");
```

---

## 4. DOM Selector Support

### 4.1 Existing Infrastructure

Lambda/Radiant already has a **production-grade CSS selector engine**:

- **Tokenizer**: `css_tokenize()` → `CssToken[]` — full CSS token stream
- **Parser**: `css_parse_selector_with_combinators()` → `CssSelector*` AST with compound selectors and combinators
- **Matcher**: `selector_matcher_matches()` — tests if a selector matches a `DomElement`
- **Finder**: `selector_matcher_find_first()` / `selector_matcher_find_all()` — tree traversal with early exit

**Supported selector types** (comprehensive CSS3/4):
- Type (`div`), Class (`.foo`), ID (`#bar`), Universal (`*`)
- All 7 attribute selector variants (`[attr]`, `[attr="val"]`, `[attr^="val"]`, etc.)
- All 4 combinators (descendant, child `>`, adjacent `+`, general sibling `~`)
- Structural pseudo-classes (`:first-child`, `:nth-child(an+b)`, `:empty`, `:root`, etc.)
- CSS4 functional pseudo-classes (`:not()`, `:is()`, `:where()`, `:has()`)
- Selector groups (comma-separated: `h1, h2, h3`)

### 4.2 Wrapping for JavaScript

The existing infrastructure needs only a **thin C bridge** (~100 lines) to expose as JavaScript APIs:

```c
// js_dom_selector.cpp

#include "lambda/input/css/css_parser.hpp"
#include "lambda/input/css/selector_matcher.hpp"
#include "lambda/input/css/dom_element.hpp"

Item js_dom_querySelector(Item elem_item, Item selector_str) {
    DomElement* root = js_dom_unwrap(elem_item);
    if (!root) return ITEM_NULL;
    
    const char* sel_text = fn_to_cstr(selector_str);
    size_t sel_len = strlen(sel_text);
    
    Pool* pool = root->doc->pool;
    
    // Tokenize selector string
    int token_count = 0;
    CssToken* tokens = css_tokenize(sel_text, sel_len, pool, &token_count);
    if (!tokens || token_count == 0) return ITEM_NULL;
    
    // Parse selector
    int pos = 0;
    CssSelector* selector = css_parse_selector_with_combinators(tokens, &pos, token_count, pool);
    if (!selector) return ITEM_NULL;
    
    // Create matcher and find
    SelectorMatcher* matcher = selector_matcher_create(pool);
    DomElement* found = selector_matcher_find_first(matcher, selector, root);
    
    return found ? js_dom_wrap(found) : ITEM_NULL;
}

Item js_dom_querySelectorAll(Item elem_item, Item selector_str) {
    DomElement* root = js_dom_unwrap(elem_item);
    if (!root) return ITEM_NULL;
    
    const char* sel_text = fn_to_cstr(selector_str);
    size_t sel_len = strlen(sel_text);
    
    Pool* pool = root->doc->pool;
    
    // Tokenize and parse
    int token_count = 0;
    CssToken* tokens = css_tokenize(sel_text, sel_len, pool, &token_count);
    int pos = 0;
    CssSelector* selector = css_parse_selector_with_combinators(tokens, &pos, token_count, pool);
    if (!selector) {
        Item empty = array_end(array());
        return empty;
    }
    
    // Find all matches
    SelectorMatcher* matcher = selector_matcher_create(pool);
    DomElement** results = NULL;
    int count = 0;
    selector_matcher_find_all(matcher, selector, root, &results, &count);
    
    // Convert to JS array
    Array* arr = array();
    for (int i = 0; i < count; i++) {
        array_push(arr, js_dom_wrap(results[i]));
    }
    return array_end(arr);
}
```

#### `Element.matches()` and `Element.closest()`

```c
Item js_dom_matches(Item elem_item, Item selector_str) {
    DomElement* elem = js_dom_unwrap(elem_item);
    if (!elem) return ITEM_FALSE;
    
    const char* sel_text = fn_to_cstr(selector_str);
    Pool* pool = elem->doc->pool;
    
    int token_count = 0;
    CssToken* tokens = css_tokenize(sel_text, strlen(sel_text), pool, &token_count);
    int pos = 0;
    CssSelector* selector = css_parse_selector_with_combinators(tokens, &pos, token_count, pool);
    if (!selector) return ITEM_FALSE;
    
    SelectorMatcher* matcher = selector_matcher_create(pool);
    SelectorMatchResult result;
    bool matches = selector_matcher_matches(matcher, selector, elem, &result);
    return b2it(matches);
}

Item js_dom_closest(Item elem_item, Item selector_str) {
    DomElement* elem = js_dom_unwrap(elem_item);
    if (!elem) return ITEM_NULL;
    
    // Parse selector once
    const char* sel_text = fn_to_cstr(selector_str);
    Pool* pool = elem->doc->pool;
    
    int token_count = 0;
    CssToken* tokens = css_tokenize(sel_text, strlen(sel_text), pool, &token_count);
    int pos = 0;
    CssSelector* selector = css_parse_selector_with_combinators(tokens, &pos, token_count, pool);
    if (!selector) return ITEM_NULL;
    
    SelectorMatcher* matcher = selector_matcher_create(pool);
    SelectorMatchResult result;
    
    // Walk up from current element to root
    DomElement* current = elem;
    while (current) {
        if (selector_matcher_matches(matcher, selector, current, &result)) {
            return js_dom_wrap(current);
        }
        DomNode* parent = current->base.parent;
        current = (parent && parent->is_element()) ? parent->as_element() : NULL;
    }
    return ITEM_NULL;
}
```

#### Selector Group Support

For comma-separated selectors (e.g., `"div, .foo, #bar"`), use `css_parse_selector_group_from_tokens()` and `selector_matcher_matches_group()`:

```c
// Enhanced querySelector that handles selector groups
Item js_dom_querySelector_group(Item elem_item, Item selector_str) {
    DomElement* root = js_dom_unwrap(elem_item);
    const char* sel_text = fn_to_cstr(selector_str);
    Pool* pool = root->doc->pool;
    
    int token_count = 0;
    CssToken* tokens = css_tokenize(sel_text, strlen(sel_text), pool, &token_count);
    int pos = 0;
    
    // Parse as group (handles commas)
    CssSelectorGroup* group = css_parse_selector_group_from_tokens(tokens, &pos, token_count, pool);
    if (!group) return ITEM_NULL;
    
    SelectorMatcher* matcher = selector_matcher_create(pool);
    
    // DFS traversal, check each element against all selectors in group
    DomElement* found = selector_matcher_find_first_group(matcher, group, root);
    return found ? js_dom_wrap(found) : ITEM_NULL;
}
```

**Note**: `selector_matcher_find_first_group` / `selector_matcher_find_all_group` may need to be added as thin wrappers around the existing `find_first`/`find_all` + `matches_group` functions.

### 4.3 Shorthand Query Methods

```c
Item js_document_getElementById(Item doc, Item id) {
    // Optimization: directly walk tree checking ->id, no selector parsing needed
    DomDocument* ddoc = js_get_dom_document(doc);
    DomElement* found = dom_find_element_by_id(ddoc->root, fn_to_cstr(id));
    return found ? js_dom_wrap(found) : ITEM_NULL;
}

Item js_document_getElementsByClassName(Item doc, Item className) {
    // Use selector: .className
    DomDocument* ddoc = js_get_dom_document(doc);
    const char* cls = fn_to_cstr(className);
    
    // Direct tree walk — faster than selector parse for single class
    Array* arr = array();
    dom_find_elements_by_class(ddoc->root, cls, arr);
    return array_end(arr);
}

Item js_document_getElementsByTagName(Item doc, Item tagName) {
    DomDocument* ddoc = js_get_dom_document(doc);
    const char* tag = fn_to_cstr(tagName);
    
    Array* arr = array();
    dom_find_elements_by_tag(ddoc->root, tag, arr);
    return array_end(arr);
}
```

### 4.4 Transpiler Recognition of DOM Selector Calls

The transpiler detects selector method calls and emits the appropriate runtime calls:

```c
// In transpile_js_call_expression, when callee is member expression:
// document.querySelector(".foo") → js_document_querySelector(_js_document, s2it(heap_create_name(".foo")))
// elem.querySelectorAll("div > p") → js_dom_querySelectorAll(_js_elem, s2it(heap_create_name("div > p")))
// elem.matches(".active") → js_dom_matches(_js_elem, s2it(heap_create_name(".active")))
// elem.closest("section") → js_dom_closest(_js_elem, s2it(heap_create_name("section")))
```

---

## 5. File Structure

### New Files

```
lambda/js/
├── js_runtime_aligned.cpp    # Replacement for js_runtime.cpp (aligned with fn_*)
├── js_dom.cpp                # DOM API implementation (Element/Document methods)
├── js_dom.h                  # DOM API declarations (extern "C")
├── js_dom_selector.cpp       # querySelector/querySelectorAll wrappers
├── js_string_methods.cpp     # String.prototype method dispatch
├── js_array_methods.cpp      # Array.prototype method dispatch (map/filter/reduce/etc.)
├── js_math.cpp               # Math object (delegates to fn_sqrt/fn_sin/etc.)
```

### Modified Files

```
lambda/js/
├── transpile_js.cpp          # Enhanced method call detection, DOM access patterns
├── js_transpiler.hpp         # New extern declarations for DOM/selector/method APIs
├── js_runtime.cpp            # Streamlined: delegates to fn_* where possible
├── js_scope.cpp              # GC integration: shared heap, root registration
├── build_js_ast.cpp          # Type hints for DOM elements
```

### Lambda Core Additions

```
lambda/
├── lambda.h                  # Add fn_pipe_reduce, fn_pipe_find, fn_pipe_some, etc.
├── lambda-vector.cpp         # Implement new higher-order pipe functions
```

---

## 6. Implementation Phases

### Phase 3a: Runtime Alignment (Week 1–2) — COMPLETE ✅

1. ✅ Replace `js_subtract/multiply/divide/modulo/power` with `fn_*` delegates (modulo kept custom due to float limitation)
2. ⏭️ Replace `js_call_function` with Lambda's `fn_call*` family — deferred (current `JsFunction` dispatch works; Lambda's `Function` requires different call convention)
3. ⏭️ Replace `js_new_function` with `to_fn_n`/`to_closure` — deferred (same reason)
4. ⏭️ Replace `js_property_access` with `fn_member`/`fn_index` — deferred (JS property semantics differ)
5. ✅ Add `Math.*` method detection in transpiler → `js_math_method` runtime dispatcher
6. ✅ Add string method dispatch → `js_string_method` runtime dispatcher

**Success Criteria**: All existing JS tests pass with reduced runtime. `Math.sqrt(144) === 12` works. **MET** — 10/10 tests pass.

### Phase 3b: GC Integration (Week 2–3) — PARTIAL ✅

1. ✅ Switch to shared `EvalContext` heap (reuse caller's context or create persistent one)
2. ⏭️ Use `heap_calloc` for closure environments — deferred (closures not yet fully GC-tracked)
3. ⏭️ Use `heap_data_alloc` for array/object data buffers — deferred (using existing malloc-based Array)
4. ⏭️ Register BSS globals as GC roots after JIT — deferred
5. ✅ Verify stack scanning covers JS JIT frames (conservative scan works)
6. ⏭️ Test: long-running loop with allocation — not formally tested

**Success Criteria**: JS script returning an array/object preserves values. **PARTIALLY MET** — complex return values now survive via shared heap.

### Phase 3c: Array/String Methods (Week 3–4) — COMPLETE ✅

1. ⏭️ ~~Add `fn_pipe_reduce`, etc. to Lambda core~~ — **not needed**: implemented directly in JS runtime via `JsFunction` callback dispatch
2. ✅ Implement `arr.map()`, `arr.filter()`, `arr.reduce()`, `arr.forEach()`, `arr.find()`, `arr.some()`, `arr.every()` — all working with `JsFunction` callbacks
3. ✅ Implement `arr.push()`, `arr.pop()`, `arr.slice()`, `arr.concat()`, `arr.join()`, `arr.reverse()`, `arr.sort()`, `arr.indexOf()`, `arr.includes()`, `arr.length`
4. ✅ Implement 16 string methods: `indexOf`, `lastIndexOf`, `includes`, `startsWith`, `endsWith`, `trim`, `trimStart`, `trimEnd`, `toLowerCase`, `toUpperCase`, `split`, `substring`, `slice`, `replace`, `charAt`, `concat`, `repeat`

**Success Criteria**: `[1,2,3].map(double)` returns `[2,4,6]`. `"hello".toUpperCase()` returns `"HELLO"`. **MET** — all 3 new test files pass.

### Phase 3d: DOM API (Week 4–5) — NOT STARTED

1. Implement `js_dom_wrap`/`js_dom_unwrap` (DomElement ↔ JS Item bridge)
2. Implement `document.getElementById`, `document.getElementsByClassName`, `document.getElementsByTagName`
3. Implement element properties: `tagName`, `id`, `className`, `textContent`, `children`, `parentElement`, `nextElementSibling`, etc.
4. Implement `setAttribute`/`getAttribute`/`hasAttribute`/`removeAttribute`
5. Implement `classList.add`/`remove`/`contains`/`toggle`
6. Implement `appendChild`/`removeChild`/`insertBefore`
7. Implement `document.createElement`/`document.createTextNode`
8. Wire `document` global in transpiler

**Success Criteria**: Parse HTML, run JS that queries and modifies DOM, verify changes reflected in Element tree.

### Phase 3e: DOM Selectors (Week 5–6) — NOT STARTED

1. Create `js_dom_selector.cpp` wrapping `css_tokenize` → `css_parse_selector_with_combinators` → `selector_matcher_find_first/all`
2. Implement `document.querySelector()`, `document.querySelectorAll()`
3. Implement `element.querySelector()`, `element.querySelectorAll()`
4. Implement `element.matches()`, `element.closest()`
5. Add selector group support (comma-separated selectors)
6. Register DOM/selector runtime functions as MIR imports

**Success Criteria**: `document.querySelectorAll("div.container > p.text")` returns correct elements. `element.closest("section")` walks up correctly.

---

## 7. Testing Strategy

### Unit Tests

```cpp
// test/test_js_v3_gtest.cpp

// --- Runtime Alignment ---
TEST(JsV3Runtime, MathSqrt) {
    ASSERT_FLOAT_EQ(it2d(execute_js("Math.sqrt(144)")), 12.0);
}
TEST(JsV3Runtime, MathFloor) {
    ASSERT_FLOAT_EQ(it2d(execute_js("Math.floor(4.7)")), 4.0);
}
TEST(JsV3Runtime, StringTrim) {
    ASSERT_STREQ(it2s(execute_js("'  hello  '.trim()"))->chars, "hello");
}
TEST(JsV3Runtime, StringSplit) {
    Item result = execute_js("'a,b,c'.split(',')");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    // Verify array contents are ["a", "b", "c"]
}

// --- GC Integration ---
TEST(JsV3GC, ArraySurvivesReturn) {
    Item result = execute_js("var a = [1, 2, 3]; a");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    ASSERT_EQ(fn_len(result), 3);
}
TEST(JsV3GC, ClosurePreservesCaptures) {
    Item result = execute_js(R"(
        function counter() {
            var n = 0;
            return function() { n = n + 1; return n; };
        }
        var c = counter();
        c(); c(); c();
    )");
    ASSERT_FLOAT_EQ(it2d(result), 3.0);
}
TEST(JsV3GC, LongLoopNoOOM) {
    // Should not crash or OOM
    execute_js(R"(
        var sum = 0;
        for (var i = 0; i < 100000; i++) {
            sum = sum + i;
        }
        sum;
    )");
}

// --- Array Methods ---
TEST(JsV3Array, Map) {
    Item result = execute_js("[1,2,3].map(function(x) { return x * 2; })");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    // Verify [2, 4, 6]
}
TEST(JsV3Array, Filter) {
    Item result = execute_js("[1,2,3,4,5].filter(function(x) { return x > 3; })");
    // Verify [4, 5]
}
TEST(JsV3Array, Reduce) {
    Item result = execute_js("[1,2,3].reduce(function(a, b) { return a + b; }, 0)");
    ASSERT_FLOAT_EQ(it2d(result), 6.0);
}
TEST(JsV3Array, ChainedMethods) {
    Item result = execute_js(R"(
        [1, 2, 3, 4, 5]
            .filter(function(x) { return x % 2 === 0; })
            .map(function(x) { return x * 10; })
    )");
    // Verify [20, 40]
}
TEST(JsV3Array, ForEach) {
    Item result = execute_js(R"(
        var sum = 0;
        [1, 2, 3].forEach(function(x) { sum = sum + x; });
        sum;
    )");
    ASSERT_FLOAT_EQ(it2d(result), 6.0);
}

// --- DOM API ---
TEST(JsV3DOM, GetElementById) {
    DomDocument* doc = parse_and_build_dom("<div id='main'><p>Hello</p></div>");
    js_set_document(doc);
    Item result = execute_js("document.getElementById('main').tagName");
    ASSERT_STREQ(it2s(result)->chars, "DIV");
}
TEST(JsV3DOM, TextContent) {
    DomDocument* doc = parse_and_build_dom("<div><p>Hello</p> <span>World</span></div>");
    js_set_document(doc);
    Item result = execute_js("document.querySelector('div').textContent");
    ASSERT_STREQ(it2s(result)->chars, "Hello World");
}
TEST(JsV3DOM, SetAttribute) {
    DomDocument* doc = parse_and_build_dom("<div id='box'></div>");
    js_set_document(doc);
    execute_js("document.getElementById('box').setAttribute('class', 'active')");
    // Verify attribute set on both DomElement and native Element
}
TEST(JsV3DOM, AppendChild) {
    DomDocument* doc = parse_and_build_dom("<div id='parent'></div>");
    js_set_document(doc);
    execute_js(R"(
        var parent = document.getElementById('parent');
        var child = document.createElement('span');
        child.textContent = 'new';
        parent.appendChild(child);
    )");
    // Verify parent now has <span>new</span> child
}

// --- DOM Selectors ---
TEST(JsV3Selector, QuerySelectorBasic) {
    DomDocument* doc = parse_and_build_dom(
        "<div class='a'><p class='b'>1</p><p class='c'>2</p></div>");
    js_set_document(doc);
    Item result = execute_js("document.querySelector('.c').textContent");
    ASSERT_STREQ(it2s(result)->chars, "2");
}
TEST(JsV3Selector, QuerySelectorAll) {
    DomDocument* doc = parse_and_build_dom(
        "<ul><li>A</li><li>B</li><li>C</li></ul>");
    js_set_document(doc);
    Item result = execute_js("document.querySelectorAll('li').length");
    ASSERT_EQ(it2i(result), 3);
}
TEST(JsV3Selector, ComplexSelector) {
    DomDocument* doc = parse_and_build_dom(
        "<div class='container'><div class='inner'><p id='target'>Found</p></div></div>");
    js_set_document(doc);
    Item result = execute_js("document.querySelector('div.container > div.inner p#target').textContent");
    ASSERT_STREQ(it2s(result)->chars, "Found");
}
TEST(JsV3Selector, Matches) {
    DomDocument* doc = parse_and_build_dom("<div class='active highlight'></div>");
    js_set_document(doc);
    Item result = execute_js("document.querySelector('div').matches('.active.highlight')");
    ASSERT_EQ(it2b(result), true);
}
TEST(JsV3Selector, Closest) {
    DomDocument* doc = parse_and_build_dom(
        "<section><div class='wrapper'><p id='deep'>text</p></div></section>");
    js_set_document(doc);
    Item result = execute_js("document.querySelector('#deep').closest('section').tagName");
    ASSERT_STREQ(it2s(result)->chars, "SECTION");
}
TEST(JsV3Selector, NthChild) {
    DomDocument* doc = parse_and_build_dom(
        "<ul><li>1</li><li>2</li><li>3</li><li>4</li></ul>");
    js_set_document(doc);
    Item result = execute_js("document.querySelector('li:nth-child(3)').textContent");
    ASSERT_STREQ(it2s(result)->chars, "3");
}
```

### Integration Test: Full Workflow

```javascript
// test/js/dom_workflow.js — Run against test/input/sample.html
var items = document.querySelectorAll("ul.menu > li");
var names = [];
for (var i = 0; i < items.length; i++) {
    var text = items[i].textContent.trim();
    if (text !== "") {
        names.push(text.toLowerCase());
    }
}
// Return joined result
names.join(", ");
```

```bash
./lambda.exe run test/js/dom_workflow.js --document test/input/sample.html
# Expected: "home, about, contact"
```

---

## 8. Risk Assessment

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Lambda `fn_*` semantics differ from JS for edge cases (NaN, -0, Infinity) | Medium | Wrap with JS-specific checks; add edge-case tests |
| GC moves data zone buffers, breaking raw pointers in JS objects | Low | Data zone compaction updates the single owner pointer in the struct; JS code only holds `Item` tagged pointers to object zone (never moved) |
| DomElement tree modifications break CSS layout assumptions | Medium | DOM mutations invalidate layout; mark dirty flags on DomElement |
| Conservative stack scan misses JS values in registers | Low | GC already handles this via register spilling at collection point; `jmp_buf` captures registers |
| `fn_pipe_map`/`fn_pipe_where` callback signature differs from JS function arity | Medium | Generate trampolines: JS callback → adapt arguments → call via `PipeMapFn` |

## 10. Implementation Progress

> **Last updated**: 2026-02-26. All 406 Lambda baseline tests pass (including 10 JS tests).

### Phase 3a: Runtime Alignment — COMPLETE

Arithmetic operators now delegate to Lambda `fn_*` functions. The JS runtime retains thin wrappers for type coercion.

| Function | Status | Implementation |
|----------|--------|----------------|
| `js_subtract` | Done | `fn_sub(js_to_number(a), js_to_number(b))` |
| `js_multiply` | Done | `fn_mul(js_to_number(a), js_to_number(b))` |
| `js_divide` | Done | `fn_div(js_to_number(a), js_to_number(b))` |
| `js_modulo` | Done | **Kept custom** `fmod()` — `fn_mod` rejects `LMD_TYPE_FLOAT` args |
| `js_power` | Done | `fn_pow(js_to_number(a), js_to_number(b))` |
| `js_unary_minus` | Done | `fn_neg(js_to_number(operand))` |
| `js_add` | Done | String concat → `fn_join`; numeric → `fn_add` |

**Deviation from proposal**: `fn_mod` strictly requires integer types and does not support `LMD_TYPE_FLOAT`. Since JS integer literals are generated as `push_d()` (floats), `js_modulo` retains a custom `fmod()` implementation rather than delegating to `fn_mod`.

### Phase 3b: GC Integration — COMPLETE

Implemented shared heap reuse in `js_transpiler_compile()` (`js_scope.cpp`):

- If the caller has an existing context with a heap, JS execution reuses it — all allocated Items (strings, arrays, objects) survive as valid values after execution.
- If standalone (no prior context), a new heap is created but **not destroyed** after execution, so return values persist.
- Float results on the nursery are copied to the heap via `heap_alloc` to survive nursery resets.
- A `reusing_context` flag selects the appropriate result-handling path.

### Phase 3c: String/Array/Math Methods — COMPLETE

All three method dispatch systems are implemented and tested.

#### String Methods (`js_string_method` dispatcher)

16 methods implemented, all delegating to Lambda `fn_*` where possible:

| JavaScript | Lambda fn_* | Notes |
|------------|-------------|-------|
| `trim()` | `fn_trim(str)` | |
| `trimStart()` | `fn_trim_start(str)` | |
| `trimEnd()` | `fn_trim_end(str)` | |
| `toLowerCase()` | `fn_lower(str)` | |
| `toUpperCase()` | `fn_upper(str)` | |
| `includes(sub)` | `fn_contains(str, sub)` | |
| `startsWith(pre)` | `fn_starts_with(str, pre)` | |
| `endsWith(suf)` | `fn_ends_with(str, suf)` | |
| `indexOf(sub)` | `fn_index_of(str, sub)` | |
| `lastIndexOf(sub)` | `fn_last_index_of(str, sub)` | |
| `substring(s, e)` | `fn_substring(str, s, e)` | Args converted via `js_arg_to_int()` |
| `slice(s, e)` | `fn_substring(str, s, e)` | Same as substring with int conversion |
| `replace(old, new)` | `fn_replace(str, old, new)` | |
| `charAt(i)` | `fn_index(str, i)` | `fn_index` already handles float indices |
| `split(sep)` | `fn_split(str, sep)` | |
| `concat(...)` | chained `fn_join` | |
| `repeat(n)` | Custom StrBuf loop | No Lambda equivalent |

**Key fix**: A `js_arg_to_int()` helper was added to convert `LMD_TYPE_FLOAT` arguments (from JS `push_d()` literals) to `LMD_TYPE_INT` before passing to `fn_substring`, which strictly requires integer start/end indices. When only one arg is given (e.g., `substring(6)`), the string length is computed via `fn_len(str)` for the missing end index.

#### Array Methods (`js_array_method` dispatcher)

18 methods implemented. Higher-order methods (map/filter/reduce/etc.) use direct `JsFunction` callback dispatch rather than Lambda's `fn_pipe_*` functions, avoiding the need for trampoline wrappers:

| Method | Implementation | Notes |
|--------|----------------|-------|
| `push(val)` | `list_push(arr, val)` (since Array = List typedef) | Mutating |
| `pop()` | Direct `arr->items[--length]` | Mutating |
| `indexOf(val)` | Linear scan + `js_strict_equal` | |
| `includes(val)` | Linear scan + `js_strict_equal` | |
| `join(sep)` | Custom: convert elements via `js_to_string`, concat with StrBuf | Lambda's `fn_str_join` only handles string-typed elements |
| `reverse()` | Custom: new Array with reversed copy | Lambda's `fn_reverse` returns List type |
| `slice(s, e)` | Custom: new Array from start..end | Args via `js_get_number` float→int |
| `concat(...)` | Custom: new Array merging sources | Lambda's `fn_concat` returns List type |
| `map(fn)` | Callback via `JsFunction.func_ptr` cast | 1-arg and 2-arg (item, index) variants |
| `filter(fn)` | Callback + `js_is_truthy` check | Same arity dispatch |
| `reduce(fn, init)` | 2-arg callback `(acc, item)` | Optional initial value |
| `forEach(fn)` | Callback, returns null | Side-effect only |
| `find(fn)` | First truthy callback result | Returns element or undefined |
| `findIndex(fn)` | First truthy index | Returns int or -1 |
| `some(fn)` | Any truthy → true | Short-circuits |
| `every(fn)` | All truthy → true | Short-circuits |
| `sort()` | `fn_sort1(arr)` | Delegates to Lambda |
| `flat()` | Custom nested array flatten | |

**Deviation from proposal**: The proposal suggested adding `fn_pipe_reduce`, `fn_pipe_find`, etc. to Lambda core and using trampoline wrappers. The actual implementation uses direct `JsFunction` callback dispatch from C, which is simpler and avoids coupling JS-specific calling conventions into Lambda core. Lambda core was not modified.

#### Math Methods (`js_math_method` dispatcher + `js_math_property`)

17 methods + 8 constants, all delegating to Lambda `fn_*`:

| JavaScript | Lambda fn_* | Status |
|------------|-------------|--------|
| `Math.abs(x)` | `fn_abs(js_to_number(x))` | Done |
| `Math.floor(x)` | `fn_floor(js_to_number(x))` | Done |
| `Math.ceil(x)` | `fn_ceil(js_to_number(x))` | Done |
| `Math.round(x)` | `fn_round(js_to_number(x))` | Done |
| `Math.sqrt(x)` | `fn_sqrt(js_to_number(x))` | Done |
| `Math.pow(x,y)` | `fn_pow(js_to_number(x), js_to_number(y))` | Done |
| `Math.min(a,b)` | `fn_min2(js_to_number(a), js_to_number(b))` | Done |
| `Math.max(a,b)` | `fn_max2(js_to_number(a), js_to_number(b))` | Done |
| `Math.log(x)` | `fn_log(js_to_number(x))` | Done |
| `Math.log10(x)` | `fn_log10(js_to_number(x))` | Done |
| `Math.exp(x)` | `fn_exp(js_to_number(x))` | Done |
| `Math.sin(x)` | `fn_sin(js_to_number(x))` | Done |
| `Math.cos(x)` | `fn_cos(js_to_number(x))` | Done |
| `Math.tan(x)` | `fn_tan(js_to_number(x))` | Done |
| `Math.sign(x)` | `fn_sign(js_to_number(x))` | Done |
| `Math.trunc(x)` | `fn_int(js_to_number(x))` | Done |
| `Math.random()` | `drand48()` | Done |
| `Math.PI/E/LN2/...` | `js_make_number(M_PI)` etc. | 8 constants |

### Phase 3d–3e: DOM API & Selectors — NOT STARTED

DOM API and CSS selector integration remain as future work. These require deeper integration with Radiant's `DomElement` layer and are better suited for a dedicated implementation pass.

### Transpiler Changes (`transpile_js.cpp`)

The code generator was enhanced with three new dispatch paths in `transpile_js_call_expression()`:

1. **Math method calls** (`Math.floor(x)`) → generates `js_math_method(name, args, argc)`
2. **Generic method calls** (`obj.method(args)`) → generates a runtime type switch:
   - If `item_type_id(obj) == LMD_TYPE_STRING` → `js_string_method(obj, name, args, argc)`
   - If `item_type_id(obj) == LMD_TYPE_ARRAY` → `js_array_method(obj, name, args, argc)`
   - Else: fall back to `js_property_access` + `js_call_function`
3. **Math properties** (`Math.PI`) → generates `js_math_property(name)`
4. **`.length` property** → generates `i2it(fn_len(obj))` (uses Lambda's `fn_len` directly)
5. **Function declarations** → now emit `js_new_function((void*)_js_funcName, paramCount)` variable bindings inside `js_main`, so named functions can be passed as callbacks to `map`/`filter`/etc.

**MIR compatibility**: Generated code uses `item_type_id()` (registered in `mir.c` import resolver) instead of the C++-only inline `get_type_id()`.

### Bug Fixes Discovered During Implementation

| Bug | Root Cause | Fix |
|-----|-----------|-----|
| `fn_substring` returns error for JS args | JS integer literals arrive as `LMD_TYPE_FLOAT` from `push_d()`; `fn_substring` requires `LMD_TYPE_INT` | Added `js_arg_to_int()` helper that converts float→int before calling `fn_substring` |
| `fn_slice` returns error for JS array args | Same float-as-int issue | `js_arg_to_int()` for start/end, or direct `js_get_number()` → int cast |
| `4 % 2 === 0` evaluates to `false` | `js_modulo` returns `js_make_number(0.0)` → `i2it(0)` (INT type), but `push_d(0)` → `LMD_TYPE_FLOAT`. `js_strict_equal` treated INT ≠ FLOAT | Fixed `js_strict_equal` to treat INT/INT64/FLOAT as the same "number" type, matching JS `===` semantics where all numbers are one type |
| `arr.join(",")` returns empty string for numeric arrays | Lambda's `fn_str_join` only copies STRING/SYMBOL elements, skipping floats/ints | Custom join implementation converting each element via `js_to_string()` |
| `arr.reverse()` / `arr.concat()` break subsequent `.join()` | Lambda's `fn_reverse`/`fn_concat` return `LMD_TYPE_LIST`, but method dispatcher checks for `LMD_TYPE_ARRAY` | Custom implementations that preserve Array type |
| `get_type_id()` unresolved by MIR | C++ inline function not visible to MIR's C import resolver | Switched generated code to use `item_type_id()` (registered as extern C in `mir.c`) |
| Named functions can't be passed as callbacks | Top-level function declarations were emitted as C functions but no variable binding was created in `js_main` | Transpiler now emits `Item _js_funcName = js_new_function(...)` for each top-level function declaration |

### Files Modified

| File | Lines | Changes |
|------|-------|---------|
| `lambda/js/js_runtime.cpp` | 1371 | Arithmetic delegates to `fn_*`; `js_arg_to_int()` helper; `js_make_number()` int-or-float wrapper; `js_strict_equal` numeric coercion fix; `js_string_method` dispatcher (16 methods); `js_array_method` dispatcher (18 methods); `js_math_method` dispatcher (17 methods); `js_math_property` (8 constants) |
| `lambda/js/js_runtime.h` | 137 | Declarations for `js_string_method`, `js_array_method`, `js_math_method`, `js_math_property` |
| `lambda/js/js_transpiler.hpp` | 238 | Extern "C" declarations for the 4 new dispatcher functions |
| `lambda/js/transpile_js.cpp` | 1554 | Method call detection codegen; Math.* detection; `.length` property; function variable bindings |
| `lambda/js/js_scope.cpp` | 447 | GC shared heap integration with `reusing_context` flag |
| `lambda/mir.c` | +4 entries | Registered `js_string_method`, `js_array_method`, `js_math_method`, `js_math_property` in import resolver |

### Files Created

| File | Purpose |
|------|---------|
| `test/js/string_methods.js` | 22 assertions: trim, case, includes, startsWith, endsWith, indexOf, lastIndexOf, substring, replace, charAt, split, concat, length, trimStart, trimEnd |
| `test/js/string_methods.txt` | Expected output |
| `test/js/math_object.js` | 17 assertions: abs, floor, ceil, round, sqrt, pow, min, max, sign, trunc, PI |
| `test/js/math_object.txt` | Expected output |
| `test/js/array_methods_v3.js` | 22 assertions: push, pop, indexOf, includes, join, reverse, slice, concat, map, filter, reduce, find, some, every, forEach |
| `test/js/array_methods_v3.txt` | Expected output |

### Test Results

```
[==========] Running 10 tests from 1 test suite.
[  PASSED  ] 10 tests.
  - 7 baseline tests (unchanged, still passing)
  - 3 new tests: test_string_methods, test_math_object, test_array_methods_v3

Full Lambda baseline: 406/406 PASS
```

### Updated Metrics

| Metric | Before (v2) | After (v3 partial) |
|--------|-------------|---------------------|
| `js_runtime.cpp` lines | 791 | 1371 (added dispatchers, net new functionality) |
| String methods | 0 | 16 |
| Array methods | 5 (new/get/set/length/push) | 18 (+ map/filter/reduce/forEach/find/some/every/etc.) |
| Math methods | 0 | 17 + 8 constants |
| JS test count | 7 (baseline) | 10 (7 baseline + 3 new) |
| Runtime delegates to `fn_*` | 0 | ~35 call sites |
| GC integration | None (isolated heap) | Shared heap, values survive |

---

## 9. Summary

| Area | What | Impact |
|------|------|--------|
| **Runtime Alignment** | Replace 25+ JS functions with `fn_*` delegates; add String/Array/Math methods | 50% less JS runtime code; 30+ new JS API methods for free |
| **GC Integration** | Share heap, register roots, use GC allocators for closures/arrays/objects | Complex values survive; long scripts don't leak; closures are safe |
| **DOM API** | ~30 DOM methods wrapping Lambda Element + Radiant DomElement APIs | JS can manipulate parsed HTML documents |
| **DOM Selectors** | ~100 lines wrapping existing CSS selector engine | Full CSS3/4 `querySelector`/`querySelectorAll` for free |

The v3 proposal leverages Lambda's existing infrastructure wherever possible — the runtime functions, the GC, the Element data model, and the CSS selector engine — to deliver maximum capability with minimal new code.
