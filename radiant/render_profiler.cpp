#include "render.hpp"

#include "event.hpp"
#include "view.hpp"
#include "../lib/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void render_profiler_reset(RenderProfiler* profiler) {
    if (!profiler) {
        return;
    }
    memset(profiler, 0, sizeof(RenderProfiler));
}

double render_profiler_now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

void render_profiler_increment(RenderProfiler* profiler, RenderProfileZone zone) {
    if (!profiler) {
        return;
    }
    switch (zone) {
    case RENDER_PROFILE_GLYPH_LOAD: profiler->glyph_count++; break;
    case RENDER_PROFILE_GLYPH_DRAW: profiler->draw_count++; break;
    case RENDER_PROFILE_SETUP_FONT: profiler->setup_font_count++; break;
    case RENDER_PROFILE_BOUND: profiler->bound_count++; break;
    case RENDER_PROFILE_TEXT: profiler->text_count++; break;
    case RENDER_PROFILE_IMAGE: profiler->image_count++; break;
    case RENDER_PROFILE_SVG: profiler->svg_count++; break;
    case RENDER_PROFILE_FILTER: profiler->filter_count++; break;
    case RENDER_PROFILE_CLIP: profiler->clip_count++; break;
    case RENDER_PROFILE_OPACITY: profiler->opacity_count++; break;
    case RENDER_PROFILE_BLEND: profiler->blend_count++; break;
    case RENDER_PROFILE_BLOCK: profiler->block_count++; break;
    case RENDER_PROFILE_INLINE: profiler->inline_count++; break;
    case RENDER_PROFILE_DISPATCH: profiler->dispatch_count++; break;
    case RENDER_PROFILE_OVERFLOW_CLIP: profiler->overflow_clip_count++; break;
    case RENDER_PROFILE_FONT_METRICS: profiler->font_metrics_count++; break;
    case RENDER_PROFILE_BLOCK_SELF:
    case RENDER_PROFILE_CHILDREN:
        break;
    }
}

void render_profiler_add_time(RenderProfiler* profiler, RenderProfileZone zone, double ms) {
    if (!profiler) {
        return;
    }
    switch (zone) {
    case RENDER_PROFILE_GLYPH_LOAD: profiler->load_glyph_time += ms; break;
    case RENDER_PROFILE_GLYPH_DRAW: profiler->draw_glyph_time += ms; break;
    case RENDER_PROFILE_SETUP_FONT: profiler->setup_font_time += ms; break;
    case RENDER_PROFILE_BOUND: profiler->bound_time += ms; break;
    case RENDER_PROFILE_TEXT: profiler->text_total_time += ms; break;
    case RENDER_PROFILE_IMAGE: profiler->image_time += ms; break;
    case RENDER_PROFILE_SVG: profiler->svg_time += ms; break;
    case RENDER_PROFILE_FILTER: profiler->filter_time += ms; break;
    case RENDER_PROFILE_CLIP: profiler->clip_time += ms; break;
    case RENDER_PROFILE_OPACITY: profiler->opacity_time += ms; break;
    case RENDER_PROFILE_BLEND: profiler->blend_time += ms; break;
    case RENDER_PROFILE_INLINE: profiler->inline_time += ms; break;
    case RENDER_PROFILE_BLOCK_SELF: profiler->block_self_time += ms; break;
    case RENDER_PROFILE_CHILDREN: profiler->children_time += ms; break;
    case RENDER_PROFILE_OVERFLOW_CLIP: profiler->overflow_clip_time += ms; break;
    case RENDER_PROFILE_FONT_METRICS: profiler->font_metrics_time += ms; break;
    case RENDER_PROFILE_BLOCK:
    case RENDER_PROFILE_DISPATCH:
        break;
    }
}

void render_profiler_add_sample(RenderProfiler* profiler, RenderProfileZone zone, double ms) {
    render_profiler_add_time(profiler, zone, ms);
    render_profiler_increment(profiler, zone);
}

void render_profiler_log(RenderProfiler* profiler) {
    if (!profiler) {
        return;
    }
    log_info("[TIMING] render stats: load_glyph calls=%lld (%.1fms), draw_glyph calls=%lld (%.1fms), setup_font calls=%lld (%.1fms)",
        profiler->glyph_count, profiler->load_glyph_time,
        profiler->draw_count, profiler->draw_glyph_time,
        profiler->setup_font_count, profiler->setup_font_time);
}

static void render_profiler_stderr_line(const char* format, ...) {
    char buf[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }
    log_info("%s", buf);
}

void render_profiler_write_record_stderr(double render_ms, int surface_width,
                                         int surface_height, int display_list_items) {
    render_profiler_stderr_line("[RENDER_PROF] render_block_view: %.1fms  surface: %dx%d  dl_items: %d\n",
        render_ms, surface_width, surface_height, display_list_items);
}

void render_profiler_write_counters_stderr(RenderProfiler* profiler) {
    if (!profiler) {
        return;
    }
    render_profiler_stderr_line("[RENDER_PROF] font: load_glyph=%lld(%.1fms) draw_glyph=%lld(%.1fms) setup_font=%lld(%.1fms)\n",
        (long long)profiler->glyph_count, profiler->load_glyph_time,
        (long long)profiler->draw_count, profiler->draw_glyph_time,
        (long long)profiler->setup_font_count, profiler->setup_font_time);
    render_profiler_stderr_line("[RENDER_PROF] bound=%lld(%.1fms) text=%lld(%.1fms) image=%lld(%.1fms) svg=%lld(%.1fms)\n",
        (long long)profiler->bound_count, profiler->bound_time,
        (long long)profiler->text_count, profiler->text_total_time,
        (long long)profiler->image_count, profiler->image_time,
        (long long)profiler->svg_count, profiler->svg_time);
    render_profiler_stderr_line("[RENDER_PROF] filter=%lld(%.1fms) clip=%lld(%.1fms) opacity=%lld(%.1fms) blend=%lld(%.1fms)\n",
        (long long)profiler->filter_count, profiler->filter_time,
        (long long)profiler->clip_count, profiler->clip_time,
        (long long)profiler->opacity_count, profiler->opacity_time,
        (long long)profiler->blend_count, profiler->blend_time);
    render_profiler_stderr_line("[RENDER_PROF] blocks=%lld(self=%.1fms) inlines=%lld(%.1fms) dispatches=%lld children=%.1fms\n",
        (long long)profiler->block_count, profiler->block_self_time,
        (long long)profiler->inline_count, profiler->inline_time,
        (long long)profiler->dispatch_count, profiler->children_time);
    render_profiler_stderr_line("[RENDER_PROF] overflow_clip=%lld(%.1fms) font_metrics=%lld(%.1fms)\n",
        (long long)profiler->overflow_clip_count, profiler->overflow_clip_time,
        (long long)profiler->font_metrics_count, profiler->font_metrics_time);
}

void render_profiler_write_replay_stderr(double replay_ms, int item_count) {
    render_profiler_stderr_line("[RENDER_PROF] dl_replay: %.1fms  items: %d\n",
        replay_ms, item_count);
}

void render_profiler_write_tiled_replay_stderr(double replay_ms, int item_count,
                                               int tile_count, int thread_count) {
    render_profiler_stderr_line("[RENDER_PROF] dl_replay_tiled: %.1fms  items: %d  tiles: %d  threads: %d\n",
        replay_ms, item_count, tile_count, thread_count);
}

void render_profiler_emit_event(RenderProfiler* profiler, UiContext* uicon,
                                DocState* state, double record_ms, double replay_ms,
                                double total_ms, int item_count, bool selective,
                                bool tiled, int tile_count, int thread_count) {
    if (!profiler) {
        return;
    }
    EventStateLog* log = state && state->active_event_log ? state->active_event_log :
        (uicon ? uicon->event_log : NULL);
    if (!event_state_log_enabled(log)) {
        return;
    }

    char buf[2048];
    JsonWriter w;
    uint64_t cascade_id = state ? state->active_cascade_id : 0;
    event_state_log_begin_record(log, &w, buf, sizeof(buf), "render.stats", cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_double(&w, "record_ms", record_ms);
        jw_kv_double(&w, "replay_ms", replay_ms);
        jw_kv_double(&w, "total_ms", total_ms);
        jw_kv_int(&w, "display_list_items", item_count);
        jw_kv_bool(&w, "selective", selective);
        jw_kv_bool(&w, "tiled", tiled);
        jw_kv_int(&w, "tile_count", tile_count);
        jw_kv_int(&w, "thread_count", thread_count);
        if (uicon && uicon->surface) {
            jw_kv_int(&w, "surface_width", uicon->surface->width);
            jw_kv_int(&w, "surface_height", uicon->surface->height);
        }
        jw_key(&w, "font");
        jw_obj_begin(&w);
            jw_kv_int(&w, "load_glyph_count", profiler->glyph_count);
            jw_kv_double(&w, "load_glyph_ms", profiler->load_glyph_time);
            jw_kv_int(&w, "draw_glyph_count", profiler->draw_count);
            jw_kv_double(&w, "draw_glyph_ms", profiler->draw_glyph_time);
            jw_kv_int(&w, "setup_font_count", profiler->setup_font_count);
            jw_kv_double(&w, "setup_font_ms", profiler->setup_font_time);
            jw_kv_int(&w, "font_metrics_count", profiler->font_metrics_count);
            jw_kv_double(&w, "font_metrics_ms", profiler->font_metrics_time);
        jw_obj_end(&w);
        jw_key(&w, "ops");
        jw_obj_begin(&w);
            jw_kv_int(&w, "bound_count", profiler->bound_count);
            jw_kv_double(&w, "bound_ms", profiler->bound_time);
            jw_kv_int(&w, "text_count", profiler->text_count);
            jw_kv_double(&w, "text_ms", profiler->text_total_time);
            jw_kv_int(&w, "image_count", profiler->image_count);
            jw_kv_double(&w, "image_ms", profiler->image_time);
            jw_kv_int(&w, "svg_count", profiler->svg_count);
            jw_kv_double(&w, "svg_ms", profiler->svg_time);
            jw_kv_int(&w, "filter_count", profiler->filter_count);
            jw_kv_double(&w, "filter_ms", profiler->filter_time);
            jw_kv_int(&w, "clip_count", profiler->clip_count);
            jw_kv_double(&w, "clip_ms", profiler->clip_time);
            jw_kv_int(&w, "opacity_count", profiler->opacity_count);
            jw_kv_double(&w, "opacity_ms", profiler->opacity_time);
            jw_kv_int(&w, "blend_count", profiler->blend_count);
            jw_kv_double(&w, "blend_ms", profiler->blend_time);
            jw_kv_int(&w, "overflow_clip_count", profiler->overflow_clip_count);
            jw_kv_double(&w, "overflow_clip_ms", profiler->overflow_clip_time);
        jw_obj_end(&w);
        jw_key(&w, "tree");
        jw_obj_begin(&w);
            jw_kv_int(&w, "block_count", profiler->block_count);
            jw_kv_double(&w, "block_self_ms", profiler->block_self_time);
            jw_kv_int(&w, "inline_count", profiler->inline_count);
            jw_kv_double(&w, "inline_ms", profiler->inline_time);
            jw_kv_int(&w, "dispatch_count", profiler->dispatch_count);
            jw_kv_double(&w, "children_ms", profiler->children_time);
        jw_obj_end(&w);
    jw_obj_end(&w);
    event_state_log_finish_record(log, &w);
}

void render_profiler_emit_path_trace(RenderProfiler* profiler, UiContext* uicon,
                                     DocState* state, const RenderPathTrace* trace) {
    (void)profiler;
    if (!trace) {
        return;
    }

    log_info("[RENDER_PATH] target=%s replay=%s backend=%s dl=%d paint_ir=%d selective=%d tiled=%d large_tiled=%d items=%d tiles=%d threads=%d surface=%dx%d retained={candidates:%d captured:%d skipped:%d copy_failed:%d hits:%d misses:%d stale:%d dirty:%d} lowering={commands:%d emitted:%d fallback:%d unsupported:%d} caps={paths:%d gradients:%d clips:%d svg:%d opacity:%d blend:%d blur:%d color_matrix:%d text:%d batch:%d tile_offsets:%d}",
             trace->target ? trace->target : "unknown",
             trace->replay_mode ? trace->replay_mode : "unknown",
             trace->backend_name ? trace->backend_name : "unknown",
             trace->display_list_recorded ? 1 : 0,
             trace->paint_ir_enabled ? 1 : 0,
             trace->selective ? 1 : 0,
             trace->tiled_replay ? 1 : 0,
             trace->large_tiled_export ? 1 : 0,
             trace->display_list_items,
             trace->tile_count,
             trace->thread_count,
             trace->surface_width,
             trace->surface_height,
             trace->retained_capture_candidates,
             trace->retained_captured,
             trace->retained_skipped_non_retainable,
             trace->retained_copy_failed,
             trace->retained_reuse_hits,
             trace->retained_reuse_misses,
             trace->retained_reuse_rejected_resources,
             trace->retained_reuse_rejected_dirty,
             trace->paint_ir_commands,
             trace->paint_ir_emitted,
             trace->paint_ir_fallbacks,
             trace->paint_ir_unsupported,
             trace->backend_vector_paths ? 1 : 0,
             trace->backend_gradients ? 1 : 0,
             trace->backend_nested_clips ? 1 : 0,
             trace->backend_picture_svg ? 1 : 0,
             trace->backend_opacity_group ? 1 : 0,
             trace->backend_blend_modes ? 1 : 0,
             trace->backend_gaussian_blur ? 1 : 0,
             trace->backend_color_matrix_filters ? 1 : 0,
             trace->backend_native_text_runs ? 1 : 0,
             trace->backend_vector_batching ? 1 : 0,
             trace->backend_tile_offsets ? 1 : 0);

    EventStateLog* log = state && state->active_event_log ? state->active_event_log :
        (uicon ? uicon->event_log : NULL);
    if (!event_state_log_enabled(log)) {
        return;
    }

    char buf[1536];
    JsonWriter w;
    uint64_t cascade_id = state ? state->active_cascade_id : 0;
    event_state_log_begin_record(log, &w, buf, sizeof(buf), "render.path", cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "target", trace->target ? trace->target : "unknown");
        jw_kv_str(&w, "replay_mode", trace->replay_mode ? trace->replay_mode : "unknown");
        jw_kv_str(&w, "backend_name", trace->backend_name ? trace->backend_name : "unknown");
        jw_kv_bool(&w, "display_list_recorded", trace->display_list_recorded);
        jw_kv_bool(&w, "paint_ir_enabled", trace->paint_ir_enabled);
        jw_kv_bool(&w, "selective", trace->selective);
        jw_kv_bool(&w, "tiled_replay", trace->tiled_replay);
        jw_kv_bool(&w, "large_tiled_export", trace->large_tiled_export);
        jw_kv_int(&w, "display_list_items", trace->display_list_items);
        jw_kv_int(&w, "tile_count", trace->tile_count);
        jw_kv_int(&w, "thread_count", trace->thread_count);
        jw_kv_int(&w, "surface_width", trace->surface_width);
        jw_kv_int(&w, "surface_height", trace->surface_height);
        jw_key(&w, "backend_caps");
        jw_obj_begin(&w);
            jw_kv_bool(&w, "vector_paths", trace->backend_vector_paths);
            jw_kv_bool(&w, "gradients", trace->backend_gradients);
            jw_kv_bool(&w, "nested_clips", trace->backend_nested_clips);
            jw_kv_bool(&w, "picture_svg", trace->backend_picture_svg);
            jw_kv_bool(&w, "opacity_group", trace->backend_opacity_group);
            jw_kv_bool(&w, "blend_modes", trace->backend_blend_modes);
            jw_kv_bool(&w, "gaussian_blur", trace->backend_gaussian_blur);
            jw_kv_bool(&w, "color_matrix_filters", trace->backend_color_matrix_filters);
            jw_kv_bool(&w, "native_text_runs", trace->backend_native_text_runs);
            jw_kv_bool(&w, "vector_batching", trace->backend_vector_batching);
            jw_kv_bool(&w, "tile_offsets", trace->backend_tile_offsets);
        jw_obj_end(&w);
        jw_key(&w, "retained");
        jw_obj_begin(&w);
            jw_kv_int(&w, "capture_candidates", trace->retained_capture_candidates);
            jw_kv_int(&w, "captured", trace->retained_captured);
            jw_kv_int(&w, "skipped_non_retainable", trace->retained_skipped_non_retainable);
            jw_kv_int(&w, "copy_failed", trace->retained_copy_failed);
            jw_kv_int(&w, "reuse_hits", trace->retained_reuse_hits);
            jw_kv_int(&w, "reuse_misses", trace->retained_reuse_misses);
            jw_kv_int(&w, "reuse_rejected_resources", trace->retained_reuse_rejected_resources);
            jw_kv_int(&w, "reuse_rejected_dirty", trace->retained_reuse_rejected_dirty);
        jw_obj_end(&w);
        jw_key(&w, "paint_ir_lowering");
        jw_obj_begin(&w);
            jw_kv_int(&w, "commands", trace->paint_ir_commands);
            jw_kv_int(&w, "emitted", trace->paint_ir_emitted);
            jw_kv_int(&w, "fallbacks", trace->paint_ir_fallbacks);
            jw_kv_int(&w, "unsupported", trace->paint_ir_unsupported);
        jw_obj_end(&w);
    jw_obj_end(&w);
    event_state_log_finish_record(log, &w);
}
