#include "layout.hpp"

#include "../lib/tagged.hpp"
// Advanced constraint policy depends on formatting-context and writing-mode
// helpers, so it stays separate from primitive box geometry used by lightweight clients.
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

bool layout_parent_block_edge_is_unedged(ViewBlock* block,
                                         bool horizontal, bool start) {
    ViewElement* parent_view = block->parent_view();
    if (!parent_view || !parent_view->is_block()) return true;
    ViewBlock* parent = lam::view_require_block(parent_view);
    if (!parent->bound) return true;

    WritingMode writing_mode = layout_block_writing_mode(parent);
    bool use_right = horizontal &&
        (writing_mode == WM_VERTICAL_RL ? start : !start);
    bool use_bottom = !horizontal && !start;
    CssBoxSide side = horizontal
        ? (use_right ? CSS_BOX_SIDE_RIGHT : CSS_BOX_SIDE_LEFT)
        : (use_bottom ? CSS_BOX_SIDE_BOTTOM : CSS_BOX_SIDE_TOP);
    BoxMetrics box = layout_box_metrics(parent);
    return box.padding.values[side] <= 0.0f && box.border.values[side] <= 0.0f;
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
    if (layout_parent_block_edge_is_unedged(block, horizontal, true)) {
        *start_margin = 0.0f;
    }
    if (layout_parent_block_edge_is_unedged(block, horizontal, false)) {
        *end_margin = 0.0f;
    }
}

float layout_stretch_fit_border_box_size(ViewBlock* block, float available_margin_box_size,
                                         bool horizontal) {
    if (!block) return 0.0f;

    float start_margin = 0.0f;
    float end_margin = 0.0f;
    if (block->bound) {
        LayoutAxis axis = horizontal ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
        LayoutAxisRefs refs(block, axis);
        CssEnum* start_type = refs.margins.start_type;
        CssEnum* end_type = refs.margins.end_type;
        float* start_value = refs.margins.start;
        float* end_value = refs.margins.end;
        if (horizontal) {
            ViewElement* parent_view = block->parent_view();
            ViewBlock* parent = parent_view && parent_view->is_block()
                ? lam::view_require_block(parent_view) : nullptr;
            if (parent && layout_block_writing_mode(parent) == WM_VERTICAL_RL) {
                start_type = refs.margins.end_type;
                end_type = refs.margins.start_type;
                start_value = refs.margins.end;
                end_value = refs.margins.start;
            }
            if (start_type && *start_type != CSS_VALUE_AUTO) {
                ViewElement* parent_view = block->parent_view();
                ViewBlock* parent = parent_view && parent_view->is_block()
                    ? lam::view_require_block(parent_view) : nullptr;
                start_margin = layout_vertical_flow_block_start_margin(block,
                    parent ? layout_block_writing_mode(parent) : WM_VERTICAL_LR);
            }
            if (end_type && *end_type != CSS_VALUE_AUTO) {
                end_margin = layout_vertical_flow_block_end_margin(block,
                    parent ? layout_block_writing_mode(parent) : WM_VERTICAL_LR);
            }
        } else {
            if (start_type && *start_type != CSS_VALUE_AUTO) start_margin = *start_value;
            if (end_type && *end_type != CSS_VALUE_AUTO) end_margin = *end_value;
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
    float css_size = layout_used_css_size_from_border_box(block, border_size, horizontal);
    return layout_apply_min_max_axis(block, css_size, horizontal, false);
}

void layout_resolve_stretch_minmax_axis(ViewBlock* block, float available_margin_box_size,
                                        bool available_size_is_definite, bool horizontal) {
    if (!block || !block->blk) return;

    LayoutAxisRefs axis(block->blk, horizontal);
    float* minimum = axis.minimum;
    float* maximum = axis.maximum;
    CssEnum minimum_type = *axis.minimum_type;
    CssEnum maximum_type = *axis.maximum_type;

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
    float css_size = layout_used_css_size_from_border_box(block, border_size, horizontal);
    if (minimum_type == CSS_VALUE_STRETCH) *minimum = css_size;
    if (maximum_type == CSS_VALUE_STRETCH) *maximum = css_size;
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
    LayoutAxisPair<float> ratio = layout_axis_pair(*content_width, *content_height);
    LayoutAxisPair<float> minimum = {};
    LayoutAxisPair<float> maximum = {};
    LayoutAxisRefs constraints(block->block_mut(), LAYOUT_AXIS_X);
    minimum[LAYOUT_AXIS_X] = layout_aspect_ratio_constraint_size(
        block, *constraints.minimum, true, ratio_uses_border_box);
    maximum[LAYOUT_AXIS_X] = layout_aspect_ratio_constraint_size(
        block, *constraints.maximum, true, ratio_uses_border_box);
    constraints = LayoutAxisRefs(block->block_mut(), LAYOUT_AXIS_Y);
    minimum[LAYOUT_AXIS_Y] = layout_aspect_ratio_constraint_size(
        block, *constraints.minimum, false, ratio_uses_border_box);
    maximum[LAYOUT_AXIS_Y] = layout_aspect_ratio_constraint_size(
        block, *constraints.maximum, false, ratio_uses_border_box);
    for (LayoutAxis axis : layout_axes()) {
        bool horizontal = layout_axis_is_horizontal(axis);
        if (ratio_uses_border_box) {
            ratio[axis] = layout_border_size_from_content_box(block, ratio[axis], horizontal);
        }
    }

    LayoutAxisPair<float> source_minimum = minimum;
    for (LayoutAxis axis : layout_axes()) {
        LayoutAxis other = axis == LAYOUT_AXIS_X ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X;
        if (source_minimum[other] >= 0.0f) {
            float transferred = axis == LAYOUT_AXIS_X
                ? source_minimum[other] * aspect_ratio
                : source_minimum[other] / aspect_ratio;
            if (maximum[axis] >= 0.0f) transferred = min(transferred, maximum[axis]);
            minimum[axis] = max(minimum[axis], transferred);
        }
    }

    LayoutAxisPair<float> source_maximum = maximum;
    for (LayoutAxis axis : layout_axes()) {
        LayoutAxis other = axis == LAYOUT_AXIS_X ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X;
        if (source_maximum[other] >= 0.0f) {
            float transferred = axis == LAYOUT_AXIS_X
                ? source_maximum[other] * aspect_ratio
                : source_maximum[other] / aspect_ratio;
            transferred = max(transferred, minimum[axis]);
            maximum[axis] = maximum[axis] >= 0.0f
                ? min(maximum[axis], transferred) : transferred;
        }
    }

    for (LayoutAxis axis : layout_axes()) {
        if (maximum[axis] >= 0.0f) ratio[axis] = min(ratio[axis], maximum[axis]);
        if (minimum[axis] >= 0.0f) ratio[axis] = max(ratio[axis], minimum[axis]);
        if (ratio_uses_border_box) {
            ratio[axis] = layout_content_size_from_border_box(
                block, ratio[axis], layout_axis_is_horizontal(axis));
        }
    }
    *content_width = ratio[LAYOUT_AXIS_X];
    *content_height = ratio[LAYOUT_AXIS_Y];
}

float layout_apply_min_max_border_box_axis(ViewBlock* block, float border_size, bool horizontal,
                                           bool ignore_percentage_max) {
    if (!block || !block->blk) return border_size;

    LayoutAxisRefs axis(block->block_mut(), horizontal);
    float minimum = *axis.minimum;
    float maximum = *axis.maximum;
    if (ignore_percentage_max && horizontal && !isnan(*axis.maximum_percent)) {
        // A cyclic shrink-to-fit grid cannot use the provisional percentage max;
        // its intrinsic min-width contribution must win before the grid area exists.
        maximum = -1.0f;
    }
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
