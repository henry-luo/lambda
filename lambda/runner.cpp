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
#include "../lib/hashmap_helpers.h"
#include "../lib/thread_pool.h"
#include "mark_builder.hpp"
#include "lambda-decimal.hpp"
#include "lambda-error.h"
#include "runtime/lambda-stack.h"
#include "runtime/side_stack.h"
#include "concurrency.h"
#include "module_registry.h"
#include "jube/jube_registry.h"
#include "js/js_runtime.h"
#include "js/js_event_loop.h"
#include "input/css/css_style.hpp"
#include "template_registry.h"
#include "../lib/file.h"
#include "../lib/mem_factory.h"
#include "../lib/memtrack.h"
#include "../lib/file_utils.h"
#include "../lib/shell.h"
#include "../lib/uv_loop.h"

extern "C" Item js_property_get(Item object, Item key);
extern "C" void js_dom_shutdown(void);
struct DomDocument;
extern void free_document(DomDocument* doc);

#ifndef LAMBDA_MIR_CACHE_DEFAULT
#define LAMBDA_MIR_CACHE_DEFAULT 1
#endif

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
#define PROFILE_MAX_IMPORT_LEVELS 64
#define PROFILE_PATH_MAX 512

typedef struct PhaseProfile {
    char script_path[PROFILE_PATH_MAX];
    double parse_ms;
    double ast_ms;
    double transpile_ms;
    double jit_init_ms;
    double file_write_ms;
    double c2mir_ms;
    double mir_gen_ms;
    int code_len;
    int worker_thread;
    unsigned long thread_id;
} PhaseProfile;

typedef struct ImportLevelProfile {
    int level;
    int modules;
    int jobs;
    int threads;
    int cpu_cap;
    double elapsed_ms;
} ImportLevelProfile;

bool profile_enabled = false;
bool profile_checked = false;
PhaseProfile profile_data[PROFILE_MAX_SCRIPTS];
int profile_count = 0;
ImportLevelProfile import_level_profile_data[PROFILE_MAX_IMPORT_LEVELS];
int import_level_profile_count = 0;
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

static void profile_record_import_level(const ImportLevelProfile* profile) {
    if (!profile) return;
#ifndef _WIN32
    pthread_mutex_lock(&profile_mutex);
#endif
    if (import_level_profile_count < PROFILE_MAX_IMPORT_LEVELS) {
        import_level_profile_data[import_level_profile_count++] = *profile;
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
    fprintf(f, "# Phase-Level Profile (LAMBDA_PROFILE=1)\n");
    fprintf(f, "# script | parse | ast | transpile | jit_init | file_write | c2mir | mir_gen | total | code_len | worker | thread_id\n");
    for (int i = 0; i < profile_count; i++) {
        PhaseProfile* p = &profile_data[i];
        double total = p->parse_ms + p->ast_ms + p->transpile_ms +
                       p->jit_init_ms + p->file_write_ms + p->c2mir_ms + p->mir_gen_ms;
        fprintf(f, "%s\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%d\t%d\t%lu\n",
                p->script_path, p->parse_ms, p->ast_ms, p->transpile_ms,
                p->jit_init_ms, p->file_write_ms, p->c2mir_ms, p->mir_gen_ms,
                total, p->code_len, p->worker_thread, p->thread_id);
    }
    if (import_level_profile_count > 0) {
        fprintf(f, "\n# Parallel Import Levels\n");
        fprintf(f, "# level | modules | jobs | threads | cpu_cap | elapsed_ms\n");
        for (int i = 0; i < import_level_profile_count; i++) {
            ImportLevelProfile* p = &import_level_profile_data[i];
            fprintf(f, "%d\t%d\t%d\t%d\t%d\t%.3f\n",
                    p->level, p->modules, p->jobs, p->threads, p->cpu_cap, p->elapsed_ms);
        }
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
TSParser* lambda_parser(void);
TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
void ensure_jit_imports_initialized(void);
}
#ifdef LAMBDA_C2MIR
void transpile_ast_root(Transpiler* tp, AstScript *script);
#endif
void ensure_sys_func_maps_initialized(void);
void check_memory_leak();
void print_heap_entries();

// thread-specific runtime context is provided by runtime/runtime-state.cpp.
extern __thread Context* input_context;

// Thread-local parser for parallel module compilation.
// When non-NULL, load_script() uses this instead of runtime->parser.
static __thread TSParser* tls_parser = NULL;

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

// Persistent last error (survives beyond runner lifetime)
// This is needed because context points to runner's stack-allocated EvalContext
__thread LambdaError* persistent_last_error = NULL;

// Accessor for persistent error from other modules
LambdaError* get_persistent_last_error() {
    return persistent_last_error;
}

void clear_persistent_last_error() {
    if (persistent_last_error) {
        err_free(persistent_last_error);
        persistent_last_error = NULL;
    }
}

void preserve_context_last_error(EvalContext* ctx, Item result) {
    if (!ctx || !ctx->last_error) {
        return;
    }

    if (get_type_id(result) == LMD_TYPE_ERROR) {
        clear_persistent_last_error();  // free any previous error
        persistent_last_error = ctx->last_error;
        ctx->last_error = NULL;  // transfer ownership
        return;
    }

    // error() values can be consumed by total equality, so a non-error result must drop stale diagnostics.
    err_free(ctx->last_error);
    ctx->last_error = NULL;
}

// Helper functions for C code to access EvalContext members (used by path.c)
extern "C" {
Pool* eval_context_get_pool(EvalContext* ctx) {
    if (!ctx || !ctx->heap) return nullptr;
    return ctx->heap->pool;
}

NamePool* eval_context_get_name_pool(EvalContext* ctx) {
    if (!ctx) return nullptr;
    return ctx->name_pool;
}
}

static Pool* runner_path_pool_provider(void) {
    return context ? eval_context_get_pool(context) : NULL;
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
                ModuleDescriptor* desc = module_get(import->script->reference);
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
                            Item fn_item = js_property_get(ns, key);
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

#ifdef LAMBDA_C2MIR
extern unsigned int lambda_lambda_h_len;
#endif

void transpile_script(Transpiler *tp, Script* script, const char* script_path) {
    if (!script || !script->source) {
        log_error("Error: Source code is NULL");
        return;
    }
    log_notice("Start transpiling %s...", script_path);
    win_timer start, end;

    // Phase profiling: use high-res timer for release-accurate timing.
    bool profiling = is_profile_enabled();
    profile_time_t p0, p1, p2;
    if (profiling) profile_get_time(&p0);

    // create a parser
    get_time(&start);
    // parse the source
    tp->source = script->source;
    tp->syntax_tree = lambda_parse_source(tp->parser, tp->source);
    if (tp->syntax_tree == NULL) {
        log_error("Error: Failed to parse the source code.");
        return;
    }
    get_time(&end);
    print_elapsed_time("parsing", start, end);

    if (profiling) profile_get_time(&p1);

#ifndef NDEBUG
    // print the syntax tree as an s-expr
    print_ts_root(tp->source, tp->syntax_tree);
#endif

    // check if the syntax tree is valid
    TSNode root_node = ts_tree_root_node(tp->syntax_tree);
    if (ts_node_has_error(root_node)) {
        log_error("Syntax tree has errors.");

        // collect structured parse errors
        if (!tp->errors) tp->errors = arraylist_new(8);
        find_errors(root_node, tp->source, script_path, tp->errors);
        tp->error_count = tp->errors->length;
        return;
    }

    // build the AST from the syntax tree
    get_time(&start);

    // Initialize Input base class (Script extends Input)
    Input* input_base = Input::create(mem_pool_create(NULL, MEM_ROLE_AST, "script.pool"), nullptr);
    if (!input_base) {
        log_error("Error: Failed to initialize Input base");
        return;
    }

    // Copy Input fields to Script (Script extends Input)
    tp->pool = input_base->pool;
    tp->arena = input_base->arena;
    tp->name_pool = input_base->name_pool;
    tp->type_list = input_base->type_list;
    tp->url = input_base->url;
    tp->path = input_base->path;
    tp->root = input_base->root;

    // Initialize Script-specific fields
    tp->const_list = arraylist_new(16);

    if (strcmp(ts_node_type(root_node), "document") != 0) {
        log_error("Error: The tree has no valid root node.");
        return;
    }
    // build the AST
    tp->ast_root = build_script(tp, root_node);
    get_time(&end);
    print_elapsed_time("building AST", start, end);

    if (profiling) profile_get_time(&p2);

    // Check for errors during AST building
    if (tp->error_count > 0) {
        log_error("compiled '%s' with error!!", script_path);
        return;
    }

    // MIR Direct path: skip C code generation entirely; compile the AST straight to MIR.
    // compile_script_as_mir_direct() handles import registration, transpile_mir_ast(),
    // MIR_link(), and stores jit_context/main_func on the script.
    if (tp->runtime && tp->runtime->use_mir_direct) {
        double mir_jit_init_ms = 0, mir_transpile_ms = 0, mir_gen_ms = 0;
        compile_script_as_mir_direct(tp, script, script_path,
                                      profiling ? &mir_jit_init_ms : NULL,
                                      profiling ? &mir_transpile_ms : NULL,
                                      profiling ? &mir_gen_ms : NULL);
        if (profiling) {
            PhaseProfile prof;
            memset(&prof, 0, sizeof(prof));
            profile_set_script_path(&prof, script_path);
            prof.parse_ms = elapsed_ms_val(p0, p1);
            prof.ast_ms = elapsed_ms_val(p1, p2);
            prof.transpile_ms = mir_transpile_ms;
            prof.jit_init_ms = mir_jit_init_ms;
            prof.file_write_ms = 0;
            prof.c2mir_ms = 0;
            prof.mir_gen_ms = mir_gen_ms;
            prof.code_len = 0;
            prof.worker_thread = tls_parser ? 1 : 0;
            prof.thread_id = profile_current_thread_id();
            profile_record_phase(&prof);
        }
        return;
    }

#ifdef LAMBDA_C2MIR
    profile_time_t p3, p4, p5, p6, p7;  // only used in the C2MIR pipeline below
    // print the AST for debugging
    log_debug("AST: %s ---------", tp->reference);
    print_ast_root(tp);

    // transpile the AST to C code
    log_debug("transpiling...");
    get_time(&start);
    tp->code_buf = strbuf_new_cap(1024);
    transpile_ast_root(tp, (AstScript*)tp->ast_root);
    get_time(&end);
    print_elapsed_time("transpiling", start, end);

    if (profiling) profile_get_time(&p3);

    // Check for errors during transpilation
    if (tp->error_count > 0) {
        log_error("compiled '%s' with error!!", script_path);
        strbuf_free(tp->code_buf);  tp->code_buf = NULL;
        return;
    }

    // JIT compile the C code
    get_time(&start);
    tp->jit_context = jit_init(tp->runtime->optimize_level);

    if (profiling) profile_get_time(&p4);

    // compile user code to MIR
    log_debug("compiling to MIR...");
    // Write transpiled C code when:
    //   (a) user explicitly passed --transpile-dir (transpile_dir != NULL), or
    //   (b) dev mode: log is configured to a file (not stdout/stderr)
    // In release mode without --transpile-dir, skip the write to keep the working directory clean.
    bool log_to_file = (log_default_category && log_default_category->output &&
        log_default_category->output != stdout && log_default_category->output != stderr);
    const char* write_dir = tp->runtime->transpile_dir;
    if (write_dir || log_to_file) {
        if (!write_dir) write_dir = "temp";  // dev mode fallback
        char transpiled_filename[256];
        create_dir_recursive(write_dir);
        snprintf(transpiled_filename, sizeof(transpiled_filename), "%s/_transpiled_%d.c", write_dir, script->index);
        write_text_file(transpiled_filename, tp->code_buf->str);
    }

    if (profiling) profile_get_time(&p5);

    int profile_code_len = (int)tp->code_buf->length;
    char* code = tp->code_buf->str + lambda_lambda_h_len;
    // printf("code len: %d\n", (int)strlen(code));
    log_debug("transpiled code (first 500 chars):\n---------%.500s", code);
    fflush(NULL);  // force flush all open streams for large log
    jit_compile_to_mir(tp->jit_context, tp->code_buf->str, tp->code_buf->length, script_path);

    if (profiling) profile_get_time(&p6);

    strbuf_free(tp->code_buf);  tp->code_buf = NULL;
    // generate native code and return the function
    tp->main_func = (main_func_t)jit_gen_func(tp->jit_context, "main");
    get_time(&end);

    if (profiling) profile_get_time(&p7);

    // Record profiling data
    if (profiling) {
        PhaseProfile prof;
        memset(&prof, 0, sizeof(prof));
        profile_set_script_path(&prof, script_path);
        prof.parse_ms = elapsed_ms_val(p0, p1);
        prof.ast_ms = elapsed_ms_val(p1, p2);
        prof.transpile_ms = elapsed_ms_val(p2, p3);
        prof.jit_init_ms = elapsed_ms_val(p3, p4);
        prof.file_write_ms = elapsed_ms_val(p4, p5);
        prof.c2mir_ms = elapsed_ms_val(p5, p6);
        prof.mir_gen_ms = elapsed_ms_val(p6, p7);
        prof.code_len = profile_code_len;
        prof.worker_thread = tls_parser ? 1 : 0;
        prof.thread_id = profile_current_thread_id();
        profile_record_phase(&prof);
    }

    // Build debug info table for stack traces (after MIR_link has assigned addresses)
    // Pass func_name_map so MIR internal names are mapped to Lambda user-friendly names
    tp->debug_info = (ArrayList*)build_debug_info_table(tp->jit_context, tp->func_name_map);

    // init lambda imports
    init_module_import(tp, (AstScript*)tp->ast_root);

    log_info("JIT compiled %s", script_path);
    log_debug("jit_context: %p, main_func: %p, debug_info: %p", tp->jit_context, tp->main_func, tp->debug_info);
    // copy value back to script
    memcpy(script, tp, sizeof(Script));
    script->main_func = tp->main_func;

    print_elapsed_time("JIT compiling", start, end);
#else
    // C2MIR not available — this should not be reached
    log_error("C2MIR path not available in this build (use MIR Direct)");
#endif // LAMBDA_C2MIR
}

// ============================================================================
// Parallel Module Compilation
// ============================================================================
// Pre-discovers all import dependencies and compiles modules in parallel,
// organized by topological depth (leaves first, dependents after).
// Enabled only for MIR Direct path with ≥3 modules on non-Windows platforms.

#ifndef _WIN32

// Import graph node for dependency discovery
typedef struct {
    char* path;        // canonical absolute path (owned)
    char* source;      // source text (owned)
    char* directory;   // directory for relative imports (owned)
    int* deps;         // indices of dependency nodes (owned)
    int dep_count;
    int dep_cap;
    int depth;         // topological depth (0 = leaf, -1 = uncomputed)
} ImportGraphNode;

// Hashmap entry for path→index dedup
typedef struct {
    const char* path;
    int index;
} PathIndexEntry;

HASHMAP_DEFINE_STRKEY(path_index, PathIndexEntry, path)

// Resolve a module import path to a canonical absolute path.
// Returns malloc'd canonical path, or NULL for built-in/URI imports.
static char* resolve_module_path(const char* module_text, int module_len, const char* import_dir) {
    if (module_len <= 0) return NULL;

    // skip built-in modules
    if ((module_len == 4 && strncmp(module_text, "math", 4) == 0) ||
        (module_len == 2 && strncmp(module_text, "io", 2) == 0))
        return NULL;

    // skip bare URI imports
    if (module_text[0] == '\'') return NULL;

    StrBuf* buf = strbuf_new();

    if (module_text[0] == '.') {
        // relative import: .foo.bar → base_dir/foo/bar.ls
        const char* base_dir = import_dir ? import_dir : "./";
        strbuf_append_format(buf, "%s%.*s", base_dir, module_len - 1, module_text + 1);
        char* ch = buf->str + buf->length - (module_len - 1);
        while (*ch) { if (*ch == '.') *ch = '/'; ch++; }
        strbuf_append_str(buf, ".ls");
    } else {
        // absolute import: lambda.package.chart → g_lambda_home/package/chart.ls
        strbuf_append_format(buf, "./%.*s", module_len, module_text);
        char* ch = buf->str + 2;
        while (*ch) { if (*ch == '.') *ch = '/'; ch++; }
        strbuf_append_str(buf, ".ls");

        // replace first segment with g_lambda_home
        char* segment_end = strchr(buf->str + 2, '/');
        if (segment_end) {
            StrBuf* fixed = strbuf_new();
            const char* home = g_lambda_home;
            if (home[0] == '.' && home[1] == '/') home += 2;
            strbuf_append_str(fixed, "./");
            strbuf_append_str(fixed, home);
            strbuf_append_str(fixed, segment_end);
            strbuf_free(buf);
            buf = fixed;
        }
    }

    char* resolved = file_realpath(buf->str);
    strbuf_free(buf);
    return resolved;
}

// Add a dependency edge from parent_idx to dep_idx
static void add_dep(ImportGraphNode* nodes, int parent_idx, int dep_idx) {
    ImportGraphNode* parent = &nodes[parent_idx];
    if (parent->dep_count >= parent->dep_cap) {
        parent->dep_cap = parent->dep_cap ? parent->dep_cap * 2 : 4;
        parent->deps = (int*)mem_realloc(parent->deps, sizeof(int) * parent->dep_cap, MEM_CAT_SYSTEM);
    }
    parent->deps[parent->dep_count++] = dep_idx;
}

// Recursively discover all import dependencies starting from a source file.
// Adds new modules to the graph and records dependency edges.
static void discover_imports_recursive(
    TSParser* parser, int parent_idx,
    ImportGraphNode** nodes, int* count, int* capacity,
    struct hashmap* path_map)
{
    ImportGraphNode* parent = &(*nodes)[parent_idx];
    TSTree* tree = lambda_parse_source(parser, parent->source);
    if (!tree) return;

    // Save source and directory pointers BEFORE any recursive calls that might
    // realloc the nodes array and invalidate the parent pointer.  These are
    // separate heap allocations that remain valid until cleanup.
    const char* parent_source = parent->source;
    const char* parent_dir = parent->directory;

    TSNode root = ts_tree_root_node(tree);
    TSNode child = ts_node_named_child(root, 0);

    while (!ts_node_is_null(child)) {
        if (ts_node_symbol(child) == sym_import_module) {
            TSNode module_node = ts_node_child_by_field_id(child, field_module);
            if (!ts_node_is_null(module_node)) {
                uint32_t start = ts_node_start_byte(module_node);
                uint32_t end_byte = ts_node_end_byte(module_node);
                const char* module_text = parent_source + start;
                int module_len = (int)(end_byte - start);

                char* dep_path = resolve_module_path(module_text, module_len, parent_dir);
                if (dep_path) {
                    PathIndexEntry key = { .path = dep_path, .index = 0 };
                    const PathIndexEntry* existing = (const PathIndexEntry*)hashmap_get(path_map, &key);

                    int dep_idx;
                    if (existing) {
                        dep_idx = existing->index;
                        mem_free(dep_path);
                    } else {
                        // new module discovered
                        if (*count >= *capacity) {
                            *capacity *= 2;
                            *nodes = (ImportGraphNode*)mem_realloc(*nodes, sizeof(ImportGraphNode) * (*capacity), MEM_CAT_SYSTEM);
                        }
                        dep_idx = *count;
                        ImportGraphNode* n = &(*nodes)[dep_idx];
                        memset(n, 0, sizeof(ImportGraphNode));
                        n->path = dep_path;
                        n->source = read_text_file(dep_path);
                        n->depth = -1;

                        // extract directory
                        const char* last_slash = strrchr(dep_path, '/');
                        if (last_slash) {
                            int dir_len = (int)(last_slash - dep_path + 1);
                            n->directory = (char*)mem_alloc(dir_len + 1, MEM_CAT_SYSTEM);
                            memcpy(n->directory, dep_path, dir_len);
                            n->directory[dir_len] = '\0';
                        } else {
                            n->directory = mem_strdup("./", MEM_CAT_SYSTEM);
                        }

                        PathIndexEntry entry = { .path = n->path, .index = dep_idx };
                        hashmap_set(path_map, &entry);
                        (*count)++;

                        // recurse to discover transitive imports
                        if (n->source) {
                            discover_imports_recursive(parser, dep_idx,
                                nodes, count, capacity, path_map);
                        }
                    }
                    // record dependency: parent depends on dep_idx
                    // re-fetch parent pointer since realloc may have moved the array
                    add_dep(*nodes, parent_idx, dep_idx);
                }
            }
        }
        child = ts_node_next_named_sibling(child);
    }
    ts_tree_delete(tree);
}

// Compute topological depth for a node (0 = leaf, max(deps)+1 for others).
// Uses recursive DFS with memoization.
static int compute_depth(ImportGraphNode* nodes, int idx) {
    if (nodes[idx].depth >= 0) return nodes[idx].depth;
    nodes[idx].depth = 0;  // mark as computing (breaks cycles)
    int max_dep = -1;
    for (int i = 0; i < nodes[idx].dep_count; i++) {
        int d = compute_depth(nodes, nodes[idx].deps[i]);
        if (d > max_dep) max_dep = d;
    }
    nodes[idx].depth = max_dep + 1;
    return nodes[idx].depth;
}

// Worker argument for parallel compilation thread
typedef struct {
    Runtime* runtime;
    ImportGraphNode* node;
    bool success;
} CompileWorkerArg;

static void compile_module_worker(void* arg) {
    CompileWorkerArg* work = (CompileWorkerArg*)arg;

    // create thread-local parser
    tls_parser = lambda_parser();

    // compile the module via load_script (thread-safe version)
    // pass pre-read source to avoid redundant file I/O
    Script* result = load_script(work->runtime, work->node->path, work->node->source, true);
    work->success = (result != NULL && result->jit_context != NULL);

    // cleanup thread-local parser
    ts_parser_delete(tls_parser);
    tls_parser = NULL;
}

// Pre-compile all import dependencies in parallel before the main script starts.
// Discovers the full dependency graph, then compiles level by level (leaves first).
static void precompile_imports(Runtime* runtime, const char* main_script_path) {
    // read main script source for discovery
    char* canonical = file_realpath(main_script_path);
    const char* main_path = canonical ? canonical : main_script_path;
    const char* main_source = read_text_file(main_path);
    if (!main_source) {
        if (canonical) mem_free(canonical);
        return;
    }

    // extract main script directory
    char* main_dir = NULL;
    const char* last_slash = strrchr(main_path, '/');
    if (last_slash) {
        int dir_len = (int)(last_slash - main_path + 1);
        main_dir = (char*)mem_alloc(dir_len + 1, MEM_CAT_SYSTEM);
        memcpy(main_dir, main_path, dir_len);
        main_dir[dir_len] = '\0';
    } else {
        main_dir = mem_strdup("./", MEM_CAT_SYSTEM);
    }

    // initialize graph with main script as sentinel node (index 0, not compiled here)
    int capacity = 32;
    int count = 1;
    ImportGraphNode* nodes = (ImportGraphNode*)mem_calloc(capacity, sizeof(ImportGraphNode), MEM_CAT_SYSTEM);
    nodes[0].path = mem_strdup(main_path, MEM_CAT_SYSTEM);
    nodes[0].source = (char*)main_source;
    nodes[0].directory = main_dir;
    nodes[0].depth = -1;

    struct hashmap* path_map = path_index_new(64);
    PathIndexEntry main_entry = { .path = nodes[0].path, .index = 0 };
    hashmap_set(path_map, &main_entry);

    // discover all imports recursively using a temporary parser
    TSParser* discovery_parser = lambda_parser();
    discover_imports_recursive(discovery_parser, 0, &nodes, &count, &capacity, path_map);
    ts_parser_delete(discovery_parser);

    // check if there are enough modules to justify parallelism
    int import_count = count - 1;  // exclude main script (index 0)
    if (import_count >= 2) {
        log_info("parallel import: discovered %d modules, pre-compiling...", import_count);

        // ensure one-time init before spawning threads
        ensure_jit_imports_initialized();
        ensure_sys_func_maps_initialized();

        // compute topological depths
        int max_depth = 0;
        for (int i = 1; i < count; i++) {
            int d = compute_depth(nodes, i);
            if (d > max_depth) max_depth = d;
        }

        // compile level by level: depth 0 first (leaves), then 1, 2, ...
        // main script (index 0) has the highest depth — skip it
        long ncpus = sysconf(_SC_NPROCESSORS_ONLN);
        if (ncpus < 1) ncpus = 1;
        if (ncpus > 8) ncpus = 8;

        for (int level = 0; level <= max_depth; level++) {
            // collect modules at this depth
            int batch_count = 0;
            for (int i = 1; i < count; i++) {
                if (nodes[i].depth == level && nodes[i].source) batch_count++;
            }
            if (batch_count == 0) continue;

            // skip already-cached modules
            CompileWorkerArg* args = (CompileWorkerArg*)mem_calloc(batch_count, sizeof(CompileWorkerArg), MEM_CAT_SYSTEM);
            int actual = 0;
            pthread_mutex_lock(&scripts_mutex);
            for (int i = 1; i < count; i++) {
                if (nodes[i].depth != level || !nodes[i].source) continue;
                // check if already in cache
                bool cached = runtime_script_index_get_current(runtime, nodes[i].path) != NULL;
                if (!cached) {
                    args[actual].runtime = runtime;
                    args[actual].node = &nodes[i];
                    args[actual].success = false;
                    actual++;
                }
            }
            pthread_mutex_unlock(&scripts_mutex);

            if (actual == 0) {
                mem_free(args);
                continue;
            }

            bool level_profiling = is_profile_enabled();
            profile_time_t level_start, level_end;
            if (level_profiling) profile_get_time(&level_start);
            int threads_used = 1;
            if (actual == 1) {
                // single module — compile in-place without thread overhead
                tls_parser = lambda_parser();
                load_script(runtime, args[0].node->path, args[0].node->source, true);
                ts_parser_delete(tls_parser);
                tls_parser = NULL;
            } else {
                // parallel compilation via lib/thread_pool. 8MB worker stacks
                // accommodate the transpiler's deep recursion.
                threads_used = actual;
                ThreadPool* tp = tp_create_with_stack(actual, 8 * 1024 * 1024);
                if (tp) {
                    for (int i = 0; i < actual; i++) {
                        tp_submit(tp, compile_module_worker, &args[i]);
                    }
                    tp_wait_all(tp);
                    tp_destroy(tp);
                }
            }
            if (level_profiling) {
                profile_get_time(&level_end);
                ImportLevelProfile level_profile;
                memset(&level_profile, 0, sizeof(level_profile));
                level_profile.level = level;
                level_profile.modules = batch_count;
                level_profile.jobs = actual;
                level_profile.threads = threads_used;
                level_profile.cpu_cap = (int)ncpus;
                level_profile.elapsed_ms = elapsed_ms_val(level_start, level_end);
                profile_record_import_level(&level_profile);
            }
            mem_free(args);
        }

        log_info("parallel import: pre-compilation complete");

        // compiled MIR imports embed module indexes, so post-compile
        // renumbering corrupts their mN symbol references. Import-cone traversal
        // now supplies dependency order without mutating these stable indexes.
    }

    // cleanup graph
    hashmap_free(path_map);
    for (int i = 0; i < count; i++) {
        // don't free source for index 0 — that was read_text_file'd and will be freed
        // when load_script reads it again (or it might be the same pointer)
        if (i > 0) mem_free(nodes[i].source);
        mem_free(nodes[i].path);
        mem_free(nodes[i].directory);
        mem_free(nodes[i].deps);
    }
    // index 0's source was malloc'd by read_text_file — free it
    mem_free((void*)main_source);
    // main_dir is nodes[0].directory, already freed above
    mem_free(nodes);
    if (canonical) mem_free(canonical);
}

#endif  // !_WIN32

Script* load_script(Runtime *runtime, const char* script_path, const char* source, bool is_import) {
    log_info("Loading script: %s (is_import=%d)", script_path, is_import);

#ifndef _WIN32
    // For the main script, pre-compile all imports in parallel.
    // Only trigger when: not an import, no source provided (file-based), MIR Direct mode,
    // and not already in a worker thread (tls_parser == NULL).
    if (!is_import && !source && runtime->use_mir_direct && !tls_parser) {
        precompile_imports(runtime, script_path);
    }
#endif

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
    transpiler.parser = tls_parser ? tls_parser : runtime->parser;
    transpiler.runtime = runtime;
    transpiler.error_count = 0;
    transpiler.max_errors = runtime->max_errors > 0 ? runtime->max_errors : 10;  // use runtime setting or default 10
    transpiler.errors = arraylist_new(8);  // initialize error list for structured errors

    transpile_script(&transpiler, new_script, script_path);
    new_script->is_loading = false;  // loading complete

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

    // check for compilation failure
    if (!new_script->jit_context) {
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
        module_register(new_script->reference, "lambda", ns, new_script->jit_context);
    }

    log_debug("loaded script main func: %s, %p", script_path, new_script->main_func);
    return new_script;
}

Script* load_script_mir_direct(Runtime *runtime, const char* script_path,
                               const char* source, bool is_import) {
    if (!runtime) return NULL;
    // Cross-language loaders do not enter run_script_mir(), so select MIR Direct
    // here or imported Lambda modules incorrectly fall through to the absent C2MIR path.
    bool was_mir_direct = runtime->use_mir_direct;
    runtime->use_mir_direct = true;
    Script* script = load_script(runtime, script_path, source, is_import);
    runtime->use_mir_direct = was_mir_direct;
    return script;
}

void runner_init(Runtime *runtime, Runner* runner) {
    memset(runner, 0, sizeof(Runner));
    runner->runtime = runtime;
}

#include "../lib/url.h"
#include "validator/validator.hpp"
#include "runtime/lambda-stack.h"

void runner_setup_context(Runner* runner) {
    log_debug("runner setup exec context");

    // Initialize stack overflow protection (once per thread)
    lambda_stack_init();

    // Store stack_limit in context for fast access from JIT-compiled code
    runner->context.stack_limit = _lambda_stack_limit;
    if (!lambda_side_stack_bind(&runner->context)) {
        log_error("runner side-stack: failed to initialize execution regions");
    }

    runner->context.pool = runner->script->pool;
    runner->context.type_list = runner->script->type_list;

    runner->context.type_info = type_info;
    runner->context.consts = runner->script->const_list->data;
    runner->context.result = ItemNull;  // exec result
    runner->context.cwd = get_current_dir();  // proper URL object for current directory
    // initialize decimal context (use shared fixed-precision context for runtime)
    runner->context.decimal_ctx = decimal_fixed_context();
    runner->context.context_alloc = heap_alloc;
    // init AST validator
    runner->context.validator = schema_validator_create(runner->context.pool);

    // Initialize error handling and stack trace support
    // Use debug_info from script (built after MIR compilation for address → function mapping)
    runner->context.debug_info = runner->script->debug_info;
    runner->context.current_file = runner->script->reference;  // source file for error reporting
    runner->context.last_error = NULL;

    input_context = context = &runner->context;

    // Phase 5: propagate ui_mode and result_arena from Runtime to context
    Runtime* ui_rt = runner->runtime;
    if (ui_rt && ui_rt->ui_mode && ui_rt->result_arena) {
        context->ui_mode = true;
        context->arena = ui_rt->result_arena;
    }

    // Reuse or create the GC heap and name_pool from the Runtime.
    // These persist across multiple evaluations on the same Runtime.
    Runtime* rt = runner->runtime;
    if (rt && rt->heap) {
        // Reuse retained heap and name_pool from a previous evaluation
        log_debug("runner_setup_context: reusing retained heap from Runtime");
        context->heap = rt->heap;
        context->name_pool = rt->name_pool;
        context->pool = context->heap->pool;
    } else {
        // First evaluation on this Runtime — create fresh resources
        context->name_pool = name_pool_create(context->pool, nullptr);
        if (!context->name_pool) {
            log_error("Failed to create runtime name_pool");
        }
        heap_init();
        context->pool = context->heap->pool;
        // Store on Runtime for reuse
        if (rt) {
            rt->heap = context->heap;
            rt->name_pool = context->name_pool;
        }
    }
    path_register_pool_provider(runner_path_pool_provider);

    if (rt && rt->scheduler) {
        context->scheduler = rt->scheduler;
    } else {
        context->scheduler = lambda_scheduler_create(LAMBDA_MAILBOX_DEFAULT_CAPACITY);
        if (rt) rt->scheduler = context->scheduler;
    }
    if (rt && rt->js_bootstrap_context) {
        // The Lambda runner now owns every heap resource created while its JS
        // imports were compiled; only the standalone bootstrap shell remains.
        mem_free(rt->js_bootstrap_context);
        rt->js_bootstrap_context = NULL;
    }

    // Initialize template registry for view/edit template dispatch
    if (!g_template_registry) {
        g_template_registry = template_registry_new();
    }
}

// Helper function to recursively resolve all sys:// paths in an Item tree
// This must be called before deep_copy while the execution context is still valid
// Only handles List/Array since those are the common containers for script results
extern "C" Item path_resolve_for_iteration(Path* path);

// Forward declare BSS root registration from mir.c
extern "C" void reset_and_register_bss_gc_roots(void* mir_ctx);

void resolve_sys_paths_recursive(Item item) {
    TypeId type_id = get_type_id(item);
    if (type_id == LMD_TYPE_PATH) {
        Path* path = item.path;
        if (path && path_get_scheme(path) == PATH_SCHEME_SYS && path->result == 0) {
            path_resolve_for_iteration(path);
        }
    } else if (type_id == LMD_TYPE_ARRAY) {
        List* list = item.array;
        for (int64_t i = 0; i < list->length; i++) {
            resolve_sys_paths_recursive(list->items[i]);
        }
    }
    // Note: Maps and Elements could also contain paths, but for script results
    // we mainly need to handle List/Array which collect top-level expressions
    // Map/Element traversal was causing segfaults in some edge cases (csv_test)
    // TODO: Investigate why map->data access crashes for some maps
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

    // Register BSS global variables as GC roots (module-level let bindings)
    if (runner->script->jit_context) {
        reset_and_register_bss_gc_roots((void*)runner->script->jit_context);
    }

    // set the run_main flag in the execution context
    runner->context.run_main = run_main;
    log_debug("Set context run_main = %s", run_main ? "true" : "false");

    // Phase 2: Set recovery point for signal-based stack overflow handling.
    // If a stack overflow occurs during execution, the signal handler will
    // siglongjmp back here. No per-call overhead — detection is OS-level.
    Item result;
    LambdaRecoveryCheckpoint recovery_checkpoint =
        lambda_recovery_checkpoint_capture(context);
#if defined(__APPLE__) || defined(__linux__)
    if (sigsetjmp(_lambda_recovery_point, 1)) {
#elif defined(_WIN32)
    if (setjmp(_lambda_recovery_point)) {
#else
    if (0) {
#endif
        // Stack overflow was caught — we land here after siglongjmp
        _lambda_recovery_armed = 0;   // recovery consumed; disarm
        log_error("exec: recovered from stack overflow via signal handler");
        _lambda_stack_overflow_flag = false;
        lambda_recovery_checkpoint_restore(&recovery_checkpoint);
        lambda_stack_overflow_error("<signal>");
        result = context->result = ItemError;
    } else {
        // Normal execution path — zero per-call overhead
        _lambda_recovery_armed = 1;    // arm only for the duration of user code
        log_debug("exec main func");
        result = context->result = runner->script->main_func(context);
        _lambda_recovery_armed = 0;
        lambda_recovery_checkpoint_disarm(&recovery_checkpoint);
        log_debug("after main func, result type_id=%d", get_type_id(result));
    }
    if ((!runner->runtime || !runner->runtime->no_task_drain) && context->scheduler) {
        lambda_scheduler_drain(context->scheduler);
    }
    if (context->heap) {
        context->heap->result_root = context->result.item;
    }

    preserve_context_last_error(context, result);

    // Create output Input with its own pool (independent from Script's pool)
    // This allows safe cleanup of the execution context and heap
    log_debug("Creating output Input with independent pool");
    Pool* output_pool = mem_pool_create(NULL, MEM_ROLE_AST, "script.result");
    Input* output = Input::create(output_pool, nullptr);
    if (!output) {
        log_error("Failed to create output Input");
        if (output_pool) pool_destroy(output_pool);
        if (runner->context.cwd) {
            url_destroy(runner->context.cwd);
            runner->context.cwd = NULL;
        }
        return nullptr;
    }

    // Resolve all sys:// paths in result (while context is still valid)
    resolve_sys_paths_recursive(result);
    if (runner->context.cwd) {
        url_destroy(runner->context.cwd);
        runner->context.cwd = NULL;
    }

    // Return result directly on the GC heap — no deep_copy needed.
    // With GC-managed memory the heap is retained across the session;
    // the caller is responsible for calling runtime_cleanup() when done.
    output->root = result;

    log_debug("Script execution completed, returning output Input");
    return output;
}

Input* run_script(Runtime *runtime, const char* source, char* script_path, bool transpile_only) {
    (void)runtime;
    (void)source;
    (void)script_path;
    (void)transpile_only;
    // C2MIR relies on native-stack discovery. Re-enable only after its emitter
    // publishes canonical side-stack roots at every MAY_GC boundary.
    log_error("C2MIR execution is disabled: it has no precise GC root contract");
    return nullptr;
}

Input* run_script_at(Runtime *runtime, char* script_path, bool transpile_only) {
    return run_script(runtime, NULL, script_path, transpile_only);
}

// Extended function that supports setting run_main context and returns Input*
Input* run_script_with_run_main(Runtime *runtime, char* script_path, bool transpile_only, bool run_main) {
    (void)runtime;
    (void)script_path;
    (void)transpile_only;
    (void)run_main;
    // C2MIR relies on native-stack discovery. Re-enable only after its emitter
    // publishes canonical side-stack roots at every MAY_GC boundary.
    log_error("C2MIR execution is disabled: it has no precise GC root contract");
    return nullptr;
}

// Installs runtime_cleanup into the DOM layer's hook so dom_document_destroy()
// can clean up a document's reactive lambda_runtime without dom_element.cpp
// hard-linking runner.cpp (keeps input/DOM unit tests free of the runtime).
extern "C" void dom_set_runtime_cleanup_hook(void (*fn)(Runtime*));
extern "C" void jube_register_builtin_modules(void);

void runtime_init(Runtime* runtime) {
    memset(runtime, 0, sizeof(Runtime));
    runtime->parser = lambda_parser();
    runtime->scripts = arraylist_new(16);
    runtime->script_index = script_index_new(64);
    runtime->max_errors = 10;  // default error threshold
    runtime->optimize_level = 2;  // default MIR optimization level (0=debug, 2=release)
    runtime->transpile_dir = NULL;  // default: no file output; set via --transpile-dir
    runtime->dry_run = false;  // default: real IO
    const char* disable_mir_cache = shell_getenv("LAMBDA_DISABLE_MIR_CACHE");
    runtime->mir_cache_disabled = (LAMBDA_MIR_CACHE_DEFAULT == 0) ||
        (disable_mir_cache &&
         (strcmp(disable_mir_cache, "1") == 0 || strcmp(disable_mir_cache, "true") == 0));
    // debug and release builds enable retained MIR imports by default; this opt-out is for timing and emergency bisecting.
    if (runtime->mir_cache_disabled) {
        log_info("mir cache index: retained module cache disabled by build default or LAMBDA_DISABLE_MIR_CACHE");
    }
    module_registry_init();
    jube_register_builtin_modules();
    dom_set_runtime_cleanup_hook(runtime_cleanup);  // wire DOM-layer cleanup hook
}

void runtime_register_script(Runtime* runtime, Script* script) {
    if (!runtime || !runtime->scripts || !script) return;
    arraylist_append(runtime->scripts, script);
    script->index = runtime->scripts->length - 1;
    runtime_script_index_put(runtime, script);
}

void runtime_free_script(Runtime* runtime, Script* script, bool remove_index) {
    if (!script) return;
    if (remove_index && script->reference) {
        runtime_script_index_delete_script(runtime, script);
    }
    if (script->reference) mem_free((void*)script->reference);
    if (script->source) mem_free((void*)script->source);
    if (script->directory) mem_free((void*)script->directory);
    if (script->syntax_tree) ts_tree_delete(script->syntax_tree);
    if (script->pool) pool_destroy(script->pool);
    if (script->type_list) arraylist_free(script->type_list);
    if (script->direct_imports) arraylist_free(script->direct_imports);
    if (script->jit_context) jit_cleanup(script->jit_context);
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
    if (runtime->heap) {
        EvalContext tmp_ctx;
        memset(&tmp_ctx, 0, sizeof(tmp_ctx));
        tmp_ctx.heap = runtime->heap;
        tmp_ctx.result = ItemNull;
        tmp_ctx.scheduler = runtime->scheduler;
        context = &tmp_ctx;

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

        // Batch heap replacement invalidates module-owned callback Items just
        // as final runtime teardown does; release those roots before the GC.
        jube_notify_heap_cleanup(runtime->heap);

        // Release name_pool BEFORE heap_destroy: the NamePool struct is
        // pool_calloc'd from the heap's pool, and pool_destroy bulk-frees
        // all pool memory.  We must free the hashmap inside while accessible.
        if (runtime->name_pool) {
            name_pool_release(runtime->name_pool);
            runtime->name_pool = NULL;
        }
        if (runtime->type_list) {
            arraylist_free(runtime->type_list);
            runtime->type_list = NULL;
        }

        heap_destroy();
        runtime->heap = NULL;
        context = NULL;
    }
    if (runtime->js_bootstrap_context) {
        mem_free(runtime->js_bootstrap_context);
        runtime->js_bootstrap_context = NULL;
    }
}

void runtime_cleanup(Runtime* runtime) {
    // Dump profiling data if enabled (before freeing anything)
    profile_dump_to_file();

    js_canvas_cleanup();
    module_registry_cleanup();
    template_registry_destroy(g_template_registry);
    js_eval_preamble_cache_reset();
    js_fetch_reset();

    bool event_loop_cleaned = false;

    // Destroy retained execution state (heap and name_pool)
    if (runtime->heap) {
        // Set the thread-local context so heap_destroy can access context->heap
        // (heap_destroy uses the context global)
        EvalContext tmp_ctx;
        memset(&tmp_ctx, 0, sizeof(tmp_ctx));
        tmp_ctx.heap = runtime->heap;
        tmp_ctx.result = ItemNull;
        context = &tmp_ctx;

        if (runtime->scheduler) {
            tmp_ctx.scheduler = runtime->scheduler;
            lambda_scheduler_destroy(runtime->scheduler);
            runtime->scheduler = NULL;
        }

        js_event_loop_shutdown();
        lambda_uv_cleanup();
        event_loop_cleaned = true;

        js_dom_shutdown();
        if (runtime->dom_doc) {
            free_document((DomDocument*)runtime->dom_doc);
            runtime->dom_doc = NULL;
        }

        // Jube modules may cache heap-owned callbacks across repeated page
        // interactions; release those roots before this heap disappears.
        jube_notify_heap_cleanup(runtime->heap);

        print_heap_entries();
        check_memory_leak();

        // Release name_pool BEFORE heap_destroy: the NamePool struct is
        // pool_calloc'd from the heap's pool, and pool_destroy bulk-frees
        // all pool memory.  We must free the hashmap inside while accessible.
        if (runtime->name_pool) {
            name_pool_release(runtime->name_pool);
            runtime->name_pool = NULL;
        }
        if (runtime->type_list) {
            arraylist_free(runtime->type_list);
            runtime->type_list = NULL;
        }

        heap_destroy();
        runtime->heap = NULL;
        context = NULL;
    } else {
        js_dom_shutdown();
        if (runtime->dom_doc) {
            free_document((DomDocument*)runtime->dom_doc);
            runtime->dom_doc = NULL;
        }
    }
    if (!event_loop_cleaned) {
        js_event_loop_shutdown();
        lambda_uv_cleanup();
    }
    if (runtime->js_bootstrap_context) {
        mem_free(runtime->js_bootstrap_context);
        runtime->js_bootstrap_context = NULL;
    }
    lambda_stack_cleanup();
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
