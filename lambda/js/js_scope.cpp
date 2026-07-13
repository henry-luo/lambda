#include "js_transpiler.hpp"
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/mem_factory.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"
#include "../../lib/hashmap.h"
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include "../../lib/mem.h"

#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#define JS_IDENT_STATS_MKDIR(path) _mkdir(path)
#define JS_IDENT_STATS_OPEN(path) _open(path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE)
#define JS_IDENT_STATS_WRITE(fd, buf, len) _write(fd, buf, (unsigned int)(len))
#define JS_IDENT_STATS_CLOSE(fd) _close(fd)
#define JS_IDENT_STATS_PID() _getpid()
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#define JS_IDENT_STATS_MKDIR(path) mkdir(path, 0755)
#define JS_IDENT_STATS_OPEN(path) open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644)
#define JS_IDENT_STATS_WRITE(fd, buf, len) write(fd, buf, len)
#define JS_IDENT_STATS_CLOSE(fd) close(fd)
#define JS_IDENT_STATS_PID() getpid()
#endif

// TypeScript parser (unified: handles both JS and TS)
extern "C" {
    const TSLanguage* tree_sitter_typescript(void);
    const TSLanguage* tree_sitter_javascript(void);
}

// Tune6 diagnostics: scope-lookup counters (see js_runtime.h). Disabled by
// default so normal transpiles pay only a single predictable not-taken branch
// per scanned entry.
static bool g_js_scope_counters_enabled = false;
static JsScopeCounters g_js_scope_counters = {0, 0, 0};

static bool g_js_identifier_counters_enabled = false;
static bool g_js_identifier_counters_checked = false;
static bool g_js_identifier_counters_registered = false;
static JsIdentifierCounters g_js_identifier_counters = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static bool g_js_parse_error_valid = false;
static int64_t g_js_parse_error_row = 0;
static int64_t g_js_parse_error_col = 0;
static char g_js_parse_error_message[128];

static void js_identifier_counters_report(void);
static void js_identifier_counters_write_line(int fd, const char* line);

static void js_parse_error_reset(void) {
    g_js_parse_error_valid = false;
    g_js_parse_error_row = 0;
    g_js_parse_error_col = 0;
    g_js_parse_error_message[0] = '\0';
}

static bool js_parse_error_is_ident_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           c == '_' || c == '$';
}

static void js_parse_error_record(TSNode node, const char* source,
                                  size_t length, bool missing) {
    TSPoint s = ts_node_start_point(node);
    uint32_t start_byte = ts_node_start_byte(node);
    g_js_parse_error_valid = true;
    g_js_parse_error_row = (int64_t)s.row;
    g_js_parse_error_col = (int64_t)s.column;
    const char* message = "Unexpected token";
    if (missing) {
        message = "Unexpected end of input";
    } else if (start_byte >= length) {
        message = "Unexpected end of input";
    } else if (js_parse_error_is_ident_char(source[start_byte])) {
        message = "Unexpected identifier";
    }
    snprintf(g_js_parse_error_message, sizeof(g_js_parse_error_message), "%s", message);
}

extern "C" int js_parse_error_get(int64_t* out_row, int64_t* out_col,
                                  char* out_message,
                                  int64_t out_message_size) {
    if (!g_js_parse_error_valid) return 0;
    if (out_row) *out_row = g_js_parse_error_row;
    if (out_col) *out_col = g_js_parse_error_col;
    if (out_message && out_message_size > 0) {
        snprintf(out_message, (size_t)out_message_size, "%s",
                 g_js_parse_error_message);
    }
    return 1;
}

extern "C" void js_scope_counters_set_enabled(int enabled) {
    g_js_scope_counters_enabled = (enabled != 0);
}

extern "C" void js_scope_counters_reset(void) {
    g_js_scope_counters.lookup_calls = 0;
    g_js_scope_counters.entries_scanned = 0;
    g_js_scope_counters.scopes_walked = 0;
}

extern "C" void js_scope_counters_get(JsScopeCounters* out) {
    if (out) *out = g_js_scope_counters;
}

extern "C" void js_identifier_counters_set_enabled(int enabled) {
    g_js_identifier_counters_enabled = (enabled != 0);
}

extern "C" void js_identifier_counters_reset(void) {
    memset(&g_js_identifier_counters, 0, sizeof(g_js_identifier_counters));
}

extern "C" void js_identifier_counters_get(JsIdentifierCounters* out) {
    if (out) *out = g_js_identifier_counters;
}

extern "C" int js_identifier_counters_is_enabled(void) {
    if (!g_js_identifier_counters_checked) {
        const char* flag = getenv("LAMBDA_JS_IDENTIFIER_STATS");
        if (flag && flag[0] && strcmp(flag, "0") != 0) {
            g_js_identifier_counters_enabled = true;
        }
        g_js_identifier_counters_checked = true;
    }
    if (g_js_identifier_counters_enabled && !g_js_identifier_counters_registered) {
        atexit(js_identifier_counters_report);
        g_js_identifier_counters_registered = true;
    }
    return g_js_identifier_counters_enabled ? 1 : 0;
}

extern "C" void js_identifier_counters_record_ast(int source_len, int decoded_len,
        int has_escape, int has_non_ascii) {
    if (!js_identifier_counters_is_enabled()) return;
    g_js_identifier_counters.ast_identifiers++;
    if (has_escape) g_js_identifier_counters.ast_escaped_identifiers++;
    if (has_non_ascii) g_js_identifier_counters.ast_non_ascii_identifiers++;
    if (source_len > 0) g_js_identifier_counters.ast_source_bytes += source_len;
    if (decoded_len > 0) g_js_identifier_counters.ast_decoded_bytes += decoded_len;
}

extern "C" void js_identifier_counters_record_early_check(void) {
    if (!js_identifier_counters_is_enabled()) return;
    g_js_identifier_counters.early_identifier_checks++;
}

extern "C" void js_identifier_counters_record_early_escape(int normalized,
        int reserved_hit, int contextual_hit) {
    if (!js_identifier_counters_is_enabled()) return;
    g_js_identifier_counters.early_escape_checks++;
    if (normalized) g_js_identifier_counters.early_unicode_normalizations++;
    if (reserved_hit) g_js_identifier_counters.early_reserved_hits++;
    if (contextual_hit) g_js_identifier_counters.early_contextual_escape_hits++;
}

static void js_identifier_counters_report(void) {
    if (!g_js_identifier_counters_enabled || g_js_identifier_counters.ast_identifiers == 0) return;
    const char* dir = getenv("LAMBDA_JS_IDENTIFIER_STATS_DIR");
    if (!dir || !dir[0]) dir = "./temp/js_identifier_stats";
    JS_IDENT_STATS_MKDIR(dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/%d.tsv", dir, (int)JS_IDENT_STATS_PID());
    int fd = JS_IDENT_STATS_OPEN(path);
    if (fd < 0) return;
    js_identifier_counters_write_line(fd,
        "ast_identifiers\tast_escaped_identifiers\tast_non_ascii_identifiers\tast_source_bytes\tast_decoded_bytes\tearly_identifier_checks\tearly_escape_checks\tearly_unicode_normalizations\tearly_reserved_hits\tearly_contextual_escape_hits\n");
    char line[512];
    snprintf(line, sizeof(line), "%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\n",
        g_js_identifier_counters.ast_identifiers,
        g_js_identifier_counters.ast_escaped_identifiers,
        g_js_identifier_counters.ast_non_ascii_identifiers,
        g_js_identifier_counters.ast_source_bytes,
        g_js_identifier_counters.ast_decoded_bytes,
        g_js_identifier_counters.early_identifier_checks,
        g_js_identifier_counters.early_escape_checks,
        g_js_identifier_counters.early_unicode_normalizations,
        g_js_identifier_counters.early_reserved_hits,
        g_js_identifier_counters.early_contextual_escape_hits);
    js_identifier_counters_write_line(fd, line);
    JS_IDENT_STATS_CLOSE(fd);
    log_notice("js-ident-stats: ast=%ld escaped=%ld non_ascii=%ld early=%ld escape_checks=%ld normalized=%ld reserved_hits=%ld contextual_hits=%ld",
        g_js_identifier_counters.ast_identifiers,
        g_js_identifier_counters.ast_escaped_identifiers,
        g_js_identifier_counters.ast_non_ascii_identifiers,
        g_js_identifier_counters.early_identifier_checks,
        g_js_identifier_counters.early_escape_checks,
        g_js_identifier_counters.early_unicode_normalizations,
        g_js_identifier_counters.early_reserved_hits,
        g_js_identifier_counters.early_contextual_escape_hits);
}

static void js_identifier_counters_write_line(int fd, const char* line) {
    if (fd < 0 || !line) return;
    size_t len = strlen(line);
    const char* cur = line;
    while (len > 0) {
        int wrote = (int)JS_IDENT_STATS_WRITE(fd, cur, len);
        if (wrote <= 0) return;
        cur += wrote;
        len -= (size_t)wrote;
    }
}

// Scope management functions

static ScopeKind js_scope_type_to_scope_kind(JsScopeType scope_type) {
    switch (scope_type) {
    case JS_SCOPE_GLOBAL: return SCOPE_KIND_GLOBAL;
    case JS_SCOPE_MODULE: return SCOPE_KIND_MODULE;
    case JS_SCOPE_FUNCTION: return SCOPE_KIND_FUNCTION;
    case JS_SCOPE_BLOCK:
    default:
        return SCOPE_KIND_BLOCK;
    }
}

JsScope* js_scope_create(JsTranspiler* tp, JsScopeType scope_type, JsScope* parent) {
    JsScope* scope = (JsScope*)pool_alloc(tp->ast_pool, sizeof(JsScope));
    memset(scope, 0, sizeof(JsScope));

    scope->kind = js_scope_type_to_scope_kind(scope_type);
    scope->parent = parent;
    scope->strict = parent ? parent->strict : tp->strict_mode;
    scope->first = NULL;
    scope->last = NULL;

    return scope;
}

void js_scope_push(JsTranspiler* tp, JsScope* scope) {
    scope->parent = tp->current_scope;
    tp->current_scope = scope;
    log_debug("Pushed JavaScript scope type: %d", scope->kind);
}

void js_scope_pop(JsTranspiler* tp) {
    if (tp->current_scope) {
        JsScope* old_scope = tp->current_scope;
        tp->current_scope = old_scope->parent;
        log_debug("Popped JavaScript scope type: %d", old_scope->kind);
    }
}

NameEntry* js_scope_lookup(JsTranspiler* tp, String* name) {
    JsScope* scope = tp->current_scope;

    if (g_js_scope_counters_enabled) g_js_scope_counters.lookup_calls++;

    while (scope) {
        if (g_js_scope_counters_enabled) g_js_scope_counters.scopes_walked++;
        NameEntry* entry = scope->first;
        while (entry) {
            if (g_js_scope_counters_enabled) g_js_scope_counters.entries_scanned++;
            if (entry->name->len == name->len &&
                memcmp(entry->name->chars, name->chars, name->len) == 0) {
                return entry;
            }
            entry = entry->next;
        }

        // For var declarations, skip block scopes and go to function scope
        scope = scope->parent;
    }

    return NULL; // Not found
}

NameEntry* js_scope_lookup_current(JsTranspiler* tp, String* name) {
    if (g_js_scope_counters_enabled) g_js_scope_counters.lookup_calls++;
    if (!tp->current_scope) return NULL;

    if (g_js_scope_counters_enabled) g_js_scope_counters.scopes_walked++;
    NameEntry* entry = tp->current_scope->first;
    while (entry) {
        if (g_js_scope_counters_enabled) g_js_scope_counters.entries_scanned++;
        if (entry->name->len == name->len &&
            memcmp(entry->name->chars, name->chars, name->len) == 0) {
            return entry;
        }
        entry = entry->next;
    }

    return NULL;
}

NameEntry* js_scope_define(JsTranspiler* tp, String* name, JsAstNode* node, JsVarKind kind) {
    JsScope* target_scope = tp->current_scope;

    // var declarations are function-scoped, let/const are block-scoped
    if (kind == JS_VAR_VAR) {
        // Find the nearest function scope or global scope
        while (target_scope && target_scope->kind == SCOPE_KIND_BLOCK) {
            target_scope = target_scope->parent;
        }
    }

    if (!target_scope) {
        target_scope = tp->global_scope;
    }

    // Check for redeclaration in strict mode or with let/const
    if (target_scope->strict || kind != JS_VAR_VAR) {
        NameEntry* existing = NULL;
        NameEntry* scan = target_scope->first;
        while (scan) {
            if (scan->name->len == name->len &&
                memcmp(scan->name->chars, name->chars, name->len) == 0) {
                existing = scan;
                break;
            }
            scan = scan->next;
        }
        if (existing) {
            log_error("Identifier '%.*s' has already been declared",
                     (int)name->len, name->chars);
            return existing;
        }
    }

    // Create new name entry
    NameEntry* entry = (NameEntry*)pool_alloc(tp->ast_pool, sizeof(NameEntry));
    memset(entry, 0, sizeof(NameEntry));
    entry->name = name;
    entry->node = (AstNode*)node;
    entry->scope = target_scope;
    entry->is_mutable = (kind != JS_VAR_CONST);
    entry->is_const = (kind == JS_VAR_CONST);
    entry->is_lexical = (kind != JS_VAR_VAR);
    entry->tdz_active = entry->is_lexical;

    // Add to scope
    if (!target_scope->first) {
        target_scope->first = entry;
    } else {
        target_scope->last->next = entry;
    }
    target_scope->last = entry;

    log_debug("Defined JavaScript variable '%.*s' in scope type %d",
             (int)name->len, name->chars, target_scope->kind);
    return entry;
}

// Error handling functions

void js_error(JsTranspiler* tp, TSNode node, const char* format, ...) {
    tp->has_errors = true;

    if (!tp->error_buf) {
        tp->error_buf = strbuf_new();
    }

    // Add location information
    uint32_t start_row = ts_node_start_point(node).row;
    uint32_t start_col = ts_node_start_point(node).column;
    strbuf_append_format(tp->error_buf, "Error at line %u, column %u: ",
                        start_row + 1, start_col + 1);

    // Add error message
    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    strbuf_append_str(tp->error_buf, buffer);
    strbuf_append_char(tp->error_buf, '\n');

    log_error("JavaScript transpiler error: %s", buffer);
}

void js_warning(JsTranspiler* tp, TSNode node, const char* format, ...) {
    // Add location information
    uint32_t start_row = ts_node_start_point(node).row;
    uint32_t start_col = ts_node_start_point(node).column;

    // Format warning message
    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    log_warn("JavaScript transpiler warning at line %u, column %u: %s",
             start_row + 1, start_col + 1, buffer);
}

// Transpiler lifecycle functions

JsTranspiler* js_transpiler_create(Runtime* runtime) {
    JsTranspiler* tp = (JsTranspiler*)mem_alloc(sizeof(JsTranspiler), MEM_CAT_JS_RUNTIME);
    memset(tp, 0, sizeof(JsTranspiler));

    // Initialize memory pools
    tp->ast_pool = mem_pool_create(NULL, MEM_ROLE_AST, "js.ast"); // Memory pool
    tp->name_pool = name_pool_create(tp->ast_pool, NULL);
    tp->code_buf = strbuf_new();
    tp->func_buf = strbuf_new();  // Buffer for function expressions
    tp->error_buf = NULL;

    // Initialize Tree-sitter parser
    tp->parser = ts_parser_new();
    const TSLanguage* lang = tree_sitter_javascript();
    if (!lang) {
        lang = tree_sitter_typescript();
    }
    ts_parser_set_language(tp->parser, lang);

    // Initialize scopes
    tp->global_scope = js_scope_create(tp, JS_SCOPE_GLOBAL, NULL);
    tp->current_scope = tp->global_scope;
    tp->strict_mode = false;
    tp->function_counter = 0;
    tp->temp_var_counter = 0;
    tp->label_counter = 0;
    tp->in_expression = false;
    tp->has_errors = false;
    tp->strict_js = true;  // default: pure JS mode (reject TS syntax)
    tp->profile = &js_profile;
    tp->runtime = runtime;

    return tp;
}

void js_transpiler_destroy(JsTranspiler* tp) {
    if (!tp) return;

    // Cleanup Tree-sitter
    if (tp->tree) {
        ts_tree_delete(tp->tree);
    }
    if (tp->parser) {
        ts_parser_delete(tp->parser);
    }

    // Release name_pool BEFORE destroying ast_pool: name_pool was allocated
    // from ast_pool, so pool_destroy would unmap its memory.
    if (tp->name_pool) {
        name_pool_release(tp->name_pool);
    }
    if (tp->ast_pool) {
        pool_destroy(tp->ast_pool);
    }
    if (tp->code_buf) {
        strbuf_free(tp->code_buf);
    }
    if (tp->func_buf) {
        strbuf_free(tp->func_buf);
    }
    if (tp->error_buf) {
        strbuf_free(tp->error_buf);
    }
    if (tp->type_registry) {
        hashmap_free(tp->type_registry);
    }
    if (tp->normalized_source) {
        mem_free(tp->normalized_source);
    }

    mem_free(tp);
}

static bool js_source_utf8_whitespace_at(const char* source, size_t length, size_t pos,
        size_t* out_width, bool* out_line_terminator) {
    if (pos >= length) return false;
    unsigned char c0 = (unsigned char)source[pos];
    if (c0 < 0x80) return false;
    if (c0 == 0xC2 && pos + 1 < length && (unsigned char)source[pos + 1] == 0xA0) {
        if (out_width) *out_width = 2;
        if (out_line_terminator) *out_line_terminator = false;
        return true;
    }
    if (c0 == 0xE1 && pos + 2 < length &&
        (unsigned char)source[pos + 1] == 0x9A && (unsigned char)source[pos + 2] == 0x80) {
        if (out_width) *out_width = 3;
        if (out_line_terminator) *out_line_terminator = false;
        return true;
    }
    if (c0 == 0xE2 && pos + 2 < length) {
        unsigned char c1 = (unsigned char)source[pos + 1];
        unsigned char c2 = (unsigned char)source[pos + 2];
        if (c1 == 0x80 && ((c2 >= 0x80 && c2 <= 0x8A) || c2 == 0xAF)) {
            if (out_width) *out_width = 3;
            if (out_line_terminator) *out_line_terminator = false;
            return true;
        }
        if (c1 == 0x80 && (c2 == 0xA8 || c2 == 0xA9)) {
            if (out_width) *out_width = 3;
            if (out_line_terminator) *out_line_terminator = true;
            return true;
        }
        if (c1 == 0x81 && c2 == 0x9F) {
            if (out_width) *out_width = 3;
            if (out_line_terminator) *out_line_terminator = false;
            return true;
        }
    }
    if (c0 == 0xE3 && pos + 2 < length &&
        (unsigned char)source[pos + 1] == 0x80 && (unsigned char)source[pos + 2] == 0x80) {
        if (out_width) *out_width = 3;
        if (out_line_terminator) *out_line_terminator = false;
        return true;
    }
    if (c0 == 0xEF && pos + 2 < length &&
        (unsigned char)source[pos + 1] == 0xBB && (unsigned char)source[pos + 2] == 0xBF) {
        if (out_width) *out_width = 3;
        if (out_line_terminator) *out_line_terminator = false;
        return true;
    }
    return false;
}

static bool js_source_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '$';
}

static bool js_source_u180e_at(const char* source, size_t length, size_t pos) {
    return pos + 2 < length &&
        (unsigned char)source[pos] == 0xE1 &&
        (unsigned char)source[pos + 1] == 0xA0 &&
        (unsigned char)source[pos + 2] == 0x8E;
}

static bool js_source_slash_starts_regex(const char* source, size_t pos) {
    if (!source) return false;
    if (pos == 0) return true;
    size_t scan = pos;
    while (scan > 0) {
        char c = source[scan - 1];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            scan--;
            continue;
        }
        if (c == '(' || c == '[' || c == '{' || c == ':' || c == ',' || c == ';' ||
            c == '=' || c == '!' || c == '?' || c == '&' || c == '|' || c == '+' ||
            c == '-' || c == '*' || c == '~' || c == '^' || c == '<' || c == '>') {
            return true;
        }
        return false;
    }
    return true;
}

static bool js_source_has_unescaped_u180e_code_char(const char* source, size_t length) {
    enum {
        ST_DEFAULT = 0,
        ST_SQ,
        ST_DQ,
        ST_TPL,
        ST_LINE_COMMENT,
        ST_BLOCK_COMMENT,
        ST_REGEX,
    } state = ST_DEFAULT;

    bool escaped = false;
    bool regex_class = false;
    for (size_t i = 0; i < length; i++) {
        char c = source[i];
        char n = (i + 1 < length) ? source[i + 1] : '\0';

        if (state == ST_LINE_COMMENT) {
            if (c == '\n' || c == '\r') state = ST_DEFAULT;
            continue;
        }
        if (state == ST_BLOCK_COMMENT) {
            if (c == '*' && n == '/') {
                state = ST_DEFAULT;
                i++;
            }
            continue;
        }
        if (state == ST_SQ || state == ST_DQ || state == ST_TPL) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (c == '\\') {
                escaped = true;
                continue;
            }
            if ((state == ST_SQ && c == '\'') || (state == ST_DQ && c == '"') ||
                (state == ST_TPL && c == '`')) {
                state = ST_DEFAULT;
            }
            continue;
        }
        if (state == ST_REGEX) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (c == '\\') {
                escaped = true;
                continue;
            }
            if (c == '[') {
                regex_class = true;
                continue;
            }
            if (c == ']' && regex_class) {
                regex_class = false;
                continue;
            }
            if (c == '/' && !regex_class) {
                state = ST_DEFAULT;
            }
            continue;
        }

        if (js_source_u180e_at(source, length, i)) return true;
        if (c == '/' && n == '/') {
            state = ST_LINE_COMMENT;
            i++;
            continue;
        }
        if (c == '/' && n == '*') {
            state = ST_BLOCK_COMMENT;
            i++;
            continue;
        }
        if (c == '/' && js_source_slash_starts_regex(source, i)) {
            state = ST_REGEX;
            regex_class = false;
            continue;
        }
        if (c == '\'') {
            state = ST_SQ;
            continue;
        }
        if (c == '"') {
            state = ST_DQ;
            continue;
        }
        if (c == '`') {
            state = ST_TPL;
            continue;
        }
    }
    return false;
}

static bool js_source_hex_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool js_source_prev_word_matches(const char* source, size_t start, size_t end,
        const char* word) {
    size_t word_len = strlen(word);
    return end > start && end - start == word_len && memcmp(source + start, word, word_len) == 0;
}

static bool js_source_backtick_is_probably_tagged(const char* source, size_t pos) {
    if (!source || pos == 0) return false;
    size_t scan = pos;
    while (scan > 0 && (source[scan - 1] == ' ' || source[scan - 1] == '\t' ||
                        source[scan - 1] == '\n' || source[scan - 1] == '\r')) scan--;
    if (scan == 0) return false;
    char prev = source[scan - 1];
    if (prev == ')' || prev == ']') return true;
    if (!js_source_ident_char(prev)) return false;

    size_t word_end = scan;
    size_t word_start = word_end;
    while (word_start > 0 && js_source_ident_char(source[word_start - 1])) word_start--;
    if (js_source_prev_word_matches(source, word_start, word_end, "return") ||
        js_source_prev_word_matches(source, word_start, word_end, "throw") ||
        js_source_prev_word_matches(source, word_start, word_end, "yield") ||
        js_source_prev_word_matches(source, word_start, word_end, "await") ||
        js_source_prev_word_matches(source, word_start, word_end, "typeof") ||
        js_source_prev_word_matches(source, word_start, word_end, "void") ||
        js_source_prev_word_matches(source, word_start, word_end, "delete") ||
        js_source_prev_word_matches(source, word_start, word_end, "new") ||
        js_source_prev_word_matches(source, word_start, word_end, "case")) {
        return false;
    }
    return true;
}

static bool js_source_invalid_template_escape_at(const char* source, size_t length, size_t pos) {
    if (!source || pos + 1 >= length || source[pos] != '\\') return false;
    char esc = source[pos + 1];
    if (esc >= '1' && esc <= '9') return true;
    if (esc == '0' && pos + 2 < length && source[pos + 2] >= '0' && source[pos + 2] <= '9') return true;
    if (esc == 'x') {
        return pos + 3 >= length || !js_source_hex_char(source[pos + 2]) || !js_source_hex_char(source[pos + 3]);
    }
    if (esc == 'u') {
        if (pos + 2 < length && source[pos + 2] == '{') {
            size_t hex_pos = pos + 3;
            if (hex_pos >= length || !js_source_hex_char(source[hex_pos])) return true;
            uint32_t codepoint = 0;
            while (hex_pos < length && js_source_hex_char(source[hex_pos])) {
                char hex = source[hex_pos];
                uint32_t digit = (hex >= '0' && hex <= '9') ? (uint32_t)(hex - '0') :
                    (hex >= 'a' && hex <= 'f') ? (uint32_t)(hex - 'a' + 10) : (uint32_t)(hex - 'A' + 10);
                codepoint = (codepoint << 4) | digit;
                hex_pos++;
                if (codepoint > 0x10FFFF) return true;
            }
            return hex_pos >= length || source[hex_pos] != '}';
        }
        return pos + 5 >= length || !js_source_hex_char(source[pos + 2]) ||
               !js_source_hex_char(source[pos + 3]) || !js_source_hex_char(source[pos + 4]) ||
               !js_source_hex_char(source[pos + 5]);
    }
    return false;
}

static bool js_source_has_invalid_tagged_template_escape(const char* source, size_t length) {
    if (!source || length < 3) return false;
    bool in_template = false;
    bool tagged_template = false;
    bool escaped = false;
    for (size_t pos = 0; pos < length; pos++) {
        char c = source[pos];
        if (!in_template) {
            if (c == '`') {
                in_template = true;
                tagged_template = js_source_backtick_is_probably_tagged(source, pos);
                escaped = false;
            }
            continue;
        }
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\') {
            if (tagged_template && js_source_invalid_template_escape_at(source, length, pos)) return true;
            escaped = true;
            continue;
        }
        if (c == '`') {
            in_template = false;
            tagged_template = false;
        }
    }
    return false;
}

static size_t js_source_skip_ws_forward(const char* source, size_t length, size_t pos) {
    while (pos < length && (source[pos] == ' ' || source[pos] == '\t' || source[pos] == '\n' || source[pos] == '\r')) pos++;
    return pos;
}

static bool js_source_prev_word_is(const char* source, size_t pos, const char* word) {
    if (!source || !word || pos == 0) return false;
    size_t i = pos;
    while (i > 0 && (source[i - 1] == ' ' || source[i - 1] == '\t' || source[i - 1] == '\n' || source[i - 1] == '\r')) i--;
    size_t end = i;
    while (i > 0 && js_source_ident_char(source[i - 1])) i--;
    size_t len = strlen(word);
    return end - i == len && memcmp(source + i, word, len) == 0;
}

static bool js_source_next_word_is(const char* source, size_t length, size_t pos, const char* word) {
    if (!source || !word) return false;
    pos = js_source_skip_ws_forward(source, length, pos);
    size_t len = strlen(word);
    if (pos + len > length || memcmp(source + pos, word, len) != 0) return false;
    if (pos > 0 && js_source_ident_char(source[pos - 1])) return false;
    if (pos + len < length && js_source_ident_char(source[pos + len])) return false;
    return true;
}

static bool js_source_assignment_keyword_at(const char* source, size_t length, size_t pos,
        const char* keyword, size_t keyword_len) {
    if (pos + keyword_len > length) return false;
    if (memcmp(source + pos, keyword, keyword_len) != 0) return false;
    if (pos > 0 && js_source_ident_char(source[pos - 1])) return false;
    if (pos + keyword_len < length && js_source_ident_char(source[pos + keyword_len])) return false;

    size_t i = pos + keyword_len;
    while (i < length && (source[i] == ' ' || source[i] == '\t' || source[i] == '\n' || source[i] == '\r')) i++;
    if (i >= length || source[i] != '=') return false;
    if (i + 1 < length && (source[i + 1] == '=' || source[i + 1] == '>')) return false;
    return true;
}

static bool js_source_has_assignment_keyword(const char* source, size_t length) {
    if (!source || length < 5) return false;
    for (size_t i = 0; i < length; i++) {
        if (js_source_assignment_keyword_at(source, length, i, "await", 5) ||
            js_source_assignment_keyword_at(source, length, i, "yield", 5)) {
            return true;
        }
    }
    return false;
}

static bool js_source_arrow_after_pos(const char* source, size_t length, size_t pos) {
    size_t next = js_source_skip_ws_forward(source, length, pos);
    return next + 1 < length && source[next] == '=' && source[next + 1] == '>';
}

static bool js_source_soft_yield_identifier_at(const char* source, size_t length, size_t pos) {
    if (pos + 5 > length || memcmp(source + pos, "yield", 5) != 0) return false;
    if (pos > 0 && js_source_ident_char(source[pos - 1])) return false;
    if (pos + 5 < length && js_source_ident_char(source[pos + 5])) return false;

    size_t next = js_source_skip_ws_forward(source, length, pos + 5);
    if (next + 1 < length && source[next] == '=' && source[next + 1] == '>') return true;
    if (next < length && source[next] == ')' && js_source_arrow_after_pos(source, length, next + 1)) return true;
    return false;
}

static bool js_source_has_soft_yield_identifier(const char* source, size_t length) {
    if (!source || length < 5) return false;
    for (size_t i = 0; i < length; i++) {
        if (js_source_soft_yield_identifier_at(source, length, i)) return true;
    }
    return false;
}

static bool js_source_soft_await_identifier_at(const char* source, size_t length, size_t pos) {
    if (!js_source_assignment_keyword_at(source, length, pos, "await", 5)) {
        if (pos + 5 > length || memcmp(source + pos, "await", 5) != 0) return false;
        if (pos > 0 && js_source_ident_char(source[pos - 1])) return false;
        if (pos + 5 < length && js_source_ident_char(source[pos + 5])) return false;
    } else {
        return true;
    }

    size_t next = js_source_skip_ws_forward(source, length, pos + 5);
    char next_char = next < length ? source[next] : '\0';
    if (next_char == ',' || next_char == ')' || next_char == ';' || next_char == ']' || next_char == '}') return true;
    if (js_source_next_word_is(source, length, pos + 5, "in") ||
        js_source_next_word_is(source, length, pos + 5, "instanceof")) return true;

    if (js_source_prev_word_is(source, pos, "function")) return true;
    if ((js_source_prev_word_is(source, pos, "var") || js_source_prev_word_is(source, pos, "let") ||
         js_source_prev_word_is(source, pos, "const")) &&
        (next_char == '=' || next_char == ',' || next_char == ';')) return true;

    return false;
}

static bool js_source_has_soft_await_identifier(const char* source, size_t length) {
    if (!source || length < 5) return false;
    for (size_t i = 0; i < length; i++) {
        if (js_source_soft_await_identifier_at(source, length, i)) return true;
    }
    return false;
}

static char* js_normalize_source_for_parser(const char* source, size_t length) {
    if (!source || length == 0) return NULL;

    bool has_html_open = false;
    bool has_unicode_space = false;
    bool has_ascii_cr = false;
    bool has_nul = false;
    bool has_assignment_keyword = js_source_has_assignment_keyword(source, length);
    bool has_soft_await_identifier = js_source_has_soft_await_identifier(source, length);
    bool has_soft_yield_identifier = js_source_has_soft_yield_identifier(source, length);
    bool has_invalid_tagged_template_escape = js_source_has_invalid_tagged_template_escape(source, length);
    for (size_t i = 0; i + 3 < length; i++) {
        if (source[i] == '<' && source[i + 1] == '!' && source[i + 2] == '-' && source[i + 3] == '-') {
            has_html_open = true;
            break;
        }
    }
    for (size_t i = 0; i < length; i++) {
        if (source[i] == '\r') {
            has_ascii_cr = true;
            break;
        }
        if (source[i] == '\0') {
            has_nul = true;
            break;
        }
        size_t width = 0;
        bool line_terminator = false;
        if (js_source_utf8_whitespace_at(source, length, i, &width, &line_terminator)) {
            has_unicode_space = true;
            break;
        }
    }
    if (!has_html_open && !has_unicode_space && !has_ascii_cr && !has_nul && !has_assignment_keyword &&
        !has_soft_await_identifier && !has_soft_yield_identifier &&
        !has_invalid_tagged_template_escape) return NULL;

    char* out = (char*)mem_alloc(length + 1, MEM_CAT_JS_RUNTIME);
    memcpy(out, source, length);
    out[length] = '\0';

    enum {
        ST_DEFAULT = 0,
        ST_SQ,
        ST_DQ,
        ST_TPL,
        ST_LINE_COMMENT,
        ST_BLOCK_COMMENT,
        ST_REGEX,         // Js52 P2: inside a regex literal /.../
        ST_REGEX_CLASS,   // Js52 P2: inside a [...] character class in regex
    } state = ST_DEFAULT;

    bool escaped = false;
    bool tagged_template = false;
    // Js52 P2: track whether the previous significant token allows a regex
    // literal to follow.  Start of source allows regex (first token can be a
    // regex literal in an expression statement).  Updated on each non-comment
    // /non-whitespace char.  Used to disambiguate `/` between regex and div.
    bool prev_allows_regex = true;
    for (size_t i = 0; i < length; i++) {
        char c = out[i];
        char n = (i + 1 < length) ? out[i + 1] : '\0';
        char n2 = (i + 2 < length) ? out[i + 2] : '\0';
        char n3 = (i + 3 < length) ? out[i + 3] : '\0';

        if (state == ST_LINE_COMMENT) {
            if (c == '\0') {
                out[i] = ' ';
                continue;
            }
            if (c == '\r') {
                out[i] = '\n';
                if (n == '\n') {
                    out[i + 1] = ' ';
                    i++;
                }
                state = ST_DEFAULT;
                continue;
            }
            size_t width = 0;
            bool line_terminator = false;
            if (js_source_utf8_whitespace_at(out, length, i, &width, &line_terminator) && line_terminator) {
                out[i] = '\n';
                for (size_t j = 1; j < width; j++) out[i + j] = ' ';
                state = ST_DEFAULT;
                i += width - 1;
                continue;
            }
            if (c == '\n' || c == '\r') state = ST_DEFAULT;
            continue;
        }
        if (state == ST_BLOCK_COMMENT) {
            if (c == '\0') {
                out[i] = ' ';
                continue;
            }
            if (c == '\r') {
                out[i] = '\n';
                if (n == '\n') {
                    out[i + 1] = ' ';
                    i++;
                }
                continue;
            }
            size_t width = 0;
            bool line_terminator = false;
            if (js_source_utf8_whitespace_at(out, length, i, &width, &line_terminator) && line_terminator) {
                out[i] = '\n';
                for (size_t j = 1; j < width; j++) out[i + j] = ' ';
                i += width - 1;
                continue;
            }
            if (c == '*' && n == '/') {
                state = ST_DEFAULT;
                i++;
            }
            continue;
        }
        if (state == ST_SQ) {
            if (c == '\0') {
                out[i] = ' ';
                continue;
            }
            if (escaped) {
                escaped = false;
                continue;
            }
            if (c == '\\') {
                escaped = true;
                continue;
            }
            if (c == '\'') {
                state = ST_DEFAULT;
                prev_allows_regex = false;  // Js52 P2: closing quote is value-end
            }
            continue;
        }
        if (state == ST_DQ) {
            if (c == '\0') {
                out[i] = ' ';
                continue;
            }
            if (escaped) {
                escaped = false;
                continue;
            }
            if (c == '\\') {
                escaped = true;
                continue;
            }
            if (c == '"') {
                state = ST_DEFAULT;
                prev_allows_regex = false;  // Js52 P2: closing quote is value-end
            }
            continue;
        }
        if (state == ST_TPL) {
            if (c == '\0') {
                out[i] = ' ';
                continue;
            }
            if (escaped) {
                escaped = false;
                continue;
            }
            if (c == '\\') {
                if (tagged_template && js_source_invalid_template_escape_at(out, length, i)) {
                    out[i] = '_';
                    continue;
                }
                escaped = true;
                continue;
            }
            if (c == '`') {
                state = ST_DEFAULT;
                tagged_template = false;
                prev_allows_regex = false;  // Js52 P2: closing backtick is value-end
            }
            continue;
        }

        // Js52 P2: inside a regex literal — consume up to the closing '/' so the
        // `<!--` rewrite below cannot fire inside regex bodies.  Handles `[...]`
        // character classes (where `/` is literal) and `\\` escapes.
        if (state == ST_REGEX) {
            if (escaped) { escaped = false; continue; }
            if (c == '\\') { escaped = true; continue; }
            if (c == '[') { state = ST_REGEX_CLASS; continue; }
            if (c == '/') {
                state = ST_DEFAULT;
                prev_allows_regex = false;  // value-producing token
                continue;
            }
            if (c == '\n') {
                // unterminated regex — bail out of regex state, let parser flag it
                state = ST_DEFAULT;
            }
            continue;
        }
        if (state == ST_REGEX_CLASS) {
            if (escaped) { escaped = false; continue; }
            if (c == '\\') { escaped = true; continue; }
            if (c == ']') { state = ST_REGEX; continue; }
            if (c == '\n') {
                state = ST_DEFAULT;
            }
            continue;
        }

        if (c == '/' && n == '/') {
            state = ST_LINE_COMMENT;
            i++;
            continue;
        }
        if (c == '/' && n == '*') {
            state = ST_BLOCK_COMMENT;
            i++;
            continue;
        }
        // Js52 P2: regex literal — `/` after an operator-like context opens a
        // regex; after a value-producing token it's division.  Skipping the
        // body here ensures the `<!--` rewrite below cannot misinterpret regex
        // content (markdown-it, parsers in general use /-->/, /<!--/, etc.).
        if (c == '/' && prev_allows_regex) {
            state = ST_REGEX;
            continue;
        }
        if (c == '\'') {
            state = ST_SQ;
            continue;
        }
        if (c == '"') {
            state = ST_DQ;
            continue;
        }
        if (c == '`') {
            state = ST_TPL;
            tagged_template = js_source_backtick_is_probably_tagged(out, i);
            continue;
        }

        if (c == '\0') {
            out[i] = '@';
            continue;
        }

        if (js_source_soft_await_identifier_at(out, length, i)) {
            out[i + 2] = '$';
            i += 4;
            continue;
        }
        if (js_source_soft_yield_identifier_at(out, length, i)) {
            out[i + 2] = '$';
            i += 4;
            continue;
        }
        if (js_source_assignment_keyword_at(out, length, i, "yield", 5)) {
            out[i + 2] = '$';
            i += 4;
            continue;
        }

        if (c == '\r') {
            out[i] = '\n';
            if (n == '\n') {
                out[i + 1] = ' ';
                i++;
            }
            continue;
        }

        {
            size_t width = 0;
            bool line_terminator = false;
            if (js_source_utf8_whitespace_at(out, length, i, &width, &line_terminator)) {
                out[i] = line_terminator ? '\n' : ' ';
                for (size_t j = 1; j < width; j++) out[i + j] = ' ';
                i += width - 1;
                continue;
            }
        }

        if (c == '<' && n == '!' && n2 == '-' && n3 == '-') {
            // Normalize `<!--` to `//--` so parser treats it as single-line comment.
            // Keep source length unchanged to avoid offset shifts in diagnostics.
            // Js52 P2: this only fires when state == ST_DEFAULT — the ST_REGEX /
            // ST_REGEX_CLASS branches above already `continue` past this point,
            // so `<!--` inside a regex literal stays untouched.
            out[i] = '/';
            out[i + 1] = '/';
            state = ST_LINE_COMMENT;
            i++;
            continue;
        }

        // Js52 P2: track whether the next `/` can open a regex literal.
        // Rule of thumb: after a value-producing token (identifier, digit,
        // `)`, `]`), `/` is division; after anything else (operators, `(`,
        // `[`, `,`, `;`, `=`, etc.), `/` opens a regex.
        // This is the same disambiguation tree-sitter-javascript's lexer does.
        // Edge case not covered: keywords that allow regex (return, typeof,
        // delete, void, new, throw, yield, await, in, of, instanceof).  When
        // those are followed by a regex containing `<!--`, the regex body
        // would be rewritten — but that combination is exceedingly rare.
        {
            unsigned char uc = (unsigned char)c;
            bool is_word_end =
                (uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z') ||
                (uc >= '0' && uc <= '9') || uc == '_' || uc == '$' ||
                uc == ')' || uc == ']';
            prev_allows_regex = !is_word_end;
        }
    }

    return out;
}

bool js_transpiler_parse(JsTranspiler* tp, const char* source, size_t length) {
    js_parse_error_reset();
    if (tp->normalized_source) {
        mem_free(tp->normalized_source);
        tp->normalized_source = NULL;
    }

    if (js_source_has_unescaped_u180e_code_char(source, length)) {
        log_error("JavaScript source has invalid U+180E source character");
        return false;
    }

    const char* original_source = source;
    char* normalized = js_normalize_source_for_parser(source, length);
    const char* parse_source = source;
    if (normalized) {
        tp->normalized_source = normalized;
        parse_source = tp->normalized_source;
    }

    tp->source = original_source;
    tp->source_length = length;

    // Parse with Tree-sitter
    tp->tree = ts_parser_parse_string(tp->parser, NULL, parse_source, length);

    if (!tp->tree) {
        log_error("Failed to parse JavaScript source");
        return false;
    }

    TSNode root = ts_tree_root_node(tp->tree);

    // Check for syntax errors
    if (ts_node_has_error(root)) {
        // Recursively find the deepest error node
        TSNode current = root;
        for (int depth = 0; depth < 50; depth++) {
            bool found_error_child = false;
            uint32_t cc = ts_node_child_count(current);
            for (uint32_t i = 0; i < cc; i++) {
                TSNode child = ts_node_child(current, i);
                if (ts_node_is_missing(child)) {
                    TSPoint s = ts_node_start_point(child);
                    // Preserve the parser's actual failure location; eval
                    // callers need it after the transpiler object is gone.
                    js_parse_error_record(child, source, length, true);
                    log_error("  [depth %d] MISSING node '%s' at line %u:%u",
                        depth, ts_node_type(child), s.row + 1, s.column);
                } else if (strcmp(ts_node_type(child), "ERROR") == 0) {
                    TSPoint s = ts_node_start_point(child);
                    TSPoint e = ts_node_end_point(child);
                    // Preserve the deepest ERROR node so REPL SyntaxErrors
                    // point at the offending token instead of the whole input.
                    js_parse_error_record(child, source, length, false);
                    log_error("  [depth %d] ERROR node at line %u:%u - %u:%u",
                        depth, s.row + 1, s.column, e.row + 1, e.column);
                    // Print source around the error
                    uint32_t start_byte = ts_node_start_byte(child);
                    uint32_t end_byte = ts_node_end_byte(child);
                    uint32_t show_start = start_byte > 50 ? start_byte - 50 : 0;
                    uint32_t show_end = end_byte + 50 < length ? end_byte + 50 : (uint32_t)length;
                    char snippet[256];
                    uint32_t snip_len = show_end - show_start;
                    if (snip_len > 255) snip_len = 255;
                    memcpy(snippet, source + show_start, snip_len);
                    snippet[snip_len] = '\0';
                    // Replace newlines with spaces for readability
                    for (uint32_t k = 0; k < snip_len; k++) {
                        if (snippet[k] == '\n' || snippet[k] == '\r') snippet[k] = ' ';
                    }
                    log_error("  source: ...%s...", snippet);
                    current = child;
                    found_error_child = true;
                    break;
                } else if (ts_node_has_error(child)) {
                    TSPoint s = ts_node_start_point(child);
                    TSPoint e = ts_node_end_point(child);
                    log_error("  [depth %d] node '%s' has error, line %u:%u - %u:%u",
                        depth, ts_node_type(child), s.row + 1, s.column, e.row + 1, e.column);
                    current = child;
                    found_error_child = true;
                    break;
                }
            }
            if (!found_error_child) break;
        }
        log_error("JavaScript source has syntax errors");
        return false;
    }

    return true;
}
