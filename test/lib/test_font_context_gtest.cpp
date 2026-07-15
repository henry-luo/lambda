#include <gtest/gtest.h>

#include "../../lib/font/font_internal.h"

TEST(FontContextTest, FamilyListParserPreservesOrderAndQuotedNames) {
    const char* cursor = "  \"Open Sans\", Arial , ' Noto, Serif ', sans-serif";
    char family[64];

    ASSERT_TRUE(font_family_list_next(&cursor, family, sizeof(family)));
    EXPECT_STREQ(family, "Open Sans");
    ASSERT_TRUE(font_family_list_next(&cursor, family, sizeof(family)));
    EXPECT_STREQ(family, "Arial");
    ASSERT_TRUE(font_family_list_next(&cursor, family, sizeof(family)));
    EXPECT_STREQ(family, " Noto, Serif ");
    ASSERT_TRUE(font_family_list_next(&cursor, family, sizeof(family)));
    EXPECT_STREQ(family, "sans-serif");
    EXPECT_FALSE(font_family_list_next(&cursor, family, sizeof(family)));
}

TEST(FontContextTest, FamilyListParserSkipsEmptyCandidatesAndUnescapesQuotes) {
    const char* cursor = ", , \"A\\\" B\", Helvetica, ";
    char family[64];

    ASSERT_TRUE(font_family_list_next(&cursor, family, sizeof(family)));
    EXPECT_STREQ(family, "A\" B");
    ASSERT_TRUE(font_family_list_next(&cursor, family, sizeof(family)));
    EXPECT_STREQ(family, "Helvetica");
    EXPECT_FALSE(font_family_list_next(&cursor, family, sizeof(family)));
}

TEST(FontContextTest, GlyphArenaLimitResetReclaimsArenaChunks) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Arena* arena = arena_create_default(pool);
    ASSERT_NE(arena, nullptr);
    Arena* glyph_arena = arena_create(pool, 4096, 16384);
    ASSERT_NE(glyph_arena, nullptr);

    FontContextConfig cfg = {};
    cfg.pool = pool;
    cfg.arena = arena;
    cfg.glyph_arena = glyph_arena;
    cfg.max_glyph_arena_bytes = arena_total_allocated(glyph_arena);

    FontContext ctx = {};
    ctx.pool = pool;
    ctx.arena = arena;
    ctx.glyph_arena = glyph_arena;
    ctx.glyph_cache_generation = 1;
    ctx.config = cfg;

    size_t initial_allocated = arena_total_allocated(glyph_arena);
    uint64_t initial_generation = font_context_glyph_cache_generation(&ctx);
    ASSERT_NE(arena_alloc(glyph_arena, initial_allocated * 4), nullptr);
    ASSERT_GT(arena_total_allocated(glyph_arena), cfg.max_glyph_arena_bytes);

    EXPECT_TRUE(font_context_enforce_glyph_arena_limit(&ctx));
    EXPECT_GT(font_context_glyph_cache_generation(&ctx), initial_generation);
    EXPECT_EQ(arena_total_allocated(glyph_arena), initial_allocated);
    EXPECT_EQ(arena_total_used(glyph_arena), (size_t)0);

    arena_destroy(glyph_arena);
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(FontContextTest, LongSessionGlyphArenaUsePlateausAtConfiguredLimit) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Arena* arena = arena_create_default(pool);
    ASSERT_NE(arena, nullptr);
    Arena* glyph_arena = arena_create(pool, 4096, 16384);
    ASSERT_NE(glyph_arena, nullptr);

    FontContext ctx = {};
    ctx.pool = pool;
    ctx.arena = arena;
    ctx.glyph_arena = glyph_arena;
    ctx.glyph_cache_generation = 1;
    ctx.config.max_glyph_arena_bytes = arena_total_allocated(glyph_arena);
    size_t plateau = arena_total_allocated(glyph_arena);

    for (int frame = 0; frame < 200; frame++) {
        ASSERT_NE(arena_alloc(glyph_arena, plateau * 4), nullptr);
        ASSERT_TRUE(font_context_enforce_glyph_arena_limit(&ctx));
        EXPECT_EQ(arena_total_allocated(glyph_arena), plateau);
        EXPECT_EQ(arena_total_used(glyph_arena), (size_t)0);
    }
    EXPECT_EQ(font_context_glyph_cache_generation(&ctx), 201u);

    arena_destroy(glyph_arena);
    arena_destroy(arena);
    pool_destroy(pool);
}
