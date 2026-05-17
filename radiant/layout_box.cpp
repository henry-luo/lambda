#include "layout_box.hpp"

#include "../lib/log.h"

BoxMetrics layout_box_metrics(ViewBlock* block) {
    BoxMetrics metrics = {};
    if (!block || !block->bound) return metrics;

    metrics.margin.left = block->bound->margin.left;
    metrics.margin.right = block->bound->margin.right;
    metrics.margin.top = block->bound->margin.top;
    metrics.margin.bottom = block->bound->margin.bottom;

    metrics.padding.left = block->bound->padding.left;
    metrics.padding.right = block->bound->padding.right;
    metrics.padding.top = block->bound->padding.top;
    metrics.padding.bottom = block->bound->padding.bottom;

    if (block->bound->border) {
        metrics.border.left = block->bound->border->width.left;
        metrics.border.right = block->bound->border->width.right;
        metrics.border.top = block->bound->border->width.top;
        metrics.border.bottom = block->bound->border->width.bottom;
    }

    metrics.padding_h = metrics.padding.left + metrics.padding.right;
    metrics.padding_v = metrics.padding.top + metrics.padding.bottom;
    metrics.border_h = metrics.border.left + metrics.border.right;
    metrics.border_v = metrics.border.top + metrics.border.bottom;
    metrics.pad_border_h = metrics.padding_h + metrics.border_h;
    metrics.pad_border_v = metrics.padding_v + metrics.border_v;
    return metrics;
}

float layout_padding_border_width(ViewBlock* block) {
    BoxMetrics metrics = layout_box_metrics(block);
    return metrics.pad_border_h;
}

float layout_padding_border_height(ViewBlock* block) {
    BoxMetrics metrics = layout_box_metrics(block);
    return metrics.pad_border_v;
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

float layout_floor_border_box_width(ViewBlock* block, float border_width) {
    float floor_width = layout_padding_border_width(block);
    return border_width < floor_width ? floor_width : border_width;
}

float layout_floor_border_box_height(ViewBlock* block, float border_height) {
    float floor_height = layout_padding_border_height(block);
    return border_height < floor_height ? floor_height : border_height;
}

float layout_apply_min_max_width(ViewBlock* block, float width, bool width_is_border_box) {
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

    if (width_is_border_box || block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
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

    float constrained_height = height;
    if (block->blk->given_max_height >= 0 && constrained_height > block->blk->given_max_height) {
        constrained_height = block->blk->given_max_height;
    }
    // given_min_height overrides given_max_height if both are specified
    if (block->blk->given_min_height >= 0 && constrained_height < block->blk->given_min_height) {
        constrained_height = block->blk->given_min_height;
    }

    if (height_is_border_box || block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
        BoxMetrics metrics = layout_box_metrics(block);
        if (constrained_height < metrics.pad_border_v) {
            constrained_height = metrics.pad_border_v;
        }
    }
    return constrained_height;
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
