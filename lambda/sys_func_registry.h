// sys_func_registry.h — C-compatible header for the unified system function registry
// Defines SysFuncInfo (AST metadata + JIT function pointers) and JitImport (runtime imports).
// This is the single source of truth for all JIT-importable function registrations.
#pragma once

#include "lambda.h"  // for Type*, TypeId, SysFunc, fn_ptr
#include "value_rep.h"
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
    static void* __attribute__((unused)) _sys_func_dummy(void) { return (void*)0; }
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
    bool is_async;              // call is a suspension seed for Lambda pn analysis
} SysFuncInfo;

// GC effect and representation metadata consumed by MIR emitters. Unknown
// entries are deliberately conservative: they remain MAY_GC and their value
// classes are inferred only by the legacy physical-type fallback.
typedef enum JitGcEffect {
    JIT_EFFECT_MAY_GC = 0,
    JIT_EFFECT_NO_GC,
} JitGcEffect;

typedef enum JitReentryEffect {
    JIT_REENTRY_UNKNOWN = 0,
    JIT_REENTRY_NO,
    JIT_REENTRY_YES,
} JitReentryEffect;

// Normalized contracts separate ABI, GC, exception, and number-stack effects;
// emitters materialize this descriptor from compact registry rows.
typedef enum JitExceptionEffect {
    JIT_EXCEPTION_MAY_SET = 0,
    JIT_EXCEPTION_PRESERVES,
    JIT_EXCEPTION_CLEARS,
} JitExceptionEffect;
typedef enum JitNumberStackEffect {
    JIT_NUMBER_STACK_MAY_ALLOCATE = 0,
    JIT_NUMBER_STACK_PRESERVES,
} JitNumberStackEffect;
typedef enum JitArgEffect {
    JIT_ARG_BORROWED = 0,
    JIT_ARG_MAY_CAPTURE = 1u << 0,
    JIT_ARG_MAY_WRITE_THROUGH = 1u << 1,
    JIT_ARG_PERSISTENT_STORE = 1u << 2,
    JIT_ARG_EFFECT_UNKNOWN = 1u << 3,
} JitArgEffect;
typedef enum JitReturnTransport {
    JIT_RETURN_NONE = 0,
    JIT_RETURN_MIR_RESULT,
    JIT_RETURN_CONTEXT_ERROR,
} JitReturnTransport;
typedef struct JitAbiValue {
    JitAbiRep abi_rep;
    JitValueClass value_class;
} JitAbiValue;
typedef struct JitAbiArg {
    JitAbiValue value;
    uint16_t effects;
} JitAbiArg;
typedef struct JitReturnLane {
    JitAbiValue value;
    JitReturnTransport transport;
    ScalarReturnClass scalar_class;
    bool may_use_scalar_return_home;
} JitReturnLane;
typedef struct JitCallEffects {
    JitGcEffect gc;
    JitReentryEffect reentry;
    JitExceptionEffect exception;
    JitNumberStackEffect number_stack;
} JitCallEffects;
typedef struct JitCallMetadata {
    JitCallEffects effects;
    JitReturnLane normal_result;
    JitReturnLane error_result;
    const JitAbiArg* abi_args;
    uint16_t abi_arg_count;
    uint16_t source_arg_count;
    int16_t scalar_return_home_arg_index;
    uint8_t scalar_home_lane_mask;
    uint32_t flags;
} JitCallMetadata;

#define JIT_ARG_CLASS_BITS 3
#define JIT_ARG_CLASS(index, value_class) \
    ((uint32_t)(value_class) << ((index) * JIT_ARG_CLASS_BITS))

typedef struct JitImportMetadata {
    JitGcEffect gc_effect;
    JitReentryEffect reentry_effect;
    JitValueClass ret_class;
    uint32_t arg_classes;
    uint32_t flags;
} JitImportMetadata;

enum {
    // The returned Item is inline or already persistent/canonical and therefore
    // cannot expose an activation-temporary scalar payload.
    JIT_IMPORT_RESULT_SCALAR_STABLE = 1u << 0,
    JIT_IMPORT_NUMBER_STACK_PRESERVES = 1u << 1,
    JIT_IMPORT_ARGS_BORROWED_AUDITED = 1u << 2,
};

static inline JitValueClass jit_import_arg_class(
        const JitImportMetadata* metadata, int index) {
    if (!metadata || index < 0 || index >= 8) return JIT_VALUE_UNKNOWN;
    return (JitValueClass)((metadata->arg_classes >>
        (index * JIT_ARG_CLASS_BITS)) & 7u);
}

// JIT import entry: maps name to function pointer and is the single source of
// effect/representation metadata for MIR import emission.
typedef struct JitImport {
    const char* name;
    fn_ptr func;
    JitImportMetadata metadata;
} JitImport;

// System function definitions (AST metadata + JIT pointers)
extern SysFuncInfo sys_func_defs[];
extern const int sys_func_def_count;
fn_ptr find_dynamic_sys_func_import(const char* c_func_name);

// Runtime JIT imports (non-sys-func entries: operators, runtime infra, JS, etc.)
extern JitImport jit_runtime_imports[];
extern const int jit_runtime_import_count;
bool jit_import_get_metadata(const char* name, JitImportMetadata* metadata);
bool jit_import_validate_no_gc_allowlist(void);

#ifdef __cplusplus
}
#endif
