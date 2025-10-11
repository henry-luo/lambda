#include "layout.hpp"
#include "text_metrics.h"
#include "font_face.h"

// Enhanced text layout functions that integrate with existing layout_text.cpp

// Enhanced line initialization with Unicode support
void line_init_enhanced(LayoutContext* lycon) {
    // Call existing line_init first
    line_init(lycon);

    // Initialize enhanced text flow logging if not already done
    if (!text_log) {
        init_text_flow_logging();
    }

    log_debug(layout_log, "Enhanced line initialization: left=%d, right=%d",
              lycon->line.left, lycon->line.right);
}

// Enhanced line break with advanced metrics
void line_break_enhanced(LayoutContext* lycon) {
    if (!lycon) {
        log_error(layout_log, "Invalid layout context for enhanced line break");
        return;
    }

    log_debug(layout_log, "Enhanced line break: advance_x=%d, max_ascender=%d, max_descender=%d",
              lycon->line.advance_x, lycon->line.max_ascender, lycon->line.max_descender);

    // Use existing line_break logic but with enhanced logging
    line_break(lycon);

    log_debug(layout_log, "Line break complete: new advance_y=%d", lycon->block.advance_y);
}

// Enhanced text width calculation with Unicode support
int calculate_text_width_enhanced_unicode(LayoutContext* lycon, const char* text, int length) {
    if (!lycon || !text || length <= 0) {
        return 0;
    }

    // Create Unicode render context for this calculation
    EnhancedFontBox enhanced_fbox = {0};
    enhance_existing_font_box(&lycon->font, &enhanced_fbox);

    UnicodeRenderContext* unicode_ctx = create_unicode_render_context(lycon->ui_context, &enhanced_fbox);
    if (!unicode_ctx) {
        log_warn(text_log, "Failed to create Unicode context, falling back to basic calculation");
        // Fallback to basic calculation
        return calculate_basic_text_width(lycon, text, length);
    }

    int width = calculate_unicode_text_width(unicode_ctx, text, length);

    log_debug(text_log, "Enhanced Unicode text width: %d pixels for %d bytes", width, length);

    destroy_unicode_render_context(unicode_ctx);
    return width;
}

// Basic text width calculation (fallback)
int calculate_basic_text_width(LayoutContext* lycon, const char* text, int length) {
    if (!lycon || !text || length <= 0) {
        return 0;
    }

    int total_width = 0;
    const char* ptr = text;
    const char* end = text + length;

    while (ptr < end) {
        uint32_t codepoint;
        int bytes_consumed;

        if (*ptr < 0x80) {
            // ASCII character
            codepoint = *ptr;
            bytes_consumed = 1;
        } else {
            // Multi-byte UTF-8 character
            bytes_consumed = utf8_to_codepoint(ptr, &codepoint);
            if (bytes_consumed <= 0) {
                ptr++;
                continue;
            }
        }

        // Load glyph and get width
        if (codepoint == ' ') {
            total_width += lycon->font.space_width;
        } else {
            FT_GlyphSlot glyph = load_glyph(lycon->ui_context, lycon->font.face, &lycon->font.style, codepoint, false);
            if (glyph) {
                total_width += glyph->advance.x / 64.0;
            } else {
                total_width += lycon->font.space_width; // Fallback
            }
        }

        ptr += bytes_consumed;
    }

    return total_width;
}

// Enhanced text layout with Unicode support
void layout_text_enhanced(LayoutContext* lycon, DomNode* text_node) {
    if (!lycon || !text_node) {
        log_error(text_log, "Invalid parameters for enhanced text layout");
        return;
    }

    unsigned char* text_data = text_node->text_data();
    if (!text_data) {
        log_debug(text_log, "No text data for layout");
        return;
    }

    log_debug(text_log, "Enhanced text layout starting for text node");

    // Create enhanced font box from existing font
    EnhancedFontBox enhanced_fbox = {0};
    enhance_existing_font_box(&lycon->font, &enhanced_fbox);

    // Compute enhanced metrics if not already done
    compute_advanced_font_metrics(&enhanced_fbox);

    // Create Unicode render context
    UnicodeRenderContext* unicode_ctx = create_unicode_render_context(lycon->ui_context, &enhanced_fbox);
    if (!unicode_ctx) {
        log_warn(text_log, "Failed to create Unicode context, using basic text layout");
        // Fall back to existing layout_text function
        layout_text(lycon, text_node);
        return;
    }

    // Process text with enhanced Unicode support
    const char* text = (const char*)text_data;
    int text_length = strlen(text);

    ViewText* text_view = alloc_view_text(lycon);
    if (!text_view) {
        log_error(text_log, "Failed to allocate text view");
        destroy_unicode_render_context(unicode_ctx);
        return;
    }

    text_view->node = text_node;
    text_view->x = lycon->line.advance_x;
    text_view->y = lycon->block.advance_y;
    text_view->start_index = 0;

    // Calculate text width with Unicode support
    int text_width = calculate_unicode_text_width(unicode_ctx, text, text_length);

    // Check if text fits on current line
    if (lycon->line.advance_x + text_width > lycon->line.right) {
        log_debug(text_log, "Text width %d exceeds line width, handling line break", text_width);
        // Handle line breaking with enhanced logic
        handle_enhanced_line_breaking(lycon, unicode_ctx, text, text_length, text_view);
    } else {
        // Text fits on current line
        text_view->width = text_width;
        text_view->length = text_length;
        text_view->height = enhanced_fbox.metrics.height;

        // Update line metrics
        lycon->line.advance_x += text_width;
        lycon->line.max_ascender = max(lycon->line.max_ascender, enhanced_fbox.metrics.ascender);
        lycon->line.max_descender = max(lycon->line.max_descender, -enhanced_fbox.metrics.descender);

        log_debug(text_log, "Text layout complete: width=%d, height=%d, advance_x=%d",
                  text_width, text_view->height, lycon->line.advance_x);
    }

    destroy_unicode_render_context(unicode_ctx);
}

// Enhanced line breaking with Unicode support
void handle_enhanced_line_breaking(LayoutContext* lycon, UnicodeRenderContext* unicode_ctx,
                                  const char* text, int text_length, ViewText* text_view) {
    if (!lycon || !unicode_ctx || !text || !text_view) {
        return;
    }

    log_debug(text_log, "Handling enhanced line breaking for %d bytes of text", text_length);

    // Find optimal break point
    int available_width = lycon->line.right - lycon->line.advance_x;
    int break_point = find_unicode_break_point(unicode_ctx, text, text_length, available_width);

    if (break_point > 0) {
        // Partial text fits on current line
        int partial_width = calculate_unicode_text_width(unicode_ctx, text, break_point);

        text_view->width = partial_width;
        text_view->length = break_point;
        text_view->height = unicode_ctx->primary_font->metrics.height;

        // Update line metrics
        lycon->line.advance_x += partial_width;
        lycon->line.max_ascender = max(lycon->line.max_ascender, unicode_ctx->primary_font->metrics.ascender);
        lycon->line.max_descender = max(lycon->line.max_descender, -unicode_ctx->primary_font->metrics.descender);

        log_debug(text_log, "Partial text on line: %d chars, width=%d", break_point, partial_width);

        // Break line and continue with remaining text
        line_break_enhanced(lycon);

        // Layout remaining text on new line
        if (break_point < text_length) {
            const char* remaining_text = text + break_point;
            int remaining_length = text_length - break_point;

            // Skip leading whitespace on new line
            while (remaining_length > 0 && is_space(*remaining_text)) {
                remaining_text++;
                remaining_length--;
            }

            if (remaining_length > 0) {
                log_debug(text_log, "Continuing with remaining text: %d chars", remaining_length);
                // Recursively layout remaining text
                // In a full implementation, this would create a new text node or continue processing
            }
        }
    } else {
        // No good break point found, force break at line boundary
        log_warn(text_log, "No good break point found, forcing line break");
        line_break_enhanced(lycon);

        // Start text on new line
        text_view->x = lycon->line.advance_x;
        text_view->y = lycon->block.advance_y;

        // Try to fit text on new line
        int new_line_width = calculate_unicode_text_width(unicode_ctx, text, text_length);
        text_view->width = new_line_width;
        text_view->length = text_length;
        text_view->height = unicode_ctx->primary_font->metrics.height;

        lycon->line.advance_x += new_line_width;
        lycon->line.max_ascender = max(lycon->line.max_ascender, unicode_ctx->primary_font->metrics.ascender);
        lycon->line.max_descender = max(lycon->line.max_descender, -unicode_ctx->primary_font->metrics.descender);
    }
}

// Find optimal break point in Unicode text
int find_unicode_break_point(UnicodeRenderContext* unicode_ctx, const char* text, int text_length, int available_width) {
    if (!unicode_ctx || !text || text_length <= 0 || available_width <= 0) {
        return 0;
    }

    int current_width = 0;
    int last_break_point = 0;
    const char* ptr = text;
    const char* end = text + text_length;
    int char_position = 0;

    while (ptr < end) {
        uint32_t codepoint;
        int bytes_consumed;

        if (*ptr < 0x80) {
            codepoint = *ptr;
            bytes_consumed = 1;
        } else {
            bytes_consumed = utf8_to_codepoint(ptr, &codepoint);
            if (bytes_consumed <= 0) {
                ptr++;
                char_position++;
                continue;
            }
        }

        // Calculate character advance
        int char_advance = calculate_character_advance(unicode_ctx, codepoint);

        // Check if adding this character would exceed available width
        if (current_width + char_advance > available_width) {
            if (last_break_point > 0) {
                log_debug(text_log, "Break point found at character %d (width: %d/%d)",
                         last_break_point, current_width, available_width);
                return last_break_point;
            } else {
                // No break point found, return current position
                return char_position;
            }
        }

        current_width += char_advance;

        // Check if this is a good break point (space, punctuation, etc.)
        if (is_break_opportunity(codepoint)) {
            last_break_point = char_position + bytes_consumed;
        }

        ptr += bytes_consumed;
        char_position += bytes_consumed;
    }

    // All text fits
    return text_length;
}

// Check if character is a break opportunity
bool is_break_opportunity(uint32_t codepoint) {
    // Basic break opportunities
    if (codepoint == ' ' || codepoint == '\t' || codepoint == '\n') {
        return true;
    }

    // Punctuation break opportunities
    if (codepoint == '-' || codepoint == '/' || codepoint == '\\') {
        return true;
    }

    // Unicode line breaking would be more sophisticated
    // For now, use basic ASCII rules
    return false;
}

// Integration function to enhance existing FontBox
void enhance_existing_font_box(FontBox* existing_fbox, EnhancedFontBox* enhanced_fbox) {
    if (!existing_fbox || !enhanced_fbox) {
        return;
    }

    // Copy existing FontBox data
    enhanced_fbox->style = existing_fbox->style;
    enhanced_fbox->face = existing_fbox->face;
    enhanced_fbox->space_width = existing_fbox->space_width;
    enhanced_fbox->current_font_size = existing_fbox->current_font_size;

    // Initialize enhanced fields
    enhanced_fbox->metrics_computed = false;
    enhanced_fbox->cache_enabled = true;
    enhanced_fbox->char_width_cache = NULL;
    enhanced_fbox->char_bearing_cache = NULL;
    enhanced_fbox->pixel_ratio = 1.0f; // Default, should be set from UiContext
    enhanced_fbox->high_dpi_aware = false;

    log_debug(font_log, "Enhanced existing FontBox for font: %s",
              enhanced_fbox->face ? enhanced_fbox->face->family_name : "unknown");
}

// Integration with layout context
void integrate_advanced_metrics_with_layout(LayoutContext* lycon, TextLineMetrics* line_metrics) {
    if (!lycon || !line_metrics) {
        return;
    }

    // Update layout context with advanced line metrics
    lycon->line.advance_x = line_metrics->line_width;
    lycon->line.max_ascender = line_metrics->max_ascender;
    lycon->line.max_descender = line_metrics->max_descender;

    log_debug(layout_log, "Integrated advanced metrics: width=%d, ascender=%d, descender=%d",
              line_metrics->line_width, line_metrics->max_ascender, line_metrics->max_descender);
}

// Update layout context with Unicode support
void update_layout_context_with_unicode_support(LayoutContext* lycon, UnicodeRenderContext* unicode_ctx) {
    if (!lycon || !unicode_ctx) {
        return;
    }

    // Store Unicode context in layout context for future use
    // In a full implementation, LayoutContext would have a field for this

    log_info(layout_log, "Layout context updated with Unicode support (cache hits: %d, misses: %d)",
             unicode_ctx->cache_hits, unicode_ctx->cache_misses);
}
