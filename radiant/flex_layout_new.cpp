#include "flex_layout_new.hpp"
#include "layout.hpp"
#include "view.hpp"
extern "C" {
#include <stdlib.h>
#include <string.h>
}

// Initialize flex container layout state
void init_flex_container(ViewBlock* container) {
    if (!container || !container->embed) return;
    
    FlexContainerLayout* flex = (FlexContainerLayout*)calloc(1, sizeof(FlexContainerLayout));
    container->embed->flex_container = flex;
    
    // Set default values using correct Lexbor CSS values
    flex->direction = LXB_CSS_VALUE_ROW;
    flex->wrap = LXB_CSS_VALUE_NOWRAP;
    flex->justify = LXB_CSS_VALUE_FLEX_START;
    flex->align_items = LXB_CSS_VALUE_STRETCH;
    flex->align_content = LXB_CSS_VALUE_STRETCH;
    flex->row_gap = 0;
    flex->column_gap = 0;
    flex->writing_mode = WM_HORIZONTAL_TB;
    flex->text_direction = TD_LTR;
    
    // Initialize dynamic arrays
    flex->allocated_items = 8;
    flex->flex_items = (ViewBlock**)calloc(flex->allocated_items, sizeof(ViewBlock*));
    flex->allocated_lines = 4;
    flex->lines = (FlexLineInfo*)calloc(flex->allocated_lines, sizeof(FlexLineInfo));
    
    flex->needs_reflow = true;
}

// Cleanup flex container resources
void cleanup_flex_container(ViewBlock* container) {
    if (!container || !container->embed || !container->embed->flex_container) return;
    
    FlexContainerLayout* flex = container->embed->flex_container;
    
    // Free line items arrays
    for (int i = 0; i < flex->line_count; ++i) {
        free(flex->lines[i].items);
    }
    
    free(flex->flex_items);
    free(flex->lines);
    free(flex);
    container->embed->flex_container = nullptr;
}

// Main flex layout algorithm entry point
void layout_flex_container_new(LayoutContext* lycon, ViewBlock* container) {
    if (!container || !container->embed || !container->embed->flex_container) return;
    
    FlexContainerLayout* flex_layout = container->embed->flex_container;
    
    // Phase 1: Collect flex items
    ViewBlock** items;
    int item_count = collect_flex_items(container, &items);
    if (item_count == 0) return;
    
    // Phase 2: Sort items by order property
    sort_flex_items_by_order(items, item_count);
    
    // Phase 3: Create flex lines (handle wrapping)
    int line_count = create_flex_lines(flex_layout, items, item_count);
    
    // Phase 4: Resolve flexible lengths for each line
    for (int i = 0; i < line_count; i++) {
        resolve_flexible_lengths(flex_layout, &flex_layout->lines[i]);
    }
    
    // Phase 5: Calculate cross sizes for lines
    calculate_line_cross_sizes(flex_layout);
    
    // Phase 6: Align items on main axis
    for (int i = 0; i < line_count; i++) {
        align_items_main_axis(flex_layout, &flex_layout->lines[i]);
    }
    
    // Phase 7: Align items on cross axis
    for (int i = 0; i < line_count; i++) {
        align_items_cross_axis(flex_layout, &flex_layout->lines[i]);
    }
    
    // Phase 8: Align content (lines) if there are multiple lines
    if (line_count > 1) {
        align_content(flex_layout);
    }
    
    flex_layout->needs_reflow = false;
}

// Collect flex items from container children
int collect_flex_items(ViewBlock* container, ViewBlock*** items) {
    if (!container || !items) return 0;
    
    FlexContainerLayout* flex = container->embed->flex_container;
    if (!flex) return 0;
    
    int count = 0;
    
    // Count children first - use ViewGroup hierarchy
    View* child = container->child;
    while (child) {
        if (child->type == RDT_VIEW_BLOCK || child->type == RDT_VIEW_INLINE_BLOCK) {
            count++;
        }
        child = child->next;
    }
    
    if (count == 0) {
        *items = nullptr;
        return 0;
    }
    
    // Ensure we have enough space in the flex items array
    if (count > flex->allocated_items) {
        flex->allocated_items = count * 2;
        flex->flex_items = (ViewBlock**)realloc(flex->flex_items, 
                                               flex->allocated_items * sizeof(ViewBlock*));
    }
    
    // Collect items - use ViewGroup hierarchy
    count = 0;
    child = container->child;
    while (child) {
        if (child->type == RDT_VIEW_BLOCK || child->type == RDT_VIEW_INLINE_BLOCK) {
            flex->flex_items[count] = (ViewBlock*)child;
            count++;
        }
        child = child->next;
    }
    
    flex->item_count = count;
    *items = flex->flex_items;
    return count;
}

// Sort flex items by CSS order property
void sort_flex_items_by_order(ViewBlock** items, int count) {
    if (!items || count <= 1) return;
    
    // Simple insertion sort by order, maintaining document order for equal values
    for (int i = 1; i < count; ++i) {
        ViewBlock* key = items[i];
        int j = i - 1;
        
        while (j >= 0 && items[j]->order > key->order) {
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = key;
    }
}

// Calculate flex basis for an item
int calculate_flex_basis(ViewBlock* item, FlexContainerLayout* flex_layout) {
    if (item->flex_basis == -1) {
        // auto - use content size
        return is_main_axis_horizontal(flex_layout) ? item->width : item->height;
    } else if (item->flex_basis_is_percent) {
        // percentage basis
        int container_size = is_main_axis_horizontal(flex_layout) ? 
                           flex_layout->main_axis_size : flex_layout->cross_axis_size;
        return (container_size * item->flex_basis) / 100;
    } else {
        // fixed length
        return item->flex_basis;
    }
}

// Helper function to check if a view is a valid flex item
bool is_valid_flex_item(ViewBlock* item) {
    if (!item) return false;
    return item->type == RDT_VIEW_BLOCK || item->type == RDT_VIEW_INLINE_BLOCK;
}

// Check if main axis is horizontal
bool is_main_axis_horizontal(FlexContainerLayout* flex_layout) {
    // Consider writing mode in axis determination
    if (flex_layout->writing_mode == WM_VERTICAL_RL || 
        flex_layout->writing_mode == WM_VERTICAL_LR) {
        // In vertical writing modes, row becomes vertical
        return flex_layout->direction == LXB_CSS_VALUE_COLUMN ||
               flex_layout->direction == LXB_CSS_VALUE_COLUMN_REVERSE;
    }
    
    return flex_layout->direction == LXB_CSS_VALUE_ROW || 
           flex_layout->direction == LXB_CSS_VALUE_ROW_REVERSE;
}

// Create flex lines based on wrapping
int create_flex_lines(FlexContainerLayout* flex_layout, ViewBlock** items, int item_count) {
    if (!flex_layout || !items || item_count == 0) return 0;
    
    // Ensure we have space for lines
    if (flex_layout->allocated_lines == 0) {
        flex_layout->allocated_lines = 4;
        flex_layout->lines = (FlexLineInfo*)calloc(flex_layout->allocated_lines, sizeof(FlexLineInfo));
    }
    
    int line_count = 0;
    int current_item = 0;
    
    while (current_item < item_count) {
        // Ensure we have space for another line
        if (line_count >= flex_layout->allocated_lines) {
            flex_layout->allocated_lines *= 2;
            flex_layout->lines = (FlexLineInfo*)realloc(flex_layout->lines, 
                                                       flex_layout->allocated_lines * sizeof(FlexLineInfo));
        }
        
        FlexLineInfo* line = &flex_layout->lines[line_count];
        memset(line, 0, sizeof(FlexLineInfo));
        
        // Allocate items array for this line
        line->items = (ViewBlock**)malloc(item_count * sizeof(ViewBlock*));
        line->item_count = 0;
        
        int main_size = 0;
        int container_main_size = is_main_axis_horizontal(flex_layout) ? 
                                 flex_layout->main_axis_size : flex_layout->cross_axis_size;
        
        // Add items to line until we need to wrap
        while (current_item < item_count) {
            ViewBlock* item = items[current_item];
            int item_basis = calculate_flex_basis(item, flex_layout);
            
            // Add gap space if not the first item
            int gap_space = line->item_count > 0 ? 
                (is_main_axis_horizontal(flex_layout) ? flex_layout->column_gap : flex_layout->row_gap) : 0;
            
            // Check if we need to wrap (only if not the first item in line)
            if (flex_layout->wrap != LXB_CSS_VALUE_NOWRAP && 
                line->item_count > 0 && 
                main_size + item_basis + gap_space > container_main_size) {
                break;
            }
            
            line->items[line->item_count] = item;
            line->item_count++;
            main_size += item_basis + gap_space;
            current_item++;
        }
        
        line->main_size = main_size;
        line->free_space = container_main_size - main_size;
        
        // Calculate total flex grow/shrink for this line
        line->total_flex_grow = 0;
        line->total_flex_shrink = 0;
        for (int i = 0; i < line->item_count; i++) {
            line->total_flex_grow += line->items[i]->flex_grow;
            line->total_flex_shrink += line->items[i]->flex_shrink;
        }
        
        line_count++;
    }
    
    flex_layout->line_count = line_count;
    return line_count;
}

// Resolve flexible lengths for a flex line (flex-grow/shrink)
void resolve_flexible_lengths(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    if (!flex_layout || !line || line->item_count == 0) return;
    
    int container_main_size = is_main_axis_horizontal(flex_layout) ? 
                             flex_layout->main_axis_size : flex_layout->cross_axis_size;
    
    // Calculate initial main sizes based on flex-basis
    int total_basis_size = 0;
    for (int i = 0; i < line->item_count; i++) {
        ViewBlock* item = line->items[i];
        int basis = calculate_flex_basis(item, flex_layout);
        set_main_axis_size(item, basis, flex_layout);
        total_basis_size += basis;
    }
    
    // Add gap space
    int gap_space = calculate_gap_space(flex_layout, line->item_count, true);
    total_basis_size += gap_space;
    
    // Calculate free space
    int free_space = container_main_size - total_basis_size;
    line->free_space = free_space;
    
    if (free_space == 0) return;  // No space to distribute
    
    if (free_space > 0 && line->total_flex_grow > 0) {
        // Distribute positive free space using flex-grow
        for (int i = 0; i < line->item_count; i++) {
            ViewBlock* item = line->items[i];
            if (item->flex_grow > 0) {
                int grow_amount = (int)((item->flex_grow / line->total_flex_grow) * free_space);
                int current_size = get_main_axis_size(item, flex_layout);
                set_main_axis_size(item, current_size + grow_amount, flex_layout);
            }
        }
    } else if (free_space < 0 && line->total_flex_shrink > 0) {
        // Distribute negative free space using flex-shrink
        float total_scaled_shrink = 0;
        for (int i = 0; i < line->item_count; i++) {
            ViewBlock* item = line->items[i];
            int basis = calculate_flex_basis(item, flex_layout);
            total_scaled_shrink += item->flex_shrink * basis;
        }
        
        if (total_scaled_shrink > 0) {
            for (int i = 0; i < line->item_count; i++) {
                ViewBlock* item = line->items[i];
                if (item->flex_shrink > 0) {
                    int basis = calculate_flex_basis(item, flex_layout);
                    float scaled_shrink = item->flex_shrink * basis;
                    int shrink_amount = (int)((scaled_shrink / total_scaled_shrink) * (-free_space));
                    int current_size = get_main_axis_size(item, flex_layout);
                    int new_size = current_size - shrink_amount;
                    if (new_size < 0) new_size = 0;  // Prevent negative sizes
                    set_main_axis_size(item, new_size, flex_layout);
                }
            }
        }
    }
}

// Align items on main axis (justify-content)
void align_items_main_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    if (!flex_layout || !line || line->item_count == 0) return;
    
    int container_size = is_main_axis_horizontal(flex_layout) ? 
                        flex_layout->main_axis_size : flex_layout->cross_axis_size;
    int total_item_size = 0;
    for (int i = 0; i < line->item_count; i++) {
        total_item_size += get_main_axis_size(line->items[i], flex_layout);
    }
    
    int gap_space = calculate_gap_space(flex_layout, line->item_count, true);
    total_item_size += gap_space;
    
    int current_pos = 0;
    int spacing = 0;
    
    switch (flex_layout->justify) {
        case LXB_CSS_VALUE_FLEX_START:
            // Items are packed toward the start of the flex direction
            current_pos = 0;
            break;
        case LXB_CSS_VALUE_FLEX_END:
            // Items are packed toward the end of the flex direction
            current_pos = container_size - total_item_size;
            break;
        case LXB_CSS_VALUE_CENTER:
            // Items are centered in the line
            current_pos = (container_size - total_item_size) / 2;
            break;
        case LXB_CSS_VALUE_SPACE_BETWEEN:
            // Items are evenly distributed with first item at start, last at end
            current_pos = 0;
            if (line->item_count > 1) {
                spacing = (container_size - total_item_size) / (line->item_count - 1);
            }
            break;
        case LXB_CSS_VALUE_SPACE_AROUND:
            // Items are evenly distributed with equal space around each item
            if (line->item_count > 0) {
                spacing = (container_size - total_item_size) / line->item_count;
                current_pos = spacing / 2;
            }
            break;
        case LXB_CSS_VALUE_SPACE_EVENLY:
            // Items are evenly distributed with equal space between them
            if (line->item_count > 0) {
                spacing = (container_size - total_item_size) / (line->item_count + 1);
                current_pos = spacing;
            }
            break;
        default:
            current_pos = 0;
            break;
    }
    
    for (int i = 0; i < line->item_count; i++) {
        ViewBlock* item = line->items[i];
        set_main_axis_position(item, current_pos, flex_layout);
        current_pos += get_main_axis_size(item, flex_layout) + spacing;
        
        // Add gap between items
        if (i < line->item_count - 1) {
            current_pos += is_main_axis_horizontal(flex_layout) ? 
                          flex_layout->column_gap : flex_layout->row_gap;
        }
    }
}

// Align items on cross axis (align-items)
void align_items_cross_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    if (!flex_layout || !line || line->item_count == 0) return;
    
    for (int i = 0; i < line->item_count; i++) {
        ViewBlock* item = line->items[i];
        int align_type = item->align_self != LXB_CSS_VALUE_AUTO ? 
                        item->align_self : flex_layout->align_items;
        
        int item_cross_size = get_cross_axis_size(item, flex_layout);
        int line_cross_size = line->cross_size;
        int cross_pos = 0;
        
        switch (align_type) {
            case LXB_CSS_VALUE_FLEX_START:
                // Align to cross-start
                cross_pos = 0;
                break;
            case LXB_CSS_VALUE_FLEX_END:
                // Align to cross-end
                cross_pos = line_cross_size - item_cross_size;
                break;
            case LXB_CSS_VALUE_CENTER:
                // Center in cross axis
                cross_pos = (line_cross_size - item_cross_size) / 2;
                break;
            case LXB_CSS_VALUE_STRETCH:
                // Stretch to fill cross axis (if no explicit cross size)
                cross_pos = 0;
                if (item_cross_size < line_cross_size) {
                    // TODO: Actually stretch the item
                    item_cross_size = line_cross_size;
                }
                break;
            case LXB_CSS_VALUE_BASELINE:
                // Align baselines - TODO: implement proper baseline alignment
                cross_pos = 0;
                break;
            default:
                cross_pos = 0;
                break;
        }
        
        set_cross_axis_position(item, cross_pos, flex_layout);
    }

// Align content (align-content for multiple lines)
void align_content(FlexContainerLayout* flex_layout) {
    if (!flex_layout || flex_layout->line_count <= 1) return;
    
    int container_cross_size = is_main_axis_horizontal(flex_layout) ? 
                              flex_layout->cross_axis_size : flex_layout->main_axis_size;
    
    int total_lines_size = 0;
    for (int i = 0; i < flex_layout->line_count; i++) {
        total_lines_size += flex_layout->lines[i].cross_size;
    }
    
    int gap_space = calculate_gap_space(flex_layout, flex_layout->line_count, false);
    total_lines_size += gap_space;
    
    int free_space = container_cross_size - total_lines_size;
    int start_pos = 0;
    int line_spacing = 0;
    
    switch (flex_layout->align_content) {
        case LXB_CSS_VALUE_FLEX_START:
            start_pos = 0;
            break;
        case LXB_CSS_VALUE_FLEX_END:
            start_pos = free_space;
            break;
        case LXB_CSS_VALUE_CENTER:
            start_pos = free_space / 2;
            break;
        case LXB_CSS_VALUE_SPACE_BETWEEN:
            start_pos = 0;
            line_spacing = flex_layout->line_count > 1 ? free_space / (flex_layout->line_count - 1) : 0;
            break;
        case LXB_CSS_VALUE_SPACE_AROUND:
            line_spacing = flex_layout->line_count > 0 ? free_space / flex_layout->line_count : 0;
            start_pos = line_spacing / 2;
            break;
        case LXB_CSS_VALUE_STRETCH:
            // Distribute extra space among lines
            if (free_space > 0 && flex_layout->line_count > 0) {
                int extra_per_line = free_space / flex_layout->line_count;
                for (int i = 0; i < flex_layout->line_count; i++) {
                    flex_layout->lines[i].cross_size += extra_per_line;
                }
            }
            start_pos = 0;
            break;
        default:
            start_pos = 0;
            break;
    }
    
    // Position lines
    int current_pos = start_pos;
    for (int i = 0; i < flex_layout->line_count; i++) {
        FlexLineInfo* line = &flex_layout->lines[i];
        
        // Move all items in this line to the new cross position
        for (int j = 0; j < line->item_count; j++) {
            ViewBlock* item = line->items[j];
            int current_cross_pos = get_cross_axis_position(item, flex_layout);
            set_cross_axis_position(item, current_pos + current_cross_pos, flex_layout);
        }
        
        current_pos += line->cross_size + line_spacing;
        
        // Add gap between lines
        if (i < flex_layout->line_count - 1) {
            current_pos += is_main_axis_horizontal(flex_layout) ? 
                          flex_layout->row_gap : flex_layout->column_gap;
        }
    }
}

// Utility functions for axis-agnostic positioning
int get_main_axis_size(ViewBlock* item, FlexContainerLayout* flex_layout) {
    return is_main_axis_horizontal(flex_layout) ? item->width : item->height;
}

int get_cross_axis_size(ViewBlock* item, FlexContainerLayout* flex_layout) {
    return is_main_axis_horizontal(flex_layout) ? item->height : item->width;
}

int get_cross_axis_position(ViewBlock* item, FlexContainerLayout* flex_layout) {
    return is_main_axis_horizontal(flex_layout) ? item->y : item->x;
}

void set_main_axis_position(ViewBlock* item, int position, FlexContainerLayout* flex_layout) {
    if (is_main_axis_horizontal(flex_layout)) {
        item->x = position;
    } else {
        item->y = position;
    }
}

void set_cross_axis_position(ViewBlock* item, int position, FlexContainerLayout* flex_layout) {
    if (is_main_axis_horizontal(flex_layout)) {
        item->y = position;
    } else {
        item->x = position;
    }
}

void set_main_axis_size(ViewBlock* item, int size, FlexContainerLayout* flex_layout) {
    if (is_main_axis_horizontal(flex_layout)) {
        item->width = size;
    } else {
        item->height = size;
    }
}

void set_cross_axis_size(ViewBlock* item, int size, FlexContainerLayout* flex_layout) {
    if (is_main_axis_horizontal(flex_layout)) {
        item->height = size;
    } else {
        item->width = size;
    }
}

// Calculate gap space for items or lines
int calculate_gap_space(FlexContainerLayout* flex_layout, int item_count, bool is_main_axis) {
    if (item_count <= 1) return 0;
    
    int gap = is_main_axis ? 
              (is_main_axis_horizontal(flex_layout) ? flex_layout->column_gap : flex_layout->row_gap) :
              (is_main_axis_horizontal(flex_layout) ? flex_layout->row_gap : flex_layout->column_gap);
    
    return gap * (item_count - 1);
}

// Apply gaps between items in a flex line
void apply_gaps(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    if (!flex_layout || !line || line->item_count <= 1) return;
    
    int gap = is_main_axis_horizontal(flex_layout) ? flex_layout->column_gap : flex_layout->row_gap;
    if (gap <= 0) return;
    
    // Apply gaps by adjusting positions
    for (int i = 1; i < line->item_count; i++) {
        ViewBlock* item = line->items[i];
        int current_pos = is_main_axis_horizontal(flex_layout) ? item->x : item->y;
        set_main_axis_position(item, current_pos + (gap * i), flex_layout);
    }
}

// Distribute free space among flex items (grow/shrink)
void distribute_free_space(FlexLineInfo* line, bool is_growing) {
    if (!line || line->item_count == 0) return;
    
    float total_flex = is_growing ? line->total_flex_grow : line->total_flex_shrink;
    if (total_flex <= 0) return;
    
    int free_space = line->free_space;
    if (free_space == 0) return;
    
    // Distribute space proportionally
    for (int i = 0; i < line->item_count; i++) {
        ViewBlock* item = line->items[i];
        float flex_factor = is_growing ? item->flex_grow : item->flex_shrink;
        
        if (flex_factor > 0) {
            int space_to_distribute = (int)((flex_factor / total_flex) * free_space);
            
            // Apply the distributed space to the item's main axis size
            // This is a simplified implementation - real flexbox has more complex rules
            int current_size = is_main_axis_horizontal(line->items[0]->parent ? 
                ((ViewBlock*)line->items[0]->parent)->embed->flex_container : nullptr) ? 
                item->width : item->height;
            
            int new_size = current_size + space_to_distribute;
            if (new_size < 0) new_size = 0;  // Prevent negative sizes
            
            if (is_main_axis_horizontal(line->items[0]->parent ? 
                ((ViewBlock*)line->items[0]->parent)->embed->flex_container : nullptr)) {
                item->width = new_size;
            } else {
                item->height = new_size;
            }
        }
    }
}

// Calculate cross sizes for all flex lines
void calculate_line_cross_sizes(FlexContainerLayout* flex_layout) {
    if (!flex_layout || flex_layout->line_count == 0) return;
    
    for (int i = 0; i < flex_layout->line_count; i++) {
        FlexLineInfo* line = &flex_layout->lines[i];
        int max_cross_size = 0;
        
        // Find the maximum cross size among items in this line
        for (int j = 0; j < line->item_count; j++) {
            ViewBlock* item = line->items[j];
            int item_cross_size = get_cross_axis_size(item, flex_layout);
            if (item_cross_size > max_cross_size) {
                max_cross_size = item_cross_size;
            }
        }
        
        line->cross_size = max_cross_size;
    }
}
