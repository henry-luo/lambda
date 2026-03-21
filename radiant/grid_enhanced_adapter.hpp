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
 * - TrackArray → Pool-allocated arrays with count
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
            // CSS Grid §7.2.3.2: fit-content(N) = min(max-content, max(auto, N))
            // The MIN sizing function is auto, not min-content.
            // This distinction matters for scroll containers: auto min = 0,
            // while min-content min would always use the text min-content width.
            return MinTrackSizingFunction::Auto();

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
            // old_size->value stores the fr factor multiplied by 100 (e.g. 0.3fr → 30)
            return MaxTrackSizingFunction::Fr(static_cast<float>(old_size->value) / 100.0f);

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
inline TrackArray convert_tracks_to_enhanced(
    ::GridTrack* old_tracks, int count)
{
    TrackArray result;
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
 * Uses the "largest remainder" method to distribute any fractional pixel loss:
 * floor all track sizes first, then give +1px to the tracks with the largest
 * fractional parts until the integer total matches round(float total).
 * This prevents accumulated truncation errors (e.g. 4×17.5 → 158 instead of 160).
 */
inline void copy_enhanced_tracks_to_old(
    const TrackArray& enhanced_tracks,
    ::GridTrack* old_tracks, int count)
{
    int copy_count = std::min(static_cast<int>(enhanced_tracks.size()), count);

    // Pass 1: floor each track and copy non-size fields
    float float_total = 0.0f;
    int floor_total = 0;
    for (int i = 0; i < copy_count; i++) {
        float v = enhanced_tracks[i].base_size;
        int fv = static_cast<int>(v);           // floor (truncate towards zero)
        old_tracks[i].base_size     = fv;
        old_tracks[i].computed_size = fv;
        old_tracks[i].growth_limit  = enhanced_tracks[i].growth_limit;
        old_tracks[i].is_flexible   = enhanced_tracks[i].max_track_sizing_function.is_fr();
        float_total += v;
        floor_total += fv;
    }

    // Pass 2: distribute the fractional remainder to tracks with highest frac parts
    int remainder = static_cast<int>(roundf(float_total)) - floor_total;
    if (remainder > 0 && copy_count > 0) {
        // Build (fractional_part, index) pairs and sort descending
        // Use a simple selection approach to keep code minimal
        struct FracEntry { float frac; int idx; };
        FracEntry fracs[MAX_GRID_TRACKS];
        size_t frac_count = 0;
        fracs[0] = {};  // suppress potential uninitialized warning
        for (int i = 0; i < copy_count; i++) {
            float frac = enhanced_tracks[i].base_size
                       - static_cast<float>(static_cast<int>(enhanced_tracks[i].base_size));
            if (frac > 0.0f && frac_count < MAX_GRID_TRACKS) fracs[frac_count++] = {frac, i};
        }
        std::sort(fracs, fracs + frac_count,
                  [](const FracEntry& a, const FracEntry& b) {
                      return a.frac > b.frac;
                  });
        int to_add = std::min(remainder, static_cast<int>(frac_count));
        for (int j = 0; j < to_add; j++) {
            int idx = fracs[j].idx;
            old_tracks[idx].base_size++;
            old_tracks[idx].computed_size++;
        }
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
    ItemInfoArray& items,
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

struct GridExtent { int cols; int rows; };

/**
 * Calculate initial grid extent from items with definite positive positions.
 * This determines how many tracks are needed before resolving negative lines.
 *
 * @param items Array of item infos
 * @param explicit_col_count Number of explicit columns
 * @param explicit_row_count Number of explicit rows
 * @return GridExtent with max cols and rows needed
 */
inline GridExtent calculate_initial_grid_extent(
    const ItemInfoArray& items,
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
    ItemInfoArray item_infos;
    item_infos.reserve(item_count);
    int explicit_col_count = grid_layout->explicit_column_count;
    int explicit_row_count = grid_layout->explicit_row_count;
    for (int i = 0; i < item_count; i++) {
        item_infos.push_back(extract_grid_item_info(items[i], i, explicit_col_count, explicit_row_count));
        GridItemInfo& info = item_infos.back();
        log_debug("[GRID EXTRACT] item %d: col start=%d end=%d span=%d definite=%d neg_start=%d neg_end=%d | row start=%d end=%d span=%d definite=%d neg_start=%d neg_end=%d",
                 i, info.column.start, info.column.end, info.column.span, info.column.is_definite,
                 info.column.has_negative_start, info.column.has_negative_end,
                 info.row.start, info.row.end, info.row.span, info.row.is_definite,
                 info.row.has_negative_start, info.row.has_negative_end);
    }

    // Step 1: Calculate initial grid extent from definite positive positions
    // (result used indirectly: resolve_negative_lines_in_items uses the resolved grid size)
    (void)calculate_initial_grid_extent(item_infos, explicit_col_count, explicit_row_count);

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
    log_debug("[GRID PLACEMENT] neg_col_offset=%d neg_row_offset=%d final_cols: neg=%d exp=%d pos=%d, final_rows: neg=%d exp=%d pos=%d",
             neg_col_offset, neg_row_offset,
             final_col_counts.negative_implicit, final_col_counts.explicit_count, final_col_counts.positive_implicit,
             final_row_counts.negative_implicit, final_row_counts.explicit_count, final_row_counts.positive_implicit);
    for (size_t i = 0; i < item_infos.size(); i++) {
        log_debug("[GRID PLACEMENT] item %zu: resolved col=[%d,%d] row=[%d,%d] -> computed col=[%d,%d] row=[%d,%d]",
                 i,
                 item_infos[i].resolved_column.start.value, item_infos[i].resolved_column.end.value,
                 item_infos[i].resolved_row.start.value, item_infos[i].resolved_row.end.value,
                 item_infos[i].resolved_column.start.value + neg_col_offset + 1,
                 item_infos[i].resolved_column.end.value + neg_col_offset + 1,
                 item_infos[i].resolved_row.start.value + neg_row_offset + 1,
                 item_infos[i].resolved_row.end.value + neg_row_offset + 1);
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
 * @return ContribArray of item contributions
 */
inline ContribArray collect_item_contributions(
    GridContainerLayout* grid_layout,
    ViewBlock** items,
    int item_count,
    bool is_column_axis)
{
    ContribArray contributions;
    if (!grid_layout || !items || item_count <= 0) return contributions;

    contributions.reserve(item_count);

    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        if (!item || !item->gi) continue;

        GridItemContribution contrib = {};
        contrib.item = item;

        // Get item's placement (1-based line numbers from GridItemProp)
        if (is_column_axis) {
            int col_start = item->gi->computed_grid_column_start;
            int col_end = item->gi->computed_grid_column_end;
            if (col_start < 1 || col_end < 1) continue;

            contrib.track_start = col_start - 1;  // Convert to 0-based
            contrib.track_span = col_end - col_start;

            // Get intrinsic sizes from pre-computed measurements or calculate.
            // NOTE: has_measured_size=true means the measurement pass ran successfully.
            // 0 is a valid measurement (empty elements); do NOT skip it by checking > 0,
            // as that would cause the fallback to return 1px for empty flex items.
            if (item->gi->has_measured_size) {
                contrib.min_content_contribution = item->gi->measured_min_width;
                contrib.max_content_contribution = item->gi->measured_max_width;
            } else {
                // Fallback: use calculate_grid_item_intrinsic_sizes
                IntrinsicSizes sizes = calculate_grid_item_intrinsic_sizes(
                    grid_layout->lycon, item, false /* is_row_axis = false for width */);
                contrib.min_content_contribution = sizes.min_content;
                contrib.max_content_contribution = sizes.max_content;
            }

            // Apply CSS min-width constraint if not already in the cached measurement.
            // NOTE: Do NOT apply given_max_width here — the measurement functions return
            // the border-box size which already respects max-width at the content level
            // while preserving padding+border. Capping by given_max_width would strip
            // out padding/border from the contribution (e.g. padding_border_overrides_max_size).
            if (item->blk && item->blk->given_min_width > 0) {
                contrib.min_content_contribution = std::max(contrib.min_content_contribution,
                                                            item->blk->given_min_width);
                contrib.max_content_contribution = std::max(contrib.max_content_contribution,
                                                            contrib.min_content_contribution);
            }

            // CSS Grid §12.1: item contributions include margins (unless auto or percentage).
            // The intrinsic-sizing measurement returns the border-box; add resolved
            // horizontal margins to get the full margin-box contribution.
            // Percentage margins are excluded: in an indefinite intrinsic sizing context,
            // percentage margins resolve to 0 per CSS Sizing §3.
            if (item->bound) {
                bool left_is_auto    = (item->bound->margin.left_type  == CSS_VALUE_AUTO);
                bool right_is_auto   = (item->bound->margin.right_type == CSS_VALUE_AUTO);
                bool left_is_pct     = (item->bound->margin.left_type  == CSS_VALUE__PERCENTAGE);
                bool right_is_pct    = (item->bound->margin.right_type == CSS_VALUE__PERCENTAGE);
                float ml = (left_is_auto  || left_is_pct)  ? 0.0f : item->bound->margin.left;
                float mr = (right_is_auto || right_is_pct) ? 0.0f : item->bound->margin.right;
                contrib.min_content_contribution += ml + mr;
                contrib.max_content_contribution += ml + mr;
            }

            // CSS Grid §6.6 / CSS Sizing §4.5: Grid items with non-visible overflow in the
            // inline axis have automatic minimum size = 0. The min-content contribution
            // (actual text content) is preserved for Phase 2 (content-based minimums) and
            // Phase 5 (growth limits). Only Phase 1 uses the automatic minimum (= 0).
            if (item->scroller && item->scroller->overflow_x != CSS_VALUE_VISIBLE) {
                contrib.is_scroll_container = true;
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

            // CSS Grid §12.1: add vertical margins to the row-axis contribution.
            // In CSS, `margin: N%` resolves against the grid container's INLINE size (width),
            // not the block size. If the inline size is definite, the margins are resolved
            // to concrete pixel values and must be included in the row-axis contribution.
            // Only exclude percentage margins when the inline size itself is indefinite.
            if (item->bound) {
                bool top_is_auto = (item->bound->margin.top_type    == CSS_VALUE_AUTO);
                bool bot_is_auto = (item->bound->margin.bottom_type == CSS_VALUE_AUTO);
                bool top_is_pct  = (item->bound->margin.top_type    == CSS_VALUE__PERCENTAGE);
                bool bot_is_pct  = (item->bound->margin.bottom_type == CSS_VALUE__PERCENTAGE);
                bool inline_is_definite = (grid_layout->content_width > 0);
                float mt = top_is_auto ? 0.0f :
                           (top_is_pct && !inline_is_definite) ? 0.0f :
                           item->bound->margin.top;
                float mb = bot_is_auto ? 0.0f :
                           (bot_is_pct && !inline_is_definite) ? 0.0f :
                           item->bound->margin.bottom;
                contrib.min_content_contribution += mt + mb;
                contrib.max_content_contribution += mt + mb;
            }

            // CSS Grid §6.6 / CSS Sizing §4.5: Grid items with non-visible overflow in the
            // block axis have automatic minimum size = 0. Preserve the real min-content for
            // Phase 2; only Phase 1 uses the automatic minimum (= 0) via is_scroll_container.
            if (item->scroller && item->scroller->overflow_y != CSS_VALUE_VISIBLE) {
                contrib.is_scroll_container = true;
            }
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
 * @param out_col_intrinsic_width Optional output: the first-pass (pct-as-auto) column width.
 *        Set only when percentage tracks were re-resolved for an indefinite container.
 *        Callers should use this as the container's intrinsic width (not the sum of tracks
 *        after pct re-resolution, which may be larger due to overflow).
 */
inline void run_enhanced_track_sizing(
    GridContainerLayout* grid_layout,
    ViewBlock** items,
    int item_count,
    float container_width,
    float container_height,
    float* out_col_intrinsic_width = nullptr,
    float* out_row_intrinsic_height = nullptr)
{
    if (!grid_layout) return;

    log_debug("run_enhanced_track_sizing: container=%.1fx%.1f, cols=%d, rows=%d",
              container_width, container_height,
              grid_layout->computed_column_count, grid_layout->computed_row_count);

    // Convert existing tracks to enhanced format
    TrackArray col_tracks =
        convert_tracks_to_enhanced(grid_layout->computed_columns,
                                   grid_layout->computed_column_count);
    TrackArray row_tracks =
        convert_tracks_to_enhanced(grid_layout->computed_rows,
                                   grid_layout->computed_row_count);

    log_debug("  converted col_tracks.size=%zu, row_tracks.size=%zu",
              col_tracks.size(), row_tracks.size());

    // Handle percentage column gaps: save the percentage and use gap=0 for the first pass
    float col_gap_pct = 0.0f;
    bool has_pct_col_gap = grid_layout->column_gap_is_percent;
    float effective_col_gap = grid_layout->column_gap;
    if (has_pct_col_gap) {
        col_gap_pct = grid_layout->column_gap;  // save percentage value (e.g., 20.0)
        effective_col_gap = 0.0f;  // first pass uses gap=0
    }

    // Calculate gap-adjusted available space (gaps reduce available space for tracks)
    float col_gap_total = (grid_layout->computed_column_count > 1)
        ? (grid_layout->computed_column_count - 1) * effective_col_gap
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
        // CSS Grid §12.5: For indefinite containers, percentage tracks are treated as auto
        // during intrinsic sizing, then re-resolved against the determined container width.
        // This covers: bare % tracks, fit-content(%), and minmax(*, %) tracks.
        enum class PctKind : uint8_t { BarePercent, FitContentPercent, MaxPercent };
        struct PctColInfo { int idx; float pct; PctKind kind; MinTrackSizingFunction orig_min; };
        PctColInfo pct_col_infos[MAX_GRID_TRACKS];
        size_t pct_col_count = 0;
        if (col_available < 0.0f) {
            for (int i = 0; i < (int)col_tracks.size(); i++) {
                auto& t = col_tracks[i];
                if (t.min_track_sizing_function.type == SizingFunctionType::Percent) {
                    // Bare "20%" track or minmax(20%, ...) where both min and max are %
                    if (pct_col_count < MAX_GRID_TRACKS) pct_col_infos[pct_col_count++] = {i, t.min_track_sizing_function.value, PctKind::BarePercent, t.min_track_sizing_function};
                    t.min_track_sizing_function = MinTrackSizingFunction::Auto();
                    t.max_track_sizing_function = MaxTrackSizingFunction::Auto();
                } else if (t.max_track_sizing_function.type == SizingFunctionType::FitContentPercent) {
                    // fit-content(50%) → treat as minmax(min-content, max-content) in first pass
                    if (pct_col_count < MAX_GRID_TRACKS) pct_col_infos[pct_col_count++] = {i, t.max_track_sizing_function.value, PctKind::FitContentPercent, t.min_track_sizing_function};
                    t.max_track_sizing_function = MaxTrackSizingFunction::MaxContent();
                } else if (t.max_track_sizing_function.type == SizingFunctionType::Percent) {
                    // minmax(auto/min-content/etc, 20%) → treat max as auto in first pass
                    if (pct_col_count < MAX_GRID_TRACKS) pct_col_infos[pct_col_count++] = {i, t.max_track_sizing_function.value, PctKind::MaxPercent, t.min_track_sizing_function};
                    t.max_track_sizing_function = MaxTrackSizingFunction::Auto();
                }
            }
        }

        // 11.4 Initialize Track Sizes
        initialize_track_sizes(col_tracks, col_available);

        // 11.5 Resolve Intrinsic Track Sizes
        ContribArray col_contributions =
            collect_item_contributions(grid_layout, items, item_count, true /* is_column_axis */);

        // When the container has definite width, cap auto track growth limits to prevent
        // Phase 2 (max-content sizing) from overflowing the container.
        // Only cap when: (1) no flexible (fr) tracks present (fr tracks take remaining space),
        //               (2) the track's max-content would exceed its equal share (per_auto),
        //               (3) the track's min-content floor doesn't already exceed per_auto.
        bool col_has_fr = false;
        for (const auto& t : col_tracks) {
            if (t.is_flexible()) { col_has_fr = true; break; }
        }

        if (col_available > 0.0f && !col_contributions.empty() && !col_has_fr) {
            // Count auto max tracks (non-flexible) and sum fixed-track space
            size_t auto_max_count = 0;
            float fixed_track_space = 0.0f;
            for (const auto& track : col_tracks) {
                if (track.kind != GridTrackKind::Track) continue;
                if (track.max_track_sizing_function.type == SizingFunctionType::Auto && !track.is_flexible()) {
                    auto_max_count++;
                } else if (!track.is_flexible() && !isinf(track.growth_limit)) {
                    fixed_track_space += track.growth_limit;
                }
            }

            if (auto_max_count > 0) {
                float available_for_auto = col_available - fixed_track_space;
                float per_auto = available_for_auto / static_cast<float>(auto_max_count);

                if (per_auto > 0.0f) {
                    // Find per-track min-content and max-content floors from single-span items
                    // For scroll containers, the effective minimum is 0 (CSS Grid §6.6)
                    float track_min_floor[MAX_GRID_TRACKS];
                    float track_max_floor[MAX_GRID_TRACKS];
                    size_t n_col = col_tracks.size();
                    for (size_t i = 0; i < n_col; ++i) { track_min_floor[i] = 0.0f; track_max_floor[i] = 0.0f; }
                    for (const auto& contrib : col_contributions) {
                        if (contrib.track_span == 1) {
                            size_t ti = contrib.track_start;
                            if (ti < n_col) {
                                float effective_min = contrib.is_scroll_container ? 0.0f
                                                                                  : contrib.min_content_contribution;
                                track_min_floor[ti] = std::max(track_min_floor[ti], effective_min);
                                track_max_floor[ti] = std::max(track_max_floor[ti],
                                                               contrib.max_content_contribution);
                            }
                        }
                    }

                    // Cap growth_limit only when the track's max-content would overflow per_auto
                    // and the min-content floor doesn't already exceed per_auto.
                    // This prevents Phase 2 overflow while allowing natural content sizing
                    // for tracks whose items fit within the equal-share budget.
                    for (size_t i = 0; i < col_tracks.size(); i++) {
                        auto& track = col_tracks[i];
                        if (track.kind != GridTrackKind::Track) continue;
                        if (track.max_track_sizing_function.type == SizingFunctionType::Auto &&
                            !track.is_flexible() && isinf(track.growth_limit)) {
                            float floor = (i < n_col) ? track_min_floor[i] : 0.0f;
                            float max_fl = (i < n_col) ? track_max_floor[i] : 0.0f;
                            if (max_fl > per_auto && floor <= per_auto) {
                                // Cap: max-content exceeds equal share but min-content fits
                                track.growth_limit = per_auto;
                            }
                        }
                    }
                }
            }
        }

        for (size_t _di = 0; _di < col_tracks.size(); _di++)
            log_debug("[GRID COL] post-init col[%zu]: base=%.2f gl=%.2f min_type=%d max_type=%d",
                     _di, col_tracks[_di].base_size, col_tracks[_di].growth_limit,
                     (int)col_tracks[_di].min_track_sizing_function.type,
                     (int)col_tracks[_di].max_track_sizing_function.type);

        if (!col_contributions.empty()) {
            log_debug("[GRID COL] %zu contributions", col_contributions.size());
            for (size_t ci = 0; ci < col_contributions.size(); ci++)
                log_debug("[GRID COL] contrib[%zu]: track=%zu span=%zu min=%.1f max=%.1f scroll=%d",
                         ci, col_contributions[ci].track_start, (size_t)col_contributions[ci].track_span,
                         col_contributions[ci].min_content_contribution,
                         col_contributions[ci].max_content_contribution,
                         col_contributions[ci].is_scroll_container);
            resolve_intrinsic_track_sizes(col_tracks, col_contributions, effective_col_gap, col_available);
        }

        for (size_t _di = 0; _di < col_tracks.size(); _di++)
            log_debug("[GRID COL] post-resolve col[%zu]: base=%.2f gl=%.2f",
                     _di, col_tracks[_di].base_size, col_tracks[_di].growth_limit);

        // 11.6 Maximize Tracks
        maximize_tracks(col_tracks, col_available, col_available);

        for (size_t _di = 0; _di < col_tracks.size(); _di++)
            log_debug("  DBG post-maximize col[%zu]: base=%.2f gl=%.2f", _di, col_tracks[_di].base_size, col_tracks[_di].growth_limit);

        // 11.7 Expand Flexible Tracks
        float fr_intrinsic_total = 0.0f;
        expand_flexible_tracks(col_tracks, 0.0f, col_available, col_available,
                               col_contributions, effective_col_gap,
                               &fr_intrinsic_total);
        // For indefinite containers: the intrinsic total from Pass 1 determines the
        // container width, even though Pass 2 may redistribute tracks differently.
        if (fr_intrinsic_total > 0.0f && out_col_intrinsic_width) {
            *out_col_intrinsic_width = fr_intrinsic_total;
        }

        for (size_t _di = 0; _di < col_tracks.size(); _di++)
            log_debug("  DBG post-expand col[%zu]: base=%.2f gl=%.2f", _di, col_tracks[_di].base_size, col_tracks[_di].growth_limit);

        // 11.8 Stretch auto Tracks
        stretch_auto_tracks(col_tracks, 0.0f, col_available);

        for (size_t _di = 0; _di < col_tracks.size(); _di++)
            log_debug("  DBG post-stretch col[%zu]: base=%.2f gl=%.2f", _di, col_tracks[_di].base_size, col_tracks[_di].growth_limit);

        // Compute track positions
        compute_track_offsets(col_tracks, effective_col_gap);

        // IMPORTANT: Copy column results back BEFORE sizing rows
        // Row sizing needs to know the final column widths to calculate item heights
        copy_enhanced_tracks_to_old(col_tracks, grid_layout->computed_columns,
                                     grid_layout->computed_column_count);

        // === Second pass: re-resolve percentage tracks and/or percentage gaps ===
        // After the first pass (where pct were treated as auto, pct gaps as 0),
        // we know the container width. Re-resolve and re-run all phases.
        bool needs_second_pass = (pct_col_count > 0) || has_pct_col_gap;
        if (needs_second_pass) {
            // Compute first-pass total width (sum of track base_sizes + gaps)
            float first_total = col_gap_total;
            for (const auto& t : col_tracks) first_total += t.base_size;

            // Expose the intrinsic width for the caller so it can cap container size
            if (out_col_intrinsic_width) *out_col_intrinsic_width = first_total;
            log_debug("PCT/GAP second pass: first_total=%.1f, %zu pct tracks, pct_gap=%d",
                      first_total, pct_col_count, has_pct_col_gap);

            // Resolve the percentage gap against the intrinsic width
            float resolved_col_gap = effective_col_gap;
            if (has_pct_col_gap) {
                resolved_col_gap = first_total * (col_gap_pct / 100.0f);
                grid_layout->column_gap = resolved_col_gap;
                grid_layout->column_gap_is_percent = false;
            }

            // Recompute gap total and available space with resolved gap
            float col_gap_total2 = (grid_layout->computed_column_count > 1)
                ? (grid_layout->computed_column_count - 1) * resolved_col_gap
                : 0.0f;
            float col_available2 = first_total - col_gap_total2;

            // Re-create col_tracks from the original track definitions
            TrackArray col_tracks2 =
                convert_tracks_to_enhanced(grid_layout->computed_columns,
                                           grid_layout->computed_column_count);
            // Override pct tracks with their resolved values based on kind
            for (size_t _pi = 0; _pi < pct_col_count; _pi++) {
                const auto& pti = pct_col_infos[_pi];
                float resolved = first_total * (pti.pct / 100.0f);
                switch (pti.kind) {
                    case PctKind::BarePercent:
                        col_tracks2[pti.idx].min_track_sizing_function = MinTrackSizingFunction::Length(resolved);
                        col_tracks2[pti.idx].max_track_sizing_function = MaxTrackSizingFunction::Length(resolved);
                        break;
                    case PctKind::FitContentPercent:
                        col_tracks2[pti.idx].min_track_sizing_function = pti.orig_min;
                        col_tracks2[pti.idx].max_track_sizing_function = MaxTrackSizingFunction::FitContentPx(resolved);
                        break;
                    case PctKind::MaxPercent:
                        col_tracks2[pti.idx].min_track_sizing_function = pti.orig_min;
                        col_tracks2[pti.idx].max_track_sizing_function = MaxTrackSizingFunction::Length(resolved);
                        break;
                }
            }

            // Re-run all phases with pct tracks now definite and resolved gap
            initialize_track_sizes(col_tracks2, col_available2);
            if (!col_contributions.empty()) {
                resolve_intrinsic_track_sizes(col_tracks2, col_contributions, resolved_col_gap, col_available);
            }
            // For shrink-to-fit containers: use indefinite semantics for maximize
            // so that only finite-gl tracks grow to their gl (no free-space distribution).
            float max_avail = (container_width < 0) ? -1.0f : col_available2;
            maximize_tracks(col_tracks2, max_avail, max_avail);
            expand_flexible_tracks(col_tracks2, 0.0f, col_available2, col_available2,
                                   col_contributions, resolved_col_gap);
            // For shrink-to-fit: no stretching (container size = content, no free space)
            if (container_width >= 0)
                stretch_auto_tracks(col_tracks2, 0.0f, col_available2);
            compute_track_offsets(col_tracks2, resolved_col_gap);
            copy_enhanced_tracks_to_old(col_tracks2, grid_layout->computed_columns,
                                         grid_layout->computed_column_count);
        }
    }

    // === Run track sizing for rows ===
    if (!row_tracks.empty()) {
        // CSS Grid §12.5: For indefinite containers, percentage row tracks are treated as auto
        // during intrinsic sizing, then re-resolved against the determined container height.
        enum class RowPctKind : uint8_t { BarePercent, FitContentPercent, MaxPercent };
        struct PctRowInfo { int idx; float pct; RowPctKind kind; MinTrackSizingFunction orig_min; };
        PctRowInfo pct_row_infos[MAX_GRID_TRACKS];
        size_t pct_row_count = 0;
        bool needs_row_second_pass = false;

        if (row_available < 0.0f) {
            for (int i = 0; i < (int)row_tracks.size(); i++) {
                auto& t = row_tracks[i];
                if (t.min_track_sizing_function.type == SizingFunctionType::Percent) {
                    if (pct_row_count < MAX_GRID_TRACKS) pct_row_infos[pct_row_count++] = {i, t.min_track_sizing_function.value, RowPctKind::BarePercent, t.min_track_sizing_function};
                    t.min_track_sizing_function = MinTrackSizingFunction::Auto();
                    t.max_track_sizing_function = MaxTrackSizingFunction::Auto();
                    needs_row_second_pass = true;
                } else if (t.max_track_sizing_function.type == SizingFunctionType::FitContentPercent) {
                    if (pct_row_count < MAX_GRID_TRACKS) pct_row_infos[pct_row_count++] = {i, t.max_track_sizing_function.value, RowPctKind::FitContentPercent, t.min_track_sizing_function};
                    t.max_track_sizing_function = MaxTrackSizingFunction::MaxContent();
                    needs_row_second_pass = true;
                } else if (t.max_track_sizing_function.type == SizingFunctionType::Percent) {
                    if (pct_row_count < MAX_GRID_TRACKS) pct_row_infos[pct_row_count++] = {i, t.max_track_sizing_function.value, RowPctKind::MaxPercent, t.min_track_sizing_function};
                    t.max_track_sizing_function = MaxTrackSizingFunction::Auto();
                    needs_row_second_pass = true;
                }
            }
        }

        // 11.4 Initialize Track Sizes
        initialize_track_sizes(row_tracks, row_available);

        // 11.5 Resolve Intrinsic Track Sizes
        // NOTE: This uses grid_layout->computed_columns which was just updated above
        ContribArray row_contributions =
            collect_item_contributions(grid_layout, items, item_count, false /* is_column_axis */);
        if (!row_contributions.empty()) {
            resolve_intrinsic_track_sizes(row_tracks, row_contributions, grid_layout->row_gap, row_available);
        }

        // Only do maximize/expand/stretch if we have definite space
        if (row_available > 0) {
            // 11.6 Maximize Tracks
            maximize_tracks(row_tracks, row_available, row_available);

            // 11.7 Expand Flexible Tracks
            expand_flexible_tracks(row_tracks, 0.0f, row_available, row_available,
                                   row_contributions, grid_layout->row_gap);

            // 11.8 Stretch auto Tracks
            stretch_auto_tracks(row_tracks, 0.0f, row_available);
        }

        // Second pass for percentage row tracks in indefinite containers
        if (needs_row_second_pass) {
            float first_row_total = (grid_layout->computed_row_count > 1)
                ? (grid_layout->computed_row_count - 1) * grid_layout->row_gap : 0.0f;
            for (const auto& t : row_tracks) first_row_total += t.base_size;

            // Expose the intrinsic row height for the caller (container height should not exceed this)
            if (out_row_intrinsic_height) *out_row_intrinsic_height = first_row_total;

            // Re-resolve percentage row tracks against first-pass intrinsic height
            TrackArray row_tracks2 = row_tracks;
            for (size_t _pi = 0; _pi < pct_row_count; _pi++) {
                const auto& pri = pct_row_infos[_pi];
                float resolved = first_row_total * (pri.pct / 100.0f);
                switch (pri.kind) {
                    case RowPctKind::BarePercent:
                        row_tracks2[pri.idx].min_track_sizing_function = MinTrackSizingFunction::Length(resolved);
                        row_tracks2[pri.idx].max_track_sizing_function = MaxTrackSizingFunction::Length(resolved);
                        break;
                    case RowPctKind::FitContentPercent:
                        row_tracks2[pri.idx].min_track_sizing_function = pri.orig_min;
                        row_tracks2[pri.idx].max_track_sizing_function = MaxTrackSizingFunction::FitContentPx(resolved);
                        break;
                    case RowPctKind::MaxPercent:
                        row_tracks2[pri.idx].min_track_sizing_function = pri.orig_min;
                        row_tracks2[pri.idx].max_track_sizing_function = MaxTrackSizingFunction::Length(resolved);
                        break;
                }
            }

            float row_available2 = first_row_total;
            initialize_track_sizes(row_tracks2, row_available2);
            if (!row_contributions.empty()) {
                resolve_intrinsic_track_sizes(row_tracks2, row_contributions, grid_layout->row_gap, row_available);
            }
            maximize_tracks(row_tracks2, row_available2, row_available2);
            expand_flexible_tracks(row_tracks2, 0.0f, row_available2, row_available2,
                                   row_contributions, grid_layout->row_gap);
            stretch_auto_tracks(row_tracks2, 0.0f, row_available2);
            compute_track_offsets(row_tracks2, grid_layout->row_gap);
            copy_enhanced_tracks_to_old(row_tracks2, grid_layout->computed_rows,
                                         grid_layout->computed_row_count);
        } else {
            // Compute track positions
            compute_track_offsets(row_tracks, grid_layout->row_gap);

            // Copy row results back
            copy_enhanced_tracks_to_old(row_tracks, grid_layout->computed_rows,
                                         grid_layout->computed_row_count);
        }
    }
}

} // namespace grid_adapter
} // namespace radiant
