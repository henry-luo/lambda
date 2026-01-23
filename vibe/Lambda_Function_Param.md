# Lambda Function Parameter Handling Proposal

## Overview

This document analyzes Lambda's current function call parameter handling and proposes improvements for:
1. Parameter count mismatch handling
2. Parameter type matching and type errors
3. Boxing/unboxing between Item and primitive types
4. Return type matching
5. Structured test coverage

---

## Current Implementation Analysis

### 1. AST Building Phase (`build_ast.cpp`)

#### Function Definition (`build_func()`)
- Located at lines 1950-2013
- Builds function AST with `TypeFunc` containing:
  - `param_count`: number of declared parameters
  - `param`: linked list of `TypeParam` for each parameter
  - `returned`: return type (inferred from body if not declared)
  - `is_anonymous`, `is_proc`, `is_public` flags

#### Function Call (`build_call_expr()`)
- Located at lines 193-280
- Counts arguments via Tree-sitter cursor traversal
- **Current behavior**:
  - For system functions: looks up `get_sys_func_info()` with `arg_count`
  - For user functions: no validation of argument count against `param_count`
  - Simply builds argument AST nodes linked via `->next`

**Gap identified**: No validation of argument count vs parameter count during AST building.

### 2. Transpilation Phase (`transpile.cpp`)

#### Function Call Transpilation (`transpile_call_expr()`)
- Located at lines 1175-1310
- Iterates through arguments and parameters in parallel
- **Current boxing/unboxing logic**:
  - System functions: special handling for DateTime boxing
  - User functions with typed parameters:
    - Same type: pass directly
    - `float` param with `int/int64/float` arg: pass directly
    - `float` param with `ANY` arg: wrap with `it2d()`
    - `int64` param with `int/int64` arg: pass directly
    - `int64` param with `float` arg: cast `((int64_t)...)`
    - `int64` param with `ANY` arg: wrap with `it2l()`
    - `int` param: similar logic
    - All other types: call `transpile_box_item()`

**Gaps identified**:
1. No handling when `arg_count < param_count` (missing arguments)
2. No handling when `arg_count > param_count` (extra arguments)
3. Type mismatch logs error but doesn't stop transpilation
4. Return type not validated against function body

#### Function Definition Transpilation (`define_func()`)
- Located at lines 1438-1468
- Uses function body type as return type
- **Current behavior**: No return type validation

### 3. Boxing/Unboxing Functions (`lambda.h`, `lambda-data.cpp`)

| Function | Purpose | Location |
|----------|---------|----------|
| `i2it(int)` | int → Item | lambda.h:212-214 |
| `l2it(int64_t*)` | int64 pointer → Item | lambda.h:216 |
| `d2it(double*)` | double pointer → Item | lambda.h:217 |
| `b2it(bool)` | bool → Item | lambda.h:207 |
| `k2it(ptr)` | datetime pointer → Item | lambda.h:217 |
| `it2i(Item)` | Item → int | lambda-data.cpp:219 |
| `it2l(Item)` | Item → int64 | lambda-data.cpp:237 |
| `it2d(Item)` | Item → double | lambda-data.cpp:170 |

---

## Proposed Changes

### 1. Parameter Count Mismatch Handling

#### 1.1 Missing Arguments (arg_count < param_count)

**Location**: `transpile.cpp:transpile_call_expr()`

**Current behavior**: Silently generates incorrect C code

**Proposed behavior**: 
- Fill missing arguments with `null` (ItemNull)
- Add debug log for development visibility

**Implementation**:
```cpp
// After transpiling provided arguments, fill missing with null
while (param_type) {
    if (arg) {
        strbuf_append_char(tp->code_buf, ',');
    } else {
        // First missing param, add comma if we had any args
        if (call_node->argument) {
            strbuf_append_char(tp->code_buf, ',');
        }
    }
    log_debug("param_mismatch: filling missing argument with null for param type %d", 
        param_type->type_id);
    strbuf_append_str(tp->code_buf, "ITEM_NULL");
    param_type = param_type->next;
}
```

#### 1.2 Extra Arguments (arg_count > param_count)

**Location**: `transpile.cpp:transpile_call_expr()`

**Current behavior**: Generates all arguments (causes undefined behavior)

**Proposed behavior**:
- Log warning at transpile time
- Discard extra arguments

**Implementation**:
```cpp
// Track argument position
int arg_index = 0;
int expected_count = fn_type ? fn_type->param_count : -1;

while (arg) {
    if (expected_count >= 0 && arg_index >= expected_count) {
        log_warning("param_mismatch: discarding extra argument %d (function expects %d params)",
            arg_index + 1, expected_count);
        arg = arg->next;
        arg_index++;
        continue;  // skip transpiling this argument
    }
    // ... existing transpilation logic ...
    arg_index++;
}
```

### 2. Parameter Type Matching

#### 2.1 Compile-Time Type Error Detection

**Location**: `build_ast.cpp:build_call_expr()`

**Proposed behavior**: Validate argument types against parameter types during AST building

**Implementation**:
**Error Accumulation Strategy**: Instead of stopping on the first type error, record errors and continue transpiling until error count exceeds a threshold (e.g., 10 errors). This provides better developer experience by showing multiple issues at once.

**Implementation**:
```cpp
// In Transpiler struct (transpiler.hpp), add error tracking:
struct Transpiler : Script {
    // ... existing fields ...
    int error_count;           // accumulated error count
    int max_errors;            // threshold (default: 10)
    ArrayList* error_list;     // list of error messages for reporting
};

// Initialize in transpiler setup:
tp->error_count = 0;
tp->max_errors = 10;  // configurable threshold
tp->error_list = arraylist_create(sizeof(char*));

// Helper function to record type error:
void record_type_error(Transpiler* tp, const char* format, ...) {
    tp->error_count++;
    
    // Format and store error message
    va_list args;
    va_start(args, format);
    char* error_msg = (char*)pool_alloc(tp->pool, 256);
    vsnprintf(error_msg, 256, format, args);
    va_end(args);
    arraylist_append(tp->error_list, &error_msg);
    
    log_error("%s", error_msg);
    
    // Check threshold
    if (tp->error_count >= tp->max_errors) {
        log_error("error_threshold: max errors (%d) reached, stopping transpilation", 
            tp->max_errors);
    }
}

// Check if should continue transpiling:
bool should_continue_transpiling(Transpiler* tp) {
    return tp->error_count < tp->max_errors;
}
```

**Type validation in build_call_expr()**:
```cpp
// After building argument, validate against corresponding param type
TypeFunc* func_type = (TypeFunc*)ast_node->function->type;
TypeParam* expected_param = func_type ? func_type->param : NULL;
AstNode* arg = ast_node->argument;
int arg_index = 0;

while (arg && expected_param) {
    if (!types_compatible(arg->type, (Type*)expected_param)) {
        record_type_error(tp, "type_error: argument %d has type %d, expected %d at line %d",
            arg_index + 1, arg->type->type_id, expected_param->type_id,
            ts_node_start_point(call_node).row + 1);
        // Continue processing remaining arguments instead of returning immediately
        if (!should_continue_transpiling(tp)) {
            ast_node->type = &TYPE_ERROR;
            return (AstNode*)ast_node;
        }
    }
    arg = arg->next;
    expected_param = expected_param->next;
    arg_index++;
}

// Mark as error type if any errors were recorded for this call
if (tp->error_count > 0 && ast_node->type != &TYPE_ERROR) {
    // Keep original type for continued transpilation, errors are logged
}
```

**New helper function**:
```cpp
// Check if arg_type is compatible with param_type
bool types_compatible(Type* arg_type, Type* param_type) {
    if (!arg_type || !param_type) return true;  // unknown types are compatible
    if (param_type->type_id == LMD_TYPE_ANY) return true;  // any accepts all
    if (arg_type->type_id == param_type->type_id) return true;
    
    // Numeric coercion: int → int64, int → float, int64 → float
    if (param_type->type_id == LMD_TYPE_FLOAT) {
        if (arg_type->type_id == LMD_TYPE_INT || 
            arg_type->type_id == LMD_TYPE_INT64) return true;
    }
    if (param_type->type_id == LMD_TYPE_INT64) {
        if (arg_type->type_id == LMD_TYPE_INT) return true;
    }
    if (param_type->type_id == LMD_TYPE_NUMBER) {
        if (arg_type->type_id == LMD_TYPE_INT || 
            arg_type->type_id == LMD_TYPE_INT64 ||
            arg_type->type_id == LMD_TYPE_FLOAT) return true;
    }
    
    return false;
}
```

#### 2.2 Enhanced Boxing/Unboxing

**Location**: `transpile.cpp:transpile_call_expr()`

Improve the existing boxing logic to handle more cases:

**Current gaps**:
- `bool` param with `int` arg: not handled
- `string` param with `symbol` arg: not handled
- Container types: insufficient validation

**Proposed additions**:
```cpp
else if (param_type->type_id == LMD_TYPE_BOOL) {
    if (arg->type->type_id == LMD_TYPE_BOOL) {
        transpile_expr(tp, arg);
    }
    else if (arg->type->type_id == LMD_TYPE_INT) {
        strbuf_append_str(tp->code_buf, "((");
        transpile_expr(tp, arg);
        strbuf_append_str(tp->code_buf, ") != 0)");
    }
    else if (arg->type->type_id == LMD_TYPE_ANY) {
        strbuf_append_str(tp->code_buf, "it2b(");
        transpile_expr(tp, arg);
        strbuf_append_char(tp->code_buf, ')');
    }
    else {
        log_error("type_error: incompatible argument type %d for bool parameter",
            arg->type->type_id);
        strbuf_append_str(tp->code_buf, "false");
    }
}
```

### 3. Return Type Matching

#### 3.1 Return Type Validation

**Location**: `build_ast.cpp:build_func()`

**Current behavior**: Infers return type from body if not declared

**Proposed enhancement**: Validate declared return type against body type

**Implementation** (add after line 2009):
```cpp
// Validate return type if explicitly declared
if (fn_type->returned && ast_node->body->type) {
    if (!types_compatible(ast_node->body->type, fn_type->returned)) {
        log_error("return_type_error: function '%.*s' body returns type %d, declared %d",
            (int)ast_node->name->len, ast_node->name->chars,
            ast_node->body->type->type_id, fn_type->returned->type_id);
        // Keep the declared return type for interface consistency
    }
}
```

#### 3.2 Return Value Boxing

**Location**: `transpile.cpp:define_func()`

**Current issue**: Return value may need boxing if function signature expects Item

**Proposed enhancement**:
```cpp
// Before: strbuf_append_str(tp->code_buf, ";\n}\n");
// After:
TypeFunc* fn_type = (TypeFunc*)fn_node->type;
if (fn_type->returned && fn_type->returned->type_id == LMD_TYPE_ANY) {
    // Function returns Item, body may return primitive
    strbuf_append_str(tp->code_buf, " return ");
    transpile_box_item(tp, fn_node->body);
    strbuf_append_str(tp->code_buf, ";\n}\n");
} else {
    strbuf_append_str(tp->code_buf, " return ");
    transpile_expr(tp, fn_node->body);
    strbuf_append_str(tp->code_buf, ";\n}\n");
}
```

---

## Files to Modify

| File | Changes |
|------|---------|
| `lambda/transpiler.hpp` | Add `error_count`, `max_errors`, `error_list` fields to Transpiler struct |
| `lambda/build_ast.cpp` | Add `types_compatible()`, `record_type_error()`, `should_continue_transpiling()`, validate arg types in `build_call_expr()`, validate return type in `build_func()` |
| `lambda/transpile.cpp` | Update `transpile_call_expr()` for param count handling, enhance boxing logic in `transpile_box_item()`, update `define_func()` for return boxing |
| `lambda/lambda-data.hpp` | Add `it2b()` declaration if needed |
| `lambda/lambda-data.cpp` | Implement `it2b()` if needed |

## Open Questions

1. ~~Should type errors be hard errors or warnings with fallback?~~ **Resolved**: Record errors and continue, stop at threshold (default: 10)
2. Should extra arguments be silently discarded or always warn? Should always warn.
3. Should MIR transpiler (`transpile-mir.cpp`) also be updated? (Currently doesn't handle function calls) We'll update that part later.
4. Should the error threshold be configurable via CLI flag (e.g., `--max-errors=20`)? Yes.

---

## Implementation Status (Completed: January 22, 2026)

### Summary of Changes

| Feature | Status | Files Modified |
|---------|--------|----------------|
| Missing arguments filled with null | ✅ Complete | `transpile.cpp` |
| Extra arguments discarded with warning | ✅ Complete | `transpile.cpp` |
| Parameter type validation | ✅ Complete | `build_ast.cpp` |
| Error accumulation with threshold | ✅ Complete | `ast.hpp`, `build_ast.cpp`, `runner.cpp` |
| Return type validation | ✅ Complete | `build_ast.cpp` |
| Null comparison enhancement | ✅ Complete | `lambda-eval.cpp` |
| Lambda test scripts | ✅ Complete | `test/lambda/func_param.ls`, `test/lambda/negative/func_param_negative.ls` |

### Detailed Changes

1. **ast.hpp**: Added `error_count`, `max_errors` fields to Transpiler struct; added `types_compatible()` declaration

2. **build_ast.cpp**: 
   - Added `types_compatible()` function for type compatibility checking
   - Added `record_type_error()` function for error logging with accumulation
   - Added `should_continue_transpiling()` to check threshold
   - Modified `build_call_expr()` to validate argument types against parameter types
   - Modified `build_func()` to validate return type against body type

3. **transpile.cpp**: Modified `transpile_call_expr()` to:
   - Track argument index during transpilation
   - Discard extra arguments with warning
   - Fill missing arguments with `ITEM_NULL`

4. **runner.cpp**: Initialize `error_count = 0`, `max_errors = 10` when creating Transpiler

5. **lambda-eval.cpp**: Modified `fn_eq()` to allow null comparisons with any type:
   - `null == x` returns `false` (not error) when x is non-null
   - `x != null` returns `true` (not error) when x is non-null
   - This enables idiomatic null checking patterns like `if (x == null) ...`

6. **test/lambda/comp_expr_edge.ls** and **.txt**: Updated expected results to reflect new null comparison semantics

### Test Results

All 73 baseline tests pass. The feature is fully functional with:
- Missing arguments correctly filled with null
- Extra arguments discarded with warnings
- Type coercion working (int→float)
- Null comparison working for optional parameter patterns
- Error accumulation stopping at 10 errors

---

# Phase 2: Advanced Parameter Features (Proposed: January 23, 2026)

## Overview

This phase extends Lambda's function parameter handling with four major features:
1. **Optional/Required Parameters**: Type-based optionality with `?` modifier (TypeScript-style)
2. **Default Parameter Values**: Compile-time default value expressions with typed param support
3. **Named Arguments**: Order-independent argument passing with parameter names
4. **Variadic Parameters**: Variable argument lists with `...` and `varg()` accessor

---

## TypeScript Compatibility Note

Lambda's optional parameter syntax follows TypeScript's semantics:

| Lambda Syntax | TypeScript Equivalent | Meaning |
|---------------|----------------------|---------|
| `fn f(a?: T)` | `function f(a?: T)` | Parameter `a` is **optional**, caller can omit it. Inside function, `a` is `T \| null` |
| `fn f(a: T?)` | `function f(a: T \| null)` | Type `T` is **nullable**, but parameter is **required**. Caller must pass something (even `null`) |

**Example**:
```lambda
// Optional parameter - can call f() or f(5)
fn f(a?: int) { a ?? 0 }
f()         // OK, a is null
f(5)        // OK, a is 5

// Nullable type - must call g(val), even g(null)
fn g(a: int?) { a ?? 0 }
g()         // ERROR: missing required parameter
g(null)     // OK, a is null
g(5)        // OK, a is 5
```

---

## 1. Optional and Required Parameters

### Semantics

| Declaration | Optionality | Behavior |
|-------------|-------------|----------|
| `fn f(a, b)` | All optional | Untyped params default to `null` if omitted |
| `fn f(a: T, b: T)` | All required | Type annotation implies required |
| `fn f(a?: T)` | Optional | `?` before `:` makes typed param optional |
| `fn f(a: T?)` | Required | Nullable type, but param must be provided |
| `fn f(a: int = 5)` | Optional | Type + default value = optional |

### Constraint
**Optional parameters must come after all required parameters.**

```lambda
fn valid(req: int, opt?: int) { ... }           // OK
fn valid2(req: int, opt: int = 10) { ... }      // OK
fn invalid(opt?: int, req: int) { ... }         // Error
```

---

## 2. Default Parameter Values

### Syntax

```lambda
fn greet(name = "World") { "Hello, " ++ name ++ "!" }
fn add(a: int, b: int = 0) { a + b }

greet()                     // "Hello, World!"
greet("Lambda")             // "Hello, Lambda!"
add(5)                      // 5
add(5, 3)                   // 8
```

### Constraints
1. **Default expressions evaluated at call site** (not definition site)
2. **Defaults can reference earlier parameters**: `fn make_rect(w: int, h = w)`
3. **Order constraint**: Parameters with defaults must come after required parameters

---

## 3. Variadic Parameters (Rest Parameters)

### Syntax

```lambda
fn sum(...) {
    let total = 0
    for (x in varg()) { total = total + x }
    total
}

sum(1, 2, 3)        // 6
sum()               // 0

fn printf(fmt, ...) {
    format(fmt, varg())
}
```

### System Function: `varg()`

| Call | Return |
|------|--------|
| `varg()` | List of all variadic arguments |
| `varg(n)` | nth variadic argument |

**Note**: `arg()` for full argument introspection is reserved for future.

---

## 4. Named Arguments

### Syntax

```lambda
fn create_rect(x: int, y: int, width: int, height: int) {
    { x: x, y: y, width: width, height: height }
}

// Named call - order independent
create_rect(x: 10, y: 20, width: 100, height: 50)
create_rect(width: 100, height: 50, x: 10, y: 20)  // Same result

// Mixed (positional must come first)
create_rect(10, 20, width: 100, height: 50)
```

### Skipping Optional Parameters

```lambda
fn connect(host: string, port: int = 80, timeout: int = 30, ssl: bool = false) { ... }

// Skip port and timeout, set only ssl
connect("example.com", ssl: true)
```

---

## 5. Combined Features

All features work together:

```lambda
fn flexible(required: int, opt?: int, default_val: int = 10, ...) {
    let base = required + (opt ?? 0) + default_val
    let extra = 0
    for (x in varg()) { extra = extra + x }
    base + extra
}

flexible(1)                             // 1 + 0 + 10 + 0 = 11
flexible(required: 1, default_val: 20)  // 1 + 0 + 20 + 0 = 21
```

### Parameter Order Constraint

```
required_typed → optional_typed (?) → defaults → variadic
```

---

## Future Features

### 1. Full Argument Introspection: `arg()` (Future)

```lambda
fn debug_call(...) {
    let all_args = arg()     // Get all arguments as list
    let first = arg(0)       // First argument by index
    let by_name = arg('x')   // Argument by parameter name (if named)
    ...
}
```

> Reserved for future implementation when full argument introspection is needed.

### 2. Typed Variadics (Future)

```lambda
fn sum_ints(...: int) {  // All varargs must be int
    let total = 0
    for (x in varg()) { total = total + x }
    total
}
```

### 3. Spread Operator (Future)

```lambda
fn inner(...) { varg().len }
fn outer(...) { inner(varg()...) }  // Spread variadic args
outer(1, 2, 3)              // 3
```

### 4. Parameter Destructuring (Future)

```lambda
fn process_point({x, y}: Point) {
    x + y
}

fn first_two([a, b, ...rest]) {
    [a, b]
}
```

## Open Questions

1. ~~Should defaults support calling other functions?~~ **Resolved**: Yes, defaults are full expressions
2. ~~Should defaults be evaluated lazily?~~ **Resolved**: Yes, lazy evaluation at call site
3. ~~For named arguments, should duplicate names be a compile error?~~ **Resolved**: Yes, compile error
4. ~~Can named arguments be mixed with variadic?~~ **Resolved**: Yes

---

## Phase 2 Implementation Status (Completed: January 23, 2026)

### Summary

| Feature | Status | Test Coverage |
|---------|--------|---------------|
| Optional params (`a?`) | ✅ Complete | `func_param2.ls` test2 |
| Default values (`a = 10`) | ✅ Complete | `func_param2.ls` test1, test3, test8, test9 |
| Typed defaults (`a: int = 10`) | ✅ Complete | `func_param2.ls` test10 |
| Typed optional (`a?: int`) | ✅ Complete | `func_param2.ls` test11 |
| Named arguments | ✅ Complete | `func_param2.ls` test5, test6, test7 |
| Variadic (`...`) | ✅ Complete | `func_param2.ls` test12-15 |

### Key Implementation Details

**Grammar** (`tree-sitter-lambda/grammar.js`):
- `parameter` rule extended: `name` + optional `?` + optional `: type` + optional `= default`
- Added `named_argument` rule: `name: value`

**AST** (`lambda-data.hpp`):
```cpp
typedef struct TypeParam : Type {
    struct TypeParam* next;
    bool is_optional;           // true if param has ? or default
    struct AstNode* default_value;
} TypeParam;

typedef struct TypeFunc : Type {
    // ...existing fields...
    int required_param_count;   // count of non-optional params
    bool is_variadic;
} TypeFunc;
```

**Build Phase** (`build_ast.cpp`):
- `build_param_expr()`: Parses `?`, type annotation, and default value; sets `is_optional`
- `build_func()`: Validates param ordering (required before optional), counts required params
- `build_named_argument()`: Creates `AST_NODE_NAMED_ARG` for `name: value` syntax

**Transpilation** (`transpile.cpp`):
- `define_func()`: Optional typed params use `Item` C type (not primitive) to accept null; variadic functions receive hidden `List* _vargs` parameter
- `transpile_call_expr()`: Reorders named args to match param order; fills missing with default or `ITEM_NULL`; builds inline list for variadic args
- `is_optional_param_ref()`: Detects optional param references to skip redundant boxing

**Variadic Runtime** (`lambda-eval.cpp`, `lambda.h`):
- Thread-local `current_vargs` stores variadic args for current function call
- `set_vargs(List*)`: Sets the thread-local vargs pointer (called at function entry)
- `fn_varg0()`: Returns all variadic args as a List (empty list if none)
- `fn_varg1(Item index)`: Returns nth variadic arg by index
- Variadic args passed as inline stack-allocated list: `({Item _va[]={...}; List _vl={...}; &_vl;})`
- C2MIR uses `null` (not `NULL`) for null pointer constants

### Typed Optional Parameter Behavior

When a **typed optional parameter** is omitted, it receives `null` (not type default like `0`):

```lambda
fn greet(name: string, age?: int) {
    if (age) name ++ " is " ++ age
    else name ++ " (age unknown)"
}

greet("Alice", 30)   // "Alice is 30"
greet("Bob")         // "Bob (age unknown)" - age is null
greet("Charlie", 0)  // "Charlie is 0" - 0 is truthy in Lambda
```

**Implementation**: Optional params are transpiled as `Item` type in C, allowing null. The `is_optional_param_ref()` helper prevents double-boxing when the param is used.

### Test File

`test/lambda/func_param2.ls` - 15 test cases covering all implemented features:
- Tests 1-11: Optional params, defaults, typed params, named arguments
- Test 12: `sum_all(...)` - variadic sum with `sum(varg())`
- Test 13: `format_args(fmt, ...)` - regular param + variadic with `len(varg())`
- Test 14: `first_or_default(default, ...)` - variadic indexed access with `varg(0)`
- Test 15: `count_args(...)` - variadic length with `len(varg())`
