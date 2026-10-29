#include <gtest/gtest.h>

#include <cstring>

#include "../../lib/font/font_internal.h"
#include "../../lib/font/font_glyf.h"
#include "../../lib/font/font_tables.h"
#include "../../lib/font/font_gsub.h"
#include "../../lib/font/font_math.h"
#include "../../lib/font/font_export.h"
#include "../../lib/endian.h"
#include "../../lib/arena.h"
#include "../../lib/hashmap_helpers.h"
#include "../../lib/utf.h"

class FontVerticalTablesTest : public ::testing::Test {
protected:
    uint8_t data[512] = {};
    FontTableDir directories[8] = {};
    FontTables tables = {};
    MaxpTable maxp = {};
    HeadTable head = {};
    Pool* pool = nullptr;
    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr);
        maxp.num_glyphs = 16;
        head.units_per_em = 1000;
        head.index_to_loc_format = 1;
        tables.data = data;
        tables.data_len = sizeof(data);
        tables.dirs = directories;
        tables.maxp = &maxp;
        tables.head = &head;
        tables.parsed_flags = FONT_PARSED_MAXP | FONT_PARSED_HEAD;
        tables.pool = pool;
    }
    void TearDown() override { pool_destroy(pool); }
    void table(uint32_t tag, uint32_t offset, uint32_t length) {
        directories[tables.num_tables++] = {tag, 0, offset, length};
    }
    void metrics() {
        maxp.num_glyphs = 4;
        table(FONT_TAG('v','h','e','a'), 0, 36);
        table(FONT_TAG('v','m','t','x'), 36, 12);
        write_be16(data + 34, 2);
        write_be16(data + 36, 1000); write_be16(data + 38, 100);
        write_be16(data + 40, 1200); write_be16(data + 42, 20);
        write_be16(data + 44, 30); write_be16(data + 46, (uint16_t)-10);
    }
    void substitutions(bool extension = false) {
        table(FONT_TAG('G','S','U','B'), 0, 144);
        write_be16(data, 1); write_be16(data + 4, 10);
        write_be16(data + 6, 12); write_be16(data + 8, 52);
        write_be16(data + 12, 3);
        // two script feature records share vert; vrt2 is a separate rotation feature.
        write_be32(data + 14, FONT_TAG('v','e','r','t')); write_be16(data + 18, 20);
        write_be32(data + 20, FONT_TAG('v','e','r','t')); write_be16(data + 24, 28);
        write_be32(data + 26, FONT_TAG('v','r','t','2')); write_be16(data + 30, 34);
        write_be16(data + 34, 2); write_be16(data + 36, 0); write_be16(data + 38, 2);
        write_be16(data + 42, 1); write_be16(data + 44, 0);
        write_be16(data + 48, 1); write_be16(data + 50, 1);
        write_be16(data + 52, 3);
        write_be16(data + 54, 8); write_be16(data + 56, 32); write_be16(data + 58, 56);
        for (size_t lookup : {60u, 84u, 108u}) {
            write_be16(data + lookup, 1); write_be16(data + lookup + 4, 1);
            write_be16(data + lookup + 6, 8);
        }
        // format 1: glyphs 5..7 each advance by one; applying a duplicate lookup would change the result.
        for (size_t single : {68u, 92u}) {
            write_be16(data + single, 1); write_be16(data + single + 2, 6);
            write_be16(data + single + 4, single == 68 ? 1 : 4);
            write_be16(data + single + 6, 2); write_be16(data + single + 8, 1);
            write_be16(data + single + 10, 5); write_be16(data + single + 12, 7);
        }
        // format 2: 6 -> 8, 7 -> 9, followed by range coverage.
        write_be16(data + 116, 2); write_be16(data + 118, 10); write_be16(data + 120, 2);
        write_be16(data + 122, 8); write_be16(data + 124, 9);
        write_be16(data + 126, 2); write_be16(data + 128, 1);
        write_be16(data + 130, 6); write_be16(data + 132, 7);
        if (extension) {
            // two independent extension siblings: the first is uncovered, the second substitutes glyph 5.
            write_be16(data + 60, 7); write_be16(data + 64, 2);
            write_be16(data + 66, 10); write_be16(data + 68, 18);
            for (size_t wrapper : {70u, 78u}) {
                write_be16(data + wrapper, 1); write_be16(data + wrapper + 2, 1);
                write_be32(data + wrapper + 4, wrapper == 70 ? 66 : 14);
            }
            write_be16(data + 136, 1); write_be16(data + 138, 6);
            write_be16(data + 140, 1); write_be16(data + 142, 1);
            write_be16(data + 144, 1); write_be16(data + 146, 15);
            directories[0].length = 148;
        }
    }
};

TEST(FontTableProviders, CopiesRequestedTablesOnceAndReleasesTheirSelectedOwner) {
    Pool* pool = pool_create(); ASSERT_NE(pool, nullptr);
    struct Owner { unsigned copies, releases; } owner = {};
    auto copy = [](void* pointer, uint32_t tag, uint32_t* length, void* pool) -> uint8_t* {
        Owner* owner = (Owner*)pointer; owner->copies++;
        uint8_t* bytes = (uint8_t*)pool_calloc((Pool*)pool, 6);
        if (bytes) { bytes[4] = 1; bytes[5] = 7; *length = 6; }
        return bytes;
    };
    auto release = [](void* pointer) { ((Owner*)pointer)->releases++; };
    uint32_t tag = FONT_TAG('m','a','x','p');
    FontTables* tables = font_tables_open_provider(&tag, 1, 0x10000, copy, release, &owner, pool);
    ASSERT_NE(tables, nullptr);
    EXPECT_TRUE(font_tables_has(tables, tag));
    EXPECT_EQ(owner.copies, 0u);
    ASSERT_NE(font_tables_get_maxp(tables), nullptr);
    EXPECT_EQ(font_tables_get_maxp(tables)->num_glyphs, 263);
    EXPECT_EQ(owner.copies, 1u);
    EXPECT_EQ(font_tables_find(tables, FONT_TAG('v','m','t','x'), nullptr), nullptr);
    EXPECT_EQ(owner.copies, 1u);
    font_tables_close(tables, pool);
    EXPECT_EQ(owner.releases, 1u);
    pool_destroy(pool);
}

TEST_F(FontVerticalTablesTest, ReusesTrailingAdvanceAndKeepsPerGlyphVerticalOrigins) {
    metrics();
    table(FONT_TAG('C','F','F',' '), 60, 1);
    table(FONT_TAG('V','O','R','G'), 48, 12);
    write_be16(data + 48, 1); write_be16(data + 52, 880); write_be16(data + 54, 1);
    write_be16(data + 56, 2); write_be16(data + 58, 760);
    FontVerticalMetrics result;
    for (uint16_t glyph = 0; glyph < 4; glyph++) {
        ASSERT_TRUE(font_tables_vertical_metrics(&tables, glyph, &result));
        EXPECT_EQ(result.advance, glyph == 0 ? 1000 : 1200);
        const int bearings[] = {100, 20, 30, -10};
        EXPECT_EQ(result.top_bearing, bearings[glyph]);
        EXPECT_TRUE(result.has_origin);
        EXPECT_EQ(result.origin_y, glyph == 2 ? 760 : 880);
    }
    EXPECT_FALSE(font_tables_vertical_metrics(&tables, 4, &result));
    // a TrueType glyph's declared ymax plus bearing overrides a stray VORG table.
    directories[2].tag = FONT_TAG('g','l','y','f'); directories[2].offset = 64; directories[2].length = 40;
    table(FONT_TAG('l','o','c','a'), 104, 20);
    for (uint32_t i = 0; i <= 4; i++) write_be32(data + 104 + i * 4, i * 10);
    for (uint32_t i = 0; i < 4; i++) write_be16(data + 64 + i * 10 + 8, 700);
    ASSERT_TRUE(font_tables_vertical_metrics(&tables, 2, &result));
    EXPECT_TRUE(result.has_origin);
    EXPECT_EQ(result.origin_y, 730);
}

TEST_F(FontVerticalTablesTest, RejectsTruncatedMetricArraysAndInvalidCounts) {
    metrics();
    for (uint16_t count : {0, 2, 5}) {
        tables.vmtx = nullptr; tables.parsed_flags &= ~FONT_PARSED_VMTX;
        write_be16(data + 34, count); directories[1].length = 11;
        EXPECT_EQ(font_tables_get_vmtx(&tables), nullptr);
    }
}

TEST_F(FontVerticalTablesTest, ActivatesVertOnceWithoutVrt2AndPreservesMissingCoverage) {
    substitutions();
    GsubTable* gsub = font_tables_get_gsub(&tables);
    ASSERT_NE(gsub, nullptr);
    uint16_t glyph = 0;
    ASSERT_TRUE(gsub_vertical_glyph(gsub, 5, &glyph)); EXPECT_EQ(glyph, 8);
    ASSERT_TRUE(gsub_vertical_glyph(gsub, 6, &glyph)); EXPECT_EQ(glyph, 9);
    ASSERT_TRUE(gsub_vertical_glyph(gsub, 12, &glyph)); EXPECT_EQ(glyph, 12);
    write_be16(data + 122, 99);
    EXPECT_FALSE(gsub_vertical_glyph(gsub, 5, &glyph));
}

TEST_F(FontVerticalTablesTest, ResolvesEachExtensionSiblingAndRejectsWrappedOffsets) {
    substitutions(true);
    GsubTable* gsub = font_tables_get_gsub(&tables);
    ASSERT_NE(gsub, nullptr);
    uint16_t glyph = 0;
    ASSERT_TRUE(gsub_vertical_glyph(gsub, 5, &glyph)); EXPECT_EQ(glyph, 9);
    write_be32(data + 74, 0xfffffff0u);
    EXPECT_FALSE(gsub_vertical_glyph(gsub, 5, &glyph));
}

TEST(UnicodeVerticalOrientationTest, UsesUax50PropertiesRatherThanScriptOrWidthHeuristics) {
    EXPECT_EQ(utf_vertical_orientation('A'), UTF_VERTICAL_ROTATED);
    EXPECT_EQ(utf_vertical_orientation(0x6c34), UTF_VERTICAL_UPRIGHT);
    EXPECT_EQ(utf_vertical_orientation(0x3001), UTF_VERTICAL_TRANSFORMED_UPRIGHT);
    EXPECT_EQ(utf_vertical_orientation(0x300c), UTF_VERTICAL_TRANSFORMED_ROTATED);
    EXPECT_EQ(utf_vertical_orientation(0xff21), UTF_VERTICAL_UPRIGHT);
    EXPECT_EQ(utf_vertical_orientation(0x110000), UTF_VERTICAL_ROTATED);
}

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

TEST_F(FontMathTest, ExportsOwnedStandaloneFontWithValidChecksumAndGlyphMetrics) {
    load("test/lambda/math/fonts/NotoSansMath-Regular.ttf");
    FontHandle handle = {}; handle.tables = tables;
    FontExport exported = {}; const char* error = nullptr;
    ASSERT_TRUE(font_export_face(&handle, &exported, &error)) << (error ? error : "");
    EXPECT_NE(exported.bytes, bytes);
    uint32_t checksum = 0;
    for (size_t offset = 0; offset < exported.length; offset += 4) checksum += read_be32(exported.bytes + offset);
    EXPECT_EQ(checksum, 0xB1B0AFBAu);
    FontTables* reopened = font_tables_open(exported.bytes, exported.length, pool);
    ASSERT_NE(reopened, nullptr);
    uint16_t glyph = cmap_lookup(font_tables_get_cmap(tables), 'W');
    EXPECT_EQ(cmap_lookup(font_tables_get_cmap(reopened), 'W'), glyph);
    EXPECT_FLOAT_EQ(font_export_glyph_width(&handle, glyph),
        1000.0f * hmtx_get_advance(font_tables_get_hmtx(tables), glyph) / font_tables_get_head(tables)->units_per_em);
    EXPECT_EQ(font_export_italic_shear(&handle), 0);
    handle.slant = FONT_SLANT_ITALIC;
    EXPECT_FLOAT_EQ(font_export_italic_shear(&handle), FONT_SYNTHETIC_ITALIC_SHEAR);
    font_tables_close(reopened, pool);
    font_export_free(&exported);
}

TEST_F(FontMathTest, CompleteEmbeddingHonorsPermissionsAndTableExtents) {
    load("test/lambda/math/fonts/NotoSansMath-Regular.ttf");
    FontHandle handle = {}; handle.tables = tables;
    Os2Table* os2 = font_tables_get_os2(tables); ASSERT_NE(os2, nullptr);
    FontExport exported = {}; const char* error = nullptr;
    os2->fs_type = 2;
    EXPECT_FALSE(font_export_face(&handle, &exported, &error));
    EXPECT_EQ(exported.bytes, nullptr);
    os2->fs_type = 0x100; // complete embedding remains valid when subsetting is forbidden.
    ASSERT_TRUE(font_export_face(&handle, &exported, &error));
    font_export_free(&exported);
    // native-instance conversion emits selected glyph programs, so it cannot ignore no-subsetting.
    uint32_t tag = tables->dirs[0].tag;
    tables->dirs[0].tag = FONT_TAG('f','v','a','r');
    EXPECT_FALSE(font_export_face(&handle, &exported, &error));
    tables->dirs[0].tag = tag;
    tables->dirs[0].offset = UINT32_MAX;
    EXPECT_FALSE(font_export_face(&handle, &exported, &error));
    EXPECT_EQ(exported.bytes, nullptr);
}

TEST_F(FontMathTest, LegacyEmbeddingFlagsKeepPreviewAndEditablePermissions) {
    load("test/lambda/math/fonts/NotoSansMath-Regular.ttf");
    FontHandle handle = {}; handle.tables = tables;
    Os2Table* os2 = font_tables_get_os2(tables); ASSERT_NE(os2, nullptr);
    uint32_t length = 0;
    uint8_t* raw = (uint8_t*)font_tables_find(tables, FONT_TAG('O','S','/','2'), &length);
    ASSERT_NE(raw, nullptr); ASSERT_GE(length, 10u);
    os2->version = 2;
    write_be16(raw, 2);
    for (uint16_t permissions : {0x0006, 0x000a, 0x000e}) {
        os2->fs_type = permissions;
        write_be16(raw + 8, permissions);
        FontExport exported = {}; const char* error = nullptr;
        ASSERT_TRUE(font_export_face(&handle, &exported, &error)) << (error ? error : "");
        FontTables* reopened = font_tables_open(exported.bytes, exported.length, pool);
        ASSERT_NE(reopened, nullptr);
        EXPECT_EQ(font_tables_get_os2(reopened)->fs_type, permissions);
        font_tables_close(reopened, pool);
        font_export_free(&exported);
    }
}

TEST_F(FontMathTest, ExtractsCollectionFaceWithStandaloneOffsets) {
    load("test/lambda/math/fonts/NotoSansMath-Regular.ttf");
    size_t length = tables->data_len;
    uint8_t* collection = (uint8_t*)calloc(1, length + 32); ASSERT_NE(collection, nullptr);
    memcpy(collection, "ttcf", 4); write_be32(collection + 4, 0x00010000);
    write_be32(collection + 8, 1); write_be32(collection + 12, 32);
    memcpy(collection + 32, bytes, length);
    for (uint16_t i = 0; i < tables->num_tables; i++) {
        uint8_t* offset = collection + 32 + 12 + (size_t)i * 16 + 8;
        write_be32(offset, read_be32(offset) + 32);
    }
    FontTables* face = font_tables_open_face(collection, length + 32, 0, pool); ASSERT_NE(face, nullptr);
    FontHandle handle = {}; handle.tables = face;
    FontExport exported = {}; const char* error = nullptr;
    ASSERT_TRUE(font_export_face(&handle, &exported, &error)) << (error ? error : "");
    FontTables* reopened = font_tables_open(exported.bytes, exported.length, pool); ASSERT_NE(reopened, nullptr);
    EXPECT_EQ(cmap_lookup(font_tables_get_cmap(reopened), 'W'), cmap_lookup(font_tables_get_cmap(tables), 'W'));
    font_tables_close(reopened, pool); font_tables_close(face, pool);
    font_export_free(&exported); free(collection);
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
