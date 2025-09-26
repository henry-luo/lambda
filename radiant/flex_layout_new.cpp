#include "flex_layout_new.hpp"
#include "layout.hpp"
#include "view.hpp"
extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
}

// Forward declarations for static helper functions
static int convert_wrap_to_lexbor(int wrap);
static int convert_justify_to_lexbor(int justify);
static int convert_align_to_lexbor(int align);

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
    
    // Phase 9: Handle wrap-reverse if needed
    int wrap = convert_wrap_to_lexbor(flex_layout->wrap);
    if (wrap == LXB_CSS_VALUE_WRAP_REVERSE && is_main_axis_horizontal(flex_layout)) {
        // Reverse the cross-axis positions for wrap-reverse
        int container_cross_size = flex_layout->cross_axis_size;
        for (int i = 0; i < line_count; i++) {
            FlexLineInfo* line = &flex_layout->lines[i];
            for (int j = 0; j < line->item_count; j++) {
                ViewBlock* item = line->items[j];
                int current_cross_pos = get_cross_axis_position(item, flex_layout);
                int item_cross_size = get_cross_axis_size(item, flex_layout);
                set_cross_axis_position(item, container_cross_size - current_cross_pos - item_cross_size, flex_layout);
            }
        }
    }
    
    flex_layout->needs_reflow = false;
}

// Collect flex items from container children
int collect_flex_items(ViewBlock* container, ViewBlock*** items) {
    if (!container || !items) return 0;
    
    FlexContainerLayout* flex = container->embed->flex_container;
    if (!flex) return 0;
    
    int count = 0;
    
    // Count children first - use ViewGroup hierarchy, filter by position and visibility
    View* child = container->child;
    while (child) {
        if ((child->type == RDT_VIEW_BLOCK || child->type == RDT_VIEW_INLINE_BLOCK)) {
            ViewBlock* block = (ViewBlock*)child;
            // Filter out absolutely positioned and hidden items
            if (block->position != POS_ABSOLUTE && block->visibility != VIS_HIDDEN) {
                count++;
            }
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
    
    // Collect items - use ViewGroup hierarchy, apply filtering
    count = 0;
    child = container->child;
    while (child) {
        if ((child->type == RDT_VIEW_BLOCK || child->type == RDT_VIEW_INLINE_BLOCK)) {
            ViewBlock* block = (ViewBlock*)child;
            // Filter out absolutely positioned and hidden items
            if (block->position != POS_ABSOLUTE && block->visibility != VIS_HIDDEN) {
                flex->flex_items[count] = block;
                
                // Apply constraints and resolve percentages
                apply_constraints(block, 
                    is_main_axis_horizontal(flex) ? flex->main_axis_size : flex->cross_axis_size,
                    is_main_axis_horizontal(flex) ? flex->cross_axis_size : flex->main_axis_size);
                
                count++;
            }
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

// Helper function for clamping values with min/max constraints
float clamp_value(float value, float min_val, float max_val) {
    if (max_val > 0) {
        return fmin(fmax(value, min_val), max_val);
    }
    return fmax(value, min_val);
}

// Helper function to resolve percentage values
int resolve_percentage(int value, bool is_percent, int container_size) {
    if (is_percent) {
        float percentage = (float)value / 100.0f;
        return (int)(percentage * container_size);
    }
    return value;
}

// Apply constraints including aspect ratio and min/max values
void apply_constraints(ViewBlock* item, int container_width, int container_height) {
    if (!item) return;
    
    // Resolve percentage-based values
    int actual_width = resolve_percentage(item->width, item->width_is_percent, container_width);
    int actual_height = resolve_percentage(item->height, item->height_is_percent, container_height);
    int min_width = resolve_percentage(item->min_width, item->min_width_is_percent, container_width);
    int max_width = resolve_percentage(item->max_width, item->max_width_is_percent, container_width);
    int min_height = resolve_percentage(item->min_height, item->min_height_is_percent, container_height);
    int max_height = resolve_percentage(item->max_height, item->max_height_is_percent, container_height);
    
    // Apply aspect ratio if specified
    if (item->aspect_ratio > 0) {
        if (actual_width > 0 && actual_height == 0) {
            actual_height = (int)(actual_width / item->aspect_ratio);
        } else if (actual_height > 0 && actual_width == 0) {
            actual_width = (int)(actual_height * item->aspect_ratio);
        }
    }
    
    // Apply min/max constraints
    actual_width = (int)clamp_value(actual_width, min_width, max_width);
    actual_height = (int)clamp_value(actual_height, min_height, max_height);
    
    // Reapply aspect ratio after clamping if needed
    if (item->aspect_ratio > 0) {
        if (actual_width > 0 && actual_height == 0) {
            actual_height = (int)(actual_width / item->aspect_ratio);
        } else if (actual_height > 0 && actual_width == 0) {
            actual_width = (int)(actual_height * item->aspect_ratio);
        }
    }
    
    // Update item dimensions
    item->width = actual_width;
    item->height = actual_height;
}

// Find maximum baseline in a flex line for baseline alignment
int find_max_baseline(FlexLineInfo* line) {
    int max_baseline = 0;
    bool found = false;
    
    for (int i = 0; i < line->item_count; i++) {
        ViewBlock* item = line->items[i];
        if (convert_align_to_lexbor(item->align_self) == LXB_CSS_VALUE_BASELINE) {
            int baseline = item->baseline_offset;
            if (baseline <= 0) {
                // Default to 3/4 of height if no explicit baseline
                baseline = (int)(item->height * 0.75);
            }
            if (baseline > max_baseline) {
                max_baseline = baseline;
            }
            found = true;
        }
    }
    
    return found ? max_baseline : 0;
}

// Helper function to convert old enum constants to Lexbor constants
static int convert_direction_to_lexbor(int direction) {
    switch (direction) {
        case 0: return LXB_CSS_VALUE_ROW;           // DIR_ROW
        case 1: return LXB_CSS_VALUE_ROW_REVERSE;   // DIR_ROW_REVERSE  
        case 2: return LXB_CSS_VALUE_COLUMN;        // DIR_COLUMN
        case 3: return LXB_CSS_VALUE_COLUMN_REVERSE; // DIR_COLUMN_REVERSE
        default: return direction; // Already Lexbor constant
    }
}

static int convert_wrap_to_lexbor(int wrap) {
    switch (wrap) {
        case 0: return LXB_CSS_VALUE_NOWRAP;        // WRAP_NOWRAP
        case 1: return LXB_CSS_VALUE_WRAP;          // WRAP_WRAP
        case 2: return LXB_CSS_VALUE_WRAP_REVERSE;  // WRAP_WRAP_REVERSE
        default: return wrap; // Already Lexbor constant
    }
}

static int convert_justify_to_lexbor(int justify) {
    switch (justify) {
        case 0: return LXB_CSS_VALUE_FLEX_START;    // JUSTIFY_START
        case 1: return LXB_CSS_VALUE_FLEX_END;      // JUSTIFY_END
        case 2: return LXB_CSS_VALUE_CENTER;        // JUSTIFY_CENTER
        case 3: return LXB_CSS_VALUE_SPACE_BETWEEN; // JUSTIFY_SPACE_BETWEEN
        case 4: return LXB_CSS_VALUE_SPACE_AROUND;  // JUSTIFY_SPACE_AROUND
        case 5: return LXB_CSS_VALUE_SPACE_EVENLY;  // JUSTIFY_SPACE_EVENLY
        default: return justify; // Already Lexbor constant
    }
}

static int convert_align_to_lexbor(int align) {
    switch (align) {
        case 0: return LXB_CSS_VALUE_AUTO;          // ALIGN_AUTO
        case 1: return LXB_CSS_VALUE_FLEX_START;    // ALIGN_START
        case 2: return LXB_CSS_VALUE_FLEX_END;      // ALIGN_END
        case 3: return LXB_CSS_VALUE_CENTER;        // ALIGN_CENTER
        case 4: return LXB_CSS_VALUE_BASELINE;      // ALIGN_BASELINE
        case 5: return LXB_CSS_VALUE_STRETCH;       // ALIGN_STRETCH
        default: return align; // Already Lexbor constant
    }
}

// Check if main axis is horizontal
bool is_main_axis_horizontal(FlexContainerLayout* flex_layout) {
    int direction = convert_direction_to_lexbor(flex_layout->direction);
    
    // Consider writing mode in axis determination
    if (flex_layout->writing_mode == WM_VERTICAL_RL || 
        flex_layout->writing_mode == WM_VERTICAL_LR) {
        // In vertical writing modes, row becomes vertical
        return direction == LXB_CSS_VALUE_COLUMN ||
               direction == LXB_CSS_VALUE_COLUMN_REVERSE;
    }
    
    return direction == LXB_CSS_VALUE_ROW || 
           direction == LXB_CSS_VALUE_ROW_REVERSE;
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
            int wrap = convert_wrap_to_lexbor(flex_layout->wrap);
            if (wrap != LXB_CSS_VALUE_NOWRAP && 
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
                int new_size = current_size + grow_amount;
                set_main_axis_size(item, new_size, flex_layout);
                
                // Adjust cross axis size based on aspect ratio
                if (item->aspect_ratio > 0) {
                    if (is_main_axis_horizontal(flex_layout)) {
                        item->height = (int)(new_size / item->aspect_ratio);
                    } else {
                        item->width = (int)(new_size * item->aspect_ratio);
                    }
                }
                
                // Apply constraints after flex adjustment
                apply_constraints(item, 
                    is_main_axis_horizontal(flex_layout) ? flex_layout->main_axis_size : flex_layout->cross_axis_size,
                    is_main_axis_horizontal(flex_layout) ? flex_layout->cross_axis_size : flex_layout->main_axis_size);
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
                    
                    // Adjust cross axis size based on aspect ratio
                    if (item->aspect_ratio > 0) {
                        if (is_main_axis_horizontal(flex_layout)) {
                            item->height = (int)(new_size / item->aspect_ratio);
                        } else {
                            item->width = (int)(new_size * item->aspect_ratio);
                        }
                    }
                    
                    // Apply constraints after flex adjustment
                    apply_constraints(item, 
                        is_main_axis_horizontal(flex_layout) ? flex_layout->main_axis_size : flex_layout->cross_axis_size,
                        is_main_axis_horizontal(flex_layout) ? flex_layout->cross_axis_size : flex_layout->main_axis_size);
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
    
    // Check for auto margins on main axis
    int auto_margin_count = 0;
    for (int i = 0; i < line->item_count; i++) {
        ViewBlock* item = line->items[i];
        if (is_main_axis_horizontal(flex_layout)) {
            if (item->margin_left_auto) auto_margin_count++;
            if (item->margin_right_auto) auto_margin_count++;
        } else {
            if (item->margin_top_auto) auto_margin_count++;
            if (item->margin_bottom_auto) auto_margin_count++;
        }
    }
    
    int current_pos = 0;
    int spacing = 0;
    float auto_margin_size = 0;
    
    if (auto_margin_count > 0 && container_size > total_item_size) {
        // Distribute free space among auto margins
        auto_margin_size = (float)(container_size - total_item_size) / auto_margin_count;
    } else {
        // Apply justify-content if no auto margins
        int justify = convert_justify_to_lexbor(flex_layout->justify);
        switch (justify) {
            case LXB_CSS_VALUE_FLEX_START:
                current_pos = 0;
                break;
            case LXB_CSS_VALUE_FLEX_END:
                current_pos = container_size - total_item_size;
                break;
            case LXB_CSS_VALUE_CENTER:
                current_pos = (container_size - total_item_size) / 2;
                break;
            case LXB_CSS_VALUE_SPACE_BETWEEN:
                current_pos = 0;
                if (line->item_count > 1) {
                    spacing = (container_size - total_item_size) / (line->item_count - 1);
                }
                break;
            case LXB_CSS_VALUE_SPACE_AROUND:
                if (line->item_count > 0) {
                    spacing = (container_size - total_item_size) / line->item_count;
                    current_pos = spacing / 2;
                }
                break;
            case LXB_CSS_VALUE_SPACE_EVENLY:
                if (line->item_count > 0) {
                    spacing = (container_size - total_item_size) / (line->item_count + 1);
                    current_pos = spacing;
                }
                break;
            default:
                current_pos = 0;
                break;
        }
    }
    
    for (int i = 0; i < line->item_count; i++) {
        ViewBlock* item = line->items[i];
        
        // Handle auto margins
        if (auto_margin_count > 0) {
            bool left_auto = is_main_axis_horizontal(flex_layout) ? 
                           item->margin_left_auto : item->margin_top_auto;
            bool right_auto = is_main_axis_horizontal(flex_layout) ? 
                            item->margin_right_auto : item->margin_bottom_auto;
            
            if (left_auto && right_auto) {
                // Center item with auto margins on both sides
                int remaining_space = container_size - get_main_axis_size(item, flex_layout);
                current_pos = remaining_space / 2;
            } else {
                if (left_auto) current_pos += (int)auto_margin_size;
                
                set_main_axis_position(item, current_pos, flex_layout);
                current_pos += get_main_axis_size(item, flex_layout);
                
                if (right_auto) current_pos += (int)auto_margin_size;
            }
        } else {
            set_main_axis_position(item, current_pos, flex_layout);
            current_pos += get_main_axis_size(item, flex_layout) + spacing;
        }
        
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
    
    // Find maximum baseline for baseline alignment
    int max_baseline = find_max_baseline(line);
    
    for (int i = 0; i < line->item_count; i++) {
        ViewBlock* item = line->items[i];
        int item_align = convert_align_to_lexbor(item->align_self);
        int container_align = convert_align_to_lexbor(flex_layout->align_items);
        int align_type = item_align != LXB_CSS_VALUE_AUTO ? 
                        item_align : container_align;
        
        int item_cross_size = get_cross_axis_size(item, flex_layout);
        int line_cross_size = line->cross_size;
        int cross_pos = 0;
        
        // Check for auto margins in cross axis
        bool top_auto = is_main_axis_horizontal(flex_layout) ? 
                       item->margin_top_auto : item->margin_left_auto;
        bool bottom_auto = is_main_axis_horizontal(flex_layout) ? 
                          item->margin_bottom_auto : item->margin_right_auto;
        
        if (top_auto || bottom_auto) {
            // Handle auto margins in cross axis
            int container_cross_size = is_main_axis_horizontal(flex_layout) ? 
                                     flex_layout->cross_axis_size : flex_layout->main_axis_size;
            
            if (top_auto && bottom_auto) {
                // Center item with auto margins on both sides
                cross_pos = (container_cross_size - item_cross_size) / 2;
            } else if (top_auto) {
                // Push item to bottom
                cross_pos = container_cross_size - item_cross_size;
            } else if (bottom_auto) {
                // Keep item at top
                cross_pos = 0;
            }
        } else {
            // Regular alignment
            switch (align_type) {
                case LXB_CSS_VALUE_FLEX_START:
                    cross_pos = 0;
                    break;
                case LXB_CSS_VALUE_FLEX_END:
                    cross_pos = line_cross_size - item_cross_size;
                    break;
                case LXB_CSS_VALUE_CENTER:
                    cross_pos = (line_cross_size - item_cross_size) / 2;
                    break;
                case LXB_CSS_VALUE_STRETCH:
                    cross_pos = 0;
                    if (item_cross_size < line_cross_size) {
                        // Actually stretch the item
                        set_cross_axis_size(item, line_cross_size, flex_layout);
                        item_cross_size = line_cross_size;
                    }
                    break;
                case LXB_CSS_VALUE_BASELINE:
                    if (is_main_axis_horizontal(flex_layout)) {
                        // Calculate baseline offset
                        int baseline = item->baseline_offset;
                        if (baseline <= 0) {
                            baseline = (int)(item->height * 0.75);
                        }
                        // Position item so its baseline aligns with max baseline
                        cross_pos = max_baseline - baseline;
                    } else {
                        // For column direction, baseline is equivalent to start
                        cross_pos = 0;
                    }
                    break;
                default:
                    cross_pos = 0;
                    break;
            }
        }
        
        set_cross_axis_position(item, cross_pos, flex_layout);
    }
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
    
    int align_content = convert_align_to_lexbor(flex_layout->align_content);
    switch (align_content) {
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
