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
 * @return TextIntrinsicWidths with min and max content widths
 */
TextIntrinsicWidths measure_text_intrinsic_widths(LayoutContext* lycon,
                                                   const char* text,
                                                   size_t length);

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
// Future Integration Points
// ============================================================================
//
// The following areas are candidates for future unification:
//
// 1. Table Layout (layout_table.cpp):
//    - measure_cell_intrinsic_width() - can use measure_text_intrinsic_widths()
//    - measure_cell_minimum_width() - can use min-content calculation
//
// 2. Flex Layout (layout_flex_measurement.cpp):
//    - calculate_item_intrinsic_sizes() - can delegate to this module
//    - measure_text_content_width() - should share text measurement logic
//
// 3. Grid Layout (grid_utils.cpp):
//    - Grid item sizing should use the same intrinsic size calculations
// ============================================================================
