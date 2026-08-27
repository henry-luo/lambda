#include "layout.hpp"
#include "view.hpp"
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

static IntrinsicSize layout_measure_flat_size(LayoutContext* lycon, ViewBlock* block,
                                              AvailableSpace space, float width, float height,
                                              const char* label) {
    IntrinsicSize result = {};
    result.min_width = result.max_width = max(width, 0.0f);
    result.min_height = result.max_height = max(height, 0.0f);
    layout_measure_cache_store(lycon, block, space, result, label);
    return result;
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
    NameId tag = block->tag();
    if (width <= 0.0f || height <= 0.0f) {
        if (tag == MARKUP_NAME_IFRAME || tag == MARKUP_NAME_VIDEO || tag == MARKUP_NAME_CANVAS ||
            tag == MARKUP_NAME_OBJECT || tag == MARKUP_NAME_EMBED || tag == MARKUP_NAME_SVG) {
            if (width <= 0.0f) width = 300.0f;
            if (height <= 0.0f) height = 150.0f;
        } else if (tag == MARKUP_NAME_AUDIO) {
            if (width <= 0.0f) width = 300.0f;
            if (height <= 0.0f) height = 54.0f;
        } else if (tag == MARKUP_NAME_METER) {
            if (width <= 0.0f) width = form_control_em_size(
                lycon, block, FormDefaults::METER_INLINE_SIZE_EM);
            if (height <= 0.0f) height = form_control_em_size(
                lycon, block, FormDefaults::FORM_WIDGET_BLOCK_SIZE_EM);
        } else if (tag == MARKUP_NAME_PROGRESS) {
            if (width <= 0.0f) width = form_control_em_size(
                lycon, block, FormDefaults::PROGRESS_INLINE_SIZE_EM);
            if (height <= 0.0f) height = form_control_em_size(
                lycon, block, FormDefaults::FORM_WIDGET_BLOCK_SIZE_EM);
        }
    }
    return layout_measure_flat_size(lycon, block, space, width, height, "REPLACED_MEASURE");
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

    return layout_measure_flat_size(lycon, block, space, width, height, "FORM_MEASURE");
}

IntrinsicSizes layout_measure_intrinsic_widths(LayoutContext* lycon, DomElement* element,
    bool content_only) {
    IntrinsicSizes sizes = {};
    if (!lycon || !element) return sizes;

    radiant::LayoutProfileScope profile_scope(lycon, radiant::LAYOUT_PROFILE_INTRINSIC, element);
    sizes = measure_element_intrinsic_widths(lycon, element, content_only);
    return sizes;
}

TextIntrinsicWidths layout_measure_text_intrinsic_widths(LayoutContext* lycon,
    const char* text, size_t length, CssEnum text_transform, CssEnum font_variant,
    CssEnum white_space, CssEnum overflow_wrap, CssEnum word_break) {
    TextIntrinsicWidths widths = {};
    if (!lycon || !text) return widths;

    widths = measure_text_intrinsic_widths(lycon, text, length, text_transform, font_variant,
                                           white_space, overflow_wrap, word_break);
    return widths;
}
