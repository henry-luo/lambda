#include "grid.hpp"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../lib/log.h"
}

// Initialize track sizes
void initialize_track_sizes(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_debug("Initializing track sizes\n");

    // Allocate computed tracks
    if (grid_layout->computed_row_count > 0) {
        grid_layout->computed_rows = (GridTrack*)calloc(grid_layout->computed_row_count, sizeof(GridTrack));

        // Initialize row tracks
        for (int i = 0; i < grid_layout->computed_row_count; i++) {
            GridTrack* track = &grid_layout->computed_rows[i];

            if (i < grid_layout->explicit_row_count &&
                grid_layout->grid_template_rows &&
                i < grid_layout->grid_template_rows->track_count) {
                // Explicit track - use template definition (shared, don't free)
                track->size = grid_layout->grid_template_rows->tracks[i];
                track->is_implicit = false;
                track->owns_size = false;  // Shared with template
            } else {
                // Implicit track - use grid-auto-rows if defined, otherwise auto sizing
                if (grid_layout->grid_auto_rows && grid_layout->grid_auto_rows->track_count > 0) {
                    // Use the first auto-row track size (cycle through if multiple)
                    // Note: this is shared, don't free
                    int auto_track_idx = (i - grid_layout->explicit_row_count) % grid_layout->grid_auto_rows->track_count;
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
            track->is_flexible = (track->size && track->size->type == GRID_TRACK_SIZE_FR);
        }
    }

    if (grid_layout->computed_column_count > 0) {
        grid_layout->computed_columns = (GridTrack*)calloc(grid_layout->computed_column_count, sizeof(GridTrack));

        // Initialize column tracks
        for (int i = 0; i < grid_layout->computed_column_count; i++) {
            GridTrack* track = &grid_layout->computed_columns[i];

            if (i < grid_layout->explicit_column_count &&
                grid_layout->grid_template_columns &&
                i < grid_layout->grid_template_columns->track_count) {
                // Explicit track - use template definition (shared, don't free)
                track->size = grid_layout->grid_template_columns->tracks[i];
                track->is_implicit = false;
                track->owns_size = false;  // Shared with template
            } else {
                // Implicit track - use grid-auto-columns if defined, otherwise auto sizing
                if (grid_layout->grid_auto_columns && grid_layout->grid_auto_columns->track_count > 0) {
                    // Use the first auto-column track size (cycle through if multiple)
                    // Note: this is shared, don't free
                    int auto_track_idx = (i - grid_layout->explicit_column_count) % grid_layout->grid_auto_columns->track_count;
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
            track->is_flexible = (track->size && track->size->type == GRID_TRACK_SIZE_FR);
        }
    }

    log_debug("Track sizes initialized - %d rows, %d columns\n",
              grid_layout->computed_row_count, grid_layout->computed_column_count);
}

// Resolve intrinsic track sizes
void resolve_intrinsic_track_sizes(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_debug("Resolving intrinsic track sizes\n");

    // Resolve row track sizes
    for (int i = 0; i < grid_layout->computed_row_count; i++) {
        GridTrack* track = &grid_layout->computed_rows[i];

        if (!track->size) continue;

        switch (track->size->type) {
            case GRID_TRACK_SIZE_LENGTH:
                track->base_size = track->size->value;
                track->growth_limit = track->size->value;
                break;

            case GRID_TRACK_SIZE_PERCENTAGE:
                if (grid_layout->content_height > 0) {
                    track->base_size = (grid_layout->content_height * track->size->value) / 100;
                    track->growth_limit = track->base_size;
                } else {
                    track->base_size = 0;
                    track->growth_limit = 0;
                }
                break;

            case GRID_TRACK_SIZE_AUTO:
            case GRID_TRACK_SIZE_MIN_CONTENT:
            case GRID_TRACK_SIZE_MAX_CONTENT:
                // Calculate based on content
                track->base_size = calculate_track_intrinsic_size(grid_layout, i, true, track->size->type);
                track->growth_limit = track->base_size;
                break;

            case GRID_TRACK_SIZE_FR:
                track->base_size = 0;
                track->growth_limit = INFINITY;
                track->is_flexible = true;
                break;

            default:
                track->base_size = 0;
                track->growth_limit = 0;
                break;
        }

        track->computed_size = track->base_size;
    }

    // Resolve column track sizes
    for (int i = 0; i < grid_layout->computed_column_count; i++) {
        GridTrack* track = &grid_layout->computed_columns[i];

        if (!track->size) continue;

        switch (track->size->type) {
            case GRID_TRACK_SIZE_LENGTH:
                track->base_size = track->size->value;
                track->growth_limit = track->size->value;
                break;

            case GRID_TRACK_SIZE_PERCENTAGE:
                if (grid_layout->content_width > 0) {
                    track->base_size = (grid_layout->content_width * track->size->value) / 100;
                    track->growth_limit = track->base_size;
                } else {
                    track->base_size = 0;
                    track->growth_limit = 0;
                }
                break;

            case GRID_TRACK_SIZE_AUTO:
            case GRID_TRACK_SIZE_MIN_CONTENT:
            case GRID_TRACK_SIZE_MAX_CONTENT:
                // Calculate based on content
                track->base_size = calculate_track_intrinsic_size(grid_layout, i, false, track->size->type);
                track->growth_limit = track->base_size;
                break;

            case GRID_TRACK_SIZE_FR:
                track->base_size = 0;
                track->growth_limit = INFINITY;
                track->is_flexible = true;
                break;

            default:
                track->base_size = 0;
                track->growth_limit = 0;
                break;
        }

        track->computed_size = track->base_size;
    }

    log_debug("Intrinsic track sizes resolved\n");
}

// Calculate intrinsic size for a track based on its content
int calculate_track_intrinsic_size(GridContainerLayout* grid_layout, int track_index, bool is_row, GridTrackSizeType size_type) {
    if (!grid_layout) return 0;

    int max_size = 0;

    // Find all items that span this track
    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* item = grid_layout->grid_items[i];
        if (!item->gi) continue;  // Skip items without grid item properties

        bool spans_track = false;
        if (is_row) {
            spans_track = (item->gi->computed_grid_row_start <= track_index + 1 &&
                          item->gi->computed_grid_row_end > track_index + 1);
        } else {
            spans_track = (item->gi->computed_grid_column_start <= track_index + 1 &&
                          item->gi->computed_grid_column_end > track_index + 1);
        }

        if (spans_track) {
            IntrinsicSizes item_sizes = calculate_grid_item_intrinsic_sizes(grid_layout->lycon, item, is_row);

            int item_size = 0;
            switch (size_type) {
                case GRID_TRACK_SIZE_MIN_CONTENT:
                    item_size = item_sizes.min_content;
                    break;
                case GRID_TRACK_SIZE_MAX_CONTENT:
                    item_size = item_sizes.max_content;
                    break;
                case GRID_TRACK_SIZE_AUTO:
                    // For auto, use max-content
                    item_size = item_sizes.max_content;
                    break;
                default:
                    item_size = item_sizes.max_content;
                    break;
            }

            // If item spans multiple tracks, distribute size proportionally
            int span_count = 1;
            if (is_row) {
                span_count = item->gi->computed_grid_row_end - item->gi->computed_grid_row_start;
            } else {
                span_count = item->gi->computed_grid_column_end - item->gi->computed_grid_column_start;
            }

            if (span_count > 1) {
                item_size = item_size / span_count;
            }

            max_size = fmax(max_size, item_size);
        }
    }

    return max_size;
}

// Maximize tracks
void maximize_tracks(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_debug("Maximizing tracks\n");

    // For non-flexible tracks, set computed size to growth limit
    for (int i = 0; i < grid_layout->computed_row_count; i++) {
        GridTrack* track = &grid_layout->computed_rows[i];
        if (!track->is_flexible && track->growth_limit != INFINITY) {
            track->computed_size = track->growth_limit;
        }
    }

    for (int i = 0; i < grid_layout->computed_column_count; i++) {
        GridTrack* track = &grid_layout->computed_columns[i];
        if (!track->is_flexible && track->growth_limit != INFINITY) {
            track->computed_size = track->growth_limit;
        }
    }

    log_debug("Tracks maximized\n");
}

// Distribute remaining space to auto tracks (CSS Grid stretch behavior)
static void distribute_space_to_auto_tracks_in_axis(GridTrack* tracks, int track_count, int available_space) {
    if (!tracks || track_count == 0 || available_space <= 0) {
        log_debug(" distribute_space_to_auto_tracks early return - space=%d\n", available_space);
        return;
    }

    // Count auto tracks (implicit tracks with AUTO sizing that can stretch)
    int auto_count = 0;
    int used_space = 0;
    for (int i = 0; i < track_count; i++) {
        GridTrack* track = &tracks[i];
        used_space += track->computed_size;
        // Track is stretchable if it's AUTO type and implicit
        if (track->size && track->size->type == GRID_TRACK_SIZE_AUTO && track->is_implicit) {
            auto_count++;
        }
    }

    int remaining_space = available_space - used_space;
    log_debug(" Distribute to auto tracks - available=%d, used=%d, remaining=%d, auto_count=%d\n",
           available_space, used_space, remaining_space, auto_count);

    if (auto_count == 0 || remaining_space <= 0) {
        return;
    }

    // Distribute remaining space equally among auto tracks
    int extra_per_track = remaining_space / auto_count;
    int leftover = remaining_space % auto_count;

    for (int i = 0; i < track_count; i++) {
        GridTrack* track = &tracks[i];
        if (track->size && track->size->type == GRID_TRACK_SIZE_AUTO && track->is_implicit) {
            int extra = extra_per_track + (leftover > 0 ? 1 : 0);
            if (leftover > 0) leftover--;
            log_debug(" Auto track %d: %d + %d = %d\n",
                   i, track->computed_size, extra, track->computed_size + extra);
            track->computed_size += extra;
        }
    }
}

// Distribute remaining space to auto tracks
void distribute_space_to_auto_tracks(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_debug(" Distributing space to auto tracks\n");
    log_debug(" Content dimensions: %dx%d\n", grid_layout->content_width, grid_layout->content_height);

    // For columns, distribute remaining horizontal space
    if (grid_layout->content_width > 0) {
        int col_gap_total = (grid_layout->computed_column_count - 1) * grid_layout->column_gap;
        int available = grid_layout->content_width - col_gap_total;
        distribute_space_to_auto_tracks_in_axis(grid_layout->computed_columns,
                                                  grid_layout->computed_column_count,
                                                  available);
    }

    // For rows, only distribute if container has defined height
    if (grid_layout->content_height > 0) {
        int row_gap_total = (grid_layout->computed_row_count - 1) * grid_layout->row_gap;
        int available = grid_layout->content_height - row_gap_total;
        distribute_space_to_auto_tracks_in_axis(grid_layout->computed_rows,
                                                  grid_layout->computed_row_count,
                                                  available);
    }

    log_debug(" Auto track space distribution complete\n");
}

// Expand flexible tracks
void expand_flexible_tracks(GridContainerLayout* grid_layout, ViewBlock* container) {
    if (!grid_layout || !container) return;

    log_debug(" Expanding flexible tracks\n");
    log_debug(" Content dimensions: %dx%d\n", grid_layout->content_width, grid_layout->content_height);
    log_debug(" Grid dimensions: %d rows, %d columns\n", grid_layout->computed_row_count, grid_layout->computed_column_count);
    log_debug(" Gaps: row=%.1f, column=%.1f\n", grid_layout->row_gap, grid_layout->column_gap);
    log_debug("Expanding flexible tracks\n");

    // Calculate available space for flexible tracks
    int available_row_space = grid_layout->content_height;
    int available_column_space = grid_layout->content_width;

    log_debug(" Initial available space - rows: %d, columns: %d\n", available_row_space, available_column_space);

    // Subtract space used by non-flexible tracks and gaps
    for (int i = 0; i < grid_layout->computed_row_count; i++) {
        GridTrack* track = &grid_layout->computed_rows[i];
        if (!track->is_flexible) {
            log_debug(" Subtracting non-flexible row %d size: %d\n", i, track->computed_size);
            available_row_space -= track->computed_size;
        }
    }
    int row_gap_total = (grid_layout->computed_row_count - 1) * grid_layout->row_gap;
    available_row_space -= row_gap_total;
    log_debug(" After subtracting gaps (%d), available row space: %d\n", row_gap_total, available_row_space);

    for (int i = 0; i < grid_layout->computed_column_count; i++) {
        GridTrack* track = &grid_layout->computed_columns[i];
        if (!track->is_flexible) {
            log_debug(" Subtracting non-flexible column %d size: %d\n", i, track->computed_size);
            available_column_space -= track->computed_size;
        }
    }
    int column_gap_total = (grid_layout->computed_column_count - 1) * grid_layout->column_gap;
    available_column_space -= column_gap_total;
    log_debug(" After subtracting gaps (%d), available column space: %d\n", column_gap_total, available_column_space);

    // Distribute available space among flexible tracks
    expand_flexible_tracks_in_axis(grid_layout->computed_rows, grid_layout->computed_row_count, available_row_space);
    expand_flexible_tracks_in_axis(grid_layout->computed_columns, grid_layout->computed_column_count, available_column_space);

    log_debug("Flexible tracks expanded\n");
}

// Expand flexible tracks in a single axis
void expand_flexible_tracks_in_axis(GridTrack* tracks, int track_count, int available_space) {
    if (!tracks || track_count == 0 || available_space <= 0) {
        log_debug(" expand_flexible_tracks_in_axis early return - tracks=%p, count=%d, space=%d\n",
               tracks, track_count, available_space);
        return;
    }

    // Calculate total fr units
    float total_fr = 0;
    int flexible_count = 0;
    for (int i = 0; i < track_count; i++) {
        if (tracks[i].is_flexible && tracks[i].size && tracks[i].size->type == GRID_TRACK_SIZE_FR) {
            // Convert from stored integer (multiplied by 100) back to float
            float fr_value = tracks[i].size->value / 100.0f;
            total_fr += fr_value;
            flexible_count++;
            log_debug(" Track %d is flexible: %.2ffr (stored as %d)\n", i, fr_value, tracks[i].size->value);
        } else {
            log_debug(" Track %d is not flexible: type=%d, is_flexible=%d\n",
                   i, tracks[i].size ? tracks[i].size->type : -1, tracks[i].is_flexible);
        }
    }

    if (total_fr <= 0) {
        log_debug(" No flexible tracks found (total_fr=%.2f)\n", total_fr);
        return;
    }

    // Distribute space proportionally
    float fr_size = available_space / total_fr;

    log_debug(" Flexible track sizing - available_space=%d, total_fr=%.2f, fr_size=%.2f, flexible_count=%d\n",
           available_space, total_fr, fr_size, flexible_count);

    for (int i = 0; i < track_count; i++) {
        GridTrack* track = &tracks[i];
        if (track->is_flexible && track->size && track->size->type == GRID_TRACK_SIZE_FR) {
            // Convert from stored integer (multiplied by 100) back to float
            float fr_value = track->size->value / 100.0f;
            track->computed_size = (int)(fr_value * fr_size);

            log_debug(" Flexible track %d: %.2ffr × %.2f = %dpx\n", i, fr_value, fr_size, track->computed_size);
            log_debug("Flexible track %d: %.2ffr = %dpx\n", i, fr_value, track->computed_size);
        }
    }
}

// Main track sizing algorithm
void resolve_track_sizes(GridContainerLayout* grid_layout, ViewBlock* container) {
    if (!grid_layout || !container) return;

    log_debug("Resolving track sizes\n");

    // Phase 1: Initialize track sizes
    initialize_track_sizes(grid_layout);

    // Phase 2: Resolve intrinsic track sizes
    resolve_intrinsic_track_sizes(grid_layout);

    // Phase 3: Maximize tracks
    maximize_tracks(grid_layout);

    // Phase 4: Expand flexible tracks (for FR units)
    expand_flexible_tracks(grid_layout, container);

    // Phase 5: Distribute remaining space to auto tracks (stretch behavior)
    distribute_space_to_auto_tracks(grid_layout);

    log_debug("Track sizes resolved\n");
}
