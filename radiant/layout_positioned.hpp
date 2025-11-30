#pragma once
#include "view.hpp"
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
 */

// ============================================================================
// Enhanced Float Layout System
// Based on CSS 2.2 Section 9.5.1 Float Positioning Rules
// ============================================================================

/**
 * FloatBox - Represents a single floating element with margin box bounds
 * Tracks both the element position and its full margin box for proper
 * space calculations during line layout.
 */
typedef struct FloatBox {
    ViewBlock* element;         // The floating element
    
    // Margin box bounds (outer bounds including margins)
    // Used for space calculations and float stacking
    float margin_box_top;
    float margin_box_bottom;
    float margin_box_left;
    float margin_box_right;
    
    // Border box bounds (element position and size)
    float x, y, width, height;
    
    CssEnum float_side;         // CSS_VALUE_LEFT or CSS_VALUE_RIGHT
    struct FloatBox* next;      // Linked list for multiple floats
} FloatBox;

/**
 * FloatSideData - Container for floats on one side (left or right)
 * Uses a linked list to support multiple floats per side.
 */
typedef struct FloatSideData {
    FloatBox* head;             // Head of linked list
    FloatBox* tail;             // Tail for O(1) append
    int count;                  // Number of floats on this side
} FloatSideData;

/**
 * FloatContext - Context for float layout within a block formatting context
 * Manages all floats and provides efficient Y-based space queries.
 */
typedef struct FloatContext {
    FloatSideData left;         // Left floats
    FloatSideData right;        // Right floats
    
    // Container content area bounds (coordinates relative to container)
    float content_left;
    float content_right;
    float content_top;
    float content_bottom;
    
    ViewBlock* container;       // Containing block establishing this context
} FloatContext;

/**
 * FloatAvailableSpace - Result of space query at a given Y coordinate
 */
typedef struct FloatAvailableSpace {
    float left;                 // Left edge of available space
    float right;                // Right edge of available space
} FloatAvailableSpace;

// Core positioning functions
void layout_relative_positioned(LayoutContext* lycon, ViewBlock* block);

// Utility functions
bool element_has_positioning(ViewBlock* block);
ViewBlock* find_containing_block(ViewBlock* element, CssEnum position_type);

// ============================================================================
// Float Context API
// ============================================================================

// Float context lifecycle
FloatContext* float_context_create(ViewBlock* container);
void float_context_destroy(FloatContext* ctx);

// Float management
void float_context_add_float(FloatContext* ctx, ViewBlock* element);
void float_context_position_float(FloatContext* ctx, ViewBlock* element, float current_y);

// Space queries
FloatAvailableSpace float_space_at_y(FloatContext* ctx, float y, float line_height);
float float_find_y_for_width(FloatContext* ctx, float required_width, float start_y);
float float_find_clear_position(FloatContext* ctx, CssEnum clear_value);

// Layout integration
void layout_float_element(LayoutContext* lycon, ViewBlock* block);
void adjust_line_for_floats(LayoutContext* lycon, FloatContext* float_ctx);
void layout_clear_element(LayoutContext* lycon, ViewBlock* block);

// Float context lifecycle management (integration with LayoutContext)
void init_float_context_for_block(LayoutContext* lycon, ViewBlock* block);
void cleanup_float_context(LayoutContext* lycon);
FloatContext* get_current_float_context(LayoutContext* lycon);

// Legacy compatibility (to be removed after full migration)
FloatContext* create_float_context(ViewBlock* container);
void add_float_to_context(FloatContext* ctx, ViewBlock* element, CssEnum float_side);
void position_float_element(FloatContext* ctx, ViewBlock* element, CssEnum float_side);
int find_clear_position(FloatContext* ctx, CssEnum clear_value);
bool float_intersects_line(FloatBox* float_box, int line_top, int line_bottom);
