#include "layout.hpp"
#include "grid_enhanced_adapter.hpp"  // Enhanced track sizing algorithm

extern "C" {
#include <math.h>
#include "../lib/log.h"
#include "../lib/memtrack.h"
}

static void initialize_grid_axis(GridTrack** destination, int track_count,
                                 int explicit_count, int negative_implicit_count,
                                 GridTrackList* template_tracks,
                                 GridTrackList* automatic_tracks,
                                 ScratchArena* scratch,
                                 const char* axis_name) {
    if (track_count <= 0) return;

    *destination = (GridTrack*)scratch_calloc(scratch, (size_t)track_count * sizeof(GridTrack));
    if (!*destination) {
        log_error("grid_sizing: unable to allocate %d %s scratch tracks", track_count, axis_name);
        return;
    }
    log_debug("  Allocated %d %s tracks", track_count, axis_name);
    int explicit_start = negative_implicit_count;
    int explicit_end = explicit_start + explicit_count;

    for (int index = 0; index < track_count; index++) {
        GridTrack* track = &(*destination)[index];
        if (index >= explicit_start && index < explicit_end && template_tracks &&
            index - explicit_start < template_tracks->track_count) {
            track->size = template_tracks->tracks[index - explicit_start];
            track->is_implicit = false;
            track->owns_size = false;
        } else {
            if (automatic_tracks && automatic_tracks->track_count > 0) {
                int auto_count = automatic_tracks->track_count;
                int auto_index = index < explicit_start
                    ? (auto_count - ((explicit_start - index) % auto_count)) % auto_count
                    : (index - explicit_end) % auto_count;
                track->size = automatic_tracks->tracks[auto_index];
                track->owns_size = false;
            } else {
                track->size = create_grid_track_size(GRID_TRACK_SIZE_AUTO, 0);
                track->owns_size = true;
            }
            track->is_implicit = true;
        }

        track->growth_limit = INFINITY;
        track->is_flexible = track->size &&
            (track->size->type == GRID_TRACK_SIZE_FR ||
             (track->size->type == GRID_TRACK_SIZE_MINMAX && track->size->max_size &&
              track->size->max_size->type == GRID_TRACK_SIZE_FR));
    }
}

// Initialize track sizes
void initialize_track_sizes(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;
    if (!grid_layout->lycon) {
        log_error("grid_sizing: missing layout context for scratch track allocation");
        return;
    }

    log_debug("Initializing track sizes: computed_rows=%d, computed_cols=%d",
              grid_layout->computed_row_count, grid_layout->computed_column_count);

    // Allocate computed tracks (clamp to prevent excessive allocation)
    if (grid_layout->computed_row_count > 1000) grid_layout->computed_row_count = 1000;
    if (grid_layout->computed_column_count > 1000) grid_layout->computed_column_count = 1000;

    initialize_grid_axis(&grid_layout->computed_rows, grid_layout->computed_row_count,
                         grid_layout->explicit_row_count,
                         grid_layout->negative_implicit_row_count,
                         grid_layout->grid_template_rows, grid_layout->grid_auto_rows,
                         &grid_layout->lycon->scratch, "row");
    initialize_grid_axis(&grid_layout->computed_columns, grid_layout->computed_column_count,
                         grid_layout->explicit_column_count,
                         grid_layout->negative_implicit_column_count,
                         grid_layout->grid_template_columns,
                         grid_layout->grid_auto_columns, &grid_layout->lycon->scratch, "column");

    log_debug("Track sizes initialized - %d rows, %d columns\n",
              grid_layout->computed_row_count, grid_layout->computed_column_count);
}

// Enhanced track sizing using the new algorithm adapted from Taffy
// This provides more accurate CSS Grid spec compliance
void resolve_track_sizes_enhanced(GridContainerLayout* grid_layout, ViewBlock* container) {
    if (!grid_layout || !container) return;

    log_debug("Resolving track sizes (enhanced algorithm)\n");

    // Phase 1: Initialize track sizes (still needed for memory allocation)
    initialize_track_sizes(grid_layout);

    // For shrink-to-fit containers, use indefinite width (-1) so tracks size to content
    // rather than expanding to fill available space
    float sizing_width = grid_layout->is_shrink_to_fit_width
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
        static_cast<float>(grid_layout->content_height),
        &col_intrinsic_width,
        &row_intrinsic_height
    );

    // Store row intrinsic height for container height capping
    if (row_intrinsic_height > 0.0f) {
        grid_layout->row_intrinsic_height = row_intrinsic_height;
    }

    // Diagnostic: dump column sizes after enhanced track sizing
    log_debug("grid sizing enhanced: computed_column_count=%d content_width=%.1f sizing_width=%.1f",
             grid_layout->computed_column_count, grid_layout->content_width, sizing_width);
    for (int i = 0; i < grid_layout->computed_column_count; i++) {
        GridTrack* track = &grid_layout->computed_columns[i];
        log_debug("grid sizing enhanced col[%d]: base=%.1f gl=%.1f computed=%.1f flex=%d",
                 i, track->base_size, track->growth_limit, track->computed_size, track->is_flexible);
    }

    // For shrink-to-fit containers, update content_width based on resolved track sizes.
    // When percentage tracks exist, the container's intrinsic width (first-pass) is used
    // as the upper bound: pct tracks may overflow the container, but the container size
    // is determined by the non-pct content (CSS Grid §12.5).
    if (grid_layout->is_shrink_to_fit_width && grid_layout->computed_column_count > 0) {
        float total_column_width = 0;
        for (int i = 0; i < grid_layout->computed_column_count; i++) {
            total_column_width += grid_layout->computed_columns[i].computed_size;
        }
        // Add gaps between columns
        if (grid_layout->computed_column_count > 1) {
            total_column_width += grid_layout->column_gap * (grid_layout->computed_column_count - 1);
        }
        // Cap at the intrinsic (pre-pct-re-resolution) width when pct tracks would inflate it.
        // Also use the intrinsic width when pct tracks caused columns to shrink below it —
        // the container's shrink-to-fit width is the max-content of its tracks in the first pass.
        if (col_intrinsic_width > 0.0f && col_intrinsic_width != total_column_width) {
            total_column_width = col_intrinsic_width;
        }
        grid_layout->content_width = total_column_width;
    }

    log_debug("Track sizes resolved (enhanced algorithm)\n");
}
