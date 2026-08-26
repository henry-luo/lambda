#pragma once

// Canonical layout for GC-owned JavaScript function objects.  Keep this
// lightweight so property and builtin modules do not need runtime internals.

#include "js_runtime.h"
#include "../lambda-data.hpp"

struct JsFunction;
struct AstFuncNode;
struct JsScript;
struct JsInterpEnv;

enum JsFunctionBodyKind : uint8_t {
    JS_FUNCTION_BODY_CODE = 0,
    JS_FUNCTION_BODY_AST = 1,
};

// Per-callee call entry. Choosing the calling protocol once, at function
// finalization, lets each entry contain only the steps its callee's shape
// actually needs instead of re-deciding inside one shared dispatcher body on
// every call. The generic dispatcher is itself an entry, so every function has
// one and there is no "miss" path.
typedef Item (*JsCallEntry)(Item fn_item, Item this_val, Item* args, int argc,
                            uint64_t* result_home, bool args_prerooted);

// Construction is a separate capability under D6.2.2v2. The caller supplies
// newTarget explicitly; an absent entry is the complete IsConstructor answer.
typedef Item (*JsConstructEntry)(Item fn_item, Item* args, int argc,
                                 Item new_target, uint64_t* result_home,
                                 bool args_prerooted);

union JsNativeTarget {
    JsNativeP0 p0;
    JsNativeP1 p1;
    JsNativeP2 p2;
    JsNativeP3 p3;
    JsNativeP4 p4;
    JsNativeP5 p5;
    JsNativeP6 p6;
    JsNativeP7 p7;
    JsNativeP8 p8;
    JsNativeSpan span;
    JsNativeThisSpan this_span;
    uint64_t bits;
};

enum JsNativeCallPolicy : uint8_t {
    JS_NATIVE_CALL_NONE = 0,
    JS_NATIVE_CALL_FIXED = 1,
    JS_NATIVE_CALL_REST = 2,
    JS_NATIVE_CALL_SPAN = 3,
    JS_NATIVE_CALL_THIS_SPAN = 4,
    JS_NATIVE_CALL_BODY = 6,
};

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
    Item bound_target;
    String* name;
    int catalog_id;
    Item properties_map;
    uint16_t flags;
    uint8_t intrinsic_class;
    int16_t formal_length;
    // Concrete TypedArray constructors carry their element policy directly;
    // display names and catalog IDs are never executable selectors (D6.2.2v2).
    uint8_t typed_array_element_type_plus_one;
    uint8_t pool_pointer_roots_registered;
    JsCallEntry invoke;
    JsConstructEntry construct;
    JsNativeCallBody native_call;
    JsNativeConstructBody native_construct;
    JsNativeTarget native_target;
    uint8_t native_arity;
    uint8_t native_policy;
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
    // Source classes are functions with an explicit construct capability.
    // Keeping their constructor body and instance prototype here removes the
    // former callable-Map protocol and leaves ordinary properties observable.
    Item class_constructor;
    Item class_instance_prototype;
    Item class_superclass;
    Context* runtime_context;
    // AST bodies retain source-level semantics while using the ordinary JS
    // call kernel. The lexical environment is a precise GC edge.
    AstFuncNode* ast_function;
    JsScript* ast_script;
    JsInterpEnv* interp_env;
    Item ast_lexical_this;
    Item ast_lexical_new_target;
    // AST execution facts are derived once when the closure is created. They
    // are immutable because the source AST and lexical function form are too.
    bool ast_has_direct_eval;
    bool ast_uses_arguments;
    bool ast_tail_reuse_safe;
    uint8_t body_kind;
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
#define JS_FUNC_FLAG_CLASS_CONSTRUCTOR 32768

#define JS_FUNC_POOL_POINTER_ROOTS_REGISTERED 1
#define JS_FUNC_FLAG_DATA_VIEW_ACCESSOR JS_FUNC_FLAG_METHOD
