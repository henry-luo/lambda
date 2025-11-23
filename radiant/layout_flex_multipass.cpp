#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"

#include "../lib/log.h"

// Forward declarations
void layout_final_flex_content(LayoutContext* lycon, ViewBlock* flex_container);
void run_enhanced_flex_algorithm(LayoutContext* lycon, ViewBlock* flex_container);
bool has_main_axis_auto_margins(FlexLineInfo* line);
void handle_main_axis_auto_margins(FlexContainerLayout* flex_layout, FlexLineInfo* line);
bool has_auto_margins(ViewBlock* item);
void apply_auto_margin_centering(LayoutContext* lycon, ViewBlock* flex_container);

// Multi-pass flex layout implementation
// This implements the enhanced flex layout with proper content measurement

void layout_flex_container_with_nested_content(LayoutContext* lycon, ViewBlock* flex_container) {
    if (!flex_container) return;

    printf("DEBUG: ENHANCED FLEX LAYOUT STARTING\n");
    log_debug("Starting enhanced flex layout for container %p", flex_container);

    // Clear measurement cache for this layout pass
    clear_measurement_cache();

    // PASS 1: Run enhanced flex algorithm with measured content
    log_debug("Pass 1: Running enhanced flex algorithm with measured content");

    // Use enhanced flex algorithm with auto margin support
    run_enhanced_flex_algorithm(lycon, flex_container);

    // PASS 2: Final content layout with determined flex sizes
    log_debug("Pass 2: Final content layout");
    layout_final_flex_content(lycon, flex_container);

    log_debug("Enhanced flex layout completed");
}
// Enhanced flex algorithm with auto margin support
void run_enhanced_flex_algorithm(LayoutContext* lycon, ViewBlock* flex_container) {
    printf("DEBUG: ENHANCED FLEX ALGORITHM STARTING\n");
    log_debug("Running enhanced flex algorithm with auto margin support");

    // Note: space-evenly workaround now handled via x-justify-content custom property in resolve_style.cpp

    // First, run the existing flex algorithm
    layout_flex_container(lycon, flex_container);

    // Then apply auto margin enhancements
    apply_auto_margin_centering(lycon, flex_container);

    printf("DEBUG: ENHANCED FLEX ALGORITHM COMPLETED\n");
    log_debug("Enhanced flex algorithm completed");
}

// Apply auto margin centering after flex algorithm
void apply_auto_margin_centering(LayoutContext* lycon, ViewBlock* flex_container) {
    printf("DEBUG: AUTO MARGIN CENTERING STARTING\n");
    log_debug("Applying auto margin centering");

    if (!flex_container || !flex_container->child) return;

    FlexContainerLayout* flex_layout = lycon->flex_container;
    if (!flex_layout) return;

    // Check each flex item for auto margins
    View* child = flex_container->child;
    int item_count = 0;
    while (child) {
        if (child->type == RDT_VIEW_BLOCK) {
            ViewBlock* item = (ViewBlock*)child;
            item_count++;
            printf("DEBUG: Checking item %d for auto margins\n", item_count);

            if (has_auto_margins(item)) {
                log_debug("Found item with auto margins: %p", item);

                // Calculate centering position
                int container_width = flex_container->width;
                int container_height = flex_container->height;

                // Account for container padding and border
                if (flex_container->bound) {
                    container_width -= (flex_container->bound->padding.left + flex_container->bound->padding.right);
                    container_height -= (flex_container->bound->padding.top + flex_container->bound->padding.bottom);

                    if (flex_container->bound->border) {
                        container_width -= (flex_container->bound->border->width.left + flex_container->bound->border->width.right);
                        container_height -= (flex_container->bound->border->width.top + flex_container->bound->border->width.bottom);
                    }
                }

                // Center the item
                if (item->bound && item->bound->margin.left_type == CSS_VALUE_AUTO && item->bound->margin.right_type == CSS_VALUE_AUTO) {
                    // Center horizontally
                    int center_x = (container_width - item->width) / 2;
                    if (flex_container->bound) {
                        center_x += flex_container->bound->padding.left;
                        if (flex_container->bound->border) {
                            center_x += flex_container->bound->border->width.left;
                        }
                    }
                    item->x = center_x;
                    log_debug("Centered item horizontally at x=%d", center_x);
                }

                if (item->bound && item->bound->margin.top_type == CSS_VALUE_AUTO && item->bound->margin.bottom_type == CSS_VALUE_AUTO) {
                    // Center vertically
                    int center_y = (container_height - item->height) / 2;
                    if (flex_container->bound) {
                        center_y += flex_container->bound->padding.top;
                        if (flex_container->bound->border) {
                            center_y += flex_container->bound->border->width.top;
                        }
                    }
                    item->y = center_y;
                    log_debug("Centered item vertically at y=%d", center_y);
                }
            }
        }
        child = child->next;
    }

    log_debug("Auto margin centering completed");
}

// Check if an item has auto margins
bool has_auto_margins(ViewBlock* item) {
    if (!item) return false;
    return item->bound && (item->bound->margin.left_type == CSS_VALUE_AUTO || item->bound->margin.right_type == CSS_VALUE_AUTO ||
           item->bound->margin.top_type == CSS_VALUE_AUTO || item->bound->margin.bottom_type == CSS_VALUE_AUTO);
}

// Simplified implementations that use existing functions

// Collect flex items with measured sizes
void collect_flex_items_with_measurements(FlexContainerLayout* flex_layout, ViewBlock* container) {
    log_debug("Collecting flex items with measurements");

    // Use existing implementation for now
    // TODO: Add measurement-based flex item collection in the future
}

// Calculate flex basis using measured content
void calculate_flex_basis_with_measurements(FlexContainerLayout* flex_layout) {
    log_debug("Calculating flex basis with measurements - using existing implementation");
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
        if (item->bound && (item->bound->margin.left_type == CSS_VALUE_AUTO || item->bound->margin.right_type == CSS_VALUE_AUTO ||
            item->bound->margin.top_type == CSS_VALUE_AUTO || item->bound->margin.bottom_type == CSS_VALUE_AUTO)) {
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
            item->bound && item->bound->margin.left_type == CSS_VALUE_AUTO : item->bound && item->bound->margin.top_type == CSS_VALUE_AUTO;
        bool main_end_auto = is_main_axis_horizontal(flex_layout) ?
            item->bound && item->bound->margin.right_type == CSS_VALUE_AUTO : item->bound && item->bound->margin.bottom_type == CSS_VALUE_AUTO;

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

// Enhanced flex item content layout with full HTML nested content support
// Final layout of flex item contents with determined sizes
void layout_flex_item_content(LayoutContext* lycon, ViewBlock* flex_item) {
    if (!flex_item) return;

    log_debug("Enhanced flex item content layout for %p\n", flex_item);

    // Save parent context
    LayoutContext saved_context = *lycon;

    // Set up flex item as a proper containing block
    lycon->parent = (ViewGroup*)flex_item;
    lycon->prev_view = NULL;

    // Calculate content area dimensions accounting for box model
    int content_width = flex_item->width;
    int content_height = flex_item->height;
    int content_x_offset = 0;
    int content_y_offset = 0;

    if (flex_item->bound) {
        // Account for padding and border in content area
        content_width -= (flex_item->bound->padding.left + flex_item->bound->padding.right);
        content_height -= (flex_item->bound->padding.top + flex_item->bound->padding.bottom);
        content_x_offset = flex_item->bound->padding.left;
        content_y_offset = flex_item->bound->padding.top;

        if (flex_item->bound->border) {
            content_width -= (flex_item->bound->border->width.left + flex_item->bound->border->width.right);
            content_height -= (flex_item->bound->border->width.top + flex_item->bound->border->width.bottom);
            content_x_offset += flex_item->bound->border->width.left;
            content_y_offset += flex_item->bound->border->width.top;
        }
    }

    // Set up block formatting context for nested content
    lycon->block.content_width = content_width;
    lycon->block.content_height = content_height;
    lycon->block.advance_y = content_y_offset;
    lycon->block.max_width = 0;

    // Inherit text alignment and other block properties from flex item
    if (flex_item->blk) {
        lycon->block.text_align = flex_item->blk->text_align;
        // lycon->block.line_height = flex_item->blk->line_height;
    }

    // Set up line formatting context for inline content
    line_init(lycon, content_x_offset, content_x_offset + content_width);

    // Layout all nested content using standard flow algorithm
    // This handles: text nodes, nested blocks, inline elements, images, etc.
    log_debug("*** PASS3 TRACE: About to layout children of flex item %p", flex_item);
    DomNode* child = nullptr;
    if (flex_item->node && flex_item->node->is_element()) {
        child = static_cast<DomElement*>(flex_item->node)->first_child;
    }
    if (child) {
        do {
            log_debug("*** PASS3 TRACE: Layout child %s of flex item (parent=%p)", child->name(), lycon->parent);
            // Use standard layout flow - this handles all HTML content types
            // CRITICAL: lycon->parent is set to flex_item, so text Views become children of flex_item
            layout_flow_node(lycon, child);
            log_debug("*** PASS3 TRACE: Completed layout of child %s", child->name());
            child = child->next_sibling;
        } while (child);

        // Finalize any pending line content
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        }
    }

    // Update flex item content dimensions for intrinsic sizing
    flex_item->content_width = lycon->block.max_width;
    flex_item->content_height = lycon->block.advance_y - content_y_offset;

    // Restore parent context
    *lycon = saved_context;

    log_debug("Enhanced flex item content layout complete: %dx%d\n",
              flex_item->content_width, flex_item->content_height);
}

// Final content layout pass
void layout_final_flex_content(LayoutContext* lycon, ViewBlock* flex_container) {
    log_debug("Final flex content layout");
    // Layout content within each flex item with their final sizes
    View* child = flex_container->child;
    while (child) {
        if (child->type == RDT_VIEW_BLOCK) {
            ViewBlock* flex_item = (ViewBlock*)child;
            log_debug("Final layout for flex item %p: %dx%d", flex_item, flex_item->width, flex_item->height);

            // Final layout of flex item contents with determined sizes
            layout_flex_item_content(lycon, flex_item);
        }
        child = child->next;
    }
    log_debug("Final flex content layout completed");
}

// Enhanced multi-pass flex layout
void layout_flex_content(LayoutContext* lycon, ViewBlock* block) {
    log_debug("Starting multi-pass flex layout for container %p", block);
    FlexContainerLayout* pa_flex = lycon->flex_container;
    init_flex_container(lycon, block);

    // PASS 1: Create Views with measured sizes (combined measurement + View creation)
    log_debug("FLEX MULTIPASS: Creating Views with measurements (single pass)");
    int child_count = 0;
    DomNode* measure_child = nullptr;
    if (block->node->is_element()) {
        measure_child = static_cast<DomElement*>(block->node)->first_child;
    }
    if (measure_child) {
        do {
        log_debug(">>> PASS1 TRACE: Processing flex child %p (count: %d)", measure_child, child_count);
        // Only create Views for element nodes, skip text nodes
        if (measure_child->is_element()) {
            log_debug(">>> PASS1 TRACE: Creating View with measurement for %s (node=%p)", measure_child->name(), measure_child);
            // CRITICAL: Keep measurement logic, then create View
            measure_flex_child_content(lycon, measure_child);
            layout_flow_node_for_flex(lycon, measure_child);
            log_debug(">>> PASS1 TRACE: Completed View creation for %s", measure_child->name());
        } else {
            log_debug(">>> PASS1 TRACE: Skipping text node: %s", measure_child->name());
        }
        measure_child = measure_child->next_sibling;
        child_count++;
        } while (measure_child);
    }

    // PASS 2: Run enhanced flex algorithm with nested content support
    log_debug("FLEX MULTIPASS: Running enhanced flex algorithm (final pass)");
    layout_flex_container_with_nested_content(lycon, block);

    // restore parent flex context
    cleanup_flex_container(lycon);
    lycon->flex_container = pa_flex;
}
