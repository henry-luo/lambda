#include "css_tokenizer.hpp"
#include <stdlib.h>
#include <string.h>
#include "../../../lib/str.h"
#include <ctype.h>
#include <assert.h>

// Helper function to parse CSS unit from string
static CssUnit parse_css_unit(const char* unit_str, size_t length) {
    if (!unit_str || length == 0) return CSS_UNIT_NONE;

    // Create null-terminated string for comparison
    char unit[16];
    if (length >= sizeof(unit)) return CSS_UNIT_NONE;
    strncpy(unit, unit_str, length);
    unit[length] = '\0';

    // Absolute units
    if (strcmp(unit, "px") == 0) return CSS_UNIT_PX;
    if (strcmp(unit, "cm") == 0) return CSS_UNIT_CM;
    if (strcmp(unit, "mm") == 0) return CSS_UNIT_MM;
    if (strcmp(unit, "in") == 0) return CSS_UNIT_IN;
    if (strcmp(unit, "pt") == 0) return CSS_UNIT_PT;
    if (strcmp(unit, "pc") == 0) return CSS_UNIT_PC;
    if (strcmp(unit, "q") == 0) return CSS_UNIT_Q;

    // Font-relative units
    if (strcmp(unit, "em") == 0) return CSS_UNIT_EM;
    if (strcmp(unit, "ex") == 0) return CSS_UNIT_EX;
    if (strcmp(unit, "cap") == 0) return CSS_UNIT_CAP;
    if (strcmp(unit, "ch") == 0) return CSS_UNIT_CH;
    if (strcmp(unit, "ic") == 0) return CSS_UNIT_IC;
    if (strcmp(unit, "rem") == 0) return CSS_UNIT_REM;
    if (strcmp(unit, "lh") == 0) return CSS_UNIT_LH;
    if (strcmp(unit, "rlh") == 0) return CSS_UNIT_RLH;

    // Viewport units
    if (strcmp(unit, "vw") == 0) return CSS_UNIT_VW;
    if (strcmp(unit, "vh") == 0) return CSS_UNIT_VH;
    if (strcmp(unit, "vi") == 0) return CSS_UNIT_VI;
    if (strcmp(unit, "vb") == 0) return CSS_UNIT_VB;
    if (strcmp(unit, "vmin") == 0) return CSS_UNIT_VMIN;
    if (strcmp(unit, "vmax") == 0) return CSS_UNIT_VMAX;

    // Grid fractional units
    if (strcmp(unit, "fr") == 0) return CSS_UNIT_FR;

    // Angle units
    if (strcmp(unit, "deg") == 0) return CSS_UNIT_DEG;
    if (strcmp(unit, "rad") == 0) return CSS_UNIT_RAD;
    if (strcmp(unit, "grad") == 0) return CSS_UNIT_GRAD;
    if (strcmp(unit, "turn") == 0) return CSS_UNIT_TURN;

    // Time units
    if (strcmp(unit, "s") == 0) return CSS_UNIT_S;
    if (strcmp(unit, "ms") == 0) return CSS_UNIT_MS;

    // Frequency units
    if (strcmp(unit, "hz") == 0) return CSS_UNIT_HZ;
    if (strcmp(unit, "khz") == 0) return CSS_UNIT_KHZ;

    // Resolution units
    if (strcmp(unit, "dpi") == 0) return CSS_UNIT_DPI;
    if (strcmp(unit, "dpcm") == 0) return CSS_UNIT_DPCM;
    if (strcmp(unit, "dppx") == 0) return CSS_UNIT_DPPX;

    return CSS_UNIT_NONE;
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
            if (codepoint < 0x80) {
                result[result_pos++] = (char)codepoint;
            } else if (codepoint < 0x800) {
                result[result_pos++] = (char)(0xC0 | (codepoint >> 6));
                result[result_pos++] = (char)(0x80 | (codepoint & 0x3F));
            } else if (codepoint < 0x10000) {
                result[result_pos++] = (char)(0xE0 | (codepoint >> 12));
                result[result_pos++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                result[result_pos++] = (char)(0x80 | (codepoint & 0x3F));
            } else if (codepoint < 0x110000) {
                result[result_pos++] = (char)(0xF0 | (codepoint >> 18));
                result[result_pos++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
                result[result_pos++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                result[result_pos++] = (char)(0x80 | (codepoint & 0x3F));
            }
        } else {
            result[result_pos++] = *p++;
        }
    }

    result[result_pos] = '\0';
    return result;
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

const char* css_unit_type_to_str(CssUnit unit) {
    switch (unit) {
        case CSS_UNIT_PX: return "px";
        case CSS_UNIT_EM: return "em";
        case CSS_UNIT_REM: return "rem";
        case CSS_UNIT_EX: return "ex";
        case CSS_UNIT_CH: return "ch";
        case CSS_UNIT_VW: return "vw";
        case CSS_UNIT_VH: return "vh";
        case CSS_UNIT_VMIN: return "vmin";
        case CSS_UNIT_VMAX: return "vmax";
        case CSS_UNIT_CM: return "cm";
        case CSS_UNIT_MM: return "mm";
        case CSS_UNIT_IN: return "in";
        case CSS_UNIT_PT: return "pt";
        case CSS_UNIT_PC: return "pc";
        case CSS_UNIT_Q: return "q";
        case CSS_UNIT_LH: return "lh";
        case CSS_UNIT_RLH: return "rlh";
        case CSS_UNIT_VI: return "vi";
        case CSS_UNIT_VB: return "vb";
        case CSS_UNIT_SVW: return "svw";
        case CSS_UNIT_SVH: return "svh";
        case CSS_UNIT_LVW: return "lvw";
        case CSS_UNIT_LVH: return "lvh";
        case CSS_UNIT_DVW: return "dvw";
        case CSS_UNIT_DVH: return "dvh";
        case CSS_UNIT_DEG: return "deg";
        case CSS_UNIT_GRAD: return "grad";
        case CSS_UNIT_RAD: return "rad";
        case CSS_UNIT_TURN: return "turn";
        case CSS_UNIT_S: return "s";
        case CSS_UNIT_MS: return "ms";
        case CSS_UNIT_HZ: return "hz";
        case CSS_UNIT_KHZ: return "khz";
        case CSS_UNIT_DPI: return "dpi";
        case CSS_UNIT_DPCM: return "dpcm";
        case CSS_UNIT_DPPX: return "dppx";
        case CSS_UNIT_FR: return "fr";
        case CSS_UNIT_PERCENT: return "%";
        case CSS_UNIT_NONE: return "";
        default: return "unknown";
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
                if (codepoint <= 0x7F) {
                    result[out_pos++] = (char)codepoint;
                } else if (codepoint <= 0x7FF) {
                    result[out_pos++] = (char)(0xC0 | (codepoint >> 6));
                    result[out_pos++] = (char)(0x80 | (codepoint & 0x3F));
                } else if (codepoint <= 0xFFFF) {
                    result[out_pos++] = (char)(0xE0 | (codepoint >> 12));
                    result[out_pos++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                    result[out_pos++] = (char)(0x80 | (codepoint & 0x3F));
                } else if (codepoint <= 0x10FFFF) {
                    result[out_pos++] = (char)(0xF0 | (codepoint >> 18));
                    result[out_pos++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
                    result[out_pos++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                    result[out_pos++] = (char)(0x80 | (codepoint & 0x3F));
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
                while (pos < length && (isalnum(input[pos]) || input[pos] == '-' || input[pos] == '_')) {
                    pos++;
                }
                token->type = CSS_TOKEN_HASH;
                token->length = pos - start;
                token->data.hash_type = CSS_HASH_ID; // Default to ID type
                break;
            }
            case '@': {
                // At-keyword
                size_t start = pos;
                pos++; // Skip @
                while (pos < length && (isalnum(input[pos]) || input[pos] == '-' || input[pos] == '_')) {
                    pos++;
                }
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
                    // Number parsing (same as digit case)
                    size_t start = pos;
                    pos++; // Skip sign

                    // Parse integer part
                    while (pos < length && isdigit(input[pos])) {
                        pos++;
                    }

                    // Parse decimal part
                    if (pos < length && input[pos] == '.') {
                        pos++;
                        while (pos < length && isdigit(input[pos])) {
                            pos++;
                        }
                    }

                    // Check for dimension unit or percentage
                    size_t number_end = pos;
                    if (pos < length && input[pos] == '%') {
                        pos++;
                        token->type = CSS_TOKEN_PERCENTAGE;
                    } else if (pos < length && (isalpha(input[pos]) || input[pos] == '_')) {
                        // Parse unit
                        size_t unit_start = pos;
                        while (pos < length && (isalnum(input[pos]) || input[pos] == '_' || input[pos] == '-')) {
                            pos++;
                        }
                        token->type = CSS_TOKEN_DIMENSION;
                        // Parse the unit string and convert to CssUnit enum
                        CssUnit parsed_unit = parse_css_unit(&input[unit_start], pos - unit_start);
                        token->data.dimension.unit = parsed_unit;
                    } else {
                        token->type = CSS_TOKEN_NUMBER;
                    }

                    token->length = pos - start;

                    // Parse number value
                    char* num_str = static_cast<char*>(pool_alloc(tokenizer->pool, number_end - start + 1));
                    if (num_str) {
                        strncpy(num_str, token->start, number_end - start);
                        num_str[number_end - start] = '\0';
                        if (token->type == CSS_TOKEN_DIMENSION) {
                            token->data.dimension.value = str_to_double_default(num_str, number_end - start, 0.0);
                        } else {
                            token->data.number_value = str_to_double_default(num_str, number_end - start, 0.0);
                        }
                    }
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
                    while (pos < length && (isalnum(input[pos]) || input[pos] == '-' || input[pos] == '_')) {
                        pos++;
                    }

                    token->type = CSS_TOKEN_CUSTOM_PROPERTY;
                    token->length = pos - start;

                    // Store the property name (without --) in the token value
                    // This will be set by css_token_set_value
                }
                // Check if this is a signed number
                else if (pos + 1 < length && (isdigit(input[pos + 1]) || input[pos + 1] == '.')) {
                    // Number parsing (same as digit case)
                    size_t start = pos;
                    pos++; // Skip sign

                    // Parse integer part
                    while (pos < length && isdigit(input[pos])) {
                        pos++;
                    }

                    // Parse decimal part
                    if (pos < length && input[pos] == '.') {
                        pos++;
                        while (pos < length && isdigit(input[pos])) {
                            pos++;
                        }
                    }

                    // Check for dimension unit or percentage
                    size_t number_end = pos;
                    if (pos < length && input[pos] == '%') {
                        pos++;
                        token->type = CSS_TOKEN_PERCENTAGE;
                    } else if (pos < length && (isalpha(input[pos]) || input[pos] == '_')) {
                        // Parse unit
                        size_t unit_start = pos;
                        while (pos < length && (isalnum(input[pos]) || input[pos] == '_' || input[pos] == '-')) {
                            pos++;
                        }
                        token->type = CSS_TOKEN_DIMENSION;
                        // Parse the unit string and convert to CssUnit enum
                        CssUnit parsed_unit = parse_css_unit(&input[unit_start], pos - unit_start);
                        token->data.dimension.unit = parsed_unit;
                    } else {
                        token->type = CSS_TOKEN_NUMBER;
                    }

                    token->length = pos - start;

                    // Parse number value
                    char* num_str = static_cast<char*>(pool_alloc(tokenizer->pool, number_end - start + 1));
                    if (num_str) {
                        strncpy(num_str, token->start, number_end - start);
                        num_str[number_end - start] = '\0';
                        if (token->type == CSS_TOKEN_DIMENSION) {
                            token->data.dimension.value = str_to_double_default(num_str, number_end - start, 0.0);
                        } else {
                            token->data.number_value = str_to_double_default(num_str, number_end - start, 0.0);
                        }
                    }
                }
                // Check for custom property (--foo) or identifier starting with - (e.g., -webkit-transform)
                else if (pos + 1 < length && (isalpha(input[pos + 1]) || input[pos + 1] == '_' || input[pos + 1] == '-')) {
                    // Identifier starting with - or -- (custom property)
                    size_t start = pos;
                    while (pos < length && (isalnum(input[pos]) || input[pos] == '-' || input[pos] == '_')) {
                        pos++;
                    }

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

                    // Parse integer part
                    while (pos < length && isdigit(input[pos])) {
                        pos++;
                    }

                    // Parse decimal part
                    if (pos < length && input[pos] == '.') {
                        pos++;
                        while (pos < length && isdigit(input[pos])) {
                            pos++;
                        }
                    }

                    // Check for dimension unit or percentage
                    size_t number_end = pos;
                    if (pos < length && input[pos] == '%') {
                        pos++;
                        token->type = CSS_TOKEN_PERCENTAGE;
                    } else if (pos < length && (isalpha(input[pos]) || input[pos] == '_')) {
                        // Parse unit
                        size_t unit_start = pos;
                        while (pos < length && (isalnum(input[pos]) || input[pos] == '_' || input[pos] == '-')) {
                            pos++;
                        }
                        token->type = CSS_TOKEN_DIMENSION;
                        // Parse the unit string and convert to CssUnit enum
                        CssUnit parsed_unit = parse_css_unit(&input[unit_start], pos - unit_start);
                        token->data.dimension.unit = parsed_unit;
                    } else {
                        token->type = CSS_TOKEN_NUMBER;
                    }

                    token->length = pos - start;

                    // Parse number value
                    char* num_str = static_cast<char*>(pool_alloc(tokenizer->pool, number_end - start + 1));
                    if (num_str) {
                        strncpy(num_str, token->start, number_end - start);
                        num_str[number_end - start] = '\0';
                        if (token->type == CSS_TOKEN_DIMENSION) {
                            token->data.dimension.value = str_to_double_default(num_str, number_end - start, 0.0);
                        } else {
                            token->data.number_value = str_to_double_default(num_str, number_end - start, 0.0);
                        }
                    }
                } else if (ch == '.' && pos + 1 < length && isdigit(input[pos + 1])) {
                    // Decimal number starting with . (e.g., .5)
                    size_t start = pos;
                    pos++; // Skip the '.'

                    // Parse decimal digits
                    while (pos < length && isdigit(input[pos])) {
                        pos++;
                    }

                    // Check for dimension unit or percentage
                    size_t number_end = pos;
                    if (pos < length && input[pos] == '%') {
                        pos++;
                        token->type = CSS_TOKEN_PERCENTAGE;
                    } else if (pos < length && (isalpha(input[pos]) || input[pos] == '_')) {
                        // Parse unit
                        size_t unit_start = pos;
                        while (pos < length && (isalnum(input[pos]) || input[pos] == '_' || input[pos] == '-')) {
                            pos++;
                        }
                        token->type = CSS_TOKEN_DIMENSION;
                        // Parse the unit string and convert to CssUnit enum
                        CssUnit parsed_unit = parse_css_unit(&input[unit_start], pos - unit_start);
                        token->data.dimension.unit = parsed_unit;
                    } else {
                        token->type = CSS_TOKEN_NUMBER;
                    }

                    token->length = pos - start;

                    // Parse number value
                    char* num_str = static_cast<char*>(pool_alloc(tokenizer->pool, number_end - start + 1));
                    if (num_str) {
                        strncpy(num_str, token->start, number_end - start);
                        num_str[number_end - start] = '\0';
                        if (token->type == CSS_TOKEN_DIMENSION) {
                            token->data.dimension.value = str_to_double_default(num_str, number_end - start, 0.0);
                        } else {
                            token->data.number_value = str_to_double_default(num_str, number_end - start, 0.0);
                        }
                    }
                } else if (isalpha(ch) || ch == '_' || (ch == '-' && pos + 1 < length && isalpha(input[pos + 1]))) {
                    // Identifier or function - check for ASCII start
                    size_t start = pos;
                    while (pos < length && (isalnum(input[pos]) || input[pos] == '-' || input[pos] == '_')) {
                        pos++;
                    }

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
