#pragma once

/**
 * Grid Types - Coordinate systems and track counting
 *
 * This module provides the foundation for CSS Grid layout by implementing
 * a dual coordinate system (following Taffy's design):
 *
 * 1. GridLine - CSS spec coordinates where:
 *    - Line 1 is the start of the explicit grid
 *    - Line -1 is the end of the explicit grid
 *    - 0 is invalid
 *
 * 2. OriginZeroLine - Normalized coordinates where:
 *    - Line 0 is the start of the explicit grid
 *    - Positive numbers extend right/down
 *    - Negative numbers extend left/up (into implicit grid)
 *
 * The TrackCounts struct tracks implicit and explicit track counts,
 * enabling coordinate conversion between systems.
 *
 * TODO: std::* Migration Plan (Phase 5+)
 * - <algorithm> include for std::min/std::max → MIN/MAX macros
 * - Uses mostly C types already, minimal migration needed
 */

#ifdef __cplusplus

#include <cstdint>
#include <algorithm>  // TODO: Replace with MIN/MAX macros
#include <cassert>

// Undefine min/max macros if defined (commonly from windows.h or view.hpp)
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace radiant {
namespace grid {

// Forward declarations
struct TrackCounts;

/**
 * Cell occupancy state for auto-placement tracking
 */
enum class CellOccupancyState : uint8_t {
    Unoccupied = 0,       // Cell is available for placement
    DefinitelyPlaced = 1, // Cell occupied by explicitly placed item
    AutoPlaced = 2        // Cell occupied by auto-placed item
};

/**
 * OriginZeroLine - Normalized grid line coordinate
 *
 * Line 0 = start of explicit grid
 * Positive = lines extending right/down
 * Negative = lines extending left/up (into negative implicit grid)
 */
struct OriginZeroLine {
    int16_t value;

    constexpr OriginZeroLine() : value(0) {}
    constexpr explicit OriginZeroLine(int16_t v) : value(v) {}

    // Arithmetic operations
    constexpr OriginZeroLine operator+(OriginZeroLine other) const {
        return OriginZeroLine(value + other.value);
    }
    constexpr OriginZeroLine operator-(OriginZeroLine other) const {
        return OriginZeroLine(value - other.value);
    }
    constexpr OriginZeroLine operator+(uint16_t n) const {
        return OriginZeroLine(value + static_cast<int16_t>(n));
    }
    constexpr OriginZeroLine operator-(uint16_t n) const {
        return OriginZeroLine(value - static_cast<int16_t>(n));
    }
    OriginZeroLine& operator+=(uint16_t n) {
        value += static_cast<int16_t>(n);
        return *this;
    }

    // Comparison operators
    constexpr bool operator==(OriginZeroLine other) const { return value == other.value; }
    constexpr bool operator!=(OriginZeroLine other) const { return value != other.value; }
    constexpr bool operator<(OriginZeroLine other) const { return value < other.value; }
    constexpr bool operator<=(OriginZeroLine other) const { return value <= other.value; }
    constexpr bool operator>(OriginZeroLine other) const { return value > other.value; }
    constexpr bool operator>=(OriginZeroLine other) const { return value >= other.value; }

    /**
     * The minimum number of negative implicit tracks needed if an item starts at this line
     */
    constexpr uint16_t implied_negative_implicit_tracks() const {
        return value < 0 ? static_cast<uint16_t>(-value) : 0;
    }

    /**
     * The minimum number of positive implicit tracks needed if an item ends at this line
     */
    constexpr uint16_t implied_positive_implicit_tracks(uint16_t explicit_track_count) const {
        return value > static_cast<int16_t>(explicit_track_count)
            ? static_cast<uint16_t>(value) - explicit_track_count
            : 0;
    }
};

/**
 * GridLine - CSS Grid spec coordinate
 *
 * Line 1 = start of explicit grid
 * Line -1 = end of explicit grid
 * 0 is invalid
 */
struct GridLine {
    int16_t value;

    constexpr GridLine() : value(0) {}
    constexpr explicit GridLine(int16_t v) : value(v) {}

    constexpr int16_t as_i16() const { return value; }

    /**
     * Convert CSS grid line to origin-zero coordinates
     *
     * @param explicit_track_count Number of explicit tracks in this axis
     */
    OriginZeroLine into_origin_zero_line(uint16_t explicit_track_count) const {
        int16_t explicit_line_count = static_cast<int16_t>(explicit_track_count + 1);
        int16_t oz_line = 0;

        if (value > 0) {
            oz_line = value - 1;
        } else if (value < 0) {
            oz_line = value + explicit_line_count;
        }
        // else value == 0 is invalid - treat as line 1, oz_line = 0

        return OriginZeroLine(oz_line);
    }

    constexpr bool is_valid() const { return value != 0; }
};

/**
 * Line span in OriginZero coordinates (start inclusive, end exclusive)
 */
struct LineSpan {
    OriginZeroLine start;
    OriginZeroLine end;

    constexpr LineSpan() : start(), end() {}
    constexpr LineSpan(OriginZeroLine s, OriginZeroLine e) : start(s), end(e) {}

    /**
     * The number of tracks between start and end lines
     */
    constexpr uint16_t span() const {
        int16_t diff = end.value - start.value;
        return diff > 0 ? static_cast<uint16_t>(diff) : 0;
    }
};

/**
 * TrackCounts - Tracks the number of implicit and explicit tracks
 *
 * The grid is conceptually divided into three regions:
 * [negative_implicit] [explicit] [positive_implicit]
 *
 * Where negative_implicit tracks are created when items are placed
 * before the explicit grid, and positive_implicit tracks are created
 * when items are placed after the explicit grid.
 */
struct TrackCounts {
    uint16_t negative_implicit; // Tracks before the explicit grid
    uint16_t explicit_count;    // Tracks in the explicit grid
    uint16_t positive_implicit; // Tracks after the explicit grid

    constexpr TrackCounts() : negative_implicit(0), explicit_count(0), positive_implicit(0) {}
    constexpr TrackCounts(uint16_t neg, uint16_t exp, uint16_t pos)
        : negative_implicit(neg), explicit_count(exp), positive_implicit(pos) {}

    /**
     * Total number of tracks (implicit + explicit)
     */
    constexpr size_t len() const {
        return static_cast<size_t>(negative_implicit + explicit_count + positive_implicit);
    }

    /**
     * The OriginZeroLine representing the start of the implicit grid
     * (i.e., the leftmost/topmost line)
     */
    constexpr OriginZeroLine implicit_start_line() const {
        return OriginZeroLine(-static_cast<int16_t>(negative_implicit));
    }

    /**
     * The OriginZeroLine representing the end of the implicit grid
     * (i.e., the rightmost/bottommost line)
     */
    constexpr OriginZeroLine implicit_end_line() const {
        return OriginZeroLine(static_cast<int16_t>(explicit_count + positive_implicit));
    }

    // --- Coordinate conversions ---

    /**
     * Convert an OriginZero line to the index of the track immediately following it
     * (for use with CellOccupancyMatrix)
     */
    constexpr int16_t oz_line_to_next_track(OriginZeroLine line) const {
        return line.value + static_cast<int16_t>(negative_implicit);
    }

    /**
     * Convert start/end OriginZero lines to a range of track indices
     * Returns: [start_track_idx, end_track_idx) - exclusive end
     */
    constexpr void oz_line_range_to_track_range(
        LineSpan span,
        int16_t& out_start,
        int16_t& out_end
    ) const {
        out_start = oz_line_to_next_track(span.start);
        out_end = oz_line_to_next_track(span.end);
    }

    /**
     * Convert a track index back to the OriginZero line immediately preceding it
     */
    constexpr OriginZeroLine track_to_prev_oz_line(uint16_t track_idx) const {
        return OriginZeroLine(static_cast<int16_t>(track_idx) - static_cast<int16_t>(negative_implicit));
    }

    /**
     * Convert a track index range back to OriginZero line range
     */
    constexpr LineSpan track_range_to_oz_line_range(int16_t start_idx, int16_t end_idx) const {
        return LineSpan(
            track_to_prev_oz_line(static_cast<uint16_t>(start_idx)),
            track_to_prev_oz_line(static_cast<uint16_t>(end_idx))
        );
    }

    /**
     * Convert OriginZero line to GridTrackVec index (which stores lines and tracks interleaved)
     * Even indices = lines, odd indices = tracks
     *
     * Returns -1 if the line is out of bounds
     */
    int into_track_vec_index(OriginZeroLine line) const {
        // Check bounds
        if (line.value < -static_cast<int16_t>(negative_implicit)) {
            return -1;
        }
        if (line.value > static_cast<int16_t>(explicit_count + positive_implicit)) {
            return -1;
        }

        return 2 * (line.value + static_cast<int>(negative_implicit));
    }
};

/**
 * Absolute axis type for grid operations
 */
enum class AbsoluteAxis : uint8_t {
    Horizontal = 0, // Columns (inline axis)
    Vertical = 1    // Rows (block axis)
};

constexpr AbsoluteAxis other_axis(AbsoluteAxis axis) {
    return axis == AbsoluteAxis::Horizontal ? AbsoluteAxis::Vertical : AbsoluteAxis::Horizontal;
}

} // namespace grid
} // namespace radiant

#endif // __cplusplus
