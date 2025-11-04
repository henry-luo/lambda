#include "text_wrapping.h"
#include "text_metrics.h"
#include "layout.hpp"
#include "../lib/log.h"

// Enhanced text layout with wrapping support
void layout_text_with_wrapping(LayoutContext* lycon, DomNode* text_node) {
    if (!lycon || !text_node) {
        log_warn("Invalid parameters for layout_text_with_wrapping");
        return;
    }

    // Get text content
    const char* text = text_node->text_content;
    if (!text || strlen(text) == 0) {
        log_debug("No text content to layout");
        return;
    }

    // Determine container width for wrapping
    int container_width = lycon->width;
    if (text_node->parent && text_node->parent->computed_width > 0) {
        container_width = text_node->parent->computed_width;
    }

    log_debug("Layout text with wrapping: container_width=%d, text='%s'",
             container_width, text);

    // Create text wrap configuration
    TextWrapConfig* config = create_text_wrap_config();
    if (!config) {
        log_error("Failed to create text wrap config");
        return;
    }

    config->max_width = container_width;

    // Apply CSS text properties from DOM node
    apply_css_text_properties(config, text_node);

    // Create Unicode render context for font metrics
    EnhancedFontBox* enhanced_fbox = get_enhanced_font_box_for_node(lycon, text_node);
    UnicodeRenderContext* render_ctx = NULL;

    if (enhanced_fbox) {
        render_ctx = create_unicode_render_context(lycon->uicon, enhanced_fbox);
    }

    // Create text wrap context
    TextWrapContext* wrap_ctx = create_text_wrap_context(text, strlen(text), config);
    if (!wrap_ctx) {
        log_error("Failed to create text wrap context");
        destroy_text_wrap_config(config);
        if (render_ctx) destroy_unicode_render_context(render_ctx);
        return;
    }

    // Set render context for accurate width calculations
    wrap_ctx->render_ctx = render_ctx;

    // Perform text wrapping
    int line_count = wrap_text_lines(wrap_ctx, container_width);

    if (line_count > 0) {
        log_debug("Successfully wrapped text into %d lines", line_count);

        // Update DOM node with wrapped text information
        update_dom_node_with_wrapped_text(text_node, wrap_ctx);

        // Update layout context
        update_layout_with_wrapped_text(lycon, wrap_ctx);

        // Calculate total text height
        int total_height = calculate_total_text_height(wrap_ctx, render_ctx);

        // Update node dimensions
        text_node->computed_width = container_width;
        text_node->computed_height = total_height;

        log_debug("Updated text node dimensions: %dx%d",
                 text_node->computed_width, text_node->computed_height);
    } else {
        log_warn("Text wrapping produced no lines");
    }

    // Cleanup
    destroy_text_wrap_context(wrap_ctx);
    destroy_text_wrap_config(config);
    if (render_ctx) destroy_unicode_render_context(render_ctx);
}

// Enhanced line breaking with font metrics
LineBreakResult find_best_line_break_with_metrics(TextWrapContext* ctx, int start_pos, int max_width) {
    LineBreakResult result = {0};
    result.break_position = start_pos + 1;
    result.break_type = BREAK_FORCED;
    result.line_width = 0;

    if (!ctx || !ctx->render_ctx) {
        return find_best_line_break(ctx, start_pos, max_width);
    }

    int best_break_pos = start_pos;
    int best_width = 0;
    BreakOpportunity best_type = BREAK_FORCED;

    // Find break opportunities and calculate actual text widths
    for (int i = 0; i < ctx->break_count; i++) {
        BreakInfo* break_info = &ctx->break_opportunities[i];

        if (break_info->position <= start_pos) continue;

        // Calculate actual text width using font metrics
        int line_width = calculate_unicode_text_width_range(ctx->render_ctx,
                                                           ctx->codepoints,
                                                           start_pos,
                                                           break_info->position);

        if (line_width <= max_width) {
            best_break_pos = break_info->position;
            best_width = line_width;
            best_type = break_info->type;
        } else {
            break; // Exceeded max width
        }
    }

    result.break_position = best_break_pos;
    result.line_width = best_width;
    result.break_type = best_type;

    return result;
}

// Calculate text width for a range of codepoints
int calculate_unicode_text_width_range(UnicodeRenderContext* ctx,
                                      const uint32_t* codepoints,
                                      int start_pos,
                                      int end_pos) {
    if (!ctx || !codepoints || start_pos >= end_pos) return 0;

    int total_width = 0;

    for (int i = start_pos; i < end_pos; i++) {
        uint32_t codepoint = codepoints[i];

        // Get character advance from cache or calculate
        AdvancedCharacterMetrics* metrics = get_advanced_character_metrics_cached(
            ctx->primary_font, codepoint);

        if (metrics) {
            total_width += metrics->advance_x;
        } else {
            // Fallback to estimated width
            total_width += 8; // 8 pixels per character estimate
        }
    }

    return total_width;
}

// Update DOM node with wrapped text information
void update_dom_node_with_wrapped_text(DomNode* node, TextWrapContext* wrap_ctx) {
    if (!node || !wrap_ctx) return;

    // Store wrapped line information in DOM node for rendering
    // This would extend the DomNode structure to include wrapped text info

    log_debug("Updated DOM node with %d wrapped lines", wrap_ctx->line_count);
}

// Calculate total height of wrapped text
int calculate_total_text_height(TextWrapContext* wrap_ctx, UnicodeRenderContext* render_ctx) {
    if (!wrap_ctx || wrap_ctx->line_count == 0) return 0;

    int total_height = 0;
    int line_height = 16; // Default line height

    if (render_ctx && render_ctx->primary_font) {
        // Use actual font metrics for line height
        compute_advanced_font_metrics(render_ctx->primary_font);
        line_height = render_ctx->primary_font->metrics.height;
    }

    total_height = wrap_ctx->line_count * line_height;

    // Add line spacing if configured
    if (wrap_ctx->line_count > 1) {
        int line_spacing = line_height / 4; // 25% line spacing
        total_height += (wrap_ctx->line_count - 1) * line_spacing;
    }

    return total_height;
}

// Get enhanced font box for a DOM node
EnhancedFontBox* get_enhanced_font_box_for_node(LayoutContext* lycon, DomNode* node) {
    if (!lycon || !node) return NULL;

    // This would extract font properties from the DOM node's computed styles
    // and create or retrieve an appropriate EnhancedFontBox

    // For now, return a default enhanced font box
    static EnhancedFontBox default_fbox = {0};
    if (!default_fbox.face && lycon->uicon) {
        // Initialize default font
        FontProp fprop = {0};
        fprop.font_size = 16;
        fprop.font_style = LXB_CSS_VALUE_NORMAL;
        fprop.font_weight = LXB_CSS_VALUE_NORMAL;

        setup_font(lycon->uicon, (FontBox*)&default_fbox, &fprop);

        // Initialize enhanced features
        default_fbox.metrics_computed = false;
        default_fbox.char_width_cache = NULL;
        default_fbox.cache_enabled = true;
        default_fbox.pixel_ratio = lycon->uicon->pixel_ratio;
        default_fbox.high_dpi_aware = true;
    }

    return &default_fbox;
}

// CSS white-space property integration
void apply_white_space_property(TextWrapConfig* config, const char* white_space_value) {
    if (!config || !white_space_value) return;

    if (strcmp(white_space_value, "normal") == 0) {
        config->white_space = WHITESPACE_NORMAL;
    } else if (strcmp(white_space_value, "nowrap") == 0) {
        config->white_space = WHITESPACE_NOWRAP;
    } else if (strcmp(white_space_value, "pre") == 0) {
        config->white_space = WHITESPACE_PRE;
    } else if (strcmp(white_space_value, "pre-wrap") == 0) {
        config->white_space = WHITESPACE_PRE_WRAP;
    } else if (strcmp(white_space_value, "pre-line") == 0) {
        config->white_space = WHITESPACE_PRE_LINE;
    } else if (strcmp(white_space_value, "break-spaces") == 0) {
        config->white_space = WHITESPACE_BREAK_SPACES;
    }

    log_debug("Applied white-space property: %s -> %d", white_space_value, config->white_space);
}

// CSS word-break property integration
void apply_word_break_property(TextWrapConfig* config, const char* word_break_value) {
    if (!config || !word_break_value) return;

    if (strcmp(word_break_value, "normal") == 0) {
        config->word_break = WORD_BREAK_NORMAL;
    } else if (strcmp(word_break_value, "break-all") == 0) {
        config->word_break = WORD_BREAK_BREAK_ALL;
    } else if (strcmp(word_break_value, "keep-all") == 0) {
        config->word_break = WORD_BREAK_KEEP_ALL;
    } else if (strcmp(word_break_value, "break-word") == 0) {
        config->word_break = WORD_BREAK_BREAK_WORD;
    }

    log_debug("Applied word-break property: %s -> %d", word_break_value, config->word_break);
}

// CSS overflow-wrap property integration
void apply_overflow_wrap_property(TextWrapConfig* config, const char* overflow_wrap_value) {
    if (!config || !overflow_wrap_value) return;

    if (strcmp(overflow_wrap_value, "normal") == 0) {
        config->overflow_wrap = OVERFLOW_WRAP_NORMAL;
    } else if (strcmp(overflow_wrap_value, "anywhere") == 0) {
        config->overflow_wrap = OVERFLOW_WRAP_ANYWHERE;
    } else if (strcmp(overflow_wrap_value, "break-word") == 0) {
        config->overflow_wrap = OVERFLOW_WRAP_BREAK_WORD;
    }

    log_debug("Applied overflow-wrap property: %s -> %d", overflow_wrap_value, config->overflow_wrap);
}

// Enhanced text justification
void justify_wrapped_text_line(WrappedTextLine* line, int target_width,
                               TextJustifyValue justify_mode,
                               UnicodeRenderContext* render_ctx) {
    if (!line || !line->text || target_width <= 0) return;

    int current_width = line->break_info.line_width;
    if (current_width >= target_width) return; // Already fits or exceeds

    int extra_space = target_width - current_width;

    switch (justify_mode) {
        case TEXT_JUSTIFY_INTER_WORD:
            justify_by_word_spacing(line, extra_space, render_ctx);
            break;

        case TEXT_JUSTIFY_INTER_CHARACTER:
            justify_by_character_spacing(line, extra_space, render_ctx);
            break;

        case TEXT_JUSTIFY_DISTRIBUTE:
            justify_by_distribution(line, extra_space, render_ctx);
            break;

        default:
            // Auto justification - choose best method
            if (line->word_count > 1) {
                justify_by_word_spacing(line, extra_space, render_ctx);
            } else {
                justify_by_character_spacing(line, extra_space, render_ctx);
            }
            break;
    }

    line->is_justified = true;
    line->break_info.is_justified = true;

    log_debug("Justified text line: %d extra pixels distributed", extra_space);
}

// Justify by adjusting word spacing
void justify_by_word_spacing(WrappedTextLine* line, int extra_space, UnicodeRenderContext* render_ctx) {
    if (!line || line->word_count <= 1) return;

    int gaps = line->word_count - 1;
    if (gaps <= 0) return;

    int space_per_gap = extra_space / gaps;
    int remainder = extra_space % gaps;

    // Allocate word spacing array if not already allocated
    if (!line->word_spacing) {
        line->word_spacing = (float*)calloc(line->word_count, sizeof(float));
    }

    // Distribute extra space between words
    for (int i = 0; i < gaps; i++) {
        line->word_spacing[i] = space_per_gap;
        if (i < remainder) {
            line->word_spacing[i] += 1; // Distribute remainder
        }
    }

    line->break_info.word_spacing_adjustment = space_per_gap;
}

// Justify by adjusting character spacing
void justify_by_character_spacing(WrappedTextLine* line, int extra_space, UnicodeRenderContext* render_ctx) {
    if (!line || !line->text) return;

    int char_count = line->text_length;
    if (char_count <= 1) return;

    int space_per_char = extra_space / (char_count - 1);
    line->break_info.char_spacing_adjustment = space_per_char;

    log_debug("Character spacing adjustment: %d pixels per character", space_per_char);
}

// Justify by distributing space evenly
void justify_by_distribution(WrappedTextLine* line, int extra_space, UnicodeRenderContext* render_ctx) {
    // Combine word and character spacing for even distribution
    if (line->word_count > 1) {
        int word_space = extra_space / 2;
        int char_space = extra_space - word_space;

        justify_by_word_spacing(line, word_space, render_ctx);
        justify_by_character_spacing(line, char_space, render_ctx);
    } else {
        justify_by_character_spacing(line, extra_space, render_ctx);
    }
}
