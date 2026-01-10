// test_tex_simple_math_gtest.cpp - Test simple math typesetter against DVI
//
// Validates our minimal math typesetter produces output comparable to TeX DVI.

#include <gtest/gtest.h>
#include "lambda/tex/tex_simple_math.hpp"
#include "lambda/tex/dvi_parser.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <sys/stat.h>

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class SimpleMathTest : public ::testing::Test {
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

    // Read file into arena
    const char* read_file_contents(const char* path, size_t* out_len) {
        FILE* f = fopen(path, "rb");
        if (!f) return nullptr;
        fseek(f, 0, SEEK_END);
        size_t len = ftell(f);
        fseek(f, 0, SEEK_SET);
        char* buf = (char*)arena_alloc(arena, len + 1);
        fread(buf, 1, len, f);
        buf[len] = '\0';
        fclose(f);
        if (out_len) *out_len = len;
        return buf;
    }

    bool file_exists(const char* path) {
        struct stat st;
        return stat(path, &st) == 0;
    }

    // Convert DVI scaled points to pt
    float sp_to_pt(int32_t sp) {
        // 1 pt = 65536 sp
        return sp / 65536.0f;
    }

    // Print DVI glyphs for debugging
    void print_dvi_glyphs(const dvi::DVIPage* page) {
        log_info("DVI page: %d glyphs, %d rules", page->glyph_count, page->rule_count);
        for (int i = 0; i < page->glyph_count && i < 10; i++) {
            const dvi::PositionedGlyph& g = page->glyphs[i];
            log_info("  [%d] cp=%d h=%.2f v=%.2f font=%d",
                     i, g.codepoint, sp_to_pt(g.h), sp_to_pt(g.v), g.font_num);
        }
    }

    // Print Lambda output for debugging
    void print_lambda_output(const TypesetOutput* out) {
        log_info("Lambda output: %d glyphs, %d rules", out->glyph_count, out->rule_count);
        for (int i = 0; i < out->glyph_count && i < 10; i++) {
            const PositionedGlyph& g = out->glyphs[i];
            log_info("  [%d] cp=%d x=%.2f y=%.2f font=%s",
                     i, g.codepoint, g.x, g.y, g.font);
        }
    }
};

// ============================================================================
// Basic Math Typesetting Tests
// ============================================================================

TEST_F(SimpleMathTest, TypesetSimpleAddition) {
    TypesetOutput* out = typeset_simple_math("a+b", 10.0f, 0, 0, arena);

    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out->glyph_count, 3);  // a, +, b

    // Check characters
    EXPECT_EQ(out->glyphs[0].codepoint, 'a');
    EXPECT_EQ(out->glyphs[1].codepoint, '+');
    EXPECT_EQ(out->glyphs[2].codepoint, 'b');

    // Check that positions are increasing
    EXPECT_LT(out->glyphs[0].x, out->glyphs[1].x);
    EXPECT_LT(out->glyphs[1].x, out->glyphs[2].x);
}

TEST_F(SimpleMathTest, TypesetEquation) {
    TypesetOutput* out = typeset_simple_math("a+b=c", 10.0f, 0, 0, arena);

    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out->glyph_count, 5);  // a, +, b, =, c
}

TEST_F(SimpleMathTest, MathSpacing) {
    // Binary operators should have medium space (4mu) on each side
    // Relations should have thick space (5mu) on each side
    TypesetOutput* out = typeset_simple_math("a+b=c", 10.0f, 0, 0, arena);

    ASSERT_NE(out, nullptr);
    ASSERT_EQ(out->glyph_count, 5);

    // Get widths
    float a_width = get_math_symbol_width('a', 10.0f);
    float plus_width = get_math_symbol_width('+', 10.0f);
    float b_width = get_math_symbol_width('b', 10.0f);

    // Position of '+' should be > a_width (with spacing)
    EXPECT_GT(out->glyphs[1].x, a_width);

    // There should be spacing before '='
    float b_end = out->glyphs[2].x + b_width;
    EXPECT_GT(out->glyphs[3].x, b_end);
}

TEST_F(SimpleMathTest, ClassifyMathChars) {
    EXPECT_EQ(classify_math_char('a'), SimpleMathAtom::Ord);
    EXPECT_EQ(classify_math_char('1'), SimpleMathAtom::Ord);
    EXPECT_EQ(classify_math_char('+'), SimpleMathAtom::Bin);
    EXPECT_EQ(classify_math_char('-'), SimpleMathAtom::Bin);
    EXPECT_EQ(classify_math_char('='), SimpleMathAtom::Rel);
    EXPECT_EQ(classify_math_char('('), SimpleMathAtom::Open);
    EXPECT_EQ(classify_math_char(')'), SimpleMathAtom::Close);
}

TEST_F(SimpleMathTest, FontMetrics) {
    SimpleFontMetrics m10 = get_cmr_metrics(10.0f);
    SimpleFontMetrics m12 = get_cmr_metrics(12.0f);

    // 12pt should be 1.2x 10pt
    EXPECT_NEAR(m12.quad / m10.quad, 1.2f, 0.01f);
    EXPECT_NEAR(m12.x_height / m10.x_height, 1.2f, 0.01f);
}

// ============================================================================
// Fraction Tests
// ============================================================================

TEST_F(SimpleMathTest, TypesetSimpleFraction) {
    TypesetOutput* out = create_typeset_output(arena);

    float width = typeset_fraction("a", "b", 10.0f, 0, 0, out, arena);

    EXPECT_GT(width, 0);
    EXPECT_GE(out->glyph_count, 2);  // a and b
    EXPECT_EQ(out->rule_count, 1);   // fraction bar
}

TEST_F(SimpleMathTest, TypesetComplexFraction) {
    TypesetOutput* out = create_typeset_output(arena);

    float width = typeset_fraction("a+b", "c+d", 10.0f, 0, 0, out, arena);

    EXPECT_GT(width, 0);
    EXPECT_GE(out->glyph_count, 6);  // a, +, b, c, +, d
    EXPECT_EQ(out->rule_count, 1);   // fraction bar

    // Check that rule exists
    EXPECT_GT(out->rules[0].width, 0);
    EXPECT_GT(out->rules[0].height, 0);
}

// ============================================================================
// Square Root Tests
// ============================================================================

TEST_F(SimpleMathTest, TypesetSqrt) {
    TypesetOutput* out = create_typeset_output(arena);

    float width = typeset_sqrt("x", 10.0f, 0, 0, out, arena);

    EXPECT_GT(width, 0);
    EXPECT_GE(out->glyph_count, 1);  // x
    EXPECT_EQ(out->rule_count, 1);   // vinculum
}

// ============================================================================
// DVI Comparison Tests (Structure Only)
// ============================================================================

TEST_F(SimpleMathTest, CompareSimpleMathWithDVI) {
    const char* dvi_path = "test/latex/reference/test_simple_math.dvi";
    if (!file_exists(dvi_path)) {
        GTEST_SKIP() << "Reference DVI not found: " << dvi_path;
    }

    size_t len;
    const char* data = read_file_contents(dvi_path, &len);
    ASSERT_NE(data, nullptr);

    dvi::DVIParser parser(arena);
    ASSERT_TRUE(parser.parse((const uint8_t*)data, len));

    const dvi::DVIPage* dvi_page = parser.page(0);
    ASSERT_NE(dvi_page, nullptr);

    // DVI reference: $a + b = c$ should have 5 glyphs
    EXPECT_EQ(dvi_page->glyph_count, 5);

    // Our typesetter
    TypesetOutput* our_out = typeset_simple_math("a+b=c", 10.0f, 0, 0, arena);
    EXPECT_EQ(our_out->glyph_count, 5);

    print_dvi_glyphs(dvi_page);
    print_lambda_output(our_out);

    // Check that characters match
    // Note: DVI uses font-specific character codes which may differ
    // For now, just verify we have the same count
    EXPECT_EQ(our_out->glyph_count, dvi_page->glyph_count);
}

TEST_F(SimpleMathTest, CompareFractionWithDVI) {
    const char* dvi_path = "test/latex/reference/test_fraction.dvi";
    if (!file_exists(dvi_path)) {
        GTEST_SKIP() << "Reference DVI not found: " << dvi_path;
    }

    size_t len;
    const char* data = read_file_contents(dvi_path, &len);
    ASSERT_NE(data, nullptr);

    dvi::DVIParser parser(arena);
    ASSERT_TRUE(parser.parse((const uint8_t*)data, len));

    const dvi::DVIPage* dvi_page = parser.page(0);
    ASSERT_NE(dvi_page, nullptr);

    // Check that DVI has fraction bar (rule)
    EXPECT_GE(dvi_page->rule_count, 1);

    // Our typesetter
    TypesetOutput* our_out = create_typeset_output(arena);
    typeset_fraction("a+b", "c+d", 10.0f, 0, 0, our_out, arena);

    // We should also have a rule
    EXPECT_EQ(our_out->rule_count, 1);

    print_dvi_glyphs(dvi_page);
    print_lambda_output(our_out);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    log_init("log.conf");
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
