#ifndef RENDER_PNG_HPP
#define RENDER_PNG_HPP

#include "view.hpp"

// Function declarations for PNG rendering
void save_surface_to_png(ImageSurface* surface, const char* filename);
int render_html_to_png(const char* html_file, const char* png_file);

#endif // RENDER_PNG_HPP
