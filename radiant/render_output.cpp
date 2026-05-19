#include "render_output.hpp"
#include "layout.hpp"
#include "render_backend_caps.hpp"
#include "display_list_replay.hpp"
#include "render_img.hpp"
#include "render_overlay.hpp"
#include "render_profiler.hpp"
#include "render_raster.hpp"
#include "retained_display_list.hpp"
#include "state_store.hpp"
#include "tile_pool.h"

#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/str.h"
#include <chrono>
#include <pthread.h>
#include <png.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

void render_block_view(RenderContext* rdcon, ViewBlock* view_block);
void render_image_view(RenderContext* rdcon, ViewBlock* view);
void render_video_frames(DisplayList* dl, ImageSurface* surface, DocState* rstate, UiContext* uicon);

static RenderPool* g_render_pool = nullptr;
static pthread_once_t g_render_pool_once = PTHREAD_ONCE_INIT;
static int g_render_pool_threads = 0;

static void init_render_pool_once() {
    g_render_pool = (RenderPool*)mem_calloc(1, sizeof(RenderPool), MEM_CAT_RENDER);
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

void render_output_init_context(RenderContext* rdcon, UiContext* uicon, ViewTree* view_tree,
                                RenderProfiler* profiler) {
    memset(rdcon, 0, sizeof(RenderContext));
    rdcon->ui_context = uicon;
    rdcon->profiler = profiler;
    if (uicon && uicon->document && uicon->document->state) {
        rdcon->retained_dl_cache = uicon->document->state->retained_dl_cache;
    }

    scratch_init(&rdcon->scratch, view_tree->arena);
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

void render_output_cleanup_context(RenderContext* rdcon) {
    scratch_release(&rdcon->scratch);
    rdt_vector_destroy(&rdcon->vec);
}

uint32_t render_output_canvas_background(View* root_view) {
    if (!root_view || root_view->view_type != RDT_VIEW_BLOCK) {
        return 0xFFFFFFFF;
    }

    ViewBlock* html_block = lam::view_require_block(root_view);
    bool html_has_bg = html_block->bound && html_block->bound->background &&
                       html_block->bound->background->color.a > 0;
    if (html_has_bg) {
        return html_block->bound->background->color.c;
    }

    View* child = html_block->first_child;
    while (child) {
        if (child->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* child_block = lam::view_require_block(child);
            const char* name = child_block->node_name();
            if (name && str_ieq_const(name, strlen(name), "body")) {
                if (child_block->bound && child_block->bound->background &&
                    child_block->bound->background->color.a > 0) {
                    log_debug("[RENDER] Propagating body background #%08x to canvas",
                              child_block->bound->background->color.c);
                    return child_block->bound->background->color.c;
                }
                break;
            }
        }
        child = static_cast<View*>(child->next_sibling);
    }

    return 0xFFFFFFFF;
}

RenderOutputClearResult render_output_clear_surface(RenderContext* rdcon, ViewTree* view_tree,
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
        DirtyRect* first = state->dirty_tracker.dirty_list;
        float du_l = first->x;
        float du_t = first->y;
        float du_r = first->x + first->width;
        float du_b = first->y + first->height;
        for (DirtyRect* d = first->next; d; d = d->next) {
            if (d->x < du_l) du_l = d->x;
            if (d->y < du_t) du_t = d->y;
            if (d->x + d->width > du_r) du_r = d->x + d->width;
            if (d->y + d->height > du_b) du_b = d->y + d->height;
        }
        rdcon->dirty_union = {du_l, du_t, du_r, du_b};
        rdcon->has_dirty_union = true;
        result.selective = true;
        result.replay_dirty = &state->dirty_tracker;
        log_debug("render_output_clear_surface: selective clear (dirty union: %.0f,%.0f - %.0f,%.0f)",
                  du_l, du_t, du_r, du_b);
        return result;
    }

    RasterPaintContext raster = {rdcon->ui_context->surface, &rdcon->block.clip, nullptr, 0};
    raster_fill_rect(&raster, NULL, canvas_bg);
    return result;
}

void render_output_render_view_tree(RenderContext* rdcon, ViewTree* view_tree) {
    if (!rdcon || !view_tree) {
        return;
    }

    View* root_view = view_tree->root;
    if (root_view && root_view->view_type == RDT_VIEW_BLOCK) {
        log_debug("Render root view");
        ViewBlock* root_block = lam::view_require_block(root_view);
        if (root_block->embed && root_block->embed->img) {
            render_image_view(rdcon, root_block);
        } else {
            render_block_view(rdcon, root_block);
        }
        if (root_block->position) {
            log_debug("render absolute/fixed positioned children of root view");
            ViewBlock* child_block = root_block->position->first_abs_child;
            while (child_block) {
                render_block_view(rdcon, child_block);
                child_block = child_block->position->next_abs_sibling;
            }
        }
    }
    else {
        log_error("Invalid root view");
    }
}

RenderOutputReplayResult render_output_replay_display_list(RenderContext* rdcon,
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
    bool has_glyphs = dl_contains_glyphs(display_list);
    if (!replay_dirty && render_threads != 1 && item_count > 0 && !has_glyphs) {
        ImageSurface* surface = rdcon->ui_context->surface;
        TileGrid grid;
        tile_grid_init(&grid, surface->width, surface->height, rdcon->scale);
        tile_grid_clear(&grid, canvas_bg);

        g_render_pool_threads = render_threads;
        pthread_once(&g_render_pool_once, init_render_pool_once);

        TileJob* jobs = (TileJob*)mem_alloc(grid.total * sizeof(TileJob), MEM_CAT_RENDER);
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

        mem_free(jobs);
        tile_grid_destroy(&grid);
        return result;
    }

    dl_replay(display_list, &rdcon->vec, rdcon->ui_context->surface,
              &rdcon->block.clip, &rdcon->scratch, rdcon->scale, replay_dirty);
    return result;
}

void render_output_save_surface(ImageSurface* surface, const char* output_file) {
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

void render_output_render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    RenderProfiler profiler;
    render_profiler_reset(&profiler);
    RenderContext rdcon;
    log_debug("Render HTML doc");
    render_output_init_context(&rdcon, uicon, view_tree, &profiler);

    uint32_t canvas_bg = render_output_canvas_background(view_tree->root);
    DocState* state = uicon->document ? (DocState*)uicon->document->state : nullptr;
    RenderOutputClearResult clear_result =
        render_output_clear_surface(&rdcon, view_tree, state, canvas_bg);
    bool selective = clear_result.selective;

    DisplayList display_list = {};
    dl_init(&display_list, view_tree->arena);
    rdcon.dl = &display_list;
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

    render_output_save_surface(rdcon.ui_context->surface, output_file);

    auto t_save = high_resolution_clock::now();
    if (output_file) {
        log_info("[TIMING] save_to_file: %.1fms", duration<double, std::milli>(t_save - t_sync).count());
    }

    dl_destroy(&display_list);
    render_output_cleanup_context(&rdcon);
    if (uicon->document && uicon->document->state) {
        doc_state_clear_render_flags(uicon->document->state);
    }

    auto t_end = high_resolution_clock::now();
    log_info("[TIMING] render_html_doc total: %.1fms%s",
        duration<double, std::milli>(t_end - t_start).count(), selective ? " (selective)" : "");
}

/**
 * Render a large output image in horizontal PNG strips.
 *
 * The caller passes the full physical output size. This function creates one
 * tile surface at a time, renders into page-absolute clipped coordinates, and
 * streams rows into libpng so peak pixel memory stays bounded by the tile
 * height rather than the full page height.
 */
void render_output_render_tiled_png(UiContext* uicon, ViewTree* view_tree,
                                    const char* output_file,
                                    int total_width, int total_height) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    static const int TILE_H = 4096;  // physical pixels per strip
    log_info("render_output_render_tiled_png: %dx%d px -> %s (%d tiles of %d px)",
        total_width, total_height, output_file,
        (total_height + TILE_H - 1) / TILE_H, TILE_H);

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

    for (int tile_y = 0; tile_y < total_height; tile_y += TILE_H) {
        int tile_h = (tile_y + TILE_H <= total_height) ? TILE_H : (total_height - tile_y);

        ImageSurface* tile_surf = image_surface_create(total_width, tile_h);
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
        tile_surf->tile_offset_y = tile_y;

        uicon->surface = tile_surf;
        uicon->window_height = tile_h;

        RenderProfiler profiler;
        render_profiler_reset(&profiler);
        RenderContext rdcon;
        render_output_init_context(&rdcon, uicon, view_tree, &profiler);

        rdcon.block.clip = {0, (float)tile_y, (float)total_width, (float)(tile_y + tile_h)};
        rdt_vector_set_tile_offset_y(&rdcon.vec, (float)tile_y);

        render_output_render_view_tree(&rdcon, view_tree);
        render_output_cleanup_context(&rdcon);

        for (int y = 0; y < tile_h; y++) {
            uint8_t* row = (uint8_t*)tile_surf->pixels + y * tile_surf->pitch;
            png_write_row(png, row);
        }

        image_surface_destroy(tile_surf);
        log_info("render_output_render_tiled_png: tile y=%d..%d done", tile_y, tile_y + tile_h);
    }

    uicon->surface = saved_surface;
    uicon->window_height = saved_window_height;

    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(fp);

    auto t_end = high_resolution_clock::now();
    log_info("[TIMING] render_output_render_tiled_png total: %.1fms (%dx%d)",
        duration<double, std::milli>(t_end - t_start).count(), total_width, total_height);
}
