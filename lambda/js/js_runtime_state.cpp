#include "js_runtime_internal.hpp"

JsRuntimeState js_runtime_state;
extern __thread EvalContext* context;

static void js_ensure_module_vars_gc_rooted() {
    static struct gc_heap* rooted_gc = NULL;
    if (!context || !context->heap || !context->heap->gc) return;
    if (rooted_gc == context->heap->gc) return;
    heap_register_gc_root_range((uint64_t*)js_module_vars, JS_MAX_MODULE_VARS);
    rooted_gc = context->heap->gc;
}

// Forward declaration for regex compilation cache reset (defined near JsRegexData)
void js_regex_cache_reset();

extern "C" void js_set_strict_mode(int64_t strict) {
    js_strict_mode = (strict != 0);
}

// v24: throw TypeError in strict mode for property write violations
void js_strict_throw_property_error(const char* reason, const char* prop_name, int prop_len) {
    if (!js_strict_mode) return;
    char msg[512];
    if (prop_name && prop_len > 0) {
        snprintf(msg, sizeof(msg), "Cannot %s property '%.*s' of object", reason, prop_len > 200 ? 200 : prop_len, prop_name);
    } else {
        snprintf(msg, sizeof(msg), "Cannot %s property of object", reason);
    }
    Item err_name = (Item){.item = s2it(heap_create_name("TypeError"))};
    Item err_msg = (Item){.item = s2it(heap_create_name(msg))};
    Item error = js_new_error_with_name(err_name, err_msg);
    js_throw_value(error);
}

// Forward declaration for _map_read_field (defined in lambda-data-runtime.cpp)
Item _map_read_field(ShapeEntry* field, void* map_data);
// Forward declaration for _map_get (used as fallback for nested/spread maps)
Item _map_get(TypeMap* map_type, void* map_data, const char *key, bool *is_found);
extern "C" void js_mark_non_enumerable(Item object, Item name);
extern "C" Item js_object_define_property(Item obj, Item name, Item descriptor);
extern "C" void js_set_prototype(Item object, Item prototype);
extern "C" Item js_in(Item key, Item object);
extern "C" Item js_get_current_this(void) { return js_current_this; }

static void js_runtime_make_non_enumerable(Item object, Item name) {
    Item desc = js_new_object();
    js_set_prototype(desc, ItemNull);
    Item enum_key = (Item){.item = s2it(heap_create_name("enumerable", 10))};
    js_property_set(desc, enum_key, (Item){.item = b2it(false)});
    js_object_define_property(object, name, desc);
}

bool js_runtime_trace_enabled() {
    static int trace_enabled = -1;
    if (trace_enabled < 0) {
        const char* env = getenv("JS_RUNTIME_TRACE");
        trace_enabled = (env && env[0] != '\0' && strcmp(env, "0") != 0) ? 1 : 0;
    }
    return trace_enabled != 0;
}

// Forward declaration: defined in js_globals.cpp.
extern "C" bool js_func_is_builtin_ctor(Item fn);
extern "C" uint64_t js_get_heap_epoch() { return js_heap_epoch; }

// v37: Toggle private field initialization mode (called from transpiled code)
extern "C" void js_private_field_init_begin() { js_private_field_initializing = true; }
extern "C" void js_private_field_init_end() { js_private_field_initializing = false; }

// v37: Lazily resolve and cache Object.prototype for prototype chain fallback.
// Plain objects without __proto__ need this for HasProperty / property_get checks.
Map* js_resolve_object_prototype() {
    if (js_cached_object_proto) return js_cached_object_proto;
    if (js_resolving_object_proto) return NULL;
    js_resolving_object_proto = true;
    extern Item js_get_constructor(Item name_item);
    Item obj_ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Object", 6))});
    if (get_type_id(obj_ctor) == LMD_TYPE_FUNC) {
        Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
        Item obj_proto = js_property_get(obj_ctor, proto_key);
        if (get_type_id(obj_proto) == LMD_TYPE_MAP) {
            js_cached_object_proto = obj_proto.map;
        }
    }
    js_resolving_object_proto = false;
    return js_cached_object_proto;
}

// extern "C" wrapper for js_key_is_symbol — callable from MIR JIT
extern "C" int64_t js_key_is_symbol_c(Item key) {
    return js_key_is_symbol(key) ? 1 : 0;
}

// ES2020 §7.1.14 ToPropertyKey(argument)
// ToPrimitive(string hint), then Symbols → internal __sym_N string,
// strings → as-is, others → ToString.
extern "C" Item js_to_property_key(Item key) {
    if (js_key_is_symbol(key)) return js_symbol_to_key(key);
    TypeId kt = get_type_id(key);
    if (kt == LMD_TYPE_STRING) return key;
    if (key.item == 0 || kt == LMD_TYPE_NULL)
        return (Item){.item = s2it(heap_create_name("null", 4))};
    if (kt == LMD_TYPE_UNDEFINED)
        return (Item){.item = s2it(heap_create_name("undefined", 9))};
    if (kt == LMD_TYPE_MAP || kt == LMD_TYPE_ARRAY || kt == LMD_TYPE_ELEMENT || kt == LMD_TYPE_FUNC) {
        key = js_to_primitive(key, JS_HINT_STRING);
        if (js_check_exception()) return ItemNull;
        if (js_key_is_symbol(key)) return js_symbol_to_key(key);
        kt = get_type_id(key);
        if (kt == LMD_TYPE_STRING) return key;
        if (key.item == 0 || kt == LMD_TYPE_NULL)
            return (Item){.item = s2it(heap_create_name("null", 4))};
        if (kt == LMD_TYPE_UNDEFINED)
            return (Item){.item = s2it(heap_create_name("undefined", 9))};
    }
    return js_to_string(key);
}

// Phase-5C: js_make_getter_key / js_make_setter_key removed.
// Transpiler now emits js_install_user_accessor directly which routes to
// js_define_accessor_partial without ever materializing a __get_/__set_ marker key.

extern "C" void js_set_module_var(int index, Item value) {
    if (index >= 0 && index < JS_MAX_MODULE_VARS) {
        js_active_module_vars[index] = value;
    }
}

extern "C" Item js_get_module_var(int index) {
    if (index >= 0 && index < JS_MAX_MODULE_VARS) {
        return js_active_module_vars[index];
    }
    return ItemNull;
}

extern "C" void js_reset_module_vars() {
    js_ensure_module_vars_gc_rooted();
    memset(js_module_vars, 0, sizeof(js_module_vars));
    js_module_var_count = 0;
}

// Save/restore module vars for nested require() — prevents inner module
// from clobbering the outer module's live variables via js_reset_module_vars().
extern "C" Item* js_save_module_vars(int* out_count) {
    *out_count = JS_MAX_MODULE_VARS;
    Item* saved = (Item*)mem_alloc(JS_MAX_MODULE_VARS * sizeof(Item), MEM_CAT_TEMP);
    memcpy(saved, js_module_vars, JS_MAX_MODULE_VARS * sizeof(Item));
    return saved;
}

extern "C" void js_restore_module_vars(Item* saved, int count) {
    if (!saved) return;
    memcpy(js_module_vars, saved, count * sizeof(Item));
    mem_free(saved);
}

// Allocate a per-module variable array (used by CJS modules)
extern "C" Item* js_alloc_module_vars(void) {
    Item* vars = (Item*)pool_calloc(js_input->pool, JS_MAX_MODULE_VARS * sizeof(Item));
    heap_register_gc_root_range((uint64_t*)vars, JS_MAX_MODULE_VARS);
    return vars;
}

// Get/set the active module vars pointer
extern "C" Item* js_get_active_module_vars(void) {
    return js_active_module_vars;
}

extern "C" void js_set_active_module_vars(Item* vars) {
    js_active_module_vars = vars ? vars : js_module_vars;
}

// =============================================================================
// Exception Handling State
// =============================================================================

// Throw TypeError if value is null or undefined (ES spec RequireObjectCoercible)
extern "C" void js_require_object_coercible(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) {
        const char* type_str = (type == LMD_TYPE_NULL) ? "null" : "undefined";
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot destructure '%s' as it is %s.", type_str, type_str);
        Item err_name = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item err_msg = (Item){.item = s2it(heap_create_name(msg))};
        Item error = js_new_error_with_name(err_name, err_msg);
        js_throw_value(error);
    }
}

extern "C" void js_throw_value(Item value) {
    js_exception_pending = true;
    js_exception_value = value;
    // Capture exception message into static buffer while context is alive
    js_exception_msg_buf[0] = '\0';
    if (get_type_id(value) == LMD_TYPE_MAP) {
        Item name_key = (Item){.item = s2it(heap_create_name("name"))};
        Item msg_key = (Item){.item = s2it(heap_create_name("message"))};
        Item name_val = js_property_get(value, name_key);
        Item msg_val = js_property_get(value, msg_key);
        const char* nstr = "Error"; int nlen = 5;
        if (get_type_id(name_val) == LMD_TYPE_STRING) {
            String* ns = it2s(name_val);
            nstr = ns->chars; nlen = ns->len;
        }
        if (get_type_id(msg_val) == LMD_TYPE_STRING) {
            String* ms = it2s(msg_val);
            snprintf(js_exception_msg_buf, sizeof(js_exception_msg_buf),
                     "%.*s: %.*s", nlen, nstr, ms->len, ms->chars);
        } else {
            snprintf(js_exception_msg_buf, sizeof(js_exception_msg_buf),
                     "%.*s", nlen, nstr);
        }
    } else if (get_type_id(value) == LMD_TYPE_STRING) {
        String* s = it2s(value);
        snprintf(js_exception_msg_buf, sizeof(js_exception_msg_buf),
                 "%.*s", s->len, s->chars);
    }
}

extern "C" const char* js_get_exception_message(void) {
    return js_exception_msg_buf;
}

extern "C" int js_check_exception(void) {
    return js_exception_pending ? 1 : 0;
}

extern "C" Item js_clear_exception(void) {
    js_exception_pending = false;
    Item val = js_exception_value;
    js_exception_value = ItemNull;
    return val;
}

// TDZ check: throw ReferenceError if variable is still in Temporal Dead Zone
extern "C" void js_check_tdz(Item value, const char* name, int name_len) {
    if (value.item == ITEM_JS_TDZ) {
        char buf[256];
        int len = snprintf(buf, sizeof(buf), "Cannot access '%.*s' before initialization", name_len, name);
        Item tn = (Item){.item = s2it(heap_create_name("ReferenceError", 14))};
        Item msg = (Item){.item = s2it(heap_create_name(buf, len))};
        js_throw_value(js_new_error_with_name(tn, msg));
    }
}

// Const assignment check: throw TypeError when assigning to a const variable
extern "C" void js_throw_const_assign(const char* name, int name_len) {
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "Assignment to constant variable '%.*s'", name_len, name);
    Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
    Item msg = (Item){.item = s2it(heap_create_name(buf, len))};
    js_throw_value(js_new_error_with_name(tn, msg));
}

// forward declaration for js_batch_reset (defined near js_module_count_v14)
// forward declaration for array custom prototype check
extern "C" Item js_array_get_custom_proto(Item arr);
// forward declarations for module namespace cache resets
extern "C" void js_child_process_reset();
extern "C" void js_fs_reset();
extern "C" void js_path_reset();
extern "C" void js_os_reset();
extern "C" void js_url_module_reset();
extern "C" void js_util_reset();
extern "C" void js_reset_template_registry(void);
extern "C" void js_iterator_proto_cache_reset(void);
extern "C" void js_reset_css_namespace_object(void);
extern "C" void js_dynfunc_cache_reset(void);

extern "C" void js_batch_reset() {
    // increment epoch to invalidate cached heap objects
    js_heap_epoch++;
    // reset module variable table and active pointer
    js_reset_module_vars();
    js_active_module_vars = js_module_vars;
    // clear module registry (cached namespace_obj / mir_ctx are invalid after heap reset)
    module_registry_cleanup();
    // clear JS module cache (specifier String* pointers become dangling after heap reset)
    js_module_cache_reset();
    // clear any pending exception from previous script
    js_exception_pending = false;
    js_exception_value = (Item){0};
    js_reset_transient_call_state();
    js_reset_heap_bound_runtime_state();
    // reset cached global objects (Math, JSON, console, Reflect) so they're recreated fresh
    // — tests may modify them (delete/overwrite properties)
    js_reset_math_object();
    js_reset_json_object();
    js_reset_console_object();
    js_reset_reflect_object();
    js_reset_atomics_object();
    js_reset_262_object();
    js_reset_css_namespace_object();
    // reset interned __proto__ key (allocated in old pool)
    js_reset_proto_key();
    js_reset_template_registry();
    js_iterator_proto_cache_reset();
    // reset function pointer → JsFunction cache (JsFunction* in old pool)
    js_func_cache_reset();
    // reset builtin function cache (defined later in file, called via forward decl)
    js_builtin_cache_reset();
    // deep reset: generators, promises, async contexts, pending calls
    js_deep_batch_reset();
    // reset constructor prototypes and globalThis — tests may mutate built-in
    // prototypes (Object.prototype, Error.prototype, etc.).
    extern void js_reset_constructor_prototypes(void);
    js_reset_constructor_prototypes();
    // js_batch_reset() is the heavy/crash-recovery path. After restoring the
    // prototype snapshot above, invalidate it so (a) the upcoming
    // js_ctor_cache_reset actually runs, (b) the upcoming heap teardown
    // doesn't leave stale Map*/JsCtor* pointers in the snapshot, and (c) the
    // next preamble's js_batch_reset_to takes a fresh snapshot.
    extern void js_proto_snapshot_invalidate(void);
    js_proto_snapshot_invalidate();
    // reset globalThis, constructor cache, process object — stale heap pointers
    extern void js_globals_batch_reset(void);
    js_globals_batch_reset();
    // reset DOM state — stale document proxy and document pointer
    extern void js_dom_batch_reset(void);
    js_dom_batch_reset();
    // reset legacy RegExp static properties ($1-$9, input, etc.)
    memset(&js_regexp_last_match, 0, sizeof(js_regexp_last_match));
    // reset regex compilation cache — AST pointers from previous test are stale
    js_regex_cache_reset();
    // reset microtask queue and timers — callbacks referencing old heap
    extern void js_event_loop_init(void);
    js_event_loop_init();
    // reset process event listeners — callbacks referencing old heap
    extern void js_process_reset_listeners(void);
    js_process_reset_listeners();
    // reset strict mode — prevents strict-mode test from poisoning subsequent tests
    js_strict_mode = false;
    // reset module namespace caches (pool-allocated function wrappers become dangling)
    js_child_process_reset();
    js_fs_reset();
    js_path_reset();
    js_os_reset();
    js_url_module_reset();
    js_util_reset();
    // reset new Phase 1 modules
    extern void js_reset_querystring_module(void);
    js_reset_querystring_module();
    extern void js_reset_events_module(void);
    js_reset_events_module();
    extern void js_reset_buffer_module(void);
    js_reset_buffer_module();
    // reset Phase 4 modules
    extern void js_crypto_reset(void);
    js_crypto_reset();
    extern void js_dns_reset(void);
    js_dns_reset();
    extern void js_zlib_reset(void);
    js_zlib_reset();
    extern void js_readline_reset(void);
    js_readline_reset();
    extern void js_stream_reset(void);
    js_stream_reset();
    extern void js_net_reset(void);
    js_net_reset();
    extern void js_tls_reset(void);
    js_tls_reset();
    extern void js_http_reset(void);
    js_http_reset();
    extern void js_https_reset(void);
    js_https_reset();
    extern void js_string_decoder_reset(void);
    js_string_decoder_reset();
    extern void js_assert_reset(void);
    js_assert_reset();
    extern void js_node_test_reset(void);
    js_node_test_reset();
    js_eval_preamble_cache_reset();
    js_dynfunc_cache_reset();
    js_array_runtime_items_cleanup_all();
    // v95: reset Array.prototype[Symbol.iterator] override flag
    g_array_sym_iter_ever_set = 0;
    js_assert_batch_runtime_state_clear("js_batch_reset", true);
}

// Get current module var count (for checkpointing)
extern "C" int js_get_module_var_count() {
    return js_module_var_count;
}

// Partial batch reset: restore module vars to a checkpoint and clear test state,
// but leave heap and cached builtins intact.  Used by js-test-batch preamble mode
// to avoid re-initializing the harness between tests.
extern "C" void js_batch_reset_to(int checkpoint_var_count) {
    js_ensure_module_vars_gc_rooted();
    // If preamble ran with a pool-allocated module vars array, copy preamble vars
    // [0..checkpoint) into the static js_module_vars so tests can access them via
    // js_get_module_var(). Tests skip re-including preamble files (e.g.
    // nativeFunctionMatcher.js), so those module vars would otherwise stay zero.
    if (js_active_module_vars != js_module_vars && checkpoint_var_count > 0) {
        memcpy(js_module_vars, js_active_module_vars,
               (size_t)checkpoint_var_count * sizeof(Item));
    }
    // zero out module vars beyond the checkpoint
    for (int i = checkpoint_var_count; i < JS_MAX_MODULE_VARS; i++) {
        js_module_vars[i] = (Item){0};
    }
    js_module_var_count = checkpoint_var_count;
    js_active_module_vars = js_module_vars; // reset to static fallback
    // reset strict mode — prevents strict-mode test from poisoning subsequent non-strict tests
    js_strict_mode = false;
    // clear module registry (frees strdup/calloc per registered module)
    module_registry_cleanup();
    // clear JS module cache counter
    js_module_cache_reset();
    // clear pending exception
    js_exception_pending = false;
    js_exception_value = (Item){0};
    js_reset_transient_call_state();
    js_reset_heap_bound_runtime_state();
    // reset cached global objects — tests may modify them
    js_reset_math_object();
    js_reset_json_object();
    js_reset_console_object();
    js_reset_reflect_object();
    js_reset_atomics_object();
    js_reset_262_object();
    js_reset_css_namespace_object();
    // reset interned __proto__ key
    js_reset_proto_key();
    js_reset_template_registry();
    js_iterator_proto_cache_reset();
    // reset function pointer → JsFunction cache
    js_func_cache_reset();
    js_builtin_cache_reset();
    // deep reset: generators, promises, async contexts, pending calls
    js_deep_batch_reset();
    // reset constructor prototypes and globalThis — tests may mutate built-in
    // prototypes (Object.prototype, Error.prototype, etc.).
    extern void js_reset_constructor_prototypes(void);
    js_reset_constructor_prototypes();
    // reset globalThis, constructor cache, process object — stale heap pointers
    extern void js_globals_batch_reset(void);
    js_globals_batch_reset();
    // reset DOM state — stale document proxy and document pointer
    extern void js_dom_batch_reset(void);
    js_dom_batch_reset();
    // reset microtask queue and timers — callbacks referencing old heap
    extern void js_event_loop_init(void);
    js_event_loop_init();
    // reset process event listeners — callbacks referencing old heap
    extern void js_process_reset_listeners(void);
    js_process_reset_listeners();
    // reset legacy RegExp static properties ($1-$9, input, etc.)
    memset(&js_regexp_last_match, 0, sizeof(js_regexp_last_match));
    // reset regex compilation cache — AST pointers from previous test are stale
    js_regex_cache_reset();
    // reset module namespace caches (epoch-cached objects may be stale after test mutations)
    js_child_process_reset();
    js_fs_reset();
    js_path_reset();
    js_os_reset();
    js_url_module_reset();
    js_util_reset();
    extern void js_reset_querystring_module(void);
    js_reset_querystring_module();
    extern void js_reset_events_module(void);
    js_reset_events_module();
    extern void js_reset_buffer_module(void);
    js_reset_buffer_module();
    extern void js_crypto_reset(void);
    js_crypto_reset();
    extern void js_dns_reset(void);
    js_dns_reset();
    extern void js_zlib_reset(void);
    js_zlib_reset();
    extern void js_readline_reset(void);
    js_readline_reset();
    extern void js_stream_reset(void);
    js_stream_reset();
    extern void js_net_reset(void);
    js_net_reset();
    extern void js_tls_reset(void);
    js_tls_reset();
    extern void js_http_reset(void);
    js_http_reset();
    extern void js_https_reset(void);
    js_https_reset();
    extern void js_string_decoder_reset(void);
    js_string_decoder_reset();
    extern void js_assert_reset(void);
    js_assert_reset();
    extern void js_node_test_reset(void);
    js_node_test_reset();
    js_dynfunc_cache_reset();
    // v95: reset Array.prototype[Symbol.iterator] override flag
    g_array_sym_iter_ever_set = 0;
    js_assert_batch_runtime_state_clear("js_batch_reset_to", true);
}

extern "C" Item js_new_error(Item message) {
    return js_new_error_with_stack(message, (Item){.item = ITEM_JS_UNDEFINED});
}

// AggregateError(errors, message): Error subclass with .errors array
extern "C" Item js_new_aggregate_error(Item errors, Item message) {
    Item err_name = (Item){.item = s2it(heap_create_name("AggregateError", 14))};
    Item err = js_new_error_with_name(err_name, message);
    // Convert errors iterable to array — use js_array_from for iterable conversion
    Item errors_arr = js_array_from(errors);
    js_property_set(err, (Item){.item = s2it(heap_create_name("errors", 6))}, errors_arr);
    return err;
}

// v12: Create Error with a compile-time stack trace string
extern "C" Item js_new_error_with_stack(Item message, Item stack_str) {
    Item obj = js_new_object();
    Item name_val = (Item){.item = s2it(heap_create_name("Error"))};
    // Per spec: 'name' is inherited from Error.prototype, NOT set as own property
    // Only set 'message' when it is explicitly provided (not undefined/null)
    Item msg_key = (Item){.item = s2it(heap_create_name("message"))};
    if (message.item != ITEM_JS_UNDEFINED && get_type_id(message) != LMD_TYPE_UNDEFINED) {
        if (get_type_id(message) != LMD_TYPE_STRING) {
            Item str_msg = js_to_string(message);
            js_property_set(obj, msg_key, str_msg);
        } else {
            js_property_set(obj, msg_key, message);
        }
        // mark message as non-enumerable per spec §19.5.1.1
        js_mark_non_enumerable(obj, msg_key);
    }
    // Set stack property from compile-time stack trace
    Item stack_key = (Item){.item = s2it(heap_create_name("stack"))};
    if (stack_str.item != ITEM_JS_UNDEFINED) {
        js_property_set(obj, stack_key, stack_str);
    } else {
        // Default: "Error" + message
        const char* msg_str = "";
        int msg_len = 0;
        if (get_type_id(message) == LMD_TYPE_STRING) {
            String* ms = it2s(message);
            msg_str = ms->chars;
            msg_len = ms->len;
        }
        char buf[512];
        int len;
        if (msg_len > 0) {
            len = snprintf(buf, sizeof(buf), "Error: %.*s", msg_len, msg_str);
        } else {
            len = snprintf(buf, sizeof(buf), "Error");
        }
        js_property_set(obj, stack_key, (Item){.item = s2it(heap_create_name(buf, len))});
    }
    // Set __class_name__ for instanceof support
    Item cn_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
    js_property_set(obj, cn_key, name_val);
    js_class_stamp(obj, JS_CLASS_ERROR);  // A3-T3b — js_new_error always uses "Error"
    // Set __proto__ to Error.prototype so prototype methods (toString) are found
    {
        Item ctor_fn = js_get_constructor(name_val);
        if (ctor_fn.item != ITEM_JS_UNDEFINED && get_type_id(ctor_fn) == LMD_TYPE_FUNC) {
            Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
            Item proto = js_property_get(ctor_fn, proto_key);
            if (proto.item != ItemNull.item && get_type_id(proto) == LMD_TYPE_MAP) {
                js_set_prototype(obj, proto);
            }
        }
    }
    return obj;
}

// v11: Create a typed Error (TypeError, RangeError, SyntaxError, ReferenceError)
extern "C" Item js_new_error_with_name(Item error_name, Item message) {
    return js_new_error_with_name_stack(error_name, message, (Item){.item = ITEM_JS_UNDEFINED});
}

// v12: Create typed Error with compile-time stack trace
extern "C" Item js_new_error_with_name_stack(Item error_name, Item message, Item stack_str) {
    Item obj = js_new_object();
    // Per spec: 'name' is inherited from the prototype, NOT set as own property
    // Only set 'message' when it is explicitly provided (not undefined/null)
    Item msg_key = (Item){.item = s2it(heap_create_name("message"))};
    if (message.item != ITEM_JS_UNDEFINED && get_type_id(message) != LMD_TYPE_UNDEFINED) {
        if (get_type_id(message) != LMD_TYPE_STRING) {
            Item str_msg = js_to_string(message);
            js_property_set(obj, msg_key, str_msg);
        } else {
            js_property_set(obj, msg_key, message);
        }
        // mark message as non-enumerable per spec §20.5.1.1
        js_mark_non_enumerable(obj, msg_key);
    }
    // Set stack property
    Item stack_key = (Item){.item = s2it(heap_create_name("stack"))};
    if (stack_str.item != ITEM_JS_UNDEFINED) {
        js_property_set(obj, stack_key, stack_str);
    } else {
        const char* name_str = "Error";
        int name_len = 5;
        if (get_type_id(error_name) == LMD_TYPE_STRING) {
            String* ns = it2s(error_name);
            name_str = ns->chars;
            name_len = ns->len;
        }
        const char* msg_str = "";
        int msg_len = 0;
        if (get_type_id(message) == LMD_TYPE_STRING) {
            String* ms = it2s(message);
            msg_str = ms->chars;
            msg_len = ms->len;
        }
        char buf[512];
        int len;
        if (msg_len > 0) {
            len = snprintf(buf, sizeof(buf), "%.*s: %.*s", name_len, name_str, msg_len, msg_str);
        } else {
            len = snprintf(buf, sizeof(buf), "%.*s", name_len, name_str);
        }
        js_property_set(obj, stack_key, (Item){.item = s2it(heap_create_name(buf, len))});
    }
    // Set __class_name__ for instanceof support
    Item cn_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
    js_property_set(obj, cn_key, error_name);
    // A3-T3b: typed JsClass byte. Subtype-specific names (TypeError,
    // RangeError, etc.) collapse to JS_CLASS_ERROR for now — they share
    // the same prototype-chain dispatch.
    {
        String* en = (get_type_id(error_name) == LMD_TYPE_STRING) ? it2s(error_name) : NULL;
        JsClass ec = (en && en->len == 14 && !strncmp(en->chars, "AggregateError", 14))
            ? JS_CLASS_AGGREGATE_ERROR : JS_CLASS_ERROR;
        js_class_stamp(obj, ec);
    }
    // v18c: Set .constructor for assert.throws / constructor identity checks
    Item ctor_fn = js_get_constructor(error_name);
    if (ctor_fn.item != ITEM_JS_UNDEFINED && get_type_id(ctor_fn) == LMD_TYPE_FUNC) {
        Item ctor_key = (Item){.item = s2it(heap_create_name("constructor"))};
        js_property_set(obj, ctor_key, ctor_fn);
        // Mark constructor as non-enumerable
        js_mark_non_enumerable(obj, ctor_key);
        // Set __proto__ to ErrorType.prototype so prototype methods (toString) are found
        Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
        Item proto = js_property_get(ctor_fn, proto_key);
        if (proto.item != ItemNull.item && get_type_id(proto) == LMD_TYPE_MAP) {
            js_set_prototype(obj, proto);
        }
    } else {
        // No matching constructor (e.g. DOMException names like
        // "IndexSizeError", "WrongDocumentError"): set .name as an own
        // property so `e.name` returns the supplied name. Per WHATWG
        // DOMException spec, .name is a per-instance own property.
        Item name_key = (Item){.item = s2it(heap_create_name("name", 4))};
        js_property_set(obj, name_key, error_name);
        js_mark_non_enumerable(obj, name_key);
    }
    // Mark stack as non-enumerable (per ES spec)
    js_runtime_make_non_enumerable(obj, stack_key);
    return obj;
}

// ES2022: Extract cause from options object and set on error
extern "C" Item js_error_set_cause(Item error, Item options) {
    TypeId opt_type = get_type_id(options);
    if (opt_type != LMD_TYPE_MAP && opt_type != LMD_TYPE_ARRAY &&
        opt_type != LMD_TYPE_FUNC && opt_type != LMD_TYPE_ELEMENT) {
        return error;
    }
    Item cause_key = (Item){.item = s2it(heap_create_name("cause"))};
    Item has_cause = js_in(cause_key, options);
    if (js_exception_pending) return ItemNull;
    if (js_is_truthy(has_cause)) {
        Item cause_val = js_property_get(options, cause_key);
        if (js_exception_pending) return ItemNull;
        js_property_set(error, cause_key, cause_val);
        // mark cause as non-enumerable per spec §20.5.8.1
        js_mark_non_enumerable(error, cause_key);
    }
    return error;
}

// V8-specific: Error.captureStackTrace(targetObject[, constructorOpt])
// Sets .stack property on targetObject. In our runtime, just a no-op stub
// that sets an empty stack string to satisfy code that checks for .stack.
extern "C" Item js_error_captureStackTrace(Item target, Item ctor) {
    (void)ctor;
    if (get_type_id(target) == LMD_TYPE_MAP) {
        Item stack_key = (Item){.item = s2it(heap_create_name("stack", 5))};
        // only set if not already present
        bool found = false;
        js_map_get_fast(target.map, "stack", 5, &found);
        if (!found) {
            js_property_set(target, stack_key, (Item){.item = s2it(heap_create_name("", 0))});
        }
    }
    return make_js_undefined();
}

extern "C" void js_runtime_set_input(void* input) {
    js_input = (Input*)input;
    // Register static Item variables as GC roots on the CURRENT heap so their
    // referenced objects are not collected.  Must re-register on each new heap.
    heap_register_gc_root(&js_current_this.item);
    heap_register_gc_root(&js_new_target.item);
    heap_register_gc_root(&js_pending_new_target.item);
    heap_register_gc_root(&js_exception_value.item);
}

extern "C" Item js_get_this() {
    // Sloppy-mode coercion is applied by js_call_function/js_compute_callback_this
    // before installing the binding. Here only the uninitialized sentinel means
    // "no explicit this"; an actual JS null must remain observable to strict code.
    if (js_current_this.item == ITEM_JS_TDZ) {
        Item tn = (Item){.item = s2it(heap_create_name("ReferenceError", 14))};
        Item msg = (Item){.item = s2it(heap_create_name("Must call super constructor before accessing 'this'", 51))};
        js_throw_value(js_new_error_with_name(tn, msg));
        return make_js_undefined();
    }
    if (js_current_this.item == 0) {
        extern Item js_get_global_this();
        return js_get_global_this();
    }
    return js_current_this;
}

extern "C" Item js_get_lexical_this_binding(void) {
    if (js_current_this.item == ITEM_JS_TDZ) return js_current_this;
    return js_get_this();
}

extern "C" Item js_resolve_lexical_this(Item this_val) {
    if (this_val.item == ITEM_JS_TDZ) {
        Item tn = (Item){.item = s2it(heap_create_name("ReferenceError", 14))};
        Item msg = (Item){.item = s2it(heap_create_name("Must call super constructor before accessing 'this'", 51))};
        js_throw_value(js_new_error_with_name(tn, msg));
        return make_js_undefined();
    }
    if (this_val.item == 0) {
        extern Item js_get_global_this();
        return js_get_global_this();
    }
    return this_val;
}

extern "C" void js_set_this(Item this_val) {
    js_current_this = this_val;
}

extern "C" Item js_get_new_target() {
    return js_new_target;
}

extern "C" void js_set_new_target(Item target) {
    // Set as pending — will be picked up by js_call_function on entry
    js_pending_new_target = target;
    js_has_pending_new_target = true;
}

extern "C" void js_set_direct_new_target(Item target) {
    // Directly set new.target (for direct calls that bypass js_call_function)
    js_new_target = target;
}

// Build the 'arguments' array-like object from the pending call args.
// Called at the top of JIT-compiled functions that reference 'arguments'.

extern "C" void js_set_arguments_info(int64_t is_strict) {
    js_pending_args_is_strict = (int)is_strict;
}

void js_reset_transient_call_state() {
    js_skip_accessor_dispatch = false;
    js_current_this = (Item){0};
    js_proxy_receiver = (Item){0};
    js_new_target = (Item){0};
    js_pending_new_target = (Item){0};
    js_has_pending_new_target = false;
    js_super_this_bound_depth = 0;
    memset(js_super_this_bound_stack, 0, sizeof(js_runtime_state.super_this_bound_stack));
    memset(js_super_this_value_stack, 0, sizeof(js_runtime_state.super_this_value_stack));
    js_pending_call_args = NULL;
    js_pending_call_argc = 0;
    js_pending_args_is_strict = 0;
    js_pending_args_callee = (Item){0};
    js_array_method_real_this = (Item){0};
    // drop transient call-arg stack frames and its GC registration (heap may be
    // recreated across batch tests; re-registered lazily on next push)
    js_args_stack_reset();
}

void js_reset_heap_bound_runtime_state() {
    js_cached_object_proto = NULL;
    js_resolving_object_proto = false;
    js_private_field_initializing = false;
    js_input = NULL;
}

void js_assert_batch_runtime_state_clear(const char* reset_name, bool include_heap_bound) {
    int leak_count = 0;
    const char* name = reset_name ? reset_name : "js_batch_reset";

    if (js_exception_pending) {
        leak_count++;
        log_error("js-batch-state: %s left pending exception", name);
    }
    if (js_exception_value.item != 0) {
        leak_count++;
        log_error("js-batch-state: %s left exception value item=%lld", name, (long long)js_exception_value.item);
    }
    if (js_strict_mode) {
        leak_count++;
        log_error("js-batch-state: %s left strict mode enabled", name);
    }
    if (js_skip_accessor_dispatch) {
        leak_count++;
        log_error("js-batch-state: %s left accessor dispatch bypass enabled", name);
    }
    if (js_current_this.item != 0) {
        leak_count++;
        log_error("js-batch-state: %s left current this item=%lld", name, (long long)js_current_this.item);
    }
    if (js_proxy_receiver.item != 0) {
        leak_count++;
        log_error("js-batch-state: %s left proxy receiver item=%lld", name, (long long)js_proxy_receiver.item);
    }
    if (js_new_target.item != 0) {
        leak_count++;
        log_error("js-batch-state: %s left new.target item=%lld", name, (long long)js_new_target.item);
    }
    if (js_pending_new_target.item != 0 || js_has_pending_new_target) {
        leak_count++;
        log_error("js-batch-state: %s left pending new.target item=%lld flag=%d",
            name, (long long)js_pending_new_target.item, js_has_pending_new_target ? 1 : 0);
    }
    if (js_super_this_bound_depth != 0) {
        leak_count++;
        log_error("js-batch-state: %s left super this binding depth=%d", name, js_super_this_bound_depth);
    }
    if (js_pending_call_args || js_pending_call_argc != 0) {
        leak_count++;
        log_error("js-batch-state: %s left pending call args ptr=%p argc=%d",
            name, (void*)js_pending_call_args, js_pending_call_argc);
    }
    if (js_pending_args_is_strict || js_pending_args_callee.item != 0) {
        leak_count++;
        log_error("js-batch-state: %s left pending arguments state strict=%d callee=%lld",
            name, js_pending_args_is_strict, (long long)js_pending_args_callee.item);
    }
    if (js_array_method_real_this.item != 0) {
        leak_count++;
        log_error("js-batch-state: %s left array method receiver item=%lld",
            name, (long long)js_array_method_real_this.item);
    }

    if (include_heap_bound) {
        if (js_cached_object_proto) {
            leak_count++;
            log_error("js-batch-state: %s left cached Object.prototype ptr=%p", name, (void*)js_cached_object_proto);
        }
        if (js_resolving_object_proto) {
            leak_count++;
            log_error("js-batch-state: %s left Object.prototype resolving flag enabled", name);
        }
        if (js_private_field_initializing) {
            leak_count++;
            log_error("js-batch-state: %s left private-field init flag enabled", name);
        }
        if (js_input) {
            leak_count++;
            log_error("js-batch-state: %s left Input context ptr=%p", name, (void*)js_input);
        }
    }

    if (leak_count > 0) {
        log_error("js-batch-state: %s found %d uncleared runtime state field(s)", name, leak_count);
    }
}

extern "C" Item js_lookup_builtin_method(TypeId type, const char* name, int len);

extern "C" Item js_build_arguments_object() {
    int argc = js_pending_call_argc;
    Item* args = js_pending_call_args;
    int is_strict = js_pending_args_is_strict;

    Item arr = js_array_new(argc);
    for (int i = 0; i < argc; i++) {
        arr.array->items[i] = args ? args[i] : ItemNull;
    }
    // Mark as Arguments object via is_content flag (used by iterator to snapshot length)
    arr.array->is_content = 1;
    // Mark as Arguments object via Symbol.toStringTag on companion map
    Item companion = js_new_object();
    companion.map->map_kind = MAP_KIND_ARRAY_PROPS;
    arr.array->extra = (int64_t)(uintptr_t)companion.map;

    Item length_key = (Item){.item = s2it(heap_create_name("length", 6))};
    Item length_desc = js_new_object();
    js_set_prototype(length_desc, ItemNull);
    js_property_set(length_desc, (Item){.item = s2it(heap_create_name("value", 5))}, (Item){.item = i2it(argc)});
    js_property_set(length_desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(true)});
    js_property_set(length_desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
    js_property_set(length_desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
    js_object_define_property(companion, length_key, length_desc);

    Item tag_key = (Item){.item = s2it(heap_create_name("__sym_4", 7))};
    js_property_set(companion, tag_key,
                    (Item){.item = s2it(heap_create_name("Arguments", 9))});
    js_mark_non_enumerable(companion, tag_key);

    // ES6 §9.4.4.6 step 12: Set Symbol.iterator to Array.prototype.values
    Item si_key = (Item){.item = s2it(heap_create_name("__sym_1", 7))};
    Item si_fn = js_lookup_builtin_method(LMD_TYPE_ARRAY, "values", 6);
    js_property_set(companion, si_key, si_fn);
    js_mark_non_enumerable(companion, si_key);

    // v29: Set callee property (non-strict only; strict mode throws TypeError on access)
    if (is_strict) {
        Item thrower = js_get_or_create_builtin(JS_BUILTIN_FUNC_THROW_TYPE_ERROR, "ThrowTypeError", 0);
        Item callee_key = (Item){.item = s2it(heap_create_name("callee", 6))};
        js_install_native_accessor(companion, callee_key, thrower, thrower,
                                   JSPD_NON_ENUMERABLE | JSPD_NON_CONFIGURABLE);
        js_property_set(companion, (Item){.item = s2it(heap_create_name("__strict_arguments__", 20))},
                        (Item){.item = b2it(true)});
    } else {
        // Non-strict: callee is the function object (ES5 §10.6 step 13)
        if (js_pending_args_callee.item != 0) {
            Item callee_key = (Item){.item = s2it(heap_create_name("callee", 6))};
            Item desc = js_new_object();
            js_set_prototype(desc, ItemNull);
            js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, js_pending_args_callee);
            js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(true)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
            js_object_define_property(companion, callee_key, desc);
        }
    }

    return arr;
}

extern TypeMap EmptyMap;
