#pragma once

// js_runtime_state.hpp - shared JS runtime state surface.
//
// This header intentionally exposes only the mutable runtime-state capsule and
// legacy-name aliases used while the runtime is migrated away from free globals.

#include "js_runtime.h"
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

struct JsRuntimeState {
    Input* input = NULL;
    bool strict_mode = false;
    bool skip_accessor_dispatch = false;
    int array_sym_iter_ever_set = 0;

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
    Item super_this_value_stack[128] = {};
    int super_this_bound_depth = 0;
    Item* pending_call_args = NULL;
    int pending_call_argc = 0;
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
    bool exception_pending = false;
    Item exception_value = {0};
    char exception_msg_buf[1024] = {};
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
#define g_array_sym_iter_ever_set (js_runtime_state.array_sym_iter_ever_set)
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
#define js_super_this_value_stack (js_runtime_state.super_this_value_stack)
#define js_super_this_bound_depth (js_runtime_state.super_this_bound_depth)
#define js_pending_call_args (js_runtime_state.pending_call_args)
#define js_pending_call_argc (js_runtime_state.pending_call_argc)
#define js_array_method_real_this (js_runtime_state.array_method_real_this)
#define js_dispatch_as_array_method (js_runtime_state.dispatch_as_array_method)
#define js_cached_object_proto (js_runtime_state.cached_object_proto)
#define js_resolving_object_proto (js_runtime_state.resolving_object_proto)
#define js_private_field_initializing (js_runtime_state.private_field_initializing)
#define js_eval_initializer_context (js_runtime_state.eval_initializer_context)
#define js_exception_pending (js_runtime_state.exception_pending)
#define js_exception_value (js_runtime_state.exception_value)
#define js_exception_msg_buf (js_runtime_state.exception_msg_buf)
#define js_pending_args_is_strict (js_runtime_state.pending_args_is_strict)
#define js_pending_args_callee (js_runtime_state.pending_args_callee)
#define _trace_last_fn (js_runtime_state.trace_last_fn)
#define _trace_last_fn_len (js_runtime_state.trace_last_fn_len)
#define _trace_total_calls (js_runtime_state.trace_total_calls)
