// sys_func_registry.h — C-compatible header for the unified system function registry
// Defines SysFuncInfo (AST metadata + JIT function pointers) and JitImport (runtime imports).
// This is the single source of truth for all JIT-importable function registrations.
#pragma once

#include "../lambda.h"  // for Type*, TypeId, SysFunc, fn_ptr
#include "value_rep.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// IO static-values guard: when defined (dylib/input builds), runtime function
// pointers are unavailable. Use FPTR()/NPTR() macros to resolve to a dummy
// stub, so sys_func_defs[] compiles without linking the full runtime.
// ============================================================================
#ifdef LAMBDA_IO_STATIC_VALUES
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
    C_RET_INT64,       // returns raw int64_t (fn_len, bitwise, and machine operations)
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
    // Value-lane effects are independent of `can_raise`/T^.
    // `success_type` describes a successful result; `may_return_error` marks
    // ordinary ItemError values that callers may contain with `or`.
    Type* success_type;
    bool may_return_error;
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
    JIT_EXCEPTION_SETS,
} JitExceptionEffect;
// PRESERVES means the call leaves `Context.side_number_top` where it found
// it — the postcondition a caller needs to answer "did this call leave
// anything above my pre-call top?". `em_call_import` reads it to skip the
// wide-scalar adopt sequence entirely (RV14a).
typedef enum JitNumberStackEffect {
    JIT_NUMBER_STACK_MAY_ALLOCATE = 0,
    JIT_NUMBER_STACK_PRESERVES,
} JitNumberStackEffect;
// The zero value must stay MAY_ALLOCATE: an unaudited registry row, or any
// zero-initialized JitCallEffects, then decodes as the CONSERVATIVE answer.
// Reversing the order would make silence mean "preserves", which the reader
// above would take as permission to elide work the callee actually needs.
LAMBDA_STATIC_ASSERT((int)JIT_NUMBER_STACK_MAY_ALLOCATE == 0,
    "MAY_ALLOCATE must be the zero value so unaudited rows stay conservative");
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
    // v3 (RV10): mirrored from the callee's FnReturnAnalysis so a call site
    // never recomputes the shape from its own local facts.
    FnReturnShape return_shape;
    uint32_t flags;
} JitCallMetadata;

#define JIT_ARG_CLASS_BITS 3
#define JIT_ARG_CLASS(index, value_class) \
    ((uint32_t)(value_class) << ((index) * JIT_ARG_CLASS_BITS))

#define JIT_ARG_EFFECT_BITS 4
// Zero stays the conservative default for legacy rows; stored effects are +1.
#define JIT_ARG_EFFECT(index, effect) \
    ((uint32_t)((effect) + 1u) << ((index) * JIT_ARG_EFFECT_BITS))

typedef struct JitImportMetadata {
    JitGcEffect gc_effect;
    JitReentryEffect reentry_effect;
    JitValueClass ret_class;
    uint32_t arg_classes;
    uint32_t flags;
    JitExceptionEffect exception_effect;
    uint32_t arg_effects;
} JitImportMetadata;

enum {
    // The returned Item is inline/persistent, or a scalar payload was already
    // written into an explicit caller-donated home by this import.
    JIT_IMPORT_RESULT_SCALAR_STABLE = 1u << 0,
    JIT_IMPORT_NUMBER_STACK_PRESERVES = 1u << 1,
    JIT_IMPORT_ARGS_BORROWED_AUDITED = 1u << 2,
};

// A raw scalar has no ERROR-tag transport.  Keep its catalog contract in one
// named initializer so every audited JS fast-path row states PRESERVES.
#define JIT_IMPORT_RAW_SCALAR_PRESERVES \
    {JIT_EFFECT_MAY_GC, JIT_REENTRY_UNKNOWN, JIT_VALUE_NON_GC_SCALAR, \
     0, 0, JIT_EXCEPTION_PRESERVES, 0}

// Void imports have no merged-Item return transport and must be audited as
// preserving the lane rather than silently relying on the emitter's fold.
#define JIT_IMPORT_VOID_PRESERVES \
    {JIT_EFFECT_MAY_GC, JIT_REENTRY_UNKNOWN, JIT_VALUE_NON_GC_SCALAR, \
     0, 0, JIT_EXCEPTION_PRESERVES, 0}

static inline JitValueClass jit_import_arg_class(
        const JitImportMetadata* metadata, int index) {
    if (!metadata || index < 0 || index >= 8) return JIT_VALUE_UNKNOWN;
    return (JitValueClass)((metadata->arg_classes >>
        (index * JIT_ARG_CLASS_BITS)) & 7u);
}

static inline JitArgEffect jit_import_arg_effect(
        const JitImportMetadata* metadata, int index) {
    if (!metadata || index < 0 || index >= 8) return JIT_ARG_EFFECT_UNKNOWN;
    uint32_t encoded = (metadata->arg_effects >>
        (index * JIT_ARG_EFFECT_BITS)) & 15u;
    return encoded ? (JitArgEffect)(encoded - 1u) : JIT_ARG_EFFECT_UNKNOWN;
}

// JIT import entry: maps name to function pointer and is the single source of
// effect/representation metadata for MIR import emission.
typedef struct JitImport {
    const char* name;
    fn_ptr func;
    JitImportMetadata metadata;
} JitImport;

// The C-level result type of a sys func's registered entry point. `return_type`
// in SysFuncInfo is the Lambda-level semantic type, NOT the C type: functions
// with the same Lambda return can return String* in C while others return Item,
// so the SysFunc enum is the discriminator. Shared by MIR lowering and the T0
// interpreter so both box a given entry's result identically.
static inline TypeId sysfunc_c_ret_type_id(const SysFuncInfo* info) {
    if (!info) return LMD_TYPE_ANY;
    switch (info->fn) {
    // len() stays a raw machine count. Search/ordinal calls return Item so
    // their public null result cannot be mistaken for an integer sentinel.
    case SYSFUNC_LEN:
        return LMD_TYPE_INT;
    // These keep an int64_t C result. `int64()` because its Lambda type IS
    // int64; the bitwise/shift family because bit reinterpretation is a
    // machine operation on machine words, not number math — its result is
    // converted into the int lane at the boundary below.
    case SYSFUNC_INT64:
    case SYSFUNC_BAND: case SYSFUNC_BOR: case SYSFUNC_BXOR:
    case SYSFUNC_BNOT: case SYSFUNC_SHL: case SYSFUNC_SHR:
        return LMD_TYPE_INT64;
    // C functions returning Bool (uint8_t)
    case SYSFUNC_CONTAINS: case SYSFUNC_STARTS_WITH: case SYSFUNC_ENDS_WITH:
    case SYSFUNC_EXISTS:
        return LMD_TYPE_BOOL;
    // C functions returning String*
    case SYSFUNC_STRING: case SYSFUNC_FORMAT1: case SYSFUNC_FORMAT2:
        return LMD_TYPE_STRING;
    // C functions returning Symbol*
    case SYSFUNC_NAME: case SYSFUNC_SYMBOL:
        return LMD_TYPE_SYMBOL;
    // C functions returning Type*
    case SYSFUNC_TYPE:
        return LMD_TYPE_TYPE;
    // C functions returning DateTime (uint64_t)
    case SYSFUNC_DATETIME: case SYSFUNC_DATETIME0:
    case SYSFUNC_DATE: case SYSFUNC_DATE0: case SYSFUNC_DATE3:
    case SYSFUNC_TIME: case SYSFUNC_TIME0: case SYSFUNC_TIME3:
    case SYSFUNC_JUSTNOW:
        return LMD_TYPE_DTIME;
    // C functions returning double
    case SYSPROC_CLOCK:
        return LMD_TYPE_FLOAT;
    default:
        return LMD_TYPE_ANY;  // returns Item, no boxing needed
    }
}

// System function definitions (AST metadata + JIT pointers)
extern SysFuncInfo sys_func_defs[];
extern const int sys_func_def_count;
fn_ptr find_dynamic_sys_func_import(const char* c_func_name);

// Runtime JIT imports (non-sys-func entries: operators, runtime infra, JS, etc.)
extern JitImport jit_runtime_imports[];
extern const int jit_runtime_import_count;
bool jit_import_get_metadata(const char* name, JitImportMetadata* metadata);
bool jit_import_validate_no_gc_allowlist(void);

// Registers module-owned JIT helper descriptors before compilation. The MIR
// resolver copies descriptors into its cached import map, so generated calls
// retain direct targets and never perform a per-call module lookup.
bool jit_register_module_imports(const JitImport* imports, int import_count,
                                 const char* owner_name);

#ifdef __cplusplus
}
#endif
