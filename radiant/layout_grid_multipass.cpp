#include "layout.hpp"
#include "layout_grid_multipass.hpp"
#include "grid.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "intrinsic_sizing.hpp"
#include "layout_mode.hpp"
#include "layout_cache.hpp"

extern "C" {
#include "../lib/log.h"
#include "../lib/memtrack.h"
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

    // =========================================================================
    // CACHE LOOKUP: Check if we have a cached result for these constraints
    // This avoids redundant layout for repeated measurements with same inputs
    // =========================================================================
    DomElement* dom_elem = (DomElement*)grid_container;
    radiant::LayoutCache* cache = dom_elem ? dom_elem->layout_cache : nullptr;

    // Build known dimensions from current constraints
    radiant::KnownDimensions known_dims = radiant::known_dimensions_none();
    if (lycon->block.given_width >= 0) {
        known_dims.width = lycon->block.given_width;
        known_dims.has_width = true;
    }
    if (lycon->block.given_height >= 0) {
        known_dims.height = lycon->block.given_height;
        known_dims.has_height = true;
    }

    // Try cache lookup
    if (cache) {
        radiant::SizeF cached_size;
        if (radiant::layout_cache_get(cache, known_dims, lycon->available_space,
                                       lycon->run_mode, &cached_size)) {
            // Cache hit! Use cached dimensions
            grid_container->width = cached_size.width;
            grid_container->height = cached_size.height;
            g_layout_cache_hits++;
            log_info("GRID CACHE HIT: container=%p, size=(%.1f x %.1f), mode=%d",
                     grid_container, cached_size.width, cached_size.height, (int)lycon->run_mode);
            log_leave();
            return;
        }
        g_layout_cache_misses++;
        log_debug("GRID CACHE MISS: container=%p, mode=%d", grid_container, (int)lycon->run_mode);
    }

    // =========================================================================
    // EARLY BAILOUT: For ComputeSize mode, check if dimensions are already known
    // This optimization avoids redundant layout when only measurements are needed
    // =========================================================================
    if (lycon->run_mode == radiant::RunMode::ComputeSize) {
        // Check if both dimensions are explicitly set via CSS
        bool has_definite_width = (lycon->block.given_width >= 0);
        bool has_definite_height = (lycon->block.given_height >= 0);

        if (has_definite_width && has_definite_height) {
            // Both dimensions known - can skip full layout
            grid_container->width = lycon->block.given_width;
            grid_container->height = lycon->block.given_height;
            log_info("GRID EARLY BAILOUT: Both dimensions known (%.1fx%.1f), skipping full layout",
                     grid_container->width, grid_container->height);
            log_leave();
            return;
        }
        log_debug("GRID: ComputeSize mode but dimensions not fully known (w=%d, h=%d)",
                  has_definite_width, has_definite_height);
    }

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

    // Re-align items after content is laid out (now items have final heights)
    // This is needed for align-items: center/end to work correctly
    align_grid_items(lycon->grid_container);
    log_info("=== GRID PASS 3 COMPLETE ===");

    // ========================================================================
    // Update container height based on actual item positions and sizes
    // This is needed because content layout may cause items to exceed their
    // track-allocated sizes (e.g., when item content is larger than track)
    // Only do this for containers with auto height (no explicit height set)
    // ========================================================================
    GridContainerLayout* grid_layout = lycon->grid_container;
    bool has_explicit_height = grid_container->blk && grid_container->blk->given_height > 0;

    if (grid_layout && grid_layout->item_count > 0 && !has_explicit_height) {
        // Find the maximum extent of all grid items
        float max_item_bottom = 0;
        for (int i = 0; i < grid_layout->item_count; i++) {
            ViewBlock* item = grid_layout->grid_items[i];
            if (!item) continue;

            float item_bottom = item->y + item->height;
            if (item_bottom > max_item_bottom) {
                max_item_bottom = item_bottom;
            }
        }

        // Add container's bottom padding and border
        float required_height = max_item_bottom;
        if (grid_container->bound) {
            required_height += grid_container->bound->padding.bottom;
            if (grid_container->bound->border) {
                required_height += grid_container->bound->border->width.bottom;
            }
        }

        // Update container height if needed
        if (required_height > grid_container->height) {
            log_info("GRID: Updating container height from %.1f to %.1f (based on item extents)",
                     grid_container->height, required_height);
            grid_container->height = required_height;

            // Also fix any item with negative y position (pushed above due to centering)
            // by shifting all items down
            float min_item_y = 0;
            for (int i = 0; i < grid_layout->item_count; i++) {
                ViewBlock* item = grid_layout->grid_items[i];
                if (!item) continue;
                if (item->y < min_item_y) {
                    min_item_y = item->y;
                }
            }
            if (min_item_y < 0) {
                // Shift all items down by the negative offset
                float shift = -min_item_y;
                for (int i = 0; i < grid_layout->item_count; i++) {
                    ViewBlock* item = grid_layout->grid_items[i];
                    if (!item) continue;
                    item->y += shift;
                }
                grid_container->height += shift;
                log_info("GRID: Shifted items down by %.1f to fix negative y positions", shift);
            }
        }
    }

    // Fallback: also check row-based calculation for containers without items
    // Only apply if container height is auto (not explicitly set)
    if (grid_layout && grid_layout->computed_row_count > 0 && !has_explicit_height) {
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

        // Only update if calculated height is greater (content overflow case)
        if (container_height > grid_container->height) {
            log_info("GRID: Updating container height from %.1f to %.1f (rows=%.1f, gaps=%.1f)",
                     grid_container->height, container_height, total_row_height,
                     grid_layout->row_gap * (grid_layout->computed_row_count - 1));
            grid_container->height = container_height;
        }
    }

    // ========================================================================
    // Update container width based on grid content (for shrink-to-fit containers)
    // ========================================================================
    if (grid_layout && grid_layout->computed_column_count > 0) {
        // Check if container is shrink-to-fit (absolutely positioned with no explicit width)
        bool is_shrink_to_fit = false;
        if (grid_container->position &&
            (grid_container->position->position == CSS_VALUE_ABSOLUTE ||
             grid_container->position->position == CSS_VALUE_FIXED)) {
            // Check if width is auto (not explicitly set)
            bool has_explicit_width = grid_container->blk && grid_container->blk->given_width > 0;
            bool has_left_right = grid_container->position->has_left && grid_container->position->has_right;
            if (!has_explicit_width && !has_left_right) {
                is_shrink_to_fit = true;
            }
        }

        if (is_shrink_to_fit) {
            // Calculate total width from column sizes plus gaps
            float total_column_width = 0;
            for (int i = 0; i < grid_layout->computed_column_count; i++) {
                total_column_width += grid_layout->computed_columns[i].base_size;
            }
            // Add gaps between columns
            total_column_width += grid_layout->column_gap * (grid_layout->computed_column_count - 1);

            // Add padding and border
            float container_width = total_column_width;
            if (grid_container->bound) {
                container_width += grid_container->bound->padding.left + grid_container->bound->padding.right;
                if (grid_container->bound->border) {
                    container_width += grid_container->bound->border->width.left +
                                       grid_container->bound->border->width.right;
                }
            }

            log_info("GRID: Updating container width from %d to %.1f (columns=%.1f, gaps=%.1f)",
                     grid_container->width, container_width, total_column_width,
                     grid_layout->column_gap * (grid_layout->computed_column_count - 1));
            grid_container->width = (int)container_width;
        }
    }

    // ========================================================================
    // PASS 4: Absolute Positioned Children
    // ========================================================================
    log_info("=== GRID PASS 4: Absolute positioned children ===");
    layout_grid_absolute_children(lycon, grid_container);
    log_info("=== GRID PASS 4 COMPLETE ===");

    // =========================================================================
    // CACHE STORE: Save computed result for future lookups
    // =========================================================================
    if (cache || (dom_elem && lycon->pool)) {
        // Lazy allocate cache if needed
        if (!cache && dom_elem) {
            cache = (radiant::LayoutCache*)pool_calloc(lycon->pool, sizeof(radiant::LayoutCache));
            if (cache) {
                radiant::layout_cache_init(cache);
                dom_elem->layout_cache = cache;
            }
        }
        if (cache) {
            radiant::SizeF result = radiant::size_f(grid_container->width, grid_container->height);
            radiant::layout_cache_store(cache, known_dims, lycon->available_space,
                                        lycon->run_mode, result);
            g_layout_cache_stores++;
            log_debug("GRID CACHE STORE: container=%p, size=(%.1f x %.1f), mode=%d",
                      grid_container, grid_container->width, grid_container->height, (int)lycon->run_mode);
        }
    }

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
    // IMPORTANT: fi and gi are in a union! Check item_prop_type, not just gi pointer
    if (elem->item_prop_type != DomElement::ITEM_PROP_GRID) {
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

                // Store only WIDTH measurements in the item for later use
                // HEIGHT measurements are intentionally NOT stored here because:
                // - Heights depend on the actual column width (after column sizing)
                // - Row sizing will calculate heights on-demand using item->width
                // This follows CSS Grid spec §11.5 where row sizing happens after column sizing
                if (item->gi) {
                    item->gi->measured_min_width = min_width;
                    item->gi->measured_max_width = max_width;
                    // Note: We don't set measured_min/max_height here.
                    // The calculate_grid_item_intrinsic_sizes function will compute
                    // heights on-demand using the actual column width.
                    item->gi->has_measured_size = true;  // Indicates width measurements are valid
                    log_debug("Stored width measurements for %s (gi=%p): min_w=%.1f, max_w=%.1f",
                              child->node_name(), item->gi,
                              item->gi->measured_min_width, item->gi->measured_max_width);
                } else {
                    log_debug("WARN: No gi for %s to store measurements", child->node_name());
                }

                log_debug("Grid item %s measured: min_w=%d, max_w=%d",
                          child->node_name(), min_width, max_width);
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
        // Height calculation for grid items:
        // - For min-content height: use max-content width (content flows without wrapping)
        // - For max-content height: same as min-content for block containers
        //
        // Note: Counter-intuitively, using max-content WIDTH gives MINIMUM height
        // because text doesn't wrap. Using min-content width causes wrapping = taller.
        //
        // CSS Sizing Level 3 says: For block containers, min-content height == max-content height
        // Both should be calculated at max-content width (no forced wrapping).
        //
        // The actual grid track sizing will use max-content height for auto rows.
        float width_for_height = (float)*max_width;

        // Cap to a reasonable maximum to avoid extremely long single-line text
        if (width_for_height > 2000) {
            width_for_height = 2000;
        }

        // For block containers, min-content height == max-content height
        float content_height = calculate_max_content_height(lycon, (DomNode*)item, width_for_height);
        *min_height = (int)(content_height + 0.5f);
        *max_height = (int)(content_height + 0.5f);
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
        log_debug("Pass3: grid_items[%d]=%p, x=%.1f, y=%.1f, w=%.1f, h=%.1f\n",
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
    // Note: max_width and advance_y are relative to the content box
    // We need to add padding for the full content dimensions
    grid_item->content_width = lycon->block.max_width;
    if (grid_item->bound) {
        grid_item->content_width += grid_item->bound->padding.right;
        grid_item->content_height = lycon->block.advance_y + grid_item->bound->padding.bottom;
    } else {
        grid_item->content_height = lycon->block.advance_y;
    }

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

// ============================================================================
// Grid Absolute Positioning Helpers
// ============================================================================

// Calculate track positions for a given axis
// Returns an array of (track_count + 1) positions representing grid line positions
// Caller must free the returned array
static float* calculate_grid_line_positions(GridContainerLayout* grid_layout, bool is_row_axis,
                                            float container_offset, int* out_line_count) {
    int track_count = is_row_axis ? grid_layout->computed_row_count : grid_layout->computed_column_count;
    GridTrack* tracks = is_row_axis ? grid_layout->computed_rows : grid_layout->computed_columns;
    float gap = is_row_axis ? grid_layout->row_gap : grid_layout->column_gap;

    // We need (track_count + 1) positions for grid lines
    int line_count = track_count + 1;
    float* positions = (float*)mem_calloc(line_count, sizeof(float), MEM_CAT_LAYOUT);
    if (!positions) return nullptr;

    float current_pos = container_offset;
    for (int i = 0; i <= track_count; i++) {
        positions[i] = current_pos;
        if (i < track_count) {
            current_pos += tracks[i].computed_size;
            if (i < track_count - 1) {
                current_pos += gap;
            }
        }
    }

    *out_line_count = line_count;
    return positions;
}

// Compute the containing block for an absolutely positioned grid item
// Based on its grid-column and grid-row properties
// Returns true if grid area was computed, false if should use normal containing block
static bool compute_grid_area_for_absolute(
    GridContainerLayout* grid_layout,
    ViewBlock* container,
    ViewBlock* item,
    float* out_x, float* out_y, float* out_width, float* out_height
) {
    if (!grid_layout || !container || !item) return false;

    // Get grid item properties (if any)
    GridItemProp* gi = item->gi;

    // Check if item has any grid placement
    bool has_col_start = gi && gi->has_explicit_grid_column_start && gi->grid_column_start != 0;
    bool has_col_end = gi && gi->has_explicit_grid_column_end && gi->grid_column_end != 0;
    bool has_row_start = gi && gi->has_explicit_grid_row_start && gi->grid_row_start != 0;
    bool has_row_end = gi && gi->has_explicit_grid_row_end && gi->grid_row_end != 0;

    // If no explicit grid placement, use normal containing block (whole grid padding box)
    if (!has_col_start && !has_col_end && !has_row_start && !has_row_end) {
        log_debug("Absolute item has no grid placement, using full grid padding box");
        return false;
    }

    // Calculate container offsets (padding + border)
    float container_offset_x = 0, container_offset_y = 0;
    if (container->bound) {
        container_offset_x += container->bound->padding.left;
        container_offset_y += container->bound->padding.top;
        if (container->bound->border) {
            container_offset_x += container->bound->border->width.left;
            container_offset_y += container->bound->border->width.top;
        }
    }

    // Calculate grid line positions
    int col_line_count = 0, row_line_count = 0;
    float* col_positions = calculate_grid_line_positions(grid_layout, false, container_offset_x, &col_line_count);
    float* row_positions = calculate_grid_line_positions(grid_layout, true, container_offset_y, &row_line_count);

    if (!col_positions || !row_positions) {
        mem_free(col_positions);
        mem_free(row_positions);
        return false;
    }

    // Resolve grid lines (convert from 1-based CSS to 0-based indices)
    // For auto lines, use the grid edges (line 1 or last line)
    int col_start_line = has_col_start ? gi->grid_column_start : 1;
    int col_end_line = has_col_end ? gi->grid_column_end : col_line_count;
    int row_start_line = has_row_start ? gi->grid_row_start : 1;
    int row_end_line = has_row_end ? gi->grid_row_end : row_line_count;

    // Handle negative line numbers (count from end)
    if (col_start_line < 0) col_start_line = col_line_count + col_start_line + 1;
    if (col_end_line < 0) col_end_line = col_line_count + col_end_line + 1;
    if (row_start_line < 0) row_start_line = row_line_count + row_start_line + 1;
    if (row_end_line < 0) row_end_line = row_line_count + row_end_line + 1;

    // Clamp to valid range and convert to 0-based index
    int col_start_idx = (col_start_line >= 1 && col_start_line <= col_line_count) ?
                        col_start_line - 1 : 0;
    int col_end_idx = (col_end_line >= 1 && col_end_line <= col_line_count) ?
                      col_end_line - 1 : col_line_count - 1;
    int row_start_idx = (row_start_line >= 1 && row_start_line <= row_line_count) ?
                        row_start_line - 1 : 0;
    int row_end_idx = (row_end_line >= 1 && row_end_line <= row_line_count) ?
                      row_end_line - 1 : row_line_count - 1;

    // Ensure start < end
    if (col_start_idx > col_end_idx) { int t = col_start_idx; col_start_idx = col_end_idx; col_end_idx = t; }
    if (row_start_idx > row_end_idx) { int t = row_start_idx; row_start_idx = row_end_idx; row_end_idx = t; }

    // Calculate grid area rectangle
    *out_x = col_positions[col_start_idx];
    *out_y = row_positions[row_start_idx];
    *out_width = col_positions[col_end_idx] - col_positions[col_start_idx];
    *out_height = row_positions[row_end_idx] - row_positions[row_start_idx];

    log_debug("Grid area for absolute item: lines col %d-%d, row %d-%d => pos (%.1f, %.1f) size %.1fx%.1f",
              col_start_line, col_end_line, row_start_line, row_end_line,
              *out_x, *out_y, *out_width, *out_height);

    mem_free(col_positions);
    mem_free(row_positions);
    return true;
}

void layout_grid_absolute_children(LayoutContext* lycon, ViewBlock* container) {
    log_enter();
    log_debug("=== LAYING OUT ABSOLUTE POSITIONED CHILDREN for container=%s ===", container->node_name());

    // Get grid layout for computing grid area containing blocks
    GridContainerLayout* grid_layout = lycon->grid_container;

    // For grid absolute positioning, the static position should be at the
    // padding box edge (border offset), not the content box edge (border + padding).
    // Calculate border offset for use in static position correction.
    float border_offset_x = 0, border_offset_y = 0;
    if (container->bound && container->bound->border) {
        border_offset_x = container->bound->border->width.left;
        border_offset_y = container->bound->border->width.top;
    }
    log_debug("Grid absolute: border_offset=(%f, %f)", border_offset_x, border_offset_y);

    DomNode* child = container->first_child;
    int child_count = 0;
    while (child) {
        child_count++;
        if (child->is_element()) {
            ViewBlock* child_block = (ViewBlock*)child->as_element();
            log_debug("Checking child %d: tag=%s, has_position=%d, position_type=%d",
                      child_count, child->node_name(),
                      child_block->position != nullptr,
                      child_block->position ? child_block->position->position : -1);

            // Check if this child is absolute or fixed positioned
            if (child_block->position &&
                (child_block->position->position == CSS_VALUE_ABSOLUTE ||
                 child_block->position->position == CSS_VALUE_FIXED)) {

                log_debug("Found absolute positioned child: %s", child->node_name());

                // Check if this absolute item has grid placement properties
                float grid_area_x, grid_area_y, grid_area_width, grid_area_height;
                bool has_grid_area = compute_grid_area_for_absolute(
                    grid_layout, container, child_block,
                    &grid_area_x, &grid_area_y, &grid_area_width, &grid_area_height
                );

                // Save parent context
                BlockContext pa_block = lycon->block;
                Linebox pa_line = lycon->line;

                // For grid containers, static position should be at the padding box edge
                // (where grid content starts), not at the content box edge.
                // The pa_line.left and pa_block.advance_y include padding, so we need
                // to subtract padding and only keep border offset for static position.
                // This ensures absolute items with auto insets are placed at the padding edge.
                pa_line.left = border_offset_x;
                pa_block.advance_y = border_offset_y;

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

                // If item has grid area, adjust position relative to grid area
                // The layout_abs_block uses the grid container as containing block,
                // but we need to adjust for the grid area offset
                if (has_grid_area) {
                    // Position is now relative to grid container padding box
                    // For grid-placed absolutes, need to adjust based on grid area
                    log_debug("Adjusting absolute item position for grid area");

                    // The insets (top/left/right/bottom) should be relative to grid area
                    // layout_abs_block already computed position relative to container
                    // We need to add grid area offset if item doesn't have explicit insets
                    // Actually, for CSS Grid, the containing block IS the grid area

                    // Recalculate position based on grid area as containing block
                    // This requires reimplementing some of the absolute positioning logic
                    // For now, just adjust the base position to start from grid area
                    float old_x = child_block->x;
                    float old_y = child_block->y;

                    // Get container padding/border offset (already in grid_area coords)
                    float cb_x = grid_area_x;
                    float cb_y = grid_area_y;
                    float cb_width = grid_area_width;
                    float cb_height = grid_area_height;

                    // Recompute position with grid area as containing block
                    PositionProp* pos = child_block->position;
                    float new_x = old_x, new_y = old_y;

                    // Handle horizontal positioning
                    if (pos->has_left) {
                        new_x = cb_x + pos->left;
                        if (child_block->bound && child_block->bound->margin.left > 0) {
                            new_x += child_block->bound->margin.left;
                        }
                    } else if (pos->has_right) {
                        new_x = cb_x + cb_width - pos->right - child_block->width;
                        if (child_block->bound && child_block->bound->margin.right > 0) {
                            new_x -= child_block->bound->margin.right;
                        }
                    } else {
                        // auto positioning - use static position within grid area
                        new_x = cb_x;
                    }

                    // Handle vertical positioning
                    if (pos->has_top) {
                        new_y = cb_y + pos->top;
                        if (child_block->bound && child_block->bound->margin.top > 0) {
                            new_y += child_block->bound->margin.top;
                        }
                    } else if (pos->has_bottom) {
                        new_y = cb_y + cb_height - pos->bottom - child_block->height;
                        if (child_block->bound && child_block->bound->margin.bottom > 0) {
                            new_y -= child_block->bound->margin.bottom;
                        }
                    } else {
                        // auto positioning - use static position within grid area
                        new_y = cb_y;
                    }

                    // Handle percentage width/height relative to grid area
                    if (pos->has_left && pos->has_right) {
                        // Stretch to grid area (left to right)
                        float margin_left = (child_block->bound) ? child_block->bound->margin.left : 0;
                        float margin_right = (child_block->bound) ? child_block->bound->margin.right : 0;
                        child_block->width = cb_width - pos->left - pos->right - margin_left - margin_right;
                        new_x = cb_x + pos->left + margin_left;
                    }

                    if (pos->has_top && pos->has_bottom) {
                        // Stretch to grid area (top to bottom)
                        float margin_top = (child_block->bound) ? child_block->bound->margin.top : 0;
                        float margin_bottom = (child_block->bound) ? child_block->bound->margin.bottom : 0;
                        child_block->height = cb_height - pos->top - pos->bottom - margin_top - margin_bottom;
                        new_y = cb_y + pos->top + margin_top;
                    }

                    child_block->x = new_x;
                    child_block->y = new_y;

                    log_debug("Grid area adjusted position: (%.1f, %.1f) -> (%.1f, %.1f)",
                              old_x, old_y, new_x, new_y);
                }

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
