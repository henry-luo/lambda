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

// ============================================================================
// FreeType Custom Memory Allocator — routes through Lambda Pool
// ============================================================================

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

    // create font database
    ctx->database = font_database_create_internal(pool, arena);
    if (!ctx->database) {
        log_error("font_context_create: failed to create font database");
        FT_Done_Library(ctx->ft_library);
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

    // destroy font database
    if (ctx->database) {
        font_database_destroy_internal(ctx->database);
        ctx->database = NULL;
    }

    // shut down FreeType
    if (ctx->ft_library) {
        FT_Done_Library(ctx->ft_library);
        ctx->ft_library = NULL;
    }

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
    return ctx ? ctx->ft_library : NULL;
}

void* font_handle_get_ft_face(FontHandle* handle) {
    return handle ? handle->ft_face : NULL;
}

struct FontDatabase* font_context_get_database(FontContext* ctx) {
    return ctx ? ctx->database : NULL;
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
        // destroy the FreeType face
        if (handle->ft_face) {
            FT_Done_Face(handle->ft_face);
            handle->ft_face = NULL;
        }
        // free advance cache
        if (handle->advance_cache) {
            hashmap_free(handle->advance_cache);
            handle->advance_cache = NULL;
        }
        // memory_buffer is arena-allocated, no individual free needed
        // family_name is arena-allocated, no individual free needed

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
    stats.database_font_count = font_get_font_count(ctx);
    stats.database_family_count = font_get_family_count(ctx);

    // approximate memory usage
    stats.memory_usage_bytes = 0;
    if (ctx->arena)       stats.memory_usage_bytes += arena_total_allocated(ctx->arena);
    if (ctx->glyph_arena) stats.memory_usage_bytes += arena_total_allocated(ctx->glyph_arena);

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
