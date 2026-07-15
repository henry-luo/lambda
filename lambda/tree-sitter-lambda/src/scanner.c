#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdint.h>

enum TokenType {
    START,
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

bool tree_sitter_lambda_external_scanner_scan(
    void *payload, TSLexer *lexer, const bool *valid_symbols) {
    (void)payload;
    if (!valid_symbols[START]) {
        return false;
    }

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

    lexer->result_symbol = START;
    return true;
}
