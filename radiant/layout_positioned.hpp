#pragma once
#include "view.hpp"
#include "layout.hpp"  // For BlockContext, FloatBox, FloatAvailableSpace
#include <math.h>

// Forward declarations
struct LayoutContext;
struct ViewBlock;

/**
 * CSS Positioning Layout Functions
 *
 * This module implements CSS positioning support including:
 * - Relative positioning (position: relative)
 * - Absolute positioning (position: absolute)
 * - Fixed positioning (position: fixed)
 * - Float layout (float: left/right)
 * - Clear property (clear: left/right/both)
 *
 * NOTE: FloatBox and FloatAvailableSpace are now defined in layout.hpp
 * as part of the unified BlockContext system.
 */

// Core positioning functions
void layout_relative_position_offset(ViewBlock* block, float* offset_x, float* offset_y);
void layout_relative_positioned(LayoutContext* lycon, ViewBlock* block);

// Utility functions
bool element_has_positioning(ViewBlock* block);
ViewBlock* find_initial_containing_view_block(ViewBlock* element);
ViewBlock* find_positioned_containing_block(ViewElement* view);
ViewBlock* find_containing_block(ViewBlock* element, CssEnum position_type);

// ============================================================================
// Float Layout API (using BlockContext)
// ============================================================================

// Float layout integration
void layout_float_element(LayoutContext* lycon, ViewBlock* block);
void adjust_line_for_floats(LayoutContext* lycon);
void layout_clear_element(LayoutContext* lycon, ViewBlock* block);
// Re-resolve percentage-based vertical dimensions for abs children after containing block height is known
// CSS 2.1 §10.5: For absolutely positioned elements, percentage heights resolve
// against the containing block's used height. When the CB has auto height, this
// must be deferred until after auto height is finalized.
void re_resolve_abs_children_vertical(ViewBlock* containing_block);

// Normalize flex static-positioned abs/fixed descendants after normal-flow
// ancestors have their final positions.
void layout_finalize_static_positioned_abs_descendants(ViewBlock* root);

// Shift static-positioned absolute/fixed descendants when a normal-flow ancestor
// is moved after those descendants have already been laid out.
void layout_shift_static_positioned_abs_descendants(ViewElement* root, float delta_x, float delta_y);
