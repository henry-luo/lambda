// TypeScript v2 external scanner
// Extends base JS scanner with opaque type token scanning
//
// Token types 0..9 are inherited from JS scanner (scanner.h):
//   AUTOMATIC_SEMICOLON, TEMPLATE_CHARS, TERNARY_QMARK, HTML_COMMENT,
//   LOGICAL_OR, ESCAPE_SEQUENCE, REGEX_PATTERN, JSX_TEXT,
//   FUNCTION_SIGNATURE_AUTOMATIC_SEMICOLON, ERROR_RECOVERY
//
// Token types 10..12 are new for v2:
//   TS_TYPE, TS_TYPE_ARGUMENTS, TS_TYPE_PARAMETERS

#include "tree_sitter/parser.h"
#include <wctype.h>

enum TokenType {
    AUTOMATIC_SEMICOLON,
    TEMPLATE_CHARS,
    TERNARY_QMARK,
    HTML_COMMENT,
    LOGICAL_OR,
    ESCAPE_SEQUENCE,
    REGEX_PATTERN,
    JSX_TEXT,
    FUNCTION_SIGNATURE_AUTOMATIC_SEMICOLON,
    ERROR_RECOVERY,
    // v2 additions
    TS_TYPE,
    TS_TYPE_ARGUMENTS,
    TS_TYPE_PARAMETERS,
};

// ================================================================
// JS scanner functions (from scanner.h, included directly)
// ================================================================

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static bool scan_template_chars(TSLexer *lexer) {
    lexer->result_symbol = TEMPLATE_CHARS;
    for (bool has_content = false;; has_content = true) {
        lexer->mark_end(lexer);
        switch (lexer->lookahead) {
            case '`':
                return has_content;
            case '\0':
                return false;
            case '$':
                advance(lexer);
                if (lexer->lookahead == '{') {
                    return has_content;
                }
                break;
            case '\\':
                return has_content;
            default:
                advance(lexer);
        }
    }
}

static bool scan_whitespace_and_comments(TSLexer *lexer, bool *scanned_comment) {
    for (;;) {
        while (iswspace(lexer->lookahead)) {
            skip(lexer);
        }

        if (lexer->lookahead == '/') {
            skip(lexer);

            if (lexer->lookahead == '/') {
                skip(lexer);
                while (lexer->lookahead != 0 && lexer->lookahead != '\n') {
                    skip(lexer);
                }
                *scanned_comment = true;
            } else if (lexer->lookahead == '*') {
                skip(lexer);
                while (lexer->lookahead != 0) {
                    if (lexer->lookahead == '*') {
                        skip(lexer);
                        if (lexer->lookahead == '/') {
                            skip(lexer);
                            break;
                        }
                    } else {
                        skip(lexer);
                    }
                }
            } else {
                return false;
            }
        } else {
            return true;
        }
    }
}

static bool scan_automatic_semicolon(TSLexer *lexer, const bool *valid_symbols, bool *scanned_comment) {
    lexer->result_symbol = AUTOMATIC_SEMICOLON;
    lexer->mark_end(lexer);

    for (;;) {
        if (lexer->lookahead == 0) {
            return true;
        }
        if (lexer->lookahead == '}') {
            do {
                skip(lexer);
            } while (iswspace(lexer->lookahead));
            if (lexer->lookahead == ':') {
                return valid_symbols[LOGICAL_OR];
            }
            return true;
        }
        if (!iswspace(lexer->lookahead)) {
            return false;
        }
        if (lexer->lookahead == '\n') {
            break;
        }
        skip(lexer);
    }

    skip(lexer);

    if (!scan_whitespace_and_comments(lexer, scanned_comment)) {
        return false;
    }

    switch (lexer->lookahead) {
        case '`':
        case ',':
        case '.':
        case ';':
        case '*':
        case '%':
        case '>':
        case '<':
        case '=':
        case '?':
        case '^':
        case '|':
        case '&':
        case '/':
        case ':':
            return false;

        case '{':
            if (valid_symbols[FUNCTION_SIGNATURE_AUTOMATIC_SEMICOLON]) {
                return false;
            }
            break;

        case '(':
        case '[':
            if (valid_symbols[LOGICAL_OR]) {
                return false;
            }
            break;

        case '+':
            skip(lexer);
            return lexer->lookahead == '+';
        case '-':
            skip(lexer);
            return lexer->lookahead == '-';

        case '!':
            skip(lexer);
            return lexer->lookahead != '=';

        case 'i':
            skip(lexer);

            if (lexer->lookahead != 'n') {
                return true;
            }
            skip(lexer);

            if (!iswalpha(lexer->lookahead)) {
                return false;
            }

            for (unsigned i = 0; i < 8; i++) {
                if (lexer->lookahead != "stanceof"[i]) {
                    return true;
                }
                skip(lexer);
            }

            if (!iswalpha(lexer->lookahead)) {
                return false;
            }
            break;
    }

    return true;
}

static bool scan_ternary_qmark(TSLexer *lexer) {
    for (;;) {
        if (!iswspace(lexer->lookahead)) {
            break;
        }
        skip(lexer);
    }

    if (lexer->lookahead == '?') {
        advance(lexer);

        if (lexer->lookahead == '?') {
            return false;
        }

        lexer->mark_end(lexer);
        lexer->result_symbol = TERNARY_QMARK;

        if (lexer->lookahead == '.') {
            advance(lexer);
            if (iswdigit(lexer->lookahead)) {
                return true;
            }
            return false;
        }

        for (;;) {
            if (!iswspace(lexer->lookahead)) {
                break;
            }
            advance(lexer);
        }

        if (lexer->lookahead == ':' || lexer->lookahead == ')' || lexer->lookahead == ',') {
            return false;
        }

        if (lexer->lookahead == '.') {
            advance(lexer);
            if (iswdigit(lexer->lookahead)) {
                return true;
            }
            return false;
        }
        return true;
    }
    return false;
}

static bool scan_closing_comment(TSLexer *lexer) {
    while (iswspace(lexer->lookahead) || lexer->lookahead == 0x2028 || lexer->lookahead == 0x2029) {
        skip(lexer);
    }

    const char *comment_start = "<!--";
    const char *comment_end = "-->";

    if (lexer->lookahead == '<') {
        for (unsigned i = 0; i < 4; i++) {
            if (lexer->lookahead != comment_start[i]) {
                return false;
            }
            advance(lexer);
        }
    } else if (lexer->lookahead == '-') {
        for (unsigned i = 0; i < 3; i++) {
            if (lexer->lookahead != comment_end[i]) {
                return false;
            }
            advance(lexer);
        }
    } else {
        return false;
    }

    while (lexer->lookahead != 0 && lexer->lookahead != '\n' && lexer->lookahead != 0x2028 &&
           lexer->lookahead != 0x2029) {
        advance(lexer);
    }

    lexer->result_symbol = HTML_COMMENT;
    lexer->mark_end(lexer);

    return true;
}

static bool scan_jsx_text(TSLexer *lexer) {
    bool saw_text = false;
    bool at_newline = false;

    while (lexer->lookahead != 0 && lexer->lookahead != '<' && lexer->lookahead != '>' && lexer->lookahead != '{' &&
           lexer->lookahead != '}' && lexer->lookahead != '&') {
        bool is_wspace = iswspace(lexer->lookahead);
        if (lexer->lookahead == '\n') {
            at_newline = true;
        } else {
            at_newline &= is_wspace;
            if (!at_newline) {
                saw_text = true;
            }
        }

        advance(lexer);
    }

    lexer->result_symbol = JSX_TEXT;
    return saw_text;
}

// ================================================================
// v2: Type scanning helpers
// ================================================================

// skip whitespace (don't use skip() — we want to consume, not ignore)
static void ts_skip_ws(TSLexer *lexer) {
    while (iswspace(lexer->lookahead)) {
        advance(lexer);
    }
}

// skip whitespace and comments within type context
static void ts_skip_ws_and_comments(TSLexer *lexer) {
    for (;;) {
        while (iswspace(lexer->lookahead)) {
            advance(lexer);
        }
        if (lexer->lookahead == '/') {
            advance(lexer);
            if (lexer->lookahead == '/') {
                // line comment
                advance(lexer);
                while (lexer->lookahead != 0 && lexer->lookahead != '\n') {
                    advance(lexer);
                }
            } else if (lexer->lookahead == '*') {
                // block comment
                advance(lexer);
                while (lexer->lookahead != 0) {
                    if (lexer->lookahead == '*') {
                        advance(lexer);
                        if (lexer->lookahead == '/') {
                            advance(lexer);
                            break;
                        }
                    } else {
                        advance(lexer);
                    }
                }
            } else {
                // stray slash — this is odd in a type context, bail
                return;
            }
        } else {
            return;
        }
    }
}

// skip a string literal ('...' or "...")
static void ts_skip_string(TSLexer *lexer) {
    int32_t quote = lexer->lookahead;
    advance(lexer); // consume opening quote
    while (lexer->lookahead != 0 && lexer->lookahead != quote) {
        if (lexer->lookahead == '\\') {
            advance(lexer); // escape char
            if (lexer->lookahead != 0) advance(lexer);
        } else {
            advance(lexer);
        }
    }
    if (lexer->lookahead == quote) advance(lexer); // consume closing quote
}

// skip a template literal (`...`)
static void ts_skip_template(TSLexer *lexer) {
    advance(lexer); // consume opening backtick
    int depth = 0;
    while (lexer->lookahead != 0) {
        if (lexer->lookahead == '\\') {
            advance(lexer);
            if (lexer->lookahead != 0) advance(lexer);
        } else if (lexer->lookahead == '$') {
            advance(lexer);
            if (lexer->lookahead == '{') {
                advance(lexer);
                depth++;
            }
        } else if (lexer->lookahead == '}' && depth > 0) {
            advance(lexer);
            depth--;
        } else if (lexer->lookahead == '`') {
            if (depth == 0) {
                advance(lexer); // consume closing backtick
                return;
            }
            advance(lexer);
        } else {
            advance(lexer);
        }
    }
}

static bool ts_is_ident_start(int32_t ch) {
    return iswalpha(ch) || ch == '_' || ch == '$';
}

static bool ts_is_ident_char(int32_t ch) {
    return iswalnum(ch) || ch == '_' || ch == '$';
}

// skip an identifier or keyword
static void ts_skip_ident(TSLexer *lexer) {
    while (ts_is_ident_char(lexer->lookahead)) {
        advance(lexer);
    }
}

// check if current lookahead sequence matches a keyword, without consuming
// returns true if the next chars match `kw` and the char after is not an ident char
static bool ts_check_keyword(TSLexer *lexer, const char *kw) {
    // We can't look ahead multiple chars without advancing.
    // Instead, we consume the identifier and check if it matches.
    // But we can't "unconsume". So we just need to handle this differently.
    // This function is only called after we've already confirmed ident start.
    // We'll handle keywords inline in the scanner by consuming the ident and checking.
    (void)lexer;
    (void)kw;
    return false; // placeholder — keywords handled inline
}

// ================================================================
// v2: scan_ts_type — opaque type expression scanner
// ================================================================

// Scan an opaque type expression. The grammar ensures this is only entered
// in type position (after ':', after '=', after 'as', 'satisfies', 'extends',
// 'implements', etc.).
//
// Strategy: bracket-balance ()  <>  []  {}  and consume everything as a single
// token. Terminate at depth-0 delimiters that end a type expression.
//
// At depth 0, these CONTINUE the type: | & => (after ')') [] (array suffix)
// At depth 0, these STOP the type: , ) ] ; = { (when preceded by non-type context)
//   and expression-only operators: || && ?? + - * / %
static bool scan_ts_type(TSLexer *lexer) {
    lexer->result_symbol = TS_TYPE;

    ts_skip_ws_and_comments(lexer);

    if (lexer->lookahead == 0) return false;

    // don't match empty types — need at least one character
    bool has_content = false;
    int depth = 0;    // bracket depth for ( [ {
    int angle = 0;    // separate angle bracket depth for < >

    // track if last significant token was a close paren (for => disambiguation)
    bool after_close_paren = false;

    // track if { would be a valid object type start
    // true at: start of type, after | & => ? :
    // false after: identifier, ), ], >, number, string, etc.
    bool brace_starts_type = true;

    // track conditional type depth: incremented at '?', decremented at ':'
    // so that ':' inside conditional type continues rather than terminates
    int conditional_depth = 0;

    for (;;) {
        ts_skip_ws_and_comments(lexer);

        int32_t ch = lexer->lookahead;

        if (ch == 0) {
            break;
        }

        // string literals within type context (e.g. literal type "hello")
        if (ch == '\'' || ch == '"') {
            ts_skip_string(lexer);
            lexer->mark_end(lexer);
            has_content = true;
            after_close_paren = false;
            brace_starts_type = false;
            continue;
        }

        // template literal types (e.g. `hello ${string}`)
        if (ch == '`') {
            ts_skip_template(lexer);
            lexer->mark_end(lexer);
            has_content = true;
            after_close_paren = false;
            brace_starts_type = false;
            continue;
        }

        // opening brackets — increase depth
        if (ch == '(') {
            depth++;
            advance(lexer);
            has_content = true;
            after_close_paren = false;
            brace_starts_type = true; // inside parens, { can start object type
            continue;
        }
        if (ch == '[') {
            depth++;
            advance(lexer);
            has_content = true;
            after_close_paren = false;
            brace_starts_type = true;
            continue;
        }
        if (ch == '{') {
            if (depth == 0 && angle == 0) {
                if (!brace_starts_type) {
                    // { after a complete type atom at depth 0 — this is NOT an object type
                    // it's a function body, block statement, etc.
                    break;
                }
                // { at start of type or after type operator — object type
            }
            depth++;
            advance(lexer);
            has_content = true;
            after_close_paren = false;
            brace_starts_type = true; // inside braces, nested { can start object type
            continue;
        }
        if (ch == '<') {
            angle++;
            advance(lexer);
            has_content = true;
            after_close_paren = false;
            brace_starts_type = true;
            continue;
        }

        // closing brackets — decrease depth
        if (ch == ')') {
            if (depth <= 0) {
                break;
            }
            depth--;
            advance(lexer);
            lexer->mark_end(lexer);
            after_close_paren = true;
            brace_starts_type = false;
            continue;
        }
        if (ch == ']') {
            if (depth <= 0) {
                break;
            }
            depth--;
            advance(lexer);
            lexer->mark_end(lexer);
            after_close_paren = false;
            brace_starts_type = false;
            continue;
        }
        if (ch == '}') {
            if (depth <= 0) {
                break;
            }
            depth--;
            advance(lexer);
            lexer->mark_end(lexer);
            after_close_paren = false;
            brace_starts_type = false;
            continue;
        }
        if (ch == '>') {
            if (angle > 0) {
                angle--;
                advance(lexer);
                lexer->mark_end(lexer);
                has_content = true;
                after_close_paren = false;
                brace_starts_type = false;
                // handle >> as two closes
                if (angle > 0 && lexer->lookahead == '>') {
                    angle--;
                    advance(lexer);
                    lexer->mark_end(lexer);
                }
                continue;
            }
            if (depth > 0) {
                // inside brackets but no angle brackets open — consume >
                // handles => inside parenthesized function types: ((x: T) => R)
                advance(lexer);
                has_content = true;
                after_close_paren = false;
                continue;
            }
            // depth 0, angle 0 > — end of type
            break;
        }

        // at top level (depth 0), check for type terminators vs continuators
        if (depth == 0 && angle == 0) {
            // type continuation: | and &
            if (ch == '|') {
                advance(lexer);
                if (lexer->lookahead == '|') {
                    // || is logical OR — expression operator, stop
                    break;
                }
                // single | — union type, continue
                has_content = true;
                after_close_paren = false;
                brace_starts_type = true; // { after | starts object type
                continue;
            }
            if (ch == '&') {
                advance(lexer);
                if (lexer->lookahead == '&') {
                    // && is logical AND — expression operator, stop
                    break;
                }
                // single & — intersection type, continue
                has_content = true;
                after_close_paren = false;
                brace_starts_type = true; // { after & starts object type
                continue;
            }

            // => after close paren — function type return, continue
            if (ch == '=' && after_close_paren) {
                advance(lexer);
                if (lexer->lookahead == '>') {
                    advance(lexer);
                    has_content = true;
                    after_close_paren = false;
                    brace_starts_type = true; // { after => starts object type return
                    continue;
                }
                // plain = — end of type
                break;
            }

            // structural delimiters that end a type
            if (ch == ',' || ch == ';') {
                break;
            }
            if (ch == '=') {
                break;
            }

            // expression-only operators at depth 0 — end of type
            if (ch == '?') {
                advance(lexer);
                if (lexer->lookahead == '.' || lexer->lookahead == '?') {
                    // ?. optional chain, ?? nullish coalescing — stop
                    break;
                }
                if (lexer->lookahead == ':') {
                    // ?: — stop (optional parameter)
                    break;
                }
                // standalone ? — conditional type: T extends U ? X : Y
                conditional_depth++;
                has_content = true;
                after_close_paren = false;
                brace_starts_type = true; // the consequence type can start with {
                continue;
            }
        }

        // inside brackets — consume everything
        if (depth > 0 || angle > 0) {
            if (ts_is_ident_start(ch)) {
                ts_skip_ident(lexer);
            } else {
                advance(lexer);
            }
            has_content = true;
            after_close_paren = false;
            continue;
        }

        // at depth 0 — identifiers and keywords
        if (ts_is_ident_start(ch)) {
            ts_skip_ident(lexer);
            lexer->mark_end(lexer);
            has_content = true;
            after_close_paren = false;
            brace_starts_type = false; // after an identifier, { is not a type
            continue;
        }

        // numeric literals in types (literal type: 42, -1, etc.)
        if (iswdigit(ch)) {
            while (iswdigit(lexer->lookahead) || lexer->lookahead == '.' ||
                   lexer->lookahead == 'e' || lexer->lookahead == 'E' ||
                   lexer->lookahead == 'x' || lexer->lookahead == 'X' ||
                   lexer->lookahead == '_' || lexer->lookahead == 'n' ||
                   (lexer->lookahead >= 'a' && lexer->lookahead <= 'f') ||
                   (lexer->lookahead >= 'A' && lexer->lookahead <= 'F')) {
                advance(lexer);
            }
            lexer->mark_end(lexer);
            has_content = true;
            after_close_paren = false;
            brace_starts_type = false;
            continue;
        }

        // minus/plus at depth 0 — could be unary in literal type or binary expr operator
        if (ch == '-' || ch == '+') {
            advance(lexer);
            if (iswdigit(lexer->lookahead)) {
                while (iswdigit(lexer->lookahead) || lexer->lookahead == '.' ||
                       lexer->lookahead == 'e' || lexer->lookahead == 'E' ||
                       lexer->lookahead == '_' || lexer->lookahead == 'n') {
                    advance(lexer);
                }
                lexer->mark_end(lexer);
                has_content = true;
                after_close_paren = false;
                brace_starts_type = false;
                continue;
            }
            // bare +/- at top level — expression operator, stop
            break;
        }

        // . for dotted type paths (myModule.MyType)
        if (ch == '.') {
            advance(lexer);
            if (lexer->lookahead == '.' && false) {
                // future: handle spread
            }
            has_content = true;
            after_close_paren = false;
            brace_starts_type = false;
            continue;
        }

        // : at depth 0 — check conditional type context
        if (ch == ':') {
            if (conditional_depth > 0) {
                // inside conditional type: T extends U ? X : Y — continue
                conditional_depth--;
                advance(lexer);
                has_content = true;
                after_close_paren = false;
                brace_starts_type = true;
                continue;
            }
            break;
        }

        // * (existential type) or other misc single chars
        if (ch == '*') {
            advance(lexer);
            lexer->mark_end(lexer);
            has_content = true;
            after_close_paren = false;
            brace_starts_type = false;
            continue;
        }

        // anything else we don't recognize — stop
        break;
    }

    return has_content;
}

// ================================================================
// v2: scan_ts_type_arguments — <Type1, Type2> in expression context
// ================================================================

// Disambiguate < as start of type arguments vs comparison operator.
// Uses speculative scanning: scan <...>, then check what follows.
// If followed by ( ` . ?. , ) ] ; } = : — commit as type arguments.
// Otherwise, reject (it's a comparison).
static bool scan_ts_type_arguments(TSLexer *lexer) {
    lexer->result_symbol = TS_TYPE_ARGUMENTS;

    ts_skip_ws_and_comments(lexer);

    if (lexer->lookahead != '<') return false;

    advance(lexer); // consume <
    int depth = 1;

    while (lexer->lookahead != 0 && depth > 0) {
        int32_t ch = lexer->lookahead;

        if (ch == '<') {
            depth++;
            advance(lexer);
        } else if (ch == '>') {
            depth--;
            advance(lexer);
            if (depth == 0) {
                break;
            }
            // handle >> as two closes
            if (depth > 0 && lexer->lookahead == '>') {
                depth--;
                advance(lexer);
            }
        } else if (ch == '(') {
            // scan balanced parens
            int pdepth = 1;
            advance(lexer);
            while (lexer->lookahead != 0 && pdepth > 0) {
                if (lexer->lookahead == '(') pdepth++;
                else if (lexer->lookahead == ')') pdepth--;
                advance(lexer);
            }
        } else if (ch == '[') {
            int bdepth = 1;
            advance(lexer);
            while (lexer->lookahead != 0 && bdepth > 0) {
                if (lexer->lookahead == '[') bdepth++;
                else if (lexer->lookahead == ']') bdepth--;
                advance(lexer);
            }
        } else if (ch == '{') {
            int cdepth = 1;
            advance(lexer);
            while (lexer->lookahead != 0 && cdepth > 0) {
                if (lexer->lookahead == '{') cdepth++;
                else if (lexer->lookahead == '}') cdepth--;
                advance(lexer);
            }
        } else if (ch == '\'' || ch == '"') {
            ts_skip_string(lexer);
        } else if (ch == '`') {
            ts_skip_template(lexer);
        } else if (ch == ';' || ch == 0) {
            // definitely not type arguments
            return false;
        } else {
            advance(lexer);
        }
    }

    if (depth != 0) {
        return false;
    }

    lexer->mark_end(lexer);

    // check what follows the closing >
    ts_skip_ws_and_comments(lexer);

    int32_t next = lexer->lookahead;

    // These characters confirm it was type arguments:
    //   ( — call: foo<T>(args)
    //   ` — template: foo<T>`template`
    //   . — member: foo<T>.bar
    //   , — comma: foo<T>, bar
    //   ) — close paren: (foo<T>)
    //   ] — close bracket: [foo<T>]
    //   ; — semicolon: foo<T>;
    //   } — close brace: { foo<T> }
    //   = — assignment: x = foo<T>
    //   > — comparison/close: used in nested generics
    //   ? — optional chain or ternary
    //   : — ternary consequence
    if (next == '(' || next == '`' || next == '.' ||
        next == ',' || next == ')' || next == ']' ||
        next == ';' || next == '}' || next == '=' ||
        next == '>' || next == '?' || next == ':') {
        return true;
    }

    // also if end of input
    if (next == 0) return true;

    // not type arguments — it's a comparison
    return false;
}

// ================================================================
// v2: scan_ts_type_parameters — <T, U extends V> in declaration context
// ================================================================

// Simple bracket-balanced scan. In declaration context (after function/class/interface name),
// < is unambiguously the start of type parameters.
static bool scan_ts_type_parameters(TSLexer *lexer) {
    lexer->result_symbol = TS_TYPE_PARAMETERS;

    ts_skip_ws_and_comments(lexer);

    if (lexer->lookahead != '<') return false;

    advance(lexer); // consume <
    int depth = 1;

    while (lexer->lookahead != 0 && depth > 0) {
        int32_t ch = lexer->lookahead;

        if (ch == '<') {
            depth++;
            advance(lexer);
        } else if (ch == '>') {
            depth--;
            advance(lexer);
            if (depth > 0 && lexer->lookahead == '>') {
                depth--;
                advance(lexer);
            }
        } else if (ch == '(') {
            int pdepth = 1;
            advance(lexer);
            while (lexer->lookahead != 0 && pdepth > 0) {
                if (lexer->lookahead == '(') pdepth++;
                else if (lexer->lookahead == ')') pdepth--;
                advance(lexer);
            }
        } else if (ch == '[') {
            int bdepth = 1;
            advance(lexer);
            while (lexer->lookahead != 0 && bdepth > 0) {
                if (lexer->lookahead == '[') bdepth++;
                else if (lexer->lookahead == ']') bdepth--;
                advance(lexer);
            }
        } else if (ch == '{') {
            int cdepth = 1;
            advance(lexer);
            while (lexer->lookahead != 0 && cdepth > 0) {
                if (lexer->lookahead == '{') cdepth++;
                else if (lexer->lookahead == '}') cdepth--;
                advance(lexer);
            }
        } else if (ch == '\'' || ch == '"') {
            ts_skip_string(lexer);
        } else if (ch == '`') {
            ts_skip_template(lexer);
        } else if (ch == ';') {
            // unexpected semicolon — bail
            return false;
        } else {
            advance(lexer);
        }
    }

    if (depth != 0) {
        return false;
    }

    lexer->mark_end(lexer);
    return true;
}

// ================================================================
// v2: Main scanner dispatch
// ================================================================

static inline bool external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    if (valid_symbols[TEMPLATE_CHARS]) {
        if (valid_symbols[AUTOMATIC_SEMICOLON]) {
            return false;
        }
        return scan_template_chars(lexer);
    }

    if (valid_symbols[JSX_TEXT] && scan_jsx_text(lexer)) {
        return true;
    }

    // v2: type tokens — check before ASI since type contexts are more specific
    // Check TS_TYPE first — it's the most common type token
    if (valid_symbols[TS_TYPE] &&
        !valid_symbols[AUTOMATIC_SEMICOLON] &&
        !valid_symbols[TEMPLATE_CHARS]) {
        return scan_ts_type(lexer);
    }

    // Type parameters — unambiguous in declaration context
    if (valid_symbols[TS_TYPE_PARAMETERS] &&
        !valid_symbols[AUTOMATIC_SEMICOLON] &&
        !valid_symbols[TEMPLATE_CHARS]) {
        return scan_ts_type_parameters(lexer);
    }

    // Type arguments — needs disambiguation, only when grammar says it's valid
    if (valid_symbols[TS_TYPE_ARGUMENTS] &&
        !valid_symbols[AUTOMATIC_SEMICOLON] &&
        !valid_symbols[TEMPLATE_CHARS]) {
        if (lexer->lookahead == '<' || (iswspace(lexer->lookahead))) {
            return scan_ts_type_arguments(lexer);
        }
    }

    if (valid_symbols[AUTOMATIC_SEMICOLON] || valid_symbols[FUNCTION_SIGNATURE_AUTOMATIC_SEMICOLON]) {
        bool scanned_comment = false;
        bool ret = scan_automatic_semicolon(lexer, valid_symbols, &scanned_comment);
        if (!ret && !scanned_comment && valid_symbols[TERNARY_QMARK] && lexer->lookahead == '?') {
            return scan_ternary_qmark(lexer);
        }
        return ret;
    }
    if (valid_symbols[TERNARY_QMARK]) {
        return scan_ternary_qmark(lexer);
    }

    if (valid_symbols[HTML_COMMENT] && !valid_symbols[LOGICAL_OR] && !valid_symbols[ESCAPE_SEQUENCE] &&
        !valid_symbols[REGEX_PATTERN]) {
        return scan_closing_comment(lexer);
    }

    return false;
}
