#include "js_mir_internal.hpp"
#include "../../lib/mem_factory.h"
#ifndef _WIN32
#include <sys/time.h>
#else
#include <windows.h>
#endif

int js_dynamic_import_suppress_module_drain = 0;

static JsMirPhaseTiming g_last_js_mir_phase_timing;

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

Item transpile_js_ast_to_mir(Runtime* runtime, JsTranspiler* tp, JsAstNode* ast, const char* filename) {
    log_debug("js-mir-ast: transpiling pre-built AST for '%s'", filename ? filename : "<string>");

    js_source_runtime = runtime;

    // set up evaluation context
    EvalContext js_context;
    memset(&js_context, 0, sizeof(EvalContext));
    EvalContext* old_context = context;
    bool reusing_context = false;

    if (old_context && old_context->heap) {
        context = old_context;
        reusing_context = true;
        if (!context->nursery) {
            context->nursery = mem_nursery_create(NULL, 0, MEM_ROLE_RUNTIME_HEAP, "js.nursery");
        }
        if (!context->type_list) {
            context->type_list = runtime && runtime->type_list
                ? runtime->type_list
                : arraylist_new(64);
        }
    } else {
        js_context.nursery = mem_nursery_create(NULL, 0, MEM_ROLE_RUNTIME_HEAP, "js.nursery");
        context = &js_context;
        if (runtime->reuse_pool) {
            heap_init_with_pool(runtime->reuse_pool);
            runtime->reuse_pool = NULL;
        } else {
            heap_init();
        }
        context->pool = context->heap->pool;
        context->name_pool = name_pool_create(context->pool, nullptr);
        context->type_list = arraylist_new(64);
    }

    _lambda_rt = (Context*)context;

    Input* js_input = Input::create(context->pool);
    js_runtime_set_input(js_input);

    // initialize MIR context
    MIR_context_t ctx = jit_init(g_js_mir_optimize_level);
    if (!ctx) {
        log_error("js-mir-ast: MIR context init failed");
        context = old_context;
        return (Item){.item = ITEM_ERROR};
    }

    // set up MIR transpiler
    JsMirTranspiler* mt = jm_create_mir_transpiler(tp, ctx, filename, false, 64, 32, 16, "js-mir-ast");
    if (!mt) {
        MIR_finish(ctx);
        context = old_context;
        return (Item){.item = ITEM_ERROR};
    }

    mt->module = MIR_new_module(ctx, "ts_script");

    // transpile AST to MIR
    transpile_js_mir_ast(mt, ast);

#ifndef NDEBUG
    if (getenv("JS_MIR_DUMP")) {
    create_dir_recursive("temp");
    FILE* mir_dump = fopen("temp/ts_mir_dump.txt", "w");
    if (mir_dump) {
        bool dump_safe = true;
        for (MIR_module_t m = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(ctx)); m != NULL;
             m = DLIST_NEXT(MIR_module_t, m)) {
            for (MIR_item_t item = DLIST_HEAD(MIR_item_t, m->items); item != NULL;
                 item = DLIST_NEXT(MIR_item_t, item)) {
                if (item->item_type == MIR_func_item) {
                    MIR_func_t func = item->u.func;
                    for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, func->insns); insn != NULL;
                         insn = DLIST_NEXT(MIR_insn_t, insn)) {
                        for (size_t i = 0; i < insn->nops; i++) {
                            if (insn->ops[i].mode == MIR_OP_LABEL && insn->ops[i].u.label == NULL) {
                                dump_safe = false;
                                break;
                            }
                        }
                        if (!dump_safe) break;
                    }
                }
                if (!dump_safe) break;
            }
            if (!dump_safe) break;
        }
        if (dump_safe) {
            MIR_output(ctx, mir_dump);
        } else {
            fprintf(mir_dump, "ERROR: NULL labels detected, dump skipped\n");
        }
        fclose(mir_dump);
    }
    } // JS_MIR_DUMP
#endif

    if (!jm_validate_mir_labels(ctx)) {
        log_error("js-mir-ast: NULL labels detected");
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    MIR_link(ctx, g_mir_interp_mode ? MIR_set_interp_interface : MIR_set_gen_interface, import_resolver);

    typedef Item (*js_main_func_t)(Context*);
    js_main_func_t js_main = (js_main_func_t)find_func(ctx, (char*)"js_main");

    if (!js_main) {
        log_error("js-mir-ast: failed to find js_main");
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        context = old_context;
        return (Item){.item = ITEM_ERROR};
    }

    // execute
    log_debug("js-mir-ast: executing JIT compiled code");
    js_set_active_module_vars(js_alloc_module_vars());
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
                double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
                *ptr = value;
                final_result = (Item){.item = d2it(ptr)};
            }
        } else {
            final_result = result;
        }
    }

    ArrayList* result_type_list = (ArrayList*)context->type_list;
    context = old_context;

    // cleanup
    jm_destroy_mir_transpiler(mt);
    MIR_finish(ctx);

    // stash ephemeral GC heap on Runtime for caller cleanup
    if (!reusing_context) {
        runtime->heap = js_context.heap;
        runtime->nursery = js_context.nursery;
    }
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
JsPreambleState* g_jm_preamble_out = NULL;      // output: preamble snapshot (preamble mode)
const JsPreambleState* g_jm_preamble_in = NULL;  // input: pre-seed from preamble (test mode)

void js_normalize_path_separators(char* path) {
    if (!path) return;
    for (char* p = path; *p; p++) {
        if (*p == '\\') *p = '/';
    }
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

Item transpile_js_to_mir_core_len(Runtime* runtime, const char* js_source, size_t js_source_len, const char* filename) {
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
        mem_free(owned_source);
        owned_source = injected_source;
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

    // Save runtime for dynamic function compilation (new Function(...)) support
    js_source_runtime = runtime;

    // Create JS transpiler (for parsing and AST building)
    JsTranspiler* tp = js_transpiler_create(runtime);
    if (!tp) {
        log_error("js-mir: failed to create transpiler");
        mem_free(owned_source);
        return (Item){.item = ITEM_ERROR};
    }

    // Parse JavaScript source
    long phase_start = js_mir_phase_now_us();
    if (!js_transpiler_parse(tp, js_source, js_source_len)) {
        log_error("js-mir: parse failed");
        js_transpiler_destroy(tp);
        mem_free(owned_source);
        return (Item){.item = ITEM_ERROR};
    }
    g_last_js_mir_phase_timing.parse_us = js_mir_phase_now_us() - phase_start;
    log_mem_stage("js-core: ts_parsed");

    TSNode root = ts_tree_root_node(tp->tree);

    // Build JavaScript AST
    phase_start = js_mir_phase_now_us();
    JsAstNode* js_ast = build_js_ast(tp, root);
    if (!js_ast) {
        log_error("js-mir: AST build failed");
        js_transpiler_destroy(tp);
        mem_free(owned_source);
        return (Item){.item = ITEM_ERROR};
    }
    g_last_js_mir_phase_timing.ast_us = js_mir_phase_now_us() - phase_start;
    log_mem_stage("js-core: ast_built");

    // Run early error detection (static semantic validation)
    phase_start = js_mir_phase_now_us();
    int early_errors = js_check_early_errors(tp, js_ast);
    if (early_errors > 0) {
        log_error("js-mir: %d early error(s) detected", early_errors);
        js_transpiler_destroy(tp);
        mem_free(owned_source);
        return (Item){.item = ITEM_ERROR};
    }
    g_last_js_mir_phase_timing.early_us = js_mir_phase_now_us() - phase_start;

    // Set up evaluation context EARLY — needed before module loading
    // so that heap-allocated module objects (namespaces, strings) persist.
    EvalContext js_context;
    memset(&js_context, 0, sizeof(EvalContext));
    EvalContext* old_context = context;
    bool reusing_context = false;

    if (old_context && old_context->heap) {
        context = old_context;
        reusing_context = true;
        if (!context->nursery) {
            context->nursery = mem_nursery_create(NULL, 0, MEM_ROLE_RUNTIME_HEAP, "js.nursery");
        }
        if (!context->type_list) {
            context->type_list = runtime && runtime->type_list
                ? runtime->type_list
                : arraylist_new(64);
        }
    } else {
        js_context.nursery = mem_nursery_create(NULL, 0, MEM_ROLE_RUNTIME_HEAP, "js.nursery");
        context = &js_context;
        if (runtime->reuse_pool) {
            heap_init_with_pool(runtime->reuse_pool);
            runtime->reuse_pool = NULL;
        } else {
            heap_init();
        }
        context->pool = context->heap->pool;
        context->name_pool = name_pool_create(context->pool, nullptr);
        context->type_list = arraylist_new(64);
    }

    _lambda_rt = (Context*)context;

    // Create Input context for JS runtime — must be before module loading
    Input* js_input = Input::create(context->pool);
    js_runtime_set_input(js_input);

    // Pre-compile imported modules in parallel (macOS/Linux only).
    // Discovers dependency graph, compiles modules by depth level using thread pool,
    // then executes serially.  jm_load_imports() below will skip already-loaded modules.
#ifndef _WIN32
    // fast-path: skip import precompile for scripts with no imports
    phase_start = js_mir_phase_now_us();
    if (js_source_contains_ascii(js_source, js_source_len, "import ") ||
        js_source_contains_ascii(js_source, js_source_len, "import{")) {
        jm_precompile_js_imports(runtime, js_source, filename);
    }
#endif

    // Load imported modules before main compilation (recursive)
    // After precompile, all modules with >=2 imports are already loaded — this handles
    // the fallback case (0-1 imports, Windows, or any modules missed by precompile).
#ifdef _WIN32
    phase_start = js_mir_phase_now_us();
#endif
    jm_load_imports(runtime, js_ast, filename);
    g_last_js_mir_phase_timing.imports_us = js_mir_phase_now_us() - phase_start;
    log_mem_stage("js-core: imports_loaded");

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
        js_transpiler_destroy(tp);
        mem_free(owned_source);
        return (Item){.item = ITEM_ERROR};
    }
    g_active_mir_ctx = ctx;  // track for batch timeout recovery

    // Install batch error handler if set (prevents exit(1) on MIR errors)
    if (g_batch_mir_error_handler) {
        MIR_set_error_func(ctx, g_batch_mir_error_handler);
    }

    // Set up MIR transpiler (heap-allocated: struct is ~3 MB due to func_entries[256])
    JsMirTranspiler* mt = jm_create_mir_transpiler(tp, ctx, filename, false, 64, 32, 16, "js-mir");
    if (!mt) {
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        mem_free(owned_source);
        return (Item){.item = ITEM_ERROR};
    }

    // Tune6 §3.3: enable per-opcode emission histogram for this transpile.
    static int js_opcode_hist_cached = -1;
    if (js_opcode_hist_cached < 0) {
        const char* e = getenv("JS_MIR_OPCODE_HIST");
        js_opcode_hist_cached = (e && e[0] && strcmp(e, "0") != 0) ? 1 : 0;
    }
    if (js_opcode_hist_cached) { jm_opcode_hist_set_enabled(1); jm_opcode_hist_reset(); }

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
    transpile_js_mir_ast(mt, js_ast);
    g_last_js_mir_phase_timing.mir_us = js_mir_phase_now_us() - phase_start;
    log_mem_stage("js-core: ast_to_mir");

#ifndef NDEBUG
    if (getenv("JS_MIR_DUMP")) {
    // Dump MIR for debugging (guard against NULL labels that crash output)
    create_dir_recursive("temp");
    FILE* mir_dump = fopen("temp/js_mir_dump.txt", "w");
    if (mir_dump) {
        // Check for NULL labels before dumping
        bool dump_safe = true;
        for (MIR_module_t m = DLIST_HEAD (MIR_module_t, *MIR_get_module_list(ctx)); m != NULL;
             m = DLIST_NEXT (MIR_module_t, m)) {
            for (MIR_item_t item = DLIST_HEAD (MIR_item_t, m->items); item != NULL;
                 item = DLIST_NEXT (MIR_item_t, item)) {
                if (item->item_type == MIR_func_item) {
                    MIR_func_t func = item->u.func;
                    for (MIR_insn_t insn = DLIST_HEAD (MIR_insn_t, func->insns); insn != NULL;
                         insn = DLIST_NEXT (MIR_insn_t, insn)) {
                        for (size_t i = 0; i < insn->nops; i++) {
                            if (insn->ops[i].mode == MIR_OP_LABEL && insn->ops[i].u.label == NULL) {
                                dump_safe = false;
                                log_error("js-mir: NULL label in insn code=%d func=%s", insn->code, func->name);
                                break;
                            }
                        }
                        if (!dump_safe) break;
                    }
                }
                if (!dump_safe) break;
            }
            if (!dump_safe) break;
        }
        if (dump_safe) {
            MIR_output(ctx, mir_dump);
        } else {
            fprintf(mir_dump, "ERROR: NULL labels detected, dump skipped\n");
        }
        fclose(mir_dump);
    }
    } // JS_MIR_DUMP
#endif

    // Pre-link validation: abort gracefully if NULL labels found
    if (!jm_validate_mir_labels(ctx)) {
        log_error("js-mir: NULL labels detected, aborting link for '%s'", filename ? filename : "<string>");
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        mem_free(owned_source);
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
    if (js_opcode_hist_cached) {
        jm_opcode_hist_dump(ctx, filename ? filename : "<string>");
        jm_opcode_hist_set_enabled(0);
    }

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
    log_mem_stage("js-core: mir_linked");
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
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        mem_free(owned_source);
        return (Item){.item = ITEM_ERROR};
    }

    // v14: initialize event loop before execution. Dynamic import runs inside
    // an active script, so preserve the caller's pending PromiseJobs.
    if (js_dynamic_import_suppress_module_drain <= 0) {
        js_event_loop_init();
    }

    // Set up DOM document context if available
    if (runtime->dom_doc) {
        js_dom_set_document(runtime->dom_doc);
    }

    // Execute
    log_debug("js-mir: executing JIT compiled code");
    if (!g_jm_preamble_in) {
        // Normal/preamble mode: allocate per-module vars for this top-level module
        js_set_active_module_vars(js_alloc_module_vars());
    }

    // Save module_consts as eval preamble BEFORE execution so that
    // eval()/new Function() called during js_main can resolve outer-scope
    // var declarations via the shared static js_module_vars[] array.
    if (mt->module_consts && !g_jm_preamble_mode) {
        mem_free(g_eval_preamble_entries);
        int ecount = (int)hashmap_count(mt->module_consts);
        g_eval_preamble_entries = (JsModuleConstEntry*)mem_alloc(ecount * sizeof(JsModuleConstEntry), MEM_CAT_JS_RUNTIME);
        g_eval_preamble_entry_count = 0;
        size_t eiter = 0; void* eitem;
        while (hashmap_iter(mt->module_consts, &eiter, &eitem)) {
            g_eval_preamble_entries[g_eval_preamble_entry_count++] =
                *(JsModuleConstEntry*)eitem;
        }
        g_eval_preamble_var_count = mt->module_var_count;
        log_debug("js-mir: saved eval preamble: %d entries, %d module vars",
            g_eval_preamble_entry_count, g_eval_preamble_var_count);
    }

    // With-preamble mode: caller already called js_batch_reset_to() — harness vars preserved
    phase_start = js_mir_phase_now_us();
    Item result;
#if defined(__APPLE__) || defined(__linux__)
    if (sigsetjmp(_lambda_recovery_point, 1)) {
#elif defined(_WIN32)
    if (setjmp(_lambda_recovery_point)) {
#else
    if (0) {
#endif
        // Stack overflow was caught — signal handler siglongjmp'd here
        _lambda_recovery_armed = 0;   // recovery consumed; disarm
        log_error("js-mir: recovered from stack overflow via signal handler");
        _lambda_stack_overflow_flag = false;
        result = (Item){.item = ITEM_ERROR};
        // Report the error so it shows up as an uncaught exception
        js_throw_range_error("Maximum call stack size exceeded");
    } else {
        _lambda_recovery_armed = 1;    // arm only for the duration of user code
        result = js_main((Context*)context);
        _lambda_recovery_armed = 0;
    }
    g_last_js_mir_phase_timing.execute_us = js_mir_phase_now_us() - phase_start;
    log_debug("js-mir: JIT execution returned (type=%d)", get_type_id(result));
    log_mem_stage("js-core: js_main_done");

    // v14: drain the event loop while JIT module is still alive
    // (MIR_finish below destroys compiled code, so timers must fire here).
    // Dynamic import loads modules from inside an already-running script; if the
    // nested module drains the global microtask queue, outer async-generator
    // Promise jobs can run with the imported module's temporary context active.
    if (js_dynamic_import_suppress_module_drain <= 0) {
        js_event_loop_drain();
    }
    if (runtime->dom_doc) {
        // Headless Radiant layout has no frame clock; flush a bounded number
        // of requestAnimationFrame ticks before the JS heap/context are
        // restored so DOM callbacks can still allocate wrapper objects.
        js_animation_frame_drain(8);
    }
    log_debug("js-mir: event loop drained");

    // Fire process 'exit' event listeners (Node.js compatibility).
    // Must happen while JIT code is still mapped (before MIR_finish).
    {
        int exit_code = (result.item == ITEM_ERROR || js_check_exception()) ? 1 : 0;
        js_process_emit_exit(exit_code);
    }

    // Preamble mode: snapshot module_consts so tests can inherit harness definitions
    if (g_jm_preamble_out && mt->module_consts) {
        g_jm_preamble_out->module_var_count = mt->module_var_count;
        int count = (int)hashmap_count(mt->module_consts);
        g_jm_preamble_out->entries = (JsModuleConstEntry*)mem_alloc(count * sizeof(JsModuleConstEntry), MEM_CAT_JS_RUNTIME);
        g_jm_preamble_out->entry_count = 0;
        size_t snap_iter = 0; void* snap_item;
        while (hashmap_iter(mt->module_consts, &snap_iter, &snap_item)) {
            g_jm_preamble_out->entries[g_jm_preamble_out->entry_count++] =
                *(JsModuleConstEntry*)snap_item;
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
                double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
                *ptr = value;
                final_result = (Item){.item = d2it(ptr)};
            }
        } else {
            final_result = result;
        }
    }

    // Convert JS HashMap objects to VMap for proper printing (before context restore)
    // (no longer needed — JS objects are now Lambda Maps)

    ArrayList* result_type_list = (ArrayList*)context->type_list;
    context = old_context;

    // Cleanup
    phase_start = js_mir_phase_now_us();
    jm_destroy_mir_transpiler(mt);
    if (g_jm_preamble_out) {
        // Preamble mode: keep MIR context alive — harness function objects reference compiled code
        g_jm_preamble_out->mir_ctx = ctx;
        // Keep transpiler pools alive — compiled MIR code and map shape entries
        // hold StrView pointers to strings in the transpiler's name_pool.
        // Destroying the pool would leave dangling pointers (SIGSEGV on property lookup).
        g_jm_preamble_out->tp_ast_pool = tp->ast_pool;
        g_jm_preamble_out->tp_name_pool = tp->name_pool;
        g_jm_preamble_out->source_buffer = owned_source;
        owned_source = NULL;
        tp->ast_pool = NULL;  // prevent js_transpiler_destroy from freeing these
        tp->name_pool = NULL;
    } else if (reusing_context) {
        // Hot-reload batch mode: defer MIR context destruction.
        // The heap persists across tests, so function objects on the heap
        // may still reference code pages in this MIR context. Destroying
        // the context now would leave dangling func_ptr pointers → SIGBUS.
        jm_defer_mir_cleanup(ctx);
        // JIT code and runtime metadata embed raw String* pointers interned in
        // the transpiler name pool. Keep those pools with the deferred MIR
        // context so hot-reload cleanup does not leave dangling strings.
        if (module_mir_context_count > 0) {
            module_mir_name_pools[module_mir_context_count - 1] = tp->name_pool;
            module_mir_ast_pools[module_mir_context_count - 1] = tp->ast_pool;
            module_mir_source_buffers[module_mir_context_count - 1] = owned_source;
            owned_source = NULL;
        }
        tp->name_pool = NULL;
        tp->ast_pool = NULL;
    } else {
        MIR_finish(ctx);
    }
    g_active_mir_ctx = NULL;  // normal cleanup done, no longer need recovery

    // stash ephemeral GC heap on Runtime for caller cleanup.
    // each heap allocates ~12MB (data zone + tenured zone + bump block), so
    // leaking one per document causes massive RSS growth in batch mode.
    // caller must call runtime_reset_heap() after inspecting the result.
    // In preamble mode, also stash the heap so the caller can retain it
    // for interactive event handler compilation (needs heap for reusing_context).
    if (!reusing_context) {
        runtime->heap = js_context.heap;
        runtime->nursery = js_context.nursery;
        runtime->name_pool = js_context.name_pool;
    }
    if (runtime) {
        runtime->type_list = result_type_list;
    }

    // In hot-reload batch mode, skip deferred MIR cleanup — accumulated contexts
    // must persist until batch end (heap objects may reference their code pages).
    if (!reusing_context) {
        jm_cleanup_deferred_mir();
    }
    js_transpiler_destroy(tp);

    mem_free(owned_source);
    g_last_js_mir_phase_timing.cleanup_us = js_mir_phase_now_us() - phase_start;
    g_last_js_mir_phase_timing.total_us = js_mir_phase_now_us() - phase_total_start;

    log_debug("js-mir: transpilation completed");
    return final_result;
}

Item transpile_js_to_mir_core(Runtime* runtime, const char* js_source, const char* filename) {
    return transpile_js_to_mir_core_len(runtime, js_source, strlen(js_source), filename);
}

// ============================================================================
// Public API wrappers for preamble support
// ============================================================================

Item transpile_js_to_mir(Runtime* runtime, const char* js_source, const char* filename) {
    return transpile_js_to_mir_len(runtime, js_source, strlen(js_source), filename);
}

Item transpile_js_to_mir_len(Runtime* runtime, const char* js_source, size_t js_source_len, const char* filename) {
    g_jm_preamble_mode = false;
    g_jm_preamble_out = NULL;
    g_jm_preamble_in = NULL;
    return transpile_js_to_mir_core_len(runtime, js_source, js_source_len, filename);
}

Item transpile_js_to_mir_preamble(Runtime* runtime, const char* js_source, const char* filename,
                                   JsPreambleState* out_state) {
    return transpile_js_to_mir_preamble_len(runtime, js_source, strlen(js_source), filename, out_state);
}

Item transpile_js_to_mir_preamble_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                                      const char* filename, JsPreambleState* out_state) {
    g_jm_preamble_mode = true;
    g_jm_preamble_out = out_state;
    g_jm_preamble_in = NULL;
    // Preamble (harness) always compiled at -O3 for best runtime performance
    unsigned int saved_level = g_js_mir_optimize_level;
    g_js_mir_optimize_level = 3;
    Item result = transpile_js_to_mir_core_len(runtime, js_source, js_source_len, filename);
    g_js_mir_optimize_level = saved_level;
    g_jm_preamble_mode = false;
    g_jm_preamble_out = NULL;
    return result;
}

Item transpile_js_to_mir_with_preamble(Runtime* runtime, const char* js_source, const char* filename,
                                        const JsPreambleState* preamble) {
    return transpile_js_to_mir_with_preamble_len(runtime, js_source, strlen(js_source), filename, preamble);
}

Item transpile_js_to_mir_with_preamble_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                                           const char* filename, const JsPreambleState* preamble) {
    g_jm_preamble_mode = false;
    g_jm_preamble_out = NULL;
    g_jm_preamble_in = preamble;
    Item result = transpile_js_to_mir_core_len(runtime, js_source, js_source_len, filename);
    g_jm_preamble_in = NULL;
    return result;
}

bool preamble_state_update_from_eval_snapshot(JsPreambleState* state) {
    if (!state || !g_eval_preamble_entries || g_eval_preamble_entry_count <= 0) {
        return false;
    }

    int count = g_eval_preamble_entry_count;
    JsModuleConstEntry* entries = (JsModuleConstEntry*)mem_alloc(
        count * sizeof(JsModuleConstEntry), MEM_CAT_JS_RUNTIME);
    if (!entries) {
        log_error("js-mir: failed to refresh preamble snapshot");
        return false;
    }
    memcpy(entries, g_eval_preamble_entries, count * sizeof(JsModuleConstEntry));

    mem_free(state->entries);
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
    if (state->mir_ctx) {
        MIR_finish((MIR_context_t)state->mir_ctx);
        state->mir_ctx = NULL;
    }
    // Release transpiler pools that were kept alive for string references.
    // name_pool must be released before ast_pool (it was allocated from ast_pool).
    if (state->tp_name_pool) {
        name_pool_release((NamePool*)state->tp_name_pool);
        state->tp_name_pool = NULL;
    }
    if (state->tp_ast_pool) {
        pool_destroy((Pool*)state->tp_ast_pool);
        state->tp_ast_pool = NULL;
    }
    if (state->source_buffer) {
        mem_free(state->source_buffer);
        state->source_buffer = NULL;
    }
    mem_free(state->entries);
    state->entries = NULL;
    state->entry_count = 0;
    state->module_var_count = 0;
}

// ============================================================================
// Public API: load a JS file as a module for cross-language import
// ============================================================================

Item load_js_module(Runtime* runtime, const char* js_path) {
    log_info("js-mir: loading JS module '%s' for cross-language import", js_path);
    char* source = read_text_file(js_path);
    if (!source) {
        log_error("js-mir: cannot read JS file '%s'", js_path);
        return ItemNull;
    }

    // transpile_js_module_to_mir assumes a heap context is already active
    // (normally provided by transpile_js_to_mir). When called from build_ast
    // during Lambda→JS import, no context exists yet. Set up a persistent one.
    if (!context || !context->heap) {
        EvalContext* temp_ctx = (EvalContext*)mem_calloc(1, sizeof(EvalContext), MEM_CAT_JS_RUNTIME);
        temp_ctx->pool = mem_pool_create(NULL, MEM_ROLE_RUNTIME_HEAP, "js.runtime");
        temp_ctx->result = ItemNull;
        temp_ctx->nursery = mem_nursery_create(NULL, 0, MEM_ROLE_RUNTIME_HEAP, "js.nursery");
        context = temp_ctx;
        heap_init();
        context->pool = context->heap->pool;
        context->name_pool = name_pool_create(context->pool, nullptr);
        context->type_list = arraylist_new(64);
        _lambda_rt = (Context*)context;

        // Create Input context for JS runtime
        Input* js_input = Input::create(context->pool);
        js_runtime_set_input(js_input);
        log_debug("js-mir: created persistent heap for cross-language module loading");
    }

    Item ns = transpile_js_module_to_mir(runtime, source, js_path);
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
        "var __dirname = \"%.*s\";\n";
    const char* suffix = "\nexport default __cjs_module__.exports;\n";

    size_t src_len = strlen(source);
    size_t prefix_size = strlen(prefix_fmt) + strlen(filename_buf) + dir_len + 64;
    size_t total = prefix_size + src_len + strlen(suffix) + 1;

    char* wrapped = (char*)mem_alloc(total, MEM_CAT_JS_RUNTIME);
    int offset = snprintf(wrapped, total, prefix_fmt, filename_buf, dir_len, dir_str);
    memcpy(wrapped + offset, source, src_len);
    offset += (int)src_len;
    strcpy(wrapped + offset, suffix);
    return wrapped;
}

extern "C" Item js_require(Item specifier) {
    if (get_type_id(specifier) != LMD_TYPE_STRING) {
        log_error("require: specifier is not a string");
        return ItemNull;
    }
    String* spec = it2s(specifier);
    if (!spec || spec->len == 0) return ItemNull;

    // Check if already loaded in module cache
    Item existing = js_module_get(specifier);
    if (get_type_id(existing) != LMD_TYPE_NULL) {
        // For CJS modules, the cached value is the namespace.
        // Extract the default export (which is module.exports)
        Item def_key = (Item){.item = s2it(heap_create_name("default"))};
        Item def_val = js_property_get(existing, def_key);
        TypeId dt = get_type_id(def_val);
        if (dt != LMD_TYPE_NULL && dt != LMD_TYPE_UNDEFINED) return def_val;
        return existing;
    }

    char path_buf[512];
    snprintf(path_buf, sizeof(path_buf), "%.*s", (int)spec->len, spec->chars);

    // Read the source file
    char* source = read_text_file(path_buf);
    if (!source) {
        // Node.js directory resolution: try path/index.js
        size_t plen = strlen(path_buf);
        // Strip .js extension if present, then try /index.js
        if (plen >= 3 && strcmp(path_buf + plen - 3, ".js") == 0) {
            path_buf[plen - 3] = '\0';
        }
        strncat(path_buf, "/index.js", sizeof(path_buf) - strlen(path_buf) - 1);
        source = read_text_file(path_buf);
    }
    if (!source) {
        log_error("require: cannot read module '%s'", path_buf);
        // For internal/* modules, return empty object to prevent destructure crashes
        if (strncmp(path_buf, "internal/", 9) == 0 || 
            (spec->len > 9 && memcmp(spec->chars, "internal/", 9) == 0)) {
            return js_new_object();
        }
        return ItemNull;
    }

    Runtime* runtime = js_source_runtime;
    if (!runtime) {
        log_error("require: no runtime available");
        mem_free(source);
        return ItemNull;
    }

    Item ns;
    if (js_is_cjs_file(path_buf)) {
        // Wrap CJS source with module/exports globals
        char* wrapped = js_wrap_cjs_source(source, path_buf);
        mem_free(source);
        ns = transpile_js_module_to_mir(runtime, wrapped, path_buf);
        mem_free(wrapped);
    } else {
        // ESM — transpile as-is
        ns = transpile_js_module_to_mir(runtime, source, path_buf);
        mem_free(source);
    }

    if (get_type_id(ns) == LMD_TYPE_NULL) {
        log_error("require: failed to compile module '%s'", path_buf);
        return ItemNull;
    }

    // For CJS, extract the default export (module.exports)
    if (js_is_cjs_file(path_buf)) {
        Item def_key = (Item){.item = s2it(heap_create_name("default"))};
        Item def_val = js_property_get(ns, def_key);
        TypeId dt = get_type_id(def_val);
        if (dt != LMD_TYPE_NULL && dt != LMD_TYPE_UNDEFINED) return def_val;
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
    if (js_check_exception() || get_type_id(specifier_string) != LMD_TYPE_STRING) {
        return js_promise_reject(js_clear_exception());
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
        Runtime* runtime = js_source_runtime;
        if (!runtime) {
            mem_free(source);
            js_dynamic_import_suppress_module_drain--;
            return js_dynamic_import_reject_type_error("import(): no runtime available");
        }
        ns = transpile_js_module_to_mir(runtime, source, path_buf);
        mem_free(source);
    }
    js_dynamic_import_suppress_module_drain--;
    if (js_check_exception()) {
        return js_promise_reject(js_clear_exception());
    }
    if (get_type_id(ns) == LMD_TYPE_NULL) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot find module '%.*s'", (int)spec->len, spec->chars);
        return js_dynamic_import_reject_type_error(msg);
    }

    // Wrap the namespace in a resolved Promise
    return js_promise_resolve(ns);
}
