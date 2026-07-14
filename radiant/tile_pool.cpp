// ==========================================================================
// TilePool — Tile grid, worker pool, and tile-aware display list replay.
//
// Phase 2 of the multi-threaded rendering proposal.
// ==========================================================================

#include "render.hpp"
#include "../lib/log.h"
#include "../lib/mem_factory.h"
#include "../lib/mem.h"
#include "../lib/memtrack.h"
#include <string.h>
#include <math.h>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
static inline int get_cpu_count() {
    SYSTEM_INFO si; GetSystemInfo(&si); return (int)si.dwNumberOfProcessors;
}
#else
#include <unistd.h>
static inline int get_cpu_count() { return (int)sysconf(_SC_NPROCESSORS_ONLN); }
#endif

// Global mutex to serialize ThorVG canvas operations across worker threads.
// ThorVG's internal state (global mpool, loader sharing counts, etc.) is not
// fully thread-safe for concurrent canvas operations from multiple threads.
// Thread-safety: each worker has its own thread-local ThorVG canvas and scratch arena

// ============================================================================
// TileGrid
// ============================================================================

void tile_grid_init(TileGrid* grid, int surface_w, int surface_h, float scale) {
    memset(grid, 0, sizeof(TileGrid));
    grid->scale = scale;
    grid->surface_w = surface_w;
    grid->surface_h = surface_h;

    int tile_px = (int)(TILE_SIZE_CSS * scale);
    if (tile_px <= 0) tile_px = TILE_SIZE_CSS;

    grid->cols = (surface_w + tile_px - 1) / tile_px;
    grid->rows = (surface_h + tile_px - 1) / tile_px;
    grid->total = grid->cols * grid->rows;

    grid->tiles = (Tile*)mem_calloc(grid->total, sizeof(Tile), MEM_CAT_RENDER);

    for (int r = 0; r < grid->rows; r++) {
        for (int c = 0; c < grid->cols; c++) {
            int idx = r * grid->cols + c;
            Tile* tile = &grid->tiles[idx];
            tile->col = c;
            tile->row = r;
            tile->x = (float)(c * tile_px);
            tile->y = (float)(r * tile_px);
            int remaining_w = surface_w - c * tile_px;
            int remaining_h = surface_h - r * tile_px;
            tile->pixel_w = remaining_w < tile_px ? remaining_w : tile_px;
            tile->pixel_h = remaining_h < tile_px ? remaining_h : tile_px;
            tile->w = (float)tile->pixel_w;
            tile->h = (float)tile->pixel_h;
            tile->stride = tile->pixel_w;
            tile->pixels = (uint32_t*)mem_calloc(tile->pixel_w * tile->pixel_h,
                                                  sizeof(uint32_t), MEM_CAT_RENDER);
        }
    }

    log_debug("[TILE_GRID] %dx%d tiles (%d total), tile_px=%d, surface=%dx%d scale=%.1f",
              grid->cols, grid->rows, grid->total, tile_px, surface_w, surface_h, scale);
}

void tile_grid_destroy(TileGrid* grid) {
    if (grid->tiles) {
        for (int i = 0; i < grid->total; i++) {
            if (grid->tiles[i].pixels) {
                mem_free(grid->tiles[i].pixels);
            }
        }
        mem_free(grid->tiles);
        grid->tiles = nullptr;
    }
    grid->total = 0;
}

void tile_grid_clear(TileGrid* grid, uint32_t color) {
    for (int i = 0; i < grid->total; i++) {
        Tile* tile = &grid->tiles[i];
        uint32_t* px = tile->pixels;
        int count = tile->pixel_w * tile->pixel_h;
        if (color == 0) {
            memset(px, 0, count * sizeof(uint32_t));
        } else {
            for (int j = 0; j < count; j++) {
                px[j] = color;
            }
        }
    }
}

void tile_grid_composite(TileGrid* grid, ImageSurface* surface) {
    if (!surface || !surface->pixels) return;
    uint32_t* dst = (uint32_t*)surface->pixels;
    int dst_stride = surface->pitch / 4;  // pitch is in bytes, stride in pixels

    for (int i = 0; i < grid->total; i++) {
        Tile* tile = &grid->tiles[i];
        int tx = (int)tile->x;
        int ty = (int)tile->y;

        for (int row = 0; row < tile->pixel_h; row++) {
            int dst_y = ty + row;
            if (dst_y >= surface->height) break;
            memcpy(dst + dst_y * dst_stride + tx,
                   tile->pixels + row * tile->stride,
                   tile->pixel_w * sizeof(uint32_t));
        }
    }
}

// ============================================================================
// RenderPool — worker pool
// ============================================================================

// Thread-local worker state (one per thread, created on first use)
static thread_local WorkerState tl_worker = {};

static void worker_init_local(Tile* tile) {
    if (tl_worker.initialized) return;
    // Initialize memory pool first — this calls ensure_rpmalloc_initialized()
    // which must happen before any malloc/new calls on this thread (rpmalloc
    // interposes on malloc; without per-thread init the shared fallback heap
    // is used, which is not thread-safe).
    tl_worker.pool = mem_pool_create(NULL, MEM_ROLE_RENDER, "tile.worker");
    tl_worker.arena = mem_arena_create(NULL, tl_worker.pool, MEM_ROLE_RENDER, "tile.arena");
    mem_scratch_init(NULL, &tl_worker.scratch, tl_worker.arena, MEM_ROLE_RENDER, "tile.scratch");
    // Now safe to create ThorVG canvas (internally uses malloc/new)
    rdt_vector_init(&tl_worker.vec, tile->pixels, tile->pixel_w, tile->pixel_h, tile->stride);
    tl_worker.initialized = true;
    log_debug("[WORKER] thread-local ThorVG canvas + scratch arena initialised");
}

static void* worker_thread_fn(void* arg) {
    RenderPool* pool = (RenderPool*)arg;

    while (true) {
        pthread_mutex_lock(&pool->mutex);

        // wait until there's work available or shutdown
        while (pool->next_job >= pool->job_count && !pool->shutdown) {
            pthread_cond_wait(&pool->work_available, &pool->mutex);
        }
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        // grab next job atomically
        int job_idx = pool->next_job++;
        pthread_mutex_unlock(&pool->mutex);

        // double-check in case of spurious wakeup race
        if (job_idx >= pool->job_count) continue;

        TileJob* job = &pool->jobs[job_idx];
        Tile* tile = job->tile;

        // initialise thread-local resources on first use
        worker_init_local(tile);

        // re-bind the ThorVG canvas to this tile's pixel buffer
        rdt_vector_set_target(&tl_worker.vec, tile->pixels,
                              tile->pixel_w, tile->pixel_h, tile->stride);

        // create a temporary ImageSurface for direct-pixel ops
        ImageSurface tile_surface = {};
        tile_surface.format = IMAGE_FORMAT_PNG;  // RGBA pixel data
        tile_surface.width = tile->pixel_w;
        tile_surface.height = tile->pixel_h;
        tile_surface.pitch = tile->pixel_w * 4;
        tile_surface.pixels = tile->pixels;
        tile_surface.tile_offset_y = 0;  // tile-local coords

        // replay display list for this tile (culling + coordinate translation)
        dl_replay_tile(job->display_list, &tl_worker.vec, &tile_surface,
                       &tl_worker.scratch,
                       tile->x, tile->y, tile->w, tile->h,
                       job->scale);

        // signal completion
        pthread_mutex_lock(&pool->mutex);
        pool->completed_jobs++;
        if (pool->completed_jobs >= pool->job_count) {
            pthread_cond_signal(&pool->all_done);
        }
        pthread_mutex_unlock(&pool->mutex);
    }

    // cleanup thread-local resources
    if (tl_worker.initialized) {
        rdt_vector_destroy(&tl_worker.vec);
        scratch_release(&tl_worker.scratch);
        if (tl_worker.arena) arena_destroy(tl_worker.arena);
        if (tl_worker.pool) pool_destroy(tl_worker.pool);
        tl_worker.initialized = false;
    }

    return nullptr;
}

void render_pool_init(RenderPool* pool, int threads) {
    memset(pool, 0, sizeof(RenderPool));

    if (threads <= 0) {
        // auto-detect: hardware concurrency, cap at 8
        threads = get_cpu_count();
        if (threads <= 0) threads = 4;
        if (threads > 8) threads = 8;
    }

    pool->thread_count = threads;
    pool->threads = (pthread_t*)mem_calloc(threads, sizeof(pthread_t), MEM_CAT_SYSTEM);
    // ensure workers block initially (next_job >= job_count when both are 0)
    pool->job_count = 0;
    pool->next_job = 0;

    pthread_mutex_init(&pool->mutex, nullptr);
    pthread_cond_init(&pool->work_available, nullptr);
    pthread_cond_init(&pool->all_done, nullptr);

    for (int i = 0; i < threads; i++) {
        pthread_create(&pool->threads[i], nullptr, worker_thread_fn, pool);
    }

    log_info("[RENDER_POOL] created %d worker threads", threads);
}

void render_pool_destroy(RenderPool* pool) {
    // signal shutdown
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->work_available);
    pthread_mutex_unlock(&pool->mutex);

    // join all workers
    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], nullptr);
    }

    mem_free(pool->threads);
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->work_available);
    pthread_cond_destroy(&pool->all_done);

    log_info("[RENDER_POOL] destroyed %d worker threads", pool->thread_count);
    memset(pool, 0, sizeof(RenderPool));
}

void render_pool_dispatch(RenderPool* pool, TileJob* jobs, int count) {
    pthread_mutex_lock(&pool->mutex);
    pool->jobs = jobs;
    pool->job_count = count;
    pool->next_job = 0;
    pool->completed_jobs = 0;
    pthread_cond_broadcast(&pool->work_available);

    // wait for all jobs to complete
    while (pool->completed_jobs < pool->job_count) {
        pthread_cond_wait(&pool->all_done, &pool->mutex);
    }
    pthread_mutex_unlock(&pool->mutex);
}

// ============================================================================
// Tile-aware display list replay
// ============================================================================

void dl_replay_tile(DisplayList* dl, RdtVector* vec,
                    ImageSurface* tile_surface, ScratchArena* scratch,
                    float tile_x, float tile_y, float tile_w, float tile_h,
                    float scale) {
    DisplayReplayBackdropStack backdrop_stack;
    dl_replay_backdrop_init(&backdrop_stack);

    DisplayReplayShadowClip shadow_clip;
    dl_replay_shadow_clip_init(&shadow_clip);

    // Tell ThorVG to translate all shapes from page-absolute to tile-local.
    // Both X and Y offsets are handled by the scene wrapper in tvg_push_draw_remove.
    rdt_vector_set_tile_offset_x(vec, tile_x);
    rdt_vector_set_tile_offset_y(vec, tile_y);

#ifndef NDEBUG
    int items_drawn = 0;
#endif
    const RenderBackendCaps* caps = render_backend_get_caps(vec);

    rdt_vector_begin_batch(vec);

    for (int i = 0; i < dl->count; i++) {
        DisplayItem* item = &dl->items[i];

        // Cull draw work that doesn't intersect this tile; the skip path below
        // still preserves clip/backdrop stack state for ordered replay.
        if (!dl_item_intersects_rect(item, tile_x, tile_y, tile_w, tile_h)) {
            if (dl_replay_vector_clip_item(vec, item)) {
                continue;
            }
            if (dl_replay_backdrop_skip_item(&backdrop_stack, scratch, item)) {
                continue;
            }

            // Still need to track clip/backdrop depth for correctness
            switch (item->op) {
                case DL_SHADOW_CLIP_SAVE:
                    dl_replay_shadow_clip_discard(&shadow_clip);
                    break;
                case DL_SHADOW_CLIP_RESTORE:
                    dl_replay_shadow_clip_discard(&shadow_clip);
                    break;
                case DL_OUTER_SHADOW:
                    // self-contained, nothing to track on skip
                    break;
                case DL_BEGIN_ELEMENT:
                    if (item->element_marker.matching_index > i) {
                        i = item->element_marker.matching_index;
                    }
                    break;
                default:
                    break;
            }
            continue;
        }

        DisplayReplayVectorResult vector_result = dl_replay_vector_item(vec, item, true);
        if (vector_result != DL_REPLAY_VECTOR_NOT_HANDLED) {
#ifndef NDEBUG
            if (vector_result == DL_REPLAY_VECTOR_DREW) items_drawn++;
#endif
            continue;
        }

        // vector ops are handled before this switch by dl_replay_vector_item().
        switch (item->op) {
        case DL_FILL_RECT:
        case DL_FILL_ROUNDED_RECT:
        case DL_FILL_PATH:
        case DL_STROKE_PATH:
        case DL_FILL_LINEAR_GRADIENT:
        case DL_FILL_RADIAL_GRADIENT:
        case DL_DRAW_IMAGE:
        case DL_DRAW_PICTURE:
        case DL_PUSH_CLIP:
        case DL_POP_CLIP:
            break;

        // -- Direct-pixel operations: manual tile-local coordinate translation --

        case DL_DRAW_GLYPH: {
            rdt_vector_flush_batch(vec);
            dl_replay_draw_glyph_at_offset(tile_surface, &item->draw_glyph, tile_x, tile_y);
#ifndef NDEBUG
            items_drawn++;
#endif
            break;
        }

        case DL_FILL_SURFACE_RECT: {
            rdt_vector_flush_batch(vec);
            DlFillSurfaceRect* r = &item->fill_surface_rect;
            dl_replay_fill_surface_rect_at_offset(tile_surface, scratch, r, tile_x, tile_y);
#ifndef NDEBUG
            items_drawn++;
#endif
            break;
        }

        case DL_BLIT_SURFACE_SCALED: {
            rdt_vector_flush_batch(vec);
            DlBlitSurfaceScaled* r = &item->blit_surface_scaled;
            dl_replay_blit_surface_scaled_at_offset(tile_surface, scratch, r, tile_x, tile_y);
#ifndef NDEBUG
            items_drawn++;
#endif
            break;
        }

        case DL_COMPOSITE_OPACITY: {
            rdt_vector_flush_batch(vec);
            DlCompositeOpacity* r = &item->composite_opacity;
            dl_replay_backdrop_composite_opacity(&backdrop_stack, tile_surface, scratch, r);
            break;
        }

        case DL_SAVE_BACKDROP: {
            rdt_vector_flush_batch(vec);
            DlSaveBackdrop* r = &item->save_backdrop;
            dl_replay_backdrop_save_at_offset(&backdrop_stack, tile_surface, scratch,
                                              r, tile_x, tile_y);
            break;
        }

        case DL_APPLY_BLEND_MODE: {
            rdt_vector_flush_batch(vec);
            DlApplyBlendMode* r = &item->apply_blend_mode;
            dl_replay_backdrop_apply_blend_mode(&backdrop_stack, tile_surface, scratch, r);
            break;
        }

        case DL_APPLY_FILTER: {
            rdt_vector_flush_batch(vec);
            DlApplyFilter* r = &item->apply_filter;
            dl_replay_apply_filter_at_offset(scratch, tile_surface, caps, r, tile_x, tile_y);
            break;
        }

        case DL_BOX_BLUR_REGION: {
            rdt_vector_flush_batch(vec);
            DlBoxBlurRegion* r = &item->box_blur_region;
            dl_replay_box_blur_region_at_offset(scratch, tile_surface, r, tile_x, tile_y);
            break;
        }

        case DL_BOX_BLUR_INSET: {
            rdt_vector_flush_batch(vec);
            DlBoxBlurInset* r = &item->box_blur_inset;
            dl_replay_box_blur_inset_at_offset(scratch, tile_surface, r, tile_x, tile_y);
            break;
        }

        case DL_SHADOW_CLIP_SAVE: {
            rdt_vector_flush_batch(vec);
            DlShadowClipSave* r = &item->shadow_clip_save;
            dl_replay_shadow_clip_save_at_offset(&shadow_clip, tile_surface, scratch,
                                                 r, tile_x, tile_y);
            break;
        }

        case DL_SHADOW_CLIP_RESTORE: {
            rdt_vector_flush_batch(vec);
            DlShadowClipRestore* r = &item->shadow_clip_restore;
            dl_replay_shadow_clip_restore_at_offset(&shadow_clip, tile_surface,
                                                    r, tile_x, tile_y);
            break;
        }

        case DL_OUTER_SHADOW: {
            rdt_vector_flush_batch(vec);
            DlOuterShadow* o = &item->outer_shadow;
            dl_replay_outer_shadow_at_offset(scratch, tile_surface, o, tile_x, tile_y);
            break;
        }

        case DL_BEGIN_ELEMENT:
        case DL_END_ELEMENT:
            break;

        case DL_VIDEO_PLACEHOLDER:
            // no-op during tile replay; video frames are blitted post-composite
            break;

        case DL_WEBVIEW_LAYER_PLACEHOLDER: {
            rdt_vector_flush_batch(vec);
            DlWebviewLayerPlaceholder* r = &item->webview_layer_placeholder;
            dl_replay_webview_layer_placeholder_at_offset(tile_surface, r, tile_x, tile_y);
#ifndef NDEBUG
            ImageSurface* src = (ImageSurface*)r->surface;
            if (src && src->pixels) items_drawn++;
#endif
            break;
        }
        }
    }

    rdt_vector_end_batch(vec);

    int backdrop_depth = dl_replay_backdrop_depth(&backdrop_stack);
    if (backdrop_depth > 0) {
        log_error("[DL_REPLAY_TILE] unbalanced backdrop stack: %d entries left", backdrop_depth);
    }

    log_debug("[DL_REPLAY_TILE] tile(%d,%d) %d/%d items drawn",
              (int)(tile_x / tile_w), (int)(tile_y / tile_h), items_drawn, dl->count);
}
