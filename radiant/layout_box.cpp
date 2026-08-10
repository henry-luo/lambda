#include "layout.hpp"

#include "../lib/log.h"

BoxMetrics layout_boundary_metrics(const BoundaryProp* bound) {
    BoxMetrics metrics = {};
    if (!bound) return metrics;

    metrics.margin.left = bound->margin.left;
    metrics.margin.right = bound->margin.right;
    metrics.margin.top = bound->margin.top;
    metrics.margin.bottom = bound->margin.bottom;

    metrics.padding.left = bound->padding.left;
    metrics.padding.right = bound->padding.right;
    metrics.padding.top = bound->padding.top;
    metrics.padding.bottom = bound->padding.bottom;

    if (bound->border) {
        metrics.border.left = bound->border->width.left;
        metrics.border.right = bound->border->width.right;
        metrics.border.top = bound->border->width.top;
        metrics.border.bottom = bound->border->width.bottom;
    }

    metrics.margin_h = metrics.margin.left + metrics.margin.right;
    metrics.margin_v = metrics.margin.top + metrics.margin.bottom;
    metrics.padding_h = metrics.padding.left + metrics.padding.right;
    metrics.padding_v = metrics.padding.top + metrics.padding.bottom;
    metrics.border_h = metrics.border.left + metrics.border.right;
    metrics.border_v = metrics.border.top + metrics.border.bottom;
    metrics.pad_border_h = metrics.padding_h + metrics.border_h;
    metrics.pad_border_v = metrics.padding_v + metrics.border_v;
    return metrics;
}

BoxMetrics layout_box_metrics(ViewBlock* block) {
    return layout_boundary_metrics(block ? block->bound : nullptr);
}

void layout_store_given_axis(LayoutContext* lycon, ViewBlock* block, float size,
                             bool horizontal, bool reset_type) {
    if (!lycon || !block) return;
    float* context_size = horizontal ? &lycon->block.given_width : &lycon->block.given_height;
    *context_size = size;
    if (!block->blk) return;
    float* block_size = horizontal ? &block->blk->given_width : &block->blk->given_height;
    *block_size = size;
    if (reset_type) {
        CssEnum* block_type = horizontal ? &block->blk->given_width_type
                                         : &block->blk->given_height_type;
        *block_type = CSS_VALUE__UNDEF;
    }
}

void layout_clear_given_axis(LayoutContext* lycon, ViewBlock* block, bool horizontal) {
    layout_store_given_axis(lycon, block, -1.0f, horizontal, true);
}

static float layout_padding_border_axis(ViewBlock* block, bool horizontal) {
    BoxMetrics metrics = layout_box_metrics(block);
    return horizontal ? metrics.pad_border_h : metrics.pad_border_v;
}

static float layout_content_from_border_axis(ViewBlock* block, float border_size,
                                             bool horizontal) {
    float content_size = border_size - layout_padding_border_axis(block, horizontal);
    return content_size > 0.0f ? content_size : 0.0f;
}

static float layout_border_from_content_axis(ViewBlock* block, float content_size,
                                             bool horizontal) {
    float clamped_content = content_size > 0.0f ? content_size : 0.0f;
    return clamped_content + layout_padding_border_axis(block, horizontal);
}

float layout_padding_border_width(ViewBlock* block) {
    return layout_padding_border_axis(block, true);
}

float layout_padding_border_height(ViewBlock* block) {
    return layout_padding_border_axis(block, false);
}

float layout_boundary_padding_border_axis(const BoundaryProp* bound, bool horizontal) {
    BoxMetrics metrics = layout_boundary_metrics(bound);
    return horizontal ? metrics.pad_border_h : metrics.pad_border_v;
}

float layout_content_width_from_border_box(ViewBlock* block, float border_width) {
    return layout_content_from_border_axis(block, border_width, true);
}

float layout_content_height_from_border_box(ViewBlock* block, float border_height) {
    return layout_content_from_border_axis(block, border_height, false);
}

float layout_border_width_from_content_box(ViewBlock* block, float content_width) {
    return layout_border_from_content_axis(block, content_width, true);
}

float layout_border_height_from_content_box(ViewBlock* block, float content_height) {
    return layout_border_from_content_axis(block, content_height, false);
}

float layout_boundary_content_size_from_border_box(const BoundaryProp* bound, float border_size, bool horizontal) {
    float content_size = border_size - layout_boundary_padding_border_axis(bound, horizontal);
    return content_size > 0 ? content_size : 0;
}

float layout_boundary_border_size_from_content_box(const BoundaryProp* bound, float content_size, bool horizontal) {
    float clamped_content_size = content_size > 0 ? content_size : 0;
    return clamped_content_size + layout_boundary_padding_border_axis(bound, horizontal);
}

float layout_content_size_from_border_box(ViewBlock* block, float border_size, bool horizontal) {
    return layout_content_from_border_axis(block, border_size, horizontal);
}

float layout_border_size_from_content_box(ViewBlock* block, float content_size, bool horizontal) {
    return layout_border_from_content_axis(block, content_size, horizontal);
}

float layout_css_size_to_content_box(const BoundaryProp* bound, CssEnum box_sizing, float css_size, bool horizontal) {
    return box_sizing == CSS_VALUE_BORDER_BOX
        ? layout_boundary_content_size_from_border_box(bound, css_size, horizontal)
        : css_size;
}

float layout_css_size_to_border_box(const BoundaryProp* bound, CssEnum box_sizing, float css_size, bool horizontal) {
    return box_sizing == CSS_VALUE_BORDER_BOX
        ? css_size
        : layout_boundary_border_size_from_content_box(bound, css_size, horizontal);
}

float layout_floor_border_box_width(ViewBlock* block, float border_width) {
    return layout_floor_border_box_axis(block, border_width, true);
}

float layout_floor_border_box_height(ViewBlock* block, float border_height) {
    return layout_floor_border_box_axis(block, border_height, false);
}

float layout_floor_border_box_axis(ViewBlock* block, float border_size, bool horizontal) {
    float floor = layout_padding_border_axis(block, horizontal);
    return border_size < floor ? floor : border_size;
}

static float layout_clamp_min_max_axis_value(ViewBlock* block, float size, bool horizontal) {
    if (!block || !block->blk) return size;

    BlockProp* prop = block->block_mut();
    float maximum = horizontal ? prop->given_max_width : prop->given_max_height;
    float minimum = horizontal ? prop->given_min_width : prop->given_min_height;
    float constrained = size;
    if (maximum >= 0.0f && constrained > maximum) {
        constrained = maximum;
        if (horizontal) log_debug("[LAYOUT_BOX] width clamped to max: %.2f", constrained);
    }
    // the minimum wins when declarations overlap, matching CSS used-value resolution.
    if (minimum >= 0.0f && constrained < minimum) {
        constrained = minimum;
        if (horizontal) log_debug("[LAYOUT_BOX] width clamped to min: %.2f", constrained);
    }
    return constrained;
}

float layout_clamp_min_max_width(ViewBlock* block, float width) {
    return layout_clamp_min_max_axis_value(block, width, true);
}

float layout_clamp_min_max_height(ViewBlock* block, float height) {
    return layout_clamp_min_max_axis_value(block, height, false);
}

static float layout_apply_min_max_axis_value(ViewBlock* block, float size,
                                              bool horizontal, bool size_is_border_box) {
    if (!block || !block->blk) return size;

    float constrained = size;
    if (size_is_border_box && !layout_uses_border_box(block)) {
        // convert border-box candidates before content-box min/max constraints.
        float content_size = layout_content_from_border_axis(block, size, horizontal);
        content_size = layout_clamp_min_max_axis_value(block, content_size, horizontal);
        constrained = layout_border_from_content_axis(block, content_size, horizontal);
    } else {
        constrained = layout_clamp_min_max_axis_value(block, size, horizontal);
    }
    if (size_is_border_box || layout_uses_border_box(block)) {
        float floor = layout_padding_border_axis(block, horizontal);
        if (constrained < floor) {
            if (horizontal) {
                log_debug("[LAYOUT_BOX] width border-box floor: %.2f -> %.2f (padding+border)",
                          constrained, floor);
            }
            constrained = floor;
        }
    }
    return constrained;
}

float layout_apply_min_max_width(ViewBlock* block, float width, bool width_is_border_box) {
    return layout_apply_min_max_axis_value(block, width, true, width_is_border_box);
}

float layout_apply_min_max_height(ViewBlock* block, float height, bool height_is_border_box) {
    return layout_apply_min_max_axis_value(block, height, false, height_is_border_box);
}

float layout_apply_min_max_axis(ViewBlock* block, float size, bool horizontal, bool size_is_border_box) {
    return layout_apply_min_max_axis_value(block, size, horizontal, size_is_border_box);
}

float adjust_min_max_width(ViewBlock* block, float width) {
    return layout_apply_min_max_width(block, width, false);
}

float adjust_min_max_height(ViewBlock* block, float height) {
    return layout_apply_min_max_height(block, height, false);
}

float adjust_border_padding_width(ViewBlock* block, float width) {
    return layout_content_width_from_border_box(block, width);
}

float adjust_border_padding_height(ViewBlock* block, float height) {
    return layout_content_height_from_border_box(block, height);
}
