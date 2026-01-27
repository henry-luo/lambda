#pragma once
/**
 * grid_baseline.hpp - Grid item baseline alignment support
 *
 * Implements baseline alignment for CSS Grid items as specified in CSS Grid Level 1.
 * Baseline alignment groups items by row and aligns them to a common baseline.
 *
 * Based on Taffy's resolve_item_baselines() implementation.
 *
 * TODO: std::* Migration Plan (Phase 5+)
 * - std::vector<ItemBaselineInfo> items → Fixed array + count or ArrayList*
 * - std::vector<RowBaselineGroup> → Fixed array with MAX_GRID_ROWS limit
 * - std::vector<int> row_to_group_index → Fixed array with MAX_GRID_ROWS
 * Estimated effort: Moderate refactoring (~100 lines)
 */

#include <vector>
#include <cmath>
#include <algorithm>

// Forward declarations
struct LayoutContext;
struct ViewBlock;
struct GridContainerLayout;

namespace radiant {
namespace grid {

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
    std::vector<ItemBaselineInfo> items;   // Items with baseline alignment in this row
    float max_baseline_above;              // Max distance from top to baseline
    float max_baseline_below;              // Max distance from baseline to bottom
    float group_baseline;                  // Computed shared baseline for the row
};

} // namespace grid
} // namespace radiant

// Include implementation dependencies after forward declarations
#include "grid.hpp"
#include "view.hpp"

namespace radiant {
namespace grid {

// ============================================================================
// Baseline Calculation
// ============================================================================

/**
 * Determine if an item participates in baseline alignment.
 * Item participates if align-self is baseline and it doesn't span multiple rows.
 */
inline bool item_participates_in_baseline(ViewBlock* item) {
    if (!item || !item->gi) return false;

    // Get align-self value from grid item properties
    int align_self = item->gi->align_self;

    // CSS_VALUE_BASELINE = 22 typically
    constexpr int CSS_VALUE_BASELINE = 22;

    // Must have baseline alignment
    if (align_self != CSS_VALUE_BASELINE) return false;

    // Items spanning multiple rows don't participate in baseline alignment
    int row_span = item->gi->computed_grid_row_end - item->gi->computed_grid_row_start;
    if (row_span > 1) return false;

    return true;
}

/**
 * Compute first baseline of an element.
 * Returns distance from top edge to first baseline.
 * Returns -1 if no baseline can be determined.
 */
inline float compute_item_first_baseline(ViewBlock* view) {
    if (!view) return -1.0f;

    // If element has font metrics, use font baseline
    if (view->font) {
        // First baseline is at font ascent from top of content area
        float padding_top = 0.0f;
        if (view->bound) {
            padding_top = view->bound->padding.top;
            if (view->bound->border) {
                padding_top += view->bound->border->width.top;
            }
        }
        // Approximate: baseline at ~80% of font size from top
        return padding_top + view->font->font_size * 0.8f;
    }

    // For containers, recursively find first baseline child
    if (view->first_child) {
        DomNode* child = view->first_child;
        while (child) {
            if (child->is_element()) {
                ViewBlock* child_view = (ViewBlock*)child->as_element();
                float child_baseline = compute_item_first_baseline(child_view);
                if (child_baseline >= 0) {
                    return child_view->y + child_baseline;
                }
            }
            child = child->next_sibling;
        }
    }

    // Fallback: use bottom margin edge as synthetic baseline
    return view->height;
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
    std::vector<RowBaselineGroup>& out_groups
) {
    out_groups.clear();
    if (!grid_layout || !grid_layout->grid_items || grid_layout->item_count <= 0) {
        return;
    }

    // Map from row index to group
    std::vector<int> row_to_group_index(grid_layout->computed_row_count, -1);

    // First pass: collect items that participate in baseline alignment
    for (int i = 0; i < grid_layout->item_count; ++i) {
        ViewBlock* item = grid_layout->grid_items[i];

        if (!item_participates_in_baseline(item)) continue;

        // computed_grid_row_start is 1-based, convert to 0-based
        int row = item->gi->computed_grid_row_start - 1;
        if (row < 0 || row >= grid_layout->computed_row_count) continue;

        // Get or create group for this row
        int group_index = row_to_group_index[row];
        if (group_index < 0) {
            group_index = static_cast<int>(out_groups.size());
            row_to_group_index[row] = group_index;

            RowBaselineGroup group;
            group.row_index = row;
            group.max_baseline_above = 0.0f;
            group.max_baseline_below = 0.0f;
            group.group_baseline = 0.0f;
            out_groups.push_back(group);
        }

        // Compute item's baseline
        ItemBaselineInfo info;
        info.item = item;
        info.baseline = compute_item_first_baseline(item);
        info.baseline_shim = 0.0f;
        info.participates = true;

        out_groups[group_index].items.push_back(info);
    }

    // Second pass: compute shared baselines per group
    for (auto& group : out_groups) {
        if (group.items.empty()) continue;

        // Find max baseline above and below
        for (const auto& info : group.items) {
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
        for (auto& info : group.items) {
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
    const std::vector<RowBaselineGroup>& groups
) {
    for (const auto& group : groups) {
        for (const auto& info : group.items) {
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
    std::vector<RowBaselineGroup> groups;
    resolve_grid_item_baselines(grid_layout, groups);
    apply_baseline_shims(groups);
}

} // namespace grid
} // namespace radiant
