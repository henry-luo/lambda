/**
 * Unified Intrinsic Sizing Implementation for Radiant Layout Engine
 *
 * This is the SINGLE SOURCE OF TRUTH for min-content and max-content calculations.
 * Table, flex, and grid layouts should ALL use these functions.
 */

#include "intrinsic_sizing.hpp"
#include "layout_flex.hpp"  // For FlexDirection enum
#include "grid.hpp"         // For GridTrackList
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

        // Check for zero-width space (U+200B) - UTF-8 encoding: 0xE2 0x80 0x8B
        // This is a break opportunity with no width
        if (ch == 0xE2 && i + 2 < length && str[i+1] == 0x80 && str[i+2] == 0x8B) {
            // Zero-width space is a break opportunity
            longest_word = fmax(longest_word, current_word);
            current_word = 0.0f;
            prev_glyph = 0;
            i += 2;  // Skip remaining bytes of zero-width space
            continue;
        }

        // Word boundary detection (whitespace breaks words)
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            longest_word = fmax(longest_word, current_word);
            current_word = 0.0f;

            // Get space glyph for kerning continuity (matching layout_text.cpp)
            FT_UInt space_glyph = FT_Get_Char_Index(lycon->font.ft_face, ch);

            // Apply kerning between prev character and space (matching layout_text.cpp)
            if (has_kerning && prev_glyph && space_glyph) {
                FT_Vector kern;
                FT_Get_Kerning(lycon->font.ft_face, prev_glyph, space_glyph,
                              FT_KERNING_DEFAULT, &kern);
                total_width += kern.x / 64.0f;
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

            // Apply letter-spacing (CSS spec: applied between characters, not after last)
            // Check if there are more characters after this one
            if (i + 1 < length && lycon->font.style) {
                advance += lycon->font.style->letter_spacing;
            }

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
    int inline_min_sum = 0;  // Sum of min-content widths for inline children
    int inline_max_sum = 0;  // Sum of max-content widths for inline children
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
                bool in_whitespace = true;  // Start as if preceded by whitespace (trims leading)
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
                // Trim trailing whitespace
                while (out_pos > 0 && normalized_buffer[out_pos - 1] == ' ') {
                    out_pos--;
                }
                normalized_buffer[out_pos] = '\0';

                // Skip if all whitespace (out_pos == 0)
                if (out_pos == 0) {
                    continue;
                }

                TextIntrinsicWidths text_widths = measure_text_intrinsic_widths(
                    lycon, normalized_buffer, out_pos);
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
            // For block-level children: take max of each
            sizes.min_content = max(sizes.min_content, child_sizes.min_content);
            sizes.max_content = max(sizes.max_content, child_sizes.max_content);
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
        log_debug("  inline_max_sum=%d, inline_min_sum=%d", inline_max_sum, inline_min_sum);
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
            // Measure text width using intrinsic sizing
            TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, text, text_len);
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
        // Add padding and border
        if (view->bound) {
            if (view->bound->padding.top >= 0) explicit_height += view->bound->padding.top;
            if (view->bound->padding.bottom >= 0) explicit_height += view->bound->padding.bottom;
            if (view->bound->border) {
                explicit_height += view->bound->border->width.top;
                explicit_height += view->bound->border->width.bottom;
            }
        }
        log_debug("calculate_max_content_height: %s has explicit height=%.1f", 
                  element->node_name(), explicit_height);
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
    } else {
        // Calculate children's heights (original logic for non-grid or single-column)
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
