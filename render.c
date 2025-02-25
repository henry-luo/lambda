
#include "render.h"

void render_block_view(RenderContext* rdcon, ViewBlock* view_block);
void render_inline_view(RenderContext* rdcon, ViewSpan* view_span);

// Function to draw a glyph bitmap into the image buffer
void draw_glyph(RenderContext* rdcon, FT_Bitmap *bitmap, int x, int y) {
    SDL_Surface* surface = rdcon->ui_context->surface;
    for (unsigned int i = 0; i < bitmap->rows; i++) {
        Uint32* row_pixels = ((Uint32*)surface->pixels) + (y + i) * surface->pitch / 4;
        for (unsigned int j = 0; j < bitmap->width; j++) {
            unsigned char intensity = bitmap->buffer[i * bitmap->pitch + j];
            if (intensity > 0) {
                // todo: clip the pixel, if (0 <= x && x < WIDTH && 0 <= y && y < HEIGHT) 
                row_pixels[x + j] = SDL_MapRGBA(surface->format, 255, 255, 255, intensity);
                // rdcon->buffer[index] = 255; // Alpha
                // rdcon->buffer[index + 1] = intensity; // Blue
                // rdcon->buffer[index + 2] = intensity; // Green 
                // rdcon->buffer[index + 3] = intensity; // Red
            }
        }
    }
}

void render_text_view(RenderContext* rdcon, ViewText* text) {
    float x = rdcon->block.x + text->x, y = rdcon->block.y + text->y;
    unsigned char* str = lxb_dom_interface_text(text->node)->char_data.data.data;  
    unsigned char* p = str + text->start_index;  unsigned char* end = p + text->length;
    printf("text:%s start:%d, len:%d, x:%f, y:%f, wd:%f, hg:%f, blk_x:%f\n", 
        str, text->start_index, text->length, text->x, text->y, text->width, text->height, rdcon->block.x);
    bool has_space = false;   
    for (; p < end; p++) {
        if (is_space(*p)) { 
            if (has_space) continue;  // skip consecutive spaces
            else has_space = true;
            printf("draw_space: %c, x:%f, end:%f\n", *p, x, x + rdcon->space_width);
            x += rdcon->space_width;
        }
        else {
            has_space = false;
            if (FT_Load_Char(rdcon->face, *p, FT_LOAD_RENDER)) {
                fprintf(stderr, "Could not load character '%c'\n", *p);
                continue;
            }
            // draw the glyph to the image buffer
            draw_glyph(rdcon, &rdcon->face->glyph->bitmap, x + rdcon->face->glyph->bitmap_left, 
                y + (rdcon->face->ascender>>6) - rdcon->face->glyph->bitmap_top);
            // advance to the next position
            int wd = rdcon->face->glyph->advance.x >> 6;
            printf("draw_glyph: %c, x:%f, end:%f, y:%f\n", *p, x, x + wd, y);
            x += wd;
        }
    }
    // render text deco
    if (rdcon->font && rdcon->font->text_deco != LXB_CSS_VALUE_NONE) {
        int thinkness = max(rdcon->face->underline_thickness >> 6, 1);
        SDL_Rect rect;
        if (rdcon->font->text_deco == LXB_CSS_VALUE_UNDERLINE) {
            rect.x = rdcon->block.x + text->x;  rect.y = rdcon->block.y + text->y + text->height - thinkness;      
        }
        else if (rdcon->font->text_deco == LXB_CSS_VALUE_OVERLINE) {
            rect.x = rdcon->block.x + text->x;  rect.y = rdcon->block.y + text->y;
        }
        else if (rdcon->font->text_deco == LXB_CSS_VALUE_LINE_THROUGH) {
            rect.x = rdcon->block.x + text->x;  rect.y = rdcon->block.y + text->y + text->height / 2;
        }
        else {
            fprintf(stderr, "Invalid text decoration: %d\n", rdcon->font->text_deco);
            return;
        }
        rect.w = text->width;  rect.h = thinkness;
        printf("text deco: %d, x:%d, y:%d, wd:%d, hg:%d\n", rdcon->font->text_deco, rect.x, rect.y, rect.w, rect.h);
        SDL_FillRect(rdcon->ui_context->surface, &rect, 
            SDL_MapRGBA(rdcon->ui_context->surface->format, 255, 0, 0, 255));
    }
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
    BlockBlot pa_block = rdcon->block;
    View* view = view_block->child;
    if (view) {
        rdcon->block.x = pa_block.x + view_block->x;  rdcon->block.y = pa_block.y + view_block->y;
        render_children(rdcon, view);
    }
    else {
        printf("view has no child\n");
    }
    rdcon->block = pa_block;
}

void render_inline_view(RenderContext* rdcon, ViewSpan* view_span) {
    FT_Face pa_face = rdcon->face;  FontProp* pa_font = rdcon->font;  float pa_space_width = rdcon->space_width;
    rdcon->font = &view_span->font;
    printf("render inline view, deco: %s\n", lxb_css_value_by_id(view_span->font.text_deco)->name);
    View* view = view_span->child;
    if (view) {
        rdcon->face = load_styled_font(rdcon->ui_context, rdcon->face, &view_span->font);
        if (FT_Load_Char(rdcon->face, ' ', FT_LOAD_RENDER)) {
            fprintf(stderr, "could not load space character\n");
            rdcon->space_width = rdcon->face->size->metrics.height >> 6;
        } else {
            rdcon->space_width = rdcon->face->glyph->advance.x >> 6;
        }        
        render_children(rdcon, view);
    }
    else {
        printf("view has no child\n");
    }
    rdcon->face = pa_face;  rdcon->font = pa_font;  rdcon->space_width = pa_space_width;
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
    rdcon->face = load_font_face(uicon, "Arial", 16);
    if (FT_Load_Char(rdcon->face, ' ', FT_LOAD_RENDER)) {
        fprintf(stderr, "could not load space character\n");
        rdcon->space_width = rdcon->face->size->metrics.height >> 6;
    } else {
        rdcon->space_width = rdcon->face->glyph->advance.x >> 6;
    }    
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
        SDL_MapRGBA(rdcon.ui_context->surface->format, 0, 0, 0, 255));

    // SDL_Rect rect = {0, 0, 400, 600};
    // SDL_FillRect(rdcon.ui_context->surface, &rect,
    //     SDL_MapRGBA(rdcon.ui_context->surface->format, 64, 64, 64, 255)); // gray rect
    if (root_view && root_view->type == RDT_VIEW_BLOCK) {
        printf("Render root view:\n");
        render_block_view(&rdcon, (ViewBlock*)root_view);
    }
    else {
        fprintf(stderr, "Invalid root view\n");
    }
    drawTriangle(rdcon.ui_context->canvas);
    tvg_canvas_draw(rdcon.ui_context->canvas, false); // no clearing of the buffer
    tvg_canvas_sync(rdcon.ui_context->canvas);  // wait for async draw operation to complete

    // save the modified surface to a PNG file
    if (IMG_SavePNG(rdcon.ui_context->surface, "output.png") != 0) {
        fprintf(stderr, "Failed to save the surface to a PNG file\n");
    }    
    printf("Rendered to output.png\n");

    render_clean_up(&rdcon);
}