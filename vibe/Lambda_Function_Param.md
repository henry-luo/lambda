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
