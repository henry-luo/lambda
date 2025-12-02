#include "grid.hpp"
#include "layout.hpp"
#include "view.hpp"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../lib/log.h"
}

// Initialize grid container layout state
void init_grid_container(LayoutContext* lycon, ViewBlock* container) {
    if (!container) return;
    log_debug("Initializing grid container for %p\n", container);

    GridContainerLayout* grid = (GridContainerLayout*)calloc(1, sizeof(GridContainerLayout));
    lycon->grid_container = grid;
    grid->lycon = lycon;  // Store layout context for intrinsic sizing

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
    grid->grid_items = (ViewBlock**)calloc(grid->allocated_items, sizeof(ViewBlock*));
    grid->allocated_areas = 4;
    grid->grid_areas = (GridArea*)calloc(grid->allocated_areas, sizeof(GridArea));
    grid->allocated_line_names = 8;
    grid->line_names = (GridLineName*)calloc(grid->allocated_line_names, sizeof(GridLineName));

    // Initialize track lists
    grid->grid_template_rows = create_grid_track_list(4);
    grid->grid_template_columns = create_grid_track_list(4);
    grid->grid_auto_rows = create_grid_track_list(2);
    grid->grid_auto_columns = create_grid_track_list(2);

    grid->needs_reflow = false;

    log_debug("Grid container initialized successfully\n");
}

// Cleanup grid container resources
void cleanup_grid_container(LayoutContext* lycon) {
    if (!lycon || !lycon->grid_container) return;
    log_debug("Cleaning up grid container for %p\n", lycon->grid_container);
    GridContainerLayout* grid = lycon->grid_container;

    // Free track lists
    destroy_grid_track_list(grid->grid_template_rows);
    destroy_grid_track_list(grid->grid_template_columns);
    destroy_grid_track_list(grid->grid_auto_rows);
    destroy_grid_track_list(grid->grid_auto_columns);

    // Free computed tracks
    if (grid->computed_rows) {
        for (int i = 0; i < grid->computed_row_count; i++) {
            if (grid->computed_rows[i].size) {
                destroy_grid_track_size(grid->computed_rows[i].size);
            }
        }
        free(grid->computed_rows);
    }

    if (grid->computed_columns) {
        for (int i = 0; i < grid->computed_column_count; i++) {
            if (grid->computed_columns[i].size) {
                destroy_grid_track_size(grid->computed_columns[i].size);
            }
        }
        free(grid->computed_columns);
    }

    // Free grid areas
    for (int i = 0; i < grid->area_count; i++) {
        destroy_grid_area(&grid->grid_areas[i]);
    }
    free(grid->grid_areas);

    // Free line names
    for (int i = 0; i < grid->line_name_count; i++) {
        free(grid->line_names[i].name);
    }
    free(grid->line_names);

    free(grid->grid_items);
    free(grid);
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

    // Set container dimensions
    grid_layout->container_width = container->width;
    grid_layout->container_height = container->height;

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

    // Phase 1: Collect grid items
    log_debug("DEBUG: Phase 1 - Collecting grid items");
    ViewBlock** items;
    int item_count = collect_grid_items(grid_layout, container, &items);

    log_debug("GRID - collected %d items", item_count);
    if (item_count == 0) {
        log_debug("No grid items found");
        return;
    }

    // Phase 2: Resolve grid template areas
    log_debug("DEBUG: Phase 2 - Resolving grid template areas");
    resolve_grid_template_areas(grid_layout);

    // Phase 3: Place grid items (with dense packing if enabled)
    log_debug("DEBUG: Phase 3 - Placing grid items");
    if (grid_layout->is_dense_packing) {
        auto_place_grid_items_dense(grid_layout);
    } else {
        place_grid_items(grid_layout, items, item_count);
    }

    // Phase 4: Determine grid size
    log_debug("DEBUG: Phase 4 - Determining grid size");
    determine_grid_size(grid_layout);

    // Phase 5: Resolve track sizes
    log_debug("DEBUG: Phase 5 - Resolving track sizes");
    resolve_track_sizes(grid_layout, container);

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

            // Initialize grid item properties with defaults if not set
            bool has_explicit_placement = child->gi && (
                child->gi->grid_row_start != 0 || child->gi->grid_row_end != 0 ||
                child->gi->grid_column_start != 0 || child->gi->grid_column_end != 0);
            if (!has_explicit_placement) {
                // Item has default/uninitialized grid properties - set proper defaults
                // Note: gi is allocated elsewhere, here we just mark as auto-placed
                if (child->gi) {
                    child->gi->grid_row_start = 0;    // auto
                    child->gi->grid_row_end = 0;      // auto
                    child->gi->grid_column_start = 0; // auto
                    child->gi->grid_column_end = 0;   // auto
                    child->gi->justify_self = CSS_VALUE_AUTO;
                    child->gi->align_self_grid = CSS_VALUE_AUTO;
                    child->gi->is_grid_auto_placed = true;
                }
            }

            count++;
        }
        child_node = child_node->next_sibling;
    }

    grid_layout->item_count = count;
    *items = grid_layout->grid_items;
    return count;
}

// Place grid items in the grid
void place_grid_items(GridContainerLayout* grid_layout, ViewBlock** items, int item_count) {
    if (!grid_layout || !items || item_count == 0) return;

    log_debug("Placing %d grid items\n", item_count);

    // Phase 1: Place items with explicit positions
    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        if (!item->gi) continue;  // Skip items without grid item properties

        // Check if item has explicit grid positioning
        if (item->gi->grid_row_start > 0 || item->gi->grid_row_end > 0 ||
            item->gi->grid_column_start > 0 || item->gi->grid_column_end > 0 ||
            item->gi->grid_area) {

            if (item->gi->grid_area) {
                // Resolve named grid area
                for (int j = 0; j < grid_layout->area_count; j++) {
                    if (strcmp(grid_layout->grid_areas[j].name, item->gi->grid_area) == 0) {
                        item->gi->computed_grid_row_start = grid_layout->grid_areas[j].row_start;
                        item->gi->computed_grid_row_end = grid_layout->grid_areas[j].row_end;
                        item->gi->computed_grid_column_start = grid_layout->grid_areas[j].column_start;
                        item->gi->computed_grid_column_end = grid_layout->grid_areas[j].column_end;
                        break;
                    }
                }
            } else {
                // Use explicit line positions
                item->gi->computed_grid_row_start = item->gi->grid_row_start;
                item->gi->computed_grid_row_end = item->gi->grid_row_end;
                item->gi->computed_grid_column_start = item->gi->grid_column_start;
                item->gi->computed_grid_column_end = item->gi->grid_column_end;
            }
            item->gi->is_grid_auto_placed = false;

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

    // Enhanced auto-placement algorithm that respects grid dimensions
    static int current_row = 1;
    static int current_column = 1;
    static int item_counter = 0;

    // Reset counters for new grid layout (simple heuristic)
    if (item_counter == 0) {
        current_row = 1;
        current_column = 1;
    }

    // Determine grid dimensions from template
    int max_columns = grid_layout->explicit_column_count;
    int max_rows = grid_layout->explicit_row_count;

    // CSS Grid spec: Without explicit grid-template-columns, there's 1 implicit column
    // Items are placed in a single column and stack vertically (for row flow)
    // Without explicit grid-template-rows, rows are created implicitly as needed
    if (max_columns <= 0) max_columns = 1;  // Single column when no template
    // max_rows can remain 0 - rows are created implicitly

    log_debug(" Grid dimensions for auto-placement: %dx%d (cols x rows)\n", max_columns, max_rows);

    if (grid_layout->grid_auto_flow == CSS_VALUE_ROW) {
        // Place items row by row (default behavior)
        item->gi->computed_grid_row_start = current_row;
        item->gi->computed_grid_row_end = current_row + 1;
        item->gi->computed_grid_column_start = current_column;
        item->gi->computed_grid_column_end = current_column + 1;

        log_debug(" Placing item %d at row %d, col %d\n", item_counter, current_row, current_column);

        // Advance to next position
        current_column++;
        if (current_column > max_columns) {
            current_column = 1;
            current_row++;
        }
    } else {
        // Place items column by column (grid-auto-flow: column)
        // Without explicit template, there's 1 implicit row
        if (max_rows <= 0) max_rows = 1;

        item->gi->computed_grid_column_start = current_column;
        item->gi->computed_grid_column_end = current_column + 1;
        item->gi->computed_grid_row_start = current_row;
        item->gi->computed_grid_row_end = current_row + 1;

        log_debug(" Placing item %d at row %d, col %d (column-first)\n", item_counter, current_row, current_column);

        // Advance to next position
        current_row++;
        if (current_row > max_rows) {
            current_row = 1;
            current_column++;
        }
    }

    item_counter++;

    // Reset counter when we've placed all items (simple heuristic)
    if (item_counter >= grid_layout->item_count) {
        item_counter = 0;
    }

    log_debug(" Auto-placed item at row %d-%d, col %d-%d\n",
           item->gi->computed_grid_row_start, item->gi->computed_grid_row_end,
           item->gi->computed_grid_column_start, item->gi->computed_grid_column_end);
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
