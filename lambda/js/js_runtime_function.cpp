/**
 * JavaScript runtime function object wrappers for Lambda.
 */
#include "js_runtime_internal.hpp"
#include "../../lib/memtrack.h"

// =============================================================================
// Function object wrappers
// =============================================================================

// Cache: func_ptr → JsFunction*  (ensures same MIR function → same wrapper → same .prototype)
static const int JS_FUNC_CACHE_SIZE = 512;
static void* js_func_cache_keys[512];
static JsFunction* js_func_cache_vals[512];
static int js_func_cache_count = 0;
static int js_func_cache_suppress_depth = 0;

static JsFunction* js_func_cache_lookup(void* func_ptr) {
    for (int i = 0; i < js_func_cache_count; i++) {
        if (js_func_cache_keys[i] == func_ptr) return js_func_cache_vals[i];
    }
    return NULL;
}

static void js_func_cache_insert(void* func_ptr, JsFunction* fn) {
    if (js_func_cache_count < JS_FUNC_CACHE_SIZE) {
        js_func_cache_keys[js_func_cache_count] = func_ptr;
        js_func_cache_vals[js_func_cache_count] = fn;
        js_func_cache_count++;
    }
}

void js_func_cache_reset() {
    js_func_cache_count = 0;
    js_func_cache_suppress_depth = 0;
}

extern "C" void js_func_cache_suppress_push(void) {
    js_func_cache_suppress_depth++;
}

extern "C" void js_func_cache_suppress_pop(void) {
    if (js_func_cache_suppress_depth > 0) js_func_cache_suppress_depth--;
}

// =============================================================================
// Transient JIT call-argument stack
// =============================================================================
//
// Every JS call with >=1 argument needs a contiguous Item[] buffer for its
// arguments. Allocating that per call from the pool and registering it as a
// fresh permanent GC root range (the old js_alloc_env path) made call-heavy
// loops O(n^2) — gc_register_root_range linearly scans all ranges per call and
// the ranges were never released. Instead, args live on a single bump stack
// that is registered with the GC exactly once (re-registered only on growth or
// after a batch heap reset). A call expression pushes its frame, the callee
// reads it, and the caller pops back to the saved mark after the call returns.
//
// Invariant: slots in [len, cap) are always kept zeroed, so the GC (which marks
// the whole registered [0, cap) range) never sees a stale pointer above the
// live region. Zero is a GC-safe Item value (item_to_ptr(0) is null).
//
// The base never moves. A frame's pointer must stay valid while later arguments
// (which may nest calls and push more frames) are evaluated, and partially
// filled args must remain GC-rooted in place — so we cannot realloc the buffer.
// We reserve a fixed region (covers call depths far beyond the C stack limit)
// and fall back to a per-frame pool allocation only on the pathological overflow
// (which would C-stack-overflow first); the fallback is correct, just not pooled
// back, and is registered as its own GC root.
#define JS_ARGS_STACK_CAP (256 * 1024)   // Items (2 MB)
static Item*  js_args_stack = NULL;
static size_t js_args_len = 0;            // live Items (the bump "top")
static bool   js_args_registered = false; // registered with the current GC heap?

// Reserve `count` zeroed argument slots and return a pointer to them.
extern "C" Item* js_args_push(int count) {
    if (count <= 0) return NULL;
    if (!js_args_stack) {
        js_args_stack = (Item*)mem_calloc(JS_ARGS_STACK_CAP, sizeof(Item), MEM_CAT_JS_RUNTIME);
        if (!js_args_stack) { log_error("js_args_push: stack alloc failed"); return NULL; }
    }
    if (!js_args_registered) {
        heap_register_gc_root_range((uint64_t*)js_args_stack, JS_ARGS_STACK_CAP);
        js_args_registered = true;
    }
    if (js_args_len + (size_t)count > JS_ARGS_STACK_CAP) {
        // pathological depth — fall back to a standalone GC-rooted buffer
        return js_alloc_env(count);
    }
    Item* p = js_args_stack + js_args_len;
    // slots are already zeroed (invariant); fill happens in the JIT caller
    js_args_len += (size_t)count;
    return p;
}

// Current bump top — saved before a call expression is lowered.
extern "C" int64_t js_args_save(void) {
    return (int64_t)js_args_len;
}

// Pop back to a saved mark, re-zeroing the popped slots to preserve the
// [len, cap) zeroed invariant.
extern "C" void js_args_restore(int64_t mark) {
    size_t m = (size_t)mark;
    if (m >= js_args_len) return;  // nothing to pop (or stale mark)
    memset(js_args_stack + m, 0, (js_args_len - m) * sizeof(Item));
    js_args_len = m;
}

// Called from the per-test/batch reset: the GC heap may be torn down and
// recreated, so drop our registration (re-registered lazily on next push) and
// clear any frames left behind by an unwound test.
extern "C" void js_args_stack_reset(void) {
    if (js_args_len && js_args_stack) {
        memset(js_args_stack, 0, js_args_len * sizeof(Item));
    }
    js_args_len = 0;
    js_args_registered = false;
}

extern "C" int64_t js_with_depth_active(void);
extern "C" Item* js_with_capture_stack(int* out_depth);

static void js_function_capture_with_env(JsFunction* fn) {
    if (!fn || !js_with_depth_active()) return;
    int depth = 0;
    Item* stack = js_with_capture_stack(&depth);
    if (stack && depth > 0) {
        fn->with_env = stack;
        fn->with_env_depth = depth;
    }
}

extern "C" void* js_function_get_ptr(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return NULL;
    // Try JsFunction layout first (func_ptr at offset 8)
    JsFunction* jsfn = (JsFunction*)fn_item.function;
    if (jsfn->func_ptr) return jsfn->func_ptr;
    // Fall back to Function layout (ptr at offset 16)
    Function* fn = fn_item.function;
    return (void*)fn->ptr;
}


extern "C" int js_function_get_arity(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return 0;
    JsFunction* jsfn = (JsFunction*)fn_item.function;
    // If func_ptr (offset 8) is set, it's JsFunction layout
    if (jsfn->func_ptr) return jsfn->param_count;
    // Otherwise it's Function layout — arity at offset 1
    Function* fn = fn_item.function;
    return fn->arity;
}


extern "C" void js_function_set_prototype(Item fn_item, Item proto) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* jsfn = (JsFunction*)fn_item.function;
    jsfn->prototype = proto;
}

extern "C" Item js_new_function(void* func_ptr, int param_count) {
    if (!func_ptr) {
        log_error("js_new_function: null func_ptr! param_count=%d", param_count);
        return ItemNull;
    }
    // Return cached wrapper if the same MIR function was already wrapped.
    // This ensures Foo.prototype = {...} and (new Foo()) share the same JsFunction*.
    bool has_with_env = js_with_depth_active() != 0;
    bool suppress_cache = js_func_cache_suppress_depth > 0;
    JsFunction* cached = (has_with_env || suppress_cache) ? NULL : js_func_cache_lookup(func_ptr);
    if (cached) return (Item){.function = (Function*)cached};

    // Pool-allocate: JS functions are module-lifetime objects that must not be
    // GC-collected (they live in pool-allocated env arrays unreachable from GC roots).
    JsFunction* fn = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = func_ptr;
    fn->param_count = param_count;
    fn->formal_length = -1; // -1 = use param_count for .length
    fn->env = NULL;
    fn->env_size = 0;
    fn->prototype = ItemNull;
    fn->module_vars = js_active_module_vars; // bind to creating module's vars
    js_function_capture_with_env(fn);
    if (!has_with_env && !suppress_cache) js_func_cache_insert(func_ptr, fn);
    return (Item){.function = (Function*)fn};
}

extern "C" Item js_new_method_function(void* func_ptr, int param_count) {
    if (!func_ptr) {
        log_error("js_new_method_function: null func_ptr! param_count=%d", param_count);
        return ItemNull;
    }
    JsFunction* fn = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = func_ptr;
    fn->param_count = param_count;
    fn->formal_length = -1;
    fn->env = NULL;
    fn->env_size = 0;
    fn->prototype = ItemNull;
    fn->module_vars = js_active_module_vars;
    js_function_capture_with_env(fn);
    return (Item){.function = (Function*)fn};
}

// Create a closure (function with captured environment)
extern "C" Item js_new_closure(void* func_ptr, int param_count, Item* env, int env_size) {
    // Pool-allocate: closures stored in env arrays are unreachable from GC roots
    // (env is pool-allocated, stack scan can't trace through pool to find them).
    JsFunction* fn = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = func_ptr;
    fn->param_count = param_count;
    fn->formal_length = -1; // -1 = use param_count for .length
    fn->env = env;
    fn->env_size = env_size;
    fn->prototype = ItemNull;
    fn->module_vars = js_active_module_vars; // bind to creating module's vars
    js_function_capture_with_env(fn);
    return (Item){.function = (Function*)fn};
}

// Set the ES spec formal .length for a function (params before first default, excl rest)
extern "C" void js_set_formal_length(Item fn_item, int length) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->formal_length = (int16_t)length;
}

// Allocate closure environment (array of Item on the pool)
// Register as GC root range so the collector can trace objects held in env slots.
extern "C" Item* js_alloc_env(int count) {
    Item* env = (Item*)pool_calloc(js_input->pool, count * sizeof(Item));
    heap_register_gc_root_range((uint64_t*)env, count);
    return env;
}

// v20: Mark a function as a generator (generator prototype has no constructor)
extern "C" void js_mark_generator_func(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->flags |= JS_FUNC_FLAG_GENERATOR;
}

// Mark a function as an async generator function (sets both GENERATOR and ASYNC_GEN flags)
extern "C" void js_mark_async_generator_func(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->flags |= JS_FUNC_FLAG_GENERATOR | JS_FUNC_FLAG_ASYNC_GEN;
}

// Mark a function as an async (non-generator) function — affects [[Prototype]]/.constructor
extern "C" void js_mark_async_func(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->flags |= JS_FUNC_FLAG_ASYNC;
}

extern "C" void js_mark_derived_constructor_func(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->flags |= JS_FUNC_FLAG_DERIVED_CTOR;
}

// Mark a function as an arrow function (non-constructable)
extern "C" void js_mark_arrow_func(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->flags |= JS_FUNC_FLAG_ARROW;
}

extern "C" void js_mark_method_func(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->flags |= JS_FUNC_FLAG_METHOD;
}

extern "C" void js_mark_eval_initializer_func_if_active(Item fn_item) {
    if (!js_private_field_initializing && !js_eval_initializer_context) return;
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->eval_initializer_context = true;
}

// Mark a function as strict mode (ES spec [[Strict]] internal slot)
extern "C" void js_mark_strict_func(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->flags |= JS_FUNC_FLAG_STRICT;
}

extern "C" void js_set_class_name(Item cls_item, Item name_item);
static Item js_private_display_name_item(Item name_item);

// Set the name of a JsFunction (called from transpiler after js_new_function/js_new_closure)
extern "C" void js_set_function_name(Item fn_item, Item name_item) {
    name_item = js_private_display_name_item(name_item);
    if (get_type_id(fn_item) == LMD_TYPE_MAP) {
        js_set_class_name(fn_item, name_item);
        return;
    }
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    if (get_type_id(name_item) != LMD_TYPE_STRING) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    if (fn->func_ptr) { // is JsFunction layout
        fn->name = it2s(name_item);
    }
}
extern "C" void js_set_function_name_if_anonymous(Item fn_item, Item name_item) {
    name_item = js_private_display_name_item(name_item);
    if (get_type_id(fn_item) == LMD_TYPE_MAP) {
        js_set_class_name(fn_item, name_item);
        return;
    }
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    if (get_type_id(name_item) != LMD_TYPE_STRING) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    if (fn->func_ptr && (!fn->name || fn->name->len == 0)) {
        fn->name = it2s(name_item);
    }
}

extern "C" Item js_symbol_get_description(Item sym);

static const char* js_private_display_suffix(const char* name, int len) {
    if (!name || len <= 10 || strncmp(name, "__private_", 10) != 0) return NULL;
    const char* suffix = name + 10;
    const char* end = name + len;
    const char* p = suffix;
    while (p < end && *p >= '0' && *p <= '9') p++;
    if (p > suffix && p < end && *p == '_') suffix = p + 1;
    return suffix;
}

static const char* js_hash_private_display_suffix(const char* name, int len) {
    if (!name || len <= 3 || name[0] != '#') return NULL;
    const char* suffix = name + 1;
    const char* end = name + len;
    const char* p = suffix;
    while (p < end && *p >= '0' && *p <= '9') p++;
    if (p > suffix && p < end && *p == '_') return p + 1;
    return NULL;
}

static Item js_private_display_name_item(Item name_item) {
    if (get_type_id(name_item) != LMD_TYPE_STRING) return name_item;
    String* name = it2s(name_item);
    if (!name) return name_item;

    char display[320];
    const char* suffix = js_private_display_suffix(name->chars, (int)name->len);
    const char* hash_suffix = js_hash_private_display_suffix(name->chars, (int)name->len);
    if (suffix) {
        snprintf(display, sizeof(display), "#%s", suffix);
    } else if (hash_suffix) {
        snprintf(display, sizeof(display), "#%s", hash_suffix);
    } else if (name->len > 14 && strncmp(name->chars, "get __private_", 14) == 0) {
        suffix = js_private_display_suffix(name->chars + 4, (int)name->len - 4);
        if (!suffix) return name_item;
        snprintf(display, sizeof(display), "get #%s", suffix);
    } else if (name->len > 14 && strncmp(name->chars, "set __private_", 14) == 0) {
        suffix = js_private_display_suffix(name->chars + 4, (int)name->len - 4);
        if (!suffix) return name_item;
        snprintf(display, sizeof(display), "set #%s", suffix);
    } else {
        return name_item;
    }
    return (Item){.item = s2it(heap_create_name(display, strlen(display)))};
}

static int js_function_name_from_symbol_key(String* key, char* out, int out_size) {
    if (!key || key->len <= 6 || strncmp(key->chars, "__sym_", 6) != 0) return -1;
    int64_t id = 0;
    for (int i = 6; i < (int)key->len; i++) {
        char c = key->chars[i];
        if (c < '0' || c > '9') return -1;
        id = id * 10 + (int64_t)(c - '0');
    }
    Item sym = (Item){.item = i2it(-(id + (int64_t)JS_SYMBOL_BASE))};
    Item desc = js_symbol_get_description(sym);
    if (get_type_id(desc) == LMD_TYPE_UNDEFINED) {
        if (out_size > 0) out[0] = '\0';
        return 0;
    }
    if (get_type_id(desc) != LMD_TYPE_STRING) return -1;
    String* desc_str = it2s(desc);
    if (!desc_str) return -1;
    int len = snprintf(out, out_size, "[%.*s]", (int)desc_str->len, desc_str->chars);
    if (len < 0) return -1;
    if (len >= out_size) len = out_size - 1;
    return len;
}

extern "C" void js_set_function_name_from_property_key_if_anonymous(Item fn_item, Item key_item, int64_t prefix_kind) {
    Item prop_key = js_to_property_key(key_item);
    if (get_type_id(prop_key) != LMD_TYPE_STRING) return;
    String* key = it2s(prop_key);
    if (!key) return;

    char base[256];
    int base_len = js_function_name_from_symbol_key(key, base, (int)sizeof(base));
    if (base_len < 0) {
        const char* private_suffix = js_private_display_suffix(key->chars, (int)key->len);
        if (private_suffix) {
            base_len = snprintf(base, sizeof(base), "#%s", private_suffix);
            if (base_len >= (int)sizeof(base)) base_len = (int)sizeof(base) - 1;
        } else if ((private_suffix = js_hash_private_display_suffix(key->chars, (int)key->len)) != NULL) {
            base_len = snprintf(base, sizeof(base), "#%s", private_suffix);
            if (base_len >= (int)sizeof(base)) base_len = (int)sizeof(base) - 1;
        } else {
            base_len = key->len < (int)sizeof(base) - 1 ? (int)key->len : (int)sizeof(base) - 1;
            memcpy(base, key->chars, base_len);
        }
        base[base_len] = '\0';
    }

    char display[320];
    if (prefix_kind == 1) {
        snprintf(display, sizeof(display), "get %.*s", base_len, base);
    } else if (prefix_kind == 2) {
        snprintf(display, sizeof(display), "set %.*s", base_len, base);
    } else {
        snprintf(display, sizeof(display), "%.*s", base_len, base);
    }
    Item display_item = (Item){.item = s2it(heap_create_name(display, strlen(display)))};
    if (prefix_kind == 1 || prefix_kind == 2) {
        js_set_function_name(fn_item, display_item);
    } else {
        js_set_function_name_if_anonymous(fn_item, display_item);
    }
}

extern "C" void js_set_class_name(Item cls_item, Item name_item) {
    if (get_type_id(cls_item) != LMD_TYPE_MAP) return;
    if (get_type_id(name_item) != LMD_TYPE_STRING) return;
    ShapeEntry* existing = js_find_shape_entry(cls_item, "name", 4);
    if (existing && !jspd_is_deleted(existing)) {
        Item key = (Item){.item = s2it(heap_create_name("name", 4))};
        Item current = js_property_get(cls_item, key);
        if (get_type_id(current) == LMD_TYPE_STRING) {
            String* current_name = it2s(current);
            if (current_name && current_name->len == 0) {
                String* name_key_str = heap_create_name("name", 4);
                map_put(cls_item.map, name_key_str, name_item, js_input);
                js_attr_set_writable(cls_item, "name", 4, false);
                js_attr_set_enumerable(cls_item, "name", 4, false);
                js_attr_set_configurable(cls_item, "name", 4, true);
            }
        }
        return;
    }
    String* name_key_str = heap_create_name("name", 4);
    map_put(cls_item.map, name_key_str, name_item, js_input);
    js_attr_set_writable(cls_item, "name", 4, false);
    js_attr_set_enumerable(cls_item, "name", 4, false);
    js_attr_set_configurable(cls_item, "name", 4, true);
}

extern "C" void js_set_default_constructor_property(Item proto_item, Item cls_item) {
    if (get_type_id(proto_item) != LMD_TYPE_MAP) return;
    ShapeEntry* existing = js_find_shape_entry(proto_item, "constructor", 11);
    if (existing && !jspd_is_deleted(existing)) return;
    Item key = (Item){.item = s2it(heap_create_name("constructor", 11))};
    js_property_set(proto_item, key, cls_item);
    js_attr_set_enumerable(proto_item, "constructor", 11, false);
}

extern "C" void js_prepare_class_prototype_property(Item cls_item) {
    if (get_type_id(cls_item) != LMD_TYPE_MAP) return;
    ShapeEntry* existing = js_find_shape_entry(cls_item, "prototype", 9);
    if (existing && !jspd_is_deleted(existing)) {
        js_throw_type_error("Cannot redefine property: prototype");
    }
}

extern "C" void js_check_class_static_field_key(Item key_item) {
    if (get_type_id(key_item) != LMD_TYPE_STRING) return;
    String* key = it2s(key_item);
    if (key && key->len == 9 && strncmp(key->chars, "prototype", 9) == 0) {
        js_throw_type_error("Cannot redefine property: prototype");
    }
}

// Set the source text of a JsFunction for Function.prototype.toString
extern "C" void js_set_function_source(Item fn_item, Item source_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    if (get_type_id(source_item) != LMD_TYPE_STRING) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    if (fn->func_ptr) {
        fn->source_text = it2s(source_item);
    }
}
