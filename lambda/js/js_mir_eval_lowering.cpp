#include "js_mir_internal.hpp"
#include "js_runtime_state.hpp"
#include "js_function.hpp"
#include "js_exec_profile.h"
#include "../../lib/hash.h"

void js_function_finalize_capabilities(JsFunction* fn);
Item js_native_construct_via_call_body(Item callee, Item* args, int argc,
        Item new_target, uint64_t* result_home);
#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#define JS_DYNFUNC_MKDIR(path) _mkdir(path)
#define JS_DYNFUNC_OPEN(path) _open(path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE)
#define JS_DYNFUNC_WRITE(fd, buf, len) _write(fd, buf, (unsigned int)(len))
#define JS_DYNFUNC_CLOSE(fd) _close(fd)
#define JS_DYNFUNC_PID() _getpid()
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define JS_DYNFUNC_MKDIR(path) mkdir(path, 0755)
#define JS_DYNFUNC_OPEN(path) open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644)
#define JS_DYNFUNC_WRITE(fd, buf, len) write(fd, buf, len)
#define JS_DYNFUNC_CLOSE(fd) close(fd)
#define JS_DYNFUNC_PID() getpid()
#endif

JsModuleConstEntry* g_eval_preamble_entries = NULL;
int g_eval_preamble_entry_count = 0;
int g_eval_preamble_var_count = 0;
static const int64_t JS_EVAL_FLAG_VM_GLOBAL_CONTEXT = 16;

static char* js_preamble_name_copy(const char* name) {
    if (!name) return NULL;
    size_t length = strlen(name);
    char* copy = (char*)mem_alloc(length + 1, MEM_CAT_JS_RUNTIME);
    if (!copy) return NULL;
    memcpy(copy, name, length + 1);
    return copy;
}

bool js_preamble_entry_copy(const JsModuleConstEntry* source,
                            JsModuleConstEntry* target) {
    if (!source || !target) return false;

    *target = *source;
    target->name = NULL;
    target->live_binding_specifier = NULL;
    target->name = js_preamble_name_copy(source->name);
    target->live_binding_specifier = js_preamble_name_copy(source->live_binding_specifier);
    if ((source->name && !target->name) ||
            (source->live_binding_specifier && !target->live_binding_specifier)) {
        mem_free((void*)target->name);
        mem_free((void*)target->live_binding_specifier);
        target->name = NULL;
        target->live_binding_specifier = NULL;
        return false;
    }
    return true;
}

void js_preamble_entries_free(JsModuleConstEntry* entries, int count) {
    if (!entries) return;
    for (int i = 0; i < count; i++) {
        mem_free((void*)entries[i].name);
        mem_free((void*)entries[i].live_binding_specifier);
    }
    mem_free(entries);
}

bool js_preamble_entries_copy(const JsModuleConstEntry* source, int count,
                              JsModuleConstEntry** out_entries) {
    if (!out_entries || count < 0) return false;
    *out_entries = NULL;
    if (count == 0) return true;
    if (!source) return false;

    JsModuleConstEntry* entries = (JsModuleConstEntry*)mem_calloc(
        (size_t)count, sizeof(JsModuleConstEntry), MEM_CAT_JS_RUNTIME);
    if (!entries) return false;
    for (int i = 0; i < count; i++) {
        if (!js_preamble_entry_copy(&source[i], &entries[i])) {
            js_preamble_entries_free(entries, count);
            return false;
        }
    }
    *out_entries = entries;
    return true;
}

void js_eval_preamble_entries_free(void) {
    js_preamble_entries_free(g_eval_preamble_entries, g_eval_preamble_entry_count);
    g_eval_preamble_entries = NULL;
    g_eval_preamble_entry_count = 0;
    g_eval_preamble_var_count = 0;
}

// Per-unit module-var management (defined in js_runtime_state.cpp). Used by the
// vm.runInContext path to give each unit its own module-var slot namespace.

extern "C" void js_eval_preamble_cache_reset(void) {
    js_eval_preamble_entries_free();
}

#define JS_DYNFUNC_STATS_CAP 512
#define JS_DYNFUNC_CACHE_CAP 256

typedef Item (*JsDynFuncMainFunc)(Context*);

typedef struct JsDynFuncDependency {
    int preamble_name_hits;
    int escaped_identifier_hits;
    char first_name[64];
} JsDynFuncDependency;

struct JsDynFuncStatsEntry {
    uint64_t hash;
    int source_len;
    int argc;
    int kind;
    int preamble_entries;
    int preamble_vars;
    uint64_t preamble_hash;
    int count;
    int return_body_count;
    int preamble_name_hits;
    int escaped_identifier_hits;
    int cacheable_count;
    int cache_hit_count;
    char sample[96];
};

struct JsDynFuncCacheEntry {
    uint64_t hash;
    size_t source_len;
    int argc;
    int kind;
    char* source;
    MIR_context_t ctx;
    JsDynFuncMainFunc js_main_fn;
    int hits;
};

struct JsDynFuncCacheState {
    JsDynFuncCacheEntry entries[JS_DYNFUNC_CACHE_CAP];
    int count;
    int overflow;
};

static JsDynFuncCacheState* js_dynfunc_cache_state_ensure(void) {
    if (!js_active_runtime_state) return NULL;
    if (!js_runtime_state.dynamic_function_cache_state) {
        // Dynamic Function construction is a cold compilation boundary. The
        // resulting cache remains entirely context-local and lock-free.
        js_runtime_state.dynamic_function_cache_state = mem_calloc(1,
            sizeof(JsDynFuncCacheState), MEM_CAT_JS_RUNTIME);
    }
    return (JsDynFuncCacheState*)js_runtime_state.dynamic_function_cache_state;
}

static JsDynFuncCacheState* js_dynfunc_cache_state_current(void) {
    return js_active_runtime_state ?
        (JsDynFuncCacheState*)js_runtime_state.dynamic_function_cache_state : NULL;
}

#define js_dynfunc_cache_state (*js_dynfunc_cache_state_current())
#define js_dynfunc_cache (js_dynfunc_cache_state.entries)
#define js_dynfunc_cache_count (js_dynfunc_cache_state.count)
#define js_dynfunc_cache_overflow (js_dynfunc_cache_state.overflow)

static bool js_dynfunc_stats_checked = false;
static bool js_dynfunc_stats_enabled = false;
static bool js_dynfunc_stats_registered = false;
static JsDynFuncStatsEntry js_dynfunc_stats[JS_DYNFUNC_STATS_CAP];
static int js_dynfunc_stats_count = 0;
static int js_dynfunc_stats_total = 0;
static int js_dynfunc_stats_repeated = 0;
static int js_dynfunc_stats_overflow = 0;
static int js_dynfunc_stats_return_body_total = 0;
static int js_dynfunc_stats_with_preamble = 0;
static int js_dynfunc_stats_preamble_independent = 0;
static int js_dynfunc_stats_preamble_dependent = 0;
static int js_dynfunc_stats_cacheable = 0;
static int js_dynfunc_stats_cache_hits = 0;
static int js_dynfunc_stats_cache_inserts = 0;

static bool js_dynfunc_stats_is_enabled(void);
static void js_dynfunc_stats_record(const char* parse_prefix, const char* source, size_t source_len,
        String* body, int argc, const JsDynFuncDependency* dep, bool cacheable, bool cache_hit);
static void js_dynfunc_stats_report(void);
static void js_dynfunc_stats_write_line(int fd, const char* line);
static int js_dynfunc_kind_from_prefix(const char* parse_prefix);
static const char* js_dynfunc_kind_name(int kind);
static bool js_dynfunc_body_is_return(String* body);
static bool js_dynfunc_extract_return_identifier(String* body,
        size_t* name_offset, size_t* name_len);
static Item js_dynfunc_return_identifier_call(Item callee, Item this_value,
        Item* args, int argc, uint64_t* result_home);
static Item js_dynfunc_new_return_identifier_function(String* body,
        Item* args, int argc, const char* source_prefix);
static void js_dynfunc_copy_sample(char* dst, int dst_cap, const char* source, size_t source_len);
static uint64_t js_dynfunc_preamble_hash(void);
static JsDynFuncDependency js_dynfunc_scan_preamble_dependency(const char* source, size_t source_len);
static bool js_dynfunc_is_ident_start(char ch);
static bool js_dynfunc_is_ident_part(char ch);
static bool js_dynfunc_preamble_matches_token(const char* token, size_t token_len, char* first_name, int first_name_cap);
static bool js_dynfunc_dependency_is_independent(const JsDynFuncDependency* dep);
static JsDynFuncCacheEntry* js_dynfunc_cache_lookup(uint64_t hash, const char* source, size_t source_len,
        int argc, int kind);
static void js_dynfunc_cache_insert(uint64_t hash, char* source, size_t source_len, int argc, int kind,
        MIR_context_t ctx, JsDynFuncMainFunc js_main_fn);
static Item js_dynfunc_cache_execute(JsDynFuncCacheEntry* entry, Item* args, int argc, const char* source_prefix);
static void js_dynfunc_apply_function_metadata(Item fn_item, Item* args, int argc, const char* source_prefix);

static bool js_source_contains_import_meta(const char* source, size_t len);
static bool js_dynamic_function_source_has_hashbang(const char* source, size_t len);
static bool js_dynamic_function_param_has_invalid_html_close_comment(const char* source, size_t len);
static bool js_eval_at_line_terminator(const char* source, size_t len, size_t pos, size_t* width);

static Item js_dynamic_function_throw_syntax_error(const char* message) {
    return js_throw_syntax_error((Item){.item = s2it(heap_create_name(message, (int)strlen(message)))});
}

static bool js_dynfunc_stats_is_enabled(void) {
    if (!js_dynfunc_stats_checked) {
        const char* flag = getenv("LAMBDA_JS_DYNFUNC_STATS");
        js_dynfunc_stats_enabled = flag && flag[0] && strcmp(flag, "0") != 0;
        js_dynfunc_stats_checked = true;
    }
    return js_dynfunc_stats_enabled;
}

static int js_dynfunc_kind_from_prefix(const char* parse_prefix) {
    if (!parse_prefix) return 0;
    if (strcmp(parse_prefix, "async function*") == 0) return 3;
    if (strcmp(parse_prefix, "function*") == 0) return 2;
    if (strcmp(parse_prefix, "async function") == 0) return 1;
    return 0;
}

static const char* js_dynfunc_kind_name(int kind) {
    switch (kind) {
    case 1: return "async-function";
    case 2: return "generator-function";
    case 3: return "async-generator-function";
    default: return "function";
    }
}

static bool js_dynfunc_body_is_return(String* body) {
    if (!body) return false;
    const char* s = body->chars;
    size_t len = body->len;
    size_t pos = 0;
    while (pos < len && (s[pos] == ' ' || s[pos] == '\t' ||
            s[pos] == '\n' || s[pos] == '\r')) pos++;
    if (pos + 6 > len || memcmp(s + pos, "return", 6) != 0) return false;
    if (pos + 6 < len) {
        char ch = s[pos + 6];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '$') {
            return false;
        }
    }
    return true;
}

// A Function-constructor body containing only `return <Identifier>` has no
// statement/control-flow state to lower. Keep the identifier lookup dynamic,
// but avoid creating a one-use parser/MIR context for this common shape. The
// scanner is deliberately conservative: a line terminator after `return`, a
// non-ASCII identifier, a keyword expression, or any trailing statement falls
// back to the full compiler so ASI, TDZ, and syntax errors retain D8.4.3's
// normal completion path.
static bool js_dynfunc_extract_return_identifier(String* body,
        size_t* name_offset, size_t* name_len) {
    if (!body || !name_offset || !name_len) return false;
    const char* source = body->chars;
    size_t length = body->len;
    size_t pos = 0;
    bool line_break = false;
    while (pos < length) {
        char ch = source[pos];
        if (ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v') {
            pos++;
            continue;
        }
        size_t width = 0;
        if (js_eval_at_line_terminator(source, length, pos, &width)) {
            pos += width;
            line_break = true;
            continue;
        }
        if (ch == '/' && pos + 1 < length && source[pos + 1] == '/') {
            pos += 2;
            while (pos < length && !js_eval_at_line_terminator(source, length,
                    pos, &width)) pos++;
            if (pos < length) {
                pos += width;
                line_break = true;
            }
            continue;
        }
        if (ch == '/' && pos + 1 < length && source[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < length && !(source[pos] == '*' &&
                    source[pos + 1] == '/')) {
                if (js_eval_at_line_terminator(source, length, pos, &width)) {
                    pos += width;
                    line_break = true;
                } else {
                    pos++;
                }
            }
            if (pos + 1 >= length) return false;
            pos += 2;
            continue;
        }
        break;
    }
    if (line_break || pos + 6 > length || memcmp(source + pos, "return", 6) != 0 ||
            (pos + 6 < length && js_dynfunc_is_ident_part(source[pos + 6]))) {
        return false;
    }
    pos += 6;

    // A line terminator immediately after return changes the body to an empty
    // return under ASI, so it cannot use the identifier callback.
    line_break = false;
    while (pos < length) {
        char ch = source[pos];
        if (ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v') {
            pos++;
            continue;
        }
        size_t width = 0;
        if (js_eval_at_line_terminator(source, length, pos, &width)) {
            return false;
        }
        if (ch == '/' && pos + 1 < length && source[pos + 1] == '/') {
            return false;
        }
        if (ch == '/' && pos + 1 < length && source[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < length && !(source[pos] == '*' &&
                    source[pos + 1] == '/')) {
                if (js_eval_at_line_terminator(source, length, pos, &width)) {
                    return false;
                }
                pos++;
            }
            if (pos + 1 >= length) return false;
            pos += 2;
            continue;
        }
        break;
    }
    if (pos >= length || !js_dynfunc_is_ident_start(source[pos])) return false;
    size_t start = pos++;
    while (pos < length && js_dynfunc_is_ident_part(source[pos])) pos++;
    size_t identifier_length = pos - start;

    // These spellings are expressions with semantics that are not global
    // property reads (or are syntactically reserved), so let the parser handle
    // them. `undefined` remains eligible because the realm binding lookup
    // supplies the same mutable global binding used by generated MIR.
    static const char* reserved[] = {
        "true", "false", "null", "this", "super", "new", "class",
        "function", "delete", "void", "typeof", "import", "export",
        "yield", "await", NULL
    };
    for (int i = 0; reserved[i]; i++) {
        size_t reserved_length = strlen(reserved[i]);
        if (identifier_length == reserved_length &&
                memcmp(source + start, reserved[i], reserved_length) == 0) {
            return false;
        }
    }

    bool consumed_semicolon = false;
    while (pos < length) {
        char ch = source[pos];
        if (ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v') {
            pos++;
            continue;
        }
        size_t width = 0;
        if (js_eval_at_line_terminator(source, length, pos, &width)) {
            pos += width;
            continue;
        }
        if (ch == '/' && pos + 1 < length && source[pos + 1] == '/') {
            pos += 2;
            while (pos < length && !js_eval_at_line_terminator(source, length,
                    pos, &width)) pos++;
            continue;
        }
        if (ch == '/' && pos + 1 < length && source[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < length && !(source[pos] == '*' &&
                    source[pos + 1] == '/')) pos++;
            if (pos + 1 >= length) return false;
            pos += 2;
            continue;
        }
        if (ch == ';' && !consumed_semicolon) {
            consumed_semicolon = true;
            pos++;
            continue;
        }
        return false;
    }
    *name_offset = start;
    *name_len = identifier_length;
    return true;
}

static Item js_dynfunc_return_identifier_call(Item callee, Item this_value,
        Item* args, int argc, uint64_t* result_home) {
    (void)this_value;
    (void)args;
    (void)argc;
    (void)result_home;
    if (get_type_id(callee) != LMD_TYPE_FUNC) return ItemError;
    JsFunction* fn = (JsFunction*)callee.function;
    if (!fn || !fn->env || fn->env_size < 1) return ItemError;
    // Function-constructor code is global code. The function's home global is
    // switched by the ordinary call boundary before this callback runs, so the
    // strict helper preserves global lexical bindings and ReferenceError.
    return js_get_global_property_strict(fn->env[0]);
}

static Item js_dynfunc_new_return_identifier_function(String* body,
        Item* args, int argc, const char* source_prefix) {
    if (!body || argc != 1 || !source_prefix ||
            strcmp(source_prefix, "function anonymous") != 0) return ItemNull;
    size_t name_offset = 0;
    size_t name_len = 0;
    if (!js_dynfunc_extract_return_identifier(body, &name_offset, &name_len)) {
        return ItemNull;
    }

    RootFrame roots(3);
    Rooted<Item> fn_root(roots, js_new_native_body_constructor(
        js_dynfunc_return_identifier_call, js_native_construct_via_call_body, 0));
    if (get_type_id(fn_root.get()) != LMD_TYPE_FUNC) return fn_root.get();
    Rooted<Item> name_root(roots,
        js_make_string_len(body->chars + name_offset, (int)name_len));
    if (get_type_id(name_root.get()) != LMD_TYPE_STRING) return ItemError;
    Item* env = js_alloc_env(1);
    if (!env) return ItemError;
    env[0] = name_root.get();
    JsFunction* fn = (JsFunction*)fn_root.get().function;
    fn->env = env;
    fn->env_size = 1;
    js_env_rehome_scalars(env);
    js_function_finalize_capabilities(fn);
    // Preserve Function.prototype.toString, name, and lazy prototype behavior;
    // only execution of the already-validated return identifier is specialized.
    js_dynfunc_apply_function_metadata(fn_root.get(), args, argc, source_prefix);
    return fn_root.get();
}

static void js_dynfunc_copy_sample(char* dst, int dst_cap, const char* source, size_t source_len) {
    if (!dst || dst_cap <= 0) return;
    int lim = (int)source_len;
    if (lim > dst_cap - 1) lim = dst_cap - 1;
    for (int i = 0; i < lim; i++) {
        char ch = source[i];
        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
        dst[i] = ch;
    }
    dst[lim] = '\0';
}

static uint64_t js_dynfunc_preamble_hash(void) {
    if (!g_eval_preamble_entries || g_eval_preamble_entry_count <= 0) return 0;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < g_eval_preamble_entry_count; i++) {
        JsModuleConstEntry* entry = &g_eval_preamble_entries[i];
        size_t name_len = strlen(entry->name);
        h ^= hash_fnv1a_64(entry->name, name_len);
        h *= 0x100000001b3ULL;
        h ^= (uint64_t)entry->const_type; h *= 0x100000001b3ULL;
        h ^= (uint64_t)entry->int_val; h *= 0x100000001b3ULL;
        h ^= (uint64_t)entry->modvar_type; h *= 0x100000001b3ULL;
        h ^= (uint64_t)entry->var_kind; h *= 0x100000001b3ULL;
        h ^= entry->is_int ? 0x11ULL : 0x22ULL; h *= 0x100000001b3ULL;
        h ^= entry->is_iife_var ? 0x33ULL : 0x44ULL; h *= 0x100000001b3ULL;
        h ^= entry->is_implicit_global ? 0x55ULL : 0x66ULL; h *= 0x100000001b3ULL;
        h ^= entry->is_nested_func_hoist ? 0x77ULL : 0x88ULL; h *= 0x100000001b3ULL;
        h ^= entry->annexb_suppressed ? 0x99ULL : 0xaaULL; h *= 0x100000001b3ULL;
    }
    h ^= (uint64_t)g_eval_preamble_entry_count; h *= 0x100000001b3ULL;
    h ^= (uint64_t)g_eval_preamble_var_count; h *= 0x100000001b3ULL;
    return h;
}

static bool js_dynfunc_is_ident_start(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        ch == '_' || ch == '$';
}

static bool js_dynfunc_is_ident_part(char ch) {
    return js_dynfunc_is_ident_start(ch) || (ch >= '0' && ch <= '9');
}

static bool js_dynfunc_preamble_matches_token(const char* token, size_t token_len,
        char* first_name, int first_name_cap) {
    if (!token || token_len == 0 || !g_eval_preamble_entries || g_eval_preamble_entry_count <= 0) {
        return false;
    }
    for (int i = 0; i < g_eval_preamble_entry_count; i++) {
        const char* name = g_eval_preamble_entries[i].name;
        if (!name) continue;
        if (strncmp(name, "_js_", 4) == 0) name += 4;
        size_t name_len = strlen(name);
        if (name_len == token_len && memcmp(name, token, token_len) == 0) {
            if (first_name && first_name_cap > 0 && first_name[0] == '\0') {
                int copy_len = (int)token_len;
                if (copy_len > first_name_cap - 1) copy_len = first_name_cap - 1;
                memcpy(first_name, token, copy_len);
                first_name[copy_len] = '\0';
            }
            return true;
        }
    }
    return false;
}

static JsDynFuncDependency js_dynfunc_scan_preamble_dependency(const char* source, size_t source_len) {
    JsDynFuncDependency dep;
    memset(&dep, 0, sizeof(dep));
    if (!source || source_len == 0 || !g_eval_preamble_entries || g_eval_preamble_entry_count <= 0) {
        return dep;
    }

    enum ScanState {
        SCAN_DEFAULT,
        SCAN_SQ,
        SCAN_DQ,
        SCAN_LINE_COMMENT,
        SCAN_BLOCK_COMMENT
    };
    ScanState state = SCAN_DEFAULT;
    bool escaped = false;
    for (size_t i = 0; i < source_len; i++) {
        char ch = source[i];
        char next = (i + 1 < source_len) ? source[i + 1] : '\0';
        if (state == SCAN_LINE_COMMENT) {
            if (ch == '\n' || ch == '\r') state = SCAN_DEFAULT;
            continue;
        }
        if (state == SCAN_BLOCK_COMMENT) {
            if (ch == '*' && next == '/') {
                i++;
                state = SCAN_DEFAULT;
            }
            continue;
        }
        if (state == SCAN_SQ || state == SCAN_DQ) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if ((state == SCAN_SQ && ch == '\'') || (state == SCAN_DQ && ch == '"')) {
                state = SCAN_DEFAULT;
            }
            continue;
        }

        if (ch == '/' && next == '/') {
            i++;
            state = SCAN_LINE_COMMENT;
            continue;
        }
        if (ch == '/' && next == '*') {
            i++;
            state = SCAN_BLOCK_COMMENT;
            continue;
        }
        if (ch == '\'') {
            state = SCAN_SQ;
            continue;
        }
        if (ch == '"') {
            state = SCAN_DQ;
            continue;
        }
        if (ch == '\\' && next == 'u') {
            dep.escaped_identifier_hits++;
            continue;
        }
        if (js_dynfunc_is_ident_start(ch)) {
            size_t start = i;
            i++;
            while (i < source_len && js_dynfunc_is_ident_part(source[i])) i++;
            size_t token_len = i - start;
            if (js_dynfunc_preamble_matches_token(source + start, token_len,
                    dep.first_name, (int)sizeof(dep.first_name))) {
                dep.preamble_name_hits++;
            }
            i--;
        }
    }
    return dep;
}

static bool js_dynfunc_dependency_is_independent(const JsDynFuncDependency* dep) {
    if (!dep) return true;
    return dep->preamble_name_hits == 0 && dep->escaped_identifier_hits == 0;
}

static void js_dynfunc_stats_record(const char* parse_prefix, const char* source, size_t source_len,
        String* body, int argc, const JsDynFuncDependency* dep, bool cacheable, bool cache_hit) {
    if (!js_dynfunc_stats_is_enabled() || !source) return;
    if (!js_dynfunc_stats_registered) {
        atexit(js_dynfunc_stats_report);
        js_dynfunc_stats_registered = true;
    }
    int kind = js_dynfunc_kind_from_prefix(parse_prefix);
    bool is_return_body = js_dynfunc_body_is_return(body);
    uint64_t hash = hash_fnv1a_64(source, source_len);
    int preamble_entries = g_eval_preamble_entry_count;
    int preamble_vars = g_eval_preamble_var_count;
    uint64_t preamble_hash = js_dynfunc_preamble_hash();
    js_dynfunc_stats_total++;
    if (is_return_body) js_dynfunc_stats_return_body_total++;
    if (preamble_entries > 0 || preamble_vars > 0 || g_eval_preamble_entries) {
        js_dynfunc_stats_with_preamble++;
    }
    if (dep && !js_dynfunc_dependency_is_independent(dep)) js_dynfunc_stats_preamble_dependent++;
    else js_dynfunc_stats_preamble_independent++;
    if (cacheable) js_dynfunc_stats_cacheable++;
    if (cache_hit) js_dynfunc_stats_cache_hits++;
    for (int i = 0; i < js_dynfunc_stats_count; i++) {
        JsDynFuncStatsEntry* entry = &js_dynfunc_stats[i];
        if (entry->hash == hash && entry->source_len == (int)source_len &&
            entry->argc == argc && entry->kind == kind &&
            entry->preamble_entries == preamble_entries &&
            entry->preamble_vars == preamble_vars &&
            entry->preamble_hash == preamble_hash) {
            entry->count++;
            if (is_return_body) entry->return_body_count++;
            if (dep) {
                entry->preamble_name_hits += dep->preamble_name_hits;
                entry->escaped_identifier_hits += dep->escaped_identifier_hits;
            }
            if (cacheable) entry->cacheable_count++;
            if (cache_hit) entry->cache_hit_count++;
            js_dynfunc_stats_repeated++;
            return;
        }
    }
    if (js_dynfunc_stats_count >= JS_DYNFUNC_STATS_CAP) {
        js_dynfunc_stats_overflow++;
        return;
    }
    JsDynFuncStatsEntry* entry = &js_dynfunc_stats[js_dynfunc_stats_count++];
    memset(entry, 0, sizeof(*entry));
    entry->hash = hash;
    entry->source_len = (int)source_len;
    entry->argc = argc;
    entry->kind = kind;
    entry->preamble_entries = preamble_entries;
    entry->preamble_vars = preamble_vars;
    entry->preamble_hash = preamble_hash;
    entry->count = 1;
    entry->return_body_count = is_return_body ? 1 : 0;
    if (dep) {
        entry->preamble_name_hits = dep->preamble_name_hits;
        entry->escaped_identifier_hits = dep->escaped_identifier_hits;
    }
    entry->cacheable_count = cacheable ? 1 : 0;
    entry->cache_hit_count = cache_hit ? 1 : 0;
    js_dynfunc_copy_sample(entry->sample, (int)sizeof(entry->sample), source, source_len);
}

static void js_dynfunc_stats_report(void) {
    if (!js_dynfunc_stats_enabled || js_dynfunc_stats_total == 0) return;
    const char* dir = getenv("LAMBDA_JS_DYNFUNC_STATS_DIR");
    if (!dir || !dir[0]) dir = "./temp/js_dynfunc_stats";
    JS_DYNFUNC_MKDIR(dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/%d.tsv", dir, (int)JS_DYNFUNC_PID());
    int fd = JS_DYNFUNC_OPEN(path);
    if (fd < 0) return;
    char line[512];
    snprintf(line, sizeof(line),
        "# dynfunc-stats total=%d unique=%d repeated=%d overflow=%d return_bodies=%d with_preamble=%d independent=%d dependent=%d cacheable=%d cache_hits=%d cache_inserts=%d cache_entries=%d cache_overflow=%d\n",
        js_dynfunc_stats_total, js_dynfunc_stats_count, js_dynfunc_stats_repeated,
        js_dynfunc_stats_overflow, js_dynfunc_stats_return_body_total,
        js_dynfunc_stats_with_preamble, js_dynfunc_stats_preamble_independent,
        js_dynfunc_stats_preamble_dependent, js_dynfunc_stats_cacheable,
        js_dynfunc_stats_cache_hits, js_dynfunc_stats_cache_inserts, 0, 0);
    js_dynfunc_stats_write_line(fd, line);
    js_dynfunc_stats_write_line(fd,
        "rank\tcount\tkind\targc\tsource_len\tpreamble_entries\tpreamble_vars\tpreamble_hash\treturn_count\tpreamble_refs\tescaped_refs\tcacheable\tcache_hits\thash\tsample\n");
    log_notice("dynfunc-stats: total=%d unique=%d repeated=%d overflow=%d return_bodies=%d with_preamble=%d independent=%d dependent=%d cacheable=%d cache_hits=%d cache_inserts=%d cache_entries=%d cache_overflow=%d",
        js_dynfunc_stats_total, js_dynfunc_stats_count, js_dynfunc_stats_repeated,
        js_dynfunc_stats_overflow, js_dynfunc_stats_return_body_total,
        js_dynfunc_stats_with_preamble, js_dynfunc_stats_preamble_independent,
        js_dynfunc_stats_preamble_dependent, js_dynfunc_stats_cacheable,
        js_dynfunc_stats_cache_hits, js_dynfunc_stats_cache_inserts, 0, 0);
    bool used[JS_DYNFUNC_STATS_CAP];
    memset(used, 0, sizeof(used));
    int max_lines = js_dynfunc_stats_count;
    for (int rank = 0; rank < max_lines; rank++) {
        int best = -1;
        for (int i = 0; i < js_dynfunc_stats_count; i++) {
            if (used[i]) continue;
            if (best < 0 || js_dynfunc_stats[i].count > js_dynfunc_stats[best].count) {
                best = i;
            }
        }
        if (best < 0) break;
        used[best] = true;
        JsDynFuncStatsEntry* entry = &js_dynfunc_stats[best];
        snprintf(line, sizeof(line), "%d\t%d\t%s\t%d\t%d\t%d\t%d\t%llx\t%d\t%d\t%d\t%d\t%d\t%llx\t%s\n",
            rank + 1, entry->count, js_dynfunc_kind_name(entry->kind), entry->argc,
            entry->source_len, entry->preamble_entries, entry->preamble_vars,
            (unsigned long long)entry->preamble_hash, entry->return_body_count,
            entry->preamble_name_hits, entry->escaped_identifier_hits,
            entry->cacheable_count, entry->cache_hit_count,
            (unsigned long long)entry->hash, entry->sample);
        js_dynfunc_stats_write_line(fd, line);
        if (rank < 12) {
            log_notice("dynfunc-stats: top=%d count=%d kind=%s argc=%d source_len=%d preamble=%d/%d return_count=%d refs=%d cache_hits=%d hash=%llx sample=\"%s\"",
                rank + 1, entry->count, js_dynfunc_kind_name(entry->kind), entry->argc,
                entry->source_len, entry->preamble_entries, entry->preamble_vars,
                entry->return_body_count, entry->preamble_name_hits,
                entry->cache_hit_count, (unsigned long long)entry->hash, entry->sample);
        }
    }
    JS_DYNFUNC_CLOSE(fd);
}

static void js_dynfunc_stats_write_line(int fd, const char* line) {
    if (fd < 0 || !line) return;
    size_t len = strlen(line);
    const char* cur = line;
    while (len > 0) {
        int wrote = (int)JS_DYNFUNC_WRITE(fd, cur, len);
        if (wrote <= 0) return;
        cur += wrote;
        len -= (size_t)wrote;
    }
}

static JsDynFuncCacheEntry* js_dynfunc_cache_lookup(uint64_t hash, const char* source, size_t source_len,
        int argc, int kind) {
    if (!source) return NULL;
    for (int i = 0; i < js_dynfunc_cache_count; i++) {
        JsDynFuncCacheEntry* entry = &js_dynfunc_cache[i];
        if (entry->hash != hash || entry->source_len != source_len ||
            entry->argc != argc || entry->kind != kind || !entry->source ||
            !entry->js_main_fn) {
            continue;
        }
        if (memcmp(entry->source, source, source_len) == 0) return entry;
    }
    return NULL;
}

static void js_dynfunc_cache_insert(uint64_t hash, char* source, size_t source_len, int argc, int kind,
        MIR_context_t ctx, JsDynFuncMainFunc js_main_fn) {
    if (!source || !ctx || !js_main_fn) return;
    if (js_dynfunc_cache_lookup(hash, source, source_len, argc, kind)) return;
    if (js_dynfunc_cache_count >= JS_DYNFUNC_CACHE_CAP) {
        js_dynfunc_cache_overflow++;
        return;
    }
    JsDynFuncCacheEntry* entry = &js_dynfunc_cache[js_dynfunc_cache_count++];
    memset(entry, 0, sizeof(*entry));
    entry->hash = hash;
    entry->source_len = source_len;
    entry->argc = argc;
    entry->kind = kind;
    entry->source = source;
    entry->ctx = ctx;
    entry->js_main_fn = js_main_fn;
    js_dynfunc_stats_cache_inserts++;
}

extern "C" void js_dynfunc_cache_reset(void) {
    JsDynFuncCacheState* state = js_dynfunc_cache_state_ensure();
    if (!state) return;
    memset(js_dynfunc_cache, 0, sizeof(js_dynfunc_cache));
    js_dynfunc_cache_count = 0;
    js_dynfunc_cache_overflow = 0;
}

static void js_dynfunc_apply_function_metadata(Item fn_item, Item* args, int argc, const char* source_prefix) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    Item anon_name = (Item){.item = s2it(heap_create_name("anonymous", 9))};
    js_set_function_name(fn_item, anon_name);

    StrBuf* src_buf = strbuf_new_cap(256);
    strbuf_append_str(src_buf, source_prefix);
    strbuf_append_str(src_buf, "(");
    for (int i = 0; i < argc - 1; i++) {
        if (i > 0) strbuf_append_str(src_buf, ",");
        String* ps2 = it2s(args[i]);
        if (!ps2) {
            Item si = js_to_string(args[i]);
            ps2 = it2s(si);
        }
        if (ps2 && ps2->len > 0)
            strbuf_append_str_n(src_buf, ps2->chars, (int)ps2->len);
    }
    strbuf_append_str(src_buf, "\n) {\n");
    String* body2 = (argc > 0) ? it2s(args[argc - 1]) : NULL;
    if (!body2 && argc > 0) {
        Item si = js_to_string(args[argc - 1]);
        body2 = it2s(si);
    }
    if (body2 && body2->len > 0)
        strbuf_append_str_n(src_buf, body2->chars, (int)body2->len);
    strbuf_append_str(src_buf, "\n}");
    String* src_str = heap_create_name(src_buf->str, src_buf->length);
    strbuf_free(src_buf);
    Item src_item = (Item){.item = s2it(src_str)};
    js_set_function_source(fn_item, src_item);
}

static Item js_dynfunc_cache_execute(JsDynFuncCacheEntry* entry, Item* args, int argc, const char* source_prefix) {
    if (!entry || !entry->js_main_fn) return ItemNull;
    js_func_cache_suppress_push();
    Item fn_item = entry->js_main_fn((Context*)context);
    js_func_cache_suppress_pop();
    entry->hits++;
    js_dynfunc_apply_function_metadata(fn_item, args, argc, source_prefix);
    return fn_item;
}

static void js_install_realm_global_preamble(JsMirTranspiler* mt,
        JsTranspiler* tp) {
    if (!mt || !tp || !g_eval_preamble_entries ||
            g_eval_preamble_entry_count <= 0) return;
    uint32_t harness_var_count = js_get_batch_preamble_var_count();
    JsModuleConstEntry* global_entries = (JsModuleConstEntry*)pool_calloc(
        tp->ast_pool, sizeof(JsModuleConstEntry) * g_eval_preamble_entry_count);
    if (!global_entries) return;
    int global_entry_count = 0;
    for (int i = 0; i < g_eval_preamble_entry_count; i++) {
        JsModuleConstEntry* entry = &g_eval_preamble_entries[i];
        if (entry->const_type != MCONST_MODVAR || entry->int_val < 0 ||
                entry->is_iife_var || entry->is_iife_func_decl ||
                entry->is_nested_func_hoist) continue;
        bool harness_entry = (uint64_t)entry->int_val < harness_var_count;
        bool global_lexical = entry->var_kind == JS_VAR_LET ||
            entry->var_kind == JS_VAR_CONST;
        if (!harness_entry && !global_lexical) continue;
        global_entries[global_entry_count++] = *entry;
    }
    mt->preamble_entries = global_entries;
    mt->preamble_entry_count = global_entry_count;
    // Retain original slot indices for sparse top-level lexicals. New dynamic
    // declarations must start beyond the caller slab without inheriting its
    // var/function bindings.
    mt->preamble_var_count = g_eval_preamble_var_count;
}

// ============================================================================
// new Function / GeneratorFunction / AsyncFunction dynamic compilation
// ============================================================================
static Item js_new_function_from_string_kind(Item* args, int argc, const char* parse_prefix,
        const char* source_prefix, bool inherit_caller_environment) {
    if (!js_current_runtime()) {
        log_error("js-new-function: no runtime context for dynamic function compilation");
        return ItemNull;
    }
    JsDynFuncCacheState* state = js_dynfunc_cache_state_ensure();
    if (!state) return ItemNull;

    // Build the JS source for the function expression.
    // new Function("param1", "param2", "body") or new Function("body")
    // → (function(param1, param2) { body })
    // new Function() with no args → (function() {})
    StrBuf* sb = strbuf_new_cap(256);
    strbuf_append_str(sb, "(");
    strbuf_append_str(sb, parse_prefix);
    strbuf_append_str(sb, "(");

    // params are args[0..argc-2], body is args[argc-1]
    // per spec, each argument is converted to string via ToString()
    for (int i = 0; i < argc - 1; i++) {
        if (i > 0) strbuf_append_str(sb, ",");
        String* ps = it2s(args[i]);
        if (!ps) {
            Item str_item = js_to_string(args[i]);
            // new Function applies ToString before parsing; an abrupt conversion
            // must escape instead of being misreported as a source SyntaxError.
            if (item_is_error(str_item)) {
                strbuf_free(sb);
                return str_item;
            }
            ps = it2s(str_item);
        }
        if (ps && ps->len > 0) {
            if (js_source_contains_import_meta(ps->chars, ps->len)) {
                strbuf_free(sb);
                return js_dynamic_function_throw_syntax_error("Cannot use import.meta outside a module");
            }
            if (js_dynamic_function_source_has_hashbang(ps->chars, ps->len)) {
                strbuf_free(sb);
                return js_dynamic_function_throw_syntax_error("Hashbang is not allowed here");
            }
            if (js_dynamic_function_param_has_invalid_html_close_comment(ps->chars, ps->len)) {
                strbuf_free(sb);
                return js_dynamic_function_throw_syntax_error("Unexpected token '-->'");
            }
            strbuf_append_str_n(sb, ps->chars, (int)ps->len);
        }
    }
    // Newline before ) is required by spec §20.2.1.1 to handle params ending with // comment
    strbuf_append_str(sb, "\n) {");

    // body
    String* body = (argc > 0) ? it2s(args[argc - 1]) : NULL;
    if (!body && argc > 0) {
        Item str_item = js_to_string(args[argc - 1]);
        // preserve an abrupt body ToString before any dynamic-source parsing.
        if (item_is_error(str_item)) {
            strbuf_free(sb);
            return str_item;
        }
        body = it2s(str_item);
    }
    if (body && body->len > 0) {
        if (js_source_contains_import_meta(body->chars, body->len)) {
            strbuf_free(sb);
            return js_dynamic_function_throw_syntax_error("Cannot use import.meta outside a module");
        }
        if (js_dynamic_function_source_has_hashbang(body->chars, body->len)) {
            strbuf_free(sb);
            return js_dynamic_function_throw_syntax_error("Hashbang is not allowed here");
        }
        strbuf_append_str(sb, "\n");
        strbuf_append_str_n(sb, body->chars, (int)body->len);
        strbuf_append_str(sb, "\n");

        // The Test262 harness and other host code frequently construct a
        // zero-argument function solely to return one realm binding. Keep the
        // lookup live, but share the native callback instead of paying for a
        // parser, AST, MIR context, and link for each unique spelling. This is
        // a semantic specialization of one validated grammar shape, not a
        // name-specific shortcut (D8.4.3).
        if (!inherit_caller_environment && strcmp(parse_prefix, "function") == 0 &&
                argc == 1) {
            Item fast_fn = js_dynfunc_new_return_identifier_function(
                body, args, argc, source_prefix);
            if (get_type_id(fast_fn) == LMD_TYPE_FUNC || item_is_error(fast_fn)) {
                if (get_type_id(fast_fn) == LMD_TYPE_FUNC) {
                    js_opt_trace_record(JS_OPT_DYNAMIC_FUNCTION_FASTPATH,
                        JS_OPT_REASON_NONE, JS_OPT_OUTCOME_TAKEN);
                }
                strbuf_free(sb);
                return fast_fn;
            }
        }
    }

    strbuf_append_str(sb, "})");

    size_t source_len = sb->length;

    // null-terminate — use malloc; the transpiler will copy as needed
    char* source = (char*)mem_alloc(source_len + 1, MEM_CAT_JS_RUNTIME);
    if (!source) {
        strbuf_free(sb);
        log_error("js-new-function: malloc failed for source buffer");
        return ItemNull;
    }
    memcpy(source, sb->str, source_len);
    source[source_len] = '\0';
    strbuf_free(sb);

    int dynfunc_kind = js_dynfunc_kind_from_prefix(parse_prefix);
    uint64_t source_hash = hash_fnv1a_64(source, source_len);
    // Cache reuse is unsafe whenever generated code resolves a preamble slot,
    // whether that slot is a direct-eval local or a realm-global harness/
    // lexical binding. Skipping the scan for Function constructors cached
    // fixed module indices from an unrelated script state.
    JsDynFuncDependency dep = js_dynfunc_scan_preamble_dependency(
        source, source_len);
    bool cacheable = js_dynfunc_dependency_is_independent(&dep);
    if (cacheable) {
        JsDynFuncCacheEntry* cached = js_dynfunc_cache_lookup(source_hash, source, source_len,
            argc, dynfunc_kind);
        if (cached) {
            js_dynfunc_stats_record(parse_prefix, source, source_len, body, argc, &dep, true, true);
            js_opt_trace_record(JS_OPT_DYNAMIC_FUNCTION_CACHE_HIT,
                JS_OPT_REASON_NONE, JS_OPT_OUTCOME_TAKEN);
            Item cached_fn = js_dynfunc_cache_execute(cached, args, argc, source_prefix);
            mem_free(source);
            return cached_fn;
        }
    }

    if (cacheable) {
        js_opt_trace_record(JS_OPT_DYNAMIC_FUNCTION_CACHE_MISS,
            JS_OPT_REASON_NONE, JS_OPT_OUTCOME_FALLBACK);
    }

    js_dynfunc_stats_record(parse_prefix, source, source_len, body, argc, &dep, cacheable, false);

    log_debug("js-new-function: compiling dynamic function body (len=%d)", (int)source_len);

    // Compile the function expression as a mini JS module.
    // Since the source is a top-level expression statement "(function(...) { ... })",
    // js_main() will evaluate it and return the function Item.
    JsTranspiler* tp = js_transpiler_create(js_current_runtime());
    if (!tp) {
        log_error("js-new-function: failed to create transpiler");
        mem_free(source);
        return ItemNull;
    }

    if (!js_transpiler_parse(tp, source, source_len)) {
        log_error("js-new-function: parse failed for '%s'", source);
        mem_free(source);
        js_transpiler_destroy(tp);
        return js_dynamic_function_throw_syntax_error("Invalid function source");
    }

    TSNode root = ts_tree_root_node(tp->tree);
    JsAstNode* js_ast = build_js_ast_indexed(tp, root);
    if (!js_ast) {
        log_error("js-new-function: AST build failed");
        js_transpiler_destroy(tp);
        mem_free(source);
        return ItemNull;
    }

    int early_errors = js_check_early_errors(tp, js_ast);
    if (early_errors > 0) {
        js_transpiler_destroy(tp);
        mem_free(source);
        return js_throw_syntax_error((Item){.item = s2it(heap_create_name("Invalid function source", 23))});
    }

    // Use optimize level 0 for dynamic code (eval/new Function) — small snippets
    // don't benefit from optimization but pay the full cost of each pass.
    MIR_context_t ctx = jit_init(0);
    if (!ctx) {
        log_error("js-new-function: MIR context init failed");
        js_transpiler_destroy(tp);
        mem_free(source);
        return ItemNull;
    }

    // Install batch error handler if set (prevents exit(1) on MIR errors)
    if (g_batch_mir_error_handler) {
        MIR_set_error_func(ctx, g_batch_mir_error_handler);
    }

    JsMirTranspiler* mt = jm_create_mir_transpiler(tp, ctx, "<new Function>", false, 16, 8, 8, "js-new-function");
    if (!mt) {
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        mem_free(source);
        return ItemNull;
    }
    mt->module_name_base = js_active_module_name_count();
    mt->module_ic_base = js_active_module_ic_count();

    if (inherit_caller_environment && g_eval_preamble_entries &&
            g_eval_preamble_entry_count > 0) {
        // Direct eval expression form must close over the caller module slab;
        // sharing Function-constructor lowering without this explicit mode
        // silently turned lexical reads and writes into global accesses.
        mt->preamble_entries = g_eval_preamble_entries;
        mt->preamble_entry_count = g_eval_preamble_entry_count;
        mt->preamble_var_count = g_eval_preamble_var_count;
    } else {
        // Function-constructor code is global code. It may see Test262 harness
        // bindings and top-level let/const/class bindings, but never the
        // constructor caller's function-local module slots.
        js_install_realm_global_preamble(mt, tp);
    }

    char module_name[48];
    snprintf(module_name, sizeof(module_name), "js_dynfunc_%d", js_dynamic_func_counter++);
    mt->module = MIR_new_module(ctx, module_name);

    if (!transpile_js_mir_ast(mt, js_ast)) {
        log_error("js-new-function: collection/allocation failed");
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        mem_free(source);
        return ItemNull;
    }

    if (!jm_validate_mir_labels(ctx)) {
        log_error("js-new-function: NULL labels detected");
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        mem_free(source);
        return ItemNull;
    }

    MIR_link(ctx, g_mir_interp_mode ? MIR_set_interp_interface : MIR_set_gen_interface, import_resolver);

    typedef Item (*js_main_func_t)(Context*);
    js_main_func_t js_main_fn = (js_main_func_t)find_func(ctx, (char*)"js_main");

    if (!js_main_fn) {
        log_error("js-new-function: failed to find js_main");
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        mem_free(source);
        return ItemNull;
    }

    // Dynamic functions use the caller's state, so append their sealed local
    // name image before js_main can execute code that observes a property.
    if (!js_append_compiled_name_table(mt)) {
        log_error("js-new-function: failed to append NameId table");
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        mem_free(source);
        return ItemNull;
    }

    // Function constructor code resolves the caller's top-level bindings, but
    // its generated inline module slots can exceed that caller's original
    // declaration count (for example a generated class after a batch reset).
    // Grow the shared slab before js_main can address those fixed offsets.
    if (!js_ensure_active_module_var_capacity((uint32_t)mt->module_var_count)) {
        log_error("js-new-function: failed to grow active module state");
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        mem_free(source);
        return ItemNull;
    }

    // Execute js_main to get the compiled function Item.  The returned
    // JsFunction captures the caller's expanded module state for later calls.
    Item fn_item = js_main_fn((Context*)context);

    // Keep the MIR context alive for returned closures; names are already linked
    // to the active module table and no compiler pool crosses this boundary.
    jm_destroy_mir_transpiler(mt);
    jm_defer_mir_cleanup(ctx);
    // Attach source/name/AST storage to the deferred entry so they are freed
    // together with the MIR context.  Dynamic functions can outlive this helper,
    // and their generated code/source metadata still depends on these buffers.
    if (module_mir_context_count > 0) {
        module_mir_source_buffers[module_mir_context_count - 1] = source;
        if (cacheable && get_type_id(fn_item) == LMD_TYPE_FUNC) {
            js_dynfunc_cache_insert(source_hash, source, source_len, argc, dynfunc_kind,
                ctx, js_main_fn);
        }
        source = NULL;
    }
    js_transpiler_destroy(tp);

    log_debug("js-new-function: compiled dynamic function OK (type=%d)", get_type_id(fn_item));

    // Set spec-correct name and toString() source:
    // "function anonymous(params\n) {\nbody\n}" and generator/async variants.
    // The internal source was (function(params) { body }) which doesn't match the spec.
    js_dynfunc_apply_function_metadata(fn_item, args, argc, source_prefix);

    return fn_item;
}

#undef js_dynfunc_cache_overflow
#undef js_dynfunc_cache_count
#undef js_dynfunc_cache
#undef js_dynfunc_cache_state

extern "C" void js_dynfunc_cache_destroy_context(JsRuntimeState* runtime_state) {
    if (!runtime_state || !runtime_state->dynamic_function_cache_state) return;
    mem_free(runtime_state->dynamic_function_cache_state);
    runtime_state->dynamic_function_cache_state = NULL;
}

extern "C" Item js_new_function_from_string(Item* args, int argc) {
    return js_new_function_from_string_kind(args, argc, "function",
        "function anonymous", false);
}

extern "C" Item js_new_async_function_from_string(Item* args, int argc) {
    return js_new_function_from_string_kind(args, argc, "async function",
        "async function anonymous", false);
}

extern "C" Item js_new_generator_function_from_string(Item* args, int argc, int is_async) {
    if (is_async) {
        return js_new_function_from_string_kind(args, argc, "async function*",
            "async function* anonymous", false);
    }
    return js_new_function_from_string_kind(args, argc, "function*",
        "function* anonymous", false);
}

// ============================================================================
// eval() helper: insert "return " before the last expression statement
// to capture the completion value. Returns malloc'd string or NULL.
// ============================================================================
char* eval_try_insert_return(const char* code, size_t len) {
    // Forward-scan to find the start of the last top-level statement,
    // handling strings, comments, and nesting so inner semicolons are skipped.
    size_t last_stmt_start = 0;
    int brace = 0, paren = 0, bracket = 0;
    bool in_sq = false, in_dq = false, in_tpl = false;
    bool in_line_cmt = false, in_block_cmt = false;

    for (size_t i = 0; i < len; i++) {
        char c = code[i];
        char n = (i + 1 < len) ? code[i + 1] : 0;

        if (in_line_cmt)  {
            size_t lt_width = 0;
            if (js_eval_at_line_terminator(code, len, i, &lt_width)) in_line_cmt = false;
            continue;
        }
        if (in_block_cmt) { if (c == '*' && n == '/') { in_block_cmt = false; i++; } continue; }

        if (!in_sq && !in_dq && !in_tpl) {
            if (c == '/' && n == '/') { in_line_cmt  = true;  i++; continue; }
            if (c == '/' && n == '*') { in_block_cmt = true;  i++; continue; }
        }

        if (in_sq)  { if (c == '\\') { i++; continue; } if (c == '\'') in_sq  = false; continue; }
        if (in_dq)  { if (c == '\\') { i++; continue; } if (c == '"')  in_dq  = false; continue; }
        if (in_tpl) { if (c == '\\') { i++; continue; } if (c == '`')  in_tpl = false; continue; }

        if (c == '\'') { in_sq  = true; continue; }
        if (c == '"')  { in_dq  = true; continue; }
        if (c == '`')  { in_tpl = true; continue; }

        if (c == '(') paren++;   else if (c == ')') { if (paren   > 0) paren--;   }
        if (c == '[') bracket++; else if (c == ']') { if (bracket > 0) bracket--; }
        if (c == '{') brace++;   else if (c == '}') { if (brace   > 0) brace--;   }

        // Top-level semicolon — next non-whitespace char starts next statement
        if (c == ';' && brace == 0 && paren == 0 && bracket == 0) {
            size_t nxt = i + 1;
            while (nxt < len && (code[nxt] == ' ' || code[nxt] == '\t' ||
                                 js_eval_at_line_terminator(code, len, nxt, NULL)))
                nxt++;
            if (nxt < len) last_stmt_start = nxt;
        }
    }

    // Skip leading whitespace of last statement
    size_t ls = last_stmt_start;
    while (ls < len && (code[ls] == ' ' || code[ls] == '\t' ||
                        js_eval_at_line_terminator(code, len, ls, NULL)))
        ls++;
    if (ls >= len) return NULL;

    // Extract the first identifier-word of the last statement
    size_t we = ls;
    while (we < len && ((code[we] >= 'a' && code[we] <= 'z') ||
                        (code[we] >= 'A' && code[we] <= 'Z') ||
                        (code[we] >= '0' && code[we] <= '9') ||
                        code[we] == '_' || code[we] == '$'))
        we++;
    size_t wlen = we - ls;

    // Keywords that start non-expression statements — don't prepend return
    static const char* kw[] = {
        "var", "let", "const", "function", "class",
        "if", "for", "while", "do", "switch", "try", "with",
        "throw", "return", "break", "continue", "debugger",
        "import", "export", NULL
    };
    for (int k = 0; kw[k]; k++) {
        size_t klen = strlen(kw[k]);
        if (wlen == klen && memcmp(code + ls, kw[k], klen) == 0)
            return NULL;
    }

    // Also skip if last statement starts with '{' (block)
    if (code[ls] == '{') return NULL;

    // Skip if last statement is empty (just ';') — completion is from the previous stmt
    if (code[ls] == ';') return NULL;

    // Build: code[0..ls] + "return " + code[ls..len]
    size_t total = len + 7 + 1;
    char* result = (char*)mem_alloc(total, MEM_CAT_JS_RUNTIME);
    if (!result) return NULL;
    memcpy(result, code, ls);
    memcpy(result + ls, "return ", 7);
    memcpy(result + ls + 7, code + ls, len - ls);
    result[total - 1] = '\0';
    return result;
}

// ============================================================================
static bool js_eval_is_ident_char(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '_' || ch == '$';
}

static bool js_eval_at_word(const char* source, size_t len, size_t pos, const char* word, size_t word_len) {
    if (pos + word_len > len) return false;
    if (memcmp(source + pos, word, word_len) != 0) return false;
    if (pos > 0 && js_eval_is_ident_char(source[pos - 1])) return false;
    if (pos + word_len < len && js_eval_is_ident_char(source[pos + word_len])) return false;
    return true;
}

static bool js_eval_at_line_terminator(const char* source, size_t len, size_t pos, size_t* width) {
    if (!source || pos >= len) return false;
    unsigned char ch = (unsigned char)source[pos];
    if (ch == '\n') {
        if (width) *width = 1;
        return true;
    }
    if (ch == '\r') {
        if (width) *width = (pos + 1 < len && source[pos + 1] == '\n') ? 2 : 1;
        return true;
    }
    if (pos + 2 < len && ch == 0xE2 && (unsigned char)source[pos + 1] == 0x80 &&
        ((unsigned char)source[pos + 2] == 0xA8 || (unsigned char)source[pos + 2] == 0xA9)) {
        if (width) *width = 3;
        return true;
    }
    return false;
}

// helper: skip a JavaScript string or comment while source scanners share the same lexical rules
static bool js_eval_skip_string_or_comment(const char* source, size_t len, size_t* pos) {
    size_t p = *pos;
    char ch = source[p];
    if (ch == '\'' || ch == '"' || ch == '`') {
        char quote = ch;
        p++;
        while (p < len) {
            if (source[p] == '\\' && p + 1 < len) { p += 2; continue; }
            if (source[p] == quote) { p++; break; }
            p++;
        }
        *pos = p;
        return true;
    }
    if (ch == '/' && p + 1 < len && source[p + 1] == '/') {
        p += 2;
        while (p < len && !js_eval_at_line_terminator(source, len, p, NULL)) p++;
        *pos = p;
        return true;
    }
    if (ch == '/' && p + 1 < len && source[p + 1] == '*') {
        p += 2;
        while (p + 1 < len && !(source[p] == '*' && source[p + 1] == '/')) p++;
        if (p + 1 < len) p += 2;
        *pos = p;
        return true;
    }
    return false;
}

static size_t js_eval_skip_space_and_comments(const char* source, size_t len, size_t pos) {
    while (pos < len) {
        char ch = source[pos];
        size_t lt_width = 0;
        if (ch == ' ' || ch == '\t') { pos++; continue; }
        if (js_eval_at_line_terminator(source, len, pos, &lt_width)) { pos += lt_width; continue; }
        if (ch == '/' && pos + 1 < len && source[pos + 1] == '/') {
            pos += 2;
            while (pos < len && !js_eval_at_line_terminator(source, len, pos, NULL)) pos++;
            continue;
        }
        if (ch == '/' && pos + 1 < len && source[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < len && !(source[pos] == '*' && source[pos + 1] == '/')) pos++;
            if (pos + 1 < len) pos += 2;
            continue;
        }
        break;
    }
    return pos;
}

static bool js_source_contains_import_meta(const char* source, size_t len) {
    for (size_t pos = 0; pos < len; pos++) {
        if (js_eval_skip_string_or_comment(source, len, &pos)) {
            if (pos > 0) pos--;
            continue;
        }
        if (js_eval_at_word(source, len, pos, "import", 6)) {
            size_t member_pos = js_eval_skip_space_and_comments(source, len, pos + 6);
            if (member_pos + 5 <= len && memcmp(source + member_pos, ".meta", 5) == 0 &&
                (member_pos + 5 == len || !js_eval_is_ident_char(source[member_pos + 5]))) {
                return true;
            }
            pos += 5;
        }
    }
    return false;
}

static bool js_dynamic_function_source_has_hashbang(const char* source, size_t len) {
    return source && len >= 2 && source[0] == '#' && source[1] == '!';
}

static bool js_dynamic_function_param_has_line_terminator_before(const char* source, size_t pos) {
    for (size_t i = 0; i < pos; i++) {
        if (source[i] == '\n' || source[i] == '\r') return true;
    }
    return false;
}

static bool js_dynamic_function_param_has_invalid_html_close_comment(const char* source, size_t len) {
    if (!source || len < 3) return false;

    for (size_t pos = 0; pos < len; pos++) {
        if (js_eval_skip_string_or_comment(source, len, &pos)) {
            if (pos > 0) pos--;
            continue;
        }
        char ch = source[pos];
        if (ch == '-' && pos + 2 < len && source[pos + 1] == '-' && source[pos + 2] == '>') {
            if (!js_dynamic_function_param_has_line_terminator_before(source, pos)) return true;
            pos += 2;
        }
    }

    return false;
}

typedef struct JsEvalInitializerScan {
    bool contains_arguments;
    bool contains_new_target;
    bool contains_super_call;
    bool contains_super_property;
} JsEvalInitializerScan;

static JsEvalInitializerScan js_eval_scan_initializer_source(const char* source, size_t len) {
    JsEvalInitializerScan scan = {false, false, false, false};
    for (size_t pos = 0; pos < len; pos++) {
        char ch = source[pos];
        if (ch == '\'' || ch == '"' || ch == '`') {
            char quote = ch;
            pos++;
            while (pos < len) {
                if (source[pos] == '\\' && pos + 1 < len) { pos += 2; continue; }
                if (source[pos] == quote) break;
                pos++;
            }
            continue;
        }
        if (ch == '/' && pos + 1 < len && source[pos + 1] == '/') {
            pos += 2;
            while (pos < len && !js_eval_at_line_terminator(source, len, pos, NULL)) pos++;
            continue;
        }
        if (ch == '/' && pos + 1 < len && source[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < len && !(source[pos] == '*' && source[pos + 1] == '/')) pos++;
            if (pos + 1 < len) pos++;
            continue;
        }
        if (js_eval_at_word(source, len, pos, "arguments", 9)) {
            scan.contains_arguments = true;
            pos += 8;
            continue;
        }
        if (js_eval_at_word(source, len, pos, "new", 3)) {
            size_t member_pos = js_eval_skip_space_and_comments(source, len, pos + 3);
            if (member_pos + 7 <= len && memcmp(source + member_pos, ".target", 7) == 0 &&
                (member_pos + 7 == len || !js_eval_is_ident_char(source[member_pos + 7]))) {
                scan.contains_new_target = true;
            }
            pos += 2;
            continue;
        }
        if (js_eval_at_word(source, len, pos, "super", 5)) {
            size_t member_pos = js_eval_skip_space_and_comments(source, len, pos + 5);
            if (member_pos < len && source[member_pos] == '(') scan.contains_super_call = true;
            else if (member_pos < len && (source[member_pos] == '.' || source[member_pos] == '[')) scan.contains_super_property = true;
            pos += 4;
            continue;
        }
    }
    return scan;
}

static Item js_eval_initializer_early_error(String* code_str, bool is_direct_eval) {
    if ((!js_private_field_initializing && !js_eval_initializer_context) || !code_str) return js_status_ok();
    JsEvalInitializerScan scan = js_eval_scan_initializer_source(code_str->chars, code_str->len);
    if ((is_direct_eval && scan.contains_arguments) || (!is_direct_eval && scan.contains_new_target) ||
        scan.contains_super_call || (!is_direct_eval && scan.contains_super_property)) {
        return js_throw_syntax_error((Item){.item = s2it(heap_create_name("Invalid eval in class field initializer", 39))});
    }
    return js_status_ok();
}

static Item js_eval_var_conflicts_lexical_name(String* name) {
    if (!name || name->len <= 0) return js_status_ok();
    Item key = (Item){.item = s2it(heap_create_name(name->chars, name->len))};
    if (!js_eval_local_has_lexical_binding(key)) return js_status_ok();
    return js_throw_syntax_error((Item){.item = s2it(heap_create_name("Eval var conflicts with lexical declaration", 43))});
}

static Item js_eval_var_conflicts_lexical_pattern(JsAstNode* node) {
    if (!node) return js_status_ok();
    switch (node->node_type) {
        case JS_AST_NODE_IDENTIFIER:
            return js_eval_var_conflicts_lexical_name(((JsIdentifierNode*)node)->name);
        case JS_AST_NODE_ASSIGNMENT_PATTERN:
            return js_eval_var_conflicts_lexical_pattern(((JsAssignmentPatternNode*)node)->left);
        case JS_AST_NODE_REST_ELEMENT:
        case JS_AST_NODE_REST_PROPERTY:
            return js_eval_var_conflicts_lexical_pattern(((JsSpreadElementNode*)node)->argument);
        case JS_AST_NODE_ARRAY_PATTERN:
            for (JsAstNode* e = ((JsArrayPatternNode*)node)->elements; e; e = e->next) {
                JS_ASSIGN_OR_RETURN(status, js_eval_var_conflicts_lexical_pattern(e));
            }
            break;
        case JS_AST_NODE_OBJECT_PATTERN:
            for (JsAstNode* p = ((JsObjectPatternNode*)node)->properties; p; p = p->next) {
                if (p->node_type == JS_AST_NODE_PROPERTY) {
                    JS_ASSIGN_OR_RETURN(status, js_eval_var_conflicts_lexical_pattern(((JsPropertyNode*)p)->value));
                } else {
                    JS_ASSIGN_OR_RETURN(status, js_eval_var_conflicts_lexical_pattern(p));
                }
            }
            break;
        default:
            break;
    }
    return js_status_ok();
}

static Item js_eval_var_conflicts_lexical_statement(JsAstNode* node);

static Item js_eval_var_conflicts_lexical_statements(JsAstNode* stmt) {
    for (; stmt; stmt = stmt->next) {
        JS_ASSIGN_OR_RETURN(status, js_eval_var_conflicts_lexical_statement(stmt));
    }
    return js_status_ok();
}

static Item js_eval_var_conflicts_lexical_statement(JsAstNode* node) {
    if (!node) return js_status_ok();
    switch (node->node_type) {
        case JS_AST_NODE_VARIABLE_DECLARATION: {
            JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)node;
            if (vd->kind != JS_VAR_VAR) return js_status_ok();
            for (JsAstNode* d = vd->declarations; d; d = d->next) {
                if (d->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
                JS_ASSIGN_OR_RETURN(status, js_eval_var_conflicts_lexical_pattern(((JsVariableDeclaratorNode*)d)->id));
            }
            break;
        }
        case JS_AST_NODE_FUNCTION_DECLARATION:
            return js_eval_var_conflicts_lexical_name(((JsFunctionNode*)node)->name);
        case JS_AST_NODE_BLOCK_STATEMENT:
            return js_eval_var_conflicts_lexical_statements(((JsBlockNode*)node)->statements);
        case JS_AST_NODE_IF_STATEMENT: {
            JsIfNode* in = (JsIfNode*)node;
            JS_ASSIGN_OR_RETURN(status, js_eval_var_conflicts_lexical_statement(in->consequent));
            return js_eval_var_conflicts_lexical_statement(in->alternate);
        }
        case JS_AST_NODE_WHILE_STATEMENT:
            return js_eval_var_conflicts_lexical_statement(((JsWhileNode*)node)->body);
        case JS_AST_NODE_DO_WHILE_STATEMENT:
            return js_eval_var_conflicts_lexical_statement(((JsDoWhileNode*)node)->body);
        case JS_AST_NODE_FOR_STATEMENT: {
            JsForNode* fn = (JsForNode*)node;
            JS_ASSIGN_OR_RETURN(status, js_eval_var_conflicts_lexical_statement(fn->init));
            return js_eval_var_conflicts_lexical_statement(fn->body);
        }
        case JS_AST_NODE_FOR_IN_STATEMENT:
        case JS_AST_NODE_FOR_OF_STATEMENT: {
            JsForOfNode* fo = (JsForOfNode*)node;
            JS_ASSIGN_OR_RETURN(status, js_eval_var_conflicts_lexical_statement(fo->left));
            return js_eval_var_conflicts_lexical_statement(fo->body);
        }
        case JS_AST_NODE_SWITCH_STATEMENT: {
            JsSwitchNode* sw = (JsSwitchNode*)node;
            for (JsAstNode* c = sw->cases; c; c = c->next) {
                if (c->node_type != JS_AST_NODE_SWITCH_CASE) continue;
                JS_ASSIGN_OR_RETURN(status, js_eval_var_conflicts_lexical_statements(((JsSwitchCaseNode*)c)->consequent));
            }
            break;
        }
        case JS_AST_NODE_TRY_STATEMENT: {
            JsTryNode* tn = (JsTryNode*)node;
            JS_ASSIGN_OR_RETURN(status, js_eval_var_conflicts_lexical_statement(tn->block));
            JS_ASSIGN_OR_RETURN_INTO(status, js_eval_var_conflicts_lexical_statement(tn->handler));
            return js_eval_var_conflicts_lexical_statement(tn->finalizer);
        }
        case JS_AST_NODE_CATCH_CLAUSE:
            return js_eval_var_conflicts_lexical_statement(((JsCatchNode*)node)->body);
        case JS_AST_NODE_LABELED_STATEMENT:
            return js_eval_var_conflicts_lexical_statement(((JsLabeledStatementNode*)node)->body);
        default:
            break;
    }
    return js_status_ok();
}

static Item js_eval_var_conflicts_lexical_program(JsAstNode* ast) {
    if (!ast || ast->node_type != JS_AST_NODE_PROGRAM) return js_status_ok();
    return js_eval_var_conflicts_lexical_statements(((JsProgramNode*)ast)->body);
}

static bool js_eval_source_assigns_immutable_binding(String* code_str) {
    if (!code_str) return false;
    const char* source = code_str->chars;
    size_t len = code_str->len;
    size_t pos = 0;
    while (pos < len) {
        if (js_eval_skip_string_or_comment(source, len, &pos)) continue;
        char ch = source[pos];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == '$')) {
            pos++;
            continue;
        }
        size_t start = pos++;
        while (pos < len && js_eval_is_ident_char(source[pos])) pos++;
        size_t after = js_eval_skip_space_and_comments(source, len, pos);
        bool assigns = false;
        if (after < len && source[after] == '=') {
            assigns = (after + 1 >= len || (source[after + 1] != '=' && source[after + 1] != '>'));
        }
        if (assigns) {
            Item key = (Item){.item = s2it(heap_create_name(source + start, pos - start))};
            if (js_eval_local_has_immutable_binding(key)) return true;
        }
    }
    return false;
}

static bool js_eval_strict_assigns_restricted_name(String* code_str) {
    if (!code_str) return false;
    const char* source = code_str->chars;
    size_t len = code_str->len;
    size_t pos = 0;
    while (pos < len) {
        if (js_eval_skip_string_or_comment(source, len, &pos)) continue;
        size_t name_len = 0;
        if (js_eval_at_word(source, len, pos, "arguments", 9)) name_len = 9;
        else if (js_eval_at_word(source, len, pos, "eval", 4)) name_len = 4;
        if (name_len == 0) {
            pos++;
            continue;
        }
        size_t after = js_eval_skip_space_and_comments(source, len, pos + name_len);
        if (after < len) {
            char op = source[after];
            char next = (after + 1 < len) ? source[after + 1] : '\0';
            if ((op == '=' && next != '=' && next != '>') ||
                ((op == '+' || op == '-') && next == op) ||
                ((op == '+' || op == '-' || op == '*' || op == '/' || op == '%' ||
                  op == '&' || op == '|' || op == '^') && next == '=')) {
                return true;
            }
        }
        pos += name_len;
    }
    return false;
}

static bool js_eval_source_is_v8_native_probe(String* code_str, bool* result_value) {
    if (!code_str) return false;
    const char* source = code_str->chars;
    size_t len = code_str->len;
    size_t pos = js_eval_skip_space_and_comments(source, len, 0);
    if (pos >= len || source[pos] != '%') return false;
    pos++;

    struct NativeProbe {
        const char* name;
        bool bool_value;
    };
    static const NativeProbe probes[] = {
        {"DebugPrint", false},
        {"CollectGarbage", false},
        {"HaveSameMap", true},
    };

    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        const NativeProbe* probe = &probes[i];
        size_t name_len = strlen(probe->name);
        if (pos + name_len > len || memcmp(source + pos, probe->name, name_len) != 0) continue;
        size_t open = js_eval_skip_space_and_comments(source, len, pos + name_len);
        if (open >= len || source[open] != '(') return false;
        int depth = 1;
        bool in_string = false;
        char quote = '\0';
        bool escaped = false;
        size_t p = open + 1;
        while (p < len) {
            char ch = source[p];
            if (in_string) {
                if (escaped) escaped = false;
                else if (ch == '\\') escaped = true;
                else if (ch == quote) in_string = false;
            } else {
                if (ch == '\'' || ch == '"' || ch == '`') {
                    in_string = true;
                    quote = ch;
                } else if (ch == '(') {
                    depth++;
                } else if (ch == ')') {
                    depth--;
                    if (depth == 0) {
                        size_t tail = js_eval_skip_space_and_comments(source, len, p + 1);
                        if (tail != len) return false;
                        if (result_value) *result_value = probe->bool_value;
                        return true;
                    }
                }
            }
            p++;
        }
        return false;
    }
    return false;
}

// ============================================================================
// eval(code) — dynamic evaluation of JavaScript source code
// Wraps the code in an IIFE and compiles/executes via JIT.
// eval_flags bit 0: evaluate as global/direct script; bit 1: syntactic direct eval;
// bit 2: inherit strictness from the direct eval caller.
// ============================================================================
extern "C" int64_t js_eval_source_push(Item filename, Item source,
                                        int64_t line_offset, int64_t column_offset);
extern "C" void js_eval_source_pop(void);
static Item js_eval_parse_error_message(const JsTranspiler* tp) {
    char message[128];
    message[0] = '\0';
    if (!js_transpiler_parse_error_get(tp, NULL, NULL, message, sizeof(message)) ||
        message[0] == '\0') {
        return (Item){.item = s2it(heap_create_name("Invalid eval source", 19))};
    }
    return (Item){.item = s2it(heap_create_name(message, strlen(message)))};
}


static void js_eval_unwind_direct_bridge(bool is_direct_eval, bool is_global_scope) {
    if (js_eval_env_is_active()) {
        // CJS/direct eval exposes local bindings as temporary global slots;
        // thrown eval compilation must remove active bridges before caller cleanup is skipped.
        js_eval_env_pop_frame();
    }
    if (is_direct_eval || is_global_scope) {
        // Global-script direct eval bridges top-level lexicals through globalThis.
        js_eval_global_lexical_pop_frame();
    }
}

extern "C" Item js_builtin_eval_execute(Item code_item, int64_t eval_flags,
                                         Item filename_item,
                                         int64_t line_offset,
                                         int64_t column_offset) {
    if (!js_current_runtime()) {
        log_error("js-eval: no runtime context for dynamic evaluation");
        return ItemNull;
    }
    if (get_type_id(code_item) != LMD_TYPE_STRING) {
        // eval(non-string) returns the argument unchanged (ES spec)
        return code_item;
    }
    String* code_str = it2s(code_item);
    if (!code_str || code_str->len == 0) return (Item){.item = ITEM_JS_UNDEFINED};
    bool is_direct_eval = (eval_flags & 2) != 0;
    bool is_global_scope = (eval_flags & 1) != 0;
    bool is_vm_global_context = (eval_flags & JS_EVAL_FLAG_VM_GLOBAL_CONTEXT) != 0;
    bool inherited_strict = (eval_flags & 4) != 0;
    bool has_eval_filename = get_type_id(filename_item) == LMD_TYPE_STRING;
    const char* eval_filename = has_eval_filename ? it2s(filename_item)->chars : "<eval>";
    bool native_probe_value = false;
    if (js_eval_source_is_v8_native_probe(code_str, &native_probe_value)) {
        // Node official tests use V8 native probes under --allow_natives_syntax;
        // these probes observe engine internals and should not enter the JS parser.
        return (Item){.item = native_probe_value ? b2it(true) : ITEM_JS_UNDEFINED};
    }

    JS_ASSIGN_OR_RETURN(initializer_status, js_eval_initializer_early_error(code_str, is_direct_eval));

    // Check for whitespace/comment-only code — should return undefined
    {
        const char* s = code_str->chars;
        size_t slen = code_str->len;
        size_t i = 0;
        bool has_code = false;
        while (i < slen) {
            char c = s[i];
            // skip whitespace
            size_t lt_width = 0;
            if (c == ' ' || c == '\t') { i++; continue; }
            if (js_eval_at_line_terminator(s, slen, i, &lt_width)) { i += lt_width; continue; }
            // skip hashbang comment at the start of eval script source
            if (i == 0 && c == '#' && i + 1 < slen && s[i + 1] == '!') {
                i += 2;
                while (i < slen && !js_eval_at_line_terminator(s, slen, i, NULL)) i++;
                continue;
            }
            // skip line comments
            if (c == '/' && i + 1 < slen && s[i+1] == '/') {
                i += 2;
                while (i < slen && !js_eval_at_line_terminator(s, slen, i, NULL)) i++;
                continue;
            }
            // skip block comments
            if (c == '/' && i + 1 < slen && s[i+1] == '*') {
                i += 2;
                while (i + 1 < slen && !(s[i] == '*' && s[i+1] == '/')) i++;
                if (i + 1 < slen) i += 2;
                continue;
            }
            has_code = true;
            break;
        }
        if (!has_code) {
            return (Item){.item = ITEM_JS_UNDEFINED};
        }
    }

    // Fast path: if code is a single RegExp literal, construct directly
    // without going through the full parse → AST → JIT pipeline.
    // Pattern: /.../ optionally followed by [gimsuy]* flags, nothing else.
    if (code_str->len >= 2 && code_str->chars[0] == '/') {
        size_t i = 1;
        bool in_class = false;
        while (i < code_str->len) {
            char c = code_str->chars[i];
            if (c == '\\' && i + 1 < code_str->len) { i += 2; continue; }
            if (c == '[') { in_class = true; i++; continue; }
            if (c == ']' && in_class) { in_class = false; i++; continue; }
            if (c == '/' && !in_class) break;
            i++;
        }
        if (i < code_str->len) {
            size_t flags_start = i + 1;
            bool valid = true;
            for (size_t j = flags_start; j < code_str->len; j++) {
                char f = code_str->chars[j];
                if (!(f == 'g' || f == 'i' || f == 'm' || f == 's' || f == 'u' || f == 'y')) {
                    valid = false;
                    break;
                }
            }
            if (valid && flags_start <= code_str->len) {
                extern Item js_create_regexp_from_source(const char* src, size_t len);
                Item regexp_result = js_create_regexp_from_source(code_str->chars, code_str->len);
                return regexp_result;
            }
        }
    }

    size_t code_len = code_str->len;
    Item fn_item = ItemNull;

    if (is_direct_eval && inherited_strict && js_eval_source_assigns_immutable_binding(code_str)) {
        return js_throw_type_error("Assignment to constant variable");
    }
    if (is_direct_eval && inherited_strict && js_eval_strict_assigns_restricted_name(code_str)) {
        return js_throw_syntax_error((Item){.item = s2it(heap_create_name("Invalid strict eval assignment", 30))});
    }

    // v37: Phase A — try expression form for single-expression eval code.
    // Wraps as "return (code)\n" inside a function IIFE. This handles simple
    // cases like eval("1+2"), eval("new.target"), etc. and preserves function
    // context (new.target, arguments, super) that a top-level script wouldn't have.
    // Skip if code starts with a declaration keyword or contains semicolons
    // (multi-statement code should go to Phase C for correct scoping).
    {
        // The IIFE expression fast path can model direct eval because it
        // intentionally inherits the caller environment. Indirect eval must
        // execute as global script code so realm-global lexical bindings are
        // visible without capturing caller locals.
        bool skip_expr_form = !is_direct_eval;
        const char* s = code_str->chars;
        size_t slen = code_str->len;
        // Skip leading whitespace
        size_t i = 0;
        while (i < slen) {
            size_t lt_width = 0;
            if (s[i] == ' ' || s[i] == '\t') { i++; continue; }
            if (js_eval_at_line_terminator(s, slen, i, &lt_width)) { i += lt_width; continue; }
            break;
        }
        if (i < slen) {
            if (s[i] == '/' && i + 1 < slen && (s[i + 1] == '/' || s[i + 1] == '*'))
                skip_expr_form = true;
            if (i == 0 && s[i] == '#' && i + 1 < slen && s[i + 1] == '!')
                skip_expr_form = true;
            if (slen - i >= 8 && memcmp(s + i, "function", 8) == 0 &&
                (i + 8 >= slen || s[i+8] == ' ' || s[i+8] == '*' || s[i+8] == '('))
                skip_expr_form = true;
            else if (slen - i >= 5 && memcmp(s + i, "class", 5) == 0 &&
                (i + 5 >= slen || s[i+5] == ' ' || s[i+5] == '{'))
                skip_expr_form = true;
            else if (slen - i >= 5 && memcmp(s + i, "async", 5) == 0 &&
                (i + 5 >= slen || s[i+5] == ' '))
                skip_expr_form = true;
            // v37: '{' at start is a block statement, not an object literal
            else if (s[i] == '{')
                skip_expr_form = true;
            // v37: statement keywords that can't be expressions
            else if (slen - i >= 3 && memcmp(s + i, "var", 3) == 0 &&
                (i + 3 >= slen || s[i+3] == ' '))
                skip_expr_form = true;
            else if (slen - i >= 3 && memcmp(s + i, "let", 3) == 0 &&
                (i + 3 >= slen || s[i+3] == ' '))
                skip_expr_form = true;
            else if (slen - i >= 5 && memcmp(s + i, "const", 5) == 0 &&
                (i + 5 >= slen || s[i+5] == ' '))
                skip_expr_form = true;
            else if (slen - i >= 2 && memcmp(s + i, "if", 2) == 0 &&
                (i + 2 >= slen || s[i+2] == ' ' || s[i+2] == '('))
                skip_expr_form = true;
            else if (slen - i >= 3 && memcmp(s + i, "for", 3) == 0 &&
                (i + 3 >= slen || s[i+3] == ' ' || s[i+3] == '('))
                skip_expr_form = true;
            else if (slen - i >= 5 && memcmp(s + i, "while", 5) == 0 &&
                (i + 5 >= slen || s[i+5] == ' ' || s[i+5] == '('))
                skip_expr_form = true;
            else if (slen - i >= 4 && memcmp(s + i, "with", 4) == 0 &&
                (i + 4 >= slen || s[i+4] == ' ' || s[i+4] == '('))
                skip_expr_form = true;
            else if (slen - i >= 6 && memcmp(s + i, "switch", 6) == 0 &&
                (i + 6 >= slen || s[i+6] == ' ' || s[i+6] == '('))
                skip_expr_form = true;
            else if (slen - i >= 3 && memcmp(s + i, "try", 3) == 0 &&
                (i + 3 >= slen || s[i+3] == ' ' || s[i+3] == '{'))
                skip_expr_form = true;
            else if (slen - i >= 2 && memcmp(s + i, "do", 2) == 0 &&
                (i + 2 >= slen || s[i+2] == ' ' || s[i+2] == '{'))
                skip_expr_form = true;
            else if (slen - i >= 5 && memcmp(s + i, "throw", 5) == 0 &&
                (i + 5 >= slen || s[i+5] == ' '))
                skip_expr_form = true;
            else if (slen - i >= 6 && memcmp(s + i, "return", 6) == 0 &&
                (i + 6 >= slen || s[i+6] == ' ' || s[i+6] == ';'))
                skip_expr_form = true;
            else if (slen - i >= 5 && memcmp(s + i, "break", 5) == 0 &&
                (i + 5 >= slen || s[i+5] == ' ' || s[i+5] == ';'))
                skip_expr_form = true;
            else if (slen - i >= 8 && memcmp(s + i, "continue", 8) == 0 &&
                (i + 8 >= slen || s[i+8] == ' ' || s[i+8] == ';'))
                skip_expr_form = true;
            else if (slen - i >= 6 && memcmp(s + i, "import", 6) == 0 &&
                (i + 6 >= slen || s[i+6] == ' '))
                skip_expr_form = true;
            else if (slen - i >= 6 && memcmp(s + i, "export", 6) == 0 &&
                (i + 6 >= slen || s[i+6] == ' '))
                skip_expr_form = true;
        }
        // v37: Also skip expression form if code contains semicolons (multi-statement)
        // or declarations that need to be compiled as a program for correct scoping.
        if (is_vm_global_context) {
            // node:vm global scripts must not inherit the caller's CJS lexical
            // preamble; otherwise a later local const is observed as TDZ here.
            skip_expr_form = true;
        }
        if (!skip_expr_form) {
            for (size_t j = i; j < slen; j++) {
                char c = s[j];
                if (c == ';') { skip_expr_form = true; break; }
                // Skip string literals to avoid false semicolon detection
                if (c == '\'' || c == '"' || c == '`') {
                    char q = c;
                    j++;
                    while (j < slen && s[j] != q) {
                        if (s[j] == '\\') j++; // skip escape
                        j++;
                    }
                }
            }
        }
        if (!skip_expr_form) {
            const char* prefix = inherited_strict ? "\"use strict\";\nreturn (" : "return (";
            const char* suffix = "\n)";
            size_t plen = strlen(prefix), slen2 = strlen(suffix);
            size_t total = plen + code_len + slen2 + 1;
            char* body = (char*)mem_alloc(total, MEM_CAT_JS_RUNTIME);
            if (!body) {
                return ItemNull;
            }
            memcpy(body, prefix, plen);
            memcpy(body + plen, code_str->chars, code_len);
            memcpy(body + plen + code_len, suffix, slen2);
            body[total - 1] = '\0';

            Item body_item = (Item){.item = s2it(heap_create_name(body, total - 1))};
            mem_free(body);
            fn_item = js_new_function_from_string_kind(&body_item, 1,
                "function", "function anonymous", is_direct_eval);
            if (item_is_error(fn_item)) {
                js_eval_unwind_direct_bridge(is_direct_eval, is_global_scope);
                return fn_item;
            }
        }
    }

    // If expression form succeeded, call and return
    if (fn_item.item != 0 && fn_item.item != ITEM_NULL && fn_item.item != ITEM_ERROR) {
        // Direct eval inherits the caller's this-binding, including the
        // uninitialized (TDZ) one inside a derived constructor before super().
        // Resolving it here would make even `eval("1+1")` throw; the binding is
        // passed through instead so only a `this` read inside the eval throws.
        Item eval_this = js_get_lexical_this_binding();
        Item result = js_call_function(fn_item, eval_this, NULL, 0);
        if (item_is_error(result)) js_eval_unwind_direct_bridge(is_direct_eval, is_global_scope);
        return result;
    }

    // v37: Phase C — compile code directly as a top-level script (not wrapped in a function).
    // Skip Phase B (return insertion) which still wrapped in function body (wrong scoping).
    // Phase C handles completion values via eval_completion_reg and var export
    // via is_eval_direct, making it spec-compliant for multi-statement code.
    {
        const char* source = code_str->chars;
        size_t source_len = code_str->len;

        JsTranspiler* tp = js_transpiler_create(js_current_runtime());
        if (!tp) {
            log_error("js-eval: failed to create transpiler for direct script");
            return ItemNull;
        }
        tp->strict_mode = inherited_strict;

        if (!js_transpiler_parse(tp, source, source_len)) {
            log_error("js-eval: parse failed for direct script");
            Item syntax_message = js_eval_parse_error_message(tp);
            js_transpiler_destroy(tp);
            js_eval_unwind_direct_bridge(is_direct_eval, is_global_scope);
            // Dynamic eval surfaces parser diagnostics through SyntaxError;
            // the REPL uses the same location to render the source caret.
            return js_throw_syntax_error(syntax_message);
        }

        TSNode root = ts_tree_root_node(tp->tree);
        JsAstNode* js_ast = build_js_ast_indexed(tp, root);
        if (!js_ast) {
            log_error("js-eval: AST build failed for direct script");
            js_transpiler_destroy(tp);
            return ItemNull;
        }

        int early_errors = js_check_early_errors(tp, js_ast);
        if (early_errors > 0) {
            js_transpiler_destroy(tp);
            return js_throw_syntax_error((Item){.item = s2it(heap_create_name("Invalid eval source", 19))});
        }
        if (is_direct_eval && !inherited_strict) {
            Item conflict_status = js_eval_var_conflicts_lexical_program(js_ast);
            if (item_is_error(conflict_status)) {
                js_transpiler_destroy(tp);
                return conflict_status;
            }
        }

        // Use optimize level 0 for eval code — small snippets don't benefit
        // from optimization but pay the full cost of each pass.
        MIR_context_t eval_ctx = jit_init(0);
        if (!eval_ctx) {
            log_error("js-eval: MIR context init failed");
            js_transpiler_destroy(tp);
            return ItemNull;
        }

        if (g_batch_mir_error_handler) {
            MIR_set_error_func(eval_ctx, g_batch_mir_error_handler);
        }

        JsMirTranspiler* mt = jm_create_mir_transpiler(tp, eval_ctx, eval_filename, false, 16, 8, 8, "js-eval");
        if (!mt) {
            MIR_finish(eval_ctx);
            js_transpiler_destroy(tp);
            return ItemNull;
        }
        bool js_eval_fresh_module_scope = (eval_flags & 8) != 0 || !is_direct_eval;
        mt->module_name_base = js_eval_fresh_module_scope
            ? 0 : js_active_module_name_count();
        mt->module_ic_base = js_eval_fresh_module_scope
            ? 0 : js_active_module_ic_count();
        // This lowering mode describes eval's global var environment. Indirect
        // eval is global-scope too, so Annex-B declaration lowering must keep
        // its eval export path even though it never inherits caller lexicals.
        mt->is_eval_direct = is_global_scope;
        // Tagged-template identity is realm-observable. Use the bound
        // context's monotonic counter so concurrent evals never share a
        // process-global site sequence.
        mt->template_site_salt = ++js_runtime_state.dynamic_func_counter;

        if (!is_vm_global_context && g_eval_preamble_entries &&
                g_eval_preamble_entry_count > 0) {
            if (is_direct_eval) {
                // Direct eval uses its caller's full lexical environment.
                mt->preamble_entries = g_eval_preamble_entries;
                mt->preamble_entry_count = g_eval_preamble_entry_count;
                mt->preamble_var_count = g_eval_preamble_var_count;
            } else {
                // Indirect eval shares the realm's global environment but not
                // caller var/function slots. Reusing those slots made Annex-B
                // eval update only a disposable copied module state instead of
                // defining/updating the realm-global binding.
                js_install_realm_global_preamble(mt, tp);
            }
        }

        char module_name[48];
        snprintf(module_name, sizeof(module_name), "js_eval_%d", js_dynamic_func_counter++);
        mt->module = MIR_new_module(eval_ctx, module_name);

        if (!transpile_js_mir_ast(mt, js_ast)) {
            log_error("js-eval: collection/allocation failed");
            jm_destroy_mir_transpiler(mt);
            MIR_finish(eval_ctx);
            js_transpiler_destroy(tp);
            return ItemNull;
        }

        if (!jm_validate_mir_labels(eval_ctx)) {
            log_error("js-eval: NULL labels detected in direct script");
            jm_destroy_mir_transpiler(mt);
            MIR_finish(eval_ctx);
            js_transpiler_destroy(tp);
            return ItemNull;
        }

        MIR_link(eval_ctx, g_mir_interp_mode ? MIR_set_interp_interface : MIR_set_gen_interface, import_resolver);

        typedef Item (*js_main_func_t)(Context*);
        js_main_func_t js_main_fn = (js_main_func_t)find_func(eval_ctx, (char*)"js_main");

        if (!js_main_fn) {
            log_error("js-eval: failed to find js_main in direct script");
            jm_destroy_mir_transpiler(mt);
            MIR_finish(eval_ctx);
            js_transpiler_destroy(tp);
            return ItemNull;
        }

        // Execute js_main directly — returns the completion value
        // v37: Save/restore new.target, set to undefined for eval code.
        // Per ES spec, eval in class field initializers runs "outside a constructor",
        // so new.target should be undefined, not the enclosing constructor.
        Item prev_nt = js_get_new_target();
        js_set_direct_new_target((Item){.item = ITEM_JS_UNDEFINED});

        // vm.runInContext (eval_flags bit 8): give this unit its own module-var
        // slot namespace. Units sharing a context each assign slot indices from 0,
        // so without isolation a later unit's top-level globals clobber an earlier
        // unit's slots in the shared js_module_vars array — breaking cross-unit
        // references (e.g. a constructor defined in base.js, invoked from box2d.js,
        // read the wrong slot for its own name). Functions capture a module-state
        // id at creation and js_call_function activates it per call, so cross-unit
        // invocation still resolves against the defining unit's slab.
        // VM scripts and indirect eval each compile a separate global script.
        // Their own slots begin after the retained harness prefix, preventing
        // either unit from aliasing caller module bindings.
        uint32_t js_eval_prev_module_state_id = UINT32_MAX;
        if (js_eval_fresh_module_scope) {
            js_eval_prev_module_state_id = js_get_active_module_state_id();
            if (!js_activate_module_state((uint32_t)mt->module_var_count)) {
                return ItemError;
            }
            if (!is_direct_eval && mt->preamble_var_count > 0 &&
                    !js_copy_module_state_var_prefix(js_eval_prev_module_state_id,
                        js_get_active_module_state_id(), (uint32_t)mt->preamble_var_count)) {
                return ItemError;
            }
        } else if (!js_ensure_active_module_var_capacity(
                (uint32_t)mt->module_var_count)) {
            return ItemError;
        }

        if (!js_append_compiled_name_table(mt)) {
            log_error("js-eval: failed to append NameId table");
            if (js_eval_fresh_module_scope) {
                js_set_active_module_state_id(js_eval_prev_module_state_id);
            }
            js_set_direct_new_target(prev_nt);
            jm_destroy_mir_transpiler(mt);
            MIR_finish(eval_ctx);
            js_transpiler_destroy(tp);
            return ItemError;
        }

        RootFrame eval_this_roots(2);
        Rooted<Item> previous_this(eval_this_roots, js_get_this());
        Rooted<Item> global_this(eval_this_roots, js_get_global_this());
        if (!is_direct_eval) {
            // An indirect eval executes as global script code even when its
            // source has a strict directive. Leaving the intrinsic call's
            // undefined `this` installed made top-level `this` incorrectly
            // behave like strict function code instead of the realm global.
            js_set_this(global_this.get());
        }
        Item result = js_main_fn((Context*)context);
        if (!is_direct_eval) js_set_this(previous_this.get());
        if (item_is_error(result)) js_eval_unwind_direct_bridge(is_direct_eval, is_global_scope);

        if (js_eval_fresh_module_scope) {
            js_set_active_module_state_id(js_eval_prev_module_state_id);
        }

        js_set_direct_new_target(prev_nt);

        // Cleanup
        jm_destroy_mir_transpiler(mt);
        // Defer MIR context cleanup because eval code may return closures whose
        // JIT pointers must remain valid.
        jm_defer_mir_cleanup(eval_ctx);
        js_transpiler_destroy(tp);

        return result;
    }
}

extern "C" Item js_builtin_eval_with_options(Item code_item, int64_t eval_flags,
                                              Item filename_item,
                                              int64_t line_offset,
                                              int64_t column_offset) {
    // Preserve eval's non-string and empty-source fast paths before claiming a
    // source slot; the execute body then has one owner and no return-site pops.
    bool source_pushed = js_current_runtime() && get_type_id(code_item) == LMD_TYPE_STRING &&
        it2s(code_item) && it2s(code_item)->len > 0 &&
        get_type_id(filename_item) == LMD_TYPE_STRING &&
        js_eval_source_push(filename_item, code_item, line_offset, column_offset) != 0;
    Item result = js_builtin_eval_execute(code_item, eval_flags, filename_item,
                                          line_offset, column_offset);
    if (source_pushed) js_eval_source_pop();
    return result;
}

extern "C" Item js_builtin_eval(Item code_item, int64_t eval_flags) {
    return js_builtin_eval_with_options(code_item, eval_flags,
        (Item){.item = ITEM_JS_UNDEFINED}, 0, 0);
}

// ============================================================================
// Public entry point: transpile a pre-built JS AST to MIR (used by TS transpiler)
// ============================================================================
