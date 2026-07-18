#include "layout.hpp"
#include "view.hpp"

#include "../lib/log.h"
#include "../lib/tagged.hpp"
#include <string.h>

void layout_form_control(LayoutContext* lycon, ViewBlock* block);

static bool layout_measure_cache_get(LayoutContext* lycon, ViewBlock* block,
    AvailableSpace space, IntrinsicSize* out, const char* label) {
    if (!lycon || !block || !out) return false;
    DomElement* element = block->as_element();
    if (!element) return false;

    radiant::SizeF cached = radiant::size_f_zero();
    radiant::KnownDimensions known = radiant::layout_known_dimensions_from_block(block);
    if (!radiant::layout_pass_cache_get_for_space(lycon, element, known, space, &cached, label)) {
        return false;
    }

    out->min_width = cached.width;
    out->max_width = cached.width;
    out->min_height = cached.height;
    out->max_height = cached.height;
    return true;
}

static void layout_measure_cache_store(LayoutContext* lycon, ViewBlock* block,
    AvailableSpace space, IntrinsicSize result, const char* label) {
    if (!lycon || !block) return;
    DomElement* element = block->as_element();
    if (!element) return;

    radiant::KnownDimensions known = radiant::layout_known_dimensions_from_block(block);
    radiant::SizeF size = radiant::size_f(result.max_width, result.max_height);
    radiant::layout_pass_cache_store_for_space(lycon, element, known, space, size, label);
}

IntrinsicSize layout_measure_replaced(LayoutContext* lycon, ViewBlock* block, AvailableSpace space) {
    IntrinsicSize result = {};
    if (!block) return result;

    radiant::LayoutMeasureScope measure_scope(lycon, block);
    if (lycon) lycon->available_space = space;

    if (layout_measure_cache_get(lycon, block, space, &result, "REPLACED_MEASURE")) {
        return result;
    }

    float width = block->width > 0.0f ? block->width : 0.0f;
    float height = block->height > 0.0f ? block->height : 0.0f;
    if (block->embed && block->embedp()->img) {
        if (block->embedp()->img->width > 0) width = (float)block->embedp()->img->width;
        if (block->embedp()->img->height > 0) height = (float)block->embedp()->img->height;
    }
    uintptr_t tag = block->tag();
    if (width <= 0.0f || height <= 0.0f) {
        if (tag == HTM_TAG_IFRAME || tag == HTM_TAG_VIDEO || tag == HTM_TAG_CANVAS ||
            tag == HTM_TAG_OBJECT || tag == HTM_TAG_EMBED || tag == HTM_TAG_SVG) {
            if (width <= 0.0f) width = 300.0f;
            if (height <= 0.0f) height = 150.0f;
        } else if (tag == HTM_TAG_AUDIO) {
            if (width <= 0.0f) width = 300.0f;
            if (height <= 0.0f) height = 54.0f;
        } else if (tag == HTM_TAG_METER) {
            if (width <= 0.0f) width = FormDefaults::METER_WIDTH;
            if (height <= 0.0f) height = FormDefaults::METER_HEIGHT;
        } else if (tag == HTM_TAG_PROGRESS) {
            if (width <= 0.0f) width = FormDefaults::PROGRESS_WIDTH;
            if (height <= 0.0f) height = FormDefaults::PROGRESS_HEIGHT;
        }
    }
    result.min_width = width;
    result.max_width = width;
    result.min_height = height;
    result.max_height = height;
    layout_measure_cache_store(lycon, block, space, result, "REPLACED_MEASURE");
    return result;
}

IntrinsicSize layout_measure_form_control(LayoutContext* lycon, ViewBlock* block, AvailableSpace space) {
    IntrinsicSize result = {};
    if (!block || !block->form) return result;

    radiant::LayoutMeasureScope measure_scope(lycon, block);
    if (lycon) lycon->available_space = space;

    if (layout_measure_cache_get(lycon, block, space, &result, "FORM_MEASURE")) {
        return result;
    }

    float width = block->form->intrinsic_width > 0.0f ? block->form->intrinsic_width : block->width;
    float height = block->form->intrinsic_height > 0.0f ? block->form->intrinsic_height : block->height;

    if (lycon && lycon->ui_context) {
        layout_form_control(lycon, block);
        if (block->content_width > 0.0f) width = block->content_width;
        else if (block->width > 0.0f) width = block->width;
        if (block->content_height > 0.0f) height = block->content_height;
        else if (block->height > 0.0f) height = block->height;
    }

    result.min_width = width > 0.0f ? width : 0.0f;
    result.max_width = result.min_width;
    result.min_height = height > 0.0f ? height : 0.0f;
    result.max_height = result.min_height;
    layout_measure_cache_store(lycon, block, space, result, "FORM_MEASURE");
    return result;
}

IntrinsicSizes layout_measure_intrinsic_widths(LayoutContext* lycon, DomElement* element,
    const char* log_context, bool content_only) {
    IntrinsicSizes sizes = {};
    if (!lycon || !element) return sizes;

    radiant::LayoutProfileScope profile_scope(lycon, radiant::LAYOUT_PROFILE_INTRINSIC, element);
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
