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

// C-style query functions
inline bool run_mode_is_compute_size(RunMode mode) {
    return mode == RunMode::ComputeSize;
}

inline bool run_mode_is_perform_layout(RunMode mode) {
    return mode == RunMode::PerformLayout;
}

inline bool run_mode_is_hidden(RunMode mode) {
    return mode == RunMode::PerformHiddenLayout;
}

// Check if positioning should be performed
inline bool run_mode_should_position(RunMode mode) {
    return mode == RunMode::PerformLayout;
}

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

// C-style query functions
inline bool sizing_mode_is_inherent(SizingMode mode) {
    return mode == SizingMode::InherentSize;
}

inline bool sizing_mode_is_content(SizingMode mode) {
    return mode == SizingMode::ContentSize;
}

// ============================================================================
// LayoutOutput - Result of layout computation
// ============================================================================

/**
 * LayoutOutput holds the result of a layout computation.
 * Used to return computed dimensions and optional baseline.
 */
struct LayoutOutput {
    float width;
    float height;
    float first_baseline;   // Distance from top to first baseline (-1 if none)
    float last_baseline;    // Distance from top to last baseline (-1 if none)
};

// Constructor helpers
inline LayoutOutput layout_output_from_size(float width, float height) {
    return {width, height, -1.0f, -1.0f};
}

inline LayoutOutput layout_output_from_size_and_baseline(float width, float height, float first_baseline) {
    return {width, height, first_baseline, first_baseline};
}

inline LayoutOutput layout_output_zero() {
    return {0.0f, 0.0f, -1.0f, -1.0f};
}

// Query helpers
inline bool layout_output_has_baseline(LayoutOutput output) {
    return output.first_baseline >= 0;
}

} // namespace radiant
