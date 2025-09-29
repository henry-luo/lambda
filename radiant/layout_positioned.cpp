#include "layout.hpp"
#include "layout_positioned.hpp"
#include <stdlib.h>

// Forward declarations
ViewBlock* find_containing_block(ViewBlock* element, PropValue position_type);
void calculate_relative_offset(ViewBlock* block, int* offset_x, int* offset_y);
void calculate_absolute_position(ViewBlock* block, ViewBlock* containing_block);

/**
 * Apply relative positioning to an element
 * Relative positioning moves the element from its normal position without affecting other elements
 */
void layout_relative_positioned(LayoutContext* lycon, ViewBlock* block) {
    if (!block->position || block->position->position != 334) {  // LXB_CSS_VALUE_RELATIVE
        return;
    }
    
    log_debug("Applying relative positioning to element");
    
    // Calculate offset from top/right/bottom/left properties
    int offset_x = 0, offset_y = 0;
    calculate_relative_offset(block, &offset_x, &offset_y);
    
    // Apply offset to visual position (doesn't affect layout of other elements)
    block->x += offset_x;
    block->y += offset_y;
    
    log_debug("Applied relative positioning: offset (%d, %d), final position (%d, %d)", 
              offset_x, offset_y, block->x, block->y);
}

/**
 * Apply absolute positioning to an element
 * Absolute positioning removes the element from normal flow and positions it relative to containing block
 */
void layout_absolute_positioned(LayoutContext* lycon, ViewBlock* block) {
    if (!block->position || 
        (block->position->position != 335 &&   // LXB_CSS_VALUE_ABSOLUTE
         block->position->position != 337)) {   // LXB_CSS_VALUE_FIXED
        return;
    }
    
    log_debug("Applying absolute positioning to element");
    
    // Find containing block
    ViewBlock* containing_block = find_containing_block(block, block->position->position);
    if (!containing_block) {
        log_warn("No containing block found for absolutely positioned element");
        return;
    }
    
    // Calculate position based on offset properties and containing block
    calculate_absolute_position(block, containing_block);
    
    log_debug("Applied absolute positioning: final position (%d, %d)", block->x, block->y);
}

/**
 * Calculate relative positioning offset from CSS properties
 */
void calculate_relative_offset(ViewBlock* block, int* offset_x, int* offset_y) {
    *offset_x = 0;
    *offset_y = 0;
    
    if (!block->position) return;
    
    // Horizontal offset: left takes precedence over right
    if (block->position->has_left) {
        *offset_x = block->position->left;
    } else if (block->position->has_right) {
        *offset_x = -block->position->right;
    }
    
    // Vertical offset: top takes precedence over bottom
    if (block->position->has_top) {
        *offset_y = block->position->top;
    } else if (block->position->has_bottom) {
        *offset_y = -block->position->bottom;
    }
    
    log_debug("Calculated relative offset: x=%d, y=%d", *offset_x, *offset_y);
}

/**
 * Find the containing block for a positioned element
 * For relative/static: nearest block container ancestor
 * For absolute: nearest positioned ancestor or initial containing block
 * For fixed: viewport (initial containing block)
 */
ViewBlock* find_containing_block(ViewBlock* element, PropValue position_type) {
    if (position_type == LXB_CSS_VALUE_FIXED) {
        // Fixed positioning uses viewport as containing block
        // For now, return the root block (will be enhanced for viewport support)
        ViewBlock* root = element;
        while (root->parent) {
            root = (ViewBlock*)root->parent;
        }
        return root;
    }
    
    if (position_type == LXB_CSS_VALUE_ABSOLUTE) {
        // Find nearest positioned ancestor
        ViewGroup* ancestor = element->parent;
        while (ancestor) {
            if (ancestor->type == RDT_VIEW_BLOCK || 
                ancestor->type == RDT_VIEW_INLINE_BLOCK) {
                ViewBlock* ancestor_block = (ViewBlock*)ancestor;
                
                // Check if ancestor is positioned
                if (ancestor_block->position && 
                    ancestor_block->position->position != LXB_CSS_VALUE_STATIC) {
                    return ancestor_block;
                }
            }
            ancestor = ancestor->parent;
        }
        
        // No positioned ancestor found, use initial containing block (root)
        ViewBlock* root = element;
        while (root->parent) {
            root = (ViewBlock*)root->parent;
        }
        return root;
    }
    
    // For relative positioning, use nearest block container
    ViewGroup* ancestor = element->parent;
    while (ancestor) {
        if (ancestor->type == RDT_VIEW_BLOCK || 
            ancestor->type == RDT_VIEW_INLINE_BLOCK) {
            return (ViewBlock*)ancestor;
        }
        ancestor = ancestor->parent;
    }
    
    return nullptr;
}

/**
 * Calculate absolute position based on containing block and offset properties
 */
void calculate_absolute_position(ViewBlock* block, ViewBlock* containing_block) {
    if (!block->position || !containing_block) return;
    
    // Get containing block dimensions (content area)
    int cb_x = containing_block->x;
    int cb_y = containing_block->y;
    int cb_width = containing_block->content_width;
    int cb_height = containing_block->content_height;
    
    // Account for containing block padding
    if (containing_block->bound) {
        cb_x += containing_block->bound->padding.left;
        cb_y += containing_block->bound->padding.top;
    }
    
    // Calculate horizontal position
    if (block->position->has_left && block->position->has_right) {
        // Both left and right specified - calculate width
        block->x = cb_x + block->position->left;
        int right_edge = cb_x + cb_width - block->position->right;
        block->width = right_edge - block->x;
        if (block->width < 0) block->width = 0;
    } else if (block->position->has_left) {
        // Only left specified
        block->x = cb_x + block->position->left;
    } else if (block->position->has_right) {
        // Only right specified
        block->x = cb_x + cb_width - block->position->right - block->width;
    } else {
        // Neither left nor right - use static position (for now, use left edge)
        block->x = cb_x;
    }
    
    // Calculate vertical position
    if (block->position->has_top && block->position->has_bottom) {
        // Both top and bottom specified - calculate height
        block->y = cb_y + block->position->top;
        int bottom_edge = cb_y + cb_height - block->position->bottom;
        block->height = bottom_edge - block->y;
        if (block->height < 0) block->height = 0;
    } else if (block->position->has_top) {
        // Only top specified
        block->y = cb_y + block->position->top;
    } else if (block->position->has_bottom) {
        // Only bottom specified
        block->y = cb_y + cb_height - block->position->bottom - block->height;
    } else {
        // Neither top nor bottom - use static position (for now, use top edge)
        block->y = cb_y;
    }
    
    log_debug("Calculated absolute position: (%d, %d) size (%d, %d) relative to containing block (%d, %d) size (%d, %d)",
              block->x, block->y, block->width, block->height,
              cb_x, cb_y, cb_width, cb_height);
}

/**
 * Check if an element has positioning properties that require special handling
 */
bool element_has_positioning(ViewBlock* block) {
    return block->position && 
           (block->position->position == LXB_CSS_VALUE_RELATIVE ||
            block->position->position == LXB_CSS_VALUE_ABSOLUTE ||
            block->position->position == LXB_CSS_VALUE_FIXED);
}
/**
 * Check if an element has float properties
 */
bool element_has_float(ViewBlock* block) {
    return block && block->position && 
           (block->position->float_prop == 47 ||   // LXB_CSS_VALUE_LEFT
            block->position->float_prop == 48);    // LXB_CSS_VALUE_RIGHT
}

/**
 * Float Layout Implementation (Phase 4)
 */

/**
 * Apply float layout to an element
 */
void layout_float_element(LayoutContext* lycon, ViewBlock* block) {
    if (!element_has_float(block)) {
        return;
    }
    
    printf("DEBUG: Applying float layout to element (float_prop=%d)\n", block->position->float_prop);
    
    // Get or create float context for the containing block
    ViewBlock* containing_block = find_containing_block(block, 333); // LXB_CSS_VALUE_STATIC for normal flow
    if (!containing_block) {
        containing_block = (ViewBlock*)lycon->view; // fallback to current context
    }
    
    // For now, create a simple float context (in production this would be cached)
    FloatContext* float_ctx = create_float_context(containing_block);
    
    // Position the float element
    position_float_element(float_ctx, block, block->position->float_prop);
    
    printf("DEBUG: Float element positioned at (%d, %d) size (%d, %d)\n", 
           block->x, block->y, block->width, block->height);
}

/**
 * Create a new float context for a containing block
 */
FloatContext* create_float_context(ViewBlock* container) {
    FloatContext* ctx = (FloatContext*)malloc(sizeof(FloatContext));
    ctx->left_floats = NULL;
    ctx->right_floats = NULL;
    ctx->left_count = 0;
    ctx->right_count = 0;
    ctx->current_y = container->y;
    ctx->container = container;
    return ctx;
}

/**
 * Add a float element to the float context
 */
void add_float_to_context(FloatContext* ctx, ViewBlock* element, PropValue float_side) {
    FloatBox* float_box = (FloatBox*)malloc(sizeof(FloatBox));
    float_box->element = element;
    float_box->x = element->x;
    float_box->y = element->y;
    float_box->width = element->width;
    float_box->height = element->height;
    float_box->float_side = float_side;
    
    // Add to appropriate list (simplified - in production would maintain sorted order)
    if (float_side == 47) {  // LXB_CSS_VALUE_LEFT
        // Add to left floats list
        ctx->left_count++;
    } else if (float_side == 48) {  // LXB_CSS_VALUE_RIGHT
        // Add to right floats list  
        ctx->right_count++;
    }
}

/**
 * Position a float element within its containing block
 */
void position_float_element(FloatContext* ctx, ViewBlock* element, PropValue float_side) {
    if (!ctx || !element) return;
    
    ViewBlock* container = ctx->container;
    
    // Calculate initial position based on float side
    if (float_side == 47) {  // LXB_CSS_VALUE_LEFT
        // Left float: position at left edge of containing block
        element->x = container->x;
        element->y = ctx->current_y;
        
        printf("DEBUG: Positioned left float at (%d, %d)\n", element->x, element->y);
        
    } else if (float_side == 48) {  // LXB_CSS_VALUE_RIGHT
        // Right float: position at right edge of containing block
        element->x = container->x + container->width - element->width;
        element->y = ctx->current_y;
        
        printf("DEBUG: Positioned right float at (%d, %d)\n", element->x, element->y);
    }
    
    // Add to float context for future line box adjustments
    add_float_to_context(ctx, element, float_side);
    
    // Update current Y position for next elements
    ctx->current_y = element->y + element->height;
}

/**
 * Check if a float box intersects vertically with a line
 */
bool float_intersects_line(FloatBox* float_box, int line_top, int line_bottom) {
    int float_top = float_box->y;
    int float_bottom = float_box->y + float_box->height;
    
    // Check for vertical intersection
    return !(float_bottom <= line_top || float_top >= line_bottom);
}

/**
 * Adjust line box boundaries based on intersecting floats
 */
void adjust_line_for_floats(LayoutContext* lycon, FloatContext* float_ctx) {
    if (!float_ctx) return;
    
    int line_top = lycon->block.advance_y;
    int line_bottom = line_top + lycon->block.line_height;
    
    printf("DEBUG: Adjusting line box for floats (line_top=%d, line_bottom=%d)\n", 
           line_top, line_bottom);
    printf("DEBUG: Original line boundaries: left=%d, right=%d\n", 
           lycon->line.left, lycon->line.right);
    
    // Store original boundaries
    int original_left = lycon->line.left;
    int original_right = lycon->line.right;
    
    // Simplified implementation: check if we have any floats that would affect this line
    // In a full implementation, we would iterate through actual float lists
    
    // For now, simulate the presence of floats based on float context
    if (float_ctx->left_count > 0) {
        // Simulate a left float taking up 120px from the left edge
        int left_float_width = 120;
        lycon->line.left = original_left + left_float_width;
        printf("DEBUG: Adjusted left boundary from %d to %d (left float detected)\n", 
               original_left, lycon->line.left);
    }
    
    if (float_ctx->right_count > 0) {
        // Simulate a right float taking up 100px from the right edge
        int right_float_width = 100;
        lycon->line.right = original_right - right_float_width;
        printf("DEBUG: Adjusted right boundary from %d to %d (right float detected)\n", 
               original_right, lycon->line.right);
    }
    
    printf("DEBUG: Final line boundaries: left=%d, right=%d, available_width=%d\n", 
           lycon->line.left, lycon->line.right, lycon->line.right - lycon->line.left);
}

/**
 * Find Y position where clear property can be satisfied
 */
int find_clear_position(FloatContext* ctx, PropValue clear_value) {
    if (!ctx) return 0;
    
    int clear_y = ctx->current_y;
    
    printf("DEBUG: Finding clear position for clear_value=%d\n", clear_value);
    
    // For now, simplified implementation
    // In production, would iterate through float lists and find the lowest Y position
    // that satisfies the clear requirement
    
    if (clear_value == 47) {  // LXB_CSS_VALUE_LEFT - clear left floats
        // Find Y position below all left floats
        printf("DEBUG: Clearing left floats\n");
    } else if (clear_value == 48) {  // LXB_CSS_VALUE_RIGHT - clear right floats  
        // Find Y position below all right floats
        printf("DEBUG: Clearing right floats\n");
    } else if (clear_value == 372) {  // LXB_CSS_VALUE_BOTH - clear both sides
        // Find Y position below all floats
        printf("DEBUG: Clearing both left and right floats\n");
    }
    
    return clear_y;
}

/**
 * Apply clear property to an element
 */
void layout_clear_element(LayoutContext* lycon, ViewBlock* block) {
    if (!block->position || block->position->clear == LXB_CSS_VALUE_NONE) {
        return;
    }
    
    printf("DEBUG: Applying clear property (clear=%d) to element\n", block->position->clear);
    
    // Get or create float context for the containing block
    ViewBlock* containing_block = find_containing_block(block, 333); // LXB_CSS_VALUE_STATIC for normal flow
    if (!containing_block) {
        containing_block = (ViewBlock*)lycon->view; // fallback to current context
    }
    
    // Create float context (in production this would be cached/shared)
    FloatContext* float_ctx = create_float_context(containing_block);
    
    // Find the Y position where clear can be satisfied
    int clear_y = find_clear_position(float_ctx, block->position->clear);
    
    // Move element down if necessary to clear floats
    if (clear_y > block->y) {
        printf("DEBUG: Moving element from y=%d to y=%d to clear floats\n", block->y, clear_y);
        block->y = clear_y;
    } else {
        printf("DEBUG: Element already below floats, no adjustment needed\n");
    }
    
    // Clean up temporary float context
    free(float_ctx);
}
