#include "layout.h"

void layout_flex_nodes(LayoutContext* lycon, lxb_dom_node_t *first_child) {
    dzlog_debug("layout flex nodes");
    ViewBlock* block = (ViewBlock*)lycon->view;
    if (!block->flex_container) { block->flex_container = alloc_flex_container_prop(lycon); }
    
    // Count children first
    int child_count = 0;
    lxb_dom_node_t *child = first_child;
    while (child) {
        child_count++;
        child = lxb_dom_node_next(child);
    }
    
    if (child_count == 0) return;
    
    // Create a FlexContainer
    FlexContainer flex_container;
    memset(&flex_container, 0, sizeof(FlexContainer));
    
    // Set container properties
    flex_container.width = block->width - 
        (block->bound ? block->bound->padding.left + block->bound->padding.right : 0);
    flex_container.height = block->height - 
        (block->bound ? block->bound->padding.top + block->bound->padding.bottom : 0);
    flex_container.direction = block->flex_container->direction;
    flex_container.wrap = block->flex_container->wrap;
    flex_container.justify = block->flex_container->justify;
    flex_container.align_items = block->flex_container->align_items;
    flex_container.align_content = block->flex_container->align_content;
    flex_container.row_gap = block->flex_container->row_gap;
    flex_container.column_gap = block->flex_container->column_gap;
    
    // Allocate items array
    flex_container.items = calloc(child_count, sizeof(FlexItem));
    flex_container.item_count = child_count;
    
    // First phase: layout each child as inline-block to determine its natural size
    child = first_child;
    int index = 0;
    
    // Save parent context
    Blockbox pa_block = lycon->block;
    Linebox pa_line = lycon->line;
    FontBox pa_font = lycon->font;
    ViewGroup* pa_parent = lycon->parent;
    View* pa_prev_view = lycon->prev_view;
    
    // Create temporary ViewBlock items for measuring children
    ViewBlock** child_blocks = calloc(child_count, sizeof(ViewBlock*));
            
    lycon->parent = (ViewGroup*)block;  lycon->prev_view = NULL;
    while (child && index < child_count) {
        DisplayValue display = {.outer = LXB_CSS_VALUE_INLINE_BLOCK, .inner = LXB_CSS_VALUE_FLOW};
        
        // Layout child to determine its size
        lycon->block = pa_block;
        lycon->line = pa_line;
        lycon->font = pa_font;

        // Layout the child in measuring mode
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            layout_block(lycon, (lxb_html_element_t*)child, display);
            if (lycon->prev_view && lycon->prev_view->type >= RDT_VIEW_INLINE_BLOCK) {
                ViewBlock* child_block = (ViewBlock*)lycon->prev_view;
                child_blocks[index] = child_block;
                
                // Set up the FlexItem
                FlexItem* item = &flex_container.items[index];
                item->width = child_block->width;
                item->height = child_block->height;
                dzlog_debug("Flex item %d: width=%d, height=%d\n", index, item->width, item->height);
                
                // Copy margins
                if (child_block->bound) {
                    item->margin[0] = child_block->bound->margin.top;
                    item->margin[1] = child_block->bound->margin.right;
                    item->margin[2] = child_block->bound->margin.bottom;
                    item->margin[3] = child_block->bound->margin.left;
                    
                    // Set auto margin flags
                    item->is_margin_top_auto = (child_block->bound->margin.top == LENGTH_AUTO) ? 1 : 0;
                    item->is_margin_right_auto = (child_block->bound->margin.right == LENGTH_AUTO) ? 1 : 0;
                    item->is_margin_bottom_auto = (child_block->bound->margin.bottom == LENGTH_AUTO) ? 1 : 0;
                    item->is_margin_left_auto = (child_block->bound->margin.left == LENGTH_AUTO) ? 1 : 0;
                }
                
                // Copy flex item properties if available
                if (child_block->flex_item) {
                    item->flex_basis = child_block->flex_item->flex_basis;
                    item->flex_grow = child_block->flex_item->flex_grow;
                    item->flex_shrink = child_block->flex_item->flex_shrink;
                    item->align_self = child_block->flex_item->align_self;
                    item->order = child_block->flex_item->order;
                    item->aspect_ratio = child_block->flex_item->aspect_ratio;
                    item->is_flex_basis_percent = child_block->flex_item->is_flex_basis_percent;
                    item->baseline_offset = child_block->flex_item->baseline_offset;
                } else {
                    // Default values
                    item->flex_basis = -1;  // auto
                    item->flex_grow = 0;
                    item->flex_shrink = 1;
                    item->align_self = ALIGN_START; // will be replaced with container's align-items
                    item->order = 0;
                }
                
                index++;
            }
        }
        child = lxb_dom_node_next(child);
    }
    
    // Run flex layout algorithm
    layout_flex_container(&flex_container);
    
    // Apply flex layout results back to the view blocks
    for (int i = 0; i < child_count && i < index; i++) {
        if (child_blocks[i]) {
            // Apply position and size from flex layout
            child_blocks[i]->x = flex_container.items[i].pos.x + 
                (block->bound ? block->bound->padding.left : 0);
            child_blocks[i]->y = flex_container.items[i].pos.y + 
                (block->bound ? block->bound->padding.top : 0);
            child_blocks[i]->width = flex_container.items[i].width;
            child_blocks[i]->height = flex_container.items[i].height;
            
            // Update content dimensions
            if (child_blocks[i]->bound) {
                child_blocks[i]->content_width = flex_container.items[i].width - 
                    (child_blocks[i]->bound->padding.left + child_blocks[i]->bound->padding.right);
                child_blocks[i]->content_height = flex_container.items[i].height - 
                    (child_blocks[i]->bound->padding.top + child_blocks[i]->bound->padding.bottom);
            } else {
                child_blocks[i]->content_width = flex_container.items[i].width;
                child_blocks[i]->content_height = flex_container.items[i].height;
            }

            dzlog_debug("Flex child block %d: x=%d, y=%d, w=%d, h=%d\n", 
                        i, child_blocks[i]->x, child_blocks[i]->y, child_blocks[i]->width, child_blocks[i]->height);
        }
    }
    
    // Restore parent context
    lycon->block = pa_block;
    lycon->line = pa_line;
    lycon->font = pa_font;
    lycon->parent = pa_parent;
    lycon->prev_view = pa_prev_view;
    
    // Update the block's content size
    int max_width = 0, max_height = 0;
    for (int i = 0; i < index; i++) {
        if (child_blocks[i]) {
            int right_edge = child_blocks[i]->x - block->x + child_blocks[i]->width;
            int bottom_edge = child_blocks[i]->y - block->y + child_blocks[i]->height;
            if (right_edge > max_width) max_width = right_edge;
            if (bottom_edge > max_height) max_height = bottom_edge;
        }
    }
    
    block->content_width = max_width + (block->bound ? block->bound->padding.right : 0);
    block->content_height = max_height + (block->bound ? block->bound->padding.bottom : 0);
    
    // Clean up
    free(child_blocks);
    free(flex_container.items);
    
    dzlog_debug("Flex layout complete");
}
