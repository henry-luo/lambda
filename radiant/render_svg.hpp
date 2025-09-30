#pragma once

#include "view.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Main function to layout HTML and render to SVG
int render_html_to_svg(const char* html_file, const char* svg_file);

// Function to render a view tree to SVG string
char* render_view_tree_to_svg(UiContext* uicon, View* root_view, int width, int height);

// Function to save SVG content to file
bool save_svg_to_file(const char* svg_content, const char* filename);

#ifdef __cplusplus
}
#endif
