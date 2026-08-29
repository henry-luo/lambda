#include "js_parser.h"
#include "../../runtime/parser/source_parser.h"
#include "../js_unicode_ident.h"
#include "utf8proc.h"

#include <string.h>

// keep cursor arithmetic checked so malformed or oversized input cannot stall
#define js_lexer_has(lexer, count) \
    parser_source_has((const ParserSource*)(lexer), (count))
#define js_lexer_peek(lexer, ahead) \
    parser_source_peek((const ParserSource*)(lexer), (ahead))
#define js_lexer_advance_byte(lexer) \
    parser_source_advance_byte((ParserSource*)(lexer))

static bool js_lexer_is_line_terminator_byte(unsigned char ch) {
    return ch == '\r' || ch == '\n';
}

static bool js_lexer_is_unicode_line_terminator(const JsLexer* lexer,
        size_t* width_out) {
    if (!js_lexer_has(lexer, 3)) return false;
    unsigned char a = js_lexer_peek(lexer, 0);
    unsigned char b = js_lexer_peek(lexer, 1);
    unsigned char c = js_lexer_peek(lexer, 2);
    if (a == 0xE2 && b == 0x80 && (c == 0xA8 || c == 0xA9)) {
        if (width_out) *width_out = 3;
        return true;
    }
    return false;
}

static bool js_lexer_is_unicode_space(const JsLexer* lexer, size_t* width_out,
        bool* line_terminator_out) {
    if (!lexer || !js_lexer_has(lexer, 2)) return false;
    unsigned char a = js_lexer_peek(lexer, 0);
    unsigned char b = js_lexer_peek(lexer, 1);
    unsigned char c = js_lexer_peek(lexer, 2);
    size_t width = 0;
    bool line_terminator = false;
    if (a == 0xC2 && b == 0xA0) width = 2;
    else if (a == 0xE1 && b == 0x9A && c == 0x80) width = 3;
    else if (a == 0xE2 && b == 0x80 &&
            ((c >= 0x80 && c <= 0x8A) || c == 0xAF)) width = 3;
    else if (a == 0xE2 && b == 0x81 && c == 0x9F) width = 3;
    else if (a == 0xE3 && b == 0x80 && c == 0x80) width = 3;
    else if (a == 0xEF && b == 0xBB && c == 0xBF) width = 3;
    else if (a == 0xE2 && b == 0x80 && (c == 0xA8 || c == 0xA9)) {
        width = 3;
        line_terminator = true;
    }
    if (!width) return false;
    if (width_out) *width_out = width;
    if (line_terminator_out) *line_terminator_out = line_terminator;
    return true;
}

static void js_lexer_advance_newline(JsLexer* lexer) {
    if (!lexer) return;
    if (js_lexer_peek(lexer, 0) == '\r') {
        lexer->offset++;
        if (js_lexer_peek(lexer, 0) == '\n') lexer->offset++;
    } else if (js_lexer_peek(lexer, 0) == '\n') {
        lexer->offset++;
    } else {
        size_t width = 0;
        if (js_lexer_is_unicode_line_terminator(lexer, &width)) {
            lexer->offset += width;
        } else {
            return;
        }
    }
    lexer->line++;
    lexer->column = 0;
}

static bool js_lexer_is_horizontal_space(unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v';
}

static bool js_lexer_is_ascii_digit(unsigned char ch) {
    return ch >= '0' && ch <= '9';
}

static bool js_lexer_is_hex_digit(unsigned char ch) {
    return js_lexer_is_ascii_digit(ch) ||
        (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

static bool js_lexer_is_ascii_alpha(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

static bool js_lexer_is_ident_start_ascii(unsigned char ch) {
    return js_lexer_is_ascii_alpha(ch) || ch == '_' || ch == '$' || ch >= 0x80;
}

static bool js_lexer_is_ident_start_codepoint(uint32_t codepoint) {
    if (codepoint < 0x80) {
        return js_lexer_is_ident_start_ascii((unsigned char)codepoint);
    }
    return js_unicode_id_is_start(codepoint);
}

static bool js_lexer_is_ident_continue_codepoint(uint32_t codepoint) {
    return js_unicode_id_is_continue(codepoint);
}

static bool js_lexer_decode_codepoint(const JsLexer* lexer, size_t offset,
        uint32_t* codepoint, size_t* width) {
    if (!lexer || offset >= lexer->length || !codepoint || !width) return false;
    int decoded = utf8proc_iterate((const utf8proc_uint8_t*)lexer->source + offset,
        (utf8proc_ssize_t)(lexer->length - offset),
        (utf8proc_int32_t*)codepoint);
    if (decoded <= 0) return false;
    *width = (size_t)decoded;
    return true;
}

static bool js_lexer_is_ident_start_at(const JsLexer* lexer, size_t offset) {
    uint32_t codepoint = 0;
    size_t width = 0;
    if (!js_lexer_decode_codepoint(lexer, offset, &codepoint, &width)) return false;
    return js_lexer_is_ident_start_codepoint(codepoint);
}

static bool js_lexer_is_ident_continue_at(const JsLexer* lexer, size_t offset) {
    uint32_t codepoint = 0;
    size_t width = 0;
    if (!js_lexer_decode_codepoint(lexer, offset, &codepoint, &width)) return false;
    return js_lexer_is_ident_continue_codepoint(codepoint);
}

static size_t js_lexer_unicode_escape_length(const JsLexer* lexer) {
    if (!lexer || js_lexer_peek(lexer, 0) != '\\' ||
            js_lexer_peek(lexer, 1) != 'u') return 0;
    if (js_lexer_peek(lexer, 2) == '{') {
        size_t i = 3;
        if (!js_lexer_is_hex_digit(js_lexer_peek(lexer, i))) return 0;
        while (js_lexer_is_hex_digit(js_lexer_peek(lexer, i))) i++;
        return js_lexer_peek(lexer, i) == '}' ? i + 1 : 0;
    }
    for (size_t i = 2; i < 6; i++) {
        if (!js_lexer_is_hex_digit(js_lexer_peek(lexer, i))) return 0;
    }
    return 6;
}

static void js_lexer_advance_ident_unit(JsLexer* lexer) {
    size_t escaped = js_lexer_unicode_escape_length(lexer);
    if (escaped) {
        lexer->offset += escaped;
        lexer->column++;
    } else {
        uint32_t codepoint = 0;
        size_t width = 0;
        if (js_lexer_decode_codepoint(lexer, lexer->offset, &codepoint, &width)) {
            lexer->offset += width;
            lexer->column++;
        } else {
            js_lexer_advance_byte(lexer);
        }
    }
}

static bool js_lexer_word_equals(const char* source, size_t start, size_t end,
        const char* word) {
    size_t length = strlen(word);
    return end - start == length && memcmp(source + start, word, length) == 0;
}

static JsTokenKind js_lexer_keyword_kind(const char* source, size_t start,
        size_t end) {
    struct Keyword { const char* text; JsTokenKind kind; };
    static const struct Keyword keywords[] = {
        {"break", JS_TOK_BREAK}, {"case", JS_TOK_CASE},
        {"catch", JS_TOK_CATCH}, {"class", JS_TOK_CLASS},
        {"const", JS_TOK_CONST}, {"continue", JS_TOK_CONTINUE},
        {"debugger", JS_TOK_DEBUGGER}, {"default", JS_TOK_DEFAULT},
        {"delete", JS_TOK_DELETE}, {"do", JS_TOK_DO},
        {"else", JS_TOK_ELSE}, {"export", JS_TOK_EXPORT},
        {"extends", JS_TOK_EXTENDS}, {"finally", JS_TOK_FINALLY},
        {"for", JS_TOK_FOR}, {"function", JS_TOK_FUNCTION},
        {"if", JS_TOK_IF}, {"import", JS_TOK_IMPORT},
        {"in", JS_TOK_IN}, {"instanceof", JS_TOK_INSTANCEOF},
        {"let", JS_TOK_LET}, {"new", JS_TOK_NEW},
        {"return", JS_TOK_RETURN}, {"super", JS_TOK_SUPER},
        {"switch", JS_TOK_SWITCH}, {"this", JS_TOK_THIS},
        {"throw", JS_TOK_THROW}, {"try", JS_TOK_TRY},
        {"typeof", JS_TOK_TYPEOF}, {"var", JS_TOK_VAR},
        {"void", JS_TOK_VOID}, {"while", JS_TOK_WHILE},
        {"with", JS_TOK_WITH}, {"yield", JS_TOK_YIELD},
        {"async", JS_TOK_ASYNC}, {"await", JS_TOK_AWAIT},
        {"of", JS_TOK_OF}, {"get", JS_TOK_GET}, {"set", JS_TOK_SET},
        {"true", JS_TOK_TRUE},
        {"false", JS_TOK_FALSE}, {"null", JS_TOK_NULL},
        {"as", JS_TOK_AS}, {"asserts", JS_TOK_ASSERTS},
        {"abstract", JS_TOK_ABSTRACT}, {"any", JS_TOK_ANY},
        {"boolean", JS_TOK_BOOLEAN}, {"declare", JS_TOK_DECLARE},
        {"enum", JS_TOK_ENUM}, {"from", JS_TOK_FROM},
        {"implements", JS_TOK_IMPLEMENTS}, {"infer", JS_TOK_INFER},
        {"interface", JS_TOK_INTERFACE}, {"is", JS_TOK_IS},
        {"keyof", JS_TOK_KEYOF}, {"module", JS_TOK_MODULE},
        {"namespace", JS_TOK_NAMESPACE}, {"never", JS_TOK_NEVER},
        {"number", JS_TOK_NUMBER_TYPE}, {"object", JS_TOK_OBJECT},
        {"package", JS_TOK_PACKAGE}, {"private", JS_TOK_PRIVATE},
        {"protected", JS_TOK_PROTECTED}, {"public", JS_TOK_PUBLIC},
        {"readonly", JS_TOK_READONLY}, {"require", JS_TOK_REQUIRE},
        {"satisfies", JS_TOK_SATISFIES}, {"static", JS_TOK_STATIC},
        {"string", JS_TOK_STRING_TYPE}, {"symbol", JS_TOK_SYMBOL},
        {"type", JS_TOK_TYPE}, {"unknown", JS_TOK_UNKNOWN},
    };
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (js_lexer_word_equals(source, start, end, keywords[i].text)) {
            return keywords[i].kind;
        }
    }
    return JS_TOK_IDENTIFIER;
}

static JsToken js_lexer_make_token(JsTokenKind kind, size_t start,
        uint32_t line, uint32_t column, size_t end, bool line_terminator_before) {
    JsToken token;
    token.kind = kind;
    token.span.start_byte = start > UINT32_MAX ? UINT32_MAX : (uint32_t)start;
    token.span.end_byte = end > UINT32_MAX ? UINT32_MAX : (uint32_t)end;
    token.line = line;
    token.column = column;
    token.line_terminator_before = line_terminator_before;
    token.escaped_identifier = false;
    token.has_invalid_escape = false;
    return token;
}

static JsToken js_lexer_error(JsLexer* lexer, size_t start, uint32_t line,
        uint32_t column) {
    if (lexer->offset == start && lexer->offset < lexer->length) {
        js_lexer_advance_byte(lexer);
    }
    return js_lexer_make_token(JS_TOK_ERROR, start, line, column, lexer->offset,
        lexer->line_terminator_before);
}

static bool js_lexer_skip_comment(JsLexer* lexer, bool* line_terminator_out) {
    if (js_lexer_peek(lexer, 0) != '/' || js_lexer_peek(lexer, 1) != '/') return true;
    js_lexer_advance_byte(lexer);
    js_lexer_advance_byte(lexer);
    while (lexer->offset < lexer->length) {
        size_t width = 0;
        if (js_lexer_is_line_terminator_byte(js_lexer_peek(lexer, 0)) ||
                js_lexer_is_unicode_line_terminator(lexer, &width)) {
            *line_terminator_out = true;
            return true;
        }
        js_lexer_advance_byte(lexer);
    }
    return true;
}

static bool js_lexer_skip_extras(JsLexer* lexer, bool* line_terminator_out) {
    bool line_terminator = false;
    for (;;) {
        while (js_lexer_is_horizontal_space(js_lexer_peek(lexer, 0))) {
            js_lexer_advance_byte(lexer);
        }

        size_t width = 0;
        bool unicode_line = false;
        if (js_lexer_is_unicode_space(lexer, &width, &unicode_line)) {
            if (unicode_line) line_terminator = true;
            lexer->offset += width;
            lexer->column++;
            continue;
        }
        if (js_lexer_is_line_terminator_byte(js_lexer_peek(lexer, 0)) ||
                js_lexer_is_unicode_line_terminator(lexer, &width)) {
            line_terminator = true;
            js_lexer_advance_newline(lexer);
            continue;
        }

        if (js_lexer_peek(lexer, 0) == '<' && js_lexer_peek(lexer, 1) == '!' &&
                js_lexer_peek(lexer, 2) == '-' && js_lexer_peek(lexer, 3) == '-') {
            js_lexer_advance_byte(lexer);
            js_lexer_advance_byte(lexer);
            js_lexer_advance_byte(lexer);
            js_lexer_advance_byte(lexer);
            while (lexer->offset < lexer->length) {
                size_t width = 0;
                if (js_lexer_is_line_terminator_byte(js_lexer_peek(lexer, 0)) ||
                        js_lexer_is_unicode_line_terminator(lexer, &width)) {
                    line_terminator = true;
                    break;
                }
                js_lexer_advance_byte(lexer);
            }
            continue;
        }
        if (js_lexer_peek(lexer, 0) == '-' && js_lexer_peek(lexer, 1) == '-' &&
                js_lexer_peek(lexer, 2) == '>') {
            js_lexer_advance_byte(lexer);
            js_lexer_advance_byte(lexer);
            js_lexer_advance_byte(lexer);
            while (lexer->offset < lexer->length) {
                size_t width = 0;
                if (js_lexer_is_line_terminator_byte(js_lexer_peek(lexer, 0)) ||
                        js_lexer_is_unicode_line_terminator(lexer, &width)) {
                    line_terminator = true;
                    break;
                }
                js_lexer_advance_byte(lexer);
            }
            continue;
        }
        if (js_lexer_peek(lexer, 0) != '/' || js_lexer_peek(lexer, 1) != '/') break;
        if (!js_lexer_skip_comment(lexer, &line_terminator)) return false;
        continue;
    }
    if (line_terminator_out) *line_terminator_out = line_terminator;
    return true;
}

static bool js_lexer_skip_block_comment(JsLexer* lexer, bool* line_terminator_out) {
    bool line_terminator = false;
    js_lexer_advance_byte(lexer);
    js_lexer_advance_byte(lexer);
    while (lexer->offset < lexer->length) {
        if (js_lexer_peek(lexer, 0) == '*' && js_lexer_peek(lexer, 1) == '/') {
            js_lexer_advance_byte(lexer);
            js_lexer_advance_byte(lexer);
            if (line_terminator_out) *line_terminator_out = line_terminator;
            return true;
        }
        size_t width = 0;
        if (js_lexer_is_line_terminator_byte(js_lexer_peek(lexer, 0)) ||
                js_lexer_is_unicode_line_terminator(lexer, &width)) {
            line_terminator = true;
            js_lexer_advance_newline(lexer);
        } else {
            js_lexer_advance_byte(lexer);
        }
    }
    if (line_terminator_out) *line_terminator_out = line_terminator;
    return false;
}

static void js_lexer_skip_escape(JsLexer* lexer) {
    if (js_lexer_peek(lexer, 0) != '\\') return;
    js_lexer_advance_byte(lexer);
    if (js_lexer_is_line_terminator_byte(js_lexer_peek(lexer, 0)) ||
            js_lexer_is_unicode_line_terminator(lexer, NULL)) {
        js_lexer_advance_newline(lexer);
    } else if (lexer->offset < lexer->length) {
        js_lexer_advance_byte(lexer);
    }
}

static JsToken js_lexer_scan_string(JsLexer* lexer, size_t start,
        uint32_t line, uint32_t column, unsigned char quote,
        bool line_terminator_before) {
    js_lexer_advance_byte(lexer);
    while (lexer->offset < lexer->length) {
        unsigned char ch = js_lexer_peek(lexer, 0);
        if (ch == quote) {
            js_lexer_advance_byte(lexer);
            return js_lexer_make_token(JS_TOK_STRING, start, line, column,
                lexer->offset, line_terminator_before);
        }
        if (ch == '\\') {
            js_lexer_skip_escape(lexer);
            continue;
        }
        if (js_lexer_is_line_terminator_byte(ch)) {
            return js_lexer_error(lexer, start, line, column);
        }
        if (js_lexer_is_unicode_line_terminator(lexer, NULL)) {
            // JSON superset string literals admit U+2028 and U+2029 as
            // literal code points; they still advance the source position.
            js_lexer_advance_newline(lexer);
            continue;
        }
        js_lexer_advance_byte(lexer);
    }
    return js_lexer_error(lexer, start, line, column);
}

static JsToken js_lexer_scan_regex(JsLexer* lexer, size_t start,
        uint32_t line, uint32_t column, bool line_terminator_before) {
    bool in_class = false;
    js_lexer_advance_byte(lexer);
    while (lexer->offset < lexer->length) {
        unsigned char ch = js_lexer_peek(lexer, 0);
        if (ch == '\\') {
            js_lexer_skip_escape(lexer);
            continue;
        }
        if (js_lexer_is_line_terminator_byte(ch) ||
                js_lexer_is_unicode_line_terminator(lexer, NULL)) {
            return js_lexer_error(lexer, start, line, column);
        }
        if (ch == '[') in_class = true;
        else if (ch == ']' && in_class) in_class = false;
        else if (ch == '/' && !in_class) {
            js_lexer_advance_byte(lexer);
            while (js_lexer_is_ident_continue_at(lexer, lexer->offset) ||
                    js_lexer_unicode_escape_length(lexer)) {
                js_lexer_advance_ident_unit(lexer);
            }
            return js_lexer_make_token(JS_TOK_REGEXP, start, line, column,
                lexer->offset, line_terminator_before);
        }
        js_lexer_advance_byte(lexer);
    }
    return js_lexer_error(lexer, start, line, column);
}

static JsToken js_lexer_scan_template_segment(JsLexer* lexer, size_t start,
        uint32_t line, uint32_t column, bool line_terminator_before,
        bool opening) {
    if (opening) {
        if (lexer->template_depth >= 32) {
            return js_lexer_error(lexer, start, line, column);
        }
        lexer->template_depth++;
        lexer->template_braces[lexer->template_depth - 1] = 0;
        js_lexer_advance_byte(lexer);
    }
    while (lexer->offset < lexer->length) {
        unsigned char ch = js_lexer_peek(lexer, 0);
        if (ch == '`') {
            js_lexer_advance_byte(lexer);
            if (lexer->template_depth) lexer->template_depth--;
            return js_lexer_make_token(JS_TOK_TEMPLATE, start, line, column,
                lexer->offset, line_terminator_before);
        }
        if (ch == '\\') {
            js_lexer_skip_escape(lexer);
            continue;
        }
        if (ch == '$' && js_lexer_peek(lexer, 1) == '{') {
            js_lexer_advance_byte(lexer);
            js_lexer_advance_byte(lexer);
            if (!lexer->template_depth) return js_lexer_error(lexer, start, line, column);
            lexer->template_braces[lexer->template_depth - 1] = 1;
            return js_lexer_make_token(JS_TOK_TEMPLATE, start, line, column,
                lexer->offset, line_terminator_before);
        }
        if (js_lexer_is_line_terminator_byte(ch) ||
                js_lexer_is_unicode_line_terminator(lexer, NULL)) {
            js_lexer_advance_newline(lexer);
        } else {
            js_lexer_advance_byte(lexer);
        }
    }
    return js_lexer_error(lexer, start, line, column);
}

static bool js_lexer_scan_digits(JsLexer* lexer, bool (*is_digit)(unsigned char),
        bool* invalid_separator) {
    bool saw_digit = false;
    bool previous_separator = false;
    while (is_digit(js_lexer_peek(lexer, 0)) || js_lexer_peek(lexer, 0) == '_') {
        unsigned char ch = js_lexer_peek(lexer, 0);
        if (ch == '_') {
            if (!saw_digit || previous_separator ||
                    !is_digit(js_lexer_peek(lexer, 1))) {
                *invalid_separator = true;
            }
            previous_separator = true;
        } else {
            saw_digit = true;
            previous_separator = false;
        }
        js_lexer_advance_byte(lexer);
    }
    if (previous_separator) *invalid_separator = true;
    return saw_digit;
}

static bool js_lexer_is_decimal_digit(unsigned char ch) {
    return js_lexer_is_ascii_digit(ch);
}

static JsToken js_lexer_scan_number(JsLexer* lexer, size_t start,
        uint32_t line, uint32_t column, bool line_terminator_before) {
    bool invalid_separator = false;
    bool bigint = false;
    bool valid = true;
    if (js_lexer_peek(lexer, 0) == '0' &&
            (js_lexer_peek(lexer, 1) == 'x' || js_lexer_peek(lexer, 1) == 'X' ||
             js_lexer_peek(lexer, 1) == 'b' || js_lexer_peek(lexer, 1) == 'B' ||
             js_lexer_peek(lexer, 1) == 'o' || js_lexer_peek(lexer, 1) == 'O')) {
        unsigned char base = js_lexer_peek(lexer, 1);
        js_lexer_advance_byte(lexer);
        js_lexer_advance_byte(lexer);
        bool saw_digit = false;
        bool previous_separator = false;
        while (js_lexer_peek(lexer, 0) || js_lexer_peek(lexer, 0) == '_') {
            unsigned char ch = js_lexer_peek(lexer, 0);
            bool digit = base == 'x' || base == 'X' ? js_lexer_is_hex_digit(ch) :
                (base == 'b' || base == 'B' ? (ch == '0' || ch == '1') :
                 (ch >= '0' && ch <= '7'));
            if (!digit && ch != '_') break;
            if (ch == '_') {
                if (!saw_digit || previous_separator) invalid_separator = true;
                previous_separator = true;
            } else {
                saw_digit = true;
                previous_separator = false;
            }
            js_lexer_advance_byte(lexer);
        }
        if (!saw_digit || previous_separator) valid = false;
        if (js_lexer_peek(lexer, 0) == 'n') {
            bigint = true;
            js_lexer_advance_byte(lexer);
        }
    } else {
        if (js_lexer_peek(lexer, 0) == '.') {
            js_lexer_advance_byte(lexer);
            if (!js_lexer_scan_digits(lexer, js_lexer_is_decimal_digit,
                    &invalid_separator)) valid = false;
        } else {
            if (!js_lexer_scan_digits(lexer, js_lexer_is_decimal_digit,
                    &invalid_separator)) valid = false;
            if (js_lexer_peek(lexer, 0) == '.') {
                js_lexer_advance_byte(lexer);
                js_lexer_scan_digits(lexer, js_lexer_is_decimal_digit,
                    &invalid_separator);
            }
        }
        if (js_lexer_peek(lexer, 0) == 'e' || js_lexer_peek(lexer, 0) == 'E') {
            js_lexer_advance_byte(lexer);
            if (js_lexer_peek(lexer, 0) == '+' || js_lexer_peek(lexer, 0) == '-') {
                js_lexer_advance_byte(lexer);
            }
            if (!js_lexer_scan_digits(lexer, js_lexer_is_decimal_digit,
                    &invalid_separator)) valid = false;
        }
        if (js_lexer_peek(lexer, 0) == 'n') {
            bigint = true;
            js_lexer_advance_byte(lexer);
            if (memchr(lexer->source + start, '.', lexer->offset - start) ||
                    memchr(lexer->source + start, 'e', lexer->offset - start) ||
                    memchr(lexer->source + start, 'E', lexer->offset - start)) {
                valid = false;
            }
        }
    }
    if (js_lexer_is_ident_start_at(lexer, lexer->offset)) valid = false;
    if (invalid_separator || !valid) return js_lexer_error(lexer, start, line, column);
    return js_lexer_make_token(bigint ? JS_TOK_BIGINT : JS_TOK_NUMBER, start,
        line, column, lexer->offset, line_terminator_before);
}

static JsToken js_lexer_punct(JsLexer* lexer, size_t start, uint32_t line,
        uint32_t column, bool line_terminator_before, JsTokenKind kind,
        size_t width) {
    for (size_t i = 0; i < width; i++) js_lexer_advance_byte(lexer);
    return js_lexer_make_token(kind, start, line, column, lexer->offset,
        line_terminator_before);
}

void js_lexer_init(JsLexer* lexer, const char* source, size_t length) {
    if (!lexer) return;
    memset(lexer, 0, sizeof(*lexer));
    parser_source_init((ParserSource*)lexer, source, length, 0);
    lexer->goal = JS_LEX_REGEXP;
}

void js_lexer_set_goal(JsLexer* lexer, JsLexGoal goal) {
    if (lexer) lexer->goal = goal;
}

JsToken js_lexer_next(JsLexer* lexer) {
    if (!lexer || !lexer->source || lexer->offset > lexer->length) {
        JsToken token = {0};
        token.kind = JS_TOK_ERROR;
        return token;
    }
    if (lexer->template_continuation) {
        size_t start = lexer->offset;
        uint32_t line = lexer->line;
        uint32_t column = lexer->column;
        lexer->template_continuation = false;
        return js_lexer_scan_template_segment(lexer, start, line, column,
            false, false);
    }
    bool line_terminator_before = false;
    for (;;) {
        bool extra_line_terminator = false;
        if (!js_lexer_skip_extras(lexer, &extra_line_terminator)) {
            return js_lexer_error(lexer, lexer->offset, lexer->line, lexer->column);
        }
        line_terminator_before |= extra_line_terminator;
        if (js_lexer_peek(lexer, 0) != '/' || js_lexer_peek(lexer, 1) != '*') break;
        bool comment_line = false;
        size_t comment_start = lexer->offset;
        if (!js_lexer_skip_block_comment(lexer, &comment_line)) {
            return js_lexer_error(lexer, comment_start, lexer->line, lexer->column);
        }
        if (comment_line) line_terminator_before = true;
    }
    lexer->line_terminator_before = line_terminator_before;

    size_t start = lexer->offset;
    uint32_t line = lexer->line;
    uint32_t column = lexer->column;
    unsigned char ch = js_lexer_peek(lexer, 0);
    if (!ch) {
        return js_lexer_make_token(JS_TOK_EOF, start, line, column, start,
            line_terminator_before);
    }
    if (start == 0 && ch == '#' && js_lexer_peek(lexer, 1) == '!') {
        while (lexer->offset < lexer->length &&
                !js_lexer_is_line_terminator_byte(js_lexer_peek(lexer, 0)) &&
                !js_lexer_is_unicode_line_terminator(lexer, NULL)) {
            js_lexer_advance_byte(lexer);
        }
        return js_lexer_make_token(JS_TOK_HASHBANG, start, line, column,
            lexer->offset, line_terminator_before);
    }
    if (ch == '#') {
        JsLexer private_name_probe = *lexer;
        private_name_probe.offset++;
        if (!js_lexer_is_ident_start_at(lexer, lexer->offset + 1) &&
                !js_lexer_unicode_escape_length(&private_name_probe)) {
            return js_lexer_error(lexer, start, line, column);
        }
        js_lexer_advance_byte(lexer);
        while (js_lexer_is_ident_continue_at(lexer, lexer->offset) ||
                js_lexer_unicode_escape_length(lexer)) js_lexer_advance_ident_unit(lexer);
        return js_lexer_make_token(JS_TOK_PRIVATE_IDENTIFIER, start, line, column,
            lexer->offset, line_terminator_before);
    }
    if (js_lexer_is_ident_start_at(lexer, lexer->offset) ||
            js_lexer_unicode_escape_length(lexer)) {
        bool escaped = false;
        while (js_lexer_is_ident_continue_at(lexer, lexer->offset) ||
                js_lexer_unicode_escape_length(lexer)) {
            if (js_lexer_peek(lexer, 0) == '\\') escaped = true;
            js_lexer_advance_ident_unit(lexer);
        }
        JsTokenKind kind = escaped ? JS_TOK_IDENTIFIER :
            js_lexer_keyword_kind(lexer->source, start, lexer->offset);
        JsToken token = js_lexer_make_token(kind, start, line, column, lexer->offset,
            line_terminator_before);
        token.escaped_identifier = escaped;
        return token;
    }
    if (js_lexer_is_ascii_digit(ch) || (ch == '.' && js_lexer_is_ascii_digit(js_lexer_peek(lexer, 1)))) {
        return js_lexer_scan_number(lexer, start, line, column,
            line_terminator_before);
    }
    if (ch == '\'' || ch == '"') {
        return js_lexer_scan_string(lexer, start, line, column, ch,
            line_terminator_before);
    }
    if (ch == '`') {
        return js_lexer_scan_template_segment(lexer, start, line, column,
            line_terminator_before, true);
    }
    if (ch == '/' && lexer->goal == JS_LEX_REGEXP &&
            js_lexer_peek(lexer, 1) != '/' && js_lexer_peek(lexer, 1) != '*') {
        return js_lexer_scan_regex(lexer, start, line, column,
            line_terminator_before);
    }

    if (ch == '.' && js_lexer_peek(lexer, 1) == '.' && js_lexer_peek(lexer, 2) == '.')
        return js_lexer_punct(lexer, start, line, column, line_terminator_before,
            JS_TOK_ELLIPSIS, 3);
    if (ch == '?' && js_lexer_peek(lexer, 1) == '.' &&
            !js_lexer_is_ascii_digit(js_lexer_peek(lexer, 2)))
        return js_lexer_punct(lexer, start, line, column, line_terminator_before,
            JS_TOK_QUESTION_DOT, 2);
    if (ch == '=' && js_lexer_peek(lexer, 1) == '>' )
        return js_lexer_punct(lexer, start, line, column, line_terminator_before,
            JS_TOK_ARROW, 2);
    if (ch == '+' && js_lexer_peek(lexer, 1) == '+')
        return js_lexer_punct(lexer, start, line, column, line_terminator_before,
            JS_TOK_PLUS_PLUS, 2);
    if (ch == '-' && js_lexer_peek(lexer, 1) == '-')
        return js_lexer_punct(lexer, start, line, column, line_terminator_before,
            JS_TOK_MINUS_MINUS, 2);
    if (ch == '*' && js_lexer_peek(lexer, 1) == '*') {
        if (js_lexer_peek(lexer, 2) == '=')
            return js_lexer_punct(lexer, start, line, column, line_terminator_before,
                JS_TOK_EXP_EQUAL, 3);
        return js_lexer_punct(lexer, start, line, column, line_terminator_before,
            JS_TOK_EXP, 2);
    }
    if (ch == '=' && js_lexer_peek(lexer, 1) == '=') {
        if (js_lexer_peek(lexer, 2) == '=')
            return js_lexer_punct(lexer, start, line, column, line_terminator_before,
                JS_TOK_STRICT_EQUAL, 3);
        return js_lexer_punct(lexer, start, line, column, line_terminator_before,
            JS_TOK_EQUAL_EQUAL, 2);
    }
    if (ch == '!' && js_lexer_peek(lexer, 1) == '=') {
        if (js_lexer_peek(lexer, 2) == '=')
            return js_lexer_punct(lexer, start, line, column, line_terminator_before,
                JS_TOK_STRICT_BANG_EQUAL, 3);
        return js_lexer_punct(lexer, start, line, column, line_terminator_before,
            JS_TOK_BANG_EQUAL, 2);
    }
    if ((ch == '<' || ch == '>') && js_lexer_peek(lexer, 1) == ch) {
        if (js_lexer_peek(lexer, 2) == '=')
            return js_lexer_punct(lexer, start, line, column, line_terminator_before,
                ch == '<' ? JS_TOK_LSHIFT_EQUAL : JS_TOK_RSHIFT_EQUAL, 3);
        if (ch == '>' && js_lexer_peek(lexer, 2) == '>') {
            if (js_lexer_peek(lexer, 3) == '=')
                return js_lexer_punct(lexer, start, line, column, line_terminator_before,
                    JS_TOK_URSHIFT_EQUAL, 4);
            return js_lexer_punct(lexer, start, line, column, line_terminator_before,
                JS_TOK_URSHIFT, 3);
        }
        return js_lexer_punct(lexer, start, line, column, line_terminator_before,
            ch == '<' ? JS_TOK_LSHIFT : JS_TOK_RSHIFT, 2);
    }
    if (ch == '&' && js_lexer_peek(lexer, 1) == '&') {
        if (js_lexer_peek(lexer, 2) == '=')
            return js_lexer_punct(lexer, start, line, column, line_terminator_before,
                JS_TOK_AMP_AMP_EQUAL, 3);
        return js_lexer_punct(lexer, start, line, column, line_terminator_before,
            JS_TOK_AMP_AMP, 2);
    }
    if (ch == '|' && js_lexer_peek(lexer, 1) == '|') {
        if (js_lexer_peek(lexer, 2) == '=')
            return js_lexer_punct(lexer, start, line, column, line_terminator_before,
                JS_TOK_PIPE_PIPE_EQUAL, 3);
        return js_lexer_punct(lexer, start, line, column, line_terminator_before,
            JS_TOK_PIPE_PIPE, 2);
    }
    if (ch == '?' && js_lexer_peek(lexer, 1) == '?') {
        if (js_lexer_peek(lexer, 2) == '=')
            return js_lexer_punct(lexer, start, line, column, line_terminator_before,
                JS_TOK_NULLISH_EQUAL, 3);
        return js_lexer_punct(lexer, start, line, column, line_terminator_before,
            JS_TOK_NULLISH, 2);
    }

    if (ch == '{') {
        JsToken token = js_lexer_punct(lexer, start, line, column,
            line_terminator_before, JS_TOK_LBRACE, 1);
        if (lexer->template_depth &&
                lexer->template_braces[lexer->template_depth - 1]) {
            lexer->template_braces[lexer->template_depth - 1]++;
        }
        return token;
    }
    if (ch == '}') {
        JsToken token = js_lexer_punct(lexer, start, line, column,
            line_terminator_before, JS_TOK_RBRACE, 1);
        if (lexer->template_depth &&
                lexer->template_braces[lexer->template_depth - 1]) {
            uint32_t* braces = &lexer->template_braces[
                lexer->template_depth - 1];
            if (*braces == 1) {
                lexer->template_continuation = true;
            } else {
                (*braces)--;
            }
        }
        return token;
    }

    switch (ch) {
    case '(': return js_lexer_punct(lexer, start, line, column, line_terminator_before, JS_TOK_LPAREN, 1);
    case ')': return js_lexer_punct(lexer, start, line, column, line_terminator_before, JS_TOK_RPAREN, 1);
    case '[': return js_lexer_punct(lexer, start, line, column, line_terminator_before, JS_TOK_LBRACKET, 1);
    case ']': return js_lexer_punct(lexer, start, line, column, line_terminator_before, JS_TOK_RBRACKET, 1);
    case '.': return js_lexer_punct(lexer, start, line, column, line_terminator_before, JS_TOK_DOT, 1);
    case ',': return js_lexer_punct(lexer, start, line, column, line_terminator_before, JS_TOK_COMMA, 1);
    case ';': return js_lexer_punct(lexer, start, line, column, line_terminator_before, JS_TOK_SEMICOLON, 1);
    case ':': return js_lexer_punct(lexer, start, line, column, line_terminator_before, JS_TOK_COLON, 1);
    case '?': return js_lexer_punct(lexer, start, line, column, line_terminator_before, JS_TOK_QUESTION, 1);
    case '@': return js_lexer_punct(lexer, start, line, column, line_terminator_before, JS_TOK_AT, 1);
    case '+': return js_lexer_punct(lexer, start, line, column, line_terminator_before, js_lexer_peek(lexer, 1) == '=' ? JS_TOK_PLUS_EQUAL : JS_TOK_PLUS, js_lexer_peek(lexer, 1) == '=' ? 2 : 1);
    case '-': return js_lexer_punct(lexer, start, line, column, line_terminator_before, js_lexer_peek(lexer, 1) == '=' ? JS_TOK_MINUS_EQUAL : JS_TOK_MINUS, js_lexer_peek(lexer, 1) == '=' ? 2 : 1);
    case '*': return js_lexer_punct(lexer, start, line, column, line_terminator_before, js_lexer_peek(lexer, 1) == '=' ? JS_TOK_STAR_EQUAL : JS_TOK_STAR, js_lexer_peek(lexer, 1) == '=' ? 2 : 1);
    case '/': return js_lexer_punct(lexer, start, line, column, line_terminator_before, js_lexer_peek(lexer, 1) == '=' ? JS_TOK_SLASH_EQUAL : JS_TOK_SLASH, js_lexer_peek(lexer, 1) == '=' ? 2 : 1);
    case '%': return js_lexer_punct(lexer, start, line, column, line_terminator_before, js_lexer_peek(lexer, 1) == '=' ? JS_TOK_PERCENT_EQUAL : JS_TOK_PERCENT, js_lexer_peek(lexer, 1) == '=' ? 2 : 1);
    case '!': return js_lexer_punct(lexer, start, line, column, line_terminator_before, JS_TOK_BANG, 1);
    case '~': return js_lexer_punct(lexer, start, line, column, line_terminator_before, JS_TOK_TILDE, 1);
    case '&': return js_lexer_punct(lexer, start, line, column, line_terminator_before, js_lexer_peek(lexer, 1) == '=' ? JS_TOK_AMP_EQUAL : JS_TOK_AMP, js_lexer_peek(lexer, 1) == '=' ? 2 : 1);
    case '|': return js_lexer_punct(lexer, start, line, column, line_terminator_before, js_lexer_peek(lexer, 1) == '=' ? JS_TOK_PIPE_EQUAL : JS_TOK_PIPE, js_lexer_peek(lexer, 1) == '=' ? 2 : 1);
    case '^': return js_lexer_punct(lexer, start, line, column, line_terminator_before, js_lexer_peek(lexer, 1) == '=' ? JS_TOK_CARET_EQUAL : JS_TOK_CARET, js_lexer_peek(lexer, 1) == '=' ? 2 : 1);
    case '=': return js_lexer_punct(lexer, start, line, column, line_terminator_before, JS_TOK_EQUAL, 1);
    case '<': return js_lexer_punct(lexer, start, line, column, line_terminator_before, js_lexer_peek(lexer, 1) == '=' ? JS_TOK_LTE : JS_TOK_LT, js_lexer_peek(lexer, 1) == '=' ? 2 : 1);
    case '>': return js_lexer_punct(lexer, start, line, column, line_terminator_before, js_lexer_peek(lexer, 1) == '=' ? JS_TOK_GTE : JS_TOK_GT, js_lexer_peek(lexer, 1) == '=' ? 2 : 1);
    default: return js_lexer_error(lexer, start, line, column);
    }
}
