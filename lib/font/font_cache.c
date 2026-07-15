/**
 * Lambda Unified Font Module — Face Cache
 *
 * Key-based face cache: "family:weight:slant:size" → FontHandle.
 * Supports LRU eviction when cache exceeds configured maximum.
 *
 * Replaces the ad-hoc fontface_map in
 * radiant/font.cpp with a proper ref-counted cache.
 *
 * Copyright (c) 2025 Lambda Script Project
 */

#include "font_internal.h"
#include "../memtrack.h"
#include "../str.h"

#include <stdio.h>
#include <string.h>

// ============================================================================
// Cache key construction
// ============================================================================

char* font_cache_make_key(Arena* arena, const char* family,
                           FontWeight weight, FontSlant slant, float size_px) {
    // "family:weight:slant:size" — e.g. "Arial:400:0:16.0"
    // Use float precision for size to avoid cache collisions between e.g. 13.33px and 13.67px
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "%s:%d:%d:%.1f",
                     family ? family : "",
                     (int)weight,
                     (int)slant,
                     size_px);
    if (n <= 0 || (size_t)n >= sizeof(buf)) {
        n = (int)sizeof(buf) - 1;
    }
    return arena_strndup(arena, buf, (size_t)n);
}

static const char* browser_metric_family_alias(const char* requested_family) {
    if (!requested_family) return NULL;
#ifdef __APPLE__
    size_t len = strlen(requested_family);
    if (str_ieq_const(requested_family, len, "Times") ||
        str_ieq_const(requested_family, len, "serif") ||
        str_ieq_const(requested_family, len, "ui-serif")) {
        return "Times";
    }
#endif
    return NULL;
}

static void apply_browser_metric_family_alias(FontContext* ctx, FontHandle* handle,
                                              const char* requested_family) {
    if (!ctx || !handle) return;
    const char* alias = browser_metric_family_alias(requested_family);
    if (alias) {
        handle->metric_family_name = arena_strdup(ctx->arena, alias);
    }
}

static FontHandle* font_resolve_ua_default_family(FontContext* ctx,
                                                  const FontStyleDesc* style) {
    if (!ctx || !style) return NULL;

    const char** default_fonts = font_get_generic_family("serif");
    if (!default_fonts) return NULL;

    float pixel_ratio = ctx->config.pixel_ratio;
    float physical_size = style->size_px * pixel_ratio;

    for (int i = 0; default_fonts[i]; i++) {
        FontDatabaseCriteria criteria;
        memset(&criteria, 0, sizeof(criteria));
        strncpy(criteria.family_name, default_fonts[i], sizeof(criteria.family_name) - 1);
        criteria.weight = (int)style->weight;
        criteria.style = (style->slant == FONT_SLANT_ITALIC) ? 1 : 0;

        FontDatabaseResult result = font_database_find_best_match_internal(
            ctx->database, &criteria);
        if (result.font && result.font->file_path &&
            (result.match_score >= 0.5f || result.exact_family_match)) {
            int face_index = result.font->is_collection ? result.font->collection_index : 0;
            FontHandle* handle = font_load_face_internal(ctx, result.font->file_path,
                                                          face_index,
                                                          style->size_px, physical_size,
                                                          style->weight, style->slant);
            if (handle) {
                apply_browser_metric_family_alias(ctx, handle, default_fonts[i]);
                log_info("font_resolve: unresolved family '%s' -> UA default '%s'",
                         style->family, default_fonts[i]);
                return handle;
            }
        }
    }

    return NULL;
}

// ============================================================================
// Cache lookup
// ============================================================================

FontHandle* font_cache_lookup(FontContext* ctx, const char* key,
                              bool allow_global_fallback,
                              bool* out_is_global_fallback) {
    if (!ctx || !ctx->face_cache || !key) return NULL;

    FontCacheKey search = {.key_str = (char*)key, .handle = NULL, .is_global_fallback = false};
    FontCacheKey* found = (FontCacheKey*)hashmap_get(ctx->face_cache, &search);
    if (found && found->handle &&
        (allow_global_fallback || !found->is_global_fallback)) {
        // update LRU tick
        found->handle->lru_tick = ++ctx->lru_counter;
        if (out_is_global_fallback) {
            *out_is_global_fallback = found->is_global_fallback;
        }
        return found->handle;
    }
    return NULL;
}

// ============================================================================
// Cache insert
// ============================================================================

void font_cache_insert(FontContext* ctx, const char* key, FontHandle* handle,
                       bool is_global_fallback) {
    if (!ctx || !key || !handle) return;

    // evict if at capacity
    size_t count = hashmap_count(ctx->face_cache);
    size_t max_faces = ctx->config.max_cached_faces > 0 ? ctx->config.max_cached_faces : 64;
    if (count >= max_faces) {
        font_cache_evict_lru(ctx);
    }

    // retain handle for the cache's ownership
    font_handle_retain(handle);

    // update LRU tick
    handle->lru_tick = ++ctx->lru_counter;

    // arena-dup the key so it outlives the caller
    char* dup_key = arena_strdup(ctx->arena, key);
    FontCacheKey entry = {
        .key_str = dup_key,
        .handle = handle,
        .is_global_fallback = is_global_fallback,
    };
    const FontCacheKey* old = (const FontCacheKey*)hashmap_set(ctx->face_cache, &entry);
    if (old && old->handle) {
        font_handle_release(old->handle); // release replaced entry's handle
    }

    log_debug("font_cache: inserted '%s' (count=%zu)", key, count + 1);
}

// ============================================================================
// LRU eviction — evict the least-recently-used entry
// ============================================================================

typedef struct {
    uint32_t    min_tick;
    const char* min_key;
} LruScanState;

static bool lru_scan_callback(const void* item, void* udata) {
    const FontCacheKey* entry = (const FontCacheKey*)item;
    LruScanState* state = (LruScanState*)udata;

    if (entry->handle && entry->handle->lru_tick < state->min_tick) {
        state->min_tick = entry->handle->lru_tick;
        state->min_key = entry->key_str;
    }
    return true; // continue scanning
}

void font_cache_evict_lru(FontContext* ctx) {
    if (!ctx || !ctx->face_cache) return;

    size_t count = hashmap_count(ctx->face_cache);
    if (count == 0) return;

    // find entry with smallest lru_tick
    LruScanState state = {.min_tick = UINT32_MAX, .min_key = NULL};
    hashmap_scan(ctx->face_cache, lru_scan_callback, &state);

    if (state.min_key) {
        FontCacheKey search = {
            .key_str = (char*)state.min_key,
            .handle = NULL,
            .is_global_fallback = false,
        };
        FontCacheKey* removed = (FontCacheKey*)hashmap_delete(ctx->face_cache, &search);
        if (removed && removed->handle) {
            log_debug("font_cache: evicted '%s' (tick=%u)", state.min_key, state.min_tick);
            font_handle_release(removed->handle);
        }
    }
}

// ============================================================================
// Public: trim cache to a target count
// ============================================================================

void font_cache_trim(FontContext* ctx) {
    if (!ctx || !ctx->face_cache) return;

    size_t max_faces = ctx->config.max_cached_faces > 0 ? ctx->config.max_cached_faces : 64;
    size_t target = max_faces * 3 / 4; // trim to 75% capacity
    while (hashmap_count(ctx->face_cache) > target) {
        font_cache_evict_lru(ctx);
    }
}

// ============================================================================
// Public: font_resolve — the top-level resolution function
//
// Tying together the pipeline:
//   1. Build cache key
//   2. Check face cache → hit? done
//   3. Check @font-face descriptors
//   4. Resolve generic families (serif → Times, etc.)
//   5. Database lookup (weight/slant matching)
//   6. Platform-specific fallback
//   7. Fallback font chain
// ============================================================================

static FontHandle* font_resolve_single(FontContext* ctx, const FontStyleDesc* style,
                                       bool allow_global_fallback,
                                       bool* out_is_global_fallback) {
    if (!ctx || !style) return NULL;
    if (out_is_global_fallback) *out_is_global_fallback = false;

    // 1. build cache key
    char* key = font_cache_make_key(ctx->arena, style->family,
                                     style->weight, style->slant,
                                     style->size_px);
    if (!key) return NULL;

    // 2. check face cache
    FontHandle* handle = font_cache_lookup(ctx, key, allow_global_fallback,
                                           out_is_global_fallback);
    if (handle) {
        font_handle_retain(handle);
        return handle;
    }

    float pixel_ratio = ctx->config.pixel_ratio;
    float physical_size = style->size_px * pixel_ratio;

    // 3. check @font-face descriptors
    const FontFaceEntry* face_entry = font_face_find_internal(ctx, style->family,
                                                               style->weight, style->slant);
    if (face_entry) {
        // delegate to font_face_load() which handles caching-by-size internally
        const FontFaceDesc* desc = font_face_find(ctx, style);
        if (desc) {
            handle = font_face_load(ctx, desc, style->size_px);
            if (handle) {
                log_info("font_resolve: loaded @font-face for '%s'", style->family);
                font_cache_insert(ctx, key, handle, false);
                return handle;
            }
        }
    }

    // 3.5. Browser-compatible family aliases that must win before exact local
    // matches on platforms where the browser treats a requested family as a
    // metric-compatible substitute.
    {
        const char* const* aliases = font_get_browser_compat_aliases(style->family);
        if (aliases) {
            for (int i = 0; aliases[i]; i++) {
                FontDatabaseCriteria criteria;
                memset(&criteria, 0, sizeof(criteria));
                strncpy(criteria.family_name, aliases[i], sizeof(criteria.family_name) - 1);
                criteria.weight = (int)style->weight;
                criteria.style = (style->slant == FONT_SLANT_ITALIC) ? 1 : 0;

                FontDatabaseResult result = font_database_find_best_match_internal(
                    ctx->database, &criteria);
                if (result.font && result.font->file_path && result.match_score >= 0.5f) {
                    int face_index = result.font->is_collection ? result.font->collection_index : 0;
                    handle = font_load_face_internal(ctx, result.font->file_path, face_index,
                                                      style->size_px, physical_size,
                                                      style->weight, style->slant);
                    if (handle) {
                        apply_browser_metric_family_alias(ctx, handle, style->family);
                        log_info("font_resolve: browser alias '%s' → '%s'",
                                 style->family, aliases[i]);
                        font_cache_insert(ctx, key, handle, false);
                        return handle;
                    }
                }
            }
        }
    }

    // 4. resolve generic family (serif, sans-serif, monospace, etc.)
    const char** generics = font_get_generic_family(style->family);
    if (generics) {
        for (int i = 0; generics[i]; i++) {
            FontStyleDesc resolved_style = *style;
            resolved_style.family = generics[i];

            // try database match for each candidate
            FontDatabaseCriteria criteria;
            memset(&criteria, 0, sizeof(criteria));
            strncpy(criteria.family_name, generics[i], sizeof(criteria.family_name) - 1);
            criteria.weight = (int)style->weight;
            criteria.style = (style->slant == FONT_SLANT_ITALIC) ? 1 : 0;

            FontDatabaseResult result = font_database_find_best_match_internal(
                ctx->database, &criteria);
            if (result.font && result.font->file_path &&
                (result.match_score >= 0.5f || result.exact_family_match)) {
                int face_index = result.font->is_collection ? result.font->collection_index : 0;
                handle = font_load_face_internal(ctx, result.font->file_path, face_index,
                                                  style->size_px, physical_size,
                                                  style->weight, style->slant);
                if (handle) {
                    apply_browser_metric_family_alias(ctx, handle, style->family);
                    log_info("font_resolve: generic '%s' → '%s'", style->family, generics[i]);
                    font_cache_insert(ctx, key, handle, false);
                    return handle;
                }
            }
        }
    }

    // 5. database lookup (direct family match)
    {
        FontDatabaseCriteria criteria;
        memset(&criteria, 0, sizeof(criteria));
        strncpy(criteria.family_name, style->family, sizeof(criteria.family_name) - 1);
        criteria.weight = (int)style->weight;
        criteria.style = (style->slant == FONT_SLANT_ITALIC) ? 1 : 0;

        FontDatabaseResult result = font_database_find_best_match_internal(
            ctx->database, &criteria);
        if (result.font && result.font->file_path && result.match_score >= 0.5f) {
            int face_index = result.font->is_collection ? result.font->collection_index : 0;
            handle = font_load_face_internal(ctx, result.font->file_path, face_index,
                                              style->size_px, physical_size,
                                              style->weight, style->slant);
            if (handle) {
                log_info("font_resolve: database match for '%s' (score=%.2f)",
                         style->family, result.match_score);
                font_cache_insert(ctx, key, handle, false);
                return handle;
            }
        }
    }

    // 5.5. font alias substitution (e.g. Times New Roman → Liberation Serif)
    {
        const char* const* aliases = font_get_aliases(style->family);
        if (aliases) {
            for (int i = 0; aliases[i]; i++) {
                FontDatabaseCriteria criteria;
                memset(&criteria, 0, sizeof(criteria));
                strncpy(criteria.family_name, aliases[i], sizeof(criteria.family_name) - 1);
                criteria.weight = (int)style->weight;
                criteria.style = (style->slant == FONT_SLANT_ITALIC) ? 1 : 0;

                FontDatabaseResult result = font_database_find_best_match_internal(
                    ctx->database, &criteria);
                if (result.font && result.font->file_path && result.match_score >= 0.5f) {
                    int face_index = result.font->is_collection ? result.font->collection_index : 0;
                    handle = font_load_face_internal(ctx, result.font->file_path, face_index,
                                                      style->size_px, physical_size,
                                                      style->weight, style->slant);
                    if (handle) {
                        log_info("font_resolve: alias '%s' → '%s'", style->family, aliases[i]);
                        font_cache_insert(ctx, key, handle, false);
                        return handle;
                    }
                }
            }
        }
    }

    // 6. platform-specific fallback
    {
        char* platform_path = font_platform_find_fallback(style->family);
        if (platform_path) {
            handle = font_load_face_internal(ctx, platform_path, 0,
                                              style->size_px, physical_size,
                                              style->weight, style->slant);
            mem_free(platform_path);
            if (handle) {
                log_info("font_resolve: platform fallback for '%s'", style->family);
                font_cache_insert(ctx, key, handle, false);
                return handle;
            }
        }
    }

    if (!allow_global_fallback) return NULL;

    // 7. css font matching fallback: use the UA default font family before
    // walking the broad glyph fallback chain.
    handle = font_resolve_ua_default_family(ctx, style);
    if (handle) {
        if (out_is_global_fallback) *out_is_global_fallback = true;
        font_cache_insert(ctx, key, handle, true);
        return handle;
    }

    // 8. fallback font chain
    handle = font_resolve_fallback(ctx, style);
    if (handle) {
        log_info("font_resolve: using fallback font for '%s'", style->family);
        if (out_is_global_fallback) *out_is_global_fallback = true;
        font_cache_insert(ctx, key, handle, true);
        return handle;
    }

    log_error("font_resolve: failed to resolve any font for '%s'", style->family);
    return NULL;
}

FontHandle* font_resolve(FontContext* ctx, const FontStyleDesc* style) {
    if (!ctx || !style || !style->family) return NULL;

    char* list_key = font_cache_make_key(ctx->arena, style->family,
                                         style->weight, style->slant,
                                         style->size_px);
    if (!list_key) return NULL;
    FontHandle* cached = font_cache_lookup(ctx, list_key, true, NULL);
    if (cached) {
        font_handle_retain(cached);
        return cached;
    }

    const char* cursor = style->family;
    char family[256];
    char first_family[256] = {0};
    while (font_family_list_next(&cursor, family, sizeof(family))) {
        if (!first_family[0]) {
            strncpy(first_family, family, sizeof(first_family) - 1);
        }
        FontStyleDesc candidate = *style;
        candidate.family = family;
        FontHandle* handle = font_resolve_single(ctx, &candidate, false, NULL);
        if (handle) {
            // CSS Fonts requires trying each family before the global fallback chain
            font_cache_insert(ctx, list_key, handle, false);
            return handle;
        }
    }

    FontStyleDesc fallback = *style;
    if (first_family[0]) fallback.family = first_family;
    bool is_global_fallback = false;
    FontHandle* handle = font_resolve_single(ctx, &fallback, true,
                                             &is_global_fallback);
    if (handle) {
        font_cache_insert(ctx, list_key, handle, is_global_fallback);
    }
    return handle;
}

// ============================================================================
// Public: resolve font for a specific codepoint (fallback chain)
// ============================================================================

FontHandle* font_resolve_for_codepoint(FontContext* ctx, const FontStyleDesc* style,
                                        uint32_t codepoint) {
    if (!ctx || !style) return NULL;

    // CSS Fonts matches the requested character against each authored family;
    // choosing the first loadable face alone incorrectly turns a missing glyph
    // into a global fallback before later families are considered.
    const char* cursor = style->family;
    char family[256];
    while (font_family_list_next(&cursor, family, sizeof(family))) {
        FontStyleDesc candidate = *style;
        candidate.family = family;
        FontHandle* handle = font_resolve(ctx, &candidate);
        if (handle && font_has_codepoint(handle, codepoint)) return handle;
        if (handle) font_handle_release(handle);
    }

    // No authored family covers the character; continue through the UA fallback chain.
    FontHandle* fallback = font_find_codepoint_fallback(ctx, style, codepoint);
    return fallback;
}
