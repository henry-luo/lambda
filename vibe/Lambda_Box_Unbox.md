# Lambda Box/Unbox Proposal

## Overview

This proposal optimizes Lambda function calls by generating specialized unboxed code paths when types are statically known, while falling back to boxed (dynamic) dispatch otherwise.

**Current state**: Lambda represents all values as 64-bit `Item` tagged values, incurring overhead for type checks and boxing/unboxing on every operation.

**Goal**: Eliminate this overhead for statically-typed code paths while maintaining full dynamic typing flexibility.

---

## Motivation & Problem Statement

### The Boxing Overhead Problem

Lambda uses a uniform `Item` representation (64-bit tagged values) for all runtime values. While this enables dynamic typing, it introduces significant overhead:

1. **Boxing cost**: Every numeric operation requires packing results back into `Item`
2. **Unboxing cost**: Every numeric operation must extract the raw value from `Item`
3. **Type checking**: Every operation must check `TypeId` tags at runtime
4. **Function call overhead**: All parameters passed as `Item`, all returns as `Item`

Example of current overhead for `x * x`:
```c
// Current: multiple type checks and conversions
Item fn_mul(Item a, Item b) {
    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);
    if (ta == TypeId_Int && tb == TypeId_Int)
        return i2it(it2i(a) * it2i(b));  // unbox, multiply, rebox
    if (ta == TypeId_Float || tb == TypeId_Float)
        return push_d(it2f(a) * it2f(b));  // unbox both, multiply, rebox
    // ... more type combinations
}
```

### The Optimization Opportunity

When the programmer annotates types (e.g., `fn square(x: int) -> int`), we **know at compile time** what types to expect. We can:

1. Generate a specialized "unboxed" version using native C types
2. Skip runtime type checks entirely
3. Use native CPU arithmetic instructions
4. Avoid boxing/unboxing overhead completely

```c
// Optimized: direct native operation
int64_t _square_u15(int64_t x) {
    return x * x;  // single native multiply instruction
}
```

---

## Design Rationale

### Why Two Versions?

A typed function needs **two versions** to handle both static and dynamic call contexts:

| Version | Purpose | When Used |
|---------|---------|-----------|
| **Unboxed (`_u`)** | Maximum performance, native types | Caller knows types at compile time |
| **Boxed (`_b`)** | Runtime flexibility, type checking | Caller types unknown, or mixed-type scenarios |

**Key insight**: The boxed version becomes a thin type-checking wrapper that dispatches to the unboxed version:

```c
Item _square_b15(Item x) {
    if (get_type_id(x) == TypeId_Int)
        return box_int(_square_u15(unbox_int(x)));  // delegate to unboxed
    return make_type_error("expected int");
}
```

### Why Not Just Unboxed?

Pure unboxed functions can't handle:
- Dynamic typing: `let x = read_input()` — type unknown at compile time
- Polymorphic calls: `map(square, list)` — function passed as value
- Gradual typing: Mix of typed and untyped code in same program

The boxed version preserves Lambda's dynamic nature while allowing optimization where types are known.

### Why Not Just Boxed?

If we only generate boxed versions, we lose the optimization opportunity. Even when a caller provides known types, we'd still:
- Box arguments before the call
- Type-check inside the function
- Unbox for computation
- Rebox for return
- Unbox result at caller

With unboxed path: direct native call, native return, no overhead.

---

## When to Call Each Version

### Decision Tree at Call Sites

```
Is the function typed (has any non-any parameter)?
├── NO  → Call boxed version (only version exists)
└── YES → Analyze argument types
          ├── All args statically match declared types?
          │   └── YES → Call unboxed version directly
          ├── Any arg has unknown type (any)?
          │   └── YES → Call boxed version (runtime check)
          └── Any arg statically mismatches?
              └── YES → COMPILE ERROR (type mismatch)
```

### Examples

```lambda
fn add_ints(a: int, b: int) -> int = a + b

// Case 1: Static match → unboxed
let x: int = 5
let y: int = 10
add_ints(x, y)  // → _add_ints_u42(x, y)

// Case 2: Unknown type → boxed
let z = get_input()  // type is 'any'
add_ints(x, z)  // → _add_ints_b42(box(x), z)

// Case 3: Static mismatch → compile error
let s: string = "hello"
add_ints(x, s)  // ERROR: cannot pass string to int parameter

// Case 4: Literal inference
add_ints(1, 2)  // → _add_ints_u42(1, 2)  (literals have known types)
```

### Int-to-Float Coercion

Special case: `int` can be auto-promoted to `float`:

```lambda
fn scale(x: float, factor: float) -> float = x * factor

let n: int = 5
scale(n, 2.0)  // OK: int auto-promotes to float
               // → _scale_u(double(n), 2.0)
```

The reverse (float → int) is **not** allowed implicitly — requires explicit `int(x)`.

---

## Design Summary

| Function Type | Versions Generated | Call Site Resolution |
|--------------|-------------------|---------------------|
| All `any`/untyped params | 1 (`_name_b<line>`) | Always call boxed |
| Any typed param | 2 (`_name_b<line>` + `_name_u<line>`) | Static match → unboxed; Unknown → boxed; Mismatch → error |

### Function Naming Convention

| Version | Pattern | Example |
|---------|---------|---------|
| Boxed | `_<name>_b<line>` | `_square_b15` |
| Unboxed | `_<name>_u<line>` | `_square_u15` |

- Line number = byte offset in Lambda source (ensures uniqueness)
- Source mapping is trivial since function names encode location

---

## System Functions (Built-in C Functions)

### Basic Arithmetic: Use Native C Operators

For `+`, `-`, `*`, `/`, `//`, `%`, the **unboxed version uses native C operators directly**:

```c
// Boxed (existing): fn_add(Item a, Item b)
// Unboxed: transpiler emits native C operator
double result = a + b;   // float addition
int64_t result = a / b;  // integer division
```

### Non-trivial Functions: Add Unboxed Variants

For `fn_pow`, `fn_sqrt`, `fn_sin`, etc., add unboxed versions with double underscore:

| Version | Example |
|---------|---------|
| Boxed | `fn_pow(Item a, Item b)` |
| Unboxed | `fn__pow(double a, double b)` |

```c
// New unboxed version
double fn__pow(double a, double b) {
    return pow(a, b);
}
```

---

## Type Mapping

| Lambda Type | C Type (Unboxed) | C Type (Boxed) |
|-------------|------------------|----------------|
| `int` | `int64_t` | `Item` |
| `float` | `double` | `Item` |
| `bool` | `bool` | `Item` |
| `string` | `String*` | `Item` |
| `any` / untyped | `Item` | `Item` |

---

## Coercion Policy

| From → To | Allowed? | Action |
|-----------|----------|--------|
| `int → float` | ✓ | Auto-promote |
| `float → int` | ✗ | Error (use `int(x)`) |
| Other mismatches | ✗ | Error |

---

## Example Transpilation

```lambda
fn square(x: int) -> int = x * x  // line 15
```

Generates:

```c
// Unboxed version - native types
int64_t _square_u15(int64_t x) {
    return x * x;
}

// Boxed version - type-checking trampoline
Item _square_b15(Item x) {
    TypeId t = get_type_id(x);
    if (t == TypeId_Int) {
        return box_int(_square_u15(unbox_int(x)));
    }
    return make_type_error("square: expected int, got %s", type_name(x));
}
```

Call site resolution:
```lambda
let a: int = 5
let b = get_user_input()  // type unknown

square(a)    // → _square_u15(a)  (static match)
square(b)    // → _square_b15(b)  (runtime check)
square("x")  // → COMPILE ERROR
```

---

## Implementation Plan

### Existing Code Analysis

**Current transpiler structure** (`transpile.cpp`):
- `transpile_box_item()` — boxes primitives to `Item` based on type
- `transpile_expr()` — main expression transpilation
- `transpile_binary_expr()` — already optimizes arithmetic when both types are numeric (uses native C operators)
- `write_fn_name()` — generates function name with byte offset suffix
- Function definitions already track `AstFuncNode->param` with `TypeParam` containing type info

**Key observations**:
1. **Type tracking already exists**: `AstNode->type` is populated during AST building
2. **Boxing/unboxing helpers partially exist**: `i2it()`, `it2i()`, `push_d()`, etc.
3. **Arithmetic optimization already done**: `transpile_binary_expr()` emits native `+`, `-`, `*` when types are known
4. **Function naming uses byte offset**: `write_fn_name()` appends `ts_node_start_byte()`

### Phase 1: Dual Version Generation

**Goal**: Generate both `_b` and `_u` versions for typed functions.

**Files to modify**:
- `transpile.cpp`: `define_func()` and `forward_declare_func()`

**Implementation steps**:

1. **Add `has_typed_params()` helper**:
   ```cpp
   bool has_typed_params(AstFuncNode* fn) {
       for (AstNamedNode* p = fn->param; p; p = (AstNamedNode*)p->next) {
           TypeParam* pt = (TypeParam*)p->type;
           if (pt->declared_type && pt->declared_type->type_id != LMD_TYPE_ANY)
               return true;
       }
       return false;
   }
   ```

2. **Modify `define_func()`** to emit two versions:
   - Call existing logic with suffix `_b` for boxed version (all params as `Item`)
   - Add new logic with suffix `_u` for unboxed version (native types based on declared type)

3. **Modify boxed version** to check types and call unboxed:
   ```c
   Item _square_b15(Item x) {
       if (get_type_id(x) == TypeId_Int)
           return box_int(_square_u15(unbox_int(x)));
       // int → float promotion
       if (get_type_id(x) == TypeId_Float && declared_type == int)
           return make_type_error(...);
       return make_type_error(...);
   }
   ```

4. **Update `mir.c` func_list**:
   - Add unboxed system functions: `fn__pow`, `fn__sqrt`, `fn__sin`, etc.
   - These wrap C math library with native types

### Phase 2: Call Site Resolution

**Goal**: Choose `_b` or `_u` version based on static type analysis.

**Files to modify**:
- `transpile.cpp`: `transpile_call_expr()` (around line 1200+)

**Implementation steps**:

1. **Check if callee is a typed function**:
   - If `AstCallNode->function` resolves to `AstFuncNode` with typed params

2. **Compare argument types to parameter types**:
   ```cpp
   bool all_match = true;
   bool any_mismatch = false;
   AstNode* arg = call->argument;
   AstNamedNode* param = fn->param;
   while (arg && param) {
       Type* arg_type = arg->type;
       TypeParam* param_type = (TypeParam*)param->type;
       if (param_type->declared_type && param_type->declared_type->type_id != LMD_TYPE_ANY) {
           if (arg_type->type_id == LMD_TYPE_ANY) {
               all_match = false;  // unknown at compile time
           } else if (!types_compatible(arg_type, param_type->declared_type)) {
               any_mismatch = true;  // static type error
           }
       }
       arg = arg->next;
       param = (AstNamedNode*)param->next;
   }
   ```

3. **Emit appropriate call**:
   - `any_mismatch`: Report compile-time error
   - `all_match`: Call `_u` version directly with unboxed args
   - Otherwise: Call `_b` version with boxed args

4. **Type inference for literals**:
   - `42` → `int`, `3.14` → `float`, `"hello"` → `string`
   - Already handled in AST building

### Phase 3: Optimizations (Future)

- Inline small unboxed functions at call sites
- Eliminate redundant box/unbox pairs in expression chains
- Extend type inference through variable assignments

---

## Helper Functions

Add to `lambda.h` or new `lambda-unbox.h`:

```c
// Unboxing (already exist as it2i, it2f, etc.)
static inline int64_t unbox_int(Item x)   { return it2i(x); }
static inline double  unbox_float(Item x) { return it2f(x); }
static inline bool    unbox_bool(Item x)  { return it2b(x); }

// Boxing (already exist as i2it, push_d, etc.)
static inline Item box_int(int64_t v)   { return i2it(v); }
static inline Item box_float(double v)  { return push_d(v); }
static inline Item box_bool(bool v)     { return v ? ItemTrue : ItemFalse; }

// Type checking
static inline bool is_int(Item x)   { return get_type_id(x) == LMD_TYPE_INT; }
static inline bool is_float(Item x) { return get_type_id(x) == LMD_TYPE_FLOAT; }
```

---

## Unboxed System Functions to Add

Add to `lambda-eval-num.cpp` and register in `mir.c`:

```c
// Math functions
double fn__pow(double a, double b)  { return pow(a, b); }
double fn__sqrt(double x)           { return sqrt(x); }
double fn__sin(double x)            { return sin(x); }
double fn__cos(double x)            { return cos(x); }
double fn__tan(double x)            { return tan(x); }
double fn__log(double x)            { return log(x); }
double fn__log10(double x)          { return log10(x); }
double fn__exp(double x)            { return exp(x); }
double fn__abs_f(double x)          { return fabs(x); }
int64_t fn__abs_i(int64_t x)        { return x < 0 ? -x : x; }
double fn__floor(double x)          { return floor(x); }
double fn__ceil(double x)           { return ceil(x); }
double fn__round(double x)          { return round(x); }
```

---

## Decisions & Constraints

1. **Recursive typed functions**: No special handling. Static type match → call unboxed version (including self-recursion).

2. **Closures with typed params**: Captured variables are stored as `Item` in env struct. Unboxed version still works — just unbox captured values when accessed.

3. **Multi-param functions**: Generate single unboxed version with all native types. No partial specialization (avoids combinatorial explosion).

4. **Generation strategy**: Generate unboxed versions for ALL typed functions in current phase. Consider lazy generation later.

5. **Error handling**: Use Lambda's existing `make_type_error()` / `ItemError` infrastructure.

---

## Open Questions

1. **Union types**: `fn foo(x: int | string)` — use boxed only for now.

2. **Gradual typing**: `fn foo(x: int) -> any` — should return type affect unboxing? (Probably not for Phase 1.)

3. **Varargs**: `fn sum(values: int...)` — use boxed version, check each element at runtime.
