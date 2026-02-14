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

// ============================================================================
// Generic CSS family → concrete font name lists
// Mirroring Chrome defaults with cross-platform fallbacks
// ============================================================================

static const char* serif_fonts[] = {
    "Times New Roman", "Liberation Serif", "Times", "Nimbus Roman",
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
    "SF Pro Display", "SF Pro", ".AppleSystemUIFont", "Segoe UI",
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

    // Cross-platform font aliases
    if (strcmp(family, "Times New Roman") == 0 || strcmp(family, "Times") == 0)
        return serif_fonts;
    if (strcmp(family, "Arial") == 0 || strcmp(family, "Helvetica") == 0)
        return sans_serif_fonts;
    if (strcmp(family, "Courier New") == 0 || strcmp(family, "Courier") == 0)
        return monospace_fonts;

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
            if (result.font && result.font->file_path && result.match_score >= 0.5f) {
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

// Internal cache: codepoint → FontHandle* (or NULL for negative cache)
typedef struct CodepointFallbackEntry {
    uint32_t    codepoint;
    FontHandle* handle;     // NULL = negative cache (no font has this codepoint)
} CodepointFallbackEntry;

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

static struct hashmap* ensure_codepoint_cache(FontContext* ctx) {
    if (!ctx->codepoint_fallback_cache) {
        ctx->codepoint_fallback_cache = hashmap_new(sizeof(CodepointFallbackEntry),
                                                     256, 0, 0,
                                                     cp_fallback_hash,
                                                     cp_fallback_compare,
                                                     NULL, NULL);
    }
    return ctx->codepoint_fallback_cache;
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
            if (result.font && result.font->file_path) {
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

    // negative cache — no fallback has this codepoint
    CodepointFallbackEntry neg = {.codepoint = codepoint, .handle = NULL};
    hashmap_set(cache, &neg);
    log_debug("font_fallback: no fallback for codepoint U+%04X", codepoint);
    return NULL;
}
