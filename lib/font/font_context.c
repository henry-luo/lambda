/**
 * Lambda Unified Font Module — Context Lifecycle
 *
 * Manages the FontContext: FreeType library initialization with custom
 * FT_Memory routing through Pool, arena creation, database setup, and
 * orderly shutdown.
 *
 * Copyright (c) 2025 Lambda Script Project
 */

#include "font_internal.h"
#include <time.h>
#include "../memtrack.h"

// ============================================================================
// FreeType Custom Memory Allocator — routes through Lambda Pool
// ============================================================================

#ifndef __APPLE__
static void* ft_pool_alloc(FT_Memory memory, long size) {
    Pool* pool = (Pool*)memory->user;
    return pool_alloc(pool, (size_t)size);
}

static void ft_pool_free(FT_Memory memory, void* block) {
    Pool* pool = (Pool*)memory->user;
    pool_free(pool, block);
}

static void* ft_pool_realloc(FT_Memory memory, long cur_size, long new_size, void* block) {
    (void)cur_size;
    Pool* pool = (Pool*)memory->user;
    return pool_realloc(pool, block, (size_t)new_size);
}
#endif

// ============================================================================
// Face cache hashmap callbacks
// ============================================================================

static uint64_t face_cache_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const FontCacheKey* entry = (const FontCacheKey*)item;
    if (!entry || !entry->key_str) return 0;
    return hashmap_xxhash3(entry->key_str, strlen(entry->key_str), seed0, seed1);
}

static int face_cache_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const FontCacheKey* ea = (const FontCacheKey*)a;
    const FontCacheKey* eb = (const FontCacheKey*)b;
    if (!ea || !eb || !ea->key_str || !eb->key_str) return -1;
    return strcmp(ea->key_str, eb->key_str);
}

static void face_cache_free(void* item) {
    FontCacheKey* entry = (FontCacheKey*)item;
    if (entry && entry->handle) {
        font_handle_release(entry->handle);
    }
}

// ============================================================================
// Default fallback fonts
// ============================================================================

static const char* default_fallback_fonts[] = {
    "Noto Color Emoji",     // Emoji — Linux / cross-platform (before text fonts
                            // so emoji codepoints get color glyphs, not mono outlines)
    "Apple Color Emoji",    // Emoji — macOS
    "Segoe UI Emoji",       // Emoji — Windows
    "Liberation Sans",
    "DejaVu Sans",
    "Helvetica",
    "Arial",
    "SF Pro Display",
    "Arial Unicode MS",
    "Liberation Serif",
    "Times New Roman",
    "Nimbus Sans",
    "AppleSDGothicNeo",
    NULL
};

// ============================================================================
// FontContext lifecycle
// ============================================================================

FontContext* font_context_create(FontContextConfig* config) {
    // determine pool ownership
    bool owns_pool = false;
    Pool* pool = NULL;
    if (config && config->pool) {
        pool = config->pool;
    } else {
        pool = pool_create();
        if (!pool) {
            log_error("font_context_create: failed to create pool");
            return NULL;
        }
        owns_pool = true;
    }

    // determine arena ownership
    bool owns_arena = false;
    Arena* arena = NULL;
    if (config && config->arena) {
        arena = config->arena;
    } else {
        arena = arena_create_default(pool);
        if (!arena) {
            log_error("font_context_create: failed to create arena");
            if (owns_pool) pool_destroy(pool);
            return NULL;
        }
        owns_arena = true;
    }

    // allocate context from pool
    FontContext* ctx = (FontContext*)pool_calloc(pool, sizeof(FontContext));
    if (!ctx) {
        log_error("font_context_create: failed to allocate FontContext");
        if (owns_arena) arena_destroy(arena);
        if (owns_pool)  pool_destroy(pool);
        return NULL;
    }

    ctx->pool = pool;
    ctx->arena = arena;
    ctx->owns_pool = owns_pool;
    ctx->owns_arena = owns_arena;

    // create glyph arena (separate for bitmap data, resettable)
    ctx->glyph_arena = arena_create(pool, 256 * 1024, 4 * 1024 * 1024);
    if (!ctx->glyph_arena) {
        log_error("font_context_create: failed to create glyph arena");
        if (owns_arena) arena_destroy(arena);
        if (owns_pool)  pool_destroy(pool);
        return NULL;
    }

    // apply configuration
    if (config) {
        ctx->config = *config;
    }
    if (ctx->config.max_cached_faces <= 0) ctx->config.max_cached_faces = 64;
    if (ctx->config.max_cached_glyphs <= 0) ctx->config.max_cached_glyphs = 4096;
    if (ctx->config.pixel_ratio <= 0.0f) ctx->config.pixel_ratio = 1.0f;

    // set up FreeType custom memory allocator routing through our pool
#ifndef __APPLE__
    ctx->ft_memory.user    = pool;
    ctx->ft_memory.alloc   = ft_pool_alloc;
    ctx->ft_memory.free    = ft_pool_free;
    ctx->ft_memory.realloc = ft_pool_realloc;

    // initialize FreeType with custom memory
    FT_Error error = FT_New_Library(&ctx->ft_memory, &ctx->ft_library);
    if (error) {
        log_error("font_context_create: FT_New_Library failed (error %d)", error);
        arena_destroy(ctx->glyph_arena);
        if (owns_arena) arena_destroy(arena);
        if (owns_pool)  pool_destroy(pool);
        return NULL;
    }

    // add default FreeType modules
    FT_Add_Default_Modules(ctx->ft_library);

    // enable LCD subpixel filtering if requested
    if (ctx->config.enable_lcd_rendering) {
        FT_Library_SetLcdFilter(ctx->ft_library, FT_LCD_FILTER_DEFAULT);
    }
#endif

    // create font database
    ctx->database = font_database_create_internal(pool, arena);
    if (!ctx->database) {
        log_error("font_context_create: failed to create font database");
#ifndef __APPLE__
        FT_Done_Library(ctx->ft_library);
#endif
        arena_destroy(ctx->glyph_arena);
        if (owns_arena) arena_destroy(arena);
        if (owns_pool)  pool_destroy(pool);
        return NULL;
    }

    // add platform-specific default font directories
    font_platform_add_default_dirs(ctx->database);

    // create face cache hashmap
    ctx->face_cache = hashmap_new(sizeof(FontCacheKey), 32, 0, 0,
                                  face_cache_hash, face_cache_compare,
                                  face_cache_free, NULL);
    ctx->lru_counter = 0;

    // initialize @font-face descriptor storage
    ctx->face_descriptor_capacity = 16;
    ctx->face_descriptors = (FontFaceEntry**)pool_calloc(pool,
        (size_t)ctx->face_descriptor_capacity * sizeof(FontFaceEntry*));
    ctx->face_descriptor_count = 0;

    // set default fallback fonts
    ctx->fallback_fonts = default_fallback_fonts;

    // create bitmap cache for rendered glyphs (LRU, up to max_cached_glyphs)
    ctx->bitmap_cache = NULL; // lazily created in font_render_glyph on first use

    // Phase 17: loaded glyph cache (lazily created in font_load_glyph)
    ctx->loaded_glyph_cache = NULL;

    log_info("font_context_create: initialized (pixel_ratio=%.1f, max_faces=%d, max_glyphs=%d)",
             ctx->config.pixel_ratio, ctx->config.max_cached_faces, ctx->config.max_cached_glyphs);

    return ctx;
}

void font_context_destroy(FontContext* ctx) {
    if (!ctx) return;

    log_info("font_context_destroy: tearing down");

    // clear @font-face descriptors (handles are released via face_cache cleanup)
    font_face_clear(ctx);

    // free face cache (calls face_cache_free which releases handles)
    if (ctx->face_cache) {
        hashmap_free(ctx->face_cache);
        ctx->face_cache = NULL;
    }

    // free bitmap cache
    if (ctx->bitmap_cache) {
        hashmap_free(ctx->bitmap_cache);
        ctx->bitmap_cache = NULL;
    }

    // Phase 17: free loaded glyph cache
    if (ctx->loaded_glyph_cache) {
        hashmap_free(ctx->loaded_glyph_cache);
        ctx->loaded_glyph_cache = NULL;
    }

    // free font file data cache (entries are malloc-allocated, freed via hashmap free callback)
    if (ctx->file_data_cache) {
        hashmap_free(ctx->file_data_cache);
        ctx->file_data_cache = NULL;
    }

    // destroy font database
    if (ctx->database) {
        font_database_destroy_internal(ctx->database);
        ctx->database = NULL;
    }

    // shut down FreeType
#ifndef __APPLE__
    if (ctx->ft_library) {
        FT_Done_Library(ctx->ft_library);
        ctx->ft_library = NULL;
    }
#endif

    // destroy glyph arena
    if (ctx->glyph_arena) {
        arena_destroy(ctx->glyph_arena);
        ctx->glyph_arena = NULL;
    }

    // save references before freeing ctx
    Pool*  pool      = ctx->pool;
    Arena* arena     = ctx->arena;
    bool   owns_pool = ctx->owns_pool;
    bool   owns_arena = ctx->owns_arena;

    // free the context struct itself
    pool_free(pool, ctx);

    // destroy owned allocators last
    if (owns_arena && arena) arena_destroy(arena);
    if (owns_pool  && pool)  pool_destroy(pool);
}

// ============================================================================
// Batch reset: remove document-specific entries from face_cache
// ============================================================================

// Scan callback to collect document-font cache keys for removal
typedef struct {
    const char** keys;
    int count;
    int capacity;
} DocFontScanState;

static bool collect_doc_font_keys(const void* item, void* udata) {
    const FontCacheKey* entry = (const FontCacheKey*)item;
    DocFontScanState* state = (DocFontScanState*)udata;
    if (entry->handle && entry->handle->is_document_font) {
        if (state->count < state->capacity) {
            state->keys[state->count++] = entry->key_str;
        }
    }
    return true; // continue scanning
}

void font_context_reset_document_fonts(FontContext* ctx) {
    if (!ctx) return;

    // clear @font-face descriptors from the previous document
    font_face_clear(ctx);

    // selectively clear codepoint fallback cache — only remove entries
    // referencing document-specific (@font-face) handles. System font
    // fallback entries survive across documents to avoid re-loading.
    if (ctx->codepoint_fallback_cache) {
        // scan for document-font entries
        size_t count = hashmap_count(ctx->codepoint_fallback_cache);
        if (count > 0) {
            uint32_t stack_keys[128];
            uint32_t* keys = stack_keys;
            int capacity = 128;
            if (count > 128) {
                keys = (uint32_t*)malloc(count * sizeof(uint32_t));
                capacity = (int)count;
            }
            int nremove = 0;

            // iterate and collect codepoints with document-font handles
            size_t iter = 0;
            void* item;
            while (hashmap_iter(ctx->codepoint_fallback_cache, &iter, &item)) {
                CodepointFallbackEntry* e = (CodepointFallbackEntry*)item;
                if (e->handle && e->handle->is_document_font && nremove < capacity) {
                    keys[nremove++] = e->codepoint;
                }
            }

            for (int i = 0; i < nremove; i++) {
                CodepointFallbackEntry search = {.codepoint = keys[i], .handle = NULL};
                hashmap_delete(ctx->codepoint_fallback_cache, &search);
            }

            if (keys != stack_keys) free(keys);
        }
    }

    // selectively remove @font-face entries from face_cache;
    // system font entries survive across documents to avoid re-loading
    if (ctx->face_cache) {
        size_t count = hashmap_count(ctx->face_cache);
        if (count > 0) {
            // collect keys of document-font entries
            const char* stack_keys[64];
            const char** keys = stack_keys;
            int capacity = 64;
            if (count > 64) {
                keys = (const char**)malloc(count * sizeof(const char*));
                capacity = (int)count;
            }

            DocFontScanState state = {keys, 0, capacity};
            hashmap_scan(ctx->face_cache, collect_doc_font_keys, &state);

            // remove collected entries and release their handles
            // (hashmap_delete does NOT call the free callback, so we must release manually)
            for (int i = 0; i < state.count; i++) {
                FontCacheKey search = {.key_str = (char*)state.keys[i], .handle = NULL};
                const FontCacheKey* removed = (const FontCacheKey*)hashmap_delete(ctx->face_cache, &search);
                if (removed && removed->handle) {
                    font_handle_release(removed->handle);
                }
            }

            if (keys != stack_keys) free(keys);

            log_info("font_context_reset_document_fonts: removed %d document font entries, kept %zu system entries",
                     state.count, hashmap_count(ctx->face_cache));
        }
    }

    log_info("font_context_reset_document_fonts: cleared per-document font state");
}

void font_context_reset_glyph_caches(FontContext* ctx) {
    if (!ctx) return;

    // clear loaded glyph cache (entries reference glyph_arena bitmap data)
    if (ctx->loaded_glyph_cache) {
        hashmap_clear(ctx->loaded_glyph_cache, false);
    }

    // clear bitmap cache (also references glyph_arena data)
    if (ctx->bitmap_cache) {
        hashmap_clear(ctx->bitmap_cache, false);
    }

    // reset glyph arena to reclaim bitmap memory without destroying it
    if (ctx->glyph_arena) {
        arena_reset(ctx->glyph_arena);
    }

    log_info("font_context_reset_glyph_caches: cleared loaded/bitmap caches and glyph arena");
}

bool font_context_scan(FontContext* ctx) {
    if (!ctx || !ctx->database) return false;
    if (ctx->database->scanned) return true;

    // try loading from disk cache first (much faster than scanning)
    if (ctx->config.enable_disk_cache && ctx->config.cache_dir) {
        char cache_path[1024];
        snprintf(cache_path, sizeof(cache_path), "%s/font_cache.bin", ctx->config.cache_dir);
        if (font_database_load_cache_internal(ctx->database, cache_path)) {
            log_info("font_context_scan: loaded from disk cache");
            return true;
        }
    }

    bool ok = font_database_scan_internal(ctx->database);

    // save to disk cache after successful scan
    if (ok && ctx->config.enable_disk_cache && ctx->config.cache_dir) {
        char cache_path[1024];
        snprintf(cache_path, sizeof(cache_path), "%s/font_cache.bin", ctx->config.cache_dir);
        font_database_save_cache_internal(ctx->database, cache_path);
    }

    return ok;
}

// ============================================================================
// Migration helpers — allow Radiant to access internals during transition
// ============================================================================

void* font_context_get_ft_library(FontContext* ctx) {
#ifdef __APPLE__
    (void)ctx;
    return NULL;
#else
    return ctx ? ctx->ft_library : NULL;
#endif
}

void* font_handle_get_ft_face(FontHandle* handle) {
#ifdef __APPLE__
    (void)handle;
    return NULL;
#else
    return handle ? handle->ft_face : NULL;
#endif
}

struct FontDatabase* font_context_get_database(FontContext* ctx) {
    return ctx ? ctx->database : NULL;
}

// ============================================================================
// Font Handle Accessors
// ============================================================================

const char* font_handle_get_family_name(FontHandle* handle) {
    if (!handle) return NULL;
    return handle->family_name;
}

float font_handle_get_size_px(FontHandle* handle) {
    return handle ? handle->size_px : 0;
}

float font_handle_get_physical_size_px(FontHandle* handle) {
    return handle ? handle->physical_size_px : 0;
}

// ============================================================================
// FontHandle reference counting
// ============================================================================

FontHandle* font_handle_retain(FontHandle* handle) {
    if (handle) {
        handle->ref_count++;
    }
    return handle;
}

void font_handle_release(FontHandle* handle) {
    if (!handle) return;
    handle->ref_count--;
    if (handle->ref_count <= 0) {
        // destroy the FreeType face (only if we own it)
#ifndef __APPLE__
        if (handle->ft_face && !handle->borrowed_face) {
            FT_Done_Face(handle->ft_face);
            handle->ft_face = NULL;
        }
#endif
        // destroy FontTables
        if (handle->tables && handle->ctx) {
            font_tables_close(handle->tables, handle->ctx->pool);
            handle->tables = NULL;
        }
        // free advance cache
        if (handle->advance_cache) {
            hashmap_free(handle->advance_cache);
            handle->advance_cache = NULL;
        }
        // free kern cache
        if (handle->kern_cache) {
            hashmap_free(handle->kern_cache);
            handle->kern_cache = NULL;
        }
#ifdef __APPLE__
        // release CoreText fonts
        if (handle->ct_font_ref) {
            font_platform_destroy_ct_font(handle->ct_font_ref);
            handle->ct_font_ref = NULL;
        }
        if (handle->ct_raster_ref) {
            font_platform_destroy_ct_font(handle->ct_raster_ref);
            handle->ct_raster_ref = NULL;
        }
#endif
        // free font file data: decrement ref in file_data_cache, or free directly
        if (handle->file_data_path && handle->ctx->file_data_cache) {
            FontFileDataEntry search = {.path = handle->file_data_path, .data = NULL, .data_len = 0};
            FontFileDataEntry* cached = (FontFileDataEntry*)hashmap_get(
                handle->ctx->file_data_cache, &search);
            if (cached) {
                cached->ref_count--;
                if (cached->ref_count <= 0) {
                    // remove from cache — hashmap_delete returns copy in spare,
                    // does NOT call free callback. We must free manually.
                    const FontFileDataEntry* removed = (const FontFileDataEntry*)hashmap_delete(
                        handle->ctx->file_data_cache, &search);
                    if (removed) {
                        free(removed->data);
                        free(removed->path);
                    }
                }
            }
            free(handle->file_data_path);
            handle->file_data_path = NULL;
            // memory_buffer points into cached file_data — don't free separately
        } else {
            // no file_data_path means memory_buffer was malloc'd independently
            // (e.g. from data URI or font_load_memory)
            if (handle->memory_buffer) {
                free(handle->memory_buffer);
                handle->memory_buffer = NULL;
            }
        }

        // free the handle struct via pool
        if (handle->ctx) {
            pool_free(handle->ctx->pool, handle);
        }
    }
}

// ============================================================================
// Font database query passthrough
// ============================================================================

int font_get_font_count(FontContext* ctx) {
    if (!ctx || !ctx->database || !ctx->database->all_fonts) return 0;
    return ctx->database->all_fonts->length;
}

int font_get_family_count(FontContext* ctx) {
    if (!ctx || !ctx->database || !ctx->database->families) return 0;
    return (int)hashmap_count(ctx->database->families);
}

bool font_family_exists(FontContext* ctx, const char* family) {
    if (!ctx || !ctx->database || !family) return false;
    ArrayList* matches = font_database_find_all_matches_internal(ctx->database, family);
    bool exists = (matches && matches->length > 0);
    if (matches) arraylist_free(matches);
    return exists;
}

char* font_find_path(FontContext* ctx, const char* family) {
    if (!ctx || !ctx->database || !family) return NULL;

    ArrayList* matches = font_database_find_all_matches_internal(ctx->database, family);
    if (!matches || matches->length == 0) {
        if (matches) arraylist_free(matches);
        // try platform-specific fallback
        char* result = font_platform_find_fallback(family);
        return result;  // may be NULL
    }

    // prefer Regular/Normal style font over Bold/Italic variants
    FontEntry* best_font = NULL;
    int best_score = -1;
    for (int i = 0; i < matches->length; i++) {
        FontEntry* font = (FontEntry*)matches->data[i];
        int score = 0;
        if (font->weight == 400) score += 10;       // regular weight
        else if (font->weight < 500) score += 5;    // light to normal
        if (font->style == FONT_SLANT_NORMAL) score += 10; // not italic
        // avoid TTC files (ThorVG doesn't support TrueType Collections)
        if (font->file_path && !strstr(font->file_path, ".ttc")) score += 5;
        if (score > best_score) {
            best_score = score;
            best_font = font;
        }
    }

    FontEntry* font = best_font ? best_font : (FontEntry*)matches->data[0];
    char* result = mem_strdup(font->file_path, MEM_CAT_FONT);
    arraylist_free(matches);
    return result;
}

const char* font_slant_to_string(FontSlant slant) {
    switch (slant) {
        case FONT_SLANT_NORMAL:  return "normal";
        case FONT_SLANT_ITALIC:  return "italic";
        case FONT_SLANT_OBLIQUE: return "oblique";
        default:                 return "unknown";
    }
}

FontMatchResult font_find_best_match(FontContext* ctx,
                                      const char* family,
                                      int weight,
                                      FontSlant style) {
    FontMatchResult result = {0};
    if (!ctx || !ctx->database || !family) return result;

    FontDatabaseCriteria criteria;
    memset(&criteria, 0, sizeof(criteria));
    strncpy(criteria.family_name, family, sizeof(criteria.family_name) - 1);
    criteria.weight = weight;
    criteria.style = style;

    FontDatabaseResult db_result = font_database_find_best_match_internal(ctx->database, &criteria);
    if (db_result.font && db_result.font->file_path) {
        result.file_path = db_result.font->file_path;
        result.family_name = db_result.font->family_name;
        result.weight = db_result.font->weight;
        result.style = db_result.font->style;
        result.face_index = db_result.font->is_collection ? db_result.font->collection_index : 0;
        result.match_score = db_result.match_score;
        result.found = true;
    }
    return result;
}

void font_context_add_scan_directory(FontContext* ctx, const char* directory) {
    if (!ctx || !ctx->database || !directory) return;
    if (!ctx->database->scan_directories) {
        ctx->database->scan_directories = arraylist_new(0);
    }
    char* dir_copy = arena_strdup(ctx->arena, directory);
    arraylist_append(ctx->database->scan_directories, dir_copy);
}

// ============================================================================
// Cache statistics
// ============================================================================

FontCacheStats font_get_cache_stats(FontContext* ctx) {
    FontCacheStats stats = {0};
    if (!ctx) return stats;

    stats.face_count = ctx->face_cache ? (int)hashmap_count(ctx->face_cache) : 0;
    stats.glyph_cache_count = ctx->bitmap_cache ? (int)hashmap_count(ctx->bitmap_cache) : 0;
    stats.loaded_glyph_count = ctx->loaded_glyph_cache ? (int)hashmap_count(ctx->loaded_glyph_cache) : 0;
    stats.database_font_count = font_get_font_count(ctx);
    stats.database_family_count = font_get_family_count(ctx);

    // approximate memory usage (split by arena)
    stats.main_arena_bytes = ctx->arena ? arena_total_allocated(ctx->arena) : 0;
    stats.glyph_arena_bytes = ctx->glyph_arena ? arena_total_allocated(ctx->glyph_arena) : 0;
    stats.memory_usage_bytes = stats.main_arena_bytes + stats.glyph_arena_bytes;

    return stats;
}

// ============================================================================
// Disk cache — persist font database for faster startup
// ============================================================================

bool font_cache_save(FontContext* ctx) {
    if (!ctx || !ctx->database) return false;
    if (!ctx->config.cache_dir) {
        log_debug("font_cache_save: no cache_dir configured");
        return false;
    }
    char cache_path[1024];
    snprintf(cache_path, sizeof(cache_path), "%s/font_cache.bin", ctx->config.cache_dir);
    return font_database_save_cache_internal(ctx->database, cache_path);
}

// ============================================================================
// Font accessor helpers (migration support)
// ============================================================================

float font_get_x_height_ratio(FontHandle* handle) {
    if (!handle) return 0.5f; // CSS default

    // primary: FontTables OS/2 sxHeight
    if (handle->tables) {
        Os2Table* os2t = font_tables_get_os2(handle->tables);
        HeadTable* head = font_tables_get_head(handle->tables);
        if (os2t && os2t->sx_height > 0 && head && head->units_per_em > 0) {
            return (float)os2t->sx_height / head->units_per_em;
        }
    }

    // secondary: 'x' glyph bbox via FontTables glyf table
    if (handle->tables) {
        CmapTable* cmap = font_tables_get_cmap(handle->tables);
        HeadTable* head = font_tables_get_head(handle->tables);
        if (cmap && head && head->units_per_em > 0) {
            uint16_t x_gid = cmap_lookup(cmap, 'x');
            if (x_gid > 0) {
                int16_t y_min, y_max;
                if (font_tables_get_glyph_bbox(handle->tables, x_gid, NULL, &y_min, NULL, &y_max)) {
                    return (float)(y_max - y_min) / head->units_per_em;
                }
            }
        }
    }

    return 0.5f; // fallback
}

// ============================================================================
// Font handle wrapping — borrow an existing FT_Face
// ============================================================================

#ifndef __APPLE__
FontHandle* font_handle_wrap(FontContext* ctx, void* ft_face_ptr, float size_px) {
    if (!ctx || !ft_face_ptr) return NULL;

    FT_Face face = (FT_Face)ft_face_ptr;
    float pixel_ratio = (ctx->config.pixel_ratio > 0) ? ctx->config.pixel_ratio : 1.0f;

    FontHandle* handle = (FontHandle*)pool_calloc(ctx->pool, sizeof(FontHandle));
    if (!handle) return NULL;

    handle->ft_face          = face;
    handle->ref_count        = 1;
    handle->borrowed_face    = true; // do NOT call FT_Done_Face on release
    handle->metrics_ready    = false;
    handle->ctx              = ctx;
    handle->size_px          = size_px;
    handle->physical_size_px = size_px * pixel_ratio;

    // copy family name into arena
    if (face->family_name) {
        size_t len = strlen(face->family_name);
        handle->family_name = (char*)arena_alloc(ctx->arena, len + 1);
        if (handle->family_name) {
            memcpy(handle->family_name, face->family_name, len + 1);
        }
    }

    log_debug("font_handle_wrap: borrowed %s @%.0fpx", face->family_name, size_px);
    return handle;
}
#else
FontHandle* font_handle_wrap(FontContext* ctx, void* ft_face_ptr, float size_px) {
    (void)ctx; (void)ft_face_ptr; (void)size_px;
    return NULL; // FreeType not available on macOS
}
#endif
