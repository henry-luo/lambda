#include "layout.hpp"
#include "../lib/log.h"
#include "../lib/tagged.hpp"
#include <float.h>
#include <math.h>

static bool multicol_has_vertical_inline_axis(ViewBlock* block);
static bool multicol_has_forced_break_descendant(View* view);
static bool multicol_has_out_of_flow_descendant(View* view);
static float multicol_normal_line_offset(float line_advance, float visual_height);
static void multicol_reposition_abs_children_for_fragmented_cb(
    LayoutContext* lycon, View* cb_view);
static MulticolFragmentPlacement multicol_place_fragment(
    float original_offset,
    float fragment_height,
    int column_count,
    float column_width,
    float column_gap,
    float row_gap
);

/**
 * CSS Multi-column Layout Implementation
 *
 * Multi-column layout creates a multi-column formatting context where content
 * flows from one column to the next. This implementation:
 *
 * 1. Creates pseudo-column boxes within the container
 * 2. Distributes block content across columns with balanced heights
 * 3. Handles column-span: all elements that span all columns
 * 4. Stores computed values for column rule rendering
 *
 * Limitations:
 * - Column breaks are at block boundaries only (no mid-paragraph breaks)
 * - Fragmentation properties (break-before/after) not yet implemented
 * - Column-fill: auto not fully implemented (requires height constraint)
 */

/**
 * Check if a block establishes a multi-column container
 */
bool is_multicol_container(ViewBlock* block) {
    if (!block->multicol_prop()) return false;
    // html rendering §15.5.3: normal buttons route authored children through
    // an anonymous flow-root content box, so the control is not their multicol context.
    if (block->tag() == MARKUP_NAME_BUTTON) return false;
    // Explicit column-count:1 still establishes a multicol context for spanners.
    return block->multicol_prop()->column_count > 0 ||
           block->multicol_prop()->column_width > 0 ||
           block->multicol_prop()->column_height_is_specified;
}

static float multicol_used_column_extent(ViewBlock* block) {
    if (!block || !block->multicol_prop()) return 0.0f;
    const MultiColumnProp* multicol = block->multicol_prop();
    int column_count = multicol->computed_used_column_count > 0
        ? multicol->computed_used_column_count
        : multicol->computed_column_count;
    if (column_count <= 0 || multicol->computed_column_width <= 0.0f) return 0.0f;

    float gap = multicol_column_gap(block);
    if (gap < 0.0f) gap = 0.0f;
    return column_count * multicol->computed_column_width +
        (column_count - 1) * gap;
}

float multicol_used_block_axis_extent(ViewBlock* block) {
    if (!block || !block->multicol_prop()) return 0.0f;
    const MultiColumnProp* multicol = block->multicol_prop();
    if (multicol_has_vertical_inline_axis(block) &&
        multicol->computed_block_axis_extent > 0.0f) {
        return multicol->computed_block_axis_extent;
    }
    return multicol_used_column_extent(block);
}

float multicol_empty_intrinsic_inline_size(ViewBlock* block) {
    if (!block || !block->multicol_prop() ||
        block->multicol_prop()->column_width <= 0.0f) {
        return 0.0f;
    }

    const MultiColumnProp* multicol = block->multicol_prop();
    int column_count = multicol->column_count > 0 ? multicol->column_count : 1;
    float gap = multicol->column_gap_is_normal
        ? multicol_normal_gap_size(block)
        : multicol->column_gap_is_percent ? 0.0f : multicol->column_gap;
    if (gap < 0.0f) gap = 0.0f;
    // Fixed columns are formatting-structure contributions, not child content;
    // an empty size-contained box retains their inline geometry without a fallback.
    return multicol->column_width * column_count + gap * (column_count - 1);
}

/**
 * Calculate actual column dimensions based on CSS Multi-column spec
 */
void calculate_multicol_dimensions(
    MultiColumnProp* multicol,
    float available_width,
    float normal_gap_size,
    int* out_column_count,
    float* out_column_width,
    float* out_gap
) {
    // CSS Multi-column §3.4: normal column-gap computes to 1em.
    if (normal_gap_size <= 0.0f) normal_gap_size = 16.0f;
    float gap = multicol->column_gap_is_normal ? normal_gap_size : multicol->column_gap;
    if (multicol->column_gap_is_percent) {
        // css gaps: percentages resolve against the content box at layout time.
        gap = multicol->column_gap * max(available_width, 0.0f) / 100.0f;
    }
    if (gap < 0) gap = 0;

    int column_count = multicol->column_count;  // 0 = auto
    float column_width = multicol->column_width; // 0 = auto
    // CSS Multi-column §3.4: Pseudo-algorithm for column layout
    if (column_count > 0 && column_width > 0) {
        // Both specified: use min of count and what fits
        int max_by_width = (int)floorf((available_width + gap) / (column_width + gap)); // INT_CAST_OK: integer column count
        column_count = min(column_count, max(1, max_by_width));
        // Recalculate width to fill available space
        column_width = (available_width - (column_count - 1) * gap) / column_count;
    }
    else if (column_count > 0) {
        // Only count specified: divide width evenly
        column_width = (available_width - (column_count - 1) * gap) / column_count;
    }
    else if (column_width > 0) {
        // Only width specified: fit as many as possible
        column_count = (int)floorf((available_width + gap) / (column_width + gap)); // INT_CAST_OK: integer column count
        column_count = max(1, column_count);
        // Recalculate width to fill available space
        column_width = (available_width - (column_count - 1) * gap) / column_count;
    }
    else {
        // Neither specified: single column
        column_count = 1;
        column_width = available_width;
    }
    // Ensure at least 1 column
    column_count = max(1, column_count);
    column_width = max(0.0f, column_width);

    *out_column_count = column_count;
    *out_column_width = column_width;
    *out_gap = gap;
}

struct InlineFragmentItem {
    View* view;
    TextRect* rect;
    float original_x;
    float original_y;
    float line_y;
    float height;
    int line_index;
    bool is_text;
    bool is_forced_break;
};

struct MulticolGridRowData {
    int row_count;
    float row_top[MAX_MULTICOL_BLOCKS];
    float row_bottom[MAX_MULTICOL_BLOCKS];
    bool row_seen[MAX_MULTICOL_BLOCKS];
};

static bool multicol_collect_grid_rows(
    ViewBlock* grid,
    MulticolGridRowData* rows
);
static float multicol_grid_min_fragmentainer_height(
    ViewBlock* grid,
    int column_count
);
static bool multicol_fragment_grid_rows(
    ViewBlock* container,
    ViewBlock* child,
    float item_height,
    float fragment_height,
    int column_count,
    float column_width,
    float column_gap,
    float initial_fragment_offset,
    int* out_used_columns,
    float* out_union_height
);

static float multicol_group_target_height(ViewBlock* block, float balanced_height, float group_total_height);
static float multicol_out_of_flow_balance_target(ViewBlock* block);
static bool multicol_has_direct_inline_wrapper(ViewBlock* block);
static bool multicol_is_scroll_container(ViewBlock* block);
static float multicol_definite_ancestor_fragmentainer_height(ViewBlock* block);
static bool multicol_has_definite_ancestor_fragmentainer(ViewBlock* block);
static void multicol_group_init(
    ColumnGroup* group,
    ViewBlock* container,
    float target_height,
    int column_count,
    float column_width,
    float gap,
    float inline_origin
);
static void multicol_cursor_init(FragmentedFlowCursor* cursor, ColumnGroup* group);
static bool multicol_group_should_break(
    ViewBlock* container,
    FragmentedFlowCursor* cursor,
    float item_height
);
static void multicol_cursor_advance_fragment(FragmentedFlowCursor* cursor);
static void multicol_group_ensure_fragment_count(
    FragmentedFlowCursor* cursor, int required_fragment_count);
static ColumnFragment* multicol_cursor_current_fragment(FragmentedFlowCursor* cursor);
static void multicol_group_record_fragment_count(
    ColumnGroup* group, int fragment_count);
static void multicol_cursor_place_block(
    FragmentedFlowCursor* cursor,
    ViewBlock* child,
    float group_y
);
static void multicol_fit_vertical_auto_inline_size(
    ColumnGroup* group, ViewBlock* child);
static void multicol_cursor_advance_block(FragmentedFlowCursor* cursor, float block_height);
static void multicol_cursor_advance_fragmented_block(
    FragmentedFlowCursor* cursor,
    float flow_height
);
static void multicol_group_finish(ColumnGroup* group, FragmentedFlowCursor* cursor);
static float multicol_balanced_target_search(
    ViewBlock* block,
    float* item_heights,
    bool* item_can_fragment,
    bool* break_before,
    bool* break_after,
    int item_count,
    int column_count,
    float fallback_target,
    float group_total_height,
    float* item_content_heights = nullptr,
    float* item_margin_before = nullptr,
    float* item_margin_after = nullptr,
    bool adjacent_to_spanner = false,
    ViewBlock* adjacent_item = nullptr
);
static bool multicol_group_wraps_rows(ViewBlock* container);
static bool multicol_uses_content_sized_wrapped_rows(ViewBlock* container);
static bool multicol_allows_overflow_columns(ViewBlock* container);
static bool multicol_uses_fixed_wrapped_rows(ViewBlock* container);
static bool multicol_uses_fixed_balanced_rows(ViewBlock* container);
static float multicol_specified_row_height(ViewBlock* container);
static void multicol_store_layout_fragments(
    ViewBlock* child,
    int fragment_count,
    int column_count,
    float fragment_height,
    float column_width,
    float column_gap,
    float row_gap,
    float fragment_visual_width,
    float initial_fragment_offset,
    bool zero_height_fragmentainer = false
);
static float multicol_stored_fragment_flow_height(
    ViewBlock* child,
    float fallback
);
static Pool* multicol_layout_fragment_pool(DomElement* elem);
static bool multicol_is_direct_br_node(DomNode* node);
static bool multicol_has_nested_overwide_block(
    View* view, float column_width, float fragment_height);
static bool multicol_has_nested_overwide_linebreak(
    View* view, float column_width);

struct MulticolNestedHorizontalSequence {
    float fragment_height;
    int parent_column_count;
    float parent_column_width;
    float parent_column_gap;
    float row_gap;
    int nested_column_count;
    float nested_column_width;
    float nested_column_gap;
    float initial_fragment_offset;
    bool parent_direction_rtl;
    bool nested_direction_rtl;
    bool wraps_rows;
};

template <typename FragmentFn>
static bool multicol_for_each_nested_horizontal_fragment(
    const MulticolNestedHorizontalSequence* sequence,
    float flow_start,
    float flow_extent,
    FragmentFn visit,
    int* out_fragment_count
);
static float multicol_nested_horizontal_unbreakable_start(
    const MulticolNestedHorizontalSequence* sequence,
    float flow_start,
    float flow_extent
);
static bool multicol_find_nested_horizontal_fragment(
    const MulticolNestedHorizontalSequence* sequence,
    float flow_start,
    float* out_x,
    float* out_y
);
static bool multicol_store_nested_horizontal_fragments(
    ViewBlock* child,
    const MulticolNestedHorizontalSequence* sequence,
    float flow_start,
    float flow_extent,
    float fragment_width,
    float* out_min_x,
    float* out_min_y,
    float* out_max_x,
    float* out_max_y,
    int* out_fragment_count
);
static bool multicol_init_nested_horizontal_sequence(
    ViewBlock* parent_block,
    ViewBlock* nested_multicol,
    float fragment_height,
    int parent_column_count,
    float parent_column_width,
    float parent_column_gap,
    float initial_fragment_offset,
    MulticolNestedHorizontalSequence* out_sequence
);
static bool multicol_fragment_abspos_in_context(
    LayoutContext* lycon, ViewBlock* containing_block, ViewBlock* child);
static int multicol_project_fragmented_descendants(
    LayoutContext* lycon,
    ViewBlock* child,
    float fragment_height,
    int column_count,
    float column_width,
    float column_gap,
    float block_split_height,
    float initial_fragment_offset,
    bool nested_descendants_local = false,
    const MulticolNestedHorizontalSequence* nested_horizontal_sequence = nullptr
);
static float multicol_text_box_trim_fragmented_flow_height(
    ViewBlock* child,
    float item_height,
    float fragment_height,
    int fragment_count,
    float initial_fragment_offset
);
// Forward declarations for layout functions
void layout_flow_node(LayoutContext* lycon, DomNode* node);
void layout_block_inner_content(LayoutContext* lycon, ViewBlock* block);
void line_break(LayoutContext* lycon);
void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, CssEnum outer_display);
void prescan_and_layout_floats(LayoutContext* lycon, DomNode* first_child, ViewBlock* block);
static bool multicol_is_single_column_height_only(ViewBlock* block);

static float multicol_content_box_height_limit(ViewBlock* block) {
    if (!block) return -1;

    bool vertical_writing = multicol_has_vertical_inline_axis(block);
    float limit = -1;
    const MultiColumnProp* multicol = block->multicol_prop();
    bool one_column_height_only = multicol_is_single_column_height_only(block);
    if (multicol && multicol->column_height_is_specified &&
        (block->blk || one_column_height_only ||
         (multicol->fill == COLUMN_FILL_AUTO &&
          !multicol_has_definite_ancestor_fragmentainer(block)) ||
         multicol_has_forced_break_descendant(block))) {
        // css multicol: an explicitly resolved column-height is definite even
        // when a single-column container has no separate block-style record.
        limit = multicol->column_height;
    } else if (!block->blk) {
        return -1;
    } else if (vertical_writing && block->block()->given_width >= 0) {
        // CSS Writing Modes maps a vertical multicolumn block extent to
        // physical x; using given_height here limits columns by inline size.
        limit = block->block()->given_width;
    } else if (!vertical_writing && block->block()->given_height >= 0) {
        limit = block->block()->given_height;
    } else if (vertical_writing && block->block()->given_max_width >= 0) {
        limit = block->block()->given_max_width;
    } else if (!vertical_writing && block->block()->given_max_height >= 0) {
        limit = block->block()->given_max_height;
    }

    if (limit < 0) return -1;

    if (block->bound && layout_uses_border_box(block)) {
        float border_padding = vertical_writing
            ? layout_box_metrics(block).pad_border_h
            : layout_box_metrics(block).pad_border_v;
        limit -= border_padding;
        if (limit < 0) limit = 0;
    }
    return limit;
}

static bool multicol_is_single_column_height_only(ViewBlock* block) {
    const MultiColumnProp* multicol = block ? block->multicol_prop() : nullptr;
    return multicol && !block->blk && multicol->column_height_is_specified &&
        multicol->column_count <= 1 && multicol->column_width <= 0.0f;
}

static float multicol_definite_ancestor_fragmentainer_height(ViewBlock* block) {
    if (!block) return -1.0f;
    for (ViewElement* ancestor = block->parent_view(); ancestor;
         ancestor = ancestor->parent_view()) {
        ViewBlock* ancestor_block = lam::view_as_block(ancestor);
        if (ancestor_block && is_multicol_container(ancestor_block) &&
            multicol_content_box_height_limit(ancestor_block) >= 0.0f) {
            return multicol_content_box_height_limit(ancestor_block);
        }
    }
    return -1.0f;
}

static bool multicol_has_definite_ancestor_fragmentainer(ViewBlock* block) {
    return multicol_definite_ancestor_fragmentainer_height(block) >= 0.0f;
}

static float multicol_content_box_inline_limit(ViewBlock* block) {
    if (!block || !block->blk) return -1;

    bool vertical_writing = multicol_has_vertical_inline_axis(block);
    float limit = -1;
    if (vertical_writing && block->block()->given_height >= 0) {
        limit = block->block()->given_height;
    } else if (!vertical_writing && block->block()->given_width >= 0) {
        limit = block->block()->given_width;
    } else if (vertical_writing && block->block()->given_max_height >= 0) {
        limit = block->block()->given_max_height;
    } else if (!vertical_writing && block->block()->given_max_width >= 0) {
        limit = block->block()->given_max_width;
    }

    if (limit < 0) return -1;
    if (block->bound && layout_uses_border_box(block)) {
        float border_padding = vertical_writing
            ? layout_box_metrics(block).pad_border_v
            : layout_box_metrics(block).pad_border_h;
        limit -= border_padding;
        if (limit < 0) limit = 0;
    }
    return limit;
}

static bool multicol_has_vertical_inline_axis(ViewBlock* block) {
    return block && layout_block_inline_axis_is_vertical(block);
}

static float multicol_vertical_rtl_inline_end_offset(
    ViewBlock* block, float fragment_width, float column_width) {
    if (!block || !block->blk || !multicol_has_vertical_inline_axis(block) ||
        block->block()->direction != CSS_VALUE_RTL ||
        layout_element_css_writing_mode(block->as_element()) == CSS_VALUE_SIDEWAYS_LR) {
        return 0.0f;
    }
    // CSS Writing Modes: place the occupied fragment at inline-end when the
    // inline progression is reversed, retaining the unused fragment space.
    return max(column_width - fragment_width, 0.0f);
}

static void multicol_flow_margins(ViewBlock* container, ViewBlock* child,
                                  float* out_before, float* out_after) {
    if (out_before) *out_before = 0.0f;
    if (out_after) *out_after = 0.0f;
    if (!container || !child) return;

    const Margin* margin = child->bound && child->boundary()->has_flow_margin
        ? &child->boundary()->flow_margin
        : (child->bound ? &child->boundary()->margin : nullptr);
    if (!margin) return;

    if (multicol_has_vertical_inline_axis(container)) {
        WritingMode mode = layout_block_writing_mode(container);
        if (out_before) *out_before = mode == WM_VERTICAL_RL ? margin->right : margin->left;
        if (out_after) *out_after = mode == WM_VERTICAL_RL ? margin->left : margin->right;
        return;
    }

    if (out_before) *out_before = margin->top;
    if (out_after) *out_after = margin->bottom;
}

static float multicol_outer_flow_extent(ViewBlock* container, ViewBlock* child,
                                        float extent) {
    if (!container || !child) return extent;
    float margin_before = 0.0f;
    float margin_after = 0.0f;
    multicol_flow_margins(container, child, &margin_before, &margin_after);
    return extent + margin_before + margin_after;
}

static float multicol_uncontained_trailing_margin(ViewBlock* container,
                                                  float flow_advance) {
    if (!container) return 0.0f;
    if (multicol_has_vertical_inline_axis(container)) return 0.0f;

    ViewBlock* last_block = nullptr;
    for (View* child = container->first_placed_child(); child;
         child = child->next()) {
        ViewBlock* child_block = lam::view_as_block(child);
        if (child_block && !layout_block_is_out_of_flow_positioned(child_block)) {
            last_block = child_block;
        }
    }
    if (!last_block) return 0.0f;

    float margin_after = 0.0f;
    multicol_flow_margins(container, last_block, nullptr, &margin_after);
    float child_end = last_block->y + max(last_block->height, 0.0f) - container->y;
    if (margin_after <= 0.0f || flow_advance > child_end + 0.5f) return 0.0f;
    return margin_after;
}

static void multicol_collect_in_flow_bounds(
    View* view, float* min_start, float* max_end, bool vertical_writing) {
    if (!view || !min_start || !max_end) return;
    if (ViewBlock* view_block = lam::view_as_block(view)) {
        if (layout_block_is_out_of_flow_positioned(view_block)) return;
        float start = vertical_writing ? view_block->x : view_block->y;
        float end = vertical_writing
            ? view_block->x + view_block->width
            : view_block->y + view_block->height;
        *min_start = min(*min_start, start);
        *max_end = max(*max_end, end);
    }
    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(view);
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->width > 0.0f && rect->height > 0.0f) {
                float start = vertical_writing ? rect->x : rect->y;
                float end = vertical_writing
                    ? rect->x + rect->width
                    : rect->y + rect->height;
                *min_start = min(*min_start, start);
                *max_end = max(*max_end, end);
            }
        }
    }
    if (!view->is_element()) return;
    for (View* descendant = lam::view_require_element(view)->first_placed_child();
         descendant; descendant = descendant->next()) {
        multicol_collect_in_flow_bounds(
            descendant, min_start, max_end, vertical_writing);
    }
}

static float multicol_in_flow_descendant_extent(ViewBlock* child) {
    if (!child) return 0.0f;
    bool vertical_writing = multicol_has_vertical_inline_axis(child);
    float min_start = vertical_writing ? child->x : child->y;
    float max_end = vertical_writing
        ? child->x + child->width : child->y + child->height;
    for (View* descendant = child->first_placed_child();
         descendant; descendant = descendant->next()) {
        multicol_collect_in_flow_bounds(
            descendant, &min_start, &max_end, vertical_writing);
    }
    if (!vertical_writing) return max(max_end - child->y, 0.0f);
    // css writing modes: block overflow in vertical writing is measured on x;
    // retain overflow on either side of the block-start edge.
    return max(max_end - child->x, child->x + child->width - min_start);
}

struct MulticolOutOfFlowBox {
    ViewBlock* block;
    float start;
    float end;
};

static bool multicol_contains_block(ViewBlock* ancestor, ViewBlock* block) {
    if (!ancestor || !block) return false;
    if (ancestor == block) return true;
    for (ViewElement* parent = block->parent_view(); parent;
         parent = parent->parent_view()) {
        if (!parent->is_block()) continue;
        if (lam::view_require_block(parent) == ancestor) return true;
    }
    return false;
}

static void multicol_collect_out_of_flow_boxes(
    View* view,
    ViewBlock* multicol,
    MulticolOutOfFlowBox* boxes,
    int* box_count,
    float origin_y
) {
    if (!view || !boxes || !box_count || *box_count >= MAX_MULTICOL_BLOCKS) {
        return;
    }
    if (ViewBlock* view_block = lam::view_as_block(view)) {
        if (view_block->position &&
            view_block->positionp()->position == CSS_VALUE_ABSOLUTE) {
            ViewBlock* containing_block = find_positioned_containing_block(
                lam::view_require_element(view_block));
            if (!multicol_contains_block(multicol, containing_block)) {
                return;
            }
            float start = view_block->y - origin_y;
            boxes[*box_count].block = view_block;
            boxes[*box_count].start = start;
            boxes[*box_count].end = start + max(view_block->height, 0.0f);
            (*box_count)++;
            return;
        }
    }
    if (!view->is_element()) return;
    for (View* descendant = lam::view_require_element(view)->first_placed_child();
         descendant && *box_count < MAX_MULTICOL_BLOCKS;
         descendant = descendant->next()) {
        multicol_collect_out_of_flow_boxes(
            descendant, multicol, boxes, box_count, origin_y);
    }
}

static float multicol_out_of_flow_balance_target(ViewBlock* block) {
    if (!block || !block->multicol_prop() ||
        block->multicol_prop()->fill != COLUMN_FILL_BALANCE ||
        block->multicol_prop()->column_height_is_specified ||
        multicol_content_box_height_limit(block) >= 0.0f ||
        multicol_has_vertical_inline_axis(block)) {
        return 0.0f;
    }
    int column_count = block->multicol_prop()->computed_column_count > 0
        ? block->multicol_prop()->computed_column_count
        : block->multicol_prop()->column_count;
    if (column_count <= 1) return 0.0f;

    MulticolOutOfFlowBox boxes[MAX_MULTICOL_BLOCKS];
    int box_count = 0;
    float origin_y = block->y + layout_axis_decoration_start(
        block->bound, LAYOUT_AXIS_Y);
    multicol_collect_out_of_flow_boxes(
        static_cast<View*>(block), block, boxes, &box_count, origin_y);
    if (box_count == 0) return 0.0f;

    float target = 1.0f;
    for (int index = 0; index < box_count; index++) {
        target = max(target, boxes[index].end - boxes[index].start);
    }
    for (int iteration = 0; iteration < MAX_MULTICOL_BLOCKS; iteration++) {
        bool changed = false;
        for (int index = 0; index < box_count; index++) {
            float start = max(boxes[index].start, 0.0f);
            int column = (int)floorf(start / target); // INT_CAST_OK: fragment index from non-negative offset
            if (column >= column_count) {
                float fitting_target = start / column_count + 0.5f;
                if (fitting_target > target) {
                    target = fitting_target;
                    changed = true;
                }
                continue;
            }
            float local_end = boxes[index].end - column * target;
            if (local_end > target + 0.5f) {
                float fitting_target = boxes[index].end / (column + 1);
                if (fitting_target <= target) fitting_target = target + 0.5f;
                target = fitting_target;
                changed = true;
            }
        }
        if (!changed) break;
    }
    return ceilf(max(target, 0.0f));
}

static void multicol_project_out_of_flow_descendants(
    ViewBlock* block,
    float target,
    int column_count,
    float column_width,
    float column_gap
) {
    if (!block || target <= 0.0f || column_count <= 1 ||
        multicol_has_vertical_inline_axis(block)) {
        return;
    }
    float origin_y = block->y + layout_axis_decoration_start(
        block->bound, LAYOUT_AXIS_Y);
    auto project = [&](auto&& project, View* view) -> void {
        if (!view) return;
        ViewBlock* view_block = lam::view_as_block(view);
        if (view_block && view_block->position &&
            view_block->positionp()->position == CSS_VALUE_ABSOLUTE) {
            ViewBlock* containing_block = find_positioned_containing_block(
                lam::view_require_element(view_block));
            if (!multicol_contains_block(block, containing_block)) return;
            float start = max(view_block->y - origin_y, 0.0f);
            int logical_column = (int)floorf(start / target); // INT_CAST_OK: fragment index from non-negative offset
            if (logical_column < column_count) {
                float offset_x = logical_column * (column_width + column_gap);
                float offset_y = -logical_column * target;
                if (offset_x != 0.0f || offset_y != 0.0f) {
                    // css positioning: descendants of an absolute box keep
                    // local coordinates when its containing-block offset changes.
                    view_block->x += offset_x;
                    view_block->y += offset_y;
                }
            }
            return;
        }
        if (!view->is_element()) return;
        for (View* descendant = lam::view_require_element(view)->first_placed_child();
             descendant; descendant = descendant->next()) {
            project(project, descendant);
        }
    };
    project(project, static_cast<View*>(block));
}

static bool multicol_has_in_flow_block_child(ViewBlock* parent) {
    if (!parent) return false;
    for (View* child = parent->first_placed_child(); child; child = child->next()) {
        if (ViewBlock* child_block = lam::view_as_block(child)) {
            if (!layout_block_is_out_of_flow_positioned(child_block) &&
                layout_view_is_block_flow_box(child_block)) {
                return true;
            }
            continue;
        }
        if (child->view_type == RDT_VIEW_INLINE &&
            multicol_has_in_flow_block_child(
                lam::unsafe_view_block_api_span(
                    lam::view_require<RDT_VIEW_INLINE>(child)))) {
            return true;
        }
    }
    return false;
}

static bool multicol_can_publish_vertical_child_geometry(ViewBlock* block) {
    if (!block || !multicol_has_vertical_inline_axis(block) ||
        !is_multicol_container(block)) return false;
    bool has_child = false;
    for (View* child = block->first_placed_child(); child;
         child = child->next()) {
        if (!child->view_type) continue;
        ViewBlock* child_block = lam::view_as_block(child);
        if (!child_block || layout_block_is_out_of_flow_positioned(child_block) ||
            child_block->first_child) {
            return false;
        }
        has_child = true;
    }
    return has_child;
}

static float multicol_child_flow_extent(ViewBlock* container, ViewBlock* child) {
    if (!container || !child) return 0.0f;
    float flow_extent = child->height;
    if (multicol_has_vertical_inline_axis(container)) {
        if (child->display.inner == CSS_VALUE_TABLE) {
            ViewTable* table = static_cast<ViewTable*>(child);
            if (table->tb && table->tb->vertical_flow_extent > 0.0f) {
                // css tables: the published box keeps its per-fragment
                // inline extent; logical table flow drives fragmentation.
                return multicol_outer_flow_extent(
                    container, child, table->tb->vertical_flow_extent);
            }
            // css tables: internal column and row tracks do not enlarge the
            // table's block-size contribution to its containing multicol.
            return multicol_outer_flow_extent(container, child, child->width);
        }
        flow_extent = child->width;
        float min_x = child->x;
        float max_x = child->x + child->width;
        auto collect_bounds = [&](auto&& collect, View* view) -> void {
            if (!view) return;
            if (ViewBlock* view_block = lam::view_as_block(view)) {
                if (layout_block_is_out_of_flow_positioned(view_block)) return;
                min_x = min(min_x, view_block->x);
                max_x = max(max_x, view_block->x + view_block->width);
            }
            if (!view->is_element()) return;
            for (View* descendant = lam::view_require_element(view)->first_placed_child();
                 descendant; descendant = descendant->next()) {
                collect(collect, descendant);
            }
        };
        for (View* descendant = child->first_placed_child(); descendant;
             descendant = descendant->next()) {
            collect_bounds(collect_bounds, descendant);
        }
        float block_start = layout_block_writing_mode(container) == WM_VERTICAL_RL
            ? child->x + child->width : child->x;
        float overflow_extent = layout_block_writing_mode(container) == WM_VERTICAL_RL
            ? max(block_start - min_x, max_x - child->x)
            : max(max_x - block_start, child->x + child->width - min_x);
        flow_extent = max(flow_extent, overflow_extent);
    } else if ((child->blk && child->block()->break_inside == CSS_VALUE_AVOID) ||
               (child->blk && container->multicol_prop() &&
                container->multicol_prop()->fill == COLUMN_FILL_BALANCE &&
                child->block()->given_height >= 0.0f &&
                !multicol_is_scroll_container(child))) {
        float descendant_extent = multicol_in_flow_descendant_extent(child);
        // css fragmentation: a fixed-size item's in-flow overflow contributes
        // to balancing, while avoid-break keeps that flow together.
        flow_extent = max(flow_extent, descendant_extent);
    }
    return multicol_outer_flow_extent(container, child, flow_extent);
}

static float multicol_specified_border_height(ViewBlock* block) {
    if (!block || !block->blk) return 0.0f;

    float specified_height = block->block()->given_height;
    // multicol finalization receives the CSS height value; preserve its box-sizing.
    return layout_css_size_to_border_box(block->bound, block->block()->box_sizing,
                                         specified_height, false);
}

static float multicol_used_total_height(ViewBlock* block, float flow_height,
                                        bool cap_auto_height) {
    if (block->blk && block->block_mut()->given_height >= 0) {
        return multicol_specified_border_height(block);
    }
    if (cap_auto_height && block->multicol_prop()->fill == COLUMN_FILL_AUTO) {
        float limit = multicol_content_box_height_limit(block);
        if (limit >= 0.0f) return min(flow_height, limit);
    }
    return flow_height + layout_box_metrics(block).pad_border_v;
}

static float multicol_row_gap(ViewBlock* block) {
    if (!block || !block->embed) return 0;
    if (block->embedp()->grid) return block->embedp()->grid->row_gap;
    if (block->embedp()->flex) return block->embedp()->flex->row_gap;
    return 0;
}

float multicol_normal_gap_size(ViewBlock* block) {
    if (block && block->font && block->fontp()->font_size > 0.0f) {
        return block->fontp()->font_size;
    }
    return 16.0f;
}

float multicol_column_gap(ViewBlock* block) {
    if (!block || !block->multicol_prop()) return 0.0f;
    const MultiColumnProp* multicol = block->multicol_prop();
    float gap = 0.0f;
    if (multicol->computed_column_count > 0) {
        gap = multicol->computed_column_gap;
    } else if (multicol->column_gap_is_normal) {
        gap = multicol_normal_gap_size(block);
    } else if (!multicol->column_gap_is_percent) {
        gap = multicol->column_gap;
    }
    return gap < 0.0f ? 0.0f : gap;
}


static bool multicol_uses_static_axis(ViewBlock* block, bool horizontal) {
    if (!block || !block->position) return false;
    return horizontal
        ? !block->positionp()->has_left && !block->positionp()->has_right
        : !block->positionp()->has_top && !block->positionp()->has_bottom;
}

static bool multicol_is_spanner_block(ViewBlock* block) {
    return block && block->multicol_prop() &&
           block->multicol_prop()->span == COLUMN_SPAN_ALL &&
           !layout_block_is_out_of_flow_positioned(block);
}

static bool multicol_follows_short_direct_spanner(
    ViewBlock* container, ViewBlock* child, float row_height) {
    if (!container || !child ||
        child->parent != static_cast<DomNode*>(container) ||
        row_height <= 0.0f) return false;
    ViewBlock* previous_block = nullptr;
    for (View* sibling = container->first_placed_child();
         sibling && sibling != static_cast<View*>(child);
         sibling = sibling->next()) {
        ViewBlock* sibling_block = lam::view_as_block(sibling);
        if (!sibling_block ||
            layout_block_is_out_of_flow_positioned(sibling_block)) continue;
        previous_block = sibling_block;
    }
    return previous_block && multicol_is_spanner_block(previous_block) &&
        previous_block->height <= row_height + 0.5f;
}

static void multicol_align_spanner_content(ViewBlock* spanner, float available_width) {
    if (!spanner || !spanner->blk || available_width <= 0.0f) return;
    CssEnum text_align = spanner->block()->text_align;
    if (text_align == CSS_VALUE_START) {
        text_align = spanner->block()->direction == CSS_VALUE_RTL
            ? CSS_VALUE_RIGHT : CSS_VALUE_LEFT;
    } else if (text_align == CSS_VALUE_END) {
        text_align = spanner->block()->direction == CSS_VALUE_RTL
            ? CSS_VALUE_LEFT : CSS_VALUE_RIGHT;
    }
    if (text_align != CSS_VALUE_CENTER && text_align != CSS_VALUE_RIGHT) return;
    BoxMetrics spanner_box = layout_box_metrics(spanner);
    float content_width = available_width - spanner_box.pad_border_h;
    layout_align_deferred_inline_line_runs(
        lam::view_require_element(spanner), max(content_width, 0.0f), text_align);
}

static void multicol_set_spanner_inline_size(ViewBlock* spanner, float width) {
    if (!spanner || width <= 0.0f) return;
    // css multicol: table spanners keep their table-used width while still
    // separating the surrounding column groups.
    if (spanner->view_type == RDT_VIEW_TABLE) return;
    if (multicol_has_vertical_inline_axis(spanner)) {
        spanner->height = width;
        spanner->content_height = layout_content_size_from_border_box(
            spanner, width, false);
    } else {
        spanner->width = width;
        multicol_align_spanner_content(spanner, width);
    }
}

static bool multicol_relayout_special_spanner_text(
    LayoutContext* lycon, ViewBlock* spanner) {
    if (!lycon || !spanner ||
        (spanner->tag() != MARKUP_NAME_FIELDSET &&
         spanner->tag() != MARKUP_NAME_DETAILS) || !spanner->bound) {
        return false;
    }

    bool has_direct_text = false;
    for (DomNode* child = spanner->first_child; child; child = child->next_sibling) {
        if (child->is_element()) return false;
        if (child->is_text() && layout_text_node_has_content(child)) {
            has_direct_text = true;
        }
    }
    if (!has_direct_text) return false;
    bool preserve_native_height = spanner->tag() == MARKUP_NAME_DETAILS;
    float original_height = spanner->height;

    LayoutContextScope context_scope(lycon);
    BlockContext containing = lycon->block;
    block_context_init(
        &lycon->block, spanner,
        lycon->doc && lycon->doc->view_tree ? lycon->doc->view_tree->prop_pool : nullptr);
    lycon->block.parent = &containing;
    lycon->elmt = static_cast<DomNode*>(spanner);
    lycon->view = static_cast<View*>(spanner);
    if (spanner->font) setup_font(lycon->ui_context, &lycon->font, spanner->font);
    setup_line_height(lycon, spanner);
    layout_setup_block_font_metrics(lycon);

    LayoutContentBox content = layout_content_box(spanner);
    float content_start_y = spanner->y + content.offset_y;
    lycon->block.content_width = content.width;
    lycon->block.content_height = content.height;
    lycon->block.given_width = content.width;
    lycon->block.given_height = -1.0f;
    lycon->block.advance_y = content_start_y;
    lycon->block.max_width = 0.0f;
    lycon->block.text_align = spanner->blk ? spanner->block()->text_align : CSS_VALUE_START;
    lycon->block.direction = spanner->blk ? spanner->block()->direction : CSS_VALUE_LTR;
    line_init(lycon, spanner->x + content.offset_x,
              spanner->x + content.offset_x + content.width);
    layout_block_inner_content(lycon, spanner);

    float used_content_height = lycon->block.advance_y - content_start_y;
    if (used_content_height < 0.0f) used_content_height = 0.0f;
    // html special-element flow layout publishes direct text at the completed
    // line edge; move the retained child back to the content origin.
    layout_shift_view_children(static_cast<View*>(spanner), 0.0f, -used_content_height);
    spanner->content_width = content.width;
    spanner->content_height = used_content_height;
    float relaid_height = layout_border_size_from_content_box(
        spanner, used_content_height, false);
    spanner->height = preserve_native_height ? max(original_height, relaid_height) : relaid_height;
    return true;
}

static bool multicol_forces_column_break(CssEnum value) {
    return value == CSS_VALUE_COLUMN ||
           value == CSS_VALUE_PAGE ||
           value == CSS_VALUE_LEFT ||
           value == CSS_VALUE_RIGHT;
}

static bool multicol_avoids_column_break(CssEnum value) {
    return value == CSS_VALUE_AVOID;
}

static bool multicol_has_forced_break_descendant(View* view) {
    if (!view) return false;
    if (ViewBlock* block = lam::view_as_block(view)) {
        if (block->blk && (multicol_forces_column_break(block->block()->break_before) ||
                           multicol_forces_column_break(block->block()->break_after))) {
            return true;
        }
    }
    if (!view->is_element()) return false;
    for (View* child = lam::view_require_element(view)->first_placed_child();
         child; child = child->next()) {
        if (!child->view_type) continue;
        if (multicol_has_forced_break_descendant(child)) return true;
    }
    return false;
}

static bool multicol_clear_matches_float(ViewBlock* clear_block,
                                         ViewBlock* float_block) {
    if (!clear_block || !float_block || !clear_block->position ||
        !float_block->position) return false;
    CssEnum clear_value = clear_block->positionp()->clear;
    CssEnum float_value = float_block->positionp()->float_prop;
    if (float_value == CSS_VALUE_LEFT) {
        return clear_value == CSS_VALUE_LEFT || clear_value == CSS_VALUE_BOTH;
    }
    if (float_value == CSS_VALUE_RIGHT) {
        return clear_value == CSS_VALUE_RIGHT || clear_value == CSS_VALUE_BOTH;
    }
    return false;
}

static bool multicol_is_parallel_forced_flow(ViewBlock* block) {
    return block && block->position && layout_position_is_floated(block->position) &&
        multicol_has_forced_break_descendant(block);
}

static float multicol_parallel_forced_tail_extent(ViewBlock* float_block) {
    if (!multicol_is_parallel_forced_flow(float_block)) return 0.0f;

    float tail_extent = 0.0f;
    for (View* child = float_block->first_placed_child(); child; child = child->next()) {
        ViewBlock* child_block = lam::view_as_block(child);
        if (!child_block || layout_block_is_out_of_flow_positioned(child_block) ||
            !child_block->blk) continue;

        float child_offset = child_block->y - float_block->y;
        float candidate = 0.0f;
        if (multicol_forces_column_break(child_block->block()->break_before)) {
            candidate = float_block->height - child_offset;
        } else if (multicol_forces_column_break(child_block->block()->break_after)) {
            candidate = float_block->height - child_offset - child_block->height;
        }
        if (candidate > tail_extent) tail_extent = candidate;
    }
    return max(tail_extent, 0.0f);
}

static bool multicol_has_out_of_flow_descendant(View* view) {
    if (!view || !view->is_element()) return false;
    for (View* child = lam::view_require_element(view)->first_placed_child();
         child; child = child->next()) {
        if (layout_view_is_out_of_flow_positioned(child) ||
            multicol_has_out_of_flow_descendant(child)) {
            return true;
        }
    }
    return false;
}

bool multicol_spanner_can_escape_child(ViewBlock* child) {
    if (!child) return false;
    if (block_context_establishes_bfc(child)) return false;
    // css multicol: a spanner cannot escape an ancestor that establishes a
    // containing block for positioned descendants.
    if (child->blk && child->block()->contain_positioning) return false;
    if (child->transform && child->transformp()->functions) return false;
    if (child->filter && child->filterp()->functions) return false;
    if (child->embed && (child->embedp()->flex || child->embedp()->grid)) return false;
    if (child->view_type == RDT_VIEW_INLINE_BLOCK || child->view_type == RDT_VIEW_TABLE) return false;
    return true;
}

static bool multicol_has_direct_spanner_child(ViewBlock* child);

static bool multicol_has_escaping_spanner_in_flow(ViewBlock* container) {
    if (!container) return false;
    for (View* descendant = container->first_placed_child(); descendant;
         descendant = descendant->next()) {
        ViewBlock* descendant_block = lam::view_as_block(descendant);
        if (!descendant_block || layout_block_is_out_of_flow_positioned(descendant_block)) {
            continue;
        }
        if (multicol_is_spanner_block(descendant_block) ||
            (multicol_spanner_can_escape_child(descendant_block) &&
             multicol_has_direct_spanner_child(descendant_block))) {
            return true;
        }
    }
    return false;
}

static bool multicol_inline_contains_spanner(View* view) {
    for (View* descendant = view; descendant; descendant = descendant->next()) {
        if (ViewBlock* descendant_block = lam::view_as_block(descendant)) {
            if (multicol_is_spanner_block(descendant_block)) return true;
            if (multicol_spanner_can_escape_child(descendant_block) &&
                multicol_has_direct_spanner_child(descendant_block)) {
                return true;
            }
        } else if (descendant->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(descendant);
            if (multicol_inline_contains_spanner(span->first_child)) return true;
        }
    }
    return false;
}

static bool multicol_has_direct_spanner_child(ViewBlock* child) {
    if (!multicol_spanner_can_escape_child(child)) return false;

    View* descendant = child->first_placed_child();
    while (descendant) {
        if (ViewBlock* descendant_block = lam::view_as_block(descendant)) {
            if (multicol_is_spanner_block(descendant_block)) {
                return true;
            }
            // A nested block that preserves the parent formatting context does
            // not shield a deeper column spanner from this multicol container.
            if (multicol_spanner_can_escape_child(descendant_block) &&
                multicol_has_direct_spanner_child(descendant_block)) {
                return true;
            }
        } else if (descendant->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(descendant);
            if (multicol_inline_contains_spanner(span->first_child)) return true;
        }
        descendant = descendant->next();
    }
    return false;
}

static ViewBlock* multicol_find_escaping_spanner(View* first) {
    for (View* descendant = first; descendant; descendant = descendant->next()) {
        if (ViewBlock* descendant_block = lam::view_as_block(descendant)) {
            if (multicol_is_spanner_block(descendant_block)) return descendant_block;
            if (multicol_spanner_can_escape_child(descendant_block) &&
                multicol_has_direct_spanner_child(descendant_block)) {
                ViewBlock* spanner = multicol_find_escaping_spanner(
                    descendant_block->first_placed_child());
                if (spanner) return spanner;
            }
        } else if (descendant->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(descendant);
            ViewBlock* spanner = multicol_find_escaping_spanner(span->first_child);
            if (spanner) return spanner;
        }
    }
    return nullptr;
}

static void multicol_reanchor_br_only_block(
    LayoutContext* lycon, ViewBlock* block) {
    if (!lycon || !block) return;
    bool has_br = false;
    bool has_visible_text = false;
    for (View* child = block->first_placed_child(); child; child = child->next()) {
        if (child->view_type == RDT_VIEW_BR) {
            has_br = true;
        } else if (child->view_type == RDT_VIEW_TEXT &&
                   layout_dom_text_has_non_whitespace(
                       lam::dom_require<DOM_NODE_TEXT>(child))) {
            has_visible_text = true;
        }
    }
    if (!has_br || has_visible_text) return;

    float line_advance = 0.0f;
    if (block->blk && block->block()->line_height) {
        line_advance = layout_resolve_line_height_value(
            lycon, block->block()->line_height,
            lam::dom_require<DOM_NODE_ELEMENT>(block),
            block->fontp() ? block->fontp()->font_size : 16.0f);
    }
    if (line_advance <= 0.0f && block->fontp() && block->fontp()->font_handle) {
        line_advance = calc_normal_line_height(block->fontp()->font_handle);
    }
    for (View* child = block->first_placed_child(); child; child = child->next()) {
        if (child->view_type != RDT_VIEW_BR) continue;
        float line_offset = multicol_normal_line_offset(line_advance, child->height);
        child->y = line_offset;
    }
}

static bool multicol_has_visible_inline_content(View* first) {
    for (View* current = first; current; current = current->next()) {
        if (current->node_type == DOM_NODE_TEXT) {
            if (layout_dom_text_has_non_whitespace(
                    lam::dom_require<DOM_NODE_TEXT>(current))) {
                return true;
            }
            continue;
        }
        if (current->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(current);
            if (multicol_has_visible_inline_content(span->first_child)) return true;
            continue;
        }
        if (current->is_element() &&
            multicol_has_visible_inline_content(
                lam::view_require_element(current)->first_child)) {
            return true;
        }
    }
    return false;
}

static bool multicol_has_block_before_inline_run(ViewBlock* block) {
    if (!block) return false;

    bool saw_in_flow_block = false;
    bool saw_trailing_inline = false;
    for (View* child = block->first_placed_child(); child; child = child->next()) {
        ViewBlock* child_block = lam::view_as_block(child);
        if (child_block && child_block->view_type == RDT_VIEW_BLOCK &&
            layout_view_is_block_flow_box(child_block)) {
            if (layout_block_is_out_of_flow_positioned(child_block) ||
                multicol_is_spanner_block(child_block)) {
                continue;
            }
            if (saw_trailing_inline) return false;
            saw_in_flow_block = true;
            continue;
        }
        if (saw_in_flow_block &&
            (child->view_type == RDT_VIEW_INLINE || child->node_type == DOM_NODE_TEXT) &&
            multicol_has_visible_inline_content(child)) {
            saw_trailing_inline = true;
        }
    }
    return saw_in_flow_block && saw_trailing_inline;
}

static bool multicol_is_direct_br_node(DomNode* node) {
    return node && (node->view_type == RDT_VIEW_BR ||
        (node->node_type == DOM_NODE_ELEMENT &&
         node->as_element()->tag() == MARKUP_NAME_BR));
}

static bool multicol_has_nested_overwide_block(
    View* view, float column_width, float fragment_height
) {
    if (!view || !view->is_element() || column_width <= 0.0f ||
        fragment_height <= 0.0f) return false;
    for (DomNode* child_node = view->as_element()->first_child; child_node;
         child_node = child_node->next_sibling) {
        View* child = static_cast<View*>(child_node);
        if (!child->view_type) continue;
        ViewBlock* child_block = lam::view_as_block(child);
        if (!child_block || layout_block_is_out_of_flow_positioned(child_block)) {
            continue;
        }
        if (multicol_is_spanner_block(child_block)) return false;
        if (child_block->multicol_prop() &&
            is_multicol_container(child_block)) continue;
        if (child_block->width > column_width + 0.5f &&
            child_block->height <= fragment_height + 0.5f) {
            return true;
        }
        if (multicol_has_nested_overwide_block(
                child, column_width, fragment_height)) return true;
    }
    return false;
}

static bool multicol_has_nested_overwide_linebreak(
    View* view, float column_width
) {
    if (!view || !view->is_element() || column_width <= 0.0f) return false;

    View* previous_overwide = nullptr;
    for (DomNode* child_node = view->as_element()->first_child; child_node;
         child_node = child_node->next_sibling) {
        View* child = static_cast<View*>(child_node);
        if (multicol_is_direct_br_node(child)) {
            if (previous_overwide) return true;
            continue;
        }
        if (child->node_type == DOM_NODE_TEXT ||
            child->node_type == DOM_NODE_COMMENT) {
            continue;
        }
        if (child->view_type == RDT_VIEW_INLINE_BLOCK) {
            previous_overwide = child->width > column_width + 0.5f
                ? child : nullptr;
            continue;
        }
        if (ViewBlock* child_block = lam::view_as_block(child)) {
            if ((!child_block->multicol_prop() ||
                 !is_multicol_container(child_block)) &&
                multicol_has_nested_overwide_linebreak(
                    child, column_width)) {
                return true;
            }
        } else if (child->view_type == RDT_VIEW_INLINE &&
                   multicol_has_nested_overwide_linebreak(
                       child, column_width)) {
            return true;
        }
        previous_overwide = nullptr;
    }
    return false;
}

static float multicol_reanchor_trailing_br_after_flow(
    LayoutContext* lycon,
    ViewBlock* block,
    float flow_start,
    float fragment_height,
    int column_count,
    float column_width,
    float column_gap
) {
    if (!lycon || !block) return 0.0f;
    // css inline: a br can remain only in the direct DOM child chain, so the
    // trailing line box must be reanchored from source-order children.
    bool saw_spanner = false;
    bool saw_flow_box = false;
    int br_count = 0;
    View* first_br = nullptr;
    for (DomNode* child_node = block->first_child; child_node;
         child_node = child_node->next_sibling) {
        View* child = static_cast<View*>(child_node);
        if (ViewBlock* child_block = lam::view_as_block(child)) {
            if (multicol_is_spanner_block(child_block)) {
                saw_spanner = true;
                continue;
            }
            if (saw_spanner) return 0.0f;
            // css multicol: inline-block children keep inline fragmentation;
            // this repair is only for ordinary block-flow children.
            if (child->view_type == RDT_VIEW_INLINE_BLOCK) return 0.0f;
            if (!layout_view_is_block_flow_box(child)) return 0.0f;
            saw_flow_box = true;
            continue;
        }
        if (child->node_type == DOM_NODE_TEXT) {
            bool has_text = layout_dom_text_has_non_whitespace(
                lam::dom_require<DOM_NODE_TEXT>(child));
            if (has_text && (saw_spanner || saw_flow_box)) {
                return 0.0f;
            }
            continue;
        }
        if (child->view_type == RDT_VIEW_INLINE) {
            if (multicol_has_visible_inline_content(child) &&
                (saw_spanner || saw_flow_box)) {
                return 0.0f;
            }
            continue;
        }
        if (!multicol_is_direct_br_node(child)) return 0.0f;
        if (!first_br) first_br = child;
        br_count++;
    }
    if ((!saw_spanner && !saw_flow_box) || !first_br || br_count <= 0) {
        return 0.0f;
    }

    float line_advance = 0.0f;
    if (block->blk && block->block()->line_height) {
        line_advance = layout_resolve_line_height_value(
            lycon, block->block()->line_height,
            lam::dom_require<DOM_NODE_ELEMENT>(block),
            block->fontp() ? block->fontp()->font_size : 16.0f);
    }
    if (line_advance <= 0.0f && block->fontp() && block->fontp()->font_handle) {
        line_advance = calc_normal_line_height(block->fontp()->font_handle);
    }
    if (line_advance <= 0.0f) line_advance = first_br->height;
    float visual_height = first_br->height > 0.0f ? first_br->height : line_advance;

    if (!saw_spanner) {
        if (fragment_height <= 0.0f || column_count <= 0 || column_width <= 0.0f) {
            return 0.0f;
        }
        for (DomNode* child_node = block->first_child; child_node;
             child_node = child_node->next_sibling) {
            View* child = static_cast<View*>(child_node);
            if (!multicol_is_direct_br_node(child)) continue;
            float source_offset = child->y - block->y;
            if (source_offset < 0.0f) source_offset = 0.0f;
            MulticolFragmentPlacement placement = multicol_place_fragment(
                source_offset, fragment_height, column_count, column_width,
                column_gap, multicol_row_gap(block));
            child->x = placement.x_offset;
            child->y = block->y + placement.y_offset;
        }
        return 0.0f;
    }

    float line_offset = multicol_normal_line_offset(line_advance, visual_height);
    int line_index = 0;
    for (DomNode* child_node = block->first_child; child_node;
         child_node = child_node->next_sibling) {
        View* child = static_cast<View*>(child_node);
        if (!multicol_is_direct_br_node(child) || line_index >= br_count) continue;
        child->y = flow_start + line_offset + line_index * line_advance;
        line_index++;
    }
    return br_count * line_advance;
}

static bool multicol_has_visible_inline_content_before_spanner(View* first) {
    for (View* current = first; current; current = current->next()) {
        if (ViewBlock* current_block = lam::view_as_block(current)) {
            if (multicol_is_spanner_block(current_block)) continue;
            if (multicol_has_direct_spanner_child(current_block)) {
                if (multicol_has_visible_inline_content_before_spanner(
                        current_block->first_placed_child())) {
                    return true;
                }
                continue;
            }
            if (multicol_has_visible_inline_content(current)) return true;
            continue;
        }
        if (current->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(current);
            if (multicol_has_visible_inline_content_before_spanner(span->first_child)) {
                return true;
            }
            continue;
        }
        if (current->node_type == DOM_NODE_TEXT &&
            layout_dom_text_has_non_whitespace(
                lam::dom_require<DOM_NODE_TEXT>(current))) {
            return true;
        }
    }
    return false;
}

static bool multicol_requires_separate_spanner_group(ViewBlock* child) {
    if (!child || !multicol_has_direct_spanner_child(child)) return false;
    // css multicol §6: block-only spanner wrappers form their own column line;
    // ignore text that belongs to the spanner's own formatting context.
    return !multicol_has_visible_inline_content_before_spanner(
        child->first_placed_child());
}

static float multicol_spanner_prefix_flow_extent(ViewBlock* child) {
    if (!child || !child->bound) return 0.0f;

    LayoutAxis axis = multicol_has_vertical_inline_axis(child)
        ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
    float prefix_extent = layout_axis_decoration_start(child->boundary(), axis);
    for (View* descendant = child->first_placed_child(); descendant;
         descendant = descendant->next()) {
        ViewBlock* descendant_block = lam::view_as_block(descendant);
        if (!descendant_block) continue;
        if (layout_block_is_out_of_flow_positioned(descendant_block)) continue;
        if (multicol_is_spanner_block(descendant_block) ||
            multicol_requires_separate_spanner_group(descendant_block)) {
            break;
        }

        float margin_before = 0.0f;
        float margin_after = 0.0f;
        multicol_flow_margins(child, descendant_block,
                              &margin_before, &margin_after);
        float extent = axis == LAYOUT_AXIS_X
            ? descendant_block->width : descendant_block->height;
        prefix_extent += margin_before + max(extent, 0.0f) + margin_after;
    }
    return prefix_extent;
}

static bool multicol_has_spanner_child(ViewBlock* container) {
    if (!container) return false;
    for (View* child = container->first_placed_child(); child; child = child->next()) {
        ViewBlock* child_block = lam::view_as_block(child);
        if (child_block && multicol_is_spanner_block(child_block)) return true;
    }
    return false;
}

static bool multicol_has_nested_spanner_wrapper(ViewBlock* container) {
    if (!container) return false;
    for (View* child = container->first_placed_child(); child; child = child->next()) {
        ViewBlock* child_block = lam::view_as_block(child);
        if (child_block && !multicol_is_spanner_block(child_block) &&
            multicol_has_direct_spanner_child(child_block)) {
            // css multicol §6: a nested spanner behind a block wrapper keeps
            // the ancestor's child flow in the nested container's coordinates.
            return true;
        }
    }
    return false;
}

static bool multicol_uses_reverse_horizontal_columns(ViewBlock* block) {
    return block && block->blk && block->block()->direction == CSS_VALUE_RTL &&
        !layout_block_inline_axis_is_vertical(block);
}

static void multicol_mirror_rtl_horizontal_children(ViewBlock* block,
                                                     float content_origin_x,
                                                     float content_width) {
    if (!multicol_uses_reverse_horizontal_columns(block) || content_width <= 0.0f) return;

    for (View* child = block->first_placed_child(); child; child = child->next()) {
        // CSS Multi-column places horizontal-tb columns in the inline direction;
        // direction:rtl reverses that progression around the content box.
        float mirrored_x = content_origin_x + content_width -
            (child->x + child->width);
        // Descendant text rectangles are already local to this child; the view
        // serializer applies the child translation, so moving the subtree too
        // would translate those rectangles twice.
        child->x = mirrored_x;
    }
}

static void multicol_project_fragmented_text_rects(
    ViewText* text,
    float origin_y,
    float fragment_height,
    int column_count,
    float column_width,
    float column_gap,
    float row_gap
) {
    if (!text || fragment_height <= 0 || column_count <= 0) return;

    float min_x = 1e9f;
    float min_y = 1e9f;
    TextRect* rect = text->rect;
    while (rect) {
        float original_y = rect->y - origin_y;
        MulticolFragmentPlacement placement = multicol_place_fragment(
            original_y, fragment_height, column_count, column_width, column_gap, row_gap);

        rect->x += placement.x_offset;
        rect->y = origin_y + placement.y_offset;
        if (rect->x < min_x) min_x = rect->x;
        if (rect->y < min_y) min_y = rect->y;
        rect = rect->next;
    }

    if (min_x < 1e8f) text->x = min_x;
    if (min_y < 1e8f) text->y = min_y;
}

static void multicol_reanchor_text_descendants(View* view, float target_x, float target_y) {
    if (!view) return;

    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(view);
        LayoutTextRectBounds bounds = layout_text_rect_bounds(text->rect);
        if (bounds.valid) {
            layout_shift_view_tree(static_cast<View*>(text),
                target_x - bounds.min_x, target_y - bounds.min_y);
            adjust_text_bounds(text);
            text->x = target_x;
            text->y = target_y;
        }
        return;
    }

    if (view->node_type == DOM_NODE_ELEMENT) {
        View* child = lam::dom_require<DOM_NODE_ELEMENT>(view)->first_child;
        while (child) {
            multicol_reanchor_text_descendants(child, target_x, target_y);
            child = child->next_sibling;
        }
    }
}

static bool multicol_rebuild_flattened_inline_span_union(
    LayoutContext* lycon, ViewSpan* span, ViewBlock* containing_block) {
    // css 2.1 §9.2.1.1: projected atomic children size the inline wrapper by
    // their line boxes, while an escaping block spanner remains outside it.
    if (!lycon || !span || !containing_block || !containing_block->blk) return false;

    FontHandle* font_handle = span->font ? span->fontp()->font_handle :
        (containing_block->font ? containing_block->fontp()->font_handle : nullptr);
    float visual_height = font_handle ? font_get_cell_height(font_handle) : 0.0f;
    float line_advance = containing_block->block()->line_height
        ? layout_resolve_line_height_value(
            lycon, containing_block->block()->line_height,
            lam::dom_require<DOM_NODE_ELEMENT>(containing_block),
            containing_block->fontp() ? containing_block->fontp()->font_size : 16.0f)
        : 0.0f;
    if (line_advance <= 0.0f && font_handle) {
        line_advance = calc_normal_line_height(font_handle);
    }
    if (visual_height <= 0.0f) visual_height = line_advance;
    if (line_advance <= 0.0f || visual_height <= 0.0f) return false;

    bool found_inline_block = false;
    bool has_non_whitespace_text = false;
    float min_x = FLT_MAX;
    float max_x = -FLT_MAX;
    float min_y = FLT_MAX;
    float max_y = -FLT_MAX;
    auto collect = [&](auto&& collect, View* first) -> void {
        for (View* current = first; current; current = current->next()) {
            if (current->view_type == RDT_VIEW_INLINE) {
                ViewSpan* nested_span = lam::view_require<RDT_VIEW_INLINE>(current);
                collect(collect, nested_span->first_child);
                continue;
            }
            if (current->node_type == DOM_NODE_TEXT) {
                if (layout_dom_text_has_non_whitespace(
                        lam::dom_require<DOM_NODE_TEXT>(current))) {
                    has_non_whitespace_text = true;
                }
                continue;
            }
            if (current->view_type != RDT_VIEW_INLINE_BLOCK) continue;
            ViewBlock* inline_block = lam::view_as_block(current);
            if (!inline_block || layout_block_is_out_of_flow_positioned(inline_block) ||
                multicol_is_spanner_block(inline_block)) {
                continue;
            }
            found_inline_block = true;
            min_x = min(min_x, inline_block->x);
            max_x = max(max_x, inline_block->x + inline_block->width);
            min_y = min(min_y, inline_block->y);
            max_y = max(max_y, inline_block->y);
        }
    };
    collect(collect, span->first_child);
    if (!found_inline_block || has_non_whitespace_text) return false;

    float line_offset = multicol_normal_line_offset(line_advance, visual_height);
    layout_extend_fragment_union(
        span, FRAGMENT_UNION_SPLIT_INLINE, min_x, max_x,
        min_y + line_offset, max_y + line_offset + visual_height);
    return true;
}

static void multicol_finalize_text_for_fragmented_block(
    View* view, ViewBlock* fragment_owner, bool reset_split_inline_union = false) {
    if (!view || !fragment_owner) return;

    if (view->view_type == RDT_VIEW_TEXT) {
        // Fragment projection already stores rects in the union box's local
        // coordinate space; finalization must preserve their column offsets.
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(view);
        adjust_text_bounds(text);
        return;
    }

    if (view->node_type == DOM_NODE_ELEMENT) {
        View* child = lam::dom_require<DOM_NODE_ELEMENT>(view)->first_child;
        while (child) {
            multicol_finalize_text_for_fragmented_block(
                child, fragment_owner, reset_split_inline_union);
            child = child->next_sibling;
        }
        if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(view);
            if (reset_split_inline_union) {
                // the phase-one split union belongs to the old column-local
                // line layout and must be rebuilt from projected descendants.
                span->set_has_fragment_union(FRAGMENT_UNION_SPLIT_INLINE, false);
            }
            // Inline boxes derive their DOMRect from the projected child
            // fragments, including a continuation in another column.
            compute_span_bounding_box(
                span, inline_span_has_multiple_line_fragments(span), nullptr);
        }
    }
}

static void multicol_reanchor_direct_text(ViewBlock* block, float content_offset_y) {
    if (!block) return;
    (void)content_offset_y;

    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(block);
    if (elem->layout_fragment_list() && elem->layout_fragments_count() > 1) {
        multicol_finalize_text_for_fragmented_block(static_cast<View*>(block), block);
        return;
    }

    View* child = elem->first_child;
    while (child) {
        if (child->node_type == DOM_NODE_TEXT || child->node_type == DOM_NODE_ELEMENT) {
            multicol_reanchor_text_descendants(child, 0, 0);
        }
        child = child->next_sibling;
    }
}

static void multicol_reanchor_float_only_inline_span(ViewSpan* span);

static void multicol_finalize_fragmented_inline_continuations(View* view) {
    if (!view || view->node_type != DOM_NODE_ELEMENT) return;

    if (view->view_type == RDT_VIEW_INLINE) {
        multicol_reanchor_float_only_inline_span(
            lam::view_require<RDT_VIEW_INLINE>(view));
    }

    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(view);
    bool has_fragments = elem->layout_fragment_list() && elem->layout_fragments_count() > 1 && view->is_block();
    if (has_fragments) {
        multicol_finalize_text_for_fragmented_block(view, lam::view_require_block(view));
    }

    View* child = elem->first_child;
    while (child) {
        multicol_finalize_fragmented_inline_continuations(child);
        child = child->next_sibling;
    }
}

static ViewBlock* multicol_in_flow_block_sibling(View* start, bool next) {
    View* sibling = start ? (next ? start->next_sibling : start->prev_sibling) : nullptr;
    while (sibling) {
        if (sibling->is_element() && sibling->is_block()) {
            ViewBlock* block = lam::view_require_block(sibling);
            if (!layout_block_is_out_of_flow_positioned(block)) return block;
        }
        sibling = next ? sibling->next_sibling : sibling->prev_sibling;
    }
    return nullptr;
}

static void multicol_absolute_normal_origin(ViewBlock* block, float* out_x, float* out_y) {
    if (!block || !out_x || !out_y) return;

    float abs_x = block->x;
    float abs_y = block->y;
    ViewElement* parent = block->parent_view();
    while (parent) {
        if (parent->is_block()) {
            ViewBlock* parent_block = lam::view_require_block(parent);
            abs_x += parent_block->x;
            abs_y += parent_block->y;
            if (layout_block_is_out_of_flow_positioned(parent_block)) {
                break;
            }
        }
        parent = parent->parent_view();
    }

    *out_x = abs_x;
    *out_y = abs_y;
}

static LayoutFragmentBox* multicol_last_layout_fragment(ViewBlock* block) {
    if (!block || !block->is_element()) return nullptr;

    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(block);
    LayoutFragmentBox* fragment = elem->layout_fragment_list();
    LayoutFragmentBox* last = nullptr;
    while (fragment) {
        last = fragment;
        fragment = fragment->next;
    }
    return last;
}

static void multicol_apply_static_fragment_anchor(ViewBlock* multicol, ViewBlock* oof) {
    if (!multicol || !oof || !layout_block_is_out_of_flow_positioned(oof)) return;

    if (!multicol_uses_static_axis(oof, true) &&
        !multicol_uses_static_axis(oof, false)) return;
    ViewBlock* anchor = multicol_in_flow_block_sibling(static_cast<View*>(oof), true);
    bool anchor_is_next = anchor != nullptr;
    if (!anchor) {
        anchor = multicol_in_flow_block_sibling(static_cast<View*>(oof), false);
    }
    if (!anchor) return;

    float anchor_origin_x = 0;
    float anchor_origin_y = 0;
    ViewElement* parent = oof->parent_view();
    ViewBlock* positioned_containing_block = find_positioned_containing_block(
        lam::view_require_element(oof));
    bool local_to_fragmented_containing_block = positioned_containing_block &&
        positioned_containing_block->as_element()->layout_fragments_count() > 1;
    if (!local_to_fragmented_containing_block && parent && parent->is_block()) {
        multicol_absolute_normal_origin(lam::view_require_block(parent), &anchor_origin_x, &anchor_origin_y);
    } else {
        multicol_absolute_normal_origin(multicol, &anchor_origin_x, &anchor_origin_y);
    }
    if (local_to_fragmented_containing_block) {
        anchor_origin_x = 0.0f;
        anchor_origin_y = 0.0f;
    }

    if (multicol_uses_static_axis(oof, true)) {
        LayoutFragmentBox* last_fragment = anchor_is_next ? nullptr : multicol_last_layout_fragment(anchor);
        if (last_fragment) {
            oof->x = anchor_origin_x + anchor->x + last_fragment->x;
        } else {
            oof->x = anchor_origin_x + anchor->x;
        }
    }
    if (multicol_uses_static_axis(oof, false)) {
        if (anchor_is_next) {
            oof->y = anchor_origin_y + anchor->y;
        } else {
            float margin_bottom = anchor->bound ? anchor->boundary()->margin.bottom : 0;
            LayoutFragmentBox* last_fragment = multicol_last_layout_fragment(anchor);
            if (last_fragment) {
                oof->y = anchor_origin_y + anchor->y + last_fragment->y + last_fragment->height + margin_bottom;
            } else {
                oof->y = anchor_origin_y + anchor->y + anchor->height + margin_bottom;
            }
        }
    }

}

bool multicol_find_spanner_containing_block(
    ViewBlock* block,
    ViewBlock** out_containing_block
) {
    if (out_containing_block) *out_containing_block = nullptr;
    if (!block) return false;

    bool saw_spanner = false;
    for (ViewElement* ancestor = block->parent_view(); ancestor;
         ancestor = ancestor->parent_view()) {
        if (!ancestor->is_block()) continue;
        ViewBlock* ancestor_block = lam::view_require_block(ancestor);
        if (!saw_spanner && ancestor_block->position &&
            ancestor_block->positionp()->position != CSS_VALUE_STATIC) {
            return false;
        }
        if (multicol_is_spanner_block(ancestor_block)) {
            saw_spanner = true;
            continue;
        }
        if (saw_spanner && is_multicol_container(ancestor_block)) {
            ViewBlock* containing_block = nullptr;
            if (ancestor_block->position &&
                ancestor_block->positionp()->position != CSS_VALUE_STATIC) {
                containing_block = ancestor_block;
            } else {
                containing_block = find_positioned_containing_block(ancestor_block);
            }
            if (out_containing_block) *out_containing_block = containing_block;
            return true;
        }
    }
    return false;
}

static bool multicol_cb_is_bypassed_by_spanner(ViewBlock* cb, ViewBlock* child) {
    if (!cb || !child) return false;

    bool saw_spanner = false;
    bool passed_cb = false;
    for (ViewElement* ancestor = child->parent_view(); ancestor;
         ancestor = ancestor->parent_view()) {
        if (!ancestor->is_block()) continue;
        ViewBlock* ancestor_block = lam::view_require_block(ancestor);
        if (ancestor_block == cb) {
            passed_cb = true;
            continue;
        }
        if (!passed_cb && multicol_is_spanner_block(ancestor_block)) {
            saw_spanner = true;
        }
        if (passed_cb && is_multicol_container(ancestor_block)) {
            return saw_spanner;
        }
    }
    return false;
}

static void multicol_viewport_size(LayoutContext* lycon, ViewBlock* multicol, float* out_width, float* out_height) {
    if (!out_width || !out_height) return;

    float viewport_width = 0;
    float viewport_height = 0;
    if (lycon && lycon->ui_context) {
        viewport_width = lycon->ui_context->viewport_width;
        viewport_height = lycon->ui_context->viewport_height;
    }

    if ((viewport_width <= 0 || viewport_height <= 0) && multicol) {
        ViewBlock* root = multicol;
        while (root->parent_view() && root->parent_view()->is_block()) {
            root = lam::view_require_block(root->parent_view());
        }
        if (viewport_width <= 0) viewport_width = root->width;
        if (viewport_height <= 0) viewport_height = root->height;
    }

    *out_width = viewport_width;
    *out_height = viewport_height;
}

static bool multicol_apply_spanner_containing_block_anchor(
    LayoutContext* lycon,
    ViewBlock* multicol,
    ViewBlock* oof
) {
    if (!multicol || !oof || !layout_block_is_out_of_flow_positioned(oof)) return false;
    ViewBlock* containing_block = nullptr;
    if (!multicol_find_spanner_containing_block(oof, &containing_block)) return false;
    float containing_abs_x = 0;
    float containing_abs_y = 0;
    if (containing_block) {
        multicol_absolute_normal_origin(containing_block, &containing_abs_x, &containing_abs_y);
    }

    float viewport_width = 0;
    float viewport_height = 0;
    multicol_viewport_size(lycon, multicol, &viewport_width, &viewport_height);
    float containing_padding_x = 0.0f;
    float containing_padding_y = 0.0f;
    float containing_width = viewport_width;
    float containing_height = viewport_height;
    if (containing_block) {
        LayoutContainingBlock containing = layout_containing_block_for_view(containing_block);
        containing_padding_x = containing.padding_x;
        containing_padding_y = containing.padding_y;
        containing_width = containing.padding_width;
        containing_height = containing.padding_height;
    }

    bool changed = false;
    if (oof->positionp()->has_left) {
        oof->x = containing_padding_x + oof->positionp()->left - containing_abs_x;
        changed = true;
    } else if (oof->positionp()->has_right && containing_width > 0) {
        oof->x = containing_padding_x + containing_width -
            oof->positionp()->right - oof->width - containing_abs_x;
        changed = true;
    }

    if (oof->positionp()->has_top) {
        oof->y = containing_padding_y + oof->positionp()->top - containing_abs_y;
        changed = true;
    } else if (oof->positionp()->has_bottom && containing_height > 0) {
        oof->y = containing_padding_y + containing_height -
            oof->positionp()->bottom - oof->height - containing_abs_y;
        changed = true;
    }

    return changed;
}

static void multicol_apply_positioned_fragment_anchors_in_subtree(
    LayoutContext* lycon,
    ViewBlock* multicol,
    View* view
) {
    if (!view || !multicol) return;

    if (view != static_cast<View*>(multicol) && view->is_element() && view->is_block()) {
        ViewBlock* block = lam::view_require_block(view);
        if (layout_block_is_out_of_flow_positioned(block)) {
            // Fragment anchors belong to positioned descendants; reanchoring
            // the multicol root would replace its own containing-block position.
            multicol_apply_spanner_containing_block_anchor(lycon, multicol, block);
            multicol_apply_static_fragment_anchor(multicol, block);
            return;
        }
    }

    if (!view->is_element()) return;

    View* child = lam::dom_require<DOM_NODE_ELEMENT>(view)->first_child;
    while (child) {
        multicol_apply_positioned_fragment_anchors_in_subtree(lycon, multicol, child);
        child = child->next_sibling;
    }
}

static void multicol_apply_positioned_fragment_anchors(LayoutContext* lycon, ViewBlock* multicol) {
    if (!multicol || !multicol->multicol_prop()) return;

    multicol_apply_positioned_fragment_anchors_in_subtree(lycon, multicol, static_cast<View*>(multicol));
}

static float multicol_first_text_height(View* view) {
    if (!view) return 0;
    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(view);
        return text->height > 0 ? text->height : 0;
    }
    if (view->node_type == DOM_NODE_ELEMENT) {
        View* child = lam::dom_require<DOM_NODE_ELEMENT>(view)->first_child;
        while (child) {
            float height = multicol_first_text_height(child);
            if (height > 0) return height;
            child = child->next_sibling;
        }
    }
    return 0;
}

static float multicol_line_advance_from_items(InlineFragmentItem* items, int item_count) {
    float advance = -1;
    for (int i = 1; i < item_count; i++) {
        float delta = fabsf(items[i].original_y - items[i - 1].original_y);
        if (delta > 1.0f && (advance < 0 || delta < advance)) {
            advance = delta;
        }
    }
    if (advance > 0) return advance;

    for (int i = 0; i < item_count; i++) {
        if (items[i].height > 0) return items[i].height;
    }
    return 16.0f;
}

static bool multicol_is_ruby_annotation_text(View* view) {
    if (!view || view->view_type != RDT_VIEW_TEXT) return false;
    for (DomNode* ancestor = view->parent; ancestor; ancestor = ancestor->parent) {
        if (!ancestor->is_element()) continue;
        NameId tag = ancestor->as_element()->tag();
        if (tag == MARKUP_NAME_RT) return true;
        if (tag == MARKUP_NAME_RUBY) break;
    }
    return false;
}

template <typename Callback>
static void multicol_for_each_inline_leaf(View* view, Callback&& callback) {
    auto visit = [&](View* candidate) -> bool {
        if (multicol_is_ruby_annotation_text(candidate)) return false;
        if (candidate->view_type == RDT_VIEW_TEXT || candidate->view_type == RDT_VIEW_BR) {
            callback(candidate);
        }
        return false;
    };
    auto no_finish = [](View*) {};
    layout_walk_inline_views(view, visit, no_finish, false);
}

static float multicol_trailing_inline_flow_extent(ViewBlock* block, float flow_start_offset) {
    if (!block) return 0.0f;

    float first_line_y = FLT_MAX;
    float last_line_y = -FLT_MAX;
    float max_line_height = 0.0f;
    multicol_for_each_inline_leaf(block->first_placed_child(), [&](View* leaf) {
        if (leaf->view_type != RDT_VIEW_TEXT) return;
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(leaf);
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->width <= 0.0f || rect->height <= 0.0f ||
                layout_text_rect_content_kind(text, rect) ==
                    LAYOUT_TEXT_RECT_COLLAPSED_WHITESPACE) {
                continue;
            }
            first_line_y = min(first_line_y, rect->y);
            last_line_y = max(last_line_y, rect->y);
            max_line_height = max(max_line_height, rect->height);
        }
    });
    if (first_line_y == FLT_MAX || last_line_y == -FLT_MAX) return 0.0f;
    return flow_start_offset + (last_line_y - first_line_y) + max_line_height;
}

static bool multicol_find_single_inline_block(View* view, ViewBlock** out_block) {
    if (!view || !out_block) return true;
    if (view->view_type == RDT_VIEW_TEXT || view->view_type == RDT_VIEW_BR) return true;

    if (ViewBlock* block = lam::view_as_block(view)) {
        if (layout_block_is_out_of_flow_positioned(block)) return true;
        if (*out_block) return false;
        *out_block = block;
        return true;
    }
    if (view->view_type != RDT_VIEW_INLINE) return false;

    for (View* child = lam::view_require_element(view)->first_placed_child();
         child; child = child->next()) {
        if (!child->view_type) continue;
        if (!multicol_find_single_inline_block(child, out_block)) return false;
    }
    return true;
}

static bool multicol_inline_line_metrics(
    ViewBlock* child,
    int* out_line_count,
    float* out_line_advance,
    float* out_visual_height
) {
    if (!child || !out_line_count || !out_line_advance) return false;

    int item_count = 0;
    int line_count = 0;
    float current_line_y = 0.0f;
    float previous_item_y = 0.0f;
    float line_advance = -1.0f;
    float fallback_height = 0.0f;
    // CSS Writing Modes: vertical line boxes advance on physical x, not y.
    bool vertical_writing = multicol_has_vertical_inline_axis(child);
    bool forced_break_pending = false;
    auto include_line_item = [&](float item_y, float height, bool forces_break) {
        if (fallback_height <= 0.0f && height > 0.0f) fallback_height = height;
        if (item_count == 0) {
            current_line_y = item_y;
            line_count = 1;
        } else {
            float adjacent_delta = fabsf(item_y - previous_item_y);
            if (adjacent_delta > 1.0f &&
                (line_advance < 0.0f || adjacent_delta < line_advance)) {
                line_advance = adjacent_delta;
            }
            if (forced_break_pending || fabsf(item_y - current_line_y) > 1.0f) {
                line_count++;
                current_line_y = item_y;
                forced_break_pending = false;
            }
        }
        forced_break_pending = forces_break;
        previous_item_y = item_y;
        item_count++;
    };

    multicol_for_each_inline_leaf(child->first_placed_child(), [&](View* descendant) {
        if (descendant->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(descendant);
            TextRect* rect = text->rect;
            while (rect) {
                if (rect->width <= 0 && rect->length > 0) {
                    rect = rect->next;
                    continue;
                }

                include_line_item(
                    vertical_writing ? rect->x : rect->y,
                    vertical_writing ? rect->width : rect->height, false);
                rect = rect->next;
            }
        } else if (descendant->view_type == RDT_VIEW_BR) {
            include_line_item(
                vertical_writing ? descendant->x : descendant->y,
                vertical_writing ? descendant->width : descendant->height, true);
        }
    });

    if (item_count == 0 || line_count <= 0) return false;
    if (line_advance <= 0.0f) line_advance = fallback_height > 0.0f ? fallback_height : 16.0f;
    if (line_advance <= 0.0f) return false;

    *out_line_count = line_count;
    *out_line_advance = line_advance;
    if (out_visual_height) {
        *out_visual_height = fallback_height > 0.0f ? fallback_height : line_advance;
    }
    return true;
}

static bool multicol_has_fragmentable_line_boxes(ViewBlock* child) {
    if (child && child->multicol_prop() && is_multicol_container(child)) {
        // A nested multicol establishes its own fragmentation context; its
        // internal line boxes are not break opportunities for the ancestor.
        return false;
    }
    int line_count = 0;
    float line_advance = 0.0f;
    float visual_height = 0.0f;
    return multicol_inline_line_metrics(
        child, &line_count, &line_advance, &visual_height) && line_count > 1;
}

static bool multicol_find_fragmentable_line_metrics(
    ViewBlock* block, int* out_line_count, float* out_line_advance) {
    if (!block || !out_line_count || !out_line_advance) return false;
    int line_count = 0;
    float line_advance = 0.0f;
    if (multicol_inline_line_metrics(
            block, &line_count, &line_advance, nullptr) && line_count > 1) {
        *out_line_count = line_count;
        *out_line_advance = line_advance;
        return true;
    }
    if (block->multicol_prop() && is_multicol_container(block)) return false;

    bool found = false;
    for (View* child = block->first_placed_child(); child;
         child = child->next()) {
        ViewBlock* child_block = lam::view_as_block(child);
        if (!child_block || layout_block_is_out_of_flow_positioned(child_block) ||
            child_block->view_type == RDT_VIEW_MARKER) {
            continue;
        }
        int child_line_count = 0;
        float child_line_advance = 0.0f;
        if (!multicol_find_fragmentable_line_metrics(
                child_block, &child_line_count, &child_line_advance)) {
            continue;
        }
        if (!found || child_line_count > line_count) line_count = child_line_count;
        if (!found || child_line_advance > line_advance) {
            line_advance = child_line_advance;
        }
        found = true;
    }
    if (!found) return false;
    *out_line_count = line_count;
    *out_line_advance = line_advance;
    return true;
}

static bool multicol_is_contained_monolithic(ViewBlock* block);

static bool multicol_has_fragmentable_block_children(ViewBlock* child) {
    if (!child || (child->multicol_prop() && is_multicol_container(child))) {
        // a nested multicol owns its child fragmentation context.
        return false;
    }
    bool is_list_item = child->view_type == RDT_VIEW_LIST_ITEM;
    int in_flow_block_count = 0;
    bool all_blocks_size_contained = true;
    for (View* descendant = child->first_placed_child(); descendant;
         descendant = descendant->next()) {
        ViewBlock* descendant_block = lam::view_as_block(descendant);
        if (!descendant_block || layout_block_is_out_of_flow_positioned(descendant_block) ||
            descendant_block->view_type == RDT_VIEW_MARKER) {
            continue;
        }
        if (!layout_view_is_block_flow_box(descendant_block)) continue;
        in_flow_block_count++;
        if (!layout_block_has_size_containment_in_axis(descendant_block, false)) {
            all_blocks_size_contained = false;
        }
    }
    return is_list_item || (in_flow_block_count > 1 && all_blocks_size_contained);
}

static bool multicol_is_scroll_container(ViewBlock* block) {
    if (!block || !block->scroller) return false;
    const ScrollProp* scroll = block->scroll();
    if (!scroll) return false;
    auto establishes_scroll_container = [](CssEnum overflow) {
        // CSS Overflow: clip suppresses scrolling and does not establish a
        // scroll container; hidden remains programmatically scrollable.
        return overflow == CSS_VALUE_AUTO || overflow == CSS_VALUE_SCROLL ||
            overflow == CSS_VALUE_HIDDEN;
    };
    return establishes_scroll_container(scroll->overflow_x) ||
        establishes_scroll_container(scroll->overflow_y);
}

static bool multicol_has_unbreakable_scroll_children(ViewBlock* parent) {
    if (!parent) return false;
    int child_count = 0;
    for (View* child = parent->first_placed_child(); child;
         child = child->next()) {
        ViewBlock* child_block = lam::view_as_block(child);
        if (!child_block || layout_block_is_out_of_flow_positioned(child_block) ||
            child_block->view_type == RDT_VIEW_MARKER) {
            continue;
        }
        if (!layout_view_is_block_flow_box(child_block) ||
            !multicol_is_scroll_container(child_block)) {
            return false;
        }
        child_count++;
    }
    return child_count > 1;
}

static bool multicol_has_only_non_multicol_flow_children(ViewBlock* parent) {
    if (!parent) return false;
    bool has_child = false;
    for (View* child = parent->first_placed_child(); child;
         child = child->next()) {
        if (!child->view_type) continue;
        ViewBlock* child_block = lam::view_as_block(child);
        if (!child_block) return false;
        if (layout_block_is_out_of_flow_positioned(child_block)) continue;
        if (child_block->multicol_prop() &&
            is_multicol_container(child_block)) {
            return false;
        }
        has_child = true;
    }
    // css fragmentation: a nested multicol owns its inner fragmentainers;
    // only an ordinary direct flow may extend the parent sequence inline.
    return has_child;
}

static bool multicol_is_nested_in_balancing_context(ViewBlock* block) {
    if (!block) return false;
    ViewBlock* parent = lam::view_as_block(block->parent_view());
    return parent && parent != block && parent->multicol_prop() &&
        is_multicol_container(parent) &&
        parent->multicol_prop()->fill == COLUMN_FILL_BALANCE;
}

static bool multicol_is_contained_monolithic(ViewBlock* block) {
    if (!block || !block->blk) return false;
    bool can_fragment = multicol_has_fragmentable_line_boxes(block) ||
        multicol_has_fragmentable_block_children(block);
    return !can_fragment &&
        (layout_block_has_size_containment_in_axis(block, false) ||
         block->block()->contain_positioning || multicol_is_scroll_container(block));
}

static int multicol_contained_monolithic_child_count(ViewBlock* parent) {
    if (!parent) return 0;
    int count = 0;
    for (View* child = parent->first_placed_child(); child; child = child->next()) {
        ViewBlock* child_block = lam::view_as_block(child);
        if (child_block) {
            if (layout_block_is_out_of_flow_positioned(child_block) ||
                child_block->view_type == RDT_VIEW_MARKER) continue;
            if (!layout_view_is_block_flow_box(child_block) ||
                !multicol_is_contained_monolithic(child_block)) return 0;
            count++;
        } else if (child->node_type == DOM_NODE_TEXT) {
            bool non_whitespace = layout_dom_text_has_non_whitespace(
                lam::dom_require<DOM_NODE_TEXT>(child));
            if (non_whitespace) return 0;
        } else {
            return 0;
        }
    }
    return count;
}

struct MulticolMonolithicChildFlow {
    int fragment_index;
    float block_offset;
    float pending_margin_after;
    bool has_item;
};

static void multicol_init_monolithic_child_flow(
    MulticolMonolithicChildFlow* flow, float initial_offset,
    float fragment_height) {
    if (!flow) return;
    flow->fragment_index = 0;
    flow->block_offset = 0.0f;
    flow->pending_margin_after = 0.0f;
    flow->has_item = false;
    if (fragment_height <= 0.0f || initial_offset <= 0.0f) return;
    float normalized_offset = fmodf(initial_offset, fragment_height);
    flow->fragment_index = (int)floorf(initial_offset / fragment_height); // INT_CAST_OK: fragment index from positive offset
    flow->block_offset = normalized_offset;
    flow->has_item = normalized_offset > 0.0f;
}

static float multicol_monolithic_initial_offset(
    ViewBlock* child, float initial_offset) {
    float offset = max(initial_offset, 0.0f);
    if (!child) return offset;
    for (View* descendant = child->first_placed_child(); descendant;
         descendant = descendant->next()) {
        ViewBlock* descendant_block = lam::view_as_block(descendant);
        if (!descendant_block || layout_block_is_out_of_flow_positioned(descendant_block) ||
            descendant_block->view_type == RDT_VIEW_MARKER) {
            continue;
        }
        // css fragmentation: retain leading padding in the descendant flow
        // when the parent wrapper itself starts at a fragment offset.
        float local_offset = descendant_block->y - child->y;
        if (local_offset > 0.0f) offset += local_offset;
        break;
    }
    return offset;
}

static bool multicol_advance_monolithic_child_flow(
    ViewBlock* container, ViewBlock* child, float fragment_height,
    MulticolMonolithicChildFlow* flow, int* placed_fragment,
    float* placed_offset) {
    if (!container || !child || !flow || fragment_height <= 0.0f ||
        !multicol_is_contained_monolithic(child)) return false;
    bool break_before = child->blk &&
        multicol_forces_column_break(child->block()->break_before);
    if (break_before && flow->has_item && flow->block_offset > 0.0f) {
        flow->fragment_index++;
        flow->block_offset = 0.0f;
        flow->pending_margin_after = 0.0f;
        flow->has_item = false;
    }

    float margin_before = 0.0f;
    float margin_after = 0.0f;
    multicol_flow_margins(container, child, &margin_before, &margin_after);
    float content_height = max(child->height, 0.0f);
    float flow_height = (!flow->has_item
        ? (flow->fragment_index == 0 ? margin_before : 0.0f)
        : max(flow->pending_margin_after, margin_before)) + content_height;
    if (flow->has_item && flow->block_offset > 0.0f &&
        flow->block_offset + flow_height > fragment_height + 0.5f) {
        flow->fragment_index++;
        flow->block_offset = 0.0f;
        flow->pending_margin_after = 0.0f;
        flow->has_item = false;
        flow_height = content_height;
    }
    if (placed_fragment) *placed_fragment = flow->fragment_index;
    if (placed_offset) *placed_offset = flow->block_offset;
    flow->block_offset += flow_height;
    flow->pending_margin_after = margin_after;
    flow->has_item = true;
    if (child->blk &&
        multicol_forces_column_break(child->block()->break_after)) {
        flow->fragment_index++;
        flow->block_offset = 0.0f;
        flow->pending_margin_after = 0.0f;
        flow->has_item = false;
    }
    return true;
}

static ViewBlock* multicol_single_flow_item(ViewBlock* block, int item_count) {
    if (!block || item_count != 1) return nullptr;

    ViewBlock* item = nullptr;
    for (View* child = block->first_placed_child(); child; child = child->next()) {
        ViewBlock* child_block = lam::view_as_block(child);
        if (!child_block || layout_block_is_out_of_flow_positioned(child_block) ||
            multicol_is_spanner_block(child_block)) {
            continue;
        }
        if (item) return nullptr;
        item = child_block;
    }
    return item;
}

static ViewBlock* multicol_single_monolithic_item(
    ViewBlock* block, bool* item_can_fragment, int item_count) {
    if (!block || !item_can_fragment || item_count != 1 || item_can_fragment[0]) {
        return nullptr;
    }
    return multicol_single_flow_item(block, item_count);
}

static int multicol_subtree_size_containment_count(View* view, bool horizontal) {
    if (!view) return 0;
    if (ViewBlock* block = lam::view_as_block(view)) {
        if (layout_block_has_size_containment_in_axis(block, horizontal)) return 1;
    }
    if (!view->is_element()) return 0;
    int count = 0;
    for (View* child = lam::view_require_element(view)->first_placed_child();
         child; child = child->next()) {
        if (!child->view_type) continue;
        count += multicol_subtree_size_containment_count(child, horizontal);
        if (count > 1) return 2;
    }
    return count;
}

static bool multicol_has_single_size_contained_monolithic_item(
    ViewBlock* block, bool* item_can_fragment, int item_count) {
    ViewBlock* item = multicol_single_monolithic_item(block, item_can_fragment, item_count);
    return item && multicol_subtree_size_containment_count(static_cast<View*>(item), false) == 1;
}

static bool multicol_has_single_decoration_only_monolithic_item(
    ViewBlock* block, bool* item_can_fragment, int item_count) {
    ViewBlock* item = multicol_single_monolithic_item(block, item_can_fragment, item_count);
    if (!item) return false;
    float decoration_height = layout_box_metrics(item).pad_border_v;
    return decoration_height > 0.0f && item->height <= decoration_height;
}

static float multicol_decorated_child_min_fragmentainer(
    ViewBlock* block, bool* item_can_fragment, int item_count,
    float* item_margin_before) {
    ViewBlock* item = multicol_single_monolithic_item(block, item_can_fragment, item_count);
    if (!item || !item->bound) return 0.0f;

    LayoutAxis axis = multicol_has_vertical_inline_axis(item)
        ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
    if (layout_axis_has_given_size(item, axis == LAYOUT_AXIS_X)) return 0.0f;
    float leading_decoration = layout_axis_decoration_start(item->boundary(), axis);
    if (leading_decoration <= 0.0f) return 0.0f;

    for (View* child = item->first_placed_child(); child; child = child->next()) {
        if (!child->view_type || !child->is_block()) continue;
        ViewBlock* child_block = lam::view_as_block(child);
        if (!child_block || layout_block_is_out_of_flow_positioned(child_block) ||
            multicol_is_spanner_block(child_block)) {
            continue;
        }

        float child_margin_before = 0.0f;
        multicol_flow_margins(item, child_block,
                              &child_margin_before, nullptr);
        float child_extent = axis == LAYOUT_AXIS_X ? child_block->width : child_block->height;
        float min_fragmentainer = leading_decoration + child_margin_before +
            max(child_extent, 0.0f);
        if (item_margin_before) min_fragmentainer += item_margin_before[0];
        return min_fragmentainer;
    }
    return 0.0f;
}

static float multicol_fragmentable_item_min_fragmentainer(
    ViewBlock* container, bool* item_can_fragment, int item_count,
    float* item_margin_before) {
    if (!container || !item_can_fragment || item_count != 1 ||
        !item_can_fragment[0]) return 0.0f;

    ViewBlock* item = multicol_single_flow_item(container, item_count);
    if (!item || !multicol_has_fragmentable_block_children(item)) return 0.0f;

    bool vertical_writing = multicol_has_vertical_inline_axis(container);
    LayoutAxis axis = vertical_writing ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
    float leading_decoration = layout_axis_decoration_start(item->bound, axis);
    for (View* child = item->first_placed_child(); child; child = child->next()) {
        ViewBlock* child_block = lam::view_as_block(child);
        if (!child_block || layout_block_is_out_of_flow_positioned(child_block) ||
            child_block->view_type == RDT_VIEW_MARKER ||
            !layout_view_is_block_flow_box(child_block)) {
            continue;
        }
        if (multicol_has_fragmentable_line_boxes(child_block) ||
            multicol_has_fragmentable_block_children(child_block)) {
            return 0.0f;
        }
        float child_margin_before = 0.0f;
        multicol_flow_margins(item, child_block, &child_margin_before, nullptr);
        float child_extent = axis == LAYOUT_AXIS_X
            ? child_block->width : child_block->height;
        float item_margin = item_margin_before ? item_margin_before[0] : 0.0f;
        return item_margin + leading_decoration + child_margin_before +
            max(child_extent, 0.0f);
    }
    return 0.0f;
}

static float multicol_block_children_balance_floor(
    ViewBlock* parent, int column_count) {
    if (!parent || column_count <= 1 ||
        (!multicol_has_fragmentable_block_children(parent) &&
         !multicol_has_unbreakable_scroll_children(parent))) {
        return 0.0f;
    }

    int child_count = 0;
    float lower = 0.0f;
    float upper = 0.0f;
    for (View* child = parent->first_placed_child(); child;
         child = child->next()) {
        ViewBlock* child_block = lam::view_as_block(child);
        if (!child_block || layout_block_is_out_of_flow_positioned(child_block) ||
            child_block->view_type == RDT_VIEW_MARKER) {
            continue;
        }
        if (!layout_view_is_block_flow_box(child_block) ||
            multicol_has_fragmentable_line_boxes(child_block) ||
            multicol_has_fragmentable_block_children(child_block)) {
            return 0.0f;
        }
        float margin_before = 0.0f;
        float margin_after = 0.0f;
        multicol_flow_margins(parent, child_block,
                              &margin_before, &margin_after);
        float extent = multicol_has_vertical_inline_axis(parent)
            ? child_block->width : child_block->height;
        extent = max(extent, 0.0f);
        lower = max(lower, extent);
        upper += extent + margin_before + margin_after;
        child_count++;
    }
    if (child_count <= 1 || upper <= 0.0f) return 0.0f;

    auto fragment_count_for_target = [&](float target) {
        int fragment_count = 1;
        float fragment_used = 0.0f;
        float pending_margin_after = 0.0f;
        bool has_item = false;
        for (View* child = parent->first_placed_child(); child;
             child = child->next()) {
            ViewBlock* child_block = lam::view_as_block(child);
            if (!child_block || layout_block_is_out_of_flow_positioned(child_block) ||
                child_block->view_type == RDT_VIEW_MARKER) {
                continue;
            }
            float margin_before = 0.0f;
            float margin_after = 0.0f;
            multicol_flow_margins(parent, child_block,
                                  &margin_before, &margin_after);
            float extent = multicol_has_vertical_inline_axis(parent)
                ? child_block->width : child_block->height;
            extent = max(extent, 0.0f);
            bool break_before = child_block->blk &&
                multicol_forces_column_break(child_block->block()->break_before);
            if (break_before && has_item) {
                fragment_count++;
                fragment_used = 0.0f;
                pending_margin_after = 0.0f;
                has_item = false;
            }
            float margin = has_item
                ? max(pending_margin_after, margin_before)
                : (fragment_count == 1 ? margin_before : 0.0f);
            float needed = margin + extent;
            if (has_item && fragment_used + needed > target + 0.5f) {
                fragment_count++;
                fragment_used = extent;
                margin = 0.0f;
            } else if (!has_item && needed > target + 0.5f &&
                       margin > 0.0f && extent <= target + 0.5f) {
                fragment_count++;
                fragment_used = extent;
            } else {
                fragment_used += needed;
            }
            has_item = true;
            pending_margin_after = margin_after;

            bool has_next_block = false;
            for (View* next = child->next(); next; next = next->next()) {
                ViewBlock* next_block = lam::view_as_block(next);
                if (next_block && !layout_block_is_out_of_flow_positioned(next_block) &&
                    next_block->view_type != RDT_VIEW_MARKER) {
                    has_next_block = true;
                    break;
                }
            }
            bool break_after = child_block->blk && has_next_block &&
                multicol_forces_column_break(child_block->block()->break_after);
            if (break_after) {
                fragment_count++;
                fragment_used = 0.0f;
                pending_margin_after = 0.0f;
                has_item = false;
            }
        }
        return fragment_count;
    };

    float best = upper;
    for (int step = 0; step < 12; step++) {
        float mid = floorf((lower + upper) * 0.5f);
        if (mid <= 0.0f) mid = (lower + upper) * 0.5f;
        if (fragment_count_for_target(mid) <= column_count) {
            best = mid;
            upper = mid;
        } else {
            lower = mid + 1.0f;
        }
        if (upper <= lower) break;
    }
    return best;
}

static void multicol_init_flow_item(MulticolFlowItem* item,
                                    ViewBlock* container,
                                    ViewBlock* child,
                                    float height,
                                    float inline_offset,
                                    bool spans_all) {
    if (!item) return;
    item->block = child;
    item->height = height;
    item->balance_height = height;
    multicol_flow_margins(container, child,
                          &item->margin_before, &item->margin_after);
    if (child && child->blk && !multicol_has_vertical_inline_axis(container) &&
        child->block()->given_height >= 0.0f &&
        child->block()->break_inside != CSS_VALUE_AVOID &&
        height > child->height + item->margin_before + item->margin_after + 0.5f) {
        // css fragmentation: descendant overflow contributes to balancing,
        // while a fixed-size wrapper keeps its own fragment box size.
        item->height = child->height + item->margin_before + item->margin_after;
        item->balance_height = height;
    }
    item->content_height = item->height - item->margin_before - item->margin_after;
    if (item->content_height < 0.0f) item->content_height = 0.0f;
    item->inline_offset = inline_offset;
    // css fragmentation: a block container can break between in-flow block
    // children even when it has no multi-line inline content of its own.
    bool fixed_size_overflow = child && child->blk &&
        child->block()->given_height >= 0.0f &&
        height > child->height + item->margin_before + item->margin_after + 0.5f;
    int descendant_line_count = 0;
    float descendant_line_advance = 0.0f;
    bool has_descendant_fragmentable_lines = fixed_size_overflow &&
        multicol_find_fragmentable_line_metrics(
            child, &descendant_line_count, &descendant_line_advance);
    item->can_fragment = multicol_has_fragmentable_line_boxes(child) ||
        multicol_has_fragmentable_block_children(child) ||
        has_descendant_fragmentable_lines;
    item->spans_all = spans_all;
    bool direct_float = child && layout_position_is_floated(child->position) &&
        child->parent == static_cast<DomNode*>(container);
    item->parallel_flow = direct_float &&
        !multicol_has_forced_break_descendant(child);
    item->break_before_column = child && child->blk &&
        multicol_forces_column_break(child->block()->break_before);
    item->break_after_column = child && child->blk &&
        multicol_forces_column_break(child->block()->break_after);
    item->break_before_avoid = child && child->blk &&
        multicol_avoids_column_break(child->block()->break_before);
    item->break_after_avoid = child && child->blk &&
        multicol_avoids_column_break(child->block()->break_after);
    item->parallel_balance_tail = 0.0f;
}

static bool multicol_text_has_visible_rect(DomText* text);

static bool multicol_has_direct_inline_wrapper(ViewBlock* block) {
    if (!block) return false;
    for (View* child = block->first_placed_child(); child; child = child->next()) {
        if (child->view_type == RDT_VIEW_INLINE) return true;
    }
    return false;
}

static void multicol_collect_inline_float_geometry(
    View* view, ViewBlock* container, float* max_extent, float* rightmost_x) {
    if (!view || !container || !max_extent) return;
    for (View* current = view; current; current = current->next()) {
        if (ViewBlock* current_block = lam::view_as_block(current)) {
            if (layout_position_is_floated(current_block->position)) {
                float extent = current_block->y - container->y + current_block->height +
                    layout_box_metrics(current_block).margin_v;
                if (extent > *max_extent) *max_extent = extent;
                if (rightmost_x && current_block->x > *rightmost_x) {
                    *rightmost_x = current_block->x;
                }
            }
            continue;
        }
        if (current->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(current);
            multicol_collect_inline_float_geometry(
                span->first_child, container, max_extent, rightmost_x);
        }
    }
}

static float multicol_inline_float_extent(View* view, ViewBlock* container,
                                          float* rightmost_x = nullptr) {
    float max_extent = 0.0f;
    if (rightmost_x) *rightmost_x = -FLT_MAX;
    multicol_collect_inline_float_geometry(
        view, container, &max_extent, rightmost_x);
    return max_extent;
}

static float multicol_inline_wrapper_float_extent(ViewBlock* block) {
    if (!block) return 0.0f;
    float max_extent = 0.0f;
    for (View* child = block->first_placed_child(); child; child = child->next()) {
        if (child->view_type != RDT_VIEW_INLINE) continue;
        ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(child);
        float extent = multicol_inline_float_extent(span->first_child, block);
        if (extent > max_extent) max_extent = extent;
    }
    return max_extent;
}

static void multicol_reanchor_float_only_inline_span(ViewSpan* span) {
    if (!span || span->width != 0.0f || span->height != 0.0f) return;
    ViewBlock* container = layout_nearest_block_ancestor(span->parent_view());
    if (!container) return;

    float rightmost_float_x = -FLT_MAX;
    multicol_inline_float_extent(span->first_child, container, &rightmost_float_x);
    if (rightmost_float_x > span->x) {
        // css inline: an out-of-flow-only inline box follows the furthest
        // projected float fragment in its line.
        span->x = rightmost_float_x;
    }
}

static void multicol_collect_inline_text_extent(
    View* first,
    float origin_y,
    bool* before_spanner,
    bool* found_text,
    float* leading_extent,
    float* total_extent
) {
    for (View* current = first; current; current = current->next()) {
        if (ViewBlock* current_block = lam::view_as_block(current)) {
            if (multicol_is_spanner_block(current_block)) *before_spanner = false;
            continue;
        }
        if (current->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(current);
            multicol_collect_inline_text_extent(
                span->first_child, origin_y, before_spanner, found_text,
                leading_extent, total_extent);
            continue;
        }
        if (current->view_type != RDT_VIEW_TEXT || current->node_type != DOM_NODE_TEXT) {
            continue;
        }
        DomText* text = lam::dom_require<DOM_NODE_TEXT>(current);
        if (!layout_dom_text_has_non_whitespace(text)) continue;
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->width <= 0.0f || rect->height <= 0.0f) continue;
            float extent = rect->y - origin_y + rect->height;
            if (extent < 0.0f) continue;
            *found_text = true;
            if (extent > *total_extent) *total_extent = extent;
            if (*before_spanner && extent > *leading_extent) {
                *leading_extent = extent;
            }
        }
    }
}

static bool multicol_direct_inline_text_extent(
    ViewBlock* block, float origin_y, float* leading_extent, float* total_extent) {
    if (!block || !leading_extent || !total_extent) return false;
    bool before_spanner = true;
    bool found_text = false;
    *leading_extent = 0.0f;
    *total_extent = 0.0f;
    multicol_collect_inline_text_extent(
        block->first_placed_child(), origin_y, &before_spanner, &found_text,
        leading_extent, total_extent);
    return found_text;
}

static bool multicol_collect_inline_flow_blocks(
    View* view,
    ViewBlock* container,
    MulticolFlowItem* items,
    int* item_count,
    bool floats_only = false
) {
    if (!view || !container || !items || !item_count) return true;
    for (View* descendant = view; descendant; descendant = descendant->next()) {
        if (*item_count >= MAX_MULTICOL_BLOCKS) return false;
        if (ViewBlock* descendant_block = lam::view_as_block(descendant)) {
            if (layout_block_is_out_of_flow_positioned(descendant_block)) continue;
            if (floats_only && !layout_position_is_floated(descendant_block->position)) {
                continue;
            }
            float flow_height = descendant_block->height +
                layout_box_metrics(descendant_block).margin_v;
            multicol_init_flow_item(
                &items[*item_count], container, descendant_block,
                flow_height, descendant_block->x - container->x,
                multicol_is_spanner_block(descendant_block));
            (*item_count)++;
            continue;
        }
        if (descendant->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(descendant);
            if (!multicol_collect_inline_flow_blocks(
                    span->first_child, container, items, item_count, floats_only)) {
                return false;
            }
            continue;
        }
        if (descendant->node_type == DOM_NODE_TEXT) {
            // visible inline content needs the inline fragmentation path.
            DomText* text = lam::dom_require<DOM_NODE_TEXT>(descendant);
            if (layout_dom_text_has_non_whitespace(text) &&
                multicol_text_has_visible_rect(text)) return false;
            continue;
        }
        if (descendant->view_type == RDT_VIEW_BR) return false;
        if (descendant->view_type) return false;
    }
    return true;
}

static int multicol_collect_flow_group(
    MulticolFlowItem* items,
    int item_count,
    int* index,
    float* item_heights,
    float* item_content_heights,
    float* item_margin_before,
    float* item_margin_after,
    bool* item_can_fragment,
    bool* break_before,
    bool* break_after,
    float* out_total_height
) {
    if (!items || !index || *index < 0 || *index >= item_count) {
        if (out_total_height) *out_total_height = 0.0f;
        return 0;
    }

    float total_height = 0.0f;
    float parallel_height = 0.0f;
    int group_item_count = 0;
    while (*index < item_count && !items[*index].spans_all) {
        MulticolFlowItem* item = &items[*index];
        if (group_item_count > 0 &&
            multicol_requires_separate_spanner_group(item->block) &&
            multicol_spanner_prefix_flow_extent(item->block) <= 0.0f) {
            // css multicol §6: a wrapper with no preceding flow starts its
            // own column group before its escaping spanner.
            break;
        }
        if (item->parallel_flow) {
            parallel_height += item->balance_height;
        } else {
            total_height += item->balance_height;
        }
        if (group_item_count < MAX_MULTICOL_BLOCKS) {
            item_heights[group_item_count] = item->height;
            item_content_heights[group_item_count] = item->content_height;
            item_margin_before[group_item_count] = item->margin_before;
            item_margin_after[group_item_count] = item->margin_after;
            item_can_fragment[group_item_count] = item->can_fragment;
            break_before[group_item_count] = item->break_before_column;
            break_after[group_item_count] = item->break_after_column;
            item->parallel_balance_tail = 0.0f;
            if (*index + 1 < item_count && item->block &&
                item->block->position &&
                layout_position_is_floated(item->block->position)) {
                MulticolFlowItem* next_item = &items[*index + 1];
                if (!next_item->spans_all &&
                    multicol_clear_matches_float(next_item->block, item->block)) {
                    float tail_extent = multicol_parallel_forced_tail_extent(item->block);
                    if (tail_extent > 0.0f) {
                        item->parallel_balance_tail = tail_extent +
                            item->margin_before + item->margin_after;
                    }
                }
            }
            group_item_count++;
        }
        (*index)++;
        // css multicol §6: the wrapper containing an escaping spanner is its
        // own column group, so following content starts after the spanner.
        if (multicol_requires_separate_spanner_group(item->block)) {
            break;
        }
    }
    if (total_height <= 0.0f) {
        // css multicol: a group containing only parallel flows still needs a
        // fragmentainer target sized by those flows.
        total_height = parallel_height;
    }
    if (out_total_height) *out_total_height = total_height;
    return group_item_count;
}

static float multicol_avoid_break_target_floor(
    ViewBlock* container,
    MulticolFlowItem* items,
    int group_start,
    int group_end,
    float target_height
) {
    if (!container || !items || multicol_content_box_height_limit(container) >= 0.0f) {
        return target_height;
    }
    float ancestor_fragmentainer_height =
        multicol_definite_ancestor_fragmentainer_height(container);
    if (ancestor_fragmentainer_height >= 0.0f) {
        bool has_fitting_avoid_item = false;
        for (int index = group_start; index < group_end; index++) {
            ViewBlock* child = items[index].block;
            if (child && child->blk &&
                child->block()->break_inside == CSS_VALUE_AVOID &&
                items[index].height <= ancestor_fragmentainer_height + 0.5f) {
                has_fitting_avoid_item = true;
                break;
            }
        }
        if (!has_fitting_avoid_item) return target_height;
    }
    for (int index = group_start; index < group_end; index++) {
        ViewBlock* child = items[index].block;
        if (!child || !child->blk ||
            child->block()->break_inside != CSS_VALUE_AVOID) {
            continue;
        }
        // css-break: balancing should honor avoid-break when the item can fit
        // as a whole; forced overflow still fragments during distribution.
        target_height = max(target_height, items[index].height);
    }
    return target_height;
}

static void multicol_allow_fragmentation_before_spanner(
    MulticolFlowItem* items,
    int group_start,
    int group_end,
    bool* item_can_fragment
) {
    if (!items || !item_can_fragment) return;
    for (int index = group_start; index < group_end; index++) {
        ViewBlock* child = items[index].block;
        if (!child || !child->blk ||
            child->block()->break_inside == CSS_VALUE_AVOID ||
            child->block()->contain_positioning ||
            (child->multicol_prop() && is_multicol_container(child))) {
            continue;
        }
        // css-multicol: balancing before a spanner may fragment an otherwise
        // monolithic block so the preceding column group reaches its target.
        items[index].can_fragment = true;
        item_can_fragment[index - group_start] = true;
    }
}

static bool multicol_group_has_block_margins(
    MulticolFlowItem* items, int group_start, int group_end) {
    if (!items) return false;
    for (int i = group_start; i < group_end; i++) {
        if (items[i].margin_before > 0.0f || items[i].margin_after > 0.0f) {
            return true;
        }
    }
    return false;
}

static float multicol_group_balance_total(
    MulticolFlowItem* items,
    int group_start,
    int group_item_count,
    float group_total,
    int* out_self_sizing_count
) {
    if (out_self_sizing_count) *out_self_sizing_count = 0;
    if (!items || group_item_count <= 0) return group_total;

    float balance_total = group_total;
    int self_sizing_count = 0;
    for (int i = 0; i < group_item_count; i++) {
        MulticolFlowItem& item = items[group_start + i];
        if (item.parallel_balance_tail <= 0.0f) continue;
        // css-break §2.1: a forced break in a parallel float can increase the
        // float's auto block size; solve that target-size dependency once.
        balance_total += item.parallel_flow
            ? item.parallel_balance_tail
            : item.parallel_balance_tail - item.height;
        self_sizing_count++;
    }
    if (out_self_sizing_count) *out_self_sizing_count = self_sizing_count;
    return balance_total;
}

static bool multicol_uses_slice_start_trim(ViewBlock* child) {
    // slice only trims the real box edges. With trim-both, intermediate column
    // breaks keep their normal line-box under-edge, so only start-only trim
    // changes the first fragment's line capacity.
    return child && child->blk &&
        child->block()->box_decoration_break != CSS_VALUE_CLONE &&
        (child->block()->text_box_trim_applied & TEXT_BOX_TRIM_START) &&
        !(child->block()->text_box_trim_applied & TEXT_BOX_TRIM_END) &&
        child->block()->text_box_trim_start_amount > 0.0f;
}

static bool multicol_uses_slice_end_offset_trim(ViewBlock* child, float initial_fragment_offset) {
    // trim-end keeps the block-start half-leading. If the block starts partway
    // through a fragmentainer, that consumed space reduces only the first
    // fragment's line-box capacity.
    return child && child->blk &&
        child->block()->box_decoration_break != CSS_VALUE_CLONE &&
        initial_fragment_offset > 0.0f &&
        (child->block()->text_box_trim_applied & TEXT_BOX_TRIM_END) &&
        !(child->block()->text_box_trim_applied & TEXT_BOX_TRIM_START);
}

static float multicol_line_visual_height_from_items(InlineFragmentItem* items, int item_count) {
    for (int i = 0; i < item_count; i++) {
        if (items[i].height > 0.0f) return items[i].height;
    }
    return 0.0f;
}

static float multicol_normal_line_offset(float line_advance, float visual_height) {
    if (line_advance <= visual_height || visual_height <= 0.0f) return 0.0f;
    return (line_advance - visual_height) * 0.5f;
}

static int multicol_lines_that_fit_fragment(
    float fragment_height,
    float line_advance,
    float visual_height = 0.0f,
    float line_offset = 0.0f
) {
    if (fragment_height <= 0.0f || line_advance <= 0.0f) return 1;
    if (visual_height <= 0.0f) {
        int count = (int)floorf((fragment_height + 0.5f) / line_advance); // INT_CAST_OK: line slot count from positive fragment height
        return count > 0 ? count : 1;
    }

    float remaining = fragment_height - line_offset - visual_height;
    if (remaining < -0.5f) return 1;

    int count = (int)floorf((remaining + 0.5f) / line_advance) + 1; // INT_CAST_OK: line slot count from positive fragment height
    return count > 0 ? count : 1;
}

static void multicol_map_line_to_fragment(
    int line_index,
    int first_fragment_lines,
    int continuation_fragment_lines,
    int* out_fragment_index,
    int* out_line_slot
) {
    if (first_fragment_lines < 1) first_fragment_lines = 1;
    if (continuation_fragment_lines < 1) continuation_fragment_lines = first_fragment_lines;
    if (line_index < first_fragment_lines) {
        *out_fragment_index = 0;
        *out_line_slot = line_index;
        return;
    }

    int remaining = line_index - first_fragment_lines;
    *out_fragment_index = 1 + remaining / continuation_fragment_lines;
    *out_line_slot = remaining % continuation_fragment_lines;
}

static float multicol_normalize_inline_x(float x, float origin_x, float pitch) {
    if (pitch <= 0) return x;
    float local = fmodf(x - origin_x, pitch);
    if (local < 0) local += pitch;
    return origin_x + local;
}

static float multicol_fragment_content_extent(ViewBlock* child, float fragment_extent) {
    if (!child || fragment_extent <= 0.0f || !child->blk ||
        child->block()->box_decoration_break != CSS_VALUE_CLONE) {
        return fragment_extent;
    }

    LayoutAxis block_axis = multicol_has_vertical_inline_axis(child)
        ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
    float decoration = layout_axis_decoration_start(child->bound, block_axis) +
        layout_axis_decoration_end(child->bound, block_axis);
    return max(fragment_extent - decoration, 0.0f);
}

static MulticolFragmentPlacement multicol_place_fragment(
    float original_offset,
    float fragment_height,
    int column_count,
    float column_width,
    float column_gap,
    float row_gap
) {
    MulticolFragmentPlacement placement = {};
    if (fragment_height <= 0.0f || column_count <= 0) return placement;

    placement.fragment_index = (int)floorf(original_offset / fragment_height); // INT_CAST_OK: fragment index from positive height
    if (placement.fragment_index < 0) placement.fragment_index = 0;
    placement.column_index = placement.fragment_index % column_count;
    placement.row_index = placement.fragment_index / column_count;
    placement.local_offset = original_offset - placement.fragment_index * fragment_height;
    placement.x_offset = placement.column_index * (column_width + column_gap);
    placement.y_offset = placement.row_index * (fragment_height + row_gap) +
        placement.local_offset;
    return placement;
}

template <typename FragmentFn>
static bool multicol_for_each_nested_horizontal_fragment(
    const MulticolNestedHorizontalSequence* sequence,
    float flow_start,
    float flow_extent,
    FragmentFn visit,
    int* out_fragment_count
) {
    if (out_fragment_count) *out_fragment_count = 0;
    if (!sequence || sequence->fragment_height <= 0.0f ||
        sequence->parent_column_count <= 0 ||
        sequence->nested_column_count <= 0 || flow_extent <= 0.0f) {
        return false;
    }

    float start = max(flow_start, 0.0f);
    float end = start + flow_extent;
    float consumed = 0.0f;
    int fragment_count = 0;
    for (int parent_column = 0;
         parent_column < MAX_MULTICOL_BLOCKS && consumed < end - 0.001f;
         parent_column++) {
        for (int nested_column = 0;
             nested_column < sequence->nested_column_count &&
             consumed < end - 0.001f;
             nested_column++) {
            float capacity = sequence->fragment_height -
                (parent_column == 0 ? sequence->initial_fragment_offset : 0.0f);
            if (capacity <= 0.0f) continue;

            float slot_start = consumed;
            float slot_end = slot_start + capacity;
            float overlap_start = max(start, slot_start);
            float overlap_end = min(end, slot_end);
            if (overlap_end > overlap_start) {
                int visual_parent_column = sequence->parent_direction_rtl
                    ? sequence->parent_column_count - 1 -
                        (parent_column % sequence->parent_column_count)
                    : parent_column % sequence->parent_column_count;
                int visual_nested_column = sequence->nested_direction_rtl
                    ? sequence->nested_column_count - 1 - nested_column
                    : nested_column;
                int row_index = sequence->wraps_rows
                    ? parent_column / sequence->parent_column_count : 0;
                float nested_continuation_gap = visual_parent_column *
                    sequence->nested_column_gap;
                float slot_x = visual_parent_column *
                        (sequence->parent_column_width +
                         sequence->parent_column_gap) +
                    visual_nested_column *
                        (sequence->nested_column_width +
                         sequence->nested_column_gap) +
                    nested_continuation_gap;
                float slot_y = (parent_column == 0
                        ? sequence->initial_fragment_offset : 0.0f) +
                    row_index * (sequence->fragment_height + sequence->row_gap);
                visit(slot_x, slot_y + overlap_start - slot_start,
                    overlap_end - overlap_start, visual_parent_column,
                    row_index);
                fragment_count++;
            }
            consumed = slot_end;
        }
    }
    if (out_fragment_count) *out_fragment_count = fragment_count;
    return fragment_count > 0;
}

static float multicol_nested_horizontal_unbreakable_start(
    const MulticolNestedHorizontalSequence* sequence,
    float flow_start,
    float flow_extent
) {
    if (!sequence || sequence->fragment_height <= 0.0f ||
        flow_extent <= 0.0f) return max(flow_start, 0.0f);

    float start = max(flow_start, 0.0f);
    float consumed = 0.0f;
    bool found_start = false;
    bool seek_fitting_slot = false;
    for (int parent_column = 0;
         parent_column < MAX_MULTICOL_BLOCKS; parent_column++) {
        for (int nested_column = 0;
             nested_column < sequence->nested_column_count; nested_column++) {
            float capacity = sequence->fragment_height -
                (parent_column == 0 ? sequence->initial_fragment_offset : 0.0f);
            if (capacity <= 0.0f) continue;
            float slot_start = consumed;
            float slot_end = consumed + capacity;
            if (!found_start && start < slot_end - 0.001f) {
                float remaining = slot_end - start;
                if (flow_extent <= remaining + 0.5f) return start;
                if (flow_extent > sequence->fragment_height + 0.5f) {
                    return slot_end;
                }
                found_start = true;
                seek_fitting_slot = true;
            } else if (found_start && seek_fitting_slot &&
                       capacity >= flow_extent - 0.5f) {
                return slot_start;
            }
            consumed = slot_end;
        }
    }
    return start;
}

static bool multicol_find_nested_horizontal_fragment(
    const MulticolNestedHorizontalSequence* sequence,
    float flow_start,
    float* out_x,
    float* out_y
) {
    if (!sequence || !out_x || !out_y) return false;
    bool found = false;
    multicol_for_each_nested_horizontal_fragment(
        sequence, flow_start, 0.01f,
        [&](float x, float y, float, int, int) {
            if (found) return;
            *out_x = x;
            *out_y = y;
            found = true;
        }, nullptr);
    return found;
}

static void multicol_reanchor_nested_overwide_linebreaks(
    ViewBlock* block,
    const MulticolNestedHorizontalSequence* sequence
) {
    if (!block || !sequence || sequence->nested_column_width <= 0.0f) return;

    ViewBlock* previous_overwide = nullptr;
    for (DomNode* child_node = block->first_child; child_node;
         child_node = child_node->next_sibling) {
        View* child = static_cast<View*>(child_node);
        if (multicol_is_direct_br_node(child)) {
            if (previous_overwide) {
                // css fragmentation: a forced line break after an over-wide
                // atomic inline stays at that inline's projected line edge.
                child->x = previous_overwide->x + previous_overwide->width;
                child->y = previous_overwide->y;
            }
            continue;
        }
        if (child->node_type == DOM_NODE_TEXT ||
            child->node_type == DOM_NODE_COMMENT) {
            continue;
        }
        if (child->view_type == RDT_VIEW_INLINE_BLOCK) {
            previous_overwide = child->width >
                sequence->nested_column_width + 0.5f
                ? lam::view_require_block(child) : nullptr;
            continue;
        }
        previous_overwide = nullptr;
    }
}

static bool multicol_store_nested_horizontal_fragments(
    ViewBlock* child,
    const MulticolNestedHorizontalSequence* sequence,
    float flow_start,
    float flow_extent,
    float fragment_width,
    float* out_min_x,
    float* out_min_y,
    float* out_max_x,
    float* out_max_y,
    int* out_fragment_count
) {
    if (out_min_x) *out_min_x = 0.0f;
    if (out_min_y) *out_min_y = 0.0f;
    if (out_max_x) *out_max_x = 0.0f;
    if (out_max_y) *out_max_y = 0.0f;
    if (out_fragment_count) *out_fragment_count = 0;
    if (!child || !sequence || fragment_width <= 0.0f) return false;

    float min_x = FLT_MAX;
    float min_y = FLT_MAX;
    float max_x = -FLT_MAX;
    float max_y = -FLT_MAX;
    int fragment_count = 0;
    bool has_fragments = multicol_for_each_nested_horizontal_fragment(
        sequence, flow_start, flow_extent,
        [&](float x, float y, float piece_extent, int, int) {
            min_x = min(min_x, x);
            min_y = min(min_y, y);
            max_x = max(max_x, x + fragment_width);
            max_y = max(max_y, y + piece_extent);
        }, &fragment_count);
    if (!has_fragments) return false;

    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(child);
    Pool* pool = multicol_layout_fragment_pool(elem);
    if (!pool) return false;
    elem->set_layout_fragment_list(nullptr);
    elem->layout_fragments_count_ref() = 0;

    LayoutFragmentBox* first = nullptr;
    LayoutFragmentBox* previous = nullptr;
    int fragment_index = 0;
    multicol_for_each_nested_horizontal_fragment(
        sequence, flow_start, flow_extent,
        [&](float x, float y, float piece_extent, int column_index, int row_index) {
            LayoutFragmentBox* fragment = (LayoutFragmentBox*)pool_calloc(
                pool, sizeof(LayoutFragmentBox));
            if (!fragment) return;
            fragment->x = x - min_x;
            fragment->y = y - min_y;
            fragment->width = fragment_width;
            fragment->height = piece_extent;
            fragment->fragment_index = fragment_index++;
            fragment->column_index = column_index;
            fragment->row_index = row_index;
            fragment->next = nullptr;
            if (!first) first = fragment;
            if (previous) previous->next = fragment;
            previous = fragment;
            elem->layout_fragments_count_ref()++;
        }, nullptr);
    if (!first) return false;
    elem->set_layout_fragment_list(first);
    if (out_min_x) *out_min_x = min_x;
    if (out_min_y) *out_min_y = min_y;
    if (out_max_x) *out_max_x = max_x;
    if (out_max_y) *out_max_y = max_y;
    if (out_fragment_count) *out_fragment_count = fragment_count;
    return true;
}

static bool multicol_init_nested_horizontal_sequence(
    ViewBlock* parent_block,
    ViewBlock* nested_multicol,
    float fragment_height,
    int parent_column_count,
    float parent_column_width,
    float parent_column_gap,
    float initial_fragment_offset,
    MulticolNestedHorizontalSequence* out_sequence
) {
    if (!parent_block || !nested_multicol || !out_sequence ||
        !nested_multicol->multicol_prop() || fragment_height <= 0.0f ||
        parent_column_count <= 0 || parent_column_width <= 0.0f) {
        return false;
    }
    int nested_column_count = 1;
    float nested_column_width = parent_column_width;
    float nested_column_gap = 0.0f;
    calculate_multicol_dimensions(
        nested_multicol->multicol_prop(), parent_column_width,
        multicol_normal_gap_size(nested_multicol), &nested_column_count,
        &nested_column_width, &nested_column_gap);
    if (nested_column_count <= 1 || nested_column_width <= 0.0f) {
        return false;
    }

    out_sequence->fragment_height = fragment_height;
    out_sequence->parent_column_count = parent_column_count;
    out_sequence->parent_column_width = parent_column_width;
    out_sequence->parent_column_gap = max(parent_column_gap, 0.0f);
    out_sequence->row_gap = max(multicol_row_gap(parent_block), 0.0f);
    out_sequence->nested_column_count = nested_column_count;
    out_sequence->nested_column_width = nested_column_width;
    out_sequence->nested_column_gap = max(nested_column_gap, 0.0f);
    out_sequence->initial_fragment_offset = max(
        min(initial_fragment_offset, fragment_height), 0.0f);
    out_sequence->parent_direction_rtl = parent_block->blk &&
        parent_block->block()->direction == CSS_VALUE_RTL;
    out_sequence->nested_direction_rtl = nested_multicol->blk &&
        nested_multicol->block()->direction == CSS_VALUE_RTL;
    out_sequence->wraps_rows = multicol_group_wraps_rows(parent_block);
    return true;
}

static bool multicol_view_direction_is_rtl(View* view);

static bool multicol_project_fragmented_inline_descendants(
    LayoutContext* lycon,
    ViewBlock* child,
    float fragment_height,
    int parent_column_count,
    float parent_column_width,
    float parent_column_gap,
    float row_gap,
    float initial_fragment_offset
) {
    if (!child || fragment_height <= 0 || parent_column_count <= 0) return false;
    ViewBlock* parent_block = lam::view_as_block(child->parent);
    if (!parent_block) {
        // a block in an inline wrapper inherits the wrapper's writing mode.
        parent_block = child;
    }
    bool vertical_writing = multicol_has_vertical_inline_axis(parent_block);
    WritingMode parent_mode = layout_block_writing_mode(parent_block);
    // 2048 InlineFragmentItem (~56 B) ≈ 112 KiB — too large for stack; allocate from scratch arena (LIFO).
    constexpr int MAX_INLINE_FRAGMENT_ITEMS = 2048;
    InlineFragmentItem* items = (InlineFragmentItem*)scratch_alloc(&lycon->scratch,
        MAX_INLINE_FRAGMENT_ITEMS * sizeof(InlineFragmentItem));
    if (!items) return false;
    int item_count = 0;

    multicol_for_each_inline_leaf(child->first_placed_child(), [&](View* descendant) {
        if (item_count >= MAX_INLINE_FRAGMENT_ITEMS) return;
        if (descendant->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(descendant);
            TextRect* rect = text->rect;
            while (rect && item_count < MAX_INLINE_FRAGMENT_ITEMS) {
                if (rect->width <= 0 && rect->length > 0) {
                    rect = rect->next;
                    continue;
                }
                items[item_count].view = descendant;
                items[item_count].rect = rect;
                items[item_count].original_x = rect->x;
                // TextRect coordinates are parent-block local; subtracting the
                // newly placed block offset shifts every fragment a second time.
                items[item_count].original_y = rect->y;
                items[item_count].line_y = items[item_count].original_y;
                items[item_count].height = rect->height;
                items[item_count].line_index = 0;
                items[item_count].is_text = true;
                items[item_count].is_forced_break = false;
                item_count++;
                rect = rect->next;
            }
        } else if (descendant->view_type == RDT_VIEW_BR) {
            items[item_count].view = descendant;
            items[item_count].rect = NULL;
            items[item_count].original_x = descendant->x;
            items[item_count].original_y = descendant->y;
            items[item_count].line_y = items[item_count].original_y;
            items[item_count].height = descendant->height;
            items[item_count].line_index = 0;
            items[item_count].is_text = false;
            // CSS 2.1 §9.2.1.1: out-of-flow inline descendants do not end
            // the line; only a BR contributes a forced line break here.
            items[item_count].is_forced_break = descendant->view_type == RDT_VIEW_BR;
            item_count++;
        }
    });

    if (item_count == 0) {
        scratch_free(&lycon->scratch, items);
        return false;
    }

    float first_line_y = items[0].original_y;
    float current_line_y = first_line_y;
    int line_index = 0;
    int logical_line_count = 0;
    bool forced_break_pending = false;
    for (int i = 0; i < item_count; i++) {
        if (items[i].is_text && i > 0 &&
            (forced_break_pending || fabsf(items[i].original_y - current_line_y) > 1.0f)) {
            line_index++;
            current_line_y = items[i].original_y;
            forced_break_pending = false;
        }
        items[i].line_index = line_index;
        items[i].line_y = current_line_y;
        if (line_index + 1 > logical_line_count) logical_line_count = line_index + 1;
        if (items[i].is_forced_break) {
            forced_break_pending = true;
        }
    }

    float line_advance = multicol_line_advance_from_items(items, item_count);
    float visual_height = multicol_line_visual_height_from_items(items, item_count);
    if (visual_height <= 0.0f) visual_height = line_advance;
    float normal_line_offset = multicol_normal_line_offset(line_advance, visual_height);
    bool slice_start_trim = multicol_uses_slice_start_trim(child);
    if (initial_fragment_offset < 0.0f) initial_fragment_offset = 0.0f;
    if (initial_fragment_offset >= fragment_height) {
        initial_fragment_offset = fmodf(initial_fragment_offset, fragment_height);
    }
    bool slice_end_offset_trim = multicol_uses_slice_end_offset_trim(child, initial_fragment_offset);
    float first_fragment_line_offset = first_line_y;
    float continuation_line_offset = first_line_y;
    if (slice_start_trim) {
        first_fragment_line_offset = normal_line_offset - child->block()->text_box_trim_start_amount;
        if (first_fragment_line_offset < 0.0f) first_fragment_line_offset = 0.0f;
        continuation_line_offset = normal_line_offset;
    }

    float content_fragment_height = multicol_fragment_content_extent(child, fragment_height);
    int first_fragment_lines = 0;
    int continuation_fragment_lines = 0;
    if (slice_start_trim) {
        first_fragment_lines = multicol_lines_that_fit_fragment(
            content_fragment_height, line_advance, visual_height, first_fragment_line_offset);
        continuation_fragment_lines = multicol_lines_that_fit_fragment(
            content_fragment_height, line_advance, visual_height, continuation_line_offset);
    } else if (slice_end_offset_trim || initial_fragment_offset > 0.0f) {
        // A block that begins partway through a column has only the remaining
        // fragmentainer space available before its continuation moves columns.
        first_fragment_lines = multicol_lines_that_fit_fragment(
            content_fragment_height - initial_fragment_offset, line_advance);
        continuation_fragment_lines = multicol_lines_that_fit_fragment(
            content_fragment_height, line_advance);
    } else {
        first_fragment_lines = multicol_lines_that_fit_fragment(content_fragment_height, line_advance);
        continuation_fragment_lines = first_fragment_lines;
    }

    int inner_column_count = 1;
    float inner_column_width = parent_column_width;
    float inner_column_gap = 0;
    if (child->multicol_prop() && is_multicol_container(child)) {
        calculate_multicol_dimensions(child->multicol_prop(), parent_column_width,
            multicol_normal_gap_size(child),
            &inner_column_count, &inner_column_width, &inner_column_gap);
        if (inner_column_count < 1) inner_column_count = 1;
    }

    float first_fragment_block_extent = content_fragment_height - initial_fragment_offset;
    if (first_fragment_block_extent < 0.0f) first_fragment_block_extent = 0.0f;

    float parent_pitch = parent_column_width + parent_column_gap;
    float inner_pitch = inner_column_width + inner_column_gap;
    int forced_break_index = -1;
    int forced_break_fragment_index = -1;
    float forced_break_block_position = 0.0f;
    float forced_break_source_x = 0.0f;
    bool forced_break_source_known = false;
    for (int i = 0; i < item_count; i++) {
        // CSS Writing Modes: a fragmented block and a nested inline can carry
        // different directions; either RTL context affects the continuation.
        bool fragment_direction_rtl = vertical_writing &&
            (child->block()->direction == CSS_VALUE_RTL ||
             multicol_view_direction_is_rtl(items[i].view));
        int inner_fragment_index = 0;
        int line_slot = 0;
        float projected_block_position = items[i].original_x;
        if (vertical_writing && inner_column_count == 1) {
            // CSS Multi-column fragments in vertical writing progress on the
            // physical block axis (x); the inline axis (y) remains in-column.
            float block_position = projected_block_position;
            if (items[i].is_forced_break) {
                // CSS 2.1 §9.2.1.1: a BR belongs to the line it terminates;
                // its laid-out cursor is already one block line ahead in
                // either inline direction.
                for (int previous = i - 1; previous >= 0; previous--) {
                    if (items[previous].is_text) {
                        block_position = items[previous].original_x;
                        break;
                    }
                }
                forced_break_index = i;
                forced_break_source_known = false;
            }
            projected_block_position = block_position;
            InitialLetterInfo initial_letter = {};
            bool is_initial_letter = items[i].is_text &&
                layout_get_text_initial_letter_info(
                    static_cast<DomNode*>(items[i].view), &initial_letter);
            if (parent_mode == WM_VERTICAL_RL) {
                bool in_first_fragment = is_initial_letter ||
                    block_position >= first_fragment_block_extent;
                if (in_first_fragment) {
                    inner_fragment_index = 0;
                } else {
                    float distance = first_fragment_block_extent - block_position;
                    // CSS Multicol fragmentation assigns a block-start edge
                    // on the fragment boundary to the continuation, not to
                    // an empty fragment beyond the content.
                    inner_fragment_index = (int)ceilf(
                        distance / fragment_height); // INT_CAST_OK: fragment index from positive block distance
                }
            } else {
                bool in_first_fragment = is_initial_letter ||
                    block_position < first_fragment_block_extent;
                if (in_first_fragment) {
                    inner_fragment_index = 0;
                } else {
                    float distance = block_position - first_fragment_block_extent;
                    inner_fragment_index = 1 +
                        (int)floorf(distance / fragment_height); // INT_CAST_OK: fragment index from positive block distance
                }
            }
            if (forced_break_index >= 0 && !items[i].is_forced_break) {
                if (!forced_break_source_known) {
                    forced_break_source_x = items[i].original_x;
                    forced_break_source_known = true;
                }
                if (fabsf(items[i].original_x - forced_break_source_x) <= 1.0f) {
                    if (inner_fragment_index > forced_break_fragment_index) {
                        // CSS 2.1 §9.2.1.1 keeps the line after a forced break
                        // in its current fragmentainer until that new line
                        // itself overflows.
                        inner_fragment_index = forced_break_fragment_index;
                        projected_block_position = forced_break_block_position;
                    }
                } else {
                    forced_break_index = -1;
                    forced_break_source_known = false;
                }
            }
            if (items[i].is_forced_break) {
                forced_break_fragment_index = inner_fragment_index;
                forced_break_block_position = items[i].original_x;
            }
        } else {
            bool nested_balanced_columns = child->multicol_prop() &&
                is_multicol_container(child) && inner_column_count > 1 &&
                child->multicol_prop()->fill == COLUMN_FILL_BALANCE;
            if (!nested_balanced_columns) {
                multicol_map_line_to_fragment(items[i].line_index,
                    first_fragment_lines, continuation_fragment_lines,
                    &inner_fragment_index, &line_slot);
            } else {
                // css multicol: a nested column set fills the first parent
                // fragment to capacity, then balances its final continuation set.
                int first_group_capacity = first_fragment_lines * inner_column_count;
                int line_index = items[i].line_index;
                if (line_index < first_group_capacity) {
                    inner_fragment_index = line_index / first_fragment_lines;
                    line_slot = line_index % first_fragment_lines;
                } else {
                    int remaining = logical_line_count - first_group_capacity;
                    int continuation_capacity = continuation_fragment_lines * inner_column_count;
                    int continuation_index = line_index - first_group_capacity;
                    int full_groups = continuation_capacity > 0
                        ? remaining / continuation_capacity : 0;
                    int remainder = continuation_capacity > 0
                        ? remaining % continuation_capacity : remaining;
                    int group_index;
                    int group_line_index;
                    int lines_per_column;
                    if (remainder > 0 && continuation_index >= full_groups * continuation_capacity) {
                        group_index = full_groups;
                        group_line_index = continuation_index - full_groups * continuation_capacity;
                        lines_per_column = (remainder + inner_column_count - 1) /
                            inner_column_count;
                    } else {
                        group_index = continuation_capacity > 0
                            ? continuation_index / continuation_capacity : 0;
                        group_line_index = continuation_capacity > 0
                            ? continuation_index % continuation_capacity : continuation_index;
                        lines_per_column = continuation_fragment_lines;
                    }
                    if (lines_per_column < 1) lines_per_column = 1;
                    inner_fragment_index = (group_index + 1) * inner_column_count +
                        min(group_line_index / lines_per_column, inner_column_count - 1);
                    line_slot = group_line_index % lines_per_column;
                }
            }
        }
        int inner_column_index = inner_fragment_index % inner_column_count;
        int parent_fragment_index = inner_fragment_index / inner_column_count;
        int parent_column_index = parent_fragment_index % parent_column_count;
        int parent_row_index = parent_fragment_index / parent_column_count;

        if (vertical_writing && inner_column_count == 1) {
            float new_x = 0.0f;
            if (inner_fragment_index == 0) {
                if (parent_mode == WM_VERTICAL_RL) {
                    new_x = projected_block_position - first_fragment_block_extent;
                } else {
                    new_x = projected_block_position + initial_fragment_offset;
                }
                InitialLetterInfo initial_letter = {};
                bool sideways_rl = parent_block->is_element() &&
                    layout_element_css_writing_mode(parent_block->as_element()) ==
                        CSS_VALUE_SIDEWAYS_RL;
                if (items[i].is_text && parent_mode == WM_VERTICAL_RL &&
                    !sideways_rl &&
                    layout_get_text_initial_letter_info(
                        static_cast<DomNode*>(items[i].view), &initial_letter)) {
                    // the initial-letter margin box is monolithic; keep its
                    // first fragment at the fragmentainer block-start edge.
                    new_x = 0.0f;
                }
            } else if (parent_mode == WM_VERTICAL_RL) {
                new_x = projected_block_position + initial_fragment_offset +
                    (inner_fragment_index - 1) * fragment_height;
            } else {
                new_x = projected_block_position - first_fragment_block_extent -
                    (inner_fragment_index - 1) * fragment_height;
            }
            float new_y = items[i].original_y +
                parent_column_index * parent_pitch +
                parent_row_index * (fragment_height + row_gap);
            if (!items[i].is_text && items[i].is_forced_break) {
                // CSS 2.1 §9.2.1.1: BR carries the forced-break line position;
                // its vertical coordinate already includes the inline-column
                // advance, so applying the parent pitch again double-counts it.
                float break_y = items[i].original_y;
                bool rtl_break = fragment_direction_rtl;
                if (rtl_break &&
                    parent_pitch > 0.0f) {
                    // CSS Writing Modes: RTL inline progression stores the
                    // break cursor in the parent-column coordinate space.
                    break_y = fmodf(break_y, parent_pitch);
                    if (break_y < 0.0f) break_y += parent_pitch;
                }
                new_y = break_y +
                    parent_row_index * (fragment_height + row_gap);
                if (items[i].original_y <= 0.0f && parent_pitch > 0.0f) {
                    // CSS 2.1 §9.2.1.1: a BR at the normalized fragment start
                    // has not yet carried the parent inline-column advance.
                    new_y = parent_pitch +
                        parent_row_index * (fragment_height + row_gap);
                }
            }
            if (items[i].is_text) {
                items[i].rect->x = new_x;
                items[i].rect->y = new_y;
            } else {
                items[i].view->x = new_x;
                items[i].view->y = new_y;
            }
            continue;
        }

        float normalized_x = multicol_normalize_inline_x(items[i].original_x, 0.0f, inner_pitch);
        float line_local_delta_y = items[i].is_text ? items[i].original_y - items[i].line_y : 0;
        float fragment_line_offset = slice_start_trim && inner_fragment_index > 0 ?
            continuation_line_offset : first_fragment_line_offset;
        float new_x = normalized_x +
            parent_column_index * parent_pitch +
            inner_column_index * inner_pitch;
        float first_fragment_offset = inner_fragment_index == 0 ? initial_fragment_offset : 0.0f;
        // TextRect coordinates are local to the fragmented block's union box.
        float new_y = first_fragment_offset + fragment_line_offset +
            parent_row_index * (fragment_height + row_gap) +
            line_slot * line_advance +
            line_local_delta_y;

        if (!items[i].is_text && items[i].is_forced_break) {
            // line-break boxes expose their local line-box position; only the
            // text fragment carries the parent column and row offset.
            new_x = normalized_x;
            new_y = first_fragment_offset + fragment_line_offset +
                line_slot * line_advance;
        }

        if (items[i].is_text) {
            items[i].rect->x = new_x;
            items[i].rect->y = new_y;
        } else {
            items[i].view->x = new_x;
            items[i].view->y = new_y;
        }
    }

    View* descendant = child->first_placed_child();
    while (descendant) {
        if (descendant->view_type == RDT_VIEW_TEXT) {
        adjust_text_bounds(lam::view_require<RDT_VIEW_TEXT>(descendant));
        }
        descendant = descendant->next();
    }

    scratch_free(&lycon->scratch, items);
    return true;
}

static int multicol_project_fragmented_descendants(
    LayoutContext* lycon,
    ViewBlock* child,
    float fragment_height,
    int column_count,
    float column_width,
    float column_gap,
    float block_split_height,
    float initial_fragment_offset,
    bool nested_descendants_local,
    const MulticolNestedHorizontalSequence* nested_horizontal_sequence
) {
    if (!child || fragment_height <= 0 || column_count <= 0) return 0;
    if (block_split_height <= 0) block_split_height = fragment_height;


    ViewBlock* parent_block = lam::view_as_block(child->parent);
    if (!parent_block) {
        // a block in an inline wrapper inherits the wrapper's writing mode.
        parent_block = child;
    }
    bool vertical_writing = multicol_has_vertical_inline_axis(parent_block);
    bool horizontal_table = !vertical_writing &&
        child->display.inner == CSS_VALUE_TABLE;
    float horizontal_table_grid_block_start = 0.0f;
    if (horizontal_table) {
        ViewTable* table = static_cast<ViewTable*>(child);
        horizontal_table_grid_block_start = initial_fragment_offset +
            layout_axis_decoration_start(
                child->bound ? child->boundary() : nullptr, LAYOUT_AXIS_Y);
        if (table->tb && !table->tb->border_collapse) {
            horizontal_table_grid_block_start += table->tb->border_spacing_v;
        }
    }
    float row_gap = multicol_row_gap(parent_block);
    if (row_gap < 0) row_gap = 0;

    bool has_block_flow_child = multicol_has_fragmentable_block_children(child);
    bool use_monolithic_child_flow = !vertical_writing &&
        multicol_contained_monolithic_child_count(child) > 0;
    MulticolMonolithicChildFlow monolithic_child_flow = {};
    multicol_init_monolithic_child_flow(
        &monolithic_child_flow,
        multicol_monolithic_initial_offset(child, initial_fragment_offset),
        fragment_height);
    bool forced_break_flow = multicol_has_forced_break_descendant(child);
    bool child_is_multicol = child->multicol_prop() && is_multicol_container(child);
    bool nested_multicol_spanner_projection = child_is_multicol &&
        !nested_descendants_local && multicol_has_spanner_child(child);
    bool projected_inline = !has_block_flow_child &&
        multicol_project_fragmented_inline_descendants(
            lycon, child, fragment_height, column_count, column_width, column_gap,
            row_gap, initial_fragment_offset);
    int inner_column_count = 1;
    float inner_column_width = column_width;
    float inner_column_gap = 0;
    if (child_is_multicol) {
        // css writing modes: nested columns divide the child inline axis;
        // vertical inline sizes are represented by the physical height.
        float nested_available_inline = vertical_writing
            ? max(child->height, column_width) : column_width;
        calculate_multicol_dimensions(child->multicol_prop(), nested_available_inline,
            multicol_normal_gap_size(child),
            &inner_column_count, &inner_column_width, &inner_column_gap);
        if (inner_column_count < 1) inner_column_count = 1;
        if (inner_column_width <= 0) inner_column_width = column_width;
    }
    const MulticolNestedHorizontalSequence* horizontal_sequence =
        nested_horizontal_sequence;
    MulticolNestedHorizontalSequence local_horizontal_sequence = {};
    if (!horizontal_sequence && child_is_multicol && !vertical_writing &&
        !nested_descendants_local &&
        parent_block &&
        is_multicol_container(parent_block) && inner_column_count > 1 &&
        !multicol_has_spanner_child(child) &&
        !multicol_uses_fixed_wrapped_rows(child) &&
        (multicol_has_nested_overwide_block(
             static_cast<View*>(child), inner_column_width, fragment_height) ||
         multicol_has_nested_overwide_linebreak(
             static_cast<View*>(child), inner_column_width))) {
        if (multicol_init_nested_horizontal_sequence(
                parent_block, child, fragment_height, column_count,
                column_width, column_gap, initial_fragment_offset,
                &local_horizontal_sequence)) {
            horizontal_sequence = &local_horizontal_sequence;
        }
    }

    View* descendant = child->first_placed_child();
    float subslot_flow_y = 0;
    float nested_horizontal_flow_cursor = 0.0f;
    float nested_overwide_flow_shift = 0.0f;
    float nested_last_overwide_flow = 0.0f;
    bool nested_has_overwide_flow = false;
    bool previous_nested_overwide = false;
    bool has_forced_break_origin = false;
    float forced_break_origin = 0.0f;
    int forced_break_fragment = 0;
    int projected_fragment_count = 1;
    while (descendant) {
        View* next = descendant->next();
        if (ViewBlock* descendant_block = lam::view_as_block(descendant)) {
            if (layout_block_is_out_of_flow_positioned(descendant_block)) {
                descendant = next;
                continue;
            }
        }

        if (descendant->view_type == RDT_VIEW_TEXT) {
            if (!projected_inline) {
                multicol_project_fragmented_text_rects(lam::view_require<RDT_VIEW_TEXT>(descendant),
                    child->y, fragment_height, column_count, column_width, column_gap, row_gap);
            }
            descendant = next;
            continue;
        }

        if (descendant->view_type == RDT_VIEW_BR && projected_inline &&
            !child_is_multicol) {
            // nested multicol line breaks still need their inner-column edge.
            descendant = next;
            continue;
        }

        if (descendant->view_type == RDT_VIEW_INLINE && projected_inline) {
            // The line-fragment pass has already projected the inline's text;
            // moving the span subtree again would apply the column offset twice.
            descendant = next;
            continue;
        }

        bool use_subslot_flow =
            !child_is_multicol &&
            block_split_height > 0 &&
            block_split_height < fragment_height &&
            descendant->is_block();
        float descendant_flow_height = 0;
        if (use_subslot_flow) {
            ViewBlock* descendant_block = lam::view_require_block(descendant);
            descendant_flow_height = descendant_block->height +
                layout_box_metrics(descendant_block).margin_v;
        }

        ViewBlock* descendant_block = lam::view_as_block(descendant);
        float original_y = use_subslot_flow ? subslot_flow_y :
            (child_is_multicol
                 ? descendant->y : descendant->y - child->y);
        if (horizontal_sequence && !vertical_writing && !use_subslot_flow &&
            descendant_block) {
            // css multicol: floats and other out-of-line placements still
            // consume source-order flow in a nested fragmentation context.
            original_y = max(original_y, nested_horizontal_flow_cursor);
        }
        bool nested_overwide_inline = horizontal_sequence &&
            !vertical_writing && descendant->view_type == RDT_VIEW_INLINE_BLOCK &&
            descendant->width > horizontal_sequence->nested_column_width + 0.5f;
        if (horizontal_sequence && !vertical_writing && !use_subslot_flow) {
            if (nested_overwide_inline) {
                bool first_nested_overwide = !nested_has_overwide_flow;
                if (!nested_has_overwide_flow) {
                    float first_fragment_capacity = horizontal_sequence->fragment_height -
                        horizontal_sequence->initial_fragment_offset;
                    float first_line_capacity = first_fragment_capacity *
                        horizontal_sequence->nested_column_count;
                    if (original_y < first_line_capacity - 0.5f) {
                        nested_overwide_flow_shift = first_line_capacity - original_y;
                        nested_has_overwide_flow = true;
                    }
                }
                if (first_nested_overwide) {
                    // css fragmentation: an unbreakable over-wide inline flow
                    // consumes the first nested line's fragmentainer capacity.
                    original_y += nested_overwide_flow_shift;
                }
                nested_last_overwide_flow = original_y;
            } else if (multicol_is_direct_br_node(descendant) &&
                       previous_nested_overwide) {
                // css fragmentation: a br after an unbreakable over-wide
                // inline item belongs to that item's projected line.
                original_y = nested_last_overwide_flow;
            }
        }
        float projected_flow_offset = original_y;
        if (forced_break_flow && has_forced_break_origin) {
            // css-break §2.1: a forced break starts following content at the
            // next fragmentainer block-start.
            projected_flow_offset = forced_break_fragment * fragment_height +
                (original_y - forced_break_origin);
        }
        bool force_before = descendant_block && descendant_block->blk &&
            multicol_forces_column_break(descendant_block->block()->break_before);
        if (forced_break_flow && force_before &&
            (has_forced_break_origin || projected_fragment_count > 1 ||
                             original_y > 0.0f)) {
            MulticolFragmentPlacement break_placement = multicol_place_fragment(
                projected_flow_offset, fragment_height, column_count,
                column_width, column_gap, row_gap);
            forced_break_fragment = max(forced_break_fragment,
                break_placement.fragment_index +
                (break_placement.local_offset > 0.5f ? 1 : 0));
            forced_break_origin = original_y;
            has_forced_break_origin = true;
            projected_flow_offset = forced_break_fragment * fragment_height;
        }
        float descendant_origin_offset = 0.0f;
        float descendant_flow_offset = projected_flow_offset;
        if (has_block_flow_child && !child_is_multicol && !vertical_writing) {
            // phase-one descendants retain their pre-fragment parent origin;
            // restore the parent offset before assigning their fragment.
            float phase_one_parent_offset = initial_fragment_offset;
            float source_offset = original_y + phase_one_parent_offset;
            descendant_origin_offset = phase_one_parent_offset;
            descendant_flow_offset = phase_one_parent_offset + source_offset;
        }
        if (has_block_flow_child && !child_is_multicol && !vertical_writing &&
            descendant->is_block() &&
            initial_fragment_offset > 0.0f && fragment_height > 0.0f) {
            ViewBlock* descendant_block = lam::view_require_block(descendant);
            float descendant_extent = descendant_block->height +
                layout_box_metrics(descendant_block).margin_v;
            float local_offset = fmodf(descendant_flow_offset, fragment_height);
            bool descendant_can_fragment = multicol_has_fragmentable_line_boxes(
                descendant_block);
            if (local_offset > 0.0f &&
                local_offset + descendant_extent > fragment_height + 0.5f &&
                !descendant_can_fragment) {
                // css fragmentation: keep a monolithic child together when the
                // remaining space belongs to its parent fragment.
                descendant_flow_offset += fragment_height - local_offset;
            }
        }
        MulticolFragmentPlacement placement = multicol_place_fragment(
            descendant_flow_offset, fragment_height, column_count, column_width,
            column_gap, row_gap);
        bool horizontal_table_grid = horizontal_table &&
            layout_view_uses_table_grid_coordinates(descendant);
        if (horizontal_table_grid && initial_fragment_offset > 0.0f) {
            // css multicol: a table grid starts after the initial fragment's
            // consumed space, even when its logical flow offset is local.
            placement = multicol_place_fragment(
                descendant_flow_offset + initial_fragment_offset,
                fragment_height, column_count, column_width, column_gap,
                row_gap);
        }
        bool child_owns_fragmentainer_sequence = child->multicol_prop() &&
            is_multicol_container(child);
        bool exhausted_parent_fragmentainers = !child_owns_fragmentainer_sequence &&
            !multicol_group_wraps_rows(parent_block) &&
            fragment_height > 0.0f &&
            descendant_flow_offset >= column_count * fragment_height;
        if (exhausted_parent_fragmentainers) {
            // css fragmentation: once a finite fragmentainer sequence is
            // exhausted, later overflow stays in the final fragmentainer.
            int final_column = column_count - 1;
            placement.fragment_index = final_column;
            placement.column_index = final_column;
            placement.row_index = 0;
            placement.local_offset = descendant_flow_offset -
                final_column * fragment_height;
            placement.x_offset = final_column * (column_width + column_gap);
            placement.y_offset = placement.local_offset;
        }
        if (use_monolithic_child_flow && descendant_block) {
            int monolithic_fragment = -1;
            float monolithic_offset = 0.0f;
            if (multicol_advance_monolithic_child_flow(
                    child, descendant_block, fragment_height,
                    &monolithic_child_flow, &monolithic_fragment,
                    &monolithic_offset)) {
                placement = multicol_place_fragment(
                    monolithic_fragment * fragment_height + monolithic_offset,
                    fragment_height,
                    column_count, column_width, column_gap, row_gap);
            }
        }
        int fragment_index = placement.fragment_index;
        int column_index = placement.column_index;
        int row_index = placement.row_index;
        float local_y = placement.local_offset;
        if (placement.fragment_index + 1 > projected_fragment_count) {
            projected_fragment_count = placement.fragment_index + 1;
        }
        float new_x = descendant->x + placement.x_offset;
        float new_y = child->y + placement.y_offset - descendant_origin_offset;
        bool descendant_fragmented = false;
        bool table_grid_fragmented = false;
        bool handled_nested_horizontal = false;
        float nested_horizontal_flow_end = original_y;
        if (horizontal_sequence && descendant_block && descendant->is_block() &&
            descendant_block->display.inner != CSS_VALUE_TABLE) {
            float flow_extent = max(descendant_block->height, 0.0f);
            if (descendant_block->blk &&
                layout_axis_has_given_size(descendant_block, false)) {
                float given_height = layout_axis_given_size(
                    descendant_block->block(), LAYOUT_AXIS_Y);
                flow_extent = max(flow_extent,
                    layout_used_border_box_size(
                        descendant_block, given_height, false));
            }
            float fragment_width = horizontal_sequence->nested_column_width;
            if (descendant_block->blk &&
                layout_axis_has_given_size(descendant_block, true)) {
                float given_width = layout_axis_given_size(
                    descendant_block->block(), LAYOUT_AXIS_X);
                fragment_width = layout_used_border_box_size(
                    descendant_block, given_width, true);
            }
            float nested_min_x = 0.0f;
            float nested_min_y = 0.0f;
            float nested_max_x = 0.0f;
            float nested_max_y = 0.0f;
            int nested_fragment_count = 0;
            bool placed_unbreakable = false;
            if (multicol_is_contained_monolithic(descendant_block)) {
                float unbreakable_start =
                    multicol_nested_horizontal_unbreakable_start(
                        horizontal_sequence, original_y, flow_extent);
                float unbreakable_x = 0.0f;
                float unbreakable_y = 0.0f;
                if (multicol_find_nested_horizontal_fragment(
                        horizontal_sequence, unbreakable_start,
                        &unbreakable_x, &unbreakable_y)) {
                    // css-break: a monolithic child moves to the next
                    // fragmentainer when it cannot fit the current remainder.
                    handled_nested_horizontal = true;
                    placed_unbreakable = true;
                    new_x = child->x + unbreakable_x;
                    new_y = child_is_multicol
                        ? unbreakable_y : child->y + unbreakable_y;
                    descendant_block->width = fragment_width;
                    descendant_block->height = max(
                        descendant_block->height, flow_extent);
                    descendant_block->content_height =
                        descendant_block->height;
                    nested_horizontal_flow_end = unbreakable_start + flow_extent;
                }
            }
            if (!placed_unbreakable && multicol_store_nested_horizontal_fragments(
                    descendant_block, horizontal_sequence, original_y,
                    flow_extent, fragment_width, &nested_min_x, &nested_min_y,
                    &nested_max_x, &nested_max_y, &nested_fragment_count)) {
                handled_nested_horizontal = true;
                descendant_fragmented = nested_fragment_count > 1;
                new_x = child->x + nested_min_x;
                new_y = child_is_multicol
                    ? nested_min_y : child->y + nested_min_y;
                descendant_block->width = nested_max_x - nested_min_x;
                descendant_block->height = nested_max_y - nested_min_y;
                nested_horizontal_flow_end = original_y + flow_extent;
                if (nested_fragment_count > projected_fragment_count) {
                    projected_fragment_count = nested_fragment_count;
                }
                if (descendant_block->first_placed_child()) {
                    multicol_project_fragmented_descendants(
                        lycon, descendant_block,
                        horizontal_sequence->fragment_height,
                        horizontal_sequence->parent_column_count,
                        horizontal_sequence->parent_column_width,
                        horizontal_sequence->parent_column_gap,
                        horizontal_sequence->fragment_height, 0.0f, false,
                        horizontal_sequence);
                }
            }
        }
        if (horizontal_table && descendant_block &&
            descendant_block->display.inner == CSS_VALUE_TABLE_CAPTION) {
            // css multicol: a caption that crosses the initial fragmentainer
            // exposes the union of its fragment widths in CSSOM geometry.
            float caption_flow_extent = max(descendant_block->height, 0.0f);
            int caption_fragment_count = (int)ceilf(
                (initial_fragment_offset + caption_flow_extent) /
                    fragment_height); // INT_CAST_OK: positive caption flow count
            if (caption_fragment_count < 1) caption_fragment_count = 1;
            if (caption_fragment_count > 1) {
                descendant_block->width += (caption_fragment_count - 1) *
                    (column_width + column_gap);
            }
            new_y = 0.0f;
        }
        if (vertical_writing && child->display.inner == CSS_VALUE_TABLE &&
            descendant_block &&
            descendant_block->display.inner == CSS_VALUE_TABLE_CAPTION) {
            DomElement* table_element = lam::dom_require<DOM_NODE_ELEMENT>(child);
            int table_fragment_count = table_element->layout_fragments_count();
            if (table_fragment_count > 1 && block_split_height > 0.0f) {
                float caption_flow_extent = max(descendant_block->width, 0.0f);
                int caption_fragment_count = (int)ceilf(
                    (initial_fragment_offset + caption_flow_extent) /
                        block_split_height); // INT_CAST_OK: positive caption flow count
                if (caption_fragment_count < 1) caption_fragment_count = 1;
                if (caption_fragment_count > table_fragment_count) {
                    caption_fragment_count = table_fragment_count;
                }
                bool caption_is_bottom = layout_specified_keyword(
                    descendant_block->as_element(), CSS_PROPERTY_CAPTION_SIDE,
                    CSS_VALUE_TOP) == CSS_VALUE_BOTTOM;
                int caption_start_fragment = caption_is_bottom
                    ? table_fragment_count - caption_fragment_count : 0;
                float caption_pitch = block_split_height;
                LayoutFragmentBox* first_table_fragment =
                    table_element->layout_fragment_list();
                if (first_table_fragment && first_table_fragment->next) {
                    float fragment_delta = first_table_fragment->next->y -
                        first_table_fragment->y;
                    if (fragment_delta > 0.0f) caption_pitch = fragment_delta;
                }
                ViewTable* table_view = static_cast<ViewTable*>(child);
                float caption_fragment_height = table_view->tb &&
                    table_view->tb->vertical_inline_extent > 0.0f
                    ? table_view->tb->vertical_inline_extent
                    : descendant_block->height;
                descendant_block->height = caption_fragment_height;
                multicol_store_layout_fragments(
                    descendant_block, caption_fragment_count, column_count,
                    block_split_height, column_width, column_gap, row_gap,
                    fragment_height, 0.0f);
                descendant_block->height = caption_fragment_height +
                    (caption_fragment_count - 1) * caption_pitch;
                descendant_block->content_height = descendant_block->height;
                // css tables: captions share the table fragment's block edge;
                // their pre-publication logical x is not a physical offset.
                new_x = child->x;
                new_y = caption_start_fragment * caption_pitch;
                descendant_fragmented = caption_fragment_count > 1;
                projected_fragment_count = max(projected_fragment_count,
                    caption_start_fragment + caption_fragment_count);
            }
        }
        if (!handled_nested_horizontal && child_is_multicol) {
            if (nested_multicol_spanner_projection) {
                // css multicol: a nested multicol's existing internal column
                // union is projected into the ancestor's fragmentainer slots.
                int parent_column_index = fragment_index % column_count;
                int parent_row_index = fragment_index / column_count;
                local_y = original_y - fragment_index * fragment_height;
                float descendant_x = descendant->x;
                if (descendant_block && multicol_is_spanner_block(descendant_block)) {
                    descendant_x = child->x + layout_axis_decoration_start(
                        child->bound ? child->boundary() : nullptr, LAYOUT_AXIS_X);
                }
                new_x = descendant_x +
                    parent_column_index * (column_width + column_gap);
                new_y = child->y + parent_row_index * (fragment_height + row_gap) +
                    local_y;
            } else {
                int inner_fragment_index = fragment_index;
                if (nested_descendants_local && descendant->view_type == RDT_VIEW_BR &&
                    placement.local_offset >= 0.5f) {
                    // css inline: a br marker at the end of a line belongs to
                    // the next fragment's line-start edge, not the preceding slot.
                    inner_fragment_index++;
                }
                int inner_column_index = inner_fragment_index % inner_column_count;
                int parent_fragment_index = inner_fragment_index / inner_column_count;
                int parent_column_index = parent_fragment_index % column_count;
                int parent_row_index = parent_fragment_index / column_count;
                local_y = original_y - inner_fragment_index * fragment_height;
                if (nested_descendants_local && descendant->view_type == RDT_VIEW_BR) {
                    local_y = 0.0f;
                    fragment_index = inner_fragment_index;
                }
                new_x = nested_descendants_local && descendant->view_type == RDT_VIEW_BR
                    ? 0.0f : descendant->x;
                new_x += parent_column_index * (column_width + column_gap) +
                    inner_column_index * (inner_column_width + inner_column_gap);
                new_y = parent_row_index * (fragment_height + row_gap) + local_y;

                if (nested_descendants_local && !multicol_has_spanner_child(child) &&
                    descendant_block && !multicol_is_spanner_block(descendant_block) &&
                    child->is_element() &&
                    child->as_element()->layout_fragments_count() > 1 &&
                    multicol_find_escaping_spanner(descendant->next())) {
                    LayoutFragmentBox* first_parent_fragment =
                        child->as_element()->layout_fragment_list();
                    if (first_parent_fragment) {
                        // css multicol §6: flow preceding an escaping spanner
                        // remains in the preceding parent fragmentainer.
                        new_x = descendant->x + first_parent_fragment->x;
                        new_y = first_parent_fragment->y + local_y;
                    }
                }

                if (nested_descendants_local && !multicol_has_spanner_child(child) &&
                    descendant_block &&
                    multicol_has_direct_spanner_child(descendant_block) &&
                    child->is_element() &&
                    child->as_element()->layout_fragments_count() > 1 &&
                    descendant->is_block()) {
                    // css-break: map a nested multicol's local flow through
                    // the actual parent fragments; an unbreakable child that
                    // crosses the first fragment moves to the next one.
                    float source_start = 0.0f;
                    LayoutFragmentBox* nested_fragment =
                        child->as_element()->layout_fragment_list();
                    LayoutFragmentBox* selected_fragment = nullptr;
                    float selected_source_start = 0.0f;
                    while (nested_fragment) {
                        float source_end = source_start + nested_fragment->height;
                        bool fits_fragment = original_y >= source_start - 0.5f &&
                            original_y + descendant_block->height <= source_end + 0.5f;
                        bool can_break_descendant =
                            multicol_has_fragmentable_line_boxes(descendant_block) ||
                            multicol_has_fragmentable_block_children(descendant_block);
                        if (fits_fragment || can_break_descendant ||
                            !nested_fragment->next) {
                            selected_fragment = nested_fragment;
                            selected_source_start = source_start;
                            break;
                        }
                        source_start = source_end;
                        nested_fragment = nested_fragment->next;
                    }
                    if (selected_fragment) {
                        float local_source_offset = original_y - selected_source_start;
                        if (local_source_offset < 0.0f) local_source_offset = 0.0f;
                        new_x = descendant->x + selected_fragment->x;
                        new_y = selected_fragment->y + local_source_offset;
                        local_y = local_source_offset;

                        LayoutFragmentBox* first_parent_fragment =
                            child->as_element()->layout_fragment_list();
                        if (first_parent_fragment &&
                            !multicol_is_spanner_block(descendant_block) &&
                            multicol_find_escaping_spanner(descendant->next())) {
                            // css multicol §6: flow before an escaping spanner
                            // occupies the preceding parent fragmentainer.
                            new_x = descendant->x + first_parent_fragment->x;
                            new_y = first_parent_fragment->y + local_y;
                        }

                        ViewBlock* escaping_spanner =
                            multicol_find_escaping_spanner(
                                descendant_block->first_placed_child());
                        if (escaping_spanner) {
                            float preceding_flow_end = 0.0f;
                            for (View* previous = child->first_placed_child();
                                 previous && previous != descendant;
                                 previous = previous->next()) {
                                ViewBlock* previous_block =
                                    lam::view_as_block(previous);
                                if (!previous_block ||
                                    layout_block_is_out_of_flow_positioned(previous_block)) {
                                    continue;
                                }
                                preceding_flow_end = max(preceding_flow_end,
                                    previous_block->y + previous_block->height);
                            }
                            // The spanner still belongs to the parent flow;
                            // store its position relative to the moved wrapper.
                            escaping_spanner->x = child->x - new_x;
                            escaping_spanner->y = preceding_flow_end -
                                selected_fragment->y;
                        }
                    }
                }
            }

            if (descendant->is_block()) {
                ViewBlock* descendant_block = lam::view_require_block(descendant);
                bool descendant_contained_monolithic =
                    multicol_is_contained_monolithic(descendant_block);
                float descendant_flow_extent = vertical_writing
                    ? descendant_block->width : descendant_block->height;
                if (!vertical_writing && descendant_block->is_element() &&
                    descendant_block->as_element()->layout_fragments_count() > 1) {
                    // css fragmentation: a nested descendant's union box is
                    // smaller than the source flow that generated its fragments.
                    descendant_flow_extent = max(descendant_flow_extent,
                        multicol_stored_fragment_flow_height(
                            descendant_block, descendant_flow_extent));
                }
                if (nested_descendants_local && !vertical_writing && descendant_block->blk &&
                    layout_axis_has_given_size(descendant_block, false)) {
                    descendant_flow_extent = max(descendant_flow_extent,
                        layout_axis_given_size(descendant_block->block(), LAYOUT_AXIS_Y));
                }
                if (nested_multicol_spanner_projection &&
                    descendant_flow_extent > block_split_height &&
                    !descendant_contained_monolithic) {
                    int descendant_fragment_count = (int)ceilf(
                        descendant_flow_extent / block_split_height); // INT_CAST_OK: fragment count from positive heights
                    if (descendant_fragment_count < 1) descendant_fragment_count = 1;
                    int used_columns = min(descendant_fragment_count, column_count);
                    float fragment_visual_width = descendant_block->width > 0.0f
                        ? descendant_block->width : column_width;
                    float union_width = fragment_visual_width +
                        (used_columns - 1) * (column_width + column_gap);
                    multicol_store_layout_fragments(
                        descendant_block, descendant_fragment_count, column_count,
                        block_split_height, column_width, column_gap, row_gap,
                        fragment_visual_width, 0.0f);
                    if (descendant_block->width < union_width) {
                        descendant_block->width = union_width;
                    }
                    descendant_block->height = block_split_height;
                    descendant_block->content_height = block_split_height;
                    multicol_project_fragmented_descendants(
                        lycon, descendant_block, fragment_height, column_count,
                        column_width, column_gap, block_split_height, 0.0f);
                    new_y -= local_y;
                    descendant_fragmented = true;
                }
                if (!descendant_fragmented && descendant_flow_extent > block_split_height) {
                    int total_column_slots = column_count * inner_column_count;
                    int descendant_fragment_count = (int)ceilf(descendant_flow_extent / block_split_height); // INT_CAST_OK: fragment count from positive heights
                    if (descendant_fragment_count < 1) descendant_fragment_count = 1;
                    float union_width;
                    float union_height = block_split_height;
                    if (vertical_writing) {
                        // css writing modes: nested block flow fragments on
                        // physical x, while its DOMRect height is the full
                        // logical block flow extent.
                        union_width = min(block_split_height,
                            max(descendant_block->width, 0.0f));
                        if (union_width <= 0.0f) union_width = block_split_height;
                        union_height = max(descendant_flow_extent, 0.0f);
                    } else {
                        union_width = inner_column_width +
                            (min(descendant_fragment_count, total_column_slots) - 1) *
                            (inner_column_width + inner_column_gap);
                        bool child_min_height_fragmentainer = child_is_multicol &&
                            child->multicol_prop()->fill == COLUMN_FILL_AUTO &&
                            layout_explicit_min_axis_or(child, false, -1.0f) >= 0.0f &&
                            multicol_has_definite_ancestor_fragmentainer(child);
                        // css fragmentation: a minimum-sized nested flow owns
                        // its descendant union; the wrapper width is outer flow.
                        if (!nested_descendants_local &&
                            !child_min_height_fragmentainer &&
                            child->width > union_width) {
                            union_width = child->width;
                        }
                    }
                    bool parent_is_single_column = parent_block &&
                        parent_block->multicol_prop() &&
                        parent_block->multicol_prop()->column_count <= 1;
                    if (!nested_descendants_local && parent_is_single_column &&
                        child->blk &&
                        child->block()->given_height > fragment_height + 0.5f &&
                        child->is_element() &&
                        child->as_element()->layout_fragments_count() > 1) {
                        // css fragmentation: a definite nested block that was
                        // split by this ancestor carries its prior union into
                        // the descendant's continuation geometry.
                        union_width = max(union_width, child->width) +
                            (min(descendant_fragment_count, total_column_slots) - 1) *
                            (column_width + column_gap);
                    }
                    if (!descendant_contained_monolithic) {
                        multicol_store_layout_fragments(descendant_block,
                            descendant_fragment_count, total_column_slots,
                            block_split_height, inner_column_width, inner_column_gap,
                            row_gap, inner_column_width, 0.0f);
                        if (vertical_writing || descendant_block->width < union_width) {
                            descendant_block->width = union_width;
                        }
                        descendant_block->height = union_height;
                        float descendant_split_height = block_split_height;
                        if (!vertical_writing && inner_column_count > 1) {
                            descendant_split_height = block_split_height / inner_column_count;
                        }
                        multicol_project_fragmented_descendants(lycon, descendant_block,
                            fragment_height, total_column_slots,
                            inner_column_width, inner_column_gap, descendant_split_height,
                            0.0f, nested_descendants_local);
                        descendant_fragmented = true;
                    } else {
                        // css containment: a monolithic descendant keeps its
                        // authored block size while its parent is fragmented.
                        descendant_fragmented = true;
                    }
                }
            }
        } else if (!handled_nested_horizontal &&
                   block_split_height > 0 && block_split_height < fragment_height) {
            int slots_per_fragment = (int)floorf(fragment_height / block_split_height); // INT_CAST_OK: sub-slot count from positive fragment heights
            if (slots_per_fragment < 1) slots_per_fragment = 1;
            int slot_index = (int)floorf(original_y / block_split_height); // INT_CAST_OK: sub-slot index from positive height
            if (slot_index < 0) slot_index = 0;
            int logical_column = slot_index / slots_per_fragment;
            int row_slot = slot_index % slots_per_fragment;
            column_index = logical_column % column_count;
            row_index = logical_column / column_count;
            fragment_index = slot_index;
            local_y = original_y - slot_index * block_split_height;
            new_x = child->x + column_index * (column_width + column_gap);
            new_y = child->y +
                row_index * (fragment_height + row_gap) +
                row_slot * block_split_height +
                local_y;
        }

        if (!descendant_fragmented && descendant->is_block() &&
            !exhausted_parent_fragmentainers) {
            ViewBlock* descendant_block = lam::view_require_block(descendant);
            float descendant_block_extent = vertical_writing
                ? descendant_block->width : descendant_block->height;
            if (nested_descendants_local && !vertical_writing && descendant_block->blk &&
                layout_axis_has_given_size(descendant_block, false)) {
                descendant_block_extent = max(descendant_block_extent,
                    layout_axis_given_size(descendant_block->block(), LAYOUT_AXIS_Y));
            }
            if (descendant_block_extent > block_split_height &&
                !multicol_is_contained_monolithic(descendant_block)) {
                int descendant_fragment_count = (int)ceilf(descendant_block_extent / block_split_height); // INT_CAST_OK: fragment count from positive heights
                if (descendant_fragment_count < 1) descendant_fragment_count = 1;
                int used_columns = min(descendant_fragment_count, column_count);
                int row_count = (descendant_fragment_count + column_count - 1) / column_count;
                if (row_count < 1) row_count = 1;
                // Fragment union bounds retain the block border box; clipping
                // it to the fragmentainer loses overflow in DOMRect geometry.
                float fragment_visual_width = vertical_writing
                    ? block_split_height
                    : (descendant_block->width > 0.0f
                        ? descendant_block->width : column_width);
                float union_width;
                float union_height;
                if (vertical_writing) {
                    union_width = min(block_split_height,
                        max(descendant_block->width, 0.0f));
                    if (union_width <= 0.0f) union_width = block_split_height;
                    // css writing modes: an automatic inline-size resolves to
                    // the current fragmentainer's inline extent, represented
                    // by fragment_height in the vertical projection path.
                    bool table_grid_descendant =
                        layout_view_uses_table_grid_coordinates(descendant) ||
                        layout_view_uses_table_grid_coordinates(child);
                    // css tables: automatic column tracks use their published
                    // track extent, not the containing fragmentainer extent.
                    union_height = table_grid_descendant ||
                        !layout_css_size_is_automatic(descendant_block, false)
                        ? max(descendant_block->height, 0.0f)
                        : max(fragment_height, max(column_width, 0.0f));
                    if (table_grid_descendant) {
                        // css tables: a grid track keeps its inline size while
                        // its block fragments span the containing columns.
                        union_height += (used_columns - 1) *
                            (column_width + column_gap);
                    }
                    new_x = layout_block_writing_mode(parent_block) == WM_VERTICAL_RL
                        ? child->x + child->width - block_split_height : child->x;
                    // preserve the table track's inline-axis start; only the
                    // block-axis fragments move to successive fragmentainers.
                    new_y = descendant->y;
                } else {
                    union_width = fragment_visual_width +
                        (used_columns - 1) * (column_width + column_gap);
                    union_height = row_count * block_split_height +
                        (row_count - 1) * row_gap;
                    if (block_split_height < fragment_height) {
                        union_height = block_split_height;
                    }
                }
                multicol_store_layout_fragments(descendant_block,
                    descendant_fragment_count, column_count,
                    block_split_height, column_width, column_gap,
                    row_gap, fragment_visual_width, 0.0f);
                if (vertical_writing || descendant_block->width < union_width) {
                    descendant_block->width = union_width;
                }
                descendant_block->height = union_height;
                if (layout_view_uses_table_grid_coordinates(descendant)) {
                    table_grid_fragmented = true;
                }
                // CSS Multicol fragmentation propagates through the subtree;
                // otherwise only the wrapper gets the fragment union and a
                // nested block keeps its unfragmented DOMRect.
                if (layout_view_uses_table_grid_coordinates(descendant)) {
                    // css tables: descendants use the projected grid parent's
                    // coordinate space, so publish that origin before recursing.
                    descendant->x = new_x;
                    descendant->y = horizontal_table_grid ? 0.0f : new_y;
                }
                multicol_project_fragmented_descendants(
                    lycon, descendant_block, block_split_height, column_count,
                    column_width, column_gap, block_split_height, 0.0f);
            }
        }

        if (horizontal_table_grid) {
            // css tables: fragmented grid unions begin at the table's
            // fragmentainer edge; an unfragmented track keeps its grid start.
            new_y = table_grid_fragmented
                ? 0.0f : horizontal_table_grid_block_start;
        }

        if (vertical_writing &&
            child->display.inner == CSS_VALUE_TABLE &&
            layout_block_writing_mode(parent_block) == WM_VERTICAL_RL &&
            layout_view_uses_table_grid_coordinates(descendant) &&
            !table_grid_fragmented) {
            // css writing modes: an unfragmented table grid box ends at the
            // containing fragment's block edge after vertical-rl mirroring.
            ViewTable* table = static_cast<ViewTable*>(child);
            float table_grid_start = layout_axis_decoration_start(
                child->bound ? child->boundary() : nullptr, LAYOUT_AXIS_X);
            if (table->tb && !table->tb->border_collapse) {
                table_grid_start += table->tb->border_spacing_h;
            }
            float first_fragment_grid_start = table_grid_start +
                initial_fragment_offset;
            new_x = child->x + block_split_height -
                first_fragment_grid_start - descendant->width;
            if (new_x != descendant->x) {
                descendant->x = new_x;
            }
        }

        if (vertical_writing && child->display.inner == CSS_VALUE_TABLE &&
            layout_block_writing_mode(parent_block) == WM_VERTICAL_LR &&
            layout_view_uses_table_grid_coordinates(descendant) &&
            !table_grid_fragmented) {
            // css writing modes: table captions precede the grid in the
            // published block axis, so remove that prefix for an unfragmented
            // grid child before the containing fragment offset is applied.
            ViewTable* table = static_cast<ViewTable*>(child);
            if (table->tb) {
                new_x -= table->tb->vertical_top_caption_extent;
            }
        }

        if (vertical_writing && child->display.inner == CSS_VALUE_TABLE &&
            layout_view_uses_table_grid_coordinates(descendant) &&
            initial_fragment_offset > 0.0f) {
            LayoutFragmentBox* first_table_fragment = child->is_element()
                ? child->as_element()->layout_fragment_list() : nullptr;
            float table_fragment_origin = first_table_fragment
                ? first_table_fragment->y : 0.0f;
            if (table_fragment_origin > 0.0f) {
                // css fragmentation: table-grid descendants inherit the
                // table's first fragment origin on the physical inline axis.
                new_y += table_fragment_origin;
            }
        }

        if (horizontal_sequence && !vertical_writing && descendant_block &&
            !layout_block_is_out_of_flow_positioned(descendant_block)) {
            if (nested_horizontal_flow_end <= original_y) {
                nested_horizontal_flow_end = original_y + max(
                    descendant_block->height, 0.0f);
            }
            nested_horizontal_flow_cursor = max(
                nested_horizontal_flow_cursor, nested_horizontal_flow_end);
        }

        bool preserve_escaping_spanner_descendants = nested_descendants_local &&
            child_is_multicol && descendant_block &&
            multicol_has_direct_spanner_child(descendant_block);
        if (preserve_escaping_spanner_descendants) {
            // css multicol: an escaping spanner is positioned in the ancestor
            // flow, while its wrapper's ordinary descendants stay local to
            // the wrapper's border box.
            descendant->x = new_x;
            descendant->y = new_y;
        } else if (descendant->is_block() && !descendant_fragmented) {
            // css fragmentation: moving a non-fragmented child changes its
            // containing-block origin; its descendants remain local to it.
            descendant->x = new_x;
            descendant->y = new_y;
        } else if (has_block_flow_child) {
            // css fragmentation: text rects keep their projected coordinates when
            // a list-item's fragment geometry is repositioned.
            layout_shift_view_tree_geometry(
                descendant, new_x - descendant->x, new_y - descendant->y);
        } else {
            layout_shift_view_tree(descendant, new_x - descendant->x, new_y - descendant->y);
        }
        if (preserve_escaping_spanner_descendants) {
            for (View* nested_child = descendant_block->first_placed_child();
                 nested_child; nested_child = nested_child->next()) {
                if (ViewBlock* nested_block = lam::view_as_block(nested_child)) {
                    multicol_reanchor_br_only_block(lycon, nested_block);
                }
            }
        }
        if (nested_multicol_spanner_projection && descendant_block &&
            (multicol_is_spanner_block(descendant_block) ||
             descendant_fragmented)) {
            float text_y = descendant->y +
                (descendant_fragmented ? local_y : 0.0f);
            // css multicol: nested text rects remain local to the projected
            // fragment; the containing block supplies the page coordinate.
            multicol_reanchor_text_descendants(descendant, 0.0f, text_y);
        }
        if (forced_break_flow && descendant_block && descendant_block->blk &&
            multicol_forces_column_break(descendant_block->block()->break_after)) {
            forced_break_fragment = max(forced_break_fragment, placement.fragment_index + 1);
            forced_break_origin = original_y + descendant_block->height;
            has_forced_break_origin = true;
        }
        if (use_subslot_flow) {
            subslot_flow_y += descendant_flow_height;
        }
        previous_nested_overwide = nested_overwide_inline;
        descendant = next;
    }
    if (horizontal_sequence && !vertical_writing) {
        multicol_reanchor_nested_overwide_linebreaks(child, horizontal_sequence);
    }
    if (vertical_writing && child->display.inner == CSS_VALUE_TABLE) {
        for (View* table_child = child->first_placed_child(); table_child;
             table_child = table_child->next()) {
            ViewBlock* table_child_block = lam::view_as_block(table_child);
            if (!table_child_block ||
                table_child_block->display.inner == CSS_VALUE_TABLE_CAPTION ||
                table_child_block->as_element()->layout_fragments_count() > 1) {
                continue;
            }
            if (layout_block_writing_mode(parent_block) == WM_VERTICAL_RL &&
                layout_view_uses_table_grid_coordinates(table_child)) {
                // css writing modes: vertical-rl grid children are positioned
                // by the mirrored edge mapping above.
                continue;
            }
            // css writing modes: an unfragmented table child remains on the
            // first fragment's physical block-start edge after projection.
            table_child->x += initial_fragment_offset;
        }
    }
    return projected_fragment_count;
}

static bool multicol_fit_nested_auto_multicol_fragment(
    LayoutContext* lycon,
    ViewBlock* child,
    float fragment_height,
    float column_width,
    float column_gap
) {
    if (!lycon || !child || !child->multicol_prop() ||
        !is_multicol_container(child) || fragment_height <= 0.0f ||
        child->height <= fragment_height + 0.5f ||
        (child->blk && child->block()->given_height >= 0.0f) ||
        multicol_content_box_height_limit(child) >= 0.0f) {
        return false;
    }

    // css multicol: an auto-height nested multicol uses the remaining space
    // when its break-before:avoid keeps the container in this fragmentainer.
    float original_width = child->width;
    int inner_column_count = 1;
    float inner_column_width = column_width;
    float inner_column_gap = 0.0f;
    calculate_multicol_dimensions(
        child->multicol_prop(), column_width, multicol_normal_gap_size(child),
        &inner_column_count, &inner_column_width, &inner_column_gap);
    if (inner_column_width <= 0.0f) inner_column_width = column_width;
    if (child->height > fragment_height * inner_column_count + 0.5f) {
        // css-break: an inner avoid constraint outranks the outer
        // break-before:avoid only while the inner columns can hold the flow.
        return false;
    }
    child->width = inner_column_width;
    child->height = fragment_height;
    child->content_height = fragment_height;
    multicol_project_fragmented_descendants(
        lycon, child, fragment_height, 1, column_width, column_gap,
        fragment_height, 0.0f, true);
    child->width = original_width;
    return true;
}

static bool multicol_should_fragment_monolithic_child(
    ViewBlock* container,
    ViewBlock* child,
    float item_height,
    float fragment_height
) {
    if (!container || !container->multicol_prop() || !child) return false;
    bool has_definite_max_height = container->blk && container->block_mut()->given_max_height >= 0;
    bool has_definite_height = container->blk && container->block_mut()->given_height >= 0;
    bool child_is_multicol = child->multicol_prop() && is_multicol_container(child);
    bool has_fragmentainer_height =
        container->multicol_prop()->column_height_is_specified ||
        (container->multicol_prop()->fill == COLUMN_FILL_BALANCE &&
         has_definite_height) ||
        (container->multicol_prop()->wrap == COLUMN_WRAP_WRAP &&
         multicol_content_box_height_limit(container) > 0) ||
        (container->multicol_prop()->fill == COLUMN_FILL_AUTO &&
         (has_definite_max_height || has_definite_height) &&
         multicol_content_box_height_limit(container) > 0);
    if (!has_fragmentainer_height) return false;
    if (fragment_height <= 0) return false;
    if (container->multicol_prop()->wrap == COLUMN_WRAP_NOWRAP) return false;
    if (container->multicol_prop()->wrap == COLUMN_WRAP_AUTO &&
        container->multicol_prop()->fill != COLUMN_FILL_AUTO &&
        container->multicol_prop()->fill != COLUMN_FILL_BALANCE &&
        has_definite_height && !child_is_multicol) {
        return false;
    }
    return item_height > fragment_height;
}

static void multicol_clear_layout_fragments(ViewBlock* block) {
    if (!block) return;
    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(block);
    elem->set_layout_fragment_list(nullptr);
    elem->layout_fragments_count_ref() = 0;
}

static float multicol_stored_fragment_flow_height(
    ViewBlock* child,
    float fallback
) {
    if (!child) return fallback;
    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(child);
    LayoutFragmentBox* fragment = elem->layout_fragment_list();
    float flow_height = 0.0f;
    while (fragment) {
        if (fragment->height > 0.0f) flow_height += fragment->height;
        fragment = fragment->next;
    }
    return flow_height > 0.0f ? flow_height : fallback;
}

static Pool* multicol_layout_fragment_pool(DomElement* elem) {
    if (!elem || !elem->doc) return nullptr;
    if (elem->doc->view_tree && elem->doc->view_tree->prop_pool) {
        return elem->doc->view_tree->prop_pool;
    }
    return elem->doc->document_pool;
}

static void multicol_store_layout_fragments(
    ViewBlock* child,
    int fragment_count,
    int column_count,
    float fragment_height,
    float column_width,
    float column_gap,
    float row_gap,
    float fragment_visual_width,
    float initial_fragment_offset,
    bool zero_height_fragmentainer
) {
    if (!child || fragment_count <= 1 || column_count <= 0 ||
        fragment_height < 0.0f || (fragment_height == 0.0f &&
            !zero_height_fragmentainer)) {
        multicol_clear_layout_fragments(child);
        return;
    }

    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(child);
    elem->set_layout_fragment_list(nullptr);
    elem->layout_fragments_count_ref() = 0;

    Pool* pool = multicol_layout_fragment_pool(elem);
    if (!pool) return;

    LayoutFragmentBox* first = nullptr;
    LayoutFragmentBox* prev = nullptr;
    bool vertical_writing = layout_block_inline_axis_is_vertical(child);
    ViewBlock* parent_block = lam::view_as_block(child->parent);
    bool parent_is_vertical_multicol = vertical_writing && parent_block &&
        parent_block->multicol_prop() && is_multicol_container(parent_block);
    // css writing modes: a direct child fragment's inline coordinate is
    // measured in the parent multicol's physical inline axis; preserving the
    // placed origin lets the parent republish a continuation in its column.
    float fragment_inline_origin = parent_is_vertical_multicol ? child->y : 0.0f;
    float fragment_capacity = zero_height_fragmentainer
        ? 1.0f : fragment_height;
    bool use_logical_vertical_fragments = vertical_writing &&
        child->display.inner != CSS_VALUE_TABLE &&
        multicol_has_in_flow_block_child(child);
    float remaining_extent = use_logical_vertical_fragments
        ? child->width : child->height;
    if (remaining_extent <= 0) remaining_extent = fragment_count * fragment_capacity;

    if (initial_fragment_offset < 0.0f) initial_fragment_offset = 0.0f;
    if (!zero_height_fragmentainer && initial_fragment_offset >= fragment_height) {
        initial_fragment_offset = fmodf(initial_fragment_offset, fragment_capacity);
    }

    for (int fi = 0; fi < fragment_count; fi++) {
        LayoutFragmentBox* fragment = (LayoutFragmentBox*)pool_calloc(pool, sizeof(LayoutFragmentBox));
        if (!fragment) break;

        int column_index = fi % column_count;
        int row_index = fi / column_count;
        float fragment_y = row_index * (fragment_height + row_gap);
        float piece_capacity = fi == 0
            ? fragment_capacity - initial_fragment_offset : fragment_capacity;
        float fragment_piece_extent = remaining_extent > piece_capacity
            ? piece_capacity : remaining_extent;
        if (fragment_piece_extent <= 0) {
            if (vertical_writing && !use_logical_vertical_fragments &&
                child->height > 0.0f) {
                // css writing modes: a block-axis continuation keeps the
                // element's inline-size on every fragment.
                fragment_piece_extent = child->height;
            } else {
                fragment_piece_extent = fragment_height;
            }
        }

        fragment->fragment_index = fi;
        fragment->column_index = column_index;
        fragment->row_index = row_index;
        if (vertical_writing) {
            // Fragment offsets follow the logical block/inline axes rather
            // than retaining the horizontal serializer's x/y convention.
            fragment->x = fi == 0 ? initial_fragment_offset : 0.0f;
            fragment->y = fragment_inline_origin +
                column_index * (column_width + column_gap) +
                row_index * (column_width + column_gap);
            fragment->width = fragment_visual_width;
            fragment->height = fragment_piece_extent;
        } else {
            fragment->x = column_index * (column_width + column_gap);
            fragment->y = fragment_y + (fi == 0 ? initial_fragment_offset : 0.0f);
            fragment->width = fragment_visual_width;
            fragment->height = fragment_piece_extent;
        }
        fragment->next = nullptr;

        if (!first) first = fragment;
        if (prev) prev->next = fragment;
        prev = fragment;
        elem->layout_fragments_count_ref()++;
        remaining_extent -= fragment_piece_extent;
    }

    elem->set_layout_fragment_list(first);
}

static bool multicol_collect_grid_rows(
    ViewBlock* grid,
    MulticolGridRowData* rows
) {
    if (!grid || grid->display.inner != CSS_VALUE_GRID || !rows) return false;

    rows->row_count = 0;
    for (int i = 0; i < MAX_MULTICOL_BLOCKS; i++) {
        rows->row_top[i] = 0.0f;
        rows->row_bottom[i] = 0.0f;
        rows->row_seen[i] = false;
    }

    bool has_item = false;
    for (View* view = grid->first_placed_child(); view; view = view->next()) {
        ViewBlock* item = lam::view_as_block(view);
        if (!item || layout_block_is_out_of_flow_positioned(item) ||
            item->view_type == RDT_VIEW_MARKER) {
            continue;
        }
        GridItemProp* grid_item = grid_item_prop(item);
        if (!grid_item) return false;

        int row_start = grid_item->computed_grid_row_start - 1;
        int row_end = grid_item->computed_grid_row_end - 1;
        if (row_start < 0 || row_end != row_start + 1 ||
            row_start >= MAX_MULTICOL_BLOCKS) {
            // a spanning grid item needs fragment-aware grid placement;
            // independent row projection cannot preserve its grid area.
            return false;
        }

        float row_top = grid_item->track_base_y;
        float row_height = grid_item->track_area_height;
        if (row_height <= 0.0f) {
            row_top = item->y;
            row_height = item->height;
        }
        float row_bottom = row_top + row_height;
        if (row_bottom < row_top) return false;

        if (!rows->row_seen[row_start]) {
            rows->row_top[row_start] = row_top;
            rows->row_bottom[row_start] = row_bottom;
            rows->row_seen[row_start] = true;
        } else {
            if (row_top < rows->row_top[row_start]) {
                rows->row_top[row_start] = row_top;
            }
            if (row_bottom > rows->row_bottom[row_start]) {
                rows->row_bottom[row_start] = row_bottom;
            }
        }
        if (row_start + 1 > rows->row_count) {
            rows->row_count = row_start + 1;
        }
        has_item = true;
    }

    if (!has_item || rows->row_count <= 0) return false;
    for (int row = 0; row < rows->row_count; row++) {
        if (!rows->row_seen[row] || rows->row_bottom[row] < rows->row_top[row]) {
            // an unoccupied track has no item geometry from which to preserve
            // its break boundary, so leave it to the generic path.
            return false;
        }
    }
    return true;
}

static int multicol_grid_row_fragment_count(
    const MulticolGridRowData* rows,
    float fragment_height
) {
    if (!rows || rows->row_count <= 0 || fragment_height <= 0.0f) return 0;

    int fragment_count = 1;
    float fragment_start = rows->row_top[0];
    for (int row = 1; row < rows->row_count; row++) {
        if (rows->row_bottom[row] - fragment_start > fragment_height + 0.5f) {
            fragment_count++;
            fragment_start = rows->row_top[row];
        }
    }
    return fragment_count;
}

static float multicol_grid_min_fragmentainer_height(
    ViewBlock* grid,
    int column_count
) {
    if (!grid || column_count <= 0) return 0.0f;

    MulticolGridRowData rows;
    if (!multicol_collect_grid_rows(grid, &rows)) return 0.0f;

    float lower = 0.0f;
    float upper = rows.row_bottom[rows.row_count - 1] - rows.row_top[0];
    for (int row = 0; row < rows.row_count; row++) {
        float row_height = rows.row_bottom[row] - rows.row_top[row];
        if (row_height > lower) lower = row_height;
    }
    if (upper < lower) upper = lower;
    for (int step = 0; step < 16 && upper - lower > 0.01f; step++) {
        float middle = (lower + upper) * 0.5f;
        if (multicol_grid_row_fragment_count(&rows, middle) <= column_count) {
            upper = middle;
        } else {
            lower = middle;
        }
    }
    return ceilf(upper);
}

static bool multicol_fragment_grid_rows(
    ViewBlock* container,
    ViewBlock* child,
    float item_height,
    float fragment_height,
    int column_count,
    float column_width,
    float column_gap,
    float initial_fragment_offset,
    int* out_used_columns,
    float* out_union_height
) {
    if (out_used_columns) *out_used_columns = 0;
    if (out_union_height) *out_union_height = 0.0f;
    if (!container || !child || child->display.inner != CSS_VALUE_GRID ||
        item_height <= fragment_height || fragment_height <= 0.0f ||
        column_count <= 1 || column_width <= 0.0f ||
        fabsf(initial_fragment_offset) > 0.5f ||
        multicol_has_vertical_inline_axis(container) ||
        multicol_has_spanner_child(container)) {
        return false;
    }

    MulticolGridRowData rows;
    if (!multicol_collect_grid_rows(child, &rows)) return false;

    float required_height = multicol_grid_min_fragmentainer_height(
        child, column_count);
    if (required_height <= 0.0f) return false;
    float used_fragment_height = max(fragment_height, required_height);
    int fragment_count = multicol_grid_row_fragment_count(
        &rows, used_fragment_height);
    if (fragment_count <= 1) return false;

    int layout_column_count = column_count;
    int used_columns = min(column_count, fragment_count);
    int row_count = (fragment_count + layout_column_count - 1) /
        layout_column_count;
    float row_gap = multicol_row_gap(container);
    if (row_gap < 0.0f) row_gap = 0.0f;
    float union_height = multicol_group_wraps_rows(container)
        ? row_count * used_fragment_height + (row_count - 1) * row_gap
        : used_fragment_height;
    float union_width = column_width +
        (used_columns - 1) * (column_width + column_gap);

    // Grid rows are the break units; preserve each item’s alignment within its
    // row while translating the complete row to its fragmentainer.
    multicol_store_layout_fragments(
        child, fragment_count, layout_column_count, used_fragment_height,
        column_width, column_gap, row_gap, column_width, 0.0f);
    if (child->width < union_width) child->width = union_width;
    child->height = union_height;
    child->content_height = union_height;
    multicol_reposition_abs_children_for_fragmented_cb(nullptr, static_cast<View*>(child));

    int fragment_index = 0;
    float fragment_start_top = rows.row_top[0];
    for (int row = 1; row < rows.row_count; row++) {
        if (rows.row_bottom[row] - fragment_start_top >
            used_fragment_height + 0.5f) {
            fragment_index++;
            fragment_start_top = rows.row_top[row];
        }

        for (View* view = child->first_placed_child(); view; view = view->next()) {
            ViewBlock* item = lam::view_as_block(view);
            if (!item || layout_block_is_out_of_flow_positioned(item) ||
                item->view_type == RDT_VIEW_MARKER) {
                continue;
            }
            GridItemProp* grid_item = grid_item_prop(item);
            if (!grid_item) return false;
            int item_row = grid_item->computed_grid_row_start - 1;
            if (item_row != row) continue;

            int column_index = fragment_index % layout_column_count;
            int outer_row_index = fragment_index / layout_column_count;
            float target_row_top = child->y +
                outer_row_index * (used_fragment_height + row_gap) +
                (rows.row_top[item_row] - fragment_start_top);
            float target_x = item->x +
                column_index * (column_width + column_gap);
            float target_y = target_row_top +
                (item->y - rows.row_top[item_row]);
            // grid item descendants use the item’s local coordinate space;
            // translating the subtree would apply the fragment offset twice.
            item->x = target_x;
            item->y = target_y;
        }
    }

    // The first row is not visited by the transition loop above.
    for (View* view = child->first_placed_child(); view; view = view->next()) {
        ViewBlock* item = lam::view_as_block(view);
        if (!item || layout_block_is_out_of_flow_positioned(item) ||
            item->view_type == RDT_VIEW_MARKER) {
            continue;
        }
        GridItemProp* grid_item = grid_item_prop(item);
        if (!grid_item || grid_item->computed_grid_row_start != 1) continue;
        float target_y = child->y + (item->y - rows.row_top[0]);
        item->y = target_y;
    }

    if (out_used_columns) *out_used_columns = used_columns;
    if (out_union_height) *out_union_height = union_height;
    return true;
}

static bool multicol_fragment_abspos_in_context(
    LayoutContext* lycon, ViewBlock* containing_block, ViewBlock* child) {
    if (!containing_block || !child || !child->is_element() || !child->position ||
        child->positionp()->position != CSS_VALUE_ABSOLUTE) return false;

    ViewBlock* multicol = nullptr;
    for (ViewElement* ancestor = containing_block; ancestor;
         ancestor = ancestor->parent_view()) {
        if (!ancestor->is_block()) continue;
        ViewBlock* candidate = lam::view_require_block(ancestor);
        if (is_multicol_container(candidate)) {
            multicol = candidate;
            break;
        }
    }
    if (!multicol || !multicol->multicol_prop()) return false;

    const MultiColumnProp* prop = multicol->multicol_prop();
    int local_column_count = prop->computed_column_count > 0
        ? prop->computed_column_count : prop->column_count;
    if (local_column_count < 1) local_column_count = 1;
    float local_column_width = prop->computed_column_width;
    if (local_column_width <= 0.0f) {
        local_column_width = multicol->width / local_column_count;
    }
    if (local_column_width <= 0.0f) return false;
    float local_column_gap = multicol_column_gap(multicol);

    DomElement* multicol_elem = lam::dom_require<DOM_NODE_ELEMENT>(multicol);
    DomElement* containing_elem = lam::dom_require<DOM_NODE_ELEMENT>(containing_block);
    LayoutFragmentBox* context_fragments = containing_elem->layout_fragment_list();
    int context_fragment_count = containing_elem->layout_fragments_count();
    if (!context_fragments || context_fragment_count <= 1) {
        context_fragments = multicol_elem->layout_fragment_list();
        context_fragment_count = multicol_elem->layout_fragments_count();
    }
    bool has_context_fragments = context_fragments && context_fragment_count > 1;
    bool containing_block_is_fragmented = containing_block != multicol &&
        containing_block->as_element()->layout_fragments_count() > 1;
    bool context_is_parent = has_context_fragments &&
        (containing_block_is_fragmented ||
         (multicol->parent_view() && multicol->parent_view()->is_block() &&
          is_multicol_container(lam::view_require_block(multicol->parent_view()))) ||
         context_fragments->width > local_column_width + 0.5f);
    if (!context_is_parent) return false;

    int slot_count = context_is_parent
        ? context_fragment_count * local_column_count
        : has_context_fragments ? context_fragment_count : local_column_count;
    if (slot_count <= 1) return false;

    bool vertical_writing = multicol_has_vertical_inline_axis(multicol);
    if (vertical_writing) {
        // css-position §4.4: vertical abspos fragments break on the physical
        // block axis, while each nested column keeps its inline-axis origin.
        ViewBlock* parent_multicol = nullptr;
        for (ViewElement* ancestor = multicol->parent_view(); ancestor;
             ancestor = ancestor->parent_view()) {
            if (!ancestor->is_block()) continue;
            ViewBlock* candidate = lam::view_require_block(ancestor);
            if (is_multicol_container(candidate)) {
                parent_multicol = candidate;
                break;
            }
        }

        float fragment_capacity = parent_multicol
            ? multicol_content_box_height_limit(parent_multicol)
            : layout_content_size_from_border_box(
                multicol, multicol->width, true);
        if (fragment_capacity <= 0.0f) fragment_capacity = multicol->width;
        if (fragment_capacity <= 0.0f) return false;

        float continuous_extent = child->content_width > child->width
            ? child->content_width : child->width;
        if (child->blk && layout_axis_has_given_size(child, true)) {
            float given_width = layout_axis_given_size(
                child->block(), LAYOUT_AXIS_X);
            continuous_extent = max(continuous_extent,
                layout_used_border_box_size(child, given_width, true));
        }
        if (continuous_extent <= 0.0f) return false;
        float fragment_inline_size = child->height;
        if (child->blk && layout_axis_has_given_size(child, false)) {
            float given_height = layout_axis_given_size(
                child->block(), LAYOUT_AXIS_Y);
            fragment_inline_size = layout_used_border_box_size(
                child, given_height, false);
        }
        if (fragment_inline_size <= 0.0f) return false;

        float static_block_offset = 0.0f;
        for (View* sibling = containing_block->first_placed_child();
             sibling && sibling != static_cast<View*>(child);
             sibling = sibling->next()) {
            ViewBlock* sibling_block = lam::view_as_block(sibling);
            if (!sibling_block || layout_block_is_out_of_flow_positioned(sibling_block) ||
                sibling_block->view_type == RDT_VIEW_MARKER ||
                !layout_view_is_block_flow_box(sibling_block)) {
                continue;
            }
            float sibling_extent = sibling_block->width;
            if (sibling_block->blk && layout_axis_has_given_size(sibling_block, true)) {
                float given_width = layout_axis_given_size(
                    sibling_block->block(), LAYOUT_AXIS_X);
                sibling_extent = layout_used_border_box_size(
                    sibling_block, given_width, true);
            }
            float margin_before = 0.0f;
            float margin_after = 0.0f;
            multicol_flow_margins(containing_block, sibling_block,
                                  &margin_before, &margin_after);
            static_block_offset += sibling_extent + margin_before + margin_after;
        }

        int parent_column_count = 1;
        float parent_column_inline_extent = 0.0f;
        float parent_column_gap = 0.0f;
        float parent_inline_origin = multicol->y - containing_block->y;
        bool parent_direction_rtl = false;
        if (parent_multicol && parent_multicol->multicol_prop()) {
            const MultiColumnProp* parent_prop = parent_multicol->multicol_prop();
            parent_column_count = parent_prop->computed_column_count > 0
                ? parent_prop->computed_column_count : parent_prop->column_count;
            if (parent_column_count < 1) parent_column_count = 1;
            parent_column_inline_extent = parent_prop->computed_column_width;
            parent_column_gap = multicol_column_gap(parent_multicol);
            parent_direction_rtl = parent_multicol->blk &&
                parent_multicol->block()->direction == CSS_VALUE_RTL;
        }
        if (parent_column_inline_extent <= 0.0f) {
            parent_column_inline_extent = parent_multicol
                ? parent_multicol->height / parent_column_count
                : local_column_width;
        }
        if (parent_column_inline_extent <= 0.0f) return false;

        float nested_inline_extent = parent_column_inline_extent;
        int nested_column_count = local_column_count;
        float nested_column_width = local_column_width;
        float nested_column_gap = local_column_gap;
        if (multicol->multicol_prop()) {
            calculate_multicol_dimensions(
                multicol->multicol_prop(), nested_inline_extent,
                multicol_normal_gap_size(multicol), &nested_column_count,
                &nested_column_width, &nested_column_gap);
        }
        if (nested_column_count < 1) nested_column_count = 1;
        if (nested_column_width <= 0.0f) {
            nested_column_width = nested_inline_extent / nested_column_count;
        }

        int first_parent_column = (int)floorf(
            static_block_offset / fragment_capacity); // INT_CAST_OK: fragment index from positive block offset
        float initial_block_offset = static_block_offset -
            first_parent_column * fragment_capacity;
        if (initial_block_offset < 0.0f) initial_block_offset = 0.0f;
        int fragment_count = (int)ceilf(
            (initial_block_offset + continuous_extent) /
                fragment_capacity); // INT_CAST_OK: fragment count from positive block extent
        if (fragment_count <= 1) return false;

        DomElement* child_elem = lam::dom_require<DOM_NODE_ELEMENT>(child);
        Pool* pool = multicol_layout_fragment_pool(child_elem);
        if (!pool) return false;
        child_elem->set_layout_fragment_list(nullptr);
        child_elem->layout_fragments_count_ref() = 0;

        float min_x = FLT_MAX;
        float max_x = -FLT_MAX;
        float min_y = FLT_MAX;
        float max_y = -FLT_MAX;
        LayoutFragmentBox* first = nullptr;
        LayoutFragmentBox* previous = nullptr;
        float remaining_extent = continuous_extent;
        int parent_column = first_parent_column;
        int nested_column = 0;
        float block_offset = initial_block_offset;
        WritingMode writing_mode = layout_block_writing_mode(multicol);
        for (int fi = 0; fi < fragment_count && remaining_extent > 0.0f; fi++) {
            float piece_extent = min(
                remaining_extent, fragment_capacity - block_offset);
            if (piece_extent <= 0.0f) piece_extent = min(
                remaining_extent, fragment_capacity);

            int visual_parent_column = parent_column % parent_column_count;
            if (parent_direction_rtl) {
                visual_parent_column = parent_column_count - 1 -
                    visual_parent_column;
            }
            float fragment_y = parent_inline_origin +
                visual_parent_column *
                    (parent_column_inline_extent + parent_column_gap) +
                nested_column * (nested_column_width + nested_column_gap);
            float fragment_x = 0.0f;
            if (writing_mode == WM_VERTICAL_RL &&
                piece_extent < fragment_capacity && fi == fragment_count - 1) {
                fragment_x = fragment_capacity - piece_extent;
            }

            LayoutFragmentBox* fragment = (LayoutFragmentBox*)pool_calloc(
                pool, sizeof(LayoutFragmentBox));
            if (!fragment) break;
            fragment->x = fragment_x;
            fragment->y = fragment_y;
            fragment->width = piece_extent;
            fragment->height = fragment_inline_size;
            fragment->fragment_index = fi;
            fragment->column_index = visual_parent_column;
            fragment->row_index = parent_column / parent_column_count;
            fragment->next = nullptr;
            if (!first) first = fragment;
            if (previous) previous->next = fragment;
            previous = fragment;
            child_elem->layout_fragments_count_ref()++;
            min_x = min(min_x, fragment_x);
            max_x = max(max_x, fragment_x + piece_extent);
            min_y = min(min_y, fragment_y);
            max_y = max(max_y, fragment_y + fragment_inline_size);

            remaining_extent -= piece_extent;
            block_offset = 0.0f;
            nested_column++;
            if (nested_column >= nested_column_count) {
                nested_column = 0;
                parent_column++;
            }
        }

        if (!first || child_elem->layout_fragments_count() <= 1) {
            child_elem->set_layout_fragment_list(nullptr);
            child_elem->layout_fragments_count_ref() = 0;
            return false;
        }
        child_elem->set_layout_fragment_list(first);
        child->x = min_x;
        child->y = min_y;
        child->width = max_x - min_x;
        child->height = max_y - min_y;
        return true;
    }

    ViewBlock* parent_multicol = nullptr;
    for (ViewElement* ancestor = multicol->parent_view(); ancestor;
         ancestor = ancestor->parent_view()) {
        if (!ancestor->is_block()) continue;
        ViewBlock* candidate = lam::view_require_block(ancestor);
        if (is_multicol_container(candidate)) {
            parent_multicol = candidate;
            break;
        }
    }
    if (parent_multicol && parent_multicol->multicol_prop()) {
        float fragment_capacity = multicol_content_box_height_limit(parent_multicol);
        if (fragment_capacity <= 0.0f) fragment_capacity = parent_multicol->height;
        int parent_column_count = parent_multicol->multicol_prop()->computed_column_count > 0
            ? parent_multicol->multicol_prop()->computed_column_count
            : parent_multicol->multicol_prop()->column_count;
        if (parent_column_count < 1) parent_column_count = 1;
        float parent_column_width = parent_multicol->multicol_prop()->computed_column_width;
        if (parent_column_width <= 0.0f) {
            parent_column_width = parent_multicol->width / parent_column_count;
        }
        float parent_column_gap = multicol_column_gap(parent_multicol);
        float initial_fragment_offset = 0.0f;
        LayoutFragmentBox* first_multicol_fragment =
            multicol_elem->layout_fragment_list();
        if (first_multicol_fragment && first_multicol_fragment->y > 0.0f &&
            first_multicol_fragment->y < fragment_capacity) {
            initial_fragment_offset = first_multicol_fragment->y;
        }
        MulticolNestedHorizontalSequence sequence = {};
        if (fragment_capacity > 0.0f &&
            multicol_init_nested_horizontal_sequence(
                parent_multicol, multicol, fragment_capacity,
                parent_column_count, parent_column_width, parent_column_gap,
                initial_fragment_offset, &sequence)) {
            float static_block_offset = 0.0f;
            for (View* sibling = containing_block->first_placed_child();
                 sibling && sibling != static_cast<View*>(child);
                 sibling = sibling->next()) {
                ViewBlock* sibling_block = lam::view_as_block(sibling);
                if (!sibling_block ||
                    layout_block_is_out_of_flow_positioned(sibling_block) ||
                    sibling_block->view_type == RDT_VIEW_MARKER ||
                    !layout_view_is_block_flow_box(sibling_block)) {
                    continue;
                }
                float sibling_extent = max(sibling_block->height, 0.0f);
                if (sibling_block->blk &&
                    layout_axis_has_given_size(sibling_block, false)) {
                    float given_height = layout_axis_given_size(
                        sibling_block->block(), LAYOUT_AXIS_Y);
                    sibling_extent = max(sibling_extent,
                        layout_used_border_box_size(
                            sibling_block, given_height, false));
                }
                float margin_before = 0.0f;
                float margin_after = 0.0f;
                multicol_flow_margins(containing_block, sibling_block,
                                      &margin_before, &margin_after);
                static_block_offset += sibling_extent + margin_before +
                    margin_after;
            }

            float flow_extent = max(child->height, 0.0f);
            if (child->blk && layout_axis_has_given_size(child, false)) {
                float given_height = layout_axis_given_size(
                    child->block(), LAYOUT_AXIS_Y);
                flow_extent = max(flow_extent,
                    layout_used_border_box_size(child, given_height, false));
            }
            float fragment_width = sequence.nested_column_width;
            if (child->blk && layout_axis_has_given_size(child, true)) {
                float given_width = layout_axis_given_size(
                    child->block(), LAYOUT_AXIS_X);
                fragment_width = layout_used_border_box_size(
                    child, given_width, true);
            }
            float min_x = 0.0f;
            float min_y = 0.0f;
            float max_x = 0.0f;
            float max_y = 0.0f;
            int fragment_count = 0;
            if (multicol_store_nested_horizontal_fragments(
                    child, &sequence, static_block_offset, flow_extent,
                    fragment_width, &min_x, &min_y, &max_x, &max_y,
                    &fragment_count)) {
                child->x = containing_block->x + min_x;
                child->y = containing_block->y + min_y;
                child->width = max_x - min_x;
                child->height = max_y - min_y;
                return fragment_count > 1;
            }
        }
    }

    float fragment_height = 0.0f;
    if (has_context_fragments) {
        fragment_height = context_fragments->height;
    } else {
        fragment_height = multicol_content_box_height_limit(multicol);
    }
    if (fragment_height <= 0.0f) return false;

    float continuous_height = child->content_height > child->height
        ? child->content_height : child->height;
    if (child->blk && layout_axis_has_given_size(child, false)) {
        float given_height = layout_axis_given_size(child->block(), LAYOUT_AXIS_Y);
        if (given_height > continuous_height) continuous_height = given_height;
    }
    int fragment_count = (int)ceilf(continuous_height / fragment_height); // INT_CAST_OK: fragment count from positive heights
    if (fragment_count <= 1) return false;

    DomElement* child_elem = lam::dom_require<DOM_NODE_ELEMENT>(child);
    float fragment_visual_width = child->width;
    if (child_elem->layout_fragment_list() &&
        child_elem->layout_fragments_count() > 1 &&
        child_elem->layout_fragment_list()->width > 0.0f) {
        // Repeated finalization sees the already-published visual union;
        // retain the width of one continuous fragment for the next union.
        fragment_visual_width = child_elem->layout_fragment_list()->width;
    }
    if (fragment_visual_width <= 0.0f) fragment_visual_width = local_column_width;

    Pool* pool = multicol_layout_fragment_pool(child_elem);
    if (!pool) return false;
    child_elem->set_layout_fragment_list(nullptr);
    child_elem->layout_fragments_count_ref() = 0;

    float first_slot_x = 0.0f;
    float first_slot_y = 0.0f;
    bool have_first_slot = false;
    float min_x = FLT_MAX;
    float max_x = -FLT_MAX;
    float min_y = FLT_MAX;
    float max_y = -FLT_MAX;
    LayoutFragmentBox* first = nullptr;
    LayoutFragmentBox* previous = nullptr;
    float remaining_height = continuous_height;
    for (int fi = 0; fi < fragment_count; fi++) {
        int slot_index = fi;
        int row_index = 0;
        if (slot_index >= slot_count) {
            row_index = slot_index / slot_count;
            slot_index %= slot_count;
        }

        float slot_x = 0.0f;
        float slot_y = row_index * fragment_height;
        int column_index = slot_index;
        if (context_is_parent) {
            int parent_index = slot_index / local_column_count;
            int local_index = slot_index % local_column_count;
            LayoutFragmentBox* parent_fragment = context_fragments;
            for (int index = 0; parent_fragment && index < parent_index; index++) {
                parent_fragment = parent_fragment->next;
            }
            if (!parent_fragment) return false;
            // css-position §4.4: an abspos box is broken in the continuous
            // nested column sequence; parent fragment offsets affect the
            // fragmentainer height, not the child sequence's inline pitch.
            slot_x = slot_index * (local_column_width + local_column_gap);
            slot_y = parent_fragment->y + row_index * fragment_height;
            column_index = local_index;
        } else if (has_context_fragments) {
            LayoutFragmentBox* context_fragment = context_fragments;
            for (int index = 0; context_fragment && index < slot_index; index++) {
                context_fragment = context_fragment->next;
            }
            if (!context_fragment) return false;
            slot_x = context_fragment->x;
            slot_y += context_fragment->y;
            column_index = context_fragment->column_index;
        } else {
            slot_x = slot_index * (local_column_width + local_column_gap);
        }

        if (!have_first_slot) {
            first_slot_x = slot_x;
            first_slot_y = slot_y;
            have_first_slot = true;
        }
        float local_x = slot_x - first_slot_x;
        float local_y = slot_y - first_slot_y;
        float piece_height = min(remaining_height, fragment_height);
        if (piece_height <= 0.0f) piece_height = fragment_height;

        LayoutFragmentBox* fragment = (LayoutFragmentBox*)pool_calloc(
            pool, sizeof(LayoutFragmentBox));
        if (!fragment) break;
        fragment->x = local_x;
        fragment->y = local_y;
        fragment->width = fragment_visual_width;
        fragment->height = piece_height;
        fragment->fragment_index = fi;
        fragment->column_index = column_index;
        fragment->row_index = row_index;
        fragment->next = nullptr;
        if (!first) first = fragment;
        if (previous) previous->next = fragment;
        previous = fragment;
        child_elem->layout_fragments_count_ref()++;
        min_x = min(min_x, local_x);
        max_x = max(max_x, local_x + fragment_visual_width);
        min_y = min(min_y, local_y);
        max_y = max(max_y, local_y + piece_height);
        remaining_height -= piece_height;
    }

    if (!first || child_elem->layout_fragments_count() <= 1) {
        child_elem->set_layout_fragment_list(nullptr);
        child_elem->layout_fragments_count_ref() = 0;
        return false;
    }
    child_elem->set_layout_fragment_list(first);
    child->width = max_x - min_x;
    child->height = max_y - min_y;
    if (lycon && child->multicol_prop() && is_multicol_container(child) &&
        local_column_count > 1) {
        // css fragmentation: an absolute multicol's descendants use the
        // containing block's fragmentainer size after its own reanchoring.
        multicol_project_fragmented_descendants(
            lycon, child, fragment_height, local_column_count,
            local_column_width, local_column_gap, fragment_height, 0.0f, true);
    }
    // css-position §4.4: retain continuous used content height while exposing
    // the union of the generated fragmentainers as the visual border box.
    return true;
}

static bool multicol_inline_has_in_flow_fragment(View* view);

static bool multicol_vertical_inline_extent(ViewBlock* cb, float* out_extent) {
    if (!cb || !out_extent) return false;
    for (ViewElement* ancestor = cb; ancestor; ancestor = ancestor->parent_view()) {
        if (!ancestor->is_block()) continue;
        ViewBlock* block = lam::view_require_block(ancestor);
        if (!block->blk || block->block()->given_height < 0.0f) continue;
        BoxMetrics box = layout_box_metrics(block);
        *out_extent = block->block()->given_height +
            box.padding.top + box.padding.bottom;
        return *out_extent > 0.0f;
    }
    return false;
}

static bool multicol_view_direction_is_rtl(View* view) {
    for (ViewElement* ancestor = view ? view->parent_view() : nullptr;
         ancestor; ancestor = ancestor->parent_view()) {
        if (!ancestor->is_element()) continue;
        DomElement* element = ancestor->as_element();
        CssEnum specified = layout_specified_keyword(
            element, CSS_PROPERTY_DIRECTION, CSS_VALUE__UNDEF);
        if (specified == CSS_VALUE_LTR || specified == CSS_VALUE_RTL) {
            return specified == CSS_VALUE_RTL;
        }
        if (element->blk && (element->block()->direction == CSS_VALUE_LTR ||
                             element->block()->direction == CSS_VALUE_RTL)) {
            return element->block()->direction == CSS_VALUE_RTL;
        }
    }
    return false;
}

static void multicol_reposition_abs_children_for_fragmented_cb(
    LayoutContext* lycon, View* cb_view) {
    if (!cb_view || !cb_view->is_element()) return;
    ViewBlock* cb = nullptr;
    if (cb_view->is_block()) {
        cb = lam::view_require_block(cb_view);
    } else if (cb_view->view_type == RDT_VIEW_INLINE) {
        cb = lam::unsafe_view_block_api_span(
            lam::view_require<RDT_VIEW_INLINE>(cb_view));
    } else {
        return;
    }
    if (!cb->position || !cb->positionp()->first_abs_child) return;

    LayoutContainingBlock containing = layout_containing_block_for_view(cb);
    ViewBlock* child = cb->positionp()->first_abs_child;
    while (child) {
        if (multicol_cb_is_bypassed_by_spanner(cb, child)) {
            // CSS Multicol §6.1: a spanner bypasses intermediate ancestors;
            // re-resolving here would incorrectly use that wrapper's fragment box.
            child = child->positionp()->next_abs_sibling;
            continue;
        }
        const PositionProp* position = child->positionp();
        BoxMetrics child_box = layout_box_metrics(child);
        float margin_left = child_box.margin.left;
        float margin_right = child_box.margin.right;
        float margin_top = child_box.margin.top;
        float margin_bottom = child_box.margin.bottom;
        bool direct_inline_cb_child = child->parent == static_cast<DomNode*>(cb);
        bool has_inline_cb_origin = false;
        float inline_cb_origin_y = 0.0f;
        if (layout_block_inline_axis_is_vertical(cb) &&
            cb->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(static_cast<View*>(cb));
            if (span->has_fragment_union(FRAGMENT_UNION_INLINE_CB)) {
                inline_cb_origin_y = span->fragment_union(
                    FRAGMENT_UNION_INLINE_CB)->min_y - cb->y;
                has_inline_cb_origin = true;
            }
        }
        if (position->has_left && !position->has_right) {
            child->x = containing.padding_x + position->left + margin_left;
        } else if (position->has_right && !position->has_left) {
            child->x = containing.padding_x + containing.padding_width -
                position->right - margin_right - child->width;
        }
        if (position->has_top && !position->has_bottom) {
            child->y = containing.padding_y + position->top + margin_top;
        } else if (position->has_bottom && !position->has_top) {
            bool has_fragmented_inline_end = false;
            float fragmented_inline_end = 0.0f;
            if (multicol_inline_has_in_flow_fragment(static_cast<View*>(cb)) &&
                layout_block_inline_axis_is_vertical(cb) && cb->view_type == RDT_VIEW_INLINE) {
                ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(static_cast<View*>(cb));
                if (span->has_fragment_union(FRAGMENT_UNION_INLINE_CB)) {
                    const FragmentUnion* inline_cb =
                        span->fragment_union(FRAGMENT_UNION_INLINE_CB);
                    // CSS Position 3 §2.1: RTL inline containing-block edges
                    // can be reversed in physical coordinates, so ordering is
                    // not a validity test for the end edge.
                    has_fragmented_inline_end = true;
                    fragmented_inline_end = inline_cb->max_y;
                }
            }
            if (has_fragmented_inline_end) {
                // CSS Position 3 §2.1: an inline CB uses the end edge of its
                // end-most fragment; the DOMRect union may include a later
                // continuation that is not that CB edge.
                child->y = fragmented_inline_end - cb->y - position->bottom -
                    margin_bottom - child->height;
            } else {
                child->y = containing.padding_y + containing.padding_height -
                    position->bottom - margin_bottom - child->height;
            }
        }
        float fragmented_inline_extent = 0.0f;
        ViewBlock* fragmentation_ancestor = layout_nearest_block_ancestor(cb);
        bool ltr_fragmentation_context = fragmentation_ancestor &&
            fragmentation_ancestor->block()->direction == CSS_VALUE_LTR;
        bool has_fragmented_inline_extent =
            layout_block_inline_axis_is_vertical(cb) && child->blk &&
            child->block()->direction == CSS_VALUE_RTL &&
            multicol_vertical_inline_extent(cb, &fragmented_inline_extent) &&
            cb->height > fragmented_inline_extent &&
            !(cb->view_type == RDT_VIEW_INLINE && direct_inline_cb_child &&
              ltr_fragmentation_context);
        if (has_fragmented_inline_extent) {
            // CSS Writing Modes: RTL vertical inline positions use the
            // definite logical inline extent, not the visual union of columns.
            if (position->has_top && !position->has_bottom) {
                child->y += fragmented_inline_extent;
            } else if (position->has_bottom && !position->has_top) {
                child->y -= fragmented_inline_extent;
                if (layout_element_was_inline(
                        static_cast<DomElement*>(child), false) &&
                    has_inline_cb_origin) {
                    // CSS Position 3 §4.1: nested inline coordinates retain
                    // the containing block's first-fragment origin.
                    child->y += inline_cb_origin_y;
                }
            }
        }
        multicol_fragment_abspos_in_context(lycon, cb, child);
        child = position->next_abs_sibling;
    }
}

static bool multicol_inline_fragment_bounds_y(View* view, bool first_fragment,
                                               float* out_top, float* out_bottom) {
    if (!view || !out_top || !out_bottom || layout_view_is_out_of_flow(view)) return false;
    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(view);
        TextRect* selected = nullptr;
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->width <= 0.0f || rect->height <= 0.0f ||
                layout_text_rect_content_kind(text, rect) ==
                    LAYOUT_TEXT_RECT_COLLAPSED_WHITESPACE) {
                continue;
            }
            selected = rect;
            if (first_fragment) break;
        }
        if (!selected) return false;
        *out_top = selected->y;
        *out_bottom = selected->y + selected->height;
        return true;
    }
    if (view->view_type == RDT_VIEW_BR) {
        if (view->height <= 0.0f) return false;
        *out_top = view->y;
        *out_bottom = view->y + view->height;
        return true;
    }
    if (view->view_type != RDT_VIEW_INLINE && view->is_block()) {
        if (view->height <= 0.0f) return false;
        *out_top = view->y;
        *out_bottom = view->y + view->height;
        return true;
    }
    if (!view->is_element()) return false;

    // Projected continuation views carry the visual fragment order required
    // by CSS Position 3; DOM source order is not sufficient after bidi layout.
    View* child = lam::view_require_element(view)->first_placed_child();
    bool found = false;
    float top = 0.0f;
    float bottom = 0.0f;
    while (child) {
        float child_top = 0.0f;
        float child_bottom = 0.0f;
        if (multicol_inline_fragment_bounds_y(
                child, first_fragment, &child_top, &child_bottom)) {
            if (first_fragment) {
                *out_top = child_top;
                *out_bottom = child_bottom;
                return true;
            }
            found = true;
            top = child_top;
            bottom = child_bottom;
        }
        child = child->next();
    }
    if (found) {
        *out_top = top;
        *out_bottom = bottom;
    }
    return found;
}

static bool multicol_inline_fragment_edge_y(View* view, bool end_edge, float* out_y) {
    float top = 0.0f;
    float bottom = 0.0f;
    if (!multicol_inline_fragment_bounds_y(view, !end_edge, &top, &bottom)) {
        return false;
    }
    *out_y = end_edge ? bottom : top;
    return true;
}

static bool multicol_inline_has_in_flow_fragment(View* view) {
    if (!view || view->view_type != RDT_VIEW_INLINE) return false;
    View* child = lam::dom_require<DOM_NODE_ELEMENT>(view)->first_child;
    while (child) {
        if (layout_view_is_out_of_flow(child) || child->view_type == RDT_VIEW_NONE) {
            child = child->next_sibling;
            continue;
        }
        if (child->view_type == RDT_VIEW_INLINE) {
            if (multicol_inline_has_in_flow_fragment(child)) return true;
        } else {
            // CSS Position 3 §4.1: nested inline fragments contribute to the
            // containing block of an outer positioned inline.
            return true;
        }
        child = child->next_sibling;
    }
    return false;
}

static void multicol_reposition_fragmented_positioned_subtree(
    LayoutContext* lycon, View* view) {
    if (!view || !view->is_element()) return;
    multicol_reposition_abs_children_for_fragmented_cb(lycon, view);
    View* child = lam::dom_require<DOM_NODE_ELEMENT>(view)->first_child;
    while (child) {
        multicol_reposition_fragmented_positioned_subtree(lycon, child);
        child = child->next_sibling;
    }
}

static void multicol_normalize_vertical_inline_fragment_bounds(View* view) {
    if (!view || !view->is_element()) return;
    if (view->view_type == RDT_VIEW_INLINE) {
        ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(view);
        ViewBlock* ancestor = layout_nearest_block_ancestor(span);
        if (ancestor && layout_block_inline_axis_is_vertical(ancestor) &&
            ancestor->is_element() &&
            ancestor->as_element()->layout_fragments_count() > 1) {
            // CSS Position 3 §4.1: a positioned inline's containing-block
            // origin is the fragmentainer edge, not the old line cursor.
            bool had_inline_cb = span->has_fragment_union(FRAGMENT_UNION_INLINE_CB);
            FragmentUnion* inline_cb = span->ensure_fragment_union(FRAGMENT_UNION_INLINE_CB);
            inline_cb->min_x = span->x;
            inline_cb->max_x = span->x;
            float first_fragment_top = inline_cb->min_y;
            float first_fragment_bottom = inline_cb->max_y;
            bool found_first_fragment = multicol_inline_fragment_bounds_y(
                static_cast<View*>(span), true,
                &first_fragment_top, &first_fragment_bottom);
            float last_fragment_top = inline_cb->min_y;
            float last_fragment_bottom = inline_cb->max_y;
            bool found_last_fragment = multicol_inline_fragment_bounds_y(
                static_cast<View*>(span), false,
                &last_fragment_top, &last_fragment_bottom);
            bool has_originally_inline_abs_child = false;
            bool has_direct_abs_child = false;
            if (span->position) {
                for (ViewBlock* positioned_child = span->positionp()->first_abs_child;
                     positioned_child;
                     positioned_child = positioned_child->positionp()->next_abs_sibling) {
                    if (positioned_child->parent != static_cast<DomNode*>(span)) continue;
                    has_direct_abs_child = true;
                    if (layout_element_was_inline(
                            static_cast<DomElement*>(positioned_child), false)) {
                        has_originally_inline_abs_child = true;
                    }
                }
            }
            bool first_fragment_is_cb_start = !had_inline_cb ||
                (span->has_collapsed_line_fragment_union() &&
                 !has_originally_inline_abs_child);
            bool use_vertical_inline_edges = layout_block_inline_axis_is_vertical(ancestor) &&
                ancestor->block()->direction == CSS_VALUE_LTR &&
                has_direct_abs_child;
            if (use_vertical_inline_edges && found_first_fragment && found_last_fragment) {
                // CSS Position 3 §2.1: fragmented inline CB edges follow the
                // inline axis; physical ordering alone is not the edge order.
                if (span->block()->direction == CSS_VALUE_RTL) {
                    inline_cb->min_y = last_fragment_top;
                    inline_cb->max_y = first_fragment_bottom;
                } else {
                    inline_cb->min_y = first_fragment_top;
                    inline_cb->max_y = last_fragment_bottom;
                }
            } else if (first_fragment_is_cb_start && found_first_fragment) {
                // CSS Position 3 §4.1: a collapsed-line inline uses its first
                // generated fragment unless an inline abspos child needs the
                // pre-fragment containing-block origin.
                inline_cb->min_y = first_fragment_top;
            }
            if (!use_vertical_inline_edges) {
                float last_fragment_end_y = 0.0f;
                bool found_last_fragment_end_y =
                    multicol_inline_has_in_flow_fragment(static_cast<View*>(span)) &&
                    multicol_inline_fragment_edge_y(
                        static_cast<View*>(span), true, &last_fragment_end_y);
                if (found_last_fragment_end_y) {
                    inline_cb->max_y = last_fragment_end_y;
                } else if (found_first_fragment) {
                    inline_cb->max_y = first_fragment_top;
                }
            }
            if (layout_block_inline_axis_is_vertical(ancestor)) {
                bool has_positioned_descendant =
                    multicol_has_out_of_flow_descendant(static_cast<View*>(span));
                // css writing modes: before vertical publication, the span's
                // width is the physical inline extent and height is block extent.
                if (!has_positioned_descendant) {
                    if (span->width > ancestor->height) span->width = ancestor->height;
                    if (span->height > ancestor->width) span->height = ancestor->width;
                } else {
                    // positioned inline CB geometry remains in the legacy
                    // logical edge space until its static edge is finalized.
                    if (span->width > ancestor->width) span->width = ancestor->width;
                    if (span->height > ancestor->height) span->height = ancestor->height;
                }
            } else if (span->width > ancestor->width) {
                span->width = ancestor->width;
            }
        }
    }
    View* child = lam::dom_require<DOM_NODE_ELEMENT>(view)->first_child;
    while (child) {
        multicol_normalize_vertical_inline_fragment_bounds(child);
        child = child->next_sibling;
    }
}

static float multicol_fragmented_child_union(
    LayoutContext* lycon,
    ViewBlock* container,
    ViewBlock* child,
    float item_height,
    float fragment_height,
    int column_count,
    float column_width,
    float column_gap,
    float initial_fragment_offset,
    int* out_used_columns,
    float* out_fragment_flow_height
) {
    float row_gap = multicol_row_gap(container);
    if (row_gap < 0) row_gap = 0;
    if (out_fragment_flow_height) *out_fragment_flow_height = item_height;
    bool zero_height_fragmentainer = fragment_height == 0.0f && container &&
        container->multicol_prop() &&
        container->multicol_prop()->column_height_is_specified;
    if (fragment_height < 0.0f) return 0.0f;
    float fragment_capacity = zero_height_fragmentainer
        ? 1.0f : fragment_height;

    float negative_initial_offset = initial_fragment_offset < 0.0f
        ? -initial_fragment_offset : 0.0f;
    if (initial_fragment_offset < 0.0f) initial_fragment_offset = 0.0f;
    if (!zero_height_fragmentainer && initial_fragment_offset >= fragment_height) {
        initial_fragment_offset = fmodf(initial_fragment_offset, fragment_capacity);
    }

    if (negative_initial_offset <= 0.0f) {
        int grid_used_columns = 0;
        float grid_union_height = 0.0f;
        if (multicol_fragment_grid_rows(
                container, child, item_height, fragment_height, column_count,
                column_width, column_gap, initial_fragment_offset,
                &grid_used_columns, &grid_union_height)) {
            if (out_used_columns) *out_used_columns = grid_used_columns;
            if (out_fragment_flow_height) {
                *out_fragment_flow_height = multicol_stored_fragment_flow_height(
                    child, item_height);
            }
            return grid_union_height;
        }
    }

    int fragment_count = (int)ceilf(
        (initial_fragment_offset + item_height) / fragment_capacity); // INT_CAST_OK: fragment count from positive heights
    if (fragment_count < 1) fragment_count = 1;
    bool overflow_columns = multicol_allows_overflow_columns(container);
    int layout_column_count = overflow_columns ? fragment_count : column_count;
    int used_columns = overflow_columns ? fragment_count : min(column_count, fragment_count);
    int row_count = (fragment_count + layout_column_count - 1) / layout_column_count;
    if (row_count < 1) row_count = 1;
    int contained_monolithic_child_count =
        !multicol_has_vertical_inline_axis(child)
            ? multicol_contained_monolithic_child_count(child) : 0;
    float contained_monolithic_max_height = 0.0f;
    if (contained_monolithic_child_count > 0 && fragment_height > 0.0f) {
        MulticolMonolithicChildFlow flow = {};
        multicol_init_monolithic_child_flow(
            &flow,
            multicol_monolithic_initial_offset(child, initial_fragment_offset),
            fragment_height);
        int last_fragment = -1;
        for (View* descendant = child->first_placed_child();
             descendant; descendant = descendant->next()) {
            ViewBlock* descendant_block = lam::view_as_block(descendant);
            if (!descendant_block ||
                layout_block_is_out_of_flow_positioned(descendant_block) ||
                descendant_block->view_type == RDT_VIEW_MARKER) continue;
            int placed_fragment = -1;
            if (!multicol_advance_monolithic_child_flow(
                    child, descendant_block, fragment_height, &flow,
                    &placed_fragment, nullptr)) {
                last_fragment = -1;
                break;
            }
            last_fragment = max(last_fragment, placed_fragment);
            contained_monolithic_max_height = max(
                contained_monolithic_max_height, descendant_block->height);
        }
        if (last_fragment >= 0) {
            fragment_count = last_fragment + 1;
            layout_column_count = overflow_columns ? fragment_count : column_count;
            used_columns = overflow_columns ? fragment_count :
                min(column_count, fragment_count);
            row_count = (fragment_count + layout_column_count - 1) /
                layout_column_count;
            if (row_count < 1) row_count = 1;
        }
    }
    float single_line_visual_height = 0.0f;
    int single_line_count = 0;
    float single_line_advance = 0.0f;
    bool single_line_overflow = false;
    if (overflow_columns && multicol_inline_line_metrics(
            child, &single_line_count, &single_line_advance, &single_line_visual_height)) {
        float line_box_height = 0.0f;
        if (child->blk && child->block()->line_height) {
            line_box_height = layout_resolve_line_height_value(
                lycon, child->block()->line_height,
                lam::dom_require<DOM_NODE_ELEMENT>(child),
                child->fontp() ? child->fontp()->font_size : 16.0f);
        }
        if (line_box_height <= 0.0f && child->fontp() && child->fontp()->font_handle) {
            line_box_height = calc_normal_line_height(child->fontp()->font_handle);
        }
        if (line_box_height > single_line_visual_height) {
            single_line_visual_height = line_box_height;
        }
        single_line_overflow = single_line_count == 1 &&
            single_line_visual_height > fragment_capacity;
    }
    if (single_line_overflow) {
        // css-break: a monolithic line may be sliced to guarantee progress in
        // a fragmentainer whose used block size is smaller than the line.
        float remaining_after_line = item_height - single_line_visual_height;
        if (remaining_after_line < 0.0f) remaining_after_line = 0.0f;
        int continuation_count = (int)ceilf(remaining_after_line / fragment_height); // INT_CAST_OK: fragment count from positive heights
        fragment_count = 1 + continuation_count;
        if (fragment_count < 1) fragment_count = 1;
        layout_column_count = fragment_count;
        used_columns = fragment_count;
        row_count = 1;
    }
    // Fragment union bounds retain the block border box; clipping it to the
    // fragmentainer loses overflow in DOMRect geometry.
    bool vertical_writing = multicol_has_vertical_inline_axis(container);
    bool has_authored_inline_size = layout_axis_has_given_size(child, true);
    bool has_in_flow_block_child = multicol_has_in_flow_block_child(child);
    bool zero_width_fragment = !vertical_writing && column_width <= 0.0f &&
        child->width <= 0.0f;
    float fragment_visual_width = vertical_writing
        ? fragment_height
        : (zero_width_fragment ? 0.0f
           : has_authored_inline_size && child->width > 0.0f
               ? child->width
               : column_width);
    float union_width;
    float union_height;
    if (vertical_writing) {
        bool preserve_authored_block_size = has_authored_inline_size &&
            has_in_flow_block_child &&
            child->width <= fragment_height + 0.5f;
        // css writing modes: an authored vertical block-size remains the
        // wrapper's border box while overflowing descendants fragment; a
        // block-size larger than its fragmentainer is itself fragmented.
        union_width = preserve_authored_block_size ? child->width : fragment_visual_width;
        float column_inline_extent = container->multicol_prop()->computed_column_width;
        if (column_inline_extent <= 0.0f) column_inline_extent = child->height;
        // css fragmentation: a vertical block's DOMRect is the union of its
        // fragmentainer border boxes along the physical inline axis.
        union_height = preserve_authored_block_size
            ? column_inline_extent
            : child->height + (used_columns - 1) *
                (column_inline_extent + column_gap);
        if (!preserve_authored_block_size && row_count > 1) {
            union_height += (row_count - 1) * row_gap;
        }
    } else {
        union_width = fragment_visual_width + (used_columns - 1) * (column_width + column_gap);
        union_height = row_count * fragment_height + (row_count - 1) * row_gap;
        if (!multicol_group_wraps_rows(container)) {
            union_height = fragment_height;
        }
        if (multicol_uses_fixed_balanced_rows(container) && row_count == 1 &&
            item_height > fragment_height + row_gap + 0.5f) {
            // css multicol-2 §4.2: overflow beyond a fixed balanced row
            // crosses the row gutter in the fragmented border-box union.
            union_height += row_gap;
            if (multicol_follows_short_direct_spanner(
                    container, child, multicol_specified_row_height(container))) {
                union_height += row_gap;
            }
        }
        if (multicol_uses_fixed_wrapped_rows(container) &&
            container->multicol_prop()->fill == COLUMN_FILL_AUTO &&
            row_count > 1) {
            float total_fragmented_height = initial_fragment_offset + item_height;
            float last_piece_height = fmodf(total_fragmented_height, fragment_capacity);
            if (last_piece_height <= 0.0f) last_piece_height = fragment_capacity;
            // css multicol-2 §4.2: auto-fill exposes the final fragment's
            // visual extent, while the container still owns complete rows.
            union_height = (row_count - 1) * (fragment_height + row_gap) +
                last_piece_height;
        }
        if (multicol_group_wraps_rows(container) && initial_fragment_offset > 0.0f &&
            container->multicol_prop() &&
            container->multicol_prop()->fill == COLUMN_FILL_AUTO &&
            !multicol_has_definite_ancestor_fragmentainer(container)) {
            // css multicol-2 §4.2: a tall spanner can consume the leading
            // part of the first row before the following flow is fragmented.
            union_height = max(0.0f, union_height - initial_fragment_offset);
        }
        if (zero_height_fragmentainer) {
            float total_fragmented_height = initial_fragment_offset + item_height;
            float last_piece_height = fmodf(total_fragmented_height, fragment_capacity);
            if (last_piece_height <= 0.0f) last_piece_height = fragment_capacity;
            union_height = (row_count - 1) * row_gap + last_piece_height;
        }
        if (single_line_overflow) union_height = single_line_visual_height;
        if (negative_initial_offset > 0.0f &&
            fragment_height + negative_initial_offset > union_height) {
            // css-break §5.3: a broken box fills the remaining fragmentainer;
            // retain the visible extent before a negative start margin.
            union_height = fragment_height + negative_initial_offset;
        }
        if (contained_monolithic_max_height > union_height) {
            // css fragmentation: the union of monolithic child boxes retains
            // their overflowing border-box extent.
            union_height = contained_monolithic_max_height;
        }
    }
    if (zero_width_fragment) {
        // cssom view: an all-zero fragment union remains a collapsed box;
        // its position follows the last generated zero-width fragment.
        union_width = 0.0f;
    }

    bool preserve_authored_block_size = vertical_writing &&
        has_authored_inline_size && has_in_flow_block_child &&
        child->width <= fragment_height + 0.5f;
    if (!preserve_authored_block_size) {
        multicol_store_layout_fragments(child, fragment_count, layout_column_count,
            fragment_height, column_width, column_gap, row_gap, fragment_visual_width,
            initial_fragment_offset, zero_height_fragmentainer);
        if (out_fragment_flow_height) {
            *out_fragment_flow_height = multicol_stored_fragment_flow_height(
                child, item_height);
        }
    }
    if (vertical_writing) {
        child->width = union_width;
    } else if (!has_authored_inline_size) {
        // css fragmentation: an auto-width block gets the width of each
        // fragmentainer; its union expands only across the fragments used.
        child->width = union_width;
    } else if (child->width < union_width) {
        child->width = union_width;
    }
    if (zero_width_fragment && used_columns > 1) {
        child->x += (used_columns - 1) * column_gap;
    }
    if (vertical_writing) {
        // css writing modes: normalize the union origin before projecting
        // descendants, so their coordinates use the same block-axis origin.
        child->x -= initial_fragment_offset;
    }
    child->height = union_height;
    child->content_height = union_height;
    bool normalize_before_projection = !vertical_writing &&
        initial_fragment_offset > 0.0f && child->multicol_prop() &&
        is_multicol_container(child) &&
        multicol_has_out_of_flow_descendant(static_cast<View*>(child));
    if (normalize_before_projection) {
        // css fragmentation: descendant projection uses the normalized union
        // origin; otherwise the leading fragment offset is applied twice.
        child->y -= initial_fragment_offset;
    }
    // Fragmentation changes the visual containing-block edges after abspos
    // layout; resolve physical inset edges against the final fragment union.
    multicol_reposition_abs_children_for_fragmented_cb(lycon, static_cast<View*>(child));
    bool nested_descendants_local = child->multicol_prop() &&
        is_multicol_container(child) &&
        multicol_has_nested_spanner_wrapper(child);
    int projected_fragment_count = multicol_project_fragmented_descendants(
        lycon, child, fragment_height, layout_column_count, column_width,
        column_gap, fragment_height, initial_fragment_offset,
        nested_descendants_local);
    if (!preserve_authored_block_size && projected_fragment_count > fragment_count) {
        // css multicol: descendant forced breaks can create rows beyond the
        // phase-one height; publish the union of those generated fragments.
        fragment_count = projected_fragment_count;
        layout_column_count = overflow_columns ? fragment_count : column_count;
        used_columns = overflow_columns ? fragment_count :
            min(column_count, fragment_count);
        row_count = (fragment_count + layout_column_count - 1) /
            layout_column_count;
        if (row_count < 1) row_count = 1;
        union_width = fragment_visual_width +
            (used_columns - 1) * (column_width + column_gap);
        union_height = row_count * fragment_height +
            (row_count - 1) * row_gap;
        if (!multicol_group_wraps_rows(container)) {
            union_height = fragment_height;
        }
        if (!vertical_writing && !has_authored_inline_size) {
            child->width = union_width;
        }
        child->height = union_height;
        child->content_height = union_height;
        // Store the generated fragments after publishing the final union so
        // their flow extents represent every projected fragmentainer.
        multicol_store_layout_fragments(
            child, fragment_count, layout_column_count, fragment_height,
            column_width, column_gap, row_gap, fragment_visual_width,
            initial_fragment_offset);
        if (out_fragment_flow_height) {
            *out_fragment_flow_height = multicol_stored_fragment_flow_height(
                child, item_height);
        }
    }
    // css writing modes: the initial offset is on the physical block axis;
    // normalize the union box on that axis without changing its inline origin.
    if (!normalize_before_projection && !vertical_writing &&
        initial_fragment_offset > 0.0f) {
        // DOMRect is the union of every fragment, so a continuation at the
        // next column's block-start can precede the first fragment's logical
        // start.
        child->y -= initial_fragment_offset;
    }
    if (out_used_columns) *out_used_columns = used_columns;
    return union_height;
}
// Direct multicol content and nested spanner wrappers share the same
// fragmentainer state machine. The callbacks retain only their coordinate
// system differences; break decisions, fragmentation, and cursor ownership
// must stay identical.
template <typename PlacementFn, typename ContentFn, typename HeightFn>
static void multicol_distribute_flow_group(
    LayoutContext* lycon,
    ViewBlock* container,
    MulticolFlowItem* items,
    int group_start,
    int group_end,
    float target_height,
    float group_y,
    bool fragment_monolithic_before_break,
    ColumnGroup* group,
    FragmentedFlowCursor* cursor,
    int* used_column_count,
    PlacementFn adjust_placement,
    ContentFn adjust_content,
    HeightFn adjust_height
) {
    if (!lycon || !container || !items || !group || !cursor) return;
    for (int index = group_start; index < group_end; index++) {
        MulticolFlowItem& info = items[index];
        ViewBlock* child = info.block;
        if (!child) continue;

        bool parallel_flow = info.parallel_flow;
        bool at_fragment_start = !parallel_flow && !cursor->has_item_in_fragment;
        float margin_before = at_fragment_start
            ? (cursor->current_fragment == 0 ? info.margin_before : 0.0f)
            : max(cursor->pending_margin_after, info.margin_before);
        float flow_height = parallel_flow ? 0.0f : margin_before + info.content_height;
        bool crosses_fragment = cursor->block_offset + flow_height > target_height + 0.5f;
        bool zero_height_fragmentainer =
            container->multicol_prop()->column_height_is_specified &&
            target_height <= 0.0f;
        bool child_is_multicol = child->multicol_prop() && is_multicol_container(child);
        bool child_is_contained_monolithic =
            multicol_is_contained_monolithic(child);
        bool orthogonal_monolithic =
            layout_inline_box_is_orthogonal_to_parent(child) &&
            !info.can_fragment;
        bool nested_auto_multicol = child_is_multicol &&
            child->multicol_prop()->fill == COLUMN_FILL_AUTO &&
            container->multicol_prop()->fill == COLUMN_FILL_BALANCE &&
            multicol_is_nested_in_balancing_context(child);
        // css writing modes §7.3.4: an orthogonal line box has no internal
        // break opportunity, so its content-based block size must stay whole.
        bool zero_height_monolithic = zero_height_fragmentainer &&
            !parallel_flow && multicol_is_contained_monolithic(child);
        bool can_fragment = !parallel_flow && info.can_fragment && crosses_fragment;
        bool should_fragment_monolithic = !child_is_contained_monolithic &&
            !orthogonal_monolithic &&
            multicol_should_fragment_monolithic_child(
                container, child, parallel_flow ? info.content_height : flow_height,
                target_height);
        bool should_fragment = parallel_flow
            ? (!zero_height_fragmentainer && info.content_height > target_height + 0.5f)
            : (!zero_height_fragmentainer &&
               (can_fragment || should_fragment_monolithic ||
               (flow_height > target_height && !orthogonal_monolithic &&
                 (!child_is_multicol || nested_auto_multicol) &&
                 !child_is_contained_monolithic)));
        bool can_break_before_item = can_fragment ||
            (fragment_monolithic_before_break && should_fragment);
        // css-break: an avoid constraint keeps the item in this fragmentainer;
        // exceeding the target is allowed when no later break is available.
        bool avoid_break_before_item = info.break_before_avoid ||
            (index > group_start && items[index - 1].break_after_avoid);
        bool should_break_group = multicol_group_should_break(
            container, cursor, flow_height);

        bool break_before_orphans = false;
        ColumnFragment* current_fragment = multicol_cursor_current_fragment(cursor);
        bool can_advance_fragment = current_fragment &&
            (current_fragment->column_index < group->column_count - 1 ||
             group->wraps_rows || multicol_allows_overflow_columns(container));
        bool nested_multicol_break_before =
            child_is_multicol &&
            child->multicol_prop()->fill == COLUMN_FILL_BALANCE &&
            should_fragment_monolithic &&
            cursor->has_item_in_fragment && cursor->block_offset > 0.0f &&
            cursor->block_offset + flow_height > target_height + 0.5f &&
            can_advance_fragment;
        if (can_fragment && cursor->block_offset > 0.0f && can_advance_fragment) {
            int orphans = container->blk && container->block()->orphans > 0
                ? container->block()->orphans : 2;
            int line_count = 0;
            float line_advance = 0.0f;
            if (multicol_inline_line_metrics(
                    child, &line_count, &line_advance, nullptr) &&
                line_count >= orphans) {
                // CSS Fragmentation §4.1 counts line boxes; initial-letter glyph
                // overflow must not reduce the number of lines that fit.
                int first_fragment_lines = multicol_lines_that_fit_fragment(
                    target_height - cursor->block_offset - margin_before, line_advance);
                break_before_orphans = first_fragment_lines < orphans;
            }
        }
        bool avoid_break_before_next = false;
        if (!at_fragment_start && !can_break_before_item &&
            multicol_has_escaping_spanner_in_flow(container) &&
            index + 1 < group_end && current_fragment &&
            can_advance_fragment) {
            ViewBlock* next_child = items[index + 1].block;
            bool next_break_avoid = next_child && next_child->blk &&
                multicol_avoids_column_break(next_child->block()->break_before);
            bool current_break_avoid = child->blk &&
                child->block()->break_inside == CSS_VALUE_AVOID;
            bool next_inside_avoid = next_child && next_child->blk &&
                next_child->block()->break_inside == CSS_VALUE_AVOID;
            if (next_break_avoid && current_break_avoid && next_inside_avoid) {
                MulticolFlowItem& next_info = items[index + 1];
                float next_margin_before = max(info.margin_after,
                    next_info.margin_before);
                float next_flow_height = next_margin_before + next_info.content_height;
                avoid_break_before_next =
                    cursor->block_offset + flow_height <= target_height + 0.5f &&
                    cursor->block_offset + flow_height + next_flow_height >
                        target_height + 0.5f;
            }
        }

        bool advanced_fragment = false;
        if (!parallel_flow && break_before_orphans && !avoid_break_before_item) {
            multicol_cursor_advance_fragment(cursor);
            advanced_fragment = true;
        } else if (!parallel_flow && should_fragment && !avoid_break_before_item &&
                   cursor->has_item_in_fragment &&
                   cursor->block_offset >= target_height - 0.5f &&
                   can_advance_fragment) {
            // css fragmentation: a full fragmentainer must advance before the
            // next fragmented item is placed at its block-start edge.
            multicol_cursor_advance_fragment(cursor);
            advanced_fragment = true;
        } else if (!parallel_flow && nested_multicol_break_before &&
                   !avoid_break_before_item) {
            // css fragmentation: a nested multicol that cannot fit remains
            // whole and starts in the next fragmentainer.
            multicol_cursor_advance_fragment(cursor);
            advanced_fragment = true;
        } else if (!parallel_flow && info.break_before_column && cursor->block_offset > 0.0f) {
            multicol_cursor_advance_fragment(cursor);
            advanced_fragment = true;
        } else if (!parallel_flow && !can_break_before_item &&
                   !avoid_break_before_item &&
                   (avoid_break_before_next ||
                    should_break_group)) {
            multicol_cursor_advance_fragment(cursor);
            advanced_fragment = true;
        }

        if (advanced_fragment) {
            // A break decision made at the previous fragment must not force a
            // newly placed block to fragment when its margin box fits.
            at_fragment_start = !cursor->has_item_in_fragment;
            margin_before = at_fragment_start
                ? (cursor->current_fragment == 0 ? info.margin_before : 0.0f)
                : max(cursor->pending_margin_after, info.margin_before);
            flow_height = margin_before + info.content_height;
            crosses_fragment = cursor->block_offset + flow_height > target_height + 0.5f;
            can_fragment = info.can_fragment && crosses_fragment;
            should_fragment_monolithic = !child_is_contained_monolithic &&
                !orthogonal_monolithic &&
                multicol_should_fragment_monolithic_child(
                    container, child, parallel_flow ? info.content_height : flow_height,
                    target_height);
            should_fragment = !zero_height_fragmentainer &&
                (can_fragment || should_fragment_monolithic ||
                 (flow_height > target_height &&
                  (!child_is_multicol || nested_auto_multicol) &&
                  !child_is_contained_monolithic));
        }

        multicol_cursor_advance_block(cursor, margin_before);
        float old_x = child->x;
        float old_y = child->y;
        if (parallel_flow && cursor->has_item_in_fragment &&
            cursor->block_offset + info.content_height > target_height + 0.5f &&
            can_advance_fragment) {
            // css 2.1 §9.5: a float that cannot fit beside preceding flow
            // moves to the next available column block-start.
            multicol_cursor_advance_fragment(cursor);
        }
        float placement_block_offset = cursor->block_offset;
        if (child_is_multicol &&
            child->multicol_prop()->fill == COLUMN_FILL_BALANCE &&
            multicol_has_definite_ancestor_fragmentainer(child) &&
            child->multicol_prop()->computed_used_column_count >
                child->multicol_prop()->computed_column_count &&
            !layout_axis_has_given_size(child, true)) {
            // css fragmentation: a nested flow's border-box union includes
            // every continuation column and cannot be narrower than its CB.
            float child_union_width = multicol_used_column_extent(child);
            if (container->width > child_union_width) {
                child_union_width = container->width;
            }
            if (child_union_width > child->width) child->width = child_union_width;
        }
        multicol_cursor_place_block(cursor, child, group_y);
        multicol_fit_vertical_auto_inline_size(group, child);
        adjust_placement(info, child, old_x, old_y, placement_block_offset);

        float placed_height = parallel_flow ? 0.0f : info.content_height;
        bool content_handled = parallel_flow
            ? false : adjust_content(info, child, &placed_height);
        bool nested_parallel_break = false;

        float descendant_flow_extent = multicol_in_flow_descendant_extent(child);
        if (!content_handled && target_height > 0.0f &&
            !should_fragment && !child_is_multicol &&
            !multicol_is_scroll_container(child) &&
            descendant_flow_extent > target_height + 0.5f) {
                    // css fragmentation: overflow in a fixed-height wrapper still
                    // fragments its in-flow descendants in the parent context.
            int projected_fragment_count = multicol_project_fragmented_descendants(
                lycon, child, target_height, group->column_count,
                group->column_width, group->column_gap, target_height,
                cursor->block_offset);
            bool vertical_writing = multicol_has_vertical_inline_axis(container);
            if (!vertical_writing && flow_height <= target_height + 0.5f &&
                !layout_axis_has_given_size(child, true)) {
                // css fragmentation: a fixed wrapper that fits one fragment
                // keeps one column's border box while its child overflows.
                child->width = group->column_width;
            }
            int nested_fragment_count = (int)ceilf(
                (cursor->block_offset + descendant_flow_extent) / target_height); // INT_CAST_OK: fragment count from positive heights
            if (nested_fragment_count < 1) nested_fragment_count = 1;
            if (projected_fragment_count > nested_fragment_count) {
                nested_fragment_count = projected_fragment_count;
            }
            int required_fragment_count = cursor->current_fragment +
                nested_fragment_count;
            multicol_group_ensure_fragment_count(cursor, required_fragment_count);
            if (used_column_count) {
                int nested_used_columns = min(group->column_count,
                    nested_fragment_count);
                if (nested_used_columns > *used_column_count) {
                    *used_column_count = nested_used_columns;
                }
            }
            if (nested_fragment_count > 1) {
                int nested_row_count = (required_fragment_count +
                    group->column_count - 1) / group->column_count;
                float nested_group_height = group->wraps_rows
                    ? nested_row_count * target_height +
                        (nested_row_count - 1) * group->row_gap
                    : target_height;
                if (nested_group_height > group->group_used_height) {
                    group->group_used_height = nested_group_height;
                }
                if (child->height <= 0.0f && required_fragment_count <=
                    group->fragment_count) {
                    ColumnFragment* first_fragment =
                        &group->fragments[cursor->current_fragment];
                    ColumnFragment* last_fragment =
                        &group->fragments[required_fragment_count - 1];
                    // css fragmentation: a collapsed wrapper's zero-area box
                    // is represented at the final fragmentainer edge.
                    float wrapper_shift = last_fragment->x - first_fragment->x;
                    child->x += wrapper_shift;
                    // preserve the descendant's first fragment at its own
                    // fragmentainer edge after moving the wrapper box.
                    for (View* descendant = child->first_placed_child();
                         descendant; descendant = descendant->next()) {
                        layout_shift_view_tree_geometry(
                            descendant, -wrapper_shift, 0.0f);
                    }
                }
            }
        }

        if (parallel_flow) {
            // css multicol: a float is parallel to normal flow; fragment its
            // own block-axis overflow without advancing the flow cursor.
            if (info.content_height > target_height + 0.5f) {
                int parallel_used_columns = 1;
                float parallel_flow_height = info.content_height;
                float parallel_union_height = multicol_fragmented_child_union(
                    lycon, container, child, info.content_height, target_height,
                    group->column_count, group->column_width, group->column_gap,
                    cursor->block_offset, &parallel_used_columns,
                    &parallel_flow_height);
            multicol_group_record_fragment_count(group, parallel_used_columns);
            if (parallel_union_height > group->group_used_height) {
                group->group_used_height = parallel_union_height;
            }
                if (used_column_count && parallel_used_columns > *used_column_count) {
                    *used_column_count = parallel_used_columns;
                }
            }
            continue;
        }

        if (zero_height_fragmentainer && !zero_height_monolithic) {
            int zero_used_columns = 1;
            float zero_flow_height = info.content_height;
            multicol_fragmented_child_union(
                lycon, container, child, info.content_height, target_height,
                group->column_count, group->column_width, group->column_gap,
                cursor->block_offset, &zero_used_columns, &zero_flow_height);
            DomElement* child_elem = lam::dom_require<DOM_NODE_ELEMENT>(child);
            int zero_fragment_count = child_elem->layout_fragments_count();
            for (int fi = 1; fi < zero_fragment_count; fi++) {
                multicol_cursor_advance_fragment(cursor);
            }
            adjust_height(info, child, index == group_start, &placed_height,
                placement_block_offset);
            multicol_group_record_fragment_count(group, zero_used_columns);
            if (used_column_count && zero_used_columns > *used_column_count) {
                *used_column_count = zero_used_columns;
            }
            cursor->pending_margin_after = info.margin_after;
            cursor->has_item_in_fragment = true;
            if (index + 1 < group_end || info.break_after_column) {
                multicol_cursor_advance_fragment(cursor);
            }
            continue;
        }

        if (zero_height_monolithic) {
            // css fragmentation: size-contained content overflows a zero-sized
            // fragmentainer as one monolithic item, then advances the column.
            adjust_height(info, child, index == group_start, &placed_height,
                placement_block_offset);
            ColumnFragment* fragment = multicol_cursor_current_fragment(cursor);
            if (fragment && used_column_count) {
                int candidate = fragment->column_index + 1;
                if (candidate > *used_column_count) *used_column_count = candidate;
            }
            cursor->pending_margin_after = info.margin_after;
            cursor->has_item_in_fragment = true;
            if (index + 1 < group_end || info.break_after_column) {
                multicol_cursor_advance_fragment(cursor);
            }
            continue;
        }

        // css-break: a forced break in a direct block's child flow must be
        // projected even when the block itself fits the current fragmentainer.
        bool nested_forced_break = multicol_has_forced_break_descendant(child) &&
            !child_is_multicol;
        bool nested_parallel_forced_break = multicol_is_parallel_forced_flow(child);
        if (!content_handled && !should_fragment &&
            nested_forced_break) {
            int nested_fragment_count = multicol_project_fragmented_descendants(
                lycon, child, target_height, group->column_count,
                group->column_width, group->column_gap, target_height,
                cursor->block_offset);
            if (nested_fragment_count > 1 && nested_parallel_forced_break) {
                int nested_used_columns = multicol_group_wraps_rows(container)
                    ? min(nested_fragment_count, group->column_count)
                    : nested_fragment_count;
                float nested_union_width = child->width +
                    (nested_used_columns - 1) *
                        (group->column_width + group->column_gap);
                if (nested_union_width > child->width) child->width = nested_union_width;
                multicol_store_layout_fragments(
                    child, nested_fragment_count, group->column_count,
                    target_height, group->column_width, group->column_gap,
                    group->row_gap, child->width, 0.0f);
                // css-break §2.1: the parallel float flow occupies its
                // fragmentainer before following outer-flow content resumes.
                child->height = max(child->height, target_height);
                child->content_height = child->height;
                placed_height = child->height;
                nested_parallel_break = true;
            }
        }
        bool nested_fit_to_remaining_fragment = false;
        if (!content_handled && should_fragment && child_is_multicol &&
            avoid_break_before_item && cursor->block_offset < target_height) {
            float remaining_fragment_height = target_height - cursor->block_offset;
            nested_fit_to_remaining_fragment =
                multicol_fit_nested_auto_multicol_fragment(
                    lycon, child, remaining_fragment_height,
                    group->column_width, group->column_gap);
            if (nested_fit_to_remaining_fragment) {
                placed_height = remaining_fragment_height;
            }
        }
        if (!content_handled && should_fragment &&
            !nested_fit_to_remaining_fragment) {
            int used_columns = 1;
            int fragment_count = target_height > 0.0f
                ? (int)ceilf(info.content_height / target_height) // INT_CAST_OK: fragment count from positive heights
                : 1;
            if (fragment_count < 1) fragment_count = 1;
            float flow_height = multicol_text_box_trim_fragmented_flow_height(
                child, info.content_height, target_height, fragment_count,
                cursor->block_offset);
            float fragmented_flow_height = flow_height;
            // css-break: only descendant forced breaks create new flow beyond
            // the caller's logical extent; trim-aware fragmentation owns its
            // continuation height and must not be replaced by visible unions.
            float* generated_flow_height = multicol_has_forced_break_descendant(child)
                ? &fragmented_flow_height : nullptr;
            placed_height = multicol_fragmented_child_union(
                lycon, container, child, info.content_height, target_height,
                group->column_count, group->column_width, group->column_gap,
                cursor->block_offset, &used_columns, generated_flow_height);
            multicol_group_record_fragment_count(group, used_columns);
            if (used_column_count && used_columns > *used_column_count) {
                *used_column_count = used_columns;
            }
            multicol_cursor_advance_fragmented_block(cursor, fragmented_flow_height);
            placed_height = 0.0f;
        }

        adjust_height(info, child, index == group_start, &placed_height,
            placement_block_offset);
        ColumnFragment* fragment = multicol_cursor_current_fragment(cursor);
        if (fragment && used_column_count) {
            int candidate = fragment->column_index + 1;
            if (candidate > *used_column_count) *used_column_count = candidate;
        }
        multicol_cursor_advance_block(cursor, placed_height);
        cursor->pending_margin_after = info.margin_after;
        cursor->has_item_in_fragment = true;

        if (nested_parallel_break) {
            // css-break §2.1: a forced break in a parallel flow consumes the
            // current outer fragmentainer before the following sibling.
            multicol_cursor_advance_fragment(cursor);
        }

        if (info.break_after_column && index + 1 < group_end) {
            multicol_cursor_advance_fragment(cursor);
        }
    }
}

static float multicol_clone_fragmented_flow_height(
    ViewBlock* child,
    float item_height,
    int fragment_count
) {
    if (!child || !child->blk || fragment_count <= 1) return item_height;
    if (child->block()->box_decoration_break != CSS_VALUE_CLONE) return item_height;
    if (!(child->block()->text_box_trim_applied & TEXT_BOX_TRIM_START)) return item_height;

    float extra_start_trim = child->block()->text_box_trim_start_amount * fragment_count;
    if (extra_start_trim <= 0.0f) return item_height;

    float adjusted = item_height - extra_start_trim;
    if (adjusted < 0.0f) adjusted = 0.0f;
    return adjusted;
}

static float multicol_slice_text_box_trim_flow_height(
    ViewBlock* child,
    float item_height,
    float fragment_height,
    float initial_fragment_offset
) {
    if (!child || !child->blk || fragment_height <= 0.0f) return item_height;
    if (child->block()->box_decoration_break == CSS_VALUE_CLONE) return item_height;
    if (!(child->block()->text_box_trim_applied & (TEXT_BOX_TRIM_START | TEXT_BOX_TRIM_END))) {
        return item_height;
    }

    int line_count = 0;
    float line_advance = 0.0f;
    float visual_height = 0.0f;
    if (!multicol_inline_line_metrics(child, &line_count, &line_advance, &visual_height)) {
        return item_height;
    }

    bool slice_start_trim = multicol_uses_slice_start_trim(child);
    bool slice_end_offset_trim = multicol_uses_slice_end_offset_trim(child, initial_fragment_offset);
    float normal_line_offset = multicol_normal_line_offset(line_advance, visual_height);
    float first_fragment_line_offset = 0.0f;
    float continuation_line_offset = 0.0f;
    int first_fragment_lines = 0;
    int continuation_fragment_lines = 0;
    if (slice_start_trim) {
        first_fragment_line_offset = normal_line_offset - child->block()->text_box_trim_start_amount;
        if (first_fragment_line_offset < 0.0f) first_fragment_line_offset = 0.0f;
        continuation_line_offset = normal_line_offset;
        first_fragment_lines = multicol_lines_that_fit_fragment(
            fragment_height, line_advance, visual_height, first_fragment_line_offset);
        continuation_fragment_lines = multicol_lines_that_fit_fragment(
            fragment_height, line_advance, visual_height, continuation_line_offset);
    } else if (slice_end_offset_trim) {
        first_fragment_lines = multicol_lines_that_fit_fragment(
            fragment_height - initial_fragment_offset, line_advance);
        continuation_fragment_lines = multicol_lines_that_fit_fragment(
            fragment_height, line_advance);
    } else {
        first_fragment_lines = multicol_lines_that_fit_fragment(fragment_height, line_advance);
        continuation_fragment_lines = first_fragment_lines;
    }

    int last_fragment_index = 0;
    int last_line_slot = 0;
    multicol_map_line_to_fragment(line_count - 1,
        first_fragment_lines, continuation_fragment_lines,
        &last_fragment_index, &last_line_slot);

    int fragment_count = last_fragment_index + 1;
    if (fragment_count <= 1) return item_height;

    int last_fragment_lines = last_line_slot + 1;
    float last_fragment_line_offset = slice_start_trim && last_fragment_index > 0 ?
        continuation_line_offset : first_fragment_line_offset;

    float flow_height = (fragment_count - 1) * fragment_height +
        last_fragment_line_offset + last_fragment_lines * line_advance;
    if (child->block()->text_box_trim_applied & TEXT_BOX_TRIM_END) {
        flow_height -= child->block()->text_box_trim_end_amount;
    }
    if (slice_start_trim && initial_fragment_offset > 0.0f) {
        flow_height -= initial_fragment_offset + child->block()->text_box_trim_start_amount;
    } else if (slice_end_offset_trim) {
        flow_height -= initial_fragment_offset + normal_line_offset;
    }
    if (flow_height < 0.0f) flow_height = 0.0f;

    float margin_extra = layout_box_metrics(child).margin_v;
    if (margin_extra > 0.0f) flow_height += margin_extra;

    return flow_height;
}

static float multicol_text_box_trim_fragmented_flow_height(
    ViewBlock* child,
    float item_height,
    float fragment_height,
    int fragment_count,
    float initial_fragment_offset
) {
    if (child && child->blk && child->block_mut()->box_decoration_break == CSS_VALUE_CLONE) {
        return multicol_clone_fragmented_flow_height(child, item_height, fragment_count);
    }
    return multicol_slice_text_box_trim_flow_height(child, item_height, fragment_height,
        initial_fragment_offset);
}

static bool multicol_truncates_start_margin(ViewBlock* container, ViewBlock* child) {
    return container && child && container->first_placed_child() == child && child->bound &&
        child->boundary()->has_flow_margin && child->boundary()->flow_margin.top > 0.0f;
}

static float multicol_split_child_around_spanners(
    LayoutContext* lycon,
    ViewBlock* container,
    ViewBlock* child,
    int column_count,
    float column_width,
    float column_gap,
    bool nested_wrapper = false,
    float fragmentainer_height = -1.0f,
    float* out_detached_spanner_extent = nullptr
) {
    if (out_detached_spanner_extent) *out_detached_spanner_extent = 0.0f;
    if (!container || !child || column_count <= 0 || column_width <= 0) {
        return child ? child->height : 0;
    }
    float original_child_x = child->x;
    float original_child_width = child->width;
    float original_child_height = child->height;

    ViewBlock* nested_spanner_child = nullptr;
    bool has_only_nested_spanner_child = true;
    View* nested_candidate = child->first_placed_child();
    while (nested_candidate) {
        if (nested_candidate->node_type == DOM_NODE_TEXT &&
            nested_candidate->view_type != RDT_VIEW_TEXT) {
            nested_candidate = nested_candidate->next();
            continue;
        }
        if (!nested_candidate->is_block()) {
            has_only_nested_spanner_child = false;
            break;
        }
        ViewBlock* nested_block = lam::view_as_block(nested_candidate);
        if (!nested_block || layout_block_is_out_of_flow_positioned(nested_block) ||
            multicol_is_spanner_block(nested_block) ||
            !multicol_spanner_can_escape_child(nested_block) ||
            !multicol_has_direct_spanner_child(nested_block) ||
            nested_spanner_child) {
            has_only_nested_spanner_child = false;
            break;
        }
        nested_spanner_child = nested_block;
        nested_candidate = nested_candidate->next();
    }
    if (has_only_nested_spanner_child && nested_spanner_child) {
        float original_child_y = child->y;
        float original_child_height = child->height;
        // Split through ordinary wrappers until the spanner becomes a direct
        // child; those wrappers preserve the parent formatting context.
        multicol_split_child_around_spanners(
            lycon, container, nested_spanner_child,
            column_count, column_width, column_gap, true);
        child->x = nested_spanner_child->x;
        child->y = nested_wrapper ? 0.0f : original_child_y + original_child_height;
        child->width = nested_spanner_child->width;
        child->height = nested_spanner_child->height;
        child->content_height = nested_spanner_child->content_height;
        // The wrapper's continuation box is empty, but its spanner still
        // contributes the original flow extent to the containing multicol.
        return original_child_height;
    }
    // MAX_MULTICOL_BLOCKS = 1024 → MulticolFlowItem[] ≈ 32 KiB; move to scratch arena (LIFO).
    MulticolFlowScratch flow_scratch = {};
    if (!flow_scratch.init(&lycon->scratch)) return child->height;
    MulticolFlowItem* children = flow_scratch.items;
    int child_count = 0;
    bool has_inline_wrapper = multicol_has_direct_inline_wrapper(child);
    if (has_inline_wrapper) {
        if (!multicol_collect_inline_flow_blocks(
                child->first_placed_child(), child, children, &child_count)) {
            child_count = 0;
        }
    } else {
        View* descendant = child->first_placed_child();
        while (descendant && child_count < MAX_MULTICOL_BLOCKS) {
            if (descendant->is_block()) {
                ViewBlock* descendant_block = lam::view_require_block(descendant);
                if (!layout_block_is_out_of_flow_positioned(descendant_block)) {
                    float descendant_height = descendant_block->height +
                        layout_box_metrics(descendant_block).margin_v;
                    multicol_init_flow_item(
                        &children[child_count], child, descendant_block, descendant_height, 0.0f,
                        multicol_is_spanner_block(descendant_block));
                    child_count++;
                }
            }
            descendant = descendant->next();
        }
    }

    if (child_count == 0) {
        flow_scratch.release(&lycon->scratch);
        return child->height;
    }

    float child_origin_y = 0.0f;
    if (!nested_wrapper && multicol_truncates_start_margin(container, child)) {
        // css-break §5.2: an unforced break at the first fragmentainer truncates the adjoining margin.
        child->y -= child->boundary()->flow_margin.top;
    }
    float child_fragment_origin_y = child->y;
    float inline_origin_y = child->y;
    float inline_leading_extent = 0.0f;
    float inline_total_extent = 0.0f;
    bool has_inline_text = multicol_direct_inline_text_extent(
        child, inline_origin_y, &inline_leading_extent, &inline_total_extent);
    float current_y = 0.0f;
    float prev_margin_bottom = 0;
    int used_column_count = 1;
    int non_spanner_count = 0;
    float first_group_target_height = -1;
    float spanner_extent = 0;
    float content_offset_x = 0;
    float leading_fragment_border_height = 0;
    bool leading_fragment_border_consumed = false;
    float next_group_fragment_offset = 0.0f;
    bool spanner_starts_wrapper_flow = child_count > 0 && children[0].spans_all;
    content_offset_x = layout_axis_decoration_start(
        child->bound ? child->boundary() : nullptr, LAYOUT_AXIS_X);
    leading_fragment_border_height = layout_axis_decoration_start(
        child->bound ? child->boundary() : nullptr, LAYOUT_AXIS_Y);

    MulticolGroupScratch group_scratch = {};
    if (!group_scratch.init(&lycon->scratch)) {
        group_scratch.release(&lycon->scratch);
    flow_scratch.release(&lycon->scratch);
    return child->height;
}

    int i = 0;
    while (i < child_count) {
        if (children[i].spans_all) {
            ViewBlock* spanner = children[i].block;
            if (has_inline_text && current_y < inline_leading_extent) {
                current_y = inline_leading_extent;
            }
            float margin_top = spanner->bound ? spanner->boundary()->margin.top : 0;
            float margin_bottom = spanner->bound ? spanner->boundary()->margin.bottom : 0;
            float collapsed_margin = max(prev_margin_bottom, margin_top);
            current_y -= prev_margin_bottom;
            current_y += collapsed_margin;

            spanner->x = child->x;
            spanner->y = current_y;
            multicol_set_spanner_inline_size(
                spanner, container->width > 0 ? container->width :
                    column_count * column_width + (column_count - 1) * column_gap);

            current_y += spanner->height + margin_bottom;
            spanner_extent += spanner->height + collapsed_margin + margin_bottom;
            if (i + 1 < child_count && multicol_uses_fixed_balanced_rows(container)) {
                float row_height = multicol_specified_row_height(container);
                if (children[i + 1].spans_all) {
                    // css gaps §2.1: consecutive spanner lines are separated
                    // by the gutter between their fixed rows.
                    current_y += multicol_row_gap(container);
                } else if (spanner->height > row_height + 0.5f) {
                    // css multicol-2 §4.2: following flow starts at the next
                    // fixed-row boundary after a tall spanner's end.
                    float row_gap = multicol_row_gap(container);
                    if (row_gap < 0.0f) row_gap = 0.0f;
                    float row_pitch = row_height + row_gap;
                    if (row_pitch > 0.0f) {
                        float next_row_start = ceilf(current_y / row_pitch) * row_pitch;
                        if (next_row_start > current_y + 0.5f) current_y = next_row_start;
                    }
            }
            }
            if (i < child_count && multicol_group_wraps_rows(container) &&
                container->multicol_prop()->fill == COLUMN_FILL_AUTO &&
                !multicol_has_definite_ancestor_fragmentainer(container)) {
                float row_gap = multicol_row_gap(container);
                if (row_gap < 0.0f) row_gap = 0.0f;
                float row_height = multicol_content_box_height_limit(container);
                if (row_height > 0.0f && spanner->height > row_height + 0.5f) {
                    // css multicol-2 §4.2: a tall spanner crosses the row gap;
                    // continue following flow at its actual row offset.
                    float row_pitch = row_height + row_gap;
                    float row_offset = fmodf(current_y, row_pitch);
                    if (row_offset > 0.0f) next_group_fragment_offset = row_offset;
                } else {
                    // css gaps §2.1: a row gap separates a completed column
                    // line from the next line containing ordinary flow.
                    current_y += row_gap;
                    next_group_fragment_offset = 0.0f;
                }
            }
            prev_margin_bottom = margin_bottom;
            i++;
            continue;
        }

        int group_start = i;
        float group_total_height = 0.0f;
        int group_item_count = multicol_collect_flow_group(
            children, child_count, &i, group_scratch.heights,
            group_scratch.content_heights, group_scratch.margin_before,
            group_scratch.margin_after,
            group_scratch.can_fragment, group_scratch.break_before,
            group_scratch.break_after, &group_total_height);
        int group_end = i;
        non_spanner_count += group_end - group_start;
        if (group_end < child_count && children[group_end].spans_all) {
            multicol_allow_fragmentation_before_spanner(
                children, group_start, group_end, group_scratch.can_fragment);
        }
        bool group_has_margins = multicol_group_has_block_margins(
            children, group_start, group_end);

        int self_sizing_count = 0;
        float group_balance_total = multicol_group_balance_total(
            children, group_start, group_item_count, group_total_height,
            &self_sizing_count);
        int balance_column_count = column_count - self_sizing_count;
        if (balance_column_count < 1) balance_column_count = 1;
        float balanced_height = ceilf(group_balance_total / balance_column_count);
        float target_height = multicol_group_target_height(container, balanced_height, group_total_height);
        target_height = multicol_balanced_target_search(
            container, group_scratch.heights, group_scratch.can_fragment,
            group_scratch.break_before, group_scratch.break_after,
            group_item_count, column_count, target_height, group_total_height,
            group_has_margins ? group_scratch.content_heights : nullptr,
            group_has_margins ? group_scratch.margin_before : nullptr,
            group_has_margins ? group_scratch.margin_after : nullptr);
        if (target_height <= 0) target_height = balanced_height;
        if (first_group_target_height < 0) {
            first_group_target_height = target_height;
        }

        ColumnGroup group;
        FragmentedFlowCursor cursor;
        group.fragments = group_scratch.fragments;
        // the nested split path applies its own child content offset after placement.
        multicol_group_init(&group, container, target_height, column_count,
                            column_width, column_gap, 0.0f);
        multicol_cursor_init(&cursor, &group);
        bool continues_after_tall_spanner = next_group_fragment_offset > 0.0f;
        cursor.block_offset = next_group_fragment_offset;
        next_group_fragment_offset = 0.0f;

        auto adjust_nested_placement = [&](MulticolFlowItem&, ViewBlock* block_child,
                                            float old_x, float old_y,
                                            float) {
            block_child->x += content_offset_x +
                layout_box_metrics(block_child).margin.left;
            float placement_delta_x = block_child->x - old_x;
            float placement_delta_y = block_child->y - old_y;
            if (placement_delta_x != 0.0f || placement_delta_y != 0.0f) {
                layout_shift_view_tree(block_child, placement_delta_x, placement_delta_y);
                block_child->x = old_x + placement_delta_x;
                block_child->y = old_y + placement_delta_y;
            }
        };
        auto adjust_nested_height = [&](MulticolFlowItem&, ViewBlock* block_child,
                                        bool first_item, float* placed_height,
                                        float) {
            if (!first_item || leading_fragment_border_consumed ||
                leading_fragment_border_height <= 0.0f) return;
            *placed_height += leading_fragment_border_height;
            if (block_child->height > 0.0f) {
                block_child->height += leading_fragment_border_height;
            }
            leading_fragment_border_consumed = true;
        };
        auto handle_nested_content = [&](MulticolFlowItem&, ViewBlock*, float*) {
            return false;
        };
        multicol_distribute_flow_group(
            lycon, container, children, group_start, group_end, target_height,
            current_y, continues_after_tall_spanner, &group, &cursor, &used_column_count,
            adjust_nested_placement, handle_nested_content, adjust_nested_height);

        multicol_group_finish(&group, &cursor);
        if (multicol_uses_fixed_balanced_rows(container)) {
            float row_height = multicol_specified_row_height(container);
            if (group_total_height > target_height + group.row_gap + 0.5f) {
                group.group_used_height += group.row_gap;
            } else if (group_start == 0 && group_total_height >= row_height - 0.5f) {
                group.group_used_height = max(group.group_used_height,
                    row_height + group.row_gap);
            }
        }
        for (int fi = 0; fi < group.fragment_count; fi++) {
            int candidate = group.fragments[fi].column_index + 1;
            if (candidate > used_column_count) used_column_count = candidate;
        }
        current_y += group.group_used_height;
        if (group_end < child_count && children[group_end].spans_all &&
            group.group_used_height > 0.0f && group.wraps_rows &&
            container->multicol_prop()->fill == COLUMN_FILL_AUTO &&
            !multicol_has_definite_ancestor_fragmentainer(container)) {
            // css gaps §2.1: the next spanner line follows the preceding
            // column line across its row gutter.
            current_y += group.row_gap;
        }
        prev_margin_bottom = 0;
    }

    if (spanner_starts_wrapper_flow && spanner_extent > 0.0f) {
        // css multicol §6: an escaping spanner occupies the containing flow;
        // following descendants retain the wrapper's local block-start edge.
        for (int k = 0; k < child_count; k++) {
            if (!children[k].spans_all) {
                layout_shift_view_tree(
                    static_cast<View*>(children[k].block), 0.0f,
                    -spanner_extent);
            }
        }
    }

    bool final_leading_text_consumed = false;
    bool after_spanner = false;
    for (int k = 0; k < child_count; k++) {
        if (children[k].spans_all) {
            after_spanner = true;
            continue;
        }
        float text_offset_y = 0;
        if (!final_leading_text_consumed && leading_fragment_border_height > 0) {
            text_offset_y = leading_fragment_border_height;
            final_leading_text_consumed = true;
        }
        DomElement* block_elem = lam::dom_require<DOM_NODE_ELEMENT>(children[k].block);
        if (block_elem->layout_fragment_list() && block_elem->layout_fragments_count() > 1) {
            if (text_offset_y > 0) {
                float target_y = text_offset_y + multicol_first_text_height(static_cast<View*>(children[k].block));
                View* text_child = block_elem->first_child;
                while (text_child) {
                    if (text_child->node_type == DOM_NODE_TEXT || text_child->node_type == DOM_NODE_ELEMENT) {
                        multicol_reanchor_text_descendants(text_child, 0, target_y);
                    }
                    text_child = text_child->next_sibling;
                }
            } else if (after_spanner) {
                // css multicol §6: the first line in a post-spanner group starts at that group's block-start edge.
                View* text_child = block_elem->first_child;
                while (text_child) {
                    if (text_child->node_type == DOM_NODE_TEXT || text_child->node_type == DOM_NODE_ELEMENT) {
                        multicol_reanchor_text_descendants(
                            text_child, 0.0f, 0.0f);
                    }
                    text_child = text_child->next_sibling;
                }
            }
        } else {
            multicol_reanchor_direct_text(children[k].block, text_offset_y);
        }
    }

    if (has_inline_wrapper) {
        multicol_finalize_text_for_fragmented_block(
            static_cast<View*>(child), child, true);
        for (View* descendant = child->first_placed_child(); descendant;
             descendant = descendant->next()) {
            if (descendant->view_type != RDT_VIEW_INLINE) continue;
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(descendant);
            span->set_has_fragment_union(FRAGMENT_UNION_SPLIT_INLINE, false);
            if (multicol_rebuild_flattened_inline_span_union(lycon, span, child)) {
                compute_span_bounding_box(
                    span, inline_span_has_multiple_line_fragments(span), nullptr);
            }
            multicol_reanchor_float_only_inline_span(span);
        }
    }

    float flow_height = current_y - child_origin_y;
    if (has_inline_text && inline_total_extent > flow_height) {
        flow_height = inline_total_extent;
    }
    if (flow_height < 0) flow_height = 0;
    float new_height = flow_height;
    if (child->blk && child->block_mut()->given_height >= 0 && first_group_target_height > 0) {
        float child_border_padding_height = 0;
        if (child->bound && !layout_uses_border_box(child)) {
            child_border_padding_height = layout_box_metrics(child).pad_border_v;
        }
        float decorated_split_adjustment = child_border_padding_height > 0 ?
            child_border_padding_height + spanner_extent : 0;
        float fragmented_visual_height = column_count <= 1 ?
            child->block()->given_height + child_border_padding_height + spanner_extent :
            child->block()->given_height + decorated_split_adjustment - first_group_target_height;
        if (fragmented_visual_height > 0 && fragmented_visual_height < new_height) {
            new_height = fragmented_visual_height;
        }
    }
    float union_width = used_column_count * column_width + (used_column_count - 1) * column_gap;
    float full_width = container->width > 0 ?
        container->width : column_count * column_width + (column_count - 1) * column_gap;
    if (full_width > union_width) union_width = full_width;
    if (non_spanner_count == 0 && spanner_extent <= 0.0f && !has_inline_text &&
        fragmentainer_height > 0.0f && column_count > 1 && child->blk &&
        child->block()->given_height >= 0.0f) {
        // css multicol: an empty out-of-flow spanner still forces the following
        // monolithic wrapper into the next column; retain its authored width and
        // content height while exposing only the current fragmentainer height.
        int continuation_column = min(child_count, column_count - 1);
        float row_gap = multicol_row_gap(container);
        if (row_gap < 0.0f) row_gap = 0.0f;
        int continuation_row = multicol_group_wraps_rows(container)
            ? child_count / column_count : 0;
        if (multicol_group_wraps_rows(container)) {
            continuation_column = child_count % column_count;
        }
        child->x = original_child_x + continuation_column * (column_width + column_gap);
        child->y = child_fragment_origin_y + continuation_row *
            (fragmentainer_height + row_gap);
        child->width = original_child_width;
        child->height = min(original_child_height, fragmentainer_height);
        child->content_height = original_child_height;
        for (int k = 0; k < child_count; k++) {
            if (children[k].spans_all) {
                // the escaped spanner remains in the preceding column line;
                // only the wrapper continuation advances past its break.
                children[k].block->x = original_child_x -
                    continuation_column * (column_width + column_gap);
            }
        }
        used_column_count = max(used_column_count,
            continuation_row > 0 ? column_count : continuation_column + 1);
        if (used_column_count > container->multicol_prop()->computed_used_column_count) {
            container->multicol_prop()->computed_used_column_count = used_column_count;
        }
        group_scratch.release(&lycon->scratch);
        flow_scratch.release(&lycon->scratch);
        return child->height;
    }
    if (non_spanner_count == 0 && spanner_extent > 0 && !has_inline_text &&
        !multicol_has_vertical_inline_axis(container) &&
        leading_fragment_border_height > 0.0f &&
        leading_fragment_border_height < original_child_height) {
        float column_origin_x = layout_axis_decoration_start(
            container->bound ? container->boundary() : nullptr, LAYOUT_AXIS_X);
        for (int k = 0; k < child_count; k++) {
            if (!children[k].spans_all) continue;
            children[k].block->x = column_origin_x - child->x;
            children[k].block->y = leading_fragment_border_height;
        }
        child->width = column_width;
        child->height = leading_fragment_border_height;
        child->content_height = leading_fragment_border_height;
        if (out_detached_spanner_extent) *out_detached_spanner_extent = spanner_extent;
        // css multicol §6: retain the wrapper's leading fragment; its spanner
        // is taken out of flow and continues at the following full-width line.
        return leading_fragment_border_height;
    }
    if (child->blk && child->block()->given_height >= 0.0f &&
        non_spanner_count > 0 && !has_inline_text &&
        first_group_target_height > 0.0f &&
        original_child_height <= first_group_target_height) {
        // css multicol: a fixed-height wrapper keeps its authored border box;
        // fragmented descendants may overflow that box around the spanner.
        child->width = original_child_width;
        child->height = original_child_height;
        child->content_height = original_child_height;
    } else if (non_spanner_count == 0 && spanner_extent > 0 && !has_inline_text) {
        float child_margin_after = 0.0f;
        multicol_flow_margins(container, child, nullptr, &child_margin_after);
        layout_shift_view_tree(static_cast<View*>(child), 0, -flow_height);
        for (int k = 0; k < child_count; k++) {
            if (children[k].spans_all) {
                if (!nested_wrapper && child_margin_after > 0.0f) {
                    children[k].block->y -= child_margin_after;
                }
                // css multicol: moving a spanner out of normal flow carries
                // its in-flow descendants with the spanner's fragment.
                layout_shift_view_children(
                    static_cast<View*>(children[k].block), 0, flow_height);
            }
        }
        // css-break: the wrapper continuation box follows its escaped
        // spanner and the wrapper's trailing collapsed margin.
        child->y = nested_wrapper ? 0.0f :
            child_fragment_origin_y + flow_height + child_margin_after;
        child->width = column_width;
        child->height = 0;
        child->content_height = 0;
    } else {
        if (has_inline_wrapper) {
            // css multicol §6: an inline wrapper keeps its own border box while
            // its descendants contribute the larger fragmented flow extent.
            child->width = original_child_width;
            child->height = original_child_height;
            child->content_height = original_child_height;
        } else if (has_inline_text) {
            child->x = original_child_x;
            child->y = inline_origin_y;
            child->width = original_child_width;
            child->height = new_height;
            child->content_height = new_height;
        } else {
            if (child->width < union_width) child->width = union_width;
            child->height = new_height;
            child->content_height = new_height;
        }
    }
    if (used_column_count > container->multicol_prop()->computed_used_column_count) {
        container->multicol_prop()->computed_used_column_count = used_column_count;
    }

    group_scratch.release(&lycon->scratch);
    flow_scratch.release(&lycon->scratch);
    return flow_height;
}

static float multicol_group_target_height(ViewBlock* block, float balanced_height, float group_total_height) {
    if (!block || !block->multicol_prop()) return balanced_height;

    if (group_total_height <= 0.0f &&
        block->multicol_prop()->fill == COLUMN_FILL_BALANCE) {
        float out_of_flow_target = multicol_out_of_flow_balance_target(block);
        if (out_of_flow_target > 0.0f) {
            return out_of_flow_target;
        }
    }

    float limit = multicol_content_box_height_limit(block);
    float explicit_min_height = layout_explicit_min_axis_or(block, false, -1.0f);
    if (limit < 0.0f && block->multicol_prop()->fill == COLUMN_FILL_AUTO &&
        explicit_min_height >= 0.0f &&
        multicol_has_definite_ancestor_fragmentainer(block)) {
        // css sizing: a nested auto-height multicol honors its minimum as the
        // fragmentainer size while overflow continues into later columns.
        limit = explicit_min_height;
    }
    if (limit >= 0.0f && multicol_allows_overflow_columns(block) &&
        multicol_is_scroll_container(block)) {
        // css multicol-2 §4.4: a finite nowrap sequence uses its fragmentainer
        // height and creates additional inline columns instead of balancing.
        return limit;
    }
    if (block->multicol_prop()->fill == COLUMN_FILL_AUTO) {
        if (multicol_is_nested_in_balancing_context(block)) {
            // css multicol §7.1: an auto-filled nested flow is balanced while
            // its outer multicol is determining the column block size.
            return balanced_height;
        }
        if (limit >= 0) return limit;
        return group_total_height;
    }
    if (block->multicol_prop()->column_height_is_specified && limit >= 0) {
        // css multicol-2 §4.2: a specified column-height fixes the
        // fragmentainer size even when columns remain on one line.
        return limit;
    }
    return balanced_height;
}

static float multicol_adjust_inline_balance_target(
    ViewBlock* block,
    float target_height,
    float first_line_box_offset,
    float first_line_height,
    float line_advance,
    float max_line_height,
    int line_count,
    int column_count
) {
    if (!block || !block->multicol_prop() || first_line_box_offset <= 0.0f ||
        line_count <= 0 || column_count <= 0 ||
        block->multicol_prop()->fill != COLUMN_FILL_BALANCE ||
        multicol_content_box_height_limit(block) >= 0.0f) {
        return target_height;
    }

    // css multicol balancing includes a leading line box before distributing its continuations.
    float minimum_target = max(
        first_line_box_offset + first_line_height,
        line_advance + max_line_height);
    if (target_height < minimum_target) target_height = minimum_target;

    int balance_guard = 0;
    while (balance_guard < line_count) {
        int first_fragment_fit = multicol_lines_that_fit_fragment(
            target_height - first_line_box_offset, line_advance,
            first_line_height, multicol_normal_line_offset(line_advance, first_line_height));
        int continuation_fit = multicol_lines_that_fit_fragment(
            target_height, line_advance, max_line_height);
        if (first_fragment_fit < 1) first_fragment_fit = 1;
        if (continuation_fit < 1) continuation_fit = 1;
        int remaining_lines = line_count - first_fragment_fit;
        int projected_fragments = 1;
        if (remaining_lines > 0) {
            projected_fragments += (remaining_lines + continuation_fit - 1) /
                continuation_fit;
        }
        if (projected_fragments <= column_count) break;
        target_height += line_advance;
        balance_guard++;
    }
    return target_height;
}

struct DirectInlineFlowEnd {
    int fragment_index;
    float next_line_box_offset;
    bool has_visible_lines;
};

struct MulticolMixedInlineRecord {
    ViewText* text;
    TextRect* rect;
    View* br;
    float original_x;
    float original_y;
    float height;
    int line_index;
};

struct MulticolMixedInlineLine {
    float original_y;
    float line_height;
};

struct MulticolMixedFlowItem {
    MulticolFlowItem flow;
    bool inline_flow;
    int line_start;
    int line_count;
    float line_advance;
};

static bool multicol_collect_mixed_inline_view(
    View* current,
    MulticolMixedInlineRecord* records,
    int* record_count
);

static bool multicol_collect_mixed_inline_records(
    View* first,
    MulticolMixedInlineRecord* records,
    int* record_count
) {
    if (!first || !records || !record_count) return true;
    for (View* current = first; current; current = current->next()) {
        if (!multicol_collect_mixed_inline_view(current, records, record_count)) {
            return false;
        }
    }
    return true;
}

static bool multicol_collect_mixed_inline_view(
    View* current,
    MulticolMixedInlineRecord* records,
    int* record_count
) {
    if (!current || !records || !record_count) return true;
    if (*record_count >= MAX_MULTICOL_BLOCKS) return false;
    if (current->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(current);
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->width <= 0.0f || rect->height <= 0.0f ||
                layout_text_rect_content_kind(text, rect) ==
                    LAYOUT_TEXT_RECT_COLLAPSED_WHITESPACE) {
                continue;
            }
            MulticolMixedInlineRecord& record = records[*record_count];
            record.text = text;
            record.rect = rect;
            record.br = nullptr;
            record.original_x = rect->x;
            record.original_y = rect->y;
            record.height = rect->height;
            record.line_index = -1;
            (*record_count)++;
        }
        return true;
    }
    if (current->view_type == RDT_VIEW_BR) {
        MulticolMixedInlineRecord& record = records[*record_count];
        record.text = nullptr;
        record.rect = nullptr;
        record.br = current;
        record.original_x = current->x;
        record.original_y = current->y;
        record.height = current->height;
        record.line_index = -1;
        (*record_count)++;
        return true;
    }
    if (current->view_type == RDT_VIEW_INLINE) {
        ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(current);
        if (!multicol_collect_mixed_inline_records(
                span->first_child, records, record_count)) {
            return false;
        }
        return true;
    }
    if (current->view_type == RDT_VIEW_NONE || current->view_type == 0) {
        return true;
    }
    // a nested block needs its own anonymous flow box; leave it to the
    // ordinary spanner/fragmentation paths until that box is represented.
    if (lam::view_as_block(current)) return false;
    return false;
}

static bool multicol_reflow_mixed_direct_flow(
    LayoutContext* lycon,
    ViewBlock* block,
    int column_count,
    float column_width,
    float column_gap,
    float column_group_origin_x,
    float content_start_y,
    float* out_height,
    int* out_used_columns
) {
    if (out_height) *out_height = 0.0f;
    if (out_used_columns) *out_used_columns = 0;
    if (!lycon || !block || column_count <= 1 || column_width <= 0.0f) {
        return false;
    }
    MulticolMixedFlowItem* items = (MulticolMixedFlowItem*)scratch_calloc(
        &lycon->scratch, MAX_MULTICOL_BLOCKS * sizeof(MulticolMixedFlowItem));
    MulticolMixedInlineRecord* records = (MulticolMixedInlineRecord*)scratch_calloc(
        &lycon->scratch, MAX_MULTICOL_BLOCKS * sizeof(MulticolMixedInlineRecord));
    MulticolMixedInlineLine* lines = (MulticolMixedInlineLine*)scratch_calloc(
        &lycon->scratch, MAX_MULTICOL_BLOCKS * sizeof(MulticolMixedInlineLine));
    if (!items || !records || !lines) {
        if (lines) scratch_free(&lycon->scratch, lines);
        if (records) scratch_free(&lycon->scratch, records);
        if (items) scratch_free(&lycon->scratch, items);
        return false;
    }

    int item_count = 0;
    int record_count = 0;
    int line_count = 0;
    int block_count = 0;
    bool has_inline_flow = false;
    bool collection_ok = true;

    auto append_inline_item = [&](int record_start) -> bool {
        if (record_start >= record_count || item_count >= MAX_MULTICOL_BLOCKS) {
            return true;
        }

        int line_start = line_count;
        for (int ri = record_start; ri < record_count; ri++) {
            MulticolMixedInlineRecord& record = records[ri];
            int matching_line = -1;
            for (int li = line_start; li < line_count; li++) {
                if (fabsf(lines[li].original_y - record.original_y) <= 1.0f) {
                    matching_line = li;
                    break;
                }
            }
            if (matching_line < 0) {
                if (line_count >= MAX_MULTICOL_BLOCKS) return false;
                matching_line = line_count++;
                lines[matching_line].original_y = record.original_y;
                lines[matching_line].line_height = record.height;
            } else if (record.height > lines[matching_line].line_height) {
                lines[matching_line].line_height = record.height;
            }
            record.line_index = matching_line;
        }

        float line_advance = -1.0f;
        for (int li = line_start + 1; li < line_count; li++) {
            float delta = lines[li].original_y - lines[li - 1].original_y;
            if (delta > 1.0f && (line_advance < 0.0f || delta < line_advance)) {
                line_advance = delta;
            }
        }
        if (line_advance <= 0.0f && block->blk && block->block()->line_height) {
            line_advance = layout_resolve_line_height_value(
                lycon, block->block()->line_height,
                lam::dom_require<DOM_NODE_ELEMENT>(block),
                block->fontp() ? block->fontp()->font_size : 16.0f);
        }
        if (line_advance <= 0.0f && block->fontp() && block->fontp()->font_handle) {
            line_advance = calc_normal_line_height(block->fontp()->font_handle);
        }
        if (line_advance <= 0.0f) line_advance = lines[line_start].line_height;
        if (line_advance <= 0.0f) line_advance = 16.0f;

        MulticolMixedFlowItem& item = items[item_count++];
        item.flow = {};
        item.flow.height = (line_count - line_start) * line_advance;
        item.flow.content_height = item.flow.height;
        item.flow.can_fragment = true;
        item.inline_flow = true;
        item.line_start = line_start;
        item.line_count = line_count - line_start;
        item.line_advance = line_advance;
        has_inline_flow = true;
        return true;
    };

    int pending_inline_start = 0;
    for (DomNode* child = block->first_child; child; child = child->next_sibling) {
        View* child_view = static_cast<View*>(child);
        ViewBlock* child_block = lam::view_as_block(child_view);
        if (child_block) {
            if (layout_block_is_out_of_flow_positioned(child_block)) continue;
            if (child_block->view_type != RDT_VIEW_BLOCK ||
                multicol_is_spanner_block(child_block) ||
                multicol_has_direct_spanner_child(child_block)) {
                collection_ok = false;
                break;
            }
            if (!append_inline_item(pending_inline_start)) {
                collection_ok = false;
                break;
            }
            pending_inline_start = record_count;
            if (item_count >= MAX_MULTICOL_BLOCKS) {
                collection_ok = false;
                break;
            }
            MulticolMixedFlowItem& item = items[item_count++];
            item.flow = {};
            item.flow.block = child_block;
            item.flow.height = multicol_child_flow_extent(block, child_block);
            multicol_flow_margins(block, child_block,
                                  &item.flow.margin_before,
                                  &item.flow.margin_after);
            item.flow.content_height = item.flow.height -
                item.flow.margin_before - item.flow.margin_after;
            if (item.flow.content_height < 0.0f) item.flow.content_height = 0.0f;
            item.flow.can_fragment = multicol_has_fragmentable_line_boxes(child_block) ||
                multicol_has_fragmentable_block_children(child_block);
            if (child_block->blk &&
                child_block->block()->break_inside == CSS_VALUE_AVOID) {
                item.flow.can_fragment = false;
            }
            item.flow.break_before_column = child_block->blk &&
                multicol_forces_column_break(child_block->block()->break_before);
            item.flow.break_after_column = child_block->blk &&
                multicol_forces_column_break(child_block->block()->break_after);
            item.flow.break_before_avoid = child_block->blk &&
                multicol_avoids_column_break(child_block->block()->break_before);
            item.flow.break_after_avoid = child_block->blk &&
                multicol_avoids_column_break(child_block->block()->break_after);
            item.inline_flow = false;
            item.line_start = 0;
            item.line_count = 0;
            item.line_advance = 0.0f;
            block_count++;
            continue;
        }

        int record_start = record_count;
        if (!multicol_collect_mixed_inline_view(
                child_view, records, &record_count)) {
            collection_ok = false;
            break;
        }
        if (record_count > record_start) continue;
        if (child_view->view_type != RDT_VIEW_NONE &&
            child_view->view_type != 0 &&
            child_view->view_type != RDT_VIEW_INLINE) {
            collection_ok = false;
            break;
        }
    }
    if (collection_ok && !append_inline_item(pending_inline_start)) {
        collection_ok = false;
    }
    if (!collection_ok || !has_inline_flow || block_count < 2 || item_count <= block_count) {
        scratch_free(&lycon->scratch, lines);
        scratch_free(&lycon->scratch, records);
        scratch_free(&lycon->scratch, items);
        return false;
    }

    MulticolGroupScratch balance_scratch = {};
    if (!balance_scratch.init(&lycon->scratch)) {
        balance_scratch.release(&lycon->scratch);
        scratch_free(&lycon->scratch, lines);
        scratch_free(&lycon->scratch, records);
        scratch_free(&lycon->scratch, items);
        return false;
    }
    float total_height = 0.0f;
    for (int i = 0; i < item_count; i++) {
        balance_scratch.heights[i] = items[i].flow.height;
        balance_scratch.content_heights[i] = items[i].flow.content_height;
        balance_scratch.margin_before[i] = items[i].flow.margin_before;
        balance_scratch.margin_after[i] = items[i].flow.margin_after;
        balance_scratch.can_fragment[i] = items[i].flow.can_fragment;
        balance_scratch.break_before[i] = items[i].flow.break_before_column;
        balance_scratch.break_after[i] = items[i].flow.break_after_column;
        total_height += items[i].flow.height;
    }
    float target_height = multicol_group_target_height(
        block, ceilf(total_height / column_count), total_height);
    target_height = multicol_balanced_target_search(
        block, balance_scratch.heights, balance_scratch.can_fragment,
        balance_scratch.break_before, balance_scratch.break_after,
        item_count, column_count, target_height, total_height,
        balance_scratch.content_heights, balance_scratch.margin_before,
        balance_scratch.margin_after);
    balance_scratch.release(&lycon->scratch);
    if (target_height <= 0.0f) {
        scratch_free(&lycon->scratch, lines);
        scratch_free(&lycon->scratch, records);
        scratch_free(&lycon->scratch, items);
        return false;
    }

    float row_gap = multicol_row_gap(block);
    if (row_gap < 0.0f) row_gap = 0.0f;
    float pitch = column_width + column_gap;
    int fragment_index = 0;
    float fragment_used = 0.0f;
    float max_height = 0.0f;
    int used_columns = 0;

    auto advance_fragment = [&]() {
        float fragment_end = (float)(fragment_index / column_count) *
            (target_height + row_gap) + fragment_used;
        if (fragment_end > max_height) max_height = fragment_end;
        fragment_index++;
        fragment_used = 0.0f;
    };
    auto fragment_column = [&]() -> int {
        return fragment_index % column_count;
    };
    auto fragment_row = [&]() -> int {
        return fragment_index / column_count;
    };

    for (int i = 0; i < item_count; i++) {
        MulticolMixedFlowItem& item = items[i];
        if (item.flow.break_before_column && fragment_used > 0.0f) {
            advance_fragment();
        }

        if (item.inline_flow) {
            for (int li = 0; li < item.line_count; li++) {
                MulticolMixedInlineLine& line = lines[item.line_start + li];
                if (fragment_used > 0.0f &&
                    fragment_used + item.line_advance > target_height + 0.5f) {
                    advance_fragment();
                }
                int column = fragment_column();
                int row = fragment_row();
                float line_y = content_start_y + row * (target_height + row_gap) +
                    fragment_used;
                for (int ri = 0; ri < record_count; ri++) {
                    MulticolMixedInlineRecord& record = records[ri];
                    if (record.line_index != item.line_start + li) continue;
                    float line_delta = record.original_y - line.original_y;
                    if (record.rect) {
                        record.rect->x = record.original_x + column * pitch;
                        record.rect->y = line_y + line_delta;
                    } else if (record.br) {
                        record.br->x = record.original_x + column * pitch;
                        record.br->y = line_y + line_delta;
                    }
                }
                fragment_used += item.line_advance;
            }
        } else {
            float item_height = item.flow.height;
            if (fragment_used > 0.0f &&
                fragment_used + item_height > target_height + 0.5f) {
                advance_fragment();
            }
            int column = fragment_column();
            int row = fragment_row();
            ViewBlock* child_block = item.flow.block;
            multicol_clear_layout_fragments(child_block);
            child_block->x = column_group_origin_x + column * pitch;
            child_block->y = content_start_y + row * (target_height + row_gap) +
                fragment_used + item.flow.margin_before;
            // child geometry is local to the moved formatting context; shifting
            // it with the principal box would apply the column offset twice.
            fragment_used += item_height;
        }

        int item_column = fragment_column();
        if (item.flow.break_after_column && fragment_used > 0.0f) {
            advance_fragment();
        }
        if (item_column + 1 > used_columns) used_columns = item_column + 1;
    }
    float final_end = (float)(fragment_index / column_count) *
        (target_height + row_gap) + fragment_used;
    if (final_end > max_height) max_height = final_end;

    for (int ri = 0; ri < record_count; ri++) {
        if (records[ri].text) adjust_text_bounds(records[ri].text);
    }
    if (out_height) *out_height = max_height;
    if (out_used_columns) *out_used_columns = used_columns;

    scratch_free(&lycon->scratch, lines);
    scratch_free(&lycon->scratch, records);
    scratch_free(&lycon->scratch, items);
    return true;
}

static float multicol_project_mixed_direct_inline_content(
    ViewBlock* block,
    int column_count,
    float column_width,
    float column_gap,
    float target_height,
    DirectInlineFlowEnd* flow_end,
    float flow_start_offset = -1.0f
) {
    if (flow_end) {
        flow_end->fragment_index = 0;
        flow_end->next_line_box_offset = 0.0f;
        flow_end->has_visible_lines = false;
    }
    if (!block || column_count <= 0 || column_width <= 0.0f || target_height <= 0.0f) {
        return 0.0f;
    }

    struct DirectInlineLine {
        TextRect* rect;
        float original_x;
        float original_y;
        float new_y;
        float line_height;
        int fragment_index;
    };

    struct DirectInlineBreak {
        View* view;
        float original_x;
        float original_y;
        int line_index;
    };

    constexpr int MAX_DIRECT_INLINE_LINES = 512;
    DirectInlineLine lines[MAX_DIRECT_INLINE_LINES];
    DirectInlineBreak breaks[MAX_DIRECT_INLINE_LINES];
    bool normalize_to_flow_start = flow_start_offset >= 0.0f;
    int line_count = 0;
    int break_count = 0;

    auto collect_inline_leaf = [&](View* leaf) {
        if (leaf->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(leaf);
            TextRect* rect = text->rect;
            while (rect && line_count < MAX_DIRECT_INLINE_LINES) {
                if (layout_text_rect_content_kind(text, rect) ==
                    LAYOUT_TEXT_RECT_COLLAPSED_WHITESPACE) {
                    rect = rect->next;
                    continue;
                }
                lines[line_count].rect = rect;
                lines[line_count].original_x = rect->x;
                lines[line_count].original_y = rect->y;
                lines[line_count].new_y = rect->y;
                lines[line_count].line_height = rect->height;
                lines[line_count].fragment_index = 0;
                line_count++;
                rect = rect->next;
            }
        } else if (leaf->view_type == RDT_VIEW_BR && break_count < MAX_DIRECT_INLINE_LINES) {
            breaks[break_count].view = leaf;
            breaks[break_count].original_x = leaf->x;
            breaks[break_count].original_y = leaf->y;
            breaks[break_count].line_index = -1;
            break_count++;
        }
    };

    if (normalize_to_flow_start) {
        multicol_for_each_inline_leaf(block->first_placed_child(), collect_inline_leaf);
    } else {
        View* child = block->first_child;
        while (child) {
            collect_inline_leaf(child);
            child = child->next_sibling;
        }
    }

    if (line_count == 0) return 0.0f;

    if (normalize_to_flow_start) {
        float first_phase_line_y = lines[0].original_y;
        for (int li = 0; li < line_count; li++) {
            lines[li].original_y = flow_start_offset +
                lines[li].original_y - first_phase_line_y;
        }
        for (int bi = 0; bi < break_count; bi++) {
            breaks[bi].original_y = flow_start_offset +
                breaks[bi].original_y - first_phase_line_y;
        }
    }

    float line_advance = -1.0f;
    for (int i = 1; i < line_count; i++) {
        float delta = fabsf(lines[i].original_y - lines[i - 1].original_y);
        if (delta > 1.0f && (line_advance < 0.0f || delta < line_advance)) {
            line_advance = delta;
        }
    }
    float visual_height = lines[0].line_height > 0.0f ? lines[0].line_height : 0.0f;
    float max_line_height = visual_height;
    for (int li = 1; li < line_count; li++) {
        max_line_height = max(max_line_height, lines[li].line_height);
    }
    if (line_advance <= 0.0f) line_advance = visual_height > 0.0f ? visual_height : 16.0f;
    if (visual_height <= 0.0f) visual_height = line_advance;

    float normal_line_offset = multicol_normal_line_offset(line_advance, visual_height);
    float first_line_box_y = normalize_to_flow_start
        ? lines[0].original_y - normal_line_offset
        : lines[0].original_y - block->y - normal_line_offset;
    if (first_line_box_y < 0.0f && first_line_box_y > -normal_line_offset - 0.5f) {
        first_line_box_y = 0.0f;
    }
    if (normalize_to_flow_start) {
        target_height = multicol_adjust_inline_balance_target(
            block, target_height, first_line_box_y, lines[0].line_height,
            line_advance, max_line_height, line_count, column_count);
    }

    int start_fragment = (int)floorf(first_line_box_y / target_height); // INT_CAST_OK: fragment index from positive flow offset
    if (start_fragment < 0) start_fragment = 0;
    float start_offset = first_line_box_y - start_fragment * target_height;
    if (start_offset < 0.0f) start_offset = 0.0f;

    int orphans = block->blk && block->block_mut()->orphans > 0 ? block->block_mut()->orphans : 2;
    int widows = block->blk && block->block_mut()->widows > 0 ? block->block_mut()->widows : 2;
    int first_fragment_fit = multicol_lines_that_fit_fragment(
        target_height - start_offset, line_advance, visual_height, normal_line_offset);
    bool broke_before_run = false;
    if (start_offset > 0.0f && line_count >= orphans && first_fragment_fit < orphans) {
        start_fragment++;
        start_offset = 0.0f;
        broke_before_run = true;
    }

    int current_fragment = start_fragment;
    float fragment_start_offset = start_offset;
    int line_slot = 0;
    float max_used_height = 0.0f;
    float pitch = column_width + column_gap;
    bool vertical_writing = multicol_has_vertical_inline_axis(block);
    bool trims_root_start = block->blk &&
        (block->block()->text_box_trim & TEXT_BOX_TRIM_START);

    for (int li = 0; li < line_count; li++) {
        float line_offset = fragment_start_offset > 0.0f ? normal_line_offset : 0.0f;
        float visual_bottom = fragment_start_offset + line_slot * line_advance +
            line_offset + visual_height;
        if (trims_root_start && current_fragment == start_fragment) {
            // The root trim removes the first line's start half-leading for
            // fragmentation capacity, without moving its phase-one glyph box.
            visual_bottom -= normal_line_offset;
        }
        bool should_break = line_slot > 0 && visual_bottom > target_height + 0.5f;
        if (!should_break && widows > 1 && li + 1 < line_count) {
            int remaining_after_this = line_count - (li + 1);
            int remaining_with_this = line_count - li;
            if (remaining_after_this > 0 &&
                remaining_after_this < widows &&
                remaining_with_this >= widows &&
                line_slot + 1 >= orphans) {
                float next_bottom = fragment_start_offset + (line_slot + 1) * line_advance +
                    line_offset + visual_height;
                should_break = next_bottom > target_height + 0.5f;
            }
        }

        if (should_break) {
            current_fragment++;
            fragment_start_offset = 0.0f;
            line_slot = 0;
            line_offset = 0.0f;
        }

        int column_index = current_fragment % column_count;
        int row_index = current_fragment / column_count;
        float row_y = row_index * target_height;
        float new_y = row_y + fragment_start_offset +
            line_slot * line_advance + line_offset;
        int inline_column = vertical_writing && block->block()->direction == CSS_VALUE_RTL
            ? column_count - 1 - column_index : column_index;
        lines[li].rect->x = lines[li].original_x + inline_column * pitch +
            multicol_vertical_rtl_inline_end_offset(
                block, lines[li].rect->width, column_width);
        lines[li].rect->y = new_y;
        lines[li].new_y = new_y;
        lines[li].fragment_index = current_fragment;

        float used_height = row_y + fragment_start_offset + line_slot * line_advance +
            line_offset + visual_height;
        if (used_height > max_used_height) max_used_height = used_height;
        line_slot++;
    }

    if (flow_end) {
        flow_end->fragment_index = current_fragment;
        flow_end->next_line_box_offset = fragment_start_offset + line_slot * line_advance;
        flow_end->has_visible_lines = true;
    }

    if (current_fragment > 0) {
        // direct inline runs need the same first-fragment metadata as block
        // descendants so text-box-trim can leave continuation fragments at
        // their own fragmentainer start edge.
        float row_gap = multicol_row_gap(block);
        if (row_gap < 0.0f) row_gap = 0.0f;
        multicol_store_layout_fragments(block, current_fragment + 1, column_count,
            target_height, column_width, column_gap, row_gap, column_width, 0.0f);
    }

    for (int bi = 0; bi < break_count; bi++) {
        int matched_line = -1;
        for (int li = 0; li < line_count; li++) {
            if (fabsf(breaks[bi].original_y - lines[li].original_y) <= 1.0f) {
                matched_line = li;
                break;
            }
        }
        if (matched_line < 0 && bi < line_count) matched_line = bi;
        if (matched_line < 0) continue;

        DirectInlineLine& line = lines[matched_line];
        int column_index = line.fragment_index % column_count;
        int inline_column = vertical_writing && block->block()->direction == CSS_VALUE_RTL
            ? column_count - 1 - column_index : column_index;
        if (broke_before_run || line.fragment_index == start_fragment) {
            breaks[bi].view->x = breaks[bi].original_x + inline_column * pitch;
            breaks[bi].view->y = line.new_y;
        } else {
            breaks[bi].view->x = breaks[bi].original_x;
            breaks[bi].view->y = breaks[bi].original_y - first_line_box_y;
        }
    }

    return max_used_height;
}

static bool multicol_text_has_visible_rect(DomText* text) {
    if (!text) return false;
    for (TextRect* rect = text->rect; rect; rect = rect->next) {
        if (rect->width > 0.0f && rect->height > 0.0f &&
            layout_text_rect_content_kind(
                lam::view_require_text(static_cast<View*>(text)), rect) !=
                LAYOUT_TEXT_RECT_COLLAPSED_WHITESPACE) {
            return true;
        }
    }
    return false;
}

static bool multicol_place_direct_block_after_inline_flow(
    ViewBlock* block,
    const DirectInlineFlowEnd& flow_end,
    int column_count,
    float column_width,
    float column_gap,
    float column_group_origin_x,
    float content_start_y,
    float target_height,
    float* flow_extent
) {
    if (!block || !flow_end.has_visible_lines || column_count <= 0 ||
        column_width <= 0.0f || target_height <= 0.0f) {
        return false;
    }

    ViewBlock* direct_block = nullptr;
    bool saw_visible_inline = false;
    for (DomNode* child = block->first_child; child; child = child->next_sibling) {
        if (child->node_type == DOM_NODE_TEXT) {
            DomText* text = lam::dom_require<DOM_NODE_TEXT>(child);
            if (!multicol_text_has_visible_rect(text)) continue;
            if (direct_block) return false;
            saw_visible_inline = true;
            continue;
        }
        if (!child->is_element()) return false;

        ViewBlock* child_block = lam::view_as_block(static_cast<View*>(child));
        if (!child_block || layout_block_is_out_of_flow_positioned(child_block)) return false;
        if (multicol_is_spanner_block(child_block) || child_block->view_type != RDT_VIEW_BLOCK ||
            !saw_visible_inline || direct_block) {
            return false;
        }
        direct_block = child_block;
    }
    if (!direct_block) return false;

    float block_height = direct_block->height +
        layout_box_metrics(direct_block).margin_v;
    int fragment_index = flow_end.fragment_index;
    float line_box_offset = flow_end.next_line_box_offset;
    bool starts_new_fragment = line_box_offset + block_height > target_height + 0.5f;
    if (starts_new_fragment) {
        // css fragmentation: a monolithic float whose margin box cannot fit
        // after the preceding line starts in the next fragmentainer.
        bool has_next_fragment = fragment_index + 1 < column_count ||
            multicol_group_wraps_rows(block);
        if (!has_next_fragment) return false;
        fragment_index++;
        line_box_offset = 0.0f;
    }

    int column_index = fragment_index % column_count;
    int row_index = fragment_index / column_count;
    float row_gap = multicol_row_gap(block);
    if (row_gap < 0.0f) row_gap = 0.0f;
    float old_x = direct_block->x;
    float old_y = direct_block->y;
    float direct_origin_x = column_group_origin_x -
        layout_axis_decoration_start(block->bound, LAYOUT_AXIS_X);
    // direct block placement receives the already-inset line origin; remove
    // that inset here so the following block starts at the content edge.
    float margin_before = 0.0f;
    float margin_after = 0.0f;
    multicol_flow_margins(block, direct_block, &margin_before, &margin_after);
    direct_block->x = direct_origin_x + column_index * (column_width + column_gap);
    direct_block->y = content_start_y + row_index * (target_height + row_gap) +
        line_box_offset + margin_before;
    // CSS 2.1 §9.2.1.1 forms an anonymous block for the direct inline run.
    // It must consume the fragmentainer before its following block is placed.
    layout_shift_view_children(static_cast<View*>(direct_block),
                               direct_block->x - old_x, direct_block->y - old_y);

    if (flow_extent) {
        *flow_extent = row_index * (target_height + row_gap) +
            line_box_offset + block_height;
    }
    return true;
}

static int multicol_simulate_column_count(
    float* item_heights,
    bool* item_can_fragment,
    bool* break_before,
    bool* break_after,
    int item_count,
    int column_count,
    float target_height,
    float* item_content_heights,
    float* item_margin_before,
    float* item_margin_after
) {
    if (item_count <= 0 || target_height <= 0) return 1;

    if (item_content_heights && item_margin_before && item_margin_after) {
        int fragment_count = 1;
        float fragment_used = 0.0f;
        float pending_margin_after = 0.0f;
        bool has_item = false;
        for (int i = 0; i < item_count; i++) {
            if (break_before[i] && has_item) {
                fragment_count++;
                fragment_used = 0.0f;
                pending_margin_after = 0.0f;
                has_item = false;
            }

            float margin_before = has_item
                ? max(pending_margin_after, item_margin_before[i])
                : (i == 0 ? item_margin_before[i] : 0.0f);
            float needed = margin_before + item_content_heights[i];
            if (!has_item && item_count == 1 &&
                needed > target_height + 0.5f && margin_before > 0.0f &&
                item_content_heights[i] <= target_height + 0.5f) {
                if (!item_can_fragment[i]) {
                    // A monolithic block group cannot spend its start margin
                    // as a phantom continuation; balance must fit it whole.
                    return column_count + 1;
                }
                // A fragmentable block spends its start margin only in the
                // first fragment; its remaining content continues normally.
                float available = target_height - margin_before;
                if (available <= 0.0f) {
                    fragment_count++;
                    fragment_used = item_content_heights[i];
                } else if (item_content_heights[i] > available) {
                    fragment_count++;
                    fragment_used = item_content_heights[i] - available;
                    while (fragment_used > target_height + 0.5f) {
                        fragment_used -= target_height;
                        fragment_count++;
                    }
                } else {
                    fragment_used = needed;
                }
                pending_margin_after = item_margin_after[i];
                has_item = true;
                continue;
            }
            if (!has_item && needed > target_height + 0.5f && margin_before > 0.0f &&
                item_content_heights[i] <= target_height + 0.5f) {
                // The first block's start margin belongs to the group and
                // cannot be discarded to make an undersized fragmentainer fit.
                fragment_count++;
                fragment_used = item_content_heights[i];
                pending_margin_after = item_margin_after[i];
                has_item = true;
                continue;
            }
            if (has_item && fragment_used + needed > target_height + 0.5f) {
                fragment_count++;
                fragment_used = 0.0f;
                pending_margin_after = 0.0f;
                has_item = false;
                needed = item_content_heights[i];
            }

            if (!has_item && item_content_heights[i] > target_height + 0.5f) {
                if (item_can_fragment[i]) {
                    int extra_fragments = (int)ceilf(
                        item_content_heights[i] / target_height) - 1; // INT_CAST_OK: fragment count from positive heights
                    if (extra_fragments > 0) fragment_count += extra_fragments;
                    fragment_used = fmodf(item_content_heights[i], target_height);
                    if (fragment_used <= 0.5f) fragment_used = target_height;
                } else {
                    fragment_used = item_content_heights[i];
                }
            } else {
                fragment_used += needed;
            }
            pending_margin_after = item_margin_after[i];
            has_item = true;

            if (break_after[i] && i + 1 < item_count) {
                fragment_count++;
                fragment_used = 0.0f;
                pending_margin_after = 0.0f;
                has_item = false;
            }
        }
        return fragment_count;
    }

    int fragment_count = 1;
    float fragment_used = 0;
    for (int i = 0; i < item_count; i++) {
        if (break_before[i] && fragment_used > 0) {
            fragment_count++;
            fragment_used = 0;
        }

        float item_height = item_heights[i];
        if (item_can_fragment[i]) {
            // Ordinary block containers may break between line boxes, so their
            // remaining flow consumes subsequent fragmentainers during balance.
            float remaining = item_height;
            while (fragment_used + remaining > target_height) {
                float available = target_height - fragment_used;
                if (available > 0.0f) remaining -= available;
                fragment_count++;
                fragment_used = 0.0f;
            }
            fragment_used += remaining;
            if (break_after[i] && i + 1 < item_count) {
                fragment_count++;
                fragment_used = 0.0f;
            }
            continue;
        }
        if (fragment_used > 0 && fragment_used + item_height > target_height) {
            fragment_count++;
            fragment_used = 0;
        }
        fragment_used += item_height;

        if (break_after[i] && i + 1 < item_count) {
            fragment_count++;
            fragment_used = 0;
        }
    }
    return fragment_count;
}

static float multicol_balanced_target_search(
    ViewBlock* block,
    float* item_heights,
    bool* item_can_fragment,
    bool* break_before,
    bool* break_after,
    int item_count,
    int column_count,
    float fallback_target,
    float group_total_height,
    float* item_content_heights,
    float* item_margin_before,
    float* item_margin_after,
    bool adjacent_to_spanner,
    ViewBlock* adjacent_item
) {
    if (!block || !block->multicol_prop() || item_count <= 0 || column_count <= 1) {
        return fallback_target;
    }
    if (block->multicol_prop()->fill == COLUMN_FILL_AUTO) {
        return fallback_target;
    }
    if (multicol_uses_content_sized_wrapped_rows(block)) {
        // css multicol-2 §4.4: with no column-height, wrapped rows size from
        // the content in each row instead of forcing all breaks into one row.
        return fallback_target;
    }
    if (multicol_has_definite_ancestor_fragmentainer(block)) {
        // css multicol §7.2: a nested flow already constrained by an outer
        // fragmentainer keeps that fragmentainer size and overflows columns.
        return fallback_target;
    }
    if (block->multicol_prop()->wrap == COLUMN_WRAP_WRAP &&
        multicol_content_box_height_limit(block) >= 0.0f) {
        // css multicol-2: column-wrap uses the definite column-height as the
        // fragmentainer size; balancing must not enlarge that size.
        return fallback_target;
    }

    float lower = fallback_target > 0 ? fallback_target : 1;
    float upper = group_total_height;
    float line_balance_floor = 0.0f;
    int flow_item_index = 0;
    for (View* view = block->first_placed_child();
         view && flow_item_index < item_count; view = view->next()) {
        ViewBlock* flow_item = lam::view_as_block(view);
        if (!flow_item || layout_block_is_out_of_flow_positioned(flow_item) ||
            multicol_is_spanner_block(flow_item)) {
            continue;
        }
        if (flow_item->display.inner == CSS_VALUE_GRID) {
            float grid_minimum = multicol_grid_min_fragmentainer_height(
                flow_item, column_count);
            if (grid_minimum > 0.0f) {
                if (item_count > 1 && flow_item->height > grid_minimum) {
                    // sibling flow items must be balanced after the grid
                    // container itself fits; rows can fragment once selected.
                    grid_minimum = ceilf(flow_item->height);
                }
                // css grid fragmentation: balance at complete row boundaries
                // so grid items are not independently projected as block flows.
                lower = max(lower, grid_minimum);
            }
        }
        if (multicol_is_scroll_container(flow_item)) {
            // css fragmentation: a scroll container keeps its clipping and
            // scrolling coordinate space in one fragmentainer.
            lower = max(lower, item_heights[flow_item_index]);
        }
        if (adjacent_to_spanner && !item_can_fragment[flow_item_index] &&
            !is_multicol_container(flow_item) && block->parent_view() &&
            block->parent_view()->tag() == MARKUP_NAME_BUTTON) {
            // html rendering: a button's anonymous flow-root preserves
            // unbreakable line boxes beside a column spanner.
            lower = max(lower, item_heights[flow_item_index]);
        }
        if (block->multicol_prop()->column_count > 0 &&
            !multicol_has_vertical_inline_axis(block) &&
            multicol_content_box_height_limit(block) < 0.0f) {
            int line_count = 0;
            float line_advance = 0.0f;
            bool has_line_metrics = multicol_inline_line_metrics(
                flow_item, &line_count, &line_advance, nullptr);
            if (!has_line_metrics && item_can_fragment[flow_item_index] &&
                flow_item->blk && flow_item->block()->given_height >= 0.0f) {
                has_line_metrics = multicol_find_fragmentable_line_metrics(
                    flow_item, &line_count, &line_advance);
            }
            if (has_line_metrics &&
                line_count > 1 && line_advance > 0.0f) {
                // css fragmentation: balanced columns break only between line
                // boxes, so arithmetic balancing cannot select a smaller target.
                float line_floor = ceilf(fallback_target / line_advance) * line_advance;
                line_balance_floor = max(line_balance_floor, line_floor);
            }
        }
        flow_item_index++;
    }
    lower = max(lower, line_balance_floor);
    if (multicol_has_single_size_contained_monolithic_item(
        block, item_can_fragment, item_count)) {
        // size containment makes the monolithic item's used block size authoritative.
        lower = max(lower, item_heights[0]);
    }
    if (multicol_has_single_decoration_only_monolithic_item(
            block, item_can_fragment, item_count)) {
        // an unbreakable decoration box must fit as one fragmentainer item.
        lower = max(lower, item_heights[0]);
    }
    float decorated_child_min = multicol_decorated_child_min_fragmentainer(
        block, item_can_fragment, item_count, item_margin_before);
    if (decorated_child_min > 0.0f) {
        // a decorated block needs room for its block-start decoration and first child.
        lower = max(lower, decorated_child_min);
    }
    float fragmentable_child_min = multicol_fragmentable_item_min_fragmentainer(
        block, item_can_fragment, item_count, item_margin_before);
    if (fragmentable_child_min > 0.0f) {
        // css fragmentation: the first unbreakable child and the group's
        // leading margin must fit before the wrapper continues.
        lower = max(lower, fragmentable_child_min);
    }
    if (item_count == 1 && (item_can_fragment[0] ||
                            multicol_has_unbreakable_scroll_children(
                                multicol_single_flow_item(block, item_count)))) {
        ViewBlock* item = multicol_single_flow_item(block, item_count);
        float block_children_floor = multicol_block_children_balance_floor(
            item, column_count);
        if (block_children_floor > 0.0f) {
            // css fragmentation: balance a wrapper by its block break
            // opportunities, not by treating its children as one stream.
            lower = max(lower, block_children_floor);
        }
    }
    ViewBlock* monolithic_item = multicol_single_monolithic_item(
        block, item_can_fragment, item_count);
    if (monolithic_item && multicol_has_direct_inline_wrapper(block)) {
        // css fragmentation: a monolithic block in a mixed inline flow must
        // fit before the following inline content is balanced.
        lower = max(lower, item_heights[0]);
    }
    if (adjacent_to_spanner && adjacent_item &&
        block->display.outer != CSS_VALUE_LIST_ITEM &&
        !layout_axis_has_given_size(adjacent_item, false)) {
        int line_count = 0;
        float line_advance = 0.0f;
        if (multicol_inline_line_metrics(
                adjacent_item, &line_count, &line_advance, nullptr) &&
            line_count == 1) {
            // css fragmentation: an auto-sized one-line item must remain whole
            // when balancing the group beside a spanner.
            lower = max(lower, item_heights[0]);
        }
    }
    if (monolithic_item && monolithic_item->blk &&
        monolithic_item->block()->contain_positioning) {
        // css containment: a monolithic contained formatting context must fit
        // as one item in the balanced fragmentainer.
        lower = max(lower, item_heights[0]);
    }
    if (monolithic_item && !is_multicol_container(monolithic_item) &&
        monolithic_item->blk && monolithic_item->block()->break_inside == CSS_VALUE_AVOID &&
        item_margin_before && item_margin_before[0] > 0.0f) {
        // an avoid-break item must fit its leading margin box when balancing.
        lower = max(lower, item_heights[0]);
    }
    if (upper < lower) upper = lower;

    float best = upper;
    for (int step = 0; step < 12; step++) {
        float mid = floorf((lower + upper) * 0.5f);
        if (mid <= 0) mid = (lower + upper) * 0.5f;
        if (mid < 1) mid = 1;

        int fragments = multicol_simulate_column_count(
            item_heights, item_can_fragment, break_before, break_after, item_count,
            column_count, mid,
            item_content_heights, item_margin_before, item_margin_after);
        if (fragments <= column_count) {
            best = mid;
            upper = mid;
        } else {
            lower = mid + 1;
        }
        if (upper <= lower) break;
    }

    return best;
}

float multicol_intrinsic_vertical_block_extent(LayoutContext* lycon,
                                               ViewBlock* block,
                                               DomElement* element) {
    if (!lycon || !block || !element || !block->multicol_prop()) return 0.0f;
    int column_count = block->multicol_prop()->column_count;
    if (column_count <= 1) return 0.0f;

    float* item_extents = (float*)scratch_calloc(
        &lycon->scratch, MAX_MULTICOL_BLOCKS * sizeof(float));
    bool* item_can_fragment = (bool*)scratch_calloc(
        &lycon->scratch, MAX_MULTICOL_BLOCKS * sizeof(bool));
    bool* break_before = (bool*)scratch_calloc(
        &lycon->scratch, MAX_MULTICOL_BLOCKS * sizeof(bool));
    bool* break_after = (bool*)scratch_calloc(
        &lycon->scratch, MAX_MULTICOL_BLOCKS * sizeof(bool));
    if (!item_extents || !item_can_fragment || !break_before || !break_after) {
        if (break_after) scratch_free(&lycon->scratch, break_after);
        if (break_before) scratch_free(&lycon->scratch, break_before);
        if (item_can_fragment) scratch_free(&lycon->scratch, item_can_fragment);
        if (item_extents) scratch_free(&lycon->scratch, item_extents);
        return 0.0f;
    }

    int item_count = 0;
    float total_extent = 0.0f;
    for (DomNode* child = element->first_child;
         child && item_count < MAX_MULTICOL_BLOCKS;
        child = child->next_sibling) {
        if (!child->is_element()) continue;
        ViewBlock* child_block = lam::view_as_block(static_cast<View*>(child));
        if (child_block && (layout_block_is_out_of_flow_positioned(child_block) ||
                            multicol_is_spanner_block(child_block))) continue;

        float extent = calculate_max_content_height(
            lycon, child, lycon->block.content_width);
        if (child_block) {
            extent = multicol_outer_flow_extent(
                block, child_block, extent);
        }
        extent = max(extent, 0.0f);
        item_extents[item_count] = extent;
        item_can_fragment[item_count] = child_block &&
            multicol_has_fragmentable_line_boxes(child_block);
        break_before[item_count] = child_block && child_block->blk &&
            multicol_forces_column_break(child_block->block()->break_before);
        break_after[item_count] = child_block && child_block->blk &&
            multicol_forces_column_break(child_block->block()->break_after);
        total_extent += extent;
        item_count++;
    }

    float result = 0.0f;
    if (item_count > 0 && total_extent > 0.0f) {
        float fallback = ceilf(total_extent / (float)column_count);
        result = multicol_group_target_height(block, fallback, total_extent);
        result = multicol_balanced_target_search(
            block, item_extents, item_can_fragment, break_before, break_after,
            item_count, column_count, result, total_extent);
    }

    scratch_free(&lycon->scratch, break_after);
    scratch_free(&lycon->scratch, break_before);
    scratch_free(&lycon->scratch, item_can_fragment);
    scratch_free(&lycon->scratch, item_extents);
    return result;
}

static void multicol_group_init(
    ColumnGroup* group,
    ViewBlock* container,
    float target_height,
    int column_count,
    float column_width,
    float gap,
    float inline_origin
) {
    float row_gap = multicol_row_gap(container);
    if (row_gap < 0) row_gap = 0;

    group->container = container;
    group->fragment_count = 1;
    group->logical_fragment_count = 1;
    group->column_count = column_count;
    group->column_width = column_width;
    group->column_gap = gap;
    group->inline_origin = inline_origin;
    group->row_gap = row_gap;
    group->target_height = target_height;
    group->group_used_height = 0;
    group->wraps_rows = multicol_group_wraps_rows(container);
    group->vertical_writing = layout_block_inline_axis_is_vertical(container);

    group->fragments[0].fragment_index = 0;
    group->fragments[0].column_index = 0;
    group->fragments[0].row_index = 0;
    // fragment coordinates are local to the container border box, so every
    // column must retain the content-box inset established by border/padding.
    group->fragments[0].x = inline_origin;
    group->fragments[0].y = 0;
    group->fragments[0].width = column_width;
    group->fragments[0].target_height = target_height;
    group->fragments[0].used_height = 0;
}

static void multicol_group_record_fragment_count(
    ColumnGroup* group, int fragment_count) {
    if (!group || fragment_count <= 0) return;
    if (fragment_count > group->logical_fragment_count) {
        group->logical_fragment_count = fragment_count;
    }
    int materialized_count = min(fragment_count, MAX_MULTICOL_BLOCKS);
    if (materialized_count > group->fragment_count) {
        group->fragment_count = materialized_count;
    }
}

static void multicol_cursor_init(FragmentedFlowCursor* cursor, ColumnGroup* group) {
    cursor->group = group;
    cursor->current_fragment = 0;
    cursor->block_offset = 0;
    cursor->pending_margin_after = 0.0f;
    cursor->has_item_in_fragment = false;
}

static bool multicol_group_wraps_rows(ViewBlock* container) {
    if (!container || !container->multicol_prop()) return false;
    float fragment_height = multicol_content_box_height_limit(container);
    if (!container->multicol_prop()->column_height_is_specified &&
        fragment_height < 0 &&
        container->multicol_prop()->wrap != COLUMN_WRAP_WRAP) {
        // css multicol-2: explicit column-wrap:wrap creates rows even when
        // the column block-size is auto and forced breaks determine progress.
        return false;
    }
    if (container->multicol_prop()->wrap == COLUMN_WRAP_WRAP ||
        (container->multicol_prop()->wrap == COLUMN_WRAP_AUTO &&
         container->multicol_prop()->column_height_is_specified)) {
        return true;
    }
    if (container->multicol_prop()->wrap == COLUMN_WRAP_AUTO &&
        (!container->blk || container->block()->given_height < 0) &&
        container->multicol_prop()->fill == COLUMN_FILL_AUTO) {
        return true;
    }
    return false;
}

static bool multicol_uses_content_sized_wrapped_rows(ViewBlock* container) {
    return container && container->multicol_prop() &&
        container->multicol_prop()->wrap == COLUMN_WRAP_WRAP &&
        !container->multicol_prop()->column_height_is_specified &&
        multicol_content_box_height_limit(container) < 0.0f;
}

static bool multicol_allows_overflow_columns(ViewBlock* container) {
    if (!container || !container->multicol_prop() ||
        multicol_group_wraps_rows(container)) {
        return false;
    }
    float fragmentainer_height = multicol_content_box_height_limit(container);
    bool nested_in_finite_fragmentainer = fragmentainer_height < 0.0f &&
        container->multicol_prop()->fill == COLUMN_FILL_BALANCE &&
        multicol_has_definite_ancestor_fragmentainer(container);
    if (fragmentainer_height <= 0.0f && !nested_in_finite_fragmentainer) return false;
    MultiColumnProp* prop = container->multicol_prop();
    bool auto_height = !container->blk || container->block()->given_height < 0.0f;
    if (auto_height && !prop->column_height_is_specified &&
        prop->fill == COLUMN_FILL_BALANCE && fragmentainer_height >= 0.0f) {
        // css sizing: max-height caps an auto-sized balanced container only
        // after its content establishes the used block size.
        return false;
    }
    bool only_non_multicol = multicol_has_only_non_multicol_flow_children(container);
    // css multicol-2 §4.4: a one-column flow and an explicit nowrap flow
    // continue in the inline direction after the finite fragmentainer list;
    // spanner groups and ordinary direct flows retain that continuation;
    // nested contexts also use overflow columns while their parent owns a
    // finite fragmentainer.
    return prop->column_count <= 1 || prop->wrap == COLUMN_WRAP_NOWRAP ||
        multicol_has_spanner_child(container) ||
        only_non_multicol;
}

static float multicol_specified_row_height(ViewBlock* container) {
    if (!container || !container->multicol_prop() ||
        !container->multicol_prop()->column_height_is_specified) {
        return -1.0f;
    }
    float limit = multicol_content_box_height_limit(container);
    return limit >= 0.0f ? limit : container->multicol_prop()->column_height;
}

static bool multicol_uses_fixed_balanced_rows(ViewBlock* container) {
    return multicol_uses_fixed_wrapped_rows(container) &&
        container->multicol_prop()->fill == COLUMN_FILL_BALANCE;
}

static bool multicol_uses_fixed_wrapped_rows(ViewBlock* container) {
    return container && container->multicol_prop() &&
        multicol_group_wraps_rows(container) &&
        multicol_specified_row_height(container) >= 0.0f &&
        !multicol_has_definite_ancestor_fragmentainer(container);
}

static bool multicol_group_should_break(
    ViewBlock* container,
    FragmentedFlowCursor* cursor,
    float item_height
) {
    if (!container || !container->multicol_prop() || !cursor || !cursor->group) return false;
    ColumnGroup* group = cursor->group;
    bool allow_overflow_columns = multicol_allows_overflow_columns(container);
    if (group->column_count <= 1 && !allow_overflow_columns) return false;
    if (cursor->block_offset <= 0) return false;
    ColumnFragment* fragment = multicol_cursor_current_fragment(cursor);
    if (!fragment) return false;
    if (fragment->column_index >= group->column_count - 1 &&
        !group->wraps_rows && !allow_overflow_columns) return false;

    if (container->multicol_prop()->fill == COLUMN_FILL_AUTO) {
        if (group->target_height < 0) return false;
        return cursor->block_offset + item_height > group->target_height + 0.5f;
    }

    // Fragmentainer balance uses rounded line metrics; the epsilon prevents a
    // sub-pixel sum from moving a line-sized block into the next column.
    return cursor->block_offset + item_height > group->target_height + 0.5f;
}

static void multicol_cursor_advance_fragment(FragmentedFlowCursor* cursor) {
    if (!cursor || !cursor->group) return;

    ColumnGroup* group = cursor->group;
    ColumnFragment* current = multicol_cursor_current_fragment(cursor);
    if (!current) return;
    current->used_height = cursor->block_offset;
    float fragment_extent = group->vertical_writing
        ? cursor->block_offset
        : current->y + cursor->block_offset;
    if (fragment_extent > group->group_used_height) {
        group->group_used_height = fragment_extent;
    }

    int next_column = current->column_index;
    int next_row = current->row_index;
    float next_row_y = current->y;
    if (current->column_index >= group->column_count - 1 && group->wraps_rows) {
        next_column = 0;
        next_row++;
        if (multicol_uses_content_sized_wrapped_rows(group->container)) {
            float row_height = 0.0f;
            int row_start = current->row_index * group->column_count;
            for (int index = row_start; index <= cursor->current_fragment; index++) {
                if (index >= group->fragment_count) break;
                row_height = max(row_height, group->fragments[index].used_height);
            }
            next_row_y = current->y + row_height + group->row_gap;
        } else {
            next_row_y = next_row * (group->target_height + group->row_gap);
        }
    } else {
        next_column++;
    }
    cursor->block_offset = 0;
    cursor->pending_margin_after = 0.0f;
    cursor->has_item_in_fragment = false;

    int next_fragment_index = cursor->current_fragment + 1;
    if (next_fragment_index >= 0) {
        multicol_group_record_fragment_count(group, next_fragment_index + 1);
        // the union pass may have counted future fragments before the cursor
        // reaches them; materialize bounded slots and virtualize the rest.
        if (next_fragment_index < MAX_MULTICOL_BLOCKS) {
        ColumnFragment* fragment = &group->fragments[next_fragment_index];
        fragment->fragment_index = next_fragment_index;
        fragment->column_index = next_column;
        fragment->row_index = next_row;
        if (group->vertical_writing) {
            // vertical columns advance in the physical inline axis; the
            // fragment's block-flow offset remains local to that column.
            fragment->x = 0.0f;
            fragment->y = next_column * (group->column_width + group->column_gap) +
                next_row * (group->column_width + group->column_gap);
            fragment->width = group->target_height;
        } else {
            fragment->x = group->inline_origin +
                          next_column * (group->column_width + group->column_gap);
            fragment->y = next_row_y;
            fragment->width = group->column_width;
        }
        fragment->target_height = group->target_height;
        fragment->used_height = 0;
        }
        cursor->current_fragment = next_fragment_index;
    }
}

static void multicol_group_ensure_fragment_count(
    FragmentedFlowCursor* cursor, int required_fragment_count) {
    if (!cursor || !cursor->group || required_fragment_count <= 0) return;
    multicol_group_record_fragment_count(cursor->group, required_fragment_count);
    int materialized_fragment_count = min(
        required_fragment_count, MAX_MULTICOL_BLOCKS);
    int saved_fragment = cursor->current_fragment;
    float saved_block_offset = cursor->block_offset;
    float saved_pending_margin_after = cursor->pending_margin_after;
    bool saved_has_item_in_fragment = cursor->has_item_in_fragment;
    while (cursor->group->fragment_count < materialized_fragment_count) {
        multicol_cursor_advance_fragment(cursor);
    }
    cursor->current_fragment = saved_fragment;
    cursor->block_offset = saved_block_offset;
    cursor->pending_margin_after = saved_pending_margin_after;
    cursor->has_item_in_fragment = saved_has_item_in_fragment;
}

static ColumnFragment* multicol_cursor_current_fragment(FragmentedFlowCursor* cursor) {
    if (!cursor || !cursor->group || cursor->current_fragment < 0 ||
        cursor->current_fragment >= cursor->group->logical_fragment_count) {
        return NULL;
    }
    if (cursor->current_fragment < cursor->group->fragment_count) {
        return &cursor->group->fragments[cursor->current_fragment];
    }

    ColumnGroup* group = cursor->group;
    ColumnFragment* fragment = &cursor->virtual_fragment;
    bool overflow_columns = multicol_allows_overflow_columns(group->container) &&
        !group->wraps_rows;
    int column_index = overflow_columns
        ? cursor->current_fragment
        : cursor->current_fragment % group->column_count;
    int row_index = overflow_columns
        ? 0
        : cursor->current_fragment / group->column_count;
    fragment->fragment_index = cursor->current_fragment;
    fragment->column_index = column_index;
    fragment->row_index = row_index;
    fragment->x = group->vertical_writing ? 0.0f :
        group->inline_origin + column_index *
            (group->column_width + group->column_gap);
    fragment->y = group->vertical_writing ?
        column_index * (group->column_width + group->column_gap) :
        row_index * (group->target_height + group->row_gap);
    fragment->width = group->column_width;
    fragment->target_height = group->target_height;
    fragment->used_height = 0.0f;
    return fragment;
}

static void multicol_cursor_place_block(
    FragmentedFlowCursor* cursor,
    ViewBlock* child,
    float group_y
) {
    ColumnFragment* fragment = multicol_cursor_current_fragment(cursor);
    if (!fragment || !child) return;
    if (cursor->group->vertical_writing) {
        BoxMetrics box = layout_box_metrics(cursor->group->container);
        float block_origin = box.border.left + box.padding.left;
        float inline_origin = box.border.top + box.padding.top;
        // vertical writing exchanges multicol's surrogate column and flow
        // axes: columns advance along the physical inline axis, while blocks
        // consume the physical block axis within each fragmentainer.
        child->x = block_origin + cursor->block_offset;
        float column_inline_pitch = cursor->group->column_width + cursor->group->column_gap;
        int inline_column = fragment->column_index;
        if (cursor->group->container->block()->direction == CSS_VALUE_RTL) {
            inline_column = cursor->group->column_count - 1 - inline_column;
        }
        child->y = inline_origin + inline_column * column_inline_pitch +
            fragment->row_index * (column_inline_pitch + cursor->group->row_gap);
        return;
    }
    child->x = fragment->x;
    child->y = group_y + fragment->y + cursor->block_offset;
}

static void multicol_fit_vertical_auto_inline_size(
    ColumnGroup* group, ViewBlock* child) {
    if (!group || !child || !group->vertical_writing ||
        !layout_block_inline_axis_is_vertical(child) ||
        (child->multicol_prop() && is_multicol_container(child)) ||
        child->first_placed_child()) {
        return;
    }
    bool has_authored_inline_size = layout_axis_has_given_size(child, false) &&
        !(child->blk && child->blk->vertical_auto_inline_size_constrained);
    if (has_authored_inline_size) return;

    // css writing modes: an auto inline-size fills the current column's
    // physical inline extent, not the multicol container's full extent.
    float inline_margin = layout_axis_margin_start(child->bound, LAYOUT_AXIS_Y) +
        layout_axis_margin_end(child->bound, LAYOUT_AXIS_Y);
    float content_extent = max(group->column_width - inline_margin, 0.0f);
    BoxMetrics box = layout_box_metrics(child);
    float border_box_extent = layout_uses_border_box(child)
        ? content_extent : content_extent + box.pad_border_v;
    child->height = border_box_extent;
    child->content_height = max(border_box_extent - box.pad_border_v, 0.0f);
}

static void multicol_cursor_advance_block(FragmentedFlowCursor* cursor, float block_height) {
    if (!cursor || !cursor->group) return;
    cursor->block_offset += block_height;
}

static void multicol_cursor_advance_fragmented_block(
    FragmentedFlowCursor* cursor,
    float flow_height
) {
    if (!cursor || !cursor->group || flow_height <= 0.0f) return;
    ColumnGroup* group = cursor->group;
    float target_height = group->target_height;
    if (target_height <= 0.0f) {
        multicol_cursor_advance_block(cursor, flow_height);
        return;
    }
    bool allow_overflow_columns = multicol_allows_overflow_columns(group->container);
    float remaining = flow_height;
    while (remaining > 0.0f) {
        ColumnFragment* fragment = multicol_cursor_current_fragment(cursor);
        if (!fragment) return;
        float available = target_height - cursor->block_offset;
        if (available <= 0.0f) {
            if (fragment->column_index >= group->column_count - 1 &&
                !group->wraps_rows && !allow_overflow_columns) {
                cursor->block_offset += remaining;
                return;
            }
            multicol_cursor_advance_fragment(cursor);
            continue;
        }
        if (remaining <= available) {
            cursor->block_offset += remaining;
            return;
        }
        cursor->block_offset = target_height;
        remaining -= available;
        if (fragment->column_index >= group->column_count - 1 &&
            !group->wraps_rows && !allow_overflow_columns) {
            cursor->block_offset += remaining;
            return;
        }
        multicol_cursor_advance_fragment(cursor);
    }
}

static void multicol_group_finish(ColumnGroup* group, FragmentedFlowCursor* cursor) {
    if (!group || !cursor) return;
    ColumnFragment* fragment = multicol_cursor_current_fragment(cursor);
    if (fragment) {
        fragment->used_height = cursor->block_offset;
        float fragment_extent = group->vertical_writing
            ? cursor->block_offset
            : fragment->y + (group->target_height <= 0.0f
                ? group->target_height : cursor->block_offset);
        if (fragment_extent > group->group_used_height) {
            group->group_used_height = fragment_extent;
        }
    }
}

static void multicol_store_positioned_baselines(LayoutContext* lycon,
                                                ViewBlock* container) {
    if (!lycon || !container || !container->blk) return;

    float first_baseline = 0.0f;
    float last_baseline = 0.0f;
    bool has_baseline = false;
    bool vertical_writing = layout_block_inline_axis_is_vertical(container);
    for (View* view = container->first_placed_child(); view; view = view->next()) {
        ViewBlock* child = lam::view_as_block(view);
        if (!child || layout_block_is_out_of_flow_positioned(child) ||
            child->display.inner == CSS_VALUE_TABLE) {
            continue;
        }

        float child_first = radiant::compute_view_first_text_baseline(
            lycon, static_cast<View*>(child), 0.0f, true, true, nullptr);
        if (child_first <= 0.0f) {
            child_first = child->blk ? child->block()->first_line_baseline : 0.0f;
        }
        float child_last = radiant::compute_view_last_text_baseline(
            lycon, static_cast<View*>(child), 0.0f, true, true);
        if (child_last <= 0.0f) {
            child_last = child->blk ? child->block()->last_line_baseline : 0.0f;
        }
        if (child_first < 0.0f || child_last < 0.0f) continue;
        // Vertical columns are laid out in the surrogate y flow, but their
        // baseline coordinate is on the parent's block axis. Use the logical
        // column x position when exporting that baseline set.
        float positioned_first = vertical_writing
            ? layout_block_start_content_offset(container) + child->x + child_first
            : child->y + child_first;
        float positioned_last = vertical_writing
            ? layout_block_start_content_offset(container) + child->x + child_last
            : child->y + child_last;
        if (!has_baseline || positioned_first < first_baseline) {
            first_baseline = positioned_first;
        }
        if (!has_baseline || positioned_last > last_baseline) {
            last_baseline = positioned_last;
        }
        has_baseline = true;
    }

    if (!has_baseline) return;
    // The initial flow cache can retain a first-line value after a child is
    // fragmented. Export the positioned text baselines so auto and last source
    // both use the block-end-most line after column balancing.
    lycon->block.first_line_ascender = first_baseline;
    lycon->block.last_line_ascender = last_baseline;
    container->blk->first_line_baseline = first_baseline;
    container->blk->last_line_baseline = last_baseline;
}
// Returns false once a direct inline run needs real line fragmentation. The
// simple spanner path below may preserve phase-one placement only when every
// anonymous inline block is a single formatted line.
static bool multicol_text_line_is_single_line(DomText* text, float* line_y,
                                               bool* has_visible_text);

static bool multicol_direct_text_run_is_single_line(View* first, View* after,
                                                    bool* has_visible_text) {
    if (has_visible_text) *has_visible_text = false;
    float line_y = 0.0f;
    for (View* child = first; child && child != after; child = child->next_sibling) {
        if (child->node_type != DOM_NODE_TEXT) return false;
        DomText* text = lam::dom_require<DOM_NODE_TEXT>(child);
        if (!multicol_text_line_is_single_line(text, &line_y, has_visible_text)) return false;
    }
    return true;
}

static bool multicol_text_line_is_single_line(DomText* text, float* line_y,
                                               bool* has_visible_text) {
    if (!text || !line_y || !has_visible_text) return true;
    for (TextRect* rect = text->rect; rect; rect = rect->next) {
        if (rect->width <= 0.0f || rect->height <= 0.0f) continue;
        if (!*has_visible_text) {
            *has_visible_text = true;
            *line_y = rect->y;
        } else if (fabsf(rect->y - *line_y) > 1.0f) {
            return false;
        }
    }
    return true;
}

static bool multicol_view_has_single_text_line(View* view, float* line_y,
                                                bool* has_visible_text) {
    if (!view) return true;
    if (view->node_type == DOM_NODE_TEXT) {
        DomText* text = lam::dom_require<DOM_NODE_TEXT>(view);
        if (!multicol_text_line_is_single_line(text, line_y, has_visible_text)) return false;
    }
    if (!view->is_element()) return true;
    for (DomNode* child_node = view->as_element()->first_child; child_node;
         child_node = child_node->next_sibling) {
        if (!multicol_view_has_single_text_line(
                static_cast<View*>(child_node), line_y, has_visible_text)) {
            return false;
        }
    }
    return true;
}

static float multicol_content_start_y(ViewBlock* block);

static void multicol_publish_unfragmented_flow(
    LayoutContext* lycon, ViewBlock* block, float total_content_height) {
    if (!lycon || !block || !block->multicol_prop()) return;

    float content_start_y = multicol_content_start_y(block);
    float flow_height = total_content_height - content_start_y;
    if (flow_height < 0.0f) flow_height = 0.0f;
    float total_height = flow_height + layout_box_metrics(block).pad_border_v;
    block->height = total_height;
    block->content_height = flow_height +
        layout_axis_padding_end(block->bound, LAYOUT_AXIS_Y);
    lycon->block.advance_y = content_start_y + flow_height;
    block->multicol_prop()->computed_used_column_count = 1;
    multicol_apply_positioned_fragment_anchors(lycon, block);
    multicol_finalize_fragmented_inline_continuations(static_cast<View*>(block));
    multicol_store_positioned_baselines(lycon, block);
}

static bool multicol_preserve_simple_direct_spanner_flow(
    LayoutContext* lycon,
    ViewBlock* block,
    float available_width,
    float column_group_origin_x,
    float total_content_height
) {
    if (!lycon || !block || !block->multicol_prop()) return false;

    bool saw_spanner = false;
    bool saw_inline_run = false;
    View* inline_run_first = nullptr;
    View* leading_inline_first = nullptr;
    ViewBlock* spanner = nullptr;
    int spanner_count = 0;
    for (View* child = block->first_placed_child(); child; child = child->next()) {
        ViewBlock* child_block = lam::view_as_block(child);
        if (child_block && !layout_block_is_out_of_flow_positioned(child_block)) {
            if (inline_run_first) {
                bool has_visible_text = false;
                if (!multicol_direct_text_run_is_single_line(
                        inline_run_first, child, &has_visible_text)) {
                    return false;
                }
                saw_inline_run |= has_visible_text;
                if (!leading_inline_first && !saw_spanner) {
                    leading_inline_first = inline_run_first;
                }
                inline_run_first = nullptr;
            }
            if (!multicol_is_spanner_block(child_block)) {
                if (saw_spanner && child_block->height <= 0.0f &&
                    multicol_in_flow_descendant_extent(child_block) <= 0.0f) {
                    // css multicol: an empty in-flow block after a spanner
                    // keeps its phase-one position; its out-of-flow subtree
                    // does not add a fragmentainer flow extent.
                    continue;
                }
                return false;
            }

            float line_y = 0.0f;
            bool has_visible_text = false;
            if (!multicol_view_has_single_text_line(
                static_cast<View*>(child_block), &line_y, &has_visible_text)) {
                return false;
            }
            saw_spanner = true;
            spanner = child_block;
            spanner_count++;
            continue;
        }

        if (layout_block_is_out_of_flow_positioned(child_block)) continue;
        if (child->node_type != DOM_NODE_TEXT) {
            if (multicol_is_direct_br_node(child)) {
                // css inline: a direct br before a spanner establishes the
                // preceding line box even when it has no text rect.
                if (!saw_spanner) saw_inline_run = true;
                continue;
            }
            return false;
        }
        if (!inline_run_first) inline_run_first = child;
    }

    if (inline_run_first) {
        bool has_visible_text = false;
        bool trailing_whitespace_only =
            inline_run_first->node_type == DOM_NODE_TEXT &&
            !layout_dom_text_has_non_whitespace(
                lam::dom_require<DOM_NODE_TEXT>(inline_run_first));
        if (!trailing_whitespace_only &&
            !multicol_direct_text_run_is_single_line(
                inline_run_first, nullptr, &has_visible_text)) {
            return false;
        }
        saw_inline_run |= has_visible_text;
        if (!leading_inline_first && !saw_spanner) {
            leading_inline_first = inline_run_first;
        }
    }
    if (!saw_spanner || !saw_inline_run) return false;

    // css multicol §6: a single trailing break needs an explicit line box;
    // preserve the phase-one fast path for all other simple spanner flows.
    if (spanner_count == 1 && spanner && saw_inline_run) {
        bool after_spanner = false;
        int trailing_br_count = 0;
        int break_index = 0;
        View* first_trailing_br = nullptr;
        bool simple_trailing_breaks = true;
        for (DomNode* child_node = block->first_child; child_node;
             child_node = child_node->next_sibling) {
            View* child = static_cast<View*>(child_node);
            if (child == static_cast<View*>(spanner)) {
                after_spanner = true;
                continue;
            }
            if (!after_spanner) continue;
            if (child_node->node_type == DOM_NODE_TEXT) {
                if (layout_dom_text_has_non_whitespace(
                        lam::dom_require<DOM_NODE_TEXT>(child))) {
                    simple_trailing_breaks = false;
                    break;
                }
                continue;
            }
            if (!multicol_is_direct_br_node(child)) {
                simple_trailing_breaks = false;
                break;
            }
            if (!first_trailing_br) first_trailing_br = child;
            trailing_br_count++;
        }

        if (simple_trailing_breaks && trailing_br_count > 0) {
            float line_advance = multicol_first_text_height(
                leading_inline_first ? leading_inline_first :
                    static_cast<View*>(spanner));
            if (line_advance <= 0.0f && first_trailing_br) {
                line_advance = first_trailing_br->height;
            }
            if (line_advance > 0.0f) {
                spanner->x = column_group_origin_x;
                spanner->width = available_width;
                float old_y = spanner->y;
                spanner->y = line_advance;
                layout_shift_view_tree(
                    static_cast<View*>(spanner), 0.0f, spanner->y - old_y);

                after_spanner = false;
                for (DomNode* child_node = block->first_child; child_node;
                     child_node = child_node->next_sibling) {
                    View* child = static_cast<View*>(child_node);
                    if (child == static_cast<View*>(spanner)) {
                        after_spanner = true;
                        continue;
                    }
                    if (!after_spanner || !multicol_is_direct_br_node(child)) continue;
                    child->x = spanner->x;
                    child->y = line_advance + spanner->height +
                        break_index * line_advance;
                    break_index++;
                }

                multicol_publish_unfragmented_flow(
                    lycon, block, line_advance + spanner->height +
                        trailing_br_count * line_advance);
                return true;
            }
        }
    }

    for (View* child = block->first_placed_child(); child; child = child->next()) {
        ViewBlock* child_block = lam::view_as_block(child);
        if (!child_block || !multicol_is_spanner_block(child_block)) continue;
        // css multicol §6: phase one already lays out anonymous inline blocks;
        // only the spanner's containing width changes when it escapes columns.
        child_block->x = column_group_origin_x;
        child_block->width = available_width;
    }

    multicol_publish_unfragmented_flow(lycon, block, total_content_height);
    return true;
}

static float multicol_content_start_y(ViewBlock* block) {
    if (!block || !block->bound) return 0.0f;
    return layout_axis_decoration_start(block->boundary(), LAYOUT_AXIS_Y);
}

static void multicol_layout_children(LayoutContext* lycon, ViewBlock* block) {
    DomNode* child = block ? block->first_child : nullptr;
    if (!child) return;
    prescan_and_layout_floats(lycon, child, block);
    do {
        layout_flow_node(lycon, child);
        child = child->next_sibling;
    } while (child);
    if (!lycon->line.is_line_start) line_break(lycon);
}

static float multicol_adjust_contained_block_start(ViewBlock* container,
                                                   ViewBlock* child,
                                                   bool finalize_geometry) {
    if (!container || !child || !child->blk ||
        !child->block()->contain_positioning ||
        multicol_has_vertical_inline_axis(container) ||
        (child->bound && layout_axis_decoration_start(
            child->boundary(), LAYOUT_AXIS_Y) > 0.0f)) {
        return 0.0f;
    }

    ViewBlock* first_block = nullptr;
    for (View* descendant = child->first_placed_child(); descendant;
         descendant = descendant->next()) {
        if (!descendant->is_block()) continue;
        ViewBlock* descendant_block = lam::view_as_block(descendant);
        if (descendant_block && !layout_block_is_out_of_flow_positioned(descendant_block)) {
            first_block = descendant_block;
            break;
        }
    }
    if (!first_block) return 0.0f;

    float margin_before = 0.0f;
    float margin_after = 0.0f;
    multicol_flow_margins(child, first_block, &margin_before, &margin_after);
    if (margin_before <= 0.0f) return 0.0f;
    if (!finalize_geometry && child->bound) {
        BoundaryProp* boundary = child->boundary_mut();
        if (fabsf(boundary->margin.top - margin_before) <= 0.01f) {
            boundary->margin.top = 0.0f;
        }
        if (margin_after > 0.0f &&
            fabsf(boundary->margin.bottom - margin_after) <= 0.01f) {
            boundary->margin.bottom = 0.0f;
        }
    }

    // css containment: the first child margin belongs inside the contained box.
    if (finalize_geometry) {
        first_block->y += margin_before;
    }
    if (child->block()->given_height < 0.0f) {
        if (!finalize_geometry) {
            child->height += margin_before;
            child->content_height += margin_before;
        }
        return 0.0f;
    }
    return finalize_geometry ? 0.0f : -margin_before;
}

/**
 * Layout multi-column content
 *
 * Multi-column layout works by:
 * 1. Setting up a narrow column width in the layout context
 * 2. Running normal flow layout within that width
 * 3. Measuring total content height
 * 4. Repositioning blocks to distribute across columns
 *
 * This is a simplified implementation that:
 * - Uses block-level distribution (breaks between block elements)
 * - Supports column-fill: balance (default) which tries to equalize column heights
 * - Doesn't yet support fragmentation within block elements
 */
void layout_multicol_content(LayoutContext* lycon, ViewBlock* block) {
    if (!block->multicol_prop()) {
        log_error("[MULTICOL] layout_multicol_content called without multicol prop");
        return;
    }
    // Calculate available width (content box)
    float available_width = lycon->block.content_width;
    float available_inline_extent = available_width;
    if (multicol_has_vertical_inline_axis(block)) {
        float inline_limit = multicol_content_box_inline_limit(block);
        if (inline_limit >= 0.0f) {
            // CSS Writing Modes maps a definite physical height to the
            // vertical inline axis; columns divide that extent in y.
            available_inline_extent = inline_limit;
        }
    }
    // Calculate column dimensions
    int column_count;
    float column_width, gap;
    calculate_multicol_dimensions(block->multicol_prop(), available_inline_extent,
                                   multicol_normal_gap_size(block),
                                   &column_count, &column_width, &gap);
    // Store computed values for rendering
    block->multicol_prop()->computed_column_count = column_count;
    block->multicol_prop()->computed_column_width = column_width;
    block->multicol_prop()->computed_column_gap = gap;
    block->multicol_prop()->computed_used_column_count = 1;
    block->multicol_prop()->computed_block_axis_extent = 0.0f;
    // A finite non-wrapping one-column flow still creates overflow columns.
    if (column_count <= 1 && !multicol_group_wraps_rows(block) &&
        !multicol_allows_overflow_columns(block)) {
        // css multicol-2 §4.4: a single wrapped column can still form later
        // rows when column-height fragments its content.
        block->multicol_prop()->computed_column_count = 1;
        // Run normal flow layout
        multicol_layout_children(lycon, block);

        float max_flow_extent = lycon->block.advance_y;
        float trailing_margin = multicol_uncontained_trailing_margin(
            block, max_flow_extent);
        if (trailing_margin > 0.0f) {
            // css multicol: the column box is a block formatting context, so
            // the last child’s block-end margin cannot collapse out of it.
            max_flow_extent += trailing_margin;
            lycon->block.advance_y = max_flow_extent;
        }
        View* placed = block->first_placed_child();
        while (placed) {
            if (placed->is_block()) {
                ViewBlock* child_block = lam::view_require_block(placed);
                multicol_clear_layout_fragments(child_block);
                if (!layout_block_is_out_of_flow_positioned(child_block) &&
                    multicol_has_direct_spanner_child(child_block)) {
                    float flow_height = multicol_split_child_around_spanners(
                        lycon, block, child_block, 1, available_width, gap);
                    float child_extent = child_block->y + flow_height;
                    if (child_extent > max_flow_extent) {
                        max_flow_extent = child_extent;
                    }
                }
            }
            placed = placed->next();
        }

        if (max_flow_extent > lycon->block.advance_y) {
            lycon->block.advance_y = max_flow_extent;
            block->height = max_flow_extent;
            block->content_height = max_flow_extent;
        }
        return;
    }
    // Phase 1: Layout all content within single column width
    // Save original line bounds
    float orig_line_left = lycon->line.left;
    float orig_line_right = lycon->line.right;
    float orig_content_width = lycon->block.content_width;
    float orig_content_height = lycon->block.content_height;
    float orig_given_height = lycon->block.given_height;
    AvailableSize orig_available_width = lycon->available_space.width;
    AvailableSize orig_available_height = lycon->available_space.height;
    bool vertical_writing = multicol_has_vertical_inline_axis(block);
    float column_group_origin_x = orig_line_left +
        layout_axis_decoration_start(block->bound, LAYOUT_AXIS_X);
    // Constrain layout to column width
    // In vertical writing, the column inline extent is physical height; the
    // physical width remains the multicol block-size containing the flow.
    lycon->block.content_width = vertical_writing
        ? orig_content_width : column_width;
    lycon->line.left = 0;
    lycon->line.right = lycon->block.content_width;
    // child sizing and its layout-cache key must use the fragmentainer width,
    // otherwise cached full-container widths leak into each column.
    lycon->available_space.width = AvailableSize::make_definite(
        lycon->block.content_width);
    if (vertical_writing) {
        // css writing modes: percentage inline sizes resolve against the
        // fragmentainer's physical inline extent, not the full multicol span.
        lycon->block.content_height = column_width;
        lycon->available_space.height = AvailableSize::make_definite(column_width);
    }
    // Layout children normally within column width
    multicol_layout_children(lycon, block);
    // Get total content height after layout
    float total_content_height = lycon->block.advance_y;
    for (View* placed = block->first_placed_child(); placed; placed = placed->next()) {
        ViewBlock* child_block = lam::view_as_block(placed);
        if (!child_block || layout_block_is_out_of_flow_positioned(child_block)) continue;
        total_content_height += multicol_adjust_contained_block_start(
            block, child_block, false);
    }
    lycon->block.advance_y = total_content_height;
    // Restore original widths (for container sizing)
    lycon->line.left = orig_line_left;
    lycon->line.right = orig_line_right;
    lycon->block.content_width = orig_content_width;
    lycon->block.content_height = orig_content_height;
    lycon->block.given_height = orig_given_height;
    lycon->available_space.width = orig_available_width;
    lycon->available_space.height = orig_available_height;
    if (multicol_has_direct_inline_wrapper(block)) {
        // css multicol: floats inside an inline wrapper are out of normal
        // flow, but their laid-out margin boxes still establish the input
        // extent needed to balance the containing multicol.
        float inline_float_extent = multicol_inline_wrapper_float_extent(block);
        if (inline_float_extent > total_content_height) {
            total_content_height = inline_float_extent;
            lycon->block.advance_y = total_content_height;
        }
    }
    if (total_content_height <= 0 &&
        block->multicol_prop()->fill == COLUMN_FILL_BALANCE) {
        // css fragmentation: an overflowing descendant keeps an auto-sized
        // balancing multicol out of the no-content early exit.
        for (View* placed = block->first_placed_child(); placed;
             placed = placed->next()) {
            ViewBlock* child_block = lam::view_as_block(placed);
            if (!child_block || layout_block_is_out_of_flow_positioned(child_block)) {
                continue;
            }
            if (child_block->height <= 0.0f) {
                float descendant_extent = multicol_in_flow_descendant_extent(
                    child_block);
                if (descendant_extent > 0.5f) {
                    bool vertical_writing = multicol_has_vertical_inline_axis(block);
                    float child_extent = vertical_writing
                        ? multicol_outer_flow_extent(
                            block, child_block, descendant_extent)
                        : child_block->y + multicol_outer_flow_extent(
                            block, child_block, descendant_extent);
                    if (child_extent > total_content_height) {
                        total_content_height = child_extent;
                    }
                }
            }
        }
        lycon->block.advance_y = total_content_height;
        float out_of_flow_target = multicol_out_of_flow_balance_target(block);
        if (out_of_flow_target > total_content_height) {
            // css multicol §3: out-of-flow descendants affect the used block
            // size without becoming in-flow height on their containing box.
            total_content_height = out_of_flow_target;
            lycon->block.advance_y = total_content_height;
        }
    }
    // If content fits in one column, no redistribution needed
    if (total_content_height <= 0) {
        return;
    }
    // Phase 2: Collect block children and identify column groups
    // CSS Multicol §7.1: Spanners divide content into "column groups".
    // Each column group is balanced independently.
    // MAX_MULTICOL_BLOCKS = 1024 → MulticolFlowItem[] ≈ 32 KiB; move to scratch arena (LIFO).
    MulticolFlowScratch flow_scratch = {};
    if (!flow_scratch.init(&lycon->scratch)) {
        log_error("[MULTICOL] Failed to allocate blocks array");
        return;
    }
    MulticolFlowItem* blocks = flow_scratch.items;
    int block_count = 0;

    View* child = block->first_placed_child();
    while (child) {
        if (child->is_element()) {
            DomElement* child_elem = lam::dom_require<DOM_NODE_ELEMENT>(child);
            ViewBlock* child_block = lam::view_as_block(child);
            bool blockified_inline_wrapper = child_block &&
                layout_element_was_inline(child_elem, false) &&
                child_block->display.outer == CSS_VALUE_BLOCK &&
                !layout_position_is_floated(child_block->position);

            if (child_block && !blockified_inline_wrapper &&
                (layout_view_is_block_flow_box(child_block) ||
                 child_block->view_type == RDT_VIEW_TEXT)) {

                multicol_clear_layout_fragments(child_block);

                if (layout_block_is_out_of_flow_positioned(child_block)) {
                    child = child->next_sibling;
                    continue;
                }

                float block_height = multicol_child_flow_extent(block, child_block);

                bool spans_all = child_elem->multicol_prop() &&
                                 child_elem->multicol_prop()->span == COLUMN_SPAN_ALL;
                if (!spans_all && multicol_requires_separate_spanner_group(child_block)) {
                    // css multicol §6: balance the wrapper's in-flow prefix;
                    // the escaping spanner starts after that column group.
                    float prefix_extent = multicol_spanner_prefix_flow_extent(child_block);
                    if (prefix_extent > 0.0f && prefix_extent < block_height) {
                        block_height = prefix_extent;
                    }
                }

                if (block_count < MAX_MULTICOL_BLOCKS) {
                    multicol_init_flow_item(
                        &blocks[block_count], block, child_block, block_height,
                        child_block->x - orig_line_left, spans_all);
                    block_count++;
                }

            }
        }
        child = child->next();
    }
    if (block_count == 0) {
        int flattened_count = 0;
        bool flatten_ok = true;
        for (View* placed = block->first_placed_child(); placed;
             placed = placed->next()) {
            if (placed->view_type != RDT_VIEW_INLINE) continue;
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(placed);
            if (!multicol_collect_inline_flow_blocks(
                    span->first_child, block, blocks, &flattened_count, true)) {
                flatten_ok = false;
                break;
            }
        }
        if (flatten_ok && flattened_count > 0) {
            // css multicol: block-level children inside an inline wrapper
            // still participate in the containing multicol flow.
            block_count = flattened_count;
        }
    }
    if (multicol_preserve_simple_direct_spanner_flow(
            lycon, block, available_width, column_group_origin_x,
            total_content_height)) {
        flow_scratch.release(&lycon->scratch);
        return;
    }


    if (block_count == 0) {
        // No block children — content is inline-only (text lines).
        // Redistribute TextRects across columns based on balanced height.
        log_debug("[MULTICOL] No block children; redistributing inline text across columns");
        // inline content can be nested in spans; collect the laid-out leaves in source order.
        struct LineRect {
            TextRect* rect;
            float line_y;        // original y
            float new_y;         // redistributed y
            float line_height;   // height of this rect
        };
        LineRect lines[512];
        int line_count = 0;

        multicol_for_each_inline_leaf(block->first_placed_child(), [&](View* leaf) {
            if (leaf->view_type != RDT_VIEW_TEXT) return;
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(leaf);
            for (TextRect* rect = text->rect; rect && line_count < 512; rect = rect->next) {
                if (layout_text_rect_content_kind(text, rect) ==
                    LAYOUT_TEXT_RECT_COLLAPSED_WHITESPACE) {
                    continue;
                }
                lines[line_count].rect = rect;
                lines[line_count].line_y = rect->y;
                lines[line_count].new_y = rect->y;
                lines[line_count].line_height = rect->height;
                line_count++;
            }
        });

        if (line_count == 0) {
            ViewBlock* inline_block = nullptr;
            bool has_only_inline_wrappers = true;
            for (View* child = block->first_placed_child(); child; child = child->next()) {
                if (!child->view_type) continue;
                if (!multicol_find_single_inline_block(child, &inline_block)) {
                    has_only_inline_wrappers = false;
                    break;
                }
            }
            if (has_only_inline_wrappers && inline_block && inline_block->height > 0.0f) {
                float balanced_height = ceilf(total_content_height / column_count);
                float target_height = multicol_group_target_height(
                    block, balanced_height, total_content_height);
                int used_columns = 1;
                float fragmented_height = multicol_fragmented_child_union(
                    lycon, block, inline_block, inline_block->height, target_height,
                    column_count, column_width, gap, 0.0f, &used_columns, nullptr);
                ViewElement* parent = inline_block->parent_view();
                View* fragmented_child = static_cast<View*>(inline_block);
                while (parent && static_cast<View*>(parent) != static_cast<View*>(block)) {
                    if (parent->view_type == RDT_VIEW_INLINE) {
                        ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(parent);
                        // replace the phase-one split union with the projected block union.
                        span->set_has_fragment_union(FRAGMENT_UNION_SPLIT_INLINE, false);
                        layout_extend_fragment_union(
                            span, FRAGMENT_UNION_SPLIT_INLINE,
                            fragmented_child->x, fragmented_child->x + fragmented_child->width,
                            fragmented_child->y, fragmented_child->y + fragmented_child->height);
                        compute_span_bounding_box(
                            span, inline_span_has_multiple_line_fragments(span), nullptr);
                        fragmented_child = static_cast<View*>(span);
                    }
                    parent = parent->parent_view();
                }

                float total_height = multicol_used_total_height(block, fragmented_height, true);
                block->height = total_height;
                block->content_height = fragmented_height +
                    layout_axis_padding_end(block->bound, LAYOUT_AXIS_Y);
                block->multicol_prop()->computed_used_column_count = used_columns;
                lycon->block.advance_y = multicol_content_start_y(block) + fragmented_height;
                multicol_finalize_fragmented_inline_continuations(static_cast<View*>(block));
                flow_scratch.release(&lycon->scratch);
                return;
            }
            if (total_content_height > multicol_content_start_y(block)) {
                // css multicol: an unbreakable inline atomic flow still sizes its
                // column container when no text rect supplies a line record.
                multicol_publish_unfragmented_flow(
                    lycon, block, total_content_height);
                flow_scratch.release(&lycon->scratch);
                return;
            }
            flow_scratch.release(&lycon->scratch);
            return;
        }
        ViewBlock* inline_atomic_block = nullptr;
        bool has_only_inline_wrappers = true;
        for (View* child = block->first_placed_child(); child; child = child->next()) {
            if (!child->view_type) continue;
            if (!multicol_find_single_inline_block(child, &inline_atomic_block)) {
                has_only_inline_wrappers = false;
                break;
            }
        }
        // Calculate fragmentainer height for this inline-only column group.
        float balanced_height = ceilf(total_content_height / column_count);
        float target_height = multicol_group_target_height(block, balanced_height, total_content_height);
        float line_advance = -1.0f;
        float max_line_height = 0.0f;
        for (int li = 0; li < line_count; li++) {
            if (lines[li].line_height > max_line_height) {
                max_line_height = lines[li].line_height;
            }
            if (li > 0) {
                float delta = fabsf(lines[li].line_y - lines[li - 1].line_y);
                if (delta > 1.0f && (line_advance < 0.0f || delta < line_advance)) {
                    line_advance = delta;
                }
            }
        }
        if (line_advance <= 0.0f) {
            line_advance = max_line_height > 0.0f ? max_line_height : 16.0f;
        }
        if (max_line_height <= 0.0f) max_line_height = line_advance;
        float first_line_height = lines[0].line_height > 0.0f
            ? lines[0].line_height : line_advance;
        float normal_line_offset = multicol_normal_line_offset(
            line_advance, first_line_height);
        float first_line_box_y = lines[0].line_y -
            multicol_content_start_y(block) - normal_line_offset;
        if (first_line_box_y < 0.0f &&
            first_line_box_y > -normal_line_offset - 0.5f) {
            first_line_box_y = 0.0f;
        }
        bool has_leading_line_offset = first_line_box_y > 0.0f &&
            has_only_inline_wrappers && !inline_atomic_block;
        float definite_fragmentainer_height = multicol_content_box_height_limit(block);
        if (has_leading_line_offset &&
            block->multicol_prop()->fill == COLUMN_FILL_BALANCE &&
            definite_fragmentainer_height < 0.0f) {
            target_height = multicol_adjust_inline_balance_target(
                block, target_height, first_line_box_y, lines[0].line_height,
                line_advance, max_line_height, line_count, column_count);
        }
        // Distribute rects across columns
        int current_col = 0;
        int current_row = 0;
        float col_y = 0;           // y offset within current column
        float col_start_y = 0;     // the original y of the first line assigned to the current column
        int col_start_line = 0;
        bool col_started = false;
        float max_col_height = 0;
        int used_column_count = 0;
        bool wraps_rows = multicol_group_wraps_rows(block);
        bool sideways_lr_rtl = layout_element_css_writing_mode(
            block->as_element()) == CSS_VALUE_SIDEWAYS_LR &&
            block->block()->direction == CSS_VALUE_RTL;
        float sideways_lr_rtl_inline_offset = sideways_lr_rtl
            ? lines[0].rect->x : 0.0f;
        float row_gap = multicol_row_gap(block);
        if (row_gap < 0.0f) row_gap = 0.0f;
        int orphans = block->blk && block->block_mut()->orphans > 0 ? block->block_mut()->orphans : 2;
        int widows = block->blk && block->block_mut()->widows > 0 ? block->block_mut()->widows : 2;
        int current_fragment = 0;
        float fragment_start_offset = first_line_box_y;
        int line_slot = 0;
        if (has_leading_line_offset) {
            int start_fragment = (int)floorf(first_line_box_y / target_height); // INT_CAST_OK: fragment index from positive flow offset
            if (start_fragment < 0) start_fragment = 0;
            current_fragment = start_fragment;
            fragment_start_offset = first_line_box_y - start_fragment * target_height;
            if (fragment_start_offset < 0.0f) fragment_start_offset = 0.0f;
            int first_fragment_fit = multicol_lines_that_fit_fragment(
                target_height - fragment_start_offset, line_advance,
                lines[0].line_height, normal_line_offset);
            if (fragment_start_offset > 0.0f && line_count >= orphans &&
                first_fragment_fit < orphans) {
                current_fragment++;
                fragment_start_offset = 0.0f;
            }
        }

        for (int li = 0; li < line_count; li++) {
            LineRect& lr = lines[li];
            if (has_leading_line_offset) {
                float line_offset = fragment_start_offset > 0.0f
                    ? normal_line_offset : 0.0f;
                float visual_bottom = fragment_start_offset + line_slot * line_advance +
                    line_offset + lr.line_height;
                bool should_break = line_slot > 0 && visual_bottom > target_height + 0.5f;
                if (!should_break && widows > 1 && li + 1 < line_count) {
                    int remaining_after_this = line_count - (li + 1);
                    int remaining_with_this = line_count - li;
                    if (remaining_after_this > 0 &&
                        remaining_after_this < widows &&
                        remaining_with_this >= widows &&
                        line_slot + 1 >= orphans) {
                        float next_bottom = fragment_start_offset +
                            (line_slot + 1) * line_advance + line_offset +
                            lines[li + 1].line_height;
                        should_break = next_bottom > target_height + 0.5f;
                    }
                }
                if (should_break && line_slot < orphans) should_break = false;
                if (should_break &&
                    (current_fragment < column_count - 1 || wraps_rows)) {
                    current_fragment++;
                    current_col = current_fragment % column_count;
                    current_row = current_fragment / column_count;
                    fragment_start_offset = 0.0f;
                    line_slot = 0;
                    line_offset = 0.0f;
                }
                current_col = current_fragment % column_count;
                current_row = current_fragment / column_count;
                float row_y = current_row * (target_height + row_gap);
                float new_y = row_y + fragment_start_offset +
                    line_slot * line_advance + line_offset;
                lr.rect->x += current_col * (column_width + gap) -
                    sideways_lr_rtl_inline_offset +
                    multicol_vertical_rtl_inline_end_offset(
                        block, lr.rect->width, column_width);
                lr.rect->y = new_y;
                lr.new_y = new_y;
                float used_height = row_y + fragment_start_offset +
                    line_slot * line_advance + line_offset + lr.line_height;
                if (used_height > max_col_height) max_col_height = used_height;
                col_y = fragment_start_offset + line_slot * line_advance +
                    line_offset + lr.line_height;
                line_slot++;
                if (current_col + 1 > used_column_count) {
                    used_column_count = current_col + 1;
                }
                continue;
            }
            // Relative y within original single-column layout
            float rel_y = lr.line_y - lines[0].line_y;
            // Check if this line should go to the next column.
            // Break only when including this line would overshoot AND
            // excluding it is closer to balanced than including it.
            // This matches browser behavior of preferring more content
            // in earlier columns when lines are indivisible.
            bool should_break = false;
            if (col_started && (current_col < column_count - 1 || wraps_rows)) {
                float col_h_with = rel_y - col_start_y + lr.line_height;
                if (col_h_with > target_height) {
                    float col_h_without = rel_y - col_start_y;
                    float overshoot = col_h_with - target_height;
                    float undershoot = target_height - col_h_without;
                    should_break = block->multicol_prop()->fill == COLUMN_FILL_AUTO ||
                                   undershoot <= overshoot;
                } else if (li + 1 < line_count && widows > 1) {
                    int remaining_after_this = line_count - (li + 1);
                    int remaining_with_this = line_count - li;
                    int lines_before_break = li - col_start_line;
                    if (remaining_after_this > 0 &&
                        remaining_after_this < widows &&
                        remaining_with_this >= widows &&
                        lines_before_break >= orphans) {
                        float next_rel_y = lines[li + 1].line_y - lines[0].line_y;
                        float next_col_h_with = next_rel_y - col_start_y + lines[li + 1].line_height;
                        should_break = next_col_h_with > target_height;
                    }
                }
                if (should_break && li - col_start_line < orphans) {
                    should_break = false;
                }
                if (should_break) {
                    // closer to balanced without this line, or move the break
                    // earlier so the next fragment satisfies widows.
                    float col_h_without = rel_y - col_start_y;
                    float col_extent = current_row * (target_height + row_gap) + col_h_without;
                    if (col_extent > max_col_height) max_col_height = col_extent;
                    if (wraps_rows && current_col >= column_count - 1) {
                        current_col = 0;
                        current_row++;
                    } else {
                        current_col++;
                    }
                    col_start_y = rel_y;
                    col_start_line = li;
                }
            }
            col_started = true;
            // Reposition: shift x by column offset, reset y within column
            float col_x_offset = current_col * (column_width + gap);
            lr.rect->x += col_x_offset - sideways_lr_rtl_inline_offset +
                multicol_vertical_rtl_inline_end_offset(
                    block, lr.rect->width, column_width);
            lr.new_y = lines[0].line_y + current_row * (target_height + row_gap) +
                (rel_y - col_start_y);
            lr.rect->y = lr.new_y;

            col_y = (rel_y - col_start_y) + lr.line_height;
            if (current_col + 1 > used_column_count) used_column_count = current_col + 1;
        }
        float final_col_extent = current_row * (target_height + row_gap) + col_y;
        if (final_col_extent > max_col_height) max_col_height = final_col_extent;
        // Update block height to the max column height (not total content height)
        float final_height = max_col_height;
        // Direct text rects were already moved with their line records; only
        // break views need the corresponding line-coordinate projection.
        child = block->first_child;
        while (child) {
            if (child->view_type == RDT_VIEW_BR) {
                View* br = (View*)child;
                bool matched_line = false;
                for (int li = 0; li < line_count; li++) {
                    if (fabsf(br->y - lines[li].line_y) <= 1.0f) {
                        br->y = lines[li].new_y;
                        matched_line = true;
                        break;
                    }
                }
                if (!matched_line) {
                    // css inline: a leading forced break owns the line box
                    // before the first visible line, including its inline edge.
                    bool vertical_writing = multicol_has_vertical_inline_axis(block);
                    br->y = 0.0f;
                    if (vertical_writing) {
                        float inline_extent = multicol_content_box_inline_limit(block);
                        if (inline_extent < 0.0f) inline_extent = block->height;
                        bool sideways_lr = layout_element_css_writing_mode(
                            block->as_element()) == CSS_VALUE_SIDEWAYS_LR;
                        br->x = block->block()->direction == CSS_VALUE_RTL &&
                            !sideways_lr ? inline_extent : 0.0f;
                    } else {
                        float inline_extent = available_width;
                        br->x = block->block()->direction == CSS_VALUE_RTL
                            ? inline_extent : 0.0f;
                    }
                }
            }
            child = child->next_sibling;
        }
        // Set block height: use CSS given height if specified, otherwise balanced column height
        float total_height = multicol_used_total_height(block, final_height, true);
        block->height = total_height;
        block->content_height = final_height + layout_axis_padding_end(block->bound, LAYOUT_AXIS_Y);
        block->multicol_prop()->computed_used_column_count = used_column_count;
        if (multicol_has_vertical_inline_axis(block)) {
            // vertical writing maps the fragmentainer block extent to physical width;
            // the column inline span is not part of that extent.
            block->multicol_prop()->computed_block_axis_extent = final_height;
        }

        float content_start_y = multicol_content_start_y(block);
        lycon->block.advance_y = has_leading_line_offset
            ? final_height : content_start_y + final_height;

        multicol_finalize_fragmented_inline_continuations(static_cast<View*>(block));
        ViewElement* parent_view = block->parent_view();
        if (parent_view && parent_view->view_type == RDT_VIEW_INLINE_BLOCK) {
            // a block child makes its inline-block parent use the bottom margin edge as baseline.
            lycon->line.max_ascender = 0.0f;
            lycon->line.max_descender = 0.0f;
            lycon->block.first_line_ascender = 0.0f;
            lycon->block.last_line_ascender = 0.0f;
            block->block_mut()->first_line_baseline = 0.0f;
            block->block_mut()->last_line_baseline = 0.0f;
        }
        flow_scratch.release(&lycon->scratch);
        return;
    }
    // Phase 3: Assign blocks to columns, balancing each column group
    // Process blocks in groups separated by spanners. For each group of
    // non-spanner blocks, compute a balanced height and distribute across
    // columns. Spanners are placed at full container width between groups.
    // CSS Box 4 §3.1: margin-trim:block-end — trim the last in-flow child's
    // block-end margin. We handle this here since layout_block_inner_content
    // returns early for multicol containers.
    bool trim_block_end = block->blk && (block->block()->margin_trim & MARGIN_TRIM_BLOCK_END);
    if (trim_block_end && block_count > 0) {
        ViewBlock* last_block = blocks[block_count - 1].block;
        if (last_block->bound && blocks[block_count - 1].margin_after != 0.0f) {
            float old_mb = blocks[block_count - 1].margin_after;
            last_block->boundary_mut()->margin.bottom = 0;
            last_block->boundary_mut()->flow_margin.bottom = 0;
            last_block->boundary_mut()->has_flow_margin = true;
            // Update the cached height in blocks array
            blocks[block_count - 1].height -= old_mb;
            blocks[block_count - 1].margin_after = 0.0f;
        }
    }

    float content_start_y = multicol_content_start_y(block);

    float max_column_height = 0;  // running Y offset for the entire container
    float prev_margin_bottom = 0; // for margin collapsing between consecutive spanners

    MulticolGroupScratch group_scratch = {};
    if (!group_scratch.init(&lycon->scratch)) {
        log_error("[MULTICOL] Failed to allocate group scratch buffers");
        group_scratch.release(&lycon->scratch);
        flow_scratch.release(&lycon->scratch);
        return;
    }

    int i = 0;
    while (i < block_count) {
        // --- Spanner: place at full width ---
        if (blocks[i].spans_all) {
            ViewBlock* child_block = blocks[i].block;
            float spanner_margin_top = 0.0f;
            float spanner_margin_bottom = 0.0f;
            multicol_flow_margins(block, child_block,
                                  &spanner_margin_top, &spanner_margin_bottom);
            // CSS 2.1 §8.3.1: Collapse margin between previous element and
            // this spanner. Use max of the two positive margins (simplified —
            // negative margin handling omitted for now).
            float collapsed_margin = max(prev_margin_bottom, spanner_margin_top);
            // Subtract the already-accounted prev_margin_bottom from max_column_height
            max_column_height -= prev_margin_bottom;
            max_column_height += collapsed_margin;
            // css writing modes: a spanner keeps its block-size and spans the
            // complete inline axis; its block-start position follows the
            // preceding column group in the vertical block direction.
            if (vertical_writing) {
                BoxMetrics block_box = layout_box_metrics(block);
                float block_origin_x = block_box.border.left + block_box.padding.left;
                child_block->x = block_origin_x + available_width -
                    max_column_height - child_block->width;
                child_block->y = content_start_y;
            } else {
                child_block->x = column_group_origin_x;
                child_block->y = content_start_y + max_column_height;
            }
            // css multicol: a spanner is formatted at the full inline size;
            // reapply its resolved alignment after phase-one column sizing.
            multicol_set_spanner_inline_size(
                child_block, vertical_writing ? available_inline_extent : available_width);
            multicol_relayout_special_spanner_text(lycon, child_block);
            // css multicol: special principal boxes contribute their final
            // used block-size after their own direct text has been relaid out.
            float spanner_flow_extent = vertical_writing
                ? child_block->width : child_block->height;
            max_column_height += spanner_flow_extent + spanner_margin_bottom;
            if (i + 1 < block_count && multicol_uses_fixed_balanced_rows(block)) {
                float row_height = multicol_specified_row_height(block);
                float row_gap = multicol_row_gap(block);
                if (row_gap < 0.0f) row_gap = 0.0f;
                if (blocks[i + 1].spans_all) {
                    // css gaps §2.1: consecutive spanner lines are separated
                    // by the gutter between their fixed rows.
                    max_column_height += row_gap;
                } else if (child_block->height > row_height + 0.5f) {
                    // css multicol-2 §4.2: following flow starts at the next
                    // fixed-row boundary after a tall spanner's end.
                    float row_pitch = row_height + row_gap;
                    if (row_pitch > 0.0f) {
                        float next_row_start = ceilf(max_column_height / row_pitch) * row_pitch;
                        if (next_row_start > max_column_height + 0.5f) {
                            max_column_height = next_row_start;
                        }
                    }
                }
            }
            prev_margin_bottom = spanner_margin_bottom;

            i++;
            continue;
        }
        // --- Column group: collect consecutive non-spanner blocks ---
        int group_start = i;
        float group_total_height = 0.0f;
        int group_item_count = multicol_collect_flow_group(
            blocks, block_count, &i, group_scratch.heights,
            group_scratch.content_heights, group_scratch.margin_before,
            group_scratch.margin_after,
            group_scratch.can_fragment, group_scratch.break_before,
            group_scratch.break_after, &group_total_height);
        int group_end = i;  // exclusive
        if (group_end < block_count && blocks[group_end].spans_all) {
            multicol_allow_fragmentation_before_spanner(
                blocks, group_start, group_end, group_scratch.can_fragment);
        }
        bool group_has_margins = multicol_group_has_block_margins(
            blocks, group_start, group_end);
        bool group_follows_spanner = group_start > 0 &&
            (blocks[group_start - 1].spans_all ||
             multicol_requires_separate_spanner_group(blocks[group_start - 1].block));
        bool group_precedes_spanner_wrapper = group_end < block_count &&
            multicol_requires_separate_spanner_group(blocks[group_end].block);
        bool group_adjacent_to_spanner = group_follows_spanner ||
            (group_end < block_count && blocks[group_end].spans_all) ||
            group_precedes_spanner_wrapper;
        int self_sizing_count = 0;
        float group_balance_total = multicol_group_balance_total(
            blocks, group_start, group_item_count, group_total_height,
            &self_sizing_count);
        if (group_follows_spanner) {
            // css multicol §6: a continuation column starts without the child's block-start margin.
            for (int gi = 1; gi < group_item_count; gi++) {
                group_balance_total -= group_scratch.margin_before[gi];
            }
            if (group_balance_total < 0.0f) group_balance_total = 0.0f;
        }
        // Calculate target fragmentainer height for this column group
        int balance_column_count = column_count - self_sizing_count;
        if (balance_column_count < 1) balance_column_count = 1;
        float group_balanced = group_balance_total / balance_column_count;
        // CSS Multicol §7.2: column-fill:balance distributes content evenly.
        // Use ceiling to avoid underfilling the last column.
        if (!group_follows_spanner) group_balanced = ceilf(group_balanced);
        float group_target = multicol_group_target_height(block, group_balanced, group_balance_total);
        group_target = multicol_balanced_target_search(
            block, group_scratch.heights, group_scratch.can_fragment,
            group_scratch.break_before, group_scratch.break_after,
            group_item_count, column_count, group_target, group_balance_total,
            group_has_margins ? group_scratch.content_heights : nullptr,
            group_has_margins ? group_scratch.margin_before : nullptr,
            group_has_margins ? group_scratch.margin_after : nullptr,
            group_adjacent_to_spanner,
            group_item_count == 1 ? blocks[group_start].block : nullptr);
        group_target = multicol_avoid_break_target_floor(
            block, blocks, group_start, group_end, group_target);
        float definite_fragmentainer_height = multicol_content_box_height_limit(block);
        bool single_nested_spanner_flow = group_item_count == 1 &&
            blocks[group_start].block &&
            blocks[group_start].block->multicol_prop() &&
            is_multicol_container(blocks[group_start].block) &&
            multicol_has_spanner_child(blocks[group_start].block);
        float nested_flow_height_limit = group_item_count == 1 &&
            blocks[group_start].block &&
            blocks[group_start].block->multicol_prop() &&
            is_multicol_container(blocks[group_start].block)
                ? multicol_content_box_height_limit(blocks[group_start].block)
                : -1.0f;
        if (single_nested_spanner_flow && definite_fragmentainer_height >= 0.0f) {
            // css multicol §7.2: a nested spanner flow is fragmented by the
            // definite ancestor fragmentainer before its own columns continue.
            group_target = definite_fragmentainer_height;
        }
        if (!single_nested_spanner_flow && column_count <= 1 &&
            nested_flow_height_limit > definite_fragmentainer_height &&
            definite_fragmentainer_height >= 0.0f) {
            // css fragmentation: a nested multicol with a taller definite
            // flow is split at the containing fragmentainer boundary.
            group_target = definite_fragmentainer_height;
        }
        if (group_end < block_count &&
            (blocks[group_end].spans_all ||
             multicol_requires_separate_spanner_group(blocks[group_end].block)) &&
            block->multicol_prop()->fill == COLUMN_FILL_AUTO &&
            definite_fragmentainer_height < 0.0f &&
            !multicol_has_out_of_flow_descendant(blocks[group_start].block)) {
            // css multicol §7.2: content before a spanner is balanced even
            // when column-fill:auto has no definite fragmentainer height.
            group_target = group_balanced;
        }
        if (group_end < block_count && blocks[group_end].spans_all &&
            block->multicol_prop()->fill == COLUMN_FILL_BALANCE &&
            definite_fragmentainer_height > 0.0f &&
            group_target > definite_fragmentainer_height) {
            // css multicol: a definite fragmentainer height caps the line before a spanner.
            group_target = definite_fragmentainer_height;
        }
        if (group_follows_spanner && !multicol_group_wraps_rows(block) &&
            definite_fragmentainer_height > 0.0f) {
            // css fragmentation: after a spanner, the next group uses the
            // remaining space in the current fixed fragmentainer; overflow
            // then continues in additional columns.
            group_target = max(definite_fragmentainer_height -
                max_column_height, 1.0f);
        }
        // Distribute this group's blocks across columns
        ColumnGroup group;
        FragmentedFlowCursor cursor;
        group.fragments = group_scratch.fragments;
        multicol_group_init(&group, block, group_target, column_count,
                            column_width, gap, column_group_origin_x);
        multicol_cursor_init(&cursor, &group);

        int used_column_count = 1;
        float detached_spanner_extent = 0.0f;
        float group_block_origin = max_column_height;
        bool publish_vertical_child_geometry =
            multicol_can_publish_vertical_child_geometry(block);
        auto adjust_direct_placement = [&](MulticolFlowItem& info, ViewBlock* child_block,
                                           float, float, float) {
            if (!vertical_writing || !publish_vertical_child_geometry) {
                child_block->x += info.inline_offset;
            }
        };
        auto handle_direct_content = [&](MulticolFlowItem& info, ViewBlock* child_block,
                                         float* placed_height) {
            if (!multicol_has_direct_spanner_child(child_block)) return false;
            *placed_height = multicol_split_child_around_spanners(
                lycon, block, child_block, column_count, column_width, gap,
                false, group_target, &detached_spanner_extent);
            if (multicol_truncates_start_margin(block, child_block)) {
                // css-break §5.2: the truncated margin must not advance the outer flow either.
                *placed_height = max(*placed_height - info.margin_before, 0.0f);
            }
            return true;
        };
        auto adjust_direct_height = [&](MulticolFlowItem&, ViewBlock* child_block,
                                        bool, float*, float placement_block_offset) {
            if (!vertical_writing || !publish_vertical_child_geometry || !child_block) {
                return;
            }
            BoxMetrics box = layout_box_metrics(block);
            float block_origin = box.border.left + box.padding.left;
            float block_extent = layout_content_size_from_border_box(
                block, block->width, true);
            float flow_offset = group_block_origin + placement_block_offset;
            if (layout_block_writing_mode(block) == WM_VERTICAL_RL) {
                child_block->x = block_origin + block_extent - flow_offset -
                    child_block->width;
            } else {
                child_block->x = block_origin + flow_offset;
            }
        };
        multicol_distribute_flow_group(
            lycon, block, blocks, group_start, group_end, group_target,
            content_start_y + max_column_height, true, &group, &cursor,
            &used_column_count, adjust_direct_placement,
            handle_direct_content, adjust_direct_height);
        multicol_group_finish(&group, &cursor);
        for (int fi = 0; fi < group.fragment_count; fi++) {
            int candidate = group.fragments[fi].column_index + 1;
            if (candidate > used_column_count) used_column_count = candidate;
        }
        if (used_column_count > block->multicol_prop()->computed_used_column_count) {
            block->multicol_prop()->computed_used_column_count = used_column_count;
        }
        float group_extent = group.group_used_height + detached_spanner_extent;
        if (group_total_height <= 0.0f && group_target > 0.0f &&
            group_start < group_end &&
            multicol_has_out_of_flow_descendant(
                static_cast<View*>(blocks[group_start].block))) {
            // css multicol §3: positioned overflow establishes the
            // fragmentainer size, while the zero-height wrapper stays zero.
            group_extent = max(group_extent, group_target);
        }
        if (group_end > group_start &&
            multicol_requires_separate_spanner_group(blocks[group_end - 1].block)) {
            // css-break: a wrapper's trailing collapsed margin follows its
            // escaped spanner before the next column group.
            group_extent += blocks[group_end - 1].margin_after;
        }
        if (multicol_uses_fixed_balanced_rows(block)) {
            float row_height = multicol_specified_row_height(block);
            bool group_precedes_spanner = group_end < block_count &&
                blocks[group_end].spans_all;
            if (group_adjacent_to_spanner &&
                group_total_height > group_target + group.row_gap + 0.5f) {
                group_extent += group.row_gap;
                if (group_follows_spanner && group_start > 0 &&
                    blocks[group_start - 1].spans_all &&
                    blocks[group_start - 1].block->height <= row_height + 0.5f) {
                    group_extent += group.row_gap;
                }
            } else if (group_precedes_spanner && !group_follows_spanner &&
                       group_total_height >= row_height - 0.5f) {
                group_extent = max(group_extent, row_height + group.row_gap);
            }
            if (!group_adjacent_to_spanner && row_height > 0.0f &&
                group_extent > 0.0f) {
                // css multicol-2 §4.2: fixed wrapped rows occupy complete
                // row tracks even when balanced fragments end mid-row.
                float row_pitch = row_height + group.row_gap;
                float row_count = multicol_is_single_column_height_only(block) &&
                        row_pitch > 0.0f
                    ? ceilf((group_extent + group.row_gap) / row_pitch)
                    : ceilf(group_extent / row_height);
                float row_extent = row_count * row_height;
                if (multicol_is_single_column_height_only(block)) {
                    row_extent += (row_count - 1.0f) * group.row_gap;
                }
                group_extent = max(group_extent, row_extent);
            }
        } else if (multicol_uses_fixed_wrapped_rows(block) &&
                   !group_adjacent_to_spanner) {
            float row_height = multicol_specified_row_height(block);
            if (row_height > 0.0f && group_extent > 0.0f) {
                // css multicol-2 §4.2: auto-fill still reserves every
                // fixed-height row needed by the fragmented flow.
                int required_slot_count = (int)ceilf(group_total_height /
                    row_height); // INT_CAST_OK: row slot count from positive flow extent
                if (required_slot_count < 1) required_slot_count = 1;
                int row_count = (required_slot_count + group.column_count - 1) /
                    group.column_count;
                if (row_count < 1) row_count = 1;
                group_extent = max(group_extent, row_count * row_height);
            }
        }
        if (group_follows_spanner && group_end == block_count &&
            group_target > group_extent) {
            // css multicol §7.2: the final column set keeps its balanced block extent after a spanner.
            group_extent = group_target;
        }
        max_column_height += group_extent;
        prev_margin_bottom = 0;  // column group doesn't have trailing margin
    }

    if (multicol_uses_fixed_balanced_rows(block) &&
        max_column_height > 0.0f && multicol_has_escaping_spanner_in_flow(block)) {
        // css gaps §2.1: the final fixed row includes the gutter before the
        // next row established by the spanner flow.
        float row_gap = multicol_row_gap(block);
        if (row_gap > 0.0f) max_column_height += row_gap;
    }

    float trailing_br_extent = multicol_reanchor_trailing_br_after_flow(
        lycon, block, max_column_height,
        multicol_content_box_height_limit(block), column_count,
        column_width, gap);
    if (trailing_br_extent > 0.0f) {
        // css inline: a post-spanner break establishes a final line box in a
        // new column group even though it has no text rect to redistribute.
        max_column_height += trailing_br_extent;
    }

    for (View* placed = block->first_placed_child(); placed; placed = placed->next()) {
        ViewBlock* child_block = lam::view_as_block(placed);
        if (!child_block || layout_block_is_out_of_flow_positioned(child_block)) continue;
        multicol_adjust_contained_block_start(block, child_block, true);
    }

    bool has_text_box_trim = block->blk && block->block()->text_box_trim;
    bool has_leading_block_inline_run =
        multicol_has_block_before_inline_run(block) && !has_text_box_trim;
    float mixed_direct_height = 0.0f;
    int mixed_direct_columns = 0;
    bool handled_mixed_direct_flow = multicol_reflow_mixed_direct_flow(
        lycon, block, column_count, column_width, gap,
        column_group_origin_x, content_start_y,
        &mixed_direct_height, &mixed_direct_columns);
    if (handled_mixed_direct_flow) {
        // css 2.1 §9.2.1.1: direct inline runs form anonymous blocks around
        // each block child before multicol balancing distributes the sequence.
        max_column_height = mixed_direct_height;
        if (mixed_direct_columns > block->multicol_prop()->computed_used_column_count) {
            block->multicol_prop()->computed_used_column_count = mixed_direct_columns;
        }
    } else {
        float mixed_flow_total = total_content_height;
        if (has_leading_block_inline_run) {
            // css multicol: a phase-one vertical inline size is not the block-flow
            // extent of a trailing inline run; normalize it to the preceding flow.
            float trailing_inline_extent = multicol_trailing_inline_flow_extent(
                block, max_column_height);
            mixed_flow_total = max(max_column_height, trailing_inline_extent);
        }
        float mixed_inline_target = multicol_group_target_height(
            block, ceilf(mixed_flow_total / column_count), mixed_flow_total);
        DirectInlineFlowEnd direct_inline_flow;
        float mixed_inline_height = multicol_project_mixed_direct_inline_content(
            block, column_count, column_width, gap, mixed_inline_target, &direct_inline_flow,
            has_leading_block_inline_run ? max_column_height : -1.0f);
        if (mixed_inline_height > max_column_height) {
            max_column_height = mixed_inline_height;
        }
        float direct_block_extent = 0.0f;
        if (multicol_place_direct_block_after_inline_flow(
                block, direct_inline_flow, column_count, column_width, gap,
                column_group_origin_x, content_start_y, mixed_inline_target,
            &direct_block_extent) && direct_block_extent > max_column_height) {
                max_column_height = direct_block_extent;
        }
    }

    bool balanced_nested_overflow =
        block->multicol_prop()->fill == COLUMN_FILL_BALANCE &&
        multicol_content_box_height_limit(block) < 0.0f &&
        multicol_has_definite_ancestor_fragmentainer(block) &&
        block->multicol_prop()->computed_used_column_count >
            block->multicol_prop()->computed_column_count;
    if (balanced_nested_overflow && column_count > 0) {
        float balanced_fragmentainer_height = multicol_group_target_height(
            block, ceilf(total_content_height / column_count), total_content_height);
        if (balanced_fragmentainer_height > max_column_height) {
            // css multicol: balancing retains the used fragmentainer size even
            // when a nested flow exposes continuation columns for overflow.
            max_column_height = balanced_fragmentainer_height;
        }
    }

    if (multicol_has_vertical_inline_axis(block)) {
        // finalization needs the balanced physical block span separately from
        // the definite vertical inline size stored in block->height.
        block->multicol_prop()->computed_block_axis_extent = max_column_height;
    }
    // Set block height: use CSS given height if specified, otherwise computed
    float total_height = multicol_used_total_height(block, max_column_height, false);
    block->height = total_height;
    block->content_height = max_column_height + layout_axis_padding_end(block->bound, LAYOUT_AXIS_Y);
    multicol_mirror_rtl_horizontal_children(
        block, column_group_origin_x, available_width);
    multicol_apply_positioned_fragment_anchors(lycon, block);
    multicol_finalize_fragmented_inline_continuations(static_cast<View*>(block));
    multicol_normalize_vertical_inline_fragment_bounds(static_cast<View*>(block));
    multicol_reposition_fragmented_positioned_subtree(lycon, static_cast<View*>(block));
    float out_of_flow_target = multicol_out_of_flow_balance_target(block);
    if (out_of_flow_target > 0.0f && max_column_height >= out_of_flow_target) {
        multicol_project_out_of_flow_descendants(
            block, out_of_flow_target, column_count, column_width, gap);
    }
    multicol_store_positioned_baselines(lycon, block);
    if (vertical_writing && block->blk && block_count > 0 &&
        multicol_can_publish_vertical_child_geometry(block)) {
        // css writing modes: multicol distribution has already published its
        // physical child geometry; do not remap it as ordinary vertical flow.
        block->blk->vertical_geometry_published = true;
    }
    // Update layout context's advance_y to reflect actual height
    lycon->block.advance_y = content_start_y + max_column_height;

    group_scratch.release(&lycon->scratch);
    flow_scratch.release(&lycon->scratch);
}
