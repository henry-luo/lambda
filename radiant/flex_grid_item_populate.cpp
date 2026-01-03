/**
 * flex_grid_item_populate.cpp - Implementation of FlexGridItem population helpers
 */

#include "flex_grid_item_populate.hpp"
#include "layout.hpp"
#include "grid.hpp"
#include "../lib/log.h"
#include <cfloat>
#include <cmath>

namespace radiant {

// ============================================================================
// Common Property Extraction
// ============================================================================

RectF extract_padding(ViewElement* elem) {
    RectF result = rect_f_zero();
    if (!elem || !elem->bound) return result;

    result.top = elem->bound->padding.top;
    result.right = elem->bound->padding.right;
    result.bottom = elem->bound->padding.bottom;
    result.left = elem->bound->padding.left;

    return result;
}

RectF extract_border(ViewElement* elem) {
    RectF result = rect_f_zero();
    if (!elem || !elem->bound || !elem->bound->border) return result;

    result.top = elem->bound->border->width.top;
    result.right = elem->bound->border->width.right;
    result.bottom = elem->bound->border->width.bottom;
    result.left = elem->bound->border->width.left;

    return result;
}

void extract_margins(
    FlexGridItem* item,
    ViewElement* elem,
    float container_main_size,
    float container_cross_size,
    bool is_row
) {
    item->margin = rect_f_zero();
    item->margin_top_is_auto = false;
    item->margin_right_is_auto = false;
    item->margin_bottom_is_auto = false;
    item->margin_left_is_auto = false;

    if (!elem || !elem->bound) return;

    Margin& m = elem->bound->margin;

    // Check auto margins and set flags
    if (m.top_type == CSS_VALUE_AUTO) {
        item->margin_top_is_auto = true;
        item->margin.top = 0.0f;
    } else {
        item->margin.top = std::isnan(m.top) ? 0.0f : m.top;
    }

    if (m.right_type == CSS_VALUE_AUTO) {
        item->margin_right_is_auto = true;
        item->margin.right = 0.0f;
    } else {
        item->margin.right = std::isnan(m.right) ? 0.0f : m.right;
    }

    if (m.bottom_type == CSS_VALUE_AUTO) {
        item->margin_bottom_is_auto = true;
        item->margin.bottom = 0.0f;
    } else {
        item->margin.bottom = std::isnan(m.bottom) ? 0.0f : m.bottom;
    }

    if (m.left_type == CSS_VALUE_AUTO) {
        item->margin_left_is_auto = true;
        item->margin.left = 0.0f;
    } else {
        item->margin.left = std::isnan(m.left) ? 0.0f : m.left;
    }
}

void extract_constraints(
    FlexGridItem* item,
    ViewElement* elem,
    float container_width,
    float container_height
) {
    item->min_size = optional_size_none();
    item->max_size = optional_size_none();
    item->size = optional_size_none();

    if (!elem) return;

    // Extract from BlockProp if available
    BlockProp* bp = elem->blk;
    if (!bp) return;

    // Size (width/height)
    if (bp->given_width >= 0) {
        item->size.width = bp->given_width;
        item->size.has_width = true;
    }
    if (bp->given_height >= 0) {
        item->size.height = bp->given_height;
        item->size.has_height = true;
    }

    // Min constraints
    if (bp->given_min_width >= 0) {
        item->min_size.width = bp->given_min_width;
        item->min_size.has_width = true;
    }
    if (bp->given_min_height >= 0) {
        item->min_size.height = bp->given_min_height;
        item->min_size.has_height = true;
    }

    // Max constraints
    if (bp->given_max_width >= 0 && bp->given_max_width < FLT_MAX) {
        item->max_size.width = bp->given_max_width;
        item->max_size.has_width = true;
    }
    if (bp->given_max_height >= 0 && bp->given_max_height < FLT_MAX) {
        item->max_size.height = bp->given_max_height;
        item->max_size.has_height = true;
    }
}

void copy_intrinsic_cache(
    FlexGridItem* item,
    FlexItemProp* fi
) {
    item->intrinsic_cache = intrinsic_sizes_cache_empty();

    if (!fi) return;

    if (fi->has_intrinsic_width) {
        item->intrinsic_cache.min_content_width = fi->intrinsic_width.min_content;
        item->intrinsic_cache.max_content_width = fi->intrinsic_width.max_content;
        item->intrinsic_cache.valid = true;
    }

    if (fi->has_intrinsic_height) {
        item->intrinsic_cache.min_content_height = fi->intrinsic_height.min_content;
        item->intrinsic_cache.max_content_height = fi->intrinsic_height.max_content;
        item->intrinsic_cache.valid = true;
    }
}

// ============================================================================
// Flex Item Population
// ============================================================================

void flex_grid_item_from_flex_prop(
    FlexGridItem* item,
    ViewBlock* view,
    FlexContainerLayout* flex_layout,
    bool is_row
) {
    flex_grid_item_init(item);

    if (!view) return;

    // Set node references
    item->node = (DomElement*)view;
    item->view = view;

    ViewElement* elem = (ViewElement*)view;

    // Extract flex properties from FlexItemProp
    FlexItemProp* fi = elem->fi;
    if (fi) {
        item->flex_grow = fi->flex_grow;
        item->flex_shrink = fi->flex_shrink;
        item->flex_basis = fi->flex_basis;  // -1 for auto
        item->align_self = fi->align_self;
        item->order = fi->order;

        if (fi->aspect_ratio > 0) {
            item->aspect_ratio = optional_float_some(fi->aspect_ratio);
        }

        // Copy auto margin flags from fi
        item->margin_top_is_auto = fi->is_margin_top_auto;
        item->margin_right_is_auto = fi->is_margin_right_auto;
        item->margin_bottom_is_auto = fi->is_margin_bottom_auto;
        item->margin_left_is_auto = fi->is_margin_left_auto;

        // Copy intrinsic cache
        copy_intrinsic_cache(item, fi);
    } else {
        // Defaults
        item->flex_grow = 0.0f;
        item->flex_shrink = 1.0f;
        item->flex_basis = -1.0f;  // auto
        item->align_self = 0;      // auto (inherit from container)
        item->order = 0;
    }

    // Extract padding and border
    item->padding = extract_padding(elem);
    item->border = extract_border(elem);

    // Extract margins (uses bounds, handles auto)
    float container_main = is_row ? flex_layout->main_axis_size : flex_layout->cross_axis_size;
    float container_cross = is_row ? flex_layout->cross_axis_size : flex_layout->main_axis_size;
    extract_margins(item, elem, container_main, container_cross, is_row);

    // Extract size constraints
    extract_constraints(item, elem, flex_layout->main_axis_size, flex_layout->cross_axis_size);
}

void flex_grid_item_from_flex_view(
    FlexGridItem* item,
    ViewBlock* view,
    FlexContainerLayout* flex_layout
) {
    bool is_row = (flex_layout->direction == CSS_VALUE_ROW ||
                   flex_layout->direction == CSS_VALUE_ROW_REVERSE);
    flex_grid_item_from_flex_prop(item, view, flex_layout, is_row);
}

// ============================================================================
// Grid Item Population
// ============================================================================

void flex_grid_item_from_grid_prop(
    FlexGridItem* item,
    ViewBlock* view,
    GridContainerLayout* grid_layout
) {
    flex_grid_item_init(item);

    if (!view) return;

    // Set node references
    item->node = (DomElement*)view;
    item->view = view;

    ViewElement* elem = (ViewElement*)view;

    // Extract grid properties from GridItemProp
    GridItemProp* gi = elem->gi;
    if (gi) {
        item->row_start = gi->grid_row_start;
        item->row_end = gi->grid_row_end;
        item->col_start = gi->grid_column_start;
        item->col_end = gi->grid_column_end;

        item->row_start_is_span = gi->grid_row_start_is_span;
        item->row_end_is_span = gi->grid_row_end_is_span;
        item->col_start_is_span = gi->grid_column_start_is_span;
        item->col_end_is_span = gi->grid_column_end_is_span;

        item->align_self = gi->align_self_grid;
        item->justify_self = gi->justify_self;

        // Use computed placement if available (1-based line numbers)
        if (gi->computed_grid_row_start >= 1) {
            item->placed_row = gi->computed_grid_row_start;
            item->row_span = gi->computed_grid_row_end - gi->computed_grid_row_start;
        }
        if (gi->computed_grid_column_start >= 1) {
            item->placed_col = gi->computed_grid_column_start;
            item->col_span = gi->computed_grid_column_end - gi->computed_grid_column_start;
        }
    } else {
        // Defaults (auto placement)
        item->row_start = -1;
        item->row_end = -1;
        item->col_start = -1;
        item->col_end = -1;
        item->align_self = 0;
        item->justify_self = 0;
    }

    // Extract padding and border
    item->padding = extract_padding(elem);
    item->border = extract_border(elem);

    // Extract margins
    extract_margins(item, elem,
                   (float)grid_layout->container_width,
                   (float)grid_layout->container_height,
                   true);  // Grid uses row direction for margin main/cross

    // Extract size constraints
    extract_constraints(item, elem,
                       (float)grid_layout->container_width,
                       (float)grid_layout->container_height);
}

// ============================================================================
// Collection Helpers
// ============================================================================

/**
 * Check if an element should be skipped during flex item collection.
 * Skips absolutely positioned elements and display:none elements.
 */
static bool should_skip_flex_item(ViewBlock* child) {
    if (!child) return true;

    ViewElement* elem = (ViewElement*)child;

    // Skip absolutely/fixed positioned elements
    if (elem->position && elem->position->position != CSS_VALUE_STATIC &&
        elem->position->position != CSS_VALUE_RELATIVE) {
        return true;
    }

    // Skip display:none
    if (elem->display.outer == CSS_VALUE_NONE) {
        return true;
    }

    return false;
}

int32_t collect_flex_items_to_context(
    FlexGridContext* ctx,
    ViewBlock* container,
    FlexContainerLayout* flex_layout
) {
    if (!ctx || !container || !flex_layout) return 0;

    bool is_row = (flex_layout->direction == CSS_VALUE_ROW ||
                   flex_layout->direction == CSS_VALUE_ROW_REVERSE);
    ctx->is_row_direction = is_row;
    ctx->is_reversed = (flex_layout->direction == CSS_VALUE_ROW_REVERSE ||
                        flex_layout->direction == CSS_VALUE_COLUMN_REVERSE);
    ctx->is_wrap = (flex_layout->wrap != CSS_VALUE_NOWRAP);
    ctx->is_wrap_reverse = (flex_layout->wrap == CSS_VALUE_WRAP_REVERSE);

    // Copy alignment properties
    ctx->justify_content = flex_layout->justify;
    ctx->align_items = flex_layout->align_items;
    ctx->align_content = flex_layout->align_content;
    ctx->main_gap = is_row ? flex_layout->column_gap : flex_layout->row_gap;
    ctx->cross_gap = is_row ? flex_layout->row_gap : flex_layout->column_gap;

    // Iterate over direct children
    ViewElement* container_elem = (ViewElement*)container;
    int32_t count = 0;

    for (DomNode* child_node = container_elem->first_child; child_node; child_node = child_node->next_sibling) {
        ViewBlock* child = (ViewBlock*)child_node;
        if (should_skip_flex_item(child)) {
            continue;
        }

        // Ensure capacity
        flex_grid_context_ensure_item_capacity(ctx, ctx->item_count + 1);

        // Add item and populate
        FlexGridItem* item = flex_grid_context_add_item(ctx);
        item->source_order = count;
        flex_grid_item_from_flex_prop(item, child, flex_layout, is_row);

        count++;
    }

    log_debug("flex_collect: collected %d items to FlexGridContext", count);
    return count;
}

int32_t collect_grid_items_to_context(
    FlexGridContext* ctx,
    ViewBlock* container,
    GridContainerLayout* grid_layout
) {
    if (!ctx || !container || !grid_layout) return 0;

    // Grid is always row-direction for main/cross semantics
    ctx->is_row_direction = true;

    // Copy alignment properties
    ctx->justify_content = grid_layout->justify_content;
    ctx->align_content = grid_layout->align_content;
    ctx->justify_items = grid_layout->justify_items;
    ctx->align_items_grid = grid_layout->align_items;
    ctx->main_gap = grid_layout->column_gap;
    ctx->cross_gap = grid_layout->row_gap;

    // Iterate over direct children
    ViewElement* container_elem = (ViewElement*)container;
    int32_t count = 0;

    for (DomNode* child_node = container_elem->first_child; child_node; child_node = child_node->next_sibling) {
        ViewBlock* child = (ViewBlock*)child_node;
        ViewElement* child_elem = (ViewElement*)child;

        // Skip display:none
        if (child_elem->display.outer == CSS_VALUE_NONE) {
            continue;
        }

        // Note: Grid includes absolutely positioned children (they participate in grid placement)

        // Ensure capacity
        flex_grid_context_ensure_item_capacity(ctx, ctx->item_count + 1);

        // Add item and populate
        FlexGridItem* item = flex_grid_context_add_item(ctx);
        item->source_order = count;
        flex_grid_item_from_grid_prop(item, child, grid_layout);

        count++;
    }

    log_debug("grid_collect: collected %d items to FlexGridContext", count);
    return count;
}

} // namespace radiant
