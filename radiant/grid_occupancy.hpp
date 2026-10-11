#pragma once
// internal implementation header - only radiant/ and dedicated white-box tests may include it

/**
 * CellOccupancyMatrix - Grid cell occupancy tracking
 *
 * A sparse set of origin-zero rectangles that tracks which grid cells are occupied
 * during auto-placement. Based on Taffy's CellOccupancyMatrix design.
 *
 * Key features:
 * - Tracks occupancy state for each cell (Unoccupied, DefinitelyPlaced, AutoPlaced)
 * - Dynamically expands in all 4 directions as needed
 * - Maintains TrackCounts for both rows and columns
 * - Provides coordinate conversion between OriginZero and matrix indices
 * - Supports collision detection for auto-placement algorithm
 *
 * Uses tracked manual storage so auto-placement stays consistent with
 * Radiant's C+ allocation style.
 */

#ifdef __cplusplus

// Undefine min/max macros if defined (commonly from windows.h or view.hpp)
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "layout.hpp"
#include "grid_track.hpp"

namespace radiant {
namespace grid {

/**
 * CellOccupancyMatrix - 2D matrix tracking grid cell occupancy
 *
 * The matrix automatically expands to accommodate items placed
 * outside the current bounds, creating implicit tracks as needed.
 */
class CellOccupancyMatrix {
    struct OccupiedArea {
        LineSpan row;
        LineSpan column;
        CellOccupancyState state;
    };

    TrackCounts columns_;
    TrackCounts rows_;
    GridArray<OccupiedArea> areas_;
    bool has_cleared_area_;

    static bool overlaps(LineSpan first, LineSpan second) {
        return first.start < second.end && second.start < first.end;
    }

    static void expand_axis(TrackCounts& counts, LineSpan span) {
        if (span.start.value < -counts.negative_implicit) {
            counts.negative_implicit = static_cast<uint16_t>(-span.start.value);
        }
        if (span.end.value > counts.explicit_count + counts.positive_implicit) {
            counts.positive_implicit = static_cast<uint16_t>(span.end.value - counts.explicit_count);
        }
    }

    static void resolve_area(AbsoluteAxis primary_axis, LineSpan primary, LineSpan secondary,
                             LineSpan& row, LineSpan& column) {
        row = primary_axis == AbsoluteAxis::Horizontal ? secondary : primary;
        column = primary_axis == AbsoluteAxis::Horizontal ? primary : secondary;
        row = grid_clamp_line_span(row.start.value, row.end.value);
        column = grid_clamp_line_span(column.start.value, column.end.value);
    }

public:
    CellOccupancyMatrix(TrackCounts columns, TrackCounts rows)
        : columns_(columns), rows_(rows), has_cleared_area_(false) {}
    CellOccupancyMatrix() : CellOccupancyMatrix(TrackCounts(), TrackCounts()) {}
    CellOccupancyMatrix(const CellOccupancyMatrix&) = delete;
    CellOccupancyMatrix& operator=(const CellOccupancyMatrix&) = delete;

    bool good() const { return areas_.good(); }
    const TrackCounts& track_counts(AbsoluteAxis axis) const {
        return axis == AbsoluteAxis::Horizontal ? columns_ : rows_;
    }
    TrackCounts& track_counts_mut(AbsoluteAxis axis) {
        return axis == AbsoluteAxis::Horizontal ? columns_ : rows_;
    }
    size_t rows() const { return rows_.len(); }
    size_t cols() const { return columns_.len(); }

    CellOccupancyState get(size_t row, size_t col) const {
        if (row >= rows() || col >= cols()) return CellOccupancyState::Unoccupied;
        OriginZeroLine row_line = rows_.track_to_prev_oz_line(static_cast<uint16_t>(row));
        OriginZeroLine col_line = columns_.track_to_prev_oz_line(static_cast<uint16_t>(col));
        // later marks override earlier ones, including explicit clears.
        for (size_t i = areas_.size(); i > 0; i--) {
            const OccupiedArea& area = areas_[i - 1];
            if (area.row.start <= row_line && row_line < area.row.end &&
                area.column.start <= col_line && col_line < area.column.end) return area.state;
        }
        return CellOccupancyState::Unoccupied;
    }

    void set(size_t row, size_t col, CellOccupancyState state) {
        if (row >= rows() || col >= cols()) return;
        mark_area_as(AbsoluteAxis::Horizontal,
                     grid_span_at(columns_.track_to_prev_oz_line(static_cast<uint16_t>(col)), 1),
                     grid_span_at(rows_.track_to_prev_oz_line(static_cast<uint16_t>(row)), 1), state);
    }

    bool is_area_in_range(AbsoluteAxis primary_axis,
                         int16_t primary_start, int16_t primary_end,
                         int16_t secondary_start, int16_t secondary_end) const {
        return primary_start >= 0 && secondary_start >= 0 &&
            primary_end <= track_counts(primary_axis).len() &&
            secondary_end <= track_counts(other_axis(primary_axis)).len();
    }

    void ensure_fits(AbsoluteAxis primary_axis, LineSpan primary, LineSpan secondary) {
        LineSpan row, column;
        resolve_area(primary_axis, primary, secondary, row, column);
        // expansion changes counts only; sparse coordinates never shift or allocate a dense grid.
        expand_axis(rows_, row);
        expand_axis(columns_, column);
    }

    void mark_area_as(AbsoluteAxis primary_axis, LineSpan primary, LineSpan secondary,
                      CellOccupancyState state) {
        LineSpan row, column;
        resolve_area(primary_axis, primary, secondary, row, column);
        ensure_fits(AbsoluteAxis::Horizontal, column, row);
        if (state == CellOccupancyState::Unoccupied) has_cleared_area_ = true;
        areas_.push_back(OccupiedArea{row, column, state});
    }

    bool line_area_is_unoccupied(AbsoluteAxis primary_axis,
                                LineSpan primary, LineSpan secondary) const {
        LineSpan row, column;
        resolve_area(primary_axis, primary, secondary, row, column);
        if (!has_cleared_area_) {
            for (const OccupiedArea& area : areas_) {
                if (overlaps(row, area.row) && overlaps(column, area.column)) return false;
            }
            return true;
        }
        // clears are uncommon; preserve per-cell overwrite semantics for that path.
        int16_t row_start, row_end, col_start, col_end;
        rows_.oz_line_range_to_track_range(row, row_start, row_end);
        columns_.oz_line_range_to_track_range(column, col_start, col_end);
        for (int16_t r = row_start; r < row_end; r++) {
            for (int16_t c = col_start; c < col_end; c++) {
                if (r >= 0 && c >= 0 && get((size_t)r, (size_t)c) != CellOccupancyState::Unoccupied) {
                    return false;
                }
            }
        }
        return true;
    }

    bool track_area_is_unoccupied(AbsoluteAxis primary_axis,
                                 int16_t primary_start, int16_t primary_end,
                                 int16_t secondary_start, int16_t secondary_end) const {
        const TrackCounts& primary = track_counts(primary_axis);
        const TrackCounts& secondary = track_counts(other_axis(primary_axis));
        // clip matrix indices before conversion; out-of-range cells are unoccupied.
        if (primary_start < 0) primary_start = 0;
        if (secondary_start < 0) secondary_start = 0;
        if (primary_end > primary.len()) primary_end = static_cast<int16_t>(primary.len());
        if (secondary_end > secondary.len()) secondary_end = static_cast<int16_t>(secondary.len());
        if (primary_end <= primary_start || secondary_end <= secondary_start) return true;
        return line_area_is_unoccupied(primary_axis,
            LineSpan(primary.track_to_prev_oz_line(static_cast<uint16_t>(primary_start)),
                     primary.track_to_prev_oz_line(static_cast<uint16_t>(primary_end))),
            LineSpan(secondary.track_to_prev_oz_line(static_cast<uint16_t>(secondary_start)),
                     secondary.track_to_prev_oz_line(static_cast<uint16_t>(secondary_end))));
    }

    bool last_of_type(AbsoluteAxis axis, OriginZeroLine start_at,
                      CellOccupancyState kind, OriginZeroLine& out_result) const {
        const TrackCounts& perpendicular = track_counts(other_axis(axis));
        int16_t fixed_track = perpendicular.oz_line_to_next_track(start_at);
        if (fixed_track < 0 || fixed_track >= perpendicular.len()) return false;
        const TrackCounts& searched = track_counts(axis);
        for (size_t remaining = searched.len(); remaining > 0; remaining--) {
            size_t index = remaining - 1;
            CellOccupancyState state = axis == AbsoluteAxis::Horizontal
                ? get((size_t)fixed_track, index) : get(index, (size_t)fixed_track);
            if (state == kind) {
                out_result = searched.track_to_prev_oz_line(static_cast<uint16_t>(index));
                return true;
            }
        }
        return false;
    }
};

} // namespace grid
} // namespace radiant

#endif // __cplusplus
