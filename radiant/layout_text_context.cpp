// contextual text measurement across CSS inline boundaries.
#include "layout.hpp"
#include "../lib/utf.h"
#include <limits.h>

static CssEnum text_shaping_boundary_keyword(DomElement* element,
        CssPropertyCode property, CssEnum initial, CssEnum inherited) {
    for (DomElement* current = element; current; current = dom_parent_element(current)) {
        CssDeclaration* declaration = current->specified_style
            ? style_tree_get_declaration(current->specified_style, property) : nullptr;
        if (declaration && declaration->value) {
            if (declaration->value->type != CSS_VALUE_TYPE_KEYWORD) return CSS_VALUE__UNDEF;
            CssEnum value = declaration->value->data.keyword;
            if (value == CSS_VALUE_INHERIT ||
                (value == CSS_VALUE_UNSET && property == CSS_PROPERTY_DIRECTION)) continue;
            if (value == CSS_VALUE_INITIAL || value == CSS_VALUE_UNSET) return initial;
            return value;
        }
        if (current->styles_resolved() && !current->needs_style_recompute()) {
            CssEnum value = property == CSS_PROPERTY_VERTICAL_ALIGN
                ? (current->in_line ? current->inl()->vertical_align : CSS_VALUE__UNDEF)
                : (current->blk ? (property == CSS_PROPERTY_DIRECTION
                    ? current->block()->direction : current->block()->unicode_bidi) : CSS_VALUE__UNDEF);
            if (value) return value;
        }
        const char* direction = current->get_attribute("dir");
        if (property == CSS_PROPERTY_UNICODE_BIDI) {
            if (current->tag() == MARKUP_NAME_BDI ||
                (direction && str_ieq_cstr(direction, "auto"))) return CSS_VALUE_ISOLATE;
        } else if (property == CSS_PROPERTY_DIRECTION && direction) {
            if (str_ieq_cstr(direction, "rtl")) return CSS_VALUE_RTL;
            if (str_ieq_cstr(direction, "ltr")) return CSS_VALUE_LTR;
        }
        return property == CSS_PROPERTY_DIRECTION ? inherited : initial;
    }
    return initial;
}

static bool text_shaping_crosses_inline_edge(LayoutContext* lycon, DomElement* element,
                                               bool cursive, bool inline_start) {
    DisplayValue display = resolve_display_value(element);
    if (display.outer == CSS_VALUE_CONTENTS) return true;
    if (display.outer != CSS_VALUE_INLINE ||
        layout_intrinsic_element_is_atomic_inline(element)) return false;
    if (text_shaping_boundary_keyword(element, CSS_PROPERTY_VERTICAL_ALIGN,
            CSS_VALUE_BASELINE, CSS_VALUE_BASELINE) != CSS_VALUE_BASELINE) return false;
    CssEnum unicode_bidi = text_shaping_boundary_keyword(element, CSS_PROPERTY_UNICODE_BIDI,
        CSS_VALUE_NORMAL, CSS_VALUE_NORMAL);
    if (unicode_bidi == CSS_VALUE_ISOLATE || unicode_bidi == CSS_VALUE_ISOLATE_OVERRIDE) return false;
    CssEnum direction = text_shaping_boundary_keyword(element, CSS_PROPERTY_DIRECTION,
        CSS_VALUE_LTR, lycon->block.direction);
    bool physical_left = inline_start != (direction == CSS_VALUE_RTL);
    LayoutIntrinsicMarginPair margins = layout_intrinsic_horizontal_margin_pair(lycon, element);
    // only the edge separating these characters breaks shaping; the opposite edge is irrelevant.
    if ((physical_left ? margins.left : margins.right) != 0.0f ||
        layout_intrinsic_inline_edge(lycon, element, physical_left, false) != 0.0f) return false;
    if (!cursive && element->specified_style) {
        // a ligature cannot span unresolved glyph-format changes; cursive forms retain context.
        const CssPropertyCode properties[] = {CSS_PROPERTY_FONT, CSS_PROPERTY_FONT_FAMILY,
            CSS_PROPERTY_FONT_SIZE, CSS_PROPERTY_FONT_WEIGHT, CSS_PROPERTY_FONT_STYLE,
            CSS_PROPERTY_FONT_VARIANT, CSS_PROPERTY_LETTER_SPACING};
        for (CssPropertyCode property : properties) {
            if (style_tree_get_declaration(element->specified_style, property)) return false;
        }
    }
    return true;
}

static DomText* text_shaping_neighbor(LayoutContext* lycon, DomNode* node,
                                       bool forward, bool cursive) {
    while (node) {
        DomNode* sibling = forward ? node->next_sibling : node->prev_sibling;
        if (!sibling) {
            DomElement* parent = node->parent ? node->parent->as_element() : nullptr;
            if (!parent || !text_shaping_crosses_inline_edge(lycon, parent, cursive, !forward)) return nullptr;
            node = parent;
            continue;
        }
        node = sibling;
        while (node) {
            if (node->is_text()) {
                DomText* text = node->as_text();
                if (text->text && text->length) return text;
                break;
            }
            if (node->is_element()) {
                DomElement* element = node->as_element();
                if (layout_display_is_none(resolve_display_value(element)) ||
                    layout_element_is_abs_or_fixed(element) || layout_element_is_floated(element)) break;
                if (!text_shaping_crosses_inline_edge(lycon, element, cursive, forward)) return nullptr;
                DomNode* child = forward ? element->first_child : element->last_child;
                if (child) { node = child; continue; }
                // an empty decorated inline still separates its neighbors at both edges.
                if (!text_shaping_crosses_inline_edge(lycon, element, cursive, !forward)) return nullptr;
            }
            break;
        }
    }
    return nullptr;
}

static bool text_shaping_codepoint(uint32_t codepoint, bool cursive) {
    if (is_space(codepoint)) return false;
    // cursive shaping includes left-to-right scripts such as Mongolian;
    // bidi direction alone does not classify contextual glyph substitution.
    if (cursive) return utf_requires_contextual_shaping(codepoint) ||
        utf_bidi_strong_class(codepoint) == 1 || codepoint == 0x00AD ||
        codepoint == 0x200C || codepoint == 0x200D;
    // punctuation participates in pairs such as r-comma across an undecorated inline edge.
    return codepoint >= 0x21 && codepoint <= 0x7E;
}

bool layout_measure_shaping_text(LayoutContext* lycon, const char* text, size_t length,
        size_t range_start, size_t range_length, float* width) {
    if (!lycon || !text || !width || length > INT_MAX || range_start > length ||
        range_length > length - range_start || !lycon->font.style) return false;
    FontHandle* handle = font_box_handle(&lycon->font);
    FontHandle* fallback = nullptr;
    const char* cursor = text + range_start;
    const char* end = cursor + range_length;
    uint32_t codepoint = 0;
    while (cursor < end) {
        if (!layout_utf8_next_codepoint(&cursor, end, &codepoint)) return false;
        if (!text_codepoint_has_zero_advance(codepoint)) break;
    }
    if (codepoint && !font_has_codepoint(handle, codepoint) && lycon->ui_context) {
        // use the glyph loader's CSS fallback selection rather than CoreText's implicit cascade.
        FontStyleDesc style = font_style_desc_from_prop(lycon->font.style);
        fallback = font_resolve_for_codepoint(lycon->ui_context->font_ctx, &style, codepoint);
        if (fallback) handle = fallback;
    }
    TextExtents extents = {};
    bool measured = handle != nullptr;
    if (range_start == 0 && range_length == length) {
        extents = font_measure_text(handle, text, (int)length); // INT_CAST_OK: bounded font byte count
    } else {
        measured = font_measure_text_range(handle, text, (int)length, // INT_CAST_OK: bounded font byte counts
            (int)range_start, (int)range_length, &extents); // INT_CAST_OK: bounded source range
    }
    // fallback resolution retains its handle; no borrowed font outlives this measurement.
    if (fallback) font_handle_release(fallback);
    if (measured) *width = extents.width;
    return measured;
}

static void text_shaping_context_slice(const char* source, size_t length,
        bool forward, bool cursive, size_t* first, size_t* last) {
    *first = 0;
    *last = length;
    const char* cursor = source;
    const char* end = source + length;
    while (cursor < end) {
        const char* prior = cursor;
        uint32_t codepoint = 0;
        if (!layout_utf8_next_codepoint(&cursor, end, &codepoint) ||
            !text_shaping_codepoint(codepoint, cursive)) {
            if (forward) { *last = (size_t)(prior - source); break; }
            *first = (size_t)(cursor - source);
        }
    }
}

bool layout_measure_inline_text_context(LayoutContext* lycon, DomNode* text_node,
        const unsigned char* start, size_t length, bool cursive, float* width,
        const unsigned char* text_start, const unsigned char* text_end, bool* following_context) {
    if (following_context) *following_context = false;
    if (!lycon || !start || !length || !width ||
        (text_node && !text_node->is_text()) ||
        !font_box_handle(&lycon->font)) return false;
    if (!text_start && text_node) text_start = text_node->text_data();
    if (!text_end && text_node && text_start) text_end = text_start + text_node->as_text()->length;
    if (!text_start || !text_end) return false;
    if (start < text_start || start > text_end || length > (size_t)(text_end - start)) return false;
    ScratchScope scope(&lycon->scratch);
    struct Piece { const char* text; size_t length; Piece* next; };
    Piece* before = nullptr;
    Piece* after = nullptr;
    Piece** after_tail = &after;
    size_t before_length = 0, after_length = 0;
    auto append_piece = [&](const char* source, size_t count, bool forward) {
        Piece* piece = scope.array<Piece>(1);
        if (!piece) return false;
        *piece = {source, count, nullptr};
        if (forward) {
            *after_tail = piece; after_tail = &piece->next;
            after_length += count;
        } else {
            piece->next = before; before = piece;
            before_length += count;
        }
        return true;
    };
    const bool directions[] = {false, true};
    for (bool forward : directions) {
        const char* local = (const char*)(forward ? start + length : text_start);
        size_t local_length = forward ? (size_t)(text_end - start - length)
            : (size_t)(start - text_start);
        size_t local_first, local_last;
        text_shaping_context_slice(local, local_length, forward, cursive,
            &local_first, &local_last);
        // an emergency break or tracked grapheme still shapes in its unbroken word.
        if (local_last > local_first) {
            if (!append_piece(local + local_first, local_last - local_first, forward)) return false;
        }
        if (local_first != 0 || local_last != local_length) continue;
        DomNode* node = text_node;
        while (DomText* neighbor = text_shaping_neighbor(lycon, node, forward, cursive)) {
            const char* source = neighbor->text;
            size_t first, last;
            text_shaping_context_slice(source, neighbor->length, forward, cursive, &first, &last);
            if (last == first) break;
            if (!append_piece(source + first, last - first, forward)) return false;
            if (first != 0 || last != neighbor->length) break;
            node = neighbor;
        }
    }
    if (!before_length && !after_length) return false;
    size_t total_length = before_length + length + after_length;
    if (total_length > INT_MAX) return false;
    char* context = scope.array<char>(total_length);
    if (!context) return false;
    char* output = context;
    auto append = [&](const char* source, size_t count) {
        memcpy(output, source, count); output += count;
    };
    for (Piece* piece = before; piece; piece = piece->next) append(piece->text, piece->length);
    append((const char*)start, length);
    for (Piece* piece = after; piece; piece = piece->next) append(piece->text, piece->length);
    if (!layout_measure_shaping_text(lycon, context, total_length,
            before_length, length, width)) return false;
    if (following_context) *following_context = after_length != 0;
    return true;
}
