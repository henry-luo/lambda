#include "layout.hpp"
#include "../lib/log.h"

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
    return block->position && 
           (block->position->float_prop == LXB_CSS_VALUE_LEFT ||
            block->position->float_prop == LXB_CSS_VALUE_RIGHT);
}
