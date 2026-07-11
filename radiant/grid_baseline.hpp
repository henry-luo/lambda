#pragma once
/**
 * grid_baseline.hpp - Grid item baseline alignment support
 *
 * Implements baseline alignment for CSS Grid items as specified in CSS Grid Level 1.
 * Baseline alignment groups items by row and aligns them to a common baseline.
 *
 * Based on Taffy's resolve_item_baselines() implementation.
 *
 * Uses fixed-capacity scratch arrays to keep grid layout allocation style
 * consistent with the rest of Radiant.
 */

#include <cmath>

#include "view.hpp"
#include "grid.hpp"
#include "layout_alignment.hpp"
#include "../lib/tagged.hpp"

namespace radiant {
namespace grid {

// Bring global ViewBlock and GridContainerLayout into namespace scope
using ::ViewBlock;
using ::GridContainerLayout;

constexpr int GRID_BASELINE_MAX_GROUPS = 256;
constexpr int GRID_BASELINE_MAX_ITEMS_PER_GROUP = 256;
constexpr int GRID_BASELINE_MAX_TRACKS = 256;

// ============================================================================
// Baseline Alignment Data
// ============================================================================

/**
 * Baseline information for a single grid item.
 */
struct ItemBaselineInfo {
    ViewBlock* item;          // The grid item view
    float baseline;           // Distance from top of item to baseline
    float baseline_shim;      // Adjustment to apply for alignment
    bool participates;        // Whether item participates in baseline alignment
};

/**
 * Baseline group for a single row in the grid.
 * Items in the same row with baseline alignment share a common baseline.
 */
struct RowBaselineGroup {
    int row_index;                         // Which row this group represents
    ItemBaselineInfo items[GRID_BASELINE_MAX_ITEMS_PER_GROUP]; // Items with baseline alignment in this row
    int item_count;
    float max_baseline_above;              // Max distance from top to baseline
    float max_baseline_below;              // Max distance from baseline to bottom
    float group_baseline;                  // Computed shared baseline for the row
};

struct RowBaselineGroupArray {
    RowBaselineGroup groups[GRID_BASELINE_MAX_GROUPS];
    int count;

    void clear() {
        count = 0;
    }

    RowBaselineGroup* append(int row_index) {
        if (count >= GRID_BASELINE_MAX_GROUPS) {
            return nullptr;
        }
        RowBaselineGroup* group = &groups[count++];
        group->row_index = row_index;
        group->item_count = 0;
        group->max_baseline_above = 0.0f;
        group->max_baseline_below = 0.0f;
        group->group_baseline = 0.0f;
        return group;
    }
};

inline bool row_baseline_group_append_item(RowBaselineGroup* group, const ItemBaselineInfo& info) {
    if (!group || group->item_count >= GRID_BASELINE_MAX_ITEMS_PER_GROUP) {
        return false;
    }
    group->items[group->item_count++] = info;
    return true;
}

} // namespace grid
} // namespace radiant

namespace radiant {
namespace grid {

// Bring global ViewBlock into this namespace scope so it shadows the forward decl in namespace radiant
using ::ViewBlock;
using ::GridContainerLayout;

// ============================================================================
// Baseline Calculation
// ============================================================================

/**
 * Determine if an item participates in baseline alignment.
 * Item participates if align-self is baseline and it doesn't span multiple rows.
 */
inline bool item_participates_in_baseline(ViewBlock* item) {
    GridItemProp* gi = grid_item_prop(item);
    if (!item || !gi) return false;

    // Get align-self value from grid item properties
    int align_self = gi->align_self_grid;

    // CSS_VALUE_BASELINE = 22 typically
    constexpr int CSS_VALUE_BASELINE = 22;

    // Must have baseline alignment
    if (align_self != CSS_VALUE_BASELINE) return false;

    // Items spanning multiple rows don't participate in baseline alignment
    int row_span = gi->computed_grid_row_end - gi->computed_grid_row_start;
    if (row_span > 1) return false;

    return true;
}

/**
 * Compute first baseline of an element.
 * Returns distance from top edge to first baseline.
 * Returns -1 if no baseline can be determined.
 */
inline float compute_item_first_baseline(ViewBlock* view) {
    return radiant::compute_element_first_baseline(nullptr, view, true);
}

// ============================================================================
// Baseline Resolution Algorithm
// ============================================================================

/**
 * Resolve baselines for all items in a grid container.
 * Groups items by row and computes shared baselines.
 *
 * @param grid_layout The grid container layout with items
 * @param out_groups  Output: baseline groups by row
 */
inline void resolve_grid_item_baselines(
    GridContainerLayout* grid_layout,
    RowBaselineGroupArray* out_groups
) {
    if (!out_groups) return;
    out_groups->clear();
    if (!grid_layout || !grid_layout->grid_items || grid_layout->item_count <= 0) {
        return;
    }

    // Map from row index to group
    int row_to_group_index[GRID_BASELINE_MAX_TRACKS];
    for (int i = 0; i < GRID_BASELINE_MAX_TRACKS; i++) {
        row_to_group_index[i] = -1;
    }

    // First pass: collect items that participate in baseline alignment
    for (int i = 0; i < grid_layout->item_count; ++i) {
        ViewBlock* item = grid_layout->grid_items[i];

        if (!item_participates_in_baseline(item)) continue;

        // computed_grid_row_start is 1-based, convert to 0-based
        GridItemProp* gi = grid_item_prop(item);
        if (!gi) continue;
        int row = gi->computed_grid_row_start - 1;
        if (row < 0 || row >= grid_layout->computed_row_count) continue;
        if (row >= GRID_BASELINE_MAX_TRACKS) continue;

        // Get or create group for this row
        int group_index = row_to_group_index[row];
        if (group_index < 0) {
            RowBaselineGroup* group = out_groups->append(row);
            if (!group) continue;
            group_index = out_groups->count - 1;
            row_to_group_index[row] = group_index;
        }

        // Compute item's baseline
        ItemBaselineInfo info;
        info.item = item;
        info.baseline = compute_item_first_baseline(item);
        info.baseline_shim = 0.0f;
        info.participates = true;

        row_baseline_group_append_item(&out_groups->groups[group_index], info);
    }

    // Second pass: compute shared baselines per group
    for (int group_index = 0; group_index < out_groups->count; group_index++) {
        RowBaselineGroup& group = out_groups->groups[group_index];
        if (group.item_count <= 0) continue;

        // Find max baseline above and below
        for (int item_index = 0; item_index < group.item_count; item_index++) {
            const ItemBaselineInfo& info = group.items[item_index];
            if (info.baseline < 0) continue;

            float item_height = info.item ? info.item->height : 0.0f;
            float above = info.baseline;
            float below = item_height - info.baseline;

            if (above > group.max_baseline_above) {
                group.max_baseline_above = above;
            }
            if (below > group.max_baseline_below) {
                group.max_baseline_below = below;
            }
        }

        // Set the shared baseline
        group.group_baseline = group.max_baseline_above;

        // Compute baseline shim for each item
        for (int item_index = 0; item_index < group.item_count; item_index++) {
            ItemBaselineInfo& info = group.items[item_index];
            if (info.baseline >= 0) {
                // Shim is the offset needed to align this item's baseline to the group baseline
                info.baseline_shim = group.group_baseline - info.baseline;
            }
        }
    }
}

/**
 * Apply baseline shims to grid items.
 * Call after baselines are resolved and items are positioned.
 */
inline void apply_baseline_shims(
    const RowBaselineGroupArray* groups
) {
    if (!groups) return;
    for (int group_index = 0; group_index < groups->count; group_index++) {
        const RowBaselineGroup& group = groups->groups[group_index];
        for (int item_index = 0; item_index < group.item_count; item_index++) {
            const ItemBaselineInfo& info = group.items[item_index];
            if (!info.participates || !info.item) continue;
            if (info.baseline_shim == 0.0f) continue;

            // Adjust item's Y position by the baseline shim
            info.item->y += info.baseline_shim;
        }
    }
}

/**
 * Convenience function: resolve and apply baselines in one call.
 */
inline void resolve_and_apply_grid_baselines(GridContainerLayout* grid_layout) {
    RowBaselineGroupArray groups;
    resolve_grid_item_baselines(grid_layout, &groups);
    apply_baseline_shims(&groups);
}

} // namespace grid
} // namespace radiant
