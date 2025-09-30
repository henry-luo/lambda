#include "render.hpp"
#include "view.hpp"
#include "layout.hpp"
#include "dom.hpp"
#include "../lib/log.h"
#include <stdio.h>
#include <string.h>
#include <png.h>

// Forward declarations for functions from other modules
int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
void layout_html_doc(UiContext* uicon, Document* doc, bool is_reflow);
lxb_url_t* get_current_dir_lexbor();
Document* load_html_doc(lxb_url_t* base, char* doc_url);

// Save surface to PNG using libpng
void save_surface_to_png(ImageSurface* surface, const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        log_error("Failed to open file for writing: %s", filename);
        return;
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        log_error("Failed to create PNG write struct");
        fclose(fp);
        return;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        log_error("Failed to create PNG info struct");
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(fp);
        return;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        log_error("Error during PNG creation");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return;
    }

    png_init_io(png_ptr, fp);

    // Set image information
    png_set_IHDR(png_ptr, info_ptr, surface->width, surface->height,
                 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png_ptr, info_ptr);

    // Write image data
    uint8_t** row_pointers = (uint8_t**)malloc(sizeof(uint8_t*) * surface->height);
    for (int y = 0; y < surface->height; y++) {
        row_pointers[y] = (uint8_t*)surface->pixels + y * surface->pitch;
    }

    png_write_image(png_ptr, row_pointers);
    png_write_end(png_ptr, NULL);

    // Clean up
    free(row_pointers);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);

    printf("Successfully saved PNG: %s\n", filename);
}

// Main function to layout HTML and render to PNG
int render_html_to_png(const char* html_file, const char* png_file) {
    log_debug("render_html_to_png called with html_file='%s', png_file='%s'", html_file, png_file);

    // Initialize UI context in headless mode
    UiContext ui_context;
    if (ui_context_init(&ui_context, true) != 0) {
        log_debug("Failed to initialize UI context for PNG rendering");
        return 1;
    }

    // Create a surface for rendering
    int default_width = 800;   // Default width
    int default_height = 1200; // Default height
    ui_context_create_surface(&ui_context, default_width, default_height);

    // Get current directory for relative path resolution
    lxb_url_t* cwd = get_current_dir_lexbor();
    if (!cwd) {
        log_debug("Could not get current directory");
        ui_context_cleanup(&ui_context);
        return 1;
    }

    // Load and layout the HTML document
    Document* doc = load_html_doc(cwd, (char*)html_file);
    if (!doc) {
        log_debug("Failed to load HTML document: %s", html_file);
        ui_context_cleanup(&ui_context);
        return 1;
    }

    ui_context.document = doc;

    // Layout the document
    if (doc->dom_tree) {
        layout_html_doc(&ui_context, doc, false);
    }

    // Render the document
    if (doc && doc->view_tree && doc->view_tree->root) {
        render_html_doc(&ui_context, doc->view_tree->root, png_file);
    } else {
        log_debug("No view tree to render");
        ui_context_cleanup(&ui_context);
        return 1;
    }

    log_debug("PNG rendering completed successfully");
    ui_context_cleanup(&ui_context);
    return 0;
}
