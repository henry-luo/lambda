#pragma once

// js_runtime_state.hpp - shared JS runtime state surface.
//
// This header intentionally exposes only the mutable runtime-state capsule and
// legacy-name aliases used while the runtime is migrated away from free globals.

#include "js_runtime.h"
#include "js_class.h"
#include "../lambda-data.hpp"

#define JS_REGEXP_MAX_PAREN 9
struct JsRegexpLastMatch {
    String* input;
    String* match;
    String* groups[JS_REGEXP_MAX_PAREN];
    int group_count;
    int match_start;
    int match_end;
};

struct JsExceptionState {
    bool pending = false;
    // slots[0] is the GC-visible Item; slots[1] is its scalar payload home.
    // Keep wide scalar payloads owned by the runtime state rather than in a
    // standalone GC cell that outlives the throwing activation.
    Item slots[2] = {};
    char msg_buf[1024] = {};
};

typedef void (*JsRootRangeResetFn)(void* owner);

// A fixed Item range whose address outlives every heap epoch.  Registration is
// deliberately owned by the range so clients cannot publish a live Item before
// the collector knows where that Item resides.
struct JsRootRange {
    Item* slots = NULL;
    int slot_count = 0;
    uint64_t roots_epoch = 0;
    const char* name = NULL;
    void* reset_owner = NULL;
    JsRootRangeResetFn reset = NULL;
    bool reset_registered = false;
};

// Use this only for actual single-Item LIFO storage.  Clients with replacement,
// replay, or multi-field records retain those semantic operations themselves.
struct JsItemStack {
    JsRootRange roots = {};
    int depth = 0;
};

bool js_root_range_ensure_registered(JsRootRange* range);
void js_root_range_clear(JsRootRange* range);
bool js_root_range_register_reset(JsRootRange* range, void* owner,
                                  JsRootRangeResetFn reset);
void js_root_range_reset_all(void);
bool js_item_stack_push(JsItemStack* stack, Item value);
Item js_item_stack_top(const JsItemStack* stack);
void js_item_stack_pop(JsItemStack* stack);
void js_item_stack_clear(JsItemStack* stack);
void js_item_stack_shrink(JsItemStack* stack, int depth);

#define JS_EVAL_SOURCE_STACK_MAX 16

// Source records span a runtime eval or a VM-originated function call. Their
// Item fields are separate exact ranges; POD metadata is never scanned as an
// Item merely because it is adjacent to source roots.
struct JsEvalSourceState {
    Item filename_slots[JS_EVAL_SOURCE_STACK_MAX] = {};
    Item code_slots[JS_EVAL_SOURCE_STACK_MAX] = {};
    int64_t line_offset_slots[JS_EVAL_SOURCE_STACK_MAX] = {};
    int64_t column_offset_slots[JS_EVAL_SOURCE_STACK_MAX] = {};
    bool compact_slots[JS_EVAL_SOURCE_STACK_MAX] = {};
    int depth = 0;
    JsRootRange filename_roots = {};
    JsRootRange code_roots = {};
};

#define JS_EVAL_ENV_BIND_MAX 512
#define JS_EVAL_ENV_FRAME_MAX 32
#define JS_EVAL_LOCAL_BIND_MAX 512
#define JS_EVAL_LOCAL_FRAME_MAX 64
#define JS_EVAL_LEXICAL_BIND_MAX 512
#define JS_EVAL_IMMUTABLE_BIND_MAX 512
#define JS_EVAL_PRIVATE_BIND_MAX 256

// A direct-eval bridge exists for one generated eval call.  Item columns are
// structure-of-arrays so each exact GC range excludes the bool metadata.
struct JsEvalBridgeState {
    Item env_keys[JS_EVAL_ENV_BIND_MAX] = {};
    Item env_old_values[JS_EVAL_ENV_BIND_MAX] = {};
    bool env_had_own[JS_EVAL_ENV_BIND_MAX] = {};
    bool env_from_journal[JS_EVAL_ENV_BIND_MAX] = {};
    int env_count = 0;
    int env_frame_marks[JS_EVAL_ENV_FRAME_MAX] = {};
    int env_frame_depth = 0;

    Item global_lexical_keys[JS_EVAL_ENV_BIND_MAX] = {};
    Item global_lexical_old_values[JS_EVAL_ENV_BIND_MAX] = {};
    bool global_lexical_had_own[JS_EVAL_ENV_BIND_MAX] = {};
    int global_lexical_count = 0;
    int global_lexical_frame_marks[JS_EVAL_ENV_FRAME_MAX] = {};
    int global_lexical_frame_depth = 0;

    Item private_unscoped_keys[JS_EVAL_PRIVATE_BIND_MAX] = {};
    Item private_scoped_keys[JS_EVAL_PRIVATE_BIND_MAX] = {};
    int private_count = 0;
    int private_frame_marks[JS_EVAL_LOCAL_FRAME_MAX] = {};
    int private_frame_depth = 0;

    JsRootRange env_key_roots = {};
    JsRootRange env_old_value_roots = {};
    JsRootRange global_lexical_key_roots = {};
    JsRootRange global_lexical_old_value_roots = {};
    JsRootRange private_unscoped_key_roots = {};
    JsRootRange private_scoped_key_roots = {};
};

typedef struct JsEvalLocalFrameMarks {
    int local_mark;
    int lexical_mark;
    int immutable_mark;
} JsEvalLocalFrameMarks;

// Caller-local records survive multiple direct eval calls in one generated
// function. They deliberately do not share bridge or source depths.
struct JsEvalLocalState {
    Item keys[JS_EVAL_LOCAL_BIND_MAX] = {};
    Item values[JS_EVAL_LOCAL_BIND_MAX] = {};
    int count = 0;
    JsEvalLocalFrameMarks frame_marks[JS_EVAL_LOCAL_FRAME_MAX] = {};
    int frame_depth = 0;
    Item lexical_keys[JS_EVAL_LEXICAL_BIND_MAX] = {};
    int lexical_count = 0;
    Item immutable_keys[JS_EVAL_IMMUTABLE_BIND_MAX] = {};
    int immutable_count = 0;

    JsRootRange key_roots = {};
    JsRootRange value_roots = {};
    JsRootRange lexical_key_roots = {};
    JsRootRange immutable_key_roots = {};
};

struct JsEvalState {
    JsEvalSourceState source = {};
    JsEvalBridgeState bridge = {};
    JsEvalLocalState local = {};
};

void js_eval_state_reset(JsEvalState* state);
void js_eval_state_assert_clear(const JsEvalState* state, const char* reset_name);

struct JsIntrinsicState {
    // Prototype cache slots are precise GC roots so moving collection updates
    // every cached Item; name Items are active-name-pool owned.
    uint64_t* prototype_roots[JS_CLASS__COUNT] = {};
    bool prototype_resolving[JS_CLASS__COUNT] = {};
    Item constructor_names[JS_CLASS__COUNT] = {};
    Item prototype_name = {0};
    uint64_t mutation_versions[JS_CLASS__COUNT] = {};
    uint64_t mutation_serial = 1;
    uint64_t owner_heap_epoch = 0;
    uint32_t initialization_depth = 0;
    int array_sym_iter_ever_set = 0;
    int array_proto_push_ever_set = 0;
    int array_writable_methods_ever_set = 0;
};

struct JsRuntimeState {
    Input* input = NULL;
    bool strict_mode = false;
    bool skip_accessor_dispatch = false;
    JsIntrinsicState intrinsics = {};
    JsEvalState eval = {};

    Item module_vars[JS_MAX_MODULE_VARS] = {};
    Item* active_module_vars = module_vars;
    int module_var_count = 0;
    uint64_t heap_epoch = 1;

    JsRegexpLastMatch regexp_last_match = {};

    Item current_this = {0};
    Item proxy_receiver = {0};
    Item new_target = {0};
    Item pending_new_target = {0};
    bool has_pending_new_target = false;
    bool super_this_bound_stack[128] = {};
    Item super_this_value_slots[128] = {};
    JsItemStack super_this_values = {};
    Item* pending_call_args = NULL;
    int pending_call_argc = 0;
    const char* pending_call_source = NULL;
    int pending_call_source_len = 0;
    Item array_method_real_this = {0};
    // Js54 P5: true when the currently-dispatched builtin was invoked through
    // an Array.prototype function object (no JS_FUNC_FLAG_TYPED_ARRAY_METHOD).
    // Several methods (every, fill, slice, forEach, ...) share the same
    // JS_BUILTIN_ARR_* id between Array.prototype and TypedArray.prototype.
    // TypedArray.prototype.X on an OOB receiver throws via ValidateTypedArray;
    // Array.prototype.X.call(ta_oob, ...) must NOT throw — it uses
    // LengthOfArrayLike which yields 0 and the method silently no-ops.
    // Default is false (i.e. TA-mode) so that the direct-method-call fast
    // path used by the MIR JIT (which bypasses js_call_function and calls
    // js_map_method directly) still throws on OOB. js_call_function /
    // js_invoke_fn flip this to true when the calling fn lacks the TA flag.
    bool dispatch_as_array_method = false;
    Map* cached_object_proto = NULL;
    bool resolving_object_proto = false;
    bool private_field_initializing = false;
    bool eval_initializer_context = false;
    JsExceptionState exception = {};
    int pending_args_is_strict = 0;
    Item pending_args_callee = {0};

    const char* trace_last_fn = "(none)";
    int trace_last_fn_len = 6;
    int trace_total_calls = 0;
};

extern JsRuntimeState js_runtime_state;

static inline Item*& js_active_module_vars_ref() {
    if (!js_runtime_state.active_module_vars) {
        js_runtime_state.active_module_vars = js_runtime_state.module_vars;
    }
    return js_runtime_state.active_module_vars;
}

#define js_input (js_runtime_state.input)
#define js_strict_mode (js_runtime_state.strict_mode)
#define js_skip_accessor_dispatch (js_runtime_state.skip_accessor_dispatch)
#define js_intrinsic_state (js_runtime_state.intrinsics)
#define g_array_sym_iter_ever_set (js_intrinsic_state.array_sym_iter_ever_set)
#define g_array_proto_push_ever_set (js_intrinsic_state.array_proto_push_ever_set)
#define g_array_writable_methods_ever_set (js_intrinsic_state.array_writable_methods_ever_set)
#define js_module_vars (js_runtime_state.module_vars)
#define js_active_module_vars (js_active_module_vars_ref())
#define js_module_var_count (js_runtime_state.module_var_count)
#define js_heap_epoch (js_runtime_state.heap_epoch)
#define js_regexp_last_match (js_runtime_state.regexp_last_match)
#define js_current_this (js_runtime_state.current_this)
#define js_proxy_receiver (js_runtime_state.proxy_receiver)
#define js_new_target (js_runtime_state.new_target)
#define js_pending_new_target (js_runtime_state.pending_new_target)
#define js_has_pending_new_target (js_runtime_state.has_pending_new_target)
#define js_super_this_bound_stack (js_runtime_state.super_this_bound_stack)
#define js_super_this_value_stack (js_runtime_state.super_this_values.roots.slots)
#define js_super_this_bound_depth (js_runtime_state.super_this_values.depth)
#define js_pending_call_args (js_runtime_state.pending_call_args)
#define js_pending_call_argc (js_runtime_state.pending_call_argc)
#define js_pending_call_source (js_runtime_state.pending_call_source)
#define js_pending_call_source_len (js_runtime_state.pending_call_source_len)
#define js_array_method_real_this (js_runtime_state.array_method_real_this)
#define js_dispatch_as_array_method (js_runtime_state.dispatch_as_array_method)
#define js_cached_object_proto (js_runtime_state.cached_object_proto)
#define js_resolving_object_proto (js_runtime_state.resolving_object_proto)
#define js_private_field_initializing (js_runtime_state.private_field_initializing)
#define js_eval_initializer_context (js_runtime_state.eval_initializer_context)
#define js_exception_pending (js_runtime_state.exception.pending)
#define js_exception_slots (js_runtime_state.exception.slots)
#define js_exception_value (js_exception_slots[0])
#define js_exception_msg_buf (js_runtime_state.exception.msg_buf)
#define js_pending_args_is_strict (js_runtime_state.pending_args_is_strict)
#define js_pending_args_callee (js_runtime_state.pending_args_callee)
#define _trace_last_fn (js_runtime_state.trace_last_fn)
#define _trace_last_fn_len (js_runtime_state.trace_last_fn_len)
#define _trace_total_calls (js_runtime_state.trace_total_calls)
