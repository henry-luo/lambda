/**
 * JavaScript runtime function object wrappers for Lambda.
 */
#include "js_runtime_internal.hpp"
#include "../../lib/memtrack.h"
#include "../runtime/gc/gc_heap.h"
#include "../runtime/side_stack.h"

extern __thread EvalContext* context;
extern void heap_register_gc_root(uint64_t* slot);

// =============================================================================
// Function object wrappers
// =============================================================================

#define JS_FUNCTION_SIZE_CLASS 7
static_assert(sizeof(JsFunction) <= 384,
              "JsFunction must fit its GC object-zone size class");

extern "C" JsFunction* js_alloc_gc_function_object(void) {
    // JsFunction exceeds the old 256-byte ceiling; keeping it in a dedicated
    // pooled class avoids a malloc/memset pair for every loop-created closure.
    JsFunction* fn = (JsFunction*)heap_calloc_class(
        sizeof(JsFunction), LMD_TYPE_FUNC, JS_FUNCTION_SIZE_CLASS);
    if (!fn) return NULL;
    fn->type_id = LMD_TYPE_FUNC;
    fn->layout_magic = JS_FUNCTION_LAYOUT_MAGIC;
    js_function_init_native_module_scope(fn);
    return fn;
}

extern "C" void js_function_root_item_if_needed(void* function, Item* slot) {
    JsFunction* fn = (JsFunction*)function;
    if (!fn || !slot) return;
    if (context && context->heap && context->heap->gc &&
        gc_is_managed(context->heap->gc, fn)) return;
    heap_register_gc_root(&slot->item);
}

void js_function_call_lane_recompute(JsFunction* fn) {
    if (!fn) return;
    fn->call_lane_kind = JS_CALL_LANE_GENERIC;
    // This is the single writer of the executable classification. Everything
    // that mutates the metadata an entry relies on funnels back here, so a
    // stamped entry can never outlive the facts it was chosen from.
    fn->invoke = js_call_entry_generic;
    // A stale fast classification is wrongness, not merely slowness. New or
    // dynamically-created wrappers stay generic until finalization supplies
    // every fact the ordinary lane relies on.
    if (!(fn->flags & JS_FUNC_FLAG_ANALYSIS_KNOWN) || fn->builtin_id != 0 ||
        (fn->flags & (JS_FUNC_FLAG_HAS_BOUND_THIS | JS_FUNC_FLAG_GENERATOR |
            JS_FUNC_FLAG_ASYNC_GEN | JS_FUNC_FLAG_DERIVED_CTOR |
            JS_FUNC_FLAG_TYPED_ARRAY_METHOD)) ||
        fn->with_env_depth > 0 || (fn->flags & JS_FUNC_FLAG_USES_WITH) ||
        fn->eval_initializer_context || fn->vm_stack_filename ||
        fn->vm_stack_source) {
        return;
    }
    fn->call_lane_kind = fn->home_class.item != 0
        ? JS_CALL_LANE_METHOD_HOME : JS_CALL_LANE_ORDINARY;
    fn->invoke = js_function_select_call_entry(fn);
}

extern "C" void js_set_function_home_class(Item fn_item, Item home_class) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    if (!fn) return;
    fn->home_class = home_class;
    js_function_root_item_if_needed(fn, &fn->home_class);
    js_function_call_lane_recompute(fn);
}

extern "C" int js_function_gc_trace(void* data, gc_heap_t* gc) {
    JsFunction* fn = (JsFunction*)data;
    if (!fn) return 0;
    if (fn->layout_magic != JS_FUNCTION_LAYOUT_MAGIC) {
        JsAccessorPair* pair = (JsAccessorPair*)data;
        if (pair->layout_magic != JS_ACCESSOR_PAIR_LAYOUT_MAGIC) return 0;
        // Accessor pairs share the FUNC tag for property-slot compatibility,
        // but their getter and setter are the actual reachability edges.
        gc_mark_item(gc, pair->getter.item);
        gc_mark_item(gc, pair->setter.item);
        return 1;
    }

    // A GC-owned function is the reachability owner for its closure env and
    // bound argument vectors; tracing those edges replaces permanent root ranges.
    if (fn->env) {
        gc_mark_object_ptr(gc, fn->env);
        for (int i = 0; i < fn->env_size; i++) gc_mark_item(gc, fn->env[i].item);
    }
    gc_mark_item(gc, fn->prototype.item);
    gc_mark_item(gc, fn->bound_this_store[0].item);
    if (fn->bound_args) {
        gc_mark_object_ptr(gc, fn->bound_args);
        for (int i = 0; i < fn->bound_argc; i++) gc_mark_item(gc, fn->bound_args[i].item);
    }
    gc_mark_object_ptr(gc, fn->name);
    gc_mark_item(gc, fn->properties_map.item);
    gc_mark_item(gc, fn->home_global.item);
    gc_mark_item(gc, fn->home_class.item);
    gc_mark_object_ptr(gc, fn->source_text);
    if (fn->with_env) {
        gc_mark_object_ptr(gc, fn->with_env);
        for (int i = 0; i < fn->with_env_depth; i++) gc_mark_item(gc, fn->with_env[i].item);
    }
    gc_mark_object_ptr(gc, fn->vm_stack_filename);
    gc_mark_object_ptr(gc, fn->vm_stack_source);
    return 1;
}

extern "C" int js_function_gc_compact(void* data, gc_heap_t* gc) {
    JsFunction* fn = (JsFunction*)data;
    if (!fn) return 0;
    if (fn->layout_magic != JS_FUNCTION_LAYOUT_MAGIC) {
        JsAccessorPair* pair = (JsAccessorPair*)data;
        // Accessor pairs have no movable data-zone fields; skipping the legacy
        // Function compactor prevents its field offsets from corrupting them.
        return pair->layout_magic == JS_ACCESSOR_PAIR_LAYOUT_MAGIC;
    }
    if (!fn->env || fn->env_size <= 0 ||
        !gc_data_zone_owns(gc->data_zone, fn->env)) return 1;
    // JsFunction is not the legacy Function layout. Its environment includes
    // a scalar tail, so the generic Function compactor both misread its magic
    // bytes as a field count and left fn->env pointing into reset nursery data.
    size_t size = (size_t)fn->env_size * sizeof(Item) * 2;
    Item* moved = (Item*)gc_data_zone_copy(gc->tenured_data, fn->env, size);
    if (moved) fn->env = moved;
    return 1;
}

// Cache: func_ptr → JsFunction*  (ensures same MIR function → same wrapper → same .prototype)
// It is context-owned because both the wrapper identity and its GC lifetime are
// semantic state.  No cache operation synchronizes: one context has one owner
// thread, and the arrays have a fixed address for the capsule lifetime.

static JsFunction* js_func_cache_lookup(void* func_ptr) {
    for (int i = 0; i < js_runtime_state.function_cache_count; i++) {
        if (js_runtime_state.function_cache_keys[i] == func_ptr) {
            return js_runtime_state.function_cache_values[i];
        }
    }
    return NULL;
}

static void js_func_cache_insert(void* func_ptr, JsFunction* fn) {
    if (js_runtime_state.function_cache_count < JS_FUNCTION_CACHE_CAPACITY) {
        int slot = js_runtime_state.function_cache_count++;
        js_runtime_state.function_cache_keys[slot] = func_ptr;
        js_runtime_state.function_cache_values[slot] = fn;
    }
}

void js_func_cache_reset() {
    js_runtime_state.function_cache_count = 0;
    js_runtime_state.function_cache_suppress_depth = 0;
}

extern "C" void js_func_cache_suppress_push(void) {
    js_runtime_state.function_cache_suppress_depth++;
}

extern "C" void js_func_cache_suppress_pop(void) {
    if (js_runtime_state.function_cache_suppress_depth > 0) {
        js_runtime_state.function_cache_suppress_depth--;
    }
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

static Item js_new_function_impl(void* func_ptr, int param_count,
        bool mir_context_abi) {
    Context* runtime = mir_context_abi ? (Context*)context : NULL;
    if (mir_context_abi && !runtime) {
        log_error("js-new-function: compiled wrapper missing context owner");
        return ItemError;
    }
    if (!func_ptr) {
        log_error("js_new_function: null func_ptr! param_count=%d", param_count);
        return ItemNull;
    }
    // Return cached wrapper if the same MIR function was already wrapped.
    // This ensures Foo.prototype = {...} and (new Foo()) share the same JsFunction*.
    bool has_with_env = js_with_depth_active() != 0;
    bool suppress_cache = js_runtime_state.function_cache_suppress_depth > 0;
    JsFunction* cached = (has_with_env || suppress_cache) ? NULL : js_func_cache_lookup(func_ptr);
    if (cached) return (Item){.function = (Function*)cached};

    // Only cache-addressable compiled wrappers are module-lifetime. A wrapper
    // carrying `with` state or cache-suppressed identity must own traced edges.
    RootFrame roots(1);
    Rooted<Item> fn_root(roots, ItemNull);
    JsFunction* fn = (has_with_env || suppress_cache)
        ? js_alloc_gc_function_object()
        : (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    if (!fn) return ItemError;
    fn_root.set((Item){.function = (Function*)fn});
    js_function_init_native_module_scope(fn);
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = func_ptr;
    fn->runtime_context = runtime;
    fn->param_count = param_count;
    fn->formal_length = -1; // -1 = use param_count for .length
    fn->env = NULL;
    fn->env_size = 0;
    fn->prototype = ItemNull;
    fn->module_state_id = js_get_active_module_state_id();
    fn->home_global = js_get_global_this();
    js_function_root_item_if_needed(fn, &fn->home_global);
    js_function_capture_with_env(fn);
    // A fresh wrapper has no analysis yet, so this stamps the generic entry;
    // finalization reclassifies once the compiler's facts are applied.
    js_function_call_lane_recompute(fn);
    if (!has_with_env && !suppress_cache) js_func_cache_insert(func_ptr, fn);
    return (Item){.function = (Function*)fn};
}

extern "C" Item js_new_function(void* func_ptr, int param_count) {
    return js_new_function_impl(func_ptr, param_count, false);
}

extern "C" Item js_new_distinct_function(void* func_ptr, int param_count) {
    // Native callbacks can need distinct JS identity/prototype state. Suppress
    // only wrapper caching; using the compiled-method constructor here would
    // incorrectly impose its Context* ABI on an ordinary native callback.
    js_func_cache_suppress_push();
    Item fn = js_new_function_impl(func_ptr, param_count, false);
    js_func_cache_suppress_pop();
    return fn;
}

extern "C" Item js_new_function_mir(void* func_ptr, int param_count) {
    return js_new_function_impl(func_ptr, param_count, true);
}

static Item js_new_method_function_impl(void* func_ptr, int param_count,
        bool mir_context_abi) {
    Context* runtime = mir_context_abi ? (Context*)context : NULL;
    if (mir_context_abi && !runtime) {
        log_error("js-new-method: compiled wrapper missing context owner");
        return ItemError;
    }
    if (!func_ptr) {
        log_error("js_new_method_function: null func_ptr! param_count=%d", param_count);
        return ItemNull;
    }
    // Method wrappers are not in the func_ptr identity cache, so ordinary GC
    // ownership avoids retaining every dynamically materialized method.
    RootFrame roots(1);
    Rooted<Item> fn_root(roots, ItemNull);
    JsFunction* fn = js_alloc_gc_function_object();
    if (!fn) return ItemError;
    // The wrapper is fresh and not yet reachable from its owning object while
    // global/with capture helpers may allocate.
    fn_root.set((Item){.function = (Function*)fn});
    fn->func_ptr = func_ptr;
    fn->runtime_context = runtime;
    if (runtime) {
        // Only compiled method wrappers carry an explicit Context*. Jube
        // trampolines and native interface callbacks use the ordinary ABI;
        // stamping those contextless callbacks shifted every call argument.
        fn->flags |= JS_FUNC_FLAG_MIR_PUBLIC_ABI | JS_FUNC_FLAG_MIR_CONTEXT_ABI;
    }
    fn->param_count = param_count;
    fn->formal_length = -1;
    fn->env = NULL;
    fn->env_size = 0;
    fn->prototype = ItemNull;
    fn->module_state_id = js_get_active_module_state_id();
    fn->home_global = js_get_global_this();
    js_function_root_item_if_needed(fn, &fn->home_global);
    js_function_capture_with_env(fn);
    js_function_call_lane_recompute(fn);
    return (Item){.function = (Function*)fn};
}

extern "C" Item js_new_method_function(void* func_ptr, int param_count) {
    return js_new_method_function_impl(func_ptr, param_count, false);
}

extern "C" Item js_new_method_function_mir(void* func_ptr, int param_count) {
    return js_new_method_function_impl(func_ptr, param_count, true);
}

// Create a closure (function with captured environment)
static Item js_new_closure_impl(void* func_ptr, int param_count, Item* env,
        int env_size, bool mir_context_abi) {
    Context* runtime = mir_context_abi ? (Context*)context : NULL;
    if (mir_context_abi && !runtime) {
        log_error("js-new-closure: compiled wrapper missing context owner");
        return ItemError;
    }
    RootFrame roots(1);
    JsFunction* fn = NULL;
    {
        // A closure environment is movable data-zone storage, not an Item.
        // Attach it to its function before a collection can run; raw addresses
        // in an Item root are not valid GC reachability edges for data zones.
        AutoDeferGC defer_gc;
        fn = js_alloc_gc_function_object();
        if (!fn) return ItemError;
        fn->func_ptr = func_ptr;
        fn->runtime_context = runtime;
        fn->param_count = param_count;
        fn->formal_length = -1; // -1 = use param_count for .length
        fn->env = env;
        fn->env_size = env_size;
        fn->prototype = ItemNull;
        fn->module_state_id = js_get_active_module_state_id();
    }
    // A new closure is not owned by its caller until return. Root it across
    // scalar rehoming and dynamic-with capture, both of which may allocate.
    Rooted<Item> fn_root(roots, (Item){.function = (Function*)fn});
    fn->home_global = js_get_global_this();
    js_env_rehome_scalars(fn->env);
    js_function_capture_with_env(fn);
    js_function_call_lane_recompute(fn);
    return (Item){.function = (Function*)fn};
}

extern "C" Item js_new_closure(void* func_ptr, int param_count, Item* env,
        int env_size) {
    return js_new_closure_impl(func_ptr, param_count, env, env_size, false);
}

extern "C" Item js_new_closure_mir(void* func_ptr, int param_count,
        Item* env, int env_size) {
    return js_new_closure_impl(func_ptr, param_count, env, env_size, true);
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
    return (Item*)heap_calloc_closure_env((size_t)count * sizeof(Item));
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

static void js_mark_function_flags(Item fn_item, uint32_t flags) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->flags |= flags;
    js_function_call_lane_recompute(fn);
}

// v20: Mark a function as a generator (generator prototype has no constructor)
extern "C" void js_mark_generator_func(Item fn_item) {
    js_mark_function_flags(fn_item, JS_FUNC_FLAG_GENERATOR);
}

// Mark a function as an async generator function (sets both GENERATOR and ASYNC_GEN flags)
extern "C" void js_mark_async_generator_func(Item fn_item) {
    js_mark_function_flags(fn_item, JS_FUNC_FLAG_GENERATOR | JS_FUNC_FLAG_ASYNC_GEN);
}

// Mark a function as an async (non-generator) function — affects [[Prototype]]/.constructor
extern "C" void js_mark_async_func(Item fn_item) {
    js_mark_function_flags(fn_item, JS_FUNC_FLAG_ASYNC);
}

extern "C" void js_mark_derived_constructor_func(Item fn_item) {
    js_mark_function_flags(fn_item, JS_FUNC_FLAG_DERIVED_CTOR);
}

// Mark a function as an arrow function (non-constructable)
extern "C" void js_mark_arrow_func(Item fn_item) {
    js_mark_function_flags(fn_item, JS_FUNC_FLAG_ARROW);
}

extern "C" void js_mark_method_func(Item fn_item) {
    js_mark_function_flags(fn_item, JS_FUNC_FLAG_METHOD);
}

extern "C" void js_mark_eval_initializer_func_if_active(Item fn_item) {
    if (!js_private_field_initializing && !js_eval_initializer_context) return;
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->eval_initializer_context = true;
    js_function_call_lane_recompute(fn);
}

// Mark a function as strict mode (ES spec [[Strict]] internal slot)
extern "C" void js_mark_strict_func(Item fn_item) {
    js_mark_function_flags(fn_item, JS_FUNC_FLAG_STRICT);
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
    if (init_flags & JS_FUNC_INIT_MIR_PUBLIC_ABI) fn->flags |= JS_FUNC_FLAG_MIR_PUBLIC_ABI;
    if (init_flags & JS_FUNC_INIT_USES_WITH) fn->flags |= JS_FUNC_FLAG_USES_WITH;
    if (init_flags & JS_FUNC_INIT_ANALYSIS_KNOWN) fn->flags |= JS_FUNC_FLAG_ANALYSIS_KNOWN;
    if (init_flags & JS_FUNC_INIT_READS_THIS) fn->flags |= JS_FUNC_FLAG_READS_THIS;
    if (init_flags & JS_FUNC_INIT_READS_NEW_TARGET) fn->flags |= JS_FUNC_FLAG_READS_NEW_TARGET;
    if (init_flags & JS_FUNC_INIT_MIR_CONTEXT_ABI) fn->flags |= JS_FUNC_FLAG_MIR_CONTEXT_ABI;
    if (js_private_field_initializing || js_eval_initializer_context) {
        fn->eval_initializer_context = true;
    }
    js_function_call_lane_recompute(fn);
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

static Item js_private_display_name_item(Item name_item) {
    // Private NameRecords retain their source spelling; rewriting it could
    // conflate a valid #123_name with a former compiler-private encoding.
    return name_item;
}

static int js_function_name_from_symbol_key(PropertyKeyRef key, char* out, int out_size) {
    if (!key || !property_key_requires_identity(key) ||
            property_key_kind(key) != NAME_KEY_SYMBOL) return -1;
    // SetFunctionName uses an empty name for Symbol(), not the diagnostic
    // bracket form that is required only when the Symbol has a description.
    if (key->len == 0) {
        if (out_size > 0) out[0] = '\0';
        return 0;
    }
    int len = snprintf(out, out_size, "[%.*s]", (int)key->len, key->chars);
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
        base_len = key->len < (int)sizeof(base) - 1 ? (int)key->len : (int)sizeof(base) - 1;
        memcpy(base, key->chars, base_len);
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
