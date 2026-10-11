#pragma once
// internal implementation header - only radiant/ and dedicated white-box tests may include it

/**
 * Grid Auto-Placement Algorithm
 *
 * This module implements the CSS Grid auto-placement algorithm as specified in
 * https://www.w3.org/TR/css-grid-2/#auto-placement-algo
 *
 * The algorithm places items in the grid following these steps:
 * 1. Place items with definite positions in both axes
 * 2. Place items with definite secondary axis positions
 * 3. Determine implicit grid size
 * 4. Place remaining items with indefinite positions
 *
 * Based on Taffy's implementation with adaptations for Radiant's architecture.
 *
 * Uses explicit structs and scoped growable arrays for auto-placement scratch
 * data.
 */

#ifndef RADIANT_GRID_PLACEMENT_HPP
#define RADIANT_GRID_PLACEMENT_HPP

// Undefine min/max macros if defined (commonly from windows.h or view.hpp)
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "layout.hpp"
#include "grid_occupancy.hpp"
#include "grid_track.hpp"
#include "../lib/log.h"

namespace radiant {
namespace grid {

// Cursor position for grid auto-placement iteration
struct GridCursor {
    OriginZeroLine primary;    // primary axis index
    OriginZeroLine secondary;  // secondary axis index
};


enum class GridAutoFlow {
    Row = 0,         // Fill row by row (default)
    Column = 1,      // Fill column by column
    RowDense = 2,    // Fill row by row with dense packing
    ColumnDense = 3  // Fill column by column with dense packing
};

/**
 * Check if auto-flow mode uses dense packing
 */
inline bool is_dense(GridAutoFlow flow) {
    return flow == GridAutoFlow::RowDense || flow == GridAutoFlow::ColumnDense;
}

/**
 * Get the primary axis for auto-flow mode
 */
inline AbsoluteAxis primary_axis(GridAutoFlow flow) {
    switch (flow) {
        case GridAutoFlow::Column:
        case GridAutoFlow::ColumnDense:
            return AbsoluteAxis::Vertical;
        default:
            return AbsoluteAxis::Horizontal;
    }
}

/**
 * Grid item placement specification
 */
struct GridPlacement {
    // Start line (1-based CSS coordinates, 0 = auto)
    int16_t start;
    // End line (1-based CSS coordinates, 0 = auto, negative = count from end)
    int16_t end;
    // Span (if using span instead of explicit end)
    uint16_t span;
    // Whether this placement is definite (has at least one explicit line)
    bool is_definite;
    // Whether end is a negative line number (needs deferred resolution)
    bool has_negative_end;
    // Whether start is a negative line number (needs deferred resolution)
    bool has_negative_start;

    GridPlacement() : start(0), end(0), span(1), is_definite(false), has_negative_end(false), has_negative_start(false) {}

    /**
     * Create placement from start line and span
     */
    static GridPlacement FromStartSpan(int16_t s, uint16_t sp) {
        GridPlacement p;
        p.start = s;
        p.end = 0;
        p.span = sp;
        p.is_definite = (s != 0);
        p.has_negative_end = false;
        p.has_negative_start = (s < 0);
        return p;
    }

    /**
     * Create placement from start and negative end line
     * Used for "N / -M" syntax (e.g., "1 / -1" = from line 1 to last line)
     */
    static GridPlacement FromStartNegativeEnd(int16_t s, int16_t neg_end) {
        GridPlacement p;
        p.start = s;
        p.end = neg_end;  // Store the negative value
        p.span = 1;       // Placeholder, will be resolved later
        p.is_definite = (s != 0);
        p.has_negative_end = true;
        p.has_negative_start = (s < 0);
        return p;
    }

    /**
     * Create placement from two negative lines
     * Used for "-N / -M" syntax (e.g., "-2 / -1" = second-to-last to last)
     */
    static GridPlacement FromNegativeLines(int16_t neg_start, int16_t neg_end) {
        GridPlacement p;
        p.start = neg_start;  // Store the negative value
        p.end = neg_end;      // Store the negative value
        p.span = 1;           // Placeholder, will be resolved later
        p.is_definite = true; // Both lines are definite (just need resolution)
        p.has_negative_end = true;
        p.has_negative_start = true;
        return p;
    }

    /**
     * Create auto placement with span
     */
    static GridPlacement Auto(uint16_t sp = 1) {
        GridPlacement p;
        p.start = 0;
        p.end = 0;
        p.span = sp;
        p.is_definite = false;
        p.has_negative_end = false;
        p.has_negative_start = false;
        return p;
    }

    /**
     * Get the span of this placement
     */
    uint16_t get_span() const {
        if (start != 0 && end != 0 && !has_negative_end) {
            int16_t s = start > 0 ? start : start;
            int16_t e = end > 0 ? end : end;
            // Note: This is simplified - actual CSS grid line calculation is more complex
            if (e > s) return static_cast<uint16_t>(e - s);
            if (s > e) return static_cast<uint16_t>(s - e);
        }
        return span;
    }

    /**
     * Convert to OriginZero coordinates
     */
    LineSpan to_origin_zero(uint16_t explicit_track_count) const {
        // Convert CSS grid lines to origin-zero coordinates
        // CSS: 1 = first line of explicit grid
        //     -1 = last line of explicit grid (before resolution)
        //      0 = before explicit grid start (after negative line resolution)
        // OriginZero: 0 = first line of explicit grid

        int64_t oz_start = has_negative_start && start < 0
            ? (int64_t)start + explicit_track_count + 1 : (int64_t)start - 1;
        return grid_clamp_line_span(oz_start, oz_start + span);
    }
};

/**
 * Grid item information for placement
 */
struct GridItemInfo {
    int item_index;           // Index in the original item list
    GridPlacement row;        // Row placement
    GridPlacement column;     // Column placement

    // Resolved placement in OriginZero coordinates
    LineSpan resolved_row;
    LineSpan resolved_column;

    GridItemInfo() : item_index(-1), row(), column(), resolved_row(), resolved_column() {}
};

struct ItemInfoArray : GridArray<GridItemInfo> {};

/**
 * Place a single definitely positioned item
 *
 * @param item The item with definite positions in both axes
 * @param explicit_row_count Number of explicit row tracks
 * @param explicit_col_count Number of explicit column tracks
 */
inline void place_definite_item(
    GridItemInfo& item,
    uint16_t explicit_row_count,
    uint16_t explicit_col_count
) {
    item.resolved_row = item.row.to_origin_zero(explicit_row_count);
    item.resolved_column = item.column.to_origin_zero(explicit_col_count);
}

// Definite-row and definite-column placement differ only in which axis has a
// fixed span. Keep the occupancy search in one path so their fallback rules
// cannot drift.
inline void place_definite_axis_item(
    CellOccupancyMatrix& matrix,
    GridItemInfo& item,
    AbsoluteAxis definite_axis,
    uint16_t explicit_definite_count,
    const char* axis_name,
    OriginZeroLine search_start
) {
    const GridPlacement& definite_placement = definite_axis == AbsoluteAxis::Vertical
        ? item.row : item.column;
    const GridPlacement& search_placement = definite_axis == AbsoluteAxis::Vertical
        ? item.column : item.row;
    LineSpan definite_span = definite_placement.to_origin_zero(explicit_definite_count);
    uint16_t search_item_span = search_placement.get_span();
    AbsoluteAxis search_axis = other_axis(definite_axis);
    while (search_start.value < GRID_MAX_TRACK_LINE) {
        LineSpan search_span = grid_span_at(search_start, search_item_span);
        LineSpan column_span = definite_axis == AbsoluteAxis::Vertical
            ? search_span : definite_span;
        LineSpan row_span = definite_axis == AbsoluteAxis::Vertical
            ? definite_span : search_span;
        matrix.ensure_fits(AbsoluteAxis::Horizontal, column_span, row_span);

        if (matrix.line_area_is_unoccupied(
                AbsoluteAxis::Horizontal, column_span, row_span)) {
            item.resolved_row = row_span;
            item.resolved_column = column_span;
            return;
        }
        search_start += 1;
    }

    log_warn("[RAD_CAP_GRID_EXTENT] definite-%s placement reached the positive extent", axis_name);
    OriginZeroLine fallback_start = matrix.track_counts(search_axis).implicit_end_line();
    LineSpan search_span = grid_span_at(fallback_start, search_item_span);
    LineSpan column_span = definite_axis == AbsoluteAxis::Vertical
        ? search_span : definite_span;
    LineSpan row_span = definite_axis == AbsoluteAxis::Vertical
        ? definite_span : search_span;
    matrix.ensure_fits(AbsoluteAxis::Horizontal, column_span, row_span);
    item.resolved_row = row_span;
    item.resolved_column = column_span;
}

/**
 * Place an item with indefinite positions in both axes
 *
 * @param matrix The cell occupancy matrix
 * @param item The item to place
 * @param auto_flow The auto-flow mode
 * @param cursor Current search position (primary_idx, secondary_idx)
 * @return Updated cursor position
 */
inline GridCursor place_indefinite_item(
    CellOccupancyMatrix& matrix,
    GridItemInfo& item,
    GridAutoFlow auto_flow,
    GridCursor cursor
) {
    AbsoluteAxis primary = primary_axis(auto_flow);

    const GridPlacement& primary_placement =
        (primary == AbsoluteAxis::Vertical) ? item.row : item.column;
    const GridPlacement& secondary_placement =
        (primary == AbsoluteAxis::Vertical) ? item.column : item.row;

    uint16_t primary_item_span = primary_placement.get_span();
    uint16_t secondary_item_span = secondary_placement.get_span();

    OriginZeroLine primary_start_line = matrix.track_counts(primary).implicit_start_line();
    OriginZeroLine primary_end_line = matrix.track_counts(primary).implicit_end_line();
    OriginZeroLine secondary_start_line = matrix.track_counts(other_axis(primary)).implicit_start_line();

    OriginZeroLine primary_idx = cursor.primary;
    OriginZeroLine secondary_idx = cursor.secondary;

    // Check if primary position is definite
    bool has_definite_primary = primary_placement.is_definite;


    if (has_definite_primary) {
        // Fixed primary position - search along secondary axis
        uint16_t primary_explicit = (primary == AbsoluteAxis::Vertical)
            ? matrix.track_counts(AbsoluteAxis::Vertical).explicit_count
            : matrix.track_counts(AbsoluteAxis::Horizontal).explicit_count;

        LineSpan primary_span_resolved = primary_placement.to_origin_zero(primary_explicit);

        // Reset secondary if dense or if we've wrapped around
        if (is_dense(auto_flow)) {
            secondary_idx = secondary_start_line;
        } else if (primary_span_resolved.start < primary_idx) {
            secondary_idx += 1;
        }

        while (secondary_idx.value < GRID_MAX_TRACK_LINE) {
            LineSpan secondary_span = grid_span_at(secondary_idx, secondary_item_span);

            // Ensure matrix can accommodate this search position
            matrix.ensure_fits(primary, primary_span_resolved, secondary_span);

            if (matrix.line_area_is_unoccupied(primary, primary_span_resolved, secondary_span)) {
                if (primary == AbsoluteAxis::Vertical) {
                    item.resolved_row = primary_span_resolved;
                    item.resolved_column = secondary_span;
                } else {
                    item.resolved_row = secondary_span;
                    item.resolved_column = primary_span_resolved;
                }
                return {primary_span_resolved.start, secondary_span.start};
            }

            secondary_idx += 1;
        }

        // Fallback: place at the end of secondary axis
        log_warn("[RAD_CAP_GRID_EXTENT] definite-primary placement reached the positive extent");
        OriginZeroLine fallback_secondary = matrix.track_counts(other_axis(primary)).implicit_end_line();
        LineSpan secondary_span = grid_span_at(fallback_secondary, secondary_item_span);
        matrix.ensure_fits(primary, primary_span_resolved, secondary_span);

        if (primary == AbsoluteAxis::Vertical) {
            item.resolved_row = primary_span_resolved;
            item.resolved_column = secondary_span;
        } else {
            item.resolved_row = secondary_span;
            item.resolved_column = primary_span_resolved;
        }
        return {primary_span_resolved.start, secondary_span.start};

    } else {
        // No fixed axis - search in grid order
        while (secondary_idx.value < GRID_MAX_TRACK_LINE) {
            LineSpan primary_span = grid_span_at(primary_idx, primary_item_span);
            LineSpan secondary_span = grid_span_at(secondary_idx, secondary_item_span);

            // Check if primary is out of bounds - wrap to next secondary track
            if (primary_span.end > primary_end_line) {
                secondary_idx += 1;
                primary_idx = primary_start_line;
                // Update end line after potential matrix expansion
                primary_end_line = matrix.track_counts(primary).implicit_end_line();
                continue;
            }

            // Ensure matrix can accommodate this search position
            matrix.ensure_fits(primary, primary_span, secondary_span);

            if (matrix.line_area_is_unoccupied(primary, primary_span, secondary_span)) {
                if (primary == AbsoluteAxis::Vertical) {
                    item.resolved_row = primary_span;
                    item.resolved_column = secondary_span;
                } else {
                    item.resolved_row = secondary_span;
                    item.resolved_column = primary_span;
                }
                return {primary_span.start, secondary_span.start};
            }

            primary_idx += 1;
        }

        // Fallback: place at the end of the grid
        log_warn("[RAD_CAP_GRID_EXTENT] auto placement reached the positive extent");
        OriginZeroLine fallback_primary = matrix.track_counts(primary).implicit_end_line();
        OriginZeroLine fallback_secondary = secondary_idx;
        LineSpan primary_span = grid_span_at(fallback_primary, primary_item_span);
        LineSpan secondary_span = grid_span_at(fallback_secondary, secondary_item_span);
        matrix.ensure_fits(primary, primary_span, secondary_span);

        if (primary == AbsoluteAxis::Vertical) {
            item.resolved_row = primary_span;
            item.resolved_column = secondary_span;
        } else {
            item.resolved_row = secondary_span;
            item.resolved_column = primary_span;
        }
        return {primary_span.start, secondary_span.start};
    }
}

/**
 * Run the complete grid item placement algorithm
 *
 * @param matrix Cell occupancy matrix (will be modified)
 * @param items Items to place (will have resolved positions set)
 * @param auto_flow Auto-flow mode
 * @param explicit_row_count Number of explicit row tracks
 * @param explicit_col_count Number of explicit column tracks
 */
inline void place_grid_items(
    CellOccupancyMatrix& matrix,
    ItemInfoArray& items,
    GridAutoFlow auto_flow,
    uint16_t explicit_row_count,
    uint16_t explicit_col_count
) {
    AbsoluteAxis primary = primary_axis(auto_flow);
    AbsoluteAxis secondary = other_axis(primary);

    // Step 1: Place items with definite positions in both axes
    for (auto& item : items) {
        if (!matrix.good()) return;
        if (item.row.is_definite && item.column.is_definite) {
            place_definite_item(item, explicit_row_count, explicit_col_count);

            // Mark as occupied
            matrix.mark_area_as(
                AbsoluteAxis::Horizontal,
                item.resolved_column,
                item.resolved_row,
                CellOccupancyState::DefinitelyPlaced
            );
        }
    }

    // step 2 locks the secondary axis; column flow transposes the row-flow algorithm.
    uint16_t explicit_secondary = secondary == AbsoluteAxis::Vertical ? explicit_row_count : explicit_col_count;
    for (auto& item : items) {
        if (!matrix.good()) return;
        const GridPlacement& fixed = secondary == AbsoluteAxis::Vertical ? item.row : item.column;
        const GridPlacement& automatic = primary == AbsoluteAxis::Horizontal ? item.column : item.row;
        if (!fixed.is_definite || automatic.is_definite) continue;
        OriginZeroLine search_start = matrix.track_counts(primary).implicit_start_line();
        if (!is_dense(auto_flow)) {
            LineSpan fixed_span = fixed.to_origin_zero(explicit_secondary);
            // sparse packing cannot backfill before an earlier locked item in any overlapping row/column.
            for (const auto& previous : items) {
                if (&previous == &item) break;
                const GridPlacement& previous_fixed = secondary == AbsoluteAxis::Vertical ? previous.row : previous.column;
                const GridPlacement& previous_auto = primary == AbsoluteAxis::Horizontal ? previous.column : previous.row;
                if (!previous_fixed.is_definite || previous_auto.is_definite) continue;
                LineSpan previous_fixed_span = secondary == AbsoluteAxis::Vertical ? previous.resolved_row : previous.resolved_column;
                LineSpan previous_auto_span = primary == AbsoluteAxis::Horizontal ? previous.resolved_column : previous.resolved_row;
                if (fixed_span.start < previous_fixed_span.end && previous_fixed_span.start < fixed_span.end &&
                    search_start < previous_auto_span.end) search_start = previous_auto_span.end;
            }
        }
        place_definite_axis_item(matrix,item,secondary,explicit_secondary,
            secondary == AbsoluteAxis::Vertical ? "row" : "column",search_start);
        matrix.mark_area_as(AbsoluteAxis::Horizontal,item.resolved_column,item.resolved_row,CellOccupancyState::AutoPlaced);
    }

    // CSS Grid §8.5 step 3 includes definite primary positions before automatic spans.
    // otherwise a negative-only item beside an empty explicit grid leaves an extra unused track.
    uint16_t explicit_primary = primary == AbsoluteAxis::Horizontal ? explicit_col_count : explicit_row_count;
    for (const auto& item : items) {
        const GridPlacement& placement = primary == AbsoluteAxis::Horizontal ? item.column : item.row;
        if (placement.is_definite) matrix.ensure_fits(primary, placement.to_origin_zero(explicit_primary),
            grid_span_at(matrix.track_counts(secondary).implicit_start_line(), 1));
    }

    // CSS Grid §8.5: the primary extent must also accommodate the largest automatic span.
    uint16_t largest_auto_span = 1;
    for (const auto& item : items) {
        const GridPlacement& placement = primary == AbsoluteAxis::Horizontal ? item.column : item.row;
        if (!placement.is_definite && placement.span > largest_auto_span) largest_auto_span = placement.span;
    }
    if (largest_auto_span > matrix.track_counts(primary).len()) {
        matrix.ensure_fits(primary,
            grid_span_at(matrix.track_counts(primary).implicit_start_line(), largest_auto_span),
            grid_span_at(matrix.track_counts(secondary).implicit_start_line(), 1));
    }

    // Step 4: Place remaining items in order-modified document order
    // Per CSS Grid spec §8.5: "Position the remaining grid items"
    // This includes:
    // - Items with definite column but auto row (use column, find row)
    // - Items with auto in both axes (use cursor)
    // Both are processed in DOM order, which is critical for correct placement
    OriginZeroLine primary_neg_tracks = matrix.track_counts(primary).implicit_start_line();
    OriginZeroLine secondary_neg_tracks = matrix.track_counts(secondary).implicit_start_line();
    GridCursor cursor = {primary_neg_tracks, secondary_neg_tracks};
    GridCursor grid_start = cursor;

    for (auto& item : items) {
        if (!matrix.good()) return;
        const GridPlacement& fixed = secondary == AbsoluteAxis::Vertical ? item.row : item.column;
        if (fixed.is_definite) continue;  // steps 1 and 2 already placed these items.
        // definite-primary and fully automatic items both advance the same sparse cursor.
        cursor = place_indefinite_item(matrix,item,auto_flow,cursor);

        // Mark as occupied
        matrix.mark_area_as(
            AbsoluteAxis::Horizontal,
            item.resolved_column,
            item.resolved_row,
            CellOccupancyState::AutoPlaced
        );

        // Reset cursor for dense packing
        if (is_dense(auto_flow)) {
            cursor = grid_start;
        }
    }
}

} // namespace grid
} // namespace radiant

#endif // RADIANT_GRID_PLACEMENT_HPP
