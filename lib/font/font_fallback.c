/**
 * Lambda Unified Font Module — Fallback Resolution
 *
 * Generic CSS family resolution (serif → Times New Roman, etc.),
 * fallback font chain walking, and codepoint-to-face mapping cache.
 *
 * Consolidates:
 *  - radiant/font.cpp resolve_generic_family()
 *  - radiant/font.cpp glyph_fallback_cache (GlyphFallbackEntry)
 *  - radiant/font.cpp setup_font() fallback loop
 *
 * Copyright (c) 2025 Lambda Script Project
 */

#include "font_internal.h"
#include "../str.h"
#include "../memtrack.h"

// ============================================================================
// Generic CSS family → concrete font name lists
// Mirroring Chrome defaults with cross-platform fallbacks
// ============================================================================

static const char* serif_fonts[] = {
#ifdef __APPLE__
    "Times", "Times New Roman", "Liberation Serif", "Nimbus Roman",
#else
    "Times New Roman", "Liberation Serif", "Times", "Nimbus Roman",
#endif
    "Georgia", "DejaVu Serif", NULL
};
static const char* sans_serif_fonts[] = {
    "Arial", "Liberation Sans", "Helvetica", "Nimbus Sans",
    "DejaVu Sans", NULL
};
static const char* monospace_fonts[] = {
    "Menlo", "Monaco", "Courier New", "Liberation Mono", "Courier",
    "Nimbus Mono PS", "DejaVu Sans Mono", NULL
};
static const char* cursive_fonts[] = {
    "Comic Sans MS", "Apple Chancery", NULL
};
static const char* fantasy_fonts[] = {
    "Impact", "Papyrus", NULL
};
// CSS Fonts Level 4
static const char* ui_monospace_fonts[] = {
    "SF Mono", "Menlo", "Monaco", "Consolas", "Liberation Mono",
    "Courier New", NULL
};
static const char* system_ui_fonts[] = {
    "System Font", "SF Pro Display", "SF Pro", ".AppleSystemUIFont", "Segoe UI",
    "Roboto", "Liberation Sans", "Arial", NULL
};

const char** font_get_generic_family(const char* family) {
    if (!family) return NULL;

    // CSS core generic families
    if (strcmp(family, "serif") == 0) return serif_fonts;
    if (strcmp(family, "sans-serif") == 0) return sans_serif_fonts;
    if (strcmp(family, "monospace") == 0) return monospace_fonts;
    if (strcmp(family, "cursive") == 0) return cursive_fonts;
    if (strcmp(family, "fantasy") == 0) return fantasy_fonts;

    // CSS Fonts Level 4 generic families
    if (strcmp(family, "ui-monospace") == 0) return ui_monospace_fonts;
    if (strcmp(family, "system-ui") == 0) return system_ui_fonts;
    if (strcmp(family, "ui-serif") == 0) return serif_fonts;
    if (strcmp(family, "ui-sans-serif") == 0) return sans_serif_fonts;
    if (strcmp(family, "ui-rounded") == 0) return sans_serif_fonts;

    // Apple/Safari-specific system font keywords
    if (strcmp(family, "-apple-system") == 0) return system_ui_fonts;
    if (strcmp(family, "BlinkMacSystemFont") == 0) return system_ui_fonts;

    // NOTE: Concrete font names like "Arial", "Times New Roman", "Courier New"
    // are NOT CSS generic families. They should go through the database lookup
    // path (step 5) in font_resolve() so that weight/slant matching works
    // correctly (e.g., Arial Bold vs Arial Regular).

    return NULL;
}

// ============================================================================
// Font alias table — metric-compatible substitutions
// Maps common fonts to their open-source equivalents (Liberation, DejaVu, etc.)
// Used when the requested font is not installed on the system.
// ============================================================================

static const struct {
    const char* name;
    const char* aliases[4];
} font_alias_table[] = {
    // Liberation fonts are metrically compatible with their MS counterparts
    {"Times New Roman",   {"Liberation Serif", "DejaVu Serif", "Nimbus Roman", NULL}},
    {"Times",             {"Liberation Serif", "DejaVu Serif", "Nimbus Roman", NULL}},
    {"Arial",             {"Liberation Sans", "DejaVu Sans", "Nimbus Sans", NULL}},
    {"Helvetica",         {"Liberation Sans", "DejaVu Sans", "Nimbus Sans", NULL}},
    {"Helvetica Neue",    {"Liberation Sans", "DejaVu Sans", NULL, NULL}},
    {"Courier New",       {"Liberation Mono", "DejaVu Sans Mono", "Nimbus Mono PS", NULL}},
    {"Courier",           {"Liberation Mono", "DejaVu Sans Mono", "Nimbus Mono PS", NULL}},
    {"Georgia",           {"Liberation Serif", "DejaVu Serif", NULL, NULL}},
    {"Verdana",           {"Liberation Sans", "DejaVu Sans", NULL, NULL}},
    {"Trebuchet MS",      {"Liberation Sans", "DejaVu Sans", NULL, NULL}},
    {"Palatino",          {"Liberation Serif", "DejaVu Serif", NULL, NULL}},
    {"Palatino Linotype", {"Liberation Serif", "DejaVu Serif", NULL, NULL}},
    {"Book Antiqua",      {"Liberation Serif", "DejaVu Serif", NULL, NULL}},
    {"Tahoma",            {"Liberation Sans", "DejaVu Sans", NULL, NULL}},
    {"Lucida Grande",     {"Liberation Sans", "DejaVu Sans", NULL, NULL}},
    {"Impact",            {"Liberation Sans", "DejaVu Sans", NULL, NULL}},
    {"Comic Sans MS",     {"Liberation Sans", "DejaVu Sans", NULL, NULL}},
    {NULL, {NULL, NULL, NULL, NULL}}
};

const char** font_get_aliases(const char* family) {
    if (!family) return NULL;
    for (int i = 0; font_alias_table[i].name; i++) {
        if (str_ieq(font_alias_table[i].name, strlen(font_alias_table[i].name),
                     family, strlen(family))) {
            return font_alias_table[i].aliases;
        }
    }
    return NULL;
}

// ============================================================================
// Fallback font resolution — walk a configured list of fallback families
// ============================================================================

FontHandle* font_resolve_fallback(FontContext* ctx, const FontStyleDesc* style) {
    if (!ctx || !style) return NULL;

    float pixel_ratio = ctx->config.pixel_ratio;
    float physical_size = style->size_px * pixel_ratio;

    // walk fallback_fonts from FontContext (set during font_context_create)
    if (ctx->fallback_fonts) {
        for (int i = 0; ctx->fallback_fonts[i]; i++) {
            const char* fallback_name = ctx->fallback_fonts[i];

            // try database match
            FontDatabaseCriteria criteria;
            memset(&criteria, 0, sizeof(criteria));
            strncpy(criteria.family_name, fallback_name, sizeof(criteria.family_name) - 1);
            criteria.weight = (int)style->weight;
            criteria.style = (style->slant == FONT_SLANT_ITALIC) ? 1 : 0;

            FontDatabaseResult result = font_database_find_best_match_internal(
                ctx->database, &criteria);
            if (result.font && result.font->file_path && result.exact_family_match) {
                int face_index = result.font->is_collection ? result.font->collection_index : 0;
                FontHandle* handle = font_load_face_internal(
                    ctx, result.font->file_path, face_index,
                    style->size_px, physical_size,
                    style->weight, style->slant);
                if (handle) {
                    log_info("font_fallback: resolved '%s' via fallback '%s'",
                             style->family, fallback_name);
                    return handle;
                }
            }
        }
    }

    return NULL;
}

// ============================================================================
// Codepoint-specific fallback — find a font that has a given codepoint
// ============================================================================

// CodepointFallbackEntry is defined in font_internal.h

static uint64_t cp_fallback_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const CodepointFallbackEntry* e = (const CodepointFallbackEntry*)item;
    return hashmap_xxhash3(&e->codepoint, sizeof(uint32_t), seed0, seed1);
}

static int cp_fallback_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const CodepointFallbackEntry* ea = (const CodepointFallbackEntry*)a;
    const CodepointFallbackEntry* eb = (const CodepointFallbackEntry*)b;
    return (ea->codepoint == eb->codepoint) ? 0 : (ea->codepoint < eb->codepoint ? -1 : 1);
}

static void cp_fallback_free(void* item) {
    CodepointFallbackEntry* entry = (CodepointFallbackEntry*)item;
    if (entry && entry->handle) {
        font_handle_release(entry->handle);
    }
}

static struct hashmap* ensure_codepoint_cache(FontContext* ctx) {
    if (!ctx->codepoint_fallback_cache) {
        ctx->codepoint_fallback_cache = hashmap_new(sizeof(CodepointFallbackEntry),
                                                     256, 0, 0,
                                                     cp_fallback_hash,
                                                     cp_fallback_compare,
                                                     cp_fallback_free, NULL);
    }
    return ctx->codepoint_fallback_cache;
}

// small path-based cache of recently-resolved platform fallback handles,
// keyed by (file_path, face_index, size_px). Avoids expensive
// font_load_face_internal for each codepoint when most map to the same fonts.
#define PLATFORM_FB_CACHE_SIZE 32
typedef struct {
    const char* path;       // borrowed from handle->file_data_path
    int         face_index;
    float       size_px;
    FontHandle* handle;
} PlatformFbEntry;

static PlatformFbEntry s_platform_fb[PLATFORM_FB_CACHE_SIZE];
static int             s_platform_fb_count = 0;

void font_fallback_reset_platform_cache(void) {
    s_platform_fb_count = 0;
}

static FontHandle* platform_fb_lookup(const char* path, int face_index,
                                       float size_px, uint32_t codepoint) {
    for (int i = 0; i < s_platform_fb_count; i++) {
        if (s_platform_fb[i].handle &&
            s_platform_fb[i].face_index == face_index &&
            s_platform_fb[i].size_px == size_px &&
            s_platform_fb[i].path &&
            strcmp(s_platform_fb[i].path, path) == 0 &&
            font_has_codepoint(s_platform_fb[i].handle, codepoint)) {
            return s_platform_fb[i].handle;
        }
    }
    return NULL;
}

static void platform_fb_insert(FontHandle* handle, int face_index, float size_px) {
    if (!handle || !handle->file_data_path) return;
    // check for duplicate
    for (int i = 0; i < s_platform_fb_count; i++) {
        if (s_platform_fb[i].handle == handle) return;
    }
    if (s_platform_fb_count < PLATFORM_FB_CACHE_SIZE) {
        s_platform_fb[s_platform_fb_count].path = handle->file_data_path;
        s_platform_fb[s_platform_fb_count].face_index = face_index;
        s_platform_fb[s_platform_fb_count].size_px = size_px;
        s_platform_fb[s_platform_fb_count].handle = handle;
        s_platform_fb_count++;
    }
}

FontHandle* font_find_codepoint_fallback(FontContext* ctx, const FontStyleDesc* style,
                                          uint32_t codepoint) {
    if (!ctx || !style) return NULL;

    // check codepoint fallback cache
    struct hashmap* cache = ensure_codepoint_cache(ctx);
    if (cache) {
        CodepointFallbackEntry key = {.codepoint = codepoint, .handle = NULL};
        CodepointFallbackEntry* cached = (CodepointFallbackEntry*)hashmap_get(cache, &key);
        if (cached) {
            if (cached->handle) {
                font_handle_retain(cached->handle);
                return cached->handle;
            }
            return NULL; // negative cache hit
        }
    }

    float pixel_ratio = ctx->config.pixel_ratio;
    float physical_size = style->size_px * pixel_ratio;

    // search through fallback fonts for one that has this codepoint
    if (ctx->fallback_fonts) {
        for (int i = 0; ctx->fallback_fonts[i]; i++) {
            FontDatabaseCriteria criteria;
            memset(&criteria, 0, sizeof(criteria));
            strncpy(criteria.family_name, ctx->fallback_fonts[i],
                    sizeof(criteria.family_name) - 1);
            criteria.weight = (int)style->weight;
            criteria.style = (style->slant == FONT_SLANT_ITALIC) ? 1 : 0;
            criteria.required_codepoint = codepoint;

            FontDatabaseResult result = font_database_find_best_match_internal(
                ctx->database, &criteria);
            // only use the result if it's an exact family match — avoid picking
            // a random font (e.g. Menlo) when the requested family isn't installed
            if (result.font && result.font->file_path && result.exact_family_match) {
                int face_index = result.font->is_collection ? result.font->collection_index : 0;
                FontHandle* handle = font_load_face_internal(
                    ctx, result.font->file_path, face_index,
                    style->size_px, physical_size,
                    style->weight, style->slant);
                if (handle && font_has_codepoint(handle, codepoint)) {
                    // cache positive result
                    CodepointFallbackEntry entry = {.codepoint = codepoint, .handle = handle};
                    font_handle_retain(handle);
                    hashmap_set(cache, &entry);
                    log_debug("font_fallback: codepoint U+%04X → '%s'",
                              codepoint, ctx->fallback_fonts[i]);
                    return handle;
                }
                if (handle) font_handle_release(handle);
            }
        }
    }

    // platform-specific codepoint lookup (macOS: CoreText CTFontCreateForString)
    // Check the platform fallback handle cache first — most codepoints from
    // the same Unicode block resolve to the same font file.
    {
        int face_index = 0;
        char* font_path = font_platform_find_codepoint_font(codepoint, &face_index);
        if (font_path) {
            // fast path: reuse an existing handle for this font file
            FontHandle* reused = platform_fb_lookup(font_path, face_index, style->size_px, codepoint);
            if (reused) {
                CodepointFallbackEntry entry = {.codepoint = codepoint, .handle = reused};
                font_handle_retain(reused);
                hashmap_set(cache, &entry);
                font_handle_retain(reused);
                log_debug("font_fallback: codepoint U+%04X → reused cached handle (face %d)",
                          codepoint, face_index);
                mem_free(font_path);
                return reused;
            }

            FontHandle* handle = font_load_face_internal(
                ctx, font_path, face_index,
                style->size_px, physical_size,
                style->weight, style->slant);
            if (handle && font_has_codepoint(handle, codepoint)) {
                // cache positive result
                CodepointFallbackEntry entry = {.codepoint = codepoint, .handle = handle};
                font_handle_retain(handle);
                hashmap_set(cache, &entry);
                platform_fb_insert(handle, face_index, style->size_px);
                log_debug("font_fallback: codepoint U+%04X → platform font (face %d)", codepoint, face_index);
                mem_free(font_path);
                return handle;
            }
            // TTC fallback: face_index=0 may not have the codepoint even though
            // the collection file does (e.g., Songti.ttc face 0 is "Black" variant
            // which lacks some CJK glyphs). Try other face indices in the collection.
            long num_faces = (handle && handle->memory_buffer)
                ? font_tables_get_face_count(handle->memory_buffer, handle->memory_buffer_size)
                : 0;
            if (handle) font_handle_release(handle);
            if (num_faces > 1) {
                for (long fi = 1; fi < num_faces; fi++) {
                    handle = font_load_face_internal(
                        ctx, font_path, (int)fi,
                        style->size_px, physical_size,
                        style->weight, style->slant);
                    if (handle && font_has_codepoint(handle, codepoint)) {
                        CodepointFallbackEntry entry = {.codepoint = codepoint, .handle = handle};
                        font_handle_retain(handle);
                        hashmap_set(cache, &entry);
                        platform_fb_insert(handle, (int)fi, style->size_px);
                        log_debug("font_fallback: codepoint U+%04X → platform font (face %ld of %ld)",
                                  codepoint, fi, num_faces);
                        mem_free(font_path);
                        return handle;
                    }
                    if (handle) font_handle_release(handle);
                }
            }
            mem_free(font_path);
        }
    }

    // negative cache — no fallback has this codepoint
    CodepointFallbackEntry neg = {.codepoint = codepoint, .handle = NULL};
    hashmap_set(cache, &neg);
    log_debug("font_fallback: no fallback for codepoint U+%04X", codepoint);
    return NULL;
}
