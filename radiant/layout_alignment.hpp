#pragma once
/**
 * layout_alignment.hpp - Unified alignment functions for flex and grid
 *
 * Provides common alignment computation used by both flex and grid layouts:
 * - justify-content / align-content (container level)
 * - align-items / align-self (item level)
 * - justify-items / justify-self (grid only)
 * - space-between / space-around / space-evenly distribution
 *
 * Extracted from duplicated code in layout_flex.cpp and layout_grid.cpp.
 */

#include <cstdint>

// CSS value constants (from css_value.hpp)
// We use int32_t to allow both enum and numeric values
// Common alignment values:
// CSS_VALUE_FLEX_START, CSS_VALUE_FLEX_END, CSS_VALUE_CENTER,
// CSS_VALUE_SPACE_BETWEEN, CSS_VALUE_SPACE_AROUND, CSS_VALUE_SPACE_EVENLY,
// CSS_VALUE_STRETCH, CSS_VALUE_BASELINE, CSS_VALUE_START, CSS_VALUE_END

namespace radiant {

// Forward declarations
struct LayoutContext;
struct ViewBlock;

// ============================================================================
// SpaceDistribution - Result of space distribution calculation
// ============================================================================

/**
 * SpaceDistribution holds the computed gaps for space-between/around/evenly.
 */
struct SpaceDistribution {
    float gap_before_first;   // Space before first item
    float gap_between;        // Space between items (added to existing gap)
    float gap_after_last;     // Space after last item
};

inline SpaceDistribution space_distribution_none() {
    return {0.0f, 0.0f, 0.0f};
}

// ============================================================================
// Alignment Offset Computation
// ============================================================================

/**
 * Compute offset for aligning content/items based on alignment value.
 *
 * @param alignment    CSS alignment value (CSS_VALUE_FLEX_START, etc.)
 * @param free_space   Available space for distribution (can be negative)
 * @param is_safe      If true, prevent overflow (unsafe alignment can overflow)
 * @return             Offset from start position
 *
 * For multi-item distribution (space-between, etc.), use compute_space_distribution.
 */
float compute_alignment_offset(
    int32_t alignment,
    float free_space,
    bool is_safe
);

/**
 * Simplified alignment offset (common case, not safe alignment).
 */
inline float compute_alignment_offset_simple(int32_t alignment, float free_space) {
    return compute_alignment_offset(alignment, free_space, false);
}

// ============================================================================
// Space Distribution
// ============================================================================

/**
 * Compute space distribution for justify-content / align-content.
 *
 * @param alignment    CSS alignment value
 * @param free_space   Total free space to distribute
 * @param item_count   Number of items/lines
 * @param existing_gap Base gap between items (from row-gap/column-gap)
 * @return             SpaceDistribution with computed gaps
 *
 * For negative free_space, distribution falls back to flex-start alignment.
 */
SpaceDistribution compute_space_distribution(
    int32_t alignment,
    float free_space,
    int32_t item_count,
    float existing_gap
);

// ============================================================================
// Safe Alignment Fallback
// ============================================================================

/**
 * Get fallback alignment when free_space is negative (overflow).
 * space-between/around/evenly fall back to flex-start.
 * safe alignment falls back to start.
 *
 * @param alignment    Original alignment value
 * @param free_space   Available space (negative means overflow)
 * @return             Effective alignment to use
 */
int32_t alignment_fallback_for_overflow(int32_t alignment, float free_space);

// ============================================================================
// Alignment Value Helpers
// ============================================================================

/**
 * Check if alignment is a space distribution type.
 */
bool alignment_is_space_distribution(int32_t alignment);

/**
 * Check if alignment is baseline.
 */
bool alignment_is_baseline(int32_t alignment);

/**
 * Check if alignment is stretch.
 */
bool alignment_is_stretch(int32_t alignment);

/**
 * Resolve align-self: auto to inherited align-items value.
 */
int32_t resolve_align_self(int32_t align_self, int32_t align_items);

/**
 * Resolve justify-self: auto to inherited justify-items value (grid only).
 */
int32_t resolve_justify_self(int32_t justify_self, int32_t justify_items);

// ============================================================================
// Baseline Calculation
// ============================================================================

/**
 * Compute first baseline position for an element.
 * Returns distance from element's top edge to first baseline.
 * Returns -1 if element has no baseline.
 */
float compute_element_first_baseline(
    LayoutContext* lycon,
    ViewBlock* element,
    bool is_row_direction
);

/**
 * Compute last baseline position for an element.
 */
float compute_element_last_baseline(
    LayoutContext* lycon,
    ViewBlock* element,
    bool is_row_direction
);

// ============================================================================
// Cross-axis Size Resolution (Stretch)
// ============================================================================

/**
 * Compute stretched cross size for an item with align-self: stretch.
 *
 * @param item_cross_size   Item's natural cross size
 * @param line_cross_size   Line's cross size
 * @param margin_cross      Total cross-axis margin
 * @param min_cross         Item's min cross size constraint
 * @param max_cross         Item's max cross size constraint
 * @param has_definite_size True if item has definite cross size
 * @return                  Resolved cross size
 */
float compute_stretched_cross_size(
    float item_cross_size,
    float line_cross_size,
    float margin_cross,
    float min_cross,
    float max_cross,
    bool has_definite_size
);

} // namespace radiant
