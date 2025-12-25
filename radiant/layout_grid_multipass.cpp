#include "layout.hpp"
#include "layout_grid_multipass.hpp"
#include "grid.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "intrinsic_sizing.hpp"

extern "C" {
#include "../lib/log.h"
}

// Multi-pass grid layout implementation
// Follows the same pattern as layout_flex_multipass.cpp

// External function declarations
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon);
void layout_flow_node(LayoutContext* lycon, DomNode* node);
void line_init(LayoutContext* lycon, float left, float right);
void line_break(LayoutContext* lycon);
void layout_abs_block(LayoutContext* lycon, DomNode *elmt, ViewBlock* block, BlockContext *pa_block, Linebox *pa_line);
void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, CssEnum display);

// Forward declarations for static functions
static void layout_grid_item_final_content_multipass(LayoutContext* lycon, ViewBlock* grid_item);

// ============================================================================
// Main Entry Point
// ============================================================================

void layout_grid_content(LayoutContext* lycon, ViewBlock* grid_container) {
    if (!grid_container) return;

    log_enter();
    log_info("GRID LAYOUT START: container=%p (%s)", grid_container, grid_container->node_name());

    // Save parent grid context (for nested grids)
    GridContainerLayout* pa_grid = lycon->grid_container;

    // Initialize grid container
    init_grid_container(lycon, grid_container);

    // Note: Grid properties (grid-template-columns/rows) may not be populated in embed->grid
    // at this point if they haven't been resolved in resolve_css_style.cpp.
    // The grid algorithm will use defaults in this case.

    // ========================================================================
    // PASS 0: Style Resolution and View Initialization
    // ========================================================================
    log_info("=== GRID PASS 0: Style resolution and view initialization ===");
    int item_count = resolve_grid_item_styles(lycon, grid_container);
    log_info("=== GRID PASS 0 COMPLETE: %d items initialized ===", item_count);

    if (item_count == 0) {
        log_debug("No grid items found");
        cleanup_grid_container(lycon);
        lycon->grid_container = pa_grid;
        log_leave();
        return;
    }

    // ========================================================================
    // PASS 1: Content Measurement (for intrinsic track sizing)
    // ========================================================================
    log_info("=== GRID PASS 1: Content measurement ===");
    measure_grid_items(lycon, lycon->grid_container);
    log_info("=== GRID PASS 1 COMPLETE ===");

    // ========================================================================
    // PASS 2: Grid Algorithm Execution
    // ========================================================================
    log_info("=== GRID PASS 2: Grid algorithm execution ===");
    layout_grid_container(lycon, grid_container);
    log_info("=== GRID PASS 2 COMPLETE ===");

    // ========================================================================
    // PASS 3: Final Content Layout
    // ========================================================================
    log_info("=== GRID PASS 3: Final content layout ===");
    layout_final_grid_content(lycon, lycon->grid_container);
    log_info("=== GRID PASS 3 COMPLETE ===");

    // ========================================================================
    // Update container height based on grid content
    // ========================================================================
    GridContainerLayout* grid_layout = lycon->grid_container;
    if (grid_layout && grid_layout->computed_row_count > 0) {
        // Calculate total height from row sizes plus gaps
        float total_row_height = 0;
        for (int i = 0; i < grid_layout->computed_row_count; i++) {
            total_row_height += grid_layout->computed_rows[i].base_size;
        }
        // Add gaps between rows
        total_row_height += grid_layout->row_gap * (grid_layout->computed_row_count - 1);

        // Add padding and border
        float container_height = total_row_height;
        if (grid_container->bound) {
            container_height += grid_container->bound->padding.top + grid_container->bound->padding.bottom;
            if (grid_container->bound->border) {
                container_height += grid_container->bound->border->width.top +
                                   grid_container->bound->border->width.bottom;
            }
        }

        // Only update if container height is auto (not explicitly set)
        if (grid_container->height < container_height) {
            log_info("GRID: Updating container height from %.1f to %.1f (rows=%.1f, gaps=%.1f)",
                     grid_container->height, container_height, total_row_height,
                     grid_layout->row_gap * (grid_layout->computed_row_count - 1));
            grid_container->height = container_height;
        }
    }

    // ========================================================================
    // PASS 4: Absolute Positioned Children
    // ========================================================================
    log_info("=== GRID PASS 4: Absolute positioned children ===");
    layout_grid_absolute_children(lycon, grid_container);
    log_info("=== GRID PASS 4 COMPLETE ===");

    // Cleanup and restore parent context
    cleanup_grid_container(lycon);
    lycon->grid_container = pa_grid;

    log_info("GRID LAYOUT END: container=%p", grid_container);
    log_leave();
}

// ============================================================================
// Pass 0: Style Resolution and View Initialization
// ============================================================================

int resolve_grid_item_styles(LayoutContext* lycon, ViewBlock* grid_container) {
    log_enter();
    log_debug("Resolving styles for grid items in container %s", grid_container->node_name());

    int item_count = 0;
    DomNode* child = grid_container->first_child;

    while (child) {
        if (child->is_element()) {
            DomElement* elem = child->as_element();

            // Skip absolutely positioned and hidden items (they're not grid items)
            bool is_absolute = elem->position &&
                              (elem->position->position == CSS_VALUE_ABSOLUTE ||
                               elem->position->position == CSS_VALUE_FIXED);

            if (!is_absolute) {
                // Initialize the view with style resolution
                init_grid_item_view(lycon, child);
                item_count++;
                log_debug("Initialized grid item %d: %s", item_count, child->node_name());
            } else {
                log_debug("Skipping absolute positioned child: %s", child->node_name());
            }
        }
        child = child->next_sibling;
    }

    log_debug("Resolved styles for %d grid items", item_count);
    log_leave();
    return item_count;
}

void init_grid_item_view(LayoutContext* lycon, DomNode* child) {
    if (!child || !child->is_element()) return;

    log_debug("Initializing grid item view for %s", child->node_name());

    DomElement* elem = child->as_element();

    // Resolve and store display value for this element
    // This is crucial for detecting nested grid/flex containers
    elem->display = resolve_display_value((void*)child);
    log_debug("Grid item display: outer=%d, inner=%d", elem->display.outer, elem->display.inner);

    // Set up the view type based on display
    // Grid items are blockified - treat as block
    elem->view_type = RDT_VIEW_BLOCK;

    // Initialize dimensions (will be set by grid algorithm)
    elem->x = 0;
    elem->y = 0;
    elem->width = 0;
    elem->height = 0;

    // Force boundary properties allocation for proper measurement
    if (!elem->bound) {
        Pool* pool = lycon->doc->view_tree->pool;
        elem->bound = (BoundaryProp*)pool_calloc(pool, sizeof(BoundaryProp));
    }

    // Ensure grid item properties are allocated
    if (!elem->gi) {
        Pool* pool = lycon->doc->view_tree->pool;
        elem->gi = (GridItemProp*)pool_calloc(pool, sizeof(GridItemProp));
        if (elem->gi) {
            elem->item_prop_type = DomElement::ITEM_PROP_GRID;
            // Initialize with auto placement defaults
            elem->gi->is_grid_auto_placed = true;
            elem->gi->justify_self = CSS_VALUE_AUTO;
            elem->gi->align_self_grid = CSS_VALUE_AUTO;
        }
    }

    // CRITICAL: Set lycon->view to this element so style resolution
    // applies properties to this element, not some other view
    View* saved_view = lycon->view;
    lycon->view = (View*)elem;

    // Resolve styles for this element (CSS cascade, inheritance, etc.)
    // This will now correctly apply padding/margin/border to elem->bound
    dom_node_resolve_style(child, lycon);

    // Restore previous view
    lycon->view = saved_view;

    log_debug("Grid item view initialized: %s (view_type=%d, bound=%p)",
              child->node_name(), elem->view_type, (void*)elem->bound);
}

// ============================================================================
// Pass 1: Content Measurement
// ============================================================================

void measure_grid_items(LayoutContext* lycon, GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_enter();
    log_debug("Measuring intrinsic sizes for grid items");

    // Iterate through all grid items and measure their content
    ViewBlock* container = (ViewBlock*)lycon->elmt;
    DomNode* child = container ? container->first_child : nullptr;

    while (child) {
        if (child->is_element()) {
            ViewBlock* item = (ViewBlock*)child->as_element();

            // Skip absolute positioned items
            bool is_absolute = item->position &&
                              (item->position->position == CSS_VALUE_ABSOLUTE ||
                               item->position->position == CSS_VALUE_FIXED);

            if (!is_absolute) {
                int min_width = 0, max_width = 0, min_height = 0, max_height = 0;
                measure_grid_item_intrinsic(lycon, item, &min_width, &max_width,
                                            &min_height, &max_height);

                // Store measurements in the item for later use
                if (item->gi) {
                    item->gi->measured_min_width = min_width;
                    item->gi->measured_max_width = max_width;
                    item->gi->measured_min_height = min_height;
                    item->gi->measured_max_height = max_height;
                }

                log_debug("Grid item %s measured: min_w=%d, max_w=%d, min_h=%d, max_h=%d",
                          child->node_name(), min_width, max_width, min_height, max_height);
            }
        }
        child = child->next_sibling;
    }

    log_leave();
}

void measure_grid_item_intrinsic(LayoutContext* lycon, ViewBlock* item,
                                  int* min_width, int* max_width,
                                  int* min_height, int* max_height) {
    if (!item) {
        *min_width = *max_width = *min_height = *max_height = 0;
        return;
    }

    log_debug("Measuring intrinsic sizes for grid item %s", item->node_name());

    // Check measurement cache first (shared with flex layout)
    MeasurementCacheEntry* cached = get_from_measurement_cache((DomNode*)item);
    if (cached) {
        *min_width = cached->content_width;
        *max_width = cached->measured_width;
        *min_height = cached->content_height;
        *max_height = cached->measured_height;
        log_debug("Using cached measurements for %s", item->node_name());
        return;
    }

    // Initialize output values
    *min_width = *max_width = *min_height = *max_height = 0;

    // Check if item has explicit dimensions from CSS
    bool has_explicit_width = false, has_explicit_height = false;
    if (item->blk) {
        if (item->blk->given_width > 0) {
            *min_width = *max_width = (int)item->blk->given_width;
            has_explicit_width = true;
        }
        if (item->blk->given_height > 0) {
            *min_height = *max_height = (int)item->blk->given_height;
            has_explicit_height = true;
        }

        // If both dimensions are explicit, we're done
        if (has_explicit_width && has_explicit_height) {
            log_debug("Grid item %s has explicit dimensions: %dx%d",
                      item->node_name(), *min_width, *min_height);
            return;
        }
    }

    // Use unified intrinsic sizing API (same as flex layout)
    // This uses FreeType for accurate text measurement
    if (!has_explicit_width) {
        float min_w = calculate_min_content_width(lycon, (DomNode*)item);
        float max_w = calculate_max_content_width(lycon, (DomNode*)item);
        *min_width = (int)(min_w + 0.5f);
        *max_width = (int)(max_w + 0.5f);
    }

    if (!has_explicit_height) {
        // Height depends on width for proper text wrapping
        float width_for_height = (float)*max_width;
        float min_h = calculate_min_content_height(lycon, (DomNode*)item, width_for_height);
        float max_h = calculate_max_content_height(lycon, (DomNode*)item, width_for_height);
        *min_height = (int)(min_h + 0.5f);
        *max_height = (int)(max_h + 0.5f);
    }

    // Ensure minimum sizes (prevent 0-sized items)
    if (*min_width <= 0) *min_width = 1;
    if (*max_width <= 0) *max_width = 1;
    if (*min_height <= 0) *min_height = 1;
    if (*max_height <= 0) *max_height = 1;

    // NOTE: Padding and border are already included by:
    // - calculate_max_content_width: via measure_element_intrinsic_widths (lines 304-318)
    // - calculate_max_content_height: directly adds padding/border (lines 405-413)
    // Do NOT add padding/border again here to avoid double-counting

    // Store in cache
    store_in_measurement_cache((DomNode*)item, *max_width, *max_height,
                               *min_width, *min_height);

    log_debug("Grid item %s measured: min=%dx%d, max=%dx%d",
              item->node_name(), *min_width, *min_height, *max_width, *max_height);
}

// ============================================================================
// Pass 3: Final Content Layout
// ============================================================================

void layout_final_grid_content(LayoutContext* lycon, GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_enter();
    log_info("FINAL GRID CONTENT LAYOUT START");
    log_debug("grid_layout=%p, item_count=%d, grid_items=%p",
              grid_layout, grid_layout->item_count, grid_layout->grid_items);

    // DEBUG: Print item pointers for comparison
    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* item = grid_layout->grid_items[i];
        printf("DEBUG Pass3: grid_items[%d]=%p, x=%.1f, y=%.1f, w=%.1f, h=%.1f\n",
               i, (void*)item, item ? item->x : -1, item ? item->y : -1,
               item ? item->width : -1, item ? item->height : -1);
    }

    // Layout content within each grid item with their final sizes
    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* item = grid_layout->grid_items[i];
        if (!item) continue;

        log_debug("Final layout for grid item %d: %s at (%d,%d) size %dx%d",
                  i, item->node_name(), item->x, item->y, item->width, item->height);

        layout_grid_item_final_content_multipass(lycon, item);
    }

    log_info("FINAL GRID CONTENT LAYOUT END");
    log_leave();
}

// Layout final content of a single grid item (multipass version with nested support)
static void layout_grid_item_final_content_multipass(LayoutContext* lycon, ViewBlock* grid_item) {
    if (!grid_item) return;

    log_enter();
    log_info("Layout grid item content: item=%p (%s), size=%dx%d at (%d,%d)",
             grid_item, grid_item->node_name(),
             grid_item->width, grid_item->height,
             grid_item->x, grid_item->y);

    // Save parent context
    BlockContext pa_block = lycon->block;
    Linebox pa_line = lycon->line;
    FontBox pa_font = lycon->font;

    // Calculate content area dimensions accounting for box model
    int content_width = grid_item->width;
    int content_height = grid_item->height;
    int content_x_offset = 0;
    int content_y_offset = 0;

    if (grid_item->bound) {
        // Account for padding
        content_width -= (grid_item->bound->padding.left + grid_item->bound->padding.right);
        content_height -= (grid_item->bound->padding.top + grid_item->bound->padding.bottom);
        content_x_offset = grid_item->bound->padding.left;
        content_y_offset = grid_item->bound->padding.top;

        // Account for border
        if (grid_item->bound->border) {
            content_width -= (grid_item->bound->border->width.left +
                             grid_item->bound->border->width.right);
            content_height -= (grid_item->bound->border->width.top +
                              grid_item->bound->border->width.bottom);
            content_x_offset += grid_item->bound->border->width.left;
            content_y_offset += grid_item->bound->border->width.top;
        }
    }

    // Ensure non-negative dimensions
    if (content_width < 0) content_width = 0;
    if (content_height < 0) content_height = 0;

    // Set up block formatting context for nested content
    lycon->block.content_width = content_width;
    lycon->block.content_height = content_height;
    lycon->block.given_width = content_width;
    lycon->block.given_height = -1;  // Auto height
    lycon->block.advance_y = content_y_offset;  // Start after padding/border top
    lycon->block.max_width = 0;
    lycon->elmt = (DomNode*)grid_item;

    // Inherit text alignment from grid item if specified
    if (grid_item->blk) {
        lycon->block.text_align = grid_item->blk->text_align;
    }

    // Set up line formatting context
    line_init(lycon, content_x_offset, content_x_offset + content_width);

    // Check if this grid item is itself a grid or flex container (nested)
    if (grid_item->display.inner == CSS_VALUE_GRID) {
        log_info(">>> NESTED GRID DETECTED: item=%p (%s)", grid_item, grid_item->node_name());

        // Recursively handle nested grid
        layout_grid_content(lycon, grid_item);

    } else if (grid_item->display.inner == CSS_VALUE_FLEX) {
        log_info(">>> NESTED FLEX DETECTED: item=%p (%s)", grid_item, grid_item->node_name());

        // Use flex layout for nested flex container
        // The flex layout will initialize its own flex items with init_flex_item_view
        // Do NOT call init_grid_item_view for flex children - they are flex items, not grid items!
        extern void layout_flex_container_with_nested_content(LayoutContext* lycon, ViewBlock* flex_container);
        layout_flex_container_with_nested_content(lycon, grid_item);

    } else {
        // Standard flow layout for grid item content
        log_debug("Layout flow content for grid item %s", grid_item->node_name());

        DomNode* child = grid_item->first_child;
        while (child) {
            layout_flow_node(lycon, child);
            child = child->next_sibling;
        }

        // Finalize any pending line content
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        }
    }

    // Update grid item content dimensions
    grid_item->content_width = lycon->block.max_width;
    grid_item->content_height = lycon->block.advance_y;

    // Restore parent context
    lycon->block = pa_block;
    lycon->line = pa_line;
    lycon->font = pa_font;

    log_info("Grid item content layout complete: %s, content=%dx%d",
             grid_item->node_name(), grid_item->content_width, grid_item->content_height);
    log_leave();
}

// ============================================================================
// Utility Functions
// ============================================================================

bool grid_item_is_nested_container(ViewBlock* item) {
    if (!item) return false;
    return (item->display.inner == CSS_VALUE_GRID ||
            item->display.inner == CSS_VALUE_FLEX);
}

void layout_grid_absolute_children(LayoutContext* lycon, ViewBlock* container) {
    log_enter();
    log_debug("=== LAYING OUT ABSOLUTE POSITIONED CHILDREN ===");

    DomNode* child = container->first_child;
    while (child) {
        if (child->is_element()) {
            ViewBlock* child_block = (ViewBlock*)child->as_element();

            // Check if this child is absolute or fixed positioned
            if (child_block->position &&
                (child_block->position->position == CSS_VALUE_ABSOLUTE ||
                 child_block->position->position == CSS_VALUE_FIXED)) {

                log_debug("Found absolute positioned child: %s", child->node_name());

                // Save parent context
                BlockContext pa_block = lycon->block;
                Linebox pa_line = lycon->line;

                // Set up lycon->block dimensions from the child's CSS
                if (child_block->blk) {
                    lycon->block.given_width = child_block->blk->given_width;
                    lycon->block.given_height = child_block->blk->given_height;
                } else {
                    lycon->block.given_width = -1;
                    lycon->block.given_height = -1;
                }

                // Lay out the absolute positioned block
                layout_abs_block(lycon, child, child_block, &pa_block, &pa_line);

                // Restore parent context
                lycon->block = pa_block;
                lycon->line = pa_line;

                log_debug("Absolute child laid out: %s at (%.1f, %.1f) size %.1fx%.1f",
                         child->node_name(), child_block->x, child_block->y,
                         child_block->width, child_block->height);
            }
        }
        child = child->next_sibling;
    }

    log_debug("=== ABSOLUTE POSITIONED CHILDREN LAYOUT COMPLETE ===");
    log_leave();
}
