# Lambda First-Class Functions & Closures Proposal

## Executive Summary

This proposal outlines enhancements to Lambda's function support to enable:
1. **First-Class Functions**: Functions as data that can be assigned, passed, stored, and invoked dynamically
2. **Closures**: Functions that capture and retain lexical scope variables
3. **Additional Enhancements**: Higher-order function patterns, partial application, and function composition

---

## Current Implementation Analysis

### Function Representation

**Current State** (from `lambda.h` and `lambda-data.hpp`):

```c
typedef void* (*fn_ptr)();

struct Function {
    uint8_t type_id;     // LMD_TYPE_FUNC
    void* fn;            // fn definition (TypeFunc*)
    fn_ptr ptr;          // raw function pointer
};
```

**Key Observations**:
1. Functions are JIT-compiled to native C functions via MIR
2. `to_fn()` creates a `Function*` wrapper around a raw function pointer
3. Anonymous functions (`fn_expr`) use `to_fn(_f{offset})` pattern
4. Named functions are called directly by their transpiled name (e.g., `_square123`)

### Function Definition Types (`ast.hpp`)

```cpp
typedef struct AstFuncNode : AstNode {
    String* name;              // function name (NULL for anonymous)
    AstNamedNode *param;       // parameter list
    AstNode *body;             // function body
    NameScope *vars;           // scope for params and locals
} AstFuncNode;

typedef struct TypeFunc : Type {
    TypeParam* param;          // parameter types
    Type* returned;            // return type
    int param_count;
    int required_param_count;
    int type_index;
    bool is_anonymous;
    bool is_public;
    bool is_proc;
    bool is_variadic;
} TypeFunc;
```

### Current Transpilation (`transpile.cpp`)

**Named Function Call** (direct invocation):
```c
// Lambda: square(5)
// C: _square123(5)
```

**Anonymous Function** (via Function pointer):
```c
// Lambda: let f = (x) => x * 2
// C: Item _f = to_fn(_f456);
// Call: _f->ptr(10)  // requires "->ptr" suffix for anonymous
```

### Current Limitations

1. **No Dynamic Dispatch**: Function references are resolved at compile time
2. **No Closure Support**: Functions cannot capture outer scope variables
3. **Limited Pass-by-Reference**: Function values work, but calling convention is inconsistent
4. **No Collection Storage**: Cannot reliably store and retrieve functions from arrays/maps

---

## Part 1: First-Class Functions

### 1.1 Unified Function Value Representation

**Goal**: Functions can be assigned to variables, passed as arguments, stored in collections, and called uniformly.

#### Proposed `Function` Structure Enhancement

```c
typedef struct Function {
    uint8_t type_id;           // LMD_TYPE_FUNC
    uint16_t ref_cnt;          // reference counting
    TypeFunc* fn_type;         // type metadata (param types, return type)
    fn_ptr ptr;                // native function pointer
    void* closure_env;         // closure environment (NULL if no captures)
    String* name;              // optional: function name for debugging
} Function;
```

#### Unified Calling Convention

**Current Problem**: Anonymous functions use `fn->ptr(args)`, named functions use `_name123(args)`.

**Proposed Solution**: All function values use indirect dispatch via `fn_call()`:

```c
// Runtime function for dynamic invocation
Item fn_call(Function* fn, Item* args, int arg_count) {
    if (!fn || !fn->ptr) return ITEM_ERROR;
    
    // Closure environment is passed as first hidden argument if present
    if (fn->closure_env) {
        return ((Item(*)(void*, Item*, int))fn->ptr)(fn->closure_env, args, arg_count);
    }
    
    // Non-closure: direct call based on arity
    switch (arg_count) {
        case 0: return ((Item(*)())fn->ptr)();
        case 1: return ((Item(*)(Item))fn->ptr)(args[0]);
        case 2: return ((Item(*)(Item,Item))fn->ptr)(args[0], args[1]);
        // ... up to reasonable max (e.g., 8-10)
        default: return fn_call_varargs(fn, args, arg_count);
    }
}

// Variadic fallback for many arguments
Item fn_call_varargs(Function* fn, Item* args, int arg_count);
```

#### Transpilation Changes

**Before** (current):
```c
// Named function call: square(5)
_square123(5)

// Anonymous function call: f(5)
_f->ptr(5)
```

**After** (proposed - for function variables):
```c
// Direct call (known function at compile time): square(5)
_square123(5)  // unchanged - optimal

// Indirect call (function variable): f(5)
fn_call(_f, (Item[]){i2it(5)}, 1)

// Stored function call: fns[0](5)
fn_call((Function*)it_fn(array_get(fns, 0)), (Item[]){i2it(5)}, 1)
```

**Optimization**: When the transpiler can prove the function reference is known at compile time, use direct call. Use `fn_call()` only for truly dynamic cases.

### 1.2 Function Assignment & Variables

**Lambda Syntax** (current - works):
```lambda
let f = (x) => x * 2
let g = f                    // assign function to another variable
```

**Transpilation**:
```c
Function* _f = to_fn(_anon456);
Function* _g = _f;           // pointer assignment, shares ref
```

**Enhancement**: Add reference counting for function values:
```c
Function* fn_copy(Function* fn) {
    if (fn) fn->ref_cnt++;
    return fn;
}

void fn_release(Function* fn) {
    if (fn && --fn->ref_cnt == 0) {
        if (fn->closure_env) free_closure_env(fn->closure_env);
        free(fn);
    }
}
```

### 1.3 Functions in Collections

**Lambda Usage**:
```lambda
let ops = [(x) => x + 1, (x) => x * 2, (x) => x ^ 2]
let result = ops[1](5)       // 10
```

**Transpilation**:
```c
// Array of functions
Array* ops = array_fill(array(), 3,
    (Item)to_fn(_anon100),
    (Item)to_fn(_anon200),
    (Item)to_fn(_anon300)
);

// Dynamic call
Function* fn = (Function*)array_get(ops, 1);
Item result = fn_call(fn, (Item[]){i2it(5)}, 1);
```

**Required Changes**:
1. Add `it_fn(Item)` helper to extract `Function*` from Item
2. Update `array_get()` / `map_get()` to preserve function type info
3. Add runtime type checking in `fn_call()` for safety

### 1.4 Functions as Arguments (Higher-Order Functions)

**Lambda Usage**:
```lambda
fn map(arr: array, f: fn) => for (x in arr) f(x)
fn filter(arr: array, predicate: fn) => for (x in arr) if (predicate(x)) x

let doubled = map([1, 2, 3], (x) => x * 2)        // [2, 4, 6]
let evens = filter([1, 2, 3, 4], (x) => x % 2 == 0)  // [2, 4]
```

**Type System Enhancement** (`TypeFunc` extended):

```cpp
typedef struct TypeFunc : Type {
    // ... existing fields ...
    TypeFunc* callback_type;   // NEW: for fn-typed parameters
} TypeFunc;
```

**Transpilation**:
```c
Item _map123(Array* arr, Function* f) {
    List* ls = list();
    for (int i = 0; i < arr->length; i++) {
        Item x = array_get(arr, i);
        Item result = fn_call(f, &x, 1);
        list_push(ls, result);
    }
    return list_end(ls);
}
```

### 1.5 Functions as Return Values

**Lambda Usage**:
```lambda
fn make_adder(n: int) => (x) => x + n

let add5 = make_adder(5)
add5(10)  // 15
```

This requires closure support (covered in Part 2).

---

## Part 2: Closure Support

### 2.1 Closure Fundamentals

A **closure** captures variables from its enclosing scope at definition time.

**Lambda Example**:
```lambda
fn counter() {
    var count = 0
    (let increment = () => { count = count + 1; count })
    increment
}

let c = counter()
c()  // 1
c()  // 2
```

### 2.2 Closure Environment Structure

**Design**: Captured variables are stored in a heap-allocated environment struct.

```c
typedef struct ClosureEnv {
    uint16_t ref_cnt;
    uint16_t var_count;
    Item vars[];          // captured variables (by reference for mutables)
} ClosureEnv;

// For mutable captures, store pointers to slots
typedef struct MutableCapture {
    Item* slot;           // pointer to actual storage
} MutableCapture;
```

### 2.3 Capture Analysis (AST Phase)

**New AST Node Fields**:
```cpp
typedef struct AstFuncNode : AstNode {
    // ... existing fields ...
    ArrayList* captures;       // NEW: list of captured variable names
    bool has_mutable_captures; // NEW: any captured vars are mutated
} AstFuncNode;
```

**Analysis Algorithm** (in `build_ast.cpp`):
```cpp
void analyze_captures(Transpiler* tp, AstFuncNode* fn_node) {
    // 1. Collect all free variables referenced in body
    HashSet* free_vars = collect_free_variables(fn_node->body);
    
    // 2. Remove parameters (they're local)
    for (AstNamedNode* p = fn_node->param; p; p = (AstNamedNode*)p->next) {
        hashset_remove(free_vars, p->name);
    }
    
    // 3. Remove globals and imports
    filter_non_local_names(tp, free_vars);
    
    // 4. Remaining are captures
    fn_node->captures = hashset_to_list(free_vars);
    
    // 5. Check for mutations
    fn_node->has_mutable_captures = check_mutations(fn_node->body, fn_node->captures);
}
```

### 2.4 Closure Transpilation

**Lambda Source**:
```lambda
fn make_adder(n: int) {
    (x) => x + n
}
```

**Transpiled C**:
```c
// Closure environment type for _anon456
typedef struct Env_anon456 {
    uint16_t ref_cnt;
    int _n;              // captured 'n' (immutable, stored by value)
} Env_anon456;

// The closure function takes env as first param
Item _anon456(Env_anon456* env, Item _x) {
    return i2it(it2i(_x) + env->_n);
}

// Factory function
Function* _make_adder123(int _n) {
    // Allocate closure environment
    Env_anon456* env = (Env_anon456*)malloc(sizeof(Env_anon456));
    env->ref_cnt = 1;
    env->_n = _n;
    
    // Create Function with closure
    Function* fn = (Function*)calloc(1, sizeof(Function));
    fn->type_id = LMD_TYPE_FUNC;
    fn->ptr = (fn_ptr)_anon456;
    fn->closure_env = env;
    return fn;
}
```

### 2.5 Mutable Captures

For variables that are modified after capture:

**Lambda Source**:
```lambda
fn counter() {
    var count = 0
    () => { count = count + 1; count }
}
```

**Transpiled C** (using indirection):
```c
typedef struct Env_anon789 {
    uint16_t ref_cnt;
    Item* _count;        // mutable capture - pointer to shared slot
} Env_anon789;

// Shared mutable slot
typedef struct MutableSlot {
    uint16_t ref_cnt;
    Item value;
} MutableSlot;

Item _anon789(Env_anon789* env) {
    Item old = *env->_count;
    *env->_count = i2it(it2i(old) + 1);
    return *env->_count;
}

Function* _counter123() {
    // Allocate mutable slot for 'count'
    MutableSlot* slot = (MutableSlot*)malloc(sizeof(MutableSlot));
    slot->ref_cnt = 1;
    slot->value = i2it(0);
    
    // Allocate closure environment
    Env_anon789* env = (Env_anon789*)malloc(sizeof(Env_anon789));
    env->ref_cnt = 1;
    env->_count = &slot->value;
    
    Function* fn = (Function*)calloc(1, sizeof(Function));
    fn->type_id = LMD_TYPE_FUNC;
    fn->ptr = (fn_ptr)_anon789;
    fn->closure_env = env;
    return fn;
}
```

### 2.6 Memory Management for Closures

**Reference Counting**:
- `ClosureEnv` has `ref_cnt`
- `MutableSlot` has separate `ref_cnt` (can be shared by multiple closures)
- `Function->closure_env` increment on copy, decrement on release

```c
void free_closure_env(void* env_ptr, ClosureEnvMeta* meta) {
    ClosureEnv* env = (ClosureEnv*)env_ptr;
    if (--env->ref_cnt == 0) {
        // Release mutable slots
        for (int i = 0; i < meta->mutable_count; i++) {
            MutableSlot* slot = meta->mutable_slots[i];
            if (--slot->ref_cnt == 0) free(slot);
        }
        free(env);
    }
}
```

---

## Part 3: Implementation Roadmap

### Phase 1: First-Class Functions (No Closures)

**Priority**: High  
**Status**: ✅ COMPLETE

| Task | Files | Description |
|------|-------|-------------|
| 1.1 | `lambda.h`, `lambda-data.hpp` | Enhanced `Function` struct with ref counting |
| 1.2 | `lambda-eval.cpp` | Implement `fn_call()` dynamic dispatch |
| 1.3 | `transpile.cpp` | Update call site transpilation for function variables |
| 1.4 | `mir.c` | Register `fn_call` in MIR import resolver |
| 1.5 | `test/lambda/` | First-class function test cases |

**Acceptance Criteria**: ✅ All passing
```lambda
// All these should work
let f = (x) => x * 2
let g = f
g(5)  // 10

let ops = [(x) => x + 1, (x) => x - 1]
ops[0](10)  // 11

fn apply(f, x) => f(x)
apply((n) => n ^ 2, 5)  // 25
```

### Phase 2: Basic Closures (Immutable Captures)

**Priority**: Medium  
**Status**: ✅ COMPLETE (32 tests passing)

| Task | Files | Description |
|------|-------|-------------|
| 2.1 | `build_ast.cpp` | Capture analysis during AST building |
| 2.2 | `ast.hpp` | Add capture fields to `AstFuncNode` |
| 2.3 | `transpile.cpp` | Generate closure environment structs |
| 2.4 | `transpile.cpp` | Modify closure function signatures |
| 2.5 | `lambda-mem.cpp` | Closure memory allocation helpers |
| 2.6 | `test/lambda/` | Closure test cases (13 tests in `closure_advanced.ls`) |

**Acceptance Criteria**: ✅ All passing
```lambda
fn make_adder(n) => (x) => x + n
let add10 = make_adder(10)
add10(5)  // 15

fn make_multiplier(factor) {
    (x) => x * factor
}
let triple = make_multiplier(3)
triple(7)  // 21
```

**Additional Tests Passing** (added 2026-01-24):
```lambda
// Typed parameter capture
fn make_adder(base: int) { fn adder(y) => base + y; adder }
make_adder(10)(5)  // 15

// Conditional in closure
fn make_abs(threshold: int) { fn f(x) => if (x < threshold) -x else x; f }
make_abs(0)(-5)  // 5

// Let variable capture  
fn make_counter(start: int) { let count = start * 2; fn f(step) => count + step; f }
make_counter(5)(3)  // 13
```

### Phase 3: Mutable Closures

**Priority**: Medium-Low  
**Status**: 🔲 NOT STARTED

| Task | Files | Description |
|------|-------|-------------|
| 3.1 | `build_ast.cpp` | Detect mutable captures (assignments to captured vars) |
| 3.2 | `transpile.cpp` | Generate `MutableSlot` and indirection code |
| 3.3 | `lambda-mem.cpp` | `MutableSlot` allocation and ref counting |
| 3.4 | `test/lambda/` | Mutable closure tests |

**Acceptance Criteria**:
```lambda
fn counter() {
    var n = 0
    () => { n = n + 1; n }
}
let c = counter()
c()  // 1
c()  // 2
c()  // 3
```

### Phase 4: Optimizations & Refinements

**Priority**: Low  
**Status**: 🔲 NOT STARTED

| Task | Description |
|------|-------------|
| 4.1 | Inline small closures when possible |
| 4.2 | Escape analysis to stack-allocate short-lived closures |
| 4.3 | Optimize `fn_call()` dispatch with type specialization |
| 4.4 | Add closure debugging/introspection support |

---

## Part 4: Closure Implementation Details

This section documents key implementation details discovered during Phase 2 development.

### 4.1 Closure Type Requirements

**Critical Rule**: Closures MUST use `Item` type for all parameters and return values.

This is required because `fn_call()` dispatches dynamically and passes `Item` arguments:
```c
// fn_call() signature - all args are Item
Item fn_call1(Function* fn, Item a);
Item fn_call2(Function* fn, Item a, Item b);
```

**Transpilation for Closures**:
```c
// Regular function: uses native types
int _square(int _x) { return _x * _x; }

// Closure: uses Item types for fn_call compatibility  
Item _adder(Env_adder* _env, Item _y) {
    return fn_add(_env->base, _y);  // both are Items
}
```

### 4.2 Key Helper Functions in `transpile.cpp`

| Function | Purpose |
|----------|---------|
| `is_closure_param_ref(tp, item)` | Check if identifier is a parameter of the CURRENT closure |
| `is_captured_var_ref(tp, item)` | Check if identifier is a captured variable (in closure env) |
| `emit_captured_var_item(tp, item)` | Emit `_env->varname` for captured variable access |
| `transpile_box_capture(tp, cap, from_outer)` | Box a captured variable when building closure env |

### 4.3 Distinguishing Closure Params vs Captured Vars

**Problem Solved**: When transpiling `base + y` inside a closure where `base` is captured from outer scope and `y` is the closure's own parameter, the transpiler must handle them differently:

- `y` (closure param): Already an `Item` at runtime, use directly
- `base` (captured var): Access via `_env->base`, already an `Item`

**Key Fix**: `is_closure_param_ref()` must verify the parameter belongs to the CURRENT closure's parameter list, not just any outer function's parameter:

```cpp
bool is_closure_param_ref(Transpiler* tp, AstNode* item) {
    if (!tp->current_closure) return false;
    // ... unwrap PRIMARY nodes to get IDENT ...
    AstNamedNode* param = (AstNamedNode*)ident_node->entry->node;
    // Check if this param is in CURRENT closure's param list
    AstNamedNode* closure_param = tp->current_closure->param;
    while (closure_param) {
        if (closure_param == param) return true;
        closure_param = (AstNamedNode*)closure_param->next;
    }
    return false;  // This is a param from outer function (captured)
}
```

### 4.4 Boxing for Runtime Functions

When closure body calls runtime functions like `fn_add()`, captured variables are already `Item` type in the env, so they should NOT be unboxed:

```c
// WRONG: fn_add(it2i(_env->base), _y)  -- unboxes captured var
// RIGHT: fn_add(_env->base, _y)        -- passes Item directly
```

The `transpile_box_item()` function checks `is_captured_var_ref()` and emits `_env->varname` directly without boxing/unboxing.

### 4.5 Closure Struct Pre-definition

Closure environment structs must be defined BEFORE any function that references them. The transpiler runs `pre_define_closure_envs()` to emit all struct definitions first:

```c
// Pre-defined before functions
typedef struct Env_adder { Item base; } Env_adder;

// Function can now reference the struct
Item _adder(Env_adder* _env, Item _y) { ... }
```

### 4.6 Closure Context Tracking

The transpiler maintains `current_closure` pointer during body transpilation:

```cpp
AstFuncNode* prev_closure = tp->current_closure;
if (is_closure) {
    tp->current_closure = fn_node;
}
// ... transpile body ...
tp->current_closure = prev_closure;  // restore
```

This allows `is_closure_param_ref()` and `is_captured_var_ref()` to know which closure context they're operating in.

---

## Part 5: Additional Function Enhancements (Future)

### 5.1 Partial Application

**Syntax Option A** (explicit):
```lambda
fn add(a, b) => a + b
let add5 = add(5, _)      // partial application, _ is placeholder
add5(3)  // 8
```

**Syntax Option B** (auto-curry):
```lambda
fn add(a, b) => a + b
let add5 = add(5)         // automatically curried
add5(3)  // 8
```

**Implementation**: Partial application creates a closure capturing provided arguments.

### 5.2 Function Composition

**Built-in Operators**:
```lambda
let f = (x) => x + 1
let g = (x) => x * 2
let h = f >> g            // compose: h(x) = g(f(x))
h(5)  // 12

let h2 = g << f           // reverse compose: h2(x) = f(g(x))
h2(5)  // 11
```

**Implementation**: `>>` and `<<` create new closure combining both functions.

### 5.3 Method Binding

For element/map method-like calls:
```lambda
let obj = {
    value: 10,
    double: (self) => self.value * 2
}
let method = obj.double
method(obj)  // 20

// Or with auto-binding:
let bound = obj::double   // binds 'obj' as first arg
bound()  // 20
```

### 5.4 Recursive Anonymous Functions

**Problem**: Anonymous functions cannot easily refer to themselves.

**Solution**: Special `self` or `this` binding:
```lambda
let factorial = (n) => if (n <= 1) 1 else n * self(n - 1)
factorial(5)  // 120
```

**Alternative** (Y-combinator style - no language change needed):
```lambda
let factorial = (
    let fix = (f) => (x) => f(fix(f))(x)
    fix((self) => (n) => if (n <= 1) 1 else n * self(n - 1))
)
```

---

## Part 6: Type System Considerations

### 6.1 Function Type Syntax (Existing)

Lambda already supports function types:
```lambda
type Predicate = (int) => bool
type Mapper = (a: any) => any
type Reducer = (acc: int, x: int) => int
```

### 6.2 Generic Function Types (Future)

```lambda
type Mapper<T, U> = (T) => U
type Predicate<T> = (T) => bool

fn map<T, U>(arr: T*, f: Mapper<T, U>) => ...
```

### 6.3 Closure Type Inference

The type system should infer captured variable types:
```lambda
fn make_adder(n: int) {
    // Inferred: return type is (int) => int
    (x: int) => x + n
}
```

---

## Part 7: MIR/JIT Considerations

### 7.1 Current MIR Integration

Lambda functions are transpiled to C code, then compiled via C2MIR JIT:
1. `transpile.cpp` generates C code string
2. `mir.c` compiles C → MIR
3. MIR generates native code
4. Function pointers are registered in `func_list[]`

### 7.2 Closure Handling in MIR

**Challenge**: Closures need runtime-generated environment structs.

**Solution**: 
- Generate unique struct types for each closure at transpile time
- Environment structs are plain C structs - MIR handles them normally
- Closure allocation happens at runtime via standard `malloc`

### 7.3 Dynamic Dispatch Performance

**Concern**: `fn_call()` adds indirection overhead.

**Mitigations**:
1. Direct calls when function is known at compile time (current behavior)
2. Monomorphization for hot paths (future optimization)
3. Inline caching for repeated dynamic calls (advanced)

---

## Summary

### Key Changes Required

| Component | Changes |
|-----------|---------|
| `lambda.h` | Enhanced `Function` struct, `fn_call()` declaration |
| `lambda-data.hpp` | `ClosureEnv`, `MutableSlot` types |
| `ast.hpp` | Capture fields in `AstFuncNode` |
| `build_ast.cpp` | Capture analysis pass |
| `transpile.cpp` | Closure transpilation, `fn_call()` usage |
| `lambda-eval.cpp` | `fn_call()` implementation |
| `mir.c` | Register new runtime functions |
| `lambda-mem.cpp` | Closure memory management |

### Backward Compatibility

- All existing function definitions continue to work
- Direct calls to known functions are unchanged
- New features are additive

### Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Performance overhead from indirection | Use direct calls when possible; optimize hot paths |
| Memory leaks from closures | Strict reference counting; cycle detection (future) |
| Complex closure debugging | Add closure introspection; clear error messages |
| MIR limitations | Generate standard C; no exotic constructs |

---

## References

- [Lambda_Function_Param.md](Lambda_Function_Param.md) - Current function parameter handling
- [Lambda_Proc.md](Lambda_Proc.md) - Procedural function support
- `lambda/transpile.cpp` - Current transpilation logic
- `lambda/mir.c` - MIR JIT integration
- `lambda/lambda-eval.cpp` - Runtime function implementations
