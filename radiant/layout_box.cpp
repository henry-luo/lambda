#include "layout.hpp"

#include "../lib/log.h"

BoxMetrics layout_boundary_metrics(const BoundaryProp* bound) {
    BoxMetrics metrics = {};
    if (!bound) return metrics;

    metrics.margin = layout_boundary_margin_edges(bound);
    metrics.padding = layout_boundary_padding_edges(bound);
    metrics.border = layout_boundary_border_edges(bound);

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

LayoutContentBox layout_content_box(ViewBlock* block) {
    LayoutContentBox content = {};
    if (!block) return content;

    content.width = layout_content_size_from_border_box(block, block->width, true);
    content.height = layout_content_size_from_border_box(block, block->height, false);
    content.offset_x = layout_axis_decoration_start(block->bound, LAYOUT_AXIS_X);
    content.offset_y = layout_axis_decoration_start(block->bound, LAYOUT_AXIS_Y);
    return content;
}

void layout_store_given_axis(LayoutContext* lycon, ViewBlock* block, float size,
                             bool horizontal, bool reset_type) {
    if (!lycon || !block) return;
    LayoutAxis axis = horizontal ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
    LayoutAxisRefs context(&lycon->block, axis);
    if (context.given) *context.given = size;
    if (!block->blk) return;
    LayoutAxisRefs slots(block->block_mut(), axis);
    *slots.given = size;
    if (reset_type) {
        *slots.given_type = CSS_VALUE__UNDEF;
    }
}

void layout_clear_given_axis(LayoutContext* lycon, ViewBlock* block, bool horizontal) {
    layout_store_given_axis(lycon, block, -1.0f, horizontal, true);
}

float layout_padding_border_axis(ViewBlock* block, bool horizontal) {
    return layout_boundary_padding_border_axis(block ? block->bound : nullptr, horizontal);
}

float layout_boundary_padding_border_axis(const BoundaryProp* bound, bool horizontal) {
    return layout_axis_box_metrics(
        layout_boundary_metrics(bound), horizontal ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y).pad_border;
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
    return layout_boundary_content_size_from_border_box(
        block ? block->bound : nullptr, border_size, horizontal);
}

float layout_border_size_from_content_box(ViewBlock* block, float content_size, bool horizontal) {
    return layout_boundary_border_size_from_content_box(
        block ? block->bound : nullptr, content_size, horizontal);
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
    float floor = layout_padding_border_axis(block, horizontal);
    return border_size < floor ? floor : border_size;
}

float layout_ratio_transfer_axis(ViewBlock* block, float size, bool source_horizontal,
                                 float aspect_ratio, bool size_is_border_box,
                                 bool result_is_border_box, bool ratio_is_border_box) {
    if (aspect_ratio <= 0.0f) return 0.0f;
    float ratio_source = size;
    if (size_is_border_box != ratio_is_border_box) {
        ratio_source = size_is_border_box
            ? layout_content_size_from_border_box(block, size, source_horizontal)
            : layout_border_size_from_content_box(block, size, source_horizontal);
    }
    float ratio_target = source_horizontal
        ? ratio_source / aspect_ratio : ratio_source * aspect_ratio;
    if (result_is_border_box != ratio_is_border_box) {
        ratio_target = result_is_border_box
            ? layout_border_size_from_content_box(block, ratio_target, !source_horizontal)
            : layout_content_size_from_border_box(block, ratio_target, !source_horizontal);
    }
    return ratio_target;
}

float layout_clamp_min_max_axis(ViewBlock* block, float size, bool horizontal) {
    if (!block || !block->blk) return size;

    LayoutAxis axis = horizontal ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
    LayoutAxisRefs slots(block->block_mut(), axis);
    float maximum = *slots.maximum;
    float minimum = *slots.minimum;
    float constrained = size;
    if (maximum >= 0.0f && constrained > maximum) {
        constrained = maximum;
        log_debug("[LAYOUT_BOX] %s clamped to max: %.2f",
                  horizontal ? "width" : "height", constrained);
    }
    // the minimum wins when declarations overlap, matching CSS used-value resolution.
    if (minimum >= 0.0f && constrained < minimum) {
        constrained = minimum;
        log_debug("[LAYOUT_BOX] %s clamped to min: %.2f",
                  horizontal ? "width" : "height", constrained);
    }
    return constrained;
}

static float layout_apply_min_max_axis_value(ViewBlock* block, float size,
                                              bool horizontal, bool size_is_border_box) {
    if (!block || !block->blk) return size;

    float constrained = size;
    if (size_is_border_box && !layout_uses_border_box(block)) {
        // convert border-box candidates before content-box min/max constraints.
        float content_size = layout_boundary_content_size_from_border_box(
            block->bound, size, horizontal);
        content_size = layout_clamp_min_max_axis(block, content_size, horizontal);
        constrained = layout_boundary_border_size_from_content_box(
            block->bound, content_size, horizontal);
    } else {
        constrained = layout_clamp_min_max_axis(block, size, horizontal);
    }
    if (size_is_border_box || layout_uses_border_box(block)) {
        float floor = layout_padding_border_axis(block, horizontal);
        if (constrained < floor) {
            log_debug("[LAYOUT_BOX] %s border-box floor: %.2f -> %.2f (padding+border)",
                      horizontal ? "width" : "height", constrained, floor);
            constrained = floor;
        }
    }
    return constrained;
}

float layout_apply_min_max_axis(ViewBlock* block, float size, bool horizontal, bool size_is_border_box) {
    return layout_apply_min_max_axis_value(block, size, horizontal, size_is_border_box);
}
