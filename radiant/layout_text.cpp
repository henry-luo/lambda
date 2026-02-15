#include "layout.hpp"
#include "layout_positioned.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_style.hpp"
#include "../lib/avl_tree.h"
#include "../lib/font/font.h"

#include "../lib/log.h"
#include <chrono>
#include <cctype>
#include <cwctype>
using namespace std::chrono;

// External timing accumulators from layout.cpp
extern double g_text_layout_time;
extern int64_t g_text_layout_count;

// ============================================================================
// CSS text-transform Helpers
// ============================================================================

/**
 * Apply CSS text-transform to a single Unicode codepoint.
 * @param codepoint Input Unicode codepoint
 * @param text_transform CSS text-transform value (CSS_VALUE_UPPERCASE, etc.)
 * @param is_word_start True if this is the first character of a word (for capitalize)
 * @return Transformed codepoint
 */
uint32_t apply_text_transform(uint32_t codepoint, CssEnum text_transform, bool is_word_start) {
    if (text_transform == CSS_VALUE_UPPERCASE) {
        // Convert to uppercase
        if (codepoint < 128) {
            return std::toupper(codepoint);
        } else {
            return std::towupper(codepoint);
        }
    } else if (text_transform == CSS_VALUE_LOWERCASE) {
        // Convert to lowercase
        if (codepoint < 128) {
            return std::tolower(codepoint);
        } else {
            return std::towlower(codepoint);
        }
    } else if (text_transform == CSS_VALUE_CAPITALIZE && is_word_start) {
        // Capitalize first letter of each word
        if (codepoint < 128) {
            return std::toupper(codepoint);
        } else {
            return std::towupper(codepoint);
        }
    }
    return codepoint;
}

/**
 * Get text-transform property from block.
 * @param blk BlockProp structure (can be NULL)
 * @return CSS text-transform value or CSS_VALUE_NONE
 */
CssEnum get_text_transform_from_block(BlockProp* blk) {
    if (blk && blk->text_transform != 0 && blk->text_transform != CSS_VALUE_INHERIT) {
        return blk->text_transform;
    }
    return CSS_VALUE_NONE;
}

/**
 * Get text-transform property from the layout context.
 * Checks block property for the current element or parent elements.
 */
static inline CssEnum get_text_transform(LayoutContext* lycon) {
    // Check parent chain for text-transform property (it's inherited)
    DomNode* node = lycon->elmt ? lycon->elmt : lycon->view;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = (DomElement*)node;
            CssEnum transform = get_text_transform_from_block(elem->blk);
            if (transform != CSS_VALUE_NONE) {
                return transform;
            }
        }
        node = node->parent;
    }
    return CSS_VALUE_NONE;
}

/**
 * Get word-break property from the layout context.
 * Checks block property for the current element or parent elements.
 */
static inline CssEnum get_word_break(LayoutContext* lycon) {
    // Check parent chain for word-break property (it's inherited)
    DomNode* node = lycon->elmt ? lycon->elmt : lycon->view;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = (DomElement*)node;
            if (elem->blk && elem->blk->word_break != 0) {
                return elem->blk->word_break;
            }
        }
        node = node->parent;
    }
    return CSS_VALUE_NORMAL;  // Default to normal
}

// ============================================================================
// CSS white-space Property Helpers
// ============================================================================

/**
 * Check if a codepoint is a CJK character that allows line breaks.
 * CJK text can break between any two characters.
 * Covers: Chinese (Hanzi), Japanese (Kanji/Hiragana/Katakana), Korean (Hangul)
 */
static inline bool is_cjk_character(uint32_t codepoint) {
    return (codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||  // CJK Unified Ideographs
           (codepoint >= 0x3400 && codepoint <= 0x4DBF) ||  // CJK Extension A
           (codepoint >= 0x20000 && codepoint <= 0x2A6DF) || // CJK Extension B
           (codepoint >= 0x2A700 && codepoint <= 0x2B73F) || // CJK Extension C
           (codepoint >= 0x2B740 && codepoint <= 0x2B81F) || // CJK Extension D
           (codepoint >= 0x2B820 && codepoint <= 0x2CEAF) || // CJK Extension E
           (codepoint >= 0x3040 && codepoint <= 0x309F) ||  // Hiragana
           (codepoint >= 0x30A0 && codepoint <= 0x30FF) ||  // Katakana
           (codepoint >= 0xAC00 && codepoint <= 0xD7AF);    // Hangul Syllables
}

/**
 * Get the Unicode-specified width for special space characters.
 * These characters have fixed widths defined by Unicode standard, which browsers
 * enforce regardless of what the font's glyph metrics say.
 * Returns the width as a fraction of 1em, or 0 if the character doesn't have
 * a Unicode-specified width. Returns -1 for zero-width characters.
 *
 * Reference: Unicode Standard, Chapter 6 "Writing Systems and Punctuation"
 */
static inline float get_unicode_space_width_em(uint32_t codepoint) {
    switch (codepoint) {
        // Zero-width characters (return negative to distinguish from "use font width")
        case 0x200B: return -1.0f;  // Zero Width Space (ZWSP) - break opportunity
        case 0x200C: return -1.0f;  // Zero Width Non-Joiner (ZWNJ)
        case 0x200D: return -1.0f;  // Zero Width Joiner (ZWJ)
        case 0xFEFF: return -1.0f;  // Zero Width No-Break Space (ZWNBSP / BOM)

        // Unicode spaces with defined widths
        case 0x2000: return 0.5f;   // EN QUAD - width of 'n' (nominally 1/2 em)
        case 0x2001: return 1.0f;   // EM QUAD - width of 'm' (nominally 1 em)
        case 0x2002: return 0.5f;   // EN SPACE - 1/2 em
        case 0x2003: return 1.0f;   // EM SPACE - 1 em
        case 0x2004: return 1.0f/3; // THREE-PER-EM SPACE - 1/3 em
        case 0x2005: return 0.25f;  // FOUR-PER-EM SPACE - 1/4 em
        case 0x2006: return 1.0f/6; // SIX-PER-EM SPACE - 1/6 em
        case 0x2009: return 1.0f/5; // THIN SPACE - ~1/5 em (or 1/6 em)
        case 0x200A: return 1.0f/10; // HAIR SPACE - very thin (~1/10 to 1/16 em)
        // U+2007 FIGURE SPACE and U+2008 PUNCTUATION SPACE are font-dependent
        // so we return 0 to use the font's glyph width
        default: return 0.0f;
    }
}

/**
 * Check if whitespace should be collapsed according to white-space property.
 * Returns true for: normal, nowrap, pre-line
 * Returns false for: pre, pre-wrap, break-spaces
 */
static inline bool ws_collapse_spaces(CssEnum ws) {
    return ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_NOWRAP ||
           ws == CSS_VALUE_PRE_LINE || ws == 0;  // 0 = undefined, treat as normal
}

/**
 * Check if newlines should be collapsed (treated as spaces).
 * Returns true for: normal, nowrap
 * Returns false for: pre, pre-wrap, pre-line, break-spaces
 */
static inline bool ws_collapse_newlines(CssEnum ws) {
    return ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_NOWRAP || ws == 0;
}

/**
 * Check if lines should wrap at soft break opportunities.
 * Returns true for: normal, pre-wrap, pre-line, break-spaces
 * Returns false for: nowrap, pre
 */
static inline bool ws_wrap_lines(CssEnum ws) {
    return ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_PRE_WRAP ||
           ws == CSS_VALUE_PRE_LINE || ws == CSS_VALUE_BREAK_SPACES || ws == 0;
}

/**
 * Check if a white-space value is concrete (not inherit/initial/unset/revert).
 * These special values need to be resolved by walking up the parent chain.
 */
static inline bool is_concrete_white_space_value(CssEnum ws) {
    return ws != CSS_VALUE_INHERIT &&
           ws != CSS_VALUE_INITIAL &&
           ws != CSS_VALUE_UNSET &&
           ws != CSS_VALUE_REVERT;
}

/**
 * Get the white-space property value from the text node's ancestor chain.
 * Walks up from the text node to find the nearest element with a white_space value set.
 * This properly handles inline elements like <span style="white-space: pre">.
 *
 * white-space is an inherited property, so we check:
 * 1. The resolved blk->white_space (for block elements)
 * 2. Skip INHERIT/INITIAL/UNSET/REVERT and continue walking up
 */
CssEnum get_white_space_value(DomNode* node) {
    // Walk up parent chain starting from the text node's parent
    DomNode* current = node ? node->parent : nullptr;
    while (current) {
        // PDF and other non-DOM view trees may have non-element parents
        // Only process if it's a proper DomElement
        if (!current->is_element()) {
            // Not a DomElement - this can happen with PDF view trees
            // Return default white-space value
            return CSS_VALUE_NORMAL;
        }
        DomElement* elem = static_cast<DomElement*>(current);
        // Check resolved BlockProp first (fastest path for blocks)
        if (elem->blk && elem->blk->white_space != 0) {
            CssEnum ws = elem->blk->white_space;
            // Skip INHERIT/INITIAL/UNSET/REVERT - continue walking up
            if (is_concrete_white_space_value(ws)) {
                return ws;
            }
        }
        current = current->parent;
    }
    return CSS_VALUE_NORMAL;  // default
}

// ============================================================================
// Intrinsic Sizing Mode Helpers
// ============================================================================

/**
 * Check if layout is in min-content measurement mode.
 * In min-content mode, break at every opportunity (every word boundary).
 */
static inline bool is_min_content_mode(LayoutContext* lycon) {
    return lycon->available_space.width.is_min_content();
}

/**
 * Check if layout is in max-content measurement mode.
 * In max-content mode, never break lines - measure full unwrapped width.
 */
static inline bool is_max_content_mode(LayoutContext* lycon) {
    return lycon->available_space.width.is_max_content();
}

/**
 * Check if line should break based on intrinsic sizing mode.
 * Returns true if line is full and should break.
 *
 * In min-content mode: always break at word boundaries
 * In max-content mode: never break (infinite available width)
 * In normal mode: break when line is full
 */
static inline bool should_break_line(LayoutContext* lycon, float current_x, float width) {
    if (is_max_content_mode(lycon)) {
        // Never break in max-content mode
        return false;
    }
    // Use effective_right which accounts for float intrusions
    // effective_right is adjusted per-line based on floats at current Y
    float line_right = lycon->line.has_float_intrusion ?
                       lycon->line.effective_right : lycon->line.right;
    if (is_min_content_mode(lycon)) {
        // In min-content mode, we break at every opportunity
        return current_x + width > line_right;
    }
    // Normal mode: check if line is full
    return current_x + width > line_right;
}

// ============================================================================
// BlockContext-aware Line Adjustment
// ============================================================================

/**
 * Update effective line bounds based on floats in the current BlockContext.
 * Called at line start and potentially mid-line when floats are encountered.
 *
 * Uses the new unified BlockContext API instead of the old BFC system.
 */
void update_line_for_bfc_floats(LayoutContext* lycon) {
    // Find the BFC root for this layout context
    BlockContext* bfc = block_context_find_bfc(&lycon->block);

    if (!bfc) {
        // No BFC - effective bounds same as normal bounds
        lycon->line.effective_left = lycon->line.left;
        lycon->line.effective_right = lycon->line.right;
        lycon->line.has_float_intrusion = false;
        return;
    }

    // Get current view
    ViewBlock* current_view = (ViewBlock*)lycon->view;
    if (!current_view || !current_view->is_block()) {
        lycon->line.effective_left = lycon->line.left;
        lycon->line.effective_right = lycon->line.right;
        lycon->line.has_float_intrusion = false;
        return;
    }

    // Use cached BFC offset from BlockContext
    float offset_x = lycon->block.bfc_offset_x;
    float offset_y = lycon->block.bfc_offset_y;

    float current_y_local = lycon->block.advance_y;
    float current_y_bfc = current_y_local + offset_y;
    float line_height = lycon->block.line_height > 0 ? lycon->block.line_height : 16.0f;

    log_debug("  DEBUG: line adjustment, y_local=%.1f, offset_y=%.1f, y_bfc=%.1f",
        current_y_local, offset_y, current_y_bfc);

    // Query available space at this Y using BlockContext API (in BFC coordinates)
    FloatAvailableSpace space = block_context_space_at_y(bfc, current_y_bfc, line_height);

    // Convert from BFC coordinates to local coordinates
    float local_space_left = space.left - offset_x;
    float local_space_right = space.right - offset_x;

    // Clamp to block's content area
    float local_left = fmax(local_space_left, lycon->line.left);
    float local_right = fmin(local_space_right, lycon->line.right);

    // Update effective bounds
    if (local_left > lycon->line.left || local_right < lycon->line.right) {
        lycon->line.effective_left = local_left;
        lycon->line.effective_right = local_right;
        lycon->line.has_float_intrusion = true;

        // If advance_x is before effective_left, move it
        if (lycon->line.advance_x < lycon->line.effective_left) {
            lycon->line.advance_x = lycon->line.effective_left;
        }

        log_debug("[BlockContext] Line adjusted for floats: effective (%.1f, %.1f), y_bfc=%.1f",
                  lycon->line.effective_left, lycon->line.effective_right, current_y_bfc);
    } else {
        lycon->line.effective_left = lycon->line.left;
        lycon->line.effective_right = lycon->line.right;
        lycon->line.has_float_intrusion = false;
    }
}

// Forward declarations
LineFillStatus node_has_line_filled(LayoutContext* lycon, DomNode* node);
LineFillStatus text_has_line_filled(LayoutContext* lycon, DomNode* text_node);
LineFillStatus span_has_line_filled(LayoutContext* lycon, DomNode* span) {
    DomNode* node = nullptr;
    if (span->is_element()) {
        node = static_cast<DomElement*>(span)->first_child;
    }
    if (node) {
        LineFillStatus result = node_has_line_filled(lycon, node);
        if (result) { return result; }
    }
    return RDT_NOT_SURE;
}

void line_reset(LayoutContext* lycon) {
    log_debug("initialize new line");
    lycon->line.max_ascender = lycon->line.max_descender = 0;
    lycon->line.is_line_start = true;  lycon->line.has_space = false;
    lycon->line.last_space = NULL;  lycon->line.last_space_pos = 0;  lycon->line.last_space_is_hyphen = false;
    lycon->line.start_view = NULL;
    lycon->line.line_start_font = lycon->font;
    lycon->line.prev_glyph_index = 0; // reset kerning state

    // IMPORTANT: Reset effective bounds to container bounds before float adjustment
    // line.left/right are the container bounds, set once in line_init()
    // effective_left/right are recalculated per line based on floats at that Y
    lycon->line.effective_left = lycon->line.left;
    lycon->line.effective_right = lycon->line.right;
    lycon->line.has_float_intrusion = false;
    lycon->line.advance_x = lycon->line.left;  // Start at container left

    // CSS 2.1 §16.1: text-indent applies only to the first formatted line of a block container
    // Apply text-indent offset to advance_x for the first line only
    if (lycon->block.is_first_line && lycon->block.text_indent != 0) {
        lycon->line.advance_x += lycon->block.text_indent;
        lycon->line.effective_left += lycon->block.text_indent;
        log_debug("Applied text-indent: %.1fpx, advance_x=%.1f",
                  lycon->block.text_indent, lycon->line.advance_x);
        // After applying text-indent for the first line, mark it as done
        lycon->block.is_first_line = false;
    }

    // Adjust effective bounds for floats at current Y position using BlockContext
    BlockContext* bfc = block_context_find_bfc(&lycon->block);
    if (bfc) {
        // Use unified line adjustment via BlockContext
        adjust_line_for_floats(lycon);
        log_debug("DEBUG: Used BlockContext %p for line adjustment", (void*)bfc);
    }
}

void line_init(LayoutContext* lycon, float left, float right) {
    lycon->line.left = left;  lycon->line.right = right;
    // Initialize effective bounds to full width (will be adjusted for floats later)
    lycon->line.effective_left = left;
    lycon->line.effective_right = right;
    lycon->line.has_float_intrusion = false;
    line_reset(lycon);
    lycon->line.vertical_align = CSS_VALUE_BASELINE;  // vertical-align does not inherit
}

void line_break(LayoutContext* lycon) {
    lycon->block.max_width = max(lycon->block.max_width, lycon->line.advance_x);

    if (lycon->line.max_ascender > lycon->block.init_ascender ||
        lycon->line.max_descender > lycon->block.init_descender) {
        // apply vertical alignment
        log_debug("apply vertical adjustment for the line");
        View* view = lycon->line.start_view;
        if (view) {
            FontBox pa_font = lycon->font;
            lycon->font = lycon->line.line_start_font;
            bool end_of_line = false;
            NEXT_VIEW:
            View * vw = view;
            do {
                view_vertical_align(lycon, vw);
                if (vw == lycon->view) { break; } // reached the last view in the line
                vw = vw->next();
            } while (vw);
            if (vw != lycon->view) { // need to go parent level
                view = view->parent;
                if (view) { view = view->next(); }
                if (view) goto NEXT_VIEW;
            }
            lycon->font = pa_font;
        }
    }
    // else no change to vertical alignment

    // horizontal text alignment
    line_align(lycon);

    // advance to next line
    // CSS 2.1 10.8.1: Line height controls vertical spacing between line boxes
    // When line-height is explicitly set (e.g., line-height: 1), use it exactly
    // even if it's smaller than font metrics (allowing lines to overlap)
    float font_line_height = lycon->line.max_ascender + lycon->line.max_descender;
    float css_line_height = lycon->block.line_height;

    // If css_line_height is not set (0 or negative), use font-based line height
    // This ensures text in elements without explicit line-height gets proper spacing
    if (css_line_height <= 0) {
        css_line_height = font_line_height;
    }

    // Check if we have mixed font sizes by comparing font height to CSS height
    // Only expand for mixed fonts when the larger font significantly exceeds line-height
    bool has_mixed_fonts = (font_line_height > css_line_height + 2); // 2px tolerance
    float used_line_height;

    if (has_mixed_fonts) {
        // Mixed font sizes - use max to accommodate larger fonts
        // CSS 2.1: "The height of a line box is determined by the rules given in the section on line height calculations"
        used_line_height = max(css_line_height, font_line_height);
    } else {
        // Uniform font size - use CSS line height exactly as specified
        // CSS 2.1: The computed line-height is used for line box spacing
        // Even if lines overlap (line-height < font metrics), this is correct CSS behavior
        used_line_height = css_line_height;
    }

    // printf("DEBUG: Line advance - font: %d, css: %d, mixed: %s, used: %d\n",
    //        font_line_height, css_line_height, has_mixed_fonts ? "yes" : "no", used_line_height);
    lycon->block.advance_y += used_line_height;

    // CSS 2.1 10.8.1: Track last line's ascender for inline-block baseline alignment
    // The baseline of an inline-block with in-flow content is the baseline of its last line box
    lycon->block.last_line_ascender = lycon->line.max_ascender;

    // reset the new line
    line_reset(lycon);
}

LineFillStatus text_has_line_filled(LayoutContext* lycon, DomNode* text_node) {
    // Get text data using helper function
    const char* text = (const char*)text_node->text_data();
    if (!text) return RDT_LINE_NOT_FILLED;  // null check

    unsigned char* str = (unsigned char*)text;
    unsigned char* text_end = str + strlen(text);
    float text_width = 0.0f;
    CssEnum text_transform = get_text_transform(lycon);
    bool is_word_start = true;  // First character is always word start

    do {
        if (is_space(*str)) return RDT_LINE_NOT_FILLED;

        // Get the codepoint and apply text-transform
        uint32_t codepoint = *str;
        if (codepoint >= 128) {
            int bytes = str_utf8_decode((const char*)str, (size_t)(text_end - str), &codepoint);
            if (bytes <= 0) codepoint = *str;
        }
        codepoint = apply_text_transform(codepoint, text_transform, is_word_start);
        is_word_start = false;  // Only first char is word start in this context

        // Check for Unicode space characters with defined widths
        float unicode_space_em = get_unicode_space_width_em(codepoint);
        if (unicode_space_em < 0.0f) {
            // Zero-width character - skip with no width contribution
            // (e.g., U+200B ZWSP, U+FEFF ZWNBSP/BOM)
        } else if (unicode_space_em > 0.0f) {
            // Use Unicode-specified width (fraction of em)
            text_width += unicode_space_em * lycon->font.current_font_size;
        } else {
            // get glyph advance via font module (returns CSS pixels, no FT_Face needed)
            GlyphInfo ginfo = font_get_glyph(lycon->font.font_handle, codepoint);
            if (ginfo.id == 0) {
                fprintf(stderr, "Could not load character (codepoint: %u)\n", codepoint);
                return RDT_LINE_NOT_FILLED;
            }
            text_width += ginfo.advance_x;
        }
        // Apply letter-spacing (but not after the last character)
        str++;
        if (*str) {  // Not the last character
            text_width += lycon->font.style->letter_spacing;
        }
        // Use effective_right which accounts for float intrusions
        float line_right = lycon->line.has_float_intrusion ?
                           lycon->line.effective_right : lycon->line.right;
        if (lycon->line.advance_x + text_width > line_right) { // line filled up
            return RDT_LINE_FILLED;
        }
    } while (*str);  // end of text
    // Note: Do NOT update advance_x here - this is a lookahead check only.
    // The actual advance_x update happens during real text layout.
    return RDT_NOT_SURE;
}

// check node and its siblings to see if line is filled
LineFillStatus node_has_line_filled(LayoutContext* lycon, DomNode* node) {
    do {
        if (node->is_text()) {
            LineFillStatus result = text_has_line_filled(lycon, node);
            if (result) { return result; }
        }
        else if (node->is_element()) {
            CssEnum outer_display = resolve_display_value(node).outer;
            if (outer_display == CSS_VALUE_BLOCK) { return RDT_LINE_NOT_FILLED; }
            else if (outer_display == CSS_VALUE_INLINE) {
                LineFillStatus result = span_has_line_filled(lycon, node);
                if (result) { return result; }
            }
        }
        else {
            log_debug("unknown node type");
            // skip the node
        }
        node = node->next_sibling;
    } while (node);
    return RDT_NOT_SURE;
}

// check view and its parents/siblings to see if line is filled
LineFillStatus view_has_line_filled(LayoutContext* lycon, View* view) {
    // note: this function navigates to parenets through laid out view tree,
    // and siblings through non-processed html nodes
    log_debug("check if view has line filled");
    DomNode* node = view->next_sibling;
    if (node) {
        LineFillStatus result = node_has_line_filled(lycon, node);
        if (result) { return result; }
    }
    // check at parent level
    view = (View*)view->parent;
    if (view) {
        if (view->view_type == RDT_VIEW_BLOCK) { return RDT_LINE_NOT_FILLED; }
        else if (view->view_type == RDT_VIEW_INLINE) {
            return view_has_line_filled(lycon, view);
        }
        log_debug("unknown view type");
    }
    return RDT_NOT_SURE;
}

void output_text(LayoutContext* lycon, ViewText* text, TextRect* rect, int text_length, float text_width) {
    assert(text_length > 0);
    rect->length = text_length;
    rect->width = text_width;
    lycon->line.advance_x += text_width;
    // Use OS/2 sTypo metrics only when USE_TYPO_METRICS flag is set (Chrome behavior)
    TypoMetrics typo = get_os2_typo_metrics(lycon->font.font_handle);
    if (typo.valid && typo.use_typo_metrics) {
        lycon->line.max_ascender = max(lycon->line.max_ascender, typo.ascender);
        lycon->line.max_descender = max(lycon->line.max_descender, typo.descender);
    } else if (lycon->font.font_handle) {
        const FontMetrics* m = font_get_metrics(lycon->font.font_handle);
        if (m) {
            lycon->line.max_ascender = max(lycon->line.max_ascender, m->hhea_ascender);
            lycon->line.max_descender = max(lycon->line.max_descender, -(m->hhea_descender));
        }
    }
    log_debug("text rect: '%.*t', x %f, y %f, width %f, height %f, font size %f, font family '%s'",
        text_length, text->text_data() + rect->start_index, rect->x, rect->y, rect->width, rect->height, text->font->font_size, text->font->family);

    if (text->rect == rect) {  // first rect
        text->x = rect->x;
        text->y = rect->y;
        text->width = rect->width;
        text->height = rect->height;
    } else {  // following rects after first rect
        float right = max(text->x + text->width, rect->x + rect->width);
        float bottom = max(text->y + text->height, rect->y + rect->height);
        text->x = min(text->x, rect->x);
        text->y = min(text->y, rect->y);
        text->width = right - text->x;
        text->height = bottom - text->y;
    }
}

void adjust_text_bounds(ViewText* text) {
    TextRect* rect = text->rect;
    if (!rect) return;
    text->x = rect->x;
    text->y = rect->y;
    text->width = rect->width;
    text->height = rect->height;
    rect = rect->next;
    while (rect) {
        float right = max(text->x + text->width, rect->x + rect->width);
        float bottom = max(text->y + text->height, rect->y + rect->height);
        text->x = min(text->x, rect->x);
        text->y = min(text->y, rect->y);
        text->width = right - text->x;
        text->height = bottom - text->y;
        rect = rect->next;
    }
}

void layout_text(LayoutContext* lycon, DomNode *text_node) {
    auto t_start = high_resolution_clock::now();

    unsigned char* next_ch;  ViewText* text_view = null;  TextRect* prev_rect = NULL;
    unsigned char* text_start = text_node->text_data();
    if (!text_start) return;  // null check for text data
    unsigned char* str = text_start;
    unsigned char* text_end = text_start + strlen((const char*)text_start);

    // Clear any existing text rects from previous layout passes (e.g., table measurement)
    // This prevents accumulation of duplicate rects when the same node is laid out multiple times
    if (text_node->view_type == RDT_VIEW_TEXT) {
        ViewText* existing_view = (ViewText*)text_node;
        if (existing_view->rect) {
            log_debug("clearing existing text rects for re-layout");
            existing_view->rect = nullptr;  // pool memory will be reused
        }
    }

    // Get white-space property from the text node's ancestor chain
    // This properly handles inline elements like <span style="white-space: pre">
    CssEnum white_space = get_white_space_value(text_node);  // todo: white-space should be put in BlockContext
    bool collapse_spaces = ws_collapse_spaces(white_space);
    bool collapse_newlines = ws_collapse_newlines(white_space);
    bool wrap_lines = ws_wrap_lines(white_space);

    // Get word-break property for CJK line breaking
    CssEnum word_break = get_word_break(lycon);
    bool break_all = (word_break == CSS_VALUE_BREAK_ALL);  // Can break between any characters
    bool keep_all = (word_break == CSS_VALUE_KEEP_ALL);    // Don't break CJK between letters

    // Get text-transform property
    CssEnum text_transform = get_text_transform(lycon);
    bool is_word_start = true;  // Track word boundaries for capitalize

    log_debug("layout_text: white-space=%d, collapse_spaces=%d, collapse_newlines=%d, wrap_lines=%d, text-transform=%d",
              white_space, collapse_spaces, collapse_newlines, wrap_lines, text_transform);

    // skip space at start of line (only if collapsing spaces)
    if (collapse_spaces && (lycon->line.is_line_start || lycon->line.has_space) && is_space(*str)) {
        // When collapsing spaces, skip all whitespace (including newlines if collapse_newlines)
        while (is_space(*str) && (collapse_newlines || (*str != '\n' && *str != '\r'))) {
            str++;
        }
        if (!*str) {
            // todo: probably should still set it bounds
            text_node->view_type = RDT_VIEW_NONE;
            log_debug("skipping whitespace text node");
            return;
        }
    }
    LAYOUT_TEXT:
    // Check if we're already past the line end before starting new text
    // This can happen after an inline-block that's wider than the container
    {
        float line_right = lycon->line.has_float_intrusion ?
                           lycon->line.effective_right : lycon->line.right;
        // Only break if we're strictly past the end, not just at the end
        // Being exactly at the end is fine - whitespace might be collapsed
        if (lycon->line.advance_x > line_right && !lycon->line.is_line_start) {
            log_debug("Text starts past line end (advance_x=%.1f > line_right=%.1f), breaking line",
                      lycon->line.advance_x, line_right);
            line_break(lycon);
        }
    }
    if (!text_view) {
        text_view = (ViewText*)set_view(lycon, RDT_VIEW_TEXT, text_node);
        text_view->font = lycon->font.style;
    }

    // if font-size is 0, create zero-size text rect and return
    if (lycon->font.style && lycon->font.style->font_size <= 0.0f) {
        TextRect* rect = (TextRect*)pool_calloc(lycon->doc->view_tree->pool, sizeof(TextRect));
        if (!text_view->rect) {
            text_view->rect = rect;
        } else {
            TextRect* last_rect = text_view->rect;
            while (last_rect && last_rect->next) { last_rect = last_rect->next; }
            last_rect->next = rect;
        }
        rect->start_index = 0;
        rect->length = strlen((char*)text_start);
        rect->x = lycon->line.advance_x;
        rect->y = lycon->block.advance_y;
        rect->width = 0.0f;
        rect->height = 0.0f;
        return;
    }

    TextRect* rect = (TextRect*)pool_calloc(lycon->doc->view_tree->pool, sizeof(TextRect));
    if (!text_view->rect) {
        text_view->rect = rect;
    } else {
        TextRect* last_rect = text_view->rect;;
        while (last_rect && last_rect->next) { last_rect = last_rect->next; }
        last_rect->next = rect;
    }
    rect->start_index = str - text_start;
    // FreeType metrics are in physical pixels, divide by pixel_ratio for CSS pixels
    float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0) ? lycon->ui_context->pixel_ratio : 1.0f;
    float font_height = font_get_metrics(lycon->font.font_handle)->hhea_line_height;
    rect->x = lycon->line.advance_x;
    // browser text rect height uses font metrics (ascent+descent), NOT CSS line-height
    // CSS line-height affects line spacing/positioning, but text rect height is font-based
    // Use platform-specific metrics (CoreText on macOS) for accurate ascent+descent
    rect->height = font_get_cell_height(lycon->font.font_handle);

    // Text rect y-position based on vertical alignment
    // CSS half-leading model: text is centered within the line box
    // When line-height < font height, half_leading can be negative (text extends above line box)
    // Use FreeType font_height for half-leading calculation (consistent with lead_y calculation)
    if (lycon->line.vertical_align == CSS_VALUE_MIDDLE) {
        log_debug("middle-aligned-text: font %f, line %f", font_height, lycon->block.line_height);
        rect->y = lycon->block.advance_y + (lycon->block.line_height - font_height) / 2;
    }
    else if (lycon->line.vertical_align == CSS_VALUE_BOTTOM) {
        log_debug("bottom-aligned-text: font %f, line %f", font_height, lycon->block.line_height);
        rect->y = lycon->block.advance_y + lycon->block.line_height - font_height;
    }
    else if (lycon->line.vertical_align == CSS_VALUE_TOP) {
        log_debug("top-aligned-text");
        rect->y = lycon->block.advance_y;
    }
    else { // baseline - use half-leading model
        // Calculate half-leading based on FreeType metrics for consistency with lead_y
        // Allow negative half-leading only when line-height is explicitly less than font height
        // (e.g., line-height: 1em with large fonts). For normal line-height >= font_height,
        // use clamped lead_y (compatible with table cell vertical alignment).
        if (lycon->block.line_height < font_height) {
            // Explicit tight line-height: text extends above line box
            float half_leading = (lycon->block.line_height - font_height) / 2;
            rect->y = lycon->block.advance_y + half_leading;
        } else {
            // Normal case: use clamped lead_y (non-negative)
            rect->y = lycon->block.advance_y + lycon->block.lead_y;
        }
    }
    log_debug("layout text: '%t', start_index %d, x: %f, y: %f, advance_y: %f, lead_y: %f, font_face: '%s', font_size: %f",
        str, rect->start_index, rect->x, rect->y, lycon->block.advance_y, lycon->block.lead_y, lycon->font.style->family, lycon->font.style->font_size);

    // layout the text glyphs
    do {
        float wd;
        uint32_t codepoint = *str;

        // Handle newlines as forced line breaks when not collapsing newlines
        if (!collapse_newlines && (*str == '\n' || *str == '\r')) {
            // CSS 2.2: When preserving newlines with collapsing spaces (pre-line),
            // any spaces/tabs immediately before the newline should be removed
            // Check if we have trailing whitespace to strip
            if (collapse_spaces && str > text_start + rect->start_index) {
                // Walk back to find trailing spaces before this newline
                const unsigned char* check = str - 1;
                float trailing_width = 0;
                while (check >= text_start + rect->start_index && is_space(*check)) {
                    trailing_width += lycon->font.style->space_width;
                    check--;
                }
                if (trailing_width > 0) {
                    rect->width -= trailing_width;
                    log_debug("stripped trailing whitespace before newline: width reduced by %f", trailing_width);
                }
            }
            // Output any text before the newline
            if (str > text_start + rect->start_index) {
                output_text(lycon, text_view, rect, str - text_start - rect->start_index, rect->width);
            }
            // Handle CRLF as single line break
            if (*str == '\r' && *(str + 1) == '\n') {
                str += 2;
            } else {
                str++;
            }
            line_break(lycon);
            if (*str) {
                is_word_start = true;  // Reset word boundary after line break
                goto LAYOUT_TEXT;
            }
            else return;
        }

        if (is_space(codepoint)) {
            wd = lycon->font.style->space_width;
            // Apply letter-spacing to spaces as well (but not if it's the last character)
            if (str[1]) {  // Check if there's a next character
                wd += lycon->font.style->letter_spacing;
            }
            is_word_start = true;  // Next non-space char is word start
        }
        else {
            if (codepoint >= 128) { // unicode char
                int bytes = str_utf8_decode((const char*)str, (size_t)(text_end - str), &codepoint);
                if (bytes <= 0) { // invalid utf8 char
                    next_ch = str + 1;  codepoint = 0;
                }
                else { next_ch = str + bytes; }
            }
            else { next_ch = str + 1; }

            // Apply text-transform before loading glyph
            codepoint = apply_text_transform(codepoint, text_transform, is_word_start);
            is_word_start = false;  // No longer at word start

            // Check for Unicode space characters with defined widths
            float unicode_space_em = get_unicode_space_width_em(codepoint);
            if (unicode_space_em < 0.0f) {
                // Zero-width character (e.g., U+200B ZWSP, U+FEFF ZWNBSP/BOM)
                // Skip with no width contribution, just advance the string pointer
                str = next_ch;
                lycon->line.is_line_start = false;
                lycon->line.has_space = false;
                continue;  // Skip to next character without adding width
            } else if (unicode_space_em > 0.0f) {
                // Use Unicode-specified width (fraction of em)
                wd = unicode_space_em * lycon->font.current_font_size;
            } else {
                FontStyleDesc _sd = font_style_desc_from_prop(lycon->font.style);
                LoadedGlyph* glyph = font_load_glyph(lycon->font.font_handle, &_sd, codepoint, false);
                // Font is loaded at physical pixel size, so advance is in physical pixels
                // Divide by pixel_ratio to convert back to CSS pixels for layout
                float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0) ? lycon->ui_context->pixel_ratio : 1.0f;
                wd = glyph ? (glyph->advance_x / pixel_ratio) : lycon->font.style->space_width;
            }
            // Apply letter-spacing (add to character width)
            // Note: letter-spacing is NOT applied after the last character (CSS spec)
            // We'll check if next character exists before adding letter-spacing
            if (next_ch && *next_ch) {
                wd += lycon->font.style->letter_spacing;
            }
        }
        // handle kerning
        if (lycon->font.style->has_kerning) {
            uint32_t glyph_index = font_get_glyph_index(lycon->font.font_handle, codepoint);
            if (lycon->line.prev_glyph_index) {
                float kerning_css = font_get_kerning_by_index(lycon->font.font_handle, lycon->line.prev_glyph_index, glyph_index);
                if (kerning_css != 0.0f) {
                    if (str == text_start + rect->start_index) {
                        rect->x += kerning_css;
                    }
                    else {
                        rect->width += kerning_css;
                    }
                    log_debug("apply kerning: %f to char '%c'", kerning_css, *str);
                }
            }
            lycon->line.prev_glyph_index = glyph_index;
        }
        log_debug("layout char: '%c', x: %f, width: %f, wd: %f, line right: %f",
            *str == '\n' || *str == '\r' ? '^' : *str, rect->x, rect->width, wd, lycon->line.right);
        rect->width += wd;
        // Use effective_right which accounts for float intrusions
        float line_right = lycon->line.has_float_intrusion ?
                           lycon->line.effective_right : lycon->line.right;
        if (wrap_lines && rect->x + rect->width > line_right) { // line filled up and wrapping enabled
            log_debug("line filled up");
            if (is_space(*str)) { // break at the current space
                log_debug("break on space");
                // skip spaces according to white-space mode
                if (collapse_spaces) {
                    do { str++; } while (is_space(*str) && (collapse_newlines || (*str != '\n' && *str != '\r')));
                } else {
                    str++;  // only skip the current space in pre-wrap mode
                }
                rect->width -= wd;  // minus away space width at line break
                output_text(lycon, text_view, rect, str - text_start - rect->start_index, rect->width);
                line_break(lycon);
                log_debug("after space line break");
                if (*str) { goto LAYOUT_TEXT; }
                else return;
            }
            else if (lycon->line.last_space) { // break at the last space
                log_debug("break at last space");
                if (text_start <= lycon->line.last_space && lycon->line.last_space < str) {
                    str = lycon->line.last_space + 1;
                    output_text(lycon, text_view, rect, str - text_start - rect->start_index, lycon->line.last_space_pos);
                    line_break(lycon);  goto LAYOUT_TEXT;
                }
                else { // last_space outside the text, break at start of text
                    float advance_x = lycon->line.advance_x;  // save current advance_x
                    line_break(lycon);
                    rect->y = lycon->block.advance_y;
                    rect->x = lycon->line.advance_x;  lycon->line.advance_x = advance_x;
                    // continue the text flow
                }
            }
            // CSS 2.1 §9.5: "If a shortened line box is too small to contain any content,
            // then the line box is shifted downward until either some content fits or there
            // are no more floats present."
            // When text overflows next to a float and there's no word-break opportunity,
            // try moving below the float where the line box is wider.
            // Guard conditions:
            // 1. Float is actually narrowing the line (available width < full container width)
            // 2. The text would fit on a full-width line (rect->width <= full container width)
            //    This ensures we only shift when the float is causing the overflow, not when
            //    the text is inherently too wide for even the full container.
            // Note: Add 0.5px tolerance to account for sub-pixel float width rounding
            else if (lycon->line.has_float_intrusion &&
                     (lycon->line.effective_right - lycon->line.effective_left) <
                     (lycon->line.right - lycon->line.left) &&
                     rect->width <= (lycon->line.right - lycon->line.left) + 0.5f) {
                log_debug("text overflows next to float, shifting below float (eff_width=%.1f < full_width=%.1f)",
                          lycon->line.effective_right - lycon->line.effective_left,
                          lycon->line.right - lycon->line.left);
                // Undo the width we just added - we'll re-layout from LAYOUT_TEXT
                rect->width -= wd;
                // Reset str to start of current rect (we haven't output anything yet)
                str = text_start + rect->start_index;
                // Remove the rect we allocated (it will be re-created in LAYOUT_TEXT)
                // Find and unlink this rect from the chain
                if (text_view->rect == rect) {
                    text_view->rect = nullptr;
                } else {
                    TextRect* prev = text_view->rect;
                    while (prev && prev->next != rect) prev = prev->next;
                    if (prev) prev->next = nullptr;
                }
                line_break(lycon);
                goto LAYOUT_TEXT;
            }
            // else cannot break and no float intrusion, continue the flow in current line
        }
        if (is_space(*str)) {
            if (collapse_spaces) {
                // Collapse multiple spaces into one, respecting newline preservation
                do { str++; } while (is_space(*str) && (collapse_newlines || (*str != '\n' && *str != '\r')));
            } else {
                // Preserve spaces - just advance one character
                str++;
            }
            lycon->line.last_space = str - 1;  lycon->line.last_space_pos = rect->width;
            lycon->line.last_space_is_hyphen = false;  // this is a space, not a hyphen
            lycon->line.has_space = true;
        }
        else if (*str == '-') {
            // Hyphens are break opportunities (CSS allows breaking after hyphens)
            // Track this as a potential break point, but include the hyphen in the current line
            str = next_ch;
            lycon->line.last_space = str - 1;  // position of the hyphen
            lycon->line.last_space_pos = rect->width;  // width including the hyphen
            lycon->line.last_space_is_hyphen = true;  // mark this as a hyphen break
            lycon->line.is_line_start = false;
            lycon->line.has_space = false;
        }
        else if ((break_all || (is_cjk_character(codepoint) && !keep_all)) && wrap_lines) {
            // CJK or break-all: can break after this character
            // Note: Don't track as last_space since CJK breaks don't consume characters
            // Instead, allow breaking at current position when line fills
            str = next_ch;
            lycon->line.is_line_start = false;
            lycon->line.has_space = false;

            // Track position for potential break (but don't set last_space)
            // CJK breaks happen before the next character, not after a separator
        }
        else {
            str = next_ch;  lycon->line.is_line_start = false;  lycon->line.has_space = false;
        }
    } while (*str);
    // end of text
    if (wrap_lines && lycon->line.last_space) { // need to check if line will fill up (only when wrapping)
        float saved_advance_x = lycon->line.advance_x;  lycon->line.advance_x += rect->width;
        if (view_has_line_filled(lycon, text_view) == RDT_LINE_FILLED) {
            if (text_start <= lycon->line.last_space && lycon->line.last_space < str) {
                str = lycon->line.last_space + 1;
                // Restore advance_x before output_text (it will add the correct width)
                lycon->line.advance_x = saved_advance_x;
                output_text(lycon, text_view, rect, str - text_start - rect->start_index, lycon->line.last_space_pos);
                line_break(lycon);
                if (*str) goto LAYOUT_TEXT;
                else return;  // end of text
            }
            else { // last_space outside the text, break at start of text
                // Restore advance_x before line_break
                lycon->line.advance_x = saved_advance_x;
                line_break(lycon);
                rect->x = lycon->line.advance_x;  rect->y = lycon->block.advance_y;
                // output the entire text (advance_x is 0 after line_break reset)
            }
        }
        else {
            lycon->line.advance_x = saved_advance_x;
            // output the entire text
        }
    }
    // else output the entire text
    output_text(lycon, text_view, rect, str - text_start - rect->start_index, rect->width);

    auto t_end = high_resolution_clock::now();
    g_text_layout_time += duration<double, std::milli>(t_end - t_start).count();
    g_text_layout_count++;
}
