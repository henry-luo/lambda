#include "js_c_ast_helpers.hpp"
#include "../ts/ts_ast.hpp"
#include "../../lib/mempool.h"
#include "../../lib/mem.h"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/utf.h"

#include <cstring>
#include <cstdlib>

Type* js_set_type_any(JsTranspiler* tp, AnyReason reason) {
    if (tp && reason >= 0 && reason < ANY_REASON_COUNT) {
        tp->any_census[reason]++;
    }
    return &TYPE_ANY;
}

void js_report_any_census(JsTranspiler* tp) {
    if (!tp) return;
    int any_total = 0;
    for (int reason = 0; reason < ANY_REASON_COUNT; reason++) {
        any_total += tp->any_census[reason];
    }
    if (!any_total) return;
    StrBuf* census = strbuf_new();
    if (!census) return;
    strbuf_append_format(census, "any_census: total=%d", any_total);
    for (int reason = 0; reason < ANY_REASON_COUNT; reason++) {
        if (!tp->any_census[reason]) continue;
        strbuf_append_format(census, " %s=%d",
            any_reason_name((AnyReason)reason), tp->any_census[reason]);
    }
    log_notice("%s (js)", census->str);
    strbuf_free(census);
}

JsOperator js_operator_from_string(const char* op_str, size_t len) {
    if (len == 1) {
        switch (op_str[0]) {
        case '+': return JS_OP_ADD;
        case '-': return JS_OP_SUB;
        case '*': return JS_OP_MUL;
        case '/': return JS_OP_DIV;
        case '%': return JS_OP_MOD;
        case '<': return JS_OP_LT;
        case '>': return JS_OP_GT;
        case '!': return JS_OP_NOT;
        case '~': return JS_OP_BIT_NOT;
        case '&': return JS_OP_BIT_AND;
        case '|': return JS_OP_BIT_OR;
        case '^': return JS_OP_BIT_XOR;
        case '=': return JS_OP_ASSIGN;
        }
    } else if (len == 2) {
        if (strncmp(op_str, "==", 2) == 0) return JS_OP_EQ;
        if (strncmp(op_str, "!=", 2) == 0) return JS_OP_NE;
        if (strncmp(op_str, "<=", 2) == 0) return JS_OP_LE;
        if (strncmp(op_str, ">=", 2) == 0) return JS_OP_GE;
        if (strncmp(op_str, "&&", 2) == 0) return JS_OP_AND;
        if (strncmp(op_str, "||", 2) == 0) return JS_OP_OR;
        if (strncmp(op_str, "<<", 2) == 0) return JS_OP_BIT_LSHIFT;
        if (strncmp(op_str, ">>", 2) == 0) return JS_OP_BIT_RSHIFT;
        if (strncmp(op_str, "**", 2) == 0) return JS_OP_EXP;
        if (strncmp(op_str, "++", 2) == 0) return JS_OP_INCREMENT;
        if (strncmp(op_str, "--", 2) == 0) return JS_OP_DECREMENT;
        if (strncmp(op_str, "+=", 2) == 0) return JS_OP_ADD_ASSIGN;
        if (strncmp(op_str, "-=", 2) == 0) return JS_OP_SUB_ASSIGN;
        if (strncmp(op_str, "*=", 2) == 0) return JS_OP_MUL_ASSIGN;
        if (strncmp(op_str, "/=", 2) == 0) return JS_OP_DIV_ASSIGN;
        if (strncmp(op_str, "%=", 2) == 0) return JS_OP_MOD_ASSIGN;
        if (strncmp(op_str, "&=", 2) == 0) return JS_OP_BIT_AND_ASSIGN;
        if (strncmp(op_str, "|=", 2) == 0) return JS_OP_BIT_OR_ASSIGN;
        if (strncmp(op_str, "^=", 2) == 0) return JS_OP_BIT_XOR_ASSIGN;
        if (strncmp(op_str, "??", 2) == 0) return JS_OP_NULLISH_COALESCE;
        if (strncmp(op_str, "in", 2) == 0) return JS_OP_IN;
    } else if (len == 3) {
        if (strncmp(op_str, "===", 3) == 0) return JS_OP_STRICT_EQ;
        if (strncmp(op_str, "!==", 3) == 0) return JS_OP_STRICT_NE;
        if (strncmp(op_str, ">>>", 3) == 0) return JS_OP_BIT_URSHIFT;
        if (strncmp(op_str, "**=", 3) == 0) return JS_OP_EXP_ASSIGN;
        if (strncmp(op_str, "<<=", 3) == 0) return JS_OP_LSHIFT_ASSIGN;
        if (strncmp(op_str, ">>=", 3) == 0) return JS_OP_RSHIFT_ASSIGN;
        if (strncmp(op_str, "?\?=", 3) == 0) return JS_OP_NULLISH_ASSIGN;
        if (strncmp(op_str, "&&=", 3) == 0) return JS_OP_AND_ASSIGN;
        if (strncmp(op_str, "||=", 3) == 0) return JS_OP_OR_ASSIGN;
    } else if (len == 4) {
        if (strncmp(op_str, "void", 4) == 0) return JS_OP_VOID;
        if (strncmp(op_str, ">>>=", 4) == 0) return JS_OP_URSHIFT_ASSIGN;
    } else if (len == 6) {
        if (strncmp(op_str, "typeof", 6) == 0) return JS_OP_TYPEOF;
        if (strncmp(op_str, "delete", 6) == 0) return JS_OP_DELETE;
    } else if (len == 10) {
        if (strncmp(op_str, "instanceof", 10) == 0) return JS_OP_INSTANCEOF;
    }

    log_error("Unknown JavaScript operator: %.*s", (int)len, op_str);
    return JS_OP_ADD;
}

JsOperator js_unary_operator_from_string(const char* op_str, size_t len) {
    if (len == 1 && op_str[0] == '+') return JS_OP_PLUS;
    if (len == 1 && op_str[0] == '-') return JS_OP_MINUS;
    return js_operator_from_string(op_str, len);
}

static char js_c_decode_escape_char(char c) {
    switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case '\\': return '\\';
    case '\'': return '\'';
    case '"': return '"';
    case '0': return '\0';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'v': return '\v';
    default: return c;
    }
}

static int js_c_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool js_c_hex_char(char c) {
    return js_c_hex_value(c) >= 0;
}

static size_t js_c_wtf8_encode(uint32_t cp, char* out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

static bool js_c_template_invalid_escape_at(const char* source, size_t length,
        size_t pos) {
    if (!source || pos + 1 >= length || source[pos] != '\\') return false;
    char escaped = source[pos + 1];
    if (escaped >= '1' && escaped <= '9') return true;
    if (escaped == '0' && pos + 2 < length && source[pos + 2] >= '0' &&
            source[pos + 2] <= '9') return true;
    if (escaped == 'x') {
        return pos + 3 >= length || !js_c_hex_char(source[pos + 2]) ||
            !js_c_hex_char(source[pos + 3]);
    }
    if (escaped == 'u') {
        if (pos + 2 < length && source[pos + 2] == '{') {
            size_t cursor = pos + 3;
            uint32_t codepoint = 0;
            if (cursor >= length || !js_c_hex_char(source[cursor])) return true;
            while (cursor < length && js_c_hex_char(source[cursor])) {
                codepoint = (codepoint << 4) |
                    (uint32_t)js_c_hex_value(source[cursor++]);
                if (codepoint > 0x10FFFF) return true;
            }
            return cursor >= length || source[cursor] != '}';
        }
        return pos + 5 >= length || !js_c_hex_char(source[pos + 2]) ||
            !js_c_hex_char(source[pos + 3]) || !js_c_hex_char(source[pos + 4]) ||
            !js_c_hex_char(source[pos + 5]);
    }
    return false;
}

static bool js_c_template_has_invalid_escape(const char* source, size_t length) {
    if (!source) return false;
    for (size_t pos = 0; pos < length; pos++) {
        if (js_c_template_invalid_escape_at(source, length, pos)) return true;
    }
    return false;
}

static size_t js_c_decode_unicode_escape(const char* source, size_t length,
        size_t* cursor, char* out) {
    size_t pos = *cursor;
    if (pos + 1 < length && source[pos + 1] == '{') {
        size_t digit_pos = pos + 2;
        uint32_t codepoint = 0;
        if (digit_pos >= length || !js_c_hex_char(source[digit_pos])) return 0;
        while (digit_pos < length && js_c_hex_char(source[digit_pos])) {
            codepoint = (codepoint << 4) |
                (uint32_t)js_c_hex_value(source[digit_pos++]);
            if (codepoint > 0x10FFFF) return 0;
        }
        if (digit_pos >= length || source[digit_pos] != '}') return 0;
        *cursor = digit_pos;
        return js_c_wtf8_encode(codepoint, out);
    }
    if (pos + 4 >= length) return 0;
    uint32_t codepoint = 0;
    for (size_t i = 1; i <= 4; i++) {
        int digit = js_c_hex_value(source[pos + i]);
        if (digit < 0) return 0;
        codepoint = (codepoint << 4) | (uint32_t)digit;
    }
    *cursor = pos + 4;
    // JavaScript string escapes combine a lead/trail surrogate pair into one
    // code point; retaining the two UTF-16 code units separately changes
    // matching and observable string length for supplementary characters.
    if (codepoint >= 0xD800 && codepoint <= 0xDBFF &&
            *cursor + 2 < length && source[*cursor + 1] == '\\' &&
            source[*cursor + 2] == 'u' && *cursor + 6 < length) {
        size_t trail = *cursor + 3;
        uint32_t low = 0;
        bool valid_low = true;
        for (size_t i = 0; i < 4; i++) {
            int digit = js_c_hex_value(source[trail + i]);
            if (digit < 0) {
                valid_low = false;
                break;
            }
            low = (low << 4) | (uint32_t)digit;
        }
        if (valid_low && low >= 0xDC00 && low <= 0xDFFF) {
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) +
                (low - 0xDC00);
            *cursor = trail + 3;
            return utf8_encode(codepoint, out);
        }
    }
    return js_c_wtf8_encode(codepoint, out);
}

static bool js_c_is_octal_digit(char c) {
    return c >= '0' && c <= '7';
}

static size_t js_c_decode_legacy_octal_escape(const char* source,
        size_t length, size_t start, uint32_t* out_code) {
    if (!source || start + 1 >= length || source[start] != '\\' ||
            !js_c_is_octal_digit(source[start + 1])) {
        if (out_code) *out_code = 0;
        return start;
    }

    size_t digit_pos = start + 1;
    uint32_t value = (uint32_t)(source[digit_pos] - '0');
    int max_digits = source[digit_pos] <= '3' ? 3 : 2;
    int digit_count = 1;
    while (digit_count < max_digits && digit_pos + 1 < length &&
            js_c_is_octal_digit(source[digit_pos + 1])) {
        digit_pos++;
        value = (value << 3) | (uint32_t)(source[digit_pos] - '0');
        digit_count++;
    }
    if (out_code) *out_code = value;
    return digit_pos;
}

static String* js_c_decode_identifier_name(JsTranspiler* tp,
        const char* source, size_t length) {
    if (!tp || !source || !length) return NULL;
    if (!memchr(source, '\\', length)) {
        return name_pool_create_len(tp->name_pool, source, (int)length);
    }
    char* decoded = (char*)pool_alloc(tp->pool, length + 1);
    if (!decoded) return NULL;
    size_t out = 0;
    for (size_t pos = 0; pos < length;) {
        if (source[pos] != '\\' || pos + 1 >= length || source[pos + 1] != 'u') {
            decoded[out++] = source[pos++];
            continue;
        }
        char utf8[4] = {};
        size_t escape_pos = pos + 1;
        size_t written = js_c_decode_unicode_escape(source, length,
            &escape_pos, utf8);
        if (!written) return NULL;
        for (size_t i = 0; i < written; i++) decoded[out++] = utf8[i];
        pos = escape_pos + 1;
    }
    return name_pool_create_len(tp->name_pool, decoded, (int)out);
}

#define js_decode_escape_char js_c_decode_escape_char
#define js_template_hex_char js_c_hex_char
#define js_template_has_invalid_escape js_c_template_has_invalid_escape
#define wtf8_encode js_c_wtf8_encode
#define js_decode_unicode_escape js_c_decode_unicode_escape
#define utf8_encode utf8_encode
#define js_is_octal_digit js_c_is_octal_digit
#define js_decode_legacy_octal_escape js_c_decode_legacy_octal_escape
#define js_decode_identifier_name js_c_decode_identifier_name
static JsAstNode* js_alloc_ast_node(JsTranspiler* tp,
        JsAstNodeType node_type, SourceSpan span, size_t size) {
    JsAstNode* ast_node = (JsAstNode*)pool_alloc(tp->pool, size);
    memset(ast_node, 0, size);
    ast_node->node_type = node_type;
    ast_node->source_span = span;
    return ast_node;
}

JsAstNode* alloc_js_ast_node_span(JsTranspiler* tp, JsAstNodeType node_type,
        SourceSpan span, size_t size) {
    return js_alloc_ast_node(tp, node_type, span, size);
}

JsAstNode* build_js_literal_from_source(JsTranspiler* tp, const char* node_type,
        StrView source, SourceSpan span) {
    JsLiteralNode* literal = (JsLiteralNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_LITERAL, span, sizeof(JsLiteralNode));

    if (strcmp(node_type, "number") == 0) {
        literal->literal_type = JS_LITERAL_NUMBER;
        // Check if source text ends with 'n' (BigInt literal)
        literal->is_bigint = (source.length > 0 && source.str[source.length - 1] == 'n');
        // Check if source text contains '.' or 'e'/'E' (fractional/scientific hint)
        literal->has_decimal = false;
        for (size_t i = 0; i < source.length; i++) {
            if (source.str[i] == '.' || source.str[i] == 'e' || source.str[i] == 'E') {
                literal->has_decimal = true;
                break;
            }
        }
        // Create null-terminated string, stripping numeric separators (_)
        char* temp_str = (char*)mem_alloc(source.length + 1, MEM_CAT_JS_RUNTIME);
        if (temp_str) {
            size_t j = 0;
            for (size_t i = 0; i < source.length; i++) {
                if (source.str[i] != '_') temp_str[j++] = source.str[i];
            }
            // Strip trailing 'n' for BigInt literals
            if (literal->is_bigint && j > 0) j--;
            temp_str[j] = '\0';
            // For BigInt literals, store as string to preserve arbitrary precision
            if (literal->is_bigint) {
                // allocate a String on the AST pool (heap_create_name may not be available yet)
                String* s = (String*)pool_alloc(tp->pool, sizeof(String) + j + 1);
                s->len = j;
                s->flags = 0;
                memcpy(s->chars, temp_str, j);
                s->chars[j] = '\0';
                literal->bigint_str = s;
            }
            {
                char* endptr;
                // strtod handles decimal and 0x hex, but not 0b binary, 0o
                // octal, or Annex B legacy octal integer literals.
                if (j > 2 && temp_str[0] == '0' && (temp_str[1] == 'b' || temp_str[1] == 'B')) {
                    literal->value.number_value = (double)strtoull(temp_str + 2, &endptr, 2);
                } else if (j > 2 && temp_str[0] == '0' && (temp_str[1] == 'o' || temp_str[1] == 'O')) {
                    literal->value.number_value = (double)strtoull(temp_str + 2, &endptr, 8);
                } else if (j > 1 && temp_str[0] == '0' && !literal->has_decimal) {
                    bool is_legacy_octal = true;
                    for (size_t k = 1; k < j; k++) {
                        if (temp_str[k] < '0' || temp_str[k] > '7') {
                            is_legacy_octal = false;
                            break;
                        }
                    }
                    literal->value.number_value = is_legacy_octal ?
                        (double)strtoull(temp_str, &endptr, 8) : strtod(temp_str, &endptr);
                } else {
                    literal->value.number_value = strtod(temp_str, &endptr);
                }
            }
            mem_free(temp_str);
        } else {
            literal->value.number_value = 0.0;
        }
        // BigInt is a decimal-backed JS primitive; marking it FLOAT let native
        // number inference reinterpret its preserved integer spelling.
        literal->type = literal->is_bigint ? &TYPE_DECIMAL : &TYPE_FLOAT;
    } else if (strcmp(node_type, "string") == 0) {
        literal->literal_type = JS_LITERAL_STRING;
        // Remove quotes and handle escape sequences
        if (source.length >= 2) {
            size_t content_len = source.length - 2;
            const char* src = source.str + 1;
            // Tune6 §2.4: fast path — no escapes means the literal equals its source
            // slice, so intern directly without the temp-buffer alloc/copy/free.
            if (memchr(src, '\\', content_len) == NULL) {
                literal->value.string_value = name_pool_create_len(tp->name_pool, src, content_len);
                literal->type = &TYPE_STRING;
                return (JsAstNode*)literal;
            }
            // Process escape sequences in-place
            char* temp_str = (char*)mem_alloc(content_len + 1, MEM_CAT_JS_RUNTIME);
            if (temp_str) {
                size_t out = 0;
                for (size_t i = 0; i < content_len; i++) {
                    if (src[i] == '\\' && i + 1 < content_len) {
                        char next = src[i + 1];
                        if (next == 'u') {
                            // Unicode escape: \uXXXX or \u{XXXXX}
                            if (i + 2 < content_len && src[i + 2] == '{') {
                                // \u{XXXXX} — braced Unicode code point
                                size_t hex_start = i + 3;
                                size_t hex_end = hex_start;
                                while (hex_end < content_len && src[hex_end] != '}') hex_end++;
                                size_t hex_len = hex_end - hex_start;
                                if (hex_len > 0 && hex_len <= 6) {
                                    char hex[7] = {0};
                                    memcpy(hex, src + hex_start, hex_len);
                                    uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
                                    out += wtf8_encode(cp, temp_str + out);
                                    i = hex_end; // skip past closing }
                                } else {
                                    temp_str[out++] = src[i]; // keep as-is
                                }
                            } else if (i + 5 < content_len) {
                                // \uXXXX — handles surrogate pairs
                                size_t ui = i + 1; // points at 'u'
                                out += js_decode_unicode_escape(src, content_len, &ui, temp_str + out);
                                i = ui; // skip past consumed chars
                            } else {
                                temp_str[out++] = src[i]; // keep as-is
                            }
                        } else if (next == 'x') {
                            // Hex escape: \xHH → encode as UTF-8
                            if (i + 3 < content_len) {
                                char hex[3] = {src[i+2], src[i+3], 0};
                                uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
                                out += utf8_encode(cp, temp_str + out);
                                i += 3;
                            } else {
                                temp_str[out++] = src[i];
                            }
                        } else {
                            // Line continuation: \<newline> removes both the backslash and line break
                            if (next == '\n') {
                                i++; // skip over \<LF>
                            } else if (next == '\r') {
                                i++; // skip over \<CR>
                                if (i + 1 < content_len && src[i + 1] == '\n') {
                                    i++; // also skip <LF> in \<CR><LF>
                                }
                            } else if ((unsigned char)next == 0xE2 && i + 3 < content_len &&
                                       (unsigned char)src[i + 2] == 0x80 &&
                                       ((unsigned char)src[i + 3] == 0xA8 ||
                                        (unsigned char)src[i + 3] == 0xA9)) {
                                i += 3; // skip over \<LS> or \<PS>
                            } else if (js_is_octal_digit(next)) {
                                uint32_t code = 0;
                                size_t end_pos = js_decode_legacy_octal_escape(src, content_len, i, &code);
                                if (code <= 0x7F) {
                                    temp_str[out++] = (char)code;
                                } else if (out + 2 < content_len + 1) {
                                    out += utf8_encode(code, temp_str + out);
                                }
                                i = end_pos;
                            } else {
                                temp_str[out++] = js_decode_escape_char(next);
                                i++; // skip the escaped char
                            }
                        }
                    } else {
                        temp_str[out++] = src[i];
                    }
                }
                temp_str[out] = '\0';
                literal->value.string_value = name_pool_create_len(tp->name_pool, temp_str, out);
                mem_free(temp_str);
            } else {
                literal->value.string_value = name_pool_create_len(tp->name_pool, "", 0);
            }
        } else {
            literal->value.string_value = name_pool_create_len(tp->name_pool, "", 0);
        }
        literal->type = &TYPE_STRING;
    } else if (strcmp(node_type, "true") == 0) {
        literal->literal_type = JS_LITERAL_BOOLEAN;
        literal->value.boolean_value = true;
        literal->type = &TYPE_BOOL;
    } else if (strcmp(node_type, "false") == 0) {
        literal->literal_type = JS_LITERAL_BOOLEAN;
        literal->value.boolean_value = false;
        literal->type = &TYPE_BOOL;
    } else if (strcmp(node_type, "null") == 0) {
        literal->literal_type = JS_LITERAL_NULL;
        literal->type = &TYPE_NULL;
    }

    return (JsAstNode*)literal;
}

// build a JavaScript identifier from source text.
JsAstNode* build_js_identifier_from_source(JsTranspiler* tp, StrView source,
        SourceSpan span) {
    JsIdentifierNode* identifier = (JsIdentifierNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_IDENTIFIER, span, sizeof(JsIdentifierNode));
    if (source.length == 0) {
        log_error("Empty identifier source");
        return NULL;
    }

    identifier->name = js_decode_identifier_name(tp, source.str, (int)source.length);

    if (!identifier->name) {
        log_error("Failed to create identifier name");
        return NULL;
    }
    // The direct scope pass attaches bindings after every AST child exists.
    identifier->entry = NULL;
    identifier->type = js_set_type_any(tp, ANY_OPEN_PARAM);

    return (JsAstNode*)identifier;
}

JsAstNode* build_js_new_target_from_span(JsTranspiler* tp, SourceSpan span) {
    JsIdentifierNode* identifier = (JsIdentifierNode*)alloc_js_ast_node_span(
        tp, JS_AST_NODE_IDENTIFIER, span, sizeof(JsIdentifierNode));
    identifier->name = name_pool_create_len(tp->name_pool, "new.target", 10);
    identifier->entry = NULL;
    identifier->type = js_set_type_any(tp, ANY_STATEMENT);
    return (JsAstNode*)identifier;
}

void refresh_js_binary_type(JsTranspiler* tp, JsBinaryNode* binary) {
    if (!binary) return;
    // TIG13: publish the type the operator actually produces. Every binary
    // expression used to be typed `float` — including `===`, `&&` and string
    // `+` — which made the JS lane's static types unusable for anything.
    //
    // JS policy notes [TI2]: Number is binary64 so arithmetic is `float`, not
    // `int`; bitwise operators run ToInt32/ToUint32 so their RESULT is an
    // integral value but it is still a JS number, and the emitter carries it
    // in the same lane as any other number — typing it `int` would claim a
    // carrier the lowering does not produce (the IP5 lesson), so it stays
    // `float`. `+` is overloaded on strings and `&&`/`||`/`??` yield one of
    // their operands, so those need their operands' types rather than a
    // fixed answer.
    switch (binary->op) {
    case JS_OP_EQ: case JS_OP_NE:
    case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
    case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
    case JS_OP_INSTANCEOF: case JS_OP_IN:
        // Relational, equality and membership tests are total predicates.
        binary->type = &TYPE_BOOL;
        break;
    case JS_OP_ADD: {
        // `+` is string concatenation when either side is a string, numeric
        // addition otherwise. Only a proven pair answers; anything open stays
        // open rather than guessing one of the two behaviors.
        Type* lt = binary->left ? binary->left->type : NULL;
        Type* rt = binary->right ? binary->right->type : NULL;
        TypeId l = lt ? lt->type_id : LMD_TYPE_ANY;
        TypeId r = rt ? rt->type_id : LMD_TYPE_ANY;
        if (l == LMD_TYPE_STRING || r == LMD_TYPE_STRING) {
            binary->type = &TYPE_STRING;
        } else if (l == LMD_TYPE_FLOAT && r == LMD_TYPE_FLOAT) {
            binary->type = &TYPE_FLOAT;
        } else {
            binary->type = js_set_type_any(tp, ANY_JS_BINARY);
        }
        break;
    }
    case JS_OP_AND: case JS_OP_OR: case JS_OP_NULLISH_COALESCE:
        // These yield one OPERAND, never a coerced number. Both constituents
        // reach the result, so the answer is their union when both are known.
        if (binary->left && binary->right && binary->left->type &&
                binary->right->type &&
                binary->left->type->type_id == binary->right->type->type_id) {
            binary->type = binary->left->type;
        } else {
            binary->type = js_set_type_any(tp, ANY_JS_BINARY);
        }
        break;
    default:
        // Arithmetic, exponentiation and the bitwise/shift family all produce
        // a JS number.
        binary->type = &TYPE_FLOAT;
        break;
    }
}

void refresh_js_assignment_type(JsAssignmentNode* assignment) {
    if (!assignment) return;
    assignment->type = assignment->right ? assignment->right->type : &TYPE_ANY;
}

void refresh_js_conditional_type(JsTranspiler* tp,
        JsConditionalNode* conditional) {
    if (!conditional) return;
    if (conditional->consequent && conditional->alternate &&
            conditional->consequent->type && conditional->alternate->type &&
            conditional->consequent->type->type_id ==
                conditional->alternate->type->type_id) {
        conditional->type = conditional->consequent->type;
    } else {
        conditional->type = js_set_type_any(tp, ANY_JOIN);
    }
}

// build a binary expression from parser-owned children and operator facts
JsAstNode* build_js_binary_from_children(JsTranspiler* tp, SourceSpan span,
        JsOperator op, JsAstNode* left, JsAstNode* right) {
    JsBinaryNode* binary = (JsBinaryNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_BINARY_EXPRESSION, span, sizeof(JsBinaryNode));
    binary->left = left;
    binary->right = right;
    binary->op = op;
    refresh_js_binary_type(tp, binary);

    return (JsAstNode*)binary;
}

// build a JavaScript unary expression from its operand.
JsAstNode* build_js_unary_from_child(JsTranspiler* tp, SourceSpan span,
        JsOperator op, JsAstNode* operand, bool prefix) {
    JsUnaryNode* unary = (JsUnaryNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_UNARY_EXPRESSION, span, sizeof(JsUnaryNode));
    unary->operand = operand;
    unary->op = op;
    unary->prefix = prefix;

    // Infer result type
    switch (unary->op) {
        case JS_OP_NOT:
            unary->type = &TYPE_BOOL;
            break;
        case JS_OP_TYPEOF:
            unary->type = &TYPE_STRING;
            break;
        case JS_OP_PLUS:
        case JS_OP_MINUS:
        case JS_OP_BIT_NOT:
            unary->type = &TYPE_FLOAT;
            break;
        case JS_OP_INCREMENT:
        case JS_OP_DECREMENT:
            // update expressions produce a JavaScript Number even when the
            // referenced property has an open static type.
            unary->type = &TYPE_FLOAT;
            break;
        case JS_OP_DELETE:
            unary->type = &TYPE_BOOL;
            break;
        case JS_OP_VOID:
            unary->type = &TYPE_NULL; // void always returns undefined
            break;
        default:
            unary->type = js_set_type_any(tp, ANY_JS_BINARY);
    }

    return (JsAstNode*)unary;
}

// build a call from parser-owned callee and argument-list children
JsAstNode* build_js_call_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* callee, JsAstNode* arguments, bool optional) {
    if (!callee) {
        log_error("JavaScript call has no callee");
        return NULL;
    }
    JsCallNode* call = (JsCallNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_CALL_EXPRESSION, span, sizeof(JsCallNode));
    call->callee = callee;
    call->arguments = arguments;
    call->optional = optional;
    call->type = js_set_type_any(tp, ANY_JS_CALL);
    return (JsAstNode*)call;
}

// build a constructor call from parser-owned callee and argument children
JsAstNode* build_js_new_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* callee, JsAstNode* arguments) {
    if (!callee) {
        log_error("JavaScript new expression has no constructor");
        return NULL;
    }
    JsCallNode* call = (JsCallNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_NEW_EXPRESSION, span, sizeof(JsCallNode));
    call->callee = callee;
    call->arguments = arguments;
    call->type = js_set_type_any(tp, ANY_JS_CALL);
    return (JsAstNode*)call;
}

// build a regex literal from its complete source token
JsAstNode* build_js_regex_from_source(JsTranspiler* tp, StrView source,
        SourceSpan span) {
    if (source.length < 2 || source.str[0] != '/') {
        log_error("JavaScript regex has invalid source");
        return NULL;
    }
    size_t slash = source.length;
    while (slash > 1 && source.str[slash - 1] != '/') slash--;
    if (slash <= 1) {
        log_error("JavaScript regex has no closing slash");
        return NULL;
    }
    JsRegexNode* regex = (JsRegexNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_REGEX, span, sizeof(JsRegexNode));
    String* pattern = name_pool_create_len(tp->name_pool, source.str + 1,
        (int)(slash - 2));
    String* flags = name_pool_create_len(tp->name_pool,
        source.str + slash, (int)(source.length - slash));
    regex->pattern = pattern ? pattern->chars : NULL;
    regex->pattern_len = pattern ? (int)pattern->len : 0;
    regex->flags = flags ? flags->chars : NULL;
    regex->flags_len = flags ? (int)flags->len : 0;
    regex->type = js_set_type_any(tp, ANY_OPEN_PARAM);
    return (JsAstNode*)regex;
}

static int js_template_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static String* js_template_cooked_string(JsTranspiler* tp,
        const char* source, size_t length) {
    if (!tp || !source || js_template_has_invalid_escape(source, length)) {
        return NULL;
    }
    char* cooked = (char*)pool_alloc(tp->pool, length + 1);
    if (!cooked) return NULL;
    size_t out = 0;
    for (size_t i = 0; i < length;) {
        if (source[i] != '\\' || i + 1 >= length) {
            cooked[out++] = source[i++];
            continue;
        }
        char escaped = source[i + 1];
        if (escaped == '\n') {
            i += 2;
            continue;
        }
        if (escaped == '\r') {
            i += 2;
            if (i < length && source[i] == '\n') i++;
            continue;
        }
        if ((unsigned char)escaped == 0xE2 && i + 3 < length &&
                (unsigned char)source[i + 2] == 0x80 &&
                ((unsigned char)source[i + 3] == 0xA8 ||
                 (unsigned char)source[i + 3] == 0xA9)) {
            // U+2028/U+2029 line continuations contribute no cooked code unit.
            i += 4;
            continue;
        }
        if (escaped == 'x' && i + 3 < length &&
                js_template_hex_char(source[i + 2]) &&
                js_template_hex_char(source[i + 3])) {
            int value = (js_template_hex_value(source[i + 2]) << 4) |
                js_template_hex_value(source[i + 3]);
            out += utf8_encode((uint32_t)value, cooked + out);
            i += 4;
            continue;
        }
        if (escaped == 'u' && i + 2 < length && source[i + 2] == '{') {
            size_t end = i + 3;
            uint32_t value = 0;
            while (end < length && source[end] != '}') {
                value = (value << 4) |
                    (uint32_t)js_template_hex_value(source[end]);
                end++;
            }
            out += wtf8_encode(value, cooked + out);
            i = end < length ? end + 1 : length;
            continue;
        }
        if (escaped == 'u' && i + 5 < length &&
                js_template_hex_char(source[i + 2]) &&
                js_template_hex_char(source[i + 3]) &&
                js_template_hex_char(source[i + 4]) &&
                js_template_hex_char(source[i + 5])) {
            uint32_t value = (uint32_t)js_template_hex_value(source[i + 2]);
            value = (value << 4) | (uint32_t)js_template_hex_value(source[i + 3]);
            value = (value << 4) | (uint32_t)js_template_hex_value(source[i + 4]);
            value = (value << 4) | (uint32_t)js_template_hex_value(source[i + 5]);
            out += wtf8_encode(value, cooked + out);
            i += 6;
            continue;
        }
        cooked[out++] = js_decode_escape_char(escaped);
        i += 2;
    }
    return name_pool_create_len(tp->name_pool, cooked, (int)out);
}

// build one parser-owned template quasi, preserving raw text and cooked value.
JsAstNode* build_js_template_element_from_source(JsTranspiler* tp,
        StrView source, SourceSpan span, bool tail) {
    if (!tp || !source.str || !source.length) return NULL;
    size_t begin = source.str[0] == '`' ? 1 : 0;
    size_t end = source.length;
    if (tail) {
        if (!end || source.str[end - 1] != '`') return NULL;
        // A substitution immediately followed by the closing delimiter has
        // a one-byte tail token containing only that delimiter.
        end = source.length == 1 ? 1 : end - 1;
    } else {
        if (end < 2 || source.str[end - 2] != '$' ||
                source.str[end - 1] != '{') return NULL;
        end -= 2;
    }
    if (end < begin) return NULL;
    size_t length = end - begin;
    JsTemplateElementNode* element =
        (JsTemplateElementNode*)alloc_js_ast_node_span(tp,
            JS_AST_NODE_TEMPLATE_ELEMENT, span,
            sizeof(JsTemplateElementNode));
    if (!element) return NULL;
    element->raw = name_pool_create_len(tp->name_pool, source.str + begin,
        (int)length);
    element->cooked = js_template_cooked_string(tp, source.str + begin,
        length);
    element->tail = tail;
    element->type = &TYPE_STRING;
    return (JsAstNode*)element;
}

JsAstNode* build_js_template_from_parts(JsTranspiler* tp, SourceSpan span,
        JsAstNode* parts, uint32_t length) {
    if (!tp || !parts || !length) return NULL;
    JsTemplateLiteralNode* literal =
        (JsTemplateLiteralNode*)alloc_js_ast_node_span(tp,
            JS_AST_NODE_TEMPLATE_LITERAL, span,
            sizeof(JsTemplateLiteralNode));
    if (!literal) return NULL;
    JsAstNode* quasis = NULL;
    JsAstNode* expressions = NULL;
    JsAstNode* previous_quasi = NULL;
    JsAstNode* previous_expression = NULL;
    for (JsAstNode* part = parts; part; ) {
        JsAstNode* next = part->next;
        part->next = NULL;
        if (part->node_type == JS_AST_NODE_TEMPLATE_ELEMENT) {
            if (!quasis) quasis = part;
            else previous_quasi->next = part;
            previous_quasi = part;
        } else {
            if (!expressions) expressions = part;
            else previous_expression->next = part;
            previous_expression = part;
        }
        part = next;
    }
    literal->quasis = quasis;
    literal->expressions = expressions;
    literal->type = &TYPE_STRING;
    return (JsAstNode*)literal;
}

JsAstNode* build_js_tagged_template_from_children(JsTranspiler* tp,
        SourceSpan span, JsAstNode* tag, JsAstNode* quasi) {
    if (!tp || !tag || !quasi || quasi->node_type != JS_AST_NODE_TEMPLATE_LITERAL) {
        log_error("JavaScript tagged template has invalid children");
        return NULL;
    }
    JsTaggedTemplateNode* tagged =
        (JsTaggedTemplateNode*)alloc_js_ast_node_span(tp,
            JS_AST_NODE_TAGGED_TEMPLATE, span,
            sizeof(JsTaggedTemplateNode));
    if (!tagged) return NULL;
    tagged->tag = tag;
    tagged->quasi = (JsTemplateLiteralNode*)quasi;
    tagged->type = js_set_type_any(tp, ANY_JS_CALL);
    return (JsAstNode*)tagged;
}

// build a no-substitution template literal from its complete token.
JsAstNode* build_js_template_from_source(JsTranspiler* tp, StrView source,
        SourceSpan span) {
    if (source.length < 2 || source.str[0] != '`' ||
            source.str[source.length - 1] != '`') {
        log_error("JavaScript template has invalid source");
        return NULL;
    }
    for (size_t i = 1; i + 1 < source.length; i++) {
        if (source.str[i] == '$' && source.str[i + 1] == '{') {
            log_error("JavaScript template substitution is not reduced");
            return NULL;
        }
    }
    JsAstNode* element = build_js_template_element_from_source(tp, source,
        span, true);
    return element ? build_js_template_from_parts(tp, span, element, 1) : NULL;
}

// build await/yield nodes from their parser-owned operand
JsAstNode* build_js_await_from_child(JsTranspiler* tp, SourceSpan span,
        JsAstNode* argument) {
    JsAwaitNode* await_node = (JsAwaitNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_AWAIT_EXPRESSION, span, sizeof(JsAwaitNode));
    await_node->argument = argument;
    await_node->type = js_set_type_any(tp, ANY_STATEMENT);
    return (JsAstNode*)await_node;
}

JsAstNode* build_js_yield_from_child(JsTranspiler* tp, SourceSpan span,
        JsAstNode* argument, bool delegate) {
    JsYieldNode* yield_node = (JsYieldNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_YIELD_EXPRESSION, span, sizeof(JsYieldNode));
    yield_node->argument = argument;
    yield_node->delegate = delegate;
    yield_node->type = js_set_type_any(tp, ANY_STATEMENT);
    return (JsAstNode*)yield_node;
}

static Type* resolve_js_member_type(JsMemberNode* member) {
    if (!member || member->computed || !member->object ||
            !member->object->type || !member->property ||
            member->property->node_type != JS_AST_NODE_IDENTIFIER) {
        return NULL;
    }
    Type* recv = member->object->type;
    if (recv->type_id != LMD_TYPE_MAP && recv->type_id != LMD_TYPE_OBJECT) {
        return NULL;
    }
    if (is_global_simple_type(recv)) return NULL;
    TypeMap* recv_map = (TypeMap*)recv;
    JsIdentifierNode* prop = (JsIdentifierNode*)member->property;
    if (!recv_map->shape || !prop->name) return NULL;
    FOR_EACH_MAP_FIELD(recv_map, se) {
        if (se->name && (int)se->name->length == (int)prop->name->len &&
                strncmp(se->name->str, prop->name->chars,
                    se->name->length) == 0) {
            Type* field_type = unwrap_simple_type_type(se->type);
            if (field_type && (field_type->type_id == LMD_TYPE_MAP ||
                    field_type->type_id == LMD_TYPE_ELEMENT ||
                    field_type->type_id == LMD_TYPE_OBJECT)) {
                return field_type;
            }
            return NULL;
        }
    }
    return NULL;
}

// build a member from parser-owned object/property children
JsAstNode* build_js_member_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* object, JsAstNode* property, bool computed, bool optional) {
    if (!object || !property) {
        log_error("JavaScript member has no object or property");
        return NULL;
    }
    JsMemberNode* member = (JsMemberNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_MEMBER_EXPRESSION, span, sizeof(JsMemberNode));
    member->object = object;
    member->property = property;
    member->computed = computed;
    member->optional = optional;
    Type* resolved = resolve_js_member_type(member);
    member->type = resolved ? resolved : js_set_type_any(tp, ANY_JS_MEMBER);
    return (JsAstNode*)member;
}

static JsAstNode* build_js_array_like_from_list(JsTranspiler* tp,
        JsAstNodeType node_type, SourceSpan span, JsAstNode* elements,
        uint32_t length, Type* type) {
    JsArrayNode* array = (JsArrayNode*)alloc_js_ast_node_span(tp, node_type,
        span, sizeof(JsArrayNode));
    array->elements = elements;
    array->length = (int)length;
    array->type = type;
    return (JsAstNode*)array;
}

// build an array from parser-owned element children linked in source order
JsAstNode* build_js_array_from_list(JsTranspiler* tp, SourceSpan span,
        JsAstNode* elements, uint32_t length) {
    return build_js_array_like_from_list(tp, JS_AST_NODE_ARRAY_EXPRESSION,
        span, elements, length, &TYPE_ARRAY);
}

// build a comma sequence from parser-owned expression children
JsAstNode* build_js_sequence_from_list(JsTranspiler* tp, SourceSpan span,
        JsAstNode* expressions, uint32_t length) {
    return build_js_array_like_from_list(tp, JS_AST_NODE_SEQUENCE_EXPRESSION,
        span, expressions, length, js_set_type_any(tp, ANY_JS_BINARY));
}

// build an assignment from parser-owned left/right children
JsAstNode* build_js_assignment_from_children(JsTranspiler* tp, SourceSpan span,
        JsOperator op, JsAstNode* left, JsAstNode* right) {
    if (!left || !right) {
        log_error("JavaScript assignment is missing an operand");
        return NULL;
    }
    JsAssignmentNode* assignment = (JsAssignmentNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_ASSIGNMENT_EXPRESSION, span, sizeof(JsAssignmentNode));
    assignment->op = op;
    assignment->left = left;
    assignment->right = right;
    if (left->node_type == JS_AST_NODE_IDENTIFIER && tp && tp->source &&
            span.start_byte < left->source_span.start_byte &&
            left->source_span.start_byte <= tp->source_length) {
        // Parentheses are omitted from the direct AST expression node, but
        // IsIdentifierRef must remain false for `(name) = function() {}`.
        size_t pos = span.start_byte;
        size_t end = left->source_span.start_byte;
        while (pos < end && (tp->source[pos] == ' ' ||
                tp->source[pos] == '\t' || tp->source[pos] == '\r' ||
                tp->source[pos] == '\n')) pos++;
        assignment->lhs_is_parenthesized = pos < end &&
            tp->source[pos] == '(';
    }
    refresh_js_assignment_type(assignment);
    return (JsAstNode*)assignment;
}

// build a conditional from parser-owned test and branch children
JsAstNode* build_js_conditional_from_children(JsTranspiler* tp,
        SourceSpan span, JsAstNode* test, JsAstNode* consequent,
        JsAstNode* alternate) {
    if (!test || !consequent || !alternate) {
        log_error("JavaScript conditional is missing a branch");
        return NULL;
    }
    JsConditionalNode* conditional = (JsConditionalNode*)alloc_js_ast_node_span(
        tp, JS_AST_NODE_CONDITIONAL_EXPRESSION, span,
        sizeof(JsConditionalNode));
    conditional->test = test;
    conditional->consequent = consequent;
    conditional->alternate = alternate;
    refresh_js_conditional_type(tp, conditional);
    return (JsAstNode*)conditional;
}

// build an object property from parser-owned key/value children
JsAstNode* build_js_property_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* key, JsAstNode* value, bool computed, bool shorthand) {
    if (!key) {
        log_error("JavaScript object property has no key");
        return NULL;
    }
    JsPropertyNode* property = (JsPropertyNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_PROPERTY, span, sizeof(JsPropertyNode));
    property->key = key;
    property->value = value ? value : key;
    property->computed = computed;
    property->shorthand = shorthand;
    property->type = js_set_type_any(tp, ANY_OPEN_MAP);
    return (JsAstNode*)property;
}

// build a spread element from its parser-owned argument
JsAstNode* build_js_spread_from_child(JsTranspiler* tp, SourceSpan span,
        JsAstNode* argument) {
    if (!argument) {
        log_error("JavaScript spread has no argument");
        return NULL;
    }
    JsSpreadElementNode* spread = (JsSpreadElementNode*)alloc_js_ast_node_span(
        tp, JS_AST_NODE_SPREAD_ELEMENT, span, sizeof(JsSpreadElementNode));
    spread->argument = argument;
    spread->type = &TYPE_ARRAY;
    return (JsAstNode*)spread;
}

void mark_js_object_spread(JsTranspiler* tp, JsAstNode* spread) {
    if (!spread || spread->node_type != JS_AST_NODE_SPREAD_ELEMENT) return;
    spread->type = js_set_type_any(tp, ANY_STATEMENT);
}

// build an explicit array elision so its slot survives AST lowering
JsAstNode* build_js_array_hole(JsTranspiler* tp, SourceSpan span) {
    JsAstNode* hole = alloc_js_ast_node_span(tp, JS_AST_NODE_NULL, span,
        sizeof(JsAstNode));
    if (hole) hole->type = js_set_type_any(tp, ANY_STATEMENT);
    return hole;
}

JsAstNode* build_js_pattern_array_from_list(JsTranspiler* tp, SourceSpan span,
        JsAstNode* elements, uint32_t length) {
    return build_js_array_like_from_list(tp, JS_AST_NODE_ARRAY_PATTERN,
        span, elements, length, &TYPE_ARRAY);
}

JsAstNode* build_js_pattern_object_from_list(JsTranspiler* tp, SourceSpan span,
        JsAstNode* properties, uint32_t length) {
    (void)length;
    JsObjectPatternNode* object = (JsObjectPatternNode*)alloc_js_ast_node_span(
        tp, JS_AST_NODE_OBJECT_PATTERN, span, sizeof(JsObjectPatternNode));
    object->properties = properties;
    object->type = js_set_type_any(tp, ANY_DECOMPOSE);
    return (JsAstNode*)object;
}

JsAstNode* build_js_assignment_pattern_from_children(JsTranspiler* tp,
        SourceSpan span, JsAstNode* left, JsAstNode* right) {
    if (!left || !right) {
        log_error("JavaScript assignment pattern is missing a child");
        return NULL;
    }
    JsAssignmentPatternNode* assignment =
        (JsAssignmentPatternNode*)alloc_js_ast_node_span(tp,
            JS_AST_NODE_ASSIGNMENT_PATTERN, span,
            sizeof(JsAssignmentPatternNode));
    assignment->left = left;
    assignment->right = right;
    assignment->op = (JsOperator)0;
    assignment->type = js_set_type_any(tp, ANY_DECOMPOSE);
    return (JsAstNode*)assignment;
}

JsAstNode* build_js_rest_pattern_from_child(JsTranspiler* tp, SourceSpan span,
        JsAstNode* argument, bool property) {
    if (!argument) {
        log_error("JavaScript rest pattern has no argument");
        return NULL;
    }
    JsSpreadElementNode* rest = (JsSpreadElementNode*)alloc_js_ast_node_span(
                tp, property ? JS_AST_NODE_REST_PROPERTY : JS_AST_NODE_REST_ELEMENT,
        span, sizeof(JsSpreadElementNode));
    rest->argument = argument;
    rest->type = property ? js_set_type_any(tp, ANY_DECOMPOSE) : &TYPE_ARRAY;
    return (JsAstNode*)rest;
}

JsAstNode* build_js_pattern_property_from_children(JsTranspiler* tp,
        SourceSpan span, JsAstNode* key, JsAstNode* value, bool computed,
        bool shorthand) {
    if (!key || !value) {
        log_error("JavaScript pattern property is missing a child");
        return NULL;
    }
    JsPropertyNode* property = (JsPropertyNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_PROPERTY, span, sizeof(JsPropertyNode));
    property->key = key;
    property->value = value;
    property->computed = computed;
    property->method = false;
    property->is_getter = false;
    property->is_setter = false;
    // Pattern properties are explicit key/value pairs in the reference AST.
    property->shorthand = false;
    property->type = js_set_type_any(tp, ANY_DECOMPOSE);
    return (JsAstNode*)property;
}

JsAstNode* build_js_pattern_hole(JsTranspiler* tp, SourceSpan span) {
    JsAstNode* hole = alloc_js_ast_node_span(tp, JS_AST_NODE_NULL, span,
        sizeof(JsAstNode));
    if (hole) hole->type = js_set_type_any(tp, ANY_DECOMPOSE);
    return hole;
}

// build an object from parser-owned property children linked in source order
JsAstNode* build_js_object_from_list(JsTranspiler* tp, SourceSpan span,
        JsAstNode* properties, uint32_t length) {
    (void)length;
    JsObjectNode* object = (JsObjectNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_OBJECT_EXPRESSION, span, sizeof(JsObjectNode));
    object->properties = properties;
    object->type = &TYPE_MAP;
    return (JsAstNode*)object;
}

// build a variable declarator from parser-owned binding and initializer nodes
JsAstNode* build_js_declarator_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* id, JsAstNode* init) {
    if (!id) {
        log_error("JavaScript declarator has no binding pattern");
        return NULL;
    }
    JsVariableDeclaratorNode* declarator =
        (JsVariableDeclaratorNode*)alloc_js_ast_node_span(tp,
            JS_AST_NODE_VARIABLE_DECLARATOR, span,
            sizeof(JsVariableDeclaratorNode));
    declarator->id = id;
    declarator->init = init;
    // An initializer with an unset derived type must remain unset; the
    // reference builder distinguishes it from a declaration without init.
    declarator->type = init ? init->type : &TYPE_NULL;
    return (JsAstNode*)declarator;
}

JsAstNode* build_js_declarator_with_type_from_children(JsTranspiler* tp,
        SourceSpan span, JsAstNode* id, Type* declared_type,
        JsAstNode* init) {
    if (!declared_type) return build_js_declarator_from_children(tp, span, id, init);
    JsVariableDeclaratorNode* declarator =
        (JsVariableDeclaratorNode*)build_js_declarator_from_children(tp, span,
            id, init);
    if (!declarator) return NULL;
    declarator->declared_type = declared_type;
    if (!init) declarator->type = declared_type;
    return (JsAstNode*)declarator;
}

// build a variable declaration from parser-owned declarators
JsAstNode* build_js_variable_declaration_from_list(JsTranspiler* tp,
        SourceSpan span, JsAstNode* declarations, uint32_t length, int kind) {
    (void)length;
    JsVariableDeclarationNode* declaration =
        (JsVariableDeclarationNode*)alloc_js_ast_node_span(tp,
            JS_AST_NODE_VARIABLE_DECLARATION, span,
            sizeof(JsVariableDeclarationNode));
    declaration->declarations = declarations;
    declaration->kind = kind;
    declaration->type = &TYPE_NULL;
    return (JsAstNode*)declaration;
}

// build a block from parser-owned statement children
JsAstNode* build_js_block_from_list(JsTranspiler* tp, SourceSpan span,
        JsAstNode* statements, uint32_t length) {
    (void)length;
    JsBlockNode* block = (JsBlockNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_BLOCK_STATEMENT, span, sizeof(JsBlockNode));
    block->statements = statements;
    block->type = &TYPE_NULL;
    return (JsAstNode*)block;
}

// Preserve Tree-sitter's statement-start object disambiguation. A leading
// object with plain identifier pairs is retained as a block of labeled
// expression statements; richer object syntax remains an object expression.
JsAstNode* build_js_statement_block_from_object(JsTranspiler* tp,
        SourceSpan span, JsAstNode* object) {
    if (!tp || !object || object->node_type != JS_AST_NODE_OBJECT_EXPRESSION) {
        return NULL;
    }
    JsObjectNode* object_node = (JsObjectNode*)object;
    JsAstNode* statements = NULL;
    JsAstNode* previous = NULL;
    for (JsAstNode* item = object_node->properties; item;
            item = (JsAstNode*)item->next) {
        if (item->node_type != JS_AST_NODE_PROPERTY) return NULL;
        JsPropertyNode* property = (JsPropertyNode*)item;
        if (!property->key || property->key->node_type != JS_AST_NODE_IDENTIFIER ||
                !property->value) return NULL;
        JsIdentifierNode* key = (JsIdentifierNode*)property->key;
        if (!key->name) return NULL;
        StrView label = {key->name->chars, key->name->len};
        JsAstNode* expression = build_js_expression_statement_from_child(tp,
            property->value->source_span, property->value);
        JsAstNode* labeled = build_js_labeled_from_child(tp,
            property->source_span, label, expression);
        if (!labeled) return NULL;
        if (!previous) statements = labeled;
        else previous->next = labeled;
        previous = labeled;
    }
    return build_js_block_from_list(tp, span, statements, 0);
}

// build an if statement from parser-owned condition and branch nodes
JsAstNode* build_js_if_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* test, JsAstNode* consequent, JsAstNode* alternate) {
    if (!test) {
        log_error("JavaScript if statement is missing its condition");
        return NULL;
    }
    JsIfNode* conditional = (JsIfNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_IF_STATEMENT, span, sizeof(JsIfNode));
    conditional->test = test;
    conditional->consequent = consequent;
    conditional->alternate = alternate;
    conditional->type = &TYPE_NULL;
    return (JsAstNode*)conditional;
}

// build a while statement from parser-owned condition/body nodes
JsAstNode* build_js_while_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* test, JsAstNode* body) {
    if (!test) {
        log_error("JavaScript while statement is missing its condition");
        return NULL;
    }
    JsWhileNode* loop = (JsWhileNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_WHILE_STATEMENT, span, sizeof(JsWhileNode));
    loop->form = LOOP_FORM_WHILE;
    loop->test = test;
    loop->body = body;
    // while statements do not own a header scope; only their body can create
    // a lexical environment, matching the reference AST builder.
    loop->vars = NULL;
    loop->type = &TYPE_NULL;
    return (JsAstNode*)loop;
}

// build a do/while statement from parser-owned body/test nodes
JsAstNode* build_js_do_while_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* body, JsAstNode* test) {
    if (!test) {
        log_error("JavaScript do/while statement is missing its condition");
        return NULL;
    }
    JsDoWhileNode* loop = (JsDoWhileNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_DO_WHILE_STATEMENT, span, sizeof(JsDoWhileNode));
    loop->form = LOOP_FORM_DO_WHILE;
    loop->test = test;
    loop->body = body;
    loop->vars = NULL;
    loop->type = &TYPE_NULL;
    return (JsAstNode*)loop;
}

// build return/throw control nodes from an optional parser-owned child
JsAstNode* build_js_return_from_child(JsTranspiler* tp, SourceSpan span,
        JsAstNode* argument) {
    JsReturnNode* result = (JsReturnNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_RETURN_STATEMENT, span, sizeof(JsReturnNode));
    result->argument = argument;
    result->type = argument ? argument->type : &TYPE_NULL;
    return (JsAstNode*)result;
}

JsAstNode* build_js_throw_from_child(JsTranspiler* tp, SourceSpan span,
        JsAstNode* argument) {
    if (!argument) {
        log_error("JavaScript throw has no argument");
        return NULL;
    }
    JsThrowNode* result = (JsThrowNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_THROW_STATEMENT, span, sizeof(JsThrowNode));
    result->argument = argument;
    result->type = &TYPE_NULL;
    return (JsAstNode*)result;
}

JsAstNode* build_js_break_continue(JsTranspiler* tp, SourceSpan span,
        bool is_continue, StrView label) {
    JsBreakContinueNode* result = (JsBreakContinueNode*)alloc_js_ast_node_span(
        tp, is_continue ? JS_AST_NODE_CONTINUE_STATEMENT :
            JS_AST_NODE_BREAK_STATEMENT, span, sizeof(JsBreakContinueNode));
    String* name = label.length ? name_pool_create_len(tp->name_pool,
        label.str, (int)label.length) : NULL;
    result->label = name ? name->chars : NULL;
    result->label_len = name ? (int)name->len : 0;
    result->type = &TYPE_NULL;
    return (JsAstNode*)result;
}

// build labeled and with statements from parser-owned children
JsAstNode* build_js_labeled_from_child(JsTranspiler* tp, SourceSpan span,
        StrView label, JsAstNode* body) {
    if (!label.str || !label.length) {
        log_error("JavaScript labeled statement has no label");
        return NULL;
    }
    String* name = name_pool_create_len(tp->name_pool, label.str,
        (int)label.length);
    if (!name) return NULL;
    JsLabeledStatementNode* result =
        (JsLabeledStatementNode*)alloc_js_ast_node_span(tp,
            JS_AST_NODE_LABELED_STATEMENT, span,
            sizeof(JsLabeledStatementNode));
    result->label = name->chars;
    result->label_len = (int)name->len;
    result->body = body;
    result->type = &TYPE_NULL;
    return (JsAstNode*)result;
}

JsAstNode* build_js_with_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* object, JsAstNode* body) {
    if (!object) {
        log_error("JavaScript with statement has no object");
        return NULL;
    }
    JsWithStatementNode* result =
        (JsWithStatementNode*)alloc_js_ast_node_span(tp,
            JS_AST_NODE_WITH_STATEMENT, span, sizeof(JsWithStatementNode));
    result->object = object;
    result->body = body;
    result->type = &TYPE_NULL;
    return (JsAstNode*)result;
}

// build an expression statement from its parser-owned expression
JsAstNode* build_js_expression_statement_from_child(JsTranspiler* tp,
        SourceSpan span, JsAstNode* expression) {
    if (!expression) {
        log_error("JavaScript expression statement has no expression");
        return NULL;
    }
    JsExpressionStatementNode* statement =
        (JsExpressionStatementNode*)alloc_js_ast_node_span(tp,
            JS_AST_NODE_EXPRESSION_STATEMENT, span,
            sizeof(JsExpressionStatementNode));
    statement->expression = expression;
    statement->type = expression->type ? expression->type : &TYPE_NULL;
    return (JsAstNode*)statement;
}

// build a parameter while preserving the runtime's plain-binding shape for
// unannotated parameters and its assignment/rest nodes for richer forms.
JsAstNode* build_js_parameter_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* pattern, JsAstNode* default_value, bool optional, bool rest) {
    if (!pattern || (rest && default_value)) {
        log_error("JavaScript parameter has invalid children");
        return NULL;
    }
    JsAstNode* parameter = pattern;
    if (rest) {
        JsSpreadElementNode* rest_node = (JsSpreadElementNode*)
            alloc_js_ast_node_span(tp, JS_AST_NODE_REST_ELEMENT, span,
                sizeof(JsSpreadElementNode));
        rest_node->argument = pattern;
        rest_node->type = &TYPE_ARRAY;
        parameter = (JsAstNode*)rest_node;
    } else if (default_value) {
        JsAssignmentPatternNode* assignment = (JsAssignmentPatternNode*)
            alloc_js_ast_node_span(tp, JS_AST_NODE_ASSIGNMENT_PATTERN, span,
                sizeof(JsAssignmentPatternNode));
        assignment->left = pattern;
        assignment->right = default_value;
        assignment->op = (JsOperator)0;
        assignment->type = js_set_type_any(tp, ANY_DECOMPOSE);
        parameter = (JsAstNode*)assignment;
    }
    if (optional && parameter->node_type == JS_AST_NODE_IDENTIFIER) {
        // JS has no optional parameter marker; TS annotations are handled by
        // the TypeScript reduction lane before this constructor is called.
        parameter->type = js_set_type_any(tp, ANY_OPEN_PARAM);
    }
    return parameter;
}

JsAstNode* build_js_parameter_with_type_from_children(JsTranspiler* tp,
        SourceSpan span, JsAstNode* pattern, Type* declared_type,
        JsAstNode* default_value, bool optional, bool rest) {
    if (!declared_type) return build_js_parameter_from_children(tp, span, pattern,
        default_value, optional, rest);
    if (!pattern || (rest && default_value)) {
        log_error("JavaScript TypeScript parameter has invalid children");
        return NULL;
    }
    JsAstNode* parameter_pattern = pattern;
    if (rest) {
        JsSpreadElementNode* rest_node = (JsSpreadElementNode*)
            alloc_js_ast_node_span(tp, JS_AST_NODE_REST_ELEMENT, span,
                sizeof(JsSpreadElementNode));
        rest_node->argument = pattern;
        rest_node->type = &TYPE_ARRAY;
        parameter_pattern = (JsAstNode*)rest_node;
    }
    TsParameterNode* parameter = (TsParameterNode*)alloc_js_ast_node_span(tp,
        (JsAstNodeType)TS_AST_NODE_PARAMETER, span, sizeof(TsParameterNode));
    parameter->pattern = parameter_pattern;
    parameter->declared_type = declared_type;
    parameter->default_value = default_value;
    parameter->optional = optional;
    parameter->type = js_set_type_any(tp, ANY_OPEN_PARAM);
    return (JsAstNode*)parameter;
}

JsAstNode* build_js_type_expression_from_children(JsTranspiler* tp,
        SourceSpan span, JsAstNode* inner, JsAstNode* target_type,
        bool satisfies, bool assertion) {
    if (!inner || !target_type) {
        log_error("JavaScript TypeScript expression has invalid children");
        return NULL;
    }
    // Assertions and satisfies are erased at construction time. Keeping a
    // runtime wrapper forced the retired TS post-pass to walk every JS node.
    target_type->source_span = (SourceSpan){0, 0};
    (void)tp;
    (void)span;
    (void)satisfies;
    (void)assertion;
    return inner;
}

JsAstNode* build_js_non_null_from_child(JsTranspiler* tp, SourceSpan span,
        JsAstNode* inner) {
    if (!inner) {
        log_error("JavaScript non-null expression has no operand");
        return NULL;
    }
    // Non-null assertions are type-only and have no JavaScript runtime form.
    (void)tp;
    (void)span;
    return inner;
}

static bool js_ast_string_is_use_strict(JsAstNode* node) {
    if (!node || node->node_type != JS_AST_NODE_LITERAL) return false;
    JsLiteralNode* literal = (JsLiteralNode*)node;
    String* value = literal->literal_type == JS_LITERAL_STRING
        ? literal->value.string_value : NULL;
    return value && value->len == 10 &&
        memcmp(value->chars, "use strict", 10) == 0;
}

bool js_ast_statement_list_has_use_strict_directive(JsAstNode* statements) {
    for (JsAstNode* statement = statements; statement;
            statement = statement->next) {
        if (statement->node_type != JS_AST_NODE_EXPRESSION_STATEMENT) return false;
        JsExpressionStatementNode* expression_statement =
            (JsExpressionStatementNode*)statement;
        if (js_ast_string_is_use_strict(expression_statement->expression)) return true;
        JsAstNode* expression = expression_statement->expression;
        if (!expression || expression->node_type != JS_AST_NODE_LITERAL ||
                ((JsLiteralNode*)expression)->literal_type != JS_LITERAL_STRING) {
            return false;
        }
    }
    return false;
}

bool js_ast_body_has_use_strict_directive(JsAstNode* body) {
    if (!body || body->node_type != JS_AST_NODE_BLOCK_STATEMENT) return false;
    return js_ast_statement_list_has_use_strict_directive(
        ((JsBlockNode*)body)->statements);
}

bool js_ast_body_has_use_strict_directive_source(JsTranspiler* tp,
        JsAstNode* body) {
    if (!js_ast_body_has_use_strict_directive(body)) return false;
    if (!tp || !tp->source || !body || !((JsBlockNode*)body)->statements) {
        return false;
    }
    JsAstNode* first = ((JsBlockNode*)body)->statements;
    size_t pos = body->source_span.start_byte + 1;
    size_t end = first->source_span.start_byte;
    if (end > tp->source_length) end = tp->source_length;
    while (pos < end) {
        while (pos < end && (tp->source[pos] == ' ' ||
                tp->source[pos] == '\t' || tp->source[pos] == '\r' ||
                tp->source[pos] == '\n')) pos++;
        if (pos >= end) break;
        if (tp->source[pos] == '/' && pos + 1 < end &&
                tp->source[pos + 1] == '/') {
            pos += 2;
            while (pos < end && tp->source[pos] != '\r' &&
                    tp->source[pos] != '\n') pos++;
            continue;
        }
        if (tp->source[pos] == '/' && pos + 1 < end &&
                tp->source[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < end && !(tp->source[pos] == '*' &&
                    tp->source[pos + 1] == '/')) pos++;
            if (pos + 1 < end) pos += 2;
            continue;
        }
        return tp->source[pos] != ';';
    }
    // The cooked value alone is insufficient here: escaped characters and
    // line continuations are valid directives, but are not the exact Use
    // Strict Directive spelling required by the directive-prologue grammar.
    JsAstNode* statement = first;
    while (statement) {
        if (statement->node_type != JS_AST_NODE_EXPRESSION_STATEMENT) return false;
        JsExpressionStatementNode* expression_statement =
            (JsExpressionStatementNode*)statement;
        JsAstNode* expression = expression_statement->expression;
        if (!expression || expression->node_type != JS_AST_NODE_LITERAL ||
                ((JsLiteralNode*)expression)->literal_type != JS_LITERAL_STRING) {
            return false;
        }
        SourceSpan expression_span = expression->source_span;
        if (expression_span.end_byte <= expression_span.start_byte ||
                expression_span.end_byte > tp->source_length) return false;
        size_t expression_length = expression_span.end_byte -
            expression_span.start_byte;
        const char* raw = tp->source + expression_span.start_byte;
        if (expression_length == 12 &&
                (raw[0] == '\'' || raw[0] == '"') &&
                raw[11] == raw[0] &&
                memcmp(raw + 1, "use strict", 10) == 0) {
            return true;
        }
        statement = statement->next;
    }
    return false;
}

// build a function from parser-owned children after the body has reduced.
static JsAstNode* build_js_function_from_children_common(
        JsTranspiler* tp, SourceSpan span, JsAstNode* name, JsAstNode* params,
        JsAstNode* body, Type* return_type, bool async, bool generator,
        bool declaration, bool arrow) {
    if (!tp || !body || (name && name->node_type != JS_AST_NODE_IDENTIFIER)) {
        log_error("JavaScript function has invalid children");
        return NULL;
    }
    JsAstNodeType node_type = arrow ? JS_AST_NODE_ARROW_FUNCTION :
        (declaration ? JS_AST_NODE_FUNCTION_DECLARATION :
            JS_AST_NODE_FUNCTION_EXPRESSION);
    JsFunctionNode* function = (JsFunctionNode*)alloc_js_ast_node_span(tp,
        node_type, span, sizeof(JsFunctionNode));
    function->type = &TYPE_FUNC;
    function->params = params;
    function->body = body;
    function->is_arrow = arrow;
    function->is_async = async;
    function->is_generator = generator;
    function->has_use_strict_directive =
        js_ast_body_has_use_strict_directive_source(tp, body);
    function->name = name ? ((JsIdentifierNode*)name)->name : NULL;
    function->declared_return_type = return_type;

    return (JsAstNode*)function;
}

JsAstNode* build_js_function_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* name, JsAstNode* params, JsAstNode* body, bool async,
        bool generator, bool declaration, bool arrow) {
    return build_js_function_from_children_common(tp, span, name, params, body,
        NULL, async, generator, declaration, arrow);
}

JsAstNode* build_js_function_with_return_type_from_children(
        JsTranspiler* tp, SourceSpan span, JsAstNode* name, JsAstNode* params,
        JsAstNode* body, Type* return_type, bool async, bool generator,
        bool declaration, bool arrow) {
    return build_js_function_from_children_common(tp, span, name, params, body,
        return_type, async, generator, declaration, arrow);
}

// transfer the parsed function payload into its owning method node.
static void js_method_adopt_function_payload(JsMethodDefinitionNode* method, JsAstNode* value) {
    if (!method) return;
    if (!value || (value->node_type != JS_AST_NODE_FUNCTION_EXPRESSION &&
                   value->node_type != JS_AST_NODE_FUNCTION_DECLARATION &&
                   value->node_type != JS_AST_NODE_ARROW_FUNCTION)) {
        return;
    }
    JsFunctionNode* fn = (JsFunctionNode*)value;
    method->name = fn->name;
    method->params = fn->params;
    method->body = fn->body;
    method->vars = fn->vars;
    method->captures = fn->captures;
    method->is_arrow = fn->is_arrow;
    method->is_async = fn->is_async;
    method->is_generator = fn->is_generator;
    method->has_use_strict_directive = fn->has_use_strict_directive;
    method->declared_return_type = fn->declared_return_type;
    method->type = fn->type;
}

// build a class body from parser-owned member nodes
JsAstNode* build_js_class_body_from_list(JsTranspiler* tp, SourceSpan span,
        JsAstNode* members, uint32_t length) {
    (void)length;
    JsBlockNode* body = (JsBlockNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_BLOCK_STATEMENT, span, sizeof(JsBlockNode));
    body->statements = members;
    body->type = &TYPE_NULL;
    return (JsAstNode*)body;
}

// build a class method from its key, parameter, and body reductions
static String* js_computed_method_name_from_source(JsTranspiler* tp,
        SourceSpan method_span, SourceSpan key_span) {
    if (!tp || !tp->source || key_span.start_byte < method_span.start_byte ||
            key_span.end_byte < key_span.start_byte ||
            key_span.end_byte > method_span.end_byte) return NULL;
    size_t open = method_span.start_byte;
    while (open < key_span.start_byte && tp->source[open] != '[') open++;
    if (open >= key_span.start_byte) return NULL;
    size_t close = key_span.end_byte;
    while (close < method_span.end_byte && tp->source[close] != ']') close++;
    if (close >= method_span.end_byte) return NULL;
    return name_pool_create_len(tp->name_pool, tp->source + open,
        (int)(close - open + 1));
}

JsAstNode* build_js_method_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* key, JsAstNode* params, JsAstNode* body, uint32_t flags) {
    if (!tp || !key || !body || body->node_type != JS_AST_NODE_BLOCK_STATEMENT) {
        log_error("JavaScript class method has invalid children");
        return NULL;
    }
    JsAstNode* function = build_js_function_from_children(tp, span, NULL,
        params, body, (flags & JS_REDUCTION_FLAG_ASYNC) != 0,
        (flags & JS_REDUCTION_FLAG_GENERATOR) != 0, false, false);
    if (!function) return NULL;
    // preserve the method name on the retained function payload for class constructors.
    if (key->node_type == JS_AST_NODE_IDENTIFIER) {
        ((JsFunctionNode*)function)->name = ((JsIdentifierNode*)key)->name;
    } else if (flags & JS_REDUCTION_FLAG_COMPUTED) {
        ((JsFunctionNode*)function)->name =
            js_computed_method_name_from_source(tp, span, key->source_span);
    }

    JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)alloc_js_ast_node_span(
        tp, JS_AST_NODE_METHOD_DEFINITION, span, sizeof(JsMethodDefinitionNode));
    method->key = key;
    method->computed = (flags & JS_REDUCTION_FLAG_COMPUTED) != 0;
    method->static_method = (flags & JS_REDUCTION_FLAG_STATIC) != 0;
    method->kind = (flags & JS_REDUCTION_FLAG_GETTER)
        ? JsMethodDefinitionNode::JS_METHOD_GET
        : ((flags & JS_REDUCTION_FLAG_SETTER)
            ? JsMethodDefinitionNode::JS_METHOD_SET
            : JsMethodDefinitionNode::JS_METHOD_METHOD);
    js_method_adopt_function_payload(method, function);
    if (!method->static_method && method->kind == JsMethodDefinitionNode::JS_METHOD_METHOD &&
            key->node_type == JS_AST_NODE_IDENTIFIER) {
        String* name = ((JsIdentifierNode*)key)->name;
        if (name && name->len == 11 && memcmp(name->chars, "constructor", 11) == 0) {
            method->kind = JsMethodDefinitionNode::JS_METHOD_CONSTRUCTOR;
        }
    }
    if (method->kind == JsMethodDefinitionNode::JS_METHOD_CONSTRUCTOR &&
            body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        JsAstNode* assignments = NULL;
        JsAstNode* assignment_tail = NULL;
        for (JsAstNode* parameter = params; parameter;
                parameter = parameter->next) {
            TsParameterNode* ts_parameter = parameter->node_type ==
                (JsAstNodeType)TS_AST_NODE_PARAMETER
                    ? (TsParameterNode*)parameter : NULL;
            JsAstNode* pattern = ts_parameter ? ts_parameter->pattern : NULL;
            if (!ts_parameter || !ts_parameter->accessibility || !pattern ||
                    pattern->node_type != JS_AST_NODE_IDENTIFIER) continue;
            String* name = ((JsIdentifierNode*)pattern)->name;
            JsAstNode* assignment = build_js_this_assignment_from_name(tp,
                pattern->source_span, name);
            if (!assignment) continue;
            if (!assignments) assignments = assignment;
            else assignment_tail->next = assignment;
            assignment_tail = assignment;
        }
        if (assignments) {
            JsBlockNode* block = (JsBlockNode*)body;
            assignment_tail->next = block->statements;
            block->statements = assignments;
        }
    }
    method->type = &TYPE_FUNC;
    return (JsAstNode*)method;
}

// build a class field from its key and optional initializer
JsAstNode* build_js_field_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* key, JsAstNode* value, uint32_t flags) {
    if (!tp || !key) {
        log_error("JavaScript class field has no key");
        return NULL;
    }
    JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)alloc_js_ast_node_span(
        tp, JS_AST_NODE_FIELD_DEFINITION, span, sizeof(JsFieldDefinitionNode));
    field->key = key;
    field->value = value;
    field->is_static = (flags & JS_REDUCTION_FLAG_STATIC) != 0;
    field->is_private = key->node_type == JS_AST_NODE_IDENTIFIER &&
        ((JsIdentifierNode*)key)->name &&
        ((JsIdentifierNode*)key)->name->len > 0 &&
        ((JsIdentifierNode*)key)->name->chars[0] == '#';
    field->computed = (flags & JS_REDUCTION_FLAG_COMPUTED) != 0;
    field->type = NULL;
    return (JsAstNode*)field;
}

// build a static initialization block from its block reduction
JsAstNode* build_js_static_block_from_child(JsTranspiler* tp, SourceSpan span,
        JsAstNode* body) {
    if (!tp || !body || body->node_type != JS_AST_NODE_BLOCK_STATEMENT) {
        log_error("JavaScript static block has invalid body");
        return NULL;
    }
    JsStaticBlockNode* block = (JsStaticBlockNode*)alloc_js_ast_node_span(
        tp, JS_AST_NODE_STATIC_BLOCK, span, sizeof(JsStaticBlockNode));
    block->body = body;
    // Static blocks are execution containers, not value expressions; their
    // derived value type remains unset while the nested block carries null.
    block->type = NULL;
    return (JsAstNode*)block;
}

// build a class after its heritage and body have reduced
JsAstNode* build_js_class_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* name, JsAstNode* superclass, JsAstNode* body, bool declaration) {
    if (!tp || !body || body->node_type != JS_AST_NODE_BLOCK_STATEMENT ||
            (name && name->node_type != JS_AST_NODE_IDENTIFIER)) {
        log_error("JavaScript class has invalid children");
        return NULL;
    }
    JsAstNodeType node_type = declaration ? JS_AST_NODE_CLASS_DECLARATION :
        JS_AST_NODE_CLASS_EXPRESSION;
    JsClassNode* class_node = (JsClassNode*)alloc_js_ast_node_span(tp,
        node_type, span, sizeof(JsClassNode));
    class_node->name = name ? ((JsIdentifierNode*)name)->name : NULL;
    class_node->superclass = superclass;
    class_node->body = body;
    class_node->type = &TYPE_FUNC;

    return (JsAstNode*)class_node;
}

// build a classic for loop from its present clauses
JsAstNode* build_js_for_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* init, JsAstNode* test, JsAstNode* update, JsAstNode* body) {
    if (!tp) {
        log_error("JavaScript for loop has no transpiler");
        return NULL;
    }
    JsForNode* loop = (JsForNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_FOR_STATEMENT, span, sizeof(JsForNode));
    loop->init = init;
    loop->test = test;
    loop->update = update;
    loop->body = body;
    loop->form = LOOP_FORM_FOR_C;
    loop->type = &TYPE_NULL;
    return (JsAstNode*)loop;
}

// normalize expression-form assignment heads to the pattern nodes used by the
// reference AST; object literals that are the receiver of a member target stay
// expressions because they are evaluated before the destructuring write.
static void normalize_js_for_head_target(JsTranspiler* tp, JsAstNode* node) {
    if (!node) return;
    if (node->node_type == JS_AST_NODE_ARRAY_EXPRESSION) {
        JsArrayNode* array = (JsArrayNode*)node;
        node->node_type = JS_AST_NODE_ARRAY_PATTERN;
        node->type = &TYPE_ARRAY;
        for (JsAstNode* child = array->elements; child; child = child->next)
            normalize_js_for_head_target(tp, child);
        return;
    }
    if (node->node_type == JS_AST_NODE_OBJECT_EXPRESSION) {
        JsObjectNode* object = (JsObjectNode*)node;
        node->node_type = JS_AST_NODE_OBJECT_PATTERN;
        node->type = js_set_type_any(tp, ANY_DECOMPOSE);
        for (JsAstNode* child = object->properties; child; child = child->next) {
            if (child->node_type == JS_AST_NODE_PROPERTY) {
                JsPropertyNode* property = (JsPropertyNode*)child;
                property->shorthand = false;
                normalize_js_for_head_target(tp, property->value);
            } else if (child->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                JsSpreadElementNode* spread = (JsSpreadElementNode*)child;
                child->node_type = JS_AST_NODE_REST_PROPERTY;
                child->type = js_set_type_any(tp, ANY_DECOMPOSE);
                normalize_js_for_head_target(tp, spread->argument);
            }
        }
        return;
    }
    if (node->node_type == JS_AST_NODE_ASSIGNMENT_EXPRESSION) {
        JsAssignmentNode* assignment = (JsAssignmentNode*)node;
        if (assignment->op == JS_OP_ASSIGN) {
            node->node_type = JS_AST_NODE_ASSIGNMENT_PATTERN;
            assignment->op = (JsOperator)0;
            node->type = js_set_type_any(tp, ANY_DECOMPOSE);
            normalize_js_for_head_target(tp, assignment->left);
        }
        return;
    }
    if (node->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
        JsSpreadElementNode* spread = (JsSpreadElementNode*)node;
        node->node_type = JS_AST_NODE_REST_ELEMENT;
        node->type = &TYPE_ARRAY;
        normalize_js_for_head_target(tp, spread->argument);
    }
}

static void normalize_js_for_head_pattern(JsTranspiler* tp, JsAstNode* left) {
    if (!tp || !left || (left->node_type != JS_AST_NODE_ARRAY_EXPRESSION &&
            left->node_type != JS_AST_NODE_OBJECT_EXPRESSION)) return;
    normalize_js_for_head_target(tp, left);
    if (left->node_type == JS_AST_NODE_OBJECT_PATTERN)
        left->type = js_set_type_any(tp, ANY_DECOMPOSE);
}

// build a for-in or for-of loop from its iteration head and source
JsAstNode* build_js_for_of_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* left, JsAstNode* right, JsAstNode* body, int kind,
        bool declares_binding, bool is_for_await, bool is_for_in) {
    if (!tp || !left || !right) {
        log_error("JavaScript iteration loop has an invalid head");
        return NULL;
    }
    JsAstNode* initializer = NULL;
    if (left->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
        JsVariableDeclarationNode* declaration =
            (JsVariableDeclarationNode*)left;
        JsAstNode* item = declaration->declarations;
        if (item && item->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
            initializer = ((JsVariableDeclaratorNode*)item)->init;
        }
    }
    if (!declares_binding) normalize_js_for_head_pattern(tp, left);
    // declaration heads retain the binding pattern, not the transient
    // variable-declaration wrapper used by the parser reduction.
    if (left->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
        JsVariableDeclarationNode* declaration =
            (JsVariableDeclarationNode*)left;
        JsAstNode* item = declaration->declarations;
        if (!item || item->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) {
            log_error("JavaScript iteration declaration has no declarator");
            return NULL;
        }
        left = ((JsVariableDeclaratorNode*)item)->id;
        if (left && left->node_type == JS_AST_NODE_IDENTIFIER) {
            left->type = &TYPE_ANY;
        }
    }
    JsForOfNode* loop = (JsForOfNode*)alloc_js_ast_node_span(tp,
        is_for_in ? JS_AST_NODE_FOR_IN_STATEMENT : JS_AST_NODE_FOR_OF_STATEMENT,
        span, sizeof(JsForOfNode));
    loop->left = left;
    loop->init = initializer;
    loop->right = right;
    loop->body = body;
    loop->kind = kind;
    loop->declares_binding = declares_binding;
    loop->is_await = is_for_await;
    loop->type = &TYPE_NULL;
    return (JsAstNode*)loop;
}

// build a switch and its already-linked case list
JsAstNode* build_js_switch_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* discriminant, JsAstNode* cases, uint32_t length) {
    if (!tp || !discriminant) {
        log_error("JavaScript switch has no discriminant");
        return NULL;
    }
    JsSwitchNode* switched = (JsSwitchNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_SWITCH_STATEMENT, span, sizeof(JsSwitchNode));
    switched->discriminant = discriminant;
    switched->cases = cases;
    (void)length;
    switched->type = &TYPE_NULL;
    return (JsAstNode*)switched;
}

// build one switch case, retaining a null test for default
JsAstNode* build_js_switch_case_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* test, JsAstNode* consequent, bool is_default) {
    if (!tp) return NULL;
    JsSwitchCaseNode* case_node = (JsSwitchCaseNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_SWITCH_CASE, span, sizeof(JsSwitchCaseNode));
    case_node->test = is_default ? NULL : test;
    case_node->consequent = consequent;
    case_node->body_braced = false;
    case_node->type = &TYPE_NULL;
    return (JsAstNode*)case_node;
}

// build try/catch/finally after all clauses have reduced
JsAstNode* build_js_try_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* block, JsAstNode* handler, JsAstNode* finalizer) {
    if (!tp || !block) {
        log_error("JavaScript try statement has no block");
        return NULL;
    }
    JsTryNode* tried = (JsTryNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_TRY_STATEMENT, span, sizeof(JsTryNode));
    tried->block = block;
    tried->handler = handler;
    tried->finalizer = finalizer;
    tried->type = &TYPE_NULL;
    return (JsAstNode*)tried;
}

// build a catch clause and its handler binding scope
JsAstNode* build_js_catch_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* parameter, JsAstNode* body) {
    if (!tp || !body) {
        log_error("JavaScript catch clause has no body");
        return NULL;
    }
    JsCatchNode* handler = (JsCatchNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_CATCH_CLAUSE, span, sizeof(JsCatchNode));
    handler->param = parameter;
    handler->body = body;
    handler->type = &TYPE_NULL;
    return (JsAstNode*)handler;
}

static String* js_name_from_binding_node(JsAstNode* node) {
    if (!node || node->node_type != JS_AST_NODE_IDENTIFIER) return NULL;
    return ((JsIdentifierNode*)node)->name;
}

static String* js_module_source_from_literal(JsAstNode* node) {
    if (!node || node->node_type != JS_AST_NODE_LITERAL) return NULL;
    JsLiteralNode* literal = (JsLiteralNode*)node;
    return literal->literal_type == JS_LITERAL_STRING
        ? literal->value.string_value : NULL;
}

// build an import specifier from parser-owned remote/local name nodes
JsAstNode* build_js_import_specifier_from_children(JsTranspiler* tp,
        SourceSpan span, JsAstNode* remote, JsAstNode* local) {
    String* remote_name = js_name_from_binding_node(remote);
    String* local_name = js_name_from_binding_node(local);
    if (!tp || !remote_name) {
        log_error("JavaScript import specifier has no remote name");
        return NULL;
    }
    if (!local_name) local_name = remote_name;
    JsImportSpecifierNode* specifier = (JsImportSpecifierNode*)alloc_js_ast_node_span(
        tp, JS_AST_NODE_IMPORT_SPECIFIER, span, sizeof(JsImportSpecifierNode));
    specifier->remote_name = remote_name;
    specifier->local_name = local_name;
    specifier->local_entry = local && local->node_type == JS_AST_NODE_IDENTIFIER
        ? ((JsIdentifierNode*)local)->entry : NULL;
    specifier->type = NULL;
    return (JsAstNode*)specifier;
}

// build an import declaration and retain the interpreter's module records
JsAstNode* build_js_import_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* source, JsAstNode* default_name, JsAstNode* namespace_name,
        JsAstNode* specifiers) {
    if (!tp) return NULL;
    JsImportNode* node = (JsImportNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_IMPORT_DECLARATION, span, sizeof(JsImportNode));
    node->source = js_module_source_from_literal(source);
    node->default_name = js_name_from_binding_node(default_name);
    node->namespace_name = js_name_from_binding_node(namespace_name);
    node->default_entry = default_name &&
            default_name->node_type == JS_AST_NODE_IDENTIFIER
        ? ((JsIdentifierNode*)default_name)->entry : NULL;
    node->namespace_entry = namespace_name &&
            namespace_name->node_type == JS_AST_NODE_IDENTIFIER
        ? ((JsIdentifierNode*)namespace_name)->entry : NULL;
    node->specifiers = specifiers;
    js_record_interp_import(tp, node->default_name, node->source,
        name_pool_create_len(tp->name_pool, "default", 7), false);
    js_record_interp_import(tp, node->namespace_name, node->source, NULL, true);
    for (JsAstNode* item = specifiers; item; item = (JsAstNode*)item->next) {
        JsImportSpecifierNode* specifier = (JsImportSpecifierNode*)item;
        js_record_interp_import(tp, specifier->local_name, node->source,
            specifier->remote_name, false);
    }
    node->type = &TYPE_NULL;
    return (JsAstNode*)node;
}

// build an export specifier from parser-owned local/exported name nodes
JsAstNode* build_js_export_specifier_from_children(JsTranspiler* tp,
        SourceSpan span, JsAstNode* local, JsAstNode* export_name) {
    String* local_name = js_name_from_binding_node(local);
    String* exported_name = js_name_from_binding_node(export_name);
    if (!tp || !local_name) {
        log_error("JavaScript export specifier has no local name");
        return NULL;
    }
    if (!exported_name) exported_name = local_name;
    JsExportSpecifierNode* specifier = (JsExportSpecifierNode*)alloc_js_ast_node_span(
        tp, JS_AST_NODE_EXPORT_SPECIFIER, span, sizeof(JsExportSpecifierNode));
    specifier->local_name = local_name;
    specifier->export_name = exported_name;
    specifier->type = NULL;
    return (JsAstNode*)specifier;
}

// build an export declaration and retain declaration/specifier module facts
JsAstNode* build_js_export_from_children(JsTranspiler* tp, SourceSpan span,
        JsAstNode* declaration, JsAstNode* specifiers, JsAstNode* source,
        uint32_t flags) {
    if (!tp) return NULL;
    JsExportNode* node = (JsExportNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_EXPORT_DECLARATION, span, sizeof(JsExportNode));
    node->declaration = declaration;
    node->specifiers = specifiers;
    node->source = js_module_source_from_literal(source);
    node->is_default = (flags & JS_REDUCTION_FLAG_DEFAULT) != 0;
    node->is_namespace = (flags & JS_REDUCTION_FLAG_EXPORT_NAMESPACE) != 0;
    // namespace re-exports carry a namespace fact; the reference AST does
    // not classify that form as a plain star export.
    node->is_star = (flags & JS_REDUCTION_FLAG_EXPORT_STAR) != 0 &&
        !node->is_namespace;
    if (node->is_default && declaration &&
            (declaration->node_type == JS_AST_NODE_CLASS_DECLARATION ||
             declaration->node_type == JS_AST_NODE_CLASS_EXPRESSION)) {
        JsClassNode* class_node = (JsClassNode*)declaration;
        if (!class_node->name) {
            class_node->name = name_pool_create_len(tp->name_pool, "default", 7);
        }
    }
    for (JsAstNode* item = specifiers; item; item = (JsAstNode*)item->next) {
        JsExportSpecifierNode* specifier = (JsExportSpecifierNode*)item;
        js_record_interp_export(tp, specifier->local_name,
            specifier->export_name, node->source, node->is_namespace, false);
    }
    if (declaration && !node->is_default) {
        String* name = NULL;
        if (declaration->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            name = ((JsFunctionNode*)declaration)->name;
        } else if (declaration->node_type == JS_AST_NODE_CLASS_DECLARATION ||
                declaration->node_type == JS_AST_NODE_CLASS_EXPRESSION) {
            name = ((JsClassNode*)declaration)->name;
        }
        if (name) js_record_interp_export(tp, name, name, NULL, false, false);
        else if (declaration->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
            JsVariableDeclarationNode* variables =
                (JsVariableDeclarationNode*)declaration;
            for (JsAstNode* item = variables->declarations; item;
                    item = (JsAstNode*)item->next) {
                JsVariableDeclaratorNode* variable =
                    (JsVariableDeclaratorNode*)item;
                String* local = js_name_from_binding_node(variable->id);
                if (local) js_record_interp_export(tp, local, local, NULL,
                    false, false);
            }
        }
    }
    node->type = &TYPE_NULL;
    return (JsAstNode*)node;
}

// build an object-literal method as the existing property/function shape
JsAstNode* build_js_object_method_from_children(JsTranspiler* tp,
        SourceSpan span, JsAstNode* key, JsAstNode* params, JsAstNode* body,
        uint32_t flags) {
    if (!tp || !key || !body || body->node_type != JS_AST_NODE_BLOCK_STATEMENT) {
        log_error("JavaScript object method has invalid children");
        return NULL;
    }
    JsAstNode* function = build_js_function_from_children(tp, span, NULL,
        params, body, (flags & JS_REDUCTION_FLAG_ASYNC) != 0,
        (flags & JS_REDUCTION_FLAG_GENERATOR) != 0, true, false);
    if (!function) return NULL;
    JsFunctionNode* fn = (JsFunctionNode*)function;
    fn->name = NULL;
    JsPropertyNode* property = (JsPropertyNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_PROPERTY, span, sizeof(JsPropertyNode));
    bool computed = (flags & JS_REDUCTION_FLAG_COMPUTED) != 0;
    bool getter = (flags & JS_REDUCTION_FLAG_GETTER) != 0;
    bool setter = (flags & JS_REDUCTION_FLAG_SETTER) != 0;
    // Method names are property labels, not identifier reads. The reference
    // adapter retains the label node but leaves it unresolved.
    property->key = key;
    if (!computed && key && key->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* key_identifier = (JsIdentifierNode*)key;
        key_identifier->entry = NULL;
        key_identifier->type = NULL;
    }
    property->value = function;
    property->computed = computed;
    property->method = !getter && !setter;
    property->is_getter = getter;
    property->is_setter = setter;
    property->shorthand = false;
    property->type = js_set_type_any(tp, ANY_OPEN_MAP);
    if ((flags & (JS_REDUCTION_FLAG_GETTER | JS_REDUCTION_FLAG_SETTER)) &&
            key->node_type == JS_AST_NODE_IDENTIFIER) {
        key->type = NULL;
    }
    return (JsAstNode*)property;
}

static JsAstNode* make_named_identifier_from_span(JsTranspiler* tp,
        SourceSpan span, String* name) {
    JsIdentifierNode* id = (JsIdentifierNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_IDENTIFIER, span, sizeof(JsIdentifierNode));
    id->name = name;
    id->type = js_set_type_any(tp, ANY_OPEN_PARAM);
    return (JsAstNode*)id;
}

JsAstNode* build_js_this_assignment_from_name(JsTranspiler* tp,
        SourceSpan span, String* name) {
    if (!tp || !name) return NULL;
    String* this_name = name_pool_create_len(tp->name_pool, "this", 4);
    JsMemberNode* member = (JsMemberNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_MEMBER_EXPRESSION, span, sizeof(JsMemberNode));
    member->object = make_named_identifier_from_span(tp, span, this_name);
    member->property = make_named_identifier_from_span(tp, span, name);
    member->computed = false;
    member->optional = false;
    member->type = js_set_type_any(tp, ANY_JS_MEMBER);

    JsAssignmentNode* assign = (JsAssignmentNode*)alloc_js_ast_node_span(tp,
        JS_AST_NODE_ASSIGNMENT_EXPRESSION, span, sizeof(JsAssignmentNode));
    assign->op = JS_OP_ASSIGN;
    assign->left = (JsAstNode*)member;
    assign->right = make_named_identifier_from_span(tp, span, name);
    assign->type = js_set_type_any(tp, ANY_STATEMENT);

    JsExpressionStatementNode* expr_stmt =
        (JsExpressionStatementNode*)alloc_js_ast_node_span(tp,
            JS_AST_NODE_EXPRESSION_STATEMENT, span,
            sizeof(JsExpressionStatementNode));
    expr_stmt->expression = (JsAstNode*)assign;
    expr_stmt->type = &TYPE_NULL;

    return (JsAstNode*)expr_stmt;
}
