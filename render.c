
#include "render.h"

#define WIDTH 800
#define HEIGHT 600

unsigned char image[HEIGHT][WIDTH];

// Function to set a pixel in the image buffer
void set_pixel(int x, int y, unsigned char intensity) {
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
        if (intensity > image[y][x])
            image[y][x] = intensity; // use the grayscale intensity from the glyph
    }
}

// Function to draw a glyph bitmap into the image buffer
void draw_bitmap(FT_Bitmap *bitmap, int x, int y) {
    // printf("draw bitmap: %d %d\n", x, y);
    for (int i = 0; i < bitmap->rows; i++) {
        for (int j = 0; j < bitmap->width; j++) {
            unsigned char intensity = bitmap->buffer[i * bitmap->pitch + j];
            if (intensity > 0)
                set_pixel(x + j, y + i, intensity);
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

int render_init(RenderContext* rdcon) {
    memset(rdcon, 0, sizeof(RenderContext));
    // Initialize FreeType
    if (FT_Init_FreeType(&rdcon->library)) {
        fprintf(stderr, "Could not initialize FreeType library\n");
        return EXIT_FAILURE;
    }
    // Load a font face
    if (FT_New_Face(rdcon->library, "./lato.ttf", 0, &rdcon->face)) {
        fprintf(stderr, "Could not load font\n");
        printf("Could not load font\n");
        return EXIT_FAILURE;
    }
    // Set the font size
    FT_Set_Pixel_Sizes(rdcon->face, 0, 16);    // FT_Set_Char_Size for points
}

void render_clean_up(RenderContext* rdcon) {
    // Clean up
    FT_Done_Face(rdcon->face);
    FT_Done_FreeType(rdcon->library);
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
            draw_bitmap(&rdcon->face->glyph->bitmap, x + rdcon->face->glyph->bitmap_left, 
                y + text->height - rdcon->face->glyph->bitmap_top);
        }
        // advance to the next position
        x += rdcon->face->glyph->advance.x >> 6;
    }
}

void render_block_view(RenderContext* rdcon, ViewBlock* view_block) {
    BlockBlot pa_block = rdcon->block;
    View* view = view_block->child;
    if (view) {
        rdcon->block.x = pa_block.x + view_block->x;  rdcon->block.y = pa_block.y + view_block->y;
        do {
            if (view->type == RDT_VIEW_BLOCK) {
                ViewBlock* block = (ViewText*)view;
                printf("view block:%s, x:%d, y:%d, wd:%d, hg:%d\n",
                    lxb_dom_element_local_name(block->style->node, NULL),
                    block->x, block->y, block->width, block->height);                
                render_block_view(rdcon, (ViewBlock*)view);
            }
            else {
                ViewText* text = (ViewText*)view;
                printf("text:%s start:%d, len:%d, x:%d, y:%d, wd:%d, hg:%d\n", 
                    ((StyleText*)text->style)->str, text->start_index, text->length, 
                    text->x, text->y, text->width, text->height);
                render_text_view(rdcon, text);
            }
            view = view->next;
        } while (view);
    }
    else {
        printf("%sview has no child\n");
    }
    rdcon->block = pa_block;
}

void render_html_doc(View* root_view) {
    RenderContext rdcon;
    render_init(&rdcon);
    if (root_view && root_view->type == RDT_VIEW_BLOCK) {
        printf("Render root view:\n");
        render_block_view(&rdcon, (ViewBlock*)root_view);
    }
    else {
        printf("Invalid root view\n");
    }
    // Save the image to a file
    save_to_pgm("output.pgm");
    printf("Rendered text to output.pgm\n");

    render_clean_up(&rdcon);
}