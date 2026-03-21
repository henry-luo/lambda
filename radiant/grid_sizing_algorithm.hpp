#pragma once

/**
 * Enhanced Grid Track Sizing Algorithm
 *
 * This module implements the CSS Grid track sizing algorithm as specified in
 * https://www.w3.org/TR/css-grid-1/#layout-algorithm
 *
 * The algorithm follows these steps:
 * 11.4 Initialize Track Sizes
 * 11.5 Resolve Intrinsic Track Sizes
 * 11.6 Maximize Tracks
 * 11.7 Expand Flexible Tracks
 * 11.8 Stretch auto Tracks
 *
 * Based on Taffy's implementation with adaptations for Radiant's architecture.
 *
 * TODO: std::* Migration Plan (Phase 5+) - COMPLEX
 * This file has extensive std::* usage requiring careful migration:
 * - TrackArray* → Pool-allocated arrays with count tracking
 * - IndexArray eligible_indices → Fixed-size arrays with MAX_TRACKS limit
 * - std::vector<GridItemContribution> → ArrayList* from lib/arraylist.h
 * - std::function<...> callbacks → Function pointers with void* context
 * - std::sort → qsort() from <stdlib.h>
 * - std::isinf/std::abs → isinf()/fabs() from <math.h>
 * - std::min/std::max → MIN_FLOAT/MAX_FLOAT macros
 * - std::numeric_limits → INFINITY, FLT_MAX from <math.h>/<float.h>
 * Estimated effort: Major refactoring (~500+ lines)
 */

#ifndef RADIANT_GRID_SIZING_ALGORITHM_HPP
#define RADIANT_GRID_SIZING_ALGORITHM_HPP

// Undefine min/max macros if defined (commonly from windows.h or view.hpp)
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "grid_track.hpp"
#include "grid_types.hpp"
#include <math.h>    // isinf, INFINITY, fabsf
#include <float.h>   // FLT_MAX
#include <algorithm>

struct LayoutContext;
struct ViewBlock;

namespace radiant {
namespace grid {

/**
 * Whether it is a minimum or maximum size's space being distributed.
 * This controls behaviour of the space distribution algorithm when distributing beyond limits.
 */
enum class IntrinsicContributionType {
    Minimum,
    Maximum
};

/**
 * Context for the track sizing algorithm
 */
struct TrackSizingContext {
    /** Tracks in the axis being sized */
    TrackArray* axis_tracks;

    /** Tracks in the other axis (for content sizing estimates) */
    TrackArray* other_axis_tracks;

    /** Available space in the sizing axis (may be indefinite = -1) */
    float axis_available_space;

    /** Container inner size in the sizing axis (may be indefinite = -1) */
    float axis_inner_size;

    /** Container inner size in the other axis (may be indefinite = -1) */
    float other_axis_inner_size;

    /** Gap between tracks */
    float gap;

    /** Alignment in the sizing axis */
    int axis_alignment; // CSS_ALIGN_* constants

    /** Minimum size constraint for the axis (or -1 if none) */
    float axis_min_size;

    /** Maximum size constraint for the axis (or -1 if none) */
    float axis_max_size;

    TrackSizingContext()
        : axis_tracks(nullptr)
        , other_axis_tracks(nullptr)
        , axis_available_space(-1)
        , axis_inner_size(-1)
        , other_axis_inner_size(-1)
        , gap(0)
        , axis_alignment(0)
        , axis_min_size(-1)
        , axis_max_size(-1)
    {}
};

// --- 11.4 Initialize Track Sizes ---

/**
 * Initialize each track's base size and growth limit based on its sizing functions.
 *
 * @param tracks The tracks to initialize
 * @param axis_inner_size The inner size of the container in this axis (or -1 if indefinite)
 */
inline void initialize_track_sizes(
    TrackArray& tracks,
    float axis_inner_size
) {
    for (auto& track : tracks) {
        // For each track, if the track's min track sizing function is:
        // - A fixed sizing function: Resolve to an absolute length and use as initial base size
        // - An intrinsic sizing function: Use an initial base size of zero
        float resolved_min = track.min_track_sizing_function.resolve(axis_inner_size);
        track.base_size = (resolved_min >= 0) ? resolved_min : 0.0f;

        // For each track, if the track's max track sizing function is:
        // - A fixed sizing function: Resolve to an absolute length and use as initial growth limit
        // - An intrinsic or flexible sizing function: Use an initial growth limit of infinity
        // - fit-content(N): growth_limit is capped at the fit-content argument N
        float resolved_max = track.max_track_sizing_function.resolve(axis_inner_size);
        if (resolved_max >= 0) {
            track.growth_limit = resolved_max;
        } else if (track.max_track_sizing_function.type == SizingFunctionType::FitContentPx ||
                   track.max_track_sizing_function.type == SizingFunctionType::FitContentPercent) {
            // fit-content(N) tracks: growth_limit = N (the cap argument)
            float limit = track.max_track_sizing_function.fit_content_limit(axis_inner_size);
            track.growth_limit = (limit >= 0) ? limit : (float)INFINITY;
        } else {
            track.growth_limit = (float)INFINITY;
        }

        // In all cases, if the growth limit is less than the base size,
        // increase the growth limit to match the base size
        if (track.growth_limit < track.base_size) {
            track.growth_limit = track.base_size;
        }

        // Reset scratch values
        track.reset_scratch_values();
    }
}

// --- Helper functions for space distribution ---

/**
 * Add any planned base size increases to the base size after a round of distributing space.
 * Reset the planned base size increase to zero ready for the next round.
 */
inline void flush_planned_base_size_increases(TrackArray& tracks) {
    for (auto& track : tracks) {
        track.base_size += track.base_size_planned_increase;
        track.base_size_planned_increase = 0.0f;
    }
}

/**
 * Add any planned growth limit increases to the growth limit after a round of distributing space.
 * Reset the planned growth limit increase to zero ready for the next round.
 *
 * @param set_infinitely_growable Whether to mark tracks as infinitely growable
 */
inline void flush_planned_growth_limit_increases(
    TrackArray& tracks,
    bool set_infinitely_growable
) {
    for (auto& track : tracks) {
        if (track.growth_limit_planned_increase > 0.0f) {
            if (isinf(track.growth_limit)) {
                track.growth_limit = track.base_size + track.growth_limit_planned_increase;
            } else {
                track.growth_limit += track.growth_limit_planned_increase;
            }
            track.infinitely_growable = set_infinitely_growable;
        } else {
            track.infinitely_growable = false;
        }
        track.growth_limit_planned_increase = 0.0f;
    }
}

/**
 * Compute the sum of base sizes for a range of tracks
 */
inline float sum_base_sizes(
    const TrackArray& tracks,
    size_t start_index,
    size_t end_index
) {
    float sum = 0.0f;
    for (size_t i = start_index; i < end_index && i < tracks.size(); ++i) {
        sum += tracks[i].base_size;
    }
    return sum;
}

/**
 * Count tracks that match a predicate
 */
template<typename Predicate>
inline size_t count_tracks_matching(
    const TrackArray& tracks,
    Predicate pred
) {
    size_t count = 0;
    for (const auto& track : tracks) {
        if (pred(track)) ++count;
    }
    return count;
}

// ============================================================================
// 11.5 Resolve Intrinsic Track Sizes
// ============================================================================

/**
 * Item contribution information for track sizing.
 * Contains the min/max content sizes and which tracks the item spans.
 */
struct GridItemContribution {
    float min_content_contribution;    // Item's min-content size in this axis (actual text min-content)
    float max_content_contribution;    // Item's max-content size in this axis
    size_t track_start;                // First track index spanned (0-based)
    size_t track_span;                 // Number of tracks spanned
    bool crosses_flexible_track;       // Whether item spans any flexible (fr) track
    bool is_scroll_container;          // overflow != visible → automatic minimum size is 0 (CSS Grid §6.6)
    ViewBlock* item;                   // Reference to the item for debugging
};

#define MAX_GRID_ITEMS 256

// Fixed-capacity array of GridItemContribution (replaces std::vector<GridItemContribution>)
struct ContribArray {
    GridItemContribution data[MAX_GRID_ITEMS];
    size_t count;
    ContribArray() : count(0) {}
    size_t size() const { return count; }
    bool empty() const { return count == 0; }
    GridItemContribution& operator[](size_t i) { return data[i]; }
    const GridItemContribution& operator[](size_t i) const { return data[i]; }
    void push_back(const GridItemContribution& c) { if (count < MAX_GRID_ITEMS) data[count++] = c; }
    void reserve(size_t) {} // no-op
    void clear() { count = 0; }
    GridItemContribution* begin() { return data; }
    GridItemContribution* end()   { return data + count; }
    const GridItemContribution* begin() const { return data; }
    const GridItemContribution* end()   const { return data + count; }
};

/**
 * Calculate the space already accounted for by spanned tracks.
 * Used when distributing item contributions across multiple tracks.
 */
inline float spanned_tracks_size(
    const TrackArray& tracks,
    size_t start_index,
    size_t span,
    float gap
) {
    float sum = 0.0f;
    size_t end = std::min(start_index + span, tracks.size());
    for (size_t i = start_index; i < end; ++i) {
        sum += tracks[i].base_size;
    }
    // Add gaps between tracks (span - 1 gaps)
    if (span > 1) {
        sum += (span - 1) * gap;
    }
    return sum;
}

/**
 * Increase the base size of intrinsic tracks that an item spans.
 * This implements the "distribute extra space" step from §11.5.
 *
 * @param tracks The axis tracks
 * @param start_index Starting track index
 * @param span Number of tracks spanned
 * @param space_to_distribute The extra space to distribute
 * @param contribution_type Whether this is for min or max content
 * @param include_min_content_max_tracks If true, min-content MAX tracks are eligible for
 *        Maximum distribution (used for scroll-container items whose minimum_contribution=0)
 */
inline void increase_sizes_for_spanning_item(
    TrackArray& tracks,
    size_t start_index,
    size_t span,
    float space_to_distribute,
    IntrinsicContributionType contribution_type,
    bool include_min_content_max_tracks = false
) {
    if (space_to_distribute <= 0 || span == 0) return;

    size_t end = std::min(start_index + span, tracks.size());

    // CSS Grid §11.5 Phase 2 (max-content contributions) requires a two-tier priority:
    //   Tier 1: tracks with max-content max sizing function
    //   Tier 2: tracks with auto / fit-content / percent max sizing function
    // Tier 1 tracks grow first; tier 2 only receives the remainder.
    // Phase 1 (min-content) has no inter-track priority, so all eligible tracks are
    // treated equally (single tier).

    // Helper lambda: build eligible track list filtered by a predicate
    auto build_eligible = [&](auto include) {
        IndexArray result;
        for (size_t i = start_index; i < end; ++i) {
            const auto& track = tracks[i];
            bool is_truly_flexible = track.is_flexible() && track.max_track_sizing_function.flex_factor() > 0;
            if (is_truly_flexible) continue;
            if (!include(track)) continue;
            bool is_intrinsic = track.min_track_sizing_function.is_intrinsic() ||
                                track.max_track_sizing_function.is_intrinsic();
            bool is_zero_fr = track.is_flexible() && track.max_track_sizing_function.flex_factor() == 0;
            if (is_intrinsic || is_zero_fr) result.push_back(i);
        }
        return result;
    };

    // Build the full eligible set (used for Phase 1 or as fallback)
    // CSS Grid §11.5.7: "content-based" (intrinsic) max sizing functions include
    // max-content, auto, and fit-content(). min-content max tracks are normally excluded
    // from max-content distribution UNLESS the item is a scroll container (whose automatic
    // minimum is zero) — in that case, min-content MAX tracks must participate so they
    // receive the item's max-content contribution rather than staying at zero.
    auto max_type_filter = [&](const EnhancedGridTrack& track) {
        if (contribution_type != IntrinsicContributionType::Maximum) return true;
        auto max_type = track.max_track_sizing_function.type;
        if (include_min_content_max_tracks && max_type == SizingFunctionType::MinContent) {
            return true;  // scroll-container items: include min-content MAX tracks
        }
        return max_type == SizingFunctionType::MaxContent ||
               max_type == SizingFunctionType::Auto ||
               max_type == SizingFunctionType::FitContentPx ||
               max_type == SizingFunctionType::FitContentPercent ||
               max_type == SizingFunctionType::Percent;
    };

    // Helper lambda: run the "fill smallest first" distribution loop on a set of tracks
    bool is_phase1 = (contribution_type == IntrinsicContributionType::Minimum);
    // CSS Grid §11.5: base_size increases should NOT be capped at growth_limit
    // for tracks with intrinsic min sizing functions. After distribution, the
    // invariant "growth_limit >= base_size" is enforced, raising growth_limit.
    // Phase 1: fit-content max tracks, min-content/max-content min tracks uncapped.
    // Phase 2: all tracks with intrinsic min (auto, min-content, max-content) uncapped,
    //          because base_size must reach the item's max-content contribution.
    auto no_cap = [&](const EnhancedGridTrack& track) -> bool {
        if (isinf(track.growth_limit)) return true;
        if (is_phase1) {
            auto max_type = track.max_track_sizing_function.type;
            if (max_type == SizingFunctionType::FitContentPx ||
                max_type == SizingFunctionType::FitContentPercent) return true;
            auto mt = track.min_track_sizing_function.type;
            return mt == SizingFunctionType::MinContent || mt == SizingFunctionType::MaxContent;
        } else {
            // Phase 2 (Maximum): only max-content MIN tracks are uncapped.
            // minmax(max-content, <percentage>) needs base_size to reach max-content
            // even when growth_limit is set by the percentage. Auto and min-content
            // min tracks must remain capped at growth_limit during Phase 2.
            auto mt = track.min_track_sizing_function.type;
            return mt == SizingFunctionType::MaxContent;
        }
    };

    // Compute the effective cap for a track during distribution.
    // Uses the no_cap logic to determine if the track should be capped at its growth_limit
    // or allowed to grow without limit.
    auto effective_cap = [&](const EnhancedGridTrack& track) -> float {
        if (no_cap(track)) return (float)INFINITY;
        return isinf(track.growth_limit) ? (float)INFINITY
                                              : track.growth_limit;
    };

    auto distribute = [&](IndexArray eligible_indices, float space) -> float {
        if (eligible_indices.empty() || space <= 0) return space;
        float remaining = space;
        while (remaining > 0.01f && !eligible_indices.empty()) {
            float min_base = FLT_MAX;
            float second_min_base = FLT_MAX;
            for (size_t idx : eligible_indices) {
                float bs = tracks[idx].base_size;
                if (bs < min_base) { second_min_base = min_base; min_base = bs; }
                else if (bs < second_min_base) { second_min_base = bs; }
            }
            IndexArray min_tracks;
            for (size_t idx : eligible_indices) {
                if (fabsf(tracks[idx].base_size - min_base) < 0.01f) min_tracks.push_back(idx);
            }
            if (isinf(second_min_base) || second_min_base == min_base) {
                float per_track = remaining / min_tracks.size();
                float total_given = 0.0f;
                for (size_t idx : min_tracks) {
                    auto& track = tracks[idx];
                    float cap = effective_cap(track);
                    float room = isinf(cap) ? per_track
                                 : std::max(0.0f, cap - track.base_size);
                    float given = std::min(per_track, room);
                    track.base_size += given;
                    total_given += given;
                }
                // Subtract only the space that was actually given; capped tracks leave
                // a remainder that the outer loop will redistribute to uncapped tracks.
                remaining -= total_given;
            } else {
                float gap_amount = second_min_base - min_base;
                float total_needed = gap_amount * min_tracks.size();
                if (total_needed <= remaining) {
                    float total_actually_given = 0.0f;
                    for (size_t idx : min_tracks) {
                        auto& track = tracks[idx];
                        float cap = effective_cap(track);
                        float new_base = isinf(cap) ? second_min_base
                                         : std::min(second_min_base, cap);
                        float given = new_base - track.base_size;
                        track.base_size = new_base;
                        total_actually_given += given;
                    }
                    remaining -= total_actually_given;
                } else {
                    float per_track = remaining / min_tracks.size();
                    for (size_t idx : min_tracks) {
                        auto& track = tracks[idx];
                        float cap = effective_cap(track);
                        float room = isinf(cap) ? per_track
                                     : std::max(0.0f, cap - track.base_size);
                        track.base_size += std::min(per_track, room);
                    }
                    remaining = 0;
                }
            }
            eligible_indices.erase_if([&tracks, &effective_cap](size_t idx) {
                float cap = effective_cap(tracks[idx]);
                return !isinf(cap) && tracks[idx].base_size >= cap - 0.01f;
            });
        }
        return remaining;
    };

    // Build eligible tracks (filtered by phase rules)
    IndexArray all_eligible = build_eligible(max_type_filter);
    if (all_eligible.empty()) return;

    if (contribution_type == IntrinsicContributionType::Maximum) {
        // CSS §11.5.1.4: for max-content, distribute to max-content tracks first,
        // then to auto/fit-content/percent tracks with any remaining space.
        auto is_max_content_tier = [](const EnhancedGridTrack& t) {
            return t.max_track_sizing_function.type == SizingFunctionType::MaxContent;
        };
        IndexArray tier1, tier2;
        for (size_t idx : all_eligible) {
            if (is_max_content_tier(tracks[idx])) tier1.push_back(idx);
            else tier2.push_back(idx);
        }
        float remaining_after_tier1 = distribute(tier1, space_to_distribute);
        if (remaining_after_tier1 > 0.01f && !tier2.empty()) {
            distribute(tier2, remaining_after_tier1);
        }
    } else {
        // Phase 1 (min-content): distribute to all eligible tracks equally (no priority tiers)
        distribute(all_eligible, space_to_distribute);
    }

    // After distribution: update growth_limit for ALL tracks in the spanned range.
    // CSS §7.2.1: "If a track's growth limit is less than its base size, increase the
    // growth limit to equal the base size."
    size_t span_end = std::min(start_index + span, tracks.size());
    for (size_t i = start_index; i < span_end; ++i) {
        auto& track = tracks[i];
        if (contribution_type == IntrinsicContributionType::Maximum) {
            if (!isinf(track.growth_limit)) {
                track.growth_limit = std::max(track.growth_limit, track.base_size);
            }
        }
        // Always: ensure growth_limit >= base_size (spec invariant)
        if (track.growth_limit < track.base_size) {
            track.growth_limit = track.base_size;
        }
    }
}

// ============================================================================
// ItemBatcher - Process items in correct order per CSS Grid §11.5
// ============================================================================

/**
 * Determine if an item crosses any flexible track.
 *
 * @param tracks All tracks in the axis
 * @param start_index Starting track index (0-based)
 * @param span Number of tracks spanned
 * @return true if any spanned track is flexible (has non-zero fr unit)
 */
inline bool item_crosses_flexible_track(
    const TrackArray& tracks,
    size_t start_index,
    size_t span
) {
    size_t end = std::min(start_index + span, tracks.size());
    for (size_t i = start_index; i < end; ++i) {
        // Only consider tracks with non-zero fr as truly flexible.
        // Tracks with 0fr should be sized by intrinsic contributions since
        // they don't flex at all.
        if (tracks[i].is_flexible() && tracks[i].max_track_sizing_function.flex_factor() > 0) {
            return true;
        }
    }
    return false;
}

/**
 * Sort item contributions for CSS Grid §11.5 processing order.
 *
 * Items must be processed in the following order:
 * 1. Items NOT crossing flexible tracks, ordered by ascending span
 * 2. Items crossing flexible tracks, ordered by ascending span
 *
 * This is equivalent to Taffy's ItemBatcher approach.
 */
inline void sort_contributions_for_intrinsic_sizing(
    ContribArray& contributions
) {
    std::sort(contributions.begin(), contributions.end(),
        [](const GridItemContribution& a, const GridItemContribution& b) {
            // Primary: non-flex items before flex items
            if (a.crosses_flexible_track != b.crosses_flexible_track) {
                return !a.crosses_flexible_track;  // false < true, so non-flex first
            }
            // Secondary: smaller span first
            return a.track_span < b.track_span;
        });
}

/**
 * Process items sorted by span count (ascending), with flex/non-flex separation.
 * Items with smaller spans are processed first per CSS Grid spec §11.5.
 * Items NOT crossing flexible tracks are processed before those that do.
 *
 * This implements the full ItemBatcher pattern from Taffy.
 *
 * @param tracks The axis tracks to size
 * @param contributions Vector of item contributions (will be sorted)
 * @param gap Gap between tracks
 */
inline void resolve_intrinsic_track_sizes(
    TrackArray& tracks,
    ContribArray& contributions,
    float gap,
    float axis_inner_size = -1.0f
) {
    if (contributions.empty() || tracks.empty()) return;

    // Mark which items cross flexible tracks
    for (auto& contrib : contributions) {
        contrib.crosses_flexible_track = item_crosses_flexible_track(
            tracks, contrib.track_start, contrib.track_span);
    }

    // Sort: non-flex first, then by span count (ascending)
    sort_contributions_for_intrinsic_sizing(contributions);
    // NOTE: Items crossing flexible tracks are skipped - their contribution
    // will be handled by the flexible track sizing in expand_flexible_tracks()
    for (const auto& contrib : contributions) {
        if (contrib.track_span == 0) continue;

        // Skip items that cross flexible tracks - they're handled in §11.7
        if (contrib.crosses_flexible_track) continue;

        float current_size = spanned_tracks_size(tracks, contrib.track_start, contrib.track_span, gap);
        // CSS §12.5.1 Phase 1: Use the "automatic minimum size" (minimum_contribution).
        // For scroll containers (overflow != visible), the automatic minimum size is 0.
        // The real min-content is used later in Phase 1b for min-content MIN tracks.
        //
        // EXCEPTION: When ALL spanned tracks have MaxContent min sizing AND none of them
        // has a Phase-2-eligible max (MaxContent or Auto), Phase 2 will not grow these
        // tracks at all. In that case, use max-content in Phase 1 so the track reaches its
        // correct size. This handles degenerate minmax(max-content, min-content) tracks
        // where the effective sizing IS max-content but Phase 2 won't fire.
        float minimum_contribution = contrib.is_scroll_container ? 0.0f : contrib.min_content_contribution;
        float p1_contrib = minimum_contribution;
        {
            size_t p1_end = std::min(contrib.track_start + contrib.track_span, tracks.size());
            bool all_max_content_min = true;
            bool any_phase2_eligible = false;
            for (size_t ii = contrib.track_start; ii < p1_end; ++ii) {
                if (tracks[ii].min_track_sizing_function.type != SizingFunctionType::MaxContent) {
                    all_max_content_min = false;
                }
                auto max_t = tracks[ii].max_track_sizing_function.type;
                if (max_t == SizingFunctionType::MaxContent ||
                    max_t == SizingFunctionType::Auto ||
                    max_t == SizingFunctionType::FitContentPx ||
                    max_t == SizingFunctionType::FitContentPercent ||
                    max_t == SizingFunctionType::Percent) {
                    any_phase2_eligible = true;
                }
            }
            // Only use max-content when ALL tracks have MaxContent min AND none can benefit
            // from Phase 2 (no MaxContent/Auto max tracks). This is the fallback that
            // ensures maxContent-min tracks reach their required size when Phase 2 won't fire.
            if (all_max_content_min && !any_phase2_eligible) {
                p1_contrib = contrib.max_content_contribution;
            }
        }
        float extra_space = p1_contrib - current_size;

        if (extra_space > 0) {
            log_debug("grid phase1 distributing: track=%zu span=%zu p1_contrib=%.1f current=%.1f extra=%.1f",
                     contrib.track_start, contrib.track_span, p1_contrib, current_size, extra_space);
            increase_sizes_for_spanning_item(
                tracks,
                contrib.track_start,
                contrib.track_span,
                extra_space,
                IntrinsicContributionType::Minimum
            );
        }
    }

    // Flush planned increases to base sizes
    flush_planned_base_size_increases(tracks);

    // Phase 2a (Taffy "content-based minimums"):
    // For scroll-container items: distribute min_content_contribution to tracks with a
    // min-content or max-content MIN sizing function. This ensures min-content MIN tracks
    // receive at least the item's min-content size (their own sizing function requirement).
    // Non-scroll containers: skip (their min-content is already handled in Phase 1).
    //
    // Two-tier distribution for scroll containers:
    //   Tier 1: Distribute min_content to non-fit-content-max tracks (MinContent/MaxContent MAX).
    //           These tracks absorb the item's min-content contribution first.
    //   Tier 2: Distribute remaining max_content to fit-content-max tracks,
    //           each capped at its fit-content argument.
    // This matches browser behavior where min-content tracks receive their full contribution
    // before fit-content tracks get their share.
    for (const auto& contrib : contributions) {
        if (contrib.track_span == 0) continue;
        if (contrib.crosses_flexible_track) continue;
        if (!contrib.is_scroll_container) continue;
        if (contrib.min_content_contribution <= 0) continue;

        size_t end = std::min(contrib.track_start + contrib.track_span, tracks.size());
        // Check if any spanned track has a min-content or max-content MIN sizing function
        bool has_eligible = false;
        for (size_t i = contrib.track_start; i < end; ++i) {
            auto min_type = tracks[i].min_track_sizing_function.type;
            if (min_type == SizingFunctionType::MinContent ||
                min_type == SizingFunctionType::MaxContent) {
                has_eligible = true;
                break;
            }
        }
        if (!has_eligible) continue;

        float current_size = spanned_tracks_size(tracks, contrib.track_start, contrib.track_span, gap);
        float extra_space = contrib.min_content_contribution - current_size;
        if (extra_space <= 0) continue;

        // Tier 1: distribute min_content to non-fit-content-max tracks
        {
            IndexArray tier1;
            for (size_t i = contrib.track_start; i < end; ++i) {
                auto min_type = tracks[i].min_track_sizing_function.type;
                auto max_type = tracks[i].max_track_sizing_function.type;
                bool is_truly_flexible = tracks[i].is_flexible() &&
                                         tracks[i].max_track_sizing_function.flex_factor() > 0;
                if (is_truly_flexible) continue;
                if ((min_type == SizingFunctionType::MinContent ||
                     min_type == SizingFunctionType::MaxContent) &&
                    max_type != SizingFunctionType::FitContentPx &&
                    max_type != SizingFunctionType::FitContentPercent) {
                    tier1.push_back(i);
                }
            }
            if (!tier1.empty()) {
                float per = extra_space / (float)tier1.size();
                for (size_t idx : tier1) {
                    tracks[idx].base_size += per;
                }
            }
        }

        // Note: fit-content tracks are NOT given base_size here. They get their value
        // from maximize_tracks (§11.6) growing them to their growth_limit (= fit-content limit).
        // This prevents fit-content tracks from "stealing" space from other tracks when
        // the item spans many columns.

        // Ensure growth_limit >= base_size invariant
        for (size_t i = contrib.track_start; i < end; ++i) {
            if (tracks[i].growth_limit < tracks[i].base_size) {
                tracks[i].growth_limit = tracks[i].base_size;
            }
        }
    }

    flush_planned_base_size_increases(tracks);

    // Phase 2b: §11.5 step 2.3 "max-content minimums"
    // Distribute max-content contributions to tracks with a max-content MIN sizing function.
    // Per the spec, only tracks whose MIN function is max-content receive their base_size
    // increased to accommodate the item's max-content contribution. Other intrinsic-MAX
    // tracks (auto, fit-content) get their final sizes via growth_limit distribution
    // (steps 2.4/2.5) followed by maximize_tracks (§11.6).
    // For scroll containers: distribute to auto-min tracks (automatic minimum = 0),
    // using limited_max_content capped by the spanned fit-content limit.
    // NOTE: Items crossing flexible tracks are skipped here too.
    for (const auto& contrib : contributions) {
        if (contrib.track_span == 0) continue;

        // Skip items that cross flexible tracks
        if (contrib.crosses_flexible_track) continue;

        size_t end = std::min(contrib.track_start + contrib.track_span, tracks.size());

        if (contrib.is_scroll_container) {
            // For scroll containers: two distributions in Phase 2b:
            //
            // (A) Distribute limited_max_content to auto-min/not-min-content-MAX tracks.
            //     This handles the scroll-container-specific rule where automatic minimum = 0
            //     and auto-MIN tracks need max-content growth.
            //
            // (B) CSS §12.5 Step 2.3 (max-content minimums): distribute max_content
            //     to max-content MIN tracks. This is the standard step that applies equally
            //     to scroll and non-scroll containers — MaxContent MIN tracks must reach
            //     the item's max-content contribution regardless of overflow.

            // --- Part (A): auto-MIN distribution ---
            float spanned_fc_limit = (float)INFINITY;
            for (size_t i = contrib.track_start; i < end; ++i) {
                auto max_type = tracks[i].max_track_sizing_function.type;
                if (max_type == SizingFunctionType::FitContentPx) {
                    spanned_fc_limit = std::min(spanned_fc_limit,
                                                tracks[i].max_track_sizing_function.value);
                }
            }
            float limited_max = std::min(contrib.max_content_contribution, spanned_fc_limit);
            log_debug("grid phase2b-SC: track=%zu span=%zu max=%f fc_limit=%f limited=%f",
                     contrib.track_start, contrib.track_span,
                     contrib.max_content_contribution, spanned_fc_limit, limited_max);
            if (limited_max > 0) {
                bool has_auto_min = false;
                for (size_t i = contrib.track_start; i < end; ++i) {
                    auto min_type = tracks[i].min_track_sizing_function.type;
                    auto max_type = tracks[i].max_track_sizing_function.type;
                    if (min_type == SizingFunctionType::Auto &&
                        max_type != SizingFunctionType::MinContent) {
                        has_auto_min = true;
                        break;
                    }
                }
                if (has_auto_min) {
                    float current_size = spanned_tracks_size(tracks, contrib.track_start, contrib.track_span, gap);
                    float extra_space = limited_max - current_size;
                    if (extra_space > 0) {
                        increase_sizes_for_spanning_item(
                            tracks,
                            contrib.track_start,
                            contrib.track_span,
                            extra_space,
                            IntrinsicContributionType::Maximum,
                            false
                        );
                    }
                }
            }

            // --- Part (B): Step 2.3 max-content minimums for MaxContent MIN tracks ---
            {
                bool has_mc_min = false;
                for (size_t i = contrib.track_start; i < end; ++i) {
                    auto min_type = tracks[i].min_track_sizing_function.type;
                    auto max_type = tracks[i].max_track_sizing_function.type;
                    if (min_type == SizingFunctionType::MaxContent &&
                        max_type != SizingFunctionType::MinContent) {
                        has_mc_min = true;
                        break;
                    }
                }
                if (has_mc_min) {
                    float current_size = spanned_tracks_size(tracks, contrib.track_start, contrib.track_span, gap);
                    float extra_space = contrib.max_content_contribution - current_size;
                    if (extra_space > 0) {
                        // Distribute to MaxContent MIN tracks only
                        IndexArray mc_min_tracks;
                        for (size_t i = contrib.track_start; i < end; ++i) {
                            auto& track = tracks[i];
                            if (track.min_track_sizing_function.type != SizingFunctionType::MaxContent) continue;
                            auto max_type = track.max_track_sizing_function.type;
                            if (max_type == SizingFunctionType::MinContent) continue;
                            bool is_truly_flex = track.is_flexible() &&
                                                track.max_track_sizing_function.flex_factor() > 0;
                            if (is_truly_flex) continue;
                            mc_min_tracks.push_back(i);
                        }
                        if (!mc_min_tracks.empty()) {
                            float remaining = extra_space;
                            float per_track = remaining / mc_min_tracks.size();
                            for (size_t idx : mc_min_tracks) {
                                tracks[idx].base_size += per_track;
                            }
                        }
                    }
                }

                // Ensure growth_limit >= base_size invariant
                for (size_t i = contrib.track_start; i < end; ++i) {
                    if (tracks[i].growth_limit < tracks[i].base_size) {
                        tracks[i].growth_limit = tracks[i].base_size;
                    }
                }
            }

        } else {
            // For non-scroll containers: §11.5 step 2.3 — distribute max_content
            // to tracks with a max-content MIN sizing function.
            bool has_eligible_track = false;
            for (size_t i = contrib.track_start; i < end; ++i) {
                auto min_type = tracks[i].min_track_sizing_function.type;
                auto max_type = tracks[i].max_track_sizing_function.type;
                // Must have max-content MIN AND an eligible MAX type for distribution
                // (MinContent MAX is excluded from max-content distribution per §11.5.1)
                if (min_type == SizingFunctionType::MaxContent &&
                    max_type != SizingFunctionType::MinContent) {
                    has_eligible_track = true;
                    break;
                }
            }
            if (!has_eligible_track) continue;

            float current_size = spanned_tracks_size(tracks, contrib.track_start, contrib.track_span, gap);
            float extra_space = contrib.max_content_contribution - current_size;
            log_debug("grid phase2b step2.3: track=%zu span=%zu max_content=%.1f current=%.1f extra=%.1f",
                     contrib.track_start, contrib.track_span,
                     contrib.max_content_contribution, current_size, extra_space);
            if (extra_space <= 0) continue;

            // Distribute to max-content MIN tracks, excluding those with MinContent MAX.
            // Uses tiered distribution: MaxContent MAX tracks first, then other eligible MAX.
            IndexArray mc_min_tracks;
            for (size_t i = contrib.track_start; i < end; ++i) {
                auto& track = tracks[i];
                if (track.min_track_sizing_function.type != SizingFunctionType::MaxContent) continue;
                // Apply max_type_filter: exclude MinContent MAX, include MaxContent/Auto/FC/Percent
                auto max_type = track.max_track_sizing_function.type;
                if (max_type == SizingFunctionType::MinContent) continue;
                bool is_truly_flex = track.is_flexible() &&
                                    track.max_track_sizing_function.flex_factor() > 0;
                if (is_truly_flex) continue;
                mc_min_tracks.push_back(i);
            }
            if (mc_min_tracks.empty()) continue;

            // Distribute to max-content MIN tracks without growth_limit cap.
            // Per CSS §11.5: tracks with max-content MIN are uncapped during Phase 2
            // since base_size must reach the item's max-content contribution.
            float remaining = extra_space;
            float per_track = remaining / mc_min_tracks.size();
            for (size_t idx : mc_min_tracks) {
                tracks[idx].base_size += per_track;
            }

            // Ensure growth_limit >= base_size invariant
            for (size_t i = contrib.track_start; i < end; ++i) {
                if (tracks[i].growth_limit < tracks[i].base_size) {
                    tracks[i].growth_limit = tracks[i].base_size;
                }
            }

        }
    }

    // §11.5 Step 2.4: Intrinsic maximums — increase growth_limit for intrinsic MAX tracks.
    // §11.5 Step 2.5: Max-content maximums — increase growth_limit for max-content MAX tracks.
    // These steps set finite growth_limits so that maximize_tracks (§11.6) can grow tracks
    // in indefinite containers. Without them, growth_limits stay infinite and maximize skips
    // them (it only grows finite-gl tracks in indefinite contexts).
    // Key: infinite growth_limits are treated as base_size for computing effective current size.
    for (const auto& contrib : contributions) {
        if (contrib.track_span == 0) continue;
        if (contrib.crosses_flexible_track) continue;

        size_t end = std::min(contrib.track_start + contrib.track_span, tracks.size());

        // Compute effective growth_limit sum across ALL spanned tracks.
        // ∞ gl → base_size per CSS Grid §11.5: "treat a growth limit of infinity as
        // the track's base size for the purpose of this computation."
        // Additionally, fit-content tracks with base=0 (no item contributions) use
        // effective gl=0 so they don't "claim" space from other tracks' growth allocation.
        auto effective_gl_sum = [&]() -> float {
            float sum = 0.0f;
            for (size_t i = contrib.track_start; i < end; ++i) {
                auto mt = tracks[i].max_track_sizing_function.type;
                bool is_fc = (mt == SizingFunctionType::FitContentPx ||
                              mt == SizingFunctionType::FitContentPercent);
                if (is_fc && tracks[i].base_size == 0) {
                    // Fit-content track with no contributions: don't count its gl
                    sum += 0.0f;
                } else if (isinf(tracks[i].growth_limit)) {
                    sum += tracks[i].base_size;
                } else {
                    sum += tracks[i].growth_limit;
                }
            }
            // Include gaps between spanned tracks (same as spanned_tracks_size)
            if (contrib.track_span > 1) {
                sum += (contrib.track_span - 1) * gap;
            }
            return sum;
        };

        // Helper: distribute space to growth_limits of eligible tracks
        auto distribute_to_gl = [&](float space, auto is_eligible) {
            if (space <= 0.01f) return;
            // Build eligible list
            IndexArray eligible;
            for (size_t i = contrib.track_start; i < end; ++i) {
                if (is_eligible(tracks[i])) eligible.push_back(i);
            }
            if (eligible.empty()) return;
            // Distribute evenly (simple equal distribution for growth_limits)
            float per_track = space / static_cast<float>(eligible.size());
            for (size_t idx : eligible) {
                auto& track = tracks[idx];
                float eff_gl = isinf(track.growth_limit)
                                   ? track.base_size : track.growth_limit;
                float new_gl = eff_gl + per_track;
                // CSS Grid §11.5.1: fit-content growth_limit must not exceed the limit
                auto mt = track.max_track_sizing_function.type;
                if (mt == SizingFunctionType::FitContentPx ||
                    mt == SizingFunctionType::FitContentPercent) {
                    float fc_limit = track.max_track_sizing_function.fit_content_limit(axis_inner_size);
                    if (fc_limit >= 0) new_gl = std::min(new_gl, fc_limit);
                }
                track.growth_limit = new_gl;
            }
        };

        // Step 2.4: Intrinsic maximums — distribute min-content to intrinsic MAX tracks
        // Intrinsic MAX = auto, min-content, max-content, fit-content
        {
            float extra = contrib.min_content_contribution - effective_gl_sum();
            distribute_to_gl(extra, [](const EnhancedGridTrack& t) {
                auto mt = t.max_track_sizing_function.type;
                return mt == SizingFunctionType::Auto ||
                       mt == SizingFunctionType::MinContent ||
                       mt == SizingFunctionType::MaxContent ||
                       mt == SizingFunctionType::FitContentPx ||
                       mt == SizingFunctionType::FitContentPercent;
            });
        }

        // Step 2.5: Max-content maximums — distribute max-content to max-content/auto MAX tracks
        // Per CSS Grid §11.5: targets tracks with max-content or auto max sizing function.
        // Note: Auto MAX is also spec-eligible but for scroll containers with only spanning
        // items, including Auto gives empty auto tracks finite gl that maximize inflates.
        // Only include Auto MAX for non-scroll-container items.
        {
            float extra = contrib.max_content_contribution - effective_gl_sum();
            bool include_auto = !contrib.is_scroll_container;
            distribute_to_gl(extra, [include_auto](const EnhancedGridTrack& t) {
                auto mt = t.max_track_sizing_function.type;
                if (mt == SizingFunctionType::MaxContent) return true;
                if (include_auto && mt == SizingFunctionType::Auto) return true;
                return false;
            });
        }
    }

    // After growth_limit distribution: ensure growth_limit >= base_size
    for (auto& track : tracks) {
        if (!isinf(track.growth_limit) && track.growth_limit < track.base_size) {
            track.growth_limit = track.base_size;
        }
    }

    // Clamp fit-content track growth_limits based on content from items.
    // FC tracks should grow to min(fit-content-limit, max-content-contribution)
    // for span=1 items, or to cover remaining content for spanning items.
    {
        // Track per-track max_content from span=1 items for fit-content clamping
        float fc_span1_max[MAX_GRID_TRACKS];
        float fc_max_growth[MAX_GRID_TRACKS];
        for (size_t i = 0; i < tracks.size(); ++i) { fc_span1_max[i] = -1.0f; fc_max_growth[i] = -1.0f; }
        for (const auto& contrib : contributions) {
            if (contrib.crosses_flexible_track) continue;
            size_t end = std::min(contrib.track_start + contrib.track_span, tracks.size());
            if (contrib.track_span == 1 && contrib.track_start < tracks.size()) {
                // Span=1: record max_content for this track
                size_t ti = contrib.track_start;
                auto mt = tracks[ti].max_track_sizing_function.type;
                if (mt == SizingFunctionType::FitContentPx ||
                    mt == SizingFunctionType::FitContentPercent) {
                    fc_span1_max[ti] = std::max(fc_span1_max[ti],
                                                contrib.max_content_contribution);
                }
            } else if (contrib.track_span > 1) {
                // Sum ALL base_sizes in this span (including FC tracks)
                float span_base_sum = 0;
                for (size_t i = contrib.track_start; i < end; ++i) {
                    span_base_sum += tracks[i].base_size;
                }
                // Remaining content after all current bases and gaps
                float remaining = contrib.max_content_contribution - span_base_sum;
                remaining -= (contrib.track_span - 1) * gap;
                remaining = std::max(0.0f, remaining);

                for (size_t i = contrib.track_start; i < end; ++i) {
                    auto mt = tracks[i].max_track_sizing_function.type;
                    if (mt == SizingFunctionType::FitContentPx ||
                        mt == SizingFunctionType::FitContentPercent) {
                        fc_max_growth[i] = std::max(fc_max_growth[i], remaining);
                    }
                }
            }
        }
        // Apply fit-content growth_limit clamping
        for (size_t i = 0; i < tracks.size(); ++i) {
            auto& track = tracks[i];
            auto mt = track.max_track_sizing_function.type;
            if (mt != SizingFunctionType::FitContentPx &&
                mt != SizingFunctionType::FitContentPercent) continue;
            if (track.growth_limit <= track.base_size) continue;

            if (fc_max_growth[i] >= 0) {
                // Spanning items: cap gl to cover remaining content
                float max_gl = track.base_size + std::min(track.growth_limit - track.base_size, fc_max_growth[i]);
                track.growth_limit = std::max(track.base_size, max_gl);
            } else if (fc_span1_max[i] >= 0) {
                // Span=1 items only: cap gl at max_content so maximize doesn't overshoot
                // e.g., fit-content(30px) with "HH" (max=20) → gl should be 20, not 30
                track.growth_limit = std::min(track.growth_limit, std::max(track.base_size, fc_span1_max[i]));
            } else {
                // No items at all: clamp gl to base_size so maximize doesn't inflate
                track.growth_limit = track.base_size;
            }
        }
    }

    // Phase 3: Set automatic minimum sizes for auto-min flexible tracks from items spanning them.
    // This establishes the base_size floor that expand_flexible_tracks() uses in its iterative
    // freeze algorithm. increase_sizes_for_spanning_item() skips flex tracks, so we must do
    // this directly here. CSS Grid §11.5 - items crossing flexible tracks handled here.
    for (const auto& contrib : contributions) {
        if (contrib.track_span == 0) continue;
        if (!contrib.crosses_flexible_track) continue;

        size_t p3_end = std::min(contrib.track_start + contrib.track_span, tracks.size());

        // CSS Grid §11.5 Phase 3: handle items crossing flexible tracks.
        // Flexible tracks with an intrinsic min sizing function (auto, min-content, or
        // max-content) must have their base_size set to the item's appropriate content
        // contribution so that expand_flexible_tracks() §11.7 has the correct floor.
        //
        // The content size used depends on the track's min sizing function:
        //   auto | min-content → use min_content_contribution
        //   max-content        → use max_content_contribution
        int flex_count = 0;
        float current_flex_size = 0.0f;
        for (size_t i = contrib.track_start; i < p3_end; ++i) {
            const auto& t = tracks[i];
            if (!t.is_flexible()) continue;
            auto mt = t.min_track_sizing_function.type;
            if (mt == SizingFunctionType::Auto ||
                mt == SizingFunctionType::MinContent ||
                mt == SizingFunctionType::MaxContent) {
                flex_count++;
                current_flex_size += t.base_size;
            }
        }

        if (flex_count == 0) continue;

        // Choose the item's contribution based on each flex track's min sizing function.
        // For a span that mixes auto-min and max-content-min flex tracks we compute a
        // combined "needed" size by applying the appropriate contribution per track.
        float non_flex_span_size = spanned_tracks_size(tracks, contrib.track_start, contrib.track_span, gap)
                                   - current_flex_size;

        // Gather flex tracks to update and their respective contribution floors
        struct FlexEntry { size_t idx; float contribution; float flex_factor; };
        FlexEntry flex_entries[MAX_GRID_TRACKS];
        size_t fe_count = 0;
        float total_flex_factor = 0.0f;
        for (size_t i = contrib.track_start; i < p3_end; ++i) {
            const auto& t = tracks[i];
            if (!t.is_flexible()) continue;
            auto mt = t.min_track_sizing_function.type;
            float item_contrib;
            if (mt == SizingFunctionType::MaxContent) {
                item_contrib = contrib.max_content_contribution;
            } else {
                // auto or min-content
                item_contrib = contrib.min_content_contribution;
            }
            if (mt == SizingFunctionType::Auto ||
                mt == SizingFunctionType::MinContent ||
                mt == SizingFunctionType::MaxContent) {
                if (fe_count < MAX_GRID_TRACKS) flex_entries[fe_count++] = {i, item_contrib, t.flex_factor()};
                total_flex_factor += t.flex_factor();
            }
        }

        if (total_flex_factor <= 0.0f || fe_count == 0) continue;

        // For each flex track, compute how much it needs to cover its portion of the item.
        // Use the track's own contribution type for the "required" size.
        // Distribute the excess proportionally by flex factor (CSS §11.7.1 pattern).
        for (size_t fe = 0; fe < fe_count; ++fe) {
            const auto& entry = flex_entries[fe];
            auto& track = tracks[entry.idx];
            // Total space the item needs, minus the non-flex portion and current flex size
            float remaining_needed = entry.contribution - non_flex_span_size - current_flex_size;
            if (remaining_needed <= 0) continue;

            float proportion = entry.flex_factor / total_flex_factor;
            float share = remaining_needed * proportion;
            if (share > track.base_size) {
                track.base_size = share;
                // growth_limit stays INFINITY for flex tracks - sized in expand_flexible_tracks
            }
        }
    }

    // Ensure growth limits are at least as large as base sizes
    for (auto& track : tracks) {
        if (track.growth_limit < track.base_size) {
            track.growth_limit = track.base_size;
        }
    }
}

// --- 11.6 Maximize Tracks ---

/**
 * Maximise Tracks.
 * Distributes free space (if any) to tracks with FINITE growth limits, up to their limits.
 */
inline void maximize_tracks(
    TrackArray& tracks,
    float axis_inner_size,
    float axis_available_space
) {
    // CSS Grid §11.6: Maximize Tracks.
    // When the container is indefinite (intrinsic sizing), we are computing the grid's
    // max-content size. Every non-flex track with a finite growth limit should be grown
    // to that limit — this is how the grid contributes its max-content to its container.
    // (Flex tracks are sized in §11.7; their growth_limit is infinity here.)
    if (axis_inner_size < 0 && axis_available_space < 0) {
        // Check if any non-fit-content track has a finite growth_limit
        // (set by step 2.5 growth_limit distribution). If so, fit-content tracks with
        // base=0 should NOT grow — their space was already accounted for in step 2.5.
        bool has_non_fc_finite_gl = false;
        for (const auto& track : tracks) {
            if (track.is_flexible() || isinf(track.growth_limit)) continue;
            auto mt = track.max_track_sizing_function.type;
            if (mt != SizingFunctionType::FitContentPx &&
                mt != SizingFunctionType::FitContentPercent &&
                mt != SizingFunctionType::Length &&
                mt != SizingFunctionType::Percent) {
                has_non_fc_finite_gl = true;
                break;
            }
        }
        for (auto& track : tracks) {
            if (track.is_flexible() || isinf(track.growth_limit)) continue;
            // Skip fit-content tracks with no contributions if other tracks claimed
            // growth via step 2.5 — their gl space was not counted in step 2.5.
            auto mt = track.max_track_sizing_function.type;
            if (has_non_fc_finite_gl && track.base_size == 0 &&
                (mt == SizingFunctionType::FitContentPx ||
                 mt == SizingFunctionType::FitContentPercent)) {
                continue;
            }
            track.base_size = track.growth_limit;
        }
        return;
    }

    float available = (axis_inner_size >= 0) ? axis_inner_size : axis_available_space;

    // Calculate current used space
    float used_space = 0.0f;
    for (const auto& track : tracks) {
        used_space += track.base_size;
    }

    float free_space = available - used_space;
    if (free_space <= 0) return;

    // CSS Grid §11.6: distribute free space to non-flexible tracks up to their growth limits.
    //
    // When flexible (fr) tracks are present: only grow finite-growth-limit non-flex tracks.
    // FR tracks claim their space via expand_flexible_tracks (§11.7); giving space to
    // infinite-growth-limit auto tracks here would steal from FR tracks.
    //
    // When NO flexible tracks: distribute to all non-flex tracks including those with
    // infinite growth limits (allowing unequal natural content-based distribution).
    bool has_fr_tracks = false;
    for (const auto& t : tracks) {
        if (t.is_flexible()) { has_fr_tracks = true; break; }
    }

    float space_to_distribute = free_space;

    while (space_to_distribute > 0.01f) {
        IndexArray eligible;
        for (size_t i = 0; i < tracks.size(); i++) {
            const auto& t = tracks[i];
            if (t.is_flexible()) continue;
            // When FR tracks exist, skip infinite-growth-limit tracks (let FR claim that space)
            if (has_fr_tracks && isinf(t.growth_limit)) continue;
            if (!isinf(t.growth_limit) && t.base_size >= t.growth_limit - 0.01f) continue;
            eligible.push_back(i);
        }
        if (eligible.empty()) break;

        float share = space_to_distribute / static_cast<float>(eligible.size());
        bool distributed = false;

        for (size_t i : eligible) {
            auto& track = tracks[i];
            float room = isinf(track.growth_limit)
                             ? share
                             : std::min(share, track.growth_limit - track.base_size);
            if (room <= 0) continue;
            track.base_size += room;
            space_to_distribute -= room;
            distributed = true;
        }

        if (!distributed) break;
    }
}

// --- 11.7 Expand Flexible Tracks ---

/**
 * Expand Flexible Tracks.
 * Sizes flexible tracks using the largest value it can assign to an fr without exceeding available space.
 */
inline void expand_flexible_tracks(
    TrackArray& tracks,
    float axis_min_size,
    float axis_max_size,
    float axis_available_space,
    const ContribArray& contributions = ContribArray(),
    float gap = 0.0f,
    float* out_intrinsic_total = nullptr
) {
    // If no flexible tracks, nothing to do
    float flex_factor_sum = 0.0f;
    for (const auto& track : tracks) {
        flex_factor_sum += track.flex_factor();
    }

    if (flex_factor_sum <= 0.0f) return;

    // Find available space for flexible tracks
    float used_by_non_flex = 0.0f;
    for (const auto& track : tracks) {
        if (!track.is_flexible()) {
            used_by_non_flex += track.base_size;
        }
    }

    // Determine available space
    float available = axis_available_space;
    if (available < 0) {
        // Indefinite container (shrink-to-fit / max-content sizing).
        // CSS §12.7.1: Two-pass approach:
        //   Pass 1: Compute the hypothetical fr size (used flex fraction) to determine
        //           the intrinsic container width.
        //   Pass 2: Re-run with that width as definite available space.

        // Save Phase 3 base_sizes as min floors for Pass 2
        size_t n = tracks.size();
        float phase3_base[MAX_GRID_TRACKS];
        for (size_t i = 0; i < n; ++i) phase3_base[i] = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            if (tracks[i].is_flexible()) phase3_base[i] = tracks[i].base_size;
        }

        // --- Pass 1: Compute used flex fraction ---
        float fr_size_indef = 0.0f;

        // Part (a): Per-track flex fraction (§12.7.1)
        // If flex > 1: base_size / flex_factor; else: base_size
        for (const auto& track : tracks) {
            if (!track.is_flexible() || track.flex_factor() <= 0.0f) continue;
            float contribution = (track.flex_factor() > 1.0f)
                ? track.base_size / track.flex_factor()
                : track.base_size;
            fr_size_indef = std::max(fr_size_indef, contribution);
        }

        // Part (b): "Find the Size of an fr" for each item crossing flex tracks (§12.7.1)
        for (const auto& contrib : contributions) {
            if (!contrib.crosses_flexible_track) continue;
            size_t p_end = std::min(contrib.track_start + (size_t)contrib.track_span, tracks.size());

            // Collect flex and non-flex info for tracks in this span
            float non_flex_size = 0.0f;
            int span_count = (int)(p_end - contrib.track_start);
            float span_gap = (span_count > 1) ? (span_count - 1) * gap : 0.0f;

            struct FlexInfo { size_t idx; float flex; float base; };
            FlexInfo flex_infos[MAX_GRID_TRACKS];
            size_t fi_count = 0;
            for (size_t i = contrib.track_start; i < p_end; ++i) {
                if (tracks[i].is_flexible()) {
                    if (fi_count < MAX_GRID_TRACKS) flex_infos[fi_count++] = {i, tracks[i].flex_factor(), tracks[i].base_size};
                } else {
                    non_flex_size += tracks[i].base_size;
                }
            }
            if (fi_count == 0) continue;

            float target = contrib.max_content_contribution - span_gap;
            // Iterative "Find the Size of an fr" sub-algorithm (§12.7.1)
            float fr_result = 0.0f;
            bool inflexible[MAX_GRID_TRACKS];
            for (size_t fi = 0; fi < fi_count; ++fi) inflexible[fi] = false;
            for (;;) {
                float leftover = target - non_flex_size;
                float sum_flex = 0.0f;
                for (size_t fi = 0; fi < fi_count; ++fi) {
                    if (inflexible[fi]) {
                        leftover -= flex_infos[fi].base;
                    } else {
                        sum_flex += flex_infos[fi].flex;
                    }
                }
                if (sum_flex <= 0.0f || leftover <= 0.0f) break;
                float eff_sum = std::max(sum_flex, 1.0f);
                float hyp_fr = leftover / eff_sum;
                bool any_frozen = false;
                for (size_t fi = 0; fi < fi_count; ++fi) {
                    if (inflexible[fi]) continue;
                    if (flex_infos[fi].flex * hyp_fr < flex_infos[fi].base - 0.001f) {
                        inflexible[fi] = true;
                        any_frozen = true;
                    }
                }
                if (!any_frozen) { fr_result = hyp_fr; break; }
            }
            fr_size_indef = std::max(fr_size_indef, fr_result);
        }

        // Assign initial track sizes: max(base_size, flex_factor * fr_size)
        float intrinsic_total = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            if (tracks[i].is_flexible()) {
                tracks[i].base_size = std::max(phase3_base[i], tracks[i].flex_factor() * fr_size_indef);
            }
            intrinsic_total += tracks[i].base_size;
        }
        // Add gaps
        int track_count = 0;
        for (const auto& t : tracks) { if (t.kind == GridTrackKind::Track) track_count++; }
        intrinsic_total += (track_count > 1) ? (track_count - 1) * gap : 0.0f;

        // --- Pass 2: Re-run with intrinsic total as definite available ---
        // The definite iterative freeze redistributes space with max(sum,1) clamping,
        // which can produce different (smaller) track sizes for sub-1 flex factors.
        if (out_intrinsic_total) *out_intrinsic_total = intrinsic_total;
        available = intrinsic_total;
        // Restore Phase 3 base_sizes as min floors
        for (size_t i = 0; i < n; ++i) {
            if (tracks[i].is_flexible()) tracks[i].base_size = phase3_base[i];
        }
        // Fall through to the definite iterative freeze below
    }

    // Apply min/max constraints
    if (axis_max_size >= 0) {
        available = std::min(available, axis_max_size);
    }
    if (axis_min_size >= 0) {
        available = std::max(available, axis_min_size);
    }

    float free_space = available - used_by_non_flex;
    if (free_space <= 0) return;

    // CSS §11.7.1 Iterative freeze algorithm.
    // Each flexible track has an effective minimum = max(base_size from Phase 3, resolved min fn).
    // If a flexible track's minimum exceeds its fr share, freeze it at that minimum and
    // recompute the fr size from the remaining unfrozen tracks. Repeat until stable.

    size_t n = tracks.size();
    float min_floor[MAX_GRID_TRACKS];
    bool frozen[MAX_GRID_TRACKS];
    for (size_t i = 0; i < n; ++i) { min_floor[i] = 0.0f; frozen[i] = false; }

    for (size_t i = 0; i < n; ++i) {
        if (!tracks[i].is_flexible()) {
            frozen[i] = true;  // non-flex tracks already at final size
        } else {
            float auto_min = tracks[i].base_size;  // automatic minimum from Phase 3
            float spec_min = tracks[i].min_track_sizing_function.resolve(axis_available_space);
            min_floor[i] = std::max(auto_min, (spec_min >= 0) ? spec_min : 0.0f);
        }
    }

    float fr_size = 0.0f;
    bool changed = true;
    while (changed) {
        changed = false;

        // Leftover space = available minus sum of frozen (non-flex + frozen-flex) track sizes
        float leftover = available;
        float sum_flex = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            if (frozen[i]) {
                leftover -= tracks[i].base_size;
            } else {
                sum_flex += tracks[i].flex_factor();
            }
        }

        if (sum_flex <= 0.0f) break;

        // CSS §11.7.1: if sum of flex factors < 1, use the sum as divisor to avoid over-distributing
        // (i.e., 0.3fr + 0.2fr of 100px = 30px + 20px, not 60px + 40px)
        float effective_flex_denom = std::max(sum_flex, 1.0f);
        fr_size = (leftover > 0.0f) ? (leftover / effective_flex_denom) : 0.0f;

        // Freeze any flexible track whose minimum exceeds its fr share
        for (size_t i = 0; i < n; ++i) {
            if (frozen[i]) continue;
            if (min_floor[i] > fr_size * tracks[i].flex_factor() + 0.001f) {
                tracks[i].base_size = min_floor[i];
                frozen[i] = true;
                changed = true;
            }
        }
    }

    // Assign final sizes to unfrozen flexible tracks
    for (size_t i = 0; i < n; ++i) {
        if (!tracks[i].is_flexible() || frozen[i]) continue;
        tracks[i].base_size = tracks[i].flex_factor() * fr_size;
        tracks[i].growth_limit = tracks[i].base_size;
    }

    // Sync growth_limit for frozen flexible tracks
    for (size_t i = 0; i < n; ++i) {
        if (tracks[i].is_flexible() && frozen[i]) {
            tracks[i].growth_limit = tracks[i].base_size;
        }
    }
}

// --- 11.8 Stretch auto Tracks ---

/**
 * Stretch auto Tracks.
 * Expands tracks that have an auto max track sizing function by dividing any remaining
 * positive, definite free space equally amongst them.
 */
inline void stretch_auto_tracks(
    TrackArray& tracks,
    float axis_min_size,
    float axis_available_space
) {
    // Count auto tracks (tracks with auto max sizing that aren't flexible)
    size_t auto_track_count = 0;
    float used_space = 0.0f;

    for (const auto& track : tracks) {
        used_space += track.base_size;
        if (track.max_track_sizing_function.type == SizingFunctionType::Auto && !track.is_flexible()) {
            auto_track_count++;
        }
    }

    if (auto_track_count == 0) return;

    // Determine available space
    float available = axis_available_space;
    if (available < 0) return;  // No definite space, nothing to stretch into

    if (axis_min_size >= 0) {
        available = std::max(available, axis_min_size);
    }

    float free_space = available - used_space;
    if (free_space <= 0) return;

    // Distribute free space equally among auto tracks
    float extra_per_track = free_space / auto_track_count;

    for (auto& track : tracks) {
        if (track.max_track_sizing_function.type == SizingFunctionType::Auto && !track.is_flexible()) {
            track.base_size += extra_per_track;
            if (!isinf(track.growth_limit)) {
                track.growth_limit += extra_per_track;
            }
        }
    }
}

// --- Main track sizing function ---

/**
 * Run the complete track sizing algorithm.
 *
 * @param ctx The track sizing context with all necessary parameters
 * @param get_item_contribution Function to get intrinsic contribution of an item in a track
 */
inline void run_track_sizing_algorithm(
    TrackSizingContext& ctx
) {
    if (!ctx.axis_tracks || ctx.axis_tracks->empty()) return;

    // 11.4 Initialize Track Sizes
    initialize_track_sizes(*ctx.axis_tracks, ctx.axis_inner_size);

    // 11.5 Resolve Intrinsic Track Sizes
    // Note: This requires item contribution calculations which need the item list
    // For now we skip this step - it will be implemented when we integrate with the grid items

    // 11.6 Maximize Tracks
    maximize_tracks(*ctx.axis_tracks, ctx.axis_inner_size, ctx.axis_available_space);

    // 11.7 Expand Flexible Tracks
    expand_flexible_tracks(
        *ctx.axis_tracks,
        ctx.axis_min_size,
        ctx.axis_max_size,
        ctx.axis_available_space
    );

    // 11.8 Stretch auto Tracks
    // Per CSS Grid spec, auto tracks always stretch to fill remaining positive free space
    // when justify/align-content is 'normal' (the default) or 'stretch'.
    // axis_alignment == 0 is the default (normal), which acts like stretch for auto tracks.
    stretch_auto_tracks(*ctx.axis_tracks, ctx.axis_min_size, ctx.axis_available_space);
}

/**
 * Compute track offsets from their sizes.
 * Call this after track sizing to determine the position of each track.
 */
inline void compute_track_offsets(
    TrackArray& tracks,
    float gap
) {
    float offset = 0.0f;
    for (size_t i = 0; i < tracks.size(); ++i) {
        tracks[i].offset = offset;
        offset += tracks[i].base_size;

        // Add gap after each track except the last
        if (i < tracks.size() - 1 && tracks[i].kind == GridTrackKind::Track) {
            offset += gap;
        }
    }
}

// ============================================================================
// Alignment Gutter Adjustment (Taffy-inspired)
// ============================================================================

/**
 * CSS alignment values that distribute space as gutters
 */
constexpr int ALIGNMENT_SPACE_BETWEEN = 18;  // CSS_VALUE_SPACE_BETWEEN
constexpr int ALIGNMENT_SPACE_AROUND = 19;   // CSS_VALUE_SPACE_AROUND
constexpr int ALIGNMENT_SPACE_EVENLY = 64;   // CSS_VALUE_SPACE_EVENLY

/**
 * Check if alignment mode distributes space between tracks.
 */
inline bool is_space_distribution_alignment(int alignment) {
    return alignment == ALIGNMENT_SPACE_BETWEEN ||
           alignment == ALIGNMENT_SPACE_AROUND ||
           alignment == ALIGNMENT_SPACE_EVENLY;
}

/**
 * Compute the gutter adjustment for intrinsic track sizing.
 *
 * When justify-content or align-content uses space-between, space-around, or
 * space-evenly, extra space is distributed as "gutters" between tracks. During
 * intrinsic sizing, we estimate this gutter size to improve accuracy.
 *
 * @param alignment The justify-content or align-content value
 * @param axis_inner_size The definite inner size of the container (or -1 if indefinite)
 * @param tracks The tracks being sized
 * @param gap The explicit gap between tracks
 * @return The estimated gutter adjustment (per gutter)
 */
inline float compute_alignment_gutter_adjustment(
    int alignment,
    float axis_inner_size,
    const TrackArray& tracks,
    float gap
) {
    // If inner size is indefinite, we can't compute gutters
    if (axis_inner_size < 0) return 0.0f;

    // Count the number of tracks (excluding gutter tracks)
    size_t track_count = 0;
    for (const auto& track : tracks) {
        if (track.kind == GridTrackKind::Track) {
            track_count++;
        }
    }

    if (track_count <= 1) return 0.0f;

    // Sum current track sizes
    float total_track_size = 0.0f;
    for (const auto& track : tracks) {
        if (track.kind == GridTrackKind::Track) {
            total_track_size += track.base_size;
        }
    }

    // Calculate free space
    float total_gap = gap * (track_count - 1);
    float free_space = axis_inner_size - total_track_size - total_gap;

    if (free_space <= 0) return 0.0f;

    // Calculate gutter based on alignment mode
    size_t num_gutters = track_count - 1;

    switch (alignment) {
        case ALIGNMENT_SPACE_BETWEEN:
            // All space goes between tracks
            return (num_gutters > 0) ? (free_space / num_gutters) : 0.0f;

        case ALIGNMENT_SPACE_AROUND:
            // Half-space at edges, full space between
            // Total units = track_count (half at each edge = 1 unit, between = 1 unit each)
            return free_space / track_count;

        case ALIGNMENT_SPACE_EVENLY:
            // Equal space everywhere (edges and between)
            // Total units = track_count + 1
            return free_space / (track_count + 1);

        default:
            return 0.0f;
    }
}

/**
 * Compute alignment offset for tracks based on justify-content/align-content.
 *
 * @param alignment The alignment value (CSS_VALUE_*)
 * @param free_space Available free space to distribute
 * @param track_count Number of tracks
 * @return Starting offset for the first track
 */
inline float compute_alignment_start_offset(
    int alignment,
    float free_space,
    size_t track_count
) {
    if (free_space <= 0 || track_count == 0) return 0.0f;

    // CSS alignment constants
    constexpr int CSS_VALUE_CENTER = 17;
    constexpr int CSS_VALUE_FLEX_END = 15;
    constexpr int CSS_VALUE_END = 60;

    switch (alignment) {
        case CSS_VALUE_CENTER:
            return free_space / 2.0f;

        case CSS_VALUE_FLEX_END:
        case CSS_VALUE_END:
            return free_space;

        case ALIGNMENT_SPACE_AROUND:
            // Half-space at start
            return free_space / (track_count * 2.0f);

        case ALIGNMENT_SPACE_EVENLY:
            // Equal space at start
            return free_space / (track_count + 1);

        case ALIGNMENT_SPACE_BETWEEN:
        default:
            // No offset at start
            return 0.0f;
    }
}

/**
 * Apply alignment offsets to track positions.
 * Call after compute_track_offsets() to add alignment-based spacing.
 *
 * @param tracks The tracks to adjust
 * @param alignment The justify-content or align-content value
 * @param axis_inner_size The container inner size
 * @param gap The explicit gap between tracks
 */
inline void apply_alignment_to_tracks(
    TrackArray& tracks,
    int alignment,
    float axis_inner_size,
    float gap
) {
    if (axis_inner_size < 0) return;  // Can't align without definite size

    // Calculate total track size
    float total_track_size = 0.0f;
    size_t track_count = 0;
    for (const auto& track : tracks) {
        if (track.kind == GridTrackKind::Track) {
            total_track_size += track.base_size;
            track_count++;
        }
    }

    if (track_count == 0) return;

    // Calculate free space (subtract gaps)
    float total_gap = (track_count > 1) ? gap * (track_count - 1) : 0.0f;
    float free_space = axis_inner_size - total_track_size - total_gap;

    if (free_space <= 0) return;

    // Compute starting offset
    float start_offset = compute_alignment_start_offset(alignment, free_space, track_count);

    // Compute additional gutter between tracks
    float gutter_adjustment = 0.0f;
    if (is_space_distribution_alignment(alignment)) {
        gutter_adjustment = compute_alignment_gutter_adjustment(
            alignment, axis_inner_size, tracks, gap);
    }

    // Apply offsets
    float accumulated_offset = start_offset;
    bool first_track = true;

    for (auto& track : tracks) {
        if (track.kind == GridTrackKind::Track) {
            track.offset += accumulated_offset;

            if (!first_track && is_space_distribution_alignment(alignment)) {
                accumulated_offset += gutter_adjustment;
            }
            first_track = false;
        }
    }
}

/**
 * Estimate total content size including alignment gutters.
 * Used during intrinsic sizing to estimate container size.
 */
inline float estimate_content_size_with_gutters(
    const TrackArray& tracks,
    float gap,
    int alignment,
    float axis_inner_size
) {
    float total = 0.0f;
    size_t track_count = 0;

    for (const auto& track : tracks) {
        if (track.kind == GridTrackKind::Track) {
            total += track.base_size;
            track_count++;
        }
    }

    if (track_count == 0) return 0.0f;

    // Add explicit gaps
    total += gap * (track_count - 1);

    // Add alignment gutters if applicable
    if (is_space_distribution_alignment(alignment) && axis_inner_size >= 0) {
        float gutter = compute_alignment_gutter_adjustment(alignment, axis_inner_size, tracks, gap);
        total += gutter * (track_count - 1);  // Gutters go between tracks
    }

    return total;
}

// ============================================================================
// Grid Item Baseline Alignment (See grid_baseline.hpp for full implementation)
// ============================================================================

/**
 * Baseline shim information for a grid item.
 * This is populated during baseline resolution and used to adjust item positions.
 */
struct GridItemBaselineShim {
    float row_baseline_shim;     // Vertical offset for row baseline alignment
    float col_baseline_shim;     // Horizontal offset for column baseline alignment (writing-mode)
};

/**
 * Check if grid item should participate in baseline alignment.
 *
 * An item participates in baseline alignment if:
 * - align-self is 'baseline' or 'first baseline' or 'last baseline'
 * - Item spans only one row (for row baseline) or column (for column baseline)
 *
 * @param align_self The computed align-self value
 * @param row_span Number of rows spanned
 * @return true if item participates in row baseline alignment
 */
inline bool item_participates_in_row_baseline(int align_self, int row_span) {
    constexpr int CSS_VALUE_BASELINE = 22;
    constexpr int CSS_VALUE_FIRST_BASELINE = 65;
    constexpr int CSS_VALUE_LAST_BASELINE = 66;

    if (row_span > 1) return false;  // Multi-row items don't participate

    return align_self == CSS_VALUE_BASELINE ||
           align_self == CSS_VALUE_FIRST_BASELINE ||
           align_self == CSS_VALUE_LAST_BASELINE;
}

/**
 * Compute baseline adjustment needed for track sizing.
 *
 * When items in a row are baseline-aligned, the row track may need extra space
 * to accommodate the baseline shims. This function computes that extra space.
 *
 * @param item_height Height of the item
 * @param item_baseline Distance from item top to baseline
 * @param row_baseline Shared baseline for the row
 * @return Extra space needed above the item's normal position
 */
inline float compute_baseline_adjustment_for_track(
    float item_height,
    float item_baseline,
    float row_baseline
) {
    if (item_baseline < 0 || row_baseline < 0) return 0.0f;

    // The shim is the distance from row baseline to item baseline
    float shim = row_baseline - item_baseline;

    // If shim is positive, item needs to move down, which may increase row height
    // Extra height = shim at top + whatever remains at bottom
    return shim > 0 ? shim : 0.0f;
}

} // namespace grid
} // namespace radiant

#endif // RADIANT_GRID_SIZING_ALGORITHM_HPP
