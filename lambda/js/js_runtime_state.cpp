#include "js_runtime_internal.hpp"
#include "../lambda-error.h"
#include "../jube/jube_registry.h"

JsRuntimeState js_runtime_state;
extern __thread EvalContext* context;

#define JS_EVAL_SOURCE_STACK_MAX 16
static Item js_eval_source_filename_stack[JS_EVAL_SOURCE_STACK_MAX];
static Item js_eval_source_code_stack[JS_EVAL_SOURCE_STACK_MAX];
static int64_t js_eval_source_line_offset_stack[JS_EVAL_SOURCE_STACK_MAX];
static int64_t js_eval_source_column_offset_stack[JS_EVAL_SOURCE_STACK_MAX];
static bool js_eval_source_compact_stack[JS_EVAL_SOURCE_STACK_MAX];
static int js_eval_source_stack_depth = 0;
static bool js_eval_source_roots_registered = false;

static void js_eval_source_register_roots(void) {
    if (js_eval_source_roots_registered) return;
    heap_register_gc_root_range((uint64_t*)js_eval_source_filename_stack, JS_EVAL_SOURCE_STACK_MAX);
    heap_register_gc_root_range((uint64_t*)js_eval_source_code_stack, JS_EVAL_SOURCE_STACK_MAX);
    js_eval_source_roots_registered = true;
}

static void js_eval_source_push_mode(Item filename, Item source,
                                     int64_t line_offset, int64_t column_offset,
                                     bool compact_stack) {
    js_eval_source_register_roots();
    if (js_eval_source_stack_depth >= JS_EVAL_SOURCE_STACK_MAX) return;
    int idx = js_eval_source_stack_depth++;
    js_eval_source_filename_stack[idx] = filename;
    js_eval_source_code_stack[idx] = source;
    js_eval_source_line_offset_stack[idx] = line_offset;
    js_eval_source_column_offset_stack[idx] = column_offset;
    js_eval_source_compact_stack[idx] = compact_stack;
}

extern "C" void js_eval_source_push(Item filename, Item source,
                                    int64_t line_offset, int64_t column_offset) {
    js_eval_source_push_mode(filename, source, line_offset, column_offset, false);
}

extern "C" void js_eval_source_push_compact(Item filename, Item source,
                                            int64_t line_offset, int64_t column_offset) {
    js_eval_source_push_mode(filename, source, line_offset, column_offset, true);
}

extern "C" void js_eval_source_pop(void) {
    if (js_eval_source_stack_depth <= 0) return;
    int idx = --js_eval_source_stack_depth;
    js_eval_source_filename_stack[idx] = ItemNull;
    js_eval_source_code_stack[idx] = ItemNull;
    js_eval_source_line_offset_stack[idx] = 0;
    js_eval_source_column_offset_stack[idx] = 0;
    js_eval_source_compact_stack[idx] = false;
}

static bool js_eval_source_current(Item* out_filename, Item* out_source,
                                   int64_t* out_line_offset, int64_t* out_column_offset,
                                   bool* out_compact_stack) {
    if (js_eval_source_stack_depth <= 0) return false;
    int idx = js_eval_source_stack_depth - 1;
    Item filename = js_eval_source_filename_stack[idx];
    Item source = js_eval_source_code_stack[idx];
    if (get_type_id(filename) != LMD_TYPE_STRING || get_type_id(source) != LMD_TYPE_STRING) {
        return false;
    }
    if (out_filename) *out_filename = filename;
    if (out_source) *out_source = source;
    if (out_line_offset) *out_line_offset = js_eval_source_line_offset_stack[idx];
    if (out_column_offset) *out_column_offset = js_eval_source_column_offset_stack[idx];
    if (out_compact_stack) *out_compact_stack = js_eval_source_compact_stack[idx];
    return true;
}

static int js_eval_source_first_line(String* source, const char** out_line) {
    if (!source || !out_line) return 0;
    const char* s = source->chars;
    int len = (int)source->len;
    int start = 0;
    while (start < len && (s[start] == '\n' || s[start] == '\r')) start++;
    int end = start;
    while (end < len && s[end] != '\n' && s[end] != '\r') end++;
    *out_line = s + start;
    return end - start;
}

static int js_eval_source_display_column(String* source) {
    const char* line = NULL;
    int line_len = js_eval_source_first_line(source, &line);
    if (!line || line_len <= 0) return 1;
    int pos = 0;
    while (pos < line_len && (line[pos] == ' ' || line[pos] == '\t')) pos++;
    if (pos + 5 <= line_len && memcmp(line + pos, "throw", 5) == 0 &&
        (pos + 5 == line_len || line[pos + 5] == ' ' || line[pos + 5] == '\t')) {
        pos += 5;
        while (pos < line_len && (line[pos] == ' ' || line[pos] == '\t')) pos++;
    }
    return pos + 1;
}

static Item js_eval_source_stack_string(Item error_name, Item message) {
    Item filename_item = ItemNull;
    Item source_item = ItemNull;
    int64_t line_offset = 0;
    int64_t column_offset = 0;
    bool compact_stack = false;
    if (!js_eval_source_current(&filename_item, &source_item, &line_offset, &column_offset,
                                &compact_stack)) {
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    String* filename = it2s(filename_item);
    String* source = it2s(source_item);
    if (!filename || !source) return (Item){.item = ITEM_JS_UNDEFINED};

    const char* line = NULL;
    int line_len = js_eval_source_first_line(source, &line);
    int display_line = (int)line_offset + 1;
    if (display_line < 1) display_line = 1;
    int display_col = js_eval_source_display_column(source) + (int)column_offset;
    if (display_col < 1) display_col = 1;

    const char* name_str = "Error";
    int name_len = 5;
    if (get_type_id(error_name) == LMD_TYPE_STRING) {
        String* ns = it2s(error_name);
        if (ns) { name_str = ns->chars; name_len = (int)ns->len; }
    }
    const char* msg_str = "";
    int msg_len = 0;
    if (get_type_id(message) == LMD_TYPE_STRING) {
        String* ms = it2s(message);
        if (ms) { msg_str = ms->chars; msg_len = (int)ms->len; }
    }

    if (compact_stack) {
        int total = name_len + msg_len + (int)filename->len + 64;
        char* buf = (char*)mem_alloc((size_t)total + 1, MEM_CAT_JS_RUNTIME);
        if (!buf) return (Item){.item = ITEM_JS_UNDEFINED};
        int pos = 0;
        pos += snprintf(buf + pos, (size_t)total + 1 - (size_t)pos, "%.*s",
                        name_len, name_str);
        if (msg_len > 0) {
            pos += snprintf(buf + pos, (size_t)total + 1 - (size_t)pos, ": %.*s",
                            msg_len, msg_str);
        }
        pos += snprintf(buf + pos, (size_t)total + 1 - (size_t)pos,
                        "\n    at %.*s:%d:%d",
                        (int)filename->len, filename->chars, display_line, display_col);
        if (pos < 0) pos = 0;
        if (pos > total) pos = total;
        Item result = (Item){.item = s2it(heap_create_name(buf, pos))};
        mem_free(buf);
        return result;
    }

    int caret_spaces = display_col - 1;
    int total = (int)filename->len + 32 + line_len + caret_spaces +
        name_len + msg_len + (int)filename->len + 64;
    char* buf = (char*)mem_alloc((size_t)total + 1, MEM_CAT_JS_RUNTIME);
    if (!buf) return (Item){.item = ITEM_JS_UNDEFINED};
    int pos = 0;
    pos += snprintf(buf + pos, (size_t)total + 1 - (size_t)pos, "%.*s:%d\n",
                    (int)filename->len, filename->chars, display_line);
    if (line_len > 0) {
        pos += snprintf(buf + pos, (size_t)total + 1 - (size_t)pos, "%.*s", line_len, line);
    }
    pos += snprintf(buf + pos, (size_t)total + 1 - (size_t)pos, "\n");
    for (int i = 0; i < caret_spaces && pos < total; i++) buf[pos++] = ' ';
    if (pos < total) buf[pos++] = '^';
    pos += snprintf(buf + pos, (size_t)total + 1 - (size_t)pos, "\n\n%.*s",
                    name_len, name_str);
    if (msg_len > 0) {
        pos += snprintf(buf + pos, (size_t)total + 1 - (size_t)pos, ": %.*s",
                        msg_len, msg_str);
    }
    pos += snprintf(buf + pos, (size_t)total + 1 - (size_t)pos,
                    "\n    at %.*s:%d:%d",
                    (int)filename->len, filename->chars, display_line, display_col);
    if (pos < 0) pos = 0;
    if (pos > total) pos = total;
    Item result = (Item){.item = s2it(heap_create_name(buf, pos))};
    mem_free(buf);
    return result;
}

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
    Item obj_proto = js_get_intrinsic_prototype_for_class(JS_CLASS_OBJECT);
    if (get_type_id(obj_proto) == LMD_TYPE_MAP) {
        js_cached_object_proto = obj_proto.map;
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
    // The exception slot outlives the throwing JS frame, so pointer-backed
    // scalars must move to traced heap storage before that frame is reclaimed.
    js_exception_value = lambda_item_heap_rehome(value);
    value = js_exception_value;
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
extern "C" void js_canvas_cleanup(void);
extern "C" void js_cjs_metadata_reset(void);
extern "C" void js_fetch_reset(void);
extern "C" void js_history_reset(void);

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
    // clear CommonJS metadata (filenames/modules are heap Items from the prior script)
    js_cjs_metadata_reset();
    // clear any pending exception from previous script
    js_exception_pending = false;
    js_exception_value = (Item){0};
    js_reset_transient_call_state();
    js_reset_heap_bound_runtime_state();
    js_decimal_number_egress_warning_reset();
    // reset cached global objects (Math, JSON, console, Reflect) so they're recreated fresh
    // — tests may modify them (delete/overwrite properties)
    js_canvas_cleanup();
    js_reset_math_object();
    js_reset_json_object();
    // Intl is heap-backed like the neighboring namespace caches; leaving it
    // out makes the next isolated document install a pointer to the freed heap.
    js_reset_intl_object();
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
    jube_modules_runtime_reset();
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
    // fetch() Response bodies live in a static side table so promise methods can
    // read them later; batch cleanup must release the table before memtrack.
    js_fetch_reset();
    js_history_reset();
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
    g_array_proto_push_ever_set = 0;
    js_assert_batch_runtime_state_clear("js_batch_reset", true);
}

// Get current module var count (for checkpointing)
extern "C" int js_get_module_var_count() {
    return js_module_var_count;
}

extern "C" void js_prepare_compiled_preamble_vars(int declaration_count) {
    js_reset_module_vars();
    js_active_module_vars = js_module_vars;
    if (declaration_count < 0) declaration_count = 0;
    if (declaration_count > JS_MAX_MODULE_VARS) declaration_count = JS_MAX_MODULE_VARS;
    // Compile-only preambles retain declarations but no heap-backed values;
    // js_main initializes these fresh slots in the new document realm.
    js_module_var_count = declaration_count;
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
    js_cjs_metadata_reset();
    // clear pending exception
    js_exception_pending = false;
    js_exception_value = (Item){0};
    js_reset_transient_call_state();
    js_reset_heap_bound_runtime_state();
    js_decimal_number_egress_warning_reset();
    // reset cached global objects — tests may modify them
    js_canvas_cleanup();
    js_reset_math_object();
    js_reset_json_object();
    js_reset_intl_object();
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
    jube_modules_runtime_reset();
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
    // fetch() Response bodies live in a static side table so promise methods can
    // read them later; batch cleanup must release the table before memtrack.
    js_fetch_reset();
    js_history_reset();
    extern void js_string_decoder_reset(void);
    js_string_decoder_reset();
    extern void js_assert_reset(void);
    js_assert_reset();
    extern void js_node_test_reset(void);
    js_node_test_reset();
    js_dynfunc_cache_reset();
    // v95: reset Array.prototype[Symbol.iterator] override flag
    g_array_sym_iter_ever_set = 0;
    g_array_proto_push_ever_set = 0;
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

static Item js_error_default_stack_string(Item error_name, Item message) {
    const char* name_str = "Error";
    int name_len = 5;
    if (get_type_id(error_name) == LMD_TYPE_STRING) {
        String* ns = it2s(error_name);
        if (ns) {
            name_str = ns->chars;
            name_len = (int)ns->len;
        }
    }
    const char* msg_str = "";
    int msg_len = 0;
    if (get_type_id(message) == LMD_TYPE_STRING) {
        String* ms = it2s(message);
        if (ms) {
            msg_str = ms->chars;
            msg_len = (int)ms->len;
        }
    }
    char buf[512];
    int len = msg_len > 0
        ? snprintf(buf, sizeof(buf), "%.*s: %.*s", name_len, name_str, msg_len, msg_str)
        : snprintf(buf, sizeof(buf), "%.*s", name_len, name_str);
    if (len < 0) len = 0;
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    return (Item){.item = s2it(heap_create_name(buf, len))};
}

static bool js_stack_raw_name_visible(const char* raw) {
    if (!raw || !raw[0]) return false;
    if (strcmp(raw, "js_main") == 0 || strcmp(raw, "main") == 0) return false;
    if (strncmp(raw, "js_capture_", 11) == 0) return false;
    return true;
}

static void js_stack_display_name(const char* raw, const char** out_name, int* out_len) {
    const char* name = raw ? raw : "<anonymous>";
    int len = (int)strlen(name);
    if (strncmp(name, "_js_", 4) == 0) {
        name += 4;
        len -= 4;
    }
    if (len > 2 && name[len - 2] == '_' && name[len - 1] == 'n') {
        len -= 2;
    }
    int end = len;
    while (end > 0 && name[end - 1] >= '0' && name[end - 1] <= '9') end--;
    if (end > 0 && end < len && name[end - 1] == '_') {
        len = end - 1;
    }
    if (len <= 0) {
        name = "<anonymous>";
        len = 11;
    }
    *out_name = name;
    *out_len = len;
}

static String* js_stack_current_filename(void) {
    if (context && context->current_file) {
        return heap_create_name(context->current_file, strlen(context->current_file));
    }
    Item filename_item = ItemNull;
    if (js_eval_source_current(&filename_item, NULL, NULL, NULL, NULL) &&
        get_type_id(filename_item) == LMD_TYPE_STRING) {
        return it2s(filename_item);
    }
    return NULL;
}

static bool js_stack_function_name_matches(Item fn_item, StackFrame* frame) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC || !frame || !frame->function_name) return false;
    JsFunction* fn = (JsFunction*)fn_item.function;
    if (!fn || !fn->name || fn->name->len <= 0) return false;

    const char* display = NULL;
    int display_len = 0;
    js_stack_display_name(frame->function_name, &display, &display_len);
    return display_len == (int)fn->name->len &&
        strncmp(display, fn->name->chars, (size_t)display_len) == 0;
}

static int js_stack_append_frame_text(StrBuf* sb, StackFrame* frame, bool include_prefix) {
    if (!sb || !frame || !js_stack_raw_name_visible(frame->function_name)) return 0;
    if (frame->is_native) return 0;

    const char* name = NULL;
    int name_len = 0;
    js_stack_display_name(frame->function_name, &name, &name_len);

    const char* file_chars = NULL;
    int file_len = 0;
    if (frame->location.file) {
        file_chars = frame->location.file;
        file_len = (int)strlen(file_chars);
    } else {
        String* cur_file = js_stack_current_filename();
        if (cur_file) {
            file_chars = cur_file->chars;
            file_len = (int)cur_file->len;
        }
    }
    if (!file_chars || file_len <= 0) {
        file_chars = "<anonymous>";
        file_len = 11;
    }

    uint32_t line = frame->location.line > 0 ? frame->location.line : 1;
    if (include_prefix) strbuf_append_str(sb, "    at ");
    if (name_len > 0) {
        strbuf_append_str_n(sb, name, name_len);
        strbuf_append_str(sb, " (");
        strbuf_append_str_n(sb, file_chars, file_len);
        strbuf_append_char(sb, ':');
        strbuf_append_int(sb, (int)line);
        strbuf_append_str(sb, ":1)");
    } else {
        strbuf_append_str_n(sb, file_chars, file_len);
        strbuf_append_char(sb, ':');
        strbuf_append_int(sb, (int)line);
        strbuf_append_str(sb, ":1");
    }
    return 1;
}

static Item js_stack_frame_string(StackFrame* frame) {
    StrBuf* sb = strbuf_new_cap(128);
    if (!sb) return (Item){.item = ITEM_JS_UNDEFINED};
    if (!js_stack_append_frame_text(sb, frame, false)) {
        strbuf_free(sb);
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    Item result = (Item){.item = s2it(heap_create_name(sb->str, sb->length))};
    strbuf_free(sb);
    return result;
}

static StackFrame* js_capture_native_stack_frames(void) {
    if (!context || !context->debug_info) return NULL;
    Item error_ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Error", 5))});
    int frame_limit = 10;
    if (get_type_id(error_ctor) == LMD_TYPE_FUNC) {
        Item limit = js_property_get(error_ctor, (Item){.item = s2it(heap_create_name("stackTraceLimit", 15))});
        TypeId limit_type = get_type_id(limit);
        if (limit_type == LMD_TYPE_INT || limit_type == LMD_TYPE_INT64 || limit_type == LMD_TYPE_FLOAT) {
            double dlimit = it2d(limit);
            if (dlimit <= 0 || dlimit != dlimit) return NULL;
            frame_limit = (int)dlimit;
            if (frame_limit > 200) frame_limit = 200;
        }
    }
    // LambdaJS stack traces reuse Lambda's zero-normal-overhead frame walk:
    // capture only while constructing an Error stack, never on successful calls.
    return err_capture_stack_trace(context->debug_info, frame_limit);
}

static Item js_error_native_stack_string(Item error_name, Item message, Item stack_start_fn) {
    StackFrame* trace = js_capture_native_stack_frames();
    if (!trace) return (Item){.item = ITEM_JS_UNDEFINED};

    Item header = js_error_default_stack_string(error_name, message);
    String* header_str = get_type_id(header) == LMD_TYPE_STRING ? it2s(header) : NULL;
    StrBuf* sb = strbuf_new_cap(256);
    if (!sb) {
        err_free_stack_trace(trace);
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (header_str) strbuf_append_str_n(sb, header_str->chars, header_str->len);

    int frame_count = 0;
    bool trimming = get_type_id(stack_start_fn) == LMD_TYPE_FUNC;
    for (StackFrame* frame = trace; frame; frame = frame->next) {
        if (trimming) {
            if (js_stack_function_name_matches(stack_start_fn, frame)) trimming = false;
            continue;
        }
        int before = sb->length;
        strbuf_append_char(sb, '\n');
        if (js_stack_append_frame_text(sb, frame, true)) {
            frame_count++;
        } else {
            sb->length = before;
            sb->str[before] = '\0';
        }
    }

    Item result = frame_count > 0
        ? (Item){.item = s2it(heap_create_name(sb->str, sb->length))}
        : (Item){.item = ITEM_JS_UNDEFINED};
    strbuf_free(sb);
    err_free_stack_trace(trace);
    return result;
}

static Item js_error_prepare_stack_trace(Item error_obj) {
    Item error_name = (Item){.item = s2it(heap_create_name("Error", 5))};
    Item error_ctor = js_get_constructor(error_name);
    if (get_type_id(error_ctor) != LMD_TYPE_FUNC) return (Item){.item = ITEM_JS_UNDEFINED};
    Item prepare_key = (Item){.item = s2it(heap_create_name("prepareStackTrace", 17))};
    Item prepare = js_property_get(error_ctor, prepare_key);
    if (get_type_id(prepare) != LMD_TYPE_FUNC) return (Item){.item = ITEM_JS_UNDEFINED};

    Item frames = js_array_new(0);
    StackFrame* trace = js_capture_native_stack_frames();
    int frame_count = 0;
    for (StackFrame* frame = trace; frame; frame = frame->next) {
        Item frame_text = js_stack_frame_string(frame);
        if (get_type_id(frame_text) == LMD_TYPE_STRING) {
            js_array_push(frames, frame_text);
            frame_count++;
        }
    }
    if (trace) err_free_stack_trace(trace);
    if (frame_count == 0) return (Item){.item = ITEM_JS_UNDEFINED};

    Item args[2] = { error_obj, frames };
    Item prepared = js_call_function(prepare, (Item){.item = ITEM_JS_UNDEFINED}, args, 2);
    if (js_exception_pending) return (Item){.item = ITEM_JS_UNDEFINED};
    if (get_type_id(prepared) == LMD_TYPE_STRING) return prepared;
    return js_to_string(prepared);
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
    Item stack_key = (Item){.item = s2it(heap_create_name("stack"))};
    js_class_stamp(obj, JS_CLASS_ERROR);
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
    // Error stacks now use Lambda's JIT frame walk so normal calls avoid
    // per-call push/pop bookkeeping; lexical source strings remain fallback.
    Item prepared_stack = js_error_prepare_stack_trace(obj);
    Item native_stack = js_error_native_stack_string(name_val, message, (Item){.item = ITEM_JS_UNDEFINED});
    Item eval_stack = js_eval_source_stack_string(name_val, message);
    if (prepared_stack.item != ITEM_JS_UNDEFINED) {
        js_property_set(obj, stack_key, prepared_stack);
    } else if (native_stack.item != ITEM_JS_UNDEFINED) {
        js_property_set(obj, stack_key, native_stack);
    } else if (eval_stack.item != ITEM_JS_UNDEFINED) {
        js_property_set(obj, stack_key, eval_stack);
    } else if (stack_str.item != ITEM_JS_UNDEFINED) {
        js_property_set(obj, stack_key, stack_str);
    } else {
        js_property_set(obj, stack_key, js_error_default_stack_string(name_val, message));
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
    Item stack_key = (Item){.item = s2it(heap_create_name("stack"))};
    // Stamp typed class identity. Unknown engine-created names ending in
    // "Error" still get generic Error identity.
    {
        String* en = (get_type_id(error_name) == LMD_TYPE_STRING) ? it2s(error_name) : NULL;
        JsClass ec = en ? js_class_from_name(en->chars, (int)en->len) : JS_CLASS_ERROR;
        if (ec == JS_CLASS_NONE && en && en->len >= 5 &&
            !strncmp(en->chars + en->len - 5, "Error", 5)) {
            ec = JS_CLASS_ERROR;
        }
        if (ec == JS_CLASS_NONE) ec = JS_CLASS_ERROR;
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
    // Error.prepareStackTrace may test err instanceof SyntaxError, so typed
    // errors must be linked before invoking the native-stack-backed hook.
    Item prepared_stack = js_error_prepare_stack_trace(obj);
    Item native_stack = js_error_native_stack_string(error_name, message, (Item){.item = ITEM_JS_UNDEFINED});
    Item eval_stack = js_eval_source_stack_string(error_name, message);
    if (prepared_stack.item != ITEM_JS_UNDEFINED) {
        js_property_set(obj, stack_key, prepared_stack);
    } else if (native_stack.item != ITEM_JS_UNDEFINED) {
        js_property_set(obj, stack_key, native_stack);
    } else if (eval_stack.item != ITEM_JS_UNDEFINED) {
        js_property_set(obj, stack_key, eval_stack);
    } else if (stack_str.item != ITEM_JS_UNDEFINED) {
        js_property_set(obj, stack_key, stack_str);
    } else {
        js_property_set(obj, stack_key, js_error_default_stack_string(error_name, message));
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
// Captures from the native/JIT frame chain. constructorOpt trims through the
// named function so Node's stackStartFn wrappers do not leak into .stack.
extern "C" Item js_error_captureStackTrace(Item target, Item ctor) {
    if (get_type_id(target) == LMD_TYPE_MAP) {
        Item stack_key = (Item){.item = s2it(heap_create_name("stack", 5))};
        Item name = js_property_get(target, (Item){.item = s2it(heap_create_name("name", 4))});
        if (get_type_id(name) != LMD_TYPE_STRING) {
            name = (Item){.item = s2it(heap_create_name("Error", 5))};
        }
        Item message = js_property_get(target, (Item){.item = s2it(heap_create_name("message", 7))});
        if (get_type_id(message) != LMD_TYPE_STRING) {
            message = (Item){.item = ITEM_JS_UNDEFINED};
        }
        Item stack = js_error_native_stack_string(name, message, ctor);
        if (stack.item == ITEM_JS_UNDEFINED) {
            stack = js_error_default_stack_string(name, message);
        }
        js_property_set(target, stack_key, stack);
        js_runtime_make_non_enumerable(target, stack_key);
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

extern "C" void js_set_pending_call_source(const char* source, int64_t len) {
    js_pending_call_source = source;
    js_pending_call_source_len = (len > 0 && len < 8192) ? (int)len : 0;
    if (!source || js_pending_call_source_len == 0) {
        js_pending_call_source = NULL;
        js_pending_call_source_len = 0;
    }
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
    js_pending_call_source = NULL;
    js_pending_call_source_len = 0;
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
    if (js_pending_call_source || js_pending_call_source_len != 0) {
        leak_count++;
        log_error("js-batch-state: %s left pending call source ptr=%p len=%d",
            name, (void*)js_pending_call_source, js_pending_call_source_len);
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
        // Arguments creation defines own indexed data properties directly;
        // inherited numeric setters must not intercept parameter materialization.
        js_array_define_dense_element_direct(arr, i, args ? args[i] : ItemNull);
    }
    // Mark as Arguments object via is_content flag (used by iterator to snapshot length)
    arr.array->is_content = 1;
    // Mark as Arguments object via Symbol.toStringTag on companion map
    Item companion = js_new_object();
    companion.map->map_kind = MAP_KIND_ARRAY_PROPS;
    js_array_set_props(arr.array, companion.map);

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
