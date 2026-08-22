#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// External scanner for tree-sitter-lambda — S16 Surface Syntax guards.
//
// Role (see vibe/Lambda_Design_Syntax.md §4.4): Tree-sitter is Lambda's
// OFFICIAL GRAMMAR and cross-checking reference; the C recursive-descent
// parser in lambda/runtime/parser/ is production. Because parse-table size no
// longer constrains this grammar, the former sub-language extraction tokens
// (type patterns, view patterns, path bodies) are gone — those are ordinary
// grammar rules again. What CANNOT be expressed in grammar rules is newline
// awareness: `/\s/` lives in `extras` and is invisible to the parser. That is
// this scanner's whole remaining job.
//
// It emits three zero-width guards, all pure functions of the lookahead
// position (no state, so serialize/deserialize stay empty and incremental
// parsing is safe):
//
//   JOIN           the previous expression CONTINUES here (S16.2.2/S16.2.3)
//   STMT_BOUNDARY  a new statement STARTS here            (S16.1.3)
//   NOT_PAREN      the next token is not '('              (S16.6.2, §7.7)
//
// JOIN and STMT_BOUNDARY are mutually exclusive by construction: JOIN is
// emitted only before a DUAL-role token, STMT_BOUNDARY only before a START
// token. That disjointness is what lets both be valid in the same parse state
// without the scanner having to guess which the parser wants. When a line
// starts with a dual-role token, NEITHER is emitted, so both the continuation
// and the new-statement path are blocked and the parse fails loudly — which is
// exactly S16.2.3 ("neither reading wins by default").
//
// The scanner is compiled standalone by the package's language bindings, so it
// must not reach into the Lambda runtime, and tree-sitter may run it
// speculatively during GLR ambiguity and error recovery, so it stays free of
// side effects.

enum TokenType {
    // Guarded operator tokens. Each CONSUMES its own lexeme and is emitted only
    // when the operator sits on the same line as its left operand. They are
    // separate tokens rather than one zero-width guard because a zero-width
    // marker would push the precedence-deciding token two symbols out of
    // lookahead range and break LR(1) resolution for the operator tiers.
    BIN_PLUS,
    BIN_MINUS,
    BIN_STAR,
    BIN_SLASH,
    BIN_LT,
    CALL_LPAREN,
    INDEX_LBRACKET,
    MEMBER_DOT,
    POSTFIX_CARET,
    STMT_BOUNDARY,
    NOT_PAREN,
    // Never emitted. Tree-sitter marks every external token valid during error
    // recovery; this sentinel is valid nowhere in the grammar, so seeing it
    // means recovery is running and the scanner should decline.
    ERROR_SENTINEL,
};

void *tree_sitter_lambda_external_scanner_create(void) {
    return NULL;
}

void tree_sitter_lambda_external_scanner_destroy(void *payload) {
    (void)payload;
}

unsigned tree_sitter_lambda_external_scanner_serialize(void *payload, char *buffer) {
    (void)payload;
    (void)buffer;
    return 0;
}

void tree_sitter_lambda_external_scanner_deserialize(
    void *payload, const char *buffer, unsigned length) {
    (void)payload;
    (void)buffer;
    (void)length;
}

static bool is_space(int32_t ch) {
    return ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v' ||
        ch == '\r' || ch == '\n';
}

static bool is_identifier_start(int32_t ch) {
    return ch == '$' || ch == '_' ||
        (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch >= 0x80;
}

static bool is_identifier_continue(int32_t ch) {
    return is_identifier_start(ch) || (ch >= '0' && ch <= '9');
}

static bool is_digit(int32_t ch) {
    return ch >= '0' && ch <= '9';
}

// S16.2.2 continuation words: they cannot begin a statement, so a line may
// start with one and the expression simply continues. `else`, `case`, and
// `default` join the operator words because they continue their enclosing
// construct; the `for`-clause words (`where`, `group`, `by`, `order`, `asc`,
// `desc`, `limit`, `offset`, `into`) and the view-declaration `on` are here for
// the same reason — each is only ever a continuation of the form it belongs to.
static bool is_continuation_word(const char *w, unsigned n) {
    switch (n) {
        case 2:
            return !strcmp(w, "or") || !strcmp(w, "to") || !strcmp(w, "in") ||
                !strcmp(w, "is") || !strcmp(w, "at") || !strcmp(w, "eq") ||
                !strcmp(w, "ne") || !strcmp(w, "lt") || !strcmp(w, "le") ||
                !strcmp(w, "ge") || !strcmp(w, "gt") || !strcmp(w, "by") ||
                !strcmp(w, "on");
        case 3:
            return !strcmp(w, "and") || !strcmp(w, "div") || !strcmp(w, "asc");
        case 4:
            return !strcmp(w, "that") || !strcmp(w, "else") ||
                !strcmp(w, "case") || !strcmp(w, "desc") || !strcmp(w, "into");
        case 5:
            return !strcmp(w, "where") || !strcmp(w, "group") ||
                !strcmp(w, "order") || !strcmp(w, "limit");
        case 6:
            return !strcmp(w, "offset");
        case 7:
            return !strcmp(w, "default");
        default:
            return false;
    }
}

// True when the token at the lookahead position can only BEGIN a statement —
// never continue the preceding expression. Everything dual-role (`( [ - + * ^
// / < .`) and everything continuation-only (`|> | & % > = ! == != <= >=`, the
// word operators) returns false. The caller has already marked the token end,
// so every advance here is pure inspection.
static bool classify_start(TSLexer *lexer) {
    int32_t c = lexer->lookahead;
    if (lexer->eof(lexer)) { return false; }

    switch (c) {
        // continuation-only, dual-role, closers, and separators alike: none of
        // them opens a statement.
        case '|': case '&': case '%': case '?': case '>': case '=': case '!':
        case '-': case '(': case '[': case '^': case '/': case '<': case '.':
        case ')': case ']': case '}': case ',': case ';': case ':':
            return false;
        // `+` and `*` are dual-role as prefixes, so they never open a
        // juxtaposed statement either.
        case '+': case '*':
            return false;
        case '{': case '~': case '"': case '\'': case '\\':
            return true;
        default: break;
    }

    if (is_digit(c)) { return true; }

    if (is_identifier_start(c)) {
        char word[16];
        unsigned n = 0;
        while (is_identifier_continue(lexer->lookahead)) {
            if (n + 1 < sizeof(word)) { word[n] = (char)lexer->lookahead; }
            n++;
            lexer->advance(lexer, false);
        }
        if (n >= sizeof(word)) { return true; }  // too long to be a keyword
        word[n] = '\0';
        return !is_continuation_word(word, n);
    }

    return true;
}

// Emit a guarded operator token, consuming its lexeme. `reject_next` names a
// character that turns the operator into a different, UNGUARDED token (`++`,
// `**`, `<=`, `.?`): those can only ever continue an expression, so they are
// free to open a line and must be left to the internal lexer.
static bool emit_op(TSLexer *lexer, enum TokenType type, int32_t reject_next) {
    lexer->advance(lexer, false);
    if (reject_next && lexer->lookahead == reject_next) { return false; }
    lexer->mark_end(lexer);
    lexer->result_symbol = type;
    return true;
}

bool tree_sitter_lambda_external_scanner_scan(
    void *payload, TSLexer *lexer, const bool *valid_symbols) {
    (void)payload;

    // Decline during error recovery: the guards only make sense against a real
    // parse state, and recovery marks every external valid.
    if (valid_symbols[ERROR_SENTINEL]) { return false; }

    // Skip whitespace and comments, remembering whether a line break was
    // crossed. This is the one thing grammar rules cannot see for themselves.
    bool saw_newline = false;
    bool slash_pending = false;
    for (;;) {
        while (is_space(lexer->lookahead)) {
            if (lexer->lookahead == '\n') { saw_newline = true; }
            lexer->advance(lexer, true);
        }
        // Mark the zero-width position BEFORE inspecting further, so a `/` that
        // turns out to be division rather than a comment does not end up inside
        // a zero-width token.
        lexer->mark_end(lexer);
        if (lexer->lookahead != '/') { break; }
        lexer->advance(lexer, true);
        if (lexer->lookahead == '/') {
            while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
                lexer->advance(lexer, true);
            }
            continue;
        }
        if (lexer->lookahead == '*') {
            lexer->advance(lexer, true);
            int32_t prev = 0;
            while (!lexer->eof(lexer)) {
                if (lexer->lookahead == '\n') { saw_newline = true; }
                if (prev == '*' && lexer->lookahead == '/') {
                    lexer->advance(lexer, true);
                    prev = 0;
                    break;
                }
                prev = lexer->lookahead;
                lexer->advance(lexer, true);
            }
            if (prev == 0) { continue; }
            return false;  // unterminated block comment
        }
        // A real `/`: division, or the root path step. The scanner has already
        // advanced past it, and mark_end sits before it.
        slash_pending = true;
        break;
    }

    // NOT_PAREN gates the bare spelling of `if`/`while` heads (S16.6.2) and the
    // bare `apply` statement (§7.7). It is only valid where the parser is
    // choosing between a parenthesized form and a bare one, so it never
    // competes with the operator guards.
    if (valid_symbols[NOT_PAREN] && (slash_pending || lexer->lookahead != '(')) {
        lexer->result_symbol = NOT_PAREN;
        return true;
    }
    // A `(` here means NOT_PAREN does not apply — but the state may still want
    // a call guard (`apply(x)` is an ordinary call, `apply` alone is the bare
    // statement), so fall through rather than declining outright.

    if (slash_pending) {
        // `/` is dual-role: division (continuation) or a rooted path step
        // (start). Only the same-line division reading is guarded here.
        if (valid_symbols[BIN_SLASH] && !saw_newline) {
            lexer->mark_end(lexer);
            lexer->result_symbol = BIN_SLASH;
            return true;
        }
        return false;
    }

    int32_t c = lexer->lookahead;

    // --- guarded operators: same line only --------------------------------
    if (!saw_newline) {
        switch (c) {
            case '+':
                if (valid_symbols[BIN_PLUS]) { return emit_op(lexer, BIN_PLUS, '+'); }
                break;
            case '-':
                if (valid_symbols[BIN_MINUS]) { return emit_op(lexer, BIN_MINUS, 0); }
                break;
            case '*':
                if (valid_symbols[BIN_STAR]) { return emit_op(lexer, BIN_STAR, '*'); }
                break;
            case '<':
                if (valid_symbols[BIN_LT]) { return emit_op(lexer, BIN_LT, '='); }
                break;
            case '(':
                if (valid_symbols[CALL_LPAREN]) { return emit_op(lexer, CALL_LPAREN, 0); }
                break;
            case '[':
                if (valid_symbols[INDEX_LBRACKET]) { return emit_op(lexer, INDEX_LBRACKET, 0); }
                break;
            case '^':
                if (valid_symbols[POSTFIX_CARET]) { return emit_op(lexer, POSTFIX_CARET, 0); }
                break;
            default: break;
        }
    }

    // `.` member access. S16.2.4v2 (§7.15): now that the relative path is
    // spelled `\.`, a `.` followed by an identifier has no start reading left —
    // member access is its only meaning — so it continues across a line break
    // for ANY member, not just the `.ident(` call form. That is what enables
    // full leading-dot fluent chains. `.digit` remains dual-role, because
    // `a.5` is member access with an integer field while `.5` is a float.
    if (c == '.' && valid_symbols[MEMBER_DOT]) {
        lexer->advance(lexer, false);
        int32_t after = lexer->lookahead;
        if (after == '?') { return false; }              // `.?` query operator
        // `.digit` stays dual-role: same line it is the integer member field,
        // across a break it could equally be a float literal starting a
        // statement, so neither reading may win.
        if (is_digit(after) && saw_newline) { return false; }
        lexer->mark_end(lexer);
        if (saw_newline && !is_identifier_start(after)) { return false; }
        lexer->result_symbol = MEMBER_DOT;
        return true;
    }

    // --- statement juxtaposition (S16.1.3) --------------------------------
    // Emitted only before a START token, which is disjoint from every guarded
    // operator above. When a line opens with a dual-role token, NEITHER a guard
    // nor this boundary is emitted, so both readings are blocked and the parse
    // fails loudly — S16.2.3, "neither reading wins by default".
    if (valid_symbols[STMT_BOUNDARY] && classify_start(lexer)) {
        lexer->result_symbol = STMT_BOUNDARY;
        return true;
    }

    return false;
}
