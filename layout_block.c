#include "layout.h"

void layout_flex_nodes(LayoutContext* lycon, lxb_dom_node_t *first_child);

void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, PropValue display, Blockbox* pa_block) {
    // finalize the block size
    int flow_width, flow_height;
    if (block->bound) {
        block->content_width = lycon->block.max_width + block->bound->padding.left + block->bound->padding.right;
        // advance_y already includes padding.top and border.top
        block->content_height = lycon->block.advance_y + block->bound->padding.bottom;
        flow_width = block->content_width +
            (block->bound->border ? block->bound->border->width.left + block->bound->border->width.right : 0);
        flow_height = block->content_height +
            (block->bound->border ? block->bound->border->width.bottom : 0);
    } else {
        flow_width = block->content_width = lycon->block.max_width;
        flow_height = block->content_height = lycon->block.advance_y;
    }
    
    if (display == LXB_CSS_VALUE_INLINE_BLOCK && lycon->block.given_width < 0) {
        block->width = min(flow_width, block->width);
    }
    // handle horizontal overflow
    if (flow_width > block->width) { // hz overflow
        if (!block->scroller) {
            block->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
        }
        block->scroller->has_hz_overflow = true;
        if (block->scroller->overflow_x == LXB_CSS_VALUE_VISIBLE) {
            pa_block->max_width = max(pa_block->max_width, flow_width);  
        }
        else if (block->scroller->overflow_x == LXB_CSS_VALUE_SCROLL || 
            block->scroller->overflow_x == LXB_CSS_VALUE_AUTO) {
            block->scroller->has_hz_scroll = true;
        }
        if (block->scroller->has_hz_scroll || 
            block->scroller->overflow_x == LXB_CSS_VALUE_CLIP || 
            block->scroller->overflow_x == LXB_CSS_VALUE_HIDDEN) {
            block->scroller->has_clip = true;
            block->scroller->clip.x = 0;  block->scroller->clip.y = 0;
            block->scroller->clip.width = block->width;  block->scroller->clip.height = block->height;                
        }
    }
    // handle vertical overflow and determine block->height
    if (lycon->block.given_height >= 0) { // got specified height
        // no change to block->height
        if (flow_height > block->height) { // vt overflow
            if (!block->scroller) {
                block->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
            }
            block->scroller->has_vt_overflow = true;
            if (block->scroller->overflow_y == LXB_CSS_VALUE_VISIBLE) {
                pa_block->max_height = max(pa_block->max_height, block->y + flow_height);  
            }
            else if (block->scroller->overflow_y == LXB_CSS_VALUE_SCROLL || block->scroller->overflow_y == LXB_CSS_VALUE_AUTO) {
                block->scroller->has_vt_scroll = true;
            }
            if (block->scroller->has_hz_scroll || 
                block->scroller->overflow_y == LXB_CSS_VALUE_CLIP || 
                block->scroller->overflow_y == LXB_CSS_VALUE_HIDDEN) {
                block->scroller->has_clip = true;
                block->scroller->clip.x = 0;  block->scroller->clip.y = 0;
                block->scroller->clip.width = block->width;  block->scroller->clip.height = block->height;                    
            }                
        }
    }
    else {
        block->height = flow_height;
    }
}

void layout_block(LayoutContext* lycon, lxb_html_element_t *elmt, DisplayValue display) {
    // display: LXB_CSS_VALUE_BLOCK, LXB_CSS_VALUE_INLINE_BLOCK, LXB_CSS_VALUE_LIST_ITEM
    dzlog_debug("<<layout block %s\n", lxb_dom_element_local_name(lxb_dom_interface_element(elmt), NULL));
    if (display.outer != LXB_CSS_VALUE_INLINE_BLOCK) {
        if (!lycon->line.is_line_start) { line_break(lycon); }
    }
    // save parent context
    Blockbox pa_block = lycon->block;  Linebox pa_line = lycon->line;   
    FontBox pa_font = lycon->font;  lycon->font.current_font_size = -1;  // unresolved yet
    lycon->elmt = elmt;
    lycon->block.pa_block = &pa_block;
    lycon->block.width = lycon->block.height = 0;
    lycon->block.given_width = -1;  lycon->block.given_height = -1;

    uintptr_t elmt_name = elmt->element.node.local_name;
    ViewBlock* block = elmt_name == LXB_TAG_IMG ? 
        (ViewBlock*)alloc_view(lycon, RDT_VIEW_IMAGE, (lxb_dom_node_t*)elmt) :
        (ViewBlock*)alloc_view(lycon, 
            display.outer == LXB_CSS_VALUE_INLINE_BLOCK ? RDT_VIEW_INLINE_BLOCK : 
            (display.outer == LXB_CSS_VALUE_LIST_ITEM ? RDT_VIEW_LIST_ITEM : RDT_VIEW_BLOCK), 
            (lxb_dom_node_t*)elmt);
    // handle element default styles
    float em_size = 0;  size_t value_len;  const lxb_char_t *value;
    
    switch (elmt_name) {
    case LXB_TAG_BODY:
        block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        // default: 8px margin for body
        block->bound->margin.top = block->bound->margin.bottom = 
            block->bound->margin.left = block->bound->margin.right = 8 * lycon->ui_context->pixel_ratio; 
         break;
    case LXB_TAG_H1:
        em_size = 2;  // 2em
        goto HEADING_PROP;
    case LXB_TAG_H2:
        em_size = 1.5;  // 1.5em
        goto HEADING_PROP;
    case LXB_TAG_H3:
        em_size = 1.17;  // 1.17em
        goto HEADING_PROP;
    case LXB_TAG_H4:    
        em_size = 1;  // 1em
        goto HEADING_PROP;
    case LXB_TAG_H5:
        em_size = 0.83;  // 0.83em 
        goto HEADING_PROP;
    case LXB_TAG_H6:
        em_size = 0.67;  // 0.67em
        HEADING_PROP:
        block->font = alloc_font_prop(lycon);
        block->font->font_size = lycon->font.style.font_size * em_size;
        block->font->font_weight = LXB_CSS_VALUE_BOLD;
        break;
    case LXB_TAG_P:
        block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style.font_size;
        break;
    case LXB_TAG_UL:  case LXB_TAG_OL: 
        if (!block->props) {
            block->props = (BlockProp*)alloc_prop(lycon, sizeof(BlockProp));
        }
        block->props->list_style_type = elmt_name == LXB_TAG_UL ?
            LXB_CSS_VALUE_DISC : LXB_CSS_VALUE_DECIMAL;
        if (!block->bound) {
            block->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        // margin: 1em 0; padding: 0 0 0 40px;
        block->bound->margin.top = block->bound->margin.bottom = lycon->font.style.font_size;
        block->bound->padding.left = 40 * lycon->ui_context->pixel_ratio;
        break;
    case LXB_TAG_CENTER:
        block->props = (BlockProp*)alloc_prop(lycon, sizeof(BlockProp));
        block->props->text_align = LXB_CSS_VALUE_CENTER;
        break;    
    case LXB_TAG_IMG:  // get html width and height (before the css styles)
        value = lxb_dom_element_get_attribute((lxb_dom_element_t *)elmt, (lxb_char_t*)"width", 5, &value_len);
        if (value) {
            int width = strview_to_int(&strview_init(value, value_len));
            if (width >= 0) lycon->block.given_width = width * lycon->ui_context->pixel_ratio;
            // else width attr ignored
        }
        value = lxb_dom_element_get_attribute((lxb_dom_element_t *)elmt, (lxb_char_t*)"height", 6, &value_len);
        if (value) {
            int height = strview_to_int(&strview_init(value, value_len));
            if (height >= 0) lycon->block.given_height = height * lycon->ui_context->pixel_ratio;
            // else height attr ignored
        }
        break;        
    }
    lycon->block.line_height = lycon->font.style.font_size * 1.2;  // default line height

    // resolve CSS styles
    if (elmt->element.style) {
        // lxb_dom_document_t *doc = lxb_dom_element_document((lxb_dom_element_t*)elmt);
        lexbor_avl_foreach_recursion(NULL, elmt->element.style, resolve_element_style, lycon);
        dzlog_debug("resolved element style: %p\n", elmt->element.style);
    }
    
    // switch block to list
    if (block->props && block->props->list_style_type && elmt_name != LXB_TAG_IMG) {
        block->type = RDT_VIEW_LIST;
    }
 
    lycon->block.advance_y = 0;  lycon->block.max_width = 0;
    if (block->props) lycon->block.text_align = block->props->text_align;
    lycon->line.left = 0;
    line_start(lycon);
    block->x = pa_line.left;  block->y = pa_block.advance_y;

    if (elmt_name == LXB_TAG_IMG) { // load image intrinsic width and height
        value = lxb_dom_element_get_attribute((lxb_dom_element_t *)elmt, (lxb_char_t*)"src", 3, &value_len);
        ViewImage* image = (ViewImage*)block;
        if (value && value_len) {
            StrBuf* src = strbuf_new_cap(value_len);
            strbuf_append_str_n(src, (const char*)value, value_len);
            printf("image src: %s\n", src->str);
            image->img = load_image(lycon->ui_context, src->str);
            strbuf_free(src);
            if (!image->img) {
                printf("Failed to load image\n");
                // todo: use a placeholder
            }
        }
        if (image->img) {
            if (lycon->block.given_width < 0 || lycon->block.given_height < 0) {
                // scale image by pixel ratio
                int w = image->img->width * lycon->ui_context->pixel_ratio;
                int h = image->img->height * lycon->ui_context->pixel_ratio;               
                printf("image dims: intrinsic - %d x %d, spec - %d x %d\n", w, h, 
                    lycon->block.given_width, lycon->block.given_height);
                if (lycon->block.given_width >= 0) { // scale unspecified height
                    lycon->block.given_height = lycon->block.given_width * h / w;
                }
                if (lycon->block.given_height >= 0) { // scale unspecified width
                    lycon->block.given_width = lycon->block.given_height * w / h;
                } 
                else { // both width and height unspecified
                    if (image->img->format == IMAGE_FORMAT_SVG) {
                        // scale to parent block width
                        lycon->block.given_width = lycon->block.pa_block->width;
                        lycon->block.given_height = lycon->block.given_width * h / w;
                    }
                    else { // use image intrinsic dimensions
                        lycon->block.given_width = w;  lycon->block.given_height = h;
                    }
                }
            }
            // else both width and height specified

            if (image->img->format == IMAGE_FORMAT_SVG) {
                image->img->max_render_width = max(lycon->block.given_width, image->img->max_render_width);
            }
            printf("image dimensions: %d x %d\n", lycon->block.given_width, lycon->block.given_height);         
        }
    }
    
    dzlog_debug("setting up block props\n");
    if (block->font) {
        setup_font(lycon->ui_context, &lycon->font, pa_font.face->family_name, block->font);
    }
    lycon->block.init_ascender = lycon->font.face->size->metrics.ascender >> 6;  
    lycon->block.init_descender = lycon->font.face->size->metrics.descender >> 6;

    if (block->bound) {
        if (lycon->block.given_width >= 0) { // got specified width 
            block->width = lycon->block.given_width + block->bound->padding.left + block->bound->padding.right +
                (block->bound->border ? block->bound->border->width.left + block->bound->border->width.right : 0);
            lycon->block.width = lycon->block.given_width;
            if (block->bound->margin.left == LENGTH_AUTO && block->bound->margin.right == LENGTH_AUTO)  {
                block->bound->margin.left = block->bound->margin.right = (pa_block.width - block->width) / 2;
            }
            else {
                if (block->bound->margin.left == LENGTH_AUTO) block->bound->margin.left = 0;
                if (block->bound->margin.right == LENGTH_AUTO) block->bound->margin.right = 0;
            }
        } else {
            dzlog_debug("no given width: %d, %d, %d\n", pa_block.width, block->bound->margin.left, block->bound->margin.right);
            if (block->bound->margin.left == LENGTH_AUTO) block->bound->margin.left = 0;
            if (block->bound->margin.right == LENGTH_AUTO) block->bound->margin.right = 0;
            block->width = pa_block.width - (block->bound->margin.left + block->bound->margin.right);
            lycon->block.width = block->width - (block->bound->padding.left + block->bound->padding.right);
        }
        dzlog_debug("setting up height\n");
        if (lycon->block.given_height >= 0) { // got specified height 
            block->height = lycon->block.given_height + block->bound->padding.top + block->bound->padding.bottom +
                (block->bound->border ? block->bound->border->width.top + block->bound->border->width.bottom : 0);
            lycon->block.height = lycon->block.given_height;
        } else {
            block->height = block->bound->margin.top + block->bound->margin.bottom;
            lycon->block.height = pa_block.height - block->height - (block->bound->padding.top + block->bound->padding.bottom)
                - (block->bound->border ? block->bound->border->width.top + block->bound->border->width.bottom : 0);
        }
        dzlog_debug("setting up x, y\n");
        block->x += block->bound->margin.left;
        block->y += block->bound->margin.top;
        if (block->bound->border) {
            lycon->line.advance_x += block->bound->border->width.left;
            lycon->block.advance_y += block->bound->border->width.top;
        }        
        lycon->line.advance_x += block->bound->padding.left;
        lycon->block.advance_y += block->bound->padding.top;
        lycon->line.left = lycon->line.advance_x;
    } 
    else {
        dzlog_debug("setting up bounds\n");
        if (lycon->block.given_width >= 0) { // got specified width 
            block->width = lycon->block.width = lycon->block.given_width;
        } else {
            block->width = lycon->block.width = pa_block.width;
        }
        if (lycon->block.given_height >= 0) { // got specified height 
            block->height = lycon->block.height = lycon->block.given_height;
        } else {
            block->height = lycon->block.height = pa_block.height;
        }
    }
    lycon->line.right = lycon->block.width;  
    dzlog_debug("block width: %d, height: %d\n", block->width, block->height);
    assert(lycon->block.width > 0 && lycon->block.height > 0);

    // layout block content
    if (elmt_name != LXB_TAG_IMG) {
        dzlog_debug("layout block content\n");
        lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(elmt));
        if (child) {
            lycon->parent = (ViewGroup*)block;  lycon->prev_view = NULL;
            if (display.inner == LXB_CSS_VALUE_FLOW) {
                do {
                    layout_flow_node(lycon, child);
                    child = lxb_dom_node_next(child);
                } while (child);
                // handle last line
                if (!lycon->line.is_line_start) { line_break(lycon); }                
            }
            else if (display.inner == LXB_CSS_VALUE_FLEX) {
                layout_flex_nodes(lycon, child);
            }
            else {
                dzlog_debug("unknown display type\n");
            }
            lycon->parent = block->parent;
        }
        finalize_block_flow(lycon, block, display.outer, &pa_block);
    }

    // flow the block in parent context
    dzlog_debug("flow block in parent context\n");
    lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;
    if (display.outer == LXB_CSS_VALUE_INLINE_BLOCK) {
        if (lycon->line.advance_x + block->width >= lycon->line.right) { 
            line_break(lycon);
            block->x = lycon->line.left;
        } else {
            block->x = lycon->line.advance_x;  
        }
        block->y = lycon->block.advance_y;
        lycon->line.advance_x += block->width;
        if (block->bound) { 
            block->x += block->bound->margin.left;
            block->y += block->bound->margin.top;
            lycon->line.advance_x += block->bound->margin.left + block->bound->margin.right;
        }
        // update baseline
        int block_flow_height = block->height + (block->bound ? block->bound->margin.top + block->bound->margin.bottom : 0);
        if (block->in_line && block->in_line->vertical_align != LXB_CSS_VALUE_BASELINE) {
            lycon->line.max_descender = max(lycon->line.max_descender, block_flow_height - lycon->line.max_ascender);
        } else {
            // default baseline alignment for inline block
            lycon->line.max_ascender = max(lycon->line.max_ascender, block_flow_height);
        }
        
        lycon->line.is_line_start = false;
    } else {
        if (block->bound) {
            lycon->block.advance_y += block->height + block->bound->margin.top + block->bound->margin.bottom;
            lycon->block.max_width = max(lycon->block.max_width, block->width 
                + block->bound->margin.left + block->bound->margin.right);
        } else {
            lycon->block.advance_y += block->height;
            lycon->block.max_width = max(lycon->block.max_width, block->width);        
        }
        // line_start(lycon);
        assert(lycon->line.is_line_start);
    }
    lycon->prev_view = (View*)block;
    dzlog_debug("block view: %d, end block>>\n", block->type);
}