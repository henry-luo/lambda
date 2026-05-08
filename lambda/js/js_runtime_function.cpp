/**
 * JavaScript runtime function object wrappers for Lambda.
 */
#include "js_runtime_internal.hpp"

// =============================================================================
// Function object wrappers
// =============================================================================

// Cache: func_ptr → JsFunction*  (ensures same MIR function → same wrapper → same .prototype)
static const int JS_FUNC_CACHE_SIZE = 512;
static void* js_func_cache_keys[512];
static JsFunction* js_func_cache_vals[512];
static int js_func_cache_count = 0;

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
    JsFunction* cached = js_func_cache_lookup(func_ptr);
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
    js_func_cache_insert(func_ptr, fn);
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

// Mark a function as strict mode (ES spec [[Strict]] internal slot)
extern "C" void js_mark_strict_func(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->flags |= JS_FUNC_FLAG_STRICT;
}

// Set the name of a JsFunction (called from transpiler after js_new_function/js_new_closure)
extern "C" void js_set_function_name(Item fn_item, Item name_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    if (get_type_id(name_item) != LMD_TYPE_STRING) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    if (fn->func_ptr) { // is JsFunction layout
        fn->name = it2s(name_item);
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

