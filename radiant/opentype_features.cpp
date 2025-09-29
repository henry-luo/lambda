#include "opentype_features.h"
#include "font_face.h"
#include "../lib/log.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

// OpenType logging categories
log_category_t* opentype_log = NULL;
log_category_t* ligature_log = NULL;
log_category_t* kerning_log = NULL;

// Initialize OpenType logging
void init_opentype_logging(void) {
    opentype_log = log_get_category("radiant.opentype");
    ligature_log = log_get_category("radiant.ligature");
    kerning_log = log_get_category("radiant.kerning");
    
    if (!opentype_log || !ligature_log || !kerning_log) {
        log_warn("Failed to initialize OpenType logging categories");
    } else {
        log_info("OpenType logging categories initialized");
    }
}

// === OpenType Font Analysis ===

OpenTypeFontInfo* analyze_opentype_font(FT_Face face) {
    if (!face) {
        log_error("Invalid FT_Face for OpenType analysis");
        return NULL;
    }
    
    OpenTypeFontInfo* info = (OpenTypeFontInfo*)calloc(1, sizeof(OpenTypeFontInfo));
    if (!info) {
        log_error("Failed to allocate OpenTypeFontInfo");
        return NULL;
    }
    
    info->face = face;
    
    // Check for OpenType tables
    info->has_gpos_table = FT_HAS_GLYPH_NAMES(face);
    info->has_gsub_table = FT_HAS_GLYPH_NAMES(face);
    info->has_kern_table = FT_HAS_KERNING(face);
    
    // Initialize feature arrays
    info->feature_capacity = 20;
    info->features = (OpenTypeFeature*)calloc(info->feature_capacity, sizeof(OpenTypeFeature));
    
    // Initialize ligature arrays
    info->ligature_capacity = 50;
    info->ligatures = (LigatureInfo*)calloc(info->ligature_capacity, sizeof(LigatureInfo));
    
    // Initialize kerning cache
    info->kerning_cache = hashmap_new(sizeof(KerningPair), 1000, 0, 0, NULL, NULL, NULL, NULL);
    info->kerning_enabled = info->has_kern_table;
    info->kerning_scale_factor = 64; // Default FreeType scaling
    
    // Scan for supported features
    scan_font_features(info);
    
    log_debug("Analyzed OpenType font: %s (GPOS: %s, GSUB: %s, KERN: %s)", 
             face->family_name,
             info->has_gpos_table ? "yes" : "no",
             info->has_gsub_table ? "yes" : "no", 
             info->has_kern_table ? "yes" : "no");
    
    return info;
}

void destroy_opentype_font_info(OpenTypeFontInfo* info) {
    if (!info) return;
    
    if (info->features) {
        free(info->features);
    }
    if (info->ligatures) {
        for (int i = 0; i < info->ligature_count; i++) {
            cleanup_ligature_info(&info->ligatures[i]);
        }
        free(info->ligatures);
    }
    if (info->kerning_cache) {
        hashmap_free(info->kerning_cache);
    }
    
    free(info);
}

void scan_font_features(OpenTypeFontInfo* info) {
    if (!info) return;
    
    // Add common features that most fonts support
    OpenTypeFeatureTag common_features[] = {
        OT_FEATURE_KERN, OT_FEATURE_LIGA, OT_FEATURE_CLIG,
        OT_FEATURE_CALT, OT_FEATURE_SMCP, OT_FEATURE_ONUM
    };
    
    for (int i = 0; i < 6; i++) {
        if (info->feature_count < info->feature_capacity) {
            OpenTypeFeature* feature = &info->features[info->feature_count];
            feature->tag = common_features[i];
            feature->state = OT_FEATURE_AUTO;
            feature->parameter = 0;
            feature->is_supported = true; // Assume supported for now
            
            // Set feature names
            feature_tag_to_string(feature->tag, feature->name);
            strcpy(feature->description, get_feature_description(feature->tag));
            
            info->feature_count++;
        }
    }
    
    log_debug("Scanned %d OpenType features", info->feature_count);
}

// === Feature Management ===

bool font_supports_feature(OpenTypeFontInfo* info, OpenTypeFeatureTag feature) {
    if (!info) return false;
    
    for (int i = 0; i < info->feature_count; i++) {
        if (info->features[i].tag == feature) {
            return info->features[i].is_supported;
        }
    }
    return false;
}

void enable_opentype_feature(OpenTypeFontInfo* info, OpenTypeFeatureTag feature) {
    if (!info) return;
    
    for (int i = 0; i < info->feature_count; i++) {
        if (info->features[i].tag == feature) {
            info->features[i].state = OT_FEATURE_ON;
            log_debug("Enabled OpenType feature: %s", info->features[i].name);
            return;
        }
    }
    
    log_warn("Feature not found: 0x%08X", feature);
}

bool is_feature_enabled(OpenTypeFontInfo* info, OpenTypeFeatureTag feature) {
    if (!info) return false;
    
    for (int i = 0; i < info->feature_count; i++) {
        if (info->features[i].tag == feature) {
            return info->features[i].state == OT_FEATURE_ON ||
                   (info->features[i].state == OT_FEATURE_AUTO && info->features[i].is_supported);
        }
    }
    return false;
}

// === Ligature Processing ===

int find_ligatures_in_text(OpenTypeFontInfo* info, const uint32_t* codepoints, int count, LigatureInfo** ligatures) {
    if (!info || !codepoints || count <= 0) return 0;
    
    int ligature_count = 0;
    
    // Common ligatures to check for
    struct {
        uint32_t input[3];
        int input_count;
        const char* name;
    } common_ligatures[] = {
        {{0x66, 0x69, 0}, 2, "fi"},      // fi
        {{0x66, 0x6C, 0}, 2, "fl"},      // fl
        {{0x66, 0x66, 0}, 2, "ff"},      // ff
        {{0x66, 0x66, 0x69}, 3, "ffi"},  // ffi
        {{0x66, 0x66, 0x6C}, 3, "ffl"},  // ffl
    };
    
    for (int i = 0; i < count - 1; i++) {
        for (int lig = 0; lig < 5; lig++) {
            if (i + common_ligatures[lig].input_count <= count) {
                bool matches = true;
                for (int j = 0; j < common_ligatures[lig].input_count; j++) {
                    if (codepoints[i + j] != common_ligatures[lig].input[j]) {
                        matches = false;
                        break;
                    }
                }
                
                if (matches) {
                    // Found a potential ligature
                    if (info->ligature_count < info->ligature_capacity) {
                        LigatureInfo* lig_info = &info->ligatures[info->ligature_count];
                        lig_info->input_count = common_ligatures[lig].input_count;
                        lig_info->input_codepoints = (uint32_t*)malloc(lig_info->input_count * sizeof(uint32_t));
                        memcpy(lig_info->input_codepoints, common_ligatures[lig].input, 
                               lig_info->input_count * sizeof(uint32_t));
                        lig_info->is_standard = true;
                        strcpy(lig_info->ligature_name, common_ligatures[lig].name);
                        
                        info->ligature_count++;
                        ligature_count++;
                    }
                }
            }
        }
    }
    
    if (ligatures) {
        *ligatures = info->ligatures;
    }
    
    return ligature_count;
}

bool can_form_ligature(OpenTypeFontInfo* info, const uint32_t* codepoints, int count) {
    if (!info || !codepoints || count < 2) return false;
    
    // Check for common ligature patterns
    if (count == 2) {
        if (codepoints[0] == 'f' && (codepoints[1] == 'i' || codepoints[1] == 'l')) {
            return true;
        }
        if (codepoints[0] == 'f' && codepoints[1] == 'f') {
            return true;
        }
    }
    
    return false;
}

// === Kerning Processing ===

int get_kerning_adjustment(OpenTypeFontInfo* info, uint32_t left_char, uint32_t right_char) {
    if (!info || !info->kerning_enabled) return 0;
    
    // Check cache first
    KerningPair search_key = {left_char, right_char, 0, 0, 0, 0, true, false};
    KerningPair* cached = (KerningPair*)hashmap_get(info->kerning_cache, &search_key);
    if (cached) {
        return cached->scaled_kerning;
    }
    
    // Get glyph indices
    FT_UInt left_glyph = FT_Get_Char_Index(info->face, left_char);
    FT_UInt right_glyph = FT_Get_Char_Index(info->face, right_char);
    
    if (left_glyph == 0 || right_glyph == 0) return 0;
    
    // Get kerning from FreeType
    FT_Vector kerning;
    FT_Error error = FT_Get_Kerning(info->face, left_glyph, right_glyph, FT_KERNING_DEFAULT, &kerning);
    
    if (error) return 0;
    
    int kerning_value = kerning.x >> 6; // Convert from 26.6 fixed point
    
    // Cache the result
    KerningPair pair = {
        left_char, right_char, left_glyph, right_glyph,
        kerning.x, kerning_value, true, false
    };
    hashmap_set(info->kerning_cache, &pair);
    
    info->kerning_adjustments++;
    
    if (kerning_value != 0) {
        log_debug("Kerning adjustment: '%c%c' = %d pixels", 
                 (char)left_char, (char)right_char, kerning_value);
    }
    
    return kerning_value;
}

void apply_kerning_to_glyphs(AdvancedGlyphInfo* glyphs, int glyph_count, OpenTypeFontInfo* info) {
    if (!glyphs || glyph_count < 2 || !info) return;
    
    for (int i = 0; i < glyph_count - 1; i++) {
        uint32_t left_char = glyphs[i].original_codepoint;
        uint32_t right_char = glyphs[i + 1].original_codepoint;
        
        int kerning = get_kerning_adjustment(info, left_char, right_char);
        if (kerning != 0) {
            glyphs[i + 1].offset_x += kerning;
            glyphs[i + 1].has_kerning = true;
        }
    }
}

// === Text Shaping ===

OpenTypeShapingContext* create_shaping_context(OpenTypeFontInfo* font_info, EnhancedFontBox* font_box) {
    if (!font_info || !font_box) {
        log_error("Invalid parameters for create_shaping_context");
        return NULL;
    }
    
    OpenTypeShapingContext* ctx = (OpenTypeShapingContext*)calloc(1, sizeof(OpenTypeShapingContext));
    if (!ctx) {
        log_error("Failed to allocate OpenTypeShapingContext");
        return NULL;
    }
    
    ctx->font_info = font_info;
    ctx->font_box = font_box;
    ctx->shaped_capacity = 100;
    ctx->shaped_glyphs = (AdvancedGlyphInfo*)calloc(ctx->shaped_capacity, sizeof(AdvancedGlyphInfo));
    
    // Default feature settings
    ctx->enable_ligatures = true;
    ctx->enable_kerning = true;
    ctx->enable_contextual_alternates = false;
    ctx->font_size = font_box->current_font_size;
    ctx->pixel_ratio = font_box->pixel_ratio;
    
    log_debug("Created OpenType shaping context");
    return ctx;
}

void destroy_shaping_context(OpenTypeShapingContext* ctx) {
    if (!ctx) return;
    
    if (ctx->shaped_glyphs) {
        for (int i = 0; i < ctx->shaped_count; i++) {
            cleanup_advanced_glyph_info(&ctx->shaped_glyphs[i]);
        }
        free(ctx->shaped_glyphs);
    }
    if (ctx->enabled_features) {
        free(ctx->enabled_features);
    }
    
    free(ctx);
}

int shape_text_with_opentype(OpenTypeShapingContext* ctx, const uint32_t* codepoints, int count) {
    if (!ctx || !codepoints || count <= 0) return 0;
    
    ctx->input_codepoints = codepoints;
    ctx->input_count = count;
    ctx->shaped_count = 0;
    
    // Ensure capacity
    if (count > ctx->shaped_capacity) {
        ctx->shaped_capacity = count * 2;
        ctx->shaped_glyphs = (AdvancedGlyphInfo*)realloc(ctx->shaped_glyphs, 
            ctx->shaped_capacity * sizeof(AdvancedGlyphInfo));
    }
    
    // Initialize shaped glyphs with input
    for (int i = 0; i < count; i++) {
        AdvancedGlyphInfo* glyph = &ctx->shaped_glyphs[i];
        memset(glyph, 0, sizeof(AdvancedGlyphInfo));
        
        glyph->original_codepoint = codepoints[i];
        glyph->rendered_codepoint = codepoints[i];
        glyph->glyph_index = FT_Get_Char_Index(ctx->font_info->face, codepoints[i]);
        glyph->pixel_ratio = ctx->pixel_ratio;
        
        // Load glyph metrics
        if (FT_Load_Glyph(ctx->font_info->face, glyph->glyph_index, FT_LOAD_DEFAULT) == 0) {
            FT_GlyphSlot slot = ctx->font_info->face->glyph;
            glyph->advance_x = slot->advance.x >> 6;
            glyph->advance_y = slot->advance.y >> 6;
            glyph->bearing_x = slot->bitmap_left;
            glyph->bearing_y = slot->bitmap_top;
        }
        
        ctx->shaped_count++;
    }
    
    // Apply OpenType features
    apply_opentype_features(ctx);
    
    log_debug("Shaped %d codepoints into %d glyphs", count, ctx->shaped_count);
    return ctx->shaped_count;
}

void apply_opentype_features(OpenTypeShapingContext* ctx) {
    if (!ctx) return;
    
    // Apply ligature substitutions
    if (ctx->enable_ligatures && is_feature_enabled(ctx->font_info, OT_FEATURE_LIGA)) {
        apply_ligature_substitutions(ctx);
    }
    
    // Apply kerning positioning
    if (ctx->enable_kerning && is_feature_enabled(ctx->font_info, OT_FEATURE_KERN)) {
        apply_kerning_positioning(ctx);
    }
    
    // Calculate final positions
    calculate_final_positions(ctx);
}

void apply_ligature_substitutions(OpenTypeShapingContext* ctx) {
    if (!ctx) return;
    
    // Simple ligature detection and substitution
    for (int i = 0; i < ctx->shaped_count - 1; i++) {
        uint32_t left = ctx->shaped_glyphs[i].original_codepoint;
        uint32_t right = ctx->shaped_glyphs[i + 1].original_codepoint;
        
        // Check for fi ligature
        if (left == 'f' && right == 'i') {
            // Mark as ligature (simplified)
            ctx->shaped_glyphs[i].is_ligature = true;
            ctx->shaped_glyphs[i].rendered_codepoint = 0xFB01; // fi ligature
            
            // Remove the second character
            for (int j = i + 1; j < ctx->shaped_count - 1; j++) {
                ctx->shaped_glyphs[j] = ctx->shaped_glyphs[j + 1];
            }
            ctx->shaped_count--;
            ctx->total_substitutions++;
            
            log_debug("Applied fi ligature substitution");
        }
        // Check for fl ligature
        else if (left == 'f' && right == 'l') {
            ctx->shaped_glyphs[i].is_ligature = true;
            ctx->shaped_glyphs[i].rendered_codepoint = 0xFB02; // fl ligature
            
            // Remove the second character
            for (int j = i + 1; j < ctx->shaped_count - 1; j++) {
                ctx->shaped_glyphs[j] = ctx->shaped_glyphs[j + 1];
            }
            ctx->shaped_count--;
            ctx->total_substitutions++;
            
            log_debug("Applied fl ligature substitution");
        }
    }
}

void apply_kerning_positioning(OpenTypeShapingContext* ctx) {
    if (!ctx) return;
    
    apply_kerning_to_glyphs(ctx->shaped_glyphs, ctx->shaped_count, ctx->font_info);
    ctx->total_positioning_adjustments += ctx->shaped_count - 1;
}

void calculate_final_positions(OpenTypeShapingContext* ctx) {
    if (!ctx) return;
    
    int current_x = 0;
    
    for (int i = 0; i < ctx->shaped_count; i++) {
        AdvancedGlyphInfo* glyph = &ctx->shaped_glyphs[i];
        
        // Apply positioning offset
        glyph->offset_x += current_x;
        
        // Advance position
        current_x += glyph->advance_x;
    }
}

// === Utility Functions ===

OpenTypeFeatureTag make_feature_tag(const char* tag_string) {
    if (!tag_string || strlen(tag_string) != 4) return 0;
    
    return (tag_string[0] << 24) | (tag_string[1] << 16) | (tag_string[2] << 8) | tag_string[3];
}

void feature_tag_to_string(OpenTypeFeatureTag tag, char* output) {
    if (!output) return;
    
    output[0] = (tag >> 24) & 0xFF;
    output[1] = (tag >> 16) & 0xFF;
    output[2] = (tag >> 8) & 0xFF;
    output[3] = tag & 0xFF;
    output[4] = '\0';
}

const char* get_feature_description(OpenTypeFeatureTag tag) {
    switch (tag) {
        case OT_FEATURE_KERN: return "Kerning";
        case OT_FEATURE_LIGA: return "Standard Ligatures";
        case OT_FEATURE_DLIG: return "Discretionary Ligatures";
        case OT_FEATURE_CLIG: return "Contextual Ligatures";
        case OT_FEATURE_SMCP: return "Small Capitals";
        case OT_FEATURE_ONUM: return "Oldstyle Figures";
        default: return "Unknown Feature";
    }
}

// === Memory Management ===

void cleanup_advanced_glyph_info(AdvancedGlyphInfo* glyph) {
    if (!glyph) return;
    
    if (glyph->applied_features) {
        free(glyph->applied_features);
    }
    
    memset(glyph, 0, sizeof(AdvancedGlyphInfo));
}

void cleanup_ligature_info(LigatureInfo* ligature) {
    if (!ligature) return;
    
    if (ligature->input_codepoints) {
        free(ligature->input_codepoints);
    }
    
    memset(ligature, 0, sizeof(LigatureInfo));
}

// === Integration Functions ===

void enhance_font_box_with_opentype(EnhancedFontBox* font_box, OpenTypeFontInfo* ot_info) {
    if (!font_box || !ot_info) return;
    
    // This would integrate OpenType capabilities with the enhanced font box
    log_debug("Enhanced font box with OpenType capabilities");
}

int calculate_text_width_with_opentype(OpenTypeShapingContext* ctx, const uint32_t* codepoints, int count) {
    if (!ctx || !codepoints || count <= 0) return 0;
    
    // Shape the text
    int shaped_count = shape_text_with_opentype(ctx, codepoints, count);
    
    // Calculate total width
    int total_width = 0;
    for (int i = 0; i < shaped_count; i++) {
        total_width += ctx->shaped_glyphs[i].advance_x;
    }
    
    return total_width;
}

// === Debugging Functions ===

void debug_print_shaped_glyphs(OpenTypeShapingContext* ctx) {
    if (!ctx) return;
    
    log_debug("=== Shaped Glyphs ===");
    for (int i = 0; i < ctx->shaped_count; i++) {
        AdvancedGlyphInfo* glyph = &ctx->shaped_glyphs[i];
        log_debug("Glyph %d: U+%04X -> U+%04X (advance: %d, offset: %d,%d, ligature: %s, kerning: %s)",
                 i, glyph->original_codepoint, glyph->rendered_codepoint,
                 glyph->advance_x, glyph->offset_x, glyph->offset_y,
                 glyph->is_ligature ? "yes" : "no",
                 glyph->has_kerning ? "yes" : "no");
    }
    log_debug("=== End Shaped Glyphs ===");
}
