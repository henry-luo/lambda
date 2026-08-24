#include "layout.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_style.hpp"
#include "../lib/avl_tree.h"
#include "../lib/binsearch.h"
#include "../lib/font/font.h"
#include "../lib/tagged.hpp"
#include "../lib/utf.h"

#include "../lib/log.h"
#include <chrono>

#include <cctype>
#include <cwctype>
#include <utf8proc.h>
using namespace std::chrono;

extern double g_text_layout_time;
extern int64_t g_text_layout_count;

static void clear_slice_inline_start_edge(LayoutContext* lycon, DomNode* text_node);
static void record_inline_box_decoration_fragment(LayoutContext* lycon, DomNode* text_node);

static float line_terminal_letter_spacing_trim(float letter_spacing) {
    // CSS Text 3 leaves line-end spacing undefined; Chromium trims only positive tracking.
    return max(letter_spacing, 0.0f);
}

struct SimpleCaseMapping { uint32_t from; uint32_t to; };

static int simple_case_cmp(const void* record, const void* key, void* udata) {
    (void)udata;
    uint32_t rfrom = ((const SimpleCaseMapping*)record)->from;
    uint32_t qfrom = *(const uint32_t*)key;
    return (rfrom > qfrom) - (rfrom < qfrom);
}

static uint32_t lookup_simple_case(const SimpleCaseMapping* table,
    int count, uint32_t codepoint) {
    int idx = binsearch_records(table, count, sizeof(SimpleCaseMapping),
                                 &codepoint, simple_case_cmp, nullptr);
    return idx >= 0 ? table[idx].to : 0;
}

static const SimpleCaseMapping g_uppercase_simple[] = {
    {0x023F, 0x2C7E},  // ȿ → Ȿ
    {0x0240, 0x2C7F},  // ɀ → Ɀ
    {0x0250, 0x2C6F},  // ɐ → Ɐ
    {0x0251, 0x2C6D},  // ɑ → Ɑ
    {0x0252, 0x2C70},  // ɒ → Ɒ
    {0x026B, 0x2C62},  // ɫ → Ɫ
    {0x0271, 0x2C6E},  // ɱ → Ɱ
    {0x027D, 0x2C64},  // ɽ → Ɽ
    {0x1D7D, 0x2C63},  // ᵽ → Ᵽ
    {0x2C61, 0x2C60},  // ⱡ → Ⱡ
    {0x2C65, 0x023A},  // ⱥ → Ⱥ
    {0x2C66, 0x023E},  // ⱦ → Ⱦ
    {0x2C68, 0x2C67},  // ⱨ → Ⱨ
    {0x2C6A, 0x2C69},  // ⱪ → Ⱪ
    {0x2C6C, 0x2C6B},  // ⱬ → Ⱬ
    {0x2C73, 0x2C72},  // ⱳ → Ⱳ
    {0x2C76, 0x2C75},  // ⱶ → Ⱶ
};
static const int g_uppercase_simple_count = 17;

static const SimpleCaseMapping g_lowercase_simple[] = {
    {0x023A, 0x2C65},  // Ⱥ → ⱥ
    {0x023E, 0x2C66},  // Ⱦ → ⱦ
    {0x2C60, 0x2C61},  // Ⱡ → ⱡ
    {0x2C62, 0x026B},  // Ɫ → ɫ
    {0x2C63, 0x1D7D},  // Ᵽ → ᵽ
    {0x2C64, 0x027D},  // Ɽ → ɽ
    {0x2C67, 0x2C68},  // Ⱨ → ⱨ
    {0x2C69, 0x2C6A},  // Ⱪ → ⱪ
    {0x2C6B, 0x2C6C},  // Ⱬ → ⱬ
    {0x2C6D, 0x0251},  // Ɑ → ɑ
    {0x2C6E, 0x0271},  // Ɱ → ɱ
    {0x2C6F, 0x0250},  // Ɐ → ɐ
    {0x2C70, 0x0252},  // Ɒ → ɒ
    {0x2C72, 0x2C73},  // Ⱳ → ⱳ
    {0x2C75, 0x2C76},  // Ⱶ → ⱶ
    {0x2C7E, 0x023F},  // Ȿ → ȿ
    {0x2C7F, 0x0240},  // Ɀ → ɀ
};
static const int g_lowercase_simple_count = 17;

/**
 * Apply CSS font-variant: small-caps transformation.
 * Converts lowercase characters to uppercase. The actual size reduction
 * is handled by using a smaller font size during glyph measurement.
 * Uses utf8proc for proper Unicode case conversion.
 */
static inline uint32_t apply_small_caps(uint32_t codepoint) {
    uint32_t mapped = lookup_simple_case(
        g_uppercase_simple, g_uppercase_simple_count, codepoint);
    if (mapped != 0) return mapped;
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
            DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(node);
            if (elem->font && elem->fontp()->font_variant == CSS_VALUE_SMALL_CAPS) {
                return true;
            }
        }
        node = node->parent;
    }
    return false;
}
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

static const FullCaseMapping g_lowercase_full[] = {
    {0x0130, {0x0069, 0x0307, 0x0000}, 2},
};
static const int g_lowercase_full_count = 1;

static int full_case_cmp(const void* record, const void* key, void* udata) {
    (void)udata;
    uint32_t rfrom = ((const FullCaseMapping*)record)->from;
    uint32_t qfrom = *(const uint32_t*)key;
    return (rfrom > qfrom) - (rfrom < qfrom);
}

static const FullCaseMapping* lookup_full_case(const FullCaseMapping* table,
    int count, uint32_t codepoint) {
    int idx = binsearch_records(table, count, sizeof(FullCaseMapping),
                                 &codepoint, full_case_cmp, nullptr);
    return idx >= 0 ? &table[idx] : nullptr;
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
    if (text_transform == CSS_VALUE_CAPITALIZE && is_word_start) {
        switch (codepoint) {
        case 0x01C4: // uppercase DZ with caron has titlecase ǅ
        case 0x01C5:
        case 0x01C6: out[0] = 0x01C5; return 1;
        case 0x01C7: // uppercase LJ has titlecase ǈ
        case 0x01C8:
        case 0x01C9: out[0] = 0x01C8; return 1;
        case 0x01CA: // uppercase NJ has titlecase ǋ
        case 0x01CB:
        case 0x01CC: out[0] = 0x01CB; return 1;
        case 0x01F1: // uppercase DZ has titlecase ǲ
        case 0x01F2:
        case 0x01F3: out[0] = 0x01F2; return 1;
        }
    }
    if (text_transform == CSS_VALUE_UPPERCASE || (text_transform == CSS_VALUE_CAPITALIZE && is_word_start)) {
        const FullCaseMapping* m = lookup_full_case(g_uppercase_full, g_uppercase_full_count, codepoint);
        if (m) {
            for (int i = 0; i < m->len; i++) out[i] = m->to[i];
            return m->len;
        }
        uint32_t mapped = lookup_simple_case(
            g_uppercase_simple, g_uppercase_simple_count, codepoint);
        if (mapped != 0) {
            out[0] = mapped;
            return 1;
        }
        if (codepoint < 128) {
            out[0] = std::toupper(codepoint);
        } else {
            out[0] = (uint32_t)utf8proc_toupper((utf8proc_int32_t)codepoint);
        }
        return 1;
    } else if (text_transform == CSS_VALUE_LOWERCASE) {
        const FullCaseMapping* m = lookup_full_case(g_lowercase_full, g_lowercase_full_count, codepoint);
        if (m) {
            for (int i = 0; i < m->len; i++) out[i] = m->to[i];
            return m->len;
        }
        uint32_t mapped = lookup_simple_case(
            g_lowercase_simple, g_lowercase_simple_count, codepoint);
        if (mapped != 0) {
            out[0] = mapped;
            return 1;
        }
        if (codepoint < 128) {
            out[0] = std::tolower(codepoint);
        } else {
            out[0] = (uint32_t)utf8proc_tolower((utf8proc_int32_t)codepoint);
        }
        return 1;
    } else if (text_transform == CSS_VALUE_FULL_SIZE_KANA) {
        // CSS Text 3 §2.1: Convert small Kana to their normal (full-size) equivalents
        bool odd_pair = (codepoint >= 0x3041 && codepoint <= 0x3049) ||
            (codepoint >= 0x3083 && codepoint <= 0x3087) ||
            (codepoint >= 0x30A1 && codepoint <= 0x30A9) ||
            (codepoint >= 0x30E3 && codepoint <= 0x30E7);
        if (odd_pair && (codepoint & 1)) {
            out[0] = codepoint + 1;
            return 1;
        }
        if (codepoint == 0x3063 || codepoint == 0x308E ||
            codepoint == 0x30C3 || codepoint == 0x30EE) {
            out[0] = codepoint + 1;
            return 1;
        }
        if (codepoint >= 0xFF67 && codepoint <= 0xFF6B) {
            out[0] = codepoint + 0x0A;
            return 1;
        }
        if (codepoint >= 0xFF6C && codepoint <= 0xFF6E) {
            out[0] = codepoint + 0x28;
            return 1;
        }
        switch (codepoint) {
        case 0x3095: out[0] = 0x304B; return 1;
        case 0x3096: out[0] = 0x3051; return 1;
        case 0x30F5: out[0] = 0x30AB; return 1;
        case 0x30F6: out[0] = 0x30B1; return 1;
        case 0xFF6F: out[0] = 0xFF82; return 1;
        }
    }
    out[0] = codepoint;
    return 1;
}

bool text_codepoint_has_zero_advance(uint32_t codepoint) {
    if (codepoint >= 0x1F3FB && codepoint <= 0x1F3FF) return true;  // emoji modifiers
    if (codepoint >= 0xFE00 && codepoint <= 0xFE0F) return true;    // variation selectors
    if (codepoint >= 0xE0100 && codepoint <= 0xE01EF) return true;  // variation selectors

    // CSS Text treats these controls as zero-advance even when the font
    // category lookup would not classify them as combining marks.
    if (codepoint == 0x00AD || codepoint == 0x034F || codepoint == 0x061C ||
        codepoint == 0x180E || codepoint == 0xFEFF ||
        (codepoint >= 0x200B && codepoint <= 0x200F) ||
        (codepoint >= 0x202A && codepoint <= 0x202E) ||
        (codepoint >= 0x2060 && codepoint <= 0x2064) ||
        (codepoint >= 0x2066 && codepoint <= 0x206F)) {
        return true;
    }

    utf8proc_category_t cat = utf8proc_category((utf8proc_int32_t)codepoint);
    return cat == UTF8PROC_CATEGORY_MN || cat == UTF8PROC_CATEGORY_ME;
}

bool layout_text_edge_has_whitespace(const char* text, bool end) {
    if (!text || !*text) return false;
    size_t length = strlen(text);
    unsigned char ch = (unsigned char)text[end ? length - 1 : 0];
    return ch == ' ' || ch == '\t' || ch == '\n' ||
           ch == '\r' || ch == '\f';
}

bool layout_element_edge_has_whitespace(DomNode* element, bool end) {
    if (!element || !element->is_element()) return false;

    DomNode* node = end ? element->as_element()->last_child
                        : element->as_element()->first_child;
    while (node) {
        if (node->is_text()) {
            return layout_text_edge_has_whitespace(
                (const char*)node->text_data(), end);
        }
        DomElement* child = node->is_element() ? node->as_element() : nullptr;
        DomNode* edge_child = child
            ? (end ? child->last_child : child->first_child) : nullptr;
        if (!edge_child) return false;
        node = edge_child;
    }
    return false;
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

CssEnum get_text_transform_from_node(DomNode* node) {
    while (node) {
        if (node->is_element()) {
            DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(node);
            if (elem->specified_style) {
                CssDeclaration* decl = style_tree_get_declaration(
                    elem->specified_style, CSS_PROPERTY_TEXT_TRANSFORM);
                if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                    CssEnum value = decl->value->data.keyword;
                    if (value == CSS_VALUE_INHERIT || value == CSS_VALUE__UNDEF) {
                        node = node->parent;
                        continue;
                    }
                    return value;
                }
            }

            CssEnum transform = get_text_transform_from_block(elem->blk);
            if (transform != CSS_VALUE_NONE) return transform;
        }
        node = node->parent;
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
static int count_justify_opportunities_impl(const char* str, int len,
                                            bool collapse_spaces,
                                            bool collapse_newlines,
                                            bool trim_trailing_space) {
    if (!str || len <= 0) return 0;

    int count = 0;
    const char* end = str + len;
    bool prev_was_id = false;
    bool in_collapsible_space = false;
    bool ends_in_collapsible_space = false;

    while (str < end) {
        uint32_t cp;
        int bytes = str_utf8_decode(str, (size_t)(end - str), &cp);
        if (bytes <= 0) { str++; prev_was_id = false; continue; }

        bool ascii_space = cp <= 0x7F && is_space((char)cp);
        if (collapse_spaces && ascii_space) {
            bool preserved_newline = !collapse_newlines && (cp == '\n' || cp == '\r');
            if (preserved_newline) {
                if (in_collapsible_space && count > 0) count--;
                in_collapsible_space = false;
                ends_in_collapsible_space = false;
            } else {
                if (!in_collapsible_space) count++;
                in_collapsible_space = true;
                ends_in_collapsible_space = true;
            }
            prev_was_id = false;
        } else if (cp == ' ') {
            count++;
            in_collapsible_space = false;
            ends_in_collapsible_space = false;
            prev_was_id = false;
        } else if (has_id_line_break_class(cp)) {
            if (prev_was_id) {
                count++;
            }
            in_collapsible_space = false;
            ends_in_collapsible_space = false;
            prev_was_id = true;
        } else {
            in_collapsible_space = false;
            ends_in_collapsible_space = false;
            prev_was_id = false;
        }

        str += bytes;
    }

    if (trim_trailing_space && ends_in_collapsible_space && count > 0) count--;
    return count;
}

int count_justify_opportunities(const char* str, int len) {
    return count_justify_opportunities_impl(str, len, false, false, false);
}

static inline CssEnum get_text_spacing_trim(LayoutContext* lycon, DomNode* text_node) {
    DomNode* node = text_node ? text_node->parent : (lycon ? (lycon->elmt ? lycon->elmt : lycon->view) : nullptr);
    while (node) {
        if (node->is_element()) {
            DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(node);
            if (elem->blk && elem->block_mut()->text_spacing_trim != 0) {
                CssEnum value = elem->block()->text_spacing_trim;
                if (value != CSS_VALUE_INHERIT && value != CSS_VALUE_UNSET &&
                    value != CSS_VALUE_REVERT && value != CSS_VALUE_INITIAL) {
                    return value;
                }
            }
            if (elem->specified_style) {
                CssDeclaration* decl = style_tree_get_declaration(
                    elem->specified_style, CSS_PROPERTY_TEXT_SPACING_TRIM);
                if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                    CssEnum value = decl->value->data.keyword;
                    if (value != CSS_VALUE_INHERIT && value != CSS_VALUE_UNSET &&
                        value != CSS_VALUE_REVERT && value != CSS_VALUE_INITIAL) {
                        return value;
                    }
                }
            }
        }
        node = node->parent;
    }
    return CSS_VALUE_NORMAL;
}

static inline bool should_apply_text_spacing_trim(LayoutContext* lycon, DomNode* text_node) {
    CssEnum value = get_text_spacing_trim(lycon, text_node);
    return value != CSS_VALUE_SPACE_ALL;
}

static CssEnum get_inherited_text_enum(
        LayoutContext* lycon, CssEnum BlockProp::*member, CssEnum fallback) {
    DomNode* node = lycon->elmt ? lycon->elmt : lycon->view;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(node);
            if (elem->blk && elem->block_mut()->*member != 0) {
                return elem->block()->*member;
            }
        }
        node = node->parent;
    }
    return fallback;
}

/**
 * Get word-break property from the layout context.
 * Checks block property for the current element or parent elements.
 */
/**
 * Get line-break property from the layout context.
 * Checks block property for the current element or parent elements.
 */
/**
 * Get overflow-wrap property from the layout context.
 * Checks block property for the current element or parent elements.
 */
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

/**
 * Check if a codepoint has UAX#14 line break class ID (Ideographic).
 * Characters with ID class allow line breaks before and after them
 * under normal wrapping (CSS Text 3 §5.2, UAX #14).
 * Covers: CJK ideographs, Kana, Hangul, emoji, Yi, CJK symbols/radicals,
 * CJK compatibility ideographs, and other ID-class characters.
 */
bool has_id_line_break_class(uint32_t cp) {
    if (cp >= 0x3400 && cp <= 0x9FFF) return true;   // Extension A + main block
    if (cp >= 0xF900 && cp <= 0xFAFF) return true;   // CJK Compatibility Ideographs
    if (cp >= 0x20000 && cp <= 0x2CEAF) return true;  // Extensions B/C/D/E
    if (cp >= 0x2CEB0 && cp <= 0x2EBE0) return true;  // Extension F
    if (cp >= 0x2EBF0 && cp <= 0x2F7FF) return true;  // Extension I + nearby
    if (cp >= 0x2F800 && cp <= 0x2FA1F) return true;  // CJK Compat Ideographs Supplement
    if (cp >= 0x30000 && cp <= 0x3FFFD) return true;  // Extensions G/H + Plane 3

    if (cp >= 0x3040 && cp <= 0x30FF) return true;   // Hiragana + Katakana
    if (cp >= 0x31F0 && cp <= 0x31FF) return true;   // Katakana Phonetic Extensions
    if (cp >= 0xAC00 && cp <= 0xD7AF) return true;   // Hangul Syllables
    if (cp >= 0xFF65 && cp <= 0xFF9F) return true;   // Halfwidth Katakana
    if (cp >= 0x1B000 && cp <= 0x1B2FF) return true;  // Kana Supplement + Extended-A + B

    if (cp >= 0x2E80 && cp <= 0x2FFF) return true;   // CJK Radicals + Kangxi + IDC
    if (cp >= 0x3003 && cp <= 0x3007) return true;   // Ditto mark, JIS, Closing, Number Zero
    if (cp >= 0x3012 && cp <= 0x3013) return true;   // Postal Mark, Geta Mark
    if (cp >= 0x3020 && cp <= 0x303F) return true;   // Postal Mark Face through IDHFS
    if (cp >= 0x3200 && cp <= 0x33FF) return true;   // Enclosed CJK + CJK Compatibility
    if (cp >= 0x3105 && cp <= 0x312F) return true;   // Bopomofo
    if (cp >= 0x3131 && cp <= 0x318E) return true;   // Hangul Compatibility Jamo
    if (cp >= 0x3190 && cp <= 0x31EF) return true;   // Kanbun + Bopomofo Ext + CJK Strokes

    if (cp >= 0xA000 && cp <= 0xA4CF) return true;   // Yi Syllables + Yi Radicals

    if (cp >= 0xFE30 && cp <= 0xFE6F) return true;   // CJK Compatibility Forms + Small Forms
    if (cp >= 0xFF01 && cp <= 0xFF60) return true;   // Fullwidth ASCII variants
    if (cp >= 0xFFA0 && cp <= 0xFFDC) return true;   // Halfwidth Hangul

    if (cp >= 0x17000 && cp <= 0x18DF2) return true;  // Tangut Ideographs + Components

    if (cp >= 0x1B170 && cp <= 0x1B2FB) return true;  // Nushu Characters

    if (cp >= 0x1F000 && cp <= 0x1FAFF) return true;  // Mahjong..Symbols Extended-A
    if (cp >= 0x1FC00 && cp <= 0x1FFFD) return true;  // Reserved (default ID)

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
// Unicode Line Break Class Helpers (UAX #14 / CSS Text 3 §5.2)

/**
 * Check if a codepoint has OP (Opening Punctuation) line-break class.
 * CSS Text 3 §5.2: No break after OP characters — they stay with following content.
 * Based on Unicode Line Break Algorithm (UAX #14).
 */
static inline bool is_line_break_op(uint32_t cp) {
    if (cp == 0x0028 || cp == 0x005B || cp == 0x007B) return true;
    if (cp == 0x0F3A || cp == 0x0F3C) return true;
    if (cp == 0x169B) return true;
    if (cp == 0x201A || cp == 0x201E) return true;
    if (cp == 0x2045) return true;
    if (cp == 0x2329) return true;
    if (cp >= 0x2768 && cp <= 0x2775 && (cp & 1) == 0) return true; // even = opening
    if (cp == 0x27E6 || cp == 0x27E8 || cp == 0x27EA) return true;
    if (cp >= 0x2983 && cp <= 0x2998 && (cp & 1) == 1) return true; // odd = opening
    if (cp == 0x29D8 || cp == 0x29DA || cp == 0x29FC) return true;
    if (cp == 0x3008 || cp == 0x300A || cp == 0x300C || cp == 0x300E ||
        cp == 0x3010 || cp == 0x3014 || cp == 0x3016 || cp == 0x3018 ||
        cp == 0x301A || cp == 0x301D) return true;
    if (cp >= 0xFE35 && cp <= 0xFE44 && (cp & 1) == 1) return true; // odd = opening
    if (cp == 0xFE47) return true;
    if (cp == 0xFE59 || cp == 0xFE5B || cp == 0xFE5D) return true;
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
    if (cp == 0x0029 || cp == 0x005D || cp == 0x007D) return true;
    if (cp == 0x0F3B || cp == 0x0F3D) return true;
    if (cp == 0x169C) return true;
    if (cp == 0x207E || cp == 0x208E) return true;
    if (cp == 0x232A) return true;
    if (cp >= 0x2769 && cp <= 0x2775 && (cp & 1) == 1) return true;
    if (cp == 0x27E7 || cp == 0x27E9 || cp == 0x27EB) return true;
    if (cp >= 0x2984 && cp <= 0x2998 && (cp & 1) == 0) return true;
    if (cp == 0x29D9 || cp == 0x29DB || cp == 0x29FD) return true;
    if (cp == 0x3001 || cp == 0x3002) return true;
    if (cp == 0x3009 || cp == 0x300B || cp == 0x300D || cp == 0x300F ||
        cp == 0x3011 || cp == 0x3015 || cp == 0x3017 || cp == 0x3019 ||
        cp == 0x301B || cp == 0x301E || cp == 0x301F) return true;
    if (cp >= 0xFE36 && cp <= 0xFE44 && (cp & 1) == 0) return true;
    if (cp == 0xFE48) return true;
    if (cp == 0xFE50 || cp == 0xFE52) return true;
    if (cp == 0xFE5A || cp == 0xFE5C || cp == 0xFE5E) return true;
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
    if (cp == 0x3041 || cp == 0x3043 || cp == 0x3045 || cp == 0x3047 || cp == 0x3049) return true;
    if (cp == 0x3063 || cp == 0x3083 || cp == 0x3085 || cp == 0x3087 || cp == 0x308E) return true;
    if (cp == 0x3095 || cp == 0x3096) return true;
    if (cp == 0x30A1 || cp == 0x30A3 || cp == 0x30A5 || cp == 0x30A7 || cp == 0x30A9) return true;
    if (cp == 0x30C3 || cp == 0x30E3 || cp == 0x30E5 || cp == 0x30E7 || cp == 0x30EE) return true;
    if (cp == 0x30F5 || cp == 0x30F6) return true;
    if (cp == 0x30FC) return true;
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
    if (cp == 0x0E5A || cp == 0x0E5B) return true;
    if (cp == 0x17D4 || cp == 0x17D6 || cp == 0x17DA) return true;
    if (cp == 0x203C) return true;
    if (cp == 0x3005 || cp == 0x301C || cp == 0x303B || cp == 0x303C) return true;
    if (cp == 0x309B || cp == 0x309C || cp == 0x309D || cp == 0x309E) return true;
    if (cp == 0x30A0 || cp == 0x30FB || cp == 0x30FD || cp == 0x30FE) return true;
    if (cp == 0xFE54 || cp == 0xFE55) return true;
    if (cp == 0xFF1A || cp == 0xFF1B) return true;
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
    if (cp == 0x0021 || cp == 0x003F) return true;   // ! ?
    if (cp == 0x05C6) return true;                    // HEBREW PUNCTUATION NUN HAFUKHA
    if (cp == 0x061B || cp == 0x061E || cp == 0x061F) return true;  // Arabic semicolon/punct/question
    if (cp == 0x06D4) return true;                    // ARABIC FULL STOP
    if (cp == 0xFE15 || cp == 0xFE16) return true;   // Presentation forms for ! ?
    if (cp == 0xFE56 || cp == 0xFE57) return true;   // Small ! ?
    if (cp == 0xFF01 || cp == 0xFF1F) return true;   // Fullwidth ! ?
    if (cp == 0x002C || cp == 0x002E) return true;    // , .
    if (cp == 0x003A || cp == 0x003B) return true;    // : ;
    if (cp == 0x037E) return true;                    // GREEK QUESTION MARK
    if (cp == 0x0589) return true;                    // ARMENIAN FULL STOP
    if (cp == 0x060C || cp == 0x060D) return true;    // ARABIC COMMA/DATE SEPARATOR
    if (cp == 0x07F8) return true;                    // NKO COMMA
    if (cp == 0xFE10 || cp == 0xFE13 || cp == 0xFE14) return true;  // Vertical comma/colon/semicolon
    if (cp == 0x002F) return true;                    // /
    return false;
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
                DomElement* elmt = lam::dom_require<DOM_NODE_ELEMENT>(node);
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
        if (node->next_sibling) {
            uint32_t cp = first_text_codepoint_in_subtree(node->next_sibling);
            if (cp) return cp;
        }
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
/**
 * Check if a codepoint can serve as the base (left side) of a ZWJ emoji
 * composition sequence. Only specific emoji characters produce composed
 * glyphs when followed by ZWJ + another emoji. Without HarfBuzz text
 * shaping, this heuristic covers the standard Unicode ZWJ sequences.
 * Reference: Unicode UTS #51, emoji-zwj-sequences.txt
 */
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
    if (text_codepoint_has_zero_advance(codepoint)) return -1.0f;

    switch (codepoint) {
        case 0x2000: return 0.5f;   // EN QUAD - width of 'n' (nominally 1/2 em)
        case 0x2001: return 1.0f;   // EM QUAD - width of 'm' (nominally 1 em)
        case 0x2002: return 0.5f;   // EN SPACE - 1/2 em
        case 0x2003: return 1.0f;   // EM SPACE - 1 em
        case 0x2004: return 1.0f/3; // THREE-PER-EM SPACE - 1/3 em
        case 0x2005: return 0.25f;  // FOUR-PER-EM SPACE - 1/4 em
        case 0x2006: return 1.0f/6; // SIX-PER-EM SPACE - 1/6 em
        case 0x2009: return 1.0f/5; // THIN SPACE - ~1/5 em (or 1/6 em)
        case 0x200A: return 1.0f/10; // HAIR SPACE - very thin (~1/10 to 1/16 em)
        default: return 0.0f;
    }
}

static inline bool uses_east_asian_fullwidth_cell(uint32_t cp) {
    return (cp >= 0x3400 && cp <= 0x9FFF) ||
           (cp >= 0x20000 && cp <= 0x2FA1F) ||
           (cp >= 0x2E80 && cp <= 0x303F) ||
           (cp >= 0xFE30 && cp <= 0xFE6F) ||
           (cp >= 0xFF01 && cp <= 0xFF60);
}

static float layout_font_em_size(LayoutContext* lycon) {
    if (!lycon) return 0.0f;
    if (lycon->font.current_font_size > 0.0f) {
        return lycon->font.current_font_size;
    }
    return lycon->font.style && lycon->font.style->font_size > 0.0f
        ? lycon->font.style->font_size : 0.0f;
}

static float normalize_east_asian_advance(LayoutContext* lycon, uint32_t codepoint,
                                          float advance) {
    float font_em = layout_font_em_size(lycon);
    if (font_em > 0.0f && uses_east_asian_fullwidth_cell(codepoint) &&
        advance < font_em) {
        return font_em;
    }
    return advance;
}

static float measure_current_glyph_advance(LayoutContext* lycon, uint32_t codepoint, bool trim_cjk_spacing) {
    if (!lycon || !lycon->font.style) return 0.0f;
    FontHandle* handle = lycon->font.font_handle ? lycon->font.font_handle : lycon->font.style->font_handle;
    if (handle) {
        FontStyleDesc sd = font_style_desc_from_prop(lycon->font.style);
        LoadedGlyph* glyph = font_load_glyph(handle, &sd, codepoint, false);
        if (glyph) {
            float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0)
                ? lycon->ui_context->pixel_ratio : 1.0f;
            float advance = glyph->advance_x / pixel_ratio;
            advance = normalize_east_asian_advance(lycon, codepoint, advance);
            if (trim_cjk_spacing) {
                advance += font_get_halt_adjustment(handle, codepoint) * 0.5f;
            }
            return advance;
        }
    }
    return layout_font_em_size(lycon);
}

static float text_kerning_adjustment(LayoutContext* lycon, uint32_t previous,
                                     uint32_t current) {
    if (!lycon || !lycon->font.style || !lycon->font.style->has_kerning ||
        !lycon->font.font_handle || !previous || !current) {
        return 0.0f;
    }
    return font_get_kerning(lycon->font.font_handle, previous, current);
}

static inline bool is_simple_latin_shaping_byte(unsigned char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool layout_measure_simple_latin_run(LayoutContext* lycon,
                                     struct FontHandle* handle,
                                     const unsigned char* text,
                                     size_t remaining,
                                     LayoutSimpleLatinRun* result) {
    if (!lycon || !handle || !text || remaining < 2 || !result ||
        !is_simple_latin_shaping_byte(*text)) {
        return false;
    }
    size_t bytes = 0;
    while (bytes < remaining && is_simple_latin_shaping_byte(text[bytes])) {
        GlyphInfo glyph = font_get_glyph(handle, (uint32_t)text[bytes]);
        if (glyph.id == 0) return false;
        bytes++;
    }
    if (bytes < 2) return false;

    int byte_count = (int)bytes; // INT_CAST_OK: text byte count
    TextExtents ext = font_measure_text(handle, (const char*)text, byte_count);
    if (ext.glyph_count <= 0 && ext.width <= 0.0f) return false;

    result->bytes = bytes;
    result->width = ext.width;
    result->first_codepoint = (uint32_t)*text;
    result->last_codepoint = (uint32_t)text[bytes - 1];
    return true;
}

static bool can_shape_simple_latin_run(LayoutContext* lycon, CssEnum text_transform,
                                       bool trim_cjk_spacing, bool break_all,
                                       bool break_word) {
    if (!lycon || !lycon->font.style) return false;
    if (!lycon->font.font_handle && !lycon->font.style->font_handle) return false;
    if (text_transform != CSS_VALUE_NONE) return false;
    if (has_small_caps(lycon)) return false;
    (void)trim_cjk_spacing;
    if (break_all || break_word) return false;
    if (lycon->font.style->letter_spacing != 0.0f) return false;
    return true;
}

static bool measure_shaped_simple_latin_run(LayoutContext* lycon, const unsigned char* str,
                                            const unsigned char* text_end,
                                            CssEnum text_transform,
                                            bool trim_cjk_spacing, bool break_all,
                                            bool break_word, int* out_bytes,
                                            float* out_width,
                                            uint32_t* out_first_codepoint,
                                            uint32_t* out_last_codepoint) {
    if (!str || !text_end || str >= text_end || !out_bytes || !out_width ||
        !out_first_codepoint || !out_last_codepoint) {
        return false;
    }
    if (!can_shape_simple_latin_run(lycon, text_transform, trim_cjk_spacing,
                                    break_all, break_word)) {
        return false;
    }
    if (!is_simple_latin_shaping_byte(*str)) return false;

    FontHandle* handle = lycon->font.font_handle ? lycon->font.font_handle
                                                  : lycon->font.style->font_handle;
    LayoutSimpleLatinRun result = {};
    if (!layout_measure_simple_latin_run(
            lycon, handle, str, (size_t)(text_end - str), &result)) {
        return false;
    }
    *out_bytes = (int)result.bytes; // INT_CAST_OK: text byte count
    *out_width = result.width;
    *out_first_codepoint = result.first_codepoint;
    *out_last_codepoint = result.last_codepoint;
    return true;
}

static void record_inline_fragment_union(DomNode* text_node, LayoutContext* lycon,
                                         float fragment_min_x, float fragment_max_x,
                                         float fragment_min_y, float fragment_max_y) {
    if (!text_node || !lycon || fragment_max_x <= fragment_min_x ||
        fragment_max_y <= fragment_min_y) {
        return;
    }
    DomNode* ancestor = text_node->parent;
    while (ancestor && ancestor->is_element()) {
        if (ancestor->view_type != RDT_VIEW_INLINE) {
            break;
        }
        ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(ancestor);
        layout_extend_fragment_union(span, FRAGMENT_UNION_INLINE,
                                     fragment_min_x, fragment_max_x,
                                     fragment_min_y, fragment_max_y);
        ancestor = ancestor->parent;
    }
}

static void record_soft_hyphen_inline_fragment(DomNode* text_node, LayoutContext* lycon,
                                               float fragment_width, float fragment_height) {
    if (!text_node || !lycon || fragment_width <= 0.0f || fragment_height <= 0.0f) {
        return;
    }
    float fragment_min_x = lycon->line.advance_x;
    float fragment_max_x = fragment_min_x + fragment_width;
    float fragment_min_y = lycon->block.advance_y;
    float fragment_max_y = fragment_min_y + fragment_height;
    record_inline_fragment_union(text_node, lycon, fragment_min_x, fragment_max_x,
                                 fragment_min_y, fragment_max_y);
}

/**
 * CSS Text 3 §4.1.2: Check if a codepoint has East Asian Width Fullwidth (F) or Wide (W).
 * Used for segment break transformation rules: segment breaks between two
 * East Asian F/W characters (neither Hangul) are removed instead of becoming spaces.
 * utf8proc_charwidth returns 2 for F and W characters, 1 for all others.
 */
/**
 * CSS Text 3 §4.1.2: Check if a codepoint is Hangul.
 * Segment break removal between East Asian Wide characters does not apply
 * when either side is Hangul.
 */
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

static inline float text_letter_spacing(FontProp* font, uint32_t cp,
                                        bool collapse_spaces) {
    (void)cp;
    (void)collapse_spaces;
    return font ? font->letter_spacing : 0.0f;
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

static inline bool control_fallback_keeps_primary_line_metrics(uint32_t cp) {
    return cp == 0x0081 || cp == 0x0082 || cp == 0x0084;
}

static inline float c1_control_normal_line_height(uint32_t cp, FontProp* font) {
    if (cp < 0x0080 || cp > 0x009F || !font || font->font_size <= 0 ||
        control_fallback_keeps_primary_line_metrics(cp)) {
        return 0.0f;
    }

    if (cp == 0x0080) {
        return font->font_size * (79.0f / 64.0f);
    }
    // CSS Text requires C1 controls to render visibly, but browser engines use
    return font->font_size * (45.0f / 32.0f);
}

static inline void normalize_c1_control_fallback_metrics(uint32_t cp, FontProp* font,
                                                         float* asc, float* desc,
                                                         float* normal_line_height) {
    float target = c1_control_normal_line_height(cp, font);
    if (target <= 0) return;

    float height = *asc + *desc;
    if (height > 0) {
        float scale = target / height;
        *asc *= scale;
        *desc *= scale;
    }
    if (normal_line_height) {
        *normal_line_height = target;
    }
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
    DomNode* current = node ? node->parent : nullptr;
    while (current) {
        if (!current->is_element()) {
            return CSS_VALUE_NORMAL;
        }
        DomElement* elem = lam::dom_require_element(current);
        bool has_specified_white_space = false;
        if (elem->specified_style) {
            CssDeclaration* ws_decl = style_tree_get_declaration(
                elem->specified_style, CSS_PROPERTY_WHITE_SPACE);
            if (ws_decl && ws_decl->value && ws_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                has_specified_white_space = true;
                CssEnum ws = ws_decl->value->data.keyword;
                if (is_concrete_white_space_value(ws)) {
                    return ws;
                }
            }
        }
        // CSS Text: an element's specified value overrides the inherited field
        // cached on its inline view; the latter may still contain the parent value.
        if (elem->blk && elem->block_mut()->white_space != 0) {
            CssEnum ws = elem->block()->white_space;
            if (is_concrete_white_space_value(ws)) {
                return ws;
            }
        }
        NameId tag = elem->tag();
        if (!has_specified_white_space &&
            (tag == MARKUP_NAME_PRE || tag == MARKUP_NAME_LISTING || tag == MARKUP_NAME_XMP)) {
            return CSS_VALUE_PRE;
        }
        current = current->parent;
    }
    return CSS_VALUE_NORMAL;  // default
}

int count_rendered_justify_opportunities(ViewText* text, const TextRect* rect,
                                         bool trim_trailing_space,
                                         bool* out_suppressed) {
    if (out_suppressed) *out_suppressed = false;
    if (!text || !rect) return 0;
    const char* text_data = (const char*)text->text_data();
    if (!text_data) return 0;

    for (DomNode* node = static_cast<DomNode*>(text)->parent; node; node = node->parent) {
        DomElement* element = node->as_element();
        if (!element || !element->specified_style) continue;
        CssDeclaration* declaration = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_TEXT_JUSTIFY);
        if (!declaration || !declaration->value ||
            declaration->value->type != CSS_VALUE_TYPE_KEYWORD) {
            continue;
        }
        CssEnum value = declaration->value->data.keyword;
        if (value == CSS_VALUE_INHERIT || value == CSS_VALUE_UNSET) continue;
        if (value == CSS_VALUE_NONE) {
            if (out_suppressed) *out_suppressed = true;
            return 0;
        }
        break;
    }

    CssEnum white_space = get_white_space_value(static_cast<DomNode*>(text));
    bool collapse_spaces = ws_collapse_spaces(white_space);
    bool collapse_newlines = ws_collapse_newlines(white_space);
    return count_justify_opportunities_impl(
        text_data + rect->start_index, rect->length,
        collapse_spaces, collapse_newlines,
        collapse_spaces && trim_trailing_space);
}

/**
 * Check if layout is in max-content measurement mode.
 * In max-content mode, never break lines - measure full unwrapped width.
 */
static inline bool is_min_content_mode(LayoutContext* lycon, DomNode* text_node) {
    if (!lycon) return false;
    if (lycon->available_space.width.is_min_content()) return true;
    ViewBlock* block = lycon->block.establishing_element;
    if (block && block->blk &&
        block->block()->given_width_type == CSS_VALUE_MIN_CONTENT) {
        return true;
    }
    for (DomNode* ancestor = text_node ? text_node->parent : nullptr;
         ancestor && ancestor->is_element(); ancestor = ancestor->parent) {
        DomElement* element = ancestor->as_element();
        if (element->blk && element->block()->given_width_type == CSS_VALUE_MIN_CONTENT) {
            return true;
        }
        if (element->specified_style) {
            CssDeclaration* width_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_WIDTH);
            if (width_decl && width_decl->value &&
                width_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
                width_decl->value->data.keyword == CSS_VALUE_MIN_CONTENT) {
                return true;
            }
        }
    }
    return false;
}

/**
 * Update effective line bounds based on floats in the current BlockContext.
 * Called at line start and potentially mid-line when floats are encountered.
 *
 * Uses the new unified BlockContext API instead of the old BFC system.
 */
void update_line_for_bfc_floats(LayoutContext* lycon, float query_height) {
    BlockContext* bfc = block_context_find_bfc(&lycon->block);

    if (!bfc) {
        lycon->line.effective_left = lycon->line.left;
        lycon->line.effective_right = lycon->line.right;
        lycon->line.has_float_intrusion = false;
        return;
    }

    float offset_x = lycon->block.bfc_offset_x;
    float offset_y = lycon->block.bfc_offset_y;

    float current_y_local = lycon->block.advance_y;
    float current_y_bfc = current_y_local + offset_y;
    // CSS 2.1 §9.5.1: For inline-blocks, query using the element's full height
    float effective_height = query_height > 0 ? query_height :
        (lycon->block.line_height > 0 ? lycon->block.line_height : 16.0f);

    FloatAvailableSpace space = block_context_space_at_y(bfc, current_y_bfc, effective_height,
        query_height <= 0.0f);

    float local_space_left = space.left - offset_x;
    float local_space_right = space.right - offset_x;

    float local_left = fmax(local_space_left, lycon->line.left);
    float local_right = fmin(local_space_right, lycon->line.right);

    bool has_actual_float = space.has_left_float || space.has_right_float;
    if (has_actual_float && (local_left > lycon->line.left || local_right < lycon->line.right)) {
        lycon->line.effective_left = local_left;
        lycon->line.effective_right = local_right;
        lycon->line.has_float_intrusion = true;

        if (lycon->line.advance_x < lycon->line.effective_left &&
            lycon->line.advance_x >= lycon->line.left) {
            lycon->line.advance_x = lycon->line.effective_left;
        }

    } else {
        lycon->line.effective_left = lycon->line.left;
        lycon->line.effective_right = lycon->line.right;
        lycon->line.has_float_intrusion = false;
    }
}

LineFillStatus node_has_line_filled(LayoutContext* lycon, DomNode* node);
LineFillStatus text_has_line_filled(LayoutContext* lycon, DomNode* text_node);

static bool inline_node_is_unbreakable_ascii(DomNode* node) {
    if (!node) return false;
    if (node->is_text()) {
        const unsigned char* text = node->text_data();
        for (const unsigned char* ch = text; ch && *ch; ch++) {
            if (*ch >= 0x80 || is_space(*ch) || *ch == '-' || *ch == '?') {
                return false;
            }
        }
        return text && *text;
    }
    if (!node->is_element()) return false;
    DomElement* element = lam::dom_require_element(node);
    if (resolve_display_value(element).outer != CSS_VALUE_INLINE) return false;
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (!inline_node_is_unbreakable_ascii(child)) return false;
    }
    return element->first_child != nullptr;
}

static bool inline_sequence_is_unbreakable_ascii(DomNode* node) {
    if (!node) return false;
    for (DomNode* current = node; current; current = current->next_sibling) {
        if (!inline_node_is_unbreakable_ascii(current)) return false;
    }
    return true;
}

static float ruby_simple_segment_inline_size(LayoutContext* lycon, DomNode* ruby) {
    float base_size = 0.0f;
    float annotation_size = 0.0f;
    for (DomNode* child = ruby && ruby->is_element()
             ? ruby->as_element()->first_child : nullptr;
         child; child = child->next_sibling) {
        if (child->is_element() && child->tag() == MARKUP_NAME_RP) continue;
        float child_size = calculate_max_content_width(lycon, child);
        if (child->is_element() && child->tag() == MARKUP_NAME_RT) {
            annotation_size += child_size;
        } else {
            base_size += child_size;
        }
    }
    return max(base_size, annotation_size);
}

LineFillStatus span_has_line_filled(LayoutContext* lycon, DomNode* span) {
    if (span && span->is_element() &&
        resolve_display_value(span).inner == CSS_VALUE_RUBY) {
        float ruby_size = ruby_simple_segment_inline_size(lycon, span);
        float line_right = lycon->line.has_float_intrusion ?
            lycon->line.effective_right : lycon->line.right;
        return lycon->line.advance_x + ruby_size > line_right + 0.001f
            ? RDT_LINE_FILLED : RDT_NOT_SURE;
    }
    DomNode* node = nullptr;
    if (span->is_element()) {
        node = lam::dom_require_element(span)->first_child;
    }
    if (node) {
        LineFillStatus result = node_has_line_filled(lycon, node);
        if (result) { return result; }
        if (inline_sequence_is_unbreakable_ascii(node)) {
            float fragment_width = calculate_max_content_width(lycon, span);
            float line_right = lycon->line.has_float_intrusion ?
                lycon->line.effective_right : lycon->line.right;
            if (lycon->line.advance_x + fragment_width > line_right + 0.001f) {
                return RDT_LINE_FILLED;
            }
        }
    }
    return RDT_NOT_SURE;
}

void adjust_text_bounds(ViewText* text);

extern void compute_span_bounding_box(ViewSpan* span, bool is_multi_line, struct FontHandle* fallback_fh);

/**
 * After trimming a text rect's trailing space, update the parent ViewText bounds
 * and shrink ancestor ViewSpan widths by the same amount.  This is safe to call
 * mid-layout because it only subtracts the known trim amount — it does NOT call
 * compute_span_bounding_box which can produce wrong results when some children
 * haven't been positioned yet (e.g., block-in-inline cases).
 */
static void propagate_text_trim(ViewText* text_view, float trim_amount) {
    adjust_text_bounds(text_view);

    ViewElement* parent = text_view->parent_view();
    while (parent && parent->view_type == RDT_VIEW_INLINE) {
        float span_right = parent->x + parent->width;
        float content_right = span_right;
        if (parent->bound) {
            content_right -= layout_axis_decoration_end(
                parent->boundary(), LAYOUT_AXIS_X);
        }
        float old_text_right = text_view->x + text_view->width + trim_amount;
        if ((int)old_text_right < (int)content_right) { // INT_CAST_OK: intentional
            break;  // text was not at the right edge; span width unaffected
        }
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

static void reset_line_parent_font(LayoutContext* lycon) {
    lycon->line.parent_font_ascender = lycon->block.init_ascender;
    lycon->line.parent_font_descender = lycon->block.init_descender;
    lycon->line.parent_font_size = lycon->font.style ? lycon->font.style->font_size
        : (lycon->block.init_ascender + lycon->block.init_descender);
    lycon->line.parent_font_handle = lycon->font.font_handle;
}

static void apply_bfc_initial_letter_exclusions(LayoutContext* lycon, BlockContext* bfc) {
    if (!lycon || !bfc || !bfc->initial_letters) return;

    float line_height = lycon->block.line_height > 0.0f ? lycon->block.line_height : 1.0f;
    float line_top_bfc = lycon->block.bfc_offset_y + lycon->block.advance_y;
    float line_bottom_bfc = line_top_bfc + line_height;
    bool constrains_line = false;

    for (InitialLetterBox* box = bfc->initial_letters; box; box = box->next) {
        if (box->margin_box_bottom <= line_top_bfc || box->margin_box_top >= line_bottom_bfc) {
            continue;
        }
        if (box->direction == CSS_VALUE_RTL) {
            float local_right = box->margin_box_left - lycon->block.bfc_offset_x;
            if (local_right < lycon->line.effective_right) {
                lycon->line.effective_right = max(local_right, lycon->line.effective_left);
                constrains_line = true;
            }
        } else {
            float local_left = box->margin_box_right - lycon->block.bfc_offset_x;
            if (local_left > lycon->line.effective_left) {
                lycon->line.effective_left = min(local_left, lycon->line.effective_right);
                constrains_line = true;
            }
        }
    }

    if (constrains_line) {
        lycon->line.has_float_intrusion = true;
        if (lycon->line.advance_x < lycon->line.effective_left &&
            lycon->line.advance_x >= lycon->line.left) {
            lycon->line.advance_x = lycon->line.effective_left;
        }
    }
}

void line_reset(LayoutContext* lycon) {
    lycon->line.max_ascender = lycon->line.max_descender =
        lycon->line.max_css_baseline_ascender = 0;
    lycon->line.ruby_annotation_min_line_height =
        lycon->line.ruby_annotation_over_shift = 0;
    lycon->line.initial_letter_origin_advance = 0;
    lycon->line.has_initial_letter = false;
    lycon->line.has_drop_initial_letter = false;
    lycon->line.reset_space();
    lycon->line.is_line_start = true;
    lycon->line.start_view = NULL;
    lycon->line.has_phantom_inline_fragment = false;
    lycon->line.line_start_font = lycon->font;
    lycon->line.prev_glyph_index = 0; // reset kerning state
    lycon->line.prev_codepoint = 0;   // reset codepoint kerning state
    lycon->line.prev_kerning_font_handle = nullptr;
    // IMPORTANT: Reset effective bounds to container bounds before float adjustment
    lycon->line.effective_left = lycon->line.left;
    lycon->line.effective_right = lycon->line.right;
    lycon->line.has_float_intrusion = lycon->line.has_replaced_content = false;
    lycon->line.atomic_inline_count = 0;
    lycon->line.has_cjk_text = false;
    lycon->line.max_top_bottom_height = lycon->line.max_top_height =
        lycon->line.max_bottom_height = 0;
    lycon->line.max_text_ascender = lycon->line.max_text_descender = 0;
    // line boxes are reused across breaks; stale clamped-baseline state would
    // leak a prior scroll-container tail into the next line's vertical map.
    lycon->line.clamped_baseline_tail = 0;
    lycon->line.has_clamped_baseline_tail = false;
    lycon->line.max_desc_before_last_text = 0;
    lycon->line.has_expanded_inline_lh = false;
    lycon->line.max_inline_line_height = lycon->line.max_atomic_inline_height = 0;
    lycon->line.has_different_inline_font = false;
    lycon->line.max_normal_line_height = 0;
    lycon->line.has_c1_control_text = lycon->line.has_non_c1_text =
        lycon->line.has_direct_block_text = false;
    lycon->line.c1_control_line_height = 0;
    lycon->line.trailing_letter_spacing = 0;
    // CSS 2.1 §10.8.1: top-level inline content inherits the block strut metrics.
    reset_line_parent_font(lycon);
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
    lycon->line.advance_x += lycon->line.inline_start_edge_pending;
    // This must happen BEFORE text-indent, because CSS 2.1 §16.1 says indent is
    BlockContext* bfc = block_context_find_bfc(&lycon->block);
    if (bfc) {
        adjust_line_for_floats(lycon);
        apply_bfc_initial_letter_exclusions(lycon, bfc);
    }

    float line_content_top = lycon->block.advance_y + lycon->block.lead_y;
    ViewBlock* line_container = lycon->view
        ? layout_nearest_block_ancestor(lycon->view->parent_view()) : nullptr;
    CssEnum css_writing_mode = line_container && line_container->is_element()
        ? layout_element_css_writing_mode(line_container->as_element())
        : CSS_VALUE_HORIZONTAL_TB;
    bool vertical_writing = css_writing_mode == CSS_VALUE_VERTICAL_LR ||
        css_writing_mode == CSS_VALUE_VERTICAL_RL;
    bool vertical_float_flow = vertical_writing && bfc &&
        (bfc->left_float_count > 0 || bfc->right_float_count > 0);
    bool count_exclusion = lycon->block.initial_letter_exclusion_lines > 0;
    bool geometry_exclusion = line_content_top <
        lycon->block.initial_letter_exclusion_bottom - 0.01f;
    bool shortens_line = lycon->block.initial_letter_exclusion_requires_intersection
        ? count_exclusion && geometry_exclusion
        : count_exclusion || geometry_exclusion;
    if (lycon->block.initial_letter_exclusion_width > 0.0f && shortens_line &&
        !vertical_float_flow) {
        // CSS Writing Modes maps the surrogate line width to physical inline
        if (lycon->block.direction == CSS_VALUE_RTL) {
            float initial_letter_right = lycon->line.right -
                lycon->block.initial_letter_exclusion_width;
            lycon->line.effective_right = min(lycon->line.effective_right,
                initial_letter_right);
            // RTL alignment must use the shortened line box; otherwise start alignment
            lycon->line.has_float_intrusion = true;
        } else {
            float float_adjusted_exclusion = lycon->line.effective_left +
                lycon->block.initial_letter_exclusion_width;
            lycon->line.effective_left = max(float_adjusted_exclusion,
                lycon->block.initial_letter_exclusion_right);
            lycon->line.advance_x = max(lycon->line.advance_x, lycon->line.effective_left);
        }
    }
    if (count_exclusion) lycon->block.initial_letter_exclusion_lines--;
    // CSS 2.1 §16.1: text-indent applies only to the first formatted line of a block container
    lycon->line.text_indent_offset = 0;
    if (lycon->block.is_first_line && lycon->block.text_indent != 0) {
        if (lycon->block.direction == CSS_VALUE_RTL) {
            // CSS 2.1 §16.1: In RTL, text-indent indents from the right (starting) edge
            lycon->line.text_indent_offset = lycon->block.text_indent;
        } else {
            lycon->line.advance_x += lycon->block.text_indent;
            lycon->line.effective_left += lycon->block.text_indent;
        }
        lycon->block.is_first_line = false;
    }
}

void line_init(LayoutContext* lycon, float left, float right) {
    lycon->line.left = left;  lycon->line.right = right;
    lycon->line.align_left = left;  lycon->line.align_right = right;
    lycon->line.effective_left = left;
    lycon->line.effective_right = right;
    lycon->line.has_float_intrusion = false;
    lycon->line.inline_start_edge_pending = 0;  // no pending inline edges at block start
    line_reset(lycon);
    lycon->line.vertical_align = CSS_VALUE_BASELINE;  // vertical-align does not inherit
    lycon->line.vertical_align_offset = 0;
}

static void align_forced_break_rect_to_line_baseline(LayoutContext* lycon) {
    if (!lycon || !lycon->view || lycon->view->view_type != RDT_VIEW_BR) return;
    ViewBlock* br_container = layout_nearest_block_ancestor(lycon->view->parent_view());
    bool nested_atomic_container = br_container &&
        (br_container->view_type == RDT_VIEW_INLINE_BLOCK ||
         br_container->view_type == RDT_VIEW_TABLE) &&
        lycon->block.establishing_element != br_container;
    if (nested_atomic_container) {
        // own formatting context; an ancestor line must not realign that child.
        return;
    }
    if (lycon->block.line_clamped) {
        return;
    }
    // fallback glyph extrema must not move the break independently of its text.
    if (!lycon->line.has_replaced_content &&
        !lycon->line.has_different_inline_font) return;

    View* br_view = lycon->view;
    float br_ascender = 0.0f;
    if (lycon->font.font_handle) {
        br_ascender = font_get_rendering_ascender(lycon->font.font_handle);
    }
    if (br_ascender <= 0.0f) {
        br_ascender = lycon->block.init_ascender > 0.0f
            ? lycon->block.init_ascender
            : br_view->height;
    }

    float baseline_line_height = 0.0f;
    float baseline_pos = line_baseline_position(lycon, &baseline_line_height);
    if (lycon->line.max_bottom_height > baseline_line_height) {
        baseline_pos += lycon->line.max_bottom_height - baseline_line_height;
    }

    br_view->y = lycon->block.advance_y + baseline_pos - br_ascender;
}

static float fixup_inline_vertical_decoration_height(ViewSpan* span) {
    LayoutInlineDecorationEdges edges = layout_inline_decoration_edges(span);
    return edges.top + edges.bottom;
}

static FontProp* fixup_inline_effective_font(LayoutContext* lycon, ViewSpan* span) {
    if (span && span->font) return span->font;
    DomNode* node = span ? span->parent : nullptr;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(node);
            if (elem->font) return elem->font;
        }
        node = node->parent;
    }
    return lycon ? lycon->font.style : nullptr;
}

static float fixup_inline_content_area_height(LayoutContext* lycon, ViewSpan* span) {
    if (!span) return 0.0f;
    float content_height = 0.0f;
    FontProp* font = fixup_inline_effective_font(lycon, span);
    if (font) {
        if (font->font_handle) {
            content_height = font_get_cell_height(font->font_handle);
        }
        if (content_height <= 0.0f && font->font_height > 0.0f) {
            content_height = font->font_height;
        }
        if (content_height <= 0.0f && (font->ascender > 0.0f || font->descender > 0.0f)) {
            content_height = font->ascender + font->descender;
        }
    }
    if (content_height <= 0.0f) {
        content_height = span->content_height;
    }
    return content_height;
}

static float fixup_inline_dom_rect_height(LayoutContext* lycon, ViewSpan* span) {
    if (!span) return 0.0f;
    return fixup_inline_content_area_height(lycon, span) +
        fixup_inline_vertical_decoration_height(span);
}

static void record_collapsed_line_fragment_for_inline_ancestors(
    LayoutContext* lycon, ViewText* text_view, TextRect* rect) {
    if (!lycon || !text_view || !rect || rect->height <= 0.0f) return;
    // The collapsed text fragment should materialize an inline box only when it
    if (!lycon->line.start_view || lycon->line.start_view == static_cast<View*>(text_view)) {
        return;
    }

    float fragment_min_x = rect->x;
    float fragment_max_x = rect->x + rect->width;
    float fragment_min_y = rect->y;
    float fragment_max_y = rect->y + rect->height;

    DomNode* ancestor = text_view->parent;
    while (ancestor && ancestor->is_element()) {
        if (ancestor->view_type != RDT_VIEW_INLINE) break;
        ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(ancestor);
        layout_extend_fragment_union(span, FRAGMENT_UNION_COLLAPSED_LINE,
                                     fragment_min_x, fragment_max_x,
                                     fragment_min_y, fragment_max_y);
        if (lycon->block.line_height > span->content_height) {
            span->content_height = lycon->block.line_height;
        }
        ancestor = ancestor->parent;
    }
}

static bool line_trailing_space_is_vertical_atomic_gap(ViewText* text_view,
                                                       TextRect* text_rect);

void line_consume_trailing_collapsible_space(LayoutContext* lycon,
                                             bool trim_text_bounds,
                                             bool update_ancestor_bounds) {
    if (!lycon) return;
    // CSS 2.1 §16.6.1: collapsible trailing spaces do not occupy inline
    // space before the next inline item, even when no line break is forced.
    if (lycon->line.trailing_space_width > 0 && lycon->line.last_text_rect) {
        bool preserve_vertical_gap = line_trailing_space_is_vertical_atomic_gap(
            lycon->line.last_text_view, lycon->line.last_text_rect);
        float trim_amount = preserve_vertical_gap ? 0.0f :
            lycon->line.trailing_space_width;
        if (trim_text_bounds) {
            lycon->line.last_text_rect->width -= trim_amount;
        }
        lycon->line.advance_x -= trim_amount;
        lycon->line.trailing_space_width = 0;
        if (trim_text_bounds && !preserve_vertical_gap &&
            lycon->line.last_text_rect->width <= 0.01f && lycon->line.last_text_view) {
            record_collapsed_line_fragment_for_inline_ancestors(
                lycon, lycon->line.last_text_view, lycon->line.last_text_rect);
        }
        if (update_ancestor_bounds && lycon->line.last_text_view) {
            propagate_text_trim(lycon->line.last_text_view, trim_amount);
        }
        lycon->line.committed_trailing_rect = nullptr;
        lycon->line.committed_trailing_view = nullptr;
        lycon->line.committed_trailing_space = 0;
    } else if (lycon->line.committed_trailing_space > 0 &&
               lycon->line.committed_trailing_rect) {
        bool preserve_vertical_gap = line_trailing_space_is_vertical_atomic_gap(
            lycon->line.committed_trailing_view,
            lycon->line.committed_trailing_rect);
        float trim_amount = preserve_vertical_gap ? 0.0f :
            lycon->line.committed_trailing_space;
        if (trim_text_bounds) {
            lycon->line.committed_trailing_rect->width -= trim_amount;
        }
        lycon->line.advance_x -= trim_amount;
        if (trim_text_bounds && !preserve_vertical_gap &&
            lycon->line.committed_trailing_rect->width <= 0.01f &&
            lycon->line.committed_trailing_view) {
            record_collapsed_line_fragment_for_inline_ancestors(
                lycon, lycon->line.committed_trailing_view,
                lycon->line.committed_trailing_rect);
        }
        if (update_ancestor_bounds && lycon->line.committed_trailing_view) {
            propagate_text_trim(lycon->line.committed_trailing_view, trim_amount);
        }
        lycon->line.committed_trailing_rect = nullptr;
        lycon->line.committed_trailing_view = nullptr;
        lycon->line.committed_trailing_space = 0;
    }
}

/**
 * Recursively fix height of collapsed inline spans on a line with visible content.
 * Empty inline elements contribute their line-height strut to line layout, but
 * browser DOMRects expose the inline content area plus vertical decorations.
 * compute_span_bounding_box sets 0×0 for truly empty spans; this restores the
 * visual border-box height after the line is known to contain real content.
 */
static void fixup_collapsed_inline_spans(LayoutContext* lycon, ViewSpan* span) {
    if (!lycon || !span) return;
    auto visit = [](View* view) -> bool {
        return view->view_type == RDT_VIEW_INLINE;
    };
    auto finish = [&](View* view) {
        ViewSpan* current = lam::view_require<RDT_VIEW_INLINE>(view);
        if (current->height == 0.0f) {
            if (current->content_height > 0.0f) {
                current->height = fixup_inline_dom_rect_height(lycon, current);
            }
        } else if (current->content_height > 0.0f &&
                   layout_span_children_have_no_line_content(current)) {
            float target_height = fixup_inline_dom_rect_height(lycon, current);
            if (current->height < target_height) current->height = target_height;
        }
    };
    layout_walk_inline_views(static_cast<View*>(span), visit, finish, false);
}

static bool view_is_non_rendered_table_marker(View* view) {
    if (!view || view->view_type != RDT_VIEW_NONE) return false;
    DomElement* elem = lam::dom_as<DOM_NODE_ELEMENT>(static_cast<DomNode*>(view));
    if (!elem) return false;
    return elem->display.inner == CSS_VALUE_TABLE_COLUMN ||
        elem->display.inner == CSS_VALUE_TABLE_COLUMN_GROUP ||
        elem->display.inner == CSS_VALUE_TABLE_CAPTION;
}

static void finalize_non_rendered_table_markers_walk(View* view, float line_top,
                                                     float baseline_y) {
    auto finalize = [&](View* current) -> bool {
        if (view_is_non_rendered_table_marker(current)) {
            float line_delta = current->y - line_top;
            if (line_delta < 0.0f) line_delta = -line_delta;
            if (line_delta <= 0.5f) {
                current->y = baseline_y;
                current->width = 0.0f;
                current->height = 0.0f;
            }
        }
        return false;
    };
    auto no_finish = [](View*) {};
    layout_walk_inline_views(view, finalize, no_finish, false);
}

static void finalize_non_rendered_table_markers_for_line(LayoutContext* lycon) {
    if (!lycon || !lycon->line.start_view) return;
    float line_top = lycon->block.advance_y;
    float baseline_y = line_top + lycon->line.max_ascender;
    finalize_non_rendered_table_markers_walk(lycon->line.start_view,
                                             line_top, baseline_y);
}

static void contribute_block_root_strut(LayoutContext* lycon) {
    if (!lycon || lycon->line.has_expanded_inline_lh) return;
    if (lycon->block.line_height_is_normal &&
        (lycon->line.max_ascender > 0.0f ||
         lycon->line.max_descender > 0.0f ||
         !lycon->line.has_replaced_content)) {
        return;
    }

    float ascender = lycon->block.init_ascender;
    float descender = lycon->block.init_descender;
    float content_height = ascender + descender;
    if (content_height <= 0.0f) return;
    // CSS Inline 3: the block container generates a root inline box whose
    float half_leading = (lycon->block.line_height - content_height) / 2.0f;
    ascender += half_leading;
    descender += half_leading;

    if (ascender > 0.0f) {
        lycon->line.max_ascender = max(lycon->line.max_ascender, ascender);
    }
    if (descender > 0.0f) {
        lycon->line.max_descender = max(lycon->line.max_descender, descender);
    }
}

static bool line_has_only_zero_sized_atomic(LayoutContext* lycon) {
    if (!lycon || !lycon->line.has_replaced_content ||
        lycon->line.max_atomic_inline_height > 0.0f ||
        lycon->line.last_text_rect || lycon->line.has_c1_control_text ||
        lycon->line.has_non_c1_text || lycon->line.max_top_bottom_height > 0.0f ||
        lycon->line.max_top_height > 0.0f || lycon->line.max_bottom_height > 0.0f) {
        return false;
    }
    return lycon->block.line_height <= 0.0f;
}

static bool line_trailing_space_is_vertical_atomic_gap(ViewText* text_view,
                                                       TextRect* text_rect) {
    if (!text_view || !text_rect ||
        layout_text_rect_has_painted_codepoint(text_view, text_rect)) {
        return false;
    }
    ViewBlock* parent = layout_nearest_block_ancestor(text_view->parent_view());
    if (!parent || !layout_block_inline_axis_is_vertical(parent)) return false;

    DomNode* next = static_cast<DomNode*>(text_view)->next_sibling;
    next = layout_first_view_with_type(next);
    return next && (next->view_type == RDT_VIEW_INLINE_BLOCK ||
                    next->view_type == RDT_VIEW_TABLE);
}

void line_break(LayoutContext* lycon) {
    line_consume_trailing_collapsible_space(lycon, true, true);
    // CSS Text 3 §4.1.3: Hanging spaces (U+3000, pre-wrap spaces) at end of line
    if (lycon->line.hanging_space_width > 0) {
        float hang_trim = lycon->line.hanging_space_text_trim;
        if (hang_trim > 0 && lycon->line.last_text_rect) {
            float remaining = lycon->line.last_text_rect->width - hang_trim;
            if (remaining > 0.01f) {
                lycon->line.last_text_rect->hanging_trim = hang_trim;
            }
        }
        // CSS Text 3 §4.1.3: In RTL, trailing whitespace hangs past the inline-end
        if (lycon->block.direction == CSS_VALUE_RTL) {
            lycon->line.rtl_hanging_space = lycon->line.hanging_space_width;
        }
        lycon->line.advance_x -= lycon->line.hanging_space_width;
        lycon->line.hanging_space_width = 0;
        lycon->line.hanging_space_text_trim = 0;
    }
    lycon->block.max_width = max(lycon->block.max_width, lycon->line.advance_x);
    if (lycon->line.trailing_letter_spacing != 0) {
        lycon->line.trailing_letter_spacing = 0;
    }
    // CSS Inline §5.2.1: When trailing whitespace is "collapsed away" at the end of a line,
    if (lycon->line.has_replaced_content &&
        lycon->line.last_text_rect && lycon->line.last_text_rect->width <= 0 &&
        lycon->line.max_descender > lycon->line.max_desc_before_last_text) {
        lycon->line.max_descender = lycon->line.max_desc_before_last_text;
    }
    // explicit zero line-height must not be expanded by the font root strut.
    if (!line_has_only_zero_sized_atomic(lycon)) {
        contribute_block_root_strut(lycon);
    }
    finalize_non_rendered_table_markers_for_line(lycon);
    // CSS 2.1 §10.8.1: The strut is a zero-width inline box with the block's font
    if (lycon->line.max_ascender > lycon->block.init_ascender ||
        lycon->line.max_descender > lycon->block.init_descender ||
        lycon->line.has_different_inline_font ||
        lycon->line.has_replaced_content ||
        lycon->line.max_top_bottom_height > 0 ||
        lycon->line.max_top_height > 0 ||
        lycon->line.max_bottom_height > 0) {
        View* view = lycon->line.start_view;
        if (view) {
            FontBox pa_font = lycon->font;
            lycon->font = lycon->line.line_start_font;
            reset_line_parent_font(lycon);
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

    align_forced_break_rect_to_line_baseline(lycon);

    line_align(lycon);
    place_rtl_initial_letter_line(lycon);
    // CSS Text 3 §4.1.3: RTL hanging space text rect adjustment.
    if (lycon->line.rtl_hanging_space > 0 && lycon->line.last_text_rect) {
        lycon->line.last_text_rect->x -= lycon->line.rtl_hanging_space;
        lycon->line.rtl_hanging_space = 0;
    }
    // CSS 2.1 10.8.1: Line height controls vertical spacing between line boxes
    ViewBlock* line_owner = lycon->block.establishing_element;
    if ((!line_owner || !line_owner->blk ||
         line_owner->block()->dominant_baseline != CSS_VALUE_TEXT_TOP) &&
        lycon->view && lycon->view->is_block()) {
        ViewBlock* view_block = lam::view_require_block(lycon->view);
        if (view_block->blk &&
            view_block->block()->dominant_baseline == CSS_VALUE_TEXT_TOP) {
            line_owner = view_block;
        }
    }
    if (!line_owner || !line_owner->blk ||
        line_owner->block()->dominant_baseline != CSS_VALUE_TEXT_TOP) {
        ViewElement* line_start_parent = lycon->line.start_view
            ? lycon->line.start_view->parent_view() : nullptr;
        ViewBlock* line_block = layout_nearest_block_ancestor(line_start_parent);
        if (line_block && line_block->blk &&
            line_block->block()->dominant_baseline == CSS_VALUE_TEXT_TOP) {
            line_owner = line_block;
        }
    }
    bool dominant_text_top = line_owner && line_owner->blk &&
        line_owner->block()->dominant_baseline == CSS_VALUE_TEXT_TOP;
    if (dominant_text_top && lycon->line.max_text_ascender > 0.0f) {
        // the block strut must not reintroduce its larger font ascent/descent.
        lycon->line.max_ascender = max(lycon->line.max_text_ascender,
            lycon->line.max_atomic_inline_height);
        lycon->line.max_descender = lycon->line.max_text_descender;
    }
    float font_line_height = lycon->line.max_ascender + lycon->line.max_descender;
    float css_line_height = lycon->block.line_height;
    // CSS 2.1: line-height: 0 is a valid explicit value (not a fallback case)
    if (lycon->block.line_height_is_normal && css_line_height <= 0) {
        css_line_height = font_line_height;
    }
    // CSS 2.1 10.8.1 half-leading model:
    // CSS 2.1 §10.8.1: For lines with inline-blocks/replaced elements, always use
    bool has_mixed_fonts;
    if (lycon->line.has_replaced_content && font_line_height > css_line_height) {
        has_mixed_fonts = true;
    } else {
        has_mixed_fonts = (font_line_height > css_line_height + 2);
    }
    float used_line_height;
    // CSS 2.1 §10.8.1: font backends may round ascender/descender metrics to integer
    float base_metric_excess = (lycon->block.init_ascender + lycon->block.init_descender) - css_line_height;
    if (base_metric_excess > 0 && base_metric_excess <= 2 && !lycon->line.has_replaced_content &&
        font_line_height > css_line_height + 2) {
        font_line_height -= base_metric_excess;
        has_mixed_fonts = (font_line_height > css_line_height + 2);
    }

    if (has_mixed_fonts) {
        // CSS 2.1 §10.8.1: The line box height is the distance between the
        float explicit_inline_height = css_line_height;
        if (!lycon->block.line_height_is_normal &&
            lycon->line.max_inline_line_height > explicit_inline_height) {
            explicit_inline_height = lycon->line.max_inline_line_height;
        }
        bool atomic_expands_line = !lycon->block.line_height_is_normal &&
            lycon->line.max_atomic_inline_height > explicit_inline_height + 0.5f;
        if (!lycon->block.line_height_is_normal &&
            lycon->line.has_replaced_content &&
            lycon->line.max_inline_line_height > css_line_height &&
            !atomic_expands_line) {
            // CSS 2.1 §10.8.1: an inline box's line-height, not its glyph
            used_line_height = explicit_inline_height;
        } else if (lycon->line.has_replaced_content || lycon->block.line_height_is_normal ||
            lycon->line.has_expanded_inline_lh ||
            lycon->line.has_different_inline_font) {
            used_line_height = max(css_line_height, font_line_height);
            // CSS 2.1 §10.8.1: For normal line-height with mixed fonts, each inline box
            if (lycon->block.line_height_is_normal && lycon->line.max_normal_line_height > used_line_height) {
                used_line_height = lycon->line.max_normal_line_height;
            }
        } else {
            used_line_height = css_line_height;
        }
    } else {
        used_line_height = css_line_height;
    }

    if (lycon->block.line_height_is_normal &&
        lycon->line.has_c1_control_text && !lycon->line.has_non_c1_text &&
        lycon->line.c1_control_line_height > 0 && !lycon->line.has_replaced_content) {
        used_line_height = max(css_line_height, lycon->line.c1_control_line_height);
    }

    if (lycon->line.has_cjk_text && lycon->block.line_height_is_normal) {
        float cjk_lh = get_cjk_system_line_height(lycon->line.parent_font_size);
        if (cjk_lh > used_line_height) {
            used_line_height = cjk_lh;
        }
    }

    if (lycon->line.ruby_annotation_min_line_height > used_line_height) {
        used_line_height = lycon->line.ruby_annotation_min_line_height;
    }

    if (dominant_text_top && lycon->line.max_text_ascender > 0.0f) {
        // CSS Inline's text-top baseline uses the actual line union rather than
        used_line_height = font_line_height;
    }

    if (layout_quirks_block_ignores_line_height(lycon, nullptr)) {
        used_line_height = font_line_height;
        if (lycon->block.line_height_is_normal &&
            lycon->line.max_normal_line_height > used_line_height) {
            used_line_height = lycon->line.max_normal_line_height;
        }
        if (used_line_height <= 0.0f && lycon->view &&
            lycon->view->view_type == RDT_VIEW_BR) {
            used_line_height = css_line_height;
        }
    }
    // CSS 2.1 §10.8.1: Fix height of collapsed-content inline elements.
    // CSS 2.1 §9.4.2: Line boxes with no text/content/etc. are zero-height, so
    if (used_line_height > 0 && lycon->line.start_view) {
        View* v = lycon->line.start_view;
        DomNode* line_parent = v->parent;
        while (v) {
            if (v->view_type == RDT_VIEW_INLINE) {
                fixup_collapsed_inline_spans(lycon, lam::view_require<RDT_VIEW_INLINE>(v));
            }
            DomNode* next = v->next_sibling;
            if (!next || v->parent != line_parent) break;
            v = next;
        }
    }
    // CSS 2.1 §10.8.1 second pass: expand line box for vertical-align:top/bottom elements.
    float max_tb = max(lycon->line.max_top_bottom_height,
        max(lycon->line.max_top_height, lycon->line.max_bottom_height));
    if (max_tb > used_line_height) {
        used_line_height = max_tb;
    }
    // CSS Writing Modes uses the physical inline-level box width as the
    ViewElement* line_parent_view = lycon->view ? lycon->view->parent_view() : nullptr;
    ViewBlock* line_parent = layout_nearest_block_ancestor(line_parent_view);
    if (layout_block_inline_axis_is_vertical(line_parent)) {
        used_line_height = max(used_line_height, lycon->line.max_atomic_inline_height);
    }

    lycon->block.advance_y += used_line_height;

    lycon->block.line_number++;
    bool reached_line_clamp = lycon->block.line_clamp > 0 &&
        lycon->block.line_number >= lycon->block.line_clamp &&
        !lycon->block.line_clamped;
    // CSS 2.1 10.8.1: Track last line's baseline offset for inline-block baseline alignment.
    lycon->block.last_line_ascender = lycon->block.advance_y - used_line_height + lycon->line.max_ascender;
    // CSS Flexbox §9.4: Track first line's baseline offset for flex baseline alignment.
    if (lycon->block.first_line_ascender == 0) {
        lycon->block.first_line_ascender = lycon->block.last_line_ascender;
    }
    // CSS Inline 3 §5: Track first/last line box metrics for text-box-trim.
    float trim_max_ascender = lycon->line.max_ascender;
    float trim_max_descender = lycon->line.max_descender;
    if (used_line_height > 0.0f &&
        trim_max_ascender + trim_max_descender > used_line_height &&
        !lycon->block.line_height_is_normal &&
        !lycon->line.has_replaced_content) {
        if (trim_max_ascender < used_line_height) {
            trim_max_descender = used_line_height - trim_max_ascender;
        } else {
            trim_max_ascender = used_line_height;
            trim_max_descender = 0.0f;
        }
    }
    if (lycon->block.first_line_max_ascender == 0 && lycon->block.first_line_max_descender == 0) {
        lycon->block.first_line_max_ascender = trim_max_ascender;
        lycon->block.first_line_max_descender = trim_max_descender;
    }
    lycon->block.last_line_max_ascender = trim_max_ascender;
    lycon->block.last_line_max_descender = trim_max_descender;

    if (reached_line_clamp) {
        if (lycon->line.last_text_rect && lycon->font.font_handle) {
            GlyphInfo ellipsis = font_get_glyph(lycon->font.font_handle, 0x2026); // U+2026 …
            float ellipsis_w = (ellipsis.id != 0) ? ellipsis.advance_x : lycon->font.current_font_size * 0.5f;
            TextRect* tr = lycon->line.last_text_rect;
            float max_w = lycon->line.right - tr->x;
            if (tr->width + ellipsis_w > max_w && max_w > ellipsis_w) {
                tr->width = max_w - ellipsis_w;
            }
            tr->has_trailing_ellipsis = true;
        }
        lycon->block.line_clamped = true;
        lycon->block.line_clamp_advance_y = lycon->block.advance_y;
        lycon->block.line_clamp_last_line_ascender = lycon->block.last_line_ascender;
        lycon->block.line_clamp_last_line_max_ascender = trim_max_ascender;
        lycon->block.line_clamp_last_line_max_descender = trim_max_descender;
    }

    line_reset(lycon);
    FontProp* block_font = lycon->block.establishing_element ?
        lycon->block.establishing_element->font : lycon->block.block_container_font;
    if (block_font) {
        lycon->line.line_start_font.style = block_font;
        lycon->line.line_start_font.font_handle = block_font->font_handle ?
            block_font->font_handle : lycon->line.line_start_font.font_handle;
        lycon->line.line_start_font.current_font_size = block_font->font_size;
        lycon->line.parent_font_size = block_font->font_size;
        lycon->line.parent_font_handle = lycon->line.line_start_font.font_handle;
    }
}
// CSS Text 3 §5.2: Measure the width of the first word starting from `str`.
static float measure_first_word_width(LayoutContext* lycon, const unsigned char* str,
                                      const unsigned char* text_end, CssEnum text_transform,
                                      bool collapse_spaces) {
    float width = 0.0f;
    bool word_start = true;
    uint32_t prev_codepoint = 0;

    while (*str && is_space(*str)) str++;
    if (!*str) return 0.0f;

    while (str < text_end && *str && !is_space(*str)) {
        int shaped_bytes = 0;
        float shaped_width = 0.0f;
        uint32_t first_cp = 0;
        uint32_t last_cp = 0;
        if (measure_shaped_simple_latin_run(lycon, str, text_end, text_transform,
                                            false, false, false, &shaped_bytes,
                                            &shaped_width, &first_cp, &last_cp)) {
            width += text_kerning_adjustment(lycon, prev_codepoint, first_cp) + shaped_width;
            prev_codepoint = last_cp;
            word_start = false;
            str += shaped_bytes;
            continue;
        }

        uint32_t codepoint = *str;
        int char_bytes = 1;
        if (codepoint >= 128) {
            int bytes = str_utf8_decode((const char*)str, (size_t)(text_end - str), &codepoint);
            if (bytes > 0) char_bytes = bytes;
        }

        if (has_id_line_break_class(codepoint)) {
            if (width == 0.0f) {
                uint32_t tt_out[3];
                int tt_count = apply_text_transform_full(codepoint, text_transform, word_start, tt_out);
                codepoint = tt_out[0];
                GlyphInfo ginfo = font_get_glyph(lycon->font.font_handle, codepoint);
                width += (ginfo.id != 0) ? ginfo.advance_x : layout_font_em_size(lycon);
                for (int tti = 1; tti < tt_count; tti++) {
                    if (text_codepoint_has_zero_advance(tt_out[tti])) continue;
                    GlyphInfo eg = font_get_glyph(lycon->font.font_handle, tt_out[tti]);
                    if (eg.id != 0) width += eg.advance_x;
                }
            }
            break;
        }
        if (codepoint == 0x200B) break;
        if (codepoint == 0x00AD) break;

        {
        uint32_t tt_out[3];
        int tt_count = apply_text_transform_full(codepoint, text_transform, word_start, tt_out);
        codepoint = tt_out[0];
        for (int tti = 1; tti < tt_count; tti++) {
            if (text_codepoint_has_zero_advance(tt_out[tti])) continue;
            GlyphInfo eg = font_get_glyph(lycon->font.font_handle, tt_out[tti]);
            if (eg.id != 0) width += eg.advance_x +
                text_letter_spacing(lycon->font.style, tt_out[tti], collapse_spaces);
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
            str += char_bytes;
            continue;
        } else if (unicode_space_em > 0.0f) {
            float sc_scale = is_small_caps_lower ?
                font_get_small_caps_scale(lycon->font.font_handle) : 1.0f;
            char_width = unicode_space_em * layout_font_em_size(lycon) * sc_scale;
        } else {
            GlyphInfo ginfo = font_get_glyph(lycon->font.font_handle, codepoint);
            float sc_scale = is_small_caps_lower ?
                font_get_small_caps_scale(lycon->font.font_handle) : 1.0f;
            char_width = (ginfo.id != 0) ? ginfo.advance_x * sc_scale
                                         : layout_font_em_size(lycon) * sc_scale;
        }
        width += text_kerning_adjustment(lycon, prev_codepoint, codepoint)
            + char_width + text_letter_spacing(lycon->font.style, codepoint, collapse_spaces);
        prev_codepoint = codepoint;
        str += char_bytes;
    }
    return width;
}

LineFillStatus text_has_line_filled(LayoutContext* lycon, DomNode* text_node) {
    const char* text = (const char*)text_node->text_data();
    if (!text) return RDT_LINE_NOT_FILLED;  // null check

    unsigned char* str = (unsigned char*)text;
    unsigned char* text_end = str + strlen(text);
    float text_width = 0.0f;
    CssEnum text_transform = get_text_transform_from_node(text_node);
    bool collapse_spaces = ws_collapse_spaces(get_white_space_value(text_node));
    bool trim_cjk_spacing = should_apply_text_spacing_trim(lycon, text_node);
    bool is_word_start = true;  // First character is always word start
    bool has_break_opportunity = false;  // track if hyphen/break found before overflow
    uint32_t prev_codepoint =
        lycon->line.prev_kerning_font_handle == lycon->font.font_handle
            ? lycon->line.prev_codepoint : 0;

    do {
        if (is_space(*str)) return RDT_LINE_NOT_FILLED;

        {
            int shaped_bytes = 0;
            float shaped_width = 0.0f;
            uint32_t first_cp = 0;
            uint32_t last_cp = 0;
            if (measure_shaped_simple_latin_run(lycon, str, text_end, text_transform,
                                                trim_cjk_spacing, false, false,
                                                &shaped_bytes, &shaped_width,
                                                &first_cp, &last_cp)) {
                text_width += text_kerning_adjustment(
                    lycon, prev_codepoint, first_cp) + shaped_width;
                prev_codepoint = last_cp;
                is_word_start = false;
                str += shaped_bytes;

                float line_right = lycon->line.has_float_intrusion ?
                                   lycon->line.effective_right : lycon->line.right;
                float terminal_trim = line_terminal_letter_spacing_trim(
                    lycon->font.style->letter_spacing);
                if (lycon->line.advance_x + text_width - terminal_trim > line_right + 0.001f) {
                    return has_break_opportunity ? RDT_LINE_NOT_FILLED : RDT_LINE_FILLED;
                }
                continue;
            }
        }

        uint32_t codepoint = *str;
        int char_bytes = 1;
        if (codepoint >= 128) {
            int bytes = str_utf8_decode((const char*)str, (size_t)(text_end - str), &codepoint);
            if (bytes <= 0) codepoint = *str;
            else char_bytes = bytes;
        }
        // CSS Text 3 §4.1.3: U+3000 IDEOGRAPHIC SPACE is hangable — treat as space
        if (codepoint == 0x3000) return RDT_LINE_NOT_FILLED;
        {
        uint32_t tt_out[3];
        int tt_count = apply_text_transform_full(codepoint, text_transform, is_word_start, tt_out);
        codepoint = tt_out[0];
        for (int tti = 1; tti < tt_count; tti++) {
            if (text_codepoint_has_zero_advance(tt_out[tti])) continue;
            text_width += measure_current_glyph_advance(lycon, tt_out[tti], trim_cjk_spacing) +
                text_letter_spacing(lycon->font.style, tt_out[tti], collapse_spaces);
        }
        }
        bool is_small_caps_lower = false;
        if (has_small_caps(lycon)) {
            uint32_t original = codepoint;
            codepoint = apply_small_caps(codepoint);
            is_small_caps_lower = (codepoint != original);
        }
        is_word_start = false;  // Only first char is word start in this context

        if (*str == '-' || codepoint == 0x00AD) {
            has_break_opportunity = true;
        }

        float unicode_space_em = get_unicode_space_width_em(codepoint);
        if (unicode_space_em < 0.0f) {
            if (codepoint == 0x200B) return RDT_LINE_NOT_FILLED;
            str += char_bytes;
            continue;
        }

        text_width += text_kerning_adjustment(lycon, prev_codepoint, codepoint);
        if (unicode_space_em > 0.0f) {
            float sc_scale = is_small_caps_lower ?
                font_get_small_caps_scale(lycon->font.font_handle) : 1.0f;
            text_width += unicode_space_em * layout_font_em_size(lycon) * sc_scale;
        } else {
            float sc_scale = is_small_caps_lower ?
                font_get_small_caps_scale(lycon->font.font_handle) : 1.0f;
            text_width += measure_current_glyph_advance(
                lycon, codepoint, trim_cjk_spacing) * sc_scale;
        }
        // CSS 2.1 §16.4: letter-spacing is added after every character
        text_width += text_letter_spacing(lycon->font.style, codepoint, collapse_spaces);
        prev_codepoint = codepoint;
        str += char_bytes;
        float line_right = lycon->line.has_float_intrusion ?
                           lycon->line.effective_right : lycon->line.right;
        float terminal_trim = line_terminal_letter_spacing_trim(
            lycon->font.style->letter_spacing);
        if (lycon->line.advance_x + text_width - terminal_trim > line_right + 0.001f) { // line filled up
            // CSS Text 3 §5.2: If a break opportunity (hyphen, soft hyphen, ZWSP,
            if (has_break_opportunity) return RDT_NOT_SURE;
            return RDT_LINE_FILLED;
        }
    } while (*str);  // end of text
    return RDT_NOT_SURE;
}

LineFillStatus node_has_line_filled(LayoutContext* lycon, DomNode* node) {
    float saved_advance_x = lycon->line.advance_x;
    do {
        LineFillStatus result = RDT_NOT_SURE;
        if (node->is_text()) {
            result = text_has_line_filled(lycon, node);
        }
        else if (node->is_element()) {
            // starts on a new line, so it cannot contribute to filling the current line.
            if (node->tag() == MARKUP_NAME_BR) {
                lycon->line.advance_x = saved_advance_x;
                return RDT_LINE_NOT_FILLED;
            }
            CssEnum outer_display = resolve_display_value(node).outer;
            if (outer_display == CSS_VALUE_BLOCK) {
                lycon->line.advance_x = saved_advance_x;
                return RDT_LINE_NOT_FILLED;
            }
            else if (outer_display == CSS_VALUE_INLINE) {
                result = span_has_line_filled(lycon, node);
            }
        }
        if (result) {
            lycon->line.advance_x = saved_advance_x;
            return result;
        }
        if (!inline_node_is_unbreakable_ascii(node)) {
            lycon->line.advance_x = saved_advance_x;
            return RDT_NOT_SURE;
        }
        lycon->line.advance_x += calculate_max_content_width(lycon, node);
        node = node->next_sibling;
    } while (node);
    lycon->line.advance_x = saved_advance_x;
    return RDT_NOT_SURE;
}

LineFillStatus view_has_line_filled(LayoutContext* lycon, View* view) {
    DomNode* node = view->next_sibling;
    DomNode* parent = view->parent;
    if (parent && parent->is_element() &&
        resolve_display_value(parent).inner == CSS_VALUE_RUBY) {
        while (node && node->is_element() &&
               (node->tag() == MARKUP_NAME_RT || node->tag() == MARKUP_NAME_RP)) {
            node = node->next_sibling;
        }
    }
    if (node) {
        LineFillStatus result = node_has_line_filled(lycon, node);
        if (result) { return result; }
    }
    view = view->parent;
    if (view) {
        if (view->view_type == RDT_VIEW_BLOCK) { return RDT_LINE_NOT_FILLED; }
        else if (view->view_type == RDT_VIEW_INLINE) {
            // CSS 2.1 §8.3: When checking if the line is filled from inside an
            ViewSpan* sp = lam::view_require<RDT_VIEW_INLINE>(view);
            float right_edge = layout_inline_end_edge(sp);
            lycon->line.advance_x += right_edge;
            float line_right = lycon->line.has_float_intrusion ?
                lycon->line.effective_right : lycon->line.right;
            if (right_edge > 0.001f &&
                lycon->line.advance_x > line_right + 0.001f) {
                float min_content = calculate_min_content_width(lycon, sp);
                float max_content = calculate_max_content_width(lycon, sp);
                if (min_content >= max_content - 0.001f) {
                    lycon->line.advance_x -= right_edge;
                    return RDT_LINE_FILLED;
                }
            }
            LineFillStatus result = view_has_line_filled(lycon, view);
            lycon->line.advance_x -= right_edge;
            return result;
        }
    }
    return RDT_NOT_SURE;
}

static void include_text_rect_bounds(ViewText* text, const TextRect* rect) {
    float right = max(text->x + text->width, rect->x + rect->width);
    float bottom = max(text->y + text->height, rect->y + rect->height);
    text->x = min(text->x, rect->x);
    text->y = min(text->y, rect->y);
    text->width = right - text->x;
    text->height = bottom - text->y;
}

static bool text_range_has_non_collapsed_content(ViewText* text,
                                                 TextRect* rect,
                                                 int text_length) {
    if (!text || !text->text || !rect || text_length <= 0) return false;
    if (!ws_collapse_spaces(get_white_space_value(text))) return true;

    const char* start = text->text + rect->start_index;
    const char* end = start + text_length;
    for (const char* current = start; current < end; current++) {
        char c = *current;
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f') {
            return true;
        }
    }
    return false;
}

static bool initial_letter_block_trims_start_edge(const DomNode* text_node,
                                                  const LayoutContext* lycon) {
    if (text_node && text_node->parent && text_node->parent->is_element()) {
        DomElement* pseudo = text_node->parent->as_element();
        if (pseudo && pseudo->parent && pseudo->parent->is_element()) {
            DomElement* owner = pseudo->parent->as_element();
            if (owner && owner->blk) {
                return (owner->block()->text_box_trim & TEXT_BOX_TRIM_START) != 0;
            }
        }
    }
    return lycon && lycon->block.establishing_element &&
        lycon->block.establishing_element->blk &&
        (lycon->block.establishing_element->block()->text_box_trim &
         TEXT_BOX_TRIM_START) != 0;
}

InitialLetterBoxInsets layout_initial_letter_box_insets(ViewText* text) {
    InitialLetterBoxInsets insets = {};
    ViewElement* pseudo = text ? text->parent_view() : NULL;
    if (!pseudo || !pseudo->bound) return insets;

    BoxEdges margin = layout_boundary_margin_edges(pseudo->boundary());
    BoxEdges border = layout_boundary_border_edges(pseudo->boundary());
    BoxEdges padding = layout_boundary_padding_edges(pseudo->boundary());
    for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
        insets.values[side] = margin.values[side] + border.values[side] +
            max(padding.values[side], 0.0f);
    }
    ViewBlock* block = text ? layout_nearest_block_ancestor(text->parent_view()) : nullptr;
    WritingMode writing_mode = block ? layout_block_writing_mode(block)
                                     : WM_HORIZONTAL_TB;
    if (writing_mode == WM_VERTICAL_LR || writing_mode == WM_VERTICAL_RL) {
        // physical margins must follow the surrogate's mapped axes; otherwise
        InitialLetterBoxInsets logical = {};
        // CSS Writing Modes maps block-start to physical right in vertical-rl;
        bool reverse_block_axis = writing_mode == WM_VERTICAL_RL;
        logical.top = reverse_block_axis ? insets.right : insets.left;
        bool sideways_lr = block && block->is_element() &&
            layout_element_css_writing_mode(block->as_element()) ==
                CSS_VALUE_SIDEWAYS_LR;
        logical.right = sideways_lr ? insets.top : insets.bottom;
        logical.bottom = reverse_block_axis ? insets.left : insets.right;
        logical.left = sideways_lr ? insets.bottom : insets.top;
        return logical;
    }
    return insets;
}

static float initial_letter_used_content_height(const LayoutContext* lycon,
                                                const TextRect* rect) {
    float height = lycon && lycon->font.font_handle ?
        font_get_cell_height(lycon->font.font_handle) : 0.0f;
    return height > 0.0f ? height : (rect ? rect->height : 0.0f);
}

static void initial_letter_avoid_bfc_floats(LayoutContext* lycon, TextRect* rect,
                                            const InitialLetterBoxInsets& insets) {
    if (!lycon || !rect) return;

    BlockContext* bfc = block_context_find_bfc(&lycon->block);
    if (!bfc || (bfc->left_float_count == 0 && bfc->right_float_count == 0)) return;

    float margin_box_height = initial_letter_used_content_height(lycon, rect) +
        insets.top + insets.bottom;
    if (margin_box_height <= 0.0f) return;
    float bfc_y = lycon->block.bfc_offset_y + rect->y - insets.top;
    FloatAvailableSpace space = block_context_space_at_y(bfc, bfc_y, margin_box_height);
    float local_left = space.left - lycon->block.bfc_offset_x;
    float local_right = space.right - lycon->block.bfc_offset_x;
    float margin_box_left = rect->x - insets.left;
    float margin_box_width = rect->width + insets.left + insets.right;

    if (lycon->block.direction != CSS_VALUE_RTL && space.has_left_float) {
        float shifted_left = max(margin_box_left, local_left);
        if (shifted_left > margin_box_left &&
            shifted_left + margin_box_width <= local_right) {
            // Initial letters are in-flow, but their full margin box cannot overlap
            float shift = shifted_left - margin_box_left;
            lycon->line.advance_x += shift;
            rect->x += shift;
        }
    } else if (lycon->block.direction == CSS_VALUE_RTL && space.has_right_float) {
        float shifted_left = min(margin_box_left, local_right - margin_box_width);
        if (shifted_left < margin_box_left && shifted_left >= local_left) {
            float shift = margin_box_left - shifted_left;
            lycon->line.advance_x -= shift;
            rect->x -= shift;
        }
    }
}

static float initial_letter_inherited_text_indent(LayoutContext* lycon,
                                                  ViewText* text) {
    if (!lycon || !text || !text->parent_view()) return 0.0f;
    return lycon->block.text_indent;
}

void output_text(LayoutContext* lycon, ViewText* text, TextRect* rect, int text_length, float text_width) {
    if (text_length <= 0) {
        log_error("output_text: text_length=%d, skipping (node=%s)", text_length, text->node_name());
        return;
    }
    rect->length = text_length;
    rect->width = text_width;
    rect->line_number = lycon->block.line_number;
    ViewBlock* text_line_parent = layout_nearest_block_ancestor(text->parent_view());
    bool horizontal_rtl_line = lycon->block.direction == CSS_VALUE_RTL &&
        (!text_line_parent || !layout_block_inline_axis_is_vertical(text_line_parent));
    bool rtl_inline_block_line = lycon->block.establishing_element &&
        lycon->block.establishing_element->view_type == RDT_VIEW_INLINE_BLOCK;
    if (horizontal_rtl_line &&
        (lycon->line.has_initial_letter || rtl_inline_block_line)) {
        rect->x = layout_rtl_inline_item_x(&lycon->line, text_width);
    }
    InitialLetterInfo initial_letter = {};
    bool is_initial_letter = layout_get_text_initial_letter_info(
        static_cast<DomNode*>(text), &initial_letter);
    bool is_raised_initial_letter = is_initial_letter && initial_letter.raised;
    InitialLetterBoxInsets initial_insets = is_initial_letter ?
        layout_initial_letter_box_insets(text) : InitialLetterBoxInsets{};
    if (is_initial_letter && !lycon->block.initial_letter_origin_offset_applied) {
        rect->y += initial_insets.top;
    }
    if (is_initial_letter) {
        lycon->line.has_initial_letter = true;
    }
    if (is_initial_letter && !is_raised_initial_letter) {
        lycon->line.has_drop_initial_letter = true;
    }
    if (!lycon->line.start_view) lycon->line.start_view = static_cast<View*>(text);
    ViewElement* text_parent = text->parent_view();
    if (!lycon->line.has_direct_block_text && text_parent && text_parent->is_block() &&
        text_range_has_non_collapsed_content(text, rect, text_length)) {
        lycon->line.has_direct_block_text = true;
    }
    if (is_initial_letter && !lycon->block.initial_letter_origin_offset_applied &&
        lycon->block.direction != CSS_VALUE_RTL) {
        float inherited_indent = initial_letter_inherited_text_indent(lycon, text);
        if (inherited_indent != 0.0f) {
            // its exclusion, otherwise later lines start at the stale cap edge.
            rect->x += inherited_indent;
            lycon->line.advance_x += inherited_indent;
        }
    }
    lycon->line.advance_x += text_width;
    if (is_initial_letter && lycon->font.font_handle) {
        initial_letter_avoid_bfc_floats(lycon, rect, initial_insets);
    }
    // CSS 2.1 §16.6.1: Commit trailing space info for cross-node line break trimming.
    if (lycon->line.trailing_space_width > 0) {
        lycon->line.committed_trailing_rect = rect;
        lycon->line.committed_trailing_view = text;
        lycon->line.committed_trailing_space = lycon->line.trailing_space_width;
    } else if (lycon->line.committed_trailing_rect != rect) {
        lycon->line.committed_trailing_rect = NULL;
        lycon->line.committed_trailing_view = NULL;
        lycon->line.committed_trailing_space = 0;
    }
    lycon->line.last_text_rect = rect;  // track for trailing whitespace trimming
    lycon->line.last_text_view = text;  // ViewText owner for bounds update after trimming
    // CSS 2.1 §8.3: Inline content has been placed on this line, so any pending
    lycon->line.inline_start_edge_pending = 0;

    if (is_initial_letter && !lycon->block.initial_letter_origin_offset_applied) {
        if (is_raised_initial_letter) {
            float overhang = max(0.0f, initial_letter.size - 1.0f) * lycon->block.line_height;
            if (!initial_letter_block_trims_start_edge(static_cast<DomNode*>(text), lycon)) {
                lycon->block.advance_y += overhang;
                lycon->line.initial_letter_origin_advance = overhang;
            } else {
                lycon->block.initial_letter_trimmed_start_candidate = max(
                    lycon->block.initial_letter_trimmed_start_candidate, overhang);
            }
            int following_lines = (int)ceilf(initial_letter.size - initial_letter.sink) - 1; // INT_CAST_OK: discrete initial-letter line count
            if (following_lines > 0) {
                lycon->block.initial_letter_exclusion_lines = max(
                    lycon->block.initial_letter_exclusion_lines, following_lines);
            }
            lycon->block.initial_letter_exclusion_bottom = max(
                lycon->block.initial_letter_exclusion_bottom, rect->y + rect->height);
            lycon->block.initial_letter_exclusion_requires_intersection = true;
        }
        else if (initial_letter.sink > 1.0f && initial_letter.sink < initial_letter.size) {
            // CSS Inline 3 §7.6.1: a sunken initial starts following inline
            lycon->block.advance_y += (initial_letter.sink - 1.0f) *
                lycon->block.line_height;
        }
        float margin_box_left = rect->x - initial_insets.left;
        float margin_box_right = rect->x + rect->width + initial_insets.right;
        float margin_box_top = rect->y - initial_insets.top;
        float margin_box_bottom = rect->y +
            initial_letter_used_content_height(lycon, rect) + initial_insets.bottom;
        // CSS Inline 3 §7.5.2 wraps later lines around the initial's margin
        lycon->block.initial_letter_exclusion_width = max(
            lycon->block.initial_letter_exclusion_width,
            margin_box_right - margin_box_left);
        lycon->block.initial_letter_exclusion_right = margin_box_right;
        lycon->block.initial_letter_margin_box_left = margin_box_left;
        lycon->block.initial_letter_margin_box_right = margin_box_right;
        lycon->block.initial_letter_margin_box_top = margin_box_top;
        lycon->block.initial_letter_margin_box_bottom = max(
            lycon->block.initial_letter_margin_box_bottom, margin_box_bottom);
        ViewElement* pseudo = text->parent_view();
        float block_end_margin = pseudo && pseudo->bound ?
            pseudo->boundary()->margin.bottom : 0.0f;
        lycon->block.initial_letter_border_box_bottom = max(
            lycon->block.initial_letter_border_box_bottom,
            margin_box_bottom - block_end_margin);
        lycon->block.initial_letter_origin_line_number = lycon->block.line_number;
        lycon->block.initial_letter_clears_later_start_floats =
            !is_raised_initial_letter && initial_letter.sink > 1.0f;
        if (!is_raised_initial_letter) {
            lycon->block.initial_letter_exclusion_bottom = max(
                lycon->block.initial_letter_exclusion_bottom, rect->y +
                (ceilf(initial_letter.size) + 1.0f) * lycon->block.line_height +
                initial_insets.bottom);
        }
        lycon->block.initial_letter_origin_offset_applied = true;
    }
    // CSS 2.1 10.8.1: Half-leading model for text inline boxes
    float ascender = 0, descender = 0;
    if (lycon->block.line_height_is_normal && lycon->font.font_handle) {
        font_get_normal_lh_split(lycon->font.font_handle, &ascender, &descender);
    } else {
        if (lycon->font.font_handle) {
            font_get_content_area_split(lycon->font.font_handle, &ascender, &descender);
        }
    }
    // CSS Inline 3 §7.5/§7.6: an initial letter occupies inline space but
    // its requested line span must not enlarge its originating line box.
    if (!is_initial_letter && (ascender > 0 || descender > 0)) {
        float half_leading = 0.0f;
        float css_baseline_ascender = ascender;
        if (!lycon->block.line_height_is_normal) {
            float content_height = ascender + descender;
            half_leading = (lycon->block.line_height - content_height) / 2.0f;
            ascender += half_leading;
            descender += half_leading;
            css_baseline_ascender = ascender;
            const FontMetrics* m = lycon->font.font_handle ? font_get_metrics(lycon->font.font_handle) : NULL;
            if (m) {
                float table_ascender = (m->use_typo_metrics &&
                    (m->typo_ascender > 0.0f || m->typo_descender > 0.0f))
                    ? m->typo_ascender
                    : m->hhea_ascender;
                if (table_ascender > 0.0f) {
                    css_baseline_ascender = table_ascender + half_leading;
                }
            }
        }
        float baseline_shift = vertical_align_baseline_shift(
            lycon, lycon->line.vertical_align,
            lycon->line.vertical_align_offset);
        if (baseline_shift != 0.0f) {
            ascender += baseline_shift;
            descender -= baseline_shift;
            css_baseline_ascender += baseline_shift;
        }
        lycon->line.max_text_ascender = max(
            lycon->line.max_text_ascender, ascender);
        lycon->line.max_text_descender = max(
            lycon->line.max_text_descender, descender);
        // CSS 2.1 §10.8.1: vertical-align:top/bottom elements don't participate
        if (lycon->line.vertical_align == CSS_VALUE_TOP) {
            float inline_box_height = ascender + descender;
            lycon->line.max_top_bottom_height = max(lycon->line.max_top_bottom_height, inline_box_height);
            lycon->line.max_top_height = max(lycon->line.max_top_height, inline_box_height);
        } else if (lycon->line.vertical_align == CSS_VALUE_BOTTOM) {
            float inline_box_height = ascender + descender;
            lycon->line.max_top_bottom_height = max(lycon->line.max_top_bottom_height, inline_box_height);
            lycon->line.max_bottom_height = max(lycon->line.max_bottom_height, inline_box_height);
        } else {
            lycon->line.max_desc_before_last_text = lycon->line.max_descender;
            lycon->line.max_ascender = max(lycon->line.max_ascender, ascender);
            lycon->line.max_descender = max(lycon->line.max_descender, descender);
        }
        lycon->line.max_css_baseline_ascender =
            max(lycon->line.max_css_baseline_ascender, css_baseline_ascender);
        // CSS 2.1 §10.8.1: Track if any inline text uses a different font from the
        if (!lycon->line.has_different_inline_font &&
            lycon->font.font_handle != lycon->line.line_start_font.font_handle) {
            lycon->line.has_different_inline_font = true;
        }
        if (lycon->block.line_height_is_normal && lycon->font.font_handle) {
            float normal_lh = font_calc_normal_line_height(lycon->font.font_handle);
            lycon->line.max_normal_line_height = max(lycon->line.max_normal_line_height, normal_lh);
        }
    }

    if (text->rect == rect) {  // first rect
        text->x = rect->x;
        text->y = rect->y;
        text->width = rect->width;
        text->height = rect->height;
    } else {  // following rects after first rect
        include_text_rect_bounds(text, rect);
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
        include_text_rect_bounds(text, rect);
        rect = rect->next;
    }
}

static CssEnum inline_box_decoration_break_value(DomElement* parent) {
    if (!parent || !parent->specified_style) return CSS_VALUE__UNDEF;
    CssDeclaration* declaration = style_tree_get_declaration(
        parent->specified_style, CSS_PROPERTY_BOX_DECORATION_BREAK);
    if (!declaration || !declaration->value ||
        declaration->value->type != CSS_VALUE_TYPE_KEYWORD) {
        return CSS_VALUE__UNDEF;
    }
    return declaration->value->data.keyword;
}

static void clear_slice_inline_start_edge(LayoutContext* lycon, DomNode* text_node) {
    if (!lycon || !text_node || lycon->line.inline_start_edge_pending <= 0.0f ||
        !text_node->parent || !text_node->parent->is_element()) {
        return;
    }
    DomElement* parent = text_node->parent->as_element();
    bool parent_follows_text = parent->prev_sibling && parent->prev_sibling->is_text();
    bool text_follows_break = text_node->prev_sibling &&
        text_node->prev_sibling->is_element() &&
        text_node->prev_sibling->tag() == MARKUP_NAME_BR;
    if (!parent_follows_text && !text_follows_break) {
        return;
    }
    CssEnum decoration_break = inline_box_decoration_break_value(parent);
    if (decoration_break != CSS_VALUE_SLICE &&
        !(text_follows_break && decoration_break == CSS_VALUE__UNDEF)) {
        return;
    }
    lycon->line.advance_x -= lycon->line.inline_start_edge_pending;
    lycon->line.inline_start_edge_pending = 0.0f;
}

static void record_inline_box_decoration_fragment(LayoutContext* lycon, DomNode* text_node) {
    if (!lycon || !text_node || !text_node->parent || !text_node->parent->is_element()) {
        return;
    }
    DomElement* parent = text_node->parent->as_element();
    if (!parent->prev_sibling || !parent->prev_sibling->is_text()) return;
    CssEnum decoration_break = inline_box_decoration_break_value(parent);
    if (decoration_break != CSS_VALUE_SLICE &&
        decoration_break != CSS_VALUE_CLONE) {
        return;
    }
    float line_height = lycon->block.line_height > 0.0f ? lycon->block.line_height : 1.0f;
    float fragment_min_x = lycon->line.left;
    float fragment_max_x = lycon->line.right;
    if (decoration_break == CSS_VALUE_CLONE && parent->bound) {
        fragment_min_x += parent->bound->margin.left;
        fragment_max_x -= parent->bound->margin.right;
    }
    record_inline_fragment_union(text_node, lycon, fragment_min_x, fragment_max_x,
                                 lycon->block.advance_y,
                                 lycon->block.advance_y + line_height);
}

static inline void skip_collapsible_space_sequence(unsigned char** str, bool collapse_newlines) {
    while (is_space(**str) && (collapse_newlines || (**str != '\n' && **str != '\r'))) {
        (*str)++;
    }
}

static bool skip_collapsible_text_edge(LayoutContext* lycon, DomNode* text_node,
                                       unsigned char** str, bool collapse_newlines,
                                       bool clear_view_when_empty, bool* had_leading_space) {
    skip_collapsible_space_sequence(str, collapse_newlines);
    clear_slice_inline_start_edge(lycon, text_node);
    if (!**str) {
        if (clear_view_when_empty) text_node->view_type = RDT_VIEW_NONE;
        return true;
    }
    if (had_leading_space) *had_leading_space = false;
    return false;
}

static bool whitespace_only_text_before_forced_break(DomNode* text_node) {
    if (!text_node || !text_node->next_sibling ||
        !text_node->next_sibling->is_element() ||
        text_node->next_sibling->tag() != MARKUP_NAME_BR) {
        return false;
    }
    const char* text = reinterpret_cast<const char*>(text_node->text_data());
    if (!text || text[0] == '\0') return false;
    for (const char* cursor = text; *cursor; cursor++) {
        if (!is_space(*cursor)) return false;
    }
    return true;
}

static inline bool line_is_at_collapsible_text_edge(LayoutContext* lycon) {
    if (!lycon) return true;
    if (lycon->line.is_line_start) return true;
    return lycon->line.start_view && !lycon->line.last_text_rect &&
        !lycon->line.has_replaced_content &&
        !lycon->line.has_c1_control_text &&
        !lycon->line.has_non_c1_text;
}

static bool output_break_at_last_space(LayoutContext* lycon, DomNode* text_node,
                                       ViewText* text_view, TextRect* rect,
                                       unsigned char** cursor,
                                       const unsigned char* text_start,
                                       const unsigned char* text_end,
                                       bool trim_cjk_spacing,
                                       bool restore_collapsible_trailing_space,
                                       float* soft_hyphen_leading_width) {
    unsigned char* str = lycon->line.last_space + 1;
    if (lycon->line.last_space_hanging_width > 0) {
        lycon->line.hanging_space_width = lycon->line.last_space_hanging_width;
        lycon->line.hanging_space_text_trim = lycon->line.last_space_hanging_text_trim;
    }

    float output_width = lycon->line.last_space_pos;
    int text_len = str - text_start - rect->start_index;
    if (lycon->line.last_space_kind == BRK_SOFT_HYPHEN) {
        float hyphen_width = measure_current_glyph_advance(lycon, '-', trim_cjk_spacing);
        float line_right = lycon->line.has_float_intrusion ?
            lycon->line.effective_right : lycon->line.right;
        if (rect->x + output_width + hyphen_width > line_right + 0.001f
            && lycon->line.last_non_shy_space
            && text_start <= lycon->line.last_non_shy_space
            && lycon->line.last_non_shy_space < str) {
            str = lycon->line.last_non_shy_space + 1;
            output_width = lycon->line.last_non_shy_space_pos;
            text_len = str - text_start - rect->start_index;
            lycon->line.last_space_kind = lycon->line.last_non_shy_space_kind;
            lycon->line.hanging_space_width = lycon->line.last_non_shy_space_hanging_width;
            lycon->line.hanging_space_text_trim = lycon->line.last_non_shy_space_hanging_text_trim;
        } else {
            const unsigned char* continuation = lycon->line.last_space + 1;
            uint32_t continuation_cp = 0;
            int continuation_bytes = 0;
            if (continuation < text_end && *continuation) {
                continuation_bytes = str_utf8_decode((const char*)continuation,
                    (size_t)(text_end - continuation), &continuation_cp);
                if (continuation_bytes <= 0) {
                    continuation_cp = *continuation;
                    continuation_bytes = 1;
                }
                text_len = (int)(continuation + continuation_bytes - text_start - rect->start_index);
                *soft_hyphen_leading_width =
                    measure_current_glyph_advance(lycon, continuation_cp, trim_cjk_spacing);
                str = (unsigned char*)continuation + continuation_bytes;
            } else {
                text_len -= 2;  // U+00AD is 2 bytes in UTF-8 (0xC2 0xAD)
            }
            output_width += hyphen_width;
        }
    }

    output_text(lycon, text_view, rect, text_len, output_width);
    if (lycon->line.last_space_kind == BRK_SOFT_HYPHEN) {
        rect->has_trailing_hyphen = true;
    }
    if (restore_collapsible_trailing_space && lycon->line.last_space_kind == BRK_SPACE) {
        lycon->line.trailing_space_width =
            layout_measure_space_advance(lycon, lycon->font.font_handle, lycon->font.style)
            + lycon->font.style->word_spacing
            + text_letter_spacing(lycon->font.style, 0x20, true);
    }
    line_break(lycon);

    *cursor = str;
    if (*str) return true;
    if (*soft_hyphen_leading_width > 0.0f) {
        record_soft_hyphen_inline_fragment(
            text_node, lycon, *soft_hyphen_leading_width, rect->height);
        lycon->line.advance_x += *soft_hyphen_leading_width;
        *soft_hyphen_leading_width = 0.0f;
    }
    return false;
}

static void discard_uncommitted_text_rect(ViewText* text, TextRect* rect) {
    if (text->rect == rect) {
        text->rect = nullptr;
        return;
    }
    TextRect* prev = text->rect;
    while (prev && prev->next != rect) prev = prev->next;
    if (prev) prev->next = nullptr;
}

static bool clear_initial_letter_continuation(LayoutContext* lycon) {
    if (!lycon || lycon->block.initial_letter_continuation_cleared) return false;

    BlockContext* bfc = block_context_find_bfc(&lycon->block);
    if (!bfc || !bfc->initial_letters) return false;

    float origin_y_bfc = lycon->block.bfc_offset_y + lycon->block.advance_y;
    float clear_y_bfc = origin_y_bfc;
    for (InitialLetterBox* box = bfc->initial_letters; box; box = box->next) {
        if (box->direction == lycon->block.direction &&
            origin_y_bfc < box->margin_box_bottom) {
            clear_y_bfc = max(clear_y_bfc, box->margin_box_bottom);
        }
    }
    if (clear_y_bfc <= origin_y_bfc) return false;
    // CSS Inline 3 §7.9.2 clears a following initial at the previous initial's
    lycon->block.advance_y = clear_y_bfc - lycon->block.bfc_offset_y;
    lycon->block.initial_letter_continuation_cleared = true;
    return true;
}

static void mark_line_non_space(Linebox* line) {
    line->is_line_start = false;
    line->has_space = false;
    line->trailing_space_width = 0;
    line->hanging_space_width = 0;
    line->hanging_space_text_trim = 0;
}

void layout_text(LayoutContext* lycon, DomNode *text_node) {
    auto t_start = high_resolution_clock::now();


    unsigned char* next_ch;  ViewText* text_view = null;
    unsigned char* text_start = text_node->text_data();
    if (!text_start) return;  // null check for text data
    unsigned char* str = text_start;
    unsigned char* text_end = text_start + strlen((const char*)text_start);
    // CSS Inline 3 §2.1: Zero-length text nodes generate no inline boxes and
    if (str == text_end) {
        text_node->view_type = RDT_VIEW_NONE;
        return;
    }

    if (text_node->view_type == RDT_VIEW_TEXT) {
        ViewText* existing_view = lam::view_require<RDT_VIEW_TEXT>(text_node);
        if (existing_view->rect) {
            lycon->doc->view_tree->recycle_text_rects(existing_view->rect);
            existing_view->rect = nullptr;
        }
    }

    CssEnum white_space = get_white_space_value(text_node);  // todo: white-space should be put in BlockContext
    bool collapse_spaces = ws_collapse_spaces(white_space);
    bool collapse_newlines = ws_collapse_newlines(white_space);
    // CSS Text 3 §4.1.1: trim a preceding inline's collapsible trailing space
    // before a preserved segment break forces the next line.
    if (!collapse_newlines && (*text_start == '\n' || *text_start == '\r') &&
        lycon->line.trailing_space_width > 0.0f && lycon->line.last_text_rect) {
        line_consume_trailing_collapsible_space(lycon, true, true);
    }
    // CSS Sizing 3: In max-content mode, never wrap — measure full unwrapped width
    bool wrap_lines = ws_wrap_lines(white_space) &&
        !lycon->available_space.width.is_max_content();

    CssEnum word_break = get_inherited_text_enum(
        lycon, &BlockProp::word_break, CSS_VALUE_NORMAL);
    CssEnum line_break_val = get_inherited_text_enum(
        lycon, &BlockProp::line_break, CSS_VALUE_AUTO);
    // line-break: anywhere allows break at any typographic letter unit (CSS Text 3 §5.2)
    bool break_all = (word_break == CSS_VALUE_BREAK_ALL || line_break_val == CSS_VALUE_ANYWHERE);
    bool keep_all = (word_break == CSS_VALUE_KEEP_ALL && line_break_val != CSS_VALUE_ANYWHERE);
    // CSS Text 3 §5.2: word-break: break-word behaves as overflow-wrap: anywhere
    CssEnum overflow_wrap = get_inherited_text_enum(
        lycon, &BlockProp::overflow_wrap, CSS_VALUE_NORMAL);
    bool break_word = (overflow_wrap == CSS_VALUE_BREAK_WORD || overflow_wrap == CSS_VALUE_ANYWHERE
                       || line_break_val == CSS_VALUE_ANYWHERE
                       || word_break == CSS_VALUE_BREAK_WORD);

    CssEnum text_transform = get_text_transform_from_node(text_node);
    bool trim_cjk_spacing = should_apply_text_spacing_trim(lycon, text_node);
    bool is_word_start = true;  // Track word boundaries for capitalize
    int layout_text_iterations = 0;  // guard against infinite goto loops
    float soft_hyphen_leading_width = 0.0f;
    // CSS Text 3 §6.2: Resolve lang for CJ class behavior.
    const char* lang = resolve_lang(text_node);
    bool cj_is_non_starter = (line_break_val == CSS_VALUE_STRICT)
        || (line_break_val != CSS_VALUE_LOOSE && is_lang_japanese(lang));
    // CSS Text 3 §4.1.2: Track last non-whitespace codepoint for segment break
    uint32_t last_processed_cp = 0;
    // CSS Text 3 §5.2: Track whether the text had a leading space before collapsing.
    bool had_leading_space = is_space(*str) && (collapse_newlines || (*str != '\n' && *str != '\r'));
    bool had_explicit_leading_space =
        is_space(*str) && *str != '\n' && *str != '\r';

    bool at_collapsible_text_edge = line_is_at_collapsible_text_edge(lycon);
    if (collapse_spaces && (at_collapsible_text_edge || lycon->line.has_space) && is_space(*str)) {
        skip_collapsible_space_sequence(&str, collapse_newlines);
        if (at_collapsible_text_edge) {
            clear_slice_inline_start_edge(lycon, text_node);
        }
        if (!*str) {
            text_node->view_type = RDT_VIEW_NONE;
            // CSS Text 3 §5: Even though this whitespace was fully collapsed, it
            if (wrap_lines && !at_collapsible_text_edge) {
                lycon->line.wrap_opportunity_before_nowrap = true;
            }
            return;
        }
    }
    LAYOUT_TEXT:
    // CSS Text 3 §4.1/§4.2: collapsible spaces at the start of a line
    at_collapsible_text_edge = line_is_at_collapsible_text_edge(lycon);
    bool follows_forced_break = text_node->parent && text_node->parent->is_element() &&
        text_node->prev_sibling && text_node->prev_sibling->is_element() &&
        text_node->prev_sibling->tag() == MARKUP_NAME_BR;
    if (lycon->line.is_line_start || follows_forced_break) {
        clear_slice_inline_start_edge(lycon, text_node);
    }
    if (collapse_spaces && at_collapsible_text_edge && is_space(*str)) {
        if (skip_collapsible_text_edge(lycon, text_node, &str, collapse_newlines,
                                        !text_view, &had_leading_space)) return;
    }
    if (++layout_text_iterations > 500) {
        log_error("layout_text: exceeded 500 iterations, aborting text layout");
        return;
    }
    // CSS Text 3 §5.2: Only wrap at allowed break points (soft wrap opportunities).
    // collapsed indentation immediately before <br> must not create an empty
    {
        float line_right = lycon->line.has_float_intrusion ?
                           lycon->line.effective_right : lycon->line.right;
        uint32_t first_codepoint = peek_codepoint(str);
        bool cjk_boundary_wrap = wrap_lines && !lycon->line.is_line_start &&
            lycon->line.advance_x >= line_right - 0.001f &&
            has_id_line_break_class(lycon->line.prev_codepoint) &&
            has_id_line_break_class(first_codepoint) && !keep_all;
        bool whitespace_before_forced_break = collapse_spaces &&
            whitespace_only_text_before_forced_break(text_node);
        if (wrap_lines && !whitespace_before_forced_break &&
            (lycon->line.advance_x > line_right || cjk_boundary_wrap) &&
            !lycon->line.is_line_start
            && (lycon->line.last_space || lycon->line.wrap_opportunity_before_nowrap
                || (had_leading_space && !whitespace_before_forced_break) || break_all ||
                    cjk_boundary_wrap)) {
            line_break(lycon);
            if (collapse_spaces && is_space(*str)) {
                if (skip_collapsible_text_edge(lycon, text_node, &str, collapse_newlines,
                                                !text_view, &had_leading_space)) return;
            }
        }
    }
    // CSS Text 3 §5.2: Before placing any characters, check whether the first
    if (collapse_spaces && wrap_lines && !lycon->line.is_line_start &&
        !break_all && had_leading_space) {
        float line_right = lycon->line.has_float_intrusion ?
                           lycon->line.effective_right : lycon->line.right;
        float remaining = line_right - lycon->line.advance_x;
        float first_word_w = measure_first_word_width(
            lycon, str, text_end, text_transform, collapse_spaces);
        float leading_space_w = 0.0f;
        bool min_content_line = is_min_content_mode(lycon, text_node);
        if (collapse_spaces && min_content_line) {
            leading_space_w = layout_measure_space_advance(
                lycon, lycon->font.font_handle, lycon->font.style);
            if (lycon->font.style) {
                leading_space_w += lycon->font.style->word_spacing +
                    text_letter_spacing(lycon->font.style, 0x20, true);
            }
        }
        // the candidate line width; otherwise overflow creates a false
        // CSS Text 3 §5.2: a collapsed leading space still permits the next word
        // to wrap when the preceding line is exactly full.
        // CSS Text 3 §5.2: a literal collapsible separator creates the wrap
        // opportunity at an exactly full line; a leading segment break alone does not.
        bool exact_full_collapsed_space = had_explicit_leading_space &&
            remaining >= -0.001f;
        bool first_word_does_not_fit = first_word_w + leading_space_w > remaining &&
            (remaining > 0.0f || lycon->line.wrap_opportunity_before_nowrap ||
             exact_full_collapsed_space);
        if (first_word_w > 0 && (first_word_does_not_fit ||
                                 (min_content_line && remaining <= 0.0f))) {
            record_inline_box_decoration_fragment(lycon, text_node);
            line_break(lycon);
            if (collapse_spaces && is_space(*str)) {
                if (skip_collapsible_text_edge(lycon, text_node, &str, collapse_newlines,
                                                !text_view, &had_leading_space)) return;
            }
        }
    }
    if (!text_view) {
        text_view = lam::view_require<RDT_VIEW_TEXT>(set_view(lycon, RDT_VIEW_TEXT, text_node));
        text_view->font = lycon->font.style;
    }

    if (lycon->font.style && lycon->font.style->font_size <= 0.0f) {
        TextRect* rect = lycon->doc->view_tree->alloc_text_rect();
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
        rect->line_number = lycon->block.line_number;
        return;
    }

    TextRect* rect = lycon->doc->view_tree->alloc_text_rect();
    if (!text_view->rect) {
        text_view->rect = rect;
    } else {
        TextRect* last_rect = text_view->rect;;
        while (last_rect && last_rect->next) { last_rect = last_rect->next; }
        last_rect->next = rect;
    }
    rect->start_index = str - text_start;
    if (soft_hyphen_leading_width > 0.0f) {
        rect->width = soft_hyphen_leading_width;
        soft_hyphen_leading_width = 0.0f;
    }
    float font_height = font_get_cell_height(lycon->font.font_handle);
    if (font_height <= 0.0f) font_height = 16.0f;
    rect->x = lycon->line.advance_x;
    rect->height = font_get_cell_height(lycon->font.font_handle);
    InitialLetterInfo rect_initial_letter = {};
    bool is_rect_initial = layout_get_text_initial_letter_info(
        text_node, &rect_initial_letter);
    bool is_raised_rect_initial = is_rect_initial && rect_initial_letter.raised;
    if (is_rect_initial && clear_initial_letter_continuation(lycon)) {
        discard_uncommitted_text_rect(text_view, rect);
        line_reset(lycon);
        goto LAYOUT_TEXT;
    }
    if (is_rect_initial && text_view->font &&
        text_view->font->initial_letter_computed_font_size > 0.0f &&
        text_view->font->font_size > 0.0f) {
        // the CSS Inline 3 used size calculated from the containing line.
        rect->height *= text_view->font->initial_letter_computed_font_size /
            text_view->font->font_size;
    }

    if (is_rect_initial) {
        rect->y = lycon->block.advance_y + lycon->block.lead_y;
        if (is_raised_rect_initial &&
            initial_letter_block_trims_start_edge(text_node, lycon)) {
            rect->y -= max(0.0f, rect_initial_letter.size - 1.0f) *
                lycon->block.line_height;
        }
    }
    else if (lycon->line.vertical_align == CSS_VALUE_MIDDLE) {
        rect->y = lycon->block.advance_y + (lycon->block.line_height - font_height) / 2;
    }
    else if (lycon->line.vertical_align == CSS_VALUE_BOTTOM) {
        rect->y = lycon->block.advance_y + lycon->block.line_height - font_height;
    }
    else if (lycon->line.vertical_align == CSS_VALUE_TOP) {
        rect->y = lycon->block.advance_y;
    }
    else { // baseline - use half-leading model
        // Allow negative half-leading only when line-height is explicitly less than font height
        if (lycon->block.line_height < font_height) {
            float half_leading = (lycon->block.line_height - font_height) / 2;
            rect->y = lycon->block.advance_y + half_leading;
        } else {
            rect->y = lycon->block.advance_y + lycon->block.lead_y;
        }
    }
#ifdef RADIANT_TRACE_TEXT_LAYOUT
    // Text-run tracing is opt-in because large documents otherwise emit one
    // debug record per run and spend most of layout time writing logs.
#endif

    bool zwj_preceded = false;  // UAX #14: ZWJ suppresses break between adjacent characters
    bool prev_is_zwj_base = false;  // track if previous char is a ZWJ composition base
    do {
        float wd;
        uint32_t codepoint = *str;
        bool shaped_latin_run = false;
        uint32_t shaped_latin_first_codepoint = 0;

        if (!collapse_newlines && (*str == '\n' || *str == '\r')) {
            // CSS 2.2: When preserving newlines with collapsing spaces (pre-line),
            if (collapse_spaces && str > text_start + rect->start_index) {
                const unsigned char* check = str - 1;
                float trailing_width = 0;
                while (check >= text_start + rect->start_index && is_space(*check)) {
                    trailing_width += layout_measure_space_advance(
                        lycon, lycon->font.font_handle, lycon->font.style);
                    check--;
                }
                if (trailing_width > 0) {
                    rect->width -= trailing_width;
                }
                lycon->line.trailing_space_width = 0;
            }
            int break_length = 1;
            if (*str == '\r' && *(str + 1) == '\n') {
                break_length = 2;
            }
            // CSS Text preserved segment breaks force a line break but remain part
            output_text(lycon, text_view, rect,
                str - text_start - rect->start_index + break_length, rect->width);
            if (break_length == 2) {
                str += 2;
            } else {
                str++;
            }
            // CSS Text 3 §7.2: text-align-last applies to lines immediately before
            lycon->line.is_last_line = true;
            line_break(lycon);
            lycon->line.is_last_line = false;
            if (*str) {
                // CSS 2.1 §16.6.1: When collapsing spaces (pre-line), skip leading
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

        {
            int shaped_bytes = 0;
            float shaped_width = 0.0f;
            uint32_t shaped_first_cp = 0;
            uint32_t shaped_last_cp = 0;
            if (measure_shaped_simple_latin_run(lycon, str, text_end, text_transform,
                                                trim_cjk_spacing, break_all, break_word,
                                                &shaped_bytes, &shaped_width,
                                                &shaped_first_cp, &shaped_last_cp)) {
                wd = shaped_width;
                next_ch = str + shaped_bytes;
                codepoint = shaped_last_cp;
                shaped_latin_run = true;
                shaped_latin_first_codepoint = shaped_first_cp;
                is_word_start = false;
                lycon->line.has_non_c1_text = true;
                lycon->line.trailing_letter_spacing = 0.0f;
            }
        }

        if (shaped_latin_run) {
        } else if (is_space(codepoint)) {
            wd = layout_measure_space_advance(lycon, lycon->font.font_handle, lycon->font.style);
            if (codepoint == '\t' && !collapse_spaces) {
                // CSS Text 3 §4.2: tab-size <number> — tab stops occur at points
                // not the inline element's font (CSS Text 3 §4.2: "the advance
                int ts = 8;
                ViewElement* ancestor = lycon->view->parent_view();
                while (ancestor) {
                    if (ancestor->is_element()) {
                        DomElement* elem = lam::dom_require_element(ancestor);
                        if (elem->blk && elem->block_mut()->tab_size >= 0) {
                            ts = elem->block()->tab_size;
                            break;
                        }
                    }
                    ancestor = ancestor->parent_view();
                }
                FontProp* block_font = lycon->block.block_container_font;
                if (!block_font) block_font = lycon->font.style;
                if (ts == 0) {
                    wd = 0;
                } else {
                    float raw_space_advance = layout_measure_space_advance(
                        lycon, block_font->font_handle ? block_font->font_handle : lycon->font.font_handle, block_font);
                    float space_advance = raw_space_advance
                        + block_font->word_spacing
                        + block_font->letter_spacing;
                    float tab_period = space_advance * ts;
                    float current_x = rect->x + rect->width;
                    float current_offset = current_x - lycon->line.left;
                    // CSS Text 3 §4.2: if the distance to the next tab stop is less
                    float half_ch = raw_space_advance * 0.5f;
                    float next_tab_offset = tab_period *
                        ceilf((current_offset + half_ch) / tab_period);
                    wd = next_tab_offset - current_offset;
                }
            } else {
                wd += lycon->font.style->word_spacing;
                wd += text_letter_spacing(lycon->font.style, codepoint, collapse_spaces);
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

            uint32_t tt_out[3];
            int tt_count = apply_text_transform_full(codepoint, text_transform, is_word_start, tt_out);
            codepoint = tt_out[0];
            bool is_small_caps_lower = false;
            if (has_small_caps(lycon)) {
                uint32_t original = codepoint;
                codepoint = apply_small_caps(codepoint);
                is_small_caps_lower = (codepoint != original);
            }
            is_word_start = false;  // No longer at word start

            float unicode_space_em = get_unicode_space_width_em(codepoint);
            if (unicode_space_em < 0.0f) {
                if (codepoint == 0x200B && wrap_lines) {
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
                if (codepoint == 0x00AD && wrap_lines) {
                    str = next_ch;
                    if (lycon->line.last_space && lycon->line.last_space_kind != BRK_SOFT_HYPHEN) {
                        lycon->line.last_non_shy_space = lycon->line.last_space;
                        lycon->line.last_non_shy_space_pos = lycon->line.last_space_pos;
                        lycon->line.last_non_shy_space_kind = lycon->line.last_space_kind;
                        lycon->line.last_non_shy_space_hanging_width = lycon->line.last_space_hanging_width;
                        lycon->line.last_non_shy_space_hanging_text_trim = lycon->line.last_space_hanging_text_trim;
                    }
                    lycon->line.last_space = str - 1;
                    lycon->line.last_space_pos = rect->width;
                    lycon->line.last_space_kind = BRK_SOFT_HYPHEN;
                    lycon->line.is_line_start = false;
                    lycon->line.has_space = false;
                    lycon->line.trailing_space_width = 0;
                    continue;
                }
                str = next_ch;
                lycon->line.is_line_start = false;
                lycon->line.has_space = false;
                if (codepoint == 0x200D && prev_is_zwj_base) zwj_preceded = true;
                continue;  // Skip to next character without adding width
            } else if (unicode_space_em > 0.0f) {
                float sc_scale = is_small_caps_lower ?
                    font_get_small_caps_scale(lycon->font.font_handle) : 1.0f;
                wd = unicode_space_em * layout_font_em_size(lycon) * sc_scale;
            } else {
                FontStyleDesc _sd = font_style_desc_from_prop(lycon->font.style);
                bool emoji_presentation = false;
                if (next_ch) {
                    uint32_t peek_cp;
                    int peek_bytes = str_utf8_decode((const char*)next_ch, (size_t)(text_end - next_ch), &peek_cp);
                    if (peek_bytes > 0 && peek_cp == 0xFE0F) {
                        emoji_presentation = true;
                    }
                }
                LoadedGlyph* glyph = emoji_presentation
                    ? font_load_glyph_emoji(lycon->font.font_handle, &_sd, codepoint, false)
                    : font_load_glyph(lycon->font.font_handle, &_sd, codepoint, false);
                float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0) ? lycon->ui_context->pixel_ratio : 1.0f;
                wd = glyph ? normalize_east_asian_advance(
                    lycon, codepoint, glyph->advance_x / pixel_ratio)
                    : layout_font_em_size(lycon);
                if (glyph && trim_cjk_spacing) {
                    wd += font_get_halt_adjustment(lycon->font.font_handle, codepoint) * 0.5f;
                }
                if (zwj_preceded && utf_is_emoji_for_zwj(codepoint)) {
                    wd = 0;
                }
                if (is_small_caps_lower) {
                    wd *= font_get_small_caps_scale(lycon->font.font_handle);
                }
                // fallback glyph metrics affect advance/painting, but must not
                if (glyph && glyph->font_ascender > 0 &&
                    lycon->block.line_height_is_normal &&
                    !control_fallback_keeps_primary_line_metrics(codepoint)) {
                    float fb_asc, fb_desc;
                    fb_asc = glyph->font_normal_ascender;
                    fb_desc = glyph->font_normal_descender;
                    float fb_normal_line_height = glyph->font_normal_line_height;
                    normalize_c1_control_fallback_metrics(codepoint, lycon->font.style, &fb_asc, &fb_desc,
                                                          &fb_normal_line_height);
                    // CSS 2.1 §10.8.1: vertical-align:top/bottom elements don't participate
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
                    if (lycon->block.line_height_is_normal && fb_normal_line_height > 0) {
                        lycon->line.max_normal_line_height = max(lycon->line.max_normal_line_height,
                                                                  fb_normal_line_height);
                    }
                }
                float c1_normal_line_height = c1_control_normal_line_height(codepoint, lycon->font.style);
                if (lycon->block.line_height_is_normal && c1_normal_line_height > 0) {
                    lycon->line.has_c1_control_text = true;
                    lycon->line.c1_control_line_height = max(lycon->line.c1_control_line_height,
                                                              c1_normal_line_height);
                    float c1_asc = 0.0f, c1_desc = 0.0f;
                    if (lycon->font.font_handle) {
                        font_get_normal_lh_split(lycon->font.font_handle, &c1_asc, &c1_desc);
                    }
                    float c1_height = c1_asc + c1_desc;
                    if (c1_height > 0) {
                        float c1_scale = c1_normal_line_height / c1_height;
                        c1_asc *= c1_scale;
                        c1_desc *= c1_scale;
                    } else {
                        c1_asc = c1_normal_line_height * 0.8f;
                        c1_desc = c1_normal_line_height - c1_asc;
                    }
                    lycon->line.max_ascender = max(lycon->line.max_ascender, c1_asc);
                    lycon->line.max_descender = max(lycon->line.max_descender, c1_desc);
                    lycon->line.max_normal_line_height = max(lycon->line.max_normal_line_height,
                                                              c1_normal_line_height);
                } else {
                    lycon->line.has_non_c1_text = true;
                }
                if (utf_is_cjk(codepoint)) {
                    lycon->line.has_cjk_text = true;
                }
            }
            // CSS 2.1 §16.4: letter-spacing is added after every character
            wd += text_letter_spacing(lycon->font.style, codepoint, collapse_spaces);
            // CSS 2.1 §16.4: word-spacing affects each space (U+0020) and
            // branch above. U+00A0 must be handled here since it's not collapsible
            if (codepoint == 0x00A0) {
                wd += lycon->font.style->word_spacing;
                is_word_start = true;
            }
            // CSS Text 3 §8: Track trailing letter-spacing for trimming at line ends.
            // letter-spacing must not be applied at the start or end of a line.
            lycon->line.trailing_letter_spacing =
                text_letter_spacing(lycon->font.style, codepoint, collapse_spaces);

            if (tt_count > 1) {
                FontStyleDesc _sd_extra = font_style_desc_from_prop(lycon->font.style);
                float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0) ? lycon->ui_context->pixel_ratio : 1.0f;
                for (int ti = 1; ti < tt_count; ti++) {
                    uint32_t extra_cp = tt_out[ti];
                    if (extra_cp == 0) continue;
                    if (text_codepoint_has_zero_advance(extra_cp)) continue;
                    LoadedGlyph* extra_glyph = font_load_glyph(lycon->font.font_handle, &_sd_extra, extra_cp, false);
                    float extra_wd = extra_glyph ? (extra_glyph->advance_x / pixel_ratio) : 0;
                    if (extra_glyph && trim_cjk_spacing) {
                        extra_wd += font_get_halt_adjustment(lycon->font.font_handle, extra_cp) * 0.5f;
                    }
                    if (is_small_caps_lower) {
                        extra_wd *= font_get_small_caps_scale(lycon->font.font_handle);
                    }
                    extra_wd += text_letter_spacing(
                        lycon->font.style, extra_cp, collapse_spaces);
                    wd += extra_wd;
                }
            }
        }
        if (lycon->font.style->has_kerning) {
            // fallback or styled inline cannot form a pair in the next font.
            if (lycon->line.prev_codepoint &&
                lycon->line.prev_kerning_font_handle == lycon->font.font_handle) {
                uint32_t kerning_codepoint = shaped_latin_run ? shaped_latin_first_codepoint : codepoint;
                float kerning_css = text_kerning_adjustment(
                    lycon, lycon->line.prev_codepoint, kerning_codepoint);
                if (kerning_css != 0.0f) {
                    if (str == text_start + rect->start_index) {
                        rect->x += kerning_css;
                    }
                    else {
                        rect->width += kerning_css;
                    }
                }
            }
            lycon->line.prev_codepoint = codepoint;
            lycon->line.prev_kerning_font_handle = lycon->font.font_handle;
        }
#ifdef RADIANT_TRACE_TEXT_LAYOUT
        // debugging; keeping it always-on makes long pages non-interactive.
#endif
        prev_is_zwj_base = utf_is_zwj_composition_base(codepoint);
        // CSS Text 3 §4.1.2: track last non-whitespace codepoint for segment break transformation
        if (!is_space(codepoint)) last_processed_cp = codepoint;
        if (codepoint == 0x2014 && wrap_lines && !lycon->line.is_line_start) {
            lycon->line.last_space = (uint8_t*)str - 1;       // byte before the dash
            lycon->line.last_space_pos = rect->width;          // width before the dash
            lycon->line.last_space_kind = BRK_HYPHEN;
        }
        rect->width += wd;
        // CSS Text 3 §4.1.3: Pre-wrap trailing spaces "hang" and don't count for
        if (!is_space(*str) && codepoint != 0x3000 && lycon->line.hanging_space_width > 0) {
            lycon->line.hanging_space_width = 0;
            lycon->line.hanging_space_text_trim = 0;
        }
        float line_right = lycon->line.has_float_intrusion ?
                           lycon->line.effective_right : lycon->line.right;
        float terminal_trim = line_terminal_letter_spacing_trim(
            lycon->line.trailing_letter_spacing);
        if (wrap_lines && rect->x + rect->width - terminal_trim > line_right + 0.001f) { // line filled up and wrapping enabled
            if (codepoint == 0x3000 && white_space != CSS_VALUE_BREAK_SPACES) {
                // CSS Text 3 §4.1.3: U+3000 IDEOGRAPHIC SPACE hangs at end of line.
            }
            else if (is_space(*str) && !collapse_spaces && white_space != CSS_VALUE_BREAK_SPACES) {
                // CSS Text 3 §4.1.3: In pre-wrap, trailing spaces hang at end of line.
            }
            else if (is_space(*str) && lycon->line.hanging_space_width > 0) {
            }
            // CSS Text 3 §3 + §5.2: For break-spaces with break_all/line-break:anywhere,
            else if ((is_space(*str) || codepoint == 0x3000) && white_space == CSS_VALUE_BREAK_SPACES
                     && break_all && lycon->line.last_space
                     && text_start <= lycon->line.last_space && lycon->line.last_space < str) {
                rect->width -= wd;  // undo the space width
                str = lycon->line.last_space + 1;
                float output_width = lycon->line.last_space_pos;
                output_text(lycon, text_view, rect, str - text_start - rect->start_index, output_width);
                line_break(lycon);  goto LAYOUT_TEXT;
            }
            else if (is_space(*str)) { // break at the current space (collapsible or break-spaces)
                if (collapse_spaces && white_space != CSS_VALUE_BREAK_SPACES &&
                    rect->width <= wd + 0.01f) {
                    // CSS Text §4: collapsible whitespace at the line edge is removed.
                    do { str++; } while (is_space(*str) && (collapse_newlines || (*str != '\n' && *str != '\r')));
                    rect->width = 0.0f;
                    rect->length = (int)(str - text_start - rect->start_index);
                    lycon->line.trailing_space_width = 0.0f;
                    lycon->line.has_space = true;
                    if (wrap_lines && !lycon->line.is_line_start) {
                        lycon->line.wrap_opportunity_before_nowrap = true;
                    }
                    if (*str) {
                        record_inline_box_decoration_fragment(lycon, text_node);
                        line_break(lycon);
                        clear_slice_inline_start_edge(lycon, text_node);
                        goto LAYOUT_TEXT;
                    }
                    return;
                }
                // CSS Text 3 §3: For break-spaces, "a line breaking opportunity exists
                if (white_space == CSS_VALUE_BREAK_SPACES && rect->width - wd > 0.01f) {
                    if (lycon->line.last_space
                        && text_start <= lycon->line.last_space
                        && lycon->line.last_space < str) {
                        str = lycon->line.last_space + 1;
                        float output_width = lycon->line.last_space_pos;
                        output_text(lycon, text_view, rect, str - text_start - rect->start_index, output_width);
                        line_break(lycon);  goto LAYOUT_TEXT;
                    } else if (break_word || break_all) {
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
                        if (*str) { goto LAYOUT_TEXT; }
                        else return;
                    }
                }
                if (collapse_spaces) {
                    do { str++; } while (is_space(*str) && (collapse_newlines || (*str != '\n' && *str != '\r')));
                } else {
                    str++;  // only skip the current space in break-spaces mode
                }
                // CSS Text 3 §4.1.3: For break-spaces, preserved spaces take up space
                if (white_space != CSS_VALUE_BREAK_SPACES) {
                    rect->width -= wd;  // minus away space width at line break
                }
                lycon->line.trailing_space_width = 0;  // already trimmed, don't double-subtract
                output_text(lycon, text_view, rect, str - text_start - rect->start_index, rect->width);
                line_break(lycon);
                if (*str) { goto LAYOUT_TEXT; }
                else return;
            }
            // CSS Text 3 §4.1.3: For pre-wrap, preserved spaces at end of line "hang"
            else if (lycon->line.hanging_space_width > 0
                     && rect->x + rect->width - lycon->line.hanging_space_width <= line_right) {
                log_debug("pre-wrap hanging: content fits without %dpx hanging spaces",
                    (int)lycon->line.hanging_space_width); // INT_CAST_OK: pixel count for log
            }
            else if (lycon->line.last_space) { // break at the last space
                if (text_start <= lycon->line.last_space && lycon->line.last_space < str) {
                    // CSS 2.1 §16.6.1: When wrapping at a collapsible space, the
                    // trailing space must be trimmed from the line box width.
                    // CSS Text 3 §8: Include word-spacing and letter-spacing that were
                    if (output_break_at_last_space(
                            lycon, text_node, text_view, rect, &str, text_start, text_end,
                            trim_cjk_spacing, collapse_spaces, &soft_hyphen_leading_width)) {
                        goto LAYOUT_TEXT;
                    }
                    return;
                }
                else { // last_space outside the text
                    // CSS Text 3 §5.2: overflow-wrap: break-word with last_space in
                    if (break_word && !lycon->line.is_line_start) {
                        float full_line_width = lycon->line.right - lycon->line.left;
                        if (rect->width - wd > full_line_width) {
                            rect->width -= wd;  // undo the char that overflowed
                            int text_len = str - text_start - rect->start_index;
                            if (text_len > 0) {
                                output_text(lycon, text_view, rect, text_len, rect->width);
                            } else {
                                discard_uncommitted_text_rect(text_view, rect);
                            }
                            line_break(lycon);
                            goto LAYOUT_TEXT;
                        }
                        str = text_start + rect->start_index;  // rewind to text start
                        discard_uncommitted_text_rect(text_view, rect);
                        line_break(lycon);
                        goto LAYOUT_TEXT;
                    }
                    line_break(lycon);
                    rect->y = lycon->block.advance_y;
                    rect->x = lycon->line.advance_x;
                }
            }
            // CSS 2.1 §9.5: "If a shortened line box is too small to contain any content,
            else if (lycon->line.has_float_intrusion &&
                     (lycon->line.effective_right - lycon->line.effective_left) <
                     (lycon->line.right - lycon->line.left) &&
                     rect->width <= (lycon->line.right - lycon->line.left) + 0.5f) {
                float required_width = rect->width;
                rect->width -= wd;
                str = text_start + rect->start_index;
                discard_uncommitted_text_rect(text_view, rect);
                if (lycon->line.is_line_start) {
                    BlockContext* bfc = block_context_find_bfc(&lycon->block);
                    float query_height = lycon->block.line_height > 0.0f ? lycon->block.line_height : 16.0f;
                    float current_y_bfc = lycon->block.advance_y + lycon->block.bfc_offset_y;
                    float new_y_bfc = bfc ?
                        block_context_find_y_for_width(bfc, required_width, current_y_bfc, query_height) :
                        current_y_bfc;
                    float new_y = new_y_bfc - lycon->block.bfc_offset_y;
                    if (new_y > lycon->block.advance_y + 0.01f) {
                        lycon->block.advance_y = new_y;
                        line_reset(lycon);
                    } else {
                        line_break(lycon);
                    }
                } else {
                    line_break(lycon);
                }
                goto LAYOUT_TEXT;
            }
            else if (break_word && !lycon->line.is_line_start) {
                rect->width -= wd;  // undo the char that overflowed
                int text_len = str - text_start - rect->start_index;
                if (text_len > 0) {
                    output_text(lycon, text_view, rect, text_len, rect->width);
                } else {
                    discard_uncommitted_text_rect(text_view, rect);
                }
                line_break(lycon);
                goto LAYOUT_TEXT;
            }
            // else cannot break and no float intrusion, continue the flow in current line
        }
        // CSS Text 3 §3/§5: white-space: nowrap prevents ALL line breaks within this
        else if (!wrap_lines &&
                 rect->x + rect->width +
                     ((text_node->next_sibling == nullptr && text_view->parent_view() &&
                       text_view->parent_view()->view_type == RDT_VIEW_INLINE)
                          ? layout_inline_end_edge(lam::view_require<RDT_VIEW_INLINE>(
                                text_view->parent_view())) : 0.0f) > line_right
                 && lycon->line.wrap_opportunity_before_nowrap
                 && !lycon->line.is_line_start
                 && text_transform != CSS_VALUE_CAPITALIZE) {
            str = text_start + rect->start_index;
            discard_uncommitted_text_rect(text_view, rect);
            line_break(lycon);
            goto LAYOUT_TEXT;
        }
        if (is_space(*str)) {
            if (collapse_spaces) {
                // CSS Text 3 §4.1.2: Track whether whitespace contains a segment break (newline)
                bool has_segment_break = (codepoint == '\n' || codepoint == '\r');
                do {
                    str++;
                    if ((*str == '\n' || *str == '\r') && collapse_newlines) has_segment_break = true;
                } while (is_space(*str) && (collapse_newlines || (*str != '\n' && *str != '\r')));
                // CSS Text 3 §4.1.2: Segment Break Transformation Rules
                if (has_segment_break && collapse_newlines) {
                    bool remove_break = false;
                    bool prev_is_zwsp = (last_processed_cp == 0x200B);
                    bool next_is_zwsp = false;
                    uint32_t next_cp = *str ? peek_codepoint(str) : 0;
                    if (next_cp == 0x200B) {
                        next_is_zwsp = true;
                    }
                    if (prev_is_zwsp || next_is_zwsp) {
                        remove_break = true;
                    }
                    // CSS Text 3 §4.1.2: segment breaks between two East Asian F/W
                    if (!remove_break && last_processed_cp && next_cp
                        && utf8proc_charwidth(last_processed_cp) == 2 &&
                           !utf_is_hangul(last_processed_cp)
                        && utf8proc_charwidth(next_cp) == 2 && !utf_is_hangul(next_cp)) {
                        remove_break = true;
                    }
                    if (remove_break) {
                        rect->width -= wd;  // undo the space width
                        continue;  // skip break opportunity recording
                    }
                }
            } else {
                str++;
                // stale normal-space state must not collapse a later normal space.
                lycon->line.is_line_start = false;
                lycon->line.has_space = false;
                lycon->line.trailing_space_width = 0;
            }
            // CSS Text 3 §5.2: non-wrapping white-space modes must not leak a space
            // break opportunity into a later inline text node.
            if (wrap_lines) {
                lycon->line.last_space = str - 1;
                lycon->line.last_space_pos = rect->width;
                lycon->line.last_space_kind = BRK_SPACE;
            }
            // CSS Text 3 §4.1.1: Only signal has_space for collapsible spaces.
            if (collapse_spaces) {
                lycon->line.has_space = true;
                if (wrap_lines && !lycon->line.is_line_start && !*str &&
                    lycon->font.font_handle &&
                    font_handle_is_document_font(lycon->font.font_handle)) {
                    lycon->line.wrap_opportunity_before_nowrap = true;
                }
            }
            // CSS 2.1 §16.6.1: Only track trailing space for end-of-line trimming
            if (collapse_spaces) {
                lycon->line.trailing_space_width = wd;
            }
            // CSS Text 3 §4.1.3: For pre-wrap, track accumulated trailing space
            // of line (CSS Text 3 §3: "preserved white space takes up space and
            if (!collapse_spaces && wrap_lines && white_space != CSS_VALUE_BREAK_SPACES) {
                lycon->line.hanging_space_width += wd;
                lycon->line.hanging_space_text_trim += wd;  // regular ASCII spaces get trimmed from text rects
            }
            lycon->line.last_space_hanging_width = lycon->line.hanging_space_width;
            lycon->line.last_space_hanging_text_trim = lycon->line.hanging_space_text_trim;
        }
        else if (codepoint == 0x3000) {
            // CSS Text 3 §4.1.3: U+3000 IDEOGRAPHIC SPACE is a hangable break opportunity.
            str = next_ch;
            lycon->line.last_space = str - 1;
            lycon->line.last_space_pos = rect->width;
            lycon->line.last_space_kind = BRK_IDEOGRAPHIC_SPACE;
            // CSS Text 3 §4.1.1: Only signal has_space for collapsible spaces
            if (collapse_spaces) {
                lycon->line.has_space = true;
            }
            lycon->line.is_line_start = false;
            // For break-spaces, spaces don't hang (CSS Text 3 §3), so skip accumulation.
            if (white_space != CSS_VALUE_BREAK_SPACES) {
                lycon->line.hanging_space_width += wd;
            }
            lycon->line.last_space_hanging_width = lycon->line.hanging_space_width;
            lycon->line.last_space_hanging_text_trim = lycon->line.hanging_space_text_trim;
        }
        else if (is_other_space_separator(codepoint) && codepoint != 0x3000
                 && codepoint != 0x00A0 && codepoint != 0x202F) {
            str = next_ch;
            lycon->line.last_space = str - 1;
            lycon->line.last_space_pos = rect->width;
            lycon->line.last_space_kind = BRK_HYPHEN;  // BA class: break-after, width included
            mark_line_non_space(&lycon->line);
        }
        else if (codepoint == 0x002D || codepoint == 0x2010 || codepoint == 0x2013 || codepoint == 0x2014) {
            // Hyphens and dashes are break opportunities (CSS Text 3 §5.2, UAX #14)
            str = next_ch;
            lycon->line.last_space = str - 1;  // last byte of the dash
            lycon->line.last_space_pos = rect->width;  // width including the dash
            lycon->line.last_space_kind = BRK_HYPHEN;
            mark_line_non_space(&lycon->line);
        }
        else if (codepoint == 0x003F && wrap_lines && !lycon->line.is_line_start) {
            // CSS Text 3 §5.2: UAs may add wrap opportunities at typographic symbol units.
            str = next_ch;
            uint32_t next_cp = peek_codepoint(str);
            if ((next_cp >= 'A' && next_cp <= 'Z') || (next_cp >= 'a' && next_cp <= 'z')
                    || (next_cp >= '0' && next_cp <= '9')) {
                lycon->line.last_space = str - 1;
                lycon->line.last_space_pos = rect->width;
                lycon->line.last_space_kind = BRK_TEXT;
            }
            mark_line_non_space(&lycon->line);
        }
        else if (((break_all && (is_typographic_letter_unit(codepoint)
                                  // CSS Text 3 §5.2: line-break: anywhere introduces soft wrap
                                  || (line_break_val == CSS_VALUE_ANYWHERE && (codepoint == 0x00A0 || codepoint == 0x202F))))
                  || (has_id_line_break_class(codepoint) && !keep_all)) && wrap_lines) {
            // UAX #14 / CSS Text 3 §5.2: Apply OP/CL/NS rules:
            str = next_ch;
            mark_line_non_space(&lycon->line);

            bool allow_break = true;
            // CSS Text 3 §5.2: line-break: anywhere overrides standard UAX#14 prohibitions.
            if (line_break_val != CSS_VALUE_ANYWHERE) {
            if (zwj_preceded) allow_break = false;
            // CSS Text 3 §5.2: No break after OP characters (OP stays with following content)
            if (allow_break && is_line_break_op(codepoint)) allow_break = false;
            if (allow_break) {
                uint32_t next_cp = peek_codepoint(str);
                if (next_cp == 0) next_cp = peek_next_inline_codepoint(text_node);
                if (next_cp == 0x200D) {
                    allow_break = false;  // ZWJ follows: suppress break
                } else if (next_cp > 0) {
                    bool is_loose = (line_break_val == CSS_VALUE_LOOSE);
                    if (is_line_break_cl(next_cp)) {
                        allow_break = false;
                    }
                    // CSS Text 3 §6.2: CJ → NS for Japanese (normal/strict), CJ → ID for
                    else if (is_line_break_ns(next_cp) || (cj_is_non_starter && is_line_break_cj(next_cp))) {
                        allow_break = false;
                    }
                    else if (is_line_break_ex_is_sy(next_cp) && !(is_loose && is_fullwidth_ex(next_cp))) {
                        allow_break = false;
                    }
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
            str = next_ch;
            mark_line_non_space(&lycon->line);
            zwj_preceded = false;
            // UAX #14 / CSS Text 3 §5.2: CL/NS characters adjacent to CJK text
            if (wrap_lines && (is_line_break_cl(codepoint) || is_line_break_ns(codepoint))) {
                uint32_t next_cp = peek_codepoint(str);
                if (!(next_cp > 0 && (is_line_break_cl(next_cp) || is_line_break_ns(next_cp)))) {
                    lycon->line.last_space = str - 1;
                    lycon->line.last_space_pos = rect->width;
                    lycon->line.last_space_kind = is_line_break_cl(codepoint) ? BRK_CL : BRK_NS;
                }
            }
        }
    } while (*str);
    if (wrap_lines && lycon->line.last_space) { // need to check if line will fill up (only when wrapping)
        float saved_advance_x = lycon->line.advance_x;  lycon->line.advance_x += rect->width;
        if (view_has_line_filled(lycon, text_view) == RDT_LINE_FILLED) {
            if (text_start <= lycon->line.last_space && lycon->line.last_space < str) {
                lycon->line.advance_x = saved_advance_x;
                if (output_break_at_last_space(
                        lycon, text_node, text_view, rect, &str, text_start, text_end,
                        trim_cjk_spacing, false, &soft_hyphen_leading_width)) {
                    goto LAYOUT_TEXT;
                }
                return;
            }
            else { // last_space outside the text, break at start of text
                lycon->line.advance_x = saved_advance_x;
                line_break(lycon);
                rect->x = lycon->line.advance_x;  rect->y = lycon->block.advance_y;
            }
        }
        else {
            lycon->line.advance_x = saved_advance_x;
        }
    }
    output_text(lycon, text_view, rect, str - text_start - rect->start_index, rect->width);


    auto t_end = high_resolution_clock::now();
    g_text_layout_time += duration<double, std::milli>(t_end - t_start).count();
    g_text_layout_count++;
}
