/**
 * Unified Intrinsic Sizing Implementation for Radiant Layout Engine
 *
 * This is the SINGLE SOURCE OF TRUTH for min-content and max-content calculations.
 * Table, flex, and grid layouts should ALL use these functions.
 */

#include "intrinsic_sizing.hpp"
#include "../lib/log.h"
#include <cmath>
#include <cstring>

// ============================================================================
// Text Measurement (Core Implementation)
// ============================================================================

TextIntrinsicWidths measure_text_intrinsic_widths(LayoutContext* lycon,
                                                   const char* text,
                                                   size_t length) {
    TextIntrinsicWidths result = {0, 0};

    if (!text || length == 0) {
        return result;
    }

    // Check if we have a valid font face
    if (!lycon->font.ft_face) {
        // Fallback: rough estimate without font metrics
        // Use ~8px per character for max, find longest word for min
        float total_width = 0.0f;
        float current_word = 0.0f;
        float longest_word = 0.0f;

        for (size_t i = 0; i < length; i++) {
            unsigned char ch = (unsigned char)text[i];
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
                longest_word = fmax(longest_word, current_word);
                current_word = 0.0f;
                total_width += 4.0f;  // Space width estimate
            } else {
                current_word += 8.0f;
                total_width += 8.0f;
            }
        }
        longest_word = fmax(longest_word, current_word);

        result.min_content = (int)ceilf(longest_word);
        result.max_content = (int)roundf(total_width);
        return result;
    }

    float total_width = 0.0f;
    float current_word = 0.0f;
    float longest_word = 0.0f;

    FT_UInt prev_glyph = 0;
    bool has_kerning = FT_HAS_KERNING(lycon->font.ft_face);
    const unsigned char* str = (const unsigned char*)text;

    for (size_t i = 0; i < length; i++) {
        unsigned char ch = str[i];

        // Word boundary detection (whitespace breaks words)
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            longest_word = fmax(longest_word, current_word);
            current_word = 0.0f;
            prev_glyph = 0;

            // Add space width to total
            float space_width = 4.0f;  // Default fallback
            if (lycon->font.style && lycon->font.style->space_width > 0) {
                space_width = lycon->font.style->space_width;
            } else if (lycon->font.ft_face && lycon->font.ft_face->size) {
                // Estimate from max_advance
                space_width = lycon->font.ft_face->size->metrics.max_advance / 64.0f * 0.25f;
            }
            total_width += space_width;
            continue;
        }

        // Get glyph index
        FT_UInt glyph_index = FT_Get_Char_Index(lycon->font.ft_face, ch);
        if (!glyph_index) {
            // Unknown character, use fallback width
            current_word += 8.0f;
            total_width += 8.0f;
            prev_glyph = 0;
            continue;
        }

        // Apply kerning if available
        float kerning = 0.0f;
        if (has_kerning && prev_glyph) {
            FT_Vector kern;
            FT_Get_Kerning(lycon->font.ft_face, prev_glyph, glyph_index,
                          FT_KERNING_DEFAULT, &kern);
            kerning = kern.x / 64.0f;
        }

        // Load glyph and get advance width
        if (FT_Load_Glyph(lycon->font.ft_face, glyph_index, FT_LOAD_DEFAULT) == 0) {
            float advance = lycon->font.ft_face->glyph->advance.x / 64.0f + kerning;
            current_word += advance;
            total_width += advance;
        } else {
            // Fallback if glyph load fails
            current_word += 8.0f;
            total_width += 8.0f;
        }

        prev_glyph = glyph_index;
    }

    // Don't forget the last word
    longest_word = fmax(longest_word, current_word);

    result.min_content = (int)ceilf(longest_word);   // Round up for min to prevent overflow
    result.max_content = (int)roundf(total_width);

    log_debug("measure_text_intrinsic_widths: len=%zu, min=%d, max=%d",
              length, result.min_content, result.max_content);

    return result;
}

// ============================================================================
// Element Measurement (Recursive)
// ============================================================================

IntrinsicSizes measure_element_intrinsic_widths(LayoutContext* lycon, DomElement* element) {
    IntrinsicSizes sizes = {0, 0};

    if (!element) return sizes;

    // Check for explicit CSS width first
    if (element->specified_style) {
        CssDeclaration* width_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_WIDTH);
        if (width_decl && width_decl->value &&
            width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            int explicit_width = (int)resolve_length_value(lycon, CSS_PROPERTY_WIDTH,
                                                            width_decl->value);
            if (explicit_width > 0) {
                sizes.min_content = explicit_width;
                sizes.max_content = explicit_width;
                return sizes;
            }
        }
    }

    // Measure children recursively
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        IntrinsicSizes child_sizes = {0, 0};

        if (child->is_text()) {
            const char* text = (const char*)child->text_data();
            if (text) {
                TextIntrinsicWidths text_widths = measure_text_intrinsic_widths(
                    lycon, text, strlen(text));
                child_sizes.min_content = text_widths.min_content;
                child_sizes.max_content = text_widths.max_content;
            }
        } else if (child->is_element()) {
            child_sizes = measure_element_intrinsic_widths(lycon, child->as_element());
        }

        // For block-level children: min = max of children's min, max = max of children's max
        // (This is simplified; inline children would sum max-content)
        sizes.min_content = max(sizes.min_content, child_sizes.min_content);
        sizes.max_content = max(sizes.max_content, child_sizes.max_content);
    }

    // Add padding and border
    ViewBlock* view = (ViewBlock*)element;
    if (view->bound) {
        int horiz_padding = 0;
        if (view->bound->padding.left >= 0) horiz_padding += (int)view->bound->padding.left;
        if (view->bound->padding.right >= 0) horiz_padding += (int)view->bound->padding.right;
        sizes.min_content += horiz_padding;
        sizes.max_content += horiz_padding;

        if (view->bound->border) {
            int horiz_border = (int)(view->bound->border->width.left +
                                     view->bound->border->width.right);
            sizes.min_content += horiz_border;
            sizes.max_content += horiz_border;
        }
    }

    return sizes;
}

// ============================================================================
// Main API Implementation
// ============================================================================

int calculate_min_content_width(LayoutContext* lycon, DomNode* node) {
    if (!node) return 0;

    // Handle text nodes directly
    if (node->is_text()) {
        const char* text = (const char*)node->text_data();
        if (text) {
            TextIntrinsicWidths widths = measure_text_intrinsic_widths(
                lycon, text, strlen(text));
            return widths.min_content;
        }
        return 0;
    }

    // Handle element nodes
    DomElement* element = node->as_element();
    if (!element) return 0;

    IntrinsicSizes sizes = measure_element_intrinsic_widths(lycon, element);
    return sizes.min_content;
}

int calculate_max_content_width(LayoutContext* lycon, DomNode* node) {
    if (!node) return 0;

    // Handle text nodes directly
    if (node->is_text()) {
        const char* text = (const char*)node->text_data();
        if (text) {
            TextIntrinsicWidths widths = measure_text_intrinsic_widths(
                lycon, text, strlen(text));
            return widths.max_content;
        }
        return 0;
    }

    // Handle element nodes
    DomElement* element = node->as_element();
    if (!element) return 0;

    IntrinsicSizes sizes = measure_element_intrinsic_widths(lycon, element);
    return sizes.max_content;
}

int calculate_min_content_height(LayoutContext* lycon, DomNode* node, int width) {
    // For block containers, min-content height == max-content height
    // (CSS Sizing Level 3: https://www.w3.org/TR/css-sizing-3/#min-content-block-size)
    return calculate_max_content_height(lycon, node, width);
}

int calculate_max_content_height(LayoutContext* lycon, DomNode* node, int width) {
    if (!node) return 0;

    // For text nodes, estimate based on line height
    if (node->is_text()) {
        // Simple estimation: one line height
        float line_height = 20.0f;  // Default
        if (lycon->font.style && lycon->font.style->font_size > 0) {
            line_height = lycon->font.style->font_size * 1.2f;  // Typical line-height
        }
        return (int)ceilf(line_height);
    }

    // For elements, we'd need to do a full layout pass
    // For now, use a simplified estimation
    DomElement* element = node->as_element();
    if (!element) return 0;

    int height = 0;

    // Sum heights of block-level children, or take max for inline
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        int child_height = calculate_max_content_height(lycon, child, width);
        height += child_height;  // Simplified: assume all block-level
    }

    // Add padding
    ViewBlock* view = (ViewBlock*)element;
    if (view->bound) {
        if (view->bound->padding.top >= 0) height += (int)view->bound->padding.top;
        if (view->bound->padding.bottom >= 0) height += (int)view->bound->padding.bottom;

        if (view->bound->border) {
            height += (int)(view->bound->border->width.top + view->bound->border->width.bottom);
        }
    }

    return height;
}

int calculate_fit_content_width(LayoutContext* lycon, DomNode* node, int available_width) {
    int min_content = calculate_min_content_width(lycon, node);
    int max_content = calculate_max_content_width(lycon, node);

    // fit-content = clamp(min-content, available, max-content)
    // = min(max-content, max(min-content, available))
    return min(max_content, max(min_content, available_width));
}

// ============================================================================
// Table Cell Intrinsic Width Measurement
// Note: This wrapper is designed for table layout integration.
// The actual implementation uses the table's existing measure_cell_intrinsic_width
// and measure_cell_minimum_width functions until full integration is complete.
//
// TODO: Refactor layout_table.cpp to use the unified text measurement functions
// (measure_text_intrinsic_widths) from this module for consistency.
// ============================================================================
