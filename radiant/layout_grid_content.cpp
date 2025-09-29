#include "grid.hpp"
#include "layout.hpp"
#include "view.hpp"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../lib/log.h"
}

// Layout content within a grid item
void layout_grid_item_content(LayoutContext* lycon, ViewBlock* grid_item) {
    if (!grid_item) return;
    
    log_debug("Layout grid item content for %p\n", grid_item);
    
    // Save current context
    Blockbox pa_block = lycon->block;
    Linebox pa_line = lycon->line;
    FontBox pa_font = lycon->font;
    ViewGroup* pa_parent = lycon->parent;
    
    // Set up grid item context
    lycon->parent = (ViewGroup*)grid_item;
    lycon->prev_view = NULL;
    lycon->block.width = grid_item->width;
    lycon->block.height = grid_item->height;
    lycon->block.advance_y = 0;
    lycon->block.max_width = 0;
    lycon->line.left = 0;
    lycon->line.right = grid_item->width;
    lycon->line.vertical_align = LXB_CSS_VALUE_BASELINE;
    line_init(lycon);
    
    // Layout child content
    DomNode* child = grid_item->node ? grid_item->node->first_child() : NULL;
    if (child) {
        do {
            layout_flow_node(lycon, child);
            child = child->next_sibling();
        } while (child);
        
        // Handle last line if needed
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        }
    }
    
    // Calculate final content dimensions
    grid_item->content_width = lycon->block.max_width;
    grid_item->content_height = lycon->block.advance_y;
    
    // Restore context
    lycon->block = pa_block;
    lycon->line = pa_line;
    lycon->font = pa_font;
    lycon->parent = pa_parent;
    
    log_debug("Grid item content layout complete: %dx%d\n", 
              grid_item->content_width, grid_item->content_height);
}

// Layout content within a grid item for sizing (first pass)
void layout_grid_item_content_for_sizing(LayoutContext* lycon, ViewBlock* grid_item) {
    if (!grid_item) return;
    
    log_debug("Layout grid item content for sizing\n");
    
    // This is a simplified layout pass to determine intrinsic sizes
    // We don't need to do full layout, just calculate content requirements
    
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
    
    // Now that grid algorithm has determined final sizes, do full content layout
    layout_grid_item_content(lycon, grid_item);
}
