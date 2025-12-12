#pragma once

/**
 * Grid Enhanced Adapter
 *
 * Bridges the existing Radiant grid types (GridTrack, GridTrackSize, etc.)
 * with the new enhanced grid infrastructure (EnhancedGridTrack, TrackSizingFunction,
 * CellOccupancyMatrix, etc.) adapted from Taffy.
 *
 * This adapter provides:
 * 1. Type conversion functions between old and new representations
 * 2. Integration helpers that use the new algorithms with existing data structures
 * 3. Migration path utilities for incremental adoption
 */

// IMPORTANT: Include grid.hpp first, then undef macros, then include enhanced headers
// This is necessary because grid.hpp includes view.hpp which defines min/max macros
#include "grid.hpp"

// Undefine min/max macros before including enhanced grid headers
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "grid_types.hpp"
#include "grid_track.hpp"
#include "grid_occupancy.hpp"
#include "grid_placement.hpp"
#include "grid_sizing_algorithm.hpp"

#include <vector>
#include <optional>
#include <algorithm>

namespace radiant {
namespace grid_adapter {

// Import types from grid namespace for convenience
using namespace grid;

// ============================================================================
// Type Conversion: Old → New
// ============================================================================

/**
 * Convert old GridTrackSizeType to new MinTrackSizingFunction
 */
inline MinTrackSizingFunction convert_to_min_sizing(GridTrackSize* old_size) {
    if (!old_size) {
        return MinTrackSizingFunction::Auto();
    }

    switch (old_size->type) {
        case GRID_TRACK_SIZE_LENGTH:
            return MinTrackSizingFunction::Length(static_cast<float>(old_size->value));

        case GRID_TRACK_SIZE_PERCENTAGE:
            return MinTrackSizingFunction::Percent(static_cast<float>(old_size->value) / 100.0f);

        case GRID_TRACK_SIZE_MIN_CONTENT:
            return MinTrackSizingFunction::MinContent();

        case GRID_TRACK_SIZE_MAX_CONTENT:
            return MinTrackSizingFunction::MaxContent();

        case GRID_TRACK_SIZE_AUTO:
            return MinTrackSizingFunction::Auto();

        case GRID_TRACK_SIZE_FR:
            // fr is not valid for min sizing - treat as auto
            return MinTrackSizingFunction::Auto();

        case GRID_TRACK_SIZE_FIT_CONTENT:
            // fit-content min = min-content
            return MinTrackSizingFunction::MinContent();

        case GRID_TRACK_SIZE_MINMAX:
            // Recurse on min_size
            return convert_to_min_sizing(old_size->min_size);

        default:
            return MinTrackSizingFunction::Auto();
    }
}

/**
 * Convert old GridTrackSizeType to new MaxTrackSizingFunction
 */
inline MaxTrackSizingFunction convert_to_max_sizing(GridTrackSize* old_size) {
    if (!old_size) {
        return MaxTrackSizingFunction::Auto();
    }

    switch (old_size->type) {
        case GRID_TRACK_SIZE_LENGTH:
            return MaxTrackSizingFunction::Length(static_cast<float>(old_size->value));

        case GRID_TRACK_SIZE_PERCENTAGE:
            return MaxTrackSizingFunction::Percent(static_cast<float>(old_size->value) / 100.0f);

        case GRID_TRACK_SIZE_MIN_CONTENT:
            return MaxTrackSizingFunction::MinContent();

        case GRID_TRACK_SIZE_MAX_CONTENT:
            return MaxTrackSizingFunction::MaxContent();

        case GRID_TRACK_SIZE_AUTO:
            return MaxTrackSizingFunction::Auto();

        case GRID_TRACK_SIZE_FR:
            return MaxTrackSizingFunction::Fr(static_cast<float>(old_size->value));

        case GRID_TRACK_SIZE_FIT_CONTENT:
            return MaxTrackSizingFunction::FitContentPx(static_cast<float>(old_size->fit_content_limit));

        case GRID_TRACK_SIZE_MINMAX:
            // Recurse on max_size
            return convert_to_max_sizing(old_size->max_size);

        default:
            return MaxTrackSizingFunction::Auto();
    }
}

/**
 * Convert old GridTrackSize to new TrackSizingFunction
 */
inline TrackSizingFunction convert_to_track_sizing(GridTrackSize* old_size) {
    return TrackSizingFunction(
        convert_to_min_sizing(old_size),
        convert_to_max_sizing(old_size)
    );
}

/**
 * Convert old GridTrack to new EnhancedGridTrack
 */
inline EnhancedGridTrack convert_to_enhanced_track(GridTrack* old_track) {
    if (!old_track) {
        return EnhancedGridTrack(TrackSizingFunction::Auto());
    }

    EnhancedGridTrack enhanced(convert_to_track_sizing(old_track->size));

    // Transfer existing computed values if available
    enhanced.base_size = static_cast<float>(old_track->base_size);
    enhanced.growth_limit = old_track->growth_limit;

    return enhanced;
}

/**
 * Convert old GridContainerLayout tracks to vector of EnhancedGridTrack
 */
inline std::vector<EnhancedGridTrack> convert_tracks_to_enhanced(
    GridTrack* old_tracks, int count)
{
    std::vector<EnhancedGridTrack> result;
    result.reserve(count);

    for (int i = 0; i < count; i++) {
        result.push_back(convert_to_enhanced_track(&old_tracks[i]));
    }

    return result;
}

// ============================================================================
// Type Conversion: New → Old
// ============================================================================

/**
 * Copy EnhancedGridTrack computed values back to old GridTrack
 */
inline void copy_enhanced_to_old(const EnhancedGridTrack& enhanced, GridTrack* old_track) {
    if (!old_track) return;

    old_track->base_size = static_cast<int>(enhanced.base_size);
    old_track->growth_limit = enhanced.growth_limit;
    old_track->computed_size = static_cast<int>(enhanced.base_size);
    old_track->is_flexible = enhanced.max_track_sizing_function.is_fr();
}

/**
 * Copy vector of EnhancedGridTrack back to old GridTrack array
 */
inline void copy_enhanced_tracks_to_old(
    const std::vector<EnhancedGridTrack>& enhanced_tracks,
    GridTrack* old_tracks, int count)
{
    int copy_count = std::min(static_cast<int>(enhanced_tracks.size()), count);
    for (int i = 0; i < copy_count; i++) {
        copy_enhanced_to_old(enhanced_tracks[i], &old_tracks[i]);
    }
}

// ============================================================================
// Coordinate System Conversion
// ============================================================================

/**
 * Convert 1-based CSS line number to GridLine
 */
inline GridLine css_line_to_grid_line(int css_line) {
    return GridLine(static_cast<int16_t>(css_line));
}

/**
 * Convert GridLine to 1-based CSS line number
 */
inline int grid_line_to_css_line(GridLine line) {
    return static_cast<int>(line.as_i16());
}

// ============================================================================
// Grid Item Info Extraction
// ============================================================================

/**
 * Get span from start/end values (handles negative = span N convention)
 */
inline int get_span_value(int start, int end) {
    if (start > 0 && end > 0) {
        return end - start;
    } else if (start > 0 && end < 0) {
        return -end;  // end is negative span
    } else if (end < 0) {
        return -end;  // end is negative span
    }
    return 1;  // default span
}

/**
 * Extract GridItemInfo from ViewBlock with GridItemProp
 */
inline GridItemInfo extract_grid_item_info(ViewBlock* item, int item_index) {
    GridItemInfo info;
    info.item_index = item_index;

    if (!item || !item->gi) {
        // No grid item properties - fully auto-placed
        info.column = GridPlacement::Auto(1);
        info.row = GridPlacement::Auto(1);
        return info;
    }

    GridItemProp* gi = item->gi;

    // Column placement
    int col_start = gi->grid_column_start;
    int col_end = gi->grid_column_end;
    int col_span = get_span_value(col_start, col_end);

    if (col_start > 0) {
        info.column = GridPlacement::FromStartSpan(static_cast<int16_t>(col_start),
                                                    static_cast<uint16_t>(col_span));
    } else if (col_end < 0) {
        // span only - auto placement with span
        info.column = GridPlacement::Auto(static_cast<uint16_t>(-col_end));
    } else {
        info.column = GridPlacement::Auto(1);
    }

    // Row placement
    int row_start = gi->grid_row_start;
    int row_end = gi->grid_row_end;
    int row_span = get_span_value(row_start, row_end);

    if (row_start > 0) {
        info.row = GridPlacement::FromStartSpan(static_cast<int16_t>(row_start),
                                                 static_cast<uint16_t>(row_span));
    } else if (row_end < 0) {
        // span only - auto placement with span
        info.row = GridPlacement::Auto(static_cast<uint16_t>(-row_end));
    } else {
        info.row = GridPlacement::Auto(1);
    }

    return info;
}

/**
 * Apply placement result back to ViewBlock's GridItemProp
 */
inline void apply_placement_to_item(ViewBlock* item, const GridItemInfo& info) {
    if (!item || !item->gi) return;

    GridItemProp* gi = item->gi;

    // Get resolved positions from the LineSpan (OriginZeroLine has .value member)
    int col_start = info.resolved_column.start.value + 1;  // Convert to 1-based
    int col_end = info.resolved_column.end.value + 1;
    int row_start = info.resolved_row.start.value + 1;
    int row_end = info.resolved_row.end.value + 1;

    gi->computed_grid_column_start = col_start;
    gi->computed_grid_column_end = col_end;
    gi->computed_grid_row_start = row_start;
    gi->computed_grid_row_end = row_end;
    gi->is_grid_auto_placed = false;  // Now placed
}

// ============================================================================
// Integrated Placement Algorithm
// ============================================================================

/**
 * Enhanced grid item placement using CellOccupancyMatrix
 *
 * This function provides an enhanced placement algorithm using the
 * collision-aware CellOccupancyMatrix adapted from Taffy.
 *
 * @param grid_layout The existing GridContainerLayout
 * @param items Array of ViewBlock* grid items
 * @param item_count Number of items
 * @param auto_flow Grid auto flow direction (CSS_VALUE_ROW or CSS_VALUE_COLUMN)
 * @param is_dense Whether to use dense packing
 */
inline void place_items_with_occupancy(
    GridContainerLayout* grid_layout,
    ViewBlock** items,
    int item_count,
    int auto_flow,
    bool is_dense)
{
    if (!grid_layout || !items || item_count == 0) return;

    // Convert auto flow to our enum
    GridAutoFlow flow;
    if (auto_flow == CSS_VALUE_COLUMN) {
        flow = is_dense ? GridAutoFlow::ColumnDense : GridAutoFlow::Column;
    } else {
        flow = is_dense ? GridAutoFlow::RowDense : GridAutoFlow::Row;
    }

    // Extract item info
    std::vector<GridItemInfo> item_infos;
    item_infos.reserve(item_count);
    for (int i = 0; i < item_count; i++) {
        item_infos.push_back(extract_grid_item_info(items[i], i));
    }

    // Per CSS Grid spec: If there's no explicit grid-template-columns,
    // the grid defaults to a single column. Items flow row-by-row.
    // Similarly, if there's no explicit rows and flow is column-first,
    // default to a single row.
    uint16_t effective_col_count = static_cast<uint16_t>(grid_layout->explicit_column_count);
    uint16_t effective_row_count = static_cast<uint16_t>(grid_layout->explicit_row_count);

    // When auto-flow is row (default), we need at least 1 column to start
    // When auto-flow is column, we need at least 1 row to start
    // BUT: When there are no explicit tracks in the flow direction,
    // the grid should expand implicitly based on auto tracks
    if (auto_flow != CSS_VALUE_COLUMN && effective_col_count == 0) {
        effective_col_count = 1;  // Default to single column for row-flow
    }
    if (auto_flow == CSS_VALUE_COLUMN && effective_row_count == 0) {
        effective_row_count = 1;  // Default to single row for column-flow
    }
    // For column-flow: if no explicit columns but we have grid-auto-columns,
    // set effective_col_count to 0 to allow implicit expansion
    // (The occupancy matrix will handle implicit column creation)
    if (auto_flow == CSS_VALUE_COLUMN && effective_col_count == 0 &&
        grid_layout->grid_auto_columns && grid_layout->grid_auto_columns->track_count > 0) {
        // Leave effective_col_count as 0 - the matrix will create implicit columns
    }

    // Create initial track counts from effective grid
    TrackCounts col_counts(0, effective_col_count, 0);
    TrackCounts row_counts(0, effective_row_count, 0);

    // Create occupancy matrix
    CellOccupancyMatrix matrix(col_counts, row_counts);

    // Run the placement algorithm
    place_grid_items(
        matrix,
        item_infos,
        flow,
        effective_row_count,
        effective_col_count
    );

    // Apply results back to ViewBlocks
    for (size_t i = 0; i < item_infos.size(); i++) {
        apply_placement_to_item(items[i], item_infos[i]);
    }

    // Update grid layout with final track counts from matrix
    TrackCounts final_col_counts = matrix.track_counts(AbsoluteAxis::Horizontal);
    TrackCounts final_row_counts = matrix.track_counts(AbsoluteAxis::Vertical);

    int total_columns = final_col_counts.negative_implicit +
                        final_col_counts.explicit_count +
                        final_col_counts.positive_implicit;
    int total_rows = final_row_counts.negative_implicit +
                     final_row_counts.explicit_count +
                     final_row_counts.positive_implicit;

    grid_layout->implicit_column_count = total_columns - grid_layout->explicit_column_count;
    grid_layout->implicit_row_count = total_rows - grid_layout->explicit_row_count;
    grid_layout->computed_column_count = total_columns;
    grid_layout->computed_row_count = total_rows;
}

// ============================================================================
// Integrated Track Sizing Algorithm
// ============================================================================

/**
 * Run the enhanced track sizing algorithm
 *
 * @param grid_layout The existing GridContainerLayout
 * @param items Array of ViewBlock* grid items
 * @param item_count Number of items
 * @param container_width Available width for columns
 * @param container_height Available height for rows (may be indefinite)
 */
inline void run_enhanced_track_sizing(
    GridContainerLayout* grid_layout,
    ViewBlock** items,
    int item_count,
    float container_width,
    float container_height)
{
    if (!grid_layout) return;

    // Convert existing tracks to enhanced format
    std::vector<EnhancedGridTrack> col_tracks =
        convert_tracks_to_enhanced(grid_layout->computed_columns,
                                   grid_layout->computed_column_count);
    std::vector<EnhancedGridTrack> row_tracks =
        convert_tracks_to_enhanced(grid_layout->computed_rows,
                                   grid_layout->computed_row_count);

    // Run track sizing for columns
    if (!col_tracks.empty()) {
        initialize_track_sizes(col_tracks, container_width);
        maximize_tracks(col_tracks, container_width, container_width);
        expand_flexible_tracks(col_tracks, 0.0f, container_width, container_width);
        stretch_auto_tracks(col_tracks, 0.0f, container_width);
        compute_track_offsets(col_tracks, grid_layout->column_gap);
    }

    // Run track sizing for rows (may use indefinite height)
    if (!row_tracks.empty()) {
        float row_space = container_height > 0 ? container_height : -1.0f;
        initialize_track_sizes(row_tracks, row_space);
        if (container_height > 0) {
            maximize_tracks(row_tracks, row_space, row_space);
            expand_flexible_tracks(row_tracks, 0.0f, row_space, row_space);
            stretch_auto_tracks(row_tracks, 0.0f, row_space);
        }
        compute_track_offsets(row_tracks, grid_layout->row_gap);
    }

    // Copy results back to old track structures
    copy_enhanced_tracks_to_old(col_tracks, grid_layout->computed_columns,
                                 grid_layout->computed_column_count);
    copy_enhanced_tracks_to_old(row_tracks, grid_layout->computed_rows,
                                 grid_layout->computed_row_count);
}

} // namespace grid_adapter
} // namespace radiant
