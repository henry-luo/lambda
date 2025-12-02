#pragma once

#include "layout.hpp"
#include "grid.hpp"

// Multi-pass grid layout header
// Defines the enhanced grid layout functions with proper content measurement
// Follows the same pattern as layout_flex_multipass.hpp

// ============================================================================
// Main entry point - manages the entire grid content flow
// ============================================================================

/**
 * Main entry point for grid layout - called from layout_block.cpp
 * This replaces the inline grid code block in layout_block_inner_content()
 *
 * Implements multi-pass layout:
 *   Pass 0: Style resolution and view initialization (resolve_grid_item_styles)
 *   Pass 1: Intrinsic content measurement (measure_grid_items)
 *   Pass 2: Grid algorithm execution (track sizing, item placement)
 *   Pass 3: Final content layout with determined sizes
 */
void layout_grid_content(LayoutContext* lycon, ViewBlock* grid_container);

// ============================================================================
// Pass 0: Style Resolution and View Initialization
// ============================================================================

/**
 * Initialize grid items with style resolution only (no full layout)
 * Creates View objects and resolves CSS properties without laying out content
 *
 * @param lycon Layout context
 * @param grid_container The grid container
 * @return Number of grid items initialized
 */
int resolve_grid_item_styles(LayoutContext* lycon, ViewBlock* grid_container);

/**
 * Initialize a single grid item view
 * Creates the View structure and resolves styles without layout
 *
 * @param lycon Layout context
 * @param child The DOM node to initialize
 */
void init_grid_item_view(LayoutContext* lycon, DomNode* child);

// ============================================================================
// Pass 1: Content Measurement
// ============================================================================

/**
 * Measure intrinsic sizes of all grid items
 * Used for track sizing with min-content/max-content
 *
 * @param lycon Layout context
 * @param grid_layout Grid container layout state
 */
void measure_grid_items(LayoutContext* lycon, GridContainerLayout* grid_layout);

/**
 * Measure intrinsic size of a single grid item
 *
 * @param lycon Layout context
 * @param item Grid item to measure
 * @param min_width Output: minimum content width
 * @param max_width Output: maximum content width
 * @param min_height Output: minimum content height
 * @param max_height Output: maximum content height
 */
void measure_grid_item_intrinsic(LayoutContext* lycon, ViewBlock* item,
                                  int* min_width, int* max_width,
                                  int* min_height, int* max_height);

// ============================================================================
// Pass 2: Grid Algorithm (already in layout_grid.cpp)
// ============================================================================
// Uses existing functions:
// - collect_grid_items()
// - place_grid_items()
// - determine_grid_size()
// - resolve_track_sizes()
// - position_grid_items()
// - align_grid_items()

// ============================================================================
// Pass 3: Final Content Layout
// ============================================================================

/**
 * Layout final content within each grid item
 * Called after grid algorithm has determined item positions and sizes
 *
 * @param lycon Layout context
 * @param grid_layout Grid container layout state
 */
void layout_final_grid_content(LayoutContext* lycon, GridContainerLayout* grid_layout);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Check if grid item requires nested grid/flex handling
 */
bool grid_item_is_nested_container(ViewBlock* item);

/**
 * Layout absolute positioned children within a grid container
 * These are excluded from the grid algorithm but still need layout
 */
void layout_grid_absolute_children(LayoutContext* lycon, ViewBlock* container);
