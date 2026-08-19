#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// External scanner for tree-sitter-lambda.
//
// Besides the contextual `start` keyword, this scanner delimits the type
// language: a type pattern, a single primary type, and a string/symbol pattern
// island each arrive at the parser as ONE opaque token. The scanner only finds
// where such a token ENDS — it never validates the interior. The Lambda side
// (lambda/runtime/parse_type_pattern.cpp) parses the token's source text into
// `Type*` directly. See vibe/Lambda_Grammar_Reduce5.md.
//
// The split matters: this file is compiled standalone by the package's language
// bindings, so it must not reach into the Lambda runtime, and tree-sitter may
// run it speculatively during GLR ambiguity and error recovery, so it stays
// free of side effects.

enum TokenType {
    START,
    TYPE_PATTERN_TOKEN,
    PRIMARY_TYPE_PATTERN_TOKEN,
    PATTERN_ISLAND_TOKEN,
    // Content position needs its own token: there, a bare name may instead be a
    // FIELD name (`type T { a: int }`), and only the ':' after it tells the two
    // apart. Sharing TYPE_PATTERN_TOKEN would consume the name before the
    // parser ever sees the ':'.
    CONTENT_TYPE_TOKEN,
    // The declaration-only `T (^ E)?` type sub-form.
    RETURN_TYPE_TOKEN,
    // The complete view/edit model pattern (atom or `|` union).
    VIEW_PATTERN_TOKEN,
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

static bool is_horizontal_space(int32_t ch) {
    return ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v';
}

static bool is_space(int32_t ch) {
    return is_horizontal_space(ch) || ch == '\r' || ch == '\n';
}

static bool is_identifier_start(int32_t ch) {
    return ch == '$' || ch == '_' || ch == '\\' ||
        (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch >= 0x80;
}

static bool is_identifier_continue(int32_t ch) {
    return is_identifier_start(ch) || (ch >= '0' && ch <= '9');
}

static bool is_digit(int32_t ch) {
    return ch >= '0' && ch <= '9';
}

// ---------------------------------------------------------------------------
// shared lexing helpers
// ---------------------------------------------------------------------------

// Skip whitespace and comments as extras. `skip` marks them as not belonging to
// the token, so a token never starts with or trails whitespace.
static void skip_extras(TSLexer *lexer) {
    for (;;) {
        if (is_space(lexer->lookahead)) {
            lexer->advance(lexer, true);
        }
        else if (lexer->lookahead == '/') {
            // a comment is an extra; anything else beginning with '/' belongs to
            // the caller, so this must not consume it
            lexer->mark_end(lexer);
            lexer->advance(lexer, true);
            if (lexer->lookahead == '/') {
                while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
                    lexer->advance(lexer, true);
                }
            }
            else if (lexer->lookahead == '*') {
                lexer->advance(lexer, true);
                int32_t prev = 0;
                while (!lexer->eof(lexer) && !(prev == '*' && lexer->lookahead == '/')) {
                    prev = lexer->lookahead;
                    lexer->advance(lexer, true);
                }
                if (!lexer->eof(lexer)) { lexer->advance(lexer, true); }
            }
            else {
                return;  // a lone '/', already past it — caller sees the rest
            }
        }
        else { return; }
    }
}

// Consume a quoted string or symbol literal, escapes included. Assumes the
// opening quote is the current lookahead.
static void consume_quoted(TSLexer *lexer, int32_t quote) {
    lexer->advance(lexer, false);
    while (!lexer->eof(lexer) && lexer->lookahead != quote) {
        if (lexer->lookahead == '\\') { lexer->advance(lexer, false); }
        if (lexer->eof(lexer)) { return; }
        lexer->advance(lexer, false);
    }
    if (!lexer->eof(lexer)) { lexer->advance(lexer, false); }  // closing quote
}

// Read an identifier/keyword into buf (truncated, always NUL-terminated) and
// report its true length. Advances past the whole word.
static unsigned consume_word(TSLexer *lexer, char *buf, unsigned cap) {
    unsigned n = 0;
    while (is_identifier_continue(lexer->lookahead)) {
        if (n + 1 < cap) { buf[n] = (char)lexer->lookahead; }
        n++;
        lexer->advance(lexer, false);
    }
    buf[n + 1 < cap ? n : cap - 1] = '\0';
    return n;
}

// Consume a `\( ... )` / `\symbol( ... )` island body, balanced, strings aware.
// Assumes the opening '\\' is the current lookahead. Returns false if what
// follows is not actually an island tag.
static bool consume_island(TSLexer *lexer) {
    lexer->advance(lexer, false);  // backslash
    if (lexer->lookahead != '(') {
        // tagged form: \symbol( — the tag is one token, no space before '('
        char tag[16];
        unsigned n = consume_word(lexer, tag, sizeof(tag));
        if (n == 0 || lexer->lookahead != '(') { return false; }
    }
    lexer->advance(lexer, false);  // '('
    int depth = 1;
    while (!lexer->eof(lexer) && depth > 0) {
        int32_t c = lexer->lookahead;
        if (c == '"' || c == '\'') { consume_quoted(lexer, c); continue; }
        if (c == '(') { depth++; }
        else if (c == ')') { depth--; }
        lexer->advance(lexer, false);
    }
    return depth == 0;
}

// A word that continues a type pattern rather than ending it. `to` joins two
// range bounds; `fn` opens a function type whose parameter list follows.
static bool is_pattern_continuation_word(const char *w, unsigned n) {
    return (n == 2 && strcmp(w, "to") == 0) || (n == 2 && strcmp(w, "fn") == 0);
}

// After a newline at depth 0 the pattern ends unless what follows can only be a
// continuation: the binary type operators and `to` cannot start a statement, so
// seeing one means the annotation wraps onto the next line. A word needs its
// full spelling checked, which only the word reader can do — hence the pending
// flag rather than a single-character guess.
static bool operator_continues_after_newline(int32_t c) {
    return c == '|' || c == '&' || c == '!';
}

// ---------------------------------------------------------------------------
// type pattern: the whole annotation-position type sub-language, one token
// ---------------------------------------------------------------------------

static bool scan_type_pattern(TSLexer *lexer, bool primary_only, bool *out_bare_word) {
    skip_extras(lexer);
    if (lexer->eof(lexer)) { return false; }

    int depth = 0;
    bool any = false;            // any content accepted yet
    bool expect_primary = true;  // at a position where a primary type may start
    bool newline_pending = false; // a depth-0 newline whose continuation is undecided
    int d0_atoms = 0;            // name-like atoms (words, quoted literals) at depth 0
    bool d0_other = false;       // anything else seen at depth 0
    if (out_bare_word) { *out_bare_word = false; }

    for (;;) {
        int32_t c = lexer->lookahead;
        if (lexer->eof(lexer)) { break; }

        // --- whitespace / newline ------------------------------------------
        if (is_space(c)) {
            bool saw_newline = false;
            while (is_space(lexer->lookahead)) {
                if (lexer->lookahead == '\n') { saw_newline = true; }
                lexer->advance(lexer, false);
            }
            // comments inside a pattern are extras too
            while (lexer->lookahead == '/') {
                lexer->mark_end(lexer);  // pin: a lone '/' must not be consumed
                lexer->advance(lexer, false);
                if (lexer->lookahead == '/') {
                    while (!lexer->eof(lexer) && lexer->lookahead != '\n') { lexer->advance(lexer, false); }
                    saw_newline = true;
                }
                else if (lexer->lookahead == '*') {
                    lexer->advance(lexer, false);
                    int32_t prev = 0;
                    while (!lexer->eof(lexer) && !(prev == '*' && lexer->lookahead == '/')) {
                        prev = lexer->lookahead;  lexer->advance(lexer, false);
                    }
                    if (!lexer->eof(lexer)) { lexer->advance(lexer, false); }
                }
                else { break; }
                while (is_space(lexer->lookahead)) {
                    if (lexer->lookahead == '\n') { saw_newline = true; }
                    lexer->advance(lexer, false);
                }
            }
            if (primary_only) { break; }
            // A newline ends the annotation unless the pattern is unfinished
            // (trailing operator) or the next line opens with something that
            // cannot begin a statement.
            if (saw_newline && depth == 0 && !expect_primary) {
                int32_t next = lexer->lookahead;
                if (operator_continues_after_newline(next)) { continue; }
                // only `to` continues among words; the word reader confirms it
                if (is_identifier_start(next)) { newline_pending = true; continue; }
                break;
            }
            continue;
        }

        // --- comment directly against the previous token ---------------------
        if (c == '/') {
            lexer->mark_end(lexer);
            lexer->advance(lexer, false);
            if (lexer->lookahead == '/') {
                while (!lexer->eof(lexer) && lexer->lookahead != '\n') { lexer->advance(lexer, false); }
                continue;
            }
            if (lexer->lookahead == '*') {
                lexer->advance(lexer, false);
                int32_t prev = 0;
                while (!lexer->eof(lexer) && !(prev == '*' && lexer->lookahead == '/')) {
                    prev = lexer->lookahead;  lexer->advance(lexer, false);
                }
                if (!lexer->eof(lexer)) { lexer->advance(lexer, false); }
                continue;
            }
            break;  // a bare '/' is not type syntax
        }

        // --- literals --------------------------------------------------------
        if (c == '"' || c == '\'') {
            consume_quoted(lexer, c);
            lexer->mark_end(lexer);
            any = true;  expect_primary = false;
            // a quoted literal can be a FIELD NAME ('type': string), so it
            // counts as a name-like atom for the content decline rule
            if (depth == 0) { d0_atoms++; }
            if (primary_only && depth == 0) { break; }
            continue;
        }

        // --- string/symbol pattern island -----------------------------------
        if (c == '\\') {
            if (!consume_island(lexer)) { break; }
            lexer->mark_end(lexer);
            any = true;  expect_primary = false;
            if (primary_only && depth == 0) { break; }
            continue;
        }

        // --- identifiers, base-type keywords, `to`, and the `that` stop ------
        if (is_identifier_start(c)) {
            char word[8];
            unsigned n = consume_word(lexer, word, sizeof(word));
            // `that` closes the pattern and opens the constraint predicate: it
            // is never part of a pattern (CT1v2), so leave it for the parser.
            if (depth == 0 && n == 4 && strcmp(word, "that") == 0) { break; }
            // A word on the next line only continues the pattern when it is
            // `to`; anything else starts a new statement, and the token ended
            // back at the line break.
            if (newline_pending) {
                newline_pending = false;
                if (!(n == 2 && strcmp(word, "to") == 0)) { break; }
            }
            lexer->mark_end(lexer);
            any = true;
            if (depth == 0) { d0_atoms++; }
            expect_primary = is_pattern_continuation_word(word, n);
            if (primary_only && depth == 0 && !expect_primary) { break; }
            continue;
        }

        // --- numeric literals -------------------------------------------------
        if (is_digit(c) || (c == '-' && expect_primary)) {
            lexer->advance(lexer, false);
            while (is_digit(lexer->lookahead) || lexer->lookahead == '.' ||
                   lexer->lookahead == 'e' || lexer->lookahead == 'E' ||
                   is_identifier_continue(lexer->lookahead)) {
                lexer->advance(lexer, false);
            }
            lexer->mark_end(lexer);
            any = true;  expect_primary = false;
            if (primary_only && depth == 0) { break; }
            continue;
        }

        // --- brackets ---------------------------------------------------------
        if (c == '(' || c == '[' || c == '{' || c == '<') {
            // At depth 0 an opening brace only belongs to the pattern where a
            // primary may start (map/tuple/array/element type). Otherwise it is
            // the enclosing construct's body — a match-arm block, say — and the
            // pattern ends here. `[` is the exception: after a primary it is an
            // occurrence count (int[3]).
            if (depth == 0 && !expect_primary && c != '[') { break; }
            if (depth == 0) { d0_other = true; }
            depth++;
            lexer->advance(lexer, false);
            lexer->mark_end(lexer);
            any = true;  expect_primary = (c != '[');
            continue;
        }
        if (c == ')' || c == ']' || c == '}' || c == '>') {
            if (depth == 0) { break; }  // belongs to the enclosing construct
            depth--;
            lexer->advance(lexer, false);
            lexer->mark_end(lexer);
            any = true;  expect_primary = false;
            if (primary_only && depth == 0) { break; }
            continue;
        }

        // --- operators and separators ----------------------------------------
        if (depth > 0) {
            // inside brackets everything is pattern content: field colons,
            // commas, literal attr defaults (CT8v2), occurrence counts
            lexer->advance(lexer, false);
            lexer->mark_end(lexer);
            any = true;
            if (c == ',' || c == ':' || c == '|' || c == '&' || c == '!' || c == '=') {
                expect_primary = true;
            }
            continue;
        }
        if (primary_only) { break; }
        if (c == '|' || c == '&' || c == '!') {
            lexer->advance(lexer, false);
            lexer->mark_end(lexer);
            any = true;  d0_other = true;  expect_primary = true;
            continue;
        }
        if (c == '^' && !expect_primary) {
            // The only `^` a pattern can contain is a fn type's raised-channel
            // marker (`fn() T^`, `fn() T^E`): value annotations lost `^`
            // entirely (CT3v2), so this cannot be the old union sugar.
            lexer->advance(lexer, false);
            lexer->mark_end(lexer);
            any = true;  expect_primary = true;
            continue;
        }
        if (c == '?' || c == '+' || c == '*') {
            // occurrence suffix on the primary just completed
            if (expect_primary && c != '!') { break; }
            lexer->advance(lexer, false);
            lexer->mark_end(lexer);
            any = true;  expect_primary = false;
            continue;
        }
        // ',' '=' ';' ':' and anything else at depth 0 ends the pattern
        break;
    }

    // one name-like atom and nothing else: could be a field/view name instead
    if (out_bare_word) { *out_bare_word = (any && !d0_other && d0_atoms == 1); }
    return any;
}

// ---------------------------------------------------------------------------
// declaration return contracts and view patterns
// ---------------------------------------------------------------------------

static bool scan_return_type_atom(TSLexer *lexer, char *out_word,
        unsigned out_word_cap, unsigned *out_length) {
    if (!is_identifier_start(lexer->lookahead)) { return false; }
    char discarded_word[16];
    char *word = out_word ? out_word : discarded_word;
    unsigned word_cap = out_word ? out_word_cap : sizeof(discarded_word);
    unsigned length = consume_word(lexer, word, word_cap);
    if (out_length) { *out_length = length; }
    while (is_space(lexer->lookahead)) { lexer->advance(lexer, false); }
    if (lexer->lookahead == '?' || lexer->lookahead == '+' || lexer->lookahead == '*') {
        lexer->advance(lexer, false);
        return true;
    }
    if (lexer->lookahead != '[') { return true; }
    lexer->advance(lexer, false);
    while (!lexer->eof(lexer) && lexer->lookahead != ']') {
        lexer->advance(lexer, false);
    }
    if (lexer->eof(lexer)) { return false; }
    lexer->advance(lexer, false);
    return true;
}

static bool is_return_type_statement_keyword(const char *word, unsigned length) {
    return (length == 2 && strcmp(word, "if") == 0) ||
        (length == 2 && strcmp(word, "fn") == 0) ||
        (length == 2 && strcmp(word, "pn") == 0) ||
        (length == 2 && strcmp(word, "on") == 0) ||
        (length == 3 && strcmp(word, "for") == 0) ||
        (length == 3 && strcmp(word, "let") == 0) ||
        (length == 3 && strcmp(word, "var") == 0) ||
        (length == 3 && strcmp(word, "pub") == 0) ||
        (length == 4 && strcmp(word, "else") == 0) ||
        (length == 4 && strcmp(word, "view") == 0) ||
        (length == 4 && strcmp(word, "edit") == 0) ||
        (length == 4 && strcmp(word, "type") == 0) ||
        (length == 4 && strcmp(word, "case") == 0) ||
        (length == 5 && strcmp(word, "while") == 0) ||
        (length == 5 && strcmp(word, "match") == 0) ||
        (length == 5 && strcmp(word, "raise") == 0) ||
        (length == 5 && strcmp(word, "state") == 0) ||
        (length == 5 && strcmp(word, "apply") == 0) ||
        (length == 6 && strcmp(word, "return") == 0) ||
        (length == 7 && strcmp(word, "default") == 0) ||
        (length == 8 && strcmp(word, "continue") == 0) ||
        (length == 8 && strcmp(word, "function") == 0);
}

static bool scan_return_type_token(TSLexer *lexer) {
    skip_extras(lexer);
    char first_word[16];
    unsigned first_length = 0;
    if (!scan_return_type_atom(lexer, first_word, sizeof(first_word),
            &first_length)) { return false; }
    // A return slot may start with an alias, but a control-flow keyword such
    // as `else {` is never an alias. Refusing it prevents this opaque token
    // from swallowing ordinary statement bodies during GLR recovery.
    if (is_return_type_statement_keyword(first_word, first_length)) { return false; }
    lexer->mark_end(lexer);

    bool saw_raised_channel = false;
    for (;;) {
        while (is_space(lexer->lookahead)) { lexer->advance(lexer, false); }
        if (lexer->lookahead == '^' && !saw_raised_channel) {
            lexer->advance(lexer, false);
            lexer->mark_end(lexer);
            saw_raised_channel = true;
            while (is_space(lexer->lookahead)) { lexer->advance(lexer, false); }
            if (!scan_return_type_atom(lexer, NULL, 0, NULL)) { break; }
            lexer->mark_end(lexer);
            continue;
        }
        if (lexer->lookahead != '|' && lexer->lookahead != '&' && lexer->lookahead != '!') {
            break;
        }
        lexer->advance(lexer, false);
        while (is_space(lexer->lookahead)) { lexer->advance(lexer, false); }
        // A partial union here is an expression operator (notably `|>`), not
        // a return contract. Do not turn its left identifier into a token.
        if (!scan_return_type_atom(lexer, NULL, 0, NULL)) { return false; }
        lexer->mark_end(lexer);
    }
    // `return_type` is declaration-only. During recovery the GLR parser can
    // offer it beside an ordinary expression (`raise error(...)`), so require
    // the declaration continuation before committing to this opaque token.
    while (is_space(lexer->lookahead)) { lexer->advance(lexer, false); }
    if (lexer->lookahead == '{') { return true; }
    if (lexer->lookahead == '=') {
        lexer->advance(lexer, false);
        return lexer->lookahead == '>';
    }
    if (!is_identifier_start(lexer->lookahead)) { return false; }
    char word[8];
    unsigned length = consume_word(lexer, word, sizeof(word));
    return length == 5 && strcmp(word, "state") == 0;
}

static bool scan_view_atom(TSLexer *lexer) {
    if (lexer->lookahead == '<') {
        int depth = 0;
        do {
            int32_t ch = lexer->lookahead;
            if (ch == '"' || ch == '\'') { consume_quoted(lexer, ch); continue; }
            if (ch == '<') { depth++; }
            else if (ch == '>') { depth--; }
            lexer->advance(lexer, false);
        } while (!lexer->eof(lexer) && depth > 0);
        return depth == 0;
    }
    if (!is_identifier_start(lexer->lookahead)) { return false; }
    char word[16];
    consume_word(lexer, word, sizeof(word));
    return true;
}

static bool scan_view_pattern_token(TSLexer *lexer) {
    skip_extras(lexer);
    bool first_is_word = is_identifier_start(lexer->lookahead);
    if (!scan_view_atom(lexer)) { return false; }
    lexer->mark_end(lexer);

    // `view name: Pattern` must leave the leading name to the ordinary
    // identifier rule; only the colon distinguishes it from a bare pattern.
    if (first_is_word) {
        while (is_space(lexer->lookahead)) { lexer->advance(lexer, false); }
        if (lexer->lookahead == ':') { return false; }
    }

    while (lexer->lookahead == '|') {
        lexer->advance(lexer, false);
        while (is_space(lexer->lookahead)) { lexer->advance(lexer, false); }
        if (!scan_view_atom(lexer)) { return true; }
        lexer->mark_end(lexer);
        while (is_space(lexer->lookahead)) { lexer->advance(lexer, false); }
    }
    return true;
}

// ---------------------------------------------------------------------------
// contextual `start` keyword (unchanged)
// ---------------------------------------------------------------------------

static bool scan_start(TSLexer *lexer) {
    while (is_space(lexer->lookahead)) {
        lexer->advance(lexer, true);
    }

    const char keyword[] = "start";
    for (unsigned i = 0; keyword[i] != '\0'; i++) {
        if (lexer->lookahead != keyword[i]) {
            return false;
        }
        lexer->advance(lexer, false);
    }

    // Reserving a normal literal made `start` unusable as an identifier. Keep
    // the token contextual by requiring a same-line, named call operand.
    if (!is_horizontal_space(lexer->lookahead)) {
        return false;
    }
    lexer->mark_end(lexer);
    do {
        lexer->advance(lexer, false);
    } while (is_horizontal_space(lexer->lookahead));
    if (!is_identifier_start(lexer->lookahead)) {
        return false;
    }
    do {
        lexer->advance(lexer, false);
    } while (is_identifier_continue(lexer->lookahead));
    while (lexer->lookahead == '.') {
        lexer->advance(lexer, false);
        if (!is_identifier_start(lexer->lookahead)) {
            return false;
        }
        do {
            lexer->advance(lexer, false);
        } while (is_identifier_continue(lexer->lookahead));
    }
    while (is_horizontal_space(lexer->lookahead)) {
        lexer->advance(lexer, false);
    }
    if (lexer->lookahead != '(') {
        return false;
    }

    return true;
}

bool tree_sitter_lambda_external_scanner_scan(
    void *payload, TSLexer *lexer, const bool *valid_symbols) {
    (void)payload;

    if (valid_symbols[START] && scan_start(lexer)) {
        lexer->result_symbol = START;
        return true;
    }

    // Islands are first-class values. Probe only their unambiguous backslash
    // prefix: skip_extras would inspect a rooted path's '/', which must remain
    // available to the complete-path scanner below.
    if (valid_symbols[PATTERN_ISLAND_TOKEN] && lexer->lookahead == '\\' &&
            consume_island(lexer)) {
        lexer->mark_end(lexer);
        lexer->result_symbol = PATTERN_ISLAND_TOKEN;
        return true;
    }

    // Content position: a bare name may be a FIELD name instead, and only the
    // ':' after it tells the two apart — scan fully, decline on `word :`.
    if (valid_symbols[CONTENT_TYPE_TOKEN]) {
        bool bare_word = false;
        if (scan_type_pattern(lexer, false, &bare_word)) {
            bool name_prefix = false;
            if (bare_word) {
                // peek with skip=false: a skip-advance MOVES the token start
                // (vendored lexer.c:235), and a start past mark_end clamps the
                // token to zero width; non-skip advances past mark_end are
                // plain lookahead
                while (is_space(lexer->lookahead)) { lexer->advance(lexer, false); }
                name_prefix = (lexer->lookahead == ':');
            }
            if (!name_prefix) {
                lexer->result_symbol = CONTENT_TYPE_TOKEN;
                return true;
            }
        }
        return false;  // a field name: let the parser lex it as an identifier
    }

    if (valid_symbols[RETURN_TYPE_TOKEN] && scan_return_type_token(lexer)) {
        lexer->result_symbol = RETURN_TYPE_TOKEN;
        return true;
    }

    if (valid_symbols[VIEW_PATTERN_TOKEN] && scan_view_pattern_token(lexer)) {
        lexer->result_symbol = VIEW_PATTERN_TOKEN;
        return true;
    }

    if (valid_symbols[TYPE_PATTERN_TOKEN] && scan_type_pattern(lexer, false, NULL)) {
        lexer->result_symbol = TYPE_PATTERN_TOKEN;
        return true;
    }

    if (valid_symbols[PRIMARY_TYPE_PATTERN_TOKEN] && scan_type_pattern(lexer, true, NULL)) {
        lexer->result_symbol = PRIMARY_TYPE_PATTERN_TOKEN;
        return true;
    }

    return false;
}
