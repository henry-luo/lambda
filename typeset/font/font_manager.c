#include "font_manager.h"
#include "font_metrics.h"
#include "../../lib/strbuf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

// Hash function for font cache keys
static uint32_t hash_string(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

// Calculate cache key for font
uint32_t font_calculate_cache_key(const char* family, double size, 
                                 ViewFontWeight weight, ViewFontStyle style) {
    uint32_t family_hash = hash_string(family ? family : "");
    uint32_t size_hash = (uint32_t)(size * 100); // Convert to integer with 2 decimal precision
    uint32_t weight_hash = (uint32_t)weight;
    uint32_t style_hash = (uint32_t)style;
    
    return family_hash ^ (size_hash << 8) ^ (weight_hash << 16) ^ (style_hash << 24);
}

// Font cache implementation
static FontCache* font_cache_create(int max_entries) {
    FontCache* cache = calloc(1, sizeof(FontCache));
    if (!cache) return NULL;
    
    cache->bucket_count = max_entries * 2; // Use double the max entries for bucket count
    cache->buckets = calloc(cache->bucket_count, sizeof(FontCacheEntry*));
    if (!cache->buckets) {
        free(cache);
        return NULL;
    }
    
    cache->max_entries = max_entries;
    cache->entry_count = 0;
    cache->hits = 0;
    cache->misses = 0;
    cache->evictions = 0;
    
    return cache;
}

static void font_cache_destroy(FontCache* cache) {
    if (!cache) return;
    
    // Free all entries
    for (int i = 0; i < cache->bucket_count; i++) {
        FontCacheEntry* entry = cache->buckets[i];
        while (entry) {
            FontCacheEntry* next = entry->next;
            font_release(entry->font);
            free(entry);
            entry = next;
        }
    }
    
    free(cache->buckets);
    free(cache);
}

static ViewFont* font_cache_get(FontCache* cache, uint32_t key) {
    if (!cache) return NULL;
    
    int bucket = key % cache->bucket_count;
    FontCacheEntry* entry = cache->buckets[bucket];
    
    while (entry) {
        if (entry->key == key) {
            // Update access statistics
            entry->last_access_time = time(NULL);
            entry->access_count++;
            cache->hits++;
            
            font_retain(entry->font);
            return entry->font;
        }
        entry = entry->next;
    }
    
    cache->misses++;
    return NULL;
}

static void font_cache_put(FontCache* cache, uint32_t key, ViewFont* font) {
    if (!cache || !font) return;
    
    // Check if we need to evict entries
    if (cache->entry_count >= cache->max_entries) {
        font_cache_evict_lru(NULL, 1); // Will be implemented properly
    }
    
    int bucket = key % cache->bucket_count;
    
    // Create new entry
    FontCacheEntry* entry = calloc(1, sizeof(FontCacheEntry));
    if (!entry) return;
    
    entry->key = key;
    entry->font = font;
    entry->last_access_time = time(NULL);
    entry->access_count = 1;
    
    // Add to bucket chain
    entry->next = cache->buckets[bucket];
    cache->buckets[bucket] = entry;
    cache->entry_count++;
    
    font_retain(font);
}

// Font creation and destruction
static ViewFont* view_font_create(void) {
    ViewFont* font = calloc(1, sizeof(ViewFont));
    if (!font) return NULL;
    
    font->ref_count = 1;
    font->size = 12.0; // Default size
    font->weight = FONT_WEIGHT_NORMAL;
    font->style = FONT_STYLE_NORMAL;
    font->stretch = FONT_STRETCH_NORMAL;
    font->metrics_valid = false;
    
    return font;
}

void font_retain(ViewFont* font) {
    if (font) {
        font->ref_count++;
    }
}

void font_release(ViewFont* font) {
    if (!font) return;
    
    font->ref_count--;
    if (font->ref_count <= 0) {
        free(font->family_name);
        free(font->style_name);
        free(font->file_path);
        
        if (font->cached_metrics) {
            font_metrics_destroy(font->cached_metrics);
        }
        
        // Release font face data (would call FreeType cleanup)
        if (font->font_face) {
            // FT_Done_Face((FT_Face)font->font_face);
        }
        
        free(font->font_data);
        free(font);
    }
}

// Font manager creation and destruction
FontManager* font_manager_create(Context* ctx) {
    FontManager* mgr = calloc(1, sizeof(FontManager));
    if (!mgr) return NULL;
    
    mgr->lambda_context = ctx;
    mgr->font_cache = font_cache_create(100); // Default cache size
    
    if (!mgr->font_cache) {
        free(mgr);
        return NULL;
    }
    
    // Set default font settings
    mgr->default_font_family = strdup("Times New Roman");
    mgr->default_font_size = 12.0;
    mgr->default_weight = FONT_WEIGHT_NORMAL;
    mgr->default_style = FONT_STYLE_NORMAL;
    mgr->use_system_fonts = true;
    
    // Initialize fallback families
    mgr->fallback_families = calloc(4, sizeof(char*));
    mgr->fallback_families[0] = strdup("Arial");
    mgr->fallback_families[1] = strdup("Helvetica");
    mgr->fallback_families[2] = strdup("sans-serif");
    mgr->fallback_count = 3;
    
    // Initialize font directories (platform-specific)
#ifdef _WIN32
    mgr->font_directories = calloc(2, sizeof(char*));
    mgr->font_directories[0] = strdup("C:\\Windows\\Fonts");
    mgr->font_directory_count = 1;
#elif __APPLE__
    mgr->font_directories = calloc(3, sizeof(char*));
    mgr->font_directories[0] = strdup("/System/Library/Fonts");
    mgr->font_directories[1] = strdup("/Library/Fonts");
    mgr->font_directory_count = 2;
#else // Linux
    mgr->font_directories = calloc(4, sizeof(char*));
    mgr->font_directories[0] = strdup("/usr/share/fonts");
    mgr->font_directories[1] = strdup("/usr/local/share/fonts");
    mgr->font_directories[2] = strdup("~/.local/share/fonts");
    mgr->font_directory_count = 3;
#endif
    
    return mgr;
}

void font_manager_destroy(FontManager* mgr) {
    if (!mgr) return;
    
    font_cache_destroy(mgr->font_cache);
    
    free(mgr->default_font_family);
    
    // Free font directories
    for (int i = 0; i < mgr->font_directory_count; i++) {
        free(mgr->font_directories[i]);
    }
    free(mgr->font_directories);
    
    // Free fallback families
    for (int i = 0; i < mgr->fallback_count; i++) {
        free(mgr->fallback_families[i]);
    }
    free(mgr->fallback_families);
    
    free(mgr);
}

// Font loading and management
ViewFont* font_manager_get_font(FontManager* mgr, const char* family, double size,
                               ViewFontWeight weight, ViewFontStyle style) {
    if (!mgr) return NULL;
    
    // Use defaults if not specified
    if (!family) family = mgr->default_font_family;
    if (size <= 0) size = mgr->default_font_size;
    
    // Calculate cache key
    uint32_t cache_key = font_calculate_cache_key(family, size, weight, style);
    
    // Check cache first
    ViewFont* cached_font = font_cache_get(mgr->font_cache, cache_key);
    if (cached_font) {
        return cached_font;
    }
    
    // Create new font
    ViewFont* font = view_font_create();
    if (!font) return NULL;
    
    font->family_name = strdup(family);
    font->size = size;
    font->weight = weight;
    font->style = style;
    font->cache_key = cache_key;
    
    // TODO: Load actual font face using FreeType or system API
    // For now, create a placeholder
    
    // Cache the font
    font_cache_put(mgr->font_cache, cache_key, font);
    
    mgr->stats.fonts_loaded++;
    
    return font;
}

ViewFont* font_manager_get_default_font(FontManager* mgr) {
    if (!mgr) return NULL;
    
    return font_manager_get_font(mgr, mgr->default_font_family, mgr->default_font_size,
                                mgr->default_weight, mgr->default_style);
}

ViewFont* font_manager_find_best_match(FontManager* mgr, const char* family, double size,
                                      ViewFontWeight weight, ViewFontStyle style) {
    if (!mgr) return NULL;
    
    // Try exact match first
    ViewFont* font = font_manager_get_font(mgr, family, size, weight, style);
    if (font) return font;
    
    // Try fallback families
    for (int i = 0; i < mgr->fallback_count; i++) {
        font = font_manager_get_font(mgr, mgr->fallback_families[i], size, weight, style);
        if (font) return font;
    }
    
    // Return default font as last resort
    return font_manager_get_default_font(mgr);
}

// Font properties
const char* font_get_family_name(ViewFont* font) {
    return font ? font->family_name : NULL;
}

const char* font_get_style_name(ViewFont* font) {
    return font ? font->style_name : NULL;
}

double font_get_size(ViewFont* font) {
    return font ? font->size : 0.0;
}

ViewFontWeight font_get_weight(ViewFont* font) {
    return font ? font->weight : FONT_WEIGHT_NORMAL;
}

ViewFontStyle font_get_style(ViewFont* font) {
    return font ? font->style : FONT_STYLE_NORMAL;
}

// Font settings
void font_manager_set_default_font(FontManager* mgr, const char* family, double size) {
    if (!mgr) return;
    
    free(mgr->default_font_family);
    mgr->default_font_family = strdup(family ? family : "Times New Roman");
    
    if (size > 0) {
        mgr->default_font_size = size;
    }
}

void font_manager_set_default_weight(FontManager* mgr, ViewFontWeight weight) {
    if (mgr) {
        mgr->default_weight = weight;
    }
}

void font_manager_set_default_style(FontManager* mgr, ViewFontStyle style) {
    if (mgr) {
        mgr->default_style = style;
    }
}

void font_manager_add_font_directory(FontManager* mgr, const char* directory) {
    if (!mgr || !directory) return;
    
    // Reallocate array
    char** new_dirs = realloc(mgr->font_directories, 
                             (mgr->font_directory_count + 1) * sizeof(char*));
    if (!new_dirs) return;
    
    mgr->font_directories = new_dirs;
    mgr->font_directories[mgr->font_directory_count] = strdup(directory);
    mgr->font_directory_count++;
}

void font_manager_add_fallback_family(FontManager* mgr, const char* family) {
    if (!mgr || !family) return;
    
    // Reallocate array
    char** new_fallbacks = realloc(mgr->fallback_families,
                                  (mgr->fallback_count + 1) * sizeof(char*));
    if (!new_fallbacks) return;
    
    mgr->fallback_families = new_fallbacks;
    mgr->fallback_families[mgr->fallback_count] = strdup(family);
    mgr->fallback_count++;
}

// Cache management
void font_cache_clear(FontManager* mgr) {
    if (!mgr || !mgr->font_cache) return;
    
    FontCache* cache = mgr->font_cache;
    
    // Free all entries
    for (int i = 0; i < cache->bucket_count; i++) {
        FontCacheEntry* entry = cache->buckets[i];
        while (entry) {
            FontCacheEntry* next = entry->next;
            font_release(entry->font);
            free(entry);
            entry = next;
        }
        cache->buckets[i] = NULL;
    }
    
    cache->entry_count = 0;
}

void font_cache_set_max_size(FontManager* mgr, int max_entries) {
    if (!mgr || !mgr->font_cache) return;
    
    mgr->font_cache->max_entries = max_entries;
    
    // Evict entries if current count exceeds new max
    if (mgr->font_cache->entry_count > max_entries) {
        font_cache_evict_lru(mgr, mgr->font_cache->entry_count - max_entries);
    }
}

void font_cache_evict_lru(FontManager* mgr, int count) {
    if (!mgr || !mgr->font_cache || count <= 0) return;
    
    FontCache* cache = mgr->font_cache;
    
    // Simple implementation: find and remove oldest entries
    // In a production system, this would use a proper LRU data structure
    for (int evicted = 0; evicted < count && cache->entry_count > 0; evicted++) {
        time_t oldest_time = time(NULL);
        FontCacheEntry* oldest_entry = NULL;
        int oldest_bucket = -1;
        FontCacheEntry* oldest_prev = NULL;
        
        // Find oldest entry
        for (int i = 0; i < cache->bucket_count; i++) {
            FontCacheEntry* prev = NULL;
            FontCacheEntry* entry = cache->buckets[i];
            
            while (entry) {
                if (entry->last_access_time < oldest_time) {
                    oldest_time = entry->last_access_time;
                    oldest_entry = entry;
                    oldest_bucket = i;
                    oldest_prev = prev;
                }
                prev = entry;
                entry = entry->next;
            }
        }
        
        // Remove oldest entry
        if (oldest_entry) {
            if (oldest_prev) {
                oldest_prev->next = oldest_entry->next;
            } else {
                cache->buckets[oldest_bucket] = oldest_entry->next;
            }
            
            font_release(oldest_entry->font);
            free(oldest_entry);
            cache->entry_count--;
            cache->evictions++;
        }
    }
}

// Utility functions
bool font_families_equal(const char* family1, const char* family2) {
    if (!family1 && !family2) return true;
    if (!family1 || !family2) return false;
    
    // Case-insensitive comparison
    while (*family1 && *family2) {
        char c1 = tolower(*family1);
        char c2 = tolower(*family2);
        if (c1 != c2) return false;
        family1++;
        family2++;
    }
    
    return *family1 == *family2;
}

ViewFontWeight font_weight_from_string(const char* weight_str) {
    if (!weight_str) return FONT_WEIGHT_NORMAL;
    
    if (strcmp(weight_str, "thin") == 0) return FONT_WEIGHT_THIN;
    if (strcmp(weight_str, "light") == 0) return FONT_WEIGHT_LIGHT;
    if (strcmp(weight_str, "normal") == 0) return FONT_WEIGHT_NORMAL;
    if (strcmp(weight_str, "medium") == 0) return FONT_WEIGHT_MEDIUM;
    if (strcmp(weight_str, "bold") == 0) return FONT_WEIGHT_BOLD;
    if (strcmp(weight_str, "black") == 0) return FONT_WEIGHT_BLACK;
    
    // Try parsing as number
    int weight_num = atoi(weight_str);
    if (weight_num >= 100 && weight_num <= 900) {
        return (ViewFontWeight)weight_num;
    }
    
    return FONT_WEIGHT_NORMAL;
}

ViewFontStyle font_style_from_string(const char* style_str) {
    if (!style_str) return FONT_STYLE_NORMAL;
    
    if (strcmp(style_str, "normal") == 0) return FONT_STYLE_NORMAL;
    if (strcmp(style_str, "italic") == 0) return FONT_STYLE_ITALIC;
    if (strcmp(style_str, "oblique") == 0) return FONT_STYLE_OBLIQUE;
    
    return FONT_STYLE_NORMAL;
}

const char* font_weight_to_string(ViewFontWeight weight) {
    switch (weight) {
        case FONT_WEIGHT_THIN: return "thin";
        case FONT_WEIGHT_EXTRA_LIGHT: return "extra-light";
        case FONT_WEIGHT_LIGHT: return "light";
        case FONT_WEIGHT_NORMAL: return "normal";
        case FONT_WEIGHT_MEDIUM: return "medium";
        case FONT_WEIGHT_SEMI_BOLD: return "semi-bold";
        case FONT_WEIGHT_BOLD: return "bold";
        case FONT_WEIGHT_EXTRA_BOLD: return "extra-bold";
        case FONT_WEIGHT_BLACK: return "black";
        default: return "normal";
    }
}

const char* font_style_to_string(ViewFontStyle style) {
    switch (style) {
        case FONT_STYLE_NORMAL: return "normal";
        case FONT_STYLE_ITALIC: return "italic";
        case FONT_STYLE_OBLIQUE: return "oblique";
        default: return "normal";
    }
}

// Statistics and debugging
FontManagerStats font_manager_get_stats(FontManager* mgr) {
    FontManagerStats stats = {0};
    
    if (!mgr) return stats;
    
    stats.total_fonts_loaded = mgr->stats.fonts_loaded;
    stats.cached_fonts = mgr->font_cache ? mgr->font_cache->entry_count : 0;
    stats.cache_hits = mgr->font_cache ? mgr->font_cache->hits : 0;
    stats.cache_misses = mgr->font_cache ? mgr->font_cache->misses : 0;
    stats.total_requests = stats.cache_hits + stats.cache_misses;
    stats.cache_hit_ratio = stats.total_requests > 0 ? 
        (double)stats.cache_hits / stats.total_requests : 0.0;
    stats.memory_usage = mgr->stats.memory_usage;
    stats.avg_load_time_ms = mgr->stats.avg_load_time;
    
    return stats;
}

void font_manager_print_stats(FontManager* mgr) {
    FontManagerStats stats = font_manager_get_stats(mgr);
    
    printf("Font Manager Statistics:\n");
    printf("  Total fonts loaded: %d\n", stats.total_fonts_loaded);
    printf("  Cached fonts: %d\n", stats.cached_fonts);
    printf("  Cache hits: %llu\n", (unsigned long long)stats.cache_hits);
    printf("  Cache misses: %llu\n", (unsigned long long)stats.cache_misses);
    printf("  Cache hit ratio: %.2f%%\n", stats.cache_hit_ratio * 100.0);
    printf("  Memory usage: %zu bytes\n", stats.memory_usage);
    printf("  Average load time: %.2f ms\n", stats.avg_load_time_ms);
}

void font_manager_reset_stats(FontManager* mgr) {
    if (!mgr) return;
    
    memset(&mgr->stats, 0, sizeof(mgr->stats));
    
    if (mgr->font_cache) {
        mgr->font_cache->hits = 0;
        mgr->font_cache->misses = 0;
        mgr->font_cache->evictions = 0;
    }
}
