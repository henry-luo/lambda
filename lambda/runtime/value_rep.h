#pragma once

#include <stdint.h>
// Semantic value identity is independent of its machine carrier. These types
// are C-compatible because the runtime registry and both MIR lowerers consume
// the same immutable contracts.
typedef enum JitValueClass {
    JIT_VALUE_UNKNOWN = 0,
    JIT_VALUE_BOXED_ITEM,
    JIT_VALUE_RAW_GC_POINTER,
    JIT_VALUE_NON_GC_SCALAR,
    JIT_VALUE_RAW_NON_GC_POINTER,
} JitValueClass;
typedef enum JitAbiRep {
    JIT_ABI_VOID = 0,
    JIT_ABI_ITEM,
    JIT_ABI_I64,
    JIT_ABI_F64,
    JIT_ABI_POINTER,
} JitAbiRep;
typedef enum ScalarReturnClass {
    SCALAR_RETURN_NONE = 0,
    SCALAR_RETURN_I64,
    SCALAR_RETURN_U64,
    SCALAR_RETURN_F64,
    SCALAR_RETURN_DYNAMIC,
} ScalarReturnClass;
typedef enum ValueRep {
    VALUE_REP_NONE = 0,
    VALUE_REP_ITEM,
    // lambda's compact int carrier is an i64 register, but it is not a
    // machine integer: the lane reserves the i64 extremes for poison/null.
    // keeping it distinct prevents an INT64_MAX machine quantity from being
    // reinterpreted as an int-lane sentinel (D2.4.2–D2.4.3).
    VALUE_REP_INT_LANE,
    // compiler-only quantities never cross a Lambda semantic boundary through
    // em_require_rep(); they are named here so physical i64 values cannot be
    // mistaken for Lambda values (D2.4.1–D2.4.3).
    VALUE_REP_MACHINE_I64,
    VALUE_REP_MACHINE_U64,
    VALUE_REP_I64,
    VALUE_REP_U64,
    VALUE_REP_F64,
    VALUE_REP_RAW_GC_POINTER,
    VALUE_REP_RAW_NON_GC_POINTER,
} ValueRep;
typedef enum BindingStorage {
    BINDING_STORAGE_REGISTER = 0,
    BINDING_STORAGE_SCOPE_ENV,
    BINDING_STORAGE_MODULE,
    BINDING_STORAGE_PERSISTENT,
} BindingStorage;
typedef enum FnEntryKind {
    FN_ENTRY_PUBLIC_WRAPPER = 0,
    FN_ENTRY_BOXED_BODY,
    FN_ENTRY_NATIVE_BODY,
    FN_ENTRY_RESUME,
} FnEntryKind;
typedef enum FnErrorLane {
    FN_ERROR_LANE_NONE = 0,
    FN_ERROR_LANE_CONTEXT_ITEM,
    // Return-value convention v3 (RV9): the error travels as the second MIR
    // result of a shape-4 native return, `ItemNull` meaning "no error".
    FN_ERROR_LANE_PAIR,
} FnErrorLane;
// Return-value convention v3 (RV1, `vibe/Lambda_Design_Compiling_Return_Value.md`,
// formal spec D5.2.1v3 / D8.4.2v3). The shape is a pure function of the
// declared signature (RV2) — never inferred per call site, never read back from
// MIR state — and it is the ONE descriptor every emitter, wrapper, `fn->invoke`
// entry, interpreter bridge and cached module reads (RV10).
typedef enum FnReturnShape {
    RETURN_SHAPE_ITEM = 0,      // [item]            boxed, provably wide-free
    RETURN_SHAPE_ITEM_SCALAR,   // [item, scalar]    boxed, may be pending
    RETURN_SHAPE_NATIVE,        // [native]          typed, infallible (TE-17)
    RETURN_SHAPE_NATIVE_ERROR,  // [native, error]   typed `^E`
} FnReturnShape;

// How lane 2 travels. The SHAPE says whether a companion exists; the TRANSPORT
// says where it lives (RV10a — the two are separate axes, and RV12's
// "companion location, not companion register" is exactly this distinction).
// A C prototype cannot receive a second MIR result on any platform, so every
// entry reachable from C uses the context slot while JIT-to-JIT entries use
// the register.
typedef enum FnCompanionTransport {
    FN_COMPANION_NONE = 0,      // shape 1 / 3: there is no lane 2
    FN_COMPANION_RESULT_REG,    // v3: second MIR result (JIT to JIT)
    FN_COMPANION_CONTEXT_SLOT,  // v3: Context::mir_companion_slot (RV12)
} FnCompanionTransport;

// Shape 2 is the UNIVERSAL shape: every dynamic call site may assume it, so a
// shape-1 callee still speaks it (writing a dummy lane 2).
static inline bool fn_return_shape_is_pair(FnReturnShape shape) {
    return shape == RETURN_SHAPE_ITEM_SCALAR || shape == RETURN_SHAPE_NATIVE_ERROR;
}
// Only shape 2 can produce a pending Item on lane 1; shape 4's lane 2 is an
// error Item, not a wide payload.
static inline bool fn_return_shape_may_be_pending(FnReturnShape shape) {
    return shape == RETURN_SHAPE_ITEM_SCALAR;
}
enum {
    FN_RETURN_HOME_NORMAL = 1u << 0,
    FN_RETURN_HOME_ERROR = 1u << 1,
};
