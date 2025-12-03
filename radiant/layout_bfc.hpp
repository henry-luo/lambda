#pragma once
/**
 * Block Formatting Context (BFC) for CSS Float Layout
 * 
 * Implements CSS 2.2 Section 9.5.1 Float Positioning Rules.
 * A BFC is established by:
 * - Root element
 * - Floats (float != none)
 * - Absolutely positioned elements (position: absolute/fixed)
 * - Block containers with overflow != visible
 * - display: flow-root, inline-block, table-cell, table-caption
 * - Flex items, grid items
 */

#include "view.hpp"
#include <cmath>

// Forward declarations
struct LayoutContext;
struct BlockFormattingContext;

/**
 * FloatBox - Represents a positioned floating element
 * Stores both the element reference and its margin box bounds
 * for efficient space queries.
 */
struct BfcFloatBox {
    ViewBlock* element;         // The floating element
    
    // Margin box bounds (relative to BFC origin)
    float margin_top;
    float margin_bottom;
    float margin_left;
    float margin_right;
    
    CssEnum float_side;         // CSS_VALUE_LEFT or CSS_VALUE_RIGHT
    
    // Initialize from a positioned float element
    void init_from_element(ViewBlock* elem, float bfc_origin_x, float bfc_origin_y);
};

/**
 * FloatAvailableSpace - Result of space query at a given Y coordinate
 */
struct BfcAvailableSpace {
    float left;                 // Left edge of available space (relative to BFC)
    float right;                // Right edge of available space (relative to BFC)
    
    float width() const { return right - left; }
};

/**
 * BlockFormattingContext - Manages float layout within a formatting context
 * 
 * A BFC tracks all floats within its scope and provides efficient queries
 * for available space at any Y coordinate.
 */
struct BlockFormattingContext {
    ViewBlock* establishing_element;   // Element that created this BFC
    BlockFormattingContext* parent_bfc; // Parent BFC (for nested contexts)
    
    // Float arrays (simple linked lists via next pointer in each box)
    BfcFloatBox* left_floats_head;
    BfcFloatBox* left_floats_tail;
    int left_float_count;
    
    BfcFloatBox* right_floats_head;
    BfcFloatBox* right_floats_tail;
    int right_float_count;
    
    // BFC coordinate origin (absolute position of content area top-left)
    float origin_x;
    float origin_y;
    
    // Content area bounds (relative to origin)
    float content_left;         // Usually 0
    float content_right;        // Width of content area
    float content_top;          // Usually 0
    
    // Optimization: track lowest float bottom
    float lowest_float_bottom;
    
    // Memory pool for float boxes (avoid malloc per float)
    Pool* pool;
    
    // =====================================================
    // Lifecycle
    // =====================================================
    
    /**
     * Initialize a BFC for an establishing element
     */
    void init(ViewBlock* element, Pool* pool);
    
    /**
     * Reset BFC state (for reflow)
     */
    void reset();
    
    // =====================================================
    // Float Management
    // =====================================================
    
    /**
     * Add a float to this BFC after it has been positioned
     * @param element The positioned float element
     */
    void add_float(ViewBlock* element);
    
    /**
     * Position and add a float at the current layout position
     * Implements CSS 2.2 Section 9.5.1 Rules 1-8
     * 
     * @param element The float element (must have width/height set)
     * @param current_line_y Current line Y position (relative to BFC)
     */
    void position_float(ViewBlock* element, float current_line_y);
    
    // =====================================================
    // Space Queries
    // =====================================================
    
    /**
     * Get available horizontal space at a given Y coordinate
     * @param y Y coordinate relative to BFC origin
     * @param height Height of the line/element being placed
     * @return Available space bounds
     */
    BfcAvailableSpace space_at_y(float y, float height) const;
    
    /**
     * Find the lowest Y where a given width is available
     * Used for placing floats that don't fit at current Y
     * 
     * @param required_width Width needed
     * @param min_y Minimum Y to search from
     * @return Y coordinate where width is available
     */
    float find_y_for_width(float required_width, float min_y) const;
    
    /**
     * Find Y position to clear floats
     * @param clear_type CSS_VALUE_LEFT, CSS_VALUE_RIGHT, or CSS_VALUE_BOTH
     * @return Y coordinate below all relevant floats
     */
    float find_clear_y(CssEnum clear_type) const;
    
    /**
     * Find the next Y where a float ends (margin_bottom)
     * Used for stepping through float stack
     */
    float find_next_float_bottom(float after_y) const;
    
    // =====================================================
    // Utility
    // =====================================================
    
    /**
     * Check if a float at (x, y) with given dimensions intersects existing floats
     */
    bool would_overlap_floats(float x, float y, float width, float height, CssEnum side) const;
    
    /**
     * Convert local block coordinates to BFC coordinates
     */
    float to_bfc_x(float local_x, ViewBlock* block) const;
    float to_bfc_y(float local_y, ViewBlock* block) const;
    
    /**
     * Convert BFC coordinates to local block coordinates
     */
    float from_bfc_x(float bfc_x, ViewBlock* block) const;
    float from_bfc_y(float bfc_y, ViewBlock* block) const;
    
private:
    /**
     * Allocate a FloatBox from the pool
     */
    BfcFloatBox* alloc_float_box();
    
    /**
     * Check if a float box intersects a Y range
     */
    static bool float_intersects_y(const BfcFloatBox* box, float y_top, float y_bottom);
};

// =====================================================
// Helper Functions
// =====================================================

/**
 * Check if an element establishes a new BFC
 */
bool element_establishes_bfc(ViewBlock* block);

/**
 * Create and initialize a BFC for an element
 * Returns nullptr if the element doesn't establish a BFC
 */
BlockFormattingContext* create_bfc_if_needed(ViewBlock* block, Pool* pool, 
                                              BlockFormattingContext* parent_bfc);

/**
 * Get the BFC that contains a given block
 * Walks up the parent chain to find the nearest BFC
 */
BlockFormattingContext* find_containing_bfc(LayoutContext* lycon);

/**
 * Calculate the offset of a block's content area from the BFC origin
 */
void calculate_block_offset_in_bfc(ViewBlock* block, BlockFormattingContext* bfc,
                                    float* offset_x, float* offset_y);
