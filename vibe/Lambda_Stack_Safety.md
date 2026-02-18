# Lambda Stack Overflow Protection Proposal

## Overview

This proposal outlines a comprehensive approach to protect Lambda programs from stack overflow caused by infinite or deep recursion. The solution evolves in two phases:

**Phase 1** (initial): Stack Pointer Check — per-call SP comparison with ~3 instruction overhead on unsafe functions. ✅ **Completed & superseded.**

**Phase 2** (replacement): Signal-Based Handler — zero per-call overhead; the OS catches stack overflow via `sigaltstack`/SEH. ✅ **Completed.** Graceful `E308` error on infinite recursion; 233/233 baseline tests pass.

Both phases are complemented by:

1. **Static Safety Analysis** - Compile-time analysis to classify functions
2. **Tail Recursion Elimination** - Transform tail-recursive calls to prevent stack growth

## Goals

- **Zero per-call overhead** via OS-level signal/exception handling (Phase 2)
- **Graceful error handling** instead of crash (SIGSEGV)
- **Clear error messages** with function name for debugging
- **Cross-platform support** (macOS/Linux/Windows, x64/ARM64)

---

## 1. Phase 1: Stack Pointer Check (Runtime) — *Superseded by Phase 2*

> **Note:** Phase 1 uses inline SP checks with ~3 instructions overhead per unsafe call.
> Phase 2 below replaces this with zero-overhead signal-based handling.

### 1.1 Mechanism

Use inline assembly to read the stack pointer and compare against a cached limit:

```cpp
// lambda-stack.h

#ifndef LAMBDA_STACK_H
#define LAMBDA_STACK_H

#include <stdint.h>
#include <stdbool.h>

// Thread-local stack bounds (initialized once per thread)
extern __thread uintptr_t _lambda_stack_limit;
extern __thread uintptr_t _lambda_stack_base;

// One-time initialization per thread
void lambda_stack_init(void);

// Fast inline check (~3 instructions)
static inline bool lambda_stack_check(void) {
    uintptr_t sp;

#if defined(__x86_64__) || defined(_M_X64)
    #if defined(_MSC_VER)
        sp = (uintptr_t)_AddressOfReturnAddress();
    #else
        __asm__ volatile("mov %%rsp, %0" : "=r"(sp));
    #endif

#elif defined(__aarch64__) || defined(_M_ARM64)
    #if defined(_MSC_VER)
        sp = (uintptr_t)_AddressOfReturnAddress();
    #else
        __asm__ volatile("mov %0, sp" : "=r"(sp));
    #endif

#else
    // Fallback: use local variable address (slightly less accurate)
    char stack_var;
    sp = (uintptr_t)&stack_var;
#endif

    return sp < _lambda_stack_limit;
}

// Error handler - called when overflow detected
void lambda_stack_overflow_error(const char* func_name);

#endif // LAMBDA_STACK_H
```

### 1.2 Implementation

```cpp
// lambda-stack.cpp

#include "lambda-stack.h"
#include "../lib/log.h"

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

// Thread-local storage for stack bounds
__thread uintptr_t _lambda_stack_limit = 0;
__thread uintptr_t _lambda_stack_base = 0;

// Safety margin (64KB reserved for cleanup/error handling)
#define STACK_SAFETY_MARGIN (64 * 1024)

void lambda_stack_init(void) {
    if (_lambda_stack_limit != 0) return;  // Already initialized

#if defined(__APPLE__)
    pthread_t self = pthread_self();
    void* stack_addr = pthread_get_stackaddr_np(self);
    size_t stack_size = pthread_get_stacksize_np(self);
    _lambda_stack_base = (uintptr_t)stack_addr;
    _lambda_stack_limit = (uintptr_t)stack_addr - stack_size + STACK_SAFETY_MARGIN;

#elif defined(__linux__)
    pthread_t self = pthread_self();
    pthread_attr_t attr;
    pthread_getattr_np(self, &attr);
    void* stack_addr;
    size_t stack_size;
    pthread_attr_getstack(&attr, &stack_addr, &stack_size);
    pthread_attr_destroy(&attr);
    _lambda_stack_base = (uintptr_t)stack_addr + stack_size;
    _lambda_stack_limit = (uintptr_t)stack_addr + STACK_SAFETY_MARGIN;

#elif defined(_WIN32)
    ULONG_PTR low, high;
    GetCurrentThreadStackLimits(&low, &high);
    _lambda_stack_base = (uintptr_t)high;
    _lambda_stack_limit = (uintptr_t)low + STACK_SAFETY_MARGIN;

#else
    // Conservative fallback: assume 1MB stack, use current SP as reference
    char stack_var;
    _lambda_stack_base = (uintptr_t)&stack_var;
    _lambda_stack_limit = _lambda_stack_base - (1024 * 1024) + STACK_SAFETY_MARGIN;
#endif

    log_debug("Stack initialized: base=%p, limit=%p, size=%zu KB",
              (void*)_lambda_stack_base, (void*)_lambda_stack_limit,
              (_lambda_stack_base - _lambda_stack_limit) / 1024);
}

void lambda_stack_overflow_error(const char* func_name) {
    log_error("Stack overflow in function '%s' - possible infinite recursion",
              func_name ? func_name : "<unknown>");
    // Note: We have STACK_SAFETY_MARGIN bytes to work with here
}
```

### 1.3 Generated Code Pattern

For unsafe functions, the transpiler generates a prologue check:

```cpp
// Generated for unsafe function 'factorial'
Item __lambda_factorial(Item n) {
    if (lambda_stack_check()) {
        lambda_stack_overflow_error("factorial");
        return ItemError;
    }

    // ... actual function body ...
}
```

Assembly output (~3 instructions overhead):
```asm
__lambda_factorial:
    ; Stack check prologue
    mov     rax, [_lambda_stack_limit@TLVP]   ; load TLS limit
    cmp     rsp, rax                           ; compare SP
    jb      .Lstack_overflow                   ; branch if overflow

    ; Normal function body follows...

.Lstack_overflow:
    lea     rdi, [rip + .Lfunc_name]
    call    lambda_stack_overflow_error
    ; return ItemError
```

---

## 2. Phase 2: Signal-Based Stack Overflow Handler (Runtime — Zero Overhead)

### 2.1 Motivation

Phase 1's SP check adds ~3 instructions to every unsafe function call. Phase 2 eliminates this per-call overhead entirely by leveraging the OS's built-in stack guard page mechanism:

- **macOS/Linux**: `sigaltstack` + `sigaction(SIGSEGV)` — the kernel delivers a signal when the stack hits the guard page
- **Windows**: Structured Exception Handling (SEH) with `EXCEPTION_STACK_OVERFLOW`

The result is **zero instructions** added to generated function code. Overflow is caught by the OS at the hardware/MMU level.

### 2.2 How It Works

1. At program startup, install a signal/exception handler on an **alternate signal stack**
2. When recursion exhausts the main stack and hits the guard page, the OS raises SIGSEGV (Unix) or `EXCEPTION_STACK_OVERFLOW` (Windows)
3. The handler runs on the alternate stack (so it has room to execute), records the error, and performs a non-local jump (`siglongjmp`) back to a safe recovery point
4. The runtime reports a graceful "stack overflow" error instead of crashing

### 2.3 Header

```c
// lambda-stack.h

#ifndef LAMBDA_STACK_H
#define LAMBDA_STACK_H

#include <stdbool.h>
#include <setjmp.h>

// Initialize stack overflow handler (call once at startup)
void lambda_stack_init(void);

// Recovery jump buffer — set before running user code
// Usage: if (sigsetjmp(_lambda_recovery_point, 1)) { /* overflow occurred */ }
extern __thread sigjmp_buf _lambda_recovery_point;
extern __thread bool _lambda_stack_overflow_flag;
extern __thread const char* _lambda_overflow_func_name;

#endif // LAMBDA_STACK_H
```

### 2.4 Implementation — macOS / Linux

```c
// lambda-stack.c

#include "lambda-stack.h"
#include "../lib/log.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif

// Thread-local recovery state
__thread sigjmp_buf _lambda_recovery_point;
__thread bool _lambda_stack_overflow_flag = false;
__thread const char* _lambda_overflow_func_name = NULL;

// Alternate signal stack size (64KB — enough for handler + cleanup)
#define ALT_STACK_SIZE (64 * 1024)

#if defined(__APPLE__) || defined(__linux__)

static void stack_overflow_handler(int sig, siginfo_t *info, void *ctx) {
    (void)sig; (void)info; (void)ctx;
    _lambda_stack_overflow_flag = true;
    log_error("Stack overflow detected — possible infinite recursion");
    siglongjmp(_lambda_recovery_point, 1);
}

void lambda_stack_init(void) {
    // 1. Allocate and register alternate signal stack
    stack_t ss;
    ss.ss_sp = malloc(ALT_STACK_SIZE);
    if (!ss.ss_sp) {
        log_error("Failed to allocate alternate signal stack");
        return;
    }
    ss.ss_size = ALT_STACK_SIZE;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) != 0) {
        log_error("sigaltstack failed");
        free(ss.ss_sp);
        return;
    }

    // 2. Install SIGSEGV handler on the alternate stack
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = stack_overflow_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, NULL) != 0) {
        log_error("sigaction(SIGSEGV) failed");
        return;
    }

#if defined(__linux__)
    // Linux also uses SIGBUS for some stack faults
    if (sigaction(SIGBUS, &sa, NULL) != 0) {
        log_error("sigaction(SIGBUS) failed");
    }
#endif

    log_debug("Stack overflow handler installed (alt stack = %zu KB)",
              ALT_STACK_SIZE / 1024);
}

#endif // __APPLE__ || __linux__
```

### 2.5 Implementation — Windows

```c
// lambda-stack.c (Windows section)

#if defined(_WIN32)
#include <windows.h>

static LONG WINAPI stack_overflow_seh_handler(EXCEPTION_POINTERS *ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
        _lambda_stack_overflow_flag = true;
        log_error("Stack overflow detected — possible infinite recursion");
        // Reset stack overflow state so the thread can continue
        _resetstkoflw();
        longjmp(_lambda_recovery_point, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void lambda_stack_init(void) {
    SetUnhandledExceptionFilter(stack_overflow_seh_handler);
    log_debug("Stack overflow SEH handler installed");
}

#endif // _WIN32
```

### 2.6 Runtime Integration

The recovery point is set once before executing user code — not per function call:

```c
// In lambda-eval.cpp or main.cpp — before running user script

lambda_stack_init();  // One-time setup

Item run_lambda_program(AstNode* program) {
    if (sigsetjmp(_lambda_recovery_point, 1)) {
        // Stack overflow was caught — we land here after siglongjmp
        _lambda_stack_overflow_flag = false;
        log_error("Execution aborted due to stack overflow");
        return ItemError;
    }

    // Normal execution — no per-call overhead
    return eval(program);
}
```

### 2.7 Phase 1 vs Phase 2 Comparison

| Aspect | Phase 1 (SP Check) | Phase 2 (Signal/SEH) |
|--------|-------------------|----------------------|
| Per-call overhead | ~3 instructions | **0 instructions** |
| Detection mechanism | Inline SP comparison | OS guard page / MMU |
| Error granularity | Reports exact function name | Reports at point of overflow |
| Setup cost | TLS init per thread | One-time handler install |
| Platform support | All (inline asm) | All (POSIX signals + SEH) |
| Recovery mechanism | Return `ItemError` from call | `siglongjmp` to recovery point |
| Static analysis needed | Yes (to skip check for safe fns) | No (no per-call code emitted) |

### 2.8 Caveats

1. **`siglongjmp` from signal handler**: This is POSIX-defined behavior when using `sigsetjmp`/`siglongjmp`. The signal mask is restored. However, any intermediate stack frames are abandoned — destructors in C++ frames will **not** run. Since Lambda's generated code is C (via MIR), this is acceptable.

2. **Non-stack SIGSEGV**: A null pointer dereference also raises SIGSEGV. The handler should verify that the fault address is near the stack limit. A simple heuristic:

```c
static void stack_overflow_handler(int sig, siginfo_t *info, void *ctx) {
    // Check if fault address is near the stack guard region
    uintptr_t fault_addr = (uintptr_t)info->si_addr;
    pthread_t self = pthread_self();

#if defined(__APPLE__)
    uintptr_t stack_top = (uintptr_t)pthread_get_stackaddr_np(self);
    size_t stack_size = pthread_get_stacksize_np(self);
    uintptr_t stack_bottom = stack_top - stack_size;
#elif defined(__linux__)
    pthread_attr_t attr;
    void* addr; size_t sz;
    pthread_getattr_np(self, &attr);
    pthread_attr_getstack(&attr, &addr, &sz);
    pthread_attr_destroy(&attr);
    uintptr_t stack_bottom = (uintptr_t)addr;
#endif

    // If fault is within 2 pages of stack bottom, it's a stack overflow
    if (fault_addr >= stack_bottom - 8192 && fault_addr <= stack_bottom + 8192) {
        _lambda_stack_overflow_flag = true;
        log_error("Stack overflow detected — possible infinite recursion");
        siglongjmp(_lambda_recovery_point, 1);
    }

    // Not a stack overflow — re-raise as crash
    signal(sig, SIG_DFL);
    raise(sig);
}
```

3. **Thread safety**: Each thread needs its own `_lambda_recovery_point` (via `__thread`). The `sigaltstack` call should be made per-thread if multi-threaded, since the alternate stack is thread-local state.

---

## 3. Static Safety Analysis

### 3.1 Function Classification

At compile time, classify each function as **safe** or **unsafe**:

| Classification | Definition | Stack Check |
|---------------|------------|-------------|
| **Safe** | Cannot cause unbounded recursion | ❌ None |
| **Unsafe** | May cause unbounded recursion | ✅ Required |

### 3.2 Safety Rules

```
SAFE if:
  1. Function contains no calls (leaf function)
  2. Function only calls system functions that don't take callbacks
  3. Function only calls other SAFE functions

UNSAFE if:
  1. Function calls itself (direct recursion)
  2. Function calls another user function that is UNSAFE
  3. Function is called through higher-order functions (map, filter, etc.)
  4. Function calls system functions that accept callbacks
```

### 3.3 System Function Classification

```cpp
// System functions that accept user callbacks - mark callers UNSAFE
const char* CALLBACK_SYS_FUNCS[] = {
    "map", "filter", "reduce", "fold", "foldl", "foldr",
    "find", "find_index", "any", "all", "none",
    "sort_by", "group_by", "partition",
    "foreach", "transform",
    NULL
};

// System functions without callbacks - always SAFE to call
const char* SAFE_SYS_FUNCS[] = {
    // Arithmetic
    "abs", "ceil", "floor", "round", "sqrt", "pow", "log", "exp",
    "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
    "min", "max", "clamp",

    // String
    "len", "substr", "concat", "split", "join", "trim",
    "upper", "lower", "replace", "contains", "starts_with", "ends_with",

    // Collection (no callbacks)
    "head", "tail", "take", "drop", "slice", "reverse",
    "append", "prepend", "insert", "remove",
    "keys", "values", "entries", "has_key",
    "sum", "avg", "count", "unique", "flatten",

    // Type conversion
    "int", "float", "string", "bool", "array", "list",

    // I/O
    "print", "input", "format", "parse",

    NULL
};
```

### 3.4 Analysis Algorithm

```cpp
// In build_ast.cpp or a new analysis pass

enum FunctionSafety {
    SAFETY_UNKNOWN,    // Not yet analyzed
    SAFETY_ANALYZING,  // Currently being analyzed (cycle detection)
    SAFETY_SAFE,       // Proven safe
    SAFETY_UNSAFE      // Requires stack check
};

struct FunctionInfo {
    const char* name;
    FunctionSafety safety;
    AstNode* definition;
    std::vector<const char*> callees;  // Functions this function calls
};

class SafetyAnalyzer {
    std::unordered_map<std::string, FunctionInfo> functions;

public:
    void analyze_module(AstNode* module) {
        // Phase 1: Collect all function definitions and their callees
        collect_functions(module);

        // Phase 2: Analyze safety for each function
        for (auto& [name, info] : functions) {
            analyze_function(name);
        }

        // Phase 3: Mark functions passed to HOFs as unsafe
        mark_callback_functions_unsafe(module);
    }

    FunctionSafety analyze_function(const std::string& name) {
        auto it = functions.find(name);
        if (it == functions.end()) {
            // System function - check if it's in safe list
            return is_safe_sys_func(name.c_str()) ? SAFETY_SAFE : SAFETY_UNSAFE;
        }

        FunctionInfo& info = it->second;

        if (info.safety != SAFETY_UNKNOWN) {
            return info.safety;
        }

        // Cycle detection: if we're already analyzing this function,
        // it means there's recursion
        if (info.safety == SAFETY_ANALYZING) {
            info.safety = SAFETY_UNSAFE;
            return SAFETY_UNSAFE;
        }

        info.safety = SAFETY_ANALYZING;

        // Check all callees
        bool all_safe = true;
        for (const char* callee : info.callees) {
            if (analyze_function(callee) == SAFETY_UNSAFE) {
                all_safe = false;
                break;
            }
        }

        info.safety = all_safe ? SAFETY_SAFE : SAFETY_UNSAFE;
        return info.safety;
    }

    void mark_callback_functions_unsafe(AstNode* module) {
        // Find all calls to HOF system functions
        // Mark any function passed as argument as UNSAFE
        visit_calls(module, [this](AstNode* call) {
            if (is_callback_sys_func(call->func_name)) {
                for (auto arg : call->arguments) {
                    if (arg->node_type == AST_IDENTIFIER) {
                        auto it = functions.find(arg->name);
                        if (it != functions.end()) {
                            it->second.safety = SAFETY_UNSAFE;
                        }
                    } else if (arg->node_type == AST_LAMBDA) {
                        // Anonymous lambdas passed to HOFs are unsafe
                        // (they need to be marked in codegen)
                    }
                }
            }
        });
    }
};
```

### 3.5 Example Analysis

```lambda
// SAFE: No function calls
fn add(a, b) => a + b

// SAFE: Only calls safe system functions
fn double_all(arr) => arr.map(fn(x) => x * 2)  // Wait - map takes callback!

// Actually UNSAFE: Calls map() which takes a callback
fn double_all(arr) => arr.map(fn(x) => x * 2)

// UNSAFE: Direct recursion
fn factorial(n) => if (n <= 1) 1 else n * factorial(n - 1)

// UNSAFE: Calls unsafe function
fn compute(n) => factorial(n) + 1

// SAFE: Only calls safe functions and operators
fn distance(x1, y1, x2, y2) {
    let dx = x2 - x1
    let dy = y2 - y1
    sqrt(dx * dx + dy * dy)
}
```

Analysis result:
```
add           -> SAFE   (leaf function)
factorial     -> UNSAFE (self-recursion)
compute       -> UNSAFE (calls factorial)
distance      -> SAFE   (only calls sqrt which is safe)
double_all    -> UNSAFE (lambda passed to map)
```

### 3.6 Transpiler Integration

> **Phase 1**: Safety analysis drives which functions get SP check prologues.
> **Phase 2**: With signal-based handling, no per-call code is emitted. Safety analysis
> remains useful for TCO decisions and future optimizations.

```cpp
// In transpile-mir.cpp (Phase 1 — SP check approach)

void transpile_function(AstNode* func) {
    FunctionSafety safety = safety_analyzer.get_safety(func->name);

    if (safety == SAFETY_UNSAFE) {
        // Phase 1: Emit stack check prologue (removed in Phase 2)
        emit_mir("if (lambda_stack_check()) {");
        emit_mir("    lambda_stack_overflow_error(\"%s\");", func->name);
        emit_mir("    return ItemError;");
        emit_mir("}");
    }

    // Emit rest of function body
    transpile_function_body(func);
}
```

---

## 4. Tail Recursion Elimination

### 4.1 Definition

A **tail call** is a function call that is the last operation before returning:

```lambda
// Tail recursive - can be optimized
fn factorial_tail(n, acc) =>
    if (n <= 1) acc
    else factorial_tail(n - 1, n * acc)  // Tail position

// NOT tail recursive - cannot be optimized
fn factorial(n) =>
    if (n <= 1) 1
    else n * factorial(n - 1)  // Result is multiplied, not in tail position
```

### 4.2 Detection

```cpp
bool is_tail_position(AstNode* node, AstNode* parent) {
    switch (parent->node_type) {
        case AST_FUNCTION:
        case AST_LAMBDA:
            // Direct child of function body is tail position
            return true;

        case AST_IF:
            // Both branches of if-else are tail positions
            return node == parent->then_branch || node == parent->else_branch;

        case AST_BLOCK:
            // Last expression in block is tail position
            return node == parent->statements.back();

        case AST_LET:
            // Body of let is tail position
            return node == parent->body;

        default:
            return false;
    }
}

bool is_self_tail_call(AstNode* call, AstNode* current_func) {
    if (call->node_type != AST_CALL) return false;
    if (call->callee->node_type != AST_IDENTIFIER) return false;
    return strcmp(call->callee->name, current_func->name) == 0;
}
```

### 4.3 Transformation

Transform tail-recursive functions into loops:

**Before (AST):**
```lambda
fn factorial_tail(n, acc) =>
    if (n <= 1) acc
    else factorial_tail(n - 1, n * acc)
```

**After (transformed):**
```cpp
Item factorial_tail(Item n, Item acc) {
    __tail_call_entry:

    if (fn_le(n, int_to_item(1))) {
        return acc;
    } else {
        // Update parameters for "recursive" call
        Item __new_n = fn_sub(n, int_to_item(1));
        Item __new_acc = fn_mul(n, acc);
        n = __new_n;
        acc = __new_acc;
        goto __tail_call_entry;
    }
}
```

### 4.4 Transpiler Implementation

```cpp
// In transpile-mir.cpp

void transpile_tail_recursive_function(AstNode* func) {
    // Emit function signature
    emit_mir("Item %s(", func->name);
    emit_parameters(func->params);
    emit_mir(") {");

    // Emit tail call entry label
    emit_mir("    __tail_call_entry:");

    // Transform body, replacing tail self-calls with parameter updates + goto
    transpile_with_tail_call_transform(func->body, func);

    emit_mir("}");
}

void transpile_with_tail_call_transform(AstNode* node, AstNode* func) {
    if (is_self_tail_call(node, func)) {
        // Transform: factorial_tail(n - 1, n * acc)
        // Into: n = n - 1; acc = n * acc; goto __tail_call_entry;

        AstNode* call = node;

        // Compute new argument values (in temp variables to handle dependencies)
        for (int i = 0; i < call->arguments.size(); i++) {
            emit_mir("    Item __new_%s = ", func->params[i]->name);
            transpile_expr(call->arguments[i]);
            emit_mir(";");
        }

        // Assign new values to parameters
        for (int i = 0; i < call->arguments.size(); i++) {
            emit_mir("    %s = __new_%s;", func->params[i]->name, func->params[i]->name);
        }

        // Jump back to entry
        emit_mir("    goto __tail_call_entry;");
        return;
    }

    // Regular transpilation for non-tail-call nodes
    transpile_expr_default(node);
}
```

### 4.5 Benefits

| Function Type | Without TCO | With TCO |
|---------------|-------------|----------|
| `factorial(10000)` | Stack overflow | ✅ Works (constant stack) |
| `foldl` over 1M items | Stack overflow | ✅ Works |
| Mutual tail recursion | Stack overflow | ❌ Not supported (future work) |

---

## 5. Implementation Plan

### Phase 1: Stack Pointer Check — Initial Protection ✅ Done

1. ✅ Created `lambda-stack.h` and `lambda-stack.cpp`
2. ✅ Implemented inline SP check with TLS-cached stack limit
3. ✅ Added `lambda_stack_init()` call in `runner_setup_context()`
4. ✅ Emitted `LAMBDA_STACK_CHECK` prologue for non-TCO functions in `transpile.cpp`
5. ✅ Tested on macOS (superseded by Phase 2)

### Phase 2: Signal-Based Handler — Replace SP Check ✅ Done

1. ✅ `lambda-stack.cpp`: `sigaltstack` + `sigaction(SIGSEGV)` with `SA_ONSTACK` (macOS/Linux)
2. ✅ `lambda-stack.cpp`: SEH `EXCEPTION_STACK_OVERFLOW` handler (Windows)
3. ✅ `runner.cpp`: `sigsetjmp` recovery point in `execute_script_and_create_output()`
4. ✅ `lambda-stack.cpp`: `is_stack_overflow_fault()` heuristic (64KB window around guard page)
5. ✅ `transpile.cpp`: Removed `LAMBDA_STACK_CHECK` prologue and `stack_check_code` — zero per-call overhead
6. ✅ Enabled `RuntimeError_StackOverflow` gtest; 233/233 baseline tests pass

### Phase 3: Static Safety Analysis (Week 3)

1. Implement `SafetyAnalyzer` class
2. Classify all system functions (safe vs callback-accepting)
3. Integrate with AST builder
4. Add `--dump-safety` flag for debugging
5. Safety info used for future optimizations (not needed for Phase 2 stack check)

### Phase 4: Tail Recursion Elimination (Week 4)

1. Implement tail position detection
2. Implement self-tail-call transformation
3. Add `--no-tco` flag for debugging
4. Test with recursive algorithms (factorial, fibonacci, fold)

### Phase 5: Testing & Documentation (Week 5)

1. Add unit tests for each component
2. Benchmark: confirm zero overhead on function calls with Phase 2
3. Add fuzz tests with deep/infinite recursion
4. Verify graceful error message output
5. Document in Lambda Reference

---

## 6. Expected Results

### Performance

| Scenario | Phase 1 (SP Check) | Phase 2 (Signal/SEH) |
|----------|-------------------|----------------------|
| All function calls | ~3 instructions (unsafe only) | **0 instructions** |
| Tail-recursive calls | 0 (converted to goto) | 0 (converted to goto) |
| Stack overflow detection | Inline comparison | OS guard page (hardware) |

### Memory

| Component | Memory Usage |
|-----------|--------------|
| Alternate signal stack | 64 KB per thread |
| Recovery jump buffer (TLS) | ~200 bytes per thread |
| Safety analysis | Compile-time only |
| TCO | No runtime cost |

### Safety

| Scenario | Before | After |
|----------|--------|-------|
| Infinite recursion | SIGSEGV crash | Graceful error + message |
| Deep recursion (10K+) | May crash | Error or TCO success |
| Mutual recursion | SIGSEGV crash | Graceful error |

---

## 7. Future Enhancements

1. **Mutual tail recursion** - Detect and optimize `f() -> g() -> f()` patterns
2. **Trampoline for callbacks** - Handle recursion through HOF callbacks
3. **Configurable stack size** - Allow `--stack-size=N` flag
4. **Stack usage profiling** - Track max stack depth per function
5. **Continuation-passing style (CPS)** - Transform all calls to constant stack space

---

## 8. References

- [Tail Call Optimization in JavaScript Engines](https://webkit.org/blog/6240/ecmascript-6-proper-tail-calls-in-webkit/)
- [LLVM Tail Call Optimization](https://llvm.org/docs/CodeGenerator.html#tail-call-optimization)
- [GCC Stack Checking](https://gcc.gnu.org/onlinedocs/gccint/Stack-Checking.html)
- [pthread Stack Management](https://man7.org/linux/man-pages/man3/pthread_attr_getstack.3.html)
- [sigaltstack(2)](https://man7.org/linux/man-pages/man2/sigaltstack.2.html)
- [Windows SEH - EXCEPTION_STACK_OVERFLOW](https://learn.microsoft.com/en-us/windows/win32/debug/structured-exception-handling)
