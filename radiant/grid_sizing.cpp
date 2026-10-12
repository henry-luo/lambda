#include "layout.hpp"
#include "grid_enhanced_adapter.hpp"  // Enhanced track sizing algorithm
#include <new>

extern "C" {
#include <math.h>
#include "../lib/log.h"
#include "../lib/memtrack.h"
}

static bool initialize_grid_axis(lam::Up<radiant::grid::TrackArray>* destination, int track_count,
                                 int explicit_count, int negative_implicit_count,
                                 GridTrackList* template_tracks,
                                 GridTrackList* automatic_tracks,
                                 ScratchArena* scratch,
                                 const char* axis_name) {
    if (*destination) (*destination)->~TrackArray();
    void* storage = scratch_alloc(scratch, sizeof(radiant::grid::TrackArray));
    *destination = lam::up(storage ? new (storage) radiant::grid::TrackArray() : nullptr); // NEW_DELETE_OK: scratch-owned pass state.
    if (!*destination) {
        log_error("grid_sizing: unable to allocate %d %s scratch tracks", track_count, axis_name);
        return false;
    }
    if (track_count <= 0) return true;
    if (!(*destination)->reserve((size_t)track_count)) return false;
    int explicit_start = negative_implicit_count;
    int explicit_end = explicit_start + explicit_count;
    int sized_explicit_end = explicit_start + (template_tracks ? template_tracks->track_count : 0);

    for (int index = 0; index < track_count; index++) {
        GridTrackSize* parsed_size = nullptr;
        if (index >= explicit_start && index < explicit_end && template_tracks &&
            index - explicit_start < template_tracks->track_count) {
            parsed_size = template_tracks->tracks[index - explicit_start];
        } else {
            if (automatic_tracks && automatic_tracks->track_count > 0) {
                int auto_count = automatic_tracks->track_count;
                int auto_index = index < explicit_start
                    ? (auto_count - ((explicit_start - index) % auto_count)) % auto_count
                    : (index - sized_explicit_end) % auto_count;
                parsed_size = automatic_tracks->tracks[auto_index];
            }
        }
        // Parsed GridTrackSize is input syntax only; computed state starts directly in the enhanced model.
        (*destination)->push_back(radiant::grid::EnhancedGridTrack(
            radiant::grid_adapter::convert_to_track_sizing(parsed_size)));
    }
    return (*destination)->good();
}

// Initialize track sizes
void initialize_track_sizes(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;
    if (!grid_layout->lycon) {
        log_error("grid_sizing: missing layout context for scratch track allocation");
        return;
    }


    bool rows_ready = initialize_grid_axis(&grid_layout->computed_rows, grid_layout->computed_row_count,
                         grid_layout->explicit_row_count,
                         grid_layout->negative_implicit_row_count,
                         grid_layout->grid_template_rows, grid_layout->grid_auto_rows,
                         &grid_layout->lycon->scratch, "row");
    bool columns_ready = initialize_grid_axis(&grid_layout->computed_columns, grid_layout->computed_column_count,
                         grid_layout->explicit_column_count,
                         grid_layout->negative_implicit_column_count,
                         grid_layout->grid_template_columns,
                         grid_layout->grid_auto_columns, &grid_layout->lycon->scratch, "column");
    grid_layout->storage_failed = !rows_ready || !columns_ready;

}

// Enhanced track sizing using the new algorithm adapted from Taffy
// This provides more accurate CSS Grid spec compliance
void resolve_track_sizes_enhanced(GridContainerLayout* grid_layout, ViewBlock* container) {
    if (!grid_layout || !container) return;


    // Phase 1: Initialize track sizes (still needed for memory allocation)
    initialize_track_sizes(grid_layout);
    if (grid_layout->storage_failed) return;

    // For shrink-to-fit containers, use indefinite width (-1) so tracks size to content
    // rather than expanding to fill available space
    bool intrinsic_width_constraint = grid_layout->is_min_content_width ||
        grid_layout->is_max_content_width;
    float sizing_width = (grid_layout->is_shrink_to_fit_width || intrinsic_width_constraint)
        ? -1.0f
        : static_cast<float>(grid_layout->content_width);

    // Use the enhanced track sizing algorithm from the adapter
    // This implements the full CSS Grid track sizing algorithm (§11.4-11.8)
    // out_col_intrinsic_width receives the first-pass (pct-as-auto) width when pct tracks
    // are re-resolved for an indefinite container — used to cap container size below.
    float col_intrinsic_width = 0.0f;
    float row_intrinsic_height = 0.0f;
    radiant::grid_adapter::run_enhanced_track_sizing(
        grid_layout,
        grid_layout->grid_items,
        grid_layout->item_count,
        sizing_width,
        grid_layout->has_explicit_height || grid_layout->content_height > 0.0f
            ? grid_layout->content_height : -1.0f,
        &col_intrinsic_width,
        &row_intrinsic_height,
        grid_layout->is_min_content_width,
        grid_layout->is_max_content_width
    );

    if (grid_layout->storage_failed) return;

    // Store row intrinsic height for container height capping
    if (row_intrinsic_height > 0.0f) {
        grid_layout->row_intrinsic_height = row_intrinsic_height;
    }

    // Intrinsic-width grids and shrink-to-fit grids use the resolved track sum,
    // not the provisional parent width used before their item styles were materialized.
    // When percentage tracks exist, the container's intrinsic width (first-pass) is used
    // as the upper bound: pct tracks may overflow the container, but the container size
    // is determined by the non-pct content (CSS Grid §12.5).
    if ((grid_layout->is_shrink_to_fit_width || grid_layout->is_min_content_width ||
         grid_layout->is_max_content_width) && grid_layout->computed_column_count > 0) {
        float total_column_width = radiant::grid::spanned_tracks_size(
            *grid_layout->computed_columns, 0, grid_layout->computed_column_count,
            grid_layout->column_gap);
        // Cap at the intrinsic (pre-pct-re-resolution) width when pct tracks would inflate it.
        // Also use the intrinsic width when pct tracks caused columns to shrink below it —
        // the container's shrink-to-fit width is the max-content of its tracks in the first pass.
        if (col_intrinsic_width > 0.0f && col_intrinsic_width != total_column_width) {
            total_column_width = col_intrinsic_width;
        }
        grid_layout->content_width = total_column_width;
    }

}
