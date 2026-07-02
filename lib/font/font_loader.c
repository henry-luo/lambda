/**
 * Lambda Unified Font Module — Font Loader
 *
 * Unified face loading pipeline: detect format, decompress if needed, create
 * FontTables/native backend state, and wrap the result in FontHandle.
 *
 * Copyright (c) 2025 Lambda Script Project
 */

#include "font_internal.h"
#include "font_cbdt.h"
#include "../base64.h"
#include "../memtrack.h"

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
// mmap not available on Windows — define stubs; mmap always "fails" so the
// heap-fallback path is used instead.
#define PROT_READ  0
#define MAP_PRIVATE 0
#define MAP_FAILED ((void*)-1)
#define O_CLOEXEC  0
#ifndef O_BINARY
#define O_BINARY 0
#endif
static inline void* mmap(void* a, size_t b, int c, int d, int e, long f)
    { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f; return MAP_FAILED; }
static inline int munmap(void* a, size_t b) { (void)a;(void)b; return 0; }
#else
#include <unistd.h>
#include <sys/mman.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#endif
#include <sys/stat.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
// ============================================================================
// Wrap font data into a FontHandle
// ============================================================================

#ifdef __APPLE__
// macOS: create FontHandle using CoreText + FontTables
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

    font_backend_create(handle, memory_buffer, memory_buffer_size,
                        face_index, weight, slant);

    // read actual font weight from OS/2 table for synthetic bold detection
    // and macOS CoreText advance override decisions.
    {
        Os2Table* os2t = font_tables_get_os2(handle->tables);
        handle->actual_font_weight = (os2t && os2t->weight_class > 0)
            ? (int)os2t->weight_class
            : (int)weight;
    }

    return handle;
}
#elif defined(LAMBDA_HAS_DWRITE)
// Windows: create FontHandle using FontTables + DirectWrite
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

    // create FontTables from raw font data for direct table access
    if (memory_buffer && memory_buffer_size > 0) {
        handle->tables = font_tables_open_face(memory_buffer, memory_buffer_size,
                                               face_index, ctx->pool);
        if (!handle->tables) {
            log_debug("font_loader: font_tables_open_face failed (non-fatal, DirectWrite fallback)");
        }
    }

    font_backend_create(handle, memory_buffer, memory_buffer_size,
                        face_index, weight, slant);

    // get family name from FontTables name table
    if (handle->tables) {
        NameTable* name = font_tables_get_name(handle->tables);
        if (name && name->family_name) {
            handle->family_name = arena_strdup(ctx->arena, name->family_name);
        }
    }

    // read actual font weight from OS/2 table for synthetic bold detection
    {
        Os2Table* os2t = font_tables_get_os2(handle->tables);
        handle->actual_font_weight = (os2t && os2t->weight_class > 0)
            ? (int)os2t->weight_class
            : (int)weight;
    }

    // compute bitmap_scale for fixed-size bitmap fonts via CBDT/CBLC
    if (handle->tables && physical_size > 0) {
        if (cbdt_has_table(handle->tables)) {
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
#else
// Linux/WASM: create FontHandle using FontTables + ThorVG
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

    font_backend_create(handle, memory_buffer, memory_buffer_size,
                        face_index, weight, slant);

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
        if (e->data) {
            if (e->is_mmap) { munmap(e->data, e->data_len); }
            else { mem_free(e->data); }
            e->data = NULL;
        }
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

// store font file data in cache (data is malloc-allocated or mmap'd, takes ownership)
static void file_data_cache_insert(FontContext* ctx, const char* path,
                                    uint8_t* data, size_t len, bool is_mmap) {
    struct hashmap* cache = ensure_file_data_cache(ctx);
    if (!cache) return;
    char* dup_path = mem_strdup(path, MEM_CAT_FONT);  // raw strdup: freed by file_data_free/raw free
    FontFileDataEntry entry = {.path = dup_path, .data = data, .data_len = len,
                                .ref_count = 1, .is_mmap = is_mmap};
    FontFileDataEntry* old = (FontFileDataEntry*)hashmap_set(cache, &entry);
    if (old) {
        // replaced an existing entry — free old data (but NOT our new entry's data).
        // hashmap_set returns a copy of the replaced entry.
        if (old->data != data) {
            if (old->is_mmap) { munmap(old->data, old->data_len); }
            else { mem_free(old->data); }
        }
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
            FontHandle* handle = create_handle(ctx, (uint8_t*)cached_data, cached_len,
                                                face_index, size_px, physical_size, weight, slant);
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
        file_data_cache_insert(ctx, path, (uint8_t*)sfnt_data, sfnt_len, false);

        // use cached data directly — avoid a second copy
        FontHandle* handle = create_handle(ctx, (uint8_t*)sfnt_data, sfnt_len,
                                            face_index, size_px, physical_size, weight, slant);
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
            FontHandle* handle = create_handle(ctx, (uint8_t*)cached_data, cached_len,
                                                face_index, size_px, physical_size, weight, slant);
            if (handle) {
                handle->file_data_path = mem_strdup(path, MEM_CAT_FONT);  // raw strdup: freed by raw free
                log_info("font_loader: loaded '%s' from file cache (family=%s, size=%.0f)",
                         path, handle->family_name ? handle->family_name : "?", physical_size);
            }
            return handle;
        }

        // mmap the font file (read-only, private) instead of fread'ing the whole
        // thing into the heap. Many system fonts are huge (e.g. Apple Color
        // Emoji.ttc = 188 MB) and we typically touch only a small fraction of
        // pages — mmap lets the kernel demand-page and reclaim under pressure.
        // Fall back to fread on mmap failure.
        int fd = open(path, O_RDONLY | O_BINARY | O_CLOEXEC);
        if (fd < 0) {
            log_error("font_loader: cannot open '%s'", path);
            return NULL;
        }
        struct stat st;
        if (fstat(fd, &st) != 0 || st.st_size <= 0) {
            close(fd);
            log_error("font_loader: fstat failed for '%s'", path);
            return NULL;
        }
        size_t ttf_size = (size_t)st.st_size;
        uint8_t* ttf_buf = (uint8_t*)mmap(NULL, ttf_size, PROT_READ, MAP_PRIVATE, fd, 0);
        bool ttf_is_mmap = true;
        if (ttf_buf == MAP_FAILED) {
            // mmap failed — fall back to read into heap
            ttf_buf = (uint8_t*)mem_alloc(ttf_size, MEM_CAT_FONT);
            if (!ttf_buf) { close(fd); return NULL; }
            ssize_t total = 0;
            while ((size_t)total < ttf_size) {
                ssize_t r = read(fd, ttf_buf + total, ttf_size - (size_t)total);
                if (r <= 0) break;
                total += r;
            }
            close(fd);
            if ((size_t)total != ttf_size) {
                mem_free(ttf_buf);
                log_error("font_loader: failed to read '%s'", path);
                return NULL;
            }
            ttf_is_mmap = false;
        } else {
            close(fd);  // safe to close after mmap
        }

        // cache the raw file data for reuse at different sizes
        file_data_cache_insert(ctx, path, ttf_buf, ttf_size, ttf_is_mmap);

        FontHandle* handle = create_handle(ctx, ttf_buf, ttf_size,
                                            face_index, size_px, physical_size, weight, slant);
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

    FontHandle* handle = create_handle(ctx, buf, sfnt_len,
                                        face_index, size_px, physical_size, weight, slant);
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
        if (decoded) mem_free(decoded);
        log_error("font_loader: base64 decode failed");
        return NULL;
    }

    float pixel_ratio = ctx->config.pixel_ratio;
    float physical_size = style->size_px * pixel_ratio;

    FontHandle* handle = font_load_memory_internal(ctx, decoded, decoded_len, 0,
                                                   style->size_px, physical_size,
                                                   style->weight, style->slant);
    // font_load_memory_internal copies the bytes into the font cache; the
    // decoded data URI buffer is temporary and otherwise leaks per @font-face.
    mem_free(decoded);
    return handle;
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
