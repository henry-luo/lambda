#include "layout.hpp"
#include "../lib/log.h"
#include "../lib/tagged.hpp"
#include <math.h>

// Max number of blocks that can be distributed in multicol layout
#define MAX_MULTICOL_BLOCKS 1024

// min/max macros for int and float
#define MIN_INT(a, b) ((a) < (b) ? (a) : (b))
#define MAX_INT(a, b) ((a) > (b) ? (a) : (b))
#define MIN_FLOAT(a, b) ((a) < (b) ? (a) : (b))
#define MAX_FLOAT(a, b) ((a) > (b) ? (a) : (b))

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
    if (!block->multicol) return false;

    // Explicit column-count:1 still establishes a multicol context for spanners.
    return block->multicol->column_count > 0 ||
           block->multicol->column_width > 0 ||
           block->multicol->column_height > 0;
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
    if (gap < 0) gap = 0;

    int column_count = multicol->column_count;  // 0 = auto
    float column_width = multicol->column_width; // 0 = auto

    log_debug("[MULTICOL] Input: count=%d, width=%.1f, gap=%.1f, available=%.1f",
              column_count, column_width, gap, available_width);

    // CSS Multi-column §3.4: Pseudo-algorithm for column layout
    if (column_count > 0 && column_width > 0) {
        // Both specified: use min of count and what fits
        int max_by_width = (int)floorf((available_width + gap) / (column_width + gap)); // INT_CAST_OK: integer column count
        column_count = MIN_INT(column_count, MAX_INT(1, max_by_width));
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
        column_count = MAX_INT(1, column_count);
        // Recalculate width to fill available space
        column_width = (available_width - (column_count - 1) * gap) / column_count;
    }
    else {
        // Neither specified: single column
        column_count = 1;
        column_width = available_width;
    }

    // Ensure at least 1 column
    column_count = MAX_INT(1, column_count);
    column_width = MAX_FLOAT(0.0f, column_width);

    log_debug("[MULTICOL] Computed: count=%d, width=%.1f, gap=%.1f",
              column_count, column_width, gap);

    *out_column_count = column_count;
    *out_column_width = column_width;
    *out_gap = gap;
}

/**
 * Structure to track column state during layout
 */
struct ColumnState {
    int column_index;           // Current column (0-based)
    float column_top;           // Y position at column start
    float column_height;        // Height used in current column
    float max_column_height;    // Maximum height across all columns (for balancing)
    float balanced_height;      // Target height for balanced columns
    bool balancing;             // True if balancing pass
};

struct ColumnFragment {
    int fragment_index;
    int column_index;
    int row_index;
    float x;
    float y;
    float width;
    float target_height;
    float used_height;
};

struct ColumnGroup {
    // fragments points into lycon->scratch; allocate with multicol_group_alloc_fragments,
    // free with scratch_free after multicol_group_finish. Capacity is MAX_MULTICOL_BLOCKS.
    ColumnFragment* fragments;
    int fragment_count;
    int column_count;
    float column_width;
    float column_gap;
    float inline_origin;
    float row_gap;
    float target_height;
    float group_used_height;
    bool wraps_rows;
};

struct FragmentedFlowCursor {
    ColumnGroup* group;
    int current_fragment;
    float block_offset;
};

struct InlineFragmentItem {
    View* view;
    TextRect* rect;
    float original_x;
    float original_y;
    float line_y;
    float height;
    int line_index;
    bool is_text;
};

static float multicol_group_target_height(ViewBlock* block, float balanced_height, float group_total_height);
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
static ColumnFragment* multicol_cursor_current_fragment(FragmentedFlowCursor* cursor);
static void multicol_cursor_place_block(
    FragmentedFlowCursor* cursor,
    ViewBlock* child,
    float group_y
);
static void multicol_cursor_advance_block(FragmentedFlowCursor* cursor, float block_height);
static void multicol_cursor_advance_fragmented_block(
    FragmentedFlowCursor* cursor,
    float flow_height
);
static void multicol_group_finish(ColumnGroup* group, FragmentedFlowCursor* cursor);
static float multicol_balanced_target_search(
    ViewBlock* block,
    float* item_heights,
    bool* break_before,
    bool* break_after,
    int item_count,
    int column_count,
    float fallback_target,
    float group_total_height
);
static bool multicol_group_wraps_rows(ViewBlock* container);
static void multicol_store_layout_fragments(
    ViewBlock* child,
    int fragment_count,
    int column_count,
    float fragment_height,
    float column_width,
    float column_gap,
    float row_gap,
    float fragment_visual_width
);
static void multicol_project_fragmented_descendants(
    LayoutContext* lycon,
    ViewBlock* child,
    float fragment_height,
    int column_count,
    float column_width,
    float column_gap,
    float block_split_height
);

// Forward declarations for layout functions
void layout_flow_node(LayoutContext* lycon, DomNode* node);
void line_break(LayoutContext* lycon);
void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, CssEnum outer_display);
void prescan_and_layout_floats(LayoutContext* lycon, DomNode* first_child, ViewBlock* block);

static float multicol_content_box_height_limit(ViewBlock* block) {
    if (!block || !block->blk) return -1;

    float limit = -1;
    if (block->multicol && block->multicol->column_height > 0) {
        limit = block->multicol->column_height;
    } else if (block->blk->given_height >= 0) {
        limit = block->blk->given_height;
    } else if (block->blk->given_max_height >= 0) {
        limit = block->blk->given_max_height;
    }

    if (limit < 0) return -1;

    if (block->bound && layout_uses_border_box(block)) {
        float border_padding = layout_box_metrics(block).pad_border_v;
        limit -= border_padding;
        if (limit < 0) limit = 0;
    }
    return limit;
}

static float multicol_row_gap(ViewBlock* block) {
    if (!block || !block->embed) return 0;
    if (block->embed->grid) return block->embed->grid->row_gap;
    if (block->embed->flex) return block->embed->flex->row_gap;
    return 0;
}

float multicol_normal_gap_size(ViewBlock* block) {
    if (block && block->font && block->font->font_size > 0.0f) {
        return block->font->font_size;
    }
    return 16.0f;
}

static bool multicol_is_out_of_flow(ViewBlock* block) {
    return layout_block_is_out_of_flow_positioned(block);
}

static bool multicol_uses_static_x(ViewBlock* block) {
    return block && block->position &&
           !block->position->has_left &&
           !block->position->has_right;
}

static bool multicol_uses_static_y(ViewBlock* block) {
    return block && block->position &&
           !block->position->has_top &&
           !block->position->has_bottom;
}

static bool multicol_is_spanner_block(ViewBlock* block) {
    return block && block->multicol &&
           block->multicol->span == COLUMN_SPAN_ALL &&
           !multicol_is_out_of_flow(block);
}

static bool multicol_forces_column_break(CssEnum value) {
    return value == CSS_VALUE_COLUMN ||
           value == CSS_VALUE_PAGE ||
           value == CSS_VALUE_LEFT ||
           value == CSS_VALUE_RIGHT;
}

static bool multicol_spanner_can_escape_child(ViewBlock* child) {
    if (!child) return false;
    if (block_context_establishes_bfc(child)) return false;
    if (child->embed && (child->embed->flex || child->embed->grid)) return false;
    if (child->view_type == RDT_VIEW_INLINE_BLOCK || child->view_type == RDT_VIEW_TABLE) return false;
    return true;
}

static bool multicol_has_direct_spanner_child(ViewBlock* child) {
    if (!multicol_spanner_can_escape_child(child)) return false;

    View* descendant = child->first_placed_child();
    while (descendant) {
        if (ViewBlock* descendant_block = lam::view_as_block(descendant)) {
            if (multicol_is_spanner_block(descendant_block)) {
                return true;
            }
        }
        descendant = descendant->next();
    }
    return false;
}

static void multicol_project_text_rect(TextRect* rect, float dx, float dy) {
    while (rect) {
        rect->x += dx;
        rect->y += dy;
        rect = rect->next;
    }
}

static void multicol_project_view_subtree(View* view, float dx, float dy) {
    if (!view) return;

    view->x += dx;
    view->y += dy;

    if (view->view_type == RDT_VIEW_TEXT) {
        multicol_project_text_rect(lam::view_require<RDT_VIEW_TEXT>(view)->rect, dx, dy);
        return;
    }

    if (view->is_group()) {
        View* child = lam::view_require_element(view)->first_placed_child();
        while (child) {
            multicol_project_view_subtree(child, dx, dy);
            child = child->next();
        }
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
        int fragment_index = (int)floorf(original_y / fragment_height); // INT_CAST_OK: fragment index from positive height
        if (fragment_index < 0) fragment_index = 0;
        int column_index = fragment_index % column_count;
        int row_index = fragment_index / column_count;
        float local_y = original_y - fragment_index * fragment_height;

        rect->x += column_index * (column_width + column_gap);
        rect->y = origin_y + row_index * (fragment_height + row_gap) + local_y;
        if (rect->x < min_x) min_x = rect->x;
        if (rect->y < min_y) min_y = rect->y;
        rect = rect->next;
    }

    if (min_x < 1e8f) text->x = min_x;
    if (min_y < 1e8f) text->y = min_y;
}

static void multicol_update_text_bounds(ViewText* text) {
    if (!text || !text->rect) return;

    float min_x = 1e9f;
    float min_y = 1e9f;
    float max_x = -1e9f;
    float max_y = -1e9f;
    TextRect* rect = text->rect;
    while (rect) {
        if (rect->x < min_x) min_x = rect->x;
        if (rect->y < min_y) min_y = rect->y;
        float rect_right = rect->x + rect->width;
        float rect_bottom = rect->y + rect->height;
        if (rect_right > max_x) max_x = rect_right;
        if (rect_bottom > max_y) max_y = rect_bottom;
        rect = rect->next;
    }

    if (min_x < 1e8f) text->x = min_x;
    if (min_y < 1e8f) text->y = min_y;
    if (max_x > min_x) text->width = max_x - min_x;
    if (max_y > min_y) text->height = max_y - min_y;
}

static void multicol_reanchor_text_descendants(View* view, float target_x, float target_y) {
    if (!view) return;

    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(view);
        float min_x = 1e9f;
        float min_y = 1e9f;
        TextRect* rect = text->rect;
        while (rect) {
            if (rect->x < min_x) min_x = rect->x;
            if (rect->y < min_y) min_y = rect->y;
            rect = rect->next;
        }
        if (min_x < 1e8f && min_y < 1e8f) {
            multicol_project_text_rect(text->rect, target_x - min_x, target_y - min_y);
            multicol_update_text_bounds(text);
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

static void multicol_finalize_text_for_fragmented_block(View* view, ViewBlock* fragment_owner) {
    if (!view || !fragment_owner) return;

    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(view);
        TextRect* rect = text->rect;
        while (rect) {
            rect->x = 0;
            if (rect->y > fragment_owner->height || rect->y < 0) {
                rect->y = 0;
            }
            rect = rect->next;
        }
        multicol_update_text_bounds(text);
        return;
    }

    if (view->node_type == DOM_NODE_ELEMENT) {
        View* child = lam::dom_require<DOM_NODE_ELEMENT>(view)->first_child;
        while (child) {
            multicol_finalize_text_for_fragmented_block(child, fragment_owner);
            child = child->next_sibling;
        }
    }
}

static void multicol_reanchor_direct_text(ViewBlock* block, float content_offset_y) {
    if (!block) return;
    (void)content_offset_y;

    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(block);
    if (elem->layout_fragments && elem->layout_fragment_count > 1) {
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

static void multicol_finalize_fragmented_inline_continuations(View* view) {
    if (!view || view->node_type != DOM_NODE_ELEMENT) return;

    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(view);
    bool has_fragments = elem->layout_fragments && elem->layout_fragment_count > 1 && view->is_block();
    if (has_fragments) {
        multicol_finalize_text_for_fragmented_block(view, lam::view_require_block(view));
    }

    View* child = elem->first_child;
    while (child) {
        multicol_finalize_fragmented_inline_continuations(child);
        child = child->next_sibling;
    }
}

static ViewBlock* multicol_next_in_flow_block_sibling(View* start) {
    View* sibling = start ? start->next_sibling : nullptr;
    while (sibling) {
        if (sibling->is_element() && sibling->is_block()) {
            ViewBlock* block = lam::view_require_block(sibling);
            if (!multicol_is_out_of_flow(block)) {
                return block;
            }
        }
        sibling = sibling->next_sibling;
    }
    return nullptr;
}

static ViewBlock* multicol_prev_in_flow_block_sibling(View* start) {
    View* sibling = start ? start->prev_sibling : nullptr;
    while (sibling) {
        if (sibling->is_element() && sibling->is_block()) {
            ViewBlock* block = lam::view_require_block(sibling);
            if (!multicol_is_out_of_flow(block)) {
                return block;
            }
        }
        sibling = sibling->prev_sibling;
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
    LayoutFragmentBox* fragment = elem->layout_fragments;
    LayoutFragmentBox* last = nullptr;
    while (fragment) {
        last = fragment;
        fragment = fragment->next;
    }
    return last;
}

static void multicol_apply_static_fragment_anchor(ViewBlock* multicol, ViewBlock* oof) {
    if (!multicol || !oof || !multicol_is_out_of_flow(oof)) return;
    if (!multicol_uses_static_x(oof) && !multicol_uses_static_y(oof)) return;

    ViewBlock* anchor = multicol_next_in_flow_block_sibling(static_cast<View*>(oof));
    bool anchor_is_next = anchor != nullptr;
    if (!anchor) {
        anchor = multicol_prev_in_flow_block_sibling(static_cast<View*>(oof));
    }
    if (!anchor) return;

    float anchor_origin_x = 0;
    float anchor_origin_y = 0;
    ViewElement* parent = oof->parent_view();
    if (parent && parent->is_block()) {
        multicol_absolute_normal_origin(lam::view_require_block(parent), &anchor_origin_x, &anchor_origin_y);
    } else {
        multicol_absolute_normal_origin(multicol, &anchor_origin_x, &anchor_origin_y);
    }

    if (multicol_uses_static_x(oof)) {
        LayoutFragmentBox* last_fragment = anchor_is_next ? nullptr : multicol_last_layout_fragment(anchor);
        if (last_fragment) {
            oof->x = anchor_origin_x + anchor->x + last_fragment->x;
        } else {
            oof->x = anchor_origin_x + anchor->x;
        }
    }
    if (multicol_uses_static_y(oof)) {
        if (anchor_is_next) {
            oof->y = anchor_origin_y + anchor->y;
        } else {
            float margin_bottom = anchor->bound ? anchor->bound->margin.bottom : 0;
            LayoutFragmentBox* last_fragment = multicol_last_layout_fragment(anchor);
            if (last_fragment) {
                oof->y = anchor_origin_y + anchor->y + last_fragment->y + last_fragment->height + margin_bottom;
            } else {
                oof->y = anchor_origin_y + anchor->y + anchor->height + margin_bottom;
            }
        }
    }

    log_debug("[MULTICOL] Static fragment anchor for OOF %s -> (%.1f, %.1f) via %s",
              oof->node_name(), oof->x, oof->y, anchor->node_name());
}

static bool multicol_has_spanner_ancestor(ViewBlock* multicol, ViewBlock* block) {
    if (!multicol || !block) return false;

    ViewElement* ancestor = block->parent_view();
    while (ancestor && ancestor != multicol) {
        if (ViewBlock* ancestor_block = lam::view_as_block(ancestor)) {
            if (multicol_is_spanner_block(ancestor_block)) {
                return true;
            }
        }
        ancestor = ancestor->parent_view();
    }
    return false;
}

static void multicol_viewport_size(LayoutContext* lycon, ViewBlock* multicol, float* out_width, float* out_height) {
    if (!out_width || !out_height) return;

    float viewport_width = 0;
    float viewport_height = 0;
    if (lycon && lycon->ui_context) {
        viewport_width = lycon->ui_context->viewport_width * lycon->ui_context->pixel_ratio;
        viewport_height = lycon->ui_context->viewport_height * lycon->ui_context->pixel_ratio;
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
    if (!multicol || !oof || !multicol_is_out_of_flow(oof)) return false;
    if (!multicol_has_spanner_ancestor(multicol, oof)) return false;

    ViewBlock* containing_block = find_positioned_containing_block(oof);
    float containing_abs_x = 0;
    float containing_abs_y = 0;
    if (containing_block) {
        multicol_absolute_normal_origin(containing_block, &containing_abs_x, &containing_abs_y);
    }

    float viewport_width = 0;
    float viewport_height = 0;
    multicol_viewport_size(lycon, multicol, &viewport_width, &viewport_height);

    bool changed = false;
    if (oof->position->has_left) {
        oof->x = oof->position->left - containing_abs_x;
        changed = true;
    } else if (oof->position->has_right && viewport_width > 0) {
        oof->x = viewport_width - oof->position->right - oof->width - containing_abs_x;
        changed = true;
    }

    if (oof->position->has_top) {
        oof->y = oof->position->top - containing_abs_y;
        changed = true;
    } else if (oof->position->has_bottom && viewport_height > 0) {
        oof->y = viewport_height - oof->position->bottom - oof->height - containing_abs_y;
        changed = true;
    }

    if (changed) {
        log_debug("[MULTICOL] Spanner containing-block anchor for OOF %s -> (%.1f, %.1f)",
                  oof->node_name(), oof->x, oof->y);
    }
    return changed;
}

static void multicol_apply_positioned_fragment_anchors_in_subtree(
    LayoutContext* lycon,
    ViewBlock* multicol,
    View* view
) {
    if (!view || !multicol) return;

    if (view->is_element() && view->is_block()) {
        ViewBlock* block = lam::view_require_block(view);
        if (multicol_is_out_of_flow(block)) {
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
    if (!multicol || !multicol->multicol) return;

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

    View* descendant = child->first_placed_child();
    while (descendant) {
        if (descendant->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(descendant);
            TextRect* rect = text->rect;
            while (rect) {
                if (rect->width <= 0 && rect->length > 0) {
                    rect = rect->next;
                    continue;
                }

                include_line_item(rect->y - child->y, rect->height, false);
                rect = rect->next;
            }
        } else if (descendant->view_type == RDT_VIEW_BR) {
            include_line_item(descendant->y - child->y, descendant->height, true);
        }
        descendant = descendant->next();
    }

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

static bool multicol_uses_slice_start_trim(ViewBlock* child) {
    // slice only trims the real box edges. With trim-both, intermediate column
    // breaks keep their normal line-box under-edge, so only start-only trim
    // changes the first fragment's line capacity.
    return child && child->blk &&
        child->blk->box_decoration_break != CSS_VALUE_CLONE &&
        (child->blk->text_box_trim_applied & TEXT_BOX_TRIM_START) &&
        !(child->blk->text_box_trim_applied & TEXT_BOX_TRIM_END) &&
        child->blk->text_box_trim_start_amount > 0.0f;
}

static bool multicol_uses_slice_end_offset_trim(ViewBlock* child, float initial_fragment_offset) {
    // trim-end keeps the block-start half-leading. If the block starts partway
    // through a fragmentainer, that consumed space reduces only the first
    // fragment's line-box capacity.
    return child && child->blk &&
        child->blk->box_decoration_break != CSS_VALUE_CLONE &&
        initial_fragment_offset > 0.0f &&
        (child->blk->text_box_trim_applied & TEXT_BOX_TRIM_END) &&
        !(child->blk->text_box_trim_applied & TEXT_BOX_TRIM_START);
}

static int multicol_line_boxes_that_fit_fragment(float fragment_height, float line_advance) {
    if (fragment_height <= 0.0f || line_advance <= 0.0f) return 1;
    int count = (int)floorf((fragment_height + 0.5f) / line_advance); // INT_CAST_OK: line-box slot count from positive fragment height
    return count > 0 ? count : 1;
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
    float visual_height,
    float line_offset
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

static float multicol_child_initial_fragment_offset(ViewBlock* child, float fragment_height) {
    if (!child || fragment_height <= 0.0f) return 0.0f;

    ViewElement* parent = child->parent_view();
    if (!parent || !parent->is_block()) return 0.0f;

    ViewBlock* parent_block = lam::view_require_block(parent);
    float offset = fmodf(child->y - parent_block->y, fragment_height);
    if (offset < 0.0f) offset += fragment_height;
    if (offset >= fragment_height - 0.5f) return 0.0f;
    return offset;
}

static bool multicol_project_fragmented_inline_descendants(
    LayoutContext* lycon,
    ViewBlock* child,
    float fragment_height,
    int parent_column_count,
    float parent_column_width,
    float parent_column_gap,
    float row_gap
) {
    if (!child || fragment_height <= 0 || parent_column_count <= 0) return false;

    // 2048 InlineFragmentItem (~56 B) ≈ 112 KiB — too large for stack; allocate from scratch arena (LIFO).
    constexpr int MAX_INLINE_FRAGMENT_ITEMS = 2048;
    InlineFragmentItem* items = (InlineFragmentItem*)scratch_alloc(&lycon->scratch,
        MAX_INLINE_FRAGMENT_ITEMS * sizeof(InlineFragmentItem));
    if (!items) return false;
    int item_count = 0;

    View* descendant = child->first_placed_child();
    while (descendant && item_count < MAX_INLINE_FRAGMENT_ITEMS) {
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
                items[item_count].original_y = rect->y - child->y;
                items[item_count].line_y = items[item_count].original_y;
                items[item_count].height = rect->height;
                items[item_count].line_index = 0;
                items[item_count].is_text = true;
                item_count++;
                rect = rect->next;
            }
        } else if (descendant->view_type == RDT_VIEW_BR) {
            items[item_count].view = descendant;
            items[item_count].rect = NULL;
            items[item_count].original_x = descendant->x;
            items[item_count].original_y = descendant->y - child->y;
            items[item_count].line_y = items[item_count].original_y;
            items[item_count].height = descendant->height;
            items[item_count].line_index = 0;
            items[item_count].is_text = false;
            item_count++;
        }
        descendant = descendant->next();
    }

    if (item_count == 0) {
        scratch_free(&lycon->scratch, items);
        return false;
    }

    float first_line_y = items[0].original_y;
    float current_line_y = first_line_y;
    int line_index = 0;
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
        if (!items[i].is_text) {
            forced_break_pending = true;
        }
    }

    float line_advance = multicol_line_advance_from_items(items, item_count);
    float visual_height = multicol_line_visual_height_from_items(items, item_count);
    if (visual_height <= 0.0f) visual_height = line_advance;
    float normal_line_offset = multicol_normal_line_offset(line_advance, visual_height);
    bool slice_start_trim = multicol_uses_slice_start_trim(child);
    float initial_fragment_offset = multicol_child_initial_fragment_offset(child, fragment_height);
    bool slice_end_offset_trim = multicol_uses_slice_end_offset_trim(child, initial_fragment_offset);
    float first_fragment_line_offset = first_line_y;
    float continuation_line_offset = first_line_y;
    if (slice_start_trim) {
        first_fragment_line_offset = normal_line_offset - child->blk->text_box_trim_start_amount;
        if (first_fragment_line_offset < 0.0f) first_fragment_line_offset = 0.0f;
        continuation_line_offset = normal_line_offset;
    }

    int first_fragment_lines = 0;
    int continuation_fragment_lines = 0;
    if (slice_start_trim) {
        first_fragment_lines = multicol_lines_that_fit_fragment(
            fragment_height, line_advance, visual_height, first_fragment_line_offset);
        continuation_fragment_lines = multicol_lines_that_fit_fragment(
            fragment_height, line_advance, visual_height, continuation_line_offset);
    } else if (slice_end_offset_trim) {
        first_fragment_lines = multicol_line_boxes_that_fit_fragment(
            fragment_height - initial_fragment_offset, line_advance);
        continuation_fragment_lines = multicol_line_boxes_that_fit_fragment(
            fragment_height, line_advance);
    } else {
        first_fragment_lines = multicol_line_boxes_that_fit_fragment(fragment_height, line_advance);
        continuation_fragment_lines = first_fragment_lines;
    }

    int inner_column_count = 1;
    float inner_column_width = parent_column_width;
    float inner_column_gap = 0;
    if (child->multicol && is_multicol_container(child)) {
        calculate_multicol_dimensions(child->multicol, parent_column_width,
            multicol_normal_gap_size(child),
            &inner_column_count, &inner_column_width, &inner_column_gap);
        if (inner_column_count < 1) inner_column_count = 1;
    }

    float parent_pitch = parent_column_width + parent_column_gap;
    float inner_pitch = inner_column_width + inner_column_gap;

    for (int i = 0; i < item_count; i++) {
        int inner_fragment_index = 0;
        int line_slot = 0;
        multicol_map_line_to_fragment(items[i].line_index,
            first_fragment_lines, continuation_fragment_lines,
            &inner_fragment_index, &line_slot);
        int inner_column_index = inner_fragment_index % inner_column_count;
        int parent_fragment_index = inner_fragment_index / inner_column_count;
        int parent_column_index = parent_fragment_index % parent_column_count;
        int parent_row_index = parent_fragment_index / parent_column_count;

        float normalized_x = multicol_normalize_inline_x(items[i].original_x, child->x, inner_pitch);
        float line_local_delta_y = items[i].is_text ? items[i].original_y - items[i].line_y : 0;
        float fragment_line_offset = slice_start_trim && inner_fragment_index > 0 ?
            continuation_line_offset : first_fragment_line_offset;
        float new_x = normalized_x +
            parent_column_index * parent_pitch +
            inner_column_index * inner_pitch;
        float new_y = child->y + fragment_line_offset +
            parent_row_index * (fragment_height + row_gap) +
            line_slot * line_advance +
            line_local_delta_y;

        if (items[i].is_text) {
            items[i].rect->x = new_x;
            items[i].rect->y = new_y;
        } else {
            items[i].view->x = new_x;
            items[i].view->y = new_y;
        }
    }

    descendant = child->first_placed_child();
    while (descendant) {
        if (descendant->view_type == RDT_VIEW_TEXT) {
            multicol_update_text_bounds(lam::view_require<RDT_VIEW_TEXT>(descendant));
        }
        descendant = descendant->next();
    }

    log_debug("[MULTICOL] Projected %d inline continuation items across %d/%d line slots",
              item_count, first_fragment_lines, continuation_fragment_lines);
    scratch_free(&lycon->scratch, items);
    return true;
}

static void multicol_project_fragmented_descendants(
    LayoutContext* lycon,
    ViewBlock* child,
    float fragment_height,
    int column_count,
    float column_width,
    float column_gap,
    float block_split_height
) {
    if (!child || fragment_height <= 0 || column_count <= 0) return;
    if (block_split_height <= 0) block_split_height = fragment_height;

    float row_gap = multicol_row_gap(lam::view_require_block(child->parent));
    if (row_gap < 0) row_gap = 0;

    bool projected_inline = multicol_project_fragmented_inline_descendants(
        lycon, child, fragment_height, column_count, column_width, column_gap, row_gap);

    bool child_is_multicol = child->multicol && is_multicol_container(child);
    int inner_column_count = 1;
    float inner_column_width = column_width;
    float inner_column_gap = 0;
    if (child_is_multicol) {
        calculate_multicol_dimensions(child->multicol, column_width,
            multicol_normal_gap_size(child),
            &inner_column_count, &inner_column_width, &inner_column_gap);
        if (inner_column_count < 1) inner_column_count = 1;
        if (inner_column_width <= 0) inner_column_width = column_width;
    }

    View* descendant = child->first_placed_child();
    float subslot_flow_y = 0;
    while (descendant) {
        View* next = descendant->next();
        if (ViewBlock* descendant_block = lam::view_as_block(descendant)) {
            if (multicol_is_out_of_flow(descendant_block)) {
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

        if (descendant->view_type == RDT_VIEW_BR && projected_inline) {
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
            descendant_flow_height = descendant_block->height;
            if (descendant_block->bound) {
                descendant_flow_height += descendant_block->bound->margin.top +
                                          descendant_block->bound->margin.bottom;
            }
        }

        float original_y = use_subslot_flow ? subslot_flow_y : descendant->y - child->y;
        int fragment_index = (int)floorf(original_y / fragment_height); // INT_CAST_OK: fragment index from positive height
        if (fragment_index < 0) fragment_index = 0;
        int column_index = fragment_index % column_count;
        int row_index = fragment_index / column_count;
        float local_y = original_y - fragment_index * fragment_height;
        float new_x = descendant->x + column_index * (column_width + column_gap);
        float new_y = child->y + row_index * (fragment_height + row_gap) + local_y;
        bool descendant_fragmented = false;

        if (child_is_multicol) {
            int inner_fragment_index = fragment_index;
            int inner_column_index = inner_fragment_index % inner_column_count;
            int parent_fragment_index = inner_fragment_index / inner_column_count;
            int parent_column_index = parent_fragment_index % column_count;
            int parent_row_index = parent_fragment_index / column_count;
            local_y = original_y - inner_fragment_index * fragment_height;
            new_x = descendant->x +
                parent_column_index * (column_width + column_gap) +
                inner_column_index * (inner_column_width + inner_column_gap);
            new_y = child->y + parent_row_index * (fragment_height + row_gap) + local_y;

            if (descendant->is_block()) {
                ViewBlock* descendant_block = lam::view_require_block(descendant);
                if (descendant_block->height > block_split_height) {
                    int total_column_slots = column_count * inner_column_count;
                    int descendant_fragment_count = (int)ceilf(descendant_block->height / block_split_height); // INT_CAST_OK: fragment count from positive heights
                    if (descendant_fragment_count < 1) descendant_fragment_count = 1;
                    float union_width = inner_column_width +
                        (MIN_INT(descendant_fragment_count, total_column_slots) - 1) *
                        (inner_column_width + inner_column_gap);
                    if (child->width > union_width) union_width = child->width;
                    multicol_store_layout_fragments(descendant_block,
                        descendant_fragment_count, total_column_slots,
                        block_split_height, inner_column_width, inner_column_gap,
                        row_gap, inner_column_width);
                    if (descendant_block->width < union_width) descendant_block->width = union_width;
                    descendant_block->height = block_split_height;
                    float descendant_split_height = block_split_height;
                    if (inner_column_count > 1) {
                        descendant_split_height = block_split_height / inner_column_count;
                    }
                    multicol_project_fragmented_descendants(lycon, descendant_block,
                        fragment_height, total_column_slots,
                        inner_column_width, inner_column_gap, descendant_split_height);
                    descendant_fragmented = true;
                }
            }
        } else if (block_split_height > 0 && block_split_height < fragment_height) {
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

        if (!descendant_fragmented && descendant->is_block()) {
            ViewBlock* descendant_block = lam::view_require_block(descendant);
            if (descendant_block->height > block_split_height) {
                int descendant_fragment_count = (int)ceilf(descendant_block->height / block_split_height); // INT_CAST_OK: fragment count from positive heights
                if (descendant_fragment_count < 1) descendant_fragment_count = 1;
                int used_columns = MIN_INT(descendant_fragment_count, column_count);
                int row_count = (descendant_fragment_count + column_count - 1) / column_count;
                if (row_count < 1) row_count = 1;
                float fragment_visual_width = column_width;
                if (descendant_block->width > 0 && descendant_block->width < column_width) {
                    fragment_visual_width = descendant_block->width;
                }
                float union_width = fragment_visual_width +
                    (used_columns - 1) * (column_width + column_gap);
                float union_height = row_count * block_split_height + (row_count - 1) * row_gap;
                if (block_split_height < fragment_height) {
                    union_height = block_split_height;
                }
                multicol_store_layout_fragments(descendant_block,
                    descendant_fragment_count, column_count,
                    block_split_height, column_width, column_gap,
                    row_gap, fragment_visual_width);
                if (descendant_block->width < union_width) descendant_block->width = union_width;
                descendant_block->height = union_height;
            }
        }

        multicol_project_view_subtree(descendant, new_x - descendant->x, new_y - descendant->y);
        log_debug("[MULTICOL] Projected descendant %s to fragment %d column %d row %d",
                  descendant->node_name(), fragment_index, column_index, row_index);
        if (use_subslot_flow) {
            subslot_flow_y += descendant_flow_height;
        }
        descendant = next;
    }
}

static bool multicol_should_fragment_monolithic_child(
    ViewBlock* container,
    ViewBlock* child,
    float item_height,
    float fragment_height
) {
    if (!container || !container->multicol || !child) return false;
    bool has_definite_max_height = container->blk && container->blk->given_max_height >= 0;
    bool has_definite_height = container->blk && container->blk->given_height >= 0;
    bool child_is_multicol = child->multicol && is_multicol_container(child);
    bool has_fragmentainer_height =
        container->multicol->column_height > 0 ||
        (container->multicol->fill == COLUMN_FILL_BALANCE &&
         has_definite_height) ||
        (container->multicol->wrap == COLUMN_WRAP_WRAP &&
         multicol_content_box_height_limit(container) > 0) ||
        (container->multicol->fill == COLUMN_FILL_AUTO &&
         (has_definite_max_height || has_definite_height) &&
         multicol_content_box_height_limit(container) > 0);
    if (!has_fragmentainer_height) return false;
    if (fragment_height <= 0) return false;
    if (container->multicol->wrap == COLUMN_WRAP_NOWRAP) return false;
    if (container->multicol->wrap == COLUMN_WRAP_AUTO &&
        container->multicol->fill != COLUMN_FILL_AUTO &&
        container->multicol->fill != COLUMN_FILL_BALANCE &&
        has_definite_height && !child_is_multicol) {
        return false;
    }
    return item_height > fragment_height;
}

static void multicol_clear_layout_fragments(ViewBlock* block) {
    if (!block) return;
    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(block);
    elem->layout_fragments = nullptr;
    elem->layout_fragment_count = 0;
}

static void multicol_store_layout_fragments(
    ViewBlock* child,
    int fragment_count,
    int column_count,
    float fragment_height,
    float column_width,
    float column_gap,
    float row_gap,
    float fragment_visual_width
) {
    if (!child || fragment_count <= 1 || column_count <= 0 || fragment_height <= 0) {
        multicol_clear_layout_fragments(child);
        return;
    }

    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(child);
    elem->layout_fragments = nullptr;
    elem->layout_fragment_count = 0;

    Pool* pool = nullptr;
    if (elem->doc && elem->doc->view_tree && elem->doc->view_tree->pool) {
        pool = elem->doc->view_tree->pool;
    } else if (elem->doc) {
        pool = elem->doc->pool;
    }
    if (!pool) return;

    LayoutFragmentBox* first = nullptr;
    LayoutFragmentBox* prev = nullptr;
    float remaining_height = child->height;
    if (remaining_height <= 0) remaining_height = fragment_count * fragment_height;

    for (int fi = 0; fi < fragment_count; fi++) {
        LayoutFragmentBox* fragment = (LayoutFragmentBox*)pool_calloc(pool, sizeof(LayoutFragmentBox));
        if (!fragment) break;

        int column_index = fi % column_count;
        int row_index = fi / column_count;
        float fragment_y = row_index * (fragment_height + row_gap);
        float fragment_piece_height = remaining_height > fragment_height ? fragment_height : remaining_height;
        if (fragment_piece_height <= 0) fragment_piece_height = fragment_height;

        fragment->fragment_index = fi;
        fragment->column_index = column_index;
        fragment->row_index = row_index;
        fragment->x = column_index * (column_width + column_gap);
        fragment->y = fragment_y;
        fragment->width = fragment_visual_width;
        fragment->height = fragment_piece_height;
        fragment->next = nullptr;

        if (!first) first = fragment;
        if (prev) prev->next = fragment;
        prev = fragment;
        elem->layout_fragment_count++;
        remaining_height -= fragment_piece_height;
    }

    elem->layout_fragments = first;
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
    int* out_used_columns
) {
    float row_gap = multicol_row_gap(container);
    if (row_gap < 0) row_gap = 0;

    int fragment_count = (int)ceilf(item_height / fragment_height); // INT_CAST_OK: fragment count from positive heights
    if (fragment_count < 1) fragment_count = 1;
    int used_columns = MIN_INT(column_count, fragment_count);
    int row_count = (fragment_count + column_count - 1) / column_count;
    if (row_count < 1) row_count = 1;

    float fragment_visual_width = column_width;
    if (!(child->multicol && is_multicol_container(child)) &&
        child->width > 0 && child->width < column_width) {
        fragment_visual_width = child->width;
    }
    float union_width = fragment_visual_width + (used_columns - 1) * (column_width + column_gap);
    float union_height = row_count * fragment_height + (row_count - 1) * row_gap;
    if (!multicol_group_wraps_rows(container)) {
        union_height = fragment_height;
    }

    multicol_store_layout_fragments(child, fragment_count, column_count,
        fragment_height, column_width, column_gap, row_gap, fragment_visual_width);
    if (child->width < union_width) child->width = union_width;
    child->height = union_height;
    child->content_height = union_height;
    multicol_project_fragmented_descendants(lycon, child, fragment_height, column_count, column_width, column_gap, fragment_height);
    if (out_used_columns) *out_used_columns = used_columns;
    return union_height;
}

static float multicol_clone_fragmented_flow_height(
    ViewBlock* child,
    float item_height,
    int fragment_count
) {
    if (!child || !child->blk || fragment_count <= 1) return item_height;
    if (child->blk->box_decoration_break != CSS_VALUE_CLONE) return item_height;
    if (!(child->blk->text_box_trim_applied & TEXT_BOX_TRIM_START)) return item_height;

    float extra_start_trim = child->blk->text_box_trim_start_amount * fragment_count;
    if (extra_start_trim <= 0.0f) return item_height;

    float adjusted = item_height - extra_start_trim;
    if (adjusted < 0.0f) adjusted = 0.0f;
    log_debug("[MULTICOL] clone text-box-trim flow: item=%.1f fragments=%d extra_start=%.1f adjusted=%.1f",
              item_height, fragment_count, extra_start_trim, adjusted);
    return adjusted;
}

static float multicol_slice_text_box_trim_flow_height(
    ViewBlock* child,
    float item_height,
    float fragment_height,
    float initial_fragment_offset
) {
    if (!child || !child->blk || fragment_height <= 0.0f) return item_height;
    if (child->blk->box_decoration_break == CSS_VALUE_CLONE) return item_height;
    if (!(child->blk->text_box_trim_applied & (TEXT_BOX_TRIM_START | TEXT_BOX_TRIM_END))) {
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
        first_fragment_line_offset = normal_line_offset - child->blk->text_box_trim_start_amount;
        if (first_fragment_line_offset < 0.0f) first_fragment_line_offset = 0.0f;
        continuation_line_offset = normal_line_offset;
        first_fragment_lines = multicol_lines_that_fit_fragment(
            fragment_height, line_advance, visual_height, first_fragment_line_offset);
        continuation_fragment_lines = multicol_lines_that_fit_fragment(
            fragment_height, line_advance, visual_height, continuation_line_offset);
    } else if (slice_end_offset_trim) {
        first_fragment_lines = multicol_line_boxes_that_fit_fragment(
            fragment_height - initial_fragment_offset, line_advance);
        continuation_fragment_lines = multicol_line_boxes_that_fit_fragment(
            fragment_height, line_advance);
    } else {
        first_fragment_lines = multicol_line_boxes_that_fit_fragment(fragment_height, line_advance);
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
    if (child->blk->text_box_trim_applied & TEXT_BOX_TRIM_END) {
        flow_height -= child->blk->text_box_trim_end_amount;
    }
    if (slice_start_trim && initial_fragment_offset > 0.0f) {
        flow_height -= initial_fragment_offset + child->blk->text_box_trim_start_amount;
    } else if (slice_end_offset_trim) {
        flow_height -= initial_fragment_offset + normal_line_offset;
    }
    if (flow_height < 0.0f) flow_height = 0.0f;

    float margin_extra = 0.0f;
    if (child->bound) {
        margin_extra = child->bound->margin.top + child->bound->margin.bottom;
    }
    if (margin_extra > 0.0f) flow_height += margin_extra;

    log_debug("[MULTICOL] slice text-box-trim flow: item=%.1f lines=%d per_fragment=%d/%d flow=%.1f",
              item_height, line_count, first_fragment_lines, continuation_fragment_lines, flow_height);
    return flow_height;
}

static float multicol_text_box_trim_fragmented_flow_height(
    ViewBlock* child,
    float item_height,
    float fragment_height,
    int fragment_count,
    float initial_fragment_offset
) {
    if (child && child->blk && child->blk->box_decoration_break == CSS_VALUE_CLONE) {
        return multicol_clone_fragmented_flow_height(child, item_height, fragment_count);
    }
    return multicol_slice_text_box_trim_flow_height(child, item_height, fragment_height,
        initial_fragment_offset);
}

static float multicol_split_child_around_spanners(
    LayoutContext* lycon,
    ViewBlock* container,
    ViewBlock* child,
    int column_count,
    float column_width,
    float column_gap
) {
    if (!container || !child || column_count <= 0 || column_width <= 0) {
        return child ? child->height : 0;
    }

    struct ChildInfo {
        ViewBlock* block;
        float height;
        bool spans_all;
        bool break_before_column;
        bool break_after_column;
    };

    // MAX_MULTICOL_BLOCKS = 1024 → ChildInfo[] ≈ 24 KiB; move to scratch arena (LIFO).
    ChildInfo* children = (ChildInfo*)scratch_calloc(&lycon->scratch,
        MAX_MULTICOL_BLOCKS * sizeof(ChildInfo));
    if (!children) return child->height;
    int child_count = 0;
    View* descendant = child->first_placed_child();
    while (descendant && child_count < MAX_MULTICOL_BLOCKS) {
        if (descendant->is_block()) {
            ViewBlock* descendant_block = lam::view_require_block(descendant);
            if (!multicol_is_out_of_flow(descendant_block)) {
                float descendant_height = descendant_block->height;
                if (descendant_block->bound) {
                    descendant_height += descendant_block->bound->margin.top +
                                         descendant_block->bound->margin.bottom;
                }
                children[child_count].block = descendant_block;
                children[child_count].height = descendant_height;
                children[child_count].spans_all = multicol_is_spanner_block(descendant_block);
                children[child_count].break_before_column =
                    descendant_block->blk && multicol_forces_column_break(descendant_block->blk->break_before);
                children[child_count].break_after_column =
                    descendant_block->blk && multicol_forces_column_break(descendant_block->blk->break_after);
                child_count++;
            }
        }
        descendant = descendant->next();
    }

    if (child_count == 0) {
        scratch_free(&lycon->scratch, children);
        return child->height;
    }

    float child_origin_y = child->y;
    float current_y = child_origin_y;
    float prev_margin_bottom = 0;
    int used_column_count = 1;
    int non_spanner_count = 0;
    float first_group_target_height = -1;
    float spanner_extent = 0;
    float content_offset_x = 0;
    float leading_fragment_border_height = 0;
    bool leading_fragment_border_consumed = false;
    if (child->bound) {
        if (child->bound->border) {
            content_offset_x += child->bound->border->width.left;
            leading_fragment_border_height += child->bound->border->width.top;
        }
        content_offset_x += child->bound->padding.left;
        leading_fragment_border_height += child->bound->padding.top;
    }

    // Per-group scratch buffers shared across all groups in this child:
    // MAX_MULTICOL_BLOCKS = 1024 → ~10 KiB total. Allocated once before the loop, freed after.
    float* group_heights = (float*)scratch_alloc(&lycon->scratch,
        MAX_MULTICOL_BLOCKS * sizeof(float));
    bool* group_break_before = (bool*)scratch_alloc(&lycon->scratch,
        MAX_MULTICOL_BLOCKS * sizeof(bool));
    bool* group_break_after = (bool*)scratch_alloc(&lycon->scratch,
        MAX_MULTICOL_BLOCKS * sizeof(bool));
    // ColumnFragment[MAX_MULTICOL_BLOCKS] ≈ 32 KiB — backs ColumnGroup::fragments.
    ColumnFragment* fragments_buf = (ColumnFragment*)scratch_calloc(&lycon->scratch,
        MAX_MULTICOL_BLOCKS * sizeof(ColumnFragment));
    if (!group_heights || !group_break_before || !group_break_after || !fragments_buf) {
        if (fragments_buf) scratch_free(&lycon->scratch, fragments_buf);
        if (group_break_after) scratch_free(&lycon->scratch, group_break_after);
        if (group_break_before) scratch_free(&lycon->scratch, group_break_before);
        if (group_heights) scratch_free(&lycon->scratch, group_heights);
        scratch_free(&lycon->scratch, children);
        return child->height;
    }

    int i = 0;
    while (i < child_count) {
        if (children[i].spans_all) {
            ViewBlock* spanner = children[i].block;
            float margin_top = spanner->bound ? spanner->bound->margin.top : 0;
            float margin_bottom = spanner->bound ? spanner->bound->margin.bottom : 0;
            float collapsed_margin = MAX_FLOAT(prev_margin_bottom, margin_top);
            current_y -= prev_margin_bottom;
            current_y += collapsed_margin;

            spanner->x = child->x;
            spanner->y = current_y;
            spanner->width = container->width > 0 ? container->width : column_count * column_width + (column_count - 1) * column_gap;

            current_y += spanner->height + margin_bottom;
            spanner_extent += spanner->height + collapsed_margin + margin_bottom;
            prev_margin_bottom = margin_bottom;
            log_debug("[MULTICOL] Descendant spanner %s split child %s at y=%.1f",
                      spanner->node_name(), child->node_name(), spanner->y);
            i++;
            continue;
        }

        int group_start = i;
        float group_total_height = 0;
        int group_item_count = 0;
        while (i < child_count && !children[i].spans_all) {
            group_total_height += children[i].height;
            group_heights[group_item_count] = children[i].height;
            group_break_before[group_item_count] = children[i].break_before_column;
            group_break_after[group_item_count] = children[i].break_after_column;
            group_item_count++;
            i++;
        }
        int group_end = i;
        non_spanner_count += group_end - group_start;

        float balanced_height = ceilf(group_total_height / column_count);
        float target_height = multicol_group_target_height(container, balanced_height, group_total_height);
        target_height = multicol_balanced_target_search(
            container, group_heights, group_break_before, group_break_after,
            group_item_count, column_count, target_height, group_total_height);
        if (target_height <= 0) target_height = balanced_height;
        if (first_group_target_height < 0) {
            first_group_target_height = target_height;
        }

        ColumnGroup group;
        FragmentedFlowCursor cursor;
        group.fragments = fragments_buf;  // Backing store from outer scratch alloc.
        // the nested split path applies its own child content offset after placement.
        multicol_group_init(&group, container, target_height, column_count,
                            column_width, column_gap, 0.0f);
        multicol_cursor_init(&cursor, &group);

        for (int j = group_start; j < group_end; j++) {
            ViewBlock* block_child = children[j].block;
            float placed_height = children[j].height;
            if (children[j].break_before_column && cursor.block_offset > 0) {
                multicol_cursor_advance_fragment(&cursor);
            } else if (multicol_group_should_break(container, &cursor, children[j].height)) {
                multicol_cursor_advance_fragment(&cursor);
            }

            float old_x = block_child->x;
            float old_y = block_child->y;
            multicol_cursor_place_block(&cursor, block_child, current_y);
            block_child->x += content_offset_x;
            float placement_delta_x = block_child->x - old_x;
            float placement_delta_y = block_child->y - old_y;
            if (placement_delta_x != 0 || placement_delta_y != 0) {
                multicol_project_view_subtree(block_child, placement_delta_x, placement_delta_y);
                block_child->x = old_x + placement_delta_x;
                block_child->y = old_y + placement_delta_y;
            }
            if (multicol_should_fragment_monolithic_child(container, block_child, children[j].height, target_height) ||
                children[j].height > target_height) {
                int used_columns = 1;
                int fragment_count = target_height > 0.0f ?
                    (int)ceilf(children[j].height / target_height) : 1; // INT_CAST_OK: fragment count from positive heights
                if (fragment_count < 1) fragment_count = 1;
                float flow_height = multicol_text_box_trim_fragmented_flow_height(
                    block_child, children[j].height, target_height, fragment_count,
                    cursor.block_offset);
                placed_height = multicol_fragmented_child_union(
                    lycon, container, block_child, children[j].height, target_height,
                    column_count, column_width, column_gap, &used_columns);
                if (used_columns > group.fragment_count) {
                    group.fragment_count = used_columns;
                }
                multicol_cursor_advance_fragmented_block(&cursor, flow_height);
                placed_height = 0.0f;
            }
            if (!leading_fragment_border_consumed && j == group_start && leading_fragment_border_height > 0) {
                placed_height += leading_fragment_border_height;
                if (block_child->height > 0) {
                    block_child->height += leading_fragment_border_height;
                }
                leading_fragment_border_consumed = true;
            }
            ColumnFragment* fragment = multicol_cursor_current_fragment(&cursor);
            if (fragment) {
                int candidate = fragment->column_index + 1;
                if (candidate > used_column_count) used_column_count = candidate;
            }
            multicol_cursor_advance_block(&cursor, placed_height);
            if (children[j].break_after_column && j + 1 < group_end) {
                multicol_cursor_advance_fragment(&cursor);
            }
        }

        multicol_group_finish(&group, &cursor);
        for (int fi = 0; fi < group.fragment_count; fi++) {
            int candidate = group.fragments[fi].column_index + 1;
            if (candidate > used_column_count) used_column_count = candidate;
        }
        current_y += group.group_used_height;
        prev_margin_bottom = 0;
    }

    bool final_leading_text_consumed = false;
    for (int k = 0; k < child_count; k++) {
        if (children[k].spans_all) continue;
        float text_offset_y = 0;
        if (!final_leading_text_consumed && leading_fragment_border_height > 0) {
            text_offset_y = leading_fragment_border_height;
            final_leading_text_consumed = true;
        }
        DomElement* block_elem = lam::dom_require<DOM_NODE_ELEMENT>(children[k].block);
        if (block_elem->layout_fragments && block_elem->layout_fragment_count > 1) {
            if (text_offset_y > 0) {
                float target_y = text_offset_y + multicol_first_text_height(static_cast<View*>(children[k].block));
                View* text_child = block_elem->first_child;
                while (text_child) {
                    if (text_child->node_type == DOM_NODE_TEXT || text_child->node_type == DOM_NODE_ELEMENT) {
                        multicol_reanchor_text_descendants(text_child, 0, target_y);
                    }
                    text_child = text_child->next_sibling;
                }
            }
        } else {
            multicol_reanchor_direct_text(children[k].block, text_offset_y);
        }
    }

    float flow_height = current_y - child_origin_y;
    if (flow_height < 0) flow_height = 0;
    float new_height = flow_height;
    if (child->blk && child->blk->given_height >= 0 && first_group_target_height > 0) {
        float child_border_padding_height = 0;
        if (child->bound && !layout_uses_border_box(child)) {
            child_border_padding_height = layout_box_metrics(child).pad_border_v;
        }
        float decorated_split_adjustment = child_border_padding_height > 0 ?
            child_border_padding_height + spanner_extent : 0;
        float fragmented_visual_height = column_count <= 1 ?
            child->blk->given_height + child_border_padding_height + spanner_extent :
            child->blk->given_height + decorated_split_adjustment - first_group_target_height;
        if (fragmented_visual_height > 0 && fragmented_visual_height < new_height) {
            new_height = fragmented_visual_height;
        }
    }
    float union_width = used_column_count * column_width + (used_column_count - 1) * column_gap;
    float full_width = container->width > 0 ?
        container->width : column_count * column_width + (column_count - 1) * column_gap;
    if (full_width > union_width) union_width = full_width;
    if (non_spanner_count == 0 && spanner_extent > 0) {
        multicol_project_view_subtree(static_cast<View*>(child), 0, -flow_height);
        child->y = child_origin_y + flow_height;
        child->width = column_width;
        child->height = 0;
        child->content_height = 0;
    } else {
        if (child->width < union_width) child->width = union_width;
        child->height = new_height;
        child->content_height = new_height;
    }
    if (used_column_count > container->multicol->computed_used_column_count) {
        container->multicol->computed_used_column_count = used_column_count;
    }

    log_debug("[MULTICOL] Split child %s around descendant spanners: height=%.1f used_cols=%d",
              child->node_name(), new_height, used_column_count);
    // LIFO free in reverse order of allocation.
    scratch_free(&lycon->scratch, fragments_buf);
    scratch_free(&lycon->scratch, group_break_after);
    scratch_free(&lycon->scratch, group_break_before);
    scratch_free(&lycon->scratch, group_heights);
    scratch_free(&lycon->scratch, children);
    return flow_height;
}

static float multicol_group_target_height(ViewBlock* block, float balanced_height, float group_total_height) {
    if (!block || !block->multicol) return balanced_height;

    float limit = multicol_content_box_height_limit(block);
    if (block->multicol->fill == COLUMN_FILL_AUTO) {
        if (limit >= 0) return limit;
        return group_total_height;
    }
    if (block->multicol->wrap == COLUMN_WRAP_WRAP && limit >= 0) {
        return limit;
    }
    return balanced_height;
}

static float multicol_project_mixed_direct_inline_content(
    ViewBlock* block,
    int column_count,
    float column_width,
    float column_gap,
    float target_height
) {
    if (!block || column_count <= 0 || column_width <= 0.0f || target_height <= 0.0f) {
        return 0.0f;
    }

    View* direct_child = block->first_placed_child();
    while (direct_child) {
        if (ViewBlock* direct_block = lam::view_as_block(direct_child)) {
            if (multicol_is_spanner_block(direct_block)) {
                return 0.0f;
            }
        }
        direct_child = direct_child->next();
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
    int line_count = 0;
    int break_count = 0;

    View* child = block->first_child;
    while (child) {
        if (child->node_type == DOM_NODE_TEXT) {
            DomText* tnode = lam::dom_require<DOM_NODE_TEXT>(child);
            TextRect* rect = tnode->rect;
            while (rect && line_count < MAX_DIRECT_INLINE_LINES) {
                lines[line_count].rect = rect;
                lines[line_count].original_x = rect->x;
                lines[line_count].original_y = rect->y;
                lines[line_count].new_y = rect->y;
                lines[line_count].line_height = rect->height;
                lines[line_count].fragment_index = 0;
                line_count++;
                rect = rect->next;
            }
        } else if (child->view_type == RDT_VIEW_BR && break_count < MAX_DIRECT_INLINE_LINES) {
            breaks[break_count].view = child;
            breaks[break_count].original_x = child->x;
            breaks[break_count].original_y = child->y;
            breaks[break_count].line_index = -1;
            break_count++;
        }
        child = child->next_sibling;
    }

    if (line_count == 0) return 0.0f;

    float line_advance = -1.0f;
    for (int i = 1; i < line_count; i++) {
        float delta = fabsf(lines[i].original_y - lines[i - 1].original_y);
        if (delta > 1.0f && (line_advance < 0.0f || delta < line_advance)) {
            line_advance = delta;
        }
    }
    float visual_height = lines[0].line_height > 0.0f ? lines[0].line_height : 0.0f;
    if (line_advance <= 0.0f) line_advance = visual_height > 0.0f ? visual_height : 16.0f;
    if (visual_height <= 0.0f) visual_height = line_advance;

    float normal_line_offset = multicol_normal_line_offset(line_advance, visual_height);
    float first_line_box_y = lines[0].original_y - block->y - normal_line_offset;
    if (first_line_box_y < 0.0f && first_line_box_y > -normal_line_offset - 0.5f) {
        first_line_box_y = 0.0f;
    }

    int start_fragment = (int)floorf(first_line_box_y / target_height); // INT_CAST_OK: fragment index from positive flow offset
    if (start_fragment < 0) start_fragment = 0;
    float start_offset = first_line_box_y - start_fragment * target_height;
    if (start_offset < 0.0f) start_offset = 0.0f;

    int orphans = block->blk && block->blk->orphans > 0 ? block->blk->orphans : 2;
    int widows = block->blk && block->blk->widows > 0 ? block->blk->widows : 2;
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

    for (int li = 0; li < line_count; li++) {
        float line_offset = fragment_start_offset > 0.0f ? normal_line_offset : 0.0f;
        float visual_bottom = fragment_start_offset + line_slot * line_advance +
            line_offset + visual_height;
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
        float new_y = block->y + row_y + fragment_start_offset +
            line_slot * line_advance + line_offset;
        lines[li].rect->x = lines[li].original_x + column_index * pitch;
        lines[li].rect->y = new_y;
        lines[li].new_y = new_y;
        lines[li].fragment_index = current_fragment;

        float used_height = row_y + fragment_start_offset + line_slot * line_advance +
            line_offset + visual_height;
        if (used_height > max_used_height) max_used_height = used_height;
        line_slot++;
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
        if (broke_before_run || line.fragment_index == start_fragment) {
            breaks[bi].view->x = breaks[bi].original_x + column_index * pitch;
            breaks[bi].view->y = line.new_y;
        } else {
            breaks[bi].view->x = breaks[bi].original_x;
            breaks[bi].view->y = breaks[bi].original_y - first_line_box_y;
        }
    }

    log_debug("[MULTICOL] Projected mixed direct inline run: lines=%d start_offset=%.1f fragments=%d max_used=%.1f",
              line_count, first_line_box_y, lines[line_count - 1].fragment_index - start_fragment + 1,
              max_used_height);
    return max_used_height;
}

static int multicol_simulate_column_count(
    float* item_heights,
    bool* break_before,
    bool* break_after,
    int item_count,
    float target_height
) {
    if (item_count <= 0 || target_height <= 0) return 1;

    int fragment_count = 1;
    float fragment_used = 0;
    for (int i = 0; i < item_count; i++) {
        if (break_before[i] && fragment_used > 0) {
            fragment_count++;
            fragment_used = 0;
        }

        float item_height = item_heights[i];
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
    bool* break_before,
    bool* break_after,
    int item_count,
    int column_count,
    float fallback_target,
    float group_total_height
) {
    if (!block || !block->multicol || item_count <= 0 || column_count <= 1) {
        return fallback_target;
    }
    if (block->multicol->fill == COLUMN_FILL_AUTO) {
        return fallback_target;
    }

    float lower = fallback_target > 0 ? fallback_target : 1;
    float upper = group_total_height;
    if (upper < lower) upper = lower;

    float best = upper;
    for (int step = 0; step < 12; step++) {
        float mid = floorf((lower + upper) * 0.5f);
        if (mid <= 0) mid = (lower + upper) * 0.5f;
        if (mid < 1) mid = 1;

        int fragments = multicol_simulate_column_count(
            item_heights, break_before, break_after, item_count, mid);
        if (fragments <= column_count) {
            best = mid;
            upper = mid;
        } else {
            lower = mid + 1;
        }
        if (upper <= lower) break;
    }

    log_debug("[MULTICOL] Balance search: fallback=%.1f total=%.1f best=%.1f items=%d columns=%d",
              fallback_target, group_total_height, best, item_count, column_count);
    return best;
}

static bool multicol_group_wraps_rows(ViewBlock* container);

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

    group->fragment_count = 1;
    group->column_count = column_count;
    group->column_width = column_width;
    group->column_gap = gap;
    group->inline_origin = inline_origin;
    group->row_gap = row_gap;
    group->target_height = target_height;
    group->group_used_height = 0;
    group->wraps_rows = multicol_group_wraps_rows(container);

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

static void multicol_cursor_init(FragmentedFlowCursor* cursor, ColumnGroup* group) {
    cursor->group = group;
    cursor->current_fragment = 0;
    cursor->block_offset = 0;
}

static bool multicol_group_wraps_rows(ViewBlock* container) {
    if (!container || !container->multicol) return false;
    float fragment_height = multicol_content_box_height_limit(container);
    if (fragment_height <= 0) return false;
    if (container->multicol->wrap == COLUMN_WRAP_WRAP) return true;
    if (container->multicol->wrap == COLUMN_WRAP_AUTO &&
        (!container->blk || container->blk->given_height < 0)) {
        return true;
    }
    return false;
}

static bool multicol_group_should_break(
    ViewBlock* container,
    FragmentedFlowCursor* cursor,
    float item_height
) {
    if (!container || !container->multicol || !cursor || !cursor->group) return false;
    ColumnGroup* group = cursor->group;
    if (group->column_count <= 1) return false;
    if (cursor->block_offset <= 0) return false;
    ColumnFragment* fragment = &group->fragments[cursor->current_fragment];
    if (fragment->column_index >= group->column_count - 1 && !group->wraps_rows) return false;

    if (container->multicol->fill == COLUMN_FILL_AUTO) {
        if (group->target_height < 0) return false;
        return cursor->block_offset + item_height > group->target_height;
    }

    return cursor->block_offset + item_height > group->target_height;
}

static void multicol_cursor_advance_fragment(FragmentedFlowCursor* cursor) {
    if (!cursor || !cursor->group) return;

    ColumnGroup* group = cursor->group;
    ColumnFragment* current = &group->fragments[cursor->current_fragment];
    current->used_height = cursor->block_offset;
    float fragment_extent = current->y + cursor->block_offset;
    if (fragment_extent > group->group_used_height) {
        group->group_used_height = fragment_extent;
    }

    int next_column = current->column_index;
    int next_row = current->row_index;
    if (current->column_index >= group->column_count - 1 && group->wraps_rows) {
        next_column = 0;
        next_row++;
    } else {
        next_column++;
    }
    cursor->block_offset = 0;

    if (group->fragment_count < MAX_MULTICOL_BLOCKS) {
        ColumnFragment* fragment = &group->fragments[group->fragment_count];
        fragment->fragment_index = group->fragment_count;
        fragment->column_index = next_column;
        fragment->row_index = next_row;
        fragment->x = group->inline_origin +
                      next_column * (group->column_width + group->column_gap);
        fragment->y = next_row * (group->target_height + group->row_gap);
        fragment->width = group->column_width;
        fragment->target_height = group->target_height;
        fragment->used_height = 0;
        cursor->current_fragment = group->fragment_count;
        group->fragment_count++;
    }
}

static ColumnFragment* multicol_cursor_current_fragment(FragmentedFlowCursor* cursor) {
    if (!cursor || !cursor->group || cursor->current_fragment < 0 ||
        cursor->current_fragment >= cursor->group->fragment_count) {
        return NULL;
    }
    return &cursor->group->fragments[cursor->current_fragment];
}

static void multicol_cursor_place_block(
    FragmentedFlowCursor* cursor,
    ViewBlock* child,
    float group_y
) {
    ColumnFragment* fragment = multicol_cursor_current_fragment(cursor);
    if (!fragment || !child) return;
    child->x = fragment->x;
    child->y = group_y + fragment->y + cursor->block_offset;
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

    float remaining = flow_height;
    while (remaining > 0.0f) {
        ColumnFragment* fragment = multicol_cursor_current_fragment(cursor);
        if (!fragment) return;
        float available = target_height - cursor->block_offset;
        if (available <= 0.0f) {
            if (fragment->column_index >= group->column_count - 1 && !group->wraps_rows) {
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
        if (fragment->column_index >= group->column_count - 1 && !group->wraps_rows) {
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
        float fragment_extent = fragment->y + cursor->block_offset;
        if (fragment_extent > group->group_used_height) {
            group->group_used_height = fragment_extent;
        }
    }
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
    if (!block->multicol) {
        log_error("[MULTICOL] layout_multicol_content called without multicol prop");
        return;
    }

    log_debug("[MULTICOL] Starting layout for %s", block->node_name());

    // Calculate available width (content box)
    float available_width = lycon->block.content_width;

    // Calculate column dimensions
    int column_count;
    float column_width, gap;
    calculate_multicol_dimensions(block->multicol, available_width,
                                   multicol_normal_gap_size(block),
                                   &column_count, &column_width, &gap);

    // Store computed values for rendering
    block->multicol->computed_column_count = column_count;
    block->multicol->computed_column_width = column_width;
    block->multicol->computed_used_column_count = 1;

    // If only 1 column, fall back to normal flow
    if (column_count <= 1) {
        log_debug("[MULTICOL] Single column, falling back to normal flow");
        block->multicol->computed_column_count = 1;

        // Run normal flow layout
        DomNode* child = block->first_child;
        if (child) {
            prescan_and_layout_floats(lycon, child, block);
            do {
                layout_flow_node(lycon, child);
                child = child->next_sibling;
            } while (child);
            if (!lycon->line.is_line_start) {
                line_break(lycon);
            }
        }

        float max_flow_extent = lycon->block.advance_y;
        View* placed = block->first_placed_child();
        while (placed) {
            if (placed->is_block()) {
                ViewBlock* child_block = lam::view_require_block(placed);
                multicol_clear_layout_fragments(child_block);
                if (!multicol_is_out_of_flow(child_block) &&
                    multicol_has_direct_spanner_child(child_block)) {
                    float flow_height = multicol_split_child_around_spanners(
                        lycon, block, child_block, 1, available_width, gap);
                    float child_extent = child_block->y + flow_height;
                    if (child_extent > max_flow_extent) {
                        max_flow_extent = child_extent;
                    }
                    log_debug("[MULTICOL] Single-column split nested spanner child %s, flow=%.1f",
                              child_block->node_name(), flow_height);
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

    log_debug("[MULTICOL] Layout with %d columns, width=%.1f, gap=%.1f",
              column_count, column_width, gap);

    // =========================================================================
    // Phase 1: Layout all content within single column width
    // =========================================================================

    // Save original line bounds
    float orig_line_left = lycon->line.left;
    float orig_line_right = lycon->line.right;
    float orig_content_width = lycon->block.content_width;
    AvailableSize orig_available_width = lycon->available_space.width;

    // Constrain layout to column width
    lycon->block.content_width = column_width;
    lycon->line.left = 0;
    lycon->line.right = column_width;
    // child sizing and its layout-cache key must use the fragmentainer width,
    // otherwise cached full-container widths leak into each column.
    lycon->available_space.width = AvailableSize::make_definite(column_width);

    // Layout children normally within column width
    DomNode* child = block->first_child;
    if (child) {
        prescan_and_layout_floats(lycon, child, block);
        do {
            layout_flow_node(lycon, child);
            child = child->next_sibling;
        } while (child);
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        }
    }

    // Get total content height after layout
    float total_content_height = lycon->block.advance_y;
    log_debug("[MULTICOL] Total content height after layout: %.1f", total_content_height);

    // Restore original widths (for container sizing)
    lycon->line.left = orig_line_left;
    lycon->line.right = orig_line_right;
    lycon->block.content_width = orig_content_width;
    lycon->available_space.width = orig_available_width;

    // If content fits in one column, no redistribution needed
    if (total_content_height <= 0) {
        log_debug("[MULTICOL] No content to distribute");
        return;
    }

    // =========================================================================
    // Phase 2: Collect block children and identify column groups
    // =========================================================================
    // CSS Multicol §7.1: Spanners divide content into "column groups".
    // Each column group is balanced independently.

    struct BlockInfo {
        ViewBlock* block;
        float height;       // Total height including margins
        float orig_y;       // Original Y position
        bool spans_all;     // column-span: all
        bool break_before_column;
        bool break_after_column;
    };
    // MAX_MULTICOL_BLOCKS = 1024 → BlockInfo[] ≈ 32 KiB; move to scratch arena (LIFO).
    BlockInfo* blocks = (BlockInfo*)scratch_calloc(&lycon->scratch,
        MAX_MULTICOL_BLOCKS * sizeof(BlockInfo));
    if (!blocks) {
        log_error("[MULTICOL] Failed to allocate blocks array");
        return;
    }
    int block_count = 0;

    child = block->first_child;
    while (child) {
        if (child->is_element()) {
            DomElement* child_elem = lam::dom_require<DOM_NODE_ELEMENT>(child);
            ViewBlock* child_block = lam::view_as_block(child);

            if (child_block && (child_block->view_type == RDT_VIEW_BLOCK ||
                child_block->view_type == RDT_VIEW_INLINE_BLOCK ||
                child_block->view_type == RDT_VIEW_TEXT)) {

                multicol_clear_layout_fragments(child_block);

                if (multicol_is_out_of_flow(child_block)) {
                    log_debug("[MULTICOL] Skipping out-of-flow child %s in column distribution",
                              child_block->node_name());
                    child = child->next_sibling;
                    continue;
                }

                float block_height = child_block->height;
                if (child_block->bound) {
                    block_height += child_block->bound->margin.top +
                                    child_block->bound->margin.bottom;
                }

                bool spans_all = child_elem->multicol &&
                                 child_elem->multicol->span == COLUMN_SPAN_ALL;

                if (block_count < MAX_MULTICOL_BLOCKS) {
                    blocks[block_count].block = child_block;
                    blocks[block_count].height = block_height;
                    blocks[block_count].orig_y = child_block->y;
                    blocks[block_count].spans_all = spans_all;
                    blocks[block_count].break_before_column =
                        child_block->blk && multicol_forces_column_break(child_block->blk->break_before);
                    blocks[block_count].break_after_column =
                        child_block->blk && multicol_forces_column_break(child_block->blk->break_after);
                    block_count++;
                }

                log_debug("[MULTICOL] Block %s: height=%.1f, y=%.1f, spans_all=%d",
                          child_block->node_name(), block_height, child_block->y, spans_all);
            }
        }
        child = child->next_sibling;
    }

    if (block_count == 0) {
        // No block children — content is inline-only (text lines).
        // Redistribute TextRects across columns based on balanced height.
        log_debug("[MULTICOL] No block children; redistributing inline text across columns");

        // Collect all TextRects from all text children
        struct LineRect {
            TextRect* rect;
            DomText* text_node;  // owning text node
            float line_y;        // original y
            float new_y;         // redistributed y
            float line_height;   // height of this rect
        };
        LineRect lines[512];
        int line_count = 0;

        child = block->first_child;
        while (child) {
            if (child->node_type == DOM_NODE_TEXT) {
                DomText* tnode = lam::dom_require<DOM_NODE_TEXT>(child);
                TextRect* tr = tnode->rect;
                while (tr && line_count < 512) {
                    lines[line_count].rect = tr;
                    lines[line_count].text_node = tnode;
                    lines[line_count].line_y = tr->y;
                    lines[line_count].new_y = tr->y;
                    lines[line_count].line_height = tr->height;
                    line_count++;
                    tr = tr->next;
                }
            }
            child = child->next_sibling;
        }

        if (line_count == 0) {
            log_debug("[MULTICOL] No text rects to distribute");
            return;
        }

        // Calculate fragmentainer height for this inline-only column group.
        float balanced_height = ceilf(total_content_height / column_count);
        float target_height = multicol_group_target_height(block, balanced_height, total_content_height);

        log_debug("[MULTICOL] Inline redistribution: %d rects, total_h=%.1f, target_h=%.1f",
                  line_count, total_content_height, target_height);

        // Distribute rects across columns
        int current_col = 0;
        float col_y = 0;           // y offset within current column
        float col_start_y = 0;     // the original y of the first line assigned to the current column
        int col_start_line = 0;
        bool col_started = false;
        float max_col_height = 0;
        int orphans = block->blk && block->blk->orphans > 0 ? block->blk->orphans : 2;
        int widows = block->blk && block->blk->widows > 0 ? block->blk->widows : 2;

        for (int li = 0; li < line_count; li++) {
            LineRect& lr = lines[li];

            // Relative y within original single-column layout
            float rel_y = lr.line_y - lines[0].line_y;

            // Check if this line should go to the next column.
            // Break only when including this line would overshoot AND
            // excluding it is closer to balanced than including it.
            // This matches browser behavior of preferring more content
            // in earlier columns when lines are indivisible.
            bool should_break = false;
            if (col_started && current_col < column_count - 1) {
                float col_h_with = rel_y - col_start_y + lr.line_height;
                if (col_h_with > target_height) {
                    float col_h_without = rel_y - col_start_y;
                    float overshoot = col_h_with - target_height;
                    float undershoot = target_height - col_h_without;
                    should_break = block->multicol->fill == COLUMN_FILL_AUTO ||
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
                    if (col_h_without > max_col_height) max_col_height = col_h_without;
                    current_col++;
                    col_start_y = rel_y;
                    col_start_line = li;
                    log_debug("[MULTICOL] Inline column break -> column %d at rel_y=%.1f", current_col, rel_y);
                }
            }
            col_started = true;

            // Reposition: shift x by column offset, reset y within column
            float col_x_offset = current_col * (column_width + gap);
            lr.rect->x += col_x_offset;
            lr.new_y = lines[0].line_y + (rel_y - col_start_y);
            lr.rect->y = lr.new_y;

            col_y = (rel_y - col_start_y) + lr.line_height;
        }
        if (col_y > max_col_height) max_col_height = col_y;

        // Update block height to the max column height (not total content height)
        float final_height = max_col_height;

        // Update text node bounds (x, y, width, height)
        child = block->first_child;
        while (child) {
            if (child->node_type == DOM_NODE_TEXT) {
                DomText* tnode = lam::dom_require<DOM_NODE_TEXT>(child);
                // Recalculate text node bounding box from its rects
                float min_x = 1e9f, min_y = 1e9f, max_x = 0, max_y_val = 0;
                TextRect* tr = tnode->rect;
                while (tr) {
                    if (tr->x < min_x) min_x = tr->x;
                    if (tr->y < min_y) min_y = tr->y;
                    float rx = tr->x + tr->width;
                    float ry = tr->y + tr->height;
                    if (rx > max_x) max_x = rx;
                    if (ry > max_y_val) max_y_val = ry;
                    tr = tr->next;
                }
                // DomText doesn't have x/y/width/height directly, but
                // the parent block uses content_height from advance_y
            } else if (child->view_type == RDT_VIEW_BR) {
                View* br = (View*)child;
                for (int li = 0; li < line_count; li++) {
                    if (fabsf(br->y - lines[li].line_y) <= 1.0f) {
                        br->y = lines[li].new_y;
                        break;
                    }
                }
            }
            child = child->next_sibling;
        }

        // Set block height: use CSS given height if specified, otherwise balanced column height
        float total_height = final_height;
        if (block->blk && block->blk->given_height >= 0) {
            total_height = block->blk->given_height;
        } else if (block->multicol->fill == COLUMN_FILL_AUTO &&
                   multicol_content_box_height_limit(block) >= 0 &&
                   final_height > multicol_content_box_height_limit(block)) {
            total_height = multicol_content_box_height_limit(block);
        } else if (block->bound) {
            total_height += layout_box_metrics(block).pad_border_v;
        }
        block->height = total_height;
        block->content_height = final_height + (block->bound ? block->bound->padding.bottom : 0);
        block->multicol->computed_used_column_count = current_col + 1;

        float content_start_y = 0;
        if (block->bound) {
            if (block->bound->border) content_start_y += block->bound->border->width.top;
            content_start_y += block->bound->padding.top;
        }
        lycon->block.advance_y = content_start_y + final_height;

        log_debug("[MULTICOL] Inline redistribution complete: %d columns, max_col_h=%.1f, block_h=%.1f",
                  column_count, max_col_height, block->height);
        return;
    }

    // =========================================================================
    // Phase 3: Assign blocks to columns, balancing each column group
    // =========================================================================
    // Process blocks in groups separated by spanners. For each group of
    // non-spanner blocks, compute a balanced height and distribute across
    // columns. Spanners are placed at full container width between groups.

    // CSS Box 4 §3.1: margin-trim:block-end — trim the last in-flow child's
    // block-end margin. We handle this here since layout_block_inner_content
    // returns early for multicol containers.
    bool trim_block_end = block->blk && (block->blk->margin_trim & MARGIN_TRIM_BLOCK_END);
    if (trim_block_end && block_count > 0) {
        ViewBlock* last_block = blocks[block_count - 1].block;
        if (last_block->bound && last_block->bound->margin.bottom != 0) {
            log_debug("[MULTICOL] margin-trim block-end: trimming margin.bottom=%.1f on last child %s",
                      last_block->bound->margin.bottom, last_block->node_name());
            float old_mb = last_block->bound->margin.bottom;
            last_block->bound->margin.bottom = 0;
            // Update the cached height in blocks array
            blocks[block_count - 1].height -= old_mb;
        }
    }

    float content_start_y = 0.0f;
    if (block->bound) {
        if (block->bound->border) {
            content_start_y += block->bound->border->width.top;
        }
        content_start_y += block->bound->padding.top;
    }

    float max_column_height = 0;  // running Y offset for the entire container
    float prev_margin_bottom = 0; // for margin collapsing between consecutive spanners

    // Per-group scratch buffers shared across all column groups: ~10 KiB total.
    // ColumnFragment[MAX_MULTICOL_BLOCKS] ≈ 32 KiB — backs ColumnGroup::fragments.
    float* group_heights_buf = (float*)scratch_alloc(&lycon->scratch,
        MAX_MULTICOL_BLOCKS * sizeof(float));
    bool* group_break_before_buf = (bool*)scratch_alloc(&lycon->scratch,
        MAX_MULTICOL_BLOCKS * sizeof(bool));
    bool* group_break_after_buf = (bool*)scratch_alloc(&lycon->scratch,
        MAX_MULTICOL_BLOCKS * sizeof(bool));
    ColumnFragment* fragments_buf = (ColumnFragment*)scratch_calloc(&lycon->scratch,
        MAX_MULTICOL_BLOCKS * sizeof(ColumnFragment));
    if (!group_heights_buf || !group_break_before_buf || !group_break_after_buf || !fragments_buf) {
        log_error("[MULTICOL] Failed to allocate group scratch buffers");
        if (fragments_buf) scratch_free(&lycon->scratch, fragments_buf);
        if (group_break_after_buf) scratch_free(&lycon->scratch, group_break_after_buf);
        if (group_break_before_buf) scratch_free(&lycon->scratch, group_break_before_buf);
        if (group_heights_buf) scratch_free(&lycon->scratch, group_heights_buf);
        scratch_free(&lycon->scratch, blocks);
        return;
    }

    int i = 0;
    while (i < block_count) {
        // --- Spanner: place at full width ---
        if (blocks[i].spans_all) {
            ViewBlock* child_block = blocks[i].block;
            float spanner_margin_top = child_block->bound ? child_block->bound->margin.top : 0;
            float spanner_margin_bottom = child_block->bound ? child_block->bound->margin.bottom : 0;

            // CSS 2.1 §8.3.1: Collapse margin between previous element and
            // this spanner. Use max of the two positive margins (simplified —
            // negative margin handling omitted for now).
            float collapsed_margin = MAX_FLOAT(prev_margin_bottom, spanner_margin_top);
            // Subtract the already-accounted prev_margin_bottom from max_column_height
            max_column_height -= prev_margin_bottom;
            max_column_height += collapsed_margin;

            // spanners share the multicol content-box origin with column groups.
            child_block->x = orig_line_left;
            child_block->y = content_start_y + max_column_height;
            child_block->width = available_width;

            max_column_height += child_block->height + spanner_margin_bottom;
            prev_margin_bottom = spanner_margin_bottom;

            log_debug("[MULTICOL] Spanner %s at y=%.1f, margin_top=%.1f, margin_bottom=%.1f, collapsed=%.1f",
                      child_block->node_name(), child_block->y, spanner_margin_top, spanner_margin_bottom, collapsed_margin);
            i++;
            continue;
        }

        // --- Column group: collect consecutive non-spanner blocks ---
        int group_start = i;
        float group_total_height = 0;
        while (i < block_count && !blocks[i].spans_all) {
            group_total_height += blocks[i].height;
            i++;
        }
        int group_end = i;  // exclusive

        // Calculate target fragmentainer height for this column group
        float group_balanced = group_total_height / column_count;
        // CSS Multicol §7.2: column-fill:balance distributes content evenly.
        // Use ceiling to avoid underfilling the last column.
        group_balanced = ceilf(group_balanced);
        float group_target = multicol_group_target_height(block, group_balanced, group_total_height);
        // Reuse the outer-scope scratch buffers (allocated once before the i-loop).
        float* group_heights = group_heights_buf;
        bool* group_break_before = group_break_before_buf;
        bool* group_break_after = group_break_after_buf;
        int group_item_count = 0;
        for (int gi = group_start; gi < group_end && group_item_count < MAX_MULTICOL_BLOCKS; gi++) {
            group_heights[group_item_count] = blocks[gi].height;
            group_break_before[group_item_count] = blocks[gi].break_before_column;
            group_break_after[group_item_count] = blocks[gi].break_after_column;
            group_item_count++;
        }
        group_target = multicol_balanced_target_search(
            block, group_heights, group_break_before, group_break_after,
            group_item_count, column_count, group_target, group_total_height);

        log_debug("[MULTICOL] Column group [%d..%d): total_h=%.1f, target_h=%.1f",
                  group_start, group_end, group_total_height, group_target);

        // Distribute this group's blocks across columns
        ColumnGroup group;
        FragmentedFlowCursor cursor;
        group.fragments = fragments_buf;  // Backing store from outer scratch alloc.
        multicol_group_init(&group, block, group_target, column_count,
                            column_width, gap, orig_line_left);
        multicol_cursor_init(&cursor, &group);

        for (int j = group_start; j < group_end; j++) {
            BlockInfo& info = blocks[j];
            ViewBlock* cb = info.block;
            float placed_height = info.height;
            bool child_can_fragment =
                multicol_should_fragment_monolithic_child(block, cb, info.height, group_target) ||
                info.height > group_target;

            // Check if we should break to next column
            if (info.break_before_column && cursor.block_offset > 0) {
                multicol_cursor_advance_fragment(&cursor);
                ColumnFragment* fragment = multicol_cursor_current_fragment(&cursor);
                log_debug("[MULTICOL] Forced column break before %s -> column %d",
                          cb->node_name(), fragment ? fragment->column_index : -1);
            } else if (!child_can_fragment && multicol_group_should_break(block, &cursor, info.height)) {
                multicol_cursor_advance_fragment(&cursor);
                ColumnFragment* fragment = multicol_cursor_current_fragment(&cursor);
                log_debug("[MULTICOL] Column break -> column %d at y=%.1f",
                          fragment ? fragment->column_index : -1,
                          fragment ? fragment->y : 0);
            }

            multicol_cursor_place_block(
                &cursor, cb, content_start_y + max_column_height);

            if (multicol_has_direct_spanner_child(cb)) {
                placed_height = multicol_split_child_around_spanners(
                    lycon, block, cb, column_count, column_width, gap);
                log_debug("[MULTICOL] Split nested spanner child %s, height=%.1f",
                          cb->node_name(), placed_height);
            } else if (child_can_fragment) {
                int used_columns = 1;
                int fragment_count = group_target > 0.0f ?
                    (int)ceilf(info.height / group_target) : 1; // INT_CAST_OK: fragment count from positive heights
                if (fragment_count < 1) fragment_count = 1;
                float flow_height = multicol_text_box_trim_fragmented_flow_height(
                    cb, info.height, group_target, fragment_count, cursor.block_offset);
                placed_height = multicol_fragmented_child_union(
                    lycon, block, cb, info.height, group_target, column_count, column_width, gap, &used_columns);
                if (used_columns > group.fragment_count) {
                    group.fragment_count = used_columns;
                }
                log_debug("[MULTICOL] Fragmented monolithic %s into %d columns, union height=%.1f",
                          cb->node_name(), used_columns, placed_height);
                multicol_cursor_advance_fragmented_block(&cursor, flow_height);
                placed_height = 0.0f;
            }

            ColumnFragment* fragment = multicol_cursor_current_fragment(&cursor);
            log_debug("[MULTICOL] Placed %s in column %d at (%.1f, %.1f)",
                      cb->node_name(), fragment ? fragment->column_index : -1, cb->x, cb->y);

            multicol_cursor_advance_block(&cursor, placed_height);
            if (info.break_after_column && j + 1 < group_end) {
                multicol_cursor_advance_fragment(&cursor);
                ColumnFragment* next_fragment = multicol_cursor_current_fragment(&cursor);
                log_debug("[MULTICOL] Forced column break after %s -> column %d",
                          cb->node_name(), next_fragment ? next_fragment->column_index : -1);
            }
        }
        multicol_group_finish(&group, &cursor);
        int used_column_count = 1;
        for (int fi = 0; fi < group.fragment_count; fi++) {
            int candidate = group.fragments[fi].column_index + 1;
            if (candidate > used_column_count) used_column_count = candidate;
        }
        if (used_column_count > block->multicol->computed_used_column_count) {
            block->multicol->computed_used_column_count = used_column_count;
        }
        max_column_height += group.group_used_height;
        prev_margin_bottom = 0;  // column group doesn't have trailing margin
    }

    float mixed_inline_target = multicol_group_target_height(
        block, ceilf(total_content_height / column_count), total_content_height);
    float mixed_inline_height = multicol_project_mixed_direct_inline_content(
        block, column_count, column_width, gap, mixed_inline_target);
    if (mixed_inline_height > max_column_height) {
        max_column_height = mixed_inline_height;
    }

    // Set block height: use CSS given height if specified, otherwise computed
    float total_height = max_column_height;
    if (block->blk && block->blk->given_height >= 0) {
        total_height = block->blk->given_height;
    } else {
        if (block->bound) {
            total_height += layout_box_metrics(block).pad_border_v;
        }
    }
    block->height = total_height;
    block->content_height = max_column_height + (block->bound ? block->bound->padding.bottom : 0);
    multicol_apply_positioned_fragment_anchors(lycon, block);
    multicol_finalize_fragmented_inline_continuations(static_cast<View*>(block));

    // Update layout context's advance_y to reflect actual height
    lycon->block.advance_y = content_start_y + max_column_height;

    log_debug("[MULTICOL] Final layout: %d columns, max height=%.1f, block height=%.1f",
              column_count, max_column_height, block->height);

    // LIFO free in reverse order of allocation.
    scratch_free(&lycon->scratch, fragments_buf);
    scratch_free(&lycon->scratch, group_break_after_buf);
    scratch_free(&lycon->scratch, group_break_before_buf);
    scratch_free(&lycon->scratch, group_heights_buf);
    scratch_free(&lycon->scratch, blocks);
}
