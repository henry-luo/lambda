#include "render.h"
// #define STB_IMAGE_WRITE_IMPLEMENTATION
// #include "lib/stb_image_write.h"

#define DEBUG_RENDER 1

void render_block_view(RenderContext* rdcon, ViewBlock* view_block);
void render_inline_view(RenderContext* rdcon, ViewSpan* view_span);
void render_children(RenderContext* rdcon, View* view);
void scrollpane_render(Tvg_Canvas* canvas, ScrollPane* sp, Rect* block_bound, int content_width, int content_height);

// draw a glyph bitmap into the doc surface
void draw_glyph(RenderContext* rdcon, FT_Bitmap *bitmap, int x, int y) {
    int left = max(rdcon->block.clip.x, x);
    int right = min(rdcon->block.clip.x + rdcon->block.clip.width, x + (int)bitmap->width);
    int top = max(rdcon->block.clip.y, y);
    int bottom = min(rdcon->block.clip.y + rdcon->block.clip.height, y + (int)bitmap->rows);
    if (left >= right || top >= bottom) return; // glyph outside the surface
    ImageSurface* surface = rdcon->ui_context->surface;
    for (int i = top - y; i < bottom - y; i++) {
        uint8_t* row_pixels = (uint8_t*)surface->pixels + (y + i) * surface->pitch;
        for (int j = left - x; j < right - x; j++) {
            if (x + j < 0 || x + j >= surface->width) continue;
            uint32_t intensity = bitmap->buffer[i * bitmap->pitch + j];
            if (intensity > 0) {
                // blend the pixel with the background
                uint8_t* p = (uint8_t*)(row_pixels + (x + j) * 4);
                // important to use 32bit int for computation below
                uint32_t v = 255 - intensity;
                // can further optimize if background is a fixed color
                if (rdcon->color.c == 0xFF) { // black text color
                    p[0] = p[0] * v / 255;
                    p[1] = p[1] * v / 255;  
                    p[2] = p[2] * v / 255;
                    p[3] = 0xFF;           
                }
                else { // non-black text color
                    p[0] = (p[0] * v + rdcon->color.r * intensity) / 255;
                    p[1] = (p[1] * v + rdcon->color.g * intensity) / 255;  
                    p[2] = (p[2] * v + rdcon->color.b * intensity) / 255;
                    p[3] = 0xFF;  // alpha channel
                }
            }
        }
    }
}

void render_text_view(RenderContext* rdcon, ViewText* text) {
    if (!rdcon->font.face) {
        printf("font face is null\n");
        return;
    }    
    float x = rdcon->block.x + text->x, y = rdcon->block.y + text->y;
    unsigned char* str = lxb_dom_interface_text(text->node)->char_data.data.data;  
    unsigned char* p = str + text->start_index;  unsigned char* end = p + text->length;
    // printf("draw text:%s start:%d, len:%d, x:%f, y:%f, wd:%f, hg:%f, at (%f, %f)\n", 
    //     str, text->start_index, text->length, text->x, text->y, text->width, text->height, x, y);
    bool has_space = false;  uint32_t codepoint;
    while (p < end) {
        // printf("draw character '%c'\n", *p);
        if (is_space(*p)) { 
            if (!has_space) {  // add whitespace
                has_space = true;
                // printf("draw_space: %c, x:%f, end:%f\n", *p, x, x + rdcon->font.space_width);
                x += rdcon->font.space_width;                
            } 
            // else  // skip consecutive spaces
            p++;
        }
        else {
            has_space = false;
            int bytes = utf8_to_codepoint(p, &codepoint);
            if (bytes <= 0) { p++;  codepoint = 0; }
            else { p += bytes; }

            FT_GlyphSlot glyph = load_glyph(rdcon->ui_context, rdcon->font.face, &rdcon->font.style, codepoint);
            if (!glyph) {
                // draw a square box for missing glyph
                Rect rect = {x + 1, y, rdcon->font.space_width - 2, rdcon->font.face->size->metrics.y_ppem >> 6};
                fill_surface_rect(rdcon->ui_context->surface, &rect, 0xFF0000FF, &rdcon->block.clip);
                x += rdcon->font.space_width;
            }
            else {
                // draw the glyph to the image buffer
                int ascend = rdcon->font.face->size->metrics.ascender >> 6; // still use orginal font ascend to align glyphs at same baseline
                draw_glyph(rdcon, &glyph->bitmap, x + glyph->bitmap_left, y + ascend - glyph->bitmap_top);
                // advance to the next position
                x += glyph->advance.x >> 6;
            }
        }
    }
    // render text deco
    if (rdcon->font.style.text_deco != LXB_CSS_VALUE_NONE) {
        int thinkness = max(rdcon->font.face->underline_thickness >> 6, 1);
        Rect rect;
        // todo: underline probably shoul draw below/before the text, and leaves a gap where text has descender
        if (rdcon->font.style.text_deco == LXB_CSS_VALUE_UNDERLINE) {
            // underline drawn at baseline, with a gap of thickness
            rect.x = rdcon->block.x + text->x;  rect.y = rdcon->block.y + text->y + 
                (rdcon->font.face->size->metrics.ascender >> 6) + thinkness;      
        }
        else if (rdcon->font.style.text_deco == LXB_CSS_VALUE_OVERLINE) {
            rect.x = rdcon->block.x + text->x;  rect.y = rdcon->block.y + text->y;
        }
        else if (rdcon->font.style.text_deco == LXB_CSS_VALUE_LINE_THROUGH) {
            rect.x = rdcon->block.x + text->x;  rect.y = rdcon->block.y + text->y + text->height / 2;
        }
        rect.width = text->width;  rect.height = thinkness; // corrected the variable name from h to height
        printf("text deco: %d, x:%d, y:%d, wd:%d, hg:%d\n", rdcon->font.style.text_deco, 
            rect.x, rect.y, rect.width, rect.height); // corrected w to width
        fill_surface_rect(rdcon->ui_context->surface, &rect, rdcon->color.c, &rdcon->block.clip);
    }
}

// Function to convert integer to Roman numeral
/*
static void toRoman(int num, char* result, int uppercase) {
    if (num <= 0 || num >= 4000) {
        strcpy(result, "invalid");
        return;
    }
    const int values[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    const char* symbols_lower[] = {"m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"};
    const char* symbols_upper[] = {"M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I"};
    const char** symbols = uppercase ? symbols_upper : symbols_lower;
    result[0] = '\0';
    int i = 0;
    while (num > 0) {
        while (num >= values[i]) {
            strcat(result, symbols[i]);
            num -= values[i];
        }
        i++;
    }
}

// list bullet formatting function
void formatListNumber(StrBuf* buf, int num, PropValue list_style) {
    if (num <= 0) { return; }
    switch (list_style) {
        case LXB_CSS_VALUE_LOWER_ROMAN:
            toRoman(num, buf, 0);
            break;
        case LXB_CSS_VALUE_UPPER_ROMAN:
            toRoman(num, buf, 1);
            break;
        case LXB_CSS_VALUE_UPPER_ALPHA:
            if (num > 26) {
                strcpy(result, "invalid");
            } else {
                result[0] = 'A' + (num - 1);
                result[1] = '\0';
            }
            break;
        case LXB_CSS_VALUE_LOWER_ALPHA:
            if (num > 26) {
                strcpy(result, "invalid");
            } else {
                result[0] = 'a' + (num - 1);
                result[1] = '\0';
            }
            break;
    }
}
*/
void render_list_bullet(RenderContext* rdcon, ViewBlock* list_item) {
    // bullets are aligned to the top and right side of the list item
    float ratio = rdcon->ui_context->pixel_ratio;
    if (rdcon->list.list_style_type == LXB_CSS_VALUE_DISC) {
        Rect rect;
        rect.x = rdcon->block.x + list_item->x - 15 * ratio;  
        rect.y = rdcon->block.y + list_item->y + 7 * ratio;
        rect.width = rect.height = 5 * ratio;
        fill_surface_rect(rdcon->ui_context->surface, &rect, rdcon->color.c, &rdcon->block.clip);
    }
    else if (rdcon->list.list_style_type == LXB_CSS_VALUE_DECIMAL) {
        printf("render list decimal\n");
        StrBuf* num = strbuf_new_cap(10);
        strbuf_append_format(num, "%d.", rdcon->list.item_index);
        // output the number as VIEW_TEXT
        lxb_dom_text_t node;  ViewText text;
        text.type = RDT_VIEW_TEXT;  text.next = NULL;  text.parent = NULL;
        text.start_index = 0;  text.length = num->length;
        node.char_data.data.data = (lxb_char_t *)num->str;  node.char_data.data.length = text.length;
        text.node = (lxb_dom_node_t *)&node;
        int font_size = rdcon->font.face->size->metrics.y_ppem >> 6;
        text.x = list_item->x - 20 * ratio;
        text.y = list_item->y;  // align at top the list item
        text.width = text.length * font_size;  text.height = font_size;
        render_text_view(rdcon, &text);
        strbuf_free(num);
    }
    else {
        printf("unknown list style type\n");
    }
}

void render_litem_view(RenderContext* rdcon, ViewBlock* list_item) {
    printf("view list item:%s\n", lxb_dom_element_local_name(lxb_dom_interface_element(list_item->node), NULL)); 
    rdcon->list.item_index++;
    render_block_view(rdcon, list_item);
}

void render_list_view(RenderContext* rdcon, ViewBlock* view) {
    ViewBlock* list = (ViewBlock*)view;
    printf("view list:%s\n", lxb_dom_element_local_name(lxb_dom_interface_element(list->node), NULL)); 
    ListBlot pa_list = rdcon->list;
    rdcon->list.item_index = 0;  rdcon->list.list_style_type = list->blk->list_style_type;
    render_block_view(rdcon, list);
    rdcon->list = pa_list;
}

void render_bound(RenderContext* rdcon, ViewBlock* view) {
    Rect rect;  
    rect.x = rdcon->block.x + view->x;  rect.y = rdcon->block.y + view->y;
    rect.width = view->width;  rect.height = view->height;
    // fill background, if bg-color is non-transparent
    if (view->bound->background && (view->bound->background->color.a)) {
        fill_surface_rect(rdcon->ui_context->surface, &rect, view->bound->background->color.c, &rdcon->block.clip);
    }
    if (view->bound->border) {
        if (view->bound->border->left_color.a) {
            Rect border_rect = rect;
            border_rect.width = view->bound->border->width.left;            
            fill_surface_rect(rdcon->ui_context->surface, &border_rect, view->bound->border->left_color.c, &rdcon->block.clip);
        }
        if (view->bound->border->right_color.a) {
            Rect border_rect = rect;
            border_rect.x = rect.x + rect.width - view->bound->border->width.right;
            border_rect.width = view->bound->border->width.right;
            fill_surface_rect(rdcon->ui_context->surface, &border_rect, view->bound->border->right_color.c, &rdcon->block.clip);
        }
        if (view->bound->border->top_color.a) {
            Rect border_rect = rect;
            border_rect.height = view->bound->border->width.top;
            fill_surface_rect(rdcon->ui_context->surface, &border_rect, view->bound->border->top_color.c, &rdcon->block.clip);
        }
        if (view->bound->border->bottom_color.a) {
            Rect border_rect = rect;
            border_rect.y = rect.y + rect.height - view->bound->border->width.bottom;
            border_rect.height = view->bound->border->width.bottom;
            fill_surface_rect(rdcon->ui_context->surface, &border_rect, view->bound->border->bottom_color.c, &rdcon->block.clip);
        }        
    }
}

void drawRect(Tvg_Canvas* canvas, Rect rect) {
    Tvg_Paint* shape = tvg_shape_new();
    tvg_shape_move_to(shape, rect.x, rect.y);
    tvg_shape_line_to(shape, rect.x + rect.width, rect.y);
    tvg_shape_line_to(shape, rect.x + rect.width, rect.y + rect.height);
    tvg_shape_line_to(shape, rect.x, rect.y + rect.height);
    tvg_shape_close(shape);
    tvg_shape_set_stroke_width(shape, 2); // stroke width of 2 pixels
    tvg_shape_set_stroke_color(shape, 255, 0, 0, 100); // Red stroke color (RGBA)
    // define the dash pattern for a dotted line
    float dash_pattern[2] = {8.0f, 8.0f}; // 8 units on, 8 units off
    tvg_shape_set_stroke_dash(shape, dash_pattern, 2, 0); 
    tvg_canvas_push(canvas, shape);
}

void render_block_view(RenderContext* rdcon, ViewBlock* block) {
    BlockBlot pa_block = rdcon->block;  FontBox pa_font = rdcon->font;  Color pa_color = rdcon->color;
    if (block->font) {
        setup_font(rdcon->ui_context, &rdcon->font, pa_font.face->family_name, block->font);
    }
    // render bullet after setting the font, as bullet is rendered using the same font as the list item
    if (block->type == RDT_VIEW_LIST_ITEM) {
        render_list_bullet(rdcon, block);
    }
    if (block->bound) {
        render_bound(rdcon, block);
    }

    rdcon->block.x = pa_block.x + block->x;  rdcon->block.y = pa_block.y + block->y;

    if (DEBUG_RENDER) {  // debugging outline around the block margin border
        Rect rc;
        rc.x = rdcon->block.x - (block->bound ? block->bound->margin.left : 0);  
        rc.y = rdcon->block.y - (block->bound ? block->bound->margin.top : 0);
        rc.width = block->width + (block->bound ? block->bound->margin.left + block->bound->margin.right : 0);
        rc.height = block->height + (block->bound ? block->bound->margin.top + block->bound->margin.bottom : 0);
        drawRect(rdcon->canvas, rc);
    }

    View* view = block->child;
    if (view) {
        if (block->in_line && block->in_line->color.c) {
            rdcon->color = block->in_line->color;
        }
        // setup clip box
        if (block->scroller) {
            if (block->scroller->has_clip) {
                rdcon->block.clip.x = max(rdcon->block.clip.x, rdcon->block.x + block->scroller->clip.x);
                rdcon->block.clip.y = max(rdcon->block.clip.y, rdcon->block.y + block->scroller->clip.y);
                rdcon->block.clip.width = min(rdcon->block.clip.width, block->scroller->clip.width);
                rdcon->block.clip.height = min(rdcon->block.clip.height, block->scroller->clip.height);
            }
            if (block->scroller->pane) {
                rdcon->block.x -= block->scroller->pane->h_scroll_position;
                rdcon->block.y -= block->scroller->pane->v_scroll_position;
            }
        }
        render_children(rdcon, view);
    }
    else {
        printf("view has no child\n");
    }

    // render scrollbars
    if (block->scroller) {
        printf("render scrollbars\n");
        rdcon->block.x = pa_block.x + block->x;  rdcon->block.y = pa_block.y + block->y;
        if (block->scroller->has_hz_scroll || block->scroller->has_vt_scroll) {
            if (!block->scroller->pane) {
                block->scroller->pane = (ScrollPane*)calloc(1, sizeof(ScrollPane));
            }
            Rect rect = {rdcon->block.x, rdcon->block.y, block->width, block->height};
            if (block->bound && block->bound->border) {
                rect.x += block->bound->border->width.left;
                rect.y += block->bound->border->width.top;
                rect.width -= block->bound->border->width.left + block->bound->border->width.right;
                rect.height -= block->bound->border->width.top + block->bound->border->width.bottom;
            }
            scrollpane_render(rdcon->canvas, block->scroller->pane, &rect,
                block->content_width, block->content_height);
        }
    }
    rdcon->block = pa_block;  rdcon->font = pa_font;  rdcon->color = pa_color;
}

void renderSvg(ImageSurface* surface) {
    if (!surface->pic) {
        printf("no picture to render\n");
        return;
    }
    // Step 1: Create an offscreen canvas to render the original Picture
    Tvg_Canvas* canvas = tvg_swcanvas_create();
    if (!canvas) return;

    uint32_t width = surface->max_render_width;  
    uint32_t height = surface->max_render_width * surface->height / surface->width;
    surface->pixels = (uint32_t*)malloc(width * height * sizeof(uint32_t));
    if (!surface->pixels) {
        tvg_canvas_destroy(canvas);
        return;
    }

    // Set the canvas target to the buffer
    if (tvg_swcanvas_set_target(canvas, surface->pixels, width, width, height, 
        TVG_COLORSPACE_ABGR8888) != TVG_RESULT_SUCCESS) {
        printf("Failed to set canvas target\n");
        free(surface->pixels);  surface->pixels = NULL;
        tvg_canvas_destroy(canvas);
        return;
    }

    tvg_picture_set_size(surface->pic, width, height);
    tvg_canvas_push(canvas, surface->pic);
    tvg_canvas_draw(canvas, true);
    tvg_canvas_update(canvas);

    // Step 4: Clean up canvas
    tvg_canvas_destroy(canvas); // this also frees pic
    surface->pic = NULL;
    surface->width = width;  surface->height = height;  surface->pitch = width * sizeof(uint32_t);
}

// load surface pixels to a picture
Tvg_Paint* load_picture(ImageSurface* surface) {
    Tvg_Paint* pic = tvg_picture_new();
    if (!pic) { return NULL; }
 
    // Load the raw pixel data into the new Picture
    if (tvg_picture_load_raw(pic, surface->pixels, surface->width, surface->height, 
        TVG_COLORSPACE_ABGR8888, false) != TVG_RESULT_SUCCESS) {
        printf("Failed to load raw pixel data\n");
        tvg_paint_del(pic);
        return NULL;
    }
    return pic;
}

void render_image_view(RenderContext* rdcon, ViewBlock* view) {
    printf("render image view\n");
    // render border and background, etc.
    render_block_view(rdcon, (ViewBlock*)view);
    // render the image
    if (view->embed && view->embed->img) {
        ImageSurface* img = view->embed->img;
        Rect rect;
        rect.x = rdcon->block.x + view->x;  rect.y = rdcon->block.y + view->y;
        rect.width = view->width;  rect.height = view->height;         
        if (img->format == IMAGE_FORMAT_SVG) {
            // render the SVG image
            if (!img->pixels) {
                renderSvg(img);
            }
            Tvg_Paint* pic = load_picture(img);
            if (pic) {
                tvg_picture_set_size(pic, rect.width, rect.height);
                tvg_paint_translate(pic, rect.x, rect.y);
                tvg_canvas_push(rdcon->canvas, pic);
            } else {
                printf("Failed to load svg picture\n");
            }
        } else {
            blit_surface_scaled(img, NULL, rdcon->ui_context->surface, &rect, &rdcon->block.clip);
        }
    }
    else {
        printf("image view has no image surface\n");
    }
    printf("after render image view\n");
}

void render_inline_view(RenderContext* rdcon, ViewSpan* view_span) {
    FontBox pa_font = rdcon->font;  Color pa_color = rdcon->color;
    printf("render inline view\n");
    View* view = view_span->child;
    if (view) {
        if (view_span->font) {
            setup_font(rdcon->ui_context, &rdcon->font, pa_font.face->family_name, view_span->font);
        }
        if (view_span->in_line && view_span->in_line->color.c) {
            rdcon->color = view_span->in_line->color;
        }
        render_children(rdcon, view);
    }
    else {
        printf("view has no child\n");
    }
    rdcon->font = pa_font;  rdcon->color = pa_color; 
}

void render_children(RenderContext* rdcon, View* view) {
    do {
        if (view->type == RDT_VIEW_BLOCK || view->type == RDT_VIEW_INLINE_BLOCK) {
            ViewBlock* block = (ViewBlock*)view;
            if (block->embed) {
                if (block->embed->img) {
                    render_image_view(rdcon, block);
                }
            }
            else if (block->blk && block->blk->list_style_type) {
                render_list_view(rdcon, block);
            }
            else {
                render_block_view(rdcon, block);
            }
        }
        else if (view->type == RDT_VIEW_LIST_ITEM) {
            render_litem_view(rdcon, (ViewBlock*)view);
        }

        else if (view->type == RDT_VIEW_INLINE) {
            ViewSpan* span = (ViewSpan*)view;                
            render_inline_view(rdcon, span);
        }
        else if (view->type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)view;
            render_text_view(rdcon, text);
        }
        else {
            dzlog_debug("unknown view in rendering: %d", view->type);
        }
        view = view->next;
    } while (view);
}

void drawTriangle(Tvg_Canvas* canvas) {
    Tvg_Paint* shape = tvg_shape_new();
    tvg_shape_move_to(shape, 750, 1150);
    tvg_shape_line_to(shape, 800, 1175);
    tvg_shape_line_to(shape, 750, 1200);
    tvg_shape_close(shape);
    tvg_shape_set_fill_color(shape, 255, 10, 10, 200); // semi-transparent red color
    tvg_canvas_push(canvas, shape);
}

void render_init(RenderContext* rdcon, UiContext* uicon) {
    memset(rdcon, 0, sizeof(RenderContext));
    rdcon->ui_context = uicon;
    rdcon->canvas = tvg_swcanvas_create();
    tvg_swcanvas_set_target(rdcon->canvas, uicon->surface->pixels, uicon->surface->width,
        uicon->surface->width, uicon->surface->height, TVG_COLORSPACE_ABGR8888); 

    // load default font Arial, size 16 px
    setup_font(uicon, &rdcon->font, uicon->default_font.family, &rdcon->ui_context->default_font);
    rdcon->block.clip = (Rect){0, 0, uicon->surface->width, uicon->surface->height};
}

void render_clean_up(RenderContext* rdcon) {
    tvg_canvas_destroy(rdcon->canvas);
}

// void save_surface_to_png(SDL_Surface* surface, const char* filename) {
//     int width = surface->w, height = surface->h;
//     int channels = surface->format->BytesPerPixel;
//     if (stbi_write_png(filename, width, height, channels, surface->pixels, width * channels)) {
//         printf("Successfully saved PNG: %s\n", filename);
//     } else {
//         printf("Error: Failed to save PNG\n");
//     }
// }

void render_html_doc(UiContext* uicon, View* root_view) {
    RenderContext rdcon;
    printf("Render HTML doc\n");
    render_init(&rdcon, uicon);

    // fill the surface with a white background
    fill_surface_rect(rdcon.ui_context->surface, NULL, 0xFFFFFFFF, &rdcon.block.clip);

    if (root_view && root_view->type == RDT_VIEW_BLOCK) {
        printf("Render root view:\n");
        render_block_view(&rdcon, (ViewBlock*)root_view);
    }
    else {
        fprintf(stderr, "Invalid root view\n");
    }

    // drawTriangle(rdcon.canvas);
    tvg_canvas_draw(rdcon.canvas, false); // no clearing of the buffer
    tvg_canvas_sync(rdcon.canvas);  // wait for async draw operation to complete

    // save the modified surface to a PNG file
    // save_surface_to_png(rdcon.ui_context->surface, "output.png");
    // stb_image_write very slow, so still have to use SDL_image to write image
    // if (IMG_SavePNG(rdcon.ui_context->surface, "output.png") != 0) {
    //     fprintf(stderr, "Failed to save the surface to a PNG file\n");
    // } else {
    //     printf("Rendered to output.png\n");
    // }

    render_clean_up(&rdcon);
    if (uicon->document->state) {
        uicon->document->state->is_dirty = false;
    }
}