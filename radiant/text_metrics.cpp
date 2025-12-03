#include "text_metrics.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

// Enhanced font metrics computation
void compute_advanced_font_metrics(EnhancedFontBox* fbox) {
    if (!fbox || !fbox->face) {
        log_error(font_log, "Invalid parameters for compute_advanced_font_metrics");
        return;
    }

    if (fbox->metrics_computed) {
        return; // Already computed
    }

    FT_Face face = fbox->face;
    EnhancedFontMetrics* metrics = &fbox->metrics;

    log_debug(font_log, "Computing advanced font metrics for: %s", face->family_name);

    // Basic metrics from FreeType
    metrics->ascender = face->size->metrics.ascender >> 6;
    metrics->descender = face->size->metrics.descender >> 6;
    metrics->height = face->size->metrics.height >> 6;
    metrics->line_gap = metrics->height - (metrics->ascender - metrics->descender);

    // Compute OpenType metrics
    compute_opentype_metrics(fbox);

    // Compute baseline metrics
    compute_baseline_metrics(fbox);

    // Apply pixel ratio scaling if needed
    if (fbox->pixel_ratio > 1.0f) {
        metrics->ascender = (int)(metrics->ascender * fbox->pixel_ratio);
        metrics->descender = (int)(metrics->descender * fbox->pixel_ratio);
        metrics->height = (int)(metrics->height * fbox->pixel_ratio);
        metrics->line_gap = (int)(metrics->line_gap * fbox->pixel_ratio);

        log_debug(font_log, "Applied pixel ratio %.2f to font metrics", fbox->pixel_ratio);
    }

    metrics->metrics_computed = true;
    fbox->metrics_computed = true;

    log_info(font_log, "Advanced font metrics computed: %s (asc=%d, desc=%d, height=%d)",
             face->family_name, metrics->ascender, metrics->descender, metrics->height);
}

void compute_opentype_metrics(EnhancedFontBox* fbox) {
    if (!fbox || !fbox->face) return;

    FT_Face face = fbox->face;
    EnhancedFontMetrics* metrics = &fbox->metrics;

    // Read actual OS/2 table metrics for SFNT fonts
    // Reference: CSS Inline Layout Module Level 3 §5.1 and Chrome Blink simple_font_data.cc
    if (face->face_flags & FT_FACE_FLAG_SFNT) {
        TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
        if (os2) {
            // Convert from font units to pixels
            float scale = (float)face->size->metrics.y_ppem / face->units_per_EM;

            // OS/2 table sTypo* metrics (preferred for line height per CSS spec)
            metrics->typo_ascender = (int)(os2->sTypoAscender * scale);
            metrics->typo_descender = (int)(os2->sTypoDescender * scale);  // Typically negative
            // CSS spec: line gap must be floored at zero
            metrics->typo_line_gap = (os2->sTypoLineGap > 0) ? (int)(os2->sTypoLineGap * scale) : 0;

            // OS/2 table usWin* metrics (for clipping bounds)
            metrics->win_ascent = (int)(os2->usWinAscent * scale);
            metrics->win_descent = (int)(os2->usWinDescent * scale);  // Positive value

            log_debug(font_log, "OS/2 table metrics for %s: sTypo(%d,%d,%d) usWin(%d,%d)",
                      face->family_name, metrics->typo_ascender, metrics->typo_descender,
                      metrics->typo_line_gap, metrics->win_ascent, metrics->win_descent);
        } else {
            // No OS/2 table, fall back to basic metrics
            metrics->typo_ascender = metrics->ascender;
            metrics->typo_descender = metrics->descender;
            metrics->typo_line_gap = metrics->line_gap;
            metrics->win_ascent = metrics->ascender;
            metrics->win_descent = -metrics->descender;
            log_debug(font_log, "No OS/2 table for SFNT font %s, using basic metrics", face->family_name);
        }

        // HHEA table metrics (FreeType's default source for face->size->metrics)
        // These are already what FreeType provides in face->size->metrics
        metrics->hhea_ascender = metrics->ascender;
        metrics->hhea_descender = metrics->descender;
        metrics->hhea_line_gap = metrics->line_gap;

        log_debug(font_log, "OpenType metrics computed for SFNT font: %s", face->family_name);
    } else {
        // Non-SFNT font, use basic metrics
        metrics->typo_ascender = metrics->ascender;
        metrics->typo_descender = metrics->descender;
        metrics->typo_line_gap = metrics->line_gap;
        metrics->win_ascent = metrics->ascender;
        metrics->win_descent = -metrics->descender;
        metrics->hhea_ascender = metrics->ascender;
        metrics->hhea_descender = metrics->descender;
        metrics->hhea_line_gap = metrics->line_gap;

        log_debug(font_log, "Basic metrics used for non-SFNT font: %s", face->family_name);
    }
}

void compute_baseline_metrics(EnhancedFontBox* fbox) {
    if (!fbox || !fbox->face) return;

    FT_Face face = fbox->face;
    EnhancedFontMetrics* metrics = &fbox->metrics;

    // Estimate x-height and cap-height
    // In a full implementation, these would be read from the font's OS/2 table

    // Try to load 'x' character for x-height
    FT_UInt x_index = FT_Get_Char_Index(face, 'x');
    if (x_index > 0) {
        FT_Error error = FT_Load_Glyph(face, x_index, FT_LOAD_DEFAULT);
        if (!error) {
            metrics->x_height = face->glyph->metrics.height >> 6;
            log_debug(font_log, "X-height measured from 'x' character: %d", metrics->x_height);
        } else {
            metrics->x_height = metrics->ascender * 0.5; // Fallback estimate
            log_debug(font_log, "X-height estimated: %d", metrics->x_height);
        }
    } else {
        metrics->x_height = metrics->ascender * 0.5; // Fallback estimate
        log_debug(font_log, "X-height estimated (no 'x' glyph): %d", metrics->x_height);
    }

    // Try to load 'H' character for cap-height
    FT_UInt h_index = FT_Get_Char_Index(face, 'H');
    if (h_index > 0) {
        FT_Error error = FT_Load_Glyph(face, h_index, FT_LOAD_DEFAULT);
        if (!error) {
            metrics->cap_height = face->glyph->metrics.height >> 6;
            log_debug(font_log, "Cap-height measured from 'H' character: %d", metrics->cap_height);
        } else {
            metrics->cap_height = metrics->ascender * 0.7; // Fallback estimate
            log_debug(font_log, "Cap-height estimated: %d", metrics->cap_height);
        }
    } else {
        metrics->cap_height = metrics->ascender * 0.7; // Fallback estimate
        log_debug(font_log, "Cap-height estimated (no 'H' glyph): %d", metrics->cap_height);
    }

    // Baseline offset (usually 0 for normal fonts)
    metrics->baseline_offset = 0;
}

// Character metrics functions
AdvancedCharacterMetrics* get_advanced_character_metrics(EnhancedFontBox* fbox, uint32_t codepoint) {
    if (!fbox) {
        log_error(text_log, "Invalid font box for character metrics: U+%04X", codepoint);
        return NULL;
    }

    // Check cache first
    if (is_character_metrics_cached(fbox, codepoint)) {
        log_debug(text_log, "Character metrics cache hit: U+%04X", codepoint);
        // In a full implementation, return cached metrics
        return NULL; // Placeholder
    }

    // Compute new metrics
    AdvancedCharacterMetrics* metrics = (AdvancedCharacterMetrics*)malloc(sizeof(AdvancedCharacterMetrics));
    if (!metrics) {
        log_error(text_log, "Failed to allocate character metrics for U+%04X", codepoint);
        return NULL;
    }

    memset(metrics, 0, sizeof(AdvancedCharacterMetrics));
    metrics->codepoint = codepoint;
    metrics->pixel_ratio = fbox->pixel_ratio;
    metrics->scaled_for_display = fbox->high_dpi_aware;

    // Load glyph to get metrics
    if (fbox->face) {
        FT_UInt glyph_index = FT_Get_Char_Index(fbox->face, codepoint);
        if (glyph_index > 0) {
            FT_Error error = FT_Load_Glyph(fbox->face, glyph_index, FT_LOAD_DEFAULT);
            if (!error) {
                FT_GlyphSlot glyph = fbox->face->glyph;

                // Basic metrics
                metrics->advance_x = glyph->advance.x >> 6;
                metrics->advance_y = glyph->advance.y >> 6;
                metrics->bearing_x = glyph->metrics.horiBearingX >> 6;
                metrics->bearing_y = glyph->metrics.horiBearingY >> 6;
                metrics->width = glyph->metrics.width >> 6;
                metrics->height = glyph->metrics.height >> 6;

                // Advanced metrics
                metrics->left_side_bearing = metrics->bearing_x;
                metrics->right_side_bearing = metrics->advance_x - (metrics->bearing_x + metrics->width);
                metrics->top_side_bearing = metrics->bearing_y;
                metrics->bottom_side_bearing = metrics->bearing_y - metrics->height;

                // Baseline information
                metrics->baseline_offset = 0; // Relative to baseline
                metrics->ascender_offset = metrics->bearing_y - fbox->metrics.ascender;
                metrics->descender_offset = (metrics->bearing_y - metrics->height) - fbox->metrics.descender;

                metrics->is_cached = false;
                metrics->cache_timestamp = 0; // Would use actual timestamp in full implementation

                log_debug(text_log, "Computed advanced metrics for U+%04X: advance=%d, width=%d, height=%d",
                         codepoint, metrics->advance_x, metrics->width, metrics->height);

                // Cache the metrics
                cache_advanced_character_metrics(fbox, codepoint, metrics);

                return metrics;
            } else {
                log_warn(text_log, "Failed to load glyph for U+%04X", codepoint);
            }
        } else {
            log_warn(text_log, "No glyph index for U+%04X", codepoint);
        }
    }

    free(metrics);
    return NULL;
}

void cache_advanced_character_metrics(EnhancedFontBox* fbox, uint32_t codepoint, AdvancedCharacterMetrics* metrics) {
    if (!fbox || !metrics || !fbox->cache_enabled) {
        return;
    }

    // Initialize cache if needed
    if (!fbox->char_width_cache) {
        fbox->char_width_cache = hashmap_new(sizeof(uint32_t) + sizeof(AdvancedCharacterMetrics), 256, 0, 0,
                                            NULL, NULL, NULL, NULL);
    }

    if (fbox->char_width_cache) {
        struct { uint32_t codepoint; AdvancedCharacterMetrics metrics; } entry = {codepoint, *metrics};
        hashmap_set(fbox->char_width_cache, &entry);

        log_debug(text_log, "Cached advanced character metrics: U+%04X", codepoint);
    }
}

bool is_character_metrics_cached(EnhancedFontBox* fbox, uint32_t codepoint) {
    if (!fbox || !fbox->cache_enabled || !fbox->char_width_cache) {
        return false;
    }

    struct { uint32_t codepoint; AdvancedCharacterMetrics metrics; } search_key = {codepoint, {0}};
    void* entry = hashmap_get(fbox->char_width_cache, &search_key);

    return (entry != NULL);
}

// Unicode character rendering
AdvancedGlyphRenderInfo* render_unicode_character(UnicodeRenderContext* ctx, uint32_t codepoint) {
    if (!ctx) {
        log_error(text_log, "Invalid render context for U+%04X", codepoint);
        return NULL;
    }

    log_debug(text_log, "Rendering Unicode character: U+%04X", codepoint);

    AdvancedGlyphRenderInfo* render_info = (AdvancedGlyphRenderInfo*)malloc(sizeof(AdvancedGlyphRenderInfo));
    if (!render_info) {
        log_error(text_log, "Failed to allocate render info for U+%04X", codepoint);
        return NULL;
    }

    memset(render_info, 0, sizeof(AdvancedGlyphRenderInfo));
    render_info->codepoint = codepoint;

    // Find appropriate font for this codepoint
    FT_Face font_face = find_font_for_codepoint(ctx, codepoint);
    if (!font_face) {
        log_warn(text_log, "No font found for U+%04X", codepoint);
        free(render_info);
        return NULL;
    }

    render_info->font_face = font_face;
    render_info->uses_fallback = (font_face != ctx->primary_font->face);

    if (render_info->uses_fallback) {
        render_info->fallback_font_name = strdup(font_face->family_name);
        log_font_fallback_usage(ctx->primary_font->face ? ctx->primary_font->face->family_name : "unknown",
                               font_face->family_name, codepoint);
    }

    // Load the glyph
    if (load_unicode_glyph(font_face, codepoint, &render_info->glyph)) {
        // Get advanced character metrics
        AdvancedCharacterMetrics* char_metrics = get_advanced_character_metrics(ctx->primary_font, codepoint);
        if (char_metrics) {
            render_info->metrics = *char_metrics;
            free(char_metrics); // We copied the data
        }

        // Apply rendering settings
        render_info->subpixel_x = 0.0f; // No subpixel positioning by default
        render_info->subpixel_y = 0.0f;
        render_info->pixel_x = 0; // Will be set during layout
        render_info->pixel_y = 0;
        render_info->hinting_applied = ctx->font_hinting;
        render_info->antialiasing_enabled = true; // Default to enabled
        render_info->rendering_quality = 2; // Medium quality

        log_debug(text_log, "Successfully rendered U+%04X using font: %s", codepoint, font_face->family_name);

        return render_info;
    } else {
        log_error(text_log, "Failed to load glyph for U+%04X", codepoint);
        if (render_info->fallback_font_name) {
            free(render_info->fallback_font_name);
        }
        free(render_info);
        return NULL;
    }
}

FT_Face find_font_for_codepoint(UnicodeRenderContext* ctx, uint32_t codepoint) {
    if (!ctx || !ctx->primary_font) {
        return NULL;
    }

    // Try primary font first
    if (ctx->primary_font->face) {
        FT_UInt glyph_index = FT_Get_Char_Index(ctx->primary_font->face, codepoint);
        if (glyph_index > 0) {
            log_debug(text_log, "Primary font supports U+%04X", codepoint);
            return ctx->primary_font->face;
        }
    }

    // Try fallback chain
    if (ctx->fallback_chain) {
        FT_Face fallback_face = resolve_font_for_codepoint(ctx->fallback_chain, codepoint, &ctx->primary_font->style);
        if (fallback_face) {
            log_debug(text_log, "Fallback font found for U+%04X: %s", codepoint, fallback_face->family_name);
            return fallback_face;
        }
    }

    log_warn(text_log, "No font found for U+%04X", codepoint);
    return NULL;
}

bool load_unicode_glyph(FT_Face face, uint32_t codepoint, FT_GlyphSlot* glyph) {
    if (!face || !glyph) {
        return false;
    }

    FT_UInt glyph_index = FT_Get_Char_Index(face, codepoint);
    if (glyph_index == 0) {
        return false;
    }

    FT_Error error = FT_Load_Glyph(face, glyph_index, (FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING));
    if (error) {
        log_debug(text_log, "FreeType error loading glyph U+%04X: %d", codepoint, error);
        return false;
    }

    *glyph = face->glyph;
    return true;
}

// Unicode text width calculation
int calculate_unicode_text_width(UnicodeRenderContext* ctx, const char* text, int length) {
    if (!ctx || !text || length <= 0) {
        return 0;
    }

    int total_width = 0;
    const char* ptr = text;
    const char* end = text + length;
    uint32_t prev_codepoint = 0;

    log_debug(text_log, "Calculating Unicode text width for %d bytes", length);

    while (ptr < end) {
        uint32_t codepoint;
        int bytes_consumed;

        // Decode UTF-8 character
        if (*ptr < 0x80) {
            // ASCII character
            codepoint = *ptr;
            bytes_consumed = 1;
        } else {
            // Multi-byte UTF-8 character
            bytes_consumed = utf8_to_codepoint(ptr, &codepoint);
            if (bytes_consumed <= 0) {
                // Invalid UTF-8, skip byte
                ptr++;
                continue;
            }
        }

        // Calculate character advance
        int char_advance = calculate_character_advance(ctx, codepoint);

        // Apply kerning if we have a previous character
        if (prev_codepoint != 0) {
            float kerning = calculate_kerning_adjustment(ctx, prev_codepoint, codepoint);
            char_advance += (int)kerning;
        }

        total_width += char_advance;
        prev_codepoint = codepoint;
        ptr += bytes_consumed;
    }

    log_debug(text_log, "Unicode text width calculated: %d pixels", total_width);
    return total_width;
}

int calculate_character_advance(UnicodeRenderContext* ctx, uint32_t codepoint) {
    if (!ctx || !ctx->primary_font) {
        return 0;
    }

    // Check cache first
    if (ctx->cache_enabled && ctx->metrics_cache) {
        struct { uint32_t codepoint; int advance; } search_key = {codepoint, 0};
        struct { uint32_t codepoint; int advance; }* cached =
            (struct { uint32_t codepoint; int advance; }*)hashmap_get(ctx->metrics_cache, &search_key);

        if (cached) {
            ctx->cache_hits++;
            return cached->advance;
        }
    }

    ctx->cache_misses++;

    // Find font for this codepoint
    FT_Face font_face = find_font_for_codepoint(ctx, codepoint);
    if (!font_face) {
        return ctx->primary_font->space_width; // Fallback to space width
    }

    // Load glyph and get advance
    FT_UInt glyph_index = FT_Get_Char_Index(font_face, codepoint);
    if (glyph_index > 0) {
        FT_Error error = FT_Load_Glyph(font_face, glyph_index, FT_LOAD_DEFAULT);
        if (!error) {
            int advance = font_face->glyph->advance.x >> 6;

            // Apply pixel ratio scaling
            if (ctx->pixel_ratio > 1.0f) {
                advance = (int)(advance * ctx->pixel_ratio);
            }

            // Cache the result
            if (ctx->cache_enabled && ctx->metrics_cache) {
                struct { uint32_t codepoint; int advance; } entry = {codepoint, advance};
                hashmap_set(ctx->metrics_cache, &entry);
            }

            return advance;
        }
    }

    return ctx->primary_font->space_width; // Fallback
}

float calculate_kerning_adjustment(UnicodeRenderContext* ctx, uint32_t left_char, uint32_t right_char) {
    if (!ctx || !ctx->primary_font || !ctx->primary_font->face) {
        return 0.0f;
    }

    FT_Face face = ctx->primary_font->face;

    // Check if font has kerning
    if (!FT_HAS_KERNING(face)) {
        return 0.0f;
    }

    FT_UInt left_index = FT_Get_Char_Index(face, left_char);
    FT_UInt right_index = FT_Get_Char_Index(face, right_char);

    if (left_index == 0 || right_index == 0) {
        return 0.0f;
    }

    FT_Vector kerning;
    FT_Error error = FT_Get_Kerning(face, left_index, right_index, FT_KERNING_DEFAULT, &kerning);

    if (error) {
        return 0.0f;
    }

    float kerning_pixels = (float)(kerning.x >> 6);

    // Apply pixel ratio scaling
    if (ctx->pixel_ratio > 1.0f) {
        kerning_pixels *= ctx->pixel_ratio;
    }

    if (kerning_pixels != 0.0f) {
        log_debug(text_log, "Kerning adjustment for U+%04X,U+%04X: %.2f", left_char, right_char, kerning_pixels);
    }

    return kerning_pixels;
}

// Context management
UnicodeRenderContext* create_unicode_render_context(UiContext* uicon, EnhancedFontBox* primary_font) {
    if (!uicon || !primary_font) {
        log_error(text_log, "Invalid parameters for create_unicode_render_context");
        return NULL;
    }

    UnicodeRenderContext* ctx = (UnicodeRenderContext*)malloc(sizeof(UnicodeRenderContext));
    if (!ctx) {
        log_error(text_log, "Failed to allocate Unicode render context");
        return NULL;
    }

    memset(ctx, 0, sizeof(UnicodeRenderContext));

    // Initialize context
    ctx->primary_font = primary_font;
    ctx->fallback_chain = build_fallback_chain(uicon, primary_font->style.family ? primary_font->style.family : "default");
    ctx->pixel_ratio = uicon->pixel_ratio;
    ctx->subpixel_positioning = false; // Disabled by default
    ctx->font_hinting = true; // Enabled by default
    ctx->text_direction = CSS_VALUE_LTR; // Default to LTR
    ctx->writing_mode = CSS_VALUE_HORIZONTAL_TB; // Default to horizontal
    ctx->language = strdup("en"); // Default to English

    // Initialize caches
    ctx->cache_enabled = true;
    ctx->glyph_cache = hashmap_new(sizeof(uint32_t) + sizeof(AdvancedGlyphRenderInfo*), 512, 0, 0,
                                  NULL, NULL, NULL, NULL);
    ctx->metrics_cache = hashmap_new(sizeof(uint32_t) + sizeof(int), 512, 0, 0,
                                    NULL, NULL, NULL, NULL);

    // Initialize counters
    ctx->cache_hits = 0;
    ctx->cache_misses = 0;
    ctx->debug_rendering = false;

    log_info(text_log, "Created Unicode render context (pixel_ratio: %.2f, hinting: %s)",
             ctx->pixel_ratio, ctx->font_hinting ? "enabled" : "disabled");

    return ctx;
}

void destroy_unicode_render_context(UnicodeRenderContext* ctx) {
    if (!ctx) {
        return;
    }

    log_info(text_log, "Destroying Unicode render context (cache hits: %d, misses: %d)",
             ctx->cache_hits, ctx->cache_misses);

    // Cleanup caches
    if (ctx->glyph_cache) {
        hashmap_free(ctx->glyph_cache);
    }
    if (ctx->metrics_cache) {
        hashmap_free(ctx->metrics_cache);
    }

    // Cleanup strings
    if (ctx->language) {
        free(ctx->language);
    }

    // Note: fallback_chain and primary_font are owned by other components

    free(ctx);
}

// Performance and debugging
void log_character_rendering(uint32_t codepoint, AdvancedGlyphRenderInfo* glyph_info) {
    if (!glyph_info || !text_log) {
        return;
    }

    log_debug(text_log, "Character rendering: U+%04X, font: %s, fallback: %s, advance: %d",
              codepoint,
              glyph_info->font_face ? glyph_info->font_face->family_name : "unknown",
              glyph_info->uses_fallback ? "yes" : "no",
              glyph_info->metrics.advance_x);
}

void log_rendering_performance(UnicodeRenderContext* ctx) {
    if (!ctx || !text_log) {
        return;
    }

    int total_requests = ctx->cache_hits + ctx->cache_misses;
    float hit_rate = total_requests > 0 ? (float)ctx->cache_hits / total_requests * 100.0f : 0.0f;

    log_info(text_log, "Rendering performance: cache hits: %d, misses: %d, hit rate: %.1f%%",
             ctx->cache_hits, ctx->cache_misses, hit_rate);
}
