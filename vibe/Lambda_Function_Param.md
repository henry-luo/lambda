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

---

## Test Plan

### Test File: `test/lambda/func_param.ls`

```lambda
// Test 1: Missing arguments - should fill with null
fn two_params(a, b) { [a, b] }
two_params(1)           // Expected: [1, null]
two_params()            // Expected: [null, null]

// Test 2: Extra arguments - should discard with warning
fn one_param(a) { a }
one_param(1, 2, 3)      // Expected: 1 (with warning in log)

// Test 3: Type matching - compatible types
fn takes_float(x: float) -> float { x * 2.0 }
takes_float(5)          // Expected: 10.0 (int → float coercion)
takes_float(5.0)        // Expected: 10.0

// Test 4: Type matching - int64 coercion
fn takes_int64(x: int) -> int { x + 1 }
takes_int64(100)        // Expected: 101

// Test 5: Return type matching
fn returns_int() -> int { 42 }
returns_int()           // Expected: 42

fn returns_float() -> float { 3.14 }
returns_float()         // Expected: 3.14

// Test 6: Mixed param and return types
fn process(a: int, b: float) -> float { a + b }
process(10, 2.5)        // Expected: 12.5

// Test 7: Variadic-like behavior with null filling
fn optional_params(required, opt1, opt2) {
    if (opt1 == null) "no opt1"
    else if (opt2 == null) "no opt2"
    else "all present"
}
optional_params(1)          // Expected: "no opt1"
optional_params(1, 2)       // Expected: "no opt2"
optional_params(1, 2, 3)    // Expected: "all present"
```

### Test File: `test/lambda/func_param_negative.ls`

```lambda
// Negative tests - these trigger type errors but should continue transpiling
// Run with: ./lambda.exe test/lambda/negative/func_param_negative.ls
// Expected: Multiple error messages logged, transpilation continues until threshold

// Test 1: Type error - string passed to int param
fn strict_int(x: int) -> int { x }
strict_int("hello")   // Error: string incompatible with int, outputs null or error

// Test 2: Multiple type errors in sequence (tests error accumulation)
fn typed_fn(a: int, b: float, c: string) { [a, b, c] }
typed_fn("wrong", "also wrong", 123)  // 3 type errors, should all be reported

// Test 3: Return type mismatch
fn bad_return() -> int { "string" }  // Warning: body returns string, declared int
bad_return()

// Test 4: Mixed valid and invalid calls (error recovery)
fn add_ints(a: int, b: int) -> int { a + b }
add_ints(1, 2)           // Valid
add_ints("x", "y")       // Error
add_ints(3, 4)           // Valid - should still work after error
```

---

## Implementation Order

1. **Phase 1: Parameter Count Handling** (Minimal risk)
   - Implement missing argument filling with null
   - Implement extra argument discarding with warning
   - Add tests for both scenarios

2. **Phase 2: Type Compatibility Check** (Medium risk)
   - Add `types_compatible()` helper function
   - Add type validation in `build_call_expr()`
   - Add tests for type coercion scenarios

3. **Phase 3: Return Type Validation** (Medium risk)
   - Add return type validation in `build_func()`
   - Update return value boxing in `define_func()`
   - Add tests for return type scenarios

4. **Phase 4: Enhanced Boxing** (Low risk)
   - Complete boxing/unboxing cases in `transpile_call_expr()`
   - Add `it2b()` if needed
   - Add edge case tests

---

## Risk Assessment

| Change | Risk | Mitigation |
|--------|------|------------|
| Missing arg → null | Low | Well-defined behavior, easy to test |
| Extra arg → discard | Low | Conservative approach, warning logged |
| Type errors at transpile | Medium | Error accumulation with threshold; existing scripts continue until limit |
| Return type validation | Medium | Warning only, doesn't change runtime |
| Boxing enhancements | Low | Additive, doesn't change existing paths |

---

## Backward Compatibility

- **Missing arguments**: New behavior (previously undefined/crashed)
- **Extra arguments**: New behavior (previously undefined/crashed)  
- **Type checking**: May need a flag `--strict-types` to enable errors vs warnings
- **Return type**: Warning only, no runtime change

---

## Open Questions

1. ~~Should type errors be hard errors or warnings with fallback?~~ **Resolved**: Record errors and continue, stop at threshold (default: 10)
2. Should we support default parameter values in the future?
3. Should extra arguments be silently discarded or always warn?
4. Should MIR transpiler (`transpile-mir.cpp`) also be updated? (Currently doesn't handle function calls)
5. Should the error threshold be configurable via CLI flag (e.g., `--max-errors=20`)?

---

## References

- [build_ast.cpp](../lambda/build_ast.cpp) - AST building
- [transpile.cpp](../lambda/transpile.cpp) - C code transpilation
- [lambda.h](../lambda/lambda.h) - Boxing macros
- [lambda-data.cpp](../lambda/lambda-data.cpp) - Unboxing functions
- [test_lambda_gtest.cpp](../test/test_lambda_gtest.cpp) - Test patterns

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
| `fn f(a?: T, b?: T)` | All optional | `?` before `:` makes typed param optional |
| `fn f(a: T, b?: T)` | `a` required, `b` optional | Mixed: first required, second optional |
| `fn f(a: T?)` | Required | Nullable type, but param must be provided |
| `fn f(a: int = 5)` | Optional | Type + default value = optional |

### Constraint
**Optional parameters must come after all required parameters.**

```lambda
// Valid
fn valid1(req: int, opt?: int) { ... }
fn valid2(req1: int, req2: float, opt1?, opt2?) { ... }
fn valid3(req: int, opt: int = 10) { ... }          // typed with default

// Invalid - compile error
fn invalid(opt?: int, req: int) { ... }  // Error: required after optional
```

### Grammar Changes (`grammar.js`)

Current `parameter` rule:
```javascript
parameter: $ => seq(
  field('name', $.identifier), optional(seq(':', field('type', $._type_expr))),
),
```

Proposed change:
```javascript
parameter: $ => seq(
  field('name', $.identifier),
  optional(field('optional', '?')),  // optional marker BEFORE type
  optional(seq(':', field('type', $._type_expr))),
  optional(seq('=', field('default', $._expression))),
),
```

**Note**: The `?` comes BEFORE the `:` to distinguish from nullable types:
- `a?: int` - optional parameter of type `int`
- `a: int?` - required parameter of nullable type `int?`

### AST Changes (`ast.hpp`)

Extend `TypeParam`:
```cpp
typedef struct TypeParam : Type {
    struct TypeParam* next;
    bool is_optional;        // New: whether parameter is optional
    AstNode* default_value;  // New: default value expression (NULL if none)
} TypeParam;
```

### Build Phase Changes (`build_ast.cpp`)

In `build_param_expr()`:
```cpp
AstNamedNode* build_param_expr(Transpiler* tp, TSNode param_node, bool is_type) {
    // ... existing code ...
    
    // Check for optional marker '?' (before the colon)
    TSNode optional_node = ts_node_child_by_field_id(param_node, FIELD_OPTIONAL);
    TypeParam* type_param = (TypeParam*)ast_node->type;
    
    if (!ts_node_is_null(optional_node)) {
        // Explicit '?' marker = optional
        type_param->is_optional = true;
    } else if (ts_node_is_null(type_node)) {
        // No type annotation = optional (implicit)
        type_param->is_optional = true;
    } else {
        // Has type annotation without '?' = required
        type_param->is_optional = false;
    }
    
    // Parse default value (implies optional)
    TSNode default_node = ts_node_child_by_field_id(param_node, FIELD_DEFAULT);
    if (!ts_node_is_null(default_node)) {
        type_param->default_value = build_expr(tp, default_node);
        type_param->is_optional = true;  // default implies optional
    }
    
    // ... rest of existing code ...
}
```

In `build_func()`, add validation:
```cpp
// After building all params, validate ordering
TypeParam* param = fn_type->param;
bool seen_optional = false;
while (param) {
    if (param->is_optional) {
        seen_optional = true;
    } else if (seen_optional) {
        record_type_error(tp, line, 
            "required parameter after optional parameter");
    }
    param = param->next;
}
```

### Transpilation Changes (`transpile.cpp`)

In `transpile_call_expr()`:
```cpp
// Fill missing arguments with default value or ITEM_NULL
while (param_type) {
    log_debug("param_mismatch: filling missing argument for param type %d", 
        param_type->type_id);
    if (has_output_arg) {
        strbuf_append_char(tp->code_buf, ',');
    }
    
    if (param_type->default_value) {
        // Use default value expression
        transpile_expr(tp, param_type->default_value);
    } else if (param_type->is_optional) {
        // Optional without default → null
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
    } else {
        // Required parameter missing - error
        log_error("missing_required_param: required parameter not provided");
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
    }
    has_output_arg = true;
    param_type = param_type->next;
}
```

---

## 2. Default Parameter Values

### Syntax

Default values are supported for both untyped and typed parameters:

```lambda
// Untyped with default
fn greet(name = "World") {
    "Hello, " ++ name ++ "!"
}

// Typed with default (Type + Default syntax)
fn add(a: int, b: int = 0) {
    a + b
}

// Multiple defaults
fn range_sum(start: int = 0, end: int = 10, step: int = 1) {
    let sum = 0
    for (i in start to end by step) { sum = sum + i }
    sum
}

greet()                     // "Hello, World!"
greet("Lambda")             // "Hello, Lambda!"
add(5)                      // 5
add(5, 3)                   // 8
range_sum()                 // 45 (0+1+2+...+9)
range_sum(5)                // 35 (5+6+7+8+9)
range_sum(0, 5)             // 10 (0+1+2+3+4)
```

### Type + Default Syntax

Lambda supports typed parameters with default values using `name: Type = value`:

```lambda
fn create_user(
    name: string,           // required
    age: int = 0,           // optional with type and default
    active: bool = true     // optional with type and default
) {
    { name: name, age: age, active: active }
}

create_user("Alice")                    // {name: "Alice", age: 0, active: true}
create_user("Bob", 30)                  // {name: "Bob", age: 30, active: true}
create_user("Charlie", 25, false)       // {name: "Charlie", age: 25, active: false}
```

### Constraints

1. **Default expressions evaluated at call site** (not definition site)
   - Allows referencing earlier parameters
   - Expressions re-evaluated each call

2. **Defaults can reference earlier parameters**:
```lambda
fn make_rect(width: int, height = width) {  // height defaults to width
    [width, height]
}
make_rect(10)       // [10, 10]
make_rect(10, 20)   // [10, 20]
```

3. **Order constraint**: Parameters with defaults must come after required parameters (same as optional)

### Default Value Evaluation in Transpiler

When filling missing arguments, the default expression is transpiled inline:

```cpp
// In transpile_call_expr()
if (param_type->default_value) {
    // Transpile the default expression at the call site
    transpile_expr(tp, param_type->default_value);
}
```

For referencing earlier parameters in defaults:
```cpp
fn make_rect(width: int, height = width) { ... }

// Transpiles to:
Item fn_make_rect(Item _width, Item _height) {
    // ...
}

// Call: make_rect(10) transpiles to:
fn_make_rect(i2it(10), _width)  // height gets value of width
```

**Note**: This requires that earlier parameters are accessible during default value transpilation. The transpiler must track parameter bindings.

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
sum(1, 2, 3, 4, 5)  // 15
sum()               // 0

fn printf(fmt, ...) {
    // fmt is required, rest are variadic
    format(fmt, varg())
}

printf("Hello %s, you have %d messages", "Alice", 5)
```

### Grammar Changes

```javascript
// Add variadic marker
parameter: $ => choice(
    seq(
      field('name', $.identifier),
      optional(field('optional', '?')),
      optional(seq(':', field('type', $._type_expr))),
      optional(seq('=', field('default', $._expression))),
    ),
    field('variadic', '...'),  // variadic marker
),
```

### System Functions: `varg()` and `arg()` (Future)

**`varg()`** provides access to **variadic arguments only**:

| Call | Return |
|------|--------|
| `varg()` | List of all variadic arguments |
| `varg(0)` | First variadic argument |
| `varg(1)` | Second variadic argument |
| `varg().len` | Count of variadic arguments |

**`arg()`** (Future Feature) will provide access to **all arguments**:

| Call | Return |
|------|--------|
| `arg()` | List of all arguments (named + variadic) |
| `arg(0)` | First argument |
| `arg('name')` | Argument by parameter name |

> **Note**: `arg()` is reserved for future implementation when full argument introspection is needed.

### Implementation Strategy

**Runtime Argument List (Recommended)**

Store variadic arguments in a special runtime structure:

```cpp
// In lambda.h
typedef struct VargList {
    Item* items;      // array of Items
    int count;        // number of items
    int capacity;     // allocated capacity
} VargList;

// Thread-local or context-passed
extern __thread VargList* current_varg_list;

// System function implementation
Item fn_varg0() {  // varg() - return all as list
    return create_list_from_items(current_varg_list->items, current_varg_list->count);
}

Item fn_varg1(int index) {  // varg(n) - return nth
    if (index < 0 || index >= current_varg_list->count) {
        return ITEM_NULL;
    }
    return current_varg_list->items[index];
}
```

**AST Changes**:
```cpp
typedef struct TypeFunc : Type {
    TypeParam* param;
    Type* returned;
    int param_count;
    int type_index;
    bool is_anonymous;
    bool is_public;
    bool is_proc;
    bool is_variadic;         // New: function accepts variadic args
    int required_param_count; // New: non-variadic params before ...
} TypeFunc;
```

**Transpilation**:

For variadic function call:
```cpp
// sum(1, 2, 3) transpiles to:
({
    Item _varargs[] = {i2it(1), i2it(2), i2it(3)};
    VargList _varglist = {_varargs, 3, 3};
    current_varg_list = &_varglist;
    Item _result = fn_sum();
    current_varg_list = NULL;
    _result;
})
```

For function definition:
```cpp
// fn sum(...) { ... } transpiles to:
Item fn_sum() {
    // varg() accesses current_varg_list
    ...
}
```

---

## 4. Named Arguments

### Syntax

Named arguments allow passing arguments by parameter name, making calls self-documenting and order-independent:

```lambda
fn create_rect(x: int, y: int, width: int, height: int) {
    { x: x, y: y, width: width, height: height }
}

// Positional call (existing)
create_rect(10, 20, 100, 50)

// Named call - order independent
create_rect(x: 10, y: 20, width: 100, height: 50)
create_rect(width: 100, height: 50, x: 10, y: 20)  // Same result

// Mixed positional and named (positional must come first)
create_rect(10, 20, width: 100, height: 50)
```

### Benefits

1. **Self-documenting code**: Parameter names visible at call site
2. **Skip optional parameters**: Can skip middle optional params
3. **Reduces errors**: No confusion with multiple same-type parameters
4. **IDE support**: Better autocomplete and documentation

### Skipping Optional Parameters

Named arguments are especially useful when you want to skip optional parameters:

```lambda
fn connect(
    host: string,
    port: int = 80,
    timeout: int = 30,
    retries: int = 3,
    ssl: bool = false
) { ... }

// Skip port and timeout, set only retries and ssl
connect("example.com", retries: 5, ssl: true)

// Equivalent to:
connect("example.com", 80, 30, 5, true)
```

### Grammar Changes for Named Arguments

The call expression grammar needs to support named arguments:

```javascript
// In grammar.js, modify call_expr arguments
_argument: $ => choice(
    $._expression,  // positional argument
    $.named_argument,  // named argument
),

named_argument: $ => seq(
    field('name', $.identifier),
    ':',
    field('value', $._expression),
),
```

### Build Phase Changes

In `build_call_expr()`:
```cpp
// Track named vs positional arguments
bool seen_named = false;
while (arg_node) {
    if (is_named_argument(arg_node)) {
        seen_named = true;
        // Map argument to parameter by name
        String* arg_name = get_argument_name(arg_node);
        int param_index = find_param_by_name(fn_type, arg_name);
        if (param_index < 0) {
            record_type_error(tp, line, "unknown parameter name: %.*s",
                (int)arg_name->len, arg_name->chars);
        }
        // Store mapping for transpilation
    } else if (seen_named) {
        record_type_error(tp, line, 
            "positional argument after named argument");
    }
    // ... rest of argument processing
}
```

### Transpilation Changes

Named arguments are reordered to match parameter order during transpilation:

```cpp
// Call: create_rect(width: 100, height: 50, x: 10, y: 20)
// Parameters: x, y, width, height
// Transpiles to: fn_create_rect(i2it(10), i2it(20), i2it(100), i2it(50))
```

---

## 5. Combined Features

All four features work together:

```lambda
fn flexible(required: int, opt?: int, default_val: int = 10, ...) {
    let base = required + (opt ?? 0) + default_val
    let extra = 0
    for (x in varg()) { extra = extra + x }
    base + extra
}

// Positional calls
flexible(1)                     // 1 + 0 + 10 + 0 = 11
flexible(1, 2)                  // 1 + 2 + 10 + 0 = 13
flexible(1, 2, 3)               // 1 + 2 + 3 + 0 = 6
flexible(1, 2, 3, 4, 5)         // 1 + 2 + 3 + (4+5) = 15
flexible(1, null, 5)            // 1 + 0 + 5 + 0 = 6

// Named argument - skip opt, set default_val
flexible(required: 1, default_val: 20)  // 1 + 0 + 20 + 0 = 21
```

### Parameter Order Constraint

```
required_typed → optional_typed (?) → defaults → variadic
```

Examples:
```lambda
fn valid(a: int, b?: int, c: int = 0, ...) { ... }   // Valid
fn invalid1(..., a) { ... }                           // Error: ... must be last
fn invalid2(a?: int, b: int) { ... }                  // Error: required after optional
```

---

### 
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

---

## Risk Assessment

| Feature                                   | Risk   | Notes                                                    |
| ----------------------------------------- | ------ | -------------------------------------------------------- |
| Optional params (`a?`)                    | Low    | Minimal grammar change, TypeScript-compatible semantics  |
| Optional vs Nullable (`a?: T` vs `a: T?`) | Low    | Clear distinction, matches TypeScript                    |
| Default values                            | Medium | Must handle scoping of earlier params in defaults        |
| Type + Default (`a: int = 5`)             | Low    | Natural extension of default values                      |
| Named arguments                           | Medium | Grammar change for call expressions, argument reordering |
| Variadic (...)                            | Higher | Requires runtime support, thread-safety considerations   |
| varg() system func                        | Medium | New system function, clear API                           |

---

## Open Questions

1. ~~Should defaults support calling other functions?~~ **Resolved**: Yes, defaults are full expressions and can call functions
2. ~~Should defaults be evaluated lazily (only when param is missing)?~~ **Resolved**: Yes, lazy evaluation at call site
3. ~~For named arguments, should duplicate names be a compile error or runtime error?~~ **Resolved**: Compile error
4. ~~Can named arguments be mixed with variadic?~~ **Resolved**: Yes, named args fill named params, remaining positional args go to variadic

---

## Design Decisions Summary

| Decision | Resolution | Rationale |
|----------|------------|-----------|
| Default expressions | Full expressions allowed | Enables `fn f(a, b = compute(a))` patterns |
| Default evaluation | Lazy (at call site) | Only evaluated when argument is missing |
| Duplicate named args | Compile error | Catch errors early, no ambiguity |
| Named + variadic mixing | Allowed | Named args match params, rest go to `...` |
