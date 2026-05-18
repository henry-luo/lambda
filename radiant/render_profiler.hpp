#pragma once

#include <stdbool.h>
#include <stdint.h>

struct DocState;
struct UiContext;

typedef enum RenderProfileZone {
    RENDER_PROFILE_GLYPH_LOAD,
    RENDER_PROFILE_GLYPH_DRAW,
    RENDER_PROFILE_SETUP_FONT,
    RENDER_PROFILE_BOUND,
    RENDER_PROFILE_TEXT,
    RENDER_PROFILE_IMAGE,
    RENDER_PROFILE_SVG,
    RENDER_PROFILE_FILTER,
    RENDER_PROFILE_CLIP,
    RENDER_PROFILE_OPACITY,
    RENDER_PROFILE_BLEND,
    RENDER_PROFILE_BLOCK,
    RENDER_PROFILE_INLINE,
    RENDER_PROFILE_DISPATCH,
    RENDER_PROFILE_BLOCK_SELF,
    RENDER_PROFILE_CHILDREN,
    RENDER_PROFILE_OVERFLOW_CLIP,
    RENDER_PROFILE_FONT_METRICS,
} RenderProfileZone;

typedef struct RenderProfiler {
    int64_t glyph_count;
    int64_t draw_count;
    double load_glyph_time;
    double draw_glyph_time;
    int64_t setup_font_count;
    double setup_font_time;

    double bound_time;
    int64_t bound_count;
    double text_total_time;
    int64_t text_count;
    double image_time;
    int64_t image_count;
    double svg_time;
    int64_t svg_count;
    double filter_time;
    int64_t filter_count;
    double clip_time;
    int64_t clip_count;
    double opacity_time;
    int64_t opacity_count;
    double blend_time;
    int64_t blend_count;
    int64_t block_count;
    double inline_time;
    int64_t inline_count;
    int64_t dispatch_count;
    double block_self_time;
    double children_time;
    double overflow_clip_time;
    int64_t overflow_clip_count;
    double font_metrics_time;
    int64_t font_metrics_count;
} RenderProfiler;

void render_profiler_reset(RenderProfiler* profiler);
double render_profiler_now_ms();
void render_profiler_increment(RenderProfiler* profiler, RenderProfileZone zone);
void render_profiler_add_time(RenderProfiler* profiler, RenderProfileZone zone, double ms);
void render_profiler_add_sample(RenderProfiler* profiler, RenderProfileZone zone, double ms);
void render_profiler_log(RenderProfiler* profiler);
void render_profiler_write_record_stderr(double render_ms, int surface_width,
    int surface_height, int display_list_items);
void render_profiler_write_counters_stderr(RenderProfiler* profiler);
void render_profiler_write_replay_stderr(double replay_ms, int item_count);
void render_profiler_write_tiled_replay_stderr(double replay_ms, int item_count,
    int tile_count, int thread_count);
void render_profiler_emit_event(RenderProfiler* profiler, struct UiContext* uicon,
    struct DocState* state, double record_ms, double replay_ms, double total_ms,
    int item_count, bool selective, bool tiled, int tile_count, int thread_count);
