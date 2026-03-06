// sys_func_registry.h — C-compatible header for the unified system function registry
// Defines SysFuncInfo (AST metadata + JIT function pointers) and JitImport (runtime imports).
// This is the single source of truth for all JIT-importable function registrations.
#pragma once

#include "lambda.h"  // for Type*, TypeId, SysFunc, fn_ptr
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// LAMBDA_STATIC guard: when defined (dylib/input builds), runtime function
// pointers are unavailable. Use FPTR()/NPTR() macros to resolve to a dummy
// stub, so sys_func_defs[] compiles without linking the full runtime.
// ============================================================================
#ifdef LAMBDA_STATIC
    static void* _sys_func_dummy(void) { return (void*)0; }
    #define FPTR(x)  (fn_ptr) _sys_func_dummy  // stub for func_ptr
    #define NPTR(x)  (fn_ptr) _sys_func_dummy  // stub for native_func_ptr
#else
    #define FPTR(x)  (fn_ptr)(x)               // real function pointer
    #define NPTR(x)  (fn_ptr)(x)               // real native function pointer
#endif

// C-level return type convention for system functions
typedef enum CRetType {
    C_RET_ITEM = 0,    // returns boxed Item (default, most sys funcs)
    C_RET_RETITEM,     // returns RetItem {Item value; LambdaError* err} (can_raise functions)
    C_RET_INT64,       // returns raw int64_t (fn_len, fn_index_of, bitwise, etc.)
    C_RET_DOUBLE,      // returns raw double (pn_clock)
    C_RET_BOOL,        // returns Bool/uint8_t (fn_contains, fn_starts_with, etc.)
    C_RET_STRING,      // returns String* (fn_string, fn_format1/2)
    C_RET_SYMBOL,      // returns Symbol* (fn_name, fn_symbol1)
    C_RET_DTIME,       // returns DateTime/uint64_t (datetime funcs)
    C_RET_TYPE_PTR,    // returns Type* (fn_type)
    C_RET_CONTAINER,   // returns container pointer: Map*, List*, Array*, etc.
} CRetType;

// C-level argument convention for system functions
typedef enum CArgConvention {
    C_ARG_ITEM = 0,    // all arguments are boxed Items (default)
    C_ARG_NATIVE,      // arguments are native C types (int64_t for bitwise ops)
} CArgConvention;

// System function metadata + JIT import pointer
typedef struct SysFuncInfo {
    SysFunc fn;
    const char* name;
    int arg_count;  // -1 for variable args
    Type* return_type;
    bool is_proc;   // is procedural
    bool is_overloaded;
    bool is_method_eligible;    // can be called as obj.method() style
    TypeId first_param_type;    // expected type of first param (LMD_TYPE_ANY for any)
    bool can_raise;             // function may return error (T^ return type)
    CRetType c_ret_type;        // C-level return type (default: C_RET_ITEM)
    CArgConvention c_arg_conv;  // C-level argument convention (default: C_ARG_ITEM)
    const char* c_func_name;    // C function name emitted by transpiler ("fn_len", "pn_print", etc.)
    fn_ptr func_ptr;            // actual C function pointer for JIT import resolution (NULL if unimplemented)
    const char* native_c_name;  // native C math function for optimization ("fabs", "sin", etc.), NULL if none
    fn_ptr native_func_ptr;     // actual C function pointer for native math optimization
    bool native_returns_float;  // True if native function returns double
    int native_arg_count;       // Number of args for native function (1 or 2), 0 if not applicable
} SysFuncInfo;

// JIT import entry: maps name to function pointer for MIR import resolution
typedef struct JitImport {
    const char* name;
    fn_ptr func;
} JitImport;

// System function definitions (AST metadata + JIT pointers)
extern SysFuncInfo sys_func_defs[];
extern const int sys_func_def_count;

// Runtime JIT imports (non-sys-func entries: operators, runtime infra, JS, etc.)
extern JitImport jit_runtime_imports[];
extern const int jit_runtime_import_count;

#ifdef __cplusplus
}
#endif
