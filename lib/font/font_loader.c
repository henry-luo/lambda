/**
 * Lambda Unified Font Module — Font Loader
 *
 * Unified face loading pipeline: detect format → decompress if needed →
 * FT_New_Face or FT_New_Memory_Face → size selection → wrap in FontHandle.
 *
 * Consolidates the duplicated FT_Select_Size logic (was in 4 places)
 * into a single select_best_fixed_size() helper.
 *
 * Copyright (c) 2025 Lambda Script Project
 */

#include "font_internal.h"
#include "../base64.h"

#include <stdio.h>
#include <limits.h>

// ============================================================================
// Fixed size selection (for bitmap/emoji fonts) — unified from 4 duplicates
// ============================================================================

void font_select_best_fixed_size(FT_Face face, int target_ppem) {
    if (!face || face->num_fixed_sizes <= 0) return;

    int best_idx = 0;
    int best_diff = INT_MAX;

    for (int i = 0; i < face->num_fixed_sizes; i++) {
        int ppem = face->available_sizes[i].y_ppem >> 6;
        int diff = abs(ppem - target_ppem);
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = i;
        }
    }

    FT_Select_Size(face, best_idx);
    log_debug("font_loader: selected fixed size index %d (ppem=%ld) for target %d",
              best_idx, face->available_sizes[best_idx].y_ppem >> 6, target_ppem);
}

// ============================================================================
// Set face size — handles both scalable and fixed-size fonts
// ============================================================================

static void set_face_size(FT_Face face, float physical_size_px) {
    int target_ppem = (int)physical_size_px;

    // color emoji / fixed bitmap fonts: use FT_Select_Size
    if ((face->face_flags & FT_FACE_FLAG_FIXED_SIZES) &&
        (face->face_flags & FT_FACE_FLAG_COLOR) &&
        face->num_fixed_sizes > 0) {
        font_select_best_fixed_size(face, target_ppem);
    } else {
        FT_Set_Pixel_Sizes(face, 0, (FT_UInt)physical_size_px);
    }
}

// ============================================================================
// Wrap an FT_Face into a FontHandle
// ============================================================================

static FontHandle* create_handle(FontContext* ctx, FT_Face face,
                                  uint8_t* memory_buffer, size_t memory_buffer_size,
                                  float size_px, float physical_size,
                                  FontWeight weight, FontSlant slant) {
    FontHandle* handle = (FontHandle*)pool_calloc(ctx->pool, sizeof(FontHandle));
    if (!handle) {
        FT_Done_Face(face);
        return NULL;
    }

    handle->ft_face = face;
    handle->ref_count = 1;
    handle->ctx = ctx;
    handle->memory_buffer = memory_buffer;
    handle->memory_buffer_size = memory_buffer_size;
    handle->size_px = size_px;
    handle->physical_size_px = physical_size;
    handle->weight = weight;
    handle->slant = slant;
    handle->metrics_ready = false;

    // copy family name from FreeType face
    if (face->family_name) {
        handle->family_name = arena_strdup(ctx->arena, face->family_name);
    }

    return handle;
}

// ============================================================================
// Load face from file path
// ============================================================================

FontHandle* font_load_face_internal(FontContext* ctx, const char* path,
                                     int face_index, float size_px,
                                     float physical_size, FontWeight weight, FontSlant slant) {
    if (!ctx || !path) return NULL;

    // detect format by reading file magic bytes
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        log_error("font_loader: cannot open '%s'", path);
        return NULL;
    }

    uint8_t magic[4];
    size_t nread = fread(magic, 1, 4, fp);
    fclose(fp);

    if (nread < 4) {
        log_error("font_loader: file too small '%s'", path);
        return NULL;
    }

    FontFormat format = font_detect_format(magic, 4);

    // WOFF/WOFF2: read entire file → decompress → load from memory
    if (format == FONT_FORMAT_WOFF || format == FONT_FORMAT_WOFF2) {
        // read entire file into memory
        fp = fopen(path, "rb");
        if (!fp) {
            log_error("font_loader: cannot reopen '%s'", path);
            return NULL;
        }
        fseek(fp, 0, SEEK_END);
        long file_size_long = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (file_size_long <= 0) { fclose(fp); return NULL; }
        size_t file_size = (size_t)file_size_long;
        uint8_t* file_data = (uint8_t*)malloc(file_size);
        if (!file_data) { fclose(fp); return NULL; }
        size_t read_count = fread(file_data, 1, file_size, fp);
        fclose(fp);
        if (read_count != file_size) {
            free(file_data);
            log_error("font_loader: failed to read '%s'", path);
            return NULL;
        }

        const uint8_t* sfnt_data = NULL;
        size_t sfnt_len = 0;
        bool ok = font_decompress_if_needed(ctx->arena, file_data, file_size,
                                             format, &sfnt_data, &sfnt_len);
        free(file_data);

        if (!ok) {
            log_error("font_loader: decompression failed for '%s'", path);
            return NULL;
        }

        // sfnt_data is arena-allocated, load from memory
        return font_load_memory_internal(ctx, sfnt_data, sfnt_len, face_index,
                                          size_px, physical_size, weight, slant);
    }

    // TTF/OTF/TTC: load directly from file
    FT_Face face = NULL;
    FT_Error err = FT_New_Face(ctx->ft_library, path, face_index, &face);
    if (err) {
        log_error("font_loader: FT_New_Face failed for '%s' (error %d)", path, err);
        return NULL;
    }

    set_face_size(face, physical_size);

    FontHandle* handle = create_handle(ctx, face, NULL, 0,
                                        size_px, physical_size, weight, slant);
    if (handle) {
        log_info("font_loader: loaded '%s' (family=%s, size=%.0f)",
                 path, face->family_name ? face->family_name : "?", physical_size);
    }
    return handle;
}

// ============================================================================
// Load face from memory buffer
// ============================================================================

FontHandle* font_load_memory_internal(FontContext* ctx, const uint8_t* data,
                                       size_t len, int face_index, float size_px,
                                       float physical_size, FontWeight weight, FontSlant slant) {
    if (!ctx || !data || len == 0) return NULL;

    // detect format and decompress if needed
    FontFormat format = font_detect_format(data, len);

    const uint8_t* sfnt_data = data;
    size_t sfnt_len = len;

    if (format == FONT_FORMAT_WOFF || format == FONT_FORMAT_WOFF2) {
        const uint8_t* decompressed = NULL;
        size_t decompressed_len = 0;
        if (!font_decompress_if_needed(ctx->arena, data, len, format,
                                        &decompressed, &decompressed_len)) {
            log_error("font_loader: in-memory decompression failed");
            return NULL;
        }
        sfnt_data = decompressed;
        sfnt_len = decompressed_len;
    }

    // copy to arena if the data isn't already arena-owned
    // (FreeType requires the buffer to outlive the face)
    uint8_t* buf = NULL;
    if (!arena_owns(ctx->arena, sfnt_data)) {
        buf = (uint8_t*)arena_alloc(ctx->arena, sfnt_len);
        if (!buf) {
            log_error("font_loader: arena_alloc failed for %zu bytes", sfnt_len);
            return NULL;
        }
        memcpy(buf, sfnt_data, sfnt_len);
    } else {
        buf = (uint8_t*)sfnt_data; // already arena-owned
    }

    FT_Face face = NULL;
    FT_Error err = FT_New_Memory_Face(ctx->ft_library, buf, (FT_Long)sfnt_len,
                                       face_index, &face);
    if (err) {
        log_error("font_loader: FT_New_Memory_Face failed (error %d)", err);
        return NULL;
    }

    set_face_size(face, physical_size);

    FontHandle* handle = create_handle(ctx, face, buf, sfnt_len,
                                        size_px, physical_size, weight, slant);
    if (handle) {
        log_info("font_loader: loaded from memory (family=%s, size=%.0f, %zu bytes)",
                 face->family_name ? face->family_name : "?", physical_size, sfnt_len);
    }
    return handle;
}

// ============================================================================
// Public API: load from file
// ============================================================================

FontHandle* font_load_from_file(FontContext* ctx, const char* path,
                                 const FontStyleDesc* style) {
    if (!ctx || !path || !style) return NULL;

    float pixel_ratio = ctx->config.pixel_ratio;
    float physical_size = style->size_px * pixel_ratio;

    return font_load_face_internal(ctx, path, 0, style->size_px,
                                    physical_size, style->weight, style->slant);
}

// ============================================================================
// Public API: load from data URI
// ============================================================================

FontHandle* font_load_from_data_uri(FontContext* ctx, const char* data_uri,
                                     const FontStyleDesc* style) {
    if (!ctx || !data_uri || !style) return NULL;

    // data URIs look like: data:font/woff2;base64,AAAA...
    // or: data:application/font-woff2;base64,AAAA...

    // find the base64 data start
    const char* comma = strchr(data_uri, ',');
    if (!comma) {
        log_error("font_loader: invalid data URI (no comma)");
        return NULL;
    }
    comma++; // skip comma

    // base64 decode
    size_t b64_len = strlen(comma);
    size_t decoded_len = 0;
    uint8_t* decoded = base64_decode(comma, b64_len, &decoded_len);
    if (!decoded || decoded_len == 0) {
        log_error("font_loader: base64 decode failed");
        return NULL;
    }

    float pixel_ratio = ctx->config.pixel_ratio;
    float physical_size = style->size_px * pixel_ratio;

    return font_load_memory_internal(ctx, decoded, decoded_len, 0,
                                      style->size_px, physical_size,
                                      style->weight, style->slant);
}

// ============================================================================
// Public API: load from raw bytes
// ============================================================================

FontHandle* font_load_from_memory(FontContext* ctx, const uint8_t* data,
                                   size_t len, const FontStyleDesc* style) {
    if (!ctx || !data || len == 0 || !style) return NULL;

    float pixel_ratio = ctx->config.pixel_ratio;
    float physical_size = style->size_px * pixel_ratio;

    return font_load_memory_internal(ctx, data, len, 0,
                                      style->size_px, physical_size,
                                      style->weight, style->slant);
}
