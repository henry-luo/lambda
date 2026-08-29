#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>    // for sysconf
#endif
#include "transpiler.hpp"
#include "ast_build.hpp"
#include "../../lib/hashmap_helpers.h"
#include "../../lib/thread_pool.h"
#include "../io/mark_builder.hpp"
#include "../core/lambda-decimal.hpp"
#include "lambda-error.h"
#include "lambda-stack.h"
#include "recovery_frame.h"
#include "side_stack.h"
#include "concurrency.h"
#include "module_registry.h"
#include "../jube/jube_registry.h"
#include "../js/js_runtime.h"
#include "../js/js_runtime_state.hpp"
#include "../js/js_event_loop.h"
#include "../js/js_exec_profile.h"
#include "../input/css/css_style.hpp"
#include "template_registry.h"
#include "render_map.h"
#include "template_state.h"
#include "edit_bridge.h"
#include "interp.hpp"
#include "runtime-state.h"
#include "../input/input.hpp"
#include "../../lib/file.h"
#include "../../lib/mem_factory.h"
#include "../../lib/memtrack.h"
#include "../../lib/file_utils.h"
#include "../../lib/shell.h"
#include "../../lib/uv_loop.h"
#include "compiler_timing.hpp"

extern "C" Item js_get_key_default(Item object, Item key);
extern "C" void js_dom_shutdown(void);
struct DomDocument;
extern void free_document(DomDocument* doc);

#ifndef LAMBDA_MIR_CACHE_DEFAULT
#define LAMBDA_MIR_CACHE_DEFAULT 1
#endif

static __thread LambdaCompilerTiming g_last_lambda_compiler_timing;
static int g_compiler_timing_enabled = -1;

static void record_direct_parse_error(Transpiler* tp, const char* script_path,
        const LambdaParseError* parse_error) {
    if (!tp || !tp->source) return;
    SourceSpan span = parse_error ? parse_error->span : (SourceSpan){0, 0};
    size_t source_length = strlen(tp->source);
    if (span.start_byte > source_length) span.start_byte = (uint32_t)source_length;
    if (span.end_byte < span.start_byte || span.end_byte > source_length) {
        span.end_byte = span.start_byte;
    }
    LambdaSourcePoint start = lambda_source_span_start_point(tp->source, span);
    LambdaSourcePoint end = lambda_source_span_end_point(tp->source, span);
    SourceLocation location = src_loc_span(script_path, start.row + 1, start.column + 1,
        end.row + 1, end.column + 1);
    location.source = tp->source;
    char message[256];
    const char* text = tp->source + span.start_byte;
    size_t text_length = span.end_byte - span.start_byte;
    if (text_length == 0 && span.start_byte < source_length) text_length = 1;
    bool separated_relation = false;
    if (parse_error) {
        // Pratt has already consumed the relation before it discovers that
        // its right operand cannot start a sibling statement. Walk back on
        // this source line to recover that committed delimiter.
        size_t cursor = span.start_byte;
        while (cursor > 0 && tp->source[cursor - 1] != '\n' &&
                tp->source[cursor - 1] != '\r') {
            char previous = tp->source[--cursor];
            if (previous == ' ' || previous == '\t') continue;
            separated_relation = previous == '<' || previous == '>';
            // '=>' and '|>' end in '>' but are not relations; without this
            // guard an arrow-body error ('=> return x') was rewritten into the
            // element-ambiguity diagnosis, hiding the parser's real repair.
            if (separated_relation && previous == '>' && cursor > 0 &&
                    (tp->source[cursor - 1] == '=' || tp->source[cursor - 1] == '|')) {
                separated_relation = false;
            }
            break;
        }
    }
    if (separated_relation) {
        // The direct Pratt parser reaches the same unseparated relation that
        // The reference grammar marks this as element-ambiguous; preserve one user-facing
        // syntax diagnosis instead of exposing parser-internal terminology.
        snprintf(message, sizeof(message),
            "'<' and '>' are ambiguous with element syntax at statement level");
    } else if (parse_error && parse_error->actual_kind == LAMBDA_TOK_ERROR) {
        // An unlexable token has no structural diagnosis to offer — the parser
        // can only say "invalid token". Naming the offending text is strictly
        // more useful here, so this one case keeps the synthesized form.
        snprintf(message, sizeof(message), "Unexpected syntax near '%.*s'",
            (int)(text_length > 30 ? 30 : text_length), text);
    } else if (parse_error && parse_error->message && parse_error->message[0]) {
        // The parser's own diagnosis names the repair — "write ';' to start a
        // new statement, or move it to the end of that line", "'pub' modifies a
        // declaration; write 'pub let'", and the rest. Synthesizing
        // "Unexpected syntax near 'X'" here discarded every one of them, so the
        // S16.2.3 rejections reached the user without their repair (§4.2 makes
        // that text part of the design, not decoration).
        snprintf(message, sizeof(message), "%s", parse_error->message);
    } else {
        snprintf(message, sizeof(message), "Unexpected syntax near '%.*s'",
            (int)(text_length > 30 ? 30 : text_length), text);
    }
    LambdaError* error = err_create(ERR_SYNTAX_ERROR, message, &location);
    if (!error) return;
    if (separated_relation) {
        // The reference grammar attaches the repair to this same diagnosis
        // (lambda-error.cpp); dropping it here left the C parser naming the
        // ambiguity without telling the user how to resolve it.
        error->help = mem_strdup(
            "Use parentheses to group the comparison expression, e.g. (\"a\" < \"b\").",
            MEM_CAT_TEMP);
    }
    if (tp->errors) arraylist_append(tp->errors, error);
    else err_free(error);
    tp->error_count++;
}

static void record_direct_parse_diagnostics(Transpiler* tp,
        const char* script_path, const LambdaParseError* fallback) {
    if (!tp || !tp->source) return;
    LambdaParseReport report = {};
    lambda_rd_parse_recovering(tp->source, strlen(tp->source), &report);
    if (report.error_count == 0) {
        record_direct_parse_error(tp, script_path, fallback);
        return;
    }
    for (uint32_t i = 0; i < report.error_count; i++) {
        record_direct_parse_error(tp, script_path, &report.errors[i]);
    }
}

static void free_transpiler_error_list(ArrayList* errors) {
    if (!errors) return;
    for (int i = 0; i < errors->length; i++) {
        err_free((LambdaError*)errors->data[i]);
    }
    arraylist_free(errors);
}

static void free_transpiler_diagnostics(Transpiler* tp) {
    if (!tp) return;
    free_transpiler_error_list(tp->errors);
    free_transpiler_error_list(tp->warnings);
    tp->errors = NULL;
    tp->warnings = NULL;
}

extern "C" int lambda_compiler_timing_enabled(void) {
    if (g_compiler_timing_enabled >= 0) return g_compiler_timing_enabled;
    const char* value = shell_getenv("LAMBDA_COMPILER_TIMING");
    g_compiler_timing_enabled = value && value[0] && strcmp(value, "0") != 0;
    return g_compiler_timing_enabled;
}

extern "C" void lambda_compiler_timing_reset(void) {
    memset(&g_last_lambda_compiler_timing, 0, sizeof(g_last_lambda_compiler_timing));
}

extern "C" void lambda_compiler_timing_get(LambdaCompilerTiming* out) {
    if (out) *out = g_last_lambda_compiler_timing;
}

// ============================================================================
// Lambda Home Path
// ============================================================================
// g_lambda_home is the directory containing Lambda's runtime assets
// (package/, input/).
//
//   Dev default  : "./lambda"   (assets live next to source)
//   Release      : "./lmd"      (set via -DLAMBDA_HOME_RELEASE compile flag,
//                                or override at runtime with LAMBDA_HOME env var)
//
// The name "lmd" avoids a name clash between the lambda executable and a
// directory of the same name on macOS/Linux.

#ifdef LAMBDA_HOME_RELEASE
const char* g_lambda_home = "./lmd";
#else
const char* g_lambda_home = "./lambda";
#endif

// check if a directory exists
static bool dir_exists(const char* path) {
    return file_is_dir(path);
}

void lambda_home_init(void) {
    // 1. environment variable always wins
    const char* env = shell_getenv("LAMBDA_HOME");
    if (env && env[0]) {
        g_lambda_home = env;
        return;
    }

    // 2. auto-detect: try the compiled-in default first, then the other
    if (dir_exists(g_lambda_home)) return;

#ifdef LAMBDA_HOME_RELEASE
    // release binary but ./lmd/ missing — fall back to ./lambda/ (dev tree)
    if (dir_exists("./lambda")) { g_lambda_home = "./lambda"; }
#else
    // dev binary but ./lambda/ missing — try ./lmd/ (release layout)
    if (dir_exists("./lmd"))    { g_lambda_home = "./lmd"; }
#endif
}

// Build a malloc'd path "<g_lambda_home>/<rel>".  Caller must free().
char* lambda_home_path(const char* rel) {
    size_t home_len = strlen(g_lambda_home);
    size_t rel_len  = strlen(rel);
    char* out = (char*)mem_alloc(home_len + 1 + rel_len + 1, MEM_CAT_SYSTEM);
    if (!out) return NULL;
    memcpy(out, g_lambda_home, home_len);
    out[home_len] = '/';
    memcpy(out + home_len + 1, rel, rel_len + 1);
    return out;
}


#if _WIN32
#include <windows.h>
#endif

// ============================================================================
// Phase-Level Profiling (enabled by LAMBDA_PROFILE=1 environment variable)
// ============================================================================
// Stores timing data in memory during compilation, dumps to file at cleanup.
// Zero overhead when disabled — all gated by profile_enabled flag.

#define PROFILE_MAX_SCRIPTS 64
#define PROFILE_PATH_MAX 512

typedef struct PhaseProfile {
    char script_path[PROFILE_PATH_MAX];
    double parse_ms;
    double ast_ms;
    // T0 columns: `plan_ms` is the frame-plan pass, `interp_exec_ms` the walk.
    // Both stay 0 on the JIT path so the TSV reads the same for either tier.
    double plan_ms;
    double transpile_ms;
    double jit_init_ms;
    double mir_gen_ms;
    double interp_exec_ms;
    double peak_rss_mb;
    int code_len;
    int worker_thread;
    unsigned long thread_id;
} PhaseProfile;

bool profile_enabled = false;
bool profile_checked = false;
PhaseProfile profile_data[PROFILE_MAX_SCRIPTS];
int profile_count = 0;
#ifndef _WIN32
static pthread_mutex_t profile_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

bool is_profile_enabled() {
    if (!profile_checked) {
        const char* env = shell_getenv("LAMBDA_PROFILE");
        profile_enabled = (env && (strcmp(env, "1") == 0 || strcmp(env, "true") == 0));
        profile_checked = true;
    }
    return profile_enabled;
}

// High-resolution profiling timer (cross-platform)
#ifdef _WIN32
typedef LARGE_INTEGER profile_time_t;
void profile_get_time(profile_time_t* t) { QueryPerformanceCounter(t); }
double elapsed_ms_val(profile_time_t t0, profile_time_t t1) {
    LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
    return (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
}
#else
typedef struct timespec profile_time_t;
void profile_get_time(profile_time_t* t) { clock_gettime(CLOCK_MONOTONIC, t); }
double elapsed_ms_val(profile_time_t t0, profile_time_t t1) {
    long sec = t1.tv_sec - t0.tv_sec;
    long nsec = t1.tv_nsec - t0.tv_nsec;
    if (nsec < 0) { sec--; nsec += 1000000000L; }
    return sec * 1000.0 + nsec / 1e6;
}
#endif

static unsigned long profile_current_thread_id() {
#ifdef _WIN32
    return (unsigned long)GetCurrentThreadId();
#else
    return (unsigned long)(uintptr_t)pthread_self();
#endif
}

static void profile_set_script_path(PhaseProfile* profile, const char* script_path) {
    if (!profile) return;
    if (!script_path) script_path = "";
    size_t len = strlen(script_path);
    if (len >= PROFILE_PATH_MAX) len = PROFILE_PATH_MAX - 1;
    memcpy(profile->script_path, script_path, len);
    profile->script_path[len] = '\0';
}

static void profile_record_phase(const PhaseProfile* profile) {
    if (!profile) return;
#ifndef _WIN32
    pthread_mutex_lock(&profile_mutex);
#endif
    if (profile_count < PROFILE_MAX_SCRIPTS) {
        profile_data[profile_count++] = *profile;
    }
#ifndef _WIN32
    pthread_mutex_unlock(&profile_mutex);
#endif
}

void profile_dump_to_file() {
    if (!profile_enabled || profile_count == 0) return;
    create_dir_recursive("temp");
    FILE* f = fopen("temp/phase_profile.txt", "w");
    if (!f) return;
    // TSV format v2: `plan`, `interp_exec` and `peak_rss_mb` added for the T0
    // turnaround/memory report; JIT-tier rows carry 0 in the T0 columns.
    fprintf(f, "# Phase-Level Profile (LAMBDA_PROFILE=1) format=2\n");
    fprintf(f, "# script | parse | ast | plan | transpile | jit_init | mir_gen | interp_exec | total | peak_rss_mb | code_len | worker | thread_id\n");
    for (int i = 0; i < profile_count; i++) {
        PhaseProfile* p = &profile_data[i];
        double total = p->parse_ms + p->ast_ms + p->plan_ms + p->transpile_ms +
                       p->jit_init_ms + p->mir_gen_ms + p->interp_exec_ms;
        fprintf(f, "%s\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%d\t%d\t%lu\n",
                p->script_path, p->parse_ms, p->ast_ms, p->plan_ms, p->transpile_ms,
                p->jit_init_ms, p->mir_gen_ms, p->interp_exec_ms,
                total, p->peak_rss_mb, p->code_len, p->worker_thread, p->thread_id);
    }
    fclose(f);
}

// ============================================================================
// Existing timing helpers (for log_debug output)
// ============================================================================

#if _WIN32

// Windows-specific timing implementation
typedef struct {
    LARGE_INTEGER counter;
} win_timer;

static void get_time(win_timer* timer) {
    QueryPerformanceCounter(&timer->counter);
}

static void print_elapsed_time(const char* label, win_timer start, win_timer end) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);

    double elapsed_ms = ((double)(end.counter.QuadPart - start.counter.QuadPart) * 1000.0) / frequency.QuadPart;
    log_debug("%s took %.3f ms", label, elapsed_ms);
}

#else
// Unix/Linux/macOS version
typedef struct timespec win_timer;

static void get_time(win_timer* timer) {
    clock_gettime(CLOCK_MONOTONIC, timer);
}

static void print_elapsed_time(const char* label, win_timer start, win_timer end) {
    // Calculate elapsed time in milliseconds
    long seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000;
    }
    double elapsed_ms = seconds * 1000.0 + nanoseconds / 1e6;
    log_debug("%s took %.3f ms", label, elapsed_ms);
    (void)elapsed_ms;
}
#endif


extern "C" {
char* read_text_file(const char *filename);
void write_text_file(const char *filename, const char *content);
void ensure_jit_imports_initialized(void);
}
void ensure_sys_func_maps_initialized(void);
void check_memory_leak();
void print_heap_entries();

// thread-specific runtime context is provided by runtime/runtime-state.cpp.
extern __thread Context* input_context;

typedef struct ScriptIndexEntry {
    const char* path;
    Script* script;
} ScriptIndexEntry;

HASHMAP_DEFINE_STRKEY(script_index, ScriptIndexEntry, path)

#ifndef _WIN32
// Mutex for thread-safe access to runtime->scripts during parallel compilation
static pthread_mutex_t scripts_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static Script* runtime_script_index_get(Runtime* runtime, const char* path) {
    if (!runtime || !runtime->script_index || !path) return NULL;
    ScriptIndexEntry probe = { .path = path, .script = NULL };
    const ScriptIndexEntry* found = (const ScriptIndexEntry*)hashmap_get(runtime->script_index, &probe);
    return found ? found->script : NULL;
}

static void runtime_script_index_put(Runtime* runtime, Script* script) {
    if (!runtime || !script || !script->reference) return;
    if (!runtime->script_index) {
        runtime->script_index = script_index_new(64);
    }
    ScriptIndexEntry entry = { .path = script->reference, .script = script };
    hashmap_set(runtime->script_index, &entry);
    if (hashmap_oom(runtime->script_index)) {
        log_error("mir cache index: failed to index script %s", script->reference);
    }
}

static void runtime_script_index_delete(Runtime* runtime, const char* path) {
    if (!runtime || !runtime->script_index || !path) return;
    ScriptIndexEntry probe = { .path = path, .script = NULL };
    hashmap_delete(runtime->script_index, &probe);
}

static void runtime_script_index_delete_script(Runtime* runtime, Script* script) {
    if (!runtime || !runtime->script_index || !script || !script->reference) return;
    if (runtime_script_index_get(runtime, script->reference) != script) return;
    runtime_script_index_delete(runtime, script->reference);
}

static void capture_script_file_stat(Script* script, const char* path, bool file_backed) {
    if (!script || !path || !file_backed) return;
    struct stat st;
    if (stat(path, &st) == 0) {
        script->src_mtime = st.st_mtime;
        script->src_size = st.st_size;
    }
}

static bool script_file_stat_changed(Script* script, const char* path) {
    if (!script || !path) return false;
    if (script->src_mtime == 0 && script->src_size == 0) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return st.st_mtime != script->src_mtime || st.st_size != script->src_size;
}

static bool script_ptr_list_contains(ArrayList* list, Script* script) {
    if (!list || !script) return false;
    for (int i = 0; i < list->length; i++) {
        if ((Script*)list->data[i] == script) return true;
    }
    return false;
}

static bool script_imports_retired_dep(Script* script, ArrayList* retired) {
    if (!script || !script->direct_imports || !retired) return false;
    for (int i = 0; i < script->direct_imports->length; i++) {
        Script* dep = (Script*)script->direct_imports->data[i];
        if (script_ptr_list_contains(retired, dep)) return true;
    }
    return false;
}

static void retire_script_cache_entry(Runtime* runtime, Script* script, ArrayList* retired, const char* reason) {
    if (!runtime || !script || script->cache_retired) return;
    script->cache_retired = true;
    runtime_script_index_delete_script(runtime, script);
    arraylist_append(retired, script);
    runtime->mir_cache_invalidations++;
    log_info("mir cache index: retired path=%s index=%d reason=%s",
             script->reference ? script->reference : "<unknown>", script->index,
             reason ? reason : "changed dependency");
}

static void retire_script_cone(Runtime* runtime, Script* root) {
    if (!runtime || !runtime->scripts || !root) return;
    ArrayList* retired = arraylist_new(8);
    retire_script_cache_entry(runtime, root, retired, "source changed");

    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < runtime->scripts->length; i++) {
            Script* candidate = (Script*)runtime->scripts->data[i];
            if (!candidate || candidate->cache_retired || !candidate->cache_retain) continue;
            if (script_imports_retired_dep(candidate, retired)) {
                retire_script_cache_entry(runtime, candidate, retired, "dependent of changed module");
                changed = true;
            }
        }
    }
    arraylist_free(retired);
}

static Script* runtime_script_index_get_current(Runtime* runtime, const char* path) {
    Script* script = runtime_script_index_get(runtime, path);
    if (!script) return NULL;
    if (script_file_stat_changed(script, path)) {
        log_info("mir cache index: stale path=%s index=%d", path, script->index);
        retire_script_cone(runtime, script);
        return NULL;
    }
    return script;
}

// The canonical EvalContext outlives runners, so error diagnostics remain with
// their semantic owner instead of escaping into a thread-wide side channel.
LambdaError* get_persistent_last_error() {
    return context ? context->last_error : NULL;
}

void clear_persistent_last_error() {
    if (context && context->last_error) {
        err_free(context->last_error);
        context->last_error = NULL;
    }
}

void preserve_context_last_error(Item result) {
    EvalContext* ctx = context;
    if (!ctx) {
        return;
    }

    if (get_type_id(result) == LMD_TYPE_ERROR) {
        LambdaError* result_error = it2err(result);
        if (result_error && ctx->last_error != result_error) {
            // An explicit interpreter/JIT completion can be the only owner of
            // the rich error; publish it for diagnostics without making the
            // diagnostic mirror part of ordinary control flow.
            if (ctx->last_error) err_free(ctx->last_error);
            ctx->last_error = result_error;
        }
        return;
    }

    if (!ctx->last_error) return;

    // error() values can be consumed by total equality, so a non-error result must drop stale diagnostics.
    // A completed non-error result cannot retain this context's old diagnostic.
    err_free(ctx->last_error);
    ctx->last_error = NULL;
}

// Helper functions for C code to access EvalContext members (used by path.c)
extern "C" {
Pool* eval_context_get_pool() {
    if (!context || !context->heap) return nullptr;
    return context->heap->pool;
}

NamePool* eval_context_get_name_pool() {
    return context ? context->name_pool : nullptr;
}
}

static Pool* runner_path_pool_provider(void) {
    return eval_context_get_pool();
}





void init_module_import(Transpiler *tp, AstScript *script) {
    log_debug("init imports of script");
    log_enter();
    AstNode* child = script->child;
    while (child) {
        if (child->node_type == AST_NODE_IMPORT) {
            AstImportNode* import = (AstImportNode*)child;
            log_debug("init import: %.*s", (int)(import->module.length), import->module.str);
            // find the module bss item
            char buf[256];
            snprintf(buf, sizeof(buf), "m%d", import->script->index);
            MIR_item_t imp = find_import(tp->jit_context, buf);
            log_debug("imported item: %p", imp);
            if (!imp) {
                log_error("Error: Failed to find import item for module %.*s",
                    (int)(import->module.length), import->module.str);
                goto RETURN;
            }
            uint8_t* mod_def = (uint8_t*)imp->addr;

            if (import->is_cross_lang) {
                // Cross-language import (e.g., JS module from Lambda)
                // JS modules have no _init_mod_consts, _init_mod_types, _init_mod_vars
                log_debug("cross-lang import: %.*s (ref: %s)",
                    (int)(import->module.length), import->module.str,
                    import->script->reference);

                // skip consts pointer field
                mod_def += sizeof(void**);

                // _mod_main = NULL (JS modules have no Lambda-style main entry)
                *(main_func_t*)mod_def = NULL;
                mod_def += sizeof(main_func_t);

                // _init_vars = NULL (no module variables to initialize)
                typedef void (*init_vars_fn)(void*);
                *(init_vars_fn*)mod_def = NULL;
                mod_def += sizeof(init_vars_fn);

                // Look up namespace from unified module registry
                ModuleDescriptor* desc = module_get_for_runtime(
                    tp->runtime, import->script->reference);
                if (!desc) {
                    log_error("Error: cross-lang module '%s' not found in registry",
                        import->script->reference);
                    goto RETURN;
                }
                Item ns = desc->namespace_obj;

                // Populate function pointer fields from JS namespace
                AstNode *node = import->script->ast_root;
                assert(node->node_type == AST_SCRIPT);
                node = ((AstScript*)node)->child;
                while (node) {
                    if (node->node_type == AST_NODE_FUNC) {
                        AstFuncNode *func_node = (AstFuncNode*)node;
                        if (((TypeFunc*)func_node->type)->is_public) {
                            Item key = {.item = s2it(heap_create_name(
                                func_node->name->chars, func_node->name->len))};
                            Item fn_item = js_get_key_default(ns, key);
                            if (get_type_id(fn_item) == LMD_TYPE_FUNC) {
                                void* fn_ptr = js_function_get_ptr(fn_item);
                                *(main_func_t*)mod_def = (main_func_t)fn_ptr;
                
                            } else {
                                *(main_func_t*)mod_def = NULL;
                                log_debug("cross-lang fn '%.*s' not found in namespace",
                                    (int)func_node->name->len, func_node->name->chars);
                            }
                            mod_def += sizeof(main_func_t);
                            // No _b wrapper for JS functions (synthetic nodes have no typed params)
                        }
                    }
                    node = node->next;
                }
            } else {
                // Regular Lambda module import
                typedef void (*init_consts_fn)(void**);
                init_consts_fn init_fn = (init_consts_fn)find_func(import->script->jit_context, "_init_mod_consts");
                if (init_fn) {
                    log_debug("Initializing module constants for %.*s", (int)(import->module.length), import->module.str);
                    init_fn(import->script->const_list->data);
                } else {
                    log_debug("Module %.*s has no _init_mod_consts (may have no constants)",
                        (int)(import->module.length), import->module.str);
                }

                // Initialize the module's type_list by calling _init_mod_types
                typedef void (*init_types_fn)(void*);
                init_types_fn init_types = (init_types_fn)find_func(import->script->jit_context, "_init_mod_types");
                if (init_types) {
                    log_debug("Initializing module type_list for %.*s", (int)(import->module.length), import->module.str);
                    init_types(import->script->type_list);
                } else {
                    log_debug("Module %.*s has no _init_mod_types (may have no types)",
                        (int)(import->module.length), import->module.str);
                }

                // skip consts pointer field
                mod_def += sizeof(void**);

                // populate _mod_main: module's main() entry point
                *(main_func_t*) mod_def = import->script->main_func;
                log_debug("set _mod_main for %.*s: %p", (int)(import->module.length), import->module.str, import->script->main_func);
                mod_def += sizeof(main_func_t);

                // populate _init_vars: function that copies module globals into Mod struct
                typedef void (*init_vars_fn)(void*);
                init_vars_fn init_vars_func = (init_vars_fn)find_func(import->script->jit_context, "_init_mod_vars");
                *(init_vars_fn*) mod_def = init_vars_func;
                log_debug("set _init_vars for %.*s: %p", (int)(import->module.length), import->module.str, (void*)init_vars_func);
                mod_def += sizeof(init_vars_fn);

                // populate function pointer fields for each public function
                AstNode *node = import->script->ast_root;
                assert(node->node_type == AST_SCRIPT);
                node = ((AstScript*)node)->child;
                while (node) {
                    log_debug("checking node: %d", node->node_type);
                    if (node->node_type == AST_NODE_CONTENT) {
                        node = ((AstListNode*)node)->item;  // drill down
                        continue;
                    }
                    else if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR || node->node_type == AST_NODE_PROC) {
                        AstFuncNode *func_node = (AstFuncNode*)node;
                        if (((TypeFunc*)func_node->type)->is_public) {
                            // get func addr
                            StrBuf *func_name = strbuf_new();
                            write_fn_name(func_name, func_node, NULL);
                            log_debug("loading fn addr: %s from script: %s", func_name->str, import->script->reference);
                            void* fn_ptr = find_func(import->script->jit_context, func_name->str);
                            log_debug("got imported fn: %s, func_ptr: %p", func_name->str, fn_ptr);
                            strbuf_free(func_name);
                            *(main_func_t*) mod_def = (main_func_t)fn_ptr;
                            mod_def += sizeof(main_func_t);

                            // also populate _b boxed wrapper pointer if this function needs fn_call* wrapper
                            if (node->node_type != AST_NODE_PROC && needs_fn_call_wrapper(func_node)) {
                                StrBuf *wrapper_name = strbuf_new();
                                write_fn_name_ex(wrapper_name, func_node, NULL, "_b");
                                log_debug("loading boxed wrapper fn: %s", wrapper_name->str);
                                void* b_ptr = find_func(import->script->jit_context, wrapper_name->str);
                                log_debug("got boxed wrapper fn: %s, ptr: %p", wrapper_name->str, b_ptr);
                                strbuf_free(wrapper_name);
                                *(main_func_t*) mod_def = (main_func_t)b_ptr;
                                mod_def += sizeof(main_func_t);
                            }
                        }
                    }
                    // pub var fields are populated at runtime by _init_mod_vars, skip pointer arithmetic
                    // (struct layout matches but values are set when module main() runs)
                    else if (node->node_type == AST_NODE_PUB_STAM) {
                        // no-op: pub vars initialized via _init_mod_vars at runtime
                    }
                    node = node->next;
                }
            }
        }
        child = child->next;
    }
    RETURN:
    log_leave();
}

// Both tiers finish a load the same way: the Script-sized prefix of the
// Transpiler carries the AST, const/type lists and whichever artifact the tier
// produced (a linked MIR context, or a frame plan and nothing else).
void script_adopt_transpiler(Script* script, Transpiler* tp) {
    if (!script || !tp) return;
    memcpy(script, tp, sizeof(Script));
}

// a parent that falls back to MIR cannot link a dependency that was already
// admitted to T0: MIR imports require the child's generated symbols. Demote
// the complete loaded cone in post-order before compiling that parent.
static bool interp_force_jit_script(Script* script, Runtime* runtime) {
    if (!script || !runtime) return false;
    if (script->direct_imports) {
        for (int i = 0; i < script->direct_imports->length; i++) {
            Script* dep = (Script*)script->direct_imports->data[i];
            if (!interp_force_jit_script(dep, runtime)) return false;
        }
    }
    if (script->jit_context) return true;
    if (!script->interp_supported) {
        log_error("interp: fallback dependency '%s' has no executable tier",
            script->reference ? script->reference : "<unknown>");
        return false;
    }

    Transpiler tp = {};
    memcpy(&tp, script, sizeof(Script));
    tp.runtime = runtime;
    script->interp_supported = false;
    script->interp_planned = false;
    compile_script_as_mir_direct(&tp, script, script->reference, NULL, NULL,
        NULL, NULL, NULL, NULL);
    if (!script->jit_context) {
        log_error("interp: failed to lower fallback dependency '%s' to MIR",
            script->reference ? script->reference : "<unknown>");
        return false;
    }
    interp_run_stats()->scripts_fallback++;
    log_notice("interp: demoted dependency file=%s to MIR fallback",
        script->reference ? script->reference : "<unknown>");
    return true;
}

static bool interp_force_jit_import_cone(Transpiler* tp) {
    if (!tp || !tp->direct_imports) return true;
    for (int i = 0; i < tp->direct_imports->length; i++) {
        Script* dep = (Script*)tp->direct_imports->data[i];
        if (!interp_force_jit_script(dep, tp->runtime)) return false;
    }
    return true;
}

static bool initialize_script_ast_storage(Transpiler* tp) {
    Input* input_base = Input::create(
        mem_pool_create(NULL, MEM_ROLE_AST, "script.pool"), nullptr);
    if (!input_base) {
        log_error("Error: Failed to initialize Input base");
        return false;
    }
    tp->pool = input_base->pool;
    tp->arena = input_base->arena;
    tp->name_pool = input_base->name_pool;
    tp->shape_pool = input_base->shape_pool;
    tp->type_list = input_base->type_list;
    tp->url = input_base->url;
    tp->path = input_base->path;
    tp->root = input_base->root;
    tp->const_list = arraylist_new(16);
    return tp->const_list != NULL;
}

void transpile_script(Transpiler *tp, Script* script, const char* script_path) {
    if (!script || !script->source) {
        log_error("Error: Source code is NULL");
        return;
    }
    log_notice("Start transpiling %s...", script_path);
    win_timer start, end;

    // Phase profiling: use high-res timer for release-accurate timing.
    bool profiling = is_profile_enabled();
    bool compiler_timing = lambda_compiler_timing_enabled();
    if (compiler_timing) lambda_compiler_timing_reset();
    profile_time_t p0, p1, p2, p3;
    if (profiling || compiler_timing) profile_get_time(&p0);

    get_time(&start);
    tp->source = script->source;
    AstScript* direct_root = NULL;
    LambdaParseError parse_error = {};
    if (!initialize_script_ast_storage(tp) ||
            lambda_rd_build_ast(tp, tp->source, strlen(tp->source),
                &direct_root, &parse_error) != LAMBDA_PARSE_OK || !direct_root) {
        // A direct-parser failure must enter the structured diagnostic lane so
        // callers receive the same source-aware error contract as other inputs.
        record_direct_parse_diagnostics(tp, script_path, &parse_error);
        log_error("C parser rejected %s: %s", script_path,
            parse_error.message ? parse_error.message : "direct AST reduction failed");
        return;
    }
    tp->ast_root = (AstNode*)direct_root;
    get_time(&end);
    print_elapsed_time("parsing", start, end);

    if (profiling || compiler_timing) profile_get_time(&p1);

    if (profiling || compiler_timing) profile_get_time(&p2);
    // Publish the first production pass contract now: all later Lambda work
    // consumes the indexed identity table rather than rediscovering children
    // or owners. The remaining legacy passes are added to this schedule as
    // their inputs/outputs become explicit.
    CompilerPassManager pass_manager;
    compiler_pass_manager_init(&pass_manager,
        COMPILER_FACT_AST | COMPILER_FACT_BOUND | COMPILER_FACT_VALIDATED);
    AstIndexPassContext index_context = {
        &tp->ast_index, tp->ast_root, tp->profile};
    CompilerPassSpec index_pass = {"index",
        COMPILER_FACT_AST | COMPILER_FACT_BOUND | COMPILER_FACT_VALIDATED,
        COMPILER_FACT_INDEXED, ast_index_compiler_pass, &index_context};
    if (!compiler_pass_manager_add(&pass_manager, &index_pass) ||
            !compiler_pass_manager_run(&pass_manager, NULL)) {
        log_error("failed to run indexed AST pass for '%s'", script_path);
        return;
    }
    if (profiling || compiler_timing) profile_get_time(&p3);
    get_time(&end);
    print_elapsed_time("building AST", start, end);

    // ANY-census [Type_Infer TI3]: one line per compile naming where static
    // types fell back to `any`. Purely diagnostic — later inference slices
    // prove their effect by the delta, not by reading the emitter.
    {
        int any_total = 0;
        for (int r = 0; r < ANY_REASON_COUNT; r++) any_total += tp->any_census[r];
        if (any_total > 0) {
            StrBuf* census = strbuf_new();
            if (census) {
                strbuf_append_format(census, "any_census: total=%d", any_total);
                for (int r = 0; r < ANY_REASON_COUNT; r++) {
                    if (!tp->any_census[r]) continue;
                    strbuf_append_format(census, " %s=%d",
                        any_reason_name((AnyReason)r), tp->any_census[r]);
                }
                log_notice("%s (%s)", census->str, script_path);
                strbuf_free(census);
            }
        }
    }

    // Check for errors during AST building
    if (tp->error_count > 0) {
        log_error("compiled '%s' with error!!", script_path);
        return;
    }

    // T0 (D8.1.1v2): stop after the AST passes and interpret. A script whose
    // pre-scan finds a kind the walker cannot execute falls back to the whole
    // module JIT path below, counted and logged — never silently half-run (R4).
    if (lambda_tier_selected() == LAMBDA_TIER_INTERP ||
            lambda_tier_selected() == LAMBDA_TIER_AUTO) {
        profile_time_t plan0, plan1;
        if (profiling || compiler_timing) profile_get_time(&plan0);
        // `direct_imports` is normally filled inside compile_script_as_mir_direct,
        // which T0 skips — but the cone drives module init order, so record it
        // here before the tier decision is published.
        AstScript* interp_root = (AstScript*)tp->ast_root;
        if (tp->direct_imports) { arraylist_free(tp->direct_imports); tp->direct_imports = NULL; }
        for (AstNode* child = interp_root ? interp_root->child : NULL; child;
                child = child->next) {
            if (child->node_type != AST_NODE_IMPORT) continue;
            AstImportNode* imp = (AstImportNode*)child;
            if (imp->is_cross_lang || !imp->script) continue;
            if (!tp->direct_imports) tp->direct_imports = arraylist_new(4);
            arraylist_append(tp->direct_imports, imp->script);
        }
        AstNodeType reject = AST_NODE_NULL;
        bool supported = interp_scan_supported(tp, &reject) && interp_plan_script(tp);
        if (profiling || compiler_timing) profile_get_time(&plan1);
        if (supported) {
            tp->interp_supported = true;
            script_adopt_transpiler(script, tp);
            if (compiler_timing) {
                LambdaCompilerTiming* timing = &g_last_lambda_compiler_timing;
                timing->parse_us = (uint64_t)(elapsed_ms_val(p0, p1) * 1000.0);
                timing->ast_build_us = (uint64_t)(elapsed_ms_val(p1, p2) * 1000.0);
                timing->index_us = (uint64_t)(elapsed_ms_val(p2, p3) * 1000.0);
                timing->plan_us = (uint64_t)(elapsed_ms_val(plan0, plan1) * 1000.0);
                timing->build_transpile_us = timing->parse_us + timing->ast_build_us +
                    timing->index_us + timing->plan_us;
                timing->valid = 1;
            }
            if (profiling) {
                PhaseProfile prof;
                memset(&prof, 0, sizeof(prof));
                profile_set_script_path(&prof, script_path);
                prof.parse_ms = elapsed_ms_val(p0, p1);
                prof.ast_ms = elapsed_ms_val(p1, p2) + elapsed_ms_val(p2, p3);
                prof.plan_ms = elapsed_ms_val(plan0, plan1);
                prof.worker_thread = 0;
                prof.thread_id = profile_current_thread_id();
                profile_record_phase(&prof);
            }
            log_notice("interp: planned file=%s module_slots=%u",
                script_path, (unsigned)tp->interp_slab_count);
            return;
        }
        tp->interp_reject_kind = reject;
        interp_run_stats()->scripts_fallback++;
        log_notice("interp: fallback file=%s reason=node:%s",
            script_path, interp_node_kind_name(reject));
        if (!interp_force_jit_import_cone(tp)) return;
    }

    // compile the AST directly to MIR; this is the only supported Lambda backend.
    {
        double mir_jit_init_ms = 0, mir_transpile_ms = 0, mir_gen_ms = 0;
        uint64_t mir_module_count = 0;
        uint64_t mir_function_count = 0;
        uint64_t mir_instruction_count = 0;
        compile_script_as_mir_direct(tp, script, script_path,
                                      profiling || compiler_timing ? &mir_jit_init_ms : NULL,
                                      profiling || compiler_timing ? &mir_transpile_ms : NULL,
                                      profiling || compiler_timing ? &mir_gen_ms : NULL,
                                      compiler_timing ? &mir_module_count : NULL,
                                      compiler_timing ? &mir_function_count : NULL,
                                      compiler_timing ? &mir_instruction_count : NULL);
        if (compiler_timing) {
            LambdaCompilerTiming* timing = &g_last_lambda_compiler_timing;
            timing->parse_us = (uint64_t)(elapsed_ms_val(p0, p1) * 1000.0);
            timing->ast_build_us = (uint64_t)(elapsed_ms_val(p1, p2) * 1000.0);
            timing->index_us = (uint64_t)(elapsed_ms_val(p2, p3) * 1000.0);
            timing->module_finalize_us = (uint64_t)(mir_jit_init_ms * 1000.0);
            timing->mir_lower_us = (uint64_t)(mir_transpile_ms * 1000.0);
            timing->link_us = (uint64_t)(mir_gen_ms * 1000.0);
            timing->build_transpile_us = timing->parse_us + timing->ast_build_us + timing->index_us +
                timing->module_finalize_us + timing->mir_lower_us + timing->link_us;
            timing->mir_module_count = mir_module_count;
            timing->mir_function_count = mir_function_count;
            timing->mir_insn_count = mir_instruction_count;
            timing->valid = 1;
        }
        if (profiling) {
            PhaseProfile prof;
            memset(&prof, 0, sizeof(prof));
            profile_set_script_path(&prof, script_path);
            prof.parse_ms = elapsed_ms_val(p0, p1);
            prof.ast_ms = elapsed_ms_val(p1, p2);
            prof.transpile_ms = mir_transpile_ms;
            prof.jit_init_ms = mir_jit_init_ms;
            prof.mir_gen_ms = mir_gen_ms;
            prof.code_len = 0;
            prof.worker_thread = 0;
            prof.thread_id = profile_current_thread_id();
            profile_record_phase(&prof);
        }
        return;
    }

}

// ============================================================================

Script* load_script(Runtime *runtime, const char* script_path, const char* source, bool is_import) {
    log_info("Loading script: %s (is_import=%d)", script_path, is_import);

    // Normalize path to canonical absolute path for reliable deduplication
    // (skip for source-provided scripts like REPL which have synthetic paths)
    const char* lookup_path = script_path;
    char* canonical_path = NULL;
    if (!source) {
        canonical_path = file_realpath(script_path);
        if (canonical_path) {
            lookup_path = canonical_path;
        }
    }

    // find the script in the path index (thread-safe)
#ifndef _WIN32
    pthread_mutex_lock(&scripts_mutex);
#endif
    Script* cached_script = runtime_script_index_get_current(runtime, lookup_path);
    if (cached_script) {
        // circular import detection: script is in list but still being loaded
        if (cached_script->is_loading) {
#ifndef _WIN32
            pthread_mutex_unlock(&scripts_mutex);
#endif
            log_error("Circular import detected: %s", lookup_path);
            fprintf(stderr, "Error: Circular import detected: %s\n", lookup_path);
            if (canonical_path) mem_free(canonical_path);
            return NULL;
        }
#ifndef _WIN32
        pthread_mutex_unlock(&scripts_mutex);
#endif
        runtime->mir_cache_hits++;
        log_info("mir cache index: hit path=%s index=%d retained=%d",
                 lookup_path, cached_script->index, cached_script->cache_retain ? 1 : 0);
        if (canonical_path) mem_free(canonical_path);
        return cached_script;
    }
    runtime->mir_cache_misses++;
    log_info("mir cache index: miss path=%s", lookup_path);
    // script not found — create stub and register immediately to prevent duplicates
    Script *new_script = (Script*)mem_calloc(1, sizeof(Script), MEM_CAT_SYSTEM);
    new_script->reference = mem_strdup(lookup_path, MEM_CAT_SYSTEM);
    new_script->is_loading = true;
    new_script->profile = &lambda_profile;
    runtime_register_script(runtime, new_script);
#ifndef _WIN32
    pthread_mutex_unlock(&scripts_mutex);
#endif

    // strdup when source is provided externally (e.g. REPL) so the script owns its copy
    // and runtime_cleanup can safely free it without a double-free
    const char* script_source = source ? mem_strdup(source, MEM_CAT_SYSTEM) : read_text_file(lookup_path);
    if (!script_source) {
        log_error("Error: Failed to read source code from %s", lookup_path);
        // failed stubs must leave neither a live slot nor an index entry for later imports
        int failed_index = new_script->index;
        runtime_free_script(runtime, new_script, true);
        if (runtime->scripts && failed_index >= 0 && failed_index < runtime->scripts->length) {
            runtime->scripts->data[failed_index] = NULL;
        }
        if (canonical_path) mem_free(canonical_path);
        return NULL;
    }

    // extract directory from script path for script-relative imports
    const char* last_slash = strrchr(lookup_path, '/');
#ifdef _WIN32
    const char* last_backslash = strrchr(lookup_path, '\\');
    if (last_backslash && (!last_slash || last_backslash > last_slash))
        last_slash = last_backslash;
#endif
    if (!is_import && runtime->import_base_dir) {
        // use caller-specified import base directory for main script
        new_script->directory = mem_strdup(runtime->import_base_dir, MEM_CAT_SYSTEM);
    } else if (last_slash) {
        int dir_len = (int)(last_slash - lookup_path + 1);
        char* dir = (char*)mem_alloc(dir_len + 1, MEM_CAT_SYSTEM);
        memcpy(dir, lookup_path, dir_len);
        dir[dir_len] = '\0';
        new_script->directory = dir;
    } else {
        new_script->directory = mem_strdup("./", MEM_CAT_SYSTEM);
    }
    log_debug("script directory: %s", new_script->directory);
    new_script->source = script_source;
    capture_script_file_stat(new_script, lookup_path, source == NULL || is_import);
    if (canonical_path) mem_free(canonical_path);
    log_debug("script source length: %d", (int)strlen(new_script->source));
    new_script->is_main = !is_import;  // main script is not an import
    new_script->cache_retain = false;

    // Initialize decimal context (use shared unlimited context for transpiler)
    new_script->decimal_ctx = decimal_unlimited_context();

    Transpiler transpiler;  memset(&transpiler, 0, sizeof(Transpiler));
    memcpy(&transpiler, new_script, sizeof(Script));
    transpiler.script_owner = new_script;
    transpiler.runtime = runtime;
    transpiler.error_count = 0;
    transpiler.max_errors = runtime->max_errors > 0 ? runtime->max_errors : 10;  // use runtime setting or default 10
    transpiler.errors = arraylist_new(8);  // initialize error list for structured errors
    // relaxed mode (--static-warning): semantic type errors report as
    // warnings and compilation proceeds (SI3v2/TI6 per-surface policy)
    transpiler.static_warning = runtime->static_warning;
    transpiler.warning_count = 0;
    transpiler.warnings = NULL;  // created lazily on first downgraded diagnostic

    transpile_script(&transpiler, new_script, script_path);
    new_script->is_loading = false;  // loading complete

    // Print downgraded static warnings first (--static-warning relaxed mode);
    // they never fail the compile, so the script result follows below them.
    if (transpiler.warnings && transpiler.warnings->length > 0) {
        fprintf(stderr, "\n");
        for (int i = 0; i < transpiler.warnings->length; i++) {
            LambdaError* warning = (LambdaError*)transpiler.warnings->data[i];
            err_print_warning(warning);
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "%d static warning(s) (--static-warning relaxed mode).\n",
            transpiler.warnings->length);
    }

    // Print structured errors if any
    if (transpiler.errors && transpiler.errors->length > 0) {
        fprintf(stderr, "\n");
        for (int i = 0; i < transpiler.errors->length; i++) {
            LambdaError* error = (LambdaError*)transpiler.errors->data[i];
            err_print(error);
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "%d error(s) found.\n", transpiler.errors->length);
    }

    free_transpiler_diagnostics(&transpiler);

    // check for compilation failure — a T0-planned script deliberately has no
    // MIR context, so its success signal is the frame plan instead.
    if (!new_script->jit_context && !new_script->interp_supported) {
        log_error("Error: Failed to compile script %s", script_path);
        return NULL;
    }
    // L1 cache can be disabled for timing runs while preserving same-run import dedup.
    new_script->cache_retain = is_import && runtime->use_mir_direct &&
        !runtime->mir_cache_disabled && !new_script->cache_cross_lang_tainted;
    if (new_script->cache_retain) {
        log_info("mir cache index: retaining import path=%s index=%d", new_script->reference, new_script->index);
    }
    runtime->mir_cache_compiles++;

    // Register in unified module registry for cross-language imports.
    // Only when the runtime context (heap, name_pool) is already initialized —
    // module_build_lambda_namespace creates heap objects (maps, strings).
    // During pure Lambda→Lambda compilation, context isn't set up yet (it's
    // initialized later by runner_setup_context). The JS→Lambda path sets up
    // context before calling load_script, so registration works there.
    if (!new_script->is_main && context && context->heap) {
        Item ns = module_build_lambda_namespace(new_script);
        module_register_for_runtime(
            runtime, new_script->reference, "lambda", ns, new_script->jit_context);
    }

    log_debug("loaded script main func: %s, %p", script_path, new_script->main_func);
    return new_script;
}

Script* load_script_mir_direct(Runtime *runtime, const char* script_path,
                               const char* source, bool is_import) {
    if (!runtime) return NULL;
    // Cross-language loaders do not enter run_script_mir(), so select the sole
    // MIR Direct backend explicitly before loading the module.
    bool was_mir_direct = runtime->use_mir_direct;
    runtime->use_mir_direct = true;
    // The JS membrane reaches a Lambda export through a native function
    // pointer, so this module must actually be JIT-compiled. Under AUTO the
    // planner would stop at T0 and produce no MIR context at all, leaving
    // module_build_lambda_namespace with nothing to export and the JS side
    // reporting "is not a function". Pin the tier for the load only.
    LambdaTier was_tier = lambda_tier_selected();
    lambda_tier_set(LAMBDA_TIER_JIT);
    Script* script = load_script(runtime, script_path, source, is_import);
    lambda_tier_set(was_tier);
    runtime->use_mir_direct = was_mir_direct;
    return script;
}

static void repl_restore_scope(NameScope* scope, NameEntry* first,
        NameEntry* last) {
    if (!scope) return;
    scope->first = first;
    scope->last = last;
    if (last) last->next = NULL;
}

static void repl_restore_source(Script* script, size_t length) {
    if (!script || !script->repl_source) return;
    script->repl_source->length = length;
    script->repl_source->str[length] = '\0';
    script->source = script->repl_source->str;
}

// The REPL owns its diagnostics: nothing downstream prints a rejected
// fragment's errors, so they are reported here before the list is released.
// Freeing them silently left the user with a bare "rolled back" notice and no
// diagnosis at all — every parse and type message the REPL exists to teach
// with was being dropped.
static void repl_report_transpiler_errors(ArrayList* errors) {
    if (!errors) return;
    for (int i = 0; i < errors->length; i++) {
        LambdaError* error = (LambdaError*)errors->data[i];
        if (error) err_print(error);
    }
}

static void repl_free_transpiler_errors(ArrayList* errors) {
    free_transpiler_error_list(errors);
}

void interp_repl_session_destroy(InterpReplSession* session) {
    if (!session) return;
    Runtime* runtime = session->runner.runtime;
    Script* script = session->runner.script;
    if (runtime && script) {
        // Each REPL Script receives a unique module id. Releasing its exact
        // root before freeing the Script prevents `clear` from pinning its
        // former bindings until the whole Runtime exits (D5.3.3).
        lambda_module_state_release(script->module_state_id);
        int index = script->index;
        runtime_free_script(runtime, script, true);
        if (runtime->scripts && index >= 0 && index < runtime->scripts->length) {
            runtime->scripts->data[index] = NULL;
        }
    }
    memset(session, 0, sizeof(*session));
}

bool interp_repl_session_init(InterpReplSession* session, Runtime* runtime) {
    if (!session || !runtime) return false;
    interp_repl_session_destroy(session);

    // The normal loader owns AST/pool initialization. Build the empty session
    // through its T0 branch even when the shell's default remains eager JIT.
    LambdaTier saved_tier = lambda_tier_selected();
    lambda_tier_set(LAMBDA_TIER_INTERP);
    Script* script = load_script(runtime, "<repl-session>", "", false);
    lambda_tier_set(saved_tier);
    if (!script || !script->interp_supported) {
        log_error("interp-repl: could not create initial interpreter module");
        return false;
    }
    script->repl_source = strbuf_new_cap(256);
    if (!script->repl_source) {
        log_error("interp-repl: could not allocate retained source state");
        // The bootstrap source still has Script ownership until both retained
        // buffers exist; clear partial replacements before common teardown.
        if (script->repl_source) {
            strbuf_free(script->repl_source);
            script->repl_source = NULL;
        }
        runner_init(runtime, &session->runner);
        session->runner.script = script;
        interp_repl_session_destroy(session);
        return false;
    }
    // `load_script` owns the empty bootstrap buffer; source thereafter aliases
    // the growable session buffer and runtime_free_script frees it as one unit.
    mem_free((void*)script->source);
    script->source = script->repl_source->str;

    runner_init(runtime, &session->runner);
    session->runner.script = script;
    runner_setup_context(&session->runner);
    if (!session->runner.context || !lambda_module_state_prepare(
            script->module_state_id, script->interp_slab_count)) {
        log_error("interp-repl: could not prepare persistent module slab");
        interp_repl_session_destroy(session);
        return false;
    }
    session->initialized = true;
    return true;
}

Item interp_repl_session_eval(InterpReplSession* session, const char* source) {
    if (session) session->last_input_rejected = false;
    if (!session || !session->initialized || !session->runner.runtime ||
            !session->runner.script || !source) return (session->last_input_rejected = true), ItemError;
    Script* script = session->runner.script;
    AstScript* root = (AstScript*)script->ast_root;
    if (!root || !script->repl_source) return (session->last_input_rejected = true), ItemError;

    size_t saved_source_length = script->repl_source->length;
    size_t prefix_length = saved_source_length;
    if (prefix_length) {
        strbuf_append_char(script->repl_source, '\n');
        prefix_length++;
    }
    strbuf_append_str(script->repl_source, source);
    script->source = script->repl_source->str;

    NameScope* globals = root->global_vars;
    NameEntry* saved_scope_first = globals ? globals->first : NULL;
    NameEntry* saved_scope_last = globals ? globals->last : NULL;
    int saved_const_count = script->const_list ? script->const_list->length : 0;
    int saved_type_count = script->type_list ? script->type_list->length : 0;
    uint32_t saved_slab_count = script->interp_slab_count;

    Transpiler tp = {};
    memcpy(&tp, script, sizeof(Script));
    tp.script_owner = script;
    tp.runtime = session->runner.runtime;
    tp.current_scope = globals;
    tp.max_errors = session->runner.runtime->max_errors > 0
        ? session->runner.runtime->max_errors : 10;
    tp.errors = arraylist_new(4);

    AstScript* parsed_root = NULL;
    LambdaParseError parse_error = {};
    const char* fragment_source = script->source + prefix_length;
    LambdaParseStatus parse_status = lambda_rd_build_ast(&tp, fragment_source,
        strlen(source), &parsed_root, &parse_error);
    if (parse_status != LAMBDA_PARSE_OK || !parsed_root) {
        record_direct_parse_diagnostics(&tp, "<repl>", &parse_error);
        repl_restore_scope(globals, saved_scope_first, saved_scope_last);
        if (script->const_list) script->const_list->length = saved_const_count;
        if (script->type_list) script->type_list->length = saved_type_count;
        script->interp_slab_count = saved_slab_count;
        repl_restore_source(script, saved_source_length);
        repl_report_transpiler_errors(tp.errors);
        repl_free_transpiler_errors(tp.errors);
        log_error("interp-repl: direct parser rejected completed input");
        return (session->last_input_rejected = true), ItemError;
    }

    AstNode* fragment = parsed_root->child;
    if (tp.error_count != 0 || !fragment) {
        repl_restore_scope(globals, saved_scope_first, saved_scope_last);
        if (script->const_list) script->const_list->length = saved_const_count;
        if (script->type_list) script->type_list->length = saved_type_count;
        script->interp_slab_count = saved_slab_count;
        repl_restore_source(script, saved_source_length);
        repl_free_transpiler_errors(tp.errors);
        return (session->last_input_rejected = true), ItemError;
    }
    // Direct parsing is intentionally fragment-local for REPL latency. Rebase
    // every retained AST span before the fragment sees the append-only source.
    lambda_ast_shift_source_spans(fragment, (uint32_t)prefix_length);
    repl_free_transpiler_errors(tp.errors);

    AstScript scan_root = {};
    scan_root.node_type = AST_SCRIPT;
    scan_root.child = fragment;
    Script scan_script = {};
    scan_script.ast_root = (AstNode*)&scan_root;
    scan_script.profile = script->profile;
    AstNodeType reject = AST_NODE_NULL;
    bool supported = interp_scan_supported(&scan_script, &reject);
    bool planned = supported && interp_plan_repl_fragment(script, fragment);
    bool slab_grown = planned && lambda_module_state_grow_vars(
        script->module_state_id, script->interp_slab_count);
    if (!supported || !planned || !slab_grown) {
        repl_restore_scope(globals, saved_scope_first, saved_scope_last);
        if (script->const_list) script->const_list->length = saved_const_count;
        if (script->type_list) script->type_list->length = saved_type_count;
        if (!slab_grown) script->interp_slab_count = saved_slab_count;
        repl_restore_source(script, saved_source_length);
        log_error("interp-repl: rejected fragment node=%s",
            interp_node_kind_name(reject));
        return (session->last_input_rejected = true), ItemError;
    }

    AstNode* prior_last = script->repl_last_top_level;
    AstNode* fragment_last = fragment;
    while (fragment_last->next) fragment_last = fragment_last->next;
    if (prior_last) prior_last->next = fragment;
    else root->child = fragment;
    if (!ast_index_append_profile(&script->ast_index, fragment,
            (AstNode*)root, script->profile)) {
        if (prior_last) prior_last->next = NULL;
        else root->child = NULL;
        repl_restore_scope(globals, saved_scope_first, saved_scope_last);
        if (script->const_list) script->const_list->length = saved_const_count;
        if (script->type_list) script->type_list->length = saved_type_count;
        ast_index_build_profile(&script->ast_index, script->ast_root, script->profile);
        repl_restore_source(script, saved_source_length);
        return (session->last_input_rejected = true), ItemError;
    }

    LambdaModuleStateSnapshot snapshot = {};
    if (!lambda_module_state_snapshot(script->module_state_id, &snapshot)) {
        if (prior_last) prior_last->next = NULL;
        else root->child = NULL;
        repl_restore_scope(globals, saved_scope_first, saved_scope_last);
        if (script->const_list) script->const_list->length = saved_const_count;
        if (script->type_list) script->type_list->length = saved_type_count;
        ast_index_build_profile(&script->ast_index, script->ast_root, script->profile);
        repl_restore_source(script, saved_source_length);
        return (session->last_input_rejected = true), ItemError;
    }
    Item result = interp_run_repl_fragment(&session->runner, fragment);
    if (item_is_error(result)) {
        lambda_module_state_restore(script->module_state_id, &snapshot);
        if (prior_last) prior_last->next = NULL;
        else root->child = NULL;
        repl_restore_scope(globals, saved_scope_first, saved_scope_last);
        if (script->const_list) script->const_list->length = saved_const_count;
        if (script->type_list) script->type_list->length = saved_type_count;
        ast_index_build_profile(&script->ast_index, script->ast_root, script->profile);
        repl_restore_source(script, saved_source_length);
        lambda_module_state_snapshot_dispose(&snapshot);
        return result;
    }
    lambda_module_state_snapshot_dispose(&snapshot);
    script->repl_last_top_level = fragment_last;
    return result;
}

void runner_init(Runtime *runtime, Runner* runner) {
    memset(runner, 0, sizeof(Runner));
    runner->runtime = runtime;
}

EvalContext* runtime_get_eval_context(Runtime* runtime) {
    if (!runtime) return NULL;
    if (!runtime->eval_context) {
        runtime->eval_context = (EvalContext*)mem_alloc(sizeof(EvalContext), MEM_CAT_EVAL);
        if (!runtime->eval_context) {
            log_error("runtime-context: failed to allocate canonical EvalContext");
            return NULL;
        }
        memset(runtime->eval_context, 0, sizeof(EvalContext));
    }
    runtime->eval_context->runtime = runtime;
    return runtime->eval_context;
}

#include "../../lib/url.h"
#include "../validator/validator.hpp"
#include "lambda-stack.h"

void runner_setup_context(Runner* runner) {
    log_debug("runner setup exec context");
    if (!runner || !runner->runtime || !runner->script) {
        log_error("runtime-context: runner setup requires Runtime and Script");
        return;
    }
    EvalContext* ctx = runtime_get_eval_context(runner->runtime);
    if (!ctx) return;
    runner->context = ctx;

    // Initialize stack overflow protection (once per thread)
    lambda_stack_init();

    // Store stack_limit in context for fast access from JIT-compiled code
    ctx->stack_limit = _lambda_stack_limit;

    ctx->pool = runner->script->pool;
    ctx->type_list = runner->script->type_list;

    ctx->type_info = type_info;
    ctx->consts = runner->script->const_list->data;
    ctx->result = ItemNull;  // exec result
    if (ctx->cwd) {
        // A new REPL session may replace an unexecuted predecessor; its CWD
        // never reached the normal execution-boundary cleanup in that case.
        url_destroy(ctx->cwd);
        ctx->cwd = NULL;
    }
    ctx->cwd = get_current_dir();  // proper URL object for current directory
    // initialize decimal context (use shared fixed-precision context for runtime)
    ctx->decimal_ctx = decimal_fixed_context();
    ctx->context_alloc = heap_alloc;
    // init AST validator
    ctx->validator = schema_validator_create(ctx->pool);

    // Initialize error handling and stack trace support
    // Use debug_info from script (built after MIR compilation for address → function mapping)
    ctx->debug_info = runner->script->debug_info;
    ctx->current_file = runner->script->reference;  // source file for error reporting
    ctx->current_vargs = NULL;
    if (ctx->last_error) {
        // The canonical context may carry an error until the shell consumes it.
        err_free(ctx->last_error);
        ctx->last_error = NULL;
    }

    input_context = (Context*)ctx;
    if (!eval_context_init(ctx)) return;
    // The side-stack bind resolves its owner through the thread's context
    // identity, so it must follow eval_context_init: on a fresh
    // thread the earlier ordering silently bound nothing.
    if (!lambda_side_stack_bind()) {
        log_error("runner side-stack: failed to initialize execution regions");
    }
    // Phase 5: propagate ui_mode and result_arena from Runtime to context
    Runtime* ui_rt = runner->runtime;
    if (ui_rt && ui_rt->ui_mode && ui_rt->result_arena) {
        ctx->ui_mode = true;
        ctx->arena = ui_rt->result_arena;
    }

    // Reuse or create the GC heap and name_pool from the Runtime.
    // These persist across multiple evaluations on the same Runtime.
    Runtime* rt = runner->runtime;
    if (rt && rt->heap) {
        // Reuse retained heap and name_pool from a previous evaluation
        log_debug("runner_setup_context: reusing retained heap from Runtime");
        ctx->heap = rt->heap;
        ctx->name_pool = rt->name_pool;
        ctx->pool = ctx->heap->pool;
    } else {
        // First evaluation on this Runtime — create fresh resources
        ctx->name_pool = name_pool_create_runtime(ctx->pool);
        if (!ctx->name_pool) {
            log_error("Failed to create runtime name_pool");
        }
        heap_init();
        ctx->pool = ctx->heap->pool;
        // Store on Runtime for reuse
        if (rt) {
            rt->heap = ctx->heap;
            rt->name_pool = ctx->name_pool;
        }
    }
    path_register_pool_provider(runner_path_pool_provider);

    if (rt && rt->scheduler) {
        ctx->scheduler = rt->scheduler;
    } else {
        ctx->scheduler = lambda_scheduler_create(LAMBDA_MAILBOX_DEFAULT_CAPACITY);
        if (rt) rt->scheduler = ctx->scheduler;
    }
    if (rt && rt->js_bootstrap_context) {
        // The Lambda runner now owns every heap resource created while its JS
        // imports were compiled. Move the JS capsule before freeing the shell;
        // otherwise callbacks would retain semantic state in dead stack-like
        // bootstrap storage.
        if (rt->js_bootstrap_context != ctx && !ctx->js_state) {
            ctx->js_state = rt->js_bootstrap_context->js_state;
            rt->js_bootstrap_context->js_state = NULL;
        }
        // JS import setup can already be using Runtime's canonical context.
        // That owner survives until runtime_cleanup, so freeing this alias here
        // left runner setup dereferencing a released EvalContext.
        if (rt->js_bootstrap_context != ctx) {
            mem_free(rt->js_bootstrap_context);
        }
        rt->js_bootstrap_context = NULL;
    }
    // Radiant/Jube Lambda calls reuse JS DOM primitives even without importing
    // JavaScript. Initialize the derived capsule once for this eval-thread
    // lifetime so those native helpers can read their paired TLS state.
    if (!js_runtime_state_init(ctx)) return;

    // Initialize template registry for view/edit template dispatch
    if (!g_template_registry) {
        g_template_registry = template_registry_new();
    }
}

// Helper function to recursively resolve all sys:// paths in an Item tree.
// This must be called while the execution context is still valid.  Map fields
// use the same shape-aware reader as ordinary member access; reading their
// packed bytes as raw Items was the cause of the old map-walk crash.
extern "C" Item path_resolve_for_iteration(Path* path);

// Module-state instantiation from mir.c. It runs before execution and owns
// the one-time slab allocation/root publication for a sealed module.
extern "C" bool prepare_context_module_state(void* mir_ctx, void* consts,
                                              void* type_list);

void resolve_sys_paths_recursive(Item item);

static void resolve_sys_paths_in_shape(TypeMap* map_type, void* map_data) {
    if (!map_type || !map_data) return;
    FOR_EACH_MAP_FIELD(map_type, field) {
        resolve_sys_paths_recursive(map_shape_field_to_item(map_data, field));
    }
}

void resolve_sys_paths_recursive(Item item) {
    TypeId type_id = get_type_id(item);
    if (type_id == LMD_TYPE_PATH) {
        Path* path = item.path;
        if (path && path_get_scheme(path) == PATH_SCHEME_SYS && path->result == 0) {
            Item resolved = path_resolve_for_iteration(path);
            if (resolved.item != ItemNull.item && resolved.item != ItemError.item) {
                resolve_sys_paths_recursive(resolved);
            }
        } else if (path && path->result != 0) {
            resolve_sys_paths_recursive((Item){.item = path->result});
        }
    } else if (type_id == LMD_TYPE_ARRAY) {
        List* list = item.array;
        if (!list || !list->items) return;
        for (int64_t i = 0; i < list->length; i++) {
            resolve_sys_paths_recursive(list->items[i]);
        }
    } else if (type_id == LMD_TYPE_MAP) {
        Map* map = item.map;
        if (map) resolve_sys_paths_in_shape((TypeMap*)map->type, map->data);
    } else if (type_id == LMD_TYPE_OBJECT) {
        Object* object = item.object;
        if (object) resolve_sys_paths_in_shape((TypeMap*)object->type, object->data);
    } else if (type_id == LMD_TYPE_ELEMENT) {
        Element* element = item.element;
        if (!element) return;
        resolve_sys_paths_in_shape((TypeMap*)element->type, element->data);
        if (!element->items) return;
        for (int64_t i = 0; i < element->length; i++) {
            resolve_sys_paths_recursive(element->items[i]);
        }
    }
}

// Common helper function to execute a compiled script and wrap the result in an Input*
// The GC heap is retained on the Runtime — caller calls runtime_cleanup() when done.

Input* execute_script_and_create_output(Runner* runner, bool run_main) {
    if (!runner->script || !runner->script->main_func) {
        log_error("Error: Failed to compile the function.");
        Pool* error_pool = mem_pool_create(NULL, MEM_ROLE_AST, "script.result");
        Input* output = Input::create(error_pool, nullptr);
        if (!output) {
            log_error("Failed to create error output Input");
            if (error_pool) pool_destroy(error_pool);
            return nullptr;
        }
        output->root = ItemError;
        return output;
    }

    log_notice("Executing JIT compiled code...");
    runner_setup_context(runner);
    EvalContext* ctx = runner->context;
    if (!ctx) return nullptr;

    // Establish the script's context-owned global binding slab.
    if (runner->script->jit_context) {
        if (!prepare_context_module_state((void*)runner->script->jit_context,
                runner->script->const_list ? runner->script->const_list->data : nullptr,
                runner->script->type_list)) return nullptr;
    }

    // set the run_main flag in the execution context
    ctx->run_main = run_main;
    log_debug("Set context run_main = %s", run_main ? "true" : "false");

    // Keep the frame outside automatic storage: siglongjmp makes automatic
    // objects modified after setjmp indeterminate, but this boundary must
    // inspect and restore its checkpoint after the jump.
    Item result = ItemError;
    LambdaRecoveryFrame* recovery_frame = lambda_recovery_frame_begin_for(
        (Context*)context, LAMBDA_RECOVERY_CAP_EXECUTION_BOUNDARY);
    if (!recovery_frame) {
        log_error("exec: failed to allocate recovery frame");
        result = context->result = lambda_recovery_publish_fault_item(
            (Context*)context, LAMBDA_FAULT_OUT_OF_MEMORY, ERR_OK);
    } else if (LAMBDA_RECOVERY_FRAME_SETJMP(recovery_frame)) {
        Item recovered = ItemError;
        if (!lambda_recovery_frame_restore_landing(recovery_frame)) {
            log_error("exec: recovery frame landing invariant failed");
            recovered = lambda_recovery_publish_fault_item((Context*)context,
                LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK);
        } else {
            recovered = lambda_recovery_frame_fault_item((Context*)context,
                recovery_frame);
        }
        _lambda_stack_overflow_flag = false;
        lambda_recovery_frame_end(recovery_frame);
        result = context->result = recovered;
    } else {
        if (!lambda_recovery_frame_arm(recovery_frame)) {
            log_error("exec: failed to arm recovery frame");
            lambda_recovery_frame_end(recovery_frame);
            result = context->result = lambda_recovery_publish_fault_item(
                (Context*)context, LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK);
        } else {
            log_debug("exec main func");
            result = context->result = runner->script->main_func(context);
            lambda_recovery_frame_end(recovery_frame);
            log_debug("after main func, result type_id=%d", get_type_id(result));
        }
    }
    if ((!runner->runtime || !runner->runtime->no_task_drain) && context->scheduler) {
        lambda_scheduler_drain(context->scheduler);
    }
    if (context->heap) {
        context->heap->result_root = context->result.item;
    }

    preserve_context_last_error(result);

    // Create output Input with its own pool (independent from Script's pool)
    // This allows safe cleanup of the execution context and heap
    log_debug("Creating output Input with independent pool");
    Pool* output_pool = mem_pool_create(NULL, MEM_ROLE_AST, "script.result");
    Input* output = Input::create(output_pool, nullptr);
    if (!output) {
        log_error("Failed to create output Input");
        if (output_pool) pool_destroy(output_pool);
        if (ctx->cwd) {
            url_destroy(ctx->cwd);
            ctx->cwd = NULL;
        }
        return nullptr;
    }

    // Resolve all sys:// paths in result (while context is still valid)
    resolve_sys_paths_recursive(result);
    if (ctx->cwd) {
        url_destroy(ctx->cwd);
        ctx->cwd = NULL;
    }

    // Return result directly on the GC heap — no deep_copy needed.
    // With GC-managed memory the heap is retained across the session;
    // the caller is responsible for calling runtime_cleanup() when done.
    output->root = result;

    log_debug("Script execution completed, returning output Input");
    return output;
}

// Installs runtime_cleanup into the DOM layer's hook so dom_document_destroy()
// can clean up a document's reactive lambda_runtime without dom_element.cpp
// hard-linking runner.cpp (keeps input/DOM unit tests free of the runtime).
extern "C" void dom_set_runtime_cleanup_hook(void (*fn)(Runtime*));
extern "C" void jube_register_builtin_modules(void);

void runtime_init(Runtime* runtime) {
    memset(runtime, 0, sizeof(Runtime));
    // MIR Direct is the sole Lambda backend; keep the mode bit true for cache
    // and import scheduling code that still uses it as a fast-path predicate.
    runtime->use_mir_direct = true;
    runtime->scripts = arraylist_new(16);
    runtime->script_index = script_index_new(64);
    runtime->max_errors = 10;  // default error threshold
    runtime->optimize_level = 2;  // default MIR optimization level (0=debug, 2=release)
    runtime->dry_run = false;  // default: real IO
    const char* disable_mir_cache = shell_getenv("LAMBDA_DISABLE_MIR_CACHE");
    runtime->mir_cache_disabled = (LAMBDA_MIR_CACHE_DEFAULT == 0) ||
        (disable_mir_cache &&
         (strcmp(disable_mir_cache, "1") == 0 || strcmp(disable_mir_cache, "true") == 0));
    // debug and release builds enable retained MIR imports by default; this opt-out is for timing and emergency bisecting.
    if (runtime->mir_cache_disabled) {
        log_info("mir cache index: retained module cache disabled by build default or LAMBDA_DISABLE_MIR_CACHE");
    }
    // The CLI creates a short-lived selector Runtime before some language
    // subcommands create their execution Runtime. Keep the registry lazy so
    // that selector never owns a module-registry allocation it cannot use;
    // module registration paths create it on their first real module.
    jube_register_builtin_modules();
    dom_set_runtime_cleanup_hook(runtime_cleanup);  // wire DOM-layer cleanup hook
}

void runtime_register_script(Runtime* runtime, Script* script) {
    if (!runtime || !script) return;
    // Reserve the module-state identity first, and independently of the script
    // list. The slabs this id indexes live on the EvalContext, so a Script that
    // never gets one keeps id 0 and shares slot 0 with whatever already owns it.
    // That is harmless only while both layouts happen to agree; a document
    // runtime built by script_runner has no script list (it is allocated
    // without runtime_init), so every Lambda module loaded into a JS page took
    // id 0 and collapsed onto the JS realm's module state — "sealed layout
    // changed for module 0", which left the dom package with no templates
    // (ESO34). Allocation comes from the owning Runtime's counter, the same one
    // lambda_module_state_reserve() uses, so Lambda and JS ids never overlap.
    script->module_state_id = runtime->next_module_state_id++;
    if (!runtime->scripts) {
        // No script list on this runtime: path dedup and the script index are
        // unavailable, but the identity above is still valid and unique.
        log_debug("runtime_register_script: no script list on runtime %p; "
                  "'%s' keeps module_state_id=%u without path dedup",
                  (void*)runtime, script->reference ? script->reference : "<none>",
                  script->module_state_id);
        return;
    }
    arraylist_append(runtime->scripts, script);
    script->index = runtime->scripts->length - 1;
    runtime_script_index_put(runtime, script);
}

// runtime::type_list can alias a Script's Input-owned list while a nested
// Lambda package is evaluated. The Script remains the owner of that alias.
bool runtime_type_list_is_script_owned(Runtime* runtime) {
    if (!runtime || !runtime->type_list || !runtime->scripts) return false;
    for (int i = 0; i < runtime->scripts->length; i++) {
        Script* script = (Script*)runtime->scripts->data[i];
        if (script && script->type_list == runtime->type_list) return true;
    }
    return false;
}

// Release every Script this runtime owns, plus the list and path index.
// Hosts that tear a runtime down by hand (script_runner's per-document JS
// runtime) must call this too: a Lambda module loaded into such a runtime is
// owned by nothing else, and skipping it leaks the Script and its pool.
void runtime_free_all_scripts(Runtime* runtime) {
    if (!runtime) return;
    if (runtime->scripts) {
        for (int i = 0; i < runtime->scripts->length; i++) {
            Script *script = (Script*)runtime->scripts->data[i];
            if (!script) continue;
            runtime_free_script(runtime, script, false);
            runtime->scripts->data[i] = NULL;
        }
        arraylist_free(runtime->scripts);
        runtime->scripts = NULL;
    }
    if (runtime->script_index) {
        hashmap_free(runtime->script_index);
        runtime->script_index = NULL;
    }
}

void runtime_free_script(Runtime* runtime, Script* script, bool remove_index) {
    if (!script) return;
    if (remove_index && script->reference) {
        runtime_script_index_delete_script(runtime, script);
    }
    // Hosted owners release their language-specific facts before the shared
    // AST/Input storage disappears. The hook never owns common Script fields.
    if (script->destroy_extension) {
        script->destroy_extension(script);
        script->destroy_extension = NULL;
    }
    if (script->reference) mem_free((void*)script->reference);
    if (script->repl_source) strbuf_free(script->repl_source);
    else if (script->source) mem_free((void*)script->source);
    if (script->directory) mem_free((void*)script->directory);
    // The T0 load path keeps the indexed AST alive for the Script's lifetime
    // (AIO4) instead of releasing it at the MIR handoff; destroying a zeroed
    // AstIndex is a no-op, so this covers both tiers.
    ast_index_destroy(&script->ast_index);
    if (script->const_list) {
        arraylist_free(script->const_list);
        script->const_list = NULL;
    }
    decimal_constants_release(script->decimal_constants);
    script->decimal_constants = NULL;
    input_release_auxiliary_resources((Input*)script);
    if (runtime && runtime->eval_context && runtime->eval_context->validator &&
            runtime->eval_context->validator->get_pool() == script->pool) {
        // The validator is allocated beside this Script; clear the shared
        // context before its pool is destroyed to avoid a stale cleanup owner.
        schema_validator_destroy(runtime->eval_context->validator);
        runtime->eval_context->validator = NULL;
    }
    if (script->pool) pool_destroy(script->pool);
    if (script->direct_imports) arraylist_free(script->direct_imports);
    if (script->jit_context) {
        jit_cleanup_mode(script->jit_context, script->mir_gen_initialized ? 1 : 0);
    }
    // decimal context is shared global; cached/free paths only clear the borrowed pointer
    script->decimal_ctx = NULL;
    mem_free(script);
}

void runtime_teardown_batch_scripts(Runtime* runtime) {
    if (!runtime || !runtime->scripts) return;
    for (int i = 0; i < runtime->scripts->length; i++) {
        Script* script = (Script*)runtime->scripts->data[i];
        if (!script) continue;
        // retained modules keep compile pools/JIT contexts alive until invalidation or cleanup
        if (script->cache_retain && !script->cache_retired) continue;
        runtime_free_script(runtime, script, true);
        runtime->scripts->data[i] = NULL;
    }
}

void runtime_release_script_generation(Runtime* runtime, int first_script_index,
        uint32_t first_module_state_id) {
    if (!runtime) return;
    // The batch realm has already dropped callbacks, globals, and module
    // registry entries. Retire the exact tail before the next test reuses its
    // module IDs; otherwise an old AST can retain a mismatched sealed slab.
    js_release_global_var_module_bindings_from(first_module_state_id);
    lambda_module_state_release_from(first_module_state_id);
    if (runtime->scripts) {
        int first = first_script_index;
        if (first < 0) first = 0;
        if (first > runtime->scripts->length) first = runtime->scripts->length;
        for (int i = runtime->scripts->length - 1; i >= first; i--) {
            Script* script = (Script*)runtime->scripts->data[i];
            if (script) runtime_free_script(runtime, script, true);
        }
        if (first < runtime->scripts->length) {
            arraylist_remove_range(runtime->scripts, first,
                runtime->scripts->length - first);
        }
    }
    if (runtime->next_module_state_id > first_module_state_id) {
        runtime->next_module_state_id = first_module_state_id;
    }
}

void runtime_log_mir_cache_summary(Runtime* runtime) {
    if (!runtime) return;
    int lookups = runtime->mir_cache_hits + runtime->mir_cache_misses;
    double hit_rate = lookups > 0 ? (100.0 * (double)runtime->mir_cache_hits / (double)lookups) : 0.0;
    size_t retained = runtime->script_index ? hashmap_count(runtime->script_index) : 0;
    log_info("mir cache index: summary modules_cached=%zu compiles_saved=%d hit_rate=%.1f%% compiles=%d hits=%d misses=%d invalidations=%d disabled=%d",
             retained, runtime->mir_cache_hits, hit_rate,
             runtime->mir_cache_compiles, runtime->mir_cache_hits,
             runtime->mir_cache_misses, runtime->mir_cache_invalidations,
             runtime->mir_cache_disabled ? 1 : 0);
    (void)retained;
    (void)hit_rate;
}

// Reset the retained heap and name_pool on a Runtime.
// Used between independent evaluations (e.g. test-batch) so that each
// script starts with a clean GC heap.  The next runner_setup_context()
// call will create fresh heap/name_pool state and store it back.
void runtime_reset_heap(Runtime* runtime) {
    if (!runtime) return;
    if (runtime->heap) {
        EvalContext* cleanup_context = runtime_get_eval_context(runtime);
        if (!cleanup_context) return;
        if (!eval_context_init(cleanup_context)) return;
        cleanup_context->heap = runtime->heap;
        cleanup_context->name_pool = runtime->name_pool;
        cleanup_context->type_list = runtime->type_list;
        cleanup_context->result = ItemNull;
        cleanup_context->scheduler = runtime->scheduler;
        if (cleanup_context->js_state &&
                !js_runtime_state_init(cleanup_context)) return;
        if (cleanup_context->last_error) {
            // Diagnostics can own allocations from the retiring heap. Clear
            // them before teardown so the next batch never frees a stale
            // context-owned error while setting up its fresh heap.
            err_free(cleanup_context->last_error);
            cleanup_context->last_error = NULL;
        }
        // The editor may retain document Items allocated by this heap. Tear it
        // down while its owning context is still bound.
        edit_bridge_destroy();
        render_map_destroy();
        tmpl_state_destroy();
        // Template entries retain both name-pool strings and JIT body pointers
        // from this evaluation. Keeping them across heap replacement let the
        // next script dispatch through unmapped code (D5.4.3).
        TemplateRegistry* template_registry = cleanup_context->template_registry;
        cleanup_context->template_registry = NULL;
        template_registry_destroy(template_registry);

        if (runtime->js_runtime_used) {
            // Cross-language JS caches retain Items from the current heap.
            // Reset them before heap destruction so a later batch script cannot
            // dereference stale Promise/module state from the preceding script.
            js_batch_reset();
        }

        if (runtime->scheduler) {
            lambda_scheduler_destroy(runtime->scheduler);
            runtime->scheduler = NULL;
        }
        if (runtime->js_runtime_used) {
            js_event_loop_shutdown();
            lambda_uv_cleanup();
            runtime->js_runtime_used = false;
        }

        // Every module namespace is a heap Item owned by this Runtime.  Drop
        // the registry even for Lambda-only batches, which do not enter the JS
        // reset path that historically happened to clear the global cache.
        module_registry_cleanup_for_runtime(runtime);

        // Batch heap replacement invalidates module-owned callback Items just
        // as final runtime teardown does; release those roots before the GC.
        jube_notify_heap_cleanup(runtime->heap);
        // Module bindings and ICs are context-owned slabs.  Drop their precise
        // root registrations and bulk-clear them while the old heap is still
        // current; the next module instantiation re-registers once.
        lambda_module_state_reset();

        if (runtime->type_list) {
            // a nested package may publish its Script-owned type list through
            // the runtime context; let runtime_free_script release that owner.
            bool script_owned = runtime_type_list_is_script_owned(runtime);
            if (!script_owned) arraylist_free(runtime->type_list);
            runtime->type_list = NULL;
        }

        js_runtime_state_release_heap_resources();
        heap_destroy();
        runtime->heap = NULL;
        cleanup_context->heap = NULL;
        // D4.2.1v2/RN-NamePool: GC finalizers may still inspect NameRecords;
        // release the dedicated runtime pool only after heap destruction.
        if (runtime->name_pool) {
            name_pool_release(runtime->name_pool);
            runtime->name_pool = NULL;
        }
        cleanup_context->name_pool = NULL;
        cleanup_context->type_list = NULL;
        cleanup_context->scheduler = NULL;
    }
    if (runtime->js_bootstrap_context) {
        // Cross-language imports can use the canonical EvalContext directly;
        // freeing that alias here left the next batch binding a dead context.
        if (runtime->js_bootstrap_context != runtime_get_eval_context(runtime)) {
            mem_free(runtime->js_bootstrap_context);
        }
        runtime->js_bootstrap_context = NULL;
    }
}

void runtime_cleanup(Runtime* runtime) {
    if (!runtime) return;
    EvalContext* cleanup_owner = runtime->eval_context;
    if (cleanup_owner) {
        if (!eval_context_init(cleanup_owner)) return;
        if (cleanup_owner->js_state &&
                !js_runtime_state_init(cleanup_owner)) return;
        if (cleanup_owner->cwd) {
            // A session can end before its first execution; unlike the JIT
            // output path, that leaves its per-execution cwd URL to cleanup.
            url_destroy(cleanup_owner->cwd);
            cleanup_owner->cwd = NULL;
        }
    }
    // Dump profiling data if enabled (before freeing anything)
    profile_dump_to_file();
    js_opt_trace_dump();

    js_canvas_cleanup();
    module_registry_cleanup_for_runtime(runtime);
    TemplateRegistry* template_registry = runtime->eval_context
        ? runtime->eval_context->template_registry : NULL;
    if (runtime->eval_context) runtime->eval_context->template_registry = NULL;
    template_registry_destroy(template_registry);
    js_eval_preamble_cache_reset();
    js_fetch_reset();

    bool event_loop_cleaned = false;

    // Destroy retained execution state (heap and name_pool)
    if (runtime->heap) {
        EvalContext* cleanup_context = cleanup_owner
            ? cleanup_owner : runtime_get_eval_context(runtime);
        if (!cleanup_context) return;
        if (!eval_context_init(cleanup_context)) return;
        cleanup_context->heap = runtime->heap;
        cleanup_context->name_pool = runtime->name_pool;
        cleanup_context->type_list = runtime->type_list;
        cleanup_context->result = ItemNull;
        if (cleanup_context->js_state &&
                !js_runtime_state_init(cleanup_context)) return;

        // Destruction follows the same owner-bound path as heap replacement.
        edit_bridge_destroy();
        render_map_destroy();
        tmpl_state_destroy();

        if (runtime->scheduler) {
            cleanup_context->scheduler = runtime->scheduler;
            lambda_scheduler_destroy(runtime->scheduler);
            runtime->scheduler = NULL;
        }

        if (cleanup_context->js_state) js_event_loop_shutdown();
        lambda_uv_cleanup();
        event_loop_cleaned = true;

        js_dom_shutdown();
        if (runtime->dom_doc) {
            free_document((DomDocument*)runtime->dom_doc);
            runtime->dom_doc = NULL;
        }
        runtime->dom_ui_context = NULL;

        // Intrinsic cache entries own native precise-root slots outside the GC
        // pool; release them while their heap is current and before leak accounting.
        if (cleanup_context->js_state) js_intrinsic_state_teardown();

        // Jube modules may cache heap-owned callbacks across repeated page
        // interactions; release those roots before this heap disappears.
        jube_notify_heap_cleanup(runtime->heap);

        print_heap_entries();
        check_memory_leak();

        if (runtime->type_list) {
            // a nested package may publish its Script-owned type list through
            // the runtime context; let runtime_free_script release that owner.
            bool script_owned = runtime_type_list_is_script_owned(runtime);
            if (!script_owned) arraylist_free(runtime->type_list);
            runtime->type_list = NULL;
        }

        js_runtime_state_release_heap_resources();
        if (cleanup_context->js_state) {
            // Full JS capsule destruction can release function-owned module
            // bindings, so keep both the JS realm and its slabs alive until it
            // has completed while the owning heap is still valid.
            if (!js_runtime_state_thread_matches(cleanup_context)) return;
            js_runtime_state_destroy_context();
        }
        // DOM and JS cleanup can dispose callbacks that still activate their
        // defining module slab; destroy those slabs only after that cleanup.
        lambda_module_state_destroy();
        heap_destroy();
        runtime->heap = NULL;
        cleanup_context->heap = NULL;
        // D4.2.1v2/RN-NamePool: GC finalizers can traverse name-backed
        // shapes, so the dedicated runtime pool outlives heap teardown.
        if (runtime->name_pool) {
            name_pool_release(runtime->name_pool);
            runtime->name_pool = NULL;
        }
        cleanup_context->name_pool = NULL;
        cleanup_context->type_list = NULL;
        cleanup_context->scheduler = NULL;
    } else {
        js_dom_shutdown();
        if (runtime->dom_doc) {
            free_document((DomDocument*)runtime->dom_doc);
            runtime->dom_doc = NULL;
        }
        runtime->dom_ui_context = NULL;
    }
    if (!event_loop_cleaned) {
        if (runtime->eval_context && runtime->eval_context->js_state) {
            if (!js_runtime_state_init(runtime->eval_context)) return;
            js_event_loop_shutdown();
        }
        lambda_uv_cleanup();
    }
    if (runtime->js_bootstrap_context) {
        // A failed cross-language compile can leave the bootstrap pointer
        // aliased to the canonical EvalContext. Freeing it here makes the
        // later last_error cleanup dereference a dead context; the canonical
        // owner is released in the eval_context block below.
        if (runtime->js_bootstrap_context != runtime->eval_context) {
            mem_free(runtime->js_bootstrap_context);
        }
        runtime->js_bootstrap_context = NULL;
    }
    if (runtime->eval_context) {
        EvalContext* retiring_context = runtime->eval_context;
        if (runtime->eval_context->last_error) {
            err_free(runtime->eval_context->last_error);
            runtime->eval_context->last_error = NULL;
        }
        if (runtime->eval_context->validator) {
            // validator registries use heap allocations outside the script pool.
            schema_validator_destroy(runtime->eval_context->validator);
            runtime->eval_context->validator = NULL;
        }
        js_runtime_state_destroy_context();
        lambda_module_state_destroy();
        if (!eval_context_shutdown(retiring_context)) return;
        mem_free(runtime->eval_context);
        runtime->eval_context = NULL;
    }
    lambda_stack_cleanup();
    runtime_free_all_scripts(runtime);
}
