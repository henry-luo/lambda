/**
 * @file html_entities.cpp
 * @brief Unified HTML5 named-entity resolution (WHATWG table, binary search)
 *
 * The entity table is auto-generated into html_entities_table.inc by:
 *   python3 utils/generate_html5_entities.py
 */

#include "html_entities.h"
#include "input-utils.h"
#include <cstring>

// ── Table entry type ───────────────────────────────────────────────
struct HtmlEntityEntry {
    const char* name;
    const char* replacement;  // pre-encoded UTF-8
};

// ── Auto-generated sorted table ────────────────────────────────────
#include "html_entities_table.inc"

// ── Lookup (binary search, case-sensitive) ─────────────────────────
const char* html_entity_lookup(const char* name, size_t len) {
    if (!name || len == 0) return nullptr;

    size_t low = 0;
    size_t high = HTML_ENTITY_COUNT;

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        const char* mid_name = html_entity_table[mid].name;
        size_t mid_len = strlen(mid_name);

        size_t min_len = len < mid_len ? len : mid_len;
        int cmp = memcmp(name, mid_name, min_len);
        if (cmp == 0) {
            if (len < mid_len) cmp = -1;
            else if (len > mid_len) cmp = 1;
        }

        if (cmp < 0)      high = mid;
        else if (cmp > 0)  low = mid + 1;
        else                return html_entity_table[mid].replacement;
    }
    return nullptr;
}

// ── ASCII-escape check ─────────────────────────────────────────────
bool html_entity_is_ascii_escape(const char* name, size_t len) {
    // Five XML built-in entities
    if (len == 2 && memcmp(name, "lt", 2) == 0) return true;
    if (len == 2 && memcmp(name, "gt", 2) == 0) return true;
    if (len == 3 && memcmp(name, "amp", 3) == 0) return true;
    if (len == 4 && memcmp(name, "quot", 4) == 0) return true;
    if (len == 4 && memcmp(name, "apos", 4) == 0) return true;
    return false;
}

// ── Reverse lookup (codepoint → entity name) ──────────────────────
const char* html_entity_name_for_codepoint(uint32_t codepoint) {
    if (codepoint == 0) return nullptr;

    // Encode the target codepoint to UTF-8 for comparison
    char target[5];
    int target_len = codepoint_to_utf8(codepoint, target);
    if (target_len == 0) return nullptr;

    // Linear scan — reverse lookups are rare (formatting path only)
    for (size_t i = 0; i < HTML_ENTITY_COUNT; i++) {
        const char* rep = html_entity_table[i].replacement;
        if (memcmp(rep, target, target_len) == 0 && rep[target_len] == '\0') {
            return html_entity_table[i].name;
        }
    }
    return nullptr;
}

// ── UTF-8 → first codepoint ───────────────────────────────────────
uint32_t utf8_first_codepoint(const char* utf8) {
    if (!utf8 || !*utf8) return 0;

    unsigned char c = (unsigned char)utf8[0];
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0) {
        if (!utf8[1]) return 0;
        return ((uint32_t)(c & 0x1F) << 6) |
               ((uint32_t)(utf8[1] & 0x3F));
    }
    if ((c & 0xF0) == 0xE0) {
        if (!utf8[1] || !utf8[2]) return 0;
        return ((uint32_t)(c & 0x0F) << 12) |
               ((uint32_t)(utf8[1] & 0x3F) << 6) |
               ((uint32_t)(utf8[2] & 0x3F));
    }
    if ((c & 0xF8) == 0xF0) {
        if (!utf8[1] || !utf8[2] || !utf8[3]) return 0;
        return ((uint32_t)(c & 0x07) << 18) |
               ((uint32_t)(utf8[1] & 0x3F) << 12) |
               ((uint32_t)(utf8[2] & 0x3F) << 6) |
               ((uint32_t)(utf8[3] & 0x3F));
    }
    return 0;
}
