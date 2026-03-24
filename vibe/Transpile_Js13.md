# JS Engine Transpiler — Optimizations from Lambda Restructuring

## Overview

This document identifies optimizations from the Lambda transpiler restructuring series
([Restructure1–5](Lambda_Transpile_Restructure.md)) that can be meaningfully applied to the
JS MIR transpiler (`transpile_js_mir.cpp`). It focuses on the optimizations that are either
not yet implemented in the JS engine, or only partially implemented.

### Current State of the JS Engine (Already Implemented)

Many Lambda restructuring concepts are already present in the JS engine. These are **not** targets for this proposal:

| Lambda Proposal | JS Engine Status |
|-----------------|-----------------|
| Dual-version `_n`/`_b` (Restructure1 §1, Restructure4) | ✅ Done — `has_native_version`, `native_func_item` |
| Parameter type inference (Restructure3 body-usage) | ✅ Done — `jm_infer_param_types()` |
| Return type tracking | ✅ Done — `fc->return_type`, `jm_infer_return_type_walk()` |
| O(1) import & function lookup | ✅ Done — `import_cache` hashmap |
| Inline arithmetic for typed variables | ✅ Done — native INT/FLOAT MIR ops |
| Math.xxx() native calls (Phase 5) | ✅ Done — `jm_transpile_math_native()` |
| Typed array native access (P9) | ✅ Done |
| Inline array element get (A2/A3) | ✅ Done |
| Constructor prop shape scanning (A5 scan) | ✅ Done — `jm_scan_ctor_props()` |
| Variable float widening pre-scan | ✅ Done — `widen_to_float` hashmap |
| Tail-call optimization (TCO) | ✅ Done — `is_tco_eligible` |

The proposals below address gaps that remain.

---

## Proposal 1: Return Type → Variable Type Propagation

### Problem

When `let x = foo()` where `foo` has a native version with a known return type
(e.g., `LMD_TYPE_FLOAT`), the variable `x` currently receives `LMD_TYPE_ANY` from
`jm_set_var`. This prevents all subsequent uses of `x` from using the native path,
forcing unnecessary boxing/unboxing throughout the variable's lifetime.

`jm_get_effective_type()` already returns `fc->return_type` for call expressions (line 1416),
but this type is not propagated to the variable entry at the declaration site.

### Example

```js
function computeArea(r) { return 3.14159 * r * r; }   // return_type = FLOAT
let area = computeArea(radius);   // area stored as ANY, not FLOAT
let scaled = area * 2.0;          // area unboxed via it2d() — unnecessary
```

### Fix

In `jm_transpile_statement` for `JS_AST_NODE_VARIABLE_DECLARATOR`, after calling
`jm_transpile_expression()` on the initializer:

```cpp
// After: existing code that emits the initializer
TypeId init_type = jm_get_effective_type(mt, decl->init);
if (init_type != LMD_TYPE_ANY && jm_is_native_type(init_type)) {
    // Initializer is a native-returning call/expression — store variable as native
    jm_set_var(mt, var_name, result_reg, type_to_mir(init_type), init_type);
} else {
    jm_set_var(mt, var_name, result_reg);  // existing path
}
```

The same logic applies to `JS_AST_NODE_ASSIGNMENT_EXPRESSION` when the RHS is a
typed call expression.

### Expected Impact

- Chains like `let x = nativeFunc(); let y = x * 2.0;` eliminate the box→unbox round-trip for `x`
- Particularly effective for loops where a typed function result is used across many iterations

---

## Proposal 2: Bump-Pointer Nursery for JS Object Allocation

### Problem

Every JS object creation (`{}` literal or `new Foo()`) calls `js_new_object()` →
`heap_alloc()` → GC slab allocator. The Lambda splay benchmark (Restructure5) showed
a **31% speedup** just by replacing the slab free-list pop with a JIT-inlined bump-pointer
check for common allocation sizes. The same bottleneck exists in the JS engine for
object-intensive programs.

Current allocation path:
```
js_new_object()  →  heap_alloc(size, type_id)  →  gc_alloc_slab()  →  slab pop + memset + header init
```

### Proposed Inline Bump-Pointer Allocation

The Lambda runtime already added a **4MB bump-pointer nursery** as phase Restructure5's opt #10.
The nursery pointer and limit are exposed via the `Context` struct accessible as `_lambda_rt`.

For the JS engine, add an `emit_inline_alloc` helper in the MIR transpiler that:

1. Loads `nursery_ptr` from `_lambda_rt->nursery_next`
2. Checks `nursery_ptr + alloc_size <= _lambda_rt->nursery_end`
3. On success (fast path): advance `nursery_next`, write GC header inline → no function call
4. On failure (slow path): call `heap_alloc()` normally (function call, rare)

```cpp
// Emit JIT-inlined bump-pointer allocation for a fixed-size object
static MIR_reg_t jm_emit_inline_alloc(JsMirTranspiler* mt, int alloc_size, TypeId type_id) {
    // Load nursery_next from _lambda_rt
    MIR_reg_t rt_ptr = ...;   // pointer to _lambda_rt
    MIR_reg_t cur = jm_new_reg(mt, "nurs_cur", MIR_T_I64);
    MIR_reg_t end = jm_new_reg(mt, "nurs_end", MIR_T_I64);
    MIR_reg_t obj  = jm_new_reg(mt, "obj", MIR_T_I64);

    // Load nursery_next and nursery_end
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, ...));  // cur = rt->nursery_next

    // Check if bump fits
    MIR_label_t slow_label = jm_new_label(mt);
    MIR_label_t done_label = jm_new_label(mt);
    // if (cur + alloc_size > end) goto slow
    ...
    // Fast path: advance pointer, write type_id header
    // rt->nursery_next = cur + alloc_size
    // *((TypeId*)cur) = type_id
    MIR_reg_t result = cur;
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, done_label)));

    // Slow path: regular heap_alloc call
    jm_emit_label(mt, slow_label);
    result = jm_call_2(mt, "heap_alloc", MIR_T_I64, ...);

    jm_emit_label(mt, done_label);
    return result;
}
```

### Where to Apply

- `jm_transpile_object_expression` — for `{}` literals with ≤ N known properties
- Constructor dispatch (`new Foo(...)`) — when constructor shape is known (A5)
- `js_array_new` calls for small known-size arrays

### Expected Impact

The splay benchmark saw 31% improvement from this alone in Lambda. JS benchmarks with
many short-lived object allocations (e.g., tree traversal, map/filter chains producing
intermediate arrays) would benefit significantly.

---

## Proposal 3: Direct Property Stores in Constructor (Complete A5)

### Problem

`jm_scan_ctor_props()` already collects `this.x = y` assignment patterns in constructors
(the A5 scan). But the property stores still go through `js_property_set()` — a runtime
call that performs a hashmap lookup on every write. The Lambda splay benchmark showed a
**50% runtime reduction** by emitting direct byte-offset MIR stores for known-shape maps.

The A5 scan populates `fc->ctor_prop_ptrs[]` and `fc->ctor_prop_lens[]` but this
information is not yet used during code generation.

### Proposed: Emit MIR Stores at Compile-Time Offsets

When `jm_transpile_statement` processes an assignment `this.prop = value` inside a
constructor function (detected via `mt->current_fc->ctor_prop_count > 0`):

1. Look up the property name in `current_fc->ctor_prop_ptrs[]` to get the slot index `i`
2. Compute the byte offset: `header_size + i * 8` (for pointer-sized fields)
3. Emit a direct `MIR_MOV` to `obj_ptr + offset` instead of calling `js_property_set`

```cpp
// Instead of: jm_call_3(mt, "js_property_set", ...)
// Emit: *(obj + header_size + slot * 8) = value

int slot = jm_ctor_prop_slot(mt->current_fc, prop_name, prop_len);
if (slot >= 0 && !mem->computed) {
    int offset = JS_OBJECT_HEADER_SIZE + slot * sizeof(uint64_t);
    MIR_reg_t boxed_val = jm_transpile_box_item(mt, asgn->right);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_mem_op(mt->ctx, MIR_T_I64, offset, obj_reg, 0, 1),
        MIR_new_reg_op(mt->ctx, boxed_val)));
    return;
}
// Fall through to js_property_set for other cases
```

This requires `new Foo(...)` to pre-allocate an object with exactly `ctor_prop_count` 
property slots (Proposal 2 handles allocation; this handles field stores).

### Expected Impact

For class-heavy code, each constructor call eliminates `ctor_prop_count` runtime
`js_property_set` calls. For a class with 4 properties, that's 4 hashmap operations → 4 direct stores per `new`.

---

## Proposal 4: Direct Property Access for Typed Class Instances

### Problem

Lambda's Phase 3 direct field access optimization (the triangl and splay benchmarks)
converts `node.key` from `fn_member(node, "key")` (hashmap lookup) to a direct
byte-offset load when the variable is typed. The JS engine has no equivalent for
class instances: every `obj.prop` goes through `js_property_get()`.

For code like:
```js
class Node { constructor(val, left, right) { this.val = val; ... } }
function count(node) { if (!node) return 0; return node.val + count(node.left); }
```

`node.val`, `node.left`, `node.right` each invoke `js_property_get` (hashmap lookup).
If `node` is tagged as an instance of `Node` in the var scope, these could be direct loads.

### Approach

1. **Instance Variable Tagging**: When `let node = new Foo(...)` or a function parameter
   is assigned from a `new Foo(...)` call, tag the variable entry in `JsMirVarEntry` with
   a class reference:

   ```cpp
   struct JsMirVarEntry {
       // ... existing fields ...
       JsClassEntry* class_entry;   // non-NULL if variable is a class instance
   };
   ```

2. **Property Access Optimization**: In `jm_transpile_expression` for `JS_AST_NODE_MEMBER_EXPRESSION`,
   check if `mem->object` resolves to a variable tagged with a class. If yes, look up
   `prop_name` in the class's A5 ctor_prop_ptrs[] to get the slot index, then emit a
   direct load:

   ```cpp
   JsMirVarEntry* obj_var = jm_find_var(...);
   if (obj_var && obj_var->class_entry && !mem->computed) {
       int slot = jm_ctor_prop_slot(obj_var->class_entry->constructor->fc, prop_name);
       if (slot >= 0) {
           int offset = JS_OBJECT_HEADER_SIZE + slot * sizeof(uint64_t);
           MIR_reg_t result = jm_new_reg(mt, "propld", MIR_T_I64);
           jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
               MIR_new_reg_op(mt->ctx, result),
               MIR_new_mem_op(mt->ctx, MIR_T_I64, offset, obj_var->reg, 0, 1)));
           return result;
       }
   }
   // Fall back to js_property_get
   ```

3. **Propagate Class Tag**: Propagate the class tag through:
   - `let x = someFunc()` when `someFunc` is a constructor (return type = class instance)
   - `let x = y` or parameters — tag is copied from rhs variable

### Expected Impact

For tree/graph algorithms (the JS equivalent of Lambda's triangl/splay benchmarks),
this converts O(N) hashmap lookups into O(N) direct memory loads — a significant
constant factor improvement that can be 5–20x for property-access-heavy code.

---

## Proposal 5: Arithmetic on Module Variables Without Boxing

### Problem

For module-level variables accessed via `js_get_module_var(index)` / `js_set_module_var(index, val)`,
arithmetic operations currently go through the full box→compute→unbox cycle:

```
Load boxed MODVAR → call it2i/it2d to unbox → compute → box result → call js_set_module_var
```

When a module variable is assigned a constant int/float during initialization, the
transpiler already records this in `JsModuleConstEntry` with `const_type = MCONST_INT` or
`MCONST_FLOAT`. But for mutable module variables (`MCONST_MODVAR`), the JIT still goes
through `js_{get,set}_module_var`.

### Approach

Where a `JsModuleConstEntry` has `const_type == MCONST_MODVAR`, and the variable was first
initialized with a numeric literal (giving a known `TypeId`), extend the entry to record
the initial type:

```cpp
struct JsModuleConstEntry {
    // ... existing fields ...
    TypeId modvar_type;  // NEW: type of this module-level variable (if known)
};
```

When loading a typed MODVAR, emit an unbox inline after the load. When storing,
emit a box inline before the store. This is functionally the same but avoids a second
function call for the type conversion.

For cases where the MODVAR type is `LMD_TYPE_INT` (integer counter, loop index),
and the use is a `+= 1` update, the entire load→unbox→add→box→store sequence can be
tightened to be fully inline — matching what happens for local typed variables today.

### Expected Impact

Module-level counters and accumulators in top-level loop code (e.g., running totals,
histogram buckets) eliminate the type-conversion function calls on each iteration.

---

## Proposal 6: Function Inlining for Single-Expression Functions

### Problem

Restructure4 identifies function inlining as a remaining bottleneck for MIR Direct.
In JS, many utility functions are single-expression:

```js
function square(x)   { return x * x; }
function clamp(x, lo, hi) { return x < lo ? lo : x > hi ? hi : x; }
function dot(ax, ay, bx, by) { return ax * bx + ay * by; }
```

These functions:
- Are called thousands of times in tight loops
- Have typed parameters (inferred as INT or FLOAT by `jm_infer_param_types`)
- Generate a native MIR function (`has_native_version = true`)

Each call costs: function call setup/teardown + 2–4 box/unbox conversions
(even with native versions, MIR still pays ABI overhead for each call).

### Approach

Extend `jm_resolve_native_call` to also check if inlining is eligible:

```cpp
static bool jm_should_inline(JsFuncCollected* fc) {
    if (!fc->has_native_version) return false;
    if (fc->capture_count > 0) return false;  // closures excluded
    if (fc->param_count > 4) return false;    // limit complexity
    // Check if body is a single return statement with an expression
    JsFunctionNode* fn = fc->node;
    if (!fn->body || fn->body->node_type != JS_AST_NODE_BLOCK_STATEMENT) return false;
    JsBlockNode* blk = (JsBlockNode*)fn->body;
    int stmt_count = 0;
    JsAstNode* s = blk->statements;
    while (s) { stmt_count++; s = s->next; }
    return stmt_count == 1 && blk->statements &&
           blk->statements->node_type == JS_AST_NODE_RETURN_STATEMENT;
}
```

When inlining is eligible, instead of emitting a MIR `CALL` instruction:
1. Push a "virtual scope" for the function's parameters
2. Map call-site argument registers to parameter names
3. Transpile the function body inline (the single return expression)
4. Pop the virtual scope

This eliminates the call overhead entirely — zero ABI cost for inlined functions.

### Limitations & Safety

- Inlining increases code size — limit to functions called ≥ 2 times in the same function (to avoid code bloat for single-use helpers)
- Recursive functions cannot be inlined
- Functions with `try/catch` bodies should not be inlined
- First pass: only inline single-statement functions (trivially safe)

### Expected Impact

For compute benchmarks with small hot utility functions (like triangl's movement checks
or splay's key comparisons), this eliminates 2–4 function call overhead cycles per
inner loop iteration. V8 inlines aggressively; this closes a structural gap.

---

## Proposal 7: `jm_resolve_native_call` Extension to Typed Method Calls

### Problem

`jm_resolve_native_call()` currently only handles **direct function calls** where the callee
is a simple identifier (`function_name(args)`). Class method calls like `node.compute(x)`
never go through the native path even when `node` is a typed class instance and `compute`
has a known native version.

### Approach

Extend `jm_resolve_native_call` to also handle `MEMBER_EXPRESSION` callees:

```cpp
static JsFuncCollected* jm_resolve_native_call(JsMirTranspiler* mt, JsCallNode* call) {
    // ... existing identifier path ...

    // NEW: Handle obj.method(args) when obj has a known class type
    if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* mem = (JsMemberNode*)call->callee;
        if (!mem->computed && mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsMirVarEntry* obj_var = jm_get_var_from_expr(mt, mem->object);
            if (obj_var && obj_var->class_entry) {
                JsIdentifierNode* method_name = (JsIdentifierNode*)mem->property;
                JsClassMethodEntry* method = jm_find_class_method(
                    obj_var->class_entry, method_name->name);
                if (method && method->fc && method->fc->has_native_version)
                    return method->fc;
            }
        }
    }
    return NULL;
}
```

This requires Proposal 4 (typed class instance tagging) as a prerequisite.

### Expected Impact

Class-based OOP code (common in JS) can use native method calls when the receiver type
is known at compile time. For tight loops over typed class instances (iterating a linked
list, traversing a tree), this removes the dynamic dispatch overhead per call.

---

## Summary: Priority Matrix

| Proposal | Complexity | Impact | Prerequisite | Status |
|----------|------------|--------|-------------|--------|
| **P1** Return type → var type propagation | Low | Medium | None | ✅ Done |
| **P2** Bump-pointer nursery inline alloc | Medium | High (allocation-heavy code) | Runtime nursery already in place | ✅ Done |
| **P3** Direct property stores in constructor | Medium | High (class-heavy code) | A5 scan (already done) | ✅ Done |
| **P4** Direct property access for typed instances | Medium | High (tree/graph code) | P3, A5 scan | ✅ Done |
| **P5** Module variable arithmetic without boxing | Low | Medium (top-level loops) | None | ✅ Done |
| **P6** Single-expression function inlining | Medium-High | High (compute benchmarks) | None | ✅ Done |
| **P7** Native method call resolution | Low once P4 done | Medium | P4 | ✅ Done |

### Implementation Progress

- ✅ **P1** — Early native eligibility flag (Phase 1.75) + forward native func declarations (Phase 1.9). Validated: 670/670 tests pass.
- ✅ **P5** — `modvar_type` field on `JsModuleConstEntry`; inline arithmetic for INT module-variable compound assigns. Validated: 670/670 tests pass.
- ✅ **P6** — Single-expression function inlining via `jm_transpile_inline_native()`. Eligible when `has_native_version`, no captures, ≤4 params, single return statement. Validated: 670/670 tests pass.
- ✅ **P3** — `js_set_shaped_slot()` runtime helper + A5-style pre-shaping for class `new` path + `is_constructor` flag on `JsFuncCollected`. Eliminates `js_property_set` calls for `this.prop = val` in constructors. Validated: 670/670 tests pass.
- ✅ **P4** — `js_get_shaped_slot()` runtime helper + `class_entry` field on `JsMirVarEntry` + detection at `var = new ClassName()` declaration sites. Direct slot-indexed reads for typed class instance property access. Both helpers registered in `jit_runtime_imports[]`. Validated: 670/670 tests pass.
- ✅ **P7** — Extended `jm_resolve_native_call` to handle `MEMBER_EXPRESSION` callees. Checks `JsMirVarEntry.class_entry` for local vars and `JsModuleConstEntry.class_entry` (new field) for module-level vars (top-level `var x = new Foo()`). When receiver type + method native version are both known, emits a direct MIR `CALL` to the `_n` native function, bypassing generic boxing + runtime dispatch. P7 path also delegates to P6 inlining when the method body is a single return expression. Validated: 677/677 tests pass.
- ✅ **P2** — `js_new_object()` and `js_new_object_with_shape()` in `js_runtime.cpp` now call `heap_calloc_class(sizeof(Map), LMD_TYPE_MAP, JS_MAP_SIZE_CLASS)` instead of `heap_calloc()`. Pre-computed size class index (JS_MAP_SIZE_CLASS=1, since sizeof(Map)=32 maps to SIZE_CLASSES[1]) skips the O(7) class-index lookup and uses the bump-pointer fast path directly. Validated: 677/677 tests pass.

---

## Relationship to Lambda Restructuring Phases

| Lambda Proposal | JS Analogue |
|-----------------|-------------|
| Restructure1 §1: Dual `_n`/`_b` versions | ✅ Already in JS (Phase 4) |
| Restructure1 §2: `Ret*` structured returns | Not directly applicable — JS uses exception frames for error propagation |
| Restructure2: JIT header diet | Shared — JS also uses `lambda.h`; benefits automatically |
| Restructure3 P5: Direct field stores | → **P3** (constructor direct stores) |
| Restructure3 P5: Direct field reads | → **P4** (typed instance direct reads) |
| Restructure3 Phase 3: Body-usage type inference | → **P1** (return type → var type) |
| Restructure4: MIR Direct dual versions | ✅ Already in JS (Phase 4) |
| Restructure4: Function inlining (mentioned as gap) | → **P6** |
| Restructure5: Bump-pointer nursery + JIT-inline | → **P2** |
| Restructure5: A5 ctor prop scan | ✅ Already in JS (scan done), → **P3** to complete |
| Restructure6: Direct string pointer | Applicable but lower priority for JS (fewer string-heavy compute paths) |

The proposals above represent the highest-leverage gaps between the current JS engine
and V8's performance on compute + allocation benchmarks.
