/**
 * Unified Intrinsic Sizing API for Radiant Layout Engine
 *
 * This module provides a single source of truth for min-content and max-content
 * width/height calculations, used by table, flex, and grid layouts.
 *
 * Based on CSS Intrinsic & Extrinsic Sizing Module Level 3:
 * https://www.w3.org/TR/css-sizing-3/
 */

#pragma once
#include "layout.hpp"
#include "view.hpp"
#include "available_space.hpp"

// ============================================================================
// Intrinsic Size Cache
// ============================================================================

/**
 * Per-element cache for intrinsic size calculations.
 * Height values are cached per-width since height depends on available width
 * (due to text wrapping).
 */
struct IntrinsicSizeCache {
    float min_content_width;   // -1 means not computed
    float max_content_width;   // -1 means not computed

    // Height depends on width, so we cache per-width
    // Simple fixed-size cache for common widths
    struct HeightCacheEntry {
        float width;
        float min_height;
        float max_height;
        bool valid;
    };
    static constexpr int HEIGHT_CACHE_SIZE = 4;
    HeightCacheEntry height_cache[HEIGHT_CACHE_SIZE];

    IntrinsicSizeCache() {
        reset();
    }

    void reset() {
        min_content_width = -1;
        max_content_width = -1;
        for (int i = 0; i < HEIGHT_CACHE_SIZE; i++) {
            height_cache[i].valid = false;
        }
    }

    float get_min_height_for_width(float width) const {
        for (int i = 0; i < HEIGHT_CACHE_SIZE; i++) {
            if (height_cache[i].valid && height_cache[i].width == width) {
                return height_cache[i].min_height;
            }
        }
        return -1;
    }

    float get_max_height_for_width(float width) const {
        for (int i = 0; i < HEIGHT_CACHE_SIZE; i++) {
            if (height_cache[i].valid && height_cache[i].width == width) {
                return height_cache[i].max_height;
            }
        }
        return -1;
    }

    void set_height_for_width(float width, float min_h, float max_h) {
        // Find empty slot or oldest entry to replace
        for (int i = 0; i < HEIGHT_CACHE_SIZE; i++) {
            if (!height_cache[i].valid) {
                height_cache[i].width = width;
                height_cache[i].min_height = min_h;
                height_cache[i].max_height = max_h;
                height_cache[i].valid = true;
                return;
            }
        }
        // Cache full, replace first entry (simple LRU approximation)
        height_cache[0].width = width;
        height_cache[0].min_height = min_h;
        height_cache[0].max_height = max_h;
    }
};

// ============================================================================
// Text Intrinsic Width Result
// ============================================================================

/**
 * Result of measuring text intrinsic widths.
 * Contains both min-content (longest word) and max-content (full line) widths.
 */
struct TextIntrinsicWidths {
    float min_content;  // Width of longest unbreakable segment (word)
    float max_content;  // Width of entire text without wrapping
};

// ============================================================================
// Unified Intrinsic Sizing API
// ============================================================================

/**
 * Calculate min-content width for any DOM node.
 *
 * Min-content width is the narrowest width a box can take without causing overflow.
 * - For text: width of the longest word
 * - For blocks: maximum of children's min-content widths
 * - For replaced elements (img): natural width
 *
 * @param lycon Layout context with font information
 * @param node DOM node to measure
 * @return Min-content width in pixels
 */
float calculate_min_content_width(LayoutContext* lycon, DomNode* node);

/**
 * Calculate max-content width for any DOM node.
 *
 * Max-content width is the natural width without any wrapping constraints.
 * - For text: full text width on single line
 * - For blocks: maximum of children's max-content widths
 * - For replaced elements (img): natural width
 *
 * @param lycon Layout context with font information
 * @param node DOM node to measure
 * @return Max-content width in pixels
 */
float calculate_max_content_width(LayoutContext* lycon, DomNode* node);

/**
 * Calculate min-content height for a DOM node at a specific width.
 *
 * For block containers and tables, min-content height equals max-content height.
 * Height depends on width due to text wrapping.
 *
 * @param lycon Layout context
 * @param node DOM node to measure
 * @param width Available width for layout
 * @return Min-content height in pixels
 */
float calculate_min_content_height(LayoutContext* lycon, DomNode* node, float width);

/**
 * Calculate max-content height for a DOM node at a specific width.
 *
 * Natural height after laying out content at the given width.
 *
 * @param lycon Layout context
 * @param node DOM node to measure
 * @param width Available width for layout
 * @return Max-content height in pixels
 */
float calculate_max_content_height(LayoutContext* lycon, DomNode* node, float width);

/**
 * Calculate fit-content width.
 *
 * fit-content = clamp(min-content, available, max-content)
 * This is the "shrink-to-fit" width used by floats, inline-blocks, etc.
 *
 * @param lycon Layout context
 * @param node DOM node to measure
 * @param available_width Available width constraint
 * @return Fit-content width in pixels
 */
float calculate_fit_content_width(LayoutContext* lycon, DomNode* node, float available_width);

// ============================================================================
// Low-Level Measurement Helpers
// ============================================================================

/**
 * Measure text intrinsic widths using FreeType font metrics.
 *
 * This is the core text measurement function used by all layout modes.
 * Uses accurate glyph metrics with kerning support.
 *
 * @param lycon Layout context with font information
 * @param text Text string to measure
 * @param length Length of text in bytes
 * @param text_transform CSS text-transform value (uppercase, lowercase, capitalize, none)
 * @return TextIntrinsicWidths with min and max content widths
 */
TextIntrinsicWidths measure_text_intrinsic_widths(LayoutContext* lycon,
                                                   const char* text,
                                                   size_t length,
                                                   CssEnum text_transform = CSS_VALUE_NONE);

/**
 * Measure element intrinsic widths recursively.
 *
 * Traverses child elements and computes aggregate intrinsic widths
 * based on the element's display type.
 *
 * @param lycon Layout context
 * @param element DOM element to measure
 * @return IntrinsicSizes with min and max content widths
 */
IntrinsicSizes measure_element_intrinsic_widths(LayoutContext* lycon, DomElement* element);

// ============================================================================
// Unified Intrinsic Sizing API (Section 4.2)
// ============================================================================

/**
 * IntrinsicSizesBidirectional - Complete intrinsic sizes for both axes
 *
 * This structure contains min-content and max-content sizes for both
 * width and height in a single measurement result.
 */
struct IntrinsicSizesBidirectional {
    float min_content_width;   // Width of longest unbreakable segment
    float max_content_width;   // Width without any wrapping
    float min_content_height;  // Height at min-content width
    float max_content_height;  // Height at max-content width
};

/**
 * Unified intrinsic size measurement entry point.
 *
 * This is the SINGLE ENTRY POINT for measuring intrinsic sizes,
 * used by flex, grid, and table layouts. It measures both width
 * and height intrinsic sizes in a single call.
 *
 * The available_space parameter provides cross-axis constraints:
 * - For width measurement: height constraint (if any)
 * - For height measurement: width constraint (affects text wrapping)
 *
 * @param lycon Layout context with font and style information
 * @param element Element to measure (ViewBlock/DomElement)
 * @param available_space Available space constraints for measurement (2D)
 * @return IntrinsicSizesBidirectional with all four intrinsic sizes
 */
IntrinsicSizesBidirectional measure_intrinsic_sizes(
    LayoutContext* lycon,
    ViewBlock* element,
    AvailableSpace available_space
);

/**
 * Helper to extract width-axis sizes from bidirectional result
 */
inline IntrinsicSizes intrinsic_sizes_width(IntrinsicSizesBidirectional sizes) {
    return {sizes.min_content_width, sizes.max_content_width};
}

/**
 * Helper to extract height-axis sizes from bidirectional result
 */
inline IntrinsicSizes intrinsic_sizes_height(IntrinsicSizesBidirectional sizes) {
    return {sizes.min_content_height, sizes.max_content_height};
}

/**
 * Helper to extract axis-specific sizes based on direction
 */
inline IntrinsicSizes intrinsic_sizes_for_axis(IntrinsicSizesBidirectional sizes, bool is_row_axis) {
    if (is_row_axis) {
        return {sizes.min_content_width, sizes.max_content_width};
    } else {
        return {sizes.min_content_height, sizes.max_content_height};
    }
}

// ============================================================================
// Table-Specific Intrinsic Sizing
// ============================================================================

/**
 * CellWidths - Intrinsic widths for table cell measurement
 *
 * This is used by table layout for measuring cell intrinsic widths.
 * Table cells need both min-content (MCW) and max-content (PCW) widths
 * for the table column width algorithm.
 */
struct CellIntrinsicWidths {
    float min_width;  // Minimum content width (MCW) - longest word
    float max_width;  // Preferred/maximum content width (PCW) - no wrapping
};

/**
 * Measure table cell intrinsic widths.
 *
 * This is a convenience wrapper around measure_intrinsic_sizes() for
 * table layout. It handles the table-specific measurement context
 * (infinite available width, proper font setup, etc.).
 *
 * @param lycon Layout context
 * @param cell Table cell to measure
 * @return CellIntrinsicWidths with min and max content widths
 */
CellIntrinsicWidths measure_table_cell_intrinsic_widths(
    LayoutContext* lycon,
    ViewBlock* cell
);

// ============================================================================
// Backward Compatibility Notes
// ============================================================================
//
// The following functions remain for backward compatibility:
// - calculate_min_content_width() - use measure_intrinsic_sizes().min_content_width
// - calculate_max_content_width() - use measure_intrinsic_sizes().max_content_width
// - calculate_min_content_height() - use measure_intrinsic_sizes().min_content_height
// - calculate_max_content_height() - use measure_intrinsic_sizes().max_content_height
// - measure_element_intrinsic_widths() - use measure_intrinsic_sizes() for width
//
// These will be gradually deprecated in favor of the unified API.
// ============================================================================
