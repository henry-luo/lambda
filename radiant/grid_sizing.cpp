#include "grid.hpp"
#include "grid_enhanced_adapter.hpp"  // Enhanced track sizing algorithm

extern "C" {
#include <stdlib.h>
#include <string.h>
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

    // Allocate computed tracks
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

// Resolve intrinsic column track sizes
// Called first so that row sizing can use actual column widths
static void resolve_intrinsic_column_sizes(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_debug("Resolving intrinsic column track sizes\n");

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
                track->base_size = calculate_track_intrinsic_size(grid_layout, i, false, track->size->type);
                track->growth_limit = track->base_size;
                break;

            case GRID_TRACK_SIZE_FIT_CONTENT: {
                track->base_size = calculate_track_intrinsic_size(grid_layout, i, false, GRID_TRACK_SIZE_MIN_CONTENT);
                int max_content = calculate_track_intrinsic_size(grid_layout, i, false, GRID_TRACK_SIZE_MAX_CONTENT);
                int fit_content_limit;
                if (track->size->is_percentage && grid_layout->content_width > 0) {
                    fit_content_limit = (grid_layout->content_width * track->size->fit_content_limit) / 100;
                } else {
                    fit_content_limit = track->size->fit_content_limit;
                }
                track->growth_limit = (max_content < fit_content_limit) ? max_content : fit_content_limit;
                if (track->growth_limit < track->base_size) {
                    track->growth_limit = track->base_size;
                }
                log_debug("fit-content column track %d: base=%d, growth=%d", i, track->base_size, (int)track->growth_limit);
                break;
            }

            case GRID_TRACK_SIZE_FR:
                track->base_size = 0;
                track->growth_limit = INFINITY;
                track->is_flexible = true;
                break;

            case GRID_TRACK_SIZE_MINMAX: {
                GridTrackSize* min_size = track->size->min_size;
                GridTrackSize* max_size = track->size->max_size;

                float growth_from_max = INFINITY;
                if (max_size) {
                    switch (max_size->type) {
                        case GRID_TRACK_SIZE_LENGTH:
                            growth_from_max = max_size->value;
                            break;
                        case GRID_TRACK_SIZE_PERCENTAGE:
                            if (grid_layout->content_width > 0) {
                                growth_from_max = (grid_layout->content_width * max_size->value) / 100;
                            }
                            break;
                        case GRID_TRACK_SIZE_AUTO:
                        case GRID_TRACK_SIZE_MAX_CONTENT:
                            growth_from_max = calculate_track_intrinsic_size(grid_layout, i, false, GRID_TRACK_SIZE_MAX_CONTENT);
                            break;
                        case GRID_TRACK_SIZE_MIN_CONTENT:
                            growth_from_max = calculate_track_intrinsic_size(grid_layout, i, false, GRID_TRACK_SIZE_MIN_CONTENT);
                            break;
                        case GRID_TRACK_SIZE_FR:
                            growth_from_max = INFINITY;
                            track->is_flexible = true;
                            break;
                        default:
                            growth_from_max = INFINITY;
                            break;
                    }
                }
                track->growth_limit = growth_from_max;

                if (min_size) {
                    switch (min_size->type) {
                        case GRID_TRACK_SIZE_LENGTH:
                            track->base_size = min_size->value;
                            break;
                        case GRID_TRACK_SIZE_PERCENTAGE:
                            if (grid_layout->content_width > 0) {
                                track->base_size = (grid_layout->content_width * min_size->value) / 100;
                            } else {
                                track->base_size = 0;
                            }
                            break;
                        case GRID_TRACK_SIZE_AUTO: {
                            int min_content = calculate_track_intrinsic_size(grid_layout, i, false, GRID_TRACK_SIZE_MIN_CONTENT);
                            if (growth_from_max != INFINITY && growth_from_max < min_content) {
                                track->base_size = (int)growth_from_max;
                            } else {
                                track->base_size = min_content;
                            }
                            break;
                        }
                        case GRID_TRACK_SIZE_MIN_CONTENT:
                            track->base_size = calculate_track_intrinsic_size(grid_layout, i, false, GRID_TRACK_SIZE_MIN_CONTENT);
                            break;
                        case GRID_TRACK_SIZE_MAX_CONTENT:
                            track->base_size = calculate_track_intrinsic_size(grid_layout, i, false, GRID_TRACK_SIZE_MAX_CONTENT);
                            break;
                        default:
                            track->base_size = 0;
                            break;
                    }
                } else {
                    track->base_size = 0;
                }

                if (track->growth_limit < track->base_size) {
                    track->growth_limit = track->base_size;
                }

                log_debug("minmax column track %d: base=%d, growth=%.1f", i, track->base_size, track->growth_limit);
                break;
            }

            default:
                track->base_size = 0;
                track->growth_limit = 0;
                break;
        }

        track->computed_size = track->base_size;
    }

    log_debug("Intrinsic column sizes resolved\n");
}

// Resolve intrinsic row track sizes
// Called after columns are fully sized so heights can use actual column widths
static void resolve_intrinsic_row_sizes(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_debug("Resolving intrinsic row track sizes\n");

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
                // Height will be calculated using actual column widths
                track->base_size = calculate_track_intrinsic_size(grid_layout, i, true, track->size->type);
                track->growth_limit = track->base_size;
                break;

            case GRID_TRACK_SIZE_FIT_CONTENT: {
                track->base_size = calculate_track_intrinsic_size(grid_layout, i, true, GRID_TRACK_SIZE_MIN_CONTENT);
                int max_content = calculate_track_intrinsic_size(grid_layout, i, true, GRID_TRACK_SIZE_MAX_CONTENT);
                int fit_content_limit;
                if (track->size->is_percentage && grid_layout->content_height > 0) {
                    fit_content_limit = (grid_layout->content_height * track->size->fit_content_limit) / 100;
                } else {
                    fit_content_limit = track->size->fit_content_limit;
                }
                track->growth_limit = (max_content < fit_content_limit) ? max_content : fit_content_limit;
                if (track->growth_limit < track->base_size) {
                    track->growth_limit = track->base_size;
                }
                break;
            }

            case GRID_TRACK_SIZE_FR:
                track->base_size = 0;
                track->growth_limit = INFINITY;
                track->is_flexible = true;
                break;

            case GRID_TRACK_SIZE_MINMAX: {
                GridTrackSize* min_size = track->size->min_size;
                GridTrackSize* max_size = track->size->max_size;

                float growth_from_max = INFINITY;
                if (max_size) {
                    switch (max_size->type) {
                        case GRID_TRACK_SIZE_LENGTH:
                            growth_from_max = max_size->value;
                            break;
                        case GRID_TRACK_SIZE_PERCENTAGE:
                            if (grid_layout->content_height > 0) {
                                growth_from_max = (grid_layout->content_height * max_size->value) / 100;
                            }
                            break;
                        case GRID_TRACK_SIZE_AUTO:
                        case GRID_TRACK_SIZE_MAX_CONTENT:
                            growth_from_max = calculate_track_intrinsic_size(grid_layout, i, true, GRID_TRACK_SIZE_MAX_CONTENT);
                            break;
                        case GRID_TRACK_SIZE_MIN_CONTENT:
                            growth_from_max = calculate_track_intrinsic_size(grid_layout, i, true, GRID_TRACK_SIZE_MIN_CONTENT);
                            break;
                        case GRID_TRACK_SIZE_FR:
                            growth_from_max = INFINITY;
                            track->is_flexible = true;
                            break;
                        default:
                            growth_from_max = INFINITY;
                            break;
                    }
                }
                track->growth_limit = growth_from_max;

                if (min_size) {
                    switch (min_size->type) {
                        case GRID_TRACK_SIZE_LENGTH:
                            track->base_size = min_size->value;
                            break;
                        case GRID_TRACK_SIZE_PERCENTAGE:
                            if (grid_layout->content_height > 0) {
                                track->base_size = (grid_layout->content_height * min_size->value) / 100;
                            } else {
                                track->base_size = 0;
                            }
                            break;
                        case GRID_TRACK_SIZE_AUTO: {
                            int min_content = calculate_track_intrinsic_size(grid_layout, i, true, GRID_TRACK_SIZE_MIN_CONTENT);
                            if (growth_from_max != INFINITY && growth_from_max < min_content) {
                                track->base_size = (int)growth_from_max;
                            } else {
                                track->base_size = min_content;
                            }
                            break;
                        }
                        case GRID_TRACK_SIZE_MIN_CONTENT:
                            track->base_size = calculate_track_intrinsic_size(grid_layout, i, true, GRID_TRACK_SIZE_MIN_CONTENT);
                            break;
                        case GRID_TRACK_SIZE_MAX_CONTENT:
                            track->base_size = calculate_track_intrinsic_size(grid_layout, i, true, GRID_TRACK_SIZE_MAX_CONTENT);
                            break;
                        default:
                            track->base_size = 0;
                            break;
                    }
                } else {
                    track->base_size = 0;
                }

                if (track->growth_limit < track->base_size) {
                    track->growth_limit = track->base_size;
                }

                log_debug("minmax row track %d: base=%d, growth=%.1f", i, track->base_size, track->growth_limit);
                break;
            }

            default:
                track->base_size = 0;
                track->growth_limit = 0;
                break;
        }

        track->computed_size = track->base_size;
    }

    log_debug("Intrinsic row sizes resolved\n");
}

// Legacy combined function (still available for compatibility)
void resolve_intrinsic_track_sizes(GridContainerLayout* grid_layout) {
    if (!grid_layout) return;
    resolve_intrinsic_column_sizes(grid_layout);
    resolve_intrinsic_row_sizes(grid_layout);
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

    // For rows, only distribute if container has explicit height set (not auto height)
    // Auto-height grid containers should size rows to content, not stretch to fill parent
    if (grid_layout->has_explicit_height && grid_layout->content_height > 0) {
        int row_gap_total = (grid_layout->computed_row_count - 1) * grid_layout->row_gap;
        int available = grid_layout->content_height - row_gap_total;
        distribute_space_to_auto_tracks_in_axis(grid_layout->computed_rows,
                                                  grid_layout->computed_row_count,
                                                  available);
    } else {
        log_debug(" Skipping row space distribution - container has auto height\n");
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

    // For rows, only expand fr tracks if container has explicit height
    // CSS Grid spec: fr units in auto-height containers resolve to min-content
    bool expand_row_fr = grid_layout->has_explicit_height;
    if (!expand_row_fr) {
        log_debug(" Skipping row fr expansion - container has auto height\n");
    }

    // Subtract space used by non-flexible tracks and gaps
    if (expand_row_fr) {
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
    }

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
    if (expand_row_fr) {
        expand_flexible_tracks_in_axis(grid_layout->computed_rows, grid_layout->computed_row_count, available_row_space);
    }
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
        if (tracks[i].is_flexible && tracks[i].size) {
            float fr_value = 0;
            if (tracks[i].size->type == GRID_TRACK_SIZE_FR) {
                // Direct fr value - convert from stored integer (multiplied by 100) back to float
                fr_value = tracks[i].size->value / 100.0f;
            } else if (tracks[i].size->type == GRID_TRACK_SIZE_MINMAX && tracks[i].size->max_size) {
                // minmax with fr max - extract fr value from max_size
                if (tracks[i].size->max_size->type == GRID_TRACK_SIZE_FR) {
                    fr_value = tracks[i].size->max_size->value / 100.0f;
                }
            }
            if (fr_value > 0) {
                total_fr += fr_value;
                flexible_count++;
                log_debug(" Track %d is flexible: %.2ffr\n", i, fr_value);
            } else {
                log_debug(" Track %d marked flexible but no fr value found: type=%d\n",
                       i, tracks[i].size->type);
            }
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
        if (track->is_flexible && track->size) {
            float fr_value = 0;
            if (track->size->type == GRID_TRACK_SIZE_FR) {
                // Direct fr value
                fr_value = track->size->value / 100.0f;
            } else if (track->size->type == GRID_TRACK_SIZE_MINMAX && track->size->max_size) {
                // minmax with fr max
                if (track->size->max_size->type == GRID_TRACK_SIZE_FR) {
                    fr_value = track->size->max_size->value / 100.0f;
                }
            }
            if (fr_value > 0) {
                track->computed_size = (int)(fr_value * fr_size);
                log_debug(" Flexible track %d: %.2ffr × %.2f = %dpx\n", i, fr_value, fr_size, track->computed_size);
                log_debug("Flexible track %d: %.2ffr = %dpx\n", i, fr_value, track->computed_size);
            }
        }
    }
}

// Main track sizing algorithm
// CSS Grid spec §11: Column sizing should complete before row sizing
// because row heights depend on column widths (for text wrapping).
void resolve_track_sizes(GridContainerLayout* grid_layout, ViewBlock* container) {
    if (!grid_layout || !container) return;

    log_debug("Resolving track sizes\n");

    // Phase 1: Initialize all track sizes
    initialize_track_sizes(grid_layout);

    // ========================================================
    // Phase 2: Complete column sizing FIRST
    // ========================================================
    log_debug("Phase 2: Sizing columns\n");

    // 2a: Resolve column intrinsic sizes
    resolve_intrinsic_column_sizes(grid_layout);

    // 2b: Maximize column tracks
    for (int i = 0; i < grid_layout->computed_column_count; i++) {
        GridTrack* track = &grid_layout->computed_columns[i];
        if (!track->is_flexible && track->growth_limit != INFINITY) {
            track->computed_size = track->growth_limit;
        }
    }

    // 2c: Expand flexible column tracks (FR units)
    if (grid_layout->content_width > 0) {
        int col_gap_total = (grid_layout->computed_column_count - 1) * grid_layout->column_gap;
        int available_col_space = grid_layout->content_width - col_gap_total;

        // Subtract space used by non-flexible tracks (sized in step 2b)
        for (int i = 0; i < grid_layout->computed_column_count; i++) {
            GridTrack* track = &grid_layout->computed_columns[i];
            if (!track->is_flexible) {
                available_col_space -= track->computed_size;
            }
        }

        expand_flexible_tracks_in_axis(grid_layout->computed_columns,
                                        grid_layout->computed_column_count,
                                        available_col_space);
    }

    // 2d: Distribute remaining space to auto column tracks
    if (grid_layout->content_width > 0) {
        int col_gap_total = (grid_layout->computed_column_count - 1) * grid_layout->column_gap;
        int available = grid_layout->content_width - col_gap_total;
        distribute_space_to_auto_tracks_in_axis(grid_layout->computed_columns,
                                                  grid_layout->computed_column_count,
                                                  available);
    }

    log_debug("Columns fully sized, now sizing rows\n");

    // ========================================================
    // Phase 3: Complete row sizing (columns are now known)
    // ========================================================
    log_debug("Phase 3: Sizing rows\n");

    // 3a: Resolve row intrinsic sizes (will use column sizes for height calc)
    resolve_intrinsic_row_sizes(grid_layout);

    // 3b: Maximize row tracks
    for (int i = 0; i < grid_layout->computed_row_count; i++) {
        GridTrack* track = &grid_layout->computed_rows[i];
        if (!track->is_flexible && track->growth_limit != INFINITY) {
            track->computed_size = track->growth_limit;
        }
    }

    // 3c: Expand flexible row tracks (FR units)
    if (grid_layout->has_explicit_height && grid_layout->content_height > 0) {
        int row_gap_total = (grid_layout->computed_row_count - 1) * grid_layout->row_gap;
        int available_row_space = grid_layout->content_height - row_gap_total;

        // Subtract space used by non-flexible tracks (sized in step 3b)
        for (int i = 0; i < grid_layout->computed_row_count; i++) {
            GridTrack* track = &grid_layout->computed_rows[i];
            if (!track->is_flexible) {
                available_row_space -= track->computed_size;
            }
        }

        expand_flexible_tracks_in_axis(grid_layout->computed_rows,
                                        grid_layout->computed_row_count,
                                        available_row_space);
    }

    // 3d: Distribute remaining space to auto row tracks (only if explicit height)
    if (grid_layout->has_explicit_height && grid_layout->content_height > 0) {
        int row_gap_total = (grid_layout->computed_row_count - 1) * grid_layout->row_gap;
        int available = grid_layout->content_height - row_gap_total;
        distribute_space_to_auto_tracks_in_axis(grid_layout->computed_rows,
                                                  grid_layout->computed_row_count,
                                                  available);
    }

    log_debug("Track sizes resolved\n");
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
    radiant::grid_adapter::run_enhanced_track_sizing(
        grid_layout,
        grid_layout->grid_items,
        grid_layout->item_count,
        sizing_width,
        static_cast<float>(grid_layout->content_height)
    );

    // For shrink-to-fit containers, update content_width based on resolved track sizes
    if (grid_layout->is_shrink_to_fit_width && grid_layout->computed_column_count > 0) {
        float total_column_width = 0;
        for (int i = 0; i < grid_layout->computed_column_count; i++) {
            total_column_width += grid_layout->computed_columns[i].computed_size;
        }
        // Add gaps between columns
        if (grid_layout->computed_column_count > 1) {
            total_column_width += grid_layout->column_gap * (grid_layout->computed_column_count - 1);
        }
        grid_layout->content_width = (int)total_column_width;
        log_debug("GRID shrink-to-fit: updated content_width to %d\n", grid_layout->content_width);
    }

    log_debug("Track sizes resolved (enhanced algorithm)\n");
}
