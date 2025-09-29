#include "font_face.h"
#include "view.hpp"

// Initialize text flow enhancements for a UiContext
void init_text_flow_enhancements(UiContext* uicon) {
    if (!uicon) {
        return;
    }
    
    // Initialize logging categories
    init_text_flow_logging();
    
    log_info(font_log ? font_log : log_get_category("default"), 
             "Text flow enhancements initialized for UiContext (pixel_ratio: %.2f)", 
             uicon->pixel_ratio);
    
    // Future: Initialize @font-face descriptor storage
    // uicon->font_faces = NULL;
    // uicon->font_face_count = 0;
    // uicon->font_face_capacity = 0;
}

// Enhanced font loading function that integrates with existing system
FT_Face load_font_enhanced(UiContext* uicon, const char* font_name, FontProp* fprop) {
    if (!uicon || !font_name || !fprop) {
        log_error(font_log ? font_log : log_get_category("default"), 
                 "Invalid parameters for load_font_enhanced");
        return NULL;
    }
    
    // For Phase 1, use existing font loading with enhanced logging
    log_debug(font_log ? font_log : log_get_category("default"), 
             "Enhanced font loading: %s (size: %d, weight: %d, style: %d)", 
             font_name, fprop->font_size, fprop->font_weight, fprop->font_style);
    
    FT_Face face = load_styled_font(uicon, font_name, fprop);
    
    if (face) {
        log_info(font_log ? font_log : log_get_category("default"), 
                "Enhanced font loaded successfully: %s", face->family_name);
    } else {
        log_error(font_log ? font_log : log_get_category("default"), 
                 "Enhanced font loading failed: %s", font_name);
    }
    
    return face;
}

// Enhanced glyph loading with structured logging
FT_GlyphSlot load_glyph_enhanced_logging(UiContext* uicon, FT_Face face, FontProp* font_style, uint32_t codepoint) {
    if (!uicon || !face || !font_style) {
        log_error(text_log ? text_log : log_get_category("default"), 
                 "Invalid parameters for load_glyph_enhanced_logging");
        return NULL;
    }
    
    log_debug(text_log ? text_log : log_get_category("default"), 
             "Loading glyph: U+%04X from font: %s", codepoint, face->family_name);
    
    FT_GlyphSlot slot = load_glyph(uicon, face, font_style, codepoint);
    
    if (slot) {
        log_debug(text_log ? text_log : log_get_category("default"), 
                 "Glyph loaded: U+%04X, advance: %ld, width: %d, height: %d", 
                 codepoint, slot->advance.x >> 6, slot->bitmap.width, slot->bitmap.rows);
    } else {
        log_warn(text_log ? text_log : log_get_category("default"), 
                "Failed to load glyph: U+%04X", codepoint);
    }
    
    return slot;
}

// Enhanced setup_font with pixel ratio support
void setup_font_with_pixel_ratio(UiContext* uicon, FontBox *fbox, const char* font_name, FontProp *fprop) {
    if (!uicon || !fbox || !font_name || !fprop) {
        log_error(font_log ? font_log : log_get_category("default"), 
                 "Invalid parameters for setup_font_with_pixel_ratio");
        return;
    }
    
    // Scale font size for high-DPI displays (preserve existing pixel_ratio handling)
    int scaled_font_size = fprop->font_size;
    if (uicon->pixel_ratio > 1.0f) {
        scaled_font_size = (int)(fprop->font_size * uicon->pixel_ratio);
        log_debug(font_log ? font_log : log_get_category("default"), 
                 "Scaling font size for pixel_ratio %.2f: %d -> %d", 
                 uicon->pixel_ratio, fprop->font_size, scaled_font_size);
    }
    
    // Create scaled font properties
    FontProp scaled_fprop = *fprop;
    scaled_fprop.font_size = scaled_font_size;
    
    // Use existing setup_font function
    setup_font(uicon, fbox, font_name, &scaled_fprop);
    
    // Store original font size and pixel ratio info
    fbox->current_font_size = fprop->font_size; // Original size
    
    log_info(font_log ? font_log : log_get_category("default"), 
            "Font setup with pixel ratio complete: %s (original: %d, scaled: %d, ratio: %.2f)", 
            font_name, fprop->font_size, scaled_font_size, uicon->pixel_ratio);
}
