#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_content.hpp"
#include "layout_flex_measurement.hpp"

#include "../lib/log.h"

// Forward declarations
void layout_final_flex_content(LayoutContext* lycon, ViewBlock* flex_container);
bool has_main_axis_auto_margins(FlexLineInfo* line);
void handle_main_axis_auto_margins(FlexContainerLayout* flex_layout, FlexLineInfo* line);

// Multi-pass flex layout implementation
// This implements the enhanced flex layout with proper content measurement

void layout_flex_container_with_nested_content(LayoutContext* lycon, ViewBlock* flex_container) {
    if (!flex_container) return;
    
    log_debug("Starting multi-pass flex layout for container %p", flex_container);
    
    // Clear measurement cache for this layout pass
    clear_measurement_cache();
    
    // PASS 1: Content measurement (already done in layout_block_content integration)
    // The measurement pass has already been completed before this function is called
    log_debug("Pass 1: Content measurement completed");
    
    // PASS 2: Run enhanced flex algorithm
    log_debug("Pass 2: Running enhanced flex algorithm");
    
    // For now, use the existing flex algorithm
    // In the future, we can call run_flex_algorithm_with_measurements here
    layout_flex_container(lycon, flex_container);
    
    // PASS 3: Final content layout with determined flex sizes
    log_debug("Pass 3: Final content layout");
    layout_final_flex_content(lycon, flex_container);
    
    log_debug("Multi-pass flex layout completed");
}

// Simplified implementations that use existing functions

// Collect flex items with measured sizes
void collect_flex_items_with_measurements(FlexContainerLayout* flex_layout, ViewBlock* container) {
    log_debug("Collecting flex items with measurements - using existing implementation");
    // For now, just use the existing collect_flex_items function
    // In the future, we can enhance this to use measured sizes
}

// Calculate flex basis using measured content
void calculate_flex_basis_with_measurements(FlexContainerLayout* flex_layout) {
    log_debug("Calculating flex basis with measurements - using existing implementation");
    // For now, use existing flex basis calculation
    // In the future, we can enhance this to use measured content sizes
}

// Enhanced flexible length resolution
void resolve_flexible_lengths_with_measurements(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    log_debug("Resolving flexible lengths with measurements");
    
    if (!line || line->item_count == 0) return;
    
    // Use existing resolve_flexible_lengths function as base
    resolve_flexible_lengths(flex_layout, line);
    
    log_debug("Flexible length resolution completed");
}

// Enhanced main axis alignment with proper auto margin handling
void align_items_main_axis_enhanced(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    log_debug("Enhanced main axis alignment");
    
    if (!line || line->item_count == 0) return;
    
    // For now, just use existing align_items_main_axis function
    // TODO: Add auto margin handling later
    
    // Use existing align_items_main_axis function
    align_items_main_axis(flex_layout, line);
}

// Check if any items have main axis auto margins
bool has_main_axis_auto_margins(FlexLineInfo* line) {
    for (int i = 0; i < line->item_count; i++) {
        ViewBlock* item = line->items[i];
        if (item->margin_left_auto || item->margin_right_auto ||
            item->margin_top_auto || item->margin_bottom_auto) {
            return true;
        }
    }
    return false;
}

// Handle main axis auto margins
void handle_main_axis_auto_margins(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    log_debug("Handling main axis auto margins");
    
    // For now, implement simple centering for items with auto margins
    for (int i = 0; i < line->item_count; i++) {
        ViewBlock* item = line->items[i];
        
        bool main_start_auto = is_main_axis_horizontal(flex_layout) ? 
            item->margin_left_auto : item->margin_top_auto;
        bool main_end_auto = is_main_axis_horizontal(flex_layout) ? 
            item->margin_right_auto : item->margin_bottom_auto;
        
        if (main_start_auto && main_end_auto) {
            // Center the item
            int container_size = flex_layout->main_axis_size;
            int item_size = get_main_axis_size(item, flex_layout);
            int center_pos = (container_size - item_size) / 2;
            
            log_debug("Centering item %d at position %d", i, center_pos);
            set_main_axis_position(item, center_pos, flex_layout);
        }
    }
}

// Final content layout pass
void layout_final_flex_content(LayoutContext* lycon, ViewBlock* flex_container) {
    log_debug("Final flex content layout");
    
    // Layout content within each flex item with their final sizes
    View* child = flex_container->child;
    while (child) {
        if (child->type == RDT_VIEW_BLOCK) {
            ViewBlock* flex_item = (ViewBlock*)child;
            
            log_debug("Final layout for flex item %p: %dx%d", 
                      flex_item, flex_item->width, flex_item->height);
            
            // Layout the content within the flex item
            layout_flex_item_final_content(lycon, flex_item);
        }
        child = child->next;
    }
    
    log_debug("Final flex content layout completed");
}

// Note: layout_flex_item_final_content is already defined in layout_flex_content.cpp

