#pragma once

/**
 * Enhanced Grid Track Structures
 *
 * This module provides enhanced grid track types that separate min and max
 * sizing functions (following Taffy's design) and include scratch values
 * for the track sizing algorithm.
 *
 * Key improvements over the original GridTrack:
 * 1. Separate min_sizing_function and max_sizing_function
 * 2. Scratch values for multi-pass track sizing algorithm
 * 3. Track kind (Track vs Gutter) for gap handling
 * 4. Better fit-content() support
 *
 * TODO: std::* Migration Plan (Phase 5+)
 * - std::numeric_limits<float>::infinity() → INFINITY macro from <math.h>
 * - std::min/std::max → MIN_FLOAT/MAX_FLOAT macros (see layout_multicol.cpp)
 * - <cmath> std::isinf → isinf() from <math.h>
 * - <algorithm> include can be removed after min/max migration
 */

#ifdef __cplusplus

#include <cstdint>
#include <cmath>
#include <limits>
#include <algorithm>

// Undefine min/max macros if defined (commonly from windows.h or view.hpp)
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace radiant {
namespace grid {

/**
 * Track sizing function type - shared between min and max
 */
enum class SizingFunctionType : uint8_t {
    Auto = 0,
    MinContent = 1,
    MaxContent = 2,
    Length = 3,      // Fixed px value
    Percent = 4,     // Percentage of container
    Fr = 5,          // Fractional unit (only valid for max)
    FitContentPx = 6,   // fit-content(px)
    FitContentPercent = 7, // fit-content(%)
};

/**
 * MinTrackSizingFunction - minimum sizing function for a track
 *
 * Valid types: Auto, MinContent, MaxContent, Length, Percent
 * Note: Fr is NOT valid for min sizing
 */
struct MinTrackSizingFunction {
    SizingFunctionType type;
    float value;  // For Length, Percent

    constexpr MinTrackSizingFunction() : type(SizingFunctionType::Auto), value(0.0f) {}

    // Factory methods
    static constexpr MinTrackSizingFunction Auto() {
        return MinTrackSizingFunction{SizingFunctionType::Auto, 0.0f};
    }
    static constexpr MinTrackSizingFunction MinContent() {
        return MinTrackSizingFunction{SizingFunctionType::MinContent, 0.0f};
    }
    static constexpr MinTrackSizingFunction MaxContent() {
        return MinTrackSizingFunction{SizingFunctionType::MaxContent, 0.0f};
    }
    static constexpr MinTrackSizingFunction Length(float px) {
        return MinTrackSizingFunction{SizingFunctionType::Length, px};
    }
    static constexpr MinTrackSizingFunction Percent(float pct) {
        return MinTrackSizingFunction{SizingFunctionType::Percent, pct};
    }

    /**
     * Returns true if the min track sizing function is intrinsic (MinContent, MaxContent, or Auto)
     */
    constexpr bool is_intrinsic() const {
        return type == SizingFunctionType::Auto ||
               type == SizingFunctionType::MinContent ||
               type == SizingFunctionType::MaxContent;
    }

    /**
     * Returns true if the sizing function uses a percentage
     */
    constexpr bool uses_percentage() const {
        return type == SizingFunctionType::Percent;
    }

    /**
     * Resolve the min sizing function to a definite pixel value
     * Returns -1 if the value needs content-based sizing
     */
    float resolve(float container_size) const {
        switch (type) {
            case SizingFunctionType::Length:
                return value;
            case SizingFunctionType::Percent:
                return container_size * (value / 100.0f);
            default:
                return -1.0f;  // Needs content-based sizing
        }
    }

private:
    constexpr MinTrackSizingFunction(SizingFunctionType t, float v) : type(t), value(v) {}
};

/**
 * MaxTrackSizingFunction - maximum sizing function for a track
 *
 * Valid types: Auto, MinContent, MaxContent, Length, Percent, Fr, FitContentPx, FitContentPercent
 */
struct MaxTrackSizingFunction {
    SizingFunctionType type;
    float value;  // For Length, Percent, Fr, FitContent

    constexpr MaxTrackSizingFunction() : type(SizingFunctionType::Auto), value(0.0f) {}

    // Factory methods
    static constexpr MaxTrackSizingFunction Auto() {
        return MaxTrackSizingFunction{SizingFunctionType::Auto, 0.0f};
    }
    static constexpr MaxTrackSizingFunction MinContent() {
        return MaxTrackSizingFunction{SizingFunctionType::MinContent, 0.0f};
    }
    static constexpr MaxTrackSizingFunction MaxContent() {
        return MaxTrackSizingFunction{SizingFunctionType::MaxContent, 0.0f};
    }
    static constexpr MaxTrackSizingFunction Length(float px) {
        return MaxTrackSizingFunction{SizingFunctionType::Length, px};
    }
    static constexpr MaxTrackSizingFunction Percent(float pct) {
        return MaxTrackSizingFunction{SizingFunctionType::Percent, pct};
    }
    static constexpr MaxTrackSizingFunction Fr(float flex) {
        return MaxTrackSizingFunction{SizingFunctionType::Fr, flex};
    }
    static constexpr MaxTrackSizingFunction FitContentPx(float px) {
        return MaxTrackSizingFunction{SizingFunctionType::FitContentPx, px};
    }
    static constexpr MaxTrackSizingFunction FitContentPercent(float pct) {
        return MaxTrackSizingFunction{SizingFunctionType::FitContentPercent, pct};
    }

    /**
     * Returns true if the max track sizing function is a flex unit (fr)
     */
    constexpr bool is_fr() const {
        return type == SizingFunctionType::Fr;
    }

    /**
     * Returns true if the max track sizing function is intrinsic (MinContent, MaxContent, or Auto)
     */
    constexpr bool is_intrinsic() const {
        return type == SizingFunctionType::Auto ||
               type == SizingFunctionType::MinContent ||
               type == SizingFunctionType::MaxContent;
    }

    /**
     * Returns true if the sizing function uses a percentage
     */
    constexpr bool uses_percentage() const {
        return type == SizingFunctionType::Percent ||
               type == SizingFunctionType::FitContentPercent;
    }

    /**
     * Returns true if this is a fit-content() function
     */
    constexpr bool is_fit_content() const {
        return type == SizingFunctionType::FitContentPx ||
               type == SizingFunctionType::FitContentPercent;
    }

    /**
     * Get the flex factor if this is an Fr track, else 0
     */
    constexpr float flex_factor() const {
        return is_fr() ? value : 0.0f;
    }

    /**
     * Get the fit-content limit value
     */
    float fit_content_limit(float axis_available_space) const {
        switch (type) {
            case SizingFunctionType::FitContentPx:
                return value;
            case SizingFunctionType::FitContentPercent:
                return axis_available_space * (value / 100.0f);
            default:
                return std::numeric_limits<float>::infinity();
        }
    }

    /**
     * Resolve the max sizing function to a definite pixel value
     * Returns -1 if the value needs content-based sizing
     */
    float resolve(float container_size) const {
        switch (type) {
            case SizingFunctionType::Length:
                return value;
            case SizingFunctionType::Percent:
                return container_size * (value / 100.0f);
            default:
                return -1.0f;  // Needs content-based or flex sizing
        }
    }

private:
    constexpr MaxTrackSizingFunction(SizingFunctionType t, float v) : type(t), value(v) {}
};

/**
 * TrackSizingFunction - combined min/max sizing for a track (like CSS minmax())
 */
struct TrackSizingFunction {
    MinTrackSizingFunction min;
    MaxTrackSizingFunction max;

    constexpr TrackSizingFunction()
        : min(MinTrackSizingFunction::Auto()), max(MaxTrackSizingFunction::Auto()) {}

    constexpr TrackSizingFunction(MinTrackSizingFunction mn, MaxTrackSizingFunction mx)
        : min(mn), max(mx) {}

    // Common factory methods
    static constexpr TrackSizingFunction Auto() {
        return TrackSizingFunction(MinTrackSizingFunction::Auto(), MaxTrackSizingFunction::Auto());
    }
    static constexpr TrackSizingFunction MinContent() {
        return TrackSizingFunction(MinTrackSizingFunction::MinContent(), MaxTrackSizingFunction::MinContent());
    }
    static constexpr TrackSizingFunction MaxContent() {
        return TrackSizingFunction(MinTrackSizingFunction::MaxContent(), MaxTrackSizingFunction::MaxContent());
    }
    static constexpr TrackSizingFunction Length(float px) {
        return TrackSizingFunction(MinTrackSizingFunction::Length(px), MaxTrackSizingFunction::Length(px));
    }
    static constexpr TrackSizingFunction Percent(float pct) {
        return TrackSizingFunction(MinTrackSizingFunction::Percent(pct), MaxTrackSizingFunction::Percent(pct));
    }
    static constexpr TrackSizingFunction Fr(float flex) {
        // Fr tracks have auto min and fr max
        return TrackSizingFunction(MinTrackSizingFunction::Auto(), MaxTrackSizingFunction::Fr(flex));
    }
    static constexpr TrackSizingFunction FitContent(float px) {
        // fit-content() has auto min and fit-content max
        return TrackSizingFunction(MinTrackSizingFunction::Auto(), MaxTrackSizingFunction::FitContentPx(px));
    }
    static constexpr TrackSizingFunction FitContentPercent(float pct) {
        return TrackSizingFunction(MinTrackSizingFunction::Auto(), MaxTrackSizingFunction::FitContentPercent(pct));
    }
    static constexpr TrackSizingFunction Minmax(MinTrackSizingFunction mn, MaxTrackSizingFunction mx) {
        return TrackSizingFunction(mn, mx);
    }

    constexpr bool is_flexible() const { return max.is_fr(); }
    constexpr bool has_intrinsic_sizing() const { return min.is_intrinsic() || max.is_intrinsic(); }
    constexpr bool uses_percentage() const { return min.uses_percentage() || max.uses_percentage(); }
};

/**
 * Whether an EnhancedGridTrack represents an actual track or a gutter
 */
enum class GridTrackKind : uint8_t {
    Track = 0,   // Actual track (row or column)
    Gutter = 1   // Gutter (gap between tracks)
};

/**
 * EnhancedGridTrack - Internal sizing information for a single grid track
 *
 * This structure is used during the track sizing algorithm and contains
 * both the track's sizing functions and scratch values for the algorithm.
 *
 * Gutters (gaps) between tracks are also represented by this struct.
 */
struct EnhancedGridTrack {
    // --- Sizing configuration ---

    /** Whether the track is an actual track or a gutter */
    GridTrackKind kind;

    /** Whether the track is collapsed (effectively treated as zero size) */
    bool is_collapsed;

    /** The minimum track sizing function */
    MinTrackSizingFunction min_track_sizing_function;

    /** The maximum track sizing function */
    MaxTrackSizingFunction max_track_sizing_function;

    // --- Computed values ---

    /** The distance of the start of the track from the start of the grid container */
    float offset;

    /** The current size (width/height as applicable) of the track */
    float base_size;

    /** Growth limit - upper bound for base_size. Can be infinity. */
    float growth_limit;

    // --- Scratch values for track sizing algorithm ---

    /**
     * A temporary scratch value when sizing tracks. Used as an additional amount to add
     * to the estimate for the available space in the opposite axis when content sizing items
     */
    float content_alignment_adjustment;

    /**
     * A temporary scratch value when "distributing space" to avoid clobbering planned increase
     */
    float item_incurred_increase;

    /**
     * A temporary scratch value when "distributing space" - planned increase to base_size
     */
    float base_size_planned_increase;

    /**
     * A temporary scratch value when "distributing space" - planned increase to growth_limit
     */
    float growth_limit_planned_increase;

    /**
     * A temporary scratch value for "distributing space"
     * See: https://www.w3.org/TR/css3-grid-layout/#infinitely-growable
     */
    bool infinitely_growable;

    // --- Constructors ---

    /** Default constructor - creates an auto-sized track */
    EnhancedGridTrack()
        : kind(GridTrackKind::Track)
        , is_collapsed(false)
        , min_track_sizing_function(MinTrackSizingFunction::Auto())
        , max_track_sizing_function(MaxTrackSizingFunction::Auto())
        , offset(0.0f)
        , base_size(0.0f)
        , growth_limit(0.0f)
        , content_alignment_adjustment(0.0f)
        , item_incurred_increase(0.0f)
        , base_size_planned_increase(0.0f)
        , growth_limit_planned_increase(0.0f)
        , infinitely_growable(false)
    {}

    /** Create a new track with the specified sizing functions */
    EnhancedGridTrack(MinTrackSizingFunction min_fn, MaxTrackSizingFunction max_fn)
        : kind(GridTrackKind::Track)
        , is_collapsed(false)
        , min_track_sizing_function(min_fn)
        , max_track_sizing_function(max_fn)
        , offset(0.0f)
        , base_size(0.0f)
        , growth_limit(0.0f)
        , content_alignment_adjustment(0.0f)
        , item_incurred_increase(0.0f)
        , base_size_planned_increase(0.0f)
        , growth_limit_planned_increase(0.0f)
        , infinitely_growable(false)
    {}

    /** Create a new track from a TrackSizingFunction */
    explicit EnhancedGridTrack(TrackSizingFunction sizing)
        : EnhancedGridTrack(sizing.min, sizing.max)
    {}

    /** Create a gutter track with fixed size */
    static EnhancedGridTrack Gutter(float size) {
        EnhancedGridTrack track;
        track.kind = GridTrackKind::Gutter;
        track.min_track_sizing_function = MinTrackSizingFunction::Length(size);
        track.max_track_sizing_function = MaxTrackSizingFunction::Length(size);
        return track;
    }

    // --- Query methods ---

    /** Returns true if this is a flexible (fr) track */
    bool is_flexible() const {
        return max_track_sizing_function.is_fr();
    }

    /** Returns true if this track uses percentage sizing */
    bool uses_percentage() const {
        return min_track_sizing_function.uses_percentage() ||
               max_track_sizing_function.uses_percentage();
    }

    /** Returns true if this track has an intrinsic min or max sizing function */
    bool has_intrinsic_sizing_function() const {
        return min_track_sizing_function.is_intrinsic() ||
               max_track_sizing_function.is_intrinsic();
    }

    /** Get the fit-content limit (infinity if not a fit-content track) */
    float fit_content_limit(float axis_available_space) const {
        return max_track_sizing_function.fit_content_limit(axis_available_space);
    }

    /** Get the growth limit clamped by fit-content */
    float fit_content_limited_growth_limit(float axis_available_space) const {
        float limit = fit_content_limit(axis_available_space);
        return std::min(growth_limit, limit);
    }

    /** Get the flex factor (0 if not flexible) */
    float flex_factor() const {
        return max_track_sizing_function.flex_factor();
    }

    // --- Mutating methods ---

    /** Mark this track as collapsed */
    void collapse() {
        is_collapsed = true;
        min_track_sizing_function = MinTrackSizingFunction::Length(0);
        max_track_sizing_function = MaxTrackSizingFunction::Length(0);
    }

    /** Reset scratch values for a new round of track sizing */
    void reset_scratch_values() {
        content_alignment_adjustment = 0.0f;
        item_incurred_increase = 0.0f;
        base_size_planned_increase = 0.0f;
        growth_limit_planned_increase = 0.0f;
        infinitely_growable = false;
    }
};

} // namespace grid
} // namespace radiant

#endif // __cplusplus
