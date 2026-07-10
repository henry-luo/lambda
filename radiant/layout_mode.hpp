#pragma once
/**
 * layout_mode.hpp - Layout run modes and sizing modes
 *
 * Provides enums for controlling layout behavior:
 * - RunMode: Whether to compute size only or perform full layout
 * - SizingMode: Whether to use element's own size or content size
 *
 * These enable early bailout optimizations when only measurements are needed.
 * Inspired by Taffy's RunMode and SizingMode enums.
 */

#include <cstdint>

namespace radiant {

// ============================================================================
// RunMode - Controls layout computation depth
// ============================================================================

/**
 * RunMode determines how much work layout performs:
 *
 * - ComputeSize: Only compute final dimensions, skip positioning.
 *   Used when parent just needs to know child's size (flex/grid measurement).
 *   Allows early bailout if dimensions are already known.
 *
 * - PerformLayout: Full layout with final positioning.
 *   Sets x, y, width, height on all elements.
 *
 * - PerformHiddenLayout: Minimal layout for display:none elements.
 *   Sets dimensions to 0, skips most computation.
 */
enum class RunMode : uint8_t {
    ComputeSize = 0,       // Only compute dimensions
    PerformLayout = 1,     // Full layout with positioning
    PerformHiddenLayout = 2  // Layout for display:none (minimal work)
};

// ============================================================================
// SizingMode - Controls which size properties to use
// ============================================================================

/**
 * SizingMode determines which size to use for layout:
 *
 * - InherentSize: Use the element's own CSS size properties (width, height).
 *   This is normal layout behavior.
 *
 * - ContentSize: Ignore CSS size properties, use content-based size.
 *   Used when measuring intrinsic sizes (min-content, max-content).
 */
enum class SizingMode : uint8_t {
    InherentSize = 0,  // Use element's own size styles
    ContentSize = 1    // Use intrinsic content size (ignore CSS width/height)
};

} // namespace radiant
