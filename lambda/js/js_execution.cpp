#include "js_execution_internal.hpp"
#include "js_interp.hpp"
#include "js_runtime_state.hpp"
#include "js_source_module.hpp"
#include "mvp-lmd/mvp_lmd.h"
#include "../jube/jube_registry.h"
#include "../jube/jube_interface.h"
#include "../module/node_core/node_runtime_state.hpp"
#include "../module/node_core/node_trace_events.hpp"
#undef js_input
#include "../input/input.hpp"
#include "../input/input-script-cache.h"
#include "../runtime/lambda-error.h"
#include "../runtime/recovery_frame.h"
#include "../runtime/mir_dump.h"
#include "../../lib/base64.h"
#include "../../lib/file.h"
#include "../../lib/mem_factory.h"
#include "../../lib/path_str.h"
#include "../../lib/str.h"
#include "../../lib/url.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
// the Win32 header declares GetTickCount64; omitting it leaves the native Windows build without the timing API declaration.
#include <windows.h>
#include <direct.h>
#define getcwd _getcwd
#define realpath(path, resolved) (_fullpath((resolved), (path), _MAX_PATH))
#endif
#ifndef _WIN32
#include <sys/time.h>
#include <unistd.h>
#endif
#include "js_runtime_internal.hpp"
#include "../../lib/time_util.h"

// Global MIR error handler override for batch mode.
// If non-NULL, installed after each jit_init() to prevent exit(1) on MIR errors.
MIR_error_func_t g_batch_mir_error_handler = NULL;

// Global MIR optimization level for JS compilation (default: 2).
// Set from CLI (e.g., --opt-level=0). Preamble always uses its own level.
unsigned int g_js_mir_optimize_level = 2;

// The Lambda Direct compiler still owns this legacy document policy. LambdaJS
// no longer reads it: D8.1.3v11 requires selected JS MIR to execute native code.
int g_js_force_document_interp = 0;

// diagnostic probes are shared by the host and native JS runtime.
static int g_js_diagnose_enabled = 0;

extern "C" void js_set_diagnose_enabled(int enabled) {
    g_js_diagnose_enabled = enabled ? 1 : 0;
}

extern "C" int js_is_diagnose_enabled(void) {
    return g_js_diagnose_enabled;
}

int js_dynamic_import_suppress_module_drain = 0;
extern "C" int js_batch_execution_mode = 0;
extern "C" int js_process_current_exit_code(void);
extern "C" void js_async_hooks_drain_destroy_queue(void);
extern "C" Item js_module_get_builtin(Item specifier);

static Item js_require_module_not_found(const char* specifier) {
    const char* name = specifier ? specifier : "";
    char message[640];
    snprintf(message, sizeof(message), "Cannot find module '%s'", name);
    Item error = js_new_error_with_name(js_make_string("Error"), js_make_string(message));
    js_set_key_cstr(error, "code", js_make_string("MODULE_NOT_FOUND"));
    // the returned error must stay attached to the call result; there is no
    // pending side channel for require callers to recover.
    return js_throw_value(error);
}

static JsMirPhaseTiming g_last_js_mir_phase_timing;
static JsMirPhaseTiming g_document_js_mir_phase_timing;
static bool g_document_js_mir_phase_timing_active = false;
static thread_local JsDocumentSourceLoadObserver g_document_source_load_observer = NULL;
static bool js_ast_is_es_module(JsAstNode* ast);
static bool js_test262_source_has_build_string_helper(const char* source,
    size_t source_len, const char* filename);

extern "C" void js_set_document_source_load_observer(
        JsDocumentSourceLoadObserver observer) {
    g_document_source_load_observer = observer;
}


static void js_mir_finish_script_turn(Runtime* runtime, Item result) {
    // D5.4.1: callbacks remain part of the active evaluator turn; marking this
    // boundary prevents synchronous layout from handing TLS to another realm.
    RuntimeExecutionScope execution_scope(context);
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

static Item js_mir_execute_retained_ast_script(Runtime* runtime, JsScript* script,
        char* owned_source, const char* js_source, size_t js_source_len,
        const char* filename, uint64_t* result_home, bool test262_native_harness) {
    if (!script) {
        js_clear_active_compile(NULL, owned_source);
        mem_free(owned_source);
        return ItemError;
    }
    // A cache hit keeps parse facts only; each ordinary source execution gets
    // fresh script-mode flags and rebuilds values in the active document realm.
    bool es_module = script->is_module || script->is_es_module ||
        js_ast_is_es_module((JsAstNode*)script->ast_root);
    script->is_module = script->is_es_module = es_module;
    script->is_eval_script = false;
    script->is_direct_eval = false;
    script->test262_native_harness = test262_native_harness;
    script->test262_native_build_string =
        js_test262_source_has_build_string_helper(js_source, js_source_len, filename);
    js_clear_active_compile(NULL, owned_source);
    mem_free(owned_source);
    // D8.5.1: an AST module's host turn owns its queued continuations. Keep
    // the runtime owner active through the first microtask drain so a nested
    // dynamic import can resume the module carrier after the executor returns.
    RuntimeExecutionScope module_execution_scope;
    if (module_execution_scope.is_outermost() &&
            !js_runtime_state.event_loop->callback_running &&
            js_dynamic_import_suppress_module_drain <= 0) {
        // This retained-AST entry owns the outer script turn. Initializing the
        // loop only in js_interp_execute_script misses it because that call
        // executes under this scope, leaving timer APIs without a uv loop.
        js_event_loop_init();
    }
    Item result = es_module
        ? js_interp_execute_es_module_script(runtime, script, result_home)
        : js_interp_execute_script(runtime, script, result_home);
    // This is the outer retained-AST module entry. Static imports use their
    // parent's turn, but the completed module must process its queued TLA
    // continuation before a Test262 batch returns to its async harness.
    if (es_module) {
        js_microtask_flush();
        Item evaluation_error = js_interp_es_module_evaluation_error(runtime, script);
        if (item_is_error(evaluation_error)) result = evaluation_error;
    }
    js_mir_finish_script_turn(runtime, result);
    return result;
}

static Item js_mir_execute_ast_script(Runtime* runtime, JsTranspiler* tp,
        char* owned_source, const char* js_source, size_t js_source_len,
        const char* filename, uint64_t* result_home, bool test262_native_harness,
        InputScriptBuildScope* cache_build) {
    // The AST tier owns the retained JsScript, but uses the same source parse,
    // early-error pass, Runtime catalog, and EvalContext setup as MIR lowering.
    js_clear_active_compile(tp, NULL);
    bool strict = tp && tp->ast_cache_requested_strict;
    bool typescript_profile = tp && tp->ast_cache_typescript_profile;
    bool module = tp && tp->is_module;
    JsScript* script = js_script_adopt_transpiler(tp, runtime, filename);
    bool published = script && (!cache_build ||
        cache_build->state != INPUT_SCRIPT_BUILD_POISONED) &&
        js_common_ast_cache_admit(runtime, script, js_source, js_source_len,
            filename, strict, typescript_profile, module);
    js_common_ast_cache_complete_build(cache_build, published, false);
    return js_mir_execute_retained_ast_script(runtime, script, owned_source,
        js_source, js_source_len, filename, result_home, test262_native_harness);
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
    if (context->runtime) runtime_set_name_pool(context->runtime, dynamic);
    return true;
}


static long js_mir_phase_now_us(void) {
    return (long)time_now_us();
}

static void js_mir_publish_compiler_pass_timing(const JsTranspiler* tp) {
    if (!tp || !tp->pass_timing.enabled) return;
    const JsCompilerPassTiming* timing = &tp->pass_timing;
    g_last_js_mir_phase_timing.parse_build_us = (long)timing->parse_build_us;
    g_last_js_mir_phase_timing.bind_us = (long)timing->bind_us;
    g_last_js_mir_phase_timing.validate_us = (long)timing->validate_us;
    g_last_js_mir_phase_timing.index_us = (long)timing->index_us;
    g_last_js_mir_phase_timing.collect_us = (long)timing->collect_us;
    g_last_js_mir_phase_timing.captures_us = (long)timing->captures_us;
    g_last_js_mir_phase_timing.env_layout_us = (long)timing->env_layout_us;
    g_last_js_mir_phase_timing.infer_us = (long)timing->infer_us;
    g_last_js_mir_phase_timing.forward_declare_us =
        (long)timing->forward_declare_us;
    g_last_js_mir_phase_timing.mir_lower_us = (long)timing->mir_lower_us;
    g_last_js_mir_phase_timing.finalize_us = (long)timing->finalize_us;
    g_last_js_mir_phase_timing.prelink_us = (long)timing->prelink_us;
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
    g_document_js_mir_phase_timing.realm_us += g_last_js_mir_phase_timing.realm_us;
    g_document_js_mir_phase_timing.total_us += g_last_js_mir_phase_timing.total_us;
    g_document_js_mir_phase_timing.parse_build_us += g_last_js_mir_phase_timing.parse_build_us;
    g_document_js_mir_phase_timing.bind_us += g_last_js_mir_phase_timing.bind_us;
    g_document_js_mir_phase_timing.validate_us += g_last_js_mir_phase_timing.validate_us;
    g_document_js_mir_phase_timing.index_us += g_last_js_mir_phase_timing.index_us;
    g_document_js_mir_phase_timing.collect_us += g_last_js_mir_phase_timing.collect_us;
    g_document_js_mir_phase_timing.captures_us += g_last_js_mir_phase_timing.captures_us;
    g_document_js_mir_phase_timing.env_layout_us += g_last_js_mir_phase_timing.env_layout_us;
    g_document_js_mir_phase_timing.infer_us += g_last_js_mir_phase_timing.infer_us;
    g_document_js_mir_phase_timing.forward_declare_us += g_last_js_mir_phase_timing.forward_declare_us;
    g_document_js_mir_phase_timing.mir_lower_us += g_last_js_mir_phase_timing.mir_lower_us;
    g_document_js_mir_phase_timing.finalize_us += g_last_js_mir_phase_timing.finalize_us;
    g_document_js_mir_phase_timing.prelink_us += g_last_js_mir_phase_timing.prelink_us;
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


extern "C" void js_mir_volume_counters_get(JsMirVolumeCounters* out) {
    if (out) *out = g_last_js_mir_volume;
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
            context->type_list = runtime && runtime_type_list(runtime)
                ? runtime_type_list(runtime)
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

Item js_compile_unit_fail(JsTranspiler* tp, char* owned_source) {
    js_clear_active_compile(tp, owned_source);
    js_transpiler_destroy(tp);
    mem_free(owned_source);
    return ItemError;
}

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
    JsProgramNode* program = ast && ast->node_type == AST_SCRIPT
        ? (JsProgramNode*)ast : NULL;
    for (JsAstNode* statement = program ? (JsAstNode*)program->body : NULL;
            statement; statement = (JsAstNode*)statement->next) {
        if (statement->node_type == AST_NODE_IMPORT ||
                statement->node_type == AST_NODE_EXPORT) return true;
    }
    return false;
}

static bool js_test262_source_has_build_string_helper(const char* source,
        size_t source_len, const char* filename) {
    // D8.2.5: mirror the established Test262 MIR helper contract when the
    // selected AST tier binds the source declaration into its runtime realm.
    if (!source || !filename ||
            (!strstr(filename, "ref/test262/test/") &&
             !strstr(filename, "test/js262/test/"))) return false;
    return js_source_contains_ascii(source, source_len,
               "function buildString(args)") &&
        js_source_contains_ascii(source, source_len,
               "const loneCodePoints = args.loneCodePoints;") &&
        js_source_contains_ascii(source, source_len,
               "const CHUNK_SIZE = 10000;") &&
        js_source_contains_ascii(source, source_len,
               "String.fromCodePoint.apply(null, loneCodePoints);");
}


static Item transpile_js_to_mir_core_profile_len(Runtime* runtime, const char* js_source,
                                                 size_t js_source_len, const char* filename,
                                                 uint64_t* result_home,
                                                 bool typescript_profile,
                                                 bool test262_native_harness, bool module = false) {
    if (!js_ast_interpreter_requested() && !js_execution_auto_requested()) {
        return mvp_lmd_execute_in_runtime(runtime, js_source, js_source_len, filename, result_home,
            module, NULL, typescript_profile, test262_native_harness);
    }
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

    char* owned_source = mem_dup_n(js_source, js_source_len, MEM_CAT_JS_RUNTIME);
    if (!owned_source) {
        log_error("js-mir: failed to allocate source buffer");
        return (Item){.item = ITEM_ERROR};
    }
    js_source = owned_source;
    js_track_active_compile(NULL, owned_source);

    // Inject __filename and __dirname for Node.js CommonJS compatibility.
    // Only for file-based scripts (not eval/REPL), and only if the source
    // doesn't already declare them (e.g., CJS-wrapped require'd modules).
    char* injected_source = NULL;
    if (!module && filename && filename[0] != '<' &&
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
        injected_source = mem_join3(js_source, insert_at, commonjs_header, (size_t)off,
                                    js_source + insert_at, js_source_len - insert_at,
                                    MEM_CAT_JS_RUNTIME);
        js_clear_active_compile(NULL, owned_source);
        mem_free(owned_source);
        owned_source = injected_source;
        js_track_active_compile(NULL, owned_source);
        js_source = injected_source;
        js_source_len += (size_t)off;
    }

    InputScriptBuildScope ast_cache_build = {};
    bool ast_executor_forced = js_ast_interpreter_forced();
    bool ast_executor_requested = js_ast_interpreter_requested() ||
        js_execution_auto_requested();
    bool ast_closure_ready = false;
    if (ast_executor_requested && filename && filename[0] != '<') {
        ast_closure_ready = js_module_ast_prebuild_imports(filename, js_source,
            js_source_len);
    }
    if (ast_executor_forced || runtime->js_ast_backend || ast_closure_ready) {
        JsScript* cached = js_common_ast_cache_lookup(runtime, js_source, js_source_len,
            filename, module || typescript_profile, typescript_profile, module);
        if (cached) {
            if (js_execution_auto_requested() && ast_closure_ready) {
                runtime->js_ast_backend = true;
            }
            uint64_t realm_start = js_realm_init_time_us();
            long execute_start = js_mir_phase_now_us();
            Item result = js_mir_execute_retained_ast_script(runtime, cached, owned_source,
                js_source, js_source_len, filename, result_home, test262_native_harness);
            g_last_js_mir_phase_timing.realm_us =
                (long)(js_realm_init_time_us() - realm_start);
            g_last_js_mir_phase_timing.execute_us = js_mir_phase_now_us() - execute_start -
                g_last_js_mir_phase_timing.realm_us;
            g_last_js_mir_phase_timing.total_us = js_mir_phase_now_us() - phase_total_start;
            return result;
        }
        InputScriptBuildClaim claim = js_common_ast_cache_begin_build(
            &ast_cache_build, js_source, js_source_len, filename,
            module || typescript_profile, typescript_profile, module);
        if (claim == INPUT_SCRIPT_BUILD_READY) {
            cached = js_common_ast_cache_lookup(runtime, js_source, js_source_len,
                filename, module || typescript_profile, typescript_profile, module);
            if (cached) {
                if (js_execution_auto_requested() && ast_closure_ready) {
                    runtime->js_ast_backend = true;
                }
                uint64_t realm_start = js_realm_init_time_us();
                long execute_start = js_mir_phase_now_us();
                Item result = js_mir_execute_retained_ast_script(runtime, cached,
                    owned_source, js_source, js_source_len, filename, result_home,
                    test262_native_harness);
                g_last_js_mir_phase_timing.realm_us =
                    (long)(js_realm_init_time_us() - realm_start);
                g_last_js_mir_phase_timing.execute_us = js_mir_phase_now_us() -
                    execute_start - g_last_js_mir_phase_timing.realm_us;
                g_last_js_mir_phase_timing.total_us = js_mir_phase_now_us() -
                    phase_total_start;
                return result;
            }
        }
    }

    // Create JS transpiler (for parsing and AST building)
    JsTranspiler* tp = js_transpiler_create(runtime);
    if (!tp) {
        log_error("js-mir: failed to create transpiler");
        js_common_ast_cache_complete_build(&ast_cache_build, false, false);
        js_clear_active_compile(NULL, owned_source);
        mem_free(owned_source);
        return (Item){.item = ITEM_ERROR};
    }
    if (typescript_profile) {
        // The fast TS preprocessor erases syntax, not the compilation profile;
        // losing this bit made the TS-only type() intrinsic look like a JS
        // property-miss synthesis dependency after D6.2.2v2 removed that path.
        tp->strict_js = false;
        tp->strict_mode = true;
    }
    tp->ast_cache_requested_strict = module || typescript_profile;
    tp->ast_cache_typescript_profile = typescript_profile;
    js_track_active_compile(tp, NULL);

    // Parse JavaScript source
    long phase_start = js_mir_phase_now_us();
    JsParseMode mode = module ? JS_PARSE_MODULE : JS_PARSE_AUTO;
    if (module && typescript_profile) mode = (JsParseMode)(JS_PARSE_MODULE | JS_PARSE_TYPESCRIPT);
    if (!js_transpiler_parse_c(tp, js_source, js_source_len, mode)) {
        int64_t error_row = 0;
        int64_t error_col = 0;
        char error_message[128];
        if (js_transpiler_parse_error_get(tp, &error_row, &error_col,
                error_message, sizeof(error_message))) {
            log_error("js-mir: parse failed at %lld:%lld: %s",
                (long long)error_row, (long long)error_col, error_message);
        } else {
            log_error("js-mir: parse failed");
        }
        js_common_ast_cache_complete_build(&ast_cache_build, false, false);
        return js_compile_unit_fail(tp, owned_source);
    }
    g_last_js_mir_phase_timing.parse_us = js_mir_phase_now_us() - phase_start;
    js_mir_publish_compiler_pass_timing(tp);
    log_mem_stage("js-core: ts_parsed");

    // Build JavaScript AST
    phase_start = js_mir_phase_now_us();
    JsAstNode* js_ast = (JsAstNode*)tp->ast_root;
    if (!js_ast) {
        log_error("js-mir: AST build failed");
        js_common_ast_cache_complete_build(&ast_cache_build, false, false);
        return js_compile_unit_fail(tp, owned_source);
    }
    g_last_js_mir_phase_timing.ast_us = js_mir_phase_now_us() - phase_start;
    log_mem_stage("js-core: ast_built");

    if (tp->has_errors) {
        log_error("js-mir: early error(s) detected");
        js_common_ast_cache_complete_build(&ast_cache_build, false, false);
        return js_compile_unit_fail(tp, owned_source);
    }

    bool ast_executor_supported = js_interp_script_is_supported((JsScript*)tp);
    bool auto_ast_selected = js_execution_auto_requested() &&
        ast_executor_supported;
    if (ast_executor_forced ||
            (runtime->js_ast_backend && ast_executor_supported) ||
            auto_ast_selected) {
        if (auto_ast_selected) runtime->js_ast_backend = true;
        // a nested compile reuses the same counters; retain this source's
        // record and separate realm construction from AST execution.
        JsMirPhaseTiming timing = g_last_js_mir_phase_timing;
        uint64_t realm_start = js_realm_init_time_us();
        phase_start = js_mir_phase_now_us();
        Item result = js_mir_execute_ast_script(runtime, tp, owned_source,
            js_source, js_source_len, filename, result_home,
            test262_native_harness,
            &ast_cache_build);
        timing.realm_us = (long)(js_realm_init_time_us() - realm_start);
        timing.execute_us = js_mir_phase_now_us() - phase_start - timing.realm_us;
        timing.total_us = js_mir_phase_now_us() - phase_total_start;
        g_last_js_mir_phase_timing = timing;
        return result;
    }
    if (js_execution_auto_requested()) {
        log_info("js-auto: MIR fallback script=%s", filename ? filename : "<string>");
    }

    // AUTO opened an AST build claim before it discovered this script requires
    // the MIR tier. Release that claim now; leaving its scope live retains the
    // cache lease until process exit even though no AST artifact was published.
    js_common_ast_cache_complete_build(&ast_cache_build, false, false);
    bool execute_module = module || tp->is_module || js_ast_is_es_module(js_ast);
    js_clear_active_compile(tp, owned_source);
    js_transpiler_destroy(tp);
    Item result = mvp_lmd_execute_in_runtime(runtime, owned_source, js_source_len,
        filename, result_home, execute_module, NULL, typescript_profile, test262_native_harness);
    mem_free(owned_source);
    return result;
}

Item transpile_js_to_mir_core_len(Runtime* runtime, const char* js_source,
                                  size_t js_source_len, const char* filename,
                                  uint64_t* result_home) {
    return transpile_js_to_mir_core_profile_len(runtime, js_source, js_source_len,
                                                filename, result_home, false, false);
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
    return transpile_js_to_mir_core_len(runtime, js_source, js_source_len, filename, result_home);
}

Item transpile_js_to_mir_test262_native_len(Runtime* runtime, const char* js_source,
                                            size_t js_source_len, const char* filename,
                                            uint64_t* result_home) {
    return transpile_js_to_mir_core_profile_len(runtime, js_source, js_source_len,
                                                filename, result_home, false, true);
}

Item transpile_js_typescript_to_mir_len(Runtime* runtime, const char* js_source,
                                        size_t js_source_len, const char* filename,
                                        uint64_t* result_home) {
    return transpile_js_to_mir_core_profile_len(runtime, js_source, js_source_len,
                                                filename, result_home, true, false);
}

Item transpile_js_module_to_mir(Runtime* runtime, const char* source, const char* filename) {
    return transpile_js_to_mir_core_profile_len(runtime, source, strlen(source),
        filename, NULL, false, false, true);
}


Item transpile_js_to_mir_preamble_len(Runtime* runtime, const char* source, size_t length,
        const char* filename, JsPreambleState* output, uint64_t* result_home) {
    Item result = transpile_js_to_mir_core_len(runtime, source, length, filename, result_home);
    if (output && !item_is_error(result)) {
        output->mvp_realm = !js_ast_interpreter_requested() && !js_execution_auto_requested();
        if (lambda_active_module_state_id() == UINT32_MAX &&
                !lambda_module_state_reserve_and_activate(0)) return ItemError;
        output->module_state_id = lambda_active_module_state_id();
    }
    return result;
}
Item compile_js_mir_preamble_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                                 const char* filename, JsPreambleState* out_state) {
    // open Scripts resolve declarations through the receiving realm; cached
    // machine code never borrows a document's live binding cells.
    return mvp_lmd_compile_script_image(js_source, js_source_len, filename, out_state);
}

Item compile_js_mir_with_preamble_len(Runtime* runtime, const char* js_source,
                                      size_t js_source_len, const char* filename,
                                      const JsPreambleState* preamble,
                                      JsPreambleState* out_state) {
    if (!preamble) return ItemError;
    return mvp_lmd_compile_script_image(js_source, js_source_len, filename, out_state);
}

Item execute_compiled_js_in_current_realm(Runtime* runtime,
        const JsPreambleState* base, const JsPreambleState* compiled, bool retain_unit_state) {
    (void)retain_unit_state;
    if (!base || !base->mvp_realm || !compiled || !compiled->mvp_code) return ItemError;
    return mvp_lmd_execute_script_image(runtime, compiled->mvp_code);
}
Item transpile_js_to_mir_with_preamble_len(Runtime* runtime, const char* source, size_t length,
        const char* filename, const JsPreambleState* preamble, uint64_t* result_home) {
    if (preamble && preamble->mvp_realm)
        return mvp_lmd_execute_in_runtime(runtime, source, length, filename, result_home);
    return transpile_js_to_mir_core_len(runtime, source, length, filename, result_home);
}
bool clone_js_preamble_state(const JsPreambleState* source, JsPreambleState* output) {
    if (!source || !source->mvp_code || !output) return false;
    *output = *source;
    mvp_lmd_code_retain(output->mvp_code);
    output->module_state_id = UINT32_MAX;
    return true;
}
Item instantiate_js_preamble(Runtime* runtime, const JsPreambleState* cached,
        JsPreambleState* output) {
    if (!runtime || !clone_js_preamble_state(cached, output)) return ItemError;
    Item result = mvp_lmd_execute_script_image(runtime, cached->mvp_code);
    if (item_is_error(result)) preamble_state_destroy(output);
    return result;
}
bool preamble_state_update_from_eval_snapshot(JsPreambleState* state) {
    return state && state->mvp_realm;
}
bool preamble_state_update_from_compiled(JsPreambleState* state, const JsPreambleState* compiled) {
    return state && state->mvp_realm && compiled && compiled->mvp_realm;
}
void preamble_state_destroy(JsPreambleState* state) {
    if (!state) return;
    if (state->mvp_code) mvp_lmd_code_release(state->mvp_code);
    else if (state->owns_compiled_state && state->mir_ctx) MIR_finish((MIR_context_t)state->mir_ctx);
    memset(state, 0, sizeof(*state));
    state->module_state_id = UINT32_MAX;
}

char* js_load_script_source_from_cache(const char* path,
        const char* profile, const char* execution_mode, bool module_mode,
        size_t* out_length, InputResourcePolicy resource_policy) {
    if (out_length) *out_length = 0;
    if (!path || !path[0]) return NULL;
    if (!input_resource_policy_admits(resource_policy, path)) {
        log_info("resource-admission: denied script source %s", path);
        return NULL;
    }

    bool is_http = js_path_is_http_url(path);
    bool is_data = is_data_uri(path);
    bool is_file_url = strncmp(path, "file:", 5) == 0;
    lam::Temp<char> local_path(url_file_to_local_path(path));
    if (is_file_url && !local_path) return NULL;
    // inline document modules resolve to file URLs; decode them before filesystem/cache access.
    const char* read_path = local_path ? local_path.get() : path;
    char* canonical = (is_http || is_data) ? NULL : file_realpath(read_path);
    const char* identity = canonical ? canonical : path;
    InputScriptRequest request = {};
    request.identity = identity;
    request.source_kind = (is_http || is_data) ? INPUT_SCRIPT_SOURCE_URL : INPUT_SCRIPT_SOURCE_FILE;
    request.language = "javascript";
    request.profile = profile ? profile : "js-module";
    request.parser_abi = "js-direct-parser-v1";
    request.parse_flags = module_mode ? "module" : "classic";
    request.resolution_base = identity;
    request.backend = "mir-direct";
    request.execution_mode = execution_mode
        ? execution_mode : (module_mode ? "module" : "classic");
    request.ast_abi = 1;
    request.compiler_abi = 1;
    request.interface_abi = jube_interface_registry_digest();
    request.dependency_digest = jube_interface_registry_digest();
    request.optimize_level = g_js_mir_optimize_level;
    request.module_mode = module_mode;

    char* source = NULL;
    if (is_http || is_data) {
        // embedded scripts share URL snapshot ownership instead of treating their payload as a filename.
        size_t source_length = 0;
        char* downloaded = is_data
            ? (char*)parse_data_uri(path, NULL, 0, &source_length)
            : download_http_content_cached(path, &source_length, "./temp/cache");
        if (downloaded) {
            source = input_script_cache_copy_source(
                input_manager_global_script_cache(), &request, downloaded,
                source_length, out_length);
            mem_free(downloaded);
        }
    } else {
        source = input_script_cache_copy_file_source(
            input_manager_global_script_cache(), &request, read_path, out_length);
    }
    if (canonical) mem_free(canonical);
    if (!source) {
        log_error("js-script-cache: failed to acquire source %s", path);
    } else if (g_document_source_load_observer && out_length) {
        g_document_source_load_observer(*out_length);
    }
    return source;
}

// ============================================================================
// Public API: load a JS file as a module for cross-language import
// ============================================================================

Item load_js_module(Runtime* runtime, const char* js_path) {
    log_info("js-mir: loading JS module '%s' for cross-language import", js_path);
    if (runtime) runtime->js_runtime_used = true;
    char* source = js_load_script_source_from_cache(
        js_path, "js-cross-language-module", "module", true, NULL,
        runtime ? runtime->resource_policy : INPUT_RESOURCE_ALLOW_NETWORK);
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
            js_clear_active_compile(NULL, source);
            mem_free(source);
            return ItemNull;
        }
        if (!eval_context_init(js_context)) {
            js_clear_active_compile(NULL, source);
            mem_free(source);
            return ItemNull;
        }
        heap_init();
        context->pool = context->heap->pool;
        context->name_pool = name_pool_create_runtime(context->pool);
        context->type_list = arraylist_new(64);
        context->runtime = runtime;
        if (!js_runtime_state_init(context)) {
            js_clear_active_compile(NULL, source);
            mem_free(source);
            return ItemNull;
        }

        // The canonical context owns the bootstrap heap through runner setup
        // (SCU15: `context` IS runtime_get_eval_context here, so nothing else
        // records it).
        runtime_context_publish_owners(runtime, context);

        // Create Input context for JS runtime
        Input* js_input = Input::create(context->pool);
        js_runtime_set_input(js_input);
        log_debug("js-mir: created persistent heap for cross-language module loading");
    }

    js_track_active_compile(NULL, source);
    Item ns = transpile_js_module_to_mir(runtime, source, js_path);
    js_clear_active_compile(NULL, source);
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

static char* js_require_read_source(const char* path) {
    Runtime* runtime = js_current_runtime();
    if (runtime && !input_resource_policy_admits(runtime->resource_policy, path)) return NULL;
    if (js_require_path_is_json(path)) return read_text_file(path);
    bool module_mode = !js_is_cjs_file(path);
    return js_load_script_source_from_cache(path, "js-require",
        module_mode ? "module" : "cjs", module_mode, NULL,
        runtime ? runtime->resource_policy : INPUT_RESOURCE_ALLOW_NETWORK);
}

static char* js_require_read_resolved_path_internal(char* path_buf, int path_buf_size,
        bool allow_package_main);

static void js_require_normalize_lexical_path(char* path_buf, int path_buf_size) {
    if (!path_buf || path_buf_size <= 0) return;

    js_normalize_path_separators(path_buf);
    char normalized[512];
    if (path_str_normalize_lexical_posix(path_buf, normalized,
            sizeof(normalized), false) < 0) {
        path_buf[0] = '\0';
        return;
    }
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

    char* source = js_require_read_source(path_buf);
    if (source) {
        js_require_canonicalize_existing_path(path_buf, path_buf_size);
        return source;
    }

    int len = (int)strlen(original);
    bool has_node_prefix = (len >= 5 && strncmp(original, "node:", 5) == 0);
    if (!has_node_prefix && !js_require_path_has_known_extension(original) &&
            len + 3 < path_buf_size) {
        snprintf(path_buf, path_buf_size, "%s.js", original);
        source = js_require_read_source(path_buf);
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
        str_cat(path_buf, plen, path_buf_size, "/index.js", sizeof("/index.js") - 1);
        source = js_require_read_source(path_buf);
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
#define js_cjs_module_stack_at(i) (*root_vector_at(&js_cjs_module_stack_state, (i)))
#define js_cjs_module_stack_count ((int)root_vector_count(&js_cjs_module_stack_state))

extern "C" void js_cjs_metadata_reset(void) {
    JsCjsState* state = js_cjs_state_current(false);
    if (state) root_vector_clear(&state->module_stack);
}

static Item js_cjs_current_module(void) {
    if (js_cjs_module_stack_count <= 0) return ItemNull;
    return js_cjs_module_stack_at(js_cjs_module_stack_count - 1);
}

static Item js_cjs_find_module(Item filename) {
    if (get_type_id(filename) != LMD_TYPE_STRING) return ItemNull;
    String* spec = it2s(filename);
    ModuleDescriptor* desc = module_get_for_runtime(
        context ? context->runtime : NULL, spec ? spec->chars : NULL);
    if (!desc) return ItemNull;
    if (get_type_id(desc->cjs_module) == LMD_TYPE_MAP) return desc->cjs_module;
    return ItemNull;
}

static void js_cjs_store_module(Item filename, Item module) {
    if (get_type_id(filename) != LMD_TYPE_STRING) return;
    String* spec = it2s(filename);
    if (!spec) return;
    Runtime* runtime = context ? context->runtime : NULL;
    ModuleDescriptor* existing = module_get_for_runtime(runtime, spec->chars);
    if (existing && existing->source_lang &&
            strcmp(existing->source_lang, "js") != 0 &&
            strcmp(existing->source_lang, "js-cjs") != 0) {
        // another language owns this path; CommonJS metadata never rebrands it.
        return;
    }
    if (!existing) {
        // metadata-only child (never loaded through the JS loader): the CJS
        // module object is also the descriptor's namespace.
        module_register_for_runtime(runtime, spec->chars, "js-cjs", module, NULL);
        existing = module_get_for_runtime(runtime, spec->chars);
        if (!existing) return;
    }
    // The loader marks a file as "js" before its body runs and re-registers
    // the ESM namespace after it, so the CJS `module` object cannot ride in
    // namespace_obj under a language tag: it was dropped on both writes.
    // It is a second fact of the same descriptor.
    module_descriptor_set_cjs_module(existing, module);
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
    // No CJS module object means the namespace already carries the loader's
    // `default`; replacing it with a fresh {} is how exports went missing.
    if (get_type_id(module) != LMD_TYPE_MAP) return;
    Item ns = js_module_get(filename);
    if (get_type_id(ns) != LMD_TYPE_MAP) return;
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
    if (get_type_id(module) != LMD_TYPE_MAP) {
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, module), path(roots, filename), bound_require(roots, js_module_create_require(filename));
    JS_RETURN_IF_ERROR(bound_require.get());
    JS_RETURN_IF_ERROR(js_set_key_cstr(owner.get(), "require", bound_require.get()));
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
    if (!root_vector_push(&js_cjs_module_stack_state, module)) {
        log_error("cjs-metadata: could not grow module stack");
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_cjs_complete(Item module) {
    if (get_type_id(module) == LMD_TYPE_MAP) {
        js_set_key_default(module, js_cjs_key("loaded", (int)strlen("loaded")), (Item){.item = ITEM_TRUE});
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_cjs_leave(Item module) {
    if (js_cjs_module_stack_count > 0) {
        if (module.item == ItemNull.item ||
            js_cjs_module_stack_at(js_cjs_module_stack_count - 1).item == module.item) {
            root_vector_pop(&js_cjs_module_stack_state);
        } else {
            for (int i = js_cjs_module_stack_count - 1; i >= 0; i--) {
                if (js_cjs_module_stack_at(i).item != module.item) continue;
                for (int j = i + 1; j < js_cjs_module_stack_count; j++) {
                    js_cjs_module_stack_at(j - 1) = js_cjs_module_stack_at(j);
                }
                root_vector_shrink(&js_cjs_module_stack_state,
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
    if (get_type_id(parent) != LMD_TYPE_MAP) return;
    Item child = js_cjs_find_module(child_filename);
    if (get_type_id(child) != LMD_TYPE_MAP) {
        child = js_cjs_create_module_metadata(child_filename, child_exports);
    }
    if (get_type_id(child) != LMD_TYPE_MAP) return;
    Item children = js_cjs_children(parent);
    JS_ARRAY_FOREACH(existing, children) {
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
        "__lambda_cjs_enter(__cjs_module__, __filename);\n"
        "var require = module.require;\n";
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

extern "C" Item js_require_at(Item specifier, const char* reference) {
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> source(roots, specifier);
    if (!reference || reference[0] == '<' || get_type_id(source.get()) != LMD_TYPE_STRING)
        return js_require(source.get());
    Item builtin = js_module_get_builtin(source.get());
    if (get_type_id(builtin) != LMD_TYPE_NULL) return builtin;
    String* text = source.get().get_string();
    char resolved[2048];
    // use the same document/file resolution and resource policy as ES module requests.
    js_resolve_script_module_path(js_current_runtime(), reference, text->chars, text->len,
        resolved, sizeof(resolved));
    source.set(js_name_item(resolved));
    return item_is_error(source.get()) ? source.get() : js_require(source.get());
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

    // retained namespaces obey the same admission as newly acquired source.
    Runtime* active_runtime = js_current_runtime();
    if (active_runtime && !input_resource_policy_admits(active_runtime->resource_policy, spec->chars))
        return js_require_module_not_found(spec->chars);

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
    js_track_active_compile(NULL, source);

    Item resolved_spec = js_name_item(path_buf, strlen(path_buf));
    existing = js_cjs_cached_value(resolved_spec);
    if (get_type_id(existing) != LMD_TYPE_NULL) {
        js_clear_active_compile(NULL, source);
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
        js_clear_active_compile(NULL, source);
        mem_free(source);
        return ItemNull;
    }

    Item ns;
    if (js_is_cjs_file(path_buf)) {
        // Wrap CJS source with module/exports globals
        char* wrapped = js_ast_interpreter_requested()
            ? js_wrap_cjs_source_for_ast(source, path_buf)
            : js_wrap_cjs_source(source, path_buf);
        js_clear_active_compile(NULL, source);
        mem_free(source);
        js_track_active_compile(NULL, wrapped);
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
        js_clear_active_compile(NULL, wrapped);
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
        js_clear_active_compile(NULL, source);
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

static Item js_execute_dynamic_import_module(Runtime* runtime,
        const char* source, const char* filename) {
    if (js_ast_interpreter_requested()) {
        return js_interp_execute_es_module_source(runtime, source, strlen(source),
            filename, NULL);
    }
    return transpile_js_module_to_mir(runtime, source, filename);
}

struct JsSourceImportPolicy {
    const JsSourceModuleOps* source_ops;
    void* source_owner;
};
static Item js_source_dynamic_import_job(Item callee, Item, Item*, int, uint64_t*) {
    auto* function = (JsFunction*)callee.function;
    Item* slots = function->env;
    if (!slots) return ItemError;
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> request(roots, slots[0]), path(roots, slots[1]), status(roots, ItemNull), ns(roots, ItemNull);
    // queued imports retain their originating policy across reentrant realm changes.
    const auto* policy = (const JsSourceImportPolicy*)(uintptr_t)js_fn_native(function)->target.bits;
    Runtime* runtime = policy->source_ops->ensure_runtime(policy->source_owner);
    if (!runtime) status.set(policy->source_ops->error(policy->source_owner,
        JS_SOURCE_MODULE_TYPE, "import(): module runtime unavailable"));
    JsSourceModuleRecord* record = NULL;
    if (!item_is_error(status.get())) status.set(js_module_load_link(runtime, policy->source_ops,
        policy->source_owner, it2s(path.get())->chars, NULL, 0, &record));
    if (!item_is_error(status.get())) status.set(js_module_evaluate(record));
    if (!item_is_error(status.get())) ns.set(js_module_namespace_create(record));
    if (item_is_error(ns.get())) status.set(ns.get());
    if (!item_is_error(status.get())) {
        ModuleDescriptor* completion = record->descriptor->async_cycle_root
            ? record->descriptor->async_cycle_root : record->descriptor;
        if (item_is_error(completion->evaluation_error)) status.set(completion->evaluation_error);
        else status.set(js_p5_chain_dynamic_import(completion->awaited_target, ns.get(), request.get()));
    }
    if (item_is_error(status.get())) {
        status.set(policy->source_ops->error_value(policy->source_owner, status.get()));
        js_promise_reject_existing(request.get(), status.get());
    }
    return make_js_undefined();
}

Item js_source_dynamic_import(Item specifier, const char* reference,
        const JsSourceModuleOps* ops, void* owner) {
    RootFrame roots(5);
    if (!roots.valid()) return ItemError;
    Rooted<Item> value(roots, js_retain_scalar(specifier)), request(roots, js_promise_create_pending()),
        path(roots, ItemNull), callback(roots, ItemNull), failure(roots, ItemNull);
    if (item_is_error(request.get())) return request.get();
    const JsSourceImportPolicy policy_value = {ops, owner};
    const JsSourceImportPolicy* policy = &policy_value;
    value.set(ops->to_string(owner, value.get()));
    if (item_is_error(value.get())) failure.set(value.get());
    else {
        Runtime* runtime = policy->source_ops->ensure_runtime(policy->source_owner);
        String* text = it2s(value.get());
        if (!runtime || !text || memchr(text->chars, 0, text->len)) {
            failure.set(policy->source_ops->error(policy->source_owner, JS_SOURCE_MODULE_TYPE,
                "import(): invalid module request"));
        } else {
            char resolved[4096];
            js_resolve_script_module_path(runtime, reference, text->chars, (int)text->len,
                resolved, sizeof(resolved));
            if (!input_resource_policy_admits(runtime->resource_policy, resolved)) {
                failure.set(ops->error(owner, JS_SOURCE_MODULE_TYPE,
                    "import(): module blocked by document resource policy"));
            } else {
                path.set(js_make_string(resolved));
                if (item_is_error(path.get())) failure.set(path.get());
            }
        }
    }
    if (!item_is_error(failure.get())) {
        auto* retained = (JsSourceImportPolicy*)pool_alloc(context->pool, sizeof(JsSourceImportPolicy));
        if (!retained) failure.set(ItemError);
        else {
            *retained = policy_value;
            Item* environment = js_alloc_env2(request.get(), path.get());
            callback.set(environment ? js_new_native_payload_function(js_source_dynamic_import_job,
                (uint64_t)(uintptr_t)retained, 0, environment, 2) : ItemError);
            const JsPromiseHostOps* host = js_runtime_state.promises.host_ops;
            if (!item_is_error(callback.get()) && host && host->wrap_job)
                callback.set(host->wrap_job(js_runtime_state.promises.host_context,
                    callback.get(), make_js_undefined()));
            if (item_is_error(callback.get())) failure.set(callback.get());
            else js_microtask_enqueue(callback.get());
        }
    }
    if (item_is_error(failure.get())) {
        failure.set(policy->source_ops->error_value(policy->source_owner, failure.get()));
        js_promise_reject_existing(request.get(), failure.get());
    }
    return request.get();
}

extern "C" Item js_dynamic_import(Item specifier) {
    return js_dynamic_import_at(specifier, context ? context->current_file : NULL);
}

// source policies queue their frontend's loader; the default LJS path stays unchanged.
extern "C" Item js_dynamic_import_at(Item specifier, const char* reference) {
    if (js_active_runtime_state && js_runtime_state.modules.source_ops)
        return js_source_dynamic_import(specifier, reference, js_runtime_state.modules.source_ops,
            js_runtime_state.modules.source_owner);
    RootFrame roots(5);
    Rooted<Item> specifier_string_root(roots, js_to_string(specifier));
    if (item_is_error(specifier_string_root.get()) ||
            get_type_id(specifier_string_root.get()) != LMD_TYPE_STRING) {
        // Promise rejections carry the JavaScript exception value, never the
        // runtime's internal ERROR lane. This matters for `import(import.meta)`:
        // ToString throws TypeError and a catch handler must observe that object.
        Item reason = item_is_error(specifier_string_root.get())
            ? js_error_lane_payload(specifier_string_root.get())
            : specifier_string_root.get();
        return js_promise_reject(reason);
    }
    String* spec = it2s(specifier_string_root.get());
    if (!spec || spec->len == 0) {
        return js_dynamic_import_reject_type_error("import() requires a non-empty specifier");
    }

    char resolved_path[2048];
    const char* base_file = reference;
    if (base_file && base_file[0] && base_file[0] != '<') {
        jm_resolve_module_path(base_file, spec->chars, (int)spec->len,
                               resolved_path, (int)sizeof(resolved_path));
    } else if (jm_resolve_document_module_path(context ? context->runtime : NULL,
            base_file, spec->chars, (int)spec->len, resolved_path,
            (int)sizeof(resolved_path))) {
        // The shared resolver selected the active document URL.
    } else {
        snprintf(resolved_path, sizeof(resolved_path), "%.*s",
                 (int)spec->len, spec->chars);
    }
    // reject before a warm namespace can bypass the active document's policy.
    Runtime* active_runtime = js_current_runtime();
    if (active_runtime && !input_resource_policy_admits(active_runtime->resource_policy, resolved_path)) {
        return js_dynamic_import_reject_type_error("import(): module blocked by document resource policy");
    }
    Rooted<Item> resolved_spec_root(roots, js_make_string(resolved_path));

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
        char* source = js_load_script_source_from_cache(
            path_buf, "js-dynamic-import", "module", true, NULL,
            js_current_runtime() ? js_current_runtime()->resource_policy : INPUT_RESOURCE_ALLOW_NETWORK);
        if (!source) {
            js_dynamic_import_suppress_module_drain--;
            char msg[256];
            snprintf(msg, sizeof(msg), "Cannot find module '%s'", resolved_path);
            return js_dynamic_import_reject_type_error(msg);
        }
        js_track_active_compile(NULL, source);
        Runtime* runtime = js_current_runtime();
        if (!runtime) {
            js_clear_active_compile(NULL, source);
            mem_free(source);
            js_dynamic_import_suppress_module_drain--;
            return js_dynamic_import_reject_type_error("import(): no runtime available");
        }
        ns = js_execute_dynamic_import_module(runtime, source, path_buf);
        js_clear_active_compile(NULL, source);
        mem_free(source);
    }
    js_dynamic_import_suppress_module_drain--;
    Rooted<Item> namespace_root(roots, ns);
    if (js_module_needs_async_settle(resolved_spec_root.get())) {
        js_tla_flush_for_dynamic_import();
    }
    if (item_is_error(ns)) {
        return js_promise_reject(js_error_lane_payload(ns));
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
        return js_p5_chain_dynamic_import(awaited, namespace_root.get(), ItemNull);
    }
    // Wrap the namespace in a resolved Promise
    return js_promise_resolve(namespace_root.get());
}
