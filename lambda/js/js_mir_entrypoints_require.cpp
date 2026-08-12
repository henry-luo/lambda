#include "js_mir_internal.hpp"
#include "js_runtime_state.hpp"
#undef js_input
#include "../runtime/lambda-error.h"
#include "../runtime/recovery_frame.h"
#include "../runtime/mir_dump.h"
#include "../../lib/mem_factory.h"
#include "../../lib/path_str.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#define JS_MIR_VOL_STATS_MKDIR(path) mkdir(path, 0755)
#define JS_MIR_VOL_STATS_OPEN(path) open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644)
#define JS_MIR_VOL_STATS_WRITE(fd, buf, len) write(fd, buf, len)
#define JS_MIR_VOL_STATS_CLOSE(fd) close(fd)
#define JS_MIR_VOL_STATS_PID() getpid()
#else
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#include <windows.h>
#define JS_MIR_VOL_STATS_MKDIR(path) _mkdir(path)
#define JS_MIR_VOL_STATS_OPEN(path) _open(path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE)
#define JS_MIR_VOL_STATS_WRITE(fd, buf, len) _write(fd, buf, (unsigned int)(len))
#define JS_MIR_VOL_STATS_CLOSE(fd) _close(fd)
#define JS_MIR_VOL_STATS_PID() _getpid()
#endif

int js_dynamic_import_suppress_module_drain = 0;
extern "C" int js_batch_execution_mode = 0;
extern "C" int js_process_current_exit_code(void);
extern "C" void js_async_hooks_drain_destroy_queue(void);
extern "C" void js_trace_flush(void);
extern "C" Item js_module_get_builtin(Item specifier);

static Item js_require_module_not_found(const char* specifier) {
    const char* name = specifier ? specifier : "";
    char message[640];
    snprintf(message, sizeof(message), "Cannot find module '%s'", name);
    Item error = js_new_error_with_name(make_string_item("Error"), make_string_item(message));
    js_set_key_default(error, make_string_item("code"), make_string_item("MODULE_NOT_FOUND"));
    // the returned error must stay attached to the call result; there is no
    // pending side channel for require callers to recover.
    return js_throw_value(error);
}

static JsMirPhaseTiming g_last_js_mir_phase_timing;
static JsMirPhaseTiming g_document_js_mir_phase_timing;
static bool g_document_js_mir_phase_timing_active = false;

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
    if (!context || !context->active_js_module_state) return false;
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
        uint32_t active_names = js_active_module_name_count();
        uint32_t active_ics = js_active_module_ic_count();
        if (active_names == 0 && inherited > 0 &&
                !lambda_module_state_link_property_keys(
                    context->active_js_module_state->module_id,
                    inherited_specs, inherited, inherited_bytes)) {
            return false;
        }
        uint32_t inherited_ic = g_jm_preamble_in->ic_count;
        if (active_ics == 0 && inherited_ic > 0 &&
                !js_link_module_ic_table(
                    context->active_js_module_state->module_id, inherited_ic)) {
            return false;
        }
        if (js_active_module_name_count() < inherited ||
                js_active_module_ic_count() < inherited_ic) {
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
        context->active_js_module_state->module_id, image, image_count,
        image_bytes);
    mem_free(image);
    if (!linked) return false;
    uint32_t inherited_ic = inherits_preamble ? g_jm_preamble_in->ic_count : 0;
    uint32_t local_ic = mt ? mt->ic_count : 0;
    return js_link_module_ic_table(
        context->active_js_module_state->module_id, inherited_ic + local_ic);
}

static uint32_t js_preamble_consumer_name_base(const JsPreambleState* preamble) {
    uint32_t base = preamble ? preamble->module_property_count : 0;
    if (!g_jm_preamble_compile_only) {
        uint32_t active = js_active_module_name_count();
        if (active > base) base = active;
    }
    return base;
}

static uint32_t js_preamble_consumer_ic_base(const JsPreambleState* preamble) {
    uint32_t base = preamble ? preamble->ic_count : 0;
    if (!g_jm_preamble_compile_only) {
        uint32_t active = js_active_module_ic_count();
        if (active > base) base = active;
    }
    return base;
}

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
    if (!context || !context->active_js_module_state) return false;
    PropertyKeySpec* image = NULL;
    uint32_t count = 0;
    uint32_t bytes = 0;
    if (!jm_build_property_key_image(NULL, 0, 0,
            mt ? mt->module_name_specs : NULL, &image, &count, &bytes)) {
        return false;
    }
    bool linked = lambda_module_state_append_property_keys(
        context->active_js_module_state->module_id, image, count, bytes);
    if (linked && mt && mt->ic_count > 0) {
        linked = js_append_module_ic_table(
            context->active_js_module_state->module_id, mt->ic_count);
    }
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
    uint32_t inherited_ic = g_jm_preamble_in ? g_jm_preamble_in->ic_count : 0;
    state->ic_count = inherited_ic + (mt ? mt->ic_count : 0);
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

static bool js_mir_phase_progress_is_enabled(void) {
    const char* value = getenv("LAMBDA_JS_MIR_PHASE_PROGRESS");
    return value && value[0] && strcmp(value, "0") != 0;
}

static void js_mir_log_phase_progress(const char* filename, const char* phase, long elapsed_us) {
    if (!js_mir_phase_progress_is_enabled()) return;
    log_notice("js-mir-phase: file=%s phase=%s elapsed_ms=%ld",
        filename ? filename : "<string>", phase, elapsed_us / 1000L);
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
static bool g_js_mir_volume_stats_enabled = false;
static bool g_js_mir_volume_stats_checked = false;
static bool g_js_mir_volume_stats_registered = false;
static long g_js_mir_volume_stats_samples = 0;
static long g_js_mir_volume_stats_total_functions = 0;
static long g_js_mir_volume_stats_total_insns = 0;
static long g_js_mir_volume_stats_max_insns = 0;

static void js_mir_volume_stats_report(void);

static int js_mir_volume_stats_is_enabled(void) {
    if (!g_js_mir_volume_stats_checked) {
        const char* flag = getenv("LAMBDA_JS_MIR_VOLUME_STATS");
        if (flag && flag[0] && strcmp(flag, "0") != 0) {
            g_js_mir_volume_stats_enabled = true;
        }
        g_js_mir_volume_stats_checked = true;
    }
    if (g_js_mir_volume_stats_enabled && !g_js_mir_volume_stats_registered) {
        atexit(js_mir_volume_stats_report);
        g_js_mir_volume_stats_registered = true;
    }
    return g_js_mir_volume_stats_enabled ? 1 : 0;
}

static void js_mir_volume_stats_write_line(int fd, const char* line) {
    if (fd < 0 || !line) return;
    size_t len = strlen(line);
    const char* cur = line;
    while (len > 0) {
        int wrote = (int)JS_MIR_VOL_STATS_WRITE(fd, cur, len);
        if (wrote <= 0) return;
        cur += wrote;
        len -= (size_t)wrote;
    }
}

static void js_mir_volume_stats_report(void) {
    if (!g_js_mir_volume_stats_enabled || g_js_mir_volume_stats_samples == 0) return;
    const char* dir = getenv("LAMBDA_JS_MIR_VOLUME_STATS_DIR");
    if (!dir || !dir[0]) dir = "./temp/js_mir_volume_stats";
    JS_MIR_VOL_STATS_MKDIR(dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/%d.tsv", dir, (int)JS_MIR_VOL_STATS_PID());
    int fd = JS_MIR_VOL_STATS_OPEN(path);
    if (fd < 0) return;
    js_mir_volume_stats_write_line(fd,
        "samples\tlast_functions_discovered\tlast_mir_insns_emitted\ttotal_functions_discovered\ttotal_mir_insns_emitted\tmax_mir_insns_emitted\n");
    char line[512];
    snprintf(line, sizeof(line), "%ld\t%ld\t%ld\t%ld\t%ld\t%ld\n",
        g_js_mir_volume_stats_samples,
        g_last_js_mir_volume.functions_discovered,
        g_last_js_mir_volume.mir_insns_emitted,
        g_js_mir_volume_stats_total_functions,
        g_js_mir_volume_stats_total_insns,
        g_js_mir_volume_stats_max_insns);
    js_mir_volume_stats_write_line(fd, line);
    JS_MIR_VOL_STATS_CLOSE(fd);
    log_notice("js-mir-volume-stats: samples=%ld last_funcs=%ld last_insns=%ld total_insns=%ld max_insns=%ld",
        g_js_mir_volume_stats_samples,
        g_last_js_mir_volume.functions_discovered,
        g_last_js_mir_volume.mir_insns_emitted,
        g_js_mir_volume_stats_total_insns,
        g_js_mir_volume_stats_max_insns);
}

extern "C" void js_mir_volume_counters_reset(void) {
    g_last_js_mir_volume.functions_discovered = 0;
    g_last_js_mir_volume.mir_insns_emitted = 0;
}

extern "C" void js_mir_volume_counters_set(long functions_discovered, long mir_insns_emitted) {
    g_last_js_mir_volume.functions_discovered = functions_discovered;
    g_last_js_mir_volume.mir_insns_emitted = mir_insns_emitted;
    if (js_mir_volume_stats_is_enabled()) {
        g_js_mir_volume_stats_samples++;
        g_js_mir_volume_stats_total_functions += functions_discovered;
        g_js_mir_volume_stats_total_insns += mir_insns_emitted;
        if (mir_insns_emitted > g_js_mir_volume_stats_max_insns) {
            g_js_mir_volume_stats_max_insns = mir_insns_emitted;
        }
    }
}

extern "C" void js_mir_volume_counters_get(JsMirVolumeCounters* out) {
    if (out) *out = g_last_js_mir_volume;
}

static bool js_mir_large_source_interp_enabled(void) {
    const char* flag = getenv("LAMBDA_JS_LARGE_INTERP");
    return !flag || (strcmp(flag, "0") != 0 && strcmp(flag, "false") != 0);
}

static size_t js_mir_large_source_interp_threshold(void) {
    const char* value = getenv("LAMBDA_JS_LARGE_INTERP_BYTES");
    if (!value || !value[0]) return 15000;
    char* end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || parsed <= 0) return 15000;
    return (size_t)parsed;
}

static void js_mir_destroy_unowned_eval_context(Runtime* runtime,
        EvalContext* local_context, bool reusing_context) {
    if (!reusing_context && local_context) {
        if (!eval_context_thread_matches(local_context)) {
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

Item transpile_js_ast_to_mir(Runtime* runtime, JsTranspiler* tp, JsAstNode* ast,
                             const char* filename, uint64_t* result_home) {
    log_debug("js-mir-ast: transpiling pre-built AST for '%s'", filename ? filename : "<string>");

    // Use Runtime's canonical context for a fresh JS activation. A nested
    // caller may reuse its already-bound context, but no path may switch back
    // to another owner after this call.
    EvalContext* js_context = NULL;
    bool reusing_context = false;

    if (context && context->heap) {
        js_context = context;
        reusing_context = true;
        if (!context->type_list) {
            context->type_list = runtime && runtime->type_list
                ? runtime->type_list
                : arraylist_new(64);
        }
    } else {
        js_context = runtime_get_eval_context(runtime);
        if (!js_context) return (Item){.item = ITEM_ERROR};
        if (!eval_context_thread_initialize(js_context)) {
            return (Item){.item = ITEM_ERROR};
        }
        heap_init();
        context->pool = context->heap->pool;
        // Keep the fresh static root open while this initial closure is
        // compiled; generated names are sealed only immediately before the
        // first JS execution (D5.4.3, RN-NamePool).
        context->name_pool = name_pool_create_runtime_static(context->pool);
        context->type_list = arraylist_new(64);
        // The canonical context owns this fresh heap; publish the same owner
        // immediately so runtime_cleanup releases it even if compilation fails.
        runtime->heap = context->heap;
        runtime->name_pool = context->name_pool;
        runtime->type_list = (ArrayList*)context->type_list;
    }
    context->runtime = runtime;
    if (!js_runtime_state_thread_initialize(context)) return ItemError;

    Input* js_input = Input::create(context->pool);
    js_runtime_set_input(js_input);

    // initialize MIR context
    MIR_context_t ctx = jit_init(g_js_mir_optimize_level);
    if (!ctx) {
        log_error("js-mir-ast: MIR context init failed");
        js_mir_destroy_unowned_eval_context(runtime, js_context, reusing_context);
        return (Item){.item = ITEM_ERROR};
    }

    // set up MIR transpiler
    JsMirTranspiler* mt = jm_create_mir_transpiler(tp, ctx, filename, false, 64, 32, 16, "js-mir-ast");
    if (!mt) {
        MIR_finish(ctx);
        js_mir_destroy_unowned_eval_context(runtime, js_context, reusing_context);
        return (Item){.item = ITEM_ERROR};
    }
    mt->module_name_base = js_preamble_consumer_name_base(g_jm_preamble_in);
    mt->module_ic_base = js_preamble_consumer_ic_base(g_jm_preamble_in);

    mt->module = MIR_new_module(ctx, "ts_script");

    // transpile AST to MIR
    if (!transpile_js_mir_ast(mt, ast)) {
        log_error("js-mir-ast: collection/allocation failed");
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        js_mir_destroy_unowned_eval_context(runtime, js_context, reusing_context);
        return (Item){.item = ITEM_ERROR};
    }

    // This alternate entry point executes the same generated property-key
    // table as the source entry point.  Prelink it while the runtime root is
    // still static; otherwise js_activate_runtime_name_pool would turn these
    // compile-time spellings into per-session dynamic names.
    if (!js_prelink_compiled_name_table(mt)) {
        log_error("js-mir-ast: failed to prelink property-name table");
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        js_mir_destroy_unowned_eval_context(runtime, js_context, reusing_context);
        return (Item){.item = ITEM_ERROR};
    }

    // Canonical finalized artifact (MT2); see the JS path above.
#ifndef NDEBUG
    const bool ts_legacy_mir_dump = getenv("JS_MIR_DUMP") != NULL;
#else
    const bool ts_legacy_mir_dump = false;
#endif
    mir_dump_finalized(ctx, "temp/ts_mir_dump.txt", ts_legacy_mir_dump);

    if (!jm_validate_mir_labels(ctx)) {
        log_error("js-mir-ast: NULL labels detected");
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        js_mir_destroy_unowned_eval_context(runtime, js_context, reusing_context);
        return (Item){.item = ITEM_ERROR};
    }

    MIR_link(ctx, g_mir_interp_mode ? MIR_set_interp_interface : MIR_set_gen_interface, import_resolver);

    typedef Item (*js_main_func_t)(Context*);
    js_main_func_t js_main = (js_main_func_t)find_func(ctx, (char*)"js_main");

    if (!js_main) {
        log_error("js-mir-ast: failed to find js_main");
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        js_mir_destroy_unowned_eval_context(runtime, js_context, reusing_context);
        return (Item){.item = ITEM_ERROR};
    }
    if (g_jm_preamble_out) {
        g_jm_preamble_out->entry_func = (void*)js_main;
        g_jm_preamble_out->owns_compiled_state = true;
        if (!js_capture_compiled_name_table(mt, g_jm_preamble_out)) {
            log_error("js-mir-ast: failed to retain property-name table");
            return (Item){.item = ITEM_ERROR};
        }
    }

    // execute
    log_debug("js-mir-ast: executing JIT compiled code");
    if (!js_activate_runtime_name_pool()) return (Item){.item = ITEM_ERROR};
    if (!js_activate_module_state((uint32_t)mt->module_var_count)) return (Item){.item = ITEM_ERROR};
    if (!js_link_compiled_name_table(mt)) return (Item){.item = ITEM_ERROR};
    Item result = js_main((Context*)context);
    log_debug("js-mir-ast: execution returned (type=%d)", get_type_id(result));

    // handle result
    Item final_result;
    TypeId type_id = get_type_id(result);

    if (reusing_context) {
        final_result = result;
    } else {
        if (type_id == LMD_TYPE_FLOAT) {
            double value = it2d(result);
            if (value == (double)(int64_t)value && value >= INT32_MIN && value <= INT32_MAX) {
                final_result = (Item){.item = i2it((int64_t)value)};
            } else {
                final_result = lambda_item_adopt_scalar_home(result, result_home);
            }
        } else {
            final_result = result;
        }
    }

    ArrayList* result_type_list = (ArrayList*)context->type_list;
    // The freshly created context is already Runtime-owned; no adoption or
    // pointer transfer is needed after execution.

    // cleanup
    jm_destroy_mir_transpiler(mt);
    MIR_finish(ctx);

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

static bool js_is_line_terminator(char ch) {
    return ch == '\n' || ch == '\r';
}

static bool js_is_trivia_char(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\v' || ch == '\f' || js_is_line_terminator(ch);
}

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

static Item transpile_js_to_mir_core_profile_len(Runtime* runtime, const char* js_source,
                                                 size_t js_source_len, const char* filename,
                                                 uint64_t* result_home,
                                                 bool typescript_profile) {
    // Recovery ownership begins before parsing. The first entry may initialize
    // an idle eval thread, but compilation cannot borrow and restore another
    // live context.
    EvalContext* compile_context = context ? context :
        runtime_get_eval_context(runtime);
    if (!eval_context_thread_initialize(compile_context)) {
        log_error("js-mir: cannot initialize compile recovery context");
        return ItemError;
    }
    if (!js_runtime_state_thread_initialize(compile_context)) {
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
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        jm_clear_active_js_transpile(NULL, NULL, owned_source);
        mem_free(owned_source);
        return (Item){.item = ITEM_ERROR};
    }
    g_last_js_mir_phase_timing.parse_us = js_mir_phase_now_us() - phase_start;
    js_mir_log_phase_progress(filename, "parse", g_last_js_mir_phase_timing.parse_us);
    log_mem_stage("js-core: ts_parsed");

    TSNode root = ts_tree_root_node(tp->tree);

    // Build JavaScript AST
    phase_start = js_mir_phase_now_us();
    JsAstNode* js_ast = build_js_ast(tp, root);
    if (!js_ast) {
        log_error("js-mir: AST build failed");
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        jm_clear_active_js_transpile(NULL, NULL, owned_source);
        mem_free(owned_source);
        return (Item){.item = ITEM_ERROR};
    }
    g_last_js_mir_phase_timing.ast_us = js_mir_phase_now_us() - phase_start;
    js_mir_log_phase_progress(filename, "ast", g_last_js_mir_phase_timing.ast_us);
    log_mem_stage("js-core: ast_built");

    // Run early error detection (static semantic validation)
    phase_start = js_mir_phase_now_us();
    int early_errors = js_check_early_errors(tp, js_ast);
    if (early_errors > 0) {
        log_error("js-mir: %d early error(s) detected", early_errors);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        jm_clear_active_js_transpile(NULL, NULL, owned_source);
        mem_free(owned_source);
        return (Item){.item = ITEM_ERROR};
    }
    g_last_js_mir_phase_timing.early_us = js_mir_phase_now_us() - phase_start;
    js_mir_log_phase_progress(filename, "early", g_last_js_mir_phase_timing.early_us);

    // Set up the canonical evaluation context early so module objects and
    // deferred callbacks share one lifetime owner.
    EvalContext* js_context = NULL;
    bool reusing_context = false;

    if (context->heap) {
        js_context = context;
        reusing_context = true;
        if (!context->type_list) {
            context->type_list = runtime && runtime->type_list
                ? runtime->type_list
                : arraylist_new(64);
        }
    } else {
        js_context = runtime_get_eval_context(runtime);
        if (!js_context || !eval_context_thread_matches(js_context)) {
            jm_clear_active_js_transpile(tp, NULL, NULL);
            js_transpiler_destroy(tp);
            jm_clear_active_js_transpile(NULL, NULL, owned_source);
            mem_free(owned_source);
            log_error("js-mir: Runtime context differs from eval-thread owner");
            return ItemError;
        }
        heap_init();
        context->pool = context->heap->pool;
        // The initial script/import closure owns the static collection point.
        // Dynamic allocation starts only after MIR lowering has completed and
        // the root is sealed (D5.4.3, D5.4.4).
        context->name_pool = name_pool_create_runtime_static(context->pool);
        context->type_list = arraylist_new(64);
        // Keep Runtime and its canonical context on the same fresh heap.
        runtime->heap = context->heap;
        runtime->name_pool = context->name_pool;
        runtime->type_list = (ArrayList*)context->type_list;
    }
    context->runtime = runtime;
    if (!js_runtime_state_thread_initialize(context)) return ItemError;
    if (runtime->dom_ui_context) {
        // The stack worker binds a distinct execution realm. Carry the host's
        // borrowed UI session into that realm before any DOM wrapper or task is
        // created; rebinding it after execution leaves callbacks in a dead realm.
        js_dom_set_ui_context(runtime->dom_ui_context);
    }
    ArrayList* previous_debug_info = context->debug_info;
    const char* previous_current_file = context->current_file;


    // Create Input context for JS runtime — must be before module loading
    Input* js_input = Input::create(context->pool);
    js_runtime_set_input(js_input);

    bool use_mir_interp_for_script = g_mir_interp_mode != 0;
    bool auto_interp_for_large_source = false;
    int saved_mir_interp_mode = g_mir_interp_mode;
    if (!use_mir_interp_for_script && g_js_mir_optimize_level == 0 &&
        js_mir_large_source_interp_enabled() &&
        js_source_len >= js_mir_large_source_interp_threshold()) {
        g_mir_interp_mode = 1;
        use_mir_interp_for_script = true;
        auto_interp_for_large_source = true;
        log_info("js-mir: large source (%zu bytes) uses MIR interpreter at opt=0", js_source_len);
    }
    MIR_context_t ctx = jit_init(g_js_mir_optimize_level);
    if (auto_interp_for_large_source) {
        g_mir_interp_mode = saved_mir_interp_mode;
    }
    if (!ctx) {
        log_error("js-mir: MIR context init failed");
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        jm_clear_active_js_transpile(NULL, NULL, owned_source);
        mem_free(owned_source);
        js_mir_destroy_unowned_eval_context(runtime, js_context, reusing_context);
        return (Item){.item = ITEM_ERROR};
    }
    g_active_mir_ctx = ctx;  // track for batch timeout recovery

    // Install batch error handler if set (prevents exit(1) on MIR errors)
    if (g_batch_mir_error_handler) {
        MIR_set_error_func(ctx, g_batch_mir_error_handler);
    }

    // Set up the compact MIR transpiler; source-sized collection storage is allocated after parsing.
    JsMirTranspiler* mt = jm_create_mir_transpiler(tp, ctx, filename, false, 64, 32, 16, "js-mir");
    if (!mt) {
        g_active_mir_ctx = NULL;
        MIR_finish(ctx);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        jm_clear_active_js_transpile(NULL, NULL, owned_source);
        mem_free(owned_source);
        js_mir_destroy_unowned_eval_context(runtime, js_context, reusing_context);
        return (Item){.item = ITEM_ERROR};
    }
    jm_track_active_js_transpile(NULL, mt, NULL);
    mt->module_name_base = js_preamble_consumer_name_base(g_jm_preamble_in);
    mt->module_ic_base = js_preamble_consumer_ic_base(g_jm_preamble_in);

    // Preamble mode setup
    mt->preamble_mode = g_jm_preamble_mode;
    if (g_jm_preamble_in) {
        mt->preamble_entries = g_jm_preamble_in->entries;
        mt->preamble_entry_count = g_jm_preamble_in->entry_count;
        mt->preamble_var_count = g_jm_preamble_in->module_var_count;
    }

    // Create module
    mt->module = MIR_new_module(ctx, "js_script");

    // Transpile AST to MIR
    phase_start = js_mir_phase_now_us();
    bool transpile_ok = transpile_js_mir_ast(mt, js_ast);
    g_last_js_mir_phase_timing.mir_us = js_mir_phase_now_us() - phase_start;
    js_mir_log_phase_progress(filename, "mir", g_last_js_mir_phase_timing.mir_us);
    log_mem_stage("js-core: ast_to_mir");
    if (!transpile_ok) {
        log_error("js-mir: collection/allocation failed for '%s'",
            filename ? filename : "<string>");
        jm_clear_active_js_transpile(NULL, mt, NULL);
        jm_destroy_mir_transpiler(mt);
        g_active_mir_ctx = NULL;
        MIR_finish(ctx);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        jm_clear_active_js_transpile(NULL, NULL, owned_source);
        mem_free(owned_source);
        js_mir_destroy_unowned_eval_context(runtime, js_context, reusing_context);
        return (Item){.item = ITEM_ERROR};
    }

    // Complete static discovery before any realm or module initializer can
    // create a runtime name. The main module has now contributed its sealed
    // property-key image; imported modules contribute theirs in precompile.
    if (!js_prelink_compiled_name_table(mt)) {
        log_error("js-mir: failed to prelink main property-name table");
        jm_clear_active_js_transpile(NULL, mt, NULL);
        jm_destroy_mir_transpiler(mt);
        g_active_mir_ctx = NULL;
        MIR_finish(ctx);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        jm_clear_active_js_transpile(NULL, NULL, owned_source);
        mem_free(owned_source);
        js_mir_destroy_unowned_eval_context(runtime, js_context, reusing_context);
        return (Item){.item = ITEM_ERROR};
    }

    if (!g_jm_preamble_compile_only) {
        phase_start = js_mir_phase_now_us();
#ifndef _WIN32
        bool has_static_imports = js_source_contains_ascii(js_source, js_source_len,
            "import ") || js_source_contains_ascii(js_source, js_source_len, "import{");
        if (has_static_imports && jm_precompile_js_imports(runtime, js_source, filename) < 0) {
            log_error("js-mir: failed to precompile import closure");
            jm_clear_active_js_transpile(NULL, mt, NULL);
            jm_destroy_mir_transpiler(mt);
            g_active_mir_ctx = NULL;
            MIR_finish(ctx);
            jm_clear_active_js_transpile(tp, NULL, NULL);
            js_transpiler_destroy(tp);
            jm_clear_active_js_transpile(NULL, NULL, owned_source);
            mem_free(owned_source);
            js_mir_destroy_unowned_eval_context(runtime, js_context, reusing_context);
            return (Item){.item = ITEM_ERROR};
        }
#endif
        if (!js_activate_runtime_name_pool()) {
            log_error("js-mir: failed to activate dynamic NamePool");
            jm_clear_active_js_transpile(NULL, mt, NULL);
            jm_destroy_mir_transpiler(mt);
            g_active_mir_ctx = NULL;
            MIR_finish(ctx);
            jm_clear_active_js_transpile(tp, NULL, NULL);
            js_transpiler_destroy(tp);
            jm_clear_active_js_transpile(NULL, NULL, owned_source);
            mem_free(owned_source);
            js_mir_destroy_unowned_eval_context(runtime, js_context, reusing_context);
            return (Item){.item = ITEM_ERROR};
        }
        // Realm construction is runtime work: it must occur only after static
        // discovery seals the root and installs the dynamic child.
        (void)js_get_global_this();
        jm_load_imports(runtime, js_ast, filename);
        g_last_js_mir_phase_timing.imports_us = js_mir_phase_now_us() - phase_start;
        js_mir_log_phase_progress(filename, "imports", g_last_js_mir_phase_timing.imports_us);
        log_mem_stage("js-core: imports_loaded");
    }

    // Canonical finalized artifact (MT2): transpile_js_mir_ast has already run
    // MIR_finish_func/MIR_finish_module, so this is the finalized stage the
    // emission tests read. LAMBDA_MIR_DUMP_PATH names a private artifact and
    // works in release builds; JS_MIR_DUMP keeps the legacy developer path.
#ifndef NDEBUG
    const bool js_legacy_mir_dump = getenv("JS_MIR_DUMP") != NULL;
#else
    const bool js_legacy_mir_dump = false;
#endif
    mir_dump_finalized(ctx, "temp/js_mir_dump.txt", js_legacy_mir_dump);

    // Pre-link validation: abort gracefully if NULL labels found
    if (!jm_validate_mir_labels(ctx)) {
        log_error("js-mir: NULL labels detected, aborting link for '%s'", filename ? filename : "<string>");
        jm_clear_active_js_transpile(NULL, mt, NULL);
        jm_destroy_mir_transpiler(mt);
        g_active_mir_ctx = NULL;
        MIR_finish(ctx);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        jm_clear_active_js_transpile(NULL, NULL, owned_source);
        mem_free(owned_source);
        js_mir_destroy_unowned_eval_context(runtime, js_context, reusing_context);
        return (Item){.item = ITEM_ERROR};
    }

    // Link and generate
#ifndef NDEBUG
    FILE* mir_gen_debug = NULL;
    if (getenv("JS_MIR_GEN_TIMING")) {
        create_dir_recursive("temp");
        mir_gen_debug = fopen("temp/mir_gen_timing.txt", "w");
        if (mir_gen_debug) {
            MIR_gen_set_debug_file(ctx, mir_gen_debug);
            MIR_gen_set_debug_level(ctx, 0);
        }
    }
#endif
    // Count total MIR instructions (drives the interpreter policy and the JIT
    // opt-downgrade fallback below).
    unsigned long total_insns = 0;
    for (MIR_module_t m = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(ctx)); m != NULL;
         m = DLIST_NEXT(MIR_module_t, m)) {
        for (MIR_item_t item = DLIST_HEAD(MIR_item_t, m->items); item != NULL;
             item = DLIST_NEXT(MIR_item_t, item)) {
            if (item->item_type == MIR_func_item)
                total_insns += DLIST_LENGTH(MIR_insn_t, item->u.func->insns);
        }
    }
    js_mir_volume_counters_set((long)mt->func_count, (long)total_insns);
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
    if (!use_mir_interp_for_script && js_mir_large_source_interp_enabled() &&
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
        js_lazy_mir_cached = (lazy_env && lazy_env[0] && strcmp(lazy_env, "0") != 0) ? 1 : 0;
    }
    void (*gen_interface)(MIR_context_t, MIR_item_t) =
        js_lazy_mir_cached ? MIR_set_lazy_gen_interface : MIR_set_gen_interface;

    phase_start = js_mir_phase_now_us();
    MIR_link(ctx, use_mir_interp_for_script ? MIR_set_interp_interface : gen_interface, import_resolver);
    g_last_js_mir_phase_timing.link_us = js_mir_phase_now_us() - phase_start;
    js_mir_log_phase_progress(filename, "link", g_last_js_mir_phase_timing.link_us);
    log_mem_stage("js-core: mir_linked");
    void* js_debug_info = jm_build_js_debug_info(mt, filename);
    context->debug_info = (ArrayList*)js_debug_info;
    context->current_file = filename ? filename : "<string>";
    // Restore opt level if we changed it
    if (effective_opt != g_js_mir_optimize_level) {
        MIR_gen_set_optimize_level(ctx, g_js_mir_optimize_level);
    }
#ifndef NDEBUG
    if (mir_gen_debug) {
        fclose(mir_gen_debug);
        MIR_gen_set_debug_file(ctx, NULL);
    }
#endif

    // Find js_main
    typedef Item (*js_main_func_t)(Context*);
    // Use find_func which is declared in transpiler.hpp
    js_main_func_t js_main = (js_main_func_t)find_func(ctx, (char*)"js_main");

    if (!js_main) {
        log_error("js-mir: failed to find js_main");
        context->debug_info = previous_debug_info;
        context->current_file = previous_current_file;
        if (js_debug_info) free_debug_info_table(js_debug_info);
        jm_clear_active_js_transpile(NULL, mt, NULL);
        jm_destroy_mir_transpiler(mt);
        g_active_mir_ctx = NULL;
        MIR_finish(ctx);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        jm_clear_active_js_transpile(NULL, NULL, owned_source);
        mem_free(owned_source);
        js_mir_destroy_unowned_eval_context(runtime, js_context, reusing_context);
        return (Item){.item = ITEM_ERROR};
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
        uint32_t source_state_id = js_get_active_module_state_id();
        if (!js_activate_module_state((uint32_t)mt->module_var_count) ||
                !js_copy_module_state_var_prefix(
                    source_state_id, js_get_active_module_state_id(),
                    (uint32_t)g_jm_preamble_in->module_var_count)) {
            log_error("js-mir: failed to create preamble consumer module state");
            return (Item){.item = ITEM_ERROR};
        }
    } else if (!g_jm_preamble_compile_only) {
        if (!js_activate_module_state((uint32_t)mt->module_var_count)) {
            return (Item){.item = ITEM_ERROR};
        }
    }
    if (!g_jm_preamble_compile_only && !js_link_compiled_name_table(mt)) {
        log_error("js-mir: failed to link compiled property-name table");
        return (Item){.item = ITEM_ERROR};
    }

    // v14: initialize event loop before execution. Dynamic import runs inside
    // an active script, so preserve the caller's pending PromiseJobs.
    if (js_dynamic_import_suppress_module_drain <= 0) {
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
        int ecount = (int)hashmap_count(mt->module_consts);
        JsModuleConstEntry* next_entries = NULL;
        if (ecount > 0) {
            next_entries = (JsModuleConstEntry*)mem_calloc(
                (size_t)ecount, sizeof(JsModuleConstEntry), MEM_CAT_JS_RUNTIME);
        }
        int next_entry_count = 0;
        bool copy_succeeded = ecount == 0 || next_entries != NULL;
        size_t eiter = 0; void* eitem;
        while (copy_succeeded && hashmap_iter(mt->module_consts, &eiter, &eitem)) {
            JsModuleConstEntry* source_entry = (JsModuleConstEntry*)eitem;
            copy_succeeded = js_preamble_entry_copy(
                source_entry, &next_entries[next_entry_count]);
            if (copy_succeeded) next_entry_count++;
        }
        if (copy_succeeded) {
            // defer releasing the previous slab until every shallow map entry
            // has been copied; freeing it before this loop leaves source_entry
            // names dangling in the current transpiler.
            js_eval_preamble_entries_free();
            g_eval_preamble_entries = next_entries;
            g_eval_preamble_entry_count = next_entry_count;
            g_eval_preamble_var_count = mt->module_var_count;
        } else {
            js_preamble_entries_free(next_entries, ecount);
            // The old slab was only needed while constructing this replacement.
            // Do not leave a failed replacement visible to a nested eval.
            js_eval_preamble_entries_free();
        }
        log_debug("js-mir: saved eval preamble: %d entries, %d module vars",
            g_eval_preamble_entry_count, g_eval_preamble_var_count);
    }

    // With-preamble mode: caller already called js_batch_reset_to() — harness vars preserved
    phase_start = js_mir_phase_now_us();
    Item result = ItemNull;
    if (!g_jm_preamble_compile_only) {
    LambdaRecoveryFrame* recovery_frame = lambda_recovery_frame_begin_for(
        (Context*)context, LAMBDA_RECOVERY_CAP_EXECUTION_BOUNDARY);
    if (!recovery_frame) {
        log_error("js-mir: failed to allocate recovery frame");
        result = lambda_recovery_publish_fault_item(
            (Context*)context, LAMBDA_FAULT_OUT_OF_MEMORY, ERR_OK);
    } else if (LAMBDA_RECOVERY_FRAME_SETJMP(recovery_frame)) {
        Item recovered = ItemError;
        if (!lambda_recovery_frame_restore_landing(recovery_frame)) {
            log_error("js-mir: recovery frame landing invariant failed");
            recovered = lambda_recovery_publish_fault_item((Context*)context,
                LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK);
        } else {
            recovered = lambda_recovery_frame_fault_item((Context*)context,
                recovery_frame);
        }
        _lambda_stack_overflow_flag = false;
        lambda_recovery_frame_end(recovery_frame);
        result = recovered;
        // the recovery result already carries the boundary error; a second
        // throw would replace its identity with a generic stack-overflow value.
    } else {
        if (!lambda_recovery_frame_arm(recovery_frame)) {
            log_error("js-mir: failed to arm recovery frame");
            lambda_recovery_frame_end(recovery_frame);
            result = lambda_recovery_publish_fault_item((Context*)context,
                LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK);
        } else {
            result = js_main((Context*)context);
            lambda_recovery_frame_end(recovery_frame);
        }
    }
    }
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
    if (!g_jm_preamble_compile_only) {
        js_event_loop_drain_script_turn(
            runtime->dom_doc != NULL,
            js_dynamic_import_suppress_module_drain <= 0);
    }
    log_debug("js-mir: event loop drained");

    // Fire process lifecycle listeners for Node.js compatibility.  test262
    // batch workers intentionally run many isolated ECMAScript tests in one
    // process; process lifecycle events are a CLI/Node boundary, not a
    // per-test boundary, and running them here pollutes hot-reload batches.
    if (!g_jm_preamble_compile_only && !js_batch_execution_mode) {
        int exit_code = item_is_error(result) ? 1 : js_process_current_exit_code();
        // The script's result owns its ERROR Item; lifecycle callbacks receive
        // no ambient exception state and cannot consume that failure.
        js_async_hooks_drain_destroy_queue();
        // beforeExit listener failures are intentionally consumed at this
        // process-shutdown boundary; the listener result itself carries the
        // failure, so no global state needs to be cleared.
        (void)js_process_emit_before_exit(exit_code);
        if (js_event_loop_has_refed_handles()) {
            js_event_loop_drain();
        } else {
            js_microtask_flush();
        }
        js_process_emit_exit(exit_code);
        js_trace_flush();
        js_process_current_exit_code();
    }

    // Preamble mode: snapshot module_consts so tests can inherit harness definitions
    if (g_jm_preamble_out && mt->module_consts) {
        js_preamble_entries_free(g_jm_preamble_out->entries,
                                 g_jm_preamble_out->entry_count);
        g_jm_preamble_out->entries = NULL;
        g_jm_preamble_out->entry_count = 0;
        g_jm_preamble_out->module_var_count = mt->module_var_count;
        int count = (int)hashmap_count(mt->module_consts);
        JsModuleConstEntry* entries = NULL;
        if (count > 0) {
            entries = (JsModuleConstEntry*)mem_calloc(
                (size_t)count, sizeof(JsModuleConstEntry), MEM_CAT_JS_RUNTIME);
        }
        bool copy_succeeded = count == 0 || entries != NULL;
        int entry_count = 0;
        size_t snap_iter = 0; void* snap_item;
        while (copy_succeeded && hashmap_iter(mt->module_consts, &snap_iter, &snap_item)) {
            copy_succeeded = js_preamble_entry_copy(
                (JsModuleConstEntry*)snap_item, &entries[entry_count]);
            if (copy_succeeded) entry_count++;
        }
        if (copy_succeeded) {
            g_jm_preamble_out->entries = entries;
            g_jm_preamble_out->entry_count = entry_count;
        } else {
            js_preamble_entries_free(entries, count);
            g_jm_preamble_out->module_var_count = 0;
        }
        log_debug("js-mir: preamble snapshot: %d entries, %d module vars",
            g_jm_preamble_out->entry_count, g_jm_preamble_out->module_var_count);
    }

    // Handle result (same logic as js_transpiler_compile)
    Item final_result;
    TypeId type_id = get_type_id(result);

    if (reusing_context) {
        final_result = result;
    } else {
        if (type_id == LMD_TYPE_FLOAT) {
            double value = it2d(result);
            if (value == (double)(int64_t)value && value >= INT32_MIN && value <= INT32_MAX) {
                final_result = (Item){.item = i2it((int64_t)value)};
            } else {
                // The JS context is about to be restored, so its number frame
                // cannot own an out-of-band scalar result after this boundary.
                final_result = lambda_item_adopt_scalar_home(result, result_home);
            }
        } else {
            final_result = result;
        }
    }

    // Convert JS HashMap objects to VMap for proper printing (before context restore)
    // (no longer needed — JS objects are now Lambda Maps)

    ArrayList* result_type_list = (ArrayList*)context->type_list;
    context->debug_info = previous_debug_info;
    context->current_file = previous_current_file;
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
        MIR_finish(ctx);
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
                                                filename, result_home, false);
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

Item transpile_js_typescript_to_mir_len(Runtime* runtime, const char* js_source,
                                        size_t js_source_len, const char* filename,
                                        uint64_t* result_home) {
    g_jm_preamble_mode = false;
    g_jm_preamble_out = NULL;
    g_jm_preamble_in = NULL;
    return transpile_js_to_mir_core_profile_len(runtime, js_source, js_source_len,
                                                filename, result_home, true);
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
        out_state->module_state_id = js_get_active_module_state_id();
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
                                          const JsPreambleState* compiled_state) {
    if (!runtime || !runtime->heap || !base_preamble || !compiled_state ||
            !compiled_state->entry_func) {
        return ItemError;
    }

    EvalContext* runtime_context = runtime_get_eval_context(runtime);
    if (!runtime_context) return ItemError;
    runtime_context->heap = runtime->heap;
    runtime_context->name_pool = runtime->name_pool;
    runtime_context->type_list = runtime->type_list;
    runtime_context->pool = runtime->heap->pool;
    if (!eval_context_thread_initialize(runtime_context) ||
            !js_runtime_state_thread_initialize(runtime_context)) {
        return ItemError;
    }
    if (runtime->dom_ui_context) js_dom_set_ui_context(runtime->dom_ui_context);
    if (runtime->dom_doc) js_dom_set_document(runtime->dom_doc);

    uint32_t consumer_state_id = js_get_active_module_state_id();
    uint32_t unit_var_count = compiled_state->module_var_count >
            base_preamble->module_var_count
        ? (uint32_t)compiled_state->module_var_count
        : (uint32_t)base_preamble->module_var_count;
    if (consumer_state_id == UINT32_MAX ||
            !js_activate_module_state(unit_var_count)) {
        return ItemError;
    }
    uint32_t unit_state_id = js_get_active_module_state_id();
    // D3.4.4v2: immutable cached MIR keeps the property/IC indices encoded at
    // the preamble boundary. Give it a private slab with that exact prefix;
    // executing against the growing browser-consumer slab aliases its first
    // script names and ICs once any user unit has appended to the table.
    bool linked = lambda_module_state_link_property_keys(unit_state_id,
            base_preamble->module_property_specs,
            base_preamble->module_property_count,
            base_preamble->module_property_bytes_size) &&
        lambda_module_state_append_property_keys(unit_state_id,
            compiled_state->module_property_specs,
            compiled_state->module_property_count,
            compiled_state->module_property_bytes_size) &&
        js_link_module_ic_table(unit_state_id, base_preamble->ic_count) &&
        js_append_module_ic_table(unit_state_id, compiled_state->ic_count) &&
        js_copy_module_state_var_prefix(consumer_state_id, unit_state_id,
            (uint32_t)base_preamble->module_var_count);
    if (!linked) {
        (void)js_set_active_module_state_id(consumer_state_id);
        return ItemError;
    }

    typedef Item (*js_main_func_t)(Context*);
    js_main_func_t js_main = (js_main_func_t)compiled_state->entry_func;
    js_mir_reset_last_phase_timing();
    long execute_start = js_mir_phase_now_us();
    Item result = js_main((Context*)context);
    g_last_js_mir_phase_timing.execute_us = js_mir_phase_now_us() - execute_start;
    g_last_js_mir_phase_timing.total_us = g_last_js_mir_phase_timing.execute_us;
    if (!js_set_active_module_state_id(consumer_state_id)) return ItemError;
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
    if (!preamble || !js_module_state_is_available(preamble->module_state_id) ||
            js_get_active_module_state_id() == preamble->module_state_id) {
        log_error("js-mir: invalid preamble consumer preamble=%u active=%u available=%d",
                  preamble ? preamble->module_state_id : UINT32_MAX,
                  js_get_active_module_state_id(),
                  preamble && js_module_state_is_available(preamble->module_state_id)
                      ? 1 : 0);
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
    out_state->ic_count = source->ic_count;
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
    if (!eval_context_thread_initialize(js_context)) {
        preamble_state_destroy(out_state);
        return ItemError;
    }
    context->runtime = runtime;
    if (!js_runtime_state_thread_initialize(context)) {
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
    runtime->heap = context->heap;
    runtime->name_pool = context->name_pool;
    runtime->type_list = (ArrayList*)context->type_list;

    // Realm construction can compile inline DOM handlers before js_main runs.
    // Publish the preamble slab first so those native compilation callbacks
    // never observe a context with no active module state.
    if (!js_activate_module_state((uint32_t)cached->module_var_count)) {
        preamble_state_destroy(out_state);
        return ItemError;
    }
    if (!lambda_module_state_link_property_keys(js_get_active_module_state_id(),
            cached->module_property_specs, cached->module_property_count,
            cached->module_property_bytes_size)) {
        preamble_state_destroy(out_state);
        return ItemError;
    }
    if (!js_link_module_ic_table(js_get_active_module_state_id(), cached->ic_count)) {
        preamble_state_destroy(out_state);
        return ItemError;
    }
    out_state->module_state_id = js_get_active_module_state_id();

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
    js_event_loop_init();
    if (runtime->dom_doc) js_dom_set_document(runtime->dom_doc);

    typedef Item (*js_main_func_t)(Context*);
    js_main_func_t js_main = (js_main_func_t)cached->entry_func;
    js_mir_reset_last_phase_timing();
    long execute_start = js_mir_phase_now_us();
    Item result = js_main((Context*)context);
    g_last_js_mir_phase_timing.execute_us = js_mir_phase_now_us() - execute_start;
    g_last_js_mir_phase_timing.total_us = g_last_js_mir_phase_timing.execute_us;
    js_microtask_flush();

    return result;
}

bool preamble_state_update_from_eval_snapshot(JsPreambleState* state) {
    if (!state || !g_eval_preamble_entries || g_eval_preamble_entry_count <= 0) {
        return false;
    }

    int count = g_eval_preamble_entry_count;
    JsModuleConstEntry* entries = NULL;
    if (!js_preamble_entries_copy(g_eval_preamble_entries, count, &entries)) {
        log_error("js-mir: failed to copy refreshed preamble snapshot");
        return false;
    }

    js_preamble_entries_free(state->entries, state->entry_count);
    state->entries = entries;
    state->entry_count = count;
    state->module_var_count = g_eval_preamble_var_count >= state->module_var_count
        ? g_eval_preamble_var_count
        : state->module_var_count;
    log_debug("js-mir: refreshed preamble snapshot: %d entries, %d module vars",
        state->entry_count, state->module_var_count);
    return true;
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
        if (!eval_context_thread_initialize(js_context)) {
            jm_clear_active_js_transpile(NULL, NULL, source);
            mem_free(source);
            return ItemNull;
        }
        heap_init();
        context->pool = context->heap->pool;
        context->name_pool = name_pool_create_runtime(context->pool);
        context->type_list = arraylist_new(64);
        context->runtime = runtime;
        if (!js_runtime_state_thread_initialize(context)) {
            jm_clear_active_js_transpile(NULL, NULL, source);
            mem_free(source);
            return ItemNull;
        }

        // The canonical context owns the bootstrap heap through runner setup.
        runtime->heap = context->heap;
        runtime->name_pool = context->name_pool;
        runtime->type_list = (ArrayList*)context->type_list;
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

    Item package_text = (Item){.item = s2it(heap_create_name(package_source, strlen(package_source)))};
    Item package_obj = js_json_parse(package_text);
    mem_free(package_source);
    if (item_is_error(package_obj)) return NULL;

    Item main_key = (Item){.item = s2it(heap_create_name("main", 4))};
    Item main_value = js_get_key_default(package_obj, main_key);
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

static char* js_require_read_resolved_path(char* path_buf, int path_buf_size) {
    return js_require_read_resolved_path_internal(path_buf, path_buf_size, true);
}

static Item js_cjs_key(const char* name, int len) {
    return (Item){.item = s2it(heap_create_name(name, len))};
}

static Item js_cjs_key(const char* name) {
    return js_cjs_key(name, (int)strlen(name));
}

#define js_cjs_module_stack_state (js_runtime_state.cjs.module_stack)
#define js_cjs_module_stack (js_cjs_module_stack_state.roots.slots)
#define js_cjs_module_stack_count (js_cjs_module_stack_state.depth)
#define js_cjs_module_names (js_runtime_state.cjs.module_names)
#define js_cjs_module_objects (js_runtime_state.cjs.module_objects)
#define js_cjs_module_count (js_runtime_state.cjs.module_count)
#define js_cjs_module_name_roots (js_runtime_state.cjs.module_name_roots)
#define js_cjs_module_object_roots (js_runtime_state.cjs.module_object_roots)

static bool js_cjs_register_roots(void) {
    if (!js_root_range_ensure_registered(&js_cjs_module_stack_state.roots)) return false;
    if (!js_root_range_ensure_registered(&js_cjs_module_name_roots)) return false;
    return js_root_range_ensure_registered(&js_cjs_module_object_roots);
}

extern "C" void js_cjs_metadata_reset(void) {
    js_item_stack_clear(&js_cjs_module_stack_state);
    memset(js_cjs_module_names, 0, sizeof(js_cjs_module_names));
    memset(js_cjs_module_objects, 0, sizeof(js_cjs_module_objects));
    js_cjs_module_count = 0;
}

static bool js_cjs_same_string(Item left, Item right) {
    if (left.item == right.item) return true;
    if (get_type_id(left) != LMD_TYPE_STRING || get_type_id(right) != LMD_TYPE_STRING) return false;
    String* ls = it2s(left);
    String* rs = it2s(right);
    if (!ls || !rs || ls->len != rs->len) return false;
    return memcmp(ls->chars, rs->chars, (size_t)ls->len) == 0;
}

static Item js_cjs_current_module(void) {
    if (js_cjs_module_stack_count <= 0) return ItemNull;
    return js_cjs_module_stack[js_cjs_module_stack_count - 1];
}

static Item js_cjs_find_module(Item filename) {
    for (int i = 0; i < js_cjs_module_count; i++) {
        if (js_cjs_same_string(js_cjs_module_names[i], filename)) return js_cjs_module_objects[i];
    }
    return ItemNull;
}

static void js_cjs_store_module(Item filename, Item module) {
    for (int i = 0; i < js_cjs_module_count; i++) {
        if (js_cjs_same_string(js_cjs_module_names[i], filename)) {
            js_cjs_module_objects[i] = module;
            return;
        }
    }
    if (js_cjs_module_count >= JS_CJS_MODULE_MAX) {
        log_error("cjs-metadata: module registry overflow (%d)", JS_CJS_MODULE_MAX);
        return;
    }
    js_cjs_module_names[js_cjs_module_count] = filename;
    js_cjs_module_objects[js_cjs_module_count] = module;
    js_cjs_module_count++;
}

static Item js_cjs_exports(Item module) {
    Item exports_key = js_cjs_key("exports");
    Item exports = js_get_key_default(module, exports_key);
    if (get_type_id(exports) == LMD_TYPE_NULL || get_type_id(exports) == LMD_TYPE_UNDEFINED) {
        exports = js_new_object();
        js_set_key_default(module, exports_key, exports);
    }
    return exports;
}

static Item js_cjs_children(Item module) {
    Item children_key = js_cjs_key("children");
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
    js_set_key_default(ns, js_cjs_key("default"), js_cjs_exports(module));
}

extern "C" Item js_cjs_enter(Item module, Item filename) {
    if (!js_cjs_register_roots()) return (Item){.item = ITEM_JS_UNDEFINED};
    if (get_type_id(module) != LMD_TYPE_MAP && get_type_id(module) != LMD_TYPE_OBJECT) {
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    js_set_key_default(module, js_cjs_key("id"), filename);
    js_set_key_default(module, js_cjs_key("filename"), filename);
    js_set_key_default(module, js_cjs_key("loaded"), (Item){.item = ITEM_FALSE});
    js_cjs_exports(module);
    js_cjs_children(module);
    Item parent = js_cjs_current_module();
    js_set_key_default(module, js_cjs_key("parent"), parent);
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
        js_set_key_default(module, js_cjs_key("loaded"), (Item){.item = ITEM_TRUE});
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
    js_set_key_default(module, js_cjs_key("id"), child_filename);
    js_set_key_default(module, js_cjs_key("filename"), child_filename);
    js_set_key_default(module, js_cjs_key("exports"), exports);
    js_set_key_default(module, js_cjs_key("loaded"), (Item){.item = ITEM_TRUE});
    js_set_key_default(module, js_cjs_key("children"), js_array_new(0));
    js_set_key_default(module, js_cjs_key("parent"), ItemNull);
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

char* js_wrap_cjs_source(const char* source, const char* filename) {
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
    //        export default __cjs_module__.exports;
    const char* prefix_fmt =
        "var __cjs_module__ = {exports: {}};\n"
        "var exports = __cjs_module__.exports;\n"
        "var module = __cjs_module__;\n"
        "var __filename = \"%s\";\n"
        "var __dirname = \"%.*s\";\n"
        "__lambda_cjs_enter(__cjs_module__, __filename);\n";
    const char* suffix =
        "\n__lambda_cjs_complete(__cjs_module__);\n"
        "__lambda_cjs_leave(__cjs_module__);\n"
        "export default __cjs_module__.exports;\n";

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
    Item existing = js_module_get(specifier);
    if (get_type_id(existing) != LMD_TYPE_NULL) {
        // For CJS modules, the cached value is the namespace.
        // Extract the default export (which is module.exports)
        Item def_key = (Item){.item = s2it(heap_create_name("default"))};
        Item def_val = js_get_key_default(existing, def_key);
        TypeId dt = get_type_id(def_val);
        if (dt != LMD_TYPE_NULL && dt != LMD_TYPE_UNDEFINED) {
            if (js_cjs_specifier_is_file_path(specifier)) js_cjs_note_child(specifier, def_val);
            return def_val;
        }
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

    Item resolved_spec = (Item){.item = s2it(heap_create_name(path_buf, strlen(path_buf)))};
    existing = js_module_get(resolved_spec);
    if (get_type_id(existing) != LMD_TYPE_NULL) {
        jm_clear_active_js_transpile(NULL, NULL, source);
        mem_free(source);
        Item def_key = (Item){.item = s2it(heap_create_name("default"))};
        Item def_val = js_get_key_default(existing, def_key);
        TypeId dt = get_type_id(def_val);
        if (dt != LMD_TYPE_NULL && dt != LMD_TYPE_UNDEFINED) {
            if (js_is_cjs_file(path_buf)) js_cjs_note_child(resolved_spec, def_val);
            return def_val;
        }
        if (js_is_cjs_file(path_buf)) js_cjs_note_child(resolved_spec, existing);
        return existing;
    }

    if (js_require_path_is_json(path_buf)) {
        Item json_text = (Item){.item = s2it(heap_create_name(source, strlen(source)))};
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
        char* wrapped = js_wrap_cjs_source(source, path_buf);
        jm_clear_active_js_transpile(NULL, NULL, source);
        mem_free(source);
        jm_track_active_js_transpile(NULL, NULL, wrapped);
        ns = transpile_js_module_to_mir(runtime, wrapped, path_buf);
        if (item_is_error(ns)) {
            // A synthetic JS try block changes a CJS file's top-level lexical
            // bindings into block bindings; close metadata here on abrupt exit
            // so the original source retains its Node/CommonJS lexical scope.
            js_cjs_leave(ItemNull);
        }
        jm_clear_active_js_transpile(NULL, NULL, wrapped);
        mem_free(wrapped);
    } else {
        // ESM — transpile as-is
        ns = transpile_js_module_to_mir(runtime, source, path_buf);
        jm_clear_active_js_transpile(NULL, NULL, source);
        mem_free(source);
    }

    if (get_type_id(ns) == LMD_TYPE_NULL) {
        log_error("require: failed to compile module '%s'", path_buf);
        return ItemNull;
    }

    // For CJS, extract the default export (module.exports)
    if (js_is_cjs_file(path_buf)) {
        Item def_key = (Item){.item = s2it(heap_create_name("default"))};
        Item def_val = js_get_key_default(ns, def_key);
        TypeId dt = get_type_id(def_val);
        if (dt != LMD_TYPE_NULL && dt != LMD_TYPE_UNDEFINED) {
            js_cjs_note_child(resolved_spec, def_val);
            return def_val;
        }
        js_cjs_note_child(resolved_spec, ns);
    }

    return ns;
}

static Item js_dynamic_import_reject_type_error(const char* message) {
    Item error_name = (Item){.item = s2it(heap_create_name("TypeError", 9))};
    Item error_message = (Item){.item = s2it(heap_create_name(message, (int)strlen(message)))};
    return js_promise_reject(js_new_error_with_name(error_name, error_message));
}

// dynamic import() — synchronous load, wrapped in a resolved Promise
extern "C" Item js_dynamic_import(Item specifier) {
    Item specifier_string = js_to_string(specifier);
    if (item_is_error(specifier_string) || get_type_id(specifier_string) != LMD_TYPE_STRING) {
        return js_promise_reject(specifier_string);
    }
    String* spec = it2s(specifier_string);
    if (!spec || spec->len == 0) {
        return js_dynamic_import_reject_type_error("import() requires a non-empty specifier");
    }

    // Js56 P10: dynamic `import(...)` is ES-module-only — CommonJS uses
    // `require()`. The shared js_require() path treats unmarked .js files as
    // CJS by default and extracts the CJS `default` export from the loaded
    // namespace; for ESM dynamic imports that strips the entire namespace
    // (`export var x = 42` ends up not on the returned object). Resolve
    // through the module cache and the ESM transpile path directly here so
    // the dynamic import returns the real namespace.
    js_dynamic_import_suppress_module_drain++;
    Item ns;
    Item existing = js_module_get(specifier_string);
    if (get_type_id(existing) != LMD_TYPE_NULL) {
        ns = existing;
    } else {
        char path_buf[512];
        snprintf(path_buf, sizeof(path_buf), "%.*s", (int)spec->len, spec->chars);
        char* source = read_text_file(path_buf);
        if (!source) {
            js_dynamic_import_suppress_module_drain--;
            char msg[256];
            snprintf(msg, sizeof(msg), "Cannot find module '%.*s'", (int)spec->len, spec->chars);
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
    if (item_is_error(ns)) {
        return js_promise_reject(ns);
    }
    if (get_type_id(ns) == LMD_TYPE_NULL) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot find module '%.*s'", (int)spec->len, spec->chars);
        return js_dynamic_import_reject_type_error(msg);
    }

    // Js57 P5: if the imported module (or any of its static dependencies)
    // had a pending TLA target captured, return a Promise chained on that
    // target so dynamic-import .then/.finally callbacks fire in spec order
    // (importing modules' callbacks fire after the underlying TLA settles).
    extern Item js_p5_chain_dynamic_import(Item, Item);
    Item awaited = js_module_get_awaited_target(specifier_string);
    if (get_type_id(awaited) != LMD_TYPE_NULL) {
        return js_p5_chain_dynamic_import(awaited, ns);
    }
    // Wrap the namespace in a resolved Promise
    return js_promise_resolve(ns);
}
