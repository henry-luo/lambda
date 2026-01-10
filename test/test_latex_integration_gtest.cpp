// test_latex_integration_gtest.cpp - LaTeX/TeX integration tests
//
// Phase 1: DVI parsing and glue/unit tests
// Phase 2: Full typesetter integration (when implementation is complete)

#include <gtest/gtest.h>
#include "lambda/tex/tex_glue.hpp"
#include "lambda/tex/dvi_parser.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"
#include <cstring>
#include <cstdio>
#include <sys/stat.h>

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class LaTeXIntegrationTest : public ::testing::Test {
protected:
    Arena* arena;
    Pool* pool;

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
    }

    void TearDown() override {
        arena_destroy(arena);
        pool_destroy(pool);
    }

    // Read a file into arena
    const char* read_file_contents(const char* path, size_t* out_len) {
        FILE* f = fopen(path, "rb");
        if (!f) return nullptr;

        fseek(f, 0, SEEK_END);
        size_t len = ftell(f);
        fseek(f, 0, SEEK_SET);

        char* buf = (char*)arena_alloc(arena, len + 1);
        if (!buf) {
            fclose(f);
            return nullptr;
        }

        fread(buf, 1, len, f);
        buf[len] = '\0';
        fclose(f);

        if (out_len) *out_len = len;
        return buf;
    }

    // Check if file exists
    bool file_exists(const char* path) {
        struct stat st;
        return stat(path, &st) == 0;
    }
};

// ============================================================================
// Glue Tests
// ============================================================================

TEST_F(LaTeXIntegrationTest, GlueCreation) {
    Glue g1 = Glue::fixed(10.0f);
    Glue g2 = Glue::flexible(5.0f, 2.0f, 1.0f);

    EXPECT_FLOAT_EQ(g1.space, 10.0f);
    EXPECT_FLOAT_EQ(g1.stretch, 0.0f);

    EXPECT_FLOAT_EQ(g2.space, 5.0f);
    EXPECT_FLOAT_EQ(g2.stretch, 2.0f);
    EXPECT_FLOAT_EQ(g2.shrink, 1.0f);

    Glue sum = g1 + g2;
    EXPECT_FLOAT_EQ(sum.space, 15.0f);
    EXPECT_FLOAT_EQ(sum.stretch, 2.0f);
}

TEST_F(LaTeXIntegrationTest, InfiniteGlue) {
    Glue fil = Glue::fil(0.0f);
    Glue fill = Glue::fill(0.0f);

    EXPECT_EQ(fil.stretch_order, GlueOrder::Fil);
    EXPECT_EQ(fill.stretch_order, GlueOrder::Fill);

    Glue sum = fil + fill;
    EXPECT_EQ(sum.stretch_order, GlueOrder::Fill);
}

TEST_F(LaTeXIntegrationTest, UnitConversion) {
    float px = pt_to_px(72.27f);
    EXPECT_NEAR(px, 96.0f, 0.5f);

    float bp = bp_to_px(72.0f);
    EXPECT_NEAR(bp, 96.0f, 0.1f);

    float mu = mu_to_px(18.0f, 16.0f);
    EXPECT_NEAR(mu, 16.0f, 0.1f);
}

// ============================================================================
// DVI Parser Tests
// ============================================================================

TEST_F(LaTeXIntegrationTest, DVIParserSimpleMath) {
    const char* dvi_path = "test/latex/reference/test_simple_math.dvi";
    if (!file_exists(dvi_path)) {
        GTEST_SKIP() << "Reference DVI not found: " << dvi_path;
    }

    size_t len;
    const char* data = read_file_contents(dvi_path, &len);
    ASSERT_NE(data, nullptr) << "Failed to read DVI file";

    dvi::DVIParser parser(arena);
    bool ok = parser.parse((const uint8_t*)data, len);
    ASSERT_TRUE(ok) << "Failed to parse DVI file";

    EXPECT_EQ(parser.page_count(), 1);

    const dvi::DVIPage* page = parser.page(0);
    ASSERT_NE(page, nullptr);

    // test_simple_math.tex: $a + b = c$
    EXPECT_EQ(page->glyph_count, 5);
    EXPECT_EQ(page->rule_count, 0);

    log_info("simple_math DVI: %d glyphs, %d rules",
             page->glyph_count, page->rule_count);
}

TEST_F(LaTeXIntegrationTest, DVIParserFraction) {
    const char* dvi_path = "test/latex/reference/test_fraction.dvi";
    if (!file_exists(dvi_path)) {
        GTEST_SKIP() << "Reference DVI not found: " << dvi_path;
    }

    size_t len;
    const char* data = read_file_contents(dvi_path, &len);
    ASSERT_NE(data, nullptr);

    dvi::DVIParser parser(arena);
    bool ok = parser.parse((const uint8_t*)data, len);
    ASSERT_TRUE(ok);

    const dvi::DVIPage* page = parser.page(0);
    ASSERT_NE(page, nullptr);

    EXPECT_GE(page->glyph_count, 6);
    EXPECT_GE(page->rule_count, 1);

    log_info("fraction DVI: %d glyphs, %d rules",
             page->glyph_count, page->rule_count);
}

TEST_F(LaTeXIntegrationTest, DVIParserSqrt) {
    const char* dvi_path = "test/latex/reference/test_sqrt.dvi";
    if (!file_exists(dvi_path)) {
        GTEST_SKIP() << "Reference DVI not found: " << dvi_path;
    }

    size_t len;
    const char* data = read_file_contents(dvi_path, &len);
    ASSERT_NE(data, nullptr);

    dvi::DVIParser parser(arena);
    bool ok = parser.parse((const uint8_t*)data, len);
    ASSERT_TRUE(ok);

    const dvi::DVIPage* page = parser.page(0);
    ASSERT_NE(page, nullptr);

    EXPECT_GE(page->glyph_count, 5);
    EXPECT_GE(page->rule_count, 1);

    log_info("sqrt DVI: %d glyphs, %d rules",
             page->glyph_count, page->rule_count);
}

TEST_F(LaTeXIntegrationTest, DVIParserGreek) {
    const char* dvi_path = "test/latex/reference/test_greek.dvi";
    if (!file_exists(dvi_path)) {
        GTEST_SKIP() << "Reference DVI not found: " << dvi_path;
    }

    size_t len;
    const char* data = read_file_contents(dvi_path, &len);
    ASSERT_NE(data, nullptr);

    dvi::DVIParser parser(arena);
    bool ok = parser.parse((const uint8_t*)data, len);
    ASSERT_TRUE(ok);

    const dvi::DVIPage* page = parser.page(0);
    ASSERT_NE(page, nullptr);

    EXPECT_GE(page->glyph_count, 10);

    log_info("greek DVI: %d glyphs, %d rules",
             page->glyph_count, page->rule_count);
}

TEST_F(LaTeXIntegrationTest, DVIParserSumIntegral) {
    const char* dvi_path = "test/latex/reference/test_sum_integral.dvi";
    if (!file_exists(dvi_path)) {
        GTEST_SKIP() << "Reference DVI not found: " << dvi_path;
    }

    size_t len;
    const char* data = read_file_contents(dvi_path, &len);
    ASSERT_NE(data, nullptr);

    dvi::DVIParser parser(arena);
    bool ok = parser.parse((const uint8_t*)data, len);
    ASSERT_TRUE(ok);

    const dvi::DVIPage* page = parser.page(0);
    ASSERT_NE(page, nullptr);

    EXPECT_GE(page->glyph_count, 10);

    log_info("sum_integral DVI: %d glyphs, %d rules",
             page->glyph_count, page->rule_count);
}

TEST_F(LaTeXIntegrationTest, DVIParserDelimiters) {
    const char* dvi_path = "test/latex/reference/test_delimiters.dvi";
    if (!file_exists(dvi_path)) {
        GTEST_SKIP() << "Reference DVI not found: " << dvi_path;
    }

    size_t len;
    const char* data = read_file_contents(dvi_path, &len);
    ASSERT_NE(data, nullptr);

    dvi::DVIParser parser(arena);
    bool ok = parser.parse((const uint8_t*)data, len);
    ASSERT_TRUE(ok);

    const dvi::DVIPage* page = parser.page(0);
    ASSERT_NE(page, nullptr);

    EXPECT_GE(page->glyph_count, 10);

    log_info("delimiters DVI: %d glyphs, %d rules",
             page->glyph_count, page->rule_count);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    log_init("log.conf");
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
