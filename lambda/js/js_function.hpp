#pragma once

// Canonical layout for GC-owned JavaScript function objects.  Keep this
// lightweight so property and builtin modules do not need runtime internals.

#include "js_runtime.h"
#include "../lambda-data.hpp"

struct JsFunction;

// Per-callee call entry. Choosing the calling protocol once, at function
// finalization, lets each entry contain only the steps its callee's shape
// actually needs instead of re-deciding inside one shared dispatcher body on
// every call. The generic dispatcher is itself an entry, so every function has
// one and there is no "miss" path.
typedef Item (*JsCallEntry)(Item fn_item, Item this_val, Item* args, int argc,
                            uint64_t* result_home, bool args_prerooted);

struct JsFunction {
    TypeId type_id;
    uint32_t layout_magic;
    void* func_ptr;
    int param_count;
    Item* env;
    int env_size;
    Item prototype;
    Item bound_this_store[2];
    Item* bound_args;
    int bound_argc;
    String* name;
    int builtin_id;
    Item properties_map;
    uint16_t flags;
    uint8_t call_lane_kind;
    uint8_t special_ctor_kind;
    int16_t formal_length;
    NameId special_ctor_name_id;
    JsCallEntry invoke;
    uint32_t module_state_id;
    Item home_global;
    Item home_class;
    String* source_text;
    bool eval_initializer_context;
    Item* with_env;
    int with_env_depth;
    String* vm_stack_filename;
    String* vm_stack_source;
    int64_t vm_stack_line_offset;
    int64_t vm_stack_column_offset;
    Context* runtime_context;
};

#define JS_FUNCTION_LAYOUT_MAGIC 0x4A53464Eu
static_assert(offsetof(JsFunction, func_ptr) == 8,
              "JsFunction prefix must preserve the compiled-function ABI");
static_assert(offsetof(JsFunction, bound_this_store) == 48,
              "JsFunction bound-this slot must preserve the shared ABI");

static inline void js_function_init_native_module_scope(JsFunction* fn) {
    if (!fn) return;
    // Pool allocation zeroes this field, but zero is a valid module id. Native
    // wrappers have no compiled module scope and must not switch callers to it.
    fn->module_state_id = UINT32_MAX;
}

static inline void js_function_set_bound_this(JsFunction* fn, Item value) {
    owned_item_slot_store(fn->bound_this_store, 1, 0, value);
}

static inline Item js_function_get_bound_this(JsFunction* fn) {
    return owned_item_slot_read(fn->bound_this_store, 1, 0, false);
}

#define JS_FUNC_FLAG_GENERATOR 1
#define JS_FUNC_FLAG_ARROW     2
#define JS_FUNC_FLAG_TYPED_ARRAY_METHOD 4
#define JS_FUNC_FLAG_STRICT    8
#define JS_FUNC_FLAG_HAS_BOUND_THIS 16
#define JS_FUNC_FLAG_METHOD    32
#define JS_FUNC_FLAG_ASYNC_GEN 64
#define JS_FUNC_FLAG_ASYNC     128
#define JS_FUNC_FLAG_DERIVED_CTOR 256
#define JS_FUNC_FLAG_MIR_PUBLIC_ABI 512
#define JS_FUNC_FLAG_USES_WITH 1024
#define JS_FUNC_FLAG_READS_THIS 2048
#define JS_FUNC_FLAG_READS_NEW_TARGET 4096
#define JS_FUNC_FLAG_ANALYSIS_KNOWN 8192
#define JS_FUNC_FLAG_MIR_CONTEXT_ABI 16384
#define JS_FUNC_FLAG_DATA_VIEW_ACCESSOR JS_FUNC_FLAG_METHOD

enum JsFunctionCallLaneKind : uint8_t {
    JS_CALL_LANE_GENERIC = 0,
    JS_CALL_LANE_ORDINARY = 1,
    JS_CALL_LANE_METHOD_HOME = 2,
};

// Call dispatch caches the classification against the NameId of fn->name;
// pointer identity is not stable across the Input/runtime materialization
// boundary covered by D4.6.1v2.
enum JsSpecialCtorKind : uint8_t {
    JS_SPECIAL_CTOR_UNCHECKED = 0,
    JS_SPECIAL_CTOR_NONE = 1,
    JS_SPECIAL_CTOR_DATE = 2,
    JS_SPECIAL_CTOR_FUNCTION = 3,
    JS_SPECIAL_CTOR_GENERATOR_FUNCTION = 4,
    JS_SPECIAL_CTOR_ASYNC_GENERATOR_FUNCTION = 5,
    JS_SPECIAL_CTOR_ASYNC_FUNCTION = 6,
};

// Classify against the current name and remember the key. Cheap enough to run
// inline on the rare miss; never changes which callees the dispatcher treats
// as special — the sufficient conditions stay at the use sites.
static inline uint8_t js_function_special_ctor_kind(JsFunction* fn) {
    if (!fn) return JS_SPECIAL_CTOR_NONE;
    NameId name_id = fn->name ? name_ref_id(fn->name) : NAME_ID_NONE;
    if (name_id != NAME_ID_NONE &&
        fn->special_ctor_kind != JS_SPECIAL_CTOR_UNCHECKED &&
        fn->special_ctor_name_id == name_id) {
        return fn->special_ctor_kind;
    }
    uint8_t kind = JS_SPECIAL_CTOR_NONE;
    String* name = fn->name;
    if (name) {
        switch (name->len) {
        case 4:
            if (memcmp(name->chars, "Date", 4) == 0) kind = JS_SPECIAL_CTOR_DATE;
            break;
        case 8:
            if (memcmp(name->chars, "Function", 8) == 0) kind = JS_SPECIAL_CTOR_FUNCTION;
            break;
        case 13:
            if (memcmp(name->chars, "AsyncFunction", 13) == 0)
                kind = JS_SPECIAL_CTOR_ASYNC_FUNCTION;
            break;
        case 17:
            if (memcmp(name->chars, "GeneratorFunction", 17) == 0)
                kind = JS_SPECIAL_CTOR_GENERATOR_FUNCTION;
            break;
        case 22:
            if (memcmp(name->chars, "AsyncGeneratorFunction", 22) == 0)
                kind = JS_SPECIAL_CTOR_ASYNC_GENERATOR_FUNCTION;
            break;
        default: break;
        }
    }
    fn->special_ctor_name_id = name_id;
    fn->special_ctor_kind = kind;
    return kind;
}
