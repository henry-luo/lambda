// ts_preprocess.cpp — Strip TypeScript type annotations from source text
//
// Replaces type annotations with spaces so that the resulting text is valid
// JavaScript that can be fed to tree_sitter_javascript(). Source positions
// are preserved because we replace characters with spaces (same byte length).
//
// Handles: variable/param/return type annotations, interface/type declarations,
// generic type parameters, 'as' expressions, non-null assertions, access
// modifiers, and 'import type' statements.

#include "ts_transpiler.hpp"
#include "../../lib/strbuf.h"
#include <cstring>
#include "../../lib/mem.h"

// ============================================================================
// Character helpers
// ============================================================================

static inline bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}

static inline bool is_alnum(char c) {
    return is_alpha(c) || (c >= '0' && c <= '9');
}

static inline bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

// ============================================================================
// TS Source Preprocessor
// ============================================================================

// Context for the preprocessor
struct TsPreprocessor {
    const char* src;
    size_t len;
    char* out;     // mutable copy
    size_t pos;
};

// blank out range [start, end) with spaces, preserving newlines
static void blank(TsPreprocessor* pp, size_t start, size_t end) {
    for (size_t i = start; i < end && i < pp->len; i++) {
        if (pp->out[i] != '\n') {
            pp->out[i] = ' ';
        }
    }
}

// skip whitespace (not newlines), return new position
static size_t skip_ws(const char* src, size_t pos, size_t len) {
    while (pos < len && is_whitespace(src[pos])) pos++;
    return pos;
}

// skip an identifier, return end position
static size_t skip_ident(const char* src, size_t pos, size_t len) {
    while (pos < len && is_alnum(src[pos])) pos++;
    return pos;
}

// check if current position matches a keyword (word boundary check)
static bool match_keyword(const char* src, size_t pos, size_t len, const char* kw) {
    size_t kw_len = strlen(kw);
    if (pos + kw_len > len) return false;
    if (memcmp(src + pos, kw, kw_len) != 0) return false;
    // check word boundary
    if (pos + kw_len < len && is_alnum(src[pos + kw_len])) return false;
    return true;
}

// skip a string literal (single or double quoted), return position after closing quote
static size_t skip_string(const char* src, size_t pos, size_t len) {
    char quote = src[pos];
    pos++; // skip opening quote
    while (pos < len) {
        if (src[pos] == '\\') { pos += 2; continue; }
        if (src[pos] == quote) { pos++; break; }
        pos++;
    }
    return pos;
}

// skip a template literal, return position after closing backtick
static size_t skip_template(const char* src, size_t pos, size_t len) {
    pos++; // skip opening backtick
    int depth = 1;
    while (pos < len && depth > 0) {
        if (src[pos] == '\\') { pos += 2; continue; }
        if (src[pos] == '$' && pos + 1 < len && src[pos + 1] == '{') {
            depth++;
            pos += 2;
            continue;
        }
        if (src[pos] == '}') { depth--; if (depth == 0) { pos++; break; } }
        if (src[pos] == '`') { pos++; break; }
        pos++;
    }
    return pos;
}

// skip a balanced group of angle brackets, handling nested <>, return position after >
// returns original position if no balanced group found
static size_t skip_angle_brackets(const char* src, size_t pos, size_t len) {
    if (pos >= len || src[pos] != '<') return pos;
    size_t start = pos;
    int depth = 1;
    pos++;
    while (pos < len && depth > 0) {
        char c = src[pos];
        if (c == '<') { depth++; }
        else if (c == '>') { depth--; }
        else if (c == '(' || c == '[' || c == '{') {
            // nested brackets - skip them
            char close = (c == '(' ? ')' : (c == '[' ? ']' : '}'));
            int inner_depth = 1;
            pos++;
            while (pos < len && inner_depth > 0) {
                if (src[pos] == c) inner_depth++;
                else if (src[pos] == close) inner_depth--;
                else if (src[pos] == '\'' || src[pos] == '"') pos = skip_string(src, pos, len) - 1;
                else if (src[pos] == '`') pos = skip_template(src, pos, len) - 1;
                pos++;
            }
            continue;
        }
        else if (c == '\'' || c == '"') { pos = skip_string(src, pos, len); continue; }
        else if (c == '`') { pos = skip_template(src, pos, len); continue; }
        // heuristic: if we hit a semicolon or newline before closing, it's not generics
        else if (c == ';' || c == '\n') { return start; }
        pos++;
    }
    return (depth == 0) ? pos : start;
}

// skip a type expression that follows ':' in annotations
// stops before '=' (assignment), ',' (next param), ')' (end params),
// '{' (function body), ';' (end statement), or newline at top level
static size_t skip_type_expr(const char* src, size_t pos, size_t len) {
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;
    int angle_depth = 0;

    while (pos < len) {
        char c = src[pos];

        // inside nested delimiters, keep going
        if (paren_depth > 0 || bracket_depth > 0 || brace_depth > 0) {
            if (c == '(') paren_depth++;
            else if (c == ')') paren_depth--;
            else if (c == '[') bracket_depth++;
            else if (c == ']') bracket_depth--;
            else if (c == '{') brace_depth++;
            else if (c == '}') brace_depth--;
            else if (c == '\'' || c == '"') { pos = skip_string(src, pos, len); continue; }
            else if (c == '`') { pos = skip_template(src, pos, len); continue; }
            pos++;
            continue;
        }

        // at top level of type expression
        if (c == '=') {
            if (pos + 1 < len && src[pos + 1] == '>') {
                // '=>' at top level: function type arrow is inside parens, so at top level this ends the type
                return pos;
            }
            return pos; // assignment
        }
        if (c == ',' || c == ')' || c == ';') return pos;
        if (c == '{') return pos; // function body
        if (c == '\n') {
            // check if next non-ws is something that continues the type
            size_t next = skip_ws(src, pos + 1, len);
            if (next < len && (src[next] == '|' || src[next] == '&')) {
                // union/intersection type continuation
                pos = next;
                continue;
            }
            return pos;
        }

        if (c == '(') { paren_depth++; pos++; continue; }
        if (c == '[') { bracket_depth++; pos++; continue; }
        if (c == '<') { angle_depth++; pos++; continue; }
        if (c == '>') { angle_depth--; pos++; continue; }

        if (c == '\'' || c == '"') { pos = skip_string(src, pos, len); continue; }
        if (c == '`') { pos = skip_template(src, pos, len); continue; }

        pos++;
    }
    return pos;
}

// skip a balanced brace block { ... }, return position after '}'
static size_t skip_brace_block(const char* src, size_t pos, size_t len) {
    if (pos >= len || src[pos] != '{') return pos;
    int depth = 1;
    pos++;
    while (pos < len && depth > 0) {
        char c = src[pos];
        if (c == '{') depth++;
        else if (c == '}') depth--;
        else if (c == '\'' || c == '"') { pos = skip_string(src, pos, len); continue; }
        else if (c == '`') { pos = skip_template(src, pos, len); continue; }
        pos++;
    }
    return pos;
}

// after a ':' that is a type annotation colon, blank the colon and type
static void blank_type_annotation(TsPreprocessor* pp, size_t colon_pos) {
    size_t after_colon = skip_ws(pp->src, colon_pos + 1, pp->len);
    size_t type_end = skip_type_expr(pp->src, after_colon, pp->len);
    blank(pp, colon_pos, type_end);
}

// check if a ':' at pos is a type annotation colon (not object literal or ternary)
// we call this for colons found after identifiers in parameter lists, variable
// declarations, and return types
static bool is_type_annotation_colon(const char* src, size_t colon_pos, size_t len) {
    // look ahead: type annotations are followed by type keywords/identifiers
    size_t after = colon_pos + 1;
    while (after < len && is_whitespace(src[after])) after++;
    if (after >= len) return false;

    char c = src[after];
    // type names start with alpha, or special type syntax: ( for function types, { for object types, [ for tuple
    return is_alpha(c) || c == '(' || c == '{' || c == '[' || c == '\'' || c == '"';
}

// ============================================================================
// process function/arrow function parameters for type annotations
// pp->pos should be AT the opening '('
// returns position after the closing ')'
// ============================================================================

static size_t process_params(TsPreprocessor* pp, size_t pos) {
    if (pos >= pp->len || pp->src[pos] != '(') return pos;
    pos++; // skip '('

    int depth = 1;
    while (pos < pp->len && depth > 0) {
        char c = pp->src[pos];
        if (c == '(') { depth++; pos++; continue; }
        if (c == ')') { depth--; if (depth == 0) { pos++; break; } pos++; continue; }
        if (c == '\'' || c == '"') { pos = skip_string(pp->src, pos, pp->len); continue; }
        if (c == '`') { pos = skip_template(pp->src, pos, pp->len); continue; }

        // look for identifier followed by optional '?' and then ':'
        if (is_alpha(c)) {
            size_t id_start = pos;
            size_t id_end = skip_ident(pp->src, pos, pp->len);
            pos = id_end;
            size_t after_id = skip_ws(pp->src, pos, pp->len);
            // skip optional '?'
            if (after_id < pp->len && pp->src[after_id] == '?') {
                blank(pp, after_id, after_id + 1);
                after_id = skip_ws(pp->src, after_id + 1, pp->len);
            }
            if (after_id < pp->len && pp->src[after_id] == ':') {
                blank_type_annotation(pp, after_id);
                // advance pos past the blanked annotation
                size_t type_end = skip_ws(pp->src, after_id + 1, pp->len);
                type_end = skip_type_expr(pp->src, type_end, pp->len);
                pos = type_end;
            }
            continue;
        }

        // handle destructuring patterns: { ... } or [ ... ] with type annotations
        if (c == '{' || c == '[') {
            char close = (c == '{') ? '}' : ']';
            int inner = 1;
            pos++;
            while (pos < pp->len && inner > 0) {
                if (pp->src[pos] == c) inner++;
                else if (pp->src[pos] == close) inner--;
                else if (pp->src[pos] == '\'' || pp->src[pos] == '"') { pos = skip_string(pp->src, pos, pp->len); continue; }
                pos++;
            }
            // after destructuring, check for ': type'
            size_t after_destruct = skip_ws(pp->src, pos, pp->len);
            if (after_destruct < pp->len && pp->src[after_destruct] == ':') {
                blank_type_annotation(pp, after_destruct);
                size_t type_end = skip_ws(pp->src, after_destruct + 1, pp->len);
                type_end = skip_type_expr(pp->src, type_end, pp->len);
                pos = type_end;
            }
            continue;
        }

        pos++;
    }
    return pos;
}

// ============================================================================
// Main preprocessing pass
// ============================================================================

char* ts_preprocess_source(const char* src, size_t len, size_t* out_len) {
    if (!src || len == 0) {
        *out_len = 0;
        return NULL;
    }

    TsPreprocessor pp;
    pp.src = src;
    pp.len = len;
    pp.out = (char*)mem_alloc(len + 1, MEM_CAT_TEMP);
    if (!pp.out) {
        *out_len = 0;
        return NULL;
    }
    memcpy(pp.out, src, len);
    pp.out[len] = '\0';
    pp.pos = 0;

    while (pp.pos < len) {
        char c = pp.src[pp.pos];

        // skip string literals
        if (c == '\'' || c == '"') {
            pp.pos = skip_string(pp.src, pp.pos, len);
            continue;
        }
        if (c == '`') {
            pp.pos = skip_template(pp.src, pp.pos, len);
            continue;
        }

        // skip single-line comments
        if (c == '/' && pp.pos + 1 < len && pp.src[pp.pos + 1] == '/') {
            while (pp.pos < len && pp.src[pp.pos] != '\n') pp.pos++;
            continue;
        }

        // skip multi-line comments
        if (c == '/' && pp.pos + 1 < len && pp.src[pp.pos + 1] == '*') {
            pp.pos += 2;
            while (pp.pos + 1 < len && !(pp.src[pp.pos] == '*' && pp.src[pp.pos + 1] == '/')) pp.pos++;
            pp.pos += 2;
            continue;
        }

        // ----------------------------------------------------------------
        // 'interface' declaration — blank entire block
        // ----------------------------------------------------------------
        if (match_keyword(pp.src, pp.pos, len, "interface")) {
            size_t start = pp.pos;
            pp.pos += 9; // skip "interface"
            // skip to opening brace
            while (pp.pos < len && pp.src[pp.pos] != '{') {
                if (pp.src[pp.pos] == '<') {
                    pp.pos = skip_angle_brackets(pp.src, pp.pos, len);
                    continue;
                }
                pp.pos++;
            }
            pp.pos = skip_brace_block(pp.src, pp.pos, len);
            blank(&pp, start, pp.pos);
            continue;
        }

        // ----------------------------------------------------------------
        // 'type' alias declaration: type X = ...;
        // but not 'typeof', and must be at statement level
        // ----------------------------------------------------------------
        if (match_keyword(pp.src, pp.pos, len, "type")) {
            // check it's not 'typeof'
            if (pp.pos + 4 < len && pp.src[pp.pos + 4] == 'o') {
                pp.pos++;
                continue;
            }
            // check context: should be at start of statement
            // look backwards for statement boundary
            size_t back = pp.pos;
            if (back > 0) back--;
            while (back > 0 && is_whitespace(pp.src[back])) back--;
            // if preceded by '.', this is not a type alias
            if (back < pp.pos && pp.src[back] == '.') {
                pp.pos++;
                continue;
            }
            // check for newline, semicolon, or start of file
            if (back == 0 || pp.src[back] == '\n' || pp.src[back] == ';' || pp.src[back] == '{' || pp.src[back] == '}') {
                size_t start = pp.pos;
                pp.pos += 4; // skip "type"
                // find the end: skip to ';' or newline (handling balanced groups)
                while (pp.pos < len && pp.src[pp.pos] != ';' && pp.src[pp.pos] != '\n') {
                    if (pp.src[pp.pos] == '{') { pp.pos = skip_brace_block(pp.src, pp.pos, len); continue; }
                    if (pp.src[pp.pos] == '<') { pp.pos = skip_angle_brackets(pp.src, pp.pos, len); continue; }
                    if (pp.src[pp.pos] == '(' || pp.src[pp.pos] == '[') {
                        char open_ch = pp.src[pp.pos];
                        char close_ch = (open_ch == '(') ? ')' : ']';
                        int d = 1; pp.pos++;
                        while (pp.pos < len && d > 0) {
                            if (pp.src[pp.pos] == open_ch) d++;
                            else if (pp.src[pp.pos] == close_ch) d--;
                            pp.pos++;
                        }
                        continue;
                    }
                    if (pp.src[pp.pos] == '\'' || pp.src[pp.pos] == '"') { pp.pos = skip_string(pp.src, pp.pos, len); continue; }
                    pp.pos++;
                }
                if (pp.pos < len && pp.src[pp.pos] == ';') pp.pos++;
                blank(&pp, start, pp.pos);
                continue;
            }
            pp.pos++;
            continue;
        }

        // ----------------------------------------------------------------
        // 'import type' / 'export type' — blank 'type' keyword
        // ----------------------------------------------------------------
        if (match_keyword(pp.src, pp.pos, len, "import")) {
            size_t after_import = skip_ws(pp.src, pp.pos + 6, len);
            if (match_keyword(pp.src, after_import, len, "type")) {
                // blank just "type " 
                blank(&pp, after_import, after_import + 4);
            }
            // skip to end of import statement
            while (pp.pos < len && pp.src[pp.pos] != ';' && pp.src[pp.pos] != '\n') pp.pos++;
            if (pp.pos < len && pp.src[pp.pos] == ';') pp.pos++;
            continue;
        }

        // ----------------------------------------------------------------
        // 'const'/'let'/'var' — strip type annotations: const x: type = value
        // ----------------------------------------------------------------
        if (match_keyword(pp.src, pp.pos, len, "const") ||
            match_keyword(pp.src, pp.pos, len, "let") ||
            match_keyword(pp.src, pp.pos, len, "var")) {
            size_t kw_len = pp.src[pp.pos] == 'c' ? 5 : (pp.src[pp.pos] == 'l' ? 3 : 3);
            pp.pos += kw_len;
            size_t after_kw = skip_ws(pp.src, pp.pos, len);
            // skip identifier
            size_t id_end = skip_ident(pp.src, after_kw, len);
            if (id_end > after_kw) {
                pp.pos = id_end;
                size_t after_id = skip_ws(pp.src, pp.pos, len);
                // check for '!' (definite assignment assertion)
                if (after_id < len && pp.src[after_id] == '!') {
                    size_t after_bang = skip_ws(pp.src, after_id + 1, len);
                    if (after_bang < len && pp.src[after_bang] == ':') {
                        blank(&pp, after_id, after_id + 1); // blank '!'
                        blank_type_annotation(&pp, after_bang);
                        size_t type_end = skip_ws(pp.src, after_bang + 1, len);
                        type_end = skip_type_expr(pp.src, type_end, len);
                        pp.pos = type_end;
                        continue;
                    }
                }
                // check for ':' type annotation
                if (after_id < len && pp.src[after_id] == ':') {
                    blank_type_annotation(&pp, after_id);
                    size_t type_end = skip_ws(pp.src, after_id + 1, len);
                    type_end = skip_type_expr(pp.src, type_end, len);
                    pp.pos = type_end;
                    continue;
                }
            }
            continue;
        }

        // ----------------------------------------------------------------
        // 'function' declaration — strip generic params and param/return type annotations
        // ----------------------------------------------------------------
        if (match_keyword(pp.src, pp.pos, len, "function")) {
            pp.pos += 8; // skip "function"
            size_t after_fn = skip_ws(pp.src, pp.pos, len);
            // skip optional '*' for generators
            if (after_fn < len && pp.src[after_fn] == '*') after_fn++;
            // skip function name
            after_fn = skip_ws(pp.src, after_fn, len);
            size_t name_end = skip_ident(pp.src, after_fn, len);
            pp.pos = name_end;

            size_t after_name = skip_ws(pp.src, pp.pos, len);
            // strip generic type parameters <T, U>
            if (after_name < len && pp.src[after_name] == '<') {
                size_t angle_end = skip_angle_brackets(pp.src, after_name, len);
                if (angle_end > after_name) {
                    blank(&pp, after_name, angle_end);
                    pp.pos = angle_end;
                    after_name = skip_ws(pp.src, pp.pos, len);
                }
            }

            // process parameters
            if (after_name < len && pp.src[after_name] == '(') {
                pp.pos = process_params(&pp, after_name);
            }

            // strip return type annotation after ')'
            size_t after_params = skip_ws(pp.src, pp.pos, len);
            if (after_params < len && pp.src[after_params] == ':') {
                blank_type_annotation(&pp, after_params);
                size_t type_end = skip_ws(pp.src, after_params + 1, len);
                type_end = skip_type_expr(pp.src, type_end, len);
                pp.pos = type_end;
            }
            continue;
        }

        // ----------------------------------------------------------------
        // Parenthesized parameter list for arrow functions: (a: T, b: U): R => ...
        // Process parameter type annotations inside the parens
        // ----------------------------------------------------------------
        if (c == '(') {
            // save position and try to find matching ')' to check for '=>'
            size_t paren_start = pp.pos;
            // find matching ')'
            int depth = 1;
            size_t scan = pp.pos + 1;
            bool has_colon = false;
            while (scan < len && depth > 0) {
                char sc = pp.src[scan];
                if (sc == '(') depth++;
                else if (sc == ')') depth--;
                else if (sc == ':') has_colon = true;
                else if (sc == '\'' || sc == '"') { scan = skip_string(pp.src, scan, len); continue; }
                else if (sc == '`') { scan = skip_template(pp.src, scan, len); continue; }
                scan++;
            }
            // scan is now just past the closing ')'
            if (depth == 0 && has_colon) {
                // check what follows the ')'
                size_t after_close = skip_ws(pp.src, scan, len);
                bool is_arrow = false;
                if (after_close + 1 < len && pp.src[after_close] == '=' && pp.src[after_close + 1] == '>') {
                    is_arrow = true;
                }
                if (!is_arrow && after_close < len && pp.src[after_close] == ':') {
                    // could be return type: ): type =>
                    size_t rt_start = skip_ws(pp.src, after_close + 1, len);
                    size_t rt_end = skip_type_expr(pp.src, rt_start, len);
                    size_t after_rt = skip_ws(pp.src, rt_end, len);
                    if (after_rt + 1 < len && pp.src[after_rt] == '=' && pp.src[after_rt + 1] == '>') {
                        is_arrow = true;
                    }
                }
                // also check for function declaration context (already handled by 'function' keyword)
                // or method definitions
                if (!is_arrow && after_close < len && pp.src[after_close] == '{') {
                    // could be method or function body — process params
                    is_arrow = true;
                }
                if (is_arrow) {
                    pp.pos = process_params(&pp, paren_start);
                    // also strip return type annotation after ')'
                    size_t after_rp = skip_ws(pp.src, pp.pos, len);
                    if (after_rp < len && pp.src[after_rp] == ':') {
                        size_t rt_colon = after_rp;
                        size_t rt_start = skip_ws(pp.src, rt_colon + 1, len);
                        size_t rt_end = skip_type_expr(pp.src, rt_start, len);
                        blank(&pp, rt_colon, rt_end);
                        pp.pos = rt_end;
                    }
                    continue;
                }
            }
            pp.pos++;
            continue;
        }

        // ----------------------------------------------------------------
        // Arrow functions: (...): type => ...
        // Look for ')' followed by ':' then '=>'
        // ----------------------------------------------------------------
        if (c == ')') {
            pp.pos++;
            size_t after_paren = skip_ws(pp.src, pp.pos, len);
            if (after_paren < len && pp.src[after_paren] == ':') {
                // look ahead past the type to see if '=>' follows
                size_t colon_pos = after_paren;
                size_t type_start = skip_ws(pp.src, colon_pos + 1, len);
                size_t type_end = skip_type_expr(pp.src, type_start, len);
                size_t after_type = skip_ws(pp.src, type_end, len);
                if (after_type + 1 < len && pp.src[after_type] == '=' && pp.src[after_type + 1] == '>') {
                    blank(&pp, colon_pos, type_end);
                    pp.pos = type_end;
                }
            }
            continue;
        }

        // ----------------------------------------------------------------
        // 'as' type assertion: expr as Type
        // ----------------------------------------------------------------
        if (match_keyword(pp.src, pp.pos, len, "as")) {
            // check that what precedes is not start of statement (e.g., not 'as' identifier)
            size_t back = pp.pos;
            if (back > 0) back--;
            while (back > 0 && is_whitespace(pp.src[back])) back--;
            // must be preceded by identifier, ), ], or similar expression end
            if (back < pp.pos && (is_alnum(pp.src[back]) || pp.src[back] == ')' || pp.src[back] == ']' || pp.src[back] == '"' || pp.src[back] == '\'')) {
                size_t start = pp.pos;
                pp.pos += 2; // skip "as"
                size_t type_start = skip_ws(pp.src, pp.pos, len);
                size_t type_end = skip_type_expr(pp.src, type_start, len);
                blank(&pp, start, type_end);
                pp.pos = type_end;
                continue;
            }
            pp.pos++;
            continue;
        }

        // ----------------------------------------------------------------
        // Access modifiers in class declarations: public/private/protected/readonly
        // Blank these keywords (they don't exist in JS)
        // ----------------------------------------------------------------
        if (match_keyword(pp.src, pp.pos, len, "public") ||
            match_keyword(pp.src, pp.pos, len, "private") ||
            match_keyword(pp.src, pp.pos, len, "protected") ||
            match_keyword(pp.src, pp.pos, len, "readonly")) {
            size_t kw_start = pp.pos;
            size_t kw_end = skip_ident(pp.src, pp.pos, len);
            size_t after = skip_ws(pp.src, kw_end, len);
            // only blank if followed by identifier (not if it's a property name like obj.public)
            if (after < len && is_alpha(pp.src[after])) {
                // check it's not preceded by '.'
                size_t b = kw_start;
                if (b > 0) b--;
                while (b > 0 && is_whitespace(pp.src[b])) b--;
                if (b < kw_start && pp.src[b] == '.') {
                    pp.pos = kw_end;
                    continue;
                }
                blank(&pp, kw_start, kw_end);
            }
            pp.pos = kw_end;
            continue;
        }

        // ----------------------------------------------------------------
        // Class method/property: handle colons after identifiers inside class body
        // handled by the general ':' detection below
        // ----------------------------------------------------------------

        // ----------------------------------------------------------------
        // Non-null assertion: expr! — blank '!' when followed by '.' or '['
        // ----------------------------------------------------------------
        if (c == '!') {
            size_t after = pp.pos + 1;
            if (after < len && (pp.src[after] == '.' || pp.src[after] == '[' || pp.src[after] == ')' || pp.src[after] == ',' || pp.src[after] == ';')) {
                // check that preceding char is identifier or ) or ]
                size_t b = pp.pos;
                if (b > 0) b--;
                if (b < pp.pos && (is_alnum(pp.src[b]) || pp.src[b] == ')' || pp.src[b] == ']')) {
                    // but don't blank != or !==
                    if (after < len && pp.src[after] == '=') {
                        pp.pos++;
                        continue;
                    }
                    blank(&pp, pp.pos, pp.pos + 1);
                }
            }
            pp.pos++;
            continue;
        }

        // ----------------------------------------------------------------
        // Generic type parameters on class/method calls: foo<Type>(...)
        // This is tricky — we need heuristics to distinguish from comparison
        // For now, handle: identifier<...>( pattern
        // ----------------------------------------------------------------
        if (c == '<' && pp.pos > 0 && is_alnum(pp.src[pp.pos - 1])) {
            // try to parse as generic angle brackets
            size_t angle_end = skip_angle_brackets(pp.src, pp.pos, len);
            if (angle_end > pp.pos) {
                size_t after = skip_ws(pp.src, angle_end, len);
                // if followed by '(' it's a generic call — blank the angle brackets
                if (after < len && pp.src[after] == '(') {
                    blank(&pp, pp.pos, angle_end);
                    pp.pos = angle_end;
                    continue;
                }
                // if followed by ',' or ')' or '>' it could be generic in a type context (already handled)
            }
            pp.pos++;
            continue;
        }

        pp.pos++;
    }

    *out_len = len;
    return pp.out;
}
