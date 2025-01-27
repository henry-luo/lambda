
#include <stdio.h>
#include <stdlib.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#define WIDTH 800
#define HEIGHT 200

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

FT_Library library;
FT_Face face;

int render_init() {
    // Initialize the image buffer
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            image[y][x] = 0;  // Set all pixels to black
        }
    }

    // Initialize FreeType
    if (FT_Init_FreeType(&library)) {
        fprintf(stderr, "Could not initialize FreeType library\n");
        return EXIT_FAILURE;
    }

    // Load a font face
    if (FT_New_Face(library, "./lato.ttf", 0, &face)) {
        fprintf(stderr, "Could not load font\n");
        printf("Could not load font\n");
        return EXIT_FAILURE;
    }
}

int y = 0;

void render_text(const char* text) {
    // Initialize the image buffer
    // for (int y = 0; y < HEIGHT; y++) {
    //     for (int x = 0; x < WIDTH; x++) {
    //         image[y][x] = 0;  // Set all pixels to black
    //     }
    // }

    // Set the font size
    FT_Set_Pixel_Sizes(face, 0, 48);

    int x = 50;  // Start position
    y += 50;

    // Render each character
    for (const char *p = text; *p; p++) {
        if (FT_Load_Char(face, *p, FT_LOAD_RENDER)) {
            fprintf(stderr, "Could not load character '%c'\n", *p);
            continue;
        }

        // Draw the glyph to the image buffer
        draw_bitmap(&face->glyph->bitmap, x + face->glyph->bitmap_left, y - face->glyph->bitmap_top);

        // Advance to the next position
        x += face->glyph->advance.x >> 6;
    }
}

void render_clean_up() {
    // Save the image to a file
    save_to_pgm("output.pgm");
    printf("Rendered text to output.pgm\n");
    // Clean up
    FT_Done_Face(face);
    FT_Done_FreeType(library);
}