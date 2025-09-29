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
void init_grid_container(ViewBlock* container) {
    if (!container) return;
    
    log_debug("Initializing grid container for %p\n", container);
    
    // Create embed structure if it doesn't exist
    if (!container->embed) {
        container->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
    }
    
    GridContainerLayout* grid = (GridContainerLayout*)calloc(1, sizeof(GridContainerLayout));
    container->embed->grid_container = grid;
    
    // Set default values using enum names that align with Lexbor constants
    grid->justify_content = LXB_CSS_VALUE_START;
    grid->align_content = LXB_CSS_VALUE_START;
    grid->justify_items = LXB_CSS_VALUE_STRETCH;
    grid->align_items = LXB_CSS_VALUE_STRETCH;
    grid->grid_auto_flow = LXB_CSS_VALUE_ROW;
    
    // Initialize gaps
    grid->row_gap = 0;
    grid->column_gap = 0;
    
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
void cleanup_grid_container(ViewBlock* container) {
    if (!container || !container->embed || !container->embed->grid_container) return;
    
    log_debug("Cleaning up grid container for %p\n", container);
    
    GridContainerLayout* grid = container->embed->grid_container;
    
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
    container->embed->grid_container = nullptr;
    
    log_debug("Grid container cleanup complete\n");
}

// Main grid layout algorithm entry point
void layout_grid_container_new(LayoutContext* lycon, ViewBlock* container) {
    printf("DEBUG: layout_grid_container_new called with container=%p\n", container);
    if (!container) {
        printf("DEBUG: Early return - container is NULL\n");
        return;
    }
    if (!container->embed) {
        printf("DEBUG: Early return - container->embed is NULL\n");
        return;
    }
    if (!container->embed->grid_container) {
        printf("DEBUG: Early return - container->embed->grid_container is NULL\n");
        return;
    }
    if (!container || !container->embed || !container->embed->grid_container) {
        log_debug("Early return - missing container or grid properties\n");
        return;
    }
    
    GridContainerLayout* grid_layout = container->embed->grid_container;
    
    printf("DEBUG: Grid container found - template_columns=%p, template_rows=%p\n", 
           grid_layout->grid_template_columns, grid_layout->grid_template_rows);
    if (grid_layout->grid_template_columns) {
        printf("DEBUG: Template columns track count: %d\n", grid_layout->grid_template_columns->track_count);
    }
    if (grid_layout->grid_template_rows) {
        printf("DEBUG: Template rows track count: %d\n", grid_layout->grid_template_rows->track_count);
    }
    
    log_debug("GRID START - container: %dx%d at (%d,%d)\n", 
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
    printf("DEBUG: Phase 1 - Collecting grid items\n");
    ViewBlock** items;
    int item_count = collect_grid_items(container, &items);
    
    printf("DEBUG: GRID - collected %d items\n", item_count);
    log_debug("GRID - collected %d items\n", item_count);
    
    if (item_count == 0) {
        printf("DEBUG: No grid items found, returning early\n");
        log_debug("No grid items found\n");
        return;
    }
    
    // Phase 2: Resolve grid template areas
    printf("DEBUG: Phase 2 - Resolving grid template areas\n");
    resolve_grid_template_areas(grid_layout);
    
    // Phase 3: Place grid items (with dense packing if enabled)
    printf("DEBUG: Phase 3 - Placing grid items\n");
    if (grid_layout->is_dense_packing) {
        auto_place_grid_items_dense(grid_layout);
    } else {
        place_grid_items(grid_layout, items, item_count);
    }
    
    // Phase 4: Determine grid size
    printf("DEBUG: Phase 4 - Determining grid size\n");
    determine_grid_size(grid_layout);
    
    // Phase 5: Resolve track sizes
    printf("DEBUG: Phase 5 - Resolving track sizes\n");
    resolve_track_sizes(grid_layout, container);
    
    // Phase 6: Position grid items
    printf("DEBUG: Phase 6 - Positioning grid items\n");
    position_grid_items(grid_layout, container);
    
    // Phase 7: Align grid items
    printf("DEBUG: Phase 7 - Aligning grid items\n");
    align_grid_items(grid_layout);
    
    // Debug: Final item positions
    printf("DEBUG: FINAL GRID POSITIONS:\n");
    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        printf("DEBUG: FINAL_GRID_ITEM %d - pos: (%d,%d), size: %dx%d, grid_area: (%d-%d, %d-%d)\n", 
               i, item->x, item->y, item->width, item->height,
               item->computed_grid_row_start, item->computed_grid_row_end,
               item->computed_grid_column_start, item->computed_grid_column_end);
    }
    log_debug("FINAL GRID POSITIONS:\n");
    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        log_debug("FINAL_GRID_ITEM %d - pos: (%d,%d), size: %dx%d\n", 
                  i, item->x, item->y, item->width, item->height);
    }
    
    grid_layout->needs_reflow = false;
}

// Collect grid items from container children
int collect_grid_items(ViewBlock* container, ViewBlock*** items) {
    printf("DEBUG: collect_grid_items called with container=%p, items=%p\n", container, items);
    if (!container || !items) {
        printf("DEBUG: Early return - container=%p, items=%p\n", container, items);
        return 0;
    }
    
    GridContainerLayout* grid = container->embed->grid_container;
    printf("DEBUG: grid=%p\n", grid);
    if (!grid) {
        printf("DEBUG: Early return - grid is NULL\n");
        return 0;
    }
    
    int count = 0;
    
    // Count children first - use ViewBlock hierarchy for grid items
    printf("DEBUG: About to access container->first_child\n");
    ViewBlock* child = container->first_child;
    printf("DEBUG: first_child=%p\n", child);
    while (child) {
        // Filter out absolutely positioned and hidden items
        bool is_absolute = child->position && 
                          (child->position->position == LXB_CSS_VALUE_ABSOLUTE || 
                           child->position->position == LXB_CSS_VALUE_FIXED);
        bool is_hidden = child->visibility == VIS_HIDDEN;
        if (!is_absolute && !is_hidden) {
            count++;
        }
        child = child->next_sibling;
    }
    
    if (count == 0) {
        *items = nullptr;
        return 0;
    }
    
    // Ensure we have enough space in the grid items array
    if (count > grid->allocated_items) {
        grid->allocated_items = count * 2;
        grid->grid_items = (ViewBlock**)realloc(grid->grid_items, 
                                               grid->allocated_items * sizeof(ViewBlock*));
    }
    
    // Collect items - use ViewBlock hierarchy for grid items
    count = 0;
    child = container->first_child;
    while (child) {
        // Filter out absolutely positioned and hidden items
        bool is_absolute = child->position && 
                          (child->position->position == LXB_CSS_VALUE_ABSOLUTE || 
                           child->position->position == LXB_CSS_VALUE_FIXED);
        bool is_hidden = child->visibility == VIS_HIDDEN;
        if (!is_absolute && !is_hidden) {
            grid->grid_items[count] = child;
            
            // Initialize grid item properties with defaults if not set
            if (child->grid_row_start == 0 && child->grid_row_end == 0 &&
                child->grid_column_start == 0 && child->grid_column_end == 0) {
                // Item has default/uninitialized grid properties - set proper defaults
                child->grid_row_start = 0;    // auto
                child->grid_row_end = 0;      // auto
                child->grid_column_start = 0; // auto
                child->grid_column_end = 0;   // auto
                child->justify_self = LXB_CSS_VALUE_AUTO;
                child->align_self_grid = LXB_CSS_VALUE_AUTO;
                child->is_grid_auto_placed = true;
            }
            
            count++;
        }
        child = child->next_sibling;
    }
    
    grid->item_count = count;
    *items = grid->grid_items;
    return count;
}

// Place grid items in the grid
void place_grid_items(GridContainerLayout* grid_layout, ViewBlock** items, int item_count) {
    if (!grid_layout || !items || item_count == 0) return;
    
    log_debug("Placing %d grid items\n", item_count);
    
    // Phase 1: Place items with explicit positions
    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        
        // Check if item has explicit grid positioning
        if (item->grid_row_start > 0 || item->grid_row_end > 0 ||
            item->grid_column_start > 0 || item->grid_column_end > 0 ||
            item->grid_area) {
            
            GridItemPlacement placement = {0};
            
            if (item->grid_area) {
                // Resolve named grid area
                for (int j = 0; j < grid_layout->area_count; j++) {
                    if (strcmp(grid_layout->grid_areas[j].name, item->grid_area) == 0) {
                        placement.row_start = grid_layout->grid_areas[j].row_start;
                        placement.row_end = grid_layout->grid_areas[j].row_end;
                        placement.column_start = grid_layout->grid_areas[j].column_start;
                        placement.column_end = grid_layout->grid_areas[j].column_end;
                        break;
                    }
                }
            } else {
                // Use explicit line positions
                placement.row_start = item->grid_row_start;
                placement.row_end = item->grid_row_end;
                placement.column_start = item->grid_column_start;
                placement.column_end = item->grid_column_end;
            }
            
            // Store computed positions
            item->computed_grid_row_start = placement.row_start;
            item->computed_grid_row_end = placement.row_end;
            item->computed_grid_column_start = placement.column_start;
            item->computed_grid_column_end = placement.column_end;
            item->is_grid_auto_placed = false;
            
            log_debug("Explicit placement - item %d: row %d-%d, col %d-%d\n", 
                      i, placement.row_start, placement.row_end,
                      placement.column_start, placement.column_end);
        }
    }
    
    // Phase 2: Auto-place remaining items
    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        
        if (item->is_grid_auto_placed) {
            GridItemPlacement placement = {0};
            auto_place_grid_item(grid_layout, item, &placement);
            
            item->computed_grid_row_start = placement.row_start;
            item->computed_grid_row_end = placement.row_end;
            item->computed_grid_column_start = placement.column_start;
            item->computed_grid_column_end = placement.column_end;
            
            log_debug("Auto placement - item %d: row %d-%d, col %d-%d\n", 
                      i, placement.row_start, placement.row_end,
                      placement.column_start, placement.column_end);
        }
    }
}

// Auto-place a grid item
void auto_place_grid_item(GridContainerLayout* grid_layout, ViewBlock* item, GridItemPlacement* placement) {
    if (!grid_layout || !item || !placement) return;
    
    printf("DEBUG: auto_place_grid_item called for item %p\n", item);
    
    // Enhanced auto-placement algorithm that respects grid dimensions
    static int current_row = 1;
    static int current_column = 1;
    static int item_counter = 0;
    
    // Reset counters for new grid layout (simple heuristic)
    if (item_counter == 0) {
        current_row = 1;
        current_column = 1;
    }
    
    // Determine grid dimensions from template or use defaults
    int max_columns = grid_layout->explicit_column_count;
    int max_rows = grid_layout->explicit_row_count;
    
    // If no explicit template, try to infer from computed dimensions
    if (max_columns <= 0) {
        max_columns = grid_layout->computed_column_count;
    }
    if (max_rows <= 0) {
        max_rows = grid_layout->computed_row_count;
    }
    
    // Fallback to reasonable defaults for 2x2 grid
    if (max_columns <= 0) max_columns = 2;
    if (max_rows <= 0) max_rows = 2;
    
    printf("DEBUG: Grid dimensions for auto-placement: %dx%d (cols x rows)\n", max_columns, max_rows);
    
    if (grid_layout->grid_auto_flow == LXB_CSS_VALUE_ROW) {
        // Place items row by row (default behavior)
        placement->row_start = current_row;
        placement->row_end = current_row + 1;
        placement->column_start = current_column;
        placement->column_end = current_column + 1;
        
        printf("DEBUG: Placing item %d at row %d, col %d\n", item_counter, current_row, current_column);
        
        // Advance to next position
        current_column++;
        if (current_column > max_columns) {
            current_column = 1;
            current_row++;
        }
    } else {
        // Place items column by column
        placement->column_start = current_column;
        placement->column_end = current_column + 1;
        placement->row_start = current_row;
        placement->row_end = current_row + 1;
        
        printf("DEBUG: Placing item %d at row %d, col %d (column-first)\n", item_counter, current_row, current_column);
        
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
    
    printf("DEBUG: Auto-placed item at row %d-%d, col %d-%d\n", 
           placement->row_start, placement->row_end,
           placement->column_start, placement->column_end);
    log_debug("Auto-placed item at row %d-%d, col %d-%d\n", 
              placement->row_start, placement->row_end,
              placement->column_start, placement->column_end);
}

// Determine the size of the grid
void determine_grid_size(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;
    
    printf("DEBUG: Determining grid size\n");
    log_debug("Determining grid size\n");
    
    // Count explicit tracks from template
    grid_layout->explicit_row_count = grid_layout->grid_template_rows ? 
                                     grid_layout->grid_template_rows->track_count : 0;
    grid_layout->explicit_column_count = grid_layout->grid_template_columns ? 
                                        grid_layout->grid_template_columns->track_count : 0;
    
    printf("DEBUG: Explicit tracks - rows: %d, columns: %d\n", 
           grid_layout->explicit_row_count, grid_layout->explicit_column_count);
    
    // Find maximum implicit tracks needed based on item placement
    int max_row = grid_layout->explicit_row_count;
    int max_column = grid_layout->explicit_column_count;
    
    printf("DEBUG: Checking %d items for grid size requirements\n", grid_layout->item_count);
    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* item = grid_layout->grid_items[i];
        printf("DEBUG: Item %d placement - row: %d-%d, col: %d-%d\n", 
               i, item->computed_grid_row_start, item->computed_grid_row_end,
               item->computed_grid_column_start, item->computed_grid_column_end);
        
        // CRITICAL FIX: Grid positions are 1-indexed, but we need the actual track count
        // If an item ends at position 2, it uses tracks 0 and 1 (2 tracks total)
        max_row = fmax(max_row, item->computed_grid_row_end - 1);
        max_column = fmax(max_column, item->computed_grid_column_end - 1);
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
    
    printf("DEBUG: Final grid size - rows: %d (%d explicit + %d implicit), cols: %d (%d explicit + %d implicit)\n",
           grid_layout->computed_row_count, grid_layout->explicit_row_count, grid_layout->implicit_row_count,
           grid_layout->computed_column_count, grid_layout->explicit_column_count, grid_layout->implicit_column_count);
    
    log_debug("Grid size determined - rows: %d (%d explicit + %d implicit), cols: %d (%d explicit + %d implicit)\n",
              grid_layout->computed_row_count, grid_layout->explicit_row_count, grid_layout->implicit_row_count,
              grid_layout->computed_column_count, grid_layout->explicit_column_count, grid_layout->implicit_column_count);
}

// Helper function to check if a view is a valid grid item
bool is_valid_grid_item(ViewBlock* item) {
    if (!item) return false;
    return item->type == RDT_VIEW_BLOCK || item->type == RDT_VIEW_INLINE_BLOCK;
}

// Helper function to check if a block is a grid item
bool is_grid_item(ViewBlock* block) {
    if (!block || !block->parent) return false;
    
    ViewBlock* parent = (ViewBlock*)block->parent;
    bool is_absolute = block->position && 
                      (block->position->position == LXB_CSS_VALUE_ABSOLUTE || 
                       block->position->position == LXB_CSS_VALUE_FIXED);
    bool is_hidden = block->visibility == VIS_HIDDEN;
    return parent->embed && 
           parent->embed->grid_container && 
           !is_absolute &&
           !is_hidden;
}
