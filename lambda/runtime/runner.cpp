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
#include "doc_context.hpp"
#include "write_set.hpp"
#include "ast_build.hpp"
#include "module_ast_prebuild.hpp"
#include "../../lib/hashmap_typed.hpp"
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
#include "../jube/jube_interface.h"
#include "../js/js_runtime.h"
#include "../js/js_runtime_state.hpp"
#include "../js/js_transpiler.hpp"
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
#include "../dom/dom.h"

extern "C" Item js_get_key_default(Item object, Item key);
struct DomDocument;
extern void free_document(DomDocument* doc);

#ifndef LAMBDA_MIR_CACHE_DEFAULT
#define LAMBDA_MIR_CACHE_DEFAULT 1
#endif

static __thread LambdaCompilerTiming g_last_lambda_compiler_timing;
static int g_compiler_timing_enabled = -1;

typedef struct LambdaAstPrebuildDiscoverState {
    const char* source;
    size_t source_length;
    ArrayList* specifiers;
    LambdaParseValue next_value;
    bool failed;
} LambdaAstPrebuildDiscoverState;

static LambdaParseValue lambda_ast_prebuild_discover_reduce(void* opaque,
        const LambdaParseReduction* reduction) {
    LambdaAstPrebuildDiscoverState* state =
        (LambdaAstPrebuildDiscoverState*)opaque;
    if (!state || !reduction) return 0;
    if (reduction->kind == LAMBDA_REDUCE_DECLARATION &&
            reduction->form == LAMBDA_REDUCTION_FORM_IMPORT) {
        SourceSpan span = reduction->secondary_token.span;
        if (span.end_byte < span.start_byte || span.end_byte > state->source_length) {
            state->failed = true;
            return 0;
        }
        char* specifier = mem_dup_n(state->source + span.start_byte,
            span.end_byte - span.start_byte, MEM_CAT_SYSTEM);
        if (!specifier || !arraylist_append(state->specifiers, specifier)) {
            mem_free(specifier);
            state->failed = true;
            return 0;
        }
    }
    state->next_value++;
    return state->next_value;
}

static ArrayList* lambda_ast_prebuild_discover_imports(void* opaque,
        const char* source, size_t source_length) {
    (void)opaque;
    if (!source) return NULL;
    ArrayList* specifiers = arraylist_new(4);
    if (!specifiers) return NULL;
    LambdaAstPrebuildDiscoverState state = {source, source_length, specifiers,
        0, false};
    LambdaParseSink sink = {lambda_ast_prebuild_discover_reduce};
    LambdaParseError error = {};
    if (lambda_rd_parse_source(source, source_length, &sink, &state, NULL,
            &error) != LAMBDA_PARSE_OK || state.failed) {
        for (int index = 0; index < specifiers->length; index++) {
            mem_free(specifiers->data[index]);
        }
        arraylist_free(specifiers);
        return NULL;
    }
    return specifiers;
}

static char* lambda_ast_prebuild_importer_directory(const char* path) {
    if (!path) return NULL;
    const char* slash = strrchr(path, '/');
#ifdef _WIN32
    const char* backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
#endif
    return slash ? mem_dup_n(path, (size_t)(slash - path + 1), MEM_CAT_SYSTEM)
        : mem_strdup("./", MEM_CAT_SYSTEM);
}

static bool lambda_ast_prebuild_resolve_import(void* opaque,
        const char* importer_path, const char* specifier,
        ModuleAstResolvedImport* out) {
    (void)opaque;
    if (!out || !importer_path || !specifier || !specifier[0] ||
            specifier[0] == '\'') return false;
    char* directory = lambda_ast_prebuild_importer_directory(importer_path);
    StrView module = strview_from_cstr(specifier);
    char* path = lambda_resolve_import_module_path(directory, module);
    mem_free(directory);
    if (!path || !file_exists(path)) {
        mem_free(path);
        return false;
    }
    out->path = path;
    out->language = MODULE_AST_LANGUAGE_LAMBDA;
    return true;
}

static bool lambda_ast_prebuild_build_module(void* opaque, const char* path) {
    (void)opaque;
    Runtime worker = {};
    runtime_init(&worker);
    worker.ast_prebuild_only = true;
    Script* script = load_script(&worker, path, NULL, true);
    bool built = script && script->ast_root && !script->jit_context &&
        (script->cache_owned_template || script->cache_template);
    runtime_cleanup_ast_prebuild_worker(&worker);
    return built;
}

static const ModuleAstPrebuildProfile* lambda_ast_prebuild_profile(void) {
    static const ModuleAstPrebuildProfile profile = {
        "lambda", MODULE_AST_LANGUAGE_LAMBDA,
        lambda_ast_prebuild_discover_imports, lambda_ast_prebuild_resolve_import,
        lambda_ast_prebuild_build_module, NULL,
    };
    return &profile;
}

static bool lambda_ast_prebuild_imports(const char* path) {
    ModuleAstPrebuildProfiles profiles = {};
    profiles.profiles[MODULE_AST_LANGUAGE_LAMBDA] = lambda_ast_prebuild_profile();
    ModuleAstPrebuildStats stats = {};
    return module_ast_prebuild_imports(&profiles, MODULE_AST_LANGUAGE_LAMBDA,
        path, NULL, 0, &stats);
}

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
// (package trees, input/).
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
    char* out = mem_join3(g_lambda_home, home_len, "/", 1, rel, rel_len, MEM_CAT_SYSTEM);
    if (!out) return NULL;
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
    double build_reducer_ms;
    double inline_analysis_ms;
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

extern "C" int lambda_compiler_timing_collecting(void) {
    return lambda_compiler_timing_enabled() || is_profile_enabled();
}

extern "C" void lambda_compiler_timing_add_inline_analysis_us(
        uint64_t elapsed_us) {
    if (!lambda_compiler_timing_collecting()) return;
    g_last_lambda_compiler_timing.analysis_us += elapsed_us;
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

typedef enum LambdaOwnTimingPhase {
    LAMBDA_OWN_TIMING_NONE,
    LAMBDA_OWN_TIMING_PARSE,
    LAMBDA_OWN_TIMING_BUILD,
    LAMBDA_OWN_TIMING_BIND,
    LAMBDA_OWN_TIMING_VALIDATE,
    LAMBDA_OWN_TIMING_INDEX,
    LAMBDA_OWN_TIMING_PLAN,
    LAMBDA_OWN_TIMING_MIR,
    LAMBDA_OWN_TIMING_COUNT,
} LambdaOwnTimingPhase;

typedef struct LambdaOwnTimingFrame {
    struct LambdaOwnTimingFrame* parent;
    profile_time_t started;
    double nested_ms[LAMBDA_OWN_TIMING_COUNT];
    LambdaOwnTimingPhase active;
} LambdaOwnTimingFrame;

static thread_local LambdaOwnTimingFrame* g_lambda_own_timing_frame = NULL;

static void lambda_own_timing_enter(LambdaOwnTimingFrame* frame) {
    if (!frame) return;
    memset(frame, 0, sizeof(*frame));
    profile_get_time(&frame->started);
    frame->parent = g_lambda_own_timing_frame;
    g_lambda_own_timing_frame = frame;
}

static void lambda_own_timing_set_phase(LambdaOwnTimingFrame* frame,
        LambdaOwnTimingPhase phase) {
    if (frame) frame->active = phase;
}

static double lambda_own_timing_elapsed(LambdaOwnTimingFrame* frame,
        LambdaOwnTimingPhase phase, profile_time_t start, profile_time_t end) {
    double elapsed = elapsed_ms_val(start, end);
    double nested = frame && phase > LAMBDA_OWN_TIMING_NONE &&
        phase < LAMBDA_OWN_TIMING_COUNT ? frame->nested_ms[phase] : 0;
    return elapsed > nested ? elapsed - nested : 0;
}

static void lambda_own_timing_leave(LambdaOwnTimingFrame* frame) {
    if (!frame || g_lambda_own_timing_frame != frame) return;
    profile_time_t ended;
    profile_get_time(&ended);
    g_lambda_own_timing_frame = frame->parent;
    if (frame->parent && frame->parent->active > LAMBDA_OWN_TIMING_NONE &&
            frame->parent->active < LAMBDA_OWN_TIMING_COUNT) {
        frame->parent->nested_ms[frame->parent->active] +=
            elapsed_ms_val(frame->started, ended);
    }
}

// An import owned by a prebuild worker is not a nested transpile on this
// thread, but its future wait is still child compilation time rather than the
// importer's own reducer work. Account for it on the active parent phase.
static void lambda_own_timing_subtract_active(double elapsed_ms) {
    LambdaOwnTimingFrame* frame = g_lambda_own_timing_frame;
    if (!frame || elapsed_ms <= 0 || frame->active <= LAMBDA_OWN_TIMING_NONE ||
            frame->active >= LAMBDA_OWN_TIMING_COUNT) return;
    frame->nested_ms[frame->active] += elapsed_ms;
}

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
    // TSV format v3: build own-time detail separates replay from analyses;
    // `plan`, `interp_exec` and `peak_rss_mb` remain the T0 report columns.
    // turnaround/memory report; JIT-tier rows carry 0 in the T0 columns.
    fprintf(f, "# Phase-Level Profile (LAMBDA_PROFILE=1) format=3\n");
    fprintf(f, "# script | parse | ast | build_reducer | inline_analysis | plan | transpile | jit_init | mir_gen | interp_exec | total | peak_rss_mb | code_len | worker | thread_id\n");
    for (int i = 0; i < profile_count; i++) {
        PhaseProfile* p = &profile_data[i];
        double total = p->parse_ms + p->ast_ms + p->plan_ms + p->transpile_ms +
                       p->jit_init_ms + p->mir_gen_ms + p->interp_exec_ms;
        fprintf(f, "%s\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%d\t%d\t%lu\n",
                p->script_path, p->parse_ms, p->ast_ms, p->build_reducer_ms,
                p->inline_analysis_ms, p->plan_ms, p->transpile_ms,
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

typedef struct RuntimeLoadedScriptEntry {
    const char* path;
    Script* script;
} RuntimeLoadedScriptEntry;

typedef TypedHashMap<RuntimeLoadedScriptEntry,
    HashMapCStrMemberKeyOps<RuntimeLoadedScriptEntry, &RuntimeLoadedScriptEntry::path>>
    RuntimeLoadedScriptIndex;

#ifndef _WIN32
// Mutex for thread-safe access to runtime->scripts during parallel compilation
static pthread_mutex_t scripts_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static Script* runtime_loaded_script_get(Runtime* runtime, const char* path) {
    if (!runtime || !runtime->loaded_script_index || !path) return NULL;
    RuntimeLoadedScriptEntry probe = { .path = path, .script = NULL };
    const RuntimeLoadedScriptEntry* found = RuntimeLoadedScriptIndex::get(
        runtime->loaded_script_index, probe);
    return found ? found->script : NULL;
}

static void runtime_loaded_script_put(Runtime* runtime, Script* script) {
    if (!runtime || !script || !script->reference) return;
    if (!runtime->loaded_script_index) {
        runtime->loaded_script_index = RuntimeLoadedScriptIndex::create(64);
    }
    RuntimeLoadedScriptEntry entry = { .path = script->reference, .script = script };
    RuntimeLoadedScriptIndex::set(runtime->loaded_script_index, entry);
    if (RuntimeLoadedScriptIndex::oom(runtime->loaded_script_index)) {
        log_error("runtime-script-registry: failed to index %s", script->reference);
    }
}

static void runtime_loaded_script_delete(Runtime* runtime, const char* path) {
    if (!runtime || !runtime->loaded_script_index || !path) return;
    RuntimeLoadedScriptEntry probe = { .path = path, .script = NULL };
    RuntimeLoadedScriptIndex::erase(runtime->loaded_script_index, probe);
}

static void runtime_loaded_script_delete_instance(Runtime* runtime, Script* script) {
    if (!runtime || !runtime->loaded_script_index || !script || !script->reference) return;
    if (runtime_loaded_script_get(runtime, script->reference) != script) return;
    runtime_loaded_script_delete(runtime, script->reference);
}

uint32_t script_compilation_unit_id(const Script* script) {
    if (!script) return 0;
    // Uncached and synthetic scripts have no persistent identity, so their
    // already-dense slab address is also their local link identity.
    return script->cache_compilation_unit_id != 0
        ? script->cache_compilation_unit_id : script->module_state_id;
}

static void runtime_module_unit_index_put(Runtime* runtime, const Script* script) {
    uint32_t unit_id = script ? script->cache_compilation_unit_id : 0;
    if (!runtime || !script || unit_id == 0) return;
    (void)runtime_module_state_bind_unit(runtime, unit_id,
        script->module_state_id);
}

static void runtime_module_unit_index_delete_script(Runtime* runtime,
        const Script* script) {
    uint32_t unit_id = script ? script->cache_compilation_unit_id : 0;
    if (!runtime || !script || unit_id == 0) return;
    runtime_module_state_unbind_unit(runtime, unit_id, script->module_state_id);
}

static int64_t script_file_mtime_nsec(const struct stat* stat_value) {
    if (!stat_value) return 0;
#if defined(__APPLE__)
    return stat_value->st_mtimespec.tv_nsec;
#elif defined(_WIN32)
    return 0;
#else
    return stat_value->st_mtim.tv_nsec;
#endif
}

static void capture_script_file_stat(Script* script, const char* path, bool file_backed) {
    if (!script || !path || !file_backed) return;
    struct stat st;
    if (stat(path, &st) == 0) {
        script->src_mtime = st.st_mtime;
        script->src_mtime_nsec = script_file_mtime_nsec(&st);
        script->src_size = st.st_size;
    }
}

static bool script_file_stat_changed(Script* script, const char* path) {
    if (!script || !path) return false;
    if (script->src_mtime == 0 && script->src_mtime_nsec == 0 &&
            script->src_size == 0) return false;
    struct stat st;
    if (stat(path, &st) != 0) return true;
    return st.st_mtime != script->src_mtime ||
        script_file_mtime_nsec(&st) != script->src_mtime_nsec ||
        st.st_size != script->src_size;
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

static void retire_runtime_script(Runtime* runtime, Script* script, ArrayList* retired, const char* reason) {
    if (!runtime || !script || script->is_retired) return;
    script->is_retired = true;
    runtime_loaded_script_delete_instance(runtime, script);
    arraylist_append(retired, script);
    runtime->script_load_invalidations++;
    log_info("runtime-script-registry: retired path=%s index=%d reason=%s",
             script->reference ? script->reference : "<unknown>", script->index,
             reason ? reason : "changed dependency");
}

static void retire_script_cone(Runtime* runtime, Script* root) {
    if (!runtime || !runtime->scripts || !root) return;
    ArrayList* retired = arraylist_new(8);
    retire_runtime_script(runtime, root, retired, "source changed");

    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < runtime->scripts->length; i++) {
            Script* candidate = (Script*)runtime->scripts->data[i];
            if (!candidate || candidate->is_retired) continue;
            if (script_imports_retired_dep(candidate, retired)) {
                retire_runtime_script(runtime, candidate, retired, "dependent of changed module");
                changed = true;
            }
        }
    }
    arraylist_free(retired);
}

static Script* runtime_loaded_script_get_current(Runtime* runtime, const char* path) {
    Script* script = runtime_loaded_script_get(runtime, path);
    if (!script) return NULL;
    if (script_file_stat_changed(script, path)) {
        log_info("runtime-script-registry: stale path=%s index=%d", path, script->index);
        retire_script_cone(runtime, script);
        return NULL;
    }
    return script;
}

static bool script_source_matches(Script* script, const char* source,
        size_t source_length) {
    if (!script || !script->source || !source) return false;
    size_t script_length = strlen(script->source);
    return script_length == source_length &&
        (source_length == 0 || memcmp(script->source, source, source_length) == 0);
}

class LambdaScriptSourceLease {
public:
    LambdaScriptSourceLease(InputScriptLease* lease, InputCacheScope* scope)
        : lease_(lease), scope_(scope) {}
    LambdaScriptSourceLease(const LambdaScriptSourceLease&) = delete;
    LambdaScriptSourceLease& operator=(const LambdaScriptSourceLease&) = delete;
    ~LambdaScriptSourceLease() { input_script_cache_close_scope(scope_); }

    InputScriptLease* get() const { return lease_; }
    const char* source() const {
        ScriptInput* input = input_script_lease_input(lease_);
        return input_script_source(input);
    }
    size_t source_length() const {
        ScriptInput* input = input_script_lease_input(lease_);
        return input_script_source_length(input);
    }
    InputCacheScope* scope() const { return scope_; }
    InputCacheScope* release_scope() {
        InputCacheScope* scope = scope_;
        scope_ = NULL;
        lease_ = NULL;
        return scope;
    }

private:
    InputScriptLease* lease_;
    InputCacheScope* scope_;
};

class LambdaScriptBuildClaim {
public:
    LambdaScriptBuildClaim(InputScriptLease* lease, InputScriptBuildKind kind,
            bool enabled)
        : lease_(lease), kind_(kind), claim_(enabled
            ? input_script_cache_claim_build(lease, kind)
            : INPUT_SCRIPT_BUILD_BYPASS), completed_(false) {}
    LambdaScriptBuildClaim(const LambdaScriptBuildClaim&) = delete;
    LambdaScriptBuildClaim& operator=(const LambdaScriptBuildClaim&) = delete;
    ~LambdaScriptBuildClaim() {
        if (claim_ == INPUT_SCRIPT_BUILD_OWNER && !completed_) {
            input_script_cache_complete_build(lease_, kind_, false, false);
        }
    }

    bool is_owner() const { return claim_ == INPUT_SCRIPT_BUILD_OWNER; }
    bool is_ready() const { return claim_ == INPUT_SCRIPT_BUILD_READY; }
    bool is_poisoned() const { return claim_ == INPUT_SCRIPT_BUILD_POISONED; }
    void complete(bool published) {
        if (!is_owner() || completed_) return;
        input_script_cache_complete_build(lease_, kind_, published, false);
        completed_ = true;
    }

private:
    InputScriptLease* lease_;
    InputScriptBuildKind kind_;
    InputScriptBuildClaim claim_;
    bool completed_;
};

static void lambda_script_cache_destroy_artifact(void* value) {
    Script* script = (Script*)value;
    runtime_destroy_cached_script_template(script);
}

static size_t lambda_script_cache_artifact_bytes(const void* value) {
    const Script* script = (const Script*)value;
    if (!script) return 0;
    // The source itself is accounted by ScriptInput. Pools and JIT pages are
    // opaque to the common cache, so report the owned descriptor exactly and
    // leave allocator-level attribution to their named memory contexts.
    return sizeof(Script);
}

static bool lambda_ast_template_is_reusable_shallow(const Script* script) {
    if (!script || !script->ast_root || script->jit_context ||
            script->cache_cross_lang_tainted) {
        return false;
    }
    return script->ast_frontend_only ||
        (script->interp_supported && script->interp_planned);
}

// An executing Runtime sees a shell around a process-cache template.  Cache
// graph operations must follow the immutable owner, never retain or validate
// the shell's EvalContext-local edges (D8.5.1v7).
static const Script* lambda_cache_template_owner(const Script* script) {
    if (!script) return NULL;
    return script->cache_owned_template ? script : script->cache_template;
}

static const ArrayList* lambda_cache_direct_imports(const Script* script) {
    if (!script) return NULL;
    return script->cache_owned_template && script->cache_direct_imports
        ? script->cache_direct_imports : script->direct_imports;
}

static bool lambda_ast_template_dependencies_reusable(Script* script,
        ArrayList* seen) {
    if (!lambda_ast_template_is_reusable_shallow(script) || !seen) return false;
    if (script_ptr_list_contains(seen, (Script*)script)) return true;
    if (!arraylist_append(seen, (void*)script)) return false;
    const ArrayList* dependencies = lambda_cache_direct_imports(script);
    if (!dependencies) return true;
    for (int i = 0; i < dependencies->length; i++) {
        const Script* dependency = (const Script*)dependencies->data[i];
        // A prebuild worker activates a cached child through a fresh runtime
        // shell.  Its `cache_template`, not the shell itself, owns the
        // immutable AST lease; rejecting the shell made every importer miss
        // despite its worker having already built the complete closure.
        const Script* dependency_template = lambda_cache_template_owner(
            dependency);
        if (!dependency_template ||
                !lambda_ast_template_dependencies_reusable(
                    (Script*)dependency_template, seen)) {
            return false;
        }
    }
    return true;
}

static bool lambda_cache_prepare_template_direct_imports(Script* script) {
    if (!script || script->cache_direct_imports) return script != NULL;
    if (!script->direct_imports || script->direct_imports->length == 0) return true;
    ArrayList* dependencies = arraylist_new(script->direct_imports->length);
    if (!dependencies) return false;
    for (int i = 0; i < script->direct_imports->length; i++) {
        const Script* template_owner = lambda_cache_template_owner(
            (const Script*)script->direct_imports->data[i]);
        if (!template_owner || !arraylist_append(dependencies,
                (void*)template_owner)) {
            arraylist_free(dependencies);
            return false;
        }
    }
    script->cache_direct_imports = dependencies;
    return true;
}

static bool lambda_ast_template_is_reusable(Script* script) {
    ArrayList* seen = arraylist_new(4);
    if (!seen) return false;
    bool reusable = lambda_ast_template_dependencies_reusable(script, seen);
    arraylist_free(seen);
    return reusable;
}

static bool lambda_cache_record_direct_dependencies(InputScriptCache* cache,
        const Script* importer) {
    const ArrayList* dependencies = lambda_cache_direct_imports(importer);
    if (!importer || !dependencies) return true;
    if (!importer->cache_compilation_unit_id) return false;
    for (int i = 0; i < dependencies->length; i++) {
        const Script* dependency = lambda_cache_template_owner(
            (const Script*)dependencies->data[i]);
        if (!dependency || !dependency->cache_compilation_unit_id ||
                !input_script_cache_record_dependency_by_unit(cache,
                    importer->cache_compilation_unit_id,
                    dependency->cache_compilation_unit_id)) {
            log_error("script-cache: could not record dependency importer=%s",
                importer->reference ? importer->reference : "<unknown>");
            return false;
        }
    }
    return true;
}

static bool lambda_cache_refresh_dependencies(const Script* script,
        InputScriptCache* cache, ArrayList* seen) {
    script = lambda_cache_template_owner(script);
    if (!script || !cache || !seen) return false;
    if (script_ptr_list_contains(seen, (Script*)script)) return false;
    if (!arraylist_append(seen, (void*)script)) return true;
    const ArrayList* dependencies = lambda_cache_direct_imports(script);
    if (!dependencies) return false;
    for (int i = 0; i < dependencies->length; i++) {
        const Script* dependency = (const Script*)dependencies->data[i];
        if (!dependency || !dependency->reference ||
                !dependency->cache_compilation_unit_id) {
            // A cache image with an untracked child has no safe freshness
            // proof, so bypass it rather than serving a stale import graph.
            return true;
        }
        if (script_file_stat_changed((Script*)dependency, dependency->reference)) {
            bool changed = false;
            if (!input_script_cache_refresh_file_unit(cache,
                    dependency->cache_compilation_unit_id, dependency->reference,
                    &changed) || changed) {
                log_info("script-cache: dependency changed importer=%s dependency=%s",
                    script->reference ? script->reference : "<unknown>",
                    dependency->reference);
                return true;
            }
        }
        if (lambda_cache_refresh_dependencies(dependency, cache, seen)) return true;
    }
    return false;
}

static bool lambda_cache_dependencies_changed(const Script* script,
        InputScriptCache* cache) {
    ArrayList* seen = arraylist_new(4);
    if (!seen) return true;
    bool changed = lambda_cache_refresh_dependencies(script, cache, seen);
    arraylist_free(seen);
    return changed;
}

static void lambda_cache_invalidate_untrusted_cone(InputScriptCache* cache,
        uint32_t compilation_unit_id, const char* lookup_path) {
    if (!cache || !compilation_unit_id) {
        return;
    }
    size_t retired = input_script_cache_invalidate_unit(cache,
        compilation_unit_id);
    if (retired > 0) {
        log_info("script-cache: retired %zu untrusted Lambda cache generation(s) for %s",
            retired, lookup_path ? lookup_path : "<unknown>");
    }
}

static Script* lambda_script_template_clone(Runtime* runtime, const Script* cached,
        InputCacheScope* scope) {
    if (!runtime || !cached) return NULL;
    Script* instance = (Script*)mem_calloc(1, sizeof(Script), MEM_CAT_SYSTEM);
    if (!instance) return NULL;
    memcpy(instance, cached, sizeof(Script));
    instance->reference = cached->reference
        ? mem_strdup(cached->reference, MEM_CAT_SYSTEM) : NULL;
    instance->directory = cached->directory
        ? mem_strdup(cached->directory, MEM_CAT_SYSTEM) : NULL;
    if ((cached->reference && !instance->reference) ||
            (cached->directory && !instance->directory)) {
        mem_free((void*)instance->reference);
        mem_free((void*)instance->directory);
        mem_free(instance);
        return NULL;
    }
    instance->cache_template = cached;
    instance->cache_scope = scope;
    instance->cache_owned_template = false;
    instance->cache_mir_artifact = cached->cache_mir_artifact;
    instance->is_loading = false;
    instance->is_retired = false;
    instance->interp_slab = NULL;
    instance->interp_views_registered = false;
    instance->ast_overlay_strings = NULL;
    instance->ast_promotion_overlay = NULL;
    // Satellite work and private code images belong exclusively to this run.
    instance->interp_satellite_queue = NULL;
    instance->interp_satellite_images = NULL;
    if (!instance->cache_mir_artifact) {
        // AST reuse borrows parser/analysis facts only. A MIR context
        // (including satellites promoted by an earlier execution) is
        // runtime-local and must never be copied into the new shell
        // (D8.5.1v3).
        instance->jit_context = NULL;
        instance->main_func = NULL;
        instance->mir_gen_initialized = false;
        instance->interp_satellite_count = 0;
        instance->interp_whole_script_poc_attempted = false;
        instance->interp_whole_script_poc_active = false;
    }
    // The template's dependency list belongs to the cache image. The clone
    // graph below replaces it with fresh Script shells for this EvalContext.
    instance->direct_imports = NULL;
    instance->cache_direct_imports = NULL;
    runtime_register_script(runtime, instance);
    return instance;
}

typedef struct LambdaAstCloneGraph {
    ArrayList* templates;
    ArrayList* instances;
} LambdaAstCloneGraph;

static Script* lambda_ast_clone_graph_find(const LambdaAstCloneGraph* graph,
        const Script* cached) {
    if (!graph || !graph->templates || !graph->instances) return NULL;
    for (int i = 0; i < graph->templates->length; i++) {
        if (graph->templates->data[i] == cached) {
            return (Script*)graph->instances->data[i];
        }
    }
    return NULL;
}

static Script* lambda_ast_template_clone_graph(Runtime* runtime,
        const Script* cached, LambdaAstCloneGraph* graph) {
    Script* existing = lambda_ast_clone_graph_find(graph, cached);
    if (existing) return existing;
    Script* instance = lambda_script_template_clone(runtime, cached, NULL);
    if (!instance || !arraylist_append(graph->templates, (void*)cached) ||
            !arraylist_append(graph->instances, instance)) {
        return NULL;
    }
    const ArrayList* dependencies = lambda_cache_direct_imports(cached);
    if (!dependencies || dependencies->length == 0) {
        return instance;
    }
    instance->direct_imports = arraylist_new(dependencies->length);
    if (!instance->direct_imports) return NULL;
    for (int i = 0; i < dependencies->length; i++) {
        const Script* dependency = lambda_cache_template_owner(
            (const Script*)dependencies->data[i]);
        Script* dependency_instance = lambda_ast_template_clone_graph(runtime,
            dependency, graph);
        if (!dependency_instance || !arraylist_append(instance->direct_imports,
                dependency_instance)) {
            return NULL;
        }
    }
    return instance;
}

static void lambda_ast_clone_graph_discard(Runtime* runtime,
        LambdaAstCloneGraph* graph) {
    if (!runtime || !graph || !graph->instances) return;
    for (int i = graph->instances->length - 1; i >= 0; i--) {
        Script* instance = (Script*)graph->instances->data[i];
        if (!instance) continue;
        int index = instance->index;
        runtime_free_script(runtime, instance, true);
        if (runtime->scripts && index >= 0 && index < runtime->scripts->length) {
            runtime->scripts->data[index] = NULL;
        }
    }
}

static Script* lambda_ast_template_clone_for_runtime(Runtime* runtime,
        const Script* cached, InputCacheScope* scope) {
    LambdaAstCloneGraph graph = {arraylist_new(4), arraylist_new(4)};
    if (!graph.templates || !graph.instances) {
        if (graph.templates) arraylist_free(graph.templates);
        if (graph.instances) arraylist_free(graph.instances);
        return NULL;
    }
    Script* instance = lambda_ast_template_clone_graph(runtime, cached, &graph);
    if (instance) instance->cache_scope = scope;
    else lambda_ast_clone_graph_discard(runtime, &graph);
    arraylist_free(graph.templates);
    arraylist_free(graph.instances);
    return instance;
}

Script* lambda_ast_overlay_import_script(const Script* importer,
        const AstImportNode* import_node) {
    if (!import_node || !importer || !importer->cache_template ||
            import_node->is_cross_lang || !importer->direct_imports) {
        return import_node ? import_node->script : NULL;
    }
    AstScript* root = (AstScript*)importer->ast_root;
    int direct_index = 0;
    for (AstNode* child = root ? root->child : NULL; child; child = child->next) {
        if (child->node_type != AST_NODE_IMPORT) continue;
        AstImportNode* candidate = (AstImportNode*)child;
        if (candidate->is_cross_lang) continue;
        if (candidate == import_node) {
            return direct_index < importer->direct_imports->length
                ? (Script*)importer->direct_imports->data[direct_index] : NULL;
        }
        direct_index++;
    }
    return import_node->script;
}

const char* lambda_ast_overlay_string(Script* script, const char* text) {
    if (!script || !text) return NULL;
    char* copy = mem_strdup(text, MEM_CAT_SYSTEM);
    if (!copy) return NULL;
    if (!script->ast_overlay_strings) script->ast_overlay_strings = arraylist_new(2);
    if (!script->ast_overlay_strings || !arraylist_append(script->ast_overlay_strings, copy)) {
        mem_free(copy);
        return NULL;
    }
    return copy;
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
static bool lambda_finalize_ast_template_for_execution(Runtime* runtime,
        Script* script);

static bool interp_force_jit_script(Script* script, Runtime* runtime) {
    if (!script || !runtime) return false;
    if (script->ast_frontend_only &&
            !lambda_finalize_ast_template_for_execution(runtime, script)) {
        return false;
    }
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

static bool lambda_prepare_ast_interpreter(Transpiler* tp) {
    if (!tp || !tp->ast_root) return false;
    AstScript* interp_root = (AstScript*)tp->ast_root;
    if (tp->direct_imports) {
        arraylist_free(tp->direct_imports);
        tp->direct_imports = NULL;
    }
    for (AstNode* child = interp_root->child; child; child = child->next) {
        if (child->node_type != AST_NODE_IMPORT) continue;
        AstImportNode* import_node = (AstImportNode*)child;
        Script* imported = lambda_ast_overlay_import_script(tp->script_owner,
            import_node);
        if (import_node->is_cross_lang) {
            // Cross-language namespaces are execution-owned. Keeping one in
            // a Lambda AST image could retain another Runtime's adapter state.
            tp->cache_cross_lang_tainted = true;
            continue;
        }
        if (!imported) continue;
        if (!tp->direct_imports) tp->direct_imports = arraylist_new(4);
        if (!tp->direct_imports || !arraylist_append(tp->direct_imports,
                imported)) return false;
    }
    AstNodeType reject = AST_NODE_NULL;
    bool supported = interp_scan_supported(tp, &reject) && interp_plan_script(tp);
    if (supported) {
        tp->interp_supported = true;
        tp->ast_frontend_only = false;
        return true;
    }
    tp->interp_reject_kind = reject;
    return false;
}

static bool lambda_finalize_ast_template_for_execution(Runtime* runtime,
        Script* script) {
    if (!runtime || !script || !script->ast_frontend_only) return script != NULL;
    if (runtime->ast_prebuild_only) return true;
    Transpiler transpiler = {};
    memcpy(&transpiler, script, sizeof(Script));
    transpiler.script_owner = script;
    transpiler.runtime = runtime;
    script->ast_frontend_only = false;
    transpiler.ast_frontend_only = false;
    if (lambda_prepare_ast_interpreter(&transpiler)) {
        script_adopt_transpiler(script, &transpiler);
        log_info("module-ast-prebuild: activated Lambda AST template path=%s",
            script->reference ? script->reference : "<unknown>");
        return true;
    }
    log_info("module-ast-prebuild: execution MIR fallback path=%s reason=node:%s",
        script->reference ? script->reference : "<unknown>",
        interp_node_kind_name(transpiler.interp_reject_kind));
    if (!interp_force_jit_import_cone(&transpiler)) return false;
    compile_script_as_mir_direct(&transpiler, script, script->reference,
        NULL, NULL, NULL, NULL, NULL, NULL);
    return script->jit_context != NULL;
}

typedef struct LambdaDirectFrontendPassContext {
    Transpiler* tp;
    const char* script_path;
    LambdaParseError parse_error;
    LambdaReductionTape* reductions;
    ArrayList* functions;
} LambdaDirectFrontendPassContext;

static int lambda_parse_compiler_pass(void* opaque) {
    LambdaDirectFrontendPassContext* pass =
        (LambdaDirectFrontendPassContext*)opaque;
    if (!pass || !pass->tp || lambda_rd_parse_reductions(pass->tp->source,
                strlen(pass->tp->source), &pass->reductions, &pass->parse_error) !=
                LAMBDA_PARSE_OK) {
        if (pass && pass->tp) {
            record_direct_parse_diagnostics(pass->tp, pass->script_path,
                &pass->parse_error);
            log_error("C parser rejected %s: %s", pass->script_path,
                pass->parse_error.message ? pass->parse_error.message :
                "direct AST reduction failed");
        }
        return 0;
    }
    return 1;
}

static int lambda_build_compiler_pass(void* opaque) {
    LambdaDirectFrontendPassContext* pass =
        (LambdaDirectFrontendPassContext*)opaque;
    AstScript* root = NULL;
    if (!pass || !pass->tp || lambda_rd_build_reductions(pass->tp,
                pass->tp->source, strlen(pass->tp->source), pass->reductions,
                &root, &pass->parse_error) != LAMBDA_PARSE_OK || !root) {
        if (pass && pass->tp) {
            record_direct_parse_diagnostics(pass->tp, pass->script_path,
                &pass->parse_error);
            log_error("C AST build rejected %s: %s", pass->script_path,
                pass->parse_error.message ? pass->parse_error.message :
                "direct AST construction failed");
        }
        if (pass) {
            lambda_rd_destroy_reductions(pass->reductions);
            pass->reductions = NULL;
        }
        return 0;
    }
    lambda_rd_destroy_reductions(pass->reductions);
    pass->reductions = NULL;
    pass->tp->ast_root = (AstNode*)root;
    return 1;
}

static int lambda_bind_compiler_pass(void* opaque) {
    LambdaDirectFrontendPassContext* pass =
        (LambdaDirectFrontendPassContext*)opaque;
    if (!pass || !pass->tp || !pass->tp->ast_root ||
            !lambda_ast_rebind_direct_scope_graph_with_functions(pass->tp,
                (AstScript*)pass->tp->ast_root, &pass->functions)) {
        if (pass && pass->tp) {
            log_error("Lambda AST binding rejected %s", pass->script_path);
        }
        return 0;
    }
    return 1;
}

static int lambda_validate_compiler_pass(void* opaque) {
    LambdaDirectFrontendPassContext* pass =
        (LambdaDirectFrontendPassContext*)opaque;
    if (!pass) return 0;
    int valid = lambda_ast_finalize_script_with_functions(pass->tp,
        (AstScript*)pass->tp->ast_root, pass->functions);
    arraylist_free(pass->functions);
    pass->functions = NULL;
    return valid;
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
    if (profiling || compiler_timing) lambda_compiler_timing_reset();
    profile_time_t p0, p1, p2, p3, p4, p5;
    LambdaOwnTimingFrame own_timing = {};
    bool own_timing_enabled = profiling || compiler_timing;
    if (own_timing_enabled) lambda_own_timing_enter(&own_timing);
    if (profiling || compiler_timing) profile_get_time(&p0);

    get_time(&start);
    tp->source = script->source;
    tp->defer_ast_index_columns = lambda_tier_selected() != LAMBDA_TIER_JIT;
    LambdaDirectFrontendPassContext front_end = {tp, script_path, {}};
    compiler_pass_manager_init(&tp->pass_manager, COMPILER_FACT_NONE);
    CompilerPassSpec parse_pass = {"parse", COMPILER_FACT_NONE,
        COMPILER_FACT_PARSED, lambda_parse_compiler_pass, &front_end};
    CompilerPassSpec build_pass = {"build", COMPILER_FACT_PARSED,
        COMPILER_FACT_AST, lambda_build_compiler_pass, &front_end};
    CompilerPassSpec bind_pass = {"bind", COMPILER_FACT_AST,
        COMPILER_FACT_BOUND | COMPILER_FACT_INDEXED,
        lambda_bind_compiler_pass, &front_end};
    lambda_own_timing_set_phase(&own_timing, LAMBDA_OWN_TIMING_PARSE);
    if (!compiler_pass_manager_add(&tp->pass_manager, &parse_pass) ||
            !compiler_pass_manager_run(&tp->pass_manager, NULL)) {
        lambda_rd_destroy_reductions(front_end.reductions);
        if (own_timing_enabled) lambda_own_timing_leave(&own_timing);
        return;
    }
    if (profiling || compiler_timing) profile_get_time(&p1);
    get_time(&end);
    print_elapsed_time("parsing", start, end);

    lambda_own_timing_set_phase(&own_timing, LAMBDA_OWN_TIMING_BUILD);
    if (!compiler_pass_manager_add(&tp->pass_manager, &build_pass) ||
            !compiler_pass_manager_run(&tp->pass_manager, NULL)) {
        lambda_rd_destroy_reductions(front_end.reductions);
        if (own_timing_enabled) lambda_own_timing_leave(&own_timing);
        return;
    }
    if (profiling || compiler_timing) profile_get_time(&p2);
    get_time(&end);
    print_elapsed_time("building AST", start, end);

    lambda_own_timing_set_phase(&own_timing, LAMBDA_OWN_TIMING_BIND);
    if (!compiler_pass_manager_add(&tp->pass_manager, &bind_pass) ||
            !compiler_pass_manager_run(&tp->pass_manager, NULL)) {
        arraylist_free(front_end.functions);
        if (own_timing_enabled) lambda_own_timing_leave(&own_timing);
        return;
    }
    if (tp->defer_ast_index_columns) {
        tp->pass_manager.facts &= ~COMPILER_FACT_INDEXED;
    }
    if (profiling || compiler_timing) profile_get_time(&p3);
    get_time(&end);
    print_elapsed_time("binding AST", start, end);

    CompilerPassSpec validate_pass = {"validate", COMPILER_FACT_AST |
        COMPILER_FACT_BOUND, COMPILER_FACT_VALIDATED,
        lambda_validate_compiler_pass, &front_end};
    lambda_own_timing_set_phase(&own_timing, LAMBDA_OWN_TIMING_VALIDATE);
    if (!compiler_pass_manager_add(&tp->pass_manager, &validate_pass) ||
            !compiler_pass_manager_run(&tp->pass_manager, NULL)) {
        log_error("compiler validation rejected '%s'", script_path);
        arraylist_free(front_end.functions);
        if (own_timing_enabled) lambda_own_timing_leave(&own_timing);
        return;
    }
    if (profiling || compiler_timing) profile_get_time(&p4);
    // Allocation reserves dense node IDs and bind publishes graph columns, so
    // no post-validation index walk remains (D8.2.4/D8.2.5v2).
    if (profiling || compiler_timing) p5 = p4;
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

    // T0 (D8.1.1v2): stop after the AST passes and interpret. A script whose
    // pre-scan finds a kind the walker cannot execute falls back to the whole
    // module JIT path below, counted and logged — never silently half-run (R4).
    if (lambda_tier_selected() == LAMBDA_TIER_INTERP ||
            lambda_tier_selected() == LAMBDA_TIER_AUTO) {
        profile_time_t plan0, plan1;
        if (profiling || compiler_timing) profile_get_time(&plan0);
        lambda_own_timing_set_phase(&own_timing, LAMBDA_OWN_TIMING_PLAN);
        bool supported = lambda_prepare_ast_interpreter(tp);
        if (profiling || compiler_timing) profile_get_time(&plan1);
        if (supported) {
            tp->interp_supported = true;
            script_adopt_transpiler(script, tp);
            if (compiler_timing) {
                LambdaCompilerTiming* timing = &g_last_lambda_compiler_timing;
                timing->parse_us = (uint64_t)(lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_PARSE, p0, p1) * 1000.0);
                timing->ast_build_us = (uint64_t)(lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_BUILD, p1, p2) * 1000.0);
                timing->bind_us = (uint64_t)(lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_BIND, p2, p3) * 1000.0);
                timing->validate_us = (uint64_t)(lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_VALIDATE, p3, p4) * 1000.0);
                timing->index_us = (uint64_t)(lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_INDEX, p4, p5) * 1000.0);
                timing->plan_us = (uint64_t)(lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_PLAN, plan0, plan1) * 1000.0);
                timing->build_transpile_us = timing->parse_us + timing->ast_build_us +
                    timing->bind_us + timing->validate_us + timing->index_us +
                    timing->plan_us;
                timing->valid = 1;
            }
            if (profiling) {
                PhaseProfile prof;
                memset(&prof, 0, sizeof(prof));
                profile_set_script_path(&prof, script_path);
                prof.parse_ms = lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_PARSE, p0, p1);
                prof.ast_ms = lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_BUILD, p1, p2) + lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_BIND, p2, p3) +
                    lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_VALIDATE, p3, p4) + lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_INDEX, p4, p5);
                prof.inline_analysis_ms =
                    (double)g_last_lambda_compiler_timing.analysis_us / 1000.0;
                double build_ms = lambda_own_timing_elapsed(&own_timing,
                    LAMBDA_OWN_TIMING_BUILD, p1, p2);
                prof.build_reducer_ms = build_ms > prof.inline_analysis_ms
                    ? build_ms - prof.inline_analysis_ms : 0;
                prof.plan_ms = lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_PLAN, plan0, plan1);
                prof.worker_thread = 0;
                prof.thread_id = profile_current_thread_id();
                profile_record_phase(&prof);
            }
            log_notice("interp: planned file=%s module_slots=%u",
                script_path, (unsigned)tp->interp_slab_count);
            if (own_timing_enabled) lambda_own_timing_leave(&own_timing);
            return;
        }
        if (tp->runtime && tp->runtime->ast_prebuild_only) {
            // The worker retains only validated/indexed front-end facts. The
            // execution Runtime decides whether this module needs MIR later.
            tp->ast_frontend_only = true;
            script_adopt_transpiler(script, tp);
            log_info("module-ast-prebuild: retained unsupported Lambda AST path=%s reason=node:%s",
                script_path, interp_node_kind_name(tp->interp_reject_kind));
            if (own_timing_enabled) lambda_own_timing_leave(&own_timing);
            return;
        }
        interp_run_stats()->scripts_fallback++;
        log_notice("interp: fallback file=%s reason=node:%s",
            script_path, interp_node_kind_name(tp->interp_reject_kind));
        if (!interp_force_jit_import_cone(tp)) {
            if (own_timing_enabled) lambda_own_timing_leave(&own_timing);
            return;
        }
    }

    // compile the AST directly to MIR; this is the only supported Lambda backend.
    {
        double mir_jit_init_ms = 0, mir_transpile_ms = 0, mir_gen_ms = 0;
        uint64_t mir_module_count = 0;
        uint64_t mir_function_count = 0;
        uint64_t mir_instruction_count = 0;
        lambda_own_timing_set_phase(&own_timing, LAMBDA_OWN_TIMING_MIR);
        compile_script_as_mir_direct(tp, script, script_path,
                                      profiling || compiler_timing ? &mir_jit_init_ms : NULL,
                                      profiling || compiler_timing ? &mir_transpile_ms : NULL,
                                      profiling || compiler_timing ? &mir_gen_ms : NULL,
                                      compiler_timing ? &mir_module_count : NULL,
                                      compiler_timing ? &mir_function_count : NULL,
                                      compiler_timing ? &mir_instruction_count : NULL);
        if (compiler_timing) {
            LambdaCompilerTiming* timing = &g_last_lambda_compiler_timing;
            timing->parse_us = (uint64_t)(lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_PARSE, p0, p1) * 1000.0);
            timing->ast_build_us = (uint64_t)(lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_BUILD, p1, p2) * 1000.0);
            timing->bind_us = (uint64_t)(lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_BIND, p2, p3) * 1000.0);
            timing->validate_us = (uint64_t)(lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_VALIDATE, p3, p4) * 1000.0);
            timing->index_us = (uint64_t)(lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_INDEX, p4, p5) * 1000.0);
            timing->module_finalize_us = (uint64_t)(mir_jit_init_ms * 1000.0);
            timing->mir_lower_us = (uint64_t)(mir_transpile_ms * 1000.0);
            timing->link_us = (uint64_t)(mir_gen_ms * 1000.0);
            timing->build_transpile_us = timing->parse_us + timing->ast_build_us +
                timing->bind_us + timing->validate_us + timing->index_us +
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
                prof.parse_ms = lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_PARSE, p0, p1);
                prof.ast_ms = lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_BUILD, p1, p2) + lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_BIND, p2, p3) +
                    lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_VALIDATE, p3, p4) + lambda_own_timing_elapsed(&own_timing, LAMBDA_OWN_TIMING_INDEX, p4, p5);
            prof.inline_analysis_ms =
                (double)g_last_lambda_compiler_timing.analysis_us / 1000.0;
            double build_ms = lambda_own_timing_elapsed(&own_timing,
                LAMBDA_OWN_TIMING_BUILD, p1, p2);
            prof.build_reducer_ms = build_ms > prof.inline_analysis_ms
                ? build_ms - prof.inline_analysis_ms : 0;
            prof.transpile_ms = mir_transpile_ms;
            prof.jit_init_ms = mir_jit_init_ms;
            prof.mir_gen_ms = mir_gen_ms;
            prof.code_len = 0;
            prof.worker_thread = 0;
            prof.thread_id = profile_current_thread_id();
            profile_record_phase(&prof);
        }
        if (own_timing_enabled) lambda_own_timing_leave(&own_timing);
        return;
    }

}

// ============================================================================

Script* load_script(Runtime *runtime, const char* script_path, const char* source, bool is_import) {
    log_info("Loading script: %s (is_import=%d)", script_path, is_import);

    // Build the static closure before the root enters its Runtime. Workers only
    // publish AST templates; import initialization and tier selection stay on
    // this execution path (D8.1.1v13, D8.5.1v7).
    if (runtime && !runtime->ast_prebuild_only && !is_import && !source &&
            lambda_tier_selected() != LAMBDA_TIER_JIT &&
            input_script_cache_ast_enabled(input_manager_global_script_cache())) {
        (void)lambda_ast_prebuild_imports(script_path);
    }
    if (runtime && !runtime->ast_prebuild_only && is_import && !source &&
            lambda_tier_selected() != LAMBDA_TIER_JIT &&
            input_script_cache_ast_enabled(input_manager_global_script_cache())) {
        // Only an ordinary consumer waits, and only for its own direct module.
        // Prebuild workers bypass this path to keep the bounded pool runnable.
        profile_time_t await_start, await_end;
        bool account_prebuild_wait = g_lambda_own_timing_frame != NULL;
        if (account_prebuild_wait) profile_get_time(&await_start);
        (void)module_ast_prebuild_await_import(lambda_ast_prebuild_profile(),
            script_path);
        if (account_prebuild_wait) {
            profile_get_time(&await_end);
            lambda_own_timing_subtract_active(elapsed_ms_val(await_start,
                await_end));
        }
    }

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

    InputScriptRequest cache_request = {};
    cache_request.identity = lookup_path;
    cache_request.source = source;
    cache_request.source_length = source ? strlen(source) : 0;
    cache_request.source_kind = source ? INPUT_SCRIPT_SOURCE_INLINE
        : INPUT_SCRIPT_SOURCE_FILE;
    cache_request.language = "lambda";
    cache_request.profile = "lambda";
    cache_request.parser_abi = "lambda-direct-parser-v1";
    cache_request.parse_flags = runtime->static_warning ? "static-warning" : "default";
    cache_request.resolution_base = runtime->import_base_dir
        ? runtime->import_base_dir : lookup_path;
    cache_request.backend = "mir-direct";
    cache_request.execution_mode = is_import ? "module" : "script";
    cache_request.ast_abi = 1;
    cache_request.compiler_abi = 1;
    cache_request.optimize_level = runtime->optimize_level;
    cache_request.module_mode = is_import;
    InputScriptCache* script_cache = input_manager_global_script_cache();
    InputCacheScope* cache_scope = input_script_cache_open_scope(script_cache);
    if (!cache_scope) {
        if (canonical_path) mem_free(canonical_path);
        log_error("script-cache: failed to open Lambda source scope");
        return NULL;
    }
    InputScriptLease* raw_lease = source
        ? input_script_cache_acquire(cache_scope, &cache_request)
        : input_script_cache_acquire_file(cache_scope, &cache_request, lookup_path);
    LambdaScriptSourceLease source_lease(raw_lease, cache_scope);
    if (!raw_lease) {
        if (canonical_path) mem_free(canonical_path);
        log_error("script-cache: failed to acquire Lambda source %s", lookup_path);
        return NULL;
    }
    const char* exact_source = source_lease.source();
    size_t exact_source_length = source_lease.source_length();
    // REPL fragments append mutable AST/source history. Until that history is
    // part of its key, retain only its common source record (D8.5.1v3).
    bool cache_artifact_enabled = strcmp(lookup_path, "<repl-session>") != 0;

    // find the script in the path index (thread-safe)
#ifndef _WIN32
    pthread_mutex_lock(&scripts_mutex);
#endif
    Script* cached_script = runtime_loaded_script_get_current(runtime, lookup_path);
    if (cached_script) {
        if (!script_source_matches(cached_script, exact_source, exact_source_length)) {
            log_info("runtime-script-registry: source bytes changed path=%s index=%d",
                lookup_path, cached_script->index);
            retire_script_cone(runtime, cached_script);
            InputScriptRequest invalidation_request = cache_request;
            invalidation_request.source = exact_source;
            invalidation_request.source_length = exact_source_length;
            size_t retired = input_script_cache_invalidate(script_cache,
                &invalidation_request);
            if (retired > 0) {
                log_info("script-cache: retired %zu stale Lambda source generation(s) for %s",
                    retired, lookup_path);
            }
            cached_script = NULL;
        }
    }
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
        runtime->script_load_hits++;
        log_info("runtime-script-registry: hit path=%s index=%d",
                 lookup_path, cached_script->index);
        if (canonical_path) mem_free(canonical_path);
        return cached_script;
    }

    // The process cache owns immutable Lambda images; each Runtime receives a
    // shell with its own execution lease while code resolves mutable values
    // through that Runtime's module-state table. Do this after same-runtime
    // circular-import lookup so an in-flight source never observes itself as
    // a finished cache artifact.
    void* cached_image = NULL;
    bool cache_artifact_rejected = false;
    if (cache_artifact_enabled && runtime->use_mir_direct && !runtime->mir_cache_disabled &&
            lambda_tier_selected() == LAMBDA_TIER_JIT &&
            input_script_cache_get_mir(raw_lease, &cached_image)) {
        Script* cached_template = (Script*)cached_image;
        if (cached_template && cached_template->cache_owned_template &&
                cached_template->jit_context && cached_template->main_func) {
            if (lambda_cache_dependencies_changed(cached_template, script_cache)) {
                // A dependency changed or lost its tracked identity. Retire
                // this root before retrying so recursion cannot rediscover
                // the same no-longer-trustworthy artifact.
                lambda_cache_invalidate_untrusted_cone(script_cache,
                    cached_template->cache_compilation_unit_id, lookup_path);
                InputCacheScope* stale_scope = source_lease.release_scope();
                input_script_cache_close_scope(stale_scope);
#ifndef _WIN32
                pthread_mutex_unlock(&scripts_mutex);
#endif
                if (canonical_path) mem_free(canonical_path);
                return load_script(runtime, script_path, source, is_import);
            }
            // Generated imports name logical units, but those units still
            // need this Runtime's dense slabs before the cached root enters
            // MIR. Instantiate the complete cached dependency cone (D8.5.1v3).
            Script* instance = lambda_ast_template_clone_for_runtime(runtime,
                cached_template, source_lease.scope());
            if (instance) {
                (void)source_lease.release_scope();
                runtime->script_load_hits++;
                input_script_cache_mark_module_hit(script_cache);
                log_info("script-cache: Lambda MIR hit path=%s unit=%u", lookup_path,
                    cached_template->cache_compilation_unit_id);
#ifndef _WIN32
                // The common entry is immutable and its lease is now owned by
                // the instance, so no Runtime-index mutation remains under
                // this lock. Returning with it held deadlocks the next miss.
                pthread_mutex_unlock(&scripts_mutex);
#endif
                if (canonical_path) mem_free(canonical_path);
                return instance;
            }
            log_error("script-cache: failed to instantiate Lambda MIR image %s",
                lookup_path);
        }
        cache_artifact_rejected = true;
    }
    // T0 AST templates retain only parser/validation/frame-plan facts. A hit
    // always receives a new Script and dense module slab, never an old
    // EvalContext, satisfying D8.5.1v3 without changing tier policy.
    void* cached_ast = NULL;
    if (cache_artifact_enabled && lambda_tier_selected() != LAMBDA_TIER_JIT &&
            input_script_cache_get_ast(raw_lease, &cached_ast)) {
        Script* cached_template = (Script*)cached_ast;
        if (cached_template && cached_template->cache_owned_template &&
                lambda_ast_template_is_reusable(cached_template)) {
            if (lambda_cache_dependencies_changed(cached_template, script_cache)) {
                // See the MIR path: a retry must not reacquire this artifact.
                lambda_cache_invalidate_untrusted_cone(script_cache,
                    cached_template->cache_compilation_unit_id, lookup_path);
                InputCacheScope* stale_scope = source_lease.release_scope();
                input_script_cache_close_scope(stale_scope);
#ifndef _WIN32
                pthread_mutex_unlock(&scripts_mutex);
#endif
                if (canonical_path) mem_free(canonical_path);
                return load_script(runtime, script_path, source, is_import);
            }
            Script* instance = lambda_ast_template_clone_for_runtime(runtime, cached_template,
                source_lease.scope());
            if (instance) {
                (void)source_lease.release_scope();
                runtime->script_load_hits++;
                input_script_cache_mark_module_hit(script_cache);
                log_info("script-cache: Lambda AST hit path=%s unit=%u", lookup_path,
                    cached_template->cache_compilation_unit_id);
#ifndef _WIN32
                pthread_mutex_unlock(&scripts_mutex);
#endif
                if (!lambda_finalize_ast_template_for_execution(runtime, instance)) {
                    log_error("module-ast-prebuild: failed to activate Lambda template %s",
                        lookup_path);
                    if (canonical_path) mem_free(canonical_path);
                    return NULL;
                }
                if (canonical_path) mem_free(canonical_path);
                return instance;
            }
            log_error("script-cache: failed to instantiate Lambda AST template %s",
                lookup_path);
        }
        cache_artifact_rejected = true;
    }
    InputScriptBuildKind build_kind = lambda_tier_selected() == LAMBDA_TIER_JIT &&
        runtime->use_mir_direct && !runtime->mir_cache_disabled
        ? INPUT_SCRIPT_BUILD_MIR : INPUT_SCRIPT_BUILD_AST;
    // A cache claim may wait for a prebuild worker that recursively loads one
    // of its imports. The registry lock protects this Runtime's short index
    // mutations only; holding it across that wait blocks the publisher at its
    // nested registration and deadlocks the whole prebuild pool.
#ifndef _WIN32
    pthread_mutex_unlock(&scripts_mutex);
#endif
    LambdaScriptBuildClaim build_claim(raw_lease, build_kind,
        cache_artifact_enabled);
    if (build_claim.is_ready()) {
        // A waiter can observe READY only after the owner published between
        // this call's first lookup and claim. Retry that normal hit without
        // retiring it; only a lookup that actually rejected an artifact is
        // untrusted (D8.5.1v3).
        if (cache_artifact_rejected) {
            lambda_cache_invalidate_untrusted_cone(script_cache,
                input_script_compilation_unit_id(input_script_lease_input(raw_lease)),
                lookup_path);
        }
        InputCacheScope* ready_scope = source_lease.release_scope();
        input_script_cache_close_scope(ready_scope);
        if (canonical_path) mem_free(canonical_path);
        return load_script(runtime, script_path, source, is_import);
    }
    if (build_claim.is_poisoned()) {
        // Source invalidation clears poisoning. This execution stays local and
        // never republishes a key whose prior builder failed integrity checks.
        log_info("script-cache: poisoned Lambda artifact bypass path=%s", lookup_path);
    }
    runtime->script_load_misses++;
    log_info("runtime-script-registry: miss path=%s", lookup_path);
    // script not found — create stub and register immediately to prevent duplicates
#ifndef _WIN32
    pthread_mutex_lock(&scripts_mutex);
#endif
    Script *new_script = (Script*)mem_calloc(1, sizeof(Script), MEM_CAT_SYSTEM);
    new_script->reference = mem_strdup(lookup_path, MEM_CAT_SYSTEM);
    new_script->is_loading = true;
    new_script->profile = &lambda_profile;
    uint32_t compilation_unit_id = input_script_compilation_unit_id(
        input_script_lease_input(raw_lease));
    // Every source-backed image receives a stable logical identity.  Its
    // physical module-state ID is assigned only when registered in this
    // Runtime, keeping the EvalContext table compact across cache reuse.
    new_script->cache_compilation_unit_id = compilation_unit_id;
    runtime_register_script(runtime, new_script);
#ifndef _WIN32
    pthread_mutex_unlock(&scripts_mutex);
#endif

    // strdup when source is provided externally (e.g. REPL) so the script owns its copy
    // and runtime_cleanup can safely free it without a double-free
    const char* script_source = mem_strdup(exact_source, MEM_CAT_SYSTEM);
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
        char* dir = mem_dup_n(lookup_path, dir_len, MEM_CAT_SYSTEM);
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
    if (!new_script->jit_context && !new_script->interp_supported &&
            !new_script->ast_frontend_only) {
        log_error("Error: Failed to compile script %s", script_path);
        return NULL;
    }
    // The common cache accepts only self-contained Lambda MIR images. A
    // cross-language import can retain guest-owned callbacks or module
    // namespaces, so it stays source-only until its adapter proves a fresh
    // instance contract.
    bool cache_dependencies_valid = !cache_artifact_enabled ||
        (!build_claim.is_poisoned() && lambda_cache_record_direct_dependencies(
            script_cache, new_script));
    if (!cache_dependencies_valid) input_script_cache_mark_rejected(script_cache);
    bool cache_published = false;
    if (cache_artifact_enabled && cache_dependencies_valid &&
            lambda_cache_prepare_template_direct_imports(new_script) &&
            new_script->jit_context && runtime->use_mir_direct &&
            !runtime->mir_cache_disabled && !new_script->cache_cross_lang_tainted) {
        InputScriptArtifactOps ops = {
            lambda_script_cache_destroy_artifact,
            lambda_script_cache_artifact_bytes,
        };
        if (input_script_cache_publish_mir(raw_lease, new_script, &ops)) {
            cache_published = true;
            new_script->cache_owned_template = true;
            new_script->cache_mir_artifact = true;
            new_script->cache_scope = source_lease.release_scope();
            log_info("script-cache: admitted Lambda MIR path=%s unit=%u",
                new_script->reference, new_script->cache_compilation_unit_id);
        }
    }
    else if (cache_artifact_enabled && cache_dependencies_valid &&
            lambda_ast_template_is_reusable(new_script) &&
            lambda_cache_prepare_template_direct_imports(new_script)) {
        InputScriptArtifactOps ops = {
            lambda_script_cache_destroy_artifact,
            lambda_script_cache_artifact_bytes,
        };
        if (input_script_cache_publish_ast(raw_lease, new_script, &ops)) {
            cache_published = true;
            new_script->cache_owned_template = true;
            new_script->cache_scope = source_lease.release_scope();
            log_info("script-cache: admitted Lambda AST path=%s unit=%u",
                new_script->reference, new_script->cache_compilation_unit_id);
        }
    }
    build_claim.complete(cache_published);
    runtime->script_load_compiles++;

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
    // A rejected fragment may have populated the pointer index with bindings
    // that are no longer in the retained list.  Drop that derived cache so
    // the next lookup cannot observe a rolled-back declaration.
    scope->name_index = NULL;
    scope->name_index_capacity = 0;
    scope->name_index_count = 0;
    scope->entry_count = 0;
    for (NameEntry* entry = first; entry; entry = entry->next) {
        scope->entry_count++;
    }
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
    LambdaParseStatus parse_status = lambda_rd_reduce_ast(&tp, fragment_source,
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
    lambda_ast_finalize_script(&tp, parsed_root);

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

// runtime_get_eval_context lives in runtime-state.cpp with the ownership seam (SCU14).

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

    // one recoverable limit per thread, shared with the JS guards (JC23)
    ctx->stack_limit = lambda_stack_recoverable_limit();

    ArrayList* next_type_list = runner->script->type_list;
    if (runtime_type_list(runner->runtime) && runtime_type_list(runner->runtime) != next_type_list &&
            !runtime_type_list_is_script_owned(runner->runtime)) {
        // A Lambda package can replace the active JS registry on a reused
        // heap. Release the old Runtime-owned registry before publishing the
        // package's Script-owned list, or final teardown loses that pointer.
        arraylist_free(runtime_type_list(runner->runtime));
    }
    runtime_set_type_list(runner->runtime, next_type_list);
    ctx->pool = runner->script->pool;
    ctx->type_list = next_type_list;

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
    if (rt && runtime_heap(rt)) {
        // Reuse retained heap and name_pool from a previous evaluation
        log_debug("runner_setup_context: reusing retained heap from Runtime");
        if (!runtime_context_bind_retained(rt, ctx)) return;
    } else {
        // First evaluation on this Runtime — create fresh resources
        ctx->name_pool = name_pool_create_runtime(ctx->pool);
        if (!ctx->name_pool) {
            log_error("Failed to create runtime name_pool");
        }
        heap_init();
        ctx->pool = ctx->heap->pool;
        // Publish the owner pair together so retained-context users always
        // observe a coherent heap/name-pool generation (D5.2.1v3).
        runtime_context_publish_owners(rt, ctx);
    }
    path_register_pool_provider(runner_path_pool_provider);

    if (rt && runtime_scheduler(rt)) {
        ctx->scheduler = runtime_scheduler(rt);
    } else {
        ctx->scheduler = lambda_scheduler_create(LAMBDA_MAILBOX_DEFAULT_CAPACITY);
        if (rt) runtime_set_scheduler(rt, ctx->scheduler);
    }
    // SCU15: cross-language JS imports compile on this same canonical context
    // (load_js_module binds runtime_get_eval_context), so their JS capsule is
    // already in ctx's JS capsule; there is no separate bootstrap context to adopt.
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
            resolve_sys_paths_recursive(array_item_read(list, i));
        }
    } else if (type_id == LMD_TYPE_MAP) {
        Map* map = item.map;
        if (map) resolve_sys_paths_in_shape((TypeMap*)map->type, map->data);

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
    RuntimeExecutionScope execution_scope(ctx);

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
        result = runtime_publish_result(context, lambda_recovery_publish_fault_item(
            (Context*)context, LAMBDA_FAULT_OUT_OF_MEMORY, ERR_OK));
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
        result = runtime_publish_result(context, recovered);
    } else {
        if (!lambda_recovery_frame_arm(recovery_frame)) {
            log_error("exec: failed to arm recovery frame");
            lambda_recovery_frame_end(recovery_frame);
            result = runtime_publish_result(context, lambda_recovery_publish_fault_item(
                (Context*)context, LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK));
        } else {
            log_debug("exec main func");
            result = runtime_publish_result(context,
                runner->script->main_func(context));
            lambda_recovery_frame_end(recovery_frame);
            log_debug("after main func, result type_id=%d", get_type_id(result));
        }
    }
    if ((!runner->runtime || !runner->runtime->no_task_drain) && context->scheduler) {
        lambda_scheduler_drain(context->scheduler);
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
    runtime->loaded_script_index = RuntimeLoadedScriptIndex::create(64);
    runtime->max_errors = 10;  // default error threshold
    runtime->optimize_level = 2;  // default MIR optimization level (0=debug, 2=release)
    runtime->dry_run = false;  // default: real IO
    InputScriptCache* script_cache = input_manager_global_script_cache();
    const char* disable_mir_cache = shell_getenv("LAMBDA_DISABLE_MIR_CACHE");
    runtime->mir_cache_disabled = (LAMBDA_MIR_CACHE_DEFAULT == 0) ||
        !input_script_cache_mir_enabled(script_cache) ||
        (disable_mir_cache &&
         (strcmp(disable_mir_cache, "1") == 0 || strcmp(disable_mir_cache, "true") == 0));
    // debug and release builds enable retained MIR imports by default; this opt-out is for timing and emergency bisecting.
    if (runtime->mir_cache_disabled) {
        log_info("runtime-script-registry: process MIR artifacts disabled by build default or LAMBDA_DISABLE_MIR_CACHE");
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
    // Logical cache units are independent from this Runtime's dense physical
    // slab IDs.  Never let a process-wide cache counter size module_states.
    script->module_state_id = runtime->next_module_state_id++;
    runtime_module_unit_index_put(runtime, script);
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
    runtime_loaded_script_put(runtime, script);
}

// runtime::type_list can alias a Script's Input-owned list while a nested
// Lambda package is evaluated. The Script remains the owner of that alias.
bool runtime_type_list_is_script_owned(Runtime* runtime) {
    if (!runtime || !runtime_type_list(runtime) || !runtime->scripts) return false;
    for (int i = 0; i < runtime->scripts->length; i++) {
        Script* script = (Script*)runtime->scripts->data[i];
        if (script && script->type_list == runtime_type_list(runtime)) return true;
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
        // Satellites compile a dependent script from its retained AST, which
        // may reference Type descriptors owned by an imported script. Drain
        // every worker before any dependency pool is released (D8.5.1v6).
        for (int i = 0; i < runtime->scripts->length; i++) {
            interp_satellite_cancel_script((Script*)runtime->scripts->data[i]);
        }
        for (int i = 0; i < runtime->scripts->length; i++) {
            Script *script = (Script*)runtime->scripts->data[i];
            if (!script) continue;
            runtime_free_script(runtime, script, false);
            runtime->scripts->data[i] = NULL;
        }
        arraylist_free(runtime->scripts);
        runtime->scripts = NULL;
    }
    if (runtime->loaded_script_index) {
        RuntimeLoadedScriptIndex::destroy(runtime->loaded_script_index);
        runtime->loaded_script_index = NULL;
    }
    runtime_module_state_clear_unit_index(runtime);
}

void runtime_cleanup_ast_prebuild_worker(Runtime* runtime) {
    if (!runtime) return;
    // Cross-language prebuild imports can register namespace roots against
    // this worker even though it never owns an EvalContext.
    module_registry_cleanup_for_runtime(runtime);
    runtime_free_all_scripts(runtime);
}

void runtime_free_script(Runtime* runtime, Script* script, bool remove_index) {
    if (!script) return;
    runtime_module_unit_index_delete_script(runtime, script);
    if (remove_index && script->reference) {
        runtime_loaded_script_delete_instance(runtime, script);
    }
    // A queued satellite reads this Script's immutable AST. Retire it before
    // releasing an execution shell, then release every private MIR context
    // that was already published into its Function entries.
    interp_satellite_cancel_script(script);
    if (script->interp_satellite_images) {
        for (int index = 0; index < script->interp_satellite_images->length; index++) {
            interp_satellite_image_destroy((InterpSatelliteImage*)
                script->interp_satellite_images->data[index]);
        }
        arraylist_free(script->interp_satellite_images);
        script->interp_satellite_images = NULL;
    }
    if (script->cache_owned_template) {
        // D8.5.1v2: the cache entry retains its template lease until eviction.
        // Closing it here would evict the image and recursively destroy this
        // Script while runtime cleanup still owns its stack frame.
        return;
    }
    if (script->cache_scope) {
        input_script_cache_close_scope(script->cache_scope);
        script->cache_scope = NULL;
    }
    if (script->ast_overlay_strings) {
        for (int i = 0; i < script->ast_overlay_strings->length; i++) {
            mem_free(script->ast_overlay_strings->data[i]);
        }
        arraylist_free(script->ast_overlay_strings);
        script->ast_overlay_strings = NULL;
    }
    if (script->cache_template) {
        // A cached instance owns only its shell and path copies. AST pools,
        // compiler state, MIR code, and direct-import graph belong to the
        // immutable template retained by InputScriptCache.
        if (script->destroy_extension) {
            script->destroy_extension(script);
            script->destroy_extension = NULL;
        }
        if (script->ast_promotion_overlay) {
            for (int i = 0; i < script->ast_promotion_overlay->length; i++) {
                mem_free(script->ast_promotion_overlay->data[i]);
            }
            arraylist_free(script->ast_promotion_overlay);
            script->ast_promotion_overlay = NULL;
        }
        // A cached AST shell can compile its own satellite image. The
        // immutable template never owns that context, so release it with the
        // shell rather than leaking it past the execution lease.
        if (!script->cache_mir_artifact && script->jit_context) {
            jit_cleanup_mode(script->jit_context,
                script->mir_gen_initialized ? 1 : 0);
            script->jit_context = NULL;
        }
        if (script->reference) mem_free((void*)script->reference);
        if (script->directory) mem_free((void*)script->directory);
        mem_free(script);
        return;
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
    if (script->type_list) {
        // Script teardown owns this registry; clear the canonical owner's
        // alias before releasing it so a later Runtime cleanup cannot free it
        // twice (one owner now: the canonical EvalContext).
        if (runtime && runtime_type_list(runtime) == script->type_list) {
            runtime_set_type_list(runtime, NULL);
        }
    }
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
    if (script->cache_direct_imports) arraylist_free(script->cache_direct_imports);
    if (script->jit_context) {
        jit_cleanup_mode(script->jit_context, script->mir_gen_initialized ? 1 : 0);
    }
    // decimal context is shared global; cached/free paths only clear the borrowed pointer
    script->decimal_ctx = NULL;
    mem_free(script);
}

void runtime_destroy_cached_script_template(Script* script) {
    if (!script) return;
    // Artifact destruction runs only after InputScriptCache observed no active
    // lease. Clear the ownership marker so common Script teardown releases the
    // retained AST pool and sealed MIR context exactly once.
    script->cache_owned_template = false;
    runtime_free_script(NULL, script, false);
}

bool runtime_hold_js_module_mir_scope(Runtime* runtime, InputCacheScope* scope) {
    if (!runtime || !scope) return false;
    if (!runtime->js_module_mir_scopes) runtime->js_module_mir_scopes = arraylist_new(2);
    return runtime->js_module_mir_scopes && arraylist_append(
        runtime->js_module_mir_scopes, scope);
}

static void runtime_close_js_module_mir_scopes(Runtime* runtime) {
    if (!runtime || !runtime->js_module_mir_scopes) return;
    for (int i = 0; i < runtime->js_module_mir_scopes->length; i++) {
        InputCacheScope* scope = (InputCacheScope*)runtime->js_module_mir_scopes->data[i];
        input_script_cache_close_scope(scope);
    }
    arraylist_free(runtime->js_module_mir_scopes);
    runtime->js_module_mir_scopes = NULL;
}

void runtime_teardown_batch_scripts(Runtime* runtime) {
    if (!runtime || !runtime->scripts) return;
    for (int i = 0; i < runtime->scripts->length; i++) {
        Script* script = (Script*)runtime->scripts->data[i];
        if (!script) continue;
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

void runtime_log_script_load_summary(Runtime* runtime) {
    if (!runtime) return;
    int lookups = runtime->script_load_hits + runtime->script_load_misses;
    double hit_rate = lookups > 0 ? (100.0 * (double)runtime->script_load_hits / (double)lookups) : 0.0;
    size_t loaded = RuntimeLoadedScriptIndex::count(runtime->loaded_script_index);
    log_info("runtime-script-registry: summary loaded=%zu reuses=%d hit_rate=%.1f%% compiles=%d hits=%d misses=%d invalidations=%d artifacts_disabled=%d",
             loaded, runtime->script_load_hits, hit_rate,
             runtime->script_load_compiles, runtime->script_load_hits,
             runtime->script_load_misses, runtime->script_load_invalidations,
             runtime->mir_cache_disabled ? 1 : 0);
    (void)loaded;
    (void)hit_rate;
}

// Reset the retained heap and name_pool on a Runtime.
// Used between independent evaluations (e.g. test-batch) so that each
// script starts with a clean GC heap.  The next runner_setup_context()
// call will create fresh heap/name_pool state and store it back.
static void runtime_quiesce_satellite_workers(Runtime* runtime);

void runtime_reset_heap(Runtime* runtime) {
    if (!runtime) return;
    // A satellite still lowering the finished script reads types and names
    // this reset frees; retire and await it first, as runtime_cleanup does.
    // test-batch resets the heap before it frees the scripts, and a worker
    // racing that reset crashed the next script's run.
    runtime_quiesce_satellite_workers(runtime);
    if (runtime_heap(runtime)) {
        EvalContext* cleanup_context = runtime_get_eval_context(runtime);
        if (!cleanup_context) return;
        if (!runtime_context_bind_retained(runtime, cleanup_context)) return;
        cleanup_context->result = ItemNull;
        cleanup_context->scheduler = runtime_scheduler(runtime);
        if (js_runtime_state_for(cleanup_context) &&
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

        // Any context that has a JS realm owns caches (constructors, intrinsic
        // prototypes, module namespaces) built from this heap's pool. Reset
        // them before heap destruction so a later batch script cannot
        // dereference stale Promise/module state from the preceding script.
        // `js_runtime_used` is the cross-language membrane flag, not "JS ran":
        // gating on it left a plain JS batch's constructor cache dangling, and
        // the next js_get_constructor faulted on freed pool memory.
        if (js_runtime_state_for(cleanup_context)) {
            js_batch_reset();
        } else {
            // Lambda DOM imports also allocate Radiant Velmt/VArray wrappers
            // from this heap. Retire their weak cache slots before replacing
            // the heap even when no JavaScript source ran (D7.4.5v2).
            dom_batch_reset();
        }

        if (runtime_scheduler(runtime)) {
            lambda_scheduler_destroy(runtime_scheduler(runtime));
            runtime_set_scheduler(runtime, NULL);
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
        jube_notify_heap_cleanup(runtime_heap(runtime));
        // Module bindings and ICs are context-owned slabs.  Drop their precise
        // root registrations and bulk-clear them while the old heap is still
        // current; the next module instantiation re-registers once.
        lambda_module_state_reset();

        if (runtime_type_list(runtime)) {
            // a nested package may publish its Script-owned type list through
            // the runtime context; let runtime_free_script release that owner.
            bool script_owned = runtime_type_list_is_script_owned(runtime);
            if (!script_owned) arraylist_free(runtime_type_list(runtime));
            runtime_set_type_list(runtime, NULL);
        }

        js_runtime_state_release_heap_resources();
        heap_destroy();
        runtime_set_heap(runtime, NULL);
        cleanup_context->heap = NULL;
        // D4.2.1v2/RN-NamePool: GC finalizers may still inspect NameRecords;
        // release the dedicated runtime pool only after heap destruction.
        if (runtime_name_pool(runtime)) {
            name_pool_release(runtime_name_pool(runtime));
            runtime_set_name_pool(runtime, NULL);
        }
        cleanup_context->name_pool = NULL;
        cleanup_context->type_list = NULL;
        cleanup_context->scheduler = NULL;
    }
}

void runtime_request_satellite_cancel(Runtime* runtime) {
    if (!runtime || !runtime->scripts) return;
    for (int i = 0; i < runtime->scripts->length; i++) {
        interp_satellite_request_cancel_script((Script*)runtime->scripts->data[i]);
    }
}

// Satellite workers lower against a Runtime-owned script shell and shared JIT
// services. Wait only after the close request has prevented further work.
static void runtime_quiesce_satellite_workers(Runtime* runtime) {
    if (!runtime || !runtime->scripts) return;
    runtime_request_satellite_cancel(runtime);
    for (int i = 0; i < runtime->scripts->length; i++) {
        interp_satellite_cancel_script((Script*)runtime->scripts->data[i]);
    }
}

void runtime_cleanup(Runtime* runtime) {
    if (!runtime) return;
    runtime_quiesce_satellite_workers(runtime);
    // PTH44v2/SO20: the document context and its node table live for the
    // evaluation. Their entries point into the heap this teardown destroys, so
    // they must go with it or the next run would read freed nodes.
    doc_context_reset();
    write_set_reset();
    EvalContext* cleanup_owner = runtime->eval_context;
    if (cleanup_owner) {
        if (!eval_context_init(cleanup_owner)) return;
        if (js_runtime_state_for(cleanup_owner) &&
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
    if (runtime_heap(runtime)) {
        EvalContext* cleanup_context = cleanup_owner
            ? cleanup_owner : runtime_get_eval_context(runtime);
        if (!cleanup_context) return;
        if (!runtime_context_bind_retained(runtime, cleanup_context)) return;
        cleanup_context->result = ItemNull;
        if (js_runtime_state_for(cleanup_context) &&
                !js_runtime_state_init(cleanup_context)) return;

        // Destruction follows the same owner-bound path as heap replacement.
        edit_bridge_destroy();
        render_map_destroy();
        tmpl_state_destroy();

        if (js_runtime_state_for(cleanup_context)) {
            // Cancel host tasks while their roots and native owners are still
            // valid; scheduler teardown only drains their inert completions.
            runtime_resource_table_clear(js_runtime_resource_table());
        }
        if (runtime_scheduler(runtime)) {
            cleanup_context->scheduler = runtime_scheduler(runtime);
            lambda_scheduler_destroy(runtime_scheduler(runtime));
            runtime_set_scheduler(runtime, NULL);
        }

        if (js_runtime_state_for(cleanup_context)) js_event_loop_shutdown();
        lambda_uv_cleanup();
        event_loop_cleaned = true;

        dom_shutdown();
        if (runtime->dom_doc) {
            free_document((DomDocument*)runtime->dom_doc);
            runtime->dom_doc = NULL;
        }
        runtime->dom_ui_context = NULL;

        // Intrinsic cache entries own native precise-root slots outside the GC
        // pool; release them while their heap is current and before leak accounting.
        if (js_runtime_state_for(cleanup_context)) js_intrinsic_state_teardown();

        // Jube module globals and interface records are process-global, but
        // their namespace, prototype, and method roots belong to this heap.
        // Reset both before this Runtime is retired.
        jube_modules_runtime_reset();
        jube_interface_runtime_reset();

        // Jube modules may cache heap-owned callbacks across repeated page
        // interactions; release those roots before this heap disappears.
        jube_notify_heap_cleanup(runtime_heap(runtime));

        print_heap_entries();
        check_memory_leak();

        if (runtime_type_list(runtime)) {
            // a nested package may publish its Script-owned type list through
            // the runtime context; let runtime_free_script release that owner.
            bool script_owned = runtime_type_list_is_script_owned(runtime);
            if (!script_owned) arraylist_free(runtime_type_list(runtime));
            runtime_set_type_list(runtime, NULL);
        }

        js_runtime_state_release_heap_resources();
        if (js_runtime_state_for(cleanup_context)) {
            // Full JS capsule destruction can release function-owned module
            // bindings, so keep both the JS realm and its slabs alive until it
            // has completed while the owning heap is still valid.
            if (!js_runtime_state_thread_matches(cleanup_context)) return;
            // One-shot teardown has no later heap calls; finish eval-generated
            // MIR while its deferred-context list still belongs to this realm.
            jm_cleanup_deferred_mir();
            js_runtime_state_destroy_context();
        }
        // DOM and JS cleanup can dispose callbacks that still activate their
        // defining module slab; destroy those slabs only after that cleanup.
        lambda_module_state_destroy();
        heap_destroy();
        runtime_set_heap(runtime, NULL);
        cleanup_context->heap = NULL;
        // D4.2.1v2/RN-NamePool: GC finalizers can traverse name-backed
        // shapes, so the dedicated runtime pool outlives heap teardown.
        if (runtime_name_pool(runtime)) {
            name_pool_release(runtime_name_pool(runtime));
            runtime_set_name_pool(runtime, NULL);
        }
        cleanup_context->name_pool = NULL;
        cleanup_context->type_list = NULL;
        cleanup_context->scheduler = NULL;
    } else {
        dom_shutdown();
        if (runtime->dom_doc) {
            free_document((DomDocument*)runtime->dom_doc);
            runtime->dom_doc = NULL;
        }
        runtime->dom_ui_context = NULL;
    }
    if (!event_loop_cleaned) {
        if (runtime->eval_context && js_runtime_state_for(runtime->eval_context)) {
            if (!js_runtime_state_init(runtime->eval_context)) return;
            js_event_loop_shutdown();
        }
        lambda_uv_cleanup();
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
    runtime_close_js_module_mir_scopes(runtime);
    runtime_free_all_scripts(runtime);
}
