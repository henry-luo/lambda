#include "font_metrics.h"
#include "../../lib/strbuf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// Font metrics creation and destruction
FontMetrics* font_metrics_create(ViewFont* font) {
    if (!font) return NULL;
    
    FontMetrics* metrics = calloc(1, sizeof(FontMetrics));
    if (!metrics) return NULL;
    
    metrics->source_font = font;
    metrics->font_size = font->size;
    metrics->is_valid = false;
    metrics->cache_timestamp = 0;
    
    font_retain(font);
    
    return metrics;
}

FontMetrics* font_metrics_create_for_size(ViewFont* font, double size) {
    if (!font || size <= 0) return NULL;
    
    FontMetrics* metrics = font_metrics_create(font);
    if (!metrics) return NULL;
    
    metrics->font_size = size;
    
    return metrics;
}

void font_metrics_destroy(FontMetrics* metrics) {
    if (!metrics) return;
    
    font_release(metrics->source_font);
    free(metrics->supported_ranges);
    free(metrics);
}

// Font metrics calculation
FontMetrics* font_calculate_metrics(ViewFont* font) {
    if (!font) return NULL;
    
    FontMetrics* metrics = font_metrics_create(font);
    if (!metrics) return NULL;
    
    // Set default values (in a real implementation, these would come from the font file)
    metrics->units_per_em = 1000;
    metrics->ascent = 800;      // 80% of em size
    metrics->descent = -200;    // 20% of em size
    metrics->line_height = 1200; // 120% of em size
    
    // Scale metrics to font size
    font_metrics_scale_for_size(metrics, font->size);
    
    // Calculate derived metrics
    metrics->average_char_width = metrics->em_size * 0.5;
    metrics->space_width = metrics->em_size * 0.25;
    metrics->em_width = metrics->em_size;
    metrics->en_width = metrics->em_size * 0.5;
    
    // Mathematical metrics
    metrics->math_axis_height = metrics->scaled_x_height * 0.5;
    metrics->superscript_offset = metrics->scaled_ascent * 0.6;
    metrics->subscript_offset = metrics->scaled_descent * 0.4;
    metrics->superscript_scale = 0.7;
    metrics->subscript_scale = 0.7;
    
    // Layout metrics
    metrics->baseline_to_baseline = metrics->scaled_line_height;
    metrics->leading = metrics->scaled_line_height - (metrics->scaled_ascent + metrics->scaled_descent);
    
    // Font properties (would be determined from actual font data)
    metrics->is_monospace = false;
    metrics->has_kerning = true;
    metrics->has_ligatures = true;
    metrics->supports_math = false;
    
    metrics->is_valid = true;
    metrics->cache_timestamp = time(NULL);
    
    return metrics;
}

void font_metrics_scale_for_size(FontMetrics* metrics, double size) {
    if (!metrics || size <= 0) return;
    
    double scale = size / metrics->units_per_em;
    
    metrics->scaled_ascent = metrics->ascent * scale;
    metrics->scaled_descent = abs(metrics->descent) * scale; // Make positive
    metrics->scaled_line_height = metrics->line_height * scale;
    metrics->scaled_x_height = metrics->units_per_em * 0.5 * scale; // Approximate x-height
    metrics->scaled_cap_height = metrics->units_per_em * 0.7 * scale; // Approximate cap height
    metrics->em_size = size;
    
    metrics->font_size = size;
}

bool font_metrics_update_if_needed(FontMetrics* metrics) {
    if (!metrics) return false;
    
    // Check if metrics need updating (font size changed, cache expired, etc.)
    if (!metrics->is_valid || metrics->font_size != metrics->source_font->size) {
        FontMetrics* new_metrics = font_calculate_metrics(metrics->source_font);
        if (new_metrics) {
            // Copy new metrics to existing structure
            memcpy(metrics, new_metrics, sizeof(FontMetrics));
            free(new_metrics); // Free the temporary structure (but not its contents)
            return true;
        }
    }
    
    return false;
}

// Font metrics access
FontMetrics* font_get_metrics(ViewFont* font) {
    if (!font) return NULL;
    
    // Check if metrics are cached and valid
    if (font->cached_metrics && font->metrics_valid) {
        return font->cached_metrics;
    }
    
    // Calculate new metrics
    FontMetrics* metrics = font_calculate_metrics(font);
    if (!metrics) return NULL;
    
    // Cache the metrics
    if (font->cached_metrics) {
        font_metrics_destroy(font->cached_metrics);
    }
    font->cached_metrics = metrics;
    font->metrics_valid = true;
    
    return metrics;
}

FontMetrics* font_get_metrics_for_size(ViewFont* font, double size) {
    if (!font || size <= 0) return NULL;
    
    // If size matches font size, use cached metrics
    if (fabs(size - font->size) < 0.01) {
        return font_get_metrics(font);
    }
    
    // Create metrics for different size
    FontMetrics* metrics = font_metrics_create_for_size(font, size);
    if (!metrics) return NULL;
    
    // Calculate metrics for the specific size
    FontMetrics* base_metrics = font_calculate_metrics(font);
    if (base_metrics) {
        memcpy(metrics, base_metrics, sizeof(FontMetrics));
        font_metrics_scale_for_size(metrics, size);
        font_metrics_destroy(base_metrics);
    }
    
    return metrics;
}

// Basic measurements
double font_get_ascent(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->scaled_ascent : 0.0;
}

double font_get_descent(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->scaled_descent : 0.0;
}

double font_get_line_height(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->scaled_line_height : 0.0;
}

double font_get_x_height(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->scaled_x_height : 0.0;
}

double font_get_cap_height(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->scaled_cap_height : 0.0;
}

double font_get_em_size(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->em_size : 0.0;
}

// Character measurements
double font_measure_char_width(ViewFont* font, uint32_t codepoint) {
    if (!font) return 0.0;
    
    FontMetrics* metrics = font_get_metrics(font);
    if (!metrics) return 0.0;
    
    // Simple approximation - in a real implementation, this would look up glyph metrics
    if (codepoint == 0x20) { // Space
        return metrics->space_width;
    } else if (codepoint >= 0x21 && codepoint <= 0x7E) { // Printable ASCII
        return metrics->average_char_width;
    } else {
        return metrics->average_char_width; // Default for non-ASCII
    }
}

double font_measure_space_width(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->space_width : 0.0;
}

double font_measure_em_width(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->em_width : 0.0;
}

double font_measure_en_width(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->en_width : 0.0;
}

// Text measurement
TextMeasurement* font_measure_text(ViewFont* font, const char* text, int length) {
    return font_measure_text_with_options(font, text, length, true, true);
}

TextMeasurement* font_measure_text_with_options(ViewFont* font, const char* text, int length,
                                               bool apply_kerning, bool apply_ligatures) {
    if (!font || !text || length <= 0) return NULL;
    
    TextMeasurement* measurement = calloc(1, sizeof(TextMeasurement));
    if (!measurement) return NULL;
    
    measurement->font = font;
    measurement->font_size = font->size;
    measurement->text_length = length;
    measurement->text = strndup(text, length);
    measurement->includes_kerning = apply_kerning;
    measurement->includes_ligatures = apply_ligatures;
    measurement->is_shaped = false; // Simple measurement without complex shaping
    
    font_retain(font);
    
    FontMetrics* metrics = font_get_metrics(font);
    if (!metrics) {
        text_measurement_destroy(measurement);
        return NULL;
    }
    
    // Simple character-by-character measurement
    // In a real implementation, this would use proper text shaping
    double total_width = 0.0;
    int char_count = 0;
    
    const char* ptr = text;
    const char* end = text + length;
    
    while (ptr < end) {
        // Simple UTF-8 character extraction (single byte for ASCII)
        uint32_t codepoint = (uint8_t)*ptr;
        
        if (codepoint < 0x80) {
            // ASCII character
            double char_width = font_measure_char_width(font, codepoint);
            total_width += char_width;
            char_count++;
            ptr++;
        } else {
            // Multi-byte UTF-8 character (simplified handling)
            double char_width = metrics->average_char_width;
            total_width += char_width;
            char_count++;
            ptr++; // Simplified - should properly decode UTF-8
        }
    }
    
    measurement->total_width = total_width;
    measurement->total_height = metrics->scaled_line_height;
    measurement->ascent = metrics->scaled_ascent;
    measurement->descent = metrics->scaled_descent;
    measurement->leading = metrics->leading;
    measurement->glyph_count = char_count;
    
    // Allocate glyph arrays (simplified)
    if (char_count > 0) {
        measurement->glyph_metrics = calloc(char_count, sizeof(GlyphMetrics));
        measurement->glyph_positions = calloc(char_count, sizeof(ViewPoint));
        
        // Fill in basic glyph information
        double x_pos = 0.0;
        ptr = text;
        for (int i = 0; i < char_count && ptr < end; i++) {
            uint32_t codepoint = (uint8_t)*ptr;
            double char_width = font_measure_char_width(font, codepoint);
            
            measurement->glyph_metrics[i].codepoint = codepoint;
            measurement->glyph_metrics[i].advance_width = char_width;
            measurement->glyph_metrics[i].is_whitespace = (codepoint == 0x20);
            
            measurement->glyph_positions[i].x = x_pos;
            measurement->glyph_positions[i].y = 0.0;
            
            x_pos += char_width;
            ptr++;
        }
    }
    
    return measurement;
}

void text_measurement_destroy(TextMeasurement* measurement) {
    if (!measurement) return;
    
    font_release(measurement->font);
    free(measurement->text);
    free(measurement->glyph_metrics);
    free(measurement->glyph_positions);
    free(measurement->line_breaks);
    free(measurement->line_widths);
    free(measurement);
}

// Simple text width measurement
double font_measure_text_width(ViewFont* font, const char* text, int length) {
    if (!font || !text || length <= 0) return 0.0;
    
    FontMetrics* metrics = font_get_metrics(font);
    if (!metrics) return 0.0;
    
    double total_width = 0.0;
    const char* ptr = text;
    const char* end = text + length;
    
    while (ptr < end) {
        uint32_t codepoint = (uint8_t)*ptr; // Simplified UTF-8 handling
        total_width += font_measure_char_width(font, codepoint);
        ptr++;
    }
    
    return total_width;
}

double font_measure_text_width_fast(ViewFont* font, const char* text, int length) {
    // Fast approximation using average character width
    if (!font || !text || length <= 0) return 0.0;
    
    FontMetrics* metrics = font_get_metrics(font);
    if (!metrics) return 0.0;
    
    return length * metrics->average_char_width;
}

// Glyph metrics
GlyphMetrics* font_get_glyph_metrics(ViewFont* font, uint32_t glyph_id) {
    if (!font) return NULL;
    
    GlyphMetrics* glyph = calloc(1, sizeof(GlyphMetrics));
    if (!glyph) return NULL;
    
    glyph->glyph_id = glyph_id;
    
    FontMetrics* metrics = font_get_metrics(font);
    if (metrics) {
        glyph->advance_width = metrics->average_char_width;
        // Set other default values
        glyph->left_side_bearing = 0.0;
        glyph->right_side_bearing = 0.0;
        glyph->advance_height = metrics->scaled_line_height;
    }
    
    return glyph;
}

GlyphMetrics* font_get_codepoint_metrics(ViewFont* font, uint32_t codepoint) {
    if (!font) return NULL;
    
    // In a real implementation, this would map codepoint to glyph ID
    uint32_t glyph_id = codepoint; // Simplified mapping
    
    GlyphMetrics* glyph = font_get_glyph_metrics(font, glyph_id);
    if (glyph) {
        glyph->codepoint = codepoint;
        glyph->advance_width = font_measure_char_width(font, codepoint);
        glyph->is_whitespace = (codepoint == 0x20 || codepoint == 0x09 || 
                               codepoint == 0x0A || codepoint == 0x0D);
        glyph->is_line_break = (codepoint == 0x0A || codepoint == 0x0D);
    }
    
    return glyph;
}

void glyph_metrics_destroy(GlyphMetrics* glyph) {
    free(glyph);
}

// Glyph lookup
uint32_t font_get_glyph_id(ViewFont* font, uint32_t codepoint) {
    // Simplified implementation - in reality, this would use the font's cmap table
    return codepoint;
}

bool font_has_glyph(ViewFont* font, uint32_t codepoint) {
    // Simplified implementation - assume basic Latin support
    return (codepoint >= 0x20 && codepoint <= 0x7E);
}

uint32_t font_get_fallback_glyph_id(ViewFont* font) {
    // Return glyph ID for missing character (usually .notdef)
    return 0;
}

// Kerning
double font_get_kerning(ViewFont* font, uint32_t left_glyph, uint32_t right_glyph) {
    // Simplified implementation - no kerning
    return 0.0;
}

bool font_has_kerning_pair(ViewFont* font, uint32_t left_glyph, uint32_t right_glyph) {
    return false; // Simplified - no kerning support
}

// Baseline calculations
double font_get_alphabetic_baseline(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->scaled_ascent : 0.0;
}

double font_get_ideographic_baseline(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? -metrics->scaled_descent : 0.0;
}

double font_get_hanging_baseline(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->scaled_ascent * 0.8 : 0.0;
}

double font_get_mathematical_baseline(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->math_axis_height : 0.0;
}

// Mathematical typography metrics
double font_get_math_axis_height(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->math_axis_height : 0.0;
}

double font_get_superscript_offset(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->superscript_offset : 0.0;
}

double font_get_subscript_offset(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->subscript_offset : 0.0;
}

double font_get_superscript_scale(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->superscript_scale : 0.7;
}

double font_get_subscript_scale(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->subscript_scale : 0.7;
}

// Line metrics calculations
LineMetrics* calculate_line_metrics(ViewFont** fonts, double* font_sizes, int font_count) {
    if (!fonts || font_count <= 0) return NULL;
    
    LineMetrics* line_metrics = calloc(1, sizeof(LineMetrics));
    if (!line_metrics) return NULL;
    
    line_metrics->fonts_in_line = calloc(font_count, sizeof(ViewFont*));
    if (!line_metrics->fonts_in_line) {
        free(line_metrics);
        return NULL;
    }
    
    double max_ascent = 0.0;
    double max_descent = 0.0;
    double max_line_height = 0.0;
    
    for (int i = 0; i < font_count; i++) {
        ViewFont* font = fonts[i];
        double size = font_sizes ? font_sizes[i] : font->size;
        
        FontMetrics* metrics = font_get_metrics_for_size(font, size);
        if (metrics) {
            if (metrics->scaled_ascent > max_ascent) {
                max_ascent = metrics->scaled_ascent;
            }
            if (metrics->scaled_descent > max_descent) {
                max_descent = metrics->scaled_descent;
            }
            if (metrics->scaled_line_height > max_line_height) {
                max_line_height = metrics->scaled_line_height;
            }
            
            font_metrics_destroy(metrics);
        }
        
        line_metrics->fonts_in_line[i] = font;
        font_retain(font);
    }
    
    line_metrics->ascent = max_ascent;
    line_metrics->descent = max_descent;
    line_metrics->line_height = max_line_height;
    line_metrics->baseline_offset = max_ascent;
    line_metrics->leading = max_line_height - (max_ascent + max_descent);
    line_metrics->font_count = font_count;
    
    return line_metrics;
}

LineMetrics* calculate_line_metrics_from_text_runs(ViewNode** text_runs, int run_count) {
    if (!text_runs || run_count <= 0) return NULL;
    
    // Extract fonts from text runs
    ViewFont** fonts = calloc(run_count, sizeof(ViewFont*));
    double* sizes = calloc(run_count, sizeof(double));
    
    if (!fonts || !sizes) {
        free(fonts);
        free(sizes);
        return NULL;
    }
    
    int font_count = 0;
    for (int i = 0; i < run_count; i++) {
        ViewNode* run = text_runs[i];
        if (run && run->type == VIEW_NODE_TEXT_RUN && run->content.text_run) {
            fonts[font_count] = run->content.text_run->font;
            sizes[font_count] = run->content.text_run->font_size;
            font_count++;
        }
    }
    
    LineMetrics* metrics = calculate_line_metrics(fonts, sizes, font_count);
    
    free(fonts);
    free(sizes);
    
    return metrics;
}

void line_metrics_destroy(LineMetrics* metrics) {
    if (!metrics) return;
    
    for (int i = 0; i < metrics->font_count; i++) {
        font_release(metrics->fonts_in_line[i]);
    }
    
    free(metrics->fonts_in_line);
    free(metrics);
}

// Font feature detection (simplified implementations)
bool font_supports_feature(ViewFont* font, const char* feature_tag) {
    return false; // Simplified - no OpenType feature support
}

bool font_supports_script(ViewFont* font, const char* script_tag) {
    return strcmp(script_tag, "latn") == 0; // Only Latin script
}

bool font_supports_language(ViewFont* font, const char* language_tag) {
    return strncmp(language_tag, "en", 2) == 0; // Only English
}

// Unicode support (simplified)
bool font_supports_codepoint(ViewFont* font, uint32_t codepoint) {
    return font_has_glyph(font, codepoint);
}

bool font_supports_unicode_range(ViewFont* font, uint32_t start, uint32_t end) {
    // Simplified - check if it's in basic Latin range
    return (start >= 0x20 && end <= 0x7E);
}

// Font classification (simplified)
bool font_is_monospace(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->is_monospace : false;
}

bool font_is_serif(ViewFont* font) {
    // Simplified check based on family name
    const char* family = font_get_family_name(font);
    return family && (strstr(family, "Times") || strstr(family, "serif"));
}

bool font_is_sans_serif(ViewFont* font) {
    const char* family = font_get_family_name(font);
    return family && (strstr(family, "Arial") || strstr(family, "Helvetica") || 
                     strstr(family, "sans"));
}

bool font_supports_mathematics(ViewFont* font) {
    FontMetrics* metrics = font_get_metrics(font);
    return metrics ? metrics->supports_math : false;
}

// Utility functions
double points_to_pixels(double points, double dpi) {
    return points * dpi / 72.0;
}

double pixels_to_points(double pixels, double dpi) {
    return pixels * 72.0 / dpi;
}

double font_units_to_points(int font_units, int units_per_em, double font_size) {
    return (double)font_units * font_size / units_per_em;
}

int points_to_font_units(double points, int units_per_em, double font_size) {
    return (int)(points * units_per_em / font_size);
}

// Debugging functions
void font_metrics_print(FontMetrics* metrics) {
    if (!metrics) {
        printf("FontMetrics: NULL\n");
        return;
    }
    
    printf("FontMetrics:\n");
    printf("  Font size: %.2f\n", metrics->font_size);
    printf("  Ascent: %.2f\n", metrics->scaled_ascent);
    printf("  Descent: %.2f\n", metrics->scaled_descent);
    printf("  Line height: %.2f\n", metrics->scaled_line_height);
    printf("  X-height: %.2f\n", metrics->scaled_x_height);
    printf("  Cap height: %.2f\n", metrics->scaled_cap_height);
    printf("  Em size: %.2f\n", metrics->em_size);
    printf("  Space width: %.2f\n", metrics->space_width);
    printf("  Average char width: %.2f\n", metrics->average_char_width);
    printf("  Is monospace: %s\n", metrics->is_monospace ? "yes" : "no");
    printf("  Has kerning: %s\n", metrics->has_kerning ? "yes" : "no");
    printf("  Supports math: %s\n", metrics->supports_math ? "yes" : "no");
}

void text_measurement_print(TextMeasurement* measurement) {
    if (!measurement) {
        printf("TextMeasurement: NULL\n");
        return;
    }
    
    printf("TextMeasurement:\n");
    printf("  Text: \"%.50s%s\"\n", measurement->text, 
           measurement->text_length > 50 ? "..." : "");
    printf("  Total width: %.2f\n", measurement->total_width);
    printf("  Total height: %.2f\n", measurement->total_height);
    printf("  Ascent: %.2f\n", measurement->ascent);
    printf("  Descent: %.2f\n", measurement->descent);
    printf("  Glyph count: %d\n", measurement->glyph_count);
    printf("  Includes kerning: %s\n", measurement->includes_kerning ? "yes" : "no");
    printf("  Is shaped: %s\n", measurement->is_shaped ? "yes" : "no");
}
