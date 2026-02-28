/**
 * html-defs.cpp — shared HTML tag/attribute classification
 *
 * Sorted arrays with binary search for O(log n) lookups.
 */
#include "html-defs.h"
#include "../../lib/str.h"
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

static bool lookup(const char* table[], int count, const char* name, size_t len) {
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = str_icmp(name, len, table[mid], strlen(table[mid]));
        if (cmp < 0)      hi = mid - 1;
        else if (cmp > 0) lo = mid + 1;
        else               return true;
    }
    return false;
}

// ── public API ─────────────────────────────────────────────────────

bool html_is_void_element(const char* tag, size_t len) {
    return lookup(void_elements, void_elements_count, tag, len);
}

bool html_is_raw_text_element(const char* tag, size_t len) {
    return lookup(raw_text_elements, raw_text_elements_count, tag, len);
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
