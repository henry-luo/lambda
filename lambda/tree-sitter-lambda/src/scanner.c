#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
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
    // ORDER IS LOAD-BEARING: this enum must match grammar.js `externals`
    // element for element, or every token id shifts.
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
    // PTH40 (S16.2.3v3): `&` gained a PREFIX role (address-of) beside its infix
    // set-intersection one, so it joined the dual-role set and needs the same
    // same-line guard `+` and `-` have.
    BIN_AMP,
    CALL_LPAREN,
    INDEX_LBRACKET,
    // S11.1.6v2: `{` opens a counted occurrence (`int{2,4}`) and also starts a
    // map type and a block. Zero-width, emitted before a `{` bound tight to a
    // type and holding an integer -- the C parser's test.
    OCCURRENCE_LBRACE,
    MEMBER_DOT,
    POSTFIX_CARET,
    STMT_BOUNDARY,
    // S16.5.1: inside an element, `<` is never an operator — it always opens a
    // child — so it starts a juxtaposed content item where at statement level
    // it would be dual-role. Element content therefore needs its own boundary
    // token; the two differ only in how `<` is classified.
    ELEM_STMT_BOUNDARY,
    NOT_PAREN,
    // §7.16: emitted (zero-width) after a numeric literal only when the very
    // next character cannot continue an identifier. Withholding it makes
    // `123abc` and `0b1010` LEXICAL errors instead of silent splits into a
    // number plus a juxtaposed statement.
    NUM_BOUNDARY,
    // S16.6.6: emitted (zero-width) before an EXPRESSION BODY only when the
    // word there is not `return`/`break`/`continue`. Those are statements, and
    // the four unbraced body positions are expression positions. Without this
    // the word-rule fallback lexes them as plain identifiers wherever the
    // keyword token is not valid, so `if (c) return -1` silently misparsed as
    // a subtraction from a variable named `return` (LR02-12). Scoped to those
    // four positions, not to every identifier, to keep the scanner's blast
    // radius small (see the §7.17 note on scanner fragility).
    EXPR_BODY_START,
    // S2.4.1v2: emitted (zero-width) after a bare path root `/` or `\` only when
    // whitespace, a step (`.`, `[`), a separator, a closer, or the end follows.
    // Withholding it makes `/b` -- the retired `/a` spelling -- an error instead
    // of the root plus a silently juxtaposed statement `b`.
    ROOT_BOUNDARY,
    // S11.1.5v2 / S16.2.3v3: zero-width, emitted right after a signature's `)`
    // when its return type opens on the same line.
    FN_RETURN,
    // Never emitted: grammar.js `_misplaced_let` uses it as the dead end that
    // makes a `let` outside a parenthesized list an error.
    LET_OUTSIDE_LIST,
    // Never emitted. Tree-sitter marks every external token valid during error
    // recovery; this sentinel is valid nowhere in the grammar, so seeing it
    // means recovery is running and the scanner should decline.
    ERROR_SENTINEL,
};

// The statement keywords S16.6.6 bars from an unbraced expression body.
static bool is_control_statement_word(const char *word, unsigned n) {
    switch (n) {
        case 5: return memcmp(word, "break", 5) == 0;
        case 6: return memcmp(word, "return", 6) == 0;
        case 8: return memcmp(word, "continue", 8) == 0;
        default: return false;
    }
}

void *tree_sitter_lambda_external_scanner_create(void) {
    return NULL;  // stateless by design; see the §7.17 note below
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

static bool is_hex_digit(int32_t ch) {
    return is_digit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

// S11.1.6v2: true when the text after a `{` begins with an INTEGER token, the
// C parser's `parser_at_counted_run` test, read with the C lexer's number
// rules: digits (`_` only between two) or `0x` hex, not continued into a
// fraction, an exponent, or an imaginary, decimal or sized suffix. Pure
// inspection past the token end, so the caller keeps its own mark.
static bool brace_holds_integer(TSLexer *lexer) {
    while (is_space(lexer->lookahead)) { lexer->advance(lexer, false); }
    if (!is_digit(lexer->lookahead)) { return false; }
    bool hex = false;
    if (lexer->lookahead == '0') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == 'x' || lexer->lookahead == 'X') {
            hex = true;
            lexer->advance(lexer, false);
        }
    }
    for (;;) {
        if (hex ? is_hex_digit(lexer->lookahead) : is_digit(lexer->lookahead)) {
            lexer->advance(lexer, false);
        } else if (lexer->lookahead == '_') {
            lexer->advance(lexer, false);
            // C ends the literal before a `_` no digit follows: still INTEGER
            if (!(hex ? is_hex_digit(lexer->lookahead) : is_digit(lexer->lookahead))) {
                return true;
            }
        } else {
            break;
        }
    }
    int32_t c = lexer->lookahead;
    if (c == 'j' || c == 'n' || c == 'm') { return false; }
    // A fraction or exponent (decimal digits only), or a sized suffix, followed
    // by a digit makes the number a different token.
    bool fraction = !hex && (c == '.' || c == 'e' || c == 'E');
    if (fraction || c == 'i' || c == 'u' || c == 'f') {
        lexer->advance(lexer, false);
        if ((c == 'e' || c == 'E') &&
                (lexer->lookahead == '+' || lexer->lookahead == '-')) {
            lexer->advance(lexer, false);
        }
        return !is_digit(lexer->lookahead);
    }
    return true;
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

// The rest of the C lexer's keyword table (lambda_lexer.c) that names no type,
// plus the named values. With the continuation and control words and `as`,
// these are the words a signature's return type cannot open with; `type`,
// `fn`, `pn` and the base-type names can (S11.1.5v2).
static bool is_statement_keyword(const char *w, unsigned n) {
    static const char *const words[] = {
        "let", "pub", "var", "view", "edit", "state", "if", "match", "for",
        "while", "raise", "import", "put", "del", "commit", "rollback", "open",
        "apply", "not", "last", "true", "false", "inf", "nan",
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        if (strlen(words[i]) == n && memcmp(words[i], w, n) == 0) { return true; }
    }
    return false;
}

// Reads the word at the lookahead into `word`, NUL-terminated when it fits
// (longer words are never keywords), and returns its full length. Pure
// inspection past the caller's mark.
static unsigned read_word(TSLexer *lexer, char word[16]) {
    unsigned n = 0;
    while (is_identifier_continue(lexer->lookahead)) {
        if (n + 1 < 16) { word[n] = (char)lexer->lookahead; }
        n++;
        lexer->advance(lexer, false);
    }
    if (n < 16) { word[n] = '\0'; }
    return n;
}

// S16.1.3: no statement begins with a return type and `=>`; that text only
// ends an arrow head (`() int => 1`), where the empty list `()` shares the
// arrow's parse state, so a boundary here would cut the arrow off. The name is
// already consumed: read the rest of a return-type run -- suffixes, `|` `&`
// `!` alternatives, a `^` error arm -- and report whether `=>` closes it, the
// probe C's `arrow_head_candidate` makes. Pure inspection past the mark.
static bool name_opens_arrow_return(TSLexer *lexer) {
    for (;;) {
        for (;;) {
            int32_t c = lexer->lookahead;
            if (c == '?' || c == '+' || c == '*') {
                lexer->advance(lexer, false);
            } else if (c == '[') {
                lexer->advance(lexer, false);
                while (is_digit(lexer->lookahead) || is_space(lexer->lookahead)) {
                    lexer->advance(lexer, false);
                }
                if (lexer->lookahead != ']') { return false; }
                lexer->advance(lexer, false);
            } else {
                break;
            }
        }
        while (is_space(lexer->lookahead)) { lexer->advance(lexer, false); }
        int32_t c = lexer->lookahead;
        if (c == '=') {
            lexer->advance(lexer, false);
            return lexer->lookahead == '>';
        }
        if (c != '|' && c != '&' && c != '!' && c != '^') { return false; }
        lexer->advance(lexer, false);
        while (is_space(lexer->lookahead)) { lexer->advance(lexer, false); }
        if (c == '^' && lexer->lookahead == '=') {  // `T^ =>`: any error
            lexer->advance(lexer, false);
            return lexer->lookahead == '>';
        }
        if (!is_identifier_start(lexer->lookahead)) { return false; }
        while (is_identifier_continue(lexer->lookahead)) { lexer->advance(lexer, false); }
    }
}

// True when the token at the lookahead position can only BEGIN a statement —
// never continue the preceding expression. Everything dual-role (`( [ - + * ^
// / < .`) and everything continuation-only (`|> | & % > = ! == != <= >=`, the
// word operators) returns false. The caller has already marked the token end,
// so every advance here is pure inspection.
static bool classify_start(TSLexer *lexer, bool element_scope) {
    int32_t c = lexer->lookahead;
    if (lexer->eof(lexer)) { return false; }
    // S16.5.1: `<` opens a child element in element scope, so it starts an item
    // there even though it is dual-role everywhere else.
    if (c == '<' && element_scope) { return true; }

    switch (c) {
        // continuation-only, dual-role, closers, and separators alike: none of
        // them opens a statement.
        case '|': case '&': case '%': case '?': case '>': case '=': case '!':
        case '-': case '(': case '[': case '^': case '/': case '<': case '.':
        case ')': case ']': case '}': case ',': case ';': case ':':
        // PTH39 (S16.2.2v3): `#` is the force step and has no prefix role, so
        // it can only CONTINUE — a line beginning `#name` extends the chain
        // above it rather than opening a statement.
        case '#':
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
        unsigned n = read_word(lexer, word);
        if (n < sizeof(word) && is_continuation_word(word, n)) { return false; }
        return !name_opens_arrow_return(lexer);
    }

    return true;
}

// A statement starts at the mark: emit whichever boundary the state allows,
// for a caller that has already read past the start token.
static bool emit_statement_boundary(TSLexer *lexer, const bool *valid_symbols) {
    if (valid_symbols[ELEM_STMT_BOUNDARY]) {
        lexer->result_symbol = ELEM_STMT_BOUNDARY;
        return true;
    }
    if (valid_symbols[STMT_BOUNDARY]) {
        lexer->result_symbol = STMT_BOUNDARY;
        return true;
    }
    return false;
}

// Emit a guarded operator token, consuming its lexeme. A rejected following
// character turns it into a different, unguarded token (`++`, `**`, `<=`,
// `<:`, `.?`): the internal lexer owns that longer spelling.
static bool emit_op(TSLexer *lexer, enum TokenType type, int32_t reject_first,
        int32_t reject_second) {
    lexer->advance(lexer, false);
    if ((reject_first && lexer->lookahead == reject_first) ||
            (reject_second && lexer->lookahead == reject_second)) {
        return false;
    }
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

    // §7.16 must inspect the character IMMEDIATELY after the literal, so it is
    // tested before any whitespace is skipped.
    if (valid_symbols[NUM_BOUNDARY]) {
        if (is_identifier_start(lexer->lookahead)) { return false; }
        lexer->mark_end(lexer);
        lexer->result_symbol = NUM_BOUNDARY;
        return true;
    }

    // The same adjacency test after a bare path root, mirroring the C
    // parser's `parse_path_slot`: `.` and `[` begin a step (though `.?` is a
    // query, not a step), and whitespace, separators, closers and the end
    // finish the path. Anything else touching the root is rejected.
    if (valid_symbols[ROOT_BOUNDARY]) {
        int32_t c = lexer->lookahead;
        lexer->mark_end(lexer);
        if (c == '.') {
            lexer->advance(lexer, false);
            if (lexer->lookahead == '?') { return false; }
        } else if (!(lexer->eof(lexer) || is_space(c) || c == '[' || c == ';' ||
                c == ',' || c == ')' || c == ']' || c == '}' || c == '>')) {
            return false;
        }
        lexer->result_symbol = ROOT_BOUNDARY;
        return true;
    }

    // Skip whitespace and comments, remembering whether a line break was
    // crossed. This is the one thing grammar rules cannot see for themselves.
    bool saw_newline = false;
    // Whether the byte right before the token is whitespace: the C parser's
    // test for a `{` bound tight to a type (S11.1.6v2).
    bool space_before = false;
    bool slash_pending = false;
    for (;;) {
        while (is_space(lexer->lookahead)) {
            if (lexer->lookahead == '\n') { saw_newline = true; }
            space_before = true;
            lexer->advance(lexer, true);
        }
        // Mark the zero-width position BEFORE inspecting further, so a `/` that
        // turns out to be division rather than a comment does not end up inside
        // a zero-width token.
        lexer->mark_end(lexer);
        if (lexer->lookahead != '/') { break; }
        // Not skipped: if this turns out to open a comment the character must
        // be inside the emitted token, and if it turns out to be division the
        // mark_end above already sits in front of it.
        lexer->advance(lexer, false);
        if (lexer->lookahead == '/') {
            while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
                lexer->advance(lexer, true);
            }
            continue;
        }
        if (lexer->lookahead == '*') {
            lexer->advance(lexer, true);
            int32_t prev = 0;
            bool closed = false;
            while (!lexer->eof(lexer)) {
                if (lexer->lookahead == '\n') { saw_newline = true; }
                if (prev == '*' && lexer->lookahead == '/') {
                    lexer->advance(lexer, true);
                    closed = true;
                    break;
                }
                prev = lexer->lookahead;
                lexer->advance(lexer, true);
            }
            space_before = false;  // the byte before the token is the `/`
            if (closed) { continue; }
            return false;  // unterminated block comment
        }
        // A real `/`: division, or the root path step. The scanner has already
        // advanced past it, and mark_end sits before it.
        slash_pending = true;
        break;
    }

    // S16.6.6: veto an unbraced expression body that starts with a statement
    // keyword. Zero-width and stateless — a pure function of the lookahead —
    // so it carries none of the §7.17 carry-stale-state hazard. Placed after
    // whitespace/comment skipping because the keyword follows a space
    // (`if (c) return`), and mark_end above already fixed the zero-width
    // position. Withholding the token kills the expression-body alternative,
    // which is exactly the rejection S16.6.6 requires.
    if (valid_symbols[EXPR_BODY_START] && !slash_pending) {
        if (is_identifier_start(lexer->lookahead)) {
            char word[16];
            unsigned n = read_word(lexer, word);
            if (n < sizeof(word) && is_control_statement_word(word, n)) { return false; }
        }
        lexer->result_symbol = EXPR_BODY_START;
        return true;
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

    // S11.1.5v2 / S16.2.3v3: right after a signature's `)`, a name -- or
    // `fn`/`pn` -- opens its optional return type, on the same line. Opening
    // the next line it could as well begin a new statement, so neither reading
    // is taken: no token, and the parse fails. A keyword that names no type
    // keeps its own reading (`as` is the parameter's binder).
    if (valid_symbols[FN_RETURN] && !slash_pending &&
            is_identifier_start(lexer->lookahead)) {
        char word[16];
        unsigned n = read_word(lexer, word);
        bool continues = n < sizeof(word) &&
            (is_continuation_word(word, n) || !strcmp(word, "as"));
        bool keyword = continues || (n < sizeof(word) &&
            (is_control_statement_word(word, n) || is_statement_keyword(word, n)));
        // `fn name`, `pn name` and `function name` are declaration heads, never
        // a type (C's `parser_at_declaration_head`); `function` needs its name
        // on the same line
        if (!keyword && n < sizeof(word) &&
                (!strcmp(word, "fn") || !strcmp(word, "pn") || !strcmp(word, "function"))) {
            bool same_line = !strcmp(word, "function");
            while (is_space(lexer->lookahead) && !(same_line && lexer->lookahead == '\n')) {
                lexer->advance(lexer, false);
            }
            keyword = is_identifier_start(lexer->lookahead) || lexer->lookahead == '\'';
        }
        if (!keyword) {
            if (saw_newline) { return false; }
            lexer->result_symbol = FN_RETURN;
            return true;
        }
        // a statement keyword starts the next statement, as classify_start says
        return continues ? false : emit_statement_boundary(lexer, valid_symbols);
    }

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
                if (valid_symbols[BIN_PLUS]) { return emit_op(lexer, BIN_PLUS, '+', 0); }
                break;
            case '-':
                if (valid_symbols[BIN_MINUS]) { return emit_op(lexer, BIN_MINUS, 0, 0); }
                break;
            case '*':
                if (valid_symbols[BIN_STAR]) { return emit_op(lexer, BIN_STAR, '*', 0); }
                break;
            case '<':
                // `<:` is the ordinary grammar token for S11.1.4v2, never a
                // guarded `<` followed by an orphaned colon.
                if (valid_symbols[BIN_LT]) { return emit_op(lexer, BIN_LT, '=', ':'); }
                break;
            case '&':
                if (valid_symbols[BIN_AMP]) { return emit_op(lexer, BIN_AMP, 0, 0); }
                break;
            case '(':
                if (valid_symbols[CALL_LPAREN]) { return emit_op(lexer, CALL_LPAREN, 0, 0); }
                break;
            case '[':
                if (valid_symbols[INDEX_LBRACKET]) { return emit_op(lexer, INDEX_LBRACKET, 0, 0); }
                break;
            case '{':
                // S11.1.6v2: a count binds tight and holds an integer
                // (`int{2}`), exactly as C's `parser_at_counted_run` reads it.
                // Any other brace after a type opens a body, a block or a map
                // (`if x is int { … }`, `type T = int {2}`), so it takes the
                // statement boundary a `{` start token would otherwise get.
                // Both answers need the text past the `{`, so each is
                // zero-width: the mark stays in front of the brace.
                if (valid_symbols[OCCURRENCE_LBRACE]) {
                    lexer->advance(lexer, false);
                    if (!space_before && brace_holds_integer(lexer)) {
                        lexer->result_symbol = OCCURRENCE_LBRACE;
                        return true;
                    }
                    return emit_statement_boundary(lexer, valid_symbols);
                }
                break;
            case '^':
                // §3.6: `^` is followed either by nothing (propagate) or by a
                // handler brace, so `{` after it is DUAL-ROLE. Decide here, at
                // the caret: blocking only the handler path would let GLR fall
                // through to propagate-plus-a-block-statement, which is the
                // silent split S16.1.1 forbids. A `{` on a later line yields
                // NEITHER token, so the parse fails loudly.
                if (valid_symbols[POSTFIX_CARET]) {
                    lexer->advance(lexer, false);
                    lexer->mark_end(lexer);
                    bool brace_newline = false;
                    while (is_space(lexer->lookahead)) {
                        if (lexer->lookahead == '\n') { brace_newline = true; }
                        lexer->advance(lexer, false);
                    }
                    if (lexer->lookahead == '{' && brace_newline) { return false; }
                    lexer->result_symbol = POSTFIX_CARET;
                    return true;
                }
                break;
            default: break;
        }
    }

    // `.` member access. S16.2.4v3 (§7.15): now that the relative path is
    // rooted at `\`, a `.` followed by an identifier has no start reading left —
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
        // S16.2.4v3: every other step has no start reading, so it continues
        // across the break -- a name, `'sym'`, `*`/`**`, `~~` or `/`.
        bool step_start = is_identifier_start(after) || after == '\'' ||
            after == '*' || after == '~' || after == '/';
        if (saw_newline && !step_start) { return false; }
        lexer->result_symbol = MEMBER_DOT;
        return true;
    }

    // --- statement juxtaposition (S16.1.3) --------------------------------
    // Emitted only before a START token, which is disjoint from every guarded
    // operator above. When a line opens with a dual-role token, NEITHER a guard
    // nor this boundary is emitted, so both readings are blocked and the parse
    // fails loudly — S16.2.3, "neither reading wins by default".
    if (valid_symbols[ELEM_STMT_BOUNDARY] && classify_start(lexer, true)) {
        lexer->result_symbol = ELEM_STMT_BOUNDARY;
        return true;
    }
    if (valid_symbols[STMT_BOUNDARY] && classify_start(lexer, false)) {
        lexer->result_symbol = STMT_BOUNDARY;
        return true;
    }

    return false;
}
