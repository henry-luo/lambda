#include "layout.hpp"

#include "../lib/log.h"
void layout_block_content(LayoutContext* lycon, ViewBlock* block, DisplayValue display);

void reflow_flex_item(LayoutContext* lycon, ViewBlock* block) {
    // display: LXB_CSS_VALUE_BLOCK, LXB_CSS_VALUE_INLINE_BLOCK, LXB_CSS_VALUE_LIST_ITEM
    // save parent context
    Blockbox pa_block = lycon->block;  Linebox pa_line = lycon->line;   
    FontBox pa_font = lycon->font;  lycon->font.current_font_size = -1;  // -1 as unresolved
    lycon->block.pa_block = &pa_block;  // lycon->elmt = elmt;
    lycon->block.width = lycon->block.height = 0;
    lycon->block.given_width = -1;  lycon->block.given_height = -1;
    // lycon->block.line_height // inherit
    
    // no need to resolve the styles again
    lycon->block.line_height = lycon->font.style.font_size * 1.2;  // default line height

    lycon->block.advance_y = 0;  lycon->block.max_width = 0;
    if (block->blk) lycon->block.text_align = block->blk->text_align;
    lycon->line.left = 0;  lycon->line.right = pa_block.width;
    lycon->line.vertical_align = LXB_CSS_VALUE_BASELINE;
    line_init(lycon);
    
    log_debug("setting up block blk\n");
    if (block->font) {
        setup_font(lycon->ui_context, &lycon->font, pa_font.face->family_name, block->font);
    }
    lycon->block.init_ascender = lycon->font.face->size->metrics.ascender >> 6;  
    lycon->block.init_descender = (-lycon->font.face->size->metrics.descender) >> 6;

    if (block->bound) {
        lycon->block.given_width = block->width - (block->bound->padding.left + block->bound->padding.right);
        lycon->block.given_height = block->height - (block->bound->padding.top + block->bound->padding.bottom);
        lycon->block.width = lycon->block.given_width;
        lycon->block.height = lycon->block.given_height;
        if (block->bound->margin.left == LENGTH_AUTO && block->bound->margin.right == LENGTH_AUTO)  {
            block->bound->margin.left = block->bound->margin.right = (pa_block.width - block->width) / 2;
        }
        else {
            if (block->bound->margin.left == LENGTH_AUTO) block->bound->margin.left = 0;
            if (block->bound->margin.right == LENGTH_AUTO) block->bound->margin.right = 0;
        }
        if (block->bound->border) {
            lycon->line.advance_x += block->bound->border->width.left;
            lycon->block.advance_y += block->bound->border->width.top;
        }        
        lycon->line.advance_x += block->bound->padding.left;
        lycon->block.advance_y += block->bound->padding.top;
        lycon->line.left = lycon->line.advance_x;
    } 
    else {
        lycon->block.width = lycon->block.given_width = block->width;
        lycon->block.height = lycon->block.given_height = block->height;
    }
    lycon->line.right = lycon->block.width;  
    printf("block-sizes: width:%d, height:%d, line-hg:%d, wd:%d, hg:%d\n",
        block->width, block->height, lycon->block.line_height, lycon->block.width, lycon->block.height);
    if (lycon->block.width <0) { lycon->block.width = 0; }
    if (lycon->block.height < 0) { lycon->block.height = 0; }

    // free old block content
    if (block->child) {
        View* view = block->child;
        do {
            View* next = view->next;
            free_view(lycon->doc->view_tree, view);
            view = next;
        } while (view);
        block->child = NULL;
    }
    // layout block content
    if (block->display.inner != RDT_DISPLAY_REPLACED) {
        layout_block_content(lycon, block, block->display);
    }

    // flow the block in parent context
    log_debug("flow block in parent context\n");
    lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;
    lycon->block.max_width = max(lycon->block.max_width, block->width 
        + (block->bound ? block->bound->margin.left + block->bound->margin.right : 0));
    lycon->prev_view = (View*)block;
    log_debug("block view: %d, end block>>\n", block->type);
}

void layout_flex_nodes(LayoutContext* lycon, lxb_dom_node_t *first_child) {
    log_debug("layout flex nodes");
    ViewBlock* block = (ViewBlock*)lycon->view;
    alloc_flex_container_prop(lycon, block); 
    
    // Count children first
    int child_count = 0;
    DomNode *child = (DomNode*)first_child;
    while (child) {
        child_count++;
        child = child->next_sibling();
    }
    
    if (child_count == 0) return;
    
    // Create a FlexContainer
    FlexContainer flex_container;
    memset(&flex_container, 0, sizeof(FlexContainer));
    
    // Set container properties
    flex_container.width = block->width - 
        (block->bound ? block->bound->padding.left + block->bound->padding.right : 0);
    flex_container.height = lycon->block.given_height >= 0 ? lycon->block.given_height :
        (block->bound ? block->bound->padding.top + block->bound->padding.bottom : 0);
    flex_container.direction = (FlexDirection)block->embed->flex_container->direction;
    flex_container.wrap = (FlexWrap)block->embed->flex_container->wrap;
    flex_container.justify = (JustifyContent)block->embed->flex_container->justify;
    flex_container.align_items = (AlignType)block->embed->flex_container->align_items;
    flex_container.align_content = (AlignType)block->embed->flex_container->align_content;
    flex_container.row_gap = block->embed->flex_container->row_gap;
    
    // Allocate items array
    flex_container.items = (FlexItem*)calloc(child_count, sizeof(FlexItem));
    flex_container.item_count = child_count;
    
    // First phase: layout each child as inline-block to determine its natural size
    child = (DomNode*)first_child;
    int index = 0;
    
    // Create temporary ViewBlock items for measuring children
    ViewBlock** child_blocks = (ViewBlock**)calloc(child_count, sizeof(ViewBlock*));
    
    DisplayValue display = {LXB_CSS_VALUE_INLINE_BLOCK, LXB_CSS_VALUE_FLOW};
    while (child && index < child_count) {
        // Layout child in measuring mode to determine its size 
        if (child->is_element()) {
            // reset avance_x and advance_y for each child
            lycon->line.advance_x = 0;  lycon->block.advance_y = 0;
            layout_block(lycon, child, display);

            if (lycon->prev_view && lycon->prev_view->type >= RDT_VIEW_INLINE_BLOCK) {
                ViewBlock* child_block = (ViewBlock*)lycon->prev_view;
                child_blocks[index] = child_block;
                log_debug("flex child %d: x=%d, y=%d, w=%d, h=%d", 
                    index, child_block->x, child_block->y, child_block->width, child_block->height);

                // Set up the FlexItem
                FlexItem* item = &flex_container.items[index];
                item->width = child_block->width;  item->height = child_block->height;
                log_debug("flex item %d: width=%d, height=%d\n", index, item->width, item->height);
                
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
                
                // Copy flex item properties from embedded ViewSpan properties
                item->flex_basis = child_block->flex_basis;
                item->flex_grow = child_block->flex_grow;
                item->flex_shrink = child_block->flex_shrink;
                item->align_self = (AlignType)child_block->align_self;
                item->order = child_block->order;
                item->is_flex_basis_percent = child_block->flex_basis_is_percent;
                index++;
            }
        }
        child = child->next_sibling();
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

            log_debug("flex child adjusted block %d: x=%d, y=%d, w=%d, h=%d", 
                i, child_blocks[i]->x, child_blocks[i]->y, child_blocks[i]->width, child_blocks[i]->height);
        }
    }
    
    // update the block's content size
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
    if (lycon->block.given_height < 0) {
        block->height = block->content_height
            + (block->bound ? block->bound->padding.top + block->bound->padding.bottom : 0) 
            + (block->bound && block->bound->border ? block->bound->border->width.top + block->bound->border->width.bottom : 0);
    }
    lycon->block.max_width = max_width;  // includes padding-left
    lycon->block.advance_y = max_height;  // includes padding-top  
    log_debug("flex block final: content-wd=%d, content-hg=%d, wd:%d, hg:%d\n", 
        block->content_width, block->content_height, block->width, block->height); 
    
    // reflow the block
    for (int i = 0; i < child_count && i < index; i++) {
        if (child_blocks[i]) {
            reflow_flex_item(lycon, child_blocks[i]);
        }
    }

    // clean up
    free(child_blocks);
    free(flex_container.items);
    
    log_debug("Flex layout complete");
}
