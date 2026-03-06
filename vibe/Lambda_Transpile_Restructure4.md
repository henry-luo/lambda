# Lambda Transpiler — Performance Restructuring Proposal (Phase 4)

## Overview

This proposal targets **MIR Direct transpiler performance enhancements** — specifically eliminating unnecessary boxing/unboxing overhead at function boundaries and reducing call overhead for small frequently-invoked functions. Phase 3 achieved a **~3.1x** geometric mean vs Node.js on compute benchmarks; this phase aims to push further by attacking the calling convention overhead that remains the dominant bottleneck.

| Benchmark | Current MIR Direct | Node.js | Ratio | Primary Remaining Bottleneck |
|-----------|------------------:|--------:|------:|------------------------------|
| triangl | ~340ms | 93ms | ~3.7x | Call overhead + `set_at` mutations |
| diviter | ~285ms (CSI) | 488ms | 0.58x | Already faster — dual versions would improve further |
| collatz | ~439ms (CSI) | 1,459ms | 0.30x | Already faster — inlining could improve further |
| gcbench | ~436ms | 27ms | 16.1x | GC/allocation dominated |
| gcbench2 | ~266ms | 27ms | 9.9x | GC/allocation dominated |

### Prior Art

- [Lambda_Transpile_Restructure.md](Lambda_Transpile_Restructure.md) — Dual-version function generation (`_n`/`_b`) in C2MIR, structured returns (`RetItem`)
- [Lambda_Transpile_Restructure2.md](Lambda_Transpile_Restructure2.md) — JIT header diet, O(1) sys func lookup, config-driven code gen
- [Lambda_Transpile_Restructure3.md](Lambda_Transpile_Restructure3.md) — P1/P5/B2/CSI/D1/D3-MIR optimizations, body-usage type inference

This Phase 4 focuses on **MIR Direct calling convention optimization, function inlining, and additional hot-path improvements**.

---

## Root Cause Analysis

### Why MIR Direct Still Has Overhead at Function Boundaries

#### The Boxing Round-Trip Problem

Currently, MIR Direct uses a **uniform Item ABI** for all user function calls:

1. **Caller**: Evaluates arguments as native types (e.g., `int64_t`, `double`), then **boxes** each argument to `Item` via `transpile_box_item()` (calls `emit_box_int`, `emit_box_float`, etc.)
2. **Callee**: Receives all parameters as `MIR_T_I64` (boxed Items), then **unboxes** each parameter at entry via `emit_unbox()` (calls `it2i`, `it2d`, etc.)
3. **Callee return**: Computes result as native type, then **re-boxes** via `transpile_box_item()` before returning
4. **Caller**: Receives boxed `Item` return, then **unboxes** via `emit_unbox()` if the call type is known

For a function `fn add(x: int, y: int) -> int`, each call performs **4 unnecessary conversions**:

```
Caller:  x_native → emit_box_int(x) → Item_x     ← box arg 1
         y_native → emit_box_int(y) → Item_y     ← box arg 2
Callee:  Item_x → emit_unbox(INT) → x_native     ← unbox param 1
         Item_y → emit_unbox(INT) → y_native     ← unbox param 2
         result_native → emit_box_int(r) → Item  ← box return
Caller:  Item → emit_unbox(INT) → result_native  ← unbox return
```

Each box/unbox is either a multi-instruction inline sequence (for INT: mask, OR, range check, branch) or a runtime function call (`it2i`, `it2d`, `push_d`). On Apple Silicon at ~3ns per call, a function invoked millions of times (like `triangl`'s recursive `try_it`) burns significant overhead just converting between representations.

#### Current Code Path (transpile-mir.cpp)

**Function definition** (`transpile_func_def`, line ~7300):
```cpp
// All params hardcoded as MIR_T_I64 (boxed Item)
params[param_count] = {MIR_T_I64, strdup(pname), 0};
```
After function creation, typed/inferred params are unboxed at entry:
```cpp
if (!may_be_null && (tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT || ...)) {
    MIR_reg_t unboxed = emit_unbox(mt, preg, tid);
    set_var(mt, pname, unboxed, mtype, tid);
}
```

**Function call** (`transpile_call`, line ~5300):
```cpp
// All args boxed before call
MIR_reg_t val = transpile_box_item(mt, resolved_args[i]);
arg_ops[i] = MIR_new_reg_op(mt->ctx, val);
arg_vars[i] = {MIR_T_I64, "p", 0};  // boxed Item ABI
```

**Function return** (line ~7500):
```cpp
// Body always returns boxed Item
MIR_reg_t body_result = transpile_box_item(mt, fn_node->body);
```

#### Why C2MIR Doesn't Have This Problem

C2MIR generates **two versions** of typed functions:
1. **Native version** (`_f12_add`): Takes native C types (`int64_t x, int64_t y`), returns native type. Used for intra-module direct calls.
2. **Boxed wrapper** (`_f12_add_b`): Takes `Item` params, unboxes → calls native → re-boxes return. Used for `fn_call*` dynamic dispatch.

The `needs_fn_call_wrapper()` function (transpile.cpp line ~446) decides when dual versions are needed:
- Function has typed (non-ANY) parameters
- Function has `can_raise` attribute
- Function has no parameters but returns native type

For intra-module calls where both caller and callee types are known at compile time, C2MIR emits a direct call to the native version — **zero boxing overhead**.

---

## Proposal 1: Dual Function Versions (Boxed + Native)

### Summary

Generate two MIR function versions for user functions with typed or inferred-typed parameters:
- **Native version**: Parameters use native MIR types (`MIR_T_I64` for int/bool/string/int64, `MIR_T_D` for float), return type is native
- **Boxed wrapper**: All-`MIR_T_I64` (Item) ABI — unboxes params, calls native, re-boxes return. Used for dynamic dispatch (closures, `fn_call*`, cross-module imports)

### Design

#### 1.1 Determine Eligibility

A function qualifies for dual-version generation when:
- **Not a closure** (closures already use Item ABI; captured variables require boxing)
- **Has at least one typed/inferred parameter** with a type that maps to a different native representation (INT, FLOAT, BOOL, STRING, INT64)
- **OR** has a known native return type that differs from Item

Use the existing `infer_param_type()` infrastructure (already in MIR Direct) to determine parameter types for untyped parameters.

```cpp
static bool mir_needs_dual_version(AstFuncNode* fn_node) {
    if (fn_node->captures) return false;  // closures excluded
    
    AstNamedNode* param = fn_node->param;
    while (param) {
        TypeId tid = param->type ? param->type->type_id : LMD_TYPE_ANY;
        // Also check if inference would give a concrete type
        if (tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT || tid == LMD_TYPE_BOOL ||
            tid == LMD_TYPE_STRING || tid == LMD_TYPE_INT64) {
            return true;
        }
        param = (AstNamedNode*)param->next;
    }
    // Check return type if known
    // ...
    return false;
}
```

#### 1.2 Generate Native Version

Modify `transpile_func_def` to accept a `bool native_version` flag. When true:

1. **Parameter types**: Use `type_to_mir(param_tid)` instead of hardcoded `MIR_T_I64`:
   ```cpp
   // Instead of: params[i] = {MIR_T_I64, strdup(pname), 0};
   TypeId tid = /* declared or inferred type */;
   params[i] = {type_to_mir(tid), strdup(pname), 0};
   ```

2. **Skip entry unboxing**: Since params arrive as native types, skip the `emit_unbox` at function entry.

3. **Return type**: Use the resolved return type instead of `MIR_T_I64`:
   ```cpp
   MIR_type_t ret_type = type_to_mir(return_tid);  // e.g., MIR_T_D for float
   ```

4. **Return value**: Return a native value instead of boxing:
   ```cpp
   // Instead of: transpile_box_item(mt, fn_node->body)
   MIR_reg_t body_result = transpile_expr(mt, fn_node->body);
   ```

5. **Function naming**: Use base name (e.g., `_f0_add`) for the native version, `_f0_add_b` for the boxed wrapper.

#### 1.3 Generate Boxed Wrapper

After the native function is defined, generate a thin wrapper function:

```cpp
static void transpile_func_boxed_wrapper(MirTranspiler* mt, AstFuncNode* fn_node) {
    // 1. Create wrapper function: _f0_add_b(Item _x, Item _y) -> Item
    //    All params MIR_T_I64, return MIR_T_I64
    
    // 2. For each param, emit unbox to native type
    //    e.g., int64_t x_native = it2i(_x);
    
    // 3. Call native version with native args
    //    e.g., int64_t result = _f0_add(x_native, y_native);
    
    // 4. Box result back to Item
    //    e.g., Item boxed = i2it(result);
    
    // 5. Return boxed result
}
```

This is the MIR Direct equivalent of C2MIR's `define_func_boxed()` (transpile.cpp line 6556), but emitting MIR instructions instead of C code.

#### 1.4 Update Call Emission

In the local/known function call path (~line 5300):

```cpp
// Determine if we can use native call
bool can_native_call = fn_def && mir_has_native_version(fn_def);

if (can_native_call) {
    // Emit args in native types (no boxing)
    for (int i = 0; i < expected_params; i++) {
        TypeId param_tid = /* get param's native type */;
        MIR_reg_t val = transpile_expr(mt, resolved_args[i]);
        // Convert if needed (e.g., int to float promotion)
        arg_ops[i] = MIR_new_reg_op(mt->ctx, val);
        arg_vars[i] = {type_to_mir(param_tid), "p", 0};
    }
    // Return type is native
    MIR_type_t ret_type = type_to_mir(return_tid);
    // ... emit MIR_CALL to native version
} else {
    // Existing boxed call path
}
```

#### 1.5 Register Boxed Wrapper for Dynamic Dispatch

When a dual-version function is used as a first-class value (passed to higher-order functions, stored in closures), register the boxed wrapper address:

```cpp
// In transpile_ident when creating Function* objects:
// Use _b wrapper address instead of native version
MIR_item_t wrapper = find_local_func(mt, boxed_name);
fn_addr = MIR_new_ref_op(mt->ctx, wrapper);
```

#### 1.6 TCO Interaction

The existing TCO mechanism (tail-call optimization) assigns to parameter registers and jumps back. With native versions:
- TCO args are evaluated as native types directly (skip box/unbox)
- Register assignments use the native type (MIR_DMOV for float, MIR_MOV for int)
- This is already partially correct since `infer_param_type` narrows the types, but the current code still boxes/unboxes through the Item ABI

### Expected Impact

**Call-intensive benchmarks** (triangl, AWFY suite):
- Eliminates 2× box + 2× unbox per call × millions of iterations
- Each box_int is ~5 MIR instructions (AND + OR + LE + GE + branch); each unbox is a runtime function call (`it2i`, `it2d`)
- **Estimated 15-30% improvement** on call-heavy benchmarks
- For triangl: `try_it` is called recursively ~14M times with 4 int params → eliminates ~112M box + 56M unbox operations

**Float-heavy benchmarks** (mandelbrot, nbody):
- `emit_box_float` calls `push_d` (GC nursery allocation) to box each double return value
- Eliminating this removes one GC allocation per function return
- **Estimated 20-40% improvement** on float-intensive code

### Implementation Complexity: Medium

| Step | Effort | Risk |
|------|--------|------|
| `mir_needs_dual_version()` eligibility check | Low | Low |
| Native version generation (modify `transpile_func_def`) | Medium | Medium — must handle all param types correctly |
| Boxed wrapper generation (`transpile_func_boxed_wrapper`) | Medium | Low — thin wrapper, well-defined pattern |
| Call emission update (native vs boxed dispatch) | Medium | Medium — must distinguish local/import/dynamic paths |
| TCO with native params | Low | Low — mostly simplification of existing code |
| First-class function references (use wrapper) | Low | Low |
| **Total** | **~2-3 days** | **Medium** |

---

## Proposal 2: Inline Short User Functions

### Summary

For small user functions (single-expression body or simple conditional), replace the `MIR_CALL` with inline emission of the function body at the call site. This eliminates the function call ABI overhead entirely: no parameter passing, no return value handling, no stack frame setup.

### Design

#### 2.1 Inlining Criteria

A function is eligible for inlining when:
- **Body is a single expression** (not a content block with declarations)
- **Not recursive** (direct or through TCO)
- **Not a closure** (no captured variables to manage)
- **Parameter count ≤ 4** (keep register pressure manageable)
- **Not variadic**
- **Body does not contain function definitions** (nested closures)
- **Call site is within the same module** (can access AST)

Score functions by "inlining benefit" — prioritize functions called in tight loops:

```cpp
static bool mir_should_inline(AstFuncNode* fn_def, AstCallNode* call_site) {
    if (fn_def->captures) return false;
    if (is_recursive(fn_def)) return false;
    
    // Check body complexity
    int body_depth = ast_expression_depth(fn_def->body);
    if (body_depth > 3) return false;  // too complex
    
    // Check parameter count
    int param_count = count_params(fn_def);
    if (param_count > 4) return false;
    
    return true;
}
```

#### 2.2 Inline Emission

At the call site, instead of emitting `MIR_CALL`:

1. **Push a new scope** for parameter bindings
2. **Bind arguments to parameter names** as local variables (using the caller's evaluated argument registers)
3. **Transpile the function body** inline as if it were a local expression
4. **Pop the scope**

```cpp
static MIR_reg_t transpile_inline_call(MirTranspiler* mt, AstFuncNode* fn_def, AstCallNode* call) {
    push_scope(mt);
    
    // Bind args to param names
    AstNamedNode* param = fn_def->param;
    AstNode* arg = call->argument;
    while (param && arg) {
        char pname[64];
        snprintf(pname, sizeof(pname), "%.*s", (int)param->name->len, param->name->chars);
        
        MIR_reg_t val = transpile_expr(mt, arg);
        TypeId val_tid = get_effective_type(mt, arg);
        set_var(mt, pname, val, type_to_mir(val_tid), val_tid);
        
        param = (AstNamedNode*)param->next;
        arg = arg->next;
    }
    
    // Transpile body inline (NOT boxed — direct native expression)
    MIR_reg_t result = transpile_expr(mt, fn_def->body);
    
    pop_scope(mt);
    return result;
}
```

#### 2.3 Handling Parameters as SSA Values

MIR uses SSA, so inlined parameter references must map correctly:
- Each argument expression is evaluated once and bound to a register
- The parameter name resolves to that register during body transpilation
- No aliasing issues since parameters are read-only in Lambda (functional `fn`)

For `pn` functions with mutable parameters, the inlined scope needs mutable local copies, which is already how `set_var` works.

#### 2.4 Recursive Inlining Guard

To prevent infinite expansion (A calls B calls A), maintain a set of "currently being inlined" functions:

```cpp
// In MirTranspiler struct:
AstFuncNode* inline_stack[8];
int inline_depth;
```

If a function appears in the inline stack, skip inlining and fall back to normal call emission.

#### 2.5 Depth-Limited Multi-Level Inlining

Allow one level of transitive inlining: if `f` calls `g` and both are inlineable, inline `g` into `f`'s inlined body but stop there. This covers common patterns like:

```lambda
fn square(x: int) = x * x
fn distance(a: int, b: int) = square(a - b)
// At call site: distance(p, q) → (p - q) * (p - q)
```

### Expected Impact

**Tiny helper functions** (common in recursive algorithms):
- Eliminates function call overhead (~8-12ns per call on Apple Silicon including stack frame)
- For triangl with ~14M recursive calls, inlining the board-test helper could save ~100-200ms

**Lambda built-in patterns** (identity, projection, constant functions):
- `fn id(x) = x` → eliminated entirely
- `fn fst(a, b) = a` → eliminated entirely
- Common in higher-order function usage

**Estimated improvement**: 10-25% on call-heavy benchmarks, up to 50% for code that heavily uses tiny helper functions.

### Implementation Complexity: Medium-High

| Step | Effort | Risk |
|------|--------|------|
| Inlining criteria check | Low | Low |
| Inline body emission | Medium | Medium — must handle scope correctly |
| Recursive guard | Low | Low |
| Parameter binding (SSA mapping) | Medium | Medium — MIR register management |
| Multi-level inlining | High | Medium — combinatorial complexity |
| **Total** | **~3-4 days** | **Medium-High** |

---

## Proposal 3: Additional MIR Direct Optimizations

### 3.1 Boolean Specialization (D5)

**Problem**: The triangl benchmark spends significant time in `fn_and`, `fn_not`, and `is_truthy` for boolean operations on array elements retrieved via `item_at`. Even with D3-MIR inline array reads, the retrieved element is a boxed `Item` that must be checked for truthiness through runtime dispatch.

**Current code path** for `a[i] && a[j]`:
```
item_at(arr, i) → boxed Item
  → is_truthy(Item) → runtime function call
item_at(arr, j) → boxed Item
  → is_truthy(Item) → runtime function call
fn_and(Item, Item) → runtime function call
```

**Proposed solution**: When the array element type is known to be `bool` (from TypeArray's nested type), skip the `is_truthy` runtime call and use the inline boolean path:

1. After `item_at`, if array is typed `bool[]`, emit inline unbox to native bool:
   ```
   item_at(arr, i) → Item → emit_unbox(BOOL) → native 0/1
   ```
2. Use native MIR boolean logic (AND/OR/NOT as MIR instructions) instead of `fn_and`/`fn_or`/`fn_not`

**Expected impact**: 15-25% improvement on triangl. The ~4.7x gap vs Node.js is dominated by boolean ops in the inner loop.

**Complexity**: Medium — requires propagating array element types through `item_at` dispatch and maintaining type info on the returned value.

### 3.2 Inline `item_at` for Provably-Typed Arrays (D6)

**Problem**: `item_at` is a generic runtime function that dispatches on the container's `type_id` at runtime. For arrays whose type is statically known (via AST type info or inference), this dispatch is unnecessary.

**Current path** (D3-MIR runtime type check):
```
emit: reg = *(arr_ptr + 0)              // load type_id byte
emit: if (type_id == ARRAY_INT) goto fast_path
emit: fast_path: val = array_int_get(arr, idx)
emit: else: val = item_at(arr, idx)     // fallback
```

**Proposed enhancement**: When the array variable has a **provably fixed type** (from declaration, type annotation, or inference) and that type has been consistent since assignment, skip the runtime type check entirely:

```
// When arr is provably ArrayInt:
emit: data_ptr = *(arr_ptr + 16)        // load data pointer
emit: val = *(data_ptr + idx * 8)       // direct memory load
```

This eliminates both the type check branch and the function call overhead.

**Expected impact**: 10-15% improvement on array-heavy code (triangl inner loop, AWFY benchmarks using arrays).

**Complexity**: Medium — type tracking through variable assignments must be sound; any re-assignment to a different array type must invalidate the optimization.

### 3.3 Procedure Return Type Inference (P6)

**Problem**: MIR Direct always returns `MIR_T_I64` (boxed Item) from user functions. Even when the function body's last expression has a known native type (e.g., an `int` arithmetic result), it gets boxed via `transpile_box_item()` before return and unboxed by the caller.

**Proposed solution**: Analyze the function body to determine the return type:
1. For `fn` functions: the body expression's type IS the return type
2. For `pn` functions: collect all `return` statement types and unify
3. If a consistent native type is found, use it as the MIR return type

This naturally combines with Proposal 1 (dual versions): the native version returns the inferred type, the boxed wrapper re-boxes it.

**Expected impact**: Moderate — primarily enables Proposal 1's full benefit. Without return type inference, even "native version" functions must box their return values.

**Complexity**: Low-Medium — the type analysis infrastructure already exists via `get_effective_type()` and `infer_param_type()`.

### 3.4 Loop-Invariant Code Motion (D2)

**Problem**: In `for` loops iterating over arrays, the array's `data` pointer and `length` are re-loaded from the struct on every iteration via `item_at`. Since Lambda arrays are immutable, these loads are loop-invariant.

**Current pattern** (simplified):
```
loop:
  arr_ptr = strip_tag(arr_item)        // re-computed each iteration
  type_id = *(arr_ptr + 0)             // re-loaded each iteration
  data    = *(arr_ptr + 16)            // re-loaded each iteration
  len     = *(arr_ptr + 24)            // re-loaded each iteration (for bounds)
  val     = *(data + idx * 8)          // actual access
```

**Proposed solution**: Hoist the tag stripping, type check, data pointer load, and length load ABOVE the loop:

```
arr_ptr = strip_tag(arr_item)          // hoisted
type_id = *(arr_ptr + 0)              // hoisted
data    = *(arr_ptr + 16)             // hoisted
len     = *(arr_ptr + 24)             // hoisted
loop:
  val = *(data + idx * 8)             // only this remains in the loop
```

**Expected impact**: 5-10% on array iteration loops. Each hoisted operation saves 1 memory load per iteration.

**Complexity**: Medium — requires identifying loop-invariant expressions at the MIR emission level. A simpler approach is to specialize `for` loops that iterate over known-type arrays at the transpiler level.

### 3.5 Speculative Native Arithmetic for Untyped Code

**Problem**: When both operands of an arithmetic expression resolve to `LMD_TYPE_ANY` at compile time (no type info, no inference possible), the current path boxes both operands and calls `fn_add`/`fn_sub`/etc. These runtime functions dispatch on type tags, which is expensive.

**Proposed solution**: Emit a speculative fast path that checks both operands are INT at runtime, performs native arithmetic, and only falls back to the boxed path if types don't match:

```
// Speculative INT fast path for ANY + ANY
emit: tag_a = a >> 56
emit: tag_b = b >> 56
emit: if (tag_a != INT_TAG || tag_b != INT_TAG) goto slow_path
emit: val_a = a & MASK56  // sign-extend
emit: val_b = b & MASK56
emit: result = val_a + val_b
emit: boxed_result = (INT_TAG | (result & MASK56))
emit: goto done
emit: slow_path: boxed_result = fn_add(a, b)
emit: done:
```

**Expected impact**: 20-40% on dynamically-typed arithmetic-heavy code. The speculative check is ~4 instructions (shift, compare, branch) vs the ~20+ instruction overhead of boxing + function call + dispatch.

**Complexity**: Medium — inline MIR emission for the fast path is straightforward, but correctness requires handling overflow (INT result might exceed 56-bit range) and mixed-type cases.

---

## Prioritized Implementation Order

| Priority | Optimization | Expected Impact | Effort | Dependencies |
|:--------:|-------------|----------------|--------|-------------|
| **1** | **P4-1: Dual function versions** | 15-30% on call-heavy | 2-3 days | None |
| **2** | **P4-3.3: Return type inference** | Enables P4-1 fully | 1-2 days | None (but amplifies P4-1) |
| **3** | **P4-3.1: Boolean specialization** | 15-25% on triangl | 2 days | D3-MIR (done) |
| **4** | **P4-2: Inline short functions** | 10-25% on call-heavy | 3-4 days | P4-1 (benefits compound) |
| **5** | **P4-3.2: Inline item_at** | 10-15% on array code | 2 days | D3-MIR (done) |
| **6** | **P4-3.4: Loop-invariant hoisting** | 5-10% on loops | 2 days | P4-3.2 (benefits compound) |
| **7** | **P4-3.5: Speculative native arith** | 20-40% on untyped arith | 2-3 days | None |

**Recommended order**: P4-1 → P4-3.3 → P4-3.1 → P4-2 → P4-3.2

P4-1 (dual versions) combined with P4-3.3 (return type inference) eliminates the largest source of overhead — the boxing round-trip at every function boundary. P4-3.1 (boolean specialization) then addresses the second-largest bottleneck specific to triangl. P4-2 (inlining) compounds the gains by eliminating call overhead entirely for small functions.

---

## Implementation Status

| Optimization                       | Status        | Date       | Test Results                                | Notes                                      |
| ---------------------------------- | ------------- | ---------- | ------------------------------------------- | ------------------------------------------ |
| **P4-1: Dual function versions**   | ✅ Done        | 2026-03-06 | 604/606 pass (2 pre-existing REPL failures) | Params native, return still boxed          |
| **P4-3.3: Return type inference**  | ✅ Done        | 2026-03-07 | 604/606 pass (156/156 MIR)                  | Native return for fn functions             |
| **P4-3.1: Boolean specialization** | ✅ Done        | 2026-03-07 | 604/606 pass (155/156 MIR)                  | Inline bool array read, ~290-380ms triangl |
| **P4-2: Inline short functions**   | ❌ Not started | —          | —                                           |                                            |
| **P4-3.2: Inline item_at**         | ❌ Not started | —          | —                                           |                                            |

### P4-1 Implementation Details (Completed)

**What was implemented** in `transpile-mir.cpp`:

1. **`NativeFuncInfo` struct** (~line 215): Tracks per-function native parameter types, MIR types, return type, and `has_native` flag. Uses a hashmap for O(1) lookup by mangled function name.

2. **Pre-resolution in `transpile_func_def`** (~line 7400): All parameter types are resolved (declared + body-usage inference) into a `resolved_param_types[16]` array BEFORE the MIR function is created. This determines eligibility for dual-version generation.

3. **Eligibility criteria**: Not a closure, not a method, not variadic, and at least one parameter has a native type (INT, FLOAT, BOOL, STRING, INT64). The `mir_is_native_param_type()` helper checks eligibility.

4. **Native version**: When eligible, the MIR function is created with native-typed parameters (`type_to_mir(resolved_tid)` instead of hardcoded `MIR_T_I64`). Entry unboxing is skipped since params arrive as native types. Body still returns boxed Item (return type inference is P4-3.3).

5. **`emit_native_boxed_wrapper()`** (~line 7345): Generates a thin `_b` suffixed wrapper with all-Item ABI. The wrapper unboxes each param → calls native version → returns result directly.

6. **Call emission** (~line 5350): Checks `NativeFuncInfo` for the target function. When native calling is available, passes args with native types directly (skipping boxing), and uses native MIR types in the call proto.

7. **Function\* references**: Both the ident path (line ~1548) and func-expr path (line ~6970) check for native versions and redirect to the `_b` wrapper for dynamic dispatch.

8. **Forward declarations** (`prepass_forward_declare`, ~line 7960): Forward-declares both the native name and `_b` wrapper. Pre-registers `NativeFuncInfo` early so that call sites in later-defined functions (mutual recursion) can use native calling convention.

**Current limitation**: The native version still returns boxed `Item` (`MIR_T_I64`). Callers still unbox the return value. P4-3.3 (Return Type Inference) will address this by enabling native return types.

### P4-3.3 Implementation Details (Completed)

**What was implemented** in `transpile-mir.cpp`:

1. **`infer_return_type()`** (~line 7450): Infers native return types for `fn` functions from two sources: (a) TypeFunc::returned declared annotation, (b) body expression type. Only accepts INT, FLOAT, BOOL. Excludes `pn` (procedural) functions (multiple `return` paths) and `can_raise` functions (`T^E` error return types — error branches don't produce native values).

2. **NativeFuncInfo return fields**: Both registration sites (`transpile_func_def` and `prepass_forward_declare`) now call `infer_return_type()` to populate `nfi.return_type` and `nfi.return_mir` instead of hardcoding `LMD_TYPE_ANY`/`MIR_T_I64`.

3. **Native function body** (~line 7960): When `native_return` is true, the body uses `transpile_expr()` instead of `transpile_box_item()`, producing an unboxed native register. A type safety check compares body's effective type to the expected return type, applying box+unbox conversion if they differ.

4. **Function ret_type**: The MIR function declaration now uses the native return type (e.g., `MIR_T_D` for float, `MIR_T_I64` for int/bool) instead of always `MIR_T_I64`. TCO overflow path also handles double returns (`MIR_new_double_op(0.0)`).

5. **Boxed wrapper** (`emit_native_boxed_wrapper`): Updated to use native return type in the call proto, then boxes the native return via `emit_box()` before returning the boxed Item to the caller.

6. **Call emission** (~line 5490): When calling a native function with known native return: (a) proto uses native return type, (b) result register matches native type, (c) post-call handling skips unnecessary unbox when caller expects the same native type, or boxes the native return when caller expects boxed Item.

**Scope**: Only `fn` (functional) functions get native returns. Procedural (`pn`) functions with explicit `return` statements require propagating the native return type to all return paths — deferred to a future phase.

### P4-3.1 Implementation Details (Completed)

**What was implemented** in `transpile-mir.cpp`:

1. **`MirVarEntry.elem_type`** (~line 81): New `TypeId elem_type` field tracks the element type of container variables. Initialized to `LMD_TYPE_ANY` in `set_var()`. Used by downstream code to know the element type of arrays from `fill()` narrowing.

2. **`fill()` bool narrowing** (~line 3148): Extended the existing `fill(n, val)` detection to handle bool fill values. When `fill_val_tid == LMD_TYPE_BOOL`, sets `var_tid = LMD_TYPE_ARRAY` and `fill_elem_type = LMD_TYPE_BOOL`. After `set_var()`, copies `fill_elem_type` to `v->elem_type` — only when `var_tid` is still `LMD_TYPE_ARRAY` (not overridden by typed array coercion like `int[] = fill(n, true)`).

3. **`get_effective_type()` INDEX_EXPR** (~line 1696): When a node is an INDEX_EXPR, unwraps PRIMARY wrapper nodes around the object, then checks the object variable's `elem_type`. If `elem_type != ANY`, returns it directly, enabling native bool paths in AND/OR/NOT expressions that use `board[i]`.

4. **FAST PATH 1b nested_bool** (~line 4320): For `ARRAY + INT index` with `nested_bool` detected from either AST TypeArray nested type or MirVarEntry elem_type (with PRIMARY unwrapping). Inline Array read: runtime type check → bounds check → `items[idx]` → extract `raw_val & 1` for native bool. Slow path: `item_at()` → `& 1`.

5. **FAST PATH 1d nested_bool** (~line 4720): For `ARRAY + ANY index` (the critical hot path in triangl). Same inline read as 1b but unboxes the index first via `it2i()`. This handles `board[mfrom[mi]]` where `mfrom[mi]` returns a boxed INT Item but `board` has `elem_type=BOOL`.

**Key fixes during implementation**:
- **PRIMARY node unwrapping**: The AST wraps identifiers in PRIMARY nodes. All variable lookups in `get_effective_type` INDEX_EXPR, FAST PATH 1b, and FAST PATH 1d must unwrap PRIMARY to find the IDENT.
- **Typed array coercion guard**: `var arr:int[] = fill(3, true)` coerces the array to ArrayInt via `ensure_typed_array`, changing `var_tid` to ANY. The `elem_type` assignment must check `var_tid == LMD_TYPE_ARRAY` to avoid applying bool elem_type to a coerced integer array.
- **`--mir` flag**: The `run` CLI command requires `--mir` flag to use MIR Direct transpiler; without it, C2MIR is used and P4 optimizations don't apply.

**Performance impact** (triangl benchmark, release build):
- C2MIR (no `--mir`): ~1250-1540ms
- MIR Direct Phase 3 baseline: ~441ms  
- MIR Direct P4-1 + P4-3.3 + P4-3.1: ~290-380ms (median ~340ms)
- Improvement: ~17-34% over Phase 3 baseline

---

## Projected Benchmark Improvements

### With P4-1 (Dual Versions) + P4-3.3 (Return Type Inference)

| Benchmark | Current | Projected | Node.js | Projected Ratio |
|-----------|--------:|----------:|--------:|----------------:|
| triangl (MIR) | ~441ms | ~340-380ms | 93ms | ~3.6-4.1x |
| diviter | ~285ms | ~240-260ms | 488ms | 0.49-0.53x |
| collatz | ~439ms | ~370-400ms | 1,459ms | 0.25-0.27x |

### With P4-1 + P4-2 (Inlining) + P4-3.1 (Boolean Spec)

| Benchmark | Current | Projected | Node.js | Projected Ratio |
|-----------|--------:|----------:|--------:|----------------:|
| triangl (MIR) | ~441ms | ~220-280ms | 93ms | ~2.4-3.0x |
| diviter | ~285ms | ~220-250ms | 488ms | 0.45-0.51x |
| collatz | ~439ms | ~340-380ms | 1,459ms | 0.23-0.26x |

The combination of dual versions + inlining + boolean specialization could reduce the triangl gap from ~4.7x to ~2.5-3.0x, while further widening the advantage on diviter and collatz.

---

## Technical Notes

### MIR ABI Constraints

MIR registers only support `MIR_T_I64`, `MIR_T_F`, `MIR_T_D`, and `MIR_T_LD`. Pointers must be passed as `MIR_T_I64` (cast on Apple Silicon where pointers are 64-bit). The `type_to_mir()` helper already handles this mapping:

```cpp
static MIR_type_t type_to_mir(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_FLOAT: return MIR_T_D;
    default: return MIR_T_I64;  // int, bool, string ptr, etc.
    }
}
```

### Cross-Module Compatibility

MIR Direct already imports C2MIR's `_b` boxed wrappers for cross-module calls (line ~8380). The dual-version scheme should:
- Export both native and `_b` versions via `register_local_func()`
- When calling imported functions from C2MIR modules, use the `_b` import (already working)
- When calling imported functions from other MIR Direct modules, prefer native import if available, fall back to `_b`

### Interaction with TCO

The existing TCO mechanism assigns to parameter registers and jumps. With native versions:
- If the TCO function has a native version, TCO args are evaluated as native types and assigned directly to native registers — no boxing needed
- This simplifies the TCO arg conversion code (currently boxes then unboxes)

### Interaction with Closures

Closures are explicitly excluded from dual-version generation. They already use the Item ABI because:
- Captured variables are stored as boxed Items in the env array
- The closure dispatch mechanism (`fn_call*`) expects Item params
- Closure function pointers are wrapped in `Function*` objects that store the entry point
