#include "layout.hpp"
#include "grid_enhanced_adapter.hpp"  // Enhanced track sizing algorithm

extern "C" {
#include <math.h>
#include "../lib/log.h"
#include "../lib/memtrack.h"
}

// Initialize track sizes
void initialize_track_sizes(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_debug("Initializing track sizes: computed_rows=%d, computed_cols=%d",
              grid_layout->computed_row_count, grid_layout->computed_column_count);

    // Get negative implicit offsets (tracks added before the explicit grid)
    int neg_row_offset = grid_layout->negative_implicit_row_count;
    int neg_col_offset = grid_layout->negative_implicit_column_count;

    // Allocate computed tracks (clamp to prevent excessive allocation)
    if (grid_layout->computed_row_count > 1000) grid_layout->computed_row_count = 1000;
    if (grid_layout->computed_column_count > 1000) grid_layout->computed_column_count = 1000;

    if (grid_layout->computed_row_count > 0) {
        grid_layout->computed_rows = (GridTrack*)mem_calloc(grid_layout->computed_row_count, sizeof(GridTrack), MEM_CAT_LAYOUT);
        log_debug("  Allocated %d row tracks", grid_layout->computed_row_count);

        // Initialize row tracks
        for (int i = 0; i < grid_layout->computed_row_count; i++) {
            GridTrack* track = &grid_layout->computed_rows[i];

            // Calculate the adjusted index relative to the explicit grid
            // With neg_row_offset negative implicit tracks:
            //   Tracks 0 to (neg_row_offset-1) = negative implicit (before explicit)
            //   Tracks neg_row_offset to (neg_row_offset + explicit - 1) = explicit
            //   Tracks (neg_row_offset + explicit) onwards = positive implicit
            int explicit_start = neg_row_offset;
            int explicit_end = neg_row_offset + grid_layout->explicit_row_count;

            if (i >= explicit_start && i < explicit_end &&
                grid_layout->grid_template_rows &&
                (i - explicit_start) < grid_layout->grid_template_rows->track_count) {
                // Explicit track - use template definition (shared, don't free)
                track->size = grid_layout->grid_template_rows->tracks[i - explicit_start];
                track->is_implicit = false;
                track->owns_size = false;  // Shared with template
            } else {
                // Implicit track - use grid-auto-rows if defined, otherwise auto sizing
                if (grid_layout->grid_auto_rows && grid_layout->grid_auto_rows->track_count > 0) {
                    // Calculate auto-row cycle index
                    // For negative implicit tracks (i < explicit_start): cycle backwards
                    // For positive implicit tracks (i >= explicit_end): cycle forwards
                    int auto_track_idx;
                    if (i < explicit_start) {
                        // Negative implicit tracks - count backwards from explicit grid
                        // Track at i=neg_row_offset-1 should be auto_rows[(auto_count-1)]
                        // Track at i=neg_row_offset-2 should be auto_rows[(auto_count-2)]
                        // etc.
                        int distance_from_explicit = explicit_start - i;  // 1, 2, 3...
                        int auto_count = grid_layout->grid_auto_rows->track_count;
                        // Wrap backwards: -1 mod 3 should give 2, -2 mod 3 should give 1
                        auto_track_idx = ((auto_count - (distance_from_explicit % auto_count)) % auto_count);
                    } else {
                        // Positive implicit tracks - count forwards from explicit grid end
                        int distance_from_explicit = i - explicit_end;  // 0, 1, 2...
                        auto_track_idx = distance_from_explicit % grid_layout->grid_auto_rows->track_count;
                    }
                    track->size = grid_layout->grid_auto_rows->tracks[auto_track_idx];
                    track->owns_size = false;  // Shared with auto tracks
                } else {
                    track->size = create_grid_track_size(GRID_TRACK_SIZE_AUTO, 0);
                    track->owns_size = true;  // We created this, we own it
                }
                track->is_implicit = true;
            }

            track->computed_size = 0;
            track->base_size = 0;
            track->growth_limit = INFINITY;
            // Check if track is flexible (has fr units)
            if (track->size) {
                if (track->size->type == GRID_TRACK_SIZE_FR) {
                    track->is_flexible = true;
                } else if (track->size->type == GRID_TRACK_SIZE_MINMAX &&
                           track->size->max_size && track->size->max_size->type == GRID_TRACK_SIZE_FR) {
                    track->is_flexible = true;
                } else {
                    track->is_flexible = false;
                }
            } else {
                track->is_flexible = false;
            }
        }
    }

    if (grid_layout->computed_column_count > 0) {
        grid_layout->computed_columns = (GridTrack*)mem_calloc(grid_layout->computed_column_count, sizeof(GridTrack), MEM_CAT_LAYOUT);
        log_debug("  Allocated %d column tracks", grid_layout->computed_column_count);

        // Initialize column tracks
        for (int i = 0; i < grid_layout->computed_column_count; i++) {
            GridTrack* track = &grid_layout->computed_columns[i];

            // Calculate the adjusted index relative to the explicit grid
            int explicit_start = neg_col_offset;
            int explicit_end = neg_col_offset + grid_layout->explicit_column_count;

            if (i >= explicit_start && i < explicit_end &&
                grid_layout->grid_template_columns &&
                (i - explicit_start) < grid_layout->grid_template_columns->track_count) {
                // Explicit track - use template definition (shared, don't free)
                track->size = grid_layout->grid_template_columns->tracks[i - explicit_start];
                track->is_implicit = false;
                track->owns_size = false;  // Shared with template
            } else {
                // Implicit track - use grid-auto-columns if defined, otherwise auto sizing
                if (grid_layout->grid_auto_columns && grid_layout->grid_auto_columns->track_count > 0) {
                    // Calculate auto-column cycle index
                    int auto_track_idx;
                    if (i < explicit_start) {
                        int distance_from_explicit = explicit_start - i;
                        int auto_count = grid_layout->grid_auto_columns->track_count;
                        auto_track_idx = ((auto_count - (distance_from_explicit % auto_count)) % auto_count);
                    } else {
                        int distance_from_explicit = i - explicit_end;
                        auto_track_idx = distance_from_explicit % grid_layout->grid_auto_columns->track_count;
                    }
                    track->size = grid_layout->grid_auto_columns->tracks[auto_track_idx];
                    track->owns_size = false;  // Shared with auto tracks
                } else {
                    track->size = create_grid_track_size(GRID_TRACK_SIZE_AUTO, 0);
                    track->owns_size = true;  // We created this, we own it
                }
                track->is_implicit = true;
            }

            track->computed_size = 0;
            track->base_size = 0;
            track->growth_limit = INFINITY;
            // Check if track is flexible (has fr units)
            if (track->size) {
                if (track->size->type == GRID_TRACK_SIZE_FR) {
                    track->is_flexible = true;
                } else if (track->size->type == GRID_TRACK_SIZE_MINMAX &&
                           track->size->max_size && track->size->max_size->type == GRID_TRACK_SIZE_FR) {
                    track->is_flexible = true;
                } else {
                    track->is_flexible = false;
                }
            } else {
                track->is_flexible = false;
            }
        }
    }

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
