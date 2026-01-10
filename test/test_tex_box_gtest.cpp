// test_tex_box_gtest.cpp - Unit tests for TeX box model and glue

#include <gtest/gtest.h>
#include "../lambda/tex/tex_box.hpp"
#include "../lambda/tex/tex_glue.hpp"
#include "../lib/arena.h"

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class TexBoxTest : public ::testing::Test {
protected:
    Arena arena;

    void SetUp() override {
        arena_init(&arena, 64 * 1024);
    }

    void TearDown() override {
        arena_destroy(&arena);
    }
};

// ============================================================================
// Glue Tests
// ============================================================================

TEST_F(TexBoxTest, GlueCreation) {
    Glue g = Glue::fixed(10.0f);

    EXPECT_FLOAT_EQ(g.space, 10.0f);
    EXPECT_FLOAT_EQ(g.stretch, 0.0f);
    EXPECT_FLOAT_EQ(g.shrink, 0.0f);
    EXPECT_EQ(g.stretch_order, 0);
    EXPECT_EQ(g.shrink_order, 0);
}

TEST_F(TexBoxTest, GlueWithStretch) {
    Glue g = Glue::stretchable(10.0f, 5.0f, 3.0f);

    EXPECT_FLOAT_EQ(g.space, 10.0f);
    EXPECT_FLOAT_EQ(g.stretch, 5.0f);
    EXPECT_FLOAT_EQ(g.shrink, 3.0f);
}

TEST_F(TexBoxTest, GlueFil) {
    Glue g = Glue::fil();

    EXPECT_GT(g.stretch_order, 0);  // Infinite stretch
}

TEST_F(TexBoxTest, GlueFill) {
    Glue g = Glue::fill();

    EXPECT_GT(g.stretch_order, Glue::fil().stretch_order);  // More infinite
}

TEST_F(TexBoxTest, GlueFilll) {
    Glue g = Glue::filll();

    EXPECT_GT(g.stretch_order, Glue::fill().stretch_order);
}

TEST_F(TexBoxTest, GlueAddition) {
    Glue a = { 10.0f, 5.0f, 2.0f, 0, 0 };
    Glue b = { 20.0f, 3.0f, 1.0f, 0, 0 };

    Glue sum = a + b;

    EXPECT_FLOAT_EQ(sum.space, 30.0f);
    EXPECT_FLOAT_EQ(sum.stretch, 8.0f);
    EXPECT_FLOAT_EQ(sum.shrink, 3.0f);
}

TEST_F(TexBoxTest, GlueScaling) {
    Glue g = { 10.0f, 4.0f, 2.0f, 0, 0 };
    Glue scaled = g * 2.0f;

    EXPECT_FLOAT_EQ(scaled.space, 20.0f);
    EXPECT_FLOAT_EQ(scaled.stretch, 8.0f);
    EXPECT_FLOAT_EQ(scaled.shrink, 4.0f);
}

// ============================================================================
// Box Creation Tests
// ============================================================================

TEST_F(TexBoxTest, CharBoxCreation) {
    TexBox* box = make_char_box(&arena, 'A', 6.5f, 7.2f, 0.0f);

    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->kind, BoxKind::Char);
    EXPECT_EQ(box->content.ch.codepoint, 'A');
    EXPECT_FLOAT_EQ(box->width, 6.5f);
    EXPECT_FLOAT_EQ(box->height, 7.2f);
    EXPECT_FLOAT_EQ(box->depth, 0.0f);
}

TEST_F(TexBoxTest, RuleBoxCreation) {
    TexBox* box = make_rule_box(&arena, 100.0f, 0.4f, 0.0f);

    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->kind, BoxKind::Rule);
    EXPECT_FLOAT_EQ(box->width, 100.0f);
    EXPECT_FLOAT_EQ(box->height, 0.4f);
}

TEST_F(TexBoxTest, GlueBoxCreation) {
    Glue g = { 10.0f, 5.0f, 3.0f, 0, 0 };
    TexBox* box = make_glue_box(&arena, g);

    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->kind, BoxKind::Glue);
    EXPECT_FLOAT_EQ(box->content.glue.space, 10.0f);
    EXPECT_FLOAT_EQ(box->content.glue.stretch, 5.0f);
    EXPECT_FLOAT_EQ(box->content.glue.shrink, 3.0f);
}

TEST_F(TexBoxTest, KernBoxCreation) {
    TexBox* box = make_kern_box(&arena, 5.0f);

    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->kind, BoxKind::Kern);
    EXPECT_FLOAT_EQ(box->width, 5.0f);
}

TEST_F(TexBoxTest, HBoxCreation) {
    TexBox* box = make_hbox(&arena, 8);

    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->kind, BoxKind::HBox);
    EXPECT_GE(box->content.hbox.capacity, 8);
    EXPECT_EQ(box->content.hbox.count, 0);
}

TEST_F(TexBoxTest, VBoxCreation) {
    TexBox* box = make_vbox(&arena, 4);

    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->kind, BoxKind::VBox);
    EXPECT_GE(box->content.vbox.capacity, 4);
    EXPECT_EQ(box->content.vbox.count, 0);
}

// ============================================================================
// HBox Tests
// ============================================================================

TEST_F(TexBoxTest, HBoxAddChild) {
    TexBox* hbox = make_hbox(&arena, 4);
    TexBox* child1 = make_char_box(&arena, 'A', 6.0f, 7.0f, 0.0f);
    TexBox* child2 = make_char_box(&arena, 'B', 6.0f, 7.0f, 0.0f);

    hbox_add_child(hbox, child1);
    hbox_add_child(hbox, child2);

    EXPECT_EQ(hbox->content.hbox.count, 2);
    EXPECT_EQ(hbox->content.hbox.children[0], child1);
    EXPECT_EQ(hbox->content.hbox.children[1], child2);
}

TEST_F(TexBoxTest, HBoxNaturalWidth) {
    TexBox* hbox = make_hbox(&arena, 4);
    hbox_add_child(hbox, make_char_box(&arena, 'A', 6.0f, 7.0f, 0.0f));
    hbox_add_child(hbox, make_char_box(&arena, 'B', 6.0f, 7.0f, 0.0f));
    hbox_add_child(hbox, make_char_box(&arena, 'C', 6.0f, 7.0f, 0.0f));

    hbox_compute_dimensions(hbox);

    EXPECT_FLOAT_EQ(hbox->width, 18.0f);  // 6 * 3
    EXPECT_FLOAT_EQ(hbox->height, 7.0f);
    EXPECT_FLOAT_EQ(hbox->depth, 0.0f);
}

TEST_F(TexBoxTest, HBoxWithGlue) {
    TexBox* hbox = make_hbox(&arena, 4);
    hbox_add_child(hbox, make_char_box(&arena, 'A', 6.0f, 7.0f, 0.0f));
    hbox_add_child(hbox, make_glue_box(&arena, { 10.0f, 5.0f, 3.0f, 0, 0 }));
    hbox_add_child(hbox, make_char_box(&arena, 'B', 6.0f, 7.0f, 0.0f));

    hbox_compute_dimensions(hbox);

    EXPECT_FLOAT_EQ(hbox->width, 22.0f);  // 6 + 10 + 6
}

TEST_F(TexBoxTest, HBoxHeightDepthComputation) {
    TexBox* hbox = make_hbox(&arena, 4);
    hbox_add_child(hbox, make_char_box(&arena, 'A', 6.0f, 7.0f, 0.0f));   // no depth
    hbox_add_child(hbox, make_char_box(&arena, 'g', 5.0f, 5.0f, 2.0f));   // has depth
    hbox_add_child(hbox, make_char_box(&arena, 'l', 3.0f, 10.0f, 0.0f));  // tall

    hbox_compute_dimensions(hbox);

    EXPECT_FLOAT_EQ(hbox->height, 10.0f);  // max height
    EXPECT_FLOAT_EQ(hbox->depth, 2.0f);    // max depth
}

// ============================================================================
// VBox Tests
// ============================================================================

TEST_F(TexBoxTest, VBoxAddChild) {
    TexBox* vbox = make_vbox(&arena, 4);
    TexBox* child1 = make_hbox(&arena, 1);
    child1->width = 100.0f;
    child1->height = 10.0f;

    TexBox* child2 = make_hbox(&arena, 1);
    child2->width = 100.0f;
    child2->height = 10.0f;

    vbox_add_child(vbox, child1);
    vbox_add_child(vbox, child2);

    EXPECT_EQ(vbox->content.vbox.count, 2);
}

TEST_F(TexBoxTest, VBoxNaturalHeight) {
    TexBox* vbox = make_vbox(&arena, 4);

    TexBox* line1 = make_hbox(&arena, 1);
    line1->height = 10.0f;
    line1->depth = 2.0f;

    TexBox* line2 = make_hbox(&arena, 1);
    line2->height = 10.0f;
    line2->depth = 2.0f;

    vbox_add_child(vbox, line1);
    vbox_add_child(vbox, line2);

    vbox_compute_dimensions(vbox);

    // Total height = h1 + d1 + h2 (last box's depth not included in vbox height)
    EXPECT_FLOAT_EQ(vbox->height + vbox->depth, 24.0f);  // 10+2+10+2
}

TEST_F(TexBoxTest, VBoxWidth) {
    TexBox* vbox = make_vbox(&arena, 4);

    TexBox* line1 = make_hbox(&arena, 1);
    line1->width = 100.0f;

    TexBox* line2 = make_hbox(&arena, 1);
    line2->width = 150.0f;  // wider

    vbox_add_child(vbox, line1);
    vbox_add_child(vbox, line2);

    vbox_compute_dimensions(vbox);

    EXPECT_FLOAT_EQ(vbox->width, 150.0f);  // max width
}

// ============================================================================
// Box Setting (Glue Distribution) Tests
// ============================================================================

TEST_F(TexBoxTest, HBoxSetToWidth_Stretch) {
    TexBox* hbox = make_hbox(&arena, 4);
    hbox_add_child(hbox, make_char_box(&arena, 'A', 10.0f, 7.0f, 0.0f));
    hbox_add_child(hbox, make_glue_box(&arena, { 10.0f, 10.0f, 5.0f, 0, 0 }));
    hbox_add_child(hbox, make_char_box(&arena, 'B', 10.0f, 7.0f, 0.0f));

    // Natural width = 30, set to 40 (stretch by 10)
    hbox_set_to_width(hbox, 40.0f);

    EXPECT_FLOAT_EQ(hbox->width, 40.0f);
    // Glue should have stretched to fill the extra space
}

TEST_F(TexBoxTest, HBoxSetToWidth_Shrink) {
    TexBox* hbox = make_hbox(&arena, 4);
    hbox_add_child(hbox, make_char_box(&arena, 'A', 10.0f, 7.0f, 0.0f));
    hbox_add_child(hbox, make_glue_box(&arena, { 10.0f, 10.0f, 5.0f, 0, 0 }));
    hbox_add_child(hbox, make_char_box(&arena, 'B', 10.0f, 7.0f, 0.0f));

    // Natural width = 30, set to 25 (shrink by 5)
    hbox_set_to_width(hbox, 25.0f);

    EXPECT_FLOAT_EQ(hbox->width, 25.0f);
}

TEST_F(TexBoxTest, HBoxSetToWidth_FilGlue) {
    TexBox* hbox = make_hbox(&arena, 4);
    hbox_add_child(hbox, make_char_box(&arena, 'A', 10.0f, 7.0f, 0.0f));
    hbox_add_child(hbox, make_glue_box(&arena, Glue::fil()));
    hbox_add_child(hbox, make_char_box(&arena, 'B', 10.0f, 7.0f, 0.0f));

    // With fil glue, can stretch to any width
    hbox_set_to_width(hbox, 500.0f);

    EXPECT_FLOAT_EQ(hbox->width, 500.0f);
}

// ============================================================================
// Fraction Box Tests
// ============================================================================

TEST_F(TexBoxTest, FractionBoxCreation) {
    TexBox* num = make_char_box(&arena, 'a', 5.0f, 5.0f, 0.0f);
    TexBox* denom = make_char_box(&arena, 'b', 5.0f, 5.0f, 2.0f);

    TexBox* frac = make_fraction_box(&arena, num, denom, 0.4f);

    ASSERT_NE(frac, nullptr);
    EXPECT_EQ(frac->kind, BoxKind::Fraction);
    EXPECT_EQ(frac->content.fraction.numerator, num);
    EXPECT_EQ(frac->content.fraction.denominator, denom);
    EXPECT_FLOAT_EQ(frac->content.fraction.rule_thickness, 0.4f);
}

TEST_F(TexBoxTest, FractionBoxDimensions) {
    TexBox* num = make_char_box(&arena, 'a', 5.0f, 5.0f, 0.0f);
    TexBox* denom = make_char_box(&arena, 'b', 5.0f, 5.0f, 2.0f);

    TexBox* frac = make_fraction_box(&arena, num, denom, 0.4f);
    fraction_compute_dimensions(frac);

    // Fraction should be taller than either component
    EXPECT_GT(frac->height + frac->depth, num->height + denom->height);
    // Width should be max of num and denom
    EXPECT_GE(frac->width, 5.0f);
}

// ============================================================================
// Radical Box Tests
// ============================================================================

TEST_F(TexBoxTest, RadicalBoxCreation) {
    TexBox* radicand = make_char_box(&arena, 'x', 5.0f, 5.0f, 0.0f);

    TexBox* radical = make_radical_box(&arena, radicand, nullptr, 0.4f);

    ASSERT_NE(radical, nullptr);
    EXPECT_EQ(radical->kind, BoxKind::Radical);
    EXPECT_EQ(radical->content.radical.radicand, radicand);
    EXPECT_EQ(radical->content.radical.index, nullptr);
}

TEST_F(TexBoxTest, RadicalWithIndex) {
    TexBox* radicand = make_char_box(&arena, 'x', 5.0f, 5.0f, 0.0f);
    TexBox* index = make_char_box(&arena, '3', 3.0f, 4.0f, 0.0f);

    TexBox* radical = make_radical_box(&arena, radicand, index, 0.4f);

    EXPECT_NE(radical->content.radical.index, nullptr);
}

// ============================================================================
// Delimiter Box Tests
// ============================================================================

TEST_F(TexBoxTest, DelimiterBoxCreation) {
    TexBox* delim = make_delimiter_box(&arena, '(', true, 10.0f);

    ASSERT_NE(delim, nullptr);
    EXPECT_EQ(delim->kind, BoxKind::Delimiter);
    EXPECT_EQ(delim->content.delimiter.codepoint, '(');
    EXPECT_TRUE(delim->content.delimiter.is_left);
}

TEST_F(TexBoxTest, DelimiterSizing) {
    // Delimiters should scale to match content height
    TexBox* small_delim = make_delimiter_box(&arena, '(', true, 10.0f);
    TexBox* large_delim = make_delimiter_box(&arena, '(', true, 30.0f);

    EXPECT_GT(large_delim->height + large_delim->depth,
              small_delim->height + small_delim->depth);
}

// ============================================================================
// Penalty Tests
// ============================================================================

TEST_F(TexBoxTest, PenaltyValues) {
    EXPECT_EQ(PENALTY_INFINITY, 10000);
    EXPECT_EQ(PENALTY_NEG_INFINITY, -10000);
}

TEST_F(TexBoxTest, PenaltyBox) {
    TexBox* box = make_penalty_box(&arena, 100);

    ASSERT_NE(box, nullptr);
    EXPECT_FLOAT_EQ(box->width, 0.0f);  // Penalties have no width
}

// ============================================================================
// Box Shift Tests
// ============================================================================

TEST_F(TexBoxTest, BoxShift) {
    TexBox* box = make_char_box(&arena, 'x', 5.0f, 5.0f, 2.0f);

    // Shift down
    box->y = 3.0f;

    EXPECT_FLOAT_EQ(box->y, 3.0f);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
