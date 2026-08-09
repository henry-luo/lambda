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

static float layout_padding_border_axis(ViewBlock* block, bool horizontal) {
    BoxMetrics metrics = layout_box_metrics(block);
    return horizontal ? metrics.pad_border_h : metrics.pad_border_v;
}

float layout_boundary_padding_border_axis(const BoundaryProp* bound, bool horizontal) {
    BoxMetrics metrics = layout_boundary_metrics(bound);
    return horizontal ? metrics.pad_border_h : metrics.pad_border_v;
}

float layout_content_size_from_border_box(ViewBlock* block, float border_size, bool horizontal) {
    return layout_boundary_content_size_from_border_box(
        block ? block->bound : nullptr, border_size, horizontal);
}

float layout_border_size_from_content_box(ViewBlock* block, float content_size, bool horizontal) {
    return layout_boundary_border_size_from_content_box(
        block ? block->bound : nullptr, content_size, horizontal);
}

float layout_padding_border_width(ViewBlock* block) {
    return layout_padding_border_axis(block, true);
}

float layout_padding_border_height(ViewBlock* block) {
    return layout_padding_border_axis(block, false);
}

float layout_boundary_content_size_from_border_box(const BoundaryProp* bound, float border_size, bool horizontal) {
    BoxMetrics metrics = layout_boundary_metrics(bound);
    float padding_border = horizontal ? metrics.pad_border_h : metrics.pad_border_v;
    float content_size = border_size - padding_border;
    return content_size > 0 ? content_size : 0;
}

float layout_boundary_border_size_from_content_box(const BoundaryProp* bound, float content_size, bool horizontal) {
    BoxMetrics metrics = layout_boundary_metrics(bound);
    float padding_border = horizontal ? metrics.pad_border_h : metrics.pad_border_v;
    float clamped_content_size = content_size > 0 ? content_size : 0;
    return clamped_content_size + padding_border;
}

float layout_content_width_from_border_box(ViewBlock* block, float border_width) {
    return layout_content_size_from_border_box(block, border_width, true);
}

float layout_content_height_from_border_box(ViewBlock* block, float border_height) {
    return layout_content_size_from_border_box(block, border_height, false);
}

float layout_border_width_from_content_box(ViewBlock* block, float content_width) {
    return layout_border_size_from_content_box(block, content_width, true);
}

float layout_border_height_from_content_box(ViewBlock* block, float content_height) {
    return layout_border_size_from_content_box(block, content_height, false);
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

float layout_floor_border_box_axis(ViewBlock* block, float border_size, bool horizontal) {
    float floor_size = layout_padding_border_axis(block, horizontal);
    return border_size < floor_size ? floor_size : border_size;
}

float layout_floor_border_box_width(ViewBlock* block, float border_width) {
    return layout_floor_border_box_axis(block, border_width, true);
}

float layout_floor_border_box_height(ViewBlock* block, float border_height) {
    return layout_floor_border_box_axis(block, border_height, false);
}

static float layout_clamp_min_max_axis_impl(ViewBlock* block, float size, bool horizontal) {
    if (!block || !block->blk) return size;

    BlockProp* props = block->block_mut();
    float maximum = horizontal ? props->given_max_width : props->given_max_height;
    float minimum = horizontal ? props->given_min_width : props->given_min_height;
    float constrained_size = size;
    if (maximum >= 0 && constrained_size > maximum) {
        constrained_size = maximum;
        if (horizontal) {
            log_debug("[LAYOUT_BOX] width clamped to max: %.2f", constrained_size);
        }
    }
    // a larger min-size overrides max-size when both are specified.
    if (minimum >= 0 && constrained_size < minimum) {
        constrained_size = minimum;
        if (horizontal) {
            log_debug("[LAYOUT_BOX] width clamped to min: %.2f", constrained_size);
        }
    }
    return constrained_size;
}

float layout_clamp_min_max_width(ViewBlock* block, float width) {
    return layout_clamp_min_max_axis_impl(block, width, true);
}

float layout_clamp_min_max_height(ViewBlock* block, float height) {
    return layout_clamp_min_max_axis_impl(block, height, false);
}

static float layout_apply_min_max_axis_impl(ViewBlock* block, float size, bool horizontal,
                                             bool size_is_border_box) {
    if (!block || !block->blk) return size;

    float constrained_size = size;
    if (size_is_border_box && !layout_uses_border_box(block)) {
        // content-box min/max declarations must clamp content, not the candidate's border box.
        float content_size = layout_content_size_from_border_box(block, size, horizontal);
        content_size = layout_clamp_min_max_axis_impl(block, content_size, horizontal);
        constrained_size = layout_border_size_from_content_box(block, content_size, horizontal);
    } else {
        constrained_size = layout_clamp_min_max_axis_impl(block, size, horizontal);
    }
    if (size_is_border_box || layout_uses_border_box(block)) {
        float floor_size = layout_padding_border_axis(block, horizontal);
        if (constrained_size < floor_size) {
            if (horizontal) {
                log_debug("[LAYOUT_BOX] width border-box floor: %.2f -> %.2f (padding+border)",
                          constrained_size, floor_size);
            }
            constrained_size = floor_size;
        }
    }
    return constrained_size;
}

float layout_apply_min_max_width(ViewBlock* block, float width, bool width_is_border_box) {
    return layout_apply_min_max_axis_impl(block, width, true, width_is_border_box);
}

float layout_apply_min_max_height(ViewBlock* block, float height, bool height_is_border_box) {
    return layout_apply_min_max_axis_impl(block, height, false, height_is_border_box);
}

float layout_apply_min_max_axis(ViewBlock* block, float size, bool horizontal, bool size_is_border_box) {
    return layout_apply_min_max_axis_impl(block, size, horizontal, size_is_border_box);
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
