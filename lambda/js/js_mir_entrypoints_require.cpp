#include "js_mir_internal.hpp"
#include "js_interp.hpp"
#include "js_runtime_state.hpp"
#include "../jube/jube_registry.h"
#include "../module/node_core/node_runtime_state.hpp"
#include "../module/node_core/node_trace_events.hpp"
#undef js_input
#include "../runtime/lambda-error.h"
#include "../runtime/recovery_frame.h"
#include "../runtime/mir_dump.h"
#include "../../lib/mem_factory.h"
#include "../../lib/path_str.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
// the Win32 header declares GetTickCount64; omitting it leaves the native Windows build without the timing API declaration.
#include <windows.h>
#endif
#ifndef _WIN32
#include <sys/time.h>
#endif

int js_dynamic_import_suppress_module_drain = 0;
extern "C" int js_batch_execution_mode = 0;
extern "C" int js_process_current_exit_code(void);
extern "C" void js_async_hooks_drain_destroy_queue(void);
extern "C" Item js_module_get_builtin(Item specifier);

static Item js_mir_finalize_result(Item result, bool reusing_context,
        uint64_t* result_home) {
    if (reusing_context || get_type_id(result) != LMD_TYPE_FLOAT) return result;
    double value = it2d(result);
    if (value == (double)(int64_t)value && value >= INT32_MIN && value <= INT32_MAX) {
        return (Item){.item = i2it((int64_t)value)};
    }
    // A fresh compile context cannot own an out-of-band scalar after restore.
    return lambda_item_adopt_scalar_home(result, result_home);
}

static Item js_require_module_not_found(const char* specifier) {
    const char* name = specifier ? specifier : "";
    char message[640];
    snprintf(message, sizeof(message), "Cannot find module '%s'", name);
    Item error = js_new_error_with_name(make_string_item("Error"), make_string_item(message));
    js_set_key_cstr(error, "code", make_string_item("MODULE_NOT_FOUND"));
    // the returned error must stay attached to the call result; there is no
    // pending side channel for require callers to recover.
    return js_throw_value(error);
}

static JsMirPhaseTiming g_last_js_mir_phase_timing;
static JsMirPhaseTiming g_document_js_mir_phase_timing;
static bool g_document_js_mir_phase_timing_active = false;

Item js_mir_execute_compiled_entry(void* entry_func) {
    if (!entry_func || !context) return ItemError;

    typedef Item (*JsMainFunc)(Context*);
    JsMainFunc js_main = (JsMainFunc)entry_func;
    LambdaRecoveryFrame* recovery_frame = lambda_recovery_frame_begin_for(
        (Context*)context, LAMBDA_RECOVERY_CAP_EXECUTION_BOUNDARY);
    if (!recovery_frame) {
        log_error("js-mir-exec: failed to allocate recovery frame");
        return runtime_publish_result(context,
            lambda_recovery_publish_fault_item((Context*)context,
                LAMBDA_FAULT_OUT_OF_MEMORY, ERR_OK));
    }
    if (LAMBDA_RECOVERY_FRAME_SETJMP(recovery_frame)) {
        Item recovered = ItemError;
        if (!lambda_recovery_frame_restore_landing(recovery_frame)) {
            log_error("js-mir-exec: recovery frame landing invariant failed");
            recovered = lambda_recovery_publish_fault_item((Context*)context,
                LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK);
        } else {
            recovered = lambda_recovery_frame_fault_item((Context*)context,
                recovery_frame);
        }
        _lambda_stack_overflow_flag = false;
        lambda_recovery_frame_end(recovery_frame);
        return runtime_publish_result(context, recovered);
    }
    if (!lambda_recovery_frame_arm(recovery_frame)) {
        log_error("js-mir-exec: failed to arm recovery frame");
        lambda_recovery_frame_end(recovery_frame);
        return runtime_publish_result(context,
            lambda_recovery_publish_fault_item((Context*)context,
                LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK));
    }

    Item result = js_main((Context*)context);
    lambda_recovery_frame_end(recovery_frame);
    return runtime_publish_result(context, result);
}

JsMirMainFunc js_mir_link_main(MIR_context_t ctx, bool use_interp,
        void (*gen_interface)(MIR_context_t, MIR_item_t)) {
    MIR_link(ctx, use_interp ? MIR_set_interp_interface : gen_interface, import_resolver);
    return (JsMirMainFunc)find_func(ctx, (char*)"js_main");
}

static void js_mir_finish_script_turn(Runtime* runtime, Item result) {
    js_event_loop_drain_script_turn(
        runtime && runtime->dom_doc != NULL,
        js_dynamic_import_suppress_module_drain <= 0);
    if (js_batch_execution_mode) return;

    // Process lifecycle is a CLI boundary, not a per-test boundary; batch
    // workers skip it while direct and cached non-batch entries share it.
    int exit_code = item_is_error(result) ? 1 : js_process_current_exit_code();
    js_async_hooks_drain_destroy_queue();
    (void)js_process_emit_before_exit(exit_code);
    if (js_event_loop_has_refed_handles()) {
        js_event_loop_drain();
    } else {
        js_microtask_flush();
    }
    js_process_emit_exit(exit_code);
    node_trace_events_flush();
    js_process_current_exit_code();
}

bool js_activate_runtime_name_pool(void) {
    if (!context || !context->name_pool) return false;
    NamePool* current = context->name_pool;
    if (name_pool_id_mode(current) != NAME_POOL_STATIC) return true;
    NamePool* dynamic = name_pool_activate_runtime_dynamic(current);
    if (!dynamic) {
        log_error("js-name-pool: failed to seal static root and create dynamic child");
        return false;
    }
    context->name_pool = dynamic;
    if (context->runtime) context->runtime->name_pool = dynamic;
    return true;
}

static bool js_compiled_name_table_inherits_preamble(
        const JsMirTranspiler* mt) {
    // D3.4.4v2: an ES module owns a private zero-based name slab. A nested
    // module compile must not inherit the outer script's preamble merely
    // because that script remains the active compiler owner.
    return g_jm_preamble_in && mt && !mt->is_module;
}

bool js_link_compiled_name_table(const JsMirTranspiler* mt) {
    if (!context || !context->active_module_state) return false;
    bool inherits_preamble = js_compiled_name_table_inherits_preamble(mt);
    const PropertyKeySpec* inherited_specs = inherits_preamble
        ? g_jm_preamble_in->module_property_specs : NULL;
    uint32_t inherited = inherits_preamble
        ? g_jm_preamble_in->module_property_count : 0;
    uint32_t inherited_bytes = inherits_preamble
        ? g_jm_preamble_in->module_property_bytes_size : 0;
    // A preamble consumer is a growing document/test slab. Linking a fresh
    // inherited+local image for every script re-sealed that slab at a different
    // size; direct Get->Call then made the latent failure observable as soon as
    // a host method allocated names. Preserve the D3.4.4v2 NameId prefix once
    // and append each consumer unit at the base encoded into its MIR.
    if (inherits_preamble) {
        uint32_t active_names = lambda_module_state_property_key_count(
            lambda_active_module_state_id());
        if (active_names == 0 && inherited > 0 &&
                !lambda_module_state_link_property_keys(
                    context->active_module_state->module_id,
                    inherited_specs, inherited, inherited_bytes)) {
            return false;
        }
        if (lambda_module_state_property_key_count(
                lambda_active_module_state_id()) < inherited) {
            log_error("js-preamble-link: consumer is missing its inherited prefix");
            return false;
        }
        return js_append_compiled_name_table(mt);
    }

    PropertyKeySpec* image = NULL;
    uint32_t image_count = 0;
    uint32_t image_bytes = 0;
    if (!jm_build_property_key_image(inherited_specs, inherited,
            inherited_bytes, mt ? mt->module_name_specs : NULL, &image,
            &image_count, &image_bytes)) return false;
    bool linked = lambda_module_state_link_property_keys(
        context->active_module_state->module_id, image, image_count,
        image_bytes);
    mem_free(image);
    if (!linked) return false;
    return true;
}

// every consumer gets a fresh state whose prefix is the immutable preamble;
// using a prior consumer's appended count shifts this unit's names past the
// prefix that js_link_compiled_name_table actually installs.
JS_FORWARD_STATIC_EXPRESSION(uint32_t, js_preamble_consumer_name_base,
    (const JsPreambleState* preamble), preamble ? preamble->module_property_count : 0)

bool js_prelink_compiled_name_table(const JsMirTranspiler* mt) {
    bool inherits_preamble = js_compiled_name_table_inherits_preamble(mt);
    const PropertyKeySpec* inherited_specs = inherits_preamble
        ? g_jm_preamble_in->module_property_specs : NULL;
    uint32_t inherited = inherits_preamble
        ? g_jm_preamble_in->module_property_count : 0;
    uint32_t inherited_bytes = inherits_preamble
        ? g_jm_preamble_in->module_property_bytes_size : 0;
    PropertyKeySpec* image = NULL;
    uint32_t image_count = 0;
    uint32_t image_bytes = 0;
    if (!jm_build_property_key_image(inherited_specs, inherited,
            inherited_bytes, mt ? mt->module_name_specs : NULL, &image,
            &image_count, &image_bytes)) return false;
    bool linked = lambda_property_key_specs_prelink(image, image_count,
        image_bytes);
    mem_free(image);
    return linked;
}

bool js_append_compiled_name_table(const JsMirTranspiler* mt) {
    if (!context || !context->active_module_state) return false;
    PropertyKeySpec* image = NULL;
    uint32_t count = 0;
    uint32_t bytes = 0;
    if (!jm_build_property_key_image(NULL, 0, 0,
            mt ? mt->module_name_specs : NULL, &image, &count, &bytes)) {
        return false;
    }
    bool linked = lambda_module_state_append_property_keys(
        context->active_module_state->module_id, image, count, bytes);
    mem_free(image);
    return linked;
}

bool js_capture_compiled_name_table(const JsMirTranspiler* mt,
        JsPreambleState* state) {
    if (!state) return true;
    if (state->module_property_specs) {
        mem_free(state->module_property_specs);
        state->module_property_specs = NULL;
        state->module_property_count = 0;
        state->module_property_bytes_size = 0;
    }
    uint32_t inherited_count = g_jm_preamble_in
        ? g_jm_preamble_in->module_property_count : 0;
    uint32_t inherited_bytes = g_jm_preamble_in
        ? g_jm_preamble_in->module_property_bytes_size : 0;
    if ((!mt || !mt->module_name_specs || mt->module_name_specs->length == 0) &&
            inherited_count == 0) return true;
    return jm_build_property_key_image(
        g_jm_preamble_in ? g_jm_preamble_in->module_property_specs : NULL,
        inherited_count, inherited_bytes,
        mt ? mt->module_name_specs : NULL,
        &state->module_property_specs, &state->module_property_count,
        &state->module_property_bytes_size);
}

static long js_mir_phase_now_us(void) {
#ifndef _WIN32
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000000L + (long)tv.tv_usec;
#else
    return (long)GetTickCount64() * 1000L;
#endif
}

extern "C" void js_mir_reset_last_phase_timing(void) {
    memset(&g_last_js_mir_phase_timing, 0, sizeof(g_last_js_mir_phase_timing));
}

extern "C" void js_mir_get_last_phase_timing(JsMirPhaseTiming* out) {
    if (!out) return;
    *out = g_last_js_mir_phase_timing;
}

extern "C" void js_mir_begin_document_phase_timing(void) {
    // A document can transpile the browser preamble plus many script tasks;
    // resetting only the last-task record under-reports the load-time JS cost.
    memset(&g_document_js_mir_phase_timing, 0, sizeof(g_document_js_mir_phase_timing));
    g_document_js_mir_phase_timing_active = true;
}

extern "C" void js_mir_accumulate_last_phase_timing(bool is_preamble) {
    if (!g_document_js_mir_phase_timing_active) return;
    g_document_js_mir_phase_timing.parse_us += g_last_js_mir_phase_timing.parse_us;
    g_document_js_mir_phase_timing.ast_us += g_last_js_mir_phase_timing.ast_us;
    g_document_js_mir_phase_timing.early_us += g_last_js_mir_phase_timing.early_us;
    g_document_js_mir_phase_timing.imports_us += g_last_js_mir_phase_timing.imports_us;
    g_document_js_mir_phase_timing.mir_us += g_last_js_mir_phase_timing.mir_us;
    g_document_js_mir_phase_timing.link_us += g_last_js_mir_phase_timing.link_us;
    g_document_js_mir_phase_timing.execute_us += g_last_js_mir_phase_timing.execute_us;
    g_document_js_mir_phase_timing.cleanup_us += g_last_js_mir_phase_timing.cleanup_us;
    g_document_js_mir_phase_timing.total_us += g_last_js_mir_phase_timing.total_us;
    if (is_preamble) {
        g_document_js_mir_phase_timing.preamble_us += g_last_js_mir_phase_timing.total_us;
    }
}

extern "C" void js_mir_end_document_phase_timing(JsMirPhaseTiming* out) {
    if (out) *out = g_document_js_mir_phase_timing;
    g_document_js_mir_phase_timing_active = false;
}

// Tune6 §3.2: MIR generated-code volume for the last transpile.
static JsMirVolumeCounters g_last_js_mir_volume;

extern "C" void js_mir_volume_counters_reset(void) {
    g_last_js_mir_volume.functions_discovered = 0;
    g_last_js_mir_volume.mir_insns_emitted = 0;
}

extern "C" void js_mir_volume_counters_set(long functions_discovered, long mir_insns_emitted) {
    g_last_js_mir_volume.functions_discovered = functions_discovered;
    g_last_js_mir_volume.mir_insns_emitted = mir_insns_emitted;
}

extern "C" void js_mir_volume_counters_get(JsMirVolumeCounters* out) {
    if (out) *out = g_last_js_mir_volume;
}

static void js_mir_destroy_unowned_eval_context(Runtime* runtime,
        EvalContext* local_context, bool reusing_context) {
    if (!reusing_context && local_context) {
        if (!eval_context_matches(local_context)) {
            log_error("js-mir-cleanup: failed context is not current");
            return;
        }
        if (local_context->js_state &&
                !js_runtime_state_thread_matches(local_context)) {
            log_error("js-mir-cleanup: failed JS state is not current");
            return;
        }
        // MIR setup can fail after a one-shot JS heap is created; destroy it here because
        // runtime_cleanup() only owns heaps that reached the normal runtime stash point.
        if (local_context->type_list) {
            arraylist_free((ArrayList*)local_context->type_list);
            local_context->type_list = NULL;
        }
        if (local_context->heap) {
            heap_destroy();
            local_context->heap = NULL;
        }
        // D4.2.1v2/RN-NamePool: failed-run GC teardown may inspect NameRecords;
        // release the dedicated pool only after the heap is gone.
        if (local_context->name_pool) {
            name_pool_release(local_context->name_pool);
            local_context->name_pool = NULL;
        }
        js_runtime_state_destroy_context();
        if (runtime) {
            runtime->heap = NULL;
            runtime->name_pool = NULL;
            runtime->type_list = NULL;
        }
    }
}

Item js_mir_compile_unit_fail(MIR_context_t ctx,
        JsMirTranspiler* mt, JsTranspiler* tp, char* owned_source,
        Runtime* runtime, EvalContext* js_context, bool reusing_context,
        int mir_gen_initialized) {
    // MIR failures must release the active transpiler before its context and source;
    // one cleanup order prevents stale owners on every compile-error lane.
    if (mt) {
        jm_clear_active_js_transpile(NULL, mt, NULL);
        jm_destroy_mir_transpiler(mt);
    }
    g_active_mir_ctx = NULL;
    if (ctx) {
        // MIR_gen_init allocates a separate generator arena; MIR_finish does
        // not release it, so pair teardown with the context's init mode.
        jit_cleanup_mode(ctx,
            mir_gen_initialized < 0 ? !g_mir_interp_mode : mir_gen_initialized);
    }
    jm_clear_active_js_transpile(tp, NULL, NULL);
    js_transpiler_destroy(tp);
    jm_clear_active_js_transpile(NULL, NULL, owned_source);
    mem_free(owned_source);
    js_mir_destroy_unowned_eval_context(runtime, js_context, reusing_context);
    return (Item){.item = ITEM_ERROR};
}

extern "C" bool js_prepare_eval_context(Runtime* runtime,
        bool initialize_thread, EvalContext** out_context,
        bool* out_reusing_context) {
    if (!out_context || !out_reusing_context) return false;
    EvalContext* js_context = NULL;
    bool reusing_context = context && context->heap;
    if (reusing_context) {
        js_context = context;
        if (!context->type_list) {
            context->type_list = runtime && runtime->type_list
                ? runtime->type_list
                : arraylist_new(64);
        }
    } else {
        js_context = runtime_get_eval_context(runtime);
        if (!js_context) return false;
        bool thread_ready = initialize_thread
            ? eval_context_init(js_context)
            : eval_context_matches(js_context);
        if (!thread_ready) return false;
        heap_init();
        context->pool = context->heap->pool;
        context->name_pool = name_pool_create_runtime_static(context->pool);
        context->type_list = arraylist_new(64);
    }
    context->runtime = runtime;
    // Fresh and retained clients publish one canonical resource tuple.
    runtime_context_publish_owners(runtime, context);
    if (!js_runtime_state_init(context)) return false;
    *out_context = js_context;
    *out_reusing_context = reusing_context;
    return true;
}

// one compile unit owns one MIR transpiler and its module artifact.
JsMirTranspiler* js_mir_open_compile_unit(
        JsTranspiler* tp, const char* filename,
        const char* module_name, bool is_module, uint32_t module_name_base,
        unsigned int optimize_level, bool compact_storage,
        const char* log_prefix, bool install_error_handler,
        MIR_context_t* out_ctx) {
    if (!out_ctx) return NULL;
    *out_ctx = jit_init(optimize_level);
    if (!*out_ctx) {
        log_error("%s: MIR context init failed", log_prefix ? log_prefix : "js-mir");
        return NULL;
    }
    if (install_error_handler && g_batch_mir_error_handler) {
        MIR_set_error_func(*out_ctx, g_batch_mir_error_handler);
    }
    int import_capacity = compact_storage ? 16 : 64;
    int local_func_capacity = compact_storage ? 8 : 32;
    int var_scope_capacity = compact_storage ? 8 : 16;
    JsMirTranspiler* mt = jm_create_mir_transpiler(tp, *out_ctx, filename,
        is_module, import_capacity, local_func_capacity, var_scope_capacity,
        log_prefix);
    if (!mt) return NULL;
    jm_track_active_js_transpile(NULL, mt, NULL);
    mt->module_name_base = module_name_base;
    mt->module = MIR_new_module(*out_ctx, module_name);
    return mt;
}

Item transpile_js_ast_to_mir(Runtime* runtime, JsTranspiler* tp, JsAstNode* ast,
                             const char* filename, uint64_t* result_home) {
    log_debug("js-mir-ast: transpiling pre-built AST for '%s'", filename ? filename : "<string>");

    // Use Runtime's canonical context for a fresh JS activation. A nested
    // caller may reuse its already-bound context, but no path may switch back
    // to another owner after this call.
    EvalContext* js_context = NULL;
    bool reusing_context = false;
    if (!js_prepare_eval_context(runtime, true, &js_context, &reusing_context)) {
        return (Item){.item = ITEM_ERROR};
    }

    Input* js_input = Input::create(context->pool);
    js_runtime_set_input(js_input);

    MIR_context_t ctx = NULL;
    JsMirTranspiler* mt = js_mir_open_compile_unit(tp, filename,
        "ts_script", false, js_preamble_consumer_name_base(g_jm_preamble_in),
        g_js_mir_optimize_level, false, "js-mir-ast", false, &ctx);
    if (!mt) {
        return js_mir_compile_unit_fail(ctx, NULL, tp, NULL,
            runtime, js_context, reusing_context);
    }

    // transpile AST to MIR
    if (!transpile_js_mir_ast(mt, ast)) {
        log_error("js-mir-ast: collection/allocation failed");
        return js_mir_compile_unit_fail(ctx, mt, tp, NULL,
            runtime, js_context, reusing_context);
    }

    // This alternate entry point executes the same generated property-key
    // table as the source entry point.  Prelink it while the runtime root is
    // still static; otherwise js_activate_runtime_name_pool would turn these
    // compile-time spellings into per-session dynamic names.
    if (!js_prelink_compiled_name_table(mt)) {
        log_error("js-mir-ast: failed to prelink property-name table");
        return js_mir_compile_unit_fail(ctx, mt, tp, NULL,
            runtime, js_context, reusing_context);
    }

    // Canonical finalized artifact (MT2); see the JS path above.
#ifndef NDEBUG
    const bool ts_mir_dump_default_enabled = true;
#else
    const bool ts_mir_dump_default_enabled = false;
#endif
    mir_dump_finalized(ctx, "temp/ts_mir_dump.txt", ts_mir_dump_default_enabled);

    if (!jm_validate_mir_labels(ctx)) {
        log_error("js-mir-ast: NULL labels detected");
        return js_mir_compile_unit_fail(ctx, mt, tp, NULL,
            runtime, js_context, reusing_context);
    }

    JsMirMainFunc js_main = js_mir_link_main(ctx, g_mir_interp_mode,
        MIR_set_gen_interface);

    if (!js_main) {
        log_error("js-mir-ast: failed to find js_main");
        return js_mir_compile_unit_fail(ctx, mt, tp, NULL,
            runtime, js_context, reusing_context);
    }
    if (g_jm_preamble_out) {
        g_jm_preamble_out->entry_func = (void*)js_main;
        g_jm_preamble_out->owns_compiled_state = true;
        if (!js_capture_compiled_name_table(mt, g_jm_preamble_out)) {
            log_error("js-mir-ast: failed to retain property-name table");
            g_jm_preamble_out->entry_func = NULL;
            g_jm_preamble_out->owns_compiled_state = false;
            return js_mir_compile_unit_fail(ctx, mt, tp, NULL,
                runtime, js_context, reusing_context);
        }
    }

    // execute
    log_debug("js-mir-ast: executing JIT compiled code");
    if (!js_activate_runtime_name_pool()) {
        return js_mir_compile_unit_fail(ctx, mt, tp, NULL,
            runtime, js_context, reusing_context);
    }
    if (!lambda_module_state_reserve_and_activate((uint32_t)mt->module_var_count)) {
        return js_mir_compile_unit_fail(ctx, mt, tp, NULL,
            runtime, js_context, reusing_context);
    }
    if (!js_link_compiled_name_table(mt)) {
        return js_mir_compile_unit_fail(ctx, mt, tp, NULL,
            runtime, js_context, reusing_context);
    }
    Item result = js_mir_execute_compiled_entry((void*)js_main);
    log_debug("js-mir-ast: execution returned (type=%d)", get_type_id(result));

    // handle result
    Item final_result = js_mir_finalize_result(result, reusing_context, result_home);

    ArrayList* result_type_list = (ArrayList*)context->type_list;
    // The freshly created context is already Runtime-owned; no adoption or
    // pointer transfer is needed after execution.

    // cleanup
    jm_clear_active_js_transpile(NULL, mt, NULL);
    jm_destroy_mir_transpiler(mt);
    jit_cleanup_mode(ctx, !g_mir_interp_mode);
    // the AST entry point owns parser state after delegation; its TypeScript
    // caller must not destroy that parser-owned state a second time.
    js_transpiler_destroy(tp);

    // stash ephemeral GC heap on Runtime for caller cleanup
    if (runtime) {
        runtime->type_list = result_type_list;
    }

    jm_cleanup_deferred_mir();

    log_debug("js-mir-ast: transpilation completed");
    return final_result;
}

// ============================================================================
// Preamble support for batch mode two-module split
// ============================================================================

// Static globals for preamble mode control.
// Set by wrapper functions before calling the core transpiler.
bool g_jm_preamble_mode = false;               // compile as preamble (func decls → module vars)
bool g_jm_preamble_compile_only = false;        // retain code/metadata without binding a document heap
JsPreambleState* g_jm_preamble_out = NULL;      // output: preamble snapshot (preamble mode)
const JsPreambleState* g_jm_preamble_in = NULL;  // input: pre-seed from preamble (test mode)

void js_normalize_path_separators(char* path) {
    path_str_normalize_separators(path);
}

// ============================================================================
// Public entry point for JS transpilation via direct MIR generation
// ============================================================================

static bool js_source_contains_ascii(const char* source, size_t source_len, const char* needle) {
    if (!source || !needle) return false;
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || source_len < needle_len) return false;
    for (size_t i = 0; i + needle_len <= source_len; i++) {
        if (memcmp(source + i, needle, needle_len) == 0) return true;
    }
    return false;
}

JS_FORWARD_STATIC_EXPRESSION(bool, js_is_line_terminator, (char ch),
    ch == '\n' || ch == '\r')
JS_FORWARD_STATIC_EXPRESSION(bool, js_is_trivia_char, (char ch),
    ch == ' ' || ch == '\t' || ch == '\v' || ch == '\f' || js_is_line_terminator(ch))

static size_t js_skip_trivia(const char* source, size_t source_len, size_t offset,
                             bool allow_hashbang, bool* saw_line_terminator) {
    size_t i = offset;
    if (i == 0 && source_len >= 3 &&
        (unsigned char)source[0] == 0xEF && (unsigned char)source[1] == 0xBB &&
        (unsigned char)source[2] == 0xBF) {
        i = 3;
    }
    if (allow_hashbang && i + 1 < source_len && source[i] == '#' && source[i + 1] == '!') {
        i += 2;
        while (i < source_len && !js_is_line_terminator(source[i])) i++;
    }
    while (i < source_len) {
        char ch = source[i];
        if (js_is_trivia_char(ch)) {
            if (js_is_line_terminator(ch) && saw_line_terminator) *saw_line_terminator = true;
            i++;
            continue;
        }
        if (i + 1 < source_len && source[i] == '/' && source[i + 1] == '/') {
            i += 2;
            while (i < source_len && !js_is_line_terminator(source[i])) i++;
            continue;
        }
        if (i + 1 < source_len && source[i] == '/' && source[i + 1] == '*') {
            i += 2;
            while (i + 1 < source_len) {
                if (js_is_line_terminator(source[i]) && saw_line_terminator) *saw_line_terminator = true;
                if (source[i] == '*' && source[i + 1] == '/') {
                    i += 2;
                    break;
                }
                i++;
            }
            continue;
        }
        break;
    }
    return i;
}

static bool js_scan_string_literal(const char* source, size_t source_len, size_t offset, size_t* end_offset) {
    if (offset >= source_len) return false;
    char quote = source[offset];
    if (quote != '\'' && quote != '"') return false;
    size_t i = offset + 1;
    while (i < source_len) {
        char ch = source[i];
        if (ch == quote) {
            *end_offset = i + 1;
            return true;
        }
        if (js_is_line_terminator(ch)) return false;
        if (ch == '\\') {
            i++;
            if (i < source_len) {
                if (source[i] == '\r' && i + 1 < source_len && source[i + 1] == '\n') i++;
                i++;
            }
            continue;
        }
        i++;
    }
    return false;
}

static size_t js_commonjs_injection_offset(const char* source, size_t source_len) {
    size_t i = js_skip_trivia(source, source_len, 0, true, NULL);
    while (i < source_len) {
        size_t stmt_start = i;
        size_t literal_end = 0;
        if (!js_scan_string_literal(source, source_len, i, &literal_end)) break;

        bool saw_line_terminator = false;
        size_t after_literal = js_skip_trivia(source, source_len, literal_end, false, &saw_line_terminator);
        if (after_literal < source_len && source[after_literal] == ';') {
            i = js_skip_trivia(source, source_len, after_literal + 1, false, NULL);
            continue;
        }
        if (after_literal >= source_len || saw_line_terminator) {
            i = after_literal;
            continue;
        }
        return stmt_start;
    }
    return i;
}

static bool js_ast_is_es_module(JsAstNode* ast) {
    JsProgramNode* program = ast && ast->node_type == JS_AST_NODE_PROGRAM
        ? (JsProgramNode*)ast : NULL;
    for (JsAstNode* statement = program ? (JsAstNode*)program->body : NULL;
            statement; statement = (JsAstNode*)statement->next) {
        if (statement->node_type == JS_AST_NODE_IMPORT_DECLARATION ||
                statement->node_type == JS_AST_NODE_EXPORT_DECLARATION) return true;
    }
    return false;
}

static Item transpile_js_to_mir_core_profile_len(Runtime* runtime, const char* js_source,
                                                 size_t js_source_len, const char* filename,
                                                 uint64_t* result_home,
                                                 bool typescript_profile,
                                                 bool test262_native_harness) {
    // Recovery ownership begins before parsing. The first entry may initialize
    // an idle eval thread, but compilation cannot borrow and restore another
    // live context.
    EvalContext* compile_context = context ? context :
        runtime_get_eval_context(runtime);
    if (!eval_context_init(compile_context)) {
        log_error("js-mir: cannot initialize compile recovery context");
        return ItemError;
    }
    if (!js_runtime_state_init(compile_context)) {
        log_error("js-mir: cannot initialize compile recovery state");
        return ItemError;
    }
    js_mir_reset_last_phase_timing();
    long phase_total_start = js_mir_phase_now_us();
    log_debug("js-mir: starting direct MIR transpilation for '%s'", filename ? filename : "<string>");
    log_mem_stage("js-core: enter");

    char* owned_source = (char*)mem_alloc(js_source_len + 1, MEM_CAT_JS_RUNTIME);
    if (!owned_source) {
        log_error("js-mir: failed to allocate source buffer");
        return (Item){.item = ITEM_ERROR};
    }
    memcpy(owned_source, js_source, js_source_len);
    owned_source[js_source_len] = '\0';
    js_source = owned_source;
    jm_track_active_js_transpile(NULL, NULL, owned_source);

    // Inject __filename and __dirname for Node.js CommonJS compatibility.
    // Only for file-based scripts (not eval/REPL), and only if the source
    // doesn't already declare them (e.g., CJS-wrapped require'd modules).
    char* injected_source = NULL;
    if (filename && filename[0] != '<' &&
        !js_source_contains_ascii(js_source, js_source_len, "var __filename")) {
        // resolve to absolute path
        char abs_path[2048];
        if (filename[0] == '/') {
            snprintf(abs_path, sizeof(abs_path), "%s", filename);
        } else {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd))) {
                snprintf(abs_path, sizeof(abs_path), "%s/%s", cwd, filename);
            } else {
                snprintf(abs_path, sizeof(abs_path), "%s", filename);
            }
        }
        // Normalize: resolve . and .. components
        {
            char resolved[2048];
            if (realpath(abs_path, resolved)) {
                snprintf(abs_path, sizeof(abs_path), "%s", resolved);
            }
        }
        js_normalize_path_separators(abs_path);
        const char* last_slash = strrchr(abs_path, '/');
        int dir_len = last_slash ? (int)(last_slash - abs_path) : 1;
        const char* dir_str = last_slash ? abs_path : ".";

        char commonjs_header[4096];
        int off = snprintf(commonjs_header, sizeof(commonjs_header),
            "var __filename = \"%s\";\nvar __dirname = \"%.*s\";\n",
            abs_path, dir_len, dir_str);
        if (off < 0 || (size_t)off >= sizeof(commonjs_header)) off = 0;
        size_t insert_at = js_commonjs_injection_offset(js_source, js_source_len);
        injected_source = (char*)mem_alloc(js_source_len + (size_t)off + 1, MEM_CAT_JS_RUNTIME);
        memcpy(injected_source, js_source, insert_at);
        memcpy(injected_source + insert_at, commonjs_header, (size_t)off);
        memcpy(injected_source + insert_at + (size_t)off, js_source + insert_at, js_source_len - insert_at);
        injected_source[js_source_len + (size_t)off] = '\0';
        jm_clear_active_js_transpile(NULL, NULL, owned_source);
        mem_free(owned_source);
        owned_source = injected_source;
        jm_track_active_js_transpile(NULL, NULL, owned_source);
        js_source = injected_source;
        js_source_len += (size_t)off;
    }

    // Check env var for interpreter mode (once, as fallback for CLI --mir-interp)
    static bool interp_checked = false;
    if (!interp_checked) {
        if (!g_mir_interp_mode) {
            const char* env = getenv("JS_MIR_INTERP");
            if (env && (strcmp(env, "1") == 0 || strcmp(env, "true") == 0)) {
                g_mir_interp_mode = 1;
            }
        }
        if (g_mir_interp_mode) {
            log_info("js-mir: INTERPRETER MODE enabled");
        }
        interp_checked = true;
    }

    // Create JS transpiler (for parsing and AST building)
    JsTranspiler* tp = js_transpiler_create(runtime);
    if (!tp) {
        log_error("js-mir: failed to create transpiler");
        jm_clear_active_js_transpile(NULL, NULL, owned_source);
        mem_free(owned_source);
        return (Item){.item = ITEM_ERROR};
    }
    if (typescript_profile) {
        // The fast TS preprocessor erases syntax, not the compilation profile;
        // losing this bit made the TS-only type() intrinsic look like a JS
        // property-miss synthesis dependency after D6.2.2v2 removed that path.
        tp->strict_js = false;
        tp->strict_mode = true;
        tp->global_scope->strict = true;
    }
    jm_track_active_js_transpile(tp, NULL, NULL);

    // Parse JavaScript source
    long phase_start = js_mir_phase_now_us();
    if (!js_transpiler_parse(tp, js_source, js_source_len)) {
        log_error("js-mir: parse failed");
        return js_mir_compile_unit_fail(NULL, NULL, tp, owned_source,
            runtime, NULL, true);
    }
    g_last_js_mir_phase_timing.parse_us = js_mir_phase_now_us() - phase_start;
    log_mem_stage("js-core: ts_parsed");

    // Build JavaScript AST
    phase_start = js_mir_phase_now_us();
    JsAstNode* js_ast = js_transpiler_build_ast(tp);
    if (!js_ast) {
        log_error("js-mir: AST build failed");
        return js_mir_compile_unit_fail(NULL, NULL, tp, owned_source,
            runtime, NULL, true);
    }
    g_last_js_mir_phase_timing.ast_us = js_mir_phase_now_us() - phase_start;
    log_mem_stage("js-core: ast_built");

    if (tp->has_errors) {
        log_error("js-mir: early error(s) detected");
        return js_mir_compile_unit_fail(NULL, NULL, tp, owned_source,
            runtime, NULL, true);
    }

    if (js_ast_interpreter_requested()) {
        // The AST tier owns the retained JsScript, but uses the same source
        // parse, early-error pass, Runtime catalog, and EvalContext setup as
        // the MIR tier. Unsupported syntax is a deterministic admission
        // error; this explicit selector never silently falls back to MIR.
        jm_clear_active_js_transpile(tp, NULL, NULL);
        JsScript* script = js_script_adopt_transpiler(tp, runtime, filename);
        if (!script) {
            jm_clear_active_js_transpile(NULL, NULL, owned_source);
            mem_free(owned_source);
            return ItemError;
        }
        script->test262_native_harness = test262_native_harness;
        jm_clear_active_js_transpile(NULL, NULL, owned_source);
        mem_free(owned_source);
        Item result = js_ast_is_es_module(js_ast)
            ? js_interp_execute_es_module_script(runtime, script, result_home)
            : js_interp_execute_script(runtime, script, result_home);
        js_mir_finish_script_turn(runtime, result);
        // AST direct eval can schedule callbacks backed by deferred MIR code.
        // Drain the script turn before matching the JIT fresh-turn cleanup.
        if (!js_batch_execution_mode) jm_cleanup_deferred_mir();
        return result;
    }

    // Set up the canonical evaluation context early so module objects and
    // deferred callbacks share one lifetime owner.
    EvalContext* js_context = NULL;
    bool reusing_context = false;
    if (!js_prepare_eval_context(runtime, false, &js_context, &reusing_context)) {
        log_error("js-mir: Runtime context differs from eval-thread owner");
        return js_mir_compile_unit_fail(NULL, NULL, tp, owned_source,
            runtime, NULL, true);
    }
    if (runtime->dom_ui_context) {
        // The stack worker binds a distinct execution realm. Carry the host's
        // borrowed UI session into that realm before any DOM wrapper or task is
        // created; rebinding it after execution leaves callbacks in a dead realm.
        js_dom_set_ui_context(runtime->dom_ui_context);
    }
    ArrayList* previous_debug_info = context->debug_info;
    RuntimeCurrentFileScope current_file(context, filename ? filename : "<string>");


    // Create Input context for JS runtime — must be before module loading
    Input* js_input = Input::create(context->pool);
    js_runtime_set_input(js_input);

    bool use_mir_interp_for_script = g_mir_interp_mode != 0;
    bool auto_interp_for_large_source = false;
    int saved_mir_interp_mode = g_mir_interp_mode;
    if (!use_mir_interp_for_script && g_js_mir_optimize_level == 0 &&
        mir_large_interp_enabled() &&
        js_source_len >= mir_large_source_interp_threshold()) {
        g_mir_interp_mode = 1;
        use_mir_interp_for_script = true;
        auto_interp_for_large_source = true;
        log_info("js-mir: large source (%zu bytes) uses MIR interpreter at opt=0", js_source_len);
    }
    if (auto_interp_for_large_source) {
        g_mir_interp_mode = saved_mir_interp_mode;
    }
    MIR_context_t ctx = NULL;

    JsMirTranspiler* mt = js_mir_open_compile_unit(tp, filename, "js_script", false,
        js_preamble_consumer_name_base(g_jm_preamble_in), g_js_mir_optimize_level,
        false, "js-mir", true, &ctx);
    if (!mt) {
        return js_mir_compile_unit_fail(ctx, NULL, tp, owned_source,
            runtime, js_context, reusing_context);
    }
    g_active_mir_ctx = ctx;  // track for batch timeout recovery

    mt->preamble_mode = g_jm_preamble_mode;
    if (g_jm_preamble_in) {
        mt->preamble_entries = g_jm_preamble_in->entries;
        mt->preamble_entry_count = g_jm_preamble_in->entry_count;
        mt->preamble_var_count = g_jm_preamble_in->module_var_count;
    }

    // Transpile AST to MIR
    phase_start = js_mir_phase_now_us();
    bool transpile_ok = transpile_js_mir_ast(mt, js_ast);
    g_last_js_mir_phase_timing.mir_us = js_mir_phase_now_us() - phase_start;
    log_mem_stage("js-core: ast_to_mir");
    if (!transpile_ok) {
        log_error("js-mir: collection/allocation failed for '%s'",
            filename ? filename : "<string>");
        return js_mir_compile_unit_fail(ctx, mt, tp, owned_source,
            runtime, js_context, reusing_context);
    }

    // Complete static discovery before any realm or module initializer can
    // create a runtime name. The main module has now contributed its sealed
    // property-key image; imported modules contribute theirs in precompile.
    if (!js_prelink_compiled_name_table(mt)) {
        log_error("js-mir: failed to prelink main property-name table");
        return js_mir_compile_unit_fail(ctx, mt, tp, owned_source,
            runtime, js_context, reusing_context);
    }

    if (!g_jm_preamble_compile_only) {
        phase_start = js_mir_phase_now_us();
        if (!js_activate_runtime_name_pool()) {
            log_error("js-mir: failed to activate dynamic NamePool");
            return js_mir_compile_unit_fail(ctx, mt, tp, owned_source,
                runtime, js_context, reusing_context);
        }
        // Realm construction is runtime work: it must occur only after static
        // discovery seals the root and installs the dynamic child.
        (void)js_get_global_this();
        jm_load_imports(runtime, js_ast, filename);
        g_last_js_mir_phase_timing.imports_us = js_mir_phase_now_us() - phase_start;
        log_mem_stage("js-core: imports_loaded");
    }

    // Canonical finalized artifact (MT2): transpile_js_mir_ast has already run
    // MIR_finish_func/MIR_finish_module, so this is the finalized stage the
    // emission tests read. LAMBDA_MIR_DUMP_PATH names a private artifact and
    // works in release builds; the default path matches transpile-mir.cpp's
    // Lambda-side dump — debug builds write it, and mir_dump_finalized's own
    // --no-log gate keeps test runs from emitting it.
#ifndef NDEBUG
    const bool js_mir_dump_default_enabled = true;
#else
    const bool js_mir_dump_default_enabled = false;
#endif
    mir_dump_finalized(ctx, "temp/js_mir_dump.txt", js_mir_dump_default_enabled);

    // Pre-link validation: abort gracefully if NULL labels found
    if (!jm_validate_mir_labels(ctx)) {
        log_error("js-mir: NULL labels detected, aborting link for '%s'", filename ? filename : "<string>");
        return js_mir_compile_unit_fail(ctx, mt, tp, owned_source,
            runtime, js_context, reusing_context);
    }

    // Link and generate
    // Count finalized executable MIR instructions (drives the interpreter
    // policy and the AST-tuning volume gate). Labels are structural and must
    // match the MT7 artifact counter used by test_mir_ratchet_gtest.
    uint64_t total_functions = 0;
    uint64_t total_insns = 0;
    mir_count_module_volume(ctx, NULL, &total_functions, &total_insns);
    js_mir_volume_counters_set((long)total_functions, (long)total_insns);
    // Tune6 (see vibe/jube/Transpile_Js_Tune6_AST.md §0.2a–§0.2d): the dominant JS
    // startup cost is eager per-function MIR_gen during MIR_link. For large modules
    // opt=0 JIT ≈ opt=2 JIT (link is codegen-emit-bound, not optimizer-bound), so
    // the old ">100k → opt=0" downgrade was a near-no-op. The genuinely fast path
    // for large *cold* code is the MIR interpreter, which skips codegen entirely.
    //
    // Policy: prefer the interpreter when the module is very large (any context) OR
    // when running in a document/Radiant context (cold vendor JS) above a moderate
    // size. Keep the JIT for compute-heavy standalone JS. The generator stays
    // initialized (g_mir_interp_mode is left 0), so jit_init/jit_cleanup remain
    // paired — only the MIR_link interface differs. Disable with LAMBDA_JS_LARGE_INTERP=0.
    unsigned int effective_opt = g_js_mir_optimize_level;
    bool document_context = (runtime && runtime->dom_doc != NULL);
    if (!use_mir_interp_for_script && mir_large_interp_enabled() &&
        (total_insns > JM_LARGE_MODULE_INSN_THRESHOLD ||
         (document_context && (g_js_force_document_interp ||
                               total_insns > JM_RADIANT_INTERP_INSN_THRESHOLD)))) {
        use_mir_interp_for_script = true;
        log_info("js-mir: %s module (%lu insns)%s → MIR interpreter (skip JIT codegen)",
                 total_insns > JM_LARGE_MODULE_INSN_THRESHOLD ? "large" : "cold-document",
                 total_insns, document_context ? " [document]" : "");
    }
    // Fallback: if we still JIT a very large module (interpreter disabled), downgrade
    // opt to avoid MIR's super-linear opt passes on huge functions.
    if (!use_mir_interp_for_script && effective_opt >= 2 &&
        total_insns > JM_LARGE_MODULE_INSN_THRESHOLD) {
        log_info("js-mir: large module (%lu insns) → opt=0 (was %u)", total_insns, effective_opt);
        MIR_gen_set_optimize_level(ctx, 0);
        effective_opt = 0;
    }
    // Tune6: JS_LAZY_MIR=1 selects MIR's native per-function lazy codegen
    // (MIR_set_lazy_gen_interface) instead of eager generation. Lazy gen installs
    // a wrapper thunk on func_item->addr and runs MIR_gen on first call, then
    // redirects the thunk to the real code. This defers the dominant per-function
    // codegen cost out of the link phase to first call. ABI-compatible: both the
    // direct-call (MIR_new_ref_op(func_item)) and indirect (js_call_function)
    // paths use func_item->addr. Does not affect the interp path.
    static int js_lazy_mir_cached = -1;
    if (js_lazy_mir_cached < 0) {
        const char* lazy_env = getenv("JS_LAZY_MIR");
        // Lazy generation remains opt-in for JS modules: sequential library
        // tests can retain deferred MIR contexts across host callbacks, so the
        // eager path is the correctness default until that owner lifetime is
        // made explicit. Performance captures may opt in with JS_LAZY_MIR=1.
        js_lazy_mir_cached = lazy_env
            ? (lazy_env[0] && strcmp(lazy_env, "0") != 0 ? 1 : 0)
            : 0;
    }
    void (*gen_interface)(MIR_context_t, MIR_item_t) =
        js_lazy_mir_cached ? MIR_set_lazy_gen_interface : MIR_set_gen_interface;

    phase_start = js_mir_phase_now_us();
    JsMirMainFunc linked_main = js_mir_link_main(ctx,
        use_mir_interp_for_script, gen_interface);
    g_last_js_mir_phase_timing.link_us = js_mir_phase_now_us() - phase_start;
    log_mem_stage("js-core: mir_linked");
    void* js_debug_info = jm_build_js_debug_info(mt, filename);
    context->debug_info = (ArrayList*)js_debug_info;
    // Restore opt level if we changed it
    if (effective_opt != g_js_mir_optimize_level) {
        MIR_gen_set_optimize_level(ctx, g_js_mir_optimize_level);
    }

    // Find js_main
    JsMirMainFunc js_main = linked_main;

    if (!js_main) {
        log_error("js-mir: failed to find js_main");
        context->debug_info = previous_debug_info;
        if (js_debug_info) free_debug_info_table(js_debug_info);
        return js_mir_compile_unit_fail(ctx, mt, tp, owned_source,
            runtime, js_context, reusing_context);
    }
    if (g_jm_preamble_out) {
        // The reusable preamble must retain both its entry thunk and ownership;
        // otherwise batch callers silently recompile it and leak its MIR state.
        g_jm_preamble_out->entry_func = (void*)js_main;
        g_jm_preamble_out->owns_compiled_state = true;
        if (!js_capture_compiled_name_table(mt, g_jm_preamble_out)) {
            log_error("js-mir: failed to retain property-name table");
            return (Item){.item = ITEM_ERROR};
        }
    }

    // Publish/grow the execution slab before realm setup. DOM installation can
    // compile inline handlers, and those nested native callbacks need the same
    // active module owner as the js_main that follows.
    if (!g_jm_preamble_compile_only && !js_activate_runtime_name_pool()) {
        return (Item){.item = ITEM_ERROR};
    }
    if (!g_jm_preamble_compile_only && g_jm_preamble_in) {
        // Each preamble consumer has its own property-key image. Reusing the
        // previous consumer slab leaves its sealed key count attached to the
        // next MIR unit, whose local property set is necessarily different.
        uint32_t source_state_id = lambda_active_module_state_id();
        if (!lambda_module_state_reserve_and_activate(
                    (uint32_t)mt->module_var_count) ||
                !lambda_module_state_copy_var_prefix(
                    source_state_id, lambda_active_module_state_id(),
                    (uint32_t)g_jm_preamble_in->module_var_count)) {
            log_error("js-mir: failed to create preamble consumer module state");
            return (Item){.item = ITEM_ERROR};
        }
    } else if (!g_jm_preamble_compile_only) {
        if (!lambda_module_state_reserve_and_activate(
                    (uint32_t)mt->module_var_count)) {
            return (Item){.item = ITEM_ERROR};
        }
    }
    if (!g_jm_preamble_compile_only && !js_link_compiled_name_table(mt)) {
        log_error("js-mir: failed to link compiled property-name table");
        return (Item){.item = ITEM_ERROR};
    }

    // One runtime turn spans JIT code and nested AST fallbacks alike; compile-
    // only preambles do not enter the execution lifecycle.
    RuntimeExecutionScope execution_scope(
        g_jm_preamble_compile_only ? NULL : context);

    // v14: initialize event loop before execution. Dynamic import runs inside
    // an active script, so preserve the caller's pending PromiseJobs.
    if (!g_jm_preamble_compile_only && execution_scope.is_outermost() &&
            !js_runtime_state.event_loop.callback_running &&
            js_dynamic_import_suppress_module_drain <= 0) {
        js_event_loop_init();
    }

    // Compile-only cache construction must not instantiate DOM wrappers or
    // inline handlers in the disposable compilation heap.
    if (runtime->dom_doc && !g_jm_preamble_compile_only) {
        js_dom_set_document(runtime->dom_doc);
    }

    // Execute
    log_debug("js-mir: executing JIT compiled code");

    // Save module_consts as eval preamble BEFORE execution so that
    // eval()/new Function() called during js_main can resolve outer-scope
    // var declarations via the active context-owned module slab.
    if (mt->module_consts && !g_jm_preamble_mode) {
        JsModuleConstEntry* next_entries = NULL;
        int next_entry_count = 0;
        bool copy_succeeded = js_preamble_entries_from_module_consts(
            mt->module_consts, &next_entry_count, &next_entries);
        if (copy_succeeded) {
            // retain the old slab until all shallow entries are copied.
            js_eval_preamble_entries_free();
            g_eval_preamble_entries = next_entries;
            g_eval_preamble_entry_count = next_entry_count;
            g_eval_preamble_var_count = mt->module_var_count;
        } else {
            // The old slab was only needed while constructing this replacement.
            // Do not leave a failed replacement visible to a nested eval.
            js_eval_preamble_entries_free();
        }
        log_debug("js-mir: saved eval preamble: %d entries, %d module vars",
            g_eval_preamble_entry_count, g_eval_preamble_var_count);
    }

    // With-preamble mode: caller already called js_batch_reset_to() — harness vars preserved
    phase_start = js_mir_phase_now_us();
    Item result = g_jm_preamble_compile_only
        ? ItemNull
        : js_mir_execute_compiled_entry((void*)js_main);
    g_last_js_mir_phase_timing.execute_us = js_mir_phase_now_us() - phase_start;
    if (item_is_error(result)) {
        log_error("js-mir-execution: uncaught error lane");
    }
    log_mem_stage("js-core: js_main_done");

    // v14: drain the event loop while JIT module is still alive
    // (MIR_finish below destroys compiled code, so timers must fire here).
    // Dynamic import loads modules from inside an already-running script; if the
    // nested module drains the global microtask queue, outer async-generator
    // Promise jobs can run with the imported module's temporary context active.
    if (!g_jm_preamble_compile_only) js_mir_finish_script_turn(runtime, result);
    log_debug("js-mir: event loop drained");

    // Preamble mode: snapshot module_consts so tests can inherit harness definitions
    if (g_jm_preamble_out && mt->module_consts) {
        js_preamble_entries_free(g_jm_preamble_out->entries,
                                 g_jm_preamble_out->entry_count);
        g_jm_preamble_out->entries = NULL;
        g_jm_preamble_out->entry_count = 0;
        g_jm_preamble_out->module_var_count = mt->module_var_count;
        JsModuleConstEntry* entries = NULL;
        int entry_count = 0;
        bool copy_succeeded = js_preamble_entries_from_module_consts(
            mt->module_consts, &entry_count, &entries);
        if (copy_succeeded) {
            g_jm_preamble_out->entries = entries;
            g_jm_preamble_out->entry_count = entry_count;
        } else {
            g_jm_preamble_out->module_var_count = 0;
        }
        log_debug("js-mir: preamble snapshot: %d entries, %d module vars",
            g_jm_preamble_out->entry_count, g_jm_preamble_out->module_var_count);
    }

    // Handle result (same logic as js_transpiler_compile)
    Item final_result = js_mir_finalize_result(result, reusing_context, result_home);

    // Convert JS HashMap objects to VMap for proper printing (before context restore)
    // (no longer needed — JS objects are now Lambda Maps)

    ArrayList* result_type_list = (ArrayList*)context->type_list;
    context->debug_info = previous_debug_info;
    // A fresh activation already used Runtime's canonical context.

    // Cleanup
    phase_start = js_mir_phase_now_us();
    jm_clear_active_js_transpile(NULL, mt, NULL);
    jm_destroy_mir_transpiler(mt);
    if (g_jm_preamble_out) {
        // Preamble mode: keep MIR context alive — harness function objects reference compiled code
        g_jm_preamble_out->mir_ctx = ctx;
        g_jm_preamble_out->source_buffer = owned_source;
        jm_clear_active_js_transpile(NULL, NULL, owned_source);
        owned_source = NULL;
    } else if (reusing_context) {
        // Hot-reload batch mode: defer MIR context destruction.
        // The heap persists across tests, so function objects on the heap
        // may still reference code pages in this MIR context. Destroying
        // the context now would leave dangling func_ptr pointers → SIGBUS.
        jm_defer_mir_cleanup(ctx);
        if (module_mir_context_count > 0) {
            module_mir_source_buffers[module_mir_context_count - 1] = owned_source;
            jm_clear_active_js_transpile(NULL, NULL, owned_source);
            owned_source = NULL;
        }
    } else {
        g_active_mir_ctx = NULL;
        jit_cleanup_mode(ctx, !g_mir_interp_mode);
    }
    if (js_debug_info) {
        free_debug_info_table(js_debug_info);
        js_debug_info = NULL;
    }
    g_active_mir_ctx = NULL;  // normal cleanup done, no longer need recovery

    // stash ephemeral GC heap on Runtime for caller cleanup.
    // each heap allocates ~12MB (data zone + tenured zone + bump block), so
    // leaking one per document causes massive RSS growth in batch mode.
    // caller must call runtime_reset_heap() after inspecting the result.
    // In preamble mode, also stash the heap so the caller can retain it
    // for interactive event handler compilation (needs heap for reusing_context).
    if (runtime) {
        runtime->type_list = result_type_list;
    }

    // In hot-reload batch mode, skip deferred MIR cleanup — accumulated contexts
    // must persist until batch end (heap objects may reference their code pages).
    if (!reusing_context) {
        jm_cleanup_deferred_mir();
    }
    // Clear recovery ownership while this compilation's JS realm is still
    // bound.  Clearing after the TLS unbind leaves the completed transpiler
    // recorded in the context capsule, so runtime teardown destroys it again.
    jm_clear_active_js_transpile(tp, NULL, NULL);
    jm_clear_active_js_transpile(NULL, NULL, owned_source);
    js_transpiler_destroy(tp);
    mem_free(owned_source);
    g_last_js_mir_phase_timing.cleanup_us = js_mir_phase_now_us() - phase_start;
    g_last_js_mir_phase_timing.total_us = js_mir_phase_now_us() - phase_total_start;

    log_debug("js-mir: transpilation completed");
    return final_result;
}

Item transpile_js_to_mir_core_len(Runtime* runtime, const char* js_source,
                                  size_t js_source_len, const char* filename,
                                  uint64_t* result_home) {
    return transpile_js_to_mir_core_profile_len(runtime, js_source, js_source_len,
                                                filename, result_home, false, false);
}

Item transpile_js_to_mir_core(Runtime* runtime, const char* js_source,
                              const char* filename, uint64_t* result_home) {
    return transpile_js_to_mir_core_len(runtime, js_source, strlen(js_source), filename,
                                        result_home);
}

// ============================================================================
// Public API wrappers for preamble support
// ============================================================================

Item transpile_js_to_mir(Runtime* runtime, const char* js_source, const char* filename,
                         uint64_t* result_home) {
    return transpile_js_to_mir_len(runtime, js_source, strlen(js_source), filename, result_home);
}

Item transpile_js_to_mir_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                             const char* filename, uint64_t* result_home) {
    g_jm_preamble_mode = false;
    g_jm_preamble_out = NULL;
    g_jm_preamble_in = NULL;
    return transpile_js_to_mir_core_len(runtime, js_source, js_source_len, filename, result_home);
}

Item transpile_js_to_mir_test262_native_len(Runtime* runtime, const char* js_source,
                                            size_t js_source_len, const char* filename,
                                            uint64_t* result_home) {
    g_jm_preamble_mode = false;
    g_jm_preamble_out = NULL;
    g_jm_preamble_in = NULL;
    return transpile_js_to_mir_core_profile_len(runtime, js_source, js_source_len,
                                                filename, result_home, false, true);
}

Item transpile_js_typescript_to_mir_len(Runtime* runtime, const char* js_source,
                                        size_t js_source_len, const char* filename,
                                        uint64_t* result_home) {
    g_jm_preamble_mode = false;
    g_jm_preamble_out = NULL;
    g_jm_preamble_in = NULL;
    return transpile_js_to_mir_core_profile_len(runtime, js_source, js_source_len,
                                                filename, result_home, true, false);
}

Item transpile_js_to_mir_preamble(Runtime* runtime, const char* js_source, const char* filename,
                                   JsPreambleState* out_state, uint64_t* result_home) {
    return transpile_js_to_mir_preamble_len(runtime, js_source, strlen(js_source), filename,
                                            out_state, result_home);
}

Item transpile_js_to_mir_preamble_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                                      const char* filename, JsPreambleState* out_state,
                                      uint64_t* result_home) {
    g_jm_preamble_mode = true;
    g_jm_preamble_out = out_state;
    g_jm_preamble_in = NULL;
    // Preamble (harness) always compiled at -O3 for best runtime performance
    unsigned int saved_level = g_js_mir_optimize_level;
    g_js_mir_optimize_level = 3;
    Item result = transpile_js_to_mir_core_len(runtime, js_source, js_source_len, filename,
                                                result_home);
    if (out_state && result.item != ITEM_ERROR) {
        // The preamble's declarations and function closures are valid only in
        // the slab initialized by this js_main invocation.
        out_state->module_state_id = lambda_active_module_state_id();
    }
    g_js_mir_optimize_level = saved_level;
    g_jm_preamble_mode = false;
    g_jm_preamble_out = NULL;
    return result;
}

static Item compile_js_mir_cached_unit_len(
    Runtime* runtime, const char* js_source, size_t js_source_len,
    const char* filename, bool preamble_mode,
    const JsPreambleState* preamble, JsPreambleState* out_state) {
    // Cached units retain code and declaration metadata only. Execution is a
    // separate step so no document heap can become part of the cache owner.
    g_jm_preamble_mode = preamble_mode;
    g_jm_preamble_compile_only = true;
    g_jm_preamble_out = out_state;
    g_jm_preamble_in = preamble;
    unsigned int saved_level = g_js_mir_optimize_level;
    g_js_mir_optimize_level = 3;
    uint64_t compile_result_home = 0;
    Item result = transpile_js_to_mir_core_len(runtime, js_source, js_source_len, filename,
                                                &compile_result_home);
    g_js_mir_optimize_level = saved_level;
    g_jm_preamble_out = NULL;
    g_jm_preamble_in = NULL;
    g_jm_preamble_compile_only = false;
    g_jm_preamble_mode = false;
    return result;
}

Item compile_js_mir_preamble_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                                 const char* filename, JsPreambleState* out_state) {
    // Unlike js262's hot-heap preamble, a browser preamble captures document
    // globals. Retain only MIR code and declaration metadata, then instantiate
    // it separately into each document heap.
    return compile_js_mir_cached_unit_len(runtime, js_source, js_source_len,
                                          filename, true, NULL, out_state);
}

Item compile_js_mir_with_preamble_len(Runtime* runtime, const char* js_source,
                                      size_t js_source_len, const char* filename,
                                      const JsPreambleState* preamble,
                                      JsPreambleState* out_state) {
    if (!preamble) return ItemError;
    return compile_js_mir_cached_unit_len(runtime, js_source, js_source_len,
                                          filename, false, preamble, out_state);
}

Item execute_compiled_js_in_current_realm(Runtime* runtime,
                                          const JsPreambleState* base_preamble,
                                          const JsPreambleState* compiled_state,
                                          bool retain_unit_state) {
    if (!runtime || !runtime->heap || !base_preamble || !compiled_state ||
            !compiled_state->entry_func) {
        return ItemError;
    }

    EvalContext* runtime_context = runtime_get_eval_context(runtime);
    if (!runtime_context) return ItemError;
    if (!runtime_context_bind_retained(runtime, runtime_context) ||
            !js_runtime_state_init(runtime_context)) {
        return ItemError;
    }
    RuntimeExecutionScope execution_scope(runtime_context);
    if (runtime->dom_ui_context) js_dom_set_ui_context(runtime->dom_ui_context);
    if (runtime->dom_doc) js_dom_set_document(runtime->dom_doc);
    if (execution_scope.is_outermost() &&
            !js_runtime_state.event_loop.callback_running &&
            js_dynamic_import_suppress_module_drain <= 0) {
        js_event_loop_init();
    }

    uint32_t consumer_state_id = lambda_active_module_state_id();
    RuntimeModuleStateScope module_state(runtime_context);
    uint32_t unit_var_count = compiled_state->module_var_count >
            base_preamble->module_var_count
        ? (uint32_t)compiled_state->module_var_count
        : (uint32_t)base_preamble->module_var_count;
    if (consumer_state_id == UINT32_MAX ||
            !lambda_module_state_reserve_and_activate(unit_var_count)) {
        return ItemError;
    }
    uint32_t unit_state_id = lambda_active_module_state_id();
    // D3.4.4v2: the retained image already contains the inherited prefix plus
    // this unit's local names in the exact order encoded by immutable MIR.
    bool linked = lambda_module_state_link_property_keys(unit_state_id,
            compiled_state->module_property_specs,
            compiled_state->module_property_count,
            compiled_state->module_property_bytes_size) &&
        lambda_module_state_copy_var_prefix(consumer_state_id, unit_state_id,
            (uint32_t)base_preamble->module_var_count);
    if (!linked) {
        return ItemError;
    }

    js_mir_reset_last_phase_timing();
    long execute_start = js_mir_phase_now_us();
    Item result = js_mir_execute_compiled_entry(compiled_state->entry_func);
    g_last_js_mir_phase_timing.execute_us = js_mir_phase_now_us() - execute_start;
    g_last_js_mir_phase_timing.total_us = g_last_js_mir_phase_timing.execute_us;
    js_mir_finish_script_turn(runtime, result);
    // External classics extend one document-local declaration slab. Lifecycle
    // units can instead restore their caller by passing retain_unit_state=false.
    if (retain_unit_state) module_state.retain_active();
    return result;
}

Item transpile_js_to_mir_with_preamble(Runtime* runtime, const char* js_source, const char* filename,
                                        const JsPreambleState* preamble, uint64_t* result_home) {
    return transpile_js_to_mir_with_preamble_len(runtime, js_source, strlen(js_source), filename,
                                                  preamble, result_home);
}

Item transpile_js_to_mir_with_preamble_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                                           const char* filename, const JsPreambleState* preamble,
                                           uint64_t* result_home) {
    bool preamble_available = preamble && context &&
        preamble->module_state_id != UINT32_MAX &&
        preamble->module_state_id < context->module_state_capacity &&
        context->module_states && context->module_states[preamble->module_state_id];
    if (!preamble_available ||
            lambda_active_module_state_id() == preamble->module_state_id) {
        log_error("js-mir: invalid preamble consumer preamble=%u active=%u available=%d",
                  preamble ? preamble->module_state_id : UINT32_MAX,
                  lambda_active_module_state_id(),
                  preamble_available ? 1 : 0);
        return ItemError;
    }
    g_jm_preamble_mode = false;
    g_jm_preamble_out = NULL;
    g_jm_preamble_in = preamble;
    Item result = transpile_js_to_mir_core_len(runtime, js_source, js_source_len, filename,
                                                result_home);
    g_jm_preamble_in = NULL;
    return result;
}

bool clone_js_preamble_state(const JsPreambleState* source, JsPreambleState* out_state) {
    if (!source || !source->entry_func || !source->mir_ctx || !out_state) return false;
    memset(out_state, 0, sizeof(*out_state));
    out_state->mir_ctx = source->mir_ctx;
    out_state->source_buffer = source->source_buffer;
    out_state->entry_func = source->entry_func;
    out_state->module_var_count = source->module_var_count;
    out_state->module_state_id = UINT32_MAX;
    out_state->entry_count = source->entry_count;
    out_state->owns_compiled_state = false;
    if (!js_preamble_entries_copy(source->entries, source->entry_count,
                                  &out_state->entries)) {
        memset(out_state, 0, sizeof(*out_state));
        return false;
    }
    if (source->module_property_count > 0) {
        size_t bytes = source->module_property_bytes_size;
        out_state->module_property_specs = (PropertyKeySpec*)mem_alloc(
            bytes, MEM_CAT_JS_RUNTIME);
        if (!out_state->module_property_specs) {
            js_preamble_entries_free(out_state->entries, out_state->entry_count);
            memset(out_state, 0, sizeof(*out_state));
            return false;
        }
        memcpy(out_state->module_property_specs, source->module_property_specs, bytes);
        out_state->module_property_count = source->module_property_count;
        out_state->module_property_bytes_size = source->module_property_bytes_size;
    }
    return true;
}

Item instantiate_js_preamble(Runtime* runtime, const JsPreambleState* cached,
                             JsPreambleState* out_state) {
    if (!runtime || !clone_js_preamble_state(cached, out_state)) return ItemError;

    if (context && context->heap) {
        // Cached code is only safe when instantiated into a new document heap.
        preamble_state_destroy(out_state);
        return ItemError;
    }

    EvalContext* js_context = runtime_get_eval_context(runtime);
    if (!js_context) {
        preamble_state_destroy(out_state);
        return ItemError;
    }
    if (!eval_context_init(js_context)) {
        preamble_state_destroy(out_state);
        return ItemError;
    }
    RuntimeExecutionScope execution_scope(js_context);
    context->runtime = runtime;
    if (!js_runtime_state_init(context)) {
        preamble_state_destroy(out_state);
        return ItemError;
    }
    if (runtime->dom_ui_context) js_dom_set_ui_context(runtime->dom_ui_context);
    heap_init();
    if (!context->heap) {
        preamble_state_destroy(out_state);
        js_mir_destroy_unowned_eval_context(runtime, js_context, false);
        return ItemError;
    }
    context->pool = context->heap->pool;
    context->name_pool = name_pool_create_runtime(context->pool);
    context->type_list = arraylist_new(64);
    if (!context->name_pool || !context->type_list) {
        preamble_state_destroy(out_state);
        js_mir_destroy_unowned_eval_context(runtime, js_context, false);
        return ItemError;
    }
    runtime_context_publish_owners(runtime, context);

    // Realm construction can compile inline DOM handlers before js_main runs.
    // Publish the preamble slab first so those native compilation callbacks
    // never observe a context with no active module state.
    if (!lambda_module_state_reserve_and_activate(
            (uint32_t)cached->module_var_count)) {
        preamble_state_destroy(out_state);
        return ItemError;
    }
    if (!lambda_module_state_link_property_keys(lambda_active_module_state_id(),
            cached->module_property_specs, cached->module_property_count,
            cached->module_property_bytes_size)) {
        preamble_state_destroy(out_state);
        return ItemError;
    }
    out_state->module_state_id = lambda_active_module_state_id();

    // js262 restores a value checkpoint because its harness heap survives.
    // This heap is new: clear all process caches, then retain only the compiled
    // declaration count so js_main initializes fresh module values.
    js_batch_reset();
    js_prepare_compiled_preamble_vars(cached->module_var_count);
    Input* js_input_context = Input::create(context->pool);
    js_runtime_set_input(js_input_context);
    // Cached MIR is immutable, but its globals are document-local. Recreate
    // globalThis after installing this realm's Input so document bindings do
    // not resolve through the prior batch document's discarded global object.
    (void)js_get_global_this();
    if (execution_scope.is_outermost() &&
            !js_runtime_state.event_loop.callback_running) {
        js_event_loop_init();
    }
    if (runtime->dom_doc) js_dom_set_document(runtime->dom_doc);

    JsMirMainFunc js_main = (JsMirMainFunc)cached->entry_func;
    js_mir_reset_last_phase_timing();
    long execute_start = js_mir_phase_now_us();
    Item result = js_mir_execute_compiled_entry((void*)js_main);
    g_last_js_mir_phase_timing.execute_us = js_mir_phase_now_us() - execute_start;
    g_last_js_mir_phase_timing.total_us = g_last_js_mir_phase_timing.execute_us;
    js_microtask_flush();

    return result;
}

static bool preamble_state_replace_entries(JsPreambleState* state,
                                           const JsModuleConstEntry* source,
                                           int count, int module_var_count,
                                           const char* source_name) {
    if (!state || count < 0 || (count > 0 && !source)) return false;
    JsModuleConstEntry* entries = NULL;
    if (!js_preamble_entries_copy(source, count, &entries)) {
        log_error("js-mir-preamble-refresh: failed to copy %s declaration snapshot",
                  source_name ? source_name : "unknown");
        return false;
    }

    js_preamble_entries_free(state->entries, state->entry_count);
    state->entries = entries;
    state->entry_count = count;
    state->module_var_count = module_var_count >= state->module_var_count
        ? module_var_count
        : state->module_var_count;
    log_debug("js-mir-preamble-refresh: source=%s entries=%d module_vars=%d",
        source_name ? source_name : "unknown", state->entry_count,
        state->module_var_count);
    return true;
}

bool preamble_state_update_from_eval_snapshot(JsPreambleState* state) {
    if (!g_eval_preamble_entries || g_eval_preamble_entry_count <= 0) return false;
    return preamble_state_replace_entries(
        state, g_eval_preamble_entries, g_eval_preamble_entry_count,
        g_eval_preamble_var_count, "eval");
}

bool preamble_state_update_from_compiled(JsPreambleState* state,
                                         const JsPreambleState* compiled_state) {
    if (!compiled_state) return false;
    // D1.7/D1.8: only immutable code is shared; declaration metadata is copied
    // into the current document so later classic scripts see this unit's vars.
    return preamble_state_replace_entries(
        state, compiled_state->entries, compiled_state->entry_count,
        compiled_state->module_var_count, "compiled");
}

void preamble_state_destroy(JsPreambleState* state) {
    if (!state) return;
    if (state->owns_compiled_state && state->mir_ctx) {
        MIR_finish((MIR_context_t)state->mir_ctx);
        state->mir_ctx = NULL;
    }
    if (state->owns_compiled_state && state->source_buffer) {
        mem_free(state->source_buffer);
        state->source_buffer = NULL;
    }
    js_preamble_entries_free(state->entries, state->entry_count);
    state->entries = NULL;
    state->entry_count = 0;
    if (state->module_property_specs) {
        mem_free(state->module_property_specs);
        state->module_property_specs = NULL;
    }
    state->module_property_count = 0;
    state->module_property_bytes_size = 0;
    state->module_var_count = 0;
    state->module_state_id = UINT32_MAX;
    state->entry_func = NULL;
    state->owns_compiled_state = false;
}

// ============================================================================
// Public API: load a JS file as a module for cross-language import
// ============================================================================

Item load_js_module(Runtime* runtime, const char* js_path) {
    log_info("js-mir: loading JS module '%s' for cross-language import", js_path);
    if (runtime) runtime->js_runtime_used = true;
    char* source = read_text_file(js_path);
    if (!source) {
        log_error("js-mir: cannot read JS file '%s'", js_path);
        return ItemNull;
    }
    // transpile_js_module_to_mir assumes a heap context is already active
    // (normally provided by transpile_js_to_mir). When called from build_ast
    // during Lambda→JS import, no context exists yet. Set up a persistent one.
    if (!context || !context->heap) {
        EvalContext* js_context = runtime_get_eval_context(runtime);
        if (!js_context) {
            jm_clear_active_js_transpile(NULL, NULL, source);
            mem_free(source);
            return ItemNull;
        }
        if (!eval_context_init(js_context)) {
            jm_clear_active_js_transpile(NULL, NULL, source);
            mem_free(source);
            return ItemNull;
        }
        heap_init();
        context->pool = context->heap->pool;
        context->name_pool = name_pool_create_runtime(context->pool);
        context->type_list = arraylist_new(64);
        context->runtime = runtime;
        if (!js_runtime_state_init(context)) {
            jm_clear_active_js_transpile(NULL, NULL, source);
            mem_free(source);
            return ItemNull;
        }

        // The canonical context owns the bootstrap heap through runner setup.
        runtime_context_publish_owners(runtime, context);
        runtime->js_bootstrap_context = context;

        // Create Input context for JS runtime
        Input* js_input = Input::create(context->pool);
        js_runtime_set_input(js_input);
        log_debug("js-mir: created persistent heap for cross-language module loading");
    }

    jm_track_active_js_transpile(NULL, NULL, source);
    Item ns = transpile_js_module_to_mir(runtime, source, js_path);
    jm_clear_active_js_transpile(NULL, NULL, source);
    mem_free(source);
    return ns;
}

// ============================================================================
// CJS require() — runtime function called from JIT code
// ============================================================================

bool js_is_cjs_file(const char* path) {
    size_t len = strlen(path);
    if (len >= 4 && strcmp(path + len - 4, ".cjs") == 0) return true;
    if (len >= 4 && strcmp(path + len - 4, ".mjs") == 0) return false;
    // For .js files loaded via require(), treat as CJS (Node.js behavior)
    return true;
}

static bool js_require_path_has_known_extension(const char* path) {
    int len = path ? (int)strlen(path) : 0;
    return (len >= 3 && strcmp(path + len - 3, ".js") == 0) ||
           (len >= 4 && strcmp(path + len - 4, ".mjs") == 0) ||
           (len >= 4 && strcmp(path + len - 4, ".cjs") == 0) ||
           (len >= 5 && strcmp(path + len - 5, ".json") == 0) ||
           (len >= 3 && strcmp(path + len - 3, ".ls") == 0);
}

static bool js_require_path_is_json(const char* path) {
    int len = path ? (int)strlen(path) : 0;
    return len >= 5 && strcmp(path + len - 5, ".json") == 0;
}

static char* js_require_read_resolved_path_internal(char* path_buf, int path_buf_size,
        bool allow_package_main);

static void js_require_normalize_lexical_path(char* path_buf, int path_buf_size) {
    if (!path_buf || path_buf_size <= 0) return;

    js_normalize_path_separators(path_buf);
    char normalized[512];
    path_str_normalize_lexical_posix(path_buf, normalized, sizeof(normalized), false);
    snprintf(path_buf, path_buf_size, "%s", normalized);
}

static void js_require_canonicalize_existing_path(char* path_buf, int path_buf_size) {
    if (!path_buf || path_buf_size <= 0) return;
    char resolved[512];
    if (!realpath(path_buf, resolved)) return;
    js_normalize_path_separators(resolved);
    if ((int)strlen(resolved) >= path_buf_size) return;
    snprintf(path_buf, path_buf_size, "%s", resolved);
}

static char* js_require_read_package_main(char* path_buf, int path_buf_size,
        const char* dir_path) {
    if (!dir_path || !dir_path[0]) return NULL;

    char canonical_dir[512];
    const char* package_dir = dir_path;
    if (realpath(dir_path, canonical_dir)) {
        js_normalize_path_separators(canonical_dir);
        package_dir = canonical_dir;
    }

    char package_path[512];
    int dir_len = (int)strlen(package_dir);
    if (dir_len + 14 >= (int)sizeof(package_path)) return NULL;
    snprintf(package_path, sizeof(package_path), "%s/package.json", package_dir);

    char* package_source = read_text_file(package_path);
    if (!package_source) return NULL;

    Item package_text = js_name_item(package_source, strlen(package_source));
    Item package_obj = js_json_parse(package_text);
    mem_free(package_source);
    if (item_is_error(package_obj)) return NULL;

    Item main_value = js_get_name_key(package_obj, "main", 4);
    if (item_is_error(main_value) || get_type_id(main_value) != LMD_TYPE_STRING) return NULL;

    String* main_str = it2s(main_value);
    if (!main_str || main_str->len <= 0) return NULL;

    char main_path[512];
    if (main_str->chars[0] == '/') {
        if (main_str->len >= (int64_t)sizeof(main_path)) return NULL;
        snprintf(main_path, sizeof(main_path), "%.*s", (int)main_str->len, main_str->chars);
    } else {
        if (dir_len + 1 + main_str->len >= (int64_t)sizeof(main_path)) return NULL;
        snprintf(main_path, sizeof(main_path), "%s/%.*s", package_dir, (int)main_str->len, main_str->chars);
    }

    char resolved_main[512];
    snprintf(resolved_main, sizeof(resolved_main), "%s", main_path);
    char* source = js_require_read_resolved_path_internal(resolved_main, (int)sizeof(resolved_main), false);
    if (!source) return NULL;
    if ((int)strlen(resolved_main) >= path_buf_size) {
        mem_free(source);
        return NULL;
    }
    snprintf(path_buf, path_buf_size, "%s", resolved_main);
    return source;
}

static char* js_require_read_resolved_path_internal(char* path_buf, int path_buf_size,
        bool allow_package_main) {
    js_require_normalize_lexical_path(path_buf, path_buf_size);
    char original[512];
    snprintf(original, sizeof(original), "%s", path_buf);

    char* source = read_text_file(path_buf);
    if (source) {
        js_require_canonicalize_existing_path(path_buf, path_buf_size);
        return source;
    }

    int len = (int)strlen(original);
    bool has_node_prefix = (len >= 5 && strncmp(original, "node:", 5) == 0);
    if (!has_node_prefix && !js_require_path_has_known_extension(original) &&
            len + 3 < path_buf_size) {
        snprintf(path_buf, path_buf_size, "%s.js", original);
        source = read_text_file(path_buf);
        if (source) {
            js_require_canonicalize_existing_path(path_buf, path_buf_size);
            return source;
        }
    }

    snprintf(path_buf, path_buf_size, "%s", original);
    size_t plen = strlen(path_buf);
    if (plen >= 3 && strcmp(path_buf + plen - 3, ".js") == 0) {
        path_buf[plen - 3] = '\0';
        plen -= 3;
    }
    if (!has_node_prefix && allow_package_main) {
        source = js_require_read_package_main(path_buf, path_buf_size, path_buf);
        if (source) return source;
    }
    if (plen + strlen("/index.js") < (size_t)path_buf_size) {
        strncat(path_buf, "/index.js", path_buf_size - strlen(path_buf) - 1);
        source = read_text_file(path_buf);
        if (source) {
            js_require_canonicalize_existing_path(path_buf, path_buf_size);
            return source;
        }
    }

    snprintf(path_buf, path_buf_size, "%s", original);
    return NULL;
}

JS_FORWARD_STATIC_RETURN(char*, js_require_read_resolved_path,
    (char* path_buf, int path_buf_size), js_require_read_resolved_path_internal,
    (path_buf, path_buf_size, true))

static Item js_cjs_key(const char* name, int len) {
    return js_name_item(name, len);
}

static JsCjsState* js_cjs_state_current(bool attach) {
    if (attach) jube_modules_runtime_attach();
    void* session = jube_node_runtime_current_session();
    return session ? jube_node_cjs_state(session) : NULL;
}

#define js_cjs_module_stack_state (js_cjs_state_current(true)->module_stack)
#define js_cjs_module_stack (js_cjs_module_stack_state.roots.slots)
#define js_cjs_module_stack_count (js_cjs_module_stack_state.depth)

extern "C" void js_cjs_metadata_reset(void) {
    JsCjsState* state = js_cjs_state_current(false);
    if (state) js_item_stack_clear(&state->module_stack);
}

static Item js_cjs_current_module(void) {
    if (js_cjs_module_stack_count <= 0) return ItemNull;
    return js_cjs_module_stack[js_cjs_module_stack_count - 1];
}

static Item js_cjs_find_module(Item filename) {
    if (get_type_id(filename) != LMD_TYPE_STRING) return ItemNull;
    String* spec = it2s(filename);
    ModuleDescriptor* desc = module_get_for_runtime(
        context ? context->runtime : NULL, spec ? spec->chars : NULL);
    if (desc && desc->source_lang && strcmp(desc->source_lang, "js-cjs") == 0) {
        return desc->namespace_obj;
    }
    return ItemNull;
}

static void js_cjs_store_module(Item filename, Item module) {
    if (get_type_id(filename) != LMD_TYPE_STRING) return;
    String* spec = it2s(filename);
    if (!spec) return;
    ModuleDescriptor* existing = module_get_for_runtime(
        context ? context->runtime : NULL, spec->chars);
    if (existing && existing->source_lang &&
            strcmp(existing->source_lang, "js-cjs") != 0) return;
    module_register_for_runtime(context ? context->runtime : NULL,
        spec->chars, "js-cjs", module, NULL);
}

static Item js_cjs_exports(Item module) {
    Item exports_key = js_cjs_key("exports", (int)strlen("exports"));
    Item exports = js_get_key_default(module, exports_key);
    if (get_type_id(exports) == LMD_TYPE_NULL || get_type_id(exports) == LMD_TYPE_UNDEFINED) {
        exports = js_new_object();
        js_set_key_default(module, exports_key, exports);
    }
    return exports;
}

static Item js_cjs_children(Item module) {
    Item children_key = js_cjs_key("children", (int)strlen("children"));
    Item children = js_get_key_default(module, children_key);
    if (get_type_id(children) != LMD_TYPE_ARRAY) {
        children = js_array_new(0);
        js_set_key_default(module, children_key, children);
    }
    return children;
}

static void js_cjs_update_cached_default(Item filename, Item module) {
    Item ns = js_module_get(filename);
    if (get_type_id(ns) != LMD_TYPE_MAP && get_type_id(ns) != LMD_TYPE_OBJECT) return;
    js_set_key_default(ns, js_cjs_key("default", (int)strlen("default")), js_cjs_exports(module));
}

// all loader clients project the same cached namespace to its CJS default;
// keeping that projection here prevents raw and canonical require probes from
// drifting apart.
static Item js_cjs_cached_value(Item specifier) {
    Item namespace_item = js_module_get(specifier);
    if (get_type_id(namespace_item) == LMD_TYPE_NULL) return ItemNull;
    Item default_value = js_get_key_default(namespace_item,
        js_cjs_key("default", (int)strlen("default")));
    TypeId type = get_type_id(default_value);
    return type != LMD_TYPE_NULL && type != LMD_TYPE_UNDEFINED
        ? default_value : namespace_item;
}

extern "C" Item js_cjs_enter(Item module, Item filename) {
    if (!js_cjs_state_current(true)) return (Item){.item = ITEM_JS_UNDEFINED};
    if (!js_root_range_ensure_registered(&js_cjs_module_stack_state.roots)) {
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (get_type_id(module) != LMD_TYPE_MAP && get_type_id(module) != LMD_TYPE_OBJECT) {
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    js_set_key_default(module, js_cjs_key("id", (int)strlen("id")), filename);
    js_set_key_default(module, js_cjs_key("filename", (int)strlen("filename")), filename);
    js_set_key_default(module, js_cjs_key("loaded", (int)strlen("loaded")), (Item){.item = ITEM_FALSE});
    js_cjs_exports(module);
    js_cjs_children(module);
    Item parent = js_cjs_current_module();
    js_set_key_default(module, js_cjs_key("parent", (int)strlen("parent")), parent);
    if (get_type_id(filename) == LMD_TYPE_STRING) {
        js_cjs_store_module(filename, module);
        js_cjs_update_cached_default(filename, module);
    }
    if (js_cjs_module_stack_count < JS_CJS_STACK_MAX) {
        js_item_stack_push(&js_cjs_module_stack_state, module);
    } else {
        log_error("cjs-metadata: module stack overflow (%d)", JS_CJS_STACK_MAX);
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_cjs_complete(Item module) {
    if (get_type_id(module) == LMD_TYPE_MAP || get_type_id(module) == LMD_TYPE_OBJECT) {
        js_set_key_default(module, js_cjs_key("loaded", (int)strlen("loaded")), (Item){.item = ITEM_TRUE});
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_cjs_leave(Item module) {
    if (js_cjs_module_stack_count > 0) {
        if (module.item == ItemNull.item ||
            js_cjs_module_stack[js_cjs_module_stack_count - 1].item == module.item) {
            js_item_stack_pop(&js_cjs_module_stack_state);
        } else {
            for (int i = js_cjs_module_stack_count - 1; i >= 0; i--) {
                if (js_cjs_module_stack[i].item != module.item) continue;
                for (int j = i + 1; j < js_cjs_module_stack_count; j++) {
                    js_cjs_module_stack[j - 1] = js_cjs_module_stack[j];
                }
                js_item_stack_shrink(&js_cjs_module_stack_state,
                                     js_cjs_module_stack_count - 1);
                break;
            }
        }
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item js_cjs_create_module_metadata(Item child_filename, Item exports) {
    Item module = js_new_object();
    js_set_key_default(module, js_cjs_key("id", (int)strlen("id")), child_filename);
    js_set_key_default(module, js_cjs_key("filename", (int)strlen("filename")), child_filename);
    js_set_key_default(module, js_cjs_key("exports", (int)strlen("exports")), exports);
    js_set_key_default(module, js_cjs_key("loaded", (int)strlen("loaded")), (Item){.item = ITEM_TRUE});
    js_set_key_default(module, js_cjs_key("children", (int)strlen("children")), js_array_new(0));
    js_set_key_default(module, js_cjs_key("parent", (int)strlen("parent")), ItemNull);
    js_cjs_store_module(child_filename, module);
    return module;
}

static bool js_cjs_specifier_is_file_path(Item specifier) {
    if (get_type_id(specifier) != LMD_TYPE_STRING) return false;
    String* spec = it2s(specifier);
    if (!spec || spec->len <= 0) return false;
    bool has_slash = false;
    for (int64_t i = 0; i < spec->len; i++) {
        if (spec->chars[i] == '/') {
            has_slash = true;
            break;
        }
    }
    if (!has_slash) return false;
    if (spec->len >= 4 && memcmp(spec->chars + spec->len - 4, ".mjs", 4) == 0) return false;
    if (spec->len >= 4 && memcmp(spec->chars + spec->len - 4, ".cjs", 4) == 0) return true;
    if (spec->len >= 3 && memcmp(spec->chars + spec->len - 3, ".js", 3) == 0) return true;
    return false;
}

static void js_cjs_note_child(Item child_filename, Item child_exports) {
    Item parent = js_cjs_current_module();
    if (get_type_id(parent) != LMD_TYPE_MAP && get_type_id(parent) != LMD_TYPE_OBJECT) return;
    Item child = js_cjs_find_module(child_filename);
    if (get_type_id(child) != LMD_TYPE_MAP && get_type_id(child) != LMD_TYPE_OBJECT) {
        child = js_cjs_create_module_metadata(child_filename, child_exports);
    }
    if (get_type_id(child) != LMD_TYPE_MAP && get_type_id(child) != LMD_TYPE_OBJECT) return;
    Item children = js_cjs_children(parent);
    int64_t len = js_array_length(children);
    for (int64_t i = 0; i < len; i++) {
        Item existing = js_elements_get_int(children, i);
        if (existing.item == child.item) return;
    }
    js_array_push(children, child);
}

static char* js_wrap_cjs_source_with_suffix(const char* source,
        const char* filename, const char* suffix) {
    char filename_buf[2048];
    snprintf(filename_buf, sizeof(filename_buf), "%s", filename);
    js_normalize_path_separators(filename_buf);

    // Extract __dirname from filename
    const char* last_slash = strrchr(filename_buf, '/');
    int dir_len = last_slash ? (int)(last_slash - filename_buf) : 1;
    const char* dir_str = last_slash ? filename_buf : ".";

    // Wrap:  var __cjs_module__ = {exports: {}};
    //        var exports = __cjs_module__.exports;
    //        var module = __cjs_module__;
    //        var __filename = "..."; var __dirname = "...";
    //        <original source>
    //        <caller-provided module completion>
    const char* prefix_fmt =
        "var __cjs_module__ = {exports: {}};\n"
        "var exports = __cjs_module__.exports;\n"
        "var module = __cjs_module__;\n"
        "var __filename = \"%s\";\n"
        "var __dirname = \"%.*s\";\n"
        "__lambda_cjs_enter(__cjs_module__, __filename);\n";
    size_t src_len = strlen(source);
    size_t prefix_size = strlen(prefix_fmt) + strlen(filename_buf) + dir_len + 64;
    size_t total = prefix_size + src_len + strlen(suffix) + 1;

    char* wrapped = (char*)mem_alloc(total, MEM_CAT_JS_RUNTIME);
    int offset = snprintf(wrapped, total, prefix_fmt, filename_buf, dir_len, dir_str);
    memcpy(wrapped + offset, source, src_len);
    offset += (int)src_len;
    snprintf(wrapped + offset, total - (size_t)offset, "%s", suffix);
    return wrapped;
}

char* js_wrap_cjs_source(const char* source, const char* filename) {
    return js_wrap_cjs_source_with_suffix(source, filename,
        "\n__lambda_cjs_complete(__cjs_module__);\n"
        "__lambda_cjs_leave(__cjs_module__);\n"
        "export default __cjs_module__.exports;\n");
}

static char* js_wrap_cjs_source_for_ast(const char* source, const char* filename) {
    // The AST backend returns the completed exports expression directly; it
    // must not inject ESM syntax merely to project CommonJS's default value.
    return js_wrap_cjs_source_with_suffix(source, filename,
        "\n__lambda_cjs_complete(__cjs_module__);\n"
        "__lambda_cjs_leave(__cjs_module__);\n"
        "__cjs_module__.exports;\n");
}

extern "C" Item js_require(Item specifier) {
    if (get_type_id(specifier) != LMD_TYPE_STRING) {
        log_error("require: specifier is not a string");
        return ItemNull;
    }
    String* spec = it2s(specifier);
    if (!spec || spec->len == 0) return ItemNull;

    // CJS require resolves native built-ins before disk lookup; built-in-only
    // modules like dgram otherwise fall through as missing dgram.js files.
    Item builtin = js_module_get_builtin(specifier);
    if (get_type_id(builtin) != LMD_TYPE_NULL) return builtin;

    // Check if already loaded in module cache
    Item existing = js_cjs_cached_value(specifier);
    if (get_type_id(existing) != LMD_TYPE_NULL) {
        if (js_cjs_specifier_is_file_path(specifier)) js_cjs_note_child(specifier, existing);
        return existing;
    }

    char path_buf[512];
    snprintf(path_buf, sizeof(path_buf), "%.*s", (int)spec->len, spec->chars);
    char requested_specifier[sizeof(path_buf)];
    snprintf(requested_specifier, sizeof(requested_specifier), "%s", path_buf);

    // Read the source file using Node-style file and directory fallbacks.
    char* source = js_require_read_resolved_path(path_buf, (int)sizeof(path_buf));
    if (!source) {
        log_error("require: cannot read module '%s'", path_buf);
        // For internal/* modules, return empty object to prevent destructure crashes
        if (strncmp(path_buf, "internal/", 9) == 0 || 
            (spec->len > 9 && memcmp(spec->chars, "internal/", 9) == 0)) {
            return js_new_object();
        }
        return js_require_module_not_found(requested_specifier);
    }
    jm_track_active_js_transpile(NULL, NULL, source);

    Item resolved_spec = js_name_item(path_buf, strlen(path_buf));
    existing = js_cjs_cached_value(resolved_spec);
    if (get_type_id(existing) != LMD_TYPE_NULL) {
        jm_clear_active_js_transpile(NULL, NULL, source);
        mem_free(source);
        if (js_is_cjs_file(path_buf)) js_cjs_note_child(resolved_spec, existing);
        return existing;
    }

    if (js_require_path_is_json(path_buf)) {
        Item json_text = js_name_item(source, strlen(source));
        Item parsed = js_json_parse(json_text);
        mem_free(source);
        if (item_is_error(parsed)) return parsed;
        js_module_register(resolved_spec, parsed);
        js_cjs_note_child(resolved_spec, parsed);
        return parsed;
    }

    Runtime* runtime = js_current_runtime();
    if (!runtime) {
        log_error("require: no runtime available");
        jm_clear_active_js_transpile(NULL, NULL, source);
        mem_free(source);
        return ItemNull;
    }

    Item ns;
    if (js_is_cjs_file(path_buf)) {
        // Wrap CJS source with module/exports globals
        char* wrapped = js_ast_interpreter_requested()
            ? js_wrap_cjs_source_for_ast(source, path_buf)
            : js_wrap_cjs_source(source, path_buf);
        jm_clear_active_js_transpile(NULL, NULL, source);
        mem_free(source);
        jm_track_active_js_transpile(NULL, NULL, wrapped);
        ns = js_ast_interpreter_requested()
            ? js_interp_execute_module_source(runtime, wrapped, strlen(wrapped),
                path_buf, false, NULL)
            : transpile_js_module_to_mir(runtime, wrapped, path_buf);
        if (item_is_error(ns)) {
            // A synthetic JS try block changes a CJS file's top-level lexical
            // bindings into block bindings; close metadata here on abrupt exit
            // so the original source retains its Node/CommonJS lexical scope.
            js_cjs_leave(ItemNull);
        }
        jm_clear_active_js_transpile(NULL, NULL, wrapped);
        mem_free(wrapped);
        if (!item_is_error(ns)) {
            Item module = js_cjs_find_module(resolved_spec);
            js_cjs_update_cached_default(resolved_spec, module);
        }
    } else {
        // ESM modules use the same retained AST owner and registry under the
        // forced backend; the default preserves the established MIR path.
        ns = js_ast_interpreter_requested()
            ? js_interp_execute_es_module_source(runtime, source, strlen(source),
                path_buf, NULL)
            : transpile_js_module_to_mir(runtime, source, path_buf);
        jm_clear_active_js_transpile(NULL, NULL, source);
        mem_free(source);
    }

    if (get_type_id(ns) == LMD_TYPE_NULL) {
        log_error("require: failed to compile module '%s'", path_buf);
        return ItemNull;
    }

    // For CJS, extract the default export (module.exports)
    if (js_is_cjs_file(path_buf)) {
        Item result = js_cjs_cached_value(resolved_spec);
        if (get_type_id(result) != LMD_TYPE_NULL) {
            js_cjs_note_child(resolved_spec, result);
            return result;
        }
    }

    return ns;
}

static Item js_dynamic_import_reject_type_error(const char* message) {
    Item error_name = js_name_item("TypeError", 9);
    Item error_message = js_name_item(message, (int)strlen(message));
    return js_promise_reject(js_new_error_with_name(error_name, error_message));
}

// dynamic import() — synchronous load, wrapped in a resolved Promise
extern "C" Item js_dynamic_import(Item specifier) {
    RootFrame roots(5);
    Rooted<Item> specifier_string_root(roots, js_to_string(specifier));
    if (item_is_error(specifier_string_root.get()) ||
            get_type_id(specifier_string_root.get()) != LMD_TYPE_STRING) {
        return js_promise_reject(specifier_string_root.get());
    }
    String* spec = it2s(specifier_string_root.get());
    if (!spec || spec->len == 0) {
        return js_dynamic_import_reject_type_error("import() requires a non-empty specifier");
    }

    char resolved_path[2048];
    const char* base_file = context ? context->current_file : NULL;
    if (base_file && base_file[0] && base_file[0] != '<') {
        jm_resolve_module_path(base_file, spec->chars, (int)spec->len,
                               resolved_path, (int)sizeof(resolved_path));
    } else {
        snprintf(resolved_path, sizeof(resolved_path), "%.*s",
                 (int)spec->len, spec->chars);
    }
    Rooted<Item> resolved_spec_root(roots, make_string_item(resolved_path));

    // Js56 P10: dynamic `import(...)` is ES-module-only — CommonJS uses
    // `require()`. The shared js_require() path treats unmarked .js files as
    // CJS by default and extracts the CJS `default` export from the loaded
    // namespace; for ESM dynamic imports that strips the entire namespace
    // (`export var x = 42` ends up not on the returned object). Resolve
    // through the module cache and the ESM transpile path directly here so
    // the dynamic import returns the real namespace.
    js_dynamic_import_suppress_module_drain++;
    Item ns;
    Item existing = js_module_get(resolved_spec_root.get());
    if (get_type_id(existing) != LMD_TYPE_NULL) {
        ns = existing;
    } else {
        char path_buf[2048];
        snprintf(path_buf, sizeof(path_buf), "%s", resolved_path);
        char* source = read_text_file(path_buf);
        if (!source) {
            js_dynamic_import_suppress_module_drain--;
            char msg[256];
            snprintf(msg, sizeof(msg), "Cannot find module '%s'", resolved_path);
            return js_dynamic_import_reject_type_error(msg);
        }
        jm_track_active_js_transpile(NULL, NULL, source);
        Runtime* runtime = js_current_runtime();
        if (!runtime) {
            jm_clear_active_js_transpile(NULL, NULL, source);
            mem_free(source);
            js_dynamic_import_suppress_module_drain--;
            return js_dynamic_import_reject_type_error("import(): no runtime available");
        }
        ns = transpile_js_module_to_mir(runtime, source, path_buf);
        jm_clear_active_js_transpile(NULL, NULL, source);
        mem_free(source);
    }
    js_dynamic_import_suppress_module_drain--;
    Rooted<Item> namespace_root(roots, ns);
    if (js_module_needs_async_settle(resolved_spec_root.get())) {
        js_tla_flush_for_dynamic_import();
    }
    if (item_is_error(ns)) {
        return js_promise_reject(ns);
    }
    if (get_type_id(ns) == LMD_TYPE_NULL) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot find module '%s'", resolved_path);
        return js_dynamic_import_reject_type_error(msg);
    }

    // Js57 P5: if the imported module (or any of its static dependencies)
    // had a pending TLA target captured, return a Promise chained on that
    // target so dynamic-import .then/.finally callbacks fire in spec order
    // (importing modules' callbacks fire after the underlying TLA settles).
    extern Item js_p5_chain_dynamic_import(Item, Item);
    Item awaited = js_module_get_awaited_target(resolved_spec_root.get());
    if (get_type_id(awaited) == LMD_TYPE_NULL &&
            js_module_needs_async_settle(resolved_spec_root.get())) {
        // A nested module can suspend at a non-Promise await (for example
        // `await 1`) and therefore has no awaited target to chain.  The
        // importer is still required to see the module only after its deferred
        // post-await body publishes exports; finish that local continuation
        // before resolving the dynamic-import Promise (ECMA-262
        // ContinueDynamicImport).
        js_tla_drain_pending_modules();
        awaited = js_module_get_awaited_target(resolved_spec_root.get());
    }
    if (get_type_id(awaited) != LMD_TYPE_NULL) {
        return js_p5_chain_dynamic_import(awaited, namespace_root.get());
    }
    // Wrap the namespace in a resolved Promise
    return js_promise_resolve(namespace_root.get());
}
