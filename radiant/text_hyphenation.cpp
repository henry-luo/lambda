#include "text_wrapping.h"
#include "../lib/log.h"
#include <string.h>
#include <stdlib.h>

// Hyphenation context management
HyphenationContext* create_hyphenation_context(const char* language) {
    if (!language) {
        log_warn("No language specified for hyphenation context");
        return NULL;
    }
    
    HyphenationContext* ctx = (HyphenationContext*)calloc(1, sizeof(HyphenationContext));
    if (!ctx) {
        log_error("Failed to allocate HyphenationContext");
        return NULL;
    }
    
    ctx->language = strdup(language);
    ctx->enabled = true;
    ctx->min_word_length = 5;
    ctx->min_prefix_length = 2;
    ctx->min_suffix_length = 2;
    
    // Initialize dictionary hashmap
    ctx->dictionary = hashmap_new(sizeof(HyphenDictEntry), 1000, 0, 0, 
                                  NULL, NULL, NULL, NULL);
    
    if (!ctx->dictionary) {
        log_warn("Failed to create hyphenation dictionary");
        ctx->enabled = false;
    }
    
    log_debug("Created hyphenation context for language: %s", language);
    return ctx;
}

void destroy_hyphenation_context(HyphenationContext* ctx) {
    if (!ctx) return;
    
    if (ctx->language) {
        free(ctx->language);
    }
    if (ctx->dictionary) {
        hashmap_free(ctx->dictionary);
    }
    
    free(ctx);
}

// Simple hyphenation algorithm for English
int find_hyphenation_points(HyphenationContext* ctx, const char* word, int word_length, int* positions) {
    if (!ctx || !ctx->enabled || !word || word_length < ctx->min_word_length || !positions) {
        return 0;
    }
    
    int point_count = 0;
    
    // Simple vowel-based hyphenation for English
    const char* vowels = "aeiouAEIOU";
    
    for (int i = ctx->min_prefix_length; i < word_length - ctx->min_suffix_length; i++) {
        char c = word[i];
        
        // Check if current character is a vowel
        if (strchr(vowels, c)) {
            // Check if next character is a consonant
            if (i + 1 < word_length && !strchr(vowels, word[i + 1])) {
                positions[point_count++] = i + 1;
                
                // Limit number of hyphenation points
                if (point_count >= 5) break;
            }
        }
    }
    
    log_debug("Found %d hyphenation points for word: %s", point_count, word);
    return point_count;
}

bool can_hyphenate_at_position(HyphenationContext* ctx, const char* word, int position) {
    if (!ctx || !ctx->enabled || !word) return false;
    
    int word_length = strlen(word);
    
    // Check minimum lengths
    if (position < ctx->min_prefix_length || 
        position > word_length - ctx->min_suffix_length) {
        return false;
    }
    
    // Simple check: don't hyphenate between two vowels or two consonants
    const char* vowels = "aeiouAEIOU";
    bool prev_is_vowel = strchr(vowels, word[position - 1]) != NULL;
    bool curr_is_vowel = strchr(vowels, word[position]) != NULL;
    
    // Prefer consonant-vowel boundaries
    return !prev_is_vowel && curr_is_vowel;
}

void load_hyphenation_dictionary(HyphenationContext* ctx, const char* dict_path) {
    if (!ctx || !dict_path) return;
    
    // This would load a hyphenation dictionary from file
    // For now, just log that it would be loaded
    log_debug("Would load hyphenation dictionary from: %s", dict_path);
    
    // In a real implementation, this would:
    // 1. Open the dictionary file
    // 2. Parse hyphenation patterns
    // 3. Store them in the hashmap
    // 4. Enable more sophisticated hyphenation
}

// Bidirectional text support
BidiContext* create_bidi_context(TextDirection base_direction) {
    BidiContext* ctx = (BidiContext*)calloc(1, sizeof(BidiContext));
    if (!ctx) {
        log_error("Failed to allocate BidiContext");
        return NULL;
    }
    
    ctx->base_direction = base_direction;
    ctx->char_directions = NULL;
    ctx->reorder_map = NULL;
    ctx->has_rtl_content = false;
    ctx->needs_reordering = false;
    
    log_debug("Created bidirectional text context with base direction: %d", base_direction);
    return ctx;
}

void destroy_bidi_context(BidiContext* ctx) {
    if (!ctx) return;
    
    if (ctx->char_directions) {
        free(ctx->char_directions);
    }
    if (ctx->reorder_map) {
        free(ctx->reorder_map);
    }
    
    free(ctx);
}

TextDirection detect_text_direction(const uint32_t* codepoints, int count) {
    if (!codepoints || count <= 0) return TEXT_DIR_LTR;
    
    int ltr_count = 0;
    int rtl_count = 0;
    
    for (int i = 0; i < count; i++) {
        uint32_t cp = codepoints[i];
        
        // ASCII and Latin characters are LTR
        if ((cp >= 0x0041 && cp <= 0x005A) || // A-Z
            (cp >= 0x0061 && cp <= 0x007A) || // a-z
            (cp >= 0x0030 && cp <= 0x0039)) { // 0-9
            ltr_count++;
        }
        // Arabic characters are RTL
        else if (cp >= 0x0600 && cp <= 0x06FF) {
            rtl_count++;
        }
        // Hebrew characters are RTL
        else if (cp >= 0x0590 && cp <= 0x05FF) {
            rtl_count++;
        }
    }
    
    if (rtl_count > ltr_count) {
        log_debug("Detected RTL text direction (RTL: %d, LTR: %d)", rtl_count, ltr_count);
        return TEXT_DIR_RTL;
    } else if (ltr_count > 0) {
        log_debug("Detected LTR text direction (LTR: %d, RTL: %d)", ltr_count, rtl_count);
        return TEXT_DIR_LTR;
    }
    
    return TEXT_DIR_AUTO;
}

void analyze_bidi_text(BidiContext* ctx, const uint32_t* codepoints, int count) {
    if (!ctx || !codepoints || count <= 0) return;
    
    // Allocate direction arrays
    ctx->char_directions = (TextDirection*)malloc(count * sizeof(TextDirection));
    ctx->reorder_map = (int*)malloc(count * sizeof(int));
    
    if (!ctx->char_directions || !ctx->reorder_map) {
        log_error("Failed to allocate bidirectional analysis arrays");
        return;
    }
    
    // Analyze character directions
    for (int i = 0; i < count; i++) {
        uint32_t cp = codepoints[i];
        
        // Determine character direction
        if ((cp >= 0x0600 && cp <= 0x06FF) || // Arabic
            (cp >= 0x0590 && cp <= 0x05FF)) { // Hebrew
            ctx->char_directions[i] = TEXT_DIR_RTL;
            ctx->has_rtl_content = true;
        } else {
            ctx->char_directions[i] = TEXT_DIR_LTR;
        }
        
        // Initialize reorder map (identity mapping)
        ctx->reorder_map[i] = i;
    }
    
    // Determine if reordering is needed
    ctx->needs_reordering = ctx->has_rtl_content && (ctx->base_direction == TEXT_DIR_LTR);
    
    log_debug("Analyzed bidirectional text: %s RTL content, %s reordering", 
             ctx->has_rtl_content ? "has" : "no",
             ctx->needs_reordering ? "needs" : "no");
}

void reorder_bidi_text(BidiContext* ctx, char* text, int length) {
    if (!ctx || !text || length <= 0 || !ctx->needs_reordering) return;
    
    // This is a simplified bidirectional reordering
    // A full implementation would use the Unicode Bidirectional Algorithm
    
    log_debug("Would reorder bidirectional text (simplified implementation)");
    
    // In a real implementation, this would:
    // 1. Apply the Unicode Bidirectional Algorithm
    // 2. Reorder characters according to their directional properties
    // 3. Handle neutral characters appropriately
    // 4. Apply mirroring for symmetric characters
}

// Enhanced break opportunity detection with hyphenation
BreakInfo* find_next_break_opportunity_with_hyphenation(TextWrapContext* ctx, int start_position, HyphenationContext* hyphen_ctx) {
    if (!ctx || start_position >= ctx->codepoint_count) return NULL;
    
    // First, try to find regular break opportunities
    for (int i = 0; i < ctx->break_count; i++) {
        BreakInfo* break_info = &ctx->break_opportunities[i];
        if (break_info->position > start_position) {
            return break_info;
        }
    }
    
    // If no regular breaks found and hyphenation is enabled, try hyphenation
    if (hyphen_ctx && hyphen_ctx->enabled) {
        // This would implement hyphenation-based break detection
        log_debug("Would attempt hyphenation-based break detection");
    }
    
    return NULL;
}

// Integration with text wrapping
void enable_hyphenation_in_wrap_context(TextWrapContext* ctx, HyphenationContext* hyphen_ctx) {
    if (!ctx || !hyphen_ctx) return;
    
    ctx->config.hyphenation_enabled = true;
    
    // This would integrate hyphenation into the text wrapping process
    log_debug("Enabled hyphenation in text wrap context");
}

void enable_bidi_support_in_wrap_context(TextWrapContext* ctx, BidiContext* bidi_ctx) {
    if (!ctx || !bidi_ctx) return;
    
    // This would integrate bidirectional text support into wrapping
    log_debug("Enabled bidirectional text support in wrap context");
}

// Advanced justification with bidirectional support
void justify_bidi_text_line(WrappedTextLine* line, int target_width, 
                            TextJustifyValue justify_mode, 
                            BidiContext* bidi_ctx) {
    if (!line || !bidi_ctx) {
        // Fall back to regular justification
        return;
    }
    
    // This would implement bidirectional-aware text justification
    log_debug("Would apply bidirectional-aware text justification");
    
    // In a real implementation, this would:
    // 1. Consider text direction when distributing space
    // 2. Handle mixed LTR/RTL content appropriately
    // 3. Apply different justification rules for different scripts
}
