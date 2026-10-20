#pragma once
// internal implementation header - only radiant/ and dedicated white-box tests may include it

/**
 * Grid Enhanced Integration
 *
 * Connects parsed GridTrackSize syntax and the existing grid layout pipeline to
 * the enhanced track, placement, occupancy, and sizing infrastructure.
 *
 * This adapter provides:
 * GridContainerLayout stores EnhancedGridTrack arrays directly. GridTrackSize is
 * converted once at pass initialization and never retained as computed state.
 *
 * Uses fixed-capacity arrays and C+ helper functions for layout scratch work.
 */

// IMPORTANT: Include grid.hpp first, then undef macros, then include enhanced headers
// This is necessary because grid.hpp includes view.hpp which defines min/max macros
#include "layout.hpp"

// Undefine min/max macros before including enhanced grid headers
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "grid_track.hpp"
#include "grid_occupancy.hpp"
#include "grid_placement.hpp"
#include "grid_sizing_algorithm.hpp"

namespace radiant {
namespace grid_adapter {

// Import types from grid namespace for convenience
using namespace grid;

// ============================================================================
// Parsed syntax → canonical track sizing functions
// ============================================================================

// Parsed tracks enter sizing only as a min/max pair, so convert that pair once.
inline TrackSizingFunction convert_to_track_sizing(GridTrackSize* old_size) {
    if (!old_size) return TrackSizingFunction::Auto();

    switch (old_size->type) {
        case GRID_TRACK_SIZE_LENGTH:
            return TrackSizingFunction::Length(old_size->value);
        case GRID_TRACK_SIZE_PERCENTAGE:
            return TrackSizingFunction::Percent(old_size->value, old_size->expression);
        case GRID_TRACK_SIZE_MIN_CONTENT:
            return TrackSizingFunction::MinContent();
        case GRID_TRACK_SIZE_MAX_CONTENT:
            return TrackSizingFunction::MaxContent();
        case GRID_TRACK_SIZE_FR:
            return TrackSizingFunction::Fr(old_size->value);
        case GRID_TRACK_SIZE_FIT_CONTENT:
            // CSS Grid §7.2.3.2 uses an auto minimum for fit-content().
            return old_size->is_percentage
                ? TrackSizingFunction::FitContentPercent(
                    old_size->fit_content_limit, old_size->expression)
                : TrackSizingFunction::FitContent(
                    old_size->fit_content_limit);
        case GRID_TRACK_SIZE_MINMAX: {
            TrackSizingFunction minimum = convert_to_track_sizing(old_size->min_size);
            TrackSizingFunction maximum = convert_to_track_sizing(old_size->max_size);
            return TrackSizingFunction(minimum.min, maximum.max);
        }
        case GRID_TRACK_SIZE_AUTO:
        default:
            return TrackSizingFunction::Auto();
    }
}

inline size_t defer_grid_percentage_tracks(TrackArray& tracks) {
    size_t count = 0;
    for (auto& track : tracks) {
        if (!track.uses_percentage()) continue;
        count++;
        // only the cyclic bound becomes intrinsic; a minmax() partner keeps its own sizing function.
        if (track.min_track_sizing_function.uses_percentage())
            track.min_track_sizing_function = MinTrackSizingFunction::Auto();
        if (track.max_track_sizing_function.uses_percentage())
            track.max_track_sizing_function = track.max_track_sizing_function.type == SizingFunctionType::FitContentPercent
                ? MaxTrackSizingFunction::MaxContent() : MaxTrackSizingFunction::Auto();
    }
    return count;
}

inline void restore_grid_percentage_tracks(TrackArray& tracks, const TrackArray& definitions,
                                           float container_size) {
    tracks = definitions;
    for (auto& track : tracks) {
        if (track.min_track_sizing_function.uses_percentage())
            track.min_track_sizing_function = MinTrackSizingFunction::Length(
                track.min_track_sizing_function.resolve(container_size));
        if (track.max_track_sizing_function.type == SizingFunctionType::FitContentPercent)
            track.max_track_sizing_function = MaxTrackSizingFunction::FitContentPx(
                track.max_track_sizing_function.fit_content_limit(container_size));
        else if (track.max_track_sizing_function.uses_percentage())
            track.max_track_sizing_function = MaxTrackSizingFunction::Length(
                track.max_track_sizing_function.resolve(container_size));
    }
}

/**
 * Finalize canonical used sizes with the largest-remainder method:
 * floor all track sizes first, then give +1px to the tracks with the largest
 * fractional parts until the integer total matches round(float total).
 * This prevents accumulated truncation errors (e.g. 4×17.5 → 158 instead of 160).
 */
inline void finalize_track_used_sizes(TrackArray& tracks) {
    if (tracks.empty()) return;
    int track_count = static_cast<int>(tracks.size()); // INT_CAST_OK: bounded track count
    float float_total = 0.0f;
    int floor_total = 0;
    struct FracEntry { float frac; int idx; };
    auto fracs = lam::temp_array<FracEntry>(tracks.size(), MEM_CAT_LAYOUT);
    if (!fracs) { tracks.fail("fracs"); return; }
    size_t frac_count = 0;
    fracs[0] = {};
    for (int i = 0; i < track_count; i++) {
        float v = tracks[i].base_size;
        int fv = static_cast<int>(v); // INT_CAST_OK: largest-remainder floor
        tracks[i].base_size = static_cast<float>(fv);
        float frac = v - static_cast<float>(fv);
        if (frac > 0.0f) fracs[frac_count++] = {frac, i};
        float_total += v;
        floor_total += fv;
    }

    // Pass 2: distribute the fractional remainder to tracks with highest frac parts
    int remainder = static_cast<int>(roundf(float_total)) - floor_total; // INT_CAST_OK: rounded pixel remainder
    if (remainder > 0 && track_count > 0) {
        // Build (fractional_part, index) pairs and sort descending
        // Use a simple selection approach to keep code minimal
        for (size_t a = 0; a < frac_count; a++) {
            size_t best = a;
            for (size_t b = a + 1; b < frac_count; b++) {
                if (fracs[b].frac > fracs[best].frac) {
                    best = b;
                }
            }
            if (best != a) {
                FracEntry tmp = fracs[a];
                fracs[a] = fracs[best];
                fracs[best] = tmp;
            }
        }
        int to_add = grid_min_value(remainder, static_cast<int>(frac_count)); // INT_CAST_OK: bounded remainder count
        for (int j = 0; j < to_add; j++) {
            int idx = fracs[j].idx;
            tracks[idx].base_size += 1.0f;
        }
    }
}

// ============================================================================
// Grid Item Info Extraction
// ============================================================================

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
    // CSS Grid spec: values <= 0 represent positions before the explicit grid start,
    // creating negative implicit tracks. Do NOT clamp to 1.
    return resolved;
}

// resolve numeric/named edges and spans locally; authored references survive every pass.
inline GridPlacement grid_axis_placement(GridContainerLayout* grid, bool is_row, int tracks,
                                                int start, int end,
                                                const char* start_name, const char* end_name,
                                                int start_occurrence, int end_occurrence,
                                                bool start_span, bool end_span,
                                                bool has_start, bool has_end) {
    int64_t resolved_start = start;
    int64_t resolved_end = end;
    bool definite_start = has_start && !start_span;
    bool definite_end = has_end && !end_span;
    if (definite_start) resolved_start = start_name
        ? find_grid_line_by_name(grid, start_name, is_row, start_occurrence)
        : start < 0 ? (int64_t)tracks + 2 + start : start;
    if (definite_end) resolved_end = end_name
        ? find_grid_line_by_name(grid, end_name, is_row, end_occurrence, true)
        : end < 0 ? (int64_t)tracks + 2 + end : end;
    if (start_span && definite_end) {
        resolved_start = start_name ? find_grid_line_by_name(grid, start_name, is_row,
            start_occurrence, false, static_cast<int>(resolved_end), -1) // INT_CAST_OK: definite authored edge or bounded named line fits int.
            : resolved_end + start;
        definite_start = true;
    }
    if (end_span && definite_start) {
        resolved_end = end_name ? find_grid_line_by_name(grid, end_name, is_row,
            end_occurrence, true, static_cast<int>(resolved_start), 1) // INT_CAST_OK: definite authored edge or bounded named line fits int.
            : resolved_start - end;
        definite_end = true;
    }
    if (!definite_start && !definite_end) {
        // an unanchored named span becomes one; named occurrences need a definite opposite edge.
        int64_t span = start_span ? (start_name ? 1 : -(int64_t)start)
            : end_span && !end_name ? -(int64_t)end : 1;
        if (span < 1) span = 1;
        if (span > GRID_MAX_EXPLICIT_TRACKS) span = GRID_MAX_EXPLICIT_TRACKS;
        return GridPlacement::Auto(static_cast<uint16_t>(span));
    }
    if (!definite_start) resolved_start = resolved_end - 1;
    if (!definite_end) resolved_end = resolved_start + 1;
    LineSpan bounded = grid_clamp_line_span(resolved_start - 1, resolved_end - 1);
    GridPlacement placement = GridPlacement::FromStartSpan(
        bounded.start.value + 1, bounded.span());
    // line zero is definite before the explicit grid; negative lines are already resolved.
    placement.is_definite = true;
    placement.has_negative_start = false;
    return placement;
}

/**
 * Extract GridItemInfo from ViewBlock with GridItemProp
 * @param item The ViewBlock to extract info from
 * @param item_index Index of this item in the items array
 * @param explicit_col_count Number of explicit columns (for resolving negative lines)
 * @param explicit_row_count Number of explicit rows (for resolving negative lines)
 */
inline GridItemInfo extract_grid_item_info(ViewBlock* item, int item_index,
                                           int explicit_col_count = 0, int explicit_row_count = 0,
                                           GridContainerLayout* grid = nullptr) {
    GridItemInfo info;
    info.item_index = item_index;

    GridItemProp* gi = grid_item_prop(item);
    if (!item || !gi) {
        // No grid item properties - fully auto-placed
        info.column = GridPlacement::Auto(1);
        info.row = GridPlacement::Auto(1);
        return info;
    }

    info.column = grid_axis_placement(grid, false, explicit_col_count,
        gi->grid_column_start, gi->grid_column_end,
        gi->grid_column_start_name, gi->grid_column_end_name,
        gi->grid_column_start_occurrence, gi->grid_column_end_occurrence,
        gi->grid_column_start_is_span, gi->grid_column_end_is_span,
        gi->has_explicit_grid_column_start || gi->grid_column_start != 0,
        gi->has_explicit_grid_column_end || gi->grid_column_end != 0);
    info.row = grid_axis_placement(grid, true, explicit_row_count,
        gi->grid_row_start, gi->grid_row_end,
        gi->grid_row_start_name, gi->grid_row_end_name,
        gi->grid_row_start_occurrence, gi->grid_row_end_occurrence,
        gi->grid_row_start_is_span, gi->grid_row_end_is_span,
        gi->has_explicit_grid_row_start || gi->grid_row_start != 0,
        gi->has_explicit_grid_row_end || gi->grid_row_end != 0);

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
    GridItemProp* gi = grid_item_prop(item);
    if (!item || !gi) return;

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

// Resolve one placement axis. Rows and columns obey the same CSS Grid §8.3
// negative-line rules, so a single implementation keeps their swaps and spans
// consistent.
inline void resolve_negative_lines_in_placement(GridPlacement* placement,
                                                int total_track_count) {
    if (!placement) return;
    if (placement->has_negative_start && placement->has_negative_end) {
        int resolved_start = resolve_negative_line(placement->start, total_track_count);
        int resolved_end = resolve_negative_line(placement->end, total_track_count);
        if (resolved_start > resolved_end) {
            int temp = resolved_start;
            resolved_start = resolved_end;
            resolved_end = temp;
        }
        int span = resolved_end - resolved_start;
        if (span < 1) span = 1;
        placement->start = static_cast<int16_t>(resolved_start);
        placement->end = static_cast<int16_t>(resolved_end);
        placement->span = static_cast<uint16_t>(span);
        placement->has_negative_start = false;
        placement->has_negative_end = false;
        placement->is_definite = true;
    } else if (placement->has_negative_end && placement->start > 0) {
        int resolved_end = resolve_negative_line(placement->end, total_track_count);
        if (placement->start > resolved_end) {
            int original_start = placement->start;
            placement->start = static_cast<int16_t>(resolved_end);
            placement->end = static_cast<int16_t>(original_start);
            int span = original_start - resolved_end;
            if (span < 1) span = 1;
            placement->span = static_cast<uint16_t>(span);
        } else {
            int span = resolved_end - placement->start;
            if (span < 1) span = 1;
            placement->span = static_cast<uint16_t>(span);
            placement->end = static_cast<int16_t>(resolved_end);
        }
        placement->has_negative_end = false;
    }
}

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
        resolve_negative_lines_in_placement(&item.column, total_col_count);
        resolve_negative_lines_in_placement(&item.row, total_row_count);
    }
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
        GridItemProp* gi = grid_item_prop(item);
        if (!item || !gi || !gi->grid_area) continue;

        const char* area_name = gi->grid_area;
        log_debug("Item %d has grid_area='%s'", i, area_name);

        for (int j = 0; j < grid_layout->area_count; j++) {
            if (grid_layout->grid_areas[j].name &&
                strcmp(grid_layout->grid_areas[j].name, area_name) == 0) {
                // Found the area - set line positions
                gi->grid_row_start = grid_layout->grid_areas[j].row_start;
                gi->grid_row_end = grid_layout->grid_areas[j].row_end;
                gi->grid_column_start = grid_layout->grid_areas[j].column_start;
                gi->grid_column_end = grid_layout->grid_areas[j].column_end;
                gi->has_explicit_grid_row_start = true;
                gi->has_explicit_grid_row_end = true;
                gi->has_explicit_grid_column_start = true;
                gi->has_explicit_grid_column_end = true;
                log_debug("  Resolved to rows %d-%d, cols %d-%d",
                          gi->grid_row_start, gi->grid_row_end,
                          gi->grid_column_start, gi->grid_column_end);
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
    if (!item_infos.reserve((size_t)item_count)) { grid_layout->storage_failed = true; return; }
    int explicit_col_count = grid_layout->explicit_column_count;
    int explicit_row_count = grid_layout->explicit_row_count;
    for (int i = 0; i < item_count; i++) {
        if (!item_infos.push_back(extract_grid_item_info(items[i], i, explicit_col_count,
                                                       explicit_row_count, grid_layout))) {
            grid_layout->storage_failed = true;
            return;
        }
        GridItemInfo& info = item_infos.back();
        log_debug("[GRID EXTRACT] item %d: col start=%d end=%d span=%d definite=%d neg_start=%d neg_end=%d | row start=%d end=%d span=%d definite=%d neg_start=%d neg_end=%d",
                 i, info.column.start, info.column.end, info.column.span, info.column.is_definite,
                 info.column.has_negative_start, info.column.has_negative_end,
                 info.row.start, info.row.end, info.row.span, info.row.is_definite,
                 info.row.has_negative_start, info.row.has_negative_end);
    }

    // Step 1: Resolve negative line numbers against the EXPLICIT grid only
    // CSS Grid spec §8.3: "Numeric indices count from the edges of the EXPLICIT grid."
    // with no explicit tracks, -1 is line 1 and earlier indices create negative implicit tracks.
    int resolve_cols = explicit_col_count;  // Use explicit count, can be 0
    int resolve_rows = explicit_row_count;  // Use explicit count, can be 0
    resolve_negative_lines_in_items(item_infos, resolve_cols, resolve_rows);

    uint16_t effective_col_count = static_cast<uint16_t>(grid_layout->explicit_column_count);
    uint16_t effective_row_count = static_cast<uint16_t>(grid_layout->explicit_row_count);
    // automatic spans establish implicit tracks in step 3; never invent an explicit track for `none`.
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

    if (!matrix.good()) { grid_layout->storage_failed = true; return; }

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
inline float grid_item_minimum_contribution(
    ViewBlock* item,
    bool horizontal,
    float min_content,
    float max_content,
    bool is_scroll_container)
{
    CssEnum min_keyword = layout_intrinsic_min_size_keyword(item, horizontal);
    if (min_keyword == CSS_VALUE_MAX_CONTENT) return max_content;
    if (min_keyword == CSS_VALUE_MIN_CONTENT || min_keyword == CSS_VALUE_FIT_CONTENT) {
        return min_content;
    }

    float explicit_min = layout_explicit_min_axis_or(item, horizontal, -1.0f);
    if (explicit_min >= 0.0f) {
        // An explicit zero replaces the automatic grid-item minimum; otherwise
        // `min-width:0` incorrectly grows an auto track to min-content.
        return layout_css_size_to_border_box(
            item ? item->bound : nullptr, layout_box_sizing(item), explicit_min, horizontal);
    }
    return is_scroll_container ? 0.0f : min_content;
}

inline ContribArray collect_item_contributions(
    GridContainerLayout* grid_layout,
    ViewBlock** items,
    int item_count,
    bool is_column_axis)
{
    ContribArray contributions;
    if (!grid_layout || !items || item_count <= 0) return contributions;

    if (!contributions.reserve((size_t)item_count)) {
        grid_layout->storage_failed = true;
        return contributions;
    }

    for (int i = 0; i < item_count; i++) {
        ViewBlock* item = items[i];
        GridItemProp* gi = grid_item_prop(item);
        if (!item || !gi) continue;

        GridItemContribution contrib = {};
        contrib.item = item;
        float margin_contribution = 0.0f;

        // Get item's placement (1-based line numbers from GridItemProp)
        if (is_column_axis) {
            int col_start = gi->computed_grid_column_start;
            int col_end = gi->computed_grid_column_end;
            if (col_start < 1 || col_end < 1) continue;

            contrib.track_start = col_start - 1;  // Convert to 0-based
            contrib.track_span = col_end - col_start;

            // Get intrinsic sizes from pre-computed measurements or calculate.
            // NOTE: has_measured_size=true means the measurement pass ran successfully.
            // 0 is a valid measurement (empty elements); do NOT skip it by checking > 0,
            // as that would cause the fallback to return 1px for empty flex items.
            if (gi->has_measured_size) {
                contrib.min_content_contribution = gi->measured_min_width;
                contrib.max_content_contribution = gi->measured_max_width;
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
            if (item->blk && item->block()->given_min_width > 0) {
                contrib.min_content_contribution = grid_max_value(contrib.min_content_contribution,
                                                            item->block()->given_min_width);
                contrib.max_content_contribution = grid_max_value(contrib.max_content_contribution,
                                                            contrib.min_content_contribution);
            }

            // CSS Grid §12.1: item contributions include margins (unless auto or percentage).
            // The intrinsic-sizing measurement returns the border-box; add resolved
            // horizontal margins to get the full margin-box contribution.
            // Percentage margins are excluded: in an indefinite intrinsic sizing context,
            // percentage margins resolve to 0 per CSS Sizing §3.
            if (item->bound) {
                bool left_is_auto    = (item->boundary()->margin.left_type  == CSS_VALUE_AUTO);
                bool right_is_auto   = (item->boundary()->margin.right_type == CSS_VALUE_AUTO);
                bool left_is_pct     = (item->boundary()->margin.left_type  == CSS_VALUE__PERCENTAGE);
                bool right_is_pct    = (item->boundary()->margin.right_type == CSS_VALUE__PERCENTAGE);
                float ml = (left_is_auto  || left_is_pct)  ? 0.0f : item->boundary()->margin.left;
                float mr = (right_is_auto || right_is_pct) ? 0.0f : item->boundary()->margin.right;
                margin_contribution = ml + mr;
                contrib.min_content_contribution += margin_contribution;
                contrib.max_content_contribution += margin_contribution;
            }

            // CSS Grid §6.6 / CSS Sizing §4.5: Grid items with non-visible overflow in the
            // inline axis may have a zero automatic minimum, while an author-provided
            // min-width remains part of the item's minimum contribution.
            if (item->scroller && item->scroll()->overflow_x != CSS_VALUE_VISIBLE) {
                contrib.is_scroll_container = true;
            }
        } else {
            // Row axis
            int row_start = gi->computed_grid_row_start;
            int row_end = gi->computed_grid_row_end;
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
                bool top_is_auto = (item->boundary()->margin.top_type    == CSS_VALUE_AUTO);
                bool bot_is_auto = (item->boundary()->margin.bottom_type == CSS_VALUE_AUTO);
                bool top_is_pct  = (item->boundary()->margin.top_type    == CSS_VALUE__PERCENTAGE);
                bool bot_is_pct  = (item->boundary()->margin.bottom_type == CSS_VALUE__PERCENTAGE);
                bool inline_is_definite = (grid_layout->content_width > 0);
                float mt = top_is_auto ? 0.0f :
                           (top_is_pct && !inline_is_definite) ? 0.0f :
                           item->boundary()->margin.top;
                float mb = bot_is_auto ? 0.0f :
                           (bot_is_pct && !inline_is_definite) ? 0.0f :
                           item->boundary()->margin.bottom;
                margin_contribution = mt + mb;
                contrib.min_content_contribution += margin_contribution;
                contrib.max_content_contribution += margin_contribution;
            }

            // CSS Grid §6.6 / CSS Sizing §4.5: Grid items with non-visible overflow in the
            // block axis may have a zero automatic minimum, while an author-provided
            // min-height remains part of the item's minimum contribution.
            if (item->scroller && item->scroll()->overflow_y != CSS_VALUE_VISIBLE) {
                contrib.is_scroll_container = true;
            }
        }

        contrib.minimum_contribution = grid_item_minimum_contribution(
            item, is_column_axis,
            contrib.min_content_contribution - margin_contribution,
            contrib.max_content_contribution - margin_contribution,
            contrib.is_scroll_container) + margin_contribution;

        // a zero contribution still resolves an empty intrinsic track's finite growth limit.
        if (contrib.track_span > 0) {
            if (!contributions.push_back(contrib)) {
            grid_layout->storage_failed = true;
            return contributions;
        }
        }
    }

    return contributions;
}

/**
 * Run the enhanced track sizing algorithm.
 *
 * INVARIANT: This is the single live CSS Grid §11 track-sizing entry point (§11.4–§11.8,
 * including §11.5 Resolve Intrinsic Track Sizes). Do not add a parallel track-sizing driver
 * that skips §11.5 — an earlier dead `run_track_sizing_algorithm` did exactly that and was removed.
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
    float* out_row_intrinsic_height = nullptr,
    bool column_min_content_constraint = false,
    bool column_max_content_constraint = false,
    bool columns_only = false)
{
    if (!grid_layout) return;

    log_debug("run_enhanced_track_sizing: container=%.1fx%.1f, cols=%d, rows=%d",
              container_width, container_height,
              grid_layout->computed_column_count, grid_layout->computed_row_count);

    if (!grid_layout->computed_columns || !grid_layout->computed_rows) {
        grid_layout->storage_failed = true;
        return;
    }
    TrackArray& col_tracks = *grid_layout->computed_columns;
    TrackArray& row_tracks = *grid_layout->computed_rows;
    auto collapse_auto_fit = [&](TrackArray& tracks, const bool* flags, int limit,
                                 int explicit_offset, bool horizontal) { // CSS Grid §7.2.3.2
        for (size_t i = 0; i < tracks.size(); i++) {
            // collapse flags index the explicit template; placement can prepend implicit tracks.
            if (i < (size_t)explicit_offset || i - explicit_offset >= (size_t)limit ||
                !flags[i - explicit_offset]) continue;
            bool occupied = false;
            int line = static_cast<int>(i) + 1; // INT_CAST_OK: the grid extent policy bounds track indices to 20000.
            for (int j = 0; j < item_count && !occupied; j++) {
                GridItemProp* item = grid_item_prop(items[j]);
                occupied = item && (horizontal ? item->computed_grid_column_start : item->computed_grid_row_start) <= line &&
                    (horizontal ? item->computed_grid_column_end : item->computed_grid_row_end) > line;
            }
            if (!occupied) tracks[i].collapse();
        }
    };
    collapse_auto_fit(col_tracks, grid_layout->auto_fit_columns, grid_layout->auto_fit_col_count,
                      grid_layout->negative_implicit_column_count, true);
    collapse_auto_fit(row_tracks, grid_layout->auto_fit_rows, grid_layout->auto_fit_row_count,
                      grid_layout->negative_implicit_row_count, false);
    // Percentage sizing temporarily rewrites functions; preserve only the pass input definitions.
    TrackArray col_track_definitions = col_tracks;
    TrackArray row_track_definitions = row_tracks;
    if (!col_track_definitions.good() || !row_track_definitions.good()) {
        grid_layout->storage_failed = true;
        return;
    }

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
    float col_gap_total = grid_track_gaps(col_tracks, 0, col_tracks.size(), effective_col_gap);
    float row_gap_total = grid_track_gaps(row_tracks, 0, row_tracks.size(), grid_layout->row_gap);

    // a zero-sized box and a box exhausted by gaps still have definite percentage bases.
    float col_available = container_width >= 0.0f ? fmaxf(0.0f, container_width - col_gap_total) : -1.0f;
    float row_available = container_height >= 0.0f ? fmaxf(0.0f, container_height - row_gap_total) : -1.0f;
    // min-height can provide stretch space without making an auto-height percentage base definite.
    float row_percentage_base = grid_layout->has_explicit_height ? container_height : -1.0f;

    log_debug("  col_available=%.1f (container_width=%.1f - col_gap_total=%.1f)",
              col_available, container_width, col_gap_total);

    // === Run track sizing for columns ===
    if (!col_tracks.empty()) {
        // cyclic percentage bounds are intrinsic until the container's first-pass size exists.
        size_t pct_col_count = col_available < 0.0f ? defer_grid_percentage_tracks(col_tracks) : 0;

        // 11.4 Initialize Track Sizes
        initialize_track_sizes(col_tracks, container_width);

        // 11.5 Resolve Intrinsic Track Sizes
        ContribArray col_contributions =
            collect_item_contributions(grid_layout, items, item_count, true /* is_column_axis */);
        if (grid_layout->storage_failed) return;

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
                    auto track_min_floor = lam::temp_array<float>(col_tracks.size(), MEM_CAT_LAYOUT);
                    if (!track_min_floor) { grid_layout->storage_failed = true; return; }
                    auto track_max_floor = lam::temp_array<float>(col_tracks.size(), MEM_CAT_LAYOUT);
                    if (!track_max_floor) { grid_layout->storage_failed = true; return; }
                    size_t n_col = col_tracks.size();
                    for (size_t i = 0; i < n_col; ++i) { track_min_floor[i] = 0.0f; track_max_floor[i] = 0.0f; }
                    for (const auto& contrib : col_contributions) {
                        if (contrib.track_span == 1) {
                            size_t ti = contrib.track_start;
                            if (ti < n_col) {
                                float effective_min = contrib.is_scroll_container ? 0.0f
                                                                                  : contrib.min_content_contribution;
                                track_min_floor[ti] = grid_max_value(track_min_floor[ti], effective_min);
                                track_max_floor[ti] = grid_max_value(track_max_floor[ti],
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
            log_debug("[GRID COL] contrib[%zu]: track=%zu span=%zu min=%.1f max=%.1f minimum=%.1f scroll=%d",
                         ci, col_contributions[ci].track_start, (size_t)col_contributions[ci].track_span,
                         col_contributions[ci].min_content_contribution,
                         col_contributions[ci].max_content_contribution,
                         col_contributions[ci].minimum_contribution,
                         col_contributions[ci].is_scroll_container);
        }
        resolve_intrinsic_track_sizes(col_tracks, col_contributions, effective_col_gap, container_width,
                                      column_min_content_constraint, column_max_content_constraint);

        for (size_t _di = 0; _di < col_tracks.size(); _di++)
            log_debug("[GRID COL] post-resolve col[%zu]: base=%.2f gl=%.2f",
                     _di, col_tracks[_di].base_size, col_tracks[_di].growth_limit);

        // 11.6 Maximize Tracks
        maximize_tracks(col_tracks, col_available, container_width,
                        column_min_content_constraint);

        for (size_t _di = 0; _di < col_tracks.size(); _di++)
            log_debug("  DBG post-maximize col[%zu]: base=%.2f gl=%.2f", _di, col_tracks[_di].base_size, col_tracks[_di].growth_limit);

        // 11.7 Expand Flexible Tracks
        float fr_intrinsic_total = 0.0f;
        expand_flexible_tracks(col_tracks, 0.0f, col_available, container_width,
                               col_contributions, effective_col_gap,
                               &fr_intrinsic_total, column_min_content_constraint);
        // For indefinite containers: the intrinsic total from Pass 1 determines the
        // container width, even though Pass 2 may redistribute tracks differently.
        if (fr_intrinsic_total > 0.0f && out_col_intrinsic_width) {
            *out_col_intrinsic_width = fr_intrinsic_total;
        }

        for (size_t _di = 0; _di < col_tracks.size(); _di++)
            log_debug("  DBG post-expand col[%zu]: base=%.2f gl=%.2f", _di, col_tracks[_di].base_size, col_tracks[_di].growth_limit);

        // 11.8 Stretch auto Tracks
        stretch_auto_tracks(col_tracks, 0.0f, col_available, grid_layout->justify_content);

        for (size_t _di = 0; _di < col_tracks.size(); _di++)
            log_debug("  DBG post-stretch col[%zu]: base=%.2f gl=%.2f", _di, col_tracks[_di].base_size, col_tracks[_di].growth_limit);

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
            float col_gap_total2 = grid_track_gaps(col_tracks, 0, col_tracks.size(), resolved_col_gap);
            float col_available2 = first_total - col_gap_total2;

            restore_grid_percentage_tracks(col_tracks, col_track_definitions, first_total);

            // Re-run all phases with pct tracks now definite and resolved gap
            initialize_track_sizes(col_tracks, first_total);
            resolve_intrinsic_track_sizes(col_tracks, col_contributions, resolved_col_gap, first_total,
                                          column_min_content_constraint, column_max_content_constraint);
            // For shrink-to-fit containers: use indefinite semantics for maximize
            // so that only finite-gl tracks grow to their gl (no free-space distribution).
            float max_avail = (container_width < 0) ? -1.0f : col_available2;
            maximize_tracks(col_tracks, max_avail, first_total,
                            column_min_content_constraint);
            expand_flexible_tracks(col_tracks, 0.0f, col_available2, first_total,
                                   col_contributions, resolved_col_gap, nullptr,
                                   column_min_content_constraint);
            // For shrink-to-fit: no stretching (container size = content, no free space)
            if (container_width >= 0)
                stretch_auto_tracks(col_tracks, 0.0f, col_available2, grid_layout->justify_content);
        }
        // Canonical consumers observe the same integer-pixel distribution as the legacy copy seam.
        finalize_track_used_sizes(col_tracks);
        compute_track_offsets(col_tracks, grid_layout->column_gap);
    }

    // pure inline contributions must not recursively measure unused row heights.
    // === Run track sizing for rows ===
    if (!columns_only && !row_tracks.empty()) {
        size_t pct_row_count = row_percentage_base < 0.0f ? defer_grid_percentage_tracks(row_tracks) : 0;
        bool needs_row_second_pass = pct_row_count > 0;

        // 11.4 Initialize Track Sizes
        initialize_track_sizes(row_tracks, row_percentage_base);

        // 11.5 Resolve Intrinsic Track Sizes
        // NOTE: This uses grid_layout->computed_columns which was just updated above
        ContribArray row_contributions =
            collect_item_contributions(grid_layout, items, item_count, false /* is_column_axis */);
        if (grid_layout->storage_failed) return;
        resolve_intrinsic_track_sizes(row_tracks, row_contributions, grid_layout->row_gap, row_percentage_base);

        // 11.6 also runs for an indefinite grid: finite growth limits define the
        // max-content track size needed before cyclic item percentages resolve.
        maximize_tracks(row_tracks, row_available, row_percentage_base);

        // Expansion and stretch distribute definite free space only.
        if (row_available > 0) {

            // 11.7 Expand Flexible Tracks
            expand_flexible_tracks(row_tracks, 0.0f, row_available, row_percentage_base,
                                   row_contributions, grid_layout->row_gap);

            // 11.8 Stretch auto Tracks
            stretch_auto_tracks(row_tracks, 0.0f, row_available, grid_layout->align_content);
        }

        // Second pass for percentage row tracks in indefinite containers
        if (needs_row_second_pass) {
            float first_row_total = row_gap_total;
            for (const auto& t : row_tracks) first_row_total += t.base_size;

            // Expose the intrinsic row height for the caller (container height should not exceed this)
            if (out_row_intrinsic_height) *out_row_intrinsic_height = first_row_total;

            restore_grid_percentage_tracks(row_tracks, row_track_definitions, first_row_total);

            float row_available2 = first_row_total - row_gap_total;
            initialize_track_sizes(row_tracks, row_available2);
            resolve_intrinsic_track_sizes(row_tracks, row_contributions, grid_layout->row_gap, first_row_total);
            maximize_tracks(row_tracks, row_available2, row_available2);
            expand_flexible_tracks(row_tracks, 0.0f, row_available2, row_available2,
                                   row_contributions, grid_layout->row_gap);
            stretch_auto_tracks(row_tracks, 0.0f, row_available2, grid_layout->align_content);
        }
        finalize_track_used_sizes(row_tracks);
        compute_track_offsets(row_tracks, grid_layout->row_gap);
    }
    grid_layout->storage_failed = !col_tracks.good() || !row_tracks.good();
}

} // namespace grid_adapter
} // namespace radiant
