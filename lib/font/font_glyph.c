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
// Phase 17: LoadedGlyph cache for font_load_glyph
// ============================================================================

// forward declaration — defined later alongside font_load_glyph
static LoadedGlyph s_loaded_glyph;

static uint64_t loaded_glyph_cache_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const LoadedGlyphCacheEntry* entry = (const LoadedGlyphCacheEntry*)item;
    uint64_t data[2];
    data[0] = (uint64_t)(uintptr_t)entry->caller_handle;
    data[1] = ((uint64_t)entry->codepoint << 1) | (uint64_t)(entry->for_rendering ? 1 : 0);
    return hashmap_xxhash3(data, sizeof(data), seed0, seed1);
}

static int loaded_glyph_cache_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const LoadedGlyphCacheEntry* ea = (const LoadedGlyphCacheEntry*)a;
    const LoadedGlyphCacheEntry* eb = (const LoadedGlyphCacheEntry*)b;
    if (ea->caller_handle != eb->caller_handle) return ea->caller_handle < eb->caller_handle ? -1 : 1;
    if (ea->codepoint != eb->codepoint) return ea->codepoint < eb->codepoint ? -1 : 1;
    if (ea->for_rendering != eb->for_rendering) return ea->for_rendering ? 1 : -1;
    return 0;
}

static struct hashmap* ensure_loaded_glyph_cache(FontContext* ctx) {
    if (!ctx->loaded_glyph_cache) {
        int cap = ctx->config.max_cached_glyphs > 0 ? ctx->config.max_cached_glyphs : 4096;
        ctx->loaded_glyph_cache = hashmap_new(sizeof(LoadedGlyphCacheEntry), (size_t)cap, 0, 0,
                                               loaded_glyph_cache_hash, loaded_glyph_cache_compare, NULL, NULL);
    }
    return ctx->loaded_glyph_cache;
}

// deep-copy the current s_loaded_glyph into the loaded glyph cache
static void cache_loaded_glyph(FontContext* ctx, FontHandle* caller_handle,
                                uint32_t codepoint, bool for_rendering) {
    struct hashmap* cache = ensure_loaded_glyph_cache(ctx);
    if (!cache) return;

    // evict if full
    int max_entries = ctx->config.max_cached_glyphs > 0 ? ctx->config.max_cached_glyphs : 4096;
    if ((int)hashmap_count(cache) >= max_entries) {
        hashmap_clear(cache, false);
    }

    LoadedGlyphCacheEntry entry;
    entry.caller_handle = caller_handle;
    entry.codepoint = codepoint;
    entry.for_rendering = for_rendering;
    entry.glyph = s_loaded_glyph;

    // deep-copy bitmap buffer into glyph_arena for persistence
    if (s_loaded_glyph.bitmap.buffer && s_loaded_glyph.bitmap.height > 0) {
        size_t buf_size = (size_t)(abs(s_loaded_glyph.bitmap.pitch) * s_loaded_glyph.bitmap.height);
        if (buf_size > 0 && ctx->glyph_arena) {
            uint8_t* copy = (uint8_t*)arena_alloc(ctx->glyph_arena, buf_size);
            if (copy) {
                memcpy(copy, s_loaded_glyph.bitmap.buffer, buf_size);
                entry.glyph.bitmap.buffer = copy;
            }
        }
    }

    hashmap_set(cache, &entry);
}

// ============================================================================
// Core: get glyph index for a codepoint
// ============================================================================

uint32_t font_get_glyph_index(FontHandle* handle, uint32_t codepoint) {
    if (!handle) return 0;
    if (handle->tables) {
        CmapTable* cmap = font_tables_get_cmap(handle->tables);
        if (cmap) return (uint32_t)cmap_lookup(cmap, codepoint);
    }
    return 0;
}

// ============================================================================
// Core: get glyph info for a codepoint
// ============================================================================

GlyphInfo font_get_glyph(FontHandle* handle, uint32_t codepoint) {
    GlyphInfo info = {0};
    if (!handle) return info;

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

    // resolve codepoint → glyph index via FontTables cmap
    uint32_t char_index = 0;
    if (handle->tables) {
        CmapTable* cmap = font_tables_get_cmap(handle->tables);
        if (cmap) char_index = (uint32_t)cmap_lookup(cmap, codepoint);
    }
    if (char_index == 0) {
        return info; // glyph not in this font
    }

#ifndef __APPLE__
    // primary: FreeType — full glyph metrics
    {
        FT_Face face = handle->ft_face;
        if (face) {
            FT_Int32 load_flags = FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING | FT_LOAD_COLOR;
            FT_Error err = FT_Load_Glyph(face, char_index, load_flags);
            if (!err) {
                FT_GlyphSlot slot = face->glyph;
                float bscale = handle->bitmap_scale;
                info.id        = char_index;
                info.advance_x = (slot->advance.x / 64.0f) * bscale / pixel_ratio;
                info.advance_y = (slot->advance.y / 64.0f) * bscale / pixel_ratio;
                info.bearing_x = (slot->metrics.horiBearingX / 64.0f) * bscale / pixel_ratio;
                info.bearing_y = (slot->metrics.horiBearingY / 64.0f) * bscale / pixel_ratio;
                info.width     = (int)(slot->metrics.width  / 64.0f * bscale);
                info.height    = (int)(slot->metrics.height / 64.0f * bscale);
                info.is_color  = (slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA);
                goto apply_overrides;
            }
            log_debug("font_glyph: FT_Load_Glyph failed for U+%04X (error %d)", codepoint, err);
        }
    }
#endif

#ifdef __APPLE__
    // primary (macOS): CoreText ct_raster_ref for full glyph metrics
    if (handle->ct_raster_ref) {
        GlyphInfo ct_info = {0};
        if (font_rasterize_ct_metrics(handle->ct_raster_ref, codepoint,
                                       handle->bitmap_scale, &ct_info)) {
            ct_info.id = char_index;
            info = ct_info;
            goto apply_overrides;
        }
    }
#endif

    // fallback: FontTables hmtx — advance only (for layout)
    if (handle->tables) {
        HmtxTable* hmtx = font_tables_get_hmtx(handle->tables);
        HeadTable* head = font_tables_get_head(handle->tables);
        if (hmtx && head && head->units_per_em > 0) {
            uint16_t adv = hmtx_get_advance(hmtx, (uint16_t)char_index);
            float uscale = handle->size_px / (float)head->units_per_em * handle->bitmap_scale;
            info.id = char_index;
            info.advance_x = adv * uscale;
        }
    }

apply_overrides:

#ifdef __APPLE__
    // For fonts with a CoreText reference (e.g., SFNS.ttf / -apple-system), use
    // CoreText advances instead of FreeType.  We create the CTFont at CSS_size so
    // CoreText selects the same optical-size instance as Chrome.  The returned
    // advance is already in CSS pixels - no pixel_ratio division needed.
    if (handle->ct_font_ref) {
        float ft_adv = info.advance_x;
        float ct_adv = font_platform_get_glyph_advance(handle->ct_font_ref, codepoint);
        if (ct_adv >= 0.0f) {
            info.advance_x = ct_adv;
            // one-time log: show FT vs CT for letter 't' to confirm fix works
            if (codepoint == 't') {
                static bool logged_t = false;
                if (!logged_t) {
                    logged_t = true;
                    log_debug("font_glyph: CT advance fix active: 't' ft=%.3f ct=%.3f family=%s size=%.0fpx",
                              ft_adv, ct_adv,
                              handle->family_name ? handle->family_name : "?",
                              handle->size_px);
                }
            }
        }
    }
#endif

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

// kern pair cache hashmap callbacks
#define KERN_CACHE_MAX_ENTRIES 4096

static uint64_t kern_pair_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const KernPairEntry* e = (const KernPairEntry*)item;
    uint64_t key = ((uint64_t)e->left_cp << 32) | (uint64_t)e->right_cp;
    return hashmap_xxhash3(&key, sizeof(key), seed0, seed1);
}

static int kern_pair_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const KernPairEntry* ea = (const KernPairEntry*)a;
    const KernPairEntry* eb = (const KernPairEntry*)b;
    if (ea->left_cp != eb->left_cp) return ea->left_cp < eb->left_cp ? -1 : 1;
    if (ea->right_cp != eb->right_cp) return ea->right_cp < eb->right_cp ? -1 : 1;
    return 0;
}

#ifdef __APPLE__
static float font_get_kerning_coretext(FontHandle* handle, uint32_t left, uint32_t right) {
    if (!handle->ct_font_ref) return 0.0f;

    // check kern pair cache
    if (!handle->kern_cache) {
        handle->kern_cache = hashmap_new(sizeof(KernPairEntry), 256, 0, 0,
                                          kern_pair_hash, kern_pair_compare, NULL, NULL);
    }

    KernPairEntry probe = { .left_cp = left, .right_cp = right };
    const KernPairEntry* cached = (const KernPairEntry*)hashmap_get(handle->kern_cache, &probe);
    if (cached) return cached->kerning;

    // compute via CoreText
    float kerning = font_platform_get_pair_kerning(handle->ct_font_ref, left, right);

    // cache the result
    if (hashmap_count(handle->kern_cache) >= KERN_CACHE_MAX_ENTRIES) {
        hashmap_clear(handle->kern_cache, false);
    }
    probe.kerning = kerning;
    hashmap_set(handle->kern_cache, &probe);

    return kerning;
}
#endif

float font_get_kerning(FontHandle* handle, uint32_t left, uint32_t right) {
    if (!handle) return 0;

    // FontTables kern table
    if (handle->tables) {
        KernTable* kern = font_tables_get_kern(handle->tables);
        if (kern) {
            CmapTable* cmap = font_tables_get_cmap(handle->tables);
            if (cmap) {
                uint16_t li = cmap_lookup(cmap, left);
                uint16_t ri = cmap_lookup(cmap, right);
                if (li != 0 && ri != 0) {
                    int16_t val = kern_get_pair(kern, li, ri);
                    if (val != 0) {
                        HeadTable* head = font_tables_get_head(handle->tables);
                        if (head && head->units_per_em > 0) {
                            return val * handle->size_px / (float)head->units_per_em * handle->bitmap_scale;
                        }
                    }
                }
            }
        }
    }

#ifdef __APPLE__
    // For fonts with a CoreText reference, CT per-glyph advances are used for layout
    // (see font_load_glyph). Chrome (Harfbuzz) uses per-glyph advances without
    // applying CT GPOS kern on top, so skip CT kern here to match Chrome.
    if (handle->ct_font_ref) return 0.0f;
    // fallback for other fonts without kern table: CoreText GPOS kerning
    // CTFont is created at CSS_size, so the returned kerning is already CSS pixels.
    {
        return font_get_kerning_coretext(handle, left, right);
    }
#else
    return 0;
#endif
}

float font_get_kerning_by_index(FontHandle* handle, uint32_t left_index, uint32_t right_index) {
    if (!handle) return 0;

    // FontTables kern table
    if (handle->tables) {
        KernTable* kern = font_tables_get_kern(handle->tables);
        if (kern) {
            int16_t val = kern_get_pair(kern, (uint16_t)left_index, (uint16_t)right_index);
            if (val != 0) {
                HeadTable* head = font_tables_get_head(handle->tables);
                if (head && head->units_per_em > 0) {
                    return val * handle->size_px / (float)head->units_per_em * handle->bitmap_scale;
                }
            }
        }
    }

    return 0;
}

// ============================================================================
// Codepoint presence check
// ============================================================================

bool font_has_codepoint(FontHandle* handle, uint32_t codepoint) {
    if (!handle) return false;
    if (handle->tables) {
        CmapTable* cmap = font_tables_get_cmap(handle->tables);
        if (cmap) return cmap_lookup(cmap, codepoint) != 0;
    }
    return false;
}

// ============================================================================
// Glyph rendering (bitmap)
// ============================================================================

const GlyphBitmap* font_render_glyph(FontHandle* handle, uint32_t codepoint,
                                      GlyphRenderMode mode) {
    if (!handle) return NULL;
#ifndef __APPLE__
    if (!handle->ft_face) return NULL;
#endif

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

#ifdef __APPLE__
    // CoreText rasterization (sole backend on macOS)
    if (handle->ct_raster_ref) {
        float pixel_ratio = (ctx->config.pixel_ratio > 0) ? ctx->config.pixel_ratio : 1.0f;
        GlyphBitmap* bmp = font_rasterize_ct_render(handle->ct_raster_ref, codepoint,
                                                      mode, handle->bitmap_scale,
                                                      pixel_ratio, ctx->glyph_arena);
        if (bmp) {
            // insert into bitmap cache
            if (bmp_cache) {
                int max_glyphs = ctx->config.max_cached_glyphs > 0 ? ctx->config.max_cached_glyphs : 4096;
                if ((int)hashmap_count(bmp_cache) >= max_glyphs) {
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
    }
    return NULL;  // no FT fallback on macOS
}
#else

    FT_Face face = handle->ft_face;
    FT_UInt char_index = 0;
    if (handle->tables) {
        CmapTable* cmap = font_tables_get_cmap(handle->tables);
        if (cmap) char_index = (FT_UInt)cmap_lookup(cmap, codepoint);
    }
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
    bmp->bearing_x = (int)(slot->bitmap_left * handle->bitmap_scale);
    bmp->bearing_y = (int)(slot->bitmap_top  * handle->bitmap_scale);
    bmp->bitmap_scale = handle->bitmap_scale;
    bmp->mode      = mode;

    // map FreeType pixel_mode to our GlyphPixelMode enum
    switch (slot->bitmap.pixel_mode) {
        case FT_PIXEL_MODE_MONO:  bmp->pixel_mode = GLYPH_PIXEL_MONO; break;
        case FT_PIXEL_MODE_BGRA:  bmp->pixel_mode = GLYPH_PIXEL_BGRA; break;
        case FT_PIXEL_MODE_LCD:   bmp->pixel_mode = GLYPH_PIXEL_LCD;  break;
        default:                  bmp->pixel_mode = GLYPH_PIXEL_GRAY;  break;
    }

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
#endif // !__APPLE__

// ============================================================================
// font_load_glyph — glyph loading with automatic codepoint fallback
// ============================================================================

#ifndef __APPLE__
// fill the static LoadedGlyph from an FT_GlyphSlot
// bitmap_scale: for fixed-size bitmap fonts (e.g. emoji at 109ppem scaled to 16px), <1.0
static LoadedGlyph* fill_loaded_glyph_from_slot(FT_GlyphSlot slot, float bitmap_scale) {
    s_loaded_glyph.bitmap.buffer    = slot->bitmap.buffer;
    s_loaded_glyph.bitmap.width     = slot->bitmap.width;
    s_loaded_glyph.bitmap.height    = slot->bitmap.rows;
    s_loaded_glyph.bitmap.pitch     = slot->bitmap.pitch;
    s_loaded_glyph.bitmap.bearing_x = (int)(slot->bitmap_left * bitmap_scale);
    s_loaded_glyph.bitmap.bearing_y = (int)(slot->bitmap_top  * bitmap_scale);
    s_loaded_glyph.bitmap.bitmap_scale = bitmap_scale;
    s_loaded_glyph.bitmap.mode      = GLYPH_RENDER_NORMAL;
    switch (slot->bitmap.pixel_mode) {
        case FT_PIXEL_MODE_MONO: s_loaded_glyph.bitmap.pixel_mode = GLYPH_PIXEL_MONO; break;
        case FT_PIXEL_MODE_BGRA: s_loaded_glyph.bitmap.pixel_mode = GLYPH_PIXEL_BGRA; break;
        case FT_PIXEL_MODE_LCD:  s_loaded_glyph.bitmap.pixel_mode = GLYPH_PIXEL_LCD;  break;
        default:                 s_loaded_glyph.bitmap.pixel_mode = GLYPH_PIXEL_GRAY;  break;
    }
    s_loaded_glyph.advance_x = slot->advance.x / 64.0f * bitmap_scale;
    s_loaded_glyph.advance_y = slot->advance.y / 64.0f * bitmap_scale;
    return &s_loaded_glyph;
}

// try loading a glyph from a specific face with the given load flags.
// returns LoadedGlyph* on success, NULL on failure.
static LoadedGlyph* try_load_from_handle(FontHandle* h, uint32_t codepoint, FT_Int32 load_flags) {
    if (!h || !h->ft_face) return NULL;
    FT_Face face = h->ft_face;
    // use FontTables cmap for glyph index
    FT_UInt idx = 0;
    if (h->tables) {
        CmapTable* cmap = font_tables_get_cmap(h->tables);
        if (cmap) idx = (FT_UInt)cmap_lookup(cmap, codepoint);
    }
    if (idx == 0) return NULL;
    FT_Error err = FT_Load_Glyph(face, idx, load_flags);
    if (err) return NULL;
    return fill_loaded_glyph_from_slot(face->glyph, h->bitmap_scale);
}
#endif // !__APPLE__

#ifdef __APPLE__
// try loading a glyph via CoreText rasterization (macOS).
// Used for rendering passes — returns bitmap + metrics in physical pixels.
// Returns LoadedGlyph* on success, NULL on failure (falls back to FreeType).
static LoadedGlyph* try_load_from_handle_ct(FontHandle* h, uint32_t codepoint) {
    if (!h || !h->ct_raster_ref || !h->ctx) return NULL;

    float pixel_ratio = (h->ctx->config.pixel_ratio > 0)
                            ? h->ctx->config.pixel_ratio : 1.0f;

    // get metrics (advance in CSS pixels since CTFont is at CSS size)
    GlyphInfo info = {0};
    if (!font_rasterize_ct_metrics(h->ct_raster_ref, codepoint, h->bitmap_scale, &info))
        return NULL;

    // render bitmap at physical resolution
    GlyphBitmap* bmp = font_rasterize_ct_render(h->ct_raster_ref, codepoint,
                                                  GLYPH_RENDER_NORMAL, h->bitmap_scale,
                                                  pixel_ratio, h->ctx->glyph_arena);

    memset(&s_loaded_glyph, 0, sizeof(s_loaded_glyph));

    if (bmp) {
        s_loaded_glyph.bitmap = *bmp;
    } else {
        // zero-size glyph (space etc) — no bitmap
        s_loaded_glyph.bitmap.bitmap_scale = h->bitmap_scale;
    }

    // advance: CT returns CSS pixels, font_load_glyph expects physical pixels
    s_loaded_glyph.advance_x = info.advance_x * pixel_ratio;
    s_loaded_glyph.advance_y = 0;

    return &s_loaded_glyph;
}
#endif

// fill font metrics fields on the loaded glyph from the given handle
static void fill_loaded_glyph_font_metrics(FontHandle* h) {
    if (!h) return;
    float cell_height = font_get_cell_height(h);
    float normal_lh = font_calc_normal_line_height(h);
    s_loaded_glyph.font_cell_height = cell_height;
    s_loaded_glyph.font_normal_line_height = normal_lh;
    // hhea metrics for explicit line-height half-leading calculations
    const FontMetrics* m = font_get_metrics(h);
    if (m) {
        s_loaded_glyph.font_ascender = m->hhea_ascender;
        s_loaded_glyph.font_descender = -(m->hhea_descender);
    }
    // platform-aware split for line-height:normal (matches browser behavior)
    float split_asc = 0, split_desc = 0;
    font_get_normal_lh_split(h, &split_asc, &split_desc);
    s_loaded_glyph.font_normal_ascender = split_asc;
    s_loaded_glyph.font_normal_descender = split_desc;
}

LoadedGlyph* font_load_glyph(FontHandle* handle, const FontStyleDesc* style,
                              uint32_t codepoint, bool for_rendering) {
    if (!handle) return NULL;
#ifndef __APPLE__
    if (!handle->ft_face) return NULL;
#endif

    // Phase 17: check loaded glyph cache before loading
    FontContext* ctx = handle->ctx;
    if (ctx) {
        struct hashmap* lgcache = ctx->loaded_glyph_cache;
        if (lgcache) {
            LoadedGlyphCacheEntry search;
            search.caller_handle = handle;
            search.codepoint = codepoint;
            search.for_rendering = for_rendering;
            LoadedGlyphCacheEntry* cached = (LoadedGlyphCacheEntry*)hashmap_get(lgcache, &search);
            if (cached) {
                s_loaded_glyph = cached->glyph;
                return &s_loaded_glyph;
            }
        }
    }

    LoadedGlyph* result = NULL;

#ifdef __APPLE__
    // macOS: CoreText is the sole rasterizer
    if (handle->ct_raster_ref) {
        result = try_load_from_handle_ct(handle, codepoint);
        if (result) {
            fill_loaded_glyph_font_metrics(handle);
            // override advance with ct_font_ref advance for consistency
            if (handle->ct_font_ref) {
                float pixel_ratio = (ctx && ctx->config.pixel_ratio > 0)
                                        ? ctx->config.pixel_ratio : 1.0f;
                float ct_adv = font_platform_get_glyph_advance(handle->ct_font_ref, codepoint);
                if (ct_adv >= 0.0f) {
                    s_loaded_glyph.advance_x = ct_adv * pixel_ratio;
                }
            }
            if (ctx) cache_loaded_glyph(ctx, handle, codepoint, for_rendering);
            return result;
        }
    }
#else
    // Non-macOS: FreeType primary path
    FT_Int32 load_flags = for_rendering
        ? (FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL | FT_LOAD_COLOR)
        : (FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING | FT_LOAD_COLOR);

    result = try_load_from_handle(handle, codepoint, load_flags);
    if (result) {
        fill_loaded_glyph_font_metrics(handle);
        if (ctx) cache_loaded_glyph(ctx, handle, codepoint, for_rendering);
        return result;
    }
#endif

    // step 2: try codepoint fallback via FontContext
    if (!ctx || !style) return NULL;

    FontHandle* fallback = font_find_codepoint_fallback(ctx, style, codepoint);
    if (fallback) {
#ifdef __APPLE__
        if (fallback->ct_raster_ref) {
            result = try_load_from_handle_ct(fallback, codepoint);
        }
#else
        result = try_load_from_handle(fallback, codepoint, load_flags);
#endif
        if (result) {
            // populate metrics from the fallback font, not the primary
            fill_loaded_glyph_font_metrics(fallback);
            // Phase 17: cache with caller's handle so subsequent lookups hit
            cache_loaded_glyph(ctx, handle, codepoint, for_rendering);
        }
        font_handle_release(fallback); // release the ref from font_find_codepoint_fallback
        if (result) return result;
    }

    return NULL;
}

// ============================================================================
// Text measurement
// ============================================================================

/**
 * Check if a codepoint is an emoji that participates in ZWJ composition.
 * Only emoji characters form composed glyphs when joined by ZWJ.
 */
static inline bool is_emoji_for_zwj(uint32_t cp) {
    return (cp >= 0x1F000 && cp <= 0x1FFFF) ||  // SMP emoji blocks
           (cp >= 0x2600 && cp <= 0x27BF)  ||    // Misc Symbols and Dingbats
           (cp >= 0x2300 && cp <= 0x23FF)  ||    // Misc Technical
           (cp >= 0x2B00 && cp <= 0x2BFF)  ||    // Misc Symbols and Arrows
           cp == 0x200D || cp == 0x2764;
}

/**
 * Check if a codepoint can serve as the base (left side) of a ZWJ emoji
 * composition sequence. (Unicode UTS #51, emoji-zwj-sequences.txt)
 */
static inline bool is_zwj_composition_base(uint32_t cp) {
    return (cp >= 0x1F466 && cp <= 0x1F469) ||  // Boy, Girl, Man, Woman
           cp == 0x1F9D1 ||                       // Person (gender-neutral)
           cp == 0x1F441 ||                       // Eye
           (cp >= 0x1F3F3 && cp <= 0x1F3F4) ||   // Flags
           cp == 0x1F408 || cp == 0x1F415 ||      // Cat, Dog
           cp == 0x1F43B || cp == 0x1F426 ||      // Bear, Bird
           cp == 0x1F48B || cp == 0x2764;          // Kiss Mark, Heart
}

TextExtents font_measure_text(FontHandle* handle, const char* text, int byte_len) {
    TextExtents ext = {0};
    if (!handle || !text || byte_len <= 0) return ext;
    size_t len = (size_t)byte_len;

    const FontMetrics* metrics = font_get_metrics(handle);
    if (metrics) {
        ext.height = metrics->ascender - metrics->descender;
    }

    uint32_t prev_codepoint = 0;
    bool after_zwj = false;
    bool prev_is_zwj_base = false;
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

        // ZWJ (U+200D): zero advance, trigger composition only if preceded by a valid base
        if (cp == 0x200D) {
            if (prev_is_zwj_base) after_zwj = true;
            continue;
        }

        // Emoji combining marks: zero advance in composed sequences
        if ((cp >= 0x1F3FB && cp <= 0x1F3FF) ||  // skin tone modifiers
            cp == 0xFE0E || cp == 0xFE0F ||       // variation selectors
            cp == 0x20E3) {                        // combining enclosing keycap
            continue;
        }

        // Character after ZWJ: only suppress advance for emoji codepoints
        // that form composed glyphs in ZWJ sequences (Unicode UTS #51).
        if (after_zwj) {
            after_zwj = false;
            if (is_emoji_for_zwj(cp)) {
                prev_is_zwj_base = is_zwj_composition_base(cp);
                prev_codepoint = cp;
                continue;
            }
        }

        prev_is_zwj_base = is_zwj_composition_base(cp);

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
