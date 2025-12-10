#include "layout.hpp"
#include "layout_positioned.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"

#include "../lib/log.h"

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
    lycon->line.last_space = NULL;  lycon->line.last_space_pos = 0;
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
    // CRITICAL FIX: Smart line height selection for mixed font sizes vs uniform text
    // For mixed font sizes (spans), use max to accommodate larger fonts
    // For uniform text, prefer CSS line height for browser compatibility
    float font_line_height = lycon->line.max_ascender + lycon->line.max_descender;
    float css_line_height = lycon->block.line_height;

    // Check if we have mixed font sizes by comparing font height to CSS height
    bool has_mixed_fonts = (font_line_height > css_line_height + 2); // 2px tolerance
    float used_line_height;

    if (has_mixed_fonts) {
        // Mixed font sizes - use max to accommodate larger fonts
        used_line_height = max(css_line_height, font_line_height);
    } else {
        // Uniform font size - prefer CSS line height, but ensure minimum font height
        used_line_height = max(css_line_height, font_line_height - 1); // -1px adjustment for browser compatibility
    }

    // printf("DEBUG: Line advance - font: %d, css: %d, mixed: %s, used: %d\n",
    //        font_line_height, css_line_height, has_mixed_fonts ? "yes" : "no", used_line_height);
    lycon->block.advance_y += used_line_height;
    // reset the new line
    line_reset(lycon);
}

LineFillStatus text_has_line_filled(LayoutContext* lycon, DomNode* text_node) {
    // Get text data using helper function
    const char* text = (const char*)text_node->text_data();
    if (!text) return RDT_LINE_NOT_FILLED;  // null check

    unsigned char* str = (unsigned char*)text;
    float text_width = 0.0f;
    do {
        if (is_space(*str)) return RDT_LINE_NOT_FILLED;
        // Use sub-pixel rendering flags for better quality
        FT_Int32 load_flags = (FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);
        if (FT_Load_Char(lycon->font.ft_face, *str, load_flags)) {
            fprintf(stderr, "Could not load character '%c'\n", *str);
            return RDT_LINE_NOT_FILLED;
        }
        FT_GlyphSlot slot = lycon->font.ft_face->glyph;
        // Use precise float calculation for advance
        text_width += (float)(slot->advance.x) / 64.0f;
        // Use effective_right which accounts for float intrusions
        float line_right = lycon->line.has_float_intrusion ?
                           lycon->line.effective_right : lycon->line.right;
        if (lycon->line.advance_x + text_width > line_right) { // line filled up
            return RDT_LINE_FILLED;
        }
        str++;
    } while (*str);  // end of text
    lycon->line.advance_x += text_width;
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
    TypoMetrics typo = get_os2_typo_metrics(lycon->font.ft_face);
    if (typo.valid && typo.use_typo_metrics) {
        lycon->line.max_ascender = max(lycon->line.max_ascender, typo.ascender);
        lycon->line.max_descender = max(lycon->line.max_descender, typo.descender);
    } else {
        lycon->line.max_ascender = max(lycon->line.max_ascender, lycon->font.ft_face->size->metrics.ascender / 64.0);
        lycon->line.max_descender = max(lycon->line.max_descender, (-lycon->font.ft_face->size->metrics.descender) / 64.0);
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
    unsigned char* next_ch;  ViewText* text_view = null;  TextRect* prev_rect = NULL;
    unsigned char* text_start = text_node->text_data();
    if (!text_start) return;  // null check for text data
    unsigned char* str = text_start;
    // skip space at start of line
    if ((lycon->line.is_line_start || lycon->line.has_space) && is_space(*str)) {
        do { str++; } while (is_space(*str));
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
    TextRect* rect = (TextRect*)pool_calloc(lycon->doc->view_tree->pool, sizeof(TextRect));
    if (!text_view->rect) {
        text_view->rect = rect;
    } else {
        TextRect* last_rect = text_view->rect;;
        while (last_rect && last_rect->next) { last_rect = last_rect->next; }
        last_rect->next = rect;
    }
    rect->start_index = str - text_start;
    float font_height = lycon->font.ft_face->size->metrics.height / 64.0;
    rect->x = lycon->line.advance_x;
    rect->height = font_height;  // should text->height be lycon->block.line_height or font_height?

    // lead_y applies to baseline aligned text; not other vertical aligns
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
    else { // baseline
        rect->y = lycon->block.advance_y + lycon->block.lead_y;
    }
    log_debug("layout text: '%t', start_index %d, x: %f, y: %f, advance_y: %f, lead_y: %f, font_face: '%s', font_size: %f",
        str, rect->start_index, rect->x, rect->y, lycon->block.advance_y, lycon->block.lead_y, lycon->font.style->family, lycon->font.style->font_size);

    // layout the text glyphs
    do {
        float wd;
        uint32_t codepoint = *str;
        if (is_space(codepoint)) {
            wd = lycon->font.style->space_width;
        }
        else {
            if (codepoint >= 128) { // unicode char
                int bytes = utf8_to_codepoint(str, &codepoint);
                if (bytes <= 0) { // invalid utf8 char
                    next_ch = str + 1;  codepoint = 0;
                }
                else { next_ch = str + bytes; }
            }
            else { next_ch = str + 1; }
            FT_GlyphSlot glyph = load_glyph(lycon->ui_context, lycon->font.ft_face, lycon->font.style, codepoint, false);
            wd = glyph ? ((float)glyph->advance.x / 64.0) : lycon->font.style->space_width;
            // log_debug("char width: '%c', width %f", *str, wd);
        }
        // handle kerning
        if (lycon->font.style->has_kerning) {
            FT_UInt glyph_index = FT_Get_Char_Index(lycon->font.ft_face, codepoint);
            if (lycon->line.prev_glyph_index) {
                FT_Vector kerning;
                FT_Get_Kerning(lycon->font.ft_face, lycon->line.prev_glyph_index, glyph_index, FT_KERNING_DEFAULT, &kerning);
                if (kerning.x) {
                    if (str == text_start + rect->start_index) {
                        rect->x += ((float)kerning.x / 64.0);
                    }
                    else {
                        rect->width += ((float)kerning.x / 64.0);
                    }
                    log_debug("apply kerning: %f to char '%c'", (float)kerning.x / 64.0, *str);
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
        if (rect->x + rect->width > line_right) { // line filled up
            log_debug("line filled up");
            if (is_space(*str)) { // break at the current space
                log_debug("break on space");
                // skip all spaces
                do { str++; } while (is_space(*str));
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
            // else cannot break, continue the flow in current line
        }
        if (is_space(*str)) {
            do { str++; } while (is_space(*str));
            lycon->line.last_space = str - 1;  lycon->line.last_space_pos = rect->width;
            lycon->line.has_space = true;
        }
        else {
            str = next_ch;  lycon->line.is_line_start = false;  lycon->line.has_space = false;
        }
    } while (*str);
    // end of text
    if (lycon->line.last_space) { // need to check if line will fill up
        float advance_x = lycon->line.advance_x;  lycon->line.advance_x += rect->width;
        if (view_has_line_filled(lycon, text_view) == RDT_LINE_FILLED) {
            if (text_start <= lycon->line.last_space && lycon->line.last_space < str) {
                str = lycon->line.last_space + 1;
                output_text(lycon, text_view, rect, str - text_start - rect->start_index, lycon->line.last_space_pos);
                line_break(lycon);
                if (*str) goto LAYOUT_TEXT;
                else return;  // end of text
            }
            else { // last_space outside the text, break at start of text
                line_break(lycon);
                rect->x = lycon->line.advance_x;  rect->y = lycon->block.advance_y;
                // output the entire text
            }
        }
        else {
            lycon->line.advance_x = advance_x;
            // output the entire text
        }
    }
    // else output the entire text
    output_text(lycon, text_view, rect, str - text_start - rect->start_index, rect->width);
}
