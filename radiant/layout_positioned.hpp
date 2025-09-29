#pragma once
#include "view.hpp"

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
 */

// Core positioning functions
void layout_relative_positioned(LayoutContext* lycon, ViewBlock* block);
void layout_absolute_positioned(LayoutContext* lycon, ViewBlock* block);

// Utility functions
bool element_has_positioning(ViewBlock* block);
bool element_has_float(ViewBlock* block);
ViewBlock* find_containing_block(ViewBlock* element, PropValue position_type);
void calculate_relative_offset(ViewBlock* block, int* offset_x, int* offset_y);
void calculate_absolute_position(ViewBlock* block, ViewBlock* containing_block);

// Stacking context management (Phase 3)
typedef struct StackingContext {
    ViewBlock* establishing_element;  // element that creates the context
    int z_index;                     // z-index of this context
    struct StackingContext* parent;   // parent stacking context
    // Note: positioned children list will be added in Phase 3
} StackingContext;

// Float context management (Phase 4)
typedef struct FloatBox {
    ViewBlock* element;      // floating element
    int x, y, width, height; // float box bounds
    PropValue float_side;    // left or right
} FloatBox;

typedef struct FloatContext {
    FloatBox* left_floats;    // array of left-floating elements
    FloatBox* right_floats;   // array of right-floating elements
    int left_count, right_count;  // counts for each side
    int current_y;           // current line position
    ViewBlock* container;    // containing block
} FloatContext;
