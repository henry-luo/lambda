#include "render.hpp"
#include "layout.hpp"
#include "render.hpp"
#include "event.hpp"
#include "render.hpp"

#include "../lib/tagged.hpp"
#include "../lib/mem_factory.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/str.h"
#include <chrono>
#include <pthread.h>
#include <png.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

static RenderPool* g_render_pool = nullptr;
static pthread_once_t g_render_pool_once = PTHREAD_ONCE_INIT;
static int g_render_pool_threads = 0;

typedef struct RenderOutputClearResult {
    bool selective;
    DirtyTracker* replay_dirty;
} RenderOutputClearResult;

typedef struct RenderOutputReplayResult {
    bool tiled;
    int tile_count;
    int thread_count;
} RenderOutputReplayResult;

static int render_output_render_html_file_to_target(const char* html_file,
                                                    RenderOutputTarget* target);
static void render_output_render_html_doc(UiContext* uicon, ViewTree* view_tree,
                                          const char* output_file);
static void render_output_render_tiled_png(UiContext* uicon, ViewTree* view_tree,
                                           const char* output_file,
                                           int total_width, int total_height);

static void init_render_pool_once() {
    g_render_pool = (RenderPool*)mem_calloc(1, sizeof(RenderPool), MEM_CAT_RENDER); // OBJ_HEAP_OK: process render worker pool singleton.
    render_pool_init(g_render_pool, g_render_pool_threads);
}

void render_pool_shutdown() {
    if (g_render_pool) {
        render_pool_destroy(g_render_pool);
        mem_free(g_render_pool);
        g_render_pool = nullptr;
    }
}

static int render_output_thread_count() {
    static int cached = -1;
    if (cached >= 0) return cached;
    const char* env = getenv("RADIANT_RENDER_THREADS");
    if (env) {
        cached = atoi(env);
        if (cached < 0) cached = 0;
    } else {
        cached = 0;
    }
    return cached;
}

static RenderOutputKind render_output_kind_from_file(const char* output_file) {
    if (!output_file) {
        return RENDER_OUTPUT_SCREEN;
    }

    const char* ext = strrchr(output_file, '.');
    if (!ext) {
        return RENDER_OUTPUT_PNG;
    }
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
        return RENDER_OUTPUT_JPEG;
    }
    if (strcmp(ext, ".pdf") == 0) {
        return RENDER_OUTPUT_PDF;
    }
    if (strcmp(ext, ".svg") == 0) {
        return RENDER_OUTPUT_SVG;
    }
    return RENDER_OUTPUT_PNG;
}

void render_output_target_init(RenderOutputTarget* target, RenderOutputKind kind,
                               const char* output_file) {
    if (!target) {
        return;
    }
    memset(target, 0, sizeof(RenderOutputTarget));
    target->kind = kind;
    target->output_file = output_file;
    target->jpeg_quality = 85;
    target->scale = 1.0f;
    target->pixel_ratio = 1.0f;
}

bool render_export_session_begin(RenderExportSession* session, const char* html_file,
                                 int viewport_width, int viewport_height,
                                 int fallback_width, int fallback_height, float scale) {
    if (!session || !html_file) return false;
    memset(session, 0, sizeof(*session));

    bool auto_width = viewport_width == 0;
    bool auto_height = viewport_height == 0;
    int layout_width = viewport_width > 0 ? viewport_width : fallback_width;
    int layout_height = viewport_height > 0 ? viewport_height : fallback_height;
    session->scale = scale > 0.0f ? scale : 1.0f;

    session->ui_context = (UiContext*)mem_calloc(1, sizeof(UiContext), MEM_CAT_RENDER); // OBJ_HEAP_OK: export session owns the headless UI context shell.
    if (!session->ui_context) {
        log_error("[EXPORT_SESSION] Failed to allocate headless UI context");
        return false;
    }
    if (ui_context_init(session->ui_context, true) != 0) {
        log_error("[EXPORT_SESSION] Failed to initialize headless UI context");
        mem_free(session->ui_context);
        session->ui_context = nullptr;
        return false;
    }
    ui_context_create_surface(session->ui_context, layout_width, layout_height);
    session->ui_context->window_width = layout_width;
    session->ui_context->window_height = layout_height;

    session->base_url = get_current_dir();
    if (!session->base_url) {
        log_error("[EXPORT_SESSION] Could not resolve the current directory");
        ui_context_cleanup(session->ui_context);
        mem_free(session->ui_context);
        session->ui_context = nullptr;
        return false;
    }

    session->document = load_html_doc(
        session->base_url, (char*)html_file, layout_width, layout_height);
    if (!session->document) {
        log_error("[EXPORT_SESSION] Could not load HTML file: %s", html_file);
        render_export_session_end(session);
        return false;
    }

    // Every file exporter must lay out and measure the same scaled document before encoding.
    session->ui_context->document = session->document;
    session->document->viewport.given_scale = session->scale;
    session->document->viewport.scale = session->scale;
    process_document_font_faces(session->ui_context, session->document);
    layout_html_doc(session->ui_context, session->document, false);

    session->content_width = layout_width;
    session->content_height = layout_height;
    if (session->document->view_tree && session->document->view_tree->root) {
        int bounds_width = 0;
        int bounds_height = 0;
        calculate_content_bounds(
            session->document->view_tree->root, &bounds_width, &bounds_height);
        bounds_width += 50;
        bounds_height += 50;
        if (auto_width || bounds_width > layout_width) session->content_width = bounds_width;
        if (auto_height || bounds_height > layout_height) session->content_height = bounds_height;
    }

    if (auto_width || auto_height) {
        log_info("[EXPORT_SESSION] Auto-sized output to %dx%d with content padding",
                 session->content_width, session->content_height);
    } else {
        log_debug("[EXPORT_SESSION] Content bounds are %dx%d",
                  session->content_width, session->content_height);
    }
    return true;
}

void render_export_session_end(RenderExportSession* session) {
    if (!session) return;
    if (session->base_url) {
        url_destroy(session->base_url);
        session->base_url = nullptr;
    }
    if (session->ui_context) {
        ui_context_cleanup(session->ui_context);
        mem_free(session->ui_context);
        session->ui_context = nullptr;
    }
    session->document = nullptr;
}

static const char* render_output_path_trace_target(RenderOutputKind kind) {
    switch (kind) {
        case RENDER_OUTPUT_SCREEN: return "screen";
        case RENDER_OUTPUT_PNG: return "png";
        case RENDER_OUTPUT_JPEG: return "jpeg";
        case RENDER_OUTPUT_TILED_PNG: return "tiled_png";
        case RENDER_OUTPUT_PDF: return "pdf";
        case RENDER_OUTPUT_SVG: return "svg";
    }
    return "unknown";
}

static void render_output_trace_backend_caps(RenderPathTrace* trace, const RenderBackendCaps* caps) {
    if (!trace || !caps) return;
    trace->backend_name = caps->backend_name;
    trace->backend_vector_paths = caps->vector_paths;
    trace->backend_gradients = caps->gradients;
    trace->backend_nested_clips = caps->nested_clips;
    trace->backend_picture_svg = caps->picture_svg;
    trace->backend_opacity_group = caps->opacity_group;
    trace->backend_blend_modes = caps->blend_modes;
    trace->backend_gaussian_blur = caps->gaussian_blur;
    trace->backend_color_matrix_filters = caps->color_matrix_filters;
    trace->backend_native_text_runs = caps->native_text_runs;
    trace->backend_vector_batching = caps->vector_batching;
    trace->backend_tile_offsets = caps->tile_offsets;
}

static void render_output_trace_retained_stats(RenderPathTrace* trace,
                                               RetainedDisplayListCache* cache) {
    if (!trace || !cache) return;
    RetainedDisplayListStats stats = retained_dl_cache_stats(cache);
    trace->retained_capture_candidates = stats.capture_candidates;
    trace->retained_captured = stats.captured;
    trace->retained_skipped_non_retainable = stats.skipped_non_retainable;
    trace->retained_copy_failed = stats.copy_failed;
    trace->retained_reuse_hits = stats.reuse_hits;
    trace->retained_reuse_misses = stats.reuse_misses;
    trace->retained_reuse_rejected_resources = stats.reuse_rejected_resources;
    trace->retained_reuse_rejected_dirty = stats.reuse_rejected_dirty;
}

static void render_output_init_context(RenderContext* rdcon, UiContext* uicon, ViewTree* view_tree,
                                       RenderProfiler* profiler) {
    memset(rdcon, 0, sizeof(RenderContext));
    rdcon->ui_context = uicon;
    rdcon->profiler = profiler;
    if (uicon && uicon->document && uicon->document->state) {
        rdcon->retained_dl_cache = uicon->document->state->retained_dl_cache;
    }

    mem_scratch_init(NULL, &rdcon->scratch, view_tree->scratch_arena, MEM_ROLE_RENDER, "render.scratch");
    // Semantic paint IR target: routes the rc_* primitive gateway through the
    // PaintBuilder during recording (Phase C). Reused (cleared) per primitive.
    rdcon->paint_list = (PaintList*)mem_calloc(1, sizeof(PaintList), MEM_CAT_RENDER);
    paint_list_init(rdcon->paint_list, view_tree->scratch_arena);
    rdt_vector_init(&rdcon->vec, (uint32_t*)uicon->surface->pixels,
        uicon->surface->width, uicon->surface->height, uicon->surface->width);
    const RenderBackendCaps* caps = render_backend_get_caps(&rdcon->vec);
    log_debug("render_output_init_context: vector backend=%s paths=%d gradients=%d clips=%d tile_offsets=%d",
        caps ? caps->backend_name : "unknown",
        caps ? caps->vector_paths : 0,
        caps ? caps->gradients : 0,
        caps ? caps->nested_clips : 0,
        caps ? caps->tile_offsets : 0);

    rdcon->transform = rdt_matrix_identity();
    rdcon->has_transform = false;
    rdcon->scale = uicon->pixel_ratio > 0 ? uicon->pixel_ratio : 1.0f;
    log_debug("render_output_init_context: scale factor = %.2f (pixel_ratio)", rdcon->scale);

    FontProp* default_font = view_tree->html_version == HTML5 ? &uicon->default_font : &uicon->legacy_default_font;
    log_debug("render_output_init_context default font: %s, html version: %d",
        default_font->family, view_tree->html_version);
    setup_font(uicon, &rdcon->font, default_font);
    rdcon->block.clip = {0, 0, (float)uicon->surface->width, (float)uicon->surface->height};
    rdcon->color.c = 0xFF000000;
    log_debug("render_output_init_context clip: [%.0f, %.0f, %.0f, %.0f]",
        rdcon->block.clip.left, rdcon->block.clip.top,
        rdcon->block.clip.right, rdcon->block.clip.bottom);
}

static void render_output_cleanup_context(RenderContext* rdcon) {
    if (rdcon->paint_list) {
        paint_list_destroy(rdcon->paint_list);
        mem_free(rdcon->paint_list);
        rdcon->paint_list = nullptr;
    }
    scratch_release(&rdcon->scratch);
    rdt_vector_destroy(&rdcon->vec);
}

RenderFrameScope::RenderFrameScope(RenderContext* r, UiContext* uicon, ViewTree* view_tree,
                                   RenderProfiler* profiler)
    : rdcon(r), display_list{}, context_active(false), display_list_active(false) {
    if (!rdcon || !view_tree) return;
    render_output_init_context(rdcon, uicon, view_tree, profiler);
    context_active = true;
    dl_init(&display_list, view_tree->scratch_arena);
    display_list_active = true;
    rdcon->dl = &display_list;
}

RenderFrameScope::~RenderFrameScope() {
    if (display_list_active) dl_destroy(&display_list);
    if (context_active) render_output_cleanup_context(rdcon);
}

static uint32_t render_output_canvas_background(View* root_view) {
    if (!root_view || root_view->view_type != RDT_VIEW_BLOCK) {
        return 0xFFFFFFFF;
    }

    ViewBlock* html_block = lam::view_require_block(root_view);
    bool html_has_bg = html_block->bound && html_block->boundary_mut()->background &&
                       html_block->boundary()->background->color.a > 0;
    if (html_has_bg) {
        return html_block->boundary()->background->color.c;
    }

    View* child = html_block->first_child;
    while (child) {
        if (child->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* child_block = lam::view_require_block(child);
            const char* name = child_block->node_name();
            if (name && str_ieq_const(name, strlen(name), "body")) {
                if (child_block->bound && child_block->boundary_mut()->background &&
                    child_block->boundary()->background->color.a > 0) {
                    log_debug("[RENDER] Propagating body background #%08x to canvas",
                              child_block->boundary()->background->color.c);
                    return child_block->boundary()->background->color.c;
                }
                break;
            }
        }
        child = static_cast<View*>(child->next_sibling);
    }

    return 0xFFFFFFFF;
}

static RenderOutputClearResult render_output_clear_surface(RenderContext* rdcon, ViewTree* view_tree,
                                                           DocState* state, uint32_t canvas_bg) {
    RenderOutputClearResult result = {};
    if (!rdcon || !rdcon->ui_context || !rdcon->ui_context->surface || !view_tree) {
        return result;
    }

    bool force_full = state && state->is_dirty;
    if (!force_full && state && !state->dirty_tracker.full_repaint &&
        dirty_has_regions(&state->dirty_tracker)) {
        DirtyRect* dr = state->dirty_tracker.dirty_list;
        float scale = rdcon->scale;
        while (dr) {
            Rect dirty_rect = {dr->x * scale, dr->y * scale, dr->width * scale, dr->height * scale};
            RasterPaintContext raster = {rdcon->ui_context->surface, &rdcon->block.clip, nullptr, 0};
            raster_fill_rect(&raster, &dirty_rect, canvas_bg);
            dr = dr->next;
        }

        rdcon->dirty_tracker = &state->dirty_tracker;
        Bound dirty_bounds = {};
        dirty_tracker_bounds(&state->dirty_tracker, &dirty_bounds, 1.0f);
        rdcon->dirty_union = dirty_bounds;
        rdcon->has_dirty_union = true;
        result.selective = true;
        result.replay_dirty = &state->dirty_tracker;
        log_debug("render_output_clear_surface: selective clear (dirty union: %.0f,%.0f - %.0f,%.0f)",
                  dirty_bounds.left, dirty_bounds.top, dirty_bounds.right, dirty_bounds.bottom);
        return result;
    }

    RasterPaintContext raster = {rdcon->ui_context->surface, &rdcon->block.clip, nullptr, 0};
    raster_fill_rect(&raster, NULL, canvas_bg);
    return result;
}

static void render_output_render_view_tree(RenderContext* rdcon, ViewTree* view_tree) {
    if (!rdcon || !view_tree) {
        return;
    }

    render_raster_view_tree(rdcon, view_tree);
}

static RenderOutputReplayResult render_output_replay_display_list(RenderContext* rdcon,
                                                                  DisplayList* display_list,
                                                                  uint32_t canvas_bg,
                                                                  DirtyTracker* replay_dirty) {
    RenderOutputReplayResult result = {};
    result.thread_count = 1;
    if (!rdcon || !display_list || !rdcon->ui_context || !rdcon->ui_context->surface) {
        return result;
    }

    rdcon->dl = nullptr;

    int render_threads = render_output_thread_count();
    int item_count = dl_item_count(display_list);
    if (!dl_validate_or_log(display_list, "render_output_replay_display_list")) {
        return result;
    }
    bool has_glyphs = dl_contains_glyphs(display_list);
    if (!replay_dirty && render_threads != 1 && item_count > 0 && !has_glyphs) {
        ImageSurface* surface = rdcon->ui_context->surface;
        TileGrid grid;
        tile_grid_init(&grid, surface->width, surface->height, rdcon->scale);
        if (grid.total <= 0) {
            log_error("[RENDER] tile grid initialization failed for %dx%d surface",
                      surface->width, surface->height);
            return result;
        }
        tile_grid_clear(&grid, canvas_bg);

        g_render_pool_threads = render_threads;
        pthread_once(&g_render_pool_once, init_render_pool_once);

        // Render jobs are frame-scoped; scratch allocation prevents queue storage from outliving dispatch.
        TileJob* jobs = (TileJob*)scratch_calloc(&rdcon->scratch, (size_t)grid.total * sizeof(TileJob));
        if (!jobs) {
            log_error("[RENDER] failed to allocate %d tile jobs", grid.total);
            tile_grid_destroy(&grid);
            return result;
        }
        for (int i = 0; i < grid.total; i++) {
            jobs[i].tile = &grid.tiles[i];
            jobs[i].display_list = display_list;
            jobs[i].scale = rdcon->scale;
            jobs[i].bg_color = canvas_bg;
        }

        render_pool_dispatch(g_render_pool, jobs, grid.total);
        tile_grid_composite(&grid, surface);

        result.tiled = true;
        result.tile_count = grid.total;
        result.thread_count = g_render_pool ? g_render_pool->thread_count : 1;

        scratch_free(&rdcon->scratch, jobs);
        tile_grid_destroy(&grid);
        return result;
    }

    dl_replay(display_list, &rdcon->vec, rdcon->ui_context->surface,
              &rdcon->block.clip, &rdcon->scratch, rdcon->scale, replay_dirty);
    return result;
}

static void render_output_save_surface(ImageSurface* surface, const char* output_file) {
    if (!surface || !output_file) {
        return;
    }

    const char* ext = strrchr(output_file, '.');
    if (ext && (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)) {
        save_surface_to_jpeg(surface, output_file, 85);
    } else {
        save_surface_to_png(surface, output_file);
    }
}

static int render_output_render_raster_target(UiContext* uicon, ViewTree* view_tree,
                                              RenderOutputTarget* target) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    if (!uicon || !view_tree || !target) {
        log_error("render_output_render_raster_target: invalid render job");
        return 1;
    }

    RenderProfiler profiler;
    render_profiler_reset(&profiler);
    RenderContext rdcon;
    log_debug("Render HTML doc");
    RenderFrameScope frame(&rdcon, uicon, view_tree, &profiler);
    DisplayList& display_list = *frame.list();

    uint32_t canvas_bg = render_output_canvas_background(view_tree->root);
    DocState* state = uicon->document ? (DocState*)uicon->document->state : nullptr;
    RenderOutputClearResult clear_result =
        render_output_clear_surface(&rdcon, view_tree, state, canvas_bg);
    bool selective = clear_result.selective;

    retained_dl_cache_begin_frame(rdcon.retained_dl_cache);

    auto t_init = high_resolution_clock::now();

    render_output_render_view_tree(&rdcon, view_tree);

    auto t_render = high_resolution_clock::now();
    log_info("[TIMING] render_block_view (record): %.1fms, %d display list items",
             duration<double, std::milli>(t_render - t_init).count(), dl_item_count(&display_list));
    render_profiler_log(rdcon.profiler);

    double render_ms = duration<double, std::milli>(t_render - t_init).count();
    render_profiler_write_record_stderr(render_ms, uicon->surface->width,
        uicon->surface->height, dl_item_count(&display_list));
    render_profiler_write_counters_stderr(rdcon.profiler);

    if (uicon->document && uicon->document->state) {
        log_debug("[RENDER] calling render_ui_overlays, state=%p", (void*)uicon->document->state);
        render_ui_overlays(&rdcon, uicon->document->state);
    } else {
        log_debug("[RENDER] no state for overlays: doc=%p, state=%p",
            (void*)uicon->document, uicon->document ? (void*)uicon->document->state : nullptr);
    }
    if (!dl_validate_or_log(&display_list, "render_output_raster")) {
        return 1;
    }
    retained_dl_cache_capture(rdcon.retained_dl_cache, &display_list);

    auto t_replay_start = high_resolution_clock::now();

    int item_count = dl_item_count(&display_list);
    RenderOutputReplayResult replay_result =
        render_output_replay_display_list(&rdcon, &display_list, canvas_bg, clear_result.replay_dirty);

    auto t_replay_end = high_resolution_clock::now();
    double replay_ms = duration<double, std::milli>(t_replay_end - t_replay_start).count();
    if (replay_result.tiled) {
        log_info("[TIMING] dl_replay_tiled: %.1fms (%d items, %d tiles, %d threads)",
                 replay_ms, item_count, replay_result.tile_count, replay_result.thread_count);
        render_profiler_write_tiled_replay_stderr(replay_ms, item_count,
            replay_result.tile_count, replay_result.thread_count);
    } else {
        log_info("[TIMING] dl_replay: %.1fms (%d items)",
                 replay_ms, item_count);
        render_profiler_write_replay_stderr(replay_ms, item_count);
    }

    DocState* rstate = uicon->document ? uicon->document->state : nullptr;
    render_video_frames(&display_list, rdcon.ui_context->surface, rstate, rdcon.ui_context);

    auto t_sync = high_resolution_clock::now();
    log_info("[TIMING] render complete: %.1fms", duration<double, std::milli>(t_sync - t_render).count());
    render_profiler_emit_event(rdcon.profiler, uicon, rstate, render_ms, replay_ms,
        duration<double, std::milli>(t_sync - t_start).count(), item_count, selective,
        replay_result.tiled, replay_result.tile_count, replay_result.thread_count);
    RenderPathTrace trace = {};
    trace.target = render_output_path_trace_target(target->kind);
    trace.replay_mode = replay_result.tiled ? "display_list_tiled" : "display_list_single";
    trace.display_list_recorded = true;
    trace.paint_ir_enabled = rdcon.paint_list != nullptr;
    trace.selective = selective;
    trace.tiled_replay = replay_result.tiled;
    trace.large_tiled_export = false;
    trace.display_list_items = item_count;
    trace.tile_count = replay_result.tile_count;
    trace.thread_count = replay_result.thread_count;
    trace.surface_width = uicon->surface ? uicon->surface->width : 0;
    trace.surface_height = uicon->surface ? uicon->surface->height : 0;
    render_output_trace_backend_caps(&trace, render_backend_get_caps(&rdcon.vec));
    render_output_trace_retained_stats(&trace, rdcon.retained_dl_cache);
    render_profiler_emit_path_trace(rdcon.profiler, uicon, rstate, &trace);

    if (target->kind == RENDER_OUTPUT_JPEG && target->output_file) {
        save_surface_to_jpeg(rdcon.ui_context->surface, target->output_file,
                             target->jpeg_quality > 0 ? target->jpeg_quality : 85);
    } else if (target->kind == RENDER_OUTPUT_PNG && target->output_file) {
        save_surface_to_png(rdcon.ui_context->surface, target->output_file);
    } else {
        render_output_save_surface(rdcon.ui_context->surface, target->output_file);
    }

    auto t_save = high_resolution_clock::now();
    if (target->output_file) {
        log_info("[TIMING] save_to_file: %.1fms", duration<double, std::milli>(t_save - t_sync).count());
    }

    if (uicon->document && uicon->document->state) {
        doc_state_clear_render_flags(uicon->document->state);
    }

    auto t_end = high_resolution_clock::now();
    log_info("[TIMING] render_html_doc total: %.1fms%s",
        duration<double, std::milli>(t_end - t_start).count(), selective ? " (selective)" : "");
    return 0;
}

int render_output_render_view_tree_to_target(UiContext* uicon, ViewTree* view_tree,
                                             RenderOutputTarget* target) {
    if (!target) {
        log_error("render_output_render_view_tree_to_target: missing output target");
        return 1;
    }

    switch (target->kind) {
        case RENDER_OUTPUT_SCREEN:
        case RENDER_OUTPUT_PNG:
        case RENDER_OUTPUT_JPEG:
            return render_output_render_raster_target(uicon, view_tree, target);
        case RENDER_OUTPUT_TILED_PNG:
            render_output_render_tiled_png(uicon, view_tree, target->output_file,
                                           target->width, target->height);
            return 0;
        case RENDER_OUTPUT_PDF:
        case RENDER_OUTPUT_SVG:
            log_error("render_output_render_view_tree_to_target: PDF/SVG targets require file-level export");
            return 1;
    }

    log_error("render_output_render_view_tree_to_target: unknown output target kind %d", target->kind);
    return 1;
}

static int render_output_render_html_file_to_target(const char* html_file,
                                                    RenderOutputTarget* target) {
    if (!html_file || !target || !target->output_file) {
        log_error("render_output_render_html_file_to_target: invalid file render job");
        return 1;
    }

    float scale = target->scale > 0 ? target->scale : 1.0f;
    float pixel_ratio = target->pixel_ratio > 0 ? target->pixel_ratio : 1.0f;
    int viewport_width = target->viewport_width;
    int viewport_height = target->viewport_height;
    if (target->kind == RENDER_OUTPUT_PDF || target->kind == RENDER_OUTPUT_SVG) {
        RenderProfiler profiler;
        render_profiler_reset(&profiler);
        RenderPathTrace trace = {};
        trace.target = render_output_path_trace_target(target->kind);
        trace.replay_mode = "file_export";
        trace.backend_name = trace.target;
        trace.display_list_recorded = false;
        trace.paint_ir_enabled = false;
        trace.surface_width = viewport_width;
        trace.surface_height = viewport_height;
        render_profiler_emit_path_trace(&profiler, nullptr, nullptr, &trace);
    }

    switch (target->kind) {
        case RENDER_OUTPUT_PDF:
            return render_html_to_pdf(html_file, target->output_file,
                                      viewport_width, viewport_height, scale);
        case RENDER_OUTPUT_SVG:
            return render_html_to_svg(html_file, target->output_file,
                                      viewport_width, viewport_height, scale);
        case RENDER_OUTPUT_PNG:
        case RENDER_OUTPUT_TILED_PNG:
            return render_html_to_png(html_file, target->output_file,
                                      viewport_width, viewport_height, scale, pixel_ratio);
        case RENDER_OUTPUT_JPEG:
            return render_html_to_jpeg(html_file, target->output_file,
                                       target->jpeg_quality > 0 ? target->jpeg_quality : 85,
                                       viewport_width, viewport_height, scale, pixel_ratio);
        case RENDER_OUTPUT_SCREEN:
            log_error("render_output_render_html_file_to_target: screen target needs a UiContext");
            return 1;
    }

    log_error("render_output_render_html_file_to_target: unknown output target kind %d", target->kind);
    return 1;
}

int render_html_to_output_target(const char* html_file, const char* output_file,
                                 int viewport_width, int viewport_height,
                                 float scale, float pixel_ratio,
                                 int jpeg_quality) {
    RenderOutputTarget target;
    render_output_target_init(&target, render_output_kind_from_file(output_file), output_file);
    target.viewport_width = viewport_width;
    target.viewport_height = viewport_height;
    target.scale = scale;
    target.pixel_ratio = pixel_ratio;
    target.jpeg_quality = jpeg_quality > 0 ? jpeg_quality : 85;
    return render_output_render_html_file_to_target(html_file, &target);
}

static void render_output_render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file) {
    RenderOutputTarget target;
    render_output_target_init(&target, render_output_kind_from_file(output_file), output_file);
    target.surface = uicon ? uicon->surface : nullptr;
    if (target.kind == RENDER_OUTPUT_PDF || target.kind == RENDER_OUTPUT_SVG) {
        log_error("render_output_render_html_doc: PDF/SVG require render_output_render_html_file_to_target");
        return;
    }
    render_output_render_view_tree_to_target(uicon, view_tree, &target);
}

/**
 * Render a large output image in horizontal PNG strips.
 *
 * The caller passes the full physical output size. The view tree is walked
 * once to record a single full-page display list; each horizontal strip is then
 * produced by replaying that list through dl_replay_tile() with the strip as the
 * tile region. This is the same record-once / replay-many pipeline used by
 * normal raster output and threaded tiled replay, so strip output matches
 * screen/PNG output instead of diverging through a separate per-strip walk.
 *
 * Streaming rows into libpng keeps peak pixel memory bounded by the strip
 * height rather than the full page height.
 */
static void render_output_render_tiled_png(UiContext* uicon, ViewTree* view_tree,
                                           const char* output_file,
                                           int total_width, int total_height) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    // physical pixels per strip; overridable for parity testing against normal PNG.
    int TILE_H = 4096;
    if (const char* env = getenv("RADIANT_TILE_STRIP_H")) {
        int v = atoi(env);
        if (v > 0) TILE_H = v;
    }
    int tile_count = (total_height + TILE_H - 1) / TILE_H;
    log_info("render_output_render_tiled_png: %dx%d px -> %s (%d tiles of %d px)",
        total_width, total_height, output_file,
        tile_count, TILE_H);

    FILE* fp = fopen(output_file, "wb");
    if (!fp) {
        log_error("render_output_render_tiled_png: cannot open output file: %s", output_file);
        return;
    }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); return; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, NULL); fclose(fp); return; }
    if (setjmp(png_jmpbuf(png))) {
        log_error("render_output_render_tiled_png: PNG error during write");
        png_destroy_write_struct(&png, &info); fclose(fp); return;
    }
    png_init_io(png, fp);
    png_set_IHDR(png, info, total_width, total_height,
                 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    uint32_t canvas_bg = render_output_canvas_background(view_tree->root);

    ImageSurface* saved_surface = uicon->surface;
    int saved_window_height = uicon->window_height;

    int first_h = total_height < TILE_H ? total_height : TILE_H;

    // Recording surface: render_output_init_context() needs a surface for vector
    // backend setup, but recording never touches its pixels (rdcon.dl is set).
    // Size it to the first strip; the vector backend is rebound per strip below.
    ImageSurface* rec_surf = image_surface_create(total_width, first_h);
    if (!rec_surf) {
        log_error("render_output_render_tiled_png: failed to allocate recording surface %dx%d",
            total_width, first_h);
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return;
    }

    uicon->surface = rec_surf;
    uicon->window_height = first_h;

    RenderProfiler profiler;
    render_profiler_reset(&profiler);
    RenderContext rdcon;
    RenderFrameScope frame(&rdcon, uicon, view_tree, &profiler);
    DisplayList& display_list = *frame.list();

    // Record the full page once. Use the whole page as the root clip so nothing
    // is culled at record time; per-strip culling happens during replay.
    rdcon.block.clip = {0, 0, (float)total_width, (float)total_height};

    auto t_record_start = high_resolution_clock::now();
    render_output_render_view_tree(&rdcon, view_tree);
    rdcon.dl = nullptr;
    auto t_record_end = high_resolution_clock::now();
    log_info("[TIMING] render_output_render_tiled_png record: %.1fms, %d display list items",
        duration<double, std::milli>(t_record_end - t_record_start).count(),
        dl_item_count(&display_list));
    if (!dl_validate_or_log(&display_list, "render_output_tiled_png")) {
        image_surface_destroy(rec_surf);
        uicon->surface = saved_surface;
        uicon->window_height = saved_window_height;
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return;
    }

    for (int tile_y = 0; tile_y < total_height; tile_y += TILE_H) {
        int tile_h = (tile_y + TILE_H <= total_height) ? TILE_H : (total_height - tile_y);

        ImageSurface* tile_surf = (tile_y == 0) ? rec_surf
                                                : image_surface_create(total_width, tile_h);
        if (!tile_surf) {
            log_error("render_output_render_tiled_png: failed to allocate tile surface %dx%d at y=%d",
                total_width, tile_h, tile_y);
            break;
        }

        {
            Bound tile_clip = {0, 0, (float)total_width, (float)tile_h};
            RasterPaintContext raster = {tile_surf, &tile_clip, nullptr, 0};
            raster_fill_rect(&raster, NULL, canvas_bg);
        }
        // dl_replay_tile uses tile-local coordinates and translates via tile_y,
        // so the strip surface itself starts at local origin 0.
        tile_surf->tile_offset_y = 0;

        // Rebind the vector backend to this strip's pixel buffer.
        rdt_vector_set_target(&rdcon.vec, (uint32_t*)tile_surf->pixels,
                              total_width, tile_h, total_width);

        dl_replay_tile(&display_list, &rdcon.vec, tile_surf, &rdcon.scratch,
                       0.0f, (float)tile_y, (float)total_width, (float)tile_h,
                       rdcon.scale);

        for (int y = 0; y < tile_h; y++) {
            uint8_t* row = (uint8_t*)tile_surf->pixels + y * tile_surf->pitch;
            png_write_row(png, row);
        }

        if (tile_surf != rec_surf) {
            image_surface_destroy(tile_surf);
        }
        log_info("render_output_render_tiled_png: tile y=%d..%d done", tile_y, tile_y + tile_h);
    }

    DocState* rstate = uicon->document ? uicon->document->state : nullptr;
    RenderPathTrace trace = {};
    trace.target = "tiled_png";
    trace.replay_mode = "display_list_strip";
    trace.display_list_recorded = true;
    trace.paint_ir_enabled = rdcon.paint_list != nullptr;
    trace.selective = false;
    trace.tiled_replay = true;
    trace.large_tiled_export = true;
    trace.display_list_items = dl_item_count(&display_list);
    trace.tile_count = tile_count;
    trace.thread_count = 1;
    trace.surface_width = total_width;
    trace.surface_height = total_height;
    render_output_trace_backend_caps(&trace, render_backend_get_caps(&rdcon.vec));
    render_output_trace_retained_stats(&trace, rdcon.retained_dl_cache);
    render_profiler_emit_path_trace(rdcon.profiler, uicon, rstate, &trace);

    image_surface_destroy(rec_surf);

    uicon->surface = saved_surface;
    uicon->window_height = saved_window_height;

    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(fp);

    auto t_end = high_resolution_clock::now();
    log_info("[TIMING] render_output_render_tiled_png total: %.1fms (%dx%d)",
        duration<double, std::milli>(t_end - t_start).count(), total_width, total_height);
}

void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file) {
    render_output_render_html_doc(uicon, view_tree, output_file);
}

void render_html_doc_tiled(UiContext* uicon, ViewTree* view_tree,
                           const char* output_file,
                           int total_width, int total_height) {
    render_output_render_tiled_png(uicon, view_tree, output_file,
                                   total_width, total_height);
}
