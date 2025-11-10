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

// Float context management (Phase 4)
typedef struct FloatBox {
    ViewBlock* element;      // floating element
    int x, y, width, height; // float box bounds
    CssEnum float_side;    // left or right
} FloatBox;

typedef struct FloatContext {
    FloatBox* left_floats;    // array of left-floating elements
    FloatBox* right_floats;   // array of right-floating elements
    int left_count, right_count;  // counts for each side
    int current_y;           // current line position
    ViewBlock* container;    // containing block
} FloatContext;

// Core positioning functions
void layout_relative_positioned(LayoutContext* lycon, ViewBlock* block);

// Utility functions
bool element_has_positioning(ViewBlock* block);
ViewBlock* find_containing_block(ViewBlock* element, CssEnum position_type);

// Float context functions (Phase 4)
FloatContext* create_float_context(ViewBlock* container);
void add_float_to_context(FloatContext* ctx, ViewBlock* element, CssEnum float_side);
void position_float_element(FloatContext* ctx, ViewBlock* element, CssEnum float_side);
void layout_float_element(LayoutContext* lycon, ViewBlock* block);
void adjust_line_for_floats(LayoutContext* lycon, FloatContext* float_ctx);
int find_clear_position(FloatContext* ctx, CssEnum clear_value);

// Float context lifecycle management
void init_float_context_for_block(LayoutContext* lycon, ViewBlock* block);
void cleanup_float_context(LayoutContext* lycon);
FloatContext* get_current_float_context(LayoutContext* lycon);

// Clear property functions (Phase 5)
void layout_clear_element(LayoutContext* lycon, ViewBlock* block);

// Line box adjustment functions (Phase 6)
bool float_intersects_line(FloatBox* float_box, int line_top, int line_bottom);
