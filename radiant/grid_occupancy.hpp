#pragma once

/**
 * CellOccupancyMatrix - Grid cell occupancy tracking
 *
 * A dynamically sized 2D matrix that tracks which grid cells are occupied
 * during auto-placement. Based on Taffy's CellOccupancyMatrix design.
 *
 * Key features:
 * - Tracks occupancy state for each cell (Unoccupied, DefinitelyPlaced, AutoPlaced)
 * - Dynamically expands in all 4 directions as needed
 * - Maintains TrackCounts for both rows and columns
 * - Provides coordinate conversion between OriginZero and matrix indices
 * - Supports collision detection for auto-placement algorithm
 *
 * TODO: std::* Migration Plan (Phase 5+)
 * - std::vector<CellOccupancyState> data_ → Fixed-size array with dynamic expansion
 *   or custom DynamicArray<T> implementation using lib/arraylist.h patterns
 * - std::move → Manual memory management with mem_alloc/mem_free
 * - Requires careful design due to 2D matrix dynamic sizing requirements
 * - Consider max grid size limits (e.g., 1000x1000) for fixed allocation
 */

#ifdef __cplusplus

// Undefine min/max macros if defined (commonly from windows.h or view.hpp)
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "grid_types.hpp"
#include <vector>
#include <algorithm>

namespace radiant {
namespace grid {

/**
 * CellOccupancyMatrix - 2D matrix tracking grid cell occupancy
 *
 * The matrix automatically expands to accommodate items placed
 * outside the current bounds, creating implicit tracks as needed.
 */
class CellOccupancyMatrix {
public:
    /**
     * Construct with initial track counts
     *
     * @param columns Initial column track counts (can be expanded later)
     * @param rows Initial row track counts (can be expanded later)
     */
    CellOccupancyMatrix(TrackCounts columns, TrackCounts rows)
        : columns_(columns)
        , rows_(rows)
        , row_count_(rows.len())
        , col_count_(columns.len())
        , data_(row_count_ * col_count_, CellOccupancyState::Unoccupied)
    {}

    /**
     * Default constructor - empty grid
     */
    CellOccupancyMatrix()
        : columns_()
        , rows_()
        , row_count_(0)
        , col_count_(0)
        , data_()
    {}

    // --- Accessors ---

    /**
     * Get track counts for the specified axis
     */
    const TrackCounts& track_counts(AbsoluteAxis axis) const {
        return axis == AbsoluteAxis::Horizontal ? columns_ : rows_;
    }

    TrackCounts& track_counts_mut(AbsoluteAxis axis) {
        return axis == AbsoluteAxis::Horizontal ? columns_ : rows_;
    }

    size_t rows() const { return row_count_; }
    size_t cols() const { return col_count_; }

    /**
     * Get cell state at matrix indices (0-based)
     * Returns Unoccupied if out of bounds
     */
    CellOccupancyState get(size_t row, size_t col) const {
        if (row >= row_count_ || col >= col_count_) {
            return CellOccupancyState::Unoccupied;
        }
        return data_[row * col_count_ + col];
    }

    /**
     * Set cell state at matrix indices (0-based)
     * Silently ignores if out of bounds
     */
    void set(size_t row, size_t col, CellOccupancyState state) {
        if (row < row_count_ && col < col_count_) {
            data_[row * col_count_ + col] = state;
        }
    }

    // --- Range checks ---

    /**
     * Check if an area (specified in track indices) fits within current matrix bounds
     */
    bool is_area_in_range(
        AbsoluteAxis primary_axis,
        int16_t primary_start, int16_t primary_end,
        int16_t secondary_start, int16_t secondary_end
    ) const {
        const TrackCounts& primary_counts = track_counts(primary_axis);
        const TrackCounts& secondary_counts = track_counts(other_axis(primary_axis));

        if (primary_start < 0 || primary_end > static_cast<int16_t>(primary_counts.len())) {
            return false;
        }
        if (secondary_start < 0 || secondary_end > static_cast<int16_t>(secondary_counts.len())) {
            return false;
        }
        return true;
    }

    // --- Area operations ---

    /**
     * Ensure the matrix can fit the specified spans, expanding if necessary.
     * Does NOT mark cells, just ensures space exists.
     *
     * @param primary_axis Which axis is "primary" (determines row vs column orientation)
     * @param primary_span Span in primary axis (OriginZero coordinates)
     * @param secondary_span Span in secondary axis (OriginZero coordinates)
     */
    void ensure_fits(
        AbsoluteAxis primary_axis,
        LineSpan primary_span,
        LineSpan secondary_span
    ) {
        // Convert to row/column spans based on axis
        LineSpan row_span, col_span;
        if (primary_axis == AbsoluteAxis::Horizontal) {
            col_span = primary_span;
            row_span = secondary_span;
        } else {
            row_span = primary_span;
            col_span = secondary_span;
        }

        // Convert OriginZero coordinates to track indices
        int16_t col_start, col_end, row_start, row_end;
        columns_.oz_line_range_to_track_range(col_span, col_start, col_end);
        rows_.oz_line_range_to_track_range(row_span, row_start, row_end);

        // Expand if necessary
        if (!is_area_in_range(AbsoluteAxis::Horizontal, col_start, col_end, row_start, row_end)) {
            expand_to_fit_range(row_start, row_end, col_start, col_end);
        }
    }

    /**
     * Mark an area as occupied
     *
     * @param primary_axis Which axis is "primary" (determines row vs column orientation)
     * @param primary_span Span in primary axis (OriginZero coordinates)
     * @param secondary_span Span in secondary axis (OriginZero coordinates)
     * @param state Occupancy state to set
     */
    void mark_area_as(
        AbsoluteAxis primary_axis,
        LineSpan primary_span,
        LineSpan secondary_span,
        CellOccupancyState state
    ) {
        // Convert to row/column spans based on axis
        LineSpan row_span, col_span;
        if (primary_axis == AbsoluteAxis::Horizontal) {
            col_span = primary_span;
            row_span = secondary_span;
        } else {
            row_span = primary_span;
            col_span = secondary_span;
        }

        // Convert OriginZero coordinates to track indices
        int16_t col_start, col_end, row_start, row_end;
        columns_.oz_line_range_to_track_range(col_span, col_start, col_end);
        rows_.oz_line_range_to_track_range(row_span, row_start, row_end);

        // Check if we need to expand the grid
        if (!is_area_in_range(AbsoluteAxis::Horizontal, col_start, col_end, row_start, row_end)) {
            expand_to_fit_range(row_start, row_end, col_start, col_end);
            // Re-calculate indices after expansion
            columns_.oz_line_range_to_track_range(col_span, col_start, col_end);
            rows_.oz_line_range_to_track_range(row_span, row_start, row_end);
        }

        // Mark cells
        for (int16_t r = row_start; r < row_end; ++r) {
            for (int16_t c = col_start; c < col_end; ++c) {
                if (r >= 0 && c >= 0) {
                    set(static_cast<size_t>(r), static_cast<size_t>(c), state);
                }
            }
        }
    }

    /**
     * Check if an area (in OriginZero coordinates) is entirely unoccupied
     */
    bool line_area_is_unoccupied(
        AbsoluteAxis primary_axis,
        LineSpan primary_span,
        LineSpan secondary_span
    ) const {
        const TrackCounts& primary_counts = track_counts(primary_axis);
        const TrackCounts& secondary_counts = track_counts(other_axis(primary_axis));

        int16_t primary_start, primary_end, secondary_start, secondary_end;
        primary_counts.oz_line_range_to_track_range(primary_span, primary_start, primary_end);
        secondary_counts.oz_line_range_to_track_range(secondary_span, secondary_start, secondary_end);

        return track_area_is_unoccupied(primary_axis, primary_start, primary_end, secondary_start, secondary_end);
    }

    /**
     * Check if an area (in matrix track indices) is entirely unoccupied
     */
    bool track_area_is_unoccupied(
        AbsoluteAxis primary_axis,
        int16_t primary_start, int16_t primary_end,
        int16_t secondary_start, int16_t secondary_end
    ) const {
        // Convert to row/col ranges
        int16_t row_start, row_end, col_start, col_end;
        if (primary_axis == AbsoluteAxis::Horizontal) {
            col_start = primary_start; col_end = primary_end;
            row_start = secondary_start; row_end = secondary_end;
        } else {
            row_start = primary_start; row_end = primary_end;
            col_start = secondary_start; col_end = secondary_end;
        }

        // Check all cells in range
        for (int16_t r = row_start; r < row_end; ++r) {
            for (int16_t c = col_start; c < col_end; ++c) {
                if (r >= 0 && c >= 0 &&
                    static_cast<size_t>(r) < row_count_ &&
                    static_cast<size_t>(c) < col_count_) {
                    if (data_[r * col_count_ + c] != CellOccupancyState::Unoccupied) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    /**
     * Check if a specific row contains any occupied cells
     */
    bool row_is_occupied(size_t row_index) const {
        if (row_index >= row_count_) return false;

        for (size_t c = 0; c < col_count_; ++c) {
            if (data_[row_index * col_count_ + c] != CellOccupancyState::Unoccupied) {
                return true;
            }
        }
        return false;
    }

    /**
     * Check if a specific column contains any occupied cells
     */
    bool column_is_occupied(size_t col_index) const {
        if (col_index >= col_count_) return false;

        for (size_t r = 0; r < row_count_; ++r) {
            if (data_[r * col_count_ + col_index] != CellOccupancyState::Unoccupied) {
                return true;
            }
        }
        return false;
    }

    /**
     * Find the last cell of a given type in a track, searching from end to start
     *
     * @param track_type Which axis to search along
     * @param start_at The track to search in (OriginZero coordinate)
     * @param kind The occupancy state to search for
     * @return OriginZeroLine of the found cell, or nullopt if not found
     */
    bool last_of_type(
        AbsoluteAxis track_type,
        OriginZeroLine start_at,
        CellOccupancyState kind,
        OriginZeroLine& out_result
    ) const {
        const TrackCounts& counts = track_counts(other_axis(track_type));
        int16_t track_index = counts.oz_line_to_next_track(start_at);

        if (track_type == AbsoluteAxis::Horizontal) {
            // Search along a row
            if (track_index < 0 || static_cast<size_t>(track_index) >= row_count_) {
                return false;
            }
            // Search from end to start
            for (int64_t c = static_cast<int64_t>(col_count_) - 1; c >= 0; --c) {
                if (data_[track_index * col_count_ + c] == kind) {
                    out_result = counts.track_to_prev_oz_line(static_cast<uint16_t>(c));
                    return true;
                }
            }
        } else {
            // Search along a column
            if (track_index < 0 || static_cast<size_t>(track_index) >= col_count_) {
                return false;
            }
            // Search from end to start
            for (int64_t r = static_cast<int64_t>(row_count_) - 1; r >= 0; --r) {
                if (data_[r * col_count_ + track_index] == kind) {
                    out_result = counts.track_to_prev_oz_line(static_cast<uint16_t>(r));
                    return true;
                }
            }
        }

        return false;
    }

private:
    /**
     * Expand the grid to fit the specified range (in track indices)
     * Grid can expand in all 4 directions
     */
    void expand_to_fit_range(int16_t row_start, int16_t row_end, int16_t col_start, int16_t col_end) {
        // Calculate how much expansion is needed in each direction
        int16_t req_negative_rows = (-row_start > 0) ? static_cast<int16_t>(-row_start) : 0;
        int16_t rows_len = static_cast<int16_t>(rows_.len());
        int16_t req_positive_rows = (row_end > rows_len) ? static_cast<int16_t>(row_end - rows_len) : 0;
        int16_t req_negative_cols = (-col_start > 0) ? static_cast<int16_t>(-col_start) : 0;
        int16_t cols_len = static_cast<int16_t>(columns_.len());
        int16_t req_positive_cols = (col_end > cols_len) ? static_cast<int16_t>(col_end - cols_len) : 0;

        size_t old_row_count = row_count_;
        size_t old_col_count = col_count_;
        size_t new_row_count = old_row_count + req_negative_rows + req_positive_rows;
        size_t new_col_count = old_col_count + req_negative_cols + req_positive_cols;

        // Create new data array
        std::vector<CellOccupancyState> new_data(new_row_count * new_col_count, CellOccupancyState::Unoccupied);

        // Copy existing data to new position (offset by negative expansion)
        for (size_t r = 0; r < old_row_count; ++r) {
            for (size_t c = 0; c < old_col_count; ++c) {
                size_t new_r = r + req_negative_rows;
                size_t new_c = c + req_negative_cols;
                new_data[new_r * new_col_count + new_c] = data_[r * old_col_count + c];
            }
        }

        // Update state
        data_ = std::move(new_data);
        row_count_ = new_row_count;
        col_count_ = new_col_count;

        // Update track counts
        rows_.negative_implicit += static_cast<uint16_t>(req_negative_rows);
        rows_.positive_implicit += static_cast<uint16_t>(req_positive_rows);
        columns_.negative_implicit += static_cast<uint16_t>(req_negative_cols);
        columns_.positive_implicit += static_cast<uint16_t>(req_positive_cols);
    }

    TrackCounts columns_;
    TrackCounts rows_;
    size_t row_count_;
    size_t col_count_;
    std::vector<CellOccupancyState> data_;
};

} // namespace grid
} // namespace radiant

#endif // __cplusplus
