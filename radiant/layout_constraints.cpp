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
        if (horizontal) {
            ViewElement* parent_view = block->parent_view();
            ViewBlock* parent = parent_view && parent_view->is_block()
                ? lam::view_require_block(parent_view) : nullptr;
            WritingMode parent_mode = parent
                ? layout_block_writing_mode(parent) : WM_VERTICAL_LR;
            CssEnum start_type = parent_mode == WM_VERTICAL_RL
                ? block->boundary()->margin.right_type
                : block->boundary()->margin.left_type;
            CssEnum end_type = parent_mode == WM_VERTICAL_RL
                ? block->boundary()->margin.left_type
                : block->boundary()->margin.right_type;
            if (start_type != CSS_VALUE_AUTO) {
                start_margin = layout_vertical_flow_block_start_margin(block, parent_mode);
            }
            if (end_type != CSS_VALUE_AUTO) {
                end_margin = layout_vertical_flow_block_end_margin(block, parent_mode);
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

float layout_apply_min_max_border_box_axis(ViewBlock* block, float border_size, bool horizontal,
                                           bool ignore_percentage_max) {
    if (!block || !block->blk) return border_size;

    float minimum = horizontal ? block->block()->given_min_width
                               : block->block()->given_min_height;
    float maximum = horizontal ? block->block()->given_max_width
                               : block->block()->given_max_height;
    if (ignore_percentage_max && horizontal &&
        !isnan(block->block()->given_max_width_percent)) {
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
