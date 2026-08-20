#include "lambda_rd_parser.h"

#include <limits.h>
#include <string.h>

// The lexer is intentionally independent from Tree-sitter. The direct parser
// needs newline tokens for statement boundaries, whereas Tree-sitter consumes
// whitespace as extras before its grammar decides whether a line may end.

static bool lexer_has(const LambdaLexer* lexer, size_t count) {
    return lexer->offset <= lexer->length && count <= lexer->length - lexer->offset;
}

static char lexer_peek(const LambdaLexer* lexer, size_t ahead) {
    return lexer_has(lexer, ahead + 1) ? lexer->source[lexer->offset + ahead] : '\0';
}

static void lexer_advance_byte(LambdaLexer* lexer) {
    if (lexer->offset >= lexer->length) return;
    lexer->offset++;
    lexer->column++;
}

static void lexer_advance_newline(LambdaLexer* lexer) {
    if (lexer_peek(lexer, 0) == '\r') {
        lexer->offset++;
        if (lexer_peek(lexer, 0) == '\n') lexer->offset++;
    } else if (lexer_peek(lexer, 0) == '\n') {
        lexer->offset++;
    } else {
        return;
    }
    lexer->line++;
    lexer->column = 0;
}

static bool lexer_is_horizontal_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v';
}

static bool lexer_is_newline(const LambdaLexer* lexer) {
    char ch = lexer_peek(lexer, 0);
    return ch == '\r' || ch == '\n';
}

static bool lexer_is_ascii_alpha(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

static bool lexer_is_digit(char ch) {
    return ch >= '0' && ch <= '9';
}

static bool lexer_is_hex_digit(char ch) {
    return lexer_is_digit(ch) || (ch >= 'a' && ch <= 'f') ||
        (ch >= 'A' && ch <= 'F');
}

static bool lexer_is_ident_start_byte(char ch) {
    return lexer_is_ascii_alpha(ch) || ch == '_' || ch == '$' ||
        (unsigned char)ch >= 0x80;
}

static bool lexer_is_ident_continue_byte(char ch) {
    return lexer_is_ident_start_byte(ch) || lexer_is_digit(ch);
}

static size_t lexer_unicode_escape_length(const LambdaLexer* lexer) {
    if (lexer_peek(lexer, 0) != '\\' || lexer_peek(lexer, 1) != 'u') return 0;
    if (lexer_peek(lexer, 2) == '{') {
        size_t i = 3;
        if (!lexer_is_hex_digit(lexer_peek(lexer, i))) return 0;
        while (lexer_is_hex_digit(lexer_peek(lexer, i))) i++;
        return lexer_peek(lexer, i) == '}' ? i + 1 : 0;
    }
    for (size_t i = 2; i < 6; i++) {
        if (!lexer_is_hex_digit(lexer_peek(lexer, i))) return 0;
    }
    return 6;
}

static bool lexer_is_ident_start(const LambdaLexer* lexer) {
    return lexer_is_ident_start_byte(lexer_peek(lexer, 0)) ||
        lexer_unicode_escape_length(lexer) != 0;
}

static bool lexer_is_ident_continue(const LambdaLexer* lexer) {
    return lexer_is_ident_continue_byte(lexer_peek(lexer, 0)) ||
        lexer_unicode_escape_length(lexer) != 0;
}

static bool lexer_dot_continues_member_target(const LambdaLexer* lexer, size_t start) {
    if (!start) return false;
    char previous = lexer->source[start - 1];
    return lexer_is_ident_continue_byte(previous) || previous == '\'' ||
        previous == ']' || previous == ')';
}

static void lexer_advance_ident_unit(LambdaLexer* lexer) {
    size_t escaped = lexer_unicode_escape_length(lexer);
    if (escaped) {
        for (size_t i = 0; i < escaped; i++) lexer_advance_byte(lexer);
        return;
    }
    lexer_advance_byte(lexer);
}

static LambdaToken lexer_make_token(LambdaTokenKind kind, size_t start,
        uint32_t line, uint32_t column, size_t end) {
    LambdaToken token;
    token.kind = kind;
    token.span.start_byte = start > UINT32_MAX ? UINT32_MAX : (uint32_t)start;
    token.span.end_byte = end > UINT32_MAX ? UINT32_MAX : (uint32_t)end;
    token.line = line;
    token.column = column;
    return token;
}

static LambdaToken lexer_error_token(LambdaLexer* lexer, size_t start,
        uint32_t line, uint32_t column) {
    if (lexer->offset == start && lexer->offset < lexer->length) {
        lexer_advance_byte(lexer);
    }
    return lexer_make_token(LAMBDA_TOK_ERROR, start, line, column, lexer->offset);
}

// Skip horizontal extras and comments. A line-comment intentionally stops just
// before its newline so the caller emits the statement-boundary token.
static bool lexer_skip_extras(LambdaLexer* lexer) {
    for (;;) {
        while (lexer_is_horizontal_space(lexer_peek(lexer, 0))) lexer_advance_byte(lexer);

        if (lexer_peek(lexer, 0) != '/' || !lexer_has(lexer, 2)) return true;
        if (lexer_peek(lexer, 1) == '/') {
            lexer_advance_byte(lexer);
            lexer_advance_byte(lexer);
            while (lexer->offset < lexer->length && !lexer_is_newline(lexer)) {
                lexer_advance_byte(lexer);
            }
            continue;
        }
        if (lexer_peek(lexer, 1) != '*') return true;

        lexer_advance_byte(lexer);
        lexer_advance_byte(lexer);
        bool closed = false;
        while (lexer->offset < lexer->length) {
            if (lexer_peek(lexer, 0) == '*' && lexer_peek(lexer, 1) == '/') {
                lexer_advance_byte(lexer);
                lexer_advance_byte(lexer);
                closed = true;
                break;
            }
            if (lexer_is_newline(lexer)) lexer_advance_newline(lexer);
            else lexer_advance_byte(lexer);
        }
        if (!closed) return false;
    }
}

static bool lexer_word_equals(const char* source, size_t start, size_t end,
        const char* word) {
    size_t length = strlen(word);
    return end - start == length && memcmp(source + start, word, length) == 0;
}

static bool lexer_word_in(const char* source, size_t start, size_t end,
        const char* const* words, size_t word_count) {
    for (size_t i = 0; i < word_count; i++) {
        if (lexer_word_equals(source, start, end, words[i])) return true;
    }
    return false;
}

static LambdaTokenKind lexer_keyword_kind(const char* source, size_t start, size_t end) {
    struct Keyword { const char* text; LambdaTokenKind kind; };
    static const struct Keyword keywords[] = {
        {"let", LAMBDA_TOK_LET}, {"pub", LAMBDA_TOK_PUB},
        {"var", LAMBDA_TOK_VAR}, {"type", LAMBDA_TOK_TYPE},
        {"fn", LAMBDA_TOK_FN}, {"pn", LAMBDA_TOK_PN},
        {"view", LAMBDA_TOK_VIEW}, {"edit", LAMBDA_TOK_EDIT},
        {"state", LAMBDA_TOK_STATE}, {"on", LAMBDA_TOK_ON},
        {"if", LAMBDA_TOK_IF}, {"else", LAMBDA_TOK_ELSE},
        {"match", LAMBDA_TOK_MATCH}, {"case", LAMBDA_TOK_CASE},
        {"default", LAMBDA_TOK_DEFAULT}, {"for", LAMBDA_TOK_FOR},
        {"while", LAMBDA_TOK_WHILE}, {"break", LAMBDA_TOK_BREAK},
        {"continue", LAMBDA_TOK_CONTINUE}, {"return", LAMBDA_TOK_RETURN},
        {"raise", LAMBDA_TOK_RAISE}, {"import", LAMBDA_TOK_IMPORT},
        {"apply", LAMBDA_TOK_APPLY}, {"not", LAMBDA_TOK_NOT},
        {"div", LAMBDA_TOK_DIV}, {"and", LAMBDA_TOK_AND},
        {"or", LAMBDA_TOK_OR}, {"to", LAMBDA_TOK_TO},
        {"is", LAMBDA_TOK_IS}, {"in", LAMBDA_TOK_IN},
        {"at", LAMBDA_TOK_AT}, {"that", LAMBDA_TOK_THAT},
        {"where", LAMBDA_TOK_WHERE}, {"order", LAMBDA_TOK_ORDER},
        {"by", LAMBDA_TOK_BY}, {"group", LAMBDA_TOK_GROUP},
        {"into", LAMBDA_TOK_INTO}, {"limit", LAMBDA_TOK_LIMIT},
        {"offset", LAMBDA_TOK_OFFSET}, {"asc", LAMBDA_TOK_ASC},
        {"desc", LAMBDA_TOK_DESC}, {"last", LAMBDA_TOK_LAST},
        {"as", LAMBDA_TOK_AS},
        {"eq", LAMBDA_TOK_EQ_WORD}, {"ne", LAMBDA_TOK_NE_WORD},
        {"lt", LAMBDA_TOK_LT_WORD}, {"le", LAMBDA_TOK_LE_WORD},
        {"ge", LAMBDA_TOK_GE_WORD}, {"gt", LAMBDA_TOK_GT_WORD},
    };
    static const char* const base_types[] = {
        "null", "any", "bool", "int64", "int", "float", "f64", "complex",
        "decimal", "integer", "number", "datetime", "date", "time", "binary",
        "range", "list", "array", "map", "element", "entity", "object",
        "function", "error", "string", "symbol", "i8", "i16", "i32", "i64",
        "u8", "u16", "u32", "u64", "f16", "f32",
    };
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (lexer_word_equals(source, start, end, keywords[i].text)) return keywords[i].kind;
    }
    if (lexer_word_in(source, start, end, base_types,
            sizeof(base_types) / sizeof(base_types[0]))) {
        return LAMBDA_TOK_BASE_TYPE;
    }
    if (lexer_word_equals(source, start, end, "true") ||
            lexer_word_equals(source, start, end, "false") ||
            lexer_word_equals(source, start, end, "inf") ||
            lexer_word_equals(source, start, end, "nan")) {
        return LAMBDA_TOK_NAMED_VALUE;
    }
    return LAMBDA_TOK_IDENTIFIER;
}

static bool lexer_scan_quoted(LambdaLexer* lexer, char quote, bool allow_newline) {
    lexer_advance_byte(lexer);
    bool any = false;
    while (lexer->offset < lexer->length) {
        char ch = lexer_peek(lexer, 0);
        if (ch == quote) {
            lexer_advance_byte(lexer);
            return any || quote == '"';
        }
        if (lexer_is_newline(lexer)) {
            if (!allow_newline) return false;
            lexer_advance_newline(lexer);
            any = true;
            continue;
        }
        if (ch != '\\') {
            lexer_advance_byte(lexer);
            any = true;
            continue;
        }
        lexer_advance_byte(lexer);
        char escaped = lexer_peek(lexer, 0);
        if (strchr("\"'\\/bfnrt", escaped)) {
            lexer_advance_byte(lexer);
            any = true;
            continue;
        }
        if (escaped != 'u') return false;
        lexer_advance_byte(lexer);
        if (lexer_peek(lexer, 0) == '{') {
            lexer_advance_byte(lexer);
            size_t count = 0;
            while (lexer_is_hex_digit(lexer_peek(lexer, 0))) {
                lexer_advance_byte(lexer);
                count++;
            }
            if (!count || lexer_peek(lexer, 0) != '}') return false;
            lexer_advance_byte(lexer);
        } else {
            for (int i = 0; i < 4; i++) {
                if (!lexer_is_hex_digit(lexer_peek(lexer, 0))) return false;
                lexer_advance_byte(lexer);
            }
        }
        any = true;
    }
    return false;
}

static bool lexer_scan_binary(LambdaLexer* lexer, bool datetime) {
    lexer_advance_byte(lexer);  // b or t
    lexer_advance_byte(lexer);  // quote
    size_t count = 0;
    while (lexer->offset < lexer->length && lexer_peek(lexer, 0) != '\'') {
        char ch = lexer_peek(lexer, 0);
        if (lexer_is_newline(lexer)) return false;
        if (datetime && !(lexer_is_digit(ch) || strchr(":-+.tTzZ ", ch))) return false;
        lexer_advance_byte(lexer);
        count++;
    }
    if (!count || lexer_peek(lexer, 0) != '\'') return false;
    lexer_advance_byte(lexer);
    return true;
}

static bool lexer_scan_island(LambdaLexer* lexer) {
    if (lexer_peek(lexer, 0) != '\\') return false;
    lexer_advance_byte(lexer);
    if (lexer_peek(lexer, 0) == '(') {
        lexer_advance_byte(lexer);
    } else {
        if (!lexer_is_ident_start(lexer)) return false;
        do { lexer_advance_ident_unit(lexer); } while (lexer_is_ident_continue(lexer));
        if (lexer_peek(lexer, 0) != '(') return false;
        lexer_advance_byte(lexer);
    }

    int depth = 1;
    while (lexer->offset < lexer->length && depth > 0) {
        char ch = lexer_peek(lexer, 0);
        if (ch == '"' || ch == '\'') {
            if (!lexer_scan_quoted(lexer, ch, ch == '"')) return false;
            continue;
        }
        if (ch == '(') depth++;
        if (ch == ')') depth--;
        if (lexer_is_newline(lexer)) lexer_advance_newline(lexer);
        else lexer_advance_byte(lexer);
    }
    return depth == 0;
}

static void lexer_scan_digits(LambdaLexer* lexer, bool hexadecimal) {
    while (hexadecimal ? lexer_is_hex_digit(lexer_peek(lexer, 0)) :
            lexer_is_digit(lexer_peek(lexer, 0))) {
        lexer_advance_byte(lexer);
    }
}

static bool lexer_scan_exponent(LambdaLexer* lexer) {
    if (lexer_peek(lexer, 0) != 'e' && lexer_peek(lexer, 0) != 'E') return false;
    size_t saved = lexer->offset;
    uint32_t saved_column = lexer->column;
    lexer_advance_byte(lexer);
    if (lexer_peek(lexer, 0) == '+' || lexer_peek(lexer, 0) == '-') lexer_advance_byte(lexer);
    if (!lexer_is_digit(lexer_peek(lexer, 0))) {
        lexer->offset = saved;
        lexer->column = saved_column;
        return false;
    }
    lexer_scan_digits(lexer, false);
    return true;
}

static LambdaTokenKind lexer_scan_number(LambdaLexer* lexer) {
    bool floating = false;
    bool hexadecimal = false;
    if (lexer_peek(lexer, 0) == '.') {
        floating = true;
        lexer_advance_byte(lexer);
        lexer_scan_digits(lexer, false);
        lexer_scan_exponent(lexer);
    } else if (lexer_peek(lexer, 0) == '0' &&
            (lexer_peek(lexer, 1) == 'x' || lexer_peek(lexer, 1) == 'X')) {
        hexadecimal = true;
        lexer_advance_byte(lexer);
        lexer_advance_byte(lexer);
        lexer_scan_digits(lexer, true);
    } else {
        lexer_scan_digits(lexer, false);
        if (lexer_peek(lexer, 0) == '.' && lexer_is_digit(lexer_peek(lexer, 1))) {
            floating = true;
            lexer_advance_byte(lexer);
            lexer_scan_digits(lexer, false);
        }
        if (lexer_scan_exponent(lexer)) floating = true;
    }

    if (lexer_peek(lexer, 0) == 'j') {
        lexer_advance_byte(lexer);
        return LAMBDA_TOK_IMAGINARY;
    }
    if (lexer_peek(lexer, 0) == 'n' || lexer_peek(lexer, 0) == 'm') {
        lexer_advance_byte(lexer);
        return LAMBDA_TOK_DECIMAL;
    }
    if ((lexer_peek(lexer, 0) == 'i' || lexer_peek(lexer, 0) == 'u') &&
            lexer_is_digit(lexer_peek(lexer, 1))) {
        lexer_advance_byte(lexer);
        lexer_scan_digits(lexer, false);
        return LAMBDA_TOK_SIZED_INTEGER;
    }
    if (lexer_peek(lexer, 0) == 'f' && lexer_is_digit(lexer_peek(lexer, 1))) {
        lexer_advance_byte(lexer);
        lexer_scan_digits(lexer, false);
        return LAMBDA_TOK_SIZED_FLOAT;
    }
    (void)hexadecimal;
    return floating ? LAMBDA_TOK_FLOAT : LAMBDA_TOK_INTEGER;
}

void lambda_lexer_init(LambdaLexer* lexer, const char* source, size_t length) {
    if (!lexer) return;
    lexer->source = source ? source : "";
    lexer->length = source ? length : 0;
    lexer->offset = 0;
    lexer->line = 1;
    lexer->column = 0;
}

LambdaToken lambda_lexer_next(LambdaLexer* lexer) {
    if (!lexer || !lexer->source || lexer->length > UINT32_MAX) {
        LambdaLexer fallback = {"", 0, 0, 1, 0};
        return lexer_error_token(lexer ? lexer : &fallback, 0, 1, 0);
    }

    size_t start = lexer->offset;
    uint32_t line = lexer->line;
    uint32_t column = lexer->column;
    if (!lexer_skip_extras(lexer)) return lexer_error_token(lexer, start, line, column);
    start = lexer->offset;
    line = lexer->line;
    column = lexer->column;

    if (lexer->offset >= lexer->length) {
        return lexer_make_token(LAMBDA_TOK_EOF, start, line, column, start);
    }
    if (lexer_is_newline(lexer)) {
        lexer_advance_newline(lexer);
        return lexer_make_token(LAMBDA_TOK_NEWLINE, start, line, column, lexer->offset);
    }

    char ch = lexer_peek(lexer, 0);
    if (ch == '"' || ch == '\'') {
        bool valid = lexer_scan_quoted(lexer, ch, ch == '"');
        return valid ? lexer_make_token(ch == '"' ? LAMBDA_TOK_STRING : LAMBDA_TOK_SYMBOL,
            start, line, column, lexer->offset) : lexer_error_token(lexer, start, line, column);
    }
    if ((ch == 'b' || ch == 't') && lexer_peek(lexer, 1) == '\'') {
        bool valid = lexer_scan_binary(lexer, ch == 't');
        return valid ? lexer_make_token(ch == 'b' ? LAMBDA_TOK_BINARY : LAMBDA_TOK_DATETIME,
            start, line, column, lexer->offset) : lexer_error_token(lexer, start, line, column);
    }
    // A Unicode escape is an identifier unit; only the remaining backslash
    // forms begin a pattern island.
    if (ch == '\\' && lexer_unicode_escape_length(lexer) == 0) {
        if (lexer_scan_island(lexer)) {
            return lexer_make_token(LAMBDA_TOK_PATTERN_ISLAND, start, line, column, lexer->offset);
        }
        return lexer_error_token(lexer, start, line, column);
    }
    // `.1` is a float only at an expression boundary; after an adjacent
    // member target it is the path segment in `record.1` / `.a.1`.
    if (lexer_is_digit(ch) || (ch == '.' && lexer_is_digit(lexer_peek(lexer, 1)) &&
            !lexer_dot_continues_member_target(lexer, start))) {
        LambdaTokenKind kind = lexer_scan_number(lexer);
        return lexer_make_token(kind, start, line, column, lexer->offset);
    }

    if (lexer_is_ident_start(lexer)) {
        do { lexer_advance_ident_unit(lexer); } while (lexer_is_ident_continue(lexer));
        if (lexer_word_equals(lexer->source, start, lexer->offset, "decimal") &&
                lexer->offset + 4 <= lexer->length && lexer->source[lexer->offset] == '.' &&
                (memcmp(lexer->source + lexer->offset + 1, "inf", 3) == 0 ||
                 memcmp(lexer->source + lexer->offset + 1, "nan", 3) == 0) &&
                (lexer->offset + 4 == lexer->length ||
                 !lexer_is_ident_continue_byte(lexer->source[lexer->offset + 4]))) {
            for (int i = 0; i < 4; i++) lexer_advance_byte(lexer);
            return lexer_make_token(LAMBDA_TOK_NAMED_VALUE, start, line, column, lexer->offset);
        }
        return lexer_make_token(lexer_keyword_kind(lexer->source, start, lexer->offset),
            start, line, column, lexer->offset);
    }

#define PUNCT1(character, token_kind) \
    case character: lexer_advance_byte(lexer); return lexer_make_token(token_kind, start, line, column, lexer->offset)
    switch (ch) {
    PUNCT1('(', LAMBDA_TOK_LPAREN);
    PUNCT1(')', LAMBDA_TOK_RPAREN);
    PUNCT1('[', LAMBDA_TOK_LBRACKET);
    PUNCT1(']', LAMBDA_TOK_RBRACKET);
    PUNCT1('{', LAMBDA_TOK_LBRACE);
    PUNCT1('}', LAMBDA_TOK_RBRACE);
    PUNCT1(',', LAMBDA_TOK_COMMA);
    PUNCT1(':', LAMBDA_TOK_COLON);
    PUNCT1(';', LAMBDA_TOK_SEMICOLON);
    PUNCT1('^', LAMBDA_TOK_CARET);
    PUNCT1('%', LAMBDA_TOK_PERCENT);
    default: break;
    }
#undef PUNCT1

    if (ch == '.') {
        if (lexer_peek(lexer, 1) == '?') {
            lexer_advance_byte(lexer); lexer_advance_byte(lexer);
            return lexer_make_token(LAMBDA_TOK_DOT_QUESTION, start, line, column, lexer->offset);
        }
        if (lexer_peek(lexer, 1) == '.' && lexer_peek(lexer, 2) == '.') {
            lexer_advance_byte(lexer); lexer_advance_byte(lexer); lexer_advance_byte(lexer);
            return lexer_make_token(LAMBDA_TOK_ELLIPSIS, start, line, column, lexer->offset);
        }
        lexer_advance_byte(lexer);
        return lexer_make_token(LAMBDA_TOK_DOT, start, line, column, lexer->offset);
    }
    if (ch == '~') {
        lexer_advance_byte(lexer);
        if (lexer_peek(lexer, 0) == '~') {
            lexer_advance_byte(lexer);
            return lexer_make_token(LAMBDA_TOK_PARENT, start, line, column, lexer->offset);
        }
        if (lexer_peek(lexer, 0) == '#') {
            lexer_advance_byte(lexer);
            return lexer_make_token(LAMBDA_TOK_TILDE_INDEX, start, line, column, lexer->offset);
        }
        return lexer_make_token(LAMBDA_TOK_TILDE, start, line, column, lexer->offset);
    }
    if (ch == '+') {
        lexer_advance_byte(lexer);
        if (lexer_peek(lexer, 0) == '+') {
            lexer_advance_byte(lexer);
            return lexer_make_token(LAMBDA_TOK_PLUS_PLUS, start, line, column, lexer->offset);
        }
        return lexer_make_token(LAMBDA_TOK_PLUS, start, line, column, lexer->offset);
    }
    if (ch == '*') {
        lexer_advance_byte(lexer);
        if (lexer_peek(lexer, 0) == '*') {
            lexer_advance_byte(lexer);
            return lexer_make_token(LAMBDA_TOK_STAR_STAR, start, line, column, lexer->offset);
        }
        return lexer_make_token(LAMBDA_TOK_STAR, start, line, column, lexer->offset);
    }
    if (ch == '|') {
        lexer_advance_byte(lexer);
        if (lexer_peek(lexer, 0) == '>') {
            lexer_advance_byte(lexer);
            return lexer_make_token(LAMBDA_TOK_PIPE_FORWARD, start, line, column, lexer->offset);
        }
        return lexer_make_token(LAMBDA_TOK_PIPE, start, line, column, lexer->offset);
    }
    if (ch == '=') {
        lexer_advance_byte(lexer);
        if (lexer_peek(lexer, 0) == '=') {
            lexer_advance_byte(lexer);
            return lexer_make_token(LAMBDA_TOK_EQ_EQ, start, line, column, lexer->offset);
        }
        if (lexer_peek(lexer, 0) == '>') {
            lexer_advance_byte(lexer);
            return lexer_make_token(LAMBDA_TOK_ARROW, start, line, column, lexer->offset);
        }
        return lexer_make_token(LAMBDA_TOK_EQ, start, line, column, lexer->offset);
    }
    if (ch == '!') {
        lexer_advance_byte(lexer);
        if (lexer_peek(lexer, 0) == '=') {
            lexer_advance_byte(lexer);
            return lexer_make_token(LAMBDA_TOK_BANG_EQ, start, line, column, lexer->offset);
        }
        return lexer_make_token(LAMBDA_TOK_BANG, start, line, column, lexer->offset);
    }
    if (ch == '<') {
        lexer_advance_byte(lexer);
        if (lexer_peek(lexer, 0) == '=') {
            lexer_advance_byte(lexer);
            return lexer_make_token(LAMBDA_TOK_LT_EQ, start, line, column, lexer->offset);
        }
        return lexer_make_token(LAMBDA_TOK_LT, start, line, column, lexer->offset);
    }
    if (ch == '>') {
        lexer_advance_byte(lexer);
        if (lexer_peek(lexer, 0) == '=') {
            lexer_advance_byte(lexer);
            return lexer_make_token(LAMBDA_TOK_GT_EQ, start, line, column, lexer->offset);
        }
        return lexer_make_token(LAMBDA_TOK_GT, start, line, column, lexer->offset);
    }
    if (ch == '&') {
        lexer_advance_byte(lexer);
        return lexer_make_token(LAMBDA_TOK_AMPERSAND, start, line, column, lexer->offset);
    }
    if (ch == '?') {
        lexer_advance_byte(lexer);
        return lexer_make_token(LAMBDA_TOK_QUESTION, start, line, column, lexer->offset);
    }
    if (ch == '/') {
        lexer_advance_byte(lexer);
        return lexer_make_token(LAMBDA_TOK_SLASH, start, line, column, lexer->offset);
    }
    if (ch == '-') {
        lexer_advance_byte(lexer);
        return lexer_make_token(LAMBDA_TOK_MINUS, start, line, column, lexer->offset);
    }
    return lexer_error_token(lexer, start, line, column);
}

const char* lambda_token_kind_name(LambdaTokenKind kind) {
    switch (kind) {
    case LAMBDA_TOK_EOF: return "eof";
    case LAMBDA_TOK_ERROR: return "error";
    case LAMBDA_TOK_NEWLINE: return "newline";
    case LAMBDA_TOK_IDENTIFIER: return "identifier";
    case LAMBDA_TOK_BASE_TYPE: return "base_type";
    case LAMBDA_TOK_INTEGER: return "integer";
    case LAMBDA_TOK_FLOAT: return "float";
    case LAMBDA_TOK_DECIMAL: return "decimal";
    case LAMBDA_TOK_SIZED_INTEGER: return "sized_integer";
    case LAMBDA_TOK_SIZED_FLOAT: return "sized_float";
    case LAMBDA_TOK_IMAGINARY: return "imaginary";
    case LAMBDA_TOK_STRING: return "string";
    case LAMBDA_TOK_SYMBOL: return "symbol";
    case LAMBDA_TOK_BINARY: return "binary";
    case LAMBDA_TOK_DATETIME: return "datetime";
    case LAMBDA_TOK_NAMED_VALUE: return "named_value";
    case LAMBDA_TOK_PATTERN_ISLAND: return "pattern_island";
    case LAMBDA_TOK_LPAREN: return "(";
    case LAMBDA_TOK_RPAREN: return ")";
    case LAMBDA_TOK_LBRACKET: return "[";
    case LAMBDA_TOK_RBRACKET: return "]";
    case LAMBDA_TOK_LBRACE: return "{";
    case LAMBDA_TOK_RBRACE: return "}";
    case LAMBDA_TOK_COMMA: return ",";
    case LAMBDA_TOK_COLON: return ":";
    case LAMBDA_TOK_SEMICOLON: return ";";
    case LAMBDA_TOK_DOT: return ".";
    case LAMBDA_TOK_DOT_QUESTION: return ".?";
    case LAMBDA_TOK_AS: return "as";
    case LAMBDA_TOK_EQ_WORD: return "eq";
    case LAMBDA_TOK_NE_WORD: return "ne";
    case LAMBDA_TOK_LT_WORD: return "lt";
    case LAMBDA_TOK_LE_WORD: return "le";
    case LAMBDA_TOK_GE_WORD: return "ge";
    case LAMBDA_TOK_GT_WORD: return "gt";
    case LAMBDA_TOK_QUESTION: return "?";
    case LAMBDA_TOK_CARET: return "^";
    case LAMBDA_TOK_TILDE: return "~";
    case LAMBDA_TOK_TILDE_INDEX: return "~#";
    case LAMBDA_TOK_PARENT: return "~~";
    case LAMBDA_TOK_SLASH: return "/";
    case LAMBDA_TOK_PLUS: return "+";
    case LAMBDA_TOK_PLUS_PLUS: return "++";
    case LAMBDA_TOK_MINUS: return "-";
    case LAMBDA_TOK_STAR: return "*";
    case LAMBDA_TOK_STAR_STAR: return "**";
    case LAMBDA_TOK_PERCENT: return "%";
    case LAMBDA_TOK_AMPERSAND: return "&";
    case LAMBDA_TOK_PIPE: return "|";
    case LAMBDA_TOK_PIPE_FORWARD: return "|>";
    case LAMBDA_TOK_BANG: return "!";
    case LAMBDA_TOK_EQ: return "=";
    case LAMBDA_TOK_EQ_EQ: return "==";
    case LAMBDA_TOK_BANG_EQ: return "!=";
    case LAMBDA_TOK_LT: return "<";
    case LAMBDA_TOK_LT_EQ: return "<=";
    case LAMBDA_TOK_GT: return ">";
    case LAMBDA_TOK_GT_EQ: return ">=";
    case LAMBDA_TOK_ARROW: return "=>";
    case LAMBDA_TOK_ELLIPSIS: return "...";
    default: return "keyword";
    }
}
