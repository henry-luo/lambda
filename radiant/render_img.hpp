#ifndef RENDER_IMG_HPP
#define RENDER_IMG_HPP

#include "view.hpp"

// Function declarations for image rendering
void save_surface_to_png(ImageSurface* surface, const char* filename);
void save_surface_to_jpeg(ImageSurface* surface, const char* filename, int quality);
int render_html_to_png(const char* html_file, const char* png_file);
int render_html_to_jpeg(const char* html_file, const char* jpeg_file, int quality);

#endif // RENDER_IMG_HPP
