/**
 * JavaScript runtime function object wrappers for Lambda.
 */
#include "js_runtime_internal.hpp"
#include "../../lib/memtrack.h"
#include "../runtime/gc/gc_heap.h"
#include "../runtime/side_stack.h"

extern __thread EvalContext* context;

// =============================================================================
// Function object wrappers
// =============================================================================

#define JS_FUNCTION_SIZE_CLASS 7
static_assert(sizeof(JsFunction) <= 384,
              "JsFunction must fit its GC object-zone size class");

extern "C" JsFunction* js_alloc_gc_function_object(void) {
    // Source-class capabilities extend the callable carrier beyond the old
    // 256-byte class; keep the GC allocation class explicit with the layout.
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

static int js_function_metadata_length(JsFunction* fn) {
    int length = fn->formal_length >= 0 ? fn->formal_length : fn->param_count;
    if (length < 0) length = -length - 1;
    if (fn->bound_args) {
        length -= fn->bound_argc;
        if (length < 0) length = 0;
    }
    return length;
}

static void js_function_store_metadata_property(JsFunction* fn,
        const char* name, int name_length, Item value) {
    if (!fn) return;
    RootFrame roots(3);
    Rooted<Item> function_root(roots,
        (Item){.function = (Function*)fn});
    Rooted<Item> value_root(roots, value);
    Rooted<Item> key_root(roots,
        (Item){.item = s2it(heap_create_name(name, name_length))});
    // Function metadata is initialized by [[DefineOwnProperty]], not ordinary
    // assignment: its non-writable descriptor must still accept a later
    // SetFunctionName/SetFunctionLength before publication (D6.2.2v2).
    js_func_init_property(function_root.get(), key_root.get(), value_root.get());
    js_mark_non_writable(function_root.get(), key_root.get());
    js_mark_non_enumerable(function_root.get(), key_root.get());
}

static void js_function_ensure_metadata_properties(JsFunction* fn) {
    if (!fn) return;
    RootFrame roots(1);
    Rooted<Item> function_root(roots,
        (Item){.function = (Function*)fn});
    fn = (JsFunction*)function_root.get().function;
    JsShapeSlotStatus name_status = JS_SHAPE_SLOT_ABSENT;
    JsShapeSlotStatus length_status = JS_SHAPE_SLOT_ABSENT;
    if (get_type_id(fn->properties_map) == LMD_TYPE_MAP) {
        name_status = js_own_shape_slot_status(
            fn->properties_map, "name", 4, NULL, NULL);
        length_status = js_own_shape_slot_status(
            fn->properties_map, "length", 6, NULL, NULL);
    }
    if (length_status == JS_SHAPE_SLOT_ABSENT) {
        js_function_store_metadata_property(fn, "length", 6,
            (Item){.item = i2it(js_function_metadata_length(fn))});
    }
    if (name_status == JS_SHAPE_SLOT_ABSENT) {
        Item name_value = (Item){.item = s2it(fn->name
            ? fn->name : heap_create_name("", 0))};
        js_function_store_metadata_property(fn, "name", 4, name_value);
    }
}

extern "C" bool js_function_has_own_prototype(Item function) {
    if (get_type_id(function) != LMD_TYPE_FUNC) return false;
    JsFunction* fn = (JsFunction*)function.function;
    if (!fn || (fn->flags & JS_FUNC_FLAG_HAS_BOUND_THIS) ||
        (fn->flags & JS_FUNC_FLAG_ARROW) ||
        ((fn->flags & JS_FUNC_FLAG_ASYNC) &&
            !(fn->flags & JS_FUNC_FLAG_GENERATOR)) ||
        ((fn->flags & JS_FUNC_FLAG_METHOD) &&
            !(fn->flags & JS_FUNC_FLAG_GENERATOR)) ||
        (fn->flags & JS_FUNC_FLAG_TYPED_ARRAY_METHOD) ||
        fn->native_construct == js_intrinsic_ctor_proxy_construct_body) {
        return false;
    }
    if (fn->intrinsic_class == JS_CLASS_SYMBOL ||
            fn->intrinsic_class == JS_CLASS_BIGINT) {
        // D6.2.2v2: [[Construct]] and own properties are independent.
        // Symbol and BigInt deliberately reject construction but still own
        // their specification-defined prototype objects.
        return true;
    }
    // D6.2.2v2: the finalized construct entry is the authoritative capability;
    // catalog IDs and mutable function names cannot create an own prototype.
    return fn->construct != NULL ||
        (fn->flags & JS_FUNC_FLAG_GENERATOR) != 0;
}

static void js_function_refresh_name_property(JsFunction* fn) {
    if (!fn) return;
    RootFrame roots(1);
    Rooted<Item> function_root(roots,
        (Item){.function = (Function*)fn});
    fn = (JsFunction*)function_root.get().function;
    Item name_value = (Item){.item = s2it(fn && fn->name
        ? fn->name : heap_create_name("", 0))};
    js_function_store_metadata_property(fn, "name", 4, name_value);
}

static void js_function_refresh_length_property(JsFunction* fn) {
    if (!fn) return;
    js_function_store_metadata_property(fn, "length", 6,
        (Item){.item = i2it(js_function_metadata_length(fn))});
}

static void js_function_register_pool_pointer_roots(JsFunction* fn) {
    if (!fn || fn->pool_pointer_roots_registered || !context ||
            !context->heap || !context->heap->gc ||
            gc_is_managed(context->heap->gc, fn)) return;
    // D5.3.3/D5.4.3: rooting a pool-backed function Item does not trace its
    // fields. Register raw metadata slots before finalization allocates, or a
    // freshly assigned name/source can be collected through the pool record.
    bool registered = heap_try_register_gc_root((uint64_t*)&fn->name);
    registered = heap_try_register_gc_root((uint64_t*)&fn->source_text) &&
        registered;
    registered = heap_try_register_gc_root((uint64_t*)&fn->vm_stack_filename) &&
        registered;
    registered = heap_try_register_gc_root((uint64_t*)&fn->vm_stack_source) &&
        registered;
    if (registered) {
        fn->pool_pointer_roots_registered =
            JS_FUNC_POOL_POINTER_ROOTS_REGISTERED;
    }
}

void js_function_finalize_capabilities(JsFunction* fn) {
    if (!fn) return;
    js_function_register_pool_pointer_roots(fn);
    JsConstructEntry inherited_construct = fn->construct;
    fn->call_lane_kind = JS_CALL_LANE_GENERIC;
    // D6.2.2v2 requires one writer for published executable capabilities;
    // metadata mutation must re-finalize before the value is republished.
    fn->invoke = (fn->flags & JS_FUNC_FLAG_HAS_BOUND_THIS)
        ? js_call_entry_bound : js_call_entry_generic;
    fn->construct = NULL;
    bool syntax_forbids_construct = (fn->flags & (JS_FUNC_FLAG_ARROW |
        JS_FUNC_FLAG_METHOD | JS_FUNC_FLAG_GENERATOR | JS_FUNC_FLAG_ASYNC |
        JS_FUNC_FLAG_TYPED_ARRAY_METHOD)) != 0;
    if (syntax_forbids_construct) {
        fn->construct = NULL;
    } else if (fn->flags & JS_FUNC_FLAG_HAS_BOUND_THIS) {
        if (inherited_construct) fn->construct = js_construct_entry_bound;
    } else if (fn->native_construct) {
        fn->construct = js_construct_entry_native;
    } else if (inherited_construct) {
        fn->construct = inherited_construct;
    } else if (!fn->native_call && fn->func_ptr) {
        fn->construct = js_construct_entry_ordinary;
    }
    js_function_ensure_metadata_properties(fn);
    // A stale fast classification is wrongness, not merely slowness. New or
    // dynamically-created wrappers stay generic until finalization supplies
    // every fact the ordinary lane relies on.
    if (!(fn->flags & JS_FUNC_FLAG_ANALYSIS_KNOWN) || fn->native_call ||
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
    js_function_finalize_capabilities(fn);
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
    gc_mark_item(gc, fn->bound_target.item);
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
    gc_mark_item(gc, fn->class_constructor.item);
    gc_mark_item(gc, fn->class_instance_prototype.item);
    gc_mark_item(gc, fn->class_superclass.item);
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

// Cache: executable identity → JsFunction*. MIR and native target kinds are
// disjoint so the same machine address can never alias a different ABI.
// It is context-owned because both the wrapper identity and its GC lifetime are
// semantic state.  No cache operation synchronizes: one context has one owner
// thread, and the arrays have a fixed address for the capsule lifetime.

enum JsFunctionCacheKind : uint8_t {
    JS_FUNCTION_CACHE_MIR = 1,
    JS_FUNCTION_CACHE_NATIVE = 2,
};
JS_FORWARD_STATIC_EXPRESSION(bool, js_function_cache_key_equal, (const JsRuntimeState::JsFunctionCacheKey& left,         const JsRuntimeState::JsFunctionCacheKey& right), (left.target_bits == right.target_bits && left.arity == right.arity && left.kind == right.kind && left.policy == right.policy && left.capabilities == right.capabilities))

static JsRuntimeState::JsFunctionCacheKey js_mir_cache_key(void* target,
        int arity) {
    uint64_t bits = 0;
    static_assert(sizeof(target) <= sizeof(bits),
        "MIR target must fit the callable identity key");
    memcpy(&bits, &target, sizeof(target));
    return {bits, (int16_t)arity, JS_FUNCTION_CACHE_MIR,
        JS_NATIVE_CALL_NONE, 0};
}

template <typename Target>
static JsRuntimeState::JsFunctionCacheKey js_native_cache_key(Target target,
        int arity, JsNativeCallPolicy policy, bool constructable = false) {
    uint64_t bits = 0;
    static_assert(sizeof(target) <= sizeof(bits),
        "native target must fit the callable identity key");
    memcpy(&bits, &target, sizeof(target));
    // D6.2.2v2: call-only and constructable bindings using one C target have
    // different capabilities and must never alias through the target cache.
    return {bits, (int16_t)arity, JS_FUNCTION_CACHE_NATIVE, (uint8_t)policy,
        (uint8_t)(constructable ? 1 : 0)};
}

static JsFunction* js_func_cache_lookup(
        const JsRuntimeState::JsFunctionCacheKey& key) {
    for (int i = 0; i < js_runtime_state.function_cache_count; i++) {
        if (js_function_cache_key_equal(js_runtime_state.function_cache_keys[i],
                key)) {
            return js_runtime_state.function_cache_values[i];
        }
    }
    return NULL;
}

static void js_func_cache_insert(const JsRuntimeState::JsFunctionCacheKey& key,
        JsFunction* fn) {
    if (js_runtime_state.function_cache_count < JS_FUNCTION_CACHE_CAPACITY) {
        int slot = js_runtime_state.function_cache_count++;
        js_runtime_state.function_cache_keys[slot] = key;
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

extern "C" Item* js_with_capture_stack(int* out_depth);

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
    // Typed native functions deliberately have no MIR pointer; the layout
    // marker prevents them from being reinterpreted as the legacy prefix.
    JsFunction* jsfn = (JsFunction*)fn_item.function;
    if (jsfn->layout_magic == JS_FUNCTION_LAYOUT_MAGIC) return jsfn->func_ptr;
    // Fall back to Function layout (ptr at offset 16)
    Function* fn = fn_item.function;
    return (void*)fn->ptr;
}


extern "C" int js_function_get_arity(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return 0;
    JsFunction* jsfn = (JsFunction*)fn_item.function;
    // The layout marker, not a target field, distinguishes JsFunction from the
    // compact legacy Function prefix; typed native targets intentionally leave
    // the MIR-only func_ptr null.
    if (jsfn->layout_magic == JS_FUNCTION_LAYOUT_MAGIC) return jsfn->param_count;
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

static JsFunction* js_alloc_function_storage(bool gc_backed) {
    return gc_backed ? js_alloc_gc_function_object()
        : (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
}

static void js_function_init_common(JsFunction* fn, int param_count) {
    js_function_init_native_module_scope(fn);
    fn->type_id = LMD_TYPE_FUNC;
    // D6.2.2v2: every callable wrapper uses the canonical layout marker;
    // legacy arity/target decoding otherwise silently drops compiled args.
    fn->layout_magic = JS_FUNCTION_LAYOUT_MAGIC;
    fn->param_count = param_count;
    fn->formal_length = -1;
    fn->prototype = ItemNull;
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
    JsRuntimeState::JsFunctionCacheKey cache_key = js_mir_cache_key(func_ptr,
        param_count);
    JsFunction* cached = (has_with_env || suppress_cache) ? NULL
        : js_func_cache_lookup(cache_key);
    if (cached) return (Item){.function = (Function*)cached};

    // Only cache-addressable compiled wrappers are module-lifetime. A wrapper
    // carrying `with` state or cache-suppressed identity must own traced edges.
    RootFrame roots(1);
    Rooted<Item> fn_root(roots, ItemNull);
    JsFunction* fn = js_alloc_function_storage(has_with_env || suppress_cache);
    if (!fn) return ItemError;
    fn_root.set((Item){.function = (Function*)fn});
    js_function_init_common(fn, param_count);
    fn->func_ptr = func_ptr;
    fn->runtime_context = runtime;
    fn->env = NULL;
    fn->env_size = 0;
    fn->module_state_id = js_get_active_module_state_id();
    fn->home_global = js_get_global_this();
    js_function_root_item_if_needed(fn, &fn->home_global);
    js_function_capture_with_env(fn);
    // A fresh wrapper has no analysis yet, so this stamps the generic entry;
    // finalization reclassifies once the compiler's facts are applied.
    js_function_finalize_capabilities(fn);
    if (!has_with_env && !suppress_cache) js_func_cache_insert(cache_key, fn);
    return (Item){.function = (Function*)fn};
}

#define JS_DEFINE_NATIVE_CALL_ADAPTER(arity, member, params, call_args) \
    static Item js_native_call_##arity(Item fn_item, Item this_value, \
            Item* args, int argc, uint64_t* result_home) { \
        (void)this_value; (void)result_home; \
        JsFunction* fn = (JsFunction*)fn_item.function; \
        if (!fn || argc != arity || (arity > 0 && !args)) { \
            log_error("js-native-call-%d: adapted arity mismatch argc=%d", \
                arity, argc); \
            return ItemError; \
        } \
        params \
        return fn->native_target.member call_args; \
    }

JS_DEFINE_NATIVE_CALL_ADAPTER(0, p0, (void)args;, ())
JS_DEFINE_NATIVE_CALL_ADAPTER(1, p1, , (args[0]))
JS_DEFINE_NATIVE_CALL_ADAPTER(2, p2, , (args[0], args[1]))
JS_DEFINE_NATIVE_CALL_ADAPTER(3, p3, , (args[0], args[1], args[2]))
JS_DEFINE_NATIVE_CALL_ADAPTER(4, p4, , (args[0], args[1], args[2], args[3]))
JS_DEFINE_NATIVE_CALL_ADAPTER(5, p5, , (args[0], args[1], args[2], args[3], args[4]))
JS_DEFINE_NATIVE_CALL_ADAPTER(6, p6, , (args[0], args[1], args[2], args[3], args[4], args[5]))
JS_DEFINE_NATIVE_CALL_ADAPTER(7, p7, , (args[0], args[1], args[2], args[3], args[4], args[5], args[6]))
JS_DEFINE_NATIVE_CALL_ADAPTER(8, p8, , (args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]))

#undef JS_DEFINE_NATIVE_CALL_ADAPTER

static Item js_new_native_function_impl(JsNativeTarget target,
        JsNativeCallBody call_body,
        JsNativeConstructBody construct_body,
        const JsRuntimeState::JsFunctionCacheKey& cache_key,
        int arity, JsNativeCallPolicy policy, bool constructable,
        bool distinct) {
    if (!call_body) return ItemError;
    bool has_with_env = js_with_depth_active() != 0;
    bool suppress_cache = distinct ||
        js_runtime_state.function_cache_suppress_depth > 0;
    JsFunction* cached = (has_with_env || suppress_cache) ? NULL
        : js_func_cache_lookup(cache_key);
    if (cached) return (Item){.function = (Function*)cached};

    RootFrame roots(1);
    Rooted<Item> fn_root(roots, ItemNull);
    JsFunction* fn = js_alloc_function_storage(has_with_env || suppress_cache);
    if (!fn) return ItemError;
    fn_root.set((Item){.function = (Function*)fn});
    js_function_init_common(fn, policy == JS_NATIVE_CALL_REST ? -arity : arity);
    fn->native_target = target;
    fn->native_call = call_body;
    fn->native_construct = constructable
        ? (construct_body ? construct_body : js_native_construct_via_call_body)
        : NULL;
    fn->native_arity = (uint8_t)arity;
    fn->native_policy = (uint8_t)policy;
    // D6.2.2v2: host-native callbacks execute in the caller's module scope; assigning
    // the publication scope here makes nested eval copy from an unrelated
    // state and fails Test262's $262.evalScript/agent callbacks.
    fn->home_global = js_get_global_this();
    js_function_root_item_if_needed(fn, &fn->home_global);
    js_function_capture_with_env(fn);
    js_function_finalize_capabilities(fn);
    if (!has_with_env && !suppress_cache) js_func_cache_insert(cache_key, fn);
    return fn_root.get();
}

#define JS_DEFINE_NATIVE_FACTORIES(arity, type, member) \
    Item js_new_native_function(type target) { \
        JsNativeTarget stored = {}; stored.member = target; \
        return js_new_native_function_impl(stored, js_native_call_##arity, NULL, \
            js_native_cache_key(target, arity, JS_NATIVE_CALL_FIXED), arity, JS_NATIVE_CALL_FIXED, \
            false, false); \
    } \
    Item js_new_native_constructor(type target) { \
        JsNativeTarget stored = {}; stored.member = target; \
        return js_new_native_function_impl(stored, js_native_call_##arity, NULL, \
            js_native_cache_key(target, arity, JS_NATIVE_CALL_FIXED, true), arity, JS_NATIVE_CALL_FIXED, \
            true, false); \
    } \
    Item js_new_distinct_native_function(type target) { \
        JsNativeTarget stored = {}; stored.member = target; \
        return js_new_native_function_impl(stored, js_native_call_##arity, NULL, \
            js_native_cache_key(target, arity, JS_NATIVE_CALL_FIXED), arity, JS_NATIVE_CALL_FIXED, \
            false, true); \
    } \
    Item js_new_distinct_native_constructor(type target) { \
        JsNativeTarget stored = {}; stored.member = target; \
        return js_new_native_function_impl(stored, js_native_call_##arity, NULL, \
            js_native_cache_key(target, arity, JS_NATIVE_CALL_FIXED, true), arity, JS_NATIVE_CALL_FIXED, \
            true, true); \
    }

JS_DEFINE_NATIVE_FACTORIES(0, JsNativeP0, p0)
JS_DEFINE_NATIVE_FACTORIES(1, JsNativeP1, p1)
JS_DEFINE_NATIVE_FACTORIES(2, JsNativeP2, p2)
JS_DEFINE_NATIVE_FACTORIES(3, JsNativeP3, p3)
JS_DEFINE_NATIVE_FACTORIES(4, JsNativeP4, p4)
JS_DEFINE_NATIVE_FACTORIES(5, JsNativeP5, p5)
JS_DEFINE_NATIVE_FACTORIES(6, JsNativeP6, p6)
JS_DEFINE_NATIVE_FACTORIES(7, JsNativeP7, p7)
JS_DEFINE_NATIVE_FACTORIES(8, JsNativeP8, p8)

#undef JS_DEFINE_NATIVE_FACTORIES

static Item js_new_distinct_native_rest_function(JsNativeP1 target);
static Item js_new_distinct_native_rest_function(JsNativeP2 target);
static Item js_new_distinct_native_rest_function(JsNativeP3 target);
static Item js_new_distinct_native_rest_function(JsNativeP4 target);
static Item js_new_distinct_native_rest_function(JsNativeP5 target);
static Item js_new_distinct_native_rest_function(JsNativeP6 target);
static Item js_new_distinct_native_rest_function(JsNativeP7 target);
static Item js_new_distinct_native_rest_function(JsNativeP8 target);
static Item js_new_distinct_native_rest_constructor(JsNativeP1 target);
static Item js_new_distinct_native_rest_constructor(JsNativeP2 target);
static Item js_new_distinct_native_rest_constructor(JsNativeP3 target);
static Item js_new_distinct_native_rest_constructor(JsNativeP4 target);
static Item js_new_distinct_native_rest_constructor(JsNativeP5 target);
static Item js_new_distinct_native_rest_constructor(JsNativeP6 target);
static Item js_new_distinct_native_rest_constructor(JsNativeP7 target);
static Item js_new_distinct_native_rest_constructor(JsNativeP8 target);

// Publication helpers need binding identity, not target identity.  These
// private overloads preserve the adapter contract while forcing one function
// object per installed property; D6.2.2v2 permits body sharing but JC7 makes
// distinct identity the default unless an owner explicitly publishes an alias.
#define JS_DEFINE_NATIVE_ADAPTER_FACTORY(arity, type) \
    static Item js_new_native_adapter(type target, int adapter_arity, \
            bool constructor, bool distinct) { \
        if (adapter_arity == arity) { \
            if (constructor) return distinct ? js_new_distinct_native_constructor(target) : js_new_native_constructor(target); \
            return distinct ? js_new_distinct_native_function(target) : js_new_native_function(target); \
        } \
        if (arity > 0 && adapter_arity == -arity) { \
            if (constructor) return distinct ? js_new_distinct_native_rest_constructor(target) : js_new_native_rest_constructor(target); \
            return distinct ? js_new_distinct_native_rest_function(target) : js_new_native_rest_function(target); \
        } \
        log_error("js-native-adapter: target arity %d mismatches adapter %d", arity, adapter_arity); \
        return ItemError; \
    } \
    Item js_new_native_function(type target, int adapter_arity) { \
        return js_new_native_adapter(target, adapter_arity, false, false); \
    } \
    static Item js_new_distinct_native_function(type target, int adapter_arity) { \
        return js_new_native_adapter(target, adapter_arity, false, true); \
    } \
    Item js_new_native_constructor(type target, int adapter_arity) { \
        return js_new_native_adapter(target, adapter_arity, true, false); \
    } \
    static Item js_new_distinct_native_constructor(type target, int adapter_arity) { \
        return js_new_native_adapter(target, adapter_arity, true, true); \
    }

static Item js_new_native_adapter(JsNativeP0 target, int adapter_arity,
        bool constructor, bool distinct) {
    if (adapter_arity == 0) {
        if (constructor) return distinct ? js_new_distinct_native_constructor(target) : js_new_native_constructor(target);
        return distinct ? js_new_distinct_native_function(target) : js_new_native_function(target);
    }
    log_error("js-native-adapter: target arity 0 mismatches adapter %d", adapter_arity);
    return ItemError;
}

JS_FORWARD_LOCAL_RETURN(Item, js_new_native_function,
    (JsNativeP0 target, int adapter_arity), js_new_native_adapter,
    (target, adapter_arity, false, false))
JS_FORWARD_STATIC_ITEM(js_new_distinct_native_function, (JsNativeP0 target, int adapter_arity), js_new_native_adapter, (target, adapter_arity, false, true))
JS_FORWARD_LOCAL_RETURN(Item, js_new_native_constructor,
    (JsNativeP0 target, int adapter_arity), js_new_native_adapter,
    (target, adapter_arity, true, false))
JS_FORWARD_STATIC_ITEM(js_new_distinct_native_constructor, (JsNativeP0 target, int adapter_arity), js_new_native_adapter, (target, adapter_arity, true, true))

JS_DEFINE_NATIVE_ADAPTER_FACTORY(1, JsNativeP1)
JS_DEFINE_NATIVE_ADAPTER_FACTORY(2, JsNativeP2)
JS_DEFINE_NATIVE_ADAPTER_FACTORY(3, JsNativeP3)
JS_DEFINE_NATIVE_ADAPTER_FACTORY(4, JsNativeP4)
JS_DEFINE_NATIVE_ADAPTER_FACTORY(5, JsNativeP5)
JS_DEFINE_NATIVE_ADAPTER_FACTORY(6, JsNativeP6)
JS_DEFINE_NATIVE_ADAPTER_FACTORY(7, JsNativeP7)
JS_DEFINE_NATIVE_ADAPTER_FACTORY(8, JsNativeP8)

#undef JS_DEFINE_NATIVE_ADAPTER_FACTORY

#define JS_DEFINE_NATIVE_INSTALLER(name_suffix, factory, arity, type, adapter_param, adapter_arg) \
    Item js_install_native_##name_suffix(Item object, const char* name, \
            type target adapter_param) { \
        RootFrame roots(3); \
        Rooted<Item> object_root(roots, object); \
        Rooted<Item> key_root(roots, make_string_item(name)); \
        /* Function creation can collect before Set owns the key/value. */ \
        Rooted<Item> function_root(roots, \
            factory(target adapter_arg)); \
        js_set_key_default(object_root.get(), key_root.get(), function_root.get()); \
        return function_root.get(); \
    }

#define JS_DEFINE_NATIVE_METHOD_INSTALLER(arity, type) \
    JS_DEFINE_NATIVE_INSTALLER(method, js_new_distinct_native_function, arity, type, \
        JS_COMMA int adapter_arity, JS_COMMA adapter_arity) \
    Item js_install_native_method(Item object, const char* name, type target) { \
        return js_install_native_method(object, name, target, arity); \
    }

#define JS_COMMA ,

#define JS_DEFINE_NATIVE_CONSTRUCTOR_INSTALLER(arity, type) \
    JS_DEFINE_NATIVE_INSTALLER(constructor, js_new_distinct_native_constructor, arity, \
        type, JS_COMMA int adapter_arity, JS_COMMA adapter_arity) \
    Item js_install_native_constructor(Item object, const char* name, \
            type target) { \
        return js_install_native_constructor(object, name, target, arity); \
    }

// D5.2/D6.2.2v2: all native-method publication uses one exact-rooted shape;
// argument evaluation order must never decide whether the key survives GC.
JS_DEFINE_NATIVE_METHOD_INSTALLER(0, JsNativeP0)
JS_DEFINE_NATIVE_METHOD_INSTALLER(1, JsNativeP1)
JS_DEFINE_NATIVE_METHOD_INSTALLER(2, JsNativeP2)
JS_DEFINE_NATIVE_METHOD_INSTALLER(3, JsNativeP3)
JS_DEFINE_NATIVE_METHOD_INSTALLER(4, JsNativeP4)
JS_DEFINE_NATIVE_METHOD_INSTALLER(5, JsNativeP5)
JS_DEFINE_NATIVE_METHOD_INSTALLER(6, JsNativeP6)
JS_DEFINE_NATIVE_METHOD_INSTALLER(7, JsNativeP7)
JS_DEFINE_NATIVE_METHOD_INSTALLER(8, JsNativeP8)

JS_DEFINE_NATIVE_CONSTRUCTOR_INSTALLER(0, JsNativeP0)
JS_DEFINE_NATIVE_CONSTRUCTOR_INSTALLER(1, JsNativeP1)
JS_DEFINE_NATIVE_CONSTRUCTOR_INSTALLER(2, JsNativeP2)
JS_DEFINE_NATIVE_CONSTRUCTOR_INSTALLER(3, JsNativeP3)
JS_DEFINE_NATIVE_CONSTRUCTOR_INSTALLER(4, JsNativeP4)
JS_DEFINE_NATIVE_CONSTRUCTOR_INSTALLER(5, JsNativeP5)
JS_DEFINE_NATIVE_CONSTRUCTOR_INSTALLER(6, JsNativeP6)
JS_DEFINE_NATIVE_CONSTRUCTOR_INSTALLER(7, JsNativeP7)
JS_DEFINE_NATIVE_CONSTRUCTOR_INSTALLER(8, JsNativeP8)

#undef JS_DEFINE_NATIVE_METHOD_INSTALLER
#undef JS_DEFINE_NATIVE_CONSTRUCTOR_INSTALLER
#undef JS_DEFINE_NATIVE_INSTALLER
#undef JS_COMMA

Item js_initialize_native_constructor_prototype(Item constructor,
        Item prototype) {
    RootFrame roots(3);
    Rooted<Item> constructor_root(roots, constructor);
    Rooted<Item> prototype_root(roots, prototype);
    Rooted<Item> key_root(roots, make_string_item("prototype"));
    if (get_type_id(constructor_root.get()) != LMD_TYPE_FUNC ||
            !js_function_has_own_prototype(constructor_root.get())) {
        return js_throw_type_error(
            "native prototype initialization requires a constructor");
    }
    JsFunction* fn = (JsFunction*)constructor_root.get().function;
    // D5.4.3/D6.2.2v2: native publication initializes the hidden construct
    // payload and the real own data property in one exact-rooted transaction.
    // Ordinary Set cannot do this because a built-in prototype is non-writable
    // once its lazy descriptor has been materialized.
    fn->prototype = prototype_root.get();
    js_function_root_item_if_needed(fn, &fn->prototype);
    js_func_init_property(constructor_root.get(), key_root.get(),
        prototype_root.get());
    js_mark_non_writable(constructor_root.get(), key_root.get());
    js_mark_non_enumerable(constructor_root.get(), key_root.get());
    js_mark_non_configurable(constructor_root.get(), key_root.get());
    return prototype_root.get();
}

#define JS_DEFINE_NATIVE_REST_FACTORY(arity, type, member) \
    Item js_new_native_rest_function(type target) { \
        JsNativeTarget stored = {}; stored.member = target; \
        return js_new_native_function_impl(stored, js_native_call_##arity, NULL, \
            js_native_cache_key(target, arity, JS_NATIVE_CALL_REST), arity, JS_NATIVE_CALL_REST, \
            false, false); \
    } \
    Item js_new_native_rest_constructor(type target) { \
        JsNativeTarget stored = {}; stored.member = target; \
        return js_new_native_function_impl(stored, js_native_call_##arity, NULL, \
            js_native_cache_key(target, arity, JS_NATIVE_CALL_REST, true), arity, \
            JS_NATIVE_CALL_REST, true, false); \
    } \
    static Item js_new_distinct_native_rest_function(type target) { \
        JsNativeTarget stored = {}; stored.member = target; \
        return js_new_native_function_impl(stored, js_native_call_##arity, NULL, \
            js_native_cache_key(target, arity, JS_NATIVE_CALL_REST), arity, \
            JS_NATIVE_CALL_REST, false, true); \
    } \
    static Item js_new_distinct_native_rest_constructor(type target) { \
        JsNativeTarget stored = {}; stored.member = target; \
        return js_new_native_function_impl(stored, js_native_call_##arity, NULL, \
            js_native_cache_key(target, arity, JS_NATIVE_CALL_REST, true), arity, \
            JS_NATIVE_CALL_REST, true, true); \
    }

JS_DEFINE_NATIVE_REST_FACTORY(1, JsNativeP1, p1)
JS_DEFINE_NATIVE_REST_FACTORY(2, JsNativeP2, p2)
JS_DEFINE_NATIVE_REST_FACTORY(3, JsNativeP3, p3)
JS_DEFINE_NATIVE_REST_FACTORY(4, JsNativeP4, p4)
JS_DEFINE_NATIVE_REST_FACTORY(5, JsNativeP5, p5)
JS_DEFINE_NATIVE_REST_FACTORY(6, JsNativeP6, p6)
JS_DEFINE_NATIVE_REST_FACTORY(7, JsNativeP7, p7)
JS_DEFINE_NATIVE_REST_FACTORY(8, JsNativeP8, p8)

#undef JS_DEFINE_NATIVE_REST_FACTORY

static Item js_native_call_span(Item fn_item, Item this_value, Item* args,
        int argc, uint64_t* result_home) {
    (void)this_value; (void)result_home;
    JsFunction* fn = (JsFunction*)fn_item.function;
    return fn && fn->native_target.span
        ? fn->native_target.span(args, argc) : ItemError;
}

static Item js_native_call_this_span(Item fn_item, Item this_value, Item* args,
        int argc, uint64_t* result_home) {
    (void)result_home;
    JsFunction* fn = (JsFunction*)fn_item.function;
    return fn && fn->native_target.this_span
        ? fn->native_target.this_span(this_value, args, argc) : ItemError;
}

Item js_new_native_span_function(JsNativeSpan target) {
    JsNativeTarget stored = {};
    stored.span = target;
    return js_new_native_function_impl(stored, js_native_call_span, NULL,
        js_native_cache_key(target, 0, JS_NATIVE_CALL_SPAN), 0,
        JS_NATIVE_CALL_SPAN, false, false);
}

Item js_new_native_this_span_function(JsNativeThisSpan target) {
    JsNativeTarget stored = {};
    stored.this_span = target;
    return js_new_native_function_impl(stored, js_native_call_this_span, NULL,
        js_native_cache_key(target, 0, JS_NATIVE_CALL_THIS_SPAN), 0,
        JS_NATIVE_CALL_THIS_SPAN, false, false);
}

Item js_new_native_body_constructor(JsNativeCallBody call_body,
        JsNativeConstructBody construct_body, int formal_length) {
    JsNativeTarget stored = {};
    return js_new_native_function_impl(stored, call_body, construct_body,
        js_native_cache_key(call_body, formal_length, JS_NATIVE_CALL_BODY, true),
        formal_length, JS_NATIVE_CALL_BODY, true, true);
}

Item js_new_native_payload_function(JsNativeCallBody call_body,
        uint64_t payload, int formal_length) {
    JsNativeTarget stored = {};
    stored.bits = payload;
    // D5.3.3/D6.2.2v2: executable host payloads are not Item edges. Keeping
    // them in the native target union prevents precise closure tracing from
    // interpreting a process-stable record pointer as a JavaScript value.
    return js_new_native_function_impl(stored, call_body, NULL,
        js_native_cache_key(call_body, formal_length, JS_NATIVE_CALL_BODY),
        formal_length, JS_NATIVE_CALL_BODY, false, true);
}

#define JS_DEFINE_NATIVE_CLOSURE_ADAPTER(exposed_arity, member, call_args) \
    static Item js_native_closure_call_##exposed_arity(Item fn_item, \
            Item this_value, Item* args, int argc, uint64_t* result_home) { \
        (void)this_value; (void)result_home; \
        JsFunction* fn = (JsFunction*)fn_item.function; \
        if (!fn || argc != exposed_arity || \
                (exposed_arity > 0 && !args)) { \
            log_error("js-native-closure-%d: adapted arity mismatch argc=%d", \
                exposed_arity, argc); \
            return ItemError; \
        } \
        Item env_item = {.item = (uint64_t)(uintptr_t)fn->env}; \
        return fn->native_target.member call_args; \
    }

JS_DEFINE_NATIVE_CLOSURE_ADAPTER(0, p1, (env_item))
JS_DEFINE_NATIVE_CLOSURE_ADAPTER(1, p2, (env_item, args[0]))
JS_DEFINE_NATIVE_CLOSURE_ADAPTER(2, p3, (env_item, args[0], args[1]))
JS_DEFINE_NATIVE_CLOSURE_ADAPTER(3, p4, (env_item, args[0], args[1], args[2]))
JS_DEFINE_NATIVE_CLOSURE_ADAPTER(4, p5, (env_item, args[0], args[1], args[2], args[3]))

#undef JS_DEFINE_NATIVE_CLOSURE_ADAPTER

static Item js_new_native_closure_impl(JsNativeTarget target,
        JsNativeCallBody call_body,
        const JsRuntimeState::JsFunctionCacheKey& cache_key,
        int exposed_arity, JsNativeCallPolicy policy, Item* env,
        int env_size) {
    RootFrame roots(2);
    Rooted<Item> env_owner_root(roots,
        (Item){.item = (uint64_t)(uintptr_t)env});
    // D5.4.3: the caller-built GC environment has no object owner until the
    // fresh callable is attached; root the raw environment across allocation.
    Rooted<Item> fn_root(roots, js_new_native_function_impl(target, call_body,
        NULL,
        cache_key, exposed_arity, policy, false, true));
    if (get_type_id(fn_root.get()) != LMD_TYPE_FUNC) return fn_root.get();
    JsFunction* fn = (JsFunction*)fn_root.get().function;
    // D5.3.3/D6.2.2v2: the typed closure owns its environment before scalar
    // rehoming can allocate; invocation reads it without an ABI cast.
    fn->env = env;
    fn->env_size = env_size;
    js_env_rehome_scalars(fn->env);
    js_function_finalize_capabilities(fn);
    return fn_root.get();
}

#define JS_DEFINE_NATIVE_CLOSURE_FACTORY(exposed_arity, type, member) \
    Item js_new_native_closure(type target, int adapter_arity, Item* env, \
            int env_size) { \
        JsNativeCallPolicy policy = adapter_arity == exposed_arity \
            ? JS_NATIVE_CALL_FIXED : JS_NATIVE_CALL_REST; \
        if (adapter_arity != exposed_arity && \
                adapter_arity != -exposed_arity) { \
            log_error("js-native-closure-factory: target arity %d mismatches adapter %d", \
                exposed_arity, adapter_arity); \
            return ItemError; \
        } \
        JsNativeTarget stored = {}; stored.member = target; \
        return js_new_native_closure_impl(stored, \
            js_native_closure_call_##exposed_arity, \
            js_native_cache_key(target, exposed_arity, policy), exposed_arity, \
            policy, env, env_size); \
    }

JS_DEFINE_NATIVE_CLOSURE_FACTORY(0, JsNativeP1, p1)
JS_DEFINE_NATIVE_CLOSURE_FACTORY(1, JsNativeP2, p2)
JS_DEFINE_NATIVE_CLOSURE_FACTORY(2, JsNativeP3, p3)
JS_DEFINE_NATIVE_CLOSURE_FACTORY(3, JsNativeP4, p4)
JS_DEFINE_NATIVE_CLOSURE_FACTORY(4, JsNativeP5, p5)

#undef JS_DEFINE_NATIVE_CLOSURE_FACTORY
JS_FORWARD_ITEM(js_new_function_mir, (void* func_ptr, int param_count), js_new_function_impl, (func_ptr, param_count, true))

extern "C" Item js_new_distinct_function_mir(void* func_ptr, int param_count) {
    // D6.2.2v2: evaluating a function expression creates a fresh callable;
    // the MIR-pointer cache is only valid for a stable binding materialization.
    js_func_cache_suppress_push();
    Item result = js_new_function_impl(func_ptr, param_count, true);
    js_func_cache_suppress_pop();
    return result;
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
    js_function_finalize_capabilities(fn);
    return (Item){.function = (Function*)fn};
}
JS_FORWARD_ITEM(js_new_method_function_mir, (void* func_ptr, int param_count), js_new_method_function_impl, (func_ptr, param_count, true))

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
    js_function_finalize_capabilities(fn);
    return (Item){.function = (Function*)fn};
}
JS_FORWARD_ITEM(js_new_closure_mir, (void* func_ptr, int param_count,         Item* env, int env_size), js_new_closure_impl, (func_ptr, param_count, env, env_size, true))

// Set the ES spec formal .length for a function (params before first default, excl rest)
extern "C" void js_set_formal_length(Item fn_item, int length) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->formal_length = (int16_t)length;
    js_function_refresh_length_property(fn);
}

// Allocate a traced raw Item environment. Its owning closure/function keeps the
// allocation live; the GC header supplies the exact slot count to the tracer.
JS_FORWARD_EXPRESSION(Item*, js_alloc_env, (int count),
    count > 0 ? (Item*)heap_calloc_closure_env((size_t)count * sizeof(Item)) : NULL)

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
        bool is_side_number = js_env_slot_is_side_number(env[i]);
        if (is_side_number) {
            owned_item_slot_store(env, count, i, env[i]);
        }
    }
}

static void js_mark_function_flags(Item fn_item, uint32_t flags) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->flags |= flags;
    js_function_finalize_capabilities(fn);
}
JS_FORWARD_VOID( js_mark_derived_constructor_func, (Item fn_item), js_mark_function_flags, (fn_item, JS_FUNC_FLAG_DERIVED_CTOR))
JS_FORWARD_VOID( js_mark_method_func, (Item fn_item), js_mark_function_flags, (fn_item, JS_FUNC_FLAG_METHOD))

// Mark a function as strict mode (ES spec [[Strict]] internal slot)
JS_FORWARD_VOID( js_mark_strict_func, (Item fn_item), js_mark_function_flags, (fn_item, JS_FUNC_FLAG_STRICT))

extern "C" void js_finalize_function(Item fn_item, const char* name_chars,
        const char* source_chars, uint64_t span_lengths,
        int64_t formal_length, int64_t init_flags) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    RootFrame roots(1);
    Rooted<Item> function_root(roots, fn_item);
    uint32_t name_length = (uint32_t)(span_lengths & UINT32_MAX);
    uint32_t source_length = (uint32_t)(span_lengths >> 32);
    // D5.4.3: name/source materialization can collect. Root the newly created
    // callable before either allocation, then publish each edge immediately so
    // the function itself owns the strings for the rest of finalization.
    JsFunction* fn = (JsFunction*)function_root.get().function;
    if (name_chars) {
        Item name_item = js_make_string_len(name_chars, (int)name_length);
        fn = (JsFunction*)function_root.get().function;
        if (get_type_id(name_item) == LMD_TYPE_STRING) fn->name = it2s(name_item);
    }
    if (source_chars) {
        Item source_item = js_make_string_len(source_chars, (int)source_length);
        fn = (JsFunction*)function_root.get().function;
        if (get_type_id(source_item) == LMD_TYPE_STRING) fn->source_text = it2s(source_item);
    }
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
    if (init_flags & JS_FUNC_INIT_CLASS_FIELD_INITIALIZER) {
        // Synthetic field callables are finalized during class evaluation, not
        // while fields run; without this capability direct eval loses the class
        // initializer PrivateEnvironment at its later invocation.
        fn->eval_initializer_context = true;
    }
    if (js_private_field_initializing || js_eval_initializer_context) {
        fn->eval_initializer_context = true;
    }
    js_function_refresh_name_property(fn);
    js_function_refresh_length_property(fn);
    js_function_finalize_capabilities(fn);
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
    if (fn->layout_magic == JS_FUNCTION_LAYOUT_MAGIC) {
        fn->name = it2s(name_item);
        js_function_refresh_name_property(fn);
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
    if (fn->layout_magic == JS_FUNCTION_LAYOUT_MAGIC &&
            (!fn->name || fn->name->len == 0)) {
        fn->name = it2s(name_item);
        js_function_refresh_name_property(fn);
    }
}

static Item js_private_display_name_item(Item name_item) {
    // Private NameRecords retain their source spelling; rewriting it could
    // conflate a valid #123_name with a former compiler-private encoding.
    return name_item;
}

static int js_function_name_from_symbol_key(NameRef key, char* out, int out_size) {
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
    if (get_type_id(cls_item) == LMD_TYPE_FUNC) {
        JsFunction* fn = (JsFunction*)cls_item.function;
        if (!fn || !(fn->flags & JS_FUNC_FLAG_CLASS_CONSTRUCTOR) ||
                get_type_id(name_item) != LMD_TYPE_STRING) return;
        Item name_key = (Item){.item = s2it(heap_create_name("name", 4))};
        Item current = js_get_key_default(cls_item, name_key);
        // A static `name` method is an own callable property; inferred class
        // naming must not replace it with the constructor's display string.
        if (get_type_id(current) != LMD_TYPE_STRING) return;
        String* current_name = it2s(current);
        if (current_name && current_name->len != 0) return;
        // Class constructors are JsFunction values after Tune6; keeping this
        // setter Map-only silently erased every inferred class name.
        fn->name = it2s(name_item);
        js_function_refresh_name_property(fn);
        return;
    }
    if (get_type_id(cls_item) != LMD_TYPE_MAP) return;
    if (get_type_id(name_item) != LMD_TYPE_STRING) return;
    ShapeEntry* existing = js_find_shape_entry(cls_item, "name", 4);
    if (existing && !jspd_is_deleted(existing)) {
        Item key = (Item){.item = s2it(heap_create_name("name", 4))};
        Item current = js_get_key_default(cls_item, key);
        if (get_type_id(current) == LMD_TYPE_STRING) {
            String* current_name = it2s(current);
            if (current_name && current_name->len == 0) {
                String* name_key_str = heap_create_name("name", 4);
                map_put_heap(cls_item.map, name_key_str, name_item, js_input);
                js_attr_set_writable(cls_item, "name", 4, false);
                js_attr_set_enumerable(cls_item, "name", 4, false);
                js_attr_set_configurable(cls_item, "name", 4, true);
            }
        }
        return;
    }
    String* name_key_str = heap_create_name("name", 4);
    map_put_heap(cls_item.map, name_key_str, name_item, js_input);
    js_attr_set_writable(cls_item, "name", 4, false);
    js_attr_set_enumerable(cls_item, "name", 4, false);
    js_attr_set_configurable(cls_item, "name", 4, true);
}

extern "C" void js_set_default_constructor_property(Item proto_item, Item cls_item) {
    if (get_type_id(proto_item) != LMD_TYPE_MAP) return;
    ShapeEntry* existing = js_find_shape_entry(proto_item, "constructor", 11);
    if (existing && !jspd_is_deleted(existing)) return;
    Item key = (Item){.item = s2it(heap_create_name("constructor", 11))};
    js_set_key_default(proto_item, key, cls_item);
    js_attr_set_enumerable(proto_item, "constructor", 11, false);
}

extern "C" Item js_prepare_class_prototype_property(Item cls_item) {
    if (get_type_id(cls_item) != LMD_TYPE_MAP) return js_status_ok();
    ShapeEntry* existing = js_find_shape_entry(cls_item, "prototype", 9);
    if (existing && !jspd_is_deleted(existing)) {
        return js_throw_type_error("Cannot redefine property: prototype");
    }
    return js_status_ok();
}

extern "C" Item js_check_class_static_field_key(Item key_item) {
    if (get_type_id(key_item) != LMD_TYPE_STRING) return js_status_ok();
    String* key = it2s(key_item);
    if (key && key->len == 9 && strncmp(key->chars, "prototype", 9) == 0) {
        return js_throw_type_error("Cannot redefine property: prototype");
    }
    return js_status_ok();
}

// Set the source text of a JsFunction for Function.prototype.toString
extern "C" void js_set_function_source(Item fn_item, Item source_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    if (get_type_id(source_item) != LMD_TYPE_STRING) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    if (fn->layout_magic == JS_FUNCTION_LAYOUT_MAGIC) {
        fn->source_text = it2s(source_item);
    }
}
