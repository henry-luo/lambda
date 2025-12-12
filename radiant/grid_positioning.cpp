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

    // First, calculate the total grid content size (all tracks + gaps)
    int total_row_size = 0;
    for (int i = 0; i < grid_layout->computed_row_count; i++) {
        total_row_size += grid_layout->computed_rows[i].computed_size;
        if (i < grid_layout->computed_row_count - 1) {
            total_row_size += (int)grid_layout->row_gap;
        }
    }

    int total_column_size = 0;
    for (int i = 0; i < grid_layout->computed_column_count; i++) {
        total_column_size += grid_layout->computed_columns[i].computed_size;
        if (i < grid_layout->computed_column_count - 1) {
            total_column_size += (int)grid_layout->column_gap;
        }
    }

    log_debug(" Total grid content: %dx%d, container content: %dx%d\n",
              total_column_size, total_row_size,
              grid_layout->content_width, grid_layout->content_height);

    // Calculate justify-content offset and spacing (horizontal)
    int justify_offset = 0;
    float justify_spacing = 0;  // Additional spacing between tracks
    int extra_column_space = grid_layout->content_width - total_column_size;
    int col_count = grid_layout->computed_column_count;
    if (extra_column_space > 0 && col_count > 0) {
        switch (grid_layout->justify_content) {
            case CSS_VALUE_CENTER:
                justify_offset = extra_column_space / 2;
                break;
            case CSS_VALUE_END:
            case CSS_VALUE_FLEX_END:
                justify_offset = extra_column_space;
                break;
            case CSS_VALUE_SPACE_BETWEEN:
                // Space distributed between tracks (first and last track at edges)
                if (col_count > 1) {
                    justify_spacing = (float)extra_column_space / (col_count - 1);
                }
                break;
            case CSS_VALUE_SPACE_AROUND:
                // Equal space around each track (half space at edges)
                justify_spacing = (float)extra_column_space / col_count;
                justify_offset = (int)(justify_spacing / 2);
                break;
            case CSS_VALUE_SPACE_EVENLY:
                // Equal space between all tracks including edges
                justify_spacing = (float)extra_column_space / (col_count + 1);
                justify_offset = (int)justify_spacing;
                break;
            case CSS_VALUE_START:
            case CSS_VALUE_FLEX_START:
            default:
                justify_offset = 0;
                break;
        }
    }
    log_debug(" justify-content=%d, extra_space=%d, offset=%d, spacing=%.1f\n",
              grid_layout->justify_content, extra_column_space, justify_offset, justify_spacing);

    // Calculate align-content offset and spacing (vertical)
    int align_offset = 0;
    float align_spacing = 0;  // Additional spacing between tracks
    int extra_row_space = grid_layout->content_height - total_row_size;
    int row_count = grid_layout->computed_row_count;
    if (extra_row_space > 0 && row_count > 0) {
        switch (grid_layout->align_content) {
            case CSS_VALUE_CENTER:
                align_offset = extra_row_space / 2;
                break;
            case CSS_VALUE_END:
            case CSS_VALUE_FLEX_END:
                align_offset = extra_row_space;
                break;
            case CSS_VALUE_SPACE_BETWEEN:
                // Space distributed between tracks (first and last track at edges)
                if (row_count > 1) {
                    align_spacing = (float)extra_row_space / (row_count - 1);
                }
                break;
            case CSS_VALUE_SPACE_AROUND:
                // Equal space around each track (half space at edges)
                align_spacing = (float)extra_row_space / row_count;
                align_offset = (int)(align_spacing / 2);
                break;
            case CSS_VALUE_SPACE_EVENLY:
                // Equal space between all tracks including edges
                align_spacing = (float)extra_row_space / (row_count + 1);
                align_offset = (int)align_spacing;
                break;
            case CSS_VALUE_START:
            case CSS_VALUE_FLEX_START:
            default:
                align_offset = 0;
                break;
        }
    }
    log_debug(" align-content=%d, extra_space=%d, offset=%d, spacing=%.1f\n",
              grid_layout->align_content, extra_row_space, align_offset, align_spacing);

    // Calculate row positions with align-content offset and spacing
    float current_y_f = align_offset;
    log_debug(" Calculating row positions for %d rows:\n", grid_layout->computed_row_count);
    for (int i = 0; i <= grid_layout->computed_row_count; i++) {
        row_positions[i] = (int)current_y_f;
        log_debug(" Row %d position: %d\n", i, row_positions[i]);
        if (i < grid_layout->computed_row_count) {
            int track_size = grid_layout->computed_rows[i].computed_size;
            log_debug(" Row %d size: %d\n", i, track_size);
            current_y_f += track_size;
            if (i < grid_layout->computed_row_count - 1) {
                current_y_f += grid_layout->row_gap;
                // Add space-* distribution spacing
                if (grid_layout->align_content == CSS_VALUE_SPACE_BETWEEN ||
                    grid_layout->align_content == CSS_VALUE_SPACE_AROUND ||
                    grid_layout->align_content == CSS_VALUE_SPACE_EVENLY) {
                    current_y_f += align_spacing;
                }
                log_debug(" Added row gap: %.1f + spacing %.1f, new current_y: %.1f\n",
                          grid_layout->row_gap, align_spacing, current_y_f);
            }
        }
    }

    // Calculate column positions with justify-content offset and spacing
    float current_x_f = justify_offset;
    log_debug(" Calculating column positions for %d columns:\n", grid_layout->computed_column_count);
    for (int i = 0; i <= grid_layout->computed_column_count; i++) {
        column_positions[i] = (int)current_x_f;
        log_debug(" Column %d position: %d\n", i, column_positions[i]);
        if (i < grid_layout->computed_column_count) {
            int track_size = grid_layout->computed_columns[i].computed_size;
            log_debug(" Column %d size: %d\n", i, track_size);
            current_x_f += track_size;
            if (i < grid_layout->computed_column_count - 1) {
                current_x_f += grid_layout->column_gap;
                // Add space-* distribution spacing
                if (grid_layout->justify_content == CSS_VALUE_SPACE_BETWEEN ||
                    grid_layout->justify_content == CSS_VALUE_SPACE_AROUND ||
                    grid_layout->justify_content == CSS_VALUE_SPACE_EVENLY) {
                    current_x_f += justify_spacing;
                }
                log_debug(" Added column gap: %.1f + spacing %.1f, new current_x: %.1f\n",
                          grid_layout->column_gap, justify_spacing, current_x_f);
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

        // Calculate track width by summing individual track sizes (not from positions, which include gaps)
        int track_width = 0;
        for (int c = col_start; c < col_end; c++) {
            track_width += grid_layout->computed_columns[c].computed_size;
            // Add gap for interior tracks (not the last one in the span)
            if (c < col_end - 1) {
                track_width += (int)grid_layout->column_gap;
            }
        }

        int track_height = 0;
        for (int r = row_start; r < row_end; r++) {
            track_height += grid_layout->computed_rows[r].computed_size;
            // Add gap for interior tracks (not the last one in the span)
            if (r < row_end - 1) {
                track_height += (int)grid_layout->row_gap;
            }
        }

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

        // Set item position and size (relative to parent's border box, per Radiant coordinate system)
        float new_x = container_offset_x + item_x;
        float new_y = container_offset_y + item_y;
        log_debug(" Assigning item %d: x=%.0f (%d+%d), y=%.0f, width=%d, height=%d\n",
               i, new_x, container_offset_x, item_x, new_y, item_width, item_height);
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
        printf("  Container: offset=(%d,%d)\n", container_offset_x, container_offset_y);
        printf("  Final position: (%.0f,%.0f), size: %.0fx%.0f\n",
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
