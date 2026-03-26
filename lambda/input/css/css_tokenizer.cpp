#include "css_tokenizer.hpp"
#include <stdlib.h>
#include <string.h>
#include "../../../lib/str.h"
#include "../input-utils.hpp"
#include <ctype.h>
#include <assert.h>

// Helper function to parse CSS unit from string
// Delegates to the canonical css_unit_from_string() in css_value.cpp
static CssUnit parse_css_unit(const char* unit_str, size_t length) {
    return css_unit_from_string(unit_str, length);
}

// Helper: tokenize a CSS numeric value (number, percentage, or dimension).
// `start` is the beginning of the token (may include a sign or leading dot).
// `pos` on entry points past any sign/dot prefix to the first digit position.
// On return, `*pos_ptr` is advanced past the full number+unit/percentage.
static void tokenize_number(const char* input, size_t length, size_t start,
                            size_t* pos_ptr, CssToken* token, Pool* pool) {
    size_t pos = *pos_ptr;

    // parse integer part
    while (pos < length && isdigit(input[pos])) {
        pos++;
    }

    // parse decimal part
    if (pos < length && input[pos] == '.') {
        pos++;
        while (pos < length && isdigit(input[pos])) {
            pos++;
        }
    }

    // CSS Values Level 4: scientific notation exponent (e.g., 3.68e+19)
    if (pos < length && (input[pos] == 'e' || input[pos] == 'E')) {
        size_t exp_start = pos;
        pos++;
        if (pos < length && (input[pos] == '+' || input[pos] == '-')) pos++;
        if (pos < length && isdigit(input[pos])) {
            while (pos < length && isdigit(input[pos])) pos++;
        } else {
            pos = exp_start;  // not a valid exponent, rollback
        }
    }

    // determine token type: percentage, dimension, or plain number
    size_t number_end = pos;
    if (pos < length && input[pos] == '%') {
        pos++;
        token->type = CSS_TOKEN_PERCENTAGE;
    } else if (pos < length && (isalpha(input[pos]) || input[pos] == '_')) {
        size_t unit_start = pos;
        while (pos < length && (isalnum(input[pos]) || input[pos] == '_' || input[pos] == '-')) {
            pos++;
        }
        token->type = CSS_TOKEN_DIMENSION;
        CssUnit parsed_unit = parse_css_unit(&input[unit_start], pos - unit_start);
        token->data.dimension.unit = parsed_unit;
    } else {
        token->type = CSS_TOKEN_NUMBER;
    }

    token->length = pos - start;

    // parse the numeric value
    char* num_str = static_cast<char*>(pool_alloc(pool, number_end - start + 1));
    if (num_str) {
        strncpy(num_str, token->start, number_end - start);
        num_str[number_end - start] = '\0';
        if (token->type == CSS_TOKEN_DIMENSION) {
            token->data.dimension.value = str_to_double_default(num_str, number_end - start, 0.0);
        } else {
            token->data.number_value = str_to_double_default(num_str, number_end - start, 0.0);
        }
    }

    *pos_ptr = pos;
}

// Enhanced Unicode character classification
bool css_is_name_start_char_unicode(uint32_t codepoint) {
    // CSS3 name-start character definition
    if (codepoint >= 'a' && codepoint <= 'z') return true;
    if (codepoint >= 'A' && codepoint <= 'Z') return true;
    if (codepoint == '_') return true;
    if (codepoint >= 0x80) return true; // Non-ASCII
    return false;
}

bool css_is_name_char_unicode(uint32_t codepoint) {
    if (css_is_name_start_char_unicode(codepoint)) return true;
    if (codepoint >= '0' && codepoint <= '9') return true;
    if (codepoint == '-') return true;
    return false;
}

bool css_is_whitespace_unicode(uint32_t codepoint) {
    return codepoint == ' ' || codepoint == '\t' ||
           codepoint == '\n' || codepoint == '\r' || codepoint == '\f';
}

// Unicode parsing utilities
UnicodeChar css_parse_unicode_char(const char* input, size_t max_length) {
    UnicodeChar result = {0, 0};

    if (max_length == 0 || !input) return result;

    unsigned char first = (unsigned char)input[0];

    if (first < 0x80) {
        // ASCII character
        result.codepoint = first;
        result.byte_length = 1;
    } else if ((first & 0xE0) == 0xC0 && max_length >= 2) {
        // 2-byte UTF-8
        result.codepoint = ((first & 0x1F) << 6) | (input[1] & 0x3F);
        result.byte_length = 2;
    } else if ((first & 0xF0) == 0xE0 && max_length >= 3) {
        // 3-byte UTF-8
        result.codepoint = ((first & 0x0F) << 12) |
                          ((input[1] & 0x3F) << 6) |
                          (input[2] & 0x3F);
        result.byte_length = 3;
    } else if ((first & 0xF8) == 0xF0 && max_length >= 4) {
        // 4-byte UTF-8
        result.codepoint = ((first & 0x07) << 18) |
                          ((input[1] & 0x3F) << 12) |
                          ((input[2] & 0x3F) << 6) |
                          (input[3] & 0x3F);
        result.byte_length = 4;
    }

    return result;
}

bool css_is_valid_unicode_escape(const char* input) {
    if (!input || input[0] != '\\') return false;

    // CSS Unicode escape: \HHHHH (1-6 hex digits)
    const char* p = input + 1;
    int hex_count = 0;

    while (*p && css_is_hex_digit(*p) && hex_count < 6) {
        p++;
        hex_count++;
    }

    // Must have at least 1 hex digit
    if (hex_count == 0) return false;

    // Optional whitespace after hex digits
    if (*p && css_is_whitespace_unicode(*p)) p++;

    return true;
}

char* css_decode_unicode_escapes(const char* input, Pool* pool) {
    if (!input || !pool) return NULL;

    size_t input_len = strlen(input);
    char* result = (char*)pool_alloc(pool, input_len * 4 + 1); // Worst case for UTF-8
    if (!result) return NULL;

    size_t result_pos = 0;
    const char* p = input;

    while (*p) {
        if (*p == '\\' && css_is_valid_unicode_escape(p)) {
            // Parse Unicode escape
            p++; // Skip backslash
            uint32_t codepoint = 0;
            int hex_count = 0;

            while (*p && css_is_hex_digit(*p) && hex_count < 6) {
                codepoint = (codepoint << 4) +
                           (*p >= 'A' ? *p - 'A' + 10 :
                            *p >= 'a' ? *p - 'a' + 10 : *p - '0');
                p++;
                hex_count++;
            }

            // Skip optional whitespace
            if (*p && css_is_whitespace_unicode(*p)) p++;

            // Convert codepoint to UTF-8
            char utf8_buf[5];
            int utf8_len = codepoint_to_utf8(codepoint, utf8_buf);
            for (int j = 0; j < utf8_len; j++) {
                result[result_pos++] = utf8_buf[j];
            }
        } else {
            result[result_pos++] = *p++;
        }
    }

    result[result_pos] = '\0';
    return result;
}

// check if input[pos] starts a valid CSS escape sequence
// a valid escape is \ followed by any char that is NOT a newline
static inline bool css_starts_escape(const char* input, size_t length, size_t pos) {
    return pos + 1 < length && input[pos] == '\\' &&
           input[pos + 1] != '\n' && input[pos + 1] != '\r' && input[pos + 1] != '\f';
}

// consume a CSS escape sequence starting at input[*pos], advancing *pos
// caller must verify css_starts_escape() first
static inline void css_consume_escape_seq(const char* input, size_t length, size_t* pos) {
    (*pos)++; // skip backslash
    if (*pos < length && css_is_hex_digit(input[*pos])) {
        int count = 0;
        while (*pos < length && css_is_hex_digit(input[*pos]) && count < 6) {
            (*pos)++;
            count++;
        }
        // skip optional whitespace after hex escape
        if (*pos < length && (input[*pos] == ' ' || input[*pos] == '\t' ||
            input[*pos] == '\n' || input[*pos] == '\r')) {
            (*pos)++;
        }
    } else if (*pos < length) {
        (*pos)++; // simple escape - skip one char
    }
}

// consume CSS name characters (letters, digits, -, _, escapes)
// advances *pos through contiguous name code points
static inline void css_consume_name(const char* input, size_t length, size_t* pos) {
    while (*pos < length) {
        if (isalnum(input[*pos]) || input[*pos] == '-' || input[*pos] == '_') {
            (*pos)++;
        } else if (css_starts_escape(input, length, *pos)) {
            css_consume_escape_seq(input, length, pos);
        } else {
            break;
        }
    }
}

// CSS3+ function information database
static CssFunctionInfo css_function_database[] = {
    // Mathematical functions
    {"calc", 1, 1, NULL, false, true},
    {"min", 1, -1, NULL, true, true},
    {"max", 1, -1, NULL, true, true},
    {"clamp", 3, 3, NULL, false, true},
    {"round", 2, 4, NULL, false, true},
    {"mod", 2, 2, NULL, false, true},
    {"rem", 2, 2, NULL, false, true},
    {"sin", 1, 1, NULL, false, true},
    {"cos", 1, 1, NULL, false, true},
    {"tan", 1, 1, NULL, false, true},
    {"asin", 1, 1, NULL, false, true},
    {"acos", 1, 1, NULL, false, true},
    {"atan", 1, 1, NULL, false, true},
    {"atan2", 2, 2, NULL, false, true},
    {"pow", 2, 2, NULL, false, true},
    {"sqrt", 1, 1, NULL, false, true},
    {"hypot", 1, -1, NULL, true, true},
    {"log", 1, 2, NULL, false, true},
    {"exp", 1, 1, NULL, false, true},
    {"abs", 1, 1, NULL, false, true},
    {"sign", 1, 1, NULL, false, true},

    // Variable and environment functions
    {"var", 1, 2, NULL, false, false},
    {"env", 1, 2, NULL, false, false},
    {"attr", 1, 3, NULL, false, false},

    // Color functions
    {"rgb", 3, 4, NULL, false, true},
    {"rgba", 3, 4, NULL, false, true},
    {"hsl", 3, 4, NULL, false, true},
    {"hsla", 3, 4, NULL, false, true},
    {"hwb", 3, 4, NULL, false, true},
    {"lab", 3, 4, NULL, false, true},
    {"lch", 3, 4, NULL, false, true},
    {"oklab", 3, 4, NULL, false, true},
    {"oklch", 3, 4, NULL, false, true},
    {"color", 2, -1, NULL, true, true},
    {"color-mix", 3, 3, NULL, false, true},
    {"color-contrast", 2, -1, NULL, true, true},

    // Transform functions
    {"matrix", 6, 6, NULL, false, true},
    {"matrix3d", 16, 16, NULL, false, true},
    {"translate", 1, 2, NULL, false, true},
    {"translate3d", 3, 3, NULL, false, true},
    {"translateX", 1, 1, NULL, false, true},
    {"translateY", 1, 1, NULL, false, true},
    {"translateZ", 1, 1, NULL, false, true},
    {"scale", 1, 2, NULL, false, true},
    {"scale3d", 3, 3, NULL, false, true},
    {"scaleX", 1, 1, NULL, false, true},
    {"scaleY", 1, 1, NULL, false, true},
    {"scaleZ", 1, 1, NULL, false, true},
    {"rotate", 1, 1, NULL, false, true},
    {"rotate3d", 4, 4, NULL, false, true},
    {"rotateX", 1, 1, NULL, false, true},
    {"rotateY", 1, 1, NULL, false, true},
    {"rotateZ", 1, 1, NULL, false, true},
    {"skew", 1, 2, NULL, false, true},
    {"skewX", 1, 1, NULL, false, true},
    {"skewY", 1, 1, NULL, false, true},
    {"perspective", 1, 1, NULL, false, true},

    // Filter functions
    {"blur", 1, 1, NULL, false, true},
    {"brightness", 1, 1, NULL, false, true},
    {"contrast", 1, 1, NULL, false, true},
    {"drop-shadow", 2, 4, NULL, false, true},
    {"grayscale", 1, 1, NULL, false, true},
    {"hue-rotate", 1, 1, NULL, false, true},
    {"invert", 1, 1, NULL, false, true},
    {"opacity", 1, 1, NULL, false, true},
    {"saturate", 1, 1, NULL, false, true},
    {"sepia", 1, 1, NULL, false, true},

    // Gradient functions
    {"linear-gradient", 2, -1, NULL, true, false},
    {"radial-gradient", 2, -1, NULL, true, false},
    {"conic-gradient", 2, -1, NULL, true, false},
    {"repeating-linear-gradient", 2, -1, NULL, true, false},
    {"repeating-radial-gradient", 2, -1, NULL, true, false},
    {"repeating-conic-gradient", 2, -1, NULL, true, false},

    // Image functions
    {"url", 1, 1, NULL, false, false},
    {"image", 1, -1, NULL, true, false},
    {"image-set", 1, -1, NULL, true, false},
    {"cross-fade", 2, -1, NULL, true, false},
    {"element", 1, 1, NULL, false, false},

    // Grid functions
    {"repeat", 2, 2, NULL, false, false},
    {"minmax", 2, 2, NULL, false, true},
    {"fit-content", 1, 1, NULL, false, true},

    // Container and layer functions
    {"selector", 1, 1, NULL, false, false}, // For @supports

    {NULL, 0, 0, NULL, false, false} // Sentinel
};

CssFunctionInfo* css_get_function_info(const char* function_name) {
    if (!function_name) return NULL;

    for (CssFunctionInfo* info = css_function_database; info->name; info++) {
        if (strcmp(info->name, function_name) == 0) {
            return info;
        }
    }
    return NULL;
}

bool css_is_valid_css_function(const char* name) {
    return css_get_function_info(name) != NULL;
}

// Custom property validation
bool css_parse_custom_property_name(const char* input, size_t length) {
    if (length < 2 || input[0] != '-' || input[1] != '-') {
        return false;
    }

    // Must start with letter, underscore, or non-ASCII
    if (length > 2) {
        UnicodeChar first = css_parse_unicode_char(input + 2, length - 2);
        if (!css_is_name_start_char_unicode(first.codepoint)) {
            return false;
        }

        // Rest must be name characters
        size_t pos = 2 + first.byte_length;
        while (pos < length) {
            UnicodeChar ch = css_parse_unicode_char(input + pos, length - pos);
            if (!css_is_name_char_unicode(ch.codepoint)) {
                return false;
            }
            pos += ch.byte_length;
        }
    }

    return true;
}

// Enhanced color parsing
CssColorType css_detect_color_type(const char* color_str) {
    if (!color_str) return CSS_COLOR_KEYWORD;

    size_t len = strlen(color_str);

    if (color_str[0] == '#') {
        return CSS_COLOR_HEX;
    }

    if (len >= 4 && strncmp(color_str, "rgb(", 4) == 0) {
        return CSS_COLOR_RGB;
    }

    if (len >= 5 && strncmp(color_str, "rgba(", 5) == 0) {
        return CSS_COLOR_RGB;
    }

    if (len >= 4 && strncmp(color_str, "hsl(", 4) == 0) {
        return CSS_COLOR_HSL;
    }

    if (len >= 5 && strncmp(color_str, "hsla(", 5) == 0) {
        return CSS_COLOR_HSL;
    }

    if (len >= 4 && strncmp(color_str, "hwb(", 4) == 0) {
        return CSS_COLOR_HWB;
    }

    if (len >= 4 && strncmp(color_str, "lab(", 4) == 0) {
        return CSS_COLOR_LAB;
    }

    if (len >= 4 && strncmp(color_str, "lch(", 4) == 0) {
        return CSS_COLOR_LCH;
    }

    if (len >= 6 && strncmp(color_str, "oklab(", 6) == 0) {
        return CSS_COLOR_OKLAB;
    }

    if (len >= 6 && strncmp(color_str, "oklch(", 6) == 0) {
        return CSS_COLOR_OKLCH;
    }

    if (len >= 6 && strncmp(color_str, "color(", 6) == 0) {
        return CSS_COLOR_COLOR;
    }

    if (strcmp(color_str, "transparent") == 0) {
        return CSS_COLOR_TRANSPARENT;
    }

    if (strcmp(color_str, "currentColor") == 0 || strcmp(color_str, "currentcolor") == 0) {
        return CSS_COLOR_CURRENTCOLOR;
    }

    return CSS_COLOR_KEYWORD;
}

// String conversion utilities
const char* css_token_type_to_str(CssTokenType type) {
    switch (type) {
        case CSS_TOKEN_CUSTOM_PROPERTY: return "CUSTOM_PROPERTY";
        case CSS_TOKEN_CALC_FUNCTION: return "CALC_FUNCTION";
        case CSS_TOKEN_VAR_FUNCTION: return "VAR_FUNCTION";
        case CSS_TOKEN_ENV_FUNCTION: return "ENV_FUNCTION";
        case CSS_TOKEN_ATTR_FUNCTION: return "ATTR_FUNCTION";
        case CSS_TOKEN_COLOR_FUNCTION: return "COLOR_FUNCTION";
        case CSS_TOKEN_NESTING_SELECTOR: return "NESTING_SELECTOR";
        case CSS_TOKEN_CDO: return "CDO";
        case CSS_TOKEN_CDC: return "CDC";
        case CSS_TOKEN_BAD_STRING: return "BAD_STRING";
        case CSS_TOKEN_BAD_URL: return "BAD_URL";
        default:
            // Fall back to original tokenizer for basic types
            return css_token_type_to_str((CSSTokenType)type);
    }
}

const char* css_color_type_to_str(CssColorType type) {
    switch (type) {
        case CSS_COLOR_HEX: return "hex";
        case CSS_COLOR_RGB: return "rgb";
        case CSS_COLOR_HSL: return "hsl";
        case CSS_COLOR_HWB: return "hwb";
        case CSS_COLOR_LAB: return "lab";
        case CSS_COLOR_LCH: return "lch";
        case CSS_COLOR_OKLAB: return "oklab";
        case CSS_COLOR_OKLCH: return "oklch";
        case CSS_COLOR_COLOR: return "color";
        case CSS_COLOR_KEYWORD: return "keyword";
        case CSS_COLOR_TRANSPARENT: return "transparent";
        case CSS_COLOR_CURRENTCOLOR: return "currentcolor";
        case CSS_COLOR_SYSTEM: return "system";
        default: return "unknown";
    }
}

// Error recovery functions
bool css_token_is_recoverable_error(CssToken* token) {
    if (!token) return false;

    return token->type == CSS_TOKEN_BAD_STRING ||
           token->type == CSS_TOKEN_BAD_URL;
}

void css_token_fix_common_errors(CssToken* token, Pool* pool) {
    if (!token || !pool) return;

    if (token->type == CSS_TOKEN_BAD_STRING) {
        // Try to close unclosed string
        size_t len = token->length;
        char* fixed_value = (char*)pool_alloc(pool, len + 2);
        if (fixed_value) {
            memcpy(fixed_value, token->value, len);
            fixed_value[len] = '"'; // Add closing quote
            fixed_value[len + 1] = '\0';
            token->value = fixed_value;
            token->type = CSS_TOKEN_STRING;
        }
    } else if (token->type == CSS_TOKEN_BAD_URL) {
        // Try to close unclosed URL
        size_t len = token->length;
        char* fixed_value = (char*)pool_alloc(pool, len + 2);
        if (fixed_value) {
            memcpy(fixed_value, token->value, len);
            fixed_value[len] = ')'; // Add closing paren
            fixed_value[len + 1] = '\0';
            token->value = fixed_value;
            token->type = CSS_TOKEN_URL;
        }
    }
}
// Basic CSS tokenizer compatibility functions
CSSToken* css_tokenize(const char* input, size_t length, Pool* pool, size_t* token_count) {
    if (!input || !pool || !token_count) return NULL;

    // Use the tokenizer
    CSSTokenizer* enhanced = css_tokenizer_create(pool);
    if (!enhanced) return NULL;

    CssToken* enhanced_tokens;
    int enhanced_count = css_tokenizer_tokenize(enhanced, input, length, &enhanced_tokens);

    if (enhanced_count <= 0) {
        *token_count = 0;
        return NULL;
    }

    // Convert enhanced tokens to basic tokens for compatibility
    CSSToken* basic_tokens = (CSSToken*)pool_calloc(pool, enhanced_count * sizeof(CSSToken));
    if (!basic_tokens) {
        *token_count = 0;
        return NULL;
    }

    for (int i = 0; i < enhanced_count; i++) {
        CssToken* src = &enhanced_tokens[i];
        CSSToken* dst = &basic_tokens[i];

        // Copy all fields from enhanced token
        dst->type = src->type;
        dst->start = src->start;
        dst->length = src->length;
        dst->value = src->value;

        // Copy union data - use memcpy to ensure all union members are copied
        memcpy(&dst->data, &src->data, sizeof(src->data));

        // Copy metadata fields
        dst->line = src->line;
        dst->column = src->column;
        dst->is_escaped = src->is_escaped;
        dst->unicode_codepoint = src->unicode_codepoint;
    }

    *token_count = enhanced_count;
    return basic_tokens;
}

void css_free_tokens(CSSToken* tokens) {
    // Memory is managed by pool, nothing to do
    (void)tokens;
}

const char* css_enhanced_token_type_to_str(CSSTokenType type) {
    switch (type) {
        case CSS_TOKEN_IDENT: return "IDENT";
        case CSS_TOKEN_FUNCTION: return "FUNCTION";
        case CSS_TOKEN_AT_KEYWORD: return "AT_KEYWORD";
        case CSS_TOKEN_HASH: return "HASH";
        case CSS_TOKEN_STRING: return "STRING";
        case CSS_TOKEN_URL: return "URL";
        case CSS_TOKEN_NUMBER: return "NUMBER";
        case CSS_TOKEN_DIMENSION: return "DIMENSION";
        case CSS_TOKEN_PERCENTAGE: return "PERCENTAGE";
        case CSS_TOKEN_UNICODE_RANGE: return "UNICODE_RANGE";
        default: return "UNKNOWN";
    }
}

// Character classification helpers
bool css_is_name_start_char(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

bool css_is_name_char(int c) {
    return css_is_name_start_char(c) || css_is_digit(c) || c == '-';
}

bool css_is_non_printable(int c) {
    return (c >= 0x0000 && c <= 0x0008) || c == 0x000B ||
           (c >= 0x000E && c <= 0x001F) || c == 0x007F;
}

bool css_is_newline(int c) {
    return c == '\n' || c == '\r' || c == '\f';
}

// Enhanced tokenizer implementation
CSSTokenizer* css_tokenizer_create(Pool* pool) {
    if (!pool) return NULL;

    CSSTokenizer* tokenizer = static_cast<CSSTokenizer*>(pool_alloc(pool, sizeof(CSSTokenizer)));
    if (!tokenizer) return NULL;

    tokenizer->pool = pool;
    tokenizer->input = NULL;
    tokenizer->length = 0;
    tokenizer->position = 0;
    tokenizer->line = 1;
    tokenizer->column = 1;
    tokenizer->supports_unicode = true;
    tokenizer->supports_css3 = true;

    return tokenizer;
}

void css_tokenizer_destroy(CSSTokenizer* tokenizer) {
    // Memory managed by pool, no explicit cleanup needed
    (void)tokenizer;
}

/**
 * Extract and store the token value as a null-terminated string
 * For STRING tokens, strips surrounding quotes
 */
// unescape CSS escape sequences in a string
// handles \XXXXXX (1-6 hex digits) and \X (single char escape)
// returns unescaped string allocated from pool
static char* css_unescape_string(const char* str, size_t len, Pool* pool) {
    if (!str || len == 0 || !pool) {
        char* empty = (char*)pool_alloc(pool, 1);
        if (empty) empty[0] = '\0';
        return empty;
    }

    // allocate buffer (unescaped string will be <= original length)
    char* result = (char*)pool_alloc(pool, len + 1);
    if (!result) return NULL;

    size_t out_pos = 0;
    size_t i = 0;

    while (i < len) {
        if (str[i] == '\\' && i + 1 < len) {
            i++; // skip backslash

            // check if next char is hex digit
            if ((str[i] >= '0' && str[i] <= '9') ||
                (str[i] >= 'a' && str[i] <= 'f') ||
                (str[i] >= 'A' && str[i] <= 'F')) {

                // parse hex escape: \XXXXXX (1-6 hex digits)
                unsigned int codepoint = 0;
                int hex_count = 0;
                while (hex_count < 6 && i < len) {
                    char c = str[i];
                    if (c >= '0' && c <= '9') {
                        codepoint = (codepoint << 4) | (c - '0');
                    } else if (c >= 'a' && c <= 'f') {
                        codepoint = (codepoint << 4) | (c - 'a' + 10);
                    } else if (c >= 'A' && c <= 'F') {
                        codepoint = (codepoint << 4) | (c - 'A' + 10);
                    } else {
                        break; // not a hex digit
                    }
                    i++;
                    hex_count++;
                }

                // skip optional whitespace after hex escape
                if (i < len && (str[i] == ' ' || str[i] == '\t' || str[i] == '\n' || str[i] == '\r')) {
                    i++;
                }

                // convert codepoint to UTF-8
                char utf8_buf[5];
                int utf8_len = codepoint_to_utf8(codepoint, utf8_buf);
                for (int j = 0; j < utf8_len; j++) {
                    result[out_pos++] = utf8_buf[j];
                }
            } else {
                // single character escape (e.g., \", \\, \n)
                // for CSS, backslash followed by non-hex char is just that char
                result[out_pos++] = str[i];
                i++;
            }
        } else {
            // regular character
            result[out_pos++] = str[i];
            i++;
        }
    }

    result[out_pos] = '\0';
    return result;
}

static void css_token_set_value(CssToken* token, Pool* pool) {
    if (!token || !pool || !token->start) {
        return;
    }

    // Handle zero-length tokens specially
    if (token->length == 0) {
        // Empty token - allocate empty string
        char* value = (char*)pool_alloc(pool, 1);
        if (value) {
            value[0] = '\0';
            token->value = value;
        }
        return;
    }

    // For STRING tokens, strip quotes and unescape
    if (token->type == CSS_TOKEN_STRING && token->length >= 2) {
        char quote = token->start[0];
        if ((quote == '\'' || quote == '"') && token->start[token->length - 1] == quote) {
            // Strip quotes: start+1, length-2
            size_t unquoted_len = token->length - 2;
            if (unquoted_len > 0) {
                // unescape CSS escape sequences
                token->value = css_unescape_string(token->start + 1, unquoted_len, pool);
                log_debug("[CSS UNESCAPE] Input: '%.*s' -> Output: '%s' (len=%zu)",
                    (int)unquoted_len, token->start + 1,
                    token->value ? token->value : "(null)",
                    token->value ? strlen(token->value) : 0);
            } else {
                char* value = (char*)pool_alloc(pool, 1);
                if (value) value[0] = '\0';
                token->value = value;
            }
            return;
        }
    }

    // For IDENT, HASH, AT_KEYWORD, CUSTOM_PROPERTY, FUNCTION tokens,
    // unescape CSS escape sequences if the token contains a backslash
    if (token->type == CSS_TOKEN_IDENT || token->type == CSS_TOKEN_HASH ||
        token->type == CSS_TOKEN_AT_KEYWORD || token->type == CSS_TOKEN_CUSTOM_PROPERTY ||
        token->type == CSS_TOKEN_FUNCTION) {
        if (memchr(token->start, '\\', token->length)) {
            token->value = css_unescape_string(token->start, token->length, pool);
            return;
        }
    }

    // Default: copy entire token value
    char* value = (char*)pool_alloc(pool, token->length + 1);
    if (value) {
        memcpy(value, token->start, token->length);
        value[token->length] = '\0';
        token->value = value;
    }
}

int css_tokenizer_tokenize(CSSTokenizer* tokenizer,
                                   const char* input, size_t length,
                                   CssToken** tokens) {
    if (!tokenizer || !input || !tokens) return 0;

    // Simple tokenizer implementation for testing
    // This is a basic implementation to break the recursion and allow tests to pass

    // Allocate token array (estimate maximum tokens)
    size_t max_tokens = length + 10; // Conservative estimate
    CssToken* token_array = static_cast<CssToken*>(pool_alloc(tokenizer->pool, sizeof(CssToken) * max_tokens));
    if (!token_array) {
        return 0;
    }

    size_t token_count = 0;
    size_t pos = 0;

    while (pos < length && token_count < max_tokens - 1) {
        // Skip leading whitespace and track it
        size_t ws_start = pos;
        while (pos < length && css_is_whitespace(input[pos])) {
            pos++;
        }

        // Create whitespace token if we found any
        if (pos > ws_start) {
            CssToken* token = &token_array[token_count];
            token->type = CSS_TOKEN_WHITESPACE;
            token->start = input + ws_start;
            token->length = pos - ws_start;
            token->value = NULL;
            css_token_set_value(token, tokenizer->pool);
            token_count++;
        }

        if (pos >= length) break;

        char ch = input[pos];
        CssToken* token = &token_array[token_count];
        token->start = input + pos;
        token->length = 1;
        token->value = NULL;
        token->value = NULL;

        // Basic character classification
        switch (ch) {
            case '{':
                token->type = CSS_TOKEN_LEFT_BRACE;
                pos++;
                break;
            case '}':
                token->type = CSS_TOKEN_RIGHT_BRACE;
                pos++;
                break;
            case '[':
                token->type = CSS_TOKEN_LEFT_BRACKET;
                pos++;
                break;
            case ']':
                token->type = CSS_TOKEN_RIGHT_BRACKET;
                pos++;
                break;
            case '(':
                token->type = CSS_TOKEN_LEFT_PAREN;
                pos++;
                break;
            case ')':
                token->type = CSS_TOKEN_RIGHT_PAREN;
                pos++;
                break;
            case ':':
                token->type = CSS_TOKEN_COLON;
                pos++;
                break;
            case ';':
                token->type = CSS_TOKEN_SEMICOLON;
                pos++;
                break;
            case ',':
                token->type = CSS_TOKEN_COMMA;
                pos++;
                break;
            case '"':
            case '\'': {
                // Simple string parsing
                char quote = ch;
                size_t start = pos;
                pos++; // Skip opening quote
                while (pos < length && input[pos] != quote) {
                    if (input[pos] == '\\' && pos + 1 < length) {
                        pos += 2; // Skip escaped character
                    } else {
                        pos++;
                    }
                }
                if (pos < length) pos++; // Skip closing quote
                token->type = CSS_TOKEN_STRING;
                token->length = pos - start;
                break;
            }
            case '#': {
                // Hash token
                size_t start = pos;
                pos++; // Skip #
                css_consume_name(input, length, &pos);
                token->type = CSS_TOKEN_HASH;
                token->length = pos - start;
                token->data.hash_type = CSS_HASH_ID; // Default to ID type
                break;
            }
            case '@': {
                // At-keyword
                size_t start = pos;
                pos++; // Skip @
                css_consume_name(input, length, &pos);
                token->type = CSS_TOKEN_AT_KEYWORD;
                token->length = pos - start;
                break;
            }
            case '^':
                if (pos + 1 < length && input[pos + 1] == '=') {
                    token->type = CSS_TOKEN_PREFIX_MATCH;
                    token->length = 2;
                    pos += 2;
                } else {
                    token->type = CSS_TOKEN_DELIM;
                    token->data.delimiter = ch;
                    pos++;
                }
                break;
            case '$':
                if (pos + 1 < length && input[pos + 1] == '=') {
                    token->type = CSS_TOKEN_SUFFIX_MATCH;
                    token->length = 2;
                    pos += 2;
                } else {
                    token->type = CSS_TOKEN_DELIM;
                    token->data.delimiter = ch;
                    pos++;
                }
                break;
            case '*':
                if (pos + 1 < length && input[pos + 1] == '=') {
                    token->type = CSS_TOKEN_SUBSTRING_MATCH;
                    token->length = 2;
                    pos += 2;
                } else {
                    token->type = CSS_TOKEN_DELIM;
                    token->data.delimiter = ch;
                    pos++;
                }
                break;
            case '~':
                if (pos + 1 < length && input[pos + 1] == '=') {
                    token->type = CSS_TOKEN_INCLUDE_MATCH;
                    token->length = 2;
                    pos += 2;
                } else {
                    token->type = CSS_TOKEN_DELIM;
                    token->data.delimiter = ch;
                    pos++;
                }
                break;
            case '|':
                if (pos + 1 < length && input[pos + 1] == '=') {
                    token->type = CSS_TOKEN_DASH_MATCH;
                    token->length = 2;
                    pos += 2;
                } else if (pos + 1 < length && input[pos + 1] == '|') {
                    token->type = CSS_TOKEN_COLUMN;
                    token->length = 2;
                    pos += 2;
                } else {
                    token->type = CSS_TOKEN_DELIM;
                    token->data.delimiter = ch;
                    pos++;
                }
                break;
            case '/':
                if (pos + 1 < length && input[pos + 1] == '*') {
                    // Comment
                    size_t start = pos;
                    pos += 2; // Skip /*
                    while (pos + 1 < length && !(input[pos] == '*' && input[pos + 1] == '/')) {
                        pos++;
                    }
                    if (pos + 1 < length) pos += 2; // Skip */
                    token->type = CSS_TOKEN_COMMENT;
                    token->length = pos - start;
                } else {
                    token->type = CSS_TOKEN_DELIM;
                    token->data.delimiter = ch;
                    pos++;
                }
                break;
            case '+':
                // Check if this is a signed number
                if (pos + 1 < length && (isdigit(input[pos + 1]) || input[pos + 1] == '.')) {
                    size_t start = pos;
                    pos++; // Skip sign
                    tokenize_number(input, length, start, &pos, token, tokenizer->pool);
                } else {
                    token->type = CSS_TOKEN_DELIM;
                    token->data.delimiter = ch;
                    pos++;
                }
                break;
            case '-':
                // Check for CSS custom property (--name)
                if (pos + 1 < length && input[pos + 1] == '-') {
                    // CSS Custom Property: starts with --
                    size_t start = pos;
                    pos += 2; // Skip --

                    // Parse the rest of the name (can include letters, digits, hyphens, underscores)
                    css_consume_name(input, length, &pos);

                    token->type = CSS_TOKEN_CUSTOM_PROPERTY;
                    token->length = pos - start;

                    // Store the property name (without --) in the token value
                    // This will be set by css_token_set_value
                }
                // Check if this is a signed number
                else if (pos + 1 < length && (isdigit(input[pos + 1]) || input[pos + 1] == '.')) {
                    size_t start = pos;
                    pos++; // Skip sign
                    tokenize_number(input, length, start, &pos, token, tokenizer->pool);
                }
                // Check for custom property (--foo) or identifier starting with - (e.g., -webkit-transform)
                else if (pos + 1 < length && (isalpha(input[pos + 1]) || input[pos + 1] == '_' || input[pos + 1] == '-'
                          || css_starts_escape(input, length, pos + 1))) {
                    // Identifier starting with - or -- (custom property)
                    size_t start = pos;
                    css_consume_name(input, length, &pos);

                    // Check for function
                    if (pos < length && input[pos] == '(') {
                        token->type = CSS_TOKEN_FUNCTION;
                        pos++; // Include the opening parenthesis
                    } else {
                        token->type = CSS_TOKEN_IDENT;
                    }
                    token->length = pos - start;
                } else {
                    token->type = CSS_TOKEN_DELIM;
                    token->data.delimiter = ch;
                    pos++;
                }
                break;
            default:
                if (isdigit(ch)) {
                    // Number or dimension starting with digit
                    size_t start = pos;
                    tokenize_number(input, length, start, &pos, token, tokenizer->pool);
                } else if (ch == '.' && pos + 1 < length && isdigit(input[pos + 1])) {
                    // Decimal number starting with . (e.g., .5)
                    size_t start = pos;
                    pos++; // Skip the '.'
                    tokenize_number(input, length, start, &pos, token, tokenizer->pool);
                } else if (isalpha(ch) || ch == '_' || (ch == '-' && pos + 1 < length && isalpha(input[pos + 1]))
                           || css_starts_escape(input, length, pos)) {
                    // Identifier or function
                    size_t start = pos;
                    css_consume_name(input, length, &pos);

                    // Check for function
                    if (pos < length && input[pos] == '(') {
                        token->type = CSS_TOKEN_FUNCTION;
                        pos++; // Include the opening parenthesis
                    } else {
                        token->type = CSS_TOKEN_IDENT;
                    }
                    token->length = pos - start;
                } else if ((unsigned char)ch >= 0x80) {
                    // Potential UTF-8 multi-byte character - check if it's a valid name start
                    UnicodeChar uc = css_parse_unicode_char(input + pos, length - pos);
                    if (uc.byte_length > 0 && css_is_name_start_char_unicode(uc.codepoint)) {
                        // Valid UTF-8 identifier start
                        size_t start = pos;
                        pos += uc.byte_length;

                        // Continue parsing identifier with UTF-8 support
                        while (pos < length) {
                            if (isalnum(input[pos]) || input[pos] == '-' || input[pos] == '_') {
                                pos++;
                            } else if (css_starts_escape(input, length, pos)) {
                                css_consume_escape_seq(input, length, &pos);
                            } else if ((unsigned char)input[pos] >= 0x80) {
                                UnicodeChar next = css_parse_unicode_char(input + pos, length - pos);
                                if (next.byte_length > 0 && css_is_name_char_unicode(next.codepoint)) {
                                    pos += next.byte_length;
                                } else {
                                    break;
                                }
                            } else {
                                break;
                            }
                        }

                        // Check for function
                        if (pos < length && input[pos] == '(') {
                            token->type = CSS_TOKEN_FUNCTION;
                            pos++;
                        } else {
                            token->type = CSS_TOKEN_IDENT;
                        }
                        token->length = pos - start;
                    } else {
                        // Invalid UTF-8 or not a name character - treat as delimiter
                        token->type = CSS_TOKEN_DELIM;
                        token->data.delimiter = ch;
                        pos++;
                    }
                } else {
                    // Delimiter
                    token->type = CSS_TOKEN_DELIM;
                    token->data.delimiter = ch;
                    pos++;
                }
                break;
        }

        // Set token value and increment count
        css_token_set_value(token, tokenizer->pool);
        token_count++;
    }

    // Add EOF token
    if (token_count < max_tokens) {
        CssToken* eof_token = &token_array[token_count];
        eof_token->type = CSS_TOKEN_EOF;
        eof_token->start = input + length;
        eof_token->length = 0;
        eof_token->value = "";  // Empty string instead of NULL
        token_count++;
    }

    *tokens = token_array;
    return (int)token_count;
}

bool css_is_whitespace(int c) {
    return c == ' ' || c == '\t' || css_is_newline(c);
}

bool css_is_digit(int c) {
    return c >= '0' && c <= '9';
}

bool css_is_hex_digit(int c) {
    return css_is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// ============================================================================
// Token Utility Functions (for test support)
// ============================================================================

const char* css_token_type_to_string(CssTokenType type) {
    switch (type) {
        case CSS_TOKEN_IDENT: return "IDENT";
        case CSS_TOKEN_FUNCTION: return "FUNCTION";
        case CSS_TOKEN_AT_KEYWORD: return "AT_KEYWORD";
        case CSS_TOKEN_HASH: return "HASH";
        case CSS_TOKEN_STRING: return "STRING";
        case CSS_TOKEN_URL: return "URL";
        case CSS_TOKEN_NUMBER: return "NUMBER";
        case CSS_TOKEN_DIMENSION: return "DIMENSION";
        case CSS_TOKEN_PERCENTAGE: return "PERCENTAGE";
        case CSS_TOKEN_UNICODE_RANGE: return "UNICODE_RANGE";
        case CSS_TOKEN_INCLUDE_MATCH: return "INCLUDE_MATCH";
        case CSS_TOKEN_DASH_MATCH: return "DASH_MATCH";
        case CSS_TOKEN_PREFIX_MATCH: return "PREFIX_MATCH";
        case CSS_TOKEN_SUFFIX_MATCH: return "SUFFIX_MATCH";
        case CSS_TOKEN_SUBSTRING_MATCH: return "SUBSTRING_MATCH";
        case CSS_TOKEN_COLUMN: return "COLUMN";
        case CSS_TOKEN_WHITESPACE: return "WHITESPACE";
        case CSS_TOKEN_COMMENT: return "COMMENT";
        case CSS_TOKEN_COLON: return "COLON";
        case CSS_TOKEN_SEMICOLON: return "SEMICOLON";
        case CSS_TOKEN_LEFT_PAREN: return "LEFT_PAREN";
        case CSS_TOKEN_RIGHT_PAREN: return "RIGHT_PAREN";
        case CSS_TOKEN_LEFT_BRACE: return "LEFT_BRACE";
        case CSS_TOKEN_RIGHT_BRACE: return "RIGHT_BRACE";
        case CSS_TOKEN_LEFT_BRACKET: return "LEFT_BRACKET";
        case CSS_TOKEN_RIGHT_BRACKET: return "RIGHT_BRACKET";
        case CSS_TOKEN_COMMA: return "COMMA";
        case CSS_TOKEN_DELIM: return "DELIM";
        case CSS_TOKEN_EOF: return "EOF";
        case CSS_TOKEN_BAD_STRING: return "BAD_STRING";
        case CSS_TOKEN_BAD_URL: return "BAD_URL";
        case CSS_TOKEN_IDENTIFIER: return "IDENTIFIER";
        case CSS_TOKEN_MATCH: return "MATCH";
        case CSS_TOKEN_CDO: return "CDO";
        case CSS_TOKEN_CDC: return "CDC";
        case CSS_TOKEN_CUSTOM_PROPERTY: return "CUSTOM_PROPERTY";
        case CSS_TOKEN_CALC_FUNCTION: return "CALC_FUNCTION";
        case CSS_TOKEN_VAR_FUNCTION: return "VAR_FUNCTION";
        case CSS_TOKEN_ENV_FUNCTION: return "ENV_FUNCTION";
        case CSS_TOKEN_ATTR_FUNCTION: return "ATTR_FUNCTION";
        case CSS_TOKEN_SUPPORTS_SELECTOR: return "SUPPORTS_SELECTOR";
        case CSS_TOKEN_LAYER_NAME: return "LAYER_NAME";
        case CSS_TOKEN_CONTAINER_NAME: return "CONTAINER_NAME";
        case CSS_TOKEN_SCOPE_SELECTOR: return "SCOPE_SELECTOR";
        case CSS_TOKEN_NESTING_SELECTOR: return "NESTING_SELECTOR";
        case CSS_TOKEN_COLOR_FUNCTION: return "COLOR_FUNCTION";
        case CSS_TOKEN_ANGLE_FUNCTION: return "ANGLE_FUNCTION";
        case CSS_TOKEN_TIME_FUNCTION: return "TIME_FUNCTION";
        case CSS_TOKEN_FREQUENCY_FUNCTION: return "FREQUENCY_FUNCTION";
        case CSS_TOKEN_RESOLUTION_FUNCTION: return "RESOLUTION_FUNCTION";
        default: return "UNKNOWN";
    }
}

bool css_token_is_whitespace(const CssToken* token) {
    return token && token->type == CSS_TOKEN_WHITESPACE;
}

bool css_token_is_comment(const CssToken* token) {
    return token && token->type == CSS_TOKEN_COMMENT;
}

bool css_token_equals_string(const CssToken* token, const char* str) {
    if (!token || !str) return false;
    size_t str_len = strlen(str);
    return token->length == str_len && strncmp(token->start, str, str_len) == 0;
}

char* css_token_to_string(const CssToken* token, Pool* pool) {
    if (!token || !pool) return NULL;

    char* result = static_cast<char*>(pool_alloc(pool, token->length + 1));
    if (!result) return NULL;

    strncpy(result, token->start, token->length);
    result[token->length] = '\0';
    return result;
}

// ============================================================================
// Token Stream Functions (for parser support)
// ============================================================================

CssTokenStream* css_token_stream_create(CssToken* tokens, size_t length, Pool* pool) {
    if (!tokens || !pool) return NULL;

    CssTokenStream* stream = static_cast<CssTokenStream*>(pool_alloc(pool, sizeof(CssTokenStream)));
    if (!stream) return NULL;

    stream->tokens = tokens;
    stream->length = length;
    stream->current = 0;
    stream->pool = pool;

    return stream;
}

CssToken* css_token_stream_current(CssTokenStream* stream) {
    if (!stream || stream->current >= stream->length) return NULL;
    return &stream->tokens[stream->current];
}

bool css_token_stream_advance(CssTokenStream* stream) {
    if (!stream || stream->current >= stream->length) return false;
    stream->current++;
    return stream->current <= stream->length;
}

CssToken* css_token_stream_peek(CssTokenStream* stream, size_t offset) {
    if (!stream) return NULL;
    size_t peek_pos = stream->current + offset;
    if (peek_pos >= stream->length) return NULL;
    return &stream->tokens[peek_pos];
}

bool css_token_stream_consume(CssTokenStream* stream, CssTokenType expected_type) {
    CssToken* current = css_token_stream_current(stream);
    if (!current || current->type != expected_type) return false;
    return css_token_stream_advance(stream);
}

bool css_token_stream_at_end(CssTokenStream* stream) {
    if (!stream) return true;
    if (stream->current >= stream->length) return true;
    CssToken* current = css_token_stream_current(stream);
    return current && current->type == CSS_TOKEN_EOF;
}
