#include "opentype_features.h"
#include "text_wrapping.h"
#include "layout.hpp"
#include "../lib/log.h"

// Enhanced text layout with OpenType features
void layout_text_with_opentype_features(LayoutContext* lycon, DomNode* text_node) {
    if (!lycon || !text_node) {
        log_warn("Invalid parameters for layout_text_with_opentype_features");
        return;
    }
    
    // Get text content
    const char* text = text_node->text_content;
    if (!text || strlen(text) == 0) {
        log_debug("No text content to layout with OpenType");
        return;
    }
    
    log_debug("Layout text with OpenType features: text='%s'", text);
    
    // Get enhanced font box for this node
    EnhancedFontBox* enhanced_fbox = get_enhanced_font_box_for_node(lycon, text_node);
    if (!enhanced_fbox || !enhanced_fbox->face) {
        log_warn("No enhanced font box available for OpenType processing");
        return;
    }
    
    // Analyze OpenType capabilities of the font
    OpenTypeFontInfo* ot_info = analyze_opentype_font(enhanced_fbox->face);
    if (!ot_info) {
        log_warn("Failed to analyze OpenType font capabilities");
        return;
    }
    
    // Create shaping context
    OpenTypeShapingContext* shaping_ctx = create_shaping_context(ot_info, enhanced_fbox);
    if (!shaping_ctx) {
        log_error("Failed to create OpenType shaping context");
        destroy_opentype_font_info(ot_info);
        return;
    }
    
    // Apply CSS font-feature-settings if available
    apply_css_font_features_from_node(shaping_ctx, text_node);
    
    // Convert text to codepoints
    uint32_t* codepoints = NULL;
    int codepoint_count = utf8_to_codepoints(text, strlen(text), &codepoints);
    
    if (codepoint_count > 0 && codepoints) {
        // Shape text with OpenType features
        int shaped_count = shape_text_with_opentype(shaping_ctx, codepoints, codepoint_count);
        
        if (shaped_count > 0) {
            log_debug("Successfully shaped text: %d codepoints -> %d glyphs", 
                     codepoint_count, shaped_count);
            
            // Calculate text dimensions with OpenType features
            int text_width = calculate_text_width_with_opentype(shaping_ctx, codepoints, codepoint_count);
            int text_height = enhanced_fbox->metrics.height;
            
            // Update DOM node dimensions
            text_node->computed_width = text_width;
            text_node->computed_height = text_height;
            
            // Store shaping results for rendering
            store_shaping_results_in_node(text_node, shaping_ctx);
            
            log_debug("Updated text node dimensions: %dx%d", text_width, text_height);
        } else {
            log_warn("OpenType text shaping produced no glyphs");
        }
        
        free(codepoints);
    } else {
        log_warn("Failed to convert text to codepoints for OpenType processing");
    }
    
    // Cleanup
    destroy_shaping_context(shaping_ctx);
    destroy_opentype_font_info(ot_info);
}

// Apply CSS font-feature-settings from DOM node
void apply_css_font_features_from_node(OpenTypeShapingContext* ctx, DomNode* node) {
    if (!ctx || !node) return;
    
    // This would read CSS font-feature-settings property from the DOM node
    // For now, enable common features based on text content
    
    // Enable ligatures for text that benefits from them
    if (text_benefits_from_ligatures(ctx->input_codepoints, ctx->input_count)) {
        enable_opentype_feature(ctx->font_info, OT_FEATURE_LIGA);
        ctx->enable_ligatures = true;
        log_debug("Enabled ligatures for text content");
    }
    
    // Enable kerning for text that benefits from it
    if (text_benefits_from_kerning(ctx->input_codepoints, ctx->input_count)) {
        enable_opentype_feature(ctx->font_info, OT_FEATURE_KERN);
        ctx->enable_kerning = true;
        log_debug("Enabled kerning for text content");
    }
    
    // Check for specific CSS properties (simplified)
    // In a real implementation, this would parse actual CSS values
    const char* font_variant = get_css_property(node, "font-variant");
    if (font_variant && strcmp(font_variant, "small-caps") == 0) {
        enable_opentype_feature(ctx->font_info, OT_FEATURE_SMCP);
        log_debug("Enabled small caps feature");
    }
    
    const char* font_variant_numeric = get_css_property(node, "font-variant-numeric");
    if (font_variant_numeric && strcmp(font_variant_numeric, "oldstyle-nums") == 0) {
        enable_opentype_feature(ctx->font_info, OT_FEATURE_ONUM);
        log_debug("Enabled oldstyle numerals feature");
    }
}

// Store shaping results in DOM node for rendering
void store_shaping_results_in_node(DomNode* node, OpenTypeShapingContext* ctx) {
    if (!node || !ctx) return;
    
    // This would extend the DomNode structure to store OpenType shaping results
    // For now, just log the results
    
    log_debug("Storing OpenType shaping results in DOM node:");
    log_debug("  - Total substitutions: %d", ctx->total_substitutions);
    log_debug("  - Total positioning adjustments: %d", ctx->total_positioning_adjustments);
    log_debug("  - Shaped glyph count: %d", ctx->shaped_count);
    
    // In a real implementation, this would:
    // 1. Store the shaped glyph array in the DOM node
    // 2. Store positioning information for rendering
    // 3. Store feature application results
    // 4. Cache results for performance
}

// Enhanced text width calculation with OpenType features
int calculate_enhanced_text_width(LayoutContext* lycon, DomNode* text_node, const char* text) {
    if (!lycon || !text_node || !text) return 0;
    
    // Get enhanced font box
    EnhancedFontBox* enhanced_fbox = get_enhanced_font_box_for_node(lycon, text_node);
    if (!enhanced_fbox) {
        // Fallback to basic calculation
        return strlen(text) * 8; // 8 pixels per character estimate
    }
    
    // Analyze OpenType capabilities
    OpenTypeFontInfo* ot_info = analyze_opentype_font(enhanced_fbox->face);
    if (!ot_info) {
        // Fallback to basic calculation
        return strlen(text) * enhanced_fbox->space_width;
    }
    
    // Create shaping context
    OpenTypeShapingContext* shaping_ctx = create_shaping_context(ot_info, enhanced_fbox);
    if (!shaping_ctx) {
        destroy_opentype_font_info(ot_info);
        return strlen(text) * enhanced_fbox->space_width;
    }
    
    // Convert text to codepoints and calculate width
    uint32_t* codepoints = NULL;
    int codepoint_count = utf8_to_codepoints(text, strlen(text), &codepoints);
    int width = 0;
    
    if (codepoint_count > 0 && codepoints) {
        width = calculate_text_width_with_opentype(shaping_ctx, codepoints, codepoint_count);
        free(codepoints);
    }
    
    // Cleanup
    destroy_shaping_context(shaping_ctx);
    destroy_opentype_font_info(ot_info);
    
    return width > 0 ? width : strlen(text) * 8; // Fallback if calculation failed
}

// Integration with text wrapping system
void wrap_text_with_opentype_features(LayoutContext* lycon, DomNode* text_node, int max_width) {
    if (!lycon || !text_node || max_width <= 0) return;
    
    const char* text = text_node->text_content;
    if (!text || strlen(text) == 0) return;
    
    log_debug("Wrapping text with OpenType features: max_width=%d", max_width);
    
    // Create text wrap configuration
    TextWrapConfig* config = create_text_wrap_config();
    config->max_width = max_width;
    
    // Apply CSS text properties
    apply_css_text_properties(config, text_node);
    
    // Create wrap context
    TextWrapContext* wrap_ctx = create_text_wrap_context(text, strlen(text), config);
    if (!wrap_ctx) {
        destroy_text_wrap_config(config);
        return;
    }
    
    // Enhanced line breaking with OpenType awareness
    int line_count = wrap_text_lines_with_opentype(wrap_ctx, lycon, text_node, max_width);
    
    if (line_count > 0) {
        log_debug("Wrapped text with OpenType features into %d lines", line_count);
        
        // Update layout with wrapped text
        update_layout_with_wrapped_text(lycon, wrap_ctx);
        
        // Calculate total height
        int total_height = calculate_total_text_height(wrap_ctx, NULL);
        text_node->computed_width = max_width;
        text_node->computed_height = total_height;
    }
    
    // Cleanup
    destroy_text_wrap_context(wrap_ctx);
    destroy_text_wrap_config(config);
}

// Enhanced line breaking with OpenType features
int wrap_text_lines_with_opentype(TextWrapContext* wrap_ctx, LayoutContext* lycon, DomNode* text_node, int max_width) {
    if (!wrap_ctx || !lycon || !text_node) return 0;
    
    // Get enhanced font box for accurate measurements
    EnhancedFontBox* enhanced_fbox = get_enhanced_font_box_for_node(lycon, text_node);
    if (!enhanced_fbox) {
        // Fallback to regular text wrapping
        return wrap_text_lines(wrap_ctx, max_width);
    }
    
    // Analyze OpenType capabilities
    OpenTypeFontInfo* ot_info = analyze_opentype_font(enhanced_fbox->face);
    if (!ot_info) {
        return wrap_text_lines(wrap_ctx, max_width);
    }
    
    // Create shaping context for accurate width calculations
    OpenTypeShapingContext* shaping_ctx = create_shaping_context(ot_info, enhanced_fbox);
    if (!shaping_ctx) {
        destroy_opentype_font_info(ot_info);
        return wrap_text_lines(wrap_ctx, max_width);
    }
    
    // Find break opportunities
    find_break_opportunities(wrap_ctx);
    
    wrap_ctx->line_count = 0;
    int current_pos = 0;
    
    while (current_pos < wrap_ctx->codepoint_count) {
        LineBreakResult result = find_best_line_break_with_opentype(wrap_ctx, shaping_ctx, current_pos, max_width);
        
        if (wrap_ctx->line_count >= wrap_ctx->line_capacity) {
            wrap_ctx->line_capacity *= 2;
            wrap_ctx->lines = (WrappedTextLine*)realloc(wrap_ctx->lines, 
                wrap_ctx->line_capacity * sizeof(WrappedTextLine));
        }
        
        WrappedTextLine* line = &wrap_ctx->lines[wrap_ctx->line_count];
        memset(line, 0, sizeof(WrappedTextLine));
        
        line->start_position = current_pos;
        line->end_position = result.break_position;
        line->break_info = result;
        
        // Extract line text and apply OpenType shaping
        extract_and_shape_line_text(line, wrap_ctx, shaping_ctx);
        
        wrap_ctx->line_count++;
        current_pos = result.break_position;
        
        if (current_pos >= wrap_ctx->codepoint_count) break;
    }
    
    // Cleanup
    destroy_shaping_context(shaping_ctx);
    destroy_opentype_font_info(ot_info);
    
    return wrap_ctx->line_count;
}

// Find best line break with OpenType-aware width calculation
LineBreakResult find_best_line_break_with_opentype(TextWrapContext* wrap_ctx, 
                                                   OpenTypeShapingContext* shaping_ctx, 
                                                   int start_pos, int max_width) {
    LineBreakResult result = {0};
    result.break_position = start_pos + 1;
    result.break_type = BREAK_FORCED;
    result.line_width = 0;
    
    int best_break_pos = start_pos;
    int best_width = 0;
    BreakOpportunity best_type = BREAK_FORCED;
    
    // Find break opportunities and calculate actual text widths with OpenType
    for (int i = 0; i < wrap_ctx->break_count; i++) {
        BreakInfo* break_info = &wrap_ctx->break_opportunities[i];
        
        if (break_info->position <= start_pos) continue;
        
        // Calculate text width with OpenType features
        int line_width = calculate_line_width_with_opentype(wrap_ctx, shaping_ctx, start_pos, break_info->position);
        
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

// Calculate line width with OpenType features
int calculate_line_width_with_opentype(TextWrapContext* wrap_ctx, 
                                       OpenTypeShapingContext* shaping_ctx, 
                                       int start_pos, int end_pos) {
    if (!wrap_ctx || !shaping_ctx || start_pos >= end_pos) return 0;
    
    // Extract codepoints for this line
    int line_length = end_pos - start_pos;
    uint32_t* line_codepoints = &wrap_ctx->codepoints[start_pos];
    
    // Calculate width with OpenType features
    return calculate_text_width_with_opentype(shaping_ctx, line_codepoints, line_length);
}

// Extract and shape line text
void extract_and_shape_line_text(WrappedTextLine* line, TextWrapContext* wrap_ctx, OpenTypeShapingContext* shaping_ctx) {
    if (!line || !wrap_ctx || !shaping_ctx) return;
    
    int line_length = line->end_position - line->start_position;
    if (line_length <= 0) return;
    
    // Extract codepoints for this line
    uint32_t* line_codepoints = &wrap_ctx->codepoints[line->start_position];
    
    // Shape the line text
    int shaped_count = shape_text_with_opentype(shaping_ctx, line_codepoints, line_length);
    
    // Convert back to UTF-8 for storage (simplified)
    int utf8_length = line_length * 4; // Maximum UTF-8 bytes
    line->text = (char*)malloc(utf8_length + 1);
    line->text_length = codepoints_to_utf8(line_codepoints, line_length, &line->text);
    line->owns_text = true;
    
    log_debug("Extracted and shaped line text: %d codepoints -> %d glyphs", line_length, shaped_count);
}

// Simplified CSS property getter (would be implemented properly in real system)
const char* get_css_property(DomNode* node, const char* property) {
    // This would access the actual CSS computed styles
    // For now, return NULL (no property found)
    return NULL;
}

// Convert codepoints back to UTF-8 (simplified implementation)
int codepoints_to_utf8(const uint32_t* codepoints, int count, char** utf8_text) {
    if (!codepoints || count <= 0 || !utf8_text) return 0;
    
    // Simplified conversion - just convert ASCII codepoints
    char* text = (char*)malloc(count + 1);
    int length = 0;
    
    for (int i = 0; i < count; i++) {
        if (codepoints[i] < 128) {
            text[length++] = (char)codepoints[i];
        }
    }
    
    text[length] = '\0';
    *utf8_text = text;
    return length;
}
