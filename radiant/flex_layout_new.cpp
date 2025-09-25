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
    
    // Set default values
    flex->direction = DIR_ROW;
    flex->wrap = WRAP_NOWRAP;
    flex->justify = JUSTIFY_START;
    flex->align_items = ALIGN_STRETCH;
    flex->align_content = ALIGN_STRETCH;
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

// Collect flex items from container children
int collect_flex_items(ViewBlock* container, ViewBlock*** items) {
    if (!container || !items) return 0;
    
    FlexContainerLayout* flex = container->embed->flex_container;
    if (!flex) return 0;
    
    int count = 0;
    
    // Count children first
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
    
    // Collect items
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
        // absolute value
        return item->flex_basis;
    }
}

// Create flex lines based on wrapping
int create_flex_lines(FlexContainerLayout* flex_layout, ViewBlock** items, int item_count) {
    if (!flex_layout || !items || item_count == 0) return 0;
    
    flex_layout->line_count = 0;
    
    if (flex_layout->wrap == WRAP_NOWRAP) {
        // Single line - all items go in one line
        if (flex_layout->allocated_lines < 1) {
            flex_layout->allocated_lines = 1;
            flex_layout->lines = (FlexLineInfo*)realloc(flex_layout->lines, sizeof(FlexLineInfo));
        }
        
        FlexLineInfo* line = &flex_layout->lines[0];
        line->items = (ViewBlock**)malloc(item_count * sizeof(ViewBlock*));
        line->item_count = item_count;
        line->main_size = 0;
        line->cross_size = 0;
        line->total_flex_grow = 0;
        line->total_flex_shrink = 0;
        
        for (int i = 0; i < item_count; ++i) {
            line->items[i] = items[i];
            line->main_size += calculate_flex_basis(items[i], flex_layout);
            line->total_flex_grow += items[i]->flex_grow;
            line->total_flex_shrink += items[i]->flex_shrink;
            
            int cross_size = get_cross_axis_size(items[i], flex_layout);
            if (cross_size > line->cross_size) {
                line->cross_size = cross_size;
            }
        }
        
        // Add gaps
        if (item_count > 1) {
            line->main_size += calculate_gap_space(flex_layout, item_count, true);
        }
        
        line->free_space = flex_layout->main_axis_size - line->main_size;
        flex_layout->line_count = 1;
        
    } else {
        // Multi-line wrapping
        int current_line = 0;
        int line_start = 0;
        
        while (line_start < item_count) {
            // Ensure we have space for another line
            if (current_line >= flex_layout->allocated_lines) {
                flex_layout->allocated_lines *= 2;
                flex_layout->lines = (FlexLineInfo*)realloc(flex_layout->lines, 
                                                           flex_layout->allocated_lines * sizeof(FlexLineInfo));
            }
            
            FlexLineInfo* line = &flex_layout->lines[current_line];
            line->main_size = 0;
            line->cross_size = 0;
            line->total_flex_grow = 0;
            line->total_flex_shrink = 0;
            
            int line_end = line_start;
            int tentative_size = 0;
            
            // Add items to line until we exceed container width
            while (line_end < item_count) {
                int item_basis = calculate_flex_basis(items[line_end], flex_layout);
                int gap_space = (line_end > line_start) ? 
                              (is_main_axis_horizontal(flex_layout) ? flex_layout->column_gap : flex_layout->row_gap) : 0;
                
                if (line_end > line_start && tentative_size + gap_space + item_basis > flex_layout->main_axis_size) {
                    break; // Would overflow, start new line
                }
                
                tentative_size += gap_space + item_basis;
                line_end++;
            }
            
            // If no items fit (shouldn't happen with reasonable content), force at least one
            if (line_end == line_start) {
                line_end = line_start + 1;
            }
            
            // Create the line
            int line_item_count = line_end - line_start;
            line->items = (ViewBlock**)malloc(line_item_count * sizeof(ViewBlock*));
            line->item_count = line_item_count;
            
            for (int i = 0; i < line_item_count; ++i) {
                ViewBlock* item = items[line_start + i];
                line->items[i] = item;
                line->main_size += calculate_flex_basis(item, flex_layout);
                line->total_flex_grow += item->flex_grow;
                line->total_flex_shrink += item->flex_shrink;
                
                int cross_size = get_cross_axis_size(item, flex_layout);
                if (cross_size > line->cross_size) {
                    line->cross_size = cross_size;
                }
            }
            
            // Add gaps
            if (line_item_count > 1) {
                line->main_size += calculate_gap_space(flex_layout, line_item_count, true);
            }
            
            line->free_space = flex_layout->main_axis_size - line->main_size;
            
            current_line++;
            line_start = line_end;
        }
        
        flex_layout->line_count = current_line;
    }
    
    return flex_layout->line_count;
}

// Distribute free space among flex items in a line
void distribute_free_space(FlexLineInfo* line, bool is_growing) {
    if (!line || line->item_count == 0) return;
    
    if (is_growing && line->total_flex_grow > 0 && line->free_space > 0) {
        // Distribute positive free space
        float remaining_space = (float)line->free_space;
        
        for (int i = 0; i < line->item_count; ++i) {
            ViewBlock* item = line->items[i];
            if (item->flex_grow > 0) {
                float grow_ratio = item->flex_grow / line->total_flex_grow;
                int additional_space = (int)(remaining_space * grow_ratio);
                
                // Update item size (this will be applied to width/height later)
                item->content_width += additional_space; // Temporary storage
            }
        }
    } else if (!is_growing && line->total_flex_shrink > 0 && line->free_space < 0) {
        // Distribute negative free space (shrinking)
        float total_weighted_shrink = 0;
        for (int i = 0; i < line->item_count; ++i) {
            ViewBlock* item = line->items[i];
            int basis = calculate_flex_basis(item, nullptr); // Pass nullptr for now
            total_weighted_shrink += item->flex_shrink * basis;
        }
        
        if (total_weighted_shrink > 0) {
            float shrink_space = (float)(-line->free_space);
            
            for (int i = 0; i < line->item_count; ++i) {
                ViewBlock* item = line->items[i];
                if (item->flex_shrink > 0) {
                    int basis = calculate_flex_basis(item, nullptr);
                    float weighted_shrink = item->flex_shrink * basis;
                    float shrink_ratio = weighted_shrink / total_weighted_shrink;
                    int shrink_amount = (int)(shrink_space * shrink_ratio);
                    
                    // Update item size (temporary storage)
                    item->content_width -= shrink_amount;
                }
            }
        }
    }
}

// Resolve flexible lengths for a line
void resolve_flexible_lengths(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    if (!flex_layout || !line) return;
    
    // First, set initial sizes based on flex-basis
    for (int i = 0; i < line->item_count; ++i) {
        ViewBlock* item = line->items[i];
        int basis = calculate_flex_basis(item, flex_layout);
        
        if (is_main_axis_horizontal(flex_layout)) {
            item->content_width = basis;
        } else {
            item->content_height = basis;
        }
    }
    
    // Then distribute free space
    if (line->free_space > 0) {
        distribute_free_space(line, true);  // Growing
    } else if (line->free_space < 0) {
        distribute_free_space(line, false); // Shrinking
    }
}

// Align items along main axis within a line
void align_items_main_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    if (!flex_layout || !line || line->item_count == 0) return;
    
    int container_size = flex_layout->main_axis_size;
    int content_size = line->main_size;
    int free_space = container_size - content_size;
    
    int start_pos = 0;
    int item_spacing = 0;
    
    switch (flex_layout->justify) {
        case JUSTIFY_START:
            start_pos = 0;
            break;
            
        case JUSTIFY_END:
            start_pos = free_space;
            break;
            
        case JUSTIFY_CENTER:
            start_pos = free_space / 2;
            break;
            
        case JUSTIFY_SPACE_BETWEEN:
            start_pos = 0;
            if (line->item_count > 1) {
                item_spacing = free_space / (line->item_count - 1);
            }
            break;
            
        case JUSTIFY_SPACE_AROUND:
            if (line->item_count > 0) {
                item_spacing = free_space / line->item_count;
                start_pos = item_spacing / 2;
            }
            break;
            
        case JUSTIFY_SPACE_EVENLY:
            if (line->item_count > 0) {
                item_spacing = free_space / (line->item_count + 1);
                start_pos = item_spacing;
            }
            break;
    }
    
    // Position items
    int current_pos = start_pos;
    for (int i = 0; i < line->item_count; ++i) {
        ViewBlock* item = line->items[i];
        set_main_axis_position(item, current_pos, flex_layout);
        
        int item_size = get_main_axis_size(item, flex_layout);
        current_pos += item_size + item_spacing;
        
        // Add gap
        if (i < line->item_count - 1) {
            current_pos += is_main_axis_horizontal(flex_layout) ? 
                          flex_layout->column_gap : flex_layout->row_gap;
        }
    }
}

// Align items along cross axis within a line
void align_items_cross_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    if (!flex_layout || !line) return;
    
    for (int i = 0; i < line->item_count; ++i) {
        ViewBlock* item = line->items[i];
        AlignType align = (item->align_self != ALIGN_START) ? item->align_self : flex_layout->align_items;
        
        int item_cross_size = get_cross_axis_size(item, flex_layout);
        int line_cross_size = line->cross_size;
        int position = 0;
        
        switch (align) {
            case ALIGN_START:
                position = 0;
                break;
                
            case ALIGN_END:
                position = line_cross_size - item_cross_size;
                break;
                
            case ALIGN_CENTER:
                position = (line_cross_size - item_cross_size) / 2;
                break;
                
            case ALIGN_STRETCH:
                position = 0;
                set_cross_axis_size(item, line_cross_size, flex_layout);
                break;
                
            case ALIGN_BASELINE:
                // For now, treat as flex-start
                // TODO: Implement proper baseline alignment
                position = 0;
                break;
                
            default:
                position = 0;
                break;
        }
        
        set_cross_axis_position(item, position, flex_layout);
    }
}

// Utility functions for axis-aware operations
bool is_main_axis_horizontal(FlexContainerLayout* flex_layout) {
    if (!flex_layout) return true;
    
    bool is_row = (flex_layout->direction == DIR_ROW || flex_layout->direction == DIR_ROW_REVERSE);
    bool is_horizontal_writing = (flex_layout->writing_mode == WM_HORIZONTAL_TB);
    
    return (is_row && is_horizontal_writing) || (!is_row && !is_horizontal_writing);
}

int get_main_axis_size(ViewBlock* item, FlexContainerLayout* flex_layout) {
    return is_main_axis_horizontal(flex_layout) ? item->width : item->height;
}

int get_cross_axis_size(ViewBlock* item, FlexContainerLayout* flex_layout) {
    return is_main_axis_horizontal(flex_layout) ? item->height : item->width;
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

// Calculate gap space for items
int calculate_gap_space(FlexContainerLayout* flex_layout, int item_count, bool is_main_axis) {
    if (!flex_layout || item_count <= 1) return 0;
    
    int gap = is_main_axis ? 
              (is_main_axis_horizontal(flex_layout) ? flex_layout->column_gap : flex_layout->row_gap) :
              (is_main_axis_horizontal(flex_layout) ? flex_layout->row_gap : flex_layout->column_gap);
    
    return gap * (item_count - 1);
}

// Main flex layout function
void layout_flex_container_new(LayoutContext* lycon, ViewBlock* container) {
    if (!lycon || !container || !container->embed || !container->embed->flex_container) {
        return;
    }
    
    FlexContainerLayout* flex_layout = container->embed->flex_container;
    
    // Set container dimensions
    flex_layout->main_axis_size = is_main_axis_horizontal(flex_layout) ? 
                                 container->width : container->height;
    flex_layout->cross_axis_size = is_main_axis_horizontal(flex_layout) ? 
                                  container->height : container->width;
    
    // Step 1: Collect flex items
    ViewBlock** items;
    int item_count = collect_flex_items(container, &items);
    
    if (item_count == 0) {
        return; // No items to layout
    }
    
    // Step 2: Sort items by order
    sort_flex_items_by_order(items, item_count);
    
    // Step 3: Create flex lines
    create_flex_lines(flex_layout, items, item_count);
    
    // Step 4: Resolve flexible lengths and align items for each line
    int line_y_offset = 0;
    
    for (int line_idx = 0; line_idx < flex_layout->line_count; ++line_idx) {
        FlexLineInfo* line = &flex_layout->lines[line_idx];
        
        // Resolve flexible lengths
        resolve_flexible_lengths(flex_layout, line);
        
        // Align items on main axis
        align_items_main_axis(flex_layout, line);
        
        // Align items on cross axis
        align_items_cross_axis(flex_layout, line);
        
        // Offset items by line position
        for (int i = 0; i < line->item_count; ++i) {
            ViewBlock* item = line->items[i];
            if (!is_main_axis_horizontal(flex_layout)) {
                item->x += line_y_offset;
            } else {
                item->y += line_y_offset;
            }
        }
        
        line_y_offset += line->cross_size;
        if (line_idx < flex_layout->line_count - 1) {
            line_y_offset += is_main_axis_horizontal(flex_layout) ? 
                           flex_layout->row_gap : flex_layout->column_gap;
        }
    }
    
    // Step 5: Align content (align lines within container)
    align_content(flex_layout);
    
    // Update container content size
    container->content_width = flex_layout->main_axis_size;
    container->content_height = line_y_offset;
    
    flex_layout->needs_reflow = false;
}

// Align content (lines within container)
void align_content(FlexContainerLayout* flex_layout) {
    if (!flex_layout || flex_layout->line_count <= 1) return;
    
    int total_cross_size = 0;
    for (int i = 0; i < flex_layout->line_count; ++i) {
        total_cross_size += flex_layout->lines[i].cross_size;
    }
    
    // Add gaps between lines
    if (flex_layout->line_count > 1) {
        int gap = is_main_axis_horizontal(flex_layout) ? flex_layout->row_gap : flex_layout->column_gap;
        total_cross_size += gap * (flex_layout->line_count - 1);
    }
    
    int free_space = flex_layout->cross_axis_size - total_cross_size;
    if (free_space <= 0) return;
    
    int offset = 0;
    int line_spacing = 0;
    
    switch (flex_layout->align_content) {
        case ALIGN_START:
            offset = 0;
            break;
            
        case ALIGN_END:
            offset = free_space;
            break;
            
        case ALIGN_CENTER:
            offset = free_space / 2;
            break;
            
        case ALIGN_SPACE_BETWEEN:
            if (flex_layout->line_count > 1) {
                line_spacing = free_space / (flex_layout->line_count - 1);
            }
            break;
            
        case ALIGN_SPACE_AROUND:
            line_spacing = free_space / flex_layout->line_count;
            offset = line_spacing / 2;
            break;
            
        case ALIGN_SPACE_EVENLY:
            line_spacing = free_space / (flex_layout->line_count + 1);
            offset = line_spacing;
            break;
            
        default:
            break;
    }
    
    // Apply offsets to all items in each line
    int current_offset = offset;
    for (int line_idx = 0; line_idx < flex_layout->line_count; ++line_idx) {
        FlexLineInfo* line = &flex_layout->lines[line_idx];
        
        for (int i = 0; i < line->item_count; ++i) {
            ViewBlock* item = line->items[i];
            if (is_main_axis_horizontal(flex_layout)) {
                item->y += current_offset;
            } else {
                item->x += current_offset;
            }
        }
        
        current_offset += line->cross_size + line_spacing;
        if (line_idx < flex_layout->line_count - 1) {
            int gap = is_main_axis_horizontal(flex_layout) ? flex_layout->row_gap : flex_layout->column_gap;
            current_offset += gap;
        }
    }
}
