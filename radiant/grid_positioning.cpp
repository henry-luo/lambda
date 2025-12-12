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

    log_debug(" Positioning grid items - container: %.0fx%.0f at (%.0f,%.0f)\n",
           container->width, container->height, container->x, container->y);
    log_debug(" Grid content dimensions: %dx%d\n",
           grid_layout->content_width, grid_layout->content_height);
    log_debug(" Grid gaps - row: %.1f, column: %.1f\n",
           grid_layout->row_gap, grid_layout->column_gap);
    log_debug("Positioning grid items\n");

    // Calculate track positions
    int* row_positions = (int*)calloc(grid_layout->computed_row_count + 1, sizeof(int));
    int* column_positions = (int*)calloc(grid_layout->computed_column_count + 1, sizeof(int));

    // Calculate row positions
    int current_y = 0;
    log_debug(" Calculating row positions for %d rows:\n", grid_layout->computed_row_count);
    for (int i = 0; i <= grid_layout->computed_row_count; i++) {
        row_positions[i] = current_y;
        log_debug(" Row %d position: %d\n", i, current_y);
        if (i < grid_layout->computed_row_count) {
            int track_size = grid_layout->computed_rows[i].computed_size;
            log_debug(" Row %d size: %d\n", i, track_size);
            current_y += track_size;
            if (i < grid_layout->computed_row_count - 1) {
                current_y += (int)grid_layout->row_gap;
                log_debug(" Added row gap: %.1f, new current_y: %d\n", grid_layout->row_gap, current_y);
            }
        }
    }

    // Calculate column positions
    int current_x = 0;
    log_debug(" Calculating column positions for %d columns:\n", grid_layout->computed_column_count);
    for (int i = 0; i <= grid_layout->computed_column_count; i++) {
        column_positions[i] = current_x;
        log_debug(" Column %d position: %d\n", i, current_x);
        if (i < grid_layout->computed_column_count) {
            int track_size = grid_layout->computed_columns[i].computed_size;
            log_debug(" Column %d size: %d\n", i, track_size);
            current_x += track_size;
            if (i < grid_layout->computed_column_count - 1) {
                current_x += (int)grid_layout->column_gap;
                log_debug(" Added column gap: %.1f, new current_x: %d\n", grid_layout->column_gap, current_x);
            }
        }
    }

    // Position each grid item
    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* item = grid_layout->grid_items[i];
        log_debug(" Item pointer %d: %p, item->gi=%p\n", i, (void*)item, item ? (void*)item->gi : nullptr);
        if (!item->gi) continue;  // Skip items without grid item properties

        // Get grid area bounds (convert from 1-indexed to 0-indexed)
        int row_start = item->gi->computed_grid_row_start - 1;
        int row_end = item->gi->computed_grid_row_end - 1;
        int col_start = item->gi->computed_grid_column_start - 1;
        int col_end = item->gi->computed_grid_column_end - 1;

        // Clamp to valid ranges
        row_start = fmax(0, fmin(row_start, grid_layout->computed_row_count - 1));
        row_end = fmax(row_start + 1, fmin(row_end, grid_layout->computed_row_count));
        col_start = fmax(0, fmin(col_start, grid_layout->computed_column_count - 1));
        col_end = fmax(col_start + 1, fmin(col_end, grid_layout->computed_column_count));

        // Calculate item position and size
        int item_x = column_positions[col_start];
        int item_y = row_positions[row_start];
        int track_width = column_positions[col_end] - column_positions[col_start];
        int track_height = row_positions[row_end] - row_positions[row_start];

        // Subtract gaps from size (gaps are between tracks, not part of item area)
        int col_gaps = col_end - col_start - 1;
        int row_gaps = row_end - row_start - 1;
        track_width -= col_gaps * grid_layout->column_gap;
        track_height -= row_gaps * grid_layout->row_gap;

        // Store track area dimensions for alignment phase
        if (item->gi) {
            item->gi->track_area_width = track_width;
            item->gi->track_area_height = track_height;
        }

        // Determine item dimensions - use CSS-specified size if available,
        // otherwise default to track size (will be adjusted during alignment)
        int item_width = track_width;
        int item_height = track_height;

        // Check if item has explicit CSS width
        if (item->blk && item->blk->given_width > 0) {
            item_width = (int)item->blk->given_width;
        }

        // Check if item has explicit CSS height
        if (item->blk && item->blk->given_height > 0) {
            item_height = (int)item->blk->given_height;
        }

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
        float new_x = container->x + container_offset_x + item_x;
        float new_y = container->y + container_offset_y + item_y;
        log_debug(" Assigning item %d: x=%.0f (%.0f+%d+%d), y=%.0f, width=%d, height=%d\n",
               i, new_x, container->x, container_offset_x, item_x, new_y, item_width, item_height);
        log_debug(" Before assignment - item->x=%.0f, item->y=%.0f, item=%p\n", item->x, item->y, (void*)item);
        item->x = new_x;
        item->y = new_y;
        item->width = (float)item_width;
        item->height = (float)item_height;
        log_debug(" After assignment item %d: x=%.0f, y=%.0f, width=%.0f, height=%.0f at item=%p\n",
               i, item->x, item->y, item->width, item->height, (void*)item);

        log_debug(" Grid item %d positioning:\n", i);
        printf("  Grid area: row %d-%d, col %d-%d\n", row_start, row_end, col_start, col_end);
        printf("  Track positions: x=%d, y=%d\n", item_x, item_y);
        printf("  Track sizes: width=%d, height=%d\n", item_width, item_height);
        printf("  Container: pos=(%d,%d), offset=(%d,%d)\n",
               container->x, container->y, container_offset_x, container_offset_y);
        printf("  Final position: (%d,%d), size: %dx%d\n",
               item->x, item->y, item->width, item->height);

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
    if (!item || !grid_layout || !item->gi) return;

    // Use stored track area dimensions from positioning phase
    int available_width = item->gi->track_area_width;
    int available_height = item->gi->track_area_height;

    // Apply justify-self (horizontal alignment)
    int justify = (item->gi->justify_self != CSS_VALUE_AUTO) ?
                  item->gi->justify_self : grid_layout->justify_items;

    switch (justify) {
        case CSS_VALUE_START:
            // Already positioned at start, use item's intrinsic width
            break;

        case CSS_VALUE_END:
            item->x += (available_width - item->width);
            break;

        case CSS_VALUE_CENTER:
            item->x += (available_width - item->width) / 2;
            break;

        case CSS_VALUE_STRETCH:
            // Stretch to fill track area (unless item has explicit width)
            if (!(item->blk && item->blk->given_width > 0)) {
                item->width = available_width;
            }
            break;

        default:
            // Default to stretch for grid items (unless item has explicit width)
            if (!(item->blk && item->blk->given_width > 0)) {
                item->width = available_width;
            }
            break;
    }

    // Apply align-self (vertical alignment)
    int align = (item->gi->align_self_grid != CSS_VALUE_AUTO) ?
                item->gi->align_self_grid : grid_layout->align_items;

    switch (align) {
        case CSS_VALUE_START:
            // Already positioned at start, use item's intrinsic height
            break;

        case CSS_VALUE_END:
            item->y += (available_height - item->height);
            break;

        case CSS_VALUE_CENTER:
            item->y += (available_height - item->height) / 2;
            break;

        case CSS_VALUE_STRETCH:
            // Stretch to fill track area (unless item has explicit height)
            if (!(item->blk && item->blk->given_height > 0)) {
                item->height = available_height;
            }
            break;

        default:
            // Default to stretch for grid items (unless item has explicit height)
            if (!(item->blk && item->blk->given_height > 0)) {
                item->height = available_height;
            }
            break;
    }

    log_debug("Aligned grid item: justify=%d, align=%d, final_pos=(%d,%d), final_size=%dx%d\n",
              justify, align, item->x, item->y, item->width, item->height);
}
