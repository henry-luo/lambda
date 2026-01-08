#include "grid.hpp"
#include "layout.hpp"
#include "view.hpp"
#include "grid_enhanced_adapter.hpp"  // Enhanced grid integration

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../lib/log.h"
#include "../lib/memtrack.h"
}

// Forward declarations
void expand_auto_repeat_tracks(GridContainerLayout* grid_layout);

// Initialize grid container layout state
void init_grid_container(LayoutContext* lycon, ViewBlock* container) {
    if (!container) return;
    log_debug("Initializing grid container for %p\n", container);

    GridContainerLayout* grid = (GridContainerLayout*)mem_calloc(1, sizeof(GridContainerLayout), MEM_CAT_LAYOUT);
    lycon->grid_container = grid;
    grid->lycon = lycon;  // Store layout context for intrinsic sizing

    // Initialize auto-placement cursors (grid lines are 1-indexed)
    grid->auto_row_cursor = 1;
    grid->auto_col_cursor = 1;

    // Debug: check what's available
    log_debug("container->embed=%p", (void*)container->embed);
    if (container->embed) {
        log_debug("container->embed->grid=%p", (void*)container->embed->grid);
        if (container->embed->grid) {
            log_debug("embed->grid values: row_gap=%.1f, column_gap=%.1f",
                      container->embed->grid->row_gap, container->embed->grid->column_gap);
        }
    }

    if (container->embed && container->embed->grid) {
        memcpy(grid, container->embed->grid, sizeof(GridProp));
        grid->lycon = lycon;  // Restore after memcpy
        log_debug("Copied grid props: row_gap=%.1f, column_gap=%.1f",
                  grid->row_gap, grid->column_gap);
    } else {
        // Set default values using enum names that align with Lexbor constants
        grid->justify_content = CSS_VALUE_START;
        grid->align_content = CSS_VALUE_START;
        grid->justify_items = CSS_VALUE_STRETCH;
        grid->align_items = CSS_VALUE_STRETCH;
        grid->grid_auto_flow = CSS_VALUE_ROW;
        // Initialize gaps
        grid->row_gap = 0;
        grid->column_gap = 0;
    }

    // Initialize dynamic arrays
    grid->allocated_items = 8;
    grid->grid_items = (ViewBlock**)mem_calloc(grid->allocated_items, sizeof(ViewBlock*), MEM_CAT_LAYOUT);

    // Only allocate new areas array if not already copied from embed->grid
    if (!grid->grid_areas || grid->area_count == 0) {
        grid->allocated_areas = 4;
        grid->grid_areas = (GridArea*)mem_calloc(grid->allocated_areas, sizeof(GridArea), MEM_CAT_LAYOUT);
        grid->area_count = 0;  // Reset if we allocated new
    }
    // If grid_areas was copied from embed->grid, keep it as-is
    log_debug("Grid areas after init: area_count=%d, grid_areas=%p", grid->area_count, (void*)grid->grid_areas);

    grid->allocated_line_names = 8;
    grid->line_names = (GridLineName*)mem_calloc(grid->allocated_line_names, sizeof(GridLineName), MEM_CAT_LAYOUT);

    // Initialize track lists - only create new ones if not already copied from embed->grid
    // Track ownership to avoid double-free
    if (!grid->grid_template_rows) {
        grid->grid_template_rows = create_grid_track_list(4);
        grid->owns_template_rows = true;
    } else {
        grid->owns_template_rows = false;  // Shared with embed->grid
    }
    if (!grid->grid_template_columns) {
        grid->grid_template_columns = create_grid_track_list(4);
        grid->owns_template_columns = true;
    } else {
        grid->owns_template_columns = false;  // Shared with embed->grid
    }
    if (!grid->grid_auto_rows) {
        grid->grid_auto_rows = create_grid_track_list(2);
        grid->owns_auto_rows = true;
    } else {
        grid->owns_auto_rows = false;  // Shared with embed->grid
    }
    if (!grid->grid_auto_columns) {
        grid->grid_auto_columns = create_grid_track_list(2);
        grid->owns_auto_columns = true;
    } else {
        grid->owns_auto_columns = false;  // Shared with embed->grid
    }

    grid->needs_reflow = false;

    log_debug("Grid container initialized successfully\n");
}

// Cleanup grid container resources
void cleanup_grid_container(LayoutContext* lycon) {
    if (!lycon || !lycon->grid_container) return;
    log_debug("Cleaning up grid container for %p\n", lycon->grid_container);
    GridContainerLayout* grid = lycon->grid_container;

    // Free track lists only if we own them (not shared with embed->grid)
    if (grid->owns_template_rows) {
        destroy_grid_track_list(grid->grid_template_rows);
    }
    if (grid->owns_template_columns) {
        destroy_grid_track_list(grid->grid_template_columns);
    }
    if (grid->owns_auto_rows) {
        destroy_grid_track_list(grid->grid_auto_rows);
    }
    if (grid->owns_auto_columns) {
        destroy_grid_track_list(grid->grid_auto_columns);
    }

    // Free computed tracks
    if (grid->computed_rows) {
        for (int i = 0; i < grid->computed_row_count; i++) {
            // Only free size if we own it (created during init, not shared)
            if (grid->computed_rows[i].size && grid->computed_rows[i].owns_size) {
                destroy_grid_track_size(grid->computed_rows[i].size);
            }
        }
        mem_free(grid->computed_rows);
    }

    if (grid->computed_columns) {
        for (int i = 0; i < grid->computed_column_count; i++) {
            // Only free size if we own it (created during init, not shared)
            if (grid->computed_columns[i].size && grid->computed_columns[i].owns_size) {
                destroy_grid_track_size(grid->computed_columns[i].size);
            }
        }
        mem_free(grid->computed_columns);
    }

    // Free grid areas
    for (int i = 0; i < grid->area_count; i++) {
        destroy_grid_area(&grid->grid_areas[i]);
    }
    mem_free(grid->grid_areas);

    // Free line names
    for (int i = 0; i < grid->line_name_count; i++) {
        mem_free(grid->line_names[i].name);
    }
    mem_free(grid->line_names);

    mem_free(grid->grid_items);
    mem_free(grid);
    log_debug("Grid container cleanup complete\n");
}

// Main grid layout algorithm entry point
void layout_grid_container(LayoutContext* lycon, ViewBlock* container) {
    log_debug("layout_grid_container called with container=%p", container);
    if (!container) {
        log_debug("Early return - container is NULL\n");
        return;
    }

    // Check if this is actually a grid container by display type
    // Note: embed->grid may be NULL if grid-template-* properties weren't resolved,
    // but we can still run grid layout with auto-placement
    if (container->display.inner != CSS_VALUE_GRID) {
        log_debug("Early return - not a grid container (display.inner=%d)\n", container->display.inner);
        return;
    }

    GridContainerLayout* grid_layout = lycon->grid_container;
    log_debug("Grid container found - template_columns=%p, template_rows=%p",
        grid_layout->grid_template_columns, grid_layout->grid_template_rows);
    if (grid_layout->grid_template_columns) {
        log_debug("DEBUG: Template columns track count: %d", grid_layout->grid_template_columns->track_count);
    }
    if (grid_layout->grid_template_rows) {
        log_debug("DEBUG: Template rows track count: %d", grid_layout->grid_template_rows->track_count);
    }

    log_debug("GRID START - container: %dx%d at (%d,%d)",
        container->width, container->height, container->x, container->y);

    // Check if container is shrink-to-fit (absolutely positioned with no explicit width)
    // This affects how we determine available width for track sizing
    bool is_shrink_to_fit_width = false;
    if (container->position &&
        (container->position->position == CSS_VALUE_ABSOLUTE ||
         container->position->position == CSS_VALUE_FIXED)) {
        bool has_explicit_width = container->blk && container->blk->given_width > 0;
        bool has_left_right = container->position->has_left && container->position->has_right;
        if (!has_explicit_width && !has_left_right) {
            is_shrink_to_fit_width = true;
        }
    }
    grid_layout->is_shrink_to_fit_width = is_shrink_to_fit_width;
    log_debug("GRID: is_shrink_to_fit_width=%d", is_shrink_to_fit_width);

    // Set container dimensions
    grid_layout->container_width = container->width;
    grid_layout->container_height = container->height;

    // Determine if container has an explicit height (not auto)
    // This affects whether auto row tracks should stretch to fill the container
    grid_layout->has_explicit_height = (container->blk && container->blk->given_height >= 0);
    log_debug("GRID: has_explicit_height=%d (given_height=%.1f)",
              grid_layout->has_explicit_height,
              container->blk ? container->blk->given_height : -1);

    // Calculate content dimensions (excluding borders and padding)
    grid_layout->content_width = container->width;
    grid_layout->content_height = container->height;

    if (container->bound && container->bound->border) {
        grid_layout->content_width -= (container->bound->border->width.left + container->bound->border->width.right);
        grid_layout->content_height -= (container->bound->border->width.top + container->bound->border->width.bottom);
    }

    if (container->bound) {
        grid_layout->content_width -= (container->bound->padding.left + container->bound->padding.right);
        grid_layout->content_height -= (container->bound->padding.top + container->bound->padding.bottom);
    }

    log_debug("GRID CONTENT - content: %dx%d, container: %dx%d\n",
              grid_layout->content_width, grid_layout->content_height,
              container->width, container->height);

    // Phase 1: Collect grid items (need count for auto-fit)
    log_debug("DEBUG: Phase 1 - Collecting grid items");
    ViewBlock** items;
    int item_count = collect_grid_items(grid_layout, container, &items);

    log_debug("GRID - collected %d items", item_count);

    // Expand auto-fill/auto-fit repeat() tracks now that we know content_width and item_count
    expand_auto_repeat_tracks(grid_layout);

    if (item_count == 0) {
        log_debug("No grid items found");
        return;
    }

    // Phase 2: Resolve grid template areas
    log_debug("DEBUG: Phase 2 - Resolving grid template areas");
    resolve_grid_template_areas(grid_layout);

    // Phase 3: Determine initial grid size from templates (before placement)
    log_debug("DEBUG: Phase 3 - Determining initial grid size from templates");
    determine_grid_size(grid_layout);

    // Phase 4: Place grid items (using enhanced CellOccupancyMatrix algorithm)
    log_debug("DEBUG: Phase 4 - Placing grid items with enhanced algorithm");

    // Use enhanced placement algorithm with proper collision detection
    // This replaces both place_grid_items and auto_place_grid_items_dense
    radiant::grid_adapter::place_items_with_occupancy(
        grid_layout,
        items,
        item_count,
        grid_layout->grid_auto_flow,
        grid_layout->is_dense_packing
    );

    // Phase 5: Update grid size after placement (may have grown due to auto-placement)
    log_debug("DEBUG: Phase 5 - Updating grid size after placement");
    determine_grid_size(grid_layout);

    // Phase 6: Resolve track sizes (using enhanced algorithm with intrinsic sizing)
    log_debug("DEBUG: Phase 6 - Resolving track sizes");
    resolve_track_sizes_enhanced(grid_layout, container);

    // For shrink-to-fit containers, update container width based on resolved track sizes
    if (grid_layout->is_shrink_to_fit_width && grid_layout->computed_column_count > 0) {
        float total_column_width = grid_layout->content_width;

        // Add padding and border back to get container width
        float container_width = total_column_width;
        if (container->bound) {
            container_width += container->bound->padding.left + container->bound->padding.right;
            if (container->bound->border) {
                container_width += container->bound->border->width.left +
                                   container->bound->border->width.right;
            }
        }

        log_debug("GRID shrink-to-fit: updating container width from %d to %.1f",
                  container->width, container_width);
        container->width = (int)container_width;
        grid_layout->container_width = container->width;
    }

    // Phase 6: Position grid items
    log_debug("DEBUG: Phase 6 - Positioning grid items");
    position_grid_items(grid_layout, container);

    // Phase 7: Align grid items
    log_debug("DEBUG: Phase 7 - Aligning grid items");
    align_grid_items(grid_layout);

    // Note: Phase 8 (content layout) is now handled by layout_grid_multipass.cpp Pass 3
    // The multipass flow calls layout_final_grid_content() after this function returns

    // Debug: Final item positions
    log_debug("DEBUG: FINAL GRID POSITIONS:");
    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        log_debug("FINAL_GRID_ITEM %d - pos: (%d,%d), size: %dx%d, grid_area: (%d-%d, %d-%d)",
            i, item->x, item->y, item->width, item->height,
            item->gi ? item->gi->computed_grid_row_start : 0,
            item->gi ? item->gi->computed_grid_row_end : 0,
            item->gi ? item->gi->computed_grid_column_start : 0,
            item->gi ? item->gi->computed_grid_column_end : 0);
    }
    log_debug("FINAL GRID POSITIONS:");
    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        log_debug("FINAL_GRID_ITEM %d - pos: (%d,%d), size: %dx%d",
                  i, item->x, item->y, item->width, item->height);
    }

    grid_layout->needs_reflow = false;
}

// Collect grid items from container children
int collect_grid_items(GridContainerLayout* grid_layout, ViewBlock* container, ViewBlock*** items) {
    log_debug("collect_grid_items called with container=%p, items=%p", container, items);
    if (!container || !items) {
        log_debug("Early return - container=%p, items=%p", container, items);
        return 0;
    }
    log_debug("grid=%p", grid_layout);
    if (!grid_layout) {
        log_debug("Early return - grid is NULL");
        return 0;
    }

    int count = 0;

    // Count element children first - ONLY count element nodes, skip text nodes
    log_debug("About to access container->first_child");
    DomNode* child_node = container->first_child;
    log_debug("first_child=%p", child_node);
    while (child_node) {
        // CRITICAL FIX: Only process element nodes, skip text nodes
        if (!child_node->is_element()) {
            child_node = child_node->next_sibling;
            continue;
        }

        ViewBlock* child = (ViewBlock*)child_node;
        // Filter out absolutely positioned and hidden items
        bool is_absolute = child->position &&
                          (child->position->position == CSS_VALUE_ABSOLUTE ||
                           child->position->position == CSS_VALUE_FIXED);
        bool is_hidden = child->in_line && child->in_line->visibility == VIS_HIDDEN;
        if (!is_absolute && !is_hidden) {
            count++;
        }
        child_node = child_node->next_sibling;
    }

    log_debug("collect_grid_items: found %d element children", count);

    if (count == 0) {
        *items = nullptr;
        return 0;
    }

    // Ensure we have enough space in the grid items array
    if (count > grid_layout->allocated_items) {
        grid_layout->allocated_items = count * 2;
        grid_layout->grid_items = (ViewBlock**)realloc(
            grid_layout->grid_items, grid_layout->allocated_items * sizeof(ViewBlock*));
    }

    // Collect items - ONLY collect element nodes, skip text nodes
    count = 0;
    child_node = container->first_child;
    while (child_node) {
        // CRITICAL FIX: Only process element nodes, skip text nodes
        if (!child_node->is_element()) {
            child_node = child_node->next_sibling;
            continue;
        }

        ViewBlock* child = (ViewBlock*)child_node;
        // Filter out absolutely positioned and hidden items
        bool is_absolute = child->position &&
                          (child->position->position == CSS_VALUE_ABSOLUTE ||
                           child->position->position == CSS_VALUE_FIXED);
        bool is_hidden = child->in_line && child->in_line->visibility == VIS_HIDDEN;
        if (!is_absolute && !is_hidden) {
            grid_layout->grid_items[count] = child;

            // Initialize grid item placement properties with defaults if not set
            // Note: Only initialize placement-related properties (row/column),
            // NOT alignment properties (justify_self/align_self_grid) which may be set via CSS
            bool has_explicit_placement = child->gi && (
                child->gi->grid_row_start != 0 || child->gi->grid_row_end != 0 ||
                child->gi->grid_column_start != 0 || child->gi->grid_column_end != 0);
            if (!has_explicit_placement && child->gi) {
                // Mark as auto-placed but preserve any CSS-set alignment properties
                child->gi->is_grid_auto_placed = true;
            }

            count++;
        }
        child_node = child_node->next_sibling;
    }

    grid_layout->item_count = count;

    // Sort items by CSS order property (stable sort - preserve DOM order for equal orders)
    // CSS Grid spec: items are placed in order-modified document order
    if (count > 1) {
        // Simple insertion sort (stable, good for small arrays)
        for (int i = 1; i < count; i++) {
            ViewBlock* key = grid_layout->grid_items[i];
            int key_order = key->gi ? key->gi->order : 0;
            int j = i - 1;
            while (j >= 0) {
                int j_order = grid_layout->grid_items[j]->gi ?
                             grid_layout->grid_items[j]->gi->order : 0;
                if (j_order > key_order) {
                    grid_layout->grid_items[j + 1] = grid_layout->grid_items[j];
                    j--;
                } else {
                    break;
                }
            }
            grid_layout->grid_items[j + 1] = key;
        }
    }

    *items = grid_layout->grid_items;
    return count;
}

// Place grid items in the grid
void place_grid_items(GridContainerLayout* grid_layout, ViewBlock** items, int item_count) {
    if (!grid_layout || !items || item_count == 0) return;

    log_debug("Placing %d grid items, area_count=%d\n", item_count, grid_layout->area_count);

    // Phase 1: Place items with explicit positions
    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        if (!item->gi) continue;  // Skip items without grid item properties

        // Debug: log grid_area status
        log_debug("Item %d: grid_area='%s', row_start=%d, col_start=%d",
                  i, item->gi->grid_area ? item->gi->grid_area : "NULL",
                  item->gi->grid_row_start, item->gi->grid_column_start);

        // Check if item has explicit grid positioning
        // Note: Negative values indicate span (e.g., -2 means "span 2")
        bool has_explicit_row = item->gi->grid_row_start != 0 || item->gi->grid_row_end != 0;
        bool has_explicit_column = item->gi->grid_column_start != 0 || item->gi->grid_column_end != 0;

        if (has_explicit_row || has_explicit_column || item->gi->grid_area) {

            if (item->gi->grid_area) {
                // Resolve named grid area
                log_debug("Looking up grid_area '%s' in %d areas", item->gi->grid_area, grid_layout->area_count);
                for (int j = 0; j < grid_layout->area_count; j++) {
                    log_debug("  Checking area[%d].name='%s'", j, grid_layout->grid_areas[j].name);
                    if (strcmp(grid_layout->grid_areas[j].name, item->gi->grid_area) == 0) {
                        item->gi->computed_grid_row_start = grid_layout->grid_areas[j].row_start;
                        item->gi->computed_grid_row_end = grid_layout->grid_areas[j].row_end;
                        item->gi->computed_grid_column_start = grid_layout->grid_areas[j].column_start;
                        item->gi->computed_grid_column_end = grid_layout->grid_areas[j].column_end;
                        log_debug("  MATCH! Setting computed positions: rows %d-%d, cols %d-%d",
                                  item->gi->computed_grid_row_start, item->gi->computed_grid_row_end,
                                  item->gi->computed_grid_column_start, item->gi->computed_grid_column_end);
                        break;
                    }
                }
            } else {
                // Use explicit line positions
                // Handle span values vs negative line numbers using is_span flags

                // Column placement
                int col_start = item->gi->grid_column_start;
                int col_end = item->gi->grid_column_end;
                bool col_start_is_span = item->gi->grid_column_start_is_span;
                bool col_end_is_span = item->gi->grid_column_end_is_span;

                if (col_start == 0 && col_end < 0 && col_end_is_span) {
                    // "span N" only - needs auto-placement for start
                    // Will be handled in auto-place phase
                    item->gi->computed_grid_column_start = 0;
                    item->gi->computed_grid_column_end = col_end;  // Keep span for auto-place
                } else if (col_start > 0 && col_end < 0 && col_end_is_span) {
                    // "N / span M" - explicit start, span end
                    int span = -col_end;
                    item->gi->computed_grid_column_start = col_start;
                    item->gi->computed_grid_column_end = col_start + span;
                } else if (col_start > 0 && col_end < 0 && !col_end_is_span) {
                    // "N / -M" - explicit start, negative line number end (e.g., 1 / -1)
                    // Negative line numbers count from the end: -1 = last line
                    int track_count = grid_layout->computed_column_count;
                    if (track_count == 0) track_count = grid_layout->explicit_column_count;
                    if (track_count == 0) track_count = 1;  // At least 1 column
                    int resolved_end = track_count + col_end + 2;  // +2 because lines are 1-indexed
                    item->gi->computed_grid_column_start = col_start;
                    item->gi->computed_grid_column_end = resolved_end;
                    log_debug("Resolved negative line: col_end=%d -> resolved_end=%d (track_count=%d)",
                              col_end, resolved_end, track_count);
                } else if (col_start < 0 && col_end > 0 && col_start_is_span) {
                    // "span N / M" - span start, explicit end
                    int span = -col_start;
                    item->gi->computed_grid_column_start = col_end - span;
                    item->gi->computed_grid_column_end = col_end;
                } else if (col_start > 0 && col_end == 0) {
                    // "N" only - single line, defaults to span 1
                    item->gi->computed_grid_column_start = col_start;
                    item->gi->computed_grid_column_end = col_start + 1;
                } else {
                    // Normal explicit positions (both positive or both need resolution)
                    item->gi->computed_grid_column_start = col_start;
                    item->gi->computed_grid_column_end = col_end;
                }

                // Row placement
                int row_start = item->gi->grid_row_start;
                int row_end = item->gi->grid_row_end;
                bool row_start_is_span = item->gi->grid_row_start_is_span;
                bool row_end_is_span = item->gi->grid_row_end_is_span;

                if (row_start == 0 && row_end < 0 && row_end_is_span) {
                    // "span N" only - needs auto-placement for start
                    item->gi->computed_grid_row_start = 0;
                    item->gi->computed_grid_row_end = row_end;  // Keep span for auto-place
                } else if (row_start > 0 && row_end < 0 && row_end_is_span) {
                    // "N / span M" - explicit start, span end
                    int span = -row_end;
                    item->gi->computed_grid_row_start = row_start;
                    item->gi->computed_grid_row_end = row_start + span;
                } else if (row_start > 0 && row_end < 0 && !row_end_is_span) {
                    // "N / -M" - explicit start, negative line number end (e.g., 1 / -1)
                    int track_count = grid_layout->computed_row_count;
                    if (track_count == 0) track_count = grid_layout->explicit_row_count;
                    if (track_count == 0) track_count = 1;  // At least 1 row
                    int resolved_end = track_count + row_end + 2;
                    item->gi->computed_grid_row_start = row_start;
                    item->gi->computed_grid_row_end = resolved_end;
                    log_debug("Resolved negative line: row_end=%d -> resolved_end=%d (track_count=%d)",
                              row_end, resolved_end, track_count);
                } else if (row_start < 0 && row_end > 0 && row_start_is_span) {
                    // "span N / M" - span start, explicit end
                    int span = -row_start;
                    item->gi->computed_grid_row_start = row_end - span;
                    item->gi->computed_grid_row_end = row_end;
                } else if (row_start > 0 && row_end == 0) {
                    // "N" only - single line, defaults to span 1
                    item->gi->computed_grid_row_start = row_start;
                    item->gi->computed_grid_row_end = row_start + 1;
                } else {
                    // Normal explicit positions
                    item->gi->computed_grid_row_start = row_start;
                    item->gi->computed_grid_row_end = row_end;
                }
            }

            // Check if we still need auto-placement (for "span N" without explicit start)
            if (item->gi->computed_grid_column_start == 0 || item->gi->computed_grid_row_start == 0) {
                item->gi->is_grid_auto_placed = true;
            } else {
                item->gi->is_grid_auto_placed = false;
            }

            log_debug("Explicit placement - item %d: row %d-%d, col %d-%d\n",
                      i, item->gi->computed_grid_row_start, item->gi->computed_grid_row_end,
                      item->gi->computed_grid_column_start, item->gi->computed_grid_column_end);
        }
    }

    // Phase 2: Auto-place remaining items
    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        if (!item->gi) continue;  // Skip items without grid item properties

        if (item->gi->is_grid_auto_placed) {
            auto_place_grid_item(grid_layout, item);

            log_debug("Auto placement - item %d: row %d-%d, col %d-%d\n",
                      i, item->gi->computed_grid_row_start, item->gi->computed_grid_row_end,
                      item->gi->computed_grid_column_start, item->gi->computed_grid_column_end);
        }
    }
}

// Auto-place a grid item (writes directly to item->gi->computed_* fields)
void auto_place_grid_item(GridContainerLayout* grid_layout, ViewBlock* item) {
    if (!grid_layout || !item || !item->gi) return;

    log_debug(" auto_place_grid_item called for item %p\n", item);

    // Check if item has explicit column or row positioning
    bool has_explicit_column = (item->gi->computed_grid_column_start > 0);
    bool has_explicit_row = (item->gi->computed_grid_row_start > 0);

    log_debug(" Explicit positioning: column=%d, row=%d\n", has_explicit_column, has_explicit_row);

    // Use grid_layout to track current auto-placement cursor
    // Note: These are initialized to 1 in init_grid_container_layout

    // Determine span sizes
    int col_span = 1;
    int row_span = 1;

    // Check if computed_grid_column_end has a span value (negative)
    if (item->gi->computed_grid_column_end < 0) {
        col_span = -item->gi->computed_grid_column_end;
    } else if (has_explicit_column) {
        // Calculate span from explicit start/end
        col_span = item->gi->computed_grid_column_end - item->gi->computed_grid_column_start;
    }

    if (item->gi->computed_grid_row_end < 0) {
        row_span = -item->gi->computed_grid_row_end;
    } else if (has_explicit_row) {
        // Calculate span from explicit start/end
        row_span = item->gi->computed_grid_row_end - item->gi->computed_grid_row_start;
    }

    log_debug(" Item span: %d cols x %d rows\n", col_span, row_span);

    // Determine grid dimensions from template
    int max_columns = grid_layout->explicit_column_count;
    int max_rows = grid_layout->explicit_row_count;

    // CSS Grid spec: Without explicit grid-template-columns, there's 1 implicit column
    if (max_columns <= 0) max_columns = 1;

    // If span is larger than max_columns, the grid must expand
    if (col_span > max_columns) {
        max_columns = col_span;
    }
    if (row_span > max_rows && max_rows > 0) {
        // For rows, only expand if we have explicit rows defined
    }

    log_debug(" Grid dimensions for auto-placement: %dx%d (cols x rows)\n", max_columns, max_rows);
    log_debug(" Current cursor: row=%d, col=%d\n", grid_layout->auto_row_cursor, grid_layout->auto_col_cursor);

    // Handle explicit column with auto row (e.g., "grid-column: 1 / span 2")
    if (has_explicit_column && !has_explicit_row) {
        log_debug(" Semi-explicit: column %d-%d explicit, finding row\n",
                  item->gi->computed_grid_column_start, item->gi->computed_grid_column_end);
        // Column is already set, just find first available row
        item->gi->computed_grid_row_start = grid_layout->auto_row_cursor;
        item->gi->computed_grid_row_end = grid_layout->auto_row_cursor + row_span;
        // Advance row cursor
        grid_layout->auto_row_cursor += row_span;
        log_debug(" Placed at row %d-%d\n", item->gi->computed_grid_row_start, item->gi->computed_grid_row_end);
        return;
    }

    // Handle explicit row with auto column (e.g., "grid-row: 2 / span 3")
    if (has_explicit_row && !has_explicit_column) {
        log_debug(" Semi-explicit: row %d-%d explicit, finding column\n",
                  item->gi->computed_grid_row_start, item->gi->computed_grid_row_end);
        // Row is already set, just find first available column
        item->gi->computed_grid_column_start = grid_layout->auto_col_cursor;
        item->gi->computed_grid_column_end = grid_layout->auto_col_cursor + col_span;
        // Advance column cursor
        grid_layout->auto_col_cursor += col_span;
        if (grid_layout->auto_col_cursor > max_columns) {
            grid_layout->auto_col_cursor = 1;
            grid_layout->auto_row_cursor++;
        }
        log_debug(" Placed at column %d-%d\n", item->gi->computed_grid_column_start, item->gi->computed_grid_column_end);
        return;
    }

    // Fully automatic placement (no explicit row or column)
    if (grid_layout->grid_auto_flow == CSS_VALUE_ROW) {
        // Place items row by row (default behavior)
        // Find a position where the item fits with its span
        int iterations = 0;
        bool placed = false;

        while (!placed && iterations < 1000) {
            iterations++;
            // Check if item fits at current position
            if (grid_layout->auto_col_cursor + col_span - 1 <= max_columns) {
                // Item fits in this row
                item->gi->computed_grid_column_start = grid_layout->auto_col_cursor;
                item->gi->computed_grid_column_end = grid_layout->auto_col_cursor + col_span;
                item->gi->computed_grid_row_start = grid_layout->auto_row_cursor;
                item->gi->computed_grid_row_end = grid_layout->auto_row_cursor + row_span;
                placed = true;

                // Advance cursor past this item
                grid_layout->auto_col_cursor += col_span;
                if (grid_layout->auto_col_cursor > max_columns) {
                    grid_layout->auto_col_cursor = 1;
                    grid_layout->auto_row_cursor++;
                }
            } else {
                // Doesn't fit, move to next row
                grid_layout->auto_col_cursor = 1;
                grid_layout->auto_row_cursor++;
            }
        }

        if (!placed) {
            log_error("Failed to auto-place grid item after %d iterations", iterations);
            // Force placement at cursor as fallback
            item->gi->computed_grid_column_start = 1;
            item->gi->computed_grid_column_end = 1 + col_span;
            item->gi->computed_grid_row_start = grid_layout->auto_row_cursor;
            item->gi->computed_grid_row_end = grid_layout->auto_row_cursor + row_span;
        }

        log_debug(" Placed item at row %d-%d, col %d-%d\n",
               item->gi->computed_grid_row_start, item->gi->computed_grid_row_end,
               item->gi->computed_grid_column_start, item->gi->computed_grid_column_end);
    } else {
        // Place items column by column (grid-auto-flow: column)
        if (max_rows <= 0) max_rows = 1;
        if (row_span > max_rows) {
            max_rows = row_span;
        }

        int iterations = 0;
        bool placed = false;

        while (!placed && iterations < 1000) {
            iterations++;
            if (grid_layout->auto_row_cursor + row_span - 1 <= max_rows) {
                item->gi->computed_grid_row_start = grid_layout->auto_row_cursor;
                item->gi->computed_grid_row_end = grid_layout->auto_row_cursor + row_span;
                item->gi->computed_grid_column_start = grid_layout->auto_col_cursor;
                item->gi->computed_grid_column_end = grid_layout->auto_col_cursor + col_span;
                placed = true;

                grid_layout->auto_row_cursor += row_span;
                if (grid_layout->auto_row_cursor > max_rows) {
                    grid_layout->auto_row_cursor = 1;
                    grid_layout->auto_col_cursor++;
                }
            } else {
                grid_layout->auto_row_cursor = 1;
                grid_layout->auto_col_cursor++;
            }
        }

        if (!placed) {
            log_error("Failed to auto-place grid item (column-flow) after %d iterations", iterations);
            item->gi->computed_grid_row_start = 1;
            item->gi->computed_grid_row_end = 1 + row_span;
            item->gi->computed_grid_column_start = grid_layout->auto_col_cursor;
            item->gi->computed_grid_column_end = grid_layout->auto_col_cursor + col_span;
        }

        log_debug(" Placed item at row %d-%d, col %d-%d (column-first)\n",
               item->gi->computed_grid_row_start, item->gi->computed_grid_row_end,
               item->gi->computed_grid_column_start, item->gi->computed_grid_column_end);
    }
}

// Determine the size of the grid
void determine_grid_size(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_debug(" Determining grid size\n");
    log_debug("Determining grid size\n");

    // Count explicit tracks from template
    grid_layout->explicit_row_count = grid_layout->grid_template_rows ?
                                     grid_layout->grid_template_rows->track_count : 0;
    grid_layout->explicit_column_count = grid_layout->grid_template_columns ?
                                        grid_layout->grid_template_columns->track_count : 0;

    log_debug(" Explicit tracks - rows: %d, columns: %d\n",
           grid_layout->explicit_row_count, grid_layout->explicit_column_count);

    // Find maximum implicit tracks needed based on item placement
    int max_row = grid_layout->explicit_row_count;
    int max_column = grid_layout->explicit_column_count;

    log_debug(" Checking %d items for grid size requirements\n", grid_layout->item_count);
    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* item = grid_layout->grid_items[i];
        if (!item->gi) continue;  // Skip items without grid item properties
        log_debug(" Item %d placement - row: %d-%d, col: %d-%d\n",
               i, item->gi->computed_grid_row_start, item->gi->computed_grid_row_end,
               item->gi->computed_grid_column_start, item->gi->computed_grid_column_end);

        // CRITICAL FIX: Grid positions are 1-indexed, but we need the actual track count
        // If an item ends at position 2, it uses tracks 0 and 1 (2 tracks total)
        max_row = fmax(max_row, item->gi->computed_grid_row_end - 1);
        max_column = fmax(max_column, item->gi->computed_grid_column_end - 1);
    }

    // Ensure minimum grid size matches explicit template
    if (max_row < grid_layout->explicit_row_count) max_row = grid_layout->explicit_row_count;
    if (max_column < grid_layout->explicit_column_count) max_column = grid_layout->explicit_column_count;

    grid_layout->implicit_row_count = max_row - grid_layout->explicit_row_count;
    grid_layout->implicit_column_count = max_column - grid_layout->explicit_column_count;

    // Ensure non-negative implicit counts
    if (grid_layout->implicit_row_count < 0) grid_layout->implicit_row_count = 0;
    if (grid_layout->implicit_column_count < 0) grid_layout->implicit_column_count = 0;

    grid_layout->computed_row_count = max_row;
    grid_layout->computed_column_count = max_column;

    log_debug(" Final grid size - rows: %d (%d explicit + %d implicit), cols: %d (%d explicit + %d implicit)\n",
           grid_layout->computed_row_count, grid_layout->explicit_row_count, grid_layout->implicit_row_count,
           grid_layout->computed_column_count, grid_layout->explicit_column_count, grid_layout->implicit_column_count);

    log_debug("Grid size determined - rows: %d (%d explicit + %d implicit), cols: %d (%d explicit + %d implicit)\n",
              grid_layout->computed_row_count, grid_layout->explicit_row_count, grid_layout->implicit_row_count,
              grid_layout->computed_column_count, grid_layout->explicit_column_count, grid_layout->implicit_column_count);
}

// Helper function to check if a view is a valid grid item
bool is_valid_grid_item(ViewBlock* item) {
    if (!item) return false;
    return item->view_type == RDT_VIEW_BLOCK || item->view_type == RDT_VIEW_INLINE_BLOCK;
}

// Helper function to check if a block is a grid item
bool is_grid_item(ViewBlock* block) {
    if (!block || !block->parent) return false;

    ViewBlock* parent = (ViewBlock*)block->parent;
    bool is_absolute = block->position &&
                      (block->position->position == CSS_VALUE_ABSOLUTE ||
                       block->position->position == CSS_VALUE_FIXED);
    bool is_hidden = block->in_line && block->in_line->visibility == VIS_HIDDEN;
    return parent->embed && parent->embed->grid && !is_absolute && !is_hidden;
}

// Calculate minimum size of a track pattern for auto-fill/auto-fit expansion
static int calculate_track_pattern_min_size(GridTrackSize** tracks, int track_count) {
    int pattern_size = 0;
    for (int i = 0; i < track_count; i++) {
        GridTrackSize* ts = tracks[i];
        if (!ts) continue;

        if (ts->type == GRID_TRACK_SIZE_LENGTH) {
            pattern_size += ts->value;
        } else if (ts->type == GRID_TRACK_SIZE_MINMAX && ts->min_size) {
            // Use the min value from minmax()
            if (ts->min_size->type == GRID_TRACK_SIZE_LENGTH) {
                pattern_size += ts->min_size->value;
            } else {
                pattern_size += 100; // Default for auto/min-content/max-content
            }
        } else if (ts->type == GRID_TRACK_SIZE_FR ||
                   ts->type == GRID_TRACK_SIZE_AUTO) {
            pattern_size += 100; // Default minimum for flexible/auto tracks
        } else {
            pattern_size += 50; // Fallback
        }
    }
    return pattern_size;
}

// Expand auto-fill/auto-fit repeat() tracks based on available space
void expand_auto_repeat_tracks(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    // Count items for auto-fit (need to limit tracks to item count)
    int item_count = grid_layout->item_count;

    // Check columns for auto-fill/auto-fit
    GridTrackList* cols = grid_layout->grid_template_columns;
    if (cols && cols->track_count > 0) {
        for (int i = 0; i < cols->track_count; i++) {
            GridTrackSize* ts = cols->tracks[i];
            if (!ts || ts->type != GRID_TRACK_SIZE_REPEAT) continue;
            if (!ts->is_auto_fill && !ts->is_auto_fit) continue;

            // Found an auto-fill/auto-fit repeat
            log_debug("GRID: Expanding auto-%s columns (available width: %d, item_count: %d)",
                      ts->is_auto_fill ? "fill" : "fit", grid_layout->content_width, item_count);

            int pattern_size = calculate_track_pattern_min_size(ts->repeat_tracks, ts->repeat_track_count);
            if (pattern_size <= 0) pattern_size = 100;

            // Account for gaps
            float gap = grid_layout->column_gap;
            int available = grid_layout->content_width;

            // Calculate how many repetitions fit
            // Formula: (available + gap) / (pattern_size + gap) = max repetitions
            int repeat_count = 1;
            if (pattern_size + gap > 0) {
                repeat_count = (int)((available + gap) / (pattern_size + gap));
                if (repeat_count < 1) repeat_count = 1;
            }

            log_debug("GRID: Pattern size=%d, gap=%.1f, available=%d -> %d repetitions (before auto-fit adjustment)",
                      pattern_size, gap, available, repeat_count);

            // For auto-fit, limit to the number of items (collapse empty tracks)
            // For auto-fill, keep all calculated tracks
            if (ts->is_auto_fit && item_count > 0 && repeat_count > item_count) {
                log_debug("GRID: auto-fit: reducing from %d to %d tracks to match item count",
                          repeat_count, item_count);
                repeat_count = item_count;
            }

            // Expand the track list
            int new_track_count = cols->track_count - 1 + (repeat_count * ts->repeat_track_count);
            GridTrackSize** new_tracks = (GridTrackSize**)mem_calloc(new_track_count, sizeof(GridTrackSize*), MEM_CAT_LAYOUT);
            if (!new_tracks) return;

            int dest = 0;
            // Copy tracks before the repeat
            for (int j = 0; j < i; j++) {
                new_tracks[dest++] = cols->tracks[j];
            }
            // Expand repeat tracks
            for (int r = 0; r < repeat_count; r++) {
                for (int t = 0; t < ts->repeat_track_count; t++) {
                    // Share the track size (don't duplicate)
                    new_tracks[dest++] = ts->repeat_tracks[t];
                }
            }
            // Copy tracks after the repeat
            for (int j = i + 1; j < cols->track_count; j++) {
                new_tracks[dest++] = cols->tracks[j];
            }

            // Replace track list (but don't free old one if shared)
            if (grid_layout->owns_template_columns) {
                mem_free(cols->tracks);
            }
            cols->tracks = new_tracks;
            cols->track_count = new_track_count;
            cols->allocated_tracks = new_track_count;
            cols->is_repeat = false; // No longer has unexpanded repeat

            log_debug("GRID: Expanded to %d column tracks", new_track_count);
            break; // Only one auto-repeat per axis allowed
        }
    }

    // Check rows for auto-fill/auto-fit (same logic)
    GridTrackList* rows = grid_layout->grid_template_rows;
    if (rows && rows->track_count > 0) {
        for (int i = 0; i < rows->track_count; i++) {
            GridTrackSize* ts = rows->tracks[i];
            if (!ts || ts->type != GRID_TRACK_SIZE_REPEAT) continue;
            if (!ts->is_auto_fill && !ts->is_auto_fit) continue;

            log_debug("GRID: Expanding auto-%s rows (available height: %d, item_count: %d)",
                      ts->is_auto_fill ? "fill" : "fit", grid_layout->content_height, item_count);

            int pattern_size = calculate_track_pattern_min_size(ts->repeat_tracks, ts->repeat_track_count);
            if (pattern_size <= 0) pattern_size = 100;

            float gap = grid_layout->row_gap;
            int available = grid_layout->content_height;

            int repeat_count = 1;
            if (pattern_size + gap > 0) {
                repeat_count = (int)((available + gap) / (pattern_size + gap));
                if (repeat_count < 1) repeat_count = 1;
            }

            log_debug("GRID: Row pattern size=%d, gap=%.1f, available=%d -> %d repetitions (before auto-fit adjustment)",
                      pattern_size, gap, available, repeat_count);

            // For auto-fit, limit to the number of row tracks needed for items
            // This is more complex since it depends on column count too
            // For simplicity, we don't collapse row tracks as aggressively
            // TODO: Calculate actual row requirements based on items and columns

            int new_track_count = rows->track_count - 1 + (repeat_count * ts->repeat_track_count);
            GridTrackSize** new_tracks = (GridTrackSize**)mem_calloc(new_track_count, sizeof(GridTrackSize*), MEM_CAT_LAYOUT);
            if (!new_tracks) return;

            int dest = 0;
            for (int j = 0; j < i; j++) {
                new_tracks[dest++] = rows->tracks[j];
            }
            for (int r = 0; r < repeat_count; r++) {
                for (int t = 0; t < ts->repeat_track_count; t++) {
                    new_tracks[dest++] = ts->repeat_tracks[t];
                }
            }
            for (int j = i + 1; j < rows->track_count; j++) {
                new_tracks[dest++] = rows->tracks[j];
            }

            if (grid_layout->owns_template_rows) {
                mem_free(rows->tracks);
            }
            rows->tracks = new_tracks;
            rows->track_count = new_track_count;
            rows->allocated_tracks = new_track_count;
            rows->is_repeat = false;

            log_debug("GRID: Expanded to %d row tracks", new_track_count);
            break;
        }
    }
}
