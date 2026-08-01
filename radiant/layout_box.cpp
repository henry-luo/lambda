#include "layout.hpp"

#include "../lib/log.h"
#include "../lib/tagged.hpp"

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

static bool layout_stretch_fit_block_margins_can_adjoin(ViewBlock* block, bool horizontal) {
    ViewElement* parent_view = block ? block->parent_view() : nullptr;
    ViewBlock* parent = parent_view && parent_view->is_block()
        ? lam::view_require_block(parent_view) : nullptr;
    if (!block || !parent || horizontal != layout_block_inline_axis_is_vertical(parent) ||
        block->display.outer != CSS_VALUE_BLOCK ||
        (block->display.inner != CSS_VALUE_FLOW && block->display.inner != CSS_VALUE_FLOW_ROOT) ||
        (block->position && (block->positionp()->position == CSS_VALUE_ABSOLUTE ||
                             block->positionp()->position == CSS_VALUE_FIXED ||
                             element_has_float(block)))) {
        return false;
    }

    // flow-root and orthogonal writing only change the child; adjoining margins
    // are defined by the parent Block Layout context that contains its outer box.
    return !block_context_establishes_bfc(parent);
}

static bool layout_stretch_fit_parent_block_edge_is_unedged(ViewBlock* block,
                                                             bool horizontal, bool start) {
    ViewElement* parent_view = block->parent_view();
    if (!parent_view || !parent_view->is_block()) return true;
    ViewBlock* parent = lam::view_require_block(parent_view);
    if (!parent->bound) return true;

    WritingMode writing_mode = layout_block_writing_mode(parent);
    bool use_right = horizontal &&
        (writing_mode == WM_VERTICAL_RL ? start : !start);
    bool use_bottom = !horizontal && !start;
    float padding = horizontal
        ? (use_right ? parent->boundary()->padding.right : parent->boundary()->padding.left)
        : (use_bottom ? parent->boundary()->padding.bottom : parent->boundary()->padding.top);
    float border = 0.0f;
    if (parent->boundary()->border) {
        border = horizontal
            ? (use_right ? parent->boundary()->border->width.right
                         : parent->boundary()->border->width.left)
            : (use_bottom ? parent->boundary()->border->width.bottom
                          : parent->boundary()->border->width.top);
    }
    return padding <= 0.0f && border <= 0.0f;
}

static void layout_stretch_fit_zero_adjoining_block_margins(ViewBlock* block,
                                                             bool horizontal,
                                                             float* start_margin,
                                                             float* end_margin) {
    if (!start_margin || !end_margin ||
        !layout_stretch_fit_block_margins_can_adjoin(block, horizontal)) {
        return;
    }

    // adjoining block margins are already outside an unedged non-BFC parent,
    // so stretch-fit must not subtract them a second time from the used size.
    if (layout_stretch_fit_parent_block_edge_is_unedged(block, horizontal, true)) {
        *start_margin = 0.0f;
    }
    if (layout_stretch_fit_parent_block_edge_is_unedged(block, horizontal, false)) {
        *end_margin = 0.0f;
    }
}

float layout_stretch_fit_border_box_size(ViewBlock* block, float available_margin_box_size,
                                         bool horizontal) {
    if (!block) return 0.0f;

    float start_margin = 0.0f;
    float end_margin = 0.0f;
    if (block->bound) {
        if (horizontal) {
            if (block->boundary()->margin.left_type != CSS_VALUE_AUTO) {
                start_margin = block->boundary()->margin.left;
            }
            if (block->boundary()->margin.right_type != CSS_VALUE_AUTO) {
                end_margin = block->boundary()->margin.right;
            }
        } else {
            if (block->boundary()->margin.top_type != CSS_VALUE_AUTO) {
                start_margin = block->boundary()->margin.top;
            }
            if (block->boundary()->margin.bottom_type != CSS_VALUE_AUTO) {
                end_margin = block->boundary()->margin.bottom;
            }
        }
    }

    layout_stretch_fit_zero_adjoining_block_margins(
        block, horizontal, &start_margin, &end_margin);

    // Stretch-fit targets the margin box; auto margins are zero and the border
    // box cannot shrink below its padding and border.
    float border_size = available_margin_box_size - start_margin - end_margin;
    return layout_floor_border_box_axis(block, border_size, horizontal);
}

float layout_stretch_fit_used_css_size(ViewBlock* block, float available_margin_box_size,
                                       bool horizontal) {
    float border_size = layout_stretch_fit_border_box_size(
        block, available_margin_box_size, horizontal);
    float css_size = layout_uses_border_box(block)
        ? border_size
        : layout_content_size_from_border_box(block, border_size, horizontal);
    return layout_apply_min_max_axis(block, css_size, horizontal, false);
}

void layout_resolve_stretch_minmax_axis(ViewBlock* block, float available_margin_box_size,
                                        bool available_size_is_definite, bool horizontal) {
    if (!block || !block->blk) return;

    float* minimum = horizontal ? &block->blk->given_min_width
                                : &block->blk->given_min_height;
    float* maximum = horizontal ? &block->blk->given_max_width
                                : &block->blk->given_max_height;
    CssEnum minimum_type = horizontal ? block->blk->given_min_width_type
                                      : block->blk->given_min_height_type;
    CssEnum maximum_type = horizontal ? block->blk->given_max_width_type
                                      : block->blk->given_max_height_type;

    if (minimum_type != CSS_VALUE_STRETCH && maximum_type != CSS_VALUE_STRETCH) return;

    if (!available_size_is_definite) {
        // CSS Sizing 4 makes an indefinite stretch min-size zero and max-size
        // none; retain the keyword so a later definite reflow can resolve it.
        if (minimum_type == CSS_VALUE_STRETCH) *minimum = 0.0f;
        if (maximum_type == CSS_VALUE_STRETCH) *maximum = -1.0f;
        return;
    }

    float border_size = layout_stretch_fit_border_box_size(
        block, available_margin_box_size, horizontal);
    float css_size = layout_uses_border_box(block)
        ? border_size
        : layout_content_size_from_border_box(block, border_size, horizontal);
    if (minimum_type == CSS_VALUE_STRETCH) *minimum = css_size;
    if (maximum_type == CSS_VALUE_STRETCH) *maximum = css_size;
}

float layout_clamp_min_max_width(ViewBlock* block, float width) {
    if (!block || !block->blk) return width;

    float constrained_width = width;
    if (block->block()->given_max_width >= 0 && constrained_width > block->block()->given_max_width) {
        constrained_width = block->block()->given_max_width;
        log_debug("[LAYOUT_BOX] width clamped to max: %.2f", constrained_width);
    }
    // given_min_width overrides given_max_width if both are specified
    if (block->block()->given_min_width >= 0 && constrained_width < block->block()->given_min_width) {
        constrained_width = block->block()->given_min_width;
        log_debug("[LAYOUT_BOX] width clamped to min: %.2f", constrained_width);
    }
    return constrained_width;
}

float layout_clamp_min_max_height(ViewBlock* block, float height) {
    if (!block || !block->blk) return height;

    float constrained_height = height;
    if (block->block()->given_max_height >= 0 && constrained_height > block->block()->given_max_height) {
        constrained_height = block->block()->given_max_height;
    }
    // given_min_height overrides given_max_height if both are specified
    if (block->block()->given_min_height >= 0 && constrained_height < block->block()->given_min_height) {
        constrained_height = block->block()->given_min_height;
    }
    return constrained_height;
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

static float layout_aspect_ratio_constraint_size(ViewBlock* block,
                                                 float css_size, bool horizontal,
                                                 bool ratio_uses_border_box) {
    if (css_size < 0.0f) return -1.0f;
    return ratio_uses_border_box
        ? layout_css_size_to_border_box(block->bound, layout_box_sizing(block),
                                        css_size, horizontal)
        : layout_css_size_to_content_box(block->bound, layout_box_sizing(block),
                                         css_size, horizontal);
}

void layout_apply_aspect_ratio_min_max_constraints(ViewBlock* block, float aspect_ratio,
                                                   float* content_width, float* content_height) {
    if (!block || !block->blk || aspect_ratio <= 0.0f ||
        !content_width || !content_height) {
        return;
    }

    bool ratio_uses_border_box = !layout_aspect_ratio_uses_content_box(block) &&
        layout_uses_border_box(block);
    float ratio_width = ratio_uses_border_box
        ? layout_border_width_from_content_box(block, *content_width) : *content_width;
    float ratio_height = ratio_uses_border_box
        ? layout_border_height_from_content_box(block, *content_height) : *content_height;
    float min_width = layout_aspect_ratio_constraint_size(
        block, block->block()->given_min_width, true, ratio_uses_border_box);
    float max_width = layout_aspect_ratio_constraint_size(
        block, block->block()->given_max_width, true, ratio_uses_border_box);
    float min_height = layout_aspect_ratio_constraint_size(
        block, block->block()->given_min_height, false, ratio_uses_border_box);
    float max_height = layout_aspect_ratio_constraint_size(
        block, block->block()->given_max_height, false, ratio_uses_border_box);

    float source_min_width = min_width;
    float source_min_height = min_height;
    if (source_min_height >= 0.0f) {
        float transferred_min_width = source_min_height * aspect_ratio;
        if (max_width >= 0.0f) transferred_min_width = min(transferred_min_width, max_width);
        min_width = max(min_width, transferred_min_width);
    }
    if (source_min_width >= 0.0f) {
        float transferred_min_height = source_min_width / aspect_ratio;
        if (max_height >= 0.0f) transferred_min_height = min(transferred_min_height, max_height);
        min_height = max(min_height, transferred_min_height);
    }

    float source_max_width = max_width;
    float source_max_height = max_height;
    if (source_max_height >= 0.0f) {
        float transferred_max_width = max(source_max_height * aspect_ratio, min_width);
        max_width = max_width >= 0.0f ? min(max_width, transferred_max_width)
                                      : transferred_max_width;
    }
    if (source_max_width >= 0.0f) {
        float transferred_max_height = max(source_max_width / aspect_ratio, min_height);
        max_height = max_height >= 0.0f ? min(max_height, transferred_max_height)
                                        : transferred_max_height;
    }

    if (max_width >= 0.0f && ratio_width > max_width) ratio_width = max_width;
    if (min_width >= 0.0f && ratio_width < min_width) ratio_width = min_width;
    if (max_height >= 0.0f && ratio_height > max_height) ratio_height = max_height;
    if (min_height >= 0.0f && ratio_height < min_height) ratio_height = min_height;
    *content_width = ratio_uses_border_box
        ? layout_content_width_from_border_box(block, ratio_width) : ratio_width;
    *content_height = ratio_uses_border_box
        ? layout_content_height_from_border_box(block, ratio_height) : ratio_height;
}

float layout_apply_min_max_border_box_axis(ViewBlock* block, float border_size, bool horizontal) {
    if (!block || !block->blk) return border_size;

    float minimum = horizontal ? block->block()->given_min_width
                               : block->block()->given_min_height;
    float maximum = horizontal ? block->block()->given_max_width
                               : block->block()->given_max_height;
    float minimum_border = minimum >= 0.0f
        ? layout_css_size_to_border_box(block->bound, layout_box_sizing(block), minimum, horizontal)
        : -1.0f;
    float maximum_border = maximum >= 0.0f
        ? layout_css_size_to_border_box(block->bound, layout_box_sizing(block), maximum, horizontal)
        : -1.0f;

    if (maximum_border >= 0.0f && border_size > maximum_border) {
        border_size = maximum_border;
    }
    if (minimum_border >= 0.0f && border_size < minimum_border) {
        border_size = minimum_border;
    }
    // border-box callers cannot shrink a content-box item below its padding.
    return layout_floor_border_box_axis(block, border_size, horizontal);
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
