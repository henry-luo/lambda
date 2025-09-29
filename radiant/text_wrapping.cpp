#include "text_wrapping.h"
#include "font_face.h"
#include "../lib/log.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

// Text wrapping logging categories
log_category_t* wrap_log = NULL;
log_category_t* break_log = NULL;
log_category_t* justify_log = NULL;

// Initialize text wrapping logging
void init_text_wrapping_logging(void) {
    wrap_log = log_get_category("radiant.wrap");
    break_log = log_get_category("radiant.break");
    justify_log = log_get_category("radiant.justify");
    
    if (!wrap_log || !break_log || !justify_log) {
        log_warn("Failed to initialize text wrapping logging categories");
    } else {
        log_info("Text wrapping logging categories initialized");
    }
}

// === Text Wrap Configuration ===

TextWrapConfig* create_text_wrap_config(void) {
    TextWrapConfig* config = (TextWrapConfig*)calloc(1, sizeof(TextWrapConfig));
    if (!config) {
        log_error("Failed to allocate TextWrapConfig");
        return NULL;
    }
    
    // Set default values
    config->white_space = WHITESPACE_NORMAL;
    config->word_break = WORD_BREAK_NORMAL;
    config->overflow_wrap = OVERFLOW_WRAP_NORMAL;
    config->text_justify = TEXT_JUSTIFY_AUTO;
    config->max_width = 800;
    config->max_height = -1; // No height limit
    config->allow_overflow = false;
    config->hyphenation_enabled = false;
    config->hyphen_character = strdup("-");
    config->min_word_length = 5;
    config->break_cache_enabled = true;
    config->break_cache = NULL;
    
    return config;
}

void destroy_text_wrap_config(TextWrapConfig* config) {
    if (!config) return;
    
    if (config->hyphen_character) {
        free(config->hyphen_character);
    }
    if (config->break_cache) {
        hashmap_free(config->break_cache);
    }
    free(config);
}

void configure_white_space(TextWrapConfig* config, WhiteSpaceValue white_space) {
    if (!config) return;
    config->white_space = white_space;
    log_debug("Configured white-space: %d", white_space);
}

void configure_word_break(TextWrapConfig* config, WordBreakValue word_break) {
    if (!config) return;
    config->word_break = word_break;
    log_debug("Configured word-break: %d", word_break);
}

// === Text Wrap Context Management ===

TextWrapContext* create_text_wrap_context(const char* text, int text_length, TextWrapConfig* config) {
    if (!text || text_length <= 0 || !config) {
        log_error("Invalid parameters for create_text_wrap_context");
        return NULL;
    }
    
    TextWrapContext* ctx = (TextWrapContext*)calloc(1, sizeof(TextWrapContext));
    if (!ctx) {
        log_error("Failed to allocate TextWrapContext");
        return NULL;
    }
    
    // Copy configuration
    ctx->config = *config;
    ctx->text = text;
    ctx->text_length = text_length;
    
    // Convert UTF-8 to codepoints
    ctx->codepoint_count = utf8_to_codepoints(text, text_length, &ctx->codepoints);
    if (ctx->codepoint_count <= 0) {
        log_error("Failed to convert UTF-8 text to codepoints");
        free(ctx);
        return NULL;
    }
    ctx->owns_codepoints = true;
    
    // Initialize break opportunities array
    ctx->break_capacity = ctx->codepoint_count + 10;
    ctx->break_opportunities = (BreakInfo*)calloc(ctx->break_capacity, sizeof(BreakInfo));
    ctx->owns_break_opportunities = true;
    
    // Initialize lines array
    ctx->line_capacity = 10;
    ctx->lines = (WrappedTextLine*)calloc(ctx->line_capacity, sizeof(WrappedTextLine));
    ctx->owns_lines = true;
    
    log_debug("Created text wrap context: %d codepoints, %d chars", ctx->codepoint_count, text_length);
    return ctx;
}

void destroy_text_wrap_context(TextWrapContext* ctx) {
    if (!ctx) return;
    
    if (ctx->owns_codepoints && ctx->codepoints) {
        free(ctx->codepoints);
    }
    if (ctx->owns_break_opportunities && ctx->break_opportunities) {
        free(ctx->break_opportunities);
    }
    if (ctx->owns_lines && ctx->lines) {
        for (int i = 0; i < ctx->line_count; i++) {
            cleanup_wrapped_text_line(&ctx->lines[i]);
        }
        free(ctx->lines);
    }
    
    free(ctx);
}

// === Break Opportunity Detection ===

int find_break_opportunities(TextWrapContext* ctx) {
    if (!ctx || !ctx->codepoints) return 0;
    
    ctx->break_count = 0;
    
    for (int i = 0; i < ctx->codepoint_count; i++) {
        uint32_t codepoint = ctx->codepoints[i];
        
        if (is_break_opportunity(ctx, i, codepoint)) {
            if (ctx->break_count >= ctx->break_capacity) {
                // Expand array
                ctx->break_capacity *= 2;
                ctx->break_opportunities = (BreakInfo*)realloc(ctx->break_opportunities, 
                    ctx->break_capacity * sizeof(BreakInfo));
            }
            
            BreakInfo* break_info = &ctx->break_opportunities[ctx->break_count];
            break_info->position = i;
            break_info->type = BREAK_SOFT; // Determine actual type
            break_info->penalty = calculate_break_penalty(ctx, i, break_info->type);
            break_info->is_hyphen_break = false;
            
            ctx->break_count++;
        }
    }
    
    log_debug("Found %d break opportunities", ctx->break_count);
    return ctx->break_count;
}

bool is_break_opportunity(TextWrapContext* ctx, int position, uint32_t codepoint) {
    if (!ctx) return false;
    
    // Check for whitespace characters
    if (is_whitespace_codepoint(codepoint)) {
        return should_wrap_lines(ctx->config.white_space);
    }
    
    // Check for line break characters
    if (is_line_break_codepoint(codepoint)) {
        return true;
    }
    
    // Check for word boundaries based on word-break property
    if (ctx->config.word_break == WORD_BREAK_BREAK_ALL) {
        return true; // Can break anywhere
    }
    
    // Check for CJK characters
    if (is_cjk_character(codepoint)) {
        return true; // CJK characters can break between each other
    }
    
    return false;
}

int calculate_break_penalty(TextWrapContext* ctx, int position, BreakOpportunity type) {
    switch (type) {
        case BREAK_SOFT: return 0;      // Preferred break
        case BREAK_HARD: return -100;   // Required break
        case BREAK_FORCED: return 1000; // Avoid if possible
        case BREAK_HYPHEN: return 50;   // Moderate penalty
        default: return 100;
    }
}

// === Line Breaking ===

int wrap_text_lines(TextWrapContext* ctx, int max_width) {
    if (!ctx || max_width <= 0) return 0;
    
    find_break_opportunities(ctx);
    
    ctx->line_count = 0;
    int current_pos = 0;
    
    while (current_pos < ctx->codepoint_count) {
        LineBreakResult result = find_best_line_break(ctx, current_pos, max_width);
        
        if (ctx->line_count >= ctx->line_capacity) {
            ctx->line_capacity *= 2;
            ctx->lines = (WrappedTextLine*)realloc(ctx->lines, 
                ctx->line_capacity * sizeof(WrappedTextLine));
        }
        
        WrappedTextLine* line = &ctx->lines[ctx->line_count];
        memset(line, 0, sizeof(WrappedTextLine));
        
        line->start_position = current_pos;
        line->end_position = result.break_position;
        line->break_info = result;
        
        // Extract line text
        int line_byte_start = 0, line_byte_end = 0;
        // Convert codepoint positions to byte positions (simplified)
        for (int i = 0; i < current_pos && i < ctx->text_length; i++) {
            if (ctx->text[i] & 0x80) continue; // Skip UTF-8 continuation bytes
            line_byte_start++;
        }
        for (int i = 0; i < result.break_position && i < ctx->text_length; i++) {
            if (ctx->text[i] & 0x80) continue;
            line_byte_end++;
        }
        
        int line_length = line_byte_end - line_byte_start;
        if (line_length > 0) {
            line->text = (char*)malloc(line_length + 1);
            strncpy(line->text, ctx->text + line_byte_start, line_length);
            line->text[line_length] = '\0';
            line->text_length = line_length;
            line->owns_text = true;
        }
        
        ctx->line_count++;
        current_pos = result.break_position;
        
        if (current_pos >= ctx->codepoint_count) break;
    }
    
    log_debug("Wrapped text into %d lines", ctx->line_count);
    return ctx->line_count;
}

LineBreakResult find_best_line_break(TextWrapContext* ctx, int start_pos, int max_width) {
    LineBreakResult result = {0};
    result.break_position = start_pos + 1; // Minimum advance
    result.break_type = BREAK_FORCED;
    result.line_width = 0;
    
    // Find the best break opportunity within max_width
    for (int i = 0; i < ctx->break_count; i++) {
        BreakInfo* break_info = &ctx->break_opportunities[i];
        
        if (break_info->position <= start_pos) continue;
        
        int line_width = calculate_line_width(ctx, start_pos, break_info->position);
        
        if (line_width <= max_width) {
            result.break_position = break_info->position;
            result.break_type = break_info->type;
            result.line_width = line_width;
        } else {
            break; // Exceeded max width
        }
    }
    
    return result;
}

int calculate_line_width(TextWrapContext* ctx, int start_pos, int end_pos) {
    // Simplified width calculation - would use actual font metrics in real implementation
    return (end_pos - start_pos) * 8; // Assume 8 pixels per character
}

// === White-space Processing ===

bool should_preserve_spaces(WhiteSpaceValue white_space) {
    return white_space == WHITESPACE_PRE || 
           white_space == WHITESPACE_PRE_WRAP ||
           white_space == WHITESPACE_BREAK_SPACES;
}

bool should_preserve_newlines(WhiteSpaceValue white_space) {
    return white_space == WHITESPACE_PRE || 
           white_space == WHITESPACE_PRE_WRAP ||
           white_space == WHITESPACE_PRE_LINE;
}

bool should_wrap_lines(WhiteSpaceValue white_space) {
    return white_space == WHITESPACE_NORMAL ||
           white_space == WHITESPACE_PRE_WRAP ||
           white_space == WHITESPACE_PRE_LINE ||
           white_space == WHITESPACE_BREAK_SPACES;
}

// === Unicode Utility Functions ===

int utf8_to_codepoints(const char* utf8_text, int utf8_length, uint32_t** codepoints) {
    if (!utf8_text || utf8_length <= 0) return 0;
    
    // Allocate maximum possible codepoints (each byte could be a codepoint)
    uint32_t* cp_array = (uint32_t*)malloc(utf8_length * sizeof(uint32_t));
    if (!cp_array) return 0;
    
    int cp_count = 0;
    int i = 0;
    
    while (i < utf8_length) {
        uint32_t codepoint = 0;
        int bytes = 1;
        
        uint8_t byte = utf8_text[i];
        
        if (byte < 0x80) {
            // ASCII character
            codepoint = byte;
        } else if ((byte & 0xE0) == 0xC0) {
            // 2-byte sequence
            if (i + 1 < utf8_length) {
                codepoint = ((byte & 0x1F) << 6) | (utf8_text[i + 1] & 0x3F);
                bytes = 2;
            }
        } else if ((byte & 0xF0) == 0xE0) {
            // 3-byte sequence
            if (i + 2 < utf8_length) {
                codepoint = ((byte & 0x0F) << 12) | 
                           ((utf8_text[i + 1] & 0x3F) << 6) | 
                           (utf8_text[i + 2] & 0x3F);
                bytes = 3;
            }
        } else if ((byte & 0xF8) == 0xF0) {
            // 4-byte sequence
            if (i + 3 < utf8_length) {
                codepoint = ((byte & 0x07) << 18) | 
                           ((utf8_text[i + 1] & 0x3F) << 12) |
                           ((utf8_text[i + 2] & 0x3F) << 6) | 
                           (utf8_text[i + 3] & 0x3F);
                bytes = 4;
            }
        }
        
        cp_array[cp_count++] = codepoint;
        i += bytes;
    }
    
    // Resize array to actual size
    *codepoints = (uint32_t*)realloc(cp_array, cp_count * sizeof(uint32_t));
    return cp_count;
}

bool is_whitespace_codepoint(uint32_t codepoint) {
    return codepoint == 0x20 ||    // Space
           codepoint == 0x09 ||    // Tab
           codepoint == 0x0A ||    // Line feed
           codepoint == 0x0D ||    // Carriage return
           codepoint == 0xA0;      // Non-breaking space
}

bool is_line_break_codepoint(uint32_t codepoint) {
    return codepoint == 0x0A ||    // Line feed
           codepoint == 0x0D;      // Carriage return
}

bool is_cjk_character(uint32_t codepoint) {
    // Simplified CJK detection
    return (codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||  // CJK Unified Ideographs
           (codepoint >= 0x3400 && codepoint <= 0x4DBF) ||  // CJK Extension A
           (codepoint >= 0x3040 && codepoint <= 0x309F) ||  // Hiragana
           (codepoint >= 0x30A0 && codepoint <= 0x30FF);    // Katakana
}

// === Memory Management ===

void cleanup_wrapped_text_line(WrappedTextLine* line) {
    if (!line) return;
    
    if (line->owns_text && line->text) {
        free(line->text);
    }
    if (line->word_positions) {
        free(line->word_positions);
    }
    if (line->word_widths) {
        free(line->word_widths);
    }
    if (line->word_spacing) {
        free(line->word_spacing);
    }
    
    memset(line, 0, sizeof(WrappedTextLine));
}

// === Integration Functions ===

void wrap_text_in_layout_context(LayoutContext* lycon, DomNode* text_node, int max_width) {
    if (!lycon || !text_node || max_width <= 0) return;
    
    // Get text content
    const char* text = text_node->text_content;
    if (!text) return;
    
    int text_length = strlen(text);
    if (text_length == 0) return;
    
    // Create wrap configuration
    TextWrapConfig* config = create_text_wrap_config();
    config->max_width = max_width;
    
    // Apply CSS properties from DOM node
    apply_css_text_properties(config, text_node);
    
    // Create wrap context
    TextWrapContext* wrap_ctx = create_text_wrap_context(text, text_length, config);
    if (!wrap_ctx) {
        destroy_text_wrap_config(config);
        return;
    }
    
    // Perform text wrapping
    int line_count = wrap_text_lines(wrap_ctx, max_width);
    
    log_debug("Wrapped text into %d lines for layout context", line_count);
    
    // Update layout with wrapped text
    update_layout_with_wrapped_text(lycon, wrap_ctx);
    
    // Cleanup
    destroy_text_wrap_context(wrap_ctx);
    destroy_text_wrap_config(config);
}

void apply_css_text_properties(TextWrapConfig* config, DomNode* node) {
    if (!config || !node) return;
    
    // This would read CSS properties from the DOM node
    // For now, use defaults
    config->white_space = WHITESPACE_NORMAL;
    config->word_break = WORD_BREAK_NORMAL;
    config->overflow_wrap = OVERFLOW_WRAP_NORMAL;
    
    log_debug("Applied CSS text properties to wrap config");
}

void update_layout_with_wrapped_text(LayoutContext* lycon, TextWrapContext* wrap_ctx) {
    if (!lycon || !wrap_ctx) return;
    
    // Update layout context with wrapped line information
    // This would integrate with the existing layout system
    
    log_debug("Updated layout context with %d wrapped lines", wrap_ctx->line_count);
}

// === Debugging Functions ===

void debug_print_wrapped_lines(TextWrapContext* ctx) {
    if (!ctx) return;
    
    log_debug("=== Wrapped Text Lines ===");
    for (int i = 0; i < ctx->line_count; i++) {
        WrappedTextLine* line = &ctx->lines[i];
        log_debug("Line %d: '%s' (width: %d, break: %d)", 
                 i, line->text ? line->text : "", 
                 line->break_info.line_width, 
                 line->break_info.break_type);
    }
    log_debug("=== End Wrapped Lines ===");
}
