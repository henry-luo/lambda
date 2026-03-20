/**
 * Unified Intrinsic Sizing Implementation for Radiant Layout Engine
 *
 * This is the SINGLE SOURCE OF TRUTH for min-content and max-content calculations.
 * Table, flex, and grid layouts should ALL use these functions.
 */

#include "intrinsic_sizing.hpp"
#include "layout_flex.hpp"  // For FlexDirection enum
#include "grid.hpp"         // For GridTrackList
#include "../lib/font/font.h"
#include "../lib/strbuf.h"
#include "../lib/log.h"
// str.h included via view.hpp
#include <cmath>
#include <cstring>

// ============================================================================
// Helper: Read border width from CSS specified style
// ============================================================================
// Parses the CSS border shorthand to extract border width, matching the logic
// in resolve_css_style.cpp. Handles: explicit length, keyword (thin/medium/thick),
// and the CSS default of medium (3px) when style is visible but width unspecified.
static float get_border_width_from_css(LayoutContext* lycon, DomElement* element) {
    if (!element || !element->specified_style) return 0;
    CssDeclaration* border_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_BORDER);
    if (!border_decl || !border_decl->value) return 0;

    float bw = -1.0f;
    bool has_visible_style = false;
    CssValue* val = border_decl->value;

    auto parse_keyword = [&](CssEnum kw) {
        if (kw == CSS_VALUE_THIN) bw = 1.0f;
        else if (kw == CSS_VALUE_MEDIUM) bw = 3.0f;
        else if (kw == CSS_VALUE_THICK) bw = 5.0f;
        else if (kw == CSS_VALUE_SOLID || kw == CSS_VALUE_DASHED ||
                 kw == CSS_VALUE_DOTTED || kw == CSS_VALUE_DOUBLE ||
                 kw == CSS_VALUE_GROOVE || kw == CSS_VALUE_RIDGE ||
                 kw == CSS_VALUE_INSET || kw == CSS_VALUE_OUTSET)
            has_visible_style = true;
    };

    if (val->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < val->data.list.count; i++) {
            CssValue* v = val->data.list.values[i];
            if (v->type == CSS_VALUE_TYPE_LENGTH || v->type == CSS_VALUE_TYPE_NUMBER)
                bw = resolve_length_value(lycon, CSS_PROPERTY_BORDER_WIDTH, v);
            else if (v->type == CSS_VALUE_TYPE_KEYWORD)
                parse_keyword(v->data.keyword);
        }
    } else if (val->type == CSS_VALUE_TYPE_LENGTH || val->type == CSS_VALUE_TYPE_NUMBER) {
        bw = resolve_length_value(lycon, CSS_PROPERTY_BORDER_WIDTH, val);
    } else if (val->type == CSS_VALUE_TYPE_KEYWORD) {
        parse_keyword(val->data.keyword);
    }

    // CSS spec: visible style with no explicit width defaults to medium (3px)
    if (has_visible_style && bw < 0) bw = 3.0f;
    return bw > 0 ? bw : 0;
}

// ============================================================================
// Forward declarations for functions defined in other files
// ============================================================================
CssEnum get_white_space_value(DomNode* node);

// ============================================================================
// Text Measurement (Core Implementation)
// ============================================================================

TextIntrinsicWidths measure_text_intrinsic_widths(LayoutContext* lycon,
                                                   const char* text,
                                                   size_t length,
                                                   CssEnum text_transform,
                                                   CssEnum font_variant) {
    TextIntrinsicWidths result = {0, 0};

    if (!text || length == 0) {
        return result;
    }

    // if font-size is 0, text has no size
    if (lycon->font.style && lycon->font.style->font_size <= 0.0f) {
        return result;  // min_content=0, max_content=0
    }

    // Check if we have a valid font face
    if (!lycon->font.font_handle) {
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
                current_word += 11.0f;  // Use 11.0 to match font fallback width
                total_width += 11.0f;
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

    uint32_t prev_glyph = 0;
    bool has_kerning = lycon->font.font_handle ? font_get_metrics(lycon->font.font_handle)->has_kerning : false;
    const unsigned char* str = (const unsigned char*)text;
    bool is_word_start = true;  // for text-transform: capitalize

    for (size_t i = 0; i < length; ) {
        unsigned char ch = str[i];

        // Check for zero-width space (U+200B) - UTF-8 encoding: 0xE2 0x80 0x8B
        // This is a break opportunity with no width
        if (ch == 0xE2 && i + 2 < length && str[i+1] == 0x80 && str[i+2] == 0x8B) {
            // Zero-width space is a break opportunity
            longest_word = fmax(longest_word, current_word);
            current_word = 0.0f;
            prev_glyph = 0;
            i += 3;  // Skip all bytes of zero-width space
            is_word_start = true;  // Next character starts a new word
            continue;
        }

        // Check for Zero Width No-Break Space / BOM (U+FEFF) - UTF-8: 0xEF 0xBB 0xBF
        // This has zero width and is NOT a break opportunity
        if (ch == 0xEF && i + 2 < length && str[i+1] == 0xBB && str[i+2] == 0xBF) {
            // Zero width, no break, just skip
            prev_glyph = 0;
            i += 3;
            // Don't set is_word_start - this is not a word boundary
            continue;
        }

        // Check for other zero-width characters (ZWNJ U+200C, ZWJ U+200D)
        // UTF-8: U+200C = 0xE2 0x80 0x8C, U+200D = 0xE2 0x80 0x8D
        if (ch == 0xE2 && i + 2 < length && str[i+1] == 0x80 && (str[i+2] == 0x8C || str[i+2] == 0x8D)) {
            // Zero width, no break, just skip
            prev_glyph = 0;
            i += 3;
            continue;
        }

        // Word boundary detection (whitespace breaks words)
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            longest_word = fmax(longest_word, current_word);
            current_word = 0.0f;

            // Get space glyph for kerning continuity (matching layout_text.cpp)
            uint32_t space_glyph = font_get_glyph_index(lycon->font.font_handle, ch);

            // Apply kerning between prev character and space (matching layout_text.cpp)
            if (has_kerning && prev_glyph && space_glyph) {
                total_width += font_get_kerning_by_index(lycon->font.font_handle, prev_glyph, space_glyph);
            }

            // Use the same space_width as layout_text.cpp for consistency
            // This is pre-calculated in font.cpp using FT_Load_Char with FT_LOAD_NO_HINTING
            float space_width = 4.0f;  // Default fallback
            if (lycon->font.style && lycon->font.style->space_width > 0) {
                space_width = lycon->font.style->space_width;
            }
            // Apply word-spacing to space characters (matching layout_text.cpp)
            if (lycon->font.style) {
                space_width += lycon->font.style->word_spacing;
            }
            // Apply letter-spacing to spaces as well (matching layout_text.cpp)
            // CSS 2.1 §16.4: letter-spacing is added after every character
            // Browsers include trailing letter-spacing in measured width
            if (lycon->font.style) {
                space_width += lycon->font.style->letter_spacing;
            }

            total_width += space_width;

            // Keep tracking glyph for kerning continuity (layout_text.cpp doesn't reset)
            prev_glyph = space_glyph;
            is_word_start = true;  // Next character starts a new word
            i++;
            continue;
        }

        // Decode UTF-8 codepoint
        uint32_t codepoint;
        int bytes = str_utf8_decode((const char*)&str[i], length - i, &codepoint);
        if (bytes <= 0) {
            // Invalid UTF-8, skip byte - use 11.0 to match font fallback
            current_word += 11.0f;
            total_width += 11.0f;
            prev_glyph = 0;
            i++;
            is_word_start = false;
            continue;
        }

        // Apply text-transform if specified
        if (text_transform != CSS_VALUE_NONE && text_transform != 0) {
            codepoint = apply_text_transform(codepoint, text_transform, is_word_start);
        }

        // CSS font-variant: small-caps — convert lowercase to uppercase glyphs
        // rendered at ~0.7× size (CSS 2.1 §15.8, matching layout_text.cpp)
        bool is_small_caps_lower = false;
        if (font_variant == CSS_VALUE_SMALL_CAPS) {
            uint32_t original = codepoint;
            codepoint = apply_text_transform(codepoint, CSS_VALUE_UPPERCASE, false);
            is_small_caps_lower = (codepoint != original);
        }

        is_word_start = false;  // No longer at word start after first character

        // Get glyph index for the (possibly transformed) codepoint
        uint32_t glyph_index = font_get_glyph_index(lycon->font.font_handle, codepoint);
        if (!glyph_index) {
            // Glyph not found in primary font - try font fallback via font_load_glyph
            // This ensures intrinsic sizing uses the same fallback fonts as layout_text.cpp
            FontStyleDesc _sd = font_style_desc_from_prop(lycon->font.style);
            LoadedGlyph* glyph = font_load_glyph(lycon->font.font_handle, &_sd, codepoint, false);
            if (glyph) {
                // Font is loaded at physical pixel size, so advance is in physical pixels
                // Divide by pixel_ratio to convert back to CSS pixels for layout
                float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0) ? lycon->ui_context->pixel_ratio : 1.0f;
                float advance = glyph->advance_x / pixel_ratio;
                // small-caps: scale advance for lowercase-origin characters
                if (is_small_caps_lower) advance *= 0.7f;
                // Apply letter-spacing
                // CSS 2.1 §16.4: letter-spacing after every character (including last)
                if (lycon->font.style) {
                    advance += lycon->font.style->letter_spacing;
                }
                current_word += advance;
                total_width += advance;
            } else {
                // No fallback found - use estimate
                current_word += 11.0f;
                total_width += 11.0f;
            }
            prev_glyph = 0;
            i += bytes;
            continue;
        }

        // Apply kerning if available (returns CSS pixels directly)
        float kerning = 0.0f;
        if (has_kerning && prev_glyph) {
            kerning = font_get_kerning_by_index(lycon->font.font_handle, prev_glyph, glyph_index);
        }

        // Get glyph advance via font module (returns CSS pixels directly)
        GlyphInfo ginfo = font_get_glyph(lycon->font.font_handle, codepoint);
        if (ginfo.id != 0) {
            // small-caps: scale advance for lowercase-origin characters
            float advance = ginfo.advance_x * (is_small_caps_lower ? 0.7f : 1.0f) + kerning;

            // Apply letter-spacing (CSS 2.1 §16.4: after every character including last)
            if (lycon->font.style) {
                advance += lycon->font.style->letter_spacing;
            }

            current_word += advance;
            total_width += advance;
        } else {
            // Fallback if glyph load fails - use 11.0 to match font fallback
            current_word += 11.0f;
            total_width += 11.0f;
        }

        prev_glyph = glyph_index;
        i += bytes;  // Advance by the number of bytes consumed
    }

    // Don't forget the last word
    longest_word = fmax(longest_word, current_word);

    result.min_content = longest_word;   // Keep float precision
    result.max_content = total_width;    // Keep float precision

    log_debug("measure_text_intrinsic_widths: len=%zu, min=%.2f, max=%.2f, text_transform=%d",
              length, result.min_content, result.max_content, (int)text_transform);

    return result;
}

// ============================================================================
// Text Height at Constrained Width (CSS Flexbox §9.4)
// ============================================================================
// Simulates line breaking at a given available width to compute the resulting
// text height. This is needed for hypothetical cross size determination when
// the item's main size (width) is smaller than the text's max-content width.
//
// Algorithm: Walk through text tracking break-unit (word/ZWSP-segment) widths,
// pack them into lines greedily at the available width, count lines.
float compute_text_height_at_width(LayoutContext* lycon,
                                    const char* text,
                                    size_t length,
                                    float available_width,
                                    float line_height,
                                    CssEnum text_transform,
                                    CssEnum font_variant) {
    if (!text || length == 0 || available_width <= 0 || line_height <= 0) {
        return line_height;  // at least one line
    }

    int line_count = 1;
    float current_line_width = 0;
    float current_word_width = 0;

    // Use font metrics if available, otherwise rough estimate
    bool has_font = lycon->font.font_handle != nullptr;
    bool has_kerning = has_font ? font_get_metrics(lycon->font.font_handle)->has_kerning : false;
    uint32_t prev_glyph = 0;
    bool is_word_start = true;

    const unsigned char* str = (const unsigned char*)text;

    for (size_t i = 0; i < length; ) {
        unsigned char ch = str[i];

        // Check for zero-width space (U+200B) - break opportunity
        if (ch == 0xE2 && i + 2 < length && str[i+1] == 0x80 && str[i+2] == 0x8B) {
            // end of break unit - try to fit on current line
            if (current_line_width > 0 && current_line_width + current_word_width > available_width) {
                line_count++;
                current_line_width = current_word_width;
            } else {
                current_line_width += current_word_width;
            }
            current_word_width = 0;
            prev_glyph = 0;
            is_word_start = true;
            i += 3;
            continue;
        }

        // Skip ZWNBSP (U+FEFF), ZWNJ (U+200C), ZWJ (U+200D) - zero width, no break
        if (ch == 0xEF && i + 2 < length && str[i+1] == 0xBB && str[i+2] == 0xBF) {
            prev_glyph = 0; i += 3; continue;
        }
        if (ch == 0xE2 && i + 2 < length && str[i+1] == 0x80 && (str[i+2] == 0x8C || str[i+2] == 0x8D)) {
            prev_glyph = 0; i += 3; continue;
        }

        // Regular space/whitespace - break opportunity
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            if (current_line_width > 0 && current_line_width + current_word_width > available_width) {
                line_count++;
                current_line_width = current_word_width;
            } else {
                current_line_width += current_word_width;
            }
            current_word_width = 0;
            // add space width
            float space_width = 4.0f;
            if (lycon->font.style && lycon->font.style->space_width > 0) {
                space_width = lycon->font.style->space_width;
            }
            if (lycon->font.style) {
                space_width += lycon->font.style->word_spacing;
            }
            current_line_width += space_width;
            prev_glyph = has_font ? font_get_glyph_index(lycon->font.font_handle, ch) : 0;
            is_word_start = true;
            i++;
            continue;
        }

        // Decode UTF-8 codepoint and measure glyph
        uint32_t codepoint;
        int bytes = str_utf8_decode((const char*)&str[i], length - i, &codepoint);
        if (bytes <= 0) {
            current_word_width += has_font ? 11.0f : 11.0f;
            prev_glyph = 0;
            i++;
            is_word_start = false;
            continue;
        }

        if (text_transform != CSS_VALUE_NONE && text_transform != 0) {
            codepoint = apply_text_transform(codepoint, text_transform, is_word_start);
        }

        // CSS font-variant: small-caps scaling (matching measure_text_intrinsic_widths)
        bool is_small_caps_lower = false;
        if (font_variant == CSS_VALUE_SMALL_CAPS) {
            uint32_t original = codepoint;
            codepoint = apply_text_transform(codepoint, CSS_VALUE_UPPERCASE, false);
            is_small_caps_lower = (codepoint != original);
        }

        is_word_start = false;

        float advance = 0;
        if (has_font) {
            uint32_t glyph_index = font_get_glyph_index(lycon->font.font_handle, codepoint);
            if (glyph_index) {
                float kerning = 0;
                if (has_kerning && prev_glyph) {
                    kerning = font_get_kerning_by_index(lycon->font.font_handle, prev_glyph, glyph_index);
                }
                GlyphInfo ginfo = font_get_glyph(lycon->font.font_handle, codepoint);
                float sc_scale = is_small_caps_lower ? 0.7f : 1.0f;
                advance = (ginfo.id != 0) ? ginfo.advance_x * sc_scale + kerning : 11.0f;
            } else {
                FontStyleDesc _sd = font_style_desc_from_prop(lycon->font.style);
                LoadedGlyph* glyph = font_load_glyph(lycon->font.font_handle, &_sd, codepoint, false);
                if (glyph) {
                    float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0) ? lycon->ui_context->pixel_ratio : 1.0f;
                    advance = glyph->advance_x / pixel_ratio;
                    if (is_small_caps_lower) advance *= 0.7f;
                } else {
                    advance = 11.0f;
                }
            }
            // letter-spacing after every character (CSS 2.1 §16.4, matching layout_text.cpp)
            if (lycon->font.style) {
                advance += lycon->font.style->letter_spacing;
            }
            prev_glyph = font_get_glyph_index(lycon->font.font_handle, codepoint);
        } else {
            advance = 11.0f;
        }

        current_word_width += advance;
        i += bytes;
    }

    // flush last word
    if (current_word_width > 0) {
        if (current_line_width > 0 && current_line_width + current_word_width > available_width) {
            line_count++;
        }
    }

    float result = line_count * line_height;
    log_debug("compute_text_height_at_width: available=%.1f, line_height=%.1f, lines=%d, height=%.1f",
              available_width, line_height, line_count, result);
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
            // Explicit block display
            if (display_value == CSS_VALUE_BLOCK ||
                display_value == CSS_VALUE_FLEX ||
                display_value == CSS_VALUE_GRID ||
                display_value == CSS_VALUE_TABLE) {
                return false;
            }
        }
    }

    // Fall back to HTML default display for common inline elements
    // These elements are inline by default in HTML
    const char* tag = element->node_name();
    if (tag) {
        if (strcmp(tag, "a") == 0 ||
            strcmp(tag, "span") == 0 ||
            strcmp(tag, "em") == 0 ||
            strcmp(tag, "strong") == 0 ||
            strcmp(tag, "b") == 0 ||
            strcmp(tag, "i") == 0 ||
            strcmp(tag, "u") == 0 ||
            strcmp(tag, "s") == 0 ||
            strcmp(tag, "small") == 0 ||
            strcmp(tag, "big") == 0 ||
            strcmp(tag, "sub") == 0 ||
            strcmp(tag, "sup") == 0 ||
            strcmp(tag, "code") == 0 ||
            strcmp(tag, "kbd") == 0 ||
            strcmp(tag, "samp") == 0 ||
            strcmp(tag, "var") == 0 ||
            strcmp(tag, "cite") == 0 ||
            strcmp(tag, "abbr") == 0 ||
            strcmp(tag, "acronym") == 0 ||
            strcmp(tag, "dfn") == 0 ||
            strcmp(tag, "q") == 0 ||
            strcmp(tag, "time") == 0 ||
            strcmp(tag, "mark") == 0 ||
            strcmp(tag, "label") == 0 ||
            strcmp(tag, "button") == 0 ||
            strcmp(tag, "input") == 0 ||
            strcmp(tag, "select") == 0 ||
            strcmp(tag, "textarea") == 0) {
            return true;
        }
    }
    return false;
}

// Helper to get text-transform property from an element, traversing parent chain
// since text-transform is an inherited property
CssEnum get_element_text_transform(DomElement* element) {
    // Walk up the DOM tree to find inherited text-transform value.
    // During intrinsic sizing, the view tree hasn't been created yet,
    // so we ONLY check specified_style on DOM elements (not ViewBlock).
    DomNode* node = element;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            if (elem->specified_style) {
                CssDeclaration* decl = style_tree_get_declaration(
                    elem->specified_style, CSS_PROPERTY_TEXT_TRANSFORM);
                if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                    CssEnum val = decl->value->data.keyword;
                    if (val != CSS_VALUE_INHERIT && val != CSS_VALUE__UNDEF) {
                        return val;
                    }
                }
            }
        }
        node = node->parent;
    }
    return CSS_VALUE_NONE;
}

// Helper to get font-variant property from an element, traversing parent chain
// since font-variant is an inherited property.
// Uses elem->font (available after CSS resolution) rather than specified_style.
CssEnum get_element_font_variant(DomElement* element) {
    DomNode* node = element;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            if (elem->font && elem->font->font_variant == CSS_VALUE_SMALL_CAPS) {
                return CSS_VALUE_SMALL_CAPS;
            }
        }
        node = node->parent;
    }
    return CSS_VALUE_NONE;
}

IntrinsicSizes measure_element_intrinsic_widths(LayoutContext* lycon, DomElement* element,
                                                bool content_only) {
    IntrinsicSizes sizes = {0, 0};

    if (!element) return sizes;

    // CRITICAL FIX: Set up font context for this element BEFORE measuring text children
    // This ensures text measurement uses the element's own font (e.g., monospace for <code>)
    // rather than inheriting from parent context.
    FontBox saved_font = lycon->font;  // Save parent font context
    bool font_changed = false;
    ViewBlock* view_block_font = (ViewBlock*)element;

    // First check if element has resolved font
    if (view_block_font->font && lycon->ui_context) {
        setup_font(lycon->ui_context, &lycon->font, view_block_font->font);
        font_changed = true;
    } else if (element->specified_style && lycon->ui_context && lycon->font.style) {
        // Element has CSS styles but font not yet resolved - check font-family and font-size
        // independently. Either property alone should trigger font setup.
        FontProp* temp_font_prop = alloc_font_prop(lycon);  // Allocates from pool
        bool need_font_setup = false;
        const char* css_family = NULL;

        // Check for font-family change
        CssDeclaration* font_family_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_FONT_FAMILY);

        if (font_family_decl && font_family_decl->value) {
            // Extract font-family from CSS value
            if (font_family_decl->value->type == CSS_VALUE_TYPE_STRING) {
                css_family = font_family_decl->value->data.string;
            } else if (font_family_decl->value->type == CSS_VALUE_TYPE_LIST) {
                // Multi-font stack - use first font
                int list_count = font_family_decl->value->data.list.count;
                CssValue** list_values = font_family_decl->value->data.list.values;
                if (list_count > 0 && list_values[0]) {
                    if (list_values[0]->type == CSS_VALUE_TYPE_STRING) {
                        css_family = list_values[0]->data.string;
                    } else if (list_values[0]->type == CSS_VALUE_TYPE_KEYWORD) {
                        // Generic font family keyword
                        CssEnum kw = list_values[0]->data.keyword;
                        if (kw == CSS_VALUE_MONOSPACE || kw == CSS_VALUE_UI_MONOSPACE) css_family = "monospace";
                        else if (kw == CSS_VALUE_SANS_SERIF || kw == CSS_VALUE_UI_SANS_SERIF) css_family = "sans-serif";
                        else if (kw == CSS_VALUE_SERIF || kw == CSS_VALUE_UI_SERIF) css_family = "serif";
                        else if (kw == CSS_VALUE_SYSTEM_UI) css_family = "system-ui";
                    }
                }
            }

            if (css_family && css_family != lycon->font.style->family) {
                temp_font_prop->family = (char*)css_family;
                need_font_setup = true;
            }
        }

        // Check for font-size change independently of font-family.
        // CSS 2.1 §10.2: font-size can be set on any element and affects text
        // measurement. During intrinsic sizing, we must use the element's own
        // font-size even when font-family is inherited unchanged.
        CssDeclaration* font_size_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_FONT_SIZE);
        if (font_size_decl && font_size_decl->value &&
            font_size_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            float resolved_size = resolve_length_value(lycon, CSS_PROPERTY_FONT_SIZE,
                                                       font_size_decl->value);
            if (resolved_size > 0 && fabs(resolved_size - lycon->font.style->font_size) > 0.1f) {
                temp_font_prop->font_size = resolved_size;
                need_font_setup = true;
            }
        }

        if (need_font_setup) {
            // Ensure font-family is set (use parent's if not changed)
            if (!temp_font_prop->family && lycon->font.style) {
                temp_font_prop->family = lycon->font.style->family;
            }
            setup_font(lycon->ui_context, &lycon->font, temp_font_prop);
            font_changed = true;
        }
    }

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

    // Detect table elements early (needed to skip explicit width shortcut for tables)
    bool is_table_display = false;
    {
        ViewBlock* tview = (ViewBlock*)element;
        if (tview->display.inner == CSS_VALUE_TABLE) is_table_display = true;
        if (!is_table_display && element->tag() == HTM_TAG_TABLE) is_table_display = true;
        if (!is_table_display && element->specified_style) {
            CssDeclaration* dd = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_DISPLAY);
            if (dd && dd->value && dd->value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum dv = dd->value->data.keyword;
                if (dv == CSS_VALUE_TABLE || dv == CSS_VALUE_INLINE_TABLE) is_table_display = true;
            }
        }
    }

    // Check for explicit CSS width first
    // When content_only is true, skip this early return to measure content-only min-content.
    // This is needed for CSS Flexbox §4.5 content_size_suggestion which represents the
    // min-content of the element's content, NOT the specified CSS width.
    // CSS Tables §4.1: Table content-box inline size is never smaller than its minimum
    // content inline size, so tables skip the explicit width shortcut and measure content.
    if (element->specified_style && !content_only && !is_table_display) {
        CssDeclaration* width_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_WIDTH);
        if (width_decl && width_decl->value &&
            (width_decl->value->type == CSS_VALUE_TYPE_LENGTH ||
             (width_decl->value->type == CSS_VALUE_TYPE_NUMBER &&
              width_decl->value->data.number.value == 0))) {
            int explicit_width = (int)resolve_length_value(lycon, CSS_PROPERTY_WIDTH,
                                                            width_decl->value);
            if (explicit_width >= 0) {
                // CSS width property sets content width by default (box-sizing: content-box)
                // We need to add padding and border for the total intrinsic width
                // Check box-sizing first
                bool is_border_box = false;
                ViewBlock* view_for_box = (ViewBlock*)element;
                if (view_for_box->blk && view_for_box->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
                    is_border_box = true;
                } else {
                    CssDeclaration* box_decl = style_tree_get_declaration(
                        element->specified_style, CSS_PROPERTY_BOX_SIZING);
                    if (box_decl && box_decl->value &&
                        box_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
                        box_decl->value->data.keyword == CSS_VALUE_BORDER_BOX) {
                        is_border_box = true;
                    }
                }

                if (!is_border_box) {
                    // Add padding and border to content width
                    float pad_left = 0, pad_right = 0, border_left = 0, border_right = 0;
                    // Read padding from CSS
                    CssDeclaration* pad_decl = style_tree_get_declaration(
                        element->specified_style, CSS_PROPERTY_PADDING);
                    if (pad_decl && pad_decl->value) {
                        if (pad_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                            float pad = resolve_length_value(lycon, CSS_PROPERTY_PADDING, pad_decl->value);
                            pad_left = pad_right = pad;
                        } else if (pad_decl->value->type == CSS_VALUE_TYPE_LIST) {
                            int cnt = pad_decl->value->data.list.count;
                            CssValue** vals = pad_decl->value->data.list.values;
                            if (cnt == 1) {
                                float p = resolve_length_value(lycon, CSS_PROPERTY_PADDING, vals[0]);
                                pad_left = pad_right = p;
                            } else if (cnt >= 2) {
                                float lr = resolve_length_value(lycon, CSS_PROPERTY_PADDING, vals[1]);
                                pad_left = pad_right = lr;
                                if (cnt >= 4) {
                                    pad_left = resolve_length_value(lycon, CSS_PROPERTY_PADDING, vals[3]);
                                }
                            }
                        }
                    }
                    // Read border: prefer resolved bound, fall back to CSS shorthand
                    ViewBlock* view_for_bdr = (ViewBlock*)element;
                    if (view_for_bdr->bound && view_for_bdr->bound->border) {
                        border_left = view_for_bdr->bound->border->width.left;
                        border_right = view_for_bdr->bound->border->width.right;
                    }
                    if (border_left == 0 && border_right == 0) {
                        float css_bw = get_border_width_from_css(lycon, element);
                        if (css_bw > 0) { border_left = css_bw; border_right = css_bw; }
                    }
                    explicit_width += (int)(pad_left + pad_right + border_left + border_right);
                    log_debug("  -> explicit width: %d (after adding padding=%.0f+%.0f, border=%.0f+%.0f)",
                              explicit_width, pad_left, pad_right, border_left, border_right);
                } else {
                    // border-box: floor at padding+border (content-box >= 0)
                    ViewBlock* view_for_pb = (ViewBlock*)element;
                    if (view_for_pb->bound) {
                        float pb_w = view_for_pb->bound->padding.left + view_for_pb->bound->padding.right;
                        if (view_for_pb->bound->border) {
                            pb_w += view_for_pb->bound->border->width.left + view_for_pb->bound->border->width.right;
                        }
                        if (explicit_width < (int)pb_w) {
                            log_debug("  -> explicit width: %d floored to %d (border-box, padding+border)", explicit_width, (int)pb_w);
                            explicit_width = (int)pb_w;
                        }
                    }
                    log_debug("  -> explicit width: %d (border-box)", explicit_width);
                }

                sizes.min_content = explicit_width;
                sizes.max_content = explicit_width;
                return sizes;
            }
        }
    }

    // Check for aspect-ratio with explicit height or resolvable percentage height
    // If element has height and aspect-ratio, width = height * aspect-ratio
    ViewBlock* view_block_for_aspect = (ViewBlock*)element;
    float aspect_ratio = 0;

    // First check fi for resolved aspect-ratio
    if (view_block_for_aspect->fi && view_block_for_aspect->fi->aspect_ratio > 0) {
        aspect_ratio = view_block_for_aspect->fi->aspect_ratio;
    }
    // If not in fi, check specified_style directly
    else if (element->specified_style) {
        CssDeclaration* aspect_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_ASPECT_RATIO);
        if (aspect_decl && aspect_decl->value) {
            if (aspect_decl->value->type == CSS_VALUE_TYPE_NUMBER) {
                aspect_ratio = (float)aspect_decl->value->data.number.value;
                log_debug("  -> aspect-ratio from specified_style: %.3f", aspect_ratio);
            } else if (aspect_decl->value->type == CSS_VALUE_TYPE_LIST &&
                       aspect_decl->value->data.list.count == 2) {
                // Handle "width / height" format
                CssValue* width_val = aspect_decl->value->data.list.values[0];
                CssValue* height_val = aspect_decl->value->data.list.values[1];
                if (width_val && height_val &&
                    width_val->type == CSS_VALUE_TYPE_NUMBER &&
                    height_val->type == CSS_VALUE_TYPE_NUMBER) {
                    float w = (float)width_val->data.number.value;
                    float h = (float)height_val->data.number.value;
                    if (h > 0) {
                        aspect_ratio = w / h;
                        log_debug("  -> aspect-ratio from specified_style list: %.3f (%.1f / %.1f)",
                                  aspect_ratio, w, h);
                    }
                }
            }
        }
    }

    if (aspect_ratio > 0) {
        float height = -1;

        // Check for explicit height from CSS
        if (view_block_for_aspect->blk && view_block_for_aspect->blk->given_height > 0) {
            height = view_block_for_aspect->blk->given_height;
        }

        // If no explicit height, check for percentage height that can resolve
        // against a parent with definite height
        if (height <= 0 && element->specified_style) {
            CssDeclaration* height_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_HEIGHT);
            if (height_decl && height_decl->value &&
                height_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                float percentage = (float)height_decl->value->data.percentage.value;
                // Check if parent has definite height
                float parent_height = -1;
                if (lycon->block.parent && lycon->block.parent->content_height > 0) {
                    parent_height = lycon->block.parent->content_height;
                } else if (lycon->block.parent && lycon->block.parent->given_height > 0) {
                    parent_height = lycon->block.parent->given_height;
                }
                if (parent_height > 0) {
                    height = parent_height * percentage / 100.0f;
                    log_debug("  -> percentage height resolved: %.1f%% of %.1f = %.1f",
                              percentage, parent_height, height);
                }
            }
        }

        if (height > 0) {
            float aspect_width = height * aspect_ratio;
            sizes.min_content = (int)(aspect_width + 0.5f);
            sizes.max_content = (int)(aspect_width + 0.5f);
            log_debug("  -> aspect-ratio width: %.1f (height=%.1f, ratio=%.3f)",
                      aspect_width, height, aspect_ratio);
            return sizes;
        }
    }

    // CSS 2.1 §10.3.2/§10.6.2: Replaced element intrinsic sizing
    // For replaced elements (img, video, iframe, etc.), use intrinsic dimensions
    // when no explicit CSS width is set
    ViewBlock* view_block_replaced = (ViewBlock*)element;
    if (view_block_replaced->display.inner == RDT_DISPLAY_REPLACED) {
        uintptr_t elem_tag = element->tag();

        if (elem_tag == HTM_TAG_IMG) {
            // Try to get image dimensions - first check if already loaded
            if (view_block_replaced->embed && view_block_replaced->embed->img) {
                ImageSurface* img = view_block_replaced->embed->img;
                sizes.min_content = img->width;
                sizes.max_content = img->width;
                log_debug("  -> replaced IMG intrinsic width: %d (from loaded image)", img->width);
                return sizes;
            }
            // Try to load the image to get intrinsic dimensions
            const char* src_value = element->get_attribute("src");
            if (src_value && lycon->ui_context) {
                if (!view_block_replaced->embed) {
                    view_block_replaced->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
                }
                size_t src_len = strlen(src_value);
                StrBuf* src_buf = strbuf_new_cap(src_len);
                strbuf_append_str_n(src_buf, src_value, src_len);
                view_block_replaced->embed->img = load_image(lycon->ui_context, src_buf->str);
                strbuf_free(src_buf);
                if (view_block_replaced->embed->img) {
                    ImageSurface* img = view_block_replaced->embed->img;
                    sizes.min_content = img->width;
                    sizes.max_content = img->width;
                    log_debug("  -> replaced IMG intrinsic width: %d (newly loaded)", img->width);
                    return sizes;
                }
            }
            // Fallback for unloadable images — use HTML width/height attributes if present
            const char* attr_w = element->get_attribute("width");
            if (attr_w) {
                int w = atoi(attr_w);
                if (w > 0) {
                    sizes.min_content = w;
                    sizes.max_content = w;
                    log_debug("  -> replaced IMG width from HTML attribute: %d", w);
                    return sizes;
                }
            }
            // Ultimate fallback — placeholder size
            sizes.min_content = 40;
            sizes.max_content = 40;
            log_debug("  -> replaced IMG fallback width: 40");
            return sizes;
        }
        else if (elem_tag == HTM_TAG_IFRAME) {
            // Default iframe size per CSS spec: 300x150
            sizes.min_content = 300;
            sizes.max_content = 300;
            log_debug("  -> replaced IFRAME intrinsic width: 300");
            return sizes;
        }
        else if (elem_tag == HTM_TAG_VIDEO || elem_tag == HTM_TAG_CANVAS) {
            // Default video/canvas size: 300x150
            sizes.min_content = 300;
            sizes.max_content = 300;
            log_debug("  -> replaced VIDEO/CANVAS intrinsic width: 300");
            return sizes;
        }
        else if (elem_tag == HTM_TAG_SVG) {
            // SVG intrinsic width from attributes or viewBox
            float svg_width = 300.0f;  // CSS default
            const char* attr_w = element->get_attribute("width");
            if (attr_w) {
                float w = (float)atof(attr_w);
                if (w > 0) svg_width = w;
            } else {
                const char* viewbox = element->get_attribute("viewBox");
                if (viewbox) {
                    float vb_x, vb_y, vb_w, vb_h;
                    if (sscanf(viewbox, "%f %f %f %f", &vb_x, &vb_y, &vb_w, &vb_h) == 4 && vb_w > 0) {
                        svg_width = vb_w;
                    }
                }
            }
            sizes.min_content = (int)(svg_width + 0.5f);
            sizes.max_content = (int)(svg_width + 0.5f);
            log_debug("  -> replaced SVG intrinsic width: %.0f", svg_width);
            return sizes;
        }
        else if (elem_tag == HTM_TAG_HR) {
            // HR stretches to available width, min is 0
            sizes.min_content = 0;
            sizes.max_content = 0;
            return sizes;
        }
        // Form controls (INPUT, SELECT, TEXTAREA, BUTTON) fall through to
        // normal measurement — they already have intrinsic sizing via FormControlProp
    }

    // SVG fallback: handle SVG elements even when display.inner is not yet resolved
    if (element->tag() == HTM_TAG_SVG) {
        float svg_width = 300.0f;
        const char* attr_w = element->get_attribute("width");
        if (attr_w) {
            float w = (float)atof(attr_w);
            if (w > 0) svg_width = w;
        } else {
            const char* viewbox = element->get_attribute("viewBox");
            if (viewbox) {
                float vb_x, vb_y, vb_w, vb_h;
                if (sscanf(viewbox, "%f %f %f %f", &vb_x, &vb_y, &vb_w, &vb_h) == 4 && vb_w > 0) {
                    svg_width = vb_w;
                }
            }
        }
        sizes.min_content = (int)(svg_width + 0.5f);
        sizes.max_content = (int)(svg_width + 0.5f);
        log_debug("  -> SVG (tag-based) intrinsic width: %.0f", svg_width);
        return sizes;
    }

    // ========================================================================
    // Table element special handling: table intrinsic width = sum of cell widths
    // CSS Tables §4.1: Table min/max content width is the maximum over all rows
    // of the sum of cell min/max content widths in that row + border-spacing.
    // ========================================================================
    {
        ViewBlock* tbl_view = (ViewBlock*)element;
        bool is_table_element = (tbl_view->display.inner == CSS_VALUE_TABLE);
        if (!is_table_element) {
            uintptr_t etag = element->tag();
            if (etag == HTM_TAG_TABLE) is_table_element = true;
        }
        if (!is_table_element && element->specified_style) {
            CssDeclaration* dd = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_DISPLAY);
            if (dd && dd->value && dd->value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum dv = dd->value->data.keyword;
                if (dv == CSS_VALUE_TABLE || dv == CSS_VALUE_INLINE_TABLE) is_table_element = true;
            }
        }

        if (is_table_element) {
            float border_spacing = 0;
            if (element->tb) {
                border_spacing = element->tb->border_spacing_h;
            }

            // Helper lambda: measure cells in a row and sum horizontally
            auto measure_row = [&](DomElement* row_elem) {
                float row_min = 0, row_max = 0;
                int cell_count = 0;
                for (DomNode* cell = row_elem->first_child; cell; cell = cell->next_sibling) {
                    if (!cell->is_element()) continue;
                    DomElement* cell_elem = cell->as_element();
                    ViewBlock* cell_view = (ViewBlock*)cell_elem;
                    bool is_cell = (cell_view->display.inner == CSS_VALUE_TABLE_CELL ||
                                    cell_elem->tag() == HTM_TAG_TD || cell_elem->tag() == HTM_TAG_TH);
                    if (!is_cell) continue;
                    IntrinsicSizes cell_sizes = measure_element_intrinsic_widths(lycon, cell_elem);
                    // ceil each cell width to match table layout's integer-pixel allocation
                    row_min += ceilf(cell_sizes.min_content);
                    row_max += ceilf(cell_sizes.max_content);
                    cell_count++;
                }
                if (cell_count > 1) {
                    row_min += border_spacing * (cell_count - 1);
                    row_max += border_spacing * (cell_count - 1);
                }
                sizes.min_content = max(sizes.min_content, row_min);
                sizes.max_content = max(sizes.max_content, row_max);
                log_debug("  table row: %d cells, min=%.1f, max=%.1f", cell_count, row_min, row_max);
            };

            for (DomNode* child = element->first_child; child; child = child->next_sibling) {
                if (!child->is_element()) continue;
                DomElement* child_elem = child->as_element();
                ViewBlock* child_view = (ViewBlock*)child_elem;
                uintptr_t ctag = child_elem->tag();

                bool is_row = (child_view->display.inner == CSS_VALUE_TABLE_ROW || ctag == HTM_TAG_TR);
                bool is_row_group = (!is_row && (
                    child_view->display.inner == CSS_VALUE_TABLE_ROW_GROUP ||
                    child_view->display.inner == CSS_VALUE_TABLE_HEADER_GROUP ||
                    child_view->display.inner == CSS_VALUE_TABLE_FOOTER_GROUP ||
                    ctag == HTM_TAG_TBODY || ctag == HTM_TAG_THEAD || ctag == HTM_TAG_TFOOT));

                if (is_row) {
                    measure_row(child_elem);
                } else if (is_row_group) {
                    for (DomNode* row = child_elem->first_child; row; row = row->next_sibling) {
                        if (!row->is_element()) continue;
                        DomElement* row_elem = row->as_element();
                        ViewBlock* row_view = (ViewBlock*)row_elem;
                        if (row_view->display.inner == CSS_VALUE_TABLE_ROW || row_elem->tag() == HTM_TAG_TR) {
                            measure_row(row_elem);
                        }
                    }
                }
                // Captions: treat like block children (take max)
                bool is_caption = (child_view->display.inner == CSS_VALUE_TABLE_CAPTION || ctag == HTM_TAG_CAPTION);
                if (is_caption) {
                    IntrinsicSizes cap = measure_element_intrinsic_widths(lycon, child_elem);
                    sizes.min_content = max(sizes.min_content, cap.min_content);
                    sizes.max_content = max(sizes.max_content, cap.max_content);
                }
            }

            // Add table's own padding and border
            float pad_left = 0, pad_right = 0, bdr_left = 0, bdr_right = 0;
            if (tbl_view->bound) {
                if (tbl_view->bound->padding.left >= 0) pad_left = tbl_view->bound->padding.left;
                if (tbl_view->bound->padding.right >= 0) pad_right = tbl_view->bound->padding.right;
                if (tbl_view->bound->border) {
                    bdr_left = tbl_view->bound->border->width.left;
                    bdr_right = tbl_view->bound->border->width.right;
                }
            }
            // Fallback: read border from specified CSS if bound not yet resolved
            if (bdr_left == 0 && bdr_right == 0) {
                float css_bw = get_border_width_from_css(lycon, element);
                if (css_bw > 0) { bdr_left = css_bw; bdr_right = css_bw; }
            }

            // Only use table-specific result if we actually found table rows/cells.
            // Tables with only text/block children (no row structure) should fall
            // through to the generic block measurement below (CSS anonymous box wrapping
            // will create rows/cells at layout time, but for measurement we use generic).
            if (sizes.min_content > 0 || sizes.max_content > 0) {
                sizes.min_content += pad_left + pad_right + bdr_left + bdr_right;
                sizes.max_content += pad_left + pad_right + bdr_left + bdr_right;

                log_debug("measure_element_intrinsic: TABLE %s: min=%.1f, max=%.1f",
                          element->node_name(), sizes.min_content, sizes.max_content);

                // Restore font if changed
                if (font_changed) lycon->font = saved_font;
                return sizes;
            }
            // No table structure found — fall through to generic measurement
            log_debug("measure_element_intrinsic: TABLE %s has no row structure, falling through",
                      element->node_name());
        }
    }

    // Track inline-level content separately
    float inline_min_sum = 0.0f;  // Sum of min-content widths for inline children
    float inline_max_sum = 0.0f;  // Sum of max-content widths for inline children
    bool has_inline_content = false;

    // Check if this element is a flex container (text content doesn't contribute to intrinsic size)
    // Also check if it's a ROW flex container (children laid out horizontally -> SUM widths)
    bool is_flex_container = false;
    bool is_row_flex = false;
    float flex_gap = 0;
    int flex_child_count = 0;  // Count of flex children for gap calculation
    ViewBlock* view_block = (ViewBlock*)element;

    // Check if this is a grid container with explicit height
    // Grid children with percentage heights should resolve against this height
    bool is_grid_container = false;
    float grid_explicit_height = -1;
    if (view_block->display.inner == CSS_VALUE_GRID) {
        is_grid_container = true;
    }
    if (!is_grid_container && element->specified_style) {
        CssDeclaration* display_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_DISPLAY);
        if (display_decl && display_decl->value &&
            display_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
            CssEnum dv = display_decl->value->data.keyword;
            if (dv == CSS_VALUE_GRID || dv == CSS_VALUE_INLINE_GRID) {
                is_grid_container = true;
            }
        }
    }
    if (is_grid_container) {
        // Get explicit height from view or CSS
        if (view_block->blk && view_block->blk->given_height > 0) {
            grid_explicit_height = view_block->blk->given_height;
        } else if (element->specified_style) {
            CssDeclaration* height_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_HEIGHT);
            if (height_decl && height_decl->value &&
                height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                grid_explicit_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
            }
        }
        log_debug("measure_element_intrinsic_widths: grid container with explicit height=%.1f", grid_explicit_height);

        // CSS Grid §10.1: Grid container intrinsic widths are computed column-by-column.
        // For a grid with N explicit columns, the max-content width = sum of column max-contents
        // (each column's max-content = max of all items spanning only that column).
        // This is different from a block container which takes max of block children.
        GridProp* grid_prop = view_block->embed ? view_block->embed->grid : nullptr;
        int col_count = 0;
        if (grid_prop && grid_prop->grid_template_columns) {
            col_count = grid_prop->grid_template_columns->track_count;
        }
        // Also check specified_style for unresolved grid-template-columns
        if (col_count == 0 && element->specified_style) {
            CssDeclaration* cols_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_GRID_TEMPLATE_COLUMNS);
            if (cols_decl && cols_decl->value && cols_decl->value->type == CSS_VALUE_TYPE_LIST) {
                col_count = cols_decl->value->data.list.count;
            } else if (cols_decl && cols_decl->value && cols_decl->value->type != CSS_VALUE_TYPE_KEYWORD) {
                col_count = 1;
            }
        }

        if (col_count > 1) {
            // Compute per-column max-content: assign each child to a column (auto-placement)
            // and take max of children's max-content in each column
            float* col_min = (float*)calloc(col_count, sizeof(float));
            float* col_max = (float*)calloc(col_count, sizeof(float));

            int item_idx = 0;
            for (DomNode* child = element->first_child; child; child = child->next_sibling) {
                if (!child->is_element()) continue;
                DomElement* child_elem = child->as_element();
                // Auto-placement: assign to column item_idx % col_count
                int col = item_idx % col_count;
                IntrinsicSizes child_sizes = measure_element_intrinsic_widths(lycon, child_elem);
                if (child_sizes.min_content > col_min[col]) col_min[col] = child_sizes.min_content;
                if (child_sizes.max_content > col_max[col]) col_max[col] = child_sizes.max_content;

                // Check for explicit fixed-width track
                if (grid_prop && grid_prop->grid_template_columns && col < col_count) {
                    GridTrackSize* track = grid_prop->grid_template_columns->tracks[col];
                    if (track && track->type == GRID_TRACK_SIZE_LENGTH && track->value > 0) {
                        // Fixed length track: the column size is the fixed value, not the content
                        float fixed_px = track->is_percentage ? 0 : (float)track->value;
                        if (fixed_px > 0) {
                            col_max[col] = fixed_px;
                            col_min[col] = fixed_px;
                        }
                    }
                }

                item_idx++;
            }

            // Check if track sizes are fixed-length (from CSS)
            // For fixed tracks, use fixed value regardless of content
            if (grid_prop && grid_prop->grid_template_columns) {
                for (int c = 0; c < col_count; c++) {
                    GridTrackSize* track = grid_prop->grid_template_columns->tracks[c];
                    if (track && track->type == GRID_TRACK_SIZE_LENGTH && !track->is_percentage && track->value > 0) {
                        col_max[c] = (float)track->value;
                        col_min[c] = (float)track->value;
                    }
                }
            }

            // Sum column sizes + gaps
            float column_gap = (grid_prop ? grid_prop->column_gap : 0.0f);
            float total_min = 0.0f, total_max = 0.0f;
            for (int c = 0; c < col_count; c++) {
                total_min += col_min[c];
                total_max += col_max[c];
            }
            if (col_count > 1 && column_gap > 0) {
                total_min += column_gap * (col_count - 1);
                total_max += column_gap * (col_count - 1);
            }

            free(col_min);
            free(col_max);

            // Add padding and border
            float pad_left = 0, pad_right = 0, border_left = 0, border_right = 0;
            if (view_block->bound) {
                pad_left = view_block->bound->padding.left;
                pad_right = view_block->bound->padding.right;
                if (view_block->bound->border) {
                    border_left = view_block->bound->border->width.left;
                    border_right = view_block->bound->border->width.right;
                }
            }
            total_min += pad_left + pad_right + border_left + border_right;
            total_max += pad_left + pad_right + border_left + border_right;

            log_debug("measure_element_intrinsic_widths: grid %s with %d columns: min=%.1f, max=%.1f",
                      element->node_name(), col_count, total_min, total_max);

            // Apply max-width constraint (same logic as the generic path below).
            // The grid early-return bypasses the generic constraint code, so we apply it here.
            {
                float gmax = -1;
                if (view_block->blk) gmax = view_block->blk->given_max_width;
                if (gmax < 0 && element->specified_style) {
                    CssDeclaration* mw = style_tree_get_declaration(
                        element->specified_style, CSS_PROPERTY_MAX_WIDTH);
                    if (mw && mw->value && mw->value->type == CSS_VALUE_TYPE_LENGTH) {
                        gmax = resolve_length_value(lycon, CSS_PROPERTY_MAX_WIDTH, mw->value);
                    }
                }
                if (gmax >= 0) {
                    float max_bb = gmax + pad_left + pad_right + border_left + border_right;
                    if (total_max > max_bb) total_max = max_bb;
                    if (total_min > max_bb) total_min = max_bb;
                }
            }

            // Restore font
            if (font_changed) lycon->font = saved_font;
            return {total_min, total_max};
        }
    }

    // First check resolved display.inner
    if (view_block->display.inner == CSS_VALUE_FLEX) {
        is_flex_container = true;
    }
    // Also check specified_style for unresolved display
    if (!is_flex_container && element->specified_style) {
        CssDeclaration* display_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_DISPLAY);
        if (display_decl && display_decl->value &&
            display_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
            CssEnum dv = display_decl->value->data.keyword;
            if (dv == CSS_VALUE_FLEX || dv == CSS_VALUE_INLINE_FLEX) {
                is_flex_container = true;
                log_debug("measure_element_intrinsic_widths: detected flex via specified_style");
            }
        }
    }

    // Check flex direction for row vs column
    bool is_vertical_wm = false;
    if (is_flex_container) {
        // Default flex-direction is row
        is_row_flex = true;  // Assume row by default
        if (view_block->embed && view_block->embed->flex) {
            int dir = view_block->embed->flex->direction;
            is_row_flex = (dir == CSS_VALUE_ROW || dir == CSS_VALUE_ROW_REVERSE ||
                          dir == DIR_ROW || dir == DIR_ROW_REVERSE);
            flex_gap = view_block->embed->flex->column_gap;
            is_vertical_wm = (view_block->embed->flex->writing_mode == WM_VERTICAL_LR ||
                              view_block->embed->flex->writing_mode == WM_VERTICAL_RL);
        } else if (element->specified_style) {
            // Check specified_style for flex-direction
            CssDeclaration* dir_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_FLEX_DIRECTION);
            if (dir_decl && dir_decl->value && dir_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum dir = dir_decl->value->data.keyword;
                is_row_flex = (dir == CSS_VALUE_ROW || dir == CSS_VALUE_ROW_REVERSE);
            }
            // Check for writing-mode
            CssDeclaration* wm_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_WRITING_MODE);
            if (wm_decl && wm_decl->value && wm_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum wm = wm_decl->value->data.keyword;
                is_vertical_wm = (wm == CSS_VALUE_VERTICAL_LR || wm == CSS_VALUE_VERTICAL_RL);
            }
            // Check for gap (try column-gap first, then gap shorthand)
            CssDeclaration* gap_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_COLUMN_GAP);
            if (!gap_decl) {
                gap_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_GAP);
            }
            if (gap_decl && gap_decl->value && gap_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                flex_gap = resolve_length_value(lycon, CSS_PROPERTY_GAP, gap_decl->value);
            }
        }
        // In vertical writing modes, the physical axis mapping swaps:
        // column becomes horizontal, row becomes vertical
        if (is_vertical_wm) {
            is_row_flex = !is_row_flex;
            log_debug("measure_element_intrinsic_widths: vertical writing mode, flipped is_row_flex");
        }
        log_debug("measure_element_intrinsic_widths: %s is_flex=%d, is_row_flex=%d, gap=%.1f, vertical_wm=%d",
                  element->node_name(), is_flex_container, is_row_flex, flex_gap, is_vertical_wm);
    }

    // Set up parent context for children to inherit definite height
    // This allows children with percentage heights and aspect-ratio to compute their width
    BlockContext* saved_parent = lycon->block.parent;
    BlockContext temp_parent = {};
    bool need_restore_parent = false;

    // Determine if this element has a definite height to propagate
    float element_definite_height = -1;

    log_debug("  -> checking height for %s: blk=%p, given_height=%.1f",
              element->node_name(),
              (void*)(view_block->blk),
              view_block->blk ? view_block->blk->given_height : -999);

    // First check for explicit height from CSS (length value)
    if (view_block->blk && view_block->blk->given_height > 0) {
        element_definite_height = view_block->blk->given_height;
        log_debug("  -> got explicit height from blk: %.1f", element_definite_height);
    } else if (element->specified_style) {
        CssDeclaration* height_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_HEIGHT);
        log_debug("  -> checking specified_style for height: decl=%p, value=%p",
                  (void*)height_decl, height_decl ? (void*)height_decl->value : nullptr);
        if (height_decl && height_decl->value) {
            log_debug("  -> height decl value type=%d (LENGTH=%d, PERCENTAGE=%d)",
                      height_decl->value->type, CSS_VALUE_TYPE_LENGTH, CSS_VALUE_TYPE_PERCENTAGE);
            if (height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                element_definite_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
                log_debug("  -> resolved length height: %.1f", element_definite_height);
            } else if (height_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                // Check if parent has definite height for percentage resolution
                float percentage = (float)height_decl->value->data.percentage.value;
                float parent_height = -1;
                if (lycon->block.parent && lycon->block.parent->content_height > 0) {
                    parent_height = lycon->block.parent->content_height;
                } else if (lycon->block.parent && lycon->block.parent->given_height > 0) {
                    parent_height = lycon->block.parent->given_height;
                }
                log_debug("  -> percentage height: %.1f%%, parent_height=%.1f",
                          percentage, parent_height);
                if (parent_height > 0) {
                    element_definite_height = parent_height * percentage / 100.0f;
                    log_debug("  -> element percentage height resolved: %.1f%% of %.1f = %.1f",
                              percentage, parent_height, element_definite_height);
                }
            }
        }
    }

    // If this element has a definite height, propagate it to children
    if (element_definite_height > 0) {
        temp_parent.content_height = element_definite_height;
        temp_parent.given_height = element_definite_height;
        lycon->block.parent = &temp_parent;
        need_restore_parent = true;
        log_debug("  -> set up temp parent context with height=%.1f for children", element_definite_height);
    }

    // Measure children recursively
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        IntrinsicSizes child_sizes = {0, 0};
        bool is_inline = false;

        if (child->is_text()) {
            const char* text = (const char*)child->text_data();
            if (text) {
                size_t text_len = strlen(text);
                // Normalize whitespace: collapse consecutive spaces, trim leading/trailing
                // This matches CSS white-space: normal behavior
                char normalized_buffer[2048];
                size_t out_pos = 0;
                // Only trim leading whitespace if this is the first child or preceded only by whitespace.
                // If there's inline content before this text node, leading whitespace should
                // collapse to a single space (which contributes to intrinsic width).
                bool has_inline_before = (child->prev_sibling != nullptr &&
                                          has_inline_content &&
                                          inline_max_sum > 0);
                bool in_whitespace = !has_inline_before;  // Only start as in_whitespace if no inline content before
                for (size_t i = 0; i < text_len && out_pos < sizeof(normalized_buffer) - 1; i++) {
                    unsigned char ch = (unsigned char)text[i];
                    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f') {
                        if (!in_whitespace) {
                            normalized_buffer[out_pos++] = ' ';  // Collapse to single space
                            in_whitespace = true;
                        }
                    } else {
                        normalized_buffer[out_pos++] = (char)ch;
                        in_whitespace = false;
                    }
                }
                // Only trim trailing whitespace if there's no inline content after this text node.
                // Trailing whitespace before an inline sibling (like <a>) should be preserved
                // as it contributes to the inter-word spacing.
                bool has_inline_after = false;
                if (child->next_sibling) {
                    DomNode* next = child->next_sibling;
                    // Skip whitespace-only text nodes
                    while (next && next->is_text()) {
                        const char* next_text = (const char*)next->text_data();
                        bool all_ws = true;
                        if (next_text) {
                            for (const char* p = next_text; *p && all_ws; p++) {
                                unsigned char c = (unsigned char)*p;
                                if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f') {
                                    all_ws = false;
                                }
                            }
                        }
                        if (!all_ws) break;
                        next = next->next_sibling;
                    }
                    if (next && next->is_element()) {
                        has_inline_after = is_inline_level_element(next->as_element());
                    }
                }
                if (!has_inline_after) {
                    // Trim trailing whitespace
                    while (out_pos > 0 && normalized_buffer[out_pos - 1] == ' ') {
                        out_pos--;
                    }
                }
                normalized_buffer[out_pos] = '\0';

                // Skip if all whitespace (out_pos == 0)
                if (out_pos == 0) {
                    continue;
                }

                // Get text-transform from parent element (text inherits from parent)
                CssEnum text_transform = get_element_text_transform(element);
                CssEnum font_variant = get_element_font_variant(element);

                TextIntrinsicWidths text_widths = measure_text_intrinsic_widths(
                    lycon, normalized_buffer, out_pos, text_transform, font_variant);
                child_sizes.min_content = text_widths.min_content;
                child_sizes.max_content = text_widths.max_content;

                // white-space: nowrap/pre prevents line breaks, so min-content = max-content
                {
                    CssEnum ws = get_white_space_value(child);
                    if (ws == CSS_VALUE_NOWRAP || ws == CSS_VALUE_PRE) {
                        child_sizes.min_content = child_sizes.max_content;
                    }
                }

                // In vertical writing mode, text flows top-to-bottom.
                // Physical width = font_size (one column of characters),
                // physical height = text inline extent.
                if (is_vertical_wm && is_flex_container) {
                    float font_size = lycon->font.style ? lycon->font.style->font_size : 16.0f;
                    child_sizes.min_content = font_size;
                    child_sizes.max_content = font_size;
                    log_debug("  vertical writing mode: text width -> font_size=%.1f", font_size);
                }

                // In flex containers, text nodes become anonymous flex items
                if (is_flex_container) {
                    log_debug("  flex text child: min=%.1f, max=%.1f, normalized_len=%zu, text='%.30s...'",
                              child_sizes.min_content, child_sizes.max_content, out_pos, normalized_buffer);
                }
            }
            is_inline = true;  // Text nodes are always inline (unless in flex container)
        } else if (child->is_element()) {
            DomElement* child_elem = child->as_element();
            child_sizes = measure_element_intrinsic_widths(lycon, child_elem);
            is_inline = is_inline_level_element(child_elem);

            log_debug("  child %s: min=%.1f, max=%.1f, is_inline=%d",
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
                } else if (child_elem->specified_style) {
                    // Fallback: read margins from specified CSS style when bound isn't allocated
                    float ml = 0, mr = 0;
                    CssDeclaration* ml_decl = style_tree_get_declaration(
                        child_elem->specified_style, CSS_PROPERTY_MARGIN_LEFT);
                    if (ml_decl && ml_decl->value && ml_decl->value->type == CSS_VALUE_TYPE_LENGTH)
                        ml = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_LEFT, ml_decl->value);
                    CssDeclaration* mr_decl = style_tree_get_declaration(
                        child_elem->specified_style, CSS_PROPERTY_MARGIN_RIGHT);
                    if (mr_decl && mr_decl->value && mr_decl->value->type == CSS_VALUE_TYPE_LENGTH)
                        mr = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_RIGHT, mr_decl->value);
                    if (ml == 0 && mr == 0) {
                        CssDeclaration* m_decl = style_tree_get_declaration(
                            child_elem->specified_style, CSS_PROPERTY_MARGIN);
                        if (m_decl && m_decl->value) {
                            const CssValue* val = m_decl->value;
                            if (val->type == CSS_VALUE_TYPE_LENGTH) {
                                ml = mr = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, val);
                            } else if (val->type == CSS_VALUE_TYPE_LIST && val->data.list.count >= 1) {
                                int cnt = val->data.list.count;
                                CssValue** vals = val->data.list.values;
                                if (cnt <= 3) {
                                    float lr = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, vals[cnt >= 2 ? 1 : 0]);
                                    ml = mr = lr;
                                } else {
                                    mr = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, vals[1]);
                                    ml = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, vals[3]);
                                }
                            }
                        }
                    }
                    child_sizes.max_content += ml + mr;
                    child_sizes.min_content += ml + mr;
                }
            }
        }

        // Handle flex container children - all children become flex items
        // In a flex container, both text and element children are flex items
        // They should NOT go through the inline content path
        if (is_flex_container) {
            // CSS Flexbox §4: Absolutely positioned children are out-of-flow
            // and do not participate in flex layout or contribute to intrinsic size
            if (child->is_element()) {
                DomElement* child_elem = child->as_element();
                ViewBlock* child_block = (ViewBlock*)child_elem;
                bool child_is_absolute = false;
                if (child_block && child_block->position &&
                    (child_block->position->position == CSS_VALUE_ABSOLUTE ||
                     child_block->position->position == CSS_VALUE_FIXED)) {
                    child_is_absolute = true;
                } else if (child_elem->specified_style) {
                    CssDeclaration* pos_decl = style_tree_get_declaration(
                        child_elem->specified_style, CSS_PROPERTY_POSITION);
                    if (pos_decl && pos_decl->value && pos_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                        CssEnum pos_val = pos_decl->value->data.keyword;
                        child_is_absolute = (pos_val == CSS_VALUE_ABSOLUTE || pos_val == CSS_VALUE_FIXED);
                    }
                }
                if (child_is_absolute) {
                    log_debug("  skipping absolute child %s in flex intrinsic sizing", child_elem->node_name());
                    continue;
                }
            }

            if (is_row_flex) {
                // Row flex: sum widths horizontally
                sizes.min_content += child_sizes.min_content;
                sizes.max_content += child_sizes.max_content;
                flex_child_count++;
                log_debug("  row flex item: min=%.1f, max=%.1f, count=%d",
                          child_sizes.min_content, child_sizes.max_content, flex_child_count);
            } else {
                // Column flex: take max of widths
                sizes.min_content = max(sizes.min_content, child_sizes.min_content);
                sizes.max_content = max(sizes.max_content, child_sizes.max_content);
                flex_child_count++;
            }
        } else if (is_inline) {
            // For inline content, sum widths for max-content (no wrapping)
            // and take max of min-content (can wrap between items)
            has_inline_content = true;
            inline_max_sum += child_sizes.max_content;
            inline_min_sum = max(inline_min_sum, child_sizes.min_content);
        } else {
            // Block-level child encountered
            // CSS 2.1: Block children break inline flow (block-in-inline)
            // First, flush any accumulated inline content before the block
            if (inline_max_sum > 0) {
                sizes.min_content = max(sizes.min_content, inline_min_sum);
                sizes.max_content = max(sizes.max_content, inline_max_sum);
                log_debug("  block-in-inline: flushing inline run max=%.1f, min=%.1f before block %s",
                          inline_max_sum, inline_min_sum,
                          child->is_element() ? child->as_element()->node_name() : "?");
                // Reset for next inline run after the block
                inline_max_sum = 0;
                inline_min_sum = 0;
            }

            // For block-level children: take max of each
            // Also include horizontal margins for proper shrink-to-fit calculation
            // CSS 2.2 Section 10.3.5: floated/absolutely positioned elements use shrink-to-fit
            // which includes the margin box of child floats
            float child_width = child_sizes.max_content;
            float margin_left = 0, margin_right = 0;
            if (child->is_element()) {
                DomElement* child_elem = child->as_element();
                ViewBlock* child_view = (ViewBlock*)child_elem;
                if (child_view->bound) {
                    // Add margins to the child's width for proper shrink-to-fit
                    if (child_view->bound->margin.left_type != CSS_VALUE_AUTO &&
                        child_view->bound->margin.left >= 0) {
                        margin_left = child_view->bound->margin.left;
                        child_width += margin_left;
                    }
                    if (child_view->bound->margin.right_type != CSS_VALUE_AUTO &&
                        child_view->bound->margin.right >= 0) {
                        margin_right = child_view->bound->margin.right;
                        child_width += margin_right;
                    }
                    log_debug("  block child %s: max=%.1f, margins=(%.1f,%.1f) from bound, total=%.1f",
                              child_elem->node_name(), child_sizes.max_content, margin_left, margin_right, child_width);
                } else if (child_elem->specified_style) {
                    // Read margins directly from specified CSS style
                    // This handles the case during intrinsic sizing when bound isn't allocated yet
                    // First check for individual margin properties
                    CssDeclaration* margin_left_decl = style_tree_get_declaration(
                        child_elem->specified_style, CSS_PROPERTY_MARGIN_LEFT);
                    if (margin_left_decl && margin_left_decl->value &&
                        margin_left_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                        margin_left = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_LEFT,
                                                           margin_left_decl->value);
                        child_width += margin_left;
                    }
                    CssDeclaration* margin_right_decl = style_tree_get_declaration(
                        child_elem->specified_style, CSS_PROPERTY_MARGIN_RIGHT);
                    if (margin_right_decl && margin_right_decl->value &&
                        margin_right_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                        margin_right = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_RIGHT,
                                                            margin_right_decl->value);
                        child_width += margin_right;
                    }
                    // If individual properties not found, check shorthand margin property
                    if (margin_left == 0 && margin_right == 0) {
                        CssDeclaration* margin_decl = style_tree_get_declaration(
                            child_elem->specified_style, CSS_PROPERTY_MARGIN);
                        if (margin_decl && margin_decl->value) {
                            // Handle shorthand: margin: value or margin: v1 v2 v3 v4
                            const CssValue* val = margin_decl->value;
                            if (val->type == CSS_VALUE_TYPE_LENGTH) {
                                // Single value applies to all sides
                                float margin_val = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, val);
                                margin_left = margin_val;
                                margin_right = margin_val;
                                child_width += margin_left + margin_right;
                            } else if (val->type == CSS_VALUE_TYPE_LIST && val->data.list.count >= 1) {
                                // Multi-value: 1=all, 2=TB LR, 3=T LR B, 4=T R B L
                                int cnt = val->data.list.count;
                                CssValue** vals = val->data.list.values;
                                if (cnt == 1) {
                                    float m = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, vals[0]);
                                    margin_left = margin_right = m;
                                } else if (cnt == 2) {
                                    // vals[1] = left/right
                                    float lr = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, vals[1]);
                                    margin_left = margin_right = lr;
                                } else if (cnt == 3) {
                                    // vals[1] = left/right
                                    float lr = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, vals[1]);
                                    margin_left = margin_right = lr;
                                } else if (cnt >= 4) {
                                    // vals[1] = right, vals[3] = left
                                    margin_right = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, vals[1]);
                                    margin_left = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, vals[3]);
                                }
                                child_width += margin_left + margin_right;
                            }
                        }
                    }
                    log_debug("  block child %s: max=%.1f, margins=(%.1f,%.1f) from CSS, total=%.1f",
                              child_elem->node_name(), child_sizes.max_content, margin_left, margin_right, child_width);
                } else {
                    log_debug("  block child %s: max=%.1f, no bound or specified_style",
                              child_elem->node_name(), child_sizes.max_content);
                }
            }
            sizes.min_content = max(sizes.min_content, child_sizes.min_content);
            sizes.max_content = max(sizes.max_content, child_width);
        }
    }

    // Restore parent context
    if (need_restore_parent) {
        lycon->block.parent = saved_parent;
    }

    // Add gaps for row flex containers
    if (is_row_flex && flex_child_count > 1 && flex_gap > 0) {
        int total_gap = (int)(flex_gap * (flex_child_count - 1));
        sizes.min_content += total_gap;
        sizes.max_content += total_gap;
        log_debug("  row flex gap: %d items, %.1fpx gap = %dpx total",
                  flex_child_count, flex_gap, total_gap);
    }

    // CSS 2.1 §12.5.1: For display:list-item with list-style-position:inside,
    // the marker box is the first inline box in the principal block box.
    // It contributes to the intrinsic width as inline content.
    if (view_block->display.outer == CSS_VALUE_LIST_ITEM) {
        bool is_inside_position = false;
        bool has_marker = true;  // default list-style-type is 'disc'

        // Check list-style-position — it's an inherited property, so walk up
        // the ancestor chain if not found directly on this element.
        if (view_block->blk && view_block->blk->list_style_position == 1) {  // 1 = inside
            is_inside_position = true;
        } else {
            // Walk ancestor chain looking for list-style-position (inherited property)
            for (DomNode* anc = (DomNode*)element; anc; anc = anc->parent) {
                if (!anc->is_element()) continue;
                DomElement* anc_elem = anc->as_element();
                // Check resolved blk first (if available)
                ViewBlock* anc_view = (ViewBlock*)anc_elem;
                if (anc_view->blk && anc_view->blk->list_style_position == 1) {
                    is_inside_position = true;
                    break;
                }
                if (anc_view->blk && anc_view->blk->list_style_position == 2) {
                    break;  // explicitly 'outside'
                }
                // Check specified style
                if (anc_elem->specified_style) {
                    CssDeclaration* lsp_decl = style_tree_get_declaration(
                        anc_elem->specified_style, CSS_PROPERTY_LIST_STYLE_POSITION);
                    if (lsp_decl && lsp_decl->value && lsp_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                        CssEnum lsp_val = lsp_decl->value->data.keyword;
                        if (lsp_val == 1) {  // 1 = inside
                            is_inside_position = true;
                        }
                        break;  // found an explicit value, stop searching
                    }
                }
            }
        }

        // Check list-style-type (if 'none', no marker) — also inherited
        if (view_block->blk && view_block->blk->list_style_type == CSS_VALUE_NONE) {
            has_marker = false;
        } else {
            for (DomNode* anc = (DomNode*)element; anc; anc = anc->parent) {
                if (!anc->is_element()) continue;
                DomElement* anc_elem = anc->as_element();
                ViewBlock* anc_view = (ViewBlock*)anc_elem;
                if (anc_view->blk && anc_view->blk->list_style_type == CSS_VALUE_NONE) {
                    has_marker = false;
                    break;
                }
                if (anc_view->blk && anc_view->blk->list_style_type != 0) {
                    break;  // has a non-none type, stop
                }
                if (anc_elem->specified_style) {
                    CssDeclaration* lst_decl = style_tree_get_declaration(
                        anc_elem->specified_style, CSS_PROPERTY_LIST_STYLE_TYPE);
                    if (lst_decl && lst_decl->value && lst_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                        if (lst_decl->value->data.keyword == CSS_VALUE_NONE) {
                            has_marker = false;
                        }
                        break;  // found explicit value
                    }
                }
            }
        }

        if (is_inside_position && has_marker) {
            // Marker width = font_size * 1.375 (matching layout_block.cpp marker creation)
            float font_size = 16.0f;  // default
            if (view_block->font && view_block->font->font_size > 0) {
                font_size = view_block->font->font_size;
            } else if (lycon->font.current_font_size > 0) {
                font_size = lycon->font.current_font_size;
            }
            float marker_width = font_size * 1.375f;

            // The marker is the first inline box — add to inline content accumulators
            has_inline_content = true;
            inline_max_sum += marker_width;
            inline_min_sum = max(inline_min_sum, marker_width);
            log_debug("  list-item inside marker: added %.1fpx marker width (font=%.1f)",
                      marker_width, font_size);
        }
    }

    // CSS 2.1 §16.1: text-indent applies to the first formatted line of a block container.
    // Add text-indent to inline max-content width (it contributes to preferred width).
    float text_indent = 0.0f;
    if (has_inline_content && view_block->blk && view_block->blk->text_indent != 0.0f) {
        text_indent = view_block->blk->text_indent;
    } else if (has_inline_content && element->specified_style) {
        CssDeclaration* ti_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_TEXT_INDENT);
        if (ti_decl && ti_decl->value && ti_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            text_indent = resolve_length_value(lycon, CSS_PROPERTY_TEXT_INDENT, ti_decl->value);
        }
    }

    // Merge inline content measurements
    if (has_inline_content) {
        // CSS 2.1 §16.1: text-indent applies to the first formatted line.
        // Positive: increases both min-content and max-content first-line width.
        // Negative: reduces first-line width but cannot make it negative;
        //           subsequent lines are unaffected, so min-content uses max of
        //           (first-line-with-indent, widest-subsequent-line).
        if (text_indent > 0) {
            inline_max_sum += text_indent;
            inline_min_sum += text_indent;
        } else if (text_indent < 0) {
            // Negative text-indent: first line is narrower, but min/max content
            // cannot go below zero from the indent alone.
            inline_max_sum = fmaxf(inline_max_sum + text_indent, 0.0f);
            // For min-content with negative indent: the first word + indent might
            // still be wider than subsequent words, or it might not.
            // Use max of (first-line contribution, existing min without indent).
            float first_line_min = fmaxf(inline_min_sum + text_indent, 0.0f);
            inline_min_sum = fmaxf(first_line_min, sizes.min_content);
        }
        sizes.min_content = max(sizes.min_content, inline_min_sum);
        sizes.max_content = max(sizes.max_content, inline_max_sum);
        log_debug("  inline_max_sum=%.1f, inline_min_sum=%.1f, text_indent=%.1f",
                  inline_max_sum, inline_min_sum, text_indent);
    }

    // Add padding and border
    ViewBlock* view = (ViewBlock*)element;
    float pad_left = 0, pad_right = 0;
    float border_left = 0, border_right = 0;

    if (view->bound) {
        log_debug("measure_element_intrinsic: %s has bound allocated", element->node_name());
        if (view->bound->padding.left >= 0) pad_left = view->bound->padding.left;
        if (view->bound->padding.right >= 0) pad_right = view->bound->padding.right;
        if (view->bound->border) {
            border_left = view->bound->border->width.left;
            border_right = view->bound->border->width.right;
        }
    } else if (element->specified_style) {
        log_debug("measure_element_intrinsic: %s NO bound, using CSS fallback", element->node_name());
        // Fallback: read padding from CSS styles if bound hasn't been allocated yet
        CssDeclaration* pad_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING);
        if (pad_decl && pad_decl->value) {
            if (pad_decl->value->type == CSS_VALUE_TYPE_LIST && pad_decl->value->data.list.count >= 2) {
                // Multi-value padding: top right [bottom] [left]
                CssValue** values = pad_decl->value->data.list.values;
                pad_right = resolve_length_value(lycon, CSS_PROPERTY_PADDING, values[1]);
                pad_left = (pad_decl->value->data.list.count >= 4) ?
                    resolve_length_value(lycon, CSS_PROPERTY_PADDING, values[3]) : pad_right;
                log_debug("  -> multi-value padding: left=%.1f, right=%.1f", pad_left, pad_right);
            } else if (pad_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                // Single padding value (shorthand)
                float pad = resolve_length_value(lycon, CSS_PROPERTY_PADDING, pad_decl->value);
                pad_left = pad_right = pad;
                log_debug("  -> single padding value: %.1f", pad);
            }
        }

        // If no shorthand, check individual properties
        if (pad_left == 0 && pad_right == 0) {
            // Check individual padding properties
            CssDeclaration* pl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING_LEFT);
            if (pl && pl->value && pl->value->type == CSS_VALUE_TYPE_LENGTH) {
                pad_left = resolve_length_value(lycon, CSS_PROPERTY_PADDING_LEFT, pl->value);
            }
            CssDeclaration* pr = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING_RIGHT);
            if (pr && pr->value && pr->value->type == CSS_VALUE_TYPE_LENGTH) {
                pad_right = resolve_length_value(lycon, CSS_PROPERTY_PADDING_RIGHT, pr->value);
            }
        }

        // Also check for border-width (handles both single value and multi-value shorthand)
        bool border_width_found = false;
        CssDeclaration* border_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_BORDER_WIDTH);
        if (border_decl && border_decl->value) {
            if (border_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                // Single value: applies to all sides
                float border_width = resolve_length_value(lycon, CSS_PROPERTY_BORDER_WIDTH, border_decl->value);
                border_left = border_right = border_width;
                border_width_found = true;
            } else if (border_decl->value->type == CSS_VALUE_TYPE_LIST && border_decl->value->data.list.count >= 1) {
                // Multi-value: border-width: top right [bottom] [left]
                int cnt = border_decl->value->data.list.count;
                CssValue** vals = border_decl->value->data.list.values;
                if (cnt == 1) {
                    float bw = resolve_length_value(lycon, CSS_PROPERTY_BORDER_WIDTH, vals[0]);
                    border_left = border_right = bw;
                } else if (cnt == 2) {
                    // vals[1] = left/right
                    float lr = resolve_length_value(lycon, CSS_PROPERTY_BORDER_WIDTH, vals[1]);
                    border_left = border_right = lr;
                } else if (cnt == 3) {
                    // vals[1] = left/right
                    float lr = resolve_length_value(lycon, CSS_PROPERTY_BORDER_WIDTH, vals[1]);
                    border_left = border_right = lr;
                } else if (cnt >= 4) {
                    // vals[1] = right, vals[3] = left
                    border_right = resolve_length_value(lycon, CSS_PROPERTY_BORDER_WIDTH, vals[1]);
                    border_left = resolve_length_value(lycon, CSS_PROPERTY_BORDER_WIDTH, vals[3]);
                }
                border_width_found = true;
                log_debug("  -> multi-value border-width: left=%.1f, right=%.1f", border_left, border_right);
            }
        }
        // Fallback to individual border-*-width if shorthand not found
        // IMPORTANT: Only fall back when border-width was NOT found at all.
        // When border-width IS found (even with value 0), it takes precedence
        // over individual properties and the border shorthand per CSS cascade.
        if (!border_width_found && border_left == 0 && border_right == 0) {
            CssDeclaration* bl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_BORDER_LEFT_WIDTH);
            if (bl && bl->value && bl->value->type == CSS_VALUE_TYPE_LENGTH) {
                border_left = resolve_length_value(lycon, CSS_PROPERTY_BORDER_LEFT_WIDTH, bl->value);
            }
            CssDeclaration* br = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_BORDER_RIGHT_WIDTH);
            if (br && br->value && br->value->type == CSS_VALUE_TYPE_LENGTH) {
                border_right = resolve_length_value(lycon, CSS_PROPERTY_BORDER_RIGHT_WIDTH, br->value);
            }
        }
        // Check border-left/border-right side shorthands (border-left: width style color)
        // These store the value as a list with length, keyword (style), and color components
        if (!border_width_found && border_left == 0) {
            CssDeclaration* bl_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_BORDER_LEFT);
            if (bl_decl && bl_decl->value) {
                const CssValue* v = bl_decl->value;
                if (v->type == CSS_VALUE_TYPE_LENGTH) {
                    border_left = resolve_length_value(lycon, CSS_PROPERTY_BORDER_LEFT_WIDTH, v);
                } else if (v->type == CSS_VALUE_TYPE_LIST) {
                    for (int i = 0; i < v->data.list.count; i++) {
                        CssValue* item = v->data.list.values[i];
                        if (item && item->type == CSS_VALUE_TYPE_LENGTH) {
                            border_left = resolve_length_value(lycon, CSS_PROPERTY_BORDER_LEFT_WIDTH, item);
                            break;
                        }
                    }
                }
            }
        }
        if (!border_width_found && border_right == 0) {
            CssDeclaration* br_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_BORDER_RIGHT);
            if (br_decl && br_decl->value) {
                const CssValue* v = br_decl->value;
                if (v->type == CSS_VALUE_TYPE_LENGTH) {
                    border_right = resolve_length_value(lycon, CSS_PROPERTY_BORDER_RIGHT_WIDTH, v);
                } else if (v->type == CSS_VALUE_TYPE_LIST) {
                    for (int i = 0; i < v->data.list.count; i++) {
                        CssValue* item = v->data.list.values[i];
                        if (item && item->type == CSS_VALUE_TYPE_LENGTH) {
                            border_right = resolve_length_value(lycon, CSS_PROPERTY_BORDER_RIGHT_WIDTH, item);
                            break;
                        }
                    }
                }
            }
        }
        // Also check border shorthand (border: width style color)
        // Only if neither border-width nor individual properties were found
        if (!border_width_found && border_left == 0 && border_right == 0) {
            CssDeclaration* b_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_BORDER);
            if (b_decl && b_decl->value && b_decl->value->type == CSS_VALUE_TYPE_LIST &&
                b_decl->value->data.list.count >= 1) {
                CssValue* width_val = b_decl->value->data.list.values[0];
                if (width_val && width_val->type == CSS_VALUE_TYPE_LENGTH) {
                    float bw = resolve_length_value(lycon, CSS_PROPERTY_BORDER_WIDTH, width_val);
                    border_left = border_right = bw;
                }
            }
        }
    }

    int horiz_padding = (int)(pad_left + pad_right);
    int horiz_border = (int)(border_left + border_right);
    sizes.min_content += horiz_padding + horiz_border;
    sizes.max_content += horiz_padding + horiz_border;

    // CSS 2.1 §10.4: Apply min-width and max-width constraints to intrinsic sizes.
    // min-width sets a floor: an element can never be narrower than its min-width,
    // so its intrinsic sizes must reflect this constraint.
    // max-width constrains the preferred (max-content) width.
    ViewBlock* view_for_minmax = (ViewBlock*)element;
    bool view_is_border_box = view_for_minmax->blk &&
                              view_for_minmax->blk->box_sizing == CSS_VALUE_BORDER_BOX;

    // Apply min-width: floor the intrinsic sizes to ensure they're at least min-width
    float given_min_width = -1;
    if (view_for_minmax->blk) {
        given_min_width = view_for_minmax->blk->given_min_width;
    }
    // Fallback: check CSS if blk hasn't been set up yet
    if (given_min_width < 0 && element->specified_style) {
        CssDeclaration* min_mw_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_MIN_WIDTH);
        if (min_mw_decl && min_mw_decl->value &&
            min_mw_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            given_min_width = resolve_length_value(lycon, CSS_PROPERTY_MIN_WIDTH, min_mw_decl->value);
        }
    }
    if (given_min_width > 0) {
        // For border-box: min-width is the border-box total (includes padding+border)
        // For content-box: min-width is content width, so add padding+border for border-box comparison
        float min_border_box = view_is_border_box ? given_min_width
                                                  : (given_min_width + horiz_padding + horiz_border);
        if (sizes.min_content < min_border_box) {
            sizes.min_content = min_border_box;
            log_debug("  -> min-width constraint: raised min_content to %.1f", sizes.min_content);
        }
        if (sizes.max_content < min_border_box) {
            sizes.max_content = min_border_box;
            log_debug("  -> min-width constraint: raised max_content to %.1f", sizes.max_content);
        }
    }
    float given_max_width = -1;
    if (view_for_minmax->blk) {
        given_max_width = view_for_minmax->blk->given_max_width;
    }
    // Fallback: check CSS if blk hasn't been set up yet
    if (given_max_width < 0 && element->specified_style) {
        CssDeclaration* mw_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_MAX_WIDTH);
        if (mw_decl && mw_decl->value && mw_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            given_max_width = resolve_length_value(lycon, CSS_PROPERTY_MAX_WIDTH, mw_decl->value);
        }
    }
    if (given_max_width >= 0) {
        // For content-box: max-width is content width, so add padding+border for comparison
        float max_border_box = given_max_width + horiz_padding + horiz_border;
        if (sizes.max_content > max_border_box) {
            sizes.max_content = max_border_box;
            log_debug("  -> max-width constraint: clamped max_content to %.1f", sizes.max_content);
        }
        // CSS Tables §4.1: Table content-box inline size is never smaller than its
        // minimum content inline size, so don't clamp min_content for tables.
        if (!is_table_display && sizes.min_content > max_border_box) {
            sizes.min_content = max_border_box;
        }
    }

    // Restore parent font context
    if (font_changed) {
        lycon->font = saved_font;
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
            // Get text-transform from parent element (text inherits from parent)
            CssEnum text_transform = CSS_VALUE_NONE;
            CssEnum font_variant = CSS_VALUE_NONE;
            if (node->parent && node->parent->is_element()) {
                text_transform = get_element_text_transform(node->parent->as_element());
                font_variant = get_element_font_variant(node->parent->as_element());
            }
            TextIntrinsicWidths widths = measure_text_intrinsic_widths(
                lycon, text, strlen(text), text_transform, font_variant);
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
            // Get text-transform from parent element (text inherits from parent)
            CssEnum text_transform = CSS_VALUE_NONE;
            CssEnum font_variant = CSS_VALUE_NONE;
            if (node->parent && node->parent->is_element()) {
                text_transform = get_element_text_transform(node->parent->as_element());
                font_variant = get_element_font_variant(node->parent->as_element());
            }
            TextIntrinsicWidths widths = measure_text_intrinsic_widths(
                lycon, text, strlen(text), text_transform, font_variant);
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

    // For text nodes, estimate based on line height and text width
    if (node->is_text()) {
        // Check if text is whitespace-only (shouldn't contribute to height in block context)
        const char* text = (const char*)node->text_data();
        if (!text || *text == '\0') return 0;

        bool is_whitespace_only = true;
        for (const char* p = text; *p; p++) {
            if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                is_whitespace_only = false;
                break;
            }
        }
        if (is_whitespace_only) {
            return 0;  // Whitespace-only text doesn't contribute to height
        }

        // Calculate line height
        float font_size = 16.0f;
        if (lycon->font.style && lycon->font.style->font_size > 0) {
            font_size = lycon->font.style->font_size;
        }
        float line_height = font_size * 1.2f;  // default for line-height: normal

        // Check ancestor elements for resolved CSS line-height
        // Walk up the DOM tree since intermediate parents may not have blk resolved yet
        for (DomNode* ancestor = node->parent; ancestor; ancestor = ancestor->parent) {
            if (!ancestor->is_element()) continue;
            ViewBlock* anc_view = (ViewBlock*)ancestor->as_element();
            if (anc_view->blk && anc_view->blk->line_height) {
                const CssValue* lh = anc_view->blk->line_height;
                if (lh->type == CSS_VALUE_TYPE_NUMBER) {
                    line_height = font_size * (float)lh->data.number.value;
                } else if (lh->type == CSS_VALUE_TYPE_LENGTH) {
                    float lh_px = (float)lh->data.length.value;
                    if (lh_px > 0) line_height = lh_px;
                }
                break;
            }
            if (anc_view->specified_style) {
                CssDeclaration* lh_decl = style_tree_get_declaration(
                    anc_view->specified_style, CSS_PROPERTY_LINE_HEIGHT);
                if (lh_decl && lh_decl->value) {
                    if (lh_decl->value->type == CSS_VALUE_TYPE_NUMBER) {
                        line_height = font_size * (float)lh_decl->value->data.number.value;
                    } else if (lh_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                        float lh_px = resolve_length_value(lycon, CSS_PROPERTY_LINE_HEIGHT, lh_decl->value);
                        if (lh_px > 0) line_height = lh_px;
                    }
                    break;
                }
            }
        }

        // Estimate how many lines the text will take based on available width
        if (width > 0) {
            size_t text_len = strlen(text);
            // Get text-transform and font-variant from parent element (inherited)
            CssEnum text_transform = CSS_VALUE_NONE;
            CssEnum font_variant = CSS_VALUE_NONE;
            if (node->parent && node->parent->is_element()) {
                text_transform = get_element_text_transform(node->parent->as_element());
                font_variant = get_element_font_variant(node->parent->as_element());
            }
            // Measure text width using intrinsic sizing
            TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, text, text_len, text_transform, font_variant);
            float text_width = widths.max_content;

            // Calculate number of lines (rounded up)
            // white-space: nowrap/pre prevents text wrapping → always 1 line
            // Use min_content (widest word/break unit) to estimate word-boundary waste:
            // text wraps at word boundaries, so each line's effective width is
            // floor(available_width / word_width) * word_width, not the full available_width.
            int num_lines = 1;
            CssEnum ws_val = get_white_space_value(node);
            if (ws_val != CSS_VALUE_NOWRAP && ws_val != CSS_VALUE_PRE && text_width > width) {
                float effective_width = width;
                if (widths.min_content > 0 && widths.min_content <= width) {
                    int units_per_line = (int)(width / widths.min_content);
                    if (units_per_line > 0) {
                        effective_width = units_per_line * widths.min_content;
                    }
                }
                num_lines = (int)ceil(text_width / effective_width);
            }

            log_debug("calculate_max_content_height: text len=%zu, text_width=%.1f, available_width=%.1f, lines=%d",
                      text_len, text_width, width, num_lines);

            return line_height * num_lines;
        }

        return line_height;
    }

    // For elements, we'd need to do a full layout pass
    // For now, use a simplified estimation
    DomElement* element = node->as_element();
    if (!element) return 0;

    float height = 0;
    ViewBlock* view = (ViewBlock*)element;

    // Set up element's own font context for accurate text measurement.
    // child text nodes inherit lycon->font, so we must set the element's font
    // before processing children (mirrors the logic in measure_element_intrinsic_widths).
    FontBox saved_font_h = lycon->font;
    bool height_font_changed = false;
    if (view->font && lycon->ui_context) {
        setup_font(lycon->ui_context, &lycon->font, view->font);
        height_font_changed = true;
    } else if (element->specified_style && lycon->ui_context && lycon->font.style) {
        CssDeclaration* font_size_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_FONT_SIZE);
        if (font_size_decl && font_size_decl->value &&
            font_size_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            float resolved_size = resolve_length_value(lycon, CSS_PROPERTY_FONT_SIZE,
                                                       font_size_decl->value);
            if (resolved_size > 0 && fabsf(resolved_size - lycon->font.style->font_size) > 0.1f) {
                FontProp* tfp = alloc_font_prop(lycon);
                if (tfp) {
                    tfp->family = lycon->font.style ? lycon->font.style->family : nullptr;
                    tfp->font_size = resolved_size;
                    setup_font(lycon->ui_context, &lycon->font, tfp);
                    height_font_changed = true;
                }
            }
        }
    }

    // Check for explicit height from CSS (e.g., iframe with height: 580px)
    if (view->blk && view->blk->given_height > 0) {
        // Element has explicit height specified in CSS
        float explicit_height = view->blk->given_height;

        // Check box-sizing: if border-box, the height already includes padding/border
        // Only add padding/border for content-box (default)
        bool is_border_box = (view->blk->box_sizing == CSS_VALUE_BORDER_BOX);

        if (!is_border_box && view->bound) {
            // content-box: height is content only, add padding and border
            if (view->bound->padding.top >= 0) explicit_height += view->bound->padding.top;
            if (view->bound->padding.bottom >= 0) explicit_height += view->bound->padding.bottom;
            if (view->bound->border) {
                explicit_height += view->bound->border->width.top;
                explicit_height += view->bound->border->width.bottom;
            }
        }
        // For border-box, given_height already includes padding/border, return as-is

        log_debug("calculate_max_content_height: %s has explicit height=%.1f (box_sizing=%s)",
                  element->node_name(), explicit_height, is_border_box ? "border-box" : "content-box");
        return explicit_height;
    }

    // Also check specified_style for height declaration if not yet resolved
    if (element->specified_style) {
        CssDeclaration* height_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_HEIGHT);
        if (height_decl && height_decl->value &&
            height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            float explicit_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
            if (explicit_height > 0) {
                log_debug("calculate_max_content_height: %s has specified height=%.1f",
                          element->node_name(), explicit_height);
                return explicit_height;
            }
        }
    }

    // CSS 2.1 §10.6.2: Replaced element intrinsic height
    if (view->display.inner == RDT_DISPLAY_REPLACED) {
        uintptr_t elem_tag = element->tag();
        if (elem_tag == HTM_TAG_IMG) {
            if (view->embed && view->embed->img) {
                ImageSurface* img = view->embed->img;
                float img_height = (float)img->height;
                // If width is constrained, scale height proportionally
                if (width > 0 && width < img->width && img->width > 0) {
                    img_height = width * img->height / img->width;
                }
                log_debug("calculate_max_content_height: IMG intrinsic height=%.1f", img_height);
                return img_height;
            }
            // Try to load image
            const char* src_value = element->get_attribute("src");
            if (src_value && lycon->ui_context) {
                if (!view->embed) {
                    view->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
                }
                size_t src_len = strlen(src_value);
                StrBuf* src_buf = strbuf_new_cap(src_len);
                strbuf_append_str_n(src_buf, src_value, src_len);
                view->embed->img = load_image(lycon->ui_context, src_buf->str);
                strbuf_free(src_buf);
                if (view->embed->img) {
                    ImageSurface* img = view->embed->img;
                    float img_height = (float)img->height;
                    if (width > 0 && width < img->width && img->width > 0) {
                        img_height = width * img->height / img->width;
                    }
                    log_debug("calculate_max_content_height: IMG intrinsic height=%.1f (loaded)", img_height);
                    return img_height;
                }
            }
            const char* attr_h = element->get_attribute("height");
            if (attr_h) {
                int h = atoi(attr_h);
                if (h > 0) return (float)h;
            }
            return 30.0f;  // placeholder
        }
        else if (elem_tag == HTM_TAG_IFRAME || elem_tag == HTM_TAG_VIDEO || elem_tag == HTM_TAG_CANVAS) {
            return 150.0f;  // CSS default 300x150
        }
        else if (elem_tag == HTM_TAG_SVG) {
            // SVG replaced element: determine intrinsic height from attributes/viewBox
            float svg_width = 300.0f;   // CSS default
            float svg_height = 150.0f;  // CSS default
            bool has_w = false, has_h = false;

            const char* attr_w = element->get_attribute("width");
            const char* attr_h = element->get_attribute("height");
            if (attr_w) { float w = (float)atof(attr_w); if (w > 0) { svg_width = w; has_w = true; } }
            if (attr_h) { float h = (float)atof(attr_h); if (h > 0) { svg_height = h; has_h = true; } }

            if (!has_w && !has_h) {
                // try viewBox
                const char* viewbox = element->get_attribute("viewBox");
                if (viewbox) {
                    float vb_x, vb_y, vb_w, vb_h;
                    if (sscanf(viewbox, "%f %f %f %f", &vb_x, &vb_y, &vb_w, &vb_h) == 4) {
                        if (vb_w > 0 && vb_h > 0) { svg_width = vb_w; svg_height = vb_h; has_w = true; has_h = true; }
                    }
                }
            } else if (has_w && !has_h) {
                // width only - try viewBox for aspect ratio
                const char* viewbox = element->get_attribute("viewBox");
                if (viewbox) {
                    float vb_x, vb_y, vb_w, vb_h;
                    if (sscanf(viewbox, "%f %f %f %f", &vb_x, &vb_y, &vb_w, &vb_h) == 4 && vb_w > 0 && vb_h > 0) {
                        svg_height = svg_width * vb_h / vb_w;
                    }
                }
            } else if (!has_w && has_h) {
                // height only - try viewBox for aspect ratio
                const char* viewbox = element->get_attribute("viewBox");
                if (viewbox) {
                    float vb_x, vb_y, vb_w, vb_h;
                    if (sscanf(viewbox, "%f %f %f %f", &vb_x, &vb_y, &vb_w, &vb_h) == 4 && vb_w > 0 && vb_h > 0) {
                        svg_width = svg_height * vb_w / vb_h;
                    }
                }
            }

            // scale height if width is constrained
            if (width > 0 && width < svg_width && svg_width > 0) {
                svg_height = width * svg_height / svg_width;
            }

            log_debug("calculate_max_content_height: SVG intrinsic height=%.1f", svg_height);
            return svg_height;
        }
    }

    // SVG fallback: handle SVG elements even when display.inner is not yet resolved
    if (element->tag() == HTM_TAG_SVG) {
        float svg_width = 300.0f;
        float svg_height = 150.0f;
        const char* attr_w = element->get_attribute("width");
        const char* attr_h = element->get_attribute("height");
        bool has_w = false, has_h = false;
        if (attr_w) { float w = (float)atof(attr_w); if (w > 0) { svg_width = w; has_w = true; } }
        if (attr_h) { float h = (float)atof(attr_h); if (h > 0) { svg_height = h; has_h = true; } }
        if (!has_w && !has_h) {
            const char* viewbox = element->get_attribute("viewBox");
            if (viewbox) {
                float vb_x, vb_y, vb_w, vb_h;
                if (sscanf(viewbox, "%f %f %f %f", &vb_x, &vb_y, &vb_w, &vb_h) == 4 && vb_w > 0 && vb_h > 0) {
                    svg_width = vb_w; svg_height = vb_h;
                }
            }
        } else if (has_w && !has_h) {
            const char* viewbox = element->get_attribute("viewBox");
            if (viewbox) {
                float vb_x, vb_y, vb_w, vb_h;
                if (sscanf(viewbox, "%f %f %f %f", &vb_x, &vb_y, &vb_w, &vb_h) == 4 && vb_w > 0 && vb_h > 0) {
                    svg_height = svg_width * vb_h / vb_w;
                }
            }
        }
        if (width > 0 && width < svg_width && svg_width > 0) {
            svg_height = width * svg_height / svg_width;
        }
        log_debug("calculate_max_content_height: SVG (tag-based) intrinsic height=%.1f", svg_height);
        return svg_height;
    }

    // Check if this is a grid container - need to detect column count
    bool is_grid_container = false;
    int grid_column_count = 1;  // Default: single column = vertical stacking
    float grid_row_gap = 0;

    if (view->display.inner == CSS_VALUE_GRID) {
        is_grid_container = true;
    }
    // Also check specified_style for unresolved display
    if (!is_grid_container && element->specified_style) {
        CssDeclaration* display_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_DISPLAY);
        if (display_decl && display_decl->value &&
            display_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
            CssEnum dv = display_decl->value->data.keyword;
            if (dv == CSS_VALUE_GRID || dv == CSS_VALUE_INLINE_GRID) {
                is_grid_container = true;
            }
        }
    }

    if (is_grid_container) {
        // Get column count from grid-template-columns
        if (view->embed && view->embed->grid && view->embed->grid->grid_template_columns) {
            grid_column_count = view->embed->grid->grid_template_columns->track_count;
            log_debug("calculate_max_content_height: grid %s from embed, cols=%d",
                      element->node_name(), grid_column_count);
        }
        // Try specified_style for unresolved grid
        if (grid_column_count <= 1 && element->specified_style) {
            CssDeclaration* cols_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_GRID_TEMPLATE_COLUMNS);
            if (cols_decl && cols_decl->value) {
                log_debug("calculate_max_content_height: %s grid-template-columns value type=%d",
                          element->node_name(), cols_decl->value->type);
                // Count track values in the grid template
                if (cols_decl->value->type == CSS_VALUE_TYPE_LIST) {
                    // Check if list contains repeat() function
                    int list_count = cols_decl->value->data.list.count;
                    CssValue** list_values = cols_decl->value->data.list.values;
                    int total_cols = 0;
                    for (int i = 0; i < list_count; i++) {
                        CssValue* v = list_values[i];
                        if (!v) continue;
                        if (v->type == CSS_VALUE_TYPE_FUNCTION && v->data.function &&
                            v->data.function->name && strcmp(v->data.function->name, "repeat") == 0) {
                            // repeat(n, ...) - get the count
                            if (v->data.function->arg_count > 0 && v->data.function->args[0]) {
                                CssValue* count_val = v->data.function->args[0];
                                if (count_val->type == CSS_VALUE_TYPE_NUMBER) {
                                    int repeat_count = (int)count_val->data.number.value;
                                    int tracks_per_repeat = v->data.function->arg_count - 1;
                                    if (tracks_per_repeat < 1) tracks_per_repeat = 1;
                                    total_cols += repeat_count * tracks_per_repeat;
                                    log_debug("calculate_max_content_height: repeat(%d, %d tracks) = %d cols",
                                              repeat_count, tracks_per_repeat, repeat_count * tracks_per_repeat);
                                }
                            }
                        } else {
                            // Regular track (length, percentage, minmax function, etc.)
                            total_cols++;
                        }
                    }
                    grid_column_count = total_cols > 0 ? total_cols : list_count;
                } else if (cols_decl->value->type == CSS_VALUE_TYPE_FUNCTION) {
                    // Single function like repeat(2, 1fr)
                    CssFunction* func = cols_decl->value->data.function;
                    if (func && func->name && strcmp(func->name, "repeat") == 0) {
                        if (func->arg_count > 0 && func->args[0] &&
                            func->args[0]->type == CSS_VALUE_TYPE_NUMBER) {
                            int repeat_count = (int)func->args[0]->data.number.value;
                            int tracks_per_repeat = func->arg_count - 1;
                            if (tracks_per_repeat < 1) tracks_per_repeat = 1;
                            grid_column_count = repeat_count * tracks_per_repeat;
                            log_debug("calculate_max_content_height: single repeat(%d) = %d cols",
                                      repeat_count, grid_column_count);
                        }
                    }
                } else {
                    // Single track definition means 1 column
                    grid_column_count = 1;
                }
            }
        }
        // Get row gap
        if (view->embed && view->embed->grid) {
            grid_row_gap = view->embed->grid->row_gap;
        }
        if (grid_row_gap <= 0 && element->specified_style) {
            CssDeclaration* gap_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_ROW_GAP);
            if (!gap_decl) {
                gap_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_GAP);
            }
            if (gap_decl && gap_decl->value && gap_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                grid_row_gap = resolve_length_value(lycon, CSS_PROPERTY_GAP, gap_decl->value);
            }
        }
        log_debug("calculate_max_content_height: grid %s with %d columns, gap=%.1f",
                  element->node_name(), grid_column_count, grid_row_gap);
    }

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
    bool is_flex_wrap = false;
    float flex_row_gap = 0;
    float flex_column_gap = 0;
    bool is_flex_container = (view->display.inner == CSS_VALUE_FLEX);

    // Also check specified_style for unresolved display
    if (!is_flex_container && element->specified_style) {
        CssDeclaration* display_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_DISPLAY);
        if (display_decl && display_decl->value &&
            display_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
            CssEnum dv = display_decl->value->data.keyword;
            if (dv == CSS_VALUE_FLEX || dv == CSS_VALUE_INLINE_FLEX) {
                is_flex_container = true;
            }
        }
    }

    if (is_flex_container) {
        // Default flex direction is row, wrap is nowrap
        is_flex_row = true;
        is_flex_wrap = false;

        if (view->embed && view->embed->flex) {
            if (view->embed->flex->direction == DIR_ROW ||
                view->embed->flex->direction == DIR_ROW_REVERSE) {
                is_flex_row = true;
            } else {
                is_flex_row = false;
            }
            // Check for flex-wrap
            if (view->embed->flex->wrap == WRAP_WRAP ||
                view->embed->flex->wrap == WRAP_WRAP_REVERSE) {
                is_flex_wrap = true;
            }
            flex_row_gap = view->embed->flex->row_gap;
            flex_column_gap = view->embed->flex->column_gap;
        } else if (element->specified_style) {
            // Check specified_style for flex-direction
            CssDeclaration* dir_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_FLEX_DIRECTION);
            if (dir_decl && dir_decl->value && dir_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum dir = dir_decl->value->data.keyword;
                is_flex_row = (dir == CSS_VALUE_ROW || dir == CSS_VALUE_ROW_REVERSE);
            }
            // Check for flex-wrap
            CssDeclaration* wrap_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_FLEX_WRAP);
            if (wrap_decl && wrap_decl->value && wrap_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum wrap = wrap_decl->value->data.keyword;
                if (wrap == CSS_VALUE_WRAP || wrap == CSS_VALUE_WRAP_REVERSE) {
                    is_flex_wrap = true;
                }
            }
            // Check for gap
            CssDeclaration* gap_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_GAP);
            if (gap_decl && gap_decl->value && gap_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                float gap = resolve_length_value(lycon, CSS_PROPERTY_GAP, gap_decl->value);
                flex_row_gap = gap;
                flex_column_gap = gap;
            }
            // Check for row-gap
            CssDeclaration* row_gap_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_ROW_GAP);
            if (row_gap_decl && row_gap_decl->value && row_gap_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                flex_row_gap = resolve_length_value(lycon, CSS_PROPERTY_ROW_GAP, row_gap_decl->value);
            }
            // Check for column-gap
            CssDeclaration* col_gap_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_COLUMN_GAP);
            if (col_gap_decl && col_gap_decl->value && col_gap_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                flex_column_gap = resolve_length_value(lycon, CSS_PROPERTY_COLUMN_GAP, col_gap_decl->value);
            }
        }
    }

    // Check if this block element has only inline content (text and inline elements).
    // In that case, children flow inline on the same line(s), not stacked vertically.
    bool has_only_inline_content = false;

    // First check if display is resolved; if not, try to resolve it
    CssEnum display_inner = view->display.inner;
    if (display_inner == 0 && element->specified_style) {
        // Display not resolved yet, try to get from CSS
        CssDeclaration* display_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_DISPLAY);
        if (display_decl && display_decl->value &&
            display_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
            display_inner = display_decl->value->data.keyword;
            // block => flow layout
            if (display_inner == CSS_VALUE_BLOCK) {
                display_inner = CSS_VALUE_FLOW;
            }
        } else {
            // Default: block element uses flow layout
            display_inner = CSS_VALUE_FLOW;
        }
    }

    if (!is_grid_container && !is_flex_row && display_inner == CSS_VALUE_FLOW) {
        // Block element with flow layout - check if all children are inline
        has_only_inline_content = true;
        for (DomNode* c = element->first_child; c; c = c->next_sibling) {
            if (c->is_text()) {
                continue;  // Text nodes are inline
            }
            if (c->is_element()) {
                DomElement* child_elem = c->as_element();
                // Check if child is an inline element
                const char* child_tag = child_elem->node_name();
                if (child_tag && (
                    strcmp(child_tag, "a") == 0 ||
                    strcmp(child_tag, "span") == 0 ||
                    strcmp(child_tag, "strong") == 0 ||
                    strcmp(child_tag, "b") == 0 ||
                    strcmp(child_tag, "em") == 0 ||
                    strcmp(child_tag, "i") == 0 ||
                    strcmp(child_tag, "code") == 0 ||
                    strcmp(child_tag, "br") == 0 ||
                    strcmp(child_tag, "abbr") == 0 ||
                    strcmp(child_tag, "small") == 0 ||
                    strcmp(child_tag, "sub") == 0 ||
                    strcmp(child_tag, "sup") == 0)) {
                    continue;  // Known inline elements
                }
                // Check display.outer for inline
                if (child_elem->display.outer == CSS_VALUE_INLINE) {
                    continue;  // CSS says it's inline
                }
                // Found a block element
                has_only_inline_content = false;
                break;
            }
        }
        if (has_only_inline_content) {
            log_debug("calculate_max_content_height: %s has only inline content", element->node_name());
        }
    }

    // For multi-column grids, calculate height based on rows
    if (is_grid_container && grid_column_count > 1) {
        // Collect child heights
        int child_count = 0;
        for (DomNode* c = element->first_child; c; c = c->next_sibling) {
            if (c->is_element()) child_count++;
        }

        if (child_count > 0) {
            // Calculate number of rows
            int row_count = (child_count + grid_column_count - 1) / grid_column_count;

            // Collect heights and compute row-by-row max
            float* child_heights = (float*)alloca(child_count * sizeof(float));
            float col_share = width / grid_column_count;
            int idx = 0;
            for (DomNode* c = element->first_child; c; c = c->next_sibling) {
                if (c->is_element()) {
                    // Auto columns can't shrink below their min-content width,
                    // so use max(share, min_content) to get the actual column width.
                    float child_w = col_share;
                    DomElement* ce = (DomElement*)c;
                    IntrinsicSizes cs = measure_element_intrinsic_widths(lycon, ce);
                    if (cs.min_content > child_w) child_w = cs.min_content;
                    child_heights[idx++] = calculate_max_content_height(lycon, c, child_w);
                }
            }

            // Sum up max height of each row
            for (int row = 0; row < row_count; row++) {
                float row_max_height = 0;
                for (int col = 0; col < grid_column_count; col++) {
                    int item_idx = row * grid_column_count + col;
                    if (item_idx < child_count) {
                        row_max_height = fmax(row_max_height, child_heights[item_idx]);
                    }
                }
                height += row_max_height;
                if (row > 0) {
                    height += grid_row_gap;
                }
            }
            log_debug("calculate_max_content_height: grid %s rows=%d, total_height=%.1f",
                      element->node_name(), row_count, height);
        }
    } else if (is_flex_row && is_flex_wrap && width > 0) {
        // Special handling for wrapping flex row containers
        // Simulate how items wrap based on available width
        log_debug("calculate_max_content_height: wrapping flex row %s, available_width=%.1f, row_gap=%.1f, col_gap=%.1f",
                  element->node_name(), width, flex_row_gap, flex_column_gap);

        // Collect child widths and heights
        float current_line_width = 0;
        float current_line_height = 0;
        float total_height = 0;
        int line_count = 0;
        bool first_on_line = true;

        for (DomNode* child = element->first_child; child; child = child->next_sibling) {
            if (!child->is_element()) continue;

            // Get child's intrinsic width
            DomElement* child_elem = child->as_element();
            IntrinsicSizes child_sizes = measure_element_intrinsic_widths(lycon, child_elem);
            float child_width = child_sizes.max_content;  // Use max-content width for flex items
            float child_height = calculate_max_content_height(lycon, child, width);

            // Check if we need to wrap to a new line
            float width_with_gap = first_on_line ? child_width : (flex_column_gap + child_width);
            if (!first_on_line && current_line_width + width_with_gap > width) {
                // Wrap to new line
                total_height += current_line_height;
                if (line_count > 0) {
                    total_height += flex_row_gap;
                }
                line_count++;
                current_line_width = child_width;
                current_line_height = child_height;
                first_on_line = false;
            } else {
                // Add to current line
                current_line_width += width_with_gap;
                current_line_height = fmax(current_line_height, child_height);
                first_on_line = false;
            }
        }

        // Add the last line
        if (current_line_height > 0) {
            total_height += current_line_height;
            if (line_count > 0) {
                total_height += flex_row_gap;
            }
            line_count++;
        }

        height = total_height;
        log_debug("calculate_max_content_height: wrapping flex %s, lines=%d, height=%.1f",
                  element->node_name(), line_count, height);
    } else {
        // Calculate children's heights (original logic for non-grid or single-column)

        // Determine the element's own content width for children's percentage resolution.
        // The `width` parameter is the available width from the parent (containing block),
        // but if this element has an explicit CSS width, children resolve percentages
        // against this element's content width, not the grandparent's.
        float content_w = width;
        if (element->specified_style) {
            CssDeclaration* w_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_WIDTH);
            if (w_decl && w_decl->value) {
                if (w_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE && width > 0) {
                    content_w = (float)(w_decl->value->data.percentage.value / 100.0) * width;
                } else if (w_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                    content_w = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, w_decl->value);
                }
            }
        }

        for (DomNode* child = element->first_child; child; child = child->next_sibling) {
            float child_height = calculate_max_content_height(lycon, child, content_w);

            // For grid/flex containers, child margins don't collapse —
            // add child's margin-top/bottom (percentage resolves against containing block width)
            if ((is_grid_container || is_flex_container) && child->is_element() && content_w > 0) {
                DomElement* child_elem = child->as_element();
                if (child_elem->specified_style) {
                    float mt = 0, mb = 0;
                    CssDeclaration* mt_decl = style_tree_get_declaration(child_elem->specified_style, CSS_PROPERTY_MARGIN_TOP);
                    if (mt_decl && mt_decl->value) {
                        if (mt_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE)
                            mt = (float)(mt_decl->value->data.percentage.value / 100.0) * content_w;
                        else if (mt_decl->value->type == CSS_VALUE_TYPE_LENGTH)
                            mt = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_TOP, mt_decl->value);
                    }
                    CssDeclaration* mb_decl = style_tree_get_declaration(child_elem->specified_style, CSS_PROPERTY_MARGIN_BOTTOM);
                    if (mb_decl && mb_decl->value) {
                        if (mb_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE)
                            mb = (float)(mb_decl->value->data.percentage.value / 100.0) * content_w;
                        else if (mb_decl->value->type == CSS_VALUE_TYPE_LENGTH)
                            mb = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_BOTTOM, mb_decl->value);
                    }
                    if (mt == 0 && mb == 0) {
                        CssDeclaration* m_decl = style_tree_get_declaration(child_elem->specified_style, CSS_PROPERTY_MARGIN);
                        if (m_decl && m_decl->value) {
                            if (m_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                                float m = (float)(m_decl->value->data.percentage.value / 100.0) * content_w;
                                mt = mb = m;
                            } else if (m_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                                float m = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, m_decl->value);
                                mt = mb = m;
                            }
                        }
                    }
                    child_height += mt + mb;
                }
            }

            if (is_grid_column_flow || is_flex_row || has_only_inline_content) {
                // Items are laid out horizontally - take max height
                height = fmax(height, child_height);
            } else {
                // Items are stacked vertically - sum heights
                height += child_height;
            }
        }
    }

    // Add padding and border
    float pad_top = 0, pad_bottom = 0;
    float border_top = 0, border_bottom = 0;

    // CSS percentage padding (top/bottom) resolves against the containing block's WIDTH.
    // During intrinsic sizing, view->bound may have padding=0 from stylesheet defaults
    // while specified_style has an unresolved percentage override. Check specified_style
    // for percentage padding FIRST, since percentages must be resolved against 'width'.
    bool resolved_pad_from_pct = false;
    if (element->specified_style && width > 0) {
        CssDeclaration* pt = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING_TOP);
        if (pt && pt->value && pt->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
            pad_top = (float)(pt->value->data.percentage.value / 100.0) * width;
            resolved_pad_from_pct = true;
        }
        CssDeclaration* pb = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING_BOTTOM);
        if (pb && pb->value && pb->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
            pad_bottom = (float)(pb->value->data.percentage.value / 100.0) * width;
            resolved_pad_from_pct = true;
        }
        if (!resolved_pad_from_pct) {
            CssDeclaration* pad_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING);
            if (pad_decl && pad_decl->value && pad_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                float pad = (float)(pad_decl->value->data.percentage.value / 100.0) * width;
                pad_top = pad_bottom = pad;
                resolved_pad_from_pct = true;
            }
        }
    }

    if (!resolved_pad_from_pct) {
        if (view->bound) {
            if (view->bound->padding.top >= 0) pad_top = view->bound->padding.top;
            if (view->bound->padding.bottom >= 0) pad_bottom = view->bound->padding.bottom;
        } else if (element->specified_style) {
            CssDeclaration* pad_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING);
            if (pad_decl && pad_decl->value && pad_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                float pad = resolve_length_value(lycon, CSS_PROPERTY_PADDING, pad_decl->value);
                pad_top = pad_bottom = pad;
            } else {
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
    }

    if (view->bound && view->bound->border) {
        border_top = view->bound->border->width.top;
        border_bottom = view->bound->border->width.bottom;
    }

    height += pad_top + pad_bottom + border_top + border_bottom;

    if (height_font_changed) lycon->font = saved_font_h;
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
// Unified Intrinsic Sizing API Implementation (Section 4.2)
// ============================================================================

IntrinsicSizesBidirectional measure_intrinsic_sizes(
    LayoutContext* lycon,
    ViewBlock* element,
    AvailableSpace available_space
) {
    IntrinsicSizesBidirectional result = {0, 0, 0, 0};

    if (!lycon || !element) {
        return result;
    }

    DomNode* node = (DomNode*)element;

    // Step 1: Measure width intrinsic sizes
    // Width measurement doesn't depend on available width
    result.min_content_width = calculate_min_content_width(lycon, node);
    result.max_content_width = calculate_max_content_width(lycon, node);

    // Step 2: Determine the width to use for height measurement
    // Height depends on width due to text wrapping
    float width_for_height;

    if (available_space.width.is_definite()) {
        // Use the definite available width for BOTH height measurements
        // This is critical for grid row sizing where the column width is known
        width_for_height = available_space.width.value;
    } else if (available_space.width.is_min_content()) {
        // Use min-content width for height calculation
        width_for_height = result.min_content_width;
    } else {
        // MaxContent or Indefinite: use max-content width
        width_for_height = result.max_content_width;
    }

    // Step 3: Measure height intrinsic sizes at the determined width
    // IMPORTANT: When a definite width is available, use it for BOTH min and max height
    // This matches the original grid behavior where both heights are computed at the same width
    result.min_content_height = calculate_min_content_height(lycon, node, width_for_height);
    result.max_content_height = calculate_max_content_height(lycon, node, width_for_height);

    log_debug("measure_intrinsic_sizes: %s -> width(min=%.1f, max=%.1f), height(min=%.1f, max=%.1f) at width=%.1f",
              element->node_name(),
              result.min_content_width, result.max_content_width,
              result.min_content_height, result.max_content_height,
              width_for_height);

    return result;
}

// ============================================================================
// Table Cell Intrinsic Width Measurement
// ============================================================================

CellIntrinsicWidths measure_table_cell_intrinsic_widths(
    LayoutContext* lycon,
    ViewBlock* cell
) {
    CellIntrinsicWidths result = {0, 0};

    if (!lycon || !cell) {
        return result;
    }

    // Use unified API with max-content available space
    AvailableSpace available = AvailableSpace::make_max_content();

    IntrinsicSizesBidirectional sizes = measure_intrinsic_sizes(lycon, cell, available);

    result.min_width = sizes.min_content_width;
    result.max_width = sizes.max_content_width;

    // Apply minimum usable width per CSS 2.1
    if (result.min_width < 16.0f) result.min_width = 16.0f;
    if (result.max_width < 16.0f) result.max_width = 16.0f;

    return result;
}

// ============================================================================
// Backward Compatibility Notes
// ============================================================================
//
// The following functions are now wrappers or remain for backward compatibility:
// - calculate_min_content_width() - use measure_intrinsic_sizes().min_content_width
// - calculate_max_content_width() - use measure_intrinsic_sizes().max_content_width
// - calculate_min_content_height() - use measure_intrinsic_sizes().min_content_height
// - calculate_max_content_height() - use measure_intrinsic_sizes().max_content_height
// - measure_element_intrinsic_widths() - use measure_intrinsic_sizes() for width
//
// These will be gradually deprecated in favor of the unified API.
// ============================================================================
