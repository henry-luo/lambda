#include "css_tokenizer_enhanced.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

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
static CSSFunctionInfo css_function_database[] = {
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

CSSFunctionInfo* css_get_function_info(const char* function_name) {
    if (!function_name) return NULL;
    
    for (CSSFunctionInfo* info = css_function_database; info->name; info++) {
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
CSSColorTypeEnhanced css_detect_color_type(const char* color_str) {
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
        return CSS_COLOR_CURRENT;
    }
    
    return CSS_COLOR_KEYWORD;
}

// String conversion utilities
const char* css_token_type_enhanced_to_str(CSSTokenTypeEnhanced type) {
    switch (type) {
        case CSS_TOKEN_ENHANCED_CUSTOM_PROPERTY: return "CUSTOM_PROPERTY";
        case CSS_TOKEN_ENHANCED_CALC_FUNCTION: return "CALC_FUNCTION";
        case CSS_TOKEN_ENHANCED_VAR_FUNCTION: return "VAR_FUNCTION";
        case CSS_TOKEN_ENHANCED_ENV_FUNCTION: return "ENV_FUNCTION";
        case CSS_TOKEN_ENHANCED_ATTR_FUNCTION: return "ATTR_FUNCTION";
        case CSS_TOKEN_ENHANCED_COLOR_FUNCTION: return "COLOR_FUNCTION";
        case CSS_TOKEN_ENHANCED_NESTING_SELECTOR: return "NESTING_SELECTOR";
        case CSS_TOKEN_ENHANCED_CDO: return "CDO";
        case CSS_TOKEN_ENHANCED_CDC: return "CDC";
        case CSS_TOKEN_ENHANCED_BAD_STRING: return "BAD_STRING";
        case CSS_TOKEN_ENHANCED_BAD_URL: return "BAD_URL";
        default:
            // Fall back to original tokenizer for basic types
            return css_token_type_to_str((CSSTokenType)type);
    }
}

const char* css_unit_type_to_str(CSSUnitTypeEnhanced unit) {
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

const char* css_color_type_to_str(CSSColorTypeEnhanced type) {
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
        case CSS_COLOR_CURRENT: return "current";
        case CSS_COLOR_SYSTEM: return "system";
        default: return "unknown";
    }
}

// Error recovery functions
bool css_token_is_recoverable_error(CSSTokenEnhanced* token) {
    if (!token) return false;
    
    return token->type == CSS_TOKEN_ENHANCED_BAD_STRING ||
           token->type == CSS_TOKEN_ENHANCED_BAD_URL;
}

void css_token_fix_common_errors(CSSTokenEnhanced* token, Pool* pool) {
    if (!token || !pool) return;
    
    if (token->type == CSS_TOKEN_ENHANCED_BAD_STRING) {
        // Try to close unclosed string
        size_t len = token->length;
        char* fixed_value = (char*)pool_alloc(pool, len + 2);
        if (fixed_value) {
            memcpy(fixed_value, token->value, len);
            fixed_value[len] = '"'; // Add closing quote
            fixed_value[len + 1] = '\0';
            token->value = fixed_value;
            token->type = CSS_TOKEN_ENHANCED_STRING;
        }
    } else if (token->type == CSS_TOKEN_ENHANCED_BAD_URL) {
        // Try to close unclosed URL
        size_t len = token->length;
        char* fixed_value = (char*)pool_alloc(pool, len + 2);
        if (fixed_value) {
            memcpy(fixed_value, token->value, len);
            fixed_value[len] = ')'; // Add closing paren
            fixed_value[len + 1] = '\0';
            token->value = fixed_value;
            token->type = CSS_TOKEN_ENHANCED_URL;
        }
    }
}