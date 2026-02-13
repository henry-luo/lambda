/**
 * CSS @font-face Rule Parser Implementation
 *
 * Parses @font-face rules from CSS and extracts font descriptors.
 */

#include "css_font_face.hpp"
#include "../../../lib/log.h"
#include "../../../lib/memtrack.h"
#include "../../../lib/str.h"
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
        result = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_CSS);
    }
    if (result) {
        memcpy(result, str, len);
        result[len] = '\0';
    }
    return result;
}

// Helper: extract format from "format('truetype')" or "format(woff)"
static char* extract_format_value(const char* str, Pool* pool) {
    if (!str) return nullptr;

    const char* fmt_start = strstr(str, "format(");
    if (!fmt_start) return nullptr;

    fmt_start += 7; // skip "format("

    // Skip whitespace
    while (*fmt_start == ' ' || *fmt_start == '\t') fmt_start++;

    // Skip opening quote if present
    char quote_char = 0;
    if (*fmt_start == '"' || *fmt_start == '\'') {
        quote_char = *fmt_start++;
    }

    // Find end of format
    const char* fmt_end = fmt_start;
    if (quote_char) {
        while (*fmt_end && *fmt_end != quote_char) fmt_end++;
    } else {
        while (*fmt_end && *fmt_end != ')' && *fmt_end != ' ') fmt_end++;
    }

    size_t len = fmt_end - fmt_start;
    if (len == 0) return nullptr;

    char* result;
    if (pool) {
        result = (char*)pool_alloc(pool, len + 1);
    } else {
        result = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_CSS);
    }
    if (result) {
        memcpy(result, fmt_start, len);
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
        result = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_CSS);
    }
    if (result) {
        memcpy(result, url_start, len);
        result[len] = '\0';
    }
    return result;
}

// Parse all src entries from a src declaration value
// Format: url(...) format(...), url(...) format(...), ...
// Note: URLs can contain commas (e.g., data URIs) so we must properly parse url() boundaries
static int parse_src_entries(const char* src_value, CssFontFaceSrc* entries, int max_entries, Pool* pool) {
    if (!src_value || !entries || max_entries <= 0) return 0;

    log_debug("[CSS FontFace] parse_src_entries input (first 100 chars): %.100s", src_value);

    int count = 0;
    const char* p = src_value;

    while (*p && count < max_entries) {
        // Skip whitespace
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
        if (!*p) break;

        // Find url(
        const char* url_start = strstr(p, "url(");
        if (!url_start) break;

        // Find the matching closing parenthesis for url()
        // Must track nested parentheses and respect quotes
        const char* url_content_start = url_start + 4; // after "url("
        const char* scan = url_content_start;
        int paren_depth = 1;
        char in_quote = 0;

        while (*scan && paren_depth > 0) {
            if (!in_quote) {
                if (*scan == '"' || *scan == '\'') {
                    in_quote = *scan;
                } else if (*scan == '(') {
                    paren_depth++;
                } else if (*scan == ')') {
                    paren_depth--;
                }
            } else {
                // In quote - look for closing quote (handle escape sequences)
                if (*scan == '\\' && scan[1]) {
                    scan++; // skip escaped char
                } else if (*scan == in_quote) {
                    in_quote = 0;
                }
            }
            if (paren_depth > 0) scan++;
        }

        // scan now points to the closing ')' of url()
        const char* url_paren_end = scan;

        log_debug("[CSS FontFace] Found url() content length: %zu", (size_t)(url_paren_end - url_content_start));

        // Look for optional format() after the url()
        const char* after_url = url_paren_end;
        if (*after_url == ')') after_url++;

        // Skip whitespace
        while (*after_url && (*after_url == ' ' || *after_url == '\t')) after_url++;

        // Check for format()
        const char* format_end = after_url;
        if (strncmp(after_url, "format(", 7) == 0) {
            // Find closing paren of format()
            format_end = after_url + 7;
            paren_depth = 1;
            in_quote = 0;
            while (*format_end && paren_depth > 0) {
                if (!in_quote) {
                    if (*format_end == '"' || *format_end == '\'') {
                        in_quote = *format_end;
                    } else if (*format_end == '(') {
                        paren_depth++;
                    } else if (*format_end == ')') {
                        paren_depth--;
                    }
                } else {
                    if (*format_end == in_quote) in_quote = 0;
                }
                if (paren_depth > 0) format_end++;
            }
            if (*format_end == ')') format_end++;
        }

        // entry_end is now at the end of url() format() pair
        const char* entry_end = format_end;

        // Skip to next comma or end
        while (*entry_end && *entry_end != ',') entry_end++;

        // Extract URL and format from this entry
        size_t entry_len = entry_end - url_start;
        log_debug("[CSS FontFace] Entry string length: %zu", entry_len);

        char* entry_str;
        if (pool) {
            entry_str = (char*)pool_alloc(pool, entry_len + 1);
        } else {
            entry_str = (char*)mem_alloc(entry_len + 1, MEM_CAT_INPUT_CSS);
        }
        if (!entry_str) break;

        memcpy(entry_str, url_start, entry_len);
        entry_str[entry_len] = '\0';

        // Extract URL and format from this entry
        entries[count].url = extract_url_value(entry_str, pool);
        entries[count].format = extract_format_value(entry_str, pool);

        if (!pool) mem_free(entry_str);

        if (entries[count].url) {
            // Only log first 60 chars of URL to avoid huge log messages for data URIs
            char url_preview[64];
            size_t url_len = strlen(entries[count].url);
            if (url_len > 60) {
                snprintf(url_preview, sizeof(url_preview), "%.57s...", entries[count].url);
            } else {
                strncpy(url_preview, entries[count].url, sizeof(url_preview) - 1);
                url_preview[sizeof(url_preview) - 1] = '\0';
            }
            log_debug("[CSS FontFace] Parsed src entry %d: url='%s' (len=%zu), format='%s'",
                count, url_preview, url_len, entries[count].format ? entries[count].format : "(none)");
            count++;
        }

        // Move past this entry
        p = entry_end;
        if (*p == ',') p++;
    }

    return count;
}

char* css_resolve_font_url(const char* url, const char* base_path, Pool* pool) {
    if (!url) return nullptr;

    // Skip remote URLs - we don't support downloading fonts from http/https URLs
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        log_debug("[CSS FontFace] Skipping remote font URL: %s", url);
        return nullptr;  // Return nullptr to indicate font can't be loaded
    }

    // Data URIs are self-contained - preserve them as-is
    if (strncmp(url, "data:", 5) == 0) {
        log_debug("[CSS FontFace] Preserving data URI font (length=%zu)", strlen(url));
        if (pool) {
            return pool_strdup(pool, url);
        } else {
            return mem_strdup(url, MEM_CAT_INPUT_CSS);
        }
    }

    // If URL is absolute path, return as-is
    if (url[0] == '/') {
        if (pool) {
            return pool_strdup(pool, url);
        } else {
            return mem_strdup(url, MEM_CAT_INPUT_CSS);
        }
    }

    // If no base path, return URL as-is
    if (!base_path) {
        if (pool) {
            return pool_strdup(pool, url);
        } else {
            return mem_strdup(url, MEM_CAT_INPUT_CSS);
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
        result = (char*)mem_alloc(result_size, MEM_CAT_INPUT_CSS);
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
    str_copy(write_pos, result + result_size - write_pos, rel, strlen(rel));

    return result;
}

// Helper: find the end of a CSS property value
// Must respect url(), quotes, and parentheses boundaries
// Returns pointer to the semicolon or closing brace that ends the value
static const char* find_value_end(const char* start) {
    if (!start) return start;

    const char* p = start;
    char in_quote = 0;
    int paren_depth = 0;

    while (*p && !(*p == ';' && !in_quote && paren_depth == 0) &&
           !(*p == '}' && !in_quote && paren_depth == 0)) {
        if (!in_quote) {
            if (*p == '"' || *p == '\'') {
                in_quote = *p;
            } else if (*p == '(') {
                paren_depth++;
            } else if (*p == ')' && paren_depth > 0) {
                paren_depth--;
            }
        } else {
            // In quote - handle escapes and closing quote
            if (*p == '\\' && p[1]) {
                p++; // skip escaped char
            } else if (*p == in_quote) {
                in_quote = 0;
            }
        }
        p++;
    }

    return p;
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
        descriptor = (CssFontFaceDescriptor*)mem_calloc(1, sizeof(CssFontFaceDescriptor), MEM_CAT_INPUT_CSS);
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

        // Skip CSS comments /* ... */
        while (*p == '/' && *(p+1) == '*') {
            p += 2;  // Skip /*
            while (*p && !(*p == '*' && *(p+1) == '/')) p++;
            if (*p) p += 2;  // Skip */
            // Skip whitespace after comment
            while (*p && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
        }
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

        // Find value end - must respect url() and quote boundaries!
        const char* val_start = p;
        p = find_value_end(p);
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
            // Extract all src URLs with their formats
            char* temp_val = trim_and_unquote(val_start, val_len, pool);

            // Allocate array for src entries
            if (pool) {
                descriptor->src_urls = (CssFontFaceSrc*)pool_calloc(pool, CSS_FONT_FACE_MAX_SRC * sizeof(CssFontFaceSrc));
            } else {
                descriptor->src_urls = (CssFontFaceSrc*)mem_calloc(CSS_FONT_FACE_MAX_SRC, sizeof(CssFontFaceSrc), MEM_CAT_INPUT_CSS);
            }

            if (descriptor->src_urls) {
                descriptor->src_count = parse_src_entries(temp_val, descriptor->src_urls, CSS_FONT_FACE_MAX_SRC, pool);
                log_debug("[CSS FontFace]   parsed %d src entries", descriptor->src_count);
            }

            // Also keep first URL in src_url for backwards compatibility
            descriptor->src_url = extract_url_value(temp_val, pool);
            if (!pool && temp_val) mem_free(temp_val);
            log_debug("[CSS FontFace]   src (first): '%s'", descriptor->src_url);
        }
        else if (prop_len >= 10 && strncmp(prop_start, "font-style", 10) == 0) {
            char* val = trim_and_unquote(val_start, val_len, pool);
            if (val) {
                if (strcmp(val, "italic") == 0) {
                    descriptor->font_style = CSS_VALUE_ITALIC;
                } else if (strcmp(val, "oblique") == 0) {
                    descriptor->font_style = CSS_VALUE_OBLIQUE;
                }
                if (!pool) mem_free(val);
            }
        }
        else if (prop_len >= 11 && strncmp(prop_start, "font-weight", 11) == 0) {
            char* val = trim_and_unquote(val_start, val_len, pool);
            if (val) {
                if (strcmp(val, "bold") == 0 || strcmp(val, "700") == 0) {
                    descriptor->font_weight = CSS_VALUE_BOLD;
                }
                if (!pool) mem_free(val);
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
            if (descriptor->src_url) mem_free(descriptor->src_url);
            if (descriptor->src_local) mem_free(descriptor->src_local);
            if (descriptor->src_urls) mem_free(descriptor->src_urls);
            mem_free(descriptor);
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
        result = (CssFontFaceDescriptor**)mem_calloc(count, sizeof(CssFontFaceDescriptor*), MEM_CAT_INPUT_CSS);
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
            // Resolve all src URLs
            if (descriptor->src_urls && base_path) {
                for (int j = 0; j < descriptor->src_count; j++) {
                    if (descriptor->src_urls[j].url) {
                        char* resolved = css_resolve_font_url(descriptor->src_urls[j].url, base_path, pool);
                        if (resolved) {
                            if (!pool) mem_free(descriptor->src_urls[j].url);
                            descriptor->src_urls[j].url = resolved;
                            log_debug("[CSS FontFace]   resolved src[%d]: '%s'", j, resolved);
                        } else {
                            // URL could not be resolved (e.g., remote URL) - clear it
                            if (!pool) mem_free(descriptor->src_urls[j].url);
                            descriptor->src_urls[j].url = nullptr;
                        }
                    }
                }
            }

            // Also resolve the backwards-compatible src_url
            if (descriptor->src_url && base_path) {
                char* resolved = css_resolve_font_url(descriptor->src_url, base_path, pool);
                if (resolved) {
                    if (!pool) mem_free(descriptor->src_url);
                    descriptor->src_url = resolved;
                    log_debug("[CSS FontFace]   resolved src: '%s'", descriptor->src_url);
                } else {
                    // URL could not be resolved (e.g., remote URL) - clear it
                    if (!pool) mem_free(descriptor->src_url);
                    descriptor->src_url = nullptr;
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

    if (descriptor->family_name) mem_free(descriptor->family_name);
    if (descriptor->src_url) mem_free(descriptor->src_url);
    if (descriptor->src_local) mem_free(descriptor->src_local);

    // Free src_urls array and its contents
    if (descriptor->src_urls) {
        for (int i = 0; i < descriptor->src_count; i++) {
            if (descriptor->src_urls[i].url) mem_free(descriptor->src_urls[i].url);
            if (descriptor->src_urls[i].format) mem_free(descriptor->src_urls[i].format);
        }
        mem_free(descriptor->src_urls);
    }

    mem_free(descriptor);
}
