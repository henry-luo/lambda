// ==========================================================================
// TilePool — Tile grid, worker pool, and tile-aware display list replay.
//
// Phase 2 of the multi-threaded rendering proposal.
// ==========================================================================

#include "tile_pool.h"
#include "display_list_bounds.hpp"
#include "display_list_surface_region.hpp"
#include "render_filter.hpp"
#include "render_background.hpp"
#include "render_composite.hpp"
#include "render_raster.hpp"
#include "glyph_sampling.hpp"
#include "clip_shape.h"
#include "../lib/log.h"
#include "../lib/mem_factory.h"
#include "../lib/mem.h"
#include "../lib/memtrack.h"
#include <string.h>
#include <math.h>
#include <algorithm>
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

static void tile_offset_clip_shape(ClipShape* cs, float tile_x, float tile_y) {
    if (!cs) return;
    switch (cs->type) {
        case CLIP_SHAPE_CIRCLE:
            cs->circle.cx -= tile_x; cs->circle.cy -= tile_y;
            break;
        case CLIP_SHAPE_ELLIPSE:
            cs->ellipse.cx -= tile_x; cs->ellipse.cy -= tile_y;
            break;
        case CLIP_SHAPE_INSET:
            cs->inset.x -= tile_x; cs->inset.y -= tile_y;
            break;
        case CLIP_SHAPE_ROUNDED_RECT:
            cs->rounded_rect.x -= tile_x; cs->rounded_rect.y -= tile_y;
            break;
        default:
            break;
    }
}

static int dl_restore_raster_clip_shapes(const DlClipShapeStack* src, ClipShape* shapes,
                                         ClipShape** shape_ptrs, ScratchArena* scratch,
                                         float tile_x, float tile_y) {
    if (!src || !shapes || !shape_ptrs || src->depth <= 0) return 0;
    int depth = src->depth;
    if (depth > RDT_MAX_CLIP_SHAPES) {
        log_warn("[RAD_CAP_TILE_CLIP_RESTORE] truncating raster clip stack from %d to %d shapes",
                 depth, RDT_MAX_CLIP_SHAPES);
        depth = RDT_MAX_CLIP_SHAPES;
    }
    int out_depth = 0;
    for (int i = 0; i < depth; i++) {
        if (src->type[i] == CLIP_SHAPE_NONE) continue;
        if (src->type[i] == CLIP_SHAPE_POLYGON) {
            int count = src->polygon_count[i];
            if (!scratch || count < 3 || !src->polygon_vx[i] || !src->polygon_vy[i]) continue;
            float* vx = (float*)scratch_alloc(scratch, count * sizeof(float));
            float* vy = (float*)scratch_alloc(scratch, count * sizeof(float));
            if (!vx || !vy) continue;
            for (int pi = 0; pi < count; pi++) {
                vx[pi] = src->polygon_vx[i][pi] - tile_x;
                vy[pi] = src->polygon_vy[i][pi] - tile_y;
            }
            shapes[out_depth].type = CLIP_SHAPE_POLYGON;
            shapes[out_depth].polygon = {vx, vy, count};
        } else {
            shapes[out_depth] = clip_shape_from_params(src->type[i], src->params[i]);
            tile_offset_clip_shape(&shapes[out_depth], tile_x, tile_y);
        }
        shape_ptrs[out_depth] = &shapes[out_depth];
        out_depth++;
    }
    return out_depth;
}

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
            tile->pixel_w = std::min(tile_px, surface_w - c * tile_px);
            tile->pixel_h = std::min(tile_px, surface_h - r * tile_px);
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

// Helper: translate a glyph to tile-local coordinates and render
static void replay_tile_glyph(ImageSurface* tile_surface,
                               DlDrawGlyph* g, float tile_x, float tile_y) {
    GlyphBitmap* bitmap = &g->bitmap;

    if (g->has_transform && !g->is_color_emoji) {
        glyph_draw_transformed_coverage_bitmap(tile_surface, bitmap, g->x, g->y,
            &g->clip, g->color, &g->transform, tile_x, tile_y);
        return;
    }

    int x = g->x - (int)tile_x;
    int y = g->y - (int)tile_y;
    Color color = g->color;

    // tile-local clip
    Bound clip;
    clip.left   = g->clip.left   - tile_x;
    clip.top    = g->clip.top    - tile_y;
    clip.right  = g->clip.right  - tile_x;
    clip.bottom = g->clip.bottom - tile_y;
    // clamp to tile
    if (clip.left < 0) clip.left = 0;
    if (clip.top  < 0) clip.top  = 0;
    if (clip.right  > tile_surface->width)  clip.right  = (float)tile_surface->width;
    if (clip.bottom > tile_surface->height) clip.bottom = (float)tile_surface->height;

    if (g->is_color_emoji) {
        glyph_draw_color_bgra_bitmap(tile_surface, bitmap, x, y, &clip);
        return;
    }

    glyph_draw_coverage_bitmap(tile_surface, bitmap, x, y, &clip, color);
}

void dl_replay_tile(DisplayList* dl, RdtVector* vec,
                    ImageSurface* tile_surface, ScratchArena* scratch,
                    float tile_x, float tile_y, float tile_w, float tile_h,
                    float scale) {
    // Backdrop stack for mix-blend-mode pairs within this tile
    #define DL_MAX_BACKDROP_DEPTH 16
    uint32_t* backdrop_stack[DL_MAX_BACKDROP_DEPTH];
    int backdrop_region[DL_MAX_BACKDROP_DEPTH][4];  // x0, y0, w, h (tile-local)
    int backdrop_sp = 0;

    // Shadow clip save buffer for DL_SHADOW_CLIP_SAVE / DL_SHADOW_CLIP_RESTORE pairs
    uint32_t* shadow_clip_saved = nullptr;
    int shadow_clip_region[4] = {};  // x0, y0, w, h (tile-local, clamped)

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
            // Still need to track clip/backdrop depth for correctness
            switch (item->op) {
                case DL_PUSH_CLIP:
                    rdt_push_clip(vec, item->push_clip.path,
                                  item->push_clip.has_transform ? &item->push_clip.transform : nullptr);
                    break;
                case DL_POP_CLIP:
                    rdt_pop_clip(vec);
                    break;
                case DL_SAVE_BACKDROP: {
                    // push a null backdrop so apply_blend_mode can pop correctly
                    if (backdrop_sp < DL_MAX_BACKDROP_DEPTH) {
                        backdrop_stack[backdrop_sp] = nullptr;
                        backdrop_region[backdrop_sp][0] = 0;
                        backdrop_region[backdrop_sp][1] = 0;
                        backdrop_region[backdrop_sp][2] = 0;
                        backdrop_region[backdrop_sp][3] = 0;
                        backdrop_sp++;
                    }
                    break;
                }
                case DL_APPLY_BLEND_MODE: {
                    if (backdrop_sp > 0) backdrop_sp--;
                    break;
                }
                case DL_COMPOSITE_OPACITY: {
                    // pop backdrop to keep stack balanced (same as DL_APPLY_BLEND_MODE)
                    if (backdrop_sp > 0) backdrop_sp--;
                    break;
                }
                case DL_SHADOW_CLIP_SAVE:
                    shadow_clip_saved = nullptr;
                    break;
                case DL_SHADOW_CLIP_RESTORE:
                    shadow_clip_saved = nullptr;
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

        switch (item->op) {

        // -- ThorVG vector operations: tile offset handled by scene wrapper --
        // No manual X/Y offset needed; rdt_vector_set_tile_offset_{x,y} handles it.

        case DL_FILL_RECT: {
            DlFillRect* r = &item->fill_rect;
            rdt_fill_rect(vec, r->x, r->y, r->w, r->h, r->color);
#ifndef NDEBUG
            items_drawn++;
#endif
            break;
        }

        case DL_FILL_ROUNDED_RECT: {
            DlFillRoundedRect* r = &item->fill_rounded_rect;
            rdt_fill_rounded_rect(vec, r->x, r->y, r->w, r->h, r->rx, r->ry, r->color);
#ifndef NDEBUG
            items_drawn++;
#endif
            break;
        }

        case DL_FILL_PATH: {
            DlFillPath* r = &item->fill_path;
            rdt_fill_path(vec, r->path, r->color, r->rule,
                          r->has_transform ? &r->transform : nullptr);
#ifndef NDEBUG
            items_drawn++;
#endif
            break;
        }

        case DL_STROKE_PATH: {
            DlStrokePath* r = &item->stroke_path;
            rdt_stroke_path(vec, r->path, r->color, r->width, r->cap, r->join,
                            r->dash_array, r->dash_count, r->dash_phase,
                            r->has_transform ? &r->transform : nullptr);
#ifndef NDEBUG
            items_drawn++;
#endif
            break;
        }

        case DL_FILL_LINEAR_GRADIENT: {
            DlFillLinearGradient* r = &item->fill_linear_gradient;
            rdt_fill_linear_gradient(vec, r->path, r->x1, r->y1, r->x2, r->y2,
                                     r->stops, r->stop_count, r->rule,
                                     r->has_transform ? &r->transform : nullptr,
                                     r->has_gradient_transform ? &r->gradient_transform : nullptr);
#ifndef NDEBUG
            items_drawn++;
#endif
            break;
        }

        case DL_FILL_RADIAL_GRADIENT: {
            DlFillRadialGradient* r = &item->fill_radial_gradient;
            rdt_fill_radial_gradient(vec, r->path, r->cx, r->cy, r->r,
                                     r->stops, r->stop_count, r->rule,
                                     r->has_transform ? &r->transform : nullptr,
                                     r->has_gradient_transform ? &r->gradient_transform : nullptr);
#ifndef NDEBUG
            items_drawn++;
#endif
            break;
        }

        case DL_DRAW_IMAGE: {
            DlDrawImage* r = &item->draw_image;
            rdt_draw_image(vec, r->pixels, r->src_w, r->src_h, r->src_stride,
                           r->dst_x, r->dst_y, r->dst_w, r->dst_h, r->opacity,
                           r->has_transform ? &r->transform : nullptr,
                           r->resource_generation);
#ifndef NDEBUG
            items_drawn++;
#endif
            break;
        }

        // -- Direct-pixel operations: manual tile-local coordinate translation --

        case DL_DRAW_GLYPH: {
            rdt_vector_flush_batch(vec);
            replay_tile_glyph(tile_surface, &item->draw_glyph, tile_x, tile_y);
#ifndef NDEBUG
            items_drawn++;
#endif
            break;
        }

        case DL_DRAW_PICTURE: {
            DlDrawPicture* r = &item->draw_picture;
            // use dup version: duplicates the ThorVG paint so the original stays
            // intact for other tiles (rdt_picture_draw would consume it)
            rdt_picture_draw_dup(vec, r->picture, r->opacity,
                                 r->has_transform ? &r->transform : nullptr);
#ifndef NDEBUG
            items_drawn++;
#endif
            break;
        }

        case DL_PUSH_CLIP: {
            DlPushClip* r = &item->push_clip;
            rdt_push_clip(vec, r->path,
                          r->has_transform ? &r->transform : nullptr);
            break;
        }

        case DL_POP_CLIP: {
            rdt_pop_clip(vec);
            break;
        }

        case DL_FILL_SURFACE_RECT: {
            rdt_vector_flush_batch(vec);
            DlFillSurfaceRect* r = &item->fill_surface_rect;
            // translate to tile-local coordinates
            Rect rect = {r->x - tile_x, r->y - tile_y, r->w, r->h};
            Bound bound;
            bound.left   = std::max(0.0f, r->clip.left   - tile_x);
            bound.top    = std::max(0.0f, r->clip.top    - tile_y);
            bound.right  = std::min((float)tile_surface->width,  r->clip.right  - tile_x);
            bound.bottom = std::min((float)tile_surface->height, r->clip.bottom - tile_y);
            ClipShape shapes[RDT_MAX_CLIP_SHAPES];
            ClipShape* shape_ptrs[RDT_MAX_CLIP_SHAPES];
            ScratchMark clip_mark = scratch_mark(scratch);
            int clip_depth = dl_restore_raster_clip_shapes(&r->clip_shapes, shapes, shape_ptrs,
                                                           scratch, tile_x, tile_y);
            RasterPaintContext raster = {tile_surface, &bound, shape_ptrs, clip_depth};
            raster_fill_rect(&raster, &rect, r->color);
            scratch_restore(scratch, clip_mark);
#ifndef NDEBUG
            items_drawn++;
#endif
            break;
        }

        case DL_BLIT_SURFACE_SCALED: {
            rdt_vector_flush_batch(vec);
            DlBlitSurfaceScaled* r = &item->blit_surface_scaled;
            Rect dst_rect = {r->dst_x - tile_x, r->dst_y - tile_y, r->dst_w, r->dst_h};
            Bound bound;
            bound.left   = std::max(0.0f, r->clip.left   - tile_x);
            bound.top    = std::max(0.0f, r->clip.top    - tile_y);
            bound.right  = std::min((float)tile_surface->width,  r->clip.right  - tile_x);
            bound.bottom = std::min((float)tile_surface->height, r->clip.bottom - tile_y);
            ClipShape shapes[RDT_MAX_CLIP_SHAPES];
            ClipShape* shape_ptrs[RDT_MAX_CLIP_SHAPES];
            ScratchMark clip_mark = scratch_mark(scratch);
            int clip_depth = dl_restore_raster_clip_shapes(&r->clip_shapes, shapes, shape_ptrs,
                                                           scratch, tile_x, tile_y);
            RasterPaintContext raster = {tile_surface, &bound, shape_ptrs, clip_depth};
            raster_blit_surface_scaled(&raster, (ImageSurface*)r->src_surface, nullptr,
                                       &dst_rect, (ScaleMode)r->scale_mode, r->opacity);
            scratch_restore(scratch, clip_mark);
#ifndef NDEBUG
            items_drawn++;
#endif
            break;
        }

        case DL_COMPOSITE_OPACITY: {
            rdt_vector_flush_batch(vec);
            DlCompositeOpacity* r = &item->composite_opacity;
            if (backdrop_sp > 0) {
                backdrop_sp--;
                uint32_t* backdrop = backdrop_stack[backdrop_sp];
                if (backdrop) {
                    int bx = backdrop_region[backdrop_sp][0];
                    int by = backdrop_region[backdrop_sp][1];
                    int bw = backdrop_region[backdrop_sp][2];
                    int bh = backdrop_region[backdrop_sp][3];
                    if (r->premultiplied_source && r->opacity >= 0.999f) {
                        render_composite_source_over_premul(tile_surface, backdrop,
                                                            bx, by, bw, bh);
                    } else {
                        render_composite_opacity(tile_surface, backdrop, bx, by, bw, bh,
                                                 r->opacity);
                    }
                    scratch_free(scratch, backdrop);
                }
            }
            break;
        }

        case DL_SAVE_BACKDROP: {
            rdt_vector_flush_batch(vec);
            DlSaveBackdrop* r = &item->save_backdrop;
            if (backdrop_sp < DL_MAX_BACKDROP_DEPTH) {
                // translate to tile-local
                int region[4] = {};
                uint32_t* buf = surface_region_save(tile_surface, scratch,
                                                    r->x0 - (int)tile_x,
                                                    r->y0 - (int)tile_y,
                                                    r->w, r->h,
                                                    region);
                if (buf) {
                    surface_region_clear(tile_surface, region);
                    backdrop_stack[backdrop_sp] = buf;
                    memcpy(backdrop_region[backdrop_sp], region, sizeof(region));
                } else {
                    backdrop_stack[backdrop_sp] = nullptr;
                    memcpy(backdrop_region[backdrop_sp], region, sizeof(region));
                }
                backdrop_sp++;
            }
            break;
        }

        case DL_APPLY_BLEND_MODE: {
            rdt_vector_flush_batch(vec);
            DlApplyBlendMode* r = &item->apply_blend_mode;
            if (backdrop_sp > 0) {
                backdrop_sp--;
                uint32_t* backdrop = backdrop_stack[backdrop_sp];
                if (backdrop) {
                    int bx = backdrop_region[backdrop_sp][0];
                    int by = backdrop_region[backdrop_sp][1];
                    int bw = backdrop_region[backdrop_sp][2];
                    int bh = backdrop_region[backdrop_sp][3];
                    uint32_t* px = (uint32_t*)tile_surface->pixels;
                    int pitch = tile_surface->pitch / 4;
                    for (int row = 0; row < bh; row++) {
                        for (int col = 0; col < bw; col++) {
                            uint32_t bd = backdrop[row * bw + col];
                            uint32_t source = px[(by + row) * pitch + (bx + col)];
                            px[(by + row) * pitch + (bx + col)] =
                                render_composite_blend_pixel(bd, source, (CssEnum)r->blend_mode);
                        }
                    }
                    scratch_free(scratch, backdrop);
                }
            }
            break;
        }

        case DL_APPLY_FILTER: {
            rdt_vector_flush_batch(vec);
            DlApplyFilter* r = &item->apply_filter;
            Rect rect = {r->x - tile_x, r->y - tile_y, r->w, r->h};
            Bound bound;
            bound.left   = std::max(0.0f, r->clip.left   - tile_x);
            bound.top    = std::max(0.0f, r->clip.top    - tile_y);
            bound.right  = std::min((float)tile_surface->width,  r->clip.right  - tile_x);
            bound.bottom = std::min((float)tile_surface->height, r->clip.bottom - tile_y);
            render_filter_apply_with_backend(caps, scratch, tile_surface,
                                             (FilterProp*)r->filter, &rect, &bound);
            break;
        }

        case DL_BOX_BLUR_REGION: {
            rdt_vector_flush_batch(vec);
            DlBoxBlurRegion* r = &item->box_blur_region;
            // adjust coordinates relative to tile origin
            int rx = r->rx - (int)tile_x;
            int ry = r->ry - (int)tile_y;
            if (r->premultiply_source) {
                premultiply_surface_region(tile_surface, rx, ry, r->rw, r->rh);
            }
            if (r->tint_source) {
                tint_premultiplied_surface_region(tile_surface, rx, ry,
                                                  r->rw, r->rh, r->tint_color);
            }
            if (r->clip_type && tile_surface && tile_surface->pixels) {
                int region[4] = {};
                uint32_t* saved = surface_region_save(tile_surface, scratch,
                                                      rx, ry, r->rw, r->rh,
                                                      region);
                box_blur_region(scratch, tile_surface, rx, ry, r->rw, r->rh, r->blur_radius);
                if (saved) {
                    // reconstruct clip shape with tile-relative coordinates
                    float adj_params[8];
                    memcpy(adj_params, r->clip_params, 8 * sizeof(float));
                    switch ((ClipShapeType)r->clip_type) {
                        case CLIP_SHAPE_CIRCLE:
                        case CLIP_SHAPE_ELLIPSE:
                            adj_params[0] -= tile_x; adj_params[1] -= tile_y;
                            break;
                        case CLIP_SHAPE_INSET:
                        case CLIP_SHAPE_ROUNDED_RECT:
                            adj_params[0] -= tile_x; adj_params[1] -= tile_y;
                            break;
                        default: break;
                    }
                    ClipShape cs = clip_shape_from_params(r->clip_type, adj_params);
                    surface_region_restore_masked(tile_surface, saved, region, &cs, false);
                }
            } else {
                box_blur_region(scratch, tile_surface, rx, ry, r->rw, r->rh, r->blur_radius);
            }
            break;
        }

        case DL_BOX_BLUR_INSET: {
            rdt_vector_flush_batch(vec);
            DlBoxBlurInset* r = &item->box_blur_inset;
            int rx = r->rx - (int)tile_x;
            int ry = r->ry - (int)tile_y;
            box_blur_region_inset(scratch, tile_surface, rx, ry, r->rw, r->rh,
                                  r->pad, r->blur_radius, r->bg_color);
            break;
        }

        case DL_SHADOW_CLIP_SAVE: {
            rdt_vector_flush_batch(vec);
            DlShadowClipSave* r = &item->shadow_clip_save;
            shadow_clip_saved = nullptr;
            if (tile_surface && tile_surface->pixels) {
                int rx = r->rx - (int)tile_x;
                int ry = r->ry - (int)tile_y;
                shadow_clip_saved = surface_region_save(tile_surface, scratch,
                                                        rx, ry, r->rw, r->rh,
                                                        shadow_clip_region);
            }
            break;
        }

        case DL_SHADOW_CLIP_RESTORE: {
            rdt_vector_flush_batch(vec);
            DlShadowClipRestore* r = &item->shadow_clip_restore;
            if (shadow_clip_saved && tile_surface && tile_surface->pixels && r->exclude_type) {
                // reconstruct exclude shape with tile-relative coordinates
                float adj_params[8];
                memcpy(adj_params, r->exclude_params, 8 * sizeof(float));
                switch ((ClipShapeType)r->exclude_type) {
                    case CLIP_SHAPE_CIRCLE:
                    case CLIP_SHAPE_ELLIPSE:
                        adj_params[0] -= tile_x; adj_params[1] -= tile_y;
                        break;
                    case CLIP_SHAPE_INSET:
                    case CLIP_SHAPE_ROUNDED_RECT:
                        adj_params[0] -= tile_x; adj_params[1] -= tile_y;
                        break;
                    default: break;
                }
                ClipShape ex = clip_shape_from_params(r->exclude_type, adj_params);
                surface_region_restore_masked(tile_surface, shadow_clip_saved,
                                              shadow_clip_region, &ex,
                                              r->restore_inside);
            }
            shadow_clip_saved = nullptr;
            break;
        }

        case DL_OUTER_SHADOW: {
            rdt_vector_flush_batch(vec);
            DlOuterShadow* o = &item->outer_shadow;
            // adjust shadow rect to tile-local coords
            float sx = o->shadow_x - tile_x;
            float sy = o->shadow_y - tile_y;
            // adjust exclude shape (rounded rect or inset use x/y at indices 0/1)
            int ex_type = o->exclude_type;
            float ex_params[8];
            memcpy(ex_params, o->exclude_params, 8 * sizeof(float));
            switch ((ClipShapeType)ex_type) {
                case CLIP_SHAPE_CIRCLE:
                case CLIP_SHAPE_ELLIPSE:
                case CLIP_SHAPE_INSET:
                case CLIP_SHAPE_ROUNDED_RECT:
                    ex_params[0] -= tile_x; ex_params[1] -= tile_y;
                    break;
                default: break;
            }
            int cl_type = o->clip_type;
            float cl_params[8];
            memcpy(cl_params, o->clip_params, 8 * sizeof(float));
            switch ((ClipShapeType)cl_type) {
                case CLIP_SHAPE_CIRCLE:
                case CLIP_SHAPE_ELLIPSE:
                case CLIP_SHAPE_INSET:
                case CLIP_SHAPE_ROUNDED_RECT:
                    cl_params[0] -= tile_x; cl_params[1] -= tile_y;
                    break;
                default: break;
            }
            render_outer_shadow_blur_composite(
                scratch, tile_surface,
                sx, sy, o->shadow_w, o->shadow_h,
                o->sr_tl, o->sr_tr, o->sr_br, o->sr_bl,
                o->color, o->blur_radius,
                ex_type, ex_params,
                cl_type, cl_params);
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
            ImageSurface* src = (ImageSurface*)r->surface;
            if (src && src->pixels) {
                Rect dst_rect = {r->dst_x - tile_x, r->dst_y - tile_y, r->dst_w, r->dst_h};
                Bound bound;
                bound.left   = std::max(0.0f, r->clip.left   - tile_x);
                bound.top    = std::max(0.0f, r->clip.top    - tile_y);
                bound.right  = std::min((float)tile_surface->width,  r->clip.right  - tile_x);
                bound.bottom = std::min((float)tile_surface->height, r->clip.bottom - tile_y);
                RasterPaintContext raster = {tile_surface, &bound, nullptr, 0};
                raster_blit_surface_scaled(&raster, src, nullptr, &dst_rect, SCALE_MODE_LINEAR);
#ifndef NDEBUG
                items_drawn++;
#endif
            }
            break;
        }
        }
    }

    rdt_vector_end_batch(vec);

    if (backdrop_sp > 0) {
        log_error("[DL_REPLAY_TILE] unbalanced backdrop stack: %d entries left", backdrop_sp);
    }

    log_debug("[DL_REPLAY_TILE] tile(%d,%d) %d/%d items drawn",
              (int)(tile_x / tile_w), (int)(tile_y / tile_h), items_drawn, dl->count);
}
