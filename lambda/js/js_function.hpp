#pragma once

// Canonical layout for GC-owned JavaScript function objects.  Keep this
// lightweight so property and builtin modules do not need runtime internals.

#include "js_runtime.h"
#include "../lambda-data.hpp"

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
    int16_t formal_length;
    Item* module_vars;
    Item home_global;
    String* source_text;
    bool eval_initializer_context;
    Item* with_env;
    int with_env_depth;
    String* vm_stack_filename;
    String* vm_stack_source;
    int64_t vm_stack_line_offset;
    int64_t vm_stack_column_offset;
    const char** ctor_prop_names;
    int* ctor_prop_lens;
    int ctor_prop_count;
};

#define JS_FUNCTION_LAYOUT_MAGIC 0x4A53464Eu
static_assert(offsetof(JsFunction, func_ptr) == 8,
              "JsFunction prefix must preserve the compiled-function ABI");
static_assert(offsetof(JsFunction, bound_this_store) == 48,
              "JsFunction bound-this slot must preserve the shared ABI");

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
#define JS_FUNC_FLAG_DATA_VIEW_ACCESSOR JS_FUNC_FLAG_METHOD
