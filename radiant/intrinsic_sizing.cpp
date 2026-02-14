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

// FreeType for fallback glyph loading via load_glyph
#include <ft2build.h>
#include FT_FREETYPE_H

#include "../lib/log.h"
// str.h included via view.hpp
#include <cmath>
#include <cstring>

// ============================================================================
// Text Measurement (Core Implementation)
// ============================================================================

TextIntrinsicWidths measure_text_intrinsic_widths(LayoutContext* lycon,
                                                   const char* text,
                                                   size_t length,
                                                   CssEnum text_transform) {
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

    FT_UInt prev_glyph = 0;
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
            // Apply letter-spacing to spaces as well (matching layout_text.cpp)
            // letter-spacing is applied after each character including spaces, except the last
            if (i + 1 < length && lycon->font.style) {
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
        is_word_start = false;  // No longer at word start after first character

        // Get glyph index for the (possibly transformed) codepoint
        uint32_t glyph_index = font_get_glyph_index(lycon->font.font_handle, codepoint);
        if (!glyph_index) {
            // Glyph not found in primary font - try font fallback via load_glyph
            // This ensures intrinsic sizing uses the same fallback fonts as layout_text.cpp
            FT_GlyphSlot glyph = (FT_GlyphSlot)load_glyph(lycon->ui_context, lycon->font.font_handle, lycon->font.style, codepoint, false);
            if (glyph) {
                // Font is loaded at physical pixel size, so advance is in physical pixels
                // Divide by pixel_ratio to convert back to CSS pixels for layout
                float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0) ? lycon->ui_context->pixel_ratio : 1.0f;
                float advance = glyph->advance.x / 64.0f / pixel_ratio;
                // Apply letter-spacing
                if (i + bytes < length && lycon->font.style) {
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
            float advance = ginfo.advance_x + kerning;

            // Apply letter-spacing (CSS spec: applied between characters, not after last)
            // Check if there are more characters after this one
            if (i + bytes < length && lycon->font.style) {
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
static CssEnum get_element_text_transform(DomElement* element) {
    DomNode* node = element;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            // First check resolved blk property
            ViewBlock* view = (ViewBlock*)elem;
            if (view->blk && view->blk->text_transform != 0 &&
                view->blk->text_transform != CSS_VALUE_INHERIT) {
                return view->blk->text_transform;
            }
            // Fall back to specified_style
            if (elem->specified_style) {
                CssDeclaration* decl = style_tree_get_declaration(
                    elem->specified_style, CSS_PROPERTY_TEXT_TRANSFORM);
                if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                    CssEnum val = decl->value->data.keyword;
                    if (val != CSS_VALUE_INHERIT && val != CSS_VALUE_NONE) {
                        return val;
                    }
                }
            }
        }
        node = node->parent;
    }
    return CSS_VALUE_NONE;
}

IntrinsicSizes measure_element_intrinsic_widths(LayoutContext* lycon, DomElement* element) {
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
        // Element has CSS styles but font not yet resolved - extract font-family from CSS
        CssDeclaration* font_family_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_FONT_FAMILY);

        if (font_family_decl && font_family_decl->value) {
            // Create temporary FontProp from CSS using alloc_font_prop for stable memory
            FontProp* temp_font_prop = alloc_font_prop(lycon);  // Allocates from pool
            const char* css_family = NULL;

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

                // Also check for font-size
                CssDeclaration* font_size_decl = style_tree_get_declaration(
                    element->specified_style, CSS_PROPERTY_FONT_SIZE);
                if (font_size_decl && font_size_decl->value &&
                    font_size_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                    temp_font_prop->font_size = resolve_length_value(lycon, CSS_PROPERTY_FONT_SIZE,
                                                                     font_size_decl->value);
                }

                setup_font(lycon->ui_context, &lycon->font, temp_font_prop);
                font_changed = true;
            }
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

    // Check for explicit CSS width first
    if (element->specified_style) {
        CssDeclaration* width_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_WIDTH);
        if (width_decl && width_decl->value &&
            width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            int explicit_width = (int)resolve_length_value(lycon, CSS_PROPERTY_WIDTH,
                                                            width_decl->value);
            if (explicit_width > 0) {
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
                    // Read border from CSS
                    CssDeclaration* border_decl = style_tree_get_declaration(
                        element->specified_style, CSS_PROPERTY_BORDER);
                    if (border_decl && border_decl->value) {
                        // border shorthand: width style color
                        if (border_decl->value->type == CSS_VALUE_TYPE_LIST &&
                            border_decl->value->data.list.count >= 1) {
                            CssValue* width_val = border_decl->value->data.list.values[0];
                            if (width_val && width_val->type == CSS_VALUE_TYPE_LENGTH) {
                                float bw = resolve_length_value(lycon, CSS_PROPERTY_BORDER_WIDTH, width_val);
                                border_left = border_right = bw;
                            }
                        }
                    }
                    explicit_width += (int)(pad_left + pad_right + border_left + border_right);
                    log_debug("  -> explicit width: %d (after adding padding=%.0f+%.0f, border=%.0f+%.0f)",
                              explicit_width, pad_left, pad_right, border_left, border_right);
                } else {
                    log_debug("  -> explicit width: %d (border-box, no adjustment)", explicit_width);
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
    if (is_flex_container) {
        // Default flex-direction is row
        is_row_flex = true;  // Assume row by default
        if (view_block->embed && view_block->embed->flex) {
            int dir = view_block->embed->flex->direction;
            is_row_flex = (dir == CSS_VALUE_ROW || dir == CSS_VALUE_ROW_REVERSE ||
                          dir == DIR_ROW || dir == DIR_ROW_REVERSE);
            flex_gap = view_block->embed->flex->column_gap;
        } else if (element->specified_style) {
            // Check specified_style for flex-direction
            CssDeclaration* dir_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_FLEX_DIRECTION);
            if (dir_decl && dir_decl->value && dir_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum dir = dir_decl->value->data.keyword;
                is_row_flex = (dir == CSS_VALUE_ROW || dir == CSS_VALUE_ROW_REVERSE);
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
        log_debug("measure_element_intrinsic_widths: %s is_flex=%d, is_row_flex=%d, gap=%.1f",
                  element->node_name(), is_flex_container, is_row_flex, flex_gap);
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

                TextIntrinsicWidths text_widths = measure_text_intrinsic_widths(
                    lycon, normalized_buffer, out_pos, text_transform);
                child_sizes.min_content = text_widths.min_content;
                child_sizes.max_content = text_widths.max_content;

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
                }
            }
        }

        // Handle flex container children - all children become flex items
        // In a flex container, both text and element children are flex items
        // They should NOT go through the inline content path
        if (is_flex_container) {
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

    // Merge inline content measurements
    if (has_inline_content) {
        sizes.min_content = max(sizes.min_content, inline_min_sum);
        sizes.max_content = max(sizes.max_content, inline_max_sum);
        log_debug("  inline_max_sum=%.1f, inline_min_sum=%.1f", inline_max_sum, inline_min_sum);
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

        // Also check for border
        CssDeclaration* border_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_BORDER_WIDTH);
        if (border_decl && border_decl->value && border_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            float border_width = resolve_length_value(lycon, CSS_PROPERTY_BORDER_WIDTH, border_decl->value);
            border_left = border_right = border_width;
        } else {
            CssDeclaration* bl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_BORDER_LEFT_WIDTH);
            if (bl && bl->value && bl->value->type == CSS_VALUE_TYPE_LENGTH) {
                border_left = resolve_length_value(lycon, CSS_PROPERTY_BORDER_LEFT_WIDTH, bl->value);
            }
            CssDeclaration* br = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_BORDER_RIGHT_WIDTH);
            if (br && br->value && br->value->type == CSS_VALUE_TYPE_LENGTH) {
                border_right = resolve_length_value(lycon, CSS_PROPERTY_BORDER_RIGHT_WIDTH, br->value);
            }
        }
    }

    int horiz_padding = (int)(pad_left + pad_right);
    int horiz_border = (int)(border_left + border_right);
    sizes.min_content += horiz_padding + horiz_border;
    sizes.max_content += horiz_padding + horiz_border;

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
            if (node->parent && node->parent->is_element()) {
                text_transform = get_element_text_transform(node->parent->as_element());
            }
            TextIntrinsicWidths widths = measure_text_intrinsic_widths(
                lycon, text, strlen(text), text_transform);
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
            if (node->parent && node->parent->is_element()) {
                text_transform = get_element_text_transform(node->parent->as_element());
            }
            TextIntrinsicWidths widths = measure_text_intrinsic_widths(
                lycon, text, strlen(text), text_transform);
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
        float line_height = 20.0f;  // Default
        if (lycon->font.style && lycon->font.style->font_size > 0) {
            line_height = lycon->font.style->font_size * 1.2f;  // Typical line-height
        }

        // Estimate how many lines the text will take based on available width
        if (width > 0) {
            size_t text_len = strlen(text);
            // Get text-transform from parent element (text inherits from parent)
            CssEnum text_transform = CSS_VALUE_NONE;
            if (node->parent && node->parent->is_element()) {
                text_transform = get_element_text_transform(node->parent->as_element());
            }
            // Measure text width using intrinsic sizing
            TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, text, text_len, text_transform);
            float text_width = widths.max_content;

            // Calculate number of lines (rounded up)
            int num_lines = 1;
            if (text_width > width) {
                num_lines = (int)ceil(text_width / width);
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
            int idx = 0;
            for (DomNode* c = element->first_child; c; c = c->next_sibling) {
                if (c->is_element()) {
                    child_heights[idx++] = calculate_max_content_height(lycon, c, width / grid_column_count);
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
        for (DomNode* child = element->first_child; child; child = child->next_sibling) {
            float child_height = calculate_max_content_height(lycon, child, width);

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
