#pragma once
/**
 * flex_grid_context.hpp - Container-level layout context for flex/grid
 *
 * FlexGridContext manages the container-level state during flex/grid layout.
 * It OWNS the FlexGridItem array for all child items.
 *
 * LIFECYCLE:
 * 1. ALLOCATION: Created on stack, initialized via flex_grid_context_init_flex()
 *    or flex_grid_context_init_grid()
 * 2. ITEM COLLECTION: collect_flex_items() / collect_grid_items() fills items array
 * 3. LAYOUT PASSES: Algorithm uses items array + container state
 * 4. CLEANUP: flex_grid_context_cleanup() frees items array and any owned memory
 *
 * MEMORY OWNERSHIP:
 * - FlexGridContext owns the `items` array (allocated via pool_calloc)
 * - Individual FlexGridItem entries are within that array (not separately allocated)
 * - Grid-specific auxiliary structures (track arrays) are also owned here
 */

#include <cstdint>
#include "flex_grid_item.hpp"
#include "available_space.hpp"

// Forward declarations
struct DomElement;
struct ViewBlock;
struct Pool;

namespace radiant {

// ============================================================================
// FlexLine - A single line in a wrapped flex container
// ============================================================================

struct FlexLine {
    int32_t start_index;       // First item index in this line
    int32_t end_index;         // One past last item index
    float main_size;           // Sum of item main sizes (including gaps)
    float cross_size;          // Resolved cross size of line
    float remaining_free_space; // Free space after flex resolution
    float first_baseline;      // First baseline in the line
    float max_baseline_above;  // Max distance from top to baseline
    float max_baseline_below;  // Max distance from baseline to bottom
};

inline FlexLine flex_line_empty() {
    return {0, 0, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f};
}

// ============================================================================
// GridTrack - A single row or column track in a grid container
// ============================================================================

struct GridTrack {
    float base_size;           // Current size of track
    float growth_limit;        // Maximum growth (INFINITY if none)
    float min_sizing;          // Resolved min track sizing function (pixels or 0)
    float max_sizing;          // Resolved max track sizing function (pixels or INFINITY)
    float flex_factor;         // fr value if track has fr unit (0 otherwise)
    bool is_flexible;          // Has fr unit
    bool is_intrinsic;         // Has min-content, max-content, auto, or fit-content
    bool infinitely_growable;  // For growth algorithm
};

inline GridTrack grid_track_empty() {
    return {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false, false};
}

// ============================================================================
// GridOccupancy - Tracks which cells are occupied during auto-placement
// ============================================================================

struct GridOccupancy {
    bool* cells;               // 2D array stored flat [row * col_count + col]
    int32_t row_count;
    int32_t col_count;
    int32_t allocated_rows;    // Capacity for rows
    int32_t allocated_cols;    // Capacity for cols
};

// ============================================================================
// FlexGridContext - Unified container context
// ============================================================================

/**
 * FlexGridContext - Container-level layout context for flex/grid
 */
struct FlexGridContext {
    // === Container reference ===
    DomElement* container;
    ViewBlock* container_view;
    Pool* pool;                // Memory pool for allocations

    // === Layout mode ===
    bool is_flex;              // true = flex, false = grid
    bool is_row_direction;     // true = row/row-reverse, false = column/column-reverse
    bool is_reversed;          // flex-direction: *-reverse
    bool is_wrap;              // flex-wrap: wrap or wrap-reverse
    bool is_wrap_reverse;      // flex-wrap: wrap-reverse

    // === Container sizes ===
    AvailableSpace available;          // 2D available space constraint
    float definite_main;               // Resolved main axis size (-1 if not definite)
    float definite_cross;              // Resolved cross axis size (-1 if not definite)
    float content_box_main;            // Content box main size (after padding/border)
    float content_box_cross;           // Content box cross size

    // === Gap values ===
    float main_gap;                    // gap in main axis direction
    float cross_gap;                   // gap in cross axis direction

    // === Alignment ===
    int32_t justify_content;           // CSS enum value (CSS_VALUE_*)
    int32_t align_items;
    int32_t align_content;

    // === Items array (OWNED by this context) ===
    FlexGridItem* items;               // Heap-allocated array via pool
    int32_t item_count;                // Number of items
    int32_t item_capacity;             // Allocated capacity

    // === Flex-specific state ===
    FlexLine* flex_lines;              // Array of lines (null if single-line)
    int32_t flex_line_count;
    int32_t flex_line_capacity;

    // === Grid-specific state ===
    GridTrack* row_tracks;             // Array of row tracks
    GridTrack* col_tracks;             // Array of column tracks
    int32_t row_track_count;
    int32_t col_track_count;
    int32_t row_track_capacity;
    int32_t col_track_capacity;
    GridOccupancy* occupancy;          // Cell occupancy matrix

    // Grid-specific alignment (may differ from flex)
    int32_t justify_items;             // Default justify-self for items
    int32_t align_items_grid;          // Default align-self for grid items
};

// ============================================================================
// Lifecycle Functions
// ============================================================================

/**
 * Initialize context for flex layout.
 * Allocates items array with initial capacity.
 */
void flex_grid_context_init_flex(
    FlexGridContext* ctx,
    DomElement* container,
    ViewBlock* container_view,
    Pool* pool,
    AvailableSpace available
);

/**
 * Initialize context for grid layout.
 * Allocates items array and prepares for track allocation.
 */
void flex_grid_context_init_grid(
    FlexGridContext* ctx,
    DomElement* container,
    ViewBlock* container_view,
    Pool* pool,
    AvailableSpace available,
    int32_t initial_row_count,
    int32_t initial_col_count
);

/**
 * Free all memory owned by context.
 * Call this when layout is complete.
 * NOTE: Does NOT free the FlexGridContext struct itself (stack allocated)
 */
void flex_grid_context_cleanup(FlexGridContext* ctx);

// ============================================================================
// Item Array Management
// ============================================================================

/**
 * Ensure items array has capacity for at least `needed` items.
 * Reallocates if necessary.
 */
void flex_grid_context_ensure_item_capacity(FlexGridContext* ctx, int32_t needed);

/**
 * Add an item to the context. Returns pointer to the new item.
 * Item is initialized with default values.
 */
FlexGridItem* flex_grid_context_add_item(FlexGridContext* ctx);

/**
 * Get item by index (with bounds check in debug)
 */
inline FlexGridItem* flex_grid_context_get_item(FlexGridContext* ctx, int32_t index) {
    return &ctx->items[index];
}

// ============================================================================
// Flex Line Management
// ============================================================================

/**
 * Ensure flex_lines array has capacity for at least `needed` lines.
 */
void flex_grid_context_ensure_line_capacity(FlexGridContext* ctx, int32_t needed);

/**
 * Add a flex line. Returns pointer to the new line.
 */
FlexLine* flex_grid_context_add_line(FlexGridContext* ctx);

// ============================================================================
// Grid Track Management
// ============================================================================

/**
 * Ensure row_tracks array has capacity for at least `needed` tracks.
 */
void flex_grid_context_ensure_row_track_capacity(FlexGridContext* ctx, int32_t needed);

/**
 * Ensure col_tracks array has capacity for at least `needed` tracks.
 */
void flex_grid_context_ensure_col_track_capacity(FlexGridContext* ctx, int32_t needed);

/**
 * Add a row track. Returns pointer to the new track.
 */
GridTrack* flex_grid_context_add_row_track(FlexGridContext* ctx);

/**
 * Add a column track. Returns pointer to the new track.
 */
GridTrack* flex_grid_context_add_col_track(FlexGridContext* ctx);

// ============================================================================
// Grid Occupancy Management
// ============================================================================

/**
 * Initialize the occupancy matrix with given dimensions.
 */
void flex_grid_context_init_occupancy(FlexGridContext* ctx, int32_t rows, int32_t cols);

/**
 * Expand occupancy matrix to accommodate more rows.
 */
void flex_grid_context_expand_occupancy_rows(FlexGridContext* ctx, int32_t new_row_count);

/**
 * Check if a cell is occupied.
 */
inline bool flex_grid_occupancy_is_occupied(GridOccupancy* occ, int32_t row, int32_t col) {
    if (row < 0 || row >= occ->row_count || col < 0 || col >= occ->col_count) {
        return false;
    }
    return occ->cells[row * occ->col_count + col];
}

/**
 * Mark a cell as occupied.
 */
inline void flex_grid_occupancy_mark(GridOccupancy* occ, int32_t row, int32_t col) {
    if (row >= 0 && row < occ->row_count && col >= 0 && col < occ->col_count) {
        occ->cells[row * occ->col_count + col] = true;
    }
}

/**
 * Mark a rectangular region as occupied.
 */
void flex_grid_occupancy_mark_region(
    GridOccupancy* occ,
    int32_t row_start,
    int32_t col_start,
    int32_t row_span,
    int32_t col_span
);

/**
 * Check if a rectangular region is available.
 */
bool flex_grid_occupancy_region_available(
    GridOccupancy* occ,
    int32_t row_start,
    int32_t col_start,
    int32_t row_span,
    int32_t col_span
);

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Get total number of main-axis gaps (between items)
 */
inline int32_t flex_grid_context_main_gap_count(FlexGridContext* ctx) {
    if (ctx->is_flex) {
        // For flex: gaps between items in a line (per line)
        return ctx->item_count > 1 ? ctx->item_count - 1 : 0;
    } else {
        // For grid: gaps between tracks
        int32_t track_count = ctx->is_row_direction ? ctx->col_track_count : ctx->row_track_count;
        return track_count > 1 ? track_count - 1 : 0;
    }
}

/**
 * Get total gap space in main axis
 */
inline float flex_grid_context_total_main_gap(FlexGridContext* ctx) {
    return ctx->main_gap * flex_grid_context_main_gap_count(ctx);
}

/**
 * Check if main axis size is definite
 */
inline bool flex_grid_context_has_definite_main(FlexGridContext* ctx) {
    return ctx->definite_main >= 0;
}

/**
 * Check if cross axis size is definite
 */
inline bool flex_grid_context_has_definite_cross(FlexGridContext* ctx) {
    return ctx->definite_cross >= 0;
}

} // namespace radiant
