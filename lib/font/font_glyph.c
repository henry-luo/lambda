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
#include "font_gpos.h"

#include "../memtrack.h"
#include <math.h>

// ============================================================================
// Glyph advance cache (per-FontHandle hashmap)
// ============================================================================

// maximum entries per-handle advance cache before eviction
#define ADVANCE_CACHE_MAX_ENTRIES 4096

#ifdef __APPLE__
static bool should_use_ct_advance_override(FontHandle* handle) {
    if (!handle || !handle->ct_font_ref) return false;
    // document fonts are created from exact @font-face data. A separate
    // CoreText catalog lookup can resolve to a fallback/non-exact face, so keep
    // the raster/table advance from the loaded font file for layout.
    if (handle->is_document_font) return false;

    // system color bitmap fonts can expose different advances when CoreText is
    // created from raw font bytes than when the same font is resolved through
    // the platform catalog. browser font matching uses the catalog font.
    if (handle->ct_raster_ref && font_platform_has_color_glyphs(handle->ct_font_ref)) {
        return true;
    }

    if (!handle->ct_raster_ref) return true;

    // system UI fonts rely on CoreText's catalog font so variable optical-size
    // selection matches browser layout.
    if (handle->family_name && strcmp(handle->family_name, "System Font") == 0) {
        return true;
    }

    // for an exact loaded face at the requested style, the raw-data CoreText
    // face has the correct advances. Use the catalog face only when CSS asks
    // for a different weight/style and CoreText is needed to match that face.
    int actual_weight = handle->actual_font_weight;
    if (actual_weight > 0 && actual_weight != (int)handle->weight) return true;
    if (handle->slant == FONT_SLANT_ITALIC || handle->slant == FONT_SLANT_OBLIQUE) return true;

    return false;
}
#endif

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
    data[1] = ((uint64_t)entry->codepoint << 2) |
              ((uint64_t)(entry->for_rendering ? 1 : 0) << 1) |
              (uint64_t)(entry->emoji_presentation ? 1 : 0);
    return hashmap_xxhash3(data, sizeof(data), seed0, seed1);
}

static int loaded_glyph_cache_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const LoadedGlyphCacheEntry* ea = (const LoadedGlyphCacheEntry*)a;
    const LoadedGlyphCacheEntry* eb = (const LoadedGlyphCacheEntry*)b;
    if (ea->caller_handle != eb->caller_handle) return ea->caller_handle < eb->caller_handle ? -1 : 1;
    if (ea->codepoint != eb->codepoint) return ea->codepoint < eb->codepoint ? -1 : 1;
    if (ea->for_rendering != eb->for_rendering) return ea->for_rendering ? 1 : -1;
    if (ea->emoji_presentation != eb->emoji_presentation) return ea->emoji_presentation ? 1 : -1;
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
                                uint32_t codepoint, bool for_rendering,
                                bool emoji_presentation) {
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
    entry.emoji_presentation = emoji_presentation;
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

    // Font5 §4.2: fast path for ASCII codepoints (32–126)
    // Direct array lookup — no hash computation needed.
    if (codepoint >= 32 && codepoint <= 126 && handle->ascii_advance_ready) {
        uint32_t idx = codepoint - 32;
        if (handle->ascii_glyph_id[idx] != 0) {
            info.id = handle->ascii_glyph_id[idx];
            info.advance_x = handle->ascii_advance[idx];
            return info;
        }
    }

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

    {
        GlyphInfo backend_info = {0};
        if (font_backend_metrics(handle, codepoint, &backend_info)) {
            if (backend_info.id == 0) backend_info.id = char_index;
            info = backend_info;
            goto apply_overrides;
        }
    }

#if !defined(__APPLE__)
    // primary (Linux): ThorVG metrics
    if (handle->tvg_raster_ctx && handle->tables) {
        GlyphInfo tvg_info = {0};
        if (font_rasterize_tvg_metrics(handle->tables, codepoint, handle->size_px,
                                        handle->bitmap_scale, &tvg_info)) {
            tvg_info.id = char_index;
            info = tvg_info;
            goto apply_overrides;
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
    // CoreText advances instead of table-only metrics. We create the CTFont at CSS_size so
    // CoreText selects the same optical-size instance as Chrome.  The returned
    // advance is already in CSS pixels - no pixel_ratio division needed.
    if (should_use_ct_advance_override(handle)) {
        float table_adv = info.advance_x;
        float ct_adv = font_platform_get_glyph_advance(handle->ct_font_ref, codepoint);
        if (ct_adv >= 0.0f) {
            info.advance_x = ct_adv;
            // one-time log: show FT vs CT for letter 't' to confirm fix works
            if (codepoint == 't') {
                static bool logged_t = false;
                if (!logged_t) {
                    logged_t = true;
                    log_debug("font_glyph: CT advance fix active: 't' table=%.3f ct=%.3f family=%s size=%.0fpx",
                              table_adv, ct_adv,
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

    // Font5 §4.2: populate ASCII advance table entry
    if (codepoint >= 32 && codepoint <= 126 && char_index != 0) {
        uint32_t idx = codepoint - 32;
        handle->ascii_advance[idx] = info.advance_x;
        handle->ascii_glyph_id[idx] = char_index;
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

    // Font5 §4.5: skip kerning entirely for fonts with no kern data
    const FontMetrics* m = font_get_metrics(handle);
    if (m && !m->has_kerning) return 0.0f;

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

        // GPOS PairPos kerning (fallback when kern table has no pair)
        GposTable* gpos = font_tables_get_gpos(handle->tables);
        if (gpos) {
            CmapTable* cmap = font_tables_get_cmap(handle->tables);
            if (cmap) {
                uint16_t li = cmap_lookup(cmap, left);
                uint16_t ri = cmap_lookup(cmap, right);
                if (li != 0 && ri != 0) {
                    int16_t val = gpos_get_kern(gpos, li, ri);
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

        // GPOS PairPos kerning (fallback when kern table has no pair)
        GposTable* gpos = font_tables_get_gpos(handle->tables);
        if (gpos) {
            int16_t val = gpos_get_kern(gpos, (uint16_t)left_index, (uint16_t)right_index);
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

float font_get_halt_adjustment(FontHandle* handle, uint32_t codepoint) {
    if (!handle || !handle->tables) return 0.0f;

    GposTable* gpos = font_tables_get_gpos(handle->tables);
    if (!gpos || !gpos_has_halt_adjustment(gpos)) return 0.0f;

    CmapTable* cmap = font_tables_get_cmap(handle->tables);
    if (!cmap) return 0.0f;

    uint16_t glyph_id = cmap_lookup(cmap, codepoint);
    if (glyph_id == 0) return 0.0f;

    int16_t val = gpos_get_halt_adjustment(gpos, glyph_id);
    if (val == 0) return 0.0f;

    HeadTable* head = font_tables_get_head(handle->tables);
    if (!head || head->units_per_em <= 0) return 0.0f;

    return val * handle->size_px / (float)head->units_per_em * handle->bitmap_scale;
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

    FontContext* ctx = handle->ctx;
    if (!ctx) return NULL;

    font_context_enforce_glyph_arena_limit(ctx);

    // check bitmap cache first
    struct hashmap* bmp_cache = ensure_bitmap_cache(ctx);
    if (bmp_cache) {
        BitmapCacheEntry search = {.codepoint = codepoint, .mode = mode, .handle = handle};
        BitmapCacheEntry* cached = (BitmapCacheEntry*)hashmap_get(bmp_cache, &search);
        if (cached && cached->bitmap.buffer) {
            return &cached->bitmap;
        }
    }

    GlyphBitmap* backend_bmp = font_backend_render(handle, codepoint, mode, ctx->glyph_arena);
    if (backend_bmp && (backend_bmp->buffer || backend_bmp->width == 0)) {
        if (bmp_cache) {
            int max_glyphs = ctx->config.max_cached_glyphs > 0 ? ctx->config.max_cached_glyphs : 4096;
            if ((int)hashmap_count(bmp_cache) >= max_glyphs) {
                hashmap_clear(bmp_cache, false);
            }
            BitmapCacheEntry entry = {
                .codepoint = codepoint,
                .mode      = mode,
                .handle    = handle,
                .bitmap    = *backend_bmp,
            };
            hashmap_set(bmp_cache, &entry);
        }
        return backend_bmp;
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

    // ThorVG rasterization path (preferred on Linux/WASM)
    if (handle->tvg_raster_ctx && handle->tables) {
        float pixel_ratio = (ctx->config.pixel_ratio > 0) ? ctx->config.pixel_ratio : 1.0f;
        // synthetic bold: apply stroke when requested weight is significantly heavier
        // than the actual font file's weight (e.g. CSS bold=700 but only Regular=400 available)
        float synth_bold_stroke = 0.0f;
        int req_w = (int)handle->weight;
        int act_w = (handle->actual_font_weight > 0) ? handle->actual_font_weight : req_w;
        if (req_w >= 600 && act_w < 550) {
            // stroke width ≈ size_px/14 ≈ same order as Chrome's synthetic bold
            synth_bold_stroke = handle->size_px / 14.0f * pixel_ratio;
            log_debug("font_glyph: synthetic bold stroke=%.2f (req=%d act=%d) for cp=%u",
                      synth_bold_stroke, req_w, act_w, codepoint);
        }
        GlyphBitmap* bmp = font_rasterize_tvg_render(handle->tvg_raster_ctx, handle->tables,
                                                      codepoint, handle->size_px,
                                                      handle->bitmap_scale, pixel_ratio,
                                                      synth_bold_stroke,
                                                      ctx->glyph_arena);
        if (bmp && (bmp->buffer || bmp->width == 0)) {
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

    return NULL;
}
#endif // !__APPLE__

// ============================================================================
// font_load_glyph — glyph loading with automatic codepoint fallback
// ============================================================================

#ifndef __APPLE__
// try loading a glyph via ThorVG rasterization (Linux/WASM).
// Returns LoadedGlyph* on success, NULL on failure.
static LoadedGlyph* try_load_from_handle_tvg(FontHandle* h, uint32_t codepoint) {
    if (!h || !h->tvg_raster_ctx || !h->tables || !h->ctx) return NULL;

    float pixel_ratio = (h->ctx->config.pixel_ratio > 0)
                            ? h->ctx->config.pixel_ratio : 1.0f;

    // get metrics (advance in CSS pixels)
    GlyphInfo info = {0};
    if (!font_rasterize_tvg_metrics(h->tables, codepoint, h->size_px, h->bitmap_scale, &info))
        return NULL;

    // render bitmap at physical resolution
    GlyphBitmap* bmp = font_rasterize_tvg_render(h->tvg_raster_ctx, h->tables, codepoint,
                                                   h->size_px, h->bitmap_scale,
                                                   pixel_ratio, 0.0f, h->ctx->glyph_arena);

    memset(&s_loaded_glyph, 0, sizeof(s_loaded_glyph));

    if (bmp) {
        s_loaded_glyph.bitmap = *bmp;
    } else {
        // zero-size glyph (space etc) — no bitmap
        s_loaded_glyph.bitmap.bitmap_scale = h->bitmap_scale;
    }

    // advance: tvg_metrics returns CSS pixels, font_load_glyph expects physical pixels
    s_loaded_glyph.advance_x = info.advance_x * pixel_ratio;
    s_loaded_glyph.advance_y = 0;

    return &s_loaded_glyph;
}
#endif // !__APPLE__

#ifdef __APPLE__
// try loading a glyph via CoreText rasterization (macOS).
// Used for rendering passes — returns bitmap + metrics in physical pixels.
// Returns LoadedGlyph* on success, NULL on failure.
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
    if (should_use_ct_advance_override(h)) {
        float ct_adv = font_platform_get_glyph_advance(h->ct_font_ref, codepoint);
        if (ct_adv >= 0.0f) {
            s_loaded_glyph.advance_x = ct_adv * pixel_ratio;
        }
    }

    return &s_loaded_glyph;
}
#endif

static LoadedGlyph* try_load_from_handle_backend(FontHandle* h, uint32_t codepoint) {
    if (!h || !h->ctx) return NULL;

    GlyphInfo info = {0};
    if (!font_backend_metrics(h, codepoint, &info)) return NULL;

    float pixel_ratio = (h->ctx->config.pixel_ratio > 0)
                            ? h->ctx->config.pixel_ratio : 1.0f;
    GlyphBitmap* bmp = font_backend_render(h, codepoint, GLYPH_RENDER_NORMAL,
                                           h->ctx->glyph_arena);

    memset(&s_loaded_glyph, 0, sizeof(s_loaded_glyph));
    if (bmp) {
        s_loaded_glyph.bitmap = *bmp;
    } else {
        s_loaded_glyph.bitmap.bitmap_scale = h->bitmap_scale;
    }
    s_loaded_glyph.advance_x = info.advance_x * pixel_ratio;
    s_loaded_glyph.advance_y = info.advance_y * pixel_ratio;
    return &s_loaded_glyph;
}

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
    if (handle->backend_kind == FONT_BACKEND_NONE && !handle->tables) return NULL;

    // Phase 17: check loaded glyph cache before loading
    FontContext* ctx = handle->ctx;
    if (ctx) {
        font_context_enforce_glyph_arena_limit(ctx);

        struct hashmap* lgcache = ctx->loaded_glyph_cache;
        if (lgcache) {
            LoadedGlyphCacheEntry search;
            search.caller_handle = handle;
            search.codepoint = codepoint;
            search.for_rendering = for_rendering;
            search.emoji_presentation = false;
            LoadedGlyphCacheEntry* cached = (LoadedGlyphCacheEntry*)hashmap_get(lgcache, &search);
            if (cached) {
                s_loaded_glyph = cached->glyph;
                return &s_loaded_glyph;
            }
        }
    }

    LoadedGlyph* result = NULL;

    result = try_load_from_handle_backend(handle, codepoint);
    if (result) {
        fill_loaded_glyph_font_metrics(handle);
#ifdef __APPLE__
        // The backend raster font can be the concrete file that was loaded
        // first (for example Arial Regular), while ct_font_ref may point to
        // the CSS-matched face for layout advances (for example Arial Bold
        // for font-weight:600). Keep bitmap rendering and line metrics from
        // the loaded face, but use the CSS-matched CoreText advance for layout.
        if (should_use_ct_advance_override(handle)) {
            float pixel_ratio = (ctx && ctx->config.pixel_ratio > 0)
                                    ? ctx->config.pixel_ratio : 1.0f;
            float ct_adv = font_platform_get_glyph_advance(handle->ct_font_ref, codepoint);
            if (ct_adv >= 0.0f) {
                s_loaded_glyph.advance_x = ct_adv * pixel_ratio;
            }
        }
#endif
        if (ctx) cache_loaded_glyph(ctx, handle, codepoint, for_rendering, false);
        return result;
    }

#ifdef __APPLE__
    // macOS: CoreText is the sole rasterizer
    if (handle->ct_raster_ref) {
        result = try_load_from_handle_ct(handle, codepoint);
        if (result) {
            fill_loaded_glyph_font_metrics(handle);
            // override advance with ct_font_ref advance for consistency
            if (should_use_ct_advance_override(handle)) {
                float pixel_ratio = (ctx && ctx->config.pixel_ratio > 0)
                                        ? ctx->config.pixel_ratio : 1.0f;
                float ct_adv = font_platform_get_glyph_advance(handle->ct_font_ref, codepoint);
                if (ct_adv >= 0.0f) {
                    s_loaded_glyph.advance_x = ct_adv * pixel_ratio;
                }
            }
            if (ctx) cache_loaded_glyph(ctx, handle, codepoint, for_rendering, false);
            return result;
        }
    }
#else
    // Non-macOS: ThorVG primary path
    if (handle->tvg_raster_ctx && handle->tables) {
        result = try_load_from_handle_tvg(handle, codepoint);
        if (result) {
            fill_loaded_glyph_font_metrics(handle);
            if (ctx) cache_loaded_glyph(ctx, handle, codepoint, for_rendering, false);
            return result;
        }
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
        if (fallback->tvg_raster_ctx && fallback->tables) {
            result = try_load_from_handle_tvg(fallback, codepoint);
        }
#endif
        if (result) {
            // populate metrics from the fallback font, not the primary
            fill_loaded_glyph_font_metrics(fallback);
            // Phase 17: cache with caller's handle so subsequent lookups hit
            cache_loaded_glyph(ctx, handle, codepoint, for_rendering, false);
        }
        font_handle_release(fallback); // release the ref from font_find_codepoint_fallback
        if (result) return result;
    }

    return NULL;
}

LoadedGlyph* font_load_glyph_emoji(FontHandle* handle, const FontStyleDesc* style,
                                    uint32_t codepoint, bool for_rendering) {
    if (!handle || !style) return NULL;
    FontContext* ctx = handle->ctx;
    if (!ctx) return font_load_glyph(handle, style, codepoint, for_rendering);

    font_context_enforce_glyph_arena_limit(ctx);

    // check loaded glyph cache before expensive emoji font lookup
    struct hashmap* lgcache = ctx->loaded_glyph_cache;
    if (lgcache) {
        LoadedGlyphCacheEntry search;
        search.caller_handle = handle;
        search.codepoint = codepoint;
        search.for_rendering = for_rendering;
        search.emoji_presentation = true;
        LoadedGlyphCacheEntry* cached = (LoadedGlyphCacheEntry*)hashmap_get(lgcache, &search);
        if (cached) {
            s_loaded_glyph = cached->glyph;
            return &s_loaded_glyph;
        }
    }

    // Use platform emoji font lookup: passes codepoint + VS16 (U+FE0F) to
    // CoreText so it selects Apple Color Emoji instead of a CJK text font.
    float pixel_ratio = ctx->config.pixel_ratio;
    float physical_size = style->size_px * pixel_ratio;

    // Reuse cached emoji handle if style matches (same font for all emoji)
    FontHandle* emoji_handle = NULL;
    bool handle_from_cache = false;
    if (ctx->cached_emoji_handle
        && ctx->cached_emoji_size_px == style->size_px
        && ctx->cached_emoji_physical_size == physical_size
        && ctx->cached_emoji_weight == style->weight
        && ctx->cached_emoji_slant == style->slant) {
        emoji_handle = ctx->cached_emoji_handle;
        handle_from_cache = true;
    } else {
        int face_index = 0;
        char* font_path = font_platform_find_emoji_font(codepoint, &face_index);
        if (font_path) {
            emoji_handle = font_load_face_internal(
                ctx, font_path, face_index,
                style->size_px, physical_size,
                style->weight, style->slant);
            mem_free(font_path);
            if (emoji_handle) {
                // release old cached handle and store new one
                if (ctx->cached_emoji_handle)
                    font_handle_release(ctx->cached_emoji_handle);
                font_handle_retain(emoji_handle);
                ctx->cached_emoji_handle = emoji_handle;
                ctx->cached_emoji_size_px = style->size_px;
                ctx->cached_emoji_physical_size = physical_size;
                ctx->cached_emoji_weight = style->weight;
                ctx->cached_emoji_slant = style->slant;
            }
        }
    }

    if (emoji_handle) {
        LoadedGlyph* result = NULL;
#ifdef __APPLE__
        if (emoji_handle->ct_raster_ref) {
            result = try_load_from_handle_ct(emoji_handle, codepoint);
        }
#else
        // ThorVG primary
        if (emoji_handle->tvg_raster_ctx && emoji_handle->tables) {
            result = try_load_from_handle_tvg(emoji_handle, codepoint);
        }
#endif
        if (result) {
            fill_loaded_glyph_font_metrics(emoji_handle);
        }
        if (!handle_from_cache)
            font_handle_release(emoji_handle);
        if (result) {
            // cache with caller's handle so subsequent lookups hit
            cache_loaded_glyph(ctx, handle, codepoint, for_rendering, true);
            return result;
        }
    }

    // If emoji font lookup didn't work, fall back to normal loading
    return font_load_glyph(handle, style, codepoint, for_rendering);
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

#ifdef __APPLE__
    void* ct_font_ref = NULL;
    if (should_use_ct_advance_override(handle)) {
        ct_font_ref = handle->ct_font_ref;
    } else if (handle->ct_raster_ref) {
        ct_font_ref = handle->ct_raster_ref;
    } else {
        ct_font_ref = handle->ct_font_ref;
    }
    if (ct_font_ref) {
        TextExtents shaped = font_platform_measure_text(ct_font_ref, text, byte_len, ext.height);
        if (shaped.glyph_count > 0 || shaped.width > 0.0f) {
            return shaped;
        }
    }
#endif

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
