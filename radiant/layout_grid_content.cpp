#include "grid.hpp"
#include "layout.hpp"
#include "view.hpp"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../lib/log.h"
}

// Enhanced grid item content layout with full HTML nested content support
// Based on successful Phase 3.1 flex layout enhancements
void layout_grid_item_content(LayoutContext* lycon, ViewBlock* grid_item) {
    if (!grid_item) return;

    log_debug("Enhanced grid item content layout for %p\n", grid_item);

    // Save parent context
    LayoutContext saved_context = *lycon;

    // Set up grid item as a proper containing block
    lycon->parent = (ViewGroup*)grid_item;
    lycon->prev_view = NULL;

    // Calculate content area dimensions accounting for box model
    int content_width = grid_item->width;
    int content_height = grid_item->height;
    int content_x_offset = 0;
    int content_y_offset = 0;

    if (grid_item->bound) {
        // Account for padding and border in content area
        content_width -= (grid_item->bound->padding.left + grid_item->bound->padding.right);
        content_height -= (grid_item->bound->padding.top + grid_item->bound->padding.bottom);
        content_x_offset = grid_item->bound->padding.left;
        content_y_offset = grid_item->bound->padding.top;

        if (grid_item->bound->border) {
            content_width -= (grid_item->bound->border->width.left + grid_item->bound->border->width.right);
            content_height -= (grid_item->bound->border->width.top + grid_item->bound->border->width.bottom);
            content_x_offset += grid_item->bound->border->width.left;
            content_y_offset += grid_item->bound->border->width.top;
        }
    }

    // Set up block formatting context for nested content
    lycon->block.width = content_width;
    lycon->block.height = content_height;
    lycon->block.advance_y = content_y_offset;
    lycon->block.max_width = 0;

    // Inherit text alignment and other block properties from grid item
    if (grid_item->blk) {
        lycon->block.text_align = grid_item->blk->text_align;
        // lycon->block.line_height = grid_item->blk->line_height;
        // log_debug("GRID - Inherited text_align=%d, line_height=%d\n",
        //        grid_item->blk->text_align, grid_item->blk->line_height);
    }

    // Set up line formatting context for inline content
    log_debug("GRID - Content area: %dx%d, offset (%d,%d), line (%d to %d)\n",
           content_width, content_height, content_x_offset, content_y_offset,
           lycon->line.left, lycon->line.right);

    line_init(lycon, content_x_offset, content_x_offset + content_width);

    // Layout all nested content using standard flow algorithm
    // This handles: text nodes, nested blocks, inline elements, images, etc.
    if (grid_item->node && grid_item->node->first_child()) {
        DomNode* child = grid_item->node->first_child();
        int child_count = 0;
        do {
            child_count++;
            log_debug("GRID - Processing child %d: tag=%lu\n", child_count, child->tag());

            // Use standard layout flow - this handles all HTML content types
            layout_flow_node(lycon, child);
            child = child->next_sibling();
        } while (child);

        // Finalize any pending line content
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        }

        log_debug("GRID - Processed %d children\n", child_count);
    }

    // Update grid item content dimensions for intrinsic sizing
    grid_item->content_width = lycon->block.max_width;
    grid_item->content_height = lycon->block.advance_y - content_y_offset;

    log_debug("GRID - Final content dimensions: %dx%d\n",
           grid_item->content_width, grid_item->content_height);

    // Restore parent context
    *lycon = saved_context;

    log_debug("Enhanced grid item content layout complete: %dx%d\n",
              grid_item->content_width, grid_item->content_height);
}

// Layout content within a grid item for sizing (first pass)
void layout_grid_item_content_for_sizing(LayoutContext* lycon, ViewBlock* grid_item) {
    if (!grid_item) return;

    log_debug("Layout grid item content for sizing\n");

    // Calculate intrinsic sizes for measurement phase
    // This is consistent with the flex layout approach
    calculate_grid_item_intrinsic_sizes(grid_item, true);  // Row axis
    calculate_grid_item_intrinsic_sizes(grid_item, false); // Column axis

    // Set preliminary dimensions based on intrinsic sizes
    if (grid_item->width <= 0) {
        grid_item->width = 200; // Default width for sizing
    }
    if (grid_item->height <= 0) {
        grid_item->height = 100; // Default height for sizing
    }
}

// Final layout of grid item contents with determined sizes
void layout_grid_item_final_content(LayoutContext* lycon, ViewBlock* grid_item) {
    if (!grid_item) return;

    log_debug("Final layout of grid item content\n");

    // Use the main enhanced content layout function
    layout_grid_item_content(lycon, grid_item);
}

// Layout content for all grid items (Phase 8 - Enhanced grid content layout)
// Based on successful flex layout multi-pass architecture
void layout_grid_items_content(LayoutContext* lycon, GridContainerLayout* grid_layout) {
    if (!lycon || !grid_layout) return;

    log_debug("Enhanced grid items content layout starting\n");

    // Layout content for each grid item with their final determined sizes
    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* grid_item = grid_layout->grid_items[i];

        log_debug("GRID CONTENT - Layout item %d: pos=(%d,%d), size=%dx%d\n",
                  i, grid_item->x, grid_item->y, grid_item->width, grid_item->height);

        // Layout the content within the grid item using enhanced content layout
        layout_grid_item_content(lycon, grid_item);
    }

    log_debug("Enhanced grid items content layout completed\n");
}
