#include "layout_measure.hpp"
#include "form_control.hpp"

#include "../lib/log.h"
#include "../lib/tagged.hpp"
#include <string.h>

IntrinsicSize layout_measure_intrinsic(LayoutContext* lycon, DomNode* node,
    AvailableSpace space) {
    IntrinsicSize result = {};
    if (!lycon || !node) return result;

    if (node->is_element()) {
        ViewBlock* block = lam::view_as_block(node->as_element());
        if (!block) return result;

        IntrinsicSizesBidirectional sizes = measure_intrinsic_sizes(lycon, block, space);
        result.min_width = sizes.min_content_width;
        result.max_width = sizes.max_content_width;
        result.min_height = sizes.min_content_height;
        result.max_height = sizes.max_content_height;
        return result;
    }

    if (node->is_text()) {
        const char* text = (const char*)node->text_data();
        if (!text) return result;

        size_t length = strlen(text);
        TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, text, length);
        result.min_width = widths.min_content;
        result.max_width = widths.max_content;
    }
    return result;
}

IntrinsicSize layout_measure_replaced(ViewBlock* block, AvailableSpace space) {
    (void)space;
    IntrinsicSize result = {};
    if (!block) return result;

    float width = block->width > 0.0f ? block->width : 0.0f;
    float height = block->height > 0.0f ? block->height : 0.0f;
    if (block->embed && block->embed->img) {
        if (block->embed->img->width > 0) width = (float)block->embed->img->width;
        if (block->embed->img->height > 0) height = (float)block->embed->img->height;
    }
    result.min_width = width;
    result.max_width = width;
    result.min_height = height;
    result.max_height = height;
    return result;
}

IntrinsicSize layout_measure_form_control(ViewBlock* block, AvailableSpace space) {
    (void)space;
    IntrinsicSize result = {};
    if (!block || !block->form) return result;

    float width = block->form->intrinsic_width > 0.0f ? block->form->intrinsic_width : block->width;
    float height = block->form->intrinsic_height > 0.0f ? block->form->intrinsic_height : block->height;
    result.min_width = width > 0.0f ? width : 0.0f;
    result.max_width = result.min_width;
    result.min_height = height > 0.0f ? height : 0.0f;
    result.max_height = result.min_height;
    return result;
}

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
