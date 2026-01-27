#pragma once

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
 * TODO: std::* Migration Plan (Phase 5+)
 * - std::pair<OriginZeroLine, OriginZeroLine> → struct GridLinePair { OriginZeroLine first, second; }
 * - std::vector<GridItemInfo>& → ArrayList* or pool-allocated array
 * - std::make_pair → Direct struct initialization
 * Estimated effort: Moderate refactoring (~200 lines)
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

#include "grid_types.hpp"
#include "grid_occupancy.hpp"
#include <vector>
#include <functional>

namespace radiant {
namespace grid {

/**
 * Grid auto-flow mode
 */
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
     * Create placement from start and end lines
     */
    static GridPlacement FromLines(int16_t s, int16_t e) {
        GridPlacement p;
        p.start = s;
        p.end = e;
        p.span = 1;
        p.is_definite = (s != 0) || (e != 0);
        p.has_negative_end = (e < 0);
        p.has_negative_start = (s < 0);
        return p;
    }

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
        //     -1 = last line of explicit grid
        // OriginZero: 0 = first line of explicit grid

        int16_t oz_start, oz_end;
        uint16_t explicit_line_count = explicit_track_count + 1;

        if (start > 0) {
            oz_start = start - 1;
        } else if (start < 0) {
            oz_start = start + static_cast<int16_t>(explicit_line_count);
        } else {
            oz_start = 0; // Auto - will be resolved later
        }

        if (end > 0) {
            oz_end = end - 1;
        } else if (end < 0) {
            oz_end = end + static_cast<int16_t>(explicit_line_count);
        } else {
            // Auto - use start + span
            oz_end = oz_start + span;
        }

        return LineSpan(OriginZeroLine(oz_start), OriginZeroLine(oz_end));
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

/**
 * Place an item with definite row but indefinite column (CSS Grid spec step 2)
 *
 * @param matrix The cell occupancy matrix
 * @param item The item to place
 * @param auto_flow The auto-flow mode
 * @param explicit_row_count Number of explicit row tracks
 * @param explicit_col_count Number of explicit column tracks
 */
inline void place_definite_row_item(
    CellOccupancyMatrix& matrix,
    GridItemInfo& item,
    GridAutoFlow auto_flow,
    uint16_t explicit_row_count,
    uint16_t explicit_col_count
) {
    // Row is definite, column is indefinite
    LineSpan row_span = item.row.to_origin_zero(explicit_row_count);
    uint16_t column_item_span = item.column.get_span();

    // Find first available column position in the specified row(s)
    OriginZeroLine col_start = matrix.track_counts(AbsoluteAxis::Horizontal).implicit_start_line();
    constexpr int MAX_SEARCH_ITERATIONS = 10000;
    int iterations = 0;

    while (iterations < MAX_SEARCH_ITERATIONS) {
        LineSpan col_span(col_start, col_start + column_item_span);

        // Ensure matrix can accommodate this position
        matrix.ensure_fits(AbsoluteAxis::Horizontal, col_span, row_span);

        if (matrix.line_area_is_unoccupied(AbsoluteAxis::Horizontal, col_span, row_span)) {
            // Found a free space
            item.resolved_row = row_span;
            item.resolved_column = col_span;
            return;
        }

        col_start += 1;
        iterations++;
    }

    // Fallback: place at end of grid
    OriginZeroLine fallback_col = matrix.track_counts(AbsoluteAxis::Horizontal).implicit_end_line();
    LineSpan col_span(fallback_col, fallback_col + column_item_span);
    matrix.ensure_fits(AbsoluteAxis::Horizontal, col_span, row_span);

    item.resolved_row = row_span;
    item.resolved_column = col_span;
}

/**
 * Place an item with definite column but indefinite row (CSS Grid spec step 3)
 *
 * @param matrix The cell occupancy matrix
 * @param item The item to place
 * @param auto_flow The auto-flow mode
 * @param explicit_row_count Number of explicit row tracks
 * @param explicit_col_count Number of explicit column tracks
 */
inline void place_definite_column_item(
    CellOccupancyMatrix& matrix,
    GridItemInfo& item,
    GridAutoFlow auto_flow,
    uint16_t explicit_row_count,
    uint16_t explicit_col_count
) {
    // Column is definite, row is indefinite
    LineSpan col_span = item.column.to_origin_zero(explicit_col_count);
    uint16_t row_item_span = item.row.get_span();

    // Find first available row position in the specified column(s)
    OriginZeroLine row_start = matrix.track_counts(AbsoluteAxis::Vertical).implicit_start_line();
    constexpr int MAX_SEARCH_ITERATIONS = 10000;
    int iterations = 0;

    while (iterations < MAX_SEARCH_ITERATIONS) {
        LineSpan row_span(row_start, row_start + row_item_span);

        // Ensure matrix can accommodate this position
        matrix.ensure_fits(AbsoluteAxis::Horizontal, col_span, row_span);

        if (matrix.line_area_is_unoccupied(AbsoluteAxis::Horizontal, col_span, row_span)) {
            // Found a free space
            item.resolved_row = row_span;
            item.resolved_column = col_span;
            return;
        }

        row_start += 1;
        iterations++;
    }

    // Fallback: place at end of grid
    OriginZeroLine fallback_row = matrix.track_counts(AbsoluteAxis::Vertical).implicit_end_line();
    LineSpan row_span(fallback_row, fallback_row + row_item_span);
    matrix.ensure_fits(AbsoluteAxis::Horizontal, col_span, row_span);

    item.resolved_row = row_span;
    item.resolved_column = col_span;
}

/**
 * Place an item with definite secondary axis position
 *
 * @param matrix The cell occupancy matrix
 * @param item The item to place
 * @param auto_flow The auto-flow mode
 * @param explicit_row_count Number of explicit row tracks
 * @param explicit_col_count Number of explicit column tracks
 */
inline void place_definite_secondary_axis_item(
    CellOccupancyMatrix& matrix,
    GridItemInfo& item,
    GridAutoFlow auto_flow,
    uint16_t explicit_row_count,
    uint16_t explicit_col_count
) {
    AbsoluteAxis primary = primary_axis(auto_flow);
    AbsoluteAxis secondary = other_axis(primary);

    // Get the definite secondary placement
    const GridPlacement& secondary_placement =
        (secondary == AbsoluteAxis::Vertical) ? item.row : item.column;
    const GridPlacement& primary_placement =
        (primary == AbsoluteAxis::Vertical) ? item.row : item.column;

    uint16_t primary_explicit = (primary == AbsoluteAxis::Vertical) ? explicit_row_count : explicit_col_count;
    uint16_t secondary_explicit = (secondary == AbsoluteAxis::Vertical) ? explicit_row_count : explicit_col_count;

    LineSpan secondary_span = secondary_placement.to_origin_zero(secondary_explicit);
    uint16_t primary_item_span = primary_placement.get_span();

    // Starting position for search
    OriginZeroLine primary_start_line = matrix.track_counts(primary).implicit_start_line();
    OriginZeroLine position = is_dense(auto_flow)
        ? primary_start_line
        : primary_start_line; // TODO: Use last_of_type for sparse packing

    // Search for an unoccupied area with iteration limit
    // The grid can grow implicitly, but we need a reasonable upper bound
    constexpr int MAX_SEARCH_ITERATIONS = 10000;
    int iterations = 0;

    while (iterations < MAX_SEARCH_ITERATIONS) {
        LineSpan primary_span(position, position + primary_item_span);

        // Ensure matrix can accommodate this position by expanding if needed
        matrix.ensure_fits(primary, primary_span, secondary_span);

        if (matrix.line_area_is_unoccupied(primary, primary_span, secondary_span)) {
            // Found a free space
            if (primary == AbsoluteAxis::Vertical) {
                item.resolved_row = primary_span;
                item.resolved_column = secondary_span;
            } else {
                item.resolved_row = secondary_span;
                item.resolved_column = primary_span;
            }
            return;
        }

        position += 1;
        iterations++;
    }

    // Fallback: If we couldn't find a spot, place at the end of the grid
    OriginZeroLine fallback_pos = matrix.track_counts(primary).implicit_end_line();
    LineSpan primary_span(fallback_pos, fallback_pos + primary_item_span);
    matrix.ensure_fits(primary, primary_span, secondary_span);

    if (primary == AbsoluteAxis::Vertical) {
        item.resolved_row = primary_span;
        item.resolved_column = secondary_span;
    } else {
        item.resolved_row = secondary_span;
        item.resolved_column = primary_span;
    }
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
inline std::pair<OriginZeroLine, OriginZeroLine> place_indefinite_item(
    CellOccupancyMatrix& matrix,
    GridItemInfo& item,
    GridAutoFlow auto_flow,
    std::pair<OriginZeroLine, OriginZeroLine> cursor
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

    OriginZeroLine primary_idx = cursor.first;
    OriginZeroLine secondary_idx = cursor.second;

    // Check if primary position is definite
    bool has_definite_primary = primary_placement.is_definite;

    // Iteration limit to prevent infinite loops
    constexpr int MAX_SEARCH_ITERATIONS = 10000;
    int iterations = 0;

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

        while (iterations < MAX_SEARCH_ITERATIONS) {
            LineSpan secondary_span(secondary_idx, secondary_idx + secondary_item_span);

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
                return {primary_span_resolved.end, secondary_span.start};
            }

            secondary_idx += 1;
            iterations++;
        }

        // Fallback: place at the end of secondary axis
        OriginZeroLine fallback_secondary = matrix.track_counts(other_axis(primary)).implicit_end_line();
        LineSpan secondary_span(fallback_secondary, fallback_secondary + secondary_item_span);
        matrix.ensure_fits(primary, primary_span_resolved, secondary_span);

        if (primary == AbsoluteAxis::Vertical) {
            item.resolved_row = primary_span_resolved;
            item.resolved_column = secondary_span;
        } else {
            item.resolved_row = secondary_span;
            item.resolved_column = primary_span_resolved;
        }
        return {primary_span_resolved.end, secondary_span.start};

    } else {
        // No fixed axis - search in grid order
        while (iterations < MAX_SEARCH_ITERATIONS) {
            LineSpan primary_span(primary_idx, primary_idx + primary_item_span);
            LineSpan secondary_span(secondary_idx, secondary_idx + secondary_item_span);

            // Check if primary is out of bounds - wrap to next secondary track
            if (primary_span.end > primary_end_line) {
                secondary_idx += 1;
                primary_idx = primary_start_line;
                // Update end line after potential matrix expansion
                primary_end_line = matrix.track_counts(primary).implicit_end_line();
                iterations++;
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
                return {primary_span.end, secondary_span.start};
            }

            primary_idx += 1;
            iterations++;
        }

        // Fallback: place at the end of the grid
        OriginZeroLine fallback_primary = matrix.track_counts(primary).implicit_end_line();
        OriginZeroLine fallback_secondary = secondary_idx;
        LineSpan primary_span(fallback_primary, fallback_primary + primary_item_span);
        LineSpan secondary_span(fallback_secondary, fallback_secondary + secondary_item_span);
        matrix.ensure_fits(primary, primary_span, secondary_span);

        if (primary == AbsoluteAxis::Vertical) {
            item.resolved_row = primary_span;
            item.resolved_column = secondary_span;
        } else {
            item.resolved_row = secondary_span;
            item.resolved_column = primary_span;
        }
        return {primary_span.end, secondary_span.start};
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
    std::vector<GridItemInfo>& items,
    GridAutoFlow auto_flow,
    uint16_t explicit_row_count,
    uint16_t explicit_col_count
) {
    AbsoluteAxis primary = primary_axis(auto_flow);
    AbsoluteAxis secondary = other_axis(primary);

    // Step 1: Place items with definite positions in both axes
    for (auto& item : items) {
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

    // Step 2: Place items with definite row positions (auto column)
    // Per CSS Grid spec §8.5: "Process the items locked to a given row"
    // These are items with definite row position but auto column position
    for (auto& item : items) {
        if (item.row.is_definite && !item.column.is_definite) {
            place_definite_row_item(
                matrix, item, auto_flow, explicit_row_count, explicit_col_count
            );

            // Mark as occupied
            matrix.mark_area_as(
                AbsoluteAxis::Horizontal,
                item.resolved_column,
                item.resolved_row,
                CellOccupancyState::AutoPlaced
            );
        }
    }

    // Step 3: Determine implicit grid columns
    // This is handled implicitly by CellOccupancyMatrix's expand_to_fit_range

    // Step 4: Place remaining items in order-modified document order
    // Per CSS Grid spec §8.5: "Position the remaining grid items"
    // This includes:
    // - Items with definite column but auto row (use column, find row)
    // - Items with auto in both axes (use cursor)
    // Both are processed in DOM order, which is critical for correct placement
    OriginZeroLine primary_neg_tracks = matrix.track_counts(primary).implicit_start_line();
    OriginZeroLine secondary_neg_tracks = matrix.track_counts(secondary).implicit_start_line();
    auto cursor = std::make_pair(primary_neg_tracks, secondary_neg_tracks);
    auto grid_start = cursor;

    for (auto& item : items) {
        bool row_definite = item.row.is_definite;
        bool col_definite = item.column.is_definite;

        // Skip items already placed in step 1 or step 2
        if (row_definite) {
            continue;
        }

        if (col_definite) {
            // Item has definite column but auto row
            // Per spec: "If the item has a definite column position:"
            // Use the column position and search for available row
            place_definite_column_item(
                matrix, item, auto_flow, explicit_row_count, explicit_col_count
            );
        } else {
            // Item has auto in both axes
            cursor = place_indefinite_item(matrix, item, auto_flow, cursor);
        }

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
