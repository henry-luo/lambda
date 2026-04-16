#pragma once
// ==========================================================================
// TilePool — Tile grid partitioning and parallel rasterization pool.
//
// Phase 2 of the multi-threaded rendering proposal.
// Divides the page surface into fixed-size tiles and dispatches display list
// replay to a worker pool for parallel CPU rasterization.
// ==========================================================================

#include "display_list.h"
#include "rdt_vector.hpp"
#include "view.hpp"             // ImageSurface, Bound
#include "../lib/scratch_arena.h"
#include "../lib/arena.h"
#include "../lib/mempool.h"
#include <pthread.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Tile size: 256 CSS pixels (512 physical at 2x).
// Chrome uses 256×256 — good balance of parallelism and overhead.
// ---------------------------------------------------------------------------

#define TILE_SIZE_CSS 256

// ---------------------------------------------------------------------------
// Tile — one rectangular region of the output surface
// ---------------------------------------------------------------------------

typedef struct Tile {
    int col, row;               // grid position
    float x, y, w, h;          // bounds in physical pixels (scaled)
    uint32_t* pixels;           // tile pixel buffer (owned, ABGR8888)
    int pixel_w, pixel_h;      // physical pixel dimensions
    int stride;                 // row stride in pixels (== pixel_w)
} Tile;

// ---------------------------------------------------------------------------
// TileGrid — the full grid of tiles covering the surface
// ---------------------------------------------------------------------------

typedef struct TileGrid {
    Tile* tiles;
    int cols, rows;
    int total;
    float scale;                // pixel_ratio (1.0, 2.0, etc.)
    int surface_w, surface_h;  // full surface dimensions in physical pixels
} TileGrid;

// Create a tile grid covering the given surface dimensions.
// scale = pixel_ratio (e.g. 2.0 for Retina).
void tile_grid_init(TileGrid* grid, int surface_w, int surface_h, float scale);

// Free all tile pixel buffers and the grid array.
void tile_grid_destroy(TileGrid* grid);

// Clear all tile pixels to a solid color (ABGR8888).
void tile_grid_clear(TileGrid* grid, uint32_t color);

// Composite all tiles into the final surface (memcpy rows).
void tile_grid_composite(TileGrid* grid, ImageSurface* surface);

// ---------------------------------------------------------------------------
// RenderPool — pthread-based worker pool for tile rasterization
// ---------------------------------------------------------------------------

typedef struct TileJob {
    Tile* tile;
    DisplayList* display_list;  // shared, read-only
    float scale;
    uint32_t bg_color;          // tile background clear color (ABGR8888)
} TileJob;

// Per-worker state (thread-local resources created once, reused across frames)
typedef struct WorkerState {
    RdtVector vec;              // thread-local ThorVG canvas
    ScratchArena scratch;       // thread-local scratch allocator
    Pool* pool;                 // thread-local memory pool (backing for arena)
    Arena* arena;               // thread-local arena (backing for scratch)
    bool initialized;
} WorkerState;

typedef struct RenderPool {
    pthread_t* threads;
    int thread_count;

    // job queue (simple array — all jobs submitted before workers start)
    TileJob* jobs;
    int job_count;

    // synchronisation: barrier-style — main thread signals start, waits for all done
    pthread_mutex_t mutex;
    pthread_cond_t  work_available;
    pthread_cond_t  all_done;
    int next_job;               // index into jobs array (guarded by mutex)
    int completed_jobs;
    bool shutdown;
} RenderPool;

// Create the pool with the given number of worker threads.
// threads=0 means auto-detect (use hardware_concurrency, cap at 8).
void render_pool_init(RenderPool* pool, int threads);

// Destroy the pool and join all threads.
void render_pool_destroy(RenderPool* pool);

// Submit a batch of tile jobs and wait for all to complete.
// The display list must be fully recorded and immutable.
void render_pool_dispatch(RenderPool* pool, TileJob* jobs, int count);

// ---------------------------------------------------------------------------
// Tile-aware replay — replays display list items intersecting a tile
// ---------------------------------------------------------------------------

// Replay the display list for a single tile.
// vec must be bound to the tile's pixel buffer.
// Coordinates are translated from page-absolute to tile-local.
void dl_replay_tile(DisplayList* dl, RdtVector* vec,
                    ImageSurface* tile_surface, ScratchArena* scratch,
                    float tile_x, float tile_y, float tile_w, float tile_h,
                    float scale);

#ifdef __cplusplus
}
#endif
