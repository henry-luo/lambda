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

    if (desc->unicode_range_count > 0 && desc->unicode_ranges) {
        entry->unicode_ranges = (FontFaceUnicodeRange*)pool_calloc(
            ctx->pool, (size_t)desc->unicode_range_count * sizeof(FontFaceUnicodeRange));
        if (!entry->unicode_ranges) return false;
        memcpy(entry->unicode_ranges, desc->unicode_ranges,
               (size_t)desc->unicode_range_count * sizeof(FontFaceUnicodeRange));
        entry->unicode_range_count = desc->unicode_range_count;
    }

    ctx->face_descriptors[ctx->face_descriptor_count++] = entry;

    // Retain distinct rules: unicode-range and source order select a face per glyph.
    log_info("font_face: registered '%s' (weight=%d, slant=%d, sources=%d, ranges=%d)",
             desc->family, (int)desc->weight, (int)desc->slant, desc->source_count,
             desc->unicode_range_count);
    return true;
}

static int slant_distance(FontSlant a, FontSlant b) {
    if (a == b) return 0;
    return 100; // penalty for slant mismatch
}

static bool font_face_range_includes(const FontFaceEntry* entry, uint32_t codepoint) {
    if (!entry || entry->unicode_range_count == 0 || !entry->unicode_ranges) return true;
    for (int i = 0; i < entry->unicode_range_count; i++) {
        FontFaceUnicodeRange range = entry->unicode_ranges[i];
        if (codepoint >= range.start_codepoint && codepoint <= range.end_codepoint) return true;
    }
    return false;
}

static const FontFaceEntry* font_face_find_matching(FontContext* ctx, const char* family,
                                                     FontWeight weight, FontSlant slant,
                                                     bool filter_codepoint, uint32_t codepoint) {
    if (!ctx || !family) return NULL;

    const FontFaceEntry* best = NULL;
    int best_slant_distance = INT32_MAX;
    // CSS resolves overlapping matching @font-face rules in reverse definition order.
    for (int i = ctx->face_descriptor_count - 1; i >= 0; i--) {
        FontFaceEntry* entry = ctx->face_descriptors[i];
        if (!entry || !entry->family) continue;
        if (str_icmp(entry->family, strlen(entry->family), family, strlen(family)) != 0) continue;
        if (filter_codepoint && !font_face_range_includes(entry, codepoint)) continue;

        int entry_slant_distance = slant_distance(entry->slant, slant);
        if (!best || entry_slant_distance < best_slant_distance ||
            (entry_slant_distance == best_slant_distance &&
             font_css_weight_is_better((int)weight, (int)entry->weight,
                                       (int)best->weight))) {
            best_slant_distance = entry_slant_distance;
            best = entry;
        }
    }
    return best;
}

// ============================================================================
// Find best-matching registered descriptor
// ============================================================================

const FontFaceEntry* font_face_find_internal(FontContext* ctx, const char* family,
                                              FontWeight weight, FontSlant slant) {
    // A FontProp needs one face before individual glyphs are known; use basic
    // Latin for its default metrics and select the exact range per glyph later.
    const FontFaceEntry* latin = font_face_find_matching(
        ctx, family, weight, slant, true, (uint32_t)'a');
    return latin ? latin : font_face_find_matching(ctx, family, weight, slant, false, 0);
}

bool font_face_family_registered(FontContext* ctx, const char* family) {
    if (!ctx || !family) return false;

    for (int i = 0; i < ctx->face_descriptor_count; i++) {
        FontFaceEntry* entry = ctx->face_descriptors[i];
        if (!entry || !entry->family) continue;
        if (str_icmp(entry->family, strlen(entry->family),
                     family, strlen(family)) == 0) {
            return true;
        }
    }
    return false;
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
    desc.unicode_ranges = entry->unicode_ranges;
    desc.unicode_range_count = entry->unicode_range_count;

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
        d->unicode_ranges = entry->unicode_ranges;
        d->unicode_range_count = entry->unicode_range_count;

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

static FontHandle* font_face_load_entry(FontContext* ctx, const FontFaceEntry* entry,
                                        float size_px) {
    if (!ctx || !entry) return NULL;

    float pixel_ratio = ctx->config.pixel_ratio;
    float physical_size = size_px * pixel_ratio;

    // A logical size can survive a monitor move, but its raster face cannot.
    if (entry && entry->loaded_handle &&
        entry->loaded_handle->size_px == size_px &&
        entry->loaded_handle->physical_size_px == physical_size) {
        font_handle_retain(entry->loaded_handle);
        return entry->loaded_handle;
    }

    // A src list is a format fallback list, not a glyph-coverage preference list.
    for (int i = 0; i < entry->source_count; i++) {
        const char* src_path = entry->sources[i].path;
        if (!src_path) continue;

        FontHandle* handle = NULL;

        // check if it's a data URI
        if (strncmp(src_path, "data:", 5) == 0) {
            FontStyleDesc style = {
                .family = entry->family,
                .size_px = size_px,
                .weight = entry->weight,
                .slant = entry->slant,
            };
            handle = font_load_from_data_uri(ctx, src_path, &style);
        } else {
            // local file path
            handle = font_load_face_internal(ctx, src_path, 0,
                                              size_px, physical_size,
                                              entry->weight, entry->slant);
        }

        if (handle) {
            // reject sources whose font tables couldn't be parsed (e.g. EOT
            // wrappers or unsupported containers). Without
            // `tables` we can't look up cmap/glyphs, so the font would
            // render as empty boxes. Try the next source instead.
            if (!handle->tables) {
                log_debug("font_face: source %d for '%s' has no parsable tables, trying next",
                          i, entry->family);
                font_handle_release(handle);
                continue;
            }

            // cache in entry for future loads (mutable cast is safe here)
            handle->is_document_font = true; // @font-face: cleared between documents
            if (entry) {
                FontFaceEntry* mutable_entry = (FontFaceEntry*)entry;
                // loaded_handle caches only one size; replacing it must drop the
                // previous retained size-specific handle or document fonts leak.
                if (mutable_entry->loaded_handle && mutable_entry->loaded_handle != handle) {
                    font_handle_release(mutable_entry->loaded_handle);
                }
                mutable_entry->loaded_handle = handle;
                font_handle_retain(handle); // entry holds a ref too
            }
            log_info("font_face: loaded '%s' from source %d: %s",
                     entry->family, i, src_path);
            return handle;
        }

        log_debug("font_face: source %d failed for '%s': %s", i, entry->family, src_path);
    }

    log_error("font_face: all sources failed for '%s'", entry->family);
    return NULL;
}

FontHandle* font_face_load(FontContext* ctx, const FontFaceDesc* desc,
                            float size_px) {
    if (!ctx || !desc) return NULL;
    const FontFaceEntry* entry = font_face_find_internal(
        ctx, desc->family, desc->weight, desc->slant);
    return font_face_load_entry(ctx, entry, size_px);
}

static FontHandle* font_face_load_for_codepoint(FontContext* ctx,
                                                const FontStyleDesc* style,
                                                uint32_t codepoint) {
    if (!ctx || !style) return NULL;
    const FontFaceEntry* entry = font_face_find_matching(
        ctx, style->family, style->weight, style->slant, true, codepoint);
    return font_face_load_entry(ctx, entry, style->size_px);
}

FontHandle* font_resolve_document_face_for_codepoint(FontContext* ctx,
                                                      const FontStyleDesc* style,
                                                      uint32_t codepoint) {
    if (!ctx || !style || !style->family) return NULL;

    const char* cursor = style->family;
    char family[256];
    while (font_family_list_next(&cursor, family, sizeof(family))) {
        if (!font_face_family_registered(ctx, family)) continue;
        FontStyleDesc candidate = *style;
        candidate.family = family;
        FontHandle* handle = font_face_load_for_codepoint(ctx, &candidate, codepoint);
        if (handle && font_has_codepoint(handle, codepoint)) return handle;
        if (handle) font_handle_release(handle);
    }
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
