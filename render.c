
#include "render.h"

void render_block_view(RenderContext* rdcon, ViewBlock* view_block);
void render_inline_view(RenderContext* rdcon, ViewSpan* view_span);

// Function to draw a glyph bitmap into the image buffer
void draw_glyph(RenderContext* rdcon, FT_Bitmap *bitmap, int x, int y) {
    printf("draw_glyph: %d %d\n", x, y);
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
    int x = rdcon->block.x + text->x;
    int y = rdcon->block.y + text->y;
    // render each character
    unsigned char* str = lxb_dom_interface_text(text->node)->char_data.data.data;  
    unsigned char* p = str + text->start_index;  unsigned char* end = p + text->length;
    for (; p < end; p++) {
        if (FT_Load_Char(rdcon->face, *p, FT_LOAD_RENDER)) {
            fprintf(stderr, "Could not load character '%c'\n", *p);
            continue;
        }
        if (!is_space(*p)) {
            // draw the glyph to the image buffer
            draw_glyph(rdcon, &rdcon->face->glyph->bitmap, x + rdcon->face->glyph->bitmap_left, 
                y + text->height - rdcon->face->glyph->bitmap_top);
        }
        // advance to the next position
        int wd = rdcon->face->glyph->advance.x >> 6;
        x += wd;  printf("char: %c, width: %d\n", *p, wd);
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
            ViewBlock* block = (ViewText*)view;
            printf("view block:%s, x:%d, y:%d, wd:%d, hg:%d\n",
                lxb_dom_element_local_name(block->node, NULL),
                block->x, block->y, block->width, block->height);                
            render_block_view(rdcon, (ViewBlock*)view);
        }
        else if (view->type == RDT_VIEW_INLINE) {
            ViewSpan* span = (ViewSpan*)view;
            printf("view inline:%s\n", lxb_dom_element_local_name(span->node, NULL));                
            render_inline_view(rdcon, (ViewSpan*)view);
        }
        else {
            ViewText* text = (ViewText*)view;
            unsigned char* str = lxb_dom_interface_text(text->node)->char_data.data.data; 
            printf("text:%s start:%d, len:%d, x:%d, y:%d, wd:%d, hg:%d, blk_x:%d\n", 
                str, text->start_index, text->length, 
                text->x, text->y, text->width, text->height, rdcon->block.x);
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
    FT_Face pa_face = rdcon->face;  FontProp* pa_font = rdcon->font;
    rdcon->font = &view_span->font;
    printf("render inline view, deco: %s\n", lxb_css_value_by_id(view_span->font.text_deco)->name);
    View* view = view_span->child;
    if (view) {
        rdcon->face = load_styled_font(rdcon->ui_context, rdcon->face, &view_span->font);
        render_children(rdcon, view);
        // FT_Done_Face(rdcon->face);
    }
    else {
        printf("view has no child\n");
    }
    rdcon->face = pa_face;  rdcon->font = pa_font;
}

void drawTriangle(Tvg_Canvas* canvas) {
    Tvg_Paint* shape = tvg_shape_new();
    tvg_shape_move_to(shape, 400, 100);
    tvg_shape_line_to(shape, 600, 300);
    tvg_shape_line_to(shape, 100, 500);
    tvg_shape_close(shape);
    tvg_shape_set_fill_color(shape, 255, 100, 100, 150); // semi-transparent red color
    tvg_canvas_push(canvas, shape);
}

void render_init(RenderContext* rdcon, UiContext* uicon) {
    memset(rdcon, 0, sizeof(RenderContext));
    rdcon->ui_context = uicon;
    // load default font Arial, size 16 px
    rdcon->face = load_font_face(uicon, "Arial", 16);
    // Lock the surface for rendering
    if (SDL_MUSTLOCK(rdcon->ui_context->surface)) {
        SDL_LockSurface(rdcon->ui_context->surface);
    }    
}

void render_clean_up(RenderContext* rdcon) {
    FT_Done_Face(rdcon->face);
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

    SDL_Rect rect = {0, 0, 400, 600};
    SDL_FillRect(rdcon.ui_context->surface, &rect,
        SDL_MapRGBA(rdcon.ui_context->surface->format, 64, 64, 64, 255)); // gray rect
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