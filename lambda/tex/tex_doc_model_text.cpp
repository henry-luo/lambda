// tex_doc_model_text.cpp - Text transformation utilities for document model
//
// Handles:
// - LaTeX text transformations (ligatures, quotes, dashes)
// - Diacritic support (combining characters)
// - Whitespace normalization

#include "tex_document_model.hpp"
#include "lib/strbuf.h"
#include "lib/str.h"
#include "lib/arena.h"
#include <cstdlib>
#include <cstring>

namespace tex {

// ============================================================================
// Whitespace Normalization
// ============================================================================

// Normalize LaTeX whitespace: collapse consecutive whitespace to single space
// This preserves leading and trailing whitespace (single space at most)
// since inter-element spacing is meaningful in inline context.
// Returns the normalized string allocated in arena, or nullptr if result is empty
const char* normalize_latex_whitespace(const char* text, Arena* arena) {
    if (!text) return nullptr;

    size_t len = strlen(text);
    if (len == 0) return nullptr;

    // Allocate buffer (can't be larger than original)
    char* buf = (char*)arena_alloc(arena, len + 1);
    char* out = buf;

    bool in_whitespace = false;

    for (const char* p = text; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            // Collapse consecutive whitespace to single space
            if (!in_whitespace) {
                *out++ = ' ';
                in_whitespace = true;
            }
        } else {
            *out++ = *p;
            in_whitespace = false;
        }
    }

    *out = '\0';

    size_t result_len = out - buf;
    if (result_len == 0) return nullptr;

    return buf;
}

// ============================================================================
// LaTeX Text Transformations
// ============================================================================

// Transform LaTeX text to typographic text with proper:
// - Dash ligatures: --- → em-dash (—), -- → en-dash (–), - → hyphen (‐)
// - Quote ligatures: `` → ", '' → ", ` → ', ' → '
// - Standard ligatures: fi → ﬁ, fl → ﬂ, ff → ﬀ, ffi → ﬃ, ffl → ﬄ
//
// If in_monospace is true, skip all conversions (keep literal ASCII).
// Returns a dynamically allocated string that must be freed by caller.
char* transform_latex_text(const char* text, size_t len, bool in_monospace) {
    if (!text || len == 0) return nullptr;

    // Allocate buffer with room for UTF-8 expansion (3x worst case)
    size_t buf_size = len * 4 + 1;
    char* result = (char*)malloc(buf_size);
    if (!result) return nullptr;

    size_t out_pos = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];

        // Ensure we have room (conservative check)
        if (out_pos + 8 >= buf_size) {
            buf_size *= 2;
            char* new_buf = (char*)realloc(result, buf_size);
            if (!new_buf) { free(result); return nullptr; }
            result = new_buf;
        }

        if (in_monospace) {
            // In monospace mode, keep all characters as literal ASCII
            result[out_pos++] = c;
            continue;
        }

        // Check for dash ligatures
        if (c == '-') {
            // Check for --- (em-dash)
            if (i + 2 < len && text[i+1] == '-' && text[i+2] == '-') {
                // — (U+2014 = em-dash) = E2 80 94
                result[out_pos++] = '\xE2';
                result[out_pos++] = '\x80';
                result[out_pos++] = '\x94';
                i += 2;  // Skip two more hyphens
                continue;
            }
            // Check for -- (en-dash)
            if (i + 1 < len && text[i+1] == '-') {
                // – (U+2013 = en-dash) = E2 80 93
                result[out_pos++] = '\xE2';
                result[out_pos++] = '\x80';
                result[out_pos++] = '\x93';
                i += 1;  // Skip one more hyphen
                continue;
            }
            // Single hyphen → typographic hyphen (U+2010)
            // ‐ = E2 80 90
            result[out_pos++] = '\xE2';
            result[out_pos++] = '\x80';
            result[out_pos++] = '\x90';
            continue;
        }

        // Check for quote ligatures
        if (c == '`') {
            // Check for `` (opening double quote)
            if (i + 1 < len && text[i+1] == '`') {
                // " (U+201C) = E2 80 9C
                result[out_pos++] = '\xE2';
                result[out_pos++] = '\x80';
                result[out_pos++] = '\x9C';
                i += 1;
                continue;
            }
            // Single backtick → opening single quote
            // ' (U+2018) = E2 80 98
            result[out_pos++] = '\xE2';
            result[out_pos++] = '\x80';
            result[out_pos++] = '\x98';
            continue;
        }

        if (c == '\'') {
            // Check for '' (closing double quote)
            if (i + 1 < len && text[i+1] == '\'') {
                // " (U+201D) = E2 80 9D
                result[out_pos++] = '\xE2';
                result[out_pos++] = '\x80';
                result[out_pos++] = '\x9D';
                i += 1;
                continue;
            }
            // Single apostrophe → closing single quote / apostrophe
            // ' (U+2019) = E2 80 99
            result[out_pos++] = '\xE2';
            result[out_pos++] = '\x80';
            result[out_pos++] = '\x99';
            continue;
        }

        // Check for f-ligatures
        if (c == 'f') {
            // Check for ffi
            if (i + 2 < len && text[i+1] == 'f' && text[i+2] == 'i') {
                // ﬃ (U+FB03) = EF AC 83
                result[out_pos++] = '\xEF';
                result[out_pos++] = '\xAC';
                result[out_pos++] = '\x83';
                i += 2;
                continue;
            }
            // Check for ffl
            if (i + 2 < len && text[i+1] == 'f' && text[i+2] == 'l') {
                // ﬄ (U+FB04) = EF AC 84
                result[out_pos++] = '\xEF';
                result[out_pos++] = '\xAC';
                result[out_pos++] = '\x84';
                i += 2;
                continue;
            }
            // Check for ff
            if (i + 1 < len && text[i+1] == 'f') {
                // ﬀ (U+FB00) = EF AC 80
                result[out_pos++] = '\xEF';
                result[out_pos++] = '\xAC';
                result[out_pos++] = '\x80';
                i += 1;
                continue;
            }
            // Check for fi
            if (i + 1 < len && text[i+1] == 'i') {
                // ﬁ (U+FB01) = EF AC 81
                result[out_pos++] = '\xEF';
                result[out_pos++] = '\xAC';
                result[out_pos++] = '\x81';
                i += 1;
                continue;
            }
            // Check for fl
            if (i + 1 < len && text[i+1] == 'l') {
                // ﬂ (U+FB02) = EF AC 82
                result[out_pos++] = '\xEF';
                result[out_pos++] = '\xAC';
                result[out_pos++] = '\x82';
                i += 1;
                continue;
            }
        }

        // Check for << (left guillemet)
        if (c == '<' && i + 1 < len && text[i+1] == '<') {
            // « (U+00AB) = C2 AB
            result[out_pos++] = '\xC2';
            result[out_pos++] = '\xAB';
            i += 1;
            continue;
        }

        // Check for >> (right guillemet)
        if (c == '>' && i + 1 < len && text[i+1] == '>') {
            // » (U+00BB) = C2 BB
            result[out_pos++] = '\xC2';
            result[out_pos++] = '\xBB';
            i += 1;
            continue;
        }

        // Check for !´ (inverted exclamation) - ´ is U+00B4 = C2 B4 in UTF-8
        if (c == '!' && i + 2 < len && (unsigned char)text[i+1] == 0xC2 && (unsigned char)text[i+2] == 0xB4) {
            // ¡ (U+00A1) = C2 A1
            result[out_pos++] = '\xC2';
            result[out_pos++] = '\xA1';
            i += 2;
            continue;
        }

        // Check for ?´ (inverted question) - ´ is U+00B4 = C2 B4 in UTF-8
        if (c == '?' && i + 2 < len && (unsigned char)text[i+1] == 0xC2 && (unsigned char)text[i+2] == 0xB4) {
            // ¿ (U+00BF) = C2 BF
            result[out_pos++] = '\xC2';
            result[out_pos++] = '\xBF';
            i += 2;
            continue;
        }

        // Default: copy character as-is
        result[out_pos++] = c;
    }

    result[out_pos] = '\0';
    return result;
}

// HTML escape and append text with LaTeX transformations
// Handles dash ligatures, quote ligatures, and f-ligatures
void html_escape_append_transformed(StrBuf* out, const char* text, size_t len, bool in_monospace) {
    if (!text || len == 0) return;

    char* transformed = transform_latex_text(text, len, in_monospace);
    if (transformed) {
        // HTML escape the transformed text
        for (const char* p = transformed; *p; p++) {
            unsigned char c = (unsigned char)*p;
            // Check for UTF-8 non-breaking space (U+00A0 = 0xC2 0xA0)
            if (c == 0xC2 && *(p + 1) == (char)0xA0) {
                strbuf_append_str(out, "&nbsp;");
                p++;  // Skip the second byte
                continue;
            }
            switch (c) {
            case '&':  strbuf_append_str(out, "&amp;"); break;
            case '<':  strbuf_append_str(out, "&lt;"); break;
            case '>':  strbuf_append_str(out, "&gt;"); break;
            case '"':  strbuf_append_str(out, "&quot;"); break;
            // Note: don't escape single quotes - we want the curly ones to show
            default:   strbuf_append_char(out, (char)c); break;
            }
        }
        free(transformed);
    }
}

// ============================================================================
// Diacritic Support
// ============================================================================

// Map diacritic command character to Unicode combining character
static uint32_t get_diacritic_combining(char cmd) {
    switch (cmd) {
    case '\'': return 0x0301;  // combining acute accent
    case '`':  return 0x0300;  // combining grave accent
    case '^':  return 0x0302;  // combining circumflex
    case '"':  return 0x0308;  // combining diaeresis (umlaut)
    case '~':  return 0x0303;  // combining tilde
    case '=':  return 0x0304;  // combining macron
    case '.':  return 0x0307;  // combining dot above
    case 'u':  return 0x0306;  // combining breve
    case 'v':  return 0x030C;  // combining caron (háček)
    case 'H':  return 0x030B;  // combining double acute
    case 'c':  return 0x0327;  // combining cedilla
    case 'd':  return 0x0323;  // combining dot below
    case 'b':  return 0x0331;  // combining macron below
    case 'r':  return 0x030A;  // combining ring above
    case 'k':  return 0x0328;  // combining ogonek
    default:   return 0;
    }
}

// Encode UTF-8 codepoint, return number of bytes written
static int utf8_encode(uint32_t cp, char* out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
}

// Get UTF-8 character length from first byte (delegates to str.h)
int utf8_char_len(unsigned char first_byte) {
    return (int)str_utf8_char_len(first_byte);
}

// Apply diacritic command to base character, returning combined result
// Uses NFD form (base + combining character)
const char* apply_diacritic(char diacritic_cmd, const char* base_char, Arena* arena) {
    if (!base_char || base_char[0] == '\0') {
        return nullptr;
    }

    uint32_t combining = get_diacritic_combining(diacritic_cmd);
    if (combining == 0) {
        // Unknown diacritic, just return base
        return base_char;
    }

    // Get base character length
    int base_len = utf8_char_len((unsigned char)base_char[0]);

    // Allocate buffer: base + combining (up to 4 bytes each) + null
    char* result = (char*)arena_alloc(arena, base_len + 4 + 1);

    // Copy base character
    memcpy(result, base_char, base_len);

    // Add combining character
    int comb_len = utf8_encode(combining, result + base_len);
    result[base_len + comb_len] = '\0';

    return result;
}

// Check if a tag is a diacritic command (single character)
bool is_diacritic_tag(const char* tag) {
    if (!tag || strlen(tag) != 1) return false;
    return get_diacritic_combining(tag[0]) != 0;
}

} // namespace tex
