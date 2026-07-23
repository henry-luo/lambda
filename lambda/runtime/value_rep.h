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
} FnErrorLane;
enum {
    FN_RETURN_HOME_NORMAL = 1u << 0,
    FN_RETURN_HOME_ERROR = 1u << 1,
};
