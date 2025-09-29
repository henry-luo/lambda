#include "grid.hpp"
#include "view.hpp"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../lib/log.h"
}

// Position grid items based on computed track sizes
void position_grid_items(GridContainerLayout* grid_layout, ViewBlock* container) {
    if (!grid_layout || !container) return;
    
    log_debug("Positioning grid items\n");
    
    // Calculate track positions
    int* row_positions = (int*)calloc(grid_layout->computed_row_count + 1, sizeof(int));
    int* column_positions = (int*)calloc(grid_layout->computed_column_count + 1, sizeof(int));
    
    // Calculate row positions
    int current_y = 0;
    for (int i = 0; i <= grid_layout->computed_row_count; i++) {
        row_positions[i] = current_y;
        if (i < grid_layout->computed_row_count) {
            current_y += grid_layout->computed_rows[i].computed_size;
            if (i < grid_layout->computed_row_count - 1) {
                current_y += grid_layout->row_gap;
            }
        }
    }
    
    // Calculate column positions
    int current_x = 0;
    for (int i = 0; i <= grid_layout->computed_column_count; i++) {
        column_positions[i] = current_x;
        if (i < grid_layout->computed_column_count) {
            current_x += grid_layout->computed_columns[i].computed_size;
            if (i < grid_layout->computed_column_count - 1) {
                current_x += grid_layout->column_gap;
            }
        }
    }
    
    // Position each grid item
    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* item = grid_layout->grid_items[i];
        
        // Get grid area bounds (convert from 1-indexed to 0-indexed)
        int row_start = item->computed_grid_row_start - 1;
        int row_end = item->computed_grid_row_end - 1;
        int col_start = item->computed_grid_column_start - 1;
        int col_end = item->computed_grid_column_end - 1;
        
        // Clamp to valid ranges
        row_start = fmax(0, fmin(row_start, grid_layout->computed_row_count - 1));
        row_end = fmax(row_start + 1, fmin(row_end, grid_layout->computed_row_count));
        col_start = fmax(0, fmin(col_start, grid_layout->computed_column_count - 1));
        col_end = fmax(col_start + 1, fmin(col_end, grid_layout->computed_column_count));
        
        // Calculate item position and size
        int item_x = column_positions[col_start];
        int item_y = row_positions[row_start];
        int item_width = column_positions[col_end] - column_positions[col_start];
        int item_height = row_positions[row_end] - row_positions[row_start];
        
        // Subtract gaps from size (gaps are between tracks, not part of item area)
        int col_gaps = col_end - col_start - 1;
        int row_gaps = row_end - row_start - 1;
        item_width -= col_gaps * grid_layout->column_gap;
        item_height -= row_gaps * grid_layout->row_gap;
        
        // Apply container offset (borders and padding)
        int container_offset_x = 0;
        int container_offset_y = 0;
        
        if (container->bound) {
            container_offset_x += container->bound->padding.left;
            container_offset_y += container->bound->padding.top;
        }
        
        if (container->bound && container->bound->border) {
            container_offset_x += container->bound->border->width.left;
            container_offset_y += container->bound->border->width.top;
        }
        
        // Set item position and size
        item->x = container->x + container_offset_x + item_x;
        item->y = container->y + container_offset_y + item_y;
        item->width = item_width;
        item->height = item_height;
        
        log_debug("Positioned grid item %d: pos=(%d,%d), size=%dx%d, grid_area=(%d-%d, %d-%d)\n",
                  i, item->x, item->y, item->width, item->height,
                  row_start + 1, row_end, col_start + 1, col_end);
    }
    
    free(row_positions);
    free(column_positions);
    
    log_debug("Grid items positioned\n");
}

// Align all grid items
void align_grid_items(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;
    
    log_debug("Aligning grid items\n");
    
    for (int i = 0; i < grid_layout->item_count; i++) {
        align_grid_item(grid_layout->grid_items[i], grid_layout);
    }
    
    log_debug("Grid items aligned\n");
}

// Align a single grid item
void align_grid_item(ViewBlock* item, GridContainerLayout* grid_layout) {
    if (!item || !grid_layout) return;
    
    // Get the item's grid area dimensions
    int row_start = item->computed_grid_row_start - 1;
    int row_end = item->computed_grid_row_end - 1;
    int col_start = item->computed_grid_column_start - 1;
    int col_end = item->computed_grid_column_end - 1;
    
    // Calculate available space in the grid area
    int available_width = 0;
    for (int i = col_start; i < col_end && i < grid_layout->computed_column_count; i++) {
        available_width += grid_layout->computed_columns[i].computed_size;
    }
    available_width += (col_end - col_start - 1) * grid_layout->column_gap;
    
    int available_height = 0;
    for (int i = row_start; i < row_end && i < grid_layout->computed_row_count; i++) {
        available_height += grid_layout->computed_rows[i].computed_size;
    }
    available_height += (row_end - row_start - 1) * grid_layout->row_gap;
    
    // Apply justify-self (horizontal alignment)
    int justify = (item->justify_self != LXB_CSS_VALUE_AUTO) ? 
                  item->justify_self : grid_layout->justify_items;
    
    switch (justify) {
        case LXB_CSS_VALUE_START:
            // Already positioned at start
            break;
            
        case LXB_CSS_VALUE_END:
            item->x += (available_width - item->width);
            break;
            
        case LXB_CSS_VALUE_CENTER:
            item->x += (available_width - item->width) / 2;
            break;
            
        case LXB_CSS_VALUE_STRETCH:
            item->width = available_width;
            break;
            
        default:
            // Default to stretch for grid items
            item->width = available_width;
            break;
    }
    
    // Apply align-self (vertical alignment)
    int align = (item->align_self_grid != LXB_CSS_VALUE_AUTO) ? 
                item->align_self_grid : grid_layout->align_items;
    
    switch (align) {
        case LXB_CSS_VALUE_START:
            // Already positioned at start
            break;
            
        case LXB_CSS_VALUE_END:
            item->y += (available_height - item->height);
            break;
            
        case LXB_CSS_VALUE_CENTER:
            item->y += (available_height - item->height) / 2;
            break;
            
        case LXB_CSS_VALUE_STRETCH:
            item->height = available_height;
            break;
            
        default:
            // Default to stretch for grid items
            item->height = available_height;
            break;
    }
    
    log_debug("Aligned grid item: justify=%d, align=%d, final_pos=(%d,%d), final_size=%dx%d\n",
              justify, align, item->x, item->y, item->width, item->height);
}
