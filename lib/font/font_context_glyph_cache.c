#include "font_internal.h"

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

    // hashmap eviction only drops cache entries; glyph bitmap chunks remain
    // reserved until the arena is cleared, so reset must release surplus chunks.
    if (ctx->glyph_arena) {
        arena_clear(ctx->glyph_arena);
    }
    ctx->glyph_cache_generation++;
    if (ctx->glyph_cache_generation == 0) ctx->glyph_cache_generation = 1;

    log_info("font_context_reset_glyph_caches: cleared loaded/bitmap caches and glyph arena");
}

bool font_context_enforce_glyph_arena_limit(FontContext* ctx) {
    if (!ctx || !ctx->glyph_arena || ctx->config.max_glyph_arena_bytes == 0) return false;
    size_t allocated = arena_total_allocated(ctx->glyph_arena);
    if (allocated <= ctx->config.max_glyph_arena_bytes) return false;

    log_info("font_context_enforce_glyph_arena_limit: glyph arena %zu exceeded limit %zu",
             allocated, ctx->config.max_glyph_arena_bytes);
    font_context_reset_glyph_caches(ctx);
    return true;
}

uint64_t font_context_glyph_cache_generation(FontContext* ctx) {
    return ctx ? ctx->glyph_cache_generation : 0;
}
