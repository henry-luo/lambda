#pragma once

#include "../lib/log.h"
#include "../lib/memtrack.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// opt-in counters follow synchronous tile dispatch; disabled recording takes no locks.
#define RENDER_METRIC_LIST(X) \
    X(path_parses) X(path_clones) X(path_copy_bytes) X(path_hash_bytes) \
    X(command_copy_bytes) X(payload_copy_bytes) X(native_shapes) X(native_duplicates) \
    X(batch_flushes) X(layer_recordings) X(layer_passes) X(borrowed_command_bytes) \
    X(retained_command_capacity_bytes) X(lowering_ns) X(native_shape_ns) \
    X(native_path_ns) X(native_path_temp_bytes) X(native_clip_ns) X(native_submit_ns) \
    X(native_draw_ns) X(native_sync_ns) X(native_remove_ns) X(native_duplicate_ns) \
    X(native_lock_wait_ns) X(native_canvas_ns) X(native_canvases) \
    X(flush_clip) X(flush_cpu) X(flush_target) X(flush_end) X(flush_destroy) X(flush_boundary) \
    X(full_frame_reuses) X(frame_chunk_recordings) X(basic_shape_builds) X(basic_shape_cache_hits) X(clip_segments) \
    X(tile_lock_wait_ns) X(tile_work_ns) X(shared_array_hits) X(shared_array_retains) X(shared_array_reuses) \
    X(fragment_references) X(fragment_commands) X(placement_command_copy_bytes) \
    X(svg_vector_recordings) X(svg_vector_hits) \
    X(retained_index_publications) X(retained_index_prune_checks) \
    X(retained_membership_nodes) X(retained_membership_groups) \
    X(retained_dirty_subtree_searches) X(retained_dirty_ancestry_queries) \
    X(resource_command_checks) X(resource_summary_checks) X(resource_epoch_checks) \
    X(resource_surface_checks) X(resource_child_checks) X(resource_capture_commands) X(resource_capture_chunks) \
    X(validation_command_checks) X(validation_chunk_checks) X(glyph_command_checks) X(media_command_checks)

struct RenderPipelineMetrics {
#define RENDER_METRIC_FIELD(name) uint64_t name = 0;
    RENDER_METRIC_LIST(RENDER_METRIC_FIELD)
#undef RENDER_METRIC_FIELD
};

inline thread_local RenderPipelineMetrics* render_pipeline_metrics = nullptr;

#define RENDER_METRIC_ADD(name, amount) do { \
    if (render_pipeline_metrics) __atomic_fetch_add(&render_pipeline_metrics->name, \
        (uint64_t)(amount), __ATOMIC_RELAXED); \
} while (0)

struct RenderPipelineMetricsScope {
    RenderPipelineMetrics* previous;
    explicit RenderPipelineMetricsScope(RenderPipelineMetrics* metrics)
        : previous(render_pipeline_metrics) { render_pipeline_metrics = metrics; }
    ~RenderPipelineMetricsScope() { render_pipeline_metrics = previous; }
};

inline double render_pipeline_clock_ms() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

// phase totals are worker wall time; overlapping workers must not be added to frame wall time.
struct RenderMetricTimer {
    RenderPipelineMetrics* metrics;
    uint64_t RenderPipelineMetrics::*field;
    double start;
    explicit RenderMetricTimer(uint64_t RenderPipelineMetrics::*counter)
        : metrics(render_pipeline_metrics), field(counter), start(metrics ? render_pipeline_clock_ms() : 0) {}
    ~RenderMetricTimer() {
        if (metrics) __atomic_fetch_add(&(metrics->*field),
            (uint64_t)((render_pipeline_clock_ms() - start) * 1000000.0), __ATOMIC_RELAXED);
    }
};

struct RenderPipelineMeasurement {
    RenderPipelineMetrics counters;
    RenderPipelineMetrics* previous;
    bool enabled;
    double start, previous_checkpoint;
    size_t start_bytes = 0;
    RenderPipelineMeasurement() : previous(render_pipeline_metrics), enabled(false), start(0), previous_checkpoint(0) {
        const char* setting = getenv("RADIANT_RENDER_METRICS");
        enabled = !previous && setting && strcmp(setting, "1") == 0;
        if (enabled) { render_pipeline_metrics = &counters; start = previous_checkpoint = render_pipeline_clock_ms(); start_bytes = memtrack_get_current_usage(); }
    }
    void checkpoint(const char* stage) {
        if (!enabled) return;
        double now = render_pipeline_clock_ms();
        log_notice("[RENDER_METRICS] stage=%s ms=%.3f", stage, now - previous_checkpoint);
        previous_checkpoint = now;
    }
    ~RenderPipelineMeasurement() {
        if (!enabled) return;
        checkpoint("teardown");
        MemtrackStats memory = {}; memtrack_get_stats(&memory);
        log_notice("[RENDER_METRICS] tracked_start_bytes=%zu tracked_end_bytes=%zu tracked_process_peak_bytes=%zu",
            start_bytes, memory.current_bytes, memory.peak_bytes);
        log_notice("[RENDER_METRICS] scope_total_ms=%.3f native_internal_bytes=untracked",
                 render_pipeline_clock_ms() - start);
#define RENDER_METRIC_LOG(name) log_notice("[RENDER_METRICS] " #name "=%llu", \
    (unsigned long long)__atomic_load_n(&counters.name, __ATOMIC_RELAXED));
        RENDER_METRIC_LIST(RENDER_METRIC_LOG)
#undef RENDER_METRIC_LOG
        render_pipeline_metrics = previous;
    }
};
