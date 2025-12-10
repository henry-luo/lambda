/**
 * CSS @font-face Rule Parser Implementation
 *
 * Parses @font-face rules from CSS and extracts font descriptors.
 */

#include "css_font_face.hpp"
#include "../../../lib/log.h"
#include <string.h>
#include <stdlib.h>

// Helper: trim whitespace and quotes from a string
static char* trim_and_unquote(const char* str, size_t len, Pool* pool) {
    if (!str || len == 0) return nullptr;

    // Skip leading whitespace
    while (len > 0 && (*str == ' ' || *str == '\t' || *str == '\n')) {
        str++;
        len--;
    }

    // Skip trailing whitespace
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' || str[len-1] == '\n')) {
        len--;
    }

    // Remove quotes if present
    if (len >= 2 && ((str[0] == '"' && str[len-1] == '"') ||
                     (str[0] == '\'' && str[len-1] == '\''))) {
        str++;
        len -= 2;
    }

    // Allocate and copy result
    char* result;
    if (pool) {
        result = (char*)pool_alloc(pool, len + 1);
    } else {
        result = (char*)malloc(len + 1);
    }
    if (result) {
        memcpy(result, str, len);
        result[len] = '\0';
    }
    return result;
}

// Helper: extract URL from "url( path )" format
static char* extract_url_value(const char* src_value, Pool* pool) {
    if (!src_value) return nullptr;

    // Find "url("
    const char* url_start = strstr(src_value, "url(");
    if (!url_start) {
        // Try without parentheses - plain path
        return trim_and_unquote(src_value, strlen(src_value), pool);
    }

    url_start += 4; // skip "url("

    // Skip whitespace after "url("
    while (*url_start == ' ' || *url_start == '\t') url_start++;

    // Skip opening quote if present
    char quote_char = 0;
    if (*url_start == '"' || *url_start == '\'') {
        quote_char = *url_start++;
    }

    // Find end of URL
    const char* url_end = url_start;
    if (quote_char) {
        // Find closing quote
        while (*url_end && *url_end != quote_char) url_end++;
    } else {
        // Find closing paren or space (before format())
        while (*url_end && *url_end != ')' && *url_end != ' ') url_end++;
    }

    size_t len = url_end - url_start;
    char* result;
    if (pool) {
        result = (char*)pool_alloc(pool, len + 1);
    } else {
        result = (char*)malloc(len + 1);
    }
    if (result) {
        memcpy(result, url_start, len);
        result[len] = '\0';
    }
    return result;
}

char* css_resolve_font_url(const char* url, const char* base_path, Pool* pool) {
    if (!url) return nullptr;

    // If URL is absolute, return as-is
    if (url[0] == '/') {
        if (pool) {
            return pool_strdup(pool, url);
        } else {
            return strdup(url);
        }
    }

    // If no base path, return URL as-is
    if (!base_path) {
        if (pool) {
            return pool_strdup(pool, url);
        } else {
            return strdup(url);
        }
    }

    // Find directory of base_path
    const char* last_slash = strrchr(base_path, '/');
    size_t base_dir_len = last_slash ? (last_slash - base_path + 1) : 0;

    // Build result buffer
    size_t result_size = base_dir_len + strlen(url) + 1;
    char* result;
    if (pool) {
        result = (char*)pool_alloc(pool, result_size);
    } else {
        result = (char*)malloc(result_size);
    }
    if (!result) return nullptr;

    // Copy base directory
    if (base_dir_len > 0) {
        memcpy(result, base_path, base_dir_len);
    }
    result[base_dir_len] = '\0';

    // Resolve relative path components (../)
    const char* rel = url;
    char* write_pos = result + base_dir_len;

    while (strncmp(rel, "../", 3) == 0) {
        rel += 3;
        // Go up one directory in result
        if (write_pos > result) {
            write_pos--; // back over trailing slash
            while (write_pos > result && write_pos[-1] != '/') {
                write_pos--;
            }
        }
    }

    // Handle ./ prefix
    if (strncmp(rel, "./", 2) == 0) {
        rel += 2;
    }

    // Append remaining path
    strcpy(write_pos, rel);

    return result;
}

CssFontFaceDescriptor* css_parse_font_face_content(const char* content, Pool* pool) {
    if (!content) {
        log_error("css_parse_font_face_content: null content");
        return nullptr;
    }

    log_debug("[CSS FontFace] Parsing content: %s", content);

    // Allocate descriptor
    CssFontFaceDescriptor* descriptor;
    if (pool) {
        descriptor = (CssFontFaceDescriptor*)pool_calloc(pool, sizeof(CssFontFaceDescriptor));
    } else {
        descriptor = (CssFontFaceDescriptor*)calloc(1, sizeof(CssFontFaceDescriptor));
    }
    if (!descriptor) {
        log_error("css_parse_font_face_content: allocation failed");
        return nullptr;
    }

    // Initialize defaults
    descriptor->font_style = CSS_VALUE_NORMAL;
    descriptor->font_weight = CSS_VALUE_NORMAL;
    descriptor->font_display = CSS_VALUE_AUTO;

    // Parse the content string - look for font-family and src
    const char* p = content;

    // Skip opening brace
    while (*p && *p != '{') p++;
    if (*p == '{') p++;

    // Parse declarations
    while (*p) {
        // Skip whitespace
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
        if (!*p || *p == '}') break;

        // Find property name end
        const char* prop_start = p;
        while (*p && *p != ':' && *p != '}') p++;
        if (!*p || *p == '}') break;

        size_t prop_len = p - prop_start;

        // Skip whitespace before colon
        while (prop_len > 0 && (prop_start[prop_len-1] == ' ' || prop_start[prop_len-1] == '\t')) {
            prop_len--;
        }

        // Skip colon
        p++;

        // Skip whitespace after colon
        while (*p && (*p == ' ' || *p == '\t')) p++;

        // Find value end (semicolon or closing brace)
        const char* val_start = p;
        while (*p && *p != ';' && *p != '}') p++;
        size_t val_len = p - val_start;

        // Skip semicolon
        if (*p == ';') p++;

        // Process the property
        if (prop_len >= 11 && strncmp(prop_start, "font-family", 11) == 0) {
            // Extract font-family value
            descriptor->family_name = trim_and_unquote(val_start, val_len, pool);
            log_debug("[CSS FontFace]   font-family: '%s'", descriptor->family_name);
        }
        else if (prop_len >= 3 && strncmp(prop_start, "src", 3) == 0) {
            // Extract src URL
            char* temp_val = trim_and_unquote(val_start, val_len, pool);
            descriptor->src_url = extract_url_value(temp_val, pool);
            if (!pool && temp_val) free(temp_val);
            log_debug("[CSS FontFace]   src: '%s'", descriptor->src_url);
        }
        else if (prop_len >= 10 && strncmp(prop_start, "font-style", 10) == 0) {
            char* val = trim_and_unquote(val_start, val_len, pool);
            if (val) {
                if (strcmp(val, "italic") == 0) {
                    descriptor->font_style = CSS_VALUE_ITALIC;
                } else if (strcmp(val, "oblique") == 0) {
                    descriptor->font_style = CSS_VALUE_OBLIQUE;
                }
                if (!pool) free(val);
            }
        }
        else if (prop_len >= 11 && strncmp(prop_start, "font-weight", 11) == 0) {
            char* val = trim_and_unquote(val_start, val_len, pool);
            if (val) {
                if (strcmp(val, "bold") == 0 || strcmp(val, "700") == 0) {
                    descriptor->font_weight = CSS_VALUE_BOLD;
                }
                if (!pool) free(val);
            }
        }
        else if (prop_len >= 12 && strncmp(prop_start, "font-display", 12) == 0) {
            // font-display is parsed but not actively used yet
            // Values: auto, block, swap, fallback, optional
            // For now, we just skip it since the enum values aren't defined
            log_debug("[CSS FontFace]   font-display: (skipped)");
        }
    }

    // Validate: must have family_name
    if (!descriptor->family_name) {
        log_warn("[CSS FontFace] Incomplete @font-face: missing font-family");
        if (!pool) {
            if (descriptor->src_url) free(descriptor->src_url);
            if (descriptor->src_local) free(descriptor->src_local);
            free(descriptor);
        }
        return nullptr;
    }

    return descriptor;
}

CssFontFaceDescriptor** css_extract_font_faces(CssStylesheet* stylesheet,
                                                const char* base_path,
                                                Pool* pool,
                                                int* out_count) {
    if (!stylesheet || !out_count) {
        if (out_count) *out_count = 0;
        return nullptr;
    }

    *out_count = 0;

    // First pass: count @font-face rules
    int count = 0;
    for (size_t i = 0; i < stylesheet->rule_count; i++) {
        CssRule* rule = stylesheet->rules[i];
        if (rule && rule->type == CSS_RULE_FONT_FACE) {
            count++;
        }
    }

    if (count == 0) {
        return nullptr;
    }

    log_debug("[CSS FontFace] Found %d @font-face rules in stylesheet", count);

    // Allocate result array
    CssFontFaceDescriptor** result;
    if (pool) {
        result = (CssFontFaceDescriptor**)pool_calloc(pool, count * sizeof(CssFontFaceDescriptor*));
    } else {
        result = (CssFontFaceDescriptor**)calloc(count, sizeof(CssFontFaceDescriptor*));
    }
    if (!result) {
        return nullptr;
    }

    // Second pass: parse each @font-face rule
    int idx = 0;
    for (size_t i = 0; i < stylesheet->rule_count && idx < count; i++) {
        CssRule* rule = stylesheet->rules[i];
        if (!rule || rule->type != CSS_RULE_FONT_FACE) {
            continue;
        }

        const char* content = rule->data.generic_rule.content;
        if (!content) {
            continue;
        }

        CssFontFaceDescriptor* descriptor = css_parse_font_face_content(content, pool);
        if (descriptor) {
            // Resolve relative URL
            if (descriptor->src_url && base_path) {
                char* resolved = css_resolve_font_url(descriptor->src_url, base_path, pool);
                if (resolved) {
                    if (!pool) free(descriptor->src_url);
                    descriptor->src_url = resolved;
                    log_debug("[CSS FontFace]   resolved src: '%s'", descriptor->src_url);
                }
            }

            result[idx++] = descriptor;
        }
    }

    *out_count = idx;
    log_info("[CSS FontFace] Extracted %d @font-face descriptors", idx);
    return result;
}

void css_font_face_descriptor_free(CssFontFaceDescriptor* descriptor) {
    if (!descriptor) return;

    if (descriptor->family_name) free(descriptor->family_name);
    if (descriptor->src_url) free(descriptor->src_url);
    if (descriptor->src_local) free(descriptor->src_local);
    free(descriptor);
}
