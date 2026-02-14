/**
 * Lambda Unified Font Module — Glyph Operations
 *
 * Glyph loading, advance caching, kerning, bitmap rendering, and text
 * measurement.  Consolidates radiant/font.cpp load_glyph() and the
 * TODO "cache glyph advance_x".
 *
 * Copyright (c) 2025 Lambda Script Project
 */

#include "font_internal.h"

#include <math.h>

// ============================================================================
// Glyph advance cache (per-FontHandle hashmap)
// ============================================================================

// maximum entries per-handle advance cache before eviction
#define ADVANCE_CACHE_MAX_ENTRIES 4096

static uint64_t advance_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const GlyphAdvanceEntry* entry = (const GlyphAdvanceEntry*)item;
    return hashmap_xxhash3(&entry->codepoint, sizeof(uint32_t), seed0, seed1);
}

static int advance_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    return ((const GlyphAdvanceEntry*)a)->codepoint - ((const GlyphAdvanceEntry*)b)->codepoint;
}

static struct hashmap* ensure_advance_cache(FontHandle* handle) {
    if (!handle->advance_cache) {
        handle->advance_cache = hashmap_new(sizeof(GlyphAdvanceEntry), 256, 0, 0,
                                             advance_hash, advance_compare, NULL, NULL);
    }
    return handle->advance_cache;
}

// ============================================================================
// Bitmap cache hashmap callbacks
// ============================================================================

static uint64_t bitmap_cache_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const BitmapCacheEntry* entry = (const BitmapCacheEntry*)item;
    // hash on codepoint + mode + handle pointer for uniqueness
    uint64_t data[2];
    data[0] = ((uint64_t)entry->codepoint << 8) | (uint64_t)entry->mode;
    data[1] = (uint64_t)(uintptr_t)entry->handle;
    return hashmap_xxhash3(data, sizeof(data), seed0, seed1);
}

static int bitmap_cache_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const BitmapCacheEntry* ea = (const BitmapCacheEntry*)a;
    const BitmapCacheEntry* eb = (const BitmapCacheEntry*)b;
    if (ea->handle != eb->handle) return ea->handle < eb->handle ? -1 : 1;
    if (ea->codepoint != eb->codepoint) return ea->codepoint < eb->codepoint ? -1 : 1;
    if (ea->mode != eb->mode) return ea->mode < eb->mode ? -1 : 1;
    return 0;
}

static struct hashmap* ensure_bitmap_cache(FontContext* ctx) {
    if (!ctx->bitmap_cache) {
        int cap = ctx->config.max_cached_glyphs > 0 ? ctx->config.max_cached_glyphs / 4 : 1024;
        ctx->bitmap_cache = hashmap_new(sizeof(BitmapCacheEntry), (size_t)cap, 0, 0,
                                        bitmap_cache_hash, bitmap_cache_compare, NULL, NULL);
    }
    return ctx->bitmap_cache;
}

// ============================================================================
// Core: get glyph index for a codepoint
// ============================================================================

uint32_t font_get_glyph_index(FontHandle* handle, uint32_t codepoint) {
    if (!handle || !handle->ft_face) return 0;
    return (uint32_t)FT_Get_Char_Index(handle->ft_face, codepoint);
}

// ============================================================================
// Core: get glyph info for a codepoint
// ============================================================================

GlyphInfo font_get_glyph(FontHandle* handle, uint32_t codepoint) {
    GlyphInfo info = {0};
    if (!handle || !handle->ft_face) return info;

    FT_Face face = handle->ft_face;
    float pixel_ratio = (handle->ctx && handle->ctx->config.pixel_ratio > 0)
                            ? handle->ctx->config.pixel_ratio : 1.0f;

    // check advance cache first
    struct hashmap* cache = ensure_advance_cache(handle);
    if (cache) {
        GlyphAdvanceEntry key = {.codepoint = codepoint};
        GlyphAdvanceEntry* cached = (GlyphAdvanceEntry*)hashmap_get(cache, &key);
        if (cached) {
            info.id = cached->glyph_id;
            info.advance_x = cached->advance_x;
            return info;
        }
    }

    FT_UInt char_index = FT_Get_Char_Index(face, codepoint);
    if (char_index == 0) {
        return info; // glyph not in this font
    }

    // FT_LOAD_NO_HINTING matches browser behavior most closely
    // FT_LOAD_COLOR for color emoji
    FT_Int32 load_flags = FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING | FT_LOAD_COLOR;
    FT_Error err = FT_Load_Glyph(face, char_index, load_flags);
    if (err) {
        log_debug("font_glyph: FT_Load_Glyph failed for U+%04X (error %d)", codepoint, err);
        return info;
    }

    FT_GlyphSlot slot = face->glyph;
    info.id        = char_index;
    info.advance_x = (slot->advance.x / 64.0f) / pixel_ratio;
    info.advance_y = (slot->advance.y / 64.0f) / pixel_ratio;
    info.bearing_x = (slot->metrics.horiBearingX / 64.0f) / pixel_ratio;
    info.bearing_y = (slot->metrics.horiBearingY / 64.0f) / pixel_ratio;
    info.width     = (int)(slot->metrics.width  / 64.0f);
    info.height    = (int)(slot->metrics.height / 64.0f);
    info.is_color  = (slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA);

    // cache the advance (with size limit)
    if (cache) {
        if (hashmap_count(cache) >= ADVANCE_CACHE_MAX_ENTRIES) {
            // simple eviction: clear and start fresh when full
            // advance caches are per-handle and rebuild quickly
            hashmap_clear(cache, false);
        }
        GlyphAdvanceEntry entry = {
            .codepoint = codepoint,
            .glyph_id  = char_index,
            .advance_x = info.advance_x,
        };
        hashmap_set(cache, &entry);
    }

    return info;
}

// ============================================================================
// Kerning
// ============================================================================

float font_get_kerning(FontHandle* handle, uint32_t left, uint32_t right) {
    if (!handle || !handle->ft_face) return 0;

    FT_Face face = handle->ft_face;
    if (!FT_HAS_KERNING(face)) return 0;

    float pixel_ratio = (handle->ctx && handle->ctx->config.pixel_ratio > 0)
                            ? handle->ctx->config.pixel_ratio : 1.0f;

    FT_UInt li = FT_Get_Char_Index(face, left);
    FT_UInt ri = FT_Get_Char_Index(face, right);
    if (li == 0 || ri == 0) return 0;

    FT_Vector delta;
    FT_Get_Kerning(face, li, ri, FT_KERNING_DEFAULT, &delta);
    return (delta.x / 64.0f) / pixel_ratio;
}

float font_get_kerning_by_index(FontHandle* handle, uint32_t left_index, uint32_t right_index) {
    if (!handle || !handle->ft_face) return 0;

    FT_Face face = handle->ft_face;
    if (!FT_HAS_KERNING(face)) return 0;
    if (left_index == 0 || right_index == 0) return 0;

    float pixel_ratio = (handle->ctx && handle->ctx->config.pixel_ratio > 0)
                            ? handle->ctx->config.pixel_ratio : 1.0f;

    FT_Vector delta;
    FT_Get_Kerning(face, left_index, right_index, FT_KERNING_DEFAULT, &delta);
    return (delta.x / 64.0f) / pixel_ratio;
}

// ============================================================================
// Codepoint presence check
// ============================================================================

bool font_has_codepoint(FontHandle* handle, uint32_t codepoint) {
    if (!handle || !handle->ft_face) return false;
    return FT_Get_Char_Index(handle->ft_face, codepoint) != 0;
}

// ============================================================================
// Glyph rendering (bitmap)
// ============================================================================

const GlyphBitmap* font_render_glyph(FontHandle* handle, uint32_t codepoint,
                                      GlyphRenderMode mode) {
    if (!handle || !handle->ft_face) return NULL;

    FontContext* ctx = handle->ctx;
    if (!ctx) return NULL;

    // check bitmap cache first
    struct hashmap* bmp_cache = ensure_bitmap_cache(ctx);
    if (bmp_cache) {
        BitmapCacheEntry search = {.codepoint = codepoint, .mode = mode, .handle = handle};
        BitmapCacheEntry* cached = (BitmapCacheEntry*)hashmap_get(bmp_cache, &search);
        if (cached && cached->bitmap.buffer) {
            return &cached->bitmap;
        }
    }

    FT_Face face = handle->ft_face;
    FT_UInt char_index = FT_Get_Char_Index(face, codepoint);
    if (char_index == 0) return NULL;

    // map GlyphRenderMode → FreeType load flags
    FT_Int32 load_flags = FT_LOAD_RENDER | FT_LOAD_COLOR;
    switch (mode) {
        case GLYPH_RENDER_NORMAL:
            load_flags |= FT_LOAD_TARGET_NORMAL;
            break;
        case GLYPH_RENDER_LCD:
            load_flags |= FT_LOAD_TARGET_LCD;
            break;
        case GLYPH_RENDER_MONO:
            load_flags |= FT_LOAD_TARGET_MONO;
            break;
        case GLYPH_RENDER_SDF:
            // SDF is not directly supported by FT_LOAD; fall back to normal
            load_flags |= FT_LOAD_TARGET_NORMAL;
            break;
    }

    FT_Error err = FT_Load_Glyph(face, char_index, load_flags);
    if (err) {
        log_debug("font_glyph: render FT_Load_Glyph failed for U+%04X (error %d)", codepoint, err);
        return NULL;
    }

    FT_GlyphSlot slot = face->glyph;

    // allocate GlyphBitmap from glyph arena (short-lived, reset per frame)
    GlyphBitmap* bmp = (GlyphBitmap*)arena_calloc(ctx->glyph_arena, sizeof(GlyphBitmap));
    if (!bmp) return NULL;

    bmp->width     = slot->bitmap.width;
    bmp->height    = slot->bitmap.rows;
    bmp->pitch     = slot->bitmap.pitch;
    bmp->bearing_x = slot->bitmap_left;
    bmp->bearing_y = slot->bitmap_top;
    bmp->mode      = mode;

    // copy bitmap data into glyph arena
    size_t buf_size = (size_t)(abs(bmp->pitch) * bmp->height);
    if (buf_size > 0 && slot->bitmap.buffer) {
        bmp->buffer = (uint8_t*)arena_alloc(ctx->glyph_arena, buf_size);
        if (bmp->buffer) {
            memcpy(bmp->buffer, slot->bitmap.buffer, buf_size);
        }
    }

    // insert into bitmap cache (evict if full)
    if (bmp_cache) {
        int max_glyphs = ctx->config.max_cached_glyphs > 0 ? ctx->config.max_cached_glyphs : 4096;
        if ((int)hashmap_count(bmp_cache) >= max_glyphs) {
            // simple eviction: clear the cache (bitmap data lives in glyph_arena
            // which is reset per frame anyway, so this is safe)
            hashmap_clear(bmp_cache, false);
        }
        BitmapCacheEntry entry = {
            .codepoint = codepoint,
            .mode      = mode,
            .handle    = handle,
            .bitmap    = *bmp,
        };
        hashmap_set(bmp_cache, &entry);
    }

    return bmp;
}

// ============================================================================
// Text measurement
// ============================================================================

TextExtents font_measure_text(FontHandle* handle, const char* text, int byte_len) {
    TextExtents ext = {0};
    if (!handle || !text || byte_len <= 0) return ext;
    size_t len = (size_t)byte_len;

    float pixel_ratio = (handle->ctx && handle->ctx->config.pixel_ratio > 0)
                            ? handle->ctx->config.pixel_ratio : 1.0f;

    const FontMetrics* metrics = font_get_metrics(handle);
    if (metrics) {
        ext.height = metrics->ascender - metrics->descender;
    }

    uint32_t prev_codepoint = 0;
    const uint8_t* p = (const uint8_t*)text;
    const uint8_t* end = p + len;

    while (p < end) {
        // decode UTF-8 codepoint
        uint32_t cp;
        int bytes;
        if (*p < 0x80) {
            cp = *p; bytes = 1;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = *p & 0x1F; bytes = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = *p & 0x0F; bytes = 3;
        } else if ((*p & 0xF8) == 0xF0) {
            cp = *p & 0x07; bytes = 4;
        } else {
            p++; continue; // skip invalid byte
        }
        for (int i = 1; i < bytes && (p + i) < end; i++) {
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        p += bytes;

        GlyphInfo glyph = font_get_glyph(handle, cp);
        if (glyph.id == 0) continue;

        // add kerning from previous character
        if (prev_codepoint) {
            ext.width += font_get_kerning(handle, prev_codepoint, cp);
        }

        ext.width += glyph.advance_x;
        ext.glyph_count++;
        prev_codepoint = cp;
    }

    return ext;
}

float font_measure_char(FontHandle* handle, uint32_t codepoint) {
    if (!handle) return 0;

    GlyphInfo glyph = font_get_glyph(handle, codepoint);
    return glyph.advance_x;
}
