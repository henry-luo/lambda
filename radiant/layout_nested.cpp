#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_nested.hpp"

#include "../lib/log.h"

// Forward declarations for functions defined later in this file
void apply_flex_item_constraints(LayoutContext* lycon, ViewBlock* flex_item, ViewBlock* flex_parent);
void batch_nested_layout_operations(LayoutContext* lycon, ViewBlock* container);

// Forward declarations for functions from other files
void layout_block_in_flex_item(LayoutContext* lycon, ViewBlock* block, ViewBlock* flex_item);
void layout_flex_item_content_for_sizing(LayoutContext* lycon, ViewBlock* flex_item);
void layout_flex_item_final_content(LayoutContext* lycon, ViewBlock* flex_item);

// Handle nested layout contexts
void layout_nested_context(LayoutContext* lycon, ViewBlock* container) {
    if (!container) return;
    
    log_debug("Layout nested context for container %p\n", container);
    
    // Determine container and content types
    DisplayValue container_display = container->display;
    
    // Set up appropriate layout context
    switch (container_display.inner) {
        case LXB_CSS_VALUE_FLEX: {
            // Container is flex - use the new flex layout system
            layout_flex_container_new(lycon, container);
            break;
        }
        case LXB_CSS_VALUE_FLOW:
        default: {
            // Check if parent is flex
            ViewBlock* parent = (ViewBlock*)container->parent;
            if (parent && parent->embed && parent->embed->flex_container && 
                parent->display.inner == LXB_CSS_VALUE_FLEX) {
                layout_block_in_flex_item(lycon, container, parent);
            } else {
                // Standard block layout - use existing layout functions
                layout_block(lycon, container->node, container_display);
            }
            break;
        }
    }
}

// Layout flex container that may contain nested layouts
void layout_flex_container_with_nested_content(LayoutContext* lycon, ViewBlock* flex_container) {
    if (!flex_container) return;
    
    log_debug("Layout flex container with nested content\n");
    
    // First pass: Layout flex items to determine their content sizes
    View* child = flex_container->child;
    while (child) {
        if (child->type == RDT_VIEW_BLOCK || child->type == RDT_VIEW_INLINE_BLOCK) {
            ViewBlock* flex_item = (ViewBlock*)child;
            
            // Layout the flex item's content to determine intrinsic sizes
            layout_flex_item_content_for_sizing(lycon, flex_item);
        }
        child = child->next;
    }
    
    // Second pass: Run flex algorithm with calculated intrinsic sizes
    layout_flex_container_new(lycon, flex_container);
    
    // Third pass: Final layout of flex item contents with determined sizes
    child = flex_container->child;
    while (child) {
        if (child->type == RDT_VIEW_BLOCK || child->type == RDT_VIEW_INLINE_BLOCK) {
            ViewBlock* flex_item = (ViewBlock*)child;
            layout_flex_item_final_content(lycon, flex_item);
        }
        child = child->next;
    }
    
    log_debug("Flex container with nested content layout complete\n");
}

// Layout block in flex context
void layout_block_in_flex_context(LayoutContext* lycon, ViewBlock* block, ViewBlock* flex_parent) {
    if (!block || !flex_parent) return;
    
    log_debug("Layout block in flex context\n");
    
    // Save current context
    Blockbox pa_block = lycon->block;
    Linebox pa_line = lycon->line;
    
    // Set up flex item context
    lycon->block.width = block->width;
    lycon->block.height = block->height;
    lycon->block.advance_y = 0;
    lycon->block.max_width = 0;
    lycon->line.left = 0;
    lycon->line.right = block->width;
    
    // Layout block content normally
    layout_block(lycon, block->node, block->display);
    
    // Apply flex item constraints with enhanced logic
    apply_flex_item_constraints(lycon, block, flex_parent);
    
    // Also apply general constraints as backup
    apply_constraints(block, flex_parent->width, flex_parent->height);
    
    // Restore context
    lycon->block = pa_block;
    lycon->line = pa_line;
}

// Apply flex item constraints
void apply_flex_item_constraints(LayoutContext* lycon, ViewBlock* flex_item, ViewBlock* flex_parent) {
    if (!flex_item || !flex_parent) return;
    
    log_debug("Apply flex item constraints\n");
    
    // Apply constraints from new flex implementation
    apply_constraints(flex_item, flex_parent->width, flex_parent->height);
    
    // Handle aspect ratio if specified
    if (flex_item->aspect_ratio > 0) {
        if (flex_item->width > 0 && flex_item->height <= 0) {
            flex_item->height = (int)(flex_item->width / flex_item->aspect_ratio);
        } else if (flex_item->height > 0 && flex_item->width <= 0) {
            flex_item->width = (int)(flex_item->height * flex_item->aspect_ratio);
        }
    }
    
    // Ensure minimum sizes
    if (flex_item->min_width > 0) {
        flex_item->width = max(flex_item->width, flex_item->min_width);
    }
    if (flex_item->min_height > 0) {
        flex_item->height = max(flex_item->height, flex_item->min_height);
    }
    
    // Ensure maximum sizes
    if (flex_item->max_width > 0) {
        flex_item->width = min(flex_item->width, flex_item->max_width);
    }
    if (flex_item->max_height > 0) {
        flex_item->height = min(flex_item->height, flex_item->max_height);
    }
}

// Handle flex-in-flex scenarios
void layout_nested_flex_containers(LayoutContext* lycon, ViewBlock* outer_flex, ViewBlock* inner_flex) {
    if (!outer_flex || !inner_flex) return;
    
    log_debug("Layout nested flex containers\n");
    
    // First, layout the inner flex container as a flex item of the outer container
    layout_block_in_flex_context(lycon, inner_flex, outer_flex);
    
    // Then, layout the inner flex container's own flex items
    if (inner_flex->embed && inner_flex->embed->flex_container) {
        layout_flex_container_with_nested_content(lycon, inner_flex);
    }
}

// Handle complex nested scenarios (flex + grid, flex + table, etc.)
void layout_complex_nested_scenario(LayoutContext* lycon, ViewBlock* container, ViewBlock* nested_container) {
    if (!container || !nested_container) return;
    
    log_debug("Layout complex nested scenario\n");
    
    DisplayValue container_display = container->display;
    DisplayValue nested_display = nested_container->display;
    
    // Handle different combinations
    if (container_display.inner == LXB_CSS_VALUE_FLEX) {
        if (nested_display.inner == LXB_CSS_VALUE_FLEX) {
            // Flex-in-flex
            layout_nested_flex_containers(lycon, container, nested_container);
        } else if (nested_display.inner == LXB_CSS_VALUE_FLOW) {
            // Block-in-flex
            layout_block_in_flex_context(lycon, nested_container, container);
        } else {
            // Other layouts in flex (grid, table, etc.)
            // For now, treat as block
            layout_block_in_flex_context(lycon, nested_container, container);
        }
    } else {
        // Non-flex container with nested content
        // Use standard layout
        layout_nested_context(lycon, nested_container);
    }
}

// Calculate containing block for nested elements
// Calculate containing block for nested elements
void calculate_containing_block(ViewBlock* element, ViewBlock* parent, ContainingBlock* cb) {
    if (!element || !parent || !cb) return;
    
    // Initialize containing block
    cb->width = parent->width;
    cb->height = parent->height;
    cb->x = parent->x;
    cb->y = parent->y;
    
    // Adjust for flex containers
    if (parent->embed && parent->embed->flex_container) {
        // In flex containers, containing block is the flex container's content area
        if (parent->bound) {
            cb->width -= (parent->bound->padding.left + parent->bound->padding.right);
            cb->height -= (parent->bound->padding.top + parent->bound->padding.bottom);
            cb->x += parent->bound->padding.left;
            cb->y += parent->bound->padding.top;
            
            if (parent->bound->border) {
                cb->width -= (parent->bound->border->width.left + parent->bound->border->width.right);
                cb->height -= (parent->bound->border->width.top + parent->bound->border->width.bottom);
                cb->x += parent->bound->border->width.left;
                cb->y += parent->bound->border->width.top;
            }
        }
    }
    
    log_debug("Containing block calculated: %dx%d at (%d,%d)\n", 
              cb->width, cb->height, cb->x, cb->y);
}

// Handle percentage resolution in nested contexts
int resolve_percentage_in_nested_context(int percentage_value, bool is_width, ViewBlock* element, ViewBlock* containing_block) {
    if (!element || !containing_block) return 0;
    
    // Enhanced percentage resolution with containing block calculation
    ContainingBlock cb;
    calculate_containing_block(element, containing_block, &cb);
    
    int container_size = is_width ? cb.width : cb.height;
    return resolve_percentage(percentage_value, true, container_size);
}

// Validate nested layout structure
bool validate_nested_layout_structure(ViewBlock* container) {
    if (!container) return false;
    
    // Check for circular dependencies
    ViewBlock* current = container;
    ViewBlock* parent = (ViewBlock*)container->parent;
    int depth = 0;
    const int MAX_NESTING_DEPTH = 100; // Prevent infinite loops
    
    while (parent && depth < MAX_NESTING_DEPTH) {
        if (parent == container) {
            log_warn("Circular dependency detected in nested layout\n");
            return false;
        }
        parent = (ViewBlock*)parent->parent;
        depth++;
    }
    
    if (depth >= MAX_NESTING_DEPTH) {
        log_warn("Maximum nesting depth exceeded\n");
        return false;
    }
    
    return true;
}

// Optimize nested layout performance
void optimize_nested_layout(LayoutContext* lycon, ViewBlock* container) {
    if (!container) return;
    
    // Skip layout if container hasn't changed
    // Note: layout_cache_valid property not available, skip caching for now
    log_debug("Optimizing nested layout for container %p\n", container);
    
    // Batch similar operations for performance optimization
    batch_nested_layout_operations(lycon, container);
    
    log_debug("Nested layout optimization applied\n");
}

// Batch nested layout operations for performance
void batch_nested_layout_operations(LayoutContext* lycon, ViewBlock* container) {
    if (!container) return;
    
    // Collect all flex containers that need layout
    ViewBlock* flex_containers[50]; // Use literal instead of constant
    int flex_count = 0;
    
    // Collect all block containers that need layout
    ViewBlock* block_containers[50]; // Use literal instead of constant
    int block_count = 0;
    
    // Traverse children and categorize
    View* child = container->child;
    while (child && flex_count < 50 && block_count < 50) {
        if (child->type == RDT_VIEW_BLOCK || child->type == RDT_VIEW_INLINE_BLOCK) {
            ViewBlock* block = (ViewBlock*)child;
            
            if (block->display.inner == LXB_CSS_VALUE_FLEX) {
                flex_containers[flex_count++] = block;
            } else {
                block_containers[block_count++] = block;
            }
        }
        child = child->next;
    }
    
    // Process flex containers first (they may affect block layout)
    for (int i = 0; i < flex_count; i++) {
        layout_flex_container_with_nested_content(lycon, flex_containers[i]);
    }
    
    // Process block containers
    for (int i = 0; i < block_count; i++) {
        layout_nested_context(lycon, block_containers[i]);
    }
    
    log_debug("Batched layout: %d flex containers, %d block containers\n", 
              flex_count, block_count);
}
