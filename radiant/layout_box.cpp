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

float layout_padding_border_width(ViewBlock* block) {
    BoxMetrics metrics = layout_box_metrics(block);
    return metrics.pad_border_h;
}

float layout_padding_border_height(ViewBlock* block) {
    BoxMetrics metrics = layout_box_metrics(block);
    return metrics.pad_border_v;
}

float layout_boundary_padding_border_axis(const BoundaryProp* bound, bool horizontal) {
    BoxMetrics metrics = layout_boundary_metrics(bound);
    return horizontal ? metrics.pad_border_h : metrics.pad_border_v;
}

float layout_content_width_from_border_box(ViewBlock* block, float border_width) {
    BoxMetrics metrics = layout_box_metrics(block);
    float content_width = border_width - metrics.pad_border_h;
    return content_width > 0 ? content_width : 0;
}

float layout_content_height_from_border_box(ViewBlock* block, float border_height) {
    BoxMetrics metrics = layout_box_metrics(block);
    float content_height = border_height - metrics.pad_border_v;
    return content_height > 0 ? content_height : 0;
}

float layout_border_width_from_content_box(ViewBlock* block, float content_width) {
    BoxMetrics metrics = layout_box_metrics(block);
    float clamped_content_width = content_width > 0 ? content_width : 0;
    return clamped_content_width + metrics.pad_border_h;
}

float layout_border_height_from_content_box(ViewBlock* block, float content_height) {
    BoxMetrics metrics = layout_box_metrics(block);
    float clamped_content_height = content_height > 0 ? content_height : 0;
    return clamped_content_height + metrics.pad_border_v;
}

float layout_padding_border_axis(ViewBlock* block, bool horizontal) {
    return horizontal ? layout_padding_border_width(block) : layout_padding_border_height(block);
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

float layout_content_size_from_border_box(ViewBlock* block, float border_size, bool horizontal) {
    return horizontal
        ? layout_content_width_from_border_box(block, border_size)
        : layout_content_height_from_border_box(block, border_size);
}

float layout_border_size_from_content_box(ViewBlock* block, float content_size, bool horizontal) {
    return horizontal
        ? layout_border_width_from_content_box(block, content_size)
        : layout_border_height_from_content_box(block, content_size);
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
    float floor_width = layout_padding_border_width(block);
    return border_width < floor_width ? floor_width : border_width;
}

float layout_floor_border_box_height(ViewBlock* block, float border_height) {
    float floor_height = layout_padding_border_height(block);
    return border_height < floor_height ? floor_height : border_height;
}

float layout_floor_border_box_axis(ViewBlock* block, float border_size, bool horizontal) {
    return horizontal
        ? layout_floor_border_box_width(block, border_size)
        : layout_floor_border_box_height(block, border_size);
}

float layout_clamp_min_max_width(ViewBlock* block, float width) {
    if (!block || !block->blk) return width;

    float constrained_width = width;
    if (block->blk->given_max_width >= 0 && constrained_width > block->blk->given_max_width) {
        constrained_width = block->blk->given_max_width;
        log_debug("[LAYOUT_BOX] width clamped to max: %.2f", constrained_width);
    }
    // given_min_width overrides given_max_width if both are specified
    if (block->blk->given_min_width >= 0 && constrained_width < block->blk->given_min_width) {
        constrained_width = block->blk->given_min_width;
        log_debug("[LAYOUT_BOX] width clamped to min: %.2f", constrained_width);
    }
    return constrained_width;
}

float layout_clamp_min_max_height(ViewBlock* block, float height) {
    if (!block || !block->blk) return height;

    float constrained_height = height;
    if (block->blk->given_max_height >= 0 && constrained_height > block->blk->given_max_height) {
        constrained_height = block->blk->given_max_height;
    }
    // given_min_height overrides given_max_height if both are specified
    if (block->blk->given_min_height >= 0 && constrained_height < block->blk->given_min_height) {
        constrained_height = block->blk->given_min_height;
    }
    return constrained_height;
}

float layout_clamp_min_max_axis(ViewBlock* block, float size, bool horizontal) {
    return horizontal
        ? layout_clamp_min_max_width(block, size)
        : layout_clamp_min_max_height(block, size);
}

float layout_apply_min_max_width(ViewBlock* block, float width, bool width_is_border_box) {
    if (!block || !block->blk) return width;

    float constrained_width = layout_clamp_min_max_width(block, width);
    if (width_is_border_box || layout_uses_border_box(block)) {
        BoxMetrics metrics = layout_box_metrics(block);
        if (constrained_width < metrics.pad_border_h) {
            log_debug("[LAYOUT_BOX] width border-box floor: %.2f -> %.2f (padding+border)",
                      constrained_width, metrics.pad_border_h);
            constrained_width = metrics.pad_border_h;
        }
    }
    return constrained_width;
}

float layout_apply_min_max_height(ViewBlock* block, float height, bool height_is_border_box) {
    if (!block || !block->blk) return height;

    float constrained_height = layout_clamp_min_max_height(block, height);
    if (height_is_border_box || layout_uses_border_box(block)) {
        BoxMetrics metrics = layout_box_metrics(block);
        if (constrained_height < metrics.pad_border_v) {
            constrained_height = metrics.pad_border_v;
        }
    }
    return constrained_height;
}

float layout_apply_min_max_axis(ViewBlock* block, float size, bool horizontal, bool size_is_border_box) {
    return horizontal
        ? layout_apply_min_max_width(block, size, size_is_border_box)
        : layout_apply_min_max_height(block, size, size_is_border_box);
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
