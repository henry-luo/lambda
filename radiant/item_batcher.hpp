#pragma once
/**
 * item_batcher.hpp - Grid item batching for track sizing algorithm
 *
 * Implements Taffy-style item batching where grid items are processed:
 * 1. First by whether they cross a flexible track (non-flex first)
 * 2. Then by ascending span count
 *
 * This is required by CSS Grid specification Section 11.5:
 * "Repeat for items with larger spans until all items have been considered."
 *
 * The batching ensures correct intrinsic track sizing by processing items
 * in the order that respects their contribution to track growth.
 */

#include <vector>
#include <algorithm>
#include <cstdint>

struct ViewBlock;
struct GridContainerLayout;
struct GridTrack;

namespace radiant {
namespace grid {

// ============================================================================
// ItemBatcher - Batches grid items by span and flex-track status
// ============================================================================

/**
 * Represents a batch of grid items with the same span count and flex status.
 * Used by the track sizing algorithm to process items in the correct order.
 */
struct ItemBatch {
    uint16_t span;              // The span count for this batch
    bool crosses_flex_track;    // Whether items in this batch cross a flex track
    size_t start_index;         // Start index in the sorted items array
    size_t end_index;           // End index (exclusive) in the sorted items array
};

/**
 * ItemBatcher manages grid item ordering for track sizing.
 *
 * CSS Grid spec requires items to be processed in order:
 * 1. Items not spanning flexible tracks, by ascending span
 * 2. Items spanning flexible tracks, by ascending span
 *
 * Usage:
 *   ItemBatcher batcher;
 *   batcher.prepare(grid_layout, is_row_axis);
 *   for (auto& batch : batcher.batches()) {
 *       for (size_t i = batch.start_index; i < batch.end_index; ++i) {
 *           ViewBlock* item = batcher.item_at(i);
 *           // Process item...
 *       }
 *   }
 */
class ItemBatcher {
public:
    ItemBatcher() = default;

    /**
     * Prepare the batcher with items from a grid container.
     *
     * @param grid_layout The grid container layout
     * @param is_row_axis True if sizing rows, false if sizing columns
     */
    void prepare(GridContainerLayout* grid_layout, bool is_row_axis);

    /**
     * Get all batches for iteration.
     */
    const std::vector<ItemBatch>& batches() const { return m_batches; }

    /**
     * Get the item at a specific index in the sorted order.
     */
    ViewBlock* item_at(size_t index) const {
        return (index < m_sorted_items.size()) ? m_sorted_items[index] : nullptr;
    }

    /**
     * Get total number of items.
     */
    size_t item_count() const { return m_sorted_items.size(); }

    /**
     * Check if there are any batches to process.
     */
    bool empty() const { return m_batches.empty(); }

    /**
     * Get the maximum span count encountered.
     */
    uint16_t max_span() const { return m_max_span; }

private:
    struct SortedItem {
        ViewBlock* item;
        uint16_t span;
        bool crosses_flex;
    };

    std::vector<ViewBlock*> m_sorted_items;
    std::vector<ItemBatch> m_batches;
    uint16_t m_max_span = 0;

    // Helper to compute span for an item in the given axis
    uint16_t compute_span(ViewBlock* item, bool is_row_axis) const;

    // Helper to check if item crosses a flex track
    bool crosses_flexible_track(
        ViewBlock* item,
        GridTrack* tracks,
        int track_count,
        bool is_row_axis
    ) const;
};

} // namespace grid
} // namespace radiant

// Include implementation after forward declarations
#include "grid.hpp"
#include "view.hpp"

namespace radiant {
namespace grid {

inline uint16_t ItemBatcher::compute_span(ViewBlock* item, bool is_row_axis) const {
    if (!item || !item->gi) return 1;

    if (is_row_axis) {
        int start = item->gi->computed_grid_row_start;
        int end = item->gi->computed_grid_row_end;
        return static_cast<uint16_t>((end > start) ? (end - start) : 1);
    } else {
        int start = item->gi->computed_grid_column_start;
        int end = item->gi->computed_grid_column_end;
        return static_cast<uint16_t>((end > start) ? (end - start) : 1);
    }
}

inline bool ItemBatcher::crosses_flexible_track(
    ViewBlock* item,
    GridTrack* tracks,
    int track_count,
    bool is_row_axis
) const {
    if (!item || !item->gi || !tracks || track_count <= 0) return false;

    int start, end;
    if (is_row_axis) {
        // computed_grid_row_start/end are 1-based line numbers
        start = item->gi->computed_grid_row_start - 1;  // Convert to 0-based track index
        end = item->gi->computed_grid_row_end - 1;
    } else {
        start = item->gi->computed_grid_column_start - 1;
        end = item->gi->computed_grid_column_end - 1;
    }

    // Check each track in the span
    for (int i = start; i < end && i < track_count; ++i) {
        if (i >= 0 && tracks[i].is_flexible) {
            return true;
        }
    }
    return false;
}

inline void ItemBatcher::prepare(GridContainerLayout* grid_layout, bool is_row_axis) {
    m_sorted_items.clear();
    m_batches.clear();
    m_max_span = 0;

    if (!grid_layout || !grid_layout->grid_items || grid_layout->item_count <= 0) {
        return;
    }

    // Get the tracks for the axis we're sizing
    GridTrack* tracks = is_row_axis ? grid_layout->computed_rows : grid_layout->computed_columns;
    int track_count = is_row_axis ? grid_layout->computed_row_count : grid_layout->computed_column_count;

    // Build sortable item list
    std::vector<SortedItem> sortable;
    sortable.reserve(grid_layout->item_count);

    for (int i = 0; i < grid_layout->item_count; ++i) {
        ViewBlock* item = grid_layout->grid_items[i];
        if (!item || !item->gi) continue;  // Skip invalid items

        SortedItem si;
        si.item = item;
        si.span = compute_span(item, is_row_axis);
        si.crosses_flex = crosses_flexible_track(item, tracks, track_count, is_row_axis);

        if (si.span > m_max_span) {
            m_max_span = si.span;
        }

        sortable.push_back(si);
    }

    if (sortable.empty()) return;

    // Sort by: (1) crosses_flex ascending (non-flex first), (2) span ascending
    std::sort(sortable.begin(), sortable.end(), [](const SortedItem& a, const SortedItem& b) {
        if (a.crosses_flex != b.crosses_flex) {
            return !a.crosses_flex;  // Non-flex first
        }
        return a.span < b.span;  // Then by ascending span
    });

    // Extract sorted items and build batches
    m_sorted_items.reserve(sortable.size());
    for (const auto& si : sortable) {
        m_sorted_items.push_back(si.item);
    }

    // Build batches - group items with same (crosses_flex, span)
    if (!sortable.empty()) {
        size_t batch_start = 0;
        bool current_flex = sortable[0].crosses_flex;
        uint16_t current_span = sortable[0].span;

        for (size_t i = 1; i <= sortable.size(); ++i) {
            bool should_close_batch = (i == sortable.size()) ||
                                       (sortable[i].crosses_flex != current_flex) ||
                                       (sortable[i].span != current_span);

            if (should_close_batch) {
                ItemBatch batch;
                batch.span = current_span;
                batch.crosses_flex_track = current_flex;
                batch.start_index = batch_start;
                batch.end_index = i;
                m_batches.push_back(batch);

                if (i < sortable.size()) {
                    batch_start = i;
                    current_flex = sortable[i].crosses_flex;
                    current_span = sortable[i].span;
                }
            }
        }
    }
}

} // namespace grid
} // namespace radiant
