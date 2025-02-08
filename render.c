
#include "render.h"

#define WIDTH 800
#define HEIGHT 600

unsigned char image[HEIGHT][WIDTH];
void render_block_view(RenderContext* rdcon, ViewBlock* view_block);
void render_inline_view(RenderContext* rdcon, ViewSpan* view_span);

// Function to set a pixel in the image buffer
void set_pixel(int x, int y, unsigned char intensity) {
    if (0 <= x && x < WIDTH && 0 <= y && y < HEIGHT) {
        if (intensity > image[y][x])
            image[y][x] = intensity; // use the grayscale intensity from the glyph
    }
}

void fill_rect(int x, int y, int width, int height, unsigned char intensity) {
    for (int i = y; i < y + height; i++) {
        for (int j = x; j < x + width; j++) {
            set_pixel(j, i, intensity);
        }
    }
}

// Function to draw a glyph bitmap into the image buffer
void draw_bitmap(RenderContext* rdcon, FT_Bitmap *bitmap, int x, int y) {
    // printf("draw bitmap: %d %d\n", x, y);
    for (unsigned int i = 0; i < bitmap->rows; i++) {
        for (unsigned int j = 0; j < bitmap->width; j++) {
            unsigned char intensity = bitmap->buffer[i * bitmap->pitch + j];
            if (intensity > 0) {
                set_pixel(x + j, y + i, intensity);
                int index = ((y + i) * WIDTH + (x + j)) * 4;
                rdcon->buffer[index] = 255; // Alpha
                rdcon->buffer[index + 1] = intensity; // Blue
                rdcon->buffer[index + 2] = intensity; // Green 
                rdcon->buffer[index + 3] = intensity; // Red
            }
        }
    }
}

// Save the rendered image to a PGM file
void save_to_pgm(const char *filename) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }
    fprintf(file, "P5\n%d %d\n255\n", WIDTH, HEIGHT);
    fwrite(image, 1, WIDTH * HEIGHT, file);
    fclose(file);
}

void render_init(RenderContext* rdcon, UiContext* uicon, unsigned char* buffer) {
    memset(rdcon, 0, sizeof(RenderContext));
    rdcon->ui_context = uicon;
    // load default font Arial, size 16 px
    rdcon->face = load_font_face(uicon, "Arial", 16);
    rdcon->buffer = buffer;
}

void render_clean_up(RenderContext* rdcon) {
    FT_Done_Face(rdcon->face);
}

void render_text_view(RenderContext* rdcon, ViewText* text) {
    int x = rdcon->block.x + text->x;
    int y = rdcon->block.y + text->y;
    // render each character
    char* p = ((StyleText*)text->style)->str + text->start_index;  char* end = p + text->length;
    for (; p < end; p++) {
        if (FT_Load_Char(rdcon->face, *p, FT_LOAD_RENDER)) {
            fprintf(stderr, "Could not load character '%c'\n", *p);
            continue;
        }
        if (!is_space(*p)) {
            // draw the glyph to the image buffer
            draw_bitmap(rdcon, &rdcon->face->glyph->bitmap, x + rdcon->face->glyph->bitmap_left, 
                y + text->height - rdcon->face->glyph->bitmap_top);
        }
        // advance to the next position
        int wd = rdcon->face->glyph->advance.x >> 6;
        x += wd;  printf("char: %c, width: %d\n", *p, wd);
    }
}

void render_children(RenderContext* rdcon, View* view) {
    do {
        if (view->type == RDT_VIEW_BLOCK) {
            ViewBlock* block = (ViewText*)view;
            printf("view block:%s, x:%d, y:%d, wd:%d, hg:%d\n",
                lxb_dom_element_local_name(block->style->node, NULL),
                block->x, block->y, block->width, block->height);                
            render_block_view(rdcon, (ViewBlock*)view);
        }
        else if (view->type == RDT_VIEW_INLINE) {
            ViewSpan* span = (ViewSpan*)view;
            printf("view inline:%s\n", lxb_dom_element_local_name(span->style->node, NULL));                
            render_inline_view(rdcon, (ViewSpan*)view);
        }
        else {
            ViewText* text = (ViewText*)view;
            printf("text:%s start:%d, len:%d, x:%d, y:%d, wd:%d, hg:%d, blk_x:%d\n", 
                ((StyleText*)text->style)->str, text->start_index, text->length, 
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
    FT_Face pa_face = rdcon->face;
    View* view = view_span->child;
    if (view) {
        rdcon->face = load_styled_font(rdcon->ui_context, rdcon->face, &view_span->font);
        render_children(rdcon, view);
        // FT_Done_Face(rdcon->face);
    }
    else {
        printf("view has no child\n");
    }
    rdcon->face = pa_face;
}

void render_html_doc(UiContext* uicon, View* root_view, unsigned char* buffer) {
    RenderContext rdcon;
    printf("Render HTML doc\n");
    render_init(&rdcon, uicon, buffer);

    fill_rect(0, 0, 200, 600, 40);
    if (root_view && root_view->type == RDT_VIEW_BLOCK) {
        printf("Render root view:\n");
        render_block_view(&rdcon, (ViewBlock*)root_view);
    }
    else {
        fprintf(stderr, "Invalid root view\n");
    }
    // Save the image to a file
    save_to_pgm("output.pgm");
    printf("Rendered text to output.pgm\n");

    render_clean_up(&rdcon);
}