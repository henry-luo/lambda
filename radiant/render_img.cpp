#include "render.hpp"
#include "view.hpp"
#include "layout.hpp"
#include <stdio.h>
#include <string.h>
#include <png.h>
#include <turbojpeg.h>

// Forward declarations for functions from other modules
int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
void layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
DomDocument* load_html_doc(Url* base, char* doc_url, int viewport_width, int viewport_height);

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

// Save surface to JPEG using TurboJPEG
void save_surface_to_jpeg(ImageSurface* surface, const char* filename, int quality) {
    tjhandle tj_instance = tjInitCompress();
    if (!tj_instance) {
        log_error("Failed to initialize TurboJPEG compressor: %s", tjGetErrorStr());
        return;
    }

    // Convert RGBA to RGB (JPEG doesn't support alpha channel)
    int width = surface->width;
    int height = surface->height;
    unsigned char* rgb_buffer = (unsigned char*)malloc(width * height * 3);
    if (!rgb_buffer) {
        log_error("Failed to allocate memory for RGB buffer");
        tjDestroy(tj_instance);
        return;
    }

    // Convert RGBA pixels to RGB
    uint8_t* src_pixels = (uint8_t*)surface->pixels;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int src_idx = (y * surface->pitch) + (x * 4); // RGBA = 4 bytes per pixel
            int dst_idx = (y * width * 3) + (x * 3);      // RGB = 3 bytes per pixel

            rgb_buffer[dst_idx + 0] = src_pixels[src_idx + 0]; // R
            rgb_buffer[dst_idx + 1] = src_pixels[src_idx + 1]; // G
            rgb_buffer[dst_idx + 2] = src_pixels[src_idx + 2]; // B
            // Skip alpha channel
        }
    }

    unsigned char* jpeg_buffer = NULL;
    unsigned long jpeg_size = 0;

    // Compress to JPEG
    int result = tjCompress2(tj_instance, rgb_buffer, width, 0, height, TJPF_RGB,
                             &jpeg_buffer, &jpeg_size, TJSAMP_444, quality, TJFLAG_FASTDCT);

    if (result != 0) {
        log_error("TurboJPEG compression failed: %s", tjGetErrorStr());
        free(rgb_buffer);
        tjDestroy(tj_instance);
        return;
    }

    // Write JPEG data to file
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        log_error("Failed to open file for writing: %s", filename);
        free(rgb_buffer);
        tjFree(jpeg_buffer);
        tjDestroy(tj_instance);
        return;
    }

    size_t written = fwrite(jpeg_buffer, 1, jpeg_size, fp);
    if (written != jpeg_size) {
        log_error("Failed to write complete JPEG data to file: %s", filename);
    } else {
        printf("Successfully saved JPEG: %s (quality: %d)\n", filename, quality);
    }

    // Clean up
    fclose(fp);
    free(rgb_buffer);
    tjFree(jpeg_buffer);
    tjDestroy(tj_instance);
}

// Main function to layout HTML and render to PNG
int render_html_to_png(const char* html_file, const char* png_file, int viewport_width, int viewport_height) {
    log_debug("render_html_to_png called with html_file='%s', png_file='%s', viewport=%dx%d",
              html_file, png_file, viewport_width, viewport_height);

    // Initialize UI context in headless mode
    UiContext ui_context;
    if (ui_context_init(&ui_context, true) != 0) {
        log_debug("Failed to initialize UI context for PNG rendering");
        return 1;
    }

    // Create a surface for rendering with specified viewport dimensions
    ui_context_create_surface(&ui_context, viewport_width, viewport_height);

    // Update UI context viewport dimensions for layout calculations
    ui_context.window_width = viewport_width;
    ui_context.window_height = viewport_height;

    // Get current directory for relative path resolution
    Url* cwd = get_current_dir();
    if (!cwd) {
        log_debug("Could not get current directory");
        ui_context_cleanup(&ui_context);
        return 1;
    }

    // Load and layout the HTML document
    DomDocument* doc = load_html_doc(cwd, (char*)html_file, viewport_width, viewport_height);
    if (!doc) {
        log_debug("Failed to load HTML document: %s", html_file);
        ui_context_cleanup(&ui_context);
        return 1;
    }

    ui_context.document = doc;

    // Layout the document
    if (doc->root) {
        layout_html_doc(&ui_context, doc, false);
    }

    // Render the document
    if (doc && doc->view_tree) {
        render_html_doc(&ui_context, doc->view_tree, png_file);
    } else {
        log_debug("No view tree to render");
        ui_context_cleanup(&ui_context);
        return 1;
    }

    log_debug("PNG rendering completed successfully");
    ui_context_cleanup(&ui_context);
    return 0;
}

// Main function to layout HTML and render to JPEG
int render_html_to_jpeg(const char* html_file, const char* jpeg_file, int quality, int viewport_width, int viewport_height) {
    log_debug("render_html_to_jpeg called with html_file='%s', jpeg_file='%s', quality=%d, viewport=%dx%d",
              html_file, jpeg_file, quality, viewport_width, viewport_height);

    // Initialize UI context in headless mode
    UiContext ui_context;
    if (ui_context_init(&ui_context, true) != 0) {
        log_debug("Failed to initialize UI context for JPEG rendering");
        return 1;
    }

    // Create a surface for rendering with specified viewport dimensions
    ui_context_create_surface(&ui_context, viewport_width, viewport_height);

    // Update UI context viewport dimensions for layout calculations
    ui_context.window_width = viewport_width;
    ui_context.window_height = viewport_height;

    // Get current directory for relative path resolution
    Url* cwd = get_current_dir();
    if (!cwd) {
        log_debug("Could not get current directory");
        ui_context_cleanup(&ui_context);
        return 1;
    }

    // Load and layout the HTML document
    DomDocument* doc = load_html_doc(cwd, (char*)html_file, viewport_width, viewport_height);
    if (!doc) {
        log_debug("Failed to load HTML document: %s", html_file);
        ui_context_cleanup(&ui_context);
        return 1;
    }

    ui_context.document = doc;

    // Layout the document
    if (doc->root) {
        layout_html_doc(&ui_context, doc, false);
    }

    // Render the document
    if (doc && doc->view_tree) {
        render_html_doc(&ui_context, doc->view_tree, jpeg_file);
    } else {
        log_debug("No view tree to render");
        ui_context_cleanup(&ui_context);
        return 1;
    }

    log_debug("JPEG rendering completed successfully");
    ui_context_cleanup(&ui_context);
    return 0;
}
