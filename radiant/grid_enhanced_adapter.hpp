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
 *
 * TODO: std::* Migration Plan (Phase 5+) - COMPLEX
 * - std::vector<EnhancedGridTrack> → Pool-allocated arrays with count
 * - std::vector<GridItemInfo> → ArrayList* or fixed arrays
 * - std::vector<GridItemContribution> → ArrayList*
 * - std::pair<int, int> → Simple struct { int first; int second; }
 * - std::min/std::max → MIN_INT/MAX_INT macros
 * - std::optional (if used) → Pointer + null check pattern
 * Estimated effort: Major refactoring (~400+ lines)
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
            // old_size->value is the percentage (e.g., 10 for 10%)
            // MinTrackSizingFunction::Percent expects it as-is (resolve will divide by 100)
            return MinTrackSizingFunction::Percent(static_cast<float>(old_size->value));

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
            // old_size->value is the percentage (e.g., 20 for 20%)
            // MaxTrackSizingFunction::Percent expects it as-is (resolve will divide by 100)
            return MaxTrackSizingFunction::Percent(static_cast<float>(old_size->value));

        case GRID_TRACK_SIZE_MIN_CONTENT:
            return MaxTrackSizingFunction::MinContent();

        case GRID_TRACK_SIZE_MAX_CONTENT:
            return MaxTrackSizingFunction::MaxContent();

        case GRID_TRACK_SIZE_AUTO:
            return MaxTrackSizingFunction::Auto();

        case GRID_TRACK_SIZE_FR:
            return MaxTrackSizingFunction::Fr(static_cast<float>(old_size->value));

        case GRID_TRACK_SIZE_FIT_CONTENT:
            if (old_size->is_percentage) {
                return MaxTrackSizingFunction::FitContentPercent(static_cast<float>(old_size->fit_content_limit));
            } else {
                return MaxTrackSizingFunction::FitContentPx(static_cast<float>(old_size->fit_content_limit));
            }

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
 * Convert old GridTrack (from grid.hpp) to new EnhancedGridTrack
 */
inline EnhancedGridTrack convert_to_enhanced_track(::GridTrack* old_track) {
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
    ::GridTrack* old_tracks, int count)
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
 * Copy EnhancedGridTrack computed values back to old GridTrack (from grid.hpp)
 */
inline void copy_enhanced_to_old(const EnhancedGridTrack& enhanced, ::GridTrack* old_track) {
    if (!old_track) return;

    old_track->base_size = static_cast<int>(enhanced.base_size);
    old_track->growth_limit = enhanced.growth_limit;
    old_track->computed_size = static_cast<int>(enhanced.base_size);
    old_track->is_flexible = enhanced.max_track_sizing_function.is_fr();
}

/**
 * Copy vector of EnhancedGridTrack back to old GridTrack array (from grid.hpp)
 */
inline void copy_enhanced_tracks_to_old(
    const std::vector<EnhancedGridTrack>& enhanced_tracks,
    ::GridTrack* old_tracks, int count)
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
 * Get span from start/end values
 * @param start Start line number
 * @param end End line number
 * @param end_is_span True if negative end value means "span N", false if it's a negative line number
 * @param explicit_track_count Number of explicit columns (needed to resolve negative line numbers)
 */
inline int get_span_value_ex(int start, int end, bool end_is_span, int explicit_track_count) {
    if (start > 0 && end > 0) {
        return end - start;
    } else if (start > 0 && end < 0) {
        if (end_is_span) {
            return -end;  // end is negative span value (e.g., "span 2" stored as -2)
        } else {
            // end is negative line number (e.g., -1 = last line)
            // For now, return span 1 - actual resolution happens later in resolve_negative_lines()
            // This is a placeholder; the real span will be calculated after implicit grid is known
            return 1;
        }
    } else if (end < 0 && end_is_span) {
        return -end;  // span only (auto start)
    }
    return 1;  // default span
}

/**
 * Resolve a negative line number against the total track count.
 * CSS Grid spec: -1 = last line, -2 = second to last, etc.
 * With N tracks, there are N+1 lines numbered 1 to N+1.
 * -1 = line N+1, -2 = line N, etc.
 *
 * @param negative_line The negative line number (e.g., -1, -2)
 * @param total_track_count Total number of tracks (explicit + implicit)
 * @return Resolved positive line number (1-indexed)
 */
inline int resolve_negative_line(int negative_line, int total_track_count) {
    // total_track_count tracks means lines 1 to total_track_count+1
    // -1 = total_track_count + 1 (last line)
    // -2 = total_track_count (second to last)
    // Formula: resolved = total_track_count + 2 + negative_line
    int resolved = total_track_count + 2 + negative_line;
    // Ensure we don't go below line 1
    if (resolved < 1) resolved = 1;
    return resolved;
}

/**
 * Legacy get_span_value - assumes negative end is always span
 * @deprecated Use get_span_value_ex with is_span flag instead
 */
inline int get_span_value(int start, int end) {
    if (start > 0 && end > 0) {
        return end - start;
    } else if (start > 0 && end < 0) {
        return -end;  // end is negative span (LEGACY: doesn't distinguish from negative line)
    } else if (end < 0) {
        return -end;  // end is negative span
    }
    return 1;  // default span
}

/**
 * Extract GridItemInfo from ViewBlock with GridItemProp
 * @param item The ViewBlock to extract info from
 * @param item_index Index of this item in the items array
 * @param explicit_col_count Number of explicit columns (for resolving negative lines)
 * @param explicit_row_count Number of explicit rows (for resolving negative lines)
 */
inline GridItemInfo extract_grid_item_info(ViewBlock* item, int item_index,
                                           int explicit_col_count = 0, int explicit_row_count = 0) {
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
    bool col_end_is_span = gi->grid_column_end_is_span;
    bool col_start_is_span = gi->grid_column_start_is_span;

    if (col_start < 0 && col_end < 0 && !col_end_is_span && !col_start_is_span) {
        // "-N / -M" - both start and end are negative line numbers
        // Defer resolution until we know the grid size
        info.column = GridPlacement::FromNegativeLines(
            static_cast<int16_t>(col_start),
            static_cast<int16_t>(col_end));
    } else if (col_start > 0 && col_end < 0 && !col_end_is_span) {
        // "N / -M" - explicit start, negative line number end
        // Defer resolution until we know the grid size
        info.column = GridPlacement::FromStartNegativeEnd(
            static_cast<int16_t>(col_start),
            static_cast<int16_t>(col_end));
    } else if (col_start != 0 && !col_start_is_span) {
        // Definite start position (positive line number)
        int col_span = get_span_value_ex(col_start, col_end, col_end_is_span, explicit_col_count);
        info.column = GridPlacement::FromStartSpan(static_cast<int16_t>(col_start),
                                                    static_cast<uint16_t>(col_span));
    } else if (col_end < 0 && col_end_is_span) {
        // span only - auto placement with span
        info.column = GridPlacement::Auto(static_cast<uint16_t>(-col_end));
    } else {
        info.column = GridPlacement::Auto(1);
    }

    // Row placement
    int row_start = gi->grid_row_start;
    int row_end = gi->grid_row_end;
    bool row_end_is_span = gi->grid_row_end_is_span;
    bool row_start_is_span = gi->grid_row_start_is_span;

    if (row_start < 0 && row_end < 0 && !row_end_is_span && !row_start_is_span) {
        // "-N / -M" - both start and end are negative line numbers
        // Defer resolution until we know the grid size
        info.row = GridPlacement::FromNegativeLines(
            static_cast<int16_t>(row_start),
            static_cast<int16_t>(row_end));
    } else if (row_start > 0 && row_end < 0 && !row_end_is_span) {
        // "N / -M" - explicit start, negative line number end
        // Defer resolution until we know the grid size
        info.row = GridPlacement::FromStartNegativeEnd(
            static_cast<int16_t>(row_start),
            static_cast<int16_t>(row_end));
    } else if (row_start != 0 && !row_start_is_span) {
        // Definite start position (positive line number)
        int row_span = get_span_value_ex(row_start, row_end, row_end_is_span, explicit_row_count);
        info.row = GridPlacement::FromStartSpan(static_cast<int16_t>(row_start),
                                                 static_cast<uint16_t>(row_span));
    } else if (row_end < 0 && row_end_is_span) {
        // span only - auto placement with span
        info.row = GridPlacement::Auto(static_cast<uint16_t>(-row_end));
    } else {
        info.row = GridPlacement::Auto(1);
    }

    return info;
}

/**
 * Apply placement result back to ViewBlock's GridItemProp
 *
 * @param item The ViewBlock to update
 * @param info The placement result
 * @param neg_col_offset Negative implicit column offset (to shift OriginZero to final coords)
 * @param neg_row_offset Negative implicit row offset
 */
inline void apply_placement_to_item(ViewBlock* item, const GridItemInfo& info,
                                    int neg_col_offset = 0, int neg_row_offset = 0) {
    if (!item || !item->gi) return;

    GridItemProp* gi = item->gi;

    // Convert from OriginZero coordinates to 1-based final grid coordinates
    // OriginZero(0) = first line of explicit grid
    // With N negative implicit tracks, the explicit grid starts at final line (N+1)
    // So: final_line = origin_zero_value + neg_implicit + 1
    int col_start = info.resolved_column.start.value + neg_col_offset + 1;
    int col_end = info.resolved_column.end.value + neg_col_offset + 1;
    int row_start = info.resolved_row.start.value + neg_row_offset + 1;
    int row_end = info.resolved_row.end.value + neg_row_offset + 1;

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
 * Resolve negative line numbers in item placements against the known grid size.
 * This should be called after the initial grid extent is determined from positive placements.
 *
 * @param items Vector of item infos to update
 * @param total_col_count Total column count (explicit + implicit)
 * @param total_row_count Total row count (explicit + implicit)
 */
inline void resolve_negative_lines_in_items(
    std::vector<GridItemInfo>& items,
    int total_col_count,
    int total_row_count)
{
    for (auto& item : items) {
        // Resolve column negative lines
        if (item.column.has_negative_start && item.column.has_negative_end) {
            // Both start and end are negative: "-N / -M"
            int resolved_start = resolve_negative_line(item.column.start, total_col_count);
            int resolved_end = resolve_negative_line(item.column.end, total_col_count);
            int span = resolved_end - resolved_start;
            if (span < 1) span = 1;
            item.column.start = static_cast<int16_t>(resolved_start);
            item.column.end = static_cast<int16_t>(resolved_end);
            item.column.span = static_cast<uint16_t>(span);
            item.column.has_negative_start = false;
            item.column.has_negative_end = false;
            item.column.is_definite = true;
        } else if (item.column.has_negative_end && item.column.start > 0) {
            // Only end is negative: "N / -M"
            int resolved_end = resolve_negative_line(item.column.end, total_col_count);
            int span = resolved_end - item.column.start;
            if (span < 1) span = 1;
            item.column.span = static_cast<uint16_t>(span);
            item.column.end = static_cast<int16_t>(resolved_end);
            item.column.has_negative_end = false;
        }

        // Resolve row negative lines
        if (item.row.has_negative_start && item.row.has_negative_end) {
            // Both start and end are negative: "-N / -M"
            int resolved_start = resolve_negative_line(item.row.start, total_row_count);
            int resolved_end = resolve_negative_line(item.row.end, total_row_count);
            int span = resolved_end - resolved_start;
            if (span < 1) span = 1;
            item.row.start = static_cast<int16_t>(resolved_start);
            item.row.end = static_cast<int16_t>(resolved_end);
            item.row.span = static_cast<uint16_t>(span);
            item.row.has_negative_start = false;
            item.row.has_negative_end = false;
            item.row.is_definite = true;
        } else if (item.row.has_negative_end && item.row.start > 0) {
            // Only end is negative: "N / -M"
            int resolved_end = resolve_negative_line(item.row.end, total_row_count);
            int span = resolved_end - item.row.start;
            if (span < 1) span = 1;
            item.row.span = static_cast<uint16_t>(span);
            item.row.end = static_cast<int16_t>(resolved_end);
            item.row.has_negative_end = false;
        }
    }
}

/**
 * Calculate initial grid extent from items with definite positive positions.
 * This determines how many tracks are needed before resolving negative lines.
 *
 * @param items Vector of item infos
 * @param explicit_col_count Number of explicit columns
 * @param explicit_row_count Number of explicit rows
 * @return pair of (max_col, max_row) needed
 */
inline std::pair<int, int> calculate_initial_grid_extent(
    const std::vector<GridItemInfo>& items,
    int explicit_col_count,
    int explicit_row_count)
{
    int max_col = explicit_col_count > 0 ? explicit_col_count : 1;
    int max_row = explicit_row_count > 0 ? explicit_row_count : 1;

    for (const auto& item : items) {
        // Items with positive definite start contribute their start position
        // Items with has_negative_start will be resolved after we know the grid size
        if (item.column.start > 0 && !item.column.has_negative_start) {
            // This item needs at least this column to exist
            if (item.column.start > max_col) {
                max_col = item.column.start;
            }
            // If we also know the span (no negative end), add full extent
            if (!item.column.has_negative_end) {
                int col_end = item.column.start + item.column.span;
                if (col_end > max_col + 1) max_col = col_end - 1;
            }
        }
        if (item.row.start > 0 && !item.row.has_negative_start) {
            // This item needs at least this row to exist
            if (item.row.start > max_row) {
                max_row = item.row.start;
            }
            // If we also know the span (no negative end), add full extent
            if (!item.row.has_negative_end) {
                int row_end = item.row.start + item.row.span;
                if (row_end > max_row + 1) max_row = row_end - 1;
            }
        }
    }

    return {max_col, max_row};
}

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

    // First, resolve named grid areas to line positions
    // This needs to happen before extract_grid_item_info
    log_debug("Resolving named grid areas: area_count=%d", grid_layout->area_count);
    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        if (!item || !item->gi || !item->gi->grid_area) continue;

        const char* area_name = item->gi->grid_area;
        log_debug("Item %d has grid_area='%s'", i, area_name);

        for (int j = 0; j < grid_layout->area_count; j++) {
            if (grid_layout->grid_areas[j].name &&
                strcmp(grid_layout->grid_areas[j].name, area_name) == 0) {
                // Found the area - set line positions
                item->gi->grid_row_start = grid_layout->grid_areas[j].row_start;
                item->gi->grid_row_end = grid_layout->grid_areas[j].row_end;
                item->gi->grid_column_start = grid_layout->grid_areas[j].column_start;
                item->gi->grid_column_end = grid_layout->grid_areas[j].column_end;
                item->gi->has_explicit_grid_row_start = true;
                item->gi->has_explicit_grid_row_end = true;
                item->gi->has_explicit_grid_column_start = true;
                item->gi->has_explicit_grid_column_end = true;
                log_debug("  Resolved to rows %d-%d, cols %d-%d",
                          item->gi->grid_row_start, item->gi->grid_row_end,
                          item->gi->grid_column_start, item->gi->grid_column_end);
                break;
            }
        }
    }

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
    int explicit_col_count = grid_layout->explicit_column_count;
    int explicit_row_count = grid_layout->explicit_row_count;
    for (int i = 0; i < item_count; i++) {
        item_infos.push_back(extract_grid_item_info(items[i], i, explicit_col_count, explicit_row_count));
    }

    // Step 1: Calculate initial grid extent from definite positive positions
    auto [initial_cols, initial_rows] = calculate_initial_grid_extent(
        item_infos, explicit_col_count, explicit_row_count);

    // Step 2: Resolve negative line numbers against the EXPLICIT grid only
    // CSS Grid spec §8.3: "Numeric indices count from the edges of the EXPLICIT grid."
    // If there's no explicit grid (0 tracks), -1 = line 1, -2 and beyond clamp to line 1.
    // This is the CORRECT spec behavior - negative lines NEVER reference implicit grid.
    int resolve_cols = explicit_col_count;  // Use explicit count, can be 0
    int resolve_rows = explicit_row_count;  // Use explicit count, can be 0
    resolve_negative_lines_in_items(item_infos, resolve_cols, resolve_rows);

    // Per CSS Grid spec: If there's no explicit grid-template-columns,
    // the grid defaults to a single column. Items flow row-by-row.
    uint16_t effective_col_count = static_cast<uint16_t>(grid_layout->explicit_column_count);
    uint16_t effective_row_count = static_cast<uint16_t>(grid_layout->explicit_row_count);

    // When auto-flow is row (default), we need at least 1 column to start
    // When auto-flow is column, we need at least 1 row to start
    if (auto_flow != CSS_VALUE_COLUMN && effective_col_count == 0) {
        effective_col_count = 1;  // Default to single column for row-flow
    }
    if (auto_flow == CSS_VALUE_COLUMN && effective_row_count == 0) {
        effective_row_count = 1;  // Default to single row for column-flow
    }
    // For column-flow: if no explicit columns but we have grid-auto-columns,
    // set effective_col_count to 0 to allow implicit expansion
    if (auto_flow == CSS_VALUE_COLUMN && effective_col_count == 0 &&
        grid_layout->grid_auto_columns && grid_layout->grid_auto_columns->track_count > 0) {
        // Leave effective_col_count as 0 - the matrix will create implicit columns
    }

    // Create initial track counts from effective grid
    TrackCounts col_counts(0, effective_col_count, 0);
    TrackCounts row_counts(0, effective_row_count, 0);

    // Create occupancy matrix
    CellOccupancyMatrix matrix(col_counts, row_counts);

    // Step 3: Run the placement algorithm with resolved negative lines
    place_grid_items(
        matrix,
        item_infos,
        flow,
        effective_row_count,
        effective_col_count
    );

    // Get final track counts from matrix (includes negative implicit tracks)
    TrackCounts final_col_counts = matrix.track_counts(AbsoluteAxis::Horizontal);
    TrackCounts final_row_counts = matrix.track_counts(AbsoluteAxis::Vertical);

    // Apply results back to ViewBlocks with offset for negative implicit tracks
    int neg_col_offset = final_col_counts.negative_implicit;
    int neg_row_offset = final_row_counts.negative_implicit;
    for (size_t i = 0; i < item_infos.size(); i++) {
        apply_placement_to_item(items[i], item_infos[i], neg_col_offset, neg_row_offset);
    }

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

    log_debug("place_items_with_occupancy results: computed_cols=%d, computed_rows=%d, explicit_cols=%d, explicit_rows=%d",
              grid_layout->computed_column_count, grid_layout->computed_row_count,
              grid_layout->explicit_column_count, grid_layout->explicit_row_count);

    // Store negative implicit counts for track sizing
    grid_layout->negative_implicit_row_count = final_row_counts.negative_implicit;
    grid_layout->negative_implicit_column_count = final_col_counts.negative_implicit;
}

// ============================================================================
// Integrated Track Sizing Algorithm
// ============================================================================

/**
 * Collect item contributions for intrinsic track sizing.
 * For each item, calculate its min/max content contribution in the given axis.
 *
 * @param grid_layout The grid container layout
 * @param items Array of grid items
 * @param item_count Number of items
 * @param is_column_axis True if sizing columns, false if sizing rows
 * @return Vector of item contributions
 */
inline std::vector<GridItemContribution> collect_item_contributions(
    GridContainerLayout* grid_layout,
    ViewBlock** items,
    int item_count,
    bool is_column_axis)
{
    std::vector<GridItemContribution> contributions;
    if (!grid_layout || !items || item_count <= 0) return contributions;

    contributions.reserve(item_count);

    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        if (!item || !item->gi) continue;

        GridItemContribution contrib;
        contrib.item = item;

        // Get item's placement (1-based line numbers from GridItemProp)
        if (is_column_axis) {
            int col_start = item->gi->computed_grid_column_start;
            int col_end = item->gi->computed_grid_column_end;
            if (col_start < 1 || col_end < 1) continue;

            contrib.track_start = col_start - 1;  // Convert to 0-based
            contrib.track_span = col_end - col_start;

            // Get intrinsic sizes from pre-computed measurements or calculate
            if (item->gi->has_measured_size &&
                (item->gi->measured_min_width > 0 || item->gi->measured_max_width > 0)) {
                contrib.min_content_contribution = item->gi->measured_min_width;
                contrib.max_content_contribution = item->gi->measured_max_width;
            } else {
                // Fallback: use calculate_grid_item_intrinsic_sizes
                IntrinsicSizes sizes = calculate_grid_item_intrinsic_sizes(
                    grid_layout->lycon, item, false /* is_row_axis = false for width */);
                contrib.min_content_contribution = sizes.min_content;
                contrib.max_content_contribution = sizes.max_content;
            }
        } else {
            // Row axis
            int row_start = item->gi->computed_grid_row_start;
            int row_end = item->gi->computed_grid_row_end;
            if (row_start < 1 || row_end < 1) continue;

            contrib.track_start = row_start - 1;  // Convert to 0-based
            contrib.track_span = row_end - row_start;

            // For row axis, ALWAYS calculate heights on-demand since they depend on
            // final column widths. measured_min_height/measured_max_height are not
            // populated during the measurement pass (only width is).
            // This follows CSS Grid spec §11.5 where row sizing happens after column sizing.
            IntrinsicSizes sizes = calculate_grid_item_intrinsic_sizes(
                grid_layout->lycon, item, true /* is_row_axis = true for height */);
            contrib.min_content_contribution = sizes.min_content;
            contrib.max_content_contribution = sizes.max_content;
        }

        // Only add if the contribution is meaningful
        if (contrib.track_span > 0 &&
            (contrib.min_content_contribution > 0 || contrib.max_content_contribution > 0)) {
            contributions.push_back(contrib);
        }
    }

    return contributions;
}

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

    log_debug("run_enhanced_track_sizing: container=%.1fx%.1f, cols=%d, rows=%d",
              container_width, container_height,
              grid_layout->computed_column_count, grid_layout->computed_row_count);

    // Convert existing tracks to enhanced format
    std::vector<EnhancedGridTrack> col_tracks =
        convert_tracks_to_enhanced(grid_layout->computed_columns,
                                   grid_layout->computed_column_count);
    std::vector<EnhancedGridTrack> row_tracks =
        convert_tracks_to_enhanced(grid_layout->computed_rows,
                                   grid_layout->computed_row_count);

    log_debug("  converted col_tracks.size=%zu, row_tracks.size=%zu",
              col_tracks.size(), row_tracks.size());

    // Calculate gap-adjusted available space (gaps reduce available space for tracks)
    float col_gap_total = (grid_layout->computed_column_count > 1)
        ? (grid_layout->computed_column_count - 1) * grid_layout->column_gap
        : 0.0f;
    float row_gap_total = (grid_layout->computed_row_count > 1)
        ? (grid_layout->computed_row_count - 1) * grid_layout->row_gap
        : 0.0f;

    float col_available = container_width - col_gap_total;
    float row_available = container_height > 0 ? container_height - row_gap_total : -1.0f;

    log_debug("  col_available=%.1f (container_width=%.1f - col_gap_total=%.1f)",
              col_available, container_width, col_gap_total);

    // === Run track sizing for columns ===
    if (!col_tracks.empty()) {
        // Log track types before sizing
        for (size_t i = 0; i < col_tracks.size(); i++) {
            log_debug("  col_track[%zu] before: base_size=%.1f",
                      i, col_tracks[i].base_size);
        }

        // 11.4 Initialize Track Sizes
        initialize_track_sizes(col_tracks, col_available);

        for (size_t i = 0; i < col_tracks.size(); i++) {
            log_debug("  col_track[%zu] after init: base_size=%.1f, growth_limit=%.1f",
                      i, col_tracks[i].base_size, col_tracks[i].growth_limit);
        }

        // 11.5 Resolve Intrinsic Track Sizes
        std::vector<GridItemContribution> col_contributions =
            collect_item_contributions(grid_layout, items, item_count, true /* is_column_axis */);
        log_debug("  col_contributions.size=%zu", col_contributions.size());
        if (!col_contributions.empty()) {
            resolve_intrinsic_track_sizes(col_tracks, col_contributions, grid_layout->column_gap);
        }

        // 11.6 Maximize Tracks
        maximize_tracks(col_tracks, col_available, col_available);

        // 11.7 Expand Flexible Tracks
        expand_flexible_tracks(col_tracks, 0.0f, col_available, col_available);

        for (size_t i = 0; i < col_tracks.size(); i++) {
            log_debug("  col_track[%zu] after expand: base_size=%.1f",
                      i, col_tracks[i].base_size);
        }

        // 11.8 Stretch auto Tracks
        stretch_auto_tracks(col_tracks, 0.0f, col_available);

        // Compute track positions
        compute_track_offsets(col_tracks, grid_layout->column_gap);

        // IMPORTANT: Copy column results back BEFORE sizing rows
        // Row sizing needs to know the final column widths to calculate item heights
        copy_enhanced_tracks_to_old(col_tracks, grid_layout->computed_columns,
                                     grid_layout->computed_column_count);
    }

    // === Run track sizing for rows ===
    if (!row_tracks.empty()) {
        // 11.4 Initialize Track Sizes
        initialize_track_sizes(row_tracks, row_available);

        // 11.5 Resolve Intrinsic Track Sizes
        // NOTE: This uses grid_layout->computed_columns which was just updated above
        std::vector<GridItemContribution> row_contributions =
            collect_item_contributions(grid_layout, items, item_count, false /* is_column_axis */);
        if (!row_contributions.empty()) {
            resolve_intrinsic_track_sizes(row_tracks, row_contributions, grid_layout->row_gap);
        }

        // Only do maximize/expand/stretch if we have definite space
        if (row_available > 0) {
            // 11.6 Maximize Tracks
            maximize_tracks(row_tracks, row_available, row_available);

            // 11.7 Expand Flexible Tracks
            expand_flexible_tracks(row_tracks, 0.0f, row_available, row_available);

            // 11.8 Stretch auto Tracks
            stretch_auto_tracks(row_tracks, 0.0f, row_available);
        }

        // Compute track positions
        compute_track_offsets(row_tracks, grid_layout->row_gap);

        // Copy row results back
        copy_enhanced_tracks_to_old(row_tracks, grid_layout->computed_rows,
                                     grid_layout->computed_row_count);
    }
}

} // namespace grid_adapter
} // namespace radiant
