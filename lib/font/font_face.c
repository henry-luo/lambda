/**
 * Lambda Unified Font Module — @font-face Descriptor Registry
 *
 * Manages registered @font-face descriptors:
 *   register, find (best-match), list, load, clear.
 *
 * CSS @font-face rule parsing stays in Radiant; this module only
 * stores and queries the descriptors.
 *
 * Copyright (c) 2025 Lambda Script Project
 */

#include "font_internal.h"
#include "../str.h"

// ============================================================================
// Register
// ============================================================================

bool font_face_register(FontContext* ctx, const FontFaceDesc* desc) {
    if (!ctx || !desc || !desc->family) return false;

    // grow the array if needed
    if (ctx->face_descriptor_count >= ctx->face_descriptor_capacity) {
        int new_cap = (ctx->face_descriptor_capacity == 0) ? 8
                          : ctx->face_descriptor_capacity * 2;
        FontFaceEntry** new_arr = (FontFaceEntry**)pool_calloc(
            ctx->pool, (size_t)new_cap * sizeof(FontFaceEntry*));
        if (!new_arr) return false;

        if (ctx->face_descriptors && ctx->face_descriptor_count > 0) {
            memcpy(new_arr, ctx->face_descriptors,
                   (size_t)ctx->face_descriptor_count * sizeof(FontFaceEntry*));
        }
        // old array is pool-allocated, will be freed with pool
        ctx->face_descriptors = new_arr;
        ctx->face_descriptor_capacity = new_cap;
    }

    // allocate entry
    FontFaceEntry* entry = (FontFaceEntry*)pool_calloc(ctx->pool, sizeof(FontFaceEntry));
    if (!entry) return false;

    // copy family name
    entry->family = arena_strdup(ctx->arena, desc->family);
    entry->weight = desc->weight;
    entry->slant  = desc->slant;
    entry->loaded_handle = NULL;

    // copy sources
    if (desc->source_count > 0 && desc->sources) {
        entry->sources = (struct FontFaceEntrySrc*)pool_calloc(
            ctx->pool, (size_t)desc->source_count * sizeof(struct FontFaceEntrySrc));
        if (!entry->sources) return false;

        entry->source_count = desc->source_count;
        for (int i = 0; i < desc->source_count; i++) {
            if (desc->sources[i].path) {
                entry->sources[i].path = arena_strdup(ctx->arena, desc->sources[i].path);
            }
            if (desc->sources[i].format) {
                entry->sources[i].format = arena_strdup(ctx->arena, desc->sources[i].format);
            }
        }
    }

    ctx->face_descriptors[ctx->face_descriptor_count++] = entry;

    log_info("font_face: registered '%s' (weight=%d, slant=%d, sources=%d)",
             desc->family, (int)desc->weight, (int)desc->slant, desc->source_count);
    return true;
}

// ============================================================================
// Weight distance scoring for CSS font matching
// ============================================================================

static int weight_distance(FontWeight a, FontWeight b) {
    int d = abs((int)a - (int)b);
    return d;
}

static int slant_distance(FontSlant a, FontSlant b) {
    if (a == b) return 0;
    return 100; // penalty for slant mismatch
}

// ============================================================================
// Find best-matching registered descriptor
// ============================================================================

const FontFaceEntry* font_face_find_internal(FontContext* ctx, const char* family,
                                              FontWeight weight, FontSlant slant) {
    if (!ctx || !family) return NULL;

    const FontFaceEntry* best = NULL;
    int best_score = INT32_MAX;

    for (int i = 0; i < ctx->face_descriptor_count; i++) {
        FontFaceEntry* entry = ctx->face_descriptors[i];
        if (!entry || !entry->family) continue;

        // family must match (case-insensitive)
        if (str_icmp(entry->family, strlen(entry->family), family, strlen(family)) != 0) continue;

        // compute distance (lower is better)
        int score = weight_distance(entry->weight, weight) +
                    slant_distance(entry->slant, slant);
        if (score < best_score) {
            best_score = score;
            best = entry;
        }
    }

    return best;
}

// ============================================================================
// Public: find (returns FontFaceDesc view of internal entry)
// ============================================================================

// we return a pointer into a static FontFaceDesc that mirrors the internal entry;
// the caller should not modify or free it.  It's valid until the next call
// to font_face_find.

const FontFaceDesc* font_face_find(FontContext* ctx, const FontStyleDesc* style) {
    if (!ctx || !style) return NULL;

    const FontFaceEntry* entry = font_face_find_internal(
        ctx, style->family, style->weight, style->slant);
    if (!entry) return NULL;

    // build a public-facing FontFaceDesc that aliases the internal data
    // (thread-local static for simplicity; Radiant is single-threaded)
    static FontFaceDesc desc;
    static FontFaceSource srcs[16]; // max 16 sources

    desc.family = entry->family;
    desc.weight = entry->weight;
    desc.slant  = entry->slant;

    int n = entry->source_count;
    if (n > 16) n = 16;
    for (int i = 0; i < n; i++) {
        srcs[i].path   = entry->sources[i].path;
        srcs[i].format = entry->sources[i].format;
    }
    desc.sources = srcs;
    desc.source_count = n;

    return &desc;
}

// ============================================================================
// List all descriptors for a family
// ============================================================================

int font_face_list(FontContext* ctx, const char* family,
                   const FontFaceDesc** out, int max_out) {
    if (!ctx || !family || !out || max_out <= 0) return 0;

    // reuse a small static array for returning public-facing descriptors
    static FontFaceDesc descs[64];
    static FontFaceSource srcs_pool[64][16];
    int count = 0;

    for (int i = 0; i < ctx->face_descriptor_count && count < max_out && count < 64; i++) {
        FontFaceEntry* entry = ctx->face_descriptors[i];
        if (!entry || !entry->family) continue;
        if (str_icmp(entry->family, strlen(entry->family), family, strlen(family)) != 0) continue;

        FontFaceDesc* d = &descs[count];
        d->family = entry->family;
        d->weight = entry->weight;
        d->slant  = entry->slant;

        int n = entry->source_count;
        if (n > 16) n = 16;
        for (int j = 0; j < n; j++) {
            srcs_pool[count][j].path   = entry->sources[j].path;
            srcs_pool[count][j].format = entry->sources[j].format;
        }
        d->sources = srcs_pool[count];
        d->source_count = n;

        out[count] = d;
        count++;
    }

    return count;
}

// ============================================================================
// Load a font from a registered descriptor (tries sources in order)
// ============================================================================

FontHandle* font_face_load(FontContext* ctx, const FontFaceDesc* desc,
                            float size_px) {
    if (!ctx || !desc) return NULL;

    // find the internal entry to check if already loaded
    const FontFaceEntry* entry = font_face_find_internal(
        ctx, desc->family, desc->weight, desc->slant);

    // return cached handle if already loaded at this size
    if (entry && entry->loaded_handle &&
        entry->loaded_handle->size_px == size_px) {
        font_handle_retain(entry->loaded_handle);
        return entry->loaded_handle;
    }

    float pixel_ratio = ctx->config.pixel_ratio;
    float physical_size = size_px * pixel_ratio;

    // try each source in order
    for (int i = 0; i < desc->source_count; i++) {
        const char* src_path = desc->sources[i].path;
        if (!src_path) continue;

        FontHandle* handle = NULL;

        // check if it's a data URI
        if (strncmp(src_path, "data:", 5) == 0) {
            FontStyleDesc style = {
                .family = desc->family,
                .size_px = size_px,
                .weight = desc->weight,
                .slant = desc->slant,
            };
            handle = font_load_from_data_uri(ctx, src_path, &style);
        } else {
            // local file path
            handle = font_load_face_internal(ctx, src_path, 0,
                                              size_px, physical_size,
                                              desc->weight, desc->slant);
        }

        if (handle) {
            // cache in entry for future loads (mutable cast is safe here)
            if (entry) {
                ((FontFaceEntry*)entry)->loaded_handle = handle;
                font_handle_retain(handle); // entry holds a ref too
            }
            log_info("font_face: loaded '%s' from source %d: %s",
                     desc->family, i, src_path);
            return handle;
        }

        log_debug("font_face: source %d failed for '%s': %s", i, desc->family, src_path);
    }

    log_error("font_face: all sources failed for '%s'", desc->family);
    return NULL;
}

// ============================================================================
// Clear all registered descriptors
// ============================================================================

void font_face_clear(FontContext* ctx) {
    if (!ctx) return;

    for (int i = 0; i < ctx->face_descriptor_count; i++) {
        FontFaceEntry* entry = ctx->face_descriptors[i];
        if (!entry) continue;

        // release loaded handle if any
        if (entry->loaded_handle) {
            font_handle_release(entry->loaded_handle);
            entry->loaded_handle = NULL;
        }
        // entry/strings are pool/arena-allocated, freed on destroy
    }

    ctx->face_descriptor_count = 0;
    log_info("font_face: cleared all descriptors");
}
