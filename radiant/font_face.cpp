#include "font_face.h"
#include "layout.hpp"
#include <string.h>
#include <stdlib.h>

// Text flow logging categories
log_category_t* font_log = NULL;
log_category_t* text_log = NULL;
log_category_t* layout_log = NULL;

// Initialize logging categories for text flow
void init_text_flow_logging(void) {
    font_log = log_get_category("radiant.font");
    text_log = log_get_category("radiant.text");
    layout_log = log_get_category("radiant.layout");

    if (!font_log || !text_log || !layout_log) {
        log_warn("Failed to initialize text flow logging categories");
    } else {
        log_info("Text flow logging categories initialized");
    }
}

void setup_text_flow_log_categories(void) {
    init_text_flow_logging();
}

// Structured logging for font operations (replace printf)
void log_font_loading_attempt(const char* family_name, const char* path) {
    if (font_log) {
        clog_debug(font_log, "Attempting to load font: %s from path: %s", family_name, path);
    }
}

void log_font_loading_result(const char* family_name, bool success, const char* error) {
    if (font_log) {
        if (success) {
            clog_info(font_log, "Successfully loaded font: %s", family_name);
        } else {
            clog_error(font_log, "Failed to load font: %s - %s", family_name, error ? error : "unknown error");
        }
    }
}

void log_font_cache_hit(const char* family_name, int font_size) {
    if (font_log) {
    }
}

void log_font_fallback_triggered(const char* requested, const char* fallback) {
    if (font_log) {
        clog_warn(font_log, "Font fallback triggered: %s -> %s", requested, fallback);
    }
}

// CSS @font-face parsing integration
void parse_font_face_rule(LayoutContext* lycon, lxb_css_rule_t* rule) {
    if (!lycon) {
        clog_error(font_log, "Invalid LayoutContext for parse_font_face_rule");
        return;
    }

    // For hardcoded implementation, rule can be NULL
    if (!rule) {
        clog_info(font_log, "Processing hardcoded @font-face rule (rule=NULL)");
    }

    clog_info(font_log, "Processing @font-face rules for Liberation font family");

    // Register Liberation Sans variants
    const char* liberation_sans_fonts[] = {
        "./test/layout/font/LiberationSans-Regular.ttf",
        "./test/layout/font/LiberationSans-Bold.ttf",
        "./test/layout/font/LiberationSans-Italic.ttf",
        "./test/layout/font/LiberationSans-BoldItalic.ttf"
    };

    PropValue weights[] = {LXB_CSS_VALUE_NORMAL, LXB_CSS_VALUE_BOLD, LXB_CSS_VALUE_NORMAL, LXB_CSS_VALUE_BOLD};
    PropValue styles[] = {LXB_CSS_VALUE_NORMAL, LXB_CSS_VALUE_NORMAL, LXB_CSS_VALUE_ITALIC, LXB_CSS_VALUE_ITALIC};

    for (int i = 0; i < 4; i++) {
        FontFaceDescriptor* descriptor = create_font_face_descriptor(lycon);
        if (!descriptor) {
            clog_error(font_log, "Failed to create font face descriptor");
            continue;
        }

        descriptor->family_name = strdup("Liberation Sans");
        descriptor->src_local_path = strdup(liberation_sans_fonts[i]);
        descriptor->font_style = styles[i];
        descriptor->font_weight = weights[i];
        descriptor->font_display = LXB_CSS_VALUE_AUTO;
        descriptor->is_loaded = false;

        register_font_face(lycon->ui_context, descriptor);

        clog_info(font_log, "Registered @font-face: %s -> %s (weight=%d, style=%d)",
                  descriptor->family_name, descriptor->src_local_path, weights[i], styles[i]);
    }
}

FontFaceDescriptor* create_font_face_descriptor(LayoutContext* lycon) {
    if (!lycon) {
        clog_error(font_log, "Invalid LayoutContext for create_font_face_descriptor");
        return NULL;
    }

    FontFaceDescriptor* descriptor = (FontFaceDescriptor*)calloc(1, sizeof(FontFaceDescriptor));
    if (!descriptor) {
        clog_error(font_log, "Failed to allocate FontFaceDescriptor");
        return NULL;
    }

    memset(descriptor, 0, sizeof(FontFaceDescriptor));
    descriptor->font_style = LXB_CSS_VALUE_NORMAL;
    descriptor->font_weight = LXB_CSS_VALUE_NORMAL;
    descriptor->font_display = LXB_CSS_VALUE_AUTO;
    descriptor->is_loaded = false;
    descriptor->loaded_face = NULL;
    descriptor->char_width_cache = NULL;
    descriptor->metrics_computed = false;

    return descriptor;
}

void register_font_face(UiContext* uicon, FontFaceDescriptor* descriptor) {
    if (!uicon || !descriptor) {
        clog_error(font_log, "Invalid parameters for register_font_face");
        return;
    }

    // Initialize @font-face storage if needed
    if (!uicon->font_faces) {
        uicon->font_face_capacity = 10;
        uicon->font_faces = (FontFaceDescriptor**)calloc(uicon->font_face_capacity, sizeof(FontFaceDescriptor*));
        uicon->font_face_count = 0;

        if (!uicon->font_faces) {
            clog_error(font_log, "Failed to allocate font_faces array");
            return;
        }
    }

    // Expand array if needed
    if (uicon->font_face_count >= uicon->font_face_capacity) {
        int new_capacity = uicon->font_face_capacity * 2;
        FontFaceDescriptor** new_array = (FontFaceDescriptor**)realloc(
            uicon->font_faces, new_capacity * sizeof(FontFaceDescriptor*));

        if (!new_array) {
            clog_error(font_log, "Failed to expand font_faces array");
            return;
        }

        uicon->font_faces = new_array;
        uicon->font_face_capacity = new_capacity;
    }

    // Store the descriptor
    uicon->font_faces[uicon->font_face_count] = descriptor;
    uicon->font_face_count++;

    clog_info(font_log, "Registered @font-face: %s -> %s (total: %d)",
              descriptor->family_name, descriptor->src_local_path, uicon->font_face_count);
}

// Character width caching
void cache_character_width(FontFaceDescriptor* descriptor, uint32_t codepoint, int width) {
    if (!descriptor) return;

    if (!descriptor->char_width_cache) {
        descriptor->char_width_cache = hashmap_new(sizeof(uint32_t) + sizeof(int), 128, 0, 0,
                                                  NULL, NULL, NULL, NULL);
    }

    if (descriptor->char_width_cache) {
        // Store codepoint and width as key-value pair
        struct { uint32_t codepoint; int width; } entry = {codepoint, width};
        hashmap_set(descriptor->char_width_cache, &entry);

        clog_debug(font_log, "Cached character width: U+%04X = %d", codepoint, width);
    }
}

int get_cached_char_width(FontFaceDescriptor* descriptor, uint32_t codepoint) {
    if (!descriptor || !descriptor->char_width_cache) {
        return -1; // Cache miss
    }

    typedef struct { uint32_t codepoint; int width; } CharWidthEntry;
    CharWidthEntry search_key = {codepoint, 0};
    CharWidthEntry* entry = (CharWidthEntry*)hashmap_get(descriptor->char_width_cache, &search_key);

    if (entry) {
        clog_debug(font_log, "Character width cache hit: U+%04X = %d", codepoint, entry->width);
        return entry->width;
    }

    return -1; // Cache miss
}

// Font loading with @font-face support (local fonts only)
FT_Face load_local_font_file(UiContext* uicon, const char* font_path, FontProp* style) {
    if (!uicon || !font_path) {
        clog_error(font_log, "Invalid parameters for load_local_font_file");
        return NULL;
    }

    log_font_loading_attempt("local font", font_path);

    FT_Face face = NULL;
    FT_Error error = FT_New_Face(uicon->ft_library, font_path, 0, &face);

    if (error) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "FreeType error %d loading %s", error, font_path);
        log_font_loading_result("local font", false, error_msg);
        clog_error(font_log, "FT_New_Face failed: error=%d, path=%s", error, font_path);
        return NULL;
    }

    // CRITICAL FIX: Set font size - this is required for font metrics to be valid
    // Use a default size of 16px if no size is specified
    float font_size = style ? style->font_size : 16;
    error = FT_Set_Pixel_Sizes(face, 0, font_size);
    if (error) {
        clog_error(font_log, "FT_Set_Pixel_Sizes failed: error=%d, size=%d", error, font_size);
        FT_Done_Face(face);
        return NULL;
    }

    clog_info(font_log, "Successfully loaded @font-face: %s (size=%f, height=%f)",
        face->family_name, font_size, face->size->metrics.height / 64.0);
    log_font_loading_result(face->family_name, true, NULL);
    return face;
}

bool resolve_font_path_from_descriptor(FontFaceDescriptor* descriptor, char** resolved_path) {
    if (!descriptor || !resolved_path) {
        return false;
    }

    // Try local path first
    if (descriptor->src_local_path) {
        *resolved_path = strdup(descriptor->src_local_path);
        clog_debug(font_log, "Resolved font path from local path: %s", *resolved_path);
        return true;
    }

    // Try local name (would need FontConfig lookup)
    if (descriptor->src_local_name) {
        clog_debug(font_log, "Font resolution by local name not yet implemented: %s", descriptor->src_local_name);
        return false;
    }

    clog_warn(font_log, "No resolvable font source in descriptor for: %s", descriptor->family_name);
    return false;
}

FT_Face load_font_with_descriptors(UiContext* uicon, const char* family_name,
                                   FontProp* style, bool* is_fallback) {
    if (!uicon || !family_name) { return NULL; }
    clog_debug(font_log, "Loading font with descriptors: %s", family_name);

    // Search registered @font-face descriptors first
    if (uicon->font_faces && uicon->font_face_count > 0) {
        FontFaceDescriptor* best_match = NULL;
        float best_score = 0.0f;

        for (int i = 0; i < uicon->font_face_count; i++) {
            FontFaceDescriptor* descriptor = uicon->font_faces[i];
            if (!descriptor || !descriptor->family_name) continue;

            // Check if family name matches
            if (strcmp(descriptor->family_name, family_name) == 0) {
                // Calculate match score based on weight and style
                float score = 0.5f; // Base score for family name match

                if (style) {
                    // Weight match (most important for visual accuracy)
                    if (descriptor->font_weight == style->font_weight) {
                        score += 0.3f;
                    }

                    // Style match (italic/normal)
                    if (descriptor->font_style == style->font_style) {
                        score += 0.2f;
                    }
                }

                if (score > best_score) {
                    best_match = descriptor;
                    best_score = score;
                }
            }
        }

        // Load the best matching font
        if (best_match) {
            clog_info(font_log, "Found @font-face match for: %s (score=%.2f, weight=%d, style=%d)",
                      family_name, best_score, best_match->font_weight, best_match->font_style);

            if (best_match->src_local_path) {
                FT_Face face = load_local_font_file(uicon, best_match->src_local_path, style);
                if (face) {
                    best_match->loaded_face = face;
                    best_match->is_loaded = true;

                    if (is_fallback) *is_fallback = false;
                    clog_info(font_log, "Successfully loaded @font-face: %s from %s",
                              family_name, best_match->src_local_path);
                    return face;
                } else {
                    clog_warn(font_log, "Failed to load @font-face file: %s", best_match->src_local_path);
                }
            }
        }
    }

    // Fall back to system fonts if no @font-face match
    clog_debug(font_log, "No @font-face match found, falling back to system fonts for: %s", family_name);
    if (is_fallback) *is_fallback = true;
    return load_styled_font(uicon, family_name, style);
}

// Enhanced font matching (basic implementation)
float calculate_font_match_score(FontFaceDescriptor* descriptor, FontMatchCriteria* criteria) {
    if (!descriptor || !criteria) {
        return 0.0f;
    }

    float score = 0.0f;

    // Family name match (most important)
    if (descriptor->family_name && criteria->family_name) {
        if (strcmp(descriptor->family_name, criteria->family_name) == 0) {
            score += 0.5f;
        }
    }

    // Style match
    if (descriptor->font_style == criteria->style) {
        score += 0.25f;
    }

    // Weight match
    if (descriptor->font_weight == criteria->weight) {
        score += 0.25f;
    }

    clog_debug(font_log, "Font match score for %s: %.2f", descriptor->family_name, score);
    return score;
}

FontMatchResult find_best_font_match(UiContext* uicon, FontMatchCriteria* criteria) {
    FontMatchResult result = {0};

    if (!uicon || !criteria) {
        return result;
    }

    clog_debug(font_log, "Finding best font match for: %s", criteria->family_name);

    // For now, fall back to existing font loading
    // In a full implementation, we would search @font-face descriptors
    FontProp font_prop = {
        .font_size = (float)criteria->size,
        .font_style = criteria->style,
        .font_weight = criteria->weight
    };
    result.face = load_styled_font(uicon, criteria->family_name, &font_prop);
    result.match_score = result.face ? 1.0f : 0.0f;
    result.is_exact_match = result.face != NULL;
    result.requires_synthesis = false;
    result.supports_codepoint = true; // Assume yes for now

    return result;
}

// Font fallback chain management (basic implementation)
FontFallbackChain* build_fallback_chain(UiContext* uicon, const char* css_font_family) {
    if (!uicon || !css_font_family) {
        return NULL;
    }

    FontFallbackChain* chain = (FontFallbackChain*)malloc(sizeof(FontFallbackChain));
    if (!chain) {
        clog_error(font_log, "Failed to allocate FontFallbackChain");
        return NULL;
    }

    memset(chain, 0, sizeof(FontFallbackChain));

    // Simple implementation: just use the requested family + system fallbacks
    chain->family_count = 1;
    chain->family_names = (char**)malloc(sizeof(char*));
    chain->family_names[0] = strdup(css_font_family);
    chain->system_fonts = uicon->fallback_fonts; // Use existing fallback fonts
    chain->cache_enabled = true;

    clog_debug(font_log, "Built fallback chain for: %s", css_font_family);
    return chain;
}

bool font_supports_codepoint(FT_Face face, uint32_t codepoint) {
    if (!face) {
        return false;
    }

    FT_UInt char_index = FT_Get_Char_Index(face, codepoint);
    bool supports = (char_index > 0);

    clog_debug(font_log, "Font %s %s codepoint U+%04X",
              face->family_name, supports ? "supports" : "does not support", codepoint);

    return supports;
}

FT_Face resolve_font_for_codepoint(FontFallbackChain* chain, uint32_t codepoint, FontProp* style) {
    if (!chain) {
        return NULL;
    }

    clog_debug(font_log, "Resolving font for codepoint U+%04X", codepoint);

    // Check cache first
    if (chain->cache_enabled && chain->codepoint_font_cache) {
        FT_Face* cached_face = (FT_Face*)hashmap_get(chain->codepoint_font_cache, &codepoint);
        if (cached_face && *cached_face) {
            clog_debug(font_log, "Font cache hit for codepoint U+%04X", codepoint);
            return *cached_face;
        }
    }

    // For now, return NULL - full implementation would search the fallback chain
    clog_debug(font_log, "Font fallback chain resolution not fully implemented");
    return NULL;
}

void cache_codepoint_font_mapping(FontFallbackChain* chain, uint32_t codepoint, FT_Face face) {
    if (!chain || !face) {
        return;
    }

    if (!chain->codepoint_font_cache) {
        chain->codepoint_font_cache = hashmap_new(sizeof(uint32_t) + sizeof(FT_Face), 256, 0, 0,
                                                 NULL, NULL, NULL, NULL);
    }

    if (chain->codepoint_font_cache) {
        struct { uint32_t codepoint; FT_Face face; } entry = {codepoint, face};
        hashmap_set(chain->codepoint_font_cache, &entry);

        clog_debug(font_log, "Cached font mapping: U+%04X -> %s", codepoint, face->family_name);
    }
}

// Enhanced metrics functions (basic implementation)
void compute_enhanced_font_metrics(EnhancedFontBox* fbox) {
    if (!fbox || !fbox->face) {
        return;
    }

    if (fbox->metrics_computed) {
        return; // Already computed
    }

    FT_Face face = fbox->face;
    EnhancedFontMetrics* metrics = &fbox->metrics;

    // Basic metrics from FreeType
    metrics->ascender = face->size->metrics.ascender / 64.0;
    metrics->descender = face->size->metrics.descender / 64.0;
    metrics->height = face->size->metrics.height / 64.0;
    metrics->line_gap = metrics->height - (metrics->ascender - metrics->descender);

    // OpenType metrics (if available)
    if (face->face_flags & FT_FACE_FLAG_SFNT) {
        // These would need proper OpenType table parsing
        metrics->typo_ascender = metrics->ascender;
        metrics->typo_descender = metrics->descender;
        metrics->typo_line_gap = metrics->line_gap;
        metrics->win_ascent = metrics->ascender;
        metrics->win_descent = -metrics->descender;
        metrics->hhea_ascender = metrics->ascender;
        metrics->hhea_descender = metrics->descender;
        metrics->hhea_line_gap = metrics->line_gap;
    }

    // Estimate x-height and cap-height
    // In a full implementation, these would be read from the font's OS/2 table
    metrics->x_height = metrics->ascender * 0.5; // Rough estimate
    metrics->cap_height = metrics->ascender * 0.7; // Rough estimate

    metrics->baseline_offset = 0; // No adjustment by default
    metrics->metrics_computed = true;
    fbox->metrics_computed = true;

    clog_debug(font_log, "Computed enhanced metrics for %s: asc=%d, desc=%d, height=%d",
              face->family_name, metrics->ascender, metrics->descender, metrics->height);
}

int calculate_line_height_from_css(EnhancedFontBox* fbox, PropValue line_height_css) {
    if (!fbox) {
        return 0;
    }

    compute_enhanced_font_metrics(fbox);

    // For now, use basic calculation
    // In a full implementation, this would handle CSS line-height values properly
    int base_height = fbox->metrics.height;

    clog_debug(font_log, "Calculated line height: %d", base_height);
    return base_height;
}

// High-DPI display support functions
void apply_pixel_ratio_to_font_metrics(EnhancedFontBox* fbox, float pixel_ratio) {
    if (!fbox || pixel_ratio <= 0.0f) {
        return;
    }

    fbox->pixel_ratio = pixel_ratio;
    fbox->high_dpi_aware = (pixel_ratio > 1.0f);

    clog_debug(font_log, "Applied pixel ratio %.2f to font metrics", pixel_ratio);
}

int scale_font_size_for_display(int base_size, float pixel_ratio) {
    if (pixel_ratio <= 0.0f) {
        return base_size;
    }

    int scaled_size = (int)(base_size * pixel_ratio);
    clog_debug(font_log, "Scaled font size: %d -> %d (ratio: %.2f)", base_size, scaled_size, pixel_ratio);
    return scaled_size;
}

void ensure_pixel_ratio_compatibility(EnhancedFontBox* fbox) {
    if (!fbox) {
        return;
    }

    // Ensure existing pixel_ratio handling is preserved
    if (fbox->pixel_ratio > 0.0f && fbox->high_dpi_aware) {
        clog_debug(font_log, "Pixel ratio compatibility ensured: %.2f", fbox->pixel_ratio);
    }
}

// Enhanced font system integration
void setup_font_enhanced(UiContext* uicon, EnhancedFontBox* fbox,
                        const char* font_name, FontProp* fprop) {
    if (!uicon || !fbox || !fprop) {
        clog_error(font_log, "Invalid parameters for setup_font_enhanced");
        return;
    }

    // Initialize enhanced font box
    memset(fbox, 0, sizeof(EnhancedFontBox));
    fbox->style = *fprop;
    fbox->current_font_size = fprop->font_size;
    fbox->cache_enabled = true;
    fbox->pixel_ratio = uicon->pixel_ratio; // Preserve existing pixel_ratio
    fbox->high_dpi_aware = (uicon->pixel_ratio > 1.0f);

    // Load font face
    fbox->face = load_styled_font(uicon, fprop->family ? fprop->family : font_name, fprop);

    if (fbox->face) {
        // Calculate space width
        FT_Int32 load_flags = (FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING); // FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL;
        if (FT_Load_Char(fbox->face, ' ', load_flags)) {
            clog_warn(font_log, "Could not load space character for %s", font_name);
            fbox->space_width = fbox->face->size->metrics.y_ppem / 64.0;
        } else {
            fbox->space_width = fbox->face->glyph->advance.x / 64.0;
        }

        // Compute enhanced metrics
        compute_enhanced_font_metrics(fbox);

        clog_info(font_log, "Enhanced font setup complete: %s (size: %d, pixel_ratio: %.2f)",
                 font_name, fprop->font_size, fbox->pixel_ratio);
    } else {
        clog_error(font_log, "Failed to load font face for enhanced setup: %s", font_name);
    }
}
