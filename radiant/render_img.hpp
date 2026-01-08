#ifndef RENDER_IMG_HPP
#define RENDER_IMG_HPP

#include "view.hpp"

// Function declarations for image rendering
void save_surface_to_png(ImageSurface* surface, const char* filename);
void save_surface_to_jpeg(ImageSurface* surface, const char* filename, int quality);
int render_html_to_png(const char* html_file, const char* png_file, int viewport_width, int viewport_height);
int render_html_to_jpeg(const char* html_file, const char* jpeg_file, int quality, int viewport_width, int viewport_height);

// Render existing UiContext with state (caret/selection) to image file
int render_uicontext_to_png(UiContext* uicon, const char* png_file);
int render_uicontext_to_svg(UiContext* uicon, const char* svg_file);

#endif // RENDER_IMG_HPP
