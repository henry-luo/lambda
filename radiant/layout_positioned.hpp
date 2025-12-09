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
void layout_relative_positioned(LayoutContext* lycon, ViewBlock* block);

// Utility functions
bool element_has_positioning(ViewBlock* block);
ViewBlock* find_containing_block(ViewBlock* element, CssEnum position_type);

// ============================================================================
// Float Layout API (using BlockContext)
// ============================================================================

// Float layout integration
void layout_float_element(LayoutContext* lycon, ViewBlock* block);
void adjust_line_for_floats(LayoutContext* lycon);
void layout_clear_element(LayoutContext* lycon, ViewBlock* block);
