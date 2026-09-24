/**
 * html-defs.cpp — shared HTML tag/attribute classification
 *
 * Sorted arrays with binary search for O(log n) lookups.
 */
#include "html-defs.h"
#include "../core/well_known_markup_names.h"
#include "../../lib/str.h"
#include "../../lib/binsearch.h"
#include <string.h>

// ── sorted tag/attribute tables ────────────────────────────────────

static const char* void_elements[] = {
    "area", "base", "br", "col", "command", "embed", "hr", "img",
    "input", "keygen", "link", "menuitem", "meta", "param", "source",
    "track", "wbr"
};
static const int void_elements_count = sizeof(void_elements) / sizeof(void_elements[0]);

static const char* raw_text_elements[] = {
    "iframe", "noembed", "noframes", "noscript", "plaintext",
    "script", "style", "textarea", "title", "xmp"
};
static const int raw_text_elements_count = sizeof(raw_text_elements) / sizeof(raw_text_elements[0]);

static const char* boolean_attributes[] = {
    "async", "autofocus", "autoplay", "checked", "controls", "default",
    "defer", "disabled", "formnovalidate", "hidden", "ismap", "loop",
    "multiple", "muted", "nomodule", "novalidate", "open", "playsinline",
    "readonly", "required", "reversed", "selected"
};
static const int boolean_attributes_count = sizeof(boolean_attributes) / sizeof(boolean_attributes[0]);

static const char* block_elements[] = {
    "address", "article", "aside", "blockquote", "center", "dd",
    "details", "dialog", "dir", "div", "dl", "dt", "fieldset",
    "figcaption", "figure", "footer", "form", "h1", "h2", "h3",
    "h4", "h5", "h6", "header", "hgroup", "hr", "li", "listing",
    "main", "menu", "nav", "ol", "p", "plaintext", "pre", "search",
    "section", "summary", "table", "ul", "xmp"
};
static const int block_elements_count = sizeof(block_elements) / sizeof(block_elements[0]);

// ── binary search helper ───────────────────────────────────────────
// thin wrapper over lib/binsearch.h; case-insensitive ASCII lookup.

static inline bool lookup(const char* const* table, int count, const char* name, size_t len) {
    return binsearch_strtab_n(table, count, name, len, /*case_insensitive=*/true) >= 0;
}

// ── public API ─────────────────────────────────────────────────────

bool html_is_void_element(const char* tag, size_t len) {
    return lookup(void_elements, void_elements_count, tag, len);
}

bool html_is_raw_text_element(const char* tag, size_t len) {
    return lookup(raw_text_elements, raw_text_elements_count, tag, len);
}

// ── id-first lookups ───────────────────────────────────────────────
// Void/raw-text bits of every well-known markup name, indexed by the ordinal
// of its NameId (markup names are segment 0). Built once from the tables
// above, so an id answers exactly what its spelling would. The per-element
// binary searches -- about eight case-folding compares -- were a quarter of
// HTML output time.

enum { HTML_NAME_VOID = 1, HTML_NAME_RAW_TEXT = 2 };

struct HtmlMarkupNameBits {
    uint8_t bits[65536];  // the whole ordinal space: no bound check per lookup
    HtmlMarkupNameBits() {
        // static storage is zero-initialized; set only the classified names
        for (size_t i = 0; i < g_well_known_markup_name_count; i++) {
            const WellKnownNameRecord* rec = &g_well_known_markup_names[i];
            NameId id = rec->meta.name_id;
            if (id == NAME_ID_NONE || (id >> 16) != 0) continue;
            uint8_t b = 0;
            if (html_is_void_element(rec->chars, rec->len)) b |= HTML_NAME_VOID;
            if (html_is_raw_text_element(rec->chars, rec->len)) b |= HTML_NAME_RAW_TEXT;
            bits[id] = b;
        }
    }
};

// the class bits of a markup-segment id, or -1 when the id is not one
static int html_markup_name_bits(uint32_t tag_id) {
    if (tag_id == NAME_ID_NONE || (tag_id >> 16) != 0) return -1;
    static const HtmlMarkupNameBits table;
    return table.bits[tag_id];
}

bool html_is_void_element_id(uint32_t tag_id, const char* tag, size_t len) {
    int bits = html_markup_name_bits(tag_id);
    return bits >= 0 ? (bits & HTML_NAME_VOID) != 0 : html_is_void_element(tag, len);
}

bool html_is_raw_text_element_id(uint32_t tag_id, const char* tag, size_t len) {
    int bits = html_markup_name_bits(tag_id);
    return bits >= 0 ? (bits & HTML_NAME_RAW_TEXT) != 0 : html_is_raw_text_element(tag, len);
}

bool html_is_boolean_attribute(const char* attr, size_t len) {
    return lookup(boolean_attributes, boolean_attributes_count, attr, len);
}

bool html_is_block_element(const char* tag, size_t len) {
    return lookup(block_elements, block_elements_count, tag, len);
}

bool html_is_heading(const char* tag, size_t len) {
    if (len != 2) return false;
    char c0 = tag[0], c1 = tag[1];
    return (c0 == 'h' || c0 == 'H') && c1 >= '1' && c1 <= '6';
}

int html_heading_level(const char* tag, size_t len) {
    if (len != 2) return 0;
    char c0 = tag[0], c1 = tag[1];
    if ((c0 == 'h' || c0 == 'H') && c1 >= '1' && c1 <= '6')
        return c1 - '0';
    return 0;
}
