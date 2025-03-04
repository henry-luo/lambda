
#include "render.h"

void render_block_view(RenderContext* rdcon, ViewBlock* view_block);
void render_inline_view(RenderContext* rdcon, ViewSpan* view_span);

// Function to draw a glyph bitmap into the image buffer
void draw_glyph(RenderContext* rdcon, FT_Bitmap *bitmap, int x, int y) {
    SDL_Surface* surface = rdcon->ui_context->surface;
    for (Uint32 i = 0; i < bitmap->rows; i++) {
        Uint32* row_pixels = ((Uint32*)surface->pixels) + (y + i) * surface->pitch / 4;
        for (Uint32 j = 0; j < bitmap->width; j++) {
            Uint32 intensity = bitmap->buffer[i * bitmap->pitch + j];
            if (intensity > 0) {
                // todo: clip the pixel, if (0 <= x && x < WIDTH && 0 <= y && y < HEIGHT)
                // blend the pixel with the background
                unsigned char* p = (unsigned char*)(row_pixels + (x + j));
                Uint32 v = 255 - intensity;
                // todo: to further handle alpha channel
                if (rdcon->color.c) {
                    p[0] = (p[0] * v + rdcon->color.b * intensity) / 255;  
                    p[1] = (p[1] * v + rdcon->color.g * intensity) / 255;
                    p[2] = (p[2] * v + rdcon->color.r * intensity) / 255;
                }
                else { // black text color
                    p[0] = p[0] * v / 255;  
                    p[1] = p[1] * v / 255;
                    p[2] = p[2] * v / 255;
                }
            }
        }
    }
}

void render_text_view(RenderContext* rdcon, ViewText* text) {
    float x = rdcon->block.x + text->x, y = rdcon->block.y + text->y;
    unsigned char* str = lxb_dom_interface_text(text->node)->char_data.data.data;  
    unsigned char* p = str + text->start_index;  unsigned char* end = p + text->length;
    // printf("draw text:%s start:%d, len:%d, x:%f, y:%f, wd:%f, hg:%f, blk_x:%f\n", 
    //     str, text->start_index, text->length, text->x, text->y, text->width, text->height, rdcon->block.x);
    bool has_space = false;
    for (; p < end; p++) {
        // printf("draw character '%c'\n", *p);
        if (is_space(*p)) { 
            if (has_space) continue;  // skip consecutive spaces
            else has_space = true;
            printf("draw_space: %c, x:%f, end:%f\n", *p, x, x + rdcon->font.space_width);
            x += rdcon->font.space_width;
        }
        else {
            has_space = false;
            if (!rdcon->font.face) {
                printf("font face is null\n");
                return;
            }
            if (FT_Load_Char(rdcon->font.face, *p, FT_LOAD_RENDER)) {
                printf("Could not load character '%c'\n", *p);
                continue;
            }
            if (!rdcon->font.face->glyph) {
                printf("font glyph is null\n");
                return;
            }
            // draw the glyph to the image buffer
            int ascend = rdcon->font.face->size->metrics.ascender >> 6;
            // printf("draw_glyph: %c, x:%f, y:%f, asc:%d, btop:%d\n", *p, x + rdcon->font.face->glyph->bitmap_left, 
            //     y + ascend - rdcon->font.face->glyph->bitmap_top, ascend, rdcon->font.face->glyph->bitmap_top);
            draw_glyph(rdcon, &rdcon->font.face->glyph->bitmap, x + rdcon->font.face->glyph->bitmap_left, 
                y + ascend - rdcon->font.face->glyph->bitmap_top);
            // advance to the next position
            int wd = rdcon->font.face->glyph->advance.x >> 6;
            x += wd;
        }
    }
    // render text deco
    printf("before text deco\n");
    if (rdcon->font.style.text_deco != LXB_CSS_VALUE_NONE) {
        int thinkness = max(rdcon->font.face->underline_thickness >> 6, 1);
        SDL_Rect rect;
        if (rdcon->font.style.text_deco == LXB_CSS_VALUE_UNDERLINE) {
            rect.x = rdcon->block.x + text->x;  rect.y = rdcon->block.y + text->y + text->height - thinkness;      
        }
        else if (rdcon->font.style.text_deco == LXB_CSS_VALUE_OVERLINE) {
            rect.x = rdcon->block.x + text->x;  rect.y = rdcon->block.y + text->y;
        }
        else if (rdcon->font.style.text_deco == LXB_CSS_VALUE_LINE_THROUGH) {
            rect.x = rdcon->block.x + text->x;  rect.y = rdcon->block.y + text->y + text->height / 2;
        }
        rect.w = text->width;  rect.h = thinkness;
        printf("text deco: %d, x:%d, y:%d, wd:%d, hg:%d\n", rdcon->font.style.text_deco, rect.x, rect.y, rect.w, rect.h);
        SDL_FillRect(rdcon->ui_context->surface, &rect, rdcon->color.c);
    }
    printf("end of text view\n");
}

void render_children(RenderContext* rdcon, View* view) {
    do {
        if (view->type == RDT_VIEW_BLOCK) {
            ViewBlock* block = (ViewBlock*)view;
            printf("view block:%s, x:%f, y:%f, wd:%f, hg:%f\n",
                lxb_dom_element_local_name(lxb_dom_interface_element(block->node), NULL),
                block->x, block->y, block->width, block->height);                
            render_block_view(rdcon, (ViewBlock*)view);
        }
        else if (view->type == RDT_VIEW_INLINE) {
            ViewSpan* span = (ViewSpan*)view;
            printf("view inline:%s\n", lxb_dom_element_local_name(lxb_dom_interface_element(span->node), NULL));                
            render_inline_view(rdcon, (ViewSpan*)view);
        }
        else {
            ViewText* text = (ViewText*)view;
            render_text_view(rdcon, text);
        }
        view = view->next;
    } while (view);
}

void render_block_view(RenderContext* rdcon, ViewBlock* view_block) {
    BlockBlot pa_block = rdcon->block;  FontBox pa_font = rdcon->font;  Color pa_color = rdcon->color;
    if (view_block->bound) {
        SDL_Rect rect;  
        rect.x = pa_block.x + view_block->x;  rect.y = pa_block.y + view_block->y;
        rect.w = view_block->width;  rect.h = view_block->height;
        if (view_block->bound->background) {
            SDL_FillRect(rdcon->ui_context->surface, &rect, view_block->bound->background->color.c);
        }
        if (view_block->bound->border) {
            printf("@@@ render border\n");
            SDL_Rect border_rect = rect;
            border_rect.w = view_block->bound->border->width.left;
            SDL_FillRect(rdcon->ui_context->surface, &border_rect, view_block->bound->border->color.c);
            border_rect.x = rect.x + rect.w - view_block->bound->border->width.right;
            border_rect.w = view_block->bound->border->width.right;
            SDL_FillRect(rdcon->ui_context->surface, &border_rect, view_block->bound->border->color.c);
            border_rect = rect;
            border_rect.h = view_block->bound->border->width.top;
            SDL_FillRect(rdcon->ui_context->surface, &border_rect, view_block->bound->border->color.c);
            border_rect.y = rect.y + rect.h - view_block->bound->border->width.bottom;
            border_rect.h = view_block->bound->border->width.bottom;
            SDL_FillRect(rdcon->ui_context->surface, &border_rect, view_block->bound->border->color.c);
        }
    }

    View* view = view_block->child;
    if (view) {
        if (view_block->font) {
            setup_font(rdcon->ui_context, &rdcon->font, pa_font.face->family_name, view_block->font);
        }
        if (view_block->in_line && view_block->in_line->color.c) {
            rdcon->color = view_block->in_line->color;
        }
        rdcon->block.x = pa_block.x + view_block->x;  rdcon->block.y = pa_block.y + view_block->y;
        render_children(rdcon, view);
    }
    else {
        printf("view has no child\n");
    }
    rdcon->block = pa_block;  rdcon->font = pa_font;  rdcon->color = pa_color;
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

void drawTriangle(Tvg_Canvas* canvas) {
    Tvg_Paint* shape = tvg_shape_new();
    tvg_shape_move_to(shape, 400, 400);
    tvg_shape_line_to(shape, 600, 500);
    tvg_shape_line_to(shape, 100, 600);
    tvg_shape_close(shape);
    tvg_shape_set_fill_color(shape, 255, 100, 100, 150); // semi-transparent red color
    tvg_canvas_push(canvas, shape);
}

void render_init(RenderContext* rdcon, UiContext* uicon) {
    memset(rdcon, 0, sizeof(RenderContext));
    rdcon->ui_context = uicon;
    // load default font Arial, size 16 px
    setup_font(uicon, &rdcon->font, "Arial", &default_font_prop); 
    // Lock the surface for rendering
    if (SDL_MUSTLOCK(rdcon->ui_context->surface)) {
        SDL_LockSurface(rdcon->ui_context->surface);
    }    
}

void render_clean_up(RenderContext* rdcon) {
    // unlock the surface so that the pixel data can be used elsewhere (e.g., converting to a texture).
    if (SDL_MUSTLOCK(rdcon->ui_context->surface)) {
        SDL_UnlockSurface(rdcon->ui_context->surface);
    }
}

void render_html_doc(UiContext* uicon, View* root_view) {
    RenderContext rdcon;
    printf("Render HTML doc\n");
    render_init(&rdcon, uicon);

    // fill the surface with a white background
    SDL_FillRect(rdcon.ui_context->surface, NULL, 
        SDL_MapRGBA(rdcon.ui_context->surface->format, 255, 255, 255, 255));

    if (root_view && root_view->type == RDT_VIEW_BLOCK) {
        printf("Render root view:\n");
        render_block_view(&rdcon, (ViewBlock*)root_view);
    }
    else {
        fprintf(stderr, "Invalid root view\n");
    }

    // drawTriangle(rdcon.ui_context->canvas);
    // tvg_canvas_draw(rdcon.ui_context->canvas, false); // no clearing of the buffer
    // tvg_canvas_sync(rdcon.ui_context->canvas);  // wait for async draw operation to complete

    // save the modified surface to a PNG file
    if (IMG_SavePNG(rdcon.ui_context->surface, "output.png") != 0) {
        fprintf(stderr, "Failed to save the surface to a PNG file\n");
    }    
    printf("Rendered to output.png\n");

    render_clean_up(&rdcon);
}