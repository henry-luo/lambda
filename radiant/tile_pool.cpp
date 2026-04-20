// ==========================================================================
// TilePool — Tile grid, worker pool, and tile-aware display list replay.
//
// Phase 2 of the multi-threaded rendering proposal.
// ==========================================================================

#include "tile_pool.h"
#include "render_filter.hpp"
#include "render_background.hpp"
#include "clip_shape.h"
#include "../lib/log.h"
#include "../lib/mem.h"
#include "../lib/memtrack.h"
#include <string.h>
#include <math.h>
#include <algorithm>
#include <chrono>
#include <unistd.h>

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
    // create a thread-local ThorVG canvas (small initial size; re-bound per tile)
    rdt_vector_init(&tl_worker.vec, tile->pixels, tile->pixel_w, tile->pixel_h, tile->stride);
    // create a thread-local backing arena + scratch arena
    tl_worker.pool = pool_create();
    tl_worker.arena = arena_create_default(tl_worker.pool);
    scratch_init(&tl_worker.scratch, tl_worker.arena);
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
        threads = (int)sysconf(_SC_NPROCESSORS_ONLN);
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

// Check if an item's bounds intersect a tile region (all in physical pixels)
static inline bool bounds_intersect(const float item_bounds[4],
                                     float tx, float ty, float tw, float th) {
    float ix = item_bounds[0], iy = item_bounds[1];
    float iw = item_bounds[2], ih = item_bounds[3];
    // items with zero bounds (clips, depth markers) always pass
    if (iw <= 0 && ih <= 0) return true;
    return !(ix >= tx + tw || ix + iw <= tx ||
             iy >= ty + th || iy + ih <= ty);
}

// Helper: translate a glyph to tile-local coordinates and render
static void replay_tile_glyph(ImageSurface* tile_surface,
                               DlDrawGlyph* g, float tile_x, float tile_y) {
    GlyphBitmap* bitmap = &g->bitmap;
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
        float bscale = bitmap->bitmap_scale;
        if (bscale <= 0.0f) bscale = 1.0f;
        int target_w = (int)(bitmap->width  * bscale + 0.5f);
        int target_h = (int)(bitmap->height * bscale + 0.5f);
        if (target_w <= 0 || target_h <= 0) return;

        int left   = std::max((int)clip.left,  x);
        int right  = std::min((int)clip.right,  x + target_w);
        int top    = std::max((int)clip.top,    y);
        int bottom = std::min((int)clip.bottom, y + target_h);
        if (left >= right || top >= bottom) return;

        float inv_scale = 1.0f / bscale;
        for (int dy = top - y; dy < bottom - y; dy++) {
            uint8_t* row_pixels = (uint8_t*)tile_surface->pixels + (y + dy) * tile_surface->pitch;
            float src_y = dy * inv_scale;
            int sy0 = (int)src_y;
            int sy1 = sy0 + 1;
            float fy = src_y - sy0;
            if (sy0 >= (int)bitmap->height) sy0 = bitmap->height - 1;
            if (sy1 >= (int)bitmap->height) sy1 = bitmap->height - 1;

            for (int dx = left - x; dx < right - x; dx++) {
                if (x + dx < 0 || x + dx >= tile_surface->width) continue;
                float src_x = dx * inv_scale;
                int sx0 = (int)src_x;
                int sx1 = sx0 + 1;
                float fx = src_x - sx0;
                if (sx0 >= (int)bitmap->width) sx0 = bitmap->width - 1;
                if (sx1 >= (int)bitmap->width) sx1 = bitmap->width - 1;

                uint8_t* s00 = bitmap->buffer + sy0 * bitmap->pitch + sx0 * 4;
                uint8_t* s10 = bitmap->buffer + sy0 * bitmap->pitch + sx1 * 4;
                uint8_t* s01 = bitmap->buffer + sy1 * bitmap->pitch + sx0 * 4;
                uint8_t* s11 = bitmap->buffer + sy1 * bitmap->pitch + sx1 * 4;

                float w00 = (1 - fx) * (1 - fy);
                float w10 = fx * (1 - fy);
                float w01 = (1 - fx) * fy;
                float w11 = fx * fy;

                uint8_t src_b = (uint8_t)(s00[0]*w00 + s10[0]*w10 + s01[0]*w01 + s11[0]*w11 + 0.5f);
                uint8_t src_g = (uint8_t)(s00[1]*w00 + s10[1]*w10 + s01[1]*w01 + s11[1]*w11 + 0.5f);
                uint8_t src_r = (uint8_t)(s00[2]*w00 + s10[2]*w10 + s01[2]*w01 + s11[2]*w11 + 0.5f);
                uint8_t src_a = (uint8_t)(s00[3]*w00 + s10[3]*w10 + s01[3]*w01 + s11[3]*w11 + 0.5f);

                if (src_a > 0) {
                    uint8_t* dst = (uint8_t*)(row_pixels + (x + dx) * 4);
                    if (src_a == 255) {
                        dst[0] = src_r; dst[1] = src_g; dst[2] = src_b; dst[3] = 255;
                    } else {
                        uint32_t inv_alpha = 255 - src_a;
                        dst[0] = (dst[0] * inv_alpha + src_r * src_a) / 255;
                        dst[1] = (dst[1] * inv_alpha + src_g * src_a) / 255;
                        dst[2] = (dst[2] * inv_alpha + src_b * src_a) / 255;
                        dst[3] = 255;
                    }
                }
            }
        }
        return;
    }

    // grayscale / monochrome glyph
    int left   = std::max((int)clip.left,  x);
    int right  = std::min((int)clip.right,  x + (int)bitmap->width);
    int top    = std::max((int)clip.top,    y);
    int bottom = std::min((int)clip.bottom, y + (int)bitmap->height);
    if (left >= right || top >= bottom) return;

    bool is_mono = (bitmap->pixel_mode == GLYPH_PIXEL_MONO);

    for (int i = top - y; i < bottom - y; i++) {
        uint8_t* row_pixels = (uint8_t*)tile_surface->pixels + (y + i) * tile_surface->pitch;
        for (int j = left - x; j < right - x; j++) {
            if (x + j < 0 || x + j >= tile_surface->width) continue;

            uint32_t intensity;
            if (is_mono) {
                int byte_index = j / 8;
                int bit_index = 7 - (j % 8);
                uint8_t byte_val = bitmap->buffer[i * bitmap->pitch + byte_index];
                intensity = (byte_val & (1 << bit_index)) ? 255 : 0;
            } else {
                intensity = bitmap->buffer[i * bitmap->pitch + j];
            }

            if (intensity > 0) {
                uint8_t* p = (uint8_t*)(row_pixels + (x + j) * 4);
                uint32_t v = 255 - intensity;
                if (color.c == 0xFF000000) {
                    p[0] = p[0] * v / 255;
                    p[1] = p[1] * v / 255;
                    p[2] = p[2] * v / 255;
                    p[3] = 0xFF;
                } else {
                    p[0] = (p[0] * v + color.r * intensity) / 255;
                    p[1] = (p[1] * v + color.g * intensity) / 255;
                    p[2] = (p[2] * v + color.b * intensity) / 255;
                    p[3] = 0xFF;
                }
            }
        }
    }
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

    // Per-tile clip depth save stack (avoids writing to shared display list)
    #define DL_MAX_CLIP_SAVE_DEPTH 32
    int clip_saved_depths[DL_MAX_CLIP_SAVE_DEPTH];
    int clip_save_sp = 0;

    // Tell ThorVG to translate all shapes from page-absolute to tile-local.
    // Both X and Y offsets are handled by the scene wrapper in tvg_push_draw_remove.
    rdt_vector_set_tile_offset_x(vec, tile_x);
    rdt_vector_set_tile_offset_y(vec, tile_y);

    int items_drawn = 0;

    for (int i = 0; i < dl->count; i++) {
        DisplayItem* item = &dl->items[i];

        // Cull: skip items that don't intersect this tile
        // Items with zero bounds (clips, markers) always pass
        if (!bounds_intersect(item->bounds, tile_x, tile_y, tile_w, tile_h)) {
            // Still need to track clip/backdrop depth for correctness
            switch (item->op) {
                case DL_PUSH_CLIP:
                    rdt_push_clip(vec, item->push_clip.path,
                                  item->push_clip.has_transform ? &item->push_clip.transform : nullptr);
                    break;
                case DL_POP_CLIP:
                    rdt_pop_clip(vec);
                    break;
                case DL_SAVE_CLIP_DEPTH:
                    if (clip_save_sp < DL_MAX_CLIP_SAVE_DEPTH) {
                        clip_saved_depths[clip_save_sp++] = rdt_clip_save_depth();
                    }
                    break;
                case DL_RESTORE_CLIP_DEPTH:
                    if (clip_save_sp > 0) {
                        rdt_clip_restore_depth(clip_saved_depths[--clip_save_sp]);
                    }
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
            items_drawn++;
            break;
        }

        case DL_FILL_ROUNDED_RECT: {
            DlFillRoundedRect* r = &item->fill_rounded_rect;
            rdt_fill_rounded_rect(vec, r->x, r->y, r->w, r->h, r->rx, r->ry, r->color);
            items_drawn++;
            break;
        }

        case DL_FILL_PATH: {
            DlFillPath* r = &item->fill_path;
            rdt_fill_path(vec, r->path, r->color, r->rule,
                          r->has_transform ? &r->transform : nullptr);
            items_drawn++;
            break;
        }

        case DL_STROKE_PATH: {
            DlStrokePath* r = &item->stroke_path;
            rdt_stroke_path(vec, r->path, r->color, r->width, r->cap, r->join,
                            r->dash_array, r->dash_count,
                            r->has_transform ? &r->transform : nullptr);
            items_drawn++;
            break;
        }

        case DL_FILL_LINEAR_GRADIENT: {
            DlFillLinearGradient* r = &item->fill_linear_gradient;
            rdt_fill_linear_gradient(vec, r->path, r->x1, r->y1, r->x2, r->y2,
                                     r->stops, r->stop_count, r->rule,
                                     r->has_transform ? &r->transform : nullptr);
            items_drawn++;
            break;
        }

        case DL_FILL_RADIAL_GRADIENT: {
            DlFillRadialGradient* r = &item->fill_radial_gradient;
            rdt_fill_radial_gradient(vec, r->path, r->cx, r->cy, r->r,
                                     r->stops, r->stop_count, r->rule,
                                     r->has_transform ? &r->transform : nullptr);
            items_drawn++;
            break;
        }

        case DL_DRAW_IMAGE: {
            DlDrawImage* r = &item->draw_image;
            rdt_draw_image(vec, r->pixels, r->src_w, r->src_h, r->src_stride,
                           r->dst_x, r->dst_y, r->dst_w, r->dst_h, r->opacity,
                           r->has_transform ? &r->transform : nullptr);
            items_drawn++;
            break;
        }

        // -- Direct-pixel operations: manual tile-local coordinate translation --

        case DL_DRAW_GLYPH: {
            replay_tile_glyph(tile_surface, &item->draw_glyph, tile_x, tile_y);
            items_drawn++;
            break;
        }

        case DL_DRAW_PICTURE: {
            DlDrawPicture* r = &item->draw_picture;
            // use dup version: duplicates the ThorVG paint so the original stays
            // intact for other tiles (rdt_picture_draw would consume it)
            rdt_picture_draw_dup(vec, r->picture, r->opacity,
                                 r->has_transform ? &r->transform : nullptr);
            items_drawn++;
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

        case DL_SAVE_CLIP_DEPTH: {
            if (clip_save_sp < DL_MAX_CLIP_SAVE_DEPTH) {
                clip_saved_depths[clip_save_sp++] = rdt_clip_save_depth();
            }
            break;
        }

        case DL_RESTORE_CLIP_DEPTH: {
            if (clip_save_sp > 0) {
                rdt_clip_restore_depth(clip_saved_depths[--clip_save_sp]);
            }
            break;
        }

        case DL_FILL_SURFACE_RECT: {
            DlFillSurfaceRect* r = &item->fill_surface_rect;
            // translate to tile-local coordinates
            Rect rect = {r->x - tile_x, r->y - tile_y, r->w, r->h};
            Bound bound;
            bound.left   = std::max(0.0f, r->clip.left   - tile_x);
            bound.top    = std::max(0.0f, r->clip.top    - tile_y);
            bound.right  = std::min((float)tile_surface->width,  r->clip.right  - tile_x);
            bound.bottom = std::min((float)tile_surface->height, r->clip.bottom - tile_y);
            fill_surface_rect(tile_surface, &rect, r->color, &bound);
            items_drawn++;
            break;
        }

        case DL_BLIT_SURFACE_SCALED: {
            DlBlitSurfaceScaled* r = &item->blit_surface_scaled;
            Rect dst_rect = {r->dst_x - tile_x, r->dst_y - tile_y, r->dst_w, r->dst_h};
            Bound bound;
            bound.left   = std::max(0.0f, r->clip.left   - tile_x);
            bound.top    = std::max(0.0f, r->clip.top    - tile_y);
            bound.right  = std::min((float)tile_surface->width,  r->clip.right  - tile_x);
            bound.bottom = std::min((float)tile_surface->height, r->clip.bottom - tile_y);
            blit_surface_scaled((ImageSurface*)r->src_surface, nullptr,
                                tile_surface, &dst_rect, &bound,
                                (ScaleMode)r->scale_mode, nullptr, 0);
            items_drawn++;
            break;
        }

        case DL_APPLY_OPACITY: {
            DlApplyOpacity* r = &item->apply_opacity;
            // translate to tile-local coordinates
            int x0 = r->x0 - (int)tile_x;
            int y0 = r->y0 - (int)tile_y;
            int x1 = r->x1 - (int)tile_x;
            int y1 = r->y1 - (int)tile_y;
            // clamp to tile
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > tile_surface->width)  x1 = tile_surface->width;
            if (y1 > tile_surface->height) y1 = tile_surface->height;
            if (x0 < x1 && y0 < y1) {
                for (int y = y0; y < y1; y++) {
                    uint8_t* row = (uint8_t*)tile_surface->pixels + y * tile_surface->pitch;
                    for (int x = x0; x < x1; x++) {
                        uint8_t* pixel = row + x * 4;
                        pixel[3] = (uint8_t)(pixel[3] * r->opacity + 0.5f);
                    }
                }
            }
            break;
        }

        case DL_COMPOSITE_OPACITY: {
            DlCompositeOpacity* r = &item->composite_opacity;
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
                    int opacity_i = (int)(r->opacity * 256 + 0.5f);
                    for (int row = 0; row < bh; row++) {
                        for (int col = 0; col < bw; col++) {
                            uint32_t src = px[(by + row) * pitch + (bx + col)];
                            uint32_t dst = backdrop[row * bw + col];
                            if (src == 0) {
                                px[(by + row) * pitch + (bx + col)] = dst;
                                continue;
                            }
                            uint32_t sa = (((src >> 24) & 0xFF) * opacity_i + 128) >> 8;
                            uint32_t sr = ((src & 0xFF) * opacity_i + 128) >> 8;
                            uint32_t sg = (((src >> 8) & 0xFF) * opacity_i + 128) >> 8;
                            uint32_t sb = (((src >> 16) & 0xFF) * opacity_i + 128) >> 8;
                            uint32_t inv_sa = 255 - sa;
                            uint32_t da = (dst >> 24) & 0xFF;
                            uint32_t dr = dst & 0xFF;
                            uint32_t dg = (dst >> 8) & 0xFF;
                            uint32_t db = (dst >> 16) & 0xFF;
                            uint32_t ra = sa + (da * inv_sa + 128) / 255;
                            uint32_t rr = sr + (dr * inv_sa + 128) / 255;
                            uint32_t rg = sg + (dg * inv_sa + 128) / 255;
                            uint32_t rb = sb + (db * inv_sa + 128) / 255;
                            if (ra > 255) ra = 255;
                            if (rr > 255) rr = 255;
                            if (rg > 255) rg = 255;
                            if (rb > 255) rb = 255;
                            px[(by + row) * pitch + (bx + col)] =
                                (ra << 24) | (rb << 16) | (rg << 8) | rr;
                        }
                    }
                    scratch_free(scratch, backdrop);
                }
            }
            break;
        }

        case DL_SAVE_BACKDROP: {
            DlSaveBackdrop* r = &item->save_backdrop;
            if (backdrop_sp < DL_MAX_BACKDROP_DEPTH) {
                // translate to tile-local
                int lx = r->x0 - (int)tile_x;
                int ly = r->y0 - (int)tile_y;
                int lw = r->w;
                int lh = r->h;
                // clamp to tile
                if (lx < 0) { lw += lx; lx = 0; }
                if (ly < 0) { lh += ly; ly = 0; }
                if (lx + lw > tile_surface->width)  lw = tile_surface->width - lx;
                if (ly + lh > tile_surface->height) lh = tile_surface->height - ly;

                if (lw > 0 && lh > 0) {
                    uint32_t* buf = (uint32_t*)scratch_alloc(scratch, lw * lh * sizeof(uint32_t));
                    uint32_t* px = (uint32_t*)tile_surface->pixels;
                    int pitch = tile_surface->pitch / 4;
                    for (int row = 0; row < lh; row++) {
                        memcpy(buf + row * lw,
                               px + (ly + row) * pitch + lx,
                               lw * sizeof(uint32_t));
                    }
                    for (int row = 0; row < lh; row++) {
                        memset(px + (ly + row) * pitch + lx, 0, lw * sizeof(uint32_t));
                    }
                    backdrop_stack[backdrop_sp] = buf;
                    backdrop_region[backdrop_sp][0] = lx;
                    backdrop_region[backdrop_sp][1] = ly;
                    backdrop_region[backdrop_sp][2] = lw;
                    backdrop_region[backdrop_sp][3] = lh;
                } else {
                    backdrop_stack[backdrop_sp] = nullptr;
                    backdrop_region[backdrop_sp][0] = 0;
                    backdrop_region[backdrop_sp][1] = 0;
                    backdrop_region[backdrop_sp][2] = 0;
                    backdrop_region[backdrop_sp][3] = 0;
                }
                backdrop_sp++;
            }
            break;
        }

        case DL_APPLY_BLEND_MODE: {
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
                                composite_blend_pixel(bd, source, (CssEnum)r->blend_mode);
                        }
                    }
                    scratch_free(scratch, backdrop);
                }
            }
            break;
        }

        case DL_APPLY_FILTER: {
            DlApplyFilter* r = &item->apply_filter;
            Rect rect = {r->x - tile_x, r->y - tile_y, r->w, r->h};
            Bound bound;
            bound.left   = std::max(0.0f, r->clip.left   - tile_x);
            bound.top    = std::max(0.0f, r->clip.top    - tile_y);
            bound.right  = std::min((float)tile_surface->width,  r->clip.right  - tile_x);
            bound.bottom = std::min((float)tile_surface->height, r->clip.bottom - tile_y);
            apply_css_filters(scratch, tile_surface, (FilterProp*)r->filter, &rect, &bound);
            break;
        }

        case DL_BEGIN_ELEMENT:
        case DL_END_ELEMENT:
            break;

        case DL_VIDEO_PLACEHOLDER:
            // no-op during tile replay; video frames are blitted post-composite
            break;

        case DL_WEBVIEW_LAYER_PLACEHOLDER: {
            DlWebviewLayerPlaceholder* r = &item->webview_layer_placeholder;
            ImageSurface* src = (ImageSurface*)r->surface;
            if (src && src->pixels) {
                Rect dst_rect = {r->dst_x - tile_x, r->dst_y - tile_y, r->dst_w, r->dst_h};
                Bound bound;
                bound.left   = std::max(0.0f, r->clip.left   - tile_x);
                bound.top    = std::max(0.0f, r->clip.top    - tile_y);
                bound.right  = std::min((float)tile_surface->width,  r->clip.right  - tile_x);
                bound.bottom = std::min((float)tile_surface->height, r->clip.bottom - tile_y);
                blit_surface_scaled(src, nullptr, tile_surface, &dst_rect, &bound,
                                    SCALE_MODE_LINEAR, nullptr, 0);
                items_drawn++;
            }
            break;
        }
        }
    }

    if (backdrop_sp > 0) {
        log_error("[DL_REPLAY_TILE] unbalanced backdrop stack: %d entries left", backdrop_sp);
    }

    log_debug("[DL_REPLAY_TILE] tile(%d,%d) %d/%d items drawn",
              (int)(tile_x / tile_w), (int)(tile_y / tile_h), items_drawn, dl->count);
}
