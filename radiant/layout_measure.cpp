#include "layout_measure.hpp"

#include "../lib/log.h"

IntrinsicSizes layout_measure_intrinsic_widths(LayoutContext* lycon, DomElement* element,
    const char* log_context, bool content_only) {
    IntrinsicSizes sizes = {};
    if (!lycon || !element) return sizes;

    sizes = measure_element_intrinsic_widths(lycon, element, content_only);
    if (log_context) {
        log_debug("[LAYOUT_MEASURE] %s element=%s min=%.1f max=%.1f",
                  log_context, element->node_name(), sizes.min_content, sizes.max_content);
    }
    return sizes;
}

TextIntrinsicWidths layout_measure_text_intrinsic_widths(LayoutContext* lycon,
    const char* text, size_t length, CssEnum text_transform, CssEnum font_variant,
    CssEnum white_space, CssEnum overflow_wrap, CssEnum word_break, const char* log_context) {
    TextIntrinsicWidths widths = {};
    if (!lycon || !text) return widths;

    widths = measure_text_intrinsic_widths(lycon, text, length, text_transform, font_variant,
                                           white_space, overflow_wrap, word_break);
    if (log_context) {
        log_debug("[LAYOUT_MEASURE] %s text_len=%zu min=%.1f max=%.1f",
                  log_context, length, widths.min_content, widths.max_content);
    }
    return widths;
}
