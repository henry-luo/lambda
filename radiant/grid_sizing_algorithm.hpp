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
 * - std::vector<EnhancedGridTrack>* → Pool-allocated arrays with count tracking
 * - std::vector<size_t> eligible_indices → Fixed-size arrays with MAX_TRACKS limit
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
#include <vector>
#include <cmath>
#include <algorithm>
#include <functional>

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
    std::vector<EnhancedGridTrack>* axis_tracks;

    /** Tracks in the other axis (for content sizing estimates) */
    std::vector<EnhancedGridTrack>* other_axis_tracks;

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
    std::vector<EnhancedGridTrack>& tracks,
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
        float resolved_max = track.max_track_sizing_function.resolve(axis_inner_size);
        track.growth_limit = (resolved_max >= 0) ? resolved_max : std::numeric_limits<float>::infinity();

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
inline void flush_planned_base_size_increases(std::vector<EnhancedGridTrack>& tracks) {
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
    std::vector<EnhancedGridTrack>& tracks,
    bool set_infinitely_growable
) {
    for (auto& track : tracks) {
        if (track.growth_limit_planned_increase > 0.0f) {
            if (std::isinf(track.growth_limit)) {
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
    const std::vector<EnhancedGridTrack>& tracks,
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
    const std::vector<EnhancedGridTrack>& tracks,
    Predicate pred
) {
    size_t count = 0;
    for (const auto& track : tracks) {
        if (pred(track)) ++count;
    }
    return count;
}

/**
 * Distribute space to tracks, increasing their planned_increase values.
 * Used by the track sizing algorithm to distribute extra space.
 *
 * @param space The amount of space to distribute
 * @param tracks The tracks to distribute space to
 * @param filter_fn Predicate to select which tracks receive space
 * @param planned_increase_fn Function to get/set the planned increase for a track
 * @param growth_limit_fn Function to get the growth limit for a track
 * @param item_incurred_increase_fn Function to get/set the item incurred increase
 */
inline void distribute_space_to_tracks(
    float space,
    std::vector<EnhancedGridTrack>& tracks,
    std::function<bool(const EnhancedGridTrack&)> filter_fn,
    std::function<float&(EnhancedGridTrack&)> planned_increase_fn,
    std::function<float(const EnhancedGridTrack&)> limit_fn,
    bool distribute_beyond_limits = false
) {
    if (space <= 0) return;

    // Count eligible tracks
    std::vector<size_t> eligible_indices;
    float total_limit = 0.0f;

    for (size_t i = 0; i < tracks.size(); ++i) {
        if (filter_fn(tracks[i])) {
            eligible_indices.push_back(i);
            float limit = limit_fn(tracks[i]);
            if (!std::isinf(limit)) {
                total_limit += limit - tracks[i].base_size;
            }
        }
    }

    if (eligible_indices.empty()) return;

    float remaining_space = space;

    // Distribute space equally, respecting limits
    while (remaining_space > 0.01f && !eligible_indices.empty()) {
        float space_per_track = remaining_space / eligible_indices.size();
        bool made_progress = false;

        std::vector<size_t> still_eligible;
        for (size_t idx : eligible_indices) {
            auto& track = tracks[idx];
            float limit = limit_fn(track);
            float current = track.base_size + planned_increase_fn(track);
            float room = std::isinf(limit) ? space_per_track : std::max(0.0f, limit - current);

            float increase = std::min(space_per_track, room);
            if (increase > 0) {
                planned_increase_fn(track) += increase;
                remaining_space -= increase;
                made_progress = true;
            }

            // Track is still eligible if it has room or we're distributing beyond limits
            if (room > space_per_track || distribute_beyond_limits) {
                still_eligible.push_back(idx);
            }
        }

        eligible_indices = std::move(still_eligible);

        if (!made_progress) break;
    }

    // If distributing beyond limits and there's still space, distribute equally
    if (distribute_beyond_limits && remaining_space > 0.01f && !eligible_indices.empty()) {
        float extra_per_track = remaining_space / eligible_indices.size();
        for (size_t idx : eligible_indices) {
            planned_increase_fn(tracks[idx]) += extra_per_track;
        }
    }
}

// ============================================================================
// 11.5 Resolve Intrinsic Track Sizes
// ============================================================================

/**
 * Item contribution information for track sizing.
 * Contains the min/max content sizes and which tracks the item spans.
 */
struct GridItemContribution {
    float min_content_contribution;    // Item's min-content size in this axis
    float max_content_contribution;    // Item's max-content size in this axis
    size_t track_start;                // First track index spanned (0-based)
    size_t track_span;                 // Number of tracks spanned
    bool crosses_flexible_track;       // Whether item spans any flexible (fr) track
    ViewBlock* item;                   // Reference to the item for debugging
};

/**
 * Calculate the space already accounted for by spanned tracks.
 * Used when distributing item contributions across multiple tracks.
 */
inline float spanned_tracks_size(
    const std::vector<EnhancedGridTrack>& tracks,
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
 */
inline void increase_sizes_for_spanning_item(
    std::vector<EnhancedGridTrack>& tracks,
    size_t start_index,
    size_t span,
    float space_to_distribute,
    IntrinsicContributionType contribution_type
) {
    if (space_to_distribute <= 0 || span == 0) return;

    size_t end = std::min(start_index + span, tracks.size());

    // Determine which tracks can receive space during intrinsic sizing.
    // Per CSS Grid spec §11.5, flexible (fr) tracks with non-zero fr are NOT grown
    // during intrinsic sizing - they're sized in §11.7 (Expand Flexible Tracks).
    // However, 0fr tracks don't flex and should participate in intrinsic sizing.
    //
    // IMPORTANT: For max-content contributions (Phase 2), only tracks with max-content
    // or auto max sizing should receive space. min-content tracks are already fully
    // sized by Phase 1 and should NOT grow further.
    std::vector<size_t> eligible_indices;
    for (size_t i = start_index; i < end; ++i) {
        const auto& track = tracks[i];

        // Skip truly flexible tracks (non-zero fr) - they're handled in expand_flexible_tracks()
        // But include 0fr tracks since they don't flex and need intrinsic sizing.
        bool is_truly_flexible = track.is_flexible() && track.max_track_sizing_function.flex_factor() > 0;
        if (is_truly_flexible) {
            continue;
        }

        // For max-content contributions, check if the track's max sizing function
        // actually wants max-content sizing. min-content tracks should be skipped.
        if (contribution_type == IntrinsicContributionType::Maximum) {
            auto max_type = track.max_track_sizing_function.type;
            // Only include tracks where max sizing is max-content, auto, or fit-content
            bool wants_max_content = (max_type == SizingFunctionType::MaxContent ||
                                      max_type == SizingFunctionType::Auto ||
                                      max_type == SizingFunctionType::FitContentPx ||
                                      max_type == SizingFunctionType::FitContentPercent);
            if (!wants_max_content) {
                // This track (e.g., min-content) doesn't want max-content sizing
                continue;
            }
        }

        // Include tracks with intrinsic min or max sizing, 0fr tracks, or auto max sizing
        bool is_intrinsic = track.min_track_sizing_function.is_intrinsic() ||
                            track.max_track_sizing_function.is_intrinsic();
        bool is_zero_fr = track.is_flexible() && track.max_track_sizing_function.flex_factor() == 0;
        if (is_intrinsic || is_zero_fr || track.max_track_sizing_function.type == SizingFunctionType::Auto) {
            eligible_indices.push_back(i);
        }
    }

    if (eligible_indices.empty()) {
        // No intrinsic tracks to distribute to.
        // Per CSS Grid spec §11.5, if all spanned tracks are fixed-size or flexible,
        // the item's contribution is limited by those tracks - we don't grow them here.
        return;
    }

    // CSS Grid spec §11.5.1: Distribute space to tracks that need growth.
    // First, find tracks that have "room" - tracks whose base_size is less than
    // the equal share they would need if all tracks were sized equally from zero.
    //
    // Per the spec, we distribute to "intrinsic tracks" in the span, giving priority
    // to tracks that have the smallest base_size (tracks with most room to grow).

    float remaining = space_to_distribute;

    while (remaining > 0.01f && !eligible_indices.empty()) {
        // Find the track with smallest base_size among eligible tracks
        float min_base = std::numeric_limits<float>::max();
        float second_min_base = std::numeric_limits<float>::max();

        for (size_t idx : eligible_indices) {
            float bs = tracks[idx].base_size;
            if (bs < min_base) {
                second_min_base = min_base;
                min_base = bs;
            } else if (bs < second_min_base) {
                second_min_base = bs;
            }
        }

        // Count tracks at minimum base_size
        std::vector<size_t> min_tracks;
        for (size_t idx : eligible_indices) {
            if (std::abs(tracks[idx].base_size - min_base) < 0.01f) {
                min_tracks.push_back(idx);
            }
        }

        // Calculate how much to grow each min track:
        // Either grow all min tracks to the level of second_min, or distribute remaining evenly
        float grow_to = second_min_base;
        if (std::isinf(second_min_base) || second_min_base == min_base) {
            // All eligible tracks are at the same level; distribute remaining evenly
            float per_track = remaining / min_tracks.size();
            for (size_t idx : min_tracks) {
                tracks[idx].base_size += per_track;
            }
            remaining = 0;
        } else {
            // Grow min tracks toward second_min, limited by remaining space
            float gap = second_min_base - min_base;
            float total_needed = gap * min_tracks.size();

            if (total_needed <= remaining) {
                // Can fully level up to second_min
                for (size_t idx : min_tracks) {
                    tracks[idx].base_size = second_min_base;
                }
                remaining -= total_needed;
            } else {
                // Not enough space; distribute remaining evenly among min tracks
                float per_track = remaining / min_tracks.size();
                for (size_t idx : min_tracks) {
                    tracks[idx].base_size += per_track;
                }
                remaining = 0;
            }
        }
    }

    // For max content, also increase growth limit
    for (size_t idx : eligible_indices) {
        auto& track = tracks[idx];
        if (contribution_type == IntrinsicContributionType::Maximum) {
            if (std::isinf(track.growth_limit)) {
                track.growth_limit = track.base_size;
            } else {
                track.growth_limit = std::max(track.growth_limit, track.base_size);
            }
        }

        // Ensure growth limit >= base size
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
    const std::vector<EnhancedGridTrack>& tracks,
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
    std::vector<GridItemContribution>& contributions
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
    std::vector<EnhancedGridTrack>& tracks,
    std::vector<GridItemContribution>& contributions,
    float gap
) {
    if (contributions.empty() || tracks.empty()) return;

    // Mark which items cross flexible tracks
    for (auto& contrib : contributions) {
        contrib.crosses_flexible_track = item_crosses_flexible_track(
            tracks, contrib.track_start, contrib.track_span);
    }

    // Sort: non-flex first, then by span count (ascending)
    sort_contributions_for_intrinsic_sizing(contributions);

    // Process in two phases: first min-content, then max-content
    // Phase 1: Size tracks to min-content contributions
    // NOTE: Items crossing flexible tracks are skipped - their contribution
    // will be handled by the flexible track sizing in expand_flexible_tracks()
    for (const auto& contrib : contributions) {
        if (contrib.track_span == 0) continue;

        // Skip items that cross flexible tracks - they're handled in §11.7
        if (contrib.crosses_flexible_track) continue;

        float current_size = spanned_tracks_size(tracks, contrib.track_start, contrib.track_span, gap);
        float extra_space = contrib.min_content_contribution - current_size;

        if (extra_space > 0) {
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

    // Phase 2: Size tracks to max-content contributions (for tracks with max-content/auto sizing)
    // NOTE: Items crossing flexible tracks are skipped here too
    // IMPORTANT: min-content tracks should NOT receive max-content contributions
    for (const auto& contrib : contributions) {
        if (contrib.track_span == 0) continue;

        // Skip items that cross flexible tracks
        if (contrib.crosses_flexible_track) continue;

        // Check if ANY track in the span has max-content or auto max sizing
        // If all tracks are min-content, skip this contribution (min-content tracks are already sized)
        size_t end = std::min(contrib.track_start + contrib.track_span, tracks.size());
        bool has_max_content_track = false;
        for (size_t i = contrib.track_start; i < end; ++i) {
            // Check if track's max sizing function is max-content or auto (which becomes max-content)
            auto max_type = tracks[i].max_track_sizing_function.type;
            if (max_type == SizingFunctionType::MaxContent ||
                max_type == SizingFunctionType::Auto ||
                max_type == SizingFunctionType::FitContentPx ||
                max_type == SizingFunctionType::FitContentPercent) {
                has_max_content_track = true;
                break;
            }
        }

        if (!has_max_content_track) {
            // All tracks are min-content sized, skip max-content contribution
            continue;
        }

        float current_size = spanned_tracks_size(tracks, contrib.track_start, contrib.track_span, gap);
        float extra_space = contrib.max_content_contribution - current_size;

        if (extra_space > 0) {
            increase_sizes_for_spanning_item(
                tracks,
                contrib.track_start,
                contrib.track_span,
                extra_space,
                IntrinsicContributionType::Maximum
            );
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
    std::vector<EnhancedGridTrack>& tracks,
    float axis_inner_size,
    float axis_available_space
) {
    // If there's no definite size, nothing to distribute
    if (axis_inner_size < 0 && axis_available_space < 0) return;

    float available = (axis_inner_size >= 0) ? axis_inner_size : axis_available_space;

    // Calculate current used space
    float used_space = 0.0f;
    for (const auto& track : tracks) {
        used_space += track.base_size;
    }

    float free_space = available - used_space;
    if (free_space <= 0) return;

    // Count tracks with finite growth limits
    size_t finite_tracks = 0;
    float total_room = 0.0f;
    for (const auto& track : tracks) {
        if (!std::isinf(track.growth_limit)) {
            finite_tracks++;
            total_room += track.growth_limit - track.base_size;
        }
    }

    if (finite_tracks == 0) return;

    // Distribute space up to growth limits
    float space_to_distribute = std::min(free_space, total_room);
    if (space_to_distribute <= 0) return;

    while (space_to_distribute > 0.01f && finite_tracks > 0) {
        float share = space_to_distribute / finite_tracks;
        bool made_progress = false;

        for (auto& track : tracks) {
            if (std::isinf(track.growth_limit)) continue;

            float room = track.growth_limit - track.base_size;
            if (room <= 0) continue;

            float increase = std::min(share, room);
            track.base_size += increase;
            space_to_distribute -= increase;
            made_progress = true;

            if (room <= share) {
                // Track is now at its limit
            }
        }

        if (!made_progress) break;

        // Recount eligible tracks
        finite_tracks = 0;
        for (const auto& track : tracks) {
            if (!std::isinf(track.growth_limit) && track.base_size < track.growth_limit) {
                finite_tracks++;
            }
        }
    }
}

// --- 11.7 Expand Flexible Tracks ---

/**
 * Expand Flexible Tracks.
 * Sizes flexible tracks using the largest value it can assign to an fr without exceeding available space.
 */
inline void expand_flexible_tracks(
    std::vector<EnhancedGridTrack>& tracks,
    float axis_min_size,
    float axis_max_size,
    float axis_available_space
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
        // No definite available space - use min-content based sizing
        // Flexible tracks get their base size (which should be 0 for fr tracks)
        return;
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

    // Calculate hypothetical fr size
    float hypothetical_fr_size = free_space / flex_factor_sum;

    // Find the largest base size among flexible tracks (clamping fr value)
    // CSS Grid spec: the fr value is the maximum of:
    // - The result of dividing the leftover space by the sum of flex factors
    // - The largest min track sizing function of flexible tracks divided by its flex factor
    float fr_size = hypothetical_fr_size;
    for (const auto& track : tracks) {
        if (!track.is_flexible()) continue;

        float min_size = track.min_track_sizing_function.resolve(axis_available_space);
        if (min_size < 0) min_size = 0;  // Treat indefinite as 0

        float track_fr = track.flex_factor();
        if (track_fr > 0) {
            float min_fr = min_size / track_fr;
            fr_size = std::max(fr_size, min_fr);
        }
    }

    // Assign sizes to flexible tracks
    for (auto& track : tracks) {
        if (!track.is_flexible()) continue;

        float track_size = track.flex_factor() * fr_size;

        // Clamp to min size
        float min_size = track.min_track_sizing_function.resolve(axis_available_space);
        if (min_size >= 0) {
            track_size = std::max(track_size, min_size);
        }

        track.base_size = track_size;
        track.growth_limit = track_size;
    }
}

// --- 11.8 Stretch auto Tracks ---

/**
 * Stretch auto Tracks.
 * Expands tracks that have an auto max track sizing function by dividing any remaining
 * positive, definite free space equally amongst them.
 */
inline void stretch_auto_tracks(
    std::vector<EnhancedGridTrack>& tracks,
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
            if (!std::isinf(track.growth_limit)) {
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

    // 11.8 Stretch auto Tracks (only if align/justify-content is stretch)
    // CSS_ALIGN_STRETCH = 5 typically
    if (ctx.axis_alignment == 5) {  // STRETCH
        stretch_auto_tracks(*ctx.axis_tracks, ctx.axis_min_size, ctx.axis_available_space);
    }
}

/**
 * Compute track offsets from their sizes.
 * Call this after track sizing to determine the position of each track.
 */
inline void compute_track_offsets(
    std::vector<EnhancedGridTrack>& tracks,
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
    const std::vector<EnhancedGridTrack>& tracks,
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
    std::vector<EnhancedGridTrack>& tracks,
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
    const std::vector<EnhancedGridTrack>& tracks,
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
