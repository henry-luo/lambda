/**
 * flex_grid_context.cpp - Implementation of FlexGridContext lifecycle functions
 */

#include "flex_grid_context.hpp"
#include "../lib/mempool.h"
#include "../lib/log.h"
#include <cstring>  // for memset

namespace radiant {

// Initial capacities for dynamic arrays
static const int32_t INITIAL_ITEM_CAPACITY = 16;
static const int32_t INITIAL_LINE_CAPACITY = 4;
static const int32_t INITIAL_TRACK_CAPACITY = 16;

// Helper: allocate and zero memory (pool_calloc takes pool + total_size)
static inline void* pool_calloc_array(Pool* pool, size_t count, size_t elem_size) {
    size_t total = count * elem_size;
    return pool_calloc(pool, total);
}

// ============================================================================
// Lifecycle Functions
// ============================================================================

void flex_grid_context_init_flex(
    FlexGridContext* ctx,
    DomElement* container,
    ViewBlock* container_view,
    Pool* pool,
    AvailableSpace available
) {
    ctx->container = container;
    ctx->container_view = container_view;
    ctx->pool = pool;

    ctx->is_flex = true;
    ctx->is_row_direction = true;  // Default: row
    ctx->is_reversed = false;
    ctx->is_wrap = false;
    ctx->is_wrap_reverse = false;

    ctx->available = available;
    ctx->definite_main = -1.0f;
    ctx->definite_cross = -1.0f;
    ctx->content_box_main = 0.0f;
    ctx->content_box_cross = 0.0f;

    ctx->main_gap = 0.0f;
    ctx->cross_gap = 0.0f;

    ctx->justify_content = 0;  // CSS_VALUE_FLEX_START
    ctx->align_items = 0;      // CSS_VALUE_STRETCH
    ctx->align_content = 0;    // CSS_VALUE_STRETCH

    // Allocate items array
    ctx->item_capacity = INITIAL_ITEM_CAPACITY;
    ctx->item_count = 0;
    ctx->items = (FlexGridItem*)pool_calloc_array(pool, ctx->item_capacity, sizeof(FlexGridItem));

    // Flex lines - start with none
    ctx->flex_lines = nullptr;
    ctx->flex_line_count = 0;
    ctx->flex_line_capacity = 0;

    // Grid fields not used for flex
    ctx->row_tracks = nullptr;
    ctx->col_tracks = nullptr;
    ctx->row_track_count = 0;
    ctx->col_track_count = 0;
    ctx->row_track_capacity = 0;
    ctx->col_track_capacity = 0;
    ctx->occupancy = nullptr;
    ctx->justify_items = 0;
    ctx->align_items_grid = 0;
}

void flex_grid_context_init_grid(
    FlexGridContext* ctx,
    DomElement* container,
    ViewBlock* container_view,
    Pool* pool,
    AvailableSpace available,
    int32_t initial_row_count,
    int32_t initial_col_count
) {
    ctx->container = container;
    ctx->container_view = container_view;
    ctx->pool = pool;

    ctx->is_flex = false;
    ctx->is_row_direction = true;  // Grid always horizontal primary
    ctx->is_reversed = false;
    ctx->is_wrap = false;
    ctx->is_wrap_reverse = false;

    ctx->available = available;
    ctx->definite_main = -1.0f;
    ctx->definite_cross = -1.0f;
    ctx->content_box_main = 0.0f;
    ctx->content_box_cross = 0.0f;

    ctx->main_gap = 0.0f;
    ctx->cross_gap = 0.0f;

    ctx->justify_content = 0;
    ctx->align_items = 0;
    ctx->align_content = 0;
    ctx->justify_items = 0;
    ctx->align_items_grid = 0;

    // Allocate items array
    ctx->item_capacity = INITIAL_ITEM_CAPACITY;
    ctx->item_count = 0;
    ctx->items = (FlexGridItem*)pool_calloc_array(pool, ctx->item_capacity, sizeof(FlexGridItem));

    // Flex lines not used for grid
    ctx->flex_lines = nullptr;
    ctx->flex_line_count = 0;
    ctx->flex_line_capacity = 0;

    // Allocate grid tracks
    int32_t row_cap = initial_row_count > 0 ? initial_row_count : INITIAL_TRACK_CAPACITY;
    int32_t col_cap = initial_col_count > 0 ? initial_col_count : INITIAL_TRACK_CAPACITY;

    ctx->row_track_capacity = row_cap;
    ctx->col_track_capacity = col_cap;
    ctx->row_track_count = 0;
    ctx->col_track_count = 0;
    ctx->row_tracks = (GridTrack*)pool_calloc_array(pool, row_cap, sizeof(GridTrack));
    ctx->col_tracks = (GridTrack*)pool_calloc_array(pool, col_cap, sizeof(GridTrack));

    // Occupancy matrix - allocated on demand
    ctx->occupancy = nullptr;
}

void flex_grid_context_cleanup(FlexGridContext* ctx) {
    // NOTE: We don't explicitly free pool-allocated memory since
    // the pool manages lifetime. But we clear pointers for safety.

    ctx->items = nullptr;
    ctx->item_count = 0;
    ctx->item_capacity = 0;

    ctx->flex_lines = nullptr;
    ctx->flex_line_count = 0;
    ctx->flex_line_capacity = 0;

    ctx->row_tracks = nullptr;
    ctx->col_tracks = nullptr;
    ctx->row_track_count = 0;
    ctx->col_track_count = 0;
    ctx->row_track_capacity = 0;
    ctx->col_track_capacity = 0;

    if (ctx->occupancy) {
        ctx->occupancy->cells = nullptr;
        ctx->occupancy = nullptr;
    }

    ctx->container = nullptr;
    ctx->container_view = nullptr;
    ctx->pool = nullptr;
}

// ============================================================================
// Item Array Management
// ============================================================================

void flex_grid_context_ensure_item_capacity(FlexGridContext* ctx, int32_t needed) {
    if (needed <= ctx->item_capacity) return;

    // Grow by 2x or to needed, whichever is larger
    int32_t new_capacity = ctx->item_capacity * 2;
    if (new_capacity < needed) new_capacity = needed;

    FlexGridItem* new_items = (FlexGridItem*)pool_calloc_array(ctx->pool, new_capacity, sizeof(FlexGridItem));
    if (ctx->items && ctx->item_count > 0) {
        // Copy existing items
        for (int32_t i = 0; i < ctx->item_count; i++) {
            new_items[i] = ctx->items[i];
        }
    }

    ctx->items = new_items;
    ctx->item_capacity = new_capacity;
}

FlexGridItem* flex_grid_context_add_item(FlexGridContext* ctx) {
    flex_grid_context_ensure_item_capacity(ctx, ctx->item_count + 1);

    FlexGridItem* item = &ctx->items[ctx->item_count];
    flex_grid_item_init(item);
    item->source_order = ctx->item_count;
    ctx->item_count++;

    return item;
}

// ============================================================================
// Flex Line Management
// ============================================================================

void flex_grid_context_ensure_line_capacity(FlexGridContext* ctx, int32_t needed) {
    if (needed <= ctx->flex_line_capacity) return;

    int32_t new_capacity = ctx->flex_line_capacity * 2;
    if (new_capacity < INITIAL_LINE_CAPACITY) new_capacity = INITIAL_LINE_CAPACITY;
    if (new_capacity < needed) new_capacity = needed;

    FlexLine* new_lines = (FlexLine*)pool_calloc_array(ctx->pool, new_capacity, sizeof(FlexLine));
    if (ctx->flex_lines && ctx->flex_line_count > 0) {
        for (int32_t i = 0; i < ctx->flex_line_count; i++) {
            new_lines[i] = ctx->flex_lines[i];
        }
    }

    ctx->flex_lines = new_lines;
    ctx->flex_line_capacity = new_capacity;
}

FlexLine* flex_grid_context_add_line(FlexGridContext* ctx) {
    flex_grid_context_ensure_line_capacity(ctx, ctx->flex_line_count + 1);

    FlexLine* line = &ctx->flex_lines[ctx->flex_line_count];
    *line = flex_line_empty();
    ctx->flex_line_count++;

    return line;
}

// ============================================================================
// Grid Track Management
// ============================================================================

void flex_grid_context_ensure_row_track_capacity(FlexGridContext* ctx, int32_t needed) {
    if (needed <= ctx->row_track_capacity) return;

    int32_t new_capacity = ctx->row_track_capacity * 2;
    if (new_capacity < INITIAL_TRACK_CAPACITY) new_capacity = INITIAL_TRACK_CAPACITY;
    if (new_capacity < needed) new_capacity = needed;

    GridTrack* new_tracks = (GridTrack*)pool_calloc_array(ctx->pool, new_capacity, sizeof(GridTrack));
    if (ctx->row_tracks && ctx->row_track_count > 0) {
        for (int32_t i = 0; i < ctx->row_track_count; i++) {
            new_tracks[i] = ctx->row_tracks[i];
        }
    }

    ctx->row_tracks = new_tracks;
    ctx->row_track_capacity = new_capacity;
}

void flex_grid_context_ensure_col_track_capacity(FlexGridContext* ctx, int32_t needed) {
    if (needed <= ctx->col_track_capacity) return;

    int32_t new_capacity = ctx->col_track_capacity * 2;
    if (new_capacity < INITIAL_TRACK_CAPACITY) new_capacity = INITIAL_TRACK_CAPACITY;
    if (new_capacity < needed) new_capacity = needed;

    GridTrack* new_tracks = (GridTrack*)pool_calloc_array(ctx->pool, new_capacity, sizeof(GridTrack));
    if (ctx->col_tracks && ctx->col_track_count > 0) {
        for (int32_t i = 0; i < ctx->col_track_count; i++) {
            new_tracks[i] = ctx->col_tracks[i];
        }
    }

    ctx->col_tracks = new_tracks;
    ctx->col_track_capacity = new_capacity;
}

GridTrack* flex_grid_context_add_row_track(FlexGridContext* ctx) {
    flex_grid_context_ensure_row_track_capacity(ctx, ctx->row_track_count + 1);

    GridTrack* track = &ctx->row_tracks[ctx->row_track_count];
    *track = grid_track_empty();
    ctx->row_track_count++;

    return track;
}

GridTrack* flex_grid_context_add_col_track(FlexGridContext* ctx) {
    flex_grid_context_ensure_col_track_capacity(ctx, ctx->col_track_count + 1);

    GridTrack* track = &ctx->col_tracks[ctx->col_track_count];
    *track = grid_track_empty();
    ctx->col_track_count++;

    return track;
}

// ============================================================================
// Grid Occupancy Management
// ============================================================================

void flex_grid_context_init_occupancy(FlexGridContext* ctx, int32_t rows, int32_t cols) {
    if (!ctx->occupancy) {
        ctx->occupancy = (GridOccupancy*)pool_calloc(ctx->pool, sizeof(GridOccupancy));
    }

    ctx->occupancy->row_count = rows;
    ctx->occupancy->col_count = cols;
    ctx->occupancy->allocated_rows = rows;
    ctx->occupancy->allocated_cols = cols;
    ctx->occupancy->cells = (bool*)pool_calloc_array(ctx->pool, rows * cols, sizeof(bool));
}

void flex_grid_context_expand_occupancy_rows(FlexGridContext* ctx, int32_t new_row_count) {
    if (!ctx->occupancy) {
        flex_grid_context_init_occupancy(ctx, new_row_count, 1);
        return;
    }

    if (new_row_count <= ctx->occupancy->allocated_rows) {
        ctx->occupancy->row_count = new_row_count;
        return;
    }

    // Need to reallocate
    int32_t old_rows = ctx->occupancy->row_count;
    int32_t cols = ctx->occupancy->col_count;
    int32_t new_allocated = new_row_count * 2;  // Grow with some slack

    bool* new_cells = (bool*)pool_calloc_array(ctx->pool, new_allocated * cols, sizeof(bool));

    // Copy existing data
    if (ctx->occupancy->cells) {
        for (int32_t r = 0; r < old_rows; r++) {
            for (int32_t c = 0; c < cols; c++) {
                new_cells[r * cols + c] = ctx->occupancy->cells[r * cols + c];
            }
        }
    }

    ctx->occupancy->cells = new_cells;
    ctx->occupancy->row_count = new_row_count;
    ctx->occupancy->allocated_rows = new_allocated;
}

void flex_grid_occupancy_mark_region(
    GridOccupancy* occ,
    int32_t row_start,
    int32_t col_start,
    int32_t row_span,
    int32_t col_span
) {
    for (int32_t r = row_start; r < row_start + row_span && r < occ->row_count; r++) {
        for (int32_t c = col_start; c < col_start + col_span && c < occ->col_count; c++) {
            if (r >= 0 && c >= 0) {
                occ->cells[r * occ->col_count + c] = true;
            }
        }
    }
}

bool flex_grid_occupancy_region_available(
    GridOccupancy* occ,
    int32_t row_start,
    int32_t col_start,
    int32_t row_span,
    int32_t col_span
) {
    // Check bounds
    if (row_start < 0 || col_start < 0) return false;
    if (row_start + row_span > occ->row_count) return false;
    if (col_start + col_span > occ->col_count) return false;

    // Check all cells in region
    for (int32_t r = row_start; r < row_start + row_span; r++) {
        for (int32_t c = col_start; c < col_start + col_span; c++) {
            if (occ->cells[r * occ->col_count + c]) {
                return false;
            }
        }
    }

    return true;
}

} // namespace radiant
