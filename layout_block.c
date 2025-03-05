#include "layout.h"

#define STB_IMAGE_IMPLEMENTATION
#include "lib/stb_image.h"

int strToInt(const char* str, int len) {
    int result = 0, sign = 1;
    if (len <= 0 || str[0] == '\0') {
        return 0;
    }
    // handle sign
    if (*str == '-') {
        sign = -1;  str++;
    } else if (*str == '+') {
        str++;
    }
    // process digits
    const char* end = str + len;
    while (str < end) {
        if (*str >= '0' && *str <= '9') {
            result = result * 10 + (*str - '0');
        } else {
            break;
        }
        str++;
    }
    return sign * result;
}

SDL_Surface *loadImage(const char *filePath) {
    int width, height, channels;
    unsigned char *data = stbi_load(filePath, &width, &height, &channels, 4);
    if (!data) {
        printf("Failed to load image: %s\n", filePath);
        return NULL;
    }

    SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(
        data, width, height, 32, width * 4,
        0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
    );
    if (!surface) {
        stbi_image_free(data); 
    }
    return surface;
}

// get image dimensions using SDL_image
bool get_image_dimensions(LayoutContext* lycon, const char *filename, SDL_Rect *dims) {
    bool result = false;
    if (!filename || !dims) { return result; }

    // Load the image
    printf("Image load: %s\n", filename);
    SDL_Surface *image = loadImage(filename);
    if (!image) {
        printf("IMG_Load Error: %s\n", IMG_GetError());
        return result;
    }
    // Get dimensions
    dims->w = image->w;  dims->h = image->h;
    printf("Image size: %d x %d\n", dims->w, dims->h);
    result = true;

    // Cleanup
    stbi_image_free(image->pixels);
    SDL_FreeSurface(image);
    return result;
}

void layout_block(LayoutContext* lycon, lxb_html_element_t *elmt, PropValue display) {
    // display: LXB_CSS_VALUE_BLOCK, LXB_CSS_VALUE_INLINE_BLOCK, LXB_CSS_VALUE_LIST_ITEM
    printf("layout block %s\n", lxb_dom_element_local_name(lxb_dom_interface_element(elmt), NULL));
    if (display != LXB_CSS_VALUE_INLINE_BLOCK) {
        if (!lycon->line.is_line_start) { line_break(lycon); }
    }
    // save parent context
    Blockbox pa_block = lycon->block;  Linebox pa_line = lycon->line;   FontBox pa_font = lycon->font;
    lycon->block.width = -1;  lycon->block.height = -1;

    ViewBlock* block = (ViewBlock*)alloc_view(lycon, RDT_VIEW_BLOCK, (lxb_dom_node_t*)elmt);
    // handle element default styles
    float em_size = 0;  size_t value_len;  const lxb_char_t *value;
    uintptr_t elmt_name = elmt->element.node.local_name;
    switch (elmt_name) {
    case LXB_TAG_CENTER:
        block->props = (BlockProp*)alloc_prop(lycon, sizeof(BlockProp));
        block->props->text_align = LXB_CSS_VALUE_CENTER;
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
    case LXB_TAG_IMG:  // get html width and height (before the css styles)
        value = lxb_dom_element_get_attribute((lxb_dom_element_t *)elmt, (lxb_char_t*)"width", 5, &value_len);
        if (value) {
            int width = strToInt((const char*)value, value_len);
            if (width > 0) lycon->block.width = width * lycon->ui_context->pixel_ratio;
            // else width attr ignored
        }
        value = lxb_dom_element_get_attribute((lxb_dom_element_t *)elmt, (lxb_char_t*)"height", 6, &value_len);
        if (value) {
            int height = strToInt((const char*)value, value_len);
            if (height > 0) lycon->block.height = height * lycon->ui_context->pixel_ratio;
            // else height attr ignored
        }
        break;        
    }
    lycon->block.line_height = lycon->font.style.font_size * 1.2;  // default line height

    // resolve CSS styles
    if (elmt->element.style) {
        // lxb_dom_document_t *doc = lxb_dom_element_document((lxb_dom_element_t*)elmt);
        lexbor_avl_foreach_recursion(NULL, elmt->element.style, resolve_element_style, lycon);
        printf("### got element style: %p\n", elmt->element.style);
    }
    
    // switch block to list
    if (block->props && block->props->list_style_type && elmt_name != LXB_TAG_IMG) {
        block->type = RDT_VIEW_LIST;
    }
 
    lycon->block.advance_y = 0;  lycon->block.max_width = 0;
    if (block->props) lycon->block.text_align = block->props->text_align;
    lycon->line.left = lycon->line.advance_x = lycon->line.max_ascender = lycon->line.max_descender = 0;  
    lycon->line.is_line_start = true;  lycon->line.has_space = false;
    lycon->line.last_space = NULL;  lycon->line.start_view = NULL;
    block->x = pa_line.left;  block->y = pa_block.advance_y;

    if (elmt_name == LXB_TAG_IMG) { // load image intrinsic width and height
        if (lycon->block.width < 0 || lycon->block.height < 0) {
            printf("loading image dimensions\n");
            value = lxb_dom_element_get_attribute((lxb_dom_element_t *)elmt, (lxb_char_t*)"src", 3, &value_len);
            if (value && value_len) {
                StrBuf* src = strbuf_new_cap(value_len);
                strbuf_append_str_n(src, (const char*)value, value_len);
                printf("image src: %s\n", src->s);
                SDL_Rect dims;
                if (get_image_dimensions(lycon, src->s, &dims)) {
                    printf("image dims: %d x %d, %f x %f\n", dims.w, dims.h, lycon->block.width, lycon->block.height);
                    if (lycon->block.width >= 0) {
                        lycon->block.height = lycon->block.width * dims.h / dims.w;
                    }
                    if (lycon->block.height >= 0) {
                        lycon->block.width = lycon->block.height * dims.w / dims.h;
                    }
                    else {
                        lycon->block.height = dims.h;  lycon->block.width = dims.w;
                    }
                }
                strbuf_free(src);
                printf("image dimensions: %f x %f\n", lycon->block.width, lycon->block.height);
            }
        }
    }
    
    if (block->font) {
        setup_font(lycon->ui_context, &lycon->font, pa_font.face->family_name, block->font);
    }
    if (block->bound) {
        if (lycon->block.width >= 0) { // got specified width 
            block->width = lycon->block.width + block->bound->padding.left + block->bound->padding.right +
                (block->bound->border ? block->bound->border->width.left + block->bound->border->width.right : 0);
        } else {
            block->width = pa_block.width - (block->bound->margin.left + block->bound->margin.right);
            lycon->block.width = block->width - (block->bound->padding.left + block->bound->padding.right);
        }
        if (lycon->block.height >= 0) { // got specified height 
            block->height = lycon->block.height + block->bound->padding.top + block->bound->padding.bottom +
                (block->bound->border ? block->bound->border->width.top + block->bound->border->width.bottom : 0);
        } else {
            block->height = block->bound->margin.top + block->bound->margin.bottom;
            lycon->block.height = pa_block.height - block->height - (block->bound->padding.top + block->bound->padding.bottom)
                - (block->bound->border ? block->bound->border->width.top + block->bound->border->width.bottom : 0);
        }
        block->x += block->bound->margin.left;
        block->y += block->bound->margin.top;
        lycon->line.advance_x += block->bound->padding.left;
        lycon->block.advance_y += block->bound->padding.top;
        if (block->bound->border) {
            lycon->line.advance_x += block->bound->border->width.left;
            lycon->block.advance_y += block->bound->border->width.top;
        }
        lycon->line.left = lycon->line.advance_x;
    } 
    else {
        if (lycon->block.width >= 0) { // got specified width 
            block->width = lycon->block.width;
        } else {
            block->width = lycon->block.width = pa_block.width;
        }
        if (lycon->block.height >= 0) { // got specified height 
            block->height = lycon->block.height;
        } else {
            block->height = lycon->block.height = pa_block.height;
        }
    }
    lycon->line.right = lycon->block.width;  

    if (elmt_name != LXB_TAG_IMG) {
        // layout block content
        lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(elmt));
        if (child) {
            lycon->parent = (ViewGroup*)block;  lycon->prev_view = NULL;
            do {
                layout_node(lycon, child);
                child = lxb_dom_node_next(child);
            } while (child);
            // handle last line
            if (lycon->line.max_ascender) {
                lycon->block.advance_y += max(lycon->line.max_ascender + lycon->line.max_descender, lycon->block.line_height);
            }
            lycon->parent = block->parent;
            printf("block height: %f\n", lycon->block.advance_y);
        }
        line_align(lycon);

        // finalize the block size
        if (block->bound) {
            block->width = max(block->width, lycon->block.max_width 
                + block->bound->padding.left + block->bound->padding.right +
                (block->bound->border ? block->bound->border->width.left + block->bound->border->width.right : 0));              
            block->height = lycon->block.advance_y + block->bound->padding.bottom 
                + (block->bound->border ? block->bound->border->width.bottom : 0);
        } 
        else {
            block->width = max(block->width, lycon->block.max_width);  
            block->height = lycon->block.advance_y;
        }
    }

    // flow the block in parent context
    lycon->block = pa_block;  lycon->font = pa_font;  lycon->line = pa_line;
    if (display == LXB_CSS_VALUE_INLINE_BLOCK) {
        if (lycon->line.advance_x + block->width >= lycon->line.right) { 
            line_break(lycon);
            block->x = lycon->line.left;
        } else {
            block->x = lycon->line.advance_x;  
        }
        block->y = lycon->block.advance_y;
        lycon->line.advance_x += block->width;  
        lycon->line.max_ascender = max(lycon->line.max_ascender, block->height);  // inline block aligned at baseline
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
    printf("block view: %d, self %p, child %p\n", block->type, block, block->child);
}