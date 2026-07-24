// format-math-shared.hpp — Shared infrastructure for math formatters.
//
// Included by format-math-ascii.cpp and format-math-latex.cpp.
// Each including file must define (before or after the include):
//   static void format_element_impl(StringBuf*, const ElementReader&, int);
//   static void format_symbol_impl(StringBuf*, Symbol*);
//   static void format_children(StringBuf*, const ElementReader&, int, const char*);
//
// This header provides format_item() (shared dispatch skeleton) and
// format_punctuation()/format_delimiter() (output-identical in all formats).

#ifndef FORMAT_MATH_SHARED_HPP
#define FORMAT_MATH_SHARED_HPP

#include "format.h"
#include "../core/mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"
#include <stdio.h>
#include <string.h>

// Per-file implementations resolved within each translation unit:
static void format_element_impl(StringBuf* sb, const ElementReader& elem, int depth);
static void format_symbol_impl(StringBuf* sb, Symbol* sym);
static void format_children(StringBuf* sb, const ElementReader& elem, int depth, const char* sep);

// ============================================================================
// Shared element handlers (identical output in all math formats)
// ============================================================================

static void format_punctuation(StringBuf* sb, const ElementReader& elem) {
    ItemReader val = elem.get_attr("value");
    if (!val.isNull() && val.isString())
        stringbuf_append_str(sb, val.asString()->chars);
}

static void format_delimiter(StringBuf* sb, const ElementReader& elem) {
    ItemReader val = elem.get_attr("value");
    if (!val.isNull() && val.isString())
        stringbuf_append_str(sb, val.asString()->chars);
}

// ============================================================================
// Shared format_item dispatch skeleton
// ============================================================================

static void format_item(StringBuf* sb, const ItemReader& item, int depth) {
    if (depth > 50) { stringbuf_append_str(sb, "..."); return; }
    if (item.isElement()) {
        format_element_impl(sb, item.asElement(), depth);
    } else if (item.isString()) {
        String* str = item.asString();
        if (str) stringbuf_append_str(sb, str->chars);
    } else if (item.isSymbol()) {
        Symbol* sym = item.asSymbol();
        if (sym) format_symbol_impl(sb, sym);
    } else if (item.isNumber()) {
        format_number(sb, item.item());
    }
}

// Format the root `math` element: emit children separated by spaces (identical in all formats)
static void format_math_root(StringBuf* sb, const ElementReader& elem, int depth) {
    format_children(sb, elem, depth, " ");
}

struct MathTagDispatch {
    const char* tag;
    int slot;
};

static int lookup_math_tag_slot(const MathTagDispatch* table, const char* tag, int fallback_slot) {
    if (!table || !tag) return fallback_slot;
    for (const MathTagDispatch* entry = table; entry->tag; entry++) {
        if (strcmp(tag, entry->tag) == 0) return entry->slot;
    }
    return fallback_slot;
}

#endif // FORMAT_MATH_SHARED_HPP
