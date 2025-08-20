#include "text_shaper.h"
#include "../../lib/strbuf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

// Text shaper creation and destruction
TextShaper* text_shaper_create(Context* ctx, FontManager* font_manager) {
    TextShaper* shaper = calloc(1, sizeof(TextShaper));
    if (!shaper) return NULL;
    
    shaper->lambda_context = ctx;
    shaper->font_manager = font_manager;
    shaper->enable_caching = true;
    shaper->max_cache_size = DEFAULT_CACHE_SIZE;
    
    // Initialize default features
    shaper->default_features.enable_kerning = true;
    shaper->default_features.enable_ligatures = true;
    shaper->default_features.enable_contextual = false;
    shaper->default_features.enable_positional = false;
    shaper->default_features.enable_marks = true;
    shaper->default_features.enable_cursive = false;
    
    shaper->default_direction = TEXT_DIRECTION_LTR;
    shaper->default_language = strdup("en");
    
    // Create cache if enabled
    if (shaper->enable_caching) {
        shaper->cache = shape_cache_create(shaper->max_cache_size);
    }
    
    return shaper;
}

void text_shaper_destroy(TextShaper* shaper) {
    if (!shaper) return;
    
    free(shaper->default_language);
    free(shaper->default_features.feature_tags);
    free(shaper->default_features.feature_values);
    
    if (shaper->cache) {
        shape_cache_destroy(shaper->cache);
    }
    
    free(shaper);
}

// Shaping context management
ShapingContext* shaping_context_create(TextShaper* shaper, ViewFont* font) {
    return shaping_context_create_with_options(shaper, font, TEXT_DIRECTION_LTR, 
                                              SCRIPT_LATIN, "en");
}

ShapingContext* shaping_context_create_with_options(TextShaper* shaper, ViewFont* font,
                                                   TextDirection direction, ScriptType script,
                                                   const char* language) {
    if (!shaper || !font) return NULL;
    
    ShapingContext* context = calloc(1, sizeof(ShapingContext));
    if (!context) return NULL;
    
    context->font = font;
    context->font_size = font->size;
    context->direction = direction;
    context->script = script;
    context->language = strdup(language ? language : "en");
    context->lambda_context = shaper->lambda_context;
    context->ref_count = 1;
    
    // Copy default features
    memcpy(&context->features, &shaper->default_features, sizeof(ShapingFeatures));
    
    font_retain(font);
    
    return context;
}

void shaping_context_retain(ShapingContext* context) {
    if (context) {
        context->ref_count++;
    }
}

void shaping_context_release(ShapingContext* context) {
    if (!context) return;
    
    context->ref_count--;
    if (context->ref_count <= 0) {
        font_release(context->font);
        free(context->language);
        free(context->input_text);
        free(context->features.feature_tags);
        free(context->features.feature_values);
        free(context);
    }
}

// Context configuration
void shaping_context_set_direction(ShapingContext* context, TextDirection direction) {
    if (context) {
        context->direction = direction;
    }
}

void shaping_context_set_script(ShapingContext* context, ScriptType script) {
    if (context) {
        context->script = script;
    }
}

void shaping_context_set_language(ShapingContext* context, const char* language) {
    if (context && language) {
        free(context->language);
        context->language = strdup(language);
    }
}

// Text analysis functions
TextDirection detect_text_direction(const char* text, int length) {
    if (!text || length <= 0) return TEXT_DIRECTION_LTR;
    
    // Simple direction detection based on character ranges
    int rtl_chars = 0;
    int ltr_chars = 0;
    
    const char* ptr = text;
    const char* end = text + length;
    
    while (ptr < end) {
        uint32_t codepoint = (uint8_t)*ptr; // Simplified UTF-8 handling
        
        // Arabic range
        if (codepoint >= 0x0600 && codepoint <= 0x06FF) {
            rtl_chars++;
        }
        // Hebrew range
        else if (codepoint >= 0x0590 && codepoint <= 0x05FF) {
            rtl_chars++;
        }
        // Latin and other LTR scripts
        else if (codepoint >= 0x0041 && codepoint <= 0x007A) {
            ltr_chars++;
        }
        
        ptr++;
    }
    
    return (rtl_chars > ltr_chars) ? TEXT_DIRECTION_RTL : TEXT_DIRECTION_LTR;
}

ScriptType detect_script(const char* text, int length) {
    if (!text || length <= 0) return SCRIPT_LATIN;
    
    // Count characters from different scripts
    int script_counts[11] = {0}; // One for each ScriptType
    
    const char* ptr = text;
    const char* end = text + length;
    
    while (ptr < end) {
        uint32_t codepoint = (uint8_t)*ptr; // Simplified UTF-8 handling
        
        if (codepoint >= 0x0041 && codepoint <= 0x007A) {
            script_counts[SCRIPT_LATIN]++;
        } else if (codepoint >= 0x0600 && codepoint <= 0x06FF) {
            script_counts[SCRIPT_ARABIC]++;
        } else if (codepoint >= 0x0590 && codepoint <= 0x05FF) {
            script_counts[SCRIPT_HEBREW]++;
        } else if (codepoint >= 0x4E00 && codepoint <= 0x9FFF) {
            script_counts[SCRIPT_CHINESE]++;
        } else if (codepoint >= 0x0400 && codepoint <= 0x04FF) {
            script_counts[SCRIPT_CYRILLIC]++;
        } else if (codepoint >= 0x0370 && codepoint <= 0x03FF) {
            script_counts[SCRIPT_GREEK]++;
        } else {
            script_counts[SCRIPT_UNKNOWN]++;
        }
        
        ptr++;
    }
    
    // Find script with highest count
    ScriptType dominant_script = SCRIPT_LATIN;
    int max_count = script_counts[SCRIPT_LATIN];
    
    for (int i = 1; i < 11; i++) {
        if (script_counts[i] > max_count) {
            max_count = script_counts[i];
            dominant_script = (ScriptType)i;
        }
    }
    
    return dominant_script;
}

char* detect_language(const char* text, int length) {
    // Very simplified language detection
    // In a real implementation, this would use statistical analysis
    
    ScriptType script = detect_script(text, length);
    
    switch (script) {
        case SCRIPT_LATIN:
            return strdup("en"); // Default to English for Latin script
        case SCRIPT_ARABIC:
            return strdup("ar");
        case SCRIPT_HEBREW:
            return strdup("he");
        case SCRIPT_CHINESE:
            return strdup("zh");
        case SCRIPT_CYRILLIC:
            return strdup("ru"); // Default to Russian for Cyrillic
        case SCRIPT_GREEK:
            return strdup("el");
        default:
            return strdup("en");
    }
}

bool is_complex_script(ScriptType script) {
    switch (script) {
        case SCRIPT_ARABIC:
        case SCRIPT_HEBREW:
        case SCRIPT_THAI:
        case SCRIPT_DEVANAGARI:
            return true;
        default:
            return false;
    }
}

bool requires_bidi_processing(const char* text, int length) {
    TextDirection direction = detect_text_direction(text, length);
    return (direction == TEXT_DIRECTION_RTL);
}

// Unicode processing utilities
uint32_t* utf8_to_unicode(const char* utf8_text, int byte_length, int* codepoint_count) {
    if (!utf8_text || byte_length <= 0) {
        *codepoint_count = 0;
        return NULL;
    }
    
    // Simplified UTF-8 to Unicode conversion (handles ASCII only for now)
    uint32_t* unicode_text = calloc(byte_length, sizeof(uint32_t));
    if (!unicode_text) {
        *codepoint_count = 0;
        return NULL;
    }
    
    int count = 0;
    for (int i = 0; i < byte_length; i++) {
        if ((utf8_text[i] & 0x80) == 0) {
            // ASCII character
            unicode_text[count++] = (uint32_t)(unsigned char)utf8_text[i];
        } else {
            // Multi-byte character (simplified handling)
            unicode_text[count++] = 0xFFFD; // Replacement character
        }
    }
    
    *codepoint_count = count;
    return unicode_text;
}

char* unicode_to_utf8(uint32_t* unicode_text, int codepoint_count, int* byte_length) {
    if (!unicode_text || codepoint_count <= 0) {
        *byte_length = 0;
        return NULL;
    }
    
    // Simplified Unicode to UTF-8 conversion (ASCII only)
    char* utf8_text = calloc(codepoint_count + 1, sizeof(char));
    if (!utf8_text) {
        *byte_length = 0;
        return NULL;
    }
    
    int count = 0;
    for (int i = 0; i < codepoint_count; i++) {
        if (unicode_text[i] <= 0x7F) {
            // ASCII character
            utf8_text[count++] = (char)unicode_text[i];
        } else {
            // Non-ASCII (simplified)
            utf8_text[count++] = '?';
        }
    }
    
    *byte_length = count;
    return utf8_text;
}

bool is_combining_mark(uint32_t codepoint) {
    // Simplified combining mark detection
    return (codepoint >= 0x0300 && codepoint <= 0x036F) || // Combining Diacritical Marks
           (codepoint >= 0x1AB0 && codepoint <= 0x1AFF) || // Combining Diacritical Marks Extended
           (codepoint >= 0x1DC0 && codepoint <= 0x1DFF);   // Combining Diacritical Marks Supplement
}

bool is_variation_selector(uint32_t codepoint) {
    return (codepoint >= 0xFE00 && codepoint <= 0xFE0F) || // Variation Selectors
           (codepoint >= 0xE0100 && codepoint <= 0xE01EF);  // Variation Selectors Supplement
}

bool is_emoji(uint32_t codepoint) {
    // Simplified emoji detection
    return (codepoint >= 0x1F600 && codepoint <= 0x1F64F) || // Emoticons
           (codepoint >= 0x1F300 && codepoint <= 0x1F5FF) || // Miscellaneous Symbols
           (codepoint >= 0x1F680 && codepoint <= 0x1F6FF) || // Transport and Map
           (codepoint >= 0x2600 && codepoint <= 0x26FF);     // Miscellaneous Symbols
}

// Main shaping functions
TextShapeResult* text_shape(ViewFont* font, const char* text, int length) {
    if (!font || !text || length <= 0) return NULL;
    
    TextShapeResult* result = calloc(1, sizeof(TextShapeResult));
    if (!result) return NULL;
    
    result->original_text = strndup(text, length);
    result->text_length = length;
    result->font = font;
    result->font_size = font->size;
    result->ref_count = 1;
    
    font_retain(font);
    
    // Auto-detect text properties
    result->direction = detect_text_direction(text, length);
    result->script = detect_script(text, length);
    result->language = detect_language(text, length);
    result->is_complex_script = is_complex_script(result->script);
    
    // Simple character-by-character shaping (no complex script support yet)
    int char_count = 0;
    const char* ptr = text;
    const char* end = text + length;
    
    // Count characters
    while (ptr < end) {
        char_count++;
        ptr++;
    }
    
    if (char_count == 0) {
        text_shape_result_release(result);
        return NULL;
    }
    
    // Allocate glyph arrays
    result->glyphs = calloc(char_count, sizeof(ViewGlyphInfo));
    result->positions = calloc(char_count, sizeof(ViewPoint));
    result->cluster_map = calloc(char_count, sizeof(int));
    result->reverse_cluster_map = calloc(char_count, sizeof(int));
    result->can_break_after = calloc(char_count, sizeof(bool));
    result->break_penalties = calloc(char_count, sizeof(double));
    
    if (!result->glyphs || !result->positions) {
        text_shape_result_release(result);
        return NULL;
    }
    
    result->glyph_count = char_count;
    
    // Get font metrics
    FontMetrics* metrics = font_get_metrics(font);
    if (!metrics) {
        text_shape_result_release(result);
        return NULL;
    }
    
    // Shape each character
    double x_advance = 0.0;
    ptr = text;
    
    for (int i = 0; i < char_count && ptr < end; i++) {
        uint32_t codepoint = (uint8_t)*ptr; // Simplified UTF-8 handling
        
        // Fill glyph info
        result->glyphs[i].glyph_id = font_get_glyph_id(font, codepoint);
        result->glyphs[i].codepoint = codepoint;
        result->glyphs[i].advance_width = font_measure_char_width(font, codepoint);
        result->glyphs[i].advance_height = 0.0;
        result->glyphs[i].offset.x = 0.0;
        result->glyphs[i].offset.y = 0.0;
        
        // Position glyph
        result->positions[i].x = x_advance;
        result->positions[i].y = 0.0;
        
        // Cluster mapping (1:1 for simple scripts)
        result->cluster_map[i] = i;
        result->reverse_cluster_map[i] = i;
        
        // Line break opportunities
        result->can_break_after[i] = (codepoint == 0x20 || codepoint == 0x09); // Space or tab
        result->break_penalties[i] = result->can_break_after[i] ? 0.0 : 100.0;
        
        x_advance += result->glyphs[i].advance_width;
        ptr++;
    }
    
    // Calculate overall measurements
    result->total_width = x_advance;
    result->total_height = metrics->scaled_line_height;
    result->ascent = metrics->scaled_ascent;
    result->descent = metrics->scaled_descent;
    
    // Count break opportunities
    result->break_opportunity_count = 0;
    for (int i = 0; i < char_count; i++) {
        if (result->can_break_after[i]) {
            result->break_opportunity_count++;
        }
    }
    
    return result;
}

TextShapeResult* text_shape_with_context(ShapingContext* context, const char* text, int length) {
    if (!context || !context->font) return NULL;
    
    return text_shape(context->font, text, length);
}

TextShapeResult* text_shape_with_features(ViewFont* font, const char* text, int length,
                                         ShapingFeatures* features) {
    // For now, ignore features and use basic shaping
    return text_shape(font, text, length);
}

// Shape result management
void text_shape_result_retain(TextShapeResult* result) {
    if (result) {
        result->ref_count++;
    }
}

void text_shape_result_release(TextShapeResult* result) {
    if (!result) return;
    
    result->ref_count--;
    if (result->ref_count <= 0) {
        font_release(result->font);
        free(result->original_text);
        free(result->language);
        free(result->glyphs);
        free(result->positions);
        free(result->cluster_map);
        free(result->reverse_cluster_map);
        free(result->can_break_after);
        free(result->break_penalties);
        free(result);
    }
}

// Shape result access
int text_shape_result_get_glyph_count(TextShapeResult* result) {
    return result ? result->glyph_count : 0;
}

ViewGlyphInfo* text_shape_result_get_glyph(TextShapeResult* result, int index) {
    if (!result || index < 0 || index >= result->glyph_count) return NULL;
    return &result->glyphs[index];
}

ViewPoint text_shape_result_get_glyph_position(TextShapeResult* result, int index) {
    ViewPoint zero = {0.0, 0.0};
    if (!result || index < 0 || index >= result->glyph_count) return zero;
    return result->positions[index];
}

double text_shape_result_get_total_width(TextShapeResult* result) {
    return result ? result->total_width : 0.0;
}

double text_shape_result_get_total_height(TextShapeResult* result) {
    return result ? result->total_height : 0.0;
}

// Shaping features management
ShapingFeatures* shaping_features_create(void) {
    ShapingFeatures* features = calloc(1, sizeof(ShapingFeatures));
    if (!features) return NULL;
    
    // Set default values
    features->enable_kerning = true;
    features->enable_ligatures = true;
    features->enable_contextual = false;
    features->enable_positional = false;
    features->enable_marks = true;
    features->enable_cursive = false;
    
    return features;
}

void shaping_features_destroy(ShapingFeatures* features) {
    if (!features) return;
    
    for (int i = 0; i < features->feature_count; i++) {
        free(features->feature_tags[i]);
    }
    free(features->feature_tags);
    free(features->feature_values);
    free(features);
}

void shaping_features_enable_kerning(ShapingFeatures* features, bool enable) {
    if (features) {
        features->enable_kerning = enable;
    }
}

void shaping_features_enable_ligatures(ShapingFeatures* features, bool enable) {
    if (features) {
        features->enable_ligatures = enable;
    }
}

void shaping_features_add_feature(ShapingFeatures* features, const char* tag, bool enabled) {
    if (!features || !tag || features->feature_count >= MAX_FEATURE_COUNT) return;
    
    // Reallocate arrays
    features->feature_tags = realloc(features->feature_tags, 
                                   (features->feature_count + 1) * sizeof(char*));
    features->feature_values = realloc(features->feature_values,
                                     (features->feature_count + 1) * sizeof(bool));
    
    if (!features->feature_tags || !features->feature_values) return;
    
    features->feature_tags[features->feature_count] = strdup(tag);
    features->feature_values[features->feature_count] = enabled;
    features->feature_count++;
}

// Simple cache implementation
struct ShapeCache {
    struct ShapeCacheEntry {
        uint32_t key;               // Hash of font + text
        TextShapeResult* result;    // Cached result
        struct ShapeCacheEntry* next; // Next in hash chain
        uint64_t access_time;       // Last access time
    }** buckets;
    
    int bucket_count;
    int entry_count;
    int max_entries;
    uint64_t hits;
    uint64_t misses;
};

ShapeCache* shape_cache_create(int max_entries) {
    ShapeCache* cache = calloc(1, sizeof(ShapeCache));
    if (!cache) return NULL;
    
    cache->bucket_count = max_entries * 2;
    cache->buckets = calloc(cache->bucket_count, sizeof(struct ShapeCacheEntry*));
    if (!cache->buckets) {
        free(cache);
        return NULL;
    }
    
    cache->max_entries = max_entries;
    return cache;
}

void shape_cache_destroy(ShapeCache* cache) {
    if (!cache) return;
    
    for (int i = 0; i < cache->bucket_count; i++) {
        struct ShapeCacheEntry* entry = cache->buckets[i];
        while (entry) {
            struct ShapeCacheEntry* next = entry->next;
            text_shape_result_release(entry->result);
            free(entry);
            entry = next;
        }
    }
    
    free(cache->buckets);
    free(cache);
}

static uint32_t calculate_shape_cache_key(ViewFont* font, const char* text, int length) {
    uint32_t key = font->cache_key;
    
    // Simple hash of text content
    for (int i = 0; i < length; i++) {
        key = ((key << 5) + key) + (unsigned char)text[i];
    }
    
    return key;
}

TextShapeResult* shape_cache_get(ShapeCache* cache, ViewFont* font, const char* text, int length) {
    if (!cache || !font || !text) return NULL;
    
    uint32_t key = calculate_shape_cache_key(font, text, length);
    int bucket = key % cache->bucket_count;
    
    struct ShapeCacheEntry* entry = cache->buckets[bucket];
    while (entry) {
        if (entry->key == key) {
            entry->access_time = time(NULL);
            cache->hits++;
            text_shape_result_retain(entry->result);
            return entry->result;
        }
        entry = entry->next;
    }
    
    cache->misses++;
    return NULL;
}

void shape_cache_put(ShapeCache* cache, ViewFont* font, const char* text, int length,
                     TextShapeResult* result) {
    if (!cache || !font || !text || !result) return;
    
    if (cache->entry_count >= cache->max_entries) {
        // TODO: Implement LRU eviction
        return;
    }
    
    uint32_t key = calculate_shape_cache_key(font, text, length);
    int bucket = key % cache->bucket_count;
    
    struct ShapeCacheEntry* entry = calloc(1, sizeof(struct ShapeCacheEntry));
    if (!entry) return;
    
    entry->key = key;
    entry->result = result;
    entry->access_time = time(NULL);
    entry->next = cache->buckets[bucket];
    
    cache->buckets[bucket] = entry;
    cache->entry_count++;
    
    text_shape_result_retain(result);
}

void shape_cache_clear(ShapeCache* cache) {
    if (!cache) return;
    
    for (int i = 0; i < cache->bucket_count; i++) {
        struct ShapeCacheEntry* entry = cache->buckets[i];
        while (entry) {
            struct ShapeCacheEntry* next = entry->next;
            text_shape_result_release(entry->result);
            free(entry);
            entry = next;
        }
        cache->buckets[i] = NULL;
    }
    
    cache->entry_count = 0;
}

// Statistics and debugging
TextShaperStats text_shaper_get_stats(TextShaper* shaper) {
    TextShaperStats stats = {0};
    
    if (!shaper) return stats;
    
    stats.total_shapes = shaper->stats.shapes_performed;
    stats.avg_shape_time_ms = shaper->stats.avg_shape_time;
    stats.memory_usage = shaper->stats.memory_usage;
    
    if (shaper->cache) {
        stats.cache_hits = shaper->cache->hits;
        stats.cache_misses = shaper->cache->misses;
        uint64_t total = stats.cache_hits + stats.cache_misses;
        stats.cache_hit_ratio = total > 0 ? (double)stats.cache_hits / total : 0.0;
    }
    
    return stats;
}

void text_shaper_print_stats(TextShaper* shaper) {
    TextShaperStats stats = text_shaper_get_stats(shaper);
    
    printf("Text Shaper Statistics:\n");
    printf("  Total shapes: %llu\n", (unsigned long long)stats.total_shapes);
    printf("  Cache hits: %llu\n", (unsigned long long)stats.cache_hits);
    printf("  Cache misses: %llu\n", (unsigned long long)stats.cache_misses);
    printf("  Cache hit ratio: %.2f%%\n", stats.cache_hit_ratio * 100.0);
    printf("  Average shape time: %.2f ms\n", stats.avg_shape_time_ms);
    printf("  Memory usage: %zu bytes\n", stats.memory_usage);
}

// Debugging functions
void text_shape_result_print(TextShapeResult* result) {
    if (!result) {
        printf("TextShapeResult: NULL\n");
        return;
    }
    
    printf("TextShapeResult:\n");
    printf("  Text: \"%.50s%s\"\n", result->original_text,
           result->text_length > 50 ? "..." : "");
    printf("  Glyph count: %d\n", result->glyph_count);
    printf("  Total width: %.2f\n", result->total_width);
    printf("  Total height: %.2f\n", result->total_height);
    printf("  Direction: %s\n", result->direction == TEXT_DIRECTION_RTL ? "RTL" : "LTR");
    printf("  Script: %d\n", result->script);
    printf("  Language: %s\n", result->language);
    printf("  Complex script: %s\n", result->is_complex_script ? "yes" : "no");
    printf("  Break opportunities: %d\n", result->break_opportunity_count);
}
