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
#include "font_cbdt.h"
#include "../base64.h"
#include "../memtrack.h"

#include <stdio.h>
#include <limits.h>

// ============================================================================
// Fixed size selection (for bitmap/emoji fonts) — unified from 4 duplicates
// ============================================================================

#ifdef LAMBDA_HAS_FREETYPE
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
#endif

// ============================================================================
// Wrap font data into a FontHandle
// ============================================================================

#ifdef __APPLE__
// macOS: create FontHandle using CoreText + FontTables (no FreeType)
static FontHandle* create_handle(FontContext* ctx,
                                  uint8_t* memory_buffer, size_t memory_buffer_size,
                                  int face_index, float size_px, float physical_size,
                                  FontWeight weight, FontSlant slant) {
    FontHandle* handle = (FontHandle*)pool_calloc(ctx->pool, sizeof(FontHandle));
    if (!handle) return NULL;

    handle->tables = NULL;
    handle->ref_count = 1;
    handle->ctx = ctx;
    handle->memory_buffer = memory_buffer;
    handle->memory_buffer_size = memory_buffer_size;
    handle->size_px = size_px;
    handle->physical_size_px = physical_size;
    handle->weight = weight;
    handle->slant = slant;
    handle->metrics_ready = false;
    handle->bitmap_scale = 1.0f;

    // create FontTables from raw font data (handles TTC via face_index)
    if (memory_buffer && memory_buffer_size > 0) {
        handle->tables = font_tables_open_face(memory_buffer, memory_buffer_size,
                                                face_index, ctx->pool);
        if (!handle->tables) {
            log_debug("font_loader: font_tables_open_face failed (non-fatal)");
        }
    }

    // get family name and PostScript name from FontTables name table
    if (handle->tables) {
        NameTable* name = font_tables_get_name(handle->tables);
        if (name && name->family_name) {
            handle->family_name = arena_strdup(ctx->arena, name->family_name);
        }
    }

    // create CoreText rasterizer from raw font data
    if (memory_buffer && memory_buffer_size > 0) {
        handle->ct_raster_ref = font_rasterize_ct_create(memory_buffer, memory_buffer_size,
                                                          size_px, face_index, weight, slant);
        if (!handle->ct_raster_ref) {
            log_debug("font_loader: ct_raster_ref creation failed (non-fatal)");
        }
    }

    // create CoreText font for glyph advance overrides and GPOS kerning.
    // Use FontTables name table for PostScript and family names.
    {
        const char* ps_name = NULL;
        const char* family = handle->family_name;
        if (handle->tables) {
            NameTable* name = font_tables_get_name(handle->tables);
            if (name) ps_name = name->postscript_name;
        }
        handle->ct_font_ref = font_platform_create_ct_font(
            ps_name, family, size_px, (int)weight);
    }

    return handle;
}
#elif defined(LAMBDA_HAS_FREETYPE)
// Windows: create FontHandle using FreeType + ThorVG
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
    handle->tables = NULL;
    handle->ref_count = 1;
    handle->ctx = ctx;
    handle->memory_buffer = memory_buffer;
    handle->memory_buffer_size = memory_buffer_size;
    handle->size_px = size_px;
    handle->physical_size_px = physical_size;
    handle->weight = weight;
    handle->slant = slant;
    handle->metrics_ready = false;

    // create FontTables from raw font data for direct table access
    if (memory_buffer && memory_buffer_size > 0) {
        handle->tables = font_tables_open(memory_buffer, memory_buffer_size, ctx->pool);
        if (!handle->tables) {
            log_debug("font_loader: font_tables_open failed (non-fatal, FreeType fallback)");
        }
    }

    // create ThorVG rasterization context (preferred rasterizer)
    if (handle->tables) {
        handle->tvg_raster_ctx = font_rasterize_tvg_create();
        if (!handle->tvg_raster_ctx) {
            log_debug("font_loader: tvg_raster_ctx creation failed (non-fatal, FreeType fallback)");
        }
    }

    // compute bitmap scale for fixed-size bitmap fonts (e.g. color emoji)
    handle->bitmap_scale = 1.0f;
    if ((face->face_flags & FT_FACE_FLAG_FIXED_SIZES) &&
        (face->face_flags & FT_FACE_FLAG_COLOR) &&
        face->num_fixed_sizes > 0 &&
        face->size && face->size->metrics.y_ppem > 0) {
        float actual_ppem = (float)face->size->metrics.y_ppem;
        if (actual_ppem > 0 && physical_size > 0 && actual_ppem != physical_size) {
            handle->bitmap_scale = physical_size / actual_ppem;
            log_debug("font_loader: bitmap_scale=%.4f (target=%.0f, actual_ppem=%.0f)",
                      handle->bitmap_scale, physical_size, actual_ppem);
        }
    }

    // copy family name from FreeType face
    if (face->family_name) {
        handle->family_name = arena_strdup(ctx->arena, face->family_name);
    }

    return handle;
}
#else
// Linux/WASM: create FontHandle using FontTables + ThorVG (no FreeType)
static FontHandle* create_handle(FontContext* ctx,
                                  uint8_t* memory_buffer, size_t memory_buffer_size,
                                  int face_index, float size_px, float physical_size,
                                  FontWeight weight, FontSlant slant) {
    FontHandle* handle = (FontHandle*)pool_calloc(ctx->pool, sizeof(FontHandle));
    if (!handle) return NULL;

    handle->tables = NULL;
    handle->ref_count = 1;
    handle->ctx = ctx;
    handle->memory_buffer = memory_buffer;
    handle->memory_buffer_size = memory_buffer_size;
    handle->size_px = size_px;
    handle->physical_size_px = physical_size;
    handle->weight = weight;
    handle->slant = slant;
    handle->metrics_ready = false;
    handle->bitmap_scale = 1.0f;

    // create FontTables from raw font data (handles TTC via face_index)
    if (memory_buffer && memory_buffer_size > 0) {
        handle->tables = font_tables_open_face(memory_buffer, memory_buffer_size,
                                                face_index, ctx->pool);
        if (!handle->tables) {
            log_debug("font_loader: font_tables_open_face failed (non-fatal)");
        }
    }

    // get family name from FontTables name table
    if (handle->tables) {
        NameTable* name = font_tables_get_name(handle->tables);
        if (name && name->family_name) {
            handle->family_name = arena_strdup(ctx->arena, name->family_name);
        }
    }

    // create ThorVG rasterization context (sole rasterizer on Linux)
    if (handle->tables) {
        handle->tvg_raster_ctx = font_rasterize_tvg_create();
        if (!handle->tvg_raster_ctx) {
            log_debug("font_loader: tvg_raster_ctx creation failed");
        }
    }

    // read actual font weight from OS/2 table for synthetic bold detection
    {
        Os2Table* os2t = font_tables_get_os2(handle->tables);
        handle->actual_font_weight = (os2t && os2t->weight_class > 0)
            ? (int)os2t->weight_class
            : (int)weight;  // fall back to requested weight if OS/2 unavailable
    }

    // compute bitmap_scale for fixed-size bitmap fonts via CBDT/CBLC
    if (handle->tables && physical_size > 0) {
        if (cbdt_has_table(handle->tables)) {
            // CBDT fonts have fixed strike sizes; find best match
            CbdtBitmap probe = {0};
            if (cbdt_get_bitmap(handle->tables, 0 /* any glyph */, (int)physical_size, &probe) &&
                probe.ppem > 0 && (float)probe.ppem != physical_size) {
                handle->bitmap_scale = physical_size / (float)probe.ppem;
                log_debug("font_loader: bitmap_scale=%.4f (target=%.0f, strike_ppem=%d)",
                          handle->bitmap_scale, physical_size, probe.ppem);
            }
        }
    }

    return handle;
}
#endif

// ============================================================================
// Variable font axis selection (opsz, wght)
// ============================================================================

#ifdef LAMBDA_HAS_FREETYPE
static void apply_variable_font_axes(FT_Face face, FT_Library library,
                                     float size_px, FontWeight weight) {
    if (!face || !(face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS)) return;

    FT_MM_Var* mm = NULL;
    if (FT_Get_MM_Var(face, &mm) != 0 || !mm) return;

    // read current design coordinates
    FT_Fixed* coords = (FT_Fixed*)mem_alloc(mm->num_axis * sizeof(FT_Fixed), MEM_CAT_FONT);
    if (!coords) { FT_Done_MM_Var(library, mm); return; }

    FT_Get_Var_Design_Coordinates(face, mm->num_axis, coords);

    bool changed = false;
    for (FT_UInt i = 0; i < mm->num_axis; i++) {
        FT_ULong tag = mm->axis[i].tag;
        if (tag == FT_MAKE_TAG('o','p','s','z')) {
            // set optical size to font size (clamped to axis range)
            FT_Fixed min_val = mm->axis[i].minimum;
            FT_Fixed max_val = mm->axis[i].maximum;
            FT_Fixed target = (FT_Fixed)(size_px * 65536.0f);
            if (target < min_val) target = min_val;
            if (target > max_val) target = max_val;
            coords[i] = target;
            changed = true;
        } else if (tag == FT_MAKE_TAG('w','g','h','t')) {
            // set weight axis
            FT_Fixed min_val = mm->axis[i].minimum;
            FT_Fixed max_val = mm->axis[i].maximum;
            FT_Fixed target = (FT_Fixed)((float)weight * 65536.0f);
            if (target < min_val) target = min_val;
            if (target > max_val) target = max_val;
            coords[i] = target;
            changed = true;
        }
    }

    if (changed) {
        FT_Set_Var_Design_Coordinates(face, mm->num_axis, coords);
    }

    mem_free(coords);
    FT_Done_MM_Var(library, mm);
}
#endif // LAMBDA_HAS_FREETYPE

// ============================================================================
// Font file data cache — avoids re-reading the same file into the arena
// ============================================================================

static uint64_t file_data_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const FontFileDataEntry* e = (const FontFileDataEntry*)item;
    if (!e || !e->path) return 0;
    return hashmap_xxhash3(e->path, strlen(e->path), seed0, seed1);
}

static int file_data_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const FontFileDataEntry* ea = (const FontFileDataEntry*)a;
    const FontFileDataEntry* eb = (const FontFileDataEntry*)b;
    if (!ea || !eb || !ea->path || !eb->path) return -1;
    return strcmp(ea->path, eb->path);
}

static void file_data_free(void* item) {
    FontFileDataEntry* e = (FontFileDataEntry*)item;
    if (e) {
        if (e->data) { mem_free(e->data); e->data = NULL; }
        if (e->path) { mem_free(e->path); e->path = NULL; }
    }
}

static struct hashmap* ensure_file_data_cache(FontContext* ctx) {
    if (!ctx->file_data_cache) {
        ctx->file_data_cache = hashmap_new(sizeof(FontFileDataEntry), 32, 0, 0,
                                            file_data_hash, file_data_compare,
                                            file_data_free, NULL);
    }
    return ctx->file_data_cache;
}

// look up cached font file data; returns true if found, increments ref_count
static bool file_data_cache_lookup(FontContext* ctx, const char* path,
                                    const uint8_t** out_data, size_t* out_len) {
    struct hashmap* cache = ensure_file_data_cache(ctx);
    if (!cache) return false;
    FontFileDataEntry search = {.path = (char*)path, .data = NULL, .data_len = 0};
    FontFileDataEntry* found = (FontFileDataEntry*)hashmap_get(cache, &search);
    if (found) {
        found->ref_count++;
        *out_data = found->data;
        *out_len = found->data_len;
        return true;
    }
    return false;
}

// store font file data in cache (data is malloc-allocated, takes ownership)
static void file_data_cache_insert(FontContext* ctx, const char* path,
                                    uint8_t* data, size_t len) {
    struct hashmap* cache = ensure_file_data_cache(ctx);
    if (!cache) return;
    char* dup_path = mem_strdup(path, MEM_CAT_FONT);  // raw strdup: freed by file_data_free/raw free
    FontFileDataEntry entry = {.path = dup_path, .data = data, .data_len = len, .ref_count = 1};
    FontFileDataEntry* old = (FontFileDataEntry*)hashmap_set(cache, &entry);
    if (old) {
        // replaced an existing entry — free old data (but NOT our new entry's data).
        // hashmap_set returns a copy of the replaced entry.
        if (old->data != data) { mem_free(old->data); }
        if (old->path != dup_path) { mem_free(old->path); }
    }
}

// ============================================================================
// Load face from file path
// ============================================================================

FontHandle* font_load_face_internal(FontContext* ctx, const char* path,
                                     int face_index, float size_px,
                                     float physical_size, FontWeight weight, FontSlant slant) {
    if (!ctx || !path) return NULL;

    // Strip URL query string and fragment from file path (e.g., "font.ttf?v=4.0.3#iefix" → "font.ttf")
    // CSS @font-face src URLs commonly include version parameters that aren't part of the file path
    char clean_path[2048];
    size_t path_len = strlen(path);
    if (path_len >= sizeof(clean_path)) path_len = sizeof(clean_path) - 1;
    memcpy(clean_path, path, path_len);
    clean_path[path_len] = '\0';
    for (size_t i = 0; i < path_len; i++) {
        if (clean_path[i] == '?' || clean_path[i] == '#') {
            clean_path[i] = '\0';
            break;
        }
    }
    path = clean_path;

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
        // check file data cache for previously decompressed SFNT data
        const uint8_t* cached_data = NULL;
        size_t cached_len = 0;
        if (file_data_cache_lookup(ctx, path, &cached_data, &cached_len)) {
            // reuse cached decompressed SFNT data
#ifdef LAMBDA_HAS_FREETYPE
            FT_Face face = NULL;
            FT_Error err = FT_New_Memory_Face(ctx->ft_library, cached_data, (FT_Long)cached_len,
                                               face_index, &face);
            if (err) {
                log_error("font_loader: FT_New_Memory_Face failed for '%s' (error %d)", path, err);
                return NULL;
            }
            apply_variable_font_axes(face, ctx->ft_library, size_px, weight);
            set_face_size(face, physical_size);
            FontHandle* handle = create_handle(ctx, face, (uint8_t*)cached_data, cached_len,
                                                size_px, physical_size, weight, slant);
#else
            FontHandle* handle = create_handle(ctx, (uint8_t*)cached_data, cached_len,
                                                face_index, size_px, physical_size, weight, slant);
#endif
            if (handle) {
                handle->file_data_path = mem_strdup(path, MEM_CAT_FONT);  // raw strdup: freed by raw free
                log_info("font_loader: loaded WOFF '%s' from file cache (family=%s, size=%.0f)",
                         path, handle->family_name ? handle->family_name : "?", physical_size);
            }
            return handle;
        }

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
        uint8_t* file_data = (uint8_t*)mem_alloc(file_size, MEM_CAT_FONT);
        if (!file_data) { fclose(fp); return NULL; }
        size_t read_count = fread(file_data, 1, file_size, fp);
        fclose(fp);
        if (read_count != file_size) {
            mem_free(file_data);
            log_error("font_loader: failed to read '%s'", path);
            return NULL;
        }

        const uint8_t* sfnt_data = NULL;
        size_t sfnt_len = 0;
        bool ok = font_decompress_if_needed(NULL, file_data, file_size,
                                             format, &sfnt_data, &sfnt_len);
        mem_free(file_data);

        if (!ok) {
            log_error("font_loader: decompression failed for '%s'", path);
            return NULL;
        }

        // cache decompressed SFNT data for reuse at different sizes
        file_data_cache_insert(ctx, path, (uint8_t*)sfnt_data, sfnt_len);

        // use cached data directly — avoid a second copy
#ifdef LAMBDA_HAS_FREETYPE
        FT_Face face = NULL;
        FT_Error err = FT_New_Memory_Face(ctx->ft_library, sfnt_data, (FT_Long)sfnt_len,
                                           face_index, &face);
        if (err) {
            log_error("font_loader: FT_New_Memory_Face failed for '%s' (error %d)", path, err);
            return NULL;
        }
        apply_variable_font_axes(face, ctx->ft_library, size_px, weight);
        set_face_size(face, physical_size);
        FontHandle* handle = create_handle(ctx, face, (uint8_t*)sfnt_data, sfnt_len,
                                            size_px, physical_size, weight, slant);
#else
        FontHandle* handle = create_handle(ctx, (uint8_t*)sfnt_data, sfnt_len,
                                            face_index, size_px, physical_size, weight, slant);
#endif
        if (handle) {
            handle->file_data_path = mem_strdup(path, MEM_CAT_FONT);  // raw strdup: freed by raw free
            log_info("font_loader: loaded WOFF '%s' (family=%s, size=%.0f)",
                     path, handle->family_name ? handle->family_name : "?", physical_size);
        }
        return handle;
    }

    // TTF/OTF/TTC: check file data cache first, then read from disk
    {
        const uint8_t* cached_data = NULL;
        size_t cached_len = 0;
        if (file_data_cache_lookup(ctx, path, &cached_data, &cached_len)) {
            // reuse cached file data — avoid re-reading and arena-allocating
#ifdef LAMBDA_HAS_FREETYPE
            FT_Face face = NULL;
            FT_Error err = FT_New_Memory_Face(ctx->ft_library, cached_data, (FT_Long)cached_len,
                                               face_index, &face);
            if (err) {
                log_error("font_loader: FT_New_Memory_Face failed for '%s' (error %d)", path, err);
                return NULL;
            }
            apply_variable_font_axes(face, ctx->ft_library, size_px, weight);
            set_face_size(face, physical_size);
            FontHandle* handle = create_handle(ctx, face, (uint8_t*)cached_data, cached_len,
                                                size_px, physical_size, weight, slant);
#else
            FontHandle* handle = create_handle(ctx, (uint8_t*)cached_data, cached_len,
                                                face_index, size_px, physical_size, weight, slant);
#endif
            if (handle) {
                handle->file_data_path = mem_strdup(path, MEM_CAT_FONT);  // raw strdup: freed by raw free
                log_info("font_loader: loaded '%s' from file cache (family=%s, size=%.0f)",
                         path, handle->family_name ? handle->family_name : "?", physical_size);
            }
            return handle;
        }

        fp = fopen(path, "rb");
        if (!fp) {
            log_error("font_loader: cannot reopen '%s'", path);
            return NULL;
        }
        fseek(fp, 0, SEEK_END);
        long ttf_size_long = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (ttf_size_long <= 0) { fclose(fp); return NULL; }
        size_t ttf_size = (size_t)ttf_size_long;
        uint8_t* ttf_buf = (uint8_t*)mem_alloc(ttf_size, MEM_CAT_FONT);
        if (!ttf_buf) { fclose(fp); return NULL; }
        size_t ttf_read = fread(ttf_buf, 1, ttf_size, fp);
        fclose(fp);
        if (ttf_read != ttf_size) {
            log_error("font_loader: failed to read '%s'", path);
            return NULL;
        }

        // cache the raw file data for reuse at different sizes
        file_data_cache_insert(ctx, path, ttf_buf, ttf_size);

#ifdef LAMBDA_HAS_FREETYPE
        FT_Face face = NULL;
        FT_Error err = FT_New_Memory_Face(ctx->ft_library, ttf_buf, (FT_Long)ttf_size,
                                           face_index, &face);
        if (err) {
            log_error("font_loader: FT_New_Memory_Face failed for '%s' (error %d)", path, err);
            return NULL;
        }

        apply_variable_font_axes(face, ctx->ft_library, size_px, weight);
        set_face_size(face, physical_size);

        FontHandle* handle = create_handle(ctx, face, ttf_buf, ttf_size,
                                            size_px, physical_size, weight, slant);
#else
        FontHandle* handle = create_handle(ctx, ttf_buf, ttf_size,
                                            face_index, size_px, physical_size, weight, slant);
#endif
        if (handle) {
            handle->file_data_path = mem_strdup(path, MEM_CAT_FONT);  // raw strdup: freed by raw free
            log_info("font_loader: loaded '%s' (family=%s, size=%.0f)",
                     path, handle->family_name ? handle->family_name : "?", physical_size);
        }
        return handle;
    }
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
        if (!font_decompress_if_needed(NULL, data, len, format,
                                        &decompressed, &decompressed_len)) {
            log_error("font_loader: in-memory decompression failed");
            return NULL;
        }
        sfnt_data = decompressed;
        sfnt_len = decompressed_len;
    }

    // copy data to a malloc buffer that outlives the face
    // (caller's data may be temporary, e.g. from base64 decode)
    uint8_t* buf = (uint8_t*)mem_alloc(sfnt_len, MEM_CAT_FONT);
    if (!buf) {
        log_error("font_loader: malloc failed for %zu bytes", sfnt_len);
        if (sfnt_data != data) mem_free((void*)sfnt_data); // free decompressed data
        return NULL;
    }
    if (sfnt_data == (const uint8_t*)buf) {
        // already our buffer (shouldn't happen, but guard)
    } else {
        memcpy(buf, sfnt_data, sfnt_len);
        // free decompressed buffer if we allocated one
        if (sfnt_data != data) mem_free((void*)sfnt_data);
    }

#ifdef LAMBDA_HAS_FREETYPE
    FT_Face face = NULL;
    FT_Error err = FT_New_Memory_Face(ctx->ft_library, buf, (FT_Long)sfnt_len,
                                       face_index, &face);
    if (err) {
        log_error("font_loader: FT_New_Memory_Face failed (error %d)", err);
        return NULL;
    }

    apply_variable_font_axes(face, ctx->ft_library, size_px, weight);
    set_face_size(face, physical_size);

    FontHandle* handle = create_handle(ctx, face, buf, sfnt_len,
                                        size_px, physical_size, weight, slant);
#else
    FontHandle* handle = create_handle(ctx, buf, sfnt_len,
                                        face_index, size_px, physical_size, weight, slant);
#endif
    if (handle) {
        log_info("font_loader: loaded from memory (family=%s, size=%.0f, %zu bytes)",
                 handle->family_name ? handle->family_name : "?", physical_size, sfnt_len);
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
