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

typedef struct RenderOutputClearResult {
    bool selective;
    DirtyTracker* replay_dirty;
} RenderOutputClearResult;

typedef struct RenderOutputReplayResult {
    bool tiled;
    int tile_count;
    int thread_count;
} RenderOutputReplayResult;

void render_output_init_context(RenderContext* rdcon, UiContext* uicon, ViewTree* view_tree,
                                RenderProfiler* profiler);
void render_output_cleanup_context(RenderContext* rdcon);
uint32_t render_output_canvas_background(View* root_view);
RenderOutputClearResult render_output_clear_surface(RenderContext* rdcon, ViewTree* view_tree,
                                                    DocState* state, uint32_t canvas_bg);
void render_output_render_view_tree(RenderContext* rdcon, ViewTree* view_tree);
RenderOutputReplayResult render_output_replay_display_list(RenderContext* rdcon,
                                                           DisplayList* display_list,
                                                           uint32_t canvas_bg,
                                                           DirtyTracker* replay_dirty);
void render_output_save_surface(ImageSurface* surface, const char* output_file);
RenderOutputKind render_output_kind_from_file(const char* output_file);
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
