// radiant/pdf/fonts.cpp
// PDF font mapping and embedded font extraction utilities

#include "pdf_fonts.h"
#include "operators.h"
#include "pages.hpp"  // for pdf_resolve_reference
#include "../view.hpp"
#include "../../lambda/input/input.hpp"
#include "../../lambda/input/pdf_decompress.h"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include "../../lib/memtrack.h"
#include "../../lib/str.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// FreeType for embedded font loading
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H

/**
 * Map PDF font names to system fonts
 *
 * PDF Standard 14 Fonts:
 * - Times-Roman, Times-Bold, Times-Italic, Times-BoldItalic
 * - Helvetica, Helvetica-Bold, Helvetica-Oblique, Helvetica-BoldOblique
 * - Courier, Courier-Bold, Courier-Oblique, Courier-BoldOblique
 * - Symbol
 * - ZapfDingbats
 *
 * @param pdf_font PDF font name
 * @return System font name
 */
const char* map_pdf_font_to_system(const char* pdf_font) {
    // Standard 14 fonts mapping
    static const struct {
        const char* pdf_name;
        const char* system_name;
    } font_map[] = {
        // Helvetica family
        {"Helvetica", "Arial"},
        {"Helvetica-Bold", "Arial Bold"},
        {"Helvetica-Oblique", "Arial Italic"},
        {"Helvetica-BoldOblique", "Arial Bold Italic"},

        // Times family
        {"Times-Roman", "Times New Roman"},
        {"Times-Bold", "Times New Roman Bold"},
        {"Times-Italic", "Times New Roman Italic"},
        {"Times-BoldItalic", "Times New Roman Bold Italic"},

        // Courier family
        {"Courier", "Courier New"},
        {"Courier-Bold", "Courier New Bold"},
        {"Courier-Oblique", "Courier New Italic"},
        {"Courier-BoldOblique", "Courier New Bold Italic"},

        // Symbol fonts
        {"Symbol", "Symbol"},
        {"ZapfDingbats", "Zapf Dingbats"},

        {nullptr, nullptr}
    };

    for (int i = 0; font_map[i].pdf_name; i++) {
        if (strcmp(pdf_font, font_map[i].pdf_name) == 0) {
            return font_map[i].system_name;
        }
    }

    // If not found in standard fonts, try partial matching
    if (strstr(pdf_font, "Helvetica") || strstr(pdf_font, "Arial")) {
        return "Arial";
    }
    if (strstr(pdf_font, "Times") || strstr(pdf_font, "Serif")) {
        return "Times New Roman";
    }
    if (strstr(pdf_font, "Courier") || strstr(pdf_font, "Mono")) {
        return "Courier New";
    }

    // Default fallback
    log_debug("Unknown PDF font '%s', using Arial as fallback", pdf_font);
    return "Arial";
}

// ============================================================================
// Font Encoding Tables
// ============================================================================

/**
 * MacRomanEncoding - maps byte values 0x80-0xFF to Unicode
 * Values below 0x80 are ASCII-compatible
 * Key ligatures: 0xDE = fi (U+FB01), 0xDF = fl (U+FB02)
 */
static const uint32_t mac_roman_to_unicode[128] = {
    // 0x80-0x8F
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
    0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    // 0x90-0x9F
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    // 0xA0-0xAF
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    // 0xB0-0xBF
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    // 0xC0-0xCF
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    // 0xD0-0xDF
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
    0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,  // 0xDE=fi, 0xDF=fl
    // 0xE0-0xEF
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    // 0xF0-0xFF
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7
};

/**
 * WinAnsiEncoding - maps byte values 0x80-0x9F to Unicode
 * Most of 0xA0-0xFF maps to Latin-1 Supplement (U+00A0-U+00FF)
 */
static const uint32_t win_ansi_special[32] = {
    // 0x80-0x8F
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
    // 0x90-0x9F
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178
};

/**
 * Symbol font encoding - maps byte values to Unicode
 * The Symbol font has its own encoding where lowercase letters map to Greek letters
 */
static const uint32_t symbol_to_unicode[256] = {
    // 0x00-0x1F: Control characters (not used)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x20-0x2F: Space and punctuation/symbols
    0x0020, 0x0021, 0x2200, 0x0023, 0x2203, 0x0025, 0x0026, 0x220B,  // ∀ ∃ ∋
    0x0028, 0x0029, 0x2217, 0x002B, 0x002C, 0x2212, 0x002E, 0x002F,  // ∗ −
    // 0x30-0x3F: Digits
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    // 0x40-0x4F: @ and uppercase Greek
    0x2245, 0x0391, 0x0392, 0x03A7, 0x0394, 0x0395, 0x03A6, 0x0393,  // ≅ Α Β Χ Δ Ε Φ Γ
    0x0397, 0x0399, 0x03D1, 0x039A, 0x039B, 0x039C, 0x039D, 0x039F,  // Η Ι ϑ Κ Λ Μ Ν Ο
    // 0x50-0x5F: Uppercase Greek continued
    0x03A0, 0x0398, 0x03A1, 0x03A3, 0x03A4, 0x03A5, 0x03C2, 0x03A9,  // Π Θ Ρ Σ Τ Υ ς Ω
    0x039E, 0x03A8, 0x0396, 0x005B, 0x2234, 0x005D, 0x22A5, 0x005F,  // Ξ Ψ Ζ [ ∴ ] ⊥ _
    // 0x60-0x6F: Lowercase Greek
    0x00AF, 0x03B1, 0x03B2, 0x03C7, 0x03B4, 0x03B5, 0x03C6, 0x03B3,  // ¯ α β χ δ ε φ γ
    0x03B7, 0x03B9, 0x03D5, 0x03BA, 0x03BB, 0x03BC, 0x03BD, 0x03BF,  // η ι ϕ κ λ μ ν ο
    // 0x70-0x7F: Lowercase Greek continued
    0x03C0, 0x03B8, 0x03C1, 0x03C3, 0x03C4, 0x03C5, 0x03D6, 0x03C9,  // π θ ρ σ τ υ ϖ ω
    0x03BE, 0x03C8, 0x03B6, 0x007B, 0x007C, 0x007D, 0x223C, 0x0000,  // ξ ψ ζ { | } ∼
    // 0x80-0x9F: Not defined in standard Symbol
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0xA0-0xAF: Various symbols
    0x20AC, 0x03D2, 0x2032, 0x2264, 0x2044, 0x221E, 0x0192, 0x2663,  // € ϒ ′ ≤ ⁄ ∞ ƒ ♣
    0x2666, 0x2665, 0x2660, 0x2194, 0x2190, 0x2191, 0x2192, 0x2193,  // ♦ ♥ ♠ ↔ ← ↑ → ↓
    // 0xB0-0xBF: Math symbols
    0x00B0, 0x00B1, 0x2033, 0x2265, 0x00D7, 0x221D, 0x2202, 0x2022,  // ° ± ″ ≥ × ∝ ∂ •
    0x00F7, 0x2260, 0x2261, 0x2248, 0x2026, 0x23D0, 0x23AF, 0x21B5,  // ÷ ≠ ≡ ≈ … │ ─ ↵
    // 0xC0-0xCF: Math continued
    0x2135, 0x2111, 0x211C, 0x2118, 0x2297, 0x2295, 0x2205, 0x2229,  // ℵ ℑ ℜ ℘ ⊗ ⊕ ∅ ∩
    0x222A, 0x2283, 0x2287, 0x2284, 0x2282, 0x2286, 0x2208, 0x2209,  // ∪ ⊃ ⊇ ⊄ ⊂ ⊆ ∈ ∉
    // 0xD0-0xDF: More math
    0x2220, 0x2207, 0x00AE, 0x00A9, 0x2122, 0x220F, 0x221A, 0x22C5,  // ∠ ∇ ® © ™ ∏ √ ⋅
    0x00AC, 0x2227, 0x2228, 0x21D4, 0x21D0, 0x21D1, 0x21D2, 0x21D3,  // ¬ ∧ ∨ ⇔ ⇐ ⇑ ⇒ ⇓
    // 0xE0-0xEF: Brackets and arrows
    0x25CA, 0x2329, 0x00AE, 0x00A9, 0x2122, 0x2211, 0x239B, 0x239C,  // ◊ ⟨ ® © ™ ∑ ⎛ ⎜
    0x239D, 0x23A1, 0x23A2, 0x23A3, 0x23A7, 0x23A8, 0x23A9, 0x23AA,  // ⎝ ⎡ ⎢ ⎣ ⎧ ⎨ ⎩ ⎪
    // 0xF0-0xFF: More brackets
    0x0000, 0x232A, 0x222B, 0x2320, 0x23AE, 0x2321, 0x239E, 0x239F,  // ⟩ ∫ ⌠ ⎮ ⌡ ⎞ ⎟
    0x23A0, 0x23A4, 0x23A5, 0x23A6, 0x23AB, 0x23AC, 0x23AD, 0x0000   // ⎠ ⎤ ⎥ ⎦ ⎫ ⎬ ⎭
};

/**
 * Decode a single character code using font encoding
 * Returns the Unicode code point
 */
static uint32_t decode_char_with_encoding(unsigned int char_code, PDFEncodingType encoding) {
    switch (encoding) {
        case PDF_ENCODING_SYMBOL:
            if (char_code < 256 && symbol_to_unicode[char_code] != 0) {
                return symbol_to_unicode[char_code];
            }
            return char_code;  // Fallback for undefined

        case PDF_ENCODING_MAC_ROMAN:
            // ASCII range - same for all encodings
            if (char_code < 0x80) {
                return char_code;
            }
            if (char_code >= 0x80 && char_code <= 0xFF) {
                return mac_roman_to_unicode[char_code - 0x80];
            }
            break;

        case PDF_ENCODING_WIN_ANSI:
            if (char_code < 0x80) {
                return char_code;
            }
            if (char_code >= 0x80 && char_code <= 0x9F) {
                uint32_t unicode = win_ansi_special[char_code - 0x80];
                if (unicode != 0) return unicode;
            }
            // 0xA0-0xFF maps to Latin-1 Supplement (same code point)
            return char_code;

        case PDF_ENCODING_PDF_DOC:
        case PDF_ENCODING_STANDARD:
        default:
            // For these, assume Latin-1 compatible for now
            return char_code;
    }

    return char_code;  // Fallback
}

// ============================================================================
// ToUnicode CMap Parsing
// ============================================================================

/**
 * Parse a hex string like <0041> to its integer value
 * Returns the number of bytes consumed (including angle brackets)
 */
static int parse_hex_string(const char* s, uint32_t* out_value, int* out_byte_count) {
    if (!s || *s != '<') return 0;

    const char* start = s + 1;
    const char* p = start;

    // Find closing >
    while (*p && *p != '>') p++;
    if (*p != '>') return 0;

    int hex_len = (int)(p - start);
    if (hex_len == 0 || hex_len > 8) return 0;

    // Parse hex value
    uint32_t value = 0;
    for (const char* h = start; h < p; h++) {
        char c = *h;
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return 0;
        value = (value << 4) | digit;
    }

    *out_value = value;
    if (out_byte_count) *out_byte_count = hex_len / 2;
    return (int)(p - s + 1); // Include closing >
}

/**
 * Skip whitespace in CMap stream
 */
static const char* skip_cmap_whitespace(const char* s, const char* end) {
    while (s < end && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) s++;
    return s;
}

/**
 * Find a keyword in CMap stream
 * Returns pointer after the keyword, or NULL if not found
 */
static const char* find_cmap_keyword(const char* s, const char* end, const char* keyword) {
    size_t kw_len = strlen(keyword);
    while (s + kw_len <= end) {
        if (strncmp(s, keyword, kw_len) == 0) {
            // Make sure it's a whole word (followed by whitespace or end)
            const char* after = s + kw_len;
            if (after >= end || isspace(*after)) {
                return after;
            }
        }
        s++;
    }
    return nullptr;
}

/**
 * Structure to hold ToUnicode mapping
 */
typedef struct {
    uint32_t* char_codes;     // Source character codes
    uint32_t* unicode_values; // Unicode code points (can be multiple for ligatures)
    int* unicode_lengths;     // Number of unicode chars per mapping (usually 1)
    int count;
    int capacity;
} ToUnicodeMap;

/**
 * Add a mapping to ToUnicode map
 */
static void tounicode_map_add(ToUnicodeMap* map, uint32_t char_code,
                              uint32_t unicode_value, Pool* pool) {
    if (map->count >= map->capacity) {
        int new_cap = map->capacity ? map->capacity * 2 : 256;
        uint32_t* new_codes = (uint32_t*)pool_calloc(pool, sizeof(uint32_t) * new_cap);
        uint32_t* new_values = (uint32_t*)pool_calloc(pool, sizeof(uint32_t) * new_cap);
        int* new_lengths = (int*)pool_calloc(pool, sizeof(int) * new_cap);

        if (!new_codes || !new_values || !new_lengths) return;

        if (map->count > 0) {
            memcpy(new_codes, map->char_codes, sizeof(uint32_t) * map->count);
            memcpy(new_values, map->unicode_values, sizeof(uint32_t) * map->count);
            memcpy(new_lengths, map->unicode_lengths, sizeof(int) * map->count);
        }
        map->char_codes = new_codes;
        map->unicode_values = new_values;
        map->unicode_lengths = new_lengths;
        map->capacity = new_cap;
    }

    map->char_codes[map->count] = char_code;
    map->unicode_values[map->count] = unicode_value;
    map->unicode_lengths[map->count] = 1;
    map->count++;
}

/**
 * Parse a beginbfchar section
 * Format: n beginbfchar <srcCode> <dstString> ... endbfchar
 */
static const char* parse_bfchar_section(const char* s, const char* end,
                                        ToUnicodeMap* map, Pool* pool) {
    s = skip_cmap_whitespace(s, end);

    // Parse entries until endbfchar
    while (s < end) {
        s = skip_cmap_whitespace(s, end);

        // Check for endbfchar
        if (s + 9 <= end && strncmp(s, "endbfchar", 9) == 0) {
            return s + 9;
        }

        // Parse source code
        uint32_t src_code;
        int consumed = parse_hex_string(s, &src_code, nullptr);
        if (consumed == 0) break;
        s += consumed;

        s = skip_cmap_whitespace(s, end);

        // Parse destination Unicode
        uint32_t dst_unicode;
        consumed = parse_hex_string(s, &dst_unicode, nullptr);
        if (consumed == 0) break;
        s += consumed;

        // Add mapping
        tounicode_map_add(map, src_code, dst_unicode, pool);
        log_debug("ToUnicode bfchar: %04X -> U+%04X", src_code, dst_unicode);
    }

    return s;
}

/**
 * Parse a beginbfrange section
 * Format: n beginbfrange <srcCodeLo> <srcCodeHi> <dstStringLo> ... endbfrange
 * Or:     n beginbfrange <srcCodeLo> <srcCodeHi> [<dst1> <dst2> ...] ... endbfrange
 */
static const char* parse_bfrange_section(const char* s, const char* end,
                                         ToUnicodeMap* map, Pool* pool) {
    s = skip_cmap_whitespace(s, end);

    // Parse entries until endbfrange
    while (s < end) {
        s = skip_cmap_whitespace(s, end);

        // Check for endbfrange
        if (s + 10 <= end && strncmp(s, "endbfrange", 10) == 0) {
            return s + 10;
        }

        // Parse source code low
        uint32_t src_lo;
        int consumed = parse_hex_string(s, &src_lo, nullptr);
        if (consumed == 0) break;
        s += consumed;

        s = skip_cmap_whitespace(s, end);

        // Parse source code high
        uint32_t src_hi;
        consumed = parse_hex_string(s, &src_hi, nullptr);
        if (consumed == 0) break;
        s += consumed;

        s = skip_cmap_whitespace(s, end);

        // Check if destination is an array [ ... ] or a single value
        if (*s == '[') {
            // Array of individual mappings
            s++; // Skip '['
            for (uint32_t code = src_lo; code <= src_hi && s < end; code++) {
                s = skip_cmap_whitespace(s, end);
                if (*s == ']') break;

                uint32_t dst;
                consumed = parse_hex_string(s, &dst, nullptr);
                if (consumed == 0) break;
                s += consumed;

                tounicode_map_add(map, code, dst, pool);
                log_debug("ToUnicode bfrange array: %04X -> U+%04X", code, dst);
            }
            // Find closing ]
            while (s < end && *s != ']') s++;
            if (s < end) s++; // Skip ']'
        } else {
            // Single starting value - create range
            uint32_t dst_start;
            consumed = parse_hex_string(s, &dst_start, nullptr);
            if (consumed == 0) break;
            s += consumed;

            // Add all mappings in range
            for (uint32_t code = src_lo; code <= src_hi; code++) {
                uint32_t dst = dst_start + (code - src_lo);
                tounicode_map_add(map, code, dst, pool);
            }
            log_debug("ToUnicode bfrange: %04X-%04X -> U+%04X-U+%04X",
                     src_lo, src_hi, dst_start, dst_start + (src_hi - src_lo));
        }
    }

    return s;
}

/**
 * Parse ToUnicode CMap stream and populate font entry
 *
 * @param cmap_data Raw CMap stream data (decompressed)
 * @param cmap_len Length of CMap data
 * @param entry Font entry to populate
 * @param pool Memory pool for allocations
 * @return true if parsing succeeded
 */
static bool parse_tounicode_cmap(const char* cmap_data, size_t cmap_len,
                                  PDFFontEntry* entry, Pool* pool) {
    if (!cmap_data || cmap_len == 0 || !entry) return false;

    const char* end = cmap_data + cmap_len;
    ToUnicodeMap map = {0};

    log_debug("Parsing ToUnicode CMap (%zu bytes)", cmap_len);

    // Find and parse all beginbfchar sections
    const char* s = cmap_data;
    while (s < end) {
        const char* bfchar = find_cmap_keyword(s, end, "beginbfchar");
        const char* bfrange = find_cmap_keyword(s, end, "beginbfrange");

        // Process whichever comes first
        if (!bfchar && !bfrange) break;

        if (bfchar && (!bfrange || bfchar < bfrange)) {
            s = parse_bfchar_section(bfchar, end, &map, pool);
        } else if (bfrange) {
            s = parse_bfrange_section(bfrange, end, &map, pool);
        } else {
            break;
        }
    }

    if (map.count > 0) {
        // Determine max char code to size the array
        uint32_t max_code = 0;
        for (int i = 0; i < map.count; i++) {
            if (map.char_codes[i] > max_code) max_code = map.char_codes[i];
        }

        // Allocate to_unicode array (sparse mapping, but simple)
        // Use a reasonable limit to avoid huge allocations
        if (max_code > 65535) max_code = 65535;

        entry->to_unicode_count = max_code + 1;
        entry->to_unicode = (uint32_t*)pool_calloc(pool, sizeof(uint32_t) * entry->to_unicode_count);

        if (entry->to_unicode) {
            // Initialize all to 0 (will be treated as "no mapping")
            memset(entry->to_unicode, 0, sizeof(uint32_t) * entry->to_unicode_count);

            // Fill in mappings
            for (int i = 0; i < map.count; i++) {
                uint32_t code = map.char_codes[i];
                if (code < (uint32_t)entry->to_unicode_count) {
                    entry->to_unicode[code] = map.unicode_values[i];
                }
            }

            log_info("Parsed ToUnicode CMap: %d mappings, max code %u", map.count, max_code);
            return true;
        }
    }

    return false;
}

/**
 * Extract and parse ToUnicode CMap from font dictionary
 *
 * @param font_dict PDF font dictionary
 * @param entry Font entry to populate
 * @param input Input context
 * @param pool Memory pool
 * @param pdf_data Root PDF data for resolving indirect references (optional)
 * @return true if ToUnicode was found and parsed
 */
static bool extract_tounicode_cmap(Map* font_dict, PDFFontEntry* entry,
                                    Input* input, Pool* pool, Map* pdf_data) {
    if (!font_dict || !entry) return false;

    // Use safe Map::get that doesn't require runtime context
    ConstItem tounicode_const = font_dict->get("ToUnicode");
    Item tounicode_item = *(Item*)&tounicode_const;

    if (tounicode_item.item == ITEM_NULL) {
        log_debug("No ToUnicode entry in font dict");
        return false;
    }

    // Resolve indirect reference if needed
    if (pdf_data) {
        tounicode_item = pdf_resolve_reference(pdf_data, tounicode_item, pool);
        if (tounicode_item.item == ITEM_NULL) {
            log_debug("Failed to resolve ToUnicode indirect reference");
            return false;
        }
    }

    // ToUnicode should be a stream (Map with 'data' key)
    if (get_type_id(tounicode_item) != LMD_TYPE_MAP) {
        log_debug("ToUnicode is not a stream/map (type=%d)", get_type_id(tounicode_item));
        return false;
    }

    Map* stream_dict = tounicode_item.map;

    // Get stream data - check both "data" and "stream_data" keys (using safe Map::get)
    ConstItem data_const = stream_dict->get("data");
    Item data_item = *(Item*)&data_const;

    if (data_item.item == ITEM_NULL) {
        // Try stream_data key
        ConstItem stream_data_const = stream_dict->get("stream_data");
        data_item = *(Item*)&stream_data_const;
    }

    if (data_item.item == ITEM_NULL) {
        log_debug("ToUnicode stream has no data");
        return false;
    }

    String* data_str = data_item.get_string();
    if (!data_str || data_str->len == 0) {
        log_debug("ToUnicode stream data is empty");
        return false;
    }

    // Get the stream's dictionary (Filter is inside "dictionary" key, not directly on stream)
    ConstItem dict_const = stream_dict->get("dictionary");
    Map* filter_dict = nullptr;
    if (dict_const.item != ITEM_NULL && dict_const.type_id() == LMD_TYPE_MAP) {
        filter_dict = (Map*)dict_const.map;
    }

    // Check for compression filter in the stream dictionary
    ConstItem filter_const = filter_dict ? filter_dict->get("Filter") : stream_dict->get("Filter");
    Item filter_item = *(Item*)&filter_const;

    const char* cmap_data = nullptr;
    size_t cmap_len = 0;
    char* decompressed = nullptr;

    if (filter_item.item != ITEM_NULL) {
        // Need to decompress
        String* filter_name = filter_item.get_string();
        if (filter_name) {
            log_debug("Decompressing ToUnicode CMap with filter: %s", filter_name->chars);
            const char* filters[1] = { filter_name->chars };
            cmap_data = pdf_decompress_stream(data_str->chars, data_str->len,
                                              filters, 1, &cmap_len);
            decompressed = (char*)cmap_data;
            if (cmap_data) {
                log_debug("Decompressed ToUnicode CMap: %zu bytes", cmap_len);
            }
        }
    } else {
        // Raw data
        log_debug("ToUnicode CMap is not compressed");
        cmap_data = data_str->chars;
        cmap_len = data_str->len;
    }

    if (!cmap_data || cmap_len == 0) {
        log_warn("Failed to get ToUnicode CMap data");
        return false;
    }

    bool result = parse_tounicode_cmap(cmap_data, cmap_len, entry, pool);

    // Free decompressed data if allocated
    if (decompressed) {
        free(decompressed);  // from pdf_decompress_stream which uses stdlib malloc
    }

    return result;
}

// TODO: Implement font resolution from PDF resources
// The resolve_font_from_resources function was disabled due to crashes
// when looking up indirect object references. For now, we use hardcoded
// font mappings which work for most PDFs.

/**
 * Extract font weight from PDF font name
 *
 * @param pdf_font PDF font name
 * @return Font weight (CSS_VALUE_NORMAL or CSS_VALUE_BOLD)
 */
CssEnum get_font_weight_from_name(const char* pdf_font) {
    if (strstr(pdf_font, "Bold") || strstr(pdf_font, "Heavy") || strstr(pdf_font, "Black")) {
        return CSS_VALUE_BOLD;
    }
    return CSS_VALUE_NORMAL;
}

/**
 * Extract font style from PDF font name
 *
 * @param pdf_font PDF font name
 * @return Font style (CSS_VALUE_NORMAL, CSS_VALUE_ITALIC, or CSS_VALUE_OBLIQUE)
 */
CssEnum get_font_style_from_name(const char* pdf_font) {
    if (strstr(pdf_font, "Italic")) {
        return CSS_VALUE_ITALIC;
    }
    if (strstr(pdf_font, "Oblique")) {
        return CSS_VALUE_OBLIQUE;
    }
    return CSS_VALUE_NORMAL;
}


/**
 * Create font property from PDF font descriptor
 *
 * @param pool Memory pool for allocation
 * @param font_name PDF font name or font reference (e.g., "Helvetica-Bold" or "F2")
 * @param font_size Font size in points
 * @return FontProp structure
 */
FontProp* create_font_from_pdf(Pool* pool, const char* font_name, double font_size) {
    FontProp* font = (FontProp*)pool_calloc(pool, sizeof(FontProp));
    if (!font) {
        log_error("Failed to allocate font property");
        return nullptr;
    }

    // Resolve font reference from hardcoded mapping
    const char* resolved_font_name = font_name;

    // Check if this is a font reference (F1, F2, F1.0, F2.0 etc.)
    if (font_name && font_name[0] == 'F' && font_name[1] >= '1' && font_name[1] <= '9') {
        // Use hardcoded mapping for common PDF font references
        if (font_name[2] == '\0' || (font_name[2] == '.' && font_name[3] == '0')) {
            switch (font_name[1]) {
                case '1': resolved_font_name = "Helvetica"; break;
                case '2': resolved_font_name = "Times-Roman"; break;
                case '3': resolved_font_name = "Helvetica"; break;  // Changed from Courier - most PDFs use proportional fonts
                case '4': resolved_font_name = "Helvetica-Bold"; break;
                case '5': resolved_font_name = "Times-Bold"; break;
                case '6': resolved_font_name = "Courier-Bold"; break;
                default: resolved_font_name = "Helvetica"; break;
            }
            log_debug("Font reference '%s' using fallback mapping to '%s'", font_name, resolved_font_name);
        }
    }

    // Map PDF font to system font
    font->family = (char*)map_pdf_font_to_system(resolved_font_name);
    font->font_size = (float)font_size;

    // Extract weight and style from resolved font name
    font->font_weight = get_font_weight_from_name(resolved_font_name);
    font->font_style = get_font_style_from_name(resolved_font_name);

    log_debug("Created font: %s, size: %.2f, weight: %d, style: %d",
             font->family, font->font_size, font->font_weight, font->font_style);

    return font;
}

/**
 * Extract font descriptor information from PDF font dictionary
 *
 * This is for more advanced font handling in later phases.
 * For Phase 1, we use simple font name mapping.
 *
 * TODO: Implement in Phase 2
 *
 * @param pool Memory pool for allocation
 * @param font_dict PDF font dictionary (Map)
 * @param input Input context for string creation
 * @return FontProp structure
 */
/*
FontProp* create_font_from_dict(Pool* pool, Map* font_dict, Input* input) {
    if (!font_dict) {
        log_warn("No font dictionary provided");
        return nullptr;
    }

    // Get base font name
    String* base_font_key = input_create_string(input, "BaseFont");
    Item base_font_item = map_get(font_dict, s2it(base_font_key));

    const char* font_name = "Arial"; // Default
    if (base_font_item.item != ITEM_NULL) {
        String* base_font = (String*)base_font_item.item;
        font_name = base_font->chars;
    }

    // Get font size (might not be in dictionary, will be set by Tf operator)
    double font_size = 12.0; // Default

    return create_font_from_pdf(pool, font_name, font_size);
}
*//**
 * Calculate text width (simplified for Phase 1)
 *
 * This is a rough estimation. Proper text width calculation
 * requires font metrics and glyph widths, which will be
 * implemented in Phase 2.
 *
 * @param text Text string
 * @param font_size Font size in points
 * @return Estimated text width in points
 */
float estimate_text_width(const char* text, float font_size) {
    if (!text) return 0.0f;

    int char_count = strlen(text);

    // Rough estimation: average character width is about 0.5 * font_size
    // This is a simplification; actual width varies by font and character
    return (float)char_count * font_size * 0.5f;
}

/**
 * Get font baseline offset (distance from top to baseline)
 *
 * @param font_size Font size in points
 * @return Baseline offset in points
 */
float get_font_baseline_offset(float font_size) {
    // Typical baseline is about 75-80% from top
    return font_size * 0.75f;
}

// ============================================================================
// Phase 2: Embedded Font Support
// ============================================================================

static FT_Library g_ft_library = nullptr;

/**
 * Initialize FreeType library for PDF font loading
 */
bool pdf_font_init_freetype() {
    if (g_ft_library) return true;

    FT_Error error = FT_Init_FreeType(&g_ft_library);
    if (error) {
        log_error("Failed to initialize FreeType: error %d", error);
        return false;
    }
    log_debug("Initialized FreeType for PDF font loading");
    return true;
}

/**
 * Cleanup FreeType library
 */
void pdf_font_cleanup_freetype() {
    if (g_ft_library) {
        FT_Done_FreeType(g_ft_library);
        g_ft_library = nullptr;
    }
}

/**
 * Create a font cache for a document
 */
PDFFontCache* pdf_font_cache_create(Pool* pool) {
    PDFFontCache* cache = (PDFFontCache*)pool_calloc(pool, sizeof(PDFFontCache));
    if (!cache) return nullptr;

    cache->pool = pool;
    cache->fonts = nullptr;
    cache->count = 0;

    // Initialize FreeType if needed
    if (!g_ft_library) {
        pdf_font_init_freetype();
    }
    cache->ft_library = g_ft_library;

    return cache;
}

/**
 * Detect font type from PDF font dictionary
 */
PDFFontType pdf_font_detect_type(Map* font_dict, Input* input) {
    if (!font_dict) return PDF_FONT_UNKNOWN;

    // Create helper to get string from dict
    auto get_name = [input, font_dict](const char* key) -> const char* {
        MarkBuilder builder(input);
        String* key_str = builder.createString(key);
        Item item = {.item = map_get(font_dict, {.item = s2it(key_str)}).item};
        if (item.item != ITEM_NULL) {
            String* val = item.get_string();
            return val ? val->chars : nullptr;
        }
        return nullptr;
    };

    // Get Subtype
    const char* subtype = get_name("Subtype");
    if (!subtype) return PDF_FONT_UNKNOWN;

    if (strcmp(subtype, "Type1") == 0) {
        // Check for CFF font data (FontFile3 with Type1C subtype)
        MarkBuilder builder(input);
        String* desc_key = builder.createString("FontDescriptor");
        Item desc_item = {.item = map_get(font_dict, {.item = s2it(desc_key)}).item};
        if (desc_item.item != ITEM_NULL && get_type_id(desc_item) == LMD_TYPE_MAP) {
            Map* desc_dict = desc_item.map;
            String* ff3_key = builder.createString("FontFile3");
            Item ff3_item = {.item = map_get(desc_dict, {.item = s2it(ff3_key)}).item};
            if (ff3_item.item != ITEM_NULL) {
                return PDF_FONT_TYPE1C;
            }
        }
        return PDF_FONT_TYPE1;
    }
    if (strcmp(subtype, "TrueType") == 0) return PDF_FONT_TRUETYPE;
    if (strcmp(subtype, "Type3") == 0) return PDF_FONT_TYPE3;
    if (strcmp(subtype, "CIDFontType0") == 0) return PDF_FONT_CID_TYPE0;
    if (strcmp(subtype, "CIDFontType0C") == 0) return PDF_FONT_CID_TYPE0C;
    if (strcmp(subtype, "CIDFontType2") == 0) return PDF_FONT_CID_TYPE2;
    if (strcmp(subtype, "Type0") == 0) {
        // Composite font - need to check descendant
        return PDF_FONT_CID_TYPE2; // Common case
    }
    if (strcmp(subtype, "OpenType") == 0) return PDF_FONT_OPENTYPE;

    return PDF_FONT_UNKNOWN;
}

/**
 * Extract embedded font data from PDF font dictionary
 * Returns the raw font data that can be loaded by FreeType
 */
static unsigned char* extract_embedded_font_data(Map* font_dict, Input* input,
                                                  size_t* out_len, PDFFontType* out_type) {
    if (!font_dict || !out_len) return nullptr;
    *out_len = 0;

    MarkBuilder builder(input);

    // Get FontDescriptor
    String* desc_key = builder.createString("FontDescriptor");
    Item desc_item = {.item = map_get(font_dict, {.item = s2it(desc_key)}).item};
    if (desc_item.item == ITEM_NULL || get_type_id(desc_item) != LMD_TYPE_MAP) {
        log_debug("No FontDescriptor in font dict");
        return nullptr;
    }
    Map* desc_dict = desc_item.map;

    // Try FontFile (Type 1), FontFile2 (TrueType), FontFile3 (CFF/OpenType)
    const char* font_file_keys[] = {"FontFile3", "FontFile2", "FontFile", nullptr};
    PDFFontType font_types[] = {PDF_FONT_TYPE1C, PDF_FONT_TRUETYPE, PDF_FONT_TYPE1};

    for (int i = 0; font_file_keys[i]; i++) {
        String* ff_key = builder.createString(font_file_keys[i]);
        Item ff_item = {.item = map_get(desc_dict, {.item = s2it(ff_key)}).item};

        if (ff_item.item != ITEM_NULL && get_type_id(ff_item) == LMD_TYPE_MAP) {
            Map* stream_dict = ff_item.map;

            // Get stream data
            String* data_key = builder.createString("data");
            Item data_item = {.item = map_get(stream_dict, {.item = s2it(data_key)}).item};
            if (data_item.item == ITEM_NULL) continue;

            String* data_str = data_item.get_string();
            if (!data_str || data_str->len == 0) continue;

            // Check for filter (might need decompression)
            String* filter_key = builder.createString("Filter");
            Item filter_item = {.item = map_get(stream_dict, {.item = s2it(filter_key)}).item};

            if (filter_item.item != ITEM_NULL) {
                // Need to decompress
                String* filter_name = filter_item.get_string();
                const char* filters[1] = { filter_name->chars };

                size_t decompressed_len = 0;
                char* decompressed = pdf_decompress_stream(data_str->chars, data_str->len,
                                                           filters, 1, &decompressed_len);
                if (decompressed) {
                    *out_len = decompressed_len;
                    if (out_type) *out_type = font_types[i];
                    log_info("Extracted embedded font (%s): %zu bytes", font_file_keys[i], decompressed_len);
                    return (unsigned char*)decompressed;
                }
            } else {
                // Raw data
                unsigned char* font_data = (unsigned char*)mem_alloc(data_str->len, MEM_CAT_FONT);
                if (font_data) {
                    memcpy(font_data, data_str->chars, data_str->len);
                    *out_len = data_str->len;
                    if (out_type) *out_type = font_types[i];
                    log_info("Extracted embedded font (%s): %zu bytes", font_file_keys[i], data_str->len);
                    return font_data;
                }
            }
        }
    }

    return nullptr;
}

/**
 * Load embedded font into FreeType
 */
FT_Face pdf_font_load_embedded(PDFFontCache* cache, unsigned char* font_data,
                                size_t font_data_len, PDFFontType font_type) {
    if (!cache || !font_data || font_data_len == 0) return nullptr;
    if (!cache->ft_library) {
        if (!pdf_font_init_freetype()) return nullptr;
        cache->ft_library = g_ft_library;
    }

    FT_Face face = nullptr;
    FT_Error error;

    // Load based on font type
    switch (font_type) {
        case PDF_FONT_TRUETYPE:
        case PDF_FONT_OPENTYPE:
        case PDF_FONT_CID_TYPE2:
            // Direct TrueType/OpenType loading
            error = FT_New_Memory_Face(cache->ft_library, font_data, font_data_len, 0, &face);
            break;

        case PDF_FONT_TYPE1C:
        case PDF_FONT_CID_TYPE0C:
            // CFF font - FreeType can handle directly
            error = FT_New_Memory_Face(cache->ft_library, font_data, font_data_len, 0, &face);
            break;

        case PDF_FONT_TYPE1:
            // Type 1 font - FreeType can handle PFB/PFA
            error = FT_New_Memory_Face(cache->ft_library, font_data, font_data_len, 0, &face);
            break;

        default:
            log_warn("Unsupported font type for embedded loading: %d", font_type);
            return nullptr;
    }

    if (error) {
        log_error("FreeType failed to load embedded font: error %d", error);
        return nullptr;
    }

    log_info("Loaded embedded font: %s (%s)",
             face->family_name ? face->family_name : "unknown",
             face->style_name ? face->style_name : "");

    return face;
}

/**
 * Add font to cache from PDF Resources
 */
PDFFontEntry* pdf_font_cache_add(PDFFontCache* cache, const char* ref_name,
                                  Map* font_dict, Input* input, Map* pdf_data) {
    if (!cache || !ref_name || !font_dict) return nullptr;

    // Check if already cached
    for (PDFFontEntry* entry = cache->fonts; entry; entry = entry->next) {
        if (entry->name && strcmp(entry->name, ref_name) == 0) {
            return entry;
        }
    }

    // Create new entry
    PDFFontEntry* entry = (PDFFontEntry*)pool_calloc(cache->pool, sizeof(PDFFontEntry));
    if (!entry) return nullptr;

    // Copy reference name
    size_t name_len = strlen(ref_name);
    entry->name = (char*)pool_calloc(cache->pool, name_len + 1);
    if (entry->name) {
        str_copy(entry->name, name_len + 1, ref_name, name_len);
    }

    // Get BaseFont using Map::get (safe, no runtime context needed)
    ConstItem base_const = font_dict->get("BaseFont");
    if (base_const.item != ITEM_NULL) {
        // Use string() method on ConstItem or cast to Item* for get_string()
        String* base_str = base_const.string();
        if (!base_str) {
            // Try symbol type
            Item* base_item = (Item*)&base_const;
            if (base_item->_type_id == LMD_TYPE_SYMBOL) {
                base_str = (String*)base_item->string_ptr;
            }
        }
        if (base_str) {
            entry->base_font = (char*)pool_calloc(cache->pool, base_str->len + 1);
            if (entry->base_font) {
                str_copy(entry->base_font, base_str->len + 1, base_str->chars, base_str->len);
            }
        }
    }

    // Detect font type
    entry->type = pdf_font_detect_type(font_dict, input);

    // Detect encoding type
    entry->encoding = PDF_ENCODING_STANDARD;  // Default
    ConstItem enc_const = font_dict->get("Encoding");
    if (enc_const.item != ITEM_NULL) {
        String* enc_str = enc_const.string();
        if (!enc_str) {
            Item* enc_item = (Item*)&enc_const;
            if (enc_item->_type_id == LMD_TYPE_SYMBOL) {
                enc_str = (String*)enc_item->string_ptr;
            }
        }
        if (enc_str) {
            if (strcmp(enc_str->chars, "MacRomanEncoding") == 0) {
                entry->encoding = PDF_ENCODING_MAC_ROMAN;
                log_debug("Font '%s' uses MacRomanEncoding", ref_name);
            } else if (strcmp(enc_str->chars, "WinAnsiEncoding") == 0) {
                entry->encoding = PDF_ENCODING_WIN_ANSI;
                log_debug("Font '%s' uses WinAnsiEncoding", ref_name);
            } else if (strcmp(enc_str->chars, "PDFDocEncoding") == 0) {
                entry->encoding = PDF_ENCODING_PDF_DOC;
            } else if (strcmp(enc_str->chars, "MacExpertEncoding") == 0) {
                entry->encoding = PDF_ENCODING_MACEXPERT;
            } else if (strcmp(enc_str->chars, "Identity-H") == 0) {
                entry->encoding = PDF_ENCODING_IDENTITY_H;
            }
        }
    }

    // Special case: detect Symbol and ZapfDingbats fonts by BaseFont name
    // These fonts have implicit encodings even without an Encoding key
    if (entry->encoding == PDF_ENCODING_STANDARD && entry->base_font) {
        if (strcmp(entry->base_font, "Symbol") == 0 ||
            strstr(entry->base_font, "+Symbol") != nullptr) {
            entry->encoding = PDF_ENCODING_SYMBOL;
            log_debug("Font '%s' detected as Symbol font", ref_name);
        } else if (strcmp(entry->base_font, "ZapfDingbats") == 0 ||
                   strstr(entry->base_font, "+ZapfDingbats") != nullptr) {
            entry->encoding = PDF_ENCODING_ZAPFDINGBATS;
            log_debug("Font '%s' detected as ZapfDingbats font", ref_name);
        }
    }

    // Try to extract embedded font
    PDFFontType embed_type;
    size_t font_data_len = 0;
    unsigned char* font_data = extract_embedded_font_data(font_dict, input, &font_data_len, &embed_type);

    if (font_data && font_data_len > 0) {
        entry->is_embedded = true;
        entry->font_data = font_data;
        entry->font_data_len = font_data_len;

        // Load into FreeType
        entry->ft_face = pdf_font_load_embedded(cache, font_data, font_data_len, embed_type);
        if (entry->ft_face) {
            log_info("Cached embedded font '%s' -> '%s'", ref_name,
                    entry->ft_face->family_name ? entry->ft_face->family_name : "unknown");
        }
    } else {
        entry->is_embedded = false;
        log_debug("Font '%s' (%s) is not embedded, using system fallback",
                 ref_name, entry->base_font ? entry->base_font : "unknown");
    }

    // Extract widths if present (use Map::get for safe access without runtime context)
    ConstItem widths_item_const = font_dict->get("Widths");
    Item widths_item = *(Item*)&widths_item_const;

    // Resolve widths if it's an indirect reference
    if (widths_item.item != ITEM_NULL && pdf_data) {
        widths_item = pdf_resolve_reference(pdf_data, widths_item, cache->pool);
    }

    if (widths_item.item != ITEM_NULL && get_type_id(widths_item) == LMD_TYPE_ARRAY) {
        Array* widths_array = widths_item.array;
        entry->widths_count = widths_array->length;
        entry->widths = (float*)pool_calloc(cache->pool, sizeof(float) * entry->widths_count);
        if (entry->widths) {
            for (int i = 0; i < entry->widths_count; i++) {
                // Direct array access instead of array_get (which uses runtime context)
                Item w = widths_array->items[i];
                TypeId w_type = get_type_id(w);
                if (w_type == LMD_TYPE_FLOAT) {
                    entry->widths[i] = (float)w.get_double();
                } else if (w_type == LMD_TYPE_INT) {
                    entry->widths[i] = (float)w.int_val;
                }
            }
        }
    }

    // Get FirstChar/LastChar (using safe Map::get)
    ConstItem fc_const = font_dict->get("FirstChar");
    if (fc_const.item != ITEM_NULL) {
        Item* fc_item = (Item*)&fc_const;
        TypeId fc_type = get_type_id(*fc_item);
        entry->first_char = (int)(fc_type == LMD_TYPE_FLOAT ?
                                  fc_item->get_double() : fc_item->int_val);
    }

    ConstItem lc_const = font_dict->get("LastChar");
    if (lc_const.item != ITEM_NULL) {
        Item* lc_item = (Item*)&lc_const;
        TypeId lc_type = get_type_id(*lc_item);
        entry->last_char = (int)(lc_type == LMD_TYPE_FLOAT ?
                                 lc_item->get_double() : lc_item->int_val);
    }

    // Extract and parse ToUnicode CMap for character decoding
    if (extract_tounicode_cmap(font_dict, entry, input, cache->pool, pdf_data)) {
        log_info("Font '%s' has ToUnicode mapping with %d entries",
                ref_name, entry->to_unicode_count);
    }

    // Add to cache list
    entry->next = cache->fonts;
    cache->fonts = entry;
    cache->count++;

    log_debug("Added font to cache: %s (type=%d, embedded=%d, widths=%d, tounicode=%d)",
             ref_name, entry->type, entry->is_embedded, entry->widths_count,
             entry->to_unicode_count);

    return entry;
}

/**
 * Get font entry from cache
 */
PDFFontEntry* pdf_font_cache_get(PDFFontCache* cache, const char* ref_name) {
    if (!cache || !ref_name) return nullptr;

    for (PDFFontEntry* entry = cache->fonts; entry; entry = entry->next) {
        if (entry->name && strcmp(entry->name, ref_name) == 0) {
            return entry;
        }
    }
    return nullptr;
}

/**
 * Create FontProp from cached font entry
 * Uses embedded FreeType face if available, otherwise falls back to system fonts
 */
FontProp* create_font_from_cache_entry(Pool* pool, PDFFontEntry* entry, double font_size) {
    if (!pool || !entry) return nullptr;

    FontProp* font = (FontProp*)pool_calloc(pool, sizeof(FontProp));
    if (!font) return nullptr;

    font->font_size = (float)font_size;

    if (entry->ft_face) {
        // Use embedded font - get family name from FreeType
        if (entry->ft_face->family_name) {
            font->family = entry->ft_face->family_name;
        } else {
            font->family = (char*)"Arial";
        }

        // Get style from FreeType
        if (entry->ft_face->style_flags & FT_STYLE_FLAG_BOLD) {
            font->font_weight = CSS_VALUE_BOLD;
        } else {
            font->font_weight = CSS_VALUE_NORMAL;
        }

        if (entry->ft_face->style_flags & FT_STYLE_FLAG_ITALIC) {
            font->font_style = CSS_VALUE_ITALIC;
        } else {
            font->font_style = CSS_VALUE_NORMAL;
        }

        log_debug("Using embedded font: %s, size: %.2f", font->family, font->font_size);
    } else {
        // Fall back to system font mapping
        const char* base_font = entry->base_font ? entry->base_font : "Helvetica";
        font->family = (char*)map_pdf_font_to_system(base_font);
        font->font_weight = get_font_weight_from_name(base_font);
        font->font_style = get_font_style_from_name(base_font);

        log_debug("Using system font: %s for %s, size: %.2f",
                 font->family, entry->name, font->font_size);
    }

    return font;
}

/**
 * Get glyph width from cached font entry
 */
float pdf_font_get_glyph_width(PDFFontEntry* entry, int char_code, float font_size) {
    if (!entry) return font_size * 0.5f;  // Default estimate

    // Check widths array first
    if (entry->widths && char_code >= entry->first_char && char_code <= entry->last_char) {
        int idx = char_code - entry->first_char;
        if (idx >= 0 && idx < entry->widths_count) {
            // PDF widths are in 1/1000 of text space unit
            return entry->widths[idx] / 1000.0f * font_size;
        }
    }

    // Try FreeType if embedded
    if (entry->ft_face) {
        FT_Error error = FT_Set_Char_Size(entry->ft_face, 0, (FT_F26Dot6)(font_size * 64), 72, 72);
        if (!error) {
            FT_UInt glyph_index = FT_Get_Char_Index(entry->ft_face, char_code);
            error = FT_Load_Glyph(entry->ft_face, glyph_index, FT_LOAD_NO_SCALE);
            if (!error) {
                // Advance width in font units
                float advance = entry->ft_face->glyph->linearHoriAdvance / 65536.0f;
                float units_per_em = (float)entry->ft_face->units_per_EM;
                return advance / units_per_em * font_size;
            }
        }
    }

    // Fallback: estimate based on font size
    return font_size * 0.5f;
}

/**
 * Calculate text width using cached font
 */
float pdf_font_calculate_text_width(PDFFontEntry* entry, const char* text, float font_size) {
    if (!text || !entry) return estimate_text_width(text, font_size);

    float total_width = 0.0f;
    const unsigned char* p = (const unsigned char*)text;

    while (*p) {
        total_width += pdf_font_get_glyph_width(entry, *p, font_size);
        p++;
    }

    return total_width;
}

/**
 * Check if font has ToUnicode mapping
 */
bool pdf_font_has_tounicode(PDFFontEntry* entry) {
    return entry && entry->to_unicode && entry->to_unicode_count > 0;
}

/**
 * Check if font needs text decoding (has ToUnicode or special encoding)
 * This is used to determine if pdf_font_decode_text should be called
 */
bool pdf_font_needs_decoding(PDFFontEntry* entry) {
    if (!entry) return false;

    // Has ToUnicode CMap
    if (entry->to_unicode && entry->to_unicode_count > 0) {
        return true;
    }

    // Has a non-standard encoding that requires translation
    if (entry->encoding != PDF_ENCODING_STANDARD &&
        entry->encoding != PDF_ENCODING_IDENTITY_H) {
        return true;
    }

    return false;
}

/**
 * Encode a Unicode code point to UTF-8
 * Returns the number of bytes written
 */
static int encode_utf8(uint32_t codepoint, char* buf) {
    if (codepoint < 0x80) {
        buf[0] = (char)codepoint;
        return 1;
    } else if (codepoint < 0x800) {
        buf[0] = (char)(0xC0 | (codepoint >> 6));
        buf[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint < 0x10000) {
        buf[0] = (char)(0xE0 | (codepoint >> 12));
        buf[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    } else if (codepoint < 0x110000) {
        buf[0] = (char)(0xF0 | (codepoint >> 18));
        buf[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0;
}

/**
 * Decode PDF text using ToUnicode CMap
 * Converts character codes to Unicode string
 *
 * @param entry Font entry with ToUnicode mapping
 * @param input_text Raw text from PDF (character codes)
 * @param input_len Length of input text
 * @param output_buf Buffer for decoded UTF-8 output
 * @param output_size Size of output buffer
 * @return Length of decoded string, or -1 on error
 */
int pdf_font_decode_text(PDFFontEntry* entry, const char* input_text, int input_len,
                         char* output_buf, int output_size) {
    if (!input_text || input_len <= 0 || !output_buf || output_size <= 0) {
        return -1;
    }

    // If no entry at all, just copy as-is (fallback)
    if (!entry) {
        int copy_len = input_len < output_size - 1 ? input_len : output_size - 1;
        memcpy(output_buf, input_text, copy_len);
        output_buf[copy_len] = '\0';
        return copy_len;
    }

    // Determine which decoding method to use
    bool has_tounicode = (entry->to_unicode && entry->to_unicode_count > 0);
    bool has_encoding = (entry->encoding != PDF_ENCODING_STANDARD &&
                         entry->encoding != PDF_ENCODING_IDENTITY_H);

    // If no ToUnicode and no special encoding, just copy
    if (!has_tounicode && !has_encoding) {
        int copy_len = input_len < output_size - 1 ? input_len : output_size - 1;
        memcpy(output_buf, input_text, copy_len);
        output_buf[copy_len] = '\0';
        return copy_len;
    }

    int out_pos = 0;
    const unsigned char* p = (const unsigned char*)input_text;
    const unsigned char* end = p + input_len;

    while (p < end && out_pos < output_size - 4) {  // Reserve space for UTF-8
        unsigned int char_code = *p++;

        // Determine Unicode code point
        uint32_t unicode;

        if (has_tounicode) {
            // Use ToUnicode map if available
            if (char_code < (unsigned int)entry->to_unicode_count && entry->to_unicode[char_code] != 0) {
                unicode = entry->to_unicode[char_code];
            } else {
                // Fallback to encoding or direct code
                unicode = has_encoding ? decode_char_with_encoding(char_code, entry->encoding) : char_code;
            }
        } else {
            // Use font encoding table
            unicode = decode_char_with_encoding(char_code, entry->encoding);
        }

        // Handle common ligatures by decomposing them
        // This is important for search/comparison functionality
        switch (unicode) {
            case 0xFB00:  // ff ligature
                out_pos += encode_utf8('f', output_buf + out_pos);
                out_pos += encode_utf8('f', output_buf + out_pos);
                break;
            case 0xFB01:  // fi ligature
                out_pos += encode_utf8('f', output_buf + out_pos);
                out_pos += encode_utf8('i', output_buf + out_pos);
                break;
            case 0xFB02:  // fl ligature
                out_pos += encode_utf8('f', output_buf + out_pos);
                out_pos += encode_utf8('l', output_buf + out_pos);
                break;
            case 0xFB03:  // ffi ligature
                out_pos += encode_utf8('f', output_buf + out_pos);
                out_pos += encode_utf8('f', output_buf + out_pos);
                out_pos += encode_utf8('i', output_buf + out_pos);
                break;
            case 0xFB04:  // ffl ligature
                out_pos += encode_utf8('f', output_buf + out_pos);
                out_pos += encode_utf8('f', output_buf + out_pos);
                out_pos += encode_utf8('l', output_buf + out_pos);
                break;
            default:
                out_pos += encode_utf8(unicode, output_buf + out_pos);
                break;
        }
    }

    output_buf[out_pos] = '\0';
    return out_pos;
}
