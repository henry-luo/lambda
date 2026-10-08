#include <gtest/gtest.h>

#include <cstring>

#include "../../lib/font/font_internal.h"
#include "../../lib/font/font_glyf.h"
#include "../../lib/font/font_tables.h"
#include "../../lib/font/font_math.h"
#include "../../lib/endian.h"
#include "../../lib/arena.h"
#include "../../lib/hashmap_helpers.h"

class FontMathTest : public ::testing::Test {
protected:
    Pool* pool = nullptr;
    uint8_t* bytes = nullptr;
    FontTables* tables = nullptr;
    FontMathTable math = {};

    void load(const char* path, bool expect_math = true) {
        FILE* file = fopen(path, "rb");
        ASSERT_NE(file, nullptr);
        ASSERT_EQ(fseek(file, 0, SEEK_END), 0);
        long length = ftell(file);
        ASSERT_GT(length, 0);
        rewind(file);
        bytes = (uint8_t*)malloc((size_t)length);
        ASSERT_NE(bytes, nullptr);
        ASSERT_EQ(fread(bytes, 1, (size_t)length, file), (size_t)length);
        fclose(file);
        pool = pool_create();
        ASSERT_NE(pool, nullptr);
        tables = font_tables_open(bytes, (size_t)length, pool);
        ASSERT_NE(tables, nullptr);
        ASSERT_EQ(font_math_open(tables, &math), expect_math);
    }

    void TearDown() override {
        if (tables) font_tables_close(tables, pool);
        if (pool) pool_destroy(pool);
        free(bytes);
    }
};

TEST_F(FontMathTest, ReadsIndependentFontConstants) {
    load("test/lambda/math/fonts/NotoSansMath-Regular.ttf");
    ASSERT_NE(tables, nullptr);
    int32_t value = 0;
    ASSERT_TRUE(font_math_constant(&math, FONT_MATH_axis_height, &value));
    EXPECT_EQ(value, 278);
    ASSERT_TRUE(font_math_constant(&math, FONT_MATH_radical_kern_after_degree, &value));
    EXPECT_LT(value, 0);
    EXPECT_TRUE(font_math_constant_is_percent(FONT_MATH_script_percent_scale_down));
    EXPECT_FALSE(font_math_constant_is_percent(FONT_MATH_axis_height));
}

TEST_F(FontMathTest, MissingMathIsAnOrdinaryFontCapability) {
    load("test/ui/svg_font_assets/rectangle.ttf", false);
    ASSERT_NE(tables, nullptr);
    EXPECT_EQ(math.data, nullptr);
    EXPECT_NE(font_tables_get_hmtx(tables), nullptr);
}

TEST_F(FontMathTest, ReadsGlyphVariantsAndConnectors) {
    load("test/lambda/math/fonts/NotoSansMath-Regular.ttf");
    ASSERT_NE(tables, nullptr);
    CmapTable* cmap = font_tables_get_cmap(tables);
    ASSERT_NE(cmap, nullptr);
    FontMathConstruction construction;
    ASSERT_TRUE(font_math_construction(&math, cmap_lookup(cmap, '('), true, &construction));
    ASSERT_GT(construction.variant_count, 1);
    ASSERT_GT(construction.part_count, 0);
    FontMathVariant first, last;
    ASSERT_TRUE(font_math_variant(&construction, 0, &first));
    ASSERT_TRUE(font_math_variant(&construction, construction.variant_count - 1, &last));
    EXPECT_GT(last.advance, first.advance);
    bool extender = false;
    for (uint16_t index = 0; index < construction.part_count; index++) {
        FontMathPart part;
        ASSERT_TRUE(font_math_part(&construction, index, &part));
        EXPECT_LE(part.start_connector, part.advance);
        EXPECT_LE(part.end_connector, part.advance);
        extender = extender || part.extender;
    }
    EXPECT_TRUE(extender);
    FontMathGlyph glyph;
    ASSERT_TRUE(font_math_glyph(&math, cmap_lookup(cmap, 0x1d453), &glyph));
    EXPECT_TRUE(glyph.has_italic);
    EXPECT_TRUE(glyph.has_accent);
}

TEST_F(FontMathTest, RejectsTruncatedMathStructures) {
    load("test/lambda/math/fonts/NotoSansMath-Regular.ttf");
    ASSERT_NE(tables, nullptr);
    FontMathTable truncated = math;
    for (uint32_t length = 0; length < 10; length++) {
        truncated.length = length;
        int32_t value;
        FontMathGlyph glyph;
        FontMathConstruction construction;
        EXPECT_FALSE(font_math_constant(&truncated, FONT_MATH_axis_height, &value));
        EXPECT_FALSE(font_math_glyph(&truncated, 1, &glyph));
        EXPECT_FALSE(font_math_construction(&truncated, 1, true, &construction));
    }
    // a valid directory must not let an overflowing subtable escape its owner.
    write_be16((uint8_t*)math.data + 4, 0xffff);
    truncated.length = 1000;
    int32_t value;
    EXPECT_FALSE(font_math_constant(&truncated, FONT_MATH_axis_height, &value));
}

TEST_F(FontMathTest, RejectsMalformedCoverageInsteadOfMissingGlyph) {
    load("test/lambda/math/fonts/NotoSansMath-Regular.ttf");
    ASSERT_NE(tables, nullptr);
    uint8_t* info = (uint8_t*)math.data + read_be16(math.data + 6);
    uint8_t* italics = info + read_be16(info);
    uint8_t* coverage = italics + read_be16(italics);
    write_be16(coverage, 99);
    FontMathGlyph glyph;
    EXPECT_FALSE(font_math_glyph(&math, 1, &glyph));
}

static uint64_t test_loaded_glyph_cache_hash(const void* item,
                                             uint64_t seed0, uint64_t seed1) {
    const LoadedGlyphCacheEntry* entry = (const LoadedGlyphCacheEntry*)item;
    uint64_t data[2];
    data[0] = (uint64_t)(uintptr_t)entry->caller_handle;
    data[1] = ((uint64_t)entry->codepoint << 2) |
              ((uint64_t)(entry->for_rendering ? 1 : 0) << 1) |
              (uint64_t)(entry->emoji_presentation ? 1 : 0);
    return hashmap_hash_xxhash3_bytes(data, sizeof(data), seed0, seed1);
}

static int test_loaded_glyph_cache_compare(const void* left, const void* right,
                                           void* context) {
    (void)context;
    const LoadedGlyphCacheEntry* a = (const LoadedGlyphCacheEntry*)left;
    const LoadedGlyphCacheEntry* b = (const LoadedGlyphCacheEntry*)right;
    if (a->caller_handle != b->caller_handle) {
        return a->caller_handle < b->caller_handle ? -1 : 1;
    }
    if (a->codepoint != b->codepoint) return a->codepoint < b->codepoint ? -1 : 1;
    if (a->for_rendering != b->for_rendering) return a->for_rendering ? 1 : -1;
    if (a->emoji_presentation != b->emoji_presentation) {
        return a->emoji_presentation ? 1 : -1;
    }
    return 0;
}

TEST(FontSecurityTest, RejectsWrappedTableRange) {
    uint8_t data[32] = {0};
    FontTableDir directory = {};
    directory.tag = FONT_TAG('g', 'l', 'y', 'f');
    directory.offset = 0xfffffff0u;
    directory.length = 0x20u;
    FontTables tables = {};
    tables.data = data;
    tables.data_len = sizeof(data);
    tables.dirs = &directory;
    tables.num_tables = 1;
    EXPECT_EQ(font_tables_find(&tables, directory.tag, NULL), nullptr);
}

static void test_font_security_tables(FontTables* tables, FontTableDir directories[2],
                                      HeadTable* head, MaxpTable* maxp,
                                      const uint8_t* data, size_t data_len,
                                      uint32_t glyph_len) {
    memset(tables, 0, sizeof(*tables));
    memset(directories, 0, sizeof(FontTableDir) * 2);
    head->index_to_loc_format = 1;
    maxp->num_glyphs = 1;
    directories[0].tag = FONT_TAG('l', 'o', 'c', 'a');
    directories[0].offset = 0;
    directories[0].length = 8;
    directories[1].tag = FONT_TAG('g', 'l', 'y', 'f');
    directories[1].offset = 8;
    directories[1].length = glyph_len;
    tables->data = data;
    tables->data_len = data_len;
    tables->dirs = directories;
    tables->num_tables = 2;
    tables->head = head;
    tables->maxp = maxp;
    tables->parsed_flags = FONT_PARSED_HEAD | FONT_PARSED_MAXP;
}

TEST(FontSecurityTest, RejectsCyclicCompoundGlyph) {
    uint8_t data[24] = {0};
    data[7] = 16;                 // long loca: glyph 0 spans [0, 16)
    data[8] = 0xff; data[9] = 0xff; // compound glyph marker
    data[18] = 0; data[19] = 2;  // component args are XY byte values
    FontTables tables = {};
    FontTableDir directories[2] = {};
    HeadTable head = {};
    MaxpTable maxp = {};
    test_font_security_tables(&tables, directories, &head, &maxp,
                              data, sizeof(data), 16);
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);
    GlyphOutline outline = {};
    EXPECT_EQ(glyf_get_outline(&tables, 0, &outline, arena), -1);
    arena_destroy(arena);
}

TEST(FontSecurityTest, RejectsNonMonotonicContourEndpoints) {
    uint8_t data[22] = {0};
    data[7] = 14;                 // long loca: glyph 0 spans [0, 14)
    data[8] = 0; data[9] = 2;    // two simple contours
    data[18] = 0; data[19] = 2;
    data[20] = 0; data[21] = 1;  // second endpoint moves backwards
    FontTables tables = {};
    FontTableDir directories[2] = {};
    HeadTable head = {};
    MaxpTable maxp = {};
    test_font_security_tables(&tables, directories, &head, &maxp,
                              data, sizeof(data), 14);
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);
    GlyphOutline outline = {};
    EXPECT_EQ(glyf_get_outline(&tables, 0, &outline, arena), -1);
    arena_destroy(arena);
}

TEST(FontSecurityTest, RejectsFlagRepeatPastPointCount) {
    uint8_t data[24] = {0};
    data[7] = 16;                 // long loca: glyph 0 spans [0, 16)
    data[8] = 0; data[9] = 1;    // one simple contour
    data[18] = 0; data[19] = 0;  // one point
    data[22] = 0x38;              // repeated flag with unchanged x/y
    data[23] = 1;                 // repeats beyond the declared point count
    FontTables tables = {};
    FontTableDir directories[2] = {};
    HeadTable head = {};
    MaxpTable maxp = {};
    test_font_security_tables(&tables, directories, &head, &maxp,
                              data, sizeof(data), 16);
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);
    GlyphOutline outline = {};
    EXPECT_EQ(glyf_get_outline(&tables, 0, &outline, arena), -1);
    arena_destroy(arena);
}

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
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);
    Arena* glyph_arena = arena_create(4096, 16384);
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

TEST(FontContextTest, GlyphCacheResetDropsReusedDocumentHandleEntry) {
    FontContext ctx = {};
    ctx.glyph_cache_generation = 1;
    ctx.loaded_glyph_cache = hashmap_new(
        sizeof(LoadedGlyphCacheEntry), 8, 0, 0,
        test_loaded_glyph_cache_hash, test_loaded_glyph_cache_compare, NULL, NULL);
    ASSERT_NE(ctx.loaded_glyph_cache, nullptr);

    FontHandle recycled_document_handle = {};
    LoadedGlyphCacheEntry cached = {};
    cached.caller_handle = &recycled_document_handle;
    cached.codepoint = 'A';
    cached.for_rendering = true;
    cached.glyph.advance_x = 37.0f;
    ASSERT_EQ(hashmap_set(ctx.loaded_glyph_cache, &cached), nullptr);

    LoadedGlyphCacheEntry lookup = {};
    lookup.caller_handle = &recycled_document_handle;
    lookup.codepoint = 'A';
    lookup.for_rendering = true;
    const LoadedGlyphCacheEntry* stale =
        (const LoadedGlyphCacheEntry*)hashmap_get(ctx.loaded_glyph_cache, &lookup);
    ASSERT_NE(stale, nullptr);
    EXPECT_FLOAT_EQ(stale->glyph.advance_x, 37.0f);

    // Iframe navigation can recycle a freed document handle at this address.
    font_context_reset_glyph_caches(&ctx);

    EXPECT_EQ(hashmap_count(ctx.loaded_glyph_cache), (size_t)0);
    EXPECT_EQ(hashmap_get(ctx.loaded_glyph_cache, &lookup), nullptr);
    EXPECT_EQ(font_context_glyph_cache_generation(&ctx), 2u);

    hashmap_free(ctx.loaded_glyph_cache);
}

TEST(FontContextTest, LiveHandleRegistryRetainsDetachedCacheOwnersForTeardown) {
    FontContext ctx = {};
    FontHandle cache_replaced_handle = {};
    FontHandle active_cache_handle = {};

    // A face-cache replacement must not make the caller-owned old handle
    // unreachable before FontContext teardown releases its external resources.
    font_context_track_handle(&ctx, &cache_replaced_handle);
    font_context_track_handle(&ctx, &active_cache_handle);

    EXPECT_EQ(font_context_take_live_handle(&ctx), &active_cache_handle);
    EXPECT_EQ(font_context_take_live_handle(&ctx), &cache_replaced_handle);
    EXPECT_EQ(font_context_take_live_handle(&ctx), nullptr);
}

TEST(FontContextTest, LongSessionGlyphArenaUsePlateausAtConfiguredLimit) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);
    Arena* glyph_arena = arena_create(4096, 16384);
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
