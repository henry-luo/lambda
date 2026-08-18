#include "js_runtime_internal.hpp"
#include "js_well_known_names.h"
#include "js_exec_profile.h"
#include "../runtime/lambda-error.h"
#include "../runtime/runtime-state.h"
#include "../lambda.hpp"
#include "../jube/jube_registry.h"

__thread JsRuntimeState* js_active_runtime_state = NULL;
extern __thread EvalContext* context;
extern "C" int js_initial_call_stack_limit(void);
extern "C" void js_runtime_owned_cache_destroy_context(JsRuntimeState* state);
extern "C" void js_runtime_prototype_snapshot_destroy_context(JsRuntimeState* state);
extern "C" void js_runtime_regex_cache_destroy_context(JsRuntimeState* state);
extern "C" void js_dom_platform_destroy_context(JsRuntimeState* state);
extern "C" void js_dom_events_destroy_context(JsRuntimeState* state);
extern "C" void js_dom_observers_destroy_context(JsRuntimeState* state);
extern "C" void js_xhr_destroy_context(JsRuntimeState* state);
extern "C" void js_iterator_proto_cache_reset(void);
extern "C" void js_history_reset(void);
extern "C" void js_xhr_reset(void);
extern void jm_compile_recovery_state_destroy_context(JsRuntimeState* state);

extern "C" void js_reset_buffer_module(void);
extern "C" void js_crypto_reset(void);
extern "C" void js_dns_reset(void);
extern "C" void js_zlib_reset(void);
extern "C" void js_readline_reset(void);
extern "C" void js_stream_reset(void);
extern "C" void js_net_reset(void);
extern "C" void js_tls_reset(void);
extern "C" void js_http_reset(void);
extern "C" void js_https_reset(void);
extern "C" void js_assert_reset(void);
extern "C" void js_node_test_reset(void);

static void js_reset_cached_realm_objects(void) {
    // Cached realm objects all point into the batch heap and must be invalidated together.
    js_canvas_cleanup();
    js_reset_math_object();
    js_reset_json_object();
    js_reset_intl_object();
    js_reset_console_object();
    js_reset_reflect_object();
    js_reset_atomics_object();
    js_reset_262_object();
    js_reset_css_namespace_object();
    js_reset_proto_key();
    js_reset_template_registry();
    js_iterator_proto_cache_reset();
    js_symbol_registry_batch_reset();
    js_func_cache_reset();
    js_deep_batch_reset();
    js_reset_constructor_prototypes();
}

static void js_reset_core_module_caches(void) {
    js_child_process_reset();
    js_fs_reset();
    js_util_reset();
    js_reset_buffer_module();
    js_crypto_reset();
    js_dns_reset();
    js_zlib_reset();
    js_readline_reset();
    js_stream_reset();
    js_net_reset();
    js_tls_reset();
    js_http_reset();
    js_https_reset();
    js_fetch_reset();
    js_history_reset();
    js_assert_reset();
    js_node_test_reset();
}
extern "C" void js_history_destroy_context(JsRuntimeState* state);
extern "C" void js_window_dialog_reset(void);
extern "C" void js_dom_collections_release_context(void);
extern "C" void js_dom_collections_destroy_context(JsRuntimeState* state);
extern "C" void js_dom_foreign_documents_release_context(void);
extern "C" void js_dom_foreign_documents_destroy_context(JsRuntimeState* state);
extern "C" void js_fetch_apply_bootstrap_base_path(void);
extern "C" void js_fetch_destroy_context(JsRuntimeState* state);
extern "C" void js_fs_pending_destroy_context(JsRuntimeState* state);
extern "C" void js_tls_destroy_context(JsRuntimeState* state);
extern "C" void js_permission_destroy_context(JsRuntimeState* state);
extern "C" void js_net_destroy_context(JsRuntimeState* state);
extern "C" void js_crypto_destroy_context(JsRuntimeState* state);
extern "C" void js_atomics_destroy_context(JsRuntimeState* state);
extern "C" void js_canvas_destroy_context(JsRuntimeState* state);
extern "C" void js_dynfunc_cache_destroy_context(JsRuntimeState* state);

static bool js_runtime_state_init_well_known_refs(JsRuntimeState* state) {
    if (!state) return false;
    JsWellKnownRefs* refs = &state->well_known;
    refs->constructor = JS_NAME_CONSTRUCTOR;
    refs->prototype = JS_NAME_PROTOTYPE;
    refs->name = JS_NAME_NAME;
    refs->to_string = JS_NAME_TO_STRING;
    refs->value_of = JS_NAME_VALUE_OF;
    refs->symbol_iterator = JS_SYMBOL_ITERATOR;
    refs->symbol_to_primitive = JS_SYMBOL_TO_PRIMITIVE;
    refs->symbol_has_instance = JS_SYMBOL_HAS_INSTANCE;
    refs->symbol_to_string_tag = JS_SYMBOL_TO_STRING_TAG;
    refs->symbol_async_iterator = JS_SYMBOL_ASYNC_ITERATOR;
    refs->symbol_species = JS_SYMBOL_SPECIES;
    refs->symbol_match = JS_SYMBOL_MATCH;
    refs->symbol_replace = JS_SYMBOL_REPLACE;
    refs->symbol_search = JS_SYMBOL_SEARCH;
    refs->symbol_split = JS_SYMBOL_SPLIT;
    refs->symbol_unscopables = JS_SYMBOL_UNSCOPABLES;
    refs->symbol_is_concat_spreadable = JS_SYMBOL_IS_CONCAT_SPREADABLE;
    refs->symbol_match_all = JS_SYMBOL_MATCH_ALL;
    refs->symbol_async_dispose = JS_SYMBOL_ASYNC_DISPOSE;
    refs->symbol_dispose = JS_SYMBOL_DISPOSE;
    return refs->constructor != NAME_ID_NONE && refs->prototype != NAME_ID_NONE &&
        refs->name != NAME_ID_NONE && refs->to_string != NAME_ID_NONE &&
        refs->value_of && refs->symbol_iterator && refs->symbol_to_primitive &&
        refs->symbol_has_instance && refs->symbol_to_string_tag && refs->symbol_async_iterator &&
        refs->symbol_species && refs->symbol_match && refs->symbol_replace && refs->symbol_search &&
        refs->symbol_split && refs->symbol_unscopables && refs->symbol_is_concat_spreadable &&
        refs->symbol_match_all && refs->symbol_async_dispose && refs->symbol_dispose;
}

bool js_runtime_state_thread_initialize(EvalContext* runtime_context) {
    if (!eval_context_thread_matches(runtime_context)) {
        log_error("js-thread-init: EvalContext is not current owner");
        return false;
    }
    if (!runtime_context->js_state) {
        runtime_context->js_state = (JsRuntimeState*)mem_alloc(sizeof(JsRuntimeState),
            MEM_CAT_JS_RUNTIME);
        if (!runtime_context->js_state) {
            log_error("js-runtime-state: failed to allocate context state");
            return false;
        }
        memset(runtime_context->js_state, 0, sizeof(JsRuntimeState));
        runtime_context->js_state->heap_epoch = 1;
        runtime_context->js_state->batch_test_module_state_id = UINT32_MAX;
        runtime_context->js_state->batch_preamble_module_state_id = UINT32_MAX;
        runtime_context->js_state->batch_preamble_var_count = 0;
        // This capsule is raw-allocated for C-compatible runtime ownership,
        // so restore non-zero queue identities that C++ default initializers
        // would otherwise provide only for a constructed object.
        runtime_context->js_state->event_loop.next_raf_id = 1;
        runtime_async_deque_init(
            &runtime_context->js_state->event_loop.next_tick_deque,
            &runtime_context->js_state->event_loop.queue_storage[0], 4);
        runtime_async_deque_init(
            &runtime_context->js_state->event_loop.microtask_deque,
            &runtime_context->js_state->event_loop.queue_storage[1], 4);
        runtime_async_deque_init(&runtime_context->js_state->promises.unhandled_deque,
            &runtime_context->js_state->promises.unhandled_storage, 1);
        runtime_context->js_state->timers.next_id = 1;
        runtime_context->js_state->current_private_home_class_index = -1;
        runtime_context->js_state->call_stack_limit = js_initial_call_stack_limit();
        runtime_context->js_state->test262_agent.current_slot = -1;
        runtime_context->js_state->operations.next_symbol_id = 100;
        if (!js_runtime_state_init_well_known_refs(runtime_context->js_state)) {
            // A missing generated record would make pointer identity silently
            // fall back to bytes, so fail before any realm executes code.
            log_error("js-runtime-state: incomplete generated well-known key table");
            mem_free(runtime_context->js_state);
            runtime_context->js_state = NULL;
            return false;
        }
        runtime_context->js_state->global_string_caches.last_from_char_code_cp = -1;
        runtime_context->js_state->global_string_caches.ascii_chars_epoch = ~0ULL;
        runtime_context->js_state->stream.default_byte_hwm = 16 * 1024;
        runtime_context->js_state->stream.default_object_hwm = 16;
        runtime_context->js_state->clipboard.generation = 1;
        runtime_context->js_state->intrinsics.mutation_serial = 1;
        runtime_context->js_state->async_roots_registered_epoch = UINT64_MAX;
        runtime_context->js_state->async_hooks.next_id = 2;
        runtime_context->js_state->promises.unhandled_strict =
            js_promise_initial_unhandled_rejections_strict();
        runtime_context->js_state->cluster.next_worker_id = 1;
        runtime_context->js_state->assert.node_test_next_id = 1;
        runtime_context->js_state->performance.origin_epoch = UINT64_MAX;
    }
    if (js_active_runtime_state &&
            js_active_runtime_state != runtime_context->js_state) {
        // A derived JS TLS cache may never conceal an EvalContext switch.
        log_error("js-thread-init: refusing runtime-state switch current=%p owner=%p",
                  (void*)js_active_runtime_state, (void*)runtime_context->js_state);
        return false;
    }
    js_active_runtime_state = runtime_context->js_state;
    js_fetch_apply_bootstrap_base_path();
    return true;
}

bool js_runtime_state_thread_matches(const EvalContext* runtime_context) {
    return runtime_context && eval_context_thread_matches(runtime_context) &&
        runtime_context->js_state &&
        js_active_runtime_state == runtime_context->js_state;
}

bool js_runtime_state_thread_shutdown(EvalContext* runtime_context) {
    if (!runtime_context || !eval_context_thread_matches(runtime_context) ||
            (js_active_runtime_state &&
             js_active_runtime_state != runtime_context->js_state)) {
        log_error("js-thread-shutdown: owner mismatch");
        return false;
    }
    js_active_runtime_state = NULL;
    return true;
}

void js_runtime_state_release_heap_resources(void) {
    EvalContext* runtime_context = context;
    if (!runtime_context || !runtime_context->js_state) return;
    if (!js_runtime_state_thread_matches(runtime_context)) {
        // Heap-bound JS cleanup invokes ambient helpers and therefore must run
        // on the already-bound owner thread instead of rebinding its capsule.
        log_error("js-runtime-state-release: owner thread is not current");
        return;
    }
    // Regex cache records point into the current GC heap. Release their native
    // engines before heap_destroy invalidates those records; capsule teardown
    // later only disposes context-owned containers that are still present.
    // Listener root slots and DOM pins must leave while both their heap and
    // document owner are still valid. Dispatch remains direct state access.
    js_dom_events_reset();
    js_xhr_reset();
    js_history_reset();
    js_window_dialog_reset();
    js_dom_collections_release_context();
    js_dom_foreign_documents_release_context();
    js_fetch_reset();
    js_reset_template_registry();
    js_runtime_regex_cache_destroy_context(runtime_context->js_state);
}

void js_runtime_state_destroy_context(void) {
    EvalContext* runtime_context = context;
    if (!runtime_context || !runtime_context->js_state) return;
    bool was_active = js_active_runtime_state == runtime_context->js_state;
    js_runtime_owned_cache_destroy_context(runtime_context->js_state);
    js_dom_platform_destroy_context(runtime_context->js_state);
    js_dom_events_destroy_context(runtime_context->js_state);
    js_dom_observers_destroy_context(runtime_context->js_state);
    js_xhr_destroy_context(runtime_context->js_state);
    js_history_destroy_context(runtime_context->js_state);
    js_dom_collections_destroy_context(runtime_context->js_state);
    js_dom_foreign_documents_destroy_context(runtime_context->js_state);
    js_fetch_destroy_context(runtime_context->js_state);
    js_fs_pending_destroy_context(runtime_context->js_state);
    js_tls_destroy_context(runtime_context->js_state);
    js_permission_destroy_context(runtime_context->js_state);
    js_net_destroy_context(runtime_context->js_state);
    js_crypto_destroy_context(runtime_context->js_state);
    js_atomics_destroy_context(runtime_context->js_state);
    js_canvas_destroy_context(runtime_context->js_state);
    js_dynfunc_cache_destroy_context(runtime_context->js_state);
    jm_compile_recovery_state_destroy_context(runtime_context->js_state);
    js_runtime_prototype_snapshot_destroy_context(runtime_context->js_state);
    js_runtime_regex_cache_destroy_context(runtime_context->js_state);
    if (runtime_context->js_state->operations.symbol_registry) {
        hashmap_free(runtime_context->js_state->operations.symbol_registry);
    }
    if (runtime_context->js_state->operations.symbol_description_registry) {
        hashmap_free(runtime_context->js_state->operations.symbol_description_registry);
    }
    // Promise carriers are GC-owned; context teardown only drops queue and
    // async owners before the heap itself is released.
    mem_free(runtime_context->js_state);
    runtime_context->js_state = NULL;
    if (was_active) js_active_runtime_state = NULL;
}

static void js_root_range_set_storage(JsRootRange* range, Item* slots, int slot_count,
                                      const char* name) {
    range->slots = slots;
    range->slot_count = slot_count;
    range->name = name;
}

// The state capsule has self-referential range descriptors. Initialize those
// pointers after the final global object exists; copying a default-initialized
// subobject would otherwise leave a descriptor pointing at a temporary.
static void js_runtime_state_prepare_root_ranges(JsRuntimeState* state) {
    if (!state) return;
    JsEvalState* eval = &state->eval;
    JsEvalSourceState* source = &eval->source;
    JsEvalBridgeState* bridge = &eval->bridge;
    JsEvalLocalState* local = &eval->local;
    // Keep every precise root descriptor in one catalog so a new state cache
    // cannot bypass the same initialization and GC registration invariant.
#define JS_SET_RUNTIME_ROOT(range, slots, count, label) \
    js_root_range_set_storage(range, slots, count, label);
#define JS_RUNTIME_ROOT_STORAGE(M) \
    M(&source->filename_roots, source->filename_slots, JS_EVAL_SOURCE_STACK_MAX, "eval source filenames") \
    M(&source->code_roots, source->code_slots, JS_EVAL_SOURCE_STACK_MAX, "eval source code") \
    M(&bridge->env_key_roots, bridge->env_keys, JS_EVAL_ENV_BIND_MAX, "eval env keys") \
    M(&bridge->env_old_value_roots, bridge->env_old_values, JS_EVAL_ENV_BIND_MAX, "eval env old values") \
    M(&bridge->global_lexical_key_roots, bridge->global_lexical_keys, JS_EVAL_ENV_BIND_MAX, "eval global lexical keys") \
    M(&bridge->global_lexical_old_value_roots, bridge->global_lexical_old_values, JS_EVAL_ENV_BIND_MAX, "eval global lexical old values") \
    M(&bridge->private_unscoped_key_roots, bridge->private_unscoped_keys, JS_EVAL_PRIVATE_BIND_MAX, "eval private unscoped keys") \
    M(&bridge->private_scoped_key_roots, bridge->private_scoped_keys, JS_EVAL_PRIVATE_BIND_MAX, "eval private scoped keys") \
    M(&local->key_roots, local->keys, JS_EVAL_LOCAL_BIND_MAX, "eval local keys") \
    M(&local->value_roots, local->values, JS_EVAL_LOCAL_BIND_MAX, "eval local values") \
    M(&local->lexical_key_roots, local->lexical_keys, JS_EVAL_LEXICAL_BIND_MAX, "eval lexical keys") \
    M(&local->immutable_key_roots, local->immutable_keys, JS_EVAL_IMMUTABLE_BIND_MAX, "eval immutable keys") \
    M(&state->super_this_values.roots, state->super_this_value_slots, 128, "super-this values") \
    M(&state->cjs.module_stack.roots, state->cjs.module_stack_slots, JS_CJS_STACK_MAX, "CommonJS module stack") \
    M(&state->with_scope.stack.roots, state->with_scope.stack_slots, JS_WITH_STACK_MAX, "with-scope stack") \
    M(&state->with_scope.last_binding_roots, state->with_scope.last_binding_slots, 2, "with binding cache") \
    M(&state->builtin_cache.roots, state->builtin_cache.entries, JS_INTRINSIC_BINDING_COUNT, "intrinsic binding function cache") \
    M(&state->readline.roots, &state->readline.namespace_object, 3 + 2 * JS_READLINE_INPUT_MAP_MAX, "readline namespaces and input map") \
    M(&state->buffer.roots, &state->buffer.namespace_object, 2, "Buffer namespace and prototype") \
    M(&state->https.roots, &state->https.agent_prototype, 2, "HTTPS namespace and Agent prototype") \
    M(&state->util.roots, &state->util.namespace_object, 1, "util namespace") \
    M(&state->crypto.roots, &state->crypto.namespace_object, 1, "crypto namespace") \
    M(&state->child_process.roots, &state->child_process.namespace_object, 1, "child_process namespace") \
    M(&state->zlib.roots, state->zlib.constructor_prototypes, 9, "zlib constructors and namespace") \
    M(&state->tls.roots, &state->tls.namespace_object, 5, "TLS namespace and certificate caches") \
    M(&state->stream.roots, &state->stream.key_on, 44, "stream keys, prototypes, and namespaces") \
    M(&state->http.roots, &state->http.server_prototype, 5, "HTTP namespace and prototypes") \
    M(&state->net.roots, &state->net.socket_prototype, 5, "net namespace and prototypes") \
    M(&state->fs.roots, &state->fs.internal_binding_namespace, 7, "fs namespaces and prototypes") \
    M(&state->clipboard.roots, &state->clipboard.blob_prototype, 7, "clipboard prototypes and drag session") \
    M(&state->dom.roots, &state->dom.implementation, 5, "DOM singleton wrappers") \
    M(&state->string_concat.roots, &state->string_concat.last_four_byte_escape, 273, "string concatenation fast caches") \
    M(&state->global_var_module_bindings.roots, &state->global_var_module_bindings.global, 1 + JS_GLOBAL_VAR_MODULE_BINDING_CAP, "global var module bindings") \
    M(&state->runtime_core_cache.roots, &state->runtime_core_cache.proto_key, 1, "runtime prototype key cache") \
    M(&state->function_prototypes.roots, &state->function_prototypes.generator_function, 3, "generator function prototypes") \
    M(&state->global_string_caches.roots, &state->global_string_caches.uri_last_four_byte_string, 132, "global URI and ASCII string caches") \
    M(&state->global_bindings.roots, &state->global_bindings.global_this, 1 + 64 + 1 + 1 + 1 + 2 * JS_GLOBAL_LEX_BIND_MAX, "global object and lexical bindings") \
    M(&state->constructors.roots, state->constructors.global_builtin_functions, JS_BUILTIN_GLOBAL_MAX + JS_CTOR_MAX + 2 + JS_TYPED_ARRAY_CACHE_TYPE_COUNT, "global builtin and constructor caches") \
    M(&state->namespaces.roots, &state->namespaces.math, 8, "core JS namespace objects") \
    M(&state->test262_agent.roots, &state->test262_agent.object, 1 + JS_TEST262_AGENT_MAX + JS_TEST262_AGENT_REPORT_MAX, "Test262 agent state") \
    M(&state->process.roots, &state->process.argv, 3 + 2 * JS_PROCESS_LISTENER_MAX + 2, "process realm state") \
    M(&state->iterators.roots, &state->iterators.generator_return_marker, 13, "generator and iterator prototype caches") \
    M(&state->diagnostics_channels.roots, state->diagnostics_channels.channel_names, 2 * JS_DIAGNOSTICS_CHANNEL_MAX + 6 + JS_DIAGNOSTICS_DEFERRED_ERROR_MAX, "diagnostics channel state") \
    M(&state->async_hooks.roots, &state->async_hooks.root_resource, 2 + JS_ASYNC_HOOK_STATE_MAX + JS_ASYNC_PENDING_DESTROY_STATE_MAX, "async hooks state") \
    M(&state->promises.roots, &state->promises.unhandled_storage, 3, "Promise unhandled queue and domain state") \
    M(&state->promises.domain_stack.roots, state->promises.domain_stack_slots, JS_DOMAIN_STACK_MAX, "domain stack") \
    M(&state->async_local_storage.roots, state->async_local_storage.instances, JS_MAX_ALS_INSTANCES, "AsyncLocalStorage instances") \
    M(&state->event_loop_queue_roots, state->event_loop.queue_storage, 2, "JS async queue storage") \
    M(&state->event_loop_raf_roots, state->event_loop.raf_callback, JS_EVENT_RAF_CAPACITY, "JS animation frame callbacks")
    JS_RUNTIME_ROOT_STORAGE(JS_SET_RUNTIME_ROOT)
#undef JS_RUNTIME_ROOT_STORAGE
#undef JS_SET_RUNTIME_ROOT
}

bool js_root_range_register_reset(JsRootRange* range, void* owner,
                                  JsRootRangeResetFn reset) {
    if (!range || !range->slots || range->slot_count <= 0) return false;
    if (range->reset_registered) {
        // A direct ensure may register the range before its stack client first
        // publishes a value. Preserve that descriptor but install the later
        // semantic reset callback instead of silently losing its depth reset.
        if (reset && !range->reset) {
            range->reset_owner = owner;
            range->reset = reset;
        }
        return true;
    }
    if (js_runtime_state.root_range_registry_count >= JS_ROOT_RANGE_REGISTRY_MAX) {
        log_error("js-root-range: reset registry overflow for %s",
            range->name ? range->name : "unnamed range");
        return false;
    }
    range->reset_owner = owner;
    range->reset = reset;
    js_runtime_state.root_range_registry[js_runtime_state.root_range_registry_count++] = range;
    range->reset_registered = true;
    return true;
}

bool js_root_range_ensure_registered(JsRootRange* range) {
    js_runtime_state_prepare_root_ranges(js_active_runtime_state);
    if (!range || !range->slots || range->slot_count <= 0) return false;
    if (!js_root_range_register_reset(range, NULL, NULL)) return false;
    if (!context || !context->heap || !context->heap->gc) return false;
    if (range->roots_epoch == js_heap_epoch) return true;
    heap_register_gc_root_range((uint64_t*)range->slots, range->slot_count);
    range->roots_epoch = js_heap_epoch;
    return true;
}

void js_root_range_clear(JsRootRange* range) {
    if (!range || !range->slots || range->slot_count <= 0) return;
    memset(range->slots, 0, (size_t)range->slot_count * sizeof(Item));
}

void js_root_range_reset_all(void) {
    for (int i = 0; i < js_runtime_state.root_range_registry_count; i++) {
        JsRootRange* range = js_runtime_state.root_range_registry[i];
        if (!range) continue;
        // Catalog-backed intrinsic callables are realm identity anchors.  Their
        // cache is reset explicitly for a full teardown and must survive a
        // partial preamble reset; clearing the registered root range here made
        // the next strict arguments object allocate a second %ThrowTypeError%
        // despite the restored Function.prototype snapshot (D6.2.2v2).
        if (range == &js_runtime_state.builtin_cache.roots) continue;
        js_root_range_clear(range);
        if (range->reset) range->reset(range->reset_owner);
    }
}
JS_FORWARD_STATIC_VOID( js_item_stack_reset_callback, (void* owner), js_item_stack_clear, ((JsItemStack*)owner))

bool js_item_stack_push(JsItemStack* stack, Item value) {
    if (!stack || stack->depth < 0) return false;
    if (!js_root_range_ensure_registered(&stack->roots)) return false;
    // ensure prepares self-referential runtime-state descriptors before the
    // slot-count check, so the super-this stack has no hidden init ordering.
    if (!stack->roots.slots || stack->depth >= stack->roots.slot_count) return false;
    if (!js_root_range_register_reset(&stack->roots, stack,
                                      js_item_stack_reset_callback)) return false;
    stack->roots.slots[stack->depth++] = value;
    return true;
}

void js_item_stack_pop(JsItemStack* stack) {
    if (!stack || !stack->roots.slots || stack->depth <= 0) return;
    // The registered fixed range is scanned in full, so vacated slots must not
    // retain old heap Items across a later collection or heap replacement.
    stack->roots.slots[--stack->depth] = ItemNull;
}

void js_item_stack_clear(JsItemStack* stack) {
    if (!stack || !stack->roots.slots) return;
    for (int i = 0; i < stack->depth; i++) stack->roots.slots[i] = ItemNull;
    stack->depth = 0;
}

void js_item_stack_shrink(JsItemStack* stack, int depth) {
    if (!stack || !stack->roots.slots) return;
    if (depth < 0) depth = 0;
    if (depth >= stack->depth) return;
    for (int i = depth; i < stack->depth; i++) stack->roots.slots[i] = ItemNull;
    stack->depth = depth;
}

#define js_eval_source_filename_stack (js_runtime_state.eval.source.filename_slots)
#define js_eval_source_code_stack (js_runtime_state.eval.source.code_slots)
#define js_eval_source_line_offset_stack (js_runtime_state.eval.source.line_offset_slots)
#define js_eval_source_column_offset_stack (js_runtime_state.eval.source.column_offset_slots)
#define js_eval_source_compact_stack (js_runtime_state.eval.source.compact_slots)
#define js_eval_source_stack_depth (js_runtime_state.eval.source.depth)

void js_eval_state_reset(JsEvalState* state) {
    if (!state) return;
    js_runtime_state_prepare_root_ranges(js_active_runtime_state);
    js_root_range_clear(&state->source.filename_roots);
    js_root_range_clear(&state->source.code_roots);
    memset(state->source.line_offset_slots, 0, sizeof(state->source.line_offset_slots));
    memset(state->source.column_offset_slots, 0, sizeof(state->source.column_offset_slots));
    memset(state->source.compact_slots, 0, sizeof(state->source.compact_slots));
    state->source.depth = 0;

    JsEvalBridgeState* bridge = &state->bridge;
    js_root_range_clear(&bridge->env_key_roots);
    js_root_range_clear(&bridge->env_old_value_roots);
    js_root_range_clear(&bridge->global_lexical_key_roots);
    js_root_range_clear(&bridge->global_lexical_old_value_roots);
    js_root_range_clear(&bridge->private_unscoped_key_roots);
    js_root_range_clear(&bridge->private_scoped_key_roots);
    memset(bridge->env_had_own, 0, sizeof(bridge->env_had_own));
    memset(bridge->env_from_journal, 0, sizeof(bridge->env_from_journal));
    memset(bridge->env_frame_marks, 0, sizeof(bridge->env_frame_marks));
    memset(bridge->global_lexical_had_own, 0, sizeof(bridge->global_lexical_had_own));
    memset(bridge->global_lexical_frame_marks, 0, sizeof(bridge->global_lexical_frame_marks));
    memset(bridge->private_frame_marks, 0, sizeof(bridge->private_frame_marks));
    bridge->env_count = 0;
    bridge->env_frame_depth = 0;
    bridge->global_lexical_count = 0;
    bridge->global_lexical_frame_depth = 0;
    bridge->private_count = 0;
    bridge->private_frame_depth = 0;

    JsEvalLocalState* local = &state->local;
    js_root_range_clear(&local->key_roots);
    js_root_range_clear(&local->value_roots);
    js_root_range_clear(&local->lexical_key_roots);
    js_root_range_clear(&local->immutable_key_roots);
    memset(local->frame_marks, 0, sizeof(local->frame_marks));
    local->count = 0;
    local->frame_depth = 0;
    local->lexical_count = 0;
    local->immutable_count = 0;
}

void js_eval_state_assert_clear(const JsEvalState* state, const char* reset_name) {
    if (!state) return;
    const char* name = reset_name ? reset_name : "reset";
    if (state->source.depth != 0) {
        log_error("js-eval-state: %s left source depth=%d", name, state->source.depth);
    }
    if (state->bridge.env_frame_depth != 0 || state->bridge.global_lexical_frame_depth != 0 ||
        state->bridge.private_frame_depth != 0) {
        log_error("js-eval-state: %s left bridge depths env=%d lexical=%d private=%d", name,
            state->bridge.env_frame_depth, state->bridge.global_lexical_frame_depth,
            state->bridge.private_frame_depth);
    }
    if (state->local.frame_depth != 0) {
        log_error("js-eval-state: %s left local frame depth=%d", name, state->local.frame_depth);
    }
}

static bool js_eval_source_register_roots(void) {
    JsEvalSourceState* source = &js_runtime_state.eval.source;
    return js_root_range_ensure_registered(&source->filename_roots) &&
        js_root_range_ensure_registered(&source->code_roots);
}

static bool js_eval_source_push_mode(Item filename, Item source,
                                     int64_t line_offset, int64_t column_offset,
                                     bool compact_stack) {
    if (!js_eval_source_register_roots()) return false;
    if (js_eval_source_stack_depth >= JS_EVAL_SOURCE_STACK_MAX) return false;
    int idx = js_eval_source_stack_depth++;
    js_eval_source_filename_stack[idx] = filename;
    js_eval_source_code_stack[idx] = source;
    js_eval_source_line_offset_stack[idx] = line_offset;
    js_eval_source_column_offset_stack[idx] = column_offset;
    js_eval_source_compact_stack[idx] = compact_stack;
    return true;
}
JS_FORWARD_EXPRESSION(int64_t, js_eval_source_push, (Item filename, Item source,                                          int64_t line_offset, int64_t column_offset), (js_eval_source_push_mode(filename, source, line_offset, column_offset, false) ? 1 : 0))
JS_FORWARD_EXPRESSION(int64_t, js_eval_source_push_compact, (Item filename, Item source,                                                  int64_t line_offset, int64_t column_offset), (js_eval_source_push_mode(filename, source, line_offset, column_offset, true) ? 1 : 0))

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

struct JsErrorTextParts {
    const char* name;
    int name_len;
    const char* message;
    int message_len;
};

static JsErrorTextParts js_error_text_parts(Item error_name, Item message) {
    JsErrorTextParts parts = {"Error", 5, "", 0};
    if (get_type_id(error_name) == LMD_TYPE_STRING) {
        String* ns = it2s(error_name);
        if (ns) {
            parts.name = ns->chars;
            parts.name_len = (int)ns->len;
        }
    }
    if (get_type_id(message) == LMD_TYPE_STRING) {
        String* ms = it2s(message);
        if (ms) {
            parts.message = ms->chars;
            parts.message_len = (int)ms->len;
        }
    }
    return parts;
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

    JsErrorTextParts text = js_error_text_parts(error_name, message);
    const char* name_str = text.name;
    int name_len = text.name_len;
    const char* msg_str = text.message;
    int msg_len = text.message_len;

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
        Item result = js_name_item(buf, pos);
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
    Item result = js_name_item(buf, pos);
    mem_free(buf);
    return result;
}

extern "C" Item* js_ensure_active_module_vars(void) {
    if (context && context->active_js_module_state) {
        return context->active_js_module_state->vars;
    }
    uint32_t module_id = 0;
    if (!context || !lambda_module_state_reserve(
            JS_MAX_MODULE_VARS, &module_id)) {
        log_error("js-module-vars: failed to reserve fallback module state");
        return NULL;
    }
    LambdaModuleState* state = context->module_states[module_id];
    if (!state || !state->vars) {
        log_error("js-module-vars: reserved module state has no variable slab");
        return NULL;
    }
    context->active_js_module_state = state;
    return state->vars;
}

extern "C" Item** js_active_module_vars_slot(void) {
    if (!context || !context->active_js_module_state) js_ensure_active_module_vars();
    if (!context || !context->active_js_module_state) return NULL;
    return &context->active_js_module_state->vars;
}

// Forward declaration for regex compilation cache reset (defined near JsRegexData)
void js_regex_cache_reset();

extern "C" void js_set_strict_mode(int64_t strict) {
    js_strict_mode = (strict != 0);
}

// Forward declaration for _map_read_field (defined in lambda-data-runtime.cpp)
Item _map_read_field(ShapeEntry* field, void* map_data);
// Forward declaration for _map_get (used as fallback for nested/spread maps)
Item _map_get(TypeMap* map_type, void* map_data, const char *key, bool *is_found);
extern "C" Item js_get_current_this(void) { return js_current_this; }

static void js_runtime_make_non_enumerable(Item object, Item name) {
    JS_ROOTS(roots,
        object_root, object,
        name_root, name,
        desc_root, ItemNull,
        enum_key_root, ItemNull);
    // The descriptor is fresh and property construction allocates; keep every
    // borrowed argument exact until defineProperty has consumed the object.
    desc_root.set(js_new_object());
    js_set_prototype(desc_root.get(), ItemNull);
    enum_key_root.set(js_name_item("enumerable", 10));
    js_set_key_default(desc_root.get(), enum_key_root.get(), (Item){.item = b2it(false)});
    js_object_define_property(object_root.get(), name_root.get(), desc_root.get());
}

// Forward declaration: defined in js_globals.cpp.
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
JS_FORWARD_EXPRESSION(int64_t, js_key_is_symbol_c, (Item key), (js_key_is_symbol(key) ? 1 : 0))

extern "C" Item js_well_known_symbol_key(int64_t symbol_id) {
    JsRuntimeState* state = js_active_runtime_state;
    if (!state) return ItemNull;
    JsWellKnownRefs* refs = &state->well_known;
    NameId key_id = NAME_ID_NONE;
    switch (symbol_id) {
    case 1: key_id = refs->symbol_iterator; break;
    case 2: key_id = refs->symbol_to_primitive; break;
    case 3: key_id = refs->symbol_has_instance; break;
    case 4: key_id = refs->symbol_to_string_tag; break;
    case 5: key_id = refs->symbol_async_iterator; break;
    case 6: key_id = refs->symbol_species; break;
    case 7: key_id = refs->symbol_match; break;
    case 8: key_id = refs->symbol_replace; break;
    case 9: key_id = refs->symbol_search; break;
    case 10: key_id = refs->symbol_split; break;
    case 11: key_id = refs->symbol_unscopables; break;
    case 12: key_id = refs->symbol_is_concat_spreadable; break;
    case 13: key_id = refs->symbol_match_all; break;
    case 14: key_id = refs->symbol_async_dispose; break;
    case 15: key_id = refs->symbol_dispose; break;
    default: return ItemNull;
    }
    // Initialization rejects an incomplete table, so a NULL here means an
    // invalid internal ID rather than a spelling-compatible fallback.
    NameRef key = name_pool_resolve_id(context ? context->name_pool : NULL, key_id);
    return key ? (Item){.item = s2it(key)} : ItemNull;
}

// ES2020 §7.1.14 ToPropertyKey(argument)
// ToPrimitive(string hint), then Symbols → their unique NameRecord key,
// strings → as-is, others → ToString.
static Item js_canonical_property_string(Item value) {
    if (get_type_id(value) != LMD_TYPE_STRING) return value;
    String* string_value = it2s(value);
    if (!string_value || property_key_requires_identity(string_value)) return value;
    // Input-owned strings may be id-less. Re-interning at the property-key
    // boundary gives them the context dynamic NameId, while a schema/static
    // spelling resolves through the NamePool parent before allocation.
    NameRef canonical = context && context->name_pool
        ? name_pool_create_len(context->name_pool, string_value->chars,
            string_value->len)
        : NULL;
    return canonical ? (Item){.item = s2it(canonical)} : ItemError;
}

extern "C" Item js_to_property_key(Item key) {
    if (js_key_is_symbol(key)) {
        return js_symbol_to_key(key);
    }
    TypeId kt = get_type_id(key);
    if (kt == LMD_TYPE_STRING) {
        Item result = js_canonical_property_string(key);
        return result;
    }
    if (key.item == 0 || kt == LMD_TYPE_NULL)
        return js_canonical_property_string(js_name_item("null", 4));
    if (kt == LMD_TYPE_UNDEFINED)
        return js_canonical_property_string(js_name_item("undefined", 9));
    if (kt == LMD_TYPE_MAP || kt == LMD_TYPE_ARRAY || kt == LMD_TYPE_ELEMENT || kt == LMD_TYPE_FUNC) {
        JS_ASSIGN_OR_RETURN_INTO(key, js_to_primitive(key, JS_HINT_STRING));
        if (js_key_is_symbol(key)) return js_symbol_to_key(key);
        kt = get_type_id(key);
        if (kt == LMD_TYPE_STRING) {
            return js_canonical_property_string(key);
        }
        if (key.item == 0 || kt == LMD_TYPE_NULL)
            return js_canonical_property_string(js_name_item("null", 4));
        if (kt == LMD_TYPE_UNDEFINED)
            return js_canonical_property_string(js_name_item("undefined", 9));
    }
    Item string_value = js_to_string(key);
    return item_is_error(string_value) ? string_value
        : js_canonical_property_string(string_value);
}

// Phase-5C: js_make_getter_key / js_make_setter_key removed.
// Transpiler now emits js_install_user_accessor directly which routes to
// js_define_accessor_partial without ever materializing a __get_/__set_ marker key.

extern "C" void js_set_module_var(int index, Item value) {
    if (index >= 0 && context && context->active_js_module_state &&
            index < (int)context->active_js_module_state->var_count) {
        // D5.3: module variables outlive the current MIR frame, so a scalar
        // home returned by a call must be copied into the module state's
        // persistent payload instead of retaining the frame-owned pointer.
        lambda_module_var_store(context->active_js_module_state,
            (uint32_t)index, value);
    }
}

extern "C" Item js_get_module_var(int index) {
    if (index >= 0 && context && context->active_js_module_state &&
            index < (int)context->active_js_module_state->var_count) {
        return js_active_module_vars[index];
    }
    return ItemNull;
}

extern "C" void js_reset_module_vars() {
    Item* vars = js_ensure_active_module_vars();
    if (!vars) return;
    memset(vars, 0, context->active_js_module_state->var_count * sizeof(Item));
    js_module_var_count = 0;
}

// Allocate a sealed, context-owned slab before entering a JS compilation unit.
extern "C" uint32_t js_alloc_module_state(uint32_t var_count) {
    uint32_t module_id = 0;
    if (var_count == 0) var_count = 1;
    if (!context || !lambda_module_state_reserve(
            var_count, &module_id)) return UINT32_MAX;
    return module_id;
}

extern "C" bool js_activate_module_state(uint32_t var_count) {
    uint32_t module_id = js_alloc_module_state(var_count);
    return module_id != UINT32_MAX && js_set_active_module_state_id(module_id);
}

extern "C" bool js_ensure_active_module_var_capacity(uint32_t required_var_count) {
    if (!context || !context->active_js_module_state) {
        log_error("js-module-vars: no active slab while growing to %u",
                  required_var_count);
        return false;
    }
    if (!lambda_active_js_module_state_ensure_vars(required_var_count)) {
        log_error("js-module-vars: failed to grow slab %u from %u to %u",
                  context->active_js_module_state->module_id,
                  context->active_js_module_state->var_count,
                  required_var_count);
        return false;
    }
    return true;
}

JS_FORWARD_EXPRESSION(uint32_t, js_get_active_module_state_id, (void),
    context && context->active_js_module_state
        ? context->active_js_module_state->module_id : UINT32_MAX)

static LambdaModuleState* js_module_state_at(uint32_t module_state_id) {
    if (!context || module_state_id == UINT32_MAX ||
            module_state_id >= context->module_state_capacity) return NULL;
    return context->module_states[module_state_id];
}

extern "C" bool js_set_active_module_state_id(uint32_t module_state_id) {
    LambdaModuleState* state = js_module_state_at(module_state_id);
    if (!state || !state->vars) return false;
    context->active_js_module_state = state;
    return true;
}
JS_FORWARD_EXPRESSION(bool, js_module_state_is_available, (uint32_t module_state_id), (context && module_state_id != UINT32_MAX && module_state_id < context->module_state_capacity && context->module_states[module_state_id] && context->module_states[module_state_id]->vars))

JS_FORWARD_EXPRESSION(uint64_t, js_active_module_name_id, (uint32_t index),
    !context || !context->active_js_module_state ||
            index >= context->active_js_module_state->property_key_count ||
            !context->active_js_module_state->property_keys ? NAME_ID_NONE
        : context->active_js_module_state->property_keys[index])

extern "C" Item js_active_module_name_item(uint32_t module_name_index,
        NameId direct_name_id) {
    // A generated NameId is already stable; other names must be resolved from
    // the active module image so compiled MIR never retains a compiler-pool pointer.
    NameId name_id = direct_name_id != NAME_ID_NONE ? direct_name_id
        : (NameId)js_active_module_name_id(module_name_index);
    return lambda_name_id_to_item(name_id);
}

JS_FORWARD_EXPRESSION(uint32_t, js_active_module_name_count, (void),
    context && context->active_js_module_state
        ? context->active_js_module_state->property_key_count : 0)
JS_FORWARD_EXPRESSION(uint32_t, js_get_batch_preamble_var_count, (void),
    js_runtime_state.batch_preamble_var_count)

extern "C" bool js_copy_module_state_var_prefix(uint32_t source_module_state_id,
        uint32_t destination_module_state_id, uint32_t count) {
    LambdaModuleState* source = js_module_state_at(source_module_state_id);
    LambdaModuleState* destination = js_module_state_at(destination_module_state_id);
    if (!source || !destination || !source->vars || !destination->vars ||
            count > source->var_count || count > destination->var_count) {
        return false;
    }
    if (count > 0) memcpy(destination->vars, source->vars, (size_t)count * sizeof(Item));
    return true;
}

// =============================================================================
// Error-lane construction (D8.4.3)
// =============================================================================
JS_FORWARD_EXPRESSION(Item, js_status_ok, (void), ((Item){.item = b2it(true)}))

// Throw TypeError if value is null or undefined (ES spec RequireObjectCoercible)
extern "C" Item js_require_object_coercible(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) {
        const char* type_str = (type == LMD_TYPE_NULL) ? "null" : "undefined";
        return js_throw_type_errorf("Cannot destructure '%s' as it is %s.", type_str, type_str);
    }
    return value;
}

extern "C" Item js_throw_value(Item value) {
    RootFrame roots(1);
    Rooted<Item> thrown_root(roots, value);
    value = thrown_root.get();
    // An Error already resting in the JS object lane must be retagged in place;
    // allocating a carrier here would break Error identity across throw/catch.
    LambdaError* error = js_error_from_value(value);
    bool original_is_error = error != NULL;
    if (!error) {
        char fallback_message[1024];
        fallback_message[0] = '\0';
        if (get_type_id(value) == LMD_TYPE_STRING) {
            String* s = it2s(value);
            if (s) snprintf(fallback_message, sizeof(fallback_message), "%.*s",
                s->len, s->chars);
        }
        if (fallback_message[0] == '\0') {
            snprintf(fallback_message, sizeof(fallback_message), "JavaScript exception");
        }
        error = err_create_heap(ERR_USER_ERROR, fallback_message, NULL);
    }
    Item lane = error ? err2it(error) : ItemError;
    if (error && !original_is_error) {
        // Non-Lambda JS payloads are carried by the lane and unwrapped only at
        // catch entry, preserving primitive throw semantics without a second ABI.
        error->thrown_value_item = value.item;
    }
    return lane;
}

extern "C" Item js_error_lane_payload(Item lane) {
    LambdaError* error = js_error_from_value(lane);
    if (error && error->thrown_value_item) {
        // Catch observes the original JS payload; the ERROR carrier exists only
        // to keep that payload alive while it crosses compiled/native frames.
        return (Item){.item = error->thrown_value_item};
    }
    // Catch exposes the resting Map lane; only propagating frames use the
    // ERROR tag, so an Error object is not mistaken for a failed return.
    if (error && error->is_static) {
        // Fault records have no shape while signals are being recovered; this
        // is the first safe boundary at which their Map-compatible view exists.
        error->type = js_error_carrier_type_map();
    }
    return error ? js_error_as_object(error) : lane;
}

extern "C" void js_error_lane_format(Item lane, char* out, int out_size) {
    if (!out || out_size <= 0) return;
    out[0] = '\0';
    LambdaError* error = js_error_from_value(lane);
    if (!error) {
        snprintf(out, (size_t)out_size, "JavaScript exception");
        return;
    }
    Item payload = error->thrown_value_item
        ? (Item){.item = error->thrown_value_item} : ItemNull;
    if (get_type_id(payload) == LMD_TYPE_STRING) {
        String* text = it2s(payload);
        if (text) snprintf(out, (size_t)out_size, "%.*s", text->len, text->chars);
        return;
    }
    const char* name = "Error";
    int name_len = 5;
    Item name_item = {.item = error->js_name_item};
    if (get_type_id(name_item) == LMD_TYPE_STRING) {
        String* text = it2s(name_item);
        if (text) {
            name = text->chars;
            name_len = (int)text->len;
        }
    }
    const char* message = error->message ? error->message : "";
    Item message_item = {.item = error->js_message_item};
    if (get_type_id(message_item) == LMD_TYPE_STRING) {
        String* text = it2s(message_item);
        if (text) {
            snprintf(out, (size_t)out_size, "%.*s: %.*s", name_len, name,
                (int)text->len, text->chars);
            return;
        }
    }
    if (message[0]) {
        snprintf(out, (size_t)out_size, "%.*s: %s", name_len, name, message);
    } else {
        snprintf(out, (size_t)out_size, "%.*s", name_len, name);
    }
}

// TDZ check: throw ReferenceError if variable is still in Temporal Dead Zone.
// Names arrive as module NameIds so delayed MIR never dereferences a compiler
// pool spelling after that pool has been released (D5.4.3).
extern "C" Item js_check_tdz(Item value, NameId name_id, int name_len) {
    if (value.item == ITEM_JS_TDZ) {
        NameRef name_ref = name_pool_resolve_id(context ? context->name_pool : NULL,
            name_id);
        const char* name = name_ref ? name_ref->chars : "";
        if (name_ref) name_len = (int)name_ref->len;
        char buf[256];
        int len = snprintf(buf, sizeof(buf), "Cannot access '%.*s' before initialization", name_len, name);
        return js_throw_named_error_text("ReferenceError", buf);
    }
    return value;
}

// Const assignment check: throw TypeError when assigning to a const variable.
extern "C" Item js_throw_const_assign(NameId name_id, int name_len) {
    NameRef name_ref = name_pool_resolve_id(context ? context->name_pool : NULL,
        name_id);
    const char* name = name_ref ? name_ref->chars : "";
    if (name_ref) name_len = (int)name_ref->len;
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "Assignment to constant variable '%.*s'", name_len, name);
    return js_throw_named_error_text("TypeError", buf);
}

// forward declaration for js_batch_reset (defined near the module runtime)
// forward declaration for array custom prototype check
// forward declarations for module namespace cache resets
extern "C" void js_fs_runtime_detach();
extern "C" void js_iterator_proto_cache_reset(void);
extern "C" void js_dynfunc_cache_reset(void);
extern "C" void js_cjs_metadata_reset(void);
extern "C" void js_proto_snapshot_invalidate(void);
extern "C" void js_process_reset_listeners(void);

static void js_batch_reset_runtime_caches(const char* reason, bool full_reset) {
    // Both reset modes must clear the same exception-bearing roots; leaving a
    // promise callback or assertion hook behind leaks the prior ERROR lane.
    js_eval_state_assert_clear(&js_runtime_state.eval, reason);
    js_reset_transient_call_state();
    js_decimal_number_egress_warning_reset();
    // Hot batches retain one heap/input arena across tests. Preserve its Input
    // owner while clearing transient heap-bound state so the intrinsic
    // prototype snapshot can restore mutations in that same realm; full heap
    // teardown must leave the pointer cleared (D6.2.2v2).
    Input* retained_input = js_input;
    js_reset_heap_bound_runtime_state();
    if (!full_reset) js_input = retained_input;
    js_reset_cached_realm_objects();
    // A partial hot reset restores the same constructor/prototype snapshot;
    // clearing catalog callables here would lazily create a second
    // %ThrowTypeError%/parseFloat object and break realm identity (D6.2.2v2).
    if (full_reset) js_builtin_cache_reset();
    if (full_reset) js_proto_snapshot_invalidate();
    js_fs_runtime_detach();
    jube_modules_runtime_reset();
    jube_modules_runtime_detach();
    js_globals_batch_reset();
    js_dom_batch_reset();
    memset(&js_regexp_last_match, 0, sizeof(js_regexp_last_match));
    js_regex_cache_reset();
    js_event_loop_init();
    js_process_reset_listeners();
    js_strict_mode = false;
    js_reset_core_module_caches();
    if (full_reset) js_eval_preamble_cache_reset();
    js_dynfunc_cache_reset();
    if (full_reset) js_array_runtime_items_cleanup_all();
    js_root_range_reset_all();
    js_assert_batch_runtime_state_clear(reason, true);
}

extern "C" void js_batch_reset() {
    // A host can finish a document after its context-owned JS state was
    // released.  Do not manufacture a replacement capsule just to reset it.
    if (!js_active_runtime_state) return;
    // increment epoch to invalidate cached heap objects
    js_heap_epoch++;
    // A full heap reset invalidates every retained harness/test relationship.
    // The next preamble creates a fresh pair of context-owned module slabs.
    js_runtime_state.batch_test_module_state_id = UINT32_MAX;
    js_runtime_state.batch_preamble_module_state_id = UINT32_MAX;
    js_runtime_state.batch_preamble_var_count = 0;
    // reset module variable table and active pointer
    js_reset_module_vars();
    // clear module registry (cached namespace_obj / mir_ctx are invalid after heap reset)
    module_registry_cleanup_for_runtime(context ? context->runtime : NULL);
    // clear JS module cache (specifier String* pointers become dangling after heap reset)
    js_module_cache_reset();
    // clear CommonJS metadata (filenames/modules are heap Items from the prior script)
    js_cjs_metadata_reset();
    js_batch_reset_runtime_caches("js_batch_reset pre-cleanup", true);
}

extern "C" void js_prepare_compiled_preamble_vars(int declaration_count) {
    js_reset_module_vars();
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
    // The active slab is the retained harness.  Test code must not execute in
    // it: harness closures intentionally keep that owner, while the old batch
    // path copied this prefix into a separate static test slab before each
    // script. Keep the same isolation using a reusable context-owned slab.
    LambdaModuleState* preamble_state = context ? context->active_js_module_state : NULL;
    uint32_t preamble_state_id = js_get_active_module_state_id();
    if (!preamble_state || preamble_state_id == UINT32_MAX) return;
    if (checkpoint_var_count < 0) checkpoint_var_count = 0;
    if (checkpoint_var_count > (int)preamble_state->var_count) {
        log_error("js-batch-state: preamble checkpoint exceeds its module slab");
        return;
    }
    js_runtime_state.batch_preamble_var_count = (uint32_t)checkpoint_var_count;

    uint32_t test_state_id = js_runtime_state.batch_test_module_state_id;
    if (js_runtime_state.batch_preamble_module_state_id != preamble_state_id ||
            !js_module_state_is_available(test_state_id)) {
        if (!js_activate_module_state((uint32_t)checkpoint_var_count)) return;
        test_state_id = js_get_active_module_state_id();
        js_runtime_state.batch_test_module_state_id = test_state_id;
        js_runtime_state.batch_preamble_module_state_id = preamble_state_id;
    } else if (!js_set_active_module_state_id(test_state_id)) {
        return;
    }
    if (!js_ensure_active_module_var_capacity((uint32_t)checkpoint_var_count)) return;

    Item* vars = js_ensure_active_module_vars();
    if (!vars) return;
    if (checkpoint_var_count > 0) {
        memcpy(vars, preamble_state->vars, (size_t)checkpoint_var_count * sizeof(Item));
    }
    // zero out test-owned bindings beyond the copied harness prefix
    int active_count = (int)context->active_js_module_state->var_count;
    for (int i = checkpoint_var_count; i < active_count; i++) {
        vars[i] = (Item){0};
    }
    js_module_var_count = checkpoint_var_count;
    // reset strict mode — prevents strict-mode test from poisoning subsequent non-strict tests
    js_strict_mode = false;
    // clear module registry (frees strdup/calloc per registered module)
    module_registry_cleanup_for_runtime(context ? context->runtime : NULL);
    // clear JS module cache counter
    js_module_cache_reset();
    js_cjs_metadata_reset();
    js_batch_reset_runtime_caches("js_batch_reset_to pre-cleanup", false);
}
JS_FORWARD_ITEM(js_new_error, (Item message), js_new_error_with_stack, (message, (Item){.item = ITEM_JS_UNDEFINED}))

extern "C" Item js_new_named_error(const char* type_name, const char* message) {
    RootFrame roots(2);
    Rooted<Item> name_root(roots, js_name_item(type_name ? type_name : "Error"));
    Rooted<Item> text_root(roots, js_name_item(message ? message : ""));
    if (!roots.valid()) return ItemError;
    // D5.3.3: constructing the message and Error object can collect; an
    // unrooted name/text pair produced anonymous [object Object] exceptions.
    return js_new_error_with_name(name_root.get(), text_root.get());
}
JS_FORWARD_ITEM(js_throw_named_error_text, (const char* type_name, const char* message), js_throw_value, (js_new_named_error(type_name, message)))

// AggregateError(errors, message): Error subclass with .errors array
extern "C" Item js_new_aggregate_error(Item errors, Item message) {
    JS_ROOTS(roots,
        errors_root, errors,
        message_root, message,
        err_root, ItemNull,
        errors_array_root, ItemNull);
    Item err_name = js_name_item("AggregateError", 14);
    if (get_type_id(message_root.get()) != LMD_TYPE_UNDEFINED &&
            message_root.get().item != ITEM_JS_UNDEFINED) {
        // AggregateError performs ToString(message) before publishing the
        // message own property; preserve the user's abrupt completion instead
        // of letting tagged Error storage turn it into a generic TypeError.
        Item message_string = js_to_string(message_root.get());
        if (item_is_error(message_string)) return message_string;
        message_root.set(message_string);
    }
    err_root.set(js_new_error_with_name(err_name, message_root.get()));
    // ToString(message) is evaluated while allocating the error carrier; an
    // abrupt completion must escape before IterableToList attempts to mutate
    // that completion as if it were the new AggregateError (D8.3.2).
    if (item_is_error(err_root.get())) return err_root.get();
    // AggregateError requires IterableToList. Array.from's no-argument
    // compatibility path returned [] for undefined and hid GetIterator's
    // required TypeError; use the iterator primitive directly (D6.2.2v2).
    errors_array_root.set(js_iterable_to_array(errors_root.get()));
    // IterableToList is part of construction, so an abrupt iterator result must
    // escape instead of being installed as the new error's `.errors` value.
    if (item_is_error(errors_array_root.get())) return errors_array_root.get();
    JS_ASSIGN_OR_RETURN(set_result, js_set_key_cstr(err_root.get(), "errors", errors_array_root.get()));
    return err_root.get();
}

static Item js_error_default_stack_string(Item error_name, Item message) {
    JsErrorTextParts text = js_error_text_parts(error_name, message);
    const char* name_str = text.name;
    int name_len = text.name_len;
    const char* msg_str = text.message;
    int msg_len = text.message_len;
    char buf[512];
    int len = msg_len > 0
        ? snprintf(buf, sizeof(buf), "%.*s: %.*s", name_len, name_str, msg_len, msg_str)
        : snprintf(buf, sizeof(buf), "%.*s", name_len, name_str);
    if (len < 0) len = 0;
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    return js_name_item(buf, len);
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

static int js_error_stack_trace_limit(void) {
    if (!context || !context->debug_info) return 0;
    Item error_ctor = js_get_constructor(js_name_item("Error", 5));
    if (get_type_id(error_ctor) != LMD_TYPE_FUNC) return 10;
    Item limit = js_get_key_cstr(error_ctor, "stackTraceLimit");
    TypeId limit_type = get_type_id(limit);
    if (limit_type != LMD_TYPE_INT && limit_type != LMD_TYPE_INT64 &&
            limit_type != LMD_TYPE_FLOAT) return 10;
    double dlimit = it2d(limit);
    if (dlimit <= 0 || dlimit != dlimit) return 0;
    int frame_limit = (int)dlimit;
    return frame_limit > 200 ? 200 : frame_limit;
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
    Item result = js_name_item(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

static int js_error_append_stack_frames(StrBuf* sb, StackFrame* trace, Item stack_start_fn) {
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
    return frame_count;
}

static StackFrame* js_capture_native_stack_frames(void) {
    int frame_limit = js_error_stack_trace_limit();
    if (frame_limit <= 0) return NULL;
    // LambdaJS stack traces reuse Lambda's zero-normal-overhead frame walk:
    // capture only while constructing an Error stack, never on successful calls.
    return err_capture_stack_trace(context->debug_info, frame_limit);
}

static RawStackTrace* js_capture_raw_native_stack_trace(void) {
    int frame_limit = js_error_stack_trace_limit();
    if (frame_limit <= 0) return NULL;
    return err_capture_raw_stack_trace(context->debug_info, frame_limit);
}

static Item js_error_stack_string_from_trace(Item error_name, Item message,
                                              StackFrame* trace) {
    if (!trace) return (Item){.item = ITEM_JS_UNDEFINED};

    Item header = js_error_default_stack_string(error_name, message);
    String* header_str = get_type_id(header) == LMD_TYPE_STRING ? it2s(header) : NULL;
    StrBuf* sb = strbuf_new_cap(256);
    if (!sb) return (Item){.item = ITEM_JS_UNDEFINED};
    if (header_str) strbuf_append_str_n(sb, header_str->chars, header_str->len);

    int frame_count = js_error_append_stack_frames(sb, trace,
        (Item){.item = ITEM_JS_UNDEFINED});
    Item result = frame_count > 0
        ? js_name_item(sb->str, sb->length)
        : (Item){.item = ITEM_JS_UNDEFINED};
    strbuf_free(sb);
    return result;
}

static Item js_error_native_stack_string(Item error_name, Item message, Item stack_start_fn) {
    StackFrame* trace = js_capture_native_stack_frames();
    if (!trace) return (Item){.item = ITEM_JS_UNDEFINED};

    StrBuf* sb = strbuf_new_cap(256);
    if (!sb) {
        err_free_stack_trace(trace);
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    Item header = js_error_default_stack_string(error_name, message);
    String* header_str = get_type_id(header) == LMD_TYPE_STRING ? it2s(header) : NULL;
    if (header_str) strbuf_append_str_n(sb, header_str->chars, header_str->len);

    int frame_count = js_error_append_stack_frames(sb, trace, stack_start_fn);

    Item result = frame_count > 0
        ? js_name_item(sb->str, sb->length)
        : (Item){.item = ITEM_JS_UNDEFINED};
    strbuf_free(sb);
    err_free_stack_trace(trace);
    return result;
}

static Item js_error_prepare_stack_trace_with_trace(Item error_obj, StackFrame* trace) {
    Item error_name = js_name_item("Error", 5);
    Item error_ctor = js_get_constructor(error_name);
    if (get_type_id(error_ctor) != LMD_TYPE_FUNC) return (Item){.item = ITEM_JS_UNDEFINED};
    Item prepare = js_get_name_key(error_ctor, "prepareStackTrace", 17);
    if (!js_is_callable(prepare)) return (Item){.item = ITEM_JS_UNDEFINED};

    Item frames = js_array_new(0);
    int frame_count = 0;
    for (StackFrame* frame = trace; frame; frame = frame->next) {
        Item frame_text = js_stack_frame_string(frame);
        if (get_type_id(frame_text) == LMD_TYPE_STRING) {
            js_array_push(frames, frame_text);
            frame_count++;
        }
    }
    if (frame_count == 0) return (Item){.item = ITEM_JS_UNDEFINED};

    Item args[2] = { error_obj, frames };
    JS_ASSIGN_OR_RETURN(prepared, js_call_function(prepare, (Item){.item = ITEM_JS_UNDEFINED}, args, 2));
    if (get_type_id(prepared) == LMD_TYPE_STRING) return prepared;
    return js_to_string(prepared);
}

extern "C" Item js_error_materialize_stack(Item error_obj) {
    LambdaError* error = js_error_from_value(error_obj);
    if (!error) return make_js_undefined();
    if (error->js_stack_item) return (Item){.item = error->js_stack_item};

    RootFrame roots(7);
    Rooted<Item> error_root(roots, error_obj);
    // Error property reads can allocate and move the unified carrier; read
    // through the rooted object so the raw-stack owner is never invalidated.
    Rooted<Item> name_root(roots, js_get_key_cstr(error_root.get(), "name"));
    Rooted<Item> message_root(roots, js_get_key_cstr(error_root.get(), "message"));
    Rooted<Item> prepared_root(roots, (Item){.item = ITEM_JS_UNDEFINED});
    Rooted<Item> native_root(roots, (Item){.item = ITEM_JS_UNDEFINED});
    Rooted<Item> eval_root(roots, (Item){.item = ITEM_JS_UNDEFINED});
    Rooted<Item> stack_root(roots, (Item){.item = ITEM_JS_UNDEFINED});
    if (item_is_error(name_root.get())) return name_root.get();
    if (item_is_error(message_root.get())) return message_root.get();
    if (get_type_id(name_root.get()) != LMD_TYPE_STRING) {
        name_root.set(make_string_item("Error"));
    }

    error = js_error_from_value(error_root.get());
    if (error && error->raw_stack_trace) {
        error->stack_trace = err_materialize_raw_stack_trace(error->raw_stack_trace);
        error->raw_stack_trace = NULL;
    }
    StackFrame* trace = error ? error->stack_trace : NULL;
    if (trace) {
        prepared_root.set(js_error_prepare_stack_trace_with_trace(error_root.get(), trace));
        native_root.set(js_error_stack_string_from_trace(name_root.get(), message_root.get(), trace));
    }
    eval_root.set(js_eval_source_stack_string(name_root.get(), message_root.get()));
    stack_root.set(prepared_root.get().item != ITEM_JS_UNDEFINED ? prepared_root.get() :
        native_root.get().item != ITEM_JS_UNDEFINED ? native_root.get() :
        eval_root.get().item != ITEM_JS_UNDEFINED ? eval_root.get() :
        js_error_default_stack_string(name_root.get(), message_root.get()));
    if (item_is_error(stack_root.get())) return stack_root.get();

    error = js_error_from_value(error_root.get());
    if (error) {
        error->js_stack_item = stack_root.get().item;
        error->js_own_flags |= JS_ERROR_OWN_STACK;
    }
    return stack_root.get();
}


// v12: Create Error with a compile-time stack trace string
extern "C" Item js_new_error_with_stack(Item message, Item stack_str) {
    // Keep one error-construction path: the canonical implementation owns the
    // exact native roots needed while descriptor, prototype, and stack helpers allocate.
    Item error_name = js_name_item("Error");
    return js_new_error_with_name_stack(error_name, message, stack_str);
}

// v11: Create a typed Error (TypeError, RangeError, SyntaxError, ReferenceError)
JS_FORWARD_ITEM(js_new_error_with_name, (Item error_name, Item message), js_new_error_with_name_stack, (error_name, message, (Item){.item = ITEM_JS_UNDEFINED}))

extern "C" JsClass js_error_class_id(Item value) {
    LambdaError* error = js_error_from_value(value);
    return error ? (JsClass)error->js_class_id : JS_CLASS_NONE;
}


// v12: Create typed Error with compile-time stack trace
extern "C" Item js_new_error_with_name_stack(Item error_name, Item message, Item stack_str) {
    RootFrame roots(8);
    Rooted<Item> error_name_root(roots, error_name);
    Rooted<Item> message_root(roots, message);
    Rooted<Item> stack_str_root(roots, stack_str);
    Rooted<Item> message_string_root(roots, ItemNull);
    Rooted<Item> error_root(roots, ItemNull);
    String* name_string = get_type_id(error_name_root.get()) == LMD_TYPE_STRING
        ? it2s(error_name_root.get()) : NULL;
    JsClass error_class = name_string
        ? js_class_from_name(name_string->chars, (int)name_string->len)
        : JS_CLASS_ERROR;
    if (error_class == JS_CLASS_NONE) error_class = JS_CLASS_ERROR;
    if (message_root.get().item != ITEM_JS_UNDEFINED &&
            get_type_id(message_root.get()) != LMD_TYPE_UNDEFINED) {
        message_string_root.set(get_type_id(message_root.get()) == LMD_TYPE_STRING
            ? message_root.get() : js_to_string(message_root.get()));
        if (item_is_error(message_string_root.get())) return message_string_root.get();
    } else {
        message_string_root.set(js_name_item("", 0));
    }
    String* message_string = it2s(message_string_root.get());
    const char* message_chars = message_string ? message_string->chars : "";
    LambdaError* error = err_create_heap(ERR_RUNTIME_ERROR, message_chars, NULL);
    if (!error) return ItemError;
    error->type = js_error_carrier_type_map();
    error->js_class_id = (uint8_t)error_class;
    error->js_name_item = name_string
        ? error_name_root.get().item
        : js_name_item("Error", 5).item;
    if (message_root.get().item != ITEM_JS_UNDEFINED &&
            get_type_id(message_root.get()) != LMD_TYPE_UNDEFINED) {
        error->js_message_item = message_string_root.get().item;
        error->js_own_flags |= JS_ERROR_OWN_MESSAGE;
    }
    error_root.set(js_error_as_object(error));

    if (stack_str_root.get().item != ITEM_JS_UNDEFINED) {
        error->js_stack_item = stack_str_root.get().item;
        error->js_own_flags |= JS_ERROR_OWN_STACK;
    } else {
        // stack symbolization is deferred; the raw PCs keep construction cheap
        // while retaining the exact debug-info generation for first access.
        error->raw_stack_trace = js_capture_raw_native_stack_trace();
        error->js_own_flags |= JS_ERROR_OWN_STACK;
    }
    return error_root.get();
}


// ES2022: Extract cause from options object and set on error
extern "C" Item js_error_set_cause(Item error, Item options) {
    TypeId opt_type = get_type_id(options);
    if (opt_type != LMD_TYPE_MAP && opt_type != LMD_TYPE_ARRAY &&
        opt_type != LMD_TYPE_FUNC && opt_type != LMD_TYPE_ELEMENT) {
        return error;
    }
    Item cause_key = js_name_item("cause");
    JS_ASSIGN_OR_RETURN(has_cause, js_in(cause_key, options));
    if (js_is_truthy(has_cause)) {
        JS_ASSIGN_OR_RETURN(cause_val, js_get_key_default(options, cause_key));
        JS_ASSIGN_OR_RETURN(set_result, js_set_key_default(error, cause_key, cause_val));
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
        Item stack_key = js_name_item("stack", 5);
        Item name = js_get_key_cstr(target, "name");
        if (get_type_id(name) != LMD_TYPE_STRING) {
            name = js_name_item("Error", 5);
        }
        Item message = js_get_key_cstr(target, "message");
        if (get_type_id(message) != LMD_TYPE_STRING) {
            message = (Item){.item = ITEM_JS_UNDEFINED};
        }
        Item stack = js_error_native_stack_string(name, message, ctor);
        if (stack.item == ITEM_JS_UNDEFINED) {
            stack = js_error_default_stack_string(name, message);
        }
        js_set_key_default(target, stack_key, stack);
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
    heap_register_gc_root(&js_pending_args_callee.item);
}

extern "C" Item js_get_this() {
    // Sloppy-mode coercion is applied by js_call_function/js_compute_callback_this
    // before installing the binding. Here only the uninitialized sentinel means
    // "no explicit this"; an actual JS null must remain observable to strict code.
    if (js_current_this.item == ITEM_JS_TDZ) {
        return js_throw_reference_error(make_string_item("Must call super constructor before accessing 'this'"));
    }
    if (js_current_this.item == 0) {
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
        return js_throw_reference_error(make_string_item("Must call super constructor before accessing 'this'"));
    }
    if (this_val.item == 0) {
        return js_get_global_this();
    }
    return this_val;
}

extern "C" void js_set_this(Item this_val) {
    js_current_this = this_val;
}
JS_FORWARD_EXPRESSION(Item, js_get_new_target, (), (js_new_target))

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
    js_current_this = (Item){0};
    js_new_target = (Item){0};
    memset(js_super_this_bound_stack, 0, sizeof(js_runtime_state.super_this_bound_stack));
    js_item_stack_clear(&js_runtime_state.super_this_values);
    js_pending_call_args = NULL;
    js_pending_call_argc = 0;
    js_pending_call_source = NULL;
    js_pending_call_source_len = 0;
    js_pending_args_is_strict = 0;
    js_pending_args_callee = (Item){0};
    // Array-vs-TypedArray dispatch is scoped to one builtin invocation; an
    // exceptional nested call must not select Array semantics for the next test.
    js_eval_state_reset(&js_runtime_state.eval);
}

void js_reset_heap_bound_runtime_state() {
    js_cached_object_proto = NULL;
    js_resolving_object_proto = false;
    js_private_field_initializing = false;
    js_deferred_instance_field_class = ItemNull;
    js_input = NULL;
}

void js_assert_batch_runtime_state_clear(const char* reset_name, bool include_heap_bound) {
    int leak_count = 0;
    const char* name = reset_name ? reset_name : "js_batch_reset";

    if (js_strict_mode) {
        leak_count++;
        log_error("js-batch-state: %s left strict mode enabled", name);
    }
    if (js_current_this.item != 0) {
        leak_count++;
        log_error("js-batch-state: %s left current this item=%lld", name, (long long)js_current_this.item);
    }
    if (js_new_target.item != 0) {
        leak_count++;
        log_error("js-batch-state: %s left new.target item=%lld", name, (long long)js_new_target.item);
    }
    if (js_super_this_bound_depth != 0) {
        leak_count++;
        log_error("js-batch-state: %s left super this binding depth=%d", name, js_super_this_bound_depth);
    }
    js_eval_state_assert_clear(&js_runtime_state.eval, name);
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


extern "C" Item js_build_arguments_object() {
    JS_ROOTS(roots,
        arr_root, ItemNull,
        companion_root, ItemNull,
        key_root, ItemNull,
        descriptor_root, ItemNull,
        tag_key_root, ItemNull,
        iterator_key_root, ItemNull,
        iterator_root, ItemNull,
        thrower_root, ItemNull,
        callee_key_root, ItemNull,
        callee_root, js_pending_args_callee);
    int argc = js_pending_call_argc;
    Item* args = js_pending_call_args;
    int is_strict = js_pending_args_is_strict;

    arr_root.set(js_array_new(argc));
    for (int i = 0; i < argc; i++) {
        // Arguments creation defines own indexed data properties directly;
        // inherited numeric setters must not intercept parameter materialization.
        js_array_define_dense_element_direct(arr_root.get(), i, args ? args[i] : ItemNull);
    }
    // Mark as Arguments object via is_content flag (used by iterator to snapshot length)
    arr_root.get().array->is_content = 1;
    // Mark as Arguments object via Symbol.toStringTag on companion map
    companion_root.set(js_new_object());
    companion_root.get().map->map_kind = MAP_KIND_ARRAY_PROPS;
    js_elements_set_props(arr_root.get().array, companion_root.get().map);

    key_root.set(js_name_item("length", 6));
    descriptor_root.set(js_new_object());
    js_set_prototype(descriptor_root.get(), ItemNull);
    js_set_key_cstr(descriptor_root.get(), "value", (Item){.item = i2it(argc)});
    js_set_key_cstr(descriptor_root.get(), "writable", (Item){.item = b2it(true)});
    js_set_key_cstr(descriptor_root.get(), "enumerable", (Item){.item = b2it(false)});
    js_set_key_cstr(descriptor_root.get(), "configurable", (Item){.item = b2it(true)});
    js_object_define_property(companion_root.get(), key_root.get(), descriptor_root.get());

    tag_key_root.set(js_well_known_symbol_key(4));
    js_set_key_default(companion_root.get(), tag_key_root.get(), js_name_item("Arguments", 9));
    js_mark_non_enumerable(companion_root.get(), tag_key_root.get());

    // ES6 §9.4.4.6 step 12: Set Symbol.iterator to Array.prototype.values
    iterator_key_root.set(js_well_known_symbol_key(1));
    Item array_proto = js_get_intrinsic_prototype_for_class(JS_CLASS_ARRAY);
    iterator_root.set(js_get_key_cstr(array_proto, "values"));
    js_set_key_default(companion_root.get(), iterator_key_root.get(), iterator_root.get());
    js_mark_non_enumerable(companion_root.get(), iterator_key_root.get());

    // v29: Set callee property (non-strict only; strict mode throws TypeError on access)
    if (is_strict) {
        thrower_root.set(js_intrinsic_binding_get(
            JS_BUILTIN_OWNER_FUNCTION_SYMBOL_INTERNAL, "ThrowTypeError", 14));
        callee_key_root.set(js_name_item("callee", 6));
        js_install_native_accessor(companion_root.get(), callee_key_root.get(),
                                   thrower_root.get(), thrower_root.get(),
                                   JSPD_NON_ENUMERABLE | JSPD_NON_CONFIGURABLE);
        js_set_key_cstr(companion_root.get(), "__strict_arguments__", (Item){.item = b2it(true)});
    } else {
        // Non-strict: callee is the function object (ES5 §10.6 step 13)
        if (callee_root.get().item != 0) {
            callee_key_root.set(js_name_item("callee", 6));
            descriptor_root.set(js_new_object());
            js_set_prototype(descriptor_root.get(), ItemNull);
            js_set_key_cstr(descriptor_root.get(), "value", callee_root.get());
            js_set_key_cstr(descriptor_root.get(), "writable", (Item){.item = b2it(true)});
            js_set_key_cstr(descriptor_root.get(), "enumerable", (Item){.item = b2it(false)});
            js_set_key_cstr(descriptor_root.get(), "configurable", (Item){.item = b2it(true)});
            js_object_define_property(companion_root.get(), callee_key_root.get(), descriptor_root.get());
        }
    }

    // Arguments construction allocates descriptors after creating the array;
    // every intermediate must remain precisely rooted until publication.
    return arr_root.get();
}

extern TypeMap EmptyMap;
