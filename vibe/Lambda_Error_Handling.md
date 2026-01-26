# Lambda Error Handling Enhancement Proposal

**Author**: Lambda Development Team  
**Date**: January 2026  
**Status**: Proposal  

## Executive Summary

This proposal outlines a comprehensive enhancement to Lambda's error handling and reporting system. The current implementation has ad-hoc error handling with:
- No structured error codes
- Inconsistent error messages lacking context (file, line, column)
- No stack trace support at runtime
- Minimal negative testing coverage

This enhancement aims to:
1. Define a structured, hierarchical error code system
2. Provide rich, contextual error messages
3. Enable optional stack trace printing for debugging
4. Establish a comprehensive negative test suite

---

## 1. Structured Error Code System

### 1.1 Error Code Categories

| Range | Category | Description |
|-------|----------|-------------|
| **1xx** | Syntax Errors | Lexical and grammatical errors during parsing |
| **2xx** | Semantic/Compilation Errors | Type checking, transpilation, JIT compilation |
| **3xx** | Runtime Errors | Execution-time failures |
| **4xx** | I/O Errors | File, network, external resource errors |
| **5xx** | Internal Errors | Unexpected internal states, bugs |

### 1.2 Detailed Error Codes

#### 1xx - Syntax Errors

| Code | Name | Description |
|------|------|-------------|
| 100 | `SYNTAX_ERROR` | Generic syntax error |
| 101 | `UNEXPECTED_TOKEN` | Unexpected token encountered |
| 102 | `MISSING_TOKEN` | Expected token missing (e.g., `)`, `}`) |
| 103 | `INVALID_LITERAL` | Malformed literal (number, string, datetime) |
| 104 | `INVALID_IDENTIFIER` | Invalid identifier format |
| 105 | `UNTERMINATED_STRING` | String literal not closed |
| 106 | `UNTERMINATED_COMMENT` | Comment block not closed |
| 107 | `INVALID_ESCAPE` | Invalid escape sequence in string |
| 108 | `INVALID_NUMBER` | Invalid numeric literal format |
| 109 | `INVALID_DATETIME` | Invalid datetime literal format |
| 110 | `INVALID_BINARY` | Invalid binary literal format |
| 111 | `UNEXPECTED_EOF` | Unexpected end of file |
| 112 | `INVALID_OPERATOR` | Invalid or unsupported operator |
| 113 | `INVALID_ELEMENT_SYNTAX` | Malformed element `<tag ...>` |
| 114 | `INVALID_MAP_SYNTAX` | Malformed map `{...}` |
| 115 | `INVALID_ARRAY_SYNTAX` | Malformed array `[...]` |
| 116 | `INVALID_RANGE_SYNTAX` | Malformed range expression |
| 117 | `DUPLICATE_PARAMETER` | Duplicate parameter name in function |
| 118 | `INVALID_PARAM_SYNTAX` | Malformed function parameter |
| 119 | `INVALID_TYPE_SYNTAX` | Malformed type annotation |

#### 2xx - Semantic/Compilation Errors

| Code | Name | Description |
|------|------|-------------|
| 200 | `SEMANTIC_ERROR` | Generic semantic error |
| 201 | `TYPE_MISMATCH` | Type incompatibility |
| 202 | `UNDEFINED_VARIABLE` | Reference to undefined variable |
| 203 | `UNDEFINED_FUNCTION` | Reference to undefined function |
| 204 | `UNDEFINED_TYPE` | Reference to undefined type |
| 205 | `UNDEFINED_FIELD` | Reference to undefined map/element field |
| 206 | `ARGUMENT_COUNT_MISMATCH` | Wrong number of function arguments |
| 207 | `ARGUMENT_TYPE_MISMATCH` | Function argument type incompatible |
| 208 | `RETURN_TYPE_MISMATCH` | Function return type incompatible |
| 209 | `DUPLICATE_DEFINITION` | Duplicate function/variable/type definition |
| 210 | `INVALID_ASSIGNMENT` | Invalid assignment target |
| 211 | `IMMUTABLE_ASSIGNMENT` | Assignment to immutable variable |
| 212 | `INVALID_CALL` | Calling non-function value |
| 213 | `INVALID_INDEX` | Invalid index access on non-indexable type |
| 214 | `INVALID_MEMBER_ACCESS` | Invalid member access on type |
| 215 | `CIRCULAR_DEPENDENCY` | Circular import/type dependency |
| 216 | `IMPORT_NOT_FOUND` | Module import not found |
| 217 | `IMPORT_ERROR` | Error loading imported module |
| 218 | `TRANSPILATION_ERROR` | Generic transpilation failure |
| 219 | `JIT_COMPILATION_ERROR` | MIR JIT compilation failure |
| 220 | `RECURSION_DEPTH_EXCEEDED` | Maximum AST recursion depth exceeded |
| 221 | `INVALID_EXPRESSION_CONTEXT` | Expression used in invalid context |
| 222 | `MISSING_RETURN` | Missing return in function with return type |
| 223 | `UNREACHABLE_CODE` | Code after return statement (warning) |
| 224 | `PROC_IN_FN` | Procedural construct in functional context |
| 225 | `BREAK_OUTSIDE_LOOP` | `break` used outside loop |
| 226 | `CONTINUE_OUTSIDE_LOOP` | `continue` used outside loop |
| 227 | `RETURN_OUTSIDE_FUNCTION` | `return` used outside function |

#### 3xx - Runtime Errors

| Code | Name | Description |
|------|------|-------------|
| 300 | `RUNTIME_ERROR` | Generic runtime error |
| 301 | `NULL_REFERENCE` | Null dereference |
| 302 | `INDEX_OUT_OF_BOUNDS` | Array/list index out of range |
| 303 | `KEY_NOT_FOUND` | Map key not found |
| 304 | `DIVISION_BY_ZERO` | Division or modulo by zero |
| 305 | `OVERFLOW` | Numeric overflow |
| 306 | `UNDERFLOW` | Numeric underflow |
| 307 | `INVALID_CAST` | Invalid type cast/conversion |
| 308 | `STACK_OVERFLOW` | Recursion/call stack overflow |
| 309 | `OUT_OF_MEMORY` | Memory allocation failure |
| 310 | `TIMEOUT` | Execution timeout exceeded |
| 311 | `ASSERTION_FAILED` | Assertion failure |
| 312 | `INVALID_OPERATION` | Operation not valid for type |
| 313 | `EMPTY_COLLECTION` | Operation on empty collection |
| 314 | `ITERATOR_EXHAUSTED` | Iterator has no more elements |
| 315 | `INVALID_REGEX` | Invalid regular expression |
| 316 | `DECIMAL_PRECISION_LOSS` | Decimal operation precision loss |
| 317 | `DATETIME_INVALID` | Invalid datetime operation |
| 318 | `USER_ERROR` | User-defined error via `error()` |

#### 4xx - I/O Errors

| Code | Name | Description |
|------|------|-------------|
| 400 | `IO_ERROR` | Generic I/O error |
| 401 | `FILE_NOT_FOUND` | File does not exist |
| 402 | `FILE_ACCESS_DENIED` | Permission denied |
| 403 | `FILE_READ_ERROR` | Error reading file |
| 404 | `FILE_WRITE_ERROR` | Error writing file |
| 405 | `NETWORK_ERROR` | Network operation failed |
| 406 | `NETWORK_TIMEOUT` | Network request timeout |
| 407 | `PARSE_ERROR` | Error parsing input format (JSON, XML, etc.) |
| 408 | `FORMAT_ERROR` | Error formatting output |
| 409 | `ENCODING_ERROR` | Character encoding error |
| 410 | `INVALID_URL` | Invalid URL format |
| 411 | `HTTP_ERROR` | HTTP request error (with status code in details) |

#### 5xx - Internal Errors

| Code | Name | Description |
|------|------|-------------|
| 500 | `INTERNAL_ERROR` | Generic internal error |
| 501 | `NOT_IMPLEMENTED` | Feature not yet implemented |
| 502 | `INVALID_STATE` | Invalid internal state |
| 503 | `MEMORY_CORRUPTION` | Detected memory corruption |
| 504 | `TYPE_SYSTEM_ERROR` | Type system inconsistency |
| 505 | `POOL_EXHAUSTED` | Memory pool exhausted |

### 1.3 Error Code Implementation

```cpp
// lambda/lambda_error.h

#pragma once

// Error code ranges
#define ERR_SYNTAX_BASE    100
#define ERR_SEMANTIC_BASE  200
#define ERR_RUNTIME_BASE   300
#define ERR_IO_BASE        400
#define ERR_INTERNAL_BASE  500

// Category macros
#define ERR_IS_SYNTAX(code)    ((code) >= 100 && (code) < 200)
#define ERR_IS_SEMANTIC(code)  ((code) >= 200 && (code) < 300)
#define ERR_IS_RUNTIME(code)   ((code) >= 300 && (code) < 400)
#define ERR_IS_IO(code)        ((code) >= 400 && (code) < 500)
#define ERR_IS_INTERNAL(code)  ((code) >= 500 && (code) < 600)

typedef enum LambdaErrorCode {
    // Success
    ERR_OK = 0,
    
    // 1xx - Syntax Errors
    ERR_SYNTAX_ERROR = 100,
    ERR_UNEXPECTED_TOKEN = 101,
    ERR_MISSING_TOKEN = 102,
    ERR_INVALID_LITERAL = 103,
    ERR_INVALID_IDENTIFIER = 104,
    ERR_UNTERMINATED_STRING = 105,
    // ... (all codes from table above)
    
    // 2xx - Semantic Errors
    ERR_SEMANTIC_ERROR = 200,
    ERR_TYPE_MISMATCH = 201,
    ERR_UNDEFINED_VARIABLE = 202,
    ERR_UNDEFINED_FUNCTION = 203,
    // ... (all codes from table above)
    
    // 3xx - Runtime Errors
    ERR_RUNTIME_ERROR = 300,
    ERR_NULL_REFERENCE = 301,
    ERR_INDEX_OUT_OF_BOUNDS = 302,
    // ... (all codes from table above)
    
    // 4xx - I/O Errors
    ERR_IO_ERROR = 400,
    ERR_FILE_NOT_FOUND = 401,
    // ... (all codes from table above)
    
    // 5xx - Internal Errors
    ERR_INTERNAL_ERROR = 500,
    ERR_NOT_IMPLEMENTED = 501,
    // ... (all codes from table above)
    
} LambdaErrorCode;

// Error code to string lookup
const char* err_code_name(LambdaErrorCode code);
const char* err_code_message(LambdaErrorCode code);
```

---

## 2. Enhanced Error Reporting

### 2.1 Error Structure

```cpp
// lambda/lambda_error.h

// Source location information
typedef struct SourceLocation {
    const char* file;       // source file path (may be NULL for REPL)
    uint32_t line;          // 1-based line number
    uint32_t column;        // 1-based column number
    uint32_t end_line;      // end line for multi-line spans
    uint32_t end_column;    // end column
    const char* source;     // pointer to source text (for context extraction)
} SourceLocation;

// Stack frame for stack traces
typedef struct StackFrame {
    const char* function_name;  // function name (or "<script>" for top-level)
    SourceLocation location;    // call site location
    struct StackFrame* caller;  // previous frame (toward main)
} StackFrame;

// Rich error structure
typedef struct LambdaError {
    LambdaErrorCode code;       // error code (e.g., 201)
    const char* message;        // human-readable message
    SourceLocation location;    // where the error occurred
    StackFrame* stack_trace;    // call stack (if enabled)
    const char* context_before; // source lines before error (optional)
    const char* context_after;  // source lines after error (optional)
    void* details;              // error-specific details (optional)
    struct LambdaError* cause;  // chained error (optional)
} LambdaError;
```

### 2.2 Error Formatting

Error messages should follow a consistent format:

```
<file>:<line>:<col>: error[E<code>]: <message>
    |
<ln>| <source line>
    |    ^^^^ <pointer to error>
    = help: <suggestion if available>
```

Example outputs:

```
script.ls:15:23: error[E201]: type mismatch
    |
 15 | let x: int = "hello"
    |              ^^^^^^^ expected `int`, found `string`
    = help: use `int("hello")` to parse string as integer

script.ls:42:5: error[E203]: undefined function
    |
 42 |     calculate_totl(data)
    |     ^^^^^^^^^^^^^^ function `calculate_totl` not found
    = help: did you mean `calculate_total`?

script.ls:78:17: error[E302]: index out of bounds
    |
 78 |     let item = arr[10]
    |                ^^^^^^^ index 10 out of bounds for array of length 5
    at calculate_total (script.ls:78:17)
    at process_data (script.ls:52:9)
    at <main> (script.ls:12:1)
```

### 2.3 Error Context API

```cpp
// Create error with location
LambdaError* err_create(
    LambdaErrorCode code,
    const char* message,
    SourceLocation* location
);

// Create error with formatted message
LambdaError* err_createf(
    LambdaErrorCode code,
    SourceLocation* location,
    const char* format,
    ...
);

// Add suggestion to error
void err_add_help(LambdaError* error, const char* help);

// Chain errors (for "caused by" reporting)
void err_set_cause(LambdaError* error, LambdaError* cause);

// Format error to string
char* err_format(LambdaError* error, Pool* pool);

// Format error to stderr
void err_print(LambdaError* error);

// Extract source context around location
void err_extract_context(
    LambdaError* error,
    const char* source,
    int context_lines  // lines before/after to include
);
```

### 2.4 Integration Points

#### 2.4.1 Parser Integration (build_ast.cpp)

```cpp
// Current: just logs error
log_error("Syntax error at Ln %u, Col %u", row, col);

// Enhanced: create structured error
SourceLocation loc = {
    .file = tp->reference,
    .line = row + 1,
    .column = col + 1,
    .source = tp->source
};
LambdaError* err = err_create(ERR_UNEXPECTED_TOKEN, 
    "unexpected token in expression", &loc);
err_extract_context(err, tp->source, 2);
transpiler_add_error(tp, err);
```

#### 2.4.2 Type Checker Integration

```cpp
// Current
log_error("Type mismatch: expected %s, got %s", expected, actual);

// Enhanced
LambdaError* err = err_createf(ERR_TYPE_MISMATCH, &loc,
    "type mismatch: expected `%s`, got `%s`", 
    type_name(expected), type_name(actual));
err_add_help(err, "use explicit type conversion");
```

#### 2.4.3 Runtime Integration

```cpp
// Current
return ItemError;

// Enhanced: create rich error Item
Item runtime_error(EvalContext* ctx, LambdaErrorCode code, const char* msg) {
    LambdaError* err = err_create(code, msg, ctx->current_location);
    if (ctx->enable_stack_trace) {
        err->stack_trace = capture_stack_trace(ctx);
    }
    return create_error_item(err);
}
```

### 2.5 Transpiler Error Collection

```cpp
// In transpiler.hpp
struct Transpiler {
    // ... existing fields ...
    
    // Error collection
    ArrayList* errors;      // List of LambdaError*
    int error_count;
    int warning_count;
    int max_errors;         // stop after N errors
    
    // Source tracking
    const char* current_file;
    const char* source;
};

void transpiler_add_error(Transpiler* tp, LambdaError* error);
void transpiler_add_warning(Transpiler* tp, LambdaError* warning);
void transpiler_print_errors(Transpiler* tp);
bool transpiler_has_errors(Transpiler* tp);
```

---

## 3. Stack Trace Support

### 3.1 Design Principles

- **Enabled by default** - stack traces always captured on error for better debugging
- **Zero overhead** during normal execution - stack traces captured only on error
- Uses **native stack walking** instead of explicit instrumentation
- Maps native addresses back to Lambda source locations via debug info table

### 3.2 Approach Comparison

#### ❌ Rejected: Explicit Instrumentation

We considered adding `push_stack_frame()` / `pop_stack_frame()` calls to every generated function:

```cpp
// REJECTED APPROACH - Do not use
void emit_function_entry(MirContext* ctx, AstFuncNode* func) {
    emit_call(ctx, "push_stack_frame", ...);
}
void emit_function_exit(MirContext* ctx, AstFuncNode* func) {
    emit_call(ctx, "pop_stack_frame", ...);
}
```

**Why rejected:**
- **Performance overhead**: Branch checking on every function entry/exit, even when tracing disabled
- **Code bloat**: Every function gets extra prologue/epilogue code
- **Complexity**: Must handle all exit paths (early returns, exceptions)
- **Maintenance burden**: Every code path must be instrumented correctly

#### ✅ Recommended: Native Stack Walking

Walk the C native stack only when an error occurs, then map addresses to Lambda source.

**Benefits:**
- **Zero normal overhead**: No cost until error occurs
- **Simpler codegen**: No instrumentation needed in generated code
- **Platform-native**: Uses well-tested OS APIs
- **Complete trace**: Captures entire call chain including runtime functions

### 3.3 Configuration

Stack traces are **enabled by default**. The following options are planned for future implementation:

#### KIV: CLI Flag (Future)
```bash
lambda script.ls --no-stack-trace   # disable stack traces
lambda script.ls --stack-trace=5    # limit to 5 frames
```

#### KIV: Runtime Control (Future)
```lambda
import sys;
sys.enable_stack_trace(false); // disable for performance-critical code
```

#### Runtime Configuration
```cpp
// In EvalContext
typedef struct EvalContext {
    // ... existing fields ...
    
    bool enable_stack_trace;    // default: true
    int max_stack_frames;       // 0 = unlimited (default)
    ArrayList* debug_info;      // function address → source mapping
} EvalContext;
```

### 3.4 Native Stack Walking Implementation

#### 3.4.1 Debug Info Table

Built during JIT compilation to map native addresses to Lambda source:

```cpp
// Debug information for a compiled function
typedef struct FuncDebugInfo {
    void* native_addr_start;        // start of native code
    void* native_addr_end;          // end of native code
    const char* lambda_func_name;   // Lambda function name
    const char* source_file;        // source file path
    uint32_t source_line;           // line number of function definition
} FuncDebugInfo;

// Register function debug info after JIT compilation
void register_func_debug_info(Transpiler* tp, AstFuncNode* func, MIR_item_t mir_func) {
    FuncDebugInfo* info = pool_calloc(tp->pool, sizeof(FuncDebugInfo));
    info->native_addr_start = mir_func->addr;
    // MIR doesn't expose function size directly; estimate or track separately
    info->native_addr_end = (char*)mir_func->addr + estimated_size;
    info->lambda_func_name = func->name.str;
    info->source_file = tp->reference;
    info->source_line = func->location.line;
    
    arraylist_append(tp->debug_info, info);
}

// Look up debug info for a native address
FuncDebugInfo* lookup_debug_info(ArrayList* debug_info, void* addr) {
    for (int i = 0; i < debug_info->length; i++) {
        FuncDebugInfo* info = (FuncDebugInfo*)debug_info->data[i];
        if (addr >= info->native_addr_start && addr < info->native_addr_end) {
            return info;
        }
    }
    return NULL;  // address not in Lambda code (runtime function)
}
```

#### 3.4.2 Runtime Error with Stack Trace

```cpp
// Capture stack trace only when error occurs - zero normal overhead
Item runtime_error(EvalContext* ctx, LambdaErrorCode code, const char* msg) {
    LambdaError* err = err_create(code, msg, ctx->current_location);
    
    // Walk native stack only on error
    err->stack_trace = capture_native_stack(ctx);
    
    return create_error_item(err);
}
```

### 3.5 Stack Trace Formatting

```cpp
void print_stack_trace(StackFrame* trace, FILE* out) {
    if (!trace) return;
    
    fprintf(out, "Stack trace:\n");
    int depth = 0;
    while (trace) {
        fprintf(out, "  %d: at %s (%s:%u)\n",
            depth,
            trace->function_name ? trace->function_name : "<unknown>",
            trace->location.file ? trace->location.file : "<unknown>",
            trace->location.line);
        trace = trace->caller;
        depth++;
    }
}
```

### 3.6 Platform Implementation

**Use `backtrace()` on both macOS and Linux** for consistency:

| Platform | API | Notes |
|----------|-----|-------|
| macOS | `backtrace()` from `<execinfo.h>` | Built-in, works out of the box |
| Linux | `backtrace()` from `<execinfo.h>` | Works with MIR JIT code (see below) |
| Windows | `CaptureStackBackTrace()` | From `<windows.h>` |

#### MIR JIT Compatibility on Linux

`backtrace()` works correctly with MIR JIT-compiled code because:

1. **MIR generates standard stack frames**: MIR's code generator produces x86-64/ARM64 code with proper frame pointers (RBP-based frames), which `backtrace()` can walk.

2. **No symbol resolution needed**: We don't rely on `backtrace_symbols()` for function names. Instead, we maintain our own `FuncDebugInfo` table that maps address ranges to Lambda source locations.

3. **Linker flag for AOT code**: For statically compiled runtime functions to appear in traces, link with `-rdynamic` (exports symbols for `dladdr()`). This is only needed if you want runtime C function names; Lambda function names come from our debug table.

**Build configuration:**
```makefile
# In Makefile or premake
LDFLAGS += -rdynamic  # Linux: export symbols for backtrace_symbols()
```

```cpp
// Unified implementation for macOS and Linux
#if defined(__APPLE__) || defined(__linux__)
#include <execinfo.h>

StackFrame* capture_native_stack(EvalContext* ctx) {
    void* frames[64];
    int count = backtrace(frames, 64);
    
    // Map native addresses to Lambda source locations using our debug table
    return map_addresses_to_lambda(ctx->debug_info, frames, count, 
                                   ctx->max_stack_frames);
}
#elif defined(_WIN32)
#include <windows.h>

StackFrame* capture_native_stack(EvalContext* ctx) {
    void* frames[64];
    USHORT count = CaptureStackBackTrace(0, 64, frames, NULL);
    
    return map_addresses_to_lambda(ctx->debug_info, frames, count,
                                   ctx->max_stack_frames);
}
#endif
```

---

## 4. Negative Testing Framework

### 4.1 Current State

The existing negative test coverage is minimal:
- `test/lambda/negative/` contains only 5 test files
- `test/test_html_negative_gtest.cpp` tests HTML parser recovery

### 4.2 Negative Test Structure

```
test/
├── lambda/
│   └── negative/
│       ├── syntax/           # 1xx errors
│       │   ├── unexpected_token/
│       │   ├── missing_token/
│       │   ├── invalid_literal/
│       │   └── ...
│       ├── semantic/         # 2xx errors
│       │   ├── type_mismatch/
│       │   ├── undefined_variable/
│       │   ├── undefined_function/
│       │   └── ...
│       ├── runtime/          # 3xx errors
│       │   ├── null_reference/
│       │   ├── index_out_of_bounds/
│       │   ├── division_by_zero/
│       │   └── ...
│       └── io/               # 4xx errors
│           ├── file_not_found/
│           └── ...
├── test_negative_syntax_gtest.cpp
├── test_negative_semantic_gtest.cpp
├── test_negative_runtime_gtest.cpp
└── test_negative_io_gtest.cpp
```

### 4.3 Negative Test File Format

Each negative test should specify expected error:

```lambda
// @expect-error: E201
// @expect-line: 5
// @expect-col: 14
// @expect-message: type mismatch

let x: int = "hello"  // line 5
```

Or using a companion `.expected` file:

```
// test/lambda/negative/semantic/type_mismatch/string_to_int.ls
let x: int = "hello"

// test/lambda/negative/semantic/type_mismatch/string_to_int.expected
error_code: 201
line: 1
column: 14
message_contains: "type mismatch"
message_contains: "expected `int`"
message_contains: "found `string`"
```

### 4.4 Negative Test Categories

#### 4.4.1 Syntax Error Tests (1xx)

| Test File | Error Code | Description |
|-----------|------------|-------------|
| `unexpected_eof.ls` | 111 | File ends mid-expression |
| `missing_closing_paren.ls` | 102 | Unclosed parenthesis |
| `missing_closing_brace.ls` | 102 | Unclosed brace |
| `missing_closing_bracket.ls` | 102 | Unclosed bracket |
| `unterminated_string.ls` | 105 | String not closed |
| `invalid_number_format.ls` | 108 | Malformed number |
| `invalid_datetime.ls` | 109 | Malformed datetime literal |
| `invalid_escape.ls` | 107 | Bad escape in string |
| `duplicate_param.ls` | 117 | Same param name twice |
| `invalid_range.ls` | 116 | Bad range syntax |

#### 4.4.2 Semantic Error Tests (2xx)

| Test File | Error Code | Description |
|-----------|------------|-------------|
| `type_mismatch_int_string.ls` | 201 | Assign string to int |
| `type_mismatch_return.ls` | 208 | Wrong return type |
| `undefined_variable.ls` | 202 | Use undefined var |
| `undefined_function.ls` | 203 | Call undefined fn |
| `wrong_arg_count.ls` | 206 | Too few/many args |
| `wrong_arg_type.ls` | 207 | Bad arg type |
| `duplicate_function.ls` | 209 | Same fn defined twice |
| `call_non_function.ls` | 212 | Call int as function |
| `invalid_index_type.ls` | 213 | Index string with int |
| `immutable_assign.ls` | 211 | Assign to let variable |
| `circular_import.ls` | 215 | A imports B imports A |
| `break_outside_loop.ls` | 225 | Break not in loop |
| `continue_outside_loop.ls` | 226 | Continue not in loop |
| `return_outside_fn.ls` | 227 | Return at top level |

#### 4.4.3 Runtime Error Tests (3xx)

| Test File | Error Code | Description |
|-----------|------------|-------------|
| `null_dereference.ls` | 301 | Access field of null |
| `index_negative.ls` | 302 | arr[-1] |
| `index_too_large.ls` | 302 | arr[100] on len=5 |
| `key_not_found.ls` | 303 | map['missing'] |
| `divide_by_zero.ls` | 304 | 1/0 |
| `modulo_by_zero.ls` | 304 | 5%0 |
| `int_overflow.ls` | 305 | MAX_INT + 1 |
| `invalid_cast.ls` | 307 | int("not a number") |
| `stack_overflow.ls` | 308 | Deep recursion |
| `empty_collection.ls` | 313 | sum([]) |
| `decimal_div_zero.ls` | 304 | 1.0n / 0.0n |

#### 4.4.4 I/O Error Tests (4xx)

| Test File | Error Code | Description |
|-----------|------------|-------------|
| `file_not_found.ls` | 401 | input("missing.json") |
| `invalid_json.ls` | 407 | Parse malformed JSON |
| `invalid_xml.ls` | 407 | Parse malformed XML |
| `invalid_url.ls` | 410 | fetch("not-a-url") |

### 4.5 GTest Framework for Negative Tests

```cpp
// test/test_negative_gtest.cpp

#include <gtest/gtest.h>
#include "negative_test_helper.hpp"

class NegativeSyntaxTest : public ::testing::Test {
protected:
    void expectError(const char* script, LambdaErrorCode expected_code) {
        auto result = run_script(script);
        ASSERT_TRUE(result.has_error);
        EXPECT_EQ(result.error->code, expected_code);
    }
    
    void expectErrorAt(const char* script, LambdaErrorCode code, 
                       int line, int col) {
        auto result = run_script(script);
        ASSERT_TRUE(result.has_error);
        EXPECT_EQ(result.error->code, code);
        EXPECT_EQ(result.error->location.line, line);
        EXPECT_EQ(result.error->location.column, col);
    }
};

TEST_F(NegativeSyntaxTest, UnterminatedString) {
    expectError("let x = \"hello", ERR_UNTERMINATED_STRING);
}

TEST_F(NegativeSyntaxTest, MissingClosingParen) {
    expectErrorAt("let x = (1 + 2", ERR_MISSING_TOKEN, 1, 15);
}

TEST_F(NegativeSyntaxTest, InvalidNumber) {
    expectError("let x = 12.34.56", ERR_INVALID_NUMBER);
}

// ... more tests ...

class NegativeSemanticTest : public ::testing::Test { /* ... */ };

TEST_F(NegativeSemanticTest, TypeMismatchIntString) {
    expectError("let x: int = \"hello\"", ERR_TYPE_MISMATCH);
}

TEST_F(NegativeSemanticTest, UndefinedVariable) {
    expectErrorAt("let x = y + 1", ERR_UNDEFINED_VARIABLE, 1, 9);
}

// ... more tests ...
```

### 4.6 Test Runner Integration

Add to `Makefile`:

```makefile
test-negative: build-test
	./test/test_negative_syntax_gtest.exe
	./test/test_negative_semantic_gtest.exe
	./test/test_negative_runtime_gtest.exe
	./test/test_negative_io_gtest.exe

test-lambda-baseline: test-negative
	# existing tests...
```

---

## 5. Additional Suggestions

### 5.1 Error Recovery Mode

Add "error recovery" mode that continues after first error:

```bash
lambda script.ls --max-errors=10    # report up to 10 errors
lambda script.ls --all-errors       # report all errors
```

### 5.2 Machine-Readable Error Output

Add JSON error output for IDE integration:

```bash
lambda script.ls --error-format=json
```

Output:
```json
{
  "errors": [
    {
      "code": 201,
      "name": "TYPE_MISMATCH",
      "severity": "error",
      "message": "type mismatch: expected `int`, got `string`",
      "location": {
        "file": "script.ls",
        "line": 15,
        "column": 23,
        "endLine": 15,
        "endColumn": 30
      },
      "help": "use `int(\"hello\")` to parse string as integer"
    }
  ],
  "warnings": [],
  "errorCount": 1,
  "warningCount": 0
}
```

### 5.3 LSP Error Reporting

Prepare error structures for Language Server Protocol compatibility:

```cpp
// Convert LambdaError to LSP Diagnostic
LSPDiagnostic err_to_lsp(LambdaError* error) {
    return (LSPDiagnostic) {
        .range = {
            .start = { error->location.line - 1, error->location.column - 1 },
            .end = { error->location.end_line - 1, error->location.end_column - 1 }
        },
        .severity = ERR_IS_SYNTAX(error->code) || ERR_IS_SEMANTIC(error->code) 
            ? LSP_ERROR : LSP_WARNING,
        .code = error->code,
        .source = "lambda",
        .message = error->message
    };
}
```

### 5.4 Warning System

Distinguish warnings from errors:

```cpp
typedef enum ErrorSeverity {
    SEVERITY_ERROR,
    SEVERITY_WARNING,
    SEVERITY_INFO,
    SEVERITY_HINT
} ErrorSeverity;

// Warning codes (negative to distinguish)
#define LWARN_UNUSED_VARIABLE      -1
#define LWARN_UNREACHABLE_CODE     -2
#define LWARN_IMPLICIT_ANY         -3
#define LWARN_DEPRECATED_FUNCTION  -4
```

### 5.5 Error Documentation

Create `doc/Lambda_Error_Reference.md` documenting all error codes with:
- Error code and name
- Description
- Common causes
- How to fix
- Examples

### 5.6 Fuzzy Testing for Error Handling

Extend fuzzy testing to verify:
- All error paths return proper error codes (not crashes)
- Error messages are valid strings (not null, not garbage)
- Stack traces don't cause memory issues

```bash
./test/fuzzy/test_fuzzy.sh --test-errors --duration=300
```

---

## 6. Implementation Plan

### Phase 1: Foundation (Week 1-2)
- [ ] Define `LambdaErrorCode` enum in `lambda/lambda_error.h`
- [ ] Implement `LambdaError` structure and basic API
- [ ] Add `SourceLocation` tracking in parser
- [ ] Create error code lookup tables

### Phase 2: Parser Integration (Week 3-4)
- [ ] Update `build_ast.cpp` to use structured errors
- [ ] Update `runner.cpp` error handling
- [ ] Integrate with existing `find_errors()` function
- [ ] Add source context extraction

### Phase 3: Runtime Integration (Week 5-6)
- [ ] Update `lambda-eval.cpp` runtime errors
- [ ] Implement stack trace capture (opt-in)
- [ ] Add CLI flags for stack traces
- [ ] Update error Item representation

### Phase 4: Testing (Week 7-8)
- [ ] Create negative test directory structure
- [ ] Write syntax error negative tests
- [ ] Write semantic error negative tests
- [ ] Write runtime error negative tests
- [ ] Add negative tests to CI pipeline

### Phase 5: Polish (Week 9-10)
- [ ] Add JSON error output format
- [ ] Write error documentation
- [ ] Add fuzzy testing for error paths
- [ ] Performance optimization for stack traces

---

## 7. Migration Strategy

### 7.1 Backward Compatibility

- Existing `ItemError` type preserved
- New error system wraps existing behavior initially
- Gradual migration of error call sites

### 7.2 Deprecation Path

1. **Immediate**: Add new error API, mark old patterns deprecated
2. **Next release**: Update all call sites to new API
3. **Following release**: Remove deprecated patterns

---

## 8. Success Metrics

- **Error code coverage**: 100% of errors have proper codes
- **Location accuracy**: 95%+ of errors have correct line/column
- **Negative test coverage**: 50+ negative test cases per category
- **Stack trace performance**: <5% overhead when enabled
- **User satisfaction**: Clear error messages rated 4+ in surveys

---

## Appendix A: Existing Validator Error Codes

The validator already has a structured error system (`ValidationErrorCode`):

```cpp
typedef enum ValidationErrorCode {
    VALID_ERROR_NONE = 0,
    VALID_ERROR_TYPE_MISMATCH,
    VALID_ERROR_MISSING_FIELD,
    VALID_ERROR_UNEXPECTED_FIELD,
    VALID_ERROR_NULL_VALUE,
    VALID_ERROR_INVALID_ELEMENT,
    VALID_ERROR_CONSTRAINT_VIOLATION,
    VALID_ERROR_REFERENCE_ERROR,
    VALID_ERROR_OCCURRENCE_ERROR,
    VALID_ERROR_CIRCULAR_REFERENCE,
    VALID_ERROR_PARSE_ERROR,
} ValidationErrorCode;
```

This should be unified with the new `LambdaErrorCode` system, mapping to the 2xx range for semantic errors.

---

## Appendix B: Current Error Handling Patterns

### Current Pattern 1: log_error + return
```cpp
log_error("Error message");
return ItemError;
```

### Current Pattern 2: Transpiler error_count
```cpp
tp->error_count++;
log_error("Error at line %d", line);
```

### Current Pattern 3: Validator errors
```cpp
ValidationError* err = create_validation_error(...);
result->errors = err;
```

All patterns should migrate to the unified `LambdaError` system.
