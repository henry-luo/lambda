// test_tex_output_gtest.cpp - Unit tests for TeX output and DVI parsing

#include <gtest/gtest.h>
#include "../lambda/tex/tex_output.hpp"
#include "../lambda/tex/dvi_parser.hpp"
#include "../lambda/tex/tex_box.hpp"
#include "../lib/arena.h"

using namespace tex;

class TexOutputTest : public ::testing::Test {
protected:
    Arena arena;

    void SetUp() override {
        arena_init(&arena, 64 * 1024);
    }

    void TearDown() override {
        arena_destroy(&arena);
    }

    // Helper to create a simple char box
    TexBox* make_char_box(int codepoint, float width, float height, float depth) {
        TexBox* box = (TexBox*)arena_alloc(&arena, sizeof(TexBox));
        box->kind = BoxKind::Char;
        box->width = width;
        box->height = height;
        box->depth = depth;
        box->x = 0;
        box->y = 0;
        box->content.ch.codepoint = codepoint;
        return box;
    }

    // Helper to create an hbox
    TexBox* make_hbox(TexBox** children, int count) {
        TexBox* box = (TexBox*)arena_alloc(&arena, sizeof(TexBox));
        box->kind = BoxKind::HBox;
        box->width = 0;
        box->height = 0;
        box->depth = 0;
        box->x = 0;
        box->y = 0;
        box->content.hbox.children = children;
        box->content.hbox.count = count;
        box->content.hbox.capacity = count;

        // compute dimensions
        for (int i = 0; i < count; i++) {
            if (children[i]) {
                box->width += children[i]->width;
                if (children[i]->height > box->height)
                    box->height = children[i]->height;
                if (children[i]->depth > box->depth)
                    box->depth = children[i]->depth;
            }
        }

        return box;
    }
};

// ============================================================================
// JSON Output Tests
// ============================================================================

TEST_F(TexOutputTest, CharBoxToJson) {
    TexBox* box = make_char_box('A', 6.5f, 7.2f, 0.0f);

    JSONOutputOptions opts = JSONOutputOptions::defaults();
    char* json = tex_box_to_json(box, &arena, opts);

    ASSERT_NE(json, nullptr);
    EXPECT_NE(strstr(json, "\"type\": \"char\""), nullptr);
    EXPECT_NE(strstr(json, "\"codepoint\": 65"), nullptr);
    EXPECT_NE(strstr(json, "\"width\": 6.50"), nullptr);
}

TEST_F(TexOutputTest, HBoxToJson) {
    TexBox* children[3];
    children[0] = make_char_box('A', 6.0f, 7.0f, 0.0f);
    children[1] = make_char_box('B', 6.0f, 7.0f, 0.0f);
    children[2] = make_char_box('C', 6.0f, 7.0f, 0.0f);

    TexBox* hbox = make_hbox(children, 3);

    JSONOutputOptions opts = JSONOutputOptions::defaults();
    char* json = tex_box_to_json(hbox, &arena, opts);

    ASSERT_NE(json, nullptr);
    EXPECT_NE(strstr(json, "\"type\": \"hbox\""), nullptr);
    EXPECT_NE(strstr(json, "\"children\""), nullptr);
    EXPECT_NE(strstr(json, "\"width\": 18.00"), nullptr);
}

TEST_F(TexOutputTest, CompactJsonOutput) {
    TexBox* box = make_char_box('X', 5.0f, 6.0f, 1.0f);

    JSONOutputOptions opts = JSONOutputOptions::compact();
    char* json = tex_box_to_json(box, &arena, opts);

    ASSERT_NE(json, nullptr);
    // Compact format should have no newlines in the middle
    // (there might be one at the end)
    const char* first_newline = strchr(json, '\n');
    if (first_newline) {
        EXPECT_EQ(first_newline[1], '\0');  // newline only at end
    }
}

// ============================================================================
// Glyph Extraction Tests
// ============================================================================

TEST_F(TexOutputTest, ExtractGlyphsFromCharBox) {
    TexBox* box = make_char_box('H', 6.0f, 7.0f, 0.0f);

    OutputPage* page = extract_output_page(box, 100.0f, 100.0f, &arena);

    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->glyph_count, 1);
    EXPECT_EQ(page->glyphs[0].codepoint, 'H');
    EXPECT_FLOAT_EQ(page->glyphs[0].x, 0.0f);
    EXPECT_FLOAT_EQ(page->glyphs[0].y, 0.0f);
}

TEST_F(TexOutputTest, ExtractGlyphsFromHBox) {
    TexBox* children[3];
    children[0] = make_char_box('A', 6.0f, 7.0f, 0.0f);
    children[1] = make_char_box('B', 6.0f, 7.0f, 0.0f);
    children[2] = make_char_box('C', 6.0f, 7.0f, 0.0f);

    TexBox* hbox = make_hbox(children, 3);

    OutputPage* page = extract_output_page(hbox, 100.0f, 100.0f, &arena);

    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->glyph_count, 3);

    // Check positions - glyphs should be laid out horizontally
    EXPECT_EQ(page->glyphs[0].codepoint, 'A');
    EXPECT_FLOAT_EQ(page->glyphs[0].x, 0.0f);

    EXPECT_EQ(page->glyphs[1].codepoint, 'B');
    EXPECT_FLOAT_EQ(page->glyphs[1].x, 6.0f);

    EXPECT_EQ(page->glyphs[2].codepoint, 'C');
    EXPECT_FLOAT_EQ(page->glyphs[2].x, 12.0f);
}

// ============================================================================
// DVI Parser Tests
// ============================================================================

TEST_F(TexOutputTest, DVIScaledPointConversion) {
    // 65536 sp = 1 pt
    EXPECT_FLOAT_EQ(dvi::DVIParser::sp_to_pt(65536), 1.0f);
    EXPECT_FLOAT_EQ(dvi::DVIParser::sp_to_pt(131072), 2.0f);
    EXPECT_FLOAT_EQ(dvi::DVIParser::sp_to_pt(32768), 0.5f);
}

TEST_F(TexOutputTest, DVIParserCreation) {
    // Test that parser can be created (without actual DVI data)
    dvi::DVIParser parser;

    // Parser should have no pages initially
    EXPECT_EQ(parser.page_count(), 0);
}

// ============================================================================
// Comparison Tests
// ============================================================================

TEST_F(TexOutputTest, CompareIdenticalOutput) {
    // Create Lambda output
    TexBox* box = make_char_box('A', 6.0f, 7.0f, 0.0f);
    OutputPage* lambda_page = extract_output_page(box, 100.0f, 100.0f, &arena);

    // Create matching DVI page
    dvi::DVIPage dvi_page = {};
    dvi_page.glyphs = (dvi::PositionedGlyph*)arena_alloc(&arena, sizeof(dvi::PositionedGlyph));
    dvi_page.glyph_count = 1;
    dvi_page.glyphs[0].codepoint = 'A';
    dvi_page.glyphs[0].h = 0;  // 0 sp = 0 pt
    dvi_page.glyphs[0].v = 0;

    ComparisonResult result = compare_with_dvi(lambda_page, &dvi_page, 1.0f, &arena);

    EXPECT_TRUE(result.passed);
    EXPECT_EQ(result.matching_glyphs, 1);
    EXPECT_EQ(result.mismatched_glyphs, 0);
    EXPECT_EQ(result.missing_glyphs, 0);
    EXPECT_EQ(result.extra_glyphs, 0);
}

TEST_F(TexOutputTest, CompareWithPositionError) {
    // Create Lambda output
    TexBox* box = make_char_box('A', 6.0f, 7.0f, 0.0f);
    box->x = 2.0f;  // Offset position
    OutputPage* lambda_page = extract_output_page(box, 100.0f, 100.0f, &arena);

    // Create DVI page at different position
    dvi::DVIPage dvi_page = {};
    dvi_page.glyphs = (dvi::PositionedGlyph*)arena_alloc(&arena, sizeof(dvi::PositionedGlyph));
    dvi_page.glyph_count = 1;
    dvi_page.glyphs[0].codepoint = 'A';
    dvi_page.glyphs[0].h = 0;  // at x=0
    dvi_page.glyphs[0].v = 0;

    // With tight tolerance, should fail
    ComparisonResult result1 = compare_with_dvi(lambda_page, &dvi_page, 1.0f, &arena);
    EXPECT_FALSE(result1.passed);
    EXPECT_EQ(result1.mismatched_glyphs, 1);

    // With looser tolerance, should pass
    ComparisonResult result2 = compare_with_dvi(lambda_page, &dvi_page, 3.0f, &arena);
    EXPECT_TRUE(result2.passed);
    EXPECT_EQ(result2.matching_glyphs, 1);
}

TEST_F(TexOutputTest, CompareMissingGlyph) {
    // Lambda output with 2 glyphs
    TexBox* children[2];
    children[0] = make_char_box('A', 6.0f, 7.0f, 0.0f);
    children[1] = make_char_box('B', 6.0f, 7.0f, 0.0f);
    TexBox* hbox = make_hbox(children, 2);
    OutputPage* lambda_page = extract_output_page(hbox, 100.0f, 100.0f, &arena);

    // DVI page with 3 glyphs
    dvi::DVIPage dvi_page = {};
    dvi_page.glyphs = (dvi::PositionedGlyph*)arena_alloc(&arena, 3 * sizeof(dvi::PositionedGlyph));
    dvi_page.glyph_count = 3;
    dvi_page.glyphs[0].codepoint = 'A';
    dvi_page.glyphs[0].h = 0;
    dvi_page.glyphs[0].v = 0;
    dvi_page.glyphs[1].codepoint = 'B';
    dvi_page.glyphs[1].h = 6 * 65536;  // 6pt in sp
    dvi_page.glyphs[1].v = 0;
    dvi_page.glyphs[2].codepoint = 'C';
    dvi_page.glyphs[2].h = 12 * 65536;  // 12pt in sp
    dvi_page.glyphs[2].v = 0;

    ComparisonResult result = compare_with_dvi(lambda_page, &dvi_page, 1.0f, &arena);

    EXPECT_FALSE(result.passed);
    EXPECT_EQ(result.missing_glyphs, 1);  // 'C' is missing from Lambda
}

// ============================================================================
// Box Tree Dump Tests
// ============================================================================

TEST_F(TexOutputTest, DumpTexBoxTree) {
    TexBox* children[2];
    children[0] = make_char_box('H', 6.0f, 7.0f, 0.0f);
    children[1] = make_char_box('i', 3.0f, 7.0f, 0.0f);
    TexBox* hbox = make_hbox(children, 2);

    // Just verify it doesn't crash
    dump_tex_box_tree(hbox, stdout, 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
