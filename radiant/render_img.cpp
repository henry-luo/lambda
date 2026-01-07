#include "render.hpp"
#include "view.hpp"
#include "layout.hpp"
#include "font_face.h"
extern "C" {
#include "../lib/url.h"
}
#include <stdio.h>
#include <string.h>
#include <png.h>
#include <turbojpeg.h>
#include <chrono>

// Forward declarations for functions from other modules
int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
void layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
// load_html_doc is declared in view.hpp

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
// scale: User-specified zoom factor (default 1.0)
// pixel_ratio: Device pixel ratio for HiDPI (default 1.0, use 2.0 for Retina displays)
// Final output size is (viewport_width * scale * pixel_ratio) x (viewport_height * scale * pixel_ratio)
int render_html_to_png(const char* html_file, const char* png_file, int viewport_width, int viewport_height, float scale, float pixel_ratio) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    log_debug("render_html_to_png called with html_file='%s', png_file='%s', viewport=%dx%d, scale=%.2f, pixel_ratio=%.2f",
              html_file, png_file, viewport_width, viewport_height, scale, pixel_ratio);

    // Validate scale and pixel_ratio
    if (scale <= 0) scale = 1.0f;
    if (pixel_ratio <= 0) pixel_ratio = 1.0f;

    // Combined scale factor for physical output
    float total_scale = scale * pixel_ratio;

    // Remember if we need to auto-size (viewport was 0)
    bool auto_width = (viewport_width == 0);
    bool auto_height = (viewport_height == 0);

    // Use reasonable defaults for layout if auto-sizing (CSS pixels)
    int layout_width = viewport_width > 0 ? viewport_width : 1200;
    int layout_height = viewport_height > 0 ? viewport_height : 800;

    // Initialize UI context in headless mode
    UiContext ui_context;
    if (ui_context_init(&ui_context, true) != 0) {
        log_debug("Failed to initialize UI context for PNG rendering");
        return 1;
    }

    // Set pixel_ratio for HiDPI font rendering (crisp text)
    // This ensures fonts are loaded at the correct physical size
    ui_context.pixel_ratio = pixel_ratio;

    // Create a surface for rendering with total_scale dimensions (physical pixels)
    int surface_width = (int)(layout_width * total_scale);
    int surface_height = (int)(layout_height * total_scale);
    ui_context_create_surface(&ui_context, surface_width, surface_height);

    // Update UI context dimensions
    ui_context.window_width = surface_width;    // physical pixels
    ui_context.window_height = surface_height;  // physical pixels
    ui_context.viewport_width = layout_width;   // CSS pixels
    ui_context.viewport_height = layout_height; // CSS pixels

    // Get current directory for relative path resolution
    Url* cwd = get_current_dir();
    if (!cwd) {
        log_debug("Could not get current directory");
        ui_context_cleanup(&ui_context);
        return 1;
    }

    auto t_init = high_resolution_clock::now();
    log_info("[TIMING] Init: %.1fms", duration<double, std::milli>(t_init - t_start).count());

    // Load and layout the HTML document
    DomDocument* doc = load_html_doc(cwd, (char*)html_file, layout_width, layout_height);
    if (!doc) {
        log_debug("Failed to load HTML document: %s", html_file);
        ui_context_cleanup(&ui_context);
        return 1;
    }

    auto t_load = high_resolution_clock::now();
    log_info("[TIMING] Load HTML: %.1fms", duration<double, std::milli>(t_load - t_init).count());

    ui_context.document = doc;

    // Set document scale for rendering
    // given_scale is the user zoom, scale is the combined total_scale for physical rendering
    doc->given_scale = scale;
    doc->scale = total_scale;  // Combined scale * pixel_ratio for physical output

    // Process @font-face rules before layout
    process_document_font_faces(&ui_context, doc);

    auto t_fonts = high_resolution_clock::now();
    log_info("[TIMING] Font faces: %.1fms", duration<double, std::milli>(t_fonts - t_load).count());

    // Layout the document
    if (doc->root) {
        layout_html_doc(&ui_context, doc, false);
    }

    auto t_layout = high_resolution_clock::now();
    log_info("[TIMING] Layout: %.1fms", duration<double, std::milli>(t_layout - t_fonts).count());

    // Calculate content bounds if auto-sizing
    // Layout is in CSS logical pixels, so apply total_scale for physical output dimensions
    int output_width = (int)(layout_width * total_scale);
    int output_height = (int)(layout_height * total_scale);
    if ((auto_width || auto_height) && doc->view_tree && doc->view_tree->root) {
        extern void calculate_content_bounds(View* view, int* max_x, int* max_y);
        int content_max_x = 0, content_max_y = 0;
        calculate_content_bounds(doc->view_tree->root, &content_max_x, &content_max_y);
        // Add padding to ensure nothing is cut off
        content_max_x += 50;
        content_max_y += 50;
        // Apply total_scale to content bounds
        if (auto_width) output_width = (int)(content_max_x * total_scale);
        if (auto_height) output_height = (int)(content_max_y * total_scale);
        log_info("Auto-sized output dimensions: %dx%d (content bounds with 50px padding, scale=%.2f, pixel_ratio=%.2f)",
                 output_width, output_height, scale, pixel_ratio);

        // Recreate surface with correct output dimensions
        ui_context_create_surface(&ui_context, output_width, output_height);
    }

    // Render the document
    if (doc && doc->view_tree) {
        render_html_doc(&ui_context, doc->view_tree, png_file);
    } else {
        log_debug("No view tree to render");
        ui_context_cleanup(&ui_context);
        return 1;
    }

    auto t_render = high_resolution_clock::now();
    log_info("[TIMING] Render: %.1fms", duration<double, std::milli>(t_render - t_layout).count());

    log_debug("PNG rendering completed successfully");
    ui_context_cleanup(&ui_context);

    auto t_end = high_resolution_clock::now();
    log_info("[TIMING] Cleanup: %.1fms", duration<double, std::milli>(t_end - t_render).count());
    log_info("[TIMING] TOTAL: %.1fms", duration<double, std::milli>(t_end - t_start).count());
    return 0;
}

// Main function to layout HTML and render to JPEG
// scale: User-specified zoom factor (default 1.0)
// pixel_ratio: Device pixel ratio for HiDPI (default 1.0, use 2.0 for Retina displays)
int render_html_to_jpeg(const char* html_file, const char* jpeg_file, int quality, int viewport_width, int viewport_height, float scale, float pixel_ratio) {
    log_debug("render_html_to_jpeg called with html_file='%s', jpeg_file='%s', quality=%d, viewport=%dx%d, scale=%.2f, pixel_ratio=%.2f",
              html_file, jpeg_file, quality, viewport_width, viewport_height, scale, pixel_ratio);

    // Validate scale and pixel_ratio
    if (scale <= 0) scale = 1.0f;
    if (pixel_ratio <= 0) pixel_ratio = 1.0f;

    // Combined scale factor for physical output
    float total_scale = scale * pixel_ratio;

    // Initialize UI context in headless mode
    UiContext ui_context;
    if (ui_context_init(&ui_context, true) != 0) {
        log_debug("Failed to initialize UI context for JPEG rendering");
        return 1;
    }

    // Set pixel_ratio for HiDPI font rendering
    ui_context.pixel_ratio = pixel_ratio;

    // Calculate physical output dimensions (CSS pixels * total_scale)
    int output_width = (int)(viewport_width * total_scale);
    int output_height = (int)(viewport_height * total_scale);

    // Create a surface for rendering with scaled dimensions
    ui_context_create_surface(&ui_context, output_width, output_height);

    // Update UI context dimensions
    ui_context.window_width = output_width;     // physical pixels
    ui_context.window_height = output_height;   // physical pixels
    ui_context.viewport_width = viewport_width;   // CSS pixels
    ui_context.viewport_height = viewport_height; // CSS pixels

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

    // Set document scale for rendering
    doc->given_scale = scale;
    doc->scale = total_scale;  // Combined scale * pixel_ratio

    // Process @font-face rules before layout
    process_document_font_faces(&ui_context, doc);

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
