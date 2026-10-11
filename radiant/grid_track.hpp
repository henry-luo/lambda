#pragma once
// internal implementation header - only radiant/ and dedicated white-box tests may include it

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
 * Uses C+ style sizing helpers and scoped growable arrays for layout scratch
 * storage.
 */

#ifdef __cplusplus

#include <cstdint>
#include <math.h>

#include "../lib/log.h"
#include "../lib/ownership.hpp"
#include "../lib/arraylist.hpp"

struct CssValue;
namespace radiant { float resolve_computed_length_percentage(const CssValue*, float); }

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

// The min and max forms share their fixed/intrinsic representation. The
// specialized wrappers below only expose the values each CSS production allows.
struct TrackSizingFunctionValue {
    SizingFunctionType type;
    float value;
    lam::Up<const CssValue> expression;

    constexpr TrackSizingFunctionValue(SizingFunctionType type = SizingFunctionType::Auto,
                                       float value = 0.0f, const CssValue* expression = nullptr)
        : type(type), value(value), expression(expression) {}

    constexpr bool is_intrinsic() const {
        return type == SizingFunctionType::Auto ||
               type == SizingFunctionType::MinContent ||
               type == SizingFunctionType::MaxContent;
    }

    float resolve(float container_size) const {
        switch (type) {
            case SizingFunctionType::Length:
                return value;
            case SizingFunctionType::Percent:
                if (container_size < 0.0f) return -1.0f;
                return expression ? fmaxf(0.0f, radiant::resolve_computed_length_percentage(
                    expression, container_size)) : container_size * (value / 100.0f);
            default:
                return -1.0f;
        }
    }
};

/**
 * MinTrackSizingFunction - minimum sizing function for a track
 *
 * Valid types: Auto, MinContent, MaxContent, Length, Percent
 * Note: Fr is NOT valid for min sizing
 */
struct MinTrackSizingFunction : TrackSizingFunctionValue {
    constexpr MinTrackSizingFunction() = default;

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
    static constexpr MinTrackSizingFunction Percent(float pct, const CssValue* expression = nullptr) {
        return MinTrackSizingFunction{SizingFunctionType::Percent, pct, expression};
    }

    /**
     * Returns true if the sizing function uses a percentage
     */
    constexpr bool uses_percentage() const {
        return type == SizingFunctionType::Percent;
    }

private:
    constexpr MinTrackSizingFunction(SizingFunctionType type, float value, const CssValue* expression = nullptr)
        : TrackSizingFunctionValue(type, value, expression) {}
};

/**
 * MaxTrackSizingFunction - maximum sizing function for a track
 *
 * Valid types: Auto, MinContent, MaxContent, Length, Percent, Fr, FitContentPx, FitContentPercent
 */
struct MaxTrackSizingFunction : TrackSizingFunctionValue {
    constexpr MaxTrackSizingFunction() = default;

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
    static constexpr MaxTrackSizingFunction Percent(float pct, const CssValue* expression = nullptr) {
        return MaxTrackSizingFunction{SizingFunctionType::Percent, pct, expression};
    }
    static constexpr MaxTrackSizingFunction Fr(float flex) {
        return MaxTrackSizingFunction{SizingFunctionType::Fr, flex};
    }
    static constexpr MaxTrackSizingFunction FitContentPx(float px) {
        return MaxTrackSizingFunction{SizingFunctionType::FitContentPx, px};
    }
    static constexpr MaxTrackSizingFunction FitContentPercent(float pct, const CssValue* expression = nullptr) {
        return MaxTrackSizingFunction{SizingFunctionType::FitContentPercent, pct, expression};
    }

    /**
     * Returns true if the max track sizing function is a flex unit (fr)
     */
    constexpr bool is_fr() const {
        return type == SizingFunctionType::Fr;
    }

    /**
     * Returns true if the sizing function uses a percentage
     */
    constexpr bool uses_percentage() const {
        return type == SizingFunctionType::Percent ||
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
                if (axis_available_space < 0.0f) return (float)INFINITY;
                return expression ? fmaxf(0.0f, radiant::resolve_computed_length_percentage(
                    expression, axis_available_space)) : axis_available_space * (value / 100.0f);
            default:
                return (float)INFINITY;
        }
    }

private:
    constexpr MaxTrackSizingFunction(SizingFunctionType type, float value, const CssValue* expression = nullptr)
        : TrackSizingFunctionValue(type, value, expression) {}
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
    static constexpr TrackSizingFunction Percent(float pct, const CssValue* expression = nullptr) {
        return TrackSizingFunction(MinTrackSizingFunction::Percent(pct, expression), MaxTrackSizingFunction::Percent(pct, expression));
    }
    static constexpr TrackSizingFunction Fr(float flex) {
        // Fr tracks have auto min and fr max
        return TrackSizingFunction(MinTrackSizingFunction::Auto(), MaxTrackSizingFunction::Fr(flex));
    }
    static constexpr TrackSizingFunction FitContent(float px) {
        // fit-content() has auto min and fit-content max
        return TrackSizingFunction(MinTrackSizingFunction::Auto(), MaxTrackSizingFunction::FitContentPx(px));
    }
    static constexpr TrackSizingFunction FitContentPercent(float pct, const CssValue* expression = nullptr) {
        return TrackSizingFunction(MinTrackSizingFunction::Auto(), MaxTrackSizingFunction::FitContentPercent(pct, expression));
    }
    constexpr bool is_flexible() const { return max.is_fr(); }
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

    /** Get the fit-content limit (infinity if not a fit-content track) */
    float fit_content_limit(float axis_available_space) const {
        return max_track_sizing_function.fit_content_limit(axis_available_space);
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

// ============================================================================
// Scoped growable storage shared by tracks, indices, placements and contributions.
// ============================================================================

template<typename T>
struct GridArray : lam::ArrayList<T> {
    using Base = lam::ArrayList<T>;
    bool complete;
    bool* owner_complete;

    explicit GridArray(bool* owner = nullptr)
        : Base(MEM_CAT_LAYOUT, 0), complete(true), owner_complete(owner) {}
    GridArray(const GridArray& source) : GridArray(source.owner_complete) { copy_from(source); }
    GridArray(GridArray&& source) noexcept
        : Base(static_cast<Base&&>(source)), complete(source.complete), owner_complete(source.owner_complete) {}
    GridArray& operator=(const GridArray& source) {
        if (this != &source) copy_from(source);
        return *this;
    }
    bool good() const { return complete; }
    void fail(const char* owner) {
        complete = false;
        if (owner_complete) *owner_complete = false;
        log_error("grid_storage: allocation failed in %s", owner);
    }
    bool reserve(size_t count) {
        if (!complete) return false;
        if (!Base::reserve(count)) { fail("reserve"); return false; }
        return true;
    }
    bool push_back(const T& value) {
        if (!complete) return false;
        if (!Base::push_back(value)) { fail("append"); return false; }
        return true;
    }
    void clear() { Base::clear(); complete = true; }
    bool copy_from(const GridArray& source) {
        if (!complete) return false;
        Base::clear();
        if (!source.good()) { fail("copy source"); return false; }
        if (!reserve(source.size())) return false;
        for (const T& value : source) if (!push_back(value)) return false;
        return true;
    }
};

struct TrackArray : GridArray<EnhancedGridTrack> {};

inline int grid_live_track_count(const TrackArray& tracks, size_t start, size_t span) {
    if (start >= tracks.size()) return 0;
    size_t end = span > tracks.size() - start ? tracks.size() : start + span;
    int count = 0;
    for (size_t index = start; index < end; index++) {
        if (tracks[index].kind == GridTrackKind::Track && !tracks[index].is_collapsed) count++;
    }
    return count;
}

// collapsed gutters overlap: keep one gutter between surviving tracks, none at the edges.
inline bool grid_track_has_gutter_after(const TrackArray& tracks, size_t index) {
    return index < tracks.size() && !tracks[index].is_collapsed &&
        tracks[index].kind == GridTrackKind::Track &&
        grid_live_track_count(tracks, index + 1, tracks.size()) > 0;
}

inline float grid_track_gaps(const TrackArray& tracks, size_t start, size_t span, float gap) {
    int count = grid_live_track_count(tracks, start, span);
    return count > 1 ? (count - 1) * gap : 0.0f;
}

inline float spanned_tracks_size(const TrackArray& tracks, size_t start, size_t span, float gap) {
    if (start >= tracks.size()) return 0.0f;
    size_t end = span > tracks.size() - start ? tracks.size() : start + span;
    float size = grid_track_gaps(tracks, start, span, gap);
    for (size_t index = start; index < end; index++) size += tracks[index].base_size;
    return size;
}

/** A track is definite when both sizing bounds resolve without content measurement. */
inline bool grid_track_is_definite(const EnhancedGridTrack& track) {
    SizingFunctionType min_type = track.min_track_sizing_function.type;
    SizingFunctionType max_type = track.max_track_sizing_function.type;
    bool definite_min = min_type == SizingFunctionType::Length ||
        min_type == SizingFunctionType::Percent;
    bool definite_max = max_type == SizingFunctionType::Length ||
        max_type == SizingFunctionType::Percent;
    return definite_min && definite_max;
}

/** Whether item content may increase the track beyond its resolved base size. */
inline bool grid_track_allows_content_growth(const EnhancedGridTrack& track) {
    SizingFunctionType max_type = track.max_track_sizing_function.type;
    return max_type == SizingFunctionType::Auto ||
        max_type == SizingFunctionType::MinContent ||
        max_type == SizingFunctionType::MaxContent ||
        max_type == SizingFunctionType::Fr;
}

struct IndexArray : GridArray<size_t> {
    explicit IndexArray(bool* owner = nullptr) : GridArray<size_t>(owner) {}
    template<typename Predicate>
    void erase_if(Predicate predicate) {
        size_t kept = 0;
        for (size_t i = 0; i < size(); i++) {
            if (!predicate((*this)[i])) (*this)[kept++] = (*this)[i];
        }
        remove_range(kept, size() - kept);
    }
};

} // namespace grid
} // namespace radiant

#endif // __cplusplus
