/**
 * layout_alignment.cpp - Implementation of unified alignment functions
 */

#include "layout_alignment.hpp"
#include "layout.hpp"
#include "view.hpp"
#include "../lambda/input/css/css_value.hpp"

namespace radiant {

// ============================================================================
// Alignment Offset Computation
// ============================================================================

float compute_alignment_offset(
    int32_t alignment,
    float free_space,
    bool is_safe
) {
    // Safe alignment: prevent overflow by falling back to start
    if (is_safe && free_space < 0) {
        return 0.0f;
    }

    switch (alignment) {
        case CSS_VALUE_FLEX_START:
        case CSS_VALUE_START:
            return 0.0f;

        case CSS_VALUE_FLEX_END:
        case CSS_VALUE_END:
            return free_space;

        case CSS_VALUE_CENTER:
            return free_space / 2.0f;

        case CSS_VALUE_STRETCH:
            // For single items, stretch means start position
            // Stretching the size is handled separately
            return 0.0f;

        case CSS_VALUE_BASELINE:
            // Baseline alignment offset computed separately
            return 0.0f;

        // Space distribution types - caller should use compute_space_distribution
        case CSS_VALUE_SPACE_BETWEEN:
        case CSS_VALUE_SPACE_AROUND:
        case CSS_VALUE_SPACE_EVENLY:
            return 0.0f;

        default:
            return 0.0f;
    }
}

// ============================================================================
// Space Distribution
// ============================================================================

SpaceDistribution compute_space_distribution(
    int32_t alignment,
    float free_space,
    int32_t item_count,
    float existing_gap
) {
    SpaceDistribution dist = {0.0f, 0.0f, 0.0f};

    // No items or single item - no distribution needed
    if (item_count <= 0) {
        return dist;
    }

    // Negative free space - fall back to flex-start
    if (free_space < 0) {
        return dist;
    }

    int32_t gap_count = item_count - 1;  // Gaps between items

    switch (alignment) {
        case CSS_VALUE_FLEX_START:
        case CSS_VALUE_START:
            // All items at start, all free space at end
            dist.gap_after_last = free_space;
            break;

        case CSS_VALUE_FLEX_END:
        case CSS_VALUE_END:
            // All items at end, all free space at start
            dist.gap_before_first = free_space;
            break;

        case CSS_VALUE_CENTER:
            // Items centered, free space split equally at start/end
            dist.gap_before_first = free_space / 2.0f;
            dist.gap_after_last = free_space / 2.0f;
            break;

        case CSS_VALUE_SPACE_BETWEEN:
            // First item at start, last at end, space between
            if (gap_count > 0) {
                dist.gap_between = free_space / gap_count;
            } else {
                // Single item: center it (CSS spec)
                dist.gap_before_first = free_space / 2.0f;
                dist.gap_after_last = free_space / 2.0f;
            }
            break;

        case CSS_VALUE_SPACE_AROUND:
            // Equal space around each item (half at edges)
            if (item_count > 0) {
                float per_item_space = free_space / item_count;
                dist.gap_before_first = per_item_space / 2.0f;
                dist.gap_between = per_item_space;
                dist.gap_after_last = per_item_space / 2.0f;
            }
            break;

        case CSS_VALUE_SPACE_EVENLY:
            // Equal space between all items and edges
            if (item_count > 0) {
                int32_t total_gaps = item_count + 1;  // Including edges
                float per_gap = free_space / total_gaps;
                dist.gap_before_first = per_gap;
                dist.gap_between = per_gap;
                dist.gap_after_last = per_gap;
            }
            break;

        case CSS_VALUE_STRETCH:
            // For content alignment: stretch items to fill
            // Items are stretched individually; gaps stay as-is
            break;

        default:
            // Unknown alignment - treat as flex-start
            dist.gap_after_last = free_space;
            break;
    }

    return dist;
}

// ============================================================================
// Safe Alignment Fallback
// ============================================================================

int32_t alignment_fallback_for_overflow(int32_t alignment, float free_space) {
    if (free_space >= 0) {
        return alignment;
    }

    // Negative free space - space distribution falls back to flex-start
    switch (alignment) {
        case CSS_VALUE_SPACE_BETWEEN:
        case CSS_VALUE_SPACE_AROUND:
        case CSS_VALUE_SPACE_EVENLY:
            return CSS_VALUE_FLEX_START;

        default:
            return alignment;
    }
}

// ============================================================================
// Alignment Value Helpers
// ============================================================================

bool alignment_is_space_distribution(int32_t alignment) {
    return alignment == CSS_VALUE_SPACE_BETWEEN ||
           alignment == CSS_VALUE_SPACE_AROUND ||
           alignment == CSS_VALUE_SPACE_EVENLY;
}

bool alignment_is_baseline(int32_t alignment) {
    return alignment == CSS_VALUE_BASELINE;
}

bool alignment_is_stretch(int32_t alignment) {
    return alignment == CSS_VALUE_STRETCH;
}

int32_t resolve_align_self(int32_t align_self, int32_t align_items) {
    // auto resolves to parent's align-items
    if (align_self == CSS_VALUE_AUTO || align_self == CSS_VALUE__UNDEF) {
        return align_items;
    }
    return align_self;
}

int32_t resolve_justify_self(int32_t justify_self, int32_t justify_items) {
    // auto resolves to parent's justify-items
    if (justify_self == CSS_VALUE_AUTO || justify_self == CSS_VALUE__UNDEF) {
        return justify_items;
    }
    return justify_self;
}

// ============================================================================
// Baseline Calculation
// ============================================================================

float compute_element_first_baseline(
    LayoutContext* lycon,
    ViewBlock* element,
    bool is_row_direction
) {
    // TODO: Implement proper baseline calculation
    // For now, return a simple approximation based on font metrics
    // This should walk the element tree to find the first text baseline

    if (!element) return -1.0f;

    // Simple heuristic: use the ascender of default font as baseline
    // Real implementation should find first inline content baseline
    return -1.0f;  // No baseline
}

float compute_element_last_baseline(
    LayoutContext* lycon,
    ViewBlock* element,
    bool is_row_direction
) {
    // TODO: Implement proper last baseline calculation
    return -1.0f;  // No baseline
}

// ============================================================================
// Cross-axis Size Resolution (Stretch)
// ============================================================================

float compute_stretched_cross_size(
    float item_cross_size,
    float line_cross_size,
    float margin_cross,
    float min_cross,
    float max_cross,
    bool has_definite_size
) {
    // If item has definite cross size, don't stretch
    if (has_definite_size) {
        return item_cross_size;
    }

    // Stretch to fill line (minus margins)
    float available_cross = line_cross_size - margin_cross;
    if (available_cross < 0) available_cross = 0;

    // Apply min/max constraints
    float stretched = available_cross;
    if (min_cross > 0 && stretched < min_cross) {
        stretched = min_cross;
    }
    if (max_cross > 0 && stretched > max_cross) {
        stretched = max_cross;
    }

    return stretched;
}

} // namespace radiant
