// test_tex_paragraph_gtest.cpp - Unit tests for TeX paragraph and line breaking

#include <gtest/gtest.h>
#include "../lambda/tex/tex_paragraph.hpp"
#include "../lambda/tex/tex_box.hpp"
#include "../lambda/tex/tex_glue.hpp"
#include "../lib/arena.h"

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class ParagraphTest : public ::testing::Test {
protected:
    Arena arena;
    LineBreakParams params;

    void SetUp() override {
        arena_init(&arena, 64 * 1024);

        // Default parameters
        params.line_width = 300.0f;      // 300pt line width
        params.tolerance = 200;          // Reasonable tolerance
        params.pretolerance = 100;
        params.line_penalty = 10;
        params.hyphen_penalty = 50;
        params.exhyphen_penalty = 50;
        params.looseness = 0;
        params.left_skip = Glue::fixed(0);
        params.right_skip = Glue::fixed(0);
        params.parfill_skip = Glue::fil();
    }

    void TearDown() override {
        arena_destroy(&arena);
    }

    // Helper to create a word (sequence of char boxes)
    void add_word(HList* hlist, const char* word, float char_width = 6.0f) {
        for (const char* p = word; *p; p++) {
            TexBox* box = make_char_box(&arena, *p, char_width, 7.0f, 0.0f);
            hlist_add(hlist, box);
        }
    }

    // Helper to add interword glue
    void add_space(HList* hlist) {
        TexBox* box = make_glue_box(&arena, Glue::stretchable(4.0f, 2.0f, 1.0f));
        hlist_add(hlist, box);
    }

    // Helper to add a discretionary break point
    void add_discretionary(HList* hlist) {
        TexBox* box = make_penalty_box(&arena, params.hyphen_penalty);
        hlist_add(hlist, box);
    }
};

// ============================================================================
// HList Tests
// ============================================================================

TEST_F(ParagraphTest, HListCreation) {
    HList* hlist = hlist_create(&arena);

    ASSERT_NE(hlist, nullptr);
    EXPECT_EQ(hlist->count, 0);
}

TEST_F(ParagraphTest, HListAddItem) {
    HList* hlist = hlist_create(&arena);
    TexBox* box = make_char_box(&arena, 'A', 6.0f, 7.0f, 0.0f);

    hlist_add(hlist, box);

    EXPECT_EQ(hlist->count, 1);
}

TEST_F(ParagraphTest, HListNaturalWidth) {
    HList* hlist = hlist_create(&arena);
    add_word(hlist, "Hello");  // 5 chars * 6pt = 30pt

    float width = hlist_natural_width(hlist);

    EXPECT_FLOAT_EQ(width, 30.0f);
}

TEST_F(ParagraphTest, HListWithGlue) {
    HList* hlist = hlist_create(&arena);
    add_word(hlist, "Hello");
    add_space(hlist);
    add_word(hlist, "World");

    float width = hlist_natural_width(hlist);

    // 30 + 4 + 30 = 64
    EXPECT_FLOAT_EQ(width, 64.0f);
}

// ============================================================================
// Line Breaking - Simple Cases
// ============================================================================

TEST_F(ParagraphTest, SingleWordNoBreak) {
    HList* hlist = hlist_create(&arena);
    add_word(hlist, "Hello");

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    ASSERT_NE(vbox, nullptr);
    EXPECT_EQ(vbox->kind, BoxKind::VBox);
    // Single word should fit on one line
    EXPECT_EQ(vbox->content.vbox.count, 1);
}

TEST_F(ParagraphTest, TwoWordsOnOneLine) {
    HList* hlist = hlist_create(&arena);
    add_word(hlist, "Hello");  // 30pt
    add_space(hlist);          // 4pt
    add_word(hlist, "World");  // 30pt
    // Total: 64pt, fits in 300pt line

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    ASSERT_NE(vbox, nullptr);
    EXPECT_EQ(vbox->content.vbox.count, 1);
}

TEST_F(ParagraphTest, ForcedLineBreak) {
    HList* hlist = hlist_create(&arena);
    add_word(hlist, "First");
    add_space(hlist);

    // Add forced break (penalty = -infinity)
    TexBox* penalty = make_penalty_box(&arena, PENALTY_NEG_INFINITY);
    hlist_add(hlist, penalty);

    add_word(hlist, "Second");

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    ASSERT_NE(vbox, nullptr);
    // Should have 2 lines
    EXPECT_EQ(vbox->content.vbox.count, 2);
}

// ============================================================================
// Line Breaking - Multiple Lines
// ============================================================================

TEST_F(ParagraphTest, LongParagraphBreaking) {
    HList* hlist = hlist_create(&arena);

    // Create paragraph longer than line width
    const char* words[] = {
        "The", "quick", "brown", "fox", "jumps", "over",
        "the", "lazy", "dog", "and", "continues", "running"
    };

    for (int i = 0; i < 12; i++) {
        if (i > 0) add_space(hlist);
        add_word(hlist, words[i]);
    }

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    ASSERT_NE(vbox, nullptr);
    // Should need multiple lines
    EXPECT_GT(vbox->content.vbox.count, 1);
}

TEST_F(ParagraphTest, LineBreakAtOptimalPoints) {
    HList* hlist = hlist_create(&arena);

    // Words that should break at specific points
    add_word(hlist, "Word1");
    add_space(hlist);
    add_word(hlist, "Word2");
    add_space(hlist);
    add_word(hlist, "Word3");

    params.line_width = 40.0f;  // Very narrow line

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    ASSERT_NE(vbox, nullptr);
    // Each word should be on its own line
    EXPECT_GE(vbox->content.vbox.count, 2);
}

// ============================================================================
// Glue Distribution Tests
// ============================================================================

TEST_F(ParagraphTest, GlueStretchToFill) {
    HList* hlist = hlist_create(&arena);
    add_word(hlist, "Short");  // 30pt

    params.line_width = 100.0f;

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    ASSERT_NE(vbox, nullptr);
    // Line should be set to full width
    if (vbox->content.vbox.count > 0) {
        TexBox* line = vbox->content.vbox.children[0];
        EXPECT_NEAR(line->width, params.line_width, 1.0f);
    }
}

TEST_F(ParagraphTest, GlueShrinkToFit) {
    HList* hlist = hlist_create(&arena);

    // Add words with flexible glue that needs to shrink
    add_word(hlist, "Hello");
    hlist_add(hlist, make_glue_box(&arena, Glue::stretchable(20.0f, 5.0f, 10.0f)));
    add_word(hlist, "World");
    hlist_add(hlist, make_glue_box(&arena, Glue::stretchable(20.0f, 5.0f, 10.0f)));
    add_word(hlist, "Test");

    // Natural width: 30 + 20 + 30 + 20 + 24 = 124
    // Set line width to require shrinking
    params.line_width = 110.0f;

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    ASSERT_NE(vbox, nullptr);
}

// ============================================================================
// Badness Calculation Tests
// ============================================================================

TEST_F(ParagraphTest, PerfectFitBadness) {
    // Zero badness when natural width matches target
    float badness = calculate_badness(0.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(badness, 0.0f);
}

TEST_F(ParagraphTest, StretchBadness) {
    // Badness increases with stretch ratio
    float badness1 = calculate_badness(10.0f, 20.0f, 0.0f);  // half stretch
    float badness2 = calculate_badness(10.0f, 10.0f, 0.0f);  // full stretch

    EXPECT_GT(badness2, badness1);
}

TEST_F(ParagraphTest, InfiniteBadness) {
    // Overfull box has infinite badness
    float badness = calculate_badness(-10.0f, 0.0f, 5.0f);  // needs shrink but not enough

    EXPECT_GE(badness, 10000.0f);  // INFINITY badness
}

// ============================================================================
// Penalty Tests
// ============================================================================

TEST_F(ParagraphTest, HighPenaltyPreventsBreak) {
    HList* hlist = hlist_create(&arena);
    add_word(hlist, "Word");
    hlist_add(hlist, make_penalty_box(&arena, PENALTY_INFINITY));
    add_word(hlist, "Word");

    params.line_width = 30.0f;  // Would need break between words

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    ASSERT_NE(vbox, nullptr);
    // Break should be avoided due to infinite penalty
}

TEST_F(ParagraphTest, NegativePenaltyEncouragesBreak) {
    HList* hlist = hlist_create(&arena);
    add_word(hlist, "First");
    add_space(hlist);
    hlist_add(hlist, make_penalty_box(&arena, -100));  // Encourage break
    add_word(hlist, "Second");

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    ASSERT_NE(vbox, nullptr);
}

// ============================================================================
// Hyphenation Tests
// ============================================================================

TEST_F(ParagraphTest, HyphenationPoints) {
    HList* hlist = hlist_create(&arena);

    // Add word with hyphenation points: "hyphen-ation"
    add_word(hlist, "hyphen", 6.0f);
    add_discretionary(hlist);  // Potential break after "hyphen"
    add_word(hlist, "ation", 6.0f);

    params.line_width = 50.0f;  // Needs to break

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    ASSERT_NE(vbox, nullptr);
}

// ============================================================================
// Paragraph Skip Tests
// ============================================================================

TEST_F(ParagraphTest, LeftSkip) {
    HList* hlist = hlist_create(&arena);
    add_word(hlist, "Indented");

    params.left_skip = Glue::fixed(20.0f);

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    ASSERT_NE(vbox, nullptr);
    // Line should have left indent
}

TEST_F(ParagraphTest, RightSkip) {
    HList* hlist = hlist_create(&arena);
    add_word(hlist, "Text");

    params.right_skip = Glue::fixed(20.0f);

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    ASSERT_NE(vbox, nullptr);
}

TEST_F(ParagraphTest, ParfillSkip) {
    HList* hlist = hlist_create(&arena);
    add_word(hlist, "Last");
    add_space(hlist);
    add_word(hlist, "line");

    // parfillskip fills the end of last line
    params.parfill_skip = Glue::fil();

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    ASSERT_NE(vbox, nullptr);
}

// ============================================================================
// Looseness Tests
// ============================================================================

TEST_F(ParagraphTest, LoosenessPlusOne) {
    HList* hlist = hlist_create(&arena);

    // Create paragraph that could fit in 2 or 3 lines
    for (int i = 0; i < 8; i++) {
        if (i > 0) add_space(hlist);
        add_word(hlist, "word");
    }

    params.line_width = 100.0f;

    // First, get normal line count
    TexBox* vbox_normal = break_paragraph(hlist, &params, &arena);
    int normal_lines = vbox_normal->content.vbox.count;

    // Now with looseness +1
    params.looseness = 1;
    TexBox* vbox_loose = break_paragraph(hlist, &params, &arena);

    // Should have one more line if possible
    EXPECT_GE(vbox_loose->content.vbox.count, normal_lines);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(ParagraphTest, EmptyParagraph) {
    HList* hlist = hlist_create(&arena);

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    // Empty paragraph should produce empty vbox
    ASSERT_NE(vbox, nullptr);
    EXPECT_EQ(vbox->content.vbox.count, 0);
}

TEST_F(ParagraphTest, OnlyGlue) {
    HList* hlist = hlist_create(&arena);
    add_space(hlist);
    add_space(hlist);

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    ASSERT_NE(vbox, nullptr);
}

TEST_F(ParagraphTest, VeryLongWord) {
    HList* hlist = hlist_create(&arena);

    // Word longer than line width
    add_word(hlist, "supercalifragilisticexpialidocious", 6.0f);

    params.line_width = 100.0f;  // Word is ~200pt

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    // Should handle overfull box
    ASSERT_NE(vbox, nullptr);
}

// ============================================================================
// Line Dimensions Tests
// ============================================================================

TEST_F(ParagraphTest, LineHeight) {
    HList* hlist = hlist_create(&arena);

    // Add characters with varying heights
    hlist_add(hlist, make_char_box(&arena, 'a', 5.0f, 5.0f, 0.0f));  // short
    hlist_add(hlist, make_char_box(&arena, 'l', 3.0f, 10.0f, 0.0f)); // tall
    hlist_add(hlist, make_char_box(&arena, 'g', 5.0f, 5.0f, 3.0f));  // descender

    TexBox* vbox = break_paragraph(hlist, &params, &arena);

    ASSERT_NE(vbox, nullptr);
    if (vbox->content.vbox.count > 0) {
        TexBox* line = vbox->content.vbox.children[0];
        EXPECT_FLOAT_EQ(line->height, 10.0f);  // max height
        EXPECT_FLOAT_EQ(line->depth, 3.0f);    // max depth
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
