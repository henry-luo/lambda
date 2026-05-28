#pragma once

#include "render.hpp"

typedef enum RenderOutputKind {
    RENDER_OUTPUT_SCREEN,
    RENDER_OUTPUT_PNG,
    RENDER_OUTPUT_JPEG,
    RENDER_OUTPUT_TILED_PNG,
    RENDER_OUTPUT_PDF,
    RENDER_OUTPUT_SVG
} RenderOutputKind;

typedef struct RenderOutputTarget {
    RenderOutputKind kind;
    const char* output_file;
    ImageSurface* surface;
    int width;
    int height;
    int viewport_width;
    int viewport_height;
    int jpeg_quality;
    float scale;
    float pixel_ratio;
} RenderOutputTarget;

void render_output_target_init(RenderOutputTarget* target, RenderOutputKind kind,
                               const char* output_file);
int render_output_render_view_tree_to_target(UiContext* uicon, ViewTree* view_tree,
                                             RenderOutputTarget* target);
int render_output_render_html_file_to_target(const char* html_file,
                                             RenderOutputTarget* target);
int render_html_to_output_target(const char* html_file, const char* output_file,
                                 int viewport_width, int viewport_height,
                                 float scale, float pixel_ratio,
                                 int jpeg_quality);
void render_output_render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file);
void render_output_render_tiled_png(UiContext* uicon, ViewTree* view_tree,
                                    const char* output_file,
                                    int total_width, int total_height);
