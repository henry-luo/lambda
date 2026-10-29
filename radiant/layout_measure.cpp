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

    // a cached scalar size needs compute mode, but must leave live fragments alone.
    radiant::LayoutRunModeScope run_mode(lycon, radiant::RunMode::ComputeSize);
    if (layout_measure_cache_get(lycon, block, space, &result, "REPLACED_MEASURE")) {
        return result;
    }
    // intrinsic resource facts do not lay out content or replace live fragments.
    radiant::LayoutMeasureScope measure_scope(lycon, block, true);
    if (lycon) lycon->available_space = space;

    ReplacedIntrinsicFacts facts = layout_replaced_intrinsic_facts(lycon, block);
    float width = facts.width;
    float height = facts.height;
    return layout_measure_flat_size(lycon, block, space, width, height, "REPLACED_MEASURE");
}

IntrinsicSize layout_measure_form_control(LayoutContext* lycon, ViewBlock* block, AvailableSpace space) {
    IntrinsicSize result = {};
    if (!block || !block->form) return result;

    radiant::LayoutRunModeScope run_mode(lycon, radiant::RunMode::ComputeSize);
    if (layout_measure_cache_get(lycon, block, space, &result, "FORM_MEASURE")) {
        return result;
    }
    // native sizing writes scalar boxes, not flow fragments; restoring those
    // boxes must not discard the enclosing flex item's committed content.
    radiant::LayoutMeasureScope measure_scope(lycon, block, true);
    if (lycon) lycon->available_space = space;

    float width = block->form->intrinsic_width > 0.0f ? block->form->intrinsic_width : block->width;
    float height = block->form->intrinsic_height > 0.0f ? block->form->intrinsic_height : block->height;

    if (lycon && lycon->ui_context) {
        // measurement shares flow's percentage definiteness before reading current used content sizes.
        layout_clear_cyclic_percentage_height(lycon, block);
        layout_form_control(lycon, block);
        // zero is a valid used content size, including a border-only control.
        width = block->content_width;
        height = block->content_height;
    }

    return layout_measure_flat_size(lycon, block, space, width, height, "FORM_MEASURE");
}

IntrinsicSizes layout_measure_intrinsic_widths(LayoutContext* lycon, DomElement* element,
    bool content_only) {
    IntrinsicSizes sizes = {};
    if (!lycon || !element) return sizes;

    // only a cache miss needs provisional view snapshots and layout restoration.
    radiant::LayoutProfileScope profile_scope(lycon, radiant::LAYOUT_PROFILE_INTRINSIC, element);
    sizes = measure_element_intrinsic_widths(lycon, element, content_only, true);
    return sizes;
}

TextIntrinsicWidths layout_measure_text_intrinsic_widths(LayoutContext* lycon,
    const char* text, size_t length, CssEnum text_transform, CssEnum font_variant,
    CssEnum white_space, CssEnum overflow_wrap, CssEnum word_break, CssEnum line_break) {
    TextIntrinsicWidths widths = {};
    if (!lycon || !text) return widths;

    widths = measure_text_intrinsic_widths(lycon, text, length, text_transform, font_variant,
                                           white_space, overflow_wrap, word_break, line_break);
    return widths;
}
