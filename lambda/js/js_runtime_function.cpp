/**
 * JavaScript runtime function object wrappers for Lambda.
 */
#include "js_runtime_internal.hpp"
#include "../../lib/memtrack.h"
#include "../../lib/gc/gc_heap.h"

extern __thread EvalContext* context;
extern void heap_register_gc_root(uint64_t* slot);

// =============================================================================
// Function object wrappers
// =============================================================================

extern "C" JsFunction* js_alloc_gc_function_object(void) {
    JsFunction* fn = (JsFunction*)heap_calloc(sizeof(JsFunction), LMD_TYPE_FUNC);
    if (!fn) return NULL;
    fn->type_id = LMD_TYPE_FUNC;
    fn->layout_magic = JS_FUNCTION_LAYOUT_MAGIC;
    return fn;
}

extern "C" void js_function_root_item_if_needed(void* function, Item* slot) {
    JsFunction* fn = (JsFunction*)function;
    if (!fn || !slot) return;
    if (context && context->heap && context->heap->gc &&
        gc_is_managed(context->heap->gc, fn)) return;
    heap_register_gc_root(&slot->item);
}

extern "C" int js_function_gc_trace(void* data, gc_heap_t* gc) {
    JsFunction* fn = (JsFunction*)data;
    if (!fn || fn->layout_magic != JS_FUNCTION_LAYOUT_MAGIC) return 0;

    // A GC-owned function is the reachability owner for its closure env and
    // bound argument vectors; tracing those edges replaces permanent root ranges.
    if (fn->env) {
        gc_mark_object_ptr(gc, fn->env);
        for (int i = 0; i < fn->env_size; i++) gc_mark_item(gc, fn->env[i].item);
    }
    gc_mark_item(gc, fn->prototype.item);
    gc_mark_item(gc, fn->bound_this.item);
    if (fn->bound_args) {
        gc_mark_object_ptr(gc, fn->bound_args);
        for (int i = 0; i < fn->bound_argc; i++) gc_mark_item(gc, fn->bound_args[i].item);
    }
    gc_mark_object_ptr(gc, fn->name);
    gc_mark_item(gc, fn->properties_map.item);
    gc_mark_item(gc, fn->home_global.item);
    gc_mark_object_ptr(gc, fn->source_text);
    if (fn->with_env) {
        gc_mark_object_ptr(gc, fn->with_env);
        for (int i = 0; i < fn->with_env_depth; i++) gc_mark_item(gc, fn->with_env[i].item);
    }
    gc_mark_object_ptr(gc, fn->vm_stack_filename);
    gc_mark_object_ptr(gc, fn->vm_stack_source);
    return 1;
}

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
    AutoAssertNoGC no_gc((Context*)context);
    return (int64_t)js_args_len;
}

// Pop back to a saved mark, re-zeroing the popped slots to preserve the
// [len, cap) zeroed invariant.
extern "C" void js_args_restore(int64_t mark) {
    AutoAssertNoGC no_gc((Context*)context);
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

extern "C" void js_args_stack_cleanup(void) {
    if (js_args_stack) {
        mem_free(js_args_stack);
        js_args_stack = NULL;
    }
    js_args_len = 0;
    js_args_registered = false;
}

extern "C" int64_t js_with_depth_active(void);
extern "C" Item* js_with_capture_stack(int* out_depth);
extern "C" Item js_get_global_this(void);
extern void heap_register_gc_root(uint64_t* slot);

static void js_function_capture_with_env(JsFunction* fn) {
    if (!fn || !js_with_depth_active()) return;
    int depth = 0;
    Item* stack = js_with_capture_stack(&depth);
    if (stack && depth > 0) {
        js_env_rehome_scalars(stack);
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
    js_function_root_item_if_needed(jsfn, &jsfn->prototype);
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

    // Only cache-addressable compiled wrappers are module-lifetime. A wrapper
    // carrying `with` state or cache-suppressed identity must own traced edges.
    RootFrame roots((Context*)context, 1);
    Rooted<JsFunction*> fn_root(roots, (JsFunction*)NULL);
    JsFunction* fn = (has_with_env || suppress_cache)
        ? js_alloc_gc_function_object()
        : (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    if (!fn) return ItemError;
    fn_root.set(fn);
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = func_ptr;
    fn->param_count = param_count;
    fn->formal_length = -1; // -1 = use param_count for .length
    fn->env = NULL;
    fn->env_size = 0;
    fn->prototype = ItemNull;
    fn->module_vars = js_active_module_vars; // bind to creating module's vars
    fn->home_global = js_get_global_this();
    js_function_root_item_if_needed(fn, &fn->home_global);
    js_function_capture_with_env(fn);
    if (!has_with_env && !suppress_cache) js_func_cache_insert(func_ptr, fn);
    return (Item){.function = (Function*)fn};
}

extern "C" Item js_new_method_function(void* func_ptr, int param_count) {
    if (!func_ptr) {
        log_error("js_new_method_function: null func_ptr! param_count=%d", param_count);
        return ItemNull;
    }
    // Method wrappers are not in the func_ptr identity cache, so ordinary GC
    // ownership avoids retaining every dynamically materialized method.
    RootFrame roots((Context*)context, 1);
    Rooted<JsFunction*> fn_root(roots, (JsFunction*)NULL);
    JsFunction* fn = js_alloc_gc_function_object();
    if (!fn) return ItemError;
    // The wrapper is fresh and not yet reachable from its owning object while
    // global/with capture helpers may allocate.
    fn_root.set(fn);
    fn->func_ptr = func_ptr;
    fn->param_count = param_count;
    fn->formal_length = -1;
    fn->env = NULL;
    fn->env_size = 0;
    fn->prototype = ItemNull;
    fn->module_vars = js_active_module_vars;
    fn->home_global = js_get_global_this();
    js_function_root_item_if_needed(fn, &fn->home_global);
    js_function_capture_with_env(fn);
    return (Item){.function = (Function*)fn};
}

// Create a closure (function with captured environment)
extern "C" Item js_new_closure(void* func_ptr, int param_count, Item* env, int env_size) {
    RootFrame roots((Context*)context, 2);
    // The environment has no reachability owner until the function object is
    // published. Root the raw GC allocation before allocating that function;
    // forced collection here otherwise reclaims the env and corrupts captures.
    Rooted<Item*> env_root(roots, env);
    Rooted<JsFunction*> fn_root(roots, (JsFunction*)NULL);
    JsFunction* fn = js_alloc_gc_function_object();
    if (!fn) return ItemError;
    // A new closure is not owned by its caller until return. Root it across
    // scalar rehoming and dynamic-with capture, both of which may allocate.
    fn_root.set(fn);
    fn->func_ptr = func_ptr;
    fn->param_count = param_count;
    fn->formal_length = -1; // -1 = use param_count for .length
    fn->env = env_root.get();
    fn->env_size = env_size;
    fn->prototype = ItemNull;
    fn->module_vars = js_active_module_vars; // bind to creating module's vars
    fn->home_global = js_get_global_this();
    js_env_rehome_scalars(fn->env);
    js_function_capture_with_env(fn);
    return (Item){.function = (Function*)fn};
}

// Set the ES spec formal .length for a function (params before first default, excl rest)
extern "C" void js_set_formal_length(Item fn_item, int length) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->formal_length = (int16_t)length;
}

// Allocate a traced raw Item environment. Its owning closure/function keeps the
// allocation live; the GC header supplies the exact slot count to the tracer.
extern "C" Item* js_alloc_env(int count) {
    if (count <= 0) return NULL;
    return (Item*)heap_calloc_js_env((size_t)count * sizeof(Item));
}

static bool js_env_slot_is_side_number(Item item) {
    if (!context || !context->side_number_base || !context->side_number_top) return false;
    uint8_t tag = (uint8_t)(item.item >> 56);
    if (item.item & ITEM_DBL_MASK) return false;

    uintptr_t payload = item.item & ~ITEM_HIGH_BYTE_MASK;
    if (tag == LMD_TYPE_INT64 || tag == LMD_TYPE_UINT64) {
    } else if (tag == LMD_TYPE_FLOAT || tag == LMD_TYPE_FLOAT64) {
        if (payload <= 1) return false;
    } else {
        return false;
    }

    uintptr_t base = (uintptr_t)context->side_number_base;
    uintptr_t top = (uintptr_t)context->side_number_top;
    return payload >= base && payload < top &&
        (payload - base) % sizeof(uint64_t) == 0;
}

extern "C" void js_env_rehome_scalars(Item* env) {
    if (!env || !context || !context->heap || !context->heap->gc ||
            !gc_is_managed(context->heap->gc, env)) return;
    gc_header_t* header = gc_get_header(env);
    if (header->type_tag != GC_TYPE_JS_ENV || header->alloc_size == 0) return;
    int64_t count = (int64_t)(header->alloc_size / (2 * sizeof(Item)));
    // Generator environments mix boxed Items with raw state/spill words. Only
    // tagged pointers into the active number stack are valid scalar Items;
    // decoding raw words as Items can dereference small state values as pointers.
    for (int64_t i = 0; i < count; i++) {
        if (js_env_slot_is_side_number(env[i])) {
            owned_item_slot_store(env, count, i, env[i]);
        }
    }
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

extern "C" void js_finalize_function(Item fn_item, Item name_item,
        Item source_item, int formal_length, int init_flags) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    // Lowering already canonicalizes private display names. Direct field setup
    // keeps this pre-publication transaction allocation-free and non-reentrant.
    JsFunction* fn = (JsFunction*)fn_item.function;
    if (get_type_id(name_item) == LMD_TYPE_STRING) fn->name = it2s(name_item);
    if (get_type_id(source_item) == LMD_TYPE_STRING) fn->source_text = it2s(source_item);
    if (formal_length >= 0) fn->formal_length = (int16_t)formal_length;
    if (init_flags & JS_FUNC_INIT_GENERATOR) fn->flags |= JS_FUNC_FLAG_GENERATOR;
    if (init_flags & JS_FUNC_INIT_ASYNC_GENERATOR) {
        fn->flags |= JS_FUNC_FLAG_GENERATOR | JS_FUNC_FLAG_ASYNC_GEN;
    }
    if (init_flags & JS_FUNC_INIT_ASYNC) fn->flags |= JS_FUNC_FLAG_ASYNC;
    if (init_flags & JS_FUNC_INIT_ARROW) fn->flags |= JS_FUNC_FLAG_ARROW;
    if (init_flags & JS_FUNC_INIT_STRICT) fn->flags |= JS_FUNC_FLAG_STRICT;
    if (js_private_field_initializing || js_eval_initializer_context) {
        fn->eval_initializer_context = true;
    }
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
