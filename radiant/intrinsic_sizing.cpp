/**
 * Unified Intrinsic Sizing Implementation for Radiant Layout Engine
 *
 * This is the SINGLE SOURCE OF TRUTH for min-content and max-content calculations.
 * Table, flex, and grid layouts should ALL use these functions.
 */

#include "intrinsic_sizing.hpp"
#include "layout_flex.hpp"  // For FlexDirection enum
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

        result.min_content = longest_word;
        result.max_content = total_width;
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

            // Use the same space_width as layout_text.cpp for consistency
            // This is pre-calculated in font.cpp using FT_Load_Char with FT_LOAD_NO_HINTING
            float space_width = 4.0f;  // Default fallback
            if (lycon->font.style && lycon->font.style->space_width > 0) {
                space_width = lycon->font.style->space_width;
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
        // IMPORTANT: Use FT_LOAD_NO_HINTING to match layout_text.cpp and font.cpp behavior
        // Different load flags give different advance widths, causing measurement/layout mismatch
        FT_Int32 load_flags = FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING;
        if (FT_Load_Glyph(lycon->font.ft_face, glyph_index, load_flags) == 0) {
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

    result.min_content = longest_word;   // Keep float precision
    result.max_content = total_width;    // Keep float precision

    log_debug("measure_text_intrinsic_widths: len=%zu, min=%.2f, max=%.2f",
              length, result.min_content, result.max_content);

    return result;
}

// ============================================================================
// Element Measurement (Recursive)
// ============================================================================

// Helper to check if an element has inline-level display from CSS
static bool is_inline_level_element(DomElement* element) {
    if (!element) return false;

    // First check if the view has been styled
    ViewBlock* view = (ViewBlock*)element;
    if (view->display.outer == CSS_VALUE_INLINE) {
        return true;
    }

    // Fall back to checking specified CSS style
    if (element->specified_style) {
        CssDeclaration* display_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_DISPLAY);
        if (display_decl && display_decl->value &&
            display_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
            // Check for inline display values
            CssEnum display_value = display_decl->value->data.keyword;
            if (display_value == CSS_VALUE_INLINE ||
                display_value == CSS_VALUE_INLINE_BLOCK ||
                display_value == CSS_VALUE_INLINE_FLEX ||
                display_value == CSS_VALUE_INLINE_GRID ||
                display_value == CSS_VALUE_INLINE_TABLE) {
                return true;
            }
        }
    }
    return false;
}

IntrinsicSizes measure_element_intrinsic_widths(LayoutContext* lycon, DomElement* element) {
    IntrinsicSizes sizes = {0, 0};

    if (!element) return sizes;

    // Resolve CSS styles for this element if not already resolved
    // This is needed during intrinsic measurement to get correct display property
    if (!element->styles_resolved && element->specified_style) {
        // Set measuring flag to prevent marking as permanently resolved
        bool was_measuring = lycon->is_measuring;
        lycon->is_measuring = true;

        // Resolve CSS display property at minimum
        // We don't need full style resolution, just display property
        CssDeclaration* display_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_DISPLAY);
        if (display_decl && display_decl->value &&
            display_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
            ViewBlock* view = (ViewBlock*)element;
            CssEnum display_value = display_decl->value->data.keyword;
            // Set outer display
            if (display_value == CSS_VALUE_INLINE) {
                view->display.outer = CSS_VALUE_INLINE;
            } else if (display_value == CSS_VALUE_INLINE_BLOCK) {
                view->display.outer = CSS_VALUE_INLINE_BLOCK;
            } else if (display_value == CSS_VALUE_BLOCK) {
                view->display.outer = CSS_VALUE_BLOCK;
            } else if (display_value == CSS_VALUE_LIST_ITEM) {
                view->display.outer = CSS_VALUE_LIST_ITEM;
            }
        }

        lycon->is_measuring = was_measuring;
    }

    log_debug("measure_element_intrinsic: tag=%s, outer=%d", element->node_name(),
              ((ViewBlock*)element)->display.outer);

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
                log_debug("  -> explicit width: %d", explicit_width);
                return sizes;
            }
        }
    }

    // Track inline-level content separately
    int inline_min_sum = 0;  // Sum of min-content widths for inline children
    int inline_max_sum = 0;  // Sum of max-content widths for inline children
    bool has_inline_content = false;

    // Check if this element is a flex container (text content doesn't contribute to intrinsic size)
    bool is_flex_container = false;
    ViewBlock* view_block = (ViewBlock*)element;
    if (view_block->display.inner == CSS_VALUE_FLEX) {
        is_flex_container = true;
    }

    // Measure children recursively
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        IntrinsicSizes child_sizes = {0, 0};
        bool is_inline = false;

        if (child->is_text()) {
            // Skip text nodes for flex containers - they become anonymous flex items
            // but don't contribute to the container's intrinsic min-content width
            if (is_flex_container) {
                continue;
            }

            const char* text = (const char*)child->text_data();
            if (text) {
                // Skip whitespace-only text nodes
                bool is_whitespace_only = true;
                for (const char* p = text; *p; p++) {
                    if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                        is_whitespace_only = false;
                        break;
                    }
                }

                if (is_whitespace_only) {
                    continue;
                }

                TextIntrinsicWidths text_widths = measure_text_intrinsic_widths(
                    lycon, text, strlen(text));
                child_sizes.min_content = text_widths.min_content;
                child_sizes.max_content = text_widths.max_content;
            }
            is_inline = true;  // Text nodes are always inline
        } else if (child->is_element()) {
            DomElement* child_elem = child->as_element();
            child_sizes = measure_element_intrinsic_widths(lycon, child_elem);
            is_inline = is_inline_level_element(child_elem);

            log_debug("  child %s: min=%d, max=%d, is_inline=%d",
                      child_elem->node_name(), child_sizes.min_content, child_sizes.max_content, is_inline);

            // For inline elements, also add horizontal margins
            if (is_inline) {
                ViewBlock* child_view = (ViewBlock*)child_elem;
                if (child_view->bound) {
                    if (child_view->bound->margin.left_type != CSS_VALUE_AUTO &&
                        child_view->bound->margin.left >= 0) {
                        child_sizes.max_content += (int)child_view->bound->margin.left;
                    }
                    if (child_view->bound->margin.right_type != CSS_VALUE_AUTO &&
                        child_view->bound->margin.right >= 0) {
                        child_sizes.max_content += (int)child_view->bound->margin.right;
                    }
                }
            }
        }

        if (is_inline) {
            // For inline content, sum widths for max-content (no wrapping)
            // and take max of min-content (can wrap between items)
            has_inline_content = true;
            inline_max_sum += child_sizes.max_content;
            inline_min_sum = max(inline_min_sum, child_sizes.min_content);
        } else {
            // For block-level children: take max of each
            sizes.min_content = max(sizes.min_content, child_sizes.min_content);
            sizes.max_content = max(sizes.max_content, child_sizes.max_content);
        }
    }

    // Merge inline content measurements
    if (has_inline_content) {
        sizes.min_content = max(sizes.min_content, inline_min_sum);
        sizes.max_content = max(sizes.max_content, inline_max_sum);
        log_debug("  inline_max_sum=%d, inline_min_sum=%d", inline_max_sum, inline_min_sum);
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

float calculate_min_content_width(LayoutContext* lycon, DomNode* node) {
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

float calculate_max_content_width(LayoutContext* lycon, DomNode* node) {
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

float calculate_min_content_height(LayoutContext* lycon, DomNode* node, float width) {
    // For block containers, min-content height == max-content height
    // (CSS Sizing Level 3: https://www.w3.org/TR/css-sizing-3/#min-content-block-size)
    return calculate_max_content_height(lycon, node, width);
}

float calculate_max_content_height(LayoutContext* lycon, DomNode* node, float width) {
    if (!node) return 0;

    // For text nodes, estimate based on line height
    if (node->is_text()) {
        // Simple estimation: one line height
        float line_height = 20.0f;  // Default
        if (lycon->font.style && lycon->font.style->font_size > 0) {
            line_height = lycon->font.style->font_size * 1.2f;  // Typical line-height
        }
        return line_height;
    }

    // For elements, we'd need to do a full layout pass
    // For now, use a simplified estimation
    DomElement* element = node->as_element();
    if (!element) return 0;

    float height = 0;
    ViewBlock* view = (ViewBlock*)element;

    // Check if this is a grid container with column flow
    // In column flow, items are placed in columns (side-by-side), so height = max(child_heights)
    bool is_grid_column_flow = false;
    if (view->display.inner == CSS_VALUE_GRID) {
        // Check if grid-auto-flow is column
        if (view->embed && view->embed->grid) {
            if (view->embed->grid->grid_auto_flow == CSS_VALUE_COLUMN) {
                is_grid_column_flow = true;
            }
        }
    }

    // Also check for flex containers with row direction (items side-by-side)
    bool is_flex_row = false;
    if (view->display.inner == CSS_VALUE_FLEX) {
        // Default flex direction is row
        if (view->embed && view->embed->flex) {
            if (view->embed->flex->direction == DIR_ROW ||
                view->embed->flex->direction == DIR_ROW_REVERSE) {
                is_flex_row = true;
            }
        } else {
            // No explicit flex props - default is row
            is_flex_row = true;
        }
    }

    // Calculate children's heights
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        float child_height = calculate_max_content_height(lycon, child, width);

        if (is_grid_column_flow || is_flex_row) {
            // Items are laid out horizontally - take max height
            height = fmax(height, child_height);
        } else {
            // Items are stacked vertically - sum heights
            height += child_height;
        }
    }

    // Add padding and border
    float pad_top = 0, pad_bottom = 0;
    float border_top = 0, border_bottom = 0;

    if (view->bound) {
        if (view->bound->padding.top >= 0) pad_top = view->bound->padding.top;
        if (view->bound->padding.bottom >= 0) pad_bottom = view->bound->padding.bottom;
        if (view->bound->border) {
            border_top = view->bound->border->width.top;
            border_bottom = view->bound->border->width.bottom;
        }
    } else if (element->specified_style) {
        // Fallback: read padding from CSS styles if bound hasn't been allocated yet
        CssDeclaration* pad_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING);
        if (pad_decl && pad_decl->value && pad_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            // Single padding value (shorthand)
            float pad = resolve_length_value(lycon, CSS_PROPERTY_PADDING, pad_decl->value);
            pad_top = pad_bottom = pad;
        } else {
            // Check individual padding properties
            CssDeclaration* pt = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING_TOP);
            if (pt && pt->value && pt->value->type == CSS_VALUE_TYPE_LENGTH) {
                pad_top = resolve_length_value(lycon, CSS_PROPERTY_PADDING_TOP, pt->value);
            }
            CssDeclaration* pb = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING_BOTTOM);
            if (pb && pb->value && pb->value->type == CSS_VALUE_TYPE_LENGTH) {
                pad_bottom = resolve_length_value(lycon, CSS_PROPERTY_PADDING_BOTTOM, pb->value);
            }
        }
    }

    height += pad_top + pad_bottom + border_top + border_bottom;

    return height;
}

float calculate_fit_content_width(LayoutContext* lycon, DomNode* node, float available_width) {
    float min_content = calculate_min_content_width(lycon, node);
    float max_content = calculate_max_content_width(lycon, node);

    // fit-content = clamp(min-content, available, max-content)
    // = min(max-content, max(min-content, available))
    return fminf(max_content, fmaxf(min_content, available_width));
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
