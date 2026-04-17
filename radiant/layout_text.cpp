#include "layout.hpp"
#include "layout_positioned.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_style.hpp"
#include "../lib/avl_tree.h"
#include "../lib/font/font.h"
#include "../lib/utf.h"

#include "../lib/log.h"
#include <chrono>

#include <cctype>
#include <cwctype>
#include <utf8proc.h>
using namespace std::chrono;

// External timing accumulators from layout.cpp
extern double g_text_layout_time;
extern int64_t g_text_layout_count;

// ============================================================================
// CSS text-transform Helpers
// ============================================================================

/**
 * Apply CSS font-variant: small-caps transformation.
 * Converts lowercase characters to uppercase. The actual size reduction
 * is handled by using a smaller font size during glyph measurement.
 * Uses utf8proc for proper Unicode case conversion.
 */
static inline uint32_t apply_small_caps(uint32_t codepoint) {
    if (codepoint < 128) {
        return std::toupper(codepoint);
    } else {
        return (uint32_t)utf8proc_toupper((utf8proc_int32_t)codepoint);
    }
}

/**
 * Check if font-variant: small-caps is active for the current element.
 * Walks the DOM ancestor chain since font-variant is inherited.
 */
static inline bool has_small_caps(LayoutContext* lycon) {
    DomNode* node = lycon->elmt ? lycon->elmt : lycon->view;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = (DomElement*)node;
            if (elem->font && elem->font->font_variant == CSS_VALUE_SMALL_CAPS) {
                return true;
            }
        }
        node = node->parent;
    }
    return false;
}

// Unicode full case mapping table (SpecialCasing.txt)
// Characters whose uppercase form expands to multiple codepoints.
// CSS Text 3 §2.1: "the full case mappings for Unicode code points are used"
struct FullCaseMapping { uint32_t from; uint32_t to[3]; uint8_t len; };

static const FullCaseMapping g_uppercase_full[] = {
    {0x00DF, {0x0053, 0x0053, 0x0000}, 2},  // ß → SS
    {0x0149, {0x02BC, 0x004E, 0x0000}, 2},  // ŉ → ʼN
    {0x01F0, {0x004A, 0x030C, 0x0000}, 2},  // ǰ → J̌
    {0x0390, {0x0399, 0x0308, 0x0301}, 3},  // ΐ → Ϊ́
    {0x03B0, {0x03A5, 0x0308, 0x0301}, 3},  // ΰ → Ϋ́
    {0x0587, {0x0535, 0x0552, 0x0000}, 2},  // և → ԵՒ
    {0x1E96, {0x0048, 0x0331, 0x0000}, 2},  // ẖ → H̱
    {0x1E97, {0x0054, 0x0308, 0x0000}, 2},  // ẗ → T̈
    {0x1E98, {0x0057, 0x030A, 0x0000}, 2},  // ẘ → W̊
    {0x1E99, {0x0059, 0x030A, 0x0000}, 2},  // ẙ → Y̊
    {0x1E9A, {0x0041, 0x02BE, 0x0000}, 2},  // ẚ → Aʾ
    {0x1F50, {0x03A5, 0x0313, 0x0000}, 2},
    {0x1F52, {0x03A5, 0x0313, 0x0300}, 3},
    {0x1F54, {0x03A5, 0x0313, 0x0301}, 3},
    {0x1F56, {0x03A5, 0x0313, 0x0342}, 3},
    {0x1F80, {0x1F08, 0x0399, 0x0000}, 2},
    {0x1F81, {0x1F09, 0x0399, 0x0000}, 2},
    {0x1F82, {0x1F0A, 0x0399, 0x0000}, 2},
    {0x1F83, {0x1F0B, 0x0399, 0x0000}, 2},
    {0x1F84, {0x1F0C, 0x0399, 0x0000}, 2},
    {0x1F85, {0x1F0D, 0x0399, 0x0000}, 2},
    {0x1F86, {0x1F0E, 0x0399, 0x0000}, 2},
    {0x1F87, {0x1F0F, 0x0399, 0x0000}, 2},
    {0x1F88, {0x1F08, 0x0399, 0x0000}, 2},
    {0x1F89, {0x1F09, 0x0399, 0x0000}, 2},
    {0x1F8A, {0x1F0A, 0x0399, 0x0000}, 2},
    {0x1F8B, {0x1F0B, 0x0399, 0x0000}, 2},
    {0x1F8C, {0x1F0C, 0x0399, 0x0000}, 2},
    {0x1F8D, {0x1F0D, 0x0399, 0x0000}, 2},
    {0x1F8E, {0x1F0E, 0x0399, 0x0000}, 2},
    {0x1F8F, {0x1F0F, 0x0399, 0x0000}, 2},
    {0x1F90, {0x1F28, 0x0399, 0x0000}, 2},
    {0x1F91, {0x1F29, 0x0399, 0x0000}, 2},
    {0x1F92, {0x1F2A, 0x0399, 0x0000}, 2},
    {0x1F93, {0x1F2B, 0x0399, 0x0000}, 2},
    {0x1F94, {0x1F2C, 0x0399, 0x0000}, 2},
    {0x1F95, {0x1F2D, 0x0399, 0x0000}, 2},
    {0x1F96, {0x1F2E, 0x0399, 0x0000}, 2},
    {0x1F97, {0x1F2F, 0x0399, 0x0000}, 2},
    {0x1F98, {0x1F28, 0x0399, 0x0000}, 2},
    {0x1F99, {0x1F29, 0x0399, 0x0000}, 2},
    {0x1F9A, {0x1F2A, 0x0399, 0x0000}, 2},
    {0x1F9B, {0x1F2B, 0x0399, 0x0000}, 2},
    {0x1F9C, {0x1F2C, 0x0399, 0x0000}, 2},
    {0x1F9D, {0x1F2D, 0x0399, 0x0000}, 2},
    {0x1F9E, {0x1F2E, 0x0399, 0x0000}, 2},
    {0x1F9F, {0x1F2F, 0x0399, 0x0000}, 2},
    {0x1FA0, {0x1F68, 0x0399, 0x0000}, 2},
    {0x1FA1, {0x1F69, 0x0399, 0x0000}, 2},
    {0x1FA2, {0x1F6A, 0x0399, 0x0000}, 2},
    {0x1FA3, {0x1F6B, 0x0399, 0x0000}, 2},
    {0x1FA4, {0x1F6C, 0x0399, 0x0000}, 2},
    {0x1FA5, {0x1F6D, 0x0399, 0x0000}, 2},
    {0x1FA6, {0x1F6E, 0x0399, 0x0000}, 2},
    {0x1FA7, {0x1F6F, 0x0399, 0x0000}, 2},
    {0x1FA8, {0x1F68, 0x0399, 0x0000}, 2},
    {0x1FA9, {0x1F69, 0x0399, 0x0000}, 2},
    {0x1FAA, {0x1F6A, 0x0399, 0x0000}, 2},
    {0x1FAB, {0x1F6B, 0x0399, 0x0000}, 2},
    {0x1FAC, {0x1F6C, 0x0399, 0x0000}, 2},
    {0x1FAD, {0x1F6D, 0x0399, 0x0000}, 2},
    {0x1FAE, {0x1F6E, 0x0399, 0x0000}, 2},
    {0x1FAF, {0x1F6F, 0x0399, 0x0000}, 2},
    {0x1FB2, {0x1FBA, 0x0399, 0x0000}, 2},
    {0x1FB3, {0x0391, 0x0399, 0x0000}, 2},
    {0x1FB4, {0x0386, 0x0399, 0x0000}, 2},
    {0x1FB6, {0x0391, 0x0342, 0x0000}, 2},
    {0x1FB7, {0x0391, 0x0342, 0x0399}, 3},
    {0x1FBC, {0x0391, 0x0399, 0x0000}, 2},
    {0x1FC2, {0x1FCA, 0x0399, 0x0000}, 2},
    {0x1FC3, {0x0397, 0x0399, 0x0000}, 2},
    {0x1FC4, {0x0389, 0x0399, 0x0000}, 2},
    {0x1FC6, {0x0397, 0x0342, 0x0000}, 2},
    {0x1FC7, {0x0397, 0x0342, 0x0399}, 3},
    {0x1FCC, {0x0397, 0x0399, 0x0000}, 2},
    {0x1FD2, {0x0399, 0x0308, 0x0300}, 3},
    {0x1FD3, {0x0399, 0x0308, 0x0301}, 3},
    {0x1FD6, {0x0399, 0x0342, 0x0000}, 2},
    {0x1FD7, {0x0399, 0x0308, 0x0342}, 3},
    {0x1FE2, {0x03A5, 0x0308, 0x0300}, 3},
    {0x1FE3, {0x03A5, 0x0308, 0x0301}, 3},
    {0x1FE4, {0x03A1, 0x0313, 0x0000}, 2},
    {0x1FE6, {0x03A5, 0x0342, 0x0000}, 2},
    {0x1FE7, {0x03A5, 0x0308, 0x0342}, 3},
    {0x1FF2, {0x1FFA, 0x0399, 0x0000}, 2},
    {0x1FF3, {0x03A9, 0x0399, 0x0000}, 2},
    {0x1FF4, {0x038F, 0x0399, 0x0000}, 2},
    {0x1FF6, {0x03A9, 0x0342, 0x0000}, 2},
    {0x1FF7, {0x03A9, 0x0342, 0x0399}, 3},
    {0x1FFC, {0x03A9, 0x0399, 0x0000}, 2},
    {0xFB00, {0x0046, 0x0046, 0x0000}, 2},  // ﬀ → FF
    {0xFB01, {0x0046, 0x0049, 0x0000}, 2},  // ﬁ → FI
    {0xFB02, {0x0046, 0x004C, 0x0000}, 2},  // ﬂ → FL
    {0xFB03, {0x0046, 0x0046, 0x0049}, 3},  // ﬃ → FFI
    {0xFB04, {0x0046, 0x0046, 0x004C}, 3},  // ﬄ → FFL
    {0xFB05, {0x0053, 0x0054, 0x0000}, 2},  // ﬅ → ST
    {0xFB06, {0x0053, 0x0054, 0x0000}, 2},  // ﬆ → ST
    {0xFB13, {0x0544, 0x0546, 0x0000}, 2},  // ﬓ → ՄՆ
    {0xFB14, {0x0544, 0x0535, 0x0000}, 2},  // ﬔ → ՄԵ
    {0xFB15, {0x0544, 0x053B, 0x0000}, 2},  // ﬕ → ՄԻ
    {0xFB16, {0x054E, 0x0546, 0x0000}, 2},  // ﬖ → ՎՆ
    {0xFB17, {0x0544, 0x053D, 0x0000}, 2},  // ﬗ → ՄԽ
};
static const int g_uppercase_full_count = 102;

// İ (U+0130) lowercases to i + combining dot above
static const FullCaseMapping g_lowercase_full[] = {
    {0x0130, {0x0069, 0x0307, 0x0000}, 2},
};
static const int g_lowercase_full_count = 1;

// binary search lookup in full case mapping table (sorted by 'from')
static const FullCaseMapping* lookup_full_case(const FullCaseMapping* table,
    int count, uint32_t codepoint) {
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (table[mid].from == codepoint) return &table[mid];
        if (table[mid].from < codepoint) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

/**
 * Apply CSS text-transform with full Unicode case mapping support.
 * CSS Text 3 §2.1: "the full case mappings for Unicode code points are used"
 * This handles 1-to-many case expansions (e.g., ß → SS).
 * @param codepoint Input Unicode codepoint
 * @param text_transform CSS text-transform value
 * @param is_word_start True if first character of a word (for capitalize)
 * @param out Output buffer for transformed codepoints (must hold at least 3)
 * @return Number of codepoints written to out (1-3)
 */
int apply_text_transform_full(uint32_t codepoint, CssEnum text_transform,
    bool is_word_start, uint32_t* out) {
    if (text_transform == CSS_VALUE_UPPERCASE || (text_transform == CSS_VALUE_CAPITALIZE && is_word_start)) {
        // check full case mapping table for 1-to-many expansions
        const FullCaseMapping* m = lookup_full_case(g_uppercase_full, g_uppercase_full_count, codepoint);
        if (m) {
            for (int i = 0; i < m->len; i++) out[i] = m->to[i];
            return m->len;
        }
        // simple 1-to-1 mapping
        if (codepoint < 128) {
            out[0] = std::toupper(codepoint);
        } else {
            out[0] = (uint32_t)utf8proc_toupper((utf8proc_int32_t)codepoint);
        }
        return 1;
    } else if (text_transform == CSS_VALUE_LOWERCASE) {
        // check full case mapping table for 1-to-many expansions
        const FullCaseMapping* m = lookup_full_case(g_lowercase_full, g_lowercase_full_count, codepoint);
        if (m) {
            for (int i = 0; i < m->len; i++) out[i] = m->to[i];
            return m->len;
        }
        if (codepoint < 128) {
            out[0] = std::tolower(codepoint);
        } else {
            out[0] = (uint32_t)utf8proc_tolower((utf8proc_int32_t)codepoint);
        }
        return 1;
    } else if (text_transform == CSS_VALUE_FULL_SIZE_KANA) {
        // CSS Text 3 §2.1: Convert small Kana to their normal (full-size) equivalents
        // always 1-to-1
        switch (codepoint) {
        // Hiragana small → normal
        case 0x3041: out[0] = 0x3042; return 1;
        case 0x3043: out[0] = 0x3044; return 1;
        case 0x3045: out[0] = 0x3046; return 1;
        case 0x3047: out[0] = 0x3048; return 1;
        case 0x3049: out[0] = 0x304A; return 1;
        case 0x3063: out[0] = 0x3064; return 1;
        case 0x3083: out[0] = 0x3084; return 1;
        case 0x3085: out[0] = 0x3086; return 1;
        case 0x3087: out[0] = 0x3088; return 1;
        case 0x308E: out[0] = 0x308F; return 1;
        case 0x3095: out[0] = 0x304B; return 1;
        case 0x3096: out[0] = 0x3051; return 1;
        // Katakana small → normal
        case 0x30A1: out[0] = 0x30A2; return 1;
        case 0x30A3: out[0] = 0x30A4; return 1;
        case 0x30A5: out[0] = 0x30A6; return 1;
        case 0x30A7: out[0] = 0x30A8; return 1;
        case 0x30A9: out[0] = 0x30AA; return 1;
        case 0x30C3: out[0] = 0x30C4; return 1;
        case 0x30E3: out[0] = 0x30E4; return 1;
        case 0x30E5: out[0] = 0x30E6; return 1;
        case 0x30E7: out[0] = 0x30E8; return 1;
        case 0x30EE: out[0] = 0x30EF; return 1;
        case 0x30F5: out[0] = 0x30AB; return 1;
        case 0x30F6: out[0] = 0x30B1; return 1;
        // Half-width Katakana small → normal
        case 0xFF67: out[0] = 0xFF71; return 1;
        case 0xFF68: out[0] = 0xFF72; return 1;
        case 0xFF69: out[0] = 0xFF73; return 1;
        case 0xFF6A: out[0] = 0xFF74; return 1;
        case 0xFF6B: out[0] = 0xFF75; return 1;
        case 0xFF6C: out[0] = 0xFF94; return 1;
        case 0xFF6D: out[0] = 0xFF95; return 1;
        case 0xFF6E: out[0] = 0xFF96; return 1;
        case 0xFF6F: out[0] = 0xFF82; return 1;
        }
    }
    out[0] = codepoint;
    return 1;
}

/**
 * Backward-compatible wrapper: returns only the first codepoint of the full mapping.
 */
uint32_t apply_text_transform(uint32_t codepoint, CssEnum text_transform, bool is_word_start) {
    uint32_t out[3];
    apply_text_transform_full(codepoint, text_transform, is_word_start, out);
    return out[0];
}

/**
 * Get text-transform property from block.
 * @param blk BlockProp structure (can be NULL)
 * @return CSS text-transform value or CSS_VALUE_NONE
 */
CssEnum get_text_transform_from_block(BlockProp* blk) {
    if (blk && blk->text_transform != 0 && blk->text_transform != CSS_VALUE_INHERIT) {
        return blk->text_transform;
    }
    return CSS_VALUE_NONE;
}

/**
 * Count justification opportunities in a UTF-8 text segment.
 * CSS Text 3 §7.3: For auto justification, distribute extra space at:
 *   1. Word separators (U+0020 SPACE)
 *   2. Between adjacent CJK ID-class characters (inter-character gaps)
 * @param str UTF-8 text data
 * @param len byte length of text segment
 * @return number of justification opportunities
 */
int count_justify_opportunities(const char* str, int len) {
    if (!str || len <= 0) return 0;

    int count = 0;
    const char* end = str + len;
    bool prev_was_id = false;

    while (str < end) {
        uint32_t cp;
        int bytes = str_utf8_decode(str, (size_t)(end - str), &cp);
        if (bytes <= 0) { str++; prev_was_id = false; continue; }

        if (cp == ' ') {
            count++;
            prev_was_id = false;
        } else if (has_id_line_break_class(cp)) {
            // CJK inter-character gap: opportunity between two adjacent ID-class chars
            if (prev_was_id) {
                count++;
            }
            prev_was_id = true;
        } else {
            prev_was_id = false;
        }

        str += bytes;
    }

    return count;
}

/**
 * Get text-transform property from the layout context.
 * Checks block property for the current element or parent elements.
 */
static inline CssEnum get_text_transform(LayoutContext* lycon) {
    // Check parent chain for text-transform property (it's inherited)
    DomNode* node = lycon->elmt ? lycon->elmt : lycon->view;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = (DomElement*)node;
            CssEnum transform = get_text_transform_from_block(elem->blk);
            if (transform != CSS_VALUE_NONE) {
                return transform;
            }
        }
        node = node->parent;
    }
    return CSS_VALUE_NONE;
}

/**
 * Get word-break property from the layout context.
 * Checks block property for the current element or parent elements.
 */
static inline CssEnum get_word_break(LayoutContext* lycon) {
    // Check parent chain for word-break property (it's inherited)
    DomNode* node = lycon->elmt ? lycon->elmt : lycon->view;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = (DomElement*)node;
            if (elem->blk && elem->blk->word_break != 0) {
                return elem->blk->word_break;
            }
        }
        node = node->parent;
    }
    return CSS_VALUE_NORMAL;  // Default to normal
}

/**
 * Get line-break property from the layout context.
 * Checks block property for the current element or parent elements.
 */
static inline CssEnum get_line_break(LayoutContext* lycon) {
    DomNode* node = lycon->elmt ? lycon->elmt : lycon->view;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = (DomElement*)node;
            if (elem->blk && elem->blk->line_break != 0) {
                return elem->blk->line_break;
            }
        }
        node = node->parent;
    }
    return CSS_VALUE_AUTO;
}

/**
 * Get overflow-wrap property from the layout context.
 * Checks block property for the current element or parent elements.
 */
static inline CssEnum get_overflow_wrap(LayoutContext* lycon) {
    DomNode* node = lycon->elmt ? lycon->elmt : lycon->view;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = (DomElement*)node;
            if (elem->blk && elem->blk->overflow_wrap != 0) {
                return elem->blk->overflow_wrap;
            }
        }
        node = node->parent;
    }
    return CSS_VALUE_NORMAL;
}

/**
 * Resolve the lang attribute by walking up the DOM tree.
 * Returns the first non-null lang (or xml:lang) attribute found on an ancestor,
 * or nullptr if none is set. The lang attribute is inherited per HTML spec.
 */
static const char* resolve_lang(DomNode* node) {
    while (node) {
        if (node->is_element()) {
            const char* lang = node->get_attribute("lang");
            if (lang && *lang) return lang;
            lang = node->get_attribute("xml:lang");
            if (lang && *lang) return lang;
        }
        node = node->parent;
    }
    return nullptr;
}

/**
 * Check if a lang attribute value indicates Japanese.
 * Matches "ja", "ja-JP", "ja-*" (case-insensitive prefix match).
 */
static inline bool is_lang_japanese(const char* lang) {
    if (!lang) return false;
    if ((lang[0] == 'j' || lang[0] == 'J') && (lang[1] == 'a' || lang[1] == 'A')) {
        return lang[2] == '\0' || lang[2] == '-';
    }
    return false;
}

// ============================================================================
// CSS white-space Property Helpers
// ============================================================================

static inline bool is_cjk_character(uint32_t codepoint) {
    return utf_is_cjk(codepoint);
}

/**
 * Check if a codepoint has UAX#14 line break class ID (Ideographic).
 * Characters with ID class allow line breaks before and after them
 * under normal wrapping (CSS Text 3 §5.2, UAX #14).
 * Covers: CJK ideographs, Kana, Hangul, emoji, Yi, CJK symbols/radicals,
 * CJK compatibility ideographs, and other ID-class characters.
 */
bool has_id_line_break_class(uint32_t cp) {
    // CJK Unified Ideographs and Extensions
    if (cp >= 0x3400 && cp <= 0x9FFF) return true;   // Extension A + main block
    if (cp >= 0xF900 && cp <= 0xFAFF) return true;   // CJK Compatibility Ideographs
    if (cp >= 0x20000 && cp <= 0x2CEAF) return true;  // Extensions B/C/D/E
    if (cp >= 0x2CEB0 && cp <= 0x2EBE0) return true;  // Extension F
    if (cp >= 0x2EBF0 && cp <= 0x2F7FF) return true;  // Extension I + nearby
    if (cp >= 0x2F800 && cp <= 0x2FA1F) return true;  // CJK Compat Ideographs Supplement
    if (cp >= 0x30000 && cp <= 0x3FFFD) return true;  // Extensions G/H + Plane 3

    // Kana and Hangul
    if (cp >= 0x3040 && cp <= 0x30FF) return true;   // Hiragana + Katakana
    if (cp >= 0x31F0 && cp <= 0x31FF) return true;   // Katakana Phonetic Extensions
    if (cp >= 0xAC00 && cp <= 0xD7AF) return true;   // Hangul Syllables
    if (cp >= 0xFF65 && cp <= 0xFF9F) return true;   // Halfwidth Katakana
    if (cp >= 0x1B000 && cp <= 0x1B2FF) return true;  // Kana Supplement + Extended-A + B

    // CJK Symbols, Radicals, and related
    if (cp >= 0x2E80 && cp <= 0x2FFF) return true;   // CJK Radicals + Kangxi + IDC
    if (cp >= 0x3003 && cp <= 0x3007) return true;   // Ditto mark, JIS, Closing, Number Zero
    if (cp >= 0x3012 && cp <= 0x3013) return true;   // Postal Mark, Geta Mark
    if (cp >= 0x3020 && cp <= 0x303F) return true;   // Postal Mark Face through IDHFS
    if (cp >= 0x3200 && cp <= 0x33FF) return true;   // Enclosed CJK + CJK Compatibility
    if (cp >= 0x3105 && cp <= 0x312F) return true;   // Bopomofo
    if (cp >= 0x3131 && cp <= 0x318E) return true;   // Hangul Compatibility Jamo
    if (cp >= 0x3190 && cp <= 0x31EF) return true;   // Kanbun + Bopomofo Ext + CJK Strokes

    // Yi syllables and radicals
    if (cp >= 0xA000 && cp <= 0xA4CF) return true;   // Yi Syllables + Yi Radicals

    // Halfwidth/Fullwidth forms with ID class
    if (cp >= 0xFE30 && cp <= 0xFE6F) return true;   // CJK Compatibility Forms + Small Forms
    if (cp >= 0xFF01 && cp <= 0xFF60) return true;   // Fullwidth ASCII variants
    if (cp >= 0xFFA0 && cp <= 0xFFDC) return true;   // Halfwidth Hangul

    // Tangut
    if (cp >= 0x17000 && cp <= 0x18DF2) return true;  // Tangut Ideographs + Components

    // Nushu
    if (cp >= 0x1B170 && cp <= 0x1B2FB) return true;  // Nushu Characters

    // Emoji and symbols with UAX#14 ID class
    // Supplementary Symbols and Pictographs (Plane 1) — nearly all have ID class
    if (cp >= 0x1F000 && cp <= 0x1FAFF) return true;  // Mahjong..Symbols Extended-A
    if (cp >= 0x1FC00 && cp <= 0x1FFFD) return true;  // Reserved (default ID)

    // BMP emoji with ID class (scattered in Misc Symbols / Dingbats)
    if (cp == 0x231A || cp == 0x231B) return true;   // Watch, Hourglass
    if (cp >= 0x23E9 && cp <= 0x23F3) return true;   // Media controls, timers
    if (cp >= 0x23F8 && cp <= 0x23FA) return true;   // Pause, stop, record
    if (cp == 0x2614 || cp == 0x2615) return true;   // Umbrella, Hot Beverage
    if (cp == 0x2648) return true;                     // Aries (start of zodiac)
    if (cp >= 0x2648 && cp <= 0x2653) return true;   // Zodiac symbols
    if (cp == 0x267F) return true;                     // Wheelchair
    if (cp >= 0x2693 && cp <= 0x2694) return true;   // Anchor, Swords
    if (cp == 0x26A1) return true;                     // High Voltage
    if (cp >= 0x26AA && cp <= 0x26AB) return true;   // Medium circles
    if (cp >= 0x26BD && cp <= 0x26C8) return true;   // Soccer..Thunder Cloud
    if (cp >= 0x26CE && cp <= 0x26D4) return true;   // Ophiuchus..No Entry
    if (cp >= 0x26D5 && cp <= 0x26EA) return true;   // Various symbols..Church
    if (cp >= 0x26F0 && cp <= 0x26F5) return true;   // Mountain..Sailboat
    if (cp >= 0x26F7 && cp <= 0x26FA) return true;   // Skier..Tent
    if (cp == 0x26FD) return true;                     // Fuel Pump
    if (cp == 0x2702) return true;                     // Scissors
    if (cp == 0x2705) return true;                     // Check Mark
    if (cp >= 0x2708 && cp <= 0x270D) return true;   // Airplane..Writing Hand
    if (cp == 0x270F) return true;                     // Pencil
    if (cp == 0x2712) return true;                     // Black Nib
    if (cp == 0x2714) return true;                     // Heavy Check Mark
    if (cp == 0x2716) return true;                     // Heavy Multiplication X
    if (cp == 0x271D) return true;                     // Latin Cross
    if (cp == 0x2721) return true;                     // Star of David
    if (cp == 0x2728) return true;                     // Sparkles
    if (cp >= 0x2733 && cp <= 0x2734) return true;   // Asterisk, Star
    if (cp == 0x2744) return true;                     // Snowflake
    if (cp == 0x2747) return true;                     // Sparkle
    if (cp == 0x274C) return true;                     // Cross Mark
    if (cp == 0x274E) return true;                     // Cross Mark squared
    if (cp >= 0x2753 && cp <= 0x2755) return true;   // Question marks, Exclamation
    if (cp == 0x2757) return true;                     // Heavy Exclamation
    if (cp >= 0x2763 && cp <= 0x2764) return true;   // Heart Exclamation, Heavy Heart
    if (cp >= 0x2795 && cp <= 0x2797) return true;   // Plus, Minus, Division
    if (cp == 0x27A1) return true;                     // Rightwards Arrow
    if (cp == 0x27B0) return true;                     // Curly Loop
    if (cp == 0x27BF) return true;                     // Double Curly Loop
    if (cp >= 0x2934 && cp <= 0x2935) return true;   // Arrow up-right, down-right
    if (cp >= 0x2B05 && cp <= 0x2B07) return true;   // Leftwards/Upwards/Downwards Arrow
    if (cp >= 0x2B1B && cp <= 0x2B1C) return true;   // Black/White Large Square
    if (cp == 0x2B50) return true;                     // White Medium Star
    if (cp == 0x2B55) return true;                     // Heavy Large Circle
    if (cp == 0x3297) return true;                     // Circled Ideograph Congratulation
    if (cp == 0x3299) return true;                     // Circled Ideograph Secret

    return false;
}

// ============================================================================
// Unicode Line Break Class Helpers (UAX #14 / CSS Text 3 §5.2)
// ============================================================================

/**
 * Check if a codepoint has OP (Opening Punctuation) line-break class.
 * CSS Text 3 §5.2: No break after OP characters — they stay with following content.
 * Based on Unicode Line Break Algorithm (UAX #14).
 */
static inline bool is_line_break_op(uint32_t cp) {
    // ASCII opening brackets
    if (cp == 0x0028 || cp == 0x005B || cp == 0x007B) return true;
    // Tibetan opening marks
    if (cp == 0x0F3A || cp == 0x0F3C) return true;
    // Ogham
    if (cp == 0x169B) return true;
    // Quotation marks with OP class
    if (cp == 0x201A || cp == 0x201E) return true;
    // Left-pointing double angle quotation mark opening
    if (cp == 0x2045) return true;
    // Left-pointing angle bracket
    if (cp == 0x2329) return true;
    // Misc math paired brackets
    if (cp >= 0x2768 && cp <= 0x2775 && (cp & 1) == 0) return true; // even = opening
    // Mathematical brackets
    if (cp == 0x27E6 || cp == 0x27E8 || cp == 0x27EA) return true;
    // Misc technical paired brackets
    if (cp >= 0x2983 && cp <= 0x2998 && (cp & 1) == 1) return true; // odd = opening
    if (cp == 0x29D8 || cp == 0x29DA || cp == 0x29FC) return true;
    // CJK paired brackets and quotation marks
    if (cp == 0x3008 || cp == 0x300A || cp == 0x300C || cp == 0x300E ||
        cp == 0x3010 || cp == 0x3014 || cp == 0x3016 || cp == 0x3018 ||
        cp == 0x301A || cp == 0x301D) return true;
    // CJK compatibility forms (vertical)
    if (cp >= 0xFE35 && cp <= 0xFE44 && (cp & 1) == 1) return true; // odd = opening
    if (cp == 0xFE47) return true;
    // Small form variants
    if (cp == 0xFE59 || cp == 0xFE5B || cp == 0xFE5D) return true;
    // Fullwidth forms
    if (cp == 0xFF08 || cp == 0xFF3B || cp == 0xFF5B || cp == 0xFF5F || cp == 0xFF62) return true;
    return false;
}

/**
 * Check if a codepoint has CL (Closing Punctuation) or CP line-break class.
 * CSS Text 3 §5.2: No break before CL/CP characters — they stay with preceding content.
 * Includes both CL (Closing Punctuation) and CP (Close Parenthesis) classes,
 * plus EX (Exclamation/Interrogation) as they behave similarly for CJK contexts.
 */
static inline bool is_line_break_cl(uint32_t cp) {
    // ASCII closing brackets
    if (cp == 0x0029 || cp == 0x005D || cp == 0x007D) return true;
    // Tibetan closing marks
    if (cp == 0x0F3B || cp == 0x0F3D) return true;
    // Ogham
    if (cp == 0x169C) return true;
    // Superscript/subscript closing parens
    if (cp == 0x207E || cp == 0x208E) return true;
    // Right-pointing angle bracket
    if (cp == 0x232A) return true;
    // Misc math paired brackets (odd = closing)
    if (cp >= 0x2769 && cp <= 0x2775 && (cp & 1) == 1) return true;
    // Mathematical brackets
    if (cp == 0x27E7 || cp == 0x27E9 || cp == 0x27EB) return true;
    // Misc technical paired brackets (even = closing)
    if (cp >= 0x2984 && cp <= 0x2998 && (cp & 1) == 0) return true;
    if (cp == 0x29D9 || cp == 0x29DB || cp == 0x29FD) return true;
    // CJK comma, full stop (CL class)
    if (cp == 0x3001 || cp == 0x3002) return true;
    // CJK paired closing brackets
    if (cp == 0x3009 || cp == 0x300B || cp == 0x300D || cp == 0x300F ||
        cp == 0x3011 || cp == 0x3015 || cp == 0x3017 || cp == 0x3019 ||
        cp == 0x301B || cp == 0x301E || cp == 0x301F) return true;
    // CJK compatibility forms (vertical, even = closing)
    if (cp >= 0xFE36 && cp <= 0xFE44 && (cp & 1) == 0) return true;
    if (cp == 0xFE48) return true;
    // Small form variants
    if (cp == 0xFE50 || cp == 0xFE52) return true;
    if (cp == 0xFE5A || cp == 0xFE5C || cp == 0xFE5E) return true;
    // Fullwidth forms
    if (cp == 0xFF09 || cp == 0xFF0C || cp == 0xFF0E || cp == 0xFF3D ||
        cp == 0xFF5D || cp == 0xFF60 || cp == 0xFF61 || cp == 0xFF63 ||
        cp == 0xFF64) return true;
    return false;
}

/**
 * UAX #14: CJ (Conditional Japanese Starter) class characters.
 * Resolved to NS in strict/normal mode, to ID in loose mode.
 * CSS Text 3 §6.2: line-break: loose treats these as breakable (ID class).
 */
static inline bool is_line_break_cj(uint32_t cp) {
    // Small hiragana: ぁぃぅぇぉっゃゅょゎゕゖ
    if (cp == 0x3041 || cp == 0x3043 || cp == 0x3045 || cp == 0x3047 || cp == 0x3049) return true;
    if (cp == 0x3063 || cp == 0x3083 || cp == 0x3085 || cp == 0x3087 || cp == 0x308E) return true;
    if (cp == 0x3095 || cp == 0x3096) return true;
    // Small katakana: ァィゥェォッャュョヮヵヶ
    if (cp == 0x30A1 || cp == 0x30A3 || cp == 0x30A5 || cp == 0x30A7 || cp == 0x30A9) return true;
    if (cp == 0x30C3 || cp == 0x30E3 || cp == 0x30E5 || cp == 0x30E7 || cp == 0x30EE) return true;
    if (cp == 0x30F5 || cp == 0x30F6) return true;
    // Prolonged sound mark: ー
    if (cp == 0x30FC) return true;
    // Halfwidth small katakana: ｧｨｩｪｫｬｭｮｯｰ
    if (cp >= 0xFF67 && cp <= 0xFF70) return true;
    return false;
}

/**
 * Check if a codepoint has NS (Non-Starter) line-break class.
 * CSS Text 3 §5.2: No break before NS characters when preceded by CJK.
 * Note: CJ class characters (small kana, prolonged sound mark) are also
 * non-starters in strict/normal mode — use is_line_break_cj() separately.
 */
static inline bool is_line_break_ns(uint32_t cp) {
    // Thai
    if (cp == 0x0E5A || cp == 0x0E5B) return true;
    // Khmer
    if (cp == 0x17D4 || cp == 0x17D6 || cp == 0x17DA) return true;
    // Double exclamation mark
    if (cp == 0x203C) return true;
    // CJK non-starters: iteration marks, prolonged sound marks, etc.
    if (cp == 0x3005 || cp == 0x301C || cp == 0x303B || cp == 0x303C) return true;
    if (cp == 0x309B || cp == 0x309C || cp == 0x309D || cp == 0x309E) return true;
    if (cp == 0x30A0 || cp == 0x30FB || cp == 0x30FD || cp == 0x30FE) return true;
    // Small form variants (semicolon, colon)
    if (cp == 0xFE54 || cp == 0xFE55) return true;
    // Fullwidth colon, semicolon
    if (cp == 0xFF1A || cp == 0xFF1B) return true;
    // Halfwidth forms
    if (cp == 0xFF65 || cp == 0xFF9E || cp == 0xFF9F) return true;
    return false;
}

/**
 * Check if a codepoint is a fullwidth exclamation/question mark.
 * CSS Text 3 §6.2: line-break: loose allows breaks before these in CJK context.
 */
static inline bool is_fullwidth_ex(uint32_t cp) {
    return cp == 0xFF01 || cp == 0xFF1F;  // ！ ？
}

/**
 * UAX #14 LB13: No break before EX, IS, or SY characters.
 * EX = Exclamation/Interrogation (!, ?, etc.)
 * IS = Infix Numeric Separator (., ,, :, etc.)
 * SY = Break Symbols (/)
 * These classes are not covered by is_line_break_cl (CL/CP) or is_line_break_ns (NS).
 */
static inline bool is_line_break_ex_is_sy(uint32_t cp) {
    // EX class: exclamation/interrogation
    if (cp == 0x0021 || cp == 0x003F) return true;   // ! ?
    if (cp == 0x05C6) return true;                    // HEBREW PUNCTUATION NUN HAFUKHA
    if (cp == 0x061B || cp == 0x061E || cp == 0x061F) return true;  // Arabic semicolon/punct/question
    if (cp == 0x06D4) return true;                    // ARABIC FULL STOP
    if (cp == 0xFE15 || cp == 0xFE16) return true;   // Presentation forms for ! ?
    if (cp == 0xFE56 || cp == 0xFE57) return true;   // Small ! ?
    if (cp == 0xFF01 || cp == 0xFF1F) return true;   // Fullwidth ! ?
    // IS class: infix numeric separator
    if (cp == 0x002C || cp == 0x002E) return true;    // , .
    if (cp == 0x003A || cp == 0x003B) return true;    // : ;
    if (cp == 0x037E) return true;                    // GREEK QUESTION MARK
    if (cp == 0x0589) return true;                    // ARMENIAN FULL STOP
    if (cp == 0x060C || cp == 0x060D) return true;    // ARABIC COMMA/DATE SEPARATOR
    if (cp == 0x07F8) return true;                    // NKO COMMA
    if (cp == 0xFE10 || cp == 0xFE13 || cp == 0xFE14) return true;  // Vertical comma/colon/semicolon
    // SY class: break symbols
    if (cp == 0x002F) return true;                    // /
    return false;
}


/**
 * Classify a codepoint into a semantic break kind (CSS Text 3 §4–5 + UAX #14).
 * Called early in the layout loop so each codepoint is classified once.
 * The collapse_spaces / collapse_newlines flags come from the white-space property.
 */
static BreakKind classify_break(uint32_t cp, bool collapse_spaces, bool collapse_newlines) {
    // whitespace (CSS Text 3 §4)
    if (cp == 0x0020) return collapse_spaces ? BRK_SPACE : BRK_PRESERVED_SPACE;
    if (cp == '\t')   return collapse_spaces ? BRK_SPACE : BRK_TAB;
    if (cp == '\n' || cp == '\r') return collapse_newlines ? BRK_SPACE : BRK_HARD_BREAK;

    // non-breaking glue
    if (cp == 0x00A0 || cp == 0x202F) return BRK_GLUE;         // visible NBSP / NNBSP
    if (cp == 0x2060 || cp == 0xFEFF) return BRK_GLUE_ZW;      // zero-width WJ / ZWNBSP
    if (cp == 0x200D) return BRK_ZWJ;                           // zero-width joiner

    // break opportunities
    if (cp == 0x200B) return BRK_ZERO_WIDTH_BREAK;              // ZWSP
    if (cp == 0x00AD) return BRK_SOFT_HYPHEN;                   // SHY
    if (cp == 0x002D || cp == 0x2010) return BRK_HYPHEN;        // hyphen-minus / hyphen
    if (cp == 0x2013 || cp == 0x2014) return BRK_HYPHEN;        // en-dash / em-dash

    // ideographic space
    if (cp == 0x3000) return BRK_IDEOGRAPHIC_SPACE;

    // UAX#14 ID class (CJK, emoji, Yi, etc.) — break-after unless keep-all
    if (has_id_line_break_class(cp)) return BRK_CJK;

    // UAX #14 line break classes
    if (is_line_break_op(cp)) return BRK_OP;
    if (is_line_break_cl(cp)) return BRK_CL;
    if (is_line_break_cj(cp)) return BRK_CJ;
    if (is_line_break_ns(cp)) return BRK_NS;
    if (is_line_break_ex_is_sy(cp)) return BRK_EX_IS_SY;

    return BRK_TEXT;
}


/**
 * Peek at the next Unicode codepoint without advancing the string pointer.
 * Returns 0 if at end of string.
 */
static inline uint32_t peek_codepoint(const unsigned char* str) {
    if (!str || !*str) return 0;
    if (*str < 128) return *str;
    uint32_t cp = 0;
    str_utf8_decode((const char*)str, 4, &cp);
    return cp ? cp : *str;
}

/**
 * Peek at the first codepoint of the next inline content following a given DOM node.
 * Traverses siblings and walks up through inline parents to find the next
 * text character. Used for UAX #14 LB13 cross-span lookahead (no break before IS/SY/EX).
 * Returns 0 if no next inline text is found.
 */
static uint32_t peek_next_inline_codepoint(DomNode* node);

/**
 * Find first text codepoint within a DOM subtree (depth-first).
 * Returns 0 if no text found.
 */
static uint32_t first_text_codepoint_in_subtree(DomNode* node) {
    while (node) {
        if (node->is_text()) {
            const unsigned char* text = node->text_data();
            if (text && *text) {
                while (*text && is_space(*text)) text++;
                if (*text) return peek_codepoint(text);
            }
        } else if (node->is_element()) {
            CssEnum outer_display = resolve_display_value(node).outer;
            if (outer_display == CSS_VALUE_INLINE) {
                DomElement* elmt = (DomElement*)node;
                if (elmt->first_child) {
                    uint32_t cp = first_text_codepoint_in_subtree(elmt->first_child);
                    if (cp) return cp;
                }
            } else {
                return 0;  // non-inline element stops search
            }
        }
        node = node->next_sibling;
    }
    return 0;
}

static uint32_t peek_next_inline_codepoint(DomNode* node) {
    while (node) {
        // check next siblings of this node
        if (node->next_sibling) {
            uint32_t cp = first_text_codepoint_in_subtree(node->next_sibling);
            if (cp) return cp;
        }
        // walk up to parent (only through inline elements)
        DomNode* parent = node->parent;
        if (!parent || !parent->is_element()) break;
        CssEnum parent_display = resolve_display_value(parent).outer;
        if (parent_display != CSS_VALUE_INLINE) break;
        node = parent;
    }
    return 0;
}

/**
 * Check if a codepoint is an emoji that participates in ZWJ (Zero Width Joiner)
 * composition sequences. Only emoji characters form composed glyphs when joined
 * by ZWJ; other scripts (CJK, Latin, etc.) should retain independent advances.
 * Reference: Unicode Technical Standard #51 (Emoji), UAX #29 (Grapheme Clusters)
 */
static inline bool is_emoji_for_zwj(uint32_t cp) {
    return utf_is_emoji_for_zwj(cp);
}

/**
 * Check if a codepoint can serve as the base (left side) of a ZWJ emoji
 * composition sequence. Only specific emoji characters produce composed
 * glyphs when followed by ZWJ + another emoji. Without HarfBuzz text
 * shaping, this heuristic covers the standard Unicode ZWJ sequences.
 * Reference: Unicode UTS #51, emoji-zwj-sequences.txt
 */
static inline bool is_zwj_composition_base(uint32_t cp) {
    return utf_is_zwj_composition_base(cp);
}

/**
 * Get the Unicode-specified width for special space characters.
 * These characters have fixed widths defined by Unicode standard, which browsers
 * enforce regardless of what the font's glyph metrics say.
 * Returns the width as a fraction of 1em, or 0 if the character doesn't have
 * a Unicode-specified width. Returns -1 for zero-width characters.
 *
 * Reference: Unicode Standard, Chapter 6 "Writing Systems and Punctuation"
 */
static inline float get_unicode_space_width_em(uint32_t codepoint) {
    // Emoji skin tone modifiers (U+1F3FB-U+1F3FF) modify the preceding emoji
    // and have zero advance in composed sequences (handled by font shaping)
    if (codepoint >= 0x1F3FB && codepoint <= 0x1F3FF) return -1.0f;

    // Variation Selectors: VS1-VS16 (U+FE00-U+FE0F) and VS17-VS256 (U+E0100-U+E01EF)
    // are default-ignorable characters with zero advance width
    if (codepoint >= 0xFE00 && codepoint <= 0xFE0F) return -1.0f;
    if (codepoint >= 0xE0100 && codepoint <= 0xE01EF) return -1.0f;

    switch (codepoint) {
        // Zero-width characters (return negative to distinguish from "use font width")
        case 0x200B: return -1.0f;  // Zero Width Space (ZWSP) - break opportunity
        case 0x200C: return -1.0f;  // Zero Width Non-Joiner (ZWNJ)
        case 0x200D: return -1.0f;  // Zero Width Joiner (ZWJ)
        case 0x00AD: return -1.0f;  // Soft Hyphen (SHY) - invisible unless line breaks here
        case 0xFEFF: return -1.0f;  // Zero Width No-Break Space (ZWNBSP / BOM)
        case 0x20E3: return -1.0f;  // Combining Enclosing Keycap

        // Unicode spaces with defined widths
        case 0x2000: return 0.5f;   // EN QUAD - width of 'n' (nominally 1/2 em)
        case 0x2001: return 1.0f;   // EM QUAD - width of 'm' (nominally 1 em)
        case 0x2002: return 0.5f;   // EN SPACE - 1/2 em
        case 0x2003: return 1.0f;   // EM SPACE - 1 em
        case 0x2004: return 1.0f/3; // THREE-PER-EM SPACE - 1/3 em
        case 0x2005: return 0.25f;  // FOUR-PER-EM SPACE - 1/4 em
        case 0x2006: return 1.0f/6; // SIX-PER-EM SPACE - 1/6 em
        case 0x2009: return 1.0f/5; // THIN SPACE - ~1/5 em (or 1/6 em)
        case 0x200A: return 1.0f/10; // HAIR SPACE - very thin (~1/10 to 1/16 em)
        // U+2007 FIGURE SPACE and U+2008 PUNCTUATION SPACE are font-dependent
        // so we return 0 to use the font's glyph width
        default: return 0.0f;
    }
}

/**
 * CSS Text 3 §4.1.2: Check if a codepoint has East Asian Width Fullwidth (F) or Wide (W).
 * Used for segment break transformation rules: segment breaks between two
 * East Asian F/W characters (neither Hangul) are removed instead of becoming spaces.
 * utf8proc_charwidth returns 2 for F and W characters, 1 for all others.
 */
static inline bool is_east_asian_fw(uint32_t cp) {
    return utf8proc_charwidth(cp) == 2;
}

/**
 * CSS Text 3 §4.1.2: Check if a codepoint is Hangul.
 * Segment break removal between East Asian Wide characters does not apply
 * when either side is Hangul.
 */
static inline bool is_hangul(uint32_t cp) {
    return utf_is_hangul(cp);
}

/**
 * CSS Text 3 §4.1.1: Check if a codepoint is a Unicode space separator
 * (general category Zs) other than U+0020 SPACE and U+00A0 NO-BREAK SPACE.
 * These are rendered as zero-width when white-space is collapsible.
 */
static inline bool is_other_space_separator(uint32_t cp) {
    return cp == 0x1680 ||                      // OGHAM SPACE MARK
           (cp >= 0x2000 && cp <= 0x200A) ||    // EN QUAD through HAIR SPACE
           cp == 0x202F ||                      // NARROW NO-BREAK SPACE
           cp == 0x205F ||                      // MEDIUM MATHEMATICAL SPACE
           cp == 0x3000;                        // IDEOGRAPHIC SPACE
}

/**
 * CSS Text 3 §5.2: Check if a character is a "typographic letter unit" for
 * word-break: break-all. break-all only converts letters and numbers to ID
 * class for line-breaking purposes; punctuation and other characters keep
 * their original line-break behavior.
 * Typographic letter units = Unicode General Category L* (letters) and N* (numbers).
 */
static inline bool is_typographic_letter_unit(uint32_t cp) {
    utf8proc_category_t cat = utf8proc_category(cp);
    return (cat >= UTF8PROC_CATEGORY_LU && cat <= UTF8PROC_CATEGORY_LO) ||  // L*: letters
           (cat >= UTF8PROC_CATEGORY_ND && cat <= UTF8PROC_CATEGORY_NO);    // N*: numbers
}

/**
 * Check if whitespace should be collapsed according to white-space property.
 * Returns true for: normal, nowrap, pre-line
 * Returns false for: pre, pre-wrap, break-spaces
 */
static inline bool ws_collapse_spaces(CssEnum ws) {
    return ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_NOWRAP ||
           ws == CSS_VALUE_PRE_LINE || ws == 0;  // 0 = undefined, treat as normal
}

/**
 * Check if newlines should be collapsed (treated as spaces).
 * Returns true for: normal, nowrap
 * Returns false for: pre, pre-wrap, pre-line, break-spaces
 */
static inline bool ws_collapse_newlines(CssEnum ws) {
    return ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_NOWRAP || ws == 0;
}

/**
 * Check if lines should wrap at soft break opportunities.
 * Returns true for: normal, pre-wrap, pre-line, break-spaces
 * Returns false for: nowrap, pre
 */
static inline bool ws_wrap_lines(CssEnum ws) {
    return ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_PRE_WRAP ||
           ws == CSS_VALUE_PRE_LINE || ws == CSS_VALUE_BREAK_SPACES || ws == 0;
}

/**
 * Check if a white-space value is concrete (not inherit/initial/unset/revert).
 * These special values need to be resolved by walking up the parent chain.
 */
static inline bool is_concrete_white_space_value(CssEnum ws) {
    return ws != CSS_VALUE_INHERIT &&
           ws != CSS_VALUE_INITIAL &&
           ws != CSS_VALUE_UNSET &&
           ws != CSS_VALUE_REVERT;
}

/**
 * Get the white-space property value from the text node's ancestor chain.
 * Walks up from the text node to find the nearest element with a white_space value set.
 * This properly handles inline elements like <span style="white-space: pre">.
 *
 * white-space is an inherited property, so we check:
 * 1. The resolved blk->white_space (for block elements)
 * 2. Skip INHERIT/INITIAL/UNSET/REVERT and continue walking up
 */
CssEnum get_white_space_value(DomNode* node) {
    // Walk up parent chain starting from the text node's parent
    DomNode* current = node ? node->parent : nullptr;
    while (current) {
        // PDF and other non-DOM view trees may have non-element parents
        // Only process if it's a proper DomElement
        if (!current->is_element()) {
            // Not a DomElement - this can happen with PDF view trees
            // Return default white-space value
            return CSS_VALUE_NORMAL;
        }
        DomElement* elem = static_cast<DomElement*>(current);
        // Check resolved BlockProp first (fastest path for blocks)
        if (elem->blk && elem->blk->white_space != 0) {
            CssEnum ws = elem->blk->white_space;
            // Skip INHERIT/INITIAL/UNSET/REVERT - continue walking up
            if (is_concrete_white_space_value(ws)) {
                return ws;
            }
        }
        // Fallback: check specified_style when blk is not yet resolved
        // (e.g. during intrinsic sizing measurement before full layout)
        if (elem->specified_style) {
            CssDeclaration* ws_decl = style_tree_get_declaration(
                elem->specified_style, CSS_PROPERTY_WHITE_SPACE);
            if (ws_decl && ws_decl->value && ws_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum ws = ws_decl->value->data.keyword;
                if (is_concrete_white_space_value(ws)) {
                    return ws;
                }
            }
        }
        current = current->parent;
    }
    return CSS_VALUE_NORMAL;  // default
}

// ============================================================================
// Intrinsic Sizing Mode Helpers
// ============================================================================

/**
 * Check if layout is in min-content measurement mode.
 * In min-content mode, break at every opportunity (every word boundary).
 */
static inline bool is_min_content_mode(LayoutContext* lycon) {
    return lycon->available_space.width.is_min_content();
}

/**
 * Check if layout is in max-content measurement mode.
 * In max-content mode, never break lines - measure full unwrapped width.
 */
static inline bool is_max_content_mode(LayoutContext* lycon) {
    return lycon->available_space.width.is_max_content();
}

/**
 * Check if line should break based on intrinsic sizing mode.
 * Returns true if line is full and should break.
 *
 * In min-content mode: always break at word boundaries
 * In max-content mode: never break (infinite available width)
 * In normal mode: break when line is full
 */
static inline bool should_break_line(LayoutContext* lycon, float current_x, float width) {
    if (is_max_content_mode(lycon)) {
        // Never break in max-content mode
        return false;
    }
    // Use effective_right which accounts for float intrusions
    // effective_right is adjusted per-line based on floats at current Y
    float line_right = lycon->line.has_float_intrusion ?
                       lycon->line.effective_right : lycon->line.right;
    // CSS 2.1 §16.1: RTL text-indent narrows the wrap boundary from the right
    line_right -= lycon->line.text_indent_offset;
    if (is_min_content_mode(lycon)) {
        // In min-content mode, we break at every opportunity
        return current_x + width > line_right;
    }
    // Normal mode: check if line is full
    return current_x + width > line_right;
}

// ============================================================================
// BlockContext-aware Line Adjustment
// ============================================================================

/**
 * Update effective line bounds based on floats in the current BlockContext.
 * Called at line start and potentially mid-line when floats are encountered.
 *
 * Uses the new unified BlockContext API instead of the old BFC system.
 */
void update_line_for_bfc_floats(LayoutContext* lycon, float query_height) {
    // Find the BFC root for this layout context
    BlockContext* bfc = block_context_find_bfc(&lycon->block);

    if (!bfc) {
        // No BFC - effective bounds same as normal bounds
        lycon->line.effective_left = lycon->line.left;
        lycon->line.effective_right = lycon->line.right;
        lycon->line.has_float_intrusion = false;
        return;
    }

    // Get current view
    ViewBlock* current_view = (ViewBlock*)lycon->view;
    if (!current_view || !current_view->is_block()) {
        lycon->line.effective_left = lycon->line.left;
        lycon->line.effective_right = lycon->line.right;
        lycon->line.has_float_intrusion = false;
        return;
    }

    // Use cached BFC offset from BlockContext
    float offset_x = lycon->block.bfc_offset_x;
    float offset_y = lycon->block.bfc_offset_y;

    float current_y_local = lycon->block.advance_y;
    float current_y_bfc = current_y_local + offset_y;
    // CSS 2.1 §9.5.1: For inline-blocks, query using the element's full height
    // so floats whose top is below the line start but within the element's height
    // are properly accounted for.
    float effective_height = query_height > 0 ? query_height :
        (lycon->block.line_height > 0 ? lycon->block.line_height : 16.0f);

    log_debug("  DEBUG: line adjustment, y_local=%.1f, offset_y=%.1f, y_bfc=%.1f",
        current_y_local, offset_y, current_y_bfc);

    // Query available space at this Y using BlockContext API (in BFC coordinates)
    FloatAvailableSpace space = block_context_space_at_y(bfc, current_y_bfc, effective_height);

    // Convert from BFC coordinates to local coordinates
    float local_space_left = space.left - offset_x;
    float local_space_right = space.right - offset_x;

    // Clamp to block's content area
    float local_left = fmax(local_space_left, lycon->line.left);
    float local_right = fmin(local_space_right, lycon->line.right);

    // Update effective bounds only if actual floats constrain the space.
    // Without this check, a coordinate system mismatch between BFC content-area
    // coordinates and line border-box coordinates causes false "float intrusion"
    // for any BFC root with border or padding, even when no floats exist.
    bool has_actual_float = space.has_left_float || space.has_right_float;
    if (has_actual_float && (local_left > lycon->line.left || local_right < lycon->line.right)) {
        lycon->line.effective_left = local_left;
        lycon->line.effective_right = local_right;
        lycon->line.has_float_intrusion = true;

        // If advance_x is before effective_left, move it — but respect
        // negative text-indent which legitimately places items before line.left
        if (lycon->line.advance_x < lycon->line.effective_left &&
            lycon->line.advance_x >= lycon->line.left) {
            lycon->line.advance_x = lycon->line.effective_left;
        }

        log_debug("[BlockContext] Line adjusted for floats: effective (%.1f, %.1f), y_bfc=%.1f",
                  lycon->line.effective_left, lycon->line.effective_right, current_y_bfc);
    } else {
        lycon->line.effective_left = lycon->line.left;
        lycon->line.effective_right = lycon->line.right;
        lycon->line.has_float_intrusion = false;
    }
}

// Forward declarations
LineFillStatus node_has_line_filled(LayoutContext* lycon, DomNode* node);
LineFillStatus text_has_line_filled(LayoutContext* lycon, DomNode* text_node);
LineFillStatus span_has_line_filled(LayoutContext* lycon, DomNode* span) {
    DomNode* node = nullptr;
    if (span->is_element()) {
        node = static_cast<DomElement*>(span)->first_child;
    }
    if (node) {
        LineFillStatus result = node_has_line_filled(lycon, node);
        if (result) { return result; }
    }
    return RDT_NOT_SURE;
}

// Forward declaration (defined below output_text)
void adjust_text_bounds(ViewText* text);

// Forward declaration from layout_inline.cpp
extern void compute_span_bounding_box(ViewSpan* span, bool is_multi_line, struct FontHandle* fallback_fh);

/**
 * After trimming a text rect's trailing space, update the parent ViewText bounds
 * and shrink ancestor ViewSpan widths by the same amount.  This is safe to call
 * mid-layout because it only subtracts the known trim amount — it does NOT call
 * compute_span_bounding_box which can produce wrong results when some children
 * haven't been positioned yet (e.g., block-in-inline cases).
 */
static void propagate_text_trim(ViewText* text_view, float trim_amount) {
    // After trimming a trailing-space text rect in line_break(), recompute
    // the ViewText bounding box from its (now-trimmed) TextRects.
    adjust_text_bounds(text_view);

    // Walk up parent ViewSpan chain and shrink widths when the trimmed text
    // was the rightmost content in the span. If other content (e.g. inline-table)
    // extends beyond the text, the span's bounding box is determined by that
    // content and should not change.
    ViewElement* parent = text_view->parent_view();
    while (parent && parent->view_type == RDT_VIEW_INLINE) {
        float span_right = parent->x + parent->width;
        // Compute the span's content-area right edge (inside border+padding).
        // exit_inline_box's compute_span_bounding_box already accounts for
        // border+padding, so the content ends before those decorations.
        float content_right = span_right;
        if (parent->bound) {
            if (parent->bound->border)
                content_right -= parent->bound->border->width.right;
            if (parent->bound->padding.right > 0)
                content_right -= parent->bound->padding.right;
        }
        float old_text_right = text_view->x + text_view->width + trim_amount;
        // The text was at the right edge of the span before trimming — the span
        // needs to shrink by the same amount. If the text's original right edge
        // was well inside the span content area, other content determines the
        // span width.
        if ((int)old_text_right < (int)content_right) { // INT_CAST_OK: intentional
            break;  // text was not at the right edge; span width unaffected
        }
        // If span was already trimmed by exit_inline_box (content right <=
        // post-trim text right), no further adjustment needed.
        float text_right = text_view->x + text_view->width;
        if ((int)text_right >= (int)content_right) { // INT_CAST_OK: intentional
            break;
        }
        float new_width = parent->width - trim_amount;
        if (new_width < 0) new_width = 0;
        parent->width = new_width;
        parent = parent->parent_view();
    }
}

void line_reset(LayoutContext* lycon) {
    log_debug("initialize new line");
    lycon->line.max_ascender = lycon->line.max_descender = 0;
    lycon->line.is_line_start = true;  lycon->line.has_space = false;
    lycon->line.last_space = NULL;  lycon->line.last_space_pos = 0;  lycon->line.last_space_kind = BRK_TEXT;
    lycon->line.start_view = NULL;
    lycon->line.line_start_font = lycon->font;
    lycon->line.prev_glyph_index = 0; // reset kerning state
    lycon->line.prev_codepoint = 0;   // reset codepoint kerning state

    // IMPORTANT: Reset effective bounds to container bounds before float adjustment
    // line.left/right are the container bounds, set once in line_init()
    // effective_left/right are recalculated per line based on floats at that Y
    lycon->line.effective_left = lycon->line.left;
    lycon->line.effective_right = lycon->line.right;
    lycon->line.has_float_intrusion = false;
    lycon->line.has_replaced_content = false;
    lycon->line.has_cjk_text = false;
    lycon->line.max_top_bottom_height = 0;
    lycon->line.max_top_height = 0;
    lycon->line.max_bottom_height = 0;
    lycon->line.max_desc_before_last_text = 0;
    lycon->line.has_expanded_inline_lh = false;
    lycon->line.has_different_inline_font = false;
    lycon->line.max_normal_line_height = 0;
    lycon->line.trailing_letter_spacing = 0;
    // CSS 2.1 §10.8.1: Initialize parent font metrics from block's init values.
    // These are the correct "parent" for top-level inline content.
    // span_vertical_align will override with actual parent span font when recursing.
    lycon->line.parent_font_ascender = lycon->block.init_ascender;
    lycon->line.parent_font_descender = lycon->block.init_descender;
    lycon->line.parent_font_size = lycon->font.style ? lycon->font.style->font_size : (lycon->block.init_ascender + lycon->block.init_descender);
    lycon->line.parent_font_handle = lycon->font.font_handle;
    lycon->line.last_text_rect = NULL;
    lycon->line.last_text_view = NULL;
    lycon->line.trailing_space_width = 0;
    lycon->line.committed_trailing_rect = NULL;
    lycon->line.committed_trailing_view = NULL;
    lycon->line.committed_trailing_space = 0;
    lycon->line.hanging_space_width = 0;
    lycon->line.hanging_space_text_trim = 0;
    lycon->line.rtl_hanging_space = 0;
    lycon->line.last_space_hanging_width = 0;
    lycon->line.last_space_hanging_text_trim = 0;
    lycon->line.wrap_opportunity_before_nowrap = false;
    lycon->line.is_last_line = false;
    lycon->line.advance_x = lycon->line.left;  // Start at container left

    // CSS 2.1 §8.3: Re-apply pending inline left edges from spans that haven't
    // produced content yet. When an inline span's margin+border+padding is added
    // but its first content wraps to a new line, the edges must follow to the new
    // line because that's where the span's first line fragment actually starts.
    lycon->line.advance_x += lycon->line.inline_start_edge_pending;

    // Adjust effective bounds for floats at current Y position using BlockContext
    // This must happen BEFORE text-indent, because CSS 2.1 §16.1 says indent is
    // "with respect to the left (or right) edge of the line box" — which is the
    // float-adjusted edge, not the container edge.
    BlockContext* bfc = block_context_find_bfc(&lycon->block);
    if (bfc) {
        // Use unified line adjustment via BlockContext
        adjust_line_for_floats(lycon);
        log_debug("DEBUG: Used BlockContext %p for line adjustment", (void*)bfc);
    }

    // CSS 2.1 §16.1: text-indent applies only to the first formatted line of a block container
    // Apply text-indent AFTER float adjustment so indent is additive with float offsets
    lycon->line.text_indent_offset = 0;
    if (lycon->block.is_first_line && lycon->block.text_indent != 0) {
        if (lycon->block.direction == CSS_VALUE_RTL) {
            // CSS 2.1 §16.1: In RTL, text-indent indents from the right (starting) edge
            // Store the offset to narrow wrap boundary and alignment width from the right
            lycon->line.text_indent_offset = lycon->block.text_indent;
            log_debug("Applied RTL text-indent: %.1fpx (narrows right edge)",
                      lycon->block.text_indent);
        } else {
            // LTR: indent from the left (starting) edge
            lycon->line.advance_x += lycon->block.text_indent;
            lycon->line.effective_left += lycon->block.text_indent;
            log_debug("Applied text-indent: %.1fpx, advance_x=%.1f",
                      lycon->block.text_indent, lycon->line.advance_x);
        }
        // After applying text-indent for the first line, mark it as done
        lycon->block.is_first_line = false;
    }
}

void line_init(LayoutContext* lycon, float left, float right) {
    lycon->line.left = left;  lycon->line.right = right;
    // Initialize effective bounds to full width (will be adjusted for floats later)
    lycon->line.effective_left = left;
    lycon->line.effective_right = right;
    lycon->line.has_float_intrusion = false;
    lycon->line.inline_start_edge_pending = 0;  // no pending inline edges at block start
    line_reset(lycon);
    lycon->line.vertical_align = CSS_VALUE_BASELINE;  // vertical-align does not inherit
    lycon->line.vertical_align_offset = 0;
}

/**
 * Recursively fix height of collapsed inline spans on a line with visible content.
 * CSS 2.1 §10.8.1: Empty inline elements should report height = line-height
 * when on a line that has visible content (the "strut" concept).
 * compute_span_bounding_box sets 0×0 for truly empty spans; this restores
 * their height from the pre-stored content_height (= block's line-height).
 */
static void fixup_collapsed_inline_spans(ViewSpan* span) {
    // recurse into children first (depth-first)
    View* child = span->first_child;
    while (child) {
        if (child->view_type == RDT_VIEW_INLINE) {
            fixup_collapsed_inline_spans((ViewSpan*)child);
        }
        child = child->next();
    }
    // fix this span if collapsed
    if (span->height == 0) {
        DomElement* elem = static_cast<DomElement*>((DomNode*)span);
        if (elem->content_height > 0) {
            span->height = elem->content_height;
        }
    }
}

void line_break(LayoutContext* lycon) {
    // CSS 2.1 §16.6.1: For normal/nowrap/pre-line white-space, trailing spaces
    // at the end of a line are removed. Trim the last text rect's width.
    if (lycon->line.trailing_space_width > 0 && lycon->line.last_text_rect) {
        float trim_amount = lycon->line.trailing_space_width;
        lycon->line.last_text_rect->width -= trim_amount;
        lycon->line.advance_x -= trim_amount;
        lycon->line.trailing_space_width = 0;
        // Update the ViewText bounds and parent ViewSpan bounding boxes
        if (lycon->line.last_text_view) {
            propagate_text_trim(lycon->line.last_text_view, trim_amount);
        }
        lycon->line.committed_trailing_rect = NULL;
        lycon->line.committed_trailing_view = NULL;
        lycon->line.committed_trailing_space = 0;
    }
    // CSS 2.1 §16.6.1: Cross-node trailing space trimming. When a text rect
    // with trailing space was output, then subsequent inline content was processed
    // (clearing the live trailing_space_width), and finally a line break occurs
    // with the trailing-space rect still being the last text on the line,
    // trim it using the committed trailing space info.
    else if (lycon->line.committed_trailing_space > 0 && lycon->line.committed_trailing_rect) {
        float trim_amount = lycon->line.committed_trailing_space;
        lycon->line.committed_trailing_rect->width -= trim_amount;
        lycon->line.advance_x -= trim_amount;
        // Update the ViewText bounds and parent ViewSpan bounding boxes
        if (lycon->line.committed_trailing_view) {
            propagate_text_trim(lycon->line.committed_trailing_view, trim_amount);
        }
        lycon->line.committed_trailing_rect = NULL;
        lycon->line.committed_trailing_view = NULL;
        lycon->line.committed_trailing_space = 0;
    }
    // CSS Text 3 §4.1.3: Hanging spaces (U+3000, pre-wrap spaces) at end of line
    // don't contribute to the line box width for min/max-width calculations.
    // Store how much of the trailing space should be subtracted from text node
    // JSON output. Browsers exclude hanging ASCII spaces from text node
    // getBoundingClientRect(), but INCLUDE them in parent span bounds. U+3000
    // ideographic space has a visible glyph and is NOT trimmed.
    // NOTE: Only trim when the text rect has non-whitespace content remaining
    // after the trim. When the entire text rect is hanging whitespace (e.g. " "),
    // browsers include it in the text node bounds.
    if (lycon->line.hanging_space_width > 0) {
        float hang_trim = lycon->line.hanging_space_text_trim;
        if (hang_trim > 0 && lycon->line.last_text_rect) {
            float remaining = lycon->line.last_text_rect->width - hang_trim;
            if (remaining > 0.01f) {
                // Store trim on rect for JSON output; keep rect->width untrimmed
                // so ViewText bounds (used by span bounding box) stay full-width.
                lycon->line.last_text_rect->hanging_trim = hang_trim;
            }
        }
        // CSS Text 3 §4.1.3: In RTL, trailing whitespace hangs past the inline-end
        // (left edge). Save the hanging width so line_align() can shift the last text
        // rect's x position leftward after alignment.
        if (lycon->block.direction == CSS_VALUE_RTL) {
            lycon->line.rtl_hanging_space = lycon->line.hanging_space_width;
        }
        lycon->line.advance_x -= lycon->line.hanging_space_width;
        lycon->line.hanging_space_width = 0;
        lycon->line.hanging_space_text_trim = 0;
    }
    lycon->block.max_width = max(lycon->block.max_width, lycon->line.advance_x);
    // CSS Text 3 §8: Letter-spacing must not be applied at the end of a line.
    // Subtract the trailing letter-spacing from advance_x (line width) for
    // alignment purposes (center, right, justify). Do NOT subtract before
    // max_width, as browsers include trailing letter-spacing in intrinsic sizing.
    if (lycon->line.trailing_letter_spacing != 0) {
        lycon->line.advance_x -= lycon->line.trailing_letter_spacing;
        lycon->line.trailing_letter_spacing = 0;
    }

    // CSS Inline §5.2.1: When trailing whitespace is "collapsed away" at the end of a line,
    // it should not contribute to line height. If the last text rect was entirely trailing
    // whitespace (width <= 0 after trimming), rollback its max_descender contribution.
    // This uses the saved max_descender from before the last output_text call.
    // Guard: only apply on lines with replaced/inline-block content, where the trailing
    // whitespace between text and replaced element inflates the descender incorrectly.
    if (lycon->line.has_replaced_content &&
        lycon->line.last_text_rect && lycon->line.last_text_rect->width <= 0 &&
        lycon->line.max_descender > lycon->line.max_desc_before_last_text) {
        log_debug("line_break: rolling back trailing whitespace descender (was %.1f, restoring %.1f)",
            lycon->line.max_descender, lycon->line.max_desc_before_last_text);
        lycon->line.max_descender = lycon->line.max_desc_before_last_text;
    }
    // CSS 2.1 §10.8.1: The strut is a zero-width inline box with the block's font
    // and line-height. Run vertical alignment when:
    // 1) inline content exceeds the strut (original condition), OR
    // 2) a different inline font needs baseline alignment with the strut
    //    (e.g., 32px caption text inside 128px Ahem div).
    // 3) top/bottom-aligned inline content is present (needs second-pass positioning).
    // Case 2 is guarded by has_different_inline_font to avoid triggering for
    // same-font tight line-height (line-height < font-height) where max_ascender
    // is reduced by negative half-leading, not by a different font.
    if (lycon->line.max_ascender > lycon->block.init_ascender ||
        lycon->line.max_descender > lycon->block.init_descender ||
        lycon->line.has_different_inline_font ||
        lycon->line.max_top_bottom_height > 0 ||
        lycon->line.max_top_height > 0 ||
        lycon->line.max_bottom_height > 0) {
        // apply vertical alignment
        log_debug("apply vertical adjustment for the line");
        View* view = lycon->line.start_view;
        if (view) {
            FontBox pa_font = lycon->font;
            lycon->font = lycon->line.line_start_font;
            // Reset parent font metrics to block-level for the vertical alignment pass.
            // span_vertical_align will update these per-span as it recurses.
            lycon->line.parent_font_ascender = lycon->block.init_ascender;
            lycon->line.parent_font_descender = lycon->block.init_descender;
            lycon->line.parent_font_size = lycon->font.style ? lycon->font.style->font_size
                : (lycon->block.init_ascender + lycon->block.init_descender);
            lycon->line.parent_font_handle = lycon->font.font_handle;
            bool end_of_line = false;
            NEXT_VIEW:
            View * vw = view;
            do {
                view_vertical_align(lycon, vw);
                if (vw == lycon->view) { break; } // reached the last view in the line
                vw = vw->next();
            } while (vw);
            if (vw != lycon->view) { // need to go parent level
                view = view->parent;
                if (view) { view = view->next(); }
                if (view) goto NEXT_VIEW;
            }
            lycon->font = pa_font;
        }
    }
    // else no change to vertical alignment

    // horizontal text alignment
    line_align(lycon);

    // CSS Text 3 §4.1.3: RTL hanging space text rect adjustment.
    // In RTL, trailing whitespace hangs past the inline-end (left edge).
    // After alignment positions visible content, shift the last text rect's x
    // leftward so the rect includes the hanging trailing space.
    if (lycon->line.rtl_hanging_space > 0 && lycon->line.last_text_rect) {
        lycon->line.last_text_rect->x -= lycon->line.rtl_hanging_space;
        lycon->line.rtl_hanging_space = 0;
    }

    // advance to next line
    // CSS 2.1 10.8.1: Line height controls vertical spacing between line boxes
    // When line-height is explicitly set (e.g., line-height: 1), use it exactly
    // even if it's smaller than font metrics (allowing lines to overlap)
    float font_line_height = lycon->line.max_ascender + lycon->line.max_descender;
    float css_line_height = lycon->block.line_height;
    log_debug("line_break metrics: max_ascender=%.1f, max_descender=%.1f, font_lh=%.1f, css_lh=%.1f, has_replaced=%d, line_height_is_normal=%d",
        lycon->line.max_ascender, lycon->line.max_descender, font_line_height, css_line_height,
        lycon->line.has_replaced_content, lycon->block.line_height_is_normal);

    // Only fall back to font-based line height when line-height is unset/invalid
    // CSS 2.1: line-height: 0 is a valid explicit value (not a fallback case)
    if (lycon->block.line_height_is_normal && css_line_height <= 0) {
        css_line_height = font_line_height;
    }

    // CSS 2.1 10.8.1 half-leading model:
    // When line-height is explicitly set, the inline box height equals line-height,
    // but inline-blocks and other replaced elements may extend the line box further.
    // Use max(css_line_height, font_line_height) to accommodate all inline content.
    //
    // CSS 2.1 §10.8.1: For lines with inline-blocks/replaced elements, always use
    // max(css_lh, font_lh) when font_lh > css_lh, regardless of the difference.
    // The 2px tolerance only applies to text-only content where FreeType rounding
    // can inflate font metrics by 1-2px beyond the CSS line-height.
    bool has_mixed_fonts;
    if (lycon->line.has_replaced_content && font_line_height > css_line_height) {
        // Inline-block/replaced element expands the line box: always respect it.
        has_mixed_fonts = true;
    } else {
        // Text-only: apply 2px tolerance for FreeType rounding artifacts.
        has_mixed_fonts = (font_line_height > css_line_height + 2);
    }
    float used_line_height;

    // CSS 2.1 §10.8.1: FreeType rounds font metrics (ascender/descender) to integer
    // pixels, which may inflate their sum beyond the normal line-height by 1-2px.
    // When vertical-align offsets expand the line box, this rounding error propagates.
    // Correct by subtracting the small excess from the base font metrics.
    // Only apply this correction for text-only lines (not when replaced content expands).
    float base_metric_excess = (lycon->block.init_ascender + lycon->block.init_descender) - css_line_height;
    if (base_metric_excess > 0 && base_metric_excess <= 2 && !lycon->line.has_replaced_content &&
        font_line_height > css_line_height + 2) {
        font_line_height -= base_metric_excess;
        // Recheck has_mixed_fonts after correction
        has_mixed_fonts = (font_line_height > css_line_height + 2);
    }

    if (has_mixed_fonts) {
        // CSS 2.1 §10.8.1: The line box height is the distance between the
        // uppermost box top and the lowermost box bottom. Each inline box's
        // height equals its own line-height (via half-leading), so max_ascender
        // + max_descender already reflects the correct line box extent.
        // The block's line-height contributes only through the strut, not as a cap.
        //
        // However, when all inlines share the parent's line-height, the
        // max_ascender/max_descender sum may overstate the actual line box height
        // due to different ascender/descender ratios across fonts (both values
        // are clamped ≥0, so they can't represent overlapping extents).
        // In that case, css_line_height is the correct used line height.
        if (lycon->line.has_replaced_content || lycon->block.line_height_is_normal ||
            lycon->line.has_expanded_inline_lh) {
            used_line_height = max(css_line_height, font_line_height);
            // CSS 2.1 §10.8.1: For normal line-height with mixed fonts, each inline box
            // contributes its own font's normal line-height (including lineGap).
            // Use the maximum normal LH tracked across all inline boxes on this line.
            if (lycon->block.line_height_is_normal && lycon->line.max_normal_line_height > used_line_height) {
                used_line_height = lycon->line.max_normal_line_height;
            }
        } else {
            // Text-only with explicit line-height and no per-inline expansion:
            // trust css_line_height (max_ascender + max_descender can overstate
            // due to clamping ≥0 when half-leading produces negative descenders)
            used_line_height = css_line_height;
        }
    } else {
        // Uniform text-only content - use CSS line height as specified
        used_line_height = css_line_height;
    }

    // Chrome blends system CJK font metrics for lines containing CJK characters.
    // When the primary font's normal line-height is smaller than the CJK system
    // font's line-height (e.g., Ahem@16px→16 vs PingFang SC@16px→22), Chrome uses
    // the larger value to prevent CJK glyphs from overlapping between lines.
    if (lycon->line.has_cjk_text && lycon->block.line_height_is_normal) {
        float cjk_lh = get_cjk_system_line_height(lycon->line.parent_font_size);
        if (cjk_lh > used_line_height) {
            log_debug("CJK line-height blending: %.1f → %.1f (CJK system font)",
                      used_line_height, cjk_lh);
            used_line_height = cjk_lh;
        }
    }

    // CSS 2.1 §10.8.1: Fix height of collapsed-content inline elements.
    // Inline elements whose content all collapsed (e.g., <em> </em>) get 0×0 from
    // compute_span_bounding_box. However, when the line has visible content, the
    // inline box should still show height = its line-height (the "strut" concept).
    // Walk the view tree from start_view and fix any marked collapsed inline spans,
    // recursing into arbitrarily nested inline children.
    // CSS 2.1 §9.4.2: Line boxes with no text/content/etc. are zero-height, so
    // only fix when used_line_height > 0 (line has actual visible content).
    if (used_line_height > 0 && lycon->line.start_view) {
        View* v = lycon->line.start_view;
        DomNode* line_parent = ((DomNode*)v)->parent;
        while (v) {
            if (v->view_type == RDT_VIEW_INLINE) {
                fixup_collapsed_inline_spans((ViewSpan*)v);
            }
            DomNode* next = ((DomNode*)v)->next_sibling;
            if (!next || ((DomNode*)v)->parent != line_parent) break;
            v = (View*)next;
        }
    }

    // CSS 2.1 §10.8.1 second pass: expand line box for vertical-align:top/bottom elements.
    // These elements don't participate in baseline-relative height calculation (first pass).
    // If any top/bottom-aligned element is taller than the first-pass line box, expand it.
    float max_tb = max(lycon->line.max_top_bottom_height,
        max(lycon->line.max_top_height, lycon->line.max_bottom_height));
    if (max_tb > used_line_height) {
        used_line_height = max_tb;
    }

    lycon->block.advance_y += used_line_height;

    // CSS 2.1 10.8.1: Track last line's baseline offset for inline-block baseline alignment.
    // The baseline of an inline-block is the baseline of its last line box.
    // Store the distance from the block's border-box top to the baseline:
    //   advance_y (which includes border_top + pad_top + all preceding line heights)
    //   - used_line_height (back to this line's top)
    //   + max_ascender (from line top to baseline)
    lycon->block.last_line_ascender = lycon->block.advance_y - used_line_height + lycon->line.max_ascender;

    // CSS Flexbox §9.4: Track first line's baseline offset for flex baseline alignment.
    // For block containers with in-flow line boxes, the first baseline is the
    // baseline of the first line box. Store distance from border-box top.
    if (lycon->block.first_line_ascender == 0) {
        lycon->block.first_line_ascender = lycon->block.last_line_ascender;
    }

    // CSS Inline 3 §5: Track first/last line box metrics for text-box-trim.
    // max_ascender/max_descender capture the full line box extent including
    // contributions from tall inline descendants (e.g., font-size: 200% spans).
    if (lycon->block.first_line_max_ascender == 0 && lycon->block.first_line_max_descender == 0) {
        lycon->block.first_line_max_ascender = lycon->line.max_ascender;
        lycon->block.first_line_max_descender = lycon->line.max_descender;
    }
    lycon->block.last_line_max_ascender = lycon->line.max_ascender;
    lycon->block.last_line_max_descender = lycon->line.max_descender;

    // reset the new line
    line_reset(lycon);
}

// CSS Text 3 §5.2: Measure the width of the first word starting from `str`.
// Used to check whether to wrap before placing text when the remaining space
// on a line is too narrow for the first word. Leading spaces are skipped.
// Returns 0 if the text is empty or starts at a break opportunity (CJK, ZWSP).
static float measure_first_word_width(LayoutContext* lycon, const unsigned char* str,
                                      const unsigned char* text_end, CssEnum text_transform) {
    float width = 0.0f;
    bool word_start = true;

    // Skip leading spaces — they are break opportunities, not part of the word
    while (*str && is_space(*str)) str++;
    if (!*str) return 0.0f;

    while (str < text_end && *str && !is_space(*str)) {
        uint32_t codepoint = *str;
        int char_bytes = 1;
        if (codepoint >= 128) {
            int bytes = str_utf8_decode((const char*)str, (size_t)(text_end - str), &codepoint);
            if (bytes > 0) char_bytes = bytes;
        }

        // ID-class characters are individual break opportunities — first "word" is one character
        if (has_id_line_break_class(codepoint)) {
            if (width == 0.0f) {
                // The first char is CJK — measure just this one character
                uint32_t tt_out[3];
                int tt_count = apply_text_transform_full(codepoint, text_transform, word_start, tt_out);
                codepoint = tt_out[0];
                GlyphInfo ginfo = font_get_glyph(lycon->font.font_handle, codepoint);
                width += (ginfo.id != 0) ? ginfo.advance_x : lycon->font.current_font_size;
                // Add advance for extra codepoints from full case mapping
                for (int tti = 1; tti < tt_count; tti++) {
                    GlyphInfo eg = font_get_glyph(lycon->font.font_handle, tt_out[tti]);
                    if (eg.id != 0) width += eg.advance_x;
                }
            }
            break;
        }
        // U+200B ZWSP is a break opportunity
        if (codepoint == 0x200B) break;
        // U+00AD soft hyphen is a break opportunity
        if (codepoint == 0x00AD) break;

        {
        uint32_t tt_out[3];
        int tt_count = apply_text_transform_full(codepoint, text_transform, word_start, tt_out);
        codepoint = tt_out[0];
        // Add advance widths for extra codepoints from full case mapping
        for (int tti = 1; tti < tt_count; tti++) {
            GlyphInfo eg = font_get_glyph(lycon->font.font_handle, tt_out[tti]);
            if (eg.id != 0) width += eg.advance_x + lycon->font.style->letter_spacing;
        }
        }
        bool is_small_caps_lower = false;
        if (has_small_caps(lycon)) {
            uint32_t original = codepoint;
            codepoint = apply_small_caps(codepoint);
            is_small_caps_lower = (codepoint != original);
        }
        word_start = false;

        float char_width;
        float unicode_space_em = get_unicode_space_width_em(codepoint);
        if (unicode_space_em < 0.0f) {
            char_width = 0.0f;  // zero-width character
        } else if (unicode_space_em > 0.0f) {
            float sc_scale = is_small_caps_lower ? 0.7f : 1.0f;
            char_width = unicode_space_em * lycon->font.current_font_size * sc_scale;
        } else {
            GlyphInfo ginfo = font_get_glyph(lycon->font.font_handle, codepoint);
            float sc_scale = is_small_caps_lower ? 0.7f : 1.0f;
            char_width = (ginfo.id != 0) ? ginfo.advance_x * sc_scale
                                         : lycon->font.current_font_size * sc_scale;
        }
        width += char_width + lycon->font.style->letter_spacing;
        str += char_bytes;
    }
    return width;
}

LineFillStatus text_has_line_filled(LayoutContext* lycon, DomNode* text_node) {
    // Get text data using helper function
    const char* text = (const char*)text_node->text_data();
    if (!text) return RDT_LINE_NOT_FILLED;  // null check

    unsigned char* str = (unsigned char*)text;
    unsigned char* text_end = str + strlen(text);
    float text_width = 0.0f;
    CssEnum text_transform = get_text_transform(lycon);
    bool is_word_start = true;  // First character is always word start
    bool has_break_opportunity = false;  // track if hyphen/break found before overflow

    do {
        if (is_space(*str)) return RDT_LINE_NOT_FILLED;

        // Get the codepoint and apply text-transform
        uint32_t codepoint = *str;
        int char_bytes = 1;
        if (codepoint >= 128) {
            int bytes = str_utf8_decode((const char*)str, (size_t)(text_end - str), &codepoint);
            if (bytes <= 0) codepoint = *str;
            else char_bytes = bytes;
        }

        // CSS Text 3 §4.1.3: U+3000 IDEOGRAPHIC SPACE is hangable — treat as space
        // for lookahead purposes so it doesn't predict false overflow.
        if (codepoint == 0x3000) return RDT_LINE_NOT_FILLED;
        {
        uint32_t tt_out[3];
        int tt_count = apply_text_transform_full(codepoint, text_transform, is_word_start, tt_out);
        codepoint = tt_out[0];
        // Add advance widths for extra codepoints from full case mapping
        for (int tti = 1; tti < tt_count; tti++) {
            GlyphInfo eg = font_get_glyph(lycon->font.font_handle, tt_out[tti]);
            if (eg.id != 0) text_width += eg.advance_x + lycon->font.style->letter_spacing;
        }
        }
        // CSS font-variant: small-caps — convert lowercase to uppercase
        // Track whether we scaled a lowercase char for size reduction
        bool is_small_caps_lower = false;
        if (has_small_caps(lycon)) {
            uint32_t original = codepoint;
            codepoint = apply_small_caps(codepoint);
            is_small_caps_lower = (codepoint != original);
        }
        is_word_start = false;  // Only first char is word start in this context

        // Track break opportunities: hyphens and soft hyphens.
        // If a break opportunity exists before the overflow point, the text can
        // be split there during actual layout — no need for premature wrapping.
        if (*str == '-' || codepoint == 0x00AD) {
            has_break_opportunity = true;
        }

        // Check for Unicode space characters with defined widths
        float unicode_space_em = get_unicode_space_width_em(codepoint);
        if (unicode_space_em < 0.0f) {
            // Zero-width character — U+200B (ZWSP) is a break opportunity like space
            if (codepoint == 0x200B) return RDT_LINE_NOT_FILLED;
            // Other zero-width chars (BOM, ZWJ, ZWNJ): skip with no width
        } else if (unicode_space_em > 0.0f) {
            // Use Unicode-specified width (fraction of em)
            float sc_scale = is_small_caps_lower ? 0.7f : 1.0f;
            text_width += unicode_space_em * lycon->font.current_font_size * sc_scale;
        } else {
            // get glyph advance via font module (returns CSS pixels)
            GlyphInfo ginfo = font_get_glyph(lycon->font.font_handle, codepoint);
            if (ginfo.id == 0) {
                // glyph not in primary font — estimate width as 1em for lookahead
                // (actual layout uses font_load_glyph which does fallback)
                text_width += lycon->font.current_font_size;
            } else {
                // CSS Fonts 3: small-caps lowercase chars use ~0.7x font size
                text_width += ginfo.advance_x * (is_small_caps_lower ? 0.7f : 1.0f);
            }
        }
        // CSS 2.1 §16.4: letter-spacing is added after every character
        // Browsers include trailing letter-spacing in text width (getBoundingClientRect)
        text_width += lycon->font.style->letter_spacing;
        str += char_bytes;
        // Use effective_right which accounts for float intrusions
        float line_right = lycon->line.has_float_intrusion ?
                           lycon->line.effective_right : lycon->line.right;
        // CSS Text 3 §8: letter-spacing is not applied at end of a line.
        // Subtract the trailing letter-spacing in the overflow check.
        if (lycon->line.advance_x + text_width - lycon->font.style->letter_spacing > line_right) { // line filled up
            // CSS Text 3 §5.2: If a break opportunity (hyphen, soft hyphen, ZWSP,
            // CJK) existed before the overflow, the text can be split during actual
            // layout at that break point.  Don't signal LINE_FILLED — let the text
            // start on the current line and wrap naturally at the break.
            if (has_break_opportunity) return RDT_NOT_SURE;
            return RDT_LINE_FILLED;
        }
    } while (*str);  // end of text
    // Note: Do NOT update advance_x here - this is a lookahead check only.
    // The actual advance_x update happens during real text layout.
    return RDT_NOT_SURE;
}

// check node and its siblings to see if line is filled
LineFillStatus node_has_line_filled(LayoutContext* lycon, DomNode* node) {
    do {
        if (node->is_text()) {
            LineFillStatus result = text_has_line_filled(lycon, node);
            if (result) { return result; }
        }
        else if (node->is_element()) {
            // CSS §9.3.1: <br> creates a forced line break — content after it
            // starts on a new line, so it cannot contribute to filling the current line.
            // Stop lookahead here to avoid false-positive wraps before <br>.
            if (node->tag() == HTM_TAG_BR) { return RDT_LINE_NOT_FILLED; }
            CssEnum outer_display = resolve_display_value(node).outer;
            if (outer_display == CSS_VALUE_BLOCK) { return RDT_LINE_NOT_FILLED; }
            else if (outer_display == CSS_VALUE_INLINE) {
                LineFillStatus result = span_has_line_filled(lycon, node);
                if (result) { return result; }
            }
        }
        else {
            log_debug("unknown node type");
            // skip the node
        }
        node = node->next_sibling;
    } while (node);
    return RDT_NOT_SURE;
}

// check view and its parents/siblings to see if line is filled
LineFillStatus view_has_line_filled(LayoutContext* lycon, View* view) {
    // note: this function navigates to parenets through laid out view tree,
    // and siblings through non-processed html nodes
    log_debug("check if view has line filled");
    DomNode* node = view->next_sibling;
    if (node) {
        LineFillStatus result = node_has_line_filled(lycon, node);
        if (result) { return result; }
    }
    // check at parent level
    view = (View*)view->parent;
    if (view) {
        if (view->view_type == RDT_VIEW_BLOCK) { return RDT_LINE_NOT_FILLED; }
        else if (view->view_type == RDT_VIEW_INLINE) {
            // CSS 2.1 §8.3: When checking if the line is filled from inside an
            // inline span, account for the span's right margin+border+padding.
            // These will be added to advance_x after the span's content is done,
            // so the lookahead must include them to avoid greedy over-placement.
            ViewSpan* sp = (ViewSpan*)view;
            float right_edge = 0;
            if (sp->bound) {
                right_edge += sp->bound->margin.right;
                if (sp->bound->border)
                    right_edge += sp->bound->border->width.right;
                right_edge += sp->bound->padding.right;
            }
            lycon->line.advance_x += right_edge;
            LineFillStatus result = view_has_line_filled(lycon, view);
            lycon->line.advance_x -= right_edge;
            return result;
        }
        log_debug("unknown view type");
    }
    return RDT_NOT_SURE;
}

void output_text(LayoutContext* lycon, ViewText* text, TextRect* rect, int text_length, float text_width) {
    if (text_length <= 0) {
        log_error("output_text: text_length=%d, skipping (node=%s)", text_length, text->node_name());
        return;
    }
    rect->length = text_length;
    rect->width = text_width;
    lycon->line.advance_x += text_width;
    // CSS 2.1 §16.6.1: Commit trailing space info for cross-node line break trimming.
    // When new text content is output, the previous trailing space is no longer at
    // the end of the line — clear it. Then save any trailing space from this rect.
    if (lycon->line.trailing_space_width > 0) {
        lycon->line.committed_trailing_rect = rect;
        lycon->line.committed_trailing_view = text;
        lycon->line.committed_trailing_space = lycon->line.trailing_space_width;
    } else if (lycon->line.committed_trailing_rect != rect) {
        // New text content with no trailing space clears the committed info
        lycon->line.committed_trailing_rect = NULL;
        lycon->line.committed_trailing_view = NULL;
        lycon->line.committed_trailing_space = 0;
    }
    lycon->line.last_text_rect = rect;  // track for trailing whitespace trimming
    lycon->line.last_text_view = text;  // ViewText owner for bounds update after trimming
    // CSS 2.1 §8.3: Inline content has been placed on this line, so any pending
    // inline left edges (margin+border+padding) have been consumed.
    lycon->line.inline_start_edge_pending = 0;
    // CSS 2.1 10.8.1: Half-leading model for text inline boxes
    // When line-height is explicitly set, the inline box height equals line-height.
    // Leading = line-height - content_height is split half above and half below.
    // When line-height is 'normal', use Chrome/Blink split: asc+desc above, leading below.
    float ascender = 0, descender = 0;
    if (lycon->block.line_height_is_normal && lycon->font.font_handle) {
        font_get_normal_lh_split(lycon->font.font_handle, &ascender, &descender);
    } else {
        TypoMetrics typo = get_os2_typo_metrics(lycon->font.font_handle);
        if (typo.valid && typo.use_typo_metrics) {
            ascender = typo.ascender;
            descender = typo.descender;
        } else if (lycon->font.font_handle) {
            const FontMetrics* m = font_get_metrics(lycon->font.font_handle);
            if (m) {
                ascender = m->hhea_ascender;
                descender = -(m->hhea_descender);
            }
        }
    }
    if (ascender > 0 || descender > 0) {
        if (!lycon->block.line_height_is_normal) {
            // Half-leading model: adjust ascender/descender so their sum equals line-height
            float content_height = ascender + descender;
            float half_leading = (lycon->block.line_height - content_height) / 2.0f;
            ascender += half_leading;
            descender += half_leading;
        }
        // CSS 2.1 §10.8.1: vertical-align offset shifts the inline box position,
        // which affects the line box height. A positive offset raises the box,
        // increasing the effective ascender and decreasing the effective descender.
        float va_offset = lycon->line.vertical_align_offset;
        if (va_offset != 0) {
            ascender += va_offset;
            descender -= va_offset;
        }
        log_debug("output_text BEFORE: prev_max_asc=%.1f prev_max_desc=%.1f new_asc=%.1f new_desc=%.1f va_off=%.1f va=%d",
            lycon->line.max_ascender, lycon->line.max_descender, ascender, descender, va_offset, lycon->line.vertical_align);
        // CSS 2.1 §10.8.1: vertical-align:top/bottom elements don't participate
        // in the first-pass baseline-relative line box height calculation.
        // Their inline box height is tracked separately and used in a second pass
        // to expand the line box if needed.
        if (lycon->line.vertical_align == CSS_VALUE_TOP) {
            float inline_box_height = ascender + descender;
            lycon->line.max_top_bottom_height = max(lycon->line.max_top_bottom_height, inline_box_height);
            lycon->line.max_top_height = max(lycon->line.max_top_height, inline_box_height);
        } else if (lycon->line.vertical_align == CSS_VALUE_BOTTOM) {
            float inline_box_height = ascender + descender;
            lycon->line.max_top_bottom_height = max(lycon->line.max_top_bottom_height, inline_box_height);
            lycon->line.max_bottom_height = max(lycon->line.max_bottom_height, inline_box_height);
        } else {
            // Save max_descender before this text's contribution, for trailing whitespace rollback
            lycon->line.max_desc_before_last_text = lycon->line.max_descender;
            lycon->line.max_ascender = max(lycon->line.max_ascender, ascender);
            lycon->line.max_descender = max(lycon->line.max_descender, descender);
        }
        // CSS 2.1 §10.8.1: Track if any inline text uses a different font from the
        // block's strut. When the strut font differs, the vertical alignment pass must
        // run to position content relative to the strut's baseline.
        if (!lycon->line.has_different_inline_font &&
            lycon->font.font_handle != lycon->line.line_start_font.font_handle) {
            lycon->line.has_different_inline_font = true;
        }
        log_debug("output_text: asc=%.1f desc=%.1f -> max_asc=%.1f max_desc=%.1f",
            ascender, descender, lycon->line.max_ascender, lycon->line.max_descender);
        if (descender > 9) {
            log_debug("output_text: LARGE descender=%.1f from font asc=%.1f desc=%.1f lh_normal=%d lh=%.1f",
                descender, ascender, descender, lycon->block.line_height_is_normal, lycon->block.line_height);
        }
        // Track each inline box's normal line-height for mixed-font lines
        if (lycon->block.line_height_is_normal && lycon->font.font_handle) {
            float normal_lh = font_calc_normal_line_height(lycon->font.font_handle);
            lycon->line.max_normal_line_height = max(lycon->line.max_normal_line_height, normal_lh);
        }
    }
    log_debug("text rect: '%.*t', x %f, y %f, width %f, height %f, font size %f, font family '%s'",
        text_length, text->text_data() + rect->start_index, rect->x, rect->y, rect->width, rect->height, text->font->font_size, text->font->family);

    if (text->rect == rect) {  // first rect
        text->x = rect->x;
        text->y = rect->y;
        text->width = rect->width;
        text->height = rect->height;
    } else {  // following rects after first rect
        float right = max(text->x + text->width, rect->x + rect->width);
        float bottom = max(text->y + text->height, rect->y + rect->height);
        text->x = min(text->x, rect->x);
        text->y = min(text->y, rect->y);
        text->width = right - text->x;
        text->height = bottom - text->y;
    }
}

void adjust_text_bounds(ViewText* text) {
    TextRect* rect = text->rect;
    if (!rect) return;
    text->x = rect->x;
    text->y = rect->y;
    text->width = rect->width;
    text->height = rect->height;
    rect = rect->next;
    while (rect) {
        float right = max(text->x + text->width, rect->x + rect->width);
        float bottom = max(text->y + text->height, rect->y + rect->height);
        text->x = min(text->x, rect->x);
        text->y = min(text->y, rect->y);
        text->width = right - text->x;
        text->height = bottom - text->y;
        rect = rect->next;
    }
}

void layout_text(LayoutContext* lycon, DomNode *text_node) {
    auto t_start = high_resolution_clock::now();

    unsigned char* next_ch;  ViewText* text_view = null;  TextRect* prev_rect = NULL;
    unsigned char* text_start = text_node->text_data();
    if (!text_start) return;  // null check for text data
    unsigned char* str = text_start;
    unsigned char* text_end = text_start + strlen((const char*)text_start);

    // CSS Inline 3 §2.1: Zero-length text nodes generate no inline boxes and
    // do not contribute to line box height. Skip immediately to avoid the
    // do-while loop processing a null codepoint as content.
    if (str == text_end) {
        text_node->view_type = RDT_VIEW_NONE;
        log_debug("skipping zero-length text node");
        return;
    }

    // Clear any existing text rects from previous layout passes (e.g., table measurement)
    // This prevents accumulation of duplicate rects when the same node is laid out multiple times
    if (text_node->view_type == RDT_VIEW_TEXT) {
        ViewText* existing_view = (ViewText*)text_node;
        if (existing_view->rect) {
            log_debug("clearing existing text rects for re-layout");
            existing_view->rect = nullptr;  // pool memory will be reused
        }
    }

    // Get white-space property from the text node's ancestor chain
    // This properly handles inline elements like <span style="white-space: pre">
    CssEnum white_space = get_white_space_value(text_node);  // todo: white-space should be put in BlockContext
    bool collapse_spaces = ws_collapse_spaces(white_space);
    bool collapse_newlines = ws_collapse_newlines(white_space);
    // CSS Sizing 3: In max-content mode, never wrap — measure full unwrapped width
    bool wrap_lines = ws_wrap_lines(white_space) && !is_max_content_mode(lycon);

    // Get word-break property for CJK line breaking
    CssEnum word_break = get_word_break(lycon);
    CssEnum line_break_val = get_line_break(lycon);
    // line-break: anywhere allows break at any typographic letter unit (CSS Text 3 §5.2)
    bool break_all = (word_break == CSS_VALUE_BREAK_ALL || line_break_val == CSS_VALUE_ANYWHERE);
    bool keep_all = (word_break == CSS_VALUE_KEEP_ALL && line_break_val != CSS_VALUE_ANYWHERE);

    // Get overflow-wrap property for emergency word breaking
    // line-break: anywhere also implies overflow-wrap: anywhere behavior
    // CSS Text 3 §5.2: word-break: break-word behaves as overflow-wrap: anywhere
    CssEnum overflow_wrap = get_overflow_wrap(lycon);
    bool break_word = (overflow_wrap == CSS_VALUE_BREAK_WORD || overflow_wrap == CSS_VALUE_ANYWHERE
                       || line_break_val == CSS_VALUE_ANYWHERE
                       || word_break == CSS_VALUE_BREAK_WORD);

    // Get text-transform property
    CssEnum text_transform = get_text_transform(lycon);
    bool is_word_start = true;  // Track word boundaries for capitalize
    int layout_text_iterations = 0;  // guard against infinite goto loops

    // CSS Text 3 §6.2: Resolve lang for CJ class behavior.
    // In line-break: normal, CJ → NS for Japanese, CJ → ID for Chinese/Korean.
    // In strict, CJ → NS always. In loose, CJ → ID always.
    const char* lang = resolve_lang(text_node);
    bool cj_is_non_starter = (line_break_val == CSS_VALUE_STRICT)
        || (line_break_val != CSS_VALUE_LOOSE && is_lang_japanese(lang));

    // CSS Text 3 §4.1.2: Track last non-whitespace codepoint for segment break
    // transformation. When a collapsible segment break occurs between two East Asian
    // Wide characters (neither Hangul), the break is removed instead of becoming a space.
    // Also tracks ZWSP (U+200B) which triggers removal of adjacent segment breaks.
    uint32_t last_processed_cp = 0;

    log_debug("layout_text: white-space=%d, collapse_spaces=%d, collapse_newlines=%d, wrap_lines=%d, text-transform=%d",
              white_space, collapse_spaces, collapse_newlines, wrap_lines, text_transform);

    // CSS Text 3 §5.2: Track whether the text had a leading space before collapsing.
    // A leading space constitutes a soft wrap opportunity, enabling the first-word-fit
    // check at LAYOUT_TEXT to wrap to the next line if the first word doesn't fit.
    // Preserved newlines (pre, pre-wrap) are forced breaks, not soft wrap opportunities —
    // the newline handler in the main loop will perform the line break, so exclude them
    // to avoid a double line break (one from the first-word check, one from the handler).
    bool had_leading_space = is_space(*str) && (collapse_newlines || (*str != '\n' && *str != '\r'));

    // skip space at start of line (only if collapsing spaces)
    if (collapse_spaces && (lycon->line.is_line_start || lycon->line.has_space) && is_space(*str)) {
        // When collapsing spaces, skip all whitespace (including newlines if collapse_newlines)
        while (is_space(*str) && (collapse_newlines || (*str != '\n' && *str != '\r'))) {
            str++;
        }
        if (!*str) {
            // todo: probably should still set it bounds
            text_node->view_type = RDT_VIEW_NONE;
            log_debug("skipping whitespace text node");
            // CSS Text 3 §5: Even though this whitespace was fully collapsed, it
            // represents a line break opportunity when the text's white-space allows
            // wrapping.  Record this so that subsequent nowrap content can use it as
            // a wrap point at the inter-element boundary.
            // Only set when not at line start — whitespace at line start has no
            // preceding content to break away from.
            if (wrap_lines && !lycon->line.is_line_start) {
                lycon->line.wrap_opportunity_before_nowrap = true;
            }
            return;
        }
    }
    LAYOUT_TEXT:
    // Guard against infinite loop from extreme negative margins or degenerate layouts
    if (++layout_text_iterations > 500) {
        log_error("layout_text: exceeded 500 iterations, aborting text layout");
        return;
    }
    // Check if we're already past the line end before starting new text
    // This can happen after an inline-block that's wider than the container
    // CSS Text 3 §5.2: Only wrap at allowed break points (soft wrap opportunities).
    // Empty inline elements (e.g., <span></span>) do NOT introduce break
    // opportunities. Require a valid wrap point: a previous space/ZWSP, a
    // collapsed-whitespace wrap opportunity, a leading space in this text,
    // or word-break: break-all (every character boundary is a break point).
    {
        float line_right = lycon->line.has_float_intrusion ?
                           lycon->line.effective_right : lycon->line.right;
        // Only break if we're strictly past the end, not just at the end
        // Being exactly at the end is fine - whitespace might be collapsed
        if (wrap_lines && lycon->line.advance_x > line_right && !lycon->line.is_line_start
            && (lycon->line.last_space || lycon->line.wrap_opportunity_before_nowrap
                || had_leading_space || break_all)) {
            log_debug("Text starts past line end (advance_x=%.1f > line_right=%.1f), breaking line",
                      lycon->line.advance_x, line_right);
            line_break(lycon);
        }
    }
    // CSS Text 3 §5.2: Before placing any characters, check whether the first
    // word fits in the remaining space on the current line.  If not, wrap to
    // the next line.  This prevents partial words from being placed at the end
    // of a line when they should have started on the next line.
    // Only check when the text had a leading space (soft wrap opportunity).
    // Without a space, no break opportunity exists before this text and the
    // content must continue on the current line (subject to overflow handling).
    // Skip when word-break: break-all is active, since every character is a
    // break opportunity and the remaining space can always be utilized.
    if (wrap_lines && !lycon->line.is_line_start && !break_all && had_leading_space) {
        float line_right = lycon->line.has_float_intrusion ?
                           lycon->line.effective_right : lycon->line.right;
        float remaining = line_right - lycon->line.advance_x;
        if (remaining > 0) {
            float first_word_w = measure_first_word_width(lycon, str, text_end, text_transform);
            if (first_word_w > 0 && first_word_w > remaining) {
                log_debug("First word (%.1f) exceeds remaining space (%.1f), wrapping to next line",
                          first_word_w, remaining);
                line_break(lycon);
            }
        }
    }
    if (!text_view) {
        text_view = (ViewText*)set_view(lycon, RDT_VIEW_TEXT, text_node);
        text_view->font = lycon->font.style;
    }

    // if font-size is 0, create zero-size text rect and return
    if (lycon->font.style && lycon->font.style->font_size <= 0.0f) {
        TextRect* rect = (TextRect*)pool_calloc(lycon->doc->view_tree->pool, sizeof(TextRect));
        if (!text_view->rect) {
            text_view->rect = rect;
        } else {
            TextRect* last_rect = text_view->rect;
            while (last_rect && last_rect->next) { last_rect = last_rect->next; }
            last_rect->next = rect;
        }
        rect->start_index = 0;
        rect->length = strlen((char*)text_start);
        rect->x = lycon->line.advance_x;
        rect->y = lycon->block.advance_y;
        rect->width = 0.0f;
        rect->height = 0.0f;
        return;
    }

    TextRect* rect = (TextRect*)pool_calloc(lycon->doc->view_tree->pool, sizeof(TextRect));
    if (!text_view->rect) {
        text_view->rect = rect;
    } else {
        TextRect* last_rect = text_view->rect;;
        while (last_rect && last_rect->next) { last_rect = last_rect->next; }
        last_rect->next = rect;
    }
    rect->start_index = str - text_start;
    // FreeType metrics are in physical pixels, divide by pixel_ratio for CSS pixels
    float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0) ? lycon->ui_context->pixel_ratio : 1.0f;
    const FontMetrics* _fm = font_get_metrics(lycon->font.font_handle);
    float font_height = _fm ? _fm->hhea_line_height : 16.0f;
    rect->x = lycon->line.advance_x;
    // browser text rect height uses font metrics (ascent+descent), NOT CSS line-height
    // CSS line-height affects line spacing/positioning, but text rect height is font-based
    // Use platform-specific metrics (CoreText on macOS) for accurate ascent+descent
    rect->height = font_get_cell_height(lycon->font.font_handle);

    // Text rect y-position based on vertical alignment
    // CSS half-leading model: text is centered within the line box
    // When line-height < font height, half_leading can be negative (text extends above line box)
    // Use FreeType font_height for half-leading calculation (consistent with lead_y calculation)
    if (lycon->line.vertical_align == CSS_VALUE_MIDDLE) {
        log_debug("middle-aligned-text: font %f, line %f", font_height, lycon->block.line_height);
        rect->y = lycon->block.advance_y + (lycon->block.line_height - font_height) / 2;
    }
    else if (lycon->line.vertical_align == CSS_VALUE_BOTTOM) {
        log_debug("bottom-aligned-text: font %f, line %f", font_height, lycon->block.line_height);
        rect->y = lycon->block.advance_y + lycon->block.line_height - font_height;
    }
    else if (lycon->line.vertical_align == CSS_VALUE_TOP) {
        log_debug("top-aligned-text");
        rect->y = lycon->block.advance_y;
    }
    else { // baseline - use half-leading model
        // Calculate half-leading based on FreeType metrics for consistency with lead_y
        // Allow negative half-leading only when line-height is explicitly less than font height
        // (e.g., line-height: 1em with large fonts). For normal line-height >= font_height,
        // use clamped lead_y (compatible with table cell vertical alignment).
        if (lycon->block.line_height < font_height) {
            // Explicit tight line-height: text extends above line box
            float half_leading = (lycon->block.line_height - font_height) / 2;
            rect->y = lycon->block.advance_y + half_leading;
        } else {
            // Normal case: use clamped lead_y (non-negative)
            rect->y = lycon->block.advance_y + lycon->block.lead_y;
        }
    }
    log_debug("layout text: '%t', start_index %d, x: %f, y: %f, advance_y: %f, lead_y: %f, font_face: '%s', font_size: %f",
        str, rect->start_index, rect->x, rect->y, lycon->block.advance_y, lycon->block.lead_y, lycon->font.style->family, lycon->font.style->font_size);

    // layout the text glyphs
    bool zwj_preceded = false;  // UAX #14: ZWJ suppresses break between adjacent characters
    bool prev_is_zwj_base = false;  // track if previous char is a ZWJ composition base
    do {
        float wd;
        uint32_t codepoint = *str;

        // Handle newlines as forced line breaks when not collapsing newlines
        if (!collapse_newlines && (*str == '\n' || *str == '\r')) {
            // CSS 2.2: When preserving newlines with collapsing spaces (pre-line),
            // any spaces/tabs immediately before the newline should be removed
            // Check if we have trailing whitespace to strip
            if (collapse_spaces && str > text_start + rect->start_index) {
                // Walk back to find trailing spaces before this newline
                const unsigned char* check = str - 1;
                float trailing_width = 0;
                while (check >= text_start + rect->start_index && is_space(*check)) {
                    trailing_width += lycon->font.style->space_width;
                    check--;
                }
                if (trailing_width > 0) {
                    rect->width -= trailing_width;
                    log_debug("stripped trailing whitespace before newline: width reduced by %f", trailing_width);
                }
                // Trailing spaces already stripped here — don't double-subtract in line_break
                lycon->line.trailing_space_width = 0;
            }
            // Output any text before the newline
            if (str > text_start + rect->start_index) {
                output_text(lycon, text_view, rect, str - text_start - rect->start_index, rect->width);
            }
            // Handle CRLF as single line break
            if (*str == '\r' && *(str + 1) == '\n') {
                str += 2;
            } else {
                str++;
            }
            // CSS Text 3 §7.2: text-align-last applies to lines immediately before
            // a forced line break. A preserved newline is a forced break.
            lycon->line.is_last_line = true;
            line_break(lycon);
            lycon->line.is_last_line = false;
            if (*str) {
                // CSS 2.1 §16.6.1: When collapsing spaces (pre-line), skip leading
                // spaces at the start of the new line after a preserved newline.
                if (collapse_spaces) {
                    while (is_space(*str) && (collapse_newlines || (*str != '\n' && *str != '\r'))) {
                        str++;
                    }
                    if (!*str) return;
                }
                is_word_start = true;  // Reset word boundary after line break
                goto LAYOUT_TEXT;
            }
            else return;
        }

        if (is_space(codepoint)) {
            wd = lycon->font.style->space_width;
            // Tab characters with preserved whitespace: use tab-size * space_width
            // Only when whitespace is preserved (pre, pre-wrap, break-spaces)
            if (codepoint == '\t' && !collapse_spaces) {
                // CSS Text 3 §4.2: tab-size <number> — tab stops occur at points
                // that are multiples of (tab-size × space advance) from the block's
                // starting content edge. If tab-size is 0, the tab is not rendered.
                // The space advance is computed using the block container's font,
                // not the inline element's font (CSS Text 3 §4.2: "the advance
                // width of the space character as rendered by the block's font").
                int ts = 8;
                ViewElement* ancestor = lycon->view->parent_view();
                while (ancestor) {
                    if (ancestor->is_element()) {
                        DomElement* elem = static_cast<DomElement*>(ancestor);
                        if (elem->blk && elem->blk->tab_size >= 0) {
                            ts = elem->blk->tab_size;
                            break;
                        }
                    }
                    ancestor = ancestor->parent_view();
                }
                // Use block container's font for space advance (set during block layout setup)
                FontProp* block_font = lycon->block.block_container_font;
                if (!block_font) block_font = lycon->font.style;
                if (ts == 0) {
                    wd = 0;
                } else {
                    float space_advance = block_font->space_width
                        + block_font->word_spacing
                        + block_font->letter_spacing;
                    float tab_period = space_advance * ts;
                    // Current position from block's starting content edge
                    float current_x = rect->x + rect->width;
                    // CSS Text 3 §4.2: if the distance to the next tab stop is less
                    // than 0.5ch, the next tab stop after that is used instead.
                    float half_ch = block_font->space_width * 0.5f;
                    float next_tab = tab_period * ceilf((current_x + half_ch) / tab_period);
                    wd = next_tab - current_x;
                }
            } else {
                // Regular space: apply word-spacing and letter-spacing once
                wd += lycon->font.style->word_spacing;
                wd += lycon->font.style->letter_spacing;
            }
            is_word_start = true;  // Next non-space char is word start
        }
        else {
            if (codepoint >= 128) { // unicode char
                int bytes = str_utf8_decode((const char*)str, (size_t)(text_end - str), &codepoint);
                if (bytes <= 0) { // invalid utf8 char
                    next_ch = str + 1;  codepoint = 0;
                }
                else { next_ch = str + bytes; }
            }
            else { next_ch = str + 1; }

            // Apply text-transform before loading glyph
            uint32_t tt_out[3];
            int tt_count = apply_text_transform_full(codepoint, text_transform, is_word_start, tt_out);
            codepoint = tt_out[0];
            // CSS font-variant: small-caps — convert lowercase to uppercase
            bool is_small_caps_lower = false;
            if (has_small_caps(lycon)) {
                uint32_t original = codepoint;
                codepoint = apply_small_caps(codepoint);
                is_small_caps_lower = (codepoint != original);
            }
            is_word_start = false;  // No longer at word start

            // Check for Unicode space characters with defined widths
            float unicode_space_em = get_unicode_space_width_em(codepoint);
            if (unicode_space_em < 0.0f) {
                // Zero-width character (e.g., U+200B ZWSP, U+FEFF ZWNBSP/BOM)
                // U+200B ZWSP is a line-break opportunity per Unicode Line Breaking Algorithm
                // U+FEFF BOM/ZWNBSP and U+200C/U+200D ZWJ/ZWNJ are NOT break opportunities
                if (codepoint == 0x200B && wrap_lines) {
                    // ZWSP: record as break opportunity with zero width.
                    // Set last_space to point to the final byte of the ZWSP sequence (str - 1)
                    // so that: (a) last_space < next_ch (H5) is TRUE when overflow is checked,
                    // and (b) str = last_space + 1 correctly resumes at the next character.
                    str = next_ch;
                    last_processed_cp = 0x200B;  // CSS Text 3 §4.1.2: track ZWSP for segment break rules
                    lycon->line.last_space = str - 1;
                    lycon->line.last_space_pos = rect->width;
                    lycon->line.last_space_kind = BRK_ZERO_WIDTH_BREAK;
                    lycon->line.is_line_start = false;
                    lycon->line.has_space = false;
                    lycon->line.trailing_space_width = 0;
                    continue;
                }
                // CSS Text 3 §5.2: U+00AD SOFT HYPHEN is a line-break opportunity.
                // It is invisible (zero width) unless the line breaks here, in which
                // case a visible hyphen '-' is rendered at the end of the line.
                if (codepoint == 0x00AD && wrap_lines) {
                    str = next_ch;
                    lycon->line.last_space = str - 1;
                    lycon->line.last_space_pos = rect->width;
                    lycon->line.last_space_kind = BRK_SOFT_HYPHEN;
                    lycon->line.is_line_start = false;
                    lycon->line.has_space = false;
                    lycon->line.trailing_space_width = 0;
                    continue;
                }
                // Other zero-width characters: skip with no width contribution
                str = next_ch;
                lycon->line.is_line_start = false;
                lycon->line.has_space = false;
                if (codepoint == 0x200D && prev_is_zwj_base) zwj_preceded = true;
                continue;  // Skip to next character without adding width
            } else if (unicode_space_em > 0.0f) {
                // Use Unicode-specified width (fraction of em)
                float sc_scale = is_small_caps_lower ? 0.7f : 1.0f;
                wd = unicode_space_em * lycon->font.current_font_size * sc_scale;
            } else {
                FontStyleDesc _sd = font_style_desc_from_prop(lycon->font.style);
                // Peek ahead for VS16 (U+FE0F) — forces emoji/color presentation
                bool emoji_presentation = false;
                if (next_ch) {
                    uint32_t peek_cp;
                    int peek_bytes = str_utf8_decode((const char*)next_ch, (size_t)(text_end - next_ch), &peek_cp);
                    if (peek_bytes > 0 && peek_cp == 0xFE0F) {
                        emoji_presentation = true;
                    }
                }
                // For layout metrics, use the regular font path even for Emoji_Presentation=Yes
                // codepoints (without explicit VS16). The regular fallback chain gives metrics
                // consistent with browser layout. The raster renderer (render.cpp) separately
                // forces emoji font for color output. VS16-preceded codepoints still use
                // emoji font since the author explicitly requested emoji presentation.
                LoadedGlyph* glyph = emoji_presentation
                    ? font_load_glyph_emoji(lycon->font.font_handle, &_sd, codepoint, false)
                    : font_load_glyph(lycon->font.font_handle, &_sd, codepoint, false);
                // Font is loaded at physical pixel size, so advance is in physical pixels
                // Divide by pixel_ratio to convert back to CSS pixels for layout
                float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0) ? lycon->ui_context->pixel_ratio : 1.0f;
                wd = glyph ? (glyph->advance_x / pixel_ratio) : lycon->font.style->space_width;
                // Emoji ZWJ sequence: the character following ZWJ combines with the
                // preceding base glyph to form a single composed emoji. Its advance
                // should not contribute to the line width since the base glyph already
                // occupies the full emoji width. Only suppress for emoji codepoints;
                // other scripts (CJK, Latin, etc.) keep their advance after ZWJ.
                // (Unicode UTS #51 emoji ZWJ sequences, UAX #29 grapheme clusters)
                if (zwj_preceded && is_emoji_for_zwj(codepoint)) {
                    wd = 0;
                }
                // CSS Fonts 3: small-caps lowercase chars rendered at ~0.7x font size
                // font_load_glyph returns advance at the handle's fixed size;
                // scale proportionally since FT_LOAD_NO_HINTING produces linear metrics
                if (is_small_caps_lower) {
                    wd *= 0.7f;
                }
                // Track fallback font metrics for line-height computation
                // When a glyph comes from a fallback font with taller metrics,
                // update the line ascender/descender (but NOT the text rect height,
                // which uses primary font metrics per browser behavior)
                if (glyph && glyph->font_ascender > 0) {
                    float fb_asc, fb_desc;
                    if (lycon->block.line_height_is_normal) {
                        // normal line-height: use platform-aware split from glyph
                        fb_asc = glyph->font_normal_ascender;
                        fb_desc = glyph->font_normal_descender;
                    } else {
                        // explicit line-height: use hhea metrics with half-leading
                        fb_asc = glyph->font_ascender;
                        fb_desc = glyph->font_descender;
                        float content_height = fb_asc + fb_desc;
                        float half_leading = (lycon->block.line_height - content_height) / 2.0f;
                        fb_asc += half_leading;
                        fb_desc += half_leading;
                    }
                    // CSS 2.1 §10.8.1: vertical-align:top/bottom elements don't participate
                    // in first-pass baseline-relative line box height. Track separately.
                    if (lycon->line.vertical_align == CSS_VALUE_TOP) {
                        float fb_inline_box_height = fb_asc + fb_desc;
                        lycon->line.max_top_bottom_height = max(lycon->line.max_top_bottom_height, fb_inline_box_height);
                        lycon->line.max_top_height = max(lycon->line.max_top_height, fb_inline_box_height);
                    } else if (lycon->line.vertical_align == CSS_VALUE_BOTTOM) {
                        float fb_inline_box_height = fb_asc + fb_desc;
                        lycon->line.max_top_bottom_height = max(lycon->line.max_top_bottom_height, fb_inline_box_height);
                        lycon->line.max_bottom_height = max(lycon->line.max_bottom_height, fb_inline_box_height);
                    } else {
                        lycon->line.max_ascender = max(lycon->line.max_ascender, fb_asc);
                        lycon->line.max_descender = max(lycon->line.max_descender, fb_desc);
                    }
                    if (fb_desc > 9) {
                        log_debug("FALLBACK font: LARGE fb_desc=%.1f fb_asc=%.1f lh_normal=%d",
                            fb_desc, fb_asc, lycon->block.line_height_is_normal);
                    }
                    // Also track normal line-height from the fallback font for mixed-font lines
                    if (lycon->block.line_height_is_normal && glyph->font_normal_line_height > 0) {
                        lycon->line.max_normal_line_height = max(lycon->line.max_normal_line_height,
                                                                  glyph->font_normal_line_height);
                    }
                }
                // Track CJK characters for line-height blending with system CJK font metrics
                if (is_cjk_character(codepoint)) {
                    lycon->line.has_cjk_text = true;
                }
            }
            // CSS 2.1 §16.4: letter-spacing is added after every character
            // Browsers include trailing letter-spacing in text node width
            wd += lycon->font.style->letter_spacing;

            // CSS 2.1 §16.4: word-spacing affects each space (U+0020) and
            // non-breaking space (U+00A0). U+0020 is handled in the is_space()
            // branch above. U+00A0 must be handled here since it's not collapsible
            // whitespace (it prevents line breaks and space collapsing).
            if (codepoint == 0x00A0) {
                wd += lycon->font.style->word_spacing;
                is_word_start = true;
            }
            // CSS Text 3 §8: Track trailing letter-spacing for trimming at line ends.
            // letter-spacing must not be applied at the start or end of a line.
            lycon->line.trailing_letter_spacing = lycon->font.style->letter_spacing;

            // Full case mapping expansion: add advance widths for extra codepoints
            // (e.g., ß → S,S — the second S needs its own advance width + letter-spacing)
            if (tt_count > 1) {
                FontStyleDesc _sd_extra = font_style_desc_from_prop(lycon->font.style);
                float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0) ? lycon->ui_context->pixel_ratio : 1.0f;
                for (int ti = 1; ti < tt_count; ti++) {
                    uint32_t extra_cp = tt_out[ti];
                    if (extra_cp == 0) continue;
                    LoadedGlyph* extra_glyph = font_load_glyph(lycon->font.font_handle, &_sd_extra, extra_cp, false);
                    float extra_wd = extra_glyph ? (extra_glyph->advance_x / pixel_ratio) : 0;
                    if (is_small_caps_lower) extra_wd *= 0.7f;
                    extra_wd += lycon->font.style->letter_spacing;
                    wd += extra_wd;
                }
            }
        }
        // handle kerning
        if (lycon->font.style->has_kerning) {
            if (lycon->line.prev_codepoint) {
                float kerning_css = font_get_kerning(lycon->font.font_handle, lycon->line.prev_codepoint, codepoint);
                if (kerning_css != 0.0f) {
                    if (str == text_start + rect->start_index) {
                        rect->x += kerning_css;
                    }
                    else {
                        rect->width += kerning_css;
                    }
                    log_debug("apply kerning: %f to char '%c'", kerning_css, *str);
                }
            }
            lycon->line.prev_codepoint = codepoint;
        }
        log_debug("layout char: '%c', x: %f, width: %f, wd: %f, line right: %f",
            *str == '\n' || *str == '\r' ? '^' : *str, rect->x, rect->width, wd, lycon->line.right);
        prev_is_zwj_base = is_zwj_composition_base(codepoint);
        // CSS Text 3 §4.1.2: track last non-whitespace codepoint for segment break transformation
        if (!is_space(codepoint)) last_processed_cp = codepoint;
        // UAX #14 B2: Em-dash (U+2014) allows break before and after.
        // Record break-before BEFORE adding width so the overflow check can use it.
        // Note: en-dash (U+2013, class BA) only allows break after, not before.
        if (codepoint == 0x2014 && wrap_lines && !lycon->line.is_line_start) {
            lycon->line.last_space = (uint8_t*)str - 1;       // byte before the dash
            lycon->line.last_space_pos = rect->width;          // width before the dash
            lycon->line.last_space_kind = BRK_HYPHEN;
        }
        rect->width += wd;
        // CSS Text 3 §4.1.3: Pre-wrap trailing spaces "hang" and don't count for
        // overflow. But once a non-space character follows the spaces, they are no
        // longer trailing — reset hanging_space_width so the overflow check below
        // uses the full content width (space + non-space) for wrapping decisions.
        if (!is_space(*str) && codepoint != 0x3000 && lycon->line.hanging_space_width > 0) {
            lycon->line.hanging_space_width = 0;
            lycon->line.hanging_space_text_trim = 0;
        }
        // Use effective_right which accounts for float intrusions
        float line_right = lycon->line.has_float_intrusion ?
                           lycon->line.effective_right : lycon->line.right;
        // CSS Text 3 §8: letter-spacing is not applied at end of a line.
        // Subtract the trailing letter-spacing when checking overflow, since the
        // last character's letter-spacing would be trimmed at line break time.
        // Use a small epsilon to tolerate accumulated floating-point rounding
        // errors from summing individual character widths (e.g., 0.000004px).
        if (wrap_lines && rect->x + rect->width - lycon->line.trailing_letter_spacing > line_right + 0.001f) { // line filled up and wrapping enabled
            log_debug("line filled up");
            if (codepoint == 0x3000 && white_space != CSS_VALUE_BREAK_SPACES) {
                // CSS Text 3 §4.1.3: U+3000 IDEOGRAPHIC SPACE hangs at end of line.
                // In break-spaces mode, spaces don't hang (§3), so skip this.
                // Don't break here — fall through to break tracking below.
            }
            else if (is_space(*str) && !collapse_spaces && white_space != CSS_VALUE_BREAK_SPACES) {
                // CSS Text 3 §4.1.3: In pre-wrap, trailing spaces hang at end of line.
                // Don't break here — fall through to break tracking where
                // hanging_space_width is accumulated.
            }
            else if (is_space(*str) && lycon->line.hanging_space_width > 0) {
                // Space within a hanging sequence (after U+3000): don't break.
                // The space is between hangable characters and should hang with them.
            }
            // CSS Text 3 §3 + §5.2: For break-spaces with break_all/line-break:anywhere,
            // when a space (or U+3000) overflows, prefer the earlier break_all opportunity
            // before the space. This avoids overflow by placing the space on the next line,
            // where it takes up its full width as break-spaces requires.
            else if ((is_space(*str) || codepoint == 0x3000) && white_space == CSS_VALUE_BREAK_SPACES
                     && break_all && lycon->line.last_space
                     && text_start <= lycon->line.last_space && lycon->line.last_space < str) {
                log_debug("break-spaces with break_all: break before overflowing space");
                rect->width -= wd;  // undo the space width
                str = lycon->line.last_space + 1;
                float output_width = lycon->line.last_space_pos;
                output_text(lycon, text_view, rect, str - text_start - rect->start_index, output_width);
                line_break(lycon);  goto LAYOUT_TEXT;
            }
            else if (is_space(*str)) { // break at the current space (collapsible or break-spaces)
                log_debug("break on space");
                // CSS Text 3 §3: For break-spaces, "a line breaking opportunity exists
                // after every preserved white space character." When a space overflows:
                // - If a prior break opportunity exists, rewind to it.
                // - If break-word/break-all is active, break BEFORE the space (the
                //   overflow-wrap property adds break opportunities at character
                //   boundaries, so the word/space boundary is a valid break point).
                // - Otherwise (pure break-spaces), the space stays on the current
                //   line and we break after it (may cause overflow).
                if (white_space == CSS_VALUE_BREAK_SPACES && rect->width - wd > 0.01f) {
                    if (lycon->line.last_space
                        && text_start <= lycon->line.last_space
                        && lycon->line.last_space < str) {
                        // Prior break exists — rewind to it
                        log_debug("break-spaces: rewind to prior space break");
                        str = lycon->line.last_space + 1;
                        float output_width = lycon->line.last_space_pos;
                        output_text(lycon, text_view, rect, str - text_start - rect->start_index, output_width);
                        line_break(lycon);  goto LAYOUT_TEXT;
                    } else if (break_word || break_all) {
                        // break-word/break-all active — break before space (old behavior)
                        log_debug("break-spaces + break-word: break before space");
                        rect->width -= wd;
                        output_text(lycon, text_view, rect, str - text_start - rect->start_index, rect->width);
                        line_break(lycon);
                        goto LAYOUT_TEXT;
                    } else {
                        // Pure break-spaces: space must stay on this line (CSS Text 3 §3)
                        str++;  // advance past the space
                        output_text(lycon, text_view, rect, str - text_start - rect->start_index, rect->width);
                        lycon->line.trailing_space_width = 0;
                        line_break(lycon);
                        log_debug("break-spaces: break after overflowing space (no prior break)");
                        if (*str) { goto LAYOUT_TEXT; }
                        else return;
                    }
                }
                // skip spaces according to white-space mode
                if (collapse_spaces) {
                    do { str++; } while (is_space(*str) && (collapse_newlines || (*str != '\n' && *str != '\r')));
                } else {
                    str++;  // only skip the current space in break-spaces mode
                }
                // CSS Text 3 §4.1.3: For break-spaces, preserved spaces take up space
                // and do not hang at end of line. Keep the space width in the text rect.
                // For other modes, remove the trailing space from the rect.
                if (white_space != CSS_VALUE_BREAK_SPACES) {
                    rect->width -= wd;  // minus away space width at line break
                }
                lycon->line.trailing_space_width = 0;  // already trimmed, don't double-subtract
                // Note: hanging_space_width (from U+3000) is handled by line_break().
                output_text(lycon, text_view, rect, str - text_start - rect->start_index, rect->width);
                line_break(lycon);
                log_debug("after space line break");
                if (*str) { goto LAYOUT_TEXT; }
                else return;
            }
            // CSS Text 3 §4.1.3: For pre-wrap, preserved spaces at end of line "hang"
            // and are not counted for overflow/wrapping purposes. If the non-hanging
            // content (text minus accumulated trailing spaces) fits within the line,
            // don't wrap — let the spaces hang past the margin.
            // Use live hanging_space_width (not saved last_space_hanging_width) because
            // once non-space content follows the spaces, they are no longer trailing
            // and normal wrap rules apply (the live value resets after non-space chars).
            else if (lycon->line.hanging_space_width > 0
                     && rect->x + rect->width - lycon->line.hanging_space_width <= line_right) {
                log_debug("pre-wrap hanging: content fits without %dpx hanging spaces",
                    (int)lycon->line.hanging_space_width); // INT_CAST_OK: pixel count for log
                // Don't wrap. The spaces hang past the margin. Fall through to continue.
            }
            else if (lycon->line.last_space) { // break at the last space
                log_debug("break at last space");
                if (text_start <= lycon->line.last_space && lycon->line.last_space < str) {
                    str = lycon->line.last_space + 1;
                    // Restore hanging_space_width from saved values so line_break()
                    // can subtract it from the line box width.
                    if (lycon->line.last_space_hanging_width > 0) {
                        lycon->line.hanging_space_width = lycon->line.last_space_hanging_width;
                        lycon->line.hanging_space_text_trim = lycon->line.last_space_hanging_text_trim;
                    }
                    // Output full width including hanging spaces — visual rect preserves
                    // hanging width. line_break() adjusts advance_x to exclude it.
                    float output_width = lycon->line.last_space_pos;
                    int text_len = str - text_start - rect->start_index;
                    // CSS Text 3 §5.2: Soft hyphen — exclude SHY bytes from output,
                    // add visible hyphen width, and mark rect for hyphen rendering.
                    if (lycon->line.last_space_kind == BRK_SOFT_HYPHEN) {
                        text_len -= 2;  // U+00AD is 2 bytes in UTF-8 (0xC2 0xAD)
                        GlyphInfo hglyph = font_get_glyph(lycon->font.font_handle, '-');
                        float hyphen_width = (hglyph.id != 0) ? hglyph.advance_x : lycon->font.current_font_size * 0.3f;
                        output_width += hyphen_width;
                    }
                    output_text(lycon, text_view, rect, text_len, output_width);
                    if (lycon->line.last_space_kind == BRK_SOFT_HYPHEN) {
                        rect->has_trailing_hyphen = true;
                    }
                    // CSS 2.1 §16.6.1: When wrapping at a collapsible space, the
                    // trailing space must be trimmed from the line box width.
                    // trailing_space_width was reset when subsequent non-space chars
                    // were processed (before the overflow triggered the rewind).
                    // Restore it so line_break() can trim the trailing space.
                    // CSS Text 3 §8: Include word-spacing and letter-spacing that were
                    // part of the space's total width, since trailing space trimming
                    // should remove the entire space contribution (glyph + spacing).
                    if (lycon->line.last_space_kind == BRK_SPACE && collapse_spaces) {
                        GlyphInfo sp_glyph = font_get_glyph(lycon->font.font_handle, ' ');
                        lycon->line.trailing_space_width = sp_glyph.advance_x
                            + lycon->font.style->word_spacing
                            + lycon->font.style->letter_spacing;
                    }
                    line_break(lycon);  goto LAYOUT_TEXT;
                }
                else { // last_space outside the text
                    // CSS Text 3 §5.2: overflow-wrap: break-word with last_space in
                    // a previous text node. First check if the word would fit on a fresh
                    // line. If so, rewind to the text start and move it to the next line
                    // (the space in the previous node serves as the natural wrap point).
                    // Only do an emergency mid-word break when the word itself is wider
                    // than the full line (i.e., it would overflow even on a fresh line).
                    if (break_word && !lycon->line.is_line_start) {
                        float full_line_width = lycon->line.right - lycon->line.left;
                        // rect->width includes chars measured so far (including overflow char).
                        // The total word width is at least rect->width - wd (chars that fit)
                        // plus the remaining unmeasured chars. Use rect->width - wd as a
                        // lower bound: if even that exceeds a full line, mid-word break.
                        // If it fits, the whole word likely fits — move to next line.
                        if (rect->width - wd > full_line_width) {
                            // Word is wider than a full line: emergency mid-word break
                            log_debug("break-word: mid-word break (word wider than line)");
                            rect->width -= wd;  // undo the char that overflowed
                            int text_len = str - text_start - rect->start_index;
                            if (text_len > 0) {
                                output_text(lycon, text_view, rect, text_len, rect->width);
                            } else {
                                // first char already overflows: unlink the empty rect
                                if (text_view->rect == rect) {
                                    text_view->rect = nullptr;
                                } else {
                                    TextRect* prev = text_view->rect;
                                    while (prev && prev->next != rect) prev = prev->next;
                                    if (prev) prev->next = nullptr;
                                }
                            }
                            line_break(lycon);
                            goto LAYOUT_TEXT;
                        }
                        // Word fits on a fresh line: rewind to text start and wrap
                        log_debug("break-word: rewinding text to next line (word fits on fresh line)");
                        str = text_start + rect->start_index;  // rewind to text start
                        // Unlink the partially-measured rect
                        if (text_view->rect == rect) {
                            text_view->rect = nullptr;
                        } else {
                            TextRect* prev = text_view->rect;
                            while (prev && prev->next != rect) prev = prev->next;
                            if (prev) prev->next = nullptr;
                        }
                        line_break(lycon);
                        goto LAYOUT_TEXT;
                    }
                    float advance_x = lycon->line.advance_x;  // save current advance_x
                    line_break(lycon);
                    rect->y = lycon->block.advance_y;
                    rect->x = lycon->line.advance_x;  lycon->line.advance_x = advance_x;
                    // continue the text flow
                }
            }
            // CSS 2.1 §9.5: "If a shortened line box is too small to contain any content,
            // then the line box is shifted downward until either some content fits or there
            // are no more floats present."
            // When text overflows next to a float and there's no word-break opportunity,
            // try moving below the float where the line box is wider.
            // Guard conditions:
            // 1. Float is actually narrowing the line (available width < full container width)
            // 2. The text would fit on a full-width line (rect->width <= full container width)
            //    This ensures we only shift when the float is causing the overflow, not when
            //    the text is inherently too wide for even the full container.
            // Note: Add 0.5px tolerance to account for sub-pixel float width rounding
            else if (lycon->line.has_float_intrusion &&
                     (lycon->line.effective_right - lycon->line.effective_left) <
                     (lycon->line.right - lycon->line.left) &&
                     rect->width <= (lycon->line.right - lycon->line.left) + 0.5f) {
                log_debug("text overflows next to float, shifting below float (eff_width=%.1f < full_width=%.1f)",
                          lycon->line.effective_right - lycon->line.effective_left,
                          lycon->line.right - lycon->line.left);
                // Undo the width we just added - we'll re-layout from LAYOUT_TEXT
                rect->width -= wd;
                // Reset str to start of current rect (we haven't output anything yet)
                str = text_start + rect->start_index;
                // Remove the rect we allocated (it will be re-created in LAYOUT_TEXT)
                // Find and unlink this rect from the chain
                if (text_view->rect == rect) {
                    text_view->rect = nullptr;
                } else {
                    TextRect* prev = text_view->rect;
                    while (prev && prev->next != rect) prev = prev->next;
                    if (prev) prev->next = nullptr;
                }
                line_break(lycon);
                goto LAYOUT_TEXT;
            }
            // overflow-wrap: break-word/anywhere — emergency mid-word break
            // when no other break opportunity exists
            else if (break_word && !lycon->line.is_line_start) {
                log_debug("overflow-wrap: emergency mid-word break");
                rect->width -= wd;  // undo the char that overflowed
                int text_len = str - text_start - rect->start_index;
                if (text_len > 0) {
                    output_text(lycon, text_view, rect, text_len, rect->width);
                } else {
                    // first char of this rect already overflows: unlink the empty rect
                    // so goto LAYOUT_TEXT starts fresh without a zero-length entry
                    if (text_view->rect == rect) {
                        text_view->rect = nullptr;
                    } else {
                        TextRect* prev = text_view->rect;
                        while (prev && prev->next != rect) prev = prev->next;
                        if (prev) prev->next = nullptr;
                    }
                }
                line_break(lycon);
                goto LAYOUT_TEXT;
            }
            // else cannot break and no float intrusion, continue the flow in current line
        }
        // CSS Text 3 §3/§5: white-space: nowrap prevents ALL line breaks within this
        // text, but break opportunities from a wrappable parent context still apply.
        // When nowrap content causes overflow and a wrappable break opportunity exists
        // at the inter-element boundary (e.g., collapsed whitespace between nowrap spans
        // in a normal-wrapping parent), break the line and re-layout this entire nowrap
        // text segment on the new line.
        else if (!wrap_lines && rect->x + rect->width > line_right
                 && lycon->line.wrap_opportunity_before_nowrap
                 && !lycon->line.is_line_start) {
            log_debug("nowrap overflow with wrappable break opportunity, breaking line");
            // Reset to start of current text segment
            str = text_start + rect->start_index;
            // Unlink the current (incomplete) rect — LAYOUT_TEXT will create a new one
            if (text_view->rect == rect) {
                text_view->rect = nullptr;
            } else {
                TextRect* prev = text_view->rect;
                while (prev && prev->next != rect) prev = prev->next;
                if (prev) prev->next = nullptr;
            }
            line_break(lycon);
            goto LAYOUT_TEXT;
        }
        if (is_space(*str)) {
            if (collapse_spaces) {
                // CSS Text 3 §4.1.2: Track whether whitespace contains a segment break (newline)
                bool has_segment_break = (codepoint == '\n' || codepoint == '\r');
                // Collapse multiple spaces into one, respecting newline preservation
                do {
                    str++;
                    if ((*str == '\n' || *str == '\r') && collapse_newlines) has_segment_break = true;
                } while (is_space(*str) && (collapse_newlines || (*str != '\n' && *str != '\r')));

                // CSS Text 3 §4.1.2: Segment Break Transformation Rules
                // After collapsing whitespace, if the sequence contained a segment break,
                // check whether the break should be removed entirely:
                //   Rule 1: If adjacent to ZWSP (U+200B), remove the segment break.
                //   Rule 2: If both the character before and after are East Asian Width
                //           Fullwidth/Wide (not Hangul), remove the segment break.
                // Otherwise the segment break becomes a space (already added as wd).
                if (has_segment_break && collapse_newlines) {
                    bool remove_break = false;
                    // check character before the segment break
                    bool prev_is_zwsp = (last_processed_cp == 0x200B);
                    // check character after: peek at next non-whitespace
                    bool next_is_zwsp = false;
                    uint32_t next_cp = *str ? peek_codepoint(str) : 0;
                    if (next_cp == 0x200B) {
                        next_is_zwsp = true;
                    }
                    // Rule 1: adjacent to ZWSP
                    if (prev_is_zwsp || next_is_zwsp) {
                        remove_break = true;
                    }
                    // Rule 2: East Asian Wide ↔ East Asian Wide (not Hangul)
                    // CSS Text 3 §4.1.2: segment breaks between two East Asian F/W
                    // characters (neither Hangul) are removed instead of becoming spaces.
                    if (!remove_break && last_processed_cp && next_cp
                        && is_east_asian_fw(last_processed_cp) && !is_hangul(last_processed_cp)
                        && is_east_asian_fw(next_cp) && !is_hangul(next_cp)) {
                        remove_break = true;
                    }
                    if (remove_break) {
                        rect->width -= wd;  // undo the space width
                        log_debug("segment break removed between U+%04X and U+%04X (CSS Text 3 §4.1.2)",
                                  last_processed_cp, next_cp);
                        continue;  // skip break opportunity recording
                    }
                }
            } else {
                // Preserve spaces - just advance one character
                str++;
                // CSS 2.1 §16.6.1: Preserved spaces ARE content — mark line as started.
                // Without this, a line containing only preserved spaces (white-space: pre)
                // would be treated as empty and not commit a line box.
                lycon->line.is_line_start = false;
            }
            lycon->line.last_space = str - 1;  lycon->line.last_space_pos = rect->width;
            lycon->line.last_space_kind = BRK_SPACE;
            // CSS Text 3 §4.1.1: Only signal has_space for collapsible spaces.
            // A preserved space (white-space: pre/pre-wrap) must NOT cause a
            // subsequent collapsible space in a different element to be collapsed.
            // "Any collapsible space immediately following another collapsible
            // space is collapsed" — preserved spaces are not collapsible.
            if (collapse_spaces) {
                lycon->line.has_space = true;
            }
            // CSS 2.1 §16.6.1: Only track trailing space for end-of-line trimming
            // when spaces are collapsible (normal/nowrap/pre-line). Per spec,
            // trailing spaces are removed only for those values. For pre and
            // pre-wrap, spaces are fully preserved at end of line.
            if (collapse_spaces) {
                lycon->line.trailing_space_width = wd;
            }
            // CSS Text 3 §4.1.3: For pre-wrap, track accumulated trailing space
            // width separately. Used at wrap points to compute hanging space.
            // For break-spaces, spaces do NOT hang — they take up space at end
            // of line (CSS Text 3 §3: "preserved white space takes up space and
            // does not hang at the end of a line").
            if (!collapse_spaces && wrap_lines && white_space != CSS_VALUE_BREAK_SPACES) {
                lycon->line.hanging_space_width += wd;
                lycon->line.hanging_space_text_trim += wd;  // regular ASCII spaces get trimmed from text rects
            }
            // Save hanging_space_width at the time last_space is recorded,
            // AFTER accumulating the current space. This is used at
            // break-at-last-space wrap points where non-space chars may have
            // reset the live hanging_space_width since the last space.
            lycon->line.last_space_hanging_width = lycon->line.hanging_space_width;
            lycon->line.last_space_hanging_text_trim = lycon->line.hanging_space_text_trim;
        }
        else if (codepoint == 0x3000) {
            // CSS Text 3 §4.1.3: U+3000 IDEOGRAPHIC SPACE is a hangable break opportunity.
            // It hangs at end of line in all white-space modes except 'pre'.
            str = next_ch;
            lycon->line.last_space = str - 1;
            lycon->line.last_space_pos = rect->width;
            lycon->line.last_space_kind = BRK_IDEOGRAPHIC_SPACE;
            // CSS Text 3 §4.1.1: Only signal has_space for collapsible spaces
            if (collapse_spaces) {
                lycon->line.has_space = true;
            }
            lycon->line.is_line_start = false;
            // Track as hanging space — its width doesn't count for line box width.
            // For break-spaces, spaces don't hang (CSS Text 3 §3), so skip accumulation.
            if (white_space != CSS_VALUE_BREAK_SPACES) {
                lycon->line.hanging_space_width += wd;
            }
            lycon->line.last_space_hanging_width = lycon->line.hanging_space_width;
            lycon->line.last_space_hanging_text_trim = lycon->line.hanging_space_text_trim;
        }
        else if (is_other_space_separator(codepoint) && codepoint != 0x3000
                 && codepoint != 0x00A0 && codepoint != 0x202F) {
            // UAX #14: Other space separators (Unicode Zs category) have line break
            // class BA (Break After). These are NOT CSS "white space" — they are
            // content characters that happen to provide break opportunities.
            // Unlike U+0020 (regular space), they are not collapsed, not trimmed,
            // and not hung at end of line. They always render at their natural width.
            // Includes: U+1680 OGHAM SPACE MARK, U+2000-U+200A (EN QUAD through
            // HAIR SPACE), U+205F MEDIUM MATHEMATICAL SPACE.
            str = next_ch;
            lycon->line.last_space = str - 1;
            lycon->line.last_space_pos = rect->width;
            lycon->line.last_space_kind = BRK_HYPHEN;  // BA class: break-after, width included
            lycon->line.is_line_start = false;
            lycon->line.has_space = false;
            lycon->line.trailing_space_width = 0;
            lycon->line.hanging_space_width = 0;
            lycon->line.hanging_space_text_trim = 0;
        }
        else if (codepoint == 0x002D || codepoint == 0x2010 || codepoint == 0x2013 || codepoint == 0x2014) {
            // Hyphens and dashes are break opportunities (CSS Text 3 §5.2, UAX #14)
            //   U+002D hyphen-minus, U+2010 hyphen: break after (UAX #14 class HY)
            //   U+2013 en-dash: break after (UAX #14 class BA)
            //   U+2014 em-dash: break before and after (UAX #14 class B2)
            // Track this as a potential break point, but include the dash in the current line
            str = next_ch;
            lycon->line.last_space = str - 1;  // last byte of the dash
            lycon->line.last_space_pos = rect->width;  // width including the dash
            lycon->line.last_space_kind = BRK_HYPHEN;
            lycon->line.is_line_start = false;
            lycon->line.has_space = false;
            lycon->line.trailing_space_width = 0;
            lycon->line.hanging_space_width = 0;
            lycon->line.hanging_space_text_trim = 0;
        }
        else if (codepoint == 0x003F && wrap_lines && !lycon->line.is_line_start) {
            // CSS Text 3 §5.2: UAs may add wrap opportunities at typographic symbol units.
            // ? (UAX #14 class EX): break after in URL query separators (e.g. "q3?lang=").
            // Guard: only break before alphanumeric to avoid breaking prose like: gone?"
            str = next_ch;
            uint32_t next_cp = peek_codepoint(str);
            if ((next_cp >= 'A' && next_cp <= 'Z') || (next_cp >= 'a' && next_cp <= 'z')
                    || (next_cp >= '0' && next_cp <= '9')) {
                lycon->line.last_space = str - 1;
                lycon->line.last_space_pos = rect->width;
                lycon->line.last_space_kind = BRK_TEXT;
            }
            lycon->line.is_line_start = false;
            lycon->line.has_space = false;
            lycon->line.trailing_space_width = 0;
            lycon->line.hanging_space_width = 0;
            lycon->line.hanging_space_text_trim = 0;
        }
        else if (((break_all && (is_typographic_letter_unit(codepoint)
                                  // CSS Text 3 §5.2: line-break: anywhere introduces soft wrap
                                  // opportunities around ALL typographic character units, including
                                  // GL class characters (NBSP U+00A0, NNBSP U+202F) which are
                                  // normally non-breaking. word-break: break-all only converts
                                  // letters and numbers, so NBSP remains non-breaking with break-all.
                                  || (line_break_val == CSS_VALUE_ANYWHERE && (codepoint == 0x00A0 || codepoint == 0x202F))))
                  || (has_id_line_break_class(codepoint) && !keep_all)) && wrap_lines) {
            // CJK or break-all: can break after this character.
            // Track as last_space so overflow handling can break at this position.
            // UAX #14 / CSS Text 3 §5.2: Apply OP/CL/NS rules:
            //   - No break before OP (opening punctuation) or NS (non-starter)
            //   - No break after CL (closing punctuation)
            str = next_ch;
            lycon->line.is_line_start = false;
            lycon->line.has_space = false;
            lycon->line.trailing_space_width = 0;
            lycon->line.hanging_space_width = 0;
            lycon->line.hanging_space_text_trim = 0;

            // Record break opportunity after this character, unless forbidden by line-break rules
            bool allow_break = true;
            // CSS Text 3 §5.2: line-break: anywhere overrides standard UAX#14 prohibitions.
            // All typographic character unit boundaries become soft wrap opportunities.
            if (line_break_val != CSS_VALUE_ANYWHERE) {
            // UAX #14 §9.2: ZWJ (U+200D) suppresses break between adjacent characters
            if (zwj_preceded) allow_break = false;
            // CSS Text 3 §5.2: No break after OP characters (OP stays with following content)
            if (allow_break && is_line_break_op(codepoint)) allow_break = false;
            if (allow_break) {
                // Peek at next character: no break before CL, NS, EX, IS, or SY (UAX #14 LB13)
                // Also no break before ZWJ (it joins with the preceding character)
                uint32_t next_cp = peek_codepoint(str);
                // If at end of text node, look across span boundaries for LB13
                if (next_cp == 0) next_cp = peek_next_inline_codepoint(text_node);
                if (next_cp == 0x200D) {
                    allow_break = false;  // ZWJ follows: suppress break
                } else if (next_cp > 0) {
                    bool is_loose = (line_break_val == CSS_VALUE_LOOSE);
                    // CL class always prevents break
                    if (is_line_break_cl(next_cp)) {
                        allow_break = false;
                    }
                    // NS class prevents break; CJ chars also prevent when resolved to NS
                    // CSS Text 3 §6.2: CJ → NS for Japanese (normal/strict), CJ → ID for
                    // Chinese/Korean (normal). Strict always → NS, loose always → ID.
                    else if (is_line_break_ns(next_cp) || (cj_is_non_starter && is_line_break_cj(next_cp))) {
                        allow_break = false;
                    }
                    // EX/IS/SY prevents break, but loose mode allows break before fullwidth ！？
                    else if (is_line_break_ex_is_sy(next_cp) && !(is_loose && is_fullwidth_ex(next_cp))) {
                        allow_break = false;
                    }
                    // UAX #14 LB21: × BA — no break before Break After characters.
                    // Other space separators (U+1680, U+2000-U+200A, U+205F) have
                    // class BA. Suppress CJK/break-all break if one follows.
                    else if (is_other_space_separator(next_cp)
                             && next_cp != 0x00A0 && next_cp != 0x202F && next_cp != 0x3000) {
                        allow_break = false;
                    }
                }
            }
            } // end if not line-break: anywhere
            zwj_preceded = false;  // consumed
            if (allow_break) {
                lycon->line.last_space = str - 1;  // last byte of current char
                lycon->line.last_space_pos = rect->width;  // width including this char
                lycon->line.last_space_kind = has_id_line_break_class(codepoint) ? BRK_CJK : BRK_TEXT;
            }
        }
        else {
            str = next_ch;  lycon->line.is_line_start = false;  lycon->line.has_space = false;
            lycon->line.trailing_space_width = 0;
            lycon->line.hanging_space_width = 0;
            lycon->line.hanging_space_text_trim = 0;
            zwj_preceded = false;
            // UAX #14 / CSS Text 3 §5.2: CL/NS characters adjacent to CJK text
            // participate in CJK-style break tracking. After CL/NS, a break is
            // allowed (they stay with preceding content, break after is fine).
            // OP characters don't allow break after them.
            if (wrap_lines && (is_line_break_cl(codepoint) || is_line_break_ns(codepoint))) {
                // Peek at next character: no break if next is also CL/NS
                uint32_t next_cp = peek_codepoint(str);
                if (!(next_cp > 0 && (is_line_break_cl(next_cp) || is_line_break_ns(next_cp)))) {
                    lycon->line.last_space = str - 1;
                    lycon->line.last_space_pos = rect->width;
                    lycon->line.last_space_kind = is_line_break_cl(codepoint) ? BRK_CL : BRK_NS;
                }
            }
        }
    } while (*str);
    // end of text
    if (wrap_lines && lycon->line.last_space) { // need to check if line will fill up (only when wrapping)
        float saved_advance_x = lycon->line.advance_x;  lycon->line.advance_x += rect->width;
        if (view_has_line_filled(lycon, text_view) == RDT_LINE_FILLED) {
            if (text_start <= lycon->line.last_space && lycon->line.last_space < str) {
                str = lycon->line.last_space + 1;
                // Restore advance_x before output_text (it will add the correct width)
                lycon->line.advance_x = saved_advance_x;
                // Restore hanging_space_width from saved values so line_break()
                // can subtract it from the line box width.
                if (lycon->line.last_space_hanging_width > 0) {
                    lycon->line.hanging_space_width = lycon->line.last_space_hanging_width;
                    lycon->line.hanging_space_text_trim = lycon->line.last_space_hanging_text_trim;
                }
                // Output with full width including hanging spaces — the visual rect
                // preserves hanging space width. line_break() adjusts advance_x
                // to exclude hanging width from line box calculations.
                float output_width = lycon->line.last_space_pos;
                int text_len = str - text_start - rect->start_index;
                // CSS Text 3 §5.2: Soft hyphen — exclude SHY bytes, add visible hyphen
                if (lycon->line.last_space_kind == BRK_SOFT_HYPHEN) {
                    text_len -= 2;  // U+00AD is 2 bytes in UTF-8 (0xC2 0xAD)
                    GlyphInfo hglyph = font_get_glyph(lycon->font.font_handle, '-');
                    float hyphen_width = (hglyph.id != 0) ? hglyph.advance_x : lycon->font.current_font_size * 0.3f;
                    output_width += hyphen_width;
                }
                output_text(lycon, text_view, rect, text_len, output_width);
                if (lycon->line.last_space_kind == BRK_SOFT_HYPHEN) {
                    rect->has_trailing_hyphen = true;
                }
                line_break(lycon);
                if (*str) goto LAYOUT_TEXT;
                else return;  // end of text
            }
            else { // last_space outside the text, break at start of text
                // Restore advance_x before line_break
                lycon->line.advance_x = saved_advance_x;
                line_break(lycon);
                rect->x = lycon->line.advance_x;  rect->y = lycon->block.advance_y;
                // output the entire text (advance_x is 0 after line_break reset)
            }
        }
        else {
            lycon->line.advance_x = saved_advance_x;
            // output the entire text
        }
    }
    // else output the entire text
    output_text(lycon, text_view, rect, str - text_start - rect->start_index, rect->width);

    auto t_end = high_resolution_clock::now();
    g_text_layout_time += duration<double, std::milli>(t_end - t_start).count();
    g_text_layout_count++;
}
